#pragma once

#include "emulator_core.h"
#include <array>
#include <vector>
#include <string>

class Odyssey2Machine : public EmulatorCore {
public:
    Odyssey2Machine();
    ~Odyssey2Machine() override = default;

    SystemType getSystemType() const override { return SystemType::Odyssey2; }
    std::string getSystemName() const override { return "Magnavox Odyssey² / Videopac"; }
    std::string getMediaName() const override { return currentMedia.empty() ? "KC Munchkin!" : currentMedia; }

    void init() override;
    void reset() override;
    void runFrame() override;

    const uint32* getRenderedScreen() const override { return screenBuffer.data(); }
    const int16* getAudioBuffer() const override { return audioBuffer.data(); }
    size_t getAudioSampleCount() const override { return audioBuffer.size(); }

    void handleKeyDown(SDL_Keycode key, uint16 mod) override;
    void handleKeyUp(SDL_Keycode key, uint16 mod) override;

    bool loadMedia(const std::string& path) override;

private:
    // Intel 8048 Microcontroller Registers
    uint16 pc = 0;
    uint8 a = 0;
    uint8 psw = 0x08; // PSW: CY, AC, F0, BS, -, SP(3)
    uint8 r[8] = {0};
    uint8 r_bank[8] = {0};
    uint16 stack[8] = {0};
    uint8 internalRam[128] = {0};
    std::array<uint8, 4096> rom;
    bool f1 = false;
    uint8 p1 = 0xFF;
    uint8 p2 = 0xFF;
    uint8 bus = 0xFF;

    // Intel 8244 VDC Registers & VRAM
    uint8 vdcRegs[256];
    uint8 colorReg = 0;
    bool soundEnabled = false;
    uint8 soundTone = 0;

    // Controller state: Joystick 0 & 1, Membrane Keyboard
    uint8 joyState[2] = {0, 0};
    uint8 keyMatrix[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    // Framebuffer & Audio
    std::vector<uint32> screenBuffer;
    std::vector<int16> audioBuffer;
    std::string currentMedia = "KC Munchkin!";

    // Built-in KC Munchkin & Pick Axe Pete demo games state
    int currentGame = 0; // 0: KC Munchkin, 1: Pick Axe Pete, 2: Select Game
    int playerX = 80, playerY = 100;
    int playerDir = 0;
    int score = 0;
    int animTick = 0;
    int ghosts[3][3] = {{40, 60, 1}, {120, 60, 2}, {80, 140, 0}};

    void stepCPU();
    void renderScreen();
    void renderVDC();
    void updateAudio();
    void loadCartridge(int gameId);
};
