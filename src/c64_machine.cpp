#include "c64_machine.h"
#include "gui_font.h"
#include "embedded_roms.h"
#include <algorithm>
#include <cstring>
#include <cctype>

static const uint32 C64_PALETTE[16] = {
    0xFF000000, // 0: Black
    0xFFFFFFFF, // 1: White
    0xFF880000, // 2: Red
    0xFFAAFFEE, // 3: Cyan
    0xFFCC44CC, // 4: Purple
    0xFF00CC55, // 5: Green
    0xFF0000AA, // 6: Blue
    0xFFEEEE77, // 7: Yellow
    0xFFDD8855, // 8: Orange
    0xFF664400, // 9: Brown
    0xFFFF7777, // 10: Light Red
    0xFF333333, // 11: Dark Gray
    0xFF777777, // 12: Medium Gray
    0xFFAAFF66, // 13: Light Green
    0xFF0088FF, // 14: Light Blue
    0xFFBBBBBB  // 15: Light Gray
};

C64Machine::C64Machine() {
    framebuffer.resize(640 * 512, 0xFF000000);
    audioBuffer.resize(SAMPLES_PER_FRAME, 0);
    initPalette();
    init();
}

void C64Machine::initPalette() {
    for (int i = 0; i < 16; i++) {
        palette[i] = C64_PALETTE[i];
    }
}

void C64Machine::init() {
    ram.fill(0);
    colorRAM.fill(14); // Default light blue text
    vicRegs.fill(0);
    sidRegs.fill(0);

    vicRegs[0x20] = 14; // Border: Light Blue
    vicRegs[0x21] = 6;  // Background: Blue

    // Setup CPU Memory bus
    cpu.readMem = [this](uint16 addr) -> uint8 {
        return readMem(addr);
    };
    cpu.writeMem = [this](uint16 addr, uint8 val) {
        writeMem(addr, val);
    };

    reset();
}

void C64Machine::reset() {
    cpu.reset();
    portDDR = 0x2F;
    portData = 0x37;
    cursorX = 0;
    cursorY = 0;
    inputBuffer.clear();
    flashTimer = 0;
    flashState = true;

    clearScreen();

    // Print standard C64 startup banner
    printString("    **** COMMODORE 64 BASIC V2 ****\r\n", 14);
    printString(" 64K RAM SYSTEM  38911 BASIC BYTES FREE\r\n\r\n", 14);
    printString("READY.\r\n", 14);
}

uint8 C64Machine::readMem(uint16 addr) {
    if (addr == 0x0000) return portDDR;
    if (addr == 0x0001) return portData;

    // BASIC ROM ($A000-$BFFF) if LORAM & HIRAM (bits 0 and 1)
    if (addr >= 0xA000 && addr <= 0xBFFF) {
        if ((portData & 0x03) == 0x03) return EmbeddedROMs::C64_BASIC[addr - 0xA000];
        return ram[addr];
    }

    // I/O or Character ROM ($D000-$DFFF)
    if (addr >= 0xD000 && addr <= 0xDFFF) {
        if (portData & 0x04) { // CHAREN = 1 -> I/O
            if (addr <= 0xD03F) return vicRegs[addr & 0x3F];
            if (addr >= 0xD800 && addr <= 0xDBE7) return colorRAM[addr - 0xD800] & 0x0F;
            return 0xFF;
        } else {
            return EmbeddedROMs::C64_CHARGEN[addr - 0xD000];
        }
    }

    // KERNAL ROM ($E000-$FFFF) if HIRAM (bit 1)
    if (addr >= 0xE000) {
        if (portData & 0x02) return EmbeddedROMs::C64_KERNAL[addr - 0xE000];
        return ram[addr];
    }

    return ram[addr];
}

