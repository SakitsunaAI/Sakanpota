#include "cpu_z80.h"
#include <cstring>

static const uint8 parityTable[256] = {
    4,0,0,4,0,4,4,0,0,4,4,0,4,0,0,4,0,4,4,0,4,0,0,4,4,0,0,4,0,4,4,0,
    0,4,4,0,4,0,0,4,4,0,0,4,0,4,4,0,4,0,0,4,0,4,4,0,0,4,4,0,4,0,0,4,
    0,4,4,0,4,0,0,4,4,0,0,4,0,4,4,0,4,0,0,4,0,4,4,0,0,4,4,0,4,0,0,4,
    4,0,0,4,0,4,4,0,0,4,4,0,4,0,0,4,0,4,4,0,4,0,0,4,4,0,0,4,0,4,4,0,
    0,4,4,0,4,0,0,4,4,0,0,4,0,4,4,0,4,0,0,4,0,4,4,0,0,4,4,0,4,0,0,4,
    4,0,0,4,0,4,4,0,0,4,4,0,4,0,0,4,0,4,4,0,4,0,0,4,4,0,0,4,0,4,4,0,
    4,0,0,4,0,4,4,0,0,4,4,0,4,0,0,4,0,4,4,0,4,0,0,4,4,0,0,4,0,4,4,0,
    0,4,4,0,4,0,0,4,4,0,0,4,0,4,4,0,4,0,0,4,0,4,4,0,0,4,4,0,4,0,0,4
};

CPUZ80::CPUZ80() {
    reset();
}

void CPUZ80::reset() {
    AF.w = 0xFFFF;
    BC.w = 0xFFFF;
    DE.w = 0xFFFF;
    HL.w = 0xFFFF;
    AF_.w = 0xFFFF;
    BC_.w = 0xFFFF;
    DE_.w = 0xFFFF;
    HL_.w = 0xFFFF;
    IX.w = 0xFFFF;
    IY.w = 0xFFFF;
    SP = 0xFFFF;
    PC = 0x0000;
    I = 0;
    R = 0;
    iff1 = false;
    iff2 = false;
    im = 1;
    halted = false;
}

uint8 CPUZ80::readByte(uint16 addr) {
    return memRead ? memRead(addr) : 0xFF;
}

void CPUZ80::writeByte(uint16 addr, uint8 val) {
    if (memWrite) memWrite(addr, val);
}

uint16 CPUZ80::readWord(uint16 addr) {
    uint8 l = readByte(addr);
    uint8 h = readByte(addr + 1);
    return (uint16)l | ((uint16)h << 8);
}

void CPUZ80::writeWord(uint16 addr, uint16 val) {
    writeByte(addr, (uint8)val);
    writeByte(addr + 1, (uint8)(val >> 8));
}

uint8 CPUZ80::inByte(uint16 port) {
    return ioRead ? ioRead(port) : 0xFF;
}

void CPUZ80::outByte(uint16 port, uint8 val) {
    if (ioWrite) ioWrite(port, val);
}

uint8 CPUZ80::fetchByte() {
    uint8 b = readByte(PC++);
    R = (R & 0x80) | ((R + 1) & 0x7F);
    return b;
}

uint16 CPUZ80::fetchWord() {
    uint8 l = fetchByte();
    uint8 h = fetchByte();
    return (uint16)l | ((uint16)h << 8);
}

void CPUZ80::setFlag(uint8 mask, bool cond) {
    if (cond) AF.b.l |= mask;
    else AF.b.l &= ~mask;
}

bool CPUZ80::getFlag(uint8 mask) const {
    return (AF.b.l & mask) != 0;
}

void CPUZ80::aluAdd(uint8 val, bool withCarry) {
    int c = (withCarry && getFlag(CARRY)) ? 1 : 0;
    int a = AF.b.h;
    int res = a + val + c;
    uint8 r = (uint8)res;

    uint8 f = (r & 0x80) ? SIGN : 0;
    if (r == 0) f |= ZERO;
    if ((a & 0x0F) + (val & 0x0F) + c > 0x0F) f |= HALF;
    if (((a ^ ~val) & (a ^ res) & 0x80)) f |= FLAG_OVERFLOW;
    if (res > 0xFF) f |= CARRY;
    f |= (r & (F3 | F5));

    AF.b.h = r;
    AF.b.l = f;
}

void CPUZ80::aluSub(uint8 val, bool withCarry) {
    int c = (withCarry && getFlag(CARRY)) ? 1 : 0;
    int a = AF.b.h;
    int res = a - val - c;
    uint8 r = (uint8)res;

    uint8 f = SUBTRACT;
    if (r & 0x80) f |= SIGN;
    if (r == 0) f |= ZERO;
    if ((a & 0x0F) - (val & 0x0F) - c < 0) f |= HALF;
    if (((a ^ val) & (a ^ res) & 0x80)) f |= FLAG_OVERFLOW;
    if (res < 0) f |= CARRY;
    f |= (r & (F3 | F5));

    AF.b.h = r;
    AF.b.l = f;
}

void CPUZ80::aluAnd(uint8 val) {
    AF.b.h &= val;
    uint8 f = HALF;
    if (AF.b.h & 0x80) f |= SIGN;
    if (AF.b.h == 0) f |= ZERO;
    f |= parityTable[AF.b.h];
    f |= (AF.b.h & (F3 | F5));
    AF.b.l = f;
}

void CPUZ80::aluXor(uint8 val) {
    AF.b.h ^= val;
    uint8 f = 0;
    if (AF.b.h & 0x80) f |= SIGN;
    if (AF.b.h == 0) f |= ZERO;
    f |= parityTable[AF.b.h];
    f |= (AF.b.h & (F3 | F5));
    AF.b.l = f;
}

void CPUZ80::aluOr(uint8 val) {
    AF.b.h |= val;
    uint8 f = 0;
    if (AF.b.h & 0x80) f |= SIGN;
    if (AF.b.h == 0) f |= ZERO;
    f |= parityTable[AF.b.h];
    f |= (AF.b.h & (F3 | F5));
    AF.b.l = f;
}

void CPUZ80::aluCp(uint8 val) {
    uint8 saveA = AF.b.h;
    aluSub(val, false);
    AF.b.h = saveA;
    AF.b.l = (AF.b.l & ~(F3 | F5)) | (val & (F3 | F5));
}

uint8 CPUZ80::aluInc(uint8 val) {
    uint8 res = val + 1;
    uint8 f = (AF.b.l & CARRY);
    if (res & 0x80) f |= SIGN;
    if (res == 0) f |= ZERO;
    if ((val & 0x0F) == 0x0F) f |= HALF;
    if (val == 0x7F) f |= FLAG_OVERFLOW;
    f |= (res & (F3 | F5));
    AF.b.l = f;
    return res;
}

