#pragma once

#include "types.h"
#include <functional>

class CPUZ80 {
public:
    // Z80 16-bit Register Pairs / 8-bit Registers
    union RegPair {
        uint16 w;
        struct {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            uint8 l;
            uint8 h;
#else
            uint8 h;
            uint8 l;
#endif
        } b;
    };

    RegPair AF, BC, DE, HL;
    RegPair AF_, BC_, DE_, HL_; // Shadow register set
    RegPair IX, IY;
    uint16 SP = 0;
    uint16 PC = 0;
    uint8 I = 0;
    uint8 R = 0;

    // Flags (F register): S Z Y H X P/V N C
    enum Flag {
        CARRY     = 0x01,
        SUBTRACT  = 0x02,
        PARITY        = 0x04,
        FLAG_OVERFLOW = 0x04,
        F3            = 0x08,
        HALF      = 0x10,
        F5        = 0x20,
        ZERO      = 0x40,
        SIGN      = 0x80
    };

    bool iff1 = false;
    bool iff2 = false;
    uint8 im = 1; // Interrupt Mode (0, 1, 2)
    bool halted = false;

    // Bus callbacks
    std::function<uint8(uint16)> memRead;
    std::function<void(uint16, uint8)> memWrite;
    std::function<uint8(uint16)> ioRead;
    std::function<void(uint16, uint8)> ioWrite;

    CPUZ80();
    void reset();
    int step();
    void requestInterrupt(uint8 vector = 0xFF);
    void requestNMI();

private:
    uint8 fetchByte();
    uint16 fetchWord();

    uint8 readByte(uint16 addr);
    void writeByte(uint16 addr, uint8 val);
    uint16 readWord(uint16 addr);
    void writeWord(uint16 addr, uint16 val);
    uint8 inByte(uint16 port);
    void outByte(uint16 port, uint8 val);

    // ALU helper methods
    void setFlag(uint8 mask, bool condition);
    bool getFlag(uint8 mask) const;
    void aluAdd(uint8 val, bool withCarry);
    void aluSub(uint8 val, bool withCarry);
    void aluAnd(uint8 val);
    void aluXor(uint8 val);
    void aluOr(uint8 val);
    void aluCp(uint8 val);
    uint8 aluInc(uint8 val);
    uint8 aluDec(uint8 val);
    void aluDaa();

    // Rotate and shift helpers
    uint8 rlc(uint8 val);
    uint8 rrc(uint8 val);
    uint8 rl(uint8 val);
    uint8 rr(uint8 val);
    uint8 sla(uint8 val);
    uint8 sra(uint8 val);
    uint8 sll(uint8 val);
    uint8 srl(uint8 val);

    // Prefix instruction handlers
    int executeCB();
    int executeED();
    int executeIndex(RegPair& idx, uint8 prefix);
    int executeIndexCB(uint16 addr);
};
