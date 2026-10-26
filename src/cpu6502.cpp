#include "cpu6502.h"

CPU6502::CPU6502() {
    reset();
}

uint8 CPU6502::read(uint16 addr) {
    if (readMem) return readMem(addr);
    return 0xFF;
}

void CPU6502::write(uint16 addr, uint8 val) {
    if (writeMem) writeMem(addr, val);
}

uint16 CPU6502::read16(uint16 addr) {
    uint8 lo = read(addr);
    uint8 hi = read(addr + 1);
    return (uint16)lo | ((uint16)hi << 8);
}

uint16 CPU6502::read16_wrap(uint16 addr) {
    uint8 lo = read(addr);
    uint16 hi_addr;
    if (is65C02) {
        hi_addr = addr + 1;
    } else {
        // NMOS 6502 indirect bug: page boundary wraps around within page
        hi_addr = (addr & 0xFF00) | ((addr + 1) & 0x00FF);
    }
    uint8 hi = read(hi_addr);
    return (uint16)lo | ((uint16)hi << 8);
}

void CPU6502::push(uint8 val) {
    write(0x0100 | SP, val);
    SP--;
}

uint8 CPU6502::pull() {
    SP++;
    return read(0x0100 | SP);
}

void CPU6502::push16(uint16 val) {
    push((uint8)(val >> 8));
    push((uint8)(val & 0xFF));
}

uint16 CPU6502::pull16() {
    uint8 lo = pull();
    uint8 hi = pull();
    return (uint16)lo | ((uint16)hi << 8);
}

void CPU6502::reset() {
    A = 0;
    X = 0;
    Y = 0;
    SP = 0xFD;
    P = FLAG_U | FLAG_I;
    nmiPending = false;
    irqPending = false;
    jam = false;
    cycles = 0;

    PC = read16(0xFFFC);
}

void CPU6502::triggerNMI() {
    nmiPending = true;
}

void CPU6502::triggerIRQ() {
    irqPending = true;
}

void CPU6502::clearIRQ() {
    irqPending = false;
}

void CPU6502::adc(uint8 val) {
    if (getFlag(FLAG_D)) {
        // Decimal mode
        uint8 al = (A & 0x0F) + (val & 0x0F) + (getFlag(FLAG_C) ? 1 : 0);
        uint16 bin_sum = (uint16)A + (uint16)val + (getFlag(FLAG_C) ? 1 : 0);
        setFlag(FLAG_V, (~(A ^ val) & (A ^ (uint8)bin_sum) & 0x80) != 0);
        
        if (al > 9) al += 6;
        uint16 ah = (A >> 4) + (val >> 4) + (al > 0x0F ? 1 : 0);
        if (ah > 9) ah += 6;
        setFlag(FLAG_C, ah > 0x0F);
        A = ((ah & 0x0F) << 4) | (al & 0x0F);
        setFlag(FLAG_Z, (bin_sum & 0xFF) == 0);
        setFlag(FLAG_N, (A & 0x80) != 0);
    } else {
        uint16 sum = (uint16)A + (uint16)val + (getFlag(FLAG_C) ? 1 : 0);
        setFlag(FLAG_C, sum > 0xFF);
        setFlag(FLAG_V, (~(A ^ val) & (A ^ (uint8)sum) & 0x80) != 0);
        A = (uint8)sum;
        setZN(A);
    }
}

void CPU6502::sbc(uint8 val) {
    if (getFlag(FLAG_D)) {
        uint16 bin_diff = (uint16)A - (uint16)val - (getFlag(FLAG_C) ? 0 : 1);
        setFlag(FLAG_V, ((A ^ val) & (A ^ (uint8)bin_diff) & 0x80) != 0);

        int al = (A & 0x0F) - (val & 0x0F) - (getFlag(FLAG_C) ? 0 : 1);
        int ah = (A >> 4) - (val >> 4);
        if (al < 0) {
            al -= 6;
            ah--;
        }
        if (ah < 0) {
            ah -= 6;
        }
        setFlag(FLAG_C, bin_diff < 0x100);
        A = ((ah & 0x0F) << 4) | (al & 0x0F);
        setFlag(FLAG_Z, (bin_diff & 0xFF) == 0);
        setFlag(FLAG_N, (bin_diff & 0x80) != 0);
    } else {
        uint16 diff = (uint16)A - (uint16)val - (getFlag(FLAG_C) ? 0 : 1);
        setFlag(FLAG_C, diff < 0x100);
        setFlag(FLAG_V, ((A ^ val) & (A ^ (uint8)diff) & 0x80) != 0);
        A = (uint8)diff;
        setZN(A);
    }
}

void CPU6502::cmp(uint8 reg, uint8 val) {
    uint16 diff = (uint16)reg - (uint16)val;
    setFlag(FLAG_C, reg >= val);
    setZN((uint8)diff);
}

