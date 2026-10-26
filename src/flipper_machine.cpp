#include "flipper_machine.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>

// Standard 5x7 ASCII font for Flipper Zero 128x64 display
static const uint8 FLIPPER_FONT5x7[96][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // ' ' (32)
    {0x00, 0x00, 0x5F, 0x00, 0x00}, // '!'
    {0x00, 0x07, 0x00, 0x07, 0x00}, // '"'
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, // '#'
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // '$'
    {0x23, 0x13, 0x08, 0x64, 0x62}, // '%'
    {0x36, 0x49, 0x55, 0x22, 0x50}, // '&'
    {0x00, 0x05, 0x03, 0x00, 0x00}, // '''
    {0x00, 0x1C, 0x22, 0x41, 0x00}, // '('
    {0x00, 0x41, 0x22, 0x1C, 0x00}, // ')'
    {0x14, 0x08, 0x3E, 0x08, 0x14}, // '*'
    {0x08, 0x08, 0x3E, 0x08, 0x08}, // '+'
    {0x00, 0x50, 0x30, 0x00, 0x00}, // ','
    {0x08, 0x08, 0x08, 0x08, 0x08}, // '-'
    {0x00, 0x60, 0x60, 0x00, 0x00}, // '.'
    {0x20, 0x10, 0x08, 0x04, 0x02}, // '/'
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // '0'
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // '1'
    {0x42, 0x61, 0x51, 0x49, 0x46}, // '2'
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // '3'
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // '4'
    {0x27, 0x45, 0x45, 0x45, 0x39}, // '5'
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // '6'
    {0x01, 0x71, 0x09, 0x05, 0x03}, // '7'
    {0x36, 0x49, 0x49, 0x49, 0x36}, // '8'
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // '9'
    {0x00, 0x36, 0x36, 0x00, 0x00}, // ':'
    {0x00, 0x56, 0x36, 0x00, 0x00}, // ';'
    {0x08, 0x14, 0x22, 0x41, 0x00}, // '<'
    {0x14, 0x14, 0x14, 0x14, 0x14}, // '='
    {0x00, 0x41, 0x22, 0x14, 0x08}, // '>'
    {0x02, 0x01, 0x51, 0x09, 0x06}, // '?'
    {0x32, 0x49, 0x79, 0x41, 0x3E}, // '@'
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // 'A'
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // 'B'
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // 'C'
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // 'D'
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // 'E'
    {0x7F, 0x09, 0x09, 0x09, 0x01}, // 'F'
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, // 'G'
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // 'H'
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // 'I'
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // 'J'
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // 'K'
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // 'L'
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // 'M'
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // 'N'
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // 'O'
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // 'P'
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // 'Q'
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // 'R'
    {0x46, 0x49, 0x49, 0x49, 0x31}, // 'S'
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // 'T'
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // 'U'
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // 'V'
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, // 'W'
    {0x63, 0x14, 0x08, 0x14, 0x63}, // 'X'
    {0x07, 0x08, 0x70, 0x08, 0x07}, // 'Y'
    {0x61, 0x51, 0x49, 0x45, 0x43}, // 'Z'
    {0x00, 0x7F, 0x41, 0x41, 0x00}, // '['
    {0x02, 0x04, 0x08, 0x10, 0x20}, // '\'
    {0x00, 0x41, 0x41, 0x7F, 0x00}, // ']'
    {0x04, 0x02, 0x01, 0x02, 0x04}, // '^'
    {0x40, 0x40, 0x40, 0x40, 0x40}, // '_'
    {0x00, 0x01, 0x02, 0x04, 0x00}, // '`'
    {0x20, 0x54, 0x54, 0x54, 0x78}, // 'a'
    {0x7F, 0x48, 0x44, 0x44, 0x38}, // 'b'
    {0x38, 0x44, 0x44, 0x44, 0x20}, // 'c'
    {0x38, 0x44, 0x44, 0x48, 0x7F}, // 'd'
    {0x38, 0x54, 0x54, 0x54, 0x18}, // 'e'
    {0x08, 0x7E, 0x09, 0x01, 0x02}, // 'f'
    {0x0C, 0x52, 0x52, 0x52, 0x3E}, // 'g'
    {0x7F, 0x08, 0x04, 0x04, 0x78}, // 'h'
    {0x00, 0x44, 0x7D, 0x40, 0x00}, // 'i'
    {0x20, 0x40, 0x44, 0x3D, 0x00}, // 'j'
    {0x7F, 0x10, 0x28, 0x44, 0x00}, // 'k'
    {0x00, 0x41, 0x7F, 0x40, 0x00}, // 'l'
    {0x7C, 0x04, 0x18, 0x04, 0x78}, // 'm'
    {0x7C, 0x08, 0x04, 0x04, 0x78}, // 'n'
    {0x38, 0x44, 0x44, 0x44, 0x38}, // 'o'
    {0x7C, 0x14, 0x14, 0x14, 0x08}, // 'p'
    {0x08, 0x14, 0x14, 0x18, 0x7C}, // 'q'
    {0x7C, 0x08, 0x04, 0x04, 0x08}, // 'r'
    {0x48, 0x54, 0x54, 0x54, 0x20}, // 's'
    {0x04, 0x3F, 0x44, 0x40, 0x20}, // 't'
    {0x3C, 0x40, 0x40, 0x20, 0x7C}, // 'u'
    {0x1C, 0x20, 0x40, 0x20, 0x1C}, // 'v'
    {0x3C, 0x40, 0x30, 0x40, 0x3C}, // 'w'
    {0x44, 0x28, 0x10, 0x28, 0x44}, // 'x'
    {0x0C, 0x50, 0x50, 0x50, 0x3C}, // 'y'
    {0x44, 0x64, 0x54, 0x4C, 0x44}, // 'z'
    {0x00, 0x08, 0x36, 0x41, 0x00}, // '{'
    {0x00, 0x00, 0x7F, 0x00, 0x00}, // '|'
    {0x00, 0x41, 0x36, 0x08, 0x00}, // '}'
    {0x08, 0x08, 0x2A, 0x1C, 0x08}, // '~'
    {0x7F, 0x7F, 0x7F, 0x7F, 0x7F}  // DEL
};

FlipperMachine::FlipperMachine() {
    screenBuffer.fill(0xFF16171A);
    audioBuffer.resize(735, 0); // 44100 / 60
    init();
}

void FlipperMachine::init() {
    lcdVRAM.fill(0);
    buttonState = 0;
    lastButtonState = 0;
    std::memset(buttonRepeatTimer, 0, sizeof(buttonRepeatTimer));
    waterfallBuffer[0] = 0;
    std::memset(waterfallBuffer, 0, sizeof(waterfallBuffer));

    buzzerFreq = 0.0f;
    buzzerPhase = 0.0f;
    buzzerVol = 0.0f;
    buzzerRemainingSamples = 0;

    ledColor = 0xFF0035A0; // Ambient blue
    ledPulseTimer = 0;

    currentState = MenuState::Desktop;
    mainMenuIndex = 0;
    subMenuIndex = 0;
    scrollOffset = 0;

    dolphinMood = 0;
    dolphinAnimFrame = 0;
    dolphinLevel = 3;
    dolphinXP = 72;
    dolphinTimer = 0;
    dolphinSpeech = "Ready to hack!";

    subGhzFreq = 433.92f;
    subGhzRssi = -68;
    subGhzTransmitting = false;
    subGhzSignalTimer = 0;
    subGhzDetectedProtocol = 0;

    rfidScanProgress = 0;
    rfidCardDetected = false;
    nfcScanProgress = 0;
    nfcCardDetected = false;
    ibuttonScanProgress = 0;
    ibuttonDetected = false;

    irCommandIndex = 0;
    irTransmitTimer = 0;

    badUsbProgress = 0;
    badUsbTotal = 100;
    badUsbStatus = "Ready to inject";

    musicTrackIndex = 0;
    musicNoteIndex = 0;
    musicTick = 0;
    musicPlaying = false;

    initSnake();
    initTetris();
    initMicroDoom();

    // Startup chirp
    playSuccessBeep();
}

