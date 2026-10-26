#pragma once

#include "emulator_core.h"
#include "cpu_z80.h"
#include <vector>
#include <array>
#include <string>
#include <map>

class JupiterAceMachine : public EmulatorCore {
public:
    JupiterAceMachine();
    ~JupiterAceMachine() override = default;

    SystemType getSystemType() const override { return SystemType::JupiterAce; }
    std::string getSystemName() const override { return "Jupiter Ace (Forth Z80)"; }
    std::string getMediaName() const override { return currentMedia.empty() ? "Embedded Jupiter Forth" : currentMedia; }

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
    CPUZ80 cpu;
    std::array<uint8, 65536> memory;
    std::vector<uint32> screenBuffer;
    std::vector<int16> audioBuffer;

    uint8 keyMatrix[8]; // 8 keyboard half-rows
    bool beeperState = false;
    int soundTimer = 0;
    std::string currentMedia = "";

    // Forth Environment
    std::vector<std::string> forthLines;
    std::string currentInput = "";
    int cursorBlink = 0;
    int animTick = 0;
    std::vector<int> forthStack;
    std::map<std::string, std::vector<std::string>> customWords;

    void renderScreen();
    void updateAudio();
    void executeForth(const std::string& line);
    void printForth(const std::string& text);
    void playBeep(int durationFrames);
};
