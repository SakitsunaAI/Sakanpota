#include "ti_machine.h"
#include "embedded_roms.h"
#include "gui_font.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <cmath>
#include <sstream>
#include <algorithm>

static constexpr int CANVAS_W = 640;
static constexpr int CANVAS_H = 512;

// TMS9918A Authentic 16-color Palette
static const uint32 tms9918Palette[16] = {
    0xFF000000, // 0: Transparent/Black
    0xFF000000, // 1: Black
    0xFF24C424, // 2: Medium Green
    0xFF5AE65A, // 3: Light Green
    0xFF2424E6, // 4: Dark Blue
    0xFF4A76FF, // 5: Light Blue
    0xFFB42424, // 6: Dark Red
    0xFF008899, // 7: Cyan
    0xFFE62424, // 8: Medium Red
    0xFFFF6464, // 9: Light Red
    0xFFC8C824, // 10: Dark Yellow
    0xFFE6E65A, // 11: Light Yellow
    0xFF188018, // 12: Dark Green
    0xFFD030C8, // 13: Magenta
    0xFFA0A0A0, // 14: Gray
    0xFFFFFFFF  // 15: White
};

static char translateSDLKey(SDL_Keycode key, uint16 mod) {
    bool shift = (mod & KMOD_SHIFT) || (mod & KMOD_CAPS);
    if (key >= SDLK_a && key <= SDLK_z) {
        return shift ? (char)toupper(key) : (char)key;
    }
    if (mod & KMOD_SHIFT) {
        switch (key) {
            case SDLK_1: return '!';
            case SDLK_2: return '@';
            case SDLK_3: return '#';
            case SDLK_4: return '$';
            case SDLK_5: return '%';
            case SDLK_6: return '^';
            case SDLK_7: return '&';
            case SDLK_8: return '*';
            case SDLK_9: return '(';
            case SDLK_0: return ')';
            case SDLK_MINUS: return '_';
            case SDLK_EQUALS: return '+';
            case SDLK_SEMICOLON: return ':';
            case SDLK_QUOTE: return '"';
            case SDLK_COMMA: return '<';
            case SDLK_PERIOD: return '>';
            case SDLK_SLASH: return '?';
            case SDLK_BACKQUOTE: return '~';
            case SDLK_LEFTBRACKET: return '{';
            case SDLK_RIGHTBRACKET: return '}';
            case SDLK_BACKSLASH: return '|';
            default: break;
        }
    }
    if (key >= SDLK_KP_1 && key <= SDLK_KP_9) {
        return '1' + (key - SDLK_KP_1);
    }
    if (key == SDLK_KP_0) return '0';
    if (key == SDLK_KP_PERIOD) return '.';
    if (key == SDLK_KP_PLUS) return '+';
    if (key == SDLK_KP_MINUS) return '-';
    if (key == SDLK_KP_MULTIPLY) return '*';
    if (key == SDLK_KP_DIVIDE) return '/';
    if (key >= SDLK_SPACE && key <= SDLK_z) {
        return (char)key;
    }
    return 0;
}

TexasInstrumentsMachine::TexasInstrumentsMachine(TIMode mode)
    : currentMode(mode) {
    screenBuffer.resize(CANVAS_W * CANVAS_H, 0xFF000000);
    audioBuffer.resize(882, 0);

    std::memcpy(systemRom.data(), EmbeddedROMs::TI99_SYSTEM_ROM, sizeof(systemRom));
    std::memcpy(grom.data(), EmbeddedROMs::TI99_SYSTEM_GROM, sizeof(grom));
    calcFlash.assign(EmbeddedROMs::TI83P_ROM, EmbeddedROMs::TI83P_ROM + sizeof(EmbeddedROMs::TI83P_ROM));
}

std::string TexasInstrumentsMachine::getSystemName() const {
    return (currentMode == TIMode::TI99_4A) ? "Texas Instruments TI-99/4A" : "TI-83 Plus Graphing Calculator";
}

