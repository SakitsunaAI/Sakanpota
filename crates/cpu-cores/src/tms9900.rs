use emu_core::{CpuDebugState, DisassembledInstruction};

pub const ST_LGT: u16 = 0x8000;
pub const ST_AGT: u16 = 0x4000;
pub const ST_EQ: u16 = 0x2000;
pub const ST_C: u16 = 0x1000;
pub const ST_OV: u16 = 0x0800;
pub const ST_OP: u16 = 0x0400;
pub const ST_X: u16 = 0x0200;

/// Memory + CRU serial I/O bus for Texas Instruments TMS9900 (16-bit big-endian).
pub trait Tms9900Bus {
    fn read16(&mut self, addr: u16) -> u16;
    fn write16(&mut self, addr: u16, val: u16);
    fn read8(&mut self, addr: u16) -> u8;
    fn write8(&mut self, addr: u16, val: u8);
    fn peek8(&self, addr: u16) -> u8;
    fn cru_read_bit(&mut self, bit_addr: u16) -> bool;
    fn cru_write_bit(&mut self, bit_addr: u16, val: bool);
}

#[derive(Debug, Clone)]
pub struct Tms9900 {
    pub wp: u16,
    pub pc: u16,
    pub st: u16,
    pub halted: bool,
    pub cycles: u64,
    pub irq_level: Option<u8>,
}

impl Default for Tms9900 {
    fn default() -> Self {
        Self::new()
    }
}

impl Tms9900 {
    pub fn new() -> Self {
        Self {
            wp: 0,
            pc: 0,
            st: 0,
            halted: false,
            cycles: 0,
            irq_level: None,
        }
    }

    pub fn reset<B: Tms9900Bus>(&mut self, bus: &mut B) {
        self.wp = bus.read16(0x0000);
        self.pc = bus.read16(0x0002);
        self.st = 0;
        self.halted = false;
        self.irq_level = None;
        self.cycles = 0;
    }

    #[inline]
    pub fn request_irq(&mut self, level: u8) {
        self.irq_level = Some(level & 0x0F);
    }

    #[inline]
    fn reg_addr(&self, r: u16) -> u16 {
        self.wp.wrapping_add((r & 0xF) * 2)
    }

    #[inline]
    fn read_reg<B: Tms9900Bus>(&self, bus: &mut B, r: u16) -> u16 {
        bus.read16(self.reg_addr(r))
    }

    #[inline]
    fn write_reg<B: Tms9900Bus>(&self, bus: &mut B, r: u16, val: u16) {
        bus.write16(self.reg_addr(r), val);
    }

    #[inline]
    fn fetch16<B: Tms9900Bus>(&mut self, bus: &mut B) -> u16 {
        let v = bus.read16(self.pc);
        self.pc = self.pc.wrapping_add(2);
        v
    }

    fn effective_addr<B: Tms9900Bus>(&mut self, bus: &mut B, t: u16, reg: u16, is_byte: bool) -> u16 {
        let r = reg & 0x0F;
        match t & 3 {
            0 => self.reg_addr(r),
            1 => self.read_reg(bus, r),
            2 => {
                let disp = self.fetch16(bus);
                if r == 0 {
                    disp
                } else {
                    self.read_reg(bus, r).wrapping_add(disp)
                }
            }
            _ => {
                let addr = self.read_reg(bus, r);
                let step = if is_byte { 1 } else { 2 };
                self.write_reg(bus, r, addr.wrapping_add(step));
                addr
            }
        }
    }

    fn context_switch<B: Tms9900Bus>(&mut self, bus: &mut B, new_wp: u16, new_pc: u16) {
        let old_wp = self.wp;
        let old_pc = self.pc;
        let old_st = self.st;
        self.wp = new_wp;
        self.pc = new_pc;
        self.write_reg(bus, 13, old_wp);
        self.write_reg(bus, 14, old_pc);
        self.write_reg(bus, 15, old_st);
    }

    fn compare_flags16(&mut self, a: u16, b: u16) {
        self.st &= !(ST_LGT | ST_AGT | ST_EQ);
        if a > b {
            self.st |= ST_LGT;
        }
        if (a as i16) > (b as i16) {
            self.st |= ST_AGT;
        }
        if a == b {
            self.st |= ST_EQ;
        }
    }

