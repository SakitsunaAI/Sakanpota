#pragma once

#include "emulator_core.h"
#include "cpu6502.h"
#include <array>
#include <vector>
#include <string>

class WataraSupervisionMachine : public EmulatorCore {
public:
    WataraSupervisionMachine();
    ~WataraSupervisionMachine() override = default;

    SystemType getSystemType() const override { return SystemType::WataraSupervision; }
    std::string getSystemName() const override { return "Watara Supervision"; }
    std::string getMediaName() const override { return currentMedia.empty() ? "Embedded Crystball Launch Cartridge" : currentMedia; }

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
    CPU6502 cpu;

    // Memory
    std::array<uint8, 8192> ram;   // 8KB Work RAM (0x0000 - 0x1FFF)
    std::array<uint8, 8192> vram;  // 8KB Video RAM (0x4000 - 0x5FFF)
    std::vector<uint8> cartRom;    // 32KB - 128KB Cartridge ROM
    uint8 bankSelect = 0;          // Bank register at 0x2026 / 0x2020

    // Hardware Registers (0x2000 - 0x2027)
    uint8 sound1Low = 0, sound1High = 0;
    uint8 sound2Low = 0, sound2High = 0;
    uint8 soundVol1 = 0, soundVol2 = 0;
    uint8 noiseControl = 0;
    uint8 timerPeriod = 0;
    uint8 timerCount = 0;
    uint8 irqControl = 0;
    uint8 joypadState = 0xFF;      // Active low: Right, Left, Down, Up, B, A, Select, Start

    // Screen buffer
    std::vector<uint32> screenBuffer;
    std::vector<int16> audioBuffer;
    std::string currentMedia = "";

    // Internal bus helpers
    uint8 readMemory(uint16 addr);
    void writeMemory(uint16 addr, uint8 val);

    void renderSupervisionScreen();
    void updateAudio();
};
