use emu_core::{Bus32, CpuDebugState, DisassembledInstruction, IoPortBus};

pub const X86_CF: u16 = 0x0001;
pub const X86_PF: u16 = 0x0004;
pub const X86_AF: u16 = 0x0010;
pub const X86_ZF: u16 = 0x0040;
pub const X86_SF: u16 = 0x0080;
pub const X86_TF: u16 = 0x0100;
pub const X86_IF: u16 = 0x0200;
pub const X86_DF: u16 = 0x0400;
pub const X86_OF: u16 = 0x0800;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum SegReg {
    Es = 0,
    Cs = 1,
    Ss = 2,
    Ds = 3,
}

#[derive(Debug, Clone, Copy)]
struct ModRm {
    reg: u8,
    rm: u8,
    is_mem: bool,
    seg: SegReg,
    offset: u16,
}

#[derive(Debug, Clone)]
pub struct I8086 {
    /// AX, CX, DX, BX, SP, BP, SI, DI
    pub regs: [u16; 8],
    /// ES, CS, SS, DS
    pub sregs: [u16; 4],
    pub ip: u16,
    pub flags: u16,
    pub halted: bool,
    pub cycles: u64,
}

impl Default for I8086 {
    fn default() -> Self {
        Self::new()
    }
}

impl I8086 {
    pub const AX: usize = 0;
    pub const CX: usize = 1;
    pub const DX: usize = 2;
    pub const BX: usize = 3;
    pub const SP: usize = 4;
    pub const BP: usize = 5;
    pub const SI: usize = 6;
    pub const DI: usize = 7;

    pub const ES: usize = 0;
    pub const CS: usize = 1;
    pub const SS: usize = 2;
    pub const DS: usize = 3;

    pub fn new() -> Self {
        let mut cpu = Self {
            regs: [0; 8],
            sregs: [0; 4],
            ip: 0,
            flags: 0x0002,
            halted: false,
            cycles: 0,
        };
        cpu.reset();
        cpu
    }

    pub fn reset(&mut self) {
        self.regs = [0; 8];
        self.sregs = [0, 0xFFFF, 0, 0];
        self.ip = 0x0000;
        self.flags = 0x0002;
        self.halted = false;
        self.cycles = 0;
    }

    #[inline]
    pub fn phys(seg: u16, off: u16) -> u32 {
        (((seg as u32) << 4).wrapping_add(off as u32)) & 0xFFFFF
    }

    #[inline]
    fn get_reg8(&self, idx: u8) -> u8 {
        let r = self.regs[(idx & 3) as usize];
        if (idx & 4) == 0 {
            (r & 0xFF) as u8
        } else {
            (r >> 8) as u8
        }
    }

    #[inline]
    fn set_reg8(&mut self, idx: u8, val: u8) {
        let slot = &mut self.regs[(idx & 3) as usize];
        if (idx & 4) == 0 {
            *slot = (*slot & 0xFF00) | (val as u16);
        } else {
            *slot = (*slot & 0x00FF) | ((val as u16) << 8);
        }
    }

    #[inline]
    fn read_mem8<B: Bus32>(&self, bus: &mut B, seg: SegReg, off: u16) -> u8 {
        bus.read8(Self::phys(self.sregs[seg as usize], off))
    }

    #[inline]
    fn write_mem8<B: Bus32>(&self, bus: &mut B, seg: SegReg, off: u16, val: u8) {
        bus.write8(Self::phys(self.sregs[seg as usize], off), val);
    }

    #[inline]
    fn read_mem16<B: Bus32>(&self, bus: &mut B, seg: SegReg, off: u16) -> u16 {
        let lo = self.read_mem8(bus, seg, off) as u16;
        let hi = self.read_mem8(bus, seg, off.wrapping_add(1)) as u16;
        lo | (hi << 8)
    }

    #[inline]
    fn write_mem16<B: Bus32>(&self, bus: &mut B, seg: SegReg, off: u16, val: u16) {
        self.write_mem8(bus, seg, off, (val & 0xFF) as u8);
        self.write_mem8(bus, seg, off.wrapping_add(1), (val >> 8) as u8);
    }

    #[inline]
    fn fetch8<B: Bus32>(&mut self, bus: &mut B) -> u8 {
        let addr = Self::phys(self.sregs[Self::CS], self.ip);
        self.ip = self.ip.wrapping_add(1);
        bus.read8(addr)
    }

    #[inline]
    fn fetch16<B: Bus32>(&mut self, bus: &mut B) -> u16 {
        let lo = self.fetch8(bus) as u16;
        let hi = self.fetch8(bus) as u16;
        lo | (hi << 8)
    }

    #[inline]
    fn push16<B: Bus32>(&mut self, bus: &mut B, val: u16) {
        self.regs[Self::SP] = self.regs[Self::SP].wrapping_sub(2);
        let addr = Self::phys(self.sregs[Self::SS], self.regs[Self::SP]);
        bus.write16_le(addr, val);
    }

    #[inline]
    fn pop16<B: Bus32>(&mut self, bus: &mut B) -> u16 {
        let addr = Self::phys(self.sregs[Self::SS], self.regs[Self::SP]);
        self.regs[Self::SP] = self.regs[Self::SP].wrapping_add(2);
        bus.read16_le(addr)
    }

    #[inline]
    fn set_flag(&mut self, mask: u16, cond: bool) {
        if cond {
            self.flags |= mask;
        } else {
            self.flags &= !mask;
        }
    }

    #[inline]
    fn update_psz8(&mut self, val: u8) {
        self.set_flag(X86_ZF, val == 0);
        self.set_flag(X86_SF, (val & 0x80) != 0);
        self.set_flag(X86_PF, val.count_ones().is_multiple_of(2));
    }

    #[inline]
    fn update_psz16(&mut self, val: u16) {
        self.set_flag(X86_ZF, val == 0);
        self.set_flag(X86_SF, (val & 0x8000) != 0);
        self.set_flag(X86_PF, ((val & 0xFF) as u8).count_ones().is_multiple_of(2));
    }

    fn decode_modrm<B: Bus32>(&mut self, bus: &mut B, seg_override: Option<SegReg>) -> ModRm {
        let b = self.fetch8(bus);
        let md = b >> 6;
        let reg = (b >> 3) & 7;
        let rm = b & 7;

        if md == 3 {
            return ModRm {
                reg,
                rm,
                is_mem: false,
                seg: SegReg::Ds,
                offset: 0,
            };
        }

        let mut default_seg = SegReg::Ds;
        let base_off = match rm {
            0 => self.regs[Self::BX].wrapping_add(self.regs[Self::SI]),
            1 => self.regs[Self::BX].wrapping_add(self.regs[Self::DI]),
            2 => {
                default_seg = SegReg::Ss;
                self.regs[Self::BP].wrapping_add(self.regs[Self::SI])
            }
            3 => {
                default_seg = SegReg::Ss;
                self.regs[Self::BP].wrapping_add(self.regs[Self::DI])
            }
            4 => self.regs[Self::SI],
            5 => self.regs[Self::DI],
            6 => {
                if md == 0 {
                    self.fetch16(bus)
                } else {
                    default_seg = SegReg::Ss;
                    self.regs[Self::BP]
                }
            }
            _ => self.regs[Self::BX],
        };

        let disp = match md {
            1 => self.fetch8(bus) as i8 as i16 as u16,
            2 => self.fetch16(bus),
            _ => 0,
        };

        ModRm {
            reg,
            rm,
            is_mem: true,
            seg: seg_override.unwrap_or(default_seg),
            offset: base_off.wrapping_add(disp),
        }
    }

