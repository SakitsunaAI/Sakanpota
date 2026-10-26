#include "vmu_machine.h"
#include "embedded_roms.h"
#include "embedded_vmu_games.h"
#include "gui_font.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <cmath>
#include <algorithm>

SegaVMUMachine::SegaVMUMachine() {
    screenBuffer.resize(CANVAS_W * CANVAS_H, 0xFF131722);
    audioBuffer.resize(882, 0); // 44100Hz / 50Hz = 882 samples
}

std::string SegaVMUMachine::getMediaName() const {
    if (!currentMedia.empty()) return currentMedia;
    const auto* info = EmbeddedVMUGames::getGameInfo(currentGameIndex);
    return info ? info->title : "Sega Dreamcast VMU";
}

void SegaVMUMachine::init() {
    // 1. Copy genuine Sega Dreamcast VMU BIOS (64KB)
    std::memcpy(biosRom.data(), EmbeddedROMs::SEGA_VMU_BIOS, 65536);
    flashMemory.fill(0x00);
    internalRam.fill(0x00);
    std::memset(lcdBuffer, 0, sizeof(lcdBuffer));

    // 2. Ensure encrypted VMU games are decrypted into RAM
    EmbeddedVMUGames::ensureDecrypted();

    // 3. Load default game (Chao Adventure 1)
    loadGameByIndex(0);

    reset();
}

void SegaVMUMachine::reset() {
    PC = 0x0000;
    ACC = 0;
    B = 0;
    C = 0;
    PSW = 0;
    buttonState = 0xFF;
    icons = 0x02; // Game icon active
    buzzerTimer = 0;
    buzzerFreq = 1200;
    buzzerPhase = 0.0;
    animTick = 0;
    currentApp = 0;

    // Reset minigame states
    chaoX = 16;
    chaoDir = 1;
    chaoAction = 0;
    chaoActionTimer = 0;
    chaoHP = 100;
    chaoHappiness = 95;
    chaoApples = 8;

    ca2HeroX = 8;
    ca2DarkX = 32;
    ca2HeroAction = 0;
    ca2DarkAction = 0;
    ca2HeroHP = 100;
    ca2DarkHP = 100;

    pintaShipY = 16;
    pintaScrollX = 0;
    pintaGold = 150;
    pintaFish = 4;
    pintaAction = 0;
    pirateShipX = 52;
    pirateShipY = 14;

    psFighterX = 12;
    psEnemyX = 34;
    psStones = 1;
    psSuperModeTimer = 0;
    psAction = 0;

    godzX = 10;
    godzAction = 0;
    godzDestruction = 35;
    tankX = 42;

    zombPlayerX = 10;
    zombEnemyX = 42;
    zombAmmo = 6;
    zombAction = 0;
    zombScore = 480;
    zombWave = 1;

    mouseDown = false;
    osdBannerText = "Sega Dreamcast VMU Ready - Press F2 to Cycle Games";
    osdBannerTimer = 180;
}

bool SegaVMUMachine::loadGameByIndex(int index) {
    if (index < 0 || index >= EmbeddedVMUGames::GAME_COUNT) return false;
    currentGameIndex = index;

    size_t gsize = 0;
    const uint8* gdata = EmbeddedVMUGames::getGameData(index, gsize);
    if (gdata && gsize > 0) {
        size_t copyLen = std::min(gsize, flashMemory.size());
        std::memcpy(flashMemory.data(), gdata, copyLen);
    }

    const auto* info = EmbeddedVMUGames::getGameInfo(index);
    if (info) {
        currentMedia = info->title;
        osdBannerText = std::string("[VMS] ") + info->title + " - " + info->author;
        osdBannerTimer = 220;
        std::cout << "[SEGA VMU] Loaded Encrypted Game: " << info->title << " (" << gsize << " bytes)\n";
    }

    playBuzzerTone(1500, 8);
    icons = 0x0A; // Game (0x02) + Flash access (0x08)
    return true;
}

void SegaVMUMachine::cycleNextGame() {
    int nextIdx = (currentGameIndex + 1) % EmbeddedVMUGames::GAME_COUNT;
    loadGameByIndex(nextIdx);
}

bool SegaVMUMachine::loadMedia(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;

    std::vector<uint8> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (buffer.empty()) return false;

    size_t copyLen = std::min(buffer.size(), flashMemory.size());
    std::memcpy(flashMemory.data(), buffer.data(), copyLen);

    currentMedia = path.substr(path.find_last_of("/\\") + 1);
    osdBannerText = "Loaded Custom VMS: " + currentMedia;
    osdBannerTimer = 200;
    playBuzzerTone(1200, 10);
    std::cout << "[SEGA VMU] Loaded custom VMS: " << currentMedia << " (" << buffer.size() << " bytes)\n";
    return true;
}

void SegaVMUMachine::stepInstruction() {
    // Sanyo Potato instruction interpreter
    uint8 op = biosRom[PC++];
    switch (op) {
        case 0x00: break; // NOP
        case 0x01: ACC = biosRom[PC++]; break; // LD A, #imm
        case 0x02: ACC++; if (ACC == 0) PSW |= 0x02; else PSW &= ~0x02; break; // INC A
        case 0x03: ACC--; if (ACC == 0) PSW |= 0x02; else PSW &= ~0x02; break; // DEC A
        case 0x04: PC = (biosRom[PC] << 8) | biosRom[PC + 1]; break; // JMP
        case 0x05: if (PSW & 0x02) PC = (biosRom[PC] << 8) | biosRom[PC + 1]; else PC += 2; break; // JZ
        default: break;
    }
}

void SegaVMUMachine::setLcdPixel(int x, int y, bool on) {
    if (x < 0 || x >= 48 || y < 0 || y >= 32) return;
    int byteIdx = y * 6 + (x / 8);
    uint8 mask = 0x80 >> (x % 8);
    if (on) lcdBuffer[byteIdx] |= mask;
    else    lcdBuffer[byteIdx] &= ~mask;
}

bool SegaVMUMachine::getLcdPixel(int x, int y) const {
    if (x < 0 || x >= 48 || y < 0 || y >= 32) return false;
    int byteIdx = y * 6 + (x / 8);
    return (lcdBuffer[byteIdx] & (0x80 >> (x % 8))) != 0;
}

