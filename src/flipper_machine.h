#pragma once

#include "emulator_core.h"
#include "types.h"
#include "embedded_flipper_assets.h"
#include <vector>
#include <array>
#include <string>

class FlipperMachine : public EmulatorCore {
public:
    static constexpr int CANVAS_W = 640;
    static constexpr int CANVAS_H = 512;
    static constexpr int LCD_W = 128;
    static constexpr int LCD_H = 64;

    // Display placement on 640x512 canvas (scaled 3.5x: 448x224)
    static constexpr int SCREEN_X = 80;
    static constexpr int SCREEN_Y = 64;
    static constexpr int SCREEN_SCALE_W = 448; // 128 * 3.5
    static constexpr int SCREEN_SCALE_H = 224; // 64 * 3.5

    FlipperMachine();
    ~FlipperMachine() override = default;

    SystemType getSystemType() const override { return SystemType::FlipperZero; }
    std::string getSystemName() const override { return "Flipper Zero Portable Multi-Tool"; }
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
    bool loadToolByName(const std::string& name);

    // 128x64 Monochrome Drawing Engine
    void clearLcd(bool set = false);
    void setPixel(int x, int y, bool set = true);
    bool getPixel(int x, int y) const;
    void drawLine(int x0, int y0, int x1, int y1, bool set = true);
    void drawRect(int x0, int y0, int w, int h, bool set = true);
    void fillRect(int x0, int y0, int w, int h, bool set = true);
    void drawCircle(int xc, int yc, int r, bool set = true);
    void fillCircle(int xc, int yc, int r, bool set = true);
    void drawGlyph(char c, int x, int y, bool invert = false);
    void drawString(const std::string& str, int x, int y, bool invert = false);
    void drawBitmap(int x, int y, int w, int h, const uint8* data, bool invert = false);

    // Piezo Buzzer Sound & Haptics API
    void playTone(float freqHz, int durationMs, float volume = 0.8f);
    void playClick();
    void playSuccessBeep();
    void playErrorBuzz();

private:
    // 128x64 1-bit Monochrome Screen Buffer (each byte stores 8 vertical pixels like ST7567)
    std::array<uint8, LCD_W * (LCD_H / 8)> lcdVRAM;

    // Canvas & Audio Buffers
    std::array<uint32, CANVAS_W * CANVAS_H> screenBuffer;
    std::vector<int16> audioBuffer;

    // Piezo Buzzer State
    float buzzerFreq = 0.0f;
    float buzzerPhase = 0.0f;
    float buzzerVol = 0.0f;
    int buzzerRemainingSamples = 0;

    // RGB Notification LED State
    uint32 ledColor = 0xFF0030A0; // Ambient blue
    int ledPulseTimer = 0;

    // Buttons (Up, Down, Left, Right, OK, Back)
    uint8 buttonState = 0;
    uint8 lastButtonState = 0;
    int buttonRepeatTimer[6] = {0};

    // Flipper OS State Machine
    enum class MenuState {
        Desktop,            // Dolphin idle / pet status
        MainMenu,           // Main vertical scrolling list
        SubGhz_Analyzer,    // Live RSSI & Waterfall Spectrum
        SubGhz_Read,        // Princeton / Came keyfob sniffer
        SubGhz_Saved,       // Saved remote transmitter
        RFID_Read,          // 125 kHz EM4100 / HIDProx reader
        RFID_Saved,         // Saved key fob emulator
        NFC_Read,           // 13.56 MHz MIFARE / NTAG / EMV reader
        NFC_Saved,          // Saved NFC tag emulator
        Infrared_Remote,    // Universal TV / Audio IR transmitter
        Infrared_Learn,     // Raw pulse timing graph
        iButton_Read,       // Dallas 1-Wire reader
        BadUSB_Run,         // Keystroke injection runner
        GPIO_Tools,         // Frequency generator / battery telemetry
        Game_Snake,         // Playable Snake
        Game_Tetris,        // Playable Tetris
        Game_MicroDoom,     // Raycasting pseudo-3D engine
        Music_Player,       // Chiptune tracker player
        Spectrum_Waterfall  // Live full-screen RF spectrum
    };
    MenuState currentState = MenuState::Desktop;

    // Navigation state
    int mainMenuIndex = 0;
    int subMenuIndex = 0;
    int scrollOffset = 0;

    // Dolphin Pet State
    int dolphinMood = 0; // 0: Normal, 1: Happy, 2: Hacking, 3: Eating, 4: Sleeping
    int dolphinAnimFrame = 0;
    int dolphinLevel = 3;
    int dolphinXP = 72; // 0..100%
    int dolphinTimer = 0;
    std::string dolphinSpeech = "Ready to hack!";

    // Sub-GHz State
    float subGhzFreq = 433.92f;
    int subGhzRssi = -68; // dBm
    uint8 waterfallBuffer[64];
    bool subGhzTransmitting = false;
    int subGhzSignalTimer = 0;
    int subGhzDetectedProtocol = 0; // 0: None, 1: Princeton, 2: Came, 3: KeeLoq

    // RFID & NFC State
    int rfidScanProgress = 0;
    bool rfidCardDetected = false;
    int nfcScanProgress = 0;
    bool nfcCardDetected = false;
    int ibuttonScanProgress = 0;
    bool ibuttonDetected = false;

    // Infrared State
    int irCommandIndex = 0;
    int irTransmitTimer = 0;

    // BadUSB State
    int badUsbProgress = 0;
    int badUsbTotal = 100;
    std::string badUsbStatus = "Injecting payload...";

    // Snake Game State
    struct SnakeState {
        struct Point { int x, y; };
        std::vector<Point> body;
        int dirX = 1, dirY = 0;
        Point food = {20, 15};
        int score = 0;
        bool gameOver = false;
        int tickCounter = 0;
    } snake;

    // Tetris Game State
    struct TetrisState {
        uint8 board[10][20]; // 10 wide, 20 high
        int pieceX = 4, pieceY = 0;
        int pieceType = 0;
        int rotation = 0;
        int score = 0;
        int lines = 0;
        bool gameOver = false;
        int dropTimer = 0;
    } tetris;

    // Micro-Doom Raycaster State
    struct DoomState {
        float posX = 3.5f, posY = 3.5f;
        float dirX = -1.0f, dirY = 0.0f;
        float planeX = 0.0f, planeY = 0.66f;
        int shootTimer = 0;
        int monsterX = 7, monsterY = 7;
        bool monsterAlive = true;
        int monsterHp = 3;
        int kills = 0;
    } doom;

    // Music Player State
    int musicTrackIndex = 0;
    int musicNoteIndex = 0;
    int musicTick = 0;
    bool musicPlaying = false;

    // Subsystem updates
    void updateDesktop();
    void updateMainMenu();
    void updateSubGhzAnalyzer();
    void updateSubGhzRead();
    void updateSubGhzSaved();
    void updateRfidRead();
    void updateRfidSaved();
    void updateNfcRead();
    void updateNfcSaved();
    void updateInfrared();
    void updateIButton();
    void updateBadUsb();
    void updateGpioTools();
    void updateGameSnake();
    void updateGameTetris();
    void updateGameMicroDoom();
    void updateMusicPlayer();
    void updateSpectrumWaterfall();

    // Game initializers
    void initSnake();
    void initTetris();
    void initMicroDoom();

    // Hardware Casing & Screen Output
    void renderCasing();
    void renderLcdToCanvas();
    void updateAudio();

    // Input Helpers
    bool isPressed(int btn) const;
    bool isTriggered(int btn);
};
