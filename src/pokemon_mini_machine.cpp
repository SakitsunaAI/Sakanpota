#include "pokemon_mini_machine.h"
#include "embedded_pokemon_mini.h"
#include "gui_font.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>

#define register
extern "C" {
#include "PokeMini.h"
#include "Hardware.h"
#include "MinxAudio.h"
#include "MinxLCD.h"
#include "MinxIO.h"
#include "Multicart.h"
#include "CommandLine.h"
}

PokemonMiniMachine::PokemonMiniMachine(int initialGameIndex) {
    screenBuffer.resize(640 * 512, 0xFF14243B);
    audioBuffer.resize(44100 / 60, 0);
    currentGame = std::clamp(initialGameIndex, 0, (int)EmbeddedPokeMini::NUM_GAMES - 1);
    init();
}

PokemonMiniMachine::~PokemonMiniMachine() {
    PokeMini_Destroy();
}

void PokemonMiniMachine::init() {
    static bool pokeMiniCreated = false;
    if (!pokeMiniCreated) {
        CommandLineInit();
        CommandLine.sound = 44100;
        CommandLine.synccycles = 64;
        PokeMini_Create(0, 0);
        pokeMiniCreated = true;
    }

    // Load 4KB Nintendo BIOS
    std::vector<uint8> bios;
    if (EmbeddedPokeMini::decompressBios(bios)) {
        std::memcpy(PM_BIOS, bios.data(), 4096);
        PokeMini_FreeBIOS = 0;
    }

    loadCurrentGame();
    reset();
}

void PokemonMiniMachine::reset() {
    PokeMini_Reset(0);
}

void PokemonMiniMachine::loadCurrentGame() {
    if (EmbeddedPokeMini::decompressGame(currentGame, romData)) {
        PokeMini_NewMIN((int)romData.size());
        std::memcpy(PM_ROM, romData.data(), romData.size());
        NewMulticart();
        currentMedia = EmbeddedPokeMini::GAMES[currentGame].title;
        PokeMini_Reset(0);
        std::cout << "[Pokémon Mini] Switched to embedded game #" << (currentGame + 1)
                  << ": " << currentMedia << " (" << romData.size() << " bytes)\n";
    }
}

bool PokemonMiniMachine::switchGame(int gameIndex) {
    if (gameIndex < 0 || (size_t)gameIndex >= EmbeddedPokeMini::NUM_GAMES) return false;
    currentGame = gameIndex;
    loadCurrentGame();
    inGameMenu = false;
    return true;
}

void PokemonMiniMachine::runFrame() {
    if (shakeTimer > 0) {
        shakeTimer--;
        if (shakeTimer == 0) {
            MinxIO_Keypad(MINX_KEY_SHOCK, 0);
            isShaking = false;
        }
    }

    if (!inGameMenu) {
        PokeMini_EmulateFrame();
        MinxAudio_GetSamplesS16Ch(audioBuffer.data(), (int)audioBuffer.size(), 1);
    } else {
        std::fill(audioBuffer.begin(), audioBuffer.end(), 0);
    }

    renderHandheldChassis();
    if (inGameMenu) {
        renderGameMenu();
    }
}

