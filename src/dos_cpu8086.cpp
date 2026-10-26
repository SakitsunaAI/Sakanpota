#include "dos_cpu8086.h"
#include <iostream>

CPU8086::CPU8086() {
    reset();
}

void CPU8086::reset() {
    AX = 0x0000;
    BX = 0x0000;
    CX = 0x0000;
    DX = 0x0000;
    SP = 0xFFFE;
    BP = 0x0000;
    SI = 0x0000;
    DI = 0x0000;
    CS = 0x1000;
    DS = 0x1000;
    SS = 0x1000;
    ES = 0x1000;
    IP = 0x0100; // Standard COM start
    FLAGS = 0x0202; // IF=1
    halted = false;
    totalCycles = 0;
}

uint8 CPU8086::memRead8(uint16 seg, uint16 off) {
    if (readByte) return readByte(getPhysAddr(seg, off));
    return 0xFF;
}

uint16 CPU8086::memRead16(uint16 seg, uint16 off) {
    uint8 lo = memRead8(seg, off);
    uint8 hi = memRead8(seg, off + 1);
    return ((uint16)hi << 8) | lo;
}

void CPU8086::memWrite8(uint16 seg, uint16 off, uint8 val) {
    if (writeByte) writeByte(getPhysAddr(seg, off), val);
}

void CPU8086::memWrite16(uint16 seg, uint16 off, uint16 val) {
    memWrite8(seg, off, (uint8)(val & 0xFF));
    memWrite8(seg, off + 1, (uint8)(val >> 8));
}

void CPU8086::push16(uint16 val) {
    SP -= 2;
    memWrite16(SS, SP, val);
}

uint16 CPU8086::pop16() {
    uint16 val = memRead16(SS, SP);
    SP += 2;
    return val;
}

void CPU8086::updateZSP8(uint8 val) {
    setFlag(FLAG_ZF, val == 0);
    setFlag(FLAG_SF, (val & 0x80) != 0);
    // Parity
    uint8 p = val ^ (val >> 4);
    p ^= (p >> 2);
    p ^= (p >> 1);
    setFlag(FLAG_PF, (p & 1) == 0);
}

void CPU8086::updateZSP16(uint16 val) {
    setFlag(FLAG_ZF, val == 0);
    setFlag(FLAG_SF, (val & 0x8000) != 0);
    uint8 p = (val & 0xFF) ^ ((val & 0xFF) >> 4);
    p ^= (p >> 2);
    p ^= (p >> 1);
    setFlag(FLAG_PF, (p & 1) == 0);
}

void CPU8086::triggerInterrupt(uint8 intNum) {
    if (interruptHandler && interruptHandler(intNum)) {
        return; // Handled directly by HLE
    }
    push16(FLAGS);
    push16(CS);
    push16(IP);
    setFlag(FLAG_IF, false);
    setFlag(FLAG_TF, false);
    uint16 ivOff = intNum * 4;
    IP = memRead16(0x0000, ivOff);
    CS = memRead16(0x0000, ivOff + 2);
}

// Decode ModR/M and return effective address or register pointer
struct ModRM {
    uint16* reg16 = nullptr;
    uint8*  reg8  = nullptr;
    uint16  eaSeg = 0;
    uint16  eaOff = 0;
    bool    isReg = false;
};

