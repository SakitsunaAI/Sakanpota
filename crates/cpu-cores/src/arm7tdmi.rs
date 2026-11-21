use emu_core::{Bus32, CpuDebugState, DisassembledInstruction};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Bank {
    Usr = 0,
    Fiq = 1,
    Irq = 2,
    Svc = 3,
    Abt = 4,
    Und = 5,
}

impl Bank {
    #[inline]
    fn from_mode(mode: u32) -> Self {
        match mode & 0x1F {
            0x11 => Self::Fiq,
            0x12 => Self::Irq,
            0x13 => Self::Svc,
            0x17 => Self::Abt,
            0x1B => Self::Und,
            _ => Self::Usr,
        }
    }
}

#[derive(Debug, Clone)]
pub struct Arm7tdmi {
    /// Active visible registers R0..R15 (R13=SP, R14=LR, R15=PC).
    pub regs: [u32; 16],
    /// Current Program Status Register (NZCV flags + I, F, T, M[4:0]).
    pub cpsr: u32,
    /// Saved User/System R8..R12 when in FIQ mode (or FIQ R8..R12 when not).
    usr_8_12: [u32; 5],
    fiq_8_12: [u32; 5],
    /// Banked SP/LR for [Usr/Sys, Fiq, Irq, Svc, Abt, Und].
    sp_lr: [[u32; 2]; 6],
    /// Saved PSR for [Fiq, Irq, Svc, Abt, Und] indexed by `Bank as usize - 1`.
    spsr: [u32; 5],
    /// Suppress IRQ between first and second halfwords of a Thumb BL sequence.
    thumb_bl_pending: bool,
    pub halted: bool,
    pub cycles: u64,
}

impl Default for Arm7tdmi {
    fn default() -> Self {
        Self::new()
    }
}

impl Arm7tdmi {
    pub const FLAG_N: u32 = 1 << 31;
    pub const FLAG_Z: u32 = 1 << 30;
    pub const FLAG_C: u32 = 1 << 29;
    pub const FLAG_V: u32 = 1 << 28;
    pub const FLAG_I: u32 = 1 << 7;
    pub const FLAG_F: u32 = 1 << 6;
    pub const FLAG_T: u32 = 1 << 5;

    pub const MODE_USR: u32 = 0x10;
    pub const MODE_FIQ: u32 = 0x11;
    pub const MODE_IRQ: u32 = 0x12;
    pub const MODE_SVC: u32 = 0x13;
    pub const MODE_ABT: u32 = 0x17;
    pub const MODE_UND: u32 = 0x1B;
    pub const MODE_SYS: u32 = 0x1F;

    pub fn new() -> Self {
        let mut cpu = Self {
            regs: [0; 16],
            cpsr: Self::MODE_SVC | Self::FLAG_I | Self::FLAG_F,
            usr_8_12: [0; 5],
            fiq_8_12: [0; 5],
            sp_lr: [[0; 2]; 6],
            spsr: [0; 5],
            thumb_bl_pending: false,
            halted: false,
            cycles: 0,
        };
        cpu.reset();
        cpu
    }

    pub fn reset(&mut self) {
        self.regs = [0; 16];
        self.usr_8_12 = [0; 5];
        self.fiq_8_12 = [0; 5];
        self.sp_lr = [[0; 2]; 6];
        self.spsr = [0; 5];
        self.cpsr = Self::MODE_SVC | Self::FLAG_I | Self::FLAG_F;
        self.thumb_bl_pending = false;
        self.halted = false;
        self.cycles = 0;
    }

    #[inline]
    pub fn pc(&self) -> u32 {
        self.regs[15]
    }

    #[inline]
    pub fn is_thumb(&self) -> bool {
        (self.cpsr & Self::FLAG_T) != 0
    }

    #[inline]
    pub fn irq_disabled(&self) -> bool {
        (self.cpsr & Self::FLAG_I) != 0
    }

    #[inline]
    pub fn fiq_disabled(&self) -> bool {
        (self.cpsr & Self::FLAG_F) != 0
    }

    #[inline]
    fn current_bank(&self) -> Bank {
        Bank::from_mode(self.cpsr)
    }

    fn set_cpsr(&mut self, new_cpsr: u32) {
        let old_bank = Bank::from_mode(self.cpsr);
        let new_bank = Bank::from_mode(new_cpsr);

        if old_bank != new_bank {
            if old_bank == Bank::Fiq {
                self.fiq_8_12.copy_from_slice(&self.regs[8..13]);
            } else {
                self.usr_8_12.copy_from_slice(&self.regs[8..13]);
            }
            self.sp_lr[old_bank as usize][0] = self.regs[13];
            self.sp_lr[old_bank as usize][1] = self.regs[14];

            if new_bank == Bank::Fiq {
                self.regs[8..13].copy_from_slice(&self.fiq_8_12);
            } else {
                self.regs[8..13].copy_from_slice(&self.usr_8_12);
            }
            self.regs[13] = self.sp_lr[new_bank as usize][0];
            self.regs[14] = self.sp_lr[new_bank as usize][1];
        }

        self.cpsr = new_cpsr;
    }

    fn current_spsr(&self) -> u32 {
        match self.current_bank() {
            Bank::Usr => self.cpsr,
            b => self.spsr[b as usize - 1],
        }
    }

    fn set_current_spsr(&mut self, val: u32) {
        match self.current_bank() {
            Bank::Usr => {}
            b => self.spsr[b as usize - 1] = val,
        }
    }

    fn read_usr_reg(&self, idx: usize) -> u32 {
        let cur_bank = self.current_bank();
        if (8..=12).contains(&idx) && cur_bank == Bank::Fiq {
            self.usr_8_12[idx - 8]
        } else if (idx == 13 || idx == 14) && cur_bank != Bank::Usr {
            self.sp_lr[Bank::Usr as usize][idx - 13]
        } else {
            self.regs[idx]
        }
    }

    fn write_usr_reg(&mut self, idx: usize, val: u32) {
        let cur_bank = self.current_bank();
        if (8..=12).contains(&idx) && cur_bank == Bank::Fiq {
            self.usr_8_12[idx - 8] = val;
        } else if (idx == 13 || idx == 14) && cur_bank != Bank::Usr {
            self.sp_lr[Bank::Usr as usize][idx - 13] = val;
        } else {
            self.regs[idx] = val;
        }
    }

    fn enter_exception(&mut self, mode: u32, vector: u32, ret_addr: u32, disable_fiq: bool) {
        let saved_cpsr = self.cpsr;
        let bank = Bank::from_mode(mode);
        let mut next_cpsr = (saved_cpsr & !0x1F) | (mode & 0x1F) | Self::FLAG_I;
        next_cpsr &= !Self::FLAG_T;
        if disable_fiq {
            next_cpsr |= Self::FLAG_F;
        }
        self.set_cpsr(next_cpsr);
        if bank != Bank::Usr {
            self.spsr[bank as usize - 1] = saved_cpsr;
        }
        self.regs[14] = ret_addr;
        self.regs[15] = vector;
        self.thumb_bl_pending = false;
        self.halted = false;
    }

    /// Trigger a hardware IRQ if `CPSR.I == 0`. Returns `true` if taken.
    pub fn trigger_irq(&mut self) -> bool {
        if self.irq_disabled() || self.thumb_bl_pending {
            return false;
        }
        // Hardware stores PC_next_instr + 4 in LR_irq so `SUBS PC, LR, #4` returns to PC.
        let lr = self.regs[15].wrapping_add(4);
        self.enter_exception(Self::MODE_IRQ, 0x0000_0018, lr, false);
        self.cycles += 3;
        true
    }

    /// Trigger a hardware FIQ if `CPSR.F == 0`. Returns `true` if taken.
    pub fn trigger_fiq(&mut self) -> bool {
        if self.fiq_disabled() || self.thumb_bl_pending {
            return false;
        }
        let lr = self.regs[15].wrapping_add(4);
        self.enter_exception(Self::MODE_FIQ, 0x0000_001C, lr, true);
        self.cycles += 3;
        true
    }

