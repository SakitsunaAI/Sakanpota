#pragma once

#include "emulator_core.h"
#include "types.h"
#include "embedded_pico8_carts.h"
#include <vector>
#include <array>
#include <string>

class Pico8Machine : public EmulatorCore {
public:
    static constexpr int CANVAS_W = 640;
    static constexpr int CANVAS_H = 512;
    static constexpr int P8_SCREEN_W = 128;
    static constexpr int P8_SCREEN_H = 128;
    static constexpr int P8_SCALE = 3; // 128 * 3 = 384x384 active display
    static constexpr int SCREEN_X = 128; // (640 - 384) / 2 = 128
    static constexpr int SCREEN_Y = 64;  // (512 - 384) / 2 = 64

    Pico8Machine();
    ~Pico8Machine() override = default;

    SystemType getSystemType() const override { return SystemType::Pico8; }
    std::string getSystemName() const override { return "PICO-8 Fantasy Console"; }
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
    bool loadCartridgeData(const uint8* data, size_t size, const std::string& title);

    // PICO-8 Graphics Rasterizer API
    void cls(uint8 col = 0);
    void pset(int x, int y, int col);
    uint8 pget(int x, int y);
    void sset(int x, int y, int col);
    uint8 sget(int x, int y);
    void line(int x0, int y0, int x1, int y1, int col);
    void rect(int x0, int y0, int x1, int y1, int col);
    void rectfill(int x0, int y0, int x1, int y1, int col);
    void circ(int xc, int yc, int r, int col);
    void circfill(int xc, int yc, int r, int col);
    void spr(int n, int x, int y, int w = 1, int h = 1, bool flip_x = false, bool flip_y = false);
    void sspr(int sx, int sy, int sw, int sh, int dx, int dy, int dw = -1, int dh = -1, bool flip_x = false, bool flip_y = false);
    void map(int cel_x, int cel_y, int sx, int sy, int cel_w, int cel_h, uint8 layers = 0);
    uint8 mget(int x, int y);
    void mset(int x, int y, uint8 val);
    uint8 fget(int n, int f = -1);
    void fset(int n, int f, bool v);
    void fset(int n, uint8 val);
    void pal(int c0 = -1, int c1 = -1, int p = 0);
    void palt(int c = -1, bool t = true);
    void camera(int x = 0, int y = 0);
    void clip(int x = 0, int y = 0, int w = 128, int h = 128);
    void color(int col);
    void cursor(int x, int y);
    void print(const std::string& str, int x = -1, int y = -1, int col = -1);

    // PICO-8 Audio Synthesis API
    void sfx(int n, int channel = -1, int offset = 0);
    void music(int n, int fade_len = 0, int channel_mask = 0);

    // PICO-8 Input API
    bool btn(int i, int p = 0) const;
    bool btnp(int i, int p = 0);

private:
    // 64KB authentic RAM layout (0x0000..0xFFFF):
    std::array<uint8, 0x10000> ram;

    // Draw state
    int camX = 0;
    int camY = 0;
    int clipX = 0;
    int clipY = 0;
    int clipW = 128;
    int clipH = 128;
    uint8 curColor = 6;
    int curX = 0;
    int curY = 0;
    uint16 transMask = 0x0001; // Color 0 is transparent by default
    uint8 drawPal[16];
    uint8 screenPal[16];

    // Casing, Screen & Audio buffers
    std::array<uint32, CANVAS_W * CANVAS_H> screenBuffer;
    std::vector<int16> audioBuffer;

    // Audio Voice Channels
    struct AudioVoice {
        bool active = false;
        int sfxId = -1;
        int noteIndex = 0;
        int tickCounter = 0;
        int speed = 16;
        float phase = 0.0f;
        float freq = 440.0f;
        float targetFreq = 440.0f;
        float volume = 0.0f;
        uint8 waveform = 0;
        uint8 effect = 0;
        uint32 noiseSeed = 0x12345678;
        float lastSample = 0.0f;
    };
    std::array<AudioVoice, 4> channels;
    int musicTrack = -1;
    int musicPattern = 0;
    int musicTick = 0;

    // Input state
    uint8 buttonState[2] = {0, 0};
    uint8 lastButtonState[2] = {0, 0};
    uint8 btnRepeatTimer[2][8];

