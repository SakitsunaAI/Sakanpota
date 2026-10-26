#pragma once

#include "emulator_core.h"
#include "types.h"
#include <vector>
#include <array>
#include <string>

class SegaVMUMachine : public EmulatorCore {
public:
    static constexpr int CANVAS_W = 640;
    static constexpr int CANVAS_H = 512;

    SegaVMUMachine();
    ~SegaVMUMachine() override = default;

    SystemType getSystemType() const override { return SystemType::SegaVMU; }
    std::string getSystemName() const override { return "Sega Dreamcast VMU"; }
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
    bool loadGameByIndex(int index);
    void cycleNextGame();
    int getCurrentGameIndex() const { return currentGameIndex; }

private:
    std::array<uint8, 65536> biosRom;
    std::array<uint8, 131072> flashMemory; // 128KB Flash RAM (256 blocks of 512 bytes)
    std::array<uint8, 512> internalRam;

    // Sanyo Potato CPU registers
    uint16 PC = 0x0000;
    uint8 ACC = 0;
    uint8 B = 0;
    uint8 C = 0;
    uint8 PSW = 0; // Carry, Zero

    // 48x32 LCD buffer (192 bytes = 48 * 32 / 8)
    uint8 lcdBuffer[192];
    uint8 icons = 0x02; // 4 icons: 0x01: File, 0x02: Game, 0x04: Clock, 0x08: Flash

    // Buttons: D-Pad, A, B, Mode, Sleep (active low)
    uint8 buttonState = 0xFF;
    int buzzerTimer = 0;
    int buzzerFreq = 1200;
    double buzzerPhase = 0.0;
    int animTick = 0;
    int currentApp = 0; // 0: Game, 1: Clock, 2: File Browser
    int currentGameIndex = 0;

    // Minigame states
    // Chao Adventure 1
    int chaoX = 16;
    int chaoDir = 1;
    int chaoAction = 0; // 0: walk, 1: eat, 2: pet, 3: stats
    int chaoActionTimer = 0;
    int chaoHP = 100;
    int chaoHappiness = 90;
    int chaoApples = 6;

    // Chao Adventure 2
    int ca2HeroX = 8;
    int ca2DarkX = 32;
    int ca2HeroAction = 0; // 0: stance, 1: punch, 2: kick
    int ca2DarkAction = 0;
    int ca2ActionTimer = 0;
    int ca2HeroHP = 100;
    int ca2DarkHP = 100;

    // Pinta's Quest
    int pintaShipY = 16;
    int pintaScrollX = 0;
    int pintaGold = 150;
    int pintaFish = 4;
    int pintaAction = 0; // 0: fly, 1: cannon, 2: net
    int pintaActionTimer = 0;
    int pirateShipX = 52;
    int pirateShipY = 14;

    // Power Stone
    int psFighterX = 12;
    int psEnemyX = 34;
    int psStones = 1;
    int psSuperModeTimer = 0;
    int psAction = 0; // 0: guard/move, 1: punch, 2: super
    int psActionTimer = 0;

    // Godzilla Generations
    int godzX = 10;
    int godzAction = 0; // 0: stomp, 1: atomic ray, 2: roar
    int godzActionTimer = 0;
    int godzDestruction = 35;
    int tankX = 42;

    // Zombie Revenge
    int zombPlayerX = 10;
    int zombEnemyX = 42;
    int zombAmmo = 6;
    int zombAction = 0; // 0: aim/run, 1: shoot, 2: kick
    int zombActionTimer = 0;
    int zombScore = 480;
    int zombWave = 1;

    // Mouse state
    int mouseX = 0;
    int mouseY = 0;
    bool mouseDown = false;

    // OSD / Banner
    int osdBannerTimer = 0;
    std::string osdBannerText = "";

    std::vector<uint32> screenBuffer;
    std::vector<int16> audioBuffer;
    std::string currentMedia = "";

    void stepInstruction();
    void updateGameLogic();
    void renderVMUScreen();
    void renderLCDMatrix();
    void renderCasing();
    void updateAudio();
    void playBuzzerTone(int freq, int durationTicks);

    void setLcdPixel(int x, int y, bool on);
    bool getLcdPixel(int x, int y) const;
    void drawLcdSprite(int x, int y, int w, int h, const uint8* data);
    void drawLcdString(int x, int y, const std::string& str);
};
