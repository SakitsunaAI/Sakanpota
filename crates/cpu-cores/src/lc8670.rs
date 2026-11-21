use emu_core::{CpuDebugState, DisassembledInstruction};

/// Bus interface for Sanyo LC8670 ("Potato") 8-bit MCU (Sega VMU).
pub trait Lc8670Bus {
    /// Read a byte from program memory (BIOS / Flash).
    fn read_code(&self, addr: u16) -> u8;
    /// Read a byte from internal work RAM (`bank` 0 or 1, `addr` 0..=255).
    fn read_ram(&self, bank: u8, addr: u8) -> u8;
    /// Write a byte to internal work RAM (`bank` 0 or 1, `addr` 0..=255).
    fn write_ram(&mut self, bank: u8, addr: u8, val: u8);
    /// Read an SFR (`addr` 0x00..=0x7F -> $100..$17F) or XRAM (`addr` 0x80..=0xFF -> $180..$1FF).
    fn read_sfr(&mut self, addr: u8) -> u8;
    /// Side-effect-free peek of SFR / XRAM (`0x00..=0xFF` mapping `$100..$1FF`).
    fn peek_sfr(&self, addr: u8) -> u8;
    /// Write an SFR (`addr` 0x00..=0x7F -> $100..$17F) or XRAM (`addr` 0x80..=0xFF -> $180..$1FF).
    fn write_sfr(&mut self, addr: u8, val: u8);
    /// Read a byte from 128 KB Flash (`LDF`).
    fn read_flash(&self, _addr: u32) -> u8 {
        0xFF
    }
    /// Write a byte to 128 KB Flash (`STF`).
    fn write_flash(&mut self, _addr: u32, _val: u8) {}
}

/// Complete Sanyo LC8670 8-bit Microcontroller CPU Core.
#[derive(Debug, Clone)]
pub struct Lc8670 {
    pub pc: u16,
    pub acc: u8,
    pub psw: u8,
    pub b: u8,
    pub c: u8,
    pub trl: u8,
    pub trh: u8,
    pub sp: u8,
    pub halted: bool,
    pub irq_depth: u8,
    pub cycles: u64,
}

impl Default for Lc8670 {
    fn default() -> Self {
        Self::new()
    }
}

impl Lc8670 {
    pub const PSW_CY: u8 = 0x80;
    pub const PSW_AC: u8 = 0x40;
    pub const PSW_OV: u8 = 0x04;
    pub const PSW_RAMBK0: u8 = 0x02;

    pub fn new() -> Self {
        Self {
            pc: 0,
            acc: 0,
            psw: 0,
            b: 0,
            c: 0,
            trl: 0,
            trh: 0,
            sp: 0x7F,
            halted: false,
            irq_depth: 0,
            cycles: 0,
        }
    }

    pub fn reset(&mut self) {
        self.pc = 0x0000;
        self.acc = 0;
        self.psw = 0x00;
        self.b = 0;
        self.c = 0;
        self.trl = 0;
        self.trh = 0;
        self.sp = 0x7F;
        self.halted = false;
        self.irq_depth = 0;
        self.cycles = 0;
    }

    #[inline]
    fn rambk(&self) -> u8 {
        (self.psw >> 1) & 1
    }

    #[inline]
    fn set_flag(&mut self, mask: u8, cond: bool) {
        if cond {
            self.psw |= mask;
        } else {
            self.psw &= !mask;
        }
    }

    #[inline]
    fn cy(&self) -> bool {
        (self.psw & Self::PSW_CY) != 0
    }

    #[inline]
    fn fetch8<B: Lc8670Bus>(&mut self, bus: &B) -> u8 {
        let v = bus.read_code(self.pc);
        self.pc = self.pc.wrapping_add(1);
        v
    }

    fn push8<B: Lc8670Bus>(&mut self, bus: &mut B, val: u8) {
        self.sp = self.sp.wrapping_add(1);
        bus.write_ram(0, self.sp, val);
    }

    fn pop8<B: Lc8670Bus>(&mut self, bus: &B) -> u8 {
        let v = bus.read_ram(0, self.sp);
        self.sp = self.sp.wrapping_sub(1);
        v
    }

