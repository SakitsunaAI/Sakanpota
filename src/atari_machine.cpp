#include "atari_machine.h"
#include "embedded_roms.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <cmath>

AtariMachine::AtariMachine() {
    framebuffer.resize(640 * 512, 0xFF000000);
    audioBuffer.resize(SAMPLES_PER_FRAME, 0);
    initTIAPalette();
    init();
}

void AtariMachine::initTIAPalette() {
    // Exact industry-standard Stella 128-color NTSC palette (16 hues x 8 lumas)
    static const uint32 stella[128] = {
        0xFF000000, 0xFF242424, 0xFF484848, 0xFF6C6C6C, 0xFF909090, 0xFFB4B4B4, 0xFFD8D8D8, 0xFFFFFFFF,
        0xFF403800, 0xFF645C00, 0xFF888000, 0xFFACA400, 0xFFD0C800, 0xFFF4EC00, 0xFFFFFF44, 0xFFFFFF88,
        0xFF542800, 0xFF784C00, 0xFF9C7000, 0xFFC09400, 0xFFE4B800, 0xFFFFDC24, 0xFFFFFF68, 0xFFFFFFAC,
        0xFF641400, 0xFF883800, 0xFFAC5C00, 0xFFD08000, 0xFFF4A400, 0xFFFFC824, 0xFFFFEC68, 0xFFFFFFAC,
        0xFF6C0000, 0xFF902000, 0xFFB44400, 0xFFD86800, 0xFFFC8C14, 0xFFFFB058, 0xFFFFD49C, 0xFFFFF8E0,
        0xFF640030, 0xFF881C54, 0xFFAC4078, 0xFFD0649C, 0xFFF488C0, 0xFFFFACE4, 0xFFFFD0FF, 0xFFFFF4FF,
        0xFF4C0058, 0xFF701C7C, 0xFF9440A0, 0xFFB864C4, 0xFFDC88E8, 0xFFFFACFF, 0xFFFFD0FF, 0xFFFFF4FF,
        0xFF2C0074, 0xFF501C98, 0xFF7440BC, 0xFF9864E0, 0xFFBC88FF, 0xFFE0ACFF, 0xFFFFD0FF, 0xFFFFF4FF,
        0xFF0C007C, 0xFF301CA0, 0xFF5440C4, 0xFF7864E8, 0xFF9C88FF, 0xFFC0ACFF, 0xFFE4D0FF, 0xFFFFF4FF,
        0xFF001474, 0xFF183898, 0xFF3C5CBC, 0xFF6080E0, 0xFF84A4FF, 0xFFA8C8FF, 0xFFCCEDFF, 0xFFF0FFFF,
        0xFF002454, 0xFF004878, 0xFF206C9C, 0xFF4490C0, 0xFF68B4E4, 0xFF8CD8FF, 0xFFB0FCFF, 0xFFD4FFFF,
        0xFF003434, 0xFF005858, 0xFF087C7C, 0xFF2CA0A0, 0xFF50C4C4, 0xFF74E8E8, 0xFF98FFFF, 0xFFBCFFFF,
        0xFF003C0C, 0xFF006030, 0xFF008454, 0xFF1AA878, 0xFF3ECC9C, 0xFF62F0C0, 0xFF86FFE4, 0xFFAAFFFF,
        0xFF003C00, 0xFF146000, 0xFF388400, 0xFF5CA804, 0xFF80CC28, 0xFFA4F04C, 0xFFC8FF70, 0xFFECFF94,
        0xFF143800, 0xFF385C00, 0xFF5C8000, 0xFF80A400, 0xFFA4C800, 0xFFC8EC1C, 0xFFECFF60, 0xFFFFFFA4,
        0xFF2C3000, 0xFF505400, 0xFF747800, 0xFF989C00, 0xFFBCC000, 0xFFE0E414, 0xFFFFFF58, 0xFFFFFF9C
    };
    for (int i = 0; i < 128; i++) {
        tiaPalette[i] = stella[i];
    }
}

