#pragma once

#include "emulator_core.h"
#include "cpu_z80.h"
#include <vector>
#include <array>
#include <string>
#include <map>

class EnterpriseMachine : public EmulatorCore {
public:
    EnterpriseMachine();
    ~EnterpriseMachine() override = default;

    SystemType getSystemType() const override { return SystemType::Enterprise; }
    std::string getSystemName() const override { return "Enterprise 64/128 (EXOS)"; }
    std::string getMediaName() const override { return currentMedia.empty() ? "Embedded EXOS 2.1 & BASIC" : currentMedia; }

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
    std::array<uint8, 32768> exosRom;
    std::array<uint8, 16384> basicRom;
    std::array<uint8, 65536> ram;
    uint8 segmentRegs[4]; // 4 16KB paging segments (ports 0xB0..0xB3)

    std::vector<uint32> screenBuffer;
    std::vector<int16> audioBuffer;
    std::string currentMedia = "";

    uint8 keyMatrix[10];
    uint8 activeKeyboardRow = 0;
    uint8 daveRegs[16];
    int soundTimer = 0;

    // IS-BASIC Environment
    std::vector<std::string> basicLines;
    std::string currentInput = "";
    int cursorBlink = 0;
    int animTick = 0;
    std::map<int, std::string> storedProgram;
    std::map<std::string, double> numVars;

    uint8 memRead(uint16 addr);
    void memWrite(uint16 addr, uint8 val);
    uint8 ioRead(uint16 port);
    void ioWrite(uint16 port, uint8 val);

    void renderNickDisplay();
    void updateDaveAudio();
    void executeBasicLine(const std::string& line);
    void executeProgram();
    void executeStatement(const std::string& stmt, bool& shouldStop, int& jumpToLine);
    double evaluateExpr(const std::string& expr);
    void playDaveTone(int duration);
};
