#pragma once

#include "types.h"
#include "emulator_core.h"
#include "newton_cpu_arm.h"
#include "newton_runt.h"
#include <vector>
#include <string>

struct NewtonInkPoint {
    int16 x, y;
};

struct NewtonInkStroke {
    std::vector<NewtonInkPoint> points;
};

struct NewtonNoteData {
    std::string title;
    std::vector<std::string> lines;
    std::vector<NewtonInkStroke> strokes;
};

class NewtonMachine : public EmulatorCore {
public:
    static constexpr int CANVAS_WIDTH = 640;
    static constexpr int CANVAS_HEIGHT = 512;
    static constexpr int LCD_X = 180 + 20; // 200
    static constexpr int LCD_Y = 16 + 34;  // 50

    NewtonMachine();
    ~NewtonMachine() override = default;

    SystemType getSystemType() const override { return SystemType::AppleNewton; }
    std::string getSystemName() const override { return "Apple Newton MessagePad (ARM610)"; }
    std::string getMediaName() const override { return runt.getLoadedROMName(); }

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
    void cycleROM();

private:
    NewtonRunt runt;
    NewtonCPUARM cpu;

    std::vector<uint32> rawFramebuffer;
    std::vector<int16> audioBuffer;

    bool mouseDown;
    int mouseX, mouseY;
    int currentROMIndex;
    uint64 frameCounter;

    // Interactive Newton Notepad & Text Engine
    std::vector<NewtonNoteData> notes;
    int currentNoteIdx;
    int cursorLine;
    int cursorCol;
    bool cursorBlink;
    int cursorBlinkTimer;
    int scrollY;

    // Stylus Digital Ink
    bool isStylusDrawing;
    int prevPenX, prevPenY;
    NewtonInkStroke activeStroke;

    // UI Dialogs & Modes
    bool showOverview;
    bool showKeyboard;
    bool showAssist;
    bool showFind;
    std::string findQuery;
    int activeSilkscreenBtn;
    int btnFlashFrames;
    char lastVirtualKeyPressed;
    int virtualKeyFlashFrames;

    // Tactile Audio Clicks
    int clickSamplesRemaining;
    double clickPhase;
    double clickFreq;

    void triggerClickSound(int freq = 1200, int lengthSamples = 160);
    void renderCasing();
    void renderNewtonScreen();
    void renderVirtualKeyboard();
    void renderOverviewScreen();
    void renderAssistDialog();
    void renderFindDialog();
    void updateAudio();

    void drawScreenThickLine(int x0, int y0, int x1, int y1, uint32 color);
    void handleVirtualKeyClick(int kx, int ky);
    void handleTypingChar(char c);
};

