#include "vm2_machine.h"
#include "embedded_nextgen_vmu.h"
#include "gui_font.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <cmath>
#include <algorithm>

static const char* VM2_GAMES[] = {
    "Chao Expedition (VM2 Next-Gen)",
    "Skies of Arcadia: Sky Voyage",
    "Power Stone Arena (VM2)",
    "Sonic Speed Run (VM2)",
    "Cosmic Raider (VM2 OLED)"
};

DreamwareVM2Machine::DreamwareVM2Machine() {
    screenBuffer.resize(CANVAS_W * CANVAS_H, 0xFF10131B);
    audioBuffer.resize(882, 0);
    virtualPages.resize(16);
    for (auto& page : virtualPages) {
        page.fill(0x00);
    }
}

std::string DreamwareVM2Machine::getMediaName() const {
    if (currentGameIndex >= 0 && currentGameIndex < 5) {
        return VM2_GAMES[currentGameIndex];
    }
    return "Dreamware Enterprises VM2";
}

void DreamwareVM2Machine::init() {
    EmbeddedNextGenVMU::ensureDecrypted();
    std::memcpy(firmwareRom.data(), EmbeddedNextGenVMU::VM2_FIRMWARE, 65536);

    // Populate Page 0 with initial game
    std::memcpy(virtualPages[0].data(), EmbeddedNextGenVMU::VM2_CHAO_EXPEDITION, 16384);
    std::memcpy(virtualPages[1].data(), EmbeddedNextGenVMU::VM2_SKIES_VOYAGE, 16384);
    std::memcpy(virtualPages[2].data(), EmbeddedNextGenVMU::VM2_POWER_STONE_DUEL, 16384);
    std::memcpy(virtualPages[3].data(), EmbeddedNextGenVMU::VM2_SONIC_SPEED, 16384);
    std::memcpy(virtualPages[4].data(), EmbeddedNextGenVMU::VM2_SPACE_RAID, 16384);

    reset();
}

void DreamwareVM2Machine::reset() {
    PC = 0x0000;
    ACC = 0;
    B = 0;
    PSW = 0;
    buttonState = 0xFF;
    buzzerTimer = 0;
    buzzerFreq = 1600;
    buzzerPhase = 0.0;
    animTick = 0;
    currentApp = 0;

    chaoX = 16;
    chaoDir = 1;
    chaoAction = 0;
    chaoActionTimer = 0;
    chaoHP = 100;
    chaoHappiness = 98;

    pintaY = 16;
    pintaScroll = 0;
    pintaAction = 0;

    psFighterX = 12;
    psEnemyX = 34;
    psAction = 0;

    sonicX = 14;
    sonicAction = 0;
    rings = 15;

    spaceShipX = 22;
    spaceAction = 0;

    osdBannerText = "Dreamware VM2 (OLED) - Page 1/16 - Press F2 / [P] to Switch";
    osdBannerTimer = 220;
}

void DreamwareVM2Machine::setVirtualPage(int page) {
    if (page < 0) page = 15;
    if (page > 15) page = 0;
    currentPage = page;
    char buf[64];
    snprintf(buf, sizeof(buf), "[VM2] Virtual Page %02d/16 Loaded (128KB)", currentPage + 1);
    osdBannerText = buf;
    osdBannerTimer = 180;
    playBuzzerTone(1800, 4);
}

void DreamwareVM2Machine::cycleNextGame() {
    currentGameIndex = (currentGameIndex + 1) % 5;
    setVirtualPage(currentGameIndex);
    osdBannerText = std::string("[VM2 OLED] ") + VM2_GAMES[currentGameIndex];
    osdBannerTimer = 220;
    playBuzzerTone(2000, 6);
}

bool DreamwareVM2Machine::loadMedia(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    std::vector<uint8> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (buf.empty()) return false;

    size_t copyLen = std::min(buf.size(), virtualPages[currentPage].size());
    std::memcpy(virtualPages[currentPage].data(), buf.data(), copyLen);

    osdBannerText = "Loaded Custom VMS onto Page " + std::to_string(currentPage + 1);
    osdBannerTimer = 200;
    playBuzzerTone(1500, 8);
    return true;
}

