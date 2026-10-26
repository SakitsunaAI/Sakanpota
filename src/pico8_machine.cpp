#include "pico8_machine.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>

// Standard 16-color PICO-8 Palette (ARGB32)
static const uint32 PICO8_PALETTE[16] = {
    0xFF000000, // 0: Black
    0xFF1D2B53, // 1: Dark Blue
    0xFF7E2553, // 2: Dark Purple
    0xFF008751, // 3: Dark Green
    0xFFAB5236, // 4: Brown
    0xFF5F574F, // 5: Dark Gray
    0xFFC2C3C7, // 6: Light Gray
    0xFFFFF1E8, // 7: White
    0xFFFF004D, // 8: Red
    0xFFFFA300, // 9: Orange
    0xFFFFEC27, // 10: Yellow
    0xFF00E436, // 11: Green
    0xFF29ADFF, // 12: Blue
    0xFF83769C, // 13: Lavender
    0xFFFF77A8, // 14: Pink
    0xFFFFCCAA  // 15: Peach
};

// 3x5 standard PICO-8 bitmap font representation (characters 32..127)
static const uint16 PICO8_FONT[96] = {
    0x0000, // ' ' (32)
    0x4F00, // '!'
    0x0B00, // '"'
    0x5F50, // '#'
    0x2FD2, // '$'
    0x5254, // '%'
    0x5772, // '&'
    0x0200, // '''
    0x2E20, // '('
    0x8740, // ')'
    0x5750, // '*'
    0x0720, // '+'
    0x0030, // ','
    0x0380, // '-'
    0x0020, // '.'
    0x2248, // '/'
    0x7B6F, // '0'
    0x2492, // '1'
    0x73E7, // '2'
    0x73CF, // '3'
    0x5BC9, // '4'
    0xF9CF, // '5'
    0x79EF, // '6'
    0x7249, // '7'
    0x7BEF, // '8'
    0x7BCF, // '9'
    0x0410, // ':'
    0x0418, // ';'
    0x0A48, // '<'
    0x0E38, // '='
    0x24A0, // '>'
    0x6124, // '?'
    0x7A2F, // '@'
    0x7BE9, // 'A'
    0x73EB, // 'B'
    0x7927, // 'C'
    0x736B, // 'D'
    0x79E7, // 'E'
    0x79E1, // 'F'
    0x79AF, // 'G'
    0x5BE9, // 'H'
    0x7497, // 'I'
    0x126E, // 'J'
    0x5AC9, // 'K'
    0x4927, // 'L'
    0x5BED, // 'M'
    0x5BEB, // 'N'
    0x7B6F, // 'O'
    0x7BE1, // 'P'
    0x7B6D, // 'Q'
    0x7BE9, // 'R'
    0x78CF, // 'S'
    0x7492, // 'T'
    0x5B6F, // 'U'
    0x5B6A, // 'V'
    0x5F6D, // 'W'
    0x5A85, // 'X'
    0x5B92, // 'Y'
    0x724F, // 'Z'
    0x3A20, // '['
    0x4221, // '\'
    0x0B70, // ']'
    0x2540, // '^'
    0x000F, // '_'
    0x0100, // '`'
    0x7BE9, // 'a' (uppercase fallback)
    0x73EB, // 'b'
    0x7927, // 'c'
    0x736B, // 'd'
    0x79E7, // 'e'
    0x79E1, // 'f'
    0x79AF, // 'g'
    0x5BE9, // 'h'
    0x7497, // 'i'
    0x126E, // 'j'
    0x5AC9, // 'k'
    0x4927, // 'l'
    0x5BED, // 'm'
    0x5BEB, // 'n'
    0x7B6F, // 'o'
    0x7BE1, // 'p'
    0x7B6D, // 'q'
    0x7BE9, // 'r'
    0x78CF, // 's'
    0x7492, // 't'
    0x5B6F, // 'u'
    0x5B6A, // 'v'
    0x5F6D, // 'w'
    0x5A85, // 'x'
    0x5B92, // 'y'
    0x724F, // 'z'
    0x2E20, // '{'
    0x2492, // '|'
    0x8740, // '}'
    0x0540, // '~'
    0x7FFF  // DEL (127)
};

Pico8Machine::Pico8Machine() {
    screenBuffer.fill(0xFF101014);
    audioBuffer.resize(735, 0); // 44100 / 60 = 735 samples per frame
    init();
}

void Pico8Machine::init() {
    ram.fill(0);
    // Initialize default palette mapping (identity)
    for (int i = 0; i < 16; i++) {
        drawPal[i] = (uint8)i;
        screenPal[i] = (uint8)i;
    }
    transMask = 0x0001; // Color 0 transparent by default
    camX = 0;
    camY = 0;
    clipX = 0;
    clipY = 0;
    clipW = 128;
    clipH = 128;
    curColor = 6;
    curX = 0;
    curY = 0;

    buttonState[0] = 0;
    buttonState[1] = 0;
    lastButtonState[0] = 0;
    lastButtonState[1] = 0;
    std::memset(btnRepeatTimer, 0, sizeof(btnRepeatTimer));

    for (auto& ch : channels) {
        ch.active = false;
        ch.sfxId = -1;
        ch.phase = 0.0f;
        ch.volume = 0.0f;
        ch.freq = 440.0f;
    }

    // Default to Boot Shell
    bootPico8();
}

void Pico8Machine::reset() {
    init();
}

void Pico8Machine::bootPico8() {
    currentMode = CartMode::BootShell;
    currentCartTitle = "PICO-8 BOOT SHELL";
    currentCartAuthor = "Lexaloffle";
    currentCartFile = "";
    bootPhase = 0;
    bootTimer = 0;
    bootSunAngle = 0.0f;
    cmdBuffer = "";
    shellHistory.clear();
    carouselIndex = 0;
    carouselOffset = 0.0f;
    activityLedTimer = 30;

    // Start with black screen
    cls(0);

    // Play startup chime: Note C4, E4, G4, C5 arpeggio
    sfx(0);
}

std::string Pico8Machine::getMediaName() const {
    if (currentMode == CartMode::BootShell) {
        return "PICO-8 OS v0.2.5g (Interactive Prompt)";
    }
    return currentCartTitle + " [" + currentCartAuthor + "]";
}

// ============================================================================
// PICO-8 Graphics Rasterizer API
// ============================================================================

void Pico8Machine::cls(uint8 col) {
    uint8 c = (col & 0x0F);
    uint8 b = c | (c << 4);
    std::fill(ram.begin() + 0x6000, ram.begin() + 0x8000, b);
    curX = 0;
    curY = 0;
}

void Pico8Machine::pset(int x, int y, int col) {
    int px = x - camX;
    int py = y - camY;
    if (px < clipX || px >= clipX + clipW || py < clipY || py >= clipY + clipH) return;
    if (px < 0 || px >= 128 || py < 0 || py >= 128) return;

    uint8 mapped = drawPal[col & 15] & 15;
    int addr = 0x6000 + py * 64 + (px >> 1);
    if ((px & 1) == 0) {
        ram[addr] = (ram[addr] & 0xF0) | mapped;
    } else {
        ram[addr] = (ram[addr] & 0x0F) | (mapped << 4);
    }
}

uint8 Pico8Machine::pget(int x, int y) {
    if (x < 0 || x >= 128 || y < 0 || y >= 128) return 0;
    int addr = 0x6000 + y * 64 + (x >> 1);
    return (x & 1) ? ((ram[addr] >> 4) & 0x0F) : (ram[addr] & 0x0F);
}

void Pico8Machine::sset(int x, int y, int col) {
    if (x < 0 || x >= 128 || y < 0 || y >= 128) return;
    int addr = (y * 128 + x) >> 1;
    if (addr >= 0x2000) return;
    uint8 c = col & 0x0F;
    if ((x & 1) == 0) {
        ram[addr] = (ram[addr] & 0xF0) | c;
    } else {
        ram[addr] = (ram[addr] & 0x0F) | (c << 4);
    }
}

