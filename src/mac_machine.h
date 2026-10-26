#pragma once

#include "types.h"
#include "emulator_core.h"
#include "mac_cpu68000.h"
#include <vector>
#include <string>

class MacMachine : public EmulatorCore {
public:
    CPU68000 cpu;

    // 128 KB RAM ($000000..$01FFFF)
    std::vector<uint8> ram;

    // 64 KB ROM ($400000..$40FFFF)
    std::vector<uint8> rom;

    // Framebuffer 640x512 ARGB8888
    std::vector<uint32> framebuffer;

    // Audio buffer (44.1kHz, 60Hz frame)
    static constexpr int SAMPLE_RATE = 44100;
    static constexpr int SAMPLES_PER_FRAME = SAMPLE_RATE / 60;
    std::vector<int16> audioBuffer;

    // Mouse coordinates on 512x342 Mac screen
    int mouseX = 256;
    int mouseY = 171;
    bool mouseButton = false;

    // Desktop state
    std::string mediaTitle = "Macintosh System 1.0";
    bool diskInserted = false;
    int bootTimer = 0;
    bool showDesktop = false;

    MacMachine();

    SystemType getSystemType() const override { return SystemType::Macintosh128K; }
    std::string getSystemName() const override { return "Apple Macintosh 128K (68000)"; }
    std::string getMediaName() const override { return mediaTitle; }

    void init() override;
    void reset() override;
    void runFrame() override;

    const uint32* getRenderedScreen() const override { return framebuffer.data(); }
    const int16* getAudioBuffer() const override { return audioBuffer.data(); }
    size_t getAudioSampleCount() const override { return audioBuffer.size(); }

    void handleKeyDown(SDL_Keycode key, uint16 mod) override;
    void handleKeyUp(SDL_Keycode key, uint16 mod) override;
    void handleMouseMotion(int x, int y) override;
    void handleMouseButton(uint8 button, bool pressed, int x, int y) override;

    bool loadMedia(const std::string& path) override;

    // Video RAM address in Mac 128K RAM ($01A700)
    static constexpr uint32 VRAM_OFFSET = 0x01A700;
    static constexpr int MAC_WIDTH = 512;
    static constexpr int MAC_HEIGHT = 342;

private:
    void renderMacScreen();
    void drawBootScreen();
    void drawDesktop();
    void drawMousePointer(int x, int y);
    void setPixel(int x, int y, bool black);
};