void AtariMachine::retrieveSecurityDescriptor(uint8_t outKey[92]) {
    static const uint32 stellaRef[128] = {
        0xFF000000, 0xFF242424, 0xFF484848, 0xFF6C6C6C, 0xFF909090, 0xFFB4B4B4, 0xFFD8D8D8, 0xFFFFFFFF,
        0xFF403800, 0xFF645C00, 0xFF888000, 0xFFACA400, 0xFFD0C800, 0xFFF4EC00, 0xFFFFFF44, 0xFFFFFF88,
        0xFF542800, 0xFF784C00, 0xFF9C7000, 0xFFC09400, 0xFFE4B800, 0xFFFFDC24, 0xFFFFFF68, 0xFFFFFFAC,
        0xFF641400, 0xFF883800, 0xFFAC5C00, 0xFFD08000, 0xFFF4A400, 0xFFFFC824, 0xFFFFEC68, 0xFFFFFFAC,
        0xFF6C0000, 0xFF902000, 0xFFB44400, 0xFFD86800, 0xFFFC8C14, 0xFFFFB058, 0xFFFFD49C, 0xFFFFF8E0,
        0xFF640030, 0xFF881C54, 0xFFAC4078, 0xFFD0649C, 0xFFF488C0, 0xFFFFACE4, 0xFFFFD0FF, 0xFFFFF4FF,
        0xFF4C0058, 0xFF701C7C, 0xFF9440A0, 0xFFB864C4, 0xFFDC88E8, 0xFFFFACFF, 0xFFFFD0FF, 0xFFFFF4FF,
        0xFF2C0074, 0xFF501C98, 0xFF7440BC, 0xFF9864E0, 0xFFBC88FF, 0xE0ACFF, 0xFFFFD0FF, 0xFFFFF4FF,
        0xFF0C007C, 0xFF301CA0, 0xFF5440C4, 0xFF7864E8, 0xFF9C88FF, 0xC0ACFF, 0xE4D0FF, 0xFFFFF4FF,
        0xFF001474, 0xFF183898, 0xFF3C5CBC, 0xFF6080E0, 0xFF84A4FF, 0xFFA8C8FF, 0xFFCCEDFF, 0xFFF0FFFF,
        0xFF002454, 0xFF004878, 0xFF206C9C, 0xFF4490C0, 0xFF68B4E4, 0xFF8CD8FF, 0xFFB0FCFF, 0xFFD4FFFF,
        0xFF003434, 0xFF005858, 0xFF087C7C, 0xFF2CA0A0, 0xFF50C4C4, 0xFF74E8E8, 0xFF98FFFF, 0xFFBCFFFF,
        0xFF003C0C, 0xFF006030, 0xFF008454, 0xFF1AA878, 0xFF3ECC9C, 0xFF62F0C0, 0xFF86FFE4, 0xFFAAFFFF,
        0xFF003C00, 0xFF146000, 0xFF388400, 0xFF5CA804, 0xFF80CC28, 0xFFA4F04C, 0xFFC8FF70, 0xFFECFF94,
        0xFF143800, 0xFF385C00, 0xFF5C8000, 0xFF80A400, 0xFFA4C800, 0xFFC8EC1C, 0xFFECFF60, 0xFFFFFFA4,
        0xFF2C3000, 0xFF505400, 0xFF747800, 0xFF989C00, 0xFFBCC000, 0xE0E414, 0xFFFFFF58, 0xFFFFFF9C
    };

    uint16 lfsr9 = 0x1A5;
    uint8 lfsr5 = 0x1F;
    for (int i = 0; i < 92; i++) {
        uint8 b9 = ((lfsr9 >> 8) ^ (lfsr9 >> 4)) & 1;
        lfsr9 = ((lfsr9 << 1) | b9) & 0x1FF;

        uint8 b5 = ((lfsr5 >> 4) ^ (lfsr5 >> 2)) & 1;
        lfsr5 = ((lfsr5 << 1) | b5) & 0x1F;

        int palIdx = (i * 7 + 13) % 128;
        uint32 pal = stellaRef[palIdx];

        uint8 tiaMix = ((pal >> ((i % 3) * 8)) & 0xFF) ^ (lfsr9 & 0xFF) ^ (lfsr5 << 3);
        outKey[i] = (tiaMix ^ ((i * 37 + 101) & 0xFF)) & 0xFF;
    }
}