void SegaVMUMachine::drawLcdSprite(int x, int y, int w, int h, const uint8* data) {
    int bytesPerRow = (w + 7) / 8;
    for (int r = 0; r < h; r++) {
        for (int c = 0; c < w; c++) {
            uint8 b = data[r * bytesPerRow + (c / 8)];
            if (b & (0x80 >> (c % 8))) {
                setLcdPixel(x + c, y + r, true);
            }
        }
    }
}

void SegaVMUMachine::drawLcdString(int x, int y, const std::string& str) {
    for (size_t i = 0; i < str.size(); i++) {
        char c = str[i];
        int px = x + (int)i * 4;
        if (px >= 46) break;
        // 3x5 compact bitmap font for 48x32 LCD
        if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
        uint16 glyph = 0;
        if (c >= '0' && c <= '9') {
            static const uint16 numGlyphs[10] = {
                0x7B6F, 0x2492, 0x73E7, 0x73CF, 0x5BC9, 0x79CF, 0x79EF, 0x7249, 0x7BCF, 0x7BC9
            };
            glyph = numGlyphs[c - '0'];
        } else {
            glyph = 0x5555; // Placeholder
        }
        for (int r = 0; r < 5; r++) {
            for (int col = 0; col < 3; col++) {
                if ((glyph >> (14 - (r * 3 + col))) & 1) {
                    setLcdPixel(px + col, y + r, true);
                }
            }
        }
    }
}

void SegaVMUMachine::playBuzzerTone(int freq, int durationTicks) {
    buzzerFreq = freq;
    buzzerTimer = durationTicks;
}

