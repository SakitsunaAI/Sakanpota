#include "supervision_machine.h"
#include "embedded_roms.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <cmath>

static constexpr int CANVAS_W = 640;
static constexpr int CANVAS_H = 512;

// Watara Supervision Olive LCD Palette
static const uint32 supervisionPalette[4] = {
    0xFFDFE8C0, // 0: Lightest Olive
    0xFF9EB478, // 1: Light-Medium Olive
    0xFF546C3C, // 2: Medium-Dark Olive
    0xFF182410  // 3: Dark LCD Crystal
};

WataraSupervisionMachine::WataraSupervisionMachine() {
    screenBuffer.resize(CANVAS_W * CANVAS_H, 0xFF1C2028);
    audioBuffer.resize(882, 0);

    // Default load embedded Crystball game
    cartRom.assign(EmbeddedROMs::SUPERVISION_CRYSTBALL,
                   EmbeddedROMs::SUPERVISION_CRYSTBALL + sizeof(EmbeddedROMs::SUPERVISION_CRYSTBALL));
}

void WataraSupervisionMachine::init() {
    ram.fill(0);
    vram.fill(0);
    bankSelect = 0;
    joypadState = 0xFF; // Active low: 1 = not pressed

    cpu.is65C02 = true;
    cpu.readMem = [this](uint16 addr) -> uint8 { return readMemory(addr); };
    cpu.writeMem = [this](uint16 addr, uint8 val) { writeMemory(addr, val); };

    reset();
}

void WataraSupervisionMachine::reset() {
    ram.fill(0);
    vram.fill(0);
    bankSelect = 0;
    joypadState = 0xFF;
    cpu.reset();
}

uint8 WataraSupervisionMachine::readMemory(uint16 addr) {
    if (addr < 0x2000) {
        return ram[addr & 0x1FFF];
    }
    if (addr >= 0x2000 && addr <= 0x2027) {
        if (addr == 0x2023) return timerCount;
        if (addr == 0x2027) return joypadState;
        return 0x00;
    }
    if (addr >= 0x4000 && addr < 0x6000) {
        return vram[addr - 0x4000];
    }
    if (addr >= 0x8000 && addr < 0xC000) {
        // Banked Cartridge ROM (16KB)
        if (cartRom.empty()) return 0xFF;
        size_t offset = (bankSelect * 0x4000 + (addr - 0x8000)) % cartRom.size();
        return cartRom[offset];
    }
    if (addr >= 0xC000) {
        // Fixed Upper Cartridge ROM (Last 16KB containing vectors)
        if (cartRom.empty()) return 0xFF;
        size_t base = (cartRom.size() >= 0x4000) ? (cartRom.size() - 0x4000) : 0;
        size_t offset = (base + (addr - 0xC000)) % cartRom.size();
        return cartRom[offset];
    }
    return 0xFF;
}

void WataraSupervisionMachine::writeMemory(uint16 addr, uint8 val) {
    if (addr < 0x2000) {
        ram[addr & 0x1FFF] = val;
        return;
    }
    if (addr >= 0x2000 && addr <= 0x2027) {
        switch (addr) {
            case 0x2000: sound1Low = val; break;
            case 0x2001: sound1High = val; break;
            case 0x2002: sound2Low = val; break;
            case 0x2003: sound2High = val; break;
            case 0x2004: soundVol1 = val; break;
            case 0x2005: soundVol2 = val; break;
            case 0x2006: noiseControl = val; break;
            case 0x2020:
            case 0x2026:
                bankSelect = val & 0x0F;
                break;
            case 0x2023:
                timerPeriod = val;
                timerCount = val;
                break;
            case 0x2024:
                irqControl = val;
                break;
        }
        return;
    }
    if (addr >= 0x4000 && addr < 0x6000) {
        vram[addr - 0x4000] = val;
        return;
    }
    if (addr >= 0x8000) {
        // Cartridge bank select write
        bankSelect = val & 0x0F;
    }
}

void WataraSupervisionMachine::runFrame() {
    // 4 MHz CPU clock @ 60 FPS = ~66,666 cycles
    int cycles = 0;
    while (cycles < 66666) {
        cycles += cpu.step();
    }

    // Trigger VBlank NMI / IRQ
    cpu.triggerNMI();

    renderSupervisionScreen();
    updateAudio();
}

