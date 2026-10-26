#include "camputers_lynx_machine.h"
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

CamputersLynxMachine::CamputersLynxMachine() {
    screenBuffer.resize(CANVAS_W * CANVAS_H, 0xFF000000);
    audioBuffer.resize(882, 0);
    init();
}

void CamputersLynxMachine::init() {
    std::memcpy(rom.data(), EmbeddedROMs::CAMPUTERS_LYNX_ROM, 16384);
    ram.fill(0x00);

    for (int i = 0; i < 10; i++) keyMatrix[i] = 0xFF;

    cpu.memRead = [this](uint16 addr) -> uint8 { return memRead(addr); };
    cpu.memWrite = [this](uint16 addr, uint8 val) { memWrite(addr, val); };
    cpu.ioRead = [this](uint16 port) -> uint8 { return ioRead(port); };
    cpu.ioWrite = [this](uint16 port, uint8 val) { ioWrite(port, val); };

    reset();
}

void CamputersLynxMachine::reset() {
    cpu.reset();
    romEnabled = true;
    for (int i = 0; i < 10; i++) keyMatrix[i] = 0xFF;
    soundDac = 0;
    soundTimer = 0;
    animTick = 0;
    cursorBlink = 0;
    currentInput.clear();
    storedProgram.clear();
    numVars.clear();
    basicLines.clear();

    basicLines.push_back("*** CAMPUTERS LYNX 48K ***");
    basicLines.push_back("(C) 1983 CAMPUTERS LTD - 48K RAM");
    basicLines.push_back("");
    basicLines.push_back("Ready!");
}

uint8 CamputersLynxMachine::memRead(uint16 addr) {
    if (addr < 0x4000 && romEnabled) return rom[addr];
    return ram[addr];
}

void CamputersLynxMachine::memWrite(uint16 addr, uint8 val) {
    if (addr < 0x4000 && romEnabled) return;
    ram[addr] = val;
}

uint8 CamputersLynxMachine::ioRead(uint16 port) {
    uint8 high = (port >> 8) & 0xFF;
    uint8 result = 0xFF;
    for (int row = 0; row < 10; row++) {
        if (!(high & (1 << row))) result &= keyMatrix[row];
    }
    return result;
}

void CamputersLynxMachine::ioWrite(uint16 port, uint8 val) {
    uint8 low = port & 0xFF;
    if (low == 0x80) romEnabled = (val & 0x01) == 0;
    else if (low == 0x84) soundDac = val & 0x3F;
}

void CamputersLynxMachine::playTone(int duration) {
    soundTimer = duration;
}

double CamputersLynxMachine::evaluateExpr(const std::string& expr) {
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

void CamputersLynxMachine::executeStatement(const std::string& stmt, bool& shouldStop, int& jumpToLine) {
    std::string s = stmt;
    while (!s.empty() && s.front() == ' ') s.erase(0, 1);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    if (s.empty()) return;

    std::string upper = s;
    for (char& c : upper) c = (char)toupper(c);

    if (upper == "NEW") {
        storedProgram.clear();
        numVars.clear();
        basicLines.clear();
        basicLines.push_back("Ready!");
    } else if (upper == "LIST") {
        for (const auto& [lNum, text] : storedProgram) {
            basicLines.push_back(std::to_string(lNum) + " " + text);
        }
        basicLines.push_back("Ready!");
    } else if (upper == "RUN") {
        executeProgram();
    } else if (upper.rfind("PRINT", 0) == 0) {
        std::string rest = s.substr(5);
        while (!rest.empty() && rest.front() == ' ') rest.erase(0, 1);
        if (rest.empty()) {
            basicLines.push_back("");
        } else if (rest.front() == '"' && rest.back() == '"' && rest.length() >= 2) {
            basicLines.push_back(rest.substr(1, rest.length() - 2));
        } else {
            double val = evaluateExpr(rest);
            char buf[32];
            if (std::abs(val - (long)val) < 1e-6) snprintf(buf, sizeof(buf), "%ld", (long)val);
            else snprintf(buf, sizeof(buf), "%g", val);
            basicLines.push_back(buf);
        }
    } else if (upper.rfind("GOTO", 0) == 0) {
        jumpToLine = std::stoi(upper.substr(4));
    } else if (upper.rfind("SOUND", 0) == 0 || upper.rfind("BEEP", 0) == 0) {
        playTone(15);
    } else if (upper.rfind("CLS", 0) == 0) {
        basicLines.clear();
    } else if (upper.find('=') != std::string::npos) {
        std::string assign = upper;
        if (assign.rfind("LET ", 0) == 0) assign = assign.substr(4);
        size_t eq = assign.find('=');
        std::string varName = assign.substr(0, eq);
        while (!varName.empty() && varName.back() == ' ') varName.pop_back();
        while (!varName.empty() && varName.front() == ' ') varName.erase(0, 1);
        numVars[varName] = evaluateExpr(assign.substr(eq + 1));
    } else {
        basicLines.push_back("Error");
    }
}

void CamputersLynxMachine::executeProgram() {
    if (storedProgram.empty()) {
        basicLines.push_back("Ready!");
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
                basicLines.push_back("Line not found");
                break;
            }
        } else {
            ++it;
        }
    }
    basicLines.push_back("Ready!");
}