    fn read_rm8<B: Bus32>(&self, bus: &mut B, m: ModRm) -> u8 {
        if m.is_mem {
            self.read_mem8(bus, m.seg, m.offset)
        } else {
            self.get_reg8(m.rm)
        }
    }

    fn write_rm8<B: Bus32>(&mut self, bus: &mut B, m: ModRm, val: u8) {
        if m.is_mem {
            self.write_mem8(bus, m.seg, m.offset, val);
        } else {
            self.set_reg8(m.rm, val);
        }
    }

    fn read_rm16<B: Bus32>(&self, bus: &mut B, m: ModRm) -> u16 {
        if m.is_mem {
            self.read_mem16(bus, m.seg, m.offset)
        } else {
            self.regs[m.rm as usize]
        }
    }

    fn write_rm16<B: Bus32>(&mut self, bus: &mut B, m: ModRm, val: u16) {
        if m.is_mem {
            self.write_mem16(bus, m.seg, m.offset, val);
        } else {
            self.regs[m.rm as usize] = val;
        }
    }

    fn alu8(&mut self, op: u8, a: u8, b: u8) -> u8 {
        let cf_in = if (self.flags & X86_CF) != 0 { 1u16 } else { 0u16 };
        match op & 7 {
            0 => {
                // ADD
                let sum = a as u16 + b as u16;
                let res = sum as u8;
                self.set_flag(X86_CF, sum > 0xFF);
                self.set_flag(X86_OF, ((!(a ^ b)) & (a ^ res) & 0x80) != 0);
                self.set_flag(X86_AF, ((a ^ b ^ res) & 0x10) != 0);
                self.update_psz8(res);
                res
            }
            1 => {
                // OR
                let res = a | b;
                self.flags &= !(X86_CF | X86_OF);
                self.update_psz8(res);
                res
            }
            2 => {
                // ADC
                let sum = a as u16 + b as u16 + cf_in;
                let res = sum as u8;
                self.set_flag(X86_CF, sum > 0xFF);
                self.set_flag(X86_OF, ((!(a ^ b)) & (a ^ res) & 0x80) != 0);
                self.set_flag(X86_AF, ((a ^ b ^ res) & 0x10) != 0);
                self.update_psz8(res);
                res
            }
            3 => {
                // SBB
                let diff = a as i16 - b as i16 - cf_in as i16;
                let res = diff as u8;
                self.set_flag(X86_CF, diff < 0);
                self.set_flag(X86_OF, ((a ^ b) & (a ^ res) & 0x80) != 0);
                self.set_flag(X86_AF, ((a ^ b ^ res) & 0x10) != 0);
                self.update_psz8(res);
                res
            }
            4 => {
                // AND
                let res = a & b;
                self.flags &= !(X86_CF | X86_OF);
                self.update_psz8(res);
                res
            }
            5 | 7 => {
                // SUB / CMP
                let diff = a as i16 - b as i16;
                let res = diff as u8;
                self.set_flag(X86_CF, diff < 0);
                self.set_flag(X86_OF, ((a ^ b) & (a ^ res) & 0x80) != 0);
                self.set_flag(X86_AF, ((a ^ b ^ res) & 0x10) != 0);
                self.update_psz8(res);
                res
            }
            _ => {
                // XOR
                let res = a ^ b;
                self.flags &= !(X86_CF | X86_OF);
                self.update_psz8(res);
                res
            }
        }
    }

    fn alu16(&mut self, op: u8, a: u16, b: u16) -> u16 {
        let cf_in = if (self.flags & X86_CF) != 0 { 1u32 } else { 0u32 };
        match op & 7 {
            0 => {
                let sum = a as u32 + b as u32;
                let res = sum as u16;
                self.set_flag(X86_CF, sum > 0xFFFF);
                self.set_flag(X86_OF, ((!(a ^ b)) & (a ^ res) & 0x8000) != 0);
                self.set_flag(X86_AF, ((a ^ b ^ res) & 0x10) != 0);
                self.update_psz16(res);
                res
            }
            1 => {
                let res = a | b;
                self.flags &= !(X86_CF | X86_OF);
                self.update_psz16(res);
                res
            }
            2 => {
                let sum = a as u32 + b as u32 + cf_in;
                let res = sum as u16;
                self.set_flag(X86_CF, sum > 0xFFFF);
                self.set_flag(X86_OF, ((!(a ^ b)) & (a ^ res) & 0x8000) != 0);
                self.set_flag(X86_AF, ((a ^ b ^ res) & 0x10) != 0);
                self.update_psz16(res);
                res
            }
            3 => {
                let diff = a as i32 - b as i32 - cf_in as i32;
                let res = diff as u16;
                self.set_flag(X86_CF, diff < 0);
                self.set_flag(X86_OF, ((a ^ b) & (a ^ res) & 0x8000) != 0);
                self.set_flag(X86_AF, ((a ^ b ^ res) & 0x10) != 0);
                self.update_psz16(res);
                res
            }
            4 => {
                let res = a & b;
                self.flags &= !(X86_CF | X86_OF);
                self.update_psz16(res);
                res
            }
            5 | 7 => {
                let diff = a as i32 - b as i32;
                let res = diff as u16;
                self.set_flag(X86_CF, diff < 0);
                self.set_flag(X86_OF, ((a ^ b) & (a ^ res) & 0x8000) != 0);
                self.set_flag(X86_AF, ((a ^ b ^ res) & 0x10) != 0);
                self.update_psz16(res);
                res
            }
            _ => {
                let res = a ^ b;
                self.flags &= !(X86_CF | X86_OF);
                self.update_psz16(res);
                res
            }
        }
    }

    pub fn trigger_interrupt<B: Bus32>(&mut self, bus: &mut B, vector: u8) {
        self.halted = false;
        let flags = self.flags;
        let cs = self.sregs[Self::CS];
        let ip = self.ip;
        self.push16(bus, flags);
        self.push16(bus, cs);
        self.push16(bus, ip);
        self.flags &= !(X86_IF | X86_TF);
        let ivt_addr = (vector as u32) * 4;
        self.ip = bus.read16_le(ivt_addr);
        self.sregs[Self::CS] = bus.read16_le(ivt_addr + 2);
    }

    #[inline]
    pub fn interrupts_enabled(&self) -> bool {
        (self.flags & X86_IF) != 0
    }

    #[inline]
    fn check_jcc(&self, cc: u8) -> bool {
        let cf = (self.flags & X86_CF) != 0;
        let zf = (self.flags & X86_ZF) != 0;
        let sf = (self.flags & X86_SF) != 0;
        let of = (self.flags & X86_OF) != 0;
        let pf = (self.flags & X86_PF) != 0;
        match cc & 15 {
            0x0 => of,
            0x1 => !of,
            0x2 => cf,
            0x3 => !cf,
            0x4 => zf,
            0x5 => !zf,
            0x6 => cf || zf,
            0x7 => !cf && !zf,
            0x8 => sf,
            0x9 => !sf,
            0xA => pf,
            0xB => !pf,
            0xC => sf != of,
            0xD => sf == of,
            0xE => zf || (sf != of),
            _ => !zf && (sf == of),
        }
    }