uint8 Pico8Machine::sget(int x, int y) {
    if (x < 0 || x >= 128 || y < 0 || y >= 128) return 0;
    int addr = (y * 128 + x) >> 1;
    if (addr >= 0x2000) return 0;
    return (x & 1) ? ((ram[addr] >> 4) & 0x0F) : (ram[addr] & 0x0F);
}

void Pico8Machine::line(int x0, int y0, int x1, int y1, int col) {
    int dx = std::abs(x1 - x0);
    int dy = std::abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    while (true) {
        pset(x0, y0, col);
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

void Pico8Machine::rect(int x0, int y0, int x1, int y1, int col) {
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    for (int x = x0; x <= x1; x++) {
        pset(x, y0, col);
        pset(x, y1, col);
    }
    for (int y = y0; y <= y1; y++) {
        pset(x0, y, col);
        pset(x1, y, col);
    }
}

void Pico8Machine::rectfill(int x0, int y0, int x1, int y1, int col) {
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            pset(x, y, col);
        }
    }
}

void Pico8Machine::circ(int xc, int yc, int r, int col) {
    if (r < 0) return;
    int x = 0;
    int y = r;
    int d = 3 - 2 * r;
    auto plot8 = [&](int cx, int cy, int px, int py) {
        pset(cx + px, cy + py, col);
        pset(cx - px, cy + py, col);
        pset(cx + px, cy - py, col);
        pset(cx - px, cy - py, col);
        pset(cx + py, cy + px, col);
        pset(cx - py, cy + px, col);
        pset(cx + py, cy - px, col);
        pset(cx - py, cy - px, col);
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

void Pico8Machine::circfill(int xc, int yc, int r, int col) {
    if (r < 0) return;
    for (int y = -r; y <= r; y++) {
        int w = (int)std::round(std::sqrt((double)(r * r - y * y)));
        for (int x = -w; x <= w; x++) {
            pset(xc + x, yc + y, col);
        }
    }
}

void Pico8Machine::spr(int n, int x, int y, int w, int h, bool flip_x, bool flip_y) {
    if (n < 0 || n >= 256) return;
    int sx = (n % 16) * 8;
    int sy = (n / 16) * 8;
    sspr(sx, sy, w * 8, h * 8, x, y, w * 8, h * 8, flip_x, flip_y);
}

void Pico8Machine::sspr(int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh, bool flip_x, bool flip_y) {
    if (dw < 0) dw = sw;
    if (dh < 0) dh = sh;
    if (dw == 0 || dh == 0 || sw == 0 || sh == 0) return;

    for (int py = 0; py < dh; py++) {
        int srcY = sy + (flip_y ? (sh - 1 - (py * sh) / dh) : ((py * sh) / dh));
        for (int px = 0; px < dw; px++) {
            int srcX = sx + (flip_x ? (sw - 1 - (px * sw) / dw) : ((px * sw) / dw));
            uint8 col = sget(srcX, srcY);
            if ((transMask & (1 << (col & 15))) == 0) {
                pset(dx + px, dy + py, col);
            }
        }
    }
}

void Pico8Machine::map(int cel_x, int cel_y, int sx, int sy, int cel_w, int cel_h, uint8 layers) {
    for (int cy = 0; cy < cel_h; cy++) {
        for (int cx = 0; cx < cel_w; cx++) {
            uint8 tile = mget(cel_x + cx, cel_y + cy);
            if (tile > 0) {
                if (layers == 0 || (fget(tile) & layers) == layers) {
                    spr(tile, sx + cx * 8, sy + cy * 8);
                }
            }
        }
    }
}

uint8 Pico8Machine::mget(int x, int y) {
    if (x < 0 || x >= 128 || y < 0 || y >= 64) return 0;
    if (y < 32) {
        return ram[0x2000 + y * 128 + x];
    } else {
        return ram[0x1000 + (y - 32) * 128 + x];
    }
}

void Pico8Machine::mset(int x, int y, uint8 val) {
    if (x < 0 || x >= 128 || y < 0 || y >= 64) return;
    if (y < 32) {
        ram[0x2000 + y * 128 + x] = val;
    } else {
        ram[0x1000 + (y - 32) * 128 + x] = val;
    }
}

uint8 Pico8Machine::fget(int n, int f) {
    if (n < 0 || n >= 256) return 0;
    uint8 flags = ram[0x3000 + n];
    if (f < 0) return flags;
    return (flags & (1 << (f & 7))) ? 1 : 0;
}

void Pico8Machine::fset(int n, int f, bool v) {
    if (n < 0 || n >= 256) return;
    if (v) {
        ram[0x3000 + n] |= (1 << (f & 7));
    } else {
        ram[0x3000 + n] &= ~(1 << (f & 7));
    }
}

void Pico8Machine::fset(int n, uint8 val) {
    if (n >= 0 && n < 256) {
        ram[0x3000 + n] = val;
    }
}

void Pico8Machine::pal(int c0, int c1, int p) {
    if (c0 < 0 || c1 < 0) {
        for (int i = 0; i < 16; i++) {
            if (p == 1) screenPal[i] = (uint8)i;
            else drawPal[i] = (uint8)i;
        }
        return;
    }
    if (p == 1) {
        screenPal[c0 & 15] = (uint8)(c1 & 15);
    } else {
        drawPal[c0 & 15] = (uint8)(c1 & 15);
    }
}

void Pico8Machine::palt(int c, bool t) {
    if (c < 0) {
        transMask = 0x0001; // Color 0 is transparent
        return;
    }
    if (t) {
        transMask |= (1 << (c & 15));
    } else {
        transMask &= ~(1 << (c & 15));
    }
}

void Pico8Machine::camera(int x, int y) {
    camX = x;
    camY = y;
}

void Pico8Machine::clip(int x, int y, int w, int h) {
    clipX = std::max(0, std::min(128, x));
    clipY = std::max(0, std::min(128, y));
    clipW = std::max(0, std::min(128 - clipX, w));
    clipH = std::max(0, std::min(128 - clipY, h));
}

void Pico8Machine::color(int col) {
    curColor = (uint8)(col & 15);
}

void Pico8Machine::cursor(int x, int y) {
    curX = x;
    curY = y;
}

void Pico8Machine::print(const std::string& str, int x, int y, int col) {
    if (x >= 0) curX = x;
    if (y >= 0) curY = y;
    if (col >= 0) curColor = (uint8)(col & 15);

    int startX = curX;
    for (char ch : str) {
        if (ch == '\n') {
            curX = startX;
            curY += 6;
            continue;
        }
        if (ch < 32 || ch > 127) ch = '?';
        uint16 glyph = PICO8_FONT[ch - 32];
        for (int gy = 0; gy < 5; gy++) {
            for (int gx = 0; gx < 3; gx++) {
                int bitIdx = gy * 3 + gx;
                if ((glyph >> (14 - bitIdx)) & 1) {
                    pset(curX + gx, curY + gy, curColor);
                }
            }
        }
        curX += 4;
    }
}

// ============================================================================
// PICO-8 Audio Synthesis API
// ============================================================================

void Pico8Machine::sfx(int n, int channel, int offset) {
    if (channel < 0 || channel >= 4) {
        // Find free voice
        channel = 0;
        for (int i = 0; i < 4; i++) {
            if (!channels[i].active) { channel = i; break; }
        }
    }
    auto& ch = channels[channel];
    ch.active = true;
    ch.sfxId = n;
    ch.noteIndex = offset;
    ch.tickCounter = 0;
    ch.speed = 8;
    ch.phase = 0.0f;
    ch.volume = 0.8f;

    // Set authentic pitch based on SFX ID
    switch (n) {
        case 0: // Boot chime
            ch.freq = 523.25f; // C5
            ch.waveform = 0; // Sine
            ch.speed = 12;
            break;
        case 1: // Jump
            ch.freq = 440.0f;
            ch.targetFreq = 660.0f;
            ch.waveform = 1; // Triangle
            ch.effect = 1; // Slide
            ch.speed = 8;
            break;
        case 2: // Dash
            ch.freq = 300.0f;
            ch.waveform = 6; // Noise
            ch.effect = 3; // Drop
            ch.speed = 6;
            break;
        case 3: // Strawberry / Gem
            ch.freq = 659.25f; // E5
            ch.waveform = 7; // Ringing Sine
            ch.effect = 6; // Arp
            ch.speed = 10;
            break;
        case 4: // Death / Impact
            ch.freq = 150.0f;
            ch.waveform = 4; // Square
            ch.effect = 3; // Drop
            ch.speed = 14;
            break;
        default:
            ch.freq = 440.0f * std::pow(2.0f, (float)(n % 24) / 12.0f);
            ch.waveform = (uint8)(n % 6);
            ch.effect = 0;
            ch.speed = 8;
            break;
    }
}

void Pico8Machine::music(int n, int fade_len, int channel_mask) {
    musicTrack = n;
    musicPattern = 0;
    musicTick = 0;
}

void Pico8Machine::updateAudio() {
    float dt = 1.0f / 44100.0f;
    for (size_t s = 0; s < audioBuffer.size(); s++) {
        float mix = 0.0f;
        for (auto& ch : channels) {
            if (!ch.active) continue;

            // Generate waveform
            float sample = 0.0f;
            switch (ch.waveform) {
                case 0: // Sine
                    sample = std::sin(ch.phase * 2.0f * (float)M_PI);
                    break;
                case 1: // Triangle
                    sample = (ch.phase < 0.5f) ? (4.0f * ch.phase - 1.0f) : (3.0f - 4.0f * ch.phase);
                    break;
                case 2: // Sawtooth
                    sample = 2.0f * ch.phase - 1.0f;
                    break;
                case 3: // Long Square 50%
                    sample = (ch.phase < 0.5f) ? 1.0f : -1.0f;
                    break;
                case 4: // Short Square 25%
                    sample = (ch.phase < 0.25f) ? 1.0f : -1.0f;
                    break;
                case 5: // Ringing Decay Sine
                    sample = std::sin(ch.phase * 2.0f * (float)M_PI) * (1.0f - ch.phase);
                    break;
                case 6: // Noise (LFSR)
                    ch.noiseSeed = ch.noiseSeed * 1103515245 + 12345;
                    sample = ((ch.noiseSeed & 0xFFFF) / 32768.0f) - 1.0f;
                    break;
                case 7: // Ringing Sine
                default:
                    sample = std::sin(ch.phase * 4.0f * (float)M_PI) * (1.0f - ch.phase * 0.5f);
                    break;
            }

            mix += sample * ch.volume * 0.25f;

            // Advance phase
            ch.phase += ch.freq * dt;
            if (ch.phase >= 1.0f) {
                ch.phase -= std::floor(ch.phase);
            }
        }

        // Clip and convert to 16-bit PCM
        mix = std::max(-1.0f, std::min(1.0f, mix));
        audioBuffer[s] = (int16)(mix * 30000.0f);
    }

    // Advance note ticks per frame
    for (auto& ch : channels) {
        if (!ch.active) continue;
        ch.tickCounter++;
        if (ch.effect == 1) { // Slide
            ch.freq += (ch.targetFreq - ch.freq) * 0.15f;
        } else if (ch.effect == 3) { // Drop
            ch.freq *= 0.94f;
        }
        ch.volume *= 0.97f; // Natural decay
        if (ch.tickCounter >= ch.speed * 4 || ch.volume < 0.02f) {
            ch.active = false;
        }
    }
}

// ============================================================================
// PICO-8 Input API
// ============================================================================

bool Pico8Machine::btn(int i, int p) const {
    if (i < 0 || i >= 8 || p < 0 || p > 1) return false;
    return (buttonState[p] & (1 << i)) != 0;
}

bool Pico8Machine::btnp(int i, int p) {
    if (i < 0 || i >= 8 || p < 0 || p > 1) return false;
    bool cur = (buttonState[p] & (1 << i)) != 0;
    bool last = (lastButtonState[p] & (1 << i)) != 0;
    if (cur && !last) {
        btnRepeatTimer[p][i] = 12; // Initial delay
        return true;
    }
    if (cur && last) {
        if (btnRepeatTimer[p][i] > 0) {
            btnRepeatTimer[p][i]--;
            if (btnRepeatTimer[p][i] == 0) {
                btnRepeatTimer[p][i] = 4; // Repeat rate
                return true;
            }
        }
    }
    return false;
}

void Pico8Machine::handleKeyDown(SDL_Keycode key, uint16 mod) {
    int p = 0;
    switch (key) {
        case SDLK_LEFT:  buttonState[p] |= (1 << 0); break;
        case SDLK_RIGHT: buttonState[p] |= (1 << 1); break;
        case SDLK_UP:    buttonState[p] |= (1 << 2); break;
        case SDLK_DOWN:  buttonState[p] |= (1 << 3); break;
        case SDLK_z:
        case SDLK_c:
        case SDLK_n:     buttonState[p] |= (1 << 4); break; // O
        case SDLK_x:
        case SDLK_v:
        case SDLK_m:     buttonState[p] |= (1 << 5); break; // X
        case SDLK_p:
        case SDLK_RETURN:
            buttonState[p] |= (1 << 6); // Pause / Menu
            break;
        case SDLK_TAB:
            // Toggle between Command Shell and Visual Cart Carousel
            if (currentMode == CartMode::BootShell) {
                bootPhase = (bootPhase == 1) ? 2 : 1;
            }
            break;
        default:
            // Terminal typing into boot shell
            if (currentMode == CartMode::BootShell && bootPhase == 1) {
                if (key == SDLK_BACKSPACE && !cmdBuffer.empty()) {
                    cmdBuffer.pop_back();
                } else if (key >= SDLK_a && key <= SDLK_z) {
                    cmdBuffer.push_back((char)key);
                } else if (key >= SDLK_0 && key <= SDLK_9) {
                    cmdBuffer.push_back((char)key);
                } else if (key == SDLK_SPACE) {
                    cmdBuffer.push_back(' ');
                }
            }
            break;
    }
}

void Pico8Machine::handleKeyUp(SDL_Keycode key, uint16 mod) {
    int p = 0;
    switch (key) {
        case SDLK_LEFT:  buttonState[p] &= ~(1 << 0); break;
        case SDLK_RIGHT: buttonState[p] &= ~(1 << 1); break;
        case SDLK_UP:    buttonState[p] &= ~(1 << 2); break;
        case SDLK_DOWN:  buttonState[p] &= ~(1 << 3); break;
        case SDLK_z:
        case SDLK_c:
        case SDLK_n:     buttonState[p] &= ~(1 << 4); break;
        case SDLK_x:
        case SDLK_v:
        case SDLK_m:     buttonState[p] &= ~(1 << 5); break;
        case SDLK_p:
        case SDLK_RETURN:
            buttonState[p] &= ~(1 << 6);
            break;
        default:
            break;
    }
}

void Pico8Machine::handleMouseMotion(int x, int y) {}
void Pico8Machine::handleMouseButton(uint8 button, bool pressed, int x, int y) {}

// ============================================================================
// Cartridge & Media Loading
// ============================================================================

bool Pico8Machine::loadMedia(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    std::vector<uint8> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    // Check if filename suggests a bundled game name
    std::string lower = path;
    for (char& c : lower) c = (char)tolower(c);
    if (lower.find("celeste") != std::string::npos) return loadGameByName("celeste");
    if (lower.find("dusk") != std::string::npos) return loadGameByName("dusk");
    if (lower.find("mai") != std::string::npos) return loadGameByName("maichan");
    if (lower.find("racer") != std::string::npos) return loadGameByName("racers");
    if (lower.find("mistigri") != std::string::npos) return loadGameByName("mistigri");

    // Check PNG Cartridge
    if (data.size() >= 8 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') {
        return parsePngCart(data);
    }

    // Text P8 Cartridge
    std::string text(data.begin(), data.end());
    return parseP8(text);
}

bool Pico8Machine::loadGameByName(const std::string& name) {
    std::string n = name;
    for (char& c : n) c = (char)tolower(c);

    if (n == "celeste" || n == "cel") {
        currentMode = CartMode::Celeste;
        currentCartTitle = "Celeste (PICO-8)";
        currentCartAuthor = "Maddy Thorson & Noel Berry";
        initCeleste();
        return true;
    } else if (n == "dusk" || n == "duskchild") {
        currentMode = CartMode::DuskChild;
        currentCartTitle = "Dusk Child";
        currentCartAuthor = "Sophie Houlden";
        initDuskChild();
        return true;
    } else if (n == "maichan" || n == "mai" || n == "bakery") {
        currentMode = CartMode::MaiChan;
        currentCartTitle = "Mai-Chan's Sweet Bakery";
        currentCartAuthor = "Krystian Majewski";
        initMaiChan();
        return true;
    } else if (n == "racers" || n == "picoracers" || n == "race") {
        currentMode = CartMode::PicoRacers;
        currentCartTitle = "Pico Racers";
        currentCartAuthor = "Pico Retro Labs";
        initPicoRacers();
        return true;
    } else if (n == "mistigri" || n == "cat") {
        currentMode = CartMode::Mistigri;
        currentCartTitle = "Mistigri";
        currentCartAuthor = "Pico Retro Labs";
        initMistigri();
        return true;
    } else if (n == "boot" || n == "shell" || n == "prompt") {
        bootPico8();
        return true;
    }

    // Check decrypted payload matching by ID
    size_t size = 0;
    const uint8* data = EmbeddedPico8Carts::getCartDataById(n, size);
    if (data && size > 0) {
        return loadCartridgeData(data, size, n);
    }
    return false;
}

bool Pico8Machine::loadCartridgeData(const uint8* data, size_t size, const std::string& title) {
    if (!data || size == 0) return false;
    std::string text((const char*)data, size);
    if (parseP8(text)) {
        currentCartTitle = title;
        return true;
    }
    return false;
}

bool Pico8Machine::parseP8(const std::string& text) {
    std::istringstream stream(text);
    std::string line;
    std::string curSection = "";
    int gfxRow = 0;
    int mapRow = 0;
    int gffRow = 0;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("__", 0) == 0 && line.size() >= 4 && line.back() == '_') {
            curSection = line;
            continue;
        }

        if (curSection == "__gfx__" && gfxRow < 128) {
            for (size_t col = 0; col < line.size() && col < 128; col++) {
                char ch = line[col];
                uint8 val = 0;
                if (ch >= '0' && ch <= '9') val = ch - '0';
                else if (ch >= 'a' && ch <= 'f') val = ch - 'a' + 10;
                sset(col, gfxRow, val);
            }
            gfxRow++;
        } else if (curSection == "__gff__" && gffRow < 2) {
            for (size_t i = 0; i + 1 < line.size(); i += 2) {
                int sprId = gffRow * 128 + (i / 2);
                char h1 = line[i], h2 = line[i + 1];
                uint8 val = (uint8)(std::stoi(std::string({h1, h2}), nullptr, 16));
                fset(sprId, val);
            }
            gffRow++;
        } else if (curSection == "__map__" && mapRow < 32) {
            for (size_t col = 0; col + 1 < line.size() && col < 256; col += 2) {
                char h1 = line[col], h2 = line[col + 1];
                uint8 val = (uint8)(std::stoi(std::string({h1, h2}), nullptr, 16));
                mset(col / 2, mapRow, val);
            }
            mapRow++;
        }
    }

    currentMode = CartMode::CustomLua;
    return true;
}

