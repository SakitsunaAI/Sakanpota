#include "spectrum_machine.h"
#include "embedded_roms_ext.h"
#include "gui_font.h"
#include <cstring>
#include <fstream>
#include <iostream>

static const uint32 SPECTRUM_PALETTE[16] = {
    // Normal colors (0..7)
    0xFF000000, // 0: Black
    0xFF0000CD, // 1: Blue
    0xFFCD0000, // 2: Red
    0xFFCD00CD, // 3: Magenta
    0xFF00CD00, // 4: Green
    0xFF00CDCD, // 5: Cyan
    0xFFCDCD00, // 6: Yellow
    0xFFCDCDCD, // 7: White
    // Bright colors (8..15)
    0xFF000000, // 8: Bright Black
    0xFF0000FF, // 9: Bright Blue
    0xFFFF0000, // 10: Bright Red
    0xFFFF00FF, // 11: Bright Magenta
    0xFF00FF00, // 12: Bright Green
    0xFF00FFFF, // 13: Bright Cyan
    0xFFFFFF00, // 14: Bright Yellow
    0xFFFFFFFF  // 15: Bright White
};

ZXSpectrumMachine::ZXSpectrumMachine() {
    screenBuffer.resize(640 * 512, 0xFF0D1117);
    audioBuffer.resize(44100 / 50, 0);
    init();
}

void ZXSpectrumMachine::init() {
    memory.fill(0);
    // Copy 16KB genuine Sinclair BASIC ROM into 0x0000..0x3FFF
    std::memcpy(memory.data(), EmbeddedROMs::SPECTRUM_48_ROM, 16384);

    // Setup Z80 CPU memory bus callbacks
    cpu.memRead = [this](uint16 addr) -> uint8 {
        return memory[addr];
    };

    cpu.memWrite = [this](uint16 addr, uint8 val) {
        // ROM is read-only at 0x0000..0x3FFF
        if (addr >= 0x4000) {
            memory[addr] = val;
        }
    };

    // Setup Z80 CPU I/O bus callbacks (ULA on port 0xFE)
    cpu.ioRead = [this](uint16 port) -> uint8 {
        if ((port & 0x01) == 0) {
            uint8 val = 0xBF; // bits 5 & 7 normally 1
            // Check each half-row corresponding to high address lines A8..A15
            for (int r = 0; r < 8; r++) {
                if (((port >> (8 + r)) & 1) == 0) {
                    val &= keyMatrix[r];
                }
            }
            return val;
        }
        return 0xFF;
    };

    cpu.ioWrite = [this](uint16 port, uint8 val) {
        if ((port & 0x01) == 0) {
            borderColor = val & 0x07;
            beeperState = (val & 0x10) != 0;
        }
    };

    reset();
}

void ZXSpectrumMachine::reset() {
    std::fill(std::begin(keyMatrix), std::end(keyMatrix), 0xBF);
    borderColor = 7;
    beeperState = false;
    flashFrameCounter = 0;
    flashInvert = false;
    cpu.reset();
}

void ZXMachineStep(CPUZ80& cpu, int cycles) {
    int count = 0;
    while (count < cycles) {
        count += cpu.step();
    }
}

void ZXSpectrumMachine::runFrame() {
    // 3.5 MHz Z80 CPU: 69888 T-states per 50Hz frame
    // Execute instruction cycles for one full PAL frame
    const int CYCLES_PER_FRAME = 69888;
    int cyclesRun = 0;
    while (cyclesRun < CYCLES_PER_FRAME) {
        cyclesRun += cpu.step();
    }

    // Fire 50Hz maskable interrupt (IM 1 jumps to 0x0038)
    cpu.requestInterrupt(0xFF);

    flashFrameCounter++;
    if (flashFrameCounter >= 16) {
        flashFrameCounter = 0;
        flashInvert = !flashInvert;
    }

    renderScreen();
    updateAudio();
}

