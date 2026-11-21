use emu_core::{Bus8, CpuDebugState, DisassembledInstruction};

pub const FLAG_C: u8 = 0x01;
pub const FLAG_Z: u8 = 0x02;
pub const FLAG_I: u8 = 0x04;
pub const FLAG_D: u8 = 0x08;
pub const FLAG_B: u8 = 0x10;
pub const FLAG_U: u8 = 0x20;
pub const FLAG_V: u8 = 0x40;
pub const FLAG_N: u8 = 0x80;

const CYCLE_TABLE: [u8; 256] = [
    7, 6, 2, 2, 5, 3, 5, 5, 3, 2, 2, 2, 6, 4, 6, 5, // 0x00
    2, 5, 5, 2, 5, 4, 6, 5, 2, 4, 2, 2, 6, 4, 7, 5, // 0x10
    6, 6, 2, 2, 3, 3, 5, 5, 4, 2, 2, 2, 4, 4, 6, 5, // 0x20
    2, 5, 5, 2, 4, 4, 6, 5, 2, 4, 2, 2, 4, 4, 7, 5, // 0x30
    6, 6, 2, 2, 2, 3, 5, 5, 3, 2, 2, 2, 3, 4, 6, 5, // 0x40
    2, 5, 5, 2, 2, 4, 6, 5, 2, 4, 3, 2, 2, 4, 7, 5, // 0x50
    6, 6, 2, 2, 3, 3, 5, 5, 4, 2, 2, 2, 5, 4, 6, 5, // 0x60
    2, 5, 5, 2, 4, 4, 6, 5, 2, 4, 4, 2, 6, 4, 7, 5, // 0x70
    3, 6, 2, 2, 3, 3, 3, 5, 2, 2, 2, 2, 4, 4, 4, 5, // 0x80
    2, 6, 5, 2, 4, 4, 4, 5, 2, 5, 2, 2, 4, 5, 5, 5, // 0x90
    2, 6, 2, 2, 3, 3, 3, 5, 2, 2, 2, 2, 4, 4, 4, 5, // 0xA0
    2, 5, 5, 2, 4, 4, 4, 5, 2, 4, 2, 2, 4, 4, 4, 5, // 0xB0
    2, 6, 2, 2, 3, 3, 5, 5, 2, 2, 2, 3, 4, 4, 6, 5, // 0xC0
    2, 5, 5, 2, 2, 4, 6, 5, 2, 4, 3, 3, 2, 4, 7, 5, // 0xD0
    2, 6, 2, 2, 3, 3, 5, 5, 2, 2, 2, 2, 4, 4, 6, 5, // 0xE0
    2, 5, 5, 2, 2, 4, 6, 5, 2, 4, 4, 2, 2, 4, 7, 5, // 0xF0
];

#[derive(Debug, Clone)]
pub struct Mos6502 {
    pub a: u8,
    pub x: u8,
    pub y: u8,
    pub sp: u8,
    pub p: u8,
    pub pc: u16,
    pub cycles: u64,
    pub is_65c02: bool,
    pub halted: bool,
    pub irq_pending: bool,
    pub nmi_pending: bool,
}

impl Mos6502 {
    pub fn new(is_65c02: bool) -> Self {
        Self {
            a: 0,
            x: 0,
            y: 0,
            sp: 0xFD,
            p: FLAG_I | FLAG_U,
            pc: 0,
            cycles: 0,
            is_65c02,
            halted: false,
            irq_pending: false,
            nmi_pending: false,
        }
    }

    pub fn reset<B: Bus8>(&mut self, bus: &mut B) {
        self.a = 0;
        self.x = 0;
        self.y = 0;
        self.sp = 0xFD;
        self.p = FLAG_I | FLAG_U;
        self.halted = false;
        self.irq_pending = false;
        self.nmi_pending = false;
        let lo = bus.read8(0xFFFC) as u16;
        let hi = bus.read8(0xFFFD) as u16;
        self.pc = lo | (hi << 8);
        self.cycles = 7;
    }

    #[inline]
    pub fn trigger_irq(&mut self) {
        self.irq_pending = true;
    }

    #[inline]
    pub fn trigger_nmi(&mut self) {
        self.nmi_pending = true;
    }

    #[inline]
    fn set_zn(&mut self, val: u8) -> u8 {
        self.p &= !(FLAG_Z | FLAG_N);
        if val == 0 {
            self.p |= FLAG_Z;
        }
        if (val & 0x80) != 0 {
            self.p |= FLAG_N;
        }
        val
    }

    #[inline]
    fn read8<B: Bus8>(&self, bus: &mut B, addr: u16) -> u8 {
        bus.read8(addr)
    }

    #[inline]
    fn write8<B: Bus8>(&self, bus: &mut B, addr: u16, val: u8) {
        bus.write8(addr, val);
    }

    #[inline]
    fn fetch8<B: Bus8>(&mut self, bus: &mut B) -> u8 {
        let v = bus.read8(self.pc);
        self.pc = self.pc.wrapping_add(1);
        v
    }

    #[inline]
    fn fetch16<B: Bus8>(&mut self, bus: &mut B) -> u16 {
        let lo = self.fetch8(bus) as u16;
        let hi = self.fetch8(bus) as u16;
        lo | (hi << 8)
    }

    #[inline]
    fn push8<B: Bus8>(&mut self, bus: &mut B, val: u8) {
        bus.write8(0x0100 | (self.sp as u16), val);
        self.sp = self.sp.wrapping_sub(1);
    }

    #[inline]
    fn pop8<B: Bus8>(&mut self, bus: &mut B) -> u8 {
        self.sp = self.sp.wrapping_add(1);
        bus.read8(0x0100 | (self.sp as u16))
    }

    fn push16<B: Bus8>(&mut self, bus: &mut B, val: u16) {
        self.push8(bus, (val >> 8) as u8);
        self.push8(bus, (val & 0xFF) as u8);
    }

    fn pop16<B: Bus8>(&mut self, bus: &mut B) -> u16 {
        let lo = self.pop8(bus) as u16;
        let hi = self.pop8(bus) as u16;
        lo | (hi << 8)
    }

    fn service_nmi<B: Bus8>(&mut self, bus: &mut B) -> u32 {
        self.nmi_pending = false;
        self.halted = false;
        let pc = self.pc;
        self.push16(bus, pc);
        self.push8(bus, (self.p & !FLAG_B) | FLAG_U);
        self.p |= FLAG_I;
        if self.is_65c02 {
            self.p &= !FLAG_D;
        }
        let lo = bus.read8(0xFFFA) as u16;
        let hi = bus.read8(0xFFFB) as u16;
        self.pc = lo | (hi << 8);
        self.cycles += 7;
        7
    }

    fn service_irq<B: Bus8>(&mut self, bus: &mut B) -> u32 {
        self.irq_pending = false;
        self.halted = false;
        let pc = self.pc;
        self.push16(bus, pc);
        self.push8(bus, (self.p & !FLAG_B) | FLAG_U);
        self.p |= FLAG_I;
        if self.is_65c02 {
            self.p &= !FLAG_D;
        }
        let lo = bus.read8(0xFFFE) as u16;
        let hi = bus.read8(0xFFFF) as u16;
        self.pc = lo | (hi << 8);
        self.cycles += 7;
        7
    }