    #[inline]
    fn cond_passed(&self, cond: u32) -> bool {
        let n = (self.cpsr & Self::FLAG_N) != 0;
        let z = (self.cpsr & Self::FLAG_Z) != 0;
        let c = (self.cpsr & Self::FLAG_C) != 0;
        let v = (self.cpsr & Self::FLAG_V) != 0;
        match cond {
            0x0 => z,              // EQ
            0x1 => !z,             // NE
            0x2 => c,              // CS/HS
            0x3 => !c,             // CC/LO
            0x4 => n,              // MI
            0x5 => !n,             // PL
            0x6 => v,              // VS
            0x7 => !v,             // VC
            0x8 => c && !z,        // HI
            0x9 => !c || z,        // LS
            0xA => n == v,         // GE
            0xB => n != v,         // LT
            0xC => !z && (n == v), // GT
            0xD => z || (n != v),  // LE
            0xE => true,           // AL
            _ => false,            // NV (never on ARMv4T)
        }
    }

    #[inline]
    fn set_nz(&mut self, val: u32) {
        self.cpsr = (self.cpsr & !(Self::FLAG_N | Self::FLAG_Z))
            | if (val & 0x8000_0000) != 0 { Self::FLAG_N } else { 0 }
            | if val == 0 { Self::FLAG_Z } else { 0 };
    }

    #[inline]
    fn set_nzc(&mut self, val: u32, carry: bool) {
        self.set_nz(val);
        self.cpsr = (self.cpsr & !Self::FLAG_C) | if carry { Self::FLAG_C } else { 0 };
    }

    fn alu_add(&mut self, lhs: u32, rhs: u32, carry_in: bool, update_flags: bool) -> u32 {
        let sum64 = (lhs as u64) + (rhs as u64) + (carry_in as u64);
        let res = sum64 as u32;
        if update_flags {
            let carry = sum64 > 0xFFFF_FFFF;
            let overflow = ((!(lhs ^ rhs) & (lhs ^ res)) & 0x8000_0000) != 0;
            self.cpsr = (self.cpsr & !(Self::FLAG_N | Self::FLAG_Z | Self::FLAG_C | Self::FLAG_V))
                | if (res & 0x8000_0000) != 0 { Self::FLAG_N } else { 0 }
                | if res == 0 { Self::FLAG_Z } else { 0 }
                | if carry { Self::FLAG_C } else { 0 }
                | if overflow { Self::FLAG_V } else { 0 };
        }
        res
    }

    fn alu_sub(&mut self, lhs: u32, rhs: u32, borrow_in: bool, update_flags: bool) -> u32 {
        // ARM SUB/CMP carry is !borrow: lhs + !rhs + !borrow
        self.alu_add(lhs, !rhs, !borrow_in, update_flags)
    }

    fn barrel_shift(&self, val: u32, shift_type: u32, amount: u32, is_reg_shift: bool) -> (u32, bool) {
        let old_c = (self.cpsr & Self::FLAG_C) != 0;
        if is_reg_shift {
            if amount == 0 {
                return (val, old_c);
            }
            match shift_type {
                0 => {
                    // LSL Rs
                    if amount < 32 {
                        (val << amount, ((val >> (32 - amount)) & 1) != 0)
                    } else if amount == 32 {
                        (0, (val & 1) != 0)
                    } else {
                        (0, false)
                    }
                }
                1 => {
                    // LSR Rs
                    if amount < 32 {
                        (val >> amount, ((val >> (amount - 1)) & 1) != 0)
                    } else if amount == 32 {
                        (0, (val >> 31) != 0)
                    } else {
                        (0, false)
                    }
                }
                2 => {
                    // ASR Rs
                    if amount < 32 {
                        (((val as i32) >> amount) as u32, ((val >> (amount - 1)) & 1) != 0)
                    } else {
                        let bit31 = (val >> 31) != 0;
                        (if bit31 { 0xFFFF_FFFF } else { 0 }, bit31)
                    }
                }
                _ => {
                    // ROR Rs
                    let rot = amount & 31;
                    if rot == 0 {
                        (val, (val >> 31) != 0)
                    } else {
                        let res = val.rotate_right(rot);
                        (res, (res >> 31) != 0)
                    }
                }
            }
        } else {
            match shift_type {
                0 => {
                    // LSL #imm5
                    if amount == 0 {
                        (val, old_c)
                    } else {
                        (val << amount, ((val >> (32 - amount)) & 1) != 0)
                    }
                }
                1 => {
                    // LSR #imm5 (0 -> 32)
                    if amount == 0 {
                        (0, (val >> 31) != 0)
                    } else {
                        (val >> amount, ((val >> (amount - 1)) & 1) != 0)
                    }
                }
                2 => {
                    // ASR #imm5 (0 -> 32)
                    if amount == 0 {
                        let bit31 = (val >> 31) != 0;
                        (if bit31 { 0xFFFF_FFFF } else { 0 }, bit31)
                    } else {
                        (((val as i32) >> amount) as u32, ((val >> (amount - 1)) & 1) != 0)
                    }
                }
                _ => {
                    // ROR #imm5 (0 -> RRX #1)
                    if amount == 0 {
                        let res = ((old_c as u32) << 31) | (val >> 1);
                        (res, (val & 1) != 0)
                    } else {
                        let res = val.rotate_right(amount);
                        (res, (res >> 31) != 0)
                    }
                }
            }
        }
    }

    pub fn step<B: Bus32 + ?Sized>(&mut self, bus: &mut B) -> u32 {
        if self.halted {
            self.cycles += 1;
            return 1;
        }
        if self.is_thumb() {
            self.step_thumb(bus)
        } else {
            self.step_arm(bus)
        }
    }

    fn arm_read_reg(&self, idx: usize, cur_pc: u32, pc_plus12: bool) -> u32 {
        if idx == 15 {
            cur_pc.wrapping_add(if pc_plus12 { 12 } else { 8 })
        } else {
            self.regs[idx]
        }
    }

    fn arm_write_pc(&mut self, val: u32, restore_spsr: bool) {
        if restore_spsr && self.current_bank() != Bank::Usr {
            let spsr = self.current_spsr();
            self.set_cpsr(spsr);
        }
        if self.is_thumb() {
            self.regs[15] = val & !1;
        } else {
            self.regs[15] = val & !3;
        }
    }

    fn step_arm<B: Bus32 + ?Sized>(&mut self, bus: &mut B) -> u32 {
        let cur_pc = self.regs[15] & !3;
        let instr = bus.read32_le(cur_pc);
        self.regs[15] = cur_pc.wrapping_add(4);

        let cond = instr >> 28;
        if !self.cond_passed(cond) {
            self.cycles += 1;
            return 1;
        }

        let op_top = (instr >> 25) & 7;
        let cyc = match op_top {
            0b000 | 0b001 => self.exec_arm_dp_or_misc(bus, instr, cur_pc),
            0b010 => self.exec_arm_single_transfer(bus, instr, cur_pc, false),
            0b011 => {
                if (instr & (1 << 4)) != 0 {
                    // Undefined instruction trap (ARMv4T)
                    self.enter_exception(Self::MODE_UND, 0x0000_0004, cur_pc.wrapping_add(4), false);
                    3
                } else {
                    self.exec_arm_single_transfer(bus, instr, cur_pc, true)
                }
            }
            0b100 => self.exec_arm_block_transfer(bus, instr, cur_pc),
            0b101 => {
                // B / BL
                let link = (instr & (1 << 24)) != 0;
                let off_raw = (instr & 0x00FF_FFFF) as i32;
                let off = ((off_raw << 8) >> 6) as u32;
                if link {
                    self.regs[14] = cur_pc.wrapping_add(4);
                }
                self.regs[15] = cur_pc.wrapping_add(8).wrapping_add(off) & !3;
                3
            }
            0b110 | 0b111 => {
                if (instr >> 24) & 0xF == 0xF {
                    // SWI
                    self.enter_exception(Self::MODE_SVC, 0x0000_0008, cur_pc.wrapping_add(4), false);
                    3
                } else {
                    // No attached coprocessors on PocketStation ARM7TDMI -> Undef trap
                    self.enter_exception(Self::MODE_UND, 0x0000_0004, cur_pc.wrapping_add(4), false);
                    3
                }
            }
            _ => 1,
        };

        self.cycles += cyc as u64;
        cyc
    }