    fn compare_flags8(&mut self, a: u8, b: u8) {
        self.st &= !(ST_LGT | ST_AGT | ST_EQ | ST_OP);
        if a > b {
            self.st |= ST_LGT;
        }
        if (a as i8) > (b as i8) {
            self.st |= ST_AGT;
        }
        if a == b {
            self.st |= ST_EQ;
        }
        if a.count_ones() % 2 == 1 {
            self.st |= ST_OP;
        }
    }

    fn update_lae16(&mut self, res: u16) {
        self.st &= !(ST_LGT | ST_AGT | ST_EQ);
        if res != 0 {
            self.st |= ST_LGT;
        }
        if (res as i16) > 0 {
            self.st |= ST_AGT;
        }
        if res == 0 {
            self.st |= ST_EQ;
        }
    }

    fn update_laep8(&mut self, res: u8) {
        self.st &= !(ST_LGT | ST_AGT | ST_EQ | ST_OP);
        if res != 0 {
            self.st |= ST_LGT;
        }
        if (res as i8) > 0 {
            self.st |= ST_AGT;
        }
        if res == 0 {
            self.st |= ST_EQ;
        }
        if res.count_ones() % 2 == 1 {
            self.st |= ST_OP;
        }
    }

    pub fn step<B: Tms9900Bus>(&mut self, bus: &mut B) -> u32 {
        if let Some(lvl) = self.irq_level {
            if (lvl as u16) <= (self.st & 0x000F) {
                self.irq_level = None;
                self.halted = false;
                let vec_addr = (lvl as u16) * 4;
                let nwp = bus.read16(vec_addr);
                let npc = bus.read16(vec_addr + 2);
                self.context_switch(bus, nwp, npc);
                self.st = (self.st & !0x000F) | (lvl.saturating_sub(1) as u16);
                self.cycles += 22;
                return 22;
            }
        }

        if self.halted {
            self.cycles += 4;
            return 4;
        }

        let op = self.fetch16(bus);
        let cyc = self.exec_opcode(bus, op);
        self.cycles += cyc as u64;
        cyc
    }

