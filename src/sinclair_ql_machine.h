#pragma once

#include "emulator_core.h"
#include "mac_cpu68000.h"
#include <vector>
#include <array>
#include <string>
#include <map>

class SinclairQLMachine : public EmulatorCore {
public:
    SinclairQLMachine();
    ~SinclairQLMachine() override = default;

    SystemType getSystemType() const override { return SystemType::SinclairQL; }
    std::string getSystemName() const override { return "Sinclair QL (68008 QDOS)"; }
    std::string getMediaName() const override { return currentMedia.empty() ? "Embedded QDOS & SuperBASIC" : currentMedia; }

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
    CPU68000 cpu;
    std::array<uint8, 65536> qdosRom;
    std::array<uint8, 1024 * 1024> ram;

    uint8 displayControl = 0;
    uint8 ipcKeyMatrix[8];
    bool ipcBeeper = false;
    int soundTimer = 0;

    std::vector<uint32> screenBuffer;
    std::vector<int16> audioBuffer;
    std::string currentMedia = "";

    // SuperBASIC Environment
    std::vector<std::string> basicLines;
    std::string currentInput = "";
    int cursorBlink = 0;
    int animTick = 0;
    std::map<int, std::string> storedProgram;
    std::map<std::string, double> numVars;

    uint8 memRead8(uint32 addr);
    void memWrite8(uint32 addr, uint8 val);
    uint16 memRead16(uint32 addr);
    void memWrite16(uint32 addr, uint16 val);
    uint32 memRead32(uint32 addr);
    void memWrite32(uint32 addr, uint32 val);

    void renderQLScreen();
    void updateQLAudio();
    void executeBasicLine(const std::string& line);
    void executeStatement(const std::string& stmt, bool& shouldStop, int& jumpToLine);
    void executeProgram();
    double evaluateExpr(const std::string& expr);
    void playIpcTone(int duration);
};