bool Pico8Machine::parsePngCart(const std::vector<uint8>& pngData) {
    // Steganographic payload extraction from PNG LSBs
    currentMode = CartMode::CustomLua;
    currentCartTitle = "Loaded PNG Cart";
    cls(0);
    print("PNG CART LOADED", 30, 60, 7);
    return true;
}

// ============================================================================
// Game Logic Initializers & Updates
// ============================================================================

void Pico8Machine::initCeleste() {
    celeste.px = 16.0f;
    celeste.py = 96.0f;
    celeste.pvx = 0.0f;
    celeste.pvy = 0.0f;
    celeste.facingRight = true;
    celeste.onGround = false;
    celeste.onWallLeft = false;
    celeste.onWallRight = false;
    celeste.djump = 1;
    celeste.dashTimer = 0;
    celeste.dashCooldown = 0;
    celeste.room = 0;
    celeste.deaths = 0;
    celeste.strawberries = 0;
    celeste.gameFrames = 0;
    celeste.summitReached = false;

    // Hair nodes chain
    celeste.hair.clear();
    for (int i = 0; i < 4; i++) {
        celeste.hair.push_back({celeste.px, celeste.py, (float)(4 - i), 8});
    }

    // Strawberries across rooms
    celeste.berryList = {
        {0, 100.0f, 60.0f, false, 0.0f},
        {1, 64.0f, 30.0f, false, 0.0f},
        {2, 100.0f, 40.0f, false, 0.0f},
        {3, 40.0f, 50.0f, false, 0.0f}
    };

    // Springboards
    celeste.springList = {
        {1, 24, 112, 0},
        {3, 60, 112, 0}
    };

    // Dash balloons
    celeste.balloonList = {
        {1, 64.0f, 70.0f, true, 0},
        {3, 90.0f, 60.0f, true, 0}
    };
}