    fn push_pc<B: Lc8670Bus>(&mut self, bus: &mut B) {
        let pc = self.pc;
        self.push8(bus, (pc & 0xFF) as u8);
        self.push8(bus, (pc >> 8) as u8);
    }

    fn pop_pc<B: Lc8670Bus>(&mut self, bus: &B) {
        let hi = self.pop8(bus) as u16;
        let lo = self.pop8(bus) as u16;
        self.pc = (hi << 8) | lo;
    }

    #[inline]
    fn ri_addr(&self, bus: &impl Lc8670Bus, idx: u8) -> u8 {
        // R0..R3 reside in bank 0 selected by IRBK1:IRBK0 (PSW bits 4..3 -> offset 0,4,8,12).
        let base = ((self.psw >> 1) & 0x0C) + (idx & 3);
        bus.read_ram(0, base)
    }

    fn read_d9<B: Lc8670Bus>(&mut self, bus: &mut B, d9: u16) -> u8 {
        if d9 < 0x100 {
            bus.read_ram(self.rambk(), d9 as u8)
        } else {
            let reg = (d9 & 0xFF) as u8;
            match reg {
                0x00 => self.acc,
                0x01 => self.psw,
                0x02 => self.b,
                0x03 => self.c,
                0x04 => self.trl,
                0x05 => self.trh,
                0x06 => self.sp,
                _ => bus.read_sfr(reg),
            }
        }
    }

    fn write_d9<B: Lc8670Bus>(&mut self, bus: &mut B, d9: u16, val: u8) {
        if d9 < 0x100 {
            bus.write_ram(self.rambk(), d9 as u8, val);
        } else {
            let reg = (d9 & 0xFF) as u8;
            match reg {
                0x00 => self.acc = val,
                0x01 => self.psw = val,
                0x02 => self.b = val,
                0x03 => self.c = val,
                0x04 => self.trl = val,
                0x05 => self.trh = val,
                0x06 => self.sp = val,
                0x07 => {
                    bus.write_sfr(0x07, val);
                    if (val & 1) != 0 {
                        self.halted = true;
                    }
                }
                _ => bus.write_sfr(reg, val),
            }
        }
    }

    fn read_ind<B: Lc8670Bus>(&mut self, bus: &mut B, idx: u8) -> u8 {
        let addr = self.ri_addr(bus, idx);
        let d9 = (((idx & 2) as u16) << 7) | (addr as u16);
        self.read_d9(bus, d9)
    }

    fn write_ind<B: Lc8670Bus>(&mut self, bus: &mut B, idx: u8, val: u8) {
        let addr = self.ri_addr(bus, idx);
        let d9 = (((idx & 2) as u16) << 7) | (addr as u16);
        self.write_d9(bus, d9, val);
    }

    fn add8(&mut self, rhs: u8, with_carry: bool) {
        let carry_in = if with_carry && self.cy() { 1u16 } else { 0u16 };
        let a = self.acc as u16;
        let b = rhs as u16;
        let sum = a + b + carry_in;
        let half = (self.acc & 0x0F) as u16 + (rhs & 0x0F) as u16 + carry_in;
        let res = (sum & 0xFF) as u8;
        let ov = ((!(self.acc ^ rhs)) & (self.acc ^ res) & 0x80) != 0;
        self.acc = res;
        self.set_flag(Self::PSW_CY, sum > 0xFF);
        self.set_flag(Self::PSW_AC, half > 0x0F);
        self.set_flag(Self::PSW_OV, ov);
    }

    fn sub8(&mut self, rhs: u8, with_borrow: bool) {
        let borrow = if with_borrow && self.cy() { 1i16 } else { 0i16 };
        let a = self.acc as i16;
        let b = rhs as i16;
        let diff = a - b - borrow;
        let half = (self.acc & 0x0F) as i16 - (rhs & 0x0F) as i16 - borrow;
        let res = (diff & 0xFF) as u8;
        let ov = (((self.acc ^ rhs) & (self.acc ^ res)) & 0x80) != 0;
        self.acc = res;
        self.set_flag(Self::PSW_CY, diff < 0);
        self.set_flag(Self::PSW_AC, half < 0);
        self.set_flag(Self::PSW_OV, ov);
    }