void TexasInstrumentsMachine::init() {
    reset();
}

void TexasInstrumentsMachine::reset() {
    animTick = 0;
    cursorBlink = 0;
    soundTimer = 0;
    ti99State = 0;
    screenBgColor = 7; // Cyan
    basicLines.clear();
    currentInput.clear();
    storedProgram.clear();
    numVars.clear();
    strVars.clear();
    scratchpad.fill(0);
    vdpRam.fill(0);
    calcRam.fill(0);
    calcLcd.fill(0);
    calcExpression = "";
    calcAnswer = "";
    calcGraphMode = false;

    basicLines.push_back("   TI BASIC READY");
    basicLines.push_back("");

    // Startup Chime for TI-99/4A
    if (currentMode == TIMode::TI99_4A) {
        playTone(440.0f, 150, 0);
    }
}

void TexasInstrumentsMachine::setMode(TIMode mode) {
    currentMode = mode;
    reset();
}

void TexasInstrumentsMachine::playTone(float freqHz, int durationMs, int volume) {
    if (freqHz <= 20.0f) return;
    int period = (int)(4000000.0 / (32.0 * freqHz));
    if (period < 1) period = 1;
    if (period > 1023) period = 1023;

    // Latch channel 0 frequency low 4 bits
    soundChip.write(0x80 | (period & 0x0F));
    // Data byte: high 6 bits
    soundChip.write((period >> 4) & 0x3F);
    // Channel 0 volume
    soundChip.write(0x90 | (volume & 0x0F));

    soundTimer = std::max(1, durationMs * 50 / 1000);
}

void TexasInstrumentsMachine::runFrame() {
    animTick++;
    cursorBlink = (animTick / 20) % 2;

    if (soundTimer > 0) {
        soundTimer--;
        if (soundTimer == 0) {
            soundChip.write(0x9F); // Mute channel 0
        }
    }

    if (currentMode == TIMode::TI99_4A) {
        renderTI99Screen();
    } else {
        renderTI83Screen();
    }
    updateAudio();
}

