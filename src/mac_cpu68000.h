#pragma once

#include "types.h"
#include <functional>
#include <string>

class CPU68000 {
public:
    // Data Registers D0-D7 (32-bit)
    uint32 D[8]{};

    // Address Registers A0-A7 (32-bit, A7 is SP)
    uint32 A[8]{};

    // Program Counter (32-bit, lower 24-bit on 68000)
    uint32 PC = 0x000000;

    // Status Register SR (16-bit)
    uint16 SR = 0x2700; // Supervisor mode, Interrupt level 7

    // Condition Codes (lower byte of SR)
    static constexpr uint16 CCR_C = 0x01; // Carry
    static constexpr uint16 CCR_V = 0x02; // Overflow
    static constexpr uint16 CCR_Z = 0x04; // Zero
    static constexpr uint16 CCR_N = 0x08; // Negative
    static constexpr uint16 CCR_X = 0x10; // Extend

    bool halted = false;
    uint64 totalCycles = 0;

    // Bus callbacks (24-bit address space on 68000)
    std::function<uint8(uint32)>  read8;
    std::function<uint16(uint32)> read16;
    std::function<uint32(uint32)> read32;
    std::function<void(uint32, uint8)>  write8;
    std::function<void(uint32, uint16)> write16;
    std::function<void(uint32, uint32)> write32;

    CPU68000();
    void reset();

    int step();

    // CCR helpers
    void setCCR(uint16 mask, bool val) {
        if (val) SR |= mask;
        else SR &= ~mask;
    }
    bool getCCR(uint16 mask) const { return (SR & mask) != 0; }
};