    /// Trigger an interrupt vector (e.g., 0x0003..0x004B).
    pub fn trigger_interrupt<B: Lc8670Bus>(&mut self, bus: &mut B, vector: u16) {
        self.halted = false;
        let pcon = bus.peek_sfr(0x07) & !1;
        bus.write_sfr(0x07, pcon);
        self.irq_depth = self.irq_depth.saturating_add(1);
        self.push_pc(bus);
        self.pc = vector;
        self.cycles += 2;
    }

    pub fn step<B: Lc8670Bus>(&mut self, bus: &mut B) -> u32 {
        if self.halted {
            self.cycles += 1;
            return 1;
        }

        let op = self.fetch8(bus);
        let d9 = (((op as u16) & 1) << 8) | if (op & 0x06) == 0x02 || (op & 0x08) != 0 || (op & 0xE7) == 0x60 {
            // Note: we fetch trailing byte directly inside matching arms below to avoid over-fetching.
            0
        } else {
            0
        };
        let _ = d9;

        let cyc: u32 = match op {
            // 0x00: NOP
            0x00 => 1,
            // 0x01: BR r8
            0x01 => {
                let rel = self.fetch8(bus) as i8;
                self.pc = self.pc.wrapping_add(rel as i16 as u16);
                2
            }
            // 0x02..=0x03: LD d9
            0x02..=0x03 => {
                let lo = self.fetch8(bus) as u16;
                let addr = (((op as u16) & 1) << 8) | lo;
                self.acc = self.read_d9(bus, addr);
                1
            }
            // 0x04..=0x07: LD @Ri
            0x04..=0x07 => {
                self.acc = self.read_ind(bus, op & 3);
                1
            }
            // 0x08..=0x0F, 0x18..=0x1F: CALL a12
            0x08..=0x0F | 0x18..=0x1F => {
                let lo = self.fetch8(bus) as u16;
                let target_low12 = (((op as u16) & 0x10) << 7) | (((op as u16) & 0x07) << 8) | lo;
                self.push_pc(bus);
                self.pc = (self.pc & 0xF000) | target_low12;
                2
            }
            // 0x10: CALLR r16
            0x10 => {
                let lo = self.fetch8(bus) as u16;
                let hi = self.fetch8(bus) as u16;
                let rel = (hi << 8) | lo;
                let base = self.pc.wrapping_sub(1);
                self.push_pc(bus);
                self.pc = base.wrapping_add(rel);
                4
            }
            // 0x11: BRF r16
            0x11 => {
                let lo = self.fetch8(bus) as u16;
                let hi = self.fetch8(bus) as u16;
                let rel = (hi << 8) | lo;
                let base = self.pc.wrapping_sub(1);
                self.pc = base.wrapping_add(rel);
                4
            }
            // 0x12..=0x13: ST d9
            0x12..=0x13 => {
                let lo = self.fetch8(bus) as u16;
                let addr = (((op as u16) & 1) << 8) | lo;
                self.write_d9(bus, addr, self.acc);
                1
            }
            // 0x14..=0x17: ST @Ri
            0x14..=0x17 => {
                self.write_ind(bus, op & 3, self.acc);
                1
            }
            // 0x20: CALLF a16
            0x20 => {
                let hi = self.fetch8(bus) as u16;
                let lo = self.fetch8(bus) as u16;
                self.push_pc(bus);
                self.pc = (hi << 8) | lo;
                2
            }
            // 0x21: JMPF a16
            0x21 => {
                let hi = self.fetch8(bus) as u16;
                let lo = self.fetch8(bus) as u16;
                self.pc = (hi << 8) | lo;
                2
            }
            // 0x22..=0x23: MOV #i8, d9
            0x22..=0x23 => {
                let lo = self.fetch8(bus) as u16;
                let imm = self.fetch8(bus);
                let addr = (((op as u16) & 1) << 8) | lo;
                self.write_d9(bus, addr, imm);
                2
            }
            // 0x24..=0x27: MOV #i8, @Rj
            0x24..=0x27 => {
                let imm = self.fetch8(bus);
                self.write_ind(bus, op & 3, imm);
                1
            }
            // 0x28..=0x2F, 0x38..=0x3F: JMP a12
            0x28..=0x2F | 0x38..=0x3F => {
                let lo = self.fetch8(bus) as u16;
                let target_low12 = (((op as u16) & 0x10) << 7) | (((op as u16) & 0x07) << 8) | lo;
                self.pc = (self.pc & 0xF000) | target_low12;
                2
            }
            // 0x30: MUL (B:ACC:C <- ACC:C * B)
            0x30 => {
                let lhs = ((self.acc as u32) << 8) | (self.c as u32);
                let prod = lhs * (self.b as u32);
                self.b = ((prod >> 16) & 0xFF) as u8;
                self.acc = ((prod >> 8) & 0xFF) as u8;
                self.c = (prod & 0xFF) as u8;
                self.set_flag(Self::PSW_OV, self.b != 0);
                self.set_flag(Self::PSW_CY, false);
                7
            }
            // 0x31: BE #i8, r8
            0x31 => {
                let imm = self.fetch8(bus);
                let rel = self.fetch8(bus) as i8;
                self.set_flag(Self::PSW_CY, self.acc < imm);
                if self.acc == imm {
                    self.pc = self.pc.wrapping_add(rel as i16 as u16);
                }
                2
            }
            // 0x32..=0x33: BE d9, r8
            0x32..=0x33 => {
                let lo = self.fetch8(bus) as u16;
                let rel = self.fetch8(bus) as i8;
                let val = self.read_d9(bus, (((op as u16) & 1) << 8) | lo);
                self.set_flag(Self::PSW_CY, self.acc < val);
                if self.acc == val {
                    self.pc = self.pc.wrapping_add(rel as i16 as u16);
                }
                2
            }
            // 0x34..=0x37: BE @Rj, #i8, r8
            0x34..=0x37 => {
                let imm = self.fetch8(bus);
                let rel = self.fetch8(bus) as i8;
                let val = self.read_ind(bus, op & 3);
                self.set_flag(Self::PSW_CY, val < imm);
                if val == imm {
                    self.pc = self.pc.wrapping_add(rel as i16 as u16);
                }
                2
            }
            // 0x40: DIV (ACC:C = quotient, B = remainder <- ACC:C / B)
            0x40 => {
                let divisor = self.b as u16;
                let dividend = ((self.acc as u16) << 8) | (self.c as u16);
                if let (Some(q), Some(r)) = (dividend.checked_div(divisor), dividend.checked_rem(divisor)) {
                    self.acc = (q >> 8) as u8;
                    self.c = (q & 0xFF) as u8;
                    self.b = r as u8;
                    self.set_flag(Self::PSW_OV, false);
                } else {
                    self.acc = 0xFF;
                    self.set_flag(Self::PSW_OV, true);
                }
                self.set_flag(Self::PSW_CY, false);
                7
            }
            // 0x41: BNE #i8, r8
            0x41 => {
                let imm = self.fetch8(bus);
                let rel = self.fetch8(bus) as i8;
                self.set_flag(Self::PSW_CY, self.acc < imm);
                if self.acc != imm {
                    self.pc = self.pc.wrapping_add(rel as i16 as u16);
                }
                2
            }
            // 0x42..=0x43: BNE d9, r8
            0x42..=0x43 => {
                let lo = self.fetch8(bus) as u16;
                let rel = self.fetch8(bus) as i8;
                let val = self.read_d9(bus, (((op as u16) & 1) << 8) | lo);
                self.set_flag(Self::PSW_CY, self.acc < val);
                if self.acc != val {
                    self.pc = self.pc.wrapping_add(rel as i16 as u16);
                }
                2
            }
            // 0x44..=0x47: BNE @Rj, #i8, r8
            0x44..=0x47 => {
                let imm = self.fetch8(bus);
                let rel = self.fetch8(bus) as i8;
                let val = self.read_ind(bus, op & 3);
                self.set_flag(Self::PSW_CY, val < imm);
                if val != imm {
                    self.pc = self.pc.wrapping_add(rel as i16 as u16);
                }
                2
            }
            // 0x48..=0x4F, 0x58..=0x5F: BPC d9, b3, r8
            0x48..=0x4F | 0x58..=0x5F => {
                let lo = self.fetch8(bus) as u16;
                let rel = self.fetch8(bus) as i8;
                let addr = (((op as u16) & 0x10) << 4) | lo;
                let bit = op & 7;
                let val = self.read_d9(bus, addr);
                if (val & (1 << bit)) != 0 {
                    self.write_d9(bus, addr, val & !(1 << bit));
                    self.pc = self.pc.wrapping_add(rel as i16 as u16);
                }
                2
            }
            // 0x52..=0x53: DBNZ d9, r8
            0x52..=0x53 => {
                let lo = self.fetch8(bus) as u16;
                let rel = self.fetch8(bus) as i8;
                let addr = (((op as u16) & 1) << 8) | lo;
                let v = self.read_d9(bus, addr).wrapping_sub(1);
                self.write_d9(bus, addr, v);
                if v != 0 {
                    self.pc = self.pc.wrapping_add(rel as i16 as u16);
                }
                2
            }
            // 0x54..=0x57: DBNZ @Ri, r8
            0x54..=0x57 => {
                let rel = self.fetch8(bus) as i8;
                let v = self.read_ind(bus, op & 3).wrapping_sub(1);
                self.write_ind(bus, op & 3, v);
                if v != 0 {
                    self.pc = self.pc.wrapping_add(rel as i16 as u16);
                }
                2
            }
            // 0x60..=0x61: PUSH d9
            0x60..=0x61 => {
                let lo = self.fetch8(bus) as u16;
                let v = self.read_d9(bus, (((op as u16) & 1) << 8) | lo);
                self.push8(bus, v);
                2
            }
            // 0x62..=0x63: INC d9
            0x62..=0x63 => {
                let lo = self.fetch8(bus) as u16;
                let addr = (((op as u16) & 1) << 8) | lo;
                let v = self.read_d9(bus, addr).wrapping_add(1);
                self.write_d9(bus, addr, v);
                1
            }
            // 0x64..=0x67: INC @Ri
            0x64..=0x67 => {
                let v = self.read_ind(bus, op & 3).wrapping_add(1);
                self.write_ind(bus, op & 3, v);
                1
            }
            // 0x68..=0x6F, 0x78..=0x7F: BP d9, b3, r8
            0x68..=0x6F | 0x78..=0x7F => {
                let lo = self.fetch8(bus) as u16;
                let rel = self.fetch8(bus) as i8;
                let addr = (((op as u16) & 0x10) << 4) | lo;
                let bit = op & 7;
                if (self.read_d9(bus, addr) & (1 << bit)) != 0 {
                    self.pc = self.pc.wrapping_add(rel as i16 as u16);
                }
                2
            }
            // 0x70..=0x71: POP d9
            0x70..=0x71 => {
                let lo = self.fetch8(bus) as u16;
                let v = self.pop8(bus);
                self.write_d9(bus, (((op as u16) & 1) << 8) | lo, v);
                2
            }
            // 0x72..=0x73: DEC d9
            0x72..=0x73 => {
                let lo = self.fetch8(bus) as u16;
                let addr = (((op as u16) & 1) << 8) | lo;
                let v = self.read_d9(bus, addr).wrapping_sub(1);
                self.write_d9(bus, addr, v);
                1
            }
            // 0x74..=0x77: DEC @Ri
            0x74..=0x77 => {
                let v = self.read_ind(bus, op & 3).wrapping_sub(1);
                self.write_ind(bus, op & 3, v);
                1
            }
            // 0x80: BZ r8
            0x80 => {
                let rel = self.fetch8(bus) as i8;
                if self.acc == 0 {
                    self.pc = self.pc.wrapping_add(rel as i16 as u16);
                }
                2
            }
            // 0x81..=0x87: ADD
            0x81 => {
                let rhs = self.fetch8(bus);
                self.add8(rhs, false);
                1
            }
            0x82..=0x83 => {
                let lo = self.fetch8(bus) as u16;
                let rhs = self.read_d9(bus, (((op as u16) & 1) << 8) | lo);
                self.add8(rhs, false);
                1
            }
            0x84..=0x87 => {
                let rhs = self.read_ind(bus, op & 3);
                self.add8(rhs, false);
                1
            }
            // 0x88..=0x8F, 0x98..=0x9F: BN d9, b3, r8
            0x88..=0x8F | 0x98..=0x9F => {
                let lo = self.fetch8(bus) as u16;
                let rel = self.fetch8(bus) as i8;
                let addr = (((op as u16) & 0x10) << 4) | lo;
                let bit = op & 7;
                if (self.read_d9(bus, addr) & (1 << bit)) == 0 {
                    self.pc = self.pc.wrapping_add(rel as i16 as u16);
                }
                2
            }
            // 0x90: BNZ r8
            0x90 => {
                let rel = self.fetch8(bus) as i8;
                if self.acc != 0 {
                    self.pc = self.pc.wrapping_add(rel as i16 as u16);
                }
                2
            }
            // 0x91..=0x97: ADDC
            0x91 => {
                let rhs = self.fetch8(bus);
                self.add8(rhs, true);
                1
            }
            0x92..=0x93 => {
                let lo = self.fetch8(bus) as u16;
                let rhs = self.read_d9(bus, (((op as u16) & 1) << 8) | lo);
                self.add8(rhs, true);
                1
            }
            0x94..=0x97 => {
                let rhs = self.read_ind(bus, op & 3);
                self.add8(rhs, true);
                1
            }
            // 0xA0: RET
            0xA0 => {
                self.pop_pc(bus);
                2
            }
            // 0xA1..=0xA7: SUB
            0xA1 => {
                let rhs = self.fetch8(bus);
                self.sub8(rhs, false);
                1
            }
            0xA2..=0xA3 => {
                let lo = self.fetch8(bus) as u16;
                let rhs = self.read_d9(bus, (((op as u16) & 1) << 8) | lo);
                self.sub8(rhs, false);
                1
            }
            0xA4..=0xA7 => {
                let rhs = self.read_ind(bus, op & 3);
                self.sub8(rhs, false);
                1
            }
            // 0xA8..=0xAF, 0xB8..=0xBF: NOT1 d9, b3
            0xA8..=0xAF | 0xB8..=0xBF => {
                let lo = self.fetch8(bus) as u16;
                let addr = (((op as u16) & 0x10) << 4) | lo;
                let bit = op & 7;
                let v = self.read_d9(bus, addr) ^ (1 << bit);
                self.write_d9(bus, addr, v);
                1
            }
            // 0xB0: RETI
            0xB0 => {
                self.pop_pc(bus);
                self.irq_depth = self.irq_depth.saturating_sub(1);
                2
            }
            // 0xB1..=0xB7: SUBC
            0xB1 => {
                let rhs = self.fetch8(bus);
                self.sub8(rhs, true);
                1
            }
            0xB2..=0xB3 => {
                let lo = self.fetch8(bus) as u16;
                let rhs = self.read_d9(bus, (((op as u16) & 1) << 8) | lo);
                self.sub8(rhs, true);
                1
            }
            0xB4..=0xB7 => {
                let rhs = self.read_ind(bus, op & 3);
                self.sub8(rhs, true);
                1
            }
            // 0xC0: ROR
            0xC0 => {
                self.acc = self.acc.rotate_right(1);
                1
            }
            // 0xC1: LDC (ACC = code[(TRH:TRL) + ACC])
            0xC1 => {
                let ptr = ((self.trh as u16) << 8) | (self.trl as u16);
                self.acc = bus.read_code(ptr.wrapping_add(self.acc as u16));
                2
            }
            // 0xC2..=0xC3: XCH d9
            0xC2..=0xC3 => {
                let lo = self.fetch8(bus) as u16;
                let addr = (((op as u16) & 1) << 8) | lo;
                let mem = self.read_d9(bus, addr);
                self.write_d9(bus, addr, self.acc);
                self.acc = mem;
                1
            }
            // 0xC4..=0xC7: XCH @Ri
            0xC4..=0xC7 => {
                let mem = self.read_ind(bus, op & 3);
                self.write_ind(bus, op & 3, self.acc);
                self.acc = mem;
                1
            }
            // 0xC8..=0xCF, 0xD8..=0xDF: CLR1 d9, b3
            0xC8..=0xCF | 0xD8..=0xDF => {
                let lo = self.fetch8(bus) as u16;
                let addr = (((op as u16) & 0x10) << 4) | lo;
                let bit = op & 7;
                let v = self.read_d9(bus, addr) & !(1 << bit);
                self.write_d9(bus, addr, v);
                1
            }
            // 0xD0: RORC
            0xD0 => {
                let c = u8::from(self.cy()) << 7;
                let new_c = (self.acc & 1) != 0;
                self.acc = (self.acc >> 1) | c;
                self.set_flag(Self::PSW_CY, new_c);
                1
            }
            // 0xD1..=0xD7: OR
            0xD1 => {
                self.acc |= self.fetch8(bus);
                1
            }
            0xD2..=0xD3 => {
                let lo = self.fetch8(bus) as u16;
                self.acc |= self.read_d9(bus, (((op as u16) & 1) << 8) | lo);
                1
            }
            0xD4..=0xD7 => {
                self.acc |= self.read_ind(bus, op & 3);
                1
            }
            // 0xE0: ROL
            0xE0 => {
                self.acc = self.acc.rotate_left(1);
                1
            }
            // 0xE1..=0xE7: AND
            0xE1 => {
                self.acc &= self.fetch8(bus);
                1
            }
            0xE2..=0xE3 => {
                let lo = self.fetch8(bus) as u16;
                self.acc &= self.read_d9(bus, (((op as u16) & 1) << 8) | lo);
                1
            }
            0xE4..=0xE7 => {
                self.acc &= self.read_ind(bus, op & 3);
                1
            }
            // 0xE8..=0xEF, 0xF8..=0xFF: SET1 d9, b3
            0xE8..=0xEF | 0xF8..=0xFF => {
                let lo = self.fetch8(bus) as u16;
                let addr = (((op as u16) & 0x10) << 4) | lo;
                let bit = op & 7;
                let v = self.read_d9(bus, addr) | (1 << bit);
                self.write_d9(bus, addr, v);
                1
            }
            // 0xF0: ROLC
            0xF0 => {
                let c = u8::from(self.cy());
                let new_c = (self.acc & 0x80) != 0;
                self.acc = (self.acc << 1) | c;
                self.set_flag(Self::PSW_CY, new_c);
                1
            }
            // 0xF1..=0xF7: XOR
            0xF1 => {
                self.acc ^= self.fetch8(bus);
                1
            }
            0xF2..=0xF3 => {
                let lo = self.fetch8(bus) as u16;
                self.acc ^= self.read_d9(bus, (((op as u16) & 1) << 8) | lo);
                1
            }
            0xF4..=0xF7 => {
                self.acc ^= self.read_ind(bus, op & 3);
                1
            }
            // 0x50: LDF (ACC = flash[(FPR:TRH:TRL)])
            0x50 => {
                let bank = (bus.peek_sfr(0x54) & 1) as u32;
                let addr = (bank << 16) | ((self.trh as u32) << 8) | (self.trl as u32);
                self.acc = bus.read_flash(addr);
                2
            }
            // 0x51: STF (flash[(FPR:TRH:TRL)] = ACC)
            0x51 => {
                let bank = (bus.peek_sfr(0x54) & 1) as u32;
                let addr = (bank << 16) | ((self.trh as u32) << 8) | (self.trl as u32);
                bus.write_flash(addr, self.acc);
                2
            }
        };

        self.cycles += cyc as u64;
        cyc
    }