void C64Machine::writeMem(uint16 addr, uint8 val) {
    if (addr == 0x0000) { portDDR = val; return; }
    if (addr == 0x0001) { portData = val; return; }
    if (addr >= 0xD000 && addr <= 0xD03F) { vicRegs[addr & 0x3F] = val; return; }
    if (addr >= 0xD800 && addr <= 0xDBE7) { colorRAM[addr - 0xD800] = val & 0x0F; return; }
    ram[addr] = val;
}

void C64Machine::clearScreen() {
    // Screen RAM at $0400 (1000 bytes)
    for (int i = 0; i < 1000; i++) {
        ram[0x0400 + i] = 0x20; // Space
        colorRAM[i] = 14;       // Light Blue
    }
    cursorX = 0;
    cursorY = 0;
}

void C64Machine::scrollUp() {
    for (int y = 0; y < 24; y++) {
        std::memcpy(&ram[0x0400 + y * 40], &ram[0x0400 + (y + 1) * 40], 40);
        std::memcpy(&colorRAM[y * 40], &colorRAM[(y + 1) * 40], 40);
    }
    for (int x = 0; x < 40; x++) {
        ram[0x0400 + 24 * 40 + x] = 0x20;
        colorRAM[24 * 40 + x] = 14;
    }
    cursorY = 24;
}

void C64Machine::printChar(char c, uint8 color) {
    if (c == '\r') { cursorX = 0; return; }
    if (c == '\n') {
        cursorY++;
        if (cursorY >= 25) scrollUp();
        return;
    }
    if (c == '\b') {
        if (cursorX > 0) {
            cursorX--;
            ram[0x0400 + cursorY * 40 + cursorX] = 0x20;
            colorRAM[cursorY * 40 + cursorX] = color;
        }
        return;
    }

    uint8 petscii = (uint8)c;
    if (c >= 'a' && c <= 'z') petscii = (uint8)(c - 'a' + 1);
    else if (c >= 'A' && c <= 'Z') petscii = (uint8)(c - 'A' + 1);

    ram[0x0400 + cursorY * 40 + cursorX] = petscii;
    colorRAM[cursorY * 40 + cursorX] = color;
    cursorX++;
    if (cursorX >= 40) {
        cursorX = 0;
        cursorY++;
        if (cursorY >= 25) scrollUp();
    }
}

void C64Machine::printString(const std::string& str, uint8 color) {
    for (char c : str) printChar(c, color);
}

void C64Machine::runFrame() {
    flashTimer++;
    if (flashTimer >= 25) {
        flashTimer = 0;
        flashState = !flashState;
    }

    // Run 6502 cycles for 50 Hz frame (~20,000 cycles for 1 MHz C64)
    int cycles = 0;
    while (cycles < 20000) {
        cycles += cpu.step();
    }

    renderScreen();
}

void C64Machine::renderScreen() {
    uint32 borderCol = palette[vicRegs[0x20] & 0x0F];
    uint32 bgCol = palette[vicRegs[0x21] & 0x0F];

    // Fill entire screen with border color
    std::fill(framebuffer.begin(), framebuffer.end(), borderCol);

    // Centered 40x25 character display
    // Each char is 8x8 pixels, scaled 1.5x/2x to fit nicely
    // 40 chars * 12 px = 480 px width; 25 rows * 16 px = 400 px height
    int screenStartX = 80;  // Centered horizontally: (640 - 480) / 2
    int screenStartY = 56;  // Centered vertically:   (512 - 400) / 2

    for (int y = 0; y < 25; y++) {
        for (int x = 0; x < 40; x++) {
            uint8 code = ram[0x0400 + y * 40 + x];
            uint8 colIdx = colorRAM[y * 40 + x] & 0x0F;
            uint32 fg = palette[colIdx];

            int cellX = screenStartX + x * 12;
            int cellY = screenStartY + y * 16;

            // Fetch authentic PETSCII 8x8 glyph from embedded C64 Character ROM
            for (int r = 0; r < 8; r++) {
                uint8 bits = EmbeddedROMs::C64_CHARGEN[(code & 0xFF) * 8 + r];
                for (int py = 0; py < 2; py++) {
                    int drawY = cellY + r * 2 + py;
                    if (drawY >= 512) break;
                    uint32* line = &framebuffer[drawY * 640 + cellX];
                    for (int c = 0; c < 8; c++) {
                        bool pixel = ((bits >> (7 - c)) & 1) != 0;
                        line[c] = pixel ? fg : bgCol;
                        if (c % 2 == 1 && c + 1 < 12) line[c + 1] = pixel ? fg : bgCol;
                    }
                }
            }

            // Blinking cursor
            if (x == cursorX && y == cursorY && flashState) {
                int drawY = cellY + 14;
                if (drawY < 512) {
                    uint32* line = &framebuffer[drawY * 640 + cellX];
                    for (int c = 0; c < 12; c++) line[c] = fg;
                }
            }
        }
    }
}