int CPU6502::step() {
    if (jam) return 1;

    // Check interrupts
    if (nmiPending) {
        nmiPending = false;
        push16(PC);
        push((P & ~FLAG_B) | FLAG_U);
        setFlag(FLAG_I, true);
        PC = read16(0xFFFA);
        cycles += 7;
        return 7;
    }

    if (irqPending && !getFlag(FLAG_I)) {
        push16(PC);
        push((P & ~FLAG_B) | FLAG_U);
        setFlag(FLAG_I, true);
        PC = read16(0xFFFE);
        cycles += 7;
        return 7;
    }

    uint8 opcode = read(PC++);
    int opCycles = 2;

    auto fetchByte = [&]() -> uint8 {
        return read(PC++);
    };

    auto fetchWord = [&]() -> uint16 {
        uint16 val = read16(PC);
        PC += 2;
        return val;
    };

    auto branch = [&](bool condition) {
        int8 offset = (int8)fetchByte();
        opCycles = 2;
        if (condition) {
            opCycles++;
            uint16 oldPC = PC;
            PC += offset;
            if ((oldPC & 0xFF00) != (PC & 0xFF00)) opCycles++;
        }
    };

    switch (opcode) {
        // --- ADC ---
        case 0x69: adc(fetchByte()); opCycles = 2; break; // Imm
        case 0x65: adc(read(fetchByte())); opCycles = 3; break; // ZP
        case 0x75: adc(read((fetchByte() + X) & 0xFF)); opCycles = 4; break; // ZP,X
        case 0x6D: adc(read(fetchWord())); opCycles = 4; break; // Abs
        case 0x7D: { // Abs,X
            uint16 base = fetchWord();
            uint16 addr = base + X;
            adc(read(addr));
            opCycles = 4 + (((base ^ addr) >> 8) ? 1 : 0);
            break;
        }
        case 0x79: { // Abs,Y
            uint16 base = fetchWord();
            uint16 addr = base + Y;
            adc(read(addr));
            opCycles = 4 + (((base ^ addr) >> 8) ? 1 : 0);
            break;
        }
        case 0x61: { // (ZP,X)
            uint8 zp = (fetchByte() + X) & 0xFF;
            uint16 addr = read(zp) | ((uint16)read((zp + 1) & 0xFF) << 8);
            adc(read(addr));
            opCycles = 6;
            break;
        }
        case 0x71: { // (ZP),Y
            uint8 zp = fetchByte();
            uint16 base = read(zp) | ((uint16)read((zp + 1) & 0xFF) << 8);
            uint16 addr = base + Y;
            adc(read(addr));
            opCycles = 5 + (((base ^ addr) >> 8) ? 1 : 0);
            break;
        }
        case 0x72: // 65C02 (ZP)
            if (is65C02) {
                uint8 zp = fetchByte();
                uint16 addr = read(zp) | ((uint16)read((zp + 1) & 0xFF) << 8);
                adc(read(addr));
                opCycles = 5;
            } else { jam = true; }
            break;

        // --- AND ---
        case 0x29: A &= fetchByte(); setZN(A); opCycles = 2; break;
        case 0x25: A &= read(fetchByte()); setZN(A); opCycles = 3; break;
        case 0x35: A &= read((fetchByte() + X) & 0xFF); setZN(A); opCycles = 4; break;
        case 0x2D: A &= read(fetchWord()); setZN(A); opCycles = 4; break;
        case 0x3D: {
            uint16 base = fetchWord();
            uint16 addr = base + X;
            A &= read(addr); setZN(A);
            opCycles = 4 + (((base ^ addr) >> 8) ? 1 : 0);
            break;
        }
        case 0x39: {
            uint16 base = fetchWord();
            uint16 addr = base + Y;
            A &= read(addr); setZN(A);
            opCycles = 4 + (((base ^ addr) >> 8) ? 1 : 0);
            break;
        }
        case 0x21: {
            uint8 zp = (fetchByte() + X) & 0xFF;
            uint16 addr = read(zp) | ((uint16)read((zp + 1) & 0xFF) << 8);
            A &= read(addr); setZN(A);
            opCycles = 6;
            break;
        }
        case 0x31: {
            uint8 zp = fetchByte();
            uint16 base = read(zp) | ((uint16)read((zp + 1) & 0xFF) << 8);
            uint16 addr = base + Y;
            A &= read(addr); setZN(A);
            opCycles = 5 + (((base ^ addr) >> 8) ? 1 : 0);
            break;
        }
        case 0x32:
            if (is65C02) {
                uint8 zp = fetchByte();
                uint16 addr = read(zp) | ((uint16)read((zp + 1) & 0xFF) << 8);
                A &= read(addr); setZN(A);
                opCycles = 5;
            } else { jam = true; }
            break;

        // --- ASL ---
        case 0x0A: {
            setFlag(FLAG_C, (A & 0x80) != 0);
            A <<= 1;
            setZN(A);
            opCycles = 2;
            break;
        }
        case 0x06: {
            uint8 zp = fetchByte();
            uint8 v = read(zp);
            setFlag(FLAG_C, (v & 0x80) != 0);
            v <<= 1;
            write(zp, v);
            setZN(v);
            opCycles = 5;
            break;
        }
        case 0x16: {
            uint8 zp = (fetchByte() + X) & 0xFF;
            uint8 v = read(zp);
            setFlag(FLAG_C, (v & 0x80) != 0);
            v <<= 1;
            write(zp, v);
            setZN(v);
            opCycles = 6;
            break;
        }
        case 0x0E: {
            uint16 addr = fetchWord();
            uint8 v = read(addr);
            setFlag(FLAG_C, (v & 0x80) != 0);
            v <<= 1;
            write(addr, v);
            setZN(v);
            opCycles = 6;
            break;
        }
        case 0x1E: {
            uint16 addr = fetchWord() + X;
            uint8 v = read(addr);
            setFlag(FLAG_C, (v & 0x80) != 0);
            v <<= 1;
            write(addr, v);
            setZN(v);
            opCycles = 7;
            break;
        }

        // --- Branches ---
        case 0x90: branch(!getFlag(FLAG_C)); break; // BCC
        case 0xB0: branch(getFlag(FLAG_C)); break;  // BCS
        case 0xF0: branch(getFlag(FLAG_Z)); break;  // BEQ
        case 0xD0: branch(!getFlag(FLAG_Z)); break; // BNE
        case 0x30: branch(getFlag(FLAG_N)); break;  // BMI
        case 0x10: branch(!getFlag(FLAG_N)); break; // BPL
        case 0x50: branch(!getFlag(FLAG_V)); break; // BVC
        case 0x70: branch(getFlag(FLAG_V)); break;  // BVS
        case 0x80: // 65C02 BRA
            if (is65C02) {
                branch(true);
            } else {
                // NOP on 6502 (2 bytes)
                fetchByte();
                opCycles = 2;
            }
            break;

        // --- BIT ---
        case 0x24: {
            uint8 v = read(fetchByte());
            setFlag(FLAG_Z, (A & v) == 0);
            setFlag(FLAG_N, (v & 0x80) != 0);
            setFlag(FLAG_V, (v & 0x40) != 0);
            opCycles = 3;
            break;
        }
        case 0x2C: {
            uint8 v = read(fetchWord());
            setFlag(FLAG_Z, (A & v) == 0);
            setFlag(FLAG_N, (v & 0x80) != 0);
            setFlag(FLAG_V, (v & 0x40) != 0);
            opCycles = 4;
            break;
        }
        case 0x89: // 65C02 BIT #imm
            if (is65C02) {
                uint8 v = fetchByte();
                setFlag(FLAG_Z, (A & v) == 0);
                opCycles = 2;
            } else { jam = true; }
            break;
        case 0x34: // 65C02 BIT ZP,X
            if (is65C02) {
                uint8 v = read((fetchByte() + X) & 0xFF);
                setFlag(FLAG_Z, (A & v) == 0);
                setFlag(FLAG_N, (v & 0x80) != 0);
                setFlag(FLAG_V, (v & 0x40) != 0);
                opCycles = 4;
            } else { jam = true; }
            break;
        case 0x3C: // 65C02 BIT Abs,X
            if (is65C02) {
                uint8 v = read(fetchWord() + X);
                setFlag(FLAG_Z, (A & v) == 0);
                setFlag(FLAG_N, (v & 0x80) != 0);
                setFlag(FLAG_V, (v & 0x40) != 0);
                opCycles = 4;
            } else { jam = true; }
            break;

        // --- BRK ---
        case 0x00:
            PC++;
            push16(PC);
            push(P | FLAG_B | FLAG_U);
            setFlag(FLAG_I, true);
            if (is65C02) setFlag(FLAG_D, false);
            PC = read16(0xFFFE);
            opCycles = 7;
            break;

        // --- CLC, CLD, CLI, CLV ---
        case 0x18: setFlag(FLAG_C, false); opCycles = 2; break;
        case 0xD8: setFlag(FLAG_D, false); opCycles = 2; break;
        case 0x58: setFlag(FLAG_I, false); opCycles = 2; break;
        case 0xB8: setFlag(FLAG_V, false); opCycles = 2; break;

        // --- CMP ---
        case 0xC9: cmp(A, fetchByte()); opCycles = 2; break;
        case 0xC5: cmp(A, read(fetchByte())); opCycles = 3; break;
        case 0xD5: cmp(A, read((fetchByte() + X) & 0xFF)); opCycles = 4; break;
        case 0xCD: cmp(A, read(fetchWord())); opCycles = 4; break;
        case 0xDD: {
            uint16 base = fetchWord();
            uint16 addr = base + X;
            cmp(A, read(addr));
            opCycles = 4 + (((base ^ addr) >> 8) ? 1 : 0);
            break;
        }
        case 0xD9: {
            uint16 base = fetchWord();
            uint16 addr = base + Y;
            cmp(A, read(addr));
            opCycles = 4 + (((base ^ addr) >> 8) ? 1 : 0);
            break;
        }
        case 0xC1: {
            uint8 zp = (fetchByte() + X) & 0xFF;
            uint16 addr = read(zp) | ((uint16)read((zp + 1) & 0xFF) << 8);
            cmp(A, read(addr));
            opCycles = 6;
            break;
        }
        case 0xD1: {
            uint8 zp = fetchByte();
            uint16 base = read(zp) | ((uint16)read((zp + 1) & 0xFF) << 8);
            uint16 addr = base + Y;
            cmp(A, read(addr));
            opCycles = 5 + (((base ^ addr) >> 8) ? 1 : 0);
            break;
        }
        case 0xD2:
            if (is65C02) {
                uint8 zp = fetchByte();
                uint16 addr = read(zp) | ((uint16)read((zp + 1) & 0xFF) << 8);
                cmp(A, read(addr));
                opCycles = 5;
            } else { jam = true; }
            break;

        // --- CPX ---
        case 0xE0: cmp(X, fetchByte()); opCycles = 2; break;
        case 0xE4: cmp(X, read(fetchByte())); opCycles = 3; break;
        case 0xEC: cmp(X, read(fetchWord())); opCycles = 4; break;

        // --- CPY ---
        case 0xC0: cmp(Y, fetchByte()); opCycles = 2; break;
        case 0xC4: cmp(Y, read(fetchByte())); opCycles = 3; break;
        case 0xCC: cmp(Y, read(fetchWord())); opCycles = 4; break;

        // --- DEC ---
        case 0xC6: {
            uint8 zp = fetchByte();
            uint8 v = read(zp) - 1;
            write(zp, v);
            setZN(v);
            opCycles = 5;
            break;
        }
        case 0xD6: {
            uint8 zp = (fetchByte() + X) & 0xFF;
            uint8 v = read(zp) - 1;
            write(zp, v);
            setZN(v);
            opCycles = 6;
            break;
        }
        case 0xCE: {
            uint16 addr = fetchWord();
            uint8 v = read(addr) - 1;
            write(addr, v);
            setZN(v);
            opCycles = 6;
            break;
        }
        case 0xDE: {
            uint16 addr = fetchWord() + X;
            uint8 v = read(addr) - 1;
            write(addr, v);
            setZN(v);
            opCycles = 7;
            break;
        }
        case 0x3A: // 65C02 DEA (DEC A)
            if (is65C02) {
                A--;
                setZN(A);
                opCycles = 2;
            } else { jam = true; }
            break;
        case 0xCA: X--; setZN(X); opCycles = 2; break; // DEX
        case 0x88: Y--; setZN(Y); opCycles = 2; break; // DEY

        // --- EOR ---
        case 0x49: A ^= fetchByte(); setZN(A); opCycles = 2; break;
        case 0x45: A ^= read(fetchByte()); setZN(A); opCycles = 3; break;
        case 0x55: A ^= read((fetchByte() + X) & 0xFF); setZN(A); opCycles = 4; break;
        case 0x4D: A ^= read(fetchWord()); setZN(A); opCycles = 4; break;
        case 0x5D: {
            uint16 base = fetchWord();
            uint16 addr = base + X;
            A ^= read(addr); setZN(A);
            opCycles = 4 + (((base ^ addr) >> 8) ? 1 : 0);
            break;
        }
        case 0x59: {
            uint16 base = fetchWord();
            uint16 addr = base + Y;
            A ^= read(addr); setZN(A);
            opCycles = 4 + (((base ^ addr) >> 8) ? 1 : 0);
            break;
        }
        case 0x41: {
            uint8 zp = (fetchByte() + X) & 0xFF;
            uint16 addr = read(zp) | ((uint16)read((zp + 1) & 0xFF) << 8);
            A ^= read(addr); setZN(A);
            opCycles = 6;
            break;
        }
        case 0x51: {
            uint8 zp = fetchByte();
            uint16 base = read(zp) | ((uint16)read((zp + 1) & 0xFF) << 8);
            uint16 addr = base + Y;
            A ^= read(addr); setZN(A);
            opCycles = 5 + (((base ^ addr) >> 8) ? 1 : 0);
            break;
        }
        case 0x52:
            if (is65C02) {
                uint8 zp = fetchByte();
                uint16 addr = read(zp) | ((uint16)read((zp + 1) & 0xFF) << 8);
                A ^= read(addr); setZN(A);
                opCycles = 5;
            } else { jam = true; }
            break;

        // --- INC ---
        case 0xE6: {
            uint8 zp = fetchByte();
            uint8 v = read(zp) + 1;
            write(zp, v);
            setZN(v);
            opCycles = 5;
            break;
        }
        case 0xF6: {
            uint8 zp = (fetchByte() + X) & 0xFF;
            uint8 v = read(zp) + 1;
            write(zp, v);
            setZN(v);
            opCycles = 6;
            break;
        }
        case 0xEE: {
            uint16 addr = fetchWord();
            uint8 v = read(addr) + 1;
            write(addr, v);
            setZN(v);
            opCycles = 6;
            break;
        }
        case 0xFE: {
            uint16 addr = fetchWord() + X;
            uint8 v = read(addr) + 1;
            write(addr, v);
            setZN(v);
            opCycles = 7;
            break;
        }
        case 0x1A: // 65C02 INA (INC A)
            if (is65C02) {
                A++;
                setZN(A);
                opCycles = 2;
            } else { jam = true; }
            break;
        case 0xE8: X++; setZN(X); opCycles = 2; break; // INX
        case 0xC8: Y++; setZN(Y); opCycles = 2; break; // INY

        // --- JMP ---
        case 0x4C: PC = fetchWord(); opCycles = 3; break; // Abs
        case 0x6C: { // Indirect
            uint16 ptr = fetchWord();
            PC = read16_wrap(ptr);
            opCycles = 5 + (is65C02 ? 1 : 0);
            break;
        }
        case 0x7C: // 65C02 JMP (abs,X)
            if (is65C02) {
                uint16 ptr = fetchWord() + X;
                PC = read16(ptr);
                opCycles = 6;
            } else { jam = true; }
            break;

        // --- JSR ---
        case 0x20: {
            uint16 target = fetchWord();
            push16(PC - 1);
            PC = target;
            opCycles = 6;
            break;
        }

        // --- LDA ---
        case 0xA9: A = fetchByte(); setZN(A); opCycles = 2; break;
        case 0xA5: A = read(fetchByte()); setZN(A); opCycles = 3; break;
        case 0xB5: A = read((fetchByte() + X) & 0xFF); setZN(A); opCycles = 4; break;
        case 0xAD: A = read(fetchWord()); setZN(A); opCycles = 4; break;
        case 0xBD: {
            uint16 base = fetchWord();
            uint16 addr = base + X;
            A = read(addr); setZN(A);
            opCycles = 4 + (((base ^ addr) >> 8) ? 1 : 0);
            break;
        }
        case 0xB9: {
            uint16 base = fetchWord();
            uint16 addr = base + Y;
            A = read(addr); setZN(A);
            opCycles = 4 + (((base ^ addr) >> 8) ? 1 : 0);
            break;
        }
        case 0xA1: {
            uint8 zp = (fetchByte() + X) & 0xFF;
            uint16 addr = read(zp) | ((uint16)read((zp + 1) & 0xFF) << 8);
            A = read(addr); setZN(A);
            opCycles = 6;
            break;
        }
        case 0xB1: {
            uint8 zp = fetchByte();
            uint16 base = read(zp) | ((uint16)read((zp + 1) & 0xFF) << 8);
            uint16 addr = base + Y;
            A = read(addr); setZN(A);
            opCycles = 5 + (((base ^ addr) >> 8) ? 1 : 0);
            break;
        }
        case 0xB2: // 65C02 (ZP)
            if (is65C02) {
                uint8 zp = fetchByte();
                uint16 addr = read(zp) | ((uint16)read((zp + 1) & 0xFF) << 8);
                A = read(addr); setZN(A);
                opCycles = 5;
            } else { jam = true; }
            break;

        // --- LDX ---
        case 0xA2: X = fetchByte(); setZN(X); opCycles = 2; break;
        case 0xA6: X = read(fetchByte()); setZN(X); opCycles = 3; break;
        case 0xB6: X = read((fetchByte() + Y) & 0xFF); setZN(X); opCycles = 4; break;
        case 0xAE: X = read(fetchWord()); setZN(X); opCycles = 4; break;
        case 0xBE: {
            uint16 base = fetchWord();
            uint16 addr = base + Y;
            X = read(addr); setZN(X);
            opCycles = 4 + (((base ^ addr) >> 8) ? 1 : 0);
            break;
        }

        // --- LDY ---
        case 0xA0: Y = fetchByte(); setZN(Y); opCycles = 2; break;
        case 0xA4: Y = read(fetchByte()); setZN(Y); opCycles = 3; break;
        case 0xB4: Y = read((fetchByte() + X) & 0xFF); setZN(Y); opCycles = 4; break;
        case 0xAC: Y = read(fetchWord()); setZN(Y); opCycles = 4; break;
        case 0xBC: {
            uint16 base = fetchWord();
            uint16 addr = base + X;
            Y = read(addr); setZN(Y);
            opCycles = 4 + (((base ^ addr) >> 8) ? 1 : 0);
            break;
        }

        // --- LSR ---
        case 0x4A: {
            setFlag(FLAG_C, (A & 1) != 0);
            A >>= 1;
            setZN(A);
            opCycles = 2;
            break;
        }
        case 0x46: {
            uint8 zp = fetchByte();
            uint8 v = read(zp);
            setFlag(FLAG_C, (v & 1) != 0);
            v >>= 1;
            write(zp, v);
            setZN(v);
            opCycles = 5;
            break;
        }
        case 0x56: {
            uint8 zp = (fetchByte() + X) & 0xFF;
            uint8 v = read(zp);
            setFlag(FLAG_C, (v & 1) != 0);
            v >>= 1;
            write(zp, v);
            setZN(v);
            opCycles = 6;
            break;
        }
        case 0x4E: {
            uint16 addr = fetchWord();
            uint8 v = read(addr);
            setFlag(FLAG_C, (v & 1) != 0);
            v >>= 1;
            write(addr, v);
            setZN(v);
            opCycles = 6;
            break;
        }
        case 0x5E: {
            uint16 addr = fetchWord() + X;
            uint8 v = read(addr);
            setFlag(FLAG_C, (v & 1) != 0);
            v >>= 1;
            write(addr, v);
            setZN(v);
            opCycles = 7;
            break;
        }

        // --- NOP ---
        case 0xEA: opCycles = 2; break;

        // --- ORA ---
        case 0x09: A |= fetchByte(); setZN(A); opCycles = 2; break;
        case 0x05: A |= read(fetchByte()); setZN(A); opCycles = 3; break;
        case 0x15: A |= read((fetchByte() + X) & 0xFF); setZN(A); opCycles = 4; break;
        case 0x0D: A |= read(fetchWord()); setZN(A); opCycles = 4; break;
        case 0x1D: {
            uint16 base = fetchWord();
            uint16 addr = base + X;
            A |= read(addr); setZN(A);
            opCycles = 4 + (((base ^ addr) >> 8) ? 1 : 0);
            break;
        }
        case 0x19: {
            uint16 base = fetchWord();
            uint16 addr = base + Y;
            A |= read(addr); setZN(A);
            opCycles = 4 + (((base ^ addr) >> 8) ? 1 : 0);
            break;
        }
        case 0x01: {
            uint8 zp = (fetchByte() + X) & 0xFF;
            uint16 addr = read(zp) | ((uint16)read((zp + 1) & 0xFF) << 8);
            A |= read(addr); setZN(A);
            opCycles = 6;
            break;
        }
        case 0x11: {
            uint8 zp = fetchByte();
            uint16 base = read(zp) | ((uint16)read((zp + 1) & 0xFF) << 8);
            uint16 addr = base + Y;
            A |= read(addr); setZN(A);
            opCycles = 5 + (((base ^ addr) >> 8) ? 1 : 0);
            break;
        }
        case 0x12:
            if (is65C02) {
                uint8 zp = fetchByte();
                uint16 addr = read(zp) | ((uint16)read((zp + 1) & 0xFF) << 8);
                A |= read(addr); setZN(A);
                opCycles = 5;
            } else { jam = true; }
            break;

        // --- PHA, PHP, PLA, PLP ---
        case 0x48: push(A); opCycles = 3; break;
        case 0x08: push(P | FLAG_B | FLAG_U); opCycles = 3; break;
        case 0x68: A = pull(); setZN(A); opCycles = 4; break;
        case 0x28: P = pull() | FLAG_U; opCycles = 4; break;

        // --- 65C02 PHX, PHY, PLX, PLY ---
        case 0xDA: if (is65C02) { push(X); opCycles = 3; } else jam = true; break; // PHX
        case 0x5A: if (is65C02) { push(Y); opCycles = 3; } else jam = true; break; // PHY
        case 0xFA: if (is65C02) { X = pull(); setZN(X); opCycles = 4; } else jam = true; break; // PLX
        case 0x7A: if (is65C02) { Y = pull(); setZN(Y); opCycles = 4; } else jam = true; break; // PLY

        // --- ROL ---
        case 0x2A: {
            bool c = getFlag(FLAG_C);
            setFlag(FLAG_C, (A & 0x80) != 0);
            A = (A << 1) | (c ? 1 : 0);
            setZN(A);
            opCycles = 2;
            break;
        }
        case 0x26: {
            uint8 zp = fetchByte();
            uint8 v = read(zp);
            bool c = getFlag(FLAG_C);
            setFlag(FLAG_C, (v & 0x80) != 0);
            v = (v << 1) | (c ? 1 : 0);
            write(zp, v);
            setZN(v);
            opCycles = 5;
            break;
        }
        case 0x36: {
            uint8 zp = (fetchByte() + X) & 0xFF;
            uint8 v = read(zp);
            bool c = getFlag(FLAG_C);
            setFlag(FLAG_C, (v & 0x80) != 0);
            v = (v << 1) | (c ? 1 : 0);
            write(zp, v);
            setZN(v);
            opCycles = 6;
            break;
        }
        case 0x2E: {
            uint16 addr = fetchWord();
            uint8 v = read(addr);
            bool c = getFlag(FLAG_C);
            setFlag(FLAG_C, (v & 0x80) != 0);
            v = (v << 1) | (c ? 1 : 0);
            write(addr, v);
            setZN(v);
            opCycles = 6;
            break;
        }
        case 0x3E: {
            uint16 addr = fetchWord() + X;
            uint8 v = read(addr);
            bool c = getFlag(FLAG_C);
            setFlag(FLAG_C, (v & 0x80) != 0);
            v = (v << 1) | (c ? 1 : 0);
            write(addr, v);
            setZN(v);
            opCycles = 7;
            break;
        }

        // --- ROR ---
        case 0x6A: {
            bool c = getFlag(FLAG_C);
            setFlag(FLAG_C, (A & 1) != 0);
            A = (A >> 1) | (c ? 0x80 : 0);
            setZN(A);
            opCycles = 2;
            break;
        }
        case 0x66: {
            uint8 zp = fetchByte();
            uint8 v = read(zp);
            bool c = getFlag(FLAG_C);
            setFlag(FLAG_C, (v & 1) != 0);
            v = (v >> 1) | (c ? 0x80 : 0);
            write(zp, v);
            setZN(v);
            opCycles = 5;
            break;
        }
        case 0x76: {
            uint8 zp = (fetchByte() + X) & 0xFF;
            uint8 v = read(zp);
            bool c = getFlag(FLAG_C);
            setFlag(FLAG_C, (v & 1) != 0);
            v = (v >> 1) | (c ? 0x80 : 0);
            write(zp, v);
            setZN(v);
            opCycles = 6;
            break;
        }
        case 0x6E: {
            uint16 addr = fetchWord();
            uint8 v = read(addr);
            bool c = getFlag(FLAG_C);
            setFlag(FLAG_C, (v & 1) != 0);
            v = (v >> 1) | (c ? 0x80 : 0);
            write(addr, v);
            setZN(v);
            opCycles = 6;
            break;
        }
        case 0x7E: {
            uint16 addr = fetchWord() + X;
            uint8 v = read(addr);
            bool c = getFlag(FLAG_C);
            setFlag(FLAG_C, (v & 1) != 0);
            v = (v >> 1) | (c ? 0x80 : 0);
            write(addr, v);
            setZN(v);
            opCycles = 7;
            break;
        }

        // --- RTI, RTS ---
        case 0x40: {
            P = pull() | FLAG_U;
            PC = pull16();
            opCycles = 6;
            break;
        }
        case 0x60: {
            PC = pull16() + 1;
            opCycles = 6;
            break;
        }

        // --- SBC ---
        case 0xE9: sbc(fetchByte()); opCycles = 2; break;
        case 0xE5: sbc(read(fetchByte())); opCycles = 3; break;
        case 0xF5: sbc(read((fetchByte() + X) & 0xFF)); opCycles = 4; break;
        case 0xED: sbc(read(fetchWord())); opCycles = 4; break;
        case 0xFD: {
            uint16 base = fetchWord();
            uint16 addr = base + X;
            sbc(read(addr));
            opCycles = 4 + (((base ^ addr) >> 8) ? 1 : 0);
            break;
        }
        case 0xF9: {
            uint16 base = fetchWord();
            uint16 addr = base + Y;
            sbc(read(addr));
            opCycles = 4 + (((base ^ addr) >> 8) ? 1 : 0);
            break;
        }
        case 0xE1: {
            uint8 zp = (fetchByte() + X) & 0xFF;
            uint16 addr = read(zp) | ((uint16)read((zp + 1) & 0xFF) << 8);
            sbc(read(addr));
            opCycles = 6;
            break;
        }
        case 0xF1: {
            uint8 zp = fetchByte();
            uint16 base = read(zp) | ((uint16)read((zp + 1) & 0xFF) << 8);
            uint16 addr = base + Y;
            sbc(read(addr));
            opCycles = 5 + (((base ^ addr) >> 8) ? 1 : 0);
            break;
        }
        case 0xF2:
            if (is65C02) {
                uint8 zp = fetchByte();
                uint16 addr = read(zp) | ((uint16)read((zp + 1) & 0xFF) << 8);
                sbc(read(addr));
                opCycles = 5;
            } else { jam = true; }
            break;

        // --- SEC, SED, SEI ---
        case 0x38: setFlag(FLAG_C, true); opCycles = 2; break;
        case 0xF8: setFlag(FLAG_D, true); opCycles = 2; break;
        case 0x78: setFlag(FLAG_I, true); opCycles = 2; break;

        // --- STA ---
        case 0x85: write(fetchByte(), A); opCycles = 3; break;
        case 0x95: write((fetchByte() + X) & 0xFF, A); opCycles = 4; break;
        case 0x8D: write(fetchWord(), A); opCycles = 4; break;
        case 0x9D: write(fetchWord() + X, A); opCycles = 5; break;
        case 0x99: write(fetchWord() + Y, A); opCycles = 5; break;
        case 0x81: {
            uint8 zp = (fetchByte() + X) & 0xFF;
            uint16 addr = read(zp) | ((uint16)read((zp + 1) & 0xFF) << 8);
            write(addr, A);
            opCycles = 6;
            break;
        }
        case 0x91: {
            uint8 zp = fetchByte();
            uint16 addr = (read(zp) | ((uint16)read((zp + 1) & 0xFF) << 8)) + Y;
            write(addr, A);
            opCycles = 6;
            break;
        }
        case 0x92:
            if (is65C02) {
                uint8 zp = fetchByte();
                uint16 addr = read(zp) | ((uint16)read((zp + 1) & 0xFF) << 8);
                write(addr, A);
                opCycles = 5;
            } else { jam = true; }
            break;

        // --- STX ---
        case 0x86: write(fetchByte(), X); opCycles = 3; break;
        case 0x96: write((fetchByte() + Y) & 0xFF, X); opCycles = 4; break;
        case 0x8E: write(fetchWord(), X); opCycles = 4; break;

        // --- STY ---
        case 0x84: write(fetchByte(), Y); opCycles = 3; break;
        case 0x94: write((fetchByte() + X) & 0xFF, Y); opCycles = 4; break;
        case 0x8C: write(fetchWord(), Y); opCycles = 4; break;

        // --- 65C02 STZ ---
        case 0x64: if (is65C02) { write(fetchByte(), 0); opCycles = 3; } else jam = true; break;
        case 0x74: if (is65C02) { write((fetchByte() + X) & 0xFF, 0); opCycles = 4; } else jam = true; break;
        case 0x9C: if (is65C02) { write(fetchWord(), 0); opCycles = 4; } else jam = true; break;
        case 0x9E: if (is65C02) { write(fetchWord() + X, 0); opCycles = 5; } else jam = true; break;

        // --- TAX, TAY, TSX, TXA, TXS, TYA ---
        case 0xAA: X = A; setZN(X); opCycles = 2; break;
        case 0xA8: Y = A; setZN(Y); opCycles = 2; break;
        case 0xBA: X = SP; setZN(X); opCycles = 2; break;
        case 0x8A: A = X; setZN(A); opCycles = 2; break;
        case 0x9A: SP = X; opCycles = 2; break;
        case 0x98: A = Y; setZN(A); opCycles = 2; break;

        // --- 65C02 TRB, TSB ---
        case 0x14: // TRB ZP
            if (is65C02) {
                uint8 zp = fetchByte();
                uint8 v = read(zp);
                setFlag(FLAG_Z, (A & v) == 0);
                write(zp, v & ~A);
                opCycles = 5;
            } else { jam = true; }
            break;
        case 0x1C: // TRB Abs
            if (is65C02) {
                uint16 addr = fetchWord();
                uint8 v = read(addr);
                setFlag(FLAG_Z, (A & v) == 0);
                write(addr, v & ~A);
                opCycles = 6;
            } else { jam = true; }
            break;
        case 0x04: // TSB ZP
            if (is65C02) {
                uint8 zp = fetchByte();
                uint8 v = read(zp);
                setFlag(FLAG_Z, (A & v) == 0);
                write(zp, v | A);
                opCycles = 5;
            } else { jam = true; }
            break;
        case 0x0C: // TSB Abs
            if (is65C02) {
                uint16 addr = fetchWord();
                uint8 v = read(addr);
                setFlag(FLAG_Z, (A & v) == 0);
                write(addr, v | A);
                opCycles = 6;
            } else { jam = true; }
            break;

        default:
            // Non-destructive fallback for undefined instructions
            opCycles = 2;
            break;
    }

    cycles += opCycles;
    return opCycles;
}