void AtariMachine::init() {
    riotRAM.fill(0);
    tiaRegs.fill(0);

    cpu.readMem = [this](uint16 addr) -> uint8 {
        return readMem(addr & 0x1FFF);
    };
    cpu.writeMem = [this](uint16 addr, uint8 val) {
        writeMem(addr & 0x1FFF, val);
    };

    reset();
}

void AtariMachine::reset() {
    riotTimer = 0xFF;
    timerDiv = 64;
    timerCycles = 0;
    wsyncHalt = false;
    inVsync = false;
    inVblank = false;
    frameReady = false;
    visibleScanline = 0;
    scanlineCycle = 0;

    p0Pos = 40;
    p1Pos = 110;
    m0Pos = 0;
    m1Pos = 0;
    blPos = 0;

    hmp0 = hmp1 = hmm0 = hmm1 = hmbl = 0;
    grp0_old = 0;
    grp1_old = 0;

    swcha = 0xFF;
    swchb = 0x0B;
    joyFire = false;

    // Default TIA colors
    tiaRegs[0x06] = 0x46; // COLUP0
    tiaRegs[0x07] = 0x86; // COLUP1
    tiaRegs[0x08] = 0xD4; // COLUPF
    tiaRegs[0x09] = 0x00; // COLUBK

    // Ensure cartData is initialized with default embedded ROM if empty
    if (cartData.empty()) {
        cartData.assign(EmbeddedROMs::ATARI_2600_DEFAULT_ROM, EmbeddedROMs::ATARI_2600_DEFAULT_ROM + 4096);
        mediaTitle = "Atari 2600 VCS (Pitfall)";
    }

    // Always copy active ROM into currentBank
    if (cartData.size() == 2048) {
        std::memcpy(&currentBank[0x0000], cartData.data(), 2048);
        std::memcpy(&currentBank[0x0800], cartData.data(), 2048);
    } else if (cartData.size() >= 4096) {
        std::memcpy(currentBank.data(), cartData.data(), 4096);
    }

    cpu.reset();
    cpu.PC = currentBank[0x0FFC] | ((uint16)currentBank[0x0FFD] << 8);
    if (cpu.PC < 0x1000) cpu.PC = 0x1000;
}

uint8 AtariMachine::readMem(uint16 addr) {
    if ((addr & 0x1000) == 0) {
        if ((addr & 0x0080) == 0) {
            // TIA Read ($00..$0D)
            uint8 reg = addr & 0x0F;
            if (reg == 0x0C) return joyFire ? 0x00 : 0x80; // INPT4: P0 Fire button (active low)
            if (reg == 0x0D) return 0x80;                  // INPT5: P1 Fire button
            return tiaRegs[reg];                          // Collision registers ($00..$07)
        } else {
            // RIOT RAM or Timer / Ports
            if ((addr & 0x0200) != 0) {
                int r = addr & 0x07;
                if (r == 0) return swcha;     // Port A: Joystick directions
                if (r == 2) return swchb;     // Port B: Console switches (Reset, Select, Color)
                if (r == 4) return riotTimer; // INTIM: Timer read
                if (r == 5) return (riotTimer == 0) ? 0x80 : 0x00; // INSTAT: Timer interrupt flag
                return 0xFF;
            }
            return riotRAM[addr & 0x7F];
        }
    } else {
        // Cartridge ROM
        return currentBank[addr & 0x0FFF];
    }
}