bool DreamwareVM2Machine::loadGameByName(const std::string& name) {
    std::string n = name;
    std::transform(n.begin(), n.end(), n.begin(), ::tolower);
    if (n.empty()) return true;

    if (n.find("chao") != std::string::npos) {
        currentGameIndex = 0;
    } else if (n.find("skie") != std::string::npos || n.find("sky") != std::string::npos || n.find("arcadia") != std::string::npos || n.find("pinta") != std::string::npos) {
        currentGameIndex = 1;
    } else if (n.find("power") != std::string::npos || n.find("stone") != std::string::npos || n.find("duel") != std::string::npos) {
        currentGameIndex = 2;
    } else if (n.find("sonic") != std::string::npos || n.find("speed") != std::string::npos) {
        currentGameIndex = 3;
    } else if (n.find("space") != std::string::npos || n.find("cosmic") != std::string::npos || n.find("raid") != std::string::npos) {
        currentGameIndex = 4;
    } else {
        return loadMedia(name);
    }

    setVirtualPage(currentGameIndex);
    osdBannerText = std::string("[VM2 OLED] ") + VM2_GAMES[currentGameIndex];
    osdBannerTimer = 220;
    playBuzzerTone(2000, 6);
    return true;
}

void DreamwareVM2Machine::stepInstruction() {
    uint8 op = firmwareRom[PC++];
    switch (op) {
        case 0x00: break;
        case 0x01: ACC = firmwareRom[PC++]; break;
        case 0x02: ACC++; if (ACC == 0) PSW |= 0x02; else PSW &= ~0x02; break;
        case 0x03: ACC--; if (ACC == 0) PSW |= 0x02; else PSW &= ~0x02; break;
        case 0x04: PC = (firmwareRom[PC] << 8) | firmwareRom[PC + 1]; break;
        case 0x05: if (PSW & 0x02) PC = (firmwareRom[PC] << 8) | firmwareRom[PC + 1]; else PC += 2; break;
        default: break;
    }
}

void DreamwareVM2Machine::setOledPixel(int x, int y, bool on) {
    if (x < 0 || x >= 48 || y < 0 || y >= 32) return;
    int idx = y * 6 + (x / 8);
    uint8 mask = 0x80 >> (x % 8);
    if (on) oledBuffer[idx] |= mask;
    else    oledBuffer[idx] &= ~mask;
}

bool DreamwareVM2Machine::getOledPixel(int x, int y) const {
    if (x < 0 || x >= 48 || y < 0 || y >= 32) return false;
    int idx = y * 6 + (x / 8);
    return (oledBuffer[idx] & (0x80 >> (x % 8))) != 0;
}

void DreamwareVM2Machine::drawOledSprite(int x, int y, int w, int h, const uint8* data) {
    int bytesPerRow = (w + 7) / 8;
    for (int r = 0; r < h; r++) {
        for (int c = 0; c < w; c++) {
            uint8 b = data[r * bytesPerRow + (c / 8)];
            if (b & (0x80 >> (c % 8))) {
                setOledPixel(x + c, y + r, true);
            }
        }
    }
}

void DreamwareVM2Machine::drawOledString(int x, int y, const std::string& str) {
    for (size_t i = 0; i < str.size(); i++) {
        char c = str[i];
        int px = x + (int)i * 4;
        if (px >= 46) break;
        uint16 glyph = 0x5555;
        if (c >= '0' && c <= '9') {
            static const uint16 numGlyphs[10] = {
                0x7B6F, 0x2492, 0x73E7, 0x73CF, 0x5BC9, 0x79CF, 0x79EF, 0x7249, 0x7BCF, 0x7BC9
            };
            glyph = numGlyphs[c - '0'];
        }
        for (int r = 0; r < 5; r++) {
            for (int col = 0; col < 3; col++) {
                if ((glyph >> (14 - (r * 3 + col))) & 1) {
                    setOledPixel(px + col, y + r, true);
                }
            }
        }
    }
}

void DreamwareVM2Machine::playBuzzerTone(int freq, int durationTicks) {
    buzzerFreq = freq;
    buzzerTimer = durationTicks;
}