void SegaVMUMachine::updateGameLogic() {
    // Clear 48x32 LCD buffer
    std::memset(lcdBuffer, 0, sizeof(lcdBuffer));

    // Flash icon decay
    if (icons & 0x08) {
        if (animTick % 20 == 0) icons &= ~0x08;
    }

    if (currentApp == 1) {
        // ==========================================
        // 1. CLOCK / CALENDAR MODE
        // ==========================================
        icons = 0x04; // Clock icon
        // Draw clock frame
        for (int x = 2; x < 46; x++) { setLcdPixel(x, 2, true); setLcdPixel(x, 29, true); }
        for (int y = 2; y < 30; y++) { setLcdPixel(2, y, true); setLcdPixel(45, y, true); }

        // Clock time display: 12:34
        drawLcdString(8, 8, "12:34");

        // Dancing mini Chao
        int chaoFrame = (animTick / 10) % 4;
        static const uint8 miniChao[8] = {
            0b01100000, 0b11110000, 0b10010000, 0b11110000,
            0b01100000, 0b01100000, 0b10010000, 0b00000000
        };
        drawLcdSprite(20 + (chaoFrame % 2 ? 1 : -1), 16, 8, 8, miniChao);
        return;
    }

    if (currentApp == 2) {
        // ==========================================
        // 2. VMS FILE / MEMORY CARD BROWSER MODE
        // ==========================================
        icons = 0x01; // File icon
        const auto* info = EmbeddedVMUGames::getGameInfo(currentGameIndex);
        std::string sname = info ? info->shortName : "VMS GAME";

        // Draw top header
        for (int x = 0; x < 48; x++) setLcdPixel(x, 6, true);

        // Draw mini VMS icon (8x8)
        static const uint8 vmsIcon[8] = {
            0b01111110, 0b10000001, 0b10111101, 0b10111101,
            0b10111101, 0b10000001, 0b11111111, 0b00000000
        };
        drawLcdSprite(4, 10, 8, 8, vmsIcon);

        // Draw file name
        drawLcdString(16, 11, sname.substr(0, 7));

        // Draw block info
        for (int x = 4; x < 44; x += 3) {
            setLcdPixel(x, 24, true);
            setLcdPixel(x + 1, 24, true);
        }
        drawLcdString(4, 26, "32/200");
        return;
    }

    // ==========================================
    // 3. MINIGAME APPLICATION (currentApp == 0)
    // ==========================================
    icons = 0x02; // Game icon

    int btn = ~buttonState; // Active high buttons

    switch (currentGameIndex) {
        case 0: {
            // ----------------------------------------------------
            // GAME 0: CHAO ADVENTURE (Sonic Adventure)
            // ----------------------------------------------------
            if (btn & 0x01) { // A button: Feed apple
                chaoAction = 1;
                chaoActionTimer = 30;
                playBuzzerTone(1400, 4);
            } else if (btn & 0x02) { // B button: Pet Chao
                chaoAction = 2;
                chaoActionTimer = 35;
                playBuzzerTone(1800, 5);
            } else if (btn & 0x40) { // Left
                chaoX = std::max(2, chaoX - 1);
                chaoDir = -1;
            } else if (btn & 0x80) { // Right
                chaoX = std::min(32, chaoX + 1);
                chaoDir = 1;
            }

            if (chaoActionTimer > 0) {
                chaoActionTimer--;
                if (chaoActionTimer == 0) chaoAction = 0;
            }

            // Draw ground line
            for (int x = 0; x < 48; x++) setLcdPixel(x, 26, (x % 2 == 0));

            // Animated Chao walking / standing
            int walkBob = (animTick / 8) % 2;
            int cy = 12 + (chaoAction == 2 ? -2 : walkBob);

            // Chao 12x13 Sprite
            static const uint8 chaoWalk[13 * 2] = {
                0b00111100, 0b00000000,
                0b01111110, 0b00000000,
                0b11011011, 0b00000000,
                0b11111111, 0b00000000,
                0b11111111, 0b00000000,
                0b01111110, 0b00000000,
                0b00111100, 0b00000000,
                0b01011010, 0b00000000,
                0b11111111, 0b00000000,
                0b10111101, 0b00000000,
                0b00111100, 0b00000000,
                0b01100110, 0b00000000,
                0b01100110, 0b00000000
            };
            drawLcdSprite(chaoX, cy, 12, 13, chaoWalk);

            // Floating Chao Ball / Halo
            int haloY = cy - 4 + ((animTick / 10) % 2);
            setLcdPixel(chaoX + 5, haloY, true);
            setLcdPixel(chaoX + 6, haloY, true);
            setLcdPixel(chaoX + 5, haloY + 1, true);
            setLcdPixel(chaoX + 6, haloY + 1, true);

            // Floating Hearts or Apple
            if (chaoAction == 2) {
                // Heart icon
                int hx = chaoX + 11;
                int hy = cy - 3;
                setLcdPixel(hx + 1, hy, true); setLcdPixel(hx + 3, hy, true);
                setLcdPixel(hx, hy + 1, true); setLcdPixel(hx + 2, hy + 1, true); setLcdPixel(hx + 4, hy + 1, true);
                setLcdPixel(hx + 1, hy + 2, true); setLcdPixel(hx + 3, hy + 2, true);
                setLcdPixel(hx + 2, hy + 3, true);
            } else if (chaoAction == 1) {
                // Apple
                int ax = chaoX + (chaoDir > 0 ? 12 : -5);
                setLcdPixel(ax + 1, cy + 6, true);
                setLcdPixel(ax, cy + 7, true); setLcdPixel(ax + 1, cy + 7, true); setLcdPixel(ax + 2, cy + 7, true);
                setLcdPixel(ax + 1, cy + 8, true);
            }
            break;
        }

        case 1: {
            // ----------------------------------------------------
            // GAME 1: CHAO ADVENTURE 2 (Karate Fighter)
            // ----------------------------------------------------
            if (btn & 0x01) { // A: Punch
                ca2HeroAction = 1;
                ca2ActionTimer = 16;
                playBuzzerTone(1200, 3);
            } else if (btn & 0x02) { // B: High Kick
                ca2HeroAction = 2;
                ca2ActionTimer = 20;
                playBuzzerTone(1600, 4);
            } else if (btn & 0x40) { // Left
                ca2HeroX = std::max(2, ca2HeroX - 1);
            } else if (btn & 0x80) { // Right
                ca2HeroX = std::min(ca2DarkX - 10, ca2HeroX + 1);
            }

            if (ca2ActionTimer > 0) {
                ca2ActionTimer--;
                if (ca2ActionTimer == 0) ca2HeroAction = 0;
            }

            // Ground line
            for (int x = 0; x < 48; x++) setLcdPixel(x, 26, true);

            // Hero Chao (Left)
            static const uint8 heroFighter[10] = {
                0b01110000, 0b10101000, 0b11111000, 0b01110000,
                0b01110000, 0b11111000, 0b01110000, 0b01010000,
                0b01010000, 0b11011000
            };
            drawLcdSprite(ca2HeroX, 16, 8, 10, heroFighter);
            // Punch arm
            if (ca2HeroAction == 1) {
                for (int dx = 0; dx < 6; dx++) setLcdPixel(ca2HeroX + 6 + dx, 19, true);
            } else if (ca2HeroAction == 2) {
                for (int dx = 0; dx < 6; dx++) setLcdPixel(ca2HeroX + 6 + dx, 16 - dx/2, true);
            }

            // Dark Chao (Right)
            drawLcdSprite(ca2DarkX, 16, 8, 10, heroFighter);
            // Horns for Dark Chao
            setLcdPixel(ca2DarkX + 1, 14, true);
            setLcdPixel(ca2DarkX + 5, 14, true);
            break;
        }

        case 2: {
            // ----------------------------------------------------
            // GAME 2: PINTA'S QUEST (Skies of Arcadia)
            // ----------------------------------------------------
            pintaScrollX++;
            if (btn & 0x10) pintaShipY = std::max(4, pintaShipY - 1); // Up
            if (btn & 0x20) pintaShipY = std::min(24, pintaShipY + 1); // Down
            if (btn & 0x01) { // A: Fire cannon
                pintaAction = 1;
                pintaActionTimer = 18;
                playBuzzerTone(800, 5);
            } else if (btn & 0x02) { // B: Fish net
                pintaAction = 2;
                pintaActionTimer = 22;
                playBuzzerTone(1300, 4);
            }

            if (pintaActionTimer > 0) {
                pintaActionTimer--;
                if (pintaActionTimer == 0) pintaAction = 0;
            }

            // Scrolling clouds
            for (int i = 0; i < 3; i++) {
                int cx = (i * 20 - (pintaScrollX / 2)) % 60;
                if (cx < 0) cx += 60;
                if (cx < 44) {
                    setLcdPixel(cx, 6, true); setLcdPixel(cx + 1, 6, true); setLcdPixel(cx + 2, 6, true);
                    setLcdPixel(cx - 1, 7, true); setLcdPixel(cx + 3, 7, true);
                }
            }

            // Pinta Airship (10x7)
            static const uint8 airship[7 * 2] = {
                0b00011000, 0b00000000,
                0b01111110, 0b00000000,
                0b11111111, 0b00000000,
                0b01111110, 0b00000000,
                0b00111100, 0b00000000,
                0b01011010, 0b00000000,
                0b01111110, 0b00000000
            };
            drawLcdSprite(8, pintaShipY, 10, 7, airship);

            // Cannon bullet
            if (pintaAction == 1) {
                int bx = 18 + (18 - pintaActionTimer) * 2;
                if (bx < 46) {
                    setLcdPixel(bx, pintaShipY + 3, true);
                    setLcdPixel(bx + 1, pintaShipY + 3, true);
                }
            }

            // Pirate Ship on right
            drawLcdSprite(36, 14, 10, 7, airship);
            break;
        }

        case 3: {
            // ----------------------------------------------------
            // GAME 3: POWER STONE (Capcom)
            // ----------------------------------------------------
            if (btn & 0x01) { // A: Punch
                psAction = 1;
                psActionTimer = 16;
                playBuzzerTone(1500, 4);
            } else if (btn & 0x02) { // B: Super Fusion
                psAction = 2;
                psActionTimer = 24;
                playBuzzerTone(2000, 8);
            } else if (btn & 0x40) {
                psFighterX = std::max(2, psFighterX - 1);
            } else if (btn & 0x80) {
                psFighterX = std::min(psEnemyX - 8, psFighterX + 1);
            }

            if (psActionTimer > 0) {
                psActionTimer--;
                if (psActionTimer == 0) psAction = 0;
            }

            // Platform
            for (int x = 2; x < 46; x++) setLcdPixel(x, 26, true);

            // Fighter 1
            static const uint8 brawler[9] = {
                0b01110000, 0b10101000, 0b11111000, 0b01110000,
                0b11111000, 0b10101000, 0b01110000, 0b01010000, 0b11011000
            };
            drawLcdSprite(psFighterX, 17, 8, 9, brawler);
            drawLcdSprite(psEnemyX, 17, 8, 9, brawler);

            // Glowing Power Stones on HUD top
            for (int i = 0; i < 3; i++) {
                int sx = 4 + i * 5;
                setLcdPixel(sx + 1, 3, true);
                setLcdPixel(sx, 4, true); setLcdPixel(sx + 2, 4, true);
                setLcdPixel(sx + 1, 5, true);
            }

            // Super beam
            if (psAction == 2) {
                for (int x = psFighterX + 8; x < 46; x++) {
                    setLcdPixel(x, 20, true);
                    setLcdPixel(x, 21, true);
                }
            }
            break;
        }

        case 4: {
            // ----------------------------------------------------
            // GAME 4: GODZILLA: GENERATIONS (Sega)
            // ----------------------------------------------------
            if (btn & 0x01) { // A: Atomic Ray
                godzAction = 1;
                godzActionTimer = 22;
                playBuzzerTone(900, 7);
            } else if (btn & 0x02) { // B: Tail Whip
                godzAction = 2;
                godzActionTimer = 16;
                playBuzzerTone(600, 5);
            } else if (btn & 0x40) {
                godzX = std::max(2, godzX - 1);
            } else if (btn & 0x80) {
                godzX = std::min(26, godzX + 1);
            }

            if (godzActionTimer > 0) {
                godzActionTimer--;
                if (godzActionTimer == 0) godzAction = 0;
            }

            // City Buildings on ground
            for (int b = 0; b < 4; b++) {
                int bx = 28 + b * 5;
                int bh = 6 + (b % 3) * 4;
                for (int by = 26 - bh; by < 26; by++) {
                    setLcdPixel(bx, by, true);
                    setLcdPixel(bx + 3, by, true);
                }
                for (int w = 0; w < 4; w++) setLcdPixel(bx + w, 26 - bh, true);
            }
            for (int x = 0; x < 48; x++) setLcdPixel(x, 26, true);

            // Giant Godzilla Silhouette (12x14)
            static const uint8 godzilla[14 * 2] = {
                0b00011100, 0b00000000,
                0b00111110, 0b00000000,
                0b01101111, 0b00000000,
                0b01111110, 0b00000000,
                0b00111100, 0b00000000,
                0b01111110, 0b00000000,
                0b11111111, 0b00000000,
                0b11111111, 0b00000000,
                0b01111110, 0b00000000,
                0b00111100, 0b00000000,
                0b01100110, 0b00000000,
                0b01100110, 0b00000000,
                0b11100111, 0b00000000,
                0b11100111, 0b00000000
            };
            drawLcdSprite(godzX, 12, 12, 14, godzilla);

            // Atomic Breath Ray
            if (godzAction == 1) {
                for (int x = godzX + 11; x < 48; x++) {
                    int ry = 14 + ((x + animTick) % 3 == 0 ? 1 : 0);
                    setLcdPixel(x, ry, true);
                    setLcdPixel(x, ry + 1, true);
                }
            }
            break;
        }

        case 5: {
            // ----------------------------------------------------
            // GAME 5: ZOMBIE REVENGE (AM4 / House of the Dead)
            // ----------------------------------------------------
            if (btn & 0x01) { // A: Shoot Gun
                if (zombAmmo > 0) {
                    zombAmmo--;
                    zombAction = 1;
                    zombActionTimer = 14;
                    playBuzzerTone(2200, 3);
                } else {
                    zombAmmo = 6; // Reload
                    playBuzzerTone(700, 6);
                }
            } else if (btn & 0x02) { // B: Melee Kick
                zombAction = 2;
                zombActionTimer = 18;
                playBuzzerTone(1200, 4);
            } else if (btn & 0x40) {
                zombPlayerX = std::max(2, zombPlayerX - 1);
            } else if (btn & 0x80) {
                zombPlayerX = std::min(zombEnemyX - 8, zombPlayerX + 1);
            }

            if (zombActionTimer > 0) {
                zombActionTimer--;
                if (zombActionTimer == 0) zombAction = 0;
            }

            // Ground
            for (int x = 0; x < 48; x++) setLcdPixel(x, 26, true);

            // Player Agent (10x10)
            static const uint8 agent[10] = {
                0b01110000, 0b10101000, 0b11111000, 0b01110000,
                0b01110000, 0b11111000, 0b01110000, 0b01010000, 0b01010000, 0b11011000
            };
            drawLcdSprite(zombPlayerX, 16, 8, 10, agent);

            // Gunshot Bullet trace
            if (zombAction == 1) {
                for (int x = zombPlayerX + 8; x < 44; x += 2) {
                    setLcdPixel(x, 19, true);
                }
            }

            // Zombie shambling forward
            static const uint8 zombie[10] = {
                0b00111000, 0b01010100, 0b01111000, 0b00110000,
                0b11111100, 0b10110100, 0b00110000, 0b00110000, 0b01001000, 0b11001100
            };
            int zx = zombEnemyX + ((animTick / 16) % 2);
            drawLcdSprite(zx, 16, 8, 10, zombie);

            // Ammo counter (bullets on top left)
            for (int i = 0; i < zombAmmo; i++) {
                setLcdPixel(4 + i * 2, 4, true);
            }
            break;
        }

        default: break;
    }
}