    fn addr_zp<B: Bus8>(&mut self, bus: &mut B) -> u16 {
        self.fetch8(bus) as u16
    }

    fn addr_zpx<B: Bus8>(&mut self, bus: &mut B) -> u16 {
        self.fetch8(bus).wrapping_add(self.x) as u16
    }

    fn addr_zpy<B: Bus8>(&mut self, bus: &mut B) -> u16 {
        self.fetch8(bus).wrapping_add(self.y) as u16
    }

    fn addr_abs<B: Bus8>(&mut self, bus: &mut B) -> u16 {
        self.fetch16(bus)
    }

    fn addr_abx<B: Bus8>(&mut self, bus: &mut B) -> (u16, bool) {
        let base = self.fetch16(bus);
        let eff = base.wrapping_add(self.x as u16);
        (eff, (base & 0xFF00) != (eff & 0xFF00))
    }

    fn addr_aby<B: Bus8>(&mut self, bus: &mut B) -> (u16, bool) {
        let base = self.fetch16(bus);
        let eff = base.wrapping_add(self.y as u16);
        (eff, (base & 0xFF00) != (eff & 0xFF00))
    }

    fn addr_izx<B: Bus8>(&mut self, bus: &mut B) -> u16 {
        let zp = self.fetch8(bus).wrapping_add(self.x);
        let lo = bus.read8(zp as u16) as u16;
        let hi = bus.read8(zp.wrapping_add(1) as u16) as u16;
        lo | (hi << 8)
    }

    fn addr_izy<B: Bus8>(&mut self, bus: &mut B) -> (u16, bool) {
        let zp = self.fetch8(bus);
        let lo = bus.read8(zp as u16) as u16;
        let hi = bus.read8(zp.wrapping_add(1) as u16) as u16;
        let base = lo | (hi << 8);
        let eff = base.wrapping_add(self.y as u16);
        (eff, (base & 0xFF00) != (eff & 0xFF00))
    }

    fn addr_zp_ind<B: Bus8>(&mut self, bus: &mut B) -> u16 {
        let zp = self.fetch8(bus);
        let lo = bus.read8(zp as u16) as u16;
        let hi = bus.read8(zp.wrapping_add(1) as u16) as u16;
        lo | (hi << 8)
    }

    fn branch<B: Bus8>(&mut self, bus: &mut B, cond: bool) -> u32 {
        let offset = self.fetch8(bus) as i8 as i32;
        if cond {
            let old_pc = self.pc;
            let new_pc = (old_pc as i32 + offset) as u16;
            self.pc = new_pc;
            if (old_pc & 0xFF00) != (new_pc & 0xFF00) {
                2
            } else {
                1
            }
        } else {
            0
        }
    }

    fn adc(&mut self, val: u8) -> u32 {
        let carry = if (self.p & FLAG_C) != 0 { 1u16 } else { 0u16 };
        if (self.p & FLAG_D) != 0 {
            let mut al = (self.a & 0x0F) as u16 + (val & 0x0F) as u16 + carry;
            if al > 9 {
                al += 6;
            }
            let mut sum = (self.a as u16 & 0xF0) + (val as u16 & 0xF0) + al;
            let bin_sum = self.a as u16 + val as u16 + carry;
            self.p &= !(FLAG_C | FLAG_Z | FLAG_N | FLAG_V);
            if (bin_sum & 0xFF) == 0 {
                self.p |= FLAG_Z;
            }
            if (sum & 0x80) != 0 {
                self.p |= FLAG_N;
            }
            if ((!(self.a as u16 ^ val as u16)) & (self.a as u16 ^ sum) & 0x80) != 0 {
                self.p |= FLAG_V;
            }
            if sum > 0x9F {
                sum += 0x60;
            }
            if sum > 0xFF {
                self.p |= FLAG_C;
            }
            self.a = (sum & 0xFF) as u8;
            if self.is_65c02 {
                self.set_zn(self.a);
                1
            } else {
                0
            }
        } else {
            let sum = self.a as u16 + val as u16 + carry;
            self.p &= !(FLAG_C | FLAG_V);
            if sum > 0xFF {
                self.p |= FLAG_C;
            }
            if ((!(self.a as u16 ^ val as u16)) & (self.a as u16 ^ sum) & 0x80) != 0 {
                self.p |= FLAG_V;
            }
            self.a = self.set_zn((sum & 0xFF) as u8);
            0
        }
    }

    fn sbc(&mut self, val: u8) -> u32 {
        let borrow = if (self.p & FLAG_C) != 0 { 0i16 } else { 1i16 };
        let inv = val ^ 0xFF;
        let bin_sum = self.a as u16 + inv as u16 + (if borrow == 0 { 1 } else { 0 });
        if (self.p & FLAG_D) != 0 {
            let mut al = (self.a & 0x0F) as i16 - (val & 0x0F) as i16 - borrow;
            let mut ah = (self.a >> 4) as i16 - (val >> 4) as i16;
            if al < 0 {
                al -= 6;
                ah -= 1;
            }
            if ah < 0 {
                ah -= 6;
            }
            self.p &= !(FLAG_C | FLAG_Z | FLAG_N | FLAG_V);
            if bin_sum > 0xFF {
                self.p |= FLAG_C;
            }
            if ((self.a as u16 ^ val as u16) & (self.a as u16 ^ bin_sum) & 0x80) != 0 {
                self.p |= FLAG_V;
            }
            let res = (((ah & 0x0F) << 4) | (al & 0x0F)) as u8;
            if (bin_sum & 0xFF) == 0 {
                self.p |= FLAG_Z;
            }
            if (bin_sum & 0x80) != 0 {
                self.p |= FLAG_N;
            }
            self.a = res;
            if self.is_65c02 {
                self.set_zn(self.a);
                1
            } else {
                0
            }
        } else {
            self.p &= !(FLAG_C | FLAG_V);
            if bin_sum > 0xFF {
                self.p |= FLAG_C;
            }
            if ((self.a as u16 ^ val as u16) & (self.a as u16 ^ bin_sum) & 0x80) != 0 {
                self.p |= FLAG_V;
            }
            self.a = self.set_zn((bin_sum & 0xFF) as u8);
            0
        }
    }

    #[inline]
    fn cmp(&mut self, reg: u8, val: u8) {
        let diff = reg.wrapping_sub(val);
        self.p &= !(FLAG_C | FLAG_Z | FLAG_N);
        if reg >= val {
            self.p |= FLAG_C;
        }
        self.set_zn(diff);
    }

    #[inline]
    fn bit_op(&mut self, val: u8, update_nv: bool) {
        self.p &= !FLAG_Z;
        if (self.a & val) == 0 {
            self.p |= FLAG_Z;
        }
        if update_nv {
            self.p &= !(FLAG_N | FLAG_V);
            self.p |= val & (FLAG_N | FLAG_V);
        }
    }

    #[inline]
    fn asl(&mut self, val: u8) -> u8 {
        self.p &= !FLAG_C;
        if (val & 0x80) != 0 {
            self.p |= FLAG_C;
        }
        self.set_zn(val << 1)
    }

    #[inline]
    fn lsr(&mut self, val: u8) -> u8 {
        self.p &= !FLAG_C;
        if (val & 0x01) != 0 {
            self.p |= FLAG_C;
        }
        self.set_zn(val >> 1)
    }