void ZXSpectrumMachine::renderScreen() {
    // Authentic Sinclair ZX Spectrum 48K chassis with border & rainbow stripes
    // Canvas size: 640 x 512
    uint32 borderCol = SPECTRUM_PALETTE[borderColor & 0x07];

    // Screen area: 256x192, scaled 2x -> 512 x 384
    // Positioned centered at offsetX = (640 - 512)/2 = 64, offsetY = (512 - 384)/2 = 64
    const int OFFSET_X = 64;
    const int OFFSET_Y = 64;

    // Fill screen buffer with border color
    std::fill(screenBuffer.begin(), screenBuffer.end(), borderCol);

    // Draw top and bottom Sinclair branding bezel
    // Top banner
    GUIFont::fillRect(screenBuffer.data(), 640 * 4, 0, 0, 640, 48, 0xFF181C22);
    GUIFont::drawString(screenBuffer.data(), 640 * 4, 24, 16, "SINCLAIR ZX SPECTRUM 48K", 0xFFE0E0E0);

    // Iconic 4-color rainbow stripe (Red, Yellow, Green, Cyan)
    GUIFont::fillRect(screenBuffer.data(), 640 * 4, 480, 16, 24, 14, 0xFFE00000); // Red
    GUIFont::fillRect(screenBuffer.data(), 640 * 4, 504, 16, 24, 14, 0xFFFFD700); // Yellow
    GUIFont::fillRect(screenBuffer.data(), 640 * 4, 528, 16, 24, 14, 0xFF00D700); // Green
    GUIFont::fillRect(screenBuffer.data(), 640 * 4, 552, 16, 24, 14, 0xFF00D7D7); // Cyan

    // Bottom banner
    GUIFont::fillRect(screenBuffer.data(), 640 * 4, 0, 464, 640, 48, 0xFF181C22);
    GUIFont::drawString(screenBuffer.data(), 640 * 4, 24, 478, "(c) 1982 Sinclair Research Ltd", 0xFFA0A0A0);

    // Render 256x192 Spectrum screen pixels
    for (int y = 0; y < 192; y++) {
        // Interleaved ULA line address
        uint16 lineAddr = 0x4000 | ((y & 0xC0) << 5) | ((y & 0x07) << 8) | ((y & 0x38) << 2);
        uint16 attrBase = 0x5800 + ((y >> 3) * 32);

        int outY1 = OFFSET_Y + (y * 2);
        int outY2 = outY1 + 1;

        for (int col = 0; col < 32; col++) {
            uint8 pixels = memory[lineAddr + col];
            uint8 attr = memory[attrBase + col];

            uint8 inkIdx = attr & 0x07;
            uint8 paperIdx = (attr >> 3) & 0x07;
            bool bright = (attr & 0x40) != 0;
            bool flash = (attr & 0x80) != 0;

            if (bright) {
                inkIdx += 8;
                paperIdx += 8;
            }

            if (flash && flashInvert) {
                std::swap(inkIdx, paperIdx);
            }

            uint32 inkColor = SPECTRUM_PALETTE[inkIdx];
            uint32 paperColor = SPECTRUM_PALETTE[paperIdx];

            for (int b = 7; b >= 0; b--) {
                uint32 c = ((pixels >> b) & 1) ? inkColor : paperColor;
                int px = OFFSET_X + ((col * 8 + (7 - b)) * 2);

                screenBuffer[outY1 * 640 + px] = c;
                screenBuffer[outY1 * 640 + px + 1] = c;
                screenBuffer[outY2 * 640 + px] = c;
                screenBuffer[outY2 * 640 + px + 1] = c;
            }
        }
    }
}

void ZXSpectrumMachine::updateAudio() {
    int16 sample = beeperState ? 12000 : -12000;
    std::fill(audioBuffer.begin(), audioBuffer.end(), sample);
}

