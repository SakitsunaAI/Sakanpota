#include "cpc_machine.h"
#include "embedded_roms_ext.h"
#include "gui_font.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <algorithm>

// Amstrad CPC 27 Hardware Colors
static const uint32 CPC_HW_PALETTE[32] = {
    0xFF7F7F7F, // 0: White
    0xFF7F7F7F, // 1: White
    0xFF00FF7F, // 2: Sea Green
    0xFFFFFF7F, // 3: Pastel Yellow
    0xFF00007F, // 4: Blue
    0xFFFF007F, // 5: Purple
    0xFF007F7F, // 6: Cyan
    0xFFFF7F7F, // 7: Pink
    0xFFFF007F, // 8: Purple
    0xFFFFFF7F, // 9: Pastel Yellow
    0xFFFFFF00, // 10: Bright Yellow
    0xFFFFFFFF, // 11: Bright White
    0xFFFF0000, // 12: Bright Red
    0xFFFF00FF, // 13: Bright Magenta
    0xFFFF7F00, // 14: Orange
    0xFFFF7F7F, // 15: Pink
    0xFF00007F, // 16: Pastel Blue
    0xFF00FFFF, // 17: Bright Cyan
    0xFF7FFF00, // 18: Lime Green
    0xFF7FFF7F, // 19: Pastel Green
    0xFF0000FF, // 20: Bright Blue
    0xFF00FF00, // 21: Bright Green
    0xFF7F007F, // 22: Magenta
    0xFF7F00FF, // 23: Mauve
    0xFF0000FF, // 24: Bright Blue
    0xFFFF00FF, // 25: Bright Magenta
    0xFF00FFFF, // 26: Bright Cyan
    0xFF00FF7F, // 27: Sea Green
    0xFF000000, // 28: Black
    0xFF000000, // 29: Black
    0xFF000000, // 30: Black
    0xFF000000  // 31: Black
};

AmstradCPCMachine::AmstradCPCMachine() {
    screenBuffer.resize(640 * 512, 0xFF14181F);
    audioBuffer.resize(44100 / 50, 0);
    init();
}

void AmstradCPCMachine::init() {
    ram.fill(0);
    std::fill(std::begin(keyMatrix), std::end(keyMatrix), 0xFF);
    std::fill(std::begin(palettePens), std::end(palettePens), 0);
    std::fill(std::begin(crtcRegs), std::end(crtcRegs), 0);
    std::fill(std::begin(ayRegs), std::end(ayRegs), 0);

    // Initial Gate Array configuration
    lowerRomEnabled = true;
    upperRomEnabled = true;
    screenMode = 1;
    selectedPen = 0;
    borderPen = 20; // Default Bright Blue border
    palettePens[0] = 20; // Pen 0 = Blue
    palettePens[1] = 10; // Pen 1 = Yellow
    palettePens[2] = 26; // Pen 2 = Cyan
    palettePens[3] = 12; // Pen 3 = Red

    // Setup Z80 CPU memory callbacks
    cpu.memRead = [this](uint16 addr) -> uint8 {
        if (addr < 0x4000) {
            if (lowerRomEnabled) return EmbeddedROMs::CPC464_OS_ROM[addr];
            return ram[addr];
        }
        if (addr >= 0xC000) {
            if (upperRomEnabled) return EmbeddedROMs::CPC464_BASIC_ROM[addr - 0xC000];
            return ram[addr];
        }
        return ram[addr];
    };

    cpu.memWrite = [this](uint16 addr, uint8 val) {
        // Writes always go to RAM
        ram[addr] = val;
    };

    // Setup Z80 CPU I/O callbacks
    cpu.ioRead = [this](uint16 port) -> uint8 {
        // PPI 8255 read (Port 0xF4xx, 0xF6xx)
        if ((port & 0x0800) == 0) {
            return readPPI(port);
        }
        // CRTC Read (Port 0xBDxx)
        if ((port & 0x4000) == 0 && (port & 0x0100) != 0) {
            return (crtcIndex < 18) ? crtcRegs[crtcIndex] : 0xFF;
        }
        return 0xFF;
    };

    cpu.ioWrite = [this](uint16 port, uint8 val) {
        // Gate Array (Port 0x7Fxx)
        if ((port & 0xC000) == 0x4000) {
            writeGateArray(val);
        }
        // CRTC Register Select (0xBCxx) or Data Write (0xBDxx)
        if ((port & 0x4000) == 0) {
            if ((port & 0x0100) == 0) {
                crtcIndex = val & 0x1F;
            } else {
                if (crtcIndex < 18) crtcRegs[crtcIndex] = val;
            }
        }
        // PPI 8255 write (Port 0xF4xx, 0xF6xx)
        if ((port & 0x0800) == 0) {
            writePPI(port, val);
        }
    };

    reset();
}