    fn exec_arm_dp_or_misc<B: Bus32 + ?Sized>(&mut self, bus: &mut B, instr: u32, cur_pc: u32) -> u32 {
        // BX Rm: cond 0001 0010 1111 1111 1111 0001 Rm
        if (instr & 0x0FFF_FFF0) == 0x012F_FF10 {
            let rm = (instr & 0xF) as usize;
            let target = self.arm_read_reg(rm, cur_pc, false);
            if (target & 1) != 0 {
                self.cpsr |= Self::FLAG_T;
                self.regs[15] = target & !1;
            } else {
                self.cpsr &= !Self::FLAG_T;
                self.regs[15] = target & !3;
            }
            return 3;
        }

        // SWP / SWPB: cond 0001 0B00 Rn Rd 0000 1001 Rm
        if (instr & 0x0FB0_0FF0) == 0x0100_0090 {
            let byte = (instr & (1 << 22)) != 0;
            let rn = ((instr >> 16) & 0xF) as usize;
            let rd = ((instr >> 12) & 0xF) as usize;
            let rm = (instr & 0xF) as usize;
            let addr = self.arm_read_reg(rn, cur_pc, false);
            let rm_val = self.arm_read_reg(rm, cur_pc, false);
            let old = if byte {
                let v = bus.read8(addr) as u32;
                bus.write8(addr, rm_val as u8);
                v
            } else {
                let v = bus.read32_le(addr & !3).rotate_right((addr & 3) * 8);
                bus.write32_le(addr & !3, rm_val);
                v
            };
            self.regs[rd] = old;
            return 4;
        }

        // Multiply / Multiply-Long (bits [27:25] == 000 and bits [7:4] == 1001)
        if (instr & 0x0E00_00F0) == 0x0000_0090 {
            let bit24_23 = (instr >> 23) & 3;
            if bit24_23 == 0 {
                // MUL / MLA
                let acc = (instr & (1 << 21)) != 0;
                let s = (instr & (1 << 20)) != 0;
                let rd = ((instr >> 16) & 0xF) as usize;
                let rn = ((instr >> 12) & 0xF) as usize;
                let rs = ((instr >> 8) & 0xF) as usize;
                let rm = (instr & 0xF) as usize;
                let rm_val = self.arm_read_reg(rm, cur_pc, false);
                let rs_val = self.arm_read_reg(rs, cur_pc, false);
                let mut res = rm_val.wrapping_mul(rs_val);
                if acc {
                    res = res.wrapping_add(self.arm_read_reg(rn, cur_pc, false));
                }
                if rd != 15 {
                    self.regs[rd] = res;
                }
                if s {
                    self.set_nz(res);
                }
                return if acc { 3 } else { 2 };
            } else if bit24_23 == 1 {
                // UMULL / UMLAL / SMULL / SMLAL
                let signed = (instr & (1 << 22)) != 0;
                let acc = (instr & (1 << 21)) != 0;
                let s = (instr & (1 << 20)) != 0;
                let rd_hi = ((instr >> 16) & 0xF) as usize;
                let rd_lo = ((instr >> 12) & 0xF) as usize;
                let rs = ((instr >> 8) & 0xF) as usize;
                let rm = (instr & 0xF) as usize;
                let rm_val = self.arm_read_reg(rm, cur_pc, false);
                let rs_val = self.arm_read_reg(rs, cur_pc, false);
                let mut prod: u64 = if signed {
                    ((rm_val as i32 as i64) * (rs_val as i32 as i64)) as u64
                } else {
                    (rm_val as u64) * (rs_val as u64)
                };
                if acc {
                    let hi = self.arm_read_reg(rd_hi, cur_pc, false) as u64;
                    let lo = self.arm_read_reg(rd_lo, cur_pc, false) as u64;
                    prod = prod.wrapping_add((hi << 32) | lo);
                }
                let lo = prod as u32;
                let hi = (prod >> 32) as u32;
                if rd_lo != 15 {
                    self.regs[rd_lo] = lo;
                }
                if rd_hi != 15 {
                    self.regs[rd_hi] = hi;
                }
                if s {
                    self.cpsr = (self.cpsr & !(Self::FLAG_N | Self::FLAG_Z))
                        | if (hi & 0x8000_0000) != 0 { Self::FLAG_N } else { 0 }
                        | if prod == 0 { Self::FLAG_Z } else { 0 };
                }
                return if acc { 4 } else { 3 };
            }
        }

        // Halfword / Signed Byte Transfer (bits [27:25] == 000, bit 7 == 1, bit 4 == 1, bits [6:5] != 00)
        if (instr & 0x0E00_0090) == 0x0000_0090 && ((instr >> 5) & 3) != 0 {
            return self.exec_arm_halfword_transfer(bus, instr, cur_pc);
        }

        // MRS / MSR (when S == 0 and opcode in 0x8..0xB)
        let i_bit = (instr & (1 << 25)) != 0;
        let opcode = (instr >> 21) & 0xF;
        let s_bit = (instr & (1 << 20)) != 0;
        if !s_bit && (0x8..=0xB).contains(&opcode) {
            let use_spsr = (instr & (1 << 22)) != 0;
            if !i_bit && (instr & 0x0FBF_0FFF) == 0x010F_0000 {
                // MRS Rd, CPSR/SPSR
                let rd = ((instr >> 12) & 0xF) as usize;
                let val = if use_spsr { self.current_spsr() } else { self.cpsr };
                if rd != 15 {
                    self.regs[rd] = val;
                }
                return 1;
            }
            if ((instr & 0x0FB0_FFF0) == 0x0120_F000 && !i_bit) || ((instr & 0x0FB0_F000) == 0x0320_F000 && i_bit) {
                // MSR CPSR/SPSR_<fields>, op
                let val = if i_bit {
                    let imm = instr & 0xFF;
                    let rot = ((instr >> 8) & 0xF) * 2;
                    imm.rotate_right(rot)
                } else {
                    self.arm_read_reg((instr & 0xF) as usize, cur_pc, false)
                };
                let mut mask = 0u32;
                if (instr & (1 << 16)) != 0 {
                    mask |= 0x0000_00FF;
                }
                if (instr & (1 << 17)) != 0 {
                    mask |= 0x0000_FF00;
                }
                if (instr & (1 << 18)) != 0 {
                    mask |= 0x00FF_0000;
                }
                if (instr & (1 << 19)) != 0 {
                    mask |= 0xFF00_0000;
                }
                if !use_spsr {
                    // User mode can only update flags (f); never toggle Thumb bit via MSR.
                    if (self.cpsr & 0x1F) == Self::MODE_USR {
                        mask &= 0xFF00_0000;
                    }
                    mask &= !Self::FLAG_T;
                    let next = (self.cpsr & !mask) | (val & mask);
                    self.set_cpsr(next);
                } else if self.current_bank() != Bank::Usr {
                    let next = (self.current_spsr() & !mask) | (val & mask);
                    self.set_current_spsr(next);
                }
                return 1;
            }
        }

        // Standard Data Processing
        let rn = ((instr >> 16) & 0xF) as usize;
        let rd = ((instr >> 12) & 0xF) as usize;
        let reg_shift = !i_bit && ((instr & (1 << 4)) != 0);

        let (op2, shifter_carry) = if i_bit {
            let imm = instr & 0xFF;
            let rot = ((instr >> 8) & 0xF) * 2;
            if rot == 0 {
                (imm, (self.cpsr & Self::FLAG_C) != 0)
            } else {
                let res = imm.rotate_right(rot);
                (res, (res >> 31) != 0)
            }
        } else {
            let rm = (instr & 0xF) as usize;
            let rm_val = self.arm_read_reg(rm, cur_pc, reg_shift);
            let shift_type = (instr >> 5) & 3;
            let amount = if reg_shift {
                let rs = ((instr >> 8) & 0xF) as usize;
                self.regs[rs] & 0xFF
            } else {
                (instr >> 7) & 0x1F
            };
            self.barrel_shift(rm_val, shift_type, amount, reg_shift)
        };

        let lhs = self.arm_read_reg(rn, cur_pc, reg_shift);
        let old_c = (self.cpsr & Self::FLAG_C) != 0;
        let update_normal_flags = s_bit && rd != 15;

        let (write_rd, res) = match opcode {
            0x0 => {
                // AND
                let r = lhs & op2;
                if update_normal_flags {
                    self.set_nzc(r, shifter_carry);
                }
                (true, r)
            }
            0x1 => {
                // EOR
                let r = lhs ^ op2;
                if update_normal_flags {
                    self.set_nzc(r, shifter_carry);
                }
                (true, r)
            }
            0x2 => (true, self.alu_sub(lhs, op2, false, update_normal_flags)), // SUB
            0x3 => (true, self.alu_sub(op2, lhs, false, update_normal_flags)), // RSB
            0x4 => (true, self.alu_add(lhs, op2, false, update_normal_flags)), // ADD
            0x5 => (true, self.alu_add(lhs, op2, old_c, update_normal_flags)),  // ADC
            0x6 => (true, self.alu_sub(lhs, op2, !old_c, update_normal_flags)), // SBC
            0x7 => (true, self.alu_sub(op2, lhs, !old_c, update_normal_flags)), // RSC
            0x8 => {
                // TST
                self.set_nzc(lhs & op2, shifter_carry);
                (false, 0)
            }
            0x9 => {
                // TEQ
                self.set_nzc(lhs ^ op2, shifter_carry);
                (false, 0)
            }
            0xA => {
                // CMP
                self.alu_sub(lhs, op2, false, true);
                (false, 0)
            }
            0xB => {
                // CMN
                self.alu_add(lhs, op2, false, true);
                (false, 0)
            }
            0xC => {
                // ORR
                let r = lhs | op2;
                if update_normal_flags {
                    self.set_nzc(r, shifter_carry);
                }
                (true, r)
            }
            0xD => {
                // MOV
                if update_normal_flags {
                    self.set_nzc(op2, shifter_carry);
                }
                (true, op2)
            }
            0xE => {
                // BIC
                let r = lhs & !op2;
                if update_normal_flags {
                    self.set_nzc(r, shifter_carry);
                }
                (true, r)
            }
            _ => {
                // MVN
                let r = !op2;
                if update_normal_flags {
                    self.set_nzc(r, shifter_carry);
                }
                (true, r)
            }
        };

        if write_rd {
            if rd == 15 {
                self.arm_write_pc(res, s_bit);
                return if reg_shift { 4 } else { 3 };
            }
            self.regs[rd] = res;
        }
        if reg_shift { 2 } else { 1 }
    }

