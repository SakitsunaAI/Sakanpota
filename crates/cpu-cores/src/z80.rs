use emu_core::{Bus8, CpuDebugState, DisassembledInstruction, IoPortBus};

pub const Z80_FLAG_C: u8 = 0x01;
pub const Z80_FLAG_N: u8 = 0x02;
pub const Z80_FLAG_PV: u8 = 0x04;
pub const Z80_FLAG_F3: u8 = 0x08;
pub const Z80_FLAG_H: u8 = 0x10;
pub const Z80_FLAG_F5: u8 = 0x20;
pub const Z80_FLAG_Z: u8 = 0x40;
pub const Z80_FLAG_S: u8 = 0x80;

#[inline]
fn parity_even(v: u8) -> bool {
    v.count_ones().is_multiple_of(2)
}

#[derive(Debug, Clone)]
pub struct Z80 {
    pub a: u8,
    pub f: u8,
    pub b: u8,
    pub c: u8,
    pub d: u8,
    pub e: u8,
    pub h: u8,
    pub l: u8,

    pub a_alt: u8,
    pub f_alt: u8,
    pub b_alt: u8,
    pub c_alt: u8,
    pub d_alt: u8,
    pub e_alt: u8,
    pub h_alt: u8,
    pub l_alt: u8,

    pub ix: u16,
    pub iy: u16,
    pub sp: u16,
    pub pc: u16,
    pub i: u8,
    pub r: u8,

    pub iff1: bool,
    pub iff2: bool,
    pub im: u8,
    pub halted: bool,
    pub irq_pending: bool,
    pub irq_vector: u8,
    pub nmi_pending: bool,
    pub cycles: u64,
    ei_delay: bool,
}

impl Default for Z80 {
    fn default() -> Self {
        Self::new()
    }
}

impl Z80 {
    pub fn new() -> Self {
        let mut cpu = Self {
            a: 0xFF,
            f: 0xFF,
            b: 0,
            c: 0,
            d: 0,
            e: 0,
            h: 0,
            l: 0,
            a_alt: 0xFF,
            f_alt: 0xFF,
            b_alt: 0,
            c_alt: 0,
            d_alt: 0,
            e_alt: 0,
            h_alt: 0,
            l_alt: 0,
            ix: 0xFFFF,
            iy: 0xFFFF,
            sp: 0xFFFF,
            pc: 0,
            i: 0,
            r: 0,
            iff1: false,
            iff2: false,
            im: 0,
            halted: false,
            irq_pending: false,
            irq_vector: 0xFF,
            nmi_pending: false,
            cycles: 0,
            ei_delay: false,
        };
        cpu.reset();
        cpu
    }

    pub fn reset(&mut self) {
        self.a = 0xFF;
        self.f = 0xFF;
        self.b = 0;
        self.c = 0;
        self.d = 0;
        self.e = 0;
        self.h = 0;
        self.l = 0;
        self.a_alt = 0xFF;
        self.f_alt = 0xFF;
        self.b_alt = 0;
        self.c_alt = 0;
        self.d_alt = 0;
        self.e_alt = 0;
        self.h_alt = 0;
        self.l_alt = 0;
        self.ix = 0xFFFF;
        self.iy = 0xFFFF;
        self.sp = 0xFFFF;
        self.pc = 0x0000;
        self.i = 0;
        self.r = 0;
        self.iff1 = false;
        self.iff2 = false;
        self.im = 0;
        self.halted = false;
        self.irq_pending = false;
        self.nmi_pending = false;
        self.cycles = 0;
        self.ei_delay = false;
    }

    #[inline]
    pub fn request_irq(&mut self, vector: u8) {
        self.irq_pending = true;
        self.irq_vector = vector;
    }

    #[inline]
    pub fn request_nmi(&mut self) {
        self.nmi_pending = true;
    }

    #[inline]
    pub fn bc(&self) -> u16 {
        ((self.b as u16) << 8) | (self.c as u16)
    }
    #[inline]
    pub fn set_bc(&mut self, v: u16) {
        self.b = (v >> 8) as u8;
        self.c = (v & 0xFF) as u8;
    }
    #[inline]
    pub fn de(&self) -> u16 {
        ((self.d as u16) << 8) | (self.e as u16)
    }
    #[inline]
    pub fn set_de(&mut self, v: u16) {
        self.d = (v >> 8) as u8;
        self.e = (v & 0xFF) as u8;
    }
    #[inline]
    pub fn hl(&self) -> u16 {
        ((self.h as u16) << 8) | (self.l as u16)
    }
    #[inline]
    pub fn set_hl(&mut self, v: u16) {
        self.h = (v >> 8) as u8;
        self.l = (v & 0xFF) as u8;
    }
    #[inline]
    pub fn af(&self) -> u16 {
        ((self.a as u16) << 8) | (self.f as u16)
    }
    #[inline]
    pub fn set_af(&mut self, v: u16) {
        self.a = (v >> 8) as u8;
        self.f = (v & 0xFF) as u8;
    }