void DreamwareVM2Machine::updateGameLogic() {
    std::memset(oledBuffer, 0, sizeof(oledBuffer));
    int btn = ~buttonState;

    if (currentApp == 1) {
        // VM2 System Clock & Battery Status Screen
        for (int x = 2; x < 46; x++) { setOledPixel(x, 2, true); setOledPixel(x, 29, true); }
        for (int y = 2; y < 30; y++) { setOledPixel(2, y, true); setOledPixel(45, y, true); }
        drawOledString(10, 8, "12:45");
        drawOledString(8, 16, "98%");
        // Glowing battery icon
        for (int bx = 26; bx < 40; bx++) {
            setOledPixel(bx, 17, true); setOledPixel(bx, 22, true);
        }
        for (int by = 17; by <= 22; by++) {
            setOledPixel(26, by, true); setOledPixel(39, by, true);
        }
        setOledPixel(40, 19, true); setOledPixel(40, 20, true);
        for (int bx = 28; bx < 37; bx++) {
            for (int by = 19; by <= 20; by++) setOledPixel(bx, by, true);
        }
        return;
    }

    if (currentApp == 2) {
        // VM2 Multi-Page Explorer
        for (int x = 0; x < 48; x++) setOledPixel(x, 6, true);
        drawOledString(4, 10, "PAGE");
        drawOledString(24, 10, std::to_string(currentPage + 1));
        drawOledString(4, 20, "128KB");
        return;
    }

    // Minigame Execution
    switch (currentGameIndex) {
        case 0: { // Chao Expedition (VM2)
            if (btn & 0x01) { chaoAction = 1; chaoActionTimer = 30; playBuzzerTone(1600, 4); }
            else if (btn & 0x02) { chaoAction = 2; chaoActionTimer = 35; playBuzzerTone(2100, 5); }
            else if (btn & 0x40) { chaoX = std::max(2, chaoX - 1); chaoDir = -1; }
            else if (btn & 0x80) { chaoX = std::min(32, chaoX + 1); chaoDir = 1; }

            if (chaoActionTimer > 0 && --chaoActionTimer == 0) chaoAction = 0;

            for (int x = 0; x < 48; x++) setOledPixel(x, 26, (x % 2 == 0));
            int walkBob = (animTick / 8) % 2;
            int cy = 12 + (chaoAction == 2 ? -2 : walkBob);

            static const uint8 chaoGfx[13 * 2] = {
                0b00111100, 0b00000000, 0b01111110, 0b00000000,
                0b11011011, 0b00000000, 0b11111111, 0b00000000,
                0b11111111, 0b00000000, 0b01111110, 0b00000000,
                0b00111100, 0b00000000, 0b01011010, 0b00000000,
                0b11111111, 0b00000000, 0b10111101, 0b00000000,
                0b00111100, 0b00000000, 0b01100110, 0b00000000,
                0b01100110, 0b00000000
            };
            drawOledSprite(chaoX, cy, 12, 13, chaoGfx);

            // Glowing Halo
            int hy = cy - 4 + ((animTick / 10) % 2);
            setOledPixel(chaoX + 5, hy, true); setOledPixel(chaoX + 6, hy, true);
            setOledPixel(chaoX + 5, hy + 1, true); setOledPixel(chaoX + 6, hy + 1, true);

            if (chaoAction == 2) { // Hearts
                int hx = chaoX + 11;
                setOledPixel(hx + 1, cy - 3, true); setOledPixel(hx + 3, cy - 3, true);
                setOledPixel(hx, cy - 2, true); setOledPixel(hx + 2, cy - 2, true); setOledPixel(hx + 4, cy - 2, true);
                setOledPixel(hx + 2, cy - 1, true);
            }
            break;
        }

        case 1: { // Skies of Arcadia Voyage
            pintaScroll++;
            if (btn & 0x10) pintaY = std::max(4, pintaY - 1);
            if (btn & 0x20) pintaY = std::min(24, pintaY + 1);
            if (btn & 0x01) { pintaAction = 1; playBuzzerTone(900, 4); }

            for (int i = 0; i < 3; i++) {
                int cx = (i * 20 - (pintaScroll / 2)) % 60;
                if (cx >= 0 && cx < 44) {
                    setOledPixel(cx, 6, true); setOledPixel(cx + 1, 6, true); setOledPixel(cx + 2, 6, true);
                }
            }

            static const uint8 airship[7 * 2] = {
                0b00011000, 0b00000000, 0b01111110, 0b00000000,
                0b11111111, 0b00000000, 0b01111110, 0b00000000,
                0b00111100, 0b00000000, 0b01011010, 0b00000000,
                0b01111110, 0b00000000
            };
            drawOledSprite(8, pintaY, 10, 7, airship);
            drawOledSprite(36, 14, 10, 7, airship);
            break;
        }

        case 2: { // Power Stone Arena
            if (btn & 0x01) { psAction = 1; playBuzzerTone(1600, 3); }
            else if (btn & 0x02) { psAction = 2; playBuzzerTone(2200, 7); }
            if (btn & 0x40) psFighterX = std::max(2, psFighterX - 1);
            if (btn & 0x80) psFighterX = std::min(psEnemyX - 8, psFighterX + 1);

            for (int x = 2; x < 46; x++) setOledPixel(x, 26, true);
            static const uint8 fighter[9] = {
                0b01110000, 0b10101000, 0b11111000, 0b01110000,
                0b11111000, 0b10101000, 0b01110000, 0b01010000, 0b11011000
            };
            drawOledSprite(psFighterX, 17, 8, 9, fighter);
            drawOledSprite(psEnemyX, 17, 8, 9, fighter);

            if (psAction == 2) {
                for (int x = psFighterX + 8; x < 46; x++) setOledPixel(x, 21, true);
            }
            break;
        }

        case 3: { // Sonic Speed Run
            sonicX = (sonicX + 1) % 40;
            if (btn & 0x01) { sonicAction = 1; playBuzzerTone(1800, 4); }
            for (int x = 0; x < 48; x++) setOledPixel(x, 26, true);
            static const uint8 sonic[10] = {
                0b00111100, 0b01111110, 0b11011011, 0b11111111,
                0b01111110, 0b00111100, 0b01011010, 0b11111111,
                0b01100110, 0b11000011
            };
            drawOledSprite(sonicX, 16, 8, 10, sonic);
            // Ring
            setOledPixel(36, 18, true); setOledPixel(37, 18, true);
            setOledPixel(36, 19, true); setOledPixel(37, 19, true);
            break;
        }

        case 4: { // Cosmic Raider
            if (btn & 0x40) spaceShipX = std::max(4, spaceShipX - 1);
            if (btn & 0x80) spaceShipX = std::min(40, spaceShipX + 1);
            if (btn & 0x01) { spaceAction = 1; playBuzzerTone(2400, 2); }

            // Stars
            for (int s = 0; s < 4; s++) {
                int sy = (animTick * (s + 1) * 2) % 32;
                int sx = (s * 12 + 6);
                setOledPixel(sx, sy, true);
            }
            // Ship
            static const uint8 ship[6] = {
                0b00100000, 0b01110000, 0b11111000, 0b01110000, 0b10101000, 0b00000000
            };
            drawOledSprite(spaceShipX, 24, 5, 6, ship);
            if (spaceAction == 1) {
                setOledPixel(spaceShipX + 2, 20, true);
                setOledPixel(spaceShipX + 2, 17, true);
            }
            break;
        }
        default: break;
    }
}

