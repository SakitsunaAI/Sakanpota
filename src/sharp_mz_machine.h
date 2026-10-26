#pragma once

#include "emulator_core.h"
#include "cpu_z80.h"
#include <vector>
#include <array>
#include <string>
#include <map>

class SharpMZMachine : public EmulatorCore {
public:
    SharpMZMachine();
    ~SharpMZMachine() override = default;

    SystemType getSystemType() const override { return SystemType::SharpMZ; }
    std::string getSystemName() const override { return "Sharp MZ-700 (Clean Computer)"; }
    std::string getMediaName() const override { return currentMedia.empty() ? "Embedded 1Z-013A Monitor & BASIC" : currentMedia; }

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
    std::array<uint8, 4096> monitorRom;
    std::array<uint8, 4096> fontRom;

    bool romPagingEnabled = true;
    std::vector<uint32> screenBuffer;
    std::vector<int16> audioBuffer;

    uint8 keyMatrix[10];
    uint8 ppiStrobe = 0;
    bool speakerOut = false;
    int soundTimer = 0;
    std::string currentMedia = "";

    // Sharp MZ Interactive Monitor & BASIC
    bool basicMode = false;
    std::vector<std::string> consoleLines;
    std::string currentInput = "";
    int cursorBlink = 0;
    int animTick = 0;
    std::map<int, std::string> storedProgram;
    std::map<std::string, double> numVars;

    uint8 memRead(uint16 addr);
    void memWrite(uint16 addr, uint8 val);
    uint8 ioRead(uint16 port);
    void ioWrite(uint16 port, uint8 val);

    void renderMZDisplay();
    void updateMZAudio();
    void executeCommandLine(const std::string& line);
    void executeStatement(const std::string& stmt, bool& shouldStop, int& jumpToLine);
    void executeProgram();
    double evaluateExpr(const std::string& expr);
    void playClick(int duration);
};