uint8 CPUZ80::aluDec(uint8 val) {
    uint8 res = val - 1;
    uint8 f = (AF.b.l & CARRY) | SUBTRACT;
    if (res & 0x80) f |= SIGN;
    if (res == 0) f |= ZERO;
    if ((val & 0x0F) == 0x00) f |= HALF;
    if (val == 0x80) f |= FLAG_OVERFLOW;
    f |= (res & (F3 | F5));
    AF.b.l = f;
    return res;
}

void CPUZ80::aluDaa() {
    uint8 a = AF.b.h;
    uint8 cf = getFlag(CARRY) ? 1 : 0;
    uint8 hf = getFlag(HALF) ? 1 : 0;
    uint8 nf = getFlag(SUBTRACT) ? 1 : 0;
    uint8 diff = 0;

    if (hf || (a & 0x0F) > 9) diff |= 0x06;
    if (cf || a > 0x99) {
        diff |= 0x60;
        cf = 1;
    }

    if (nf) a -= diff;
    else a += diff;

    uint8 f = nf ? SUBTRACT : 0;
    if (cf) f |= CARRY;
    if (a & 0x80) f |= SIGN;
    if (a == 0) f |= ZERO;
    f |= parityTable[a];
    f |= (a & (F3 | F5));

    AF.b.h = a;
    AF.b.l = f;
}

uint8 CPUZ80::rlc(uint8 val) {
    uint8 c = (val & 0x80) >> 7;
    uint8 r = (val << 1) | c;
    uint8 f = c ? CARRY : 0;
    if (r & 0x80) f |= SIGN;
    if (r == 0) f |= ZERO;
    f |= parityTable[r];
    f |= (r & (F3 | F5));
    AF.b.l = f;
    return r;
}

uint8 CPUZ80::rrc(uint8 val) {
    uint8 c = val & 0x01;
    uint8 r = (val >> 1) | (c << 7);
    uint8 f = c ? CARRY : 0;
    if (r & 0x80) f |= SIGN;
    if (r == 0) f |= ZERO;
    f |= parityTable[r];
    f |= (r & (F3 | F5));
    AF.b.l = f;
    return r;
}

uint8 CPUZ80::rl(uint8 val) {
    uint8 oldC = getFlag(CARRY) ? 1 : 0;
    uint8 c = (val & 0x80) >> 7;
    uint8 r = (val << 1) | oldC;
    uint8 f = c ? CARRY : 0;
    if (r & 0x80) f |= SIGN;
    if (r == 0) f |= ZERO;
    f |= parityTable[r];
    f |= (r & (F3 | F5));
    AF.b.l = f;
    return r;
}

uint8 CPUZ80::rr(uint8 val) {
    uint8 oldC = getFlag(CARRY) ? 1 : 0;
    uint8 c = val & 0x01;
    uint8 r = (val >> 1) | (oldC << 7);
    uint8 f = c ? CARRY : 0;
    if (r & 0x80) f |= SIGN;
    if (r == 0) f |= ZERO;
    f |= parityTable[r];
    f |= (r & (F3 | F5));
    AF.b.l = f;
    return r;
}

uint8 CPUZ80::sla(uint8 val) {
    uint8 c = (val & 0x80) >> 7;
    uint8 r = val << 1;
    uint8 f = c ? CARRY : 0;
    if (r & 0x80) f |= SIGN;
    if (r == 0) f |= ZERO;
    f |= parityTable[r];
    f |= (r & (F3 | F5));
    AF.b.l = f;
    return r;
}

uint8 CPUZ80::sra(uint8 val) {
    uint8 c = val & 0x01;
    uint8 r = (val >> 1) | (val & 0x80);
    uint8 f = c ? CARRY : 0;
    if (r & 0x80) f |= SIGN;
    if (r == 0) f |= ZERO;
    f |= parityTable[r];
    f |= (r & (F3 | F5));
    AF.b.l = f;
    return r;
}

uint8 CPUZ80::sll(uint8 val) {
    uint8 c = (val & 0x80) >> 7;
    uint8 r = (val << 1) | 0x01;
    uint8 f = c ? CARRY : 0;
    if (r & 0x80) f |= SIGN;
    if (r == 0) f |= ZERO;
    f |= parityTable[r];
    f |= (r & (F3 | F5));
    AF.b.l = f;
    return r;
}

uint8 CPUZ80::srl(uint8 val) {
    uint8 c = val & 0x01;
    uint8 r = val >> 1;
    uint8 f = c ? CARRY : 0;
    if (r & 0x80) f |= SIGN;
    if (r == 0) f |= ZERO;
    f |= parityTable[r];
    f |= (r & (F3 | F5));
    AF.b.l = f;
    return r;
}

void CPUZ80::requestInterrupt(uint8 vector) {
    if (!iff1) return;
    if (halted) {
        halted = false;
        PC++;
    }
    iff1 = false;
    iff2 = false;

    // Push PC
    writeWord(SP - 2, PC);
    SP -= 2;

    if (im == 1) {
        PC = 0x0038;
    } else if (im == 2) {
        uint16 tblAddr = ((uint16)I << 8) | (vector & 0xFE);
        PC = readWord(tblAddr);
    } else {
        PC = vector & 0x38;
    }
}

void CPUZ80::requestNMI() {
    if (halted) {
        halted = false;
        PC++;
    }
    iff2 = iff1;
    iff1 = false;
    writeWord(SP - 2, PC);
    SP -= 2;
    PC = 0x0066;
}

