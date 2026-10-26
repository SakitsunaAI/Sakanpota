#pragma once

#include "types.h"
#include "emulator_core.h"
#include "dos_cpu8086.h"
#include <vector>
#include <string>
#include <deque>

class DosMachine : public EmulatorCore {
public:
    CPU8086 cpu;

    // 1 MB Memory (640 KB RAM + Video RAM + BIOS ROM)
    std::vector<uint8> ram;

    // Framebuffer 640x512 ARGB8888
    std::vector<uint32> framebuffer;

    // Audio buffer
    static constexpr int SAMPLE_RATE = 44100;
    static constexpr int SAMPLES_PER_FRAME = SAMPLE_RATE / 60; // 60 Hz frame
    std::vector<int16> audioBuffer;

    // Video Mode: 0x03 (80x25 Text), 0x13 (320x200 256-color)
    uint8 videoMode = 0x03;
    uint8 cursorX = 0;
    uint8 cursorY = 0;
    uint8 currentAttr = 0x07; // Light gray on black

    // VGA Palette for 256-color and 16-color CGA/VGA
    std::array<uint32, 256> vgaPalette{};

    // Keyboard buffer
    std::deque<uint16> keyBuffer;

    // Active loaded program/media info
    std::string loadedMediaTitle = "MS-DOS 6.22";
    std::string currentDir = "C:\\";

    // Interactive Shell State
    std::string inputLine = "";
    bool inShell = true;

    DosMachine();

    SystemType getSystemType() const override { return SystemType::DosPC; }
    std::string getSystemName() const override { return "IBM PC / MS-DOS 6.22"; }
    std::string getMediaName() const override { return loadedMediaTitle; }

    void init() override;
    void reset() override;
    void runFrame() override;

    const uint32* getRenderedScreen() const override { return framebuffer.data(); }
    const int16* getAudioBuffer() const override { return audioBuffer.data(); }
    size_t getAudioSampleCount() const override { return audioBuffer.size(); }

    void handleKeyDown(SDL_Keycode key, uint16 mod) override;
    void handleKeyUp(SDL_Keycode key, uint16 mod) override;

    bool loadMedia(const std::string& path) override;

    // High Level Interrupt Handlers
    bool handleInterrupt(uint8 intNum);
    void handleInt10(); // Video BIOS
    void handleInt21(); // MS-DOS Services
    void handleInt16(); // Keyboard BIOS
    void handleInt1A(); // Time BIOS

    // Text output primitives
    void printChar(char c);
    void printString(const std::string& str);
    void scrollUp();
    void clearScreen();

    // Shell execution
    void executeCommand(const std::string& cmd);

    // Binary loading
    bool loadCOM(const std::vector<uint8>& data, const std::string& filename);
    bool loadEXE(const std::vector<uint8>& data, const std::string& filename);
    bool loadFloppyImage(const std::vector<uint8>& data, const std::string& filename);

private:
    void initPalette();
    void renderScreen();
    void renderTextMode();
    void renderMode13h();
};
