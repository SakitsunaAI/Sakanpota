#pragma once

#include "emulator_core.h"
#include "types.h"
#include <vector>
#include <array>
#include <string>

class VMUProMachine : public EmulatorCore {
public:
    static constexpr int CANVAS_W = 640;
    static constexpr int CANVAS_H = 512;
    static constexpr int IPS_W = 240;
    static constexpr int IPS_H = 160;

    VMUProMachine();
    ~VMUProMachine() override = default;

    SystemType getSystemType() const override { return SystemType::VMUPro; }
    std::string getSystemName() const override { return "8BitMods VMU Pro"; }
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
    void setBank(int bank);

private:
    std::array<uint8, 65536> firmwareRom;
    std::vector<std::array<uint8, 131072>> virtualBanks; // 128 Banks of 128KB Flash
    int currentBank = 0; // 0..127

    // Processor execution registers
    uint16 PC = 0x0000;
    uint8 ACC = 0;
    uint8 B = 0;
    uint8 PSW = 0;

    // High-Resolution Full-Color IPS Display Buffer (240x160 32-bit ARGB)
    std::vector<uint32> ipsBuffer;

    uint8 buttonState = 0xFF; // Active low
    int buzzerTimer = 0;
    int buzzerFreq = 1600;
    double buzzerPhase = 0.0;
    int animTick = 0;
    int currentApp = 0; // 0: Game, 1: 8BitMods OS Dashboard, 2: Bank Manager
    int currentGameIndex = 0;

    // Color Minigame States
    // 0: Chao Color
    int chaoX = 100;
    int chaoY = 95;
    int chaoDir = 1;
    int chaoAction = 0;
    int chaoActionTimer = 0;
    int chaoHappiness = 100;
    int chaoStamina = 95;

    // 1: Pinta's Quest Color
    int pintaX = 40;
    int pintaY = 70;
    int pintaScroll = 0;
    int pintaAction = 0;
    int pintaGold = 2450;

    // 2: Power Stone Color
    int fighter1X = 50;
    int fighter2X = 170;
    int fighter1Action = 0;
    int fighter1Timer = 0;
    int powerGemCount = 2;

    // 3: Jet Set Radio Graffiti Dash
    int jsrX = 50;
    int jsrY = 105;
    int jsrScroll = 0;
    int jsrSpraying = 0;
    int jsrSprayTimer = 0;
    int jsrCans = 8;
    int jsrScore = 14200;

    // 4: Crazy Taxi: City Dash
    int taxiX = 110;
    int taxiSpeed = 3;
    int taxiScroll = 0;
    int taxiFare = 150;
    int taxiTimeLeft = 45;

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
    void renderVMUProScreen();
    void renderIPSScreen();
    void renderCasing();
    void updateAudio();
    void playBuzzerTone(int freq, int durationTicks);

    void setIpsPixel(int x, int y, uint32 color);
    void fillIpsRect(int x, int y, int w, int h, uint32 color);
    void drawIpsString(int x, int y, const std::string& str, uint32 color);
};
