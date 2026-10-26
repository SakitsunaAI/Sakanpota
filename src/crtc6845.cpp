#include "crtc6845.h"
#include <cstring>
#include <algorithm>

const uint32 CRTC6845::physicalRGB[8] = {
    0xFF000000, // 0: Black
    0xFFFF0000, // 1: Red
    0xFF00FF00, // 2: Green
    0xFFFFFF00, // 3: Yellow
    0xFF0000FF, // 4: Blue
    0xFFFF00FF, // 5: Magenta
    0xFF00FFFF, // 6: Cyan
    0xFFFFFFFF  // 7: White
};

CRTC6845::CRTC6845() {
    reset();
}

void CRTC6845::reset() {
    addressRegister = 0;
    registers.fill(0);

    // Standard initial CRTC values for BBC Mode 7
    registers[0] = 63;  // Horiz Total
    registers[1] = 40;  // Horiz Displayed
    registers[2] = 49;  // Horiz Sync
    registers[3] = 0x24;// Sync Width
    registers[4] = 38;  // Vert Total
    registers[5] = 0;   // Vert Total Adjust
    registers[6] = 25;  // Vert Displayed
    registers[7] = 30;  // Vert Sync
    registers[8] = 0;   // Interlace
    registers[9] = 18;  // Max Scanline
    registers[12] = 0x28; // Start Addr High ($7C00 >> 3 / 0x20)
    registers[13] = 0x00; // Start Addr Low

    videoControl = 0x9E; // Teletext mode enabled by default
    paletteControl = 0;

    // Default logical palette: 0..7 standard colors, 8..15 inverted
    for (int i = 0; i < 8; i++) logicalPalette[i] = i;
    for (int i = 8; i < 16; i++) logicalPalette[i] = i ^ 7;

    flashCounter = 0;
    flashState = false;
}

uint8 CRTC6845::readCRTC(uint16 addr) {
    if ((addr & 1) == 1) {
        if (addressRegister < registers.size()) {
            return registers[addressRegister];
        }
    }
    return 0xFF;
}

void CRTC6845::writeCRTC(uint16 addr, uint8 val) {
    if ((addr & 1) == 0) {
        addressRegister = val & 0x1F;
    } else {
        if (addressRegister < registers.size()) {
            registers[addressRegister] = val;
        }
    }
}

uint8 CRTC6845::readULA(uint16 addr) {
    return 0xFF;
}

void CRTC6845::writeULA(uint16 addr, uint8 val) {
    if ((addr & 1) == 0) {
        // $FE20: Video Control Register
        videoControl = val;
    } else {
        // $FE21: Palette Register
        uint8 logical = (val >> 4) & 0x0F;
        uint8 physical = (val & 0x0F) ^ 0x07;
        logicalPalette[logical] = physical;
    }
}

uint32 CRTC6845::getPaletteColor(uint8 logical) {
    uint8 phys = logicalPalette[logical & 0x0F] & 0x07;
    // Check flashing (bit 3 of physical palette in BBC ULA)
    if ((logicalPalette[logical & 0x0F] & 0x08) && flashState) {
        phys ^= 0x07;
    }
    return physicalRGB[phys & 0x07];
}

