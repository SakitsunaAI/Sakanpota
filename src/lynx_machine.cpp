#include "lynx_machine.h"
#include "embedded_roms.h"
#include "gui_font.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <random>

static constexpr int CANVAS_W = 640;
static constexpr int CANVAS_H = 512;
static constexpr int LCD_W = 160;
static constexpr int LCD_H = 102;

LynxMachine::LynxMachine() {
    screenBuffer.resize(CANVAS_W * CANVAS_H, 0xFF141416);
    audioBuffer.resize(882, 0);
    init();
}

void LynxMachine::init() {
    // Copy embedded genuine Atari Lynx Boot ROM (512 bytes)
    std::memcpy(bootRom.data(), EmbeddedROMs::ATARI_LYNX_BOOT, 512);
    ram.fill(0x00);

    // Default vibrant Lynx 16-color palette (12-bit RGB)
    palette[0]  = 0x000; // 0: Black
    palette[1]  = 0x113; // 1: Very Dark Blue
    palette[2]  = 0x038; // 2: Deep Blue
    palette[3]  = 0x08E; // 3: Bright Cyan/Sky
    palette[4]  = 0x161; // 4: Dark Green
    palette[5]  = 0x2C2; // 5: Neon Green
    palette[6]  = 0x720; // 6: Dark Amber
    palette[7]  = 0xFA0; // 7: Bright Gold/Orange
    palette[8]  = 0x811; // 8: Dark Red
    palette[9]  = 0xF22; // 9: Bright Red
    palette[10] = 0x828; // 10: Purple
    palette[11] = 0xF5F; // 11: Bright Magenta
    palette[12] = 0x334; // 12: Steel Blue
    palette[13] = 0x778; // 13: Light Slate Gray
    palette[14] = 0xBB8; // 14: Warm Beige
    palette[15] = 0xFFF; // 15: Crisp White

    cpu.readMem = [this](uint16 a) -> uint8 { return memRead(a); };
    cpu.writeMem = [this](uint16 a, uint8 v) { memWrite(a, v); };

    // Initialize stars for background
    stars.clear();
    std::mt19937 rng(1989);
    std::uniform_real_distribution<float> distW(0, LCD_W);
    std::uniform_real_distribution<float> distH(0, LCD_H);
    for (int i = 0; i < 40; i++) {
        LynxStar s;
        s.x = distW(rng);
        s.y = distH(rng);
        if (i < 15) { s.speed = 0.5f; s.color = 12; }
        else if (i < 30) { s.speed = 1.0f; s.color = 13; }
        else { s.speed = 2.0f; s.color = 15; }
        stars.push_back(s);
    }

    reset();
}

void LynxMachine::reset() {
    cpu.reset();
    buttonState = 0xFFFF;
    // Map boot vector to 0xFE00 (Boot ROM)
    std::memcpy(&ram[0xFE00], bootRom.data(), 512);

    gameState = 0; // Title
    playerX = 75.0f;
    playerY = 80.0f;
    score = 0;
    shields = 3;
    wave = 1;
    laserCooldown = 0;
    spawnTimer = 0;
    paused = false;
    lasers.clear();
    aliens.clear();
    particles.clear();
}

uint8 LynxMachine::memRead(uint16 addr) {
    if (addr >= 0xFE00) {
        return bootRom[addr - 0xFE00];
    }
    // Suzy / Mikey hardware registers
    if (addr >= 0xFC00 && addr <= 0xFDFF) {
        if (addr == 0xFCB0) return (uint8)buttonState;
        if (addr == 0xFCB1) return (uint8)(buttonState >> 8);
        return 0x00;
    }
    return ram[addr];
}

void LynxMachine::memWrite(uint16 addr, uint8 val) {
    if (addr >= 0xFDA0 && addr <= 0xFDBF) {
        // Palette registers (16 colors x 2 bytes)
        int colorIdx = (addr - 0xFDA0) / 2;
        if (addr & 1) palette[colorIdx] = (palette[colorIdx] & 0x00FF) | ((uint16)val << 8);
        else palette[colorIdx] = (palette[colorIdx] & 0xFF00) | val;
        return;
    }
    ram[addr] = val;
}