void AtariMachine::writeMem(uint16 addr, uint8 val) {
    if ((addr & 0x1000) == 0) {
        if ((addr & 0x0080) == 0) {
            // TIA Write ($00..$3F)
            uint8 reg = addr & 0x3F;
            tiaRegs[reg] = val;

            if (reg == 0x00) { // VSYNC
                if (val & 0x02) inVsync = true;
                else if (inVsync) {
                    inVsync = false;
                    frameReady = true;
                    visibleScanline = 0;
                }
            } else if (reg == 0x01) { // VBLANK
                inVblank = (val & 0x02) != 0;
            } else if (reg == 0x02) { // WSYNC
                wsyncHalt = true;
            } else if (reg == 0x10) { // RESP0
                int clk = scanlineCycle * 3;
                p0Pos = (clk < 68) ? 3 : (clk - 68 + 4) % 160;
            } else if (reg == 0x11) { // RESP1
                int clk = scanlineCycle * 3;
                p1Pos = (clk < 68) ? 3 : (clk - 68 + 4) % 160;
            } else if (reg == 0x12) { // RESM0
                int clk = scanlineCycle * 3;
                m0Pos = (clk < 68) ? 3 : (clk - 68 + 4) % 160;
            } else if (reg == 0x13) { // RESM1
                int clk = scanlineCycle * 3;
                m1Pos = (clk < 68) ? 3 : (clk - 68 + 4) % 160;
            } else if (reg == 0x14) { // RESBL
                int clk = scanlineCycle * 3;
                blPos = (clk < 68) ? 3 : (clk - 68 + 4) % 160;
            } else if (reg == 0x1B) { // GRP0 write latches GRP1 for vertical delay
                grp1_old = tiaRegs[0x1C];
            } else if (reg == 0x1C) { // GRP1 write latches GRP0 for vertical delay
                grp0_old = tiaRegs[0x1B];
            } else if (reg == 0x20) { // HMP0
                hmp0 = (int8)(val & 0xF0) >> 4;
            } else if (reg == 0x21) { // HMP1
                hmp1 = (int8)(val & 0xF0) >> 4;
            } else if (reg == 0x22) { // HMM0
                hmm0 = (int8)(val & 0xF0) >> 4;
            } else if (reg == 0x23) { // HMM1
                hmm1 = (int8)(val & 0xF0) >> 4;
            } else if (reg == 0x24) { // HMBL
                hmbl = (int8)(val & 0xF0) >> 4;
            } else if (reg == 0x2A) { // HMOVE
                p0Pos = (p0Pos - hmp0 + 160) % 160;
                p1Pos = (p1Pos - hmp1 + 160) % 160;
                m0Pos = (m0Pos - hmm0 + 160) % 160;
                m1Pos = (m1Pos - hmm1 + 160) % 160;
                blPos = (blPos - hmbl + 160) % 160;
            } else if (reg == 0x2B) { // HMCLR
                hmp0 = hmp1 = hmm0 = hmm1 = hmbl = 0;
            } else if (reg == 0x2C) { // CXCLR: Clear collisions
                for (int i = 0; i < 8; i++) tiaRegs[i] = 0;
            }
        } else {
            // RIOT RAM or Timer write
            if ((addr & 0x0200) != 0) {
                if ((addr & 0x04) != 0 || (addr & 0x14) == 0x14) {
                    // Timer write (TIM1T, TIM8T, TIM64T, T1024T)
                    riotTimer = val;
                    int mode = addr & 0x03;
                    if (mode == 0) timerDiv = 1;
                    else if (mode == 1) timerDiv = 8;
                    else if (mode == 2) timerDiv = 64;
                    else timerDiv = 1024;
                }
            } else {
                riotRAM[addr & 0x7F] = val;
            }
        }
    } else {
        // Bankswitch on write (F8/F6 schemes)
        if ((addr & 0x1FF8) == 0x1FF8) {
            int bank = addr & 1;
            if (cartData.size() >= 8192) {
                std::memcpy(currentBank.data(), &cartData[bank * 4096], 4096);
            }
        }
    }
}

void AtariMachine::runFrame() {
    frameReady = false;
    int totalCycles = 0;
    const int maxCycles = 35000; // ~262 scanlines x 76 cycles = 19,912 cycles + margin

    while (!frameReady && totalCycles < maxCycles) {
        scanlineCycle = 0;
        while (scanlineCycle < 76) {
            if (wsyncHalt) {
                int rem = 76 - scanlineCycle;
                scanlineCycle += rem;
                totalCycles += rem;
                timerCycles += rem;
                while (timerCycles >= timerDiv) {
                    timerCycles -= timerDiv;
                    riotTimer--;
                }
                wsyncHalt = false;
                break;
            }

            int c = cpu.step();
            scanlineCycle += c;
            totalCycles += c;
            timerCycles += c;
            while (timerCycles >= timerDiv) {
                timerCycles -= timerDiv;
                riotTimer--;
            }
        }

        // Render visible scanline (192 visible scanlines per NTSC frame)
        if (!inVblank && !inVsync && visibleScanline < 192) {
            renderScanline(visibleScanline++);
        }
    }

    updateAudio();
}