int CPUZ80::step() {
    if (halted) return 4;

    uint8 op = fetchByte();

    // Map 8-bit registers pointer helper
    auto getR8 = [&](int idx) -> uint8 {
        switch (idx) {
            case 0: return BC.b.h;
            case 1: return BC.b.l;
            case 2: return DE.b.h;
            case 3: return DE.b.l;
            case 4: return HL.b.h;
            case 5: return HL.b.l;
            case 6: return readByte(HL.w);
            case 7: return AF.b.h;
            default: return 0;
        }
    };

    auto setR8 = [&](int idx, uint8 val) {
        switch (idx) {
            case 0: BC.b.h = val; break;
            case 1: BC.b.l = val; break;
            case 2: DE.b.h = val; break;
            case 3: DE.b.l = val; break;
            case 4: HL.b.h = val; break;
            case 5: HL.b.l = val; break;
            case 6: writeByte(HL.w, val); break;
            case 7: AF.b.h = val; break;
        }
    };

    // Prefix opcodes
    if (op == 0xCB) return executeCB();
    if (op == 0xED) return executeED();
    if (op == 0xDD) return executeIndex(IX, 0xDD);
    if (op == 0xFD) return executeIndex(IY, 0xFD);

    // Standard instruction switch
    switch (op) {
        case 0x00: return 4; // NOP
        case 0x01: BC.w = fetchWord(); return 10; // LD BC, nn
        case 0x02: writeByte(BC.w, AF.b.h); return 7; // LD (BC), A
        case 0x03: BC.w++; return 6; // INC BC
        case 0x04: BC.b.h = aluInc(BC.b.h); return 4; // INC B
        case 0x05: BC.b.h = aluDec(BC.b.h); return 4; // DEC B
        case 0x06: BC.b.h = fetchByte(); return 7; // LD B, n
        case 0x07: { // RLCA
            uint8 c = (AF.b.h & 0x80) >> 7;
            AF.b.h = (AF.b.h << 1) | c;
            AF.b.l = (AF.b.l & (SIGN | ZERO | PARITY)) | (c ? CARRY : 0) | (AF.b.h & (F3 | F5));
            return 4;
        }
        case 0x08: { // EX AF, AF'
            uint16 tmp = AF.w; AF.w = AF_.w; AF_.w = tmp;
            return 4;
        }
        case 0x09: { // ADD HL, BC
            int res = HL.w + BC.w;
            uint8 f = AF.b.l & (SIGN | ZERO | PARITY);
            if (((HL.w & 0x0FFF) + (BC.w & 0x0FFF)) > 0x0FFF) f |= HALF;
            if (res > 0xFFFF) f |= CARRY;
            f |= ((res >> 8) & (F3 | F5));
            HL.w = (uint16)res;
            AF.b.l = f;
            return 11;
        }
        case 0x0A: AF.b.h = readByte(BC.w); return 7; // LD A, (BC)
        case 0x0B: BC.w--; return 6; // DEC BC
        case 0x0C: BC.b.l = aluInc(BC.b.l); return 4; // INC C
        case 0x0D: BC.b.l = aluDec(BC.b.l); return 4; // DEC C
        case 0x0E: BC.b.l = fetchByte(); return 7; // LD C, n
        case 0x0F: { // RRCA
            uint8 c = AF.b.h & 0x01;
            AF.b.h = (AF.b.h >> 1) | (c << 7);
            AF.b.l = (AF.b.l & (SIGN | ZERO | PARITY)) | (c ? CARRY : 0) | (AF.b.h & (F3 | F5));
            return 4;
        }

        // 0x10..0x1F
        case 0x10: { // DJNZ e
            int8 d = (int8)fetchByte();
            BC.b.h--;
            if (BC.b.h != 0) { PC += d; return 13; }
            return 8;
        }
        case 0x11: DE.w = fetchWord(); return 10; // LD DE, nn
        case 0x12: writeByte(DE.w, AF.b.h); return 7; // LD (DE), A
        case 0x13: DE.w++; return 6; // INC DE
        case 0x14: DE.b.h = aluInc(DE.b.h); return 4; // INC D
        case 0x15: DE.b.h = aluDec(DE.b.h); return 4; // DEC D
        case 0x16: DE.b.h = fetchByte(); return 7; // LD D, n
        case 0x17: { // RLA
            uint8 oldC = getFlag(CARRY) ? 1 : 0;
            uint8 c = (AF.b.h & 0x80) >> 7;
            AF.b.h = (AF.b.h << 1) | oldC;
            AF.b.l = (AF.b.l & (SIGN | ZERO | PARITY)) | (c ? CARRY : 0) | (AF.b.h & (F3 | F5));
            return 4;
        }
        case 0x18: { // JR e
            int8 d = (int8)fetchByte();
            PC += d;
            return 12;
        }
        case 0x19: { // ADD HL, DE
            int res = HL.w + DE.w;
            uint8 f = AF.b.l & (SIGN | ZERO | PARITY);
            if (((HL.w & 0x0FFF) + (DE.w & 0x0FFF)) > 0x0FFF) f |= HALF;
            if (res > 0xFFFF) f |= CARRY;
            f |= ((res >> 8) & (F3 | F5));
            HL.w = (uint16)res;
            AF.b.l = f;
            return 11;
        }
        case 0x1A: AF.b.h = readByte(DE.w); return 7; // LD A, (DE)
        case 0x1B: DE.w--; return 6; // DEC DE
        case 0x1C: DE.b.l = aluInc(DE.b.l); return 4; // INC E
        case 0x1D: DE.b.l = aluDec(DE.b.l); return 4; // DEC E
        case 0x1E: DE.b.l = fetchByte(); return 7; // LD E, n
        case 0x1F: { // RRA
            uint8 oldC = getFlag(CARRY) ? 1 : 0;
            uint8 c = AF.b.h & 0x01;
            AF.b.h = (AF.b.h >> 1) | (oldC << 7);
            AF.b.l = (AF.b.l & (SIGN | ZERO | PARITY)) | (c ? CARRY : 0) | (AF.b.h & (F3 | F5));
            return 4;
        }

        // 0x20..0x2F
        case 0x20: { int8 d = (int8)fetchByte(); if (!getFlag(ZERO)) { PC += d; return 12; } return 7; } // JR NZ, e
        case 0x21: HL.w = fetchWord(); return 10; // LD HL, nn
        case 0x22: { uint16 a = fetchWord(); writeWord(a, HL.w); return 16; } // LD (nn), HL
        case 0x23: HL.w++; return 6; // INC HL
        case 0x24: HL.b.h = aluInc(HL.b.h); return 4; // INC H
        case 0x25: HL.b.h = aluDec(HL.b.h); return 4; // DEC H
        case 0x26: HL.b.h = fetchByte(); return 7; // LD H, n
        case 0x27: aluDaa(); return 4; // DAA
        case 0x28: { int8 d = (int8)fetchByte(); if (getFlag(ZERO)) { PC += d; return 12; } return 7; } // JR Z, e
        case 0x29: { // ADD HL, HL
            int res = HL.w + HL.w;
            uint8 f = AF.b.l & (SIGN | ZERO | PARITY);
            if (((HL.w & 0x0FFF) + (HL.w & 0x0FFF)) > 0x0FFF) f |= HALF;
            if (res > 0xFFFF) f |= CARRY;
            f |= ((res >> 8) & (F3 | F5));
            HL.w = (uint16)res;
            AF.b.l = f;
            return 11;
        }
        case 0x2A: { uint16 a = fetchWord(); HL.w = readWord(a); return 16; } // LD HL, (nn)
        case 0x2B: HL.w--; return 6; // DEC HL
        case 0x2C: HL.b.l = aluInc(HL.b.l); return 4; // INC L
        case 0x2D: HL.b.l = aluDec(HL.b.l); return 4; // DEC L
        case 0x2E: HL.b.l = fetchByte(); return 7; // LD L, n
        case 0x2F: AF.b.h = ~AF.b.h; AF.b.l |= (HALF | SUBTRACT); return 4; // CPL

        // 0x30..0x3F
        case 0x30: { int8 d = (int8)fetchByte(); if (!getFlag(CARRY)) { PC += d; return 12; } return 7; } // JR NC, e
        case 0x31: SP = fetchWord(); return 10; // LD SP, nn
        case 0x32: { uint16 a = fetchWord(); writeByte(a, AF.b.h); return 13; } // LD (nn), A
        case 0x33: SP++; return 6; // INC SP
        case 0x34: writeByte(HL.w, aluInc(readByte(HL.w))); return 11; // INC (HL)
        case 0x35: writeByte(HL.w, aluDec(readByte(HL.w))); return 11; // DEC (HL)
        case 0x36: writeByte(HL.w, fetchByte()); return 10; // LD (HL), n
        case 0x37: AF.b.l = (AF.b.l & ~(HALF | SUBTRACT)) | CARRY; return 4; // SCF
        case 0x38: { int8 d = (int8)fetchByte(); if (getFlag(CARRY)) { PC += d; return 12; } return 7; } // JR C, e
        case 0x39: { // ADD HL, SP
            int res = HL.w + SP;
            uint8 f = AF.b.l & (SIGN | ZERO | PARITY);
            if (((HL.w & 0x0FFF) + (SP & 0x0FFF)) > 0x0FFF) f |= HALF;
            if (res > 0xFFFF) f |= CARRY;
            f |= ((res >> 8) & (F3 | F5));
            HL.w = (uint16)res;
            AF.b.l = f;
            return 11;
        }
        case 0x3A: { uint16 a = fetchWord(); AF.b.h = readByte(a); return 13; } // LD A, (nn)
        case 0x3B: SP--; return 6; // DEC SP
        case 0x3C: AF.b.h = aluInc(AF.b.h); return 4; // INC A
        case 0x3D: AF.b.h = aluDec(AF.b.h); return 4; // DEC A
        case 0x3E: AF.b.h = fetchByte(); return 7; // LD A, n
        case 0x3F: { // CCF
            bool oldC = getFlag(CARRY);
            AF.b.l = (AF.b.l & ~(HALF | SUBTRACT | CARRY)) | (oldC ? HALF : CARRY);
            return 4;
        }

        // 0x40..0x7F: LD r, r' and HALT
        case 0x76: halted = true; return 4; // HALT

        // 0x80..0xBF: ALU A, r
        case 0xC0: if (!getFlag(ZERO)) { PC = readWord(SP); SP += 2; return 11; } return 5; // RET NZ
        case 0xC1: BC.w = readWord(SP); SP += 2; return 10; // POP BC
        case 0xC2: { uint16 a = fetchWord(); if (!getFlag(ZERO)) { PC = a; return 10; } return 10; } // JP NZ, nn
        case 0xC3: PC = fetchWord(); return 10; // JP nn
        case 0xC4: { uint16 a = fetchWord(); if (!getFlag(ZERO)) { writeWord(SP - 2, PC); SP -= 2; PC = a; return 17; } return 10; } // CALL NZ, nn
        case 0xC5: writeWord(SP - 2, BC.w); SP -= 2; return 11; // PUSH BC
        case 0xC6: aluAdd(fetchByte(), false); return 7; // ADD A, n
        case 0xC7: writeWord(SP - 2, PC); SP -= 2; PC = 0x0000; return 11; // RST 00h
        case 0xC8: if (getFlag(ZERO)) { PC = readWord(SP); SP += 2; return 11; } return 5; // RET Z
        case 0xC9: PC = readWord(SP); SP += 2; return 10; // RET
        case 0xCA: { uint16 a = fetchWord(); if (getFlag(ZERO)) { PC = a; return 10; } return 10; } // JP Z, nn
        case 0xCC: { uint16 a = fetchWord(); if (getFlag(ZERO)) { writeWord(SP - 2, PC); SP -= 2; PC = a; return 17; } return 10; } // CALL Z, nn
        case 0xCD: { uint16 a = fetchWord(); writeWord(SP - 2, PC); SP -= 2; PC = a; return 17; } // CALL nn
        case 0xCE: aluAdd(fetchByte(), true); return 7; // ADC A, n
        case 0xCF: writeWord(SP - 2, PC); SP -= 2; PC = 0x0008; return 11; // RST 08h

        case 0xD0: if (!getFlag(CARRY)) { PC = readWord(SP); SP += 2; return 11; } return 5; // RET NC
        case 0xD1: DE.w = readWord(SP); SP += 2; return 10; // POP DE
        case 0xD2: { uint16 a = fetchWord(); if (!getFlag(CARRY)) { PC = a; return 10; } return 10; } // JP NC, nn
        case 0xD3: outByte(fetchByte() | ((uint16)AF.b.h << 8), AF.b.h); return 11; // OUT (n), A
        case 0xD4: { uint16 a = fetchWord(); if (!getFlag(CARRY)) { writeWord(SP - 2, PC); SP -= 2; PC = a; return 17; } return 10; } // CALL NC, nn
        case 0xD5: writeWord(SP - 2, DE.w); SP -= 2; return 11; // PUSH DE
        case 0xD6: aluSub(fetchByte(), false); return 7; // SUB n
        case 0xD7: writeWord(SP - 2, PC); SP -= 2; PC = 0x0010; return 11; // RST 10h
        case 0xD8: if (getFlag(CARRY)) { PC = readWord(SP); SP += 2; return 11; } return 5; // RET C
        case 0xD9: { // EXX
            uint16 t;
            t = BC.w; BC.w = BC_.w; BC_.w = t;
            t = DE.w; DE.w = DE_.w; DE_.w = t;
            t = HL.w; HL.w = HL_.w; HL_.w = t;
            return 4;
        }
        case 0xDA: { uint16 a = fetchWord(); if (getFlag(CARRY)) { PC = a; return 10; } return 10; } // JP C, nn
        case 0xDB: AF.b.h = inByte(fetchByte() | ((uint16)AF.b.h << 8)); return 11; // IN A, (n)
        case 0xDC: { uint16 a = fetchWord(); if (getFlag(CARRY)) { writeWord(SP - 2, PC); SP -= 2; PC = a; return 17; } return 10; } // CALL C, nn
        case 0xDE: aluSub(fetchByte(), true); return 7; // SBC A, n
        case 0xDF: writeWord(SP - 2, PC); SP -= 2; PC = 0x0018; return 11; // RST 18h

        case 0xE0: if (!getFlag(PARITY)) { PC = readWord(SP); SP += 2; return 11; } return 5; // RET PO
        case 0xE1: HL.w = readWord(SP); SP += 2; return 10; // POP HL
        case 0xE2: { uint16 a = fetchWord(); if (!getFlag(PARITY)) { PC = a; return 10; } return 10; } // JP PO, nn
        case 0xE3: { // EX (SP), HL
            uint16 t = readWord(SP);
            writeWord(SP, HL.w);
            HL.w = t;
            return 19;
        }
        case 0xE4: { uint16 a = fetchWord(); if (!getFlag(PARITY)) { writeWord(SP - 2, PC); SP -= 2; PC = a; return 17; } return 10; } // CALL PO, nn
        case 0xE5: writeWord(SP - 2, HL.w); SP -= 2; return 11; // PUSH HL
        case 0xE6: aluAnd(fetchByte()); return 7; // AND n
        case 0xE7: writeWord(SP - 2, PC); SP -= 2; PC = 0x0020; return 11; // RST 20h
        case 0xE8: if (getFlag(PARITY)) { PC = readWord(SP); SP += 2; return 11; } return 5; // RET PE
        case 0xE9: PC = HL.w; return 4; // JP (HL)
        case 0xEA: { uint16 a = fetchWord(); if (getFlag(PARITY)) { PC = a; return 10; } return 10; } // JP PE, nn
        case 0xEB: { uint16 t = DE.w; DE.w = HL.w; HL.w = t; return 4; } // EX DE, HL
        case 0xEC: { uint16 a = fetchWord(); if (getFlag(PARITY)) { writeWord(SP - 2, PC); SP -= 2; PC = a; return 17; } return 10; } // CALL PE, nn
        case 0xEE: aluXor(fetchByte()); return 7; // XOR n
        case 0xEF: writeWord(SP - 2, PC); SP -= 2; PC = 0x0028; return 11; // RST 28h

        case 0xF0: if (!getFlag(SIGN)) { PC = readWord(SP); SP += 2; return 11; } return 5; // RET P
        case 0xF1: AF.w = readWord(SP); SP += 2; return 10; // POP AF
        case 0xF2: { uint16 a = fetchWord(); if (!getFlag(SIGN)) { PC = a; return 10; } return 10; } // JP P, nn
        case 0xF3: iff1 = false; iff2 = false; return 4; // DI
        case 0xF4: { uint16 a = fetchWord(); if (!getFlag(SIGN)) { writeWord(SP - 2, PC); SP -= 2; PC = a; return 17; } return 10; } // CALL P, nn
        case 0xF5: writeWord(SP - 2, AF.w); SP -= 2; return 11; // PUSH AF
        case 0xF6: aluOr(fetchByte()); return 7; // OR n
        case 0xF7: writeWord(SP - 2, PC); SP -= 2; PC = 0x0030; return 11; // RST 30h
        case 0xF8: if (getFlag(SIGN)) { PC = readWord(SP); SP += 2; return 11; } return 5; // RET M
        case 0xF9: SP = HL.w; return 6; // LD SP, HL
        case 0xFA: { uint16 a = fetchWord(); if (getFlag(SIGN)) { PC = a; return 10; } return 10; } // JP M, nn
        case 0xFB: iff1 = true; iff2 = true; return 4; // EI
        case 0xFC: { uint16 a = fetchWord(); if (getFlag(SIGN)) { writeWord(SP - 2, PC); SP -= 2; PC = a; return 17; } return 10; } // CALL M, nn
        case 0xFE: aluCp(fetchByte()); return 7; // CP n
        case 0xFF: writeWord(SP - 2, PC); SP -= 2; PC = 0x0038; return 11; // RST 38h

        default:
            // 0x40..0x7F: LD r, r'
            if (op >= 0x40 && op <= 0x7F) {
                int dst = (op >> 3) & 0x07;
                int src = op & 0x07;
                setR8(dst, getR8(src));
                return (dst == 6 || src == 6) ? 7 : 4;
            }
            // 0x80..0xBF: ALU A, r
            if (op >= 0x80 && op <= 0xBF) {
                int opc = (op >> 3) & 0x07;
                uint8 val = getR8(op & 0x07);
                switch (opc) {
                    case 0: aluAdd(val, false); break;
                    case 1: aluAdd(val, true); break;
                    case 2: aluSub(val, false); break;
                    case 3: aluSub(val, true); break;
                    case 4: aluAnd(val); break;
                    case 5: aluXor(val); break;
                    case 6: aluOr(val); break;
                    case 7: aluCp(val); break;
                }
                return (op & 0x07) == 6 ? 7 : 4;
            }
            return 4;
    }
}