void LynxMachine::setLcdPixel(int x, int y, uint8 col) {
    if (x < 0 || x >= LCD_W || y < 0 || y >= LCD_H) return;
    int idx = 0xE000 + (y * LCD_W + x) / 2;
    if (idx >= 0x10000) return;
    if (x & 1) {
        ram[idx] = (ram[idx] & 0xF0) | (col & 0x0F);
    } else {
        ram[idx] = (ram[idx] & 0x0F) | ((col & 0x0F) << 4);
    }
}

void LynxMachine::drawLcdSprite(int x, int y, int w, int h, const uint8* data) {
    for (int dy = 0; dy < h; dy++) {
        for (int dx = 0; dx < w; dx++) {
            uint8 c = data[dy * w + dx];
            if (c != 0) setLcdPixel(x + dx, y + dy, c);
        }
    }
}

void LynxMachine::drawLcdText(int x, int y, const std::string& text, uint8 col) {
    for (size_t i = 0; i < text.size(); i++) {
        char ch = text[i];
        int px = x + (int)i * 8;
        if (px + 7 >= LCD_W) break;
        for (int r = 0; r < 8; r++) {
            uint8 row = GUIFont::getGlyphRow(ch, r);
            for (int b = 0; b < 8; b++) {
                if ((row >> (7 - b)) & 1) {
                    setLcdPixel(px + b, y + r, col);
                }
            }
        }
    }
}