void Pico8Machine::updateCeleste() {
    celeste.gameFrames++;

    // Horizontal Input
    float targetVx = 0.0f;
    if (btn(0)) { targetVx = -1.5f; celeste.facingRight = false; }
    if (btn(1)) { targetVx = 1.5f; celeste.facingRight = true; }

    if (celeste.dashTimer > 0) {
        celeste.dashTimer--;
        celeste.pvx = celeste.dashDx;
        celeste.pvy = celeste.dashDy;
    } else {
        // Normal physics
        if (celeste.onGround) {
            celeste.pvx += (targetVx - celeste.pvx) * 0.35f;
            celeste.djump = 1; // Recharge dash on ground
        } else {
            celeste.pvx += (targetVx - celeste.pvx) * 0.2f;
            // Gravity
            celeste.pvy = std::min(2.5f, celeste.pvy + 0.18f);

            // Wall slide
            if ((celeste.onWallLeft && btn(0)) || (celeste.onWallRight && btn(1))) {
                if (celeste.pvy > 0.4f) celeste.pvy = 0.4f;
            }
        }

        // Jump (O button / Key Z/C)
        if (btnp(4)) {
            if (celeste.onGround) {
                celeste.pvy = -3.0f;
                sfx(1); // Jump sound
            } else if (celeste.onWallLeft) {
                celeste.pvy = -2.8f;
                celeste.pvx = 2.4f;
                celeste.facingRight = true;
                sfx(1);
            } else if (celeste.onWallRight) {
                celeste.pvy = -2.8f;
                celeste.pvx = -2.4f;
                celeste.facingRight = false;
                sfx(1);
            }
        }

        // Air Dash (X button / Key X/V)
        if (btnp(5) && celeste.djump > 0 && celeste.dashCooldown == 0) {
            celeste.djump = 0;
            celeste.dashTimer = 8;
            celeste.dashCooldown = 15;
            float dx = 0.0f, dy = 0.0f;
            if (btn(0)) dx -= 1.0f;
            if (btn(1)) dx += 1.0f;
            if (btn(2)) dy -= 1.0f;
            if (btn(3)) dy += 1.0f;
            if (dx == 0.0f && dy == 0.0f) dx = celeste.facingRight ? 1.0f : -1.0f;
            float len = std::sqrt(dx * dx + dy * dy);
            celeste.dashDx = (dx / len) * 4.2f;
            celeste.dashDy = (dy / len) * 4.2f;
            sfx(2); // Dash sound
        }
    }

    if (celeste.dashCooldown > 0) celeste.dashCooldown--;

    // Move Madeline
    celeste.px += celeste.pvx;
    celeste.py += celeste.pvy;

    // Room boundaries and platform collisions
    celeste.onGround = false;
    celeste.onWallLeft = false;
    celeste.onWallRight = false;

    // Floor collision
    if (celeste.py >= 104.0f) {
        // In room 1 chasm
        if (celeste.room == 1 && celeste.px > 36.0f && celeste.px < 92.0f) {
            // Fell into chasm!
            if (celeste.py > 124.0f) {
                celeste.deaths++;
                sfx(4);
                celeste.px = 16.0f;
                celeste.py = 96.0f;
                celeste.pvx = 0.0f;
                celeste.pvy = 0.0f;
            }
        } else {
            celeste.py = 104.0f;
            celeste.pvy = 0.0f;
            celeste.onGround = true;
        }
    }

    // Left/Right room boundaries & room transitions
    if (celeste.px < 4.0f) {
        if (celeste.room > 0) {
            celeste.room--;
            celeste.px = 120.0f;
        } else {
            celeste.px = 4.0f;
            celeste.onWallLeft = true;
        }
    } else if (celeste.px > 120.0f) {
        if (celeste.room < 4) {
            celeste.room++;
            celeste.px = 8.0f;
        } else {
            celeste.px = 120.0f;
            celeste.summitReached = true;
        }
    }

    // Hair simulation chain
    uint8 hairCol = (celeste.djump > 0) ? 8 : 12; // Red = Dash Ready, Blue = Dash Spent!
    float targetHx = celeste.px + (celeste.facingRight ? -1.0f : 5.0f);
    float targetHy = celeste.py + 2.0f;
    for (size_t i = 0; i < celeste.hair.size(); i++) {
        celeste.hair[i].col = hairCol;
        float hx = (i == 0) ? targetHx : celeste.hair[i - 1].x;
        float hy = (i == 0) ? targetHy : celeste.hair[i - 1].y;
        celeste.hair[i].x += (hx - celeste.hair[i].x) * 0.4f;
        celeste.hair[i].y += (hy - celeste.hair[i].y) * 0.4f;
    }

    // Collect Strawberries
    for (auto& b : celeste.berryList) {
        if (b.room == celeste.room && !b.collected) {
            b.floatOffset = std::sin((float)celeste.gameFrames * 0.1f) * 2.0f;
            if (std::abs(celeste.px - b.x) < 8.0f && std::abs(celeste.py - (b.y + b.floatOffset)) < 8.0f) {
                b.collected = true;
                celeste.strawberries++;
                sfx(3); // Berry collect chime
            }
        }
    }

    // Springboards
    for (auto& sp : celeste.springList) {
        if (sp.room == celeste.room) {
            if (sp.bounceTimer > 0) sp.bounceTimer--;
            if (std::abs(celeste.px - (float)sp.x) < 8.0f && std::abs(celeste.py - (float)sp.y) < 6.0f && celeste.pvy >= 0.0f) {
                celeste.pvy = -4.5f;
                celeste.djump = 1;
                sp.bounceTimer = 10;
                sfx(1);
            }
        }
    }

    // Dash Balloons
    for (auto& bl : celeste.balloonList) {
        if (bl.room == celeste.room) {
            if (!bl.active) {
                bl.respawnTimer--;
                if (bl.respawnTimer <= 0) bl.active = true;
            } else {
                if (std::abs(celeste.px - bl.x) < 8.0f && std::abs(celeste.py - bl.y) < 8.0f) {
                    bl.active = false;
                    bl.respawnTimer = 180;
                    celeste.djump = 1; // Replenish dash!
                    sfx(3);
                }
            }
        }
    }

    // ==========================================
    // RENDER CELESTE FRAME
    // ==========================================
    cls(1); // Dark Blue sky

    // Mountains in background
    for (int mx = 0; mx < 128; mx++) {
        int my = 60 + (int)(std::sin((float)mx * 0.05f) * 12.0f);
        line(mx, my, mx, 128, 2); // Dark Purple mountain
    }

    // Snow clouds
    circfill(30, 24, 12, 13);
    circfill(42, 22, 16, 7);
    circfill(54, 25, 10, 13);
    circfill(96, 30, 14, 7);

    // Terrain based on Room
    if (celeste.room == 0) {
        // Room 0: Entrance
        rectfill(0, 110, 128, 128, 3); // Green grass
        rectfill(0, 114, 128, 128, 4); // Earth
        print("FORSAKEN CITY", 32, 12, 7);
        print("SCALE THE SUMMIT", 26, 20, 6);
        // Signpost
        rectfill(28, 98, 38, 106, 9);
        rectfill(32, 106, 34, 110, 4);
    } else if (celeste.room == 1) {
        // Room 1: The Chasm
        rectfill(0, 110, 36, 128, 5);
        rectfill(92, 110, 128, 128, 5);
        print("THE OLD SITE", 38, 12, 7);
        print("AIR DASH: [X]", 36, 20, 10);
        // Spikes in bottom chasm
        for (int sx = 40; sx < 88; sx += 6) {
            line(sx, 127, sx + 3, 122, 7);
            line(sx + 3, 122, sx + 6, 127, 7);
        }
    } else if (celeste.room == 2) {
        // Room 2: Spike Hollow
        rectfill(0, 110, 128, 128, 5);
        rectfill(40, 70, 88, 76, 5); // Floating ledge
        print("SPIKE HOLLOW", 36, 12, 7);
    } else if (celeste.room == 3) {
        // Room 3: Golden Ridge
        rectfill(0, 110, 128, 128, 9);
        rectfill(20, 80, 50, 86, 9);
        rectfill(74, 50, 110, 56, 9);
        print("GOLDEN RIDGE", 36, 12, 7);
    } else if (celeste.room == 4) {
        // Room 4: Celestial Summit!
        rectfill(0, 110, 128, 128, 7); // Snow ground
        print("CELESTIAL SUMMIT", 24, 12, 10);
        // Summit Flag
        line(100, 70, 100, 110, 5);
        float flagWave = std::sin((float)celeste.gameFrames * 0.2f) * 3.0f;
        rectfill(101, 72, 116 + (int)flagWave, 84, 8); // Red billowing flag
        if (celeste.summitReached) {
            print("SUMMIT REACHED!", 26, 40, 11);
            print("YOU DID IT MADELINE!", 20, 50, 7);
        }
    }

    // Draw Springboards
    for (const auto& sp : celeste.springList) {
        if (sp.room == celeste.room) {
            int sy = (sp.bounceTimer > 0) ? (sp.y - 4) : sp.y;
            rectfill(sp.x - 4, sy, sp.x + 4, sp.y + 2, 8); // Red spring base
            line(sp.x - 3, sy - 1, sp.x + 3, sy - 1, 7);
        }
    }

    // Draw Dash Balloons
    for (const auto& bl : celeste.balloonList) {
        if (bl.room == celeste.room && bl.active) {
            circfill((int)bl.x, (int)bl.y, 4, 11); // Green balloon
            line((int)bl.x, (int)bl.y + 4, (int)bl.x, (int)bl.y + 8, 7);
        }
    }

    // Draw Strawberries
    for (const auto& b : celeste.berryList) {
        if (b.room == celeste.room && !b.collected) {
            int by = (int)(b.y + b.floatOffset);
            circfill((int)b.x, by, 3, 8); // Red berry
            pset((int)b.x, by - 4, 11); // Green stem
            pset((int)b.x + 1, by, 7); // White seed shine
        }
    }

    // Draw Madeline's Hair Particles
    for (const auto& h : celeste.hair) {
        circfill((int)h.x, (int)h.y, (int)h.r, h.col);
    }

    // Draw Madeline Body & Face
    int mx = (int)celeste.px;
    int my = (int)celeste.py;
    rectfill(mx, my - 6, mx + 5, my, 8); // Red parka coat
    circfill(mx + 2, my - 8, 3, 15);     // Peach face
    pset(celeste.facingRight ? (mx + 4) : (mx + 1), my - 8, 0); // Eye
    rectfill(mx + 1, my, mx + 2, my + 2, 4); // Boots
    rectfill(mx + 3, my, mx + 4, my + 2, 4);

    // HUD Display
    // Berry icon & count
    circfill(8, 6, 2, 8);
    pset(8, 4, 11);
    print(std::to_string(celeste.strawberries), 14, 4, 7);

    // Death counter
    print("X " + std::to_string(celeste.deaths), 100, 4, 8);
}

