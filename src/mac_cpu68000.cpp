#include "mac_cpu68000.h"

CPU68000::CPU68000() {
    reset();
}

void CPU68000::reset() {
    for (int i = 0; i < 8; i++) {
        D[i] = 0;
        A[i] = 0;
    }
    SR = 0x2700;
    halted = false;
    totalCycles = 0;

    // Standard 68000 Reset Vector:
    // Long word 0 (0x000000) -> Initial SSP (A7)
    // Long word 1 (0x000004) -> Initial PC
    if (read32) {
        A[7] = read32(0x000000);
        PC   = read32(0x000004);
    } else {
        A[7] = 0x020000; // Top of 128KB RAM
        PC   = 0x400000; // Macintosh ROM entry point
    }
}

int CPU68000::step() {
    if (halted || !read16) return 4;

    uint16 op = read16(PC);
    PC += 2;
    int cycles = 4;

    // NOP
    if (op == 0x4E71) {
        return 4;
    }

    // RTS
    if (op == 0x4E75) {
        if (read32) {
            PC = read32(A[7]);
            A[7] += 4;
        }
        return 16;
    }

    // MOVEQ #data, Dn (0111 rrr 0 dddddddd)
    if ((op & 0xF100) == 0x7000) {
        uint8 reg = (op >> 9) & 7;
        int32 data = (int8)(op & 0xFF);
        D[reg] = (uint32)data;
        setCCR(CCR_N, data < 0);
        setCCR(CCR_Z, data == 0);
        setCCR(CCR_V, false);
        setCCR(CCR_C, false);
        return 4;
    }

    // BRA.s / BSR.s / Bcc.s
    if ((op & 0xF000) == 0x6000) {
        uint8 cond = (op >> 8) & 0x0F;
        int32 disp = (int8)(op & 0xFF);
        if (disp == 0) {
            disp = (int16)read16(PC);
            PC += 2;
        }

        bool take = false;
        if (cond == 0) take = true; // BRA
        else if (cond == 1) { // BSR
            A[7] -= 4;
            if (write32) write32(A[7], PC);
            take = true;
        } else if (cond == 4) take = !getCCR(CCR_C); // BCC
        else if (cond == 5) take = getCCR(CCR_C);  // BCS
        else if (cond == 6) take = !getCCR(CCR_Z); // BNE
        else if (cond == 7) take = getCCR(CCR_Z);  // BEQ
        else if (cond == 12) take = getCCR(CCR_N) == getCCR(CCR_V); // BGE
        else if (cond == 13) take = getCCR(CCR_N) != getCCR(CCR_V); // BLT
        else take = true;

        if (take) {
            PC += disp;
        }
        return 10;
    }

    // JMP / JSR (0100 1110 11 xxx xxx)
    if ((op & 0xFFC0) == 0x4EC0) { // JMP
        uint8 mode = (op >> 3) & 7;
        uint8 reg = op & 7;
        if (mode == 2) PC = A[reg]; // (An)
        else if (mode == 7 && reg == 1) { // (xxx).L
            PC = read32(PC);
        }
        return 8;
    }
    if ((op & 0xFFC0) == 0x4E80) { // JSR
        uint8 mode = (op >> 3) & 7;
        uint8 reg = op & 7;
        uint32 target = PC;
        if (mode == 2) target = A[reg];
        else if (mode == 7 && reg == 1) {
            target = read32(PC);
            PC += 4;
        }
        A[7] -= 4;
        if (write32) write32(A[7], PC);
        PC = target;
        return 16;
    }

    // CLR Dn
    if ((op & 0xFFF8) == 0x4280) {
        D[op & 7] = 0;
        setCCR(CCR_Z, true);
        setCCR(CCR_N, false);
        return 6;
    }

    // TST Dn
    if ((op & 0xFFF8) == 0x4A80) {
        uint32 val = D[op & 7];
        setCCR(CCR_Z, val == 0);
        setCCR(CCR_N, (val & 0x80000000) != 0);
        setCCR(CCR_V, false);
        setCCR(CCR_C, false);
        return 4;
    }

    totalCycles += cycles;
    return cycles;
}