    #[inline]
    fn rol(&mut self, val: u8) -> u8 {
        let old_c = if (self.p & FLAG_C) != 0 { 1 } else { 0 };
        self.p &= !FLAG_C;
        if (val & 0x80) != 0 {
            self.p |= FLAG_C;
        }
        self.set_zn((val << 1) | old_c)
    }

    #[inline]
    fn ror(&mut self, val: u8) -> u8 {
        let old_c = if (self.p & FLAG_C) != 0 { 0x80 } else { 0 };
        self.p &= !FLAG_C;
        if (val & 0x01) != 0 {
            self.p |= FLAG_C;
        }
        self.set_zn((val >> 1) | old_c)
    }

    pub fn step<B: Bus8>(&mut self, bus: &mut B) -> u32 {
        if self.nmi_pending {
            return self.service_nmi(bus);
        }
        if self.irq_pending && (self.p & FLAG_I) == 0 {
            return self.service_irq(bus);
        }
        if self.halted {
            self.cycles += 1;
            return 1;
        }

        let opcode = self.fetch8(bus);
        let mut extra = 0u32;

        match opcode {
            // LDA
            0xA9 => {
                let v = self.fetch8(bus);
                self.a = self.set_zn(v);
            }
            0xA5 => {
                let a = self.addr_zp(bus);
                let v = self.read8(bus, a);
                self.a = self.set_zn(v);
            }
            0xB5 => {
                let a = self.addr_zpx(bus);
                let v = self.read8(bus, a);
                self.a = self.set_zn(v);
            }
            0xAD => {
                let a = self.addr_abs(bus);
                let v = self.read8(bus, a);
                self.a = self.set_zn(v);
            }
            0xBD => {
                let (a, c) = self.addr_abx(bus);
                if c {
                    extra += 1;
                }
                let v = self.read8(bus, a);
                self.a = self.set_zn(v);
            }
            0xB9 => {
                let (a, c) = self.addr_aby(bus);
                if c {
                    extra += 1;
                }
                let v = self.read8(bus, a);
                self.a = self.set_zn(v);
            }
            0xA1 => {
                let a = self.addr_izx(bus);
                let v = self.read8(bus, a);
                self.a = self.set_zn(v);
            }
            0xB1 => {
                let (a, c) = self.addr_izy(bus);
                if c {
                    extra += 1;
                }
                let v = self.read8(bus, a);
                self.a = self.set_zn(v);
            }
            0xB2 if self.is_65c02 => {
                let a = self.addr_zp_ind(bus);
                let v = self.read8(bus, a);
                self.a = self.set_zn(v);
            }

            // LDX
            0xA2 => {
                let v = self.fetch8(bus);
                self.x = self.set_zn(v);
            }
            0xA6 => {
                let a = self.addr_zp(bus);
                let v = self.read8(bus, a);
                self.x = self.set_zn(v);
            }
            0xB6 => {
                let a = self.addr_zpy(bus);
                let v = self.read8(bus, a);
                self.x = self.set_zn(v);
            }
            0xAE => {
                let a = self.addr_abs(bus);
                let v = self.read8(bus, a);
                self.x = self.set_zn(v);
            }
            0xBE => {
                let (a, c) = self.addr_aby(bus);
                if c {
                    extra += 1;
                }
                let v = self.read8(bus, a);
                self.x = self.set_zn(v);
            }

            // LDY
            0xA0 => {
                let v = self.fetch8(bus);
                self.y = self.set_zn(v);
            }
            0xA4 => {
                let a = self.addr_zp(bus);
                let v = self.read8(bus, a);
                self.y = self.set_zn(v);
            }
            0xB4 => {
                let a = self.addr_zpx(bus);
                let v = self.read8(bus, a);
                self.y = self.set_zn(v);
            }
            0xAC => {
                let a = self.addr_abs(bus);
                let v = self.read8(bus, a);
                self.y = self.set_zn(v);
            }
            0xBC => {
                let (a, c) = self.addr_abx(bus);
                if c {
                    extra += 1;
                }
                let v = self.read8(bus, a);
                self.y = self.set_zn(v);
            }

            // STA
            0x85 => {
                let a = self.addr_zp(bus);
                self.write8(bus, a, self.a);
            }
            0x95 => {
                let a = self.addr_zpx(bus);
                self.write8(bus, a, self.a);
            }
            0x8D => {
                let a = self.addr_abs(bus);
                self.write8(bus, a, self.a);
            }
            0x9D => {
                let (a, _) = self.addr_abx(bus);
                self.write8(bus, a, self.a);
            }
            0x99 => {
                let (a, _) = self.addr_aby(bus);
                self.write8(bus, a, self.a);
            }
            0x81 => {
                let a = self.addr_izx(bus);
                self.write8(bus, a, self.a);
            }
            0x91 => {
                let (a, _) = self.addr_izy(bus);
                self.write8(bus, a, self.a);
            }
            0x92 if self.is_65c02 => {
                let a = self.addr_zp_ind(bus);
                self.write8(bus, a, self.a);
            }

            // STX
            0x86 => {
                let a = self.addr_zp(bus);
                self.write8(bus, a, self.x);
            }
            0x96 => {
                let a = self.addr_zpy(bus);
                self.write8(bus, a, self.x);
            }
            0x8E => {
                let a = self.addr_abs(bus);
                self.write8(bus, a, self.x);
            }

            // STY
            0x84 => {
                let a = self.addr_zp(bus);
                self.write8(bus, a, self.y);
            }
            0x94 => {
                let a = self.addr_zpx(bus);
                self.write8(bus, a, self.y);
            }
            0x8C => {
                let a = self.addr_abs(bus);
                self.write8(bus, a, self.y);
            }

            // STZ (65C02)
            0x64 if self.is_65c02 => {
                let a = self.addr_zp(bus);
                self.write8(bus, a, 0);
            }
            0x74 if self.is_65c02 => {
                let a = self.addr_zpx(bus);
                self.write8(bus, a, 0);
            }
            0x9C if self.is_65c02 => {
                let a = self.addr_abs(bus);
                self.write8(bus, a, 0);
            }
            0x9E if self.is_65c02 => {
                let (a, _) = self.addr_abx(bus);
                self.write8(bus, a, 0);
            }

            // Transfers
            0xAA => self.x = self.set_zn(self.a),
            0xA8 => self.y = self.set_zn(self.a),
            0x8A => self.a = self.set_zn(self.x),
            0x98 => self.a = self.set_zn(self.y),
            0xBA => self.x = self.set_zn(self.sp),
            0x9A => self.sp = self.x,

            // Stack
            0x48 => self.push8(bus, self.a),
            0x68 => {
                let v = self.pop8(bus);
                self.a = self.set_zn(v);
            }
            0x08 => self.push8(bus, self.p | FLAG_B | FLAG_U),
            0x28 => self.p = (self.pop8(bus) & !FLAG_B) | FLAG_U,
            0xDA if self.is_65c02 => self.push8(bus, self.x),
            0xFA if self.is_65c02 => {
                let v = self.pop8(bus);
                self.x = self.set_zn(v);
            }
            0x5A if self.is_65c02 => self.push8(bus, self.y),
            0x7A if self.is_65c02 => {
                let v = self.pop8(bus);
                self.y = self.set_zn(v);
            }

            // AND
            0x29 => {
                let v = self.fetch8(bus);
                self.a = self.set_zn(self.a & v);
            }
            0x25 => {
                let a = self.addr_zp(bus);
                let v = self.read8(bus, a);
                self.a = self.set_zn(self.a & v);
            }
            0x35 => {
                let a = self.addr_zpx(bus);
                let v = self.read8(bus, a);
                self.a = self.set_zn(self.a & v);
            }
            0x2D => {
                let a = self.addr_abs(bus);
                let v = self.read8(bus, a);
                self.a = self.set_zn(self.a & v);
            }
            0x3D => {
                let (a, c) = self.addr_abx(bus);
                if c {
                    extra += 1;
                }
                let v = self.read8(bus, a);
                self.a = self.set_zn(self.a & v);
            }
            0x39 => {
                let (a, c) = self.addr_aby(bus);
                if c {
                    extra += 1;
                }
                let v = self.read8(bus, a);
                self.a = self.set_zn(self.a & v);
            }
            0x21 => {
                let a = self.addr_izx(bus);
                let v = self.read8(bus, a);
                self.a = self.set_zn(self.a & v);
            }
            0x31 => {
                let (a, c) = self.addr_izy(bus);
                if c {
                    extra += 1;
                }
                let v = self.read8(bus, a);
                self.a = self.set_zn(self.a & v);
            }
            0x32 if self.is_65c02 => {
                let a = self.addr_zp_ind(bus);
                let v = self.read8(bus, a);
                self.a = self.set_zn(self.a & v);
            }

            // ORA
            0x09 => {
                let v = self.fetch8(bus);
                self.a = self.set_zn(self.a | v);
            }
            0x05 => {
                let a = self.addr_zp(bus);
                let v = self.read8(bus, a);
                self.a = self.set_zn(self.a | v);
            }
            0x15 => {
                let a = self.addr_zpx(bus);
                let v = self.read8(bus, a);
                self.a = self.set_zn(self.a | v);
            }
            0x0D => {
                let a = self.addr_abs(bus);
                let v = self.read8(bus, a);
                self.a = self.set_zn(self.a | v);
            }
            0x1D => {
                let (a, c) = self.addr_abx(bus);
                if c {
                    extra += 1;
                }
                let v = self.read8(bus, a);
                self.a = self.set_zn(self.a | v);
            }
            0x19 => {
                let (a, c) = self.addr_aby(bus);
                if c {
                    extra += 1;
                }
                let v = self.read8(bus, a);
                self.a = self.set_zn(self.a | v);
            }
            0x01 => {
                let a = self.addr_izx(bus);
                let v = self.read8(bus, a);
                self.a = self.set_zn(self.a | v);
            }
            0x11 => {
                let (a, c) = self.addr_izy(bus);
                if c {
                    extra += 1;
                }
                let v = self.read8(bus, a);
                self.a = self.set_zn(self.a | v);
            }
            0x12 if self.is_65c02 => {
                let a = self.addr_zp_ind(bus);
                let v = self.read8(bus, a);
                self.a = self.set_zn(self.a | v);
            }

            // EOR
            0x49 => {
                let v = self.fetch8(bus);
                self.a = self.set_zn(self.a ^ v);
            }
            0x45 => {
                let a = self.addr_zp(bus);
                let v = self.read8(bus, a);
                self.a = self.set_zn(self.a ^ v);
            }
            0x55 => {
                let a = self.addr_zpx(bus);
                let v = self.read8(bus, a);
                self.a = self.set_zn(self.a ^ v);
            }
            0x4D => {
                let a = self.addr_abs(bus);
                let v = self.read8(bus, a);
                self.a = self.set_zn(self.a ^ v);
            }
            0x5D => {
                let (a, c) = self.addr_abx(bus);
                if c {
                    extra += 1;
                }
                let v = self.read8(bus, a);
                self.a = self.set_zn(self.a ^ v);
            }
            0x59 => {
                let (a, c) = self.addr_aby(bus);
                if c {
                    extra += 1;
                }
                let v = self.read8(bus, a);
                self.a = self.set_zn(self.a ^ v);
            }
            0x41 => {
                let a = self.addr_izx(bus);
                let v = self.read8(bus, a);
                self.a = self.set_zn(self.a ^ v);
            }
            0x51 => {
                let (a, c) = self.addr_izy(bus);
                if c {
                    extra += 1;
                }
                let v = self.read8(bus, a);
                self.a = self.set_zn(self.a ^ v);
            }
            0x52 if self.is_65c02 => {
                let a = self.addr_zp_ind(bus);
                let v = self.read8(bus, a);
                self.a = self.set_zn(self.a ^ v);
            }

            // BIT / TSB / TRB
            0x24 => {
                let a = self.addr_zp(bus);
                let v = self.read8(bus, a);
                self.bit_op(v, true);
            }
            0x2C => {
                let a = self.addr_abs(bus);
                let v = self.read8(bus, a);
                self.bit_op(v, true);
            }
            0x89 if self.is_65c02 => {
                let v = self.fetch8(bus);
                self.bit_op(v, false);
            }
            0x34 if self.is_65c02 => {
                let a = self.addr_zpx(bus);
                let v = self.read8(bus, a);
                self.bit_op(v, true);
            }
            0x3C if self.is_65c02 => {
                let (a, c) = self.addr_abx(bus);
                if c {
                    extra += 1;
                }
                let v = self.read8(bus, a);
                self.bit_op(v, true);
            }
            0x04 if self.is_65c02 => {
                let a = self.addr_zp(bus);
                let v = self.read8(bus, a);
                self.p &= !FLAG_Z;
                if (v & self.a) == 0 {
                    self.p |= FLAG_Z;
                }
                self.write8(bus, a, v | self.a);
            }
            0x0C if self.is_65c02 => {
                let a = self.addr_abs(bus);
                let v = self.read8(bus, a);
                self.p &= !FLAG_Z;
                if (v & self.a) == 0 {
                    self.p |= FLAG_Z;
                }
                self.write8(bus, a, v | self.a);
            }
            0x14 if self.is_65c02 => {
                let a = self.addr_zp(bus);
                let v = self.read8(bus, a);
                self.p &= !FLAG_Z;
                if (v & self.a) == 0 {
                    self.p |= FLAG_Z;
                }
                self.write8(bus, a, v & !self.a);
            }
            0x1C if self.is_65c02 => {
                let a = self.addr_abs(bus);
                let v = self.read8(bus, a);
                self.p &= !FLAG_Z;
                if (v & self.a) == 0 {
                    self.p |= FLAG_Z;
                }
                self.write8(bus, a, v & !self.a);
            }

            // ADC
            0x69 => {
                let v = self.fetch8(bus);
                extra += self.adc(v);
            }
            0x65 => {
                let a = self.addr_zp(bus);
                let v = self.read8(bus, a);
                extra += self.adc(v);
            }
            0x75 => {
                let a = self.addr_zpx(bus);
                let v = self.read8(bus, a);
                extra += self.adc(v);
            }
            0x6D => {
                let a = self.addr_abs(bus);
                let v = self.read8(bus, a);
                extra += self.adc(v);
            }
            0x7D => {
                let (a, c) = self.addr_abx(bus);
                if c {
                    extra += 1;
                }
                let v = self.read8(bus, a);
                extra += self.adc(v);
            }
            0x79 => {
                let (a, c) = self.addr_aby(bus);
                if c {
                    extra += 1;
                }
                let v = self.read8(bus, a);
                extra += self.adc(v);
            }
            0x61 => {
                let a = self.addr_izx(bus);
                let v = self.read8(bus, a);
                extra += self.adc(v);
            }
            0x71 => {
                let (a, c) = self.addr_izy(bus);
                if c {
                    extra += 1;
                }
                let v = self.read8(bus, a);
                extra += self.adc(v);
            }
            0x72 if self.is_65c02 => {
                let a = self.addr_zp_ind(bus);
                let v = self.read8(bus, a);
                extra += self.adc(v);
            }

            // SBC
            0xE9 => {
                let v = self.fetch8(bus);
                extra += self.sbc(v);
            }
            0xE5 => {
                let a = self.addr_zp(bus);
                let v = self.read8(bus, a);
                extra += self.sbc(v);
            }
            0xF5 => {
                let a = self.addr_zpx(bus);
                let v = self.read8(bus, a);
                extra += self.sbc(v);
            }
            0xED => {
                let a = self.addr_abs(bus);
                let v = self.read8(bus, a);
                extra += self.sbc(v);
            }
            0xFD => {
                let (a, c) = self.addr_abx(bus);
                if c {
                    extra += 1;
                }
                let v = self.read8(bus, a);
                extra += self.sbc(v);
            }
            0xF9 => {
                let (a, c) = self.addr_aby(bus);
                if c {
                    extra += 1;
                }
                let v = self.read8(bus, a);
                extra += self.sbc(v);
            }
            0xE1 => {
                let a = self.addr_izx(bus);
                let v = self.read8(bus, a);
                extra += self.sbc(v);
            }
            0xF1 => {
                let (a, c) = self.addr_izy(bus);
                if c {
                    extra += 1;
                }
                let v = self.read8(bus, a);
                extra += self.sbc(v);
            }
            0xF2 if self.is_65c02 => {
                let a = self.addr_zp_ind(bus);
                let v = self.read8(bus, a);
                extra += self.sbc(v);
            }

            // CMP
            0xC9 => {
                let v = self.fetch8(bus);
                self.cmp(self.a, v);
            }
            0xC5 => {
                let a = self.addr_zp(bus);
                let v = self.read8(bus, a);
                self.cmp(self.a, v);
            }
            0xD5 => {
                let a = self.addr_zpx(bus);
                let v = self.read8(bus, a);
                self.cmp(self.a, v);
            }
            0xCD => {
                let a = self.addr_abs(bus);
                let v = self.read8(bus, a);
                self.cmp(self.a, v);
            }
            0xDD => {
                let (a, c) = self.addr_abx(bus);
                if c {
                    extra += 1;
                }
                let v = self.read8(bus, a);
                self.cmp(self.a, v);
            }
            0xD9 => {
                let (a, c) = self.addr_aby(bus);
                if c {
                    extra += 1;
                }
                let v = self.read8(bus, a);
                self.cmp(self.a, v);
            }
            0xC1 => {
                let a = self.addr_izx(bus);
                let v = self.read8(bus, a);
                self.cmp(self.a, v);
            }
            0xD1 => {
                let (a, c) = self.addr_izy(bus);
                if c {
                    extra += 1;
                }
                let v = self.read8(bus, a);
                self.cmp(self.a, v);
            }
            0xD2 if self.is_65c02 => {
                let a = self.addr_zp_ind(bus);
                let v = self.read8(bus, a);
                self.cmp(self.a, v);
            }

            // CPX / CPY
            0xE0 => {
                let v = self.fetch8(bus);
                self.cmp(self.x, v);
            }
            0xE4 => {
                let a = self.addr_zp(bus);
                let v = self.read8(bus, a);
                self.cmp(self.x, v);
            }
            0xEC => {
                let a = self.addr_abs(bus);
                let v = self.read8(bus, a);
                self.cmp(self.x, v);
            }
            0xC0 => {
                let v = self.fetch8(bus);
                self.cmp(self.y, v);
            }
            0xC4 => {
                let a = self.addr_zp(bus);
                let v = self.read8(bus, a);
                self.cmp(self.y, v);
            }
            0xCC => {
                let a = self.addr_abs(bus);
                let v = self.read8(bus, a);
                self.cmp(self.y, v);
            }

            // INC / DEC
            0xE6 => {
                let a = self.addr_zp(bus);
                let v = self.set_zn(self.read8(bus, a).wrapping_add(1));
                self.write8(bus, a, v);
            }
            0xF6 => {
                let a = self.addr_zpx(bus);
                let v = self.set_zn(self.read8(bus, a).wrapping_add(1));
                self.write8(bus, a, v);
            }
            0xEE => {
                let a = self.addr_abs(bus);
                let v = self.set_zn(self.read8(bus, a).wrapping_add(1));
                self.write8(bus, a, v);
            }
            0xFE => {
                let (a, _) = self.addr_abx(bus);
                let v = self.set_zn(self.read8(bus, a).wrapping_add(1));
                self.write8(bus, a, v);
            }
            0xE8 => self.x = self.set_zn(self.x.wrapping_add(1)),
            0xC8 => self.y = self.set_zn(self.y.wrapping_add(1)),
            0x1A if self.is_65c02 => self.a = self.set_zn(self.a.wrapping_add(1)),

            0xC6 => {
                let a = self.addr_zp(bus);
                let v = self.set_zn(self.read8(bus, a).wrapping_sub(1));
                self.write8(bus, a, v);
            }
            0xD6 => {
                let a = self.addr_zpx(bus);
                let v = self.set_zn(self.read8(bus, a).wrapping_sub(1));
                self.write8(bus, a, v);
            }
            0xCE => {
                let a = self.addr_abs(bus);
                let v = self.set_zn(self.read8(bus, a).wrapping_sub(1));
                self.write8(bus, a, v);
            }
            0xDE => {
                let (a, _) = self.addr_abx(bus);
                let v = self.set_zn(self.read8(bus, a).wrapping_sub(1));
                self.write8(bus, a, v);
            }
            0xCA => self.x = self.set_zn(self.x.wrapping_sub(1)),
            0x88 => self.y = self.set_zn(self.y.wrapping_sub(1)),
            0x3A if self.is_65c02 => self.a = self.set_zn(self.a.wrapping_sub(1)),

            // Shifts & Rotates
            0x0A => self.a = self.asl(self.a),
            0x06 => {
                let a = self.addr_zp(bus);
                let v = self.asl(self.read8(bus, a));
                self.write8(bus, a, v);
            }
            0x16 => {
                let a = self.addr_zpx(bus);
                let v = self.asl(self.read8(bus, a));
                self.write8(bus, a, v);
            }
            0x0E => {
                let a = self.addr_abs(bus);
                let v = self.asl(self.read8(bus, a));
                self.write8(bus, a, v);
            }
            0x1E => {
                let (a, _) = self.addr_abx(bus);
                let v = self.asl(self.read8(bus, a));
                self.write8(bus, a, v);
            }

            0x4A => self.a = self.lsr(self.a),
            0x46 => {
                let a = self.addr_zp(bus);
                let v = self.lsr(self.read8(bus, a));
                self.write8(bus, a, v);
            }
            0x56 => {
                let a = self.addr_zpx(bus);
                let v = self.lsr(self.read8(bus, a));
                self.write8(bus, a, v);
            }
            0x4E => {
                let a = self.addr_abs(bus);
                let v = self.lsr(self.read8(bus, a));
                self.write8(bus, a, v);
            }
            0x5E => {
                let (a, _) = self.addr_abx(bus);
                let v = self.lsr(self.read8(bus, a));
                self.write8(bus, a, v);
            }

            0x2A => self.a = self.rol(self.a),
            0x26 => {
                let a = self.addr_zp(bus);
                let v = self.rol(self.read8(bus, a));
                self.write8(bus, a, v);
            }
            0x36 => {
                let a = self.addr_zpx(bus);
                let v = self.rol(self.read8(bus, a));
                self.write8(bus, a, v);
            }
            0x2E => {
                let a = self.addr_abs(bus);
                let v = self.rol(self.read8(bus, a));
                self.write8(bus, a, v);
            }
            0x3E => {
                let (a, _) = self.addr_abx(bus);
                let v = self.rol(self.read8(bus, a));
                self.write8(bus, a, v);
            }

            0x6A => self.a = self.ror(self.a),
            0x66 => {
                let a = self.addr_zp(bus);
                let v = self.ror(self.read8(bus, a));
                self.write8(bus, a, v);
            }
            0x76 => {
                let a = self.addr_zpx(bus);
                let v = self.ror(self.read8(bus, a));
                self.write8(bus, a, v);
            }
            0x6E => {
                let a = self.addr_abs(bus);
                let v = self.ror(self.read8(bus, a));
                self.write8(bus, a, v);
            }
            0x7E => {
                let (a, _) = self.addr_abx(bus);
                let v = self.ror(self.read8(bus, a));
                self.write8(bus, a, v);
            }

            // Branches
            0x90 => extra += self.branch(bus, (self.p & FLAG_C) == 0),
            0xB0 => extra += self.branch(bus, (self.p & FLAG_C) != 0),
            0xF0 => extra += self.branch(bus, (self.p & FLAG_Z) != 0),
            0xD0 => extra += self.branch(bus, (self.p & FLAG_Z) == 0),
            0x30 => extra += self.branch(bus, (self.p & FLAG_N) != 0),
            0x10 => extra += self.branch(bus, (self.p & FLAG_N) == 0),
            0x50 => extra += self.branch(bus, (self.p & FLAG_V) == 0),
            0x70 => extra += self.branch(bus, (self.p & FLAG_V) != 0),
            0x80 if self.is_65c02 => extra += self.branch(bus, true),

            // Jumps & Subroutines
            0x4C => self.pc = self.fetch16(bus),
            0x6C => {
                let ptr = self.fetch16(bus);
                let lo = bus.read8(ptr) as u16;
                let hi = if !self.is_65c02 && (ptr & 0x00FF) == 0x00FF {
                    bus.read8(ptr & 0xFF00) as u16
                } else {
                    bus.read8(ptr.wrapping_add(1)) as u16
                };
                self.pc = lo | (hi << 8);
                if self.is_65c02 {
                    extra += 1;
                }
            }
            0x7C if self.is_65c02 => {
                let ptr = self.fetch16(bus).wrapping_add(self.x as u16);
                let lo = bus.read8(ptr) as u16;
                let hi = bus.read8(ptr.wrapping_add(1)) as u16;
                self.pc = lo | (hi << 8);
            }
            0x20 => {
                let target = self.fetch16(bus);
                let ret = self.pc.wrapping_sub(1);
                self.push16(bus, ret);
                self.pc = target;
            }
            0x60 => {
                let ret = self.pop16(bus);
                self.pc = ret.wrapping_add(1);
            }
            0x40 => {
                self.p = (self.pop8(bus) & !FLAG_B) | FLAG_U;
                self.pc = self.pop16(bus);
            }

            // Flags
            0x18 => self.p &= !FLAG_C,
            0x38 => self.p |= FLAG_C,
            0x58 => self.p &= !FLAG_I,
            0x78 => self.p |= FLAG_I,
            0xB8 => self.p &= !FLAG_V,
            0xD8 => self.p &= !FLAG_D,
            0xF8 => self.p |= FLAG_D,

            // BRK / NOP / WAI / STP
            0x00 => {
                self.pc = self.pc.wrapping_add(1);
                let pc = self.pc;
                self.push16(bus, pc);
                self.push8(bus, self.p | FLAG_B | FLAG_U);
                self.p |= FLAG_I;
                if self.is_65c02 {
                    self.p &= !FLAG_D;
                }
                let lo = bus.read8(0xFFFE) as u16;
                let hi = bus.read8(0xFFFF) as u16;
                self.pc = lo | (hi << 8);
            }
            0xEA => {}
            0xCB if self.is_65c02 => {
                self.halted = true;
            }
            0xDB if self.is_65c02 => {
                self.halted = true;
            }

            // Rockwell / WDC 65C02 RMB/SMB/BBR/BBS
            op if self.is_65c02 && (op & 0x0F) == 0x07 => {
                let bit = (op >> 4) & 7;
                let a = self.addr_zp(bus);
                let v = self.read8(bus, a);
                if (op & 0x80) == 0 {
                    self.write8(bus, a, v & !(1u8 << bit));
                } else {
                    self.write8(bus, a, v | (1u8 << bit));
                }
            }
            op if self.is_65c02 && (op & 0x0F) == 0x0F => {
                let bit = (op >> 4) & 7;
                let a = self.addr_zp(bus);
                let v = self.read8(bus, a);
                let set = (v & (1u8 << bit)) != 0;
                let want = (op & 0x80) != 0;
                extra += self.branch(bus, set == want);
            }

            // 65C02 multi-byte NOPs for undocumented slots
            op if self.is_65c02 => match op & 0x0F {
                0x02 => {
                    let _ = self.fetch8(bus);
                }
                0x04 | 0x0C => {
                    if op == 0x5C {
                        let _ = self.fetch16(bus);
                    } else if (op & 0x0F) == 0x04 {
                        let _ = self.fetch8(bus);
                    } else {
                        let _ = self.fetch16(bus);
                    }
                }
                _ => {}
            },

            // NMOS undocumented opcodes treated cleanly as 2-cycle NOPs
            _ => {}
        }

        let total = CYCLE_TABLE[opcode as usize] as u32 + extra;
        self.cycles += total as u64;
        total
    }