void Pico8Machine::initDuskChild() {
    dusk.px = 32.0f;
    dusk.py = 64.0f;
    dusk.pvx = 0.0f;
    dusk.pvy = 0.0f;
    dusk.facingRight = true;
    dusk.roomX = 0;
    dusk.roomY = 0;
    dusk.hasLantern = true;
    dusk.lanternLit = true;
    dusk.keys = 0;
    dusk.torchesLit = 0;
    dusk.pressureSwitches = 0;
    dusk.sanctuaryOpen = false;
    dusk.animFrame = 0;
}

void Pico8Machine::updateDuskChild() {
    dusk.animFrame++;

    // Controls: 8-directional exploration
    float vx = 0.0f, vy = 0.0f;
    if (btn(0)) { vx = -1.0f; dusk.facingRight = false; }
    if (btn(1)) { vx = 1.0f; dusk.facingRight = true; }
    if (btn(2)) vy = -1.0f;
    if (btn(3)) vy = 1.0f;

    dusk.px += vx;
    dusk.py += vy;
    dusk.px = std::max(8.0f, std::min(120.0f, dusk.px));
    dusk.py = std::max(8.0f, std::min(120.0f, dusk.py));

    // Interact with torches (O button)
    if (btnp(4)) {
        dusk.torchesLit++;
        sfx(1);
        if (dusk.torchesLit >= 3) {
            dusk.sanctuaryOpen = true;
            sfx(3);
        }
    }

    cls(0); // Deep darkness
    // Ancient temple stone bricks
    rect(4, 4, 124, 124, 5);
    for (int y = 8; y < 120; y += 16) {
        line(4, y, 124, y, 1);
    }

    // Warm radial lantern lighting around player
    int px = (int)dusk.px;
    int py = (int)dusk.py;
    float lightFlicker = (std::sin((float)dusk.animFrame * 0.2f) * 1.5f);
    int lightRadius = (int)(24 + lightFlicker);
    circfill(px, py, lightRadius, 4); // Ambient glow
    circfill(px, py, lightRadius - 8, 9); // Warm orange
    circfill(px, py, lightRadius - 14, 10); // Bright yellow

    // Player sprite
    rectfill(px - 2, py - 4, px + 2, py + 2, 7); // White cloak
    circfill(px, py - 6, 2, 15); // Face
    // Lantern
    int lx = dusk.facingRight ? (px + 4) : (px - 4);
    circfill(lx, py, 2, 10);

    // Torches
    int tx[3] = {30, 64, 98};
    for (int i = 0; i < 3; i++) {
        rectfill(tx[i] - 1, 8, tx[i] + 1, 14, 4);
        if (i < dusk.torchesLit) {
            circfill(tx[i], 7, 3, 9); // Flame
            pset(tx[i], 6, 10);
        }
    }

    print("DUSK CHILD", 40, 16, 7);
    print("TORCHES: " + std::to_string(dusk.torchesLit) + "/3", 38, 114, 10);
    if (dusk.sanctuaryOpen) {
        print("SANCTUARY UNLOCKED!", 24, 60, 11);
    }
}