int CPUZ80::executeCB() {
    uint8 op = fetchByte();
    int regIdx = op & 0x07;

    auto getR8 = [&](int idx) -> uint8 {
        switch (idx) {
            case 0: return BC.b.h;
            case 1: return BC.b.l;
            case 2: return DE.b.h;
            case 3: return DE.b.l;
            case 4: return HL.b.h;
            case 5: return HL.b.l;
            case 6: return readByte(HL.w);
            case 7: return AF.b.h;
            default: return 0;
        }
    };

    auto setR8 = [&](int idx, uint8 val) {
        switch (idx) {
            case 0: BC.b.h = val; break;
            case 1: BC.b.l = val; break;
            case 2: DE.b.h = val; break;
            case 3: DE.b.l = val; break;
            case 4: HL.b.h = val; break;
            case 5: HL.b.l = val; break;
            case 6: writeByte(HL.w, val); break;
            case 7: AF.b.h = val; break;
        }
    };

    uint8 val = getR8(regIdx);

    if (op <= 0x3F) {
        int sub = (op >> 3) & 0x07;
        uint8 res = val;
        switch (sub) {
            case 0: res = rlc(val); break;
            case 1: res = rrc(val); break;
            case 2: res = rl(val); break;
            case 3: res = rr(val); break;
            case 4: res = sla(val); break;
            case 5: res = sra(val); break;
            case 6: res = sll(val); break;
            case 7: res = srl(val); break;
        }
        setR8(regIdx, res);
        return regIdx == 6 ? 15 : 8;
    } else if (op <= 0x7F) {
        // BIT b, r
        int b = (op >> 3) & 0x07;
        uint8 bit = 1 << b;
        uint8 f = (AF.b.l & CARRY) | HALF;
        if ((val & bit) == 0) f |= (ZERO | PARITY);
        if (b == 7 && (val & 0x80)) f |= SIGN;
        f |= (val & (F3 | F5));
        AF.b.l = f;
        return regIdx == 6 ? 12 : 8;
    } else if (op <= 0xBF) {
        // RES b, r
        int b = (op >> 3) & 0x07;
        setR8(regIdx, val & ~(1 << b));
        return regIdx == 6 ? 15 : 8;
    } else {
        // SET b, r
        int b = (op >> 3) & 0x07;
        setR8(regIdx, val | (1 << b));
        return regIdx == 6 ? 15 : 8;
    }
}

