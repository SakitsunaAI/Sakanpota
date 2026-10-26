#pragma once

#include "emulator_core.h"
#include "cpu6502.h"
#include <vector>
#include <array>
#include <string>
#include <map>

class OricMachine : public EmulatorCore {
public:
    OricMachine();
    ~OricMachine() override = default;

    SystemType getSystemType() const override { return SystemType::Oric; }
    std::string getSystemName() const override { return "Oric Atmos (6502)"; }
    std::string getMediaName() const override { return currentMedia.empty() ? "Embedded Oric BASIC v1.1" : currentMedia; }

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
    std::array<uint8, 49152> ram; // 48KB RAM at 0x0000..0xBFFF
    std::array<uint8, 16384> rom; // 16KB BASIC ROM at 0xC000..0xFFFF

    std::vector<uint32> screenBuffer;
    std::vector<int16> audioBuffer;

    uint8 keyMatrix[8]; // Oric 8 keyboard rows
    uint8 viaRegs[16];  // VIA 6522 registers at 0x0300
    uint8 ayRegs[16];   // AY-3-8912 PSG sound registers
    uint8 activeAyReg = 0;
    int soundTimer = 0;
    int soundType = 0;
    std::string currentMedia = "";

    // Oric BASIC Environment
    std::vector<std::string> basicLines;
    std::string currentInput = "";
    int cursorBlink = 0;
    int animTick = 0;
    std::map<int, std::string> storedProgram;
    std::map<std::string, double> numVars;

    uint8 memRead(uint16 addr);
    void memWrite(uint16 addr, uint8 val);

    void renderOricScreen();
    void updateOricAudio();
    void executeBasicLine(const std::string& line);
    void executeStatement(const std::string& stmt, bool& shouldStop, int& jumpToLine);
    void executeProgram();
    double evaluateExpr(const std::string& expr);
    void playAyEffect(int type, int duration);
};