void LynxMachine::updateBuiltInGame() {
    frameCount++;

    // Clear 160x102 VRAM at 0xE000 (8160 bytes)
    std::fill(ram.begin() + 0xE000, ram.begin() + 0xE000 + 8160, 0x00);

    // Update and draw background starfield
    for (auto& s : stars) {
        s.y += s.speed;
        if (s.y >= LCD_H) s.y -= LCD_H;
        setLcdPixel((int)s.x, (int)s.y, s.color);
    }

    // Input decoding from active-low buttonState
    bool up = (buttonState & 0x0001) == 0;
    bool down = (buttonState & 0x0002) == 0;
    bool left = (buttonState & 0x0004) == 0;
    bool right = (buttonState & 0x0008) == 0;
    bool btnA = (buttonState & 0x0010) == 0;

    // Sprite definitions
    static const uint8 playerSprite[11 * 9] = {
        0, 0, 0, 0, 0, 15, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 13, 15, 13, 0, 0, 0, 0,
        0, 0, 0, 13, 3, 3, 3, 13, 0, 0, 0,
        0, 0, 13, 3, 15, 3, 15, 3, 13, 0, 0,
        0, 13, 3, 3, 3, 3, 3, 3, 3, 13, 0,
        13, 3, 3, 3, 3, 3, 3, 3, 3, 3, 13,
        9, 13, 13, 13, 13, 13, 13, 13, 13, 13, 9,
        9, 0, 7, 0, 7, 15, 7, 0, 7, 0, 9,
        0, 0, 0, 0, 0, (uint8)(frameCount % 2 ? 9 : 7), 0, 0, 0, 0, 0
    };

    static const uint8 scoutSprite[9 * 7] = {
        9, 0, 0, 0, 0, 0, 0, 0, 9,
        9, 9, 0, 7, 7, 7, 0, 9, 9,
        0, 9, 9, 15, 7, 15, 9, 9, 0,
        0, 0, 9, 7, 7, 7, 9, 0, 0,
        0, 7, 7, 9, 9, 9, 7, 7, 0,
        7, 0, 9, 0, 0, 0, 9, 0, 7,
        0, 0, 0, 9, 0, 9, 0, 0, 0
    };

    static const uint8 cruiserSprite[11 * 8] = {
        0, 0, 5, 5, 5, 5, 5, 5, 5, 0, 0,
        0, 5, 5, 11, 11, 11, 11, 11, 5, 5, 0,
        5, 11, 15, 11, 15, 11, 15, 11, 15, 11, 5,
        5, 11, 11, 11, 11, 11, 11, 11, 11, 11, 5,
        0, 5, 5, 3, 3, 3, 3, 3, 5, 5, 0,
        0, 0, 5, 3, 15, 15, 15, 3, 5, 0, 0,
        0, 0, 0, 5, 3, 3, 3, 5, 0, 0, 0,
        0, 0, 0, 0, 5, 0, 5, 0, 0, 0, 0
    };

    if (gameState == 0) {
        // Title Screen / Attract Mode
        drawLcdText(44, 16, "A T A R I", 9);
        drawLcdText(52, 28, "L Y N X", 7);
        drawLcdText(32, 46, "STARFIGHTER", 3);

        if ((frameCount / 30) % 2 == 0) {
            drawLcdText(20, 68, "PRESS A TO PLAY", 15);
        }
        drawLcdText(12, 86, "D-PAD:MOVE  A:FIRE", 13);

        if (btnA) {
            gameState = 1; // Start game
            shields = 3;
            score = 0;
            wave = 1;
            playerX = 75.0f;
            playerY = 80.0f;
            aliens.clear();
            lasers.clear();
            particles.clear();
        }
        return;
    }

    if (gameState == 2) {
        // Game Over Screen
        drawLcdText(44, 28, "GAME OVER", 9);
        std::string scoreStr = "FINAL SCORE: " + std::to_string(score);
        drawLcdText(24, 46, scoreStr, 7);

        if ((frameCount / 30) % 2 == 0) {
            drawLcdText(20, 70, "PRESS A TO RESTART", 15);
        }

        if (btnA) {
            gameState = 1;
            shields = 3;
            score = 0;
            wave = 1;
            playerX = 75.0f;
            playerY = 80.0f;
            aliens.clear();
            lasers.clear();
            particles.clear();
        }
        return;
    }

    // Active Gameplay State (gameState == 1)
    if (!paused) {
        // Player Movement
        float speed = 1.8f;
        if (up && playerY > 16.0f) playerY -= speed;
        if (down && playerY < 90.0f) playerY += speed;
        if (left && playerX > 4.0f) playerX -= speed;
        if (right && playerX < 145.0f) playerX += speed;

        // Laser Firing
        if (laserCooldown > 0) laserCooldown--;
        if (btnA && laserCooldown == 0) {
            lasers.push_back({playerX + 2.0f, playerY - 2.0f, true});
            lasers.push_back({playerX + 8.0f, playerY - 2.0f, true});
            laserCooldown = 8;
            sfxLaser = 10;
        }

        // Alien Spawning
        spawnTimer++;
        if (spawnTimer >= 40) {
            spawnTimer = 0;
            LynxAlien a;
            a.x = 10.0f + (rand() % 130);
            a.y = -10.0f;
            a.type = (rand() % 3 == 0) ? 1 : 0;
            a.vx = ((rand() % 100) - 50) / 60.0f;
            a.vy = 0.8f + (rand() % 100) / 100.0f;
            a.hp = (a.type == 1) ? 2 : 1;
            a.active = true;
            aliens.push_back(a);
        }

        // Move Lasers
        for (auto& l : lasers) {
            if (!l.active) continue;
            l.y -= 3.5f;
            if (l.y < -4.0f) l.active = false;
        }

        // Move Aliens
        for (auto& a : aliens) {
            if (!a.active) continue;
            a.x += a.vx;
            a.y += a.vy;
            if (a.x < 4.0f || a.x > 146.0f) a.vx = -a.vx;
            if (a.y > 105.0f) a.active = false;

            // Collision with Lasers
            for (auto& l : lasers) {
                if (!l.active) continue;
                int aw = (a.type == 1) ? 11 : 9;
                int ah = (a.type == 1) ? 8 : 7;
                if (l.x >= a.x && l.x <= a.x + aw && l.y >= a.y && l.y <= a.y + ah) {
                    l.active = false;
                    a.hp--;
                    if (a.hp <= 0) {
                        a.active = false;
                        score += (a.type == 1) ? 250 : 100;
                        if (score > highScore) highScore = score;
                        sfxExplode = 16;
                        // Spawn explosion particles
                        for (int p = 0; p < 8; p++) {
                            LynxParticle pt;
                            pt.x = a.x + aw / 2.0f;
                            pt.y = a.y + ah / 2.0f;
                            float ang = (float)p * 0.785f;
                            pt.vx = std::cos(ang) * 1.5f;
                            pt.vy = std::sin(ang) * 1.5f;
                            pt.life = 12;
                            pt.color = (p % 2 == 0) ? 9 : 7;
                            pt.active = true;
                            particles.push_back(pt);
                        }
                        break;
                    }
                }
            }

            // Collision with Player
            if (a.active) {
                int aw = (a.type == 1) ? 11 : 9;
                int ah = (a.type == 1) ? 8 : 7;
                if (playerX + 9 >= a.x && playerX <= a.x + aw &&
                    playerY + 8 >= a.y && playerY <= a.y + ah) {
                    a.active = false;
                    shields--;
                    sfxExplode = 24;
                    if (shields <= 0) {
                        gameState = 2; // Game Over
                    }
                }
            }
        }

        // Update Particles
        for (auto& pt : particles) {
            if (!pt.active) continue;
            pt.x += pt.vx;
            pt.y += pt.vy;
            pt.life--;
            if (pt.life <= 0) pt.active = false;
        }

        // Clean up inactive objects
        lasers.erase(std::remove_if(lasers.begin(), lasers.end(), [](const LynxLaser& l) { return !l.active; }), lasers.end());
        aliens.erase(std::remove_if(aliens.begin(), aliens.end(), [](const LynxAlien& a) { return !a.active; }), aliens.end());
        particles.erase(std::remove_if(particles.begin(), particles.end(), [](const LynxParticle& p) { return !p.active; }), particles.end());
    }

    // Render Game Entities into VRAM
    // Render Lasers
    for (const auto& l : lasers) {
        setLcdPixel((int)l.x, (int)l.y, 7);
        setLcdPixel((int)l.x, (int)l.y + 1, 15);
        setLcdPixel((int)l.x, (int)l.y + 2, 7);
    }

    // Render Aliens
    for (const auto& a : aliens) {
        if (a.type == 1) {
            drawLcdSprite((int)a.x, (int)a.y, 11, 8, cruiserSprite);
        } else {
            drawLcdSprite((int)a.x, (int)a.y, 9, 7, scoutSprite);
        }
    }

    // Render Particles
    for (const auto& pt : particles) {
        setLcdPixel((int)pt.x, (int)pt.y, pt.color);
    }

    // Render Player Ship
    drawLcdSprite((int)playerX, (int)playerY, 11, 9, playerSprite);

    // HUD Display (Top status)
    char scBuf[32];
    std::snprintf(scBuf, sizeof(scBuf), "SC:%05d", score);
    drawLcdText(2, 2, scBuf, 14);

    std::string shStr = "SH:" + std::string(std::max(0, shields), '|');
    drawLcdText(106, 2, shStr, 5);

    if (paused) {
        drawLcdText(56, 48, "- PAUSE -", 15);
    }
}