    pub fn debug_state(&self) -> CpuDebugState {
        CpuDebugState {
            arch: "Sanyo LC8670",
            pc: self.pc as u32,
            sp: self.sp as u32,
            flags: format!(
                "{}{}{}|B{}",
                if (self.psw & Self::PSW_CY) != 0 { 'C' } else { '-' },
                if (self.psw & Self::PSW_AC) != 0 { 'A' } else { '-' },
                if (self.psw & Self::PSW_OV) != 0 { 'V' } else { '-' },
                self.rambk(),
            ),
            registers: vec![
                ("ACC".into(), format!("{:02X}", self.acc)),
                ("PSW".into(), format!("{:02X}", self.psw)),
                ("B".into(), format!("{:02X}", self.b)),
                ("C".into(), format!("{:02X}", self.c)),
                ("TR".into(), format!("{:02X}{:02X}", self.trh, self.trl)),
                ("SP".into(), format!("{:02X}", self.sp)),
            ],
            cycles: self.cycles,
            halted: self.halted,
        }
    }

    pub fn disassemble<B: Lc8670Bus>(&self, bus: &B, start: u16, count: usize) -> Vec<DisassembledInstruction> {
        let mut out = Vec::with_capacity(count);
        let mut addr = start;
        for _ in 0..count {
            let op = bus.read_code(addr);
            let b1 = bus.read_code(addr.wrapping_add(1));
            let b2 = bus.read_code(addr.wrapping_add(2));
            let d9_1 = (((op as u16) & 1) << 8) | (b1 as u16);
            let d9_bit = (((op as u16) & 0x10) << 4) | (b1 as u16);
            let a12 = (((op as u16) & 0x10) << 7) | (((op as u16) & 7) << 8) | (b1 as u16);
            let (len, mnem): (u16, String) = match op {
                0x00 => (1, "NOP".into()),
                0x01 => (2, format!("BR ${:04X}", addr.wrapping_add(2).wrapping_add((b1 as i8) as i16 as u16))),
                0x02..=0x03 => (2, format!("LD ${d9_1:03X}")),
                0x04..=0x07 => (1, format!("LD @R{}", op & 3)),
                0x08..=0x0F | 0x18..=0x1F => (2, format!("CALL ${a12:03X}")),
                0x10 => (3, "CALLR rel16".into()),
                0x11 => (3, "BRF rel16".into()),
                0x12..=0x13 => (2, format!("ST ${d9_1:03X}")),
                0x14..=0x17 => (1, format!("ST @R{}", op & 3)),
                0x20 => (3, format!("CALLF ${:04X}", ((b1 as u16) << 8) | (b2 as u16))),
                0x21 => (3, format!("JMPF ${:04X}", ((b1 as u16) << 8) | (b2 as u16))),
                0x22..=0x23 => (3, format!("MOV #${b2:02X},${d9_1:03X}")),
                0x24..=0x27 => (2, format!("MOV #${b1:02X},@R{}", op & 3)),
                0x28..=0x2F | 0x38..=0x3F => (2, format!("JMP ${a12:03X}")),
                0x30 => (1, "MUL".into()),
                0x31 => (3, format!("BE #${b1:02X},${:04X}", addr.wrapping_add(3).wrapping_add((b2 as i8) as i16 as u16))),
                0x40 => (1, "DIV".into()),
                0x48..=0x4F | 0x58..=0x5F => (3, format!("BPC ${d9_bit:03X},{}", op & 7)),
                0x60..=0x61 => (2, format!("PUSH ${d9_1:03X}")),
                0x68..=0x6F | 0x78..=0x7F => (3, format!("BP ${d9_bit:03X},{}", op & 7)),
                0x70..=0x71 => (2, format!("POP ${d9_1:03X}")),
                0x80 => (2, format!("BZ ${:04X}", addr.wrapping_add(2).wrapping_add((b1 as i8) as i16 as u16))),
                0x88..=0x8F | 0x98..=0x9F => (3, format!("BN ${d9_bit:03X},{}", op & 7)),
                0x90 => (2, format!("BNZ ${:04X}", addr.wrapping_add(2).wrapping_add((b1 as i8) as i16 as u16))),
                0xA0 => (1, "RET".into()),
                0xA8..=0xAF | 0xB8..=0xBF => (2, format!("NOT1 ${d9_bit:03X},{}", op & 7)),
                0xB0 => (1, "RETI".into()),
                0xC1 => (1, "LDC".into()),
                0xC8..=0xCF | 0xD8..=0xDF => (2, format!("CLR1 ${d9_bit:03X},{}", op & 7)),
                0xE8..=0xEF | 0xF8..=0xFF => (2, format!("SET1 ${d9_bit:03X},{}", op & 7)),
                _ => (1, format!("DB ${op:02X}")),
            };
            let mut bytes = Vec::with_capacity(len as usize);
            for i in 0..len {
                bytes.push(bus.read_code(addr.wrapping_add(i)));
            }
            out.push(DisassembledInstruction {
                address: addr as u32,
                bytes,
                mnemonic: mnem,
            });
            addr = addr.wrapping_add(len);
        }
        out
    }
}