void PokemonMiniMachine::renderHandheldChassis() {
    // 640x512 Canvas setup
    // Authentic Pokémon Mini Handheld Chasis (Wooper Blue casing)
    uint32 casingColor = 0xFF28549C;
    uint32 casingBorder = 0xFF18386C;
    uint32 screenBezel = 0xFF8AA6C8;
    uint32 lcdBg = 0xFFC6D8A8;      // Lightest green-yellow LCD background

    for (int y = 0; y < 512; y++) {
        for (int x = 0; x < 640; x++) {
            screenBuffer[y * 640 + x] = 0xFF0E1A2E; // Dark desk background
        }
    }

    // Outer Pokémon Mini console body: Rounded rectangular handheld
    GUIFont::fillRect(screenBuffer.data(), 640, 48, 16, 544, 480, casingColor);
    GUIFont::drawRect(screenBuffer.data(), 640, 48, 16, 544, 480, casingBorder);

    // Screen Bezel (Light grey/blue oval surround)
    GUIFont::fillRect(screenBuffer.data(), 640, 68, 36, 504, 348, screenBezel);
    GUIFont::drawRect(screenBuffer.data(), 640, 68, 36, 504, 348, 0xFF5C789C);

    // Pokéball Logo & Nintendo Brand above LCD
    GUIFont::drawString(screenBuffer.data(), 640, 240, 42, "POKEMON MINI", 0xFF142C54);
    GUIFont::drawString(screenBuffer.data(), 640, 450, 42, "Nintendo", 0xFF142C54);

    // Centered LCD: 96x64 pixels scaled 5x -> 480 x 320, placed at (80, 56)
    int lcdStartX = 80;
    int lcdStartY = 56;
    int scale = 5;

    // Authentic 4-shade Pokemon Mini monochrome palette
    static const uint32 pmPalette[4] = {
        0xFFC6D8A8, // 0: Off / Background
        0xFF8CA870, // 1: Light gray
        0xFF507040, // 2: Dark gray
        0xFF142810  // 3: On / Darkest
    };

    if (LCDPixelsA != nullptr) {
        for (int py = 0; py < 64; py++) {
            for (int px = 0; px < 96; px++) {
                uint8 val = LCDPixelsA[py * 96 + px];
                // Quantize 0..255 to 4 shades
                int shade = (val < 64) ? 0 : ((val < 128) ? 1 : ((val < 192) ? 2 : 3));
                uint32 pixelCol = pmPalette[shade];

                for (int dy = 0; dy < scale; dy++) {
                    for (int dx = 0; dx < scale; dx++) {
                        int bx = lcdStartX + px * scale + dx;
                        int by = lcdStartY + py * scale + dy;
                        // Subtle LCD sub-pixel grid dot
                        if (dx == scale - 1 || dy == scale - 1) {
                            screenBuffer[by * 640 + bx] = (shade == 0) ? 0xFFBED0A0 : pixelCol;
                        } else {
                            screenBuffer[by * 640 + bx] = pixelCol;
                        }
                    }
                }
            }
        }
    } else {
        GUIFont::fillRect(screenBuffer.data(), 640, lcdStartX, lcdStartY, 480, 320, lcdBg);
    }

    // Controls below screen:
    // Left: D-Pad
    GUIFont::fillRect(screenBuffer.data(), 640, 110, 400, 36, 12, 0xFF1A1A1A); // Horizontal
    GUIFont::fillRect(screenBuffer.data(), 640, 122, 388, 12, 36, 0xFF1A1A1A); // Vertical
    GUIFont::drawString(screenBuffer.data(), 640, 95, 442, "D-PAD (Oklar)", 0xFFC0D0E8);

    // Center: Status / Rumble indicator & C button
    if (PokeMini_Rumbling) {
        GUIFont::fillRect(screenBuffer.data(), 640, 260, 400, 120, 18, 0xFFE04030);
        GUIFont::drawString(screenBuffer.data(), 640, 275, 404, "RUMBLE TIK!", 0xFFFFFFFF);
    } else {
        GUIFont::fillRect(screenBuffer.data(), 640, 290, 400, 60, 18, 0xFF354868);
        GUIFont::drawString(screenBuffer.data(), 640, 305, 404, "C Tusu", 0xFFD0D8E6);
    }
    GUIFont::drawString(screenBuffer.data(), 640, 240, 426, "[TAB]: 11 Oyun Secici", 0xFFF0D030);
    GUIFont::drawString(screenBuffer.data(), 640, 240, 444, "[S]: Cihazi Salla (Shake)", 0xFF88D888);

    // Right: A & B Buttons
    GUIFont::fillRect(screenBuffer.data(), 640, 470, 406, 26, 26, 0xFFE04040); // B button
    GUIFont::drawString(screenBuffer.data(), 640, 479, 412, "B", 0xFFFFFFFF);

    GUIFont::fillRect(screenBuffer.data(), 640, 510, 388, 26, 26, 0xFFE04040); // A button
    GUIFont::drawString(screenBuffer.data(), 640, 519, 394, "A", 0xFFFFFFFF);

    GUIFont::drawString(screenBuffer.data(), 640, 465, 442, "Z: A | X: B", 0xFFC0D0E8);

    // Bottom info footer
    char infoStr[128];
    snprintf(infoStr, sizeof(infoStr), "Oyun [%d/11]: %s", currentGame + 1, currentMedia.c_str());
    GUIFont::drawString(screenBuffer.data(), 640, 80, 476, infoStr, 0xFFFFFFFF);
}

void PokemonMiniMachine::renderGameMenu() {
    // Semi-transparent overlay with Game Selector
    GUIFont::fillRect(screenBuffer.data(), 640, 100, 60, 440, 340, 0xEE101C30);
    GUIFont::drawRect(screenBuffer.data(), 640, 100, 60, 440, 340, 0xFFF0D030);

    GUIFont::drawString(screenBuffer.data(), 640, 180, 72, "POKEMON MINI 11-IN-1 SECICI", 0xFFF0D030);
    GUIFont::drawString(screenBuffer.data(), 640, 130, 92, "Yukari/Asagi ile secin, ENTER ile baslatin:", 0xFFC0D8F0);

    for (size_t i = 0; i < EmbeddedPokeMini::NUM_GAMES; i++) {
        int y = 114 + i * 20;
        bool sel = ((int)i == menuCursor);
        if (sel) {
            GUIFont::fillRect(screenBuffer.data(), 640, 114, y - 2, 412, 18, 0xFF2A5288);
            GUIFont::drawString(screenBuffer.data(), 640, 120, y, "> ", 0xFFFFFF00);
        }
        char lineStr[64];
        snprintf(lineStr, sizeof(lineStr), "%2zu. %s", i + 1, EmbeddedPokeMini::GAMES[i].title);
        GUIFont::drawString(screenBuffer.data(), 640, 136, y, lineStr, sel ? 0xFFFFFFFF : 0xFFB0C4DE);
    }

    GUIFont::drawString(screenBuffer.data(), 640, 150, 368, "ENTER: Oyunu Baslat  |  ESC/TAB: Kapat", 0xFF88A8D0);
}