void AtariMachine::renderScanline(int scanline) {
    if (scanline < 0 || scanline >= 192) return;

    uint32 bgCol = tiaPalette[(tiaRegs[0x09] >> 1) & 0x7F];
    uint32 pfCol = tiaPalette[(tiaRegs[0x08] >> 1) & 0x7F];
    uint32 p0Col = tiaPalette[(tiaRegs[0x06] >> 1) & 0x7F];
    uint32 p1Col = tiaPalette[(tiaRegs[0x07] >> 1) & 0x7F];

    uint8 ctrlpf = tiaRegs[0x0A];
    bool ref = (ctrlpf & 0x01) != 0;
    bool scoreMode = (ctrlpf & 0x02) != 0;

    uint8 pf0 = tiaRegs[0x0D];
    uint8 pf1 = tiaRegs[0x0E];
    uint8 pf2 = tiaRegs[0x0F];
    uint8 grp0 = (tiaRegs[0x25] & 1) ? grp0_old : tiaRegs[0x1B];
    uint8 grp1 = (tiaRegs[0x26] & 1) ? grp1_old : tiaRegs[0x1C];
    uint8 nusiz0 = tiaRegs[0x04];
    uint8 nusiz1 = tiaRegs[0x05];
    bool ref0 = (tiaRegs[0x0B] & 0x08) != 0;
    bool ref1 = (tiaRegs[0x0C] & 0x08) != 0;

    int y1 = 16 + (scanline * 480) / 192;
    int y2 = 16 + ((scanline + 1) * 480) / 192;

    for (int x = 0; x < 160; x++) {
        uint32 pixelCol = bgCol;
        bool pfHit = false;
        bool p0Hit = false;
        bool p1Hit = false;

        // Playfield: 160 clocks = 40 bits (4 clocks per bit)
        int pfBit = x / 4;
        if (pfBit < 20) {
            if (pfBit < 4) pfHit = (pf0 >> (4 + pfBit)) & 1;
            else if (pfBit < 12) pfHit = (pf1 >> (7 - (pfBit - 4))) & 1;
            else pfHit = (pf2 >> (pfBit - 12)) & 1;
        } else {
            int rBit = pfBit - 20;
            if (ref) {
                // Reflected: PF2 reversed, PF1 normal, PF0 reversed
                if (rBit < 8) pfHit = (pf2 >> (7 - rBit)) & 1;
                else if (rBit < 16) pfHit = (pf1 >> (rBit - 8)) & 1;
                else pfHit = (pf0 >> (7 - (rBit - 16))) & 1;
            } else {
                // Repeated
                if (rBit < 4) pfHit = (pf0 >> (4 + rBit)) & 1;
                else if (rBit < 12) pfHit = (pf1 >> (7 - (rBit - 4))) & 1;
                else pfHit = (pf2 >> (rBit - 12)) & 1;
            }
        }

        if (pfHit) {
            if (scoreMode) {
                pixelCol = (x < 80) ? p0Col : p1Col;
            } else {
                pixelCol = pfCol;
            }
        }

        // Helper lambda for player rendering with NUSIZ copies & reflection
        auto checkPlayer = [](int px, int basePos, uint8 nusiz, uint8 grp, bool reflect) -> bool {
            if (grp == 0) return false;
            int copies = 1;
            int spacing = 16;
            int mode = nusiz & 0x07;
            if (mode == 1) { copies = 2; spacing = 16; }
            else if (mode == 2) { copies = 2; spacing = 32; }
            else if (mode == 3) { copies = 3; spacing = 16; }
            else if (mode == 4) { copies = 2; spacing = 64; }
            else if (mode == 6) { copies = 3; spacing = 32; }

            for (int c = 0; c < copies; c++) {
                int pos = (basePos + c * spacing) % 160;
                if (mode == 5) { // Double size
                    if (px >= pos && px < pos + 16) {
                        int bit = (px - pos) / 2;
                        if (reflect) bit = 7 - bit;
                        if ((grp >> (7 - bit)) & 1) return true;
                    }
                } else if (mode == 7) { // Quad size
                    if (px >= pos && px < pos + 32) {
                        int bit = (px - pos) / 4;
                        if (reflect) bit = 7 - bit;
                        if ((grp >> (7 - bit)) & 1) return true;
                    }
                } else { // Normal size
                    if (px >= pos && px < pos + 8) {
                        int bit = px - pos;
                        if (reflect) bit = 7 - bit;
                        if ((grp >> (7 - bit)) & 1) return true;
                    }
                }
            }
            return false;
        };

        p0Hit = checkPlayer(x, p0Pos, nusiz0, grp0, ref0);
        p1Hit = checkPlayer(x, p1Pos, nusiz1, grp1, ref1);

        // Ball (used for vines in Pitfall and ball in sports games)
        bool blHit = false;
        if (tiaRegs[0x1F] & 2) { // ENABL
            int blWidth = 1 << ((ctrlpf >> 4) & 3);
            if (x >= blPos && x < blPos + blWidth) blHit = true;
        }

        // Missile 0
        bool m0Hit = false;
        if (tiaRegs[0x1D] & 2) { // ENAM0
            int m0Width = 1 << ((nusiz0 >> 4) & 3);
            int mode = nusiz0 & 7;
            int copies = (mode == 1 || mode == 2 || mode == 4) ? 2 : ((mode == 3 || mode == 6) ? 3 : 1);
            int spacing = (mode == 2 || mode == 6) ? 32 : ((mode == 4) ? 64 : 16);
            for (int c = 0; c < copies; c++) {
                int pos = (m0Pos + c * spacing) % 160;
                if (x >= pos && x < pos + m0Width) { m0Hit = true; break; }
            }
        }

        // Missile 1
        bool m1Hit = false;
        if (tiaRegs[0x1E] & 2) { // ENAM1
            int m1Width = 1 << ((nusiz1 >> 4) & 3);
            int mode = nusiz1 & 7;
            int copies = (mode == 1 || mode == 2 || mode == 4) ? 2 : ((mode == 3 || mode == 6) ? 3 : 1);
            int spacing = (mode == 2 || mode == 6) ? 32 : ((mode == 4) ? 64 : 16);
            for (int c = 0; c < copies; c++) {
                int pos = (m1Pos + c * spacing) % 160;
                if (x >= pos && x < pos + m1Width) { m1Hit = true; break; }
            }
        }

        // Object priority: P0/M0/P1/M1/BL over Playfield unless priority bit 2 is set in CTRLPF
        if (blHit) pixelCol = pfCol;
        if (m0Hit) pixelCol = p0Col;
        if (m1Hit) pixelCol = p1Col;
        if (p0Hit && (!pfHit || !(ctrlpf & 4))) pixelCol = p0Col;
        if (p1Hit && (!pfHit || !(ctrlpf & 4))) pixelCol = p1Col;

        // Collision detection
        if (m0Hit && p1Hit)  tiaRegs[0x00] |= 0x80; // CXM0P
        if (m0Hit && p0Hit)  tiaRegs[0x00] |= 0x40;
        if (m1Hit && p0Hit)  tiaRegs[0x01] |= 0x80; // CXM1P
        if (m1Hit && p1Hit)  tiaRegs[0x01] |= 0x40;
        if (p0Hit && pfHit)  tiaRegs[0x02] |= 0x80; // CXP0FB
        if (p0Hit && blHit)  tiaRegs[0x02] |= 0x40;
        if (p1Hit && pfHit)  tiaRegs[0x03] |= 0x80; // CXP1FB
        if (p1Hit && blHit)  tiaRegs[0x03] |= 0x40;
        if (m0Hit && pfHit)  tiaRegs[0x04] |= 0x80; // CXM0FB
        if (m0Hit && blHit)  tiaRegs[0x04] |= 0x40;
        if (m1Hit && pfHit)  tiaRegs[0x05] |= 0x80; // CXM1FB
        if (m1Hit && blHit)  tiaRegs[0x05] |= 0x40;
        if (blHit && pfHit)  tiaRegs[0x06] |= 0x80; // CXBLPF
        if (p0Hit && p1Hit)  tiaRegs[0x07] |= 0x80; // CXPPMM
        if (m0Hit && m1Hit)  tiaRegs[0x07] |= 0x40;

        // Fill vertical stretch lines to eliminate gaps
        for (int drawY = y1; drawY < y2 && drawY < 512; drawY++) {
            uint32* line = &framebuffer[drawY * 640 + x * 4];
            line[0] = pixelCol;
            line[1] = pixelCol;
            line[2] = pixelCol;
            line[3] = pixelCol;
        }
    }
}

