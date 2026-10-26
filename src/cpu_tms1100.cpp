#include "cpu_tms1100.h"
#include <cstring>

CPUTMS1100::CPUTMS1100() {
    rom = nullptr;
    romChecksum = 0;
    reset();
}

void CPUTMS1100::setROM(const uint8* romData, uint16 checksum) {
    rom = romData;
    romChecksum = checksum;
}

void CPUTMS1100::reset() {
    a = 0;
    x = 0;
    y = 0;
    status = 1;
    lastStatus = 1;
    ca = cb = cs = 0;
    pa = pb = 0x0F;
    pc = 0;
    sr = 0;
    cl = 0;
    sl = 0;
    o = 0;
    for (int i = 0; i < 16; i++) r[i] = 0;
    std::memset(ram, 0, sizeof(ram));
    cycleCounter = 0;
    frameCounter = (500000 / 6) / 30; // ~2777 cycles per frame
}

void CPUTMS1100::brCommand(int address) {
    if (cl == 0) {
        ca = cb;
        pa = pb;
        pc = address & 0x3F;
    } else {
        ca = cb;
        pc = address & 0x3F;
    }
}

void CPUTMS1100::callCommand(int address) {
    if (cl == 0) {
        cs = ca;
        ca = cb;
        uint8 temp = pa;
        pa = pb;
        pb = temp;
        sr = pc;
        pc = address & 0x3F;
        cl = 1;
    } else {
        ca = cb;
        pb = pa;
        pc = address & 0x3F;
    }
}

void CPUTMS1100::retnCommand() {
    if (cl != 0) {
        pc = sr;
        pa = pb;
        ca = cs;
        cl = 0;
    } else {
        pa = pb;
    }
}