void DreamwareVM2Machine::runFrame() {
    animTick++;
    for (int i = 0; i < 400; i++) stepInstruction();

    updateGameLogic();

    if (buzzerTimer > 0) buzzerTimer--;
    if (osdBannerTimer > 0) osdBannerTimer--;

    renderVM2Screen();
    updateAudio();
}

void DreamwareVM2Machine::renderVM2Screen() {
    std::fill(screenBuffer.begin(), screenBuffer.end(), 0xFF0D1017);

    // Subtle dark blue spotlight
    for (int y = 0; y < CANVAS_H; y++) {
        int dy = y - 256;
        for (int x = 0; x < CANVAS_W; x++) {
            int dx = x - 320;
            int dist = dx * dx + dy * dy;
            if (dist < 150000) {
                int glow = (150000 - dist) / 10000;
                uint32 cur = screenBuffer[y * CANVAS_W + x];
                uint8 b = std::min(255, (int)(cur & 0xFF) + glow);
                screenBuffer[y * CANVAS_W + x] = (cur & 0xFFFFFF00) | b;
            }
        }
    }

    renderCasing();
    renderOLEDMatrix();

    if (osdBannerTimer > 0) {
        int by = 480;
        GUIFont::fillRect(screenBuffer.data(), CANVAS_W, 40, by - 4, 560, 24, 0xEE090D14);
        GUIFont::drawRect(screenBuffer.data(), CANVAS_W, 40, by - 4, 560, 24, 0xFF007ACC);
        GUIFont::drawString(screenBuffer.data(), CANVAS_W, 52, by, osdBannerText, 0xFFE6EDF3);
    }
}