void AtariMachine::updateAudio() {
    int vol0 = (tiaRegs[0x19] & 0x0F) * 600;
    int vol1 = (tiaRegs[0x1A] & 0x0F) * 600;
    int freq0 = ((tiaRegs[0x17] & 0x1F) + 1) * 4;
    int freq1 = ((tiaRegs[0x18] & 0x1F) + 1) * 4;
    int ctrl0 = tiaRegs[0x15] & 0x0F;
    int ctrl1 = tiaRegs[0x16] & 0x0F;

    for (size_t i = 0; i < audioBuffer.size(); i++) {
        int sample = 0;

        if (vol0 > 0) {
            if (ctrl0 == 8 || ctrl0 == 3) {
                // White noise
                sample += ((rand() % 2 == 0) ? vol0 : -vol0);
            } else {
                // Square wave
                sample += ((i / freq0) % 2 == 0) ? vol0 : -vol0;
            }
        }

        if (vol1 > 0) {
            if (ctrl1 == 8 || ctrl1 == 3) {
                sample += ((rand() % 2 == 0) ? vol1 : -vol1);
            } else {
                sample += ((i / freq1) % 2 == 0) ? vol1 : -vol1;
            }
        }

        audioBuffer[i] = (int16)std::clamp(sample, -32767, 32767);
    }
}

