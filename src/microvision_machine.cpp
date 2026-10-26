#include "microvision_machine.h"
#include "embedded_roms_ext.h"
#include "gui_font.h"
#include <cstring>
#include <iostream>
#include <algorithm>
#include <cmath>

static const uint8 BIT_REVERSE[16] = { 0,8,4,12,2,10,6,14,1,9,5,13,3,11,7,15 };

MicrovisionMachine::MicrovisionMachine() {
    screenBuffer.resize(640 * 512, 0xFF14161B);
    audioBuffer.resize(44100 / 60, 0);
    init();
}

void MicrovisionMachine::init() {
    currentCartridge = 0;
    switchCartridge(0);
}

void MicrovisionMachine::switchCartridge(int id) {
    currentCartridge = id;
    const uint8* romData = nullptr;
    uint16 csum = 0;

    switch (id) {
        case 0:
            romData = EmbeddedROMs::MICROVISION_BLOCK_ROM;
            csum = 0x5091; // CSUM_BLOCKBUSTER
            currentMedia = "Block Buster (Cartridge 1 - 1979)";
            rotaryEnabled = true;
            break;
        case 1:
            romData = EmbeddedROMs::MICROVISION_BOWLING_ROM;
            csum = 0x49B1; // CSUM_BOWLING
            currentMedia = "Bowling (Cartridge 2 - 1979)";
            rotaryEnabled = true;
            break;
        case 2:
            romData = EmbeddedROMs::MICROVISION_CONNECT4_ROM;
            csum = 0x55BA; // CSUM_CONNECTFOUR
            currentMedia = "Connect Four (Cartridge 3 - 1979)";
            rotaryEnabled = true;
            break;
        case 3:
            romData = EmbeddedROMs::MICROVISION_PINBALL_ROM;
            csum = 0xFD21; // CSUM_PINBALL
            currentMedia = "Pinball (Cartridge 4 - 1979)";
            rotaryEnabled = true;
            break;
        default:
            romData = EmbeddedROMs::MICROVISION_BLOCK_ROM;
            csum = 0x5091;
            currentMedia = "Block Buster (Cartridge 1 - 1979)";
            rotaryEnabled = true;
            break;
    }

    setROM(romData, csum);
    reset();
}

void MicrovisionMachine::reset() {
    CPUTMS1100::reset();

    dataLine = 0;
    latchPulse = 0;
    notDataClock = 0;
    addressedLatchCounter = 0;
    std::memset(addressedLatches, 0, sizeof(addressedLatches));
    std::memset(lcdRows, 0, sizeof(lcdRows));
    std::memset(lcdDecay, 0, sizeof(lcdDecay));
    lcdPolarity = false;

    paddleValue = 50;
    chargeLevel = 0;
    chargeEndTime = 0;

    soundLevel = 0;
    soundToggleCount = 0;
    soundPhase = 0;

    std::memset(keyState, 0, sizeof(keyState));
}

void MicrovisionMachine::standardProcessing() {
    if (notDataClock == 0) {
        addressedLatches[addressedLatchCounter] = dataLine & 0xF;
    }

    if (latchPulse != 0 && notDataClock != 0) {
        transferHoldingLatches();
    }

    if (latchPulse != 0) {
        addressedLatchCounter = 0;
    }
}

void MicrovisionMachine::transferHoldingLatches() {
    uint16 rowData = (addressedLatches[0] << 12) | (addressedLatches[1] << 8) |
                     (addressedLatches[2] << 4)  | addressedLatches[3];
    uint16 colData = (addressedLatches[4] << 12) | (addressedLatches[5] << 8) |
                     (addressedLatches[6] << 4)  | addressedLatches[7];

    if (rowData == 0 || colData == 0) return;

    for (int r = 0; r < 16; r++) {
        if (rowData & (0x8000 >> r)) {
            lcdRows[r] = colData;
            for (int c = 0; c < 16; c++) {
                if (colData & (0x8000 >> c)) {
                    lcdDecay[r][c] = 4; // Realistic liquid crystal decay
                }
            }
        }
    }
}

