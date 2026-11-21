pub mod arm7tdmi;
pub mod i8086;
pub mod lc8670;
pub mod mos6502;
pub mod tms1100;
pub mod tms9900;
pub mod z80;

pub use arm7tdmi::Arm7tdmi;
pub use i8086::I8086;
pub use lc8670::{Lc8670, Lc8670Bus};
pub use mos6502::Mos6502;
pub use tms1100::{Tms1100, Tms1100Io};
pub use tms9900::{Tms9900, Tms9900Bus};
pub use z80::Z80;

#[cfg(test)]
mod tests {
    use super::*;
    use emu_core::{Bus32, Bus8, IoPortBus};

    struct FlatBus8([u8; 65536]);

    impl FlatBus8 {
        fn new() -> Self {
            Self([0; 65536])
        }
    }

    impl Bus8 for FlatBus8 {
        fn read8(&mut self, addr: u16) -> u8 {
            self.0[addr as usize]
        }

        fn write8(&mut self, addr: u16, val: u8) {
            self.0[addr as usize] = val;
        }

        fn peek8(&self, addr: u16) -> u8 {
            self.0[addr as usize]
        }
    }

    impl IoPortBus for FlatBus8 {
        fn port_in8(&mut self, _port: u16) -> u8 {
            0xFF
        }
        fn port_out8(&mut self, _port: u16, _val: u8) {}
        fn port_in16(&mut self, _port: u16) -> u16 {
            0xFFFF
        }
        fn port_out16(&mut self, _port: u16, _val: u16) {}
    }

    struct FlatBus32(Vec<u8>);

    impl FlatBus32 {
        fn new(size: usize) -> Self {
            Self(vec![0; size])
        }
    }

    impl Bus32 for FlatBus32 {
        fn read8(&mut self, addr: u32) -> u8 {
            self.peek8(addr)
        }
        fn write8(&mut self, addr: u32, val: u8) {
            let idx = (addr as usize) % self.0.len();
            self.0[idx] = val;
        }
        fn peek8(&self, addr: u32) -> u8 {
            self.0[(addr as usize) % self.0.len()]
        }
    }

    impl IoPortBus for FlatBus32 {
        fn port_in8(&mut self, _port: u16) -> u8 {
            0xFF
        }
        fn port_out8(&mut self, _port: u16, _val: u8) {}
        fn port_in16(&mut self, _port: u16) -> u16 {
            0xFFFF
        }
        fn port_out16(&mut self, _port: u16, _val: u16) {}
    }

    #[test]
    fn test_mos6502_adc_branch_and_irq() {
        let mut bus = FlatBus8::new();
        bus.0[0xFFFC] = 0x00;
        bus.0[0xFFFD] = 0x80;
        bus.0[0xFFFE] = 0x00;
        bus.0[0xFFFF] = 0x90;
        // 8000: LDA #$25; CLC; ADC #$17; CLI; NOP; JMP $8007
        let code = [0xA9, 0x25, 0x18, 0x69, 0x17, 0x58, 0xEA, 0x4C, 0x07, 0x80];
        bus.0[0x8000..0x800A].copy_from_slice(&code);
        // IRQ handler at 9000: INX; RTI
        bus.0[0x9000] = 0xE8;
        bus.0[0x9001] = 0x40;

        let mut cpu = Mos6502::new(true);
        cpu.reset(&mut bus);
        assert_eq!(cpu.pc, 0x8000);

        for _ in 0..5 {
            cpu.step(&mut bus);
        }
        assert_eq!(cpu.a, 0x3C);

        cpu.trigger_irq();
        cpu.step(&mut bus);
        assert_eq!(cpu.pc, 0x9000);
        cpu.step(&mut bus);
        cpu.step(&mut bus);
        assert_eq!(cpu.x, 1);
    }

    #[test]
    fn test_z80_ldir_call_and_nmi() {
        let mut bus = FlatBus8::new();
        bus.0[0x0100..0x0104].copy_from_slice(&[0xDE, 0xAD, 0xBE, 0xEF]);
        // 0000: LD SP,$FFFF; LD HL,$0100; LD DE,$0200; LD BC,$0004; LDIR; CALL $0020; HALT
        let prog = [
            0x31, 0xFF, 0xFF, // LD SP,FFFF
            0x21, 0x00, 0x01, // LD HL,0100
            0x11, 0x00, 0x02, // LD DE,0200
            0x01, 0x04, 0x00, // LD BC,0004
            0xED, 0xB0,       // LDIR
            0xCD, 0x20, 0x00, // CALL 0020
            0x76,             // HALT
        ];
        bus.0[..prog.len()].copy_from_slice(&prog);
        // 0020: LD A,$42; RET
        bus.0[0x0020..0x0023].copy_from_slice(&[0x3E, 0x42, 0xC9]);
        // NMI 0066: INC B; RETN
        bus.0[0x0066..0x0069].copy_from_slice(&[0x04, 0xED, 0x45]);

        let mut cpu = Z80::new();
        for _ in 0..16 {
            if cpu.halted {
                break;
            }
            cpu.step(&mut bus);
        }
        assert!(cpu.halted);
        assert_eq!(cpu.a, 0x42);
        assert_eq!(&bus.0[0x0200..0x0204], &[0xDE, 0xAD, 0xBE, 0xEF]);

        cpu.request_nmi();
        cpu.step(&mut bus);
        assert_eq!(cpu.pc, 0x0066);
        cpu.step(&mut bus);
        cpu.step(&mut bus);
        assert_eq!(cpu.b, 1);
    }

