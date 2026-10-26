#pragma once
#include "types.h"
#include "emulator_core.h"
#include "mac_cpu68000.h"
#include "crt_filter.h"
#include <vector>
#include <string>

class LisaMachine : public EmulatorCore {
public:
    static constexpr int LISA_WIDTH = 720;
    static constexpr int LISA_HEIGHT = 364;
    static constexpr int CANVAS_WIDTH = 640;
    static constexpr int CANVAS_HEIGHT = 512;

    CPU68000 cpu;
    std::vector<uint8> ram;   // 1MB RAM
    std::vector<uint8> rom;   // 16KB Lisa Boot ROM

    // Framebuffer
    std::vector<uint32> rawFramebuffer; // 640x512 scaled canvas
    std::vector<uint32> lisaBitmap;     // 720x364 native monochrome

    // Audio buffer
    std::vector<int16> audioBuffer;

    // Mouse state
    int mouseX = 180;
    int mouseY = 140;
    bool mouseButton = false;

    // Media & disk name
    std::string loadedMedia = "Lisa Office System (Embedded)";

    // Lisa OS state
    int activeWindow = 1; // 1 = LisaCalc, 2 = LisaDraw, 0 = Desktop
    bool menuOpen = false;
    int selectedMenu = -1;
    int frameCount = 0;

    LisaMachine();

    SystemType getSystemType() const override { return SystemType::AppleLisa; }
    std::string getSystemName() const override { return "Apple Lisa 2/5 (Lisa OS)"; }
    std::string getMediaName() const override { return loadedMedia; }

    void init() override;
    void reset() override;
    void runFrame() override;

    const uint32* getRenderedScreen() const override { return rawFramebuffer.data(); }
    const int16* getAudioBuffer() const override { return audioBuffer.data(); }
    size_t getAudioSampleCount() const override { return audioBuffer.size(); }

    void handleKeyDown(SDL_Keycode key, uint16 mod) override;
    void handleKeyUp(SDL_Keycode key, uint16 mod) override;
    void handleMouseMotion(int x, int y) override;
    void handleMouseButton(uint8 button, bool pressed, int x, int y) override;

    bool loadMedia(const std::string& path) override;

private:
    void renderLisaOS();
    void scaleToCanvas();
    void drawLisaIcon(int x, int y, const char* name, int type);
    void drawLisaWindow(int x, int y, int w, int h, const char* title, int winId);
};