void SegaVMUMachine::runFrame() {
    animTick++;

    // Execute Potato instructions
    for (int i = 0; i < 400; i++) {
        stepInstruction();
    }

    // Interactive minigame LCD logic
    updateGameLogic();

    // Buzzer timer decay
    if (buzzerTimer > 0) buzzerTimer--;
    if (osdBannerTimer > 0) osdBannerTimer--;

    renderVMUScreen();
    updateAudio();
}

void SegaVMUMachine::renderVMUScreen() {
    // Fill background with elegant studio dark backdrop
    std::fill(screenBuffer.begin(), screenBuffer.end(), 0xFF121620);

    // Subtle radial / spotlight gradient in the center
    for (int y = 0; y < CANVAS_H; y++) {
        int dy = y - 256;
        for (int x = 0; x < CANVAS_W; x++) {
            int dx = x - 320;
            int distSq = dx * dx + dy * dy;
            if (distSq < 160000) {
                int glow = (160000 - distSq) / 9000;
                uint32 bg = 0xFF121620;
                uint8 r = std::min(255, (int)((bg >> 16) & 0xFF) + glow / 2);
                uint8 g = std::min(255, (int)((bg >> 8) & 0xFF) + glow / 2);
                uint8 b = std::min(255, (int)(bg & 0xFF) + glow);
                screenBuffer[y * CANVAS_W + x] = (0xFF << 24) | (r << 16) | (g << 8) | b;
            }
        }
    }

    // Draw the stunning VMU hardware casing
    renderCasing();

    // Draw active LCD matrix inside the screen window
    renderLCDMatrix();

    // OSD Banner for game title or notifications
    if (osdBannerTimer > 0) {
        int bannerY = 480;
        GUIFont::fillRect(screenBuffer.data(), CANVAS_W, 40, bannerY - 4, 560, 24, 0xDD0D1117);
        GUIFont::drawRect(screenBuffer.data(), CANVAS_W, 40, bannerY - 4, 560, 24, 0xFF354256);
        GUIFont::drawString(screenBuffer.data(), CANVAS_W, 52, bannerY, osdBannerText, 0xFFE6EDF3);
    }
}

