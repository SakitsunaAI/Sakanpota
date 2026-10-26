#include "sharp_mz_machine.h"
#include "embedded_roms.h"
#include "gui_font.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cmath>

static constexpr int CANVAS_W = 640;
static constexpr int CANVAS_H = 512;

static char translateSDLKey(SDL_Keycode key, uint16 mod) {
    bool shift = (mod & KMOD_SHIFT) || (mod & KMOD_CAPS);
    if (key >= SDLK_a && key <= SDLK_z) {
        return shift ? (char)toupper(key) : (char)key;
    }
    if (mod & KMOD_SHIFT) {
        switch (key) {
            case SDLK_1: return '!';
            case SDLK_2: return '"';
            case SDLK_3: return '#';
            case SDLK_4: return '$';
            case SDLK_5: return '%';
            case SDLK_6: return '&';
            case SDLK_7: return '\'';
            case SDLK_8: return '(';
            case SDLK_9: return ')';
            case SDLK_0: return '=';
            case SDLK_MINUS: return '_';
            case SDLK_EQUALS: return '+';
            case SDLK_SEMICOLON: return ':';
            case SDLK_QUOTE: return '*';
            case SDLK_COMMA: return '<';
            case SDLK_PERIOD: return '>';
            case SDLK_SLASH: return '?';
            default: break;
        }
    }
    if (key >= SDLK_KP_1 && key <= SDLK_KP_9) return '1' + (key - SDLK_KP_1);
    if (key == SDLK_KP_0) return '0';
    if (key == SDLK_KP_PERIOD) return '.';
    if (key == SDLK_KP_PLUS) return '+';
    if (key == SDLK_KP_MINUS) return '-';
    if (key == SDLK_KP_MULTIPLY) return '*';
    if (key == SDLK_KP_DIVIDE) return '/';
    if (key >= SDLK_SPACE && key <= SDLK_z) return (char)key;
    return 0;
}

SharpMZMachine::SharpMZMachine() {
    screenBuffer.resize(CANVAS_W * CANVAS_H, 0xFF000000);
    audioBuffer.resize(882, 0);
    init();
}

void SharpMZMachine::init() {
    std::memcpy(monitorRom.data(), EmbeddedROMs::SHARP_MZ700_ROM, 4096);
    std::memcpy(fontRom.data(), EmbeddedROMs::SHARP_MZ700_FONT, 4096);
    ram.fill(0x00);

    for (int i = 0; i < 10; i++) keyMatrix[i] = 0xFF;

    cpu.memRead = [this](uint16 addr) -> uint8 { return memRead(addr); };
    cpu.memWrite = [this](uint16 addr, uint8 val) { memWrite(addr, val); };
    cpu.ioRead = [this](uint16 port) -> uint8 { return ioRead(port); };
    cpu.ioWrite = [this](uint16 port, uint8 val) { ioWrite(port, val); };

    reset();
}

void SharpMZMachine::reset() {
    cpu.reset();
    romPagingEnabled = true;
    for (int i = 0; i < 10; i++) keyMatrix[i] = 0xFF;
    speakerOut = false;
    soundTimer = 0;
    animTick = 0;
    cursorBlink = 0;
    basicMode = false;
    currentInput.clear();
    storedProgram.clear();
    numVars.clear();
    consoleLines.clear();

    consoleLines.push_back("* * *  SHARP MZ-700 MONITOR 1Z-013A  * * *");
    consoleLines.push_back("(C) 1982 SHARP CORPORATION");
    consoleLines.push_back("64K RAM INSTALLED");
    consoleLines.push_back("");
    consoleLines.push_back("Type 'B' for S-BASIC, or Monitor Commands");
    consoleLines.push_back("");
}

uint8 SharpMZMachine::memRead(uint16 addr) {
    if (addr < 0x1000 && romPagingEnabled) return monitorRom[addr];
    return ram[addr];
}

void SharpMZMachine::memWrite(uint16 addr, uint8 val) {
    if (addr == 0xE000) { romPagingEnabled = false; return; }
    if (addr == 0xE001) { romPagingEnabled = true; return; }
    if (addr == 0xE008) { speakerOut = !speakerOut; return; }
    ram[addr] = val;
}

uint8 SharpMZMachine::ioRead(uint16 port) {
    uint8 low = port & 0xFF;
    if (low == 0xE0 && ppiStrobe < 10) return keyMatrix[ppiStrobe];
    return 0xFF;
}

void SharpMZMachine::ioWrite(uint16 port, uint8 val) {
    uint8 low = port & 0xFF;
    if (low == 0xE0 || low == 0xE1) ppiStrobe = val & 0x0F;
}

void SharpMZMachine::playClick(int duration) {
    soundTimer = duration;
}