    fn shift8(&mut self, subop: u8, mut val: u8, count: u8) -> u8 {
        if count == 0 {
            return val;
        }
        let mut cf = (self.flags & X86_CF) != 0;
        let mut of = (self.flags & X86_OF) != 0;
        for _ in 0..count {
            match subop & 7 {
                0 => {
                    // ROL
                    cf = (val & 0x80) != 0;
                    val = (val << 1) | if cf { 1 } else { 0 };
                    of = ((val & 0x80) != 0) ^ cf;
                }
                1 => {
                    // ROR
                    cf = (val & 0x01) != 0;
                    val = (val >> 1) | if cf { 0x80 } else { 0 };
                    of = ((val >> 7) ^ ((val >> 6) & 1)) != 0;
                }
                2 => {
                    // RCL
                    let new_cf = (val & 0x80) != 0;
                    val = (val << 1) | if cf { 1 } else { 0 };
                    cf = new_cf;
                    of = ((val & 0x80) != 0) ^ cf;
                }
                3 => {
                    // RCR
                    let new_cf = (val & 0x01) != 0;
                    val = (val >> 1) | if cf { 0x80 } else { 0 };
                    cf = new_cf;
                    of = ((val >> 7) ^ ((val >> 6) & 1)) != 0;
                }
                4 | 6 => {
                    // SHL / SAL
                    cf = (val & 0x80) != 0;
                    val <<= 1;
                    of = ((val & 0x80) != 0) ^ cf;
                }
                5 => {
                    // SHR
                    of = (val & 0x80) != 0;
                    cf = (val & 0x01) != 0;
                    val >>= 1;
                }
                _ => {
                    // SAR
                    of = false;
                    cf = (val & 0x01) != 0;
                    val = ((val as i8) >> 1) as u8;
                }
            }
        }
        self.set_flag(X86_CF, cf);
        self.set_flag(X86_OF, of);
        if (subop & 7) >= 4 {
            self.update_psz8(val);
        }
        val
    }

    fn shift16(&mut self, subop: u8, mut val: u16, count: u8) -> u16 {
        if count == 0 {
            return val;
        }
        let mut cf = (self.flags & X86_CF) != 0;
        let mut of = (self.flags & X86_OF) != 0;
        for _ in 0..count {
            match subop & 7 {
                0 => {
                    cf = (val & 0x8000) != 0;
                    val = (val << 1) | if cf { 1 } else { 0 };
                    of = ((val & 0x8000) != 0) ^ cf;
                }
                1 => {
                    cf = (val & 0x0001) != 0;
                    val = (val >> 1) | if cf { 0x8000 } else { 0 };
                    of = ((val >> 15) ^ ((val >> 14) & 1)) != 0;
                }
                2 => {
                    let new_cf = (val & 0x8000) != 0;
                    val = (val << 1) | if cf { 1 } else { 0 };
                    cf = new_cf;
                    of = ((val & 0x8000) != 0) ^ cf;
                }
                3 => {
                    let new_cf = (val & 0x0001) != 0;
                    val = (val >> 1) | if cf { 0x8000 } else { 0 };
                    cf = new_cf;
                    of = ((val >> 15) ^ ((val >> 14) & 1)) != 0;
                }
                4 | 6 => {
                    cf = (val & 0x8000) != 0;
                    val <<= 1;
                    of = ((val & 0x8000) != 0) ^ cf;
                }
                5 => {
                    of = (val & 0x8000) != 0;
                    cf = (val & 0x0001) != 0;
                    val >>= 1;
                }
                _ => {
                    of = false;
                    cf = (val & 0x0001) != 0;
                    val = ((val as i16) >> 1) as u16;
                }
            }
        }
        self.set_flag(X86_CF, cf);
        self.set_flag(X86_OF, of);
        if (subop & 7) >= 4 {
            self.update_psz16(val);
        }
        val
    }

    pub fn step<B: Bus32 + IoPortBus>(&mut self, bus: &mut B) -> u32 {
        if self.halted {
            self.cycles += 2;
            return 2;
        }

        let mut seg_override: Option<SegReg> = None;
        let mut rep_mode: Option<u8> = None; // 0xF2 = REPNE, 0xF3 = REPE/REP
        let mut op;

        loop {
            op = self.fetch8(bus);
            match op {
                0x26 => seg_override = Some(SegReg::Es),
                0x2E => seg_override = Some(SegReg::Cs),
                0x36 => seg_override = Some(SegReg::Ss),
                0x3E => seg_override = Some(SegReg::Ds),
                0xF2 | 0xF3 => rep_mode = Some(op),
                0xF0 => {} // LOCK
                _ => break,
            }
        }

        let cyc = self.exec_opcode(bus, op, seg_override, rep_mode);
        self.cycles += cyc as u64;
        cyc
    }