void TexasInstrumentsMachine::renderTI99Screen() {
    // Fill TMS9918A Border
    uint32 borderCol = 0xFF004455;
    std::fill(screenBuffer.begin(), screenBuffer.end(), borderCol);

    const int tvX = 64;
    const int tvY = 64;
    const int tvW = 512;
    const int tvH = 384;

    // Active Display Area (256x192 2x scaled to 512x384)
    uint32 bgColor = (ti99State == 0) ? 0xFF003344 : tms9918Palette[screenBgColor & 0x0F];
    for (int y = tvY; y < tvY + tvH; y++) {
        for (int x = tvX; x < tvX + tvW; x++) {
            screenBuffer[y * CANVAS_W + x] = bgColor;
        }
    }

    const int pitch = CANVAS_W * 4;

    if (ti99State == 0) {
        // --- State 0: Famous TI-99/4A Splash Screen ---
        GUIFont::drawString(screenBuffer.data(), pitch, tvX + 110, tvY + 60,
                            "TEXAS  INSTRUMENTS", 0xFFFFFFFF);
        GUIFont::drawString(screenBuffer.data(), pitch, tvX + 130, tvY + 80,
                            "HOME  COMPUTER", 0xFFFFFFFF);

        // Multi-Color Rainbow Bar
        for (int x = tvX + 40; x < tvX + tvW - 40; x++) {
            for (int y = tvY + 120; y < tvY + 128; y++) {
                uint32 barCol = 0xFFE62424; // Red
                if (x > tvX + 140) barCol = 0xFFE6E65A; // Yellow
                if (x > tvX + 240) barCol = 0xFF24C424; // Green
                if (x > tvX + 340) barCol = 0xFF4A76FF; // Blue
                screenBuffer[y * CANVAS_W + x] = barCol;
            }
        }

        // Blinking Prompt
        if (cursorBlink) {
            GUIFont::drawString(screenBuffer.data(), pitch, tvX + 60, tvY + 200,
                                "READY-PRESS ANY KEY TO BEGIN", 0xFFFFFFFF);
        }

        GUIFont::drawString(screenBuffer.data(), pitch, tvX + 90, tvY + 320,
                            "(C) 1981  TEXAS INSTRUMENTS", 0xFFE6E65A);

    } else if (ti99State == 1) {
        // --- State 1: Selection Menu ---
        GUIFont::drawString(screenBuffer.data(), pitch, tvX + 60, tvY + 40,
                            "********************************", 0xFFFFFFFF);
        GUIFont::drawString(screenBuffer.data(), pitch, tvX + 60, tvY + 60,
                            "*      TI-99/4A COMPUTER       *", 0xFFFFFFFF);
        GUIFont::drawString(screenBuffer.data(), pitch, tvX + 60, tvY + 80,
                            "********************************", 0xFFFFFFFF);

        GUIFont::drawString(screenBuffer.data(), pitch, tvX + 60, tvY + 140,
                            "PRESS", 0xFFE6E65A);
        GUIFont::drawString(screenBuffer.data(), pitch, tvX + 80, tvY + 170,
                            "1  FOR TI BASIC", 0xFFFFFFFF);
        GUIFont::drawString(screenBuffer.data(), pitch, tvX + 80, tvY + 200,
                            "2  FOR TI-83 GRAPHING CALCULATOR", 0xFFFFFFFF);

        GUIFont::drawString(screenBuffer.data(), pitch, tvX + 60, tvY + 300,
                            "SELECT 1 OR 2 TO PROCEED", 0xFFE6E65A);

    } else {
        // --- State 2: Interactive TI BASIC ---
        int startY = tvY + 16;
        int maxLines = 18;
        int firstLine = (int)basicLines.size() > maxLines ? (int)basicLines.size() - maxLines : 0;

        for (size_t i = firstLine; i < basicLines.size(); i++) {
            GUIFont::drawString(screenBuffer.data(), pitch, tvX + 16, startY,
                                basicLines[i], 0xFF000000);
            startY += 18;
        }

        // Current Input Line with Cursor
        std::string promptLine = " > " + currentInput;
        GUIFont::drawString(screenBuffer.data(), pitch, tvX + 16, startY, promptLine, 0xFF000000);

        if (cursorBlink) {
            int cx = tvX + 16 + (int)promptLine.length() * 8;
            for (int y = startY; y < startY + 14; y++) {
                for (int x = cx; x < cx + 8; x++) {
                    screenBuffer[y * CANVAS_W + x] = 0xFF000000;
                }
            }
        }
    }
}