static ModRM decodeModRM(CPU8086& cpu, uint8 modrm, uint16 segOverride) {
    ModRM m;
    uint8 mod = (modrm >> 6) & 3;
    uint8 reg = (modrm >> 3) & 7;
    uint8 rm  = modrm & 7;

    // Register table
    static uint16* const r16Table[8] = { &cpu.AX, &cpu.CX, &cpu.DX, &cpu.BX, &cpu.SP, &cpu.BP, &cpu.SI, &cpu.DI };
    static uint8*  const r8Table[8]  = { &cpu.AL, &cpu.CL, &cpu.DL, &cpu.BL, &cpu.AH, &cpu.CH, &cpu.DH, &cpu.BH };

    m.reg16 = r16Table[reg];
    m.reg8  = r8Table[reg];

    if (mod == 3) {
        m.isReg = true;
        m.reg16 = r16Table[rm];
        m.reg8  = r8Table[rm];
        return m;
    }

    uint16 defaultSeg = cpu.DS;
    uint16 base = 0;

    switch (rm) {
        case 0: base = cpu.BX + cpu.SI; break;
        case 1: base = cpu.BX + cpu.DI; break;
        case 2: base = cpu.BP + cpu.SI; defaultSeg = cpu.SS; break;
        case 3: base = cpu.BP + cpu.DI; defaultSeg = cpu.SS; break;
        case 4: base = cpu.SI; break;
        case 5: base = cpu.DI; break;
        case 6:
            if (mod == 0) {
                base = cpu.memRead16(cpu.CS, cpu.IP);
                cpu.IP += 2;
            } else {
                base = cpu.BP;
                defaultSeg = cpu.SS;
            }
            break;
        case 7: base = cpu.BX; break;
    }

    if (mod == 1) {
        int8 disp8 = (int8)cpu.memRead8(cpu.CS, cpu.IP++);
        base += disp8;
    } else if (mod == 2) {
        uint16 disp16 = cpu.memRead16(cpu.CS, cpu.IP);
        cpu.IP += 2;
        base += disp16;
    }

    m.eaSeg = (segOverride != 0xFFFF) ? segOverride : defaultSeg;
    m.eaOff = base;
    return m;
}