    pub fn debug_state(&self) -> CpuDebugState {
        let flags = format!(
            "{}{}{}{}{}{}{}",
            if (self.p & FLAG_N) != 0 { 'N' } else { '-' },
            if (self.p & FLAG_V) != 0 { 'V' } else { '-' },
            if (self.p & FLAG_B) != 0 { 'B' } else { '-' },
            if (self.p & FLAG_D) != 0 { 'D' } else { '-' },
            if (self.p & FLAG_I) != 0 { 'I' } else { '-' },
            if (self.p & FLAG_Z) != 0 { 'Z' } else { '-' },
            if (self.p & FLAG_C) != 0 { 'C' } else { '-' },
        );
        CpuDebugState {
            arch: if self.is_65c02 { "WDC 65C02" } else { "MOS 6502" },
            pc: self.pc as u32,
            sp: 0x0100 | (self.sp as u32),
            flags,
            registers: vec![
                ("A".into(), format!("{:02X}", self.a)),
                ("X".into(), format!("{:02X}", self.x)),
                ("Y".into(), format!("{:02X}", self.y)),
                ("SP".into(), format!("{:02X}", self.sp)),
                ("P".into(), format!("{:02X}", self.p)),
                (
                    "IRQ".into(),
                    if self.irq_pending { "PEND" } else { "CLR" }.into(),
                ),
                (
                    "NMI".into(),
                    if self.nmi_pending { "PEND" } else { "CLR" }.into(),
                ),
            ],
            cycles: self.cycles,
            halted: self.halted,
        }
    }