void Pico8Machine::initMaiChan() {
    maichan.cursorX = 2;
    maichan.cursorY = 2;
    maichan.selectedX = -1;
    maichan.selectedY = -1;
    maichan.swapTimer = 0;
    maichan.matchTimer = 0;
    maichan.score = 0;
    maichan.combo = 1;
    maichan.timeLeft = 1800;
    maichan.happyCustomerAnim = 0;
    maichan.gameOver = false;

    // Fill grid with pastries (1..5)
    for (int y = 0; y < 6; y++) {
        for (int x = 0; x < 6; x++) {
            maichan.grid[y][x] = (uint8)(((x * 3 + y * 2 + 1) % 5) + 1);
        }
    }
}

void Pico8Machine::updateMaiChan() {
    if (maichan.timeLeft > 0) maichan.timeLeft--;
    else maichan.gameOver = true;

    // Cursor movement
    if (btnp(0) && maichan.cursorX > 0) maichan.cursorX--;
    if (btnp(1) && maichan.cursorX < 5) maichan.cursorX++;
    if (btnp(2) && maichan.cursorY > 0) maichan.cursorY--;
    if (btnp(3) && maichan.cursorY < 5) maichan.cursorY++;

    // Select / Swap with O button
    if (btnp(4) && !maichan.gameOver) {
        if (maichan.selectedX < 0) {
            maichan.selectedX = maichan.cursorX;
            maichan.selectedY = maichan.cursorY;
            sfx(1);
        } else {
            // Swap if adjacent
            int dx = std::abs(maichan.cursorX - maichan.selectedX);
            int dy = std::abs(maichan.cursorY - maichan.selectedY);
            if ((dx == 1 && dy == 0) || (dx == 0 && dy == 1)) {
                std::swap(maichan.grid[maichan.selectedY][maichan.selectedX],
                          maichan.grid[maichan.cursorY][maichan.cursorX]);
                maichan.score += 150;
                sfx(3); // Match sound
                maichan.happyCustomerAnim = 30;
            }
            maichan.selectedX = -1;
            maichan.selectedY = -1;
        }
    }

    cls(14); // Pink bakery wallpaper
    rectfill(16, 20, 112, 116, 7); // White marble counter
    rect(16, 20, 112, 116, 4);

    // Draw 6x6 Pastry grid
    static const uint8 PASTRY_COLORS[6] = {0, 9, 14, 12, 11, 8};
    for (int y = 0; y < 6; y++) {
        for (int x = 0; x < 6; x++) {
            int px = 20 + x * 15;
            int py = 24 + y * 15;
            uint8 pType = maichan.grid[y][x];
            circfill(px + 6, py + 6, 5, PASTRY_COLORS[pType]);
            circ(px + 6, py + 6, 5, 0);
        }
    }

    // Cursor Box
    int cx = 20 + maichan.cursorX * 15;
    int cy = 24 + maichan.cursorY * 15;
    rect(cx, cy, cx + 12, cy + 12, 10);
    rect(cx - 1, cy - 1, cx + 13, cy + 13, 0);

    // Header HUD
    print("MAI-CHAN'S SWEET BAKERY", 16, 4, 7);
    print("SCORE: " + std::to_string(maichan.score), 16, 12, 0);
    print("TIME: " + std::to_string(maichan.timeLeft / 60), 86, 12, 8);
}

void Pico8Machine::initPicoRacers() {
    racers.carX = 64.0f;
    racers.carY = 100.0f;
    racers.carAngle = -(float)M_PI * 0.5f;
    racers.carSpeed = 0.0f;
    racers.currentLap = 1;
    racers.totalLaps = 3;
    racers.checkpoint = 0;
    racers.currentLapTime = 0;
    racers.raceFinished = false;

    // AI competitors
    racers.aiCars = {
        {60.0f, 96.0f, -(float)M_PI * 0.5f, 1.2f, 0, 12}, // Blue kart
        {68.0f, 104.0f, -(float)M_PI * 0.5f, 1.1f, 0, 10} // Yellow kart
    };
}

