#pragma once

#include "emulator_core.h"
#include "cpu_z80.h"
#include <array>
#include <vector>
#include <string>

class ZXSpectrumMachine : public EmulatorCore {
public:
    ZXSpectrumMachine();
    ~ZXSpectrumMachine() override = default;

    SystemType getSystemType() const override { return SystemType::ZXSpectrum; }
    std::string getSystemName() const override { return "Sinclair ZX Spectrum 48K"; }
    std::string getMediaName() const override { return currentMedia.empty() ? "Embedded Sinclair BASIC" : currentMedia; }

    void init() override;
    void reset() override;
    void runFrame() override;

    const uint32* getRenderedScreen() const override { return screenBuffer.data(); }
    const int16* getAudioBuffer() const override { return audioBuffer.data(); }
    size_t getAudioSampleCount() const override { return audioBuffer.size(); }

    void handleKeyDown(SDL_Keycode key, uint16 mod) override;
    void handleKeyUp(SDL_Keycode key, uint16 mod) override;

    bool loadMedia(const std::string& path) override;
    const uint8* getMemory() const { return memory.data(); }

private:
    CPUZ80 cpu;
    std::array<uint8, 65536> memory;
    std::vector<uint32> screenBuffer;
    std::vector<int16> audioBuffer;

    uint8 keyMatrix[8]; // 8 keyboard half-rows
    uint8 borderColor = 7; // Default white border
    bool beeperState = false;
    int flashFrameCounter = 0;
    bool flashInvert = false;
    std::string currentMedia = "";

    void renderScreen();
    void updateAudio();
    bool loadSNA(const std::vector<uint8>& data);
    bool loadTAP(const std::vector<uint8>& data);
};