    // Active Cart & System State
    enum class CartMode {
        BootShell,
        Celeste,
        DuskChild,
        MaiChan,
        PicoRacers,
        Mistigri,
        CustomLua
    };
    CartMode currentMode = CartMode::BootShell;
    std::string currentCartTitle = "PICO-8 BOOT";
    std::string currentCartAuthor = "Lexaloffle";
    std::string currentCartFile = "";

    // Boot Shell state
    int bootPhase = 0; // 0: Splash Sun, 1: Command Shell, 2: Cart Carousel
    int bootTimer = 0;
    float bootSunAngle = 0.0f;
    std::string cmdBuffer = "";
    std::vector<std::string> shellHistory;
    int carouselIndex = 0;
    float carouselOffset = 0.0f;
    int activityLedTimer = 0;

    // 1. Celeste Game State Machine
    struct CelesteState {
        float px = 16.0f, py = 96.0f;
        float pvx = 0.0f, pvy = 0.0f;
        bool facingRight = true;
        bool onGround = false;
        bool onWallLeft = false;
        bool onWallRight = false;
        int djump = 1; // 1 = dash available, 0 = spent
        int dashTimer = 0;
        float dashDx = 0.0f, dashDy = 0.0f;
        int dashCooldown = 0;
        int room = 0; // 0..4
        int deaths = 0;
        int strawberries = 0;
        int gameFrames = 0;
        bool summitReached = false;
        struct HairNode { float x, y; float r; uint8 col; };
        std::vector<HairNode> hair;
        struct Strawberry { int room; float x, y; bool collected; float floatOffset; };
        std::vector<Strawberry> berryList;
        struct Spring { int room; int x, y; int bounceTimer; };
        std::vector<Spring> springList;
        struct Balloon { int room; float x, y; bool active; int respawnTimer; };
        std::vector<Balloon> balloonList;
    } celeste;

    // 2. Dusk Child Game State Machine
    struct DuskState {
        float px = 32.0f, py = 64.0f;
        float pvx = 0.0f, pvy = 0.0f;
        bool facingRight = true;
        int roomX = 0, roomY = 0;
        bool hasLantern = true;
        bool lanternLit = true;
        int keys = 0;
        int torchesLit = 0;
        int pressureSwitches = 0;
        bool sanctuaryOpen = false;
        int animFrame = 0;
    } dusk;

    // 3. Mai-Chan's Sweet Bakery Game State Machine
    struct MaiChanState {
        int cursorX = 2, cursorY = 2;
        int selectedX = -1, selectedY = -1;
        uint8 grid[6][6];
        int swapTimer = 0;
        int matchTimer = 0;
        int score = 0;
        int combo = 1;
        int timeLeft = 1800; // 30 seconds at 60fps
        int happyCustomerAnim = 0;
        bool gameOver = false;
    } maichan;

    // 4. Pico Racers Game State Machine
    struct RacersState {
        float carX = 64.0f, carY = 100.0f;
        float carAngle = 0.0f;
        float carSpeed = 0.0f;
        int currentLap = 1;
        int totalLaps = 3;
        int checkpoint = 0;
        int bestLapTime = 9999;
        int currentLapTime = 0;
        bool raceFinished = false;
        struct AICar { float x, y, angle, speed; int node; uint8 col; };
        std::vector<AICar> aiCars;
    } racers;

    // 5. Mistigri Game State Machine
    struct MistigriState {
        float catX = 20.0f, catY = 100.0f;
        float catVx = 0.0f, catVy = 0.0f;
        bool onGround = false;
        int treatsCollected = 0;
        int totalTreats = 8;
        float yarnX = 70.0f, yarnY = 100.0f, yarnVx = 0.0f;
        int stage = 1;
    } mistigri;

    // Core methods
    void initCasingArt();
    void renderCasing();
    void renderScreenToCanvas();
    void updateAudio();
    void bootPico8();

    // Mode updates
    void updateBootShell();
    void updateCeleste();
    void updateDuskChild();
    void updateMaiChan();
    void updatePicoRacers();
    void updateMistigri();
    void updateCustomLua();

    // Reset game state helpers
    void initCeleste();
    void initDuskChild();
    void initMaiChan();
    void initPicoRacers();
    void initMistigri();

    // Cartridge parsing
    bool parseP8(const std::string& text);
    bool parsePngCart(const std::vector<uint8>& pngData);
};
