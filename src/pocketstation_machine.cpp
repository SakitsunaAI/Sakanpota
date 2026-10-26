#include "pocketstation_machine.h"
#include "embedded_roms.h"
#include "gui_font.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <iostream>

PocketStationMachine::PocketStationMachine() : ps(nullptr), buttonMask(0), currentGame(PocketGame::ChocoboWorld), frameCounter(0) {
    rawFramebuffer.resize(CANVAS_WIDTH * CANVAS_HEIGHT, 0xFF141923);
    audioBuffer.resize(882, 0);

    for (int y = 0; y < LCD_SIZE; y++) {
        for (int x = 0; x < LCD_SIZE; x++) {
            lcdPixels[y][x] = false;
        }
    }

    init();
}

PocketStationMachine::~PocketStationMachine() {
    if (ps) {
        psemu_destroy(ps);
        ps = nullptr;
    }
}

void PocketStationMachine::init() {
    if (!ps) {
        ps = psemu_create();
    } else {
        psemu_reset(ps);
    }

    // Load authentic Sony PocketStation SCPH-4000 BIOS
    psemu_load_bios(ps, EmbeddedROMs::POCKETSTATION_BIOS, sizeof(EmbeddedROMs::POCKETSTATION_BIOS));

    // Load default game (Chocobo World)
    loadGameByName("chocobo");
}

void PocketStationMachine::reset() {
    buttonMask = 0;
    frameCounter = 0;
    if (ps) {
        psemu_reset(ps);
        psemu_load_bios(ps, EmbeddedROMs::POCKETSTATION_BIOS, sizeof(EmbeddedROMs::POCKETSTATION_BIOS));
    }
}

bool PocketStationMachine::loadGameByName(const std::string& name) {
    std::string n = name;
    std::transform(n.begin(), n.end(), n.begin(), ::tolower);

    const uint8* romData = nullptr;
    size_t romSize = 0;

    if (n == "chocobo" || n == "chocobo world" || n == "ff8" || n == "ffviii") {
        currentGame = PocketGame::ChocoboWorld;
        loadedMedia = "Chocobo World (FFVIII)";
        EmbeddedROMs::getROM("chocobo", romData, romSize);
    } else if (n == "dokodemo" || n == "toro" || n == "cat" || n == "dokodemo issho") {
        currentGame = PocketGame::DokodemoIssho;
        loadedMedia = "Dokodemo Issho (Toro Inoue)";
        EmbeddedROMs::getROM("dokodemo", romData, romSize);
    } else if (n == "crash" || n == "bandicoot" || n == "crash bandicoot") {
        currentGame = PocketGame::CrashBandicoot;
        loadedMedia = "Crash Bandicoot: Pocket Bandicoot";
        EmbeddedROMs::getROM("crash", romData, romSize);
    } else if (n == "megaman" || n == "rockman" || n == "mega man") {
        currentGame = PocketGame::MegaMan;
        loadedMedia = "Pocket Rockman / Mega Man";
        EmbeddedROMs::getROM("megaman", romData, romSize);
    } else if (n == "spyro" || n == "dragon" || n == "spyro the dragon") {
        currentGame = PocketGame::Spyro;
        loadedMedia = "Spyro the Dragon";
        EmbeddedROMs::getROM("spyro", romData, romSize);
    } else if (n == "space" || n == "invaders" || n == "space invaders") {
        currentGame = PocketGame::SpaceInvaders;
        loadedMedia = "Pocket Space Invaders";
        EmbeddedROMs::getROM("space", romData, romSize);
    } else if (n == "dungeon" || n == "rpg" || n == "pocket dungeon") {
        currentGame = PocketGame::PocketDungeon;
        loadedMedia = "Pocket Dungeon RPG";
        EmbeddedROMs::getROM("dungeon", romData, romSize);
    } else {
        currentGame = PocketGame::ChocoboWorld;
        loadedMedia = "Chocobo World (FFVIII)";
        EmbeddedROMs::getROM("chocobo", romData, romSize);
    }

    if (ps && romData && romSize > 0) {
        psemu_load_content(ps, romData, romSize);
    }

    reset();
    return true;
}