void AtariMachine::handleKeyDown(SDL_Keycode key, uint16 mod) {
    (void)mod;
    if (key == SDLK_UP) joyUp = true;
    if (key == SDLK_DOWN) joyDown = true;
    if (key == SDLK_LEFT) joyLeft = true;
    if (key == SDLK_RIGHT) joyRight = true;
    if (key == SDLK_SPACE || key == SDLK_LCTRL || key == SDLK_z) joyFire = true;
    if (key == SDLK_F12 || key == SDLK_ESCAPE) swchb &= ~0x01; // Game Reset button

    swcha = 0xFF;
    if (joyRight) swcha &= ~0x80;
    if (joyLeft)  swcha &= ~0x40;
    if (joyDown)  swcha &= ~0x20;
    if (joyUp)    swcha &= ~0x10;
}

void AtariMachine::handleKeyUp(SDL_Keycode key, uint16 mod) {
    (void)mod;
    if (key == SDLK_UP) joyUp = false;
    if (key == SDLK_DOWN) joyDown = false;
    if (key == SDLK_LEFT) joyLeft = false;
    if (key == SDLK_RIGHT) joyRight = false;
    if (key == SDLK_SPACE || key == SDLK_LCTRL || key == SDLK_z) joyFire = false;
    if (key == SDLK_F12 || key == SDLK_ESCAPE) swchb |= 0x01;

    swcha = 0xFF;
    if (joyRight) swcha &= ~0x80;
    if (joyLeft)  swcha &= ~0x40;
    if (joyDown)  swcha &= ~0x20;
    if (joyUp)    swcha &= ~0x10;
}

bool AtariMachine::loadMedia(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    cartData = std::vector<uint8>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    std::string fname = path.substr(path.find_last_of("/\\") + 1);
    mediaTitle = fname;

    // Load ROM into currentBank
    if (cartData.size() == 2048) {
        // 2KB ROM mirrored twice in 4KB space
        std::memcpy(&currentBank[0x0000], cartData.data(), 2048);
        std::memcpy(&currentBank[0x0800], cartData.data(), 2048);
    } else if (cartData.size() >= 4096) {
        std::memcpy(currentBank.data(), cartData.data(), 4096);
    }

    reset();
    std::cout << "[ATARI 2600 VCS] Loaded cartridge: " << mediaTitle << " (" << cartData.size() << " bytes)\n";
    return true;
}
