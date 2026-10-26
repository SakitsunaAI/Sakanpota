#pragma once

#include "emulator_core.h"
#include "cpu6502.h"
#include <vector>
#include <array>
#include <string>

struct LynxStar {
    float x, y, speed;
    uint8 color;
};

struct LynxLaser {
    float x, y;
    bool active;
};

struct LynxAlien {
    float x, y, vx, vy;
    int type; // 0: Scout, 1: Fighter, 2: Cruiser
    int hp;
    bool active;
};

struct LynxParticle {
    float x, y, vx, vy;
    int life;
    uint8 color;
    bool active;
};

class LynxMachine : public EmulatorCore {
public:
    LynxMachine();
    ~LynxMachine() override = default;

    SystemType getSystemType() const override { return SystemType::AtariLynx; }
    std::string getSystemName() const override { return "Atari Lynx (Mikey/Suzy)"; }
    std::string getMediaName() const override { return currentMedia.empty() ? "Embedded Starfighter Arcade" : currentMedia; }

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
    CPU6502 cpu;
    std::array<uint8, 65536> ram{};
    std::array<uint8, 512> bootRom{};
    std::vector<uint8> cartridgeRom;

    std::vector<uint32> screenBuffer;
    std::vector<int16> audioBuffer;

    uint16 buttonState = 0xFFFF; // Active low: Up, Down, Left, Right, Option1, Option2, A, B
    uint16 palette[16]{};        // 16 12-bit RGB colors
    std::string currentMedia = "";

    // Built-in interactive game state
    int gameState = 0; // 0: Title/Attract, 1: Playing, 2: GameOver
    float playerX = 75.0f;
    float playerY = 80.0f;
    int score = 0;
    int highScore = 12500;
    int shields = 3;
    int wave = 1;
    int frameCount = 0;
    int laserCooldown = 0;
    int spawnTimer = 0;
    bool paused = false;

    std::vector<LynxStar> stars;
    std::vector<LynxLaser> lasers;
    std::vector<LynxAlien> aliens;
    std::vector<LynxParticle> particles;

    // Audio SFX states
    int sfxLaser = 0;
    int sfxExplode = 0;

    uint8 memRead(uint16 addr);
    void memWrite(uint16 addr, uint8 val);

    void setLcdPixel(int x, int y, uint8 col);
    void drawLcdSprite(int x, int y, int w, int h, const uint8* data);
    void drawLcdText(int x, int y, const std::string& text, uint8 col);

    void updateBuiltInGame();
    void renderLynxScreen();
    void updateLynxAudio();
};