void SegaVMUMachine::renderCasing() {
    // VMU Body Geometry: X: 155..485 (width 330), Y: 22..488 (height 466)
    const int bx0 = 155, bx1 = 485;
    const int by0 = 22,  by1 = 488;

    // 1. VMU Drop Shadow onto backdrop
    for (int y = by0 + 10; y <= by1 + 14; y++) {
        for (int x = bx0 - 6; x <= bx1 + 10; x++) {
            if (y >= 0 && y < CANVAS_H && x >= 0 && x < CANVAS_W) {
                uint32 cur = screenBuffer[y * CANVAS_W + x];
                uint8 r = ((cur >> 16) & 0xFF) * 0.45;
                uint8 g = ((cur >> 8) & 0xFF) * 0.45;
                uint8 b = (cur & 0xFF) * 0.45;
                screenBuffer[y * CANVAS_W + x] = 0xFF000000 | (r << 16) | (g << 8) | b;
            }
        }
    }

    // 2. VMU Body Plastic with realistic 3D bevels
    for (int y = by0; y <= by1; y++) {
        for (int x = bx0; x <= bx1; x++) {
            // Rounded corners on body
            int cornerDist = 0;
            if (x < bx0 + 24 && y < by0 + 24) {
                int dx = bx0 + 24 - x, dy = by0 + 24 - y;
                cornerDist = dx * dx + dy * dy;
                if (cornerDist > 24 * 24) continue;
            } else if (x > bx1 - 24 && y < by0 + 24) {
                int dx = x - (bx1 - 24), dy = by0 + 24 - y;
                cornerDist = dx * dx + dy * dy;
                if (cornerDist > 24 * 24) continue;
            } else if (x < bx0 + 20 && y > by1 - 20) {
                int dx = bx0 + 20 - x, dy = y - (by1 - 20);
                cornerDist = dx * dx + dy * dy;
                if (cornerDist > 20 * 20) continue;
            } else if (x > bx1 - 20 && y > by1 - 20) {
                int dx = x - (bx1 - 20), dy = y - (by1 - 20);
                cornerDist = dx * dx + dy * dy;
                if (cornerDist > 20 * 20) continue;
            }

            // Authentic Sega Dreamcast off-white / platinum matte plastic
            // Top to bottom subtle vertical gradient: 0xFFEBF0F8 -> 0xFFD8DDE6
            float grad = (float)(y - by0) / (by1 - by0);
            int baseR = 235 - (int)(grad * 20.0f);
            int baseG = 240 - (int)(grad * 20.0f);
            int baseB = 248 - (int)(grad * 22.0f);

            // Bevel highlights & shadows
            if (x < bx0 + 3 || y < by0 + 3) {
                baseR = std::min(255, baseR + 25);
                baseG = std::min(255, baseG + 25);
                baseB = std::min(255, baseB + 25);
            } else if (x > bx1 - 4 || y > by1 - 4) {
                baseR = (int)(baseR * 0.78f);
                baseG = (int)(baseG * 0.78f);
                baseB = (int)(baseB * 0.82f);
            }

            screenBuffer[y * CANVAS_W + x] = 0xFF000000 | (baseR << 16) | (baseG << 8) | baseB;
        }
    }

    // 3. Top Connector Cap & Seam Line (Y: 22..68)
    for (int x = bx0 + 4; x <= bx1 - 4; x++) {
        // Horizontal separation groove at Y=68
        screenBuffer[68 * CANVAS_W + x] = 0xFF969DAE;
        screenBuffer[69 * CANVAS_W + x] = 0xFFB8C0D0;
    }
    // Controller locking notch slots (at X: 215..235, Y: 26..34 and X: 405..425, Y: 26..34)
    auto drawSlot = [&](int sx, int sy, int sw, int sh) {
        for (int y = sy; y < sy + sh; y++) {
            for (int x = sx; x < sx + sw; x++) {
                uint32 c = (y == sy || x == sx) ? 0xFF585F6E : 0xFF7A8394;
                screenBuffer[y * CANVAS_W + x] = c;
            }
        }
    };
    drawSlot(215, 26, 20, 8);
    drawSlot(405, 26, 20, 8);

    // Lanyard Strap Loop Hole at top-left (X: 182, Y: 42)
    for (int dy = -7; dy <= 7; dy++) {
        for (int dx = -5; dx <= 5; dx++) {
            int d = dx * dx + dy * dy;
            if (d <= 25) {
                uint32 holeCol = (dx < 0 || dy < 0) ? 0xFF4A5160 : 0xFF70798C;
                screenBuffer[(42 + dy) * CANVAS_W + 182 + dx] = holeCol;
            }
        }
    }

    // 4. Ergonomic Side Grip Ribs (Y: 260..340)
    for (int i = 0; i < 4; i++) {
        int ry = 265 + i * 18;
        for (int w = 0; w < 10; w++) {
            screenBuffer[ry * CANVAS_W + bx0 + 5 + w] = 0xFF8C94A6;
            screenBuffer[(ry + 1) * CANVAS_W + bx0 + 5 + w] = 0xFFF0F4FA;

            screenBuffer[ry * CANVAS_W + bx1 - 15 + w] = 0xFF8C94A6;
            screenBuffer[(ry + 1) * CANVAS_W + bx1 - 15 + w] = 0xFFF0F4FA;
        }
    }

    // 5. Smoked Acrylic LCD Lens Window (X: 184..456, Y: 80..290)
    const int wx0 = 184, wx1 = 456;
    const int wy0 = 80,  wy1 = 290;

    for (int y = wy0; y <= wy1; y++) {
        for (int x = wx0; x <= wx1; x++) {
            // Rounded corners on window
            if ((x < wx0 + 12 && y < wy0 + 12 && (wx0 + 12 - x)*(wx0 + 12 - x) + (wy0 + 12 - y)*(wy0 + 12 - y) > 144) ||
                (x > wx1 - 12 && y < wy0 + 12 && (x - (wx1 - 12))*(x - (wx1 - 12)) + (wy0 + 12 - y)*(wy0 + 12 - y) > 144) ||
                (x < wx0 + 12 && y > wy1 - 12 && (wx0 + 12 - x)*(wx0 + 12 - x) + (y - (wy1 - 12))*(y - (wy1 - 12)) > 144) ||
                (x > wx1 - 12 && y > wy1 - 12 && (x - (wx1 - 12))*(x - (wx1 - 12)) + (y - (wy1 - 12))*(y - (wy1 - 12)) > 144)) {
                continue;
            }

            // Dark smoked grey acrylic with inner bevel
            uint32 winCol = 0xFF252932;
            if (x == wx0 || y == wy0 || x == wx0 + 1 || y == wy0 + 1) winCol = 0xFF14171E; // Inner shadow top/left
            else if (x >= wx1 - 1 || y >= wy1 - 1) winCol = 0xFF424A58; // Inner highlight bottom/right
            screenBuffer[y * CANVAS_W + x] = winCol;
        }
    }

    // 6. Dreamcast Swirl Logo & Brand Text above LCD (Y: 92..102)
    // Draw iconic orange spiral swirl at X: 242, Y: 94
    const int swirlX = 244, swirlY = 96;
    for (int dy = -6; dy <= 6; dy++) {
        for (int dx = -6; dx <= 6; dx++) {
            int d = dx * dx + dy * dy;
            if (d >= 8 && d <= 36) {
                float angle = atan2f((float)dy, (float)dx);
                if (angle < 0) angle += 6.28318f;
                // Spiral arm mask
                float r = sqrtf((float)d);
                if (fabsf(r - (angle * 0.8f + 1.8f)) < 1.4f || fabsf(r - (angle * 0.8f + 2.8f)) < 1.4f) {
                    screenBuffer[(swirlY + dy) * CANVAS_W + swirlX + dx] = 0xFFFF7800; // Dreamcast vibrant orange
                }
            }
        }
    }
    // "Dreamcast" text
    GUIFont::drawString(screenBuffer.data(), CANVAS_W, 258, 93, "Dreamcast", 0xFF9CA5B4);

    // 7. Top LCD Status Icons ([FILE], [GAME], [CLOCK], [FLASH]) at Y: 106
    auto drawStatusIcon = [&](int ix, int iy, int type, bool active) {
        uint32 fg = active ? 0xFF0078D7 : 0xFF4A5364; // Glow blue when active
        if (type == 0) { // [FILE] Folder
            for (int dx = 0; dx < 14; dx++) {
                screenBuffer[iy * CANVAS_W + ix + dx] = fg;
                screenBuffer[(iy + 7) * CANVAS_W + ix + dx] = fg;
            }
            for (int dy = 0; dy < 8; dy++) {
                screenBuffer[(iy + dy) * CANVAS_W + ix] = fg;
                screenBuffer[(iy + dy) * CANVAS_W + ix + 13] = fg;
            }
            screenBuffer[(iy + 2) * CANVAS_W + ix + 3] = fg;
            screenBuffer[(iy + 2) * CANVAS_W + ix + 4] = fg;
        } else if (type == 1) { // [GAME] Controller D-pad
            for (int dy = 0; dy < 8; dy++) {
                for (int dx = 0; dx < 14; dx++) {
                    if ((dx >= 5 && dx <= 8) || (dy >= 2 && dy <= 5)) {
                        screenBuffer[(iy + dy) * CANVAS_W + ix + dx] = fg;
                    }
                }
            }
        } else if (type == 2) { // [CLOCK] Clock face
            for (int dy = -3; dy <= 3; dy++) {
                for (int dx = -3; dx <= 3; dx++) {
                    if (dx * dx + dy * dy <= 12) {
                        bool hand = (dx == 0 && dy <= 0) || (dy == 0 && dx >= 0);
                        screenBuffer[(iy + 3 + dy) * CANVAS_W + ix + 6 + dx] = hand ? 0xFFFFFFFF : fg;
                    }
                }
            }
        } else if (type == 3) { // [FLASH] Lightning
            static const uint8 bolt[7] = {
                0b00100000, 0b01100000, 0b11111000, 0b00110000, 0b01100000, 0b01000000, 0b00000000
            };
            for (int dy = 0; dy < 7; dy++) {
                for (int dx = 0; dx < 6; dx++) {
                    if (bolt[dy] & (0x80 >> dx)) {
                        screenBuffer[(iy + dy) * CANVAS_W + ix + 4 + dx] = active ? 0xFFFFCC00 : fg;
                    }
                }
            }
        }
    };
    drawStatusIcon(210, 106, 0, (icons & 0x01) != 0); // File
    drawStatusIcon(270, 106, 1, (icons & 0x02) != 0); // Game
    drawStatusIcon(330, 106, 2, (icons & 0x04) != 0); // Clock
    drawStatusIcon(390, 106, 3, (icons & 0x08) != 0); // Flash

    // 8. Directional D-Pad (Center: X: 230, Y: 362)
    const int dpadCX = 230, dpadCY = 362;
    int btnActive = ~buttonState;

    for (int dy = -32; dy <= 32; dy++) {
        for (int dx = -32; dx <= 32; dx++) {
            bool inCross = (abs(dx) <= 11 && abs(dy) <= 30) || (abs(dy) <= 11 && abs(dx) <= 30);
            if (inCross) {
                // 3D D-pad shading
                uint32 dpadCol = 0xFF353A45;
                if (dy < -26 || dx < -26 || (dy < 0 && abs(dx) <= 10)) dpadCol = 0xFF4D5463; // Highlight
                else if (dy > 26 || dx > 26) dpadCol = 0xFF232730; // Shadow

                // Active pressed feedback
                if ((dy < -10 && (btnActive & 0x10)) || (dy > 10 && (btnActive & 0x20)) ||
                    (dx < -10 && (btnActive & 0x40)) || (dx > 10 && (btnActive & 0x80))) {
                    dpadCol = 0xFF1E222A;
                }

                // Central concave thumb dish
                int rsq = dx * dx + dy * dy;
                if (rsq <= 144) {
                    dpadCol = (rsq <= 100) ? 0xFF272A32 : 0xFF30343E;
                }

                screenBuffer[(dpadCY + dy) * CANVAS_W + dpadCX + dx] = dpadCol;
            }
        }
    }

    // 9. A and B Action Buttons (A at X: 405, Y: 375; B at X: 442, Y: 342)
    auto drawActionButton = [&](int cx, int cy, char label, bool pressed) {
        int rad = 17;
        for (int dy = -rad; dy <= rad; dy++) {
            for (int dx = -rad; dx <= rad; dx++) {
                int d = dx * dx + dy * dy;
                if (d <= rad * rad) {
                    uint32 col = 0xFF22488A; // Authentic Dreamcast Blue
                    if (pressed) {
                        col = 0xFF142B54;
                    } else if (dx < -3 || dy < -3) {
                        col = 0xFF3667B8; // Top/left glossy highlight
                    } else if (dx > 4 || dy > 4) {
                        col = 0xFF163264; // Bottom/right shadow
                    }
                    screenBuffer[(cy + dy) * CANVAS_W + cx + dx] = col;
                }
            }
        }
        // White Letter
        GUIFont::drawChar(screenBuffer.data(), CANVAS_W, cx - 3, cy - 4, label, 0xFFFFFFFF);
    };
    drawActionButton(405, 375, 'A', (btnActive & 0x01) != 0);
    drawActionButton(442, 342, 'B', (btnActive & 0x02) != 0);

    // 10. MODE and SLEEP Oval Buttons
    auto drawPillButton = [&](int cx, int cy, const std::string& label, bool pressed) {
        for (int dy = -5; dy <= 5; dy++) {
            for (int dx = -10; dx <= 10; dx++) {
                if (dx * dx / 100.0f + dy * dy / 25.0f <= 1.0f) {
                    uint32 col = pressed ? 0xFF1A1C22 : 0xFF363A44;
                    if (!pressed && dy < -2) col = 0xFF4A4F5C;
                    screenBuffer[(cy + dy) * CANVAS_W + cx + dx] = col;
                }
            }
        }
        GUIFont::drawString(screenBuffer.data(), CANVAS_W, cx - 11, cy + 9, label, 0xFF8A93A4);
    };
    drawPillButton(312, 395, "MODE", (btnActive & 0x04) != 0);
    drawPillButton(348, 395, "SLEEP", (btnActive & 0x08) != 0);

    // 11. Piezo Speaker Grille (6 Holes near X: 330, Y: 438)
    for (int row = 0; row < 2; row++) {
        for (int col = 0; col < 3; col++) {
            int hx = 318 + col * 12;
            int hy = 434 + row * 10;
            for (int dy = -2; dy <= 2; dy++) {
                for (int dx = -2; dx <= 2; dx++) {
                    if (dx * dx + dy * dy <= 4) {
                        screenBuffer[(hy + dy) * CANVAS_W + hx + dx] = 0xFF1E222A;
                    }
                }
            }
            screenBuffer[(hy + 2) * CANVAS_W + hx + 1] = 0xFFFFFFFF; // Specular rim
        }
    }

    // 12. SEGA Logo on Chin (X: 320, Y: 468)
    GUIFont::drawString(screenBuffer.data(), CANVAS_W, 308, 466, "SEGA", 0xFF0066CC);
}