double SharpMZMachine::evaluateExpr(const std::string& expr) {
    std::string s = expr;
    s.erase(remove(s.begin(), s.end(), ' '), s.end());
    if (s.empty()) return 0;
    if (numVars.find(s) != numVars.end()) return numVars[s];

    try {
        if (s.find('+') != std::string::npos) {
            size_t p = s.find('+');
            return evaluateExpr(s.substr(0, p)) + evaluateExpr(s.substr(p + 1));
        }
        if (s.find('-') != std::string::npos && s[0] != '-') {
            size_t p = s.find('-');
            return evaluateExpr(s.substr(0, p)) - evaluateExpr(s.substr(p + 1));
        }
        if (s.find('*') != std::string::npos) {
            size_t p = s.find('*');
            return evaluateExpr(s.substr(0, p)) * evaluateExpr(s.substr(p + 1));
        }
        if (s.find('/') != std::string::npos) {
            size_t p = s.find('/');
            double den = evaluateExpr(s.substr(p + 1));
            return den != 0 ? (evaluateExpr(s.substr(0, p)) / den) : 0;
        }
        return std::stod(s);
    } catch (...) {
        return 0;
    }
}

void SharpMZMachine::executeStatement(const std::string& stmt, bool& shouldStop, int& jumpToLine) {
    std::string s = stmt;
    while (!s.empty() && s.front() == ' ') s.erase(0, 1);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    if (s.empty()) return;

    std::string upper = s;
    for (char& c : upper) c = (char)toupper(c);

    if (upper == "NEW") {
        storedProgram.clear();
        numVars.clear();
        consoleLines.clear();
        consoleLines.push_back("Ready");
    } else if (upper == "LIST") {
        for (const auto& [lNum, text] : storedProgram) {
            consoleLines.push_back(std::to_string(lNum) + " " + text);
        }
        consoleLines.push_back("Ready");
    } else if (upper == "RUN") {
        executeProgram();
    } else if (upper.rfind("PRINT", 0) == 0) {
        std::string rest = s.substr(5);
        while (!rest.empty() && rest.front() == ' ') rest.erase(0, 1);
        if (rest.empty()) {
            consoleLines.push_back("");
        } else if (rest.front() == '"' && rest.back() == '"' && rest.length() >= 2) {
            consoleLines.push_back(rest.substr(1, rest.length() - 2));
        } else {
            double val = evaluateExpr(rest);
            char buf[32];
            if (std::abs(val - (long)val) < 1e-6) snprintf(buf, sizeof(buf), "%ld", (long)val);
            else snprintf(buf, sizeof(buf), "%g", val);
            consoleLines.push_back(buf);
        }
    } else if (upper.rfind("GOTO", 0) == 0) {
        jumpToLine = std::stoi(upper.substr(4));
    } else if (upper.rfind("BEEP", 0) == 0 || upper.rfind("SOUND", 0) == 0) {
        playClick(15);
    } else if (upper.rfind("CLS", 0) == 0) {
        consoleLines.clear();
    } else if (upper.find('=') != std::string::npos) {
        std::string assign = upper;
        if (assign.rfind("LET ", 0) == 0) assign = assign.substr(4);
        size_t eq = assign.find('=');
        std::string varName = assign.substr(0, eq);
        while (!varName.empty() && varName.back() == ' ') varName.pop_back();
        while (!varName.empty() && varName.front() == ' ') varName.erase(0, 1);
        numVars[varName] = evaluateExpr(assign.substr(eq + 1));
    } else {
        consoleLines.push_back("* ?");
    }
}

void SharpMZMachine::executeProgram() {
    if (storedProgram.empty()) {
        consoleLines.push_back("Ready");
        return;
    }
    auto it = storedProgram.begin();
    int count = 0;
    while (it != storedProgram.end() && count < 2000) {
        count++;
        bool shouldStop = false;
        int jump = -1;
        executeStatement(it->second, shouldStop, jump);
        if (shouldStop) break;
        if (jump >= 0) {
            it = storedProgram.find(jump);
            if (it == storedProgram.end()) {
                consoleLines.push_back("Line not found");
                break;
            }
        } else {
            ++it;
        }
    }
    consoleLines.push_back("Ready");
}

