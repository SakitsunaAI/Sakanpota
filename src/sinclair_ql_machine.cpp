#include "sinclair_ql_machine.h"
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

SinclairQLMachine::SinclairQLMachine() {
    screenBuffer.resize(CANVAS_W * CANVAS_H, 0xFF000000);
    audioBuffer.resize(882, 0);
    init();
}

void SinclairQLMachine::init() {
    std::memcpy(qdosRom.data(), EmbeddedROMs::SINCLAIR_QL_ROM, 65536);
    ram.fill(0x00);

    for (int i = 0; i < 8; i++) ipcKeyMatrix[i] = 0xFF;

    cpu.read8 = [this](uint32 a) -> uint8 { return memRead8(a); };
    cpu.read16 = [this](uint32 a) -> uint16 { return memRead16(a); };
    cpu.read32 = [this](uint32 a) -> uint32 { return memRead32(a); };

    cpu.write8 = [this](uint32 a, uint8 v) { memWrite8(a, v); };
    cpu.write16 = [this](uint32 a, uint16 v) { memWrite16(a, v); };
    cpu.write32 = [this](uint32 a, uint32 v) { memWrite32(a, v); };

    reset();
}

void SinclairQLMachine::reset() {
    cpu.reset();
    displayControl = 0;
    for (int i = 0; i < 8; i++) ipcKeyMatrix[i] = 0xFF;
    ipcBeeper = false;
    soundTimer = 0;
    animTick = 0;
    cursorBlink = 0;
    currentInput.clear();
    storedProgram.clear();
    numVars.clear();
    basicLines.clear();

    cpu.A[7] = memRead32(0x000000);
    cpu.PC = memRead32(0x000004);

    basicLines.push_back("Sinclair QL (Motorola 68008 / QDOS v1.03)");
    basicLines.push_back("(C) 1984 Sinclair Research Ltd");
    basicLines.push_back("128K RAM Standard - SuperBASIC Ready");
    basicLines.push_back("");
    basicLines.push_back("F1: Monitor Mode   F2: TV Mode");
    basicLines.push_back("");
    basicLines.push_back("Ready");
}

uint8 SinclairQLMachine::memRead8(uint32 addr) {
    addr &= 0x00FFFFFF;
    if (addr < 0x010000) return qdosRom[addr];
    if (addr >= 0x018000 && addr <= 0x018003) return displayControl;
    if (addr >= 0x018020 && addr <= 0x018023) {
        uint8 row = (addr & 0x07);
        if (row < 8) return ipcKeyMatrix[row];
        return 0xFF;
    }
    if (addr >= 0x020000 && addr < 0x120000) return ram[addr - 0x020000];
    return 0xFF;
}

void SinclairQLMachine::memWrite8(uint32 addr, uint8 val) {
    addr &= 0x00FFFFFF;
    if (addr >= 0x018000 && addr <= 0x018003) { displayControl = val; return; }
    if (addr >= 0x018020 && addr <= 0x018023) { ipcBeeper = (val & 0x02) != 0; return; }
    if (addr >= 0x020000 && addr < 0x120000) ram[addr - 0x020000] = val;
}

uint16 SinclairQLMachine::memRead16(uint32 addr) {
    return ((uint16)memRead8(addr) << 8) | memRead8(addr + 1);
}

void SinclairQLMachine::memWrite16(uint32 addr, uint16 val) {
    memWrite8(addr, (uint8)(val >> 8));
    memWrite8(addr + 1, (uint8)val);
}

uint32 SinclairQLMachine::memRead32(uint32 addr) {
    return ((uint32)memRead16(addr) << 16) | memRead16(addr + 2);
}

void SinclairQLMachine::memWrite32(uint32 addr, uint32 val) {
    memWrite16(addr, (uint16)(val >> 16));
    memWrite16(addr + 2, (uint16)val);
}

void SinclairQLMachine::playIpcTone(int duration) {
    soundTimer = duration;
}