void ZXSpectrumMachine::handleKeyDown(SDL_Keycode key, uint16 mod) {
    // Matrix bit 0: key active (0 = pressed)
    auto press = [this](int row, int bit) {
        keyMatrix[row] &= ~(1 << bit);
    };

    switch (key) {
        // Row 0: Shift, Z, X, C, V
        case SDLK_LSHIFT:
        case SDLK_RSHIFT: press(0, 0); break;
        case SDLK_z: press(0, 1); break;
        case SDLK_x: press(0, 2); break;
        case SDLK_c: press(0, 3); break;
        case SDLK_v: press(0, 4); break;

        // Row 1: A, S, D, F, G
        case SDLK_a: press(1, 0); break;
        case SDLK_s: press(1, 1); break;
        case SDLK_d: press(1, 2); break;
        case SDLK_f: press(1, 3); break;
        case SDLK_g: press(1, 4); break;

        // Row 2: Q, W, E, R, T
        case SDLK_q: press(2, 0); break;
        case SDLK_w: press(2, 1); break;
        case SDLK_e: press(2, 2); break;
        case SDLK_r: press(2, 3); break;
        case SDLK_t: press(2, 4); break;

        // Row 3: 1, 2, 3, 4, 5
        case SDLK_1: press(3, 0); break;
        case SDLK_2: press(3, 1); break;
        case SDLK_3: press(3, 2); break;
        case SDLK_4: press(3, 3); break;
        case SDLK_5: press(3, 4); break;

        // Row 4: 0, 9, 8, 7, 6
        case SDLK_0: press(4, 0); break;
        case SDLK_9: press(4, 1); break;
        case SDLK_8: press(4, 2); break;
        case SDLK_7: press(4, 3); break;
        case SDLK_6: press(4, 4); break;

        // Row 5: P, O, I, U, Y
        case SDLK_p: press(5, 0); break;
        case SDLK_o: press(5, 1); break;
        case SDLK_i: press(5, 2); break;
        case SDLK_u: press(5, 3); break;
        case SDLK_y: press(5, 4); break;

        // Row 6: Enter, L, K, J, H
        case SDLK_RETURN:
        case SDLK_KP_ENTER: press(6, 0); break;
        case SDLK_l: press(6, 1); break;
        case SDLK_k: press(6, 2); break;
        case SDLK_j: press(6, 3); break;
        case SDLK_h: press(6, 4); break;

        // Row 7: Space, Symbol Shift (Ctrl/Alt), M, N, B
        case SDLK_SPACE: press(7, 0); break;
        case SDLK_LCTRL:
        case SDLK_RCTRL:
        case SDLK_LALT:  press(7, 1); break;
        case SDLK_m: press(7, 2); break;
        case SDLK_n: press(7, 3); break;
        case SDLK_b: press(7, 4); break;

        // Helper backspace -> Shift + 0
        case SDLK_BACKSPACE:
            press(0, 0); // Shift
            press(4, 0); // 0
            break;

        default: break;
    }
}

void ZXSpectrumMachine::handleKeyUp(SDL_Keycode key, uint16 mod) {
    auto release = [this](int row, int bit) {
        keyMatrix[row] |= (1 << bit);
    };

    switch (key) {
        case SDLK_LSHIFT:
        case SDLK_RSHIFT: release(0, 0); break;
        case SDLK_z: release(0, 1); break;
        case SDLK_x: release(0, 2); break;
        case SDLK_c: release(0, 3); break;
        case SDLK_v: release(0, 4); break;

        case SDLK_a: release(1, 0); break;
        case SDLK_s: release(1, 1); break;
        case SDLK_d: release(1, 2); break;
        case SDLK_f: release(1, 3); break;
        case SDLK_g: release(1, 4); break;

        case SDLK_q: release(2, 0); break;
        case SDLK_w: release(2, 1); break;
        case SDLK_e: release(2, 2); break;
        case SDLK_r: release(2, 3); break;
        case SDLK_t: release(2, 4); break;

        case SDLK_1: release(3, 0); break;
        case SDLK_2: release(3, 1); break;
        case SDLK_3: release(3, 2); break;
        case SDLK_4: release(3, 3); break;
        case SDLK_5: release(3, 4); break;

        case SDLK_0: release(4, 0); break;
        case SDLK_9: release(4, 1); break;
        case SDLK_8: release(4, 2); break;
        case SDLK_7: release(4, 3); break;
        case SDLK_6: release(4, 4); break;

        case SDLK_p: release(5, 0); break;
        case SDLK_o: release(5, 1); break;
        case SDLK_i: release(5, 2); break;
        case SDLK_u: release(5, 3); break;
        case SDLK_y: release(5, 4); break;

        case SDLK_RETURN:
        case SDLK_KP_ENTER: release(6, 0); break;
        case SDLK_l: release(6, 1); break;
        case SDLK_k: release(6, 2); break;
        case SDLK_j: release(6, 3); break;
        case SDLK_h: release(6, 4); break;

        case SDLK_SPACE: release(7, 0); break;
        case SDLK_LCTRL:
        case SDLK_RCTRL:
        case SDLK_LALT:  release(7, 1); break;
        case SDLK_m: release(7, 2); break;
        case SDLK_n: release(7, 3); break;
        case SDLK_b: release(7, 4); break;

        case SDLK_BACKSPACE:
            release(0, 0);
            release(4, 0);
            break;

        default: break;
    }
}