    fn exec_arm_halfword_transfer<B: Bus32 + ?Sized>(
        &mut self,
        bus: &mut B,
        instr: u32,
        cur_pc: u32,
    ) -> u32 {
        let p = (instr & (1 << 24)) != 0;
        let u = (instr & (1 << 23)) != 0;
        let i = (instr & (1 << 22)) != 0;
        let w = (instr & (1 << 21)) != 0;
        let l = (instr & (1 << 20)) != 0;
        let rn = ((instr >> 16) & 0xF) as usize;
        let rd = ((instr >> 12) & 0xF) as usize;
        let sh = (instr >> 5) & 3;

        let base = self.arm_read_reg(rn, cur_pc, false);
        let offset = if i {
            ((instr >> 4) & 0xF0) | (instr & 0xF)
        } else {
            self.arm_read_reg((instr & 0xF) as usize, cur_pc, false)
        };
        let post_base = if u {
            base.wrapping_add(offset)
        } else {
            base.wrapping_sub(offset)
        };
        let addr = if p { post_base } else { base };

        if !p || w {
            if rn == 15 {
                self.regs[15] = post_base & !3;
            } else {
                self.regs[rn] = post_base;
            }
        }

        if l {
            let val = match sh {
                1 => {
                    // LDRH (unaligned rotates on ARM7TDMI)
                    let hw = bus.read16_le(addr & !1) as u32;
                    if (addr & 1) != 0 { hw.rotate_right(8) } else { hw }
                }
                2 => bus.read8(addr) as i8 as i32 as u32, // LDRSB
                _ => {
                    // LDRSH (odd addr behaves like sign-extended byte on ARM7TDMI)
                    if (addr & 1) != 0 {
                        bus.read8(addr) as i8 as i32 as u32
                    } else {
                        bus.read16_le(addr) as i16 as i32 as u32
                    }
                }
            };
            if rd == 15 {
                self.regs[15] = val & !3;
                return 5;
            }
            self.regs[rd] = val;
            3
        } else {
            let val = self.arm_read_reg(rd, cur_pc, true) as u16;
            bus.write16_le(addr & !1, val);
            2
        }
    }

    fn exec_arm_single_transfer<B: Bus32 + ?Sized>(
        &mut self,
        bus: &mut B,
        instr: u32,
        cur_pc: u32,
        reg_offset: bool,
    ) -> u32 {
        let p = (instr & (1 << 24)) != 0;
        let u = (instr & (1 << 23)) != 0;
        let b = (instr & (1 << 22)) != 0;
        let w = (instr & (1 << 21)) != 0;
        let l = (instr & (1 << 20)) != 0;
        let rn = ((instr >> 16) & 0xF) as usize;
        let rd = ((instr >> 12) & 0xF) as usize;

        let base = self.arm_read_reg(rn, cur_pc, false);
        let offset = if reg_offset {
            let rm = (instr & 0xF) as usize;
            let rm_val = self.arm_read_reg(rm, cur_pc, false);
            let shift_type = (instr >> 5) & 3;
            let amount = (instr >> 7) & 0x1F;
            self.barrel_shift(rm_val, shift_type, amount, false).0
        } else {
            instr & 0xFFF
        };

        let post_base = if u {
            base.wrapping_add(offset)
        } else {
            base.wrapping_sub(offset)
        };
        let addr = if p { post_base } else { base };

        if !p || w {
            if rn == 15 {
                self.regs[15] = post_base & !3;
            } else {
                self.regs[rn] = post_base;
            }
        }

        if l {
            let val = if b {
                bus.read8(addr) as u32
            } else {
                bus.read32_le(addr & !3).rotate_right((addr & 3) * 8)
            };
            if rd == 15 {
                self.regs[15] = val & !3;
                return 5;
            }
            self.regs[rd] = val;
            3
        } else {
            let val = self.arm_read_reg(rd, cur_pc, true);
            if b {
                bus.write8(addr, val as u8);
            } else {
                bus.write32_le(addr & !3, val);
            }
            2
        }
    }