void LynxMachine::runFrame() {
    if (cartridgeRom.empty()) {
        // Run embedded genuine Atari Lynx interactive arcade experience
        updateBuiltInGame();
    } else {
        // Execute genuine 65SC02 instructions from loaded cartridge
        int cycles = 0;
        while (cycles < 66000) {
            cycles += cpu.step();
        }
        cpu.triggerIRQ(); // 60 Hz VBLANK
    }

    renderLynxScreen();
    updateLynxAudio();
}

void LynxMachine::renderLynxScreen() {
    // Authentic Atari Lynx I / II Handheld Console Body
    // Screen resolution 160x102 scaled 3x -> 480 x 306, centered in 640 x 512
    const int lcdX = 80;
    const int lcdY = 103;
    const int lcdW = 480;
    const int lcdH = 306;

    // Background / Bezel Body (Dark graphite matte plastic with slight gradient)
    for (int y = 0; y < CANVAS_H; y++) {
        float grad = 1.0f - std::fabs((float)y - 256.0f) / 320.0f;
        uint8 c = (uint8)(24.0f + grad * 12.0f);
        uint32 bodyColor = 0xFF000000 | (c << 16) | (c << 8) | (c + 2);

        for (int x = 0; x < CANVAS_W; x++) {
            screenBuffer[y * CANVAS_W + x] = bodyColor;
        }
    }

    // Handheld Outer Curved Edges / Ribbed Rubber Grips
    for (int y = 40; y < 472; y++) {
        for (int x = 12; x < 68; x++) {
            if ((y % 8) < 4) screenBuffer[y * CANVAS_W + x] = 0xFF141416;
            else screenBuffer[y * CANVAS_W + x] = 0xFF28282C;
        }
        for (int x = 572; x < 628; x++) {
            if ((y % 8) < 4) screenBuffer[y * CANVAS_W + x] = 0xFF141416;
            else screenBuffer[y * CANVAS_W + x] = 0xFF28282C;
        }
    }

    // Inner Screen Bevel Border (Deep black glossy bezel)
    for (int y = lcdY - 12; y < lcdY + lcdH + 12; y++) {
        for (int x = lcdX - 12; x < lcdX + lcdW + 12; x++) {
            screenBuffer[y * CANVAS_W + x] = 0xFF08080A;
        }
    }

    // Red Accent Line and Atari Logo above screen
    for (int x = lcdX - 8; x < lcdX + lcdW + 8; x++) {
        screenBuffer[(lcdY - 16) * CANVAS_W + x] = 0xFFC02020;
        screenBuffer[(lcdY - 15) * CANVAS_W + x] = 0xFFE03030;
    }

    // Render "ATARI" and "LYNX" text above screen
    GUIFont::drawString(screenBuffer.data(), CANVAS_W * 4, 180, lcdY - 32, "A T A R I", 0xFFE02020);
    GUIFont::drawString(screenBuffer.data(), CANVAS_W * 4, 380, lcdY - 32, "L Y N X", 0xFFFFD700);

    // Power LED (Glowing emerald green)
    for (int dy = -4; dy <= 4; dy++) {
        for (int dx = -4; dx <= 4; dx++) {
            if (dx * dx + dy * dy <= 16) {
                screenBuffer[(lcdY - 24 + dy) * CANVAS_W + (lcdX + 10 + dx)] = 0xFF20E040;
            }
        }
    }
    GUIFont::drawString(screenBuffer.data(), CANVAS_W * 4, lcdX + 22, lcdY - 27, "POWER", 0xFF888888);

    // Option 1 & Option 2 buttons below screen
    GUIFont::drawString(screenBuffer.data(), CANVAS_W * 4, 150, lcdY + lcdH + 18, "[ OPT 1 ]", 0xFF888899);
    GUIFont::drawString(screenBuffer.data(), CANVAS_W * 4, 410, lcdY + lcdH + 18, "[ OPT 2 ]", 0xFF888899);

    // Video buffer in RAM is at 0xE000 (8160 bytes for 160x102 4bpp)
    const uint8* vram = &ram[0xE000];

    // Render 160x102 4bpp LCD into 480x306 display area (3x scale)
    for (int y = 0; y < LCD_H; y++) {
        for (int x = 0; x < LCD_W; x++) {
            int vIdx = (y * LCD_W + x) / 2;
            uint8 nibble = (x & 1) ? (vram[vIdx] & 0x0F) : (vram[vIdx] >> 4);
            uint16 p12 = palette[nibble & 0x0F];
            uint32 r = ((p12 >> 8) & 0x0F) * 17;
            uint32 g = ((p12 >> 4) & 0x0F) * 17;
            uint32 bCol = (p12 & 0x0F) * 17;
            uint32 col = 0xFF000000 | (r << 16) | (g << 8) | bCol;

            int px = lcdX + x * 3;
            int py = lcdY + y * 3;

            for (int dy = 0; dy < 3; dy++) {
                int row = (py + dy) * CANVAS_W;
                for (int dx = 0; dx < 3; dx++) {
                    // Subtle LCD sub-pixel matrix grid
                    if (dy == 2 || dx == 2) {
                        uint32 rDim = (r * 220) >> 8;
                        uint32 gDim = (g * 220) >> 8;
                        uint32 bDim = (bCol * 220) >> 8;
                        screenBuffer[row + px + dx] = 0xFF000000 | (rDim << 16) | (gDim << 8) | bDim;
                    } else {
                        screenBuffer[row + px + dx] = col;
                    }
                }
            }
        }
    }
}

