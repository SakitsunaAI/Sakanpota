#include "enterprise_machine.h"
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
            case SDLK_QUOTE: return '@';
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

EnterpriseMachine::EnterpriseMachine() {
    screenBuffer.resize(CANVAS_W * CANVAS_H, 0xFF000000);
    audioBuffer.resize(882, 0);
    init();
}

void EnterpriseMachine::init() {
    std::memcpy(exosRom.data(), EmbeddedROMs::ENTERPRISE_EXOS, 32768);
    std::memcpy(basicRom.data(), EmbeddedROMs::ENTERPRISE_BASIC, 16384);
    ram.fill(0x00);

    segmentRegs[0] = 0x00;
    segmentRegs[1] = 0x04;
    segmentRegs[2] = 0x01;
    segmentRegs[3] = 0x02;

    for (int i = 0; i < 10; i++) keyMatrix[i] = 0xFF;
    std::memset(daveRegs, 0, sizeof(daveRegs));

    cpu.memRead = [this](uint16 addr) -> uint8 { return memRead(addr); };
    cpu.memWrite = [this](uint16 addr, uint8 val) { memWrite(addr, val); };
    cpu.ioRead = [this](uint16 port) -> uint8 { return ioRead(port); };
    cpu.ioWrite = [this](uint16 port, uint8 val) { ioWrite(port, val); };

    reset();
}

void EnterpriseMachine::reset() {
    cpu.reset();
    segmentRegs[0] = 0x00;
    segmentRegs[1] = 0x04;
    segmentRegs[2] = 0x01;
    segmentRegs[3] = 0x02;
    for (int i = 0; i < 10; i++) keyMatrix[i] = 0xFF;
    animTick = 0;
    cursorBlink = 0;
    soundTimer = 0;
    currentInput.clear();
    storedProgram.clear();
    numVars.clear();
    basicLines.clear();

    basicLines.push_back("ENTERPRISE 64  EXOS 2.1");
    basicLines.push_back("IS-BASIC v1.0  (C) 1984 Intelligent Software Ltd");
    basicLines.push_back("58500 BYTES FREE");
    basicLines.push_back("");
    basicLines.push_back("Ready");
}

uint8 EnterpriseMachine::memRead(uint16 addr) {
    int page = (addr >> 14) & 0x03;
    uint16 offset = addr & 0x3FFF;
    uint8 seg = segmentRegs[page];

    if (seg == 0x00) return exosRom[offset];
    if (seg == 0x01) return exosRom[16384 + offset];
    if (seg == 0x04) return basicRom[offset];
    uint32 ramAddr = ((seg & 0x03) * 16384 + offset) % 65536;
    return ram[ramAddr];
}

void EnterpriseMachine::memWrite(uint16 addr, uint8 val) {
    int page = (addr >> 14) & 0x03;
    uint16 offset = addr & 0x3FFF;
    uint8 seg = segmentRegs[page];
    if (seg != 0x00 && seg != 0x01 && seg != 0x04) {
        uint32 ramAddr = ((seg & 0x03) * 16384 + offset) % 65536;
        ram[ramAddr] = val;
    }
}

uint8 EnterpriseMachine::ioRead(uint16 port) {
    uint8 low = port & 0xFF;
    if (low == 0xB5) {
        if (activeKeyboardRow < 10) return keyMatrix[activeKeyboardRow];
        return 0xFF;
    }
    return 0xFF;
}

void EnterpriseMachine::ioWrite(uint16 port, uint8 val) {
    uint8 low = port & 0xFF;
    if (low >= 0xB0 && low <= 0xB3) {
        segmentRegs[low - 0xB0] = val;
    } else if (low == 0xB5) {
        activeKeyboardRow = val & 0x0F;
    } else if (low >= 0xA0 && low <= 0xAF) {
        daveRegs[low - 0xA0] = val;
    }
}

void EnterpriseMachine::playDaveTone(int duration) {
    soundTimer = duration;
}

double EnterpriseMachine::evaluateExpr(const std::string& expr) {
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

void EnterpriseMachine::executeStatement(const std::string& stmt, bool& shouldStop, int& jumpToLine) {
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
    } else if (upper.rfind("SOUND", 0) == 0) {
        playDaveTone(15);
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

void EnterpriseMachine::executeProgram() {
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

void EnterpriseMachine::executeBasicLine(const std::string& line) {
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

void EnterpriseMachine::runFrame() {
    animTick++;
    cursorBlink = (animTick / 15) % 2;

    if (soundTimer > 0) soundTimer--;

    int cycles = 0;
    while (cycles < 10000) {
        cycles += cpu.step();
    }

    renderNickDisplay();
    updateDaveAudio();
}

void EnterpriseMachine::renderNickDisplay() {
    // Fill canvas background with Enterprise deep navy blue
    std::fill(screenBuffer.begin(), screenBuffer.end(), 0xFF081024);

    const int startX = 64;
    const int startY = 48;
    const int dispW = 512;
    const int dispH = 416;

    // Outer Nick border
    for (int y = startY - 8; y < startY + dispH + 8; y++) {
        for (int x = startX - 8; x < startX + dispW + 8; x++) {
            if (x < startX || x >= startX + dispW || y < startY || y >= startY + dispH) {
                screenBuffer[y * CANVAS_W + x] = 0xFF003366;
            } else {
                screenBuffer[y * CANVAS_W + x] = 0xFF081024;
            }
        }
    }

    const uint32 colorHeader = 0xFFFFCC00; // Gold
    const uint32 colorText = 0xFF00FFCC;   // Nick Cyan
    const int pitch = CANVAS_W * 4;

    int maxLines = 22;
    int firstLine = (int)basicLines.size() > maxLines ? (int)basicLines.size() - maxLines : 0;
    int yPos = startY + 8;

    for (size_t i = firstLine; i < basicLines.size(); i++) {
        uint32 fg = (i == 0) ? colorHeader : colorText;
        GUIFont::drawString(screenBuffer.data(), pitch, startX + 16, yPos, basicLines[i], fg);
        yPos += 18;
    }

    // Input prompt with blinking block cursor
    std::string prompt = " > " + currentInput;
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

void EnterpriseMachine::updateDaveAudio() {
    int16 sample = (soundTimer > 0) ? 6000 : 0;
    for (size_t i = 0; i < audioBuffer.size(); i++) {
        audioBuffer[i] = (i % 64 < 32) ? sample : -sample;
    }
}

void EnterpriseMachine::handleKeyDown(SDL_Keycode key, uint16 mod) {
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        playDaveTone(3);
        executeBasicLine(currentInput);
        currentInput.clear();
    } else if (key == SDLK_BACKSPACE) {
        playDaveTone(2);
        if (!currentInput.empty()) currentInput.pop_back();
    } else {
        char ch = translateSDLKey(key, mod);
        if (ch != 0) {
            playDaveTone(2);
            if (ch >= 'a' && ch <= 'z') ch = (char)toupper(ch);
            currentInput.push_back(ch);
        }
    }
}

void EnterpriseMachine::handleKeyUp(SDL_Keycode key, uint16 mod) {
    (void)key; (void)mod;
}

bool EnterpriseMachine::loadMedia(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;

    std::vector<uint8> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (buffer.empty()) return false;

    currentMedia = path.substr(path.find_last_of("/\\") + 1);
    basicLines.push_back("LOADED: " + currentMedia);
    basicLines.push_back("Ready");
    std::cout << "[ENTERPRISE 64/128] Loaded media: " << currentMedia << " (" << buffer.size() << " bytes)\n";
    return true;
}
