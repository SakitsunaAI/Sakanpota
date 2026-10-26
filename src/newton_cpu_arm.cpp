#include "newton_cpu_arm.h"
#include <cstring>
#include <iostream>

NewtonCPUARM::NewtonCPUARM() {
    reset();
}

void NewtonCPUARM::reset() {
    for (int i = 0; i < 16; i++) r[i] = 0;
    for (int i = 0; i < 7; i++) {
        r_user[i] = 0;
        r_fiq[i] = 0;
    }
    r_svc[0] = r_svc[1] = 0;
    r_abt[0] = r_abt[1] = 0;
    r_irq[0] = r_irq[1] = 0;
    r_und[0] = r_und[1] = 0;

    cpsr = FLAG_I | FLAG_F | MODE_SVC;
    spsr = 0;
    spsr_fiq = spsr_svc = spsr_abt = spsr_irq = spsr_und = 0;

    // Reset CP15 for ARM610
    cp15.id = 0x41560610; // ARM610 ID code
    cp15.control = 0x00000070; // Reset control bits
    cp15.ttb = 0;
    cp15.domainAccess = 0;
    cp15.faultStatus = 0;
    cp15.faultAddress = 0;

    halted = false;
    totalCycles = 0;

    // Exception vector 0x00000000
    r[15] = 0x00000000;
}

void NewtonCPUARM::setMode(uint8 newMode) {
    uint8 oldMode = cpsr & 0x1F;
    if (oldMode == newMode) return;

    // Save old mode banked registers
    if (oldMode == MODE_FIQ) {
        for (int i = 0; i < 7; i++) r_fiq[i] = r[8 + i];
    } else {
        for (int i = 0; i < 5; i++) r_user[i] = r[8 + i];
        if (oldMode == MODE_USER || oldMode == MODE_SYS) {
            r_user[5] = r[13];
            r_user[6] = r[14];
        } else if (oldMode == MODE_SVC) {
            r_svc[0] = r[13]; r_svc[1] = r[14];
        } else if (oldMode == MODE_ABT) {
            r_abt[0] = r[13]; r_abt[1] = r[14];
        } else if (oldMode == MODE_IRQ) {
            r_irq[0] = r[13]; r_irq[1] = r[14];
        } else if (oldMode == MODE_UND) {
            r_und[0] = r[13]; r_und[1] = r[14];
        }
    }

    // Restore new mode banked registers
    if (newMode == MODE_FIQ) {
        for (int i = 0; i < 7; i++) r[8 + i] = r_fiq[i];
    } else {
        for (int i = 0; i < 5; i++) r[8 + i] = r_user[i];
        if (newMode == MODE_USER || newMode == MODE_SYS) {
            r[13] = r_user[5];
            r[14] = r_user[6];
        } else if (newMode == MODE_SVC) {
            r[13] = r_svc[0]; r[14] = r_svc[1];
        } else if (newMode == MODE_ABT) {
            r[13] = r_abt[0]; r[14] = r_abt[1];
        } else if (newMode == MODE_IRQ) {
            r[13] = r_irq[0]; r[14] = r_irq[1];
        } else if (newMode == MODE_UND) {
            r[13] = r_und[0]; r[14] = r_und[1];
        }
    }

    cpsr = (cpsr & ~0x1F) | (newMode & 0x1F);
}

void NewtonCPUARM::triggerIRQ() {
    if (cpsr & FLAG_I) return; // IRQs disabled

    uint32 returnAddr = r[15] + 4;
    uint32 oldCPSR = cpsr;
    setMode(MODE_IRQ);
    spsr_irq = oldCPSR;
    r[14] = returnAddr;
    cpsr |= FLAG_I; // Disable further IRQs
    r[15] = 0x00000018; // IRQ vector
}

void NewtonCPUARM::triggerFIQ() {
    if (cpsr & FLAG_F) return; // FIQs disabled

    uint32 returnAddr = r[15] + 4;
    uint32 oldCPSR = cpsr;
    setMode(MODE_FIQ);
    spsr_fiq = oldCPSR;
    r[14] = returnAddr;
    cpsr |= FLAG_F | FLAG_I;
    r[15] = 0x0000001C; // FIQ vector
}