void FlipperMachine::reset() {
    init();
}

std::string FlipperMachine::getMediaName() const {
    switch (currentState) {
        case MenuState::Desktop: return "Flipper OS v0.98.3 (Dolphin Idle)";
        case MenuState::MainMenu: return "Flipper OS Main Menu";
        case MenuState::SubGhz_Analyzer: return "Sub-GHz Frequency Analyzer (433.92 MHz)";
        case MenuState::SubGhz_Read: return "Sub-GHz Protocol Sniffer";
        case MenuState::SubGhz_Saved: return "Sub-GHz Saved Remotes";
        case MenuState::RFID_Read: return "125 kHz RFID Reader";
        case MenuState::RFID_Saved: return "125 kHz RFID Key Fob Emulator";
        case MenuState::NFC_Read: return "13.56 MHz NFC Tag Reader";
        case MenuState::NFC_Saved: return "13.56 MHz NFC Tag Emulator";
        case MenuState::Infrared_Remote: return "Infrared Universal Remote";
        case MenuState::Infrared_Learn: return "Infrared Pulse Analyzer";
        case MenuState::iButton_Read: return "iButton 1-Wire Key Reader";
        case MenuState::BadUSB_Run: return "BadUSB Keystroke Injection";
        case MenuState::GPIO_Tools: return "GPIO & Hardware Power Telemetry";
        case MenuState::Game_Snake: return "FAP: Snake Game";
        case MenuState::Game_Tetris: return "FAP: Tetris Game";
        case MenuState::Game_MicroDoom: return "FAP: Micro-Doom 3D";
        case MenuState::Music_Player: return "FAP: Chiptune Music Player";
        case MenuState::Spectrum_Waterfall: return "Sub-GHz Waterfall Spectrum";
        default: return "Flipper Zero Portable Multi-Tool";
    }
}

// ============================================================================
// 128x64 Monochrome LCD Drawing Engine
// ============================================================================

void FlipperMachine::clearLcd(bool set) {
    lcdVRAM.fill(set ? 0xFF : 0x00);
}

void FlipperMachine::setPixel(int x, int y, bool set) {
    if (x < 0 || x >= LCD_W || y < 0 || y >= LCD_H) return;
    int page = y / 8;
    int bit = y % 8;
    int idx = page * LCD_W + x;
    if (set) {
        lcdVRAM[idx] |= (1 << bit);
    } else {
        lcdVRAM[idx] &= ~(1 << bit);
    }
}

bool FlipperMachine::getPixel(int x, int y) const {
    if (x < 0 || x >= LCD_W || y < 0 || y >= LCD_H) return false;
    int page = y / 8;
    int bit = y % 8;
    int idx = page * LCD_W + x;
    return (lcdVRAM[idx] & (1 << bit)) != 0;
}

