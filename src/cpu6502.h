#pragma once

#include "types.h"
#include <functional>

class CPU6502 {
public:
    enum Flag : uint8 {
        FLAG_C = 0x01, // Carry
        FLAG_Z = 0x02, // Zero
        FLAG_I = 0x04, // Interrupt Disable
        FLAG_D = 0x08, // Decimal Mode
        FLAG_B = 0x10, // Break Command
        FLAG_U = 0x20, // Unused (always pushed as 1)
        FLAG_V = 0x40, // Overflow
        FLAG_N = 0x80  // Negative
    };

    // Registers
    uint8  A = 0;       // Accumulator
    uint8  X = 0;       // Index X
    uint8  Y = 0;       // Index Y
    uint8  SP = 0xFD;   // Stack pointer
    uint16 PC = 0;      // Program counter
    uint8  P = 0x34;    // Status register

    uint64 cycles = 0;
    bool is65C02 = false; // Set to true for BBC Master 128
    bool nmiPending = false;
    bool irqPending = false;
    bool jam = false;

    // Memory callbacks
    std::function<uint8(uint16)> readMem;
    std::function<void(uint16, uint8)> writeMem;

    CPU6502();

    void reset();
    void triggerNMI();
    void triggerIRQ();
    void clearIRQ();

    // Executes one instruction and returns cycles consumed
    int step();

    inline void setFlag(Flag f, bool v) {
        if (v) P |= f;
        else P &= ~f;
    }

    inline bool getFlag(Flag f) const {
        return (P & f) != 0;
    }

private:
    uint8  read(uint16 addr);
    void   write(uint16 addr, uint8 val);
    uint16 read16(uint16 addr);
    uint16 read16_wrap(uint16 addr); // 6502 page wrapping bug

    void push(uint8 val);
    uint8 pull();
    void push16(uint16 val);
    uint16 pull16();

    inline void setZN(uint8 val) {
        setFlag(FLAG_Z, val == 0);
        setFlag(FLAG_N, (val & 0x80) != 0);
    }

    void adc(uint8 val);
    void sbc(uint8 val);
    void cmp(uint8 reg, uint8 val);
};
