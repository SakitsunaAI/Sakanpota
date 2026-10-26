#pragma once

#include "emulator_core.h"
#include "cpu_z80.h"
#include <array>
#include <vector>
#include <string>

class AmstradCPCMachine : public EmulatorCore {
public:
    AmstradCPCMachine();
    ~AmstradCPCMachine() override = default;

    SystemType getSystemType() const override { return SystemType::AmstradCPC; }
    std::string getSystemName() const override { return "Amstrad CPC 464 (Locomotive BASIC)"; }
    std::string getMediaName() const override { return currentMedia.empty() ? "Embedded Locomotive BASIC" : currentMedia; }

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
    std::array<uint8, 65536> ram;
    std::vector<uint32> screenBuffer;
    std::vector<int16> audioBuffer;

    // Gate Array State
    bool lowerRomEnabled = true;
    bool upperRomEnabled = true;
    uint8 screenMode = 1; // Default Mode 1 (320x200 4 colors)
    uint8 selectedPen = 0;
    uint8 palettePens[16];
    uint8 borderPen = 0;

    // PPI 8255 & Keyboard Matrix
    uint8 keyMatrix[10]; // 10 rows x 8 columns
    uint8 ppiPortA = 0;
    uint8 ppiPortB = 0;
    uint8 ppiPortC = 0;
    uint8 ppiControl = 0;
    uint8 selectedKeyRow = 0;

    // CRTC 6845 Registers
    uint8 crtcIndex = 0;
    uint8 crtcRegs[18];

    // AY-3-8912 PSG Sound Emulation
    uint8 aySelectedReg = 0;
    uint8 ayRegs[16];
    uint32 ayTonePhase[3] = {0, 0, 0};
    bool ayToneOutput[3] = {false, false, false};

    std::string currentMedia = "";

    void renderScreen();
    void updateAudio();
    bool loadDSK(const std::vector<uint8>& data);
    void writeGateArray(uint8 val);
    void writePPI(uint16 port, uint8 val);
    uint8 readPPI(uint16 port);
    void writeAY(uint8 val);
    uint8 readAY();
};