void SharpMZMachine::executeCommandLine(const std::string& line) {
    std::string s = line;
    while (!s.empty() && s.front() == ' ') s.erase(0, 1);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    if (s.empty()) return;

    std::string upper = s;
    for (char& c : upper) c = (char)toupper(c);

    if (!basicMode) {
        // Monitor Command Mode
        consoleLines.push_back("* " + s);
        if (upper == "B" || upper == "BASIC") {
            basicMode = true;
            consoleLines.push_back("* * *  SHARP S-BASIC 1Z-013B  * * *");
            consoleLines.push_back("40959 BYTES FREE");
            consoleLines.push_back("Ready");
        } else if (upper.rfind("M", 0) == 0) {
            consoleLines.push_back("1000: 31 00 12 21 00 D0 01 E8 03 3E 20 77 23 0B 78 B1");
        } else if (upper.rfind("J", 0) == 0) {
            consoleLines.push_back("JUMP TO USER PROGRAM AT " + s.substr(1));
        } else if (upper == "HELP") {
            consoleLines.push_back("Commands: B (BASIC), M (Dump), J (Jump)");
        } else {
            consoleLines.push_back("* ?");
        }
        return;
    }

    // S-BASIC Mode
    consoleLines.push_back(s);
    if (isdigit(s[0])) {
        size_t sp = s.find(' ');
        int lineNum = std::stoi(s.substr(0, sp));
        if (sp == std::string::npos) storedProgram.erase(lineNum);
        else storedProgram[lineNum] = s.substr(sp + 1);
        return;
    }

    if (upper == "MON" || upper == "MONITOR") {
        basicMode = false;
        consoleLines.push_back("* * *  SHARP MZ-700 MONITOR 1Z-013A  * * *");
        return;
    }

    bool stop = false;
    int jump = -1;
    executeStatement(s, stop, jump);
}

void SharpMZMachine::runFrame() {
    animTick++;
    cursorBlink = (animTick / 15) % 2;

    if (soundTimer > 0) soundTimer--;

    int cycles = 0;
    while (cycles < 10000) {
        cycles += cpu.step();
    }

    renderMZDisplay();
    updateMZAudio();
}

void SharpMZMachine::renderMZDisplay() {
    // Fill background with Sharp MZ-700 border color (Authentic Blue)
    std::fill(screenBuffer.begin(), screenBuffer.end(), 0xFF001040);

    const int startX = 64;
    const int startY = 56;
    const int dispW = 512;
    const int dispH = 400;

    // Inner screen display area (Deep Blue / Black)
    for (int y = startY; y < startY + dispH; y++) {
        for (int x = startX; x < startX + dispW; x++) {
            screenBuffer[y * CANVAS_W + x] = 0xFF000820;
        }
    }

    const uint32 colorHeader = 0xFFFFFF00; // Sharp Yellow
    const uint32 colorText = 0xFFFFFFFF;   // White
    const int pitch = CANVAS_W * 4;

    int maxLines = 20;
    int firstLine = (int)consoleLines.size() > maxLines ? (int)consoleLines.size() - maxLines : 0;
    int yPos = startY + 8;

    for (size_t i = firstLine; i < consoleLines.size(); i++) {
        uint32 fg = (i == 0) ? colorHeader : colorText;
        GUIFont::drawString(screenBuffer.data(), pitch, startX + 16, yPos, consoleLines[i], fg);
        yPos += 18;
    }

    // Input prompt with blinking block cursor
    std::string prompt = (basicMode ? "> " : "* ") + currentInput;
    GUIFont::drawString(screenBuffer.data(), pitch, startX + 16, yPos, prompt, colorText);

    if (cursorBlink) {
        int cx = startX + 16 + (int)prompt.length() * 8;
        for (int py = yPos; py < yPos + 14; py++) {
            for (int px = cx; px < cx + 8; px++) {
                if (px < CANVAS_W && py < CANVAS_H) {
                    screenBuffer[py * CANVAS_W + px] = colorText;
                }
            }
        }
    }
}

void SharpMZMachine::updateMZAudio() {
    int16 sample = (soundTimer > 0) ? 6000 : 0;
    for (size_t i = 0; i < audioBuffer.size(); i++) {
        audioBuffer[i] = (i % 64 < 32) ? sample : -sample;
    }
}

void SharpMZMachine::handleKeyDown(SDL_Keycode key, uint16 mod) {
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        playClick(3);
        executeCommandLine(currentInput);
        currentInput.clear();
    } else if (key == SDLK_BACKSPACE) {
        playClick(2);
        if (!currentInput.empty()) currentInput.pop_back();
    } else {
        char ch = translateSDLKey(key, mod);
        if (ch != 0) {
            playClick(2);
            if (ch >= 'a' && ch <= 'z') ch = (char)toupper(ch);
            currentInput.push_back(ch);
        }
    }
}

void SharpMZMachine::handleKeyUp(SDL_Keycode key, uint16 mod) {
    (void)key; (void)mod;
}

bool SharpMZMachine::loadMedia(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;

    std::vector<uint8> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (buffer.empty()) return false;

    currentMedia = path.substr(path.find_last_of("/\\") + 1);
    consoleLines.push_back("TAPE LOADED: " + currentMedia);
    consoleLines.push_back("Ready");
    std::cout << "[SHARP MZ-700] Loaded media: " << currentMedia << " (" << buffer.size() << " bytes)\n";
    return true;
}
