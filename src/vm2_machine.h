#pragma once

#include "emulator_core.h"
#include "types.h"
#include <vector>
#include <array>
#include <string>

class DreamwareVM2Machine : public EmulatorCore {
public:
    static constexpr int CANVAS_W = 640;
    static constexpr int CANVAS_H = 512;

    DreamwareVM2Machine();
    ~DreamwareVM2Machine() override = default;

    SystemType getSystemType() const override { return SystemType::DreamwareVM2; }
    std::string getSystemName() const override { return "Dreamware Enterprises VM2"; }
    std::string getMediaName() const override;

    void init() override;
    void reset() override;
    void runFrame() override;

    const uint32* getRenderedScreen() const override { return screenBuffer.data(); }
    const int16* getAudioBuffer() const override { return audioBuffer.data(); }
    size_t getAudioSampleCount() const override { return audioBuffer.size(); }

    void handleKeyDown(SDL_Keycode key, uint16 mod) override;
    void handleKeyUp(SDL_Keycode key, uint16 mod) override;
    void handleMouseMotion(int x, int y) override;
    void handleMouseButton(uint8 button, bool pressed, int x, int y) override;

    bool loadMedia(const std::string& path) override;
    bool loadGameByName(const std::string& name);
    void cycleNextGame();
    void setVirtualPage(int page);

private:
    std::array<uint8, 65536> firmwareRom;
    std::vector<std::array<uint8, 131072>> virtualPages; // 16 Virtual Pages of 128KB
    int currentPage = 0; // 0..15

    // Sanyo Potato / ARM execution state
    uint16 PC = 0x0000;
    uint8 ACC = 0;
    uint8 B = 0;
    uint8 PSW = 0;

    // 48x32 OLED buffer + High-Res overlay
    uint8 oledBuffer[192];
    uint8 buttonState = 0xFF; // Active low
    int buzzerTimer = 0;
    int buzzerFreq = 1400;
    double buzzerPhase = 0.0;
    int animTick = 0;
    int currentApp = 0; // 0: Game, 1: Clock/System, 2: Page Manager
    int currentGameIndex = 0;

    // Minigame states
    int chaoX = 16;
    int chaoDir = 1;
    int chaoAction = 0;
    int chaoActionTimer = 0;
    int chaoHP = 100;
    int chaoHappiness = 98;

    int pintaY = 16;
    int pintaScroll = 0;
    int pintaAction = 0;

    int psFighterX = 12;
    int psEnemyX = 34;
    int psAction = 0;

    int sonicX = 14;
    int sonicAction = 0;
    int rings = 12;

    int spaceShipX = 22;
    int spaceAction = 0;

    // Mouse & UI
    int mouseX = 0;
    int mouseY = 0;
    bool mouseDown = false;
    int osdBannerTimer = 0;
    std::string osdBannerText = "";

    std::vector<uint32> screenBuffer;
    std::vector<int16> audioBuffer;

    void stepInstruction();
    void updateGameLogic();
    void renderVM2Screen();
    void renderOLEDMatrix();
    void renderCasing();
    void updateAudio();
    void playBuzzerTone(int freq, int durationTicks);

    void setOledPixel(int x, int y, bool on);
    bool getOledPixel(int x, int y) const;
    void drawOledSprite(int x, int y, int w, int h, const uint8* data);
    void drawOledString(int x, int y, const std::string& str);
};