void TexasInstrumentsMachine::renderTI83Screen() {
    std::fill(screenBuffer.begin(), screenBuffer.end(), 0xFF181C20);
    const int pitch = CANVAS_W * 4;

    const int caseX = 80;
    const int caseY = 16;
    const int caseW = 480;
    const int caseH = 480;

    for (int y = caseY; y < caseY + caseH; y++) {
        for (int x = caseX; x < caseX + caseW; x++) {
            screenBuffer[y * CANVAS_W + x] = 0xFF2A2E36;
        }
    }

    // Calculator Logo and Model
    GUIFont::drawString(screenBuffer.data(), pitch, caseX + 30, caseY + 12,
                        "TEXAS INSTRUMENTS", 0xFFC0C4CC);
    GUIFont::drawString(screenBuffer.data(), pitch, caseX + 320, caseY + 12,
                        "TI-83 Plus", 0xFF5090F0);

    // 96x64 Monochrome LCD Screen (Scaled 4x -> 384x256, centered at X: caseX + 48, Y: caseY + 36)
    const int lcdX = caseX + 48;
    const int lcdY = caseY + 36;
    const int lcdW = 384;
    const int lcdH = 256;

    for (int y = lcdY; y < lcdY + lcdH; y++) {
        for (int x = lcdX; x < lcdX + lcdW; x++) {
            screenBuffer[y * CANVAS_W + x] = 0xFF96A686;
        }
    }

    if (!calcGraphMode) {
        // Calculation Mode
        GUIFont::drawString(screenBuffer.data(), pitch, lcdX + 8, lcdY + 12,
                            "TI-83 Plus Graphing OS", 0xFF142010);
        GUIFont::drawString(screenBuffer.data(), pitch, lcdX + 8, lcdY + 32,
                            "Type expressions & press Enter:", 0xFF142010);

        std::string exprPrompt = ":" + calcExpression;
        GUIFont::drawString(screenBuffer.data(), pitch, lcdX + 8, lcdY + 70, exprPrompt, 0xFF142010);

        if (cursorBlink) {
            int cx = lcdX + 8 + (int)exprPrompt.length() * 8;
            for (int y = lcdY + 70; y < lcdY + 84; y++) {
                for (int x = cx; x < cx + 7; x++) {
                    screenBuffer[y * CANVAS_W + x] = 0xFF142010;
                }
            }
        }

        if (!calcAnswer.empty()) {
            int ax = lcdX + lcdW - 16 - (int)calcAnswer.length() * 8;
            GUIFont::drawString(screenBuffer.data(), pitch, ax, lcdY + 100, calcAnswer, 0xFF142010);
        }

        GUIFont::drawString(screenBuffer.data(), pitch, lcdX + 8, lcdY + 220,
                            "[G] Graph Mode | [C] Clear | [F1] TI-99", 0xFF304428);
    } else {
        // Graphing Mode: Plot Y = sin(X) on coordinate axes
        int midY = lcdY + lcdH / 2;
        int midX = lcdX + lcdW / 2;

        for (int x = lcdX; x < lcdX + lcdW; x++) screenBuffer[midY * CANVAS_W + x] = 0xFF142010;
        for (int y = lcdY; y < lcdY + lcdH; y++) screenBuffer[y * CANVAS_W + midX] = 0xFF142010;

        for (int px = 0; px < lcdW; px++) {
            float xVal = (px - lcdW / 2) * 0.05f;
            float yVal = std::sin(xVal);
            int py = midY - (int)(yVal * 80.0f);
            if (py >= lcdY && py < lcdY + lcdH) {
                screenBuffer[py * CANVAS_W + (lcdX + px)] = 0xFF142010;
                if (py + 1 < lcdY + lcdH) screenBuffer[(py + 1) * CANVAS_W + (lcdX + px)] = 0xFF142010;
            }
        }

        GUIFont::drawString(screenBuffer.data(), pitch, lcdX + 8, lcdY + 8,
                            "Y1 = sin(X)", 0xFF142010);
        GUIFont::drawString(screenBuffer.data(), pitch, lcdX + 8, lcdY + lcdH - 18,
                            "[G] Calc Mode | [F1] TI-99", 0xFF304428);
    }

    // Function Buttons Bar under screen ([Y=] [WINDOW] [ZOOM] [TRACE] [GRAPH])
    const char* funcKeys[5] = {"Y=", "WINDOW", "ZOOM", "TRACE", "GRAPH"};
    for (int i = 0; i < 5; i++) {
        int bx = caseX + 60 + i * 75;
        int by = lcdY + lcdH + 16;
        for (int y = by; y < by + 20; y++) {
            for (int x = bx; x < bx + 55; x++) {
                screenBuffer[y * CANVAS_W + x] = 0xFF424854;
            }
        }
        GUIFont::drawString(screenBuffer.data(), pitch, bx + 6, by + 4, funcKeys[i], 0xFFFFFFFF);
    }

    // Blue [2nd] and Green [ALPHA] key badges
    for (int y = lcdY + lcdH + 46; y < lcdY + lcdH + 68; y++) {
        for (int x = caseX + 60; x < caseX + 110; x++) screenBuffer[y * CANVAS_W + x] = 0xFF3060C0; // 2nd
        for (int x = caseX + 130; x < caseX + 180; x++) screenBuffer[y * CANVAS_W + x] = 0xFF309030; // ALPHA
    }
    GUIFont::drawString(screenBuffer.data(), pitch, caseX + 70, lcdY + lcdH + 52, "2nd", 0xFFFFFFFF);
    GUIFont::drawString(screenBuffer.data(), pitch, caseX + 136, lcdY + lcdH + 52, "ALPHA", 0xFFFFFFFF);
}