void DreamwareVM2Machine::renderCasing() {
    const int bx0 = 155, bx1 = 485;
    const int by0 = 22,  by1 = 488;

    // Body Shadow
    for (int y = by0 + 10; y <= by1 + 14; y++) {
        for (int x = bx0 - 6; x <= bx1 + 10; x++) {
            if (y >= 0 && y < CANVAS_H && x >= 0 && x < CANVAS_W) {
                uint32 c = screenBuffer[y * CANVAS_W + x];
                screenBuffer[y * CANVAS_W + x] = 0xFF000000 | (((c >> 16) & 0xFF) / 2 << 16) | (((c >> 8) & 0xFF) / 2 << 8) | ((c & 0xFF) / 2);
            }
        }
    }

    // VM2 Modern Metallic / Frosted Pearl Body
    for (int y = by0; y <= by1; y++) {
        for (int x = bx0; x <= bx1; x++) {
            if ((x < bx0 + 24 && y < by0 + 24 && (bx0 + 24 - x)*(bx0 + 24 - x) + (by0 + 24 - y)*(by0 + 24 - y) > 576) ||
                (x > bx1 - 24 && y < by0 + 24 && (x - (bx1 - 24))*(x - (bx1 - 24)) + (by0 + 24 - y)*(by0 + 24 - y) > 576) ||
                (x < bx0 + 20 && y > by1 - 20 && (bx0 + 20 - x)*(bx0 + 20 - x) + (y - (by1 - 20))*(y - (by1 - 20)) > 400) ||
                (x > bx1 - 20 && y > by1 - 20 && (x - (bx1 - 20))*(x - (bx1 - 20)) + (y - (by1 - 20))*(y - (by1 - 20)) > 400)) {
                continue;
            }

            float grad = (float)(y - by0) / (by1 - by0);
            int base = 230 - (int)(grad * 25.0f);
            int r = base, g = base + 4, b = base + 10; // Icy pearl white

            if (x < bx0 + 3 || y < by0 + 3) { r = std::min(255, r + 24); g = std::min(255, g + 24); b = std::min(255, b + 24); }
            else if (x > bx1 - 4 || y > by1 - 4) { r = (int)(r * 0.75f); g = (int)(g * 0.75f); b = (int)(b * 0.78f); }

            screenBuffer[y * CANVAS_W + x] = 0xFF000000 | (r << 16) | (g << 8) | b;
        }
    }

    // Top Cap Seam
    for (int x = bx0 + 4; x <= bx1 - 4; x++) {
        screenBuffer[68 * CANVAS_W + x] = 0xFF8A93A4;
        screenBuffer[69 * CANVAS_W + x] = 0xFFCED5E2;
    }

    // "VM2" Modern Metallic Branding & Blue Status LED
    GUIFont::drawString(screenBuffer.data(), CANVAS_W, 252, 92, "DREAMWARE VM2", 0xFF0088EE);
    // Blue Power LED
    for (int dy = -3; dy <= 3; dy++) {
        for (int dx = -3; dx <= 3; dx++) {
            if (dx * dx + dy * dy <= 9) screenBuffer[(95 + dy) * CANVAS_W + 372 + dx] = 0xFF00C8FF;
        }
    }

    // Deep Black Bezel for OLED Glass Window (X: 184..456, Y: 104..296)
    const int wx0 = 184, wx1 = 456;
    const int wy0 = 104, wy1 = 296;
    for (int y = wy0; y <= wy1; y++) {
        for (int x = wx0; x <= wx1; x++) {
            screenBuffer[y * CANVAS_W + x] = (x == wx0 || y == wy0) ? 0xFF000000 : 0xFF080B10;
        }
    }

    // Directional D-Pad (Center: 230, 366)
    const int dpadCX = 230, dpadCY = 366;
    int btnActive = ~buttonState;
    for (int dy = -32; dy <= 32; dy++) {
        for (int dx = -32; dx <= 32; dx++) {
            if ((abs(dx) <= 11 && abs(dy) <= 30) || (abs(dy) <= 11 && abs(dx) <= 30)) {
                uint32 col = (dy < -26 || dx < -26) ? 0xFF4E5666 : 0xFF2B303C;
                if (dx * dx + dy * dy <= 144) col = 0xFF1C2028;
                screenBuffer[(dpadCY + dy) * CANVAS_W + dpadCX + dx] = col;
            }
        }
    }

    // A and B Buttons (Cyan / Ice Blue Accented)
    auto drawVM2Btn = [&](int cx, int cy, char label, bool pressed) {
        for (int dy = -16; dy <= 16; dy++) {
            for (int dx = -16; dx <= 16; dx++) {
                if (dx * dx + dy * dy <= 256) {
                    uint32 col = pressed ? 0xFF005599 : 0xFF007ACC;
                    screenBuffer[(cy + dy) * CANVAS_W + cx + dx] = col;
                }
            }
        }
        GUIFont::drawChar(screenBuffer.data(), CANVAS_W, cx - 3, cy - 4, label, 0xFFFFFFFF);
    };
    drawVM2Btn(405, 375, 'A', (btnActive & 0x01) != 0);
    drawVM2Btn(442, 342, 'B', (btnActive & 0x02) != 0);

    // MODE & SLEEP Buttons
    GUIFont::drawString(screenBuffer.data(), CANVAS_W, 302, 392, "MODE", 0xFF6A7486);
    GUIFont::drawString(screenBuffer.data(), CANVAS_W, 338, 392, "PAGE", 0xFF0088EE);

    // USB-C Port Cutout at Bottom
    for (int y = by1 - 10; y <= by1 - 4; y++) {
        for (int x = 308; x <= 332; x++) screenBuffer[y * CANVAS_W + x] = 0xFF141820;
    }
}

