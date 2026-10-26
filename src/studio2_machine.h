#pragma once

#include "emulator_core.h"
#include <array>
#include <vector>
#include <string>

class Studio2Machine : public EmulatorCore {
public:
    Studio2Machine();
    ~Studio2Machine() override = default;

    SystemType getSystemType() const override { return SystemType::RCAStudio2; }
    std::string getSystemName() const override { return "RCA Studio II (CDP1802 COSMAC)"; }
    std::string getMediaName() const override { return currentMedia; }

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
    // 4KB Address Space ($0000..$0FFF)
    // $0000..$07FF: 2KB System ROM (5 Built-in games: Doodle, Patterns, Bowling, Space War, Freeway)
    // $0800..$09FF: 512 bytes RAM ($0900..$09FF is CDP1861 Pixie 64x32 display buffer)
    // $0400..$07FF / $0A00..$0FFF: Cartridge ROM
    std::array<uint8, 4096> memory{};
    std::vector<uint8> cartData;

    // RCA CDP1802 COSMAC CPU Registers
    std::array<uint16, 16> R{};
    uint8 D = 0;
    uint8 DF = 0;
    uint8 P = 0;
    uint8 X = 0;
    uint8 T = 0;
    uint8 Q = 0;
    uint8 IE = 1;

    // Hardware State
    bool screenEnabled = true;
    uint8 keyboardLatch = 0;
    bool keyStateP1[16]{};
    bool keyStateP2[16]{};
    bool clearPressed = false;

    // Audio
    int tonePhase = 0;
    std::vector<int16> audioBuffer;

    // Screen buffer (640x512 ARGB8888)
    std::vector<uint32> screenBuffer;
    std::string currentMedia = "RCA Studio II (Built-in Games: Bowling, Space War, Doodle, Freeway, Patterns)";

    uint8 readMem(uint16 addr);
    void writeMem(uint16 addr, uint8 val);
    void executeInstruction();
    void renderPixieDisplay();
    void updateAudio();
};