    fn exec_arm_block_transfer<B: Bus32 + ?Sized>(
        &mut self,
        bus: &mut B,
        instr: u32,
        cur_pc: u32,
    ) -> u32 {
        let p = (instr & (1 << 24)) != 0;
        let u = (instr & (1 << 23)) != 0;
        let s = (instr & (1 << 22)) != 0;
        let w = (instr & (1 << 21)) != 0;
        let l = (instr & (1 << 20)) != 0;
        let rn = ((instr >> 16) & 0xF) as usize;
        let rlist = instr & 0xFFFF;

        let base = self.arm_read_reg(rn, cur_pc, false);
        let (count, empty_list) = if rlist == 0 {
            (16u32, true)
        } else {
            (rlist.count_ones(), false)
        };
        let total_bytes = count * 4;
        let writeback_val = if u {
            base.wrapping_add(total_bytes)
        } else {
            base.wrapping_sub(total_bytes)
        };

        let mut addr = match (u, p) {
            (true, false) => base,                              // IA
            (true, true) => base.wrapping_add(4),               // IB
            (false, false) => base.wrapping_sub(total_bytes).wrapping_add(4), // DA
            (false, true) => base.wrapping_sub(total_bytes),    // DB
        };

        let pc_in_list = (rlist & (1 << 15)) != 0 || empty_list;
        let user_bank = s && !pc_in_list;
        let mut cyc = 2 + count;

        if l {
            if empty_list {
                let v = bus.read32_le(addr & !3);
                self.regs[15] = v & !3;
                cyc += 2;
            } else {
                let mut pc_loaded = None;
                for reg in 0..16usize {
                    if (rlist & (1 << reg)) == 0 {
                        continue;
                    }
                    let v = bus.read32_le(addr & !3);
                    addr = addr.wrapping_add(4);
                    if reg == 15 {
                        pc_loaded = Some(v);
                    } else if user_bank {
                        self.write_usr_reg(reg, v);
                    } else {
                        self.regs[reg] = v;
                    }
                }
                if w && ((rlist & (1 << rn)) == 0) {
                    self.regs[rn] = writeback_val;
                }
                if let Some(target) = pc_loaded {
                    self.arm_write_pc(target, s);
                    cyc += 2;
                }
            }
            if empty_list && w {
                self.regs[rn] = writeback_val;
            }
        } else {
            let lowest = rlist.trailing_zeros() as usize;
            let old_rn = base;
            if empty_list {
                bus.write32_le(addr & !3, cur_pc.wrapping_add(12));
                if w {
                    self.regs[rn] = writeback_val;
                }
            } else {
                for reg in 0..16usize {
                    if (rlist & (1 << reg)) == 0 {
                        continue;
                    }
                    let val = if reg == 15 {
                        cur_pc.wrapping_add(12)
                    } else if reg == rn && w {
                        if reg == lowest { old_rn } else { writeback_val }
                    } else if user_bank {
                        self.read_usr_reg(reg)
                    } else {
                        self.regs[reg]
                    };
                    bus.write32_le(addr & !3, val);
                    addr = addr.wrapping_add(4);
                }
                if w {
                    self.regs[rn] = writeback_val;
                }
            }
        }

        cyc
    }

    fn step_thumb<B: Bus32 + ?Sized>(&mut self, bus: &mut B) -> u32 {
        let cur_pc = self.regs[15] & !1;
        let instr = bus.read16_le(cur_pc);
        self.regs[15] = cur_pc.wrapping_add(2);
        self.thumb_bl_pending = false;

        let cyc = match instr >> 13 {
            0b000 => {
                let op = (instr >> 11) & 3;
                if op == 3 {
                    // Add / subtract
                    let imm = (instr & (1 << 10)) != 0;
                    let sub = (instr & (1 << 9)) != 0;
                    let rn_imm3 = ((instr >> 6) & 7) as u32;
                    let rs = ((instr >> 3) & 7) as usize;
                    let rd = (instr & 7) as usize;
                    let lhs = self.regs[rs];
                    let rhs = if imm { rn_imm3 } else { self.regs[rn_imm3 as usize] };
                    self.regs[rd] = if sub {
                        self.alu_sub(lhs, rhs, false, true)
                    } else {
                        self.alu_add(lhs, rhs, false, true)
                    };
                    1
                } else {
                    // Move shifted register (LSL/LSR/ASR imm5)
                    let imm5 = ((instr >> 6) & 0x1F) as u32;
                    let rs = ((instr >> 3) & 7) as usize;
                    let rd = (instr & 7) as usize;
                    let (res, c) = self.barrel_shift(self.regs[rs], op as u32, imm5, false);
                    self.set_nzc(res, c);
                    self.regs[rd] = res;
                    1
                }
            }
            0b001 => {
                // Move/Compare/Add/Subtract immediate
                let op = (instr >> 11) & 3;
                let rd = ((instr >> 8) & 7) as usize;
                let imm8 = (instr & 0xFF) as u32;
                match op {
                    0 => {
                        self.set_nz(imm8);
                        self.regs[rd] = imm8;
                    }
                    1 => {
                        self.alu_sub(self.regs[rd], imm8, false, true);
                    }
                    2 => {
                        self.regs[rd] = self.alu_add(self.regs[rd], imm8, false, true);
                    }
                    _ => {
                        self.regs[rd] = self.alu_sub(self.regs[rd], imm8, false, true);
                    }
                }
                1
            }
            0b010 => {
                if (instr & 0xFC00) == 0x4000 {
                    // ALU operations
                    self.exec_thumb_alu(instr)
                } else if (instr & 0xFC00) == 0x4400 {
                    // Hi register operations / BX
                    self.exec_thumb_hireg_bx(instr, cur_pc)
                } else if (instr & 0xF800) == 0x4800 {
                    // PC-relative load
                    let rd = ((instr >> 8) & 7) as usize;
                    let off = ((instr & 0xFF) as u32) << 2;
                    let addr = (cur_pc.wrapping_add(4) & !3).wrapping_add(off);
                    self.regs[rd] = bus.read32_le(addr);
                    3
                } else {
                    // Load/store with register offset (0101_...)
                    self.exec_thumb_ldst_reg(bus, instr)
                }
            }
            0b011 => {
                // Load/store immediate offset (word or byte)
                let b = (instr & (1 << 12)) != 0;
                let l = (instr & (1 << 11)) != 0;
                let imm5 = ((instr >> 6) & 0x1F) as u32;
                let rb = ((instr >> 3) & 7) as usize;
                let rd = (instr & 7) as usize;
                let off = if b { imm5 } else { imm5 << 2 };
                let addr = self.regs[rb].wrapping_add(off);
                if l {
                    self.regs[rd] = if b {
                        bus.read8(addr) as u32
                    } else {
                        bus.read32_le(addr & !3).rotate_right((addr & 3) * 8)
                    };
                    3
                } else {
                    if b {
                        bus.write8(addr, self.regs[rd] as u8);
                    } else {
                        bus.write32_le(addr & !3, self.regs[rd]);
                    }
                    2
                }
            }
            0b100 => {
                if (instr & 0xF000) == 0x8000 {
                    // Load/store halfword immediate
                    let l = (instr & (1 << 11)) != 0;
                    let off = (((instr >> 6) & 0x1F) as u32) << 1;
                    let rb = ((instr >> 3) & 7) as usize;
                    let rd = (instr & 7) as usize;
                    let addr = self.regs[rb].wrapping_add(off);
                    if l {
                        let hw = bus.read16_le(addr & !1) as u32;
                        self.regs[rd] = if (addr & 1) != 0 { hw.rotate_right(8) } else { hw };
                        3
                    } else {
                        bus.write16_le(addr & !1, self.regs[rd] as u16);
                        2
                    }
                } else {
                    // SP-relative load/store
                    let l = (instr & (1 << 11)) != 0;
                    let rd = ((instr >> 8) & 7) as usize;
                    let off = ((instr & 0xFF) as u32) << 2;
                    let addr = self.regs[13].wrapping_add(off);
                    if l {
                        self.regs[rd] = bus.read32_le(addr & !3).rotate_right((addr & 3) * 8);
                        3
                    } else {
                        bus.write32_le(addr & !3, self.regs[rd]);
                        2
                    }
                }
            }
            0b101 => {
                if (instr & 0xF000) == 0xA000 {
                    // Load address (ADD Rd, PC/SP, #imm8*4)
                    let sp = (instr & (1 << 11)) != 0;
                    let rd = ((instr >> 8) & 7) as usize;
                    let off = ((instr & 0xFF) as u32) << 2;
                    let base = if sp {
                        self.regs[13]
                    } else {
                        cur_pc.wrapping_add(4) & !3
                    };
                    self.regs[rd] = base.wrapping_add(off);
                    1
                } else if (instr & 0xFF00) == 0xB000 {
                    // Add offset to SP
                    let sub = (instr & (1 << 7)) != 0;
                    let off = ((instr & 0x7F) as u32) << 2;
                    self.regs[13] = if sub {
                        self.regs[13].wrapping_sub(off)
                    } else {
                        self.regs[13].wrapping_add(off)
                    };
                    1
                } else if (instr & 0xF600) == 0xB400 {
                    // PUSH / POP registers
                    self.exec_thumb_push_pop(bus, instr)
                } else {
                    // Undefined Thumb 0xB...
                    self.enter_exception(Self::MODE_UND, 0x0000_0004, cur_pc.wrapping_add(2), false);
                    3
                }
            }
            0b110 => {
                if (instr & 0xF000) == 0xC000 {
                    // Multiple load/store (LDMIA / STMIA)
                    let l = (instr & (1 << 11)) != 0;
                    let rb = ((instr >> 8) & 7) as usize;
                    let rlist = instr & 0xFF;
                    self.exec_thumb_ldm_stm(bus, rb, rlist, l)
                } else {
                    // Conditional branch or SWI
                    let cond = ((instr >> 8) & 0xF) as u32;
                    if cond == 0xF {
                        self.enter_exception(
                            Self::MODE_SVC,
                            0x0000_0008,
                            cur_pc.wrapping_add(2),
                            false,
                        );
                        3
                    } else if cond == 0xE {
                        self.enter_exception(
                            Self::MODE_UND,
                            0x0000_0004,
                            cur_pc.wrapping_add(2),
                            false,
                        );
                        3
                    } else if self.cond_passed(cond) {
                        let off = ((instr as i8 as i32) << 1) as u32;
                        self.regs[15] = cur_pc.wrapping_add(4).wrapping_add(off) & !1;
                        3
                    } else {
                        1
                    }
                }
            }
            _ => {
                // 0b111: Unconditional branch or BL
                if (instr & 0xF800) == 0xE000 {
                    let raw = (instr & 0x7FF) as i32;
                    let off = ((raw << 21) >> 20) as u32;
                    self.regs[15] = cur_pc.wrapping_add(4).wrapping_add(off) & !1;
                    3
                } else if (instr & 0xF800) == 0xF000 {
                    // BL prefix (H=0)
                    let raw = (instr & 0x7FF) as i32;
                    let off = ((raw << 21) >> 9) as u32;
                    self.regs[14] = cur_pc.wrapping_add(4).wrapping_add(off);
                    self.thumb_bl_pending = true;
                    1
                } else if (instr & 0xF800) == 0xF800 {
                    // BL suffix (H=1)
                    let off = ((instr & 0x7FF) as u32) << 1;
                    let target = self.regs[14].wrapping_add(off) & !1;
                    self.regs[14] = cur_pc.wrapping_add(2) | 1;
                    self.regs[15] = target;
                    3
                } else {
                    self.enter_exception(Self::MODE_UND, 0x0000_0004, cur_pc.wrapping_add(2), false);
                    3
                }
            }
        };

        self.cycles += cyc as u64;
        cyc
    }