void DreamwareVM2Machine::renderOLEDMatrix() {
    // 48 x 32 Matrix scaled 5x (240 x 160) centered at X: 200, Y: 124
    const int lcdX0 = 200;
    const int lcdY0 = 124;
    const int scale = 5;

    // Pitch Black OLED background
    for (int y = 0; y < 160; y++) {
        for (int x = 0; x < 240; x++) {
            screenBuffer[(lcdY0 + y) * CANVAS_W + lcdX0 + x] = 0xFF000000;
        }
    }

    // High-Res VM2 OLED Status Bar on Top of Matrix
    char statusBuf[64];
    snprintf(statusBuf, sizeof(statusBuf), "P:%02d 98%% [SD] 12:45", currentPage + 1);
    GUIFont::drawString(screenBuffer.data(), CANVAS_W, lcdX0 + 4, lcdY0 + 2, statusBuf, 0xFF00AAFF);

    // Emissive OLED Pixels (Sharp Ice Blue / Electric White: 0xFFE0F4FF)
    const uint32 oledOn = 0xFFE0F4FF;
    const uint32 oledBloom = 0xFF104878;

    for (int y = 0; y < 32; y++) {
        int py = lcdY0 + 16 + (y * 140 / 32);
        for (int x = 0; x < 48; x++) {
            int px = lcdX0 + x * scale;
            if (getOledPixel(x, y)) {
                for (int dy = 0; dy < 4; dy++) {
                    int ro = (py + dy) * CANVAS_W;
                    for (int dx = 0; dx < 4; dx++) {
                        screenBuffer[ro + px + dx] = oledOn;
                    }
                }
                // Subtle OLED bloom aura
                screenBuffer[(py - 1) * CANVAS_W + px] = oledBloom;
                screenBuffer[(py + 4) * CANVAS_W + px] = oledBloom;
            }
        }
    }
}

