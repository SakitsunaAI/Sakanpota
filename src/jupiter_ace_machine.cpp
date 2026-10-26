#include "jupiter_ace_machine.h"
#include "embedded_roms.h"
#include "gui_font.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>

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

JupiterAceMachine::JupiterAceMachine() {
    screenBuffer.resize(CANVAS_W * CANVAS_H, 0xFF000000);
    audioBuffer.resize(882, 0); // 44100 / 50 Hz = 882 samples
    init();
}

void JupiterAceMachine::init() {
    memory.fill(0x00);
    std::memcpy(memory.data(), EmbeddedROMs::JUPITER_ACE_ROM, 8192);

    for (int i = 0; i < 8; i++) keyMatrix[i] = 0x1F;

    cpu.memRead = [this](uint16 addr) -> uint8 { return memory[addr]; };
    cpu.memWrite = [this](uint16 addr, uint8 val) {
        if (addr >= 0x2000) memory[addr] = val;
    };
    cpu.ioRead = [this](uint16 port) -> uint8 {
        if ((port & 0x01) == 0x00) {
            uint8 result = 0x1F;
            uint8 high = (port >> 8) & 0xFF;
            for (int row = 0; row < 8; row++) {
                if (!(high & (1 << row))) result &= keyMatrix[row];
            }
            return result | (beeperState ? 0x20 : 0x00) | 0x40;
        }
        return 0xFF;
    };
    cpu.ioWrite = [this](uint16 port, uint8 val) {
        if ((port & 0x01) == 0x00) beeperState = (val & 0x08) != 0;
    };

    reset();
}

void JupiterAceMachine::reset() {
    cpu.reset();
    for (int i = 0; i < 8; i++) keyMatrix[i] = 0x1F;
    beeperState = false;
    soundTimer = 0;
    animTick = 0;
    cursorBlink = 0;
    currentInput.clear();
    forthStack.clear();
    customWords.clear();
    forthLines.clear();

    forthLines.push_back("   Jupiter ACE Forth");
    forthLines.push_back("   (c) 1982 Boldfield Ltd");
    forthLines.push_back("   19K RAM Available");
    forthLines.push_back("");
    forthLines.push_back("   Type Forth commands:");
    forthLines.push_back("   e.g. 10 20 + .   or   VLIST");
    forthLines.push_back("");
    forthLines.push_back("OK");
}

void JupiterAceMachine::playBeep(int durationFrames) {
    beeperState = true;
    soundTimer = durationFrames;
}

void JupiterAceMachine::printForth(const std::string& text) {
    std::string s = text;
    while (!s.empty()) {
        if (s.length() <= 32) {
            forthLines.push_back(s);
            break;
        } else {
            forthLines.push_back(s.substr(0, 32));
            s = s.substr(32);
        }
    }
}