void PocketStationMachine::cycleNextGame() {
    int next = ((int)currentGame + 1) % 7;
    switch ((PocketGame)next) {
        case PocketGame::ChocoboWorld:   loadGameByName("chocobo"); break;
        case PocketGame::DokodemoIssho:  loadGameByName("dokodemo"); break;
        case PocketGame::CrashBandicoot: loadGameByName("crash"); break;
        case PocketGame::MegaMan:        loadGameByName("megaman"); break;
        case PocketGame::Spyro:          loadGameByName("spyro"); break;
        case PocketGame::SpaceInvaders:  loadGameByName("space"); break;
        case PocketGame::PocketDungeon:  loadGameByName("dungeon"); break;
    }
}

void PocketStationMachine::updateLCD() {
    if (!ps) return;
    const uint8_t* fb = psemu_get_framebuffer(ps);
    if (!fb) return;

    for (int y = 0; y < LCD_SIZE; y++) {
        for (int x = 0; x < LCD_SIZE; x++) {
            uint8_t byte = fb[y * PSEMU_LCD_STRIDE + (x / 8)];
            lcdPixels[y][x] = (byte & (1 << (x % 8))) != 0;
        }
    }
}

void PocketStationMachine::runFrame() {
    frameCounter++;

    if (ps) {
        psemu_set_buttons(ps, buttonMask);
        psemu_run(ps, PSEMU_REFERENCE_CLOCK_HZ / 60);
        updateLCD();
    }

    renderPocketStation();
    updateAudio();
}