void WataraSupervisionMachine::renderSupervisionScreen() {
    // Clear outer background (Deep metallic handheld shell)
    std::fill(screenBuffer.begin(), screenBuffer.end(), 0xFF222830);

    // Draw Handheld Casing & Hinge (Authentic Supervision hinge styling)
    const int caseX = 90;
    const int caseY = 16;
    const int caseW = 460;
    const int caseH = 480;

    for (int y = caseY; y < caseY + caseH; y++) {
        for (int x = caseX; x < caseX + caseW; x++) {
            uint32 caseColor = 0xFF3A424E;
            // Rounded corners and edge highlights
            if (x == caseX || x == caseX + caseW - 1 || y == caseY || y == caseY + caseH - 1) {
                caseColor = 0xFF141820;
            } else if (y < caseY + 4 || x < caseX + 4) {
                caseColor = 0xFF4A5260; // Top/left bezel highlight
            }
            screenBuffer[y * CANVAS_W + x] = caseColor;
        }
    }

    // Draw Dark Gray Bezel surrounding LCD (160x160 scaled 2.5x -> 400x400)
    const int lcdX = 120;
    const int lcdY = 40;
    const int lcdW = 400;
    const int lcdH = 400;

    for (int y = lcdY - 12; y < lcdY + lcdH + 12; y++) {
        for (int x = lcdX - 12; x < lcdX + lcdW + 12; x++) {
            screenBuffer[y * CANVAS_W + x] = 0xFF1A1E24;
        }
    }

    // Render 160x160 4-shade LCD from VRAM
    // Each row has 40 bytes (160 pixels / 4 pixels per byte)
    for (int y = 0; y < 160; y++) {
        const uint8* rowPtr = &vram[y * 40];
        for (int b = 0; b < 40; b++) {
            uint8 byteVal = rowPtr[b];
            for (int p = 0; p < 4; p++) {
                int shift = (3 - p) * 2;
                uint8 colorIdx = (byteVal >> shift) & 0x03;
                uint32 pixel = supervisionPalette[colorIdx];

                int px = lcdX + (b * 4 + p) * 2.5;
                int py = lcdY + y * 2.5;

                // 2x2 / 3x2 pixel scaling
                for (int dy = 0; dy < 2; dy++) {
                    for (int dx = 0; dx < 2; dx++) {
                        if (px + dx < CANVAS_W && py + dy < CANVAS_H) {
                            screenBuffer[(py + dy) * CANVAS_W + (px + dx)] = pixel;
                        }
                    }
                }
            }
        }
    }

    // Draw Power LED (Green illuminated)
    for (int dy = -3; dy <= 3; dy++) {
        for (int dx = -3; dx <= 3; dx++) {
            if (dx * dx + dy * dy <= 9) {
                screenBuffer[(lcdY + 10 + dy) * CANVAS_W + (lcdX - 25 + dx)] = 0xFF30E030;
            }
        }
    }

    // Decorative "SUPERVISION" Branding bar at bottom bezel
    for (int x = lcdX + 80; x < lcdX + 320; x++) {
        for (int y = lcdY + lcdH + 18; y < lcdY + lcdH + 22; y++) {
            screenBuffer[y * CANVAS_W + x] = 0xFFE04040; // Red accent stripe
        }
    }
}

void WataraSupervisionMachine::updateAudio() {
    uint16 freq1 = sound1Low | ((sound1High & 0x07) << 8);
    uint16 freq2 = sound2Low | ((sound2High & 0x07) << 8);

    float f1 = (freq1 > 10) ? (4000000.0f / (32.0f * (freq1 + 1))) : 0.0f;
    float f2 = (freq2 > 10) ? (4000000.0f / (32.0f * (freq2 + 1))) : 0.0f;

    static float phase1 = 0.0f;
    static float phase2 = 0.0f;

    for (size_t i = 0; i < audioBuffer.size(); i++) {
        int16 sample = 0;
        if (f1 > 20.0f && f1 < 15000.0f && (soundVol1 & 0x0F) > 0) {
            phase1 += f1 / 44100.0f;
            if (phase1 >= 1.0f) phase1 -= 1.0f;
            sample += (phase1 < 0.5f) ? 2000 : -2000;
        }
        if (f2 > 20.0f && f2 < 15000.0f && (soundVol2 & 0x0F) > 0) {
            phase2 += f2 / 44100.0f;
            if (phase2 >= 1.0f) phase2 -= 1.0f;
            sample += (phase2 < 0.5f) ? 2000 : -2000;
        }
        audioBuffer[i] = sample;
    }
}

void WataraSupervisionMachine::handleKeyDown(SDL_Keycode key, uint16 mod) {
    if (key == SDLK_RIGHT)  joypadState &= ~(1 << 0);
    if (key == SDLK_LEFT)   joypadState &= ~(1 << 1);
    if (key == SDLK_DOWN)   joypadState &= ~(1 << 2);
    if (key == SDLK_UP)     joypadState &= ~(1 << 3);
    if (key == SDLK_x || key == SDLK_s)      joypadState &= ~(1 << 4); // B
    if (key == SDLK_z || key == SDLK_a)      joypadState &= ~(1 << 5); // A
    if (key == SDLK_RSHIFT || key == SDLK_TAB) joypadState &= ~(1 << 6); // Select
    if (key == SDLK_RETURN || key == SDLK_SPACE) joypadState &= ~(1 << 7); // Start
}

void WataraSupervisionMachine::handleKeyUp(SDL_Keycode key, uint16 mod) {
    if (key == SDLK_RIGHT)  joypadState |= (1 << 0);
    if (key == SDLK_LEFT)   joypadState |= (1 << 1);
    if (key == SDLK_DOWN)   joypadState |= (1 << 2);
    if (key == SDLK_UP)     joypadState |= (1 << 3);
    if (key == SDLK_x || key == SDLK_s)      joypadState |= (1 << 4);
    if (key == SDLK_z || key == SDLK_a)      joypadState |= (1 << 5);
    if (key == SDLK_RSHIFT || key == SDLK_TAB) joypadState |= (1 << 6);
    if (key == SDLK_RETURN || key == SDLK_SPACE) joypadState |= (1 << 7);
}

bool WataraSupervisionMachine::loadMedia(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;

    cartRom.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (cartRom.empty()) return false;

    currentMedia = path.substr(path.find_last_of("/\\") + 1);
    reset();
    std::cout << "[SUPERVISION] Loaded Cartridge: " << currentMedia << " (" << cartRom.size() << " bytes)\n";
    return true;
}