void MicrovisionMachine::onWriteO(uint8 data) {
    if (romChecksum == 0x49B1 || romChecksum == 0x55BA) { // Bowling / Connect Four
        dataLine = data & 0xF;
    } else {
        dataLine = BIT_REVERSE[data & 0xF];
    }
    standardProcessing();
}

void MicrovisionMachine::onWriteR(int line, int level) {
    level = (level != 0) ? 1 : 0;

    if (line == 6) { // Latch Pulse
        if (latchPulse != 0 && level == 0 && notDataClock == 0) {
            lcdPolarity = !lcdPolarity;
        }
        latchPulse = level;
        standardProcessing();
    } else if (line == 7) { // !Data Clock
        if (notDataClock == 0 && level != 0) {
            addressedLatchCounter = (addressedLatchCounter + 1) & 7;
        }
        notDataClock = level;
        standardProcessing();
    } else if (line == 2) { // Rotary Knob capacitor charge pin
        if (level != 0 && chargeLevel == 0) {
            // Charging RC circuit
            chargeEndTime = cycleCounter + 600 + (65 * paddleValue / 10);
        }
        chargeLevel = level;
    } else if (line == 0) { // Speaker piezo
        if (level != soundLevel && level != 0) {
            soundToggleCount++;
        }
        soundLevel = level;
    }
}

uint8 MicrovisionMachine::onReadK() {
    uint8 val = 0;

    if (r[10]) { // Column 0 (buttons 0, 3, 6, 9)
        for (int row = 0; row < 4; row++) {
            if (keyState[0][row]) val |= (8 >> row);
        }
    }
    if (r[9]) { // Column 1 (buttons 1, 4, 7, 10)
        for (int row = 0; row < 4; row++) {
            if (keyState[1][row]) val |= (8 >> row);
        }
    }
    if (r[8]) { // Column 2 (buttons 2, 5, 8, 11)
        for (int row = 0; row < 4; row++) {
            if (keyState[2][row]) val |= (8 >> row);
        }
    }

    if (rotaryEnabled) {
        val &= 0x7;
        if (chargeLevel != 0 && cycleCounter > chargeEndTime) {
            val |= 8; // K8 comparator fires when capacitor is charged
        }
    }

    return val;
}

void MicrovisionMachine::onSyncFrame() {
}

void MicrovisionMachine::runFrame() {
    // 500kHz / 6 clock divider = 83,333 cycles/sec -> ~1388 cycles per 60fps frame
    execute(1400);

    // Liquid crystal persistence decay
    for (int r = 0; r < 16; r++) {
        for (int c = 0; c < 16; c++) {
            if (lcdDecay[r][c] > 0) lcdDecay[r][c]--;
        }
    }

    renderConsole();
    updateAudio();
}