    #[inline]
    fn inc_r(&mut self) {
        self.r = (self.r & 0x80) | (self.r.wrapping_add(1) & 0x7F);
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
    fn push16<B: Bus8>(&mut self, bus: &mut B, val: u16) {
        self.sp = self.sp.wrapping_sub(1);
        bus.write8(self.sp, (val >> 8) as u8);
        self.sp = self.sp.wrapping_sub(1);
        bus.write8(self.sp, (val & 0xFF) as u8);
    }

    #[inline]
    fn pop16<B: Bus8>(&mut self, bus: &mut B) -> u16 {
        let lo = bus.read8(self.sp) as u16;
        self.sp = self.sp.wrapping_add(1);
        let hi = bus.read8(self.sp) as u16;
        self.sp = self.sp.wrapping_add(1);
        lo | (hi << 8)
    }

    #[inline]
    fn sz_xy(&self, val: u8) -> u8 {
        let mut f = val & (Z80_FLAG_S | Z80_FLAG_F5 | Z80_FLAG_F3);
        if val == 0 {
            f |= Z80_FLAG_Z;
        }
        f
    }

    fn read_reg8<B: Bus8>(&self, idx: u8, bus: &mut B, hl_override: Option<u16>) -> u8 {
        match idx & 7 {
            0 => self.b,
            1 => self.c,
            2 => self.d,
            3 => self.e,
            4 => match hl_override {
                Some(v) => (v >> 8) as u8,
                None => self.h,
            },
            5 => match hl_override {
                Some(v) => (v & 0xFF) as u8,
                None => self.l,
            },
            6 => bus.read8(hl_override.unwrap_or_else(|| self.hl())),
            _ => self.a,
        }
    }

    fn write_reg8<B: Bus8>(&mut self, idx: u8, val: u8, bus: &mut B, idx_reg: Option<u16>, mem_addr: Option<u16>) {
        match idx & 7 {
            0 => self.b = val,
            1 => self.c = val,
            2 => self.d = val,
            3 => self.e = val,
            4 => match idx_reg {
                Some(r) if r == self.ix => self.ix = (self.ix & 0x00FF) | ((val as u16) << 8),
                Some(_) => self.iy = (self.iy & 0x00FF) | ((val as u16) << 8),
                None => self.h = val,
            },
            5 => match idx_reg {
                Some(r) if r == self.ix => self.ix = (self.ix & 0xFF00) | (val as u16),
                Some(_) => self.iy = (self.iy & 0xFF00) | (val as u16),
                None => self.l = val,
            },
            6 => {
                let addr = mem_addr.unwrap_or_else(|| self.hl());
                bus.write8(addr, val);
            }
            _ => self.a = val,
        }
    }

    fn get_rp(&self, rp: u8, use_ix: bool, use_iy: bool) -> u16 {
        match rp & 3 {
            0 => self.bc(),
            1 => self.de(),
            2 => {
                if use_ix {
                    self.ix
                } else if use_iy {
                    self.iy
                } else {
                    self.hl()
                }
            }
            _ => self.sp,
        }
    }

    fn set_rp(&mut self, rp: u8, val: u16, use_ix: bool, use_iy: bool) {
        match rp & 3 {
            0 => self.set_bc(val),
            1 => self.set_de(val),
            2 => {
                if use_ix {
                    self.ix = val;
                } else if use_iy {
                    self.iy = val;
                } else {
                    self.set_hl(val);
                }
            }
            _ => self.sp = val,
        }
    }

    fn get_rp2(&self, rp: u8, use_ix: bool, use_iy: bool) -> u16 {
        match rp & 3 {
            0 => self.bc(),
            1 => self.de(),
            2 => {
                if use_ix {
                    self.ix
                } else if use_iy {
                    self.iy
                } else {
                    self.hl()
                }
            }
            _ => self.af(),
        }
    }

    fn set_rp2(&mut self, rp: u8, val: u16, use_ix: bool, use_iy: bool) {
        match rp & 3 {
            0 => self.set_bc(val),
            1 => self.set_de(val),
            2 => {
                if use_ix {
                    self.ix = val;
                } else if use_iy {
                    self.iy = val;
                } else {
                    self.set_hl(val);
                }
            }
            _ => self.set_af(val),
        }
    }

    #[inline]
    fn check_cond(&self, cc: u8) -> bool {
        match cc & 7 {
            0 => (self.f & Z80_FLAG_Z) == 0,
            1 => (self.f & Z80_FLAG_Z) != 0,
            2 => (self.f & Z80_FLAG_C) == 0,
            3 => (self.f & Z80_FLAG_C) != 0,
            4 => (self.f & Z80_FLAG_PV) == 0,
            5 => (self.f & Z80_FLAG_PV) != 0,
            6 => (self.f & Z80_FLAG_S) == 0,
            _ => (self.f & Z80_FLAG_S) != 0,
        }
    }

    fn add8(&mut self, val: u8, carry: bool) {
        let c = if carry && (self.f & Z80_FLAG_C) != 0 { 1u16 } else { 0u16 };
        let res16 = self.a as u16 + val as u16 + c;
        let res = (res16 & 0xFF) as u8;
        let mut f = self.sz_xy(res);
        if res16 > 0xFF {
            f |= Z80_FLAG_C;
        }
        if ((self.a ^ val ^ res) & 0x10) != 0 {
            f |= Z80_FLAG_H;
        }
        if ((!(self.a ^ val) & (self.a ^ res)) & 0x80) != 0 {
            f |= Z80_FLAG_PV;
        }
        self.a = res;
        self.f = f;
    }

    fn sub8(&mut self, val: u8, borrow: bool) -> u8 {
        let c = if borrow && (self.f & Z80_FLAG_C) != 0 { 1i16 } else { 0i16 };
        let res16 = self.a as i16 - val as i16 - c;
        let res = (res16 & 0xFF) as u8;
        let mut f = self.sz_xy(res) | Z80_FLAG_N;
        if res16 < 0 {
            f |= Z80_FLAG_C;
        }
        if ((self.a ^ val ^ res) & 0x10) != 0 {
            f |= Z80_FLAG_H;
        }
        if (((self.a ^ val) & (self.a ^ res)) & 0x80) != 0 {
            f |= Z80_FLAG_PV;
        }
        self.f = f;
        res
    }

    fn cp8(&mut self, val: u8) {
        let res16 = self.a as i16 - val as i16;
        let res = (res16 & 0xFF) as u8;
        let mut f = (res & Z80_FLAG_S) | (val & (Z80_FLAG_F5 | Z80_FLAG_F3)) | Z80_FLAG_N;
        if res == 0 {
            f |= Z80_FLAG_Z;
        }
        if res16 < 0 {
            f |= Z80_FLAG_C;
        }
        if ((self.a ^ val ^ res) & 0x10) != 0 {
            f |= Z80_FLAG_H;
        }
        if (((self.a ^ val) & (self.a ^ res)) & 0x80) != 0 {
            f |= Z80_FLAG_PV;
        }
        self.f = f;
    }

    fn and8(&mut self, val: u8) {
        self.a &= val;
        let mut f = self.sz_xy(self.a) | Z80_FLAG_H;
        if parity_even(self.a) {
            f |= Z80_FLAG_PV;
        }
        self.f = f;
    }

    fn or8(&mut self, val: u8) {
        self.a |= val;
        let mut f = self.sz_xy(self.a);
        if parity_even(self.a) {
            f |= Z80_FLAG_PV;
        }
        self.f = f;
    }

    fn xor8(&mut self, val: u8) {
        self.a ^= val;
        let mut f = self.sz_xy(self.a);
        if parity_even(self.a) {
            f |= Z80_FLAG_PV;
        }
        self.f = f;
    }

    fn inc8(&mut self, val: u8) -> u8 {
        let res = val.wrapping_add(1);
        let mut f = (self.f & Z80_FLAG_C) | self.sz_xy(res);
        if (val & 0x0F) == 0x0F {
            f |= Z80_FLAG_H;
        }
        if val == 0x7F {
            f |= Z80_FLAG_PV;
        }
        self.f = f;
        res
    }

    fn dec8(&mut self, val: u8) -> u8 {
        let res = val.wrapping_sub(1);
        let mut f = (self.f & Z80_FLAG_C) | self.sz_xy(res) | Z80_FLAG_N;
        if (val & 0x0F) == 0x00 {
            f |= Z80_FLAG_H;
        }
        if val == 0x80 {
            f |= Z80_FLAG_PV;
        }
        self.f = f;
        res
    }

    fn daa(&mut self) {
        let mut a = self.a;
        let mut corr = 0u8;
        let mut c = (self.f & Z80_FLAG_C) != 0;
        let n = (self.f & Z80_FLAG_N) != 0;
        let h = (self.f & Z80_FLAG_H) != 0;

        if h || (!n && (a & 0x0F) > 9) {
            corr |= 0x06;
        }
        if c || (!n && a > 0x99) {
            corr |= 0x60;
            c = true;
        }
        let prev_a = a;
        if n {
            a = a.wrapping_sub(corr);
        } else {
            a = a.wrapping_add(corr);
        }
        let mut f = self.sz_xy(a) | (self.f & Z80_FLAG_N);
        if c {
            f |= Z80_FLAG_C;
        }
        if ((prev_a ^ a) & 0x10) != 0 {
            f |= Z80_FLAG_H;
        }
        if parity_even(a) {
            f |= Z80_FLAG_PV;
        }
        self.a = a;
        self.f = f;
    }

    fn add16(&mut self, a: u16, b: u16) -> u16 {
        let sum = a as u32 + b as u32;
        let res = sum as u16;
        let mut f = (self.f & (Z80_FLAG_S | Z80_FLAG_Z | Z80_FLAG_PV))
            | (((res >> 8) as u8) & (Z80_FLAG_F5 | Z80_FLAG_F3));
        if sum > 0xFFFF {
            f |= Z80_FLAG_C;
        }
        if ((a ^ b ^ res) & 0x1000) != 0 {
            f |= Z80_FLAG_H;
        }
        self.f = f;
        res
    }

    fn adc16(&mut self, a: u16, b: u16) -> u16 {
        let c = if (self.f & Z80_FLAG_C) != 0 { 1u32 } else { 0u32 };
        let sum = a as u32 + b as u32 + c;
        let res = sum as u16;
        let mut f = ((res >> 8) as u8) & (Z80_FLAG_S | Z80_FLAG_F5 | Z80_FLAG_F3);
        if res == 0 {
            f |= Z80_FLAG_Z;
        }
        if sum > 0xFFFF {
            f |= Z80_FLAG_C;
        }
        if ((a ^ b ^ res) & 0x1000) != 0 {
            f |= Z80_FLAG_H;
        }
        if ((!(a ^ b) & (a ^ res)) & 0x8000) != 0 {
            f |= Z80_FLAG_PV;
        }
        self.f = f;
        res
    }

    fn sbc16(&mut self, a: u16, b: u16) -> u16 {
        let c = if (self.f & Z80_FLAG_C) != 0 { 1i32 } else { 0i32 };
        let diff = a as i32 - b as i32 - c;
        let res = diff as u16;
        let mut f = (((res >> 8) as u8) & (Z80_FLAG_S | Z80_FLAG_F5 | Z80_FLAG_F3)) | Z80_FLAG_N;
        if res == 0 {
            f |= Z80_FLAG_Z;
        }
        if diff < 0 {
            f |= Z80_FLAG_C;
        }
        if ((a ^ b ^ res) & 0x1000) != 0 {
            f |= Z80_FLAG_H;
        }
        if (((a ^ b) & (a ^ res)) & 0x8000) != 0 {
            f |= Z80_FLAG_PV;
        }
        self.f = f;
        res
    }

    fn cb_shift(&mut self, group: u8, val: u8) -> u8 {
        let (res, carry) = match group {
            0 => {
                // RLC
                let c = (val & 0x80) != 0;
                ((val << 1) | if c { 1 } else { 0 }, c)
            }
            1 => {
                // RRC
                let c = (val & 0x01) != 0;
                ((val >> 1) | if c { 0x80 } else { 0 }, c)
            }
            2 => {
                // RL
                let c = (val & 0x80) != 0;
                let old_c = if (self.f & Z80_FLAG_C) != 0 { 1 } else { 0 };
                ((val << 1) | old_c, c)
            }
            3 => {
                // RR
                let c = (val & 0x01) != 0;
                let old_c = if (self.f & Z80_FLAG_C) != 0 { 0x80 } else { 0 };
                ((val >> 1) | old_c, c)
            }
            4 => {
                // SLA
                ((val << 1), (val & 0x80) != 0)
            }
            5 => {
                // SRA
                (((val as i8) >> 1) as u8, (val & 0x01) != 0)
            }
            6 => {
                // SLL (undocumented)
                ((val << 1) | 1, (val & 0x80) != 0)
            }
            _ => {
                // SRL
                ((val >> 1), (val & 0x01) != 0)
            }
        };
        let mut f = self.sz_xy(res);
        if carry {
            f |= Z80_FLAG_C;
        }
        if parity_even(res) {
            f |= Z80_FLAG_PV;
        }
        self.f = f;
        res
    }

    pub fn step<B: Bus8 + IoPortBus>(&mut self, bus: &mut B) -> u32 {
        if self.nmi_pending {
            self.nmi_pending = false;
            self.halted = false;
            self.iff2 = self.iff1;
            self.iff1 = false;
            self.inc_r();
            let pc = self.pc;
            self.push16(bus, pc);
            self.pc = 0x0066;
            self.cycles += 11;
            return 11;
        }

        let allow_irq = !self.ei_delay;
        self.ei_delay = false;

        if allow_irq && self.irq_pending && self.iff1 {
            self.irq_pending = false;
            self.halted = false;
            self.iff1 = false;
            self.iff2 = false;
            self.inc_r();
            let pc = self.pc;
            self.push16(bus, pc);
            let cyc = match self.im {
                2 => {
                    let vec_addr = ((self.i as u16) << 8) | (self.irq_vector as u16);
                    let lo = bus.read8(vec_addr) as u16;
                    let hi = bus.read8(vec_addr.wrapping_add(1)) as u16;
                    self.pc = lo | (hi << 8);
                    19
                }
                0 => {
                    // Most IM 0 systems pull 0xFF (RST 38) on the bus
                    self.pc = (self.irq_vector & 0x38) as u16;
                    13
                }
                _ => {
                    self.pc = 0x0038;
                    13
                }
            };
            self.cycles += cyc;
            return cyc as u32;
        }

        if self.halted {
            self.inc_r();
            self.cycles += 4;
            return 4;
        }

        let mut use_ix = false;
        let mut use_iy = false;
        let mut op = self.fetch8(bus);
        self.inc_r();

        while op == 0xDD || op == 0xFD {
            if op == 0xDD {
                use_ix = true;
                use_iy = false;
            } else {
                use_iy = true;
                use_ix = false;
            }
            op = self.fetch8(bus);
            self.inc_r();
        }

        let cyc = if op == 0xCB {
            self.exec_cb(bus, use_ix, use_iy)
        } else if op == 0xED {
            self.exec_ed(bus)
        } else {
            self.exec_main(bus, op, use_ix, use_iy)
        };

        self.cycles += cyc as u64;
        cyc
    }

    fn exec_cb<B: Bus8>(&mut self, bus: &mut B, use_ix: bool, use_iy: bool) -> u32 {
        let disp = if use_ix || use_iy {
            Some(self.fetch8(bus) as i8 as i32)
        } else {
            self.inc_r();
            None
        };
        let op = self.fetch8(bus);
        let group = op >> 6;
        let bit_or_rot = (op >> 3) & 7;
        let reg = op & 7;

        let addr = if let Some(d) = disp {
            let base = if use_ix { self.ix } else { self.iy };
            (base as i32 + d) as u16
        } else {
            self.hl()
        };

        let val = if disp.is_some() || reg == 6 {
            bus.read8(addr)
        } else {
            self.read_reg8(reg, bus, None)
        };

        match group {
            0 => {
                let res = self.cb_shift(bit_or_rot, val);
                if disp.is_some() {
                    bus.write8(addr, res);
                    if reg != 6 {
                        self.write_reg8(reg, res, bus, None, None);
                    }
                    23
                } else {
                    self.write_reg8(reg, res, bus, None, Some(addr));
                    if reg == 6 { 15 } else { 8 }
                }
            }
            1 => {
                // BIT b, r
                let mut f = (self.f & Z80_FLAG_C) | Z80_FLAG_H;
                if (val & (1 << bit_or_rot)) == 0 {
                    f |= Z80_FLAG_Z | Z80_FLAG_PV;
                }
                if bit_or_rot == 7 && (val & 0x80) != 0 {
                    f |= Z80_FLAG_S;
                }
                if disp.is_some() {
                    f |= ((addr >> 8) as u8) & (Z80_FLAG_F5 | Z80_FLAG_F3);
                } else {
                    f |= val & (Z80_FLAG_F5 | Z80_FLAG_F3);
                }
                self.f = f;
                if disp.is_some() {
                    20
                } else if reg == 6 {
                    12
                } else {
                    8
                }
            }
            2 => {
                // RES b, r
                let res = val & !(1 << bit_or_rot);
                if disp.is_some() {
                    bus.write8(addr, res);
                    if reg != 6 {
                        self.write_reg8(reg, res, bus, None, None);
                    }
                    23
                } else {
                    self.write_reg8(reg, res, bus, None, Some(addr));
                    if reg == 6 { 15 } else { 8 }
                }
            }
            _ => {
                // SET b, r
                let res = val | (1 << bit_or_rot);
                if disp.is_some() {
                    bus.write8(addr, res);
                    if reg != 6 {
                        self.write_reg8(reg, res, bus, None, None);
                    }
                    23
                } else {
                    self.write_reg8(reg, res, bus, None, Some(addr));
                    if reg == 6 { 15 } else { 8 }
                }
            }
        }
    }

    fn exec_ed<B: Bus8 + IoPortBus>(&mut self, bus: &mut B) -> u32 {
        let op = self.fetch8(bus);
        self.inc_r();
        match op {
            0x40 | 0x48 | 0x50 | 0x58 | 0x60 | 0x68 | 0x70 | 0x78 => {
                // IN r,(C) (0x70 affects flags only)
                let port = self.bc();
                let val = bus.port_in8(port);
                let mut f = (self.f & Z80_FLAG_C) | self.sz_xy(val);
                if parity_even(val) {
                    f |= Z80_FLAG_PV;
                }
                self.f = f;
                let reg = (op >> 3) & 7;
                if reg != 6 {
                    self.write_reg8(reg, val, bus, None, None);
                }
                12
            }
            0x41 | 0x49 | 0x51 | 0x59 | 0x61 | 0x69 | 0x71 | 0x79 => {
                // OUT (C),r (0x71 outputs 0)
                let port = self.bc();
                let reg = (op >> 3) & 7;
                let val = if reg == 6 { 0 } else { self.read_reg8(reg, bus, None) };
                bus.port_out8(port, val);
                12
            }
            0x42 | 0x52 | 0x62 | 0x72 => {
                let rp = (op >> 4) & 3;
                let rhs = self.get_rp(rp, false, false);
                let hl = self.hl();
                let res = self.sbc16(hl, rhs);
                self.set_hl(res);
                15
            }
            0x4A | 0x5A | 0x6A | 0x7A => {
                let rp = (op >> 4) & 3;
                let rhs = self.get_rp(rp, false, false);
                let hl = self.hl();
                let res = self.adc16(hl, rhs);
                self.set_hl(res);
                15
            }
            0x43 | 0x53 | 0x63 | 0x73 => {
                let addr = self.fetch16(bus);
                let rp = (op >> 4) & 3;
                let v = self.get_rp(rp, false, false);
                bus.write8(addr, (v & 0xFF) as u8);
                bus.write8(addr.wrapping_add(1), (v >> 8) as u8);
                20
            }
            0x4B | 0x5B | 0x6B | 0x7B => {
                let addr = self.fetch16(bus);
                let rp = (op >> 4) & 3;
                let lo = bus.read8(addr) as u16;
                let hi = bus.read8(addr.wrapping_add(1)) as u16;
                self.set_rp(rp, lo | (hi << 8), false, false);
                20
            }
            0x44 | 0x4C | 0x54 | 0x5C | 0x64 | 0x6C | 0x74 | 0x7C => {
                // NEG
                let old = self.a;
                self.a = 0;
                self.a = self.sub8(old, false);
                8
            }
            0x45 | 0x55 | 0x65 | 0x75 | 0x4D | 0x5D | 0x6D | 0x7D => {
                // RETN / RETI
                self.iff1 = self.iff2;
                self.pc = self.pop16(bus);
                14
            }
            0x46 | 0x4E | 0x66 | 0x6E => {
                self.im = 0;
                8
            }
            0x56 | 0x76 => {
                self.im = 1;
                8
            }
            0x5E | 0x7E => {
                self.im = 2;
                8
            }
            0x47 => {
                self.i = self.a;
                9
            }
            0x4F => {
                self.r = self.a;
                9
            }
            0x57 => {
                self.a = self.i;
                let mut f = (self.f & Z80_FLAG_C) | self.sz_xy(self.a);
                if self.iff2 {
                    f |= Z80_FLAG_PV;
                }
                self.f = f;
                9
            }
            0x5F => {
                self.a = self.r;
                let mut f = (self.f & Z80_FLAG_C) | self.sz_xy(self.a);
                if self.iff2 {
                    f |= Z80_FLAG_PV;
                }
                self.f = f;
                9
            }
            0x67 => {
                // RRD
                let hl = self.hl();
                let mem = bus.read8(hl);
                let low_a = self.a & 0x0F;
                self.a = (self.a & 0xF0) | (mem & 0x0F);
                let new_mem = (low_a << 4) | (mem >> 4);
                bus.write8(hl, new_mem);
                let mut f = (self.f & Z80_FLAG_C) | self.sz_xy(self.a);
                if parity_even(self.a) {
                    f |= Z80_FLAG_PV;
                }
                self.f = f;
                18
            }
            0x6F => {
                // RLD
                let hl = self.hl();
                let mem = bus.read8(hl);
                let low_a = self.a & 0x0F;
                self.a = (self.a & 0xF0) | (mem >> 4);
                let new_mem = (mem << 4) | low_a;
                bus.write8(hl, new_mem);
                let mut f = (self.f & Z80_FLAG_C) | self.sz_xy(self.a);
                if parity_even(self.a) {
                    f |= Z80_FLAG_PV;
                }
                self.f = f;
                18
            }
            0xA0 | 0xA8 | 0xB0 | 0xB8 => {
                // LDI, LDD, LDIR, LDDR
                let hl = self.hl();
                let de = self.de();
                let v = bus.read8(hl);
                bus.write8(de, v);
                if (op & 0x08) == 0 {
                    self.set_hl(hl.wrapping_add(1));
                    self.set_de(de.wrapping_add(1));
                } else {
                    self.set_hl(hl.wrapping_sub(1));
                    self.set_de(de.wrapping_sub(1));
                }
                let bc = self.bc().wrapping_sub(1);
                self.set_bc(bc);
                let n = self.a.wrapping_add(v);
                let mut f = (self.f & (Z80_FLAG_S | Z80_FLAG_Z | Z80_FLAG_C))
                    | (n & Z80_FLAG_F3)
                    | if (n & 0x02) != 0 { Z80_FLAG_F5 } else { 0 };
                if bc != 0 {
                    f |= Z80_FLAG_PV;
                }
                self.f = f;
                if (op & 0x10) != 0 && bc != 0 {
                    self.pc = self.pc.wrapping_sub(2);
                    21
                } else {
                    16
                }
            }
            0xA1 | 0xA9 | 0xB1 | 0xB9 => {
                // CPI, CPD, CPIR, CPDR
                let hl = self.hl();
                let v = bus.read8(hl);
                if (op & 0x08) == 0 {
                    self.set_hl(hl.wrapping_add(1));
                } else {
                    self.set_hl(hl.wrapping_sub(1));
                }
                let bc = self.bc().wrapping_sub(1);
                self.set_bc(bc);
                let res = self.a.wrapping_sub(v);
                let half = ((self.a ^ v ^ res) & 0x10) != 0;
                let n = res.wrapping_sub(if half { 1 } else { 0 });
                let mut f = (self.f & Z80_FLAG_C)
                    | Z80_FLAG_N
                    | (res & Z80_FLAG_S)
                    | if res == 0 { Z80_FLAG_Z } else { 0 }
                    | if half { Z80_FLAG_H } else { 0 }
                    | (n & Z80_FLAG_F3)
                    | if (n & 0x02) != 0 { Z80_FLAG_F5 } else { 0 };
                if bc != 0 {
                    f |= Z80_FLAG_PV;
                }
                self.f = f;
                if (op & 0x10) != 0 && bc != 0 && res != 0 {
                    self.pc = self.pc.wrapping_sub(2);
                    21
                } else {
                    16
                }
            }
            0xA2 | 0xAA | 0xB2 | 0xBA => {
                // INI, IND, INIR, INDR
                let port = self.bc();
                let v = bus.port_in8(port);
                let hl = self.hl();
                bus.write8(hl, v);
                if (op & 0x08) == 0 {
                    self.set_hl(hl.wrapping_add(1));
                } else {
                    self.set_hl(hl.wrapping_sub(1));
                }
                self.b = self.b.wrapping_sub(1);
                self.f = self.sz_xy(self.b) | Z80_FLAG_N;
                if (op & 0x10) != 0 && self.b != 0 {
                    self.pc = self.pc.wrapping_sub(2);
                    21
                } else {
                    16
                }
            }
            0xA3 | 0xAB | 0xB3 | 0xBB => {
                // OUTI, OUTD, OTIR, OTDR
                let hl = self.hl();
                let v = bus.read8(hl);
                self.b = self.b.wrapping_sub(1);
                let port = self.bc();
                bus.port_out8(port, v);
                if (op & 0x08) == 0 {
                    self.set_hl(hl.wrapping_add(1));
                } else {
                    self.set_hl(hl.wrapping_sub(1));
                }
                self.f = self.sz_xy(self.b) | Z80_FLAG_N;
                if (op & 0x10) != 0 && self.b != 0 {
                    self.pc = self.pc.wrapping_sub(2);
                    21
                } else {
                    16
                }
            }
            _ => 8,
        }
    }

    fn exec_main<B: Bus8 + IoPortBus>(
        &mut self,
        bus: &mut B,
        op: u8,
        use_ix: bool,
        use_iy: bool,
    ) -> u32 {
        let idx_reg = if use_ix {
            Some(self.ix)
        } else if use_iy {
            Some(self.iy)
        } else {
            None
        };

        let x = op >> 6;
        let y = (op >> 3) & 7;
        let z = op & 7;
        let p = y >> 1;
        let q = y & 1;

        match x {
            0 => match z {
                0 => match y {
                    0 => 4, // NOP
                    1 => {
                        std::mem::swap(&mut self.a, &mut self.a_alt);
                        std::mem::swap(&mut self.f, &mut self.f_alt);
                        4
                    }
                    2 => {
                        let d = self.fetch8(bus) as i8 as i32;
                        self.b = self.b.wrapping_sub(1);
                        if self.b != 0 {
                            self.pc = (self.pc as i32 + d) as u16;
                            13
                        } else {
                            8
                        }
                    }
                    3 => {
                        let d = self.fetch8(bus) as i8 as i32;
                        self.pc = (self.pc as i32 + d) as u16;
                        12
                    }
                    _ => {
                        let d = self.fetch8(bus) as i8 as i32;
                        if self.check_cond(y - 4) {
                            self.pc = (self.pc as i32 + d) as u16;
                            12
                        } else {
                            7
                        }
                    }
                },
                1 => {
                    if q == 0 {
                        let nn = self.fetch16(bus);
                        self.set_rp(p, nn, use_ix, use_iy);
                        if idx_reg.is_some() { 14 } else { 10 }
                    } else {
                        let lhs = idx_reg.unwrap_or_else(|| self.hl());
                        let rhs = self.get_rp(p, use_ix, use_iy);
                        let res = self.add16(lhs, rhs);
                        if use_ix {
                            self.ix = res;
                        } else if use_iy {
                            self.iy = res;
                        } else {
                            self.set_hl(res);
                        }
                        if idx_reg.is_some() { 15 } else { 11 }
                    }
                }
                2 => match (q, p) {
                    (0, 0) => {
                        bus.write8(self.bc(), self.a);
                        7
                    }
                    (0, 1) => {
                        bus.write8(self.de(), self.a);
                        7
                    }
                    (0, 2) => {
                        let nn = self.fetch16(bus);
                        let v = idx_reg.unwrap_or_else(|| self.hl());
                        bus.write8(nn, (v & 0xFF) as u8);
                        bus.write8(nn.wrapping_add(1), (v >> 8) as u8);
                        if idx_reg.is_some() { 20 } else { 16 }
                    }
                    (0, 3) => {
                        let nn = self.fetch16(bus);
                        bus.write8(nn, self.a);
                        13
                    }
                    (1, 0) => {
                        self.a = bus.read8(self.bc());
                        7
                    }
                    (1, 1) => {
                        self.a = bus.read8(self.de());
                        7
                    }
                    (1, 2) => {
                        let nn = self.fetch16(bus);
                        let lo = bus.read8(nn) as u16;
                        let hi = bus.read8(nn.wrapping_add(1)) as u16;
                        let v = lo | (hi << 8);
                        if use_ix {
                            self.ix = v;
                        } else if use_iy {
                            self.iy = v;
                        } else {
                            self.set_hl(v);
                        }
                        if idx_reg.is_some() { 20 } else { 16 }
                    }
                    _ => {
                        let nn = self.fetch16(bus);
                        self.a = bus.read8(nn);
                        13
                    }
                },
                3 => {
                    let v = self.get_rp(p, use_ix, use_iy);
                    let res = if q == 0 { v.wrapping_add(1) } else { v.wrapping_sub(1) };
                    self.set_rp(p, res, use_ix, use_iy);
                    if idx_reg.is_some() { 10 } else { 6 }
                }
                4 | 5 => {
                    let is_inc = z == 4;
                    if y == 6 {
                        let addr = if let Some(base) = idx_reg {
                            let d = self.fetch8(bus) as i8 as i32;
                            (base as i32 + d) as u16
                        } else {
                            self.hl()
                        };
                        let v = bus.read8(addr);
                        let res = if is_inc { self.inc8(v) } else { self.dec8(v) };
                        bus.write8(addr, res);
                        if idx_reg.is_some() { 23 } else { 11 }
                    } else {
                        let v = self.read_reg8(y, bus, idx_reg);
                        let res = if is_inc { self.inc8(v) } else { self.dec8(v) };
                        self.write_reg8(y, res, bus, idx_reg, None);
                        4
                    }
                }
                6 => {
                    if y == 6 {
                        let addr = if let Some(base) = idx_reg {
                            let d = self.fetch8(bus) as i8 as i32;
                            (base as i32 + d) as u16
                        } else {
                            self.hl()
                        };
                        let imm = self.fetch8(bus);
                        bus.write8(addr, imm);
                        if idx_reg.is_some() { 19 } else { 10 }
                    } else {
                        let imm = self.fetch8(bus);
                        self.write_reg8(y, imm, bus, idx_reg, None);
                        7
                    }
                }
                _ => match y {
                    0 => {
                        // RLCA
                        let c = (self.a & 0x80) != 0;
                        self.a = (self.a << 1) | if c { 1 } else { 0 };
                        self.f = (self.f & (Z80_FLAG_S | Z80_FLAG_Z | Z80_FLAG_PV))
                            | (self.a & (Z80_FLAG_F5 | Z80_FLAG_F3))
                            | if c { Z80_FLAG_C } else { 0 };
                        4
                    }
                    1 => {
                        // RRCA
                        let c = (self.a & 0x01) != 0;
                        self.a = (self.a >> 1) | if c { 0x80 } else { 0 };
                        self.f = (self.f & (Z80_FLAG_S | Z80_FLAG_Z | Z80_FLAG_PV))
                            | (self.a & (Z80_FLAG_F5 | Z80_FLAG_F3))
                            | if c { Z80_FLAG_C } else { 0 };
                        4
                    }
                    2 => {
                        // RLA
                        let c = (self.a & 0x80) != 0;
                        let old_c = if (self.f & Z80_FLAG_C) != 0 { 1 } else { 0 };
                        self.a = (self.a << 1) | old_c;
                        self.f = (self.f & (Z80_FLAG_S | Z80_FLAG_Z | Z80_FLAG_PV))
                            | (self.a & (Z80_FLAG_F5 | Z80_FLAG_F3))
                            | if c { Z80_FLAG_C } else { 0 };
                        4
                    }
                    3 => {
                        // RRA
                        let c = (self.a & 0x01) != 0;
                        let old_c = if (self.f & Z80_FLAG_C) != 0 { 0x80 } else { 0 };
                        self.a = (self.a >> 1) | old_c;
                        self.f = (self.f & (Z80_FLAG_S | Z80_FLAG_Z | Z80_FLAG_PV))
                            | (self.a & (Z80_FLAG_F5 | Z80_FLAG_F3))
                            | if c { Z80_FLAG_C } else { 0 };
                        4
                    }
                    4 => {
                        self.daa();
                        4
                    }
                    5 => {
                        // CPL
                        self.a = !self.a;
                        self.f = (self.f & (Z80_FLAG_S | Z80_FLAG_Z | Z80_FLAG_PV | Z80_FLAG_C))
                            | (self.a & (Z80_FLAG_F5 | Z80_FLAG_F3))
                            | Z80_FLAG_H
                            | Z80_FLAG_N;
                        4
                    }
                    6 => {
                        // SCF
                        self.f = (self.f & (Z80_FLAG_S | Z80_FLAG_Z | Z80_FLAG_PV))
                            | (self.a & (Z80_FLAG_F5 | Z80_FLAG_F3))
                            | Z80_FLAG_C;
                        4
                    }
                    _ => {
                        // CCF
                        let old_c = (self.f & Z80_FLAG_C) != 0;
                        self.f = (self.f & (Z80_FLAG_S | Z80_FLAG_Z | Z80_FLAG_PV))
                            | (self.a & (Z80_FLAG_F5 | Z80_FLAG_F3))
                            | if old_c { Z80_FLAG_H } else { Z80_FLAG_C };
                        4
                    }
                },
            },
            1 => {
                if y == 6 && z == 6 {
                    self.halted = true;
                    4
                } else if y == 6 {
                    let addr = if let Some(base) = idx_reg {
                        let d = self.fetch8(bus) as i8 as i32;
                        (base as i32 + d) as u16
                    } else {
                        self.hl()
                    };
                    let val = self.read_reg8(z, bus, None);
                    bus.write8(addr, val);
                    if idx_reg.is_some() { 19 } else { 7 }
                } else if z == 6 {
                    let addr = if let Some(base) = idx_reg {
                        let d = self.fetch8(bus) as i8 as i32;
                        (base as i32 + d) as u16
                    } else {
                        self.hl()
                    };
                    let val = bus.read8(addr);
                    self.write_reg8(y, val, bus, None, None);
                    if idx_reg.is_some() { 19 } else { 7 }
                } else {
                    let val = self.read_reg8(z, bus, idx_reg);
                    self.write_reg8(y, val, bus, idx_reg, None);
                    4
                }
            }
            2 => {
                let val = if z == 6 {
                    let addr = if let Some(base) = idx_reg {
                        let d = self.fetch8(bus) as i8 as i32;
                        (base as i32 + d) as u16
                    } else {
                        self.hl()
                    };
                    bus.read8(addr)
                } else {
                    self.read_reg8(z, bus, idx_reg)
                };
                self.exec_alu(y, val);
                if z == 6 {
                    if idx_reg.is_some() { 19 } else { 7 }
                } else {
                    4
                }
            }
            _ => match z {
                0 => {
                    if self.check_cond(y) {
                        self.pc = self.pop16(bus);
                        11
                    } else {
                        5
                    }
                }
                1 => {
                    if q == 0 {
                        let v = self.pop16(bus);
                        self.set_rp2(p, v, use_ix, use_iy);
                        if idx_reg.is_some() { 14 } else { 10 }
                    } else {
                        match p {
                            0 => {
                                self.pc = self.pop16(bus);
                                10
                            }
                            1 => {
                                std::mem::swap(&mut self.b, &mut self.b_alt);
                                std::mem::swap(&mut self.c, &mut self.c_alt);
                                std::mem::swap(&mut self.d, &mut self.d_alt);
                                std::mem::swap(&mut self.e, &mut self.e_alt);
                                std::mem::swap(&mut self.h, &mut self.h_alt);
                                std::mem::swap(&mut self.l, &mut self.l_alt);
                                4
                            }
                            2 => {
                                self.pc = idx_reg.unwrap_or_else(|| self.hl());
                                if idx_reg.is_some() { 8 } else { 4 }
                            }
                            _ => {
                                self.sp = idx_reg.unwrap_or_else(|| self.hl());
                                if idx_reg.is_some() { 10 } else { 6 }
                            }
                        }
                    }
                }
                2 => {
                    let nn = self.fetch16(bus);
                    if self.check_cond(y) {
                        self.pc = nn;
                    }
                    10
                }
                3 => match y {
                    0 => {
                        self.pc = self.fetch16(bus);
                        10
                    }
                    2 => {
                        let port = ((self.a as u16) << 8) | (self.fetch8(bus) as u16);
                        bus.port_out8(port, self.a);
                        11
                    }
                    3 => {
                        let port = ((self.a as u16) << 8) | (self.fetch8(bus) as u16);
                        self.a = bus.port_in8(port);
                        11
                    }
                    4 => {
                        // EX (SP), HL/IX/IY
                        let lo = bus.read8(self.sp) as u16;
                        let hi = bus.read8(self.sp.wrapping_add(1)) as u16;
                        let mem = lo | (hi << 8);
                        let reg_val = idx_reg.unwrap_or_else(|| self.hl());
                        bus.write8(self.sp, (reg_val & 0xFF) as u8);
                        bus.write8(self.sp.wrapping_add(1), (reg_val >> 8) as u8);
                        if use_ix {
                            self.ix = mem;
                        } else if use_iy {
                            self.iy = mem;
                        } else {
                            self.set_hl(mem);
                        }
                        if idx_reg.is_some() { 23 } else { 19 }
                    }
                    5 => {
                        let de = self.de();
                        let hl = self.hl();
                        self.set_de(hl);
                        self.set_hl(de);
                        4
                    }
                    6 => {
                        self.iff1 = false;
                        self.iff2 = false;
                        4
                    }
                    _ => {
                        self.iff1 = true;
                        self.iff2 = true;
                        self.ei_delay = true;
                        4
                    }
                },
                4 => {
                    let nn = self.fetch16(bus);
                    if self.check_cond(y) {
                        let ret = self.pc;
                        self.push16(bus, ret);
                        self.pc = nn;
                        17
                    } else {
                        10
                    }
                }
                5 => {
                    if q == 0 {
                        let v = self.get_rp2(p, use_ix, use_iy);
                        self.push16(bus, v);
                        if idx_reg.is_some() { 15 } else { 11 }
                    } else {
                        let nn = self.fetch16(bus);
                        let ret = self.pc;
                        self.push16(bus, ret);
                        self.pc = nn;
                        17
                    }
                }
                6 => {
                    let imm = self.fetch8(bus);
                    self.exec_alu(y, imm);
                    7
                }
                _ => {
                    let ret = self.pc;
                    self.push16(bus, ret);
                    self.pc = (y as u16) * 8;
                    11
                }
            },
        }
    }

    fn exec_alu(&mut self, op: u8, val: u8) {
        match op & 7 {
            0 => self.add8(val, false),
            1 => self.add8(val, true),
            2 => {
                self.a = self.sub8(val, false);
            }
            3 => {
                self.a = self.sub8(val, true);
            }
            4 => self.and8(val),
            5 => self.xor8(val),
            6 => self.or8(val),
            _ => self.cp8(val),
        }
    }

    pub fn debug_state(&self) -> CpuDebugState {
        let flags = format!(
            "{}{}{}{}{}{}",
            if (self.f & Z80_FLAG_S) != 0 { 'S' } else { '-' },
            if (self.f & Z80_FLAG_Z) != 0 { 'Z' } else { '-' },
            if (self.f & Z80_FLAG_H) != 0 { 'H' } else { '-' },
            if (self.f & Z80_FLAG_PV) != 0 { 'P' } else { '-' },
            if (self.f & Z80_FLAG_N) != 0 { 'N' } else { '-' },
            if (self.f & Z80_FLAG_C) != 0 { 'C' } else { '-' },
        );
        CpuDebugState {
            arch: "Zilog Z80A",
            pc: self.pc as u32,
            sp: self.sp as u32,
            flags,
            registers: vec![
                ("AF".into(), format!("{:04X}", self.af())),
                ("BC".into(), format!("{:04X}", self.bc())),
                ("DE".into(), format!("{:04X}", self.de())),
                ("HL".into(), format!("{:04X}", self.hl())),
                ("IX".into(), format!("{:04X}", self.ix)),
                ("IY".into(), format!("{:04X}", self.iy)),
                ("SP".into(), format!("{:04X}", self.sp)),
                ("I/R".into(), format!("{:02X}{:02X}", self.i, self.r)),
                (
                    "IM".into(),
                    format!("{} IFF{}", self.im, if self.iff1 { 1 } else { 0 }),
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
            let (len, mnemonic) = disasm_z80_one(bus, addr);
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

fn disasm_z80_one<B: Bus8>(bus: &B, addr: u16) -> (usize, String) {
    const R8: [&str; 8] = ["B", "C", "D", "E", "H", "L", "(HL)", "A"];
    const RP: [&str; 4] = ["BC", "DE", "HL", "SP"];
    const RP2: [&str; 4] = ["BC", "DE", "HL", "AF"];
    const CC: [&str; 8] = ["NZ", "Z", "NC", "C", "PO", "PE", "P", "M"];
    const ALU: [&str; 8] = ["ADD A,", "ADC A,", "SUB ", "SBC A,", "AND ", "XOR ", "OR ", "CP "];

    let op = bus.peek8(addr);
    let b1 = bus.peek8(addr.wrapping_add(1));
    let b2 = bus.peek8(addr.wrapping_add(2));
    let nn = (b1 as u16) | ((b2 as u16) << 8);
    let rel = addr.wrapping_add(2).wrapping_add(b1 as i8 as i16 as u16);

    match op {
        0x00 => (1, "NOP".into()),
        0x08 => (1, "EX AF,AF'".into()),
        0x10 => (2, format!("DJNZ ${rel:04X}")),
        0x18 => (2, format!("JR ${rel:04X}")),
        0x20 | 0x28 | 0x30 | 0x38 => (2, format!("JR {},${rel:04X}", CC[((op - 0x20) >> 3) as usize])),
        0x02 => (1, "LD (BC),A".into()),
        0x12 => (1, "LD (DE),A".into()),
        0x0A => (1, "LD A,(BC)".into()),
        0x1A => (1, "LD A,(DE)".into()),
        0x22 => (3, format!("LD (${nn:04X}),HL")),
        0x2A => (3, format!("LD HL,(${nn:04X})")),
        0x32 => (3, format!("LD (${nn:04X}),A")),
        0x3A => (3, format!("LD A,(${nn:04X})")),
        0x07 => (1, "RLCA".into()),
        0x0F => (1, "RRCA".into()),
        0x17 => (1, "RLA".into()),
        0x1F => (1, "RRA".into()),
        0x27 => (1, "DAA".into()),
        0x2F => (1, "CPL".into()),
        0x37 => (1, "SCF".into()),
        0x3F => (1, "CCF".into()),
        0x76 => (1, "HALT".into()),
        0xC3 => (3, format!("JP ${nn:04X}")),
        0xC9 => (1, "RET".into()),
        0xCD => (3, format!("CALL ${nn:04X}")),
        0xD3 => (2, format!("OUT (${b1:02X}),A")),
        0xDB => (2, format!("IN A,(${b1:02X})")),
        0xD9 => (1, "EXX".into()),
        0xE3 => (1, "EX (SP),HL".into()),
        0xE9 => (1, "JP (HL)".into()),
        0xEB => (1, "EX DE,HL".into()),
        0xF3 => (1, "DI".into()),
        0xF9 => (1, "LD SP,HL".into()),
        0xFB => (1, "EI".into()),
        0xCB => {
            let group = b1 >> 6;
            let bit = (b1 >> 3) & 7;
            let r = R8[(b1 & 7) as usize];
            match group {
                0 => {
                    const ROT: [&str; 8] = ["RLC", "RRC", "RL", "RR", "SLA", "SRA", "SLL", "SRL"];
                    (2, format!("{} {r}", ROT[bit as usize]))
                }
                1 => (2, format!("BIT {bit},{r}")),
                2 => (2, format!("RES {bit},{r}")),
                _ => (2, format!("SET {bit},{r}")),
            }
        }
        0xED => (2, format!("ED {b1:02X}")),
        0xDD | 0xFD => {
            let idx = if op == 0xDD { "IX" } else { "IY" };
            (2, format!("{idx} op {b1:02X}"))
        }
        _ => {
            let x = op >> 6;
            let y = (op >> 3) & 7;
            let z = op & 7;
            let p = (y >> 1) as usize;
            let q = y & 1;
            match x {
                0 => match z {
                    1 => {
                        if q == 0 {
                            (3, format!("LD {},${nn:04X}", RP[p]))
                        } else {
                            (1, format!("ADD HL,{}", RP[p]))
                        }
                    }
                    3 => (1, format!("{} {}", if q == 0 { "INC" } else { "DEC" }, RP[p])),
                    4 => (1, format!("INC {}", R8[y as usize])),
                    5 => (1, format!("DEC {}", R8[y as usize])),
                    6 => (2, format!("LD {},${b1:02X}", R8[y as usize])),
                    _ => (1, format!("DB ${op:02X}")),
                },
                1 => (1, format!("LD {},{}", R8[y as usize], R8[z as usize])),
                2 => (1, format!("{}{}", ALU[y as usize], R8[z as usize])),
                _ => match z {
                    0 => (1, format!("RET {}", CC[y as usize])),
                    1 => (1, format!("POP {}", RP2[p])),
                    2 => (3, format!("JP {},${nn:04X}", CC[y as usize])),
                    4 => (3, format!("CALL {},${nn:04X}", CC[y as usize])),
                    5 => (1, format!("PUSH {}", RP2[p])),
                    6 => (2, format!("{}${b1:02X}", ALU[y as usize])),
                    _ => (1, format!("RST ${:02X}", y * 8)),
                },
            }
        }
    }
}