void PocketStationMachine::renderPocketStation() {
    // Fill background with elegant dark studio backdrop
    std::fill(rawFramebuffer.begin(), rawFramebuffer.end(), 0xFF121622);

    // Subtle radial spotlight centered behind the device
    for (int y = 0; y < CANVAS_HEIGHT; y++) {
        int dy = y - 256;
        for (int x = 0; x < CANVAS_WIDTH; x++) {
            int dx = x - 320;
            int distSq = dx * dx + dy * dy;
            if (distSq < 160000) {
                int glow = (160000 - distSq) / 9500;
                uint32 bg = 0xFF121622;
                uint8 r = std::min(255, (int)((bg >> 16) & 0xFF) + glow / 2);
                uint8 g = std::min(255, (int)((bg >> 8) & 0xFF) + glow / 2);
                uint8 b = std::min(255, (int)(bg & 0xFF) + glow);
                rawFramebuffer[y * CANVAS_WIDTH + x] = (0xFF << 24) | (r << 16) | (g << 8) | b;
            }
        }
    }

    // Sony PocketStation Body Geometry: X: 190..450 (width 260), Y: 26..486 (height 460)
    const int bx0 = 190, bx1 = 450;
    const int by0 = 26,  by1 = 486;

    // 1. Soft Outer Drop Shadow
    for (int y = by0 + 8; y <= by1 + 14; y++) {
        for (int x = bx0 - 6; x <= bx1 + 10; x++) {
            if (y >= 0 && y < CANVAS_HEIGHT && x >= 0 && x < CANVAS_WIDTH) {
                uint32 cur = rawFramebuffer[y * CANVAS_WIDTH + x];
                uint8 r = ((cur >> 16) & 0xFF) * 0.45f;
                uint8 g = ((cur >> 8) & 0xFF) * 0.45f;
                uint8 b = (cur & 0xFF) * 0.45f;
                rawFramebuffer[y * CANVAS_WIDTH + x] = 0xFF000000 | (r << 16) | (g << 8) | b;
            }
        }
    }

    // 2. PocketStation Body - Milky White Frosted Casing with 3D Bevels
    for (int y = by0; y <= by1; y++) {
        for (int x = bx0; x <= bx1; x++) {
            // Smooth 30px rounded corners on body
            int cornerDist = 0;
            if (x < bx0 + 30 && y < by0 + 30) {
                int dx = bx0 + 30 - x, dy = by0 + 30 - y;
                cornerDist = dx * dx + dy * dy;
                if (cornerDist > 30 * 30) continue;
            } else if (x > bx1 - 30 && y < by0 + 30) {
                int dx = x - (bx1 - 30), dy = by0 + 30 - y;
                cornerDist = dx * dx + dy * dy;
                if (cornerDist > 30 * 30) continue;
            } else if (x < bx0 + 26 && y > by1 - 26) {
                int dx = bx0 + 26 - x, dy = y - (by1 - 26);
                cornerDist = dx * dx + dy * dy;
                if (cornerDist > 26 * 26) continue;
            } else if (x > bx1 - 26 && y > by1 - 26) {
                int dx = x - (bx1 - 26), dy = y - (by1 - 26);
                cornerDist = dx * dx + dy * dy;
                if (cornerDist > 26 * 26) continue;
            }

            // Authentic PocketStation soft off-white / pearlescent plastic
            // Subtle vertical shading gradient
            float grad = (float)(y - by0) / (by1 - by0);
            int baseR = 244 - (int)(grad * 18.0f);
            int baseG = 248 - (int)(grad * 18.0f);
            int baseB = 254 - (int)(grad * 20.0f);

            // 3D Bevel Highlights & Shadows
            if (x < bx0 + 3 || y < by0 + 3) {
                baseR = std::min(255, baseR + 22);
                baseG = std::min(255, baseG + 22);
                baseB = std::min(255, baseB + 22);
            } else if (x > bx1 - 4 || y > by1 - 4) {
                baseR = (int)(baseR * 0.78f);
                baseG = (int)(baseG * 0.78f);
                baseB = (int)(baseB * 0.82f);
            }

            rawFramebuffer[y * CANVAS_WIDTH + x] = 0xFF000000 | (baseR << 16) | (baseG << 8) | baseB;
        }
    }

    // 3. Top Infrared (IrDA) Glossy Dome Window (Center: X: 320, Y: 34)
    for (int dy = -7; dy <= 7; dy++) {
        for (int dx = -24; dx <= 24; dx++) {
            if (dx * dx / 576.0f + dy * dy / 49.0f <= 1.0f) {
                uint32 irCol = 0xFF1C080E; // Glossy dark ruby / obsidian
                if (dy < -2) irCol = 0xFF4A1826; // Highlight
                if (dy == -3 && dx >= -12 && dx <= 12) irCol = 0xFFBA4560; // Specular streak
                rawFramebuffer[(34 + dy) * CANVAS_WIDTH + 320 + dx] = irCol;
            }
        }
    }

    // 4. Side Flip-Lid Hinge Pivot Notches (Y: 215..235)
    for (int dy = -10; dy <= 10; dy++) {
        for (int dx = 0; dx < 6; dx++) {
            rawFramebuffer[(225 + dy) * CANVAS_WIDTH + bx0 + dx] = 0xFF8A93A4;
            rawFramebuffer[(225 + dy) * CANVAS_WIDTH + bx1 - dx] = 0xFF8A93A4;
        }
    }

    // 5. Brand Logos
    // "SONY" in crisp clean dark graphite
    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH, 304, 52, "SONY", 0xFF2A2E38);

    // "PocketStation" cursive brand in PlayStation blue
    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH, 268, 68, "PocketStation", 0xFF003791);

    // 6. Smoked Dark Acrylic LCD Bezel Lens (X: 224..416, Y: 80..260)
    const int wx0 = 224, wx1 = 416;
    const int wy0 = 80,  wy1 = 260;

    for (int y = wy0; y <= wy1; y++) {
        for (int x = wx0; x <= wx1; x++) {
            // Rounded corners on acrylic lens
            if ((x < wx0 + 12 && y < wy0 + 12 && (wx0 + 12 - x)*(wx0 + 12 - x) + (wy0 + 12 - y)*(wy0 + 12 - y) > 144) ||
                (x > wx1 - 12 && y < wy0 + 12 && (x - (wx1 - 12))*(x - (wx1 - 12)) + (wy0 + 12 - y)*(wy0 + 12 - y) > 144) ||
                (x < wx0 + 12 && y > wy1 - 12 && (wx0 + 12 - x)*(wx0 + 12 - x) + (y - (wy1 - 12))*(y - (wy1 - 12)) > 144) ||
                (x > wx1 - 12 && y > wy1 - 12 && (x - (wx1 - 12))*(x - (wx1 - 12)) + (y - (wy1 - 12))*(y - (wy1 - 12)) > 144)) {
                continue;
            }

            uint32 winCol = 0xFF242832;
            if (x == wx0 || y == wy0 || x == wx0 + 1 || y == wy0 + 1) winCol = 0xFF14171E; // Inner shadow
            else if (x >= wx1 - 1 || y >= wy1 - 1) winCol = 0xFF444D5C; // Inner highlight
            rawFramebuffer[y * CANVAS_WIDTH + x] = winCol;
        }
    }

    // 7. Active 32x32 LCD Matrix (Scaled 5x -> 160x160, Centered at X: 240, Y: 90)
    const int lcdX0 = 240;
    const int lcdY0 = 90;
    const int cellScale = 5;

    const uint32 lcdBackdrop = 0xFF8FA784; // Authentic STN greenish-grey LCD
    const uint32 lcdGridGap  = 0xFF849C7A; // Matrix separator grid
    const uint32 lcdPixelOn  = 0xFF142210; // Dark liquid crystal active dot
    const uint32 lcdPixelOff = 0xFF879E7D; // Unpowered background dot

    // Fill LCD background
    for (int y = 0; y < LCD_SIZE * cellScale; y++) {
        int py = lcdY0 + y;
        for (int x = 0; x < LCD_SIZE * cellScale; x++) {
            int px = lcdX0 + x;
            rawFramebuffer[py * CANVAS_WIDTH + px] = lcdBackdrop;
        }
    }

    // Draw 32x32 pixels
    for (int y = 0; y < LCD_SIZE; y++) {
        int py = lcdY0 + y * cellScale;
        for (int x = 0; x < LCD_SIZE; x++) {
            int px = lcdX0 + x * cellScale;
            bool dot = lcdPixels[y][x];
            uint32 dotCol = dot ? lcdPixelOn : lcdPixelOff;

            for (int dy = 0; dy < cellScale - 1; dy++) {
                int rowOffset = (py + dy) * CANVAS_WIDTH;
                for (int dx = 0; dx < cellScale - 1; dx++) {
                    rawFramebuffer[rowOffset + px + dx] = dotCol;
                }
            }
            // Grid gap
            for (int dy = 0; dy < cellScale; dy++) {
                rawFramebuffer[(py + dy) * CANVAS_WIDTH + px + cellScale - 1] = lcdGridGap;
            }
            for (int dx = 0; dx < cellScale; dx++) {
                rawFramebuffer[(py + cellScale - 1) * CANVAS_WIDTH + px + dx] = lcdGridGap;
            }
        }
    }

    // Diagonal Glass Lens Glare Reflection
    for (int dy = 0; dy < 160; dy++) {
        int py = lcdY0 + dy;
        for (int dx = 0; dx < 160; dx++) {
            int px = lcdX0 + dx;
            int diag = dx + dy * 2;
            if (diag >= 190 && diag <= 250) {
                int glareAlpha = 30 - abs(diag - 220);
                if (glareAlpha > 0) {
                    uint32 cur = rawFramebuffer[py * CANVAS_WIDTH + px];
                    uint8 r = std::min(255, (int)((cur >> 16) & 0xFF) + glareAlpha);
                    uint8 g = std::min(255, (int)((cur >> 8) & 0xFF) + glareAlpha);
                    uint8 b = std::min(255, (int)(cur & 0xFF) + glareAlpha);
                    rawFramebuffer[py * CANVAS_WIDTH + px] = (0xFF << 24) | (r << 16) | (g << 8) | b;
                }
            }
        }
    }

    // 8. Official PlayStation Logo below screen (Center: X: 320, Y: 275)
    auto drawPlayStationLogo = [&](int cx, int cy) {
        // Red 'P'
        for (int dy = -7; dy <= 7; dy++) {
            for (int dx = -6; dx <= -4; dx++) rawFramebuffer[(cy + dy) * CANVAS_WIDTH + cx + dx] = 0xFFDF0024;
        }
        for (int dy = -7; dy <= -1; dy++) {
            for (int dx = -3; dx <= 0; dx++) {
                if (dx == 0 || dy == -7 || dy == -1) rawFramebuffer[(cy + dy) * CANVAS_WIDTH + cx + dx] = 0xFFDF0024;
            }
        }
        // Yellow, Teal, Blue 'S' base
        for (int dx = -6; dx <= 6; dx++) {
            rawFramebuffer[(cy + 6) * CANVAS_WIDTH + cx + dx] = 0xFFF3C300; // Yellow
            rawFramebuffer[(cy + 7) * CANVAS_WIDTH + cx + dx] = 0xFF00AC9F; // Teal
            rawFramebuffer[(cy + 8) * CANVAS_WIDTH + cx + dx] = 0xFF002F87; // Blue
        }
    };
    drawPlayStationLogo(320, 275);

    // 9. Circular 5-Way Control Pad (Compass Rose) Centered at X: 320, Y: 366
    const int padCX = 320, padCY = 366;

    // Outer Circular Recessed Dish (radius 52)
    for (int dy = -52; dy <= 52; dy++) {
        for (int dx = -52; dx <= 52; dx++) {
            int d = dx * dx + dy * dy;
            if (d <= 52 * 52) {
                uint32 dishCol = 0xFFD2D8E4;
                if (dy < -30 || dx < -30) dishCol = 0xFFB8C0D0; // Inner shadow
                else if (dy > 30 || dx > 30) dishCol = 0xFFE6ECF5; // Inner highlight
                rawFramebuffer[(padCY + dy) * CANVAS_WIDTH + padCX + dx] = dishCol;
            }
        }
    }

    bool btnUp     = (buttonMask & PSEMU_BUTTON_UP) != 0;
    bool btnDown   = (buttonMask & PSEMU_BUTTON_DOWN) != 0;
    bool btnLeft   = (buttonMask & PSEMU_BUTTON_LEFT) != 0;
    bool btnRight  = (buttonMask & PSEMU_BUTTON_RIGHT) != 0;
    bool btnAction = (buttonMask & PSEMU_BUTTON_FIRE) != 0;

    // Draw Directional Buttons (Sculpted oval caps with 3D bevels)
    auto drawDirButton = [&](int cx, int cy, const char* arrow, bool pressed) {
        int rw = 13, rh = 13;
        for (int dy = -rh; dy <= rh; dy++) {
            for (int dx = -rw; dx <= rw; dx++) {
                if (dx * dx + dy * dy <= rw * rh) {
                    uint32 col = pressed ? 0xFF9CA5B4 : 0xFFE0E5EE;
                    if (!pressed) {
                        if (dx < -2 || dy < -2) col = 0xFFF4F8FC; // Bevel highlight
                        else if (dx > 2 || dy > 2) col = 0xFFB6BECC; // Shadow
                    }
                    rawFramebuffer[(cy + dy) * CANVAS_WIDTH + cx + dx] = col;
                }
            }
        }
        GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH, cx - 3, cy - 3, arrow, 0xFF353C48);
    };

    drawDirButton(padCX, padCY - 34, "^", btnUp);    // Up
    drawDirButton(padCX, padCY + 34, "v", btnDown);  // Down
    drawDirButton(padCX - 34, padCY, "<", btnLeft);  // Left
    drawDirButton(padCX + 34, padCY, ">", btnRight); // Right

    // Center Action Button (Circular dome with PlayStation circle symbol)
    for (int dy = -16; dy <= 16; dy++) {
        for (int dx = -16; dx <= 16; dx++) {
            int d = dx * dx + dy * dy;
            if (d <= 16 * 16) {
                uint32 col = btnAction ? 0xFF8A93A2 : 0xFF4A5260;
                if (!btnAction) {
                    if (dx < -3 || dy < -3) col = 0xFF646E80;
                    else if (dx > 3 || dy > 3) col = 0xFF303642;
                }
                rawFramebuffer[(padCY + dy) * CANVAS_WIDTH + padCX + dx] = col;
            }
        }
    }
    // PlayStation Circle symbol '○'
    for (int dy = -5; dy <= 5; dy++) {
        for (int dx = -5; dx <= 5; dx++) {
            int d = dx * dx + dy * dy;
            if (d >= 12 && d <= 25) {
                rawFramebuffer[(padCY + dy) * CANVAS_WIDTH + padCX + dx] = 0xFFDF0024; // Red circle symbol
            }
        }
    }

    // 10. Acoustic Piezo Buzzer Speaker Grille (7 Perforation holes at X: 320, Y: 442)
    static const int holeOffsets[7][2] = {
        {0, 0}, {-9, -5}, {0, -10}, {9, -5}, {-9, 5}, {0, 10}, {9, 5}
    };
    for (int i = 0; i < 7; i++) {
        int hx = 320 + holeOffsets[i][0];
        int hy = 442 + holeOffsets[i][1];
        for (int dy = -2; dy <= 2; dy++) {
            for (int dx = -2; dx <= 2; dx++) {
                if (dx * dx + dy * dy <= 4) {
                    rawFramebuffer[(hy + dy) * CANVAS_WIDTH + hx + dx] = 0xFF1E222A;
                }
            }
        }
        rawFramebuffer[(hy + 2) * CANVAS_WIDTH + hx + 1] = 0xFFFFFFFF; // Specular rim
    }

    // 11. SCPH-4000 molded marking on bottom chin
    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH, 300, 468, "SCPH-4000", 0xFF9CA5B4);

    // 12. Glassmorphic HUD Card (Left Side)
    GUIFont::fillRect(rawFramebuffer.data(), CANVAS_WIDTH, 14, 26, 162, 160, 0xDD0D1117);
    GUIFont::drawRect(rawFramebuffer.data(), CANVAS_WIDTH, 14, 26, 162, 160, 0xFF30363D);

    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH, 22, 34, "SONY POCKETSTATION", 0xFF58A6FF);
    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH, 22, 48, "PK201 ARM7 Core", 0xFF7EE787);
    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH, 22, 68, loadedMedia, 0xFFF2CC60);

    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH, 22, 94, "Controls:", 0xFF58A6FF);
    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH, 22, 108, "Arrows : D-Pad", 0xFFC9D1D9);
    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH, 22, 122, "Space/Z : Action", 0xFFC9D1D9);
    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH, 22, 136, "Tab : Cycle Title", 0xFFC9D1D9);
    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH, 22, 154, "Mouse : Direct Click", 0xFF8B949E);
}