void TexasInstrumentsMachine::updateAudio() {
    soundChip.generateSamples(audioBuffer.data(), audioBuffer.size());
}

double TexasInstrumentsMachine::evaluateNumericExpression(const std::string& expr) {
    std::string s = expr;
    s.erase(remove(s.begin(), s.end(), ' '), s.end());
    if (s.empty()) return 0;

    // Check variable names first
    if (numVars.find(s) != numVars.end()) {
        return numVars[s];
    }

    try {
        if (s.find('+') != std::string::npos) {
            size_t pos = s.find('+');
            return evaluateNumericExpression(s.substr(0, pos)) + evaluateNumericExpression(s.substr(pos + 1));
        }
        if (s.find('-') != std::string::npos && s[0] != '-') {
            size_t pos = s.find('-');
            return evaluateNumericExpression(s.substr(0, pos)) - evaluateNumericExpression(s.substr(pos + 1));
        }
        if (s.find('*') != std::string::npos) {
            size_t pos = s.find('*');
            return evaluateNumericExpression(s.substr(0, pos)) * evaluateNumericExpression(s.substr(pos + 1));
        }
        if (s.find('/') != std::string::npos) {
            size_t pos = s.find('/');
            double den = evaluateNumericExpression(s.substr(pos + 1));
            return (den != 0) ? (evaluateNumericExpression(s.substr(0, pos)) / den) : 0;
        }
        return std::stod(s);
    } catch (...) {
        return 0;
    }
}

void TexasInstrumentsMachine::executeStatement(const std::string& stmt, bool& shouldStop, int& jumpToLine) {
    std::string s = stmt;
    while (!s.empty() && s.front() == ' ') s.erase(0, 1);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    if (s.empty()) return;

    std::string upper = s;
    for (char& c : upper) c = (char)toupper(c);

    if (upper == "NEW") {
        storedProgram.clear();
        numVars.clear();
        strVars.clear();
        basicLines.clear();
        basicLines.push_back("   TI BASIC READY");
    } else if (upper == "OLD") {
        basicLines.push_back("READY");
    } else if (upper == "LIST") {
        for (const auto& [lNum, text] : storedProgram) {
            basicLines.push_back(std::to_string(lNum) + " " + text);
        }
        basicLines.push_back("");
    } else if (upper == "RUN") {
        executeProgram();
    } else if (upper.rfind("PRINT", 0) == 0) {
        std::string rest = s.substr(5);
        while (!rest.empty() && rest.front() == ' ') rest.erase(0, 1);

        if (rest.empty()) {
            basicLines.push_back("");
        } else if (rest.front() == '"' && rest.back() == '"' && rest.length() >= 2) {
            basicLines.push_back("  " + rest.substr(1, rest.length() - 2));
        } else {
            double val = evaluateNumericExpression(rest);
            char buf[32];
            if (std::abs(val - (long)val) < 1e-6) {
                snprintf(buf, sizeof(buf), "  %ld", (long)val);
            } else {
                snprintf(buf, sizeof(buf), "  %g", val);
            }
            basicLines.push_back(buf);
        }
    } else if (upper.rfind("GOTO", 0) == 0) {
        int target = std::stoi(upper.substr(4));
        jumpToLine = target;
    } else if (upper.rfind("CALL SOUND", 0) == 0) {
        size_t p1 = upper.find('(');
        size_t p2 = upper.find(')');
        if (p1 != std::string::npos && p2 != std::string::npos) {
            std::string args = upper.substr(p1 + 1, p2 - p1 - 1);
            int dur = 300, vol = 0;
            float freq = 440.0f;
            sscanf(args.c_str(), "%d,%f,%d", &dur, &freq, &vol);
            playTone(freq, dur, vol);
        }
    } else if (upper.rfind("CALL CLEAR", 0) == 0) {
        basicLines.clear();
    } else if (upper.rfind("CALL SCREEN", 0) == 0) {
        size_t p1 = upper.find('(');
        size_t p2 = upper.find(')');
        if (p1 != std::string::npos && p2 != std::string::npos) {
            int col = std::stoi(upper.substr(p1 + 1, p2 - p1 - 1));
            screenBgColor = (uint8)col;
        }
    } else if (upper == "TI83") {
        currentMode = TIMode::TI83_Plus;
    } else if (upper.find('=') != std::string::npos) {
        // Variable assignment: LET A = 5 or A = 5
        std::string assign = upper;
        if (assign.rfind("LET ", 0) == 0) assign = assign.substr(4);
        size_t eq = assign.find('=');
        std::string varName = assign.substr(0, eq);
        while (!varName.empty() && varName.back() == ' ') varName.pop_back();
        while (!varName.empty() && varName.front() == ' ') varName.erase(0, 1);
        std::string expr = assign.substr(eq + 1);
        double val = evaluateNumericExpression(expr);
        numVars[varName] = val;
    } else {
        basicLines.push_back(" * SYNTAX ERROR");
    }
}