    fn exec_opcode<B: Tms9900Bus>(&mut self, bus: &mut B, op: u16) -> u32 {
        let top = (op >> 12) & 0x0F;
        if top >= 4 {
            let is_byte = (top & 1) != 0;
            let ts = (op >> 4) & 3;
            let s = op & 0x0F;
            let td = (op >> 10) & 3;
            let d = (op >> 6) & 0x0F;
            let s_addr = self.effective_addr(bus, ts, s, is_byte);

            if is_byte {
                let sv = bus.read8(s_addr);
                let d_addr = self.effective_addr(bus, td, d, is_byte);
                let dv = if top == 0xD { 0 } else { bus.read8(d_addr) };
                match top {
                    0x5 => {
                        let res = dv & !sv;
                        self.update_laep8(res);
                        bus.write8(d_addr, res);
                    }
                    0x7 => {
                        let diff = dv as i16 - sv as i16;
                        let res = diff as u8;
                        self.update_laep8(res);
                        self.st &= !(ST_C | ST_OV);
                        if dv >= sv {
                            self.st |= ST_C;
                        }
                        if (((dv ^ sv) & (dv ^ res)) & 0x80) != 0 {
                            self.st |= ST_OV;
                        }
                        bus.write8(d_addr, res);
                    }
                    0x9 => self.compare_flags8(sv, dv),
                    0xB => {
                        let sum = dv as u16 + sv as u16;
                        let res = sum as u8;
                        self.update_laep8(res);
                        self.st &= !(ST_C | ST_OV);
                        if sum > 0xFF {
                            self.st |= ST_C;
                        }
                        if ((!(dv ^ sv) & (dv ^ res)) & 0x80) != 0 {
                            self.st |= ST_OV;
                        }
                        bus.write8(d_addr, res);
                    }
                    0xD => {
                        self.update_laep8(sv);
                        bus.write8(d_addr, sv);
                    }
                    _ => {
                        let res = dv | sv;
                        self.update_laep8(res);
                        bus.write8(d_addr, res);
                    }
                }
            } else {
                let sv = bus.read16(s_addr);
                let d_addr = self.effective_addr(bus, td, d, is_byte);
                let dv = if top == 0xC { 0 } else { bus.read16(d_addr) };
                match top {
                    0x4 => {
                        let res = dv & !sv;
                        self.update_lae16(res);
                        bus.write16(d_addr, res);
                    }
                    0x6 => {
                        let diff = dv as i32 - sv as i32;
                        let res = diff as u16;
                        self.update_lae16(res);
                        self.st &= !(ST_C | ST_OV);
                        if dv >= sv {
                            self.st |= ST_C;
                        }
                        if (((dv ^ sv) & (dv ^ res)) & 0x8000) != 0 {
                            self.st |= ST_OV;
                        }
                        bus.write16(d_addr, res);
                    }
                    0x8 => self.compare_flags16(sv, dv),
                    0xA => {
                        let sum = dv as u32 + sv as u32;
                        let res = sum as u16;
                        self.update_lae16(res);
                        self.st &= !(ST_C | ST_OV);
                        if sum > 0xFFFF {
                            self.st |= ST_C;
                        }
                        if ((!(dv ^ sv) & (dv ^ res)) & 0x8000) != 0 {
                            self.st |= ST_OV;
                        }
                        bus.write16(d_addr, res);
                    }
                    0xC => {
                        self.update_lae16(sv);
                        bus.write16(d_addr, sv);
                    }
                    _ => {
                        let res = dv | sv;
                        self.update_lae16(res);
                        bus.write16(d_addr, res);
                    }
                }
            }
            return 14;
        }

        // Format III (0x2000..0x3FFF)
        if (0x2000..=0x3FFF).contains(&op) {
            let sub = (op >> 10) & 7;
            let d = (op >> 6) & 0x0F;
            let ts = (op >> 4) & 3;
            let s = op & 0x0F;
            match sub {
                0 => {
                    // COC
                    let sa = self.effective_addr(bus, ts, s, false);
                    let sv = bus.read16(sa);
                    let dv = self.read_reg(bus, d);
                    if (sv & dv) == sv {
                        self.st |= ST_EQ;
                    } else {
                        self.st &= !ST_EQ;
                    }
                }
                1 => {
                    // CZC
                    let sa = self.effective_addr(bus, ts, s, false);
                    let sv = bus.read16(sa);
                    let dv = self.read_reg(bus, d);
                    if (sv & !dv) == sv {
                        self.st |= ST_EQ;
                    } else {
                        self.st &= !ST_EQ;
                    }
                }
                2 => {
                    // XOR
                    let sa = self.effective_addr(bus, ts, s, false);
                    let sv = bus.read16(sa);
                    let res = self.read_reg(bus, d) ^ sv;
                    self.update_lae16(res);
                    self.write_reg(bus, d, res);
                }
                3 => {
                    // XOP
                    let sa = self.effective_addr(bus, ts, s, false);
                    let vec = 0x0040 + (d & 0x0F) * 4;
                    let nwp = bus.read16(vec);
                    let npc = bus.read16(vec + 2);
                    self.context_switch(bus, nwp, npc);
                    self.write_reg(bus, 11, sa);
                    self.st |= ST_X;
                }
                4 => {
                    // LDCR
                    let count = if d == 0 { 16 } else { d };
                    let sa = self.effective_addr(bus, ts, s, count <= 8);
                    let val = if count <= 8 {
                        let b = bus.read8(sa);
                        self.update_laep8(b);
                        b as u16
                    } else {
                        let w = bus.read16(sa);
                        self.update_lae16(w);
                        w
                    };
                    let base_bit = (self.read_reg(bus, 12) >> 1) & 0x0FFF;
                    for i in 0..count {
                        bus.cru_write_bit(base_bit.wrapping_add(i) & 0x0FFF, ((val >> i) & 1) != 0);
                    }
                }
                5 => {
                    // STCR
                    let count = if d == 0 { 16 } else { d };
                    let sa = self.effective_addr(bus, ts, s, count <= 8);
                    let base_bit = (self.read_reg(bus, 12) >> 1) & 0x0FFF;
                    let mut val = 0u16;
                    for i in 0..count {
                        if bus.cru_read_bit(base_bit.wrapping_add(i) & 0x0FFF) {
                            val |= 1 << i;
                        }
                    }
                    if count <= 8 {
                        let b = val as u8;
                        self.update_laep8(b);
                        bus.write8(sa, b);
                    } else {
                        self.update_lae16(val);
                        bus.write16(sa, val);
                    }
                }
                6 => {
                    // MPY
                    let sa = self.effective_addr(bus, ts, s, false);
                    let sv = bus.read16(sa) as u32;
                    let dv = self.read_reg(bus, d) as u32;
                    let prod = sv * dv;
                    self.write_reg(bus, d, (prod >> 16) as u16);
                    self.write_reg(bus, (d + 1) & 0x0F, (prod & 0xFFFF) as u16);
                }
                _ => {
                    // DIV
                    let sa = self.effective_addr(bus, ts, s, false);
                    let divisor = bus.read16(sa) as u32;
                    let hi = self.read_reg(bus, d) as u32;
                    let lo = self.read_reg(bus, (d + 1) & 0x0F) as u32;
                    if divisor <= hi {
                        self.st |= ST_OV;
                    } else {
                        self.st &= !ST_OV;
                        let num = (hi << 16) | lo;
                        self.write_reg(bus, d, (num / divisor) as u16);
                        self.write_reg(bus, (d + 1) & 0x0F, (num % divisor) as u16);
                    }
                }
            }
            return 20;
        }

        // Format II (0x1000..0x1FFF): Jumps & CRU single-bit
        if (0x1000..=0x1FFF).contains(&op) {
            let sub = (op >> 8) & 0x0F;
            let disp_i8 = (op & 0xFF) as i8;
            let target = self.pc.wrapping_add(((disp_i8 as i16) * 2) as u16);
            let lgt = (self.st & ST_LGT) != 0;
            let agt = (self.st & ST_AGT) != 0;
            let eq = (self.st & ST_EQ) != 0;
            let c = (self.st & ST_C) != 0;
            let ov = (self.st & ST_OV) != 0;
            let opp = (self.st & ST_OP) != 0;
            match sub {
                0x0 => self.pc = target,
                0x1 => if !agt && !eq { self.pc = target; },
                0x2 => if !lgt || eq { self.pc = target; },
                0x3 => if eq { self.pc = target; },
                0x4 => if lgt || eq { self.pc = target; },
                0x5 => if agt { self.pc = target; },
                0x6 => if !eq { self.pc = target; },
                0x7 => if !c { self.pc = target; },
                0x8 => if c { self.pc = target; },
                0x9 => if !ov { self.pc = target; },
                0xA => if !lgt && !eq { self.pc = target; },
                0xB => if lgt && !eq { self.pc = target; },
                0xC => if opp { self.pc = target; },
                0xD => {
                    let bit = ((self.read_reg(bus, 12) >> 1) as i16 + disp_i8 as i16) as u16;
                    bus.cru_write_bit(bit & 0x0FFF, true);
                }
                0xE => {
                    let bit = ((self.read_reg(bus, 12) >> 1) as i16 + disp_i8 as i16) as u16;
                    bus.cru_write_bit(bit & 0x0FFF, false);
                }
                _ => {
                    let bit = ((self.read_reg(bus, 12) >> 1) as i16 + disp_i8 as i16) as u16;
                    if bus.cru_read_bit(bit & 0x0FFF) {
                        self.st |= ST_EQ;
                    } else {
                        self.st &= !ST_EQ;
                    }
                }
            }
            return 10;
        }

        // Format V (0x0800..0x0BFF): Shifts
        if (0x0800..=0x0BFF).contains(&op) {
            let sub = (op >> 8) & 3;
            let mut count = (op >> 4) & 0x0F;
            let reg = op & 0x0F;
            if count == 0 {
                count = self.read_reg(bus, 0) & 0x0F;
                if count == 0 {
                    count = 16;
                }
            }
            let mut val = self.read_reg(bus, reg);
            let mut carry = false;
            let mut ov = false;
            for _ in 0..count {
                match sub {
                    0 => {
                        // SRA
                        carry = (val & 1) != 0;
                        val = ((val as i16) >> 1) as u16;
                    }
                    1 => {
                        // SRL
                        carry = (val & 1) != 0;
                        val >>= 1;
                    }
                    2 => {
                        // SLA
                        let msb_before = (val & 0x8000) != 0;
                        carry = msb_before;
                        val <<= 1;
                        if ((val & 0x8000) != 0) != msb_before {
                            ov = true;
                        }
                    }
                    _ => {
                        // SRC
                        carry = (val & 1) != 0;
                        val = (val >> 1) | if carry { 0x8000 } else { 0 };
                    }
                }
            }
            self.update_lae16(val);
            if carry {
                self.st |= ST_C;
            } else {
                self.st &= !ST_C;
            }
            if sub == 2 {
                if ov {
                    self.st |= ST_OV;
                } else {
                    self.st &= !ST_OV;
                }
            }
            self.write_reg(bus, reg, val);
            return 12 + 2 * (count as u32);
        }

        // Format VI (0x0400..0x07FF): Single operand
        if (0x0400..=0x07FF).contains(&op) {
            let sub = (op >> 6) & 0x0F;
            let ts = (op >> 4) & 3;
            let s = op & 0x0F;
            let sa = self.effective_addr(bus, ts, s, false);
            match sub {
                0x0 => {
                    // BLWP
                    let nwp = bus.read16(sa);
                    let npc = bus.read16(sa.wrapping_add(2));
                    self.context_switch(bus, nwp, npc);
                }
                0x1 => self.pc = sa,
                0x2 => {
                    let sub_op = bus.read16(sa);
                    return self.exec_opcode(bus, sub_op);
                }
                0x3 => bus.write16(sa, 0),
                0x4 => {
                    let v = bus.read16(sa);
                    let res = (-(v as i16)) as u16;
                    self.update_lae16(res);
                    self.st &= !(ST_C | ST_OV);
                    if v == 0 {
                        self.st |= ST_C;
                    }
                    if v == 0x8000 {
                        self.st |= ST_OV;
                    }
                    bus.write16(sa, res);
                }
                0x5 => {
                    let res = !bus.read16(sa);
                    self.update_lae16(res);
                    bus.write16(sa, res);
                }
                0x6 => {
                    let v = bus.read16(sa);
                    let (res, carry) = v.overflowing_add(1);
                    self.update_lae16(res);
                    self.st &= !(ST_C | ST_OV);
                    if carry {
                        self.st |= ST_C;
                    }
                    if v == 0x7FFF {
                        self.st |= ST_OV;
                    }
                    bus.write16(sa, res);
                }
                0x7 => {
                    let v = bus.read16(sa);
                    let (res, carry) = v.overflowing_add(2);
                    self.update_lae16(res);
                    self.st &= !(ST_C | ST_OV);
                    if carry {
                        self.st |= ST_C;
                    }
                    if v == 0x7FFE || v == 0x7FFF {
                        self.st |= ST_OV;
                    }
                    bus.write16(sa, res);
                }
                0x8 => {
                    let v = bus.read16(sa);
                    let res = v.wrapping_sub(1);
                    self.update_lae16(res);
                    self.st &= !(ST_C | ST_OV);
                    if v >= 1 {
                        self.st |= ST_C;
                    }
                    if v == 0x8000 {
                        self.st |= ST_OV;
                    }
                    bus.write16(sa, res);
                }
                0x9 => {
                    let v = bus.read16(sa);
                    let res = v.wrapping_sub(2);
                    self.update_lae16(res);
                    self.st &= !(ST_C | ST_OV);
                    if v >= 2 {
                        self.st |= ST_C;
                    }
                    if v == 0x8000 || v == 0x8001 {
                        self.st |= ST_OV;
                    }
                    bus.write16(sa, res);
                }
                0xA => {
                    // BL
                    let ret = self.pc;
                    self.write_reg(bus, 11, ret);
                    self.pc = sa;
                }
                0xB => {
                    let v = bus.read16(sa);
                    bus.write16(sa, v.swap_bytes());
                }
                0xC => bus.write16(sa, 0xFFFF),
                0xD => {
                    let v = bus.read16(sa) as i16;
                    let res = v.wrapping_abs() as u16;
                    self.update_lae16(v as u16);
                    self.st &= !(ST_C | ST_OV);
                    if v == i16::MIN {
                        self.st |= ST_OV;
                    }
                    bus.write16(sa, res);
                }
                _ => {}
            }
            return 12;
        }

        // Format VII / VIII (0x0200..0x03FF)
        let reg = op & 0x0F;
        match op & 0xFFE0 {
            0x0200 => {
                let imm = self.fetch16(bus);
                self.update_lae16(imm);
                self.write_reg(bus, reg, imm);
                12
            }
            0x0220 => {
                let imm = self.fetch16(bus);
                let v = self.read_reg(bus, reg);
                let sum = v as u32 + imm as u32;
                let res = sum as u16;
                self.update_lae16(res);
                self.st &= !(ST_C | ST_OV);
                if sum > 0xFFFF {
                    self.st |= ST_C;
                }
                if ((!(v ^ imm) & (v ^ res)) & 0x8000) != 0 {
                    self.st |= ST_OV;
                }
                self.write_reg(bus, reg, res);
                14
            }
            0x0240 => {
                let imm = self.fetch16(bus);
                let res = self.read_reg(bus, reg) & imm;
                self.update_lae16(res);
                self.write_reg(bus, reg, res);
                14
            }
            0x0260 => {
                let imm = self.fetch16(bus);
                let res = self.read_reg(bus, reg) | imm;
                self.update_lae16(res);
                self.write_reg(bus, reg, res);
                14
            }
            0x0280 => {
                let imm = self.fetch16(bus);
                let v = self.read_reg(bus, reg);
                self.compare_flags16(v, imm);
                14
            }
            0x02A0 => {
                self.write_reg(bus, reg, self.wp);
                8
            }
            0x02C0 => {
                self.write_reg(bus, reg, self.st);
                8
            }
            0x02E0 => {
                self.wp = self.fetch16(bus);
                10
            }
            0x0300 => {
                let mask = self.fetch16(bus) & 0x000F;
                self.st = (self.st & !0x000F) | mask;
                16
            }
            0x0340 => {
                self.halted = true;
                12
            }
            0x0360 => {
                self.st &= !0x000F;
                12
            }
            0x0380 => {
                // RTWP
                let old_wp = self.read_reg(bus, 13);
                let old_pc = self.read_reg(bus, 14);
                let old_st = self.read_reg(bus, 15);
                self.wp = old_wp;
                self.pc = old_pc;
                self.st = old_st;
                14
            }
            _ => 8,
        }
    }