void MicrovisionMachine::renderConsole() {
    std::fill(screenBuffer.begin(), screenBuffer.end(), 0xFF14161B);

    // Realistic Microvision Plastic Housing (Charcoal / Cream faceplate)
    const int CX = 260;
    const int CY = 16;
    const int CW = 280;
    const int CH = 480;

    // Body outer shell
    GUIFont::fillRect(screenBuffer.data(), 640 * 4, CX, CY, CW, CH, 0xFF2A2D34);
    GUIFont::drawRect(screenBuffer.data(), 640 * 4, CX, CY, CW, CH, 0xFF454B56);

    // Title & Brand
    GUIFont::drawString(screenBuffer.data(), 640 * 4, CX + 40, CY + 14, "MILTON BRADLEY", 0xFFE0C060, 0xFF2A2D34);
    GUIFont::drawString(screenBuffer.data(), 640 * 4, CX + 48, CY + 28, "MICROVISION", 0xFFFFFFFF, 0xFF2A2D34);

    // Cartridge bezel
    const int BZ_X = CX + 20;
    const int BZ_Y = CY + 48;
    const int BZ_W = CW - 40;
    const int BZ_H = 200;
    GUIFont::fillRect(screenBuffer.data(), 640 * 4, BZ_X, BZ_Y, BZ_W, BZ_H, 0xFF1A1C20);
    GUIFont::drawRect(screenBuffer.data(), 640 * 4, BZ_X, BZ_Y, BZ_W, BZ_H, 0xFF555B66);

    // 16x16 LCD Screen Window (160x160 pixels, centered inside cartridge bezel)
    const int LCD_X = BZ_X + (BZ_W - 160) / 2;
    const int LCD_Y = BZ_Y + 20;
    const int LCD_SIZE = 160;
    const int CELL_SIZE = 10;

    // LCD background (pale green reflective vintage LCD)
    GUIFont::fillRect(screenBuffer.data(), 640 * 4, LCD_X - 4, LCD_Y - 4, LCD_SIZE + 8, LCD_SIZE + 8, 0xFF687860);
    GUIFont::drawRect(screenBuffer.data(), 640 * 4, LCD_X - 4, LCD_Y - 4, LCD_SIZE + 8, LCD_SIZE + 8, 0xFF404C38);

    // Render 16x16 pixels from TMS1100 Hughes 0488 hardware
    for (int r = 0; r < 16; r++) {
        for (int c = 0; c < 16; c++) {
            bool on = (lcdDecay[r][c] > 0) || (lcdRows[r] & (0x8000 >> c));
            uint32 color = on ? 0xFF101410 : 0xFF708068; // Dark ink on LCD matrix
            GUIFont::fillRect(screenBuffer.data(), 640 * 4, LCD_X + c * CELL_SIZE, LCD_Y + r * CELL_SIZE, CELL_SIZE - 1, CELL_SIZE - 1, color);
        }
    }

    // 12 Membrane Buttons (3 columns x 4 rows)
    const int BTN_START_X = CX + 50;
    const int BTN_START_Y = CY + 260;
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 3; col++) {
            int bx = BTN_START_X + col * 64;
            int by = BTN_START_Y + row * 24;
            bool pressed = keyState[col][row];
            uint32 btnCol = pressed ? 0xFF993333 : 0xFF3D434D;
            GUIFont::fillRect(screenBuffer.data(), 640 * 4, bx, by, 52, 18, btnCol);
            GUIFont::drawRect(screenBuffer.data(), 640 * 4, bx, by, 52, 18, 0xFF656D7C);

            int btnNum = row * 3 + col + 1;
            char numStr[4];
            snprintf(numStr, sizeof(numStr), "%d", btnNum);
            GUIFont::drawString(screenBuffer.data(), 640 * 4, bx + 22, by + 5, numStr, 0xFFFFFFFF, btnCol);
        }
    }

    // Rotary Paddle Knob (Bottom of console)
    const int KNOB_X = CX + CW / 2;
    const int KNOB_Y = CY + 410;
    GUIFont::fillRect(screenBuffer.data(), 640 * 4, KNOB_X - 60, KNOB_Y - 24, 120, 48, 0xFF1E2024);
    GUIFont::drawRect(screenBuffer.data(), 640 * 4, KNOB_X - 60, KNOB_Y - 24, 120, 48, 0xFF555B66);
    
    // Indicator mark on knob corresponding to paddleValue (0..100)
    int markerX = (KNOB_X - 50) + (paddleValue * 100) / 100;
    GUIFont::fillRect(screenBuffer.data(), 640 * 4, markerX - 2, KNOB_Y - 20, 4, 40, 0xFFFFCC33);
    GUIFont::drawString(screenBuffer.data(), 640 * 4, KNOB_X - 20, KNOB_Y + 28, "PADDLE", 0xFF888888, 0xFF2A2D34);

    // Sidebar Info
    GUIFont::drawString(screenBuffer.data(), 640 * 4, 20, 20, "MILTON BRADLEY MICROVISION", 0xFF61AFEF, 0xFF14161B);
    GUIFont::drawString(screenBuffer.data(), 640 * 4, 20, 36, "Pure TMS1100 4-bit CPU & LCD", 0xFF98C379, 0xFF14161B);
    GUIFont::drawString(screenBuffer.data(), 640 * 4, 20, 60, currentMedia.c_str(), 0xFFE5C07B, 0xFF14161B);

    char statStr[64];
    snprintf(statStr, sizeof(statStr), "PC:0x%03X  A:%X  X:%d  Y:%X", getPCTR(), a, x, y);
    GUIFont::drawString(screenBuffer.data(), 640 * 4, 20, 80, statStr, 0xFFABB2BF, 0xFF14161B);

    GUIFont::drawString(screenBuffer.data(), 640 * 4, 20, 110, "Hotkeys:", 0xFF61AFEF, 0xFF14161B);
    GUIFont::drawString(screenBuffer.data(), 640 * 4, 20, 126, "1-4: Switch Cartridge", 0xFFABB2BF, 0xFF14161B);
    GUIFont::drawString(screenBuffer.data(), 640 * 4, 20, 142, "Left/Right: Paddle Knob", 0xFFABB2BF, 0xFF14161B);
    GUIFont::drawString(screenBuffer.data(), 640 * 4, 20, 158, "Keypad: Q,W,E / A,S,D / Z,X,C", 0xFFABB2BF, 0xFF14161B);
}