uint32 NewtonCPUARM::translateAddress(uint32 vaddr, bool isWrite, bool& fault) {
    fault = false;
    if (!isMMUEnabled()) {
        return vaddr; // Flat physical memory mapping when MMU is off
    }

    // MMU Translation using Translation Table Base (TTB)
    if (!read32) return vaddr;

    uint32 l1_index = vaddr >> 20;
    uint32 l1_desc_addr = (cp15.ttb & 0xFFFFC000) | (l1_index << 2);
    uint32 l1_desc = read32(l1_desc_addr);

    uint8 type = l1_desc & 3;
    if (type == 2) {
        // Section translation (1 MB page)
        uint32 paddr = (l1_desc & 0xFFF00000) | (vaddr & 0x000FFFFF);
        return paddr;
    } else if (type == 1) {
        // Coarse page table (4KB / 64KB pages)
        uint32 pt_base = l1_desc & 0xFFFFFC00;
        uint32 l2_index = (vaddr >> 12) & 0xFF;
        uint32 l2_desc_addr = pt_base | (l2_index << 2);
        uint32 l2_desc = read32(l2_desc_addr);

        uint8 l2_type = l2_desc & 3;
        if (l2_type == 2) {
            // Small page (4KB)
            uint32 paddr = (l2_desc & 0xFFFFF000) | (vaddr & 0x00000FFF);
            return paddr;
        } else if (l2_type == 1) {
            // Large page (64KB)
            uint32 paddr = (l2_desc & 0xFFFF0000) | (vaddr & 0x0000FFFF);
            return paddr;
        }
    }

    // Direct fallback if unmapped descriptor in emulation
    return vaddr;
}

