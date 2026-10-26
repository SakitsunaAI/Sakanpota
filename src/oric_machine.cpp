#include "oric_machine.h"
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

OricMachine::OricMachine() {
    screenBuffer.resize(CANVAS_W * CANVAS_H, 0xFF000000);
    audioBuffer.resize(882, 0);
    init();
}

void OricMachine::init() {
    std::memcpy(rom.data(), EmbeddedROMs::ORIC_ATMOS_ROM, 16384);
    ram.fill(0x00);

    for (int i = 0; i < 8; i++) keyMatrix[i] = 0xFF;
    std::memset(viaRegs, 0, sizeof(viaRegs));
    std::memset(ayRegs, 0, sizeof(ayRegs));

    cpu.readMem = [this](uint16 a) -> uint8 { return memRead(a); };
    cpu.writeMem = [this](uint16 a, uint8 v) { memWrite(a, v); };

    reset();
}

void OricMachine::reset() {
    cpu.reset();
    for (int i = 0; i < 8; i++) keyMatrix[i] = 0xFF;
    std::memset(viaRegs, 0, sizeof(viaRegs));
    std::memset(ayRegs, 0, sizeof(ayRegs));
    soundTimer = 0;
    soundType = 0;
    animTick = 0;
    cursorBlink = 0;
    currentInput.clear();
    storedProgram.clear();
    numVars.clear();
    basicLines.clear();

    basicLines.push_back("   ORIC ATMOS 48K BASIC V1.1");
    basicLines.push_back("   (C) 1983 TANGERINE COMPUTER SYSTEMS");
    basicLines.push_back("   37631 BYTES FREE");
    basicLines.push_back("");
    basicLines.push_back("Ready");
}

uint8 OricMachine::memRead(uint16 addr) {
    if (addr < 0xC000) {
        if (addr >= 0x0300 && addr <= 0x030F) {
            uint8 reg = addr & 0x0F;
            if (reg == 0x00) {
                uint8 row = (viaRegs[0x00] >> 4) & 0x07;
                return keyMatrix[row];
            }
            if (reg == 0x01) return ayRegs[activeAyReg & 0x0F];
            return viaRegs[reg];
        }
        return ram[addr];
    }
    return rom[addr - 0xC000];
}

void OricMachine::memWrite(uint16 addr, uint8 val) {
    if (addr < 0xC000) {
        if (addr >= 0x0300 && addr <= 0x030F) {
            uint8 reg = addr & 0x0F;
            viaRegs[reg] = val;
            if (reg == 0x00) {
                uint8 ayControl = (val >> 6) & 0x03;
                if (ayControl == 0x03) activeAyReg = viaRegs[0x01] & 0x0F;
                else if (ayControl == 0x02) ayRegs[activeAyReg & 0x0F] = viaRegs[0x01];
            }
            return;
        }
        ram[addr] = val;
    }
}

void OricMachine::playAyEffect(int type, int duration) {
    soundType = type;
    soundTimer = duration;
}

double OricMachine::evaluateExpr(const std::string& expr) {
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

void OricMachine::executeStatement(const std::string& stmt, bool& shouldStop, int& jumpToLine) {
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
        basicLines.push_back("Ready");
    } else if (upper == "LIST") {
        for (const auto& [lNum, text] : storedProgram) {
            basicLines.push_back(std::to_string(lNum) + " " + text);
        }
        basicLines.push_back("Ready");
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
    } else if (upper.rfind("PING", 0) == 0) {
        playAyEffect(1, 10);
    } else if (upper.rfind("ZAP", 0) == 0) {
        playAyEffect(2, 12);
    } else if (upper.rfind("EXPLODE", 0) == 0) {
        playAyEffect(3, 20);
    } else if (upper.rfind("SHOOT", 0) == 0) {
        playAyEffect(4, 15);
    } else if (upper.rfind("SOUND", 0) == 0) {
        playAyEffect(1, 15);
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
        basicLines.push_back("Syntax Error");
    }
}