void MicrovisionMachine::updateAudio() {
    int samples = audioBuffer.size();
    if (soundToggleCount > 0) {
        int freq = 800;
        int step = (freq * 65536) / 44100;
        for (int i = 0; i < samples; i++) {
            soundPhase += step;
            audioBuffer[i] = ((soundPhase >> 15) & 1) ? 12000 : -12000;
        }
        soundToggleCount = 0;
    } else {
        std::fill(audioBuffer.begin(), audioBuffer.end(), 0);
    }
}

void MicrovisionMachine::handleKeyDown(SDL_Keycode key, uint16 mod) {
    if (key >= SDLK_1 && key <= SDLK_4) {
        switchCartridge(key - SDLK_1);
        return;
    }

    if (key == SDLK_LEFT) {
        paddleValue = std::max(0, paddleValue - 5);
    } else if (key == SDLK_RIGHT) {
        paddleValue = std::min(100, paddleValue + 5);
    }

    // 3x4 Button Matrix Mapping:
    // Q, W, E -> Row 0 (Buttons 0, 1, 2)
    // A, S, D -> Row 1 (Buttons 3, 4, 5)
    // Z, X, C -> Row 2 (Buttons 6, 7, 8)
    // R, T, Y -> Row 3 (Buttons 9, 10, 11)
    if (key == SDLK_q) keyState[0][0] = true;
    if (key == SDLK_w) keyState[1][0] = true;
    if (key == SDLK_e) keyState[2][0] = true;

    if (key == SDLK_a) keyState[0][1] = true;
    if (key == SDLK_s) keyState[1][1] = true;
    if (key == SDLK_d) keyState[2][1] = true;

    if (key == SDLK_z) keyState[0][2] = true;
    if (key == SDLK_x) keyState[1][2] = true;
    if (key == SDLK_c) keyState[2][2] = true;

    if (key == SDLK_r) keyState[0][3] = true;
    if (key == SDLK_t) keyState[1][3] = true;
    if (key == SDLK_y) keyState[2][3] = true;
}

void MicrovisionMachine::handleKeyUp(SDL_Keycode key, uint16 mod) {
    if (key == SDLK_q) keyState[0][0] = false;
    if (key == SDLK_w) keyState[1][0] = false;
    if (key == SDLK_e) keyState[2][0] = false;

    if (key == SDLK_a) keyState[0][1] = false;
    if (key == SDLK_s) keyState[1][1] = false;
    if (key == SDLK_d) keyState[2][1] = false;

    if (key == SDLK_z) keyState[0][2] = false;
    if (key == SDLK_x) keyState[1][2] = false;
    if (key == SDLK_c) keyState[2][2] = false;

    if (key == SDLK_r) keyState[0][3] = false;
    if (key == SDLK_t) keyState[1][3] = false;
    if (key == SDLK_y) keyState[2][3] = false;
}

void MicrovisionMachine::handleMouseMotion(int x, int y) {
    // Map mouse X (260..540) to paddle 0..100
    if (x >= 260 && x <= 540) {
        paddleValue = ((x - 260) * 100) / 280;
    }
}

bool MicrovisionMachine::loadMedia(const std::string& path) {
    return false;
}