    fn exec_opcode<B: Bus32 + IoPortBus>(
        &mut self,
        bus: &mut B,
        op: u8,
        seg_override: Option<SegReg>,
        rep_mode: Option<u8>,
    ) -> u32 {
        match op {
            // ALU r/m, r and r, r/m and AL/AX, imm
            0x00..=0x3D if (op & 7) <= 5 => {
                let alu_op = (op >> 3) & 7;
                let dir = (op & 2) != 0;
                let word = (op & 1) != 0;
                let imm_acc = (op & 4) != 0;
                if imm_acc {
                    if !word {
                        let imm = self.fetch8(bus);
                        let res = self.alu8(alu_op, self.get_reg8(0), imm);
                        if alu_op != 7 {
                            self.set_reg8(0, res);
                        }
                    } else {
                        let imm = self.fetch16(bus);
                        let res = self.alu16(alu_op, self.regs[Self::AX], imm);
                        if alu_op != 7 {
                            self.regs[Self::AX] = res;
                        }
                    }
                    4
                } else {
                    let m = self.decode_modrm(bus, seg_override);
                    if !word {
                        let a = self.read_rm8(bus, m);
                        let b = self.get_reg8(m.reg);
                        if dir {
                            let res = self.alu8(alu_op, b, a);
                            if alu_op != 7 {
                                self.set_reg8(m.reg, res);
                            }
                        } else {
                            let res = self.alu8(alu_op, a, b);
                            if alu_op != 7 {
                                self.write_rm8(bus, m, res);
                            }
                        }
                    } else {
                        let a = self.read_rm16(bus, m);
                        let b = self.regs[m.reg as usize];
                        if dir {
                            let res = self.alu16(alu_op, b, a);
                            if alu_op != 7 {
                                self.regs[m.reg as usize] = res;
                            }
                        } else {
                            let res = self.alu16(alu_op, a, b);
                            if alu_op != 7 {
                                self.write_rm16(bus, m, res);
                            }
                        }
                    }
                    if m.is_mem { 12 } else { 3 }
                }
            }

            // PUSH / POP Sreg
            0x06 | 0x0E | 0x16 | 0x1E => {
                let sr = (op >> 3) & 3;
                let v = self.sregs[sr as usize];
                self.push16(bus, v);
                10
            }
            0x07 | 0x0F | 0x17 | 0x1F => {
                let sr = (op >> 3) & 3;
                let v = self.pop16(bus);
                self.sregs[sr as usize] = v;
                8
            }

            // BCD / ASCII Adjust
            0x27 => {
                // DAA
                let old_al = self.get_reg8(0);
                let old_cf = (self.flags & X86_CF) != 0;
                let mut al = old_al;
                let mut cf = false;
                if (al & 0x0F) > 9 || (self.flags & X86_AF) != 0 {
                    let sum = al as u16 + 6;
                    al = sum as u8;
                    cf = old_cf || (sum > 0xFF);
                    self.flags |= X86_AF;
                } else {
                    self.flags &= !X86_AF;
                }
                if old_al > 0x99 || old_cf {
                    al = al.wrapping_add(0x60);
                    cf = true;
                }
                self.set_flag(X86_CF, cf);
                self.set_reg8(0, al);
                self.update_psz8(al);
                4
            }
            0x2F => {
                // DAS
                let old_al = self.get_reg8(0);
                let old_cf = (self.flags & X86_CF) != 0;
                let mut al = old_al;
                let mut cf = false;
                if (al & 0x0F) > 9 || (self.flags & X86_AF) != 0 {
                    let diff = al as i16 - 6;
                    al = diff as u8;
                    cf = old_cf || (diff < 0);
                    self.flags |= X86_AF;
                } else {
                    self.flags &= !X86_AF;
                }
                if old_al > 0x99 || old_cf {
                    al = al.wrapping_sub(0x60);
                    cf = true;
                }
                self.set_flag(X86_CF, cf);
                self.set_reg8(0, al);
                self.update_psz8(al);
                4
            }
            0x37 => {
                // AAA
                let al = self.get_reg8(0);
                if (al & 0x0F) > 9 || (self.flags & X86_AF) != 0 {
                    self.regs[Self::AX] = self.regs[Self::AX].wrapping_add(0x0106);
                    self.flags |= X86_AF | X86_CF;
                } else {
                    self.flags &= !(X86_AF | X86_CF);
                }
                let masked = self.get_reg8(0) & 0x0F;
                self.set_reg8(0, masked);
                4
            }
            0x3F => {
                // AAS
                let al = self.get_reg8(0);
                if (al & 0x0F) > 9 || (self.flags & X86_AF) != 0 {
                    self.regs[Self::AX] = self.regs[Self::AX].wrapping_sub(6);
                    let ah = self.get_reg8(4).wrapping_sub(1);
                    self.set_reg8(4, ah);
                    self.flags |= X86_AF | X86_CF;
                } else {
                    self.flags &= !(X86_AF | X86_CF);
                }
                let masked = self.get_reg8(0) & 0x0F;
                self.set_reg8(0, masked);
                4
            }

            // INC / DEC r16
            0x40..=0x47 => {
                let r = (op - 0x40) as usize;
                let cf_save = self.flags & X86_CF;
                self.regs[r] = self.alu16(0, self.regs[r], 1);
                self.flags = (self.flags & !X86_CF) | cf_save;
                2
            }
            0x48..=0x4F => {
                let r = (op - 0x48) as usize;
                let cf_save = self.flags & X86_CF;
                self.regs[r] = self.alu16(5, self.regs[r], 1);
                self.flags = (self.flags & !X86_CF) | cf_save;
                2
            }

            // PUSH / POP r16
            0x50..=0x57 => {
                let r = (op - 0x50) as usize;
                let val = if r == Self::SP {
                    self.regs[Self::SP].wrapping_sub(2)
                } else {
                    self.regs[r]
                };
                self.push16(bus, val);
                11
            }
            0x58..=0x5F => {
                let r = (op - 0x58) as usize;
                let v = self.pop16(bus);
                self.regs[r] = v;
                8
            }

            // Jcc short (0x60..0x6F mirror 0x70..0x7F on 8086)
            0x60..=0x7F => {
                let rel = self.fetch8(bus) as i8 as i16 as u16;
                if self.check_jcc(op) {
                    self.ip = self.ip.wrapping_add(rel);
                    16
                } else {
                    4
                }
            }

            // Immediate ALU Group 80..83
            0x80..=0x83 => {
                let m = self.decode_modrm(bus, seg_override);
                let alu_op = m.reg;
                if op == 0x80 || op == 0x82 {
                    let a = self.read_rm8(bus, m);
                    let imm = self.fetch8(bus);
                    let res = self.alu8(alu_op, a, imm);
                    if alu_op != 7 {
                        self.write_rm8(bus, m, res);
                    }
                } else {
                    let a = self.read_rm16(bus, m);
                    let imm = if op == 0x83 {
                        self.fetch8(bus) as i8 as i16 as u16
                    } else {
                        self.fetch16(bus)
                    };
                    let res = self.alu16(alu_op, a, imm);
                    if alu_op != 7 {
                        self.write_rm16(bus, m, res);
                    }
                }
                if m.is_mem { 16 } else { 4 }
            }

            // TEST r/m, r
            0x84 => {
                let m = self.decode_modrm(bus, seg_override);
                let a = self.read_rm8(bus, m);
                let b = self.get_reg8(m.reg);
                self.alu8(4, a, b);
                if m.is_mem { 9 } else { 3 }
            }
            0x85 => {
                let m = self.decode_modrm(bus, seg_override);
                let a = self.read_rm16(bus, m);
                let b = self.regs[m.reg as usize];
                self.alu16(4, a, b);
                if m.is_mem { 9 } else { 3 }
            }

            // XCHG r/m, r
            0x86 => {
                let m = self.decode_modrm(bus, seg_override);
                let a = self.read_rm8(bus, m);
                let b = self.get_reg8(m.reg);
                self.write_rm8(bus, m, b);
                self.set_reg8(m.reg, a);
                if m.is_mem { 17 } else { 4 }
            }
            0x87 => {
                let m = self.decode_modrm(bus, seg_override);
                let a = self.read_rm16(bus, m);
                let b = self.regs[m.reg as usize];
                self.write_rm16(bus, m, b);
                self.regs[m.reg as usize] = a;
                if m.is_mem { 17 } else { 4 }
            }

            // MOV r/m <-> r
            0x88 => {
                let m = self.decode_modrm(bus, seg_override);
                let v = self.get_reg8(m.reg);
                self.write_rm8(bus, m, v);
                if m.is_mem { 9 } else { 2 }
            }
            0x89 => {
                let m = self.decode_modrm(bus, seg_override);
                let v = self.regs[m.reg as usize];
                self.write_rm16(bus, m, v);
                if m.is_mem { 9 } else { 2 }
            }
            0x8A => {
                let m = self.decode_modrm(bus, seg_override);
                let v = self.read_rm8(bus, m);
                self.set_reg8(m.reg, v);
                if m.is_mem { 8 } else { 2 }
            }
            0x8B => {
                let m = self.decode_modrm(bus, seg_override);
                let v = self.read_rm16(bus, m);
                self.regs[m.reg as usize] = v;
                if m.is_mem { 8 } else { 2 }
            }

            // MOV r/m16, Sreg / LEA / MOV Sreg, r/m16 / POP r/m16
            0x8C => {
                let m = self.decode_modrm(bus, seg_override);
                let v = self.sregs[(m.reg & 3) as usize];
                self.write_rm16(bus, m, v);
                if m.is_mem { 9 } else { 2 }
            }
            0x8D => {
                let m = self.decode_modrm(bus, seg_override);
                self.regs[m.reg as usize] = m.offset;
                2
            }
            0x8E => {
                let m = self.decode_modrm(bus, seg_override);
                let v = self.read_rm16(bus, m);
                self.sregs[(m.reg & 3) as usize] = v;
                if m.is_mem { 8 } else { 2 }
            }
            0x8F => {
                let m = self.decode_modrm(bus, seg_override);
                let v = self.pop16(bus);
                self.write_rm16(bus, m, v);
                if m.is_mem { 17 } else { 8 }
            }

            // NOP / XCHG AX, r16
            0x90 => 3,
            0x91..=0x97 => {
                let r = (op - 0x90) as usize;
                self.regs.swap(Self::AX, r);
                3
            }
            0x98 => {
                // CBW
                let al = self.get_reg8(0) as i8 as i16 as u16;
                self.regs[Self::AX] = al;
                2
            }
            0x99 => {
                // CWD
                self.regs[Self::DX] = if (self.regs[Self::AX] & 0x8000) != 0 {
                    0xFFFF
                } else {
                    0x0000
                };
                5
            }
            0x9A => {
                // CALL far ptr16:16
                let off = self.fetch16(bus);
                let seg = self.fetch16(bus);
                let old_cs = self.sregs[Self::CS];
                let old_ip = self.ip;
                self.push16(bus, old_cs);
                self.push16(bus, old_ip);
                self.sregs[Self::CS] = seg;
                self.ip = off;
                28
            }
            0x9B => 3, // WAIT
            0x9C => {
                let f = self.flags | 0xF002;
                self.push16(bus, f);
                10
            }
            0x9D => {
                let f = self.pop16(bus);
                self.flags = (f & 0x0FD5) | 0x0002;
                8
            }
            0x9E => {
                let ah = self.get_reg8(4) as u16;
                self.flags = (self.flags & 0xFF00) | (ah & 0xD5) | 0x02;
                4
            }
            0x9F => {
                self.set_reg8(4, (self.flags & 0xFF) as u8);
                4
            }

            // MOV AL/AX <-> [moffs16]
            0xA0 => {
                let off = self.fetch16(bus);
                let v = self.read_mem8(bus, seg_override.unwrap_or(SegReg::Ds), off);
                self.set_reg8(0, v);
                10
            }
            0xA1 => {
                let off = self.fetch16(bus);
                let v = self.read_mem16(bus, seg_override.unwrap_or(SegReg::Ds), off);
                self.regs[Self::AX] = v;
                10
            }
            0xA2 => {
                let off = self.fetch16(bus);
                let al = self.get_reg8(0);
                self.write_mem8(bus, seg_override.unwrap_or(SegReg::Ds), off, al);
                10
            }
            0xA3 => {
                let off = self.fetch16(bus);
                self.write_mem16(
                    bus,
                    seg_override.unwrap_or(SegReg::Ds),
                    off,
                    self.regs[Self::AX],
                );
                10
            }

            // String operations
            0xA4..=0xA7 | 0xAA..=0xAF => self.exec_string(bus, op, seg_override, rep_mode),

            // TEST AL/AX, imm
            0xA8 => {
                let imm = self.fetch8(bus);
                let al = self.get_reg8(0);
                self.alu8(4, al, imm);
                4
            }
            0xA9 => {
                let imm = self.fetch16(bus);
                let ax = self.regs[Self::AX];
                self.alu16(4, ax, imm);
                4
            }

            // MOV r8/r16, imm
            0xB0..=0xB7 => {
                let imm = self.fetch8(bus);
                self.set_reg8(op - 0xB0, imm);
                4
            }
            0xB8..=0xBF => {
                let imm = self.fetch16(bus);
                self.regs[(op - 0xB8) as usize] = imm;
                4
            }

            // RET near / LES / LDS / MOV r/m, imm / RETF / INT / IRET
            0xC0 | 0xC2 => {
                let imm = self.fetch16(bus);
                self.ip = self.pop16(bus);
                self.regs[Self::SP] = self.regs[Self::SP].wrapping_add(imm);
                20
            }
            0xC1 | 0xC3 => {
                self.ip = self.pop16(bus);
                16
            }
            0xC4 => {
                let m = self.decode_modrm(bus, seg_override);
                let off = self.read_mem16(bus, m.seg, m.offset);
                let seg = self.read_mem16(bus, m.seg, m.offset.wrapping_add(2));
                self.regs[m.reg as usize] = off;
                self.sregs[Self::ES] = seg;
                16
            }
            0xC5 => {
                let m = self.decode_modrm(bus, seg_override);
                let off = self.read_mem16(bus, m.seg, m.offset);
                let seg = self.read_mem16(bus, m.seg, m.offset.wrapping_add(2));
                self.regs[m.reg as usize] = off;
                self.sregs[Self::DS] = seg;
                16
            }
            0xC6 => {
                let m = self.decode_modrm(bus, seg_override);
                let imm = self.fetch8(bus);
                self.write_rm8(bus, m, imm);
                if m.is_mem { 10 } else { 4 }
            }
            0xC7 => {
                let m = self.decode_modrm(bus, seg_override);
                let imm = self.fetch16(bus);
                self.write_rm16(bus, m, imm);
                if m.is_mem { 10 } else { 4 }
            }
            0xC8 | 0xCA => {
                let imm = self.fetch16(bus);
                self.ip = self.pop16(bus);
                self.sregs[Self::CS] = self.pop16(bus);
                self.regs[Self::SP] = self.regs[Self::SP].wrapping_add(imm);
                24
            }
            0xC9 | 0xCB => {
                self.ip = self.pop16(bus);
                self.sregs[Self::CS] = self.pop16(bus);
                22
            }
            0xCC => {
                self.trigger_interrupt(bus, 3);
                51
            }
            0xCD => {
                let vec = self.fetch8(bus);
                self.trigger_interrupt(bus, vec);
                51
            }
            0xCE if (self.flags & X86_OF) != 0 => {
                self.trigger_interrupt(bus, 4);
                53
            }
            0xCE => 4,
            0xCF => {
                self.ip = self.pop16(bus);
                self.sregs[Self::CS] = self.pop16(bus);
                let f = self.pop16(bus);
                self.flags = (f & 0x0FD5) | 0x0002;
                24
            }

            // Shifts & Rotates
            0xD0..=0xD3 => {
                let m = self.decode_modrm(bus, seg_override);
                let count = if (op & 2) != 0 {
                    self.get_reg8(1)
                } else {
                    1
                };
                if (op & 1) == 0 {
                    let v = self.read_rm8(bus, m);
                    let res = self.shift8(m.reg, v, count);
                    self.write_rm8(bus, m, res);
                } else {
                    let v = self.read_rm16(bus, m);
                    let res = self.shift16(m.reg, v, count);
                    self.write_rm16(bus, m, res);
                }
                if m.is_mem { 16 } else { 4 }
            }
            0xD4 => {
                // AAM
                let base = self.fetch8(bus);
                let al = self.get_reg8(0);
                if let (Some(q), Some(rem)) = (al.checked_div(base), al.checked_rem(base)) {
                    self.set_reg8(4, q);
                    self.set_reg8(0, rem);
                    self.update_psz8(rem);
                } else {
                    self.trigger_interrupt(bus, 0);
                }
                83
            }
            0xD5 => {
                // AAD
                let base = self.fetch8(bus);
                let al = self.get_reg8(0);
                let ah = self.get_reg8(4);
                let res = al.wrapping_add(ah.wrapping_mul(base));
                self.set_reg8(0, res);
                self.set_reg8(4, 0);
                self.update_psz8(res);
                60
            }
            0xD6 => {
                // SALC
                let val = if (self.flags & X86_CF) != 0 { 0xFF } else { 0x00 };
                self.set_reg8(0, val);
                4
            }
            0xD7 => {
                // XLAT
                let off = self.regs[Self::BX].wrapping_add(self.get_reg8(0) as u16);
                let v = self.read_mem8(bus, seg_override.unwrap_or(SegReg::Ds), off);
                self.set_reg8(0, v);
                11
            }
            0xD8..=0xDF => {
                // x87 FPU escape (no 8087 installed, consume ModR/M cleanly)
                let _ = self.decode_modrm(bus, seg_override);
                4
            }

            // Loops & I/O & Jumps
            0xE0..=0xE2 => {
                let rel = self.fetch8(bus) as i8 as i16 as u16;
                self.regs[Self::CX] = self.regs[Self::CX].wrapping_sub(1);
                let cx = self.regs[Self::CX];
                let zf = (self.flags & X86_ZF) != 0;
                let cond = match op {
                    0xE0 => cx != 0 && !zf,
                    0xE1 => cx != 0 && zf,
                    _ => cx != 0,
                };
                if cond {
                    self.ip = self.ip.wrapping_add(rel);
                    17
                } else {
                    5
                }
            }
            0xE3 => {
                let rel = self.fetch8(bus) as i8 as i16 as u16;
                if self.regs[Self::CX] == 0 {
                    self.ip = self.ip.wrapping_add(rel);
                    18
                } else {
                    6
                }
            }
            0xE4 => {
                let p = self.fetch8(bus) as u16;
                let v = bus.port_in8(p);
                self.set_reg8(0, v);
                10
            }
            0xE5 => {
                let p = self.fetch8(bus) as u16;
                self.regs[Self::AX] = bus.port_in16(p);
                10
            }
            0xE6 => {
                let p = self.fetch8(bus) as u16;
                bus.port_out8(p, self.get_reg8(0));
                10
            }
            0xE7 => {
                let p = self.fetch8(bus) as u16;
                bus.port_out16(p, self.regs[Self::AX]);
                10
            }
            0xE8 => {
                let rel = self.fetch16(bus);
                let ret = self.ip;
                self.push16(bus, ret);
                self.ip = self.ip.wrapping_add(rel);
                19
            }
            0xE9 => {
                let rel = self.fetch16(bus);
                self.ip = self.ip.wrapping_add(rel);
                15
            }
            0xEA => {
                let off = self.fetch16(bus);
                let seg = self.fetch16(bus);
                self.sregs[Self::CS] = seg;
                self.ip = off;
                15
            }
            0xEB => {
                let rel = self.fetch8(bus) as i8 as i16 as u16;
                self.ip = self.ip.wrapping_add(rel);
                15
            }
            0xEC => {
                let p = self.regs[Self::DX];
                let v = bus.port_in8(p);
                self.set_reg8(0, v);
                8
            }
            0xED => {
                let p = self.regs[Self::DX];
                self.regs[Self::AX] = bus.port_in16(p);
                8
            }
            0xEE => {
                bus.port_out8(self.regs[Self::DX], self.get_reg8(0));
                8
            }
            0xEF => {
                bus.port_out16(self.regs[Self::DX], self.regs[Self::AX]);
                8
            }

            // Flags & HLT
            0xF4 => {
                self.halted = true;
                2
            }
            0xF5 => {
                self.flags ^= X86_CF;
                2
            }
            0xF8 => {
                self.flags &= !X86_CF;
                2
            }
            0xF9 => {
                self.flags |= X86_CF;
                2
            }
            0xFA => {
                self.flags &= !X86_IF;
                2
            }
            0xFB => {
                self.flags |= X86_IF;
                2
            }
            0xFC => {
                self.flags &= !X86_DF;
                2
            }
            0xFD => {
                self.flags |= X86_DF;
                2
            }

            // Group 0xF6 / 0xF7 (TEST, NOT, NEG, MUL, IMUL, DIV, IDIV)
            0xF6 => {
                let m = self.decode_modrm(bus, seg_override);
                let v = self.read_rm8(bus, m);
                match m.reg {
                    0 | 1 => {
                        let imm = self.fetch8(bus);
                        self.alu8(4, v, imm);
                    }
                    2 => self.write_rm8(bus, m, !v),
                    3 => {
                        let res = self.alu8(5, 0, v);
                        self.write_rm8(bus, m, res);
                        self.set_flag(X86_CF, v != 0);
                    }
                    4 => {
                        let prod = (self.get_reg8(0) as u16) * (v as u16);
                        self.regs[Self::AX] = prod;
                        let hi = (prod >> 8) != 0;
                        self.set_flag(X86_CF, hi);
                        self.set_flag(X86_OF, hi);
                    }
                    5 => {
                        let prod = ((self.get_reg8(0) as i8 as i16) * (v as i8 as i16)) as u16;
                        self.regs[Self::AX] = prod;
                        let ext = (prod as i8 as i16 as u16) != prod;
                        self.set_flag(X86_CF, ext);
                        self.set_flag(X86_OF, ext);
                    }
                    6 => {
                        if v == 0 {
                            self.trigger_interrupt(bus, 0);
                        } else {
                            let q = self.regs[Self::AX] / (v as u16);
                            let r = self.regs[Self::AX] % (v as u16);
                            if q > 0xFF {
                                self.trigger_interrupt(bus, 0);
                            } else {
                                self.set_reg8(0, q as u8);
                                self.set_reg8(4, r as u8);
                            }
                        }
                    }
                    _ => {
                        let d = v as i8 as i16;
                        if d == 0 {
                            self.trigger_interrupt(bus, 0);
                        } else {
                            let ax = self.regs[Self::AX] as i16;
                            let q = ax / d;
                            let r = ax % d;
                            if !(-128..=127).contains(&q) {
                                self.trigger_interrupt(bus, 0);
                            } else {
                                self.set_reg8(0, q as u8);
                                self.set_reg8(4, r as u8);
                            }
                        }
                    }
                }
                10
            }
            0xF7 => {
                let m = self.decode_modrm(bus, seg_override);
                let v = self.read_rm16(bus, m);
                match m.reg {
                    0 | 1 => {
                        let imm = self.fetch16(bus);
                        self.alu16(4, v, imm);
                    }
                    2 => self.write_rm16(bus, m, !v),
                    3 => {
                        let res = self.alu16(5, 0, v);
                        self.write_rm16(bus, m, res);
                        self.set_flag(X86_CF, v != 0);
                    }
                    4 => {
                        let prod = (self.regs[Self::AX] as u32) * (v as u32);
                        self.regs[Self::AX] = (prod & 0xFFFF) as u16;
                        self.regs[Self::DX] = (prod >> 16) as u16;
                        let hi = self.regs[Self::DX] != 0;
                        self.set_flag(X86_CF, hi);
                        self.set_flag(X86_OF, hi);
                    }
                    5 => {
                        let prod =
                            (self.regs[Self::AX] as i16 as i32) * (v as i16 as i32);
                        self.regs[Self::AX] = (prod & 0xFFFF) as u16;
                        self.regs[Self::DX] = ((prod >> 16) & 0xFFFF) as u16;
                        let ext = (self.regs[Self::AX] as i16 as i32) != prod;
                        self.set_flag(X86_CF, ext);
                        self.set_flag(X86_OF, ext);
                    }
                    6 => {
                        if v == 0 {
                            self.trigger_interrupt(bus, 0);
                        } else {
                            let num = ((self.regs[Self::DX] as u32) << 16)
                                | (self.regs[Self::AX] as u32);
                            let q = num / (v as u32);
                            let r = num % (v as u32);
                            if q > 0xFFFF {
                                self.trigger_interrupt(bus, 0);
                            } else {
                                self.regs[Self::AX] = q as u16;
                                self.regs[Self::DX] = r as u16;
                            }
                        }
                    }
                    _ => {
                        let d = v as i16 as i32;
                        if d == 0 {
                            self.trigger_interrupt(bus, 0);
                        } else {
                            let num = (((self.regs[Self::DX] as u32) << 16)
                                | (self.regs[Self::AX] as u32)) as i32;
                            let q = num / d;
                            let r = num % d;
                            if !(-32768..=32767).contains(&q) {
                                self.trigger_interrupt(bus, 0);
                            } else {
                                self.regs[Self::AX] = q as u16;
                                self.regs[Self::DX] = r as u16;
                            }
                        }
                    }
                }
                14
            }

            // Group 0xFE / 0xFF
            0xFE => {
                let m = self.decode_modrm(bus, seg_override);
                let v = self.read_rm8(bus, m);
                let cf_save = self.flags & X86_CF;
                let res = if m.reg == 0 {
                    self.alu8(0, v, 1)
                } else {
                    self.alu8(5, v, 1)
                };
                self.flags = (self.flags & !X86_CF) | cf_save;
                self.write_rm8(bus, m, res);
                15
            }
            0xFF => {
                let m = self.decode_modrm(bus, seg_override);
                match m.reg {
                    0 | 1 => {
                        let v = self.read_rm16(bus, m);
                        let cf_save = self.flags & X86_CF;
                        let res = if m.reg == 0 {
                            self.alu16(0, v, 1)
                        } else {
                            self.alu16(5, v, 1)
                        };
                        self.flags = (self.flags & !X86_CF) | cf_save;
                        self.write_rm16(bus, m, res);
                    }
                    2 => {
                        let target = self.read_rm16(bus, m);
                        let ret = self.ip;
                        self.push16(bus, ret);
                        self.ip = target;
                    }
                    3 => {
                        let off = self.read_mem16(bus, m.seg, m.offset);
                        let seg = self.read_mem16(bus, m.seg, m.offset.wrapping_add(2));
                        let old_cs = self.sregs[Self::CS];
                        let old_ip = self.ip;
                        self.push16(bus, old_cs);
                        self.push16(bus, old_ip);
                        self.sregs[Self::CS] = seg;
                        self.ip = off;
                    }
                    4 => {
                        self.ip = self.read_rm16(bus, m);
                    }
                    5 => {
                        let off = self.read_mem16(bus, m.seg, m.offset);
                        let seg = self.read_mem16(bus, m.seg, m.offset.wrapping_add(2));
                        self.sregs[Self::CS] = seg;
                        self.ip = off;
                    }
                    6 | 7 => {
                        let v = self.read_rm16(bus, m);
                        self.push16(bus, v);
                    }
                    _ => {}
                }
                16
            }

            _ => 4,
        }
    }