void JupiterAceMachine::executeForth(const std::string& line) {
    if (line.empty()) {
        forthLines.push_back("OK");
        return;
    }

    std::stringstream ss(line);
    std::string token;
    bool inColonDef = false;
    std::string newWordName = "";
    std::vector<std::string> newWordTokens;

    while (ss >> token) {
        std::string upperToken = token;
        for (char& c : upperToken) c = (char)toupper(c);

        if (inColonDef) {
            if (upperToken == ";") {
                if (!newWordName.empty()) {
                    customWords[newWordName] = newWordTokens;
                }
                inColonDef = false;
            } else if (newWordName.empty()) {
                newWordName = upperToken;
            } else {
                newWordTokens.push_back(upperToken);
            }
            continue;
        }

        if (upperToken == ":") {
            inColonDef = true;
            newWordName.clear();
            newWordTokens.clear();
            continue;
        }

        // Check custom dictionary words
        if (customWords.find(upperToken) != customWords.end()) {
            std::string subline = "";
            for (const auto& t : customWords[upperToken]) subline += t + " ";
            executeForth(subline);
            continue;
        }

        if (upperToken == "+") {
            if (forthStack.size() >= 2) {
                int b = forthStack.back(); forthStack.pop_back();
                int a = forthStack.back(); forthStack.pop_back();
                forthStack.push_back(a + b);
            }
        } else if (upperToken == "-") {
            if (forthStack.size() >= 2) {
                int b = forthStack.back(); forthStack.pop_back();
                int a = forthStack.back(); forthStack.pop_back();
                forthStack.push_back(a - b);
            }
        } else if (upperToken == "*") {
            if (forthStack.size() >= 2) {
                int b = forthStack.back(); forthStack.pop_back();
                int a = forthStack.back(); forthStack.pop_back();
                forthStack.push_back(a * b);
            }
        } else if (upperToken == "/") {
            if (forthStack.size() >= 2) {
                int b = forthStack.back(); forthStack.pop_back();
                int a = forthStack.back(); forthStack.pop_back();
                forthStack.push_back(b != 0 ? (a / b) : 0);
            }
        } else if (upperToken == "MOD") {
            if (forthStack.size() >= 2) {
                int b = forthStack.back(); forthStack.pop_back();
                int a = forthStack.back(); forthStack.pop_back();
                forthStack.push_back(b != 0 ? (a % b) : 0);
            }
        } else if (upperToken == "DUP") {
            if (!forthStack.empty()) forthStack.push_back(forthStack.back());
        } else if (upperToken == "DROP") {
            if (!forthStack.empty()) forthStack.pop_back();
        } else if (upperToken == "SWAP") {
            if (forthStack.size() >= 2) {
                std::swap(forthStack[forthStack.size() - 1], forthStack[forthStack.size() - 2]);
            }
        } else if (upperToken == "OVER") {
            if (forthStack.size() >= 2) {
                forthStack.push_back(forthStack[forthStack.size() - 2]);
            }
        } else if (upperToken == "ROT") {
            if (forthStack.size() >= 3) {
                int c = forthStack[forthStack.size() - 3];
                forthStack.erase(forthStack.end() - 3);
                forthStack.push_back(c);
            }
        } else if (upperToken == ".") {
            if (!forthStack.empty()) {
                int val = forthStack.back(); forthStack.pop_back();
                printForth(std::to_string(val) + " ");
            } else {
                printForth("Stack Underflow");
            }
        } else if (upperToken == ".S") {
            std::string out = "<" + std::to_string(forthStack.size()) + "> ";
            for (int val : forthStack) out += std::to_string(val) + " ";
            printForth(out);
        } else if (upperToken == "VLIST" || upperToken == "WORDS") {
            printForth("FORTH WORDS:");
            printForth("+ - * / MOD DUP DROP SWAP OVER");
            printForth("ROT . .S CLS BEEP EMIT CR VLIST");
            printForth("FORTH LOAD SAVE EDIT REDEFINE");
        } else if (upperToken == "CLS") {
            forthLines.clear();
        } else if (upperToken == "BEEP") {
            playBeep(15);
        } else if (upperToken == "CR") {
            forthLines.push_back("");
        } else {
            // Try parse integer
            try {
                size_t idx = 0;
                int val = std::stoi(token, &idx);
                if (idx == token.length()) {
                    forthStack.push_back(val);
                } else {
                    printForth(token + " ?");
                    return;
                }
            } catch (...) {
                printForth(token + " ?");
                return;
            }
        }
    }
    forthLines.push_back("OK");
}

void JupiterAceMachine::runFrame() {
    animTick++;
    cursorBlink = (animTick / 15) % 2;

    if (soundTimer > 0) {
        soundTimer--;
        if (soundTimer == 0) beeperState = false;
    }

    // Step CPU briefly for background execution
    int cycles = 0;
    while (cycles < 10000) {
        cycles += cpu.step();
    }

    renderScreen();
    updateAudio();
}