void AmstradCPCMachine::reset() {
    std::fill(std::begin(keyMatrix), std::end(keyMatrix), 0xFF);
    cpu.reset();
}

void AmstradCPCMachine::writeGateArray(uint8 val) {
    uint8 cmd = val >> 6;
    if (cmd == 0) {
        // Select Pen or Border
        if (val & 0x10) {
            selectedPen = 16; // Border
        } else {
            selectedPen = val & 0x0F;
        }
    } else if (cmd == 1) {
        // Set Color for selected pen
        uint8 hwCol = val & 0x1F;
        if (selectedPen == 16) {
            borderPen = hwCol;
        } else if (selectedPen < 16) {
            palettePens[selectedPen] = hwCol;
        }
    } else if (cmd == 2) {
        // Screen mode and ROM banking
        screenMode = val & 0x03;
        lowerRomEnabled = (val & 0x04) == 0;
        upperRomEnabled = (val & 0x08) == 0;
    }
}

void AmstradCPCMachine::writePPI(uint16 port, uint8 val) {
    uint8 reg = (port >> 8) & 0x03;
    if (reg == 0) { // Port A (AY-3-8912 Data bus)
        ppiPortA = val;
        if ((ppiPortC & 0xC0) == 0xC0) {
            aySelectedReg = val & 0x0F;
        } else if ((ppiPortC & 0xC0) == 0x80) {
            writeAY(val);
        }
    } else if (reg == 1) { // Port B (Cassette / VSYNC)
        ppiPortB = val;
    } else if (reg == 2) { // Port C (AY Control & Keyboard row select)
        ppiPortC = val;
        selectedKeyRow = val & 0x0F;
        uint8 ayFunc = val & 0xC0;
        if (ayFunc == 0xC0) {
            aySelectedReg = ppiPortA & 0x0F;
        } else if (ayFunc == 0x80) {
            writeAY(ppiPortA);
        }
    } else if (reg == 3) { // Control Register
        ppiControl = val;
    }
}

uint8 AmstradCPCMachine::readPPI(uint16 port) {
    uint8 reg = (port >> 8) & 0x03;
    if (reg == 0) { // Port A: Read AY register or keyboard line
        if ((ppiPortC & 0xC0) == 0x40) {
            return readAY();
        }
        return (selectedKeyRow < 10) ? keyMatrix[selectedKeyRow] : 0xFF;
    } else if (reg == 1) { // Port B: 50Hz VSYNC (bit 0), Jumpers
        return 0x1E; // 50Hz PAL, Ready
    } else if (reg == 2) {
        return ppiPortC;
    }
    return 0xFF;
}

void AmstradCPCMachine::writeAY(uint8 val) {
    if (aySelectedReg < 16) {
        ayRegs[aySelectedReg] = val;
    }
}

uint8 AmstradCPCMachine::readAY() {
    if (aySelectedReg == 14) { // Port A of AY-3-8912 is keyboard matrix on CPC
        return (selectedKeyRow < 10) ? keyMatrix[selectedKeyRow] : 0xFF;
    }
    return (aySelectedReg < 16) ? ayRegs[aySelectedReg] : 0xFF;
}