    fn exec_string<B: Bus32>(
        &mut self,
        bus: &mut B,
        op: u8,
        seg_override: Option<SegReg>,
        rep_mode: Option<u8>,
    ) -> u32 {
        let src_seg = seg_override.unwrap_or(SegReg::Ds);
        let word = (op & 1) != 0;
        let step: u16 = if (self.flags & X86_DF) != 0 {
            if word { 0xFFFE } else { 0xFFFF }
        } else if word {
            2
        } else {
            1
        };

        let mut total_cyc = 0u32;
        let has_rep = rep_mode.is_some();
        loop {
            if has_rep && self.regs[Self::CX] == 0 {
                break;
            }
            match op {
                0xA4 => {
                    let v = self.read_mem8(bus, src_seg, self.regs[Self::SI]);
                    self.write_mem8(bus, SegReg::Es, self.regs[Self::DI], v);
                    self.regs[Self::SI] = self.regs[Self::SI].wrapping_add(step);
                    self.regs[Self::DI] = self.regs[Self::DI].wrapping_add(step);
                }
                0xA5 => {
                    let v = self.read_mem16(bus, src_seg, self.regs[Self::SI]);
                    self.write_mem16(bus, SegReg::Es, self.regs[Self::DI], v);
                    self.regs[Self::SI] = self.regs[Self::SI].wrapping_add(step);
                    self.regs[Self::DI] = self.regs[Self::DI].wrapping_add(step);
                }
                0xA6 => {
                    let a = self.read_mem8(bus, src_seg, self.regs[Self::SI]);
                    let b = self.read_mem8(bus, SegReg::Es, self.regs[Self::DI]);
                    self.alu8(5, a, b);
                    self.regs[Self::SI] = self.regs[Self::SI].wrapping_add(step);
                    self.regs[Self::DI] = self.regs[Self::DI].wrapping_add(step);
                }
                0xA7 => {
                    let a = self.read_mem16(bus, src_seg, self.regs[Self::SI]);
                    let b = self.read_mem16(bus, SegReg::Es, self.regs[Self::DI]);
                    self.alu16(5, a, b);
                    self.regs[Self::SI] = self.regs[Self::SI].wrapping_add(step);
                    self.regs[Self::DI] = self.regs[Self::DI].wrapping_add(step);
                }
                0xAA => {
                    let al = self.get_reg8(0);
                    self.write_mem8(bus, SegReg::Es, self.regs[Self::DI], al);
                    self.regs[Self::DI] = self.regs[Self::DI].wrapping_add(step);
                }
                0xAB => {
                    self.write_mem16(bus, SegReg::Es, self.regs[Self::DI], self.regs[Self::AX]);
                    self.regs[Self::DI] = self.regs[Self::DI].wrapping_add(step);
                }
                0xAC => {
                    let v = self.read_mem8(bus, src_seg, self.regs[Self::SI]);
                    self.set_reg8(0, v);
                    self.regs[Self::SI] = self.regs[Self::SI].wrapping_add(step);
                }
                0xAD => {
                    let v = self.read_mem16(bus, src_seg, self.regs[Self::SI]);
                    self.regs[Self::AX] = v;
                    self.regs[Self::SI] = self.regs[Self::SI].wrapping_add(step);
                }
                0xAE => {
                    let al = self.get_reg8(0);
                    let b = self.read_mem8(bus, SegReg::Es, self.regs[Self::DI]);
                    self.alu8(5, al, b);
                    self.regs[Self::DI] = self.regs[Self::DI].wrapping_add(step);
                }
                0xAF => {
                    let ax = self.regs[Self::AX];
                    let b = self.read_mem16(bus, SegReg::Es, self.regs[Self::DI]);
                    self.alu16(5, ax, b);
                    self.regs[Self::DI] = self.regs[Self::DI].wrapping_add(step);
                }
                _ => {}
            }
            total_cyc += 12;
            if !has_rep {
                break;
            }
            self.regs[Self::CX] = self.regs[Self::CX].wrapping_sub(1);
            if op == 0xA6 || op == 0xA7 || op == 0xAE || op == 0xAF {
                let zf = (self.flags & X86_ZF) != 0;
                if rep_mode == Some(0xF3) && !zf {
                    break;
                }
                if rep_mode == Some(0xF2) && zf {
                    break;
                }
            }
        }
        total_cyc.max(9)
    }

