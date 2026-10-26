#pragma once

#include "emulator_core.h"
#include "cpu_z80.h"
#include "sn76489.h"
#include <array>
#include <vector>
#include <string>
#include <map>

enum class TIMode {
    TI99_4A,    // Texas Instruments TI-99/4A Home Computer (TMS9900/TMS9918A)
    TI83_Plus   // Texas Instruments TI-83 Plus Graphing Calculator (Z80)
};

class TexasInstrumentsMachine : public EmulatorCore {
public:
    explicit TexasInstrumentsMachine(TIMode mode = TIMode::TI99_4A);
    ~TexasInstrumentsMachine() override = default;

    SystemType getSystemType() const override { return SystemType::TexasInstruments; }
    std::string getSystemName() const override;
    std::string getMediaName() const override {
        return currentMedia.empty() ? (currentMode == TIMode::TI99_4A ? "Embedded TI-99/4A System & BASIC" : "Embedded TI-83+ Flash OS") : currentMedia;
    }

    void init() override;
    void reset() override;
    void runFrame() override;

    const uint32* getRenderedScreen() const override { return screenBuffer.data(); }
    const int16* getAudioBuffer() const override { return audioBuffer.data(); }
    size_t getAudioSampleCount() const override { return audioBuffer.size(); }

    void handleKeyDown(SDL_Keycode key, uint16 mod) override;
    void handleKeyUp(SDL_Keycode key, uint16 mod) override;

    bool loadMedia(const std::string& path) override;
    void setMode(TIMode mode);

private:
    TIMode currentMode = TIMode::TI99_4A;

    // --- TI-99/4A Architecture ---
    [[maybe_unused]] uint16 WP = 0x8300;
    [[maybe_unused]] uint16 PC = 0x0000;
    [[maybe_unused]] uint16 ST = 0x0000;
    std::array<uint8, 256> scratchpad; // 256 bytes fast RAM (0x8300-0x83FF)
    std::array<uint8, 8192> systemRom; // 8KB System ROM
    std::array<uint8, 18432> grom;     // 18KB System GROM (0, 1, 2)
    std::array<uint8, 16384> vdpRam;   // 16KB VDP Memory (TMS9918A)
    SN76489 soundChip;
    int soundTimer = 0;
    uint8 screenBgColor = 7; // Cyan

    // TI-99 State & Program Memory
    int ti99State = 0; // 0: Title splash, 1: Selection menu, 2: Interactive TI BASIC
    std::vector<std::string> basicLines;
    std::string currentInput = "";
    int cursorBlink = 0;
    int animTick = 0;

    std::map<int, std::string> storedProgram;
    std::map<std::string, double> numVars;
    std::map<std::string, std::string> strVars;

    // --- TI-83 Plus Architecture ---
    CPUZ80 cpuZ80;
    std::array<uint8, 32768> calcRam; // 32KB RAM
    std::vector<uint8> calcFlash;     // 512KB Flash ROM
    std::array<uint8, 768> calcLcd;   // 96x64 monochrome LCD (96*64/8 = 768 bytes)
    std::string calcExpression = "";
    std::string calcAnswer = "";
    bool calcGraphMode = false;

    // Screen and Audio Buffers
    std::vector<uint32> screenBuffer;
    std::vector<int16> audioBuffer;
    std::string currentMedia = "";

    void playTone(float freqHz, int durationMs, int volume = 0);
    void renderTI99Screen();
    void renderTI83Screen();
    void updateAudio();

    void executeBasicCommandLine(const std::string& line);
    void executeStatement(const std::string& stmt, bool& shouldStop, int& jumpToLine);
    void executeProgram();
    double evaluateNumericExpression(const std::string& expr);
    void evaluateCalcExpression();
};
