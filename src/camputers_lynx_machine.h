#pragma once

#include "emulator_core.h"
#include "cpu_z80.h"
#include <vector>
#include <array>
#include <string>
#include <map>

class CamputersLynxMachine : public EmulatorCore {
public:
    CamputersLynxMachine();
    ~CamputersLynxMachine() override = default;

    SystemType getSystemType() const override { return SystemType::CamputersLynx; }
    std::string getSystemName() const override { return "Camputers Lynx 48K/96K"; }
    std::string getMediaName() const override { return currentMedia.empty() ? "Embedded Lynx 48K System ROM" : currentMedia; }

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
    std::array<uint8, 16384> rom;
    bool romEnabled = true;

    std::vector<uint32> screenBuffer;
    std::vector<int16> audioBuffer;

    uint8 keyMatrix[10];
    uint8 soundDac = 0;
    int soundTimer = 0;
    std::string currentMedia = "";

    // Camputers Lynx BASIC Environment
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

    void renderLynxDisplay();
    void updateAudio();
    void executeBasicLine(const std::string& line);
    void executeStatement(const std::string& stmt, bool& shouldStop, int& jumpToLine);
    void executeProgram();
    double evaluateExpr(const std::string& expr);
    void playTone(int duration);
};
