#pragma once

#include "emulator_core.h"
#include "cpu_tms1100.h"
#include <vector>
#include <string>
#include <array>

class MicrovisionMachine : public EmulatorCore, public CPUTMS1100 {
public:
    MicrovisionMachine();
    ~MicrovisionMachine() override = default;

    SystemType getSystemType() const override { return SystemType::Microvision; }
    std::string getSystemName() const override { return "Milton Bradley Microvision (1979)"; }
    std::string getMediaName() const override { return currentMedia; }

    void init() override;
    void reset() override;
    void runFrame() override;

    const uint32* getRenderedScreen() const override { return screenBuffer.data(); }
    const int16* getAudioBuffer() const override { return audioBuffer.data(); }
    size_t getAudioSampleCount() const override { return audioBuffer.size(); }

    void handleKeyDown(SDL_Keycode key, uint16 mod) override;
    void handleKeyUp(SDL_Keycode key, uint16 mod) override;
    void handleMouseMotion(int x, int y) override;

    bool loadMedia(const std::string& path) override;
    void switchCartridge(int id);

    // CPUTMS1100 Hardware Callbacks
    void onWriteO(uint8 data) override;
    void onWriteR(int line, int level) override;
    uint8 onReadK() override;
    void onSyncFrame() override;

private:
    std::vector<uint32> screenBuffer;
    std::vector<int16> audioBuffer;

    // Hughes 0488 LCD Controller Emulation
    uint8 dataLine;
    uint8 latchPulse;
    uint8 notDataClock;
    uint8 addressedLatchCounter;
    uint8 addressedLatches[8];
    uint16 lcdRows[16];           // 16 rows x 16 bits
    uint8  lcdDecay[16][16];       // LCD phosphor persistence decay
    bool   lcdPolarity;

    // Rotary Knob (0..100) and RC charging circuit
    int paddleValue;              // 0..100
    int chargeLevel;              // R2 pin state
    long chargeEndTime;           // cycle timestamp when capacitor charges to logic 1
    bool rotaryEnabled;

    // Sound (R0 pin)
    int soundLevel;
    int soundToggleCount;
    int soundPhase;

    // Keypad (3 columns x 4 rows)
    bool keyState[3][4];          // [col 0..2][row 0..3]

    int currentCartridge;         // 0: Block Buster, 1: Bowling, 2: Connect Four, 3: Pinball
    std::string currentMedia;

    void standardProcessing();
    void transferHoldingLatches();
    void renderConsole();
    void updateAudio();
};