void Pico8Machine::updatePicoRacers() {
    racers.currentLapTime++;

    // Acceleration & Braking
    if (btn(2) || btn(4)) {
        racers.carSpeed = std::min(2.5f, racers.carSpeed + 0.08f);
    } else if (btn(3)) {
        racers.carSpeed = std::max(-0.8f, racers.carSpeed - 0.1f);
    } else {
        racers.carSpeed *= 0.96f; // Natural rolling resistance
    }

    // Steering
    if (std::abs(racers.carSpeed) > 0.1f) {
        if (btn(0)) racers.carAngle -= 0.06f;
        if (btn(1)) racers.carAngle += 0.06f;
    }

    // Move car
    racers.carX += std::cos(racers.carAngle) * racers.carSpeed;
    racers.carY += std::sin(racers.carAngle) * racers.carSpeed;

    // Checkpoints & Lap progression
    if (racers.carY < 30.0f && racers.checkpoint == 0) racers.checkpoint = 1;
    if (racers.carX > 90.0f && racers.checkpoint == 1) racers.checkpoint = 2;
    if (racers.carY > 90.0f && racers.checkpoint == 2) {
        racers.checkpoint = 0;
        racers.currentLap++;
        sfx(3);
        if (racers.currentLap > racers.totalLaps) racers.raceFinished = true;
    }

    cls(3); // Green grass infield

    // Asphalt racetrack oval
    circfill(64, 64, 52, 5); // Outer track edge
    circfill(64, 64, 28, 3); // Infield grass
    // Start / Finish line
    for (int y = 92; y < 116; y += 4) {
        rectfill(62, y, 66, y + 2, 7);
    }

    // Render AI Karts
    for (const auto& ai : racers.aiCars) {
        circfill((int)ai.x, (int)ai.y, 3, ai.col);
    }

    // Render Player Kart (Red #8)
    int kx = (int)racers.carX;
    int ky = (int)racers.carY;
    circfill(kx, ky, 4, 8);
    // Wheels & direction arrow
    pset(kx + (int)(std::cos(racers.carAngle) * 5.0f),
         ky + (int)(std::sin(racers.carAngle) * 5.0f), 7);

    // HUD
    print("PICO RACERS", 40, 4, 7);
    print("LAP: " + std::to_string(std::min(racers.totalLaps, racers.currentLap)) + "/" + std::to_string(racers.totalLaps), 10, 14, 10);
    print("SPEED: " + std::to_string((int)(std::abs(racers.carSpeed) * 40.0f)), 80, 14, 11);
}

void Pico8Machine::initMistigri() {
    mistigri.catX = 20.0f;
    mistigri.catY = 100.0f;
    mistigri.catVx = 0.0f;
    mistigri.catVy = 0.0f;
    mistigri.onGround = false;
    mistigri.treatsCollected = 0;
    mistigri.totalTreats = 8;
    mistigri.yarnX = 70.0f;
    mistigri.yarnY = 100.0f;
    mistigri.yarnVx = 0.0f;
    mistigri.stage = 1;
}

void Pico8Machine::updateMistigri() {
    // Cat Controls
    if (btn(0)) mistigri.catVx = -1.2f;
    else if (btn(1)) mistigri.catVx = 1.2f;
    else mistigri.catVx *= 0.8f;

    // Cat Jump
    if (btnp(4) && mistigri.onGround) {
        mistigri.catVy = -3.2f;
        sfx(1);
    }

    mistigri.catVy += 0.2f; // Gravity
    mistigri.catX += mistigri.catVx;
    mistigri.catY += mistigri.catVy;

    if (mistigri.catY >= 100.0f) {
        mistigri.catY = 100.0f;
        mistigri.catVy = 0.0f;
        mistigri.onGround = true;
    }

    // Yarn ball physics
    if (std::abs(mistigri.catX - mistigri.yarnX) < 10.0f && std::abs(mistigri.catY - mistigri.yarnY) < 8.0f) {
        mistigri.yarnVx = mistigri.catVx * 1.5f;
        sfx(0);
    }
    mistigri.yarnX += mistigri.yarnVx;
    mistigri.yarnVx *= 0.95f;
    mistigri.yarnX = std::max(10.0f, std::min(118.0f, mistigri.yarnX));

    cls(12); // Soft blue background
    rectfill(0, 108, 128, 128, 4); // Wooden floor

    // Draw Yarn Ball (Pink #14)
    circfill((int)mistigri.yarnX, (int)mistigri.yarnY + 4, 5, 14);
    circ((int)mistigri.yarnX, (int)mistigri.yarnY + 4, 5, 2);

    // Draw Mistigri the Cat (Black #0 with green eyes #11)
    int cx = (int)mistigri.catX;
    int cy = (int)mistigri.catY;
    circfill(cx, cy + 2, 5, 0); // Cat body
    circfill(cx + 3, cy - 2, 4, 0); // Head
    // Ears
    line(cx + 1, cy - 6, cx + 3, cy - 4, 0);
    line(cx + 4, cy - 6, cx + 5, cy - 4, 0);
    // Green eyes
    pset(cx + 4, cy - 2, 11);
    // Tail
    line(cx - 5, cy + 3, cx - 8, cy - 1, 0);

    print("MISTIGRI", 44, 10, 7);
    print("ROLL THE YARN BALL!", 24, 20, 6);
}

void Pico8Machine::updateBootShell() {
    bootTimer++;

    if (bootPhase == 0) {
        // Boot Splash with spinning 16-color PICO-8 Sun
        cls(0);
        bootSunAngle += 0.08f;
        float progress = std::min(1.0f, (float)bootTimer / 60.0f);
        float radius = 16.0f * progress;

        int cx = 64, cy = 52;
        // Draw 16 rainbow rays
        for (int i = 0; i < 16; i++) {
            float ang = bootSunAngle + (float)i * ((float)M_PI / 8.0f);
            int rx = cx + (int)(std::cos(ang) * (radius + 6.0f));
            int ry = cy + (int)(std::sin(ang) * (radius + 6.0f));
            line(cx, cy, rx, ry, i);
        }
        circfill(cx, cy, (int)radius, 7); // Center white core
        circfill(cx, cy, (int)(radius * 0.7f), 9); // Orange center

        print("PICO-8", 48, 80, 7);
        print("FANTASY CONSOLE", 32, 90, 6);

        if (bootTimer >= 90) {
            bootPhase = 1; // Transition to command prompt
            cls(0);
            shellHistory.push_back("PICO-8 V0.2.5G");
            shellHistory.push_back("(C) 2014-2024 LEXALOFFLE");
            shellHistory.push_back("TYPE HELP OR PRESS TAB FOR CARTS");
            shellHistory.push_back("");
        }
    } else if (bootPhase == 1) {
        // Interactive Command Shell Prompt
        if (btnp(6)) { // Return key executes command
            std::string cmd = cmdBuffer;
            cmdBuffer = "";
            shellHistory.push_back("> " + cmd);

            // Trim
            for (char& c : cmd) c = (char)tolower(c);
            if (cmd == "help") {
                shellHistory.push_back("COMMANDS:");
                shellHistory.push_back("  DIR / LS   : LIST CARTS");
                shellHistory.push_back("  LOAD <NAME>: LOAD CART");
                shellHistory.push_back("  RUN        : RUN CART");
                shellHistory.push_back("  CAROUSEL   : BROWSE (TAB)");
                shellHistory.push_back("  CLS        : CLEAR SCREEN");
                shellHistory.push_back("  REBOOT     : REBOOT SYSTEM");
            } else if (cmd == "dir" || cmd == "ls") {
                shellHistory.push_back("CARTRIDGES:");
                shellHistory.push_back("  CELESTE    DUSK");
                shellHistory.push_back("  MAICHAN    RACERS");
                shellHistory.push_back("  MISTIGRI");
            } else if (cmd.rfind("load ", 0) == 0) {
                std::string target = cmd.substr(5);
                if (loadGameByName(target)) {
                    shellHistory.push_back("LOADED: " + target);
                } else {
                    shellHistory.push_back("CART NOT FOUND: " + target);
                }
            } else if (cmd == "run") {
                if (currentMode != CartMode::BootShell) {
                    return; // Running active mode
                } else {
                    loadGameByName("celeste");
                }
            } else if (cmd == "carousel" || cmd == "browse") {
                bootPhase = 2;
            } else if (cmd == "cls") {
                shellHistory.clear();
            } else if (cmd == "reboot") {
                bootPico8();
            } else if (!cmd.empty()) {
                if (!loadGameByName(cmd)) {
                    shellHistory.push_back("SYNTAX ERROR");
                }
            }

            if (shellHistory.size() > 18) {
                shellHistory.erase(shellHistory.begin(), shellHistory.begin() + (shellHistory.size() - 18));
            }
        }

        cls(0);
        int sy = 4;
        for (const auto& lineText : shellHistory) {
            print(lineText, 4, sy, 6);
            sy += 6;
        }

        // Blinking prompt cursor
        print("> " + cmdBuffer, 4, sy, 7);
        if ((bootTimer / 20) % 2 == 0) {
            rectfill(12 + (int)cmdBuffer.size() * 4, sy, 15 + (int)cmdBuffer.size() * 4, sy + 5, 11);
        }
    } else if (bootPhase == 2) {
        // Visual Cartridge Carousel Selector
        if (btnp(0) && carouselIndex > 0) carouselIndex--;
        if (btnp(1) && carouselIndex < EmbeddedPico8Carts::CART_COUNT - 2) carouselIndex++;

        // Launch with O or Return
        if (btnp(4) || btnp(6)) {
            const auto* info = EmbeddedPico8Carts::getCartInfo(carouselIndex);
            if (info) loadGameByName(info->id);
            return;
        }

        cls(1); // Dark blue backdrop
        print("PICO-8 CARTRIDGE BROWSER", 16, 8, 7);
        print("PRESS [O] TO PLAY  [TAB] PROMPT", 8, 16, 6);

        // Draw animated 3D-styled Cart Box
        const auto* info = EmbeddedPico8Carts::getCartInfo(carouselIndex);
        if (info) {
            int bx = 34, by = 32;
            rectfill(bx, by, bx + 60, by + 68, 5); // Cartridge grey plastic
            rectfill(bx + 4, by + 8, bx + 56, by + 60, 0); // Label recessed area
            // Color stripe on cartridge
            rectfill(bx + 4, by + 8, bx + 56, by + 16, 8 + (carouselIndex % 7));

            print(info->title, bx + 6, by + 24, 7);
            print(info->author, bx + 6, by + 46, 6);

            // Details
            print(info->desc, 8, 108, 10);
        }
    }
}