void SegaVMUMachine::renderLCDMatrix() {
    // 48 x 32 LCD Display active area scaled 5x (240 x 160)
    // Centered at X: 200, Y: 118
    const int lcdX0 = 200;
    const int lcdY0 = 118;
    const int scale = 5;

    // Authentic Sega VMU STN LCD colors
    const uint32 lcdBackdrop = 0xFF8FA886; // Passive greenish LCD background
    const uint32 lcdGridGap  = 0xFF859D7C; // Pixel separator grid line
    const uint32 lcdPixelOn  = 0xFF182515; // Dark liquid crystal active dot
    const uint32 lcdPixelOff = 0xFF88A07F; // Faint unpowered dot

    // Fill screen background
    for (int y = 0; y < 32 * scale; y++) {
        int py = lcdY0 + y;
        for (int x = 0; x < 48 * scale; x++) {
            int px = lcdX0 + x;
            screenBuffer[py * CANVAS_W + px] = lcdBackdrop;
        }
    }

    // Draw 48x32 matrix pixels
    for (int y = 0; y < 32; y++) {
        int py = lcdY0 + y * scale;
        for (int x = 0; x < 48; x++) {
            int px = lcdX0 + x * scale;
            bool dot = getLcdPixel(x, y);

            uint32 dotCol = dot ? lcdPixelOn : lcdPixelOff;

            // Render 4x4 active pixel with 1px grid separator
            for (int dy = 0; dy < scale - 1; dy++) {
                int rowOffset = (py + dy) * CANVAS_W;
                for (int dx = 0; dx < scale - 1; dx++) {
                    screenBuffer[rowOffset + px + dx] = dotCol;
                }
            }
            // Grid gap
            for (int dy = 0; dy < scale; dy++) {
                screenBuffer[(py + dy) * CANVAS_W + px + scale - 1] = lcdGridGap;
            }
            for (int dx = 0; dx < scale; dx++) {
                screenBuffer[(py + scale - 1) * CANVAS_W + px + dx] = lcdGridGap;
            }
        }
    }

    // Photorealistic Glass Lens Glare (Diagonal specular reflection)
    for (int dy = 0; dy < 160; dy++) {
        int py = lcdY0 + dy;
        for (int dx = 0; dx < 240; dx++) {
            int px = lcdX0 + dx;
            // Angled streak from top right to bottom left
            int diag = (dx + dy * 2);
            if (diag >= 260 && diag <= 340) {
                int glareAlpha = 32 - abs(diag - 300) / 2;
                if (glareAlpha > 0) {
                    uint32 cur = screenBuffer[py * CANVAS_W + px];
                    uint8 r = std::min(255, (int)((cur >> 16) & 0xFF) + glareAlpha);
                    uint8 g = std::min(255, (int)((cur >> 8) & 0xFF) + glareAlpha);
                    uint8 b = std::min(255, (int)(cur & 0xFF) + glareAlpha);
                    screenBuffer[py * CANVAS_W + px] = (0xFF << 24) | (r << 16) | (g << 8) | b;
                }
            }
        }
    }
}