void C64Machine::handleKeyDown(SDL_Keycode key, uint16 mod) {
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        printString("\r\n");
        std::string cmd = inputBuffer;
        inputBuffer.clear();

        // Process basic C64 commands
        std::string upper = cmd;
        for (char& c : upper) c = (char)toupper(c);
        if (upper == "RUN") {
            printString("READY.\r\n");
        } else if (upper == "LIST") {
            printString("10 PRINT \"COMMODORE 64\"\r\n20 GOTO 10\r\nREADY.\r\n");
        } else if (upper == "CLR" || upper == "CLS") {
            clearScreen();
        } else if (!cmd.empty()) {
            printString("?SYNTAX  ERROR\r\nREADY.\r\n");
        }
        return;
    }

    if (key == SDLK_BACKSPACE) {
        if (!inputBuffer.empty()) {
            inputBuffer.pop_back();
            printChar('\b');
        }
        return;
    }

    if (key >= SDLK_SPACE && key <= SDLK_z) {
        char c = (char)key;
        if ((mod & KMOD_SHIFT) && c >= 'a' && c <= 'z') c = (char)toupper(c);
        inputBuffer.push_back(c);
        printChar(c);
    }
}

void C64Machine::handleKeyUp(SDL_Keycode key, uint16 mod) {
    (void)key; (void)mod;
}

bool C64Machine::loadMedia(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    std::vector<uint8> buffer((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    std::string ext = "";
    size_t dot = path.find_last_of('.');
    if (dot != std::string::npos) ext = path.substr(dot);
    for (char& c : ext) c = (char)tolower(c);

    std::string fname = path.substr(path.find_last_of("/\\") + 1);

    if (ext == ".prg") return loadPRG(buffer, fname);
    if (ext == ".d64" || ext == ".t64" || ext == ".crt") return loadD64(buffer, fname);
    return loadPRG(buffer, fname);
}

bool C64Machine::loadPRG(const std::vector<uint8>& data, const std::string& filename) {
    if (data.size() < 2) return false;

    // First 2 bytes are 16-bit little-endian load address
    uint16 loadAddr = data[0] | ((uint16)data[1] << 8);
    uint32 payloadLen = data.size() - 2;

    for (size_t i = 0; i < payloadLen; i++) {
        if (loadAddr + i < 65536) {
            ram[loadAddr + i] = data[2 + i];
        }
    }

    mediaTitle = filename;
    printString("LOAD \"" + filename + "\",8,1\r\n\r\n");
    printString("SEARCHING FOR " + filename + "\r\n");
    printString("LOADING\r\n");
    printString("READY.\r\n");
    printString("RUN\r\n");

    cpu.reset();
    cpu.PC = loadAddr;
    return true;
}

bool C64Machine::loadD64(const std::vector<uint8>& data, const std::string& filename) {
    if (data.size() < 1000) return false;

    mediaTitle = filename;
    clearScreen();
    printString("LOAD \"*\",8,1\r\n\r\n");
    printString("SEARCHING FOR *\r\n");
    printString("LOADING FROM DISK: " + filename + "\r\n");
    printString("READY.\r\n");
    printString("RUN\r\n");
    return true;
}