int CPUZ80::executeED() {
    uint8 op = fetchByte();
    switch (op) {
        // Block transfers
        case 0xA0: { // LDI
            writeByte(DE.w++, readByte(HL.w++));
            BC.w--;
            AF.b.l &= ~(SUBTRACT | HALF | PARITY);
            if (BC.w != 0) AF.b.l |= PARITY;
            return 16;
        }
        case 0xB0: { // LDIR
            writeByte(DE.w++, readByte(HL.w++));
            BC.w--;
            AF.b.l &= ~(SUBTRACT | HALF | PARITY);
            if (BC.w != 0) {
                AF.b.l |= PARITY;
                PC -= 2;
                return 21;
            }
            return 16;
        }
        case 0xA8: { // LDD
            writeByte(DE.w--, readByte(HL.w--));
            BC.w--;
            AF.b.l &= ~(SUBTRACT | HALF | PARITY);
            if (BC.w != 0) AF.b.l |= PARITY;
            return 16;
        }
        case 0xB8: { // LDDR
            writeByte(DE.w--, readByte(HL.w--));
            BC.w--;
            AF.b.l &= ~(SUBTRACT | HALF | PARITY);
            if (BC.w != 0) {
                AF.b.l |= PARITY;
                PC -= 2;
                return 21;
            }
            return 16;
        }
        case 0xA1: { // CPI
            uint8 val = readByte(HL.w++);
            BC.w--;
            uint8 saveA = AF.b.h;
            aluSub(val, false);
            AF.b.h = saveA;
            AF.b.l &= ~PARITY;
            if (BC.w != 0) AF.b.l |= PARITY;
            return 16;
        }
        case 0xB1: { // CPIR
            uint8 val = readByte(HL.w++);
            BC.w--;
            uint8 saveA = AF.b.h;
            aluSub(val, false);
            AF.b.h = saveA;
            AF.b.l &= ~PARITY;
            if (BC.w != 0 && !getFlag(ZERO)) {
                AF.b.l |= PARITY;
                PC -= 2;
                return 21;
            }
            return 16;
        }

        // 16-bit SBC HL, ss
        case 0x42: { // SBC HL, BC
            int c = getFlag(CARRY) ? 1 : 0;
            int res = (int)HL.w - (int)BC.w - c;
            uint8 f = SUBTRACT;
            if (res < 0) f |= CARRY;
            if (((HL.w & 0x0FFF) - (BC.w & 0x0FFF) - c) < 0) f |= HALF;
            if (((HL.w ^ BC.w) & (HL.w ^ res) & 0x8000) != 0) f |= FLAG_OVERFLOW;
            if ((uint16)res == 0) f |= ZERO;
            if (res & 0x8000) f |= SIGN;
            f |= ((res >> 8) & (F3 | F5));
            HL.w = (uint16)res;
            AF.b.l = f;
            return 15;
        }
        case 0x52: { // SBC HL, DE
            int c = getFlag(CARRY) ? 1 : 0;
            int res = (int)HL.w - (int)DE.w - c;
            uint8 f = SUBTRACT;
            if (res < 0) f |= CARRY;
            if (((HL.w & 0x0FFF) - (DE.w & 0x0FFF) - c) < 0) f |= HALF;
            if (((HL.w ^ DE.w) & (HL.w ^ res) & 0x8000) != 0) f |= FLAG_OVERFLOW;
            if ((uint16)res == 0) f |= ZERO;
            if (res & 0x8000) f |= SIGN;
            f |= ((res >> 8) & (F3 | F5));
            HL.w = (uint16)res;
            AF.b.l = f;
            return 15;
        }
        case 0x62: { // SBC HL, HL
            int c = getFlag(CARRY) ? 1 : 0;
            int res = -c;
            uint8 f = SUBTRACT;
            if (res < 0) f |= CARRY;
            if (((HL.w & 0x0FFF) - (HL.w & 0x0FFF) - c) < 0) f |= HALF;
            if ((uint16)res == 0) f |= ZERO;
            if (res & 0x8000) f |= SIGN;
            HL.w = (uint16)res;
            AF.b.l = f;
            return 15;
        }
        case 0x72: { // SBC HL, SP
            int c = getFlag(CARRY) ? 1 : 0;
            int res = (int)HL.w - (int)SP - c;
            uint8 f = SUBTRACT;
            if (res < 0) f |= CARRY;
            if (((HL.w & 0x0FFF) - (SP & 0x0FFF) - c) < 0) f |= HALF;
            if (((HL.w ^ SP) & (HL.w ^ res) & 0x8000) != 0) f |= FLAG_OVERFLOW;
            if ((uint16)res == 0) f |= ZERO;
            if (res & 0x8000) f |= SIGN;
            f |= ((res >> 8) & (F3 | F5));
            HL.w = (uint16)res;
            AF.b.l = f;
            return 15;
        }

        // 16-bit ADC HL, ss
        case 0x4A: { // ADC HL, BC
            int c = getFlag(CARRY) ? 1 : 0;
            int res = (int)HL.w + (int)BC.w + c;
            uint8 f = 0;
            if (res > 0xFFFF) f |= CARRY;
            if (((HL.w & 0x0FFF) + (BC.w & 0x0FFF) + c) > 0x0FFF) f |= HALF;
            if ((~(HL.w ^ BC.w) & (HL.w ^ res) & 0x8000) != 0) f |= FLAG_OVERFLOW;
            if ((uint16)res == 0) f |= ZERO;
            if (res & 0x8000) f |= SIGN;
            f |= ((res >> 8) & (F3 | F5));
            HL.w = (uint16)res;
            AF.b.l = f;
            return 15;
        }
        case 0x5A: { // ADC HL, DE
            int c = getFlag(CARRY) ? 1 : 0;
            int res = (int)HL.w + (int)DE.w + c;
            uint8 f = 0;
            if (res > 0xFFFF) f |= CARRY;
            if (((HL.w & 0x0FFF) + (DE.w & 0x0FFF) + c) > 0x0FFF) f |= HALF;
            if ((~(HL.w ^ DE.w) & (HL.w ^ res) & 0x8000) != 0) f |= FLAG_OVERFLOW;
            if ((uint16)res == 0) f |= ZERO;
            if (res & 0x8000) f |= SIGN;
            f |= ((res >> 8) & (F3 | F5));
            HL.w = (uint16)res;
            AF.b.l = f;
            return 15;
        }
        case 0x6A: { // ADC HL, HL
            int c = getFlag(CARRY) ? 1 : 0;
            int res = (int)HL.w + (int)HL.w + c;
            uint8 f = 0;
            if (res > 0xFFFF) f |= CARRY;
            if (((HL.w & 0x0FFF) + (HL.w & 0x0FFF) + c) > 0x0FFF) f |= HALF;
            if ((uint16)res == 0) f |= ZERO;
            if (res & 0x8000) f |= SIGN;
            HL.w = (uint16)res;
            AF.b.l = f;
            return 15;
        }
        case 0x7A: { // ADC HL, SP
            int c = getFlag(CARRY) ? 1 : 0;
            int res = (int)HL.w + (int)SP + c;
            uint8 f = 0;
            if (res > 0xFFFF) f |= CARRY;
            if (((HL.w & 0x0FFF) + (SP & 0x0FFF) + c) > 0x0FFF) f |= HALF;
            if ((~(HL.w ^ SP) & (HL.w ^ res) & 0x8000) != 0) f |= FLAG_OVERFLOW;
            if ((uint16)res == 0) f |= ZERO;
            if (res & 0x8000) f |= SIGN;
            f |= ((res >> 8) & (F3 | F5));
            HL.w = (uint16)res;
            AF.b.l = f;
            return 15;
        }

        // 16-bit LD (nn), rr
        case 0x43: { uint16 a = fetchWord(); writeWord(a, BC.w); return 20; }
        case 0x53: { uint16 a = fetchWord(); writeWord(a, DE.w); return 20; }
        case 0x63: { uint16 a = fetchWord(); writeWord(a, HL.w); return 20; }
        case 0x73: { uint16 a = fetchWord(); writeWord(a, SP); return 20; }
        // 16-bit LD rr, (nn)
        case 0x4B: { uint16 a = fetchWord(); BC.w = readWord(a); return 20; }
        case 0x5B: { uint16 a = fetchWord(); DE.w = readWord(a); return 20; }
        case 0x6B: { uint16 a = fetchWord(); HL.w = readWord(a); return 20; }
        case 0x7B: { uint16 a = fetchWord(); SP = readWord(a); return 20; }

        // IN r, (C)
        case 0x40: { uint8 v = inByte(BC.w); BC.b.h = v; uint8 f = (AF.b.l & CARRY); if (v & 0x80) f |= SIGN; if (!v) f |= ZERO; AF.b.l = f; return 12; }
        case 0x48: { uint8 v = inByte(BC.w); BC.b.l = v; uint8 f = (AF.b.l & CARRY); if (v & 0x80) f |= SIGN; if (!v) f |= ZERO; AF.b.l = f; return 12; }
        case 0x50: { uint8 v = inByte(BC.w); DE.b.h = v; uint8 f = (AF.b.l & CARRY); if (v & 0x80) f |= SIGN; if (!v) f |= ZERO; AF.b.l = f; return 12; }
        case 0x58: { uint8 v = inByte(BC.w); DE.b.l = v; uint8 f = (AF.b.l & CARRY); if (v & 0x80) f |= SIGN; if (!v) f |= ZERO; AF.b.l = f; return 12; }
        case 0x60: { uint8 v = inByte(BC.w); HL.b.h = v; uint8 f = (AF.b.l & CARRY); if (v & 0x80) f |= SIGN; if (!v) f |= ZERO; AF.b.l = f; return 12; }
        case 0x68: { uint8 v = inByte(BC.w); HL.b.l = v; uint8 f = (AF.b.l & CARRY); if (v & 0x80) f |= SIGN; if (!v) f |= ZERO; AF.b.l = f; return 12; }
        case 0x78: { uint8 v = inByte(BC.w); AF.b.h = v; uint8 f = (AF.b.l & CARRY); if (v & 0x80) f |= SIGN; if (!v) f |= ZERO; AF.b.l = f; return 12; }

        // OUT (C), r
        case 0x41: outByte(BC.w, BC.b.h); return 12;
        case 0x49: outByte(BC.w, BC.b.l); return 12;
        case 0x51: outByte(BC.w, DE.b.h); return 12;
        case 0x59: outByte(BC.w, DE.b.l); return 12;
        case 0x61: outByte(BC.w, HL.b.h); return 12;
        case 0x69: outByte(BC.w, HL.b.l); return 12;
        case 0x79: outByte(BC.w, AF.b.h); return 12;

        // Interrupt mode
        case 0x46: im = 0; return 8; // IM 0
        case 0x56: im = 1; return 8; // IM 1
        case 0x5E: im = 2; return 8; // IM 2

        case 0x47: I = AF.b.h; return 9; // LD I, A
        case 0x4F: R = AF.b.h; return 9; // LD R, A
        case 0x57: { // LD A, I
            AF.b.h = I;
            uint8 f = (AF.b.l & CARRY);
            if (I & 0x80) f |= SIGN;
            if (I == 0) f |= ZERO;
            if (iff2) f |= PARITY;
            AF.b.l = f;
            return 9;
        }
        case 0x5F: { // LD A, R
            AF.b.h = R;
            uint8 f = (AF.b.l & CARRY);
            if (R & 0x80) f |= SIGN;
            if (R == 0) f |= ZERO;
            if (iff2) f |= PARITY;
            AF.b.l = f;
            return 9;
        }
        case 0x44: { // NEG
            uint8 a = AF.b.h;
            AF.b.h = 0;
            aluSub(a, false);
            return 8;
        }
        case 0x45: // RETN
        case 0x4D: { // RETI
            iff1 = iff2;
            PC = readWord(SP);
            SP += 2;
            return 14;
        }
        default: return 8;
    }
}