void SegaVMUMachine::updateAudio() {
    int16 sample = 0;
    if (buzzerTimer > 0) {
        // Generate authentic 1-bit square wave buzzer sound
        buzzerPhase += (buzzerFreq / 44100.0);
        if (buzzerPhase >= 1.0) buzzerPhase -= 1.0;
        sample = (buzzerPhase < 0.5) ? 6000 : -6000;
    }
    std::fill(audioBuffer.begin(), audioBuffer.end(), sample);
}

void SegaVMUMachine::handleKeyDown(SDL_Keycode key, uint16 mod) {
    (void)mod;
    switch (key) {
        case SDLK_UP:    buttonState &= ~0x10; playBuzzerTone(1100, 3); break;
        case SDLK_DOWN:  buttonState &= ~0x20; playBuzzerTone(1100, 3); break;
        case SDLK_LEFT:  buttonState &= ~0x40; playBuzzerTone(1100, 3); break;
        case SDLK_RIGHT: buttonState &= ~0x80; playBuzzerTone(1100, 3); break;

        case SDLK_z: case SDLK_a: case SDLK_RETURN:
            buttonState &= ~0x01; playBuzzerTone(1500, 4); break; // A button

        case SDLK_x: case SDLK_s: case SDLK_SPACE:
            buttonState &= ~0x02; playBuzzerTone(1300, 4); break; // B button

        case SDLK_m: case SDLK_TAB: // MODE button
            currentApp = (currentApp + 1) % 3;
            icons = (1 << currentApp);
            playBuzzerTone(1800, 6);
            break;

        case SDLK_ESCAPE: // SLEEP button
            buttonState &= ~0x04;
            playBuzzerTone(600, 8);
            break;

        case SDLK_F2: // Cycle to next encrypted VMU game
            cycleNextGame();
            break;

        case SDLK_F12: // Reset VMU
            reset();
            break;

        default: break;
    }
}

