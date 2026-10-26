#pragma once
#include "types.h"

// Texas Instruments TMS1100 4-bit Microcontroller Core
// Pure interpreter handcrafted for Milton Bradley Microvision (1979)
class CPUTMS1100 {
public:
    CPUTMS1100();
    virtual ~CPUTMS1100() = default;

    void reset();
    int execute(int cycles);
    void setROM(const uint8* romData, uint16 checksum);

    // CPU Registers
    uint8 a;          // 4-bit accumulator (0..15)
    uint8 x;          // 3-bit X register (RAM row pointer 0..7)
    uint8 y;          // 4-bit Y register (RAM column pointer 0..15)
    uint8 status;     // 1-bit status / carry flag
    uint8 lastStatus; // 1-bit previous status (branches test this)
    uint8 pa, pb;     // 4-bit Page address and Page buffer (0..15)
    uint8 pc;         // 6-bit Program counter (0..63)
    uint8 ca, cb, cs; // 1-bit Chapter address, Chapter buffer, Chapter save
    uint8 sr;         // 6-bit Subroutine return address
    uint8 cl;         // Call latch (1 when in subroutine)
    uint8 sl;         // Status latch
    uint8 o;          // 5-bit O output latch
    uint8 r[16];      // R output latches (R0..R15)
    uint8 ram[128];   // 128 nibbles (8 rows x 16 columns)
    const uint8* rom; // 2048-byte linear program ROM
    uint16 romChecksum;
    int cycleCounter;
    int frameCounter;

    inline uint16 getPCTR() const {
        return ((ca & 1) << 10) | ((pa & 0xF) << 6) | (pc & 0x3F);
    }

    // Hardware callbacks implemented by Microvision machine
    virtual void onWriteO(uint8 data) {}
    virtual void onWriteR(int line, int level) {}
    virtual uint8 onReadK() { return 0; }
    virtual void onSyncFrame() {}

private:
    void brCommand(int address);
    void callCommand(int address);
    void retnCommand();
};