void PocketStationMachine::updateAudio() {
    if (ps) {
        psemu_get_audio_samples(ps, audioBuffer.data(), audioBuffer.size());
    } else {
        std::fill(audioBuffer.begin(), audioBuffer.end(), 0);
    }
}

void PocketStationMachine::handleKeyDown(SDL_Keycode key, uint16 mod) {
    (void)mod;
    if (key == SDLK_UP)    buttonMask |= PSEMU_BUTTON_UP;
    if (key == SDLK_DOWN)  buttonMask |= PSEMU_BUTTON_DOWN;
    if (key == SDLK_LEFT)  buttonMask |= PSEMU_BUTTON_LEFT;
    if (key == SDLK_RIGHT) buttonMask |= PSEMU_BUTTON_RIGHT;

    if (key == SDLK_SPACE || key == SDLK_z || key == SDLK_RETURN) {
        buttonMask |= PSEMU_BUTTON_FIRE;
    }

    if (key == SDLK_TAB || key == SDLK_F2) {
        cycleNextGame();
    }
}

void PocketStationMachine::handleKeyUp(SDL_Keycode key, uint16 mod) {
    (void)mod;
    if (key == SDLK_UP)    buttonMask &= ~PSEMU_BUTTON_UP;
    if (key == SDLK_DOWN)  buttonMask &= ~PSEMU_BUTTON_DOWN;
    if (key == SDLK_LEFT)  buttonMask &= ~PSEMU_BUTTON_LEFT;
    if (key == SDLK_RIGHT) buttonMask &= ~PSEMU_BUTTON_RIGHT;

    if (key == SDLK_SPACE || key == SDLK_z || key == SDLK_RETURN) {
        buttonMask &= ~PSEMU_BUTTON_FIRE;
    }
}