    fn exec_thumb_alu(&mut self, instr: u16) -> u32 {
        let op = (instr >> 6) & 0xF;
        let rs = ((instr >> 3) & 7) as usize;
        let rd = (instr & 7) as usize;
        let lhs = self.regs[rd];
        let rhs = self.regs[rs];
        let old_c = (self.cpsr & Self::FLAG_C) != 0;

        match op {
            0x0 => {
                let r = lhs & rhs;
                self.set_nz(r);
                self.regs[rd] = r;
            }
            0x1 => {
                let r = lhs ^ rhs;
                self.set_nz(r);
                self.regs[rd] = r;
            }
            0x2 => {
                let (r, c) = self.barrel_shift(lhs, 0, rhs & 0xFF, true);
                self.set_nzc(r, c);
                self.regs[rd] = r;
                return 2;
            }
            0x3 => {
                let (r, c) = self.barrel_shift(lhs, 1, rhs & 0xFF, true);
                self.set_nzc(r, c);
                self.regs[rd] = r;
                return 2;
            }
            0x4 => {
                let (r, c) = self.barrel_shift(lhs, 2, rhs & 0xFF, true);
                self.set_nzc(r, c);
                self.regs[rd] = r;
                return 2;
            }
            0x5 => self.regs[rd] = self.alu_add(lhs, rhs, old_c, true),
            0x6 => self.regs[rd] = self.alu_sub(lhs, rhs, !old_c, true),
            0x7 => {
                let (r, c) = self.barrel_shift(lhs, 3, rhs & 0xFF, true);
                self.set_nzc(r, c);
                self.regs[rd] = r;
                return 2;
            }
            0x8 => self.set_nz(lhs & rhs),
            0x9 => self.regs[rd] = self.alu_sub(0, rhs, false, true),
            0xA => {
                self.alu_sub(lhs, rhs, false, true);
            }
            0xB => {
                self.alu_add(lhs, rhs, false, true);
            }
            0xC => {
                let r = lhs | rhs;
                self.set_nz(r);
                self.regs[rd] = r;
            }
            0xD => {
                let r = lhs.wrapping_mul(rhs);
                self.set_nz(r);
                self.regs[rd] = r;
                return 2;
            }
            0xE => {
                let r = lhs & !rhs;
                self.set_nz(r);
                self.regs[rd] = r;
            }
            _ => {
                let r = !rhs;
                self.set_nz(r);
                self.regs[rd] = r;
            }
        }
        1
    }

    fn exec_thumb_hireg_bx(&mut self, instr: u16, cur_pc: u32) -> u32 {
        let op = (instr >> 8) & 3;
        let h1 = ((instr >> 7) & 1) as usize;
        let h2 = ((instr >> 6) & 1) as usize;
        let rs = ((instr >> 3) & 7) as usize + h2 * 8;
        let rd = (instr & 7) as usize + h1 * 8;

        let rs_val = if rs == 15 { cur_pc.wrapping_add(4) } else { self.regs[rs] };
        let rd_val = if rd == 15 { cur_pc.wrapping_add(4) } else { self.regs[rd] };

        match op {
            0 => {
                let sum = rd_val.wrapping_add(rs_val);
                if rd == 15 {
                    self.regs[15] = sum & !1;
                    return 3;
                }
                self.regs[rd] = sum;
                1
            }
            1 => {
                self.alu_sub(rd_val, rs_val, false, true);
                1
            }
            2 => {
                if rd == 15 {
                    self.regs[15] = rs_val & !1;
                    return 3;
                }
                self.regs[rd] = rs_val;
                1
            }
            _ => {
                // BX Rs (Thumb PC read as Rs aligns to word)
                let target = if rs == 15 {
                    cur_pc.wrapping_add(4) & !3
                } else {
                    rs_val
                };
                if (target & 1) != 0 {
                    self.cpsr |= Self::FLAG_T;
                    self.regs[15] = target & !1;
                } else {
                    self.cpsr &= !Self::FLAG_T;
                    self.regs[15] = target & !3;
                }
                3
            }
        }
    }