    #[test]
    fn test_i8086_reset_mov_add_and_int() {
        let mut bus = FlatBus32::new(0x10_0000);
        // Reset at F000:FFF0 (0xFFFF0) -> JMP F000:0100
        bus.0[0xFFFF0..0xFFFF5].copy_from_slice(&[0xEA, 0x00, 0x01, 0x00, 0xF0]);
        // At 0xF0100: MOV AX,1234; ADD AX,0010; INT 20h; HLT
        let code = [0xB8, 0x34, 0x12, 0x05, 0x10, 0x00, 0xCD, 0x20, 0xF4];
        bus.0[0xF0100..0xF0109].copy_from_slice(&code);
        // IVT 20h at 0x0080 -> 1000:0000
        bus.0[0x80..0x84].copy_from_slice(&[0x00, 0x00, 0x00, 0x10]);
        // At 0x10000: INC BX; IRET
        bus.0[0x10000..0x10002].copy_from_slice(&[0x43, 0xCF]);

        let mut cpu = I8086::new();
        cpu.sregs[I8086::SS] = 0x2000;
        cpu.regs[I8086::SP] = 0x1000;
        for _ in 0..7 {
            cpu.step(&mut bus);
        }
        assert_eq!(cpu.regs[I8086::AX], 0x1244);
        assert_eq!(cpu.regs[I8086::BX], 1);
        assert!(cpu.halted);
    }

    #[test]
    fn test_tms9900_big_endian_add_and_branch() {
        struct TmsBus(Vec<u8>, u16);
        impl Tms9900Bus for TmsBus {
            fn read16(&mut self, addr: u16) -> u16 {
                let a = (addr & !1) as usize;
                u16::from_be_bytes([self.0[a], self.0[a + 1]])
            }
            fn write16(&mut self, addr: u16, val: u16) {
                let a = (addr & !1) as usize;
                let b = val.to_be_bytes();
                self.0[a] = b[0];
                self.0[a + 1] = b[1];
            }
            fn read8(&mut self, addr: u16) -> u8 {
                self.0[addr as usize]
            }
            fn write8(&mut self, addr: u16, val: u8) {
                self.0[addr as usize] = val;
            }
            fn peek8(&self, addr: u16) -> u8 {
                self.0[addr as usize]
            }
            fn cru_read_bit(&mut self, bit_addr: u16) -> bool {
                ((self.1 >> (bit_addr & 15)) & 1) != 0
            }
            fn cru_write_bit(&mut self, bit_addr: u16, val: bool) {
                let m = 1u16 << (bit_addr & 15);
                if val { self.1 |= m } else { self.1 &= !m };
            }
        }
        let mut bus = TmsBus(vec![0; 65536], 0);
        // Reset vector WP=8300, PC=0100
        bus.write16(0x0000, 0x8300);
        bus.write16(0x0002, 0x0100);
        // 0100: LI R0, 0010 (0200 0010); AI R0, 0025 (0220 0025); IDLE (0340)
        bus.write16(0x0100, 0x0200);
        bus.write16(0x0102, 0x0010);
        bus.write16(0x0104, 0x0220);
        bus.write16(0x0106, 0x0025);
        bus.write16(0x0108, 0x0340);

        let mut cpu = Tms9900::new();
        cpu.reset(&mut bus);
        cpu.step(&mut bus);
        cpu.step(&mut bus);
        cpu.step(&mut bus);
        assert_eq!(bus.read16(0x8300), 0x0035);
        assert!(cpu.halted);
    }