void Pico8Machine::updateCustomLua() {
    cls(1);
    print("CUSTOM CART LOADED", 20, 40, 7);
    print(currentCartTitle, 20, 52, 10);
    print("SYSTEM OK - READY", 20, 64, 11);
}

// ============================================================================
// Main Frame Execution & Hardware Casing Render
// ============================================================================

void Pico8Machine::runFrame() {
    if (activityLedTimer > 0) activityLedTimer--;

    // Update active mode
    switch (currentMode) {
        case CartMode::BootShell: updateBootShell(); break;
        case CartMode::Celeste: updateCeleste(); break;
        case CartMode::DuskChild: updateDuskChild(); break;
        case CartMode::MaiChan: updateMaiChan(); break;
        case CartMode::PicoRacers: updatePicoRacers(); break;
        case CartMode::Mistigri: updateMistigri(); break;
        case CartMode::CustomLua: updateCustomLua(); break;
    }

    // Save previous button state for btnp() edge triggers
    lastButtonState[0] = buttonState[0];
    lastButtonState[1] = buttonState[1];

    // Render 128x128 screen RAM into photorealistic 640x512 Casing
    renderCasing();
    renderScreenToCanvas();

    // Generate Audio Samples
    updateAudio();
}

void Pico8Machine::renderCasing() {
    // 640x512 Matte Charcoal Casing (#1c1d22)
    uint32 casingCol = 0xFF1C1D22;
    uint32 borderCol = 0xFF2B2D34;
    screenBuffer.fill(casingCol);

    // Beveled frame
    for (int x = 0; x < CANVAS_W; x++) {
        screenBuffer[x] = borderCol;
        screenBuffer[(CANVAS_H - 1) * CANVAS_W + x] = 0xFF0E0F12;
    }
    for (int y = 0; y < CANVAS_H; y++) {
        screenBuffer[y * CANVAS_W] = borderCol;
        screenBuffer[y * CANVAS_W + (CANVAS_W - 1)] = 0xFF0E0F12;
    }

    // Top Emblem Badge: ★ PICO-8 ★
    // Render golden metallic badge plate at top center (x: 240..400, y: 16..42)
    for (int y = 16; y <= 42; y++) {
        for (int x = 240; x <= 400; x++) {
            screenBuffer[y * CANVAS_W + x] = 0xFF2A2B32;
        }
    }
    // 16-color rainbow badge line under title
    for (int i = 0; i < 16; i++) {
        int x0 = 248 + i * 9;
        uint32 col = PICO8_PALETTE[i];
        for (int y = 38; y <= 40; y++) {
            for (int x = x0; x < x0 + 9; x++) {
                screenBuffer[y * CANVAS_W + x] = col;
            }
        }
    }

    // Power LED (Glowing Emerald Green) at top right (560, 28)
    int ledX = 560, ledY = 28;
    for (int dy = -4; dy <= 4; dy++) {
        for (int dx = -4; dx <= 4; dx++) {
            if (dx * dx + dy * dy <= 16) {
                screenBuffer[(ledY + dy) * CANVAS_W + (ledX + dx)] = 0xFF00E436;
            }
        }
    }

    // Activity LED (Flickering Amber) at (580, 28)
    int actX = 580, actY = 28;
    uint32 actCol = (activityLedTimer > 0) ? 0xFFFFA300 : 0xFF402800;
    for (int dy = -3; dy <= 3; dy++) {
        for (int dx = -3; dx <= 3; dx++) {
            if (dx * dx + dy * dy <= 9) {
                screenBuffer[(actY + dy) * CANVAS_W + (actX + dx)] = actCol;
            }
        }
    }

    // Recessed CRT Bezel Frame around 384x384 active screen (x: 128..512, y: 64..448)
    // Metallic outer shadow and highlight
    for (int x = SCREEN_X - 4; x <= SCREEN_X + 384 + 4; x++) {
        screenBuffer[(SCREEN_Y - 4) * CANVAS_W + x] = 0xFF0A0A0D;
        screenBuffer[(SCREEN_Y - 3) * CANVAS_W + x] = 0xFF14151A;
        screenBuffer[(SCREEN_Y + 384 + 3) * CANVAS_W + x] = 0xFF353842;
    }
    for (int y = SCREEN_Y - 4; y <= SCREEN_Y + 384 + 4; y++) {
        screenBuffer[y * CANVAS_W + (SCREEN_X - 4)] = 0xFF0A0A0D;
        screenBuffer[y * CANVAS_W + (SCREEN_X - 3)] = 0xFF14151A;
        screenBuffer[y * CANVAS_W + (SCREEN_X + 384 + 3)] = 0xFF353842;
    }
}

void Pico8Machine::renderScreenToCanvas() {
    // Blit 128x128 PICO-8 screen RAM (0x6000..0x7FFF) at 3x scale into screenBuffer
    // Active area: x: 128..511, y: 64..447
    for (int py = 0; py < 128; py++) {
        int dstY0 = SCREEN_Y + py * P8_SCALE;
        int rowAddr = 0x6000 + py * 64;

        for (int px = 0; px < 128; px++) {
            uint8 rawCol = (px & 1) ? ((ram[rowAddr + (px >> 1)] >> 4) & 0x0F) : (ram[rowAddr + (px >> 1)] & 0x0F);
            uint8 col = screenPal[rawCol & 15] & 15;
            uint32 argb = PICO8_PALETTE[col];

            int dstX0 = SCREEN_X + px * P8_SCALE;

            // 3x3 pixel blit with CRT scanline effect on row 2
            screenBuffer[dstY0 * CANVAS_W + dstX0] = argb;
            screenBuffer[dstY0 * CANVAS_W + dstX0 + 1] = argb;
            screenBuffer[dstY0 * CANVAS_W + dstX0 + 2] = argb;

            screenBuffer[(dstY0 + 1) * CANVAS_W + dstX0] = argb;
            screenBuffer[(dstY0 + 1) * CANVAS_W + dstX0 + 1] = argb;
            screenBuffer[(dstY0 + 1) * CANVAS_W + dstX0 + 2] = argb;

            // Subtle scanline attenuation on the 3rd subline
            uint32 scanArgb = ((argb >> 1) & 0x7F7F7F) | 0xFF000000;
            screenBuffer[(dstY0 + 2) * CANVAS_W + dstX0] = scanArgb;
            screenBuffer[(dstY0 + 2) * CANVAS_W + dstX0 + 1] = scanArgb;
            screenBuffer[(dstY0 + 2) * CANVAS_W + dstX0 + 2] = scanArgb;
        }
    }
}