    fn exec_thumb_ldst_reg<B: Bus32 + ?Sized>(&mut self, bus: &mut B, instr: u16) -> u32 {
        let ro = ((instr >> 6) & 7) as usize;
        let rb = ((instr >> 3) & 7) as usize;
        let rd = (instr & 7) as usize;
        let addr = self.regs[rb].wrapping_add(self.regs[ro]);
        let h = (instr & (1 << 9)) != 0;
        let op = (instr >> 10) & 3;

        if !h {
            match op {
                0 => {
                    bus.write32_le(addr & !3, self.regs[rd]);
                    2
                }
                1 => {
                    bus.write8(addr, self.regs[rd] as u8);
                    2
                }
                2 => {
                    self.regs[rd] = bus.read32_le(addr & !3).rotate_right((addr & 3) * 8);
                    3
                }
                _ => {
                    self.regs[rd] = bus.read8(addr) as u32;
                    3
                }
            }
        } else {
            match op {
                0 => {
                    bus.write16_le(addr & !1, self.regs[rd] as u16);
                    2
                }
                1 => {
                    self.regs[rd] = bus.read8(addr) as i8 as i32 as u32;
                    3
                }
                2 => {
                    let hw = bus.read16_le(addr & !1) as u32;
                    self.regs[rd] = if (addr & 1) != 0 { hw.rotate_right(8) } else { hw };
                    3
                }
                _ => {
                    self.regs[rd] = if (addr & 1) != 0 {
                        bus.read8(addr) as i8 as i32 as u32
                    } else {
                        bus.read16_le(addr) as i16 as i32 as u32
                    };
                    3
                }
            }
        }
    }

    fn exec_thumb_push_pop<B: Bus32 + ?Sized>(&mut self, bus: &mut B, instr: u16) -> u32 {
        let l = (instr & (1 << 11)) != 0;
        let r = (instr & (1 << 8)) != 0;
        let rlist = instr & 0xFF;
        let count = rlist.count_ones() + u32::from(r);

        if l {
            // POP
            let mut sp = self.regs[13];
            if count == 0 {
                let pc = bus.read32_le(sp & !3);
                self.regs[15] = pc & !1;
                self.regs[13] = sp.wrapping_add(0x40);
                return 5;
            }
            for reg in 0..8usize {
                if (rlist & (1 << reg)) != 0 {
                    self.regs[reg] = bus.read32_le(sp & !3);
                    sp = sp.wrapping_add(4);
                }
            }
            if r {
                self.regs[15] = bus.read32_le(sp & !3) & !1;
                sp = sp.wrapping_add(4);
                self.regs[13] = sp;
                return 4 + count;
            }
            self.regs[13] = sp;
            2 + count
        } else {
            // PUSH
            let mut sp = self.regs[13];
            if count == 0 {
                sp = sp.wrapping_sub(0x40);
                bus.write32_le(sp & !3, self.regs[15].wrapping_add(2));
                self.regs[13] = sp;
                return 3;
            }
            let total = count * 4;
            let new_sp = sp.wrapping_sub(total);
            let mut addr = new_sp;
            for reg in 0..8usize {
                if (rlist & (1 << reg)) != 0 {
                    bus.write32_le(addr & !3, self.regs[reg]);
                    addr = addr.wrapping_add(4);
                }
            }
            if r {
                bus.write32_le(addr & !3, self.regs[14]);
            }
            self.regs[13] = new_sp;
            2 + count
        }
    }

    fn exec_thumb_ldm_stm<B: Bus32 + ?Sized>(
        &mut self,
        bus: &mut B,
        rb: usize,
        rlist: u16,
        load: bool,
    ) -> u32 {
        let count = rlist.count_ones();
        if count == 0 {
            let addr = self.regs[rb];
            if load {
                self.regs[15] = bus.read32_le(addr & !3) & !1;
            } else {
                bus.write32_le(addr & !3, self.regs[15].wrapping_add(4));
            }
            self.regs[rb] = addr.wrapping_add(0x40);
            return 4;
        }
        let mut addr = self.regs[rb];
        let writeback = addr.wrapping_add(count * 4);
        if load {
            for reg in 0..8usize {
                if (rlist & (1 << reg)) != 0 {
                    self.regs[reg] = bus.read32_le(addr & !3);
                    addr = addr.wrapping_add(4);
                }
            }
            if (rlist & (1 << rb)) == 0 {
                self.regs[rb] = writeback;
            }
        } else {
            let lowest = rlist.trailing_zeros() as usize;
            let old_rb = self.regs[rb];
            for reg in 0..8usize {
                if (rlist & (1 << reg)) != 0 {
                    let v = if reg == rb {
                        if reg == lowest { old_rb } else { writeback }
                    } else {
                        self.regs[reg]
                    };
                    bus.write32_le(addr & !3, v);
                    addr = addr.wrapping_add(4);
                }
            }
            self.regs[rb] = writeback;
        }
        2 + count
    }

    pub fn debug_state(&self) -> CpuDebugState {
        let mode = match self.cpsr & 0x1F {
            0x10 => "USR",
            0x11 => "FIQ",
            0x12 => "IRQ",
            0x13 => "SVC",
            0x17 => "ABT",
            0x1B => "UND",
            0x1F => "SYS",
            _ => "???",
        };
        let flags = format!(
            "{}{}{}{} {}{}{} {mode}",
            if (self.cpsr & Self::FLAG_N) != 0 { 'N' } else { '-' },
            if (self.cpsr & Self::FLAG_Z) != 0 { 'Z' } else { '-' },
            if (self.cpsr & Self::FLAG_C) != 0 { 'C' } else { '-' },
            if (self.cpsr & Self::FLAG_V) != 0 { 'V' } else { '-' },
            if (self.cpsr & Self::FLAG_I) != 0 { 'I' } else { '-' },
            if (self.cpsr & Self::FLAG_F) != 0 { 'F' } else { '-' },
            if (self.cpsr & Self::FLAG_T) != 0 { 'T' } else { 'A' },
        );
        let mut registers = Vec::with_capacity(16);
        for (i, &val) in self.regs.iter().enumerate() {
            let name = match i {
                13 => "SP".into(),
                14 => "LR".into(),
                15 => "PC".into(),
                _ => format!("R{i}"),
            };
            registers.push((name, format!("{val:08X}")));
        }
        CpuDebugState {
            arch: if self.is_thumb() { "ARM7TDMI (Thumb)" } else { "ARM7TDMI (ARM)" },
            pc: self.regs[15],
            sp: self.regs[13],
            flags,
            registers,
            cycles: self.cycles,
            halted: self.halted,
        }
    }

    pub fn disassemble<B: Bus32 + ?Sized>(
        &self,
        bus: &B,
        start_addr: u32,
        count: usize,
    ) -> Vec<DisassembledInstruction> {
        let mut out = Vec::with_capacity(count);
        if self.is_thumb() {
            let mut addr = start_addr & !1;
            for _ in 0..count {
                let hw = (bus.peek8(addr) as u16) | ((bus.peek8(addr.wrapping_add(1)) as u16) << 8);
                let mnemonic = disasm_thumb_one(hw, addr);
                out.push(DisassembledInstruction {
                    address: addr,
                    bytes: hw.to_le_bytes().to_vec(),
                    mnemonic,
                });
                addr = addr.wrapping_add(2);
            }
        } else {
            let mut addr = start_addr & !3;
            for _ in 0..count {
                let w = (bus.peek8(addr) as u32)
                    | ((bus.peek8(addr.wrapping_add(1)) as u32) << 8)
                    | ((bus.peek8(addr.wrapping_add(2)) as u32) << 16)
                    | ((bus.peek8(addr.wrapping_add(3)) as u32) << 24);
                let mnemonic = disasm_arm_one(w, addr);
                out.push(DisassembledInstruction {
                    address: addr,
                    bytes: w.to_le_bytes().to_vec(),
                    mnemonic,
                });
                addr = addr.wrapping_add(4);
            }
        }
        out
    }
}

fn arm_cond_str(cond: u32) -> &'static str {
    match cond {
        0x0 => "EQ",
        0x1 => "NE",
        0x2 => "CS",
        0x3 => "CC",
        0x4 => "MI",
        0x5 => "PL",
        0x6 => "VS",
        0x7 => "VC",
        0x8 => "HI",
        0x9 => "LS",
        0xA => "GE",
        0xB => "LT",
        0xC => "GT",
        0xD => "LE",
        0xE => "",
        _ => "NV",
    }
}