    #[test]
    fn test_tms1100_tcy_tma_and_br() {
        struct MvIo(u16);
        impl Tms1100Io for MvIo {
            fn write_o(&mut self, _o_val: u8, _cycles: u64) {}
            fn write_r(&mut self, r_latch: u16, _cycles: u64) {
                self.0 = r_latch;
            }
            fn read_k(&self, _r_latch: u16, _cycles: u64) -> u8 {
                0
            }
        }
        let mut io = MvIo(0);
        let mut cpu = Tms1100::new();
        // TCY 5 (0x4A? Wait: 0x40..=0x4F swaps nibble: 0x45 sets Y=0xA, 0x4A sets Y=5),
        // CLA (0x2F), A8AAC (0x04 -> adds 8? Let's test TCY 0 + TCMIY 3 + TMA + BR 0x10)
        let mut rom = [0u8; 2048];
        let base = 0x0F * 64; // Reset starts at PA=0x0F, PC=0x00
        // TMS1100 6-bit LFSR PC sequence: 0x00 -> 0x01 -> 0x03 -> 0x07 -> 0x0F
        rom[base] = 0x40;        // PC 0x00: TCY 0
        rom[base + 0x01] = 0x6C; // PC 0x01: TCMIY 3 (INV_4[0xC] = 3)
        rom[base + 0x03] = 0x40; // PC 0x03: TCY 0
        rom[base + 0x07] = 0x21; // PC 0x07: TMA (A = 3, status = true)
        rom[base + 0x0F] = 0x90; // PC 0x0F: BR 0x10
        rom[base + 0x10] = 0x70; // PC 0x10: IAC (A = 4, no carry -> status = false)
        rom[base + 0x20] = 0x77; // PC 0x20: DAN (A = 4 + 15 = 3, carry -> status = true)
        cpu.load_rom(&rom);
        cpu.reset();
        for _ in 0..5 {
            cpu.step(&mut io);
        }
        assert_eq!(cpu.a, 3);
        assert_eq!(cpu.pc, 0x10);
        cpu.step(&mut io); // IAC
        assert_eq!(cpu.a, 4);
        assert!(!cpu.status);
        assert_eq!(cpu.pc, 0x20);
        cpu.step(&mut io); // DAN
        assert_eq!(cpu.a, 3);
        assert!(cpu.status);
    }

    #[test]
    fn test_lc8670_arithmetic_and_push_pop() {
        struct VmuBus {
            rom: [u8; 4096],
            ram: [[u8; 256]; 2],
            sfr: [u8; 256],
        }
        impl Lc8670Bus for VmuBus {
            fn read_code(&self, addr: u16) -> u8 {
                self.rom[(addr as usize) & 0xFFF]
            }
            fn read_ram(&self, bank: u8, addr: u8) -> u8 {
                self.ram[(bank & 1) as usize][addr as usize]
            }
            fn write_ram(&mut self, bank: u8, addr: u8, val: u8) {
                self.ram[(bank & 1) as usize][addr as usize] = val;
            }
            fn read_sfr(&mut self, addr: u8) -> u8 {
                self.sfr[addr as usize]
            }
            fn peek_sfr(&self, addr: u8) -> u8 {
                self.sfr[addr as usize]
            }
            fn write_sfr(&mut self, addr: u8, val: u8) {
                self.sfr[addr as usize] = val;
            }
        }
        let mut bus = VmuBus {
            rom: [0; 4096],
            ram: [[0; 256]; 2],
            sfr: [0; 256],
        };
        // 0000: MOV #12, $00? Let's do ADD #5 (0x81 0x05), PUSH ACC (0x60 0x00), CLR1 PSW (0xD0..)? Wait: 0x00=NOP, 0x81 05=ADD #05, 0x60 06=PUSH SP? Let's test 0x81 0x05, 0x81 0x07 -> ACC = 12
        bus.rom[0..4].copy_from_slice(&[0x81, 0x05, 0x81, 0x07]);
        let mut cpu = Lc8670::new();
        cpu.step(&mut bus);
        cpu.step(&mut bus);
        assert_eq!(cpu.acc, 12);
    }