void CamputersLynxMachine::executeBasicLine(const std::string& line) {
    std::string s = line;
    while (!s.empty() && s.front() == ' ') s.erase(0, 1);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    if (s.empty()) return;

    basicLines.push_back(s);
    if (isdigit(s[0])) {
        size_t sp = s.find(' ');
        int lineNum = std::stoi(s.substr(0, sp));
        if (sp == std::string::npos) storedProgram.erase(lineNum);
        else storedProgram[lineNum] = s.substr(sp + 1);
        return;
    }

    bool stop = false;
    int jump = -1;
    executeStatement(s, stop, jump);
}

void CamputersLynxMachine::runFrame() {
    animTick++;
    cursorBlink = (animTick / 15) % 2;

    if (soundTimer > 0) soundTimer--;

    int cycles = 0;
    while (cycles < 10000) {
        cycles += cpu.step();
    }

    renderLynxDisplay();
    updateAudio();
}

void CamputersLynxMachine::renderLynxDisplay() {
    // Fill canvas background with Camputers Lynx dark border
    std::fill(screenBuffer.begin(), screenBuffer.end(), 0xFF121212);

    const int startX = 64;
    const int startY = 32;
    const int dispW = 512;
    const int dispH = 448;

    // Active screen area
    for (int y = startY; y < startY + dispH; y++) {
        for (int x = startX; x < startX + dispW; x++) {
            screenBuffer[y * CANVAS_W + x] = 0xFF000000;
        }
    }

    const uint32 colorHeader = 0xFF00FFFF; // Bright Cyan
    const uint32 colorText = 0xFF00FF00;   // Bright Green
    const int pitch = CANVAS_W * 4;

    int maxLines = 23;
    int firstLine = (int)basicLines.size() > maxLines ? (int)basicLines.size() - maxLines : 0;
    int yPos = startY + 8;

    for (size_t i = firstLine; i < basicLines.size(); i++) {
        uint32 fg = (i == 0) ? colorHeader : colorText;
        GUIFont::drawString(screenBuffer.data(), pitch, startX + 16, yPos, basicLines[i], fg);
        yPos += 18;
    }

    // Input prompt with blinking cyan block cursor
    std::string prompt = " > " + currentInput;
    GUIFont::drawString(screenBuffer.data(), pitch, startX + 16, yPos, prompt, colorHeader);

    if (cursorBlink) {
        int cx = startX + 16 + (int)prompt.length() * 8;
        for (int py = yPos; py < yPos + 14; py++) {
            for (int px = cx; px < cx + 8; px++) {
                if (px < CANVAS_W && py < CANVAS_H) {
                    screenBuffer[py * CANVAS_W + px] = colorHeader;
                }
            }
        }
    }
}

void CamputersLynxMachine::updateAudio() {
    int16 sample = (soundTimer > 0) ? 6000 : 0;
    for (size_t i = 0; i < audioBuffer.size(); i++) {
        audioBuffer[i] = (i % 64 < 32) ? sample : -sample;
    }
}

void CamputersLynxMachine::handleKeyDown(SDL_Keycode key, uint16 mod) {
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        playTone(3);
        executeBasicLine(currentInput);
        currentInput.clear();
    } else if (key == SDLK_BACKSPACE) {
        playTone(2);
        if (!currentInput.empty()) currentInput.pop_back();
    } else {
        char ch = translateSDLKey(key, mod);
        if (ch != 0) {
            playTone(2);
            if (ch >= 'a' && ch <= 'z') ch = (char)toupper(ch);
            currentInput.push_back(ch);
        }
    }
}

void CamputersLynxMachine::handleKeyUp(SDL_Keycode key, uint16 mod) {
    (void)key; (void)mod;
}

bool CamputersLynxMachine::loadMedia(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;

    std::vector<uint8> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (buffer.empty()) return false;

    currentMedia = path.substr(path.find_last_of("/\\") + 1);
    basicLines.push_back("TAPE LOADED: " + currentMedia);
    basicLines.push_back("Ready!");
    std::cout << "[CAMPUTERS LYNX] Loaded tape file: " << currentMedia << " (" << buffer.size() << " bytes)\n";
    return true;
}
