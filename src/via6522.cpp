#include "via6522.h"

VIA6522::VIA6522(const std::string& n) : name(n) {
    reset();
}

void VIA6522::reset() {
    ora = 0;
    orb = 0;
    ira = 0xFF;
    irb = 0xFF;
    ddra = 0;
    ddrb = 0;
    t1c = 0xFFFF;
    t1l = 0xFFFF;
    t1Active = false;
    t1Pb7 = false;
    t2c = 0xFFFF;
    t2l = 0xFFFF;
    t2Active = false;
    sr = 0;
    acr = 0;
    pcr = 0;
    ifr = 0;
    ier = 0x80;
    ca1 = false;
    ca2 = false;
    cb1 = false;
    cb2 = false;
    updateIFR();
}

void VIA6522::updateIFR() {
    bool irq = (ifr & ier & 0x7F) != 0;
    if (irq) {
        ifr |= 0x80;
    } else {
        ifr &= ~0x80;
    }
    if (onIRQ) onIRQ(irq);
}

void VIA6522::step(int cycles) {
    // --- Timer 1 ---
    if (t1Active) {
        if (cycles >= t1c) {
            ifr |= 0x40; // T1 interrupt flag
            if (acr & 0x40) {
                // Continuous reload mode
                int rem = cycles - t1c;
                t1c = t1l - (rem % (t1l + 1));
            } else {
                // One-shot mode
                t1c = 0xFFFF;
                t1Active = false;
            }
            if (acr & 0x80) {
                t1Pb7 = !t1Pb7;
            }
            updateIFR();
        } else {
            t1c -= cycles;
        }
    }

    // --- Timer 2 ---
    if (t2Active && !(acr & 0x20)) { // Count down on clock cycles
        if (cycles >= t2c) {
            ifr |= 0x20; // T2 interrupt flag
            t2c = 0xFFFF;
            t2Active = false;
            updateIFR();
        } else {
            t2c -= cycles;
        }
    }
}

uint8 VIA6522::read(uint8 reg) {
    switch (reg & 0x0F) {
        case 0x00: { // Port B
            ifr &= ~0x18; // Clear CB1, CB2 flags
            updateIFR();
            uint8 in = onPortBRead ? onPortBRead() : irb;
            return (orb & ddrb) | (in & ~ddrb);
        }
        case 0x01: { // Port A (with handshake)
            ifr &= ~0x03; // Clear CA1, CA2 flags
            updateIFR();
            uint8 in = onPortARead ? onPortARead() : ira;
            return (ora & ddra) | (in & ~ddra);
        }
        case 0x02: return ddrb;
        case 0x03: return ddra;
        case 0x04: // T1CL
            ifr &= ~0x40; // Clear T1 interrupt flag
            updateIFR();
            return (uint8)(t1c & 0xFF);
        case 0x05: // T1CH
            return (uint8)(t1c >> 8);
        case 0x06: return (uint8)(t1l & 0xFF);
        case 0x07: return (uint8)(t1l >> 8);
        case 0x08: // T2CL
            ifr &= ~0x20; // Clear T2 interrupt flag
            updateIFR();
            return (uint8)(t2c & 0xFF);
        case 0x09: return (uint8)(t2c >> 8);
        case 0x0A: return sr;
        case 0x0B: return acr;
        case 0x0C: return pcr;
        case 0x0D: return ifr;
        case 0x0E: return ier | 0x80;
        case 0x0F: { // Port A (without handshake)
            uint8 in = onPortARead ? onPortARead() : ira;
            return (ora & ddra) | (in & ~ddra);
        }
    }
    return 0xFF;
}

void VIA6522::write(uint8 reg, uint8 val) {
    switch (reg & 0x0F) {
        case 0x00: { // Port B
            orb = val;
            ifr &= ~0x18; // Clear CB1, CB2
            updateIFR();
            if (onPortBWrite) onPortBWrite(val);
            break;
        }
        case 0x01: { // Port A (with handshake)
            ora = val;
            ifr &= ~0x03; // Clear CA1, CA2
            updateIFR();
            if (onPortAWrite) onPortAWrite(val);
            break;
        }
        case 0x02: ddrb = val; break;
        case 0x03: ddra = val; break;
        case 0x04: // T1 Low Latch
            t1l = (t1l & 0xFF00) | val;
            break;
        case 0x05: // T1 High Counter (starts timer!)
            t1l = (t1l & 0x00FF) | ((uint16)val << 8);
            t1c = t1l;
            t1Active = true;
            ifr &= ~0x40;
            updateIFR();
            break;
        case 0x06: // T1 Low Latch
            t1l = (t1l & 0xFF00) | val;
            break;
        case 0x07: // T1 High Latch
            t1l = (t1l & 0x00FF) | ((uint16)val << 8);
            ifr &= ~0x40;
            updateIFR();
            break;
        case 0x08: // T2 Low Latch
            t2l = (t2l & 0xFF00) | val;
            break;
        case 0x09: // T2 High Counter
            t2l = (t2l & 0x00FF) | ((uint16)val << 8);
            t2c = t2l;
            t2Active = true;
            ifr &= ~0x20;
            updateIFR();
            break;
        case 0x0A: sr = val; break;
        case 0x0B: acr = val; break;
        case 0x0C: pcr = val; break;
        case 0x0D: // IFR (Writing 1s clears flags)
            ifr &= ~(val & 0x7F);
            updateIFR();
            break;
        case 0x0E: // IER
            if (val & 0x80) {
                ier |= (val & 0x7F);
            } else {
                ier &= ~(val & 0x7F);
            }
            updateIFR();
            break;
        case 0x0F: // Port A (without handshake)
            ora = val;
            if (onPortAWrite) onPortAWrite(val);
            break;
    }
}

void VIA6522::setCA1(bool val) {
    if (val != ca1) {
        ca1 = val;
        bool activeEdge = (pcr & 0x01) ? val : !val;
        if (activeEdge) {
            ifr |= 0x02; // CA1 flag
            updateIFR();
        }
    }
}

void VIA6522::setCA2(bool val) {
    if (val != ca2) {
        ca2 = val;
        bool activeEdge = (pcr & 0x04) ? val : !val;
        if (activeEdge) {
            ifr |= 0x01; // CA2 flag
            updateIFR();
        }
    }
}

void VIA6522::setCB1(bool val) {
    if (val != cb1) {
        cb1 = val;
        bool activeEdge = (pcr & 0x10) ? val : !val;
        if (activeEdge) {
            ifr |= 0x10; // CB1 flag
            updateIFR();
        }
    }
}

void VIA6522::setCB2(bool val) {
    cb2 = val;
}