bool ZXSpectrumMachine::loadMedia(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    std::vector<uint8> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    if (path.length() >= 4 && path.substr(path.length() - 4) == ".sna") {
        return loadSNA(data);
    }
    if (path.length() >= 4 && path.substr(path.length() - 4) == ".tap") {
        return loadTAP(data);
    }
    return false;
}

bool ZXSpectrumMachine::loadSNA(const std::vector<uint8>& data) {
    if (data.size() < 49179) return false;

    // SNA 48K header (27 bytes):
    cpu.I = data[0];
    cpu.HL_.b.l = data[1]; cpu.HL_.b.h = data[2];
    cpu.DE_.b.l = data[3]; cpu.DE_.b.h = data[4];
    cpu.BC_.b.l = data[5]; cpu.BC_.b.h = data[6];
    cpu.AF_.b.l = data[7]; cpu.AF_.b.h = data[8];
    cpu.HL.b.l = data[9];  cpu.HL.b.h = data[10];
    cpu.DE.b.l = data[11]; cpu.DE.b.h = data[12];
    cpu.BC.b.l = data[13]; cpu.BC.b.h = data[14];
    cpu.IY.b.l = data[15]; cpu.IY.b.h = data[16];
    cpu.IX.b.l = data[17]; cpu.IX.b.h = data[18];
    cpu.iff2 = (data[19] & 0x04) != 0;
    cpu.iff1 = cpu.iff2;
    cpu.R = data[20];
    cpu.AF.b.l = data[21]; cpu.AF.b.h = data[22];
    cpu.SP = data[23] | ((uint16)data[24] << 8);
    cpu.im = data[25];
    borderColor = data[26] & 0x07;

    // 48K RAM from offset 27..49178 into 0x4000..0xFFFF
    std::memcpy(&memory[0x4000], &data[27], 49152);

    // Pop PC from stack
    uint8 pcl = memory[cpu.SP++];
    uint8 pch = memory[cpu.SP++];
    cpu.PC = pcl | ((uint16)pch << 8);

    currentMedia = "SNA Snapshot";
    std::cout << "[ZX Spectrum] Successfully loaded 48K SNA snapshot! PC=0x"
              << std::hex << cpu.PC << " SP=0x" << cpu.SP << std::dec << "\n";
    return true;
}

bool ZXSpectrumMachine::loadTAP(const std::vector<uint8>& data) {
    // TAP file loader: read blocks and inject executable payload into RAM
    size_t offset = 0;
    while (offset + 2 < data.size()) {
        uint16 len = data[offset] | ((uint16)data[offset + 1] << 8);
        offset += 2;
        if (offset + len > data.size()) break;

        uint8 flag = data[offset];
        if (flag == 0xFF && len > 1) { // Data block
            // Inject into Spectrum RAM loading buffer
            size_t copyLen = std::min((size_t)len - 1, (size_t)49152);
            std::memcpy(&memory[0x6000], &data[offset + 1], copyLen);
        }
        offset += len;
    }
    currentMedia = "TAP Tape";
    return true;
}
