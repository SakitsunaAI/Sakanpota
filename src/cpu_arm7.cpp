#include "cpu_arm7.h"
#include <cstring>
#include <iostream>

CPUARM7::CPUARM7() {
    reset();
}

void CPUARM7::reset() {
    for (int i = 0; i < 16; i++) r[i] = 0;
    cpsr = FLAG_I | FLAG_F | 0x13; // Supervisor mode (0x13), IRQ/FIQ disabled
    halted = false;
    totalCycles = 0;

    // Reset vector: 0x00000000 (starts in ARM mode)
    r[15] = 0x00000000;
}

bool CPUARM7::checkCondition(uint8 cond) const {
    switch (cond) {
        case 0:  return (cpsr & FLAG_Z) != 0;                        // EQ
        case 1:  return (cpsr & FLAG_Z) == 0;                        // NE
        case 2:  return (cpsr & FLAG_C) != 0;                        // CS / HS
        case 3:  return (cpsr & FLAG_C) == 0;                        // CC / LO
        case 4:  return (cpsr & FLAG_N) != 0;                        // MI
        case 5:  return (cpsr & FLAG_N) == 0;                        // PL
        case 6:  return (cpsr & FLAG_V) != 0;                        // VS
        case 7:  return (cpsr & FLAG_V) == 0;                        // VC
        case 8:  return ((cpsr & FLAG_C) != 0) && ((cpsr & FLAG_Z) == 0); // HI
        case 9:  return ((cpsr & FLAG_C) == 0) || ((cpsr & FLAG_Z) != 0); // LS
        case 10: return bool(cpsr & FLAG_N) == bool(cpsr & FLAG_V); // GE
        case 11: return bool(cpsr & FLAG_N) != bool(cpsr & FLAG_V); // LT
        case 12: return ((cpsr & FLAG_Z) == 0) && (bool(cpsr & FLAG_N) == bool(cpsr & FLAG_V)); // GT
        case 13: return ((cpsr & FLAG_Z) != 0) || (bool(cpsr & FLAG_N) != bool(cpsr & FLAG_V)); // LE
        case 14: return true;                                        // AL (Always)
        default: return false;                                       // NV (Never)
    }
}

int CPUARM7::step() {
    if (halted) return 1;
    if (isThumb()) {
        return stepThumb();
    } else {
        return stepARM();
    }
}

int CPUARM7::execute(int cycles) {
    int executed = 0;
    while (executed < cycles && !halted) {
        executed += step();
    }
    return executed;
}

