#pragma once

#include "types.h"
#include <functional>

// Handcrafted ARM7TDMI 32-bit RISC & 16-bit Thumb Instruction Set Interpreter
// Pure cycle/instruction interpreter written by hand for Sony PocketStation (1999)
class CPUARM7 {
public:
    CPUARM7();
    virtual ~CPUARM7() = default;

    void reset();
    int step();
    int execute(int cycles);

    // 16 General Purpose Registers (R0 - R15)
    // R13: Stack Pointer (SP)
    // R14: Link Register (LR)
    // R15: Program Counter (PC)
    uint32 r[16];

    // Current Program Status Register (CPSR)
    uint32 cpsr;

    // CPSR Flag Bits
    static constexpr uint32 FLAG_N = 0x80000000; // Negative
    static constexpr uint32 FLAG_Z = 0x40000000; // Zero
    static constexpr uint32 FLAG_C = 0x20000000; // Carry
    static constexpr uint32 FLAG_V = 0x10000000; // Overflow
    static constexpr uint32 FLAG_I = 0x00000080; // IRQ disable
    static constexpr uint32 FLAG_F = 0x00000040; // FIQ disable
    static constexpr uint32 FLAG_T = 0x00000020; // State (1 = Thumb 16-bit, 0 = ARM 32-bit)

    inline bool isThumb() const { return (cpsr & FLAG_T) != 0; }
    inline void setThumb(bool t) {
        if (t) cpsr |= FLAG_T;
        else   cpsr &= ~FLAG_T;
    }

    bool halted;
    uint64 totalCycles;

    // Bus memory callbacks
    std::function<uint8(uint32)>  read8;
    std::function<uint16(uint32)> read16;
    std::function<uint32(uint32)> read32;
    std::function<void(uint32, uint8)>  write8;
    std::function<void(uint32, uint16)> write16;
    std::function<void(uint32, uint32)> write32;

    // Condition evaluation (ARM & Thumb conditional branches)
    bool checkCondition(uint8 cond) const;

    // Flag calculation helpers
    inline void setNZ(uint32 val) {
        cpsr &= ~(FLAG_N | FLAG_Z);
        if (val & 0x80000000) cpsr |= FLAG_N;
        if (val == 0) cpsr |= FLAG_Z;
    }

    inline void setNZC(uint32 val, bool carry) {
        cpsr &= ~(FLAG_N | FLAG_Z | FLAG_C);
        if (val & 0x80000000) cpsr |= FLAG_N;
        if (val == 0) cpsr |= FLAG_Z;
        if (carry) cpsr |= FLAG_C;
    }

    inline void setNZCV(uint32 val, bool carry, bool overflow) {
        cpsr &= ~(FLAG_N | FLAG_Z | FLAG_C | FLAG_V);
        if (val & 0x80000000) cpsr |= FLAG_N;
        if (val == 0) cpsr |= FLAG_Z;
        if (carry) cpsr |= FLAG_C;
        if (overflow) cpsr |= FLAG_V;
    }

    int stepThumb();
    int stepARM();
};