void SegaVMUMachine::handleKeyUp(SDL_Keycode key, uint16 mod) {
    (void)mod;
    switch (key) {
        case SDLK_UP:    buttonState |= 0x10; break;
        case SDLK_DOWN:  buttonState |= 0x20; break;
        case SDLK_LEFT:  buttonState |= 0x40; break;
        case SDLK_RIGHT: buttonState |= 0x80; break;
        case SDLK_z: case SDLK_a: case SDLK_RETURN: buttonState |= 0x01; break;
        case SDLK_x: case SDLK_s: case SDLK_SPACE:  buttonState |= 0x02; break;
        case SDLK_ESCAPE: buttonState |= 0x04; break;
        default: break;
    }
}

void SegaVMUMachine::handleMouseMotion(int x, int y) {
    mouseX = x;
    mouseY = y;
}

void SegaVMUMachine::handleMouseButton(uint8 button, bool pressed, int x, int y) {
    if (button != SDL_BUTTON_LEFT) return;
    mouseDown = pressed;

    if (!pressed) {
        // Release all buttons
        buttonState = 0xFF;
        return;
    }

    // 1. Top Connector Cap click (Y < 70) -> Cycle next game
    if (y >= 22 && y <= 68 && x >= 155 && x <= 485) {
        cycleNextGame();
        return;
    }

    // 2. D-Pad Click (Center: 230, 362)
    int ddx = x - 230;
    int ddy = y - 362;
    if (abs(ddx) <= 30 && abs(ddy) <= 30) {
        if (ddy < -8) buttonState &= ~0x10; // Up
        else if (ddy > 8) buttonState &= ~0x20; // Down
        else if (ddx < -8) buttonState &= ~0x40; // Left
        else if (ddx > 8) buttonState &= ~0x80; // Right
        playBuzzerTone(1100, 3);
        return;
    }

    // 3. Button A (Center: 405, 375, radius 20)
    int adx = x - 405;
    int ady = y - 375;
    if (adx * adx + ady * ady <= 400) {
        buttonState &= ~0x01;
        playBuzzerTone(1500, 4);
        return;
    }

    // 4. Button B (Center: 442, 342, radius 20)
    int bdx = x - 442;
    int bdy = y - 342;
    if (bdx * bdx + bdy * bdy <= 400) {
        buttonState &= ~0x02;
        playBuzzerTone(1300, 4);
        return;
    }

    // 5. MODE Button (Center: 312, 395)
    if (abs(x - 312) <= 12 && abs(y - 395) <= 8) {
        currentApp = (currentApp + 1) % 3;
        icons = (1 << currentApp);
        playBuzzerTone(1800, 6);
        return;
    }

    // 6. SLEEP Button (Center: 348, 395)
    if (abs(x - 348) <= 12 && abs(y - 395) <= 8) {
        buttonState &= ~0x04;
        playBuzzerTone(600, 8);
        return;
    }
}