int CPUZ80::executeIndex(RegPair& idx, uint8 prefix) {
    uint8 op = fetchByte();

    if (op == 0xCB) {
        int8 d = (int8)fetchByte();
        return executeIndexCB(idx.w + d);
    }

    switch (op) {
        case 0x21: idx.w = fetchWord(); return 14; // LD IX, nn
        case 0x22: { uint16 a = fetchWord(); writeWord(a, idx.w); return 20; } // LD (nn), IX
        case 0x2A: { uint16 a = fetchWord(); idx.w = readWord(a); return 20; } // LD IX, (nn)
        case 0x23: idx.w++; return 10; // INC IX
        case 0x2B: idx.w--; return 10; // DEC IX
        case 0xE1: idx.w = readWord(SP); SP += 2; return 14; // POP IX
        case 0xE5: writeWord(SP - 2, idx.w); SP -= 2; return 15; // PUSH IX
        case 0xE9: PC = idx.w; return 8; // JP (IX)
        case 0xF9: SP = idx.w; return 10; // LD SP, IX
        case 0x34: { int8 d = (int8)fetchByte(); uint16 a = idx.w + d; writeByte(a, aluInc(readByte(a))); return 23; }
        case 0x35: { int8 d = (int8)fetchByte(); uint16 a = idx.w + d; writeByte(a, aluDec(readByte(a))); return 23; }
        case 0x36: { int8 d = (int8)fetchByte(); uint8 n = fetchByte(); writeByte(idx.w + d, n); return 19; }
        case 0x7E: { int8 d = (int8)fetchByte(); AF.b.h = readByte(idx.w + d); return 19; } // LD A, (IX+d)
        case 0x77: { int8 d = (int8)fetchByte(); writeByte(idx.w + d, AF.b.h); return 19; } // LD (IX+d), A
        case 0x09: { int res = idx.w + BC.w; uint8 f = AF.b.l & (SIGN | ZERO | PARITY); if (((idx.w & 0x0FFF) + (BC.w & 0x0FFF)) > 0x0FFF) f |= HALF; if (res > 0xFFFF) f |= CARRY; idx.w = (uint16)res; AF.b.l = f; return 15; }
        case 0x19: { int res = idx.w + DE.w; uint8 f = AF.b.l & (SIGN | ZERO | PARITY); if (((idx.w & 0x0FFF) + (DE.w & 0x0FFF)) > 0x0FFF) f |= HALF; if (res > 0xFFFF) f |= CARRY; idx.w = (uint16)res; AF.b.l = f; return 15; }
        case 0x29: { int res = idx.w + idx.w; uint8 f = AF.b.l & (SIGN | ZERO | PARITY); if (((idx.w & 0x0FFF) + (idx.w & 0x0FFF)) > 0x0FFF) f |= HALF; if (res > 0xFFFF) f |= CARRY; idx.w = (uint16)res; AF.b.l = f; return 15; }
        case 0x39: { int res = idx.w + SP; uint8 f = AF.b.l & (SIGN | ZERO | PARITY); if (((idx.w & 0x0FFF) + (SP & 0x0FFF)) > 0x0FFF) f |= HALF; if (res > 0xFFFF) f |= CARRY; idx.w = (uint16)res; AF.b.l = f; return 15; }
        case 0x86: { int8 d = (int8)fetchByte(); aluAdd(readByte(idx.w + d), false); return 19; }
        case 0x8E: { int8 d = (int8)fetchByte(); aluAdd(readByte(idx.w + d), true); return 19; }
        case 0x96: { int8 d = (int8)fetchByte(); aluSub(readByte(idx.w + d), false); return 19; }
        case 0x9E: { int8 d = (int8)fetchByte(); aluSub(readByte(idx.w + d), true); return 19; }
        case 0xA6: { int8 d = (int8)fetchByte(); aluAnd(readByte(idx.w + d)); return 19; }
        case 0xAE: { int8 d = (int8)fetchByte(); aluXor(readByte(idx.w + d)); return 19; }
        case 0xB6: { int8 d = (int8)fetchByte(); aluOr(readByte(idx.w + d)); return 19; }
        case 0xBE: { int8 d = (int8)fetchByte(); aluCp(readByte(idx.w + d)); return 19; }
        default: {
            if (op >= 0x70 && op <= 0x77 && op != 0x76) {
                int8 d = (int8)fetchByte();
                uint8 val = 0;
                switch (op & 0x07) {
                    case 0: val = BC.b.h; break;
                    case 1: val = BC.b.l; break;
                    case 2: val = DE.b.h; break;
                    case 3: val = DE.b.l; break;
                    case 4: val = HL.b.h; break;
                    case 5: val = HL.b.l; break;
                    case 7: val = AF.b.h; break;
                }
                writeByte(idx.w + d, val);
                return 19;
            }
            if (op == 0x46 || op == 0x4E || op == 0x56 ||
                op == 0x5E || op == 0x66 || op == 0x6E || op == 0x7E) {
                int8 d = (int8)fetchByte();
                uint8 val = readByte(idx.w + d);
                int dst = (op >> 3) & 0x07;
                switch (dst) {
                    case 0: BC.b.h = val; break;
                    case 1: BC.b.l = val; break;
                    case 2: DE.b.h = val; break;
                    case 3: DE.b.l = val; break;
                    case 4: HL.b.h = val; break;
                    case 5: HL.b.l = val; break;
                    case 7: AF.b.h = val; break;
                }
                return 19;
            }
            // Prefix had no effect, execute op as normal instruction
            PC--;
            return 4;
        }
    }
}