void OricMachine::executeProgram() {
    if (storedProgram.empty()) {
        basicLines.push_back("Ready");
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
    basicLines.push_back("Ready");
}

void OricMachine::executeBasicLine(const std::string& line) {
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

void OricMachine::runFrame() {
    animTick++;
    cursorBlink = (animTick / 15) % 2;

    if (soundTimer > 0) soundTimer--;

    int cycles = 0;
    while (cycles < 5000) {
        cycles += cpu.step();
    }

    renderOricScreen();
    updateOricAudio();
}

void OricMachine::renderOricScreen() {
    // Fill background with Oric Atmos dark border
    std::fill(screenBuffer.begin(), screenBuffer.end(), 0xFF141414);

    const int startX = 80;
    const int startY = 32;
    const int dispW = 480;
    const int dispH = 448;

    // Active screen area
    for (int y = startY; y < startY + dispH; y++) {
        for (int x = startX; x < startX + dispW; x++) {
            screenBuffer[y * CANVAS_W + x] = 0xFF000000;
        }
    }

    const uint32 colorHeader = 0xFF00E0E0; // Cyan
    const uint32 colorText = 0xFFFFFFFF;   // White
    const int pitch = CANVAS_W * 4;

    int maxLines = 23;
    int firstLine = (int)basicLines.size() > maxLines ? (int)basicLines.size() - maxLines : 0;
    int yPos = startY + 8;

    for (size_t i = firstLine; i < basicLines.size(); i++) {
        uint32 fg = (i == 0) ? colorHeader : colorText;
        GUIFont::drawString(screenBuffer.data(), pitch, startX + 16, yPos, basicLines[i], fg);
        yPos += 18;
    }

    // Input prompt with authentic Oric inverted block cursor
    std::string prompt = currentInput;
    GUIFont::drawString(screenBuffer.data(), pitch, startX + 16, yPos, prompt, colorText);

    if (cursorBlink) {
        int cx = startX + 16 + (int)prompt.length() * 8;
        for (int py = yPos; py < yPos + 14; py++) {
            for (int px = cx; px < cx + 8; px++) {
                if (px < CANVAS_W && py < CANVAS_H) {
                    screenBuffer[py * CANVAS_W + px] = 0xFFE00000; // Red Oric cursor
                }
            }
        }
    }
}

void OricMachine::updateOricAudio() {
    int16 sample = 0;
    if (soundTimer > 0) {
        if (soundType == 1) { // PING
            int freq = 16 + (20 - soundTimer) * 4;
            sample = (animTick % freq < freq / 2) ? 6000 : -6000;
        } else if (soundType == 2) { // ZAP
            int freq = 8 + (soundTimer * 3);
            sample = (animTick % freq < freq / 2) ? 7000 : -7000;
        } else if (soundType == 3) { // EXPLODE
            sample = (int16)((rand() % 16000) - 8000);
        } else if (soundType == 4) { // SHOOT
            int freq = 4 + (20 - soundTimer) * 5;
            sample = (animTick % freq < freq / 2) ? 6000 : -6000;
        }
    }
    for (size_t i = 0; i < audioBuffer.size(); i++) {
        audioBuffer[i] = sample;
    }
}

void OricMachine::handleKeyDown(SDL_Keycode key, uint16 mod) {
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        playAyEffect(1, 3);
        executeBasicLine(currentInput);
        currentInput.clear();
    } else if (key == SDLK_BACKSPACE) {
        playAyEffect(1, 2);
        if (!currentInput.empty()) currentInput.pop_back();
    } else {
        char ch = translateSDLKey(key, mod);
        if (ch != 0) {
            playAyEffect(1, 2);
            if (ch >= 'a' && ch <= 'z') ch = (char)toupper(ch);
            currentInput.push_back(ch);
        }
    }
}

void OricMachine::handleKeyUp(SDL_Keycode key, uint16 mod) {
    (void)key; (void)mod;
}

bool OricMachine::loadMedia(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;

    std::vector<uint8> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (buffer.empty()) return false;

    currentMedia = path.substr(path.find_last_of("/\\") + 1);
    basicLines.push_back("LOADED: " + currentMedia);
    basicLines.push_back("Ready");
    std::cout << "[ORIC ATMOS] Loaded tape/binary: " << currentMedia << " (" << buffer.size() << " bytes)\n";
    return true;
}