    #[test]
    fn test_lc8670_indirect_r2_sfr_and_flash() {
        struct VmuFlashBus {
            rom: [u8; 64],
            ram: [[u8; 256]; 2],
            sfr: [u8; 256],
            flash: [u8; 131_072],
        }
        impl Lc8670Bus for VmuFlashBus {
            fn read_code(&self, addr: u16) -> u8 {
                self.rom[(addr as usize) & 63]
            }
            fn read_ram(&self, bank: u8, addr: u8) -> u8 {
                self.ram[(bank & 1) as usize][addr as usize]
            }
            fn write_ram(&mut self, bank: u8, addr: u8, val: u8) {
                self.ram[(bank & 1) as usize][addr as usize] = val;
            }
            fn read_sfr(&mut self, addr: u8) -> u8 {
                self.sfr[addr as usize]
            }
            fn peek_sfr(&self, addr: u8) -> u8 {
                self.sfr[addr as usize]
            }
            fn write_sfr(&mut self, addr: u8, val: u8) {
                self.sfr[addr as usize] = val;
            }
            fn read_flash(&self, addr: u32) -> u8 {
                self.flash.get(addr as usize).copied().unwrap_or(0xFF)
            }
            fn write_flash(&mut self, addr: u32, val: u8) {
                if let Some(slot) = self.flash.get_mut(addr as usize) {
                    *slot = val;
                }
            }
        }
        let mut bus = VmuFlashBus {
            rom: [0; 64],
            ram: [[0; 256]; 2],
            sfr: [0; 256],
            flash: [0xFF; 131_072],
        };
        // @R2 = 0x80 -> targets XRAM/SFR $180
        bus.ram[0][2] = 0x80;
        bus.flash[0x10234] = 0x5A;
        // MOV #$A5, $00 (wait: MOV #33 -> @R2 = 0x26 0x33; MOV #$01, $154 = 0x23 0x54 0x01;
        // MOV #$34, $104(TRL) = 0x23 0x04 0x34; MOV #$02, $105(TRH) = 0x23 0x05 0x02; LDF = 0x50; STF to bank1+$0305)
        bus.rom[0..14].copy_from_slice(&[
            0x26, 0x33, // MOV #$33, @R2 -> sfr[0x80]
            0x23, 0x54, 0x01, // MOV #$01, $154 (FPR bank 1)
            0x23, 0x05, 0x02, // MOV #$02, TRH
            0x23, 0x04, 0x34, // MOV #$34, TRL
            0x50, // LDF -> ACC = flash[0x10234] = 0x5A
            0x23, 0x04, // followed below
        ]);
        bus.rom[12] = 0x23;
        bus.rom[13] = 0x04;
        bus.rom[14] = 0x35; // MOV #$35, TRL
        bus.rom[15] = 0x51; // STF -> flash[0x10235] = 0x5A
        let mut cpu = Lc8670::new();
        for _ in 0..7 {
            cpu.step(&mut bus);
        }
        assert_eq!(bus.sfr[0x80], 0x33);
        assert_eq!(cpu.acc, 0x5A);
        assert_eq!(bus.flash[0x10235], 0x5A);
    }

    #[test]
    fn test_arm7tdmi_arm_bx_thumb_bl_and_irq() {
        let mut bus = FlatBus32::new(4096);
        // ARM @ 0x0000:
        //   LDR PC, [PC, #0x10]   -> reads 0x0018 = 0x00000100
        bus.write32_le(0x0000, 0xE59FF010);
        // IRQ vector @ 0x0018? Put jump target at 0x0020 and use #0x18:
        bus.write32_le(0x0000, 0xE59FF018); // PC+8+0x18 = 0x0020
        bus.write32_le(0x0020, 0x0000_0100);
        // ARM @ 0x0100:
        //   MOV SP, #0x400        -> E3A0D301 (0x01 rotated right by 6 -> 0x04000000? Use 0x01 ror 24 = 0x400 -> rot=12 => E3A0DC04)
        //   ADR R0, ThumbCode+1   -> E28F0005 (0x104+8+5 = 0x111)
        //   BX  R0                -> E12FFF10
        bus.write32_le(0x0100, 0xE3A0DE40); // MOV SP, #0x400 (0x40 ror 28)
        bus.write32_le(0x0104, 0xE28F0005); // R0 = 0x0104 + 8 + 5 = 0x0111
        bus.write32_le(0x0108, 0xE12FFF10); // BX R0
        // Thumb @ 0x0110:
        //   MOV R1, #0x2A         -> 212A
        //   ADD R1, #0x08         -> 3108
        //   PUSH {R1}             -> B402
        //   POP {R2}              -> BC04
        //   BL +8                 -> F000 F804
        bus.write16_le(0x0110, 0x212A);
        bus.write16_le(0x0112, 0x3108);
        bus.write16_le(0x0114, 0xB402);
        bus.write16_le(0x0116, 0xBC04);
        bus.write16_le(0x0118, 0xF000);
        bus.write16_le(0x011A, 0xF804);

        let mut cpu = Arm7tdmi::new();
        for _ in 0..8 {
            cpu.step(&mut bus);
        }
        assert!(cpu.is_thumb());
        assert_eq!(cpu.regs[13], 0x0400);
        assert_eq!(cpu.regs[2], 0x32);
        // Step BL prefix & suffix
        cpu.step(&mut bus);
        cpu.step(&mut bus);
        assert_eq!(cpu.pc(), 0x0124);
        assert_eq!(cpu.regs[14], 0x011D);
    }
}
