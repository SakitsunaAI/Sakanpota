#include "vmu_pro_machine.h"
#include "embedded_nextgen_vmu.h"
#include "gui_font.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <cmath>
#include <algorithm>

static const char* VMUPRO_GAMES[] = {
    "Chao Adventure Color (VMU Pro)",
    "Skies of Arcadia: Pinta's Quest Color",
    "Power Stone Duel Color",
    "Jet Set Radio Graffiti Dash",
    "Crazy Taxi: City Dash"
};

VMUProMachine::VMUProMachine() {
    screenBuffer.resize(CANVAS_W * CANVAS_H, 0xFF0E1117);
    audioBuffer.resize(882, 0);
    ipsBuffer.resize(IPS_W * IPS_H, 0xFF000000);
    virtualBanks.resize(128);
    for (auto& bank : virtualBanks) {
        bank.fill(0x00);
    }
}

std::string VMUProMachine::getMediaName() const {
    if (currentGameIndex >= 0 && currentGameIndex < 5) {
        return VMUPRO_GAMES[currentGameIndex];
    }
    return "8BitMods VMU Pro";
}

void VMUProMachine::init() {
    EmbeddedNextGenVMU::ensureDecrypted();
    std::memcpy(firmwareRom.data(), EmbeddedNextGenVMU::VMUPRO_FIRMWARE, 65536);

    // Populate initial banks with authentic color minigames
    std::memcpy(virtualBanks[0].data(), EmbeddedNextGenVMU::VMUPRO_CHAO_COLOR, 16384);
    std::memcpy(virtualBanks[1].data(), EmbeddedNextGenVMU::VMUPRO_PINTA_COLOR, 16384);
    std::memcpy(virtualBanks[2].data(), EmbeddedNextGenVMU::VMUPRO_POWER_STONE_COLOR, 16384);
    std::memcpy(virtualBanks[3].data(), EmbeddedNextGenVMU::VMUPRO_JET_SET_RADIO, 16384);
    std::memcpy(virtualBanks[4].data(), EmbeddedNextGenVMU::VMUPRO_CRAZY_TAXI, 16384);

    reset();
}

void VMUProMachine::reset() {
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

    chaoX = 100;
    chaoY = 95;
    chaoDir = 1;
    chaoAction = 0;
    chaoActionTimer = 0;
    chaoHappiness = 100;
    chaoStamina = 95;

    pintaX = 40;
    pintaY = 70;
    pintaScroll = 0;
    pintaAction = 0;
    pintaGold = 2450;

    fighter1X = 50;
    fighter2X = 170;
    fighter1Action = 0;
    fighter1Timer = 0;
    powerGemCount = 2;

    jsrX = 50;
    jsrY = 105;
    jsrScroll = 0;
    jsrSpraying = 0;
    jsrSprayTimer = 0;
    jsrCans = 8;
    jsrScore = 14200;

    taxiX = 110;
    taxiSpeed = 3;
    taxiScroll = 0;
    taxiFare = 150;
    taxiTimeLeft = 45;

    osdBannerText = "8BitMods VMU Pro (IPS Color) - Bank 001/128 - Press F2 / [B] to Switch";
    osdBannerTimer = 220;
}

void VMUProMachine::setBank(int bank) {
    if (bank < 0) bank = 127;
    if (bank > 127) bank = 0;
    currentBank = bank;
    char buf[64];
    snprintf(buf, sizeof(buf), "[VMU PRO] Flash Bank %03d/128 Selected (128KB)", currentBank + 1);
    osdBannerText = buf;
    osdBannerTimer = 180;
    playBuzzerTone(2200, 4);
}

void VMUProMachine::cycleNextGame() {
    currentGameIndex = (currentGameIndex + 1) % 5;
    setBank(currentGameIndex);
    osdBannerText = std::string("[VMU PRO IPS] ") + VMUPRO_GAMES[currentGameIndex];
    osdBannerTimer = 220;
    playBuzzerTone(2400, 6);
}

bool VMUProMachine::loadMedia(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    std::vector<uint8> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (buf.empty()) return false;

    size_t copyLen = std::min(buf.size(), virtualBanks[currentBank].size());
    std::memcpy(virtualBanks[currentBank].data(), buf.data(), copyLen);

    osdBannerText = "Loaded VMS File onto VMU Pro Bank " + std::to_string(currentBank + 1);
    osdBannerTimer = 200;
    playBuzzerTone(1800, 8);
    return true;
}