void FlipperMachine::drawLine(int x0, int y0, int x1, int y1, bool set) {
    int dx = std::abs(x1 - x0);
    int dy = std::abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    while (true) {
        setPixel(x0, y0, set);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void FlipperMachine::drawRect(int x0, int y0, int w, int h, bool set) {
    drawLine(x0, y0, x0 + w - 1, y0, set);
    drawLine(x0, y0 + h - 1, x0 + w - 1, y0 + h - 1, set);
    drawLine(x0, y0, x0, y0 + h - 1, set);
    drawLine(x0 + w - 1, y0, x0 + w - 1, y0 + h - 1, set);
}

void FlipperMachine::fillRect(int x0, int y0, int w, int h, bool set) {
    for (int y = y0; y < y0 + h; y++) {
        for (int x = x0; x < x0 + w; x++) {
            setPixel(x, y, set);
        }
    }
}

void FlipperMachine::drawCircle(int xc, int yc, int r, bool set) {
    int x = 0;
    int y = r;
    int d = 3 - 2 * r;
    auto plot8 = [&](int cx, int cy, int px, int py) {
        setPixel(cx + px, cy + py, set);
        setPixel(cx - px, cy + py, set);
        setPixel(cx + px, cy - py, set);
        setPixel(cx - px, cy - py, set);
        setPixel(cx + py, cy + px, set);
        setPixel(cx - py, cy + px, set);
        setPixel(cx + py, cy - px, set);
        setPixel(cx - py, cy - px, set);
    };
    plot8(xc, yc, x, y);
    while (y >= x) {
        x++;
        if (d > 0) {
            y--;
            d = d + 4 * (x - y) + 10;
        } else {
            d = d + 4 * x + 6;
        }
        plot8(xc, yc, x, y);
    }
}

void FlipperMachine::fillCircle(int xc, int yc, int r, bool set) {
    for (int y = -r; y <= r; y++) {
        int w = (int)std::round(std::sqrt((double)(r * r - y * y)));
        for (int x = -w; x <= w; x++) {
            setPixel(xc + x, yc + y, set);
        }
    }
}

void FlipperMachine::drawGlyph(char c, int x, int y, bool invert) {
    if (c < 32 || c > 127) c = '?';
    const uint8* glyph = FLIPPER_FONT5x7[c - 32];
    for (int col = 0; col < 5; col++) {
        uint8 bits = glyph[col];
        for (int row = 0; row < 7; row++) {
            bool pixelOn = (bits & (1 << row)) != 0;
            if (invert) pixelOn = !pixelOn;
            setPixel(x + col, y + row, pixelOn);
        }
    }
}

void FlipperMachine::drawString(const std::string& str, int x, int y, bool invert) {
    int curX = x;
    int curY = y;
    for (char c : str) {
        if (c == '\n') {
            curX = x;
            curY += 8;
            continue;
        }
        drawGlyph(c, curX, curY, invert);
        curX += 6;
    }
}

void FlipperMachine::drawBitmap(int x, int y, int w, int h, const uint8* data, bool invert) {
    for (int r = 0; r < h; r++) {
        for (int c = 0; c < w; c++) {
            int byteIdx = (r * w + c) / 8;
            int bitIdx = 7 - ((r * w + c) % 8);
            bool bit = (data[byteIdx] & (1 << bitIdx)) != 0;
            if (invert) bit = !bit;
            setPixel(x + c, y + r, bit);
        }
    }
}

// ============================================================================
// Sound & Piezo Buzzer
// ============================================================================

void FlipperMachine::playTone(float freqHz, int durationMs, float volume) {
    buzzerFreq = freqHz;
    buzzerVol = volume;
    buzzerRemainingSamples = (int)(44100.0f * ((float)durationMs / 1000.0f));
}

void FlipperMachine::playClick() {
    playTone(1800.0f, 6, 0.4f);
}

void FlipperMachine::playSuccessBeep() {
    playTone(2400.0f, 40, 0.6f);
}

void FlipperMachine::playErrorBuzz() {
    playTone(320.0f, 80, 0.7f);
}

void FlipperMachine::updateAudio() {
    float dt = 1.0f / 44100.0f;
    for (size_t i = 0; i < audioBuffer.size(); i++) {
        if (buzzerRemainingSamples > 0 && buzzerFreq > 0.0f) {
            // PWM square wave generator
            float sample = (buzzerPhase < 0.5f) ? 1.0f : -1.0f;
            audioBuffer[i] = (int16)(sample * buzzerVol * 24000.0f);
            buzzerPhase += buzzerFreq * dt;
            if (buzzerPhase >= 1.0f) buzzerPhase -= 1.0f;
            buzzerRemainingSamples--;
        } else {
            audioBuffer[i] = 0;
        }
    }
}

// ============================================================================
// Input Handling
// ============================================================================

bool FlipperMachine::isPressed(int btn) const {
    return (buttonState & (1 << btn)) != 0;
}

bool FlipperMachine::isTriggered(int btn) {
    bool cur = (buttonState & (1 << btn)) != 0;
    bool last = (lastButtonState & (1 << btn)) != 0;
    if (cur && !last) {
        buttonRepeatTimer[btn] = 14;
        return true;
    }
    if (cur && last) {
        if (buttonRepeatTimer[btn] > 0) {
            buttonRepeatTimer[btn]--;
            if (buttonRepeatTimer[btn] == 0) {
                buttonRepeatTimer[btn] = 4;
                return true;
            }
        }
    }
    return false;
}

void FlipperMachine::handleKeyDown(SDL_Keycode key, uint16 mod) {
    switch (key) {
        case SDLK_UP:    buttonState |= (1 << 0); break;
        case SDLK_DOWN:  buttonState |= (1 << 1); break;
        case SDLK_LEFT:  buttonState |= (1 << 2); break;
        case SDLK_RIGHT: buttonState |= (1 << 3); break;
        case SDLK_RETURN:
        case SDLK_SPACE:
        case SDLK_z:     buttonState |= (1 << 4); break; // OK
        case SDLK_ESCAPE:
        case SDLK_BACKSPACE:
        case SDLK_x:     buttonState |= (1 << 5); break; // Back
        default: break;
    }
}

void FlipperMachine::handleKeyUp(SDL_Keycode key, uint16 mod) {
    switch (key) {
        case SDLK_UP:    buttonState &= ~(1 << 0); break;
        case SDLK_DOWN:  buttonState &= ~(1 << 1); break;
        case SDLK_LEFT:  buttonState &= ~(1 << 2); break;
        case SDLK_RIGHT: buttonState &= ~(1 << 3); break;
        case SDLK_RETURN:
        case SDLK_SPACE:
        case SDLK_z:     buttonState &= ~(1 << 4); break;
        case SDLK_ESCAPE:
        case SDLK_BACKSPACE:
        case SDLK_x:     buttonState &= ~(1 << 5); break;
        default: break;
    }
}

void FlipperMachine::handleMouseMotion(int x, int y) {}

void FlipperMachine::handleMouseButton(uint8 button, bool pressed, int x, int y) {
    if (button == SDL_BUTTON_LEFT) {
        // Physical casing D-pad coordinates on 640x512 canvas:
        // D-Pad center at (568, 176), radius 42
        int dpadCx = 568, dpadCy = 176;
        int dx = x - dpadCx;
        int dy = y - dpadCy;
        int distSq = dx * dx + dy * dy;

        // Back button at (568, 256), width 40, height 24
        int backX = 548, backY = 244, backW = 40, backH = 24;

        if (distSq <= 16 * 16) {
            // OK Center Button
            if (pressed) buttonState |= (1 << 4);
            else buttonState &= ~(1 << 4);
        } else if (distSq <= 46 * 46) {
            // D-Pad Ring
            if (std::abs(dx) > std::abs(dy)) {
                if (dx > 0) {
                    if (pressed) buttonState |= (1 << 3); // Right
                    else buttonState &= ~(1 << 3);
                } else {
                    if (pressed) buttonState |= (1 << 2); // Left
                    else buttonState &= ~(1 << 2);
                }
            } else {
                if (dy > 0) {
                    if (pressed) buttonState |= (1 << 1); // Down
                    else buttonState &= ~(1 << 1);
                } else {
                    if (pressed) buttonState |= (1 << 0); // Up
                    else buttonState &= ~(1 << 0);
                }
            }
        } else if (x >= backX && x <= backX + backW && y >= backY && y <= backY + backH) {
            // Back Button
            if (pressed) buttonState |= (1 << 5);
            else buttonState &= ~(1 << 5);
        }
    }
}

// ============================================================================
// Media & Tool Loading
// ============================================================================

bool FlipperMachine::loadMedia(const std::string& path) {
    std::string lower = path;
    for (char& c : lower) c = (char)tolower(c);

    if (lower.find(".sub") != std::string::npos) {
        currentState = MenuState::SubGhz_Saved;
        return true;
    } else if (lower.find(".nfc") != std::string::npos) {
        currentState = MenuState::NFC_Saved;
        return true;
    } else if (lower.find(".rfid") != std::string::npos) {
        currentState = MenuState::RFID_Saved;
        return true;
    } else if (lower.find(".ir") != std::string::npos) {
        currentState = MenuState::Infrared_Remote;
        return true;
    } else if (lower.find(".ibtn") != std::string::npos) {
        currentState = MenuState::iButton_Read;
        return true;
    } else if (lower.find(".txt") != std::string::npos || lower.find("duck") != std::string::npos) {
        currentState = MenuState::BadUSB_Run;
        return true;
    }
    return loadToolByName(path);
}

bool FlipperMachine::loadToolByName(const std::string& name) {
    std::string n = name;
    for (char& c : n) c = (char)tolower(c);

    if (n == "subghz" || n == "sub" || n == "rf") {
        currentState = MenuState::SubGhz_Analyzer;
        return true;
    } else if (n == "nfc") {
        currentState = MenuState::NFC_Read;
        return true;
    } else if (n == "rfid" || n == "125khz") {
        currentState = MenuState::RFID_Read;
        return true;
    } else if (n == "ir" || n == "infrared" || n == "remote") {
        currentState = MenuState::Infrared_Remote;
        return true;
    } else if (n == "ibutton" || n == "dallas") {
        currentState = MenuState::iButton_Read;
        return true;
    } else if (n == "badusb" || n == "ducky" || n == "hid") {
        currentState = MenuState::BadUSB_Run;
        return true;
    } else if (n == "gpio" || n == "power" || n == "battery") {
        currentState = MenuState::GPIO_Tools;
        return true;
    } else if (n == "snake") {
        currentState = MenuState::Game_Snake;
        initSnake();
        return true;
    } else if (n == "tetris") {
        currentState = MenuState::Game_Tetris;
        initTetris();
        return true;
    } else if (n == "doom" || n == "microdoom") {
        currentState = MenuState::Game_MicroDoom;
        initMicroDoom();
        return true;
    } else if (n == "music" || n == "chiptune" || n == "fmf") {
        currentState = MenuState::Music_Player;
        return true;
    } else if (n == "spectrum" || n == "waterfall") {
        currentState = MenuState::Spectrum_Waterfall;
        return true;
    }
    return false;
}

// ============================================================================
// Flipper OS State Machine Updates
// ============================================================================

void FlipperMachine::updateDesktop() {
    dolphinTimer++;
    if (dolphinTimer % 90 == 0) {
        dolphinMood = (dolphinMood + 1) % 4;
        static const char* SPEECHES[] = {
            "Ready to hack!",
            "Catching Sub-GHz!",
            "Reading some RFID!",
            "Feed me NFC keys!"
        };
        dolphinSpeech = SPEECHES[dolphinMood];
    }

    // Open Main Menu with OK or Right
    if (isTriggered(4) || isTriggered(3)) {
        currentState = MenuState::MainMenu;
        playClick();
        return;
    }

    clearLcd(false);

    // Status Bar (Top 10 pixels)
    // Battery icon
    drawRect(108, 1, 16, 7, true);
    fillRect(124, 3, 2, 3, true);
    fillRect(110, 3, 12, 3, true); // Filled battery
    drawString("98%", 88, 1);

    // SD Card icon
    drawRect(2, 1, 7, 7, true);
    setPixel(2, 1, false);

    // Bluetooth icon
    drawLine(14, 1, 14, 7, true);
    drawLine(14, 1, 17, 3, true);
    drawLine(17, 3, 12, 5, true);

    // Clock
    drawString("13:37", 48, 1);
    drawLine(0, 9, 127, 9, true);

    // Draw Dolphin Avatar
    int dx = 12, dy = 16;
    fillCircle(dx + 16, dy + 14, 10, true);
    fillCircle(dx + 26, dy + 16, 6, true);
    // Snout
    drawLine(dx + 28, dy + 18, dx + 34, dy + 20, true);
    // Eye
    fillCircle(dx + 23, dy + 13, 1, false);
    // Dorsal Fin
    drawLine(dx + 12, dy + 5, dx + 17, dy + 10, true);
    // Tail Fluke
    int tailWiggle = (dolphinTimer / 10) % 2;
    drawLine(dx + 4, dy + 16, dx, dy + 10 + tailWiggle * 2, true);
    drawLine(dx + 4, dy + 16, dx, dy + 22 - tailWiggle * 2, true);

    // Speech Bubble
    drawRect(48, 14, 76, 22, true);
    drawString(dolphinSpeech, 52, 18);
    // Bubble pointer
    drawLine(48, 22, 42, 24, true);
    drawLine(48, 25, 42, 24, true);

    // Bottom Level & XP bar
    drawLine(0, 50, 127, 50, true);
    drawString("Lvl.3", 4, 54);
    drawRect(36, 54, 88, 7, true);
    fillRect(38, 56, (int)(84.0f * (dolphinXP / 100.0f)), 3, true);
}

void FlipperMachine::updateMainMenu() {
    static const char* MENU_ITEMS[] = {
        "Sub-GHz",
        "125 kHz RFID",
        "NFC",
        "Infrared",
        "iButton",
        "Bad USB",
        "GPIO / Hardware",
        "Game: Snake",
        "Game: Tetris",
        "Game: Micro-Doom",
        "Music Player",
        "Spectrum Analyzer"
    };
    constexpr int ITEM_COUNT = 12;

    if (isTriggered(0)) { // Up
        if (mainMenuIndex > 0) mainMenuIndex--;
        playClick();
    }
    if (isTriggered(1)) { // Down
        if (mainMenuIndex < ITEM_COUNT - 1) mainMenuIndex++;
        playClick();
    }
    if (isTriggered(5) || isTriggered(2)) { // Back / Left
        currentState = MenuState::Desktop;
        playClick();
        return;
    }
    if (isTriggered(4) || isTriggered(3)) { // OK / Right
        playSuccessBeep();
        switch (mainMenuIndex) {
            case 0: currentState = MenuState::SubGhz_Analyzer; break;
            case 1: currentState = MenuState::RFID_Read; break;
            case 2: currentState = MenuState::NFC_Read; break;
            case 3: currentState = MenuState::Infrared_Remote; break;
            case 4: currentState = MenuState::iButton_Read; break;
            case 5: currentState = MenuState::BadUSB_Run; break;
            case 6: currentState = MenuState::GPIO_Tools; break;
            case 7: currentState = MenuState::Game_Snake; initSnake(); break;
            case 8: currentState = MenuState::Game_Tetris; initTetris(); break;
            case 9: currentState = MenuState::Game_MicroDoom; initMicroDoom(); break;
            case 10: currentState = MenuState::Music_Player; break;
            case 11: currentState = MenuState::Spectrum_Waterfall; break;
        }
        return;
    }

    clearLcd(false);

    // Menu Header
    fillRect(0, 0, 128, 10, true);
    drawString("MAIN MENU", 36, 1, true);

    // Visible window: 5 items
    int startIdx = std::max(0, mainMenuIndex - 2);
    if (startIdx + 5 > ITEM_COUNT) startIdx = std::max(0, ITEM_COUNT - 5);

    int curY = 12;
    for (int i = startIdx; i < startIdx + 5 && i < ITEM_COUNT; i++) {
        if (i == mainMenuIndex) {
            fillRect(2, curY - 1, 122, 10, true);
            drawString(MENU_ITEMS[i], 6, curY, true);
            // Selection indicator arrow
            drawString(">", 116, curY, true);
        } else {
            drawString(MENU_ITEMS[i], 6, curY, false);
        }
        curY += 10;
    }

    // Scrollbar track
    drawLine(126, 11, 126, 62, true);
    int thumbH = 10;
    int thumbY = 11 + (mainMenuIndex * (51 - thumbH)) / (ITEM_COUNT - 1);
    fillRect(125, thumbY, 3, thumbH, true);
}

void FlipperMachine::updateSubGhzAnalyzer() {
    if (isTriggered(5)) { // Back
        currentState = MenuState::MainMenu;
        playClick();
        return;
    }
    // Switch between 433.92 and 868.35 with Left/Right
    if (isTriggered(2)) { subGhzFreq = 433.92f; playClick(); }
    if (isTriggered(3)) { subGhzFreq = 868.35f; playClick(); }

    // Toggle Transmit simulation with OK
    if (isTriggered(4)) {
        subGhzTransmitting = !subGhzTransmitting;
        ledColor = subGhzTransmitting ? 0xFFFF0000 : 0xFF0035A0; // Red while TX
        playTone(subGhzTransmitting ? 1200.0f : 600.0f, 30);
    }

    // Fluctuating RSSI signal meter
    static int tick = 0;
    tick++;
    int noise = (int)(std::sin((float)tick * 0.3f) * 6.0f);
    subGhzRssi = -65 + noise + (subGhzTransmitting ? 35 : 0);

    clearLcd(false);
    // Header
    fillRect(0, 0, 128, 10, true);
    std::ostringstream fs;
    fs << "SUB-GHZ: " << subGhzFreq << "M";
    drawString(fs.str(), 8, 1, true);

    // RSSI Bar
    drawString("RSSI:", 4, 14);
    std::ostringstream ss;
    ss << subGhzRssi << " dBm";
    drawString(ss.str(), 36, 14);

    drawRect(4, 24, 120, 8, true);
    int barW = std::max(0, std::min(116, (subGhzRssi + 100) * 2));
    fillRect(6, 26, barW, 4, true);

    // Live Moving RF Signal Waterfall
    drawString("SPECTRUM WATERFALL:", 4, 36);
    drawRect(4, 46, 120, 16, true);
    for (int x = 6; x < 122; x++) {
        float peak = std::exp(-std::pow((float)(x - 64) * 0.15f, 2.0f)) * 12.0f;
        int h = (int)(peak + (std::rand() % 4));
        if (h > 14) h = 14;
        drawLine(x, 61, x, 61 - h, true);
    }
}

void FlipperMachine::updateSubGhzRead() {
    if (isTriggered(5)) { currentState = MenuState::MainMenu; playClick(); return; }

    subGhzSignalTimer++;
    if (subGhzSignalTimer > 50) {
        subGhzDetectedProtocol = 1; // Princeton
        ledColor = 0xFF00FF40; // Green LED
    }

    clearLcd(false);
    fillRect(0, 0, 128, 10, true);
    drawString("Sub-GHz Sniffer", 20, 1, true);

    if (subGhzDetectedProtocol == 0) {
        drawString("Listening 433.92M", 10, 24);
        int r = (subGhzSignalTimer % 16) + 4;
        drawCircle(64, 44, r, true);
    } else {
        drawString("PRINCETON 24-BIT", 14, 16);
        drawString("Key: 0x00A1B2C3", 16, 28);
        drawString("TE: 400 us  Rep: 3", 10, 40);
        drawString("[OK] Emulate Signal", 8, 52);
        if (isTriggered(4)) {
            playTone(880.0f, 30);
            subGhzTransmitting = true;
            ledColor = 0xFFFF0000;
        }
    }
}

void FlipperMachine::updateSubGhzSaved() {
    static const char* SAVED_REMOTES[] = {"car_keyfob.sub", "gate_came.sub", "weather_station.sub"};
    constexpr int REMOTE_COUNT = 3;

    if (isTriggered(0) && subMenuIndex > 0) { subMenuIndex--; playClick(); }
    if (isTriggered(1) && subMenuIndex < REMOTE_COUNT - 1) { subMenuIndex++; playClick(); }
    if (isTriggered(5)) { currentState = MenuState::MainMenu; playClick(); return; }

    if (isTriggered(4)) { // Transmit
        subGhzTransmitting = true;
        ledColor = 0xFFFF0000; // Red TX
        playTone(1400.0f, 40);
    }

    clearLcd(false);
    fillRect(0, 0, 128, 10, true);
    drawString("SAVED SUB-GHZ", 24, 1, true);

    int curY = 16;
    for (int i = 0; i < REMOTE_COUNT; i++) {
        if (i == subMenuIndex) {
            fillRect(4, curY - 1, 120, 10, true);
            drawString(SAVED_REMOTES[i], 8, curY, true);
        } else {
            drawString(SAVED_REMOTES[i], 8, curY, false);
        }
        curY += 12;
    }
    drawString("[OK] Transmit [Back]", 4, 54);
}

void FlipperMachine::updateRfidSaved() {
    if (isTriggered(5)) { currentState = MenuState::MainMenu; playClick(); return; }
    if (isTriggered(4)) { playSuccessBeep(); ledColor = 0xFF00FF40; }

    clearLcd(false);
    fillRect(0, 0, 128, 10, true);
    drawString("SAVED RFID KEYS", 20, 1, true);

    drawString("> apartment_fob.rfid", 4, 18);
    drawString("  office_badge.rfid", 4, 30);
    drawString("[OK] Emulate 125kHz", 8, 50);
}

void FlipperMachine::updateNfcSaved() {
    if (isTriggered(5)) { currentState = MenuState::MainMenu; playClick(); return; }
    if (isTriggered(4)) { playSuccessBeep(); ledColor = 0xFF00FF40; }

    clearLcd(false);
    fillRect(0, 0, 128, 10, true);
    drawString("SAVED NFC TAGS", 22, 1, true);

    drawString("> mifare_1k.nfc", 4, 18);
    drawString("  zelda_link.nfc", 4, 30);
    drawString("  bank_card.nfc", 4, 42);
    drawString("[OK] Emulate NFC", 16, 54);
}

void FlipperMachine::updateRfidRead() {
    if (isTriggered(5)) { currentState = MenuState::MainMenu; playClick(); return; }

    rfidScanProgress++;
    if (rfidScanProgress > 60) {
        rfidCardDetected = true;
        ledColor = 0xFF00FF40; // Green LED on read!
    }

    clearLcd(false);
    fillRect(0, 0, 128, 10, true);
    drawString("125 kHz RFID", 28, 1, true);

    if (!rfidCardDetected) {
        drawString("Reading RFID...", 20, 24);
        // Animated antenna coil
        int r = (rfidScanProgress % 16) + 4;
        drawCircle(64, 44, r, true);
    } else {
        drawString("EM4100 FOUND!", 26, 16);
        drawString("UID: 01 02 03 04 05", 8, 28);
        drawString("Type: EM-Marin 64-bit", 4, 40);
        drawString("[OK] Emulate  [<] Back", 4, 54);

        if (isTriggered(4)) {
            playSuccessBeep();
            dolphinXP = std::min(100, dolphinXP + 5);
        }
    }
}

void FlipperMachine::updateNfcRead() {
    if (isTriggered(5)) { currentState = MenuState::MainMenu; playClick(); return; }

    nfcScanProgress++;
    if (nfcScanProgress > 60) {
        nfcCardDetected = true;
        ledColor = 0xFF00FF40; // Green on read
    }

    clearLcd(false);
    fillRect(0, 0, 128, 10, true);
    drawString("NFC 13.56 MHz", 26, 1, true);

    if (!nfcCardDetected) {
        drawString("Hold card to back...", 8, 24);
        int r = (nfcScanProgress % 18) + 4;
        drawCircle(64, 44, r, true);
    } else {
        drawString("MIFARE CLASSIC 1K", 14, 14);
        drawString("UID: 04:A2:8B:C1:23:45", 4, 26);
        drawString("ATQA: 0004  SAK: 08", 4, 38);
        drawString("16/16 Sectors Read!", 8, 50);
    }
}

void FlipperMachine::updateInfrared() {
    static const char* IR_CMDS[] = {"POWER", "VOL+", "VOL-", "MUTE", "CH+", "CH-", "INPUT"};
    constexpr int IR_COUNT = 7;

    if (isTriggered(0) && irCommandIndex > 0) { irCommandIndex--; playClick(); }
    if (isTriggered(1) && irCommandIndex < IR_COUNT - 1) { irCommandIndex++; playClick(); }
    if (isTriggered(5)) { currentState = MenuState::MainMenu; playClick(); return; }

    if (isTriggered(4)) { // Send command
        irTransmitTimer = 20;
        ledColor = 0xFFFF0000; // Red flashing LED
        playTone(38000.0f, 10, 0.2f); // 38kHz modulation tick
    }

    if (irTransmitTimer > 0) irTransmitTimer--;

    clearLcd(false);
    fillRect(0, 0, 128, 10, true);
    drawString("INFRARED REMOTE", 18, 1, true);

    drawString("TV: SAMSUNG SMART", 8, 14);
    drawRect(16, 26, 96, 22, true);
    fillRect(18, 28, 92, 18, true);
    drawString(IR_CMDS[irCommandIndex], 46, 33, true);

    if (irTransmitTimer > 0) {
        drawString(">> SENDING IR PULSE >>", 4, 52);
    } else {
        drawString("Press [OK] to Send", 14, 52);
    }
}

void FlipperMachine::updateIButton() {
    if (isTriggered(5)) { currentState = MenuState::MainMenu; playClick(); return; }

    ibuttonScanProgress++;
    if (ibuttonScanProgress > 45) {
        ibuttonDetected = true;
        ledColor = 0xFF00FF40;
    }

    clearLcd(false);
    fillRect(0, 0, 128, 10, true);
    drawString("iButton (1-Wire)", 16, 1, true);

    if (!ibuttonDetected) {
        drawString("Touch key to probe...", 4, 24);
        drawCircle(64, 44, 8, true);
        drawCircle(64, 44, 14, true);
    } else {
        drawString("DALLAS DS1990A", 22, 16);
        drawString("ID: 01:23:45:67:89:AB", 4, 28);
        drawString("CRC: 8-bit Valid", 16, 40);
        drawString("[OK] Emulate Key", 16, 52);
    }
}

void FlipperMachine::updateBadUsb() {
    if (isTriggered(5)) { currentState = MenuState::MainMenu; playClick(); return; }

    if (badUsbProgress < badUsbTotal) {
        badUsbProgress++;
        playTone(1400.0f, 5, 0.1f);
    }

    clearLcd(false);
    fillRect(0, 0, 128, 10, true);
    drawString("BadUSB / HID", 28, 1, true);

    drawString("Payload: Rickroll.txt", 4, 16);
    drawRect(8, 28, 112, 10, true);
    fillRect(10, 30, (int)(108.0f * ((float)badUsbProgress / (float)badUsbTotal)), 6, true);

    if (badUsbProgress >= badUsbTotal) {
        drawString("STATUS: COMPLETED!", 8, 44);
        drawString("Payload executed OK", 6, 54);
    } else {
        drawString("Injecting Keystrokes...", 4, 44);
    }
}

void FlipperMachine::updateGpioTools() {
    if (isTriggered(5)) { currentState = MenuState::MainMenu; playClick(); return; }

    clearLcd(false);
    fillRect(0, 0, 128, 10, true);
    drawString("GPIO & HARDWARE", 20, 1, true);

    drawString("Freq Gen: 1.000 kHz", 4, 16);
    drawString("5V Output: ENABLED", 4, 26);
    drawString("Battery: 4.18V (98%)", 4, 38);
    drawString("Temperature: 28.4 C", 4, 48);
}

// ============================================================================
// Games (Snake, Tetris, Micro-Doom, Music Player)
// ============================================================================

void FlipperMachine::initSnake() {
    snake.body.clear();
    snake.body.push_back({20, 15});
    snake.body.push_back({19, 15});
    snake.body.push_back({18, 15});
    snake.dirX = 1;
    snake.dirY = 0;
    snake.food = {40, 20};
    snake.score = 0;
    snake.gameOver = false;
    snake.tickCounter = 0;
}

void FlipperMachine::updateGameSnake() {
    if (isTriggered(5)) { currentState = MenuState::MainMenu; playClick(); return; }

    if (!snake.gameOver) {
        if (isTriggered(0) && snake.dirY == 0) { snake.dirX = 0; snake.dirY = -1; }
        if (isTriggered(1) && snake.dirY == 0) { snake.dirX = 0; snake.dirY = 1; }
        if (isTriggered(2) && snake.dirX == 0) { snake.dirX = -1; snake.dirY = 0; }
        if (isTriggered(3) && snake.dirX == 0) { snake.dirX = 1; snake.dirY = 0; }

        snake.tickCounter++;
        if (snake.tickCounter >= 4) {
            snake.tickCounter = 0;
            auto head = snake.body.front();
            head.x += snake.dirX * 2;
            head.y += snake.dirY * 2;

            // Wall collisions
            if (head.x < 2 || head.x >= 126 || head.y < 12 || head.y >= 62) {
                snake.gameOver = true;
                playErrorBuzz();
            }

            // Food collision
            if (std::abs(head.x - snake.food.x) < 3 && std::abs(head.y - snake.food.y) < 3) {
                snake.score += 10;
                snake.food.x = 8 + (std::rand() % 110);
                snake.food.y = 16 + (std::rand() % 42);
                playTone(1800.0f, 25);
            } else {
                snake.body.pop_back();
            }
            snake.body.insert(snake.body.begin(), head);
        }
    } else {
        if (isTriggered(4)) initSnake();
    }

    clearLcd(false);
    fillRect(0, 0, 128, 10, true);
    drawString("SNAKE", 4, 1, true);
    drawString("SCORE: " + std::to_string(snake.score), 64, 1, true);

    drawRect(0, 10, 128, 54, true);

    // Draw food
    fillRect(snake.food.x - 1, snake.food.y - 1, 3, 3, true);

    // Draw snake body
    for (const auto& pt : snake.body) {
        fillRect(pt.x - 1, pt.y - 1, 3, 3, true);
    }

    if (snake.gameOver) {
        fillRect(24, 26, 80, 18, false);
        drawRect(24, 26, 80, 18, true);
        drawString("GAME OVER", 36, 29);
        drawString("[OK] Restart", 30, 36);
    }
}

void FlipperMachine::initTetris() {
    std::memset(tetris.board, 0, sizeof(tetris.board));
    tetris.pieceX = 4;
    tetris.pieceY = 0;
    tetris.pieceType = std::rand() % 7;
    tetris.rotation = 0;
    tetris.score = 0;
    tetris.lines = 0;
    tetris.gameOver = false;
    tetris.dropTimer = 0;
}

void FlipperMachine::updateGameTetris() {
    if (isTriggered(5)) { currentState = MenuState::MainMenu; playClick(); return; }

    if (!tetris.gameOver) {
        if (isTriggered(2) && tetris.pieceX > 0) tetris.pieceX--;
        if (isTriggered(3) && tetris.pieceX < 8) tetris.pieceX++;
        if (isTriggered(0) || isTriggered(4)) { tetris.rotation = (tetris.rotation + 1) % 4; playClick(); }

        tetris.dropTimer++;
        if (tetris.dropTimer >= 15 || isPressed(1)) {
            tetris.dropTimer = 0;
            tetris.pieceY++;
            if (tetris.pieceY >= 18) {
                tetris.pieceY = 0;
                tetris.pieceX = 4;
                tetris.score += 40;
                playTone(800.0f, 15);
            }
        }
    }

    clearLcd(false);
    // Draw 10x20 Tetris Matrix
    int bx = 44, by = 2;
    drawRect(bx - 1, by - 1, 42, 62, true);

    // Draw falling piece
    fillRect(bx + tetris.pieceX * 4, by + tetris.pieceY * 3, 6, 6, true);

    // HUD
    drawString("TETRIS", 4, 10);
    drawString("SCORE:", 4, 26);
    drawString(std::to_string(tetris.score), 4, 36);
    drawString("LINES:", 92, 26);
    drawString(std::to_string(tetris.lines), 92, 36);
}

void FlipperMachine::initMicroDoom() {
    doom.posX = 3.5f;
    doom.posY = 3.5f;
    doom.dirX = -1.0f;
    doom.dirY = 0.0f;
    doom.planeX = 0.0f;
    doom.planeY = 0.66f;
    doom.shootTimer = 0;
    doom.monsterX = 7;
    doom.monsterY = 7;
    doom.monsterAlive = true;
    doom.monsterHp = 3;
    doom.kills = 0;
}

void FlipperMachine::updateGameMicroDoom() {
    if (isTriggered(5)) { currentState = MenuState::MainMenu; playClick(); return; }

    // Raycasting Maze Map (8x8)
    static const int MAP[8][8] = {
        {1,1,1,1,1,1,1,1},
        {1,0,0,0,0,0,0,1},
        {1,0,1,0,0,1,0,1},
        {1,0,1,0,0,1,0,1},
        {1,0,0,0,0,0,0,1},
        {1,0,1,1,1,1,0,1},
        {1,0,0,0,0,0,0,1},
        {1,1,1,1,1,1,1,1}
    };

    // Movement & Rotation
    float moveSpeed = 0.08f;
    float rotSpeed = 0.06f;

    if (isPressed(0)) { // Forward
        if (MAP[(int)(doom.posX + doom.dirX * moveSpeed)][(int)doom.posY] == 0) doom.posX += doom.dirX * moveSpeed;
        if (MAP[(int)doom.posX][(int)(doom.posY + doom.dirY * moveSpeed)] == 0) doom.posY += doom.dirY * moveSpeed;
    }
    if (isPressed(1)) { // Backward
        if (MAP[(int)(doom.posX - doom.dirX * moveSpeed)][(int)doom.posY] == 0) doom.posX -= doom.dirX * moveSpeed;
        if (MAP[(int)doom.posX][(int)(doom.posY - doom.dirY * moveSpeed)] == 0) doom.posY -= doom.dirY * moveSpeed;
    }
    if (isPressed(2)) { // Rotate Left
        float oldDirX = doom.dirX;
        doom.dirX = doom.dirX * std::cos(rotSpeed) - doom.dirY * std::sin(rotSpeed);
        doom.dirY = oldDirX * std::sin(rotSpeed) + doom.dirY * std::cos(rotSpeed);
        float oldPlaneX = doom.planeX;
        doom.planeX = doom.planeX * std::cos(rotSpeed) - doom.planeY * std::sin(rotSpeed);
        doom.planeY = oldPlaneX * std::sin(rotSpeed) + doom.planeY * std::cos(rotSpeed);
    }
    if (isPressed(3)) { // Rotate Right
        float oldDirX = doom.dirX;
        doom.dirX = doom.dirX * std::cos(-rotSpeed) - doom.dirY * std::sin(-rotSpeed);
        doom.dirY = oldDirX * std::sin(-rotSpeed) + doom.dirY * std::cos(-rotSpeed);
        float oldPlaneX = doom.planeX;
        doom.planeX = doom.planeX * std::cos(-rotSpeed) - doom.planeY * std::sin(-rotSpeed);
        doom.planeY = oldPlaneX * std::sin(-rotSpeed) + doom.planeY * std::cos(-rotSpeed);
    }

    // Shoot (OK / Space)
    if (isTriggered(4) && doom.shootTimer == 0) {
        doom.shootTimer = 10;
        playTone(220.0f, 40, 0.9f); // Boom sound
        // Check if monster in line of sight
        float mdx = (float)doom.monsterX - doom.posX;
        float mdy = (float)doom.monsterY - doom.posY;
        float dot = mdx * doom.dirX + mdy * doom.dirY;
        if (dot > 0.0f && doom.monsterAlive) {
            doom.monsterHp--;
            if (doom.monsterHp <= 0) {
                doom.monsterAlive = false;
                doom.kills++;
                playTone(120.0f, 60, 0.9f);
            }
        }
    }
    if (doom.shootTimer > 0) doom.shootTimer--;

    clearLcd(false);

    // Raycast 128 vertical columns
    for (int x = 0; x < LCD_W; x++) {
        float cameraX = 2.0f * (float)x / (float)LCD_W - 1.0f;
        float rayDirX = doom.dirX + doom.planeX * cameraX;
        float rayDirY = doom.dirY + doom.planeY * cameraX;

        int mapX = (int)doom.posX;
        int mapY = (int)doom.posY;
        float sideDistX, sideDistY;
        float deltaDistX = (rayDirX == 0) ? 1e30 : std::abs(1.0f / rayDirX);
        float deltaDistY = (rayDirY == 0) ? 1e30 : std::abs(1.0f / rayDirY);
        float perpWallDist;
        int stepX, stepY;
        int hit = 0, side = 0;

        if (rayDirX < 0) { stepX = -1; sideDistX = (doom.posX - mapX) * deltaDistX; }
        else { stepX = 1; sideDistX = (mapX + 1.0f - doom.posX) * deltaDistX; }

        if (rayDirY < 0) { stepY = -1; sideDistY = (doom.posY - mapY) * deltaDistY; }
        else { stepY = 1; sideDistY = (mapY + 1.0f - doom.posY) * deltaDistY; }

        while (hit == 0) {
            if (sideDistX < sideDistY) { sideDistX += deltaDistX; mapX += stepX; side = 0; }
            else { sideDistY += deltaDistY; mapY += stepY; side = 1; }
            if (MAP[mapX][mapY] > 0) hit = 1;
        }

        if (side == 0) perpWallDist = (sideDistX - deltaDistX);
        else perpWallDist = (sideDistY - deltaDistY);

        int lineHeight = (int)(LCD_H / perpWallDist);
        int drawStart = -lineHeight / 2 + LCD_H / 2;
        if (drawStart < 0) drawStart = 0;
        int drawEnd = lineHeight / 2 + LCD_H / 2;
        if (drawEnd >= LCD_H) drawEnd = LCD_H - 1;

        // Draw wall column with dither pattern
        for (int y = drawStart; y <= drawEnd; y++) {
            if (side == 1) {
                // Shaded side
                if ((x + y) % 2 == 0) setPixel(x, y, true);
            } else {
                setPixel(x, y, true);
            }
        }
    }

    // Shotgun in foreground
    int gunY = (doom.shootTimer > 0) ? 44 : 48;
    fillRect(60, gunY, 8, 16, true);
    if (doom.shootTimer > 6) {
        // Muzzle flash
        fillCircle(64, gunY - 4, 6, true);
    }

    // HUD
    drawString("DOOM", 4, 2);
    drawString("KILLS:" + std::to_string(doom.kills), 80, 2);
}

void FlipperMachine::updateMusicPlayer() {
    static const char* TRACK_NAMES[] = {"Mario Theme", "Tetris Theme", "Star Wars", "Doom Theme"};
    constexpr int TRACK_COUNT = 4;

    if (isTriggered(0) && musicTrackIndex > 0) { musicTrackIndex--; playClick(); }
    if (isTriggered(1) && musicTrackIndex < TRACK_COUNT - 1) { musicTrackIndex++; playClick(); }
    if (isTriggered(5)) { currentState = MenuState::MainMenu; playClick(); return; }

    // Toggle Play/Stop with OK
    if (isTriggered(4)) {
        musicPlaying = !musicPlaying;
        musicNoteIndex = 0;
        playTone(musicPlaying ? 1000.0f : 500.0f, 20);
    }

    if (musicPlaying) {
        musicTick++;
        if (musicTick % 8 == 0) {
            static const float MELODY[] = {659.25f, 659.25f, 0, 659.25f, 0, 523.25f, 659.25f, 783.99f, 0, 392.00f};
            float f = MELODY[musicNoteIndex % 10];
            if (f > 0.0f) playTone(f, 60, 0.7f);
            musicNoteIndex++;
        }
    }

    clearLcd(false);
    fillRect(0, 0, 128, 10, true);
    drawString("MUSIC PLAYER", 28, 1, true);

    drawString("TRACK: " + std::string(TRACK_NAMES[musicTrackIndex]), 4, 18);
    drawString(musicPlaying ? "STATUS: PLAYING >>" : "STATUS: PAUSED ||", 4, 30);
    drawString("[OK] Play/Pause", 16, 46);

    // Animated music notes
    if (musicPlaying) {
        int noteX = 100 + (musicTick % 20);
        drawString("♫", noteX, 26);
    }
}

void FlipperMachine::updateSpectrumWaterfall() {
    if (isTriggered(5)) { currentState = MenuState::MainMenu; playClick(); return; }

    clearLcd(false);
    fillRect(0, 0, 128, 10, true);
    drawString("RF SPECTRUM ANALYZER", 8, 1, true);

    // Spectrum Graph
    for (int x = 0; x < LCD_W; x++) {
        float freqDiff = std::abs((float)x - 64.0f);
        int h = (int)(std::exp(-freqDiff * 0.1f) * 40.0f + (std::rand() % 4));
        if (h > 50) h = 50;
        drawLine(x, 62, x, 62 - h, true);
    }
}

// ============================================================================
// Main Frame Execution & Hardware Casing Render
// ============================================================================

void FlipperMachine::runFrame() {
    // Dispatch active Flipper OS state
    switch (currentState) {
        case MenuState::Desktop: updateDesktop(); break;
        case MenuState::MainMenu: updateMainMenu(); break;
        case MenuState::SubGhz_Analyzer: updateSubGhzAnalyzer(); break;
        case MenuState::SubGhz_Read: updateSubGhzRead(); break;
        case MenuState::SubGhz_Saved: updateSubGhzAnalyzer(); break;
        case MenuState::RFID_Read: updateRfidRead(); break;
        case MenuState::RFID_Saved: updateRfidRead(); break;
        case MenuState::NFC_Read: updateNfcRead(); break;
        case MenuState::NFC_Saved: updateNfcRead(); break;
        case MenuState::Infrared_Remote: updateInfrared(); break;
        case MenuState::Infrared_Learn: updateInfrared(); break;
        case MenuState::iButton_Read: updateIButton(); break;
        case MenuState::BadUSB_Run: updateBadUsb(); break;
        case MenuState::GPIO_Tools: updateGpioTools(); break;
        case MenuState::Game_Snake: updateGameSnake(); break;
        case MenuState::Game_Tetris: updateGameTetris(); break;
        case MenuState::Game_MicroDoom: updateGameMicroDoom(); break;
        case MenuState::Music_Player: updateMusicPlayer(); break;
        case MenuState::Spectrum_Waterfall: updateSpectrumWaterfall(); break;
    }

    // Save previous button state
    lastButtonState = buttonState;

    // Render 640x512 Casing and Blit 128x64 Orange LCD
    renderCasing();
    renderLcdToCanvas();

    // Audio samples
    updateAudio();
}

void FlipperMachine::renderCasing() {
    // 640x512 Polycarbonate White Chassis (#F0F2F6)
    screenBuffer.fill(0xFF16171A); // Dark studio table background

    // Flipper Zero Casing Boundary: x: 40..600, y: 30..480
    int cX0 = 40, cY0 = 30, cW = 560, cH = 450;
    for (int y = cY0; y < cY0 + cH; y++) {
        for (int x = cX0; x < cX0 + cW; x++) {
            // Rounded corners
            int dx = std::min(x - cX0, cX0 + cW - 1 - x);
            int dy = std::min(y - cY0, cY0 + cH - 1 - y);
            if (dx < 20 && dy < 20 && (dx - 20) * (dx - 20) + (dy - 20) * (dy - 20) > 400) {
                continue;
            }
            screenBuffer[y * CANVAS_W + x] = 0xFFF0F2F6; // Matte White
        }
    }

    // Top Chamfer Accent line
    for (int x = cX0 + 20; x < cX0 + cW - 20; x++) {
        screenBuffer[(cY0 + 8) * CANVAS_W + x] = 0xFFD8DCE4;
    }

    // iButton Contact Ring at (568, 80)
    int ibX = 568, ibY = 80;
    for (int dy = -16; dy <= 16; dy++) {
        for (int dx = -16; dx <= 16; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 <= 16 * 16 && d2 >= 10 * 10) {
                screenBuffer[(ibY + dy) * CANVAS_W + (ibX + dx)] = 0xFFB0B6C2; // Silver outer ring
            } else if (d2 <= 6 * 6) {
                screenBuffer[(ibY + dy) * CANVAS_W + (ibX + dx)] = 0xFFD0D5DE; // Silver center contact
            }
        }
    }

    // Recessed Black Acrylic Screen Bezel: x: 70..536, y: 56..296
    for (int y = 56; y <= 296; y++) {
        for (int x = 70; x <= 536; x++) {
            screenBuffer[y * CANVAS_W + x] = 0xFF0E0F12; // Piano Black acrylic
        }
    }

    // Silk-screened "FLIPPER" logo in silver on bezel
    // (x: 80, y: 278)

    // RGB Notification LED at (58, 176)
    int ledX = 58, ledY = 176;
    for (int dy = -6; dy <= 6; dy++) {
        for (int dx = -6; dx <= 6; dx++) {
            if (dx * dx + dy * dy <= 36) {
                screenBuffer[(ledY + dy) * CANVAS_W + (ledX + dx)] = ledColor;
            }
        }
    }

    // ==========================================
    // D-PAD & BACK BUTTON (Orange Silicone #FF8200)
    // ==========================================
    int dpadCx = 568, dpadCy = 176;
    for (int dy = -46; dy <= 46; dy++) {
        for (int dx = -46; dx <= 46; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 <= 46 * 46 && d2 >= 18 * 18) {
                screenBuffer[(dpadCy + dy) * CANVAS_W + (dpadCx + dx)] = 0xFFFF8200; // D-pad outer ring
            } else if (d2 <= 14 * 14) {
                screenBuffer[(dpadCy + dy) * CANVAS_W + (dpadCx + dx)] = 0xFFFFA030; // OK center circle
            }
        }
    }

    // Directional Arrow Icons on D-Pad
    // Up
    for (int dy = -36; dy <= -28; dy++) screenBuffer[(dpadCy + dy) * CANVAS_W + dpadCx] = 0xFFFFFFFF;
    // Down
    for (int dy = 28; dy <= 36; dy++) screenBuffer[(dpadCy + dy) * CANVAS_W + dpadCx] = 0xFFFFFFFF;
    // Left
    for (int dx = -36; dx <= -28; dx++) screenBuffer[dpadCy * CANVAS_W + (dpadCx + dx)] = 0xFFFFFFFF;
    // Right
    for (int dx = 28; dx <= 36; dx++) screenBuffer[dpadCy * CANVAS_W + (dpadCx + dx)] = 0xFFFFFFFF;

    // Back Button (Oval Orange #FF8200) at (548..588, 244..268)
    for (int y = 244; y <= 268; y++) {
        for (int x = 548; x <= 588; x++) {
            screenBuffer[y * CANVAS_W + x] = 0xFFFF8200;
        }
    }
}

void FlipperMachine::renderLcdToCanvas() {
    // Authentic Flipper Zero ST7567 Orange Backlit LCD Colors
    // Backlight: Warm saturated Orange #FF8A00
    // Pixels (Active ON): Dark Indigo / Black #1A1008
    uint32 colorBacklight = 0xFFFF8A00;
    uint32 colorPixel = 0xFF1A1008;

    // Scale 128x64 display 3.5x into active area:
    // x: 80..527 (448 px wide), y: 64..287 (224 px high)
    for (int py = 0; py < LCD_H; py++) {
        int dstY0 = SCREEN_Y + (int)(py * 3.5f);
        int nextDstY = SCREEN_Y + (int)((py + 1) * 3.5f);

        for (int px = 0; px < LCD_W; px++) {
            bool on = getPixel(px, py);
            uint32 argb = on ? colorPixel : colorBacklight;

            int dstX0 = SCREEN_X + (int)(px * 3.5f);
            int nextDstX = SCREEN_X + (int)((px + 1) * 3.5f);

            for (int dy = dstY0; dy < nextDstY; dy++) {
                for (int dx = dstX0; dx < nextDstX; dx++) {
                    screenBuffer[dy * CANVAS_W + dx] = argb;
                }
            }
        }
    }
}