void TexasInstrumentsMachine::executeProgram() {
    if (storedProgram.empty()) {
        basicLines.push_back("");
        basicLines.push_back("   TI BASIC READY");
        return;
    }

    auto it = storedProgram.begin();
    int iterations = 0;
    while (it != storedProgram.end() && iterations < 1000) {
        iterations++;
        bool shouldStop = false;
        int jumpToLine = -1;

        executeStatement(it->second, shouldStop, jumpToLine);

        if (shouldStop) break;
        if (jumpToLine >= 0) {
            it = storedProgram.find(jumpToLine);
            if (it == storedProgram.end()) {
                basicLines.push_back(" * LINE NOT FOUND IN " + std::to_string(jumpToLine));
                break;
            }
        } else {
            ++it;
        }
    }
    basicLines.push_back("");
    basicLines.push_back("   TI BASIC READY");
}

void TexasInstrumentsMachine::executeBasicCommandLine(const std::string& line) {
    std::string s = line;
    while (!s.empty() && s.front() == ' ') s.erase(0, 1);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    if (s.empty()) return;

    basicLines.push_back(" > " + s);

    // Check if line starts with a line number (Stored Program entry)
    if (isdigit(s[0])) {
        size_t spacePos = s.find(' ');
        int lineNum = std::stoi(s.substr(0, spacePos));
        if (spacePos == std::string::npos || spacePos == s.length() - 1) {
            // Delete line
            storedProgram.erase(lineNum);
        } else {
            std::string code = s.substr(spacePos + 1);
            storedProgram[lineNum] = code;
        }
        return;
    }

    // Direct command execution
    bool shouldStop = false;
    int jump = -1;
    executeStatement(s, shouldStop, jump);
}

