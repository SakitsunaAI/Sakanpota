use emu_core::{CpuDebugState, DisassembledInstruction};

pub trait Tms1100Io {
    fn write_o(&mut self, o_val: u8, cycles: u64);
    fn write_r(&mut self, r_latch: u16, cycles: u64);
    fn read_k(&self, r_latch: u16, cycles: u64) -> u8;
}

const BIT_MASK_4: [u8; 4] = [0x01, 0x02, 0x04, 0x08];
const NEXT_PC: [u8; 64] = [
    0x01, 0x03, 0x05, 0x07, 0x09, 0x0B, 0x0D, 0x0F,
    0x11, 0x13, 0x15, 0x17, 0x19, 0x1B, 0x1D, 0x1F,
    0x20, 0x22, 0x24, 0x26, 0x28, 0x2A, 0x2C, 0x2E,
    0x30, 0x32, 0x34, 0x36, 0x38, 0x3A, 0x3C, 0x3F,
    0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E,
    0x10, 0x12, 0x14, 0x16, 0x18, 0x1A, 0x1C, 0x1E,
    0x21, 0x23, 0x25, 0x27, 0x29, 0x2B, 0x2D, 0x2F,
    0x31, 0x33, 0x35, 0x37, 0x39, 0x3B, 0x3D, 0x3E,
];
const INV_4: [u8; 16] = [
    0x0, 0x8, 0x4, 0xC, 0x2, 0xA, 0x6, 0xE, 0x1, 0x9, 0x5, 0xD, 0x3, 0xB, 0x7, 0xF,
];
const INV_2: [u8; 4] = [0, 2, 1, 3];

#[derive(Debug, Clone)]
pub struct Tms1100 {
    pub rom: [u8; 2048],
    pub ram: [u8; 128],
    pub pc: u8,
    pub pa: u8,
    pub pb: u8,
    pub ca: u8,
    pub cb: u8,
    pub cs: u8,
    pub sr: u8,
    pub cl: u8,
    pub a: u8,
    pub x: u8,
    pub y: u8,
    pub status: bool,
    pub last_status: bool,
    pub sl: bool,
    pub r_latch: u16,
    pub o_latch: u8,
    pub cycles: u64,
    pub output_pla: [u8; 32],
}

impl Default for Tms1100 {
    fn default() -> Self {
        Self::new()
    }
}

impl Tms1100 {
    pub fn new() -> Self {
        let mut pla = [0u8; 32];
        for (i, slot) in pla.iter_mut().enumerate() {
            *slot = (i & 0x1F) as u8;
        }
        Self {
            rom: [0; 2048],
            ram: [0; 128],
            pc: 0,
            pa: 0x0F,
            pb: 0x0F,
            ca: 0,
            cb: 0,
            cs: 0,
            sr: 0,
            cl: 0,
            a: 0,
            x: 0,
            y: 0,
            status: true,
            last_status: true,
            sl: false,
            r_latch: 0,
            o_latch: 0,
            cycles: 0,
            output_pla: pla,
        }
    }

    pub fn load_rom(&mut self, data: &[u8]) {
        self.rom.fill(0);
        let len = data.len().min(2048);
        self.rom[..len].copy_from_slice(&data[..len]);
        self.reset();
    }

    pub fn reset(&mut self) {
        self.ram.fill(0);
        self.pc = 0;
        self.pa = 0x0F;
        self.pb = 0x0F;
        self.ca = 0;
        self.cb = 0;
        self.cs = 0;
        self.sr = 0;
        self.cl = 0;
        self.a = 0;
        self.x = 0;
        self.y = 0;
        self.status = true;
        self.last_status = true;
        self.sl = false;
        self.r_latch = 0;
        self.o_latch = 0;
        self.cycles = 0;
    }

    #[inline]
    fn rom_addr(&self) -> usize {
        ((self.ca as usize) << 10) | ((self.pa as usize) << 6) | (self.pc as usize)
    }

    #[inline]
    fn ram_addr(&self) -> usize {
        (((self.x & 7) as usize) << 4) | ((self.y & 0xF) as usize)
    }