    pub fn debug_state(&self) -> CpuDebugState {
        let flags = format!(
            "{}{}{}{}{}{}{}{}{}",
            if (self.flags & X86_OF) != 0 { 'O' } else { '-' },
            if (self.flags & X86_DF) != 0 { 'D' } else { '-' },
            if (self.flags & X86_IF) != 0 { 'I' } else { '-' },
            if (self.flags & X86_TF) != 0 { 'T' } else { '-' },
            if (self.flags & X86_SF) != 0 { 'S' } else { '-' },
            if (self.flags & X86_ZF) != 0 { 'Z' } else { '-' },
            if (self.flags & X86_AF) != 0 { 'A' } else { '-' },
            if (self.flags & X86_PF) != 0 { 'P' } else { '-' },
            if (self.flags & X86_CF) != 0 { 'C' } else { '-' },
        );
        CpuDebugState {
            arch: "Intel 8088/8086",
            pc: Self::phys(self.sregs[Self::CS], self.ip),
            sp: Self::phys(self.sregs[Self::SS], self.regs[Self::SP]),
            flags,
            registers: vec![
                ("AX".into(), format!("{:04X}", self.regs[Self::AX])),
                ("BX".into(), format!("{:04X}", self.regs[Self::BX])),
                ("CX".into(), format!("{:04X}", self.regs[Self::CX])),
                ("DX".into(), format!("{:04X}", self.regs[Self::DX])),
                ("SI".into(), format!("{:04X}", self.regs[Self::SI])),
                ("DI".into(), format!("{:04X}", self.regs[Self::DI])),
                ("BP".into(), format!("{:04X}", self.regs[Self::BP])),
                ("SP".into(), format!("{:04X}", self.regs[Self::SP])),
                ("CS".into(), format!("{:04X}", self.sregs[Self::CS])),
                ("DS".into(), format!("{:04X}", self.sregs[Self::DS])),
                ("ES".into(), format!("{:04X}", self.sregs[Self::ES])),
                ("IP".into(), format!("{:04X}", self.ip)),
            ],
            cycles: self.cycles,
            halted: self.halted,
        }
    }