void TexasInstrumentsMachine::evaluateCalcExpression() {
    if (calcExpression.empty()) return;

    try {
        double val = 0.0;
        char op = 0;
        double a = 0.0, b = 0.0;

        if (calcExpression.find("sin(") != std::string::npos) {
            size_t p = calcExpression.find("sin(");
            double arg = std::stod(calcExpression.substr(p + 4));
            val = std::sin(arg * 3.1415926535 / 180.0);
        } else if (calcExpression.find("cos(") != std::string::npos) {
            size_t p = calcExpression.find("cos(");
            double arg = std::stod(calcExpression.substr(p + 4));
            val = std::cos(arg * 3.1415926535 / 180.0);
        } else if (calcExpression.find("tan(") != std::string::npos) {
            size_t p = calcExpression.find("tan(");
            double arg = std::stod(calcExpression.substr(p + 4));
            val = std::tan(arg * 3.1415926535 / 180.0);
        } else if (calcExpression.find("sqrt(") != std::string::npos) {
            size_t p = calcExpression.find("sqrt(");
            double arg = std::stod(calcExpression.substr(p + 5));
            val = std::sqrt(arg);
        } else if (calcExpression.find('^') != std::string::npos) {
            sscanf(calcExpression.c_str(), "%lf^%lf", &a, &b);
            val = std::pow(a, b);
        } else if (sscanf(calcExpression.c_str(), "%lf%c%lf", &a, &op, &b) == 3) {
            if (op == '+') val = a + b;
            else if (op == '-') val = a - b;
            else if (op == '*') val = a * b;
            else if (op == '/') val = (b != 0) ? (a / b) : 0;
        } else {
            val = std::stod(calcExpression);
        }

        char resBuf[64];
        if (std::abs(val - (long)val) < 1e-7) {
            snprintf(resBuf, sizeof(resBuf), "%ld", (long)val);
        } else {
            snprintf(resBuf, sizeof(resBuf), "%.6g", val);
        }
        calcAnswer = resBuf;
    } catch (...) {
        calcAnswer = "ERR:SYNTAX";
    }
}

void TexasInstrumentsMachine::handleKeyDown(SDL_Keycode key, uint16 mod) {
    if (key == SDLK_F1) {
        setMode(TIMode::TI99_4A);
        return;
    }
    if (key == SDLK_F2) {
        setMode(TIMode::TI83_Plus);
        return;
    }

    if (currentMode == TIMode::TI99_4A) {
        if (ti99State == 0) {
            playTone(880.0f, 100, 0);
            ti99State = 1;
            return;
        }
        if (ti99State == 1) {
            if (key == SDLK_1 || key == SDLK_KP_1 || key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE) {
                playTone(660.0f, 120, 0);
                ti99State = 2;
            } else if (key == SDLK_2 || key == SDLK_KP_2) {
                setMode(TIMode::TI83_Plus);
            }
            return;
        }

        // State 2: Interactive TI BASIC
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            playTone(1000.0f, 30, 2);
            executeBasicCommandLine(currentInput);
            currentInput.clear();
        } else if (key == SDLK_BACKSPACE) {
            if (!currentInput.empty()) currentInput.pop_back();
        } else {
            char ch = translateSDLKey(key, mod);
            if (ch != 0) {
                if (ch >= 'a' && ch <= 'z') ch = (char)toupper(ch);
                currentInput.push_back(ch);
            }
        }
    } else {
        // TI-83 Plus Mode
        if (key == SDLK_g) {
            calcGraphMode = !calcGraphMode;
        } else if (key == SDLK_c) {
            calcExpression.clear();
            calcAnswer.clear();
        } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            evaluateCalcExpression();
        } else if (key == SDLK_BACKSPACE) {
            if (!calcExpression.empty()) calcExpression.pop_back();
        } else {
            char ch = translateSDLKey(key, mod);
            if (ch != 0) {
                calcExpression.push_back(ch);
            }
        }
    }
}

void TexasInstrumentsMachine::handleKeyUp(SDL_Keycode key, uint16 mod) {
    (void)key; (void)mod;
}

bool TexasInstrumentsMachine::loadMedia(const std::string& path) {
    std::string lowerPath = path;
    for (char& c : lowerPath) c = tolower(c);

    if (lowerPath.find(".83p") != std::string::npos || lowerPath.find(".8xp") != std::string::npos) {
        setMode(TIMode::TI83_Plus);
    } else {
        setMode(TIMode::TI99_4A);
        ti99State = 2; // Jump directly to BASIC
    }

    currentMedia = path.substr(path.find_last_of("/\\") + 1);
    basicLines.push_back("LOADED: " + currentMedia);
    std::cout << "[TI] Loaded Media: " << currentMedia << "\n";
    return true;
}