    pub fn step<IO: Tms1100Io>(&mut self, io: &mut IO) -> u32 {
        let op = self.rom[self.rom_addr() & 0x7FF];
        self.pc = NEXT_PC[(self.pc & 0x3F) as usize];
        self.last_status = self.status;
        self.status = true;

        let addr = self.ram_addr();
        let imm2 = (op & 3) as usize;
        let imm4 = op & 0x0F;

        match op {
            0x00 => {
                // MNEA
                self.status = self.ram[addr] != self.a;
            }
            0x01 => {
                // ALEM
                self.status = self.a <= self.ram[addr];
            }
            0x02 => {
                // YNEA
                self.status = self.y != self.a;
                self.sl = self.status;
            }
            0x03 => {
                // XMA
                std::mem::swap(&mut self.a, &mut self.ram[addr]);
            }
            0x04 => {
                // DYN
                if self.y == 0 {
                    self.y = 0x0F;
                    self.status = false;
                } else {
                    self.y -= 1;
                }
            }
            0x05 => {
                // IYC
                if self.y == 0x0F {
                    self.y = 0;
                } else {
                    self.y += 1;
                    self.status = false;
                }
            }
            0x06 => {
                // AMAAC
                let sum = self.ram[addr] + self.a;
                self.a = sum & 0x0F;
                self.status = (sum & 0x10) != 0;
            }
            0x07 => {
                // DMAN
                let diff = self.ram[addr].wrapping_sub(1);
                self.a = diff & 0x0F;
                self.status = diff != 0xFF;
            }
            0x08 => {
                // TKA
                self.a = io.read_k(self.r_latch, self.cycles) & 0x0F;
            }
            0x09 => {
                // COMX
                self.x ^= 4;
            }
            0x0A => {
                // TDO
                let idx = ((u8::from(self.sl) << 4) | (self.a & 0x0F)) as usize;
                self.o_latch = self.output_pla[idx & 31];
                io.write_o(self.o_latch, self.cycles);
            }
            0x0B => {
                // COMC
                self.cb ^= 1;
            }
            0x0C => {
                // RSTR
                let y = self.y & 0x0F;
                self.r_latch &= !(1u16 << y);
                io.write_r(self.r_latch, self.cycles);
            }
            0x0D => {
                // SETR
                let y = self.y & 0x0F;
                self.r_latch |= 1u16 << y;
                io.write_r(self.r_latch, self.cycles);
            }
            0x0E => {
                // KNEZ
                self.status = (io.read_k(self.r_latch, self.cycles) & 0x0F) != 0;
            }
            0x0F => {
                // RETN
                if self.cl == 1 {
                    self.cl = 0;
                    self.pc = self.sr;
                    self.ca = self.cs;
                }
                self.pa = self.pb;
            }
            0x10..=0x1F => {
                // LDP
                self.pb = INV_4[imm4 as usize];
            }
            0x20 => self.y = self.a,               // TAY
            0x21 => self.a = self.ram[addr],       // TMA
            0x22 => self.y = self.ram[addr],       // TMY
            0x23 => self.a = self.y,               // TYA
            0x24 => {
                // TAMDYN
                self.ram[addr] = self.a;
                if self.y == 0 {
                    self.y = 0x0F;
                    self.status = false;
                } else {
                    self.y -= 1;
                }
            }
            0x25 => {
                // TAMIYC
                self.ram[addr] = self.a;
                self.y = (self.y + 1) & 0x0F;
                self.status = self.y == 0;
            }
            0x26 => {
                // TAMZA
                self.ram[addr] = self.a;
                self.a = 0;
            }
            0x27 => self.ram[addr] = self.a, // TAM
            0x28..=0x2F => {
                // LDX (3-bit file address)
                self.x = INV_4[(op & 7) as usize] >> 1;
            }
            0x30..=0x33 => {
                // SBIT
                self.ram[addr] |= BIT_MASK_4[INV_2[imm2] as usize];
            }
            0x34..=0x37 => {
                // RBIT
                self.ram[addr] &= !BIT_MASK_4[INV_2[imm2] as usize];
            }
            0x38..=0x3B => {
                // TBIT1
                self.status = (self.ram[addr] & BIT_MASK_4[INV_2[imm2] as usize]) != 0;
            }
            0x3C => {
                // SAMAN
                let d = self.ram[addr] + (self.a ^ 0x0F) + 1;
                self.a = d & 0x0F;
                self.status = (d & 0x10) != 0;
            }
            0x3D => {
                // CPAIZ
                let d = (self.a ^ 0x0F) + 1;
                self.a = d & 0x0F;
                self.status = (d & 0x10) != 0;
            }
            0x3E => {
                // IMAC
                let s = self.ram[addr] + 1;
                self.a = s & 0x0F;
                self.status = (s & 0x10) != 0;
            }
            0x3F => {
                // MNEZ
                self.status = self.ram[addr] != 0;
            }
            0x40..=0x4F => {
                // TCY
                self.y = INV_4[imm4 as usize];
            }
            0x50..=0x5F => {
                // YNEC
                self.status = self.y != INV_4[imm4 as usize];
            }
            0x60..=0x6F => {
                // TCMIY
                self.ram[addr] = INV_4[imm4 as usize];
                self.y = (self.y + 1) & 0x0F;
            }
            0x70..=0x7E => {
                // IAC / A*AAC / DAN
                let sum = self.a + INV_4[imm4 as usize] + 1;
                self.a = sum & 0x0F;
                self.status = (sum & 0x10) != 0;
            }
            0x7F => {
                // CLA
                self.a = 0;
            }
            0x80..=0xBF => {
                // BR
                if self.last_status {
                    if self.cl == 0 {
                        self.pa = self.pb;
                    }
                    self.ca = self.cb;
                    self.pc = op & 0x3F;
                }
            }
            _ => {
                // CALL (0xC0..0xFF)
                if self.last_status {
                    if self.cl == 0 {
                        self.sr = self.pc;
                        self.cl = 1;
                        std::mem::swap(&mut self.pa, &mut self.pb);
                        self.cs = self.ca;
                        self.ca = self.cb;
                        self.pc = op & 0x3F;
                    } else {
                        self.ca = self.cb;
                        self.pc = op & 0x3F;
                    }
                }
            }
        }
        self.cycles += 6;
        6
    }

