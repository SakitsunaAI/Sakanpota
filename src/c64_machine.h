#pragma once

#include "types.h"
#include "emulator_core.h"
#include "cpu6502.h"
#include <vector>
#include <string>
#include <array>

class C64Machine : public EmulatorCore {
public:
    CPU6502 cpu;

    // 64 KB Main RAM
    std::array<uint8, 65536> ram{};

    // Color RAM (1000 nibbles at $D800..$DBE7)
    std::array<uint8, 1024> colorRAM{};

    // VIC-II Registers ($D000..$D02E)
    std::array<uint8, 64> vicRegs{};

    // SID 6581 Registers ($D400..$D41C)
    std::array<uint8, 32> sidRegs{};

    // 6510 Processor Port ($0000..$0001)
    uint8 portDDR = 0x2F;
    uint8 portData = 0x37;

    // Framebuffer 640x512 ARGB8888
    std::vector<uint32> framebuffer;

    // Audio buffer
    static constexpr int SAMPLE_RATE = 44100;
    static constexpr int SAMPLES_PER_FRAME = SAMPLE_RATE / 50; // 50 Hz PAL
    std::vector<int16> audioBuffer;

    // 16 C64 Palette Colors
    std::array<uint32, 16> palette{};

    // State
    std::string mediaTitle = "Commodore 64 BASIC V2";
    int cursorX = 0;
    int cursorY = 0;
    std::string inputBuffer = "";
    int flashTimer = 0;
    bool flashState = true;

    C64Machine();

    SystemType getSystemType() const override { return SystemType::Commodore64; }
    std::string getSystemName() const override { return "Commodore 64 (PAL 6510)"; }
    std::string getMediaName() const override { return mediaTitle; }

    void init() override;
    void reset() override;
    void runFrame() override;

    const uint32* getRenderedScreen() const override { return framebuffer.data(); }
    const int16* getAudioBuffer() const override { return audioBuffer.data(); }
    size_t getAudioSampleCount() const override { return audioBuffer.size(); }

    void handleKeyDown(SDL_Keycode key, uint16 mod) override;
    void handleKeyUp(SDL_Keycode key, uint16 mod) override;

    bool loadMedia(const std::string& path) override;

    // C64 I/O and Bus Handlers
    uint8 readMem(uint16 addr);
    void  writeMem(uint16 addr, uint8 val);

    // Text & Video Helpers
    void printChar(char c, uint8 color = 14);
    void printString(const std::string& str, uint8 color = 14);
    void scrollUp();
    void clearScreen();

    // PRG & D64 loading
    bool loadPRG(const std::vector<uint8>& data, const std::string& filename);
    bool loadD64(const std::vector<uint8>& data, const std::string& filename);

private:
    void initPalette();
    void renderScreen();
};