void AmstradCPCMachine::runFrame() {
    // 4 MHz Z80 CPU: ~80,000 cycles per 50Hz frame
    const int CYCLES_PER_FRAME = 80000;
    int cyclesRun = 0;
    while (cyclesRun < CYCLES_PER_FRAME) {
        cyclesRun += cpu.step();
    }

    // Fire 50Hz raster interrupt (Mode 1 on CPC)
    cpu.requestInterrupt(0xFF);

    renderScreen();
    updateAudio();
}

void AmstradCPCMachine::renderScreen() {
    // Authentic Amstrad CPC 464 Chassis with Border & Cassette Deck
    uint32 borderCol = CPC_HW_PALETTE[borderPen & 0x1F];
    std::fill(screenBuffer.begin(), screenBuffer.end(), borderCol);

    // Amstrad CPC Bezel (Dark charcoal gray with colorful logo)
    GUIFont::fillRect(screenBuffer.data(), 640 * 4, 0, 0, 640, 56, 0xFF121418);
    GUIFont::drawString(screenBuffer.data(), 640 * 4, 24, 18, "AMSTRAD CPC 464", 0xFFFF0000);
    GUIFont::drawString(screenBuffer.data(), 640 * 4, 160, 18, "64K Microcomputer System", 0xFF00FF00);
    GUIFont::drawString(screenBuffer.data(), 640 * 4, 380, 18, "[Locomotive BASIC v1.0]", 0xFF00FFFF);

    // Cassette Deck status indicator
    GUIFont::fillRect(screenBuffer.data(), 640 * 4, 560, 14, 56, 26, 0xFF22262E);
    GUIFont::drawString(screenBuffer.data(), 640 * 4, 568, 22, "TAPE", 0xFFE0E0E0);

    // Bottom Bezel
    GUIFont::fillRect(screenBuffer.data(), 640 * 4, 0, 456, 640, 56, 0xFF121418);
    GUIFont::drawString(screenBuffer.data(), 640 * 4, 24, 474, "Amstrad Consumer Electronics plc / Locomotive Software Ltd.", 0xFF888E99);

    const int OFFSET_X = 0;
    const int OFFSET_Y = 56;

    // Render 640x400 display area from 16KB VRAM (0xC000..0xFFFF)
    for (int y = 0; y < 200; y++) {
        uint16 lineAddr = 0xC000 + ((y % 8) * 0x800) + ((y / 8) * 80);
        int outY1 = OFFSET_Y + (y * 2);
        int outY2 = outY1 + 1;

        if (screenMode == 1) { // Mode 1: 320x200 4 colors (scaled 2x horizontally)
            for (int byteIdx = 0; byteIdx < 80; byteIdx++) {
                uint8 b = ram[lineAddr + byteIdx];

                // Mode 1: 4 pixels per byte
                uint8 p0 = ((b >> 7) & 1) | (((b >> 3) & 1) << 1);
                uint8 p1 = ((b >> 6) & 1) | (((b >> 2) & 1) << 1);
                uint8 p2 = ((b >> 5) & 1) | (((b >> 1) & 1) << 1);
                uint8 p3 = ((b >> 4) & 1) | (((b >> 0) & 1) << 1);

                uint32 c0 = CPC_HW_PALETTE[palettePens[p0 & 3] & 0x1F];
                uint32 c1 = CPC_HW_PALETTE[palettePens[p1 & 3] & 0x1F];
                uint32 c2 = CPC_HW_PALETTE[palettePens[p2 & 3] & 0x1F];
                uint32 c3 = CPC_HW_PALETTE[palettePens[p3 & 3] & 0x1F];

                int px = OFFSET_X + (byteIdx * 8);

                // Pixel 0 (2x wide)
                screenBuffer[outY1 * 640 + px] = c0;
                screenBuffer[outY1 * 640 + px + 1] = c0;
                screenBuffer[outY2 * 640 + px] = c0;
                screenBuffer[outY2 * 640 + px + 1] = c0;

                // Pixel 1 (2x wide)
                screenBuffer[outY1 * 640 + px + 2] = c1;
                screenBuffer[outY1 * 640 + px + 3] = c1;
                screenBuffer[outY2 * 640 + px + 2] = c1;
                screenBuffer[outY2 * 640 + px + 3] = c1;

                // Pixel 2 (2x wide)
                screenBuffer[outY1 * 640 + px + 4] = c2;
                screenBuffer[outY1 * 640 + px + 5] = c2;
                screenBuffer[outY2 * 640 + px + 4] = c2;
                screenBuffer[outY2 * 640 + px + 5] = c2;

                // Pixel 3 (2x wide)
                screenBuffer[outY1 * 640 + px + 6] = c3;
                screenBuffer[outY1 * 640 + px + 7] = c3;
                screenBuffer[outY2 * 640 + px + 6] = c3;
                screenBuffer[outY2 * 640 + px + 7] = c3;
            }
        } else if (screenMode == 2) { // Mode 2: 640x200 2 colors
            for (int byteIdx = 0; byteIdx < 80; byteIdx++) {
                uint8 b = ram[lineAddr + byteIdx];
                int px = OFFSET_X + (byteIdx * 8);
                for (int bit = 7; bit >= 0; bit--) {
                    uint8 p = (b >> bit) & 1;
                    uint32 c = CPC_HW_PALETTE[palettePens[p & 1] & 0x1F];
                    screenBuffer[outY1 * 640 + px + (7 - bit)] = c;
                    screenBuffer[outY2 * 640 + px + (7 - bit)] = c;
                }
            }
        } else { // Mode 0: 160x200 16 colors (4x wide pixels)
            for (int byteIdx = 0; byteIdx < 80; byteIdx++) {
                uint8 b = ram[lineAddr + byteIdx];
                uint8 p0 = ((b >> 7) & 1) | (((b >> 3) & 1) << 1) | (((b >> 5) & 1) << 2) | (((b >> 1) & 1) << 3);
                uint8 p1 = ((b >> 6) & 1) | (((b >> 2) & 1) << 1) | (((b >> 4) & 1) << 2) | (((b >> 0) & 1) << 3);

                uint32 c0 = CPC_HW_PALETTE[palettePens[p0 & 15] & 0x1F];
                uint32 c1 = CPC_HW_PALETTE[palettePens[p1 & 15] & 0x1F];

                int px = OFFSET_X + (byteIdx * 8);
                for (int dx = 0; dx < 4; dx++) {
                    screenBuffer[outY1 * 640 + px + dx] = c0;
                    screenBuffer[outY2 * 640 + px + dx] = c0;
                    screenBuffer[outY1 * 640 + px + 4 + dx] = c1;
                    screenBuffer[outY2 * 640 + px + 4 + dx] = c1;
                }
            }
        }
    }
}