int CPUTMS1100::execute(int cycles) {
    if (!rom) return cycles;

    int executed = 0;
    while (executed < cycles) {
        uint16 pctr = getPCTR();
        uint8 opcode = rom[pctr];

        pc = (pc + 1) & 0x3F;
        lastStatus = status;
        status = 1;
        cycleCounter++;
        executed++;

        int ramIdx = ((x & 7) << 4) | (y & 0xF);

        switch (opcode) {
            case 0x00: // mnea
                status = (ram[ramIdx] != a) ? 1 : 0;
                break;
            case 0x01: // alem
                status = (a <= ram[ramIdx]) ? 1 : 0;
                break;
            case 0x02: // ynea
                status = sl = (y != a) ? 1 : 0;
                break;
            case 0x03: { // xma
                uint8 temp = ram[ramIdx];
                ram[ramIdx] = a;
                a = temp;
                break;
            }
            case 0x04: // dyn
                status = (y >= 1) ? 1 : 0;
                y = (y - 1) & 0xF;
                break;
            case 0x05: // iyc
                status = (y == 15) ? 1 : 0;
                y = (y + 1) & 0xF;
                break;
            case 0x06: // amaac
                a = a + ram[ramIdx];
                status = (a > 15) ? 1 : 0;
                a = a & 0xF;
                break;
            case 0x07: // dman
                status = (ram[ramIdx] >= 1) ? 1 : 0;
                a = (ram[ramIdx] - 1) & 0xF;
                break;
            case 0x08: // tka
                a = onReadK() & 0xF;
                break;
            case 0x09: // comx
                x = x ^ 4;
                break;
            case 0x0A: // tdo
                o = ((sl != 0) ? 0x10 : 0) | (a & 0xF);
                onWriteO(o);
                break;
            case 0x0B: // comc
                cb = cb ^ 1;
                break;
            case 0x0C: // rstr
                if (x <= 3 && y <= 15) {
                    r[y] = 0;
                    onWriteR(y, 0);
                }
                break;
            case 0x0D: // setr
                if (x <= 3 && y <= 15) {
                    r[y] = 1;
                    onWriteR(y, 1);
                }
                break;
            case 0x0E: // knez
                status = (onReadK() != 0) ? 1 : 0;
                break;
            case 0x0F: // retn
                retnCommand();
                break;

            // 0x10 - 0x1F: ldp constant
            case 0x10: pb = 0x0; break;
            case 0x11: pb = 0x8; break;
            case 0x12: pb = 0x4; break;
            case 0x13: pb = 0xC; break;
            case 0x14: pb = 0x2; break;
            case 0x15: pb = 0xA; break;
            case 0x16: pb = 0x6; break;
            case 0x17: pb = 0xE; break;
            case 0x18: pb = 0x1; break;
            case 0x19: pb = 0x9; break;
            case 0x1A: pb = 0x5; break;
            case 0x1B: pb = 0xD; break;
            case 0x1C: pb = 0x3; break;
            case 0x1D: pb = 0xB; break;
            case 0x1E: pb = 0x7; break;
            case 0x1F: pb = 0xF; break;

            case 0x20: // tay
                y = a & 0xF;
                break;
            case 0x21: // tma
                a = ram[ramIdx] & 0xF;
                break;
            case 0x22: // tmy
                y = ram[ramIdx] & 0xF;
                break;
            case 0x23: // tya
                a = y & 0xF;
                break;
            case 0x24: // tamdyn
                ram[ramIdx] = a & 0xF;
                status = (y >= 1) ? 1 : 0;
                y = (y - 1) & 0xF;
                break;
            case 0x25: // tamiyc
                ram[ramIdx] = a & 0xF;
                status = (y == 15) ? 1 : 0;
                y = (y + 1) & 0xF;
                break;
            case 0x26: // tamza
                ram[ramIdx] = a & 0xF;
                a = 0;
                break;
            case 0x27: // tam
                ram[ramIdx] = a & 0xF;
                break;

            // 0x28 - 0x2F: ldx
            case 0x28: x = 0; break;
            case 0x29: x = 4; break;
            case 0x2A: x = 2; break;
            case 0x2B: x = 6; break;
            case 0x2C: x = 1; break;
            case 0x2D: x = 5; break;
            case 0x2E: x = 3; break;
            case 0x2F: x = 7; break;

            // 0x30 - 0x33: sbit
            case 0x30: ram[ramIdx] |= (1 << 0); break;
            case 0x31: ram[ramIdx] |= (1 << 2); break;
            case 0x32: ram[ramIdx] |= (1 << 1); break;
            case 0x33: ram[ramIdx] |= (1 << 3); break;

            // 0x34 - 0x37: rbit
            case 0x34: ram[ramIdx] &= ~(1 << 0); break;
            case 0x35: ram[ramIdx] &= ~(1 << 2); break;
            case 0x36: ram[ramIdx] &= ~(1 << 1); break;
            case 0x37: ram[ramIdx] &= ~(1 << 3); break;

            // 0x38 - 0x3B: tbit1
            case 0x38: status = (ram[ramIdx] & (1 << 0)) ? 1 : 0; break;
            case 0x39: status = (ram[ramIdx] & (1 << 2)) ? 1 : 0; break;
            case 0x3A: status = (ram[ramIdx] & (1 << 1)) ? 1 : 0; break;
            case 0x3B: status = (ram[ramIdx] & (1 << 3)) ? 1 : 0; break;

            case 0x3C: // saman
                status = (a <= ram[ramIdx]) ? 1 : 0;
                a = ((a ^ 0xF) + 1 + ram[ramIdx]) & 0xF;
                break;
            case 0x3D: // cpaiz
                status = (a == 0) ? 1 : 0;
                a = ((a ^ 0xF) + 1) & 0xF;
                break;
            case 0x3E: // imac
                status = (ram[ramIdx] == 15) ? 1 : 0;
                a = (ram[ramIdx] + 1) & 0xF;
                break;
            case 0x3F: // mnez
                status = (ram[ramIdx] != 0) ? 1 : 0;
                break;

            // 0x40 - 0x4F: tcy
            case 0x40: y = 0x0; break;
            case 0x41: y = 0x8; break;
            case 0x42: y = 0x4; break;
            case 0x43: y = 0xC; break;
            case 0x44: y = 0x2; break;
            case 0x45: y = 0xA; break;
            case 0x46: y = 0x6; break;
            case 0x47: y = 0xE; break;
            case 0x48: y = 0x1; break;
            case 0x49: y = 0x9; break;
            case 0x4A: y = 0x5; break;
            case 0x4B: y = 0xD; break;
            case 0x4C: y = 0x3; break;
            case 0x4D: y = 0xB; break;
            case 0x4E: y = 0x7; break;
            case 0x4F: y = 0xF; break;

            // 0x50 - 0x5F: ynec
            case 0x50: status = (y != 0x0) ? 1 : 0; break;
            case 0x51: status = (y != 0x8) ? 1 : 0; break;
            case 0x52: status = (y != 0x4) ? 1 : 0; break;
            case 0x53: status = (y != 0xC) ? 1 : 0; break;
            case 0x54: status = (y != 0x2) ? 1 : 0; break;
            case 0x55: status = (y != 0xA) ? 1 : 0; break;
            case 0x56: status = (y != 0x6) ? 1 : 0; break;
            case 0x57: status = (y != 0xE) ? 1 : 0; break;
            case 0x58: status = (y != 0x1) ? 1 : 0; break;
            case 0x59: status = (y != 0x9) ? 1 : 0; break;
            case 0x5A: status = (y != 0x5) ? 1 : 0; break;
            case 0x5B: status = (y != 0xD) ? 1 : 0; break;
            case 0x5C: status = (y != 0x3) ? 1 : 0; break;
            case 0x5D: status = (y != 0xB) ? 1 : 0; break;
            case 0x5E: status = (y != 0x7) ? 1 : 0; break;
            case 0x5F: status = (y != 0xF) ? 1 : 0; break;

            // 0x60 - 0x6F: tcmiy
            case 0x60: ram[ramIdx] = 0x0; y = (y + 1) & 0xF; break;
            case 0x61: ram[ramIdx] = 0x8; y = (y + 1) & 0xF; break;
            case 0x62: ram[ramIdx] = 0x4; y = (y + 1) & 0xF; break;
            case 0x63: ram[ramIdx] = 0xC; y = (y + 1) & 0xF; break;
            case 0x64: ram[ramIdx] = 0x2; y = (y + 1) & 0xF; break;
            case 0x65: ram[ramIdx] = 0xA; y = (y + 1) & 0xF; break;
            case 0x66: ram[ramIdx] = 0x6; y = (y + 1) & 0xF; break;
            case 0x67: ram[ramIdx] = 0xE; y = (y + 1) & 0xF; break;
            case 0x68: ram[ramIdx] = 0x1; y = (y + 1) & 0xF; break;
            case 0x69: ram[ramIdx] = 0x9; y = (y + 1) & 0xF; break;
            case 0x6A: ram[ramIdx] = 0x5; y = (y + 1) & 0xF; break;
            case 0x6B: ram[ramIdx] = 0xD; y = (y + 1) & 0xF; break;
            case 0x6C: ram[ramIdx] = 0x3; y = (y + 1) & 0xF; break;
            case 0x6D: ram[ramIdx] = 0xB; y = (y + 1) & 0xF; break;
            case 0x6E: ram[ramIdx] = 0x7; y = (y + 1) & 0xF; break;
            case 0x6F: ram[ramIdx] = 0xF; y = (y + 1) & 0xF; break;

            // 0x70 - 0x7E: Arithmetic immediate
            case 0x70: a += 1; status = (a > 15) ? 1 : 0; a &= 0xF; break;
            case 0x71: a += 9; status = (a > 15) ? 1 : 0; a &= 0xF; break;
            case 0x72: a += 5; status = (a > 15) ? 1 : 0; a &= 0xF; break;
            case 0x73: a += 13; status = (a > 15) ? 1 : 0; a &= 0xF; break;
            case 0x74: a += 3; status = (a > 15) ? 1 : 0; a &= 0xF; break;
            case 0x75: a += 11; status = (a > 15) ? 1 : 0; a &= 0xF; break;
            case 0x76: a += 7; status = (a > 15) ? 1 : 0; a &= 0xF; break;
            case 0x77: a += 15; status = (a > 15) ? 1 : 0; a &= 0xF; break;
            case 0x78: a += 2; status = (a > 15) ? 1 : 0; a &= 0xF; break;
            case 0x79: a += 10; status = (a > 15) ? 1 : 0; a &= 0xF; break;
            case 0x7A: a += 6; status = (a > 15) ? 1 : 0; a &= 0xF; break;
            case 0x7B: a += 14; status = (a > 15) ? 1 : 0; a &= 0xF; break;
            case 0x7C: a += 4; status = (a > 15) ? 1 : 0; a &= 0xF; break;
            case 0x7D: a += 12; status = (a > 15) ? 1 : 0; a &= 0xF; break;
            case 0x7E: a += 8; status = (a > 15) ? 1 : 0; a &= 0xF; break;
            case 0x7F: a = 0; break;

            default:
                if ((opcode & 0xC0) == 0x80) { // 0x80 - 0xBF: BR
                    if (lastStatus) {
                        brCommand(opcode & 0x3F);
                    }
                } else if ((opcode & 0xC0) == 0xC0) { // 0xC0 - 0xFF: CALL
                    if (lastStatus) {
                        callCommand(opcode & 0x3F);
                    }
                }
                break;
        }

        if (--frameCounter <= 0) {
            onSyncFrame();
            frameCounter = (500000 / 6) / 30;
        }
    }

    return executed;
}
