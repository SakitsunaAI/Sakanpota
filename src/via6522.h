#pragma once

#include "types.h"
#include <functional>

class VIA6522 {
public:
    std::string name;

    // Registers
    uint8 ora = 0;
    uint8 orb = 0;
    uint8 ira = 0;
    uint8 irb = 0;
    uint8 ddra = 0;
    uint8 ddrb = 0;

    // Timers
    uint16 t1c = 0xFFFF;
    uint16 t1l = 0xFFFF;
    bool   t1Active = false;
    bool   t1Pb7 = false;

    uint16 t2c = 0xFFFF;
    uint16 t2l = 0xFFFF;
    bool   t2Active = false;

    uint8 sr = 0;
    uint8 acr = 0;
    uint8 pcr = 0;
    uint8 ifr = 0;
    uint8 ier = 0;

    // External line states
    bool ca1 = false;
    bool ca2 = false;
    bool cb1 = false;
    bool cb2 = false;

    // Callbacks
    std::function<void(bool)> onIRQ;
    std::function<void(uint8)> onPortAWrite;
    std::function<void(uint8)> onPortBWrite;
    std::function<uint8()> onPortARead;
    std::function<uint8()> onPortBRead;

    VIA6522(const std::string& n = "VIA");

    void reset();
    void step(int cycles);

    uint8 read(uint8 reg);
    void  write(uint8 reg, uint8 val);

    void setCA1(bool val);
    void setCA2(bool val);
    void setCB1(bool val);
    void setCB2(bool val);

    bool isIRQPending() const {
        return (ifr & 0x80) != 0;
    }

    void updateIFR();
};