bool VMUProMachine::loadGameByName(const std::string& name) {
    std::string n = name;
    std::transform(n.begin(), n.end(), n.begin(), ::tolower);
    if (n.empty()) return true;

    if (n.find("chao") != std::string::npos) {
        currentGameIndex = 0;
    } else if (n.find("pinta") != std::string::npos || n.find("skies") != std::string::npos || n.find("sky") != std::string::npos || n.find("arcadia") != std::string::npos) {
        currentGameIndex = 1;
    } else if (n.find("power") != std::string::npos || n.find("stone") != std::string::npos || n.find("duel") != std::string::npos) {
        currentGameIndex = 2;
    } else if (n.find("jsr") != std::string::npos || n.find("jet") != std::string::npos || n.find("radio") != std::string::npos || n.find("graffiti") != std::string::npos) {
        currentGameIndex = 3;
    } else if (n.find("taxi") != std::string::npos || n.find("crazy") != std::string::npos) {
        currentGameIndex = 4;
    } else {
        return loadMedia(name);
    }

    setBank(currentGameIndex);
    osdBannerText = std::string("[VMU PRO IPS] ") + VMUPRO_GAMES[currentGameIndex];
    osdBannerTimer = 220;
    playBuzzerTone(2400, 6);
    return true;
}

void VMUProMachine::stepInstruction() {
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

void VMUProMachine::setIpsPixel(int x, int y, uint32 color) {
    if (x < 0 || x >= IPS_W || y < 0 || y >= IPS_H) return;
    ipsBuffer[y * IPS_W + x] = color;
}

void VMUProMachine::fillIpsRect(int x, int y, int w, int h, uint32 color) {
    for (int r = 0; r < h; r++) {
        int py = y + r;
        if (py < 0 || py >= IPS_H) continue;
        int rowStart = py * IPS_W;
        for (int c = 0; c < w; c++) {
            int px = x + c;
            if (px >= 0 && px < IPS_W) {
                ipsBuffer[rowStart + px] = color;
            }
        }
    }
}

void VMUProMachine::drawIpsString(int x, int y, const std::string& str, uint32 color) {
    GUIFont::drawString(ipsBuffer.data(), IPS_W, x, y, str, color);
}

void VMUProMachine::playBuzzerTone(int freq, int durationTicks) {
    buzzerFreq = freq;
    buzzerTimer = durationTicks;
}

void VMUProMachine::updateGameLogic() {
    int btn = ~buttonState;

    if (currentApp == 1) {
        // 8BitMods OS System Dashboard
        fillIpsRect(0, 0, IPS_W, IPS_H, 0xFF141923);
        fillIpsRect(0, 0, IPS_W, 20, 0xFF1E2638);
        drawIpsString(6, 6, "8BitMods VMU PRO v2.4", 0xFF00E5FF);

        // System Specs Table
        drawIpsString(12, 32, "Screen: 240x160 Full IPS", 0xFFE6EDF3);
        drawIpsString(12, 48, "Memory: 128 Banks (16MB)", 0xFFE6EDF3);
        drawIpsString(12, 64, "Active Bank: Bank " + std::to_string(currentBank + 1), 0xFFFFCC00);
        drawIpsString(12, 80, "Storage: MicroSD Fat32 (OK)", 0xFF00FF88);
        drawIpsString(12, 96, "Battery: 3.7V LiPo 96% (Good)", 0xFF00E5FF);
        drawIpsString(12, 112, "RGB LED: Smart Synced", 0xFFFF0077);
        drawIpsString(12, 134, "Press [MODE] to Return", 0xFF8A93A4);
        return;
    }

    if (currentApp == 2) {
        // 128-Bank Quick Visualizer
        fillIpsRect(0, 0, IPS_W, IPS_H, 0xFF0B0F19);
        fillIpsRect(0, 0, IPS_W, 18, 0xFF182236);
        drawIpsString(6, 4, "BANK MANAGER (128 FLASH BANKS)", 0xFFFFCC00);

        for (int r = 0; r < 8; r++) {
            for (int c = 0; c < 16; c++) {
                int bIdx = r * 16 + c;
                int bx = 8 + c * 14;
                int by = 26 + r * 14;
                uint32 col = (bIdx == currentBank) ? 0xFF00FFCC : ((bIdx < 5) ? 0xFF0088FF : 0xFF233048);
                fillIpsRect(bx, by, 11, 11, col);
            }
        }
        drawIpsString(8, 142, "Current: Bank " + std::to_string(currentBank + 1) + " (128KB)", 0xFFFFFFFF);
        return;
    }

    // Minigame Execution
    switch (currentGameIndex) {
        case 0: { // Chao Adventure Color (VMU Pro)
            // Vibrant Outdoor Garden Background
            // Sky gradient
            for (int y = 0; y < 110; y++) {
                float t = (float)y / 110.0f;
                int r = (int)(110 + t * 40);
                int g = (int)(180 + t * 50);
                int b = 255;
                fillIpsRect(0, y, IPS_W, 1, 0xFF000000 | (r << 16) | (g << 8) | b);
            }
            // Golden Sun
            fillIpsRect(190, 16, 26, 26, 0xFFFFDD33);
            fillIpsRect(192, 18, 22, 22, 0xFFFFF077);

            // Fluffy Clouds
            fillIpsRect(30, 24, 45, 14, 0xEEFFFFFF);
            fillIpsRect(40, 18, 25, 20, 0xEEFFFFFF);

            // Lush Green Grass & Hills
            for (int y = 110; y < IPS_H; y++) {
                float t = (float)(y - 110) / 50.0f;
                int r = (int)(34 - t * 15);
                int g = (int)(180 - t * 40);
                int b = (int)(55 - t * 20);
                fillIpsRect(0, y, IPS_W, 1, 0xFF000000 | (r << 16) | (g << 8) | b);
            }

            // Controls
            if (btn & 0x01) { chaoAction = 1; chaoActionTimer = 30; playBuzzerTone(2000, 4); }
            else if (btn & 0x02) { chaoAction = 2; chaoActionTimer = 35; playBuzzerTone(2400, 5); }
            else if (btn & 0x40) { chaoX = std::max(15, chaoX - 2); chaoDir = -1; }
            else if (btn & 0x80) { chaoX = std::min(205, chaoX + 2); chaoDir = 1; }

            if (chaoActionTimer > 0 && --chaoActionTimer == 0) chaoAction = 0;

            int walkBob = (animTick / 6) % 3;
            int cy = chaoY + (chaoAction == 1 ? -6 : walkBob);

            // Shadow
            fillIpsRect(chaoX - 2, chaoY + 24, 26, 6, 0x66185A20);

            // Draw Full-Color Chao Character
            // Head & Body (Vibrant Azure Blue)
            uint32 chaoBlue = 0xFF2A9DF4;
            uint32 chaoDarkBlue = 0xFF1565C0;
            uint32 chaoEye = 0xFF000000;
            uint32 chaoYellow = 0xFFFFD700;
            uint32 chaoPink = 0xFFFF4081;

            fillIpsRect(chaoX + 4, cy, 14, 14, chaoBlue);
            fillIpsRect(chaoX + 2, cy + 2, 18, 10, chaoBlue);
            // Highlights
            fillIpsRect(chaoX + 4, cy + 2, 4, 3, 0xFF80D8FF);

            // Floating Golden Halo
            int hy = cy - 8 + ((animTick / 8) % 2);
            fillIpsRect(chaoX + 7, hy, 8, 3, chaoYellow);
            fillIpsRect(chaoX + 6, hy + 1, 10, 1, chaoYellow);

            // Big Expressive Eyes
            fillIpsRect(chaoX + (chaoDir == 1 ? 11 : 5), cy + 4, 3, 6, chaoEye);
            fillIpsRect(chaoX + (chaoDir == 1 ? 16 : 10), cy + 4, 3, 6, chaoEye);
            fillIpsRect(chaoX + (chaoDir == 1 ? 11 : 5), cy + 4, 1, 2, 0xFFFFFFFF);
            fillIpsRect(chaoX + (chaoDir == 1 ? 16 : 10), cy + 4, 1, 2, 0xFFFFFFFF);

            // Pink Wings
            fillIpsRect(chaoX - 2, cy + 8, 4, 6, chaoPink);
            fillIpsRect(chaoX + 20, cy + 8, 4, 6, chaoPink);

            // Feet
            fillIpsRect(chaoX + 4, cy + 18, 5, 4, chaoDarkBlue);
            fillIpsRect(chaoX + 13, cy + 18, 5, 4, chaoDarkBlue);

            // Action: Floating Love Hearts on Action 2
            if (chaoAction == 2) {
                int hx = chaoX + 22, hhy = cy - 10;
                fillIpsRect(hx + 1, hhy, 3, 3, 0xFFFF0055);
                fillIpsRect(hx + 5, hhy, 3, 3, 0xFFFF0055);
                fillIpsRect(hx, hhy + 3, 9, 3, 0xFFFF0055);
                fillIpsRect(hx + 2, hhy + 6, 5, 3, 0xFFFF0055);
                fillIpsRect(hx + 4, hhy + 9, 1, 2, 0xFFFF0055);
            }

            // HUD
            fillIpsRect(6, 6, 85, 14, 0xAA000000);
            drawIpsString(8, 8, "HAP:100% STA:95", 0xFF00FFCC);
            break;
        }

        case 1: { // Skies of Arcadia: Pinta's Quest Color
            pintaScroll += 2;
            fillIpsRect(0, 0, IPS_W, IPS_H, 0xFF1E88E5); // Deep Blue Sky
            // Distant Floating Islands
            int islandX = (280 - (pintaScroll / 2) % 360);
            fillIpsRect(islandX, 50, 60, 20, 0xFF8D6E63);
            fillIpsRect(islandX + 5, 45, 50, 6, 0xFF4CAF50);

            // Clouds
            int cloudX = (320 - pintaScroll % 400);
            fillIpsRect(cloudX, 90, 70, 16, 0xCCFFFFFF);

            if (btn & 0x10) pintaY = std::max(20, pintaY - 2);
            if (btn & 0x20) pintaY = std::min(125, pintaY + 2);
            if (btn & 0x01) { pintaAction = 1; playBuzzerTone(1400, 4); }

            // Red & Gold Airship Sprite
            fillIpsRect(pintaX, pintaY + 8, 38, 12, 0xFFB71C1C);
            fillIpsRect(pintaX + 8, pintaY + 4, 22, 6, 0xFFFFD700);
            fillIpsRect(pintaX + 34, pintaY + 2, 4, 18, 0xFF8D6E63);
            // Spinning Propeller
            int propState = (animTick / 3) % 2;
            fillIpsRect(pintaX - 4, pintaY + (propState ? 4 : 10), 4, 8, 0xFFFFFFFF);

            // Cannon firing
            if (pintaAction == 1) {
                fillIpsRect(pintaX + 44 + ((animTick % 10) * 8), pintaY + 12, 6, 4, 0xFFFFEE00);
            }

            fillIpsRect(6, 6, 100, 14, 0xAA000000);
            drawIpsString(8, 8, "GOLD: 2,450G", 0xFFFFD700);
            break;
        }

        case 2: { // Power Stone Duel Color
            // Fighting Arena: Marble tiled floor + Ancient Pillars
            fillIpsRect(0, 0, IPS_W, 90, 0xFF281C38); // Sunset Arena Sky
            fillIpsRect(0, 90, IPS_W, IPS_H - 90, 0xFF5D4037); // Tiled Ground
            for (int x = 0; x < IPS_W; x += 30) {
                fillIpsRect(x, 90, 2, IPS_H - 90, 0xFF3E2723);
            }

            if (btn & 0x40) fighter1X = std::max(20, fighter1X - 2);
            if (btn & 0x80) fighter1X = std::min(fighter2X - 24, fighter1X + 2);
            if (btn & 0x01) { fighter1Action = 1; fighter1Timer = 20; playBuzzerTone(2600, 4); }

            if (fighter1Timer > 0 && --fighter1Timer == 0) fighter1Action = 0;

            // Fighter 1 (Falcon - Red & White)
            fillIpsRect(fighter1X, 92, 14, 26, 0xFFD32F2F);
            fillIpsRect(fighter1X + 2, 80, 10, 12, 0xFFFFCDD2);
            if (fighter1Action == 1) {
                // Boxing punch & flame effect
                fillIpsRect(fighter1X + 14, 86, 14, 8, 0xFFFF9800);
            }

            // Fighter 2 (Ayame - Purple & Pink)
            fillIpsRect(fighter2X, 92, 14, 26, 0xFF7B1FA2);
            fillIpsRect(fighter2X + 2, 80, 10, 12, 0xFFF8BBD0);

            // Bouncing Glowing Power Stones (Gems)
            int gemY = 110 + (int)(sin(animTick * 0.15) * 6.0);
            fillIpsRect(110, gemY, 8, 8, 0xFF00E676); // Green Gem
            fillIpsRect(140, gemY - 4, 8, 8, 0xFFFF1744); // Red Gem

            // Health Bars
            fillIpsRect(10, 8, 80, 8, 0xFFFF0000);
            fillIpsRect(10, 8, 65, 8, 0xFF00FF00);
            fillIpsRect(150, 8, 80, 8, 0xFFFF0000);
            fillIpsRect(165, 8, 65, 8, 0xFF00FF00);
            break;
        }

        case 3: { // Jet Set Radio Graffiti Dash
            jsrScroll += 3;
            // Tokyo-to Vibrant Cyberpunk City Skyline
            fillIpsRect(0, 0, IPS_W, 80, 0xFF1A237E); // Neon Night Sky
            fillIpsRect(20, 20, 40, 60, 0xFF311B92);
            fillIpsRect(80, 10, 50, 70, 0xFF4A148C);
            fillIpsRect(150, 30, 45, 50, 0xFF0D47A1);

            // Street Ground & Railing
            fillIpsRect(0, 80, IPS_W, IPS_H - 80, 0xFF212121);
            fillIpsRect(0, 80, IPS_W, 4, 0xFFFFEB3B); // Yellow Railing

            if (btn & 0x01) {
                jsrSpraying = 1; jsrSprayTimer = 25;
                jsrScore += 50;
                playBuzzerTone(2800, 3);
            }
            if (jsrSprayTimer > 0 && --jsrSprayTimer == 0) jsrSpraying = 0;

            // Beat on Inline Skates (Yellow & Green)
            fillIpsRect(jsrX, jsrY, 12, 22, 0xFFFFEB3B);
            fillIpsRect(jsrX + 2, jsrY - 10, 8, 10, 0xFFFF9800);
            fillIpsRect(jsrX, jsrY + 22, 14, 5, 0xFFE91E63);

            // Neon Spray Graffiti on Wall
            if (jsrSpraying) {
                fillIpsRect(jsrX + 16, jsrY - 14, 30, 18, 0xFFFF0055);
                drawIpsString(jsrX + 18, jsrY - 10, "JET!", 0xFFFFFFFF);
            }

            fillIpsRect(6, 6, 120, 14, 0xAA000000);
            drawIpsString(8, 8, "SCORE: " + std::to_string(jsrScore), 0xFF00E5FF);
            break;
        }

        case 4: { // Crazy Taxi: City Dash
            taxiScroll += 4;
            // San Francisco Perspective Road
            fillIpsRect(0, 0, IPS_W, 60, 0xFF0288D1); // Bay Area Sky
            fillIpsRect(0, 60, IPS_W, IPS_H - 60, 0xFF37474F); // Asphalt Road
            // Yellow Striped Dividing Line
            for (int y = 60; y < IPS_H; y += 24) {
                int py = (y + (taxiScroll % 24));
                if (py < IPS_H) fillIpsRect(118, py, 4, 12, 0xFFFFEB3B);
            }

            if (btn & 0x40) taxiX = std::max(40, taxiX - 3);
            if (btn & 0x80) taxiX = std::min(170, taxiX + 3);
            if (btn & 0x01) { taxiFare += 10; playBuzzerTone(1900, 4); }

            // Yellow Cab Convertible Sprite
            fillIpsRect(taxiX, 115, 26, 18, 0xFFFFD600); // Yellow Body
            fillIpsRect(taxiX + 4, 110, 18, 5, 0xFF212121); // Windshield
            fillIpsRect(taxiX - 2, 118, 3, 6, 0xFF212121); // Left Tire
            fillIpsRect(taxiX + 25, 118, 3, 6, 0xFF212121); // Right Tire
            fillIpsRect(taxiX + 8, 106, 10, 4, 0xFFFF1744); // Taxi Roof Sign

            fillIpsRect(6, 6, 120, 14, 0xAA000000);
            drawIpsString(8, 8, "FARE: $" + std::to_string(taxiFare) + "  T:45s", 0xFF00FF66);
            break;
        }
    }
}

void VMUProMachine::runFrame() {
    animTick++;
    stepInstruction();
    updateGameLogic();

    if (buzzerTimer > 0) buzzerTimer--;
    if (osdBannerTimer > 0) osdBannerTimer--;

    renderVMUProScreen();
    updateAudio();
}

void VMUProMachine::renderVMUProScreen() {
    std::fill(screenBuffer.begin(), screenBuffer.end(), 0xFF0A0D14);

    // Dynamic Violet/Cyan Cyber Ambient Glow
    for (int y = 0; y < CANVAS_H; y++) {
        int dy = y - 256;
        for (int x = 0; x < CANVAS_W; x++) {
            int dx = x - 320;
            int dist = dx * dx + dy * dy;
            if (dist < 160000) {
                int glow = (160000 - dist) / 10000;
                uint32 cur = screenBuffer[y * CANVAS_W + x];
                uint8 r = std::min(255, (int)((cur >> 16) & 0xFF) + glow / 2);
                uint8 b = std::min(255, (int)(cur & 0xFF) + glow);
                screenBuffer[y * CANVAS_W + x] = 0xFF000000 | (r << 16) | (((cur >> 8) & 0xFF) << 8) | b;
            }
        }
    }

    renderCasing();
    renderIPSScreen();

    if (osdBannerTimer > 0) {
        int by = 480;
        GUIFont::fillRect(screenBuffer.data(), CANVAS_W, 40, by - 4, 560, 24, 0xEE080C14);
        GUIFont::drawRect(screenBuffer.data(), CANVAS_W, 40, by - 4, 560, 24, 0xFFFF0077);
        GUIFont::drawString(screenBuffer.data(), CANVAS_W, 52, by, osdBannerText, 0xFFE6EDF3);
    }
}

void VMUProMachine::renderCasing() {
    const int bx0 = 155, bx1 = 485;
    const int by0 = 22,  by1 = 488;

    // Outer Drop Shadow
    for (int y = by0 + 10; y <= by1 + 16; y++) {
        for (int x = bx0 - 8; x <= bx1 + 12; x++) {
            if (y >= 0 && y < CANVAS_H && x >= 0 && x < CANVAS_W) {
                uint32 c = screenBuffer[y * CANVAS_W + x];
                screenBuffer[y * CANVAS_W + x] = 0xFF000000 | (((c >> 16) & 0xFF) / 2 << 16) | (((c >> 8) & 0xFF) / 2 << 8) | ((c & 0xFF) / 2);
            }
        }
    }

    // 8BitMods Stealth Translucent Smoke Black Acrylic Shell
    for (int y = by0; y <= by1; y++) {
        for (int x = bx0; x <= bx1; x++) {
            if ((x < bx0 + 24 && y < by0 + 24 && (bx0 + 24 - x)*(bx0 + 24 - x) + (by0 + 24 - y)*(by0 + 24 - y) > 576) ||
                (x > bx1 - 24 && y < by0 + 24 && (x - (bx1 - 24))*(x - (bx1 - 24)) + (by0 + 24 - y)*(by0 + 24 - y) > 576) ||
                (x < bx0 + 20 && y > by1 - 20 && (bx0 + 20 - x)*(bx0 + 20 - x) + (y - (by1 - 20))*(y - (by1 - 20)) > 400) ||
                (x > bx1 - 20 && y > by1 - 20 && (x - (bx1 - 20))*(x - (bx1 - 20)) + (y - (by1 - 20))*(y - (by1 - 20)) > 400)) {
                continue;
            }

            float grad = (float)(y - by0) / (by1 - by0);
            int base = 28 + (int)(grad * 14.0f);
            int r = base + 3, g = base + 4, b = base + 9; // Smoke tint

            // Subtle PCB copper traces visible through translucent casing
            if (((x + y) % 36 == 0) || ((x - y + 400) % 48 == 0)) {
                r = std::min(255, r + 18);
                g = std::min(255, g + 14);
                b = std::min(255, b + 5);
            }

            // Beveled edges
            if (x < bx0 + 3 || y < by0 + 3) { r += 25; g += 25; b += 35; }
            else if (x > bx1 - 4 || y > by1 - 4) { r /= 2; g /= 2; b /= 2; }

            screenBuffer[y * CANVAS_W + x] = 0xFF000000 | (r << 16) | (g << 8) | b;
        }
    }

    // Top Cap Seam
    for (int x = bx0 + 4; x <= bx1 - 4; x++) {
        screenBuffer[68 * CANVAS_W + x] = 0xFF1A1F2C;
        screenBuffer[69 * CANVAS_W + x] = 0xFF4A5568;
    }

    // 8BitMods VMU Pro Signature Branding
    GUIFont::drawString(screenBuffer.data(), CANVAS_W, 235, 90, "8BitMods VMU PRO", 0xFFFF0077);

    // Multi-Color Cycling RGB Status Ring
    float hue = (float)(animTick % 180) / 180.0f;
    int rgbR = (int)((sin(hue * 6.28) * 0.5 + 0.5) * 255);
    int rgbG = (int)((sin((hue + 0.33) * 6.28) * 0.5 + 0.5) * 255);
    int rgbB = (int)((sin((hue + 0.66) * 6.28) * 0.5 + 0.5) * 255);
    uint32 ledCol = 0xFF000000 | (rgbR << 16) | (rgbG << 8) | rgbB;

    for (int dy = -4; dy <= 4; dy++) {
        for (int dx = -4; dx <= 4; dx++) {
            if (dx * dx + dy * dy <= 16) screenBuffer[(94 + dy) * CANVAS_W + 380 + dx] = ledCol;
        }
    }

    // IPS Glass Bezel Frame (X: 184..456, Y: 104..296)
    const int wx0 = 184, wx1 = 456;
    const int wy0 = 104, wy1 = 296;
    for (int y = wy0; y <= wy1; y++) {
        for (int x = wx0; x <= wx1; x++) {
            screenBuffer[y * CANVAS_W + x] = (x == wx0 || y == wy0) ? 0xFF000000 : 0xFF0F141F;
        }
    }

    // Directional D-Pad (Center: 230, 366)
    const int dpadCX = 230, dpadCY = 366;
    int btnActive = ~buttonState;
    for (int dy = -32; dy <= 32; dy++) {
        for (int dx = -32; dx <= 32; dx++) {
            if ((abs(dx) <= 11 && abs(dy) <= 30) || (abs(dy) <= 11 && abs(dx) <= 30)) {
                uint32 col = (dy < -26 || dx < -26) ? 0xFF3A4454 : 0xFF1F242E;
                if (dx * dx + dy * dy <= 144) col = 0xFF141820;
                screenBuffer[(dpadCY + dy) * CANVAS_W + dpadCX + dx] = col;
            }
        }
    }

    // A and B Buttons (Neon Magenta & Cyan Accents)
    auto drawProBtn = [&](int cx, int cy, char label, uint32 primaryCol, bool pressed) {
        for (int dy = -16; dy <= 16; dy++) {
            for (int dx = -16; dx <= 16; dx++) {
                if (dx * dx + dy * dy <= 256) {
                    uint32 col = pressed ? 0xFF330022 : primaryCol;
                    screenBuffer[(cy + dy) * CANVAS_W + cx + dx] = col;
                }
            }
        }
        GUIFont::drawChar(screenBuffer.data(), CANVAS_W, cx - 3, cy - 4, label, 0xFFFFFFFF);
    };
    drawProBtn(405, 375, 'A', 0xFFFF0077, (btnActive & 0x01) != 0);
    drawProBtn(442, 342, 'B', 0xFF00E5FF, (btnActive & 0x02) != 0);

    // MODE & BANK Switch Buttons
    GUIFont::drawString(screenBuffer.data(), CANVAS_W, 300, 392, "MODE", 0xFF8A93A4);
    GUIFont::drawString(screenBuffer.data(), CANVAS_W, 338, 392, "BANK", 0xFFFFCC00);

    // MicroSD Card Slot Accent on Left Edge
    for (int y = 200; y <= 240; y++) {
        screenBuffer[y * CANVAS_W + bx0 + 1] = 0xFFFFD700;
        screenBuffer[y * CANVAS_W + bx0 + 2] = 0xFF141820;
    }

    // USB-C Port Cutout at Bottom
    for (int y = by1 - 10; y <= by1 - 4; y++) {
        for (int x = 308; x <= 332; x++) screenBuffer[y * CANVAS_W + x] = 0xFF0C0F14;
    }
}

void VMUProMachine::renderIPSScreen() {
    // 240 x 160 Full Color IPS Matrix positioned at X: 200, Y: 120
    const int ipsX0 = 200;
    const int ipsY0 = 120;

    // Render 240x160 IPS buffer directly into screenBuffer
    for (int y = 0; y < IPS_H; y++) {
        int dstRow = (ipsY0 + y) * CANVAS_W + ipsX0;
        int srcRow = y * IPS_W;
        for (int x = 0; x < IPS_W; x++) {
            screenBuffer[dstRow + x] = ipsBuffer[srcRow + x];
        }
    }

    // Top Full-Color Status Header
    char statusBuf[64];
    snprintf(statusBuf, sizeof(statusBuf), "BANK %03d/128  96%%  [SD]  22:36", currentBank + 1);
    GUIFont::fillRect(screenBuffer.data(), CANVAS_W, ipsX0, ipsY0, IPS_W, 14, 0xDD000000);
    GUIFont::drawString(screenBuffer.data(), CANVAS_W, ipsX0 + 4, ipsY0 + 2, statusBuf, 0xFF00FFCC);
}

void VMUProMachine::updateAudio() {
    int16 sample = 0;
    if (buzzerTimer > 0) {
        buzzerPhase += (buzzerFreq / 44100.0);
        if (buzzerPhase >= 1.0) buzzerPhase -= 1.0;
        sample = (buzzerPhase < 0.5) ? 7500 : -7500;
    }
    std::fill(audioBuffer.begin(), audioBuffer.end(), sample);
}

void VMUProMachine::handleKeyDown(SDL_Keycode key, uint16 mod) {
    (void)mod;
    switch (key) {
        case SDLK_UP:    buttonState &= ~0x10; playBuzzerTone(1400, 3); break;
        case SDLK_DOWN:  buttonState &= ~0x20; playBuzzerTone(1400, 3); break;
        case SDLK_LEFT:  buttonState &= ~0x40; playBuzzerTone(1400, 3); break;
        case SDLK_RIGHT: buttonState &= ~0x80; playBuzzerTone(1400, 3); break;
        case SDLK_z: case SDLK_a: case SDLK_RETURN: buttonState &= ~0x01; playBuzzerTone(2000, 4); break;
        case SDLK_x: case SDLK_s: case SDLK_SPACE:  buttonState &= ~0x02; playBuzzerTone(1700, 4); break;
        case SDLK_m: case SDLK_TAB: currentApp = (currentApp + 1) % 3; playBuzzerTone(2200, 6); break;
        case SDLK_b: case SDLK_p: case SDLK_PAGEUP: setBank(currentBank + 1); break;
        case SDLK_PAGEDOWN: setBank(currentBank - 1); break;
        case SDLK_F2: cycleNextGame(); break;
        case SDLK_F12: reset(); break;
        default: break;
    }
}

void VMUProMachine::handleKeyUp(SDL_Keycode key, uint16 mod) {
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

void VMUProMachine::handleMouseMotion(int x, int y) {
    mouseX = x; mouseY = y;
}

void VMUProMachine::handleMouseButton(uint8 button, bool pressed, int x, int y) {
    if (button != SDL_BUTTON_LEFT) return;
    mouseDown = pressed;
    if (!pressed) { buttonState = 0xFF; return; }

    if (y >= 22 && y <= 68) { cycleNextGame(); return; }
    if (abs(x - 345) <= 18 && abs(y - 392) <= 10) { setBank(currentBank + 1); return; }
    if (abs(x - 310) <= 18 && abs(y - 392) <= 10) { currentApp = (currentApp + 1) % 3; return; }

    int ddx = x - 230, ddy = y - 366;
    if (abs(ddx) <= 30 && abs(ddy) <= 30) {
        if (ddy < -8) buttonState &= ~0x10;
        else if (ddy > 8) buttonState &= ~0x20;
        else if (ddx < -8) buttonState &= ~0x40;
        else if (ddx > 8) buttonState &= ~0x80;
        playBuzzerTone(1400, 3);
        return;
    }

    if ((x - 405)*(x - 405) + (y - 375)*(y - 375) <= 400) { buttonState &= ~0x01; playBuzzerTone(2000, 4); return; }
    if ((x - 442)*(x - 442) + (y - 342)*(y - 342) <= 400) { buttonState &= ~0x02; playBuzzerTone(1700, 4); return; }
}