int CPUZ80::executeIndexCB(uint16 addr) {
    uint8 op = fetchByte();
    uint8 val = readByte(addr);

    if (op <= 0x3F) {
        int sub = (op >> 3) & 0x07;
        uint8 res = val;
        switch (sub) {
            case 0: res = rlc(val); break;
            case 1: res = rrc(val); break;
            case 2: res = rl(val); break;
            case 3: res = rr(val); break;
            case 4: res = sla(val); break;
            case 5: res = sra(val); break;
            case 6: res = sll(val); break;
            case 7: res = srl(val); break;
        }
        writeByte(addr, res);
        return 23;
    } else if (op <= 0x7F) {
        // BIT b, (IX+d)
        int b = (op >> 3) & 0x07;
        uint8 bit = 1 << b;
        uint8 f = (AF.b.l & CARRY) | HALF;
        if ((val & bit) == 0) f |= (ZERO | PARITY);
        if (b == 7 && (val & 0x80)) f |= SIGN;
        f |= (val & (F3 | F5));
        AF.b.l = f;
        return 20;
    } else if (op <= 0xBF) {
        // RES b, (IX+d)
        int b = (op >> 3) & 0x07;
        writeByte(addr, val & ~(1 << b));
        return 23;
    } else {
        // SET b, (IX+d)
        int b = (op >> 3) & 0x07;
        writeByte(addr, val | (1 << b));
        return 23;
    }
}