void PokemonMiniMachine::handleKeyDown(SDL_Keycode key, uint16 mod) {
    (void)mod;

    // Toggle multi-game menu
    if (key == SDLK_TAB || key == SDLK_F1 || key == SDLK_m) {
        inGameMenu = !inGameMenu;
        menuCursor = currentGame;
        return;
    }

    if (inGameMenu) {
        if (key == SDLK_UP) {
            menuCursor = (menuCursor - 1 + (int)EmbeddedPokeMini::NUM_GAMES) % (int)EmbeddedPokeMini::NUM_GAMES;
        } else if (key == SDLK_DOWN) {
            menuCursor = (menuCursor + 1) % (int)EmbeddedPokeMini::NUM_GAMES;
        } else if (key == SDLK_RETURN || key == SDLK_SPACE || key == SDLK_z) {
            switchGame(menuCursor);
        } else if (key == SDLK_ESCAPE) {
            inGameMenu = false;
        }
        return;
    }

    // Direct game inputs to PokeMini
    if (key == SDLK_UP)    MinxIO_Keypad(MINX_KEY_UP, 1);
    if (key == SDLK_DOWN)  MinxIO_Keypad(MINX_KEY_DOWN, 1);
    if (key == SDLK_LEFT)  MinxIO_Keypad(MINX_KEY_LEFT, 1);
    if (key == SDLK_RIGHT) MinxIO_Keypad(MINX_KEY_RIGHT, 1);
    if (key == SDLK_z || key == SDLK_k || key == SDLK_RETURN || key == SDLK_SPACE) {
        MinxIO_Keypad(MINX_KEY_A, 1);
    }
    if (key == SDLK_x || key == SDLK_j || key == SDLK_BACKSPACE) {
        MinxIO_Keypad(MINX_KEY_B, 1);
    }
    if (key == SDLK_c || key == SDLK_l) {
        MinxIO_Keypad(MINX_KEY_C, 1);
    }
    if (key == SDLK_s) {
        // Shake sensor triggered
        isShaking = true;
        shakeTimer = 8;
        MinxIO_Keypad(MINX_KEY_SHOCK, 1);
    }
    if (key == SDLK_p || key == SDLK_F12) {
        MinxIO_Keypad(MINX_KEY_POWER, 1);
    }
}

void PokemonMiniMachine::handleKeyUp(SDL_Keycode key, uint16 mod) {
    (void)mod;
    if (inGameMenu) return;

    if (key == SDLK_UP)    MinxIO_Keypad(MINX_KEY_UP, 0);
    if (key == SDLK_DOWN)  MinxIO_Keypad(MINX_KEY_DOWN, 0);
    if (key == SDLK_LEFT)  MinxIO_Keypad(MINX_KEY_LEFT, 0);
    if (key == SDLK_RIGHT) MinxIO_Keypad(MINX_KEY_RIGHT, 0);
    if (key == SDLK_z || key == SDLK_k || key == SDLK_RETURN || key == SDLK_SPACE) {
        MinxIO_Keypad(MINX_KEY_A, 0);
    }
    if (key == SDLK_x || key == SDLK_j || key == SDLK_BACKSPACE) {
        MinxIO_Keypad(MINX_KEY_B, 0);
    }
    if (key == SDLK_c || key == SDLK_l) {
        MinxIO_Keypad(MINX_KEY_C, 0);
    }
    if (key == SDLK_p || key == SDLK_F12) {
        MinxIO_Keypad(MINX_KEY_POWER, 0);
    }
}

bool PokemonMiniMachine::loadMedia(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    romData = std::vector<uint8>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    std::string fname = path.substr(path.find_last_of("/\\") + 1);
    currentMedia = fname;

    PokeMini_NewMIN((int)romData.size());
    std::memcpy(PM_ROM, romData.data(), romData.size());
    NewMulticart();
    PokeMini_Reset(0);
    std::cout << "[Pokémon Mini] Loaded external cartridge: " << currentMedia << " (" << romData.size() << " bytes)\n";
    return true;
}