    pub fn debug_state(&self) -> CpuDebugState {
        let flags = format!(
            "{}{}{}{}{}{}{}|M{}",
            if (self.st & ST_LGT) != 0 { 'L' } else { '-' },
            if (self.st & ST_AGT) != 0 { 'A' } else { '-' },
            if (self.st & ST_EQ) != 0 { 'E' } else { '-' },
            if (self.st & ST_C) != 0 { 'C' } else { '-' },
            if (self.st & ST_OV) != 0 { 'O' } else { '-' },
            if (self.st & ST_OP) != 0 { 'P' } else { '-' },
            if (self.st & ST_X) != 0 { 'X' } else { '-' },
            self.st & 0x0F
        );
        CpuDebugState {
            arch: "TI TMS9900",
            pc: self.pc as u32,
            sp: self.wp as u32,
            flags,
            registers: vec![
                ("PC".into(), format!("{:04X}", self.pc)),
                ("WP".into(), format!("{:04X}", self.wp)),
                ("ST".into(), format!("{:04X}", self.st)),
            ],
            cycles: self.cycles,
            halted: self.halted,
        }
    }

    pub fn disassemble<B: Tms9900Bus>(
        &self,
        bus: &B,
        start: u16,
        count: usize,
    ) -> Vec<DisassembledInstruction> {
        let mut out = Vec::with_capacity(count);
        let mut addr = start;
        for _ in 0..count {
            let b0 = bus.peek8(addr);
            let b1 = bus.peek8(addr.wrapping_add(1));
            let op = ((b0 as u16) << 8) | (b1 as u16);
            let mnemonic = disasm_9900_one(op);
            out.push(DisassembledInstruction {
                address: addr as u32,
                bytes: vec![b0, b1],
                mnemonic,
            });
            addr = addr.wrapping_add(2);
        }
        out
    }
}

fn disasm_9900_one(op: u16) -> String {
    let top = op >> 12;
    match top {
        0x4 => "SZC".into(),
        0x5 => "SZCB".into(),
        0x6 => "S".into(),
        0x7 => "SB".into(),
        0x8 => "C".into(),
        0x9 => "CB".into(),
        0xA => "A".into(),
        0xB => "AB".into(),
        0xC => "MOV".into(),
        0xD => "MOVB".into(),
        0xE => "SOC".into(),
        0xF => "SOCB".into(),
        0x1 => format!("Jcc {:+}", ((op & 0xFF) as i8) * 2),
        _ => match op & 0xFFE0 {
            0x0200 => format!("LI R{},imm", op & 0xF),
            0x0220 => format!("AI R{},imm", op & 0xF),
            0x0280 => format!("CI R{},imm", op & 0xF),
            0x02E0 => "LWPI imm".into(),
            0x0300 => "LIMI imm".into(),
            0x0380 => "RTWP".into(),
            _ => format!("DW ${op:04X}"),
        },
    }
}