    pub fn disassemble<B: Bus8>(&self, bus: &B, start: u16, count: usize) -> Vec<DisassembledInstruction> {
        let mut out = Vec::with_capacity(count);
        let mut addr = start;
        for _ in 0..count {
            let op = bus.peek8(addr);
            let b1 = bus.peek8(addr.wrapping_add(1));
            let b2 = bus.peek8(addr.wrapping_add(2));
            let (len, mnemonic) = disasm_6502_one(op, addr, b1, b2, self.is_65c02);
            let mut bytes = Vec::with_capacity(len);
            for i in 0..len {
                bytes.push(bus.peek8(addr.wrapping_add(i as u16)));
            }
            out.push(DisassembledInstruction {
                address: addr as u32,
                bytes,
                mnemonic,
            });
            addr = addr.wrapping_add(len as u16);
        }
        out
    }
}

fn disasm_6502_one(op: u8, addr: u16, b1: u8, b2: u8, is_65c02: bool) -> (usize, String) {
    let abs = (b1 as u16) | ((b2 as u16) << 8);
    let rel = addr.wrapping_add(2).wrapping_add(b1 as i8 as i16 as u16);
    match op {
        0x00 => (1, "BRK".into()),
        0x01 => (2, format!("ORA (${b1:02X},X)")),
        0x04 if is_65c02 => (2, format!("TSB ${b1:02X}")),
        0x05 => (2, format!("ORA ${b1:02X}")),
        0x06 => (2, format!("ASL ${b1:02X}")),
        0x08 => (1, "PHP".into()),
        0x09 => (2, format!("ORA #${b1:02X}")),
        0x0A => (1, "ASL A".into()),
        0x0C if is_65c02 => (3, format!("TSB ${abs:04X}")),
        0x0D => (3, format!("ORA ${abs:04X}")),
        0x0E => (3, format!("ASL ${abs:04X}")),
        0x10 => (2, format!("BPL ${rel:04X}")),
        0x11 => (2, format!("ORA (${b1:02X}),Y")),
        0x12 if is_65c02 => (2, format!("ORA (${b1:02X})")),
        0x14 if is_65c02 => (2, format!("TRB ${b1:02X}")),
        0x15 => (2, format!("ORA ${b1:02X},X")),
        0x16 => (2, format!("ASL ${b1:02X},X")),
        0x18 => (1, "CLC".into()),
        0x19 => (3, format!("ORA ${abs:04X},Y")),
        0x1A if is_65c02 => (1, "INC A".into()),
        0x1C if is_65c02 => (3, format!("TRB ${abs:04X}")),
        0x1D => (3, format!("ORA ${abs:04X},X")),
        0x1E => (3, format!("ASL ${abs:04X},X")),
        0x20 => (3, format!("JSR ${abs:04X}")),
        0x21 => (2, format!("AND (${b1:02X},X)")),
        0x24 => (2, format!("BIT ${b1:02X}")),
        0x25 => (2, format!("AND ${b1:02X}")),
        0x26 => (2, format!("ROL ${b1:02X}")),
        0x28 => (1, "PLP".into()),
        0x29 => (2, format!("AND #${b1:02X}")),
        0x2A => (1, "ROL A".into()),
        0x2C => (3, format!("BIT ${abs:04X}")),
        0x2D => (3, format!("AND ${abs:04X}")),
        0x2E => (3, format!("ROL ${abs:04X}")),
        0x30 => (2, format!("BMI ${rel:04X}")),
        0x31 => (2, format!("AND (${b1:02X}),Y")),
        0x32 if is_65c02 => (2, format!("AND (${b1:02X})")),
        0x34 if is_65c02 => (2, format!("BIT ${b1:02X},X")),
        0x35 => (2, format!("AND ${b1:02X},X")),
        0x36 => (2, format!("ROL ${b1:02X},X")),
        0x38 => (1, "SEC".into()),
        0x39 => (3, format!("AND ${abs:04X},Y")),
        0x3A if is_65c02 => (1, "DEC A".into()),
        0x3C if is_65c02 => (3, format!("BIT ${abs:04X},X")),
        0x3D => (3, format!("AND ${abs:04X},X")),
        0x3E => (3, format!("ROL ${abs:04X},X")),
        0x40 => (1, "RTI".into()),
        0x41 => (2, format!("EOR (${b1:02X},X)")),
        0x45 => (2, format!("EOR ${b1:02X}")),
        0x46 => (2, format!("LSR ${b1:02X}")),
        0x48 => (1, "PHA".into()),
        0x49 => (2, format!("EOR #${b1:02X}")),
        0x4A => (1, "LSR A".into()),
        0x4C => (3, format!("JMP ${abs:04X}")),
        0x4D => (3, format!("EOR ${abs:04X}")),
        0x4E => (3, format!("LSR ${abs:04X}")),
        0x50 => (2, format!("BVC ${rel:04X}")),
        0x51 => (2, format!("EOR (${b1:02X}),Y")),
        0x52 if is_65c02 => (2, format!("EOR (${b1:02X})")),
        0x55 => (2, format!("EOR ${b1:02X},X")),
        0x56 => (2, format!("LSR ${b1:02X},X")),
        0x58 => (1, "CLI".into()),
        0x59 => (3, format!("EOR ${abs:04X},Y")),
        0x5A if is_65c02 => (1, "PHY".into()),
        0x5D => (3, format!("EOR ${abs:04X},X")),
        0x5E => (3, format!("LSR ${abs:04X},X")),
        0x60 => (1, "RTS".into()),
        0x61 => (2, format!("ADC (${b1:02X},X)")),
        0x64 if is_65c02 => (2, format!("STZ ${b1:02X}")),
        0x65 => (2, format!("ADC ${b1:02X}")),
        0x66 => (2, format!("ROR ${b1:02X}")),
        0x68 => (1, "PLA".into()),
        0x69 => (2, format!("ADC #${b1:02X}")),
        0x6A => (1, "ROR A".into()),
        0x6C => (3, format!("JMP (${abs:04X})")),
        0x6D => (3, format!("ADC ${abs:04X}")),
        0x6E => (3, format!("ROR ${abs:04X}")),
        0x70 => (2, format!("BVS ${rel:04X}")),
        0x71 => (2, format!("ADC (${b1:02X}),Y")),
        0x72 if is_65c02 => (2, format!("ADC (${b1:02X})")),
        0x74 if is_65c02 => (2, format!("STZ ${b1:02X},X")),
        0x75 => (2, format!("ADC ${b1:02X},X")),
        0x76 => (2, format!("ROR ${b1:02X},X")),
        0x78 => (1, "SEI".into()),
        0x79 => (3, format!("ADC ${abs:04X},Y")),
        0x7A if is_65c02 => (1, "PLY".into()),
        0x7C if is_65c02 => (3, format!("JMP (${abs:04X},X)")),
        0x7D => (3, format!("ADC ${abs:04X},X")),
        0x7E => (3, format!("ROR ${abs:04X},X")),
        0x80 if is_65c02 => (2, format!("BRA ${rel:04X}")),
        0x81 => (2, format!("STA (${b1:02X},X)")),
        0x84 => (2, format!("STY ${b1:02X}")),
        0x85 => (2, format!("STA ${b1:02X}")),
        0x86 => (2, format!("STX ${b1:02X}")),
        0x88 => (1, "DEY".into()),
        0x89 if is_65c02 => (2, format!("BIT #${b1:02X}")),
        0x8A => (1, "TXA".into()),
        0x8C => (3, format!("STY ${abs:04X}")),
        0x8D => (3, format!("STA ${abs:04X}")),
        0x8E => (3, format!("STX ${abs:04X}")),
        0x90 => (2, format!("BCC ${rel:04X}")),
        0x91 => (2, format!("STA (${b1:02X}),Y")),
        0x92 if is_65c02 => (2, format!("STA (${b1:02X})")),
        0x94 => (2, format!("STY ${b1:02X},X")),
        0x95 => (2, format!("STA ${b1:02X},X")),
        0x96 => (2, format!("STX ${b1:02X},Y")),
        0x98 => (1, "TYA".into()),
        0x99 => (3, format!("STA ${abs:04X},Y")),
        0x9A => (1, "TXS".into()),
        0x9C if is_65c02 => (3, format!("STZ ${abs:04X}")),
        0x9D => (3, format!("STA ${abs:04X},X")),
        0x9E if is_65c02 => (3, format!("STZ ${abs:04X},X")),
        0xA0 => (2, format!("LDY #${b1:02X}")),
        0xA1 => (2, format!("LDA (${b1:02X},X)")),
        0xA2 => (2, format!("LDX #${b1:02X}")),
        0xA4 => (2, format!("LDY ${b1:02X}")),
        0xA5 => (2, format!("LDA ${b1:02X}")),
        0xA6 => (2, format!("LDX ${b1:02X}")),
        0xA8 => (1, "TAY".into()),
        0xA9 => (2, format!("LDA #${b1:02X}")),
        0xAA => (1, "TAX".into()),
        0xAC => (3, format!("LDY ${abs:04X}")),
        0xAD => (3, format!("LDA ${abs:04X}")),
        0xAE => (3, format!("LDX ${abs:04X}")),
        0xB0 => (2, format!("BCS ${rel:04X}")),
        0xB1 => (2, format!("LDA (${b1:02X}),Y")),
        0xB2 if is_65c02 => (2, format!("LDA (${b1:02X})")),
        0xB4 => (2, format!("LDY ${b1:02X},X")),
        0xB5 => (2, format!("LDA ${b1:02X},X")),
        0xB6 => (2, format!("LDX ${b1:02X},Y")),
        0xB8 => (1, "CLV".into()),
        0xB9 => (3, format!("LDA ${abs:04X},Y")),
        0xBA => (1, "TSX".into()),
        0xBC => (3, format!("LDY ${abs:04X},X")),
        0xBD => (3, format!("LDA ${abs:04X},X")),
        0xBE => (3, format!("LDX ${abs:04X},Y")),
        0xC0 => (2, format!("CPY #${b1:02X}")),
        0xC1 => (2, format!("CMP (${b1:02X},X)")),
        0xC4 => (2, format!("CPY ${b1:02X}")),
        0xC5 => (2, format!("CMP ${b1:02X}")),
        0xC6 => (2, format!("DEC ${b1:02X}")),
        0xC8 => (1, "INY".into()),
        0xC9 => (2, format!("CMP #${b1:02X}")),
        0xCA => (1, "DEX".into()),
        0xCB if is_65c02 => (1, "WAI".into()),
        0xCC => (3, format!("CPY ${abs:04X}")),
        0xCD => (3, format!("CMP ${abs:04X}")),
        0xCE => (3, format!("DEC ${abs:04X}")),
        0xD0 => (2, format!("BNE ${rel:04X}")),
        0xD1 => (2, format!("CMP (${b1:02X}),Y")),
        0xD2 if is_65c02 => (2, format!("CMP (${b1:02X})")),
        0xD5 => (2, format!("CMP ${b1:02X},X")),
        0xD6 => (2, format!("DEC ${b1:02X},X")),
        0xD8 => (1, "CLD".into()),
        0xD9 => (3, format!("CMP ${abs:04X},Y")),
        0xDA if is_65c02 => (1, "PHX".into()),
        0xDB if is_65c02 => (1, "STP".into()),
        0xDD => (3, format!("CMP ${abs:04X},X")),
        0xDE => (3, format!("DEC ${abs:04X},X")),
        0xE0 => (2, format!("CPX #${b1:02X}")),
        0xE1 => (2, format!("SBC (${b1:02X},X)")),
        0xE4 => (2, format!("CPX ${b1:02X}")),
        0xE5 => (2, format!("SBC ${b1:02X}")),
        0xE6 => (2, format!("INC ${b1:02X}")),
        0xE8 => (1, "INX".into()),
        0xE9 => (2, format!("SBC #${b1:02X}")),
        0xEA => (1, "NOP".into()),
        0xEC => (3, format!("CPX ${abs:04X}")),
        0xED => (3, format!("SBC ${abs:04X}")),
        0xEE => (3, format!("INC ${abs:04X}")),
        0xF0 => (2, format!("BEQ ${rel:04X}")),
        0xF1 => (2, format!("SBC (${b1:02X}),Y")),
        0xF2 if is_65c02 => (2, format!("SBC (${b1:02X})")),
        0xF5 => (2, format!("SBC ${b1:02X},X")),
        0xF6 => (2, format!("INC ${b1:02X},X")),
        0xF8 => (1, "SED".into()),
        0xF9 => (3, format!("SBC ${abs:04X},Y")),
        0xFA if is_65c02 => (1, "PLX".into()),
        0xFD => (3, format!("SBC ${abs:04X},X")),
        0xFE => (3, format!("INC ${abs:04X},X")),
        _ if is_65c02 && (op & 0x0F) == 0x07 => {
            let bit = (op >> 4) & 7;
            let opname = if (op & 0x80) == 0 { "RMB" } else { "SMB" };
            (2, format!("{opname}{bit} ${b1:02X}"))
        }
        _ if is_65c02 && (op & 0x0F) == 0x0F => {
            let bit = (op >> 4) & 7;
            let opname = if (op & 0x80) == 0 { "BBR" } else { "BBS" };
            let t = addr.wrapping_add(3).wrapping_add(b2 as i8 as i16 as u16);
            (3, format!("{opname}{bit} ${b1:02X},${t:04X}"))
        }
        _ => (1, format!("DB ${op:02X}")),
    }
}