double SinclairQLMachine::evaluateExpr(const std::string& expr) {
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

void SinclairQLMachine::executeStatement(const std::string& stmt, bool& shouldStop, int& jumpToLine) {
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
    } else if (upper.rfind("BEEP", 0) == 0) {
        playIpcTone(15);
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

void SinclairQLMachine::executeProgram() {
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

void SinclairQLMachine::executeBasicLine(const std::string& line) {
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

void SinclairQLMachine::runFrame() {
    animTick++;
    cursorBlink = (animTick / 15) % 2;

    if (soundTimer > 0) soundTimer--;

    int cycles = 0;
    while (cycles < 10000) {
        cycles += cpu.step();
    }

    renderQLScreen();
    updateQLAudio();
}

void SinclairQLMachine::renderQLScreen() {
    // Fill canvas background with QL dark bezel
    std::fill(screenBuffer.begin(), screenBuffer.end(), 0xFF080808);

    const int startX = 64;
    const int startY = 48;
    const int dispW = 512;
    const int dispH = 416;

    // Window 1: Top Red border (Authentic QL QDOS window)
    for (int y = startY; y < startY + 64; y++) {
        for (int x = startX; x < startX + dispW; x++) {
            if (y == startY || y == startY + 63 || x == startX || x == startX + dispW - 1) {
                screenBuffer[y * CANVAS_W + x] = 0xFFD00000; // Red border
            } else {
                screenBuffer[y * CANVAS_W + x] = 0xFF181818;
            }
        }
    }

    // Window 2: SuperBASIC Area with White border
    for (int y = startY + 70; y < startY + dispH; y++) {
        for (int x = startX; x < startX + dispW; x++) {
            if (y == startY + 70 || y == startY + dispH - 1 || x == startX || x == startX + dispW - 1) {
                screenBuffer[y * CANVAS_W + x] = 0xFFFFFFFF; // White border
            } else {
                screenBuffer[y * CANVAS_W + x] = 0xFF101418;
            }
        }
    }

    const uint32 colorGreen = 0xFF00E000;
    const uint32 colorWhite = 0xFFFFFFFF;
    const int pitch = CANVAS_W * 4;

    // Header in Window 1
    GUIFont::drawString(screenBuffer.data(), pitch, startX + 16, startY + 12,
                        "Sinclair QL (68008 QDOS v1.03)", colorWhite);
    GUIFont::drawString(screenBuffer.data(), pitch, startX + 16, startY + 34,
                        "(C) 1984 Sinclair Research Ltd - 128K RAM", colorGreen);

    // SuperBASIC in Window 2
    int maxLines = 16;
    int firstLine = (int)basicLines.size() > maxLines ? (int)basicLines.size() - maxLines : 0;
    int yPos = startY + 84;

    for (size_t i = firstLine; i < basicLines.size(); i++) {
        GUIFont::drawString(screenBuffer.data(), pitch, startX + 16, yPos, basicLines[i], colorGreen);
        yPos += 18;
    }

    // Prompt and cursor
    std::string prompt = " > " + currentInput;
    GUIFont::drawString(screenBuffer.data(), pitch, startX + 16, yPos, prompt, colorWhite);

    if (cursorBlink) {
        int cx = startX + 16 + (int)prompt.length() * 8;
        for (int py = yPos; py < yPos + 14; py++) {
            for (int px = cx; px < cx + 8; px++) {
                if (px < CANVAS_W && py < CANVAS_H) {
                    screenBuffer[py * CANVAS_W + px] = colorGreen;
                }
            }
        }
    }
}

void SinclairQLMachine::updateQLAudio() {
    int16 sample = (soundTimer > 0) ? 6000 : 0;
    for (size_t i = 0; i < audioBuffer.size(); i++) {
        audioBuffer[i] = (i % 64 < 32) ? sample : -sample;
    }
}

void SinclairQLMachine::handleKeyDown(SDL_Keycode key, uint16 mod) {
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        playIpcTone(3);
        executeBasicLine(currentInput);
        currentInput.clear();
    } else if (key == SDLK_BACKSPACE) {
        playIpcTone(2);
        if (!currentInput.empty()) currentInput.pop_back();
    } else {
        char ch = translateSDLKey(key, mod);
        if (ch != 0) {
            playIpcTone(2);
            if (ch >= 'a' && ch <= 'z') ch = (char)toupper(ch);
            currentInput.push_back(ch);
        }
    }
}

void SinclairQLMachine::handleKeyUp(SDL_Keycode key, uint16 mod) {
    (void)key; (void)mod;
}

bool SinclairQLMachine::loadMedia(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;

    std::vector<uint8> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (buffer.empty()) return false;

    currentMedia = path.substr(path.find_last_of("/\\") + 1);
    basicLines.push_back("MDV1 LOADED: " + currentMedia);
    basicLines.push_back("Ready");
    std::cout << "[SINCLAIR QL] Loaded Microdrive image: " << currentMedia << " (" << buffer.size() << " bytes)\n";
    return true;
}