bool NewtonCPUARM::checkCondition(uint8 cond) const {
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

uint32 NewtonCPUARM::decodeShift(uint32 op, bool& carryOut) {
    uint8 rm = op & 0x0F;
    uint32 val = r[rm];
    uint8 shiftType = (op >> 5) & 3;
    uint8 shiftAmount = 0;

    carryOut = (cpsr & FLAG_C) != 0;

    if (op & 0x00000010) { // Shift amount specified by register
        uint8 rs = (op >> 8) & 0x0F;
        shiftAmount = r[rs] & 0xFF;
    } else { // Shift amount specified by immediate
        shiftAmount = (op >> 7) & 0x1F;
    }

    if (shiftAmount == 0) {
        if ((op & 0x00000010) == 0) {
            // Immediate shift of 0 has special cases
            if (shiftType == 1) { // LSR #32
                carryOut = (val >> 31) & 1;
                return 0;
            } else if (shiftType == 2) { // ASR #32
                carryOut = (val >> 31) & 1;
                return (val & 0x80000000) ? 0xFFFFFFFF : 0;
            } else if (shiftType == 3) { // RRX #1
                bool oldC = carryOut;
                carryOut = val & 1;
                return (val >> 1) | (oldC ? 0x80000000 : 0);
            }
        }
        return val;
    }

    switch (shiftType) {
        case 0: // LSL
            if (shiftAmount < 32) {
                carryOut = (val >> (32 - shiftAmount)) & 1;
                val <<= shiftAmount;
            } else if (shiftAmount == 32) {
                carryOut = val & 1;
                val = 0;
            } else {
                carryOut = false;
                val = 0;
            }
            break;
        case 1: // LSR
            if (shiftAmount < 32) {
                carryOut = (val >> (shiftAmount - 1)) & 1;
                val >>= shiftAmount;
            } else if (shiftAmount == 32) {
                carryOut = (val >> 31) & 1;
                val = 0;
            } else {
                carryOut = false;
                val = 0;
            }
            break;
        case 2: // ASR
            if (shiftAmount < 32) {
                carryOut = ((int32)val >> (shiftAmount - 1)) & 1;
                val = (uint32)((int32)val >> shiftAmount);
            } else {
                carryOut = (val >> 31) & 1;
                val = (val & 0x80000000) ? 0xFFFFFFFF : 0;
            }
            break;
        case 3: // ROR
            shiftAmount %= 32;
            if (shiftAmount > 0) {
                carryOut = (val >> (shiftAmount - 1)) & 1;
                val = (val >> shiftAmount) | (val << (32 - shiftAmount));
            }
            break;
    }

    return val;
}

void NewtonCPUARM::handleMCR(uint32 op) {
    uint8 cpNum = (op >> 8) & 0x0F;
    if (cpNum != 15) return; // Only CP15 system controller supported

    uint8 crn = (op >> 16) & 0x0F;
    uint8 rd = (op >> 12) & 0x0F;
    uint32 val = r[rd];

    switch (crn) {
        case 1: // Control register
            cp15.control = val;
            break;
        case 2: // Translation Table Base (TTB)
            cp15.ttb = val;
            break;
        case 3: // Domain Access Control
            cp15.domainAccess = val;
            break;
        case 5: // Fault Status Register
            cp15.faultStatus = val;
            break;
        case 6: // Fault Address Register
            cp15.faultAddress = val;
            break;
        case 7: // Flush Cache
        case 8: // Flush TLB
            break;
        default:
            break;
    }
}

void NewtonCPUARM::handleMRC(uint32 op) {
    uint8 cpNum = (op >> 8) & 0x0F;
    if (cpNum != 15) return;

    uint8 crn = (op >> 16) & 0x0F;
    uint8 rd = (op >> 12) & 0x0F;

    uint32 val = 0;
    switch (crn) {
        case 0: // ID Code (ARM610 = 0x41560610)
            val = cp15.id;
            break;
        case 1: // Control Register
            val = cp15.control;
            break;
        case 2: // TTB
            val = cp15.ttb;
            break;
        case 3: // Domain Access
            val = cp15.domainAccess;
            break;
        case 5: // Fault Status
            val = cp15.faultStatus;
            break;
        case 6: // Fault Address
            val = cp15.faultAddress;
            break;
        default:
            val = 0;
            break;
    }

    if (rd == 15) {
        // Write to CPSR condition flags
        cpsr = (cpsr & ~0xF0000000) | (val & 0xF0000000);
    } else {
        r[rd] = val;
    }
}

int NewtonCPUARM::step() {
    if (halted || !read32) return 1;

    uint32 pc = r[15];
    bool fault = false;
    uint32 physPC = translateAddress(pc, false, fault);
    uint32 op = read32(physPC);

    r[15] += 4; // Advance PC
    totalCycles += 4;

    uint8 cond = (op >> 28) & 0x0F;
    if (!checkCondition(cond)) return 4;

    // 1. Branch / Branch with Link (B, BL)
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

    // 2. Branch and Exchange (BX)
    if ((op & 0x0FFFFFF0) == 0x012FFF10) {
        uint8 rn = op & 0x0F;
        r[15] = r[rn] & ~3;
        return 4;
    }

    // 3. Software Interrupt (SWI)
    if ((op & 0x0F000000) == 0x0F000000) {
        uint32 oldCPSR = cpsr;
        setMode(MODE_SVC);
        spsr_svc = oldCPSR;
        r[14] = pc + 4;
        cpsr |= FLAG_I; // Disable IRQ
        r[15] = 0x00000008; // SWI vector
        return 8;
    }

    // 4. Coprocessor Register Transfer (MCR, MRC)
    if ((op & 0x0F000010) == 0x0E000010) {
        bool isMRC = (op & 0x00100000) != 0;
        if (isMRC) handleMRC(op);
        else       handleMCR(op);
        return 4;
    }

    // 5. Multiply / Multiply Accumulate (MUL, MLA)
    if ((op & 0x0FC000F0) == 0x00000090) {
        bool accumulate = (op & 0x00200000) != 0;
        bool setFlags = (op & 0x00100000) != 0;
        uint8 rd = (op >> 16) & 0x0F;
        uint8 rn = (op >> 12) & 0x0F;
        uint8 rs = (op >> 8) & 0x0F;
        uint8 rm = op & 0x0F;

        uint32 res = r[rm] * r[rs];
        if (accumulate) res += r[rn];
        r[rd] = res;

        if (setFlags) {
            setNZ(res);
        }
        return 8;
    }

    // 6. Status Register Operations (MRS, MSR)
    if ((op & 0x0FBF0FFF) == 0x010F0000) { // MRS
        bool isSPSR = (op & 0x00400000) != 0;
        uint8 rd = (op >> 12) & 0x0F;
        r[rd] = isSPSR ? spsr : cpsr;
        return 4;
    }
    if ((op & 0x0DB0F000) == 0x0120F000) { // MSR
        bool isSPSR = (op & 0x00400000) != 0;
        uint32 val;
        if (op & 0x02000000) {
            uint32 imm = op & 0xFF;
            uint8 rot = ((op >> 8) & 0x0F) * 2;
            val = (rot == 0) ? imm : ((imm >> rot) | (imm << (32 - rot)));
        } else {
            val = r[op & 0x0F];
        }

        uint32 mask = 0;
        if (op & 0x00080000) mask |= 0xFF000000; // Flags (f)
        if (op & 0x00040000) mask |= 0x00FF0000; // Status (s)
        if (op & 0x00020000) mask |= 0x0000FF00; // Extension (x)
        if (op & 0x00010000) mask |= 0x000000FF; // Control (c)
        if (mask == 0) mask = 0xF0000000; // Default flags only

        if (isSPSR) {
            spsr = (spsr & ~mask) | (val & mask);
        } else {
            if ((cpsr & 0x1F) == MODE_USER) mask &= 0xF0000000; // User mode can only change flags
            uint32 newCPSR = (cpsr & ~mask) | (val & mask);
            if ((newCPSR & 0x1F) != (cpsr & 0x1F)) {
                setMode(newCPSR & 0x1F);
            }
            cpsr = newCPSR;
        }
        return 4;
    }

    // 7. Block Data Transfer (LDM, STM)
    if ((op & 0x0E000000) == 0x08000000) {
        bool preIndex   = (op & 0x01000000) != 0;
        bool up         = (op & 0x00800000) != 0;
        bool sBit       = (op & 0x00400000) != 0;
        bool writeback  = (op & 0x00200000) != 0;
        bool isLoad     = (op & 0x00100000) != 0;
        uint8 rn        = (op >> 16) & 0x0F;
        uint16 rlist    = op & 0xFFFF;

        uint32 addr = r[rn];
        int regCount = 0;
        for (int i = 0; i < 16; i++) {
            if (rlist & (1 << i)) regCount++;
        }

        uint32 startAddr = addr;
        if (!up) {
            startAddr = addr - (regCount * 4);
            if (!preIndex) startAddr += 4;
            addr = startAddr;
        } else if (preIndex) {
            addr += 4;
        }

        for (int i = 0; i < 16; i++) {
            if (rlist & (1 << i)) {
                uint32 physAddr = translateAddress(addr, isLoad ? false : true, fault);
                if (isLoad) {
                    if (read32) r[i] = read32(physAddr);
                    if (i == 15 && sBit) {
                        // Return from exception: restore CPSR from SPSR
                        cpsr = spsr;
                        setMode(cpsr & 0x1F);
                    }
                } else {
                    if (write32) write32(physAddr, r[i]);
                }
                addr += 4;
            }
        }

        if (writeback) {
            if (up) r[rn] += regCount * 4;
            else    r[rn] -= regCount * 4;
        }

        return 4 + regCount * 2;
    }

    // 8. Single Data Transfer (LDR, STR, LDRB, STRB)
    if ((op & 0x0C000000) == 0x04000000) {
        bool immediate  = (op & 0x02000000) == 0;
        bool preIndex   = (op & 0x01000000) != 0;
        bool up         = (op & 0x00800000) != 0;
        bool isByte     = (op & 0x00400000) != 0;
        bool writeback  = (op & 0x00200000) != 0;
        bool isLoad     = (op & 0x00100000) != 0;
        uint8 rn        = (op >> 16) & 0x0F;
        uint8 rd        = (op >> 12) & 0x0F;

        uint32 offset = 0;
        if (immediate) {
            offset = op & 0x0FFF;
        } else {
            bool c;
            offset = decodeShift(op, c);
        }

        uint32 baseAddr = (rn == 15) ? (pc + 8) : r[rn];
        uint32 effectiveAddr = preIndex ? (up ? (baseAddr + offset) : (baseAddr - offset)) : baseAddr;
        uint32 physAddr = translateAddress(effectiveAddr, !isLoad, fault);

        if (isLoad) {
            if (isByte) {
                if (read8) r[rd] = read8(physAddr);
            } else {
                if (read32) {
                    uint32 val = read32(physAddr & ~3);
                    uint8 align = (physAddr & 3) * 8;
                    if (align != 0) {
                        val = (val >> align) | (val << (32 - align));
                    }
                    r[rd] = val;
                }
            }
        } else {
            uint32 storeVal = (rd == 15) ? (pc + 12) : r[rd];
            if (isByte) {
                if (write8) write8(physAddr, (uint8)storeVal);
            } else {
                if (write32) write32(physAddr & ~3, storeVal);
            }
        }

        if (!preIndex) { // Post-indexing
            r[rn] = up ? (baseAddr + offset) : (baseAddr - offset);
        } else if (writeback) {
            r[rn] = effectiveAddr;
        }

        return 4;
    }

    // 9. Data Processing Operations
    if ((op & 0x0C000000) == 0x00000000) {
        bool immediate  = (op & 0x02000000) != 0;
        uint8 aluOp     = (op >> 21) & 0x0F;
        bool setFlags   = (op & 0x00100000) != 0;
        uint8 rn        = (op >> 16) & 0x0F;
        uint8 rd        = (op >> 12) & 0x0F;

        bool carryOut = (cpsr & FLAG_C) != 0;
        uint32 op2;

        if (immediate) {
            uint32 imm = op & 0xFF;
            uint8 rot = ((op >> 8) & 0x0F) * 2;
            op2 = (rot == 0) ? imm : ((imm >> rot) | (imm << (32 - rot)));
            if (rot != 0) {
                carryOut = (op2 >> 31) & 1;
            }
        } else {
            op2 = decodeShift(op, carryOut);
        }

        uint32 op1 = (rn == 15) ? (pc + 8) : r[rn];
        uint32 res = 0;
        bool carry = carryOut;
        bool overflow = false;

        switch (aluOp) {
            case 0x0: res = op1 & op2; break;                         // AND
            case 0x1: res = op1 ^ op2; break;                         // EOR
            case 0x2: {                                               // SUB
                res = op1 - op2;
                carry = op1 >= op2;
                overflow = ((op1 ^ op2) & (op1 ^ res) & 0x80000000) != 0;
                break;
            }
            case 0x3: {                                               // RSB
                res = op2 - op1;
                carry = op2 >= op1;
                overflow = ((op2 ^ op1) & (op2 ^ res) & 0x80000000) != 0;
                break;
            }
            case 0x4: {                                               // ADD
                uint64 sum = (uint64)op1 + (uint64)op2;
                res = (uint32)sum;
                carry = sum > 0xFFFFFFFF;
                overflow = (~(op1 ^ op2) & (op1 ^ res) & 0x80000000) != 0;
                break;
            }
            case 0x5: {                                               // ADC
                uint32 cIn = (cpsr & FLAG_C) ? 1 : 0;
                uint64 sum = (uint64)op1 + (uint64)op2 + cIn;
                res = (uint32)sum;
                carry = sum > 0xFFFFFFFF;
                overflow = (~(op1 ^ op2) & (op1 ^ res) & 0x80000000) != 0;
                break;
            }
            case 0x6: {                                               // SBC
                uint32 cIn = (cpsr & FLAG_C) ? 1 : 0;
                uint64 sub = (uint64)op1 - (uint64)op2 - (1 - cIn);
                res = (uint32)sub;
                carry = (uint64)op1 >= ((uint64)op2 + (1 - cIn));
                overflow = ((op1 ^ op2) & (op1 ^ res) & 0x80000000) != 0;
                break;
            }
            case 0x7: {                                               // RSC
                uint32 cIn = (cpsr & FLAG_C) ? 1 : 0;
                uint64 sub = (uint64)op2 - (uint64)op1 - (1 - cIn);
                res = (uint32)sub;
                carry = (uint64)op2 >= ((uint64)op1 + (1 - cIn));
                overflow = ((op2 ^ op1) & (op2 ^ res) & 0x80000000) != 0;
                break;
            }
            case 0x8: res = op1 & op2; break;                         // TST
            case 0x9: res = op1 ^ op2; break;                         // TEQ
            case 0xA: {                                               // CMP
                res = op1 - op2;
                carry = op1 >= op2;
                overflow = ((op1 ^ op2) & (op1 ^ res) & 0x80000000) != 0;
                break;
            }
            case 0xB: {                                               // CMN
                uint64 sum = (uint64)op1 + (uint64)op2;
                res = (uint32)sum;
                carry = sum > 0xFFFFFFFF;
                overflow = (~(op1 ^ op2) & (op1 ^ res) & 0x80000000) != 0;
                break;
            }
            case 0xC: res = op1 | op2; break;                         // ORR
            case 0xD: res = op2; break;                               // MOV
            case 0xE: res = op1 & ~op2; break;                        // BIC
            case 0xF: res = ~op2; break;                              // MVN
        }

        if (aluOp != 0x8 && aluOp != 0x9 && aluOp != 0xA && aluOp != 0xB) {
            r[rd] = res;
        }

        if (setFlags) {
            if (rd == 15) {
                // Restore CPSR from SPSR
                cpsr = spsr;
                setMode(cpsr & 0x1F);
            } else {
                if (aluOp == 0x2 || aluOp == 0x3 || aluOp == 0x4 || aluOp == 0x5 ||
                    aluOp == 0x6 || aluOp == 0x7 || aluOp == 0xA || aluOp == 0xB) {
                    setNZCV(res, carry, overflow);
                } else {
                    setNZC(res, carryOut);
                }
            }
        }

        return 4;
    }

    return 4;
}

int NewtonCPUARM::execute(int cycles) {
    int executed = 0;
    while (executed < cycles && !halted) {
        executed += step();
    }
    return executed;
}