void JupiterAceMachine::renderScreen() {
    // Fill canvas background border with classic Jupiter Ace dark border
    std::fill(screenBuffer.begin(), screenBuffer.end(), 0xFF141414);

    // Active screen: 32 columns x 24 rows, each char is 8x8 pixels (256 x 192)
    // Scale 2x -> 512 x 384, centered at X: 64, Y: 64
    const int startX = (CANVAS_W - 512) / 2;
    const int startY = (CANVAS_H - 384) / 2;

    const uint32 colorFG = 0xFFFFFFFF; // Crisp White
    const uint32 colorBG = 0xFF000000; // Deep Black

    // Fill active screen with deep black
    for (int y = startY; y < startY + 384; y++) {
        for (int x = startX; x < startX + 512; x++) {
            screenBuffer[y * CANVAS_W + x] = colorBG;
        }
    }

    // Render Forth output lines (top 23 rows)
    int maxRows = 23;
    int firstRow = (int)forthLines.size() > maxRows ? (int)forthLines.size() - maxRows : 0;
    int rowIdx = 0;

    for (size_t i = firstRow; i < forthLines.size(); i++) {
        const std::string& line = forthLines[i];
        for (size_t col = 0; col < line.length() && col < 32; col++) {
            char c = line[col];
            for (int y = 0; y < 8; y++) {
                uint8 bits = GUIFont::getGlyphRow(c, y);
                for (int x = 0; x < 8; x++) {
                    if (bits & (0x80 >> x)) {
                        int px = startX + (col * 8 + x) * 2;
                        int py = startY + (rowIdx * 8 + y) * 2;
                        screenBuffer[py * CANVAS_W + px] = colorFG;
                        screenBuffer[py * CANVAS_W + px + 1] = colorFG;
                        screenBuffer[(py + 1) * CANVAS_W + px] = colorFG;
                        screenBuffer[(py + 1) * CANVAS_W + px + 1] = colorFG;
                    }
                }
            }
        }
        rowIdx++;
    }

    // Row 23: Input line with prompt and blinking cursor
    std::string prompt = "> " + currentInput;
    for (size_t col = 0; col < prompt.length() && col < 32; col++) {
        char c = prompt[col];
        for (int y = 0; y < 8; y++) {
            uint8 bits = GUIFont::getGlyphRow(c, y);
            for (int x = 0; x < 8; x++) {
                if (bits & (0x80 >> x)) {
                    int px = startX + (col * 8 + x) * 2;
                    int py = startY + (23 * 8 + y) * 2;
                    screenBuffer[py * CANVAS_W + px] = colorFG;
                    screenBuffer[py * CANVAS_W + px + 1] = colorFG;
                    screenBuffer[(py + 1) * CANVAS_W + px] = colorFG;
                    screenBuffer[(py + 1) * CANVAS_W + px + 1] = colorFG;
                }
            }
        }
    }

    // Blinking cursor
    if (cursorBlink && prompt.length() < 32) {
        int curCol = (int)prompt.length();
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                int px = startX + (curCol * 8 + x) * 2;
                int py = startY + (23 * 8 + y) * 2;
                screenBuffer[py * CANVAS_W + px] = colorFG;
                screenBuffer[py * CANVAS_W + px + 1] = colorFG;
                screenBuffer[(py + 1) * CANVAS_W + px] = colorFG;
                screenBuffer[(py + 1) * CANVAS_W + px + 1] = colorFG;
            }
        }
    }
}

void JupiterAceMachine::updateAudio() {
    int16 sample = beeperState ? 8000 : 0;
    for (size_t i = 0; i < audioBuffer.size(); i++) {
        audioBuffer[i] = (i % 32 < 16) ? sample : -sample;
    }
}

void JupiterAceMachine::handleKeyDown(SDL_Keycode key, uint16 mod) {
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        playBeep(3);
        forthLines.push_back("> " + currentInput);
        executeForth(currentInput);
        currentInput.clear();
    } else if (key == SDLK_BACKSPACE) {
        playBeep(2);
        if (!currentInput.empty()) currentInput.pop_back();
    } else {
        char ch = translateSDLKey(key, mod);
        if (ch != 0) {
            playBeep(2);
            if (ch >= 'a' && ch <= 'z') ch = (char)toupper(ch);
            currentInput.push_back(ch);
        }
    }
}

void JupiterAceMachine::handleKeyUp(SDL_Keycode key, uint16 mod) {
    (void)key; (void)mod;
}

bool JupiterAceMachine::loadMedia(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;

    std::vector<uint8> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (buffer.empty()) return false;

    size_t copyLen = std::min(buffer.size(), size_t(32768));
    std::memcpy(&memory[0x3C00], buffer.data(), copyLen);

    currentMedia = path.substr(path.find_last_of("/\\") + 1);
    forthLines.push_back("LOADED: " + currentMedia);
    forthLines.push_back("OK");
    return true;
}