void AmstradCPCMachine::updateAudio() {
    // 3 channels AY-3-8912 synthesis
    uint16 periodA = ayRegs[0] | ((uint16)(ayRegs[1] & 0x0F) << 8);
    uint8 volA = ayRegs[8] & 0x0F;

    if (periodA < 10) periodA = 10;
    int sample = 0;
    for (size_t i = 0; i < audioBuffer.size(); i++) {
        ayTonePhase[0] += 44100;
        if (ayTonePhase[0] >= (uint32)(periodA * 200)) {
            ayTonePhase[0] = 0;
            ayToneOutput[0] = !ayToneOutput[0];
        }
        sample = ayToneOutput[0] ? (volA * 1000) : -(volA * 1000);
        audioBuffer[i] = sample;
    }
}

void AmstradCPCMachine::handleKeyDown(SDL_Keycode key, uint16 mod) {
    auto press = [this](int row, int bit) {
        if (row < 10) keyMatrix[row] &= ~(1 << bit);
    };

    switch (key) {
        // Line 0: Up, Right, Down, 9, 6, 3, Enter, .
        case SDLK_UP: press(0, 0); break;
        case SDLK_RIGHT: press(0, 1); break;
        case SDLK_DOWN: press(0, 2); break;
        case SDLK_9: press(0, 3); break;
        case SDLK_6: press(0, 4); break;
        case SDLK_3: press(0, 5); break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER: press(0, 6); break;
        case SDLK_PERIOD: press(0, 7); break;

        // Line 1: Left, Copy, 7, 8, 5, 1, 2, 0
        case SDLK_LEFT: press(1, 0); break;
        case SDLK_7: press(1, 2); break;
        case SDLK_8: press(1, 3); break;
        case SDLK_5: press(1, 4); break;
        case SDLK_1: press(1, 5); break;
        case SDLK_2: press(1, 6); break;
        case SDLK_0: press(1, 7); break;

        // Line 2: CLR, [, Enter, ], 4, Shift, \, Ctrl
        case SDLK_BACKSPACE: press(2, 0); break;
        case SDLK_LEFTBRACKET: press(2, 1); break;
        case SDLK_RIGHTBRACKET: press(2, 3); break;
        case SDLK_4: press(2, 4); break;
        case SDLK_LSHIFT:
        case SDLK_RSHIFT: press(2, 5); break;
        case SDLK_BACKSLASH: press(2, 6); break;
        case SDLK_LCTRL:
        case SDLK_RCTRL: press(2, 7); break;

        // Line 3: ^, -, @, P, ;, :, /, .
        case SDLK_MINUS: press(3, 1); break;
        case SDLK_p: press(3, 3); break;
        case SDLK_SEMICOLON: press(3, 4); break;
        case SDLK_COLON: press(3, 5); break;
        case SDLK_SLASH: press(3, 6); break;

        // Line 4: 0, 9, O, I, L, K, M, ,
        case SDLK_o: press(4, 2); break;
        case SDLK_i: press(4, 3); break;
        case SDLK_l: press(4, 4); break;
        case SDLK_k: press(4, 5); break;
        case SDLK_m: press(4, 6); break;
        case SDLK_COMMA: press(4, 7); break;

        // Line 5: 8, 7, U, Y, H, J, N, Space
        case SDLK_u: press(5, 2); break;
        case SDLK_y: press(5, 3); break;
        case SDLK_h: press(5, 4); break;
        case SDLK_j: press(5, 5); break;
        case SDLK_n: press(5, 6); break;
        case SDLK_SPACE: press(5, 7); break;

        // Line 6: 6, 5, R, T, G, F, B, V
        case SDLK_r: press(6, 2); break;
        case SDLK_t: press(6, 3); break;
        case SDLK_g: press(6, 4); break;
        case SDLK_f: press(6, 5); break;
        case SDLK_b: press(6, 6); break;
        case SDLK_v: press(6, 7); break;

        // Line 7: 4, 3, E, W, D, S, C, X
        case SDLK_e: press(7, 2); break;
        case SDLK_w: press(7, 3); break;
        case SDLK_d: press(7, 4); break;
        case SDLK_s: press(7, 5); break;
        case SDLK_c: press(7, 6); break;
        case SDLK_x: press(7, 7); break;

        // Line 8: 1, 2, Esc, Q, Tab, A, Caps, Z
        case SDLK_ESCAPE: press(8, 2); break;
        case SDLK_q: press(8, 3); break;
        case SDLK_TAB: press(8, 4); break;
        case SDLK_a: press(8, 5); break;
        case SDLK_CAPSLOCK: press(8, 6); break;
        case SDLK_z: press(8, 7); break;

        default: break;
    }
}