fn arm_reg_name(r: u32) -> &'static str {
    match r & 0xF {
        0 => "R0",
        1 => "R1",
        2 => "R2",
        3 => "R3",
        4 => "R4",
        5 => "R5",
        6 => "R6",
        7 => "R7",
        8 => "R8",
        9 => "R9",
        10 => "R10",
        11 => "R11",
        12 => "R12",
        13 => "SP",
        14 => "LR",
        _ => "PC",
    }
}

fn disasm_arm_one(w: u32, addr: u32) -> String {
    let cond = arm_cond_str(w >> 28);
    if (w & 0x0FFF_FFF0) == 0x012F_FF10 {
        return format!("BX{cond} {}", arm_reg_name(w));
    }
    if (w & 0x0F00_0000) == 0x0F00_0000 {
        return format!("SWI{cond} #{:06X}", w & 0x00FF_FFFF);
    }
    match (w >> 25) & 7 {
        0b101 => {
            let link = if (w & (1 << 24)) != 0 { "L" } else { "" };
            let off = (((w & 0x00FF_FFFF) as i32) << 8) >> 6;
            let target = addr.wrapping_add(8).wrapping_add(off as u32);
            format!("B{link}{cond} ${target:08X}")
        }
        0b100 => {
            let mnem = if (w & (1 << 20)) != 0 { "LDM" } else { "STM" };
            let dir = match ((w >> 24) & 1, (w >> 23) & 1) {
                (0, 1) => "IA",
                (1, 1) => "IB",
                (0, 0) => "DA",
                _ => "DB",
            };
            let wb = if (w & (1 << 21)) != 0 { "!" } else { "" };
            format!("{mnem}{cond}{dir} {}{wb}, {{{:#06X}}}", arm_reg_name(w >> 16), w & 0xFFFF)
        }
        0b010 | 0b011 => {
            let mnem = if (w & (1 << 20)) != 0 { "LDR" } else { "STR" };
            let b = if (w & (1 << 22)) != 0 { "B" } else { "" };
            let rn = (w >> 16) & 0xF;
            let rd = (w >> 12) & 0xF;
            if (w & (1 << 25)) == 0 {
                let off = w & 0xFFF;
                let sign = if (w & (1 << 23)) != 0 { "+" } else { "-" };
                format!("{mnem}{cond}{b} {}, [{}, #{sign}0x{off:X}]", arm_reg_name(rd), arm_reg_name(rn))
            } else {
                format!("{mnem}{cond}{b} {}, [{}, {}]", arm_reg_name(rd), arm_reg_name(rn), arm_reg_name(w))
            }
        }
        0b000 | 0b001 => {
            const OPS: [&str; 16] = [
                "AND", "EOR", "SUB", "RSB", "ADD", "ADC", "SBC", "RSC",
                "TST", "TEQ", "CMP", "CMN", "ORR", "MOV", "BIC", "MVN",
            ];
            let op = ((w >> 21) & 0xF) as usize;
            let s = if (w & (1 << 20)) != 0 && !(8..=11).contains(&op) { "S" } else { "" };
            let rn = (w >> 16) & 0xF;
            let rd = (w >> 12) & 0xF;
            let rhs = if (w & (1 << 25)) != 0 {
                let imm = (w & 0xFF).rotate_right(((w >> 8) & 0xF) * 2);
                format!("#0x{imm:X}")
            } else {
                arm_reg_name(w).to_string()
            };
            if op == 13 || op == 15 {
                format!("{}{cond}{s} {}, {rhs}", OPS[op], arm_reg_name(rd))
            } else if (8..=11).contains(&op) {
                format!("{}{cond} {}, {rhs}", OPS[op], arm_reg_name(rn))
            } else {
                format!("{}{cond}{s} {}, {}, {rhs}", OPS[op], arm_reg_name(rd), arm_reg_name(rn))
            }
        }
        _ => format!("DCD 0x{w:08X}"),
    }
}

fn disasm_thumb_one(hw: u16, addr: u32) -> String {
    let op = hw >> 11;
    let rd = u32::from(hw & 7);
    let rs = u32::from((hw >> 3) & 7);
    match hw >> 13 {
        0 => match op {
            0..=2 => {
                const S: [&str; 3] = ["LSL", "LSR", "ASR"];
                format!("{} R{rd}, R{rs}, #{}", S[op as usize], (hw >> 6) & 0x1F)
            }
            _ => {
                let mnem = if (hw & (1 << 9)) != 0 { "SUB" } else { "ADD" };
                let f3 = (hw >> 6) & 7;
                if (hw & (1 << 10)) != 0 {
                    format!("{mnem} R{rd}, R{rs}, #{f3}")
                } else {
                    format!("{mnem} R{rd}, R{rs}, R{f3}")
                }
            }
        },
        1 => {
            const OPS: [&str; 4] = ["MOV", "CMP", "ADD", "SUB"];
            format!("{} R{}, #0x{:02X}", OPS[((hw >> 11) & 3) as usize], (hw >> 8) & 7, hw & 0xFF)
        }
        2 => {
            if (hw & 0xFC00) == 0x4400 {
                let r1 = u32::from((hw & 7) + (((hw >> 7) & 1) << 3));
                let r2 = u32::from(((hw >> 3) & 7) + (((hw >> 6) & 1) << 3));
                match (hw >> 8) & 3 {
                    0 => format!("ADD {}, {}", arm_reg_name(r1), arm_reg_name(r2)),
                    1 => format!("CMP {}, {}", arm_reg_name(r1), arm_reg_name(r2)),
                    2 => format!("MOV {}, {}", arm_reg_name(r1), arm_reg_name(r2)),
                    _ => format!("BX {}", arm_reg_name(r2)),
                }
            } else if (hw & 0xF800) == 0x4800 {
                let off = u32::from(hw & 0xFF) << 2;
                let target = (addr.wrapping_add(4) & !3).wrapping_add(off);
                format!("LDR R{}, [${target:08X}]", (hw >> 8) & 7)
            } else {
                format!("THUMB2 0x{hw:04X}")
            }
        }
        3 => {
            let mnem = if (hw & (1 << 11)) != 0 { "LDR" } else { "STR" };
            let b = if (hw & (1 << 12)) != 0 { "B" } else { "" };
            let imm = if (hw & (1 << 12)) != 0 { (hw >> 6) & 0x1F } else { ((hw >> 6) & 0x1F) << 2 };
            format!("{mnem}{b} R{rd}, [R{rs}, #0x{imm:X}]")
        }
        5 => {
            if (hw & 0xF600) == 0xB400 {
                let mnem = if (hw & (1 << 11)) != 0 { "POP" } else { "PUSH" };
                format!("{mnem} {{#{:02X}{}}}", hw & 0xFF, if (hw & (1 << 8)) != 0 { "+LR/PC" } else { "" })
            } else {
                format!("THUMB5 0x{hw:04X}")
            }
        }
        6 => {
            if (hw & 0xFF00) == 0xDF00 {
                format!("SWI #0x{:02X}", hw & 0xFF)
            } else if (hw & 0xF000) == 0xD000 {
                let cond = arm_cond_str(u32::from((hw >> 8) & 0xF));
                let target = addr.wrapping_add(4).wrapping_add(((hw as i8 as i32) << 1) as u32);
                format!("B{cond} ${target:08X}")
            } else {
                format!("LDM/STM 0x{hw:04X}")
            }
        }
        7 => match (hw >> 11) & 3 {
            0 => {
                let off = ((((hw & 0x7FF) as i32) << 21) >> 20) as u32;
                let target = addr.wrapping_add(4).wrapping_add(off);
                format!("B ${target:08X}")
            }
            2 => format!("BL_HI #0x{:03X}", hw & 0x7FF),
            3 => format!("BL_LO #0x{:03X}", hw & 0x7FF),
            _ => format!("DCW 0x{hw:04X}"),
        },
        _ => format!("THUMB 0x{hw:04X}"),
    }
}