void PocketStationMachine::handleMouseMotion(int x, int y) {
    (void)x; (void)y;
}

void PocketStationMachine::handleMouseButton(uint8 button, bool pressed, int x, int y) {
    if (button != SDL_BUTTON_LEFT) return;

    if (!pressed) {
        buttonMask = 0;
        return;
    }

    const int padCX = 320;
    const int padCY = 366;

    // 1. Top IrDA window or SONY logo click -> Cycle to next game!
    if (x >= 280 && x <= 360 && y >= 24 && y <= 64) {
        cycleNextGame();
        return;
    }

    // 2. Center Action Button (Radius 18)
    int adx = x - padCX;
    int ady = y - padCY;
    if (adx * adx + ady * ady <= 18 * 18) {
        buttonMask |= PSEMU_BUTTON_FIRE;
        return;
    }

    // 3. Directional Buttons (Compass Rose)
    // Up
    if (std::abs(x - padCX) <= 16 && y < padCY - 16 && y >= padCY - 50) {
        buttonMask |= PSEMU_BUTTON_UP;
        return;
    }
    // Down
    if (std::abs(x - padCX) <= 16 && y > padCY + 16 && y <= padCY + 50) {
        buttonMask |= PSEMU_BUTTON_DOWN;
        return;
    }
    // Left
    if (x < padCX - 16 && x >= padCX - 50 && std::abs(y - padCY) <= 16) {
        buttonMask |= PSEMU_BUTTON_LEFT;
        return;
    }
    // Right
    if (x > padCX + 16 && x <= padCX + 50 && std::abs(y - padCY) <= 16) {
        buttonMask |= PSEMU_BUTTON_RIGHT;
        return;
    }
}