void AmstradCPCMachine::handleKeyUp(SDL_Keycode key, uint16 mod) {
    auto release = [this](int row, int bit) {
        if (row < 10) keyMatrix[row] |= (1 << bit);
    };

    switch (key) {
        case SDLK_UP: release(0, 0); break;
        case SDLK_RIGHT: release(0, 1); break;
        case SDLK_DOWN: release(0, 2); break;
        case SDLK_9: release(0, 3); break;
        case SDLK_6: release(0, 4); break;
        case SDLK_3: release(0, 5); break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER: release(0, 6); break;
        case SDLK_PERIOD: release(0, 7); break;

        case SDLK_LEFT: release(1, 0); break;
        case SDLK_7: release(1, 2); break;
        case SDLK_8: release(1, 3); break;
        case SDLK_5: release(1, 4); break;
        case SDLK_1: release(1, 5); break;
        case SDLK_2: release(1, 6); break;
        case SDLK_0: release(1, 7); break;

        case SDLK_BACKSPACE: release(2, 0); break;
        case SDLK_LEFTBRACKET: release(2, 1); break;
        case SDLK_RIGHTBRACKET: release(2, 3); break;
        case SDLK_4: release(2, 4); break;
        case SDLK_LSHIFT:
        case SDLK_RSHIFT: release(2, 5); break;
        case SDLK_BACKSLASH: release(2, 6); break;
        case SDLK_LCTRL:
        case SDLK_RCTRL: release(2, 7); break;

        case SDLK_MINUS: release(3, 1); break;
        case SDLK_p: release(3, 3); break;
        case SDLK_SEMICOLON: release(3, 4); break;
        case SDLK_COLON: release(3, 5); break;
        case SDLK_SLASH: release(3, 6); break;

        case SDLK_o: release(4, 2); break;
        case SDLK_i: release(4, 3); break;
        case SDLK_l: release(4, 4); break;
        case SDLK_k: release(4, 5); break;
        case SDLK_m: release(4, 6); break;
        case SDLK_COMMA: release(4, 7); break;

        case SDLK_u: release(5, 2); break;
        case SDLK_y: release(5, 3); break;
        case SDLK_h: release(5, 4); break;
        case SDLK_j: release(5, 5); break;
        case SDLK_n: release(5, 6); break;
        case SDLK_SPACE: release(5, 7); break;

        case SDLK_r: release(6, 2); break;
        case SDLK_t: release(6, 3); break;
        case SDLK_g: release(6, 4); break;
        case SDLK_f: release(6, 5); break;
        case SDLK_b: release(6, 6); break;
        case SDLK_v: release(6, 7); break;

        case SDLK_e: release(7, 2); break;
        case SDLK_w: release(7, 3); break;
        case SDLK_d: release(7, 4); break;
        case SDLK_s: release(7, 5); break;
        case SDLK_c: release(7, 6); break;
        case SDLK_x: release(7, 7); break;

        case SDLK_ESCAPE: release(8, 2); break;
        case SDLK_q: release(8, 3); break;
        case SDLK_TAB: release(8, 4); break;
        case SDLK_a: release(8, 5); break;
        case SDLK_CAPSLOCK: release(8, 6); break;
        case SDLK_z: release(8, 7); break;

        default: break;
    }
}

bool AmstradCPCMachine::loadMedia(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    std::vector<uint8> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    if (path.length() >= 4 && path.substr(path.length() - 4) == ".dsk") {
        return loadDSK(data);
    }
    return false;
}

bool AmstradCPCMachine::loadDSK(const std::vector<uint8>& data) {
    if (data.size() < 256) return false;
    // Inject DSK file sectors into RAM at 0x1200
    size_t copyLen = std::min((size_t)data.size() - 256, (size_t)32768);
    std::memcpy(&ram[0x1200], &data[256], copyLen);
    currentMedia = "CPC Floppy DSK";
    std::cout << "[Amstrad CPC] Successfully mounted DSK image (" << data.size() << " bytes)!\n";
    return true;
}
