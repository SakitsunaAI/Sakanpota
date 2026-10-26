#pragma once

#include "types.h"
#include <functional>
#include <array>
#include <string>

// Handcrafted ARM610 (ARMv3) 32-bit RISC CPU Interpreter
// Built specifically for Apple Newton MessagePad (OMP / MP100 / MP110 / MP120 / MP130)
class NewtonCPUARM {
public:
    NewtonCPUARM();
    ~NewtonCPUARM() = default;

    void reset();
    int step();
    int execute(int cycles);

    // 16 General-Purpose 32-bit Registers in current mode
    uint32 r[16];

    // Current Program Status Register (CPSR) and Saved Status Register (SPSR)
    uint32 cpsr;
    uint32 spsr;

    // CPU Modes
    enum Mode {
        MODE_USER = 0x10,
        MODE_FIQ  = 0x11,
        MODE_IRQ  = 0x12,
        MODE_SVC  = 0x13,
        MODE_ABT  = 0x17,
        MODE_UND  = 0x1B,
        MODE_SYS  = 0x1F
    };

    // Banked Registers
    uint32 r_user[7]; // R8..R14 for user/system
    uint32 r_fiq[7];  // R8..R14 for FIQ
    uint32 r_svc[2];  // R13, R14 for SVC
    uint32 r_abt[2];  // R13, R14 for ABT
    uint32 r_irq[2];  // R13, R14 for IRQ
    uint32 r_und[2];  // R13, R14 for UND

    uint32 spsr_fiq;
    uint32 spsr_svc;
    uint32 spsr_abt;
    uint32 spsr_irq;
    uint32 spsr_und;

    // CPSR Flags
    static constexpr uint32 FLAG_N = 0x80000000;
    static constexpr uint32 FLAG_Z = 0x40000000;
    static constexpr uint32 FLAG_C = 0x20000000;
    static constexpr uint32 FLAG_V = 0x10000000;
    static constexpr uint32 FLAG_I = 0x00000080;
    static constexpr uint32 FLAG_F = 0x00000040;

    void setMode(uint8 newMode);
    uint8 getMode() const { return cpsr & 0x1F; }

    // Coprocessor 15 (System Control & MMU) Registers
    struct CP15 {
        uint32 id;                  // CR0: 0x41560610 (ARM610)
        uint32 control;             // CR1: MMU, Cache, Alignment
        uint32 ttb;                 // CR2: Translation Table Base
        uint32 domainAccess;        // CR3: Domain Access Control
        uint32 faultStatus;         // CR5: FSR
        uint32 faultAddress;        // CR6: FAR
    } cp15;

    inline bool isMMUEnabled() const { return (cp15.control & 1) != 0; }

    bool halted;
    uint64 totalCycles;

    // Memory Access Callbacks (Physical & Virtual)
    std::function<uint8(uint32)>  read8;
    std::function<uint16(uint32)> read16;
    std::function<uint32(uint32)> read32;
    std::function<void(uint32, uint8)>  write8;
    std::function<void(uint32, uint16)> write16;
    std::function<void(uint32, uint32)> write32;

    // MMU Address Translation
    uint32 translateAddress(uint32 vaddr, bool isWrite, bool& fault);

    // Interrupts
    void triggerIRQ();
    void triggerFIQ();

    // Helpers
    bool checkCondition(uint8 cond) const;

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

private:
    uint32 decodeShift(uint32 op, bool& carryOut);
    void handleMCR(uint32 op);
    void handleMRC(uint32 op);
};