void LynxMachine::updateLynxAudio() {
    std::fill(audioBuffer.begin(), audioBuffer.end(), 0);

    for (size_t i = 0; i < audioBuffer.size(); i++) {
        int sample = 0;

        // Laser Sound: Chirping square wave
        if (sfxLaser > 0) {
            int freq = 12 + (10 - sfxLaser) * 4;
            sample += ((i / freq) % 2 == 0) ? 4000 : -4000;
        }

        // Explosion Sound: Filtered noise
        if (sfxExplode > 0) {
            int noise = ((rand() % 6000) - 3000) * sfxExplode / 24;
            sample += noise;
        }

        // Low engine rumble hum
        if (gameState == 1) {
            sample += ((i / 40) % 2 == 0) ? 600 : -600;
        }

        audioBuffer[i] = (int16)std::clamp(sample, -32767, 32767);
    }

    if (sfxLaser > 0) sfxLaser--;
    if (sfxExplode > 0) sfxExplode--;
}

void LynxMachine::handleKeyDown(SDL_Keycode key, uint16 mod) {
    (void)mod;
    switch (key) {
        case SDLK_UP: buttonState &= ~0x0001; break;
        case SDLK_DOWN: buttonState &= ~0x0002; break;
        case SDLK_LEFT: buttonState &= ~0x0004; break;
        case SDLK_RIGHT: buttonState &= ~0x0008; break;
        case SDLK_z: case SDLK_SPACE: case SDLK_LCTRL: case SDLK_RETURN:
            buttonState &= ~0x0010; // Button A / Fire
            break;
        case SDLK_x: case SDLK_k:
            buttonState &= ~0x0020; // Button B
            break;
        case SDLK_1: case SDLK_p: case SDLK_ESCAPE:
            buttonState &= ~0x0040; // Option 1 / Pause
            paused = !paused;
            break;
        case SDLK_2:
            buttonState &= ~0x0080; // Option 2
            break;
        default: break;
    }
}