int CPU8086::step() {
    if (halted) return 4;

    uint16 segOverride = 0xFFFF;
    bool rep = false;
    bool repne = false;

    // Handle Prefixes
    while (true) {
        uint8 prefix = memRead8(CS, IP);
        if (prefix == 0x26) { segOverride = ES; IP++; }
        else if (prefix == 0x2E) { segOverride = CS; IP++; }
        else if (prefix == 0x36) { segOverride = SS; IP++; }
        else if (prefix == 0x3E) { segOverride = DS; IP++; }
        else if (prefix == 0xF3) { rep = true; IP++; }
        else if (prefix == 0xF2) { repne = true; IP++; }
        else break;
    }

    uint8 op = memRead8(CS, IP++);
    int cycles = 4;

    switch (op) {
        case 0x90: // NOP
            break;

        case 0xF4: // HLT
            halted = true;
            break;

        // --- MOV Register/Memory ---
        case 0x88: { // MOV r/m8, r8
            uint8 modrm = memRead8(CS, IP++);
            ModRM m = decodeModRM(*this, modrm, segOverride);
            uint8 val = *(decodeModRM(*this, modrm, segOverride).reg8);
            if (m.isReg) *m.reg8 = val;
            else memWrite8(m.eaSeg, m.eaOff, val);
            break;
        }
        case 0x89: { // MOV r/m16, r16
            uint8 modrm = memRead8(CS, IP++);
            ModRM m = decodeModRM(*this, modrm, segOverride);
            uint16 val = *(m.reg16);
            if (m.isReg) *m.reg16 = val;
            else memWrite16(m.eaSeg, m.eaOff, val);
            break;
        }
        case 0x8A: { // MOV r8, r/m8
            uint8 modrm = memRead8(CS, IP++);
            ModRM m = decodeModRM(*this, modrm, segOverride);
            uint8 val = m.isReg ? *m.reg8 : memRead8(m.eaSeg, m.eaOff);
            *m.reg8 = val;
            break;
        }
        case 0x8B: { // MOV r16, r/m16
            uint8 modrm = memRead8(CS, IP++);
            ModRM m = decodeModRM(*this, modrm, segOverride);
            uint16 val = m.isReg ? *m.reg16 : memRead16(m.eaSeg, m.eaOff);
            *m.reg16 = val;
            break;
        }
        case 0x8C: { // MOV r/m16, Sreg
            uint8 modrm = memRead8(CS, IP++);
            ModRM m = decodeModRM(*this, modrm, segOverride);
            uint8 sreg = (modrm >> 3) & 3;
            uint16 val = (sreg == 0) ? ES : (sreg == 1) ? CS : (sreg == 2) ? SS : DS;
            if (m.isReg) *m.reg16 = val;
            else memWrite16(m.eaSeg, m.eaOff, val);
            break;
        }
        case 0x8E: { // MOV Sreg, r/m16
            uint8 modrm = memRead8(CS, IP++);
            ModRM m = decodeModRM(*this, modrm, segOverride);
            uint16 val = m.isReg ? *m.reg16 : memRead16(m.eaSeg, m.eaOff);
            uint8 sreg = (modrm >> 3) & 3;
            if (sreg == 0) ES = val;
            else if (sreg == 1) CS = val;
            else if (sreg == 2) SS = val;
            else DS = val;
            break;
        }

        // --- MOV Immediate ---
        case 0xB0: case 0xB1: case 0xB2: case 0xB3:
        case 0xB4: case 0xB5: case 0xB6: case 0xB7: { // MOV r8, imm8
            uint8 r = op & 7;
            uint8 imm = memRead8(CS, IP++);
            uint8* r8Table[8] = { &AL, &CL, &DL, &BL, &AH, &CH, &DH, &BH };
            *r8Table[r] = imm;
            break;
        }
        case 0xB8: case 0xB9: case 0xBA: case 0xBB:
        case 0xBC: case 0xBD: case 0xBE: case 0xBF: { // MOV r16, imm16
            uint8 r = op & 7;
            uint16 imm = memRead16(CS, IP);
            IP += 2;
            uint16* r16Table[8] = { &AX, &CX, &DX, &BX, &SP, &BP, &SI, &DI };
            *r16Table[r] = imm;
            break;
        }
        case 0xC6: { // MOV r/m8, imm8
            uint8 modrm = memRead8(CS, IP++);
            ModRM m = decodeModRM(*this, modrm, segOverride);
            uint8 imm = memRead8(CS, IP++);
            if (m.isReg) *m.reg8 = imm;
            else memWrite8(m.eaSeg, m.eaOff, imm);
            break;
        }
        case 0xC7: { // MOV r/m16, imm16
            uint8 modrm = memRead8(CS, IP++);
            ModRM m = decodeModRM(*this, modrm, segOverride);
            uint16 imm = memRead16(CS, IP);
            IP += 2;
            if (m.isReg) *m.reg16 = imm;
            else memWrite16(m.eaSeg, m.eaOff, imm);
            break;
        }
        case 0xA0: { // MOV AL, [moffs8]
            uint16 moffs = memRead16(CS, IP); IP += 2;
            AL = memRead8((segOverride != 0xFFFF) ? segOverride : DS, moffs);
            break;
        }
        case 0xA1: { // MOV AX, [moffs16]
            uint16 moffs = memRead16(CS, IP); IP += 2;
            AX = memRead16((segOverride != 0xFFFF) ? segOverride : DS, moffs);
            break;
        }
        case 0xA2: { // MOV [moffs8], AL
            uint16 moffs = memRead16(CS, IP); IP += 2;
            memWrite8((segOverride != 0xFFFF) ? segOverride : DS, moffs, AL);
            break;
        }
        case 0xA3: { // MOV [moffs16], AX
            uint16 moffs = memRead16(CS, IP); IP += 2;
            memWrite16((segOverride != 0xFFFF) ? segOverride : DS, moffs, AX);
            break;
        }

        // --- PUSH / POP ---
        case 0x50: case 0x51: case 0x52: case 0x53:
        case 0x54: case 0x55: case 0x56: case 0x57: { // PUSH r16
            uint16* r16Table[8] = { &AX, &CX, &DX, &BX, &SP, &BP, &SI, &DI };
            push16(*r16Table[op & 7]);
            break;
        }
        case 0x58: case 0x59: case 0x5A: case 0x5B:
        case 0x5C: case 0x5D: case 0x5E: case 0x5F: { // POP r16
            uint16* r16Table[8] = { &AX, &CX, &DX, &BX, &SP, &BP, &SI, &DI };
            *r16Table[op & 7] = pop16();
            break;
        }
        case 0x06: push16(ES); break;
        case 0x07: ES = pop16(); break;
        case 0x0E: push16(CS); break;
        case 0x16: push16(SS); break;
        case 0x17: SS = pop16(); break;
        case 0x1E: push16(DS); break;
        case 0x1F: DS = pop16(); break;
        case 0x9C: push16(FLAGS); break; // PUSHF
        case 0x9D: FLAGS = pop16(); break; // POPF

        // --- Arithmetic & Logic (ADD, SUB, CMP, XOR, AND, OR) ---
        case 0x00: case 0x01: case 0x02: case 0x03:
        case 0x08: case 0x09: case 0x0A: case 0x0B:
        case 0x20: case 0x21: case 0x22: case 0x23:
        case 0x28: case 0x29: case 0x2A: case 0x2B:
        case 0x30: case 0x31: case 0x32: case 0x33:
        case 0x38: case 0x39: case 0x3A: case 0x3B: {
            uint8 aluOp = (op >> 3) & 7; // 0=ADD, 1=OR, 4=AND, 5=SUB, 6=XOR, 7=CMP
            bool isWord = (op & 1) != 0;
            bool toReg  = (op & 2) != 0;
            uint8 modrm = memRead8(CS, IP++);
            ModRM m = decodeModRM(*this, modrm, segOverride);

            if (!isWord) {
                uint8 src = toReg ? (m.isReg ? *m.reg8 : memRead8(m.eaSeg, m.eaOff)) : *m.reg8;
                uint8 dst = toReg ? *m.reg8 : (m.isReg ? *m.reg8 : memRead8(m.eaSeg, m.eaOff));
                uint32 res = 0;
                if (aluOp == 0) { res = dst + src; setFlag(FLAG_CF, res > 0xFF); }
                else if (aluOp == 1) { res = dst | src; setFlag(FLAG_CF, false); setFlag(FLAG_OF, false); }
                else if (aluOp == 4) { res = dst & src; setFlag(FLAG_CF, false); setFlag(FLAG_OF, false); }
                else if (aluOp == 5 || aluOp == 7) { res = dst - src; setFlag(FLAG_CF, dst < src); }
                else if (aluOp == 6) { res = dst ^ src; setFlag(FLAG_CF, false); setFlag(FLAG_OF, false); }

                updateZSP8((uint8)res);
                if (aluOp != 7) { // CMP does not store result
                    if (toReg) *m.reg8 = (uint8)res;
                    else if (m.isReg) *m.reg8 = (uint8)res;
                    else memWrite8(m.eaSeg, m.eaOff, (uint8)res);
                }
            } else {
                uint16 src = toReg ? (m.isReg ? *m.reg16 : memRead16(m.eaSeg, m.eaOff)) : *m.reg16;
                uint16 dst = toReg ? *m.reg16 : (m.isReg ? *m.reg16 : memRead16(m.eaSeg, m.eaOff));
                uint32 res = 0;
                if (aluOp == 0) { res = dst + src; setFlag(FLAG_CF, res > 0xFFFF); }
                else if (aluOp == 1) { res = dst | src; setFlag(FLAG_CF, false); setFlag(FLAG_OF, false); }
                else if (aluOp == 4) { res = dst & src; setFlag(FLAG_CF, false); setFlag(FLAG_OF, false); }
                else if (aluOp == 5 || aluOp == 7) { res = dst - src; setFlag(FLAG_CF, dst < src); }
                else if (aluOp == 6) { res = dst ^ src; setFlag(FLAG_CF, false); setFlag(FLAG_OF, false); }

                updateZSP16((uint16)res);
                if (aluOp != 7) {
                    if (toReg) *m.reg16 = (uint16)res;
                    else if (m.isReg) *m.reg16 = (uint16)res;
                    else memWrite16(m.eaSeg, m.eaOff, (uint16)res);
                }
            }
            break;
        }

        // INC / DEC r16
        case 0x40: case 0x41: case 0x42: case 0x43:
        case 0x44: case 0x45: case 0x46: case 0x47: { // INC r16
            uint16* r16Table[8] = { &AX, &CX, &DX, &BX, &SP, &BP, &SI, &DI };
            (*r16Table[op & 7])++;
            updateZSP16(*r16Table[op & 7]);
            break;
        }
        case 0x48: case 0x49: case 0x4A: case 0x4B:
        case 0x4C: case 0x4D: case 0x4E: case 0x4F: { // DEC r16
            uint16* r16Table[8] = { &AX, &CX, &DX, &BX, &SP, &BP, &SI, &DI };
            (*r16Table[op & 7])--;
            updateZSP16(*r16Table[op & 7]);
            break;
        }

        // --- Jumps & Calls ---
        case 0xE9: { // JMP near
            int16 rel = (int16)memRead16(CS, IP);
            IP += 2 + rel;
            break;
        }
        case 0xEB: { // JMP short
            int8 rel = (int8)memRead8(CS, IP++);
            IP += rel;
            break;
        }
        case 0xE8: { // CALL near
            int16 rel = (int16)memRead16(CS, IP);
            IP += 2;
            push16(IP);
            IP += rel;
            break;
        }
        case 0xC3: { // RET near
            IP = pop16();
            break;
        }
        case 0xCB: { // RET far
            IP = pop16();
            CS = pop16();
            break;
        }

        // Conditional Jumps
        case 0x70: case 0x71: case 0x72: case 0x73:
        case 0x74: case 0x75: case 0x76: case 0x77:
        case 0x78: case 0x79: case 0x7A: case 0x7B:
        case 0x7C: case 0x7D: case 0x7E: case 0x7F: {
            int8 rel = (int8)memRead8(CS, IP++);
            bool take = false;
            switch (op) {
                case 0x70: take = getFlag(FLAG_OF); break; // JO
                case 0x71: take = !getFlag(FLAG_OF); break; // JNO
                case 0x72: take = getFlag(FLAG_CF); break; // JB / JC
                case 0x73: take = !getFlag(FLAG_CF); break; // JAE / JNC
                case 0x74: take = getFlag(FLAG_ZF); break; // JZ / JE
                case 0x75: take = !getFlag(FLAG_ZF); break; // JNZ / JNE
                case 0x76: take = getFlag(FLAG_CF) || getFlag(FLAG_ZF); break; // JBE
                case 0x77: take = !getFlag(FLAG_CF) && !getFlag(FLAG_ZF); break; // JA
                case 0x78: take = getFlag(FLAG_SF); break; // JS
                case 0x79: take = !getFlag(FLAG_SF); break; // JNS
                case 0x7A: take = getFlag(FLAG_PF); break; // JP
                case 0x7B: take = !getFlag(FLAG_PF); break; // JNP
                case 0x7C: take = getFlag(FLAG_SF) != getFlag(FLAG_OF); break; // JL
                case 0x7D: take = getFlag(FLAG_SF) == getFlag(FLAG_OF); break; // JGE
                case 0x7E: take = getFlag(FLAG_ZF) || (getFlag(FLAG_SF) != getFlag(FLAG_OF)); break; // JLE
                case 0x7F: take = !getFlag(FLAG_ZF) && (getFlag(FLAG_SF) == getFlag(FLAG_OF)); break; // JG
            }
            if (take) IP += rel;
            break;
        }

        case 0xE2: { // LOOP
            int8 rel = (int8)memRead8(CS, IP++);
            CX--;
            if (CX != 0) IP += rel;
            break;
        }
        case 0xE3: { // JCXZ
            int8 rel = (int8)memRead8(CS, IP++);
            if (CX == 0) IP += rel;
            break;
        }

        // --- Software Interrupt ---
        case 0xCD: { // INT imm8
            uint8 intNum = memRead8(CS, IP++);
            triggerInterrupt(intNum);
            break;
        }
        case 0xCF: { // IRET
            IP = pop16();
            CS = pop16();
            FLAGS = pop16();
            break;
        }

        // --- String operations (LODS, STOS, MOVS) ---
        case 0xAA: { // STOSB
            uint16 count = rep ? CX : 1;
            while (count > 0) {
                memWrite8(ES, DI, AL);
                if (getFlag(FLAG_DF)) DI--; else DI++;
                if (rep) { CX--; count--; } else break;
            }
            break;
        }
        case 0xAB: { // STOSW
            uint16 count = rep ? CX : 1;
            while (count > 0) {
                memWrite16(ES, DI, AX);
                if (getFlag(FLAG_DF)) DI -= 2; else DI += 2;
                if (rep) { CX--; count--; } else break;
            }
            break;
        }
        case 0xAC: { // LODSB
            AL = memRead8((segOverride != 0xFFFF) ? segOverride : DS, SI);
            if (getFlag(FLAG_DF)) SI--; else SI++;
            break;
        }
        case 0xAD: { // LODSW
            AX = memRead16((segOverride != 0xFFFF) ? segOverride : DS, SI);
            if (getFlag(FLAG_DF)) SI -= 2; else SI += 2;
            break;
        }
        case 0xA4: { // MOVSB
            uint16 count = rep ? CX : 1;
            while (count > 0) {
                uint8 b = memRead8((segOverride != 0xFFFF) ? segOverride : DS, SI);
                memWrite8(ES, DI, b);
                if (getFlag(FLAG_DF)) { SI--; DI--; } else { SI++; DI++; }
                if (rep) { CX--; count--; } else break;
            }
            break;
        }
        case 0xA5: { // MOVSW
            uint16 count = rep ? CX : 1;
            while (count > 0) {
                uint16 w = memRead16((segOverride != 0xFFFF) ? segOverride : DS, SI);
                memWrite16(ES, DI, w);
                if (getFlag(FLAG_DF)) { SI -= 2; DI -= 2; } else { SI += 2; DI += 2; }
                if (rep) { CX--; count--; } else break;
            }
            break;
        }

        // --- Port I/O ---
        case 0xE4: { // IN AL, imm8
            uint8 port = memRead8(CS, IP++);
            AL = inPort ? inPort(port) : 0xFF;
            break;
        }
        case 0xE5: { // IN AX, imm8
            uint8 port = memRead8(CS, IP++);
            AL = inPort ? inPort(port) : 0xFF;
            AH = inPort ? inPort(port + 1) : 0xFF;
            break;
        }
        case 0xE6: { // OUT imm8, AL
            uint8 port = memRead8(CS, IP++);
            if (outPort) outPort(port, AL);
            break;
        }
        case 0xE7: { // OUT imm8, AX
            uint8 port = memRead8(CS, IP++);
            if (outPort) { outPort(port, AL); outPort(port + 1, AH); }
            break;
        }
        case 0xEC: { // IN AL, DX
            AL = inPort ? inPort(DX) : 0xFF;
            break;
        }
        case 0xED: { // IN AX, DX
            AL = inPort ? inPort(DX) : 0xFF;
            AH = inPort ? inPort(DX + 1) : 0xFF;
            break;
        }
        case 0xEE: { // OUT DX, AL
            if (outPort) outPort(DX, AL);
            break;
        }
        case 0xEF: { // OUT DX, AX
            if (outPort) { outPort(DX, AL); outPort(DX + 1, AH); }
            break;
        }

        // --- LEA / Flags ---
        case 0x8D: { // LEA r16, m
            uint8 modrm = memRead8(CS, IP++);
            ModRM m = decodeModRM(*this, modrm, segOverride);
            *m.reg16 = m.eaOff;
            break;
        }
        case 0xF8: setFlag(FLAG_CF, false); break; // CLC
        case 0xF9: setFlag(FLAG_CF, true); break;  // STC
        case 0xFA: setFlag(FLAG_IF, false); break; // CLI
        case 0xFB: setFlag(FLAG_IF, true); break;  // STI
        case 0xFC: setFlag(FLAG_DF, false); break; // CLD
        case 0xFD: setFlag(FLAG_DF, true); break;  // STD

        default:
            // Unimplemented / skip
            break;
    }

    totalCycles += cycles;
    return cycles;
}