int CPUARM7::stepThumb() {
    if (!read16) return 2;

    uint32 pc = r[15];
    uint16 op = read16(pc);
    r[15] += 2; // Advance PC
    totalCycles += 2;

    // 1. Shift by Immediate: 0x0000 - 0x17FF
    // 000xx xxxxx sss ddd
    if ((op & 0xE000) == 0x0000 && (op & 0x1800) != 0x1800) {
        uint8 shiftOp = (op >> 11) & 3;
        uint8 offset = (op >> 6) & 0x1F;
        uint8 rs = (op >> 3) & 7;
        uint8 rd = op & 7;
        uint32 val = r[rs];
        bool carry = (cpsr & FLAG_C) != 0;

        if (shiftOp == 0) { // LSL
            if (offset > 0) {
                carry = (val >> (32 - offset)) & 1;
                val <<= offset;
            }
        } else if (shiftOp == 1) { // LSR
            if (offset == 0) offset = 32;
            if (offset < 32) {
                carry = (val >> (offset - 1)) & 1;
                val >>= offset;
            } else if (offset == 32) {
                carry = (val >> 31) & 1;
                val = 0;
            } else {
                carry = false;
                val = 0;
            }
        } else if (shiftOp == 2) { // ASR
            if (offset == 0) offset = 32;
            int32 sval = (int32)val;
            if (offset < 32) {
                carry = (sval >> (offset - 1)) & 1;
                val = (uint32)(sval >> offset);
            } else {
                carry = (sval >> 31) & 1;
                val = (sval < 0) ? 0xFFFFFFFF : 0;
            }
        }
        r[rd] = val;
        setNZC(val, carry);
        return 2;
    }

    // 2. Add / Subtract register or 3-bit immediate: 0x1800 - 0x1FFF
    // 0001 1ioo nnn sss ddd
    if ((op & 0xF800) == 0x1800) {
        bool isImm = (op & 0x0400) != 0;
        bool isSub = (op & 0x0200) != 0;
        uint8 rn_imm = (op >> 6) & 7;
        uint8 rs = (op >> 3) & 7;
        uint8 rd = op & 7;

        uint32 op1 = r[rs];
        uint32 op2 = isImm ? rn_imm : r[rn_imm];
        uint32 res;
        bool carry, overflow;

        if (isSub) {
            res = op1 - op2;
            carry = op1 >= op2;
            overflow = ((op1 ^ op2) & (op1 ^ res) & 0x80000000) != 0;
        } else {
            res = op1 + op2;
            carry = res < op1;
            overflow = (~(op1 ^ op2) & (op1 ^ res) & 0x80000000) != 0;
        }
        r[rd] = res;
        setNZCV(res, carry, overflow);
        return 2;
    }

    // 3. Move / Compare / Add / Subtract Immediate: 0x2000 - 0x3FFF
    // 001oo ddd iiiiiiii
    if ((op & 0xE000) == 0x2000) {
        uint8 subOp = (op >> 11) & 3;
        uint8 rd = (op >> 8) & 7;
        uint32 imm = op & 0xFF;

        if (subOp == 0) { // MOV Rd, #imm8
            r[rd] = imm;
            setNZ(imm);
        } else if (subOp == 1) { // CMP Rd, #imm8
            uint32 op1 = r[rd];
            uint32 res = op1 - imm;
            bool carry = op1 >= imm;
            bool overflow = ((op1 ^ imm) & (op1 ^ res) & 0x80000000) != 0;
            setNZCV(res, carry, overflow);
        } else if (subOp == 2) { // ADD Rd, #imm8
            uint32 op1 = r[rd];
            uint32 res = op1 + imm;
            bool carry = res < op1;
            bool overflow = (~(op1 ^ imm) & (op1 ^ res) & 0x80000000) != 0;
            r[rd] = res;
            setNZCV(res, carry, overflow);
        } else if (subOp == 3) { // SUB Rd, #imm8
            uint32 op1 = r[rd];
            uint32 res = op1 - imm;
            bool carry = op1 >= imm;
            bool overflow = ((op1 ^ imm) & (op1 ^ res) & 0x80000000) != 0;
            r[rd] = res;
            setNZCV(res, carry, overflow);
        }
        return 2;
    }

    // 4. ALU Operations: 0x4000 - 0x43FF
    if ((op & 0xFC00) == 0x4000) {
        uint8 aluOp = (op >> 6) & 0x0F;
        uint8 rs = (op >> 3) & 7;
        uint8 rd = op & 7;
        uint32 a = r[rd];
        uint32 b = r[rs];
        uint32 res = 0;

        switch (aluOp) {
            case 0x0: // AND
                res = a & b; r[rd] = res; setNZ(res); break;
            case 0x1: // EOR
                res = a ^ b; r[rd] = res; setNZ(res); break;
            case 0x2: { // LSL
                uint8 shift = b & 0xFF;
                bool carry = (cpsr & FLAG_C) != 0;
                if (shift > 0 && shift < 32) {
                    carry = (a >> (32 - shift)) & 1;
                    res = a << shift;
                } else if (shift == 32) {
                    carry = a & 1;
                    res = 0;
                } else if (shift > 32) {
                    carry = false;
                    res = 0;
                } else {
                    res = a;
                }
                r[rd] = res; setNZC(res, carry); break;
            }
            case 0x3: { // LSR
                uint8 shift = b & 0xFF;
                bool carry = (cpsr & FLAG_C) != 0;
                if (shift > 0 && shift < 32) {
                    carry = (a >> (shift - 1)) & 1;
                    res = a >> shift;
                } else if (shift == 32) {
                    carry = (a >> 31) & 1;
                    res = 0;
                } else if (shift > 32) {
                    carry = false;
                    res = 0;
                } else {
                    res = a;
                }
                r[rd] = res; setNZC(res, carry); break;
            }
            case 0x4: { // ASR
                uint8 shift = b & 0xFF;
                bool carry = (cpsr & FLAG_C) != 0;
                int32 sa = (int32)a;
                if (shift > 0 && shift < 32) {
                    carry = (sa >> (shift - 1)) & 1;
                    res = (uint32)(sa >> shift);
                } else if (shift >= 32) {
                    carry = (sa >> 31) & 1;
                    res = (sa < 0) ? 0xFFFFFFFF : 0;
                } else {
                    res = a;
                }
                r[rd] = res; setNZC(res, carry); break;
            }
            case 0x5: { // ADC
                uint32 c = (cpsr & FLAG_C) ? 1 : 0;
                res = a + b + c;
                bool carry = (res < a) || (c && res == a);
                bool overflow = (~(a ^ b) & (a ^ res) & 0x80000000) != 0;
                r[rd] = res; setNZCV(res, carry, overflow); break;
            }
            case 0x6: { // SBC
                uint32 c = (cpsr & FLAG_C) ? 0 : 1; // Borrow
                res = a - b - c;
                bool carry = (uint64)a >= ((uint64)b + c);
                bool overflow = ((a ^ b) & (a ^ res) & 0x80000000) != 0;
                r[rd] = res; setNZCV(res, carry, overflow); break;
            }
            case 0x7: { // ROR
                uint8 shift = b & 0x1F;
                bool carry = (cpsr & FLAG_C) != 0;
                if (shift > 0) {
                    carry = (a >> (shift - 1)) & 1;
                    res = (a >> shift) | (a << (32 - shift));
                } else {
                    res = a;
                }
                r[rd] = res; setNZC(res, carry); break;
            }
            case 0x8: // TST
                res = a & b; setNZ(res); break;
            case 0x9: { // NEG
                res = 0 - b;
                bool carry = (0 >= b);
                bool overflow = (b & res & 0x80000000) != 0;
                r[rd] = res; setNZCV(res, carry, overflow); break;
            }
            case 0xA: { // CMP
                res = a - b;
                bool carry = a >= b;
                bool overflow = ((a ^ b) & (a ^ res) & 0x80000000) != 0;
                setNZCV(res, carry, overflow); break;
            }
            case 0xB: { // CMN
                res = a + b;
                bool carry = res < a;
                bool overflow = (~(a ^ b) & (a ^ res) & 0x80000000) != 0;
                setNZCV(res, carry, overflow); break;
            }
            case 0xC: // ORR
                res = a | b; r[rd] = res; setNZ(res); break;
            case 0xD: // MUL
                res = a * b; r[rd] = res; setNZ(res); break;
            case 0xE: // BIC
                res = a & (~b); r[rd] = res; setNZ(res); break;
            case 0xF: // MVN
                res = ~b; r[rd] = res; setNZ(res); break;
        }
        return 2;
    }

    // 5. Hi register operations / Branch exchange: 0x4400 - 0x47FF
    if ((op & 0xFC00) == 0x4400) {
        uint8 subOp = (op >> 8) & 3;
        uint8 h1 = (op >> 7) & 1;
        uint8 h2 = (op >> 6) & 1;
        uint8 rs = ((op >> 3) & 7) | (h2 << 3);
        uint8 rd = (op & 7) | (h1 << 3);

        if (subOp == 0) { // ADD
            r[rd] += r[rs];
        } else if (subOp == 1) { // CMP
            uint32 a = r[rd];
            uint32 b = r[rs];
            uint32 res = a - b;
            bool carry = a >= b;
            bool overflow = ((a ^ b) & (a ^ res) & 0x80000000) != 0;
            setNZCV(res, carry, overflow);
        } else if (subOp == 2) { // MOV
            r[rd] = r[rs];
        } else if (subOp == 3) { // BX
            uint32 target = r[rs];
            setThumb((target & 1) != 0);
            r[15] = target & ~1;
        }
        return 2;
    }

    // 6. PC-relative Load: 0x4800 - 0x4FFF (LDR Rd, [PC, #imm8 * 4])
    if ((op & 0xF800) == 0x4800) {
        uint8 rd = (op >> 8) & 7;
        uint32 imm = (op & 0xFF) * 4;
        uint32 addr = ((pc + 4) & ~3) + imm;
        if (read32) r[rd] = read32(addr);
        return 3;
    }

    // 7. Load / Store with register offset: 0x5000 - 0x5FFF
    if ((op & 0xF200) == 0x5000) {
        uint8 ro = (op >> 6) & 7;
        uint8 rb = (op >> 3) & 7;
        uint8 rd = op & 7;
        uint32 addr = r[rb] + r[ro];

        if ((op & 0x0C00) == 0x0000) { // STR
            if (write32) write32(addr, r[rd]);
        } else if ((op & 0x0C00) == 0x0400) { // STRB
            if (write8) write8(addr, (uint8)r[rd]);
        } else if ((op & 0x0C00) == 0x0800) { // LDR
            if (read32) r[rd] = read32(addr);
        } else if ((op & 0x0C00) == 0x0C00) { // LDRB
            if (read8) r[rd] = read8(addr);
        }
        return 2;
    }

    // 8. Load / Store with immediate offset: 0x6000 - 0x7FFF
    if ((op & 0xE000) == 0x6000) {
        bool isByte = (op & 0x1000) != 0;
        bool isLoad = (op & 0x0800) != 0;
        uint32 imm = (op >> 6) & 0x1F;
        uint8 rb = (op >> 3) & 7;
        uint8 rd = op & 7;
        uint32 addr = r[rb] + (isByte ? imm : (imm * 4));

        if (isLoad) {
            if (isByte) {
                if (read8) r[rd] = read8(addr);
            } else {
                if (read32) r[rd] = read32(addr);
            }
        } else {
            if (isByte) {
                if (write8) write8(addr, (uint8)r[rd]);
            } else {
                if (write32) write32(addr, r[rd]);
            }
        }
        return 2;
    }

    // 9. Load / Store Halfword: 0x8000 - 0x8FFF
    if ((op & 0xF000) == 0x8000) {
        bool isLoad = (op & 0x0800) != 0;
        uint32 imm = ((op >> 6) & 0x1F) * 2;
        uint8 rb = (op >> 3) & 7;
        uint8 rd = op & 7;
        uint32 addr = r[rb] + imm;

        if (isLoad) {
            if (read16) r[rd] = read16(addr);
        } else {
            if (write16) write16(addr, (uint16)r[rd]);
        }
        return 2;
    }

    // 10. SP-relative Load / Store: 0x9000 - 0x9FFF
    if ((op & 0xF000) == 0x9000) {
        bool isLoad = (op & 0x0800) != 0;
        uint8 rd = (op >> 8) & 7;
        uint32 imm = (op & 0xFF) * 4;
        uint32 addr = r[13] + imm;

        if (isLoad) {
            if (read32) r[rd] = read32(addr);
        } else {
            if (write32) write32(addr, r[rd]);
        }
        return 2;
    }

    // 11. Load Address: 0xA000 - 0xAFFF (ADD Rd, PC/SP, #imm8 * 4)
    if ((op & 0xF000) == 0xA000) {
        bool isSP = (op & 0x0800) != 0;
        uint8 rd = (op >> 8) & 7;
        uint32 imm = (op & 0xFF) * 4;
        r[rd] = (isSP ? r[13] : ((pc + 4) & ~3)) + imm;
        return 2;
    }

    // 12. Add offset to SP: 0xB000 - 0xB0FF
    if ((op & 0xFF00) == 0xB000) {
        bool isSub = (op & 0x0080) != 0;
        uint32 imm = (op & 0x7F) * 4;
        if (isSub) r[13] -= imm;
        else       r[13] += imm;
        return 2;
    }

    // 13. Push / Pop Registers: 0xB400 - 0xBDFF
    if ((op & 0xF600) == 0xB400) {
        bool isPop = (op & 0x0800) != 0;
        bool pclR = (op & 0x0100) != 0;
        uint8 rlist = op & 0xFF;

        if (isPop) { // POP
            for (int i = 0; i < 8; i++) {
                if (rlist & (1 << i)) {
                    if (read32) r[i] = read32(r[13]);
                    r[13] += 4;
                }
            }
            if (pclR) { // Pop PC
                if (read32) {
                    uint32 target = read32(r[13]);
                    setThumb((target & 1) != 0);
                    r[15] = target & ~1;
                }
                r[13] += 4;
            }
        } else { // PUSH
            if (pclR) { // Push LR
                r[13] -= 4;
                if (write32) write32(r[13], r[14]);
            }
            for (int i = 7; i >= 0; i--) {
                if (rlist & (1 << i)) {
                    r[13] -= 4;
                    if (write32) write32(r[13], r[i]);
                }
            }
        }
        return 4;
    }

    // 14. Multiple Load / Store: 0xC000 - 0xCFFF (STMIA / LDMIA)
    if ((op & 0xF000) == 0xC000) {
        bool isLoad = (op & 0x0800) != 0;
        uint8 rb = (op >> 8) & 7;
        uint8 rlist = op & 0xFF;
        uint32 addr = r[rb];

        for (int i = 0; i < 8; i++) {
            if (rlist & (1 << i)) {
                if (isLoad) {
                    if (read32) r[i] = read32(addr);
                } else {
                    if (write32) write32(addr, r[i]);
                }
                addr += 4;
            }
        }
        r[rb] = addr;
        return 4;
    }

    // 15. Conditional Branch: 0xD000 - 0xDFFF (B<cond> label)
    if ((op & 0xF000) == 0xD000) {
        uint8 cond = (op >> 8) & 0x0F;
        if (cond == 0x0F) { // SWI (Software Interrupt)
            return 4;
        }
        if (checkCondition(cond)) {
            int32 offset = (int8)(op & 0xFF) * 2;
            r[15] = pc + 4 + offset;
        }
        return 3;
    }

    // 16. Unconditional Branch: 0xE000 - 0xE7FF (B label)
    if ((op & 0xF800) == 0xE000) {
        int32 offset = (op & 0x07FF);
        if (offset & 0x0400) offset |= ~0x07FF; // Sign extend 11-bit
        r[15] = pc + 4 + (offset * 2);
        return 3;
    }

    // 17. Long Branch with Link: 0xF000 - 0xFFFF (BL label)
    if ((op & 0xF000) == 0xF000) {
        bool isSecond = (op & 0x0800) != 0;
        uint32 offset = op & 0x07FF;

        if (!isSecond) { // First instruction: setup LR with high 11 bits
            if (offset & 0x0400) offset |= ~0x07FF;
            r[14] = (pc + 4) + (offset << 12);
        } else { // Second instruction: jump and link
            uint32 nextPC = pc + 2;
            r[15] = r[14] + (offset * 2);
            r[14] = nextPC | 1; // Thumb bit
        }
        return 4;
    }

    return 2;
}