void DreamwareVM2Machine::updateAudio() {
    int16 sample = 0;
    if (buzzerTimer > 0) {
        buzzerPhase += (buzzerFreq / 44100.0);
        if (buzzerPhase >= 1.0) buzzerPhase -= 1.0;
        sample = (buzzerPhase < 0.5) ? 7000 : -7000;
    }
    std::fill(audioBuffer.begin(), audioBuffer.end(), sample);
}

void DreamwareVM2Machine::handleKeyDown(SDL_Keycode key, uint16 mod) {
    (void)mod;
    switch (key) {
        case SDLK_UP:    buttonState &= ~0x10; playBuzzerTone(1200, 3); break;
        case SDLK_DOWN:  buttonState &= ~0x20; playBuzzerTone(1200, 3); break;
        case SDLK_LEFT:  buttonState &= ~0x40; playBuzzerTone(1200, 3); break;
        case SDLK_RIGHT: buttonState &= ~0x80; playBuzzerTone(1200, 3); break;
        case SDLK_z: case SDLK_a: case SDLK_RETURN: buttonState &= ~0x01; playBuzzerTone(1800, 4); break;
        case SDLK_x: case SDLK_s: case SDLK_SPACE:  buttonState &= ~0x02; playBuzzerTone(1500, 4); break;
        case SDLK_m: case SDLK_TAB: currentApp = (currentApp + 1) % 3; playBuzzerTone(2000, 6); break;
        case SDLK_p: setVirtualPage(currentPage + 1); break;
        case SDLK_F2: cycleNextGame(); break;
        case SDLK_F12: reset(); break;
        default: break;
    }
}

void DreamwareVM2Machine::handleKeyUp(SDL_Keycode key, uint16 mod) {
    (void)mod;
    switch (key) {
        case SDLK_UP:    buttonState |= 0x10; break;
        case SDLK_DOWN:  buttonState |= 0x20; break;
        case SDLK_LEFT:  buttonState |= 0x40; break;
        case SDLK_RIGHT: buttonState |= 0x80; break;
        case SDLK_z: case SDLK_a: case SDLK_RETURN: buttonState |= 0x01; break;
        case SDLK_x: case SDLK_s: case SDLK_SPACE:  buttonState |= 0x02; break;
        default: break;
    }
}

void DreamwareVM2Machine::handleMouseMotion(int x, int y) {
    mouseX = x; mouseY = y;
}

void DreamwareVM2Machine::handleMouseButton(uint8 button, bool pressed, int x, int y) {
    if (button != SDL_BUTTON_LEFT) return;
    mouseDown = pressed;
    if (!pressed) { buttonState = 0xFF; return; }

    if (y >= 22 && y <= 68) { cycleNextGame(); return; }
    if (abs(x - 345) <= 18 && abs(y - 392) <= 10) { setVirtualPage(currentPage + 1); return; }
    if (abs(x - 310) <= 18 && abs(y - 392) <= 10) { currentApp = (currentApp + 1) % 3; return; }

    int ddx = x - 230, ddy = y - 366;
    if (abs(ddx) <= 30 && abs(ddy) <= 30) {
        if (ddy < -8) buttonState &= ~0x10;
        else if (ddy > 8) buttonState &= ~0x20;
        else if (ddx < -8) buttonState &= ~0x40;
        else if (ddx > 8) buttonState &= ~0x80;
        playBuzzerTone(1200, 3);
        return;
    }

    if ((x - 405)*(x - 405) + (y - 375)*(y - 375) <= 400) { buttonState &= ~0x01; playBuzzerTone(1800, 4); return; }
    if ((x - 442)*(x - 442) + (y - 342)*(y - 342) <= 400) { buttonState &= ~0x02; playBuzzerTone(1500, 4); return; }
}