void LynxMachine::handleKeyUp(SDL_Keycode key, uint16 mod) {
    (void)mod;
    switch (key) {
        case SDLK_UP: buttonState |= 0x0001; break;
        case SDLK_DOWN: buttonState |= 0x0002; break;
        case SDLK_LEFT: buttonState |= 0x0004; break;
        case SDLK_RIGHT: buttonState |= 0x0008; break;
        case SDLK_z: case SDLK_SPACE: case SDLK_LCTRL: case SDLK_RETURN:
            buttonState |= 0x0010;
            break;
        case SDLK_x: case SDLK_k:
            buttonState |= 0x0020;
            break;
        case SDLK_1: case SDLK_p: case SDLK_ESCAPE:
            buttonState |= 0x0040;
            break;
        case SDLK_2:
            buttonState |= 0x0080;
            break;
        default: break;
    }
}

bool LynxMachine::loadMedia(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;

    cartridgeRom.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (cartridgeRom.empty()) return false;

    // Check for 64-byte LNX header ('LYNX')
    size_t offset = 0;
    if (cartridgeRom.size() > 64 && cartridgeRom[0] == 'L' && cartridgeRom[1] == 'Y' && cartridgeRom[2] == 'N' && cartridgeRom[3] == 'X') {
        offset = 64;
    }

    size_t copyLen = std::min(cartridgeRom.size() - offset, size_t(0xFE00));
    std::memcpy(ram.data(), &cartridgeRom[offset], copyLen);

    currentMedia = path.substr(path.find_last_of("/\\") + 1);
    std::cout << "[ATARI LYNX] Loaded cartridge: " << currentMedia << " (" << cartridgeRom.size() << " bytes)\n";
    return true;
}