int CPUARM7::stepARM() {
    if (!read32) return 4;

    uint32 pc = r[15];
    uint32 op = read32(pc);
    r[15] += 4; // Advance PC
    totalCycles += 4;

    // Check condition code
    uint8 cond = (op >> 28) & 0x0F;
    if (!checkCondition(cond)) return 4;

    // Branch / Branch with Link (B / BL)
    if ((op & 0x0E000000) == 0x0A000000) {
        bool link = (op & 0x01000000) != 0;
        int32 offset = (op & 0x00FFFFFF);
        if (offset & 0x00800000) offset |= ~0x00FFFFFF;
        offset <<= 2;

        if (link) {
            r[14] = pc + 4;
        }
        r[15] = pc + 8 + offset;
        return 4;
    }

    // Branch and Exchange (BX)
    if ((op & 0x0FFFFFF0) == 0x012FFF10) {
        uint8 rn = op & 0x0F;
        uint32 target = r[rn];
        setThumb((target & 1) != 0);
        r[15] = target & ~1;
        return 4;
    }

    // Data Processing
    if ((op & 0x0C000000) == 0x00000000) {
        uint8 aluOp = (op >> 21) & 0x0F;
        bool setFlags = (op & 0x00100000) != 0;
        uint8 rn = (op >> 16) & 0x0F;
        uint8 rd = (op >> 12) & 0x0F;

        uint32 op2;
        if (op & 0x02000000) { // Immediate
            uint32 imm = op & 0xFF;
            uint8 rot = ((op >> 8) & 0x0F) * 2;
            op2 = (rot == 0) ? imm : ((imm >> rot) | (imm << (32 - rot)));
        } else {
            op2 = r[op & 0x0F];
        }

        uint32 op1 = r[rn];
        uint32 res = 0;

        switch (aluOp) {
            case 0x0: res = op1 & op2; break; // AND
            case 0x1: res = op1 ^ op2; break; // EOR
            case 0x2: res = op1 - op2; break; // SUB
            case 0x4: res = op1 + op2; break; // ADD
            case 0xA: res = op1 - op2; break; // CMP
            case 0xD: res = op2; break;       // MOV
            case 0xF: res = ~op2; break;      // MVN
            default:  res = op1 + op2; break;
        }

        if (aluOp != 0x8 && aluOp != 0x9 && aluOp != 0xA && aluOp != 0xB) {
            r[rd] = res;
        }
        if (setFlags) {
            setNZ(res);
        }
        return 4;
    }

    // Single Data Transfer (LDR / STR)
    if ((op & 0x0C000000) == 0x04000000) {
        bool isLoad = (op & 0x00100000) != 0;
        bool isByte = (op & 0x00400000) != 0;
        bool isUp   = (op & 0x00800000) != 0;
        uint8 rn = (op >> 16) & 0x0F;
        uint8 rd = (op >> 12) & 0x0F;
        uint32 offset = op & 0xFFF;

        uint32 addr = r[rn] + (isUp ? offset : -offset);
        if (isLoad) {
            if (isByte) {
                if (read8) r[rd] = read8(addr);
            } else {
                if (read32) r[rd] = read32(addr);
            }
        } else {
            if (isByte) {
                if (write8) write8(addr, (uint8)r[rd]);
            } else {
                if (write32) write32(addr, r[rd]);
            }
        }
        return 4;
    }

    return 4;
}
