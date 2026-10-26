#pragma once

#include "types.h"
#include "emulator_core.h"
#include "pk201/psemu/psemu.h"
#include <vector>
#include <string>

enum class PocketGame {
    ChocoboWorld = 0,
    DokodemoIssho,
    CrashBandicoot,
    MegaMan,
    Spyro,
    SpaceInvaders,
    PocketDungeon
};

class PocketStationMachine : public EmulatorCore {
public:
    static constexpr int CANVAS_WIDTH = 640;
    static constexpr int CANVAS_HEIGHT = 512;
    static constexpr int LCD_SIZE = 32;

    PocketStationMachine();
    ~PocketStationMachine() override;

    SystemType getSystemType() const override { return SystemType::PocketStation; }
    std::string getSystemName() const override { return "Sony PocketStation (PK201 Core)"; }
    std::string getMediaName() const override { return loadedMedia; }

    bool loadGameByName(const std::string& name);
    void cycleNextGame();
    PocketGame getCurrentGame() const { return currentGame; }
    bool loadMedia(const std::string& path) override { return loadGameByName(path); }

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

private:
    psemu_t* ps;

    // 32x32 LCD Framebuffer
    bool lcdPixels[LCD_SIZE][LCD_SIZE];

    // Framebuffer for main emulator window (640x512)
    std::vector<uint32> rawFramebuffer;

    // Audio buffer (882 samples mono)
    std::vector<int16> audioBuffer;

    // Control buttons state
    uint32_t buttonMask;

    // Active PocketStation Game
    PocketGame currentGame;
    std::string loadedMedia;
    int frameCounter;

    void updateLCD();
    void renderPocketStation();
    void updateAudio();
};