    pub fn debug_state(&self) -> CpuDebugState {
        CpuDebugState {
            arch: "TI TMS1100 (4-bit)",
            pc: self.rom_addr() as u32,
            sp: self.sr as u32,
            flags: format!(
                "ST:{} CL:{} CA:{} CB:{}",
                self.status as u8, self.cl, self.ca, self.cb
            ),
            registers: vec![
                ("A".into(), format!("{:X}", self.a)),
                ("X".into(), format!("{:X}", self.x)),
                ("Y".into(), format!("{:X}", self.y)),
                ("PA/PB".into(), format!("{:X}/{:X}", self.pa, self.pb)),
                ("PC".into(), format!("{:02X}", self.pc)),
                ("R".into(), format!("{:04X}", self.r_latch)),
                ("O".into(), format!("{:02X}", self.o_latch)),
            ],
            cycles: self.cycles,
            halted: false,
        }
    }

    pub fn disassemble(&self, start: u32, count: usize) -> Vec<DisassembledInstruction> {
        let mut out = Vec::with_capacity(count);
        let mut addr = (start as usize) & 0x7FF;
        for _ in 0..count {
            let op = self.rom[addr];
            let mnem = match op {
                0x08 => "TKA".into(),
                0x0A => "TDO".into(),
                0x0C => "RSTR".into(),
                0x0D => "SETR".into(),
                0x0E => "KNEZ".into(),
                0x0F => "RETN".into(),
                0x10..=0x1F => format!("LDP #{:X}", INV_4[(op & 0xF) as usize]),
                0x40..=0x4F => format!("TCY #{:X}", INV_4[(op & 0xF) as usize]),
                0x80..=0xBF => format!("BR ${:02X}", op & 0x3F),
                0xC0..=0xFF => format!("CALL ${:02X}", op & 0x3F),
                _ => format!("OP ${op:02X}"),
            };
            out.push(DisassembledInstruction {
                address: addr as u32,
                bytes: vec![op],
                mnemonic: mnem,
            });
            addr = (addr + 1) & 0x7FF;
        }
        out
    }
}