    pub fn disassemble<B: Bus32>(&self, bus: &B, start_phys: u32, count: usize) -> Vec<DisassembledInstruction> {
        let mut out = Vec::with_capacity(count);
        let mut addr = start_phys & 0xFFFFF;
        for _ in 0..count {
            let (len, mnemonic) = disasm_8086_one(bus, addr);
            let mut bytes = Vec::with_capacity(len);
            for i in 0..len {
                bytes.push(bus.peek8((addr + i as u32) & 0xFFFFF));
            }
            out.push(DisassembledInstruction {
                address: addr,
                bytes,
                mnemonic,
            });
            addr = (addr + len as u32) & 0xFFFFF;
        }
        out
    }
}

fn disasm_8086_one<B: Bus32>(bus: &B, addr: u32) -> (usize, String) {
    let op = bus.peek8(addr);
    let b1 = bus.peek8(addr + 1);
    let b2 = bus.peek8(addr + 2);
    let b3 = bus.peek8(addr + 3);
    let b4 = bus.peek8(addr + 4);
    let imm16 = (b1 as u16) | ((b2 as u16) << 8);
    let far_seg = (b3 as u16) | ((b4 as u16) << 8);
    match op {
        0x90 => (1, "NOP".into()),
        0xF4 => (1, "HLT".into()),
        0xFA => (1, "CLI".into()),
        0xFB => (1, "STI".into()),
        0xFC => (1, "CLD".into()),
        0xFD => (1, "STD".into()),
        0xC3 => (1, "RET".into()),
        0xCB => (1, "RETF".into()),
        0xCF => (1, "IRET".into()),
        0xCD => (2, format!("INT {b1:02X}h")),
        0xE4 => (2, format!("IN AL,{b1:02X}h")),
        0xE6 => (2, format!("OUT {b1:02X}h,AL")),
        0xEC => (1, "IN AL,DX".into()),
        0xEE => (1, "OUT DX,AL".into()),
        0xE8 => (3, format!("CALL +{imm16:04X}")),
        0xE9 => (3, format!("JMP +{imm16:04X}")),
        0xEA => (5, format!("JMP {far_seg:04X}:{imm16:04X}")),
        0x9A => (5, format!("CALL {far_seg:04X}:{imm16:04X}")),
        0xEB => (2, format!("JMP short {:+}", b1 as i8)),
        0xB0..=0xB7 => {
            const R8: [&str; 8] = ["AL", "CL", "DL", "BL", "AH", "CH", "DH", "BH"];
            (2, format!("MOV {},{b1:02X}h", R8[(op - 0xB0) as usize]))
        }
        0xB8..=0xBF => {
            const R16: [&str; 8] = ["AX", "CX", "DX", "BX", "SP", "BP", "SI", "DI"];
            (3, format!("MOV {},{imm16:04X}h", R16[(op - 0xB8) as usize]))
        }
        0x50..=0x57 => {
            const R16: [&str; 8] = ["AX", "CX", "DX", "BX", "SP", "BP", "SI", "DI"];
            (1, format!("PUSH {}", R16[(op - 0x50) as usize]))
        }
        0x58..=0x5F => {
            const R16: [&str; 8] = ["AX", "CX", "DX", "BX", "SP", "BP", "SI", "DI"];
            (1, format!("POP {}", R16[(op - 0x58) as usize]))
        }
        0x70..=0x7F => (2, format!("Jcc {:+}", b1 as i8)),
        0x80..=0x83 | 0x88..=0x8F => {
            let md = b1 >> 6;
            let rm = b1 & 7;
            let extra = if md == 1 {
                1
            } else if md == 2 || (md == 0 && rm == 6) {
                2
            } else {
                0
            };
            let imm_len = if op == 0x81 { 2 } else if op <= 0x83 { 1 } else { 0 };
            (2 + extra + imm_len, format!("OP {op:02X} {b1:02X}"))
        }
        _ => (1, format!("DB {op:02X}h")),
    }
}
