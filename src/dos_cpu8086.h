#pragma once

#include "types.h"
#include <functional>
#include <string>
#include <vector>

class CPU8086 {
public:
    // General Purpose Registers (16-bit)
    union {
        struct { uint8 AL, AH; };
        uint16 AX = 0;
    };
    union {
        struct { uint8 BL, BH; };
        uint16 BX = 0;
    };
    union {
        struct { uint8 CL, CH; };
        uint16 CX = 0;
    };
    union {
        struct { uint8 DL, DH; };
        uint16 DX = 0;
    };

    // Pointer and Index Registers
    uint16 SP = 0xFFFE;
    uint16 BP = 0x0000;
    uint16 SI = 0x0000;
    uint16 DI = 0x0000;

    // Segment Registers
    uint16 CS = 0x1000;
    uint16 DS = 0x1000;
    uint16 SS = 0x1000;
    uint16 ES = 0x1000;

    // Instruction Pointer & Flags
    uint16 IP = 0x0100; // Standard COM start offset
    uint16 FLAGS = 0x0202; // IF=1, bit 1=1

    // Flag bits
    static constexpr uint16 FLAG_CF = 0x0001; // Carry
    static constexpr uint16 FLAG_PF = 0x0004; // Parity
    static constexpr uint16 FLAG_AF = 0x0010; // Auxiliary carry
    static constexpr uint16 FLAG_ZF = 0x0040; // Zero
    static constexpr uint16 FLAG_SF = 0x0080; // Sign
    static constexpr uint16 FLAG_TF = 0x0100; // Trap
    static constexpr uint16 FLAG_IF = 0x0200; // Interrupt Enable
    static constexpr uint16 FLAG_DF = 0x0400; // Direction
    static constexpr uint16 FLAG_OF = 0x0800; // Overflow

    bool halted = false;
    uint64 totalCycles = 0;

    // Callbacks for bus access
    std::function<uint8(uint32)> readByte;
    std::function<void(uint32, uint8)> writeByte;
    std::function<uint8(uint16)> inPort;
    std::function<void(uint16, uint8)> outPort;
    std::function<bool(uint8)> interruptHandler; // Returns true if handled by HLE

    CPU8086();
    void reset();

    // Physical address calculation
    static inline uint32 getPhysAddr(uint16 seg, uint16 off) {
        return ((uint32)seg << 4) + off;
    }

    // Bus helpers
    uint8  memRead8(uint16 seg, uint16 off);
    uint16 memRead16(uint16 seg, uint16 off);
    void   memWrite8(uint16 seg, uint16 off, uint8 val);
    void   memWrite16(uint16 seg, uint16 off, uint16 val);

    void push16(uint16 val);
    uint16 pop16();

    // Execute single 8086 instruction, returns cycles taken
    int step();

    // Trigger hardware/software interrupt
    void triggerInterrupt(uint8 intNum);

    // Flag helpers
    void setFlag(uint16 mask, bool val) {
        if (val) FLAGS |= mask;
        else FLAGS &= ~mask;
    }
    bool getFlag(uint16 mask) const { return (FLAGS & mask) != 0; }

    void updateZSP8(uint8 val);
    void updateZSP16(uint16 val);
};