void CRTC6845::renderFrame(uint32* framebuffer, int pitch) {
    // Update 50Hz / 100Hz flash cycle
    flashCounter++;
    if (flashCounter >= 25) { // Flash every half-second (~25 frames)
        flashCounter = 0;
        flashState = !flashState;
    }

    if (!readScreenRAM) {
        std::memset(framebuffer, 0, SCREEN_HEIGHT * pitch);
        return;
    }

    // Check if Mode 7 (Teletext) is selected via Video Control Register bit 1
    if (isTeletextMode()) {
        uint8 ram7C00[1000];
        for (int i = 0; i < 1000; i++) {
            ram7C00[i] = readScreenRAM(0x7C00 + i);
        }
        teletext.renderScreen(ram7C00, framebuffer, pitch, flashState);
        return;
    }

    // Modes 0 .. 6 (Graphics / Bitmapped Modes)
    uint8 horizDisplayed = registers[1]; // Chars per row
    uint8 vertDisplayed = registers[6];  // Rows displayed
    uint8 maxRaster = (registers[9] & 0x1F) + 1; // Scanlines per char row (usually 8)
    if (maxRaster == 0) maxRaster = 8;

    uint16 startAddr = ((uint16)registers[12] << 8) | registers[13];
    // In BBC Micro, CRTC addresses memory in words, and startAddr << 3 is byte offset
    uint16 baseRAMAddr = (startAddr << 3) & 0x7FFF;

    // Detect pixels per byte based on Video Control clock / width
    // Bit 4..5 of videoControl determine 10 MHz vs 20 MHz pixel clock
    // Standard BBC modes:
    // horizDisplayed == 80: Mode 0 or 3 (640 wide, 1 bpp)
    // horizDisplayed == 40: Mode 1 or 4 (320 wide: 2 bpp in Mode 1, 1 bpp in Mode 4)
    // horizDisplayed == 20: Mode 2 or 5 (160 wide: 4 bpp in Mode 2, 2 bpp in Mode 5)
    int bpp = 1;
    if (horizDisplayed == 40) {
        // Mode 1 (2 bpp) or Mode 4 (1 bpp)
        bpp = ((videoControl & 0x10) != 0) ? 2 : 1;
    } else if (horizDisplayed == 20) {
        // Mode 2 (4 bpp) or Mode 5 (2 bpp)
        bpp = ((videoControl & 0x10) != 0) ? 4 : 2;
    }

    // Clear background
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        uint32* row = framebuffer + y * (pitch / 4);
        for (int x = 0; x < SCREEN_WIDTH; x++) row[x] = 0xFF000000;
    }

    for (int charRow = 0; charRow < vertDisplayed; charRow++) {
        for (int charCol = 0; charCol < horizDisplayed; charCol++) {
            // In BBC Micro, 8 consecutive bytes in RAM represent the 8 scanlines of one character cell
            uint16 cellAddr = baseRAMAddr + (charRow * horizDisplayed + charCol) * 8;

            for (int scanline = 0; scanline < maxRaster; scanline++) {
                int screenY = (charRow * maxRaster + scanline) * 2;
                if (screenY >= SCREEN_HEIGHT) break;

                // For 10-line character modes (Mode 3, 6), rasters 8 and 9 are blank interline spacing
                uint8 byte = (scanline < 8) ? readScreenRAM((cellAddr + scanline) & 0x7FFF) : 0;
                uint32* linePtr0 = framebuffer + screenY * (pitch / 4);
                uint32* linePtr1 = (screenY + 1 < SCREEN_HEIGHT) ? (framebuffer + (screenY + 1) * (pitch / 4)) : nullptr;

                if (bpp == 1) {
                    // 1 bit per pixel: 8 pixels per byte (Mode 0: 640x256, Mode 4/6: 320x256)
                    int screenX = (horizDisplayed == 80) ? (charCol * 8) : (charCol * 16);
                    for (int p = 0; p < 8; p++) {
                        uint8 colVal = (byte >> (7 - p)) & 1;
                        uint32 color = getPaletteColor(colVal ? 8 : 0);
                        if (horizDisplayed == 80) {
                            if (screenX + p < SCREEN_WIDTH) {
                                linePtr0[screenX + p] = color;
                                if (linePtr1) linePtr1[screenX + p] = color;
                            }
                        } else {
                            if (screenX + p * 2 + 1 < SCREEN_WIDTH) {
                                linePtr0[screenX + p * 2] = color;
                                linePtr0[screenX + p * 2 + 1] = color;
                                if (linePtr1) {
                                    linePtr1[screenX + p * 2] = color;
                                    linePtr1[screenX + p * 2 + 1] = color;
                                }
                            }
                        }
                    }
                } else if (bpp == 2) {
                    // 2 bits per pixel: 4 pixels per byte (Mode 1: 320x256, Mode 5: 160x256)
                    int screenX = (horizDisplayed == 40) ? (charCol * 16) : (charCol * 32);
                    uint8 p0 = ((byte >> 7) & 1) << 1 | ((byte >> 3) & 1);
                    uint8 p1 = ((byte >> 6) & 1) << 1 | ((byte >> 2) & 1);
                    uint8 p2 = ((byte >> 5) & 1) << 1 | ((byte >> 1) & 1);
                    uint8 p3 = ((byte >> 4) & 1) << 1 | ((byte >> 0) & 1);
                    uint8 pixels[4] = { p0, p1, p2, p3 };

                    int scale = (horizDisplayed == 40) ? 4 : 8;
                    for (int p = 0; p < 4; p++) {
                        uint8 ulaIdx = ((pixels[p] & 2) ? 8 : 0) | ((pixels[p] & 1) ? 2 : 0);
                        uint32 color = getPaletteColor(ulaIdx);
                        for (int s = 0; s < scale; s++) {
                            int x = screenX + p * scale + s;
                            if (x < SCREEN_WIDTH) {
                                linePtr0[x] = color;
                                if (linePtr1) linePtr1[x] = color;
                            }
                        }
                    }
                } else if (bpp == 4) {
                    // 4 bits per pixel: 2 pixels per byte (Mode 2: 160x256)
                    int screenX = charCol * 32;
                    uint8 p0 = ((byte >> 7) & 1) << 3 | ((byte >> 5) & 1) << 2 | ((byte >> 3) & 1) << 1 | ((byte >> 1) & 1);
                    uint8 p1 = ((byte >> 6) & 1) << 3 | ((byte >> 4) & 1) << 2 | ((byte >> 2) & 1) << 1 | ((byte >> 0) & 1);

                    uint32 color0 = getPaletteColor(p0);
                    uint32 color1 = getPaletteColor(p1);

                    for (int s = 0; s < 16; s++) {
                        if (screenX + s < SCREEN_WIDTH) {
                            linePtr0[screenX + s] = color0;
                            if (linePtr1) linePtr1[screenX + s] = color0;
                        }
                        if (screenX + 16 + s < SCREEN_WIDTH) {
                            linePtr0[screenX + 16 + s] = color1;
                            if (linePtr1) linePtr1[screenX + 16 + s] = color1;
                        }
                    }
                }
            }
        }
    }
}
