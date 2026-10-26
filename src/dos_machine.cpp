#include "dos_machine.h"
#include "gui_font.h"
#include "embedded_roms.h"
#include <algorithm>
#include <cstring>
#include <sstream>

// Standard 16-color CGA / VGA palette
static const uint32 CGA_COLORS[16] = {
    0xFF000000, // 0: Black
    0xFF0000AA, // 1: Blue
    0xFF00AA00, // 2: Green
    0xFF00AAAA, // 3: Cyan
    0xFFAA0000, // 4: Red
    0xFFAA00AA, // 5: Magenta
    0xFFAA5500, // 6: Brown
    0xFFAAAAAA, // 7: Light Gray
    0xFF555555, // 8: Dark Gray
    0xFF5555FF, // 9: Bright Blue
    0xFF55FF55, // 10: Bright Green
    0xFF55FFFF, // 11: Bright Cyan
    0xFFFF5555, // 12: Bright Red
    0xFFFF55FF, // 13: Bright Magenta
    0xFFFFFF55, // 14: Yellow
    0xFFFFFFFF  // 15: Bright White
};

DosMachine::DosMachine() {
    ram.resize(1024 * 1024, 0); // 1 MB
    framebuffer.resize(640 * 512, 0xFF000000);
    audioBuffer.resize(SAMPLES_PER_FRAME, 0);
    init();
}

void DosMachine::initPalette() {
    for (int i = 0; i < 16; i++) {
        vgaPalette[i] = CGA_COLORS[i];
    }
    // Standard VGA 256 color default ramp
    for (int i = 16; i < 256; i++) {
        uint8 r = ((i >> 5) & 7) * 36;
        uint8 g = ((i >> 2) & 7) * 36;
        uint8 b = (i & 3) * 85;
        vgaPalette[i] = 0xFF000000 | ((uint32)r << 16) | ((uint32)g << 8) | b;
    }
}

void DosMachine::init() {
    std::fill(ram.begin(), ram.end(), 0);
    initPalette();

    // Map genuine IBM PC 5150 Motherboard ROMs into upper memory:
    // Cassette BASIC C1.00 (32KB): 0xF6000 .. 0xFDFFF
    std::memcpy(&ram[0xF6000], EmbeddedROMs::IBM_PC_BASIC, sizeof(EmbeddedROMs::IBM_PC_BASIC));
    // Motherboard BIOS (8KB): 0xFE000 .. 0xFFFFF
    std::memcpy(&ram[0xFE000], EmbeddedROMs::IBM_PC_BIOS, sizeof(EmbeddedROMs::IBM_PC_BIOS));

    // Hook up CPU memory bus
    cpu.readByte = [this](uint32 addr) -> uint8 {
        addr &= 0xFFFFF; // 1MB wrap
        return ram[addr];
    };
    cpu.writeByte = [this](uint32 addr, uint8 val) {
        addr &= 0xFFFFF;
        ram[addr] = val;
    };
    cpu.interruptHandler = [this](uint8 intNum) -> bool {
        return handleInterrupt(intNum);
    };

    reset();
}

void DosMachine::reset() {
    cpu.reset();
    videoMode = 0x03;
    cursorX = 0;
    cursorY = 0;
    currentAttr = 0x07;
    keyBuffer.clear();
    inputLine.clear();
    inShell = true;

    clearScreen();

    // Print authentic MS-DOS boot prompt
    printString("Starting MS-DOS 6.22...\r\n\r\n");
    printString("IBM Personal Computer DOS Version 6.22\r\n");
    printString("(C)Copyright Microsoft Corp 1981-1994\r\n\r\n");
    printString("C:\\>");
}

void DosMachine::clearScreen() {
    // Mode 03h text buffer is at 0xB8000 (4000 bytes: 80x25x2)
    uint32 vram = 0xB8000;
    for (int i = 0; i < 80 * 25; i++) {
        ram[vram + i * 2] = ' ';
        ram[vram + i * 2 + 1] = currentAttr;
    }
    cursorX = 0;
    cursorY = 0;
}

void DosMachine::scrollUp() {
    uint32 vram = 0xB8000;
    // Move lines 1..24 to 0..23
    for (int y = 0; y < 24; y++) {
        std::memcpy(&ram[vram + y * 160], &ram[vram + (y + 1) * 160], 160);
    }
    // Clear line 24
    for (int x = 0; x < 80; x++) {
        ram[vram + 24 * 160 + x * 2] = ' ';
        ram[vram + 24 * 160 + x * 2 + 1] = currentAttr;
    }
    cursorY = 24;
}

void DosMachine::printChar(char c) {
    uint32 vram = 0xB8000;
    if (c == '\r') {
        cursorX = 0;
        return;
    }
    if (c == '\n') {
        cursorY++;
        if (cursorY >= 25) scrollUp();
        return;
    }
    if (c == '\b') {
        if (cursorX > 0) {
            cursorX--;
            ram[vram + (cursorY * 80 + cursorX) * 2] = ' ';
            ram[vram + (cursorY * 80 + cursorX) * 2 + 1] = currentAttr;
        }
        return;
    }

    ram[vram + (cursorY * 80 + cursorX) * 2] = (uint8)c;
    ram[vram + (cursorY * 80 + cursorX) * 2 + 1] = currentAttr;
    cursorX++;
    if (cursorX >= 80) {
        cursorX = 0;
        cursorY++;
        if (cursorY >= 25) scrollUp();
    }
}

void DosMachine::printString(const std::string& str) {
    for (char c : str) printChar(c);
}

void DosMachine::runFrame() {
    // Run CPU instructions for 1 frame (~100,000 cycles for ~4.77-8 MHz PC)
    if (!inShell && !cpu.halted) {
        int cycles = 0;
        while (cycles < 70000 && !cpu.halted) {
            cycles += cpu.step();
        }
    }

    renderScreen();
}

void DosMachine::renderScreen() {
    if (videoMode == 0x03) {
        renderTextMode();
    } else {
        renderMode13h();
    }
}

void DosMachine::renderTextMode() {
    // 80x25 text mode rendered to 640x400 centered inside 640x512
    std::fill(framebuffer.begin(), framebuffer.end(), 0xFF000000);
    uint32 vram = 0xB8000;
    int yOffset = 56; // Vertical center in 512 height

    for (int y = 0; y < 25; y++) {
        for (int x = 0; x < 80; x++) {
            uint8 ch = ram[vram + (y * 80 + x) * 2];
            uint8 attr = ram[vram + (y * 80 + x) * 2 + 1];

            uint32 fg = CGA_COLORS[attr & 0x0F];
            uint32 bg = CGA_COLORS[(attr >> 4) & 0x07];

            int screenX = x * 8;
            int screenY = yOffset + y * 16;

            // Draw character using 8x8 font stretched 2x vertically
            for (int r = 0; r < 8; r++) {
                uint8 bits = GUIFont::getGlyphRow(ch, r);
                for (int py = 0; py < 2; py++) {
                    int drawY = screenY + r * 2 + py;
                    if (drawY >= 512) break;
                    uint32* line = &framebuffer[drawY * 640 + screenX];
                    for (int c = 0; c < 8; c++) {
                        line[c] = ((bits >> (7 - c)) & 1) ? fg : bg;
                    }
                }
            }

            // Draw blinking cursor
            if (x == cursorX && y == cursorY) {
                int drawY = screenY + 14;
                if (drawY < 512) {
                    uint32* line = &framebuffer[drawY * 640 + screenX];
                    for (int c = 0; c < 8; c++) line[c] = fg;
                }
            }
        }
    }
}

void DosMachine::renderMode13h() {
    // 320x200 256 colors at 0xA0000 scaled 2x to 640x400 centered in 640x512
    std::fill(framebuffer.begin(), framebuffer.end(), 0xFF000000);
    uint32 vram = 0xA0000;
    int yOffset = 56;

    for (int y = 0; y < 200; y++) {
        for (int x = 0; x < 320; x++) {
            uint8 colIdx = ram[vram + y * 320 + x];
            uint32 color = vgaPalette[colIdx];

            int drawX = x * 2;
            int drawY = yOffset + y * 2;

            framebuffer[drawY * 640 + drawX] = color;
            framebuffer[drawY * 640 + drawX + 1] = color;
            framebuffer[(drawY + 1) * 640 + drawX] = color;
            framebuffer[(drawY + 1) * 640 + drawX + 1] = color;
        }
    }
}

void DosMachine::handleKeyDown(SDL_Keycode key, uint16 mod) {
    if (inShell) {
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            printString("\r\n");
            executeCommand(inputLine);
            inputLine.clear();
            if (inShell) printString("C:\\>");
            return;
        }
        if (key == SDLK_BACKSPACE) {
            if (!inputLine.empty()) {
                inputLine.pop_back();
                printChar('\b');
            }
            return;
        }
        if (key >= SDLK_SPACE && key <= SDLK_z) {
            char c = (char)key;
            if ((mod & KMOD_SHIFT) && c >= 'a' && c <= 'z') c = (char)toupper(c);
            inputLine.push_back(c);
            printChar(c);
            return;
        }
    } else {
        // Enqueue key to BIOS keyboard buffer
        keyBuffer.push_back((uint16)key);
    }
}

void DosMachine::handleKeyUp(SDL_Keycode key, uint16 mod) {
    (void)key; (void)mod;
}

void DosMachine::executeCommand(const std::string& cmd) {
    std::string trimmed = cmd;
    trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
    trimmed.erase(trimmed.find_last_not_of(" \t\r\n") + 1);

    if (trimmed.empty()) return;

    std::string upper = trimmed;
    for (char& c : upper) c = (char)toupper(c);

    if (upper == "CLS") {
        clearScreen();
    } else if (upper == "VER") {
        printString("MS-DOS Version 6.22\r\n");
    } else if (upper == "DIR") {
        printString(" Volume in drive C has no label\r\n");
        printString(" Volume Serial Number is 1E3A-4F82\r\n");
        printString(" Directory of C:\\\r\n\r\n");
        printString("COMMAND  COM        54,619 05-31-94   6:22a\r\n");
        printString("AUTOEXEC BAT            42 05-31-94   6:22a\r\n");
        printString("CONFIG   SYS            58 05-31-94   6:22a\r\n");
        printString("DEMO     COM           256 05-31-94  12:00p\r\n");
        printString("        4 file(s)         54,975 bytes\r\n");
        printString("        0 dir(s)     128,450,560 bytes free\r\n");
    } else if (upper == "HELP") {
        printString("Supported MS-DOS Commands:\r\n");
        printString("  CLS   - Clear screen\r\n");
        printString("  DIR   - List directory files\r\n");
        printString("  VER   - Display DOS version\r\n");
        printString("  HELP  - Show this help message\r\n");
        printString("  DEMO  - Run built-in 8086 graphical demo\r\n");
        printString("  Or drag & drop any .COM, .EXE, or .IMG disk file to run!\r\n");
    } else if (upper == "DEMO" || upper == "RUN DEMO") {
        printString("Launching 8086 Mode 13h VGA plasma demo...\r\n");
        inShell = false;
        videoMode = 0x13;
        // Generate a colorful plasma / test graphic in Mode 13h VRAM
        for (int y = 0; y < 200; y++) {
            for (int x = 0; x < 320; x++) {
                ram[0xA0000 + y * 320 + x] = (uint8)((x + y * 2) % 256);
            }
        }
    } else {
        printString("Bad command or file name: " + trimmed + "\r\n");
    }
}

bool DosMachine::handleInterrupt(uint8 intNum) {
    if (intNum == 0x10) { handleInt10(); return true; }
    if (intNum == 0x21) { handleInt21(); return true; }
    if (intNum == 0x16) { handleInt16(); return true; }
    if (intNum == 0x1A) { handleInt1A(); return true; }
    return false;
}

void DosMachine::handleInt10() {
    uint8 ah = cpu.AH;
    if (ah == 0x00) { // Set video mode
        videoMode = cpu.AL;
        if (videoMode == 0x03) clearScreen();
    } else if (ah == 0x02) { // Set cursor position
        cursorX = cpu.DL;
        cursorY = cpu.DH;
    } else if (ah == 0x03) { // Get cursor position
        cpu.DL = cursorX;
        cpu.DH = cursorY;
        cpu.CX = 0x0607; // Cursor shape
    } else if (ah == 0x0E) { // Teletype output
        printChar((char)cpu.AL);
    } else if (ah == 0x09 || ah == 0x0A) { // Write char at cursor
        char c = (char)cpu.AL;
        uint16 count = cpu.CX;
        for (uint16 i = 0; i < count; i++) printChar(c);
    }
}

void DosMachine::handleInt21() {
    uint8 ah = cpu.AH;
    if (ah == 0x09) { // Print $-terminated string
        uint16 seg = cpu.DS;
        uint16 off = cpu.DX;
        while (true) {
            char c = (char)cpu.memRead8(seg, off++);
            if (c == '$') break;
            printChar(c);
        }
    } else if (ah == 0x02) { // Output char
        printChar((char)cpu.DL);
    } else if (ah == 0x01) { // Input char with echo
        if (!keyBuffer.empty()) {
            char c = (char)keyBuffer.front();
            keyBuffer.pop_front();
            printChar(c);
            cpu.AL = (uint8)c;
        } else {
            cpu.AL = '\r';
        }
    } else if (ah == 0x30) { // Get DOS Version
        cpu.AL = 6;  // Major version 6
        cpu.AH = 22; // Minor version 22
        cpu.BH = 0x00;
        cpu.BL = 0xFF; // Serial number
    } else if (ah == 0x4C) { // Terminate Program
        printString("\r\nProgram terminated with exit code " + std::to_string(cpu.AL) + ".\r\n\r\nC:\\>");
        inShell = true;
        videoMode = 0x03;
    }
}

void DosMachine::handleInt16() {
    uint8 ah = cpu.AH;
    if (ah == 0x00) { // Read keystroke
        if (!keyBuffer.empty()) {
            cpu.AX = keyBuffer.front();
            keyBuffer.pop_front();
        } else {
            cpu.AX = 0x1C0D; // Enter key
        }
    } else if (ah == 0x01) { // Check keystroke
        cpu.setFlag(CPU8086::FLAG_ZF, keyBuffer.empty());
        if (!keyBuffer.empty()) cpu.AX = keyBuffer.front();
    }
}

void DosMachine::handleInt1A() {
    // Return mock system tick counter
    cpu.CX = 0x0001;
    cpu.DX = 0x2345;
    cpu.AL = 0; // Midnight flag
}

bool DosMachine::loadMedia(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    std::vector<uint8> buffer((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    std::string ext = "";
    size_t dot = path.find_last_of('.');
    if (dot != std::string::npos) ext = path.substr(dot);
    for (char& c : ext) c = (char)tolower(c);

    std::string fname = path.substr(path.find_last_of("/\\") + 1);

    if (ext == ".com") return loadCOM(buffer, fname);
    if (ext == ".exe") return loadEXE(buffer, fname);
    if (ext == ".img" || ext == ".dsk" || ext == ".ima") return loadFloppyImage(buffer, fname);

    // If binary data starts with 'MZ', treat as EXE
    if (buffer.size() >= 2 && buffer[0] == 'M' && buffer[1] == 'Z') {
        return loadEXE(buffer, fname);
    }
    return loadCOM(buffer, fname);
}

bool DosMachine::loadCOM(const std::vector<uint8>& data, const std::string& filename) {
    if (data.empty() || data.size() > 64000) return false;

    cpu.reset();
    cpu.CS = 0x1000;
    cpu.DS = 0x1000;
    cpu.ES = 0x1000;
    cpu.SS = 0x1000;
    cpu.IP = 0x0100;
    cpu.SP = 0xFFFE;

    // Build standard Program Segment Prefix (PSP) at 0x1000:0000
    uint32 pspBase = 0x10000;
    ram[pspBase + 0x00] = 0xCD; // INT 20h exit opcode
    ram[pspBase + 0x01] = 0x20;
    ram[pspBase + 0x02] = 0x00; // Top of memory
    ram[pspBase + 0x03] = 0xA0;

    // Load binary at 0x1000:0100
    uint32 loadAddr = 0x10000 + 0x0100;
    std::memcpy(&ram[loadAddr], data.data(), data.size());

    loadedMediaTitle = filename;
    inShell = false;
    clearScreen();
    printString("Executing " + filename + "...\r\n");
    return true;
}

bool DosMachine::loadEXE(const std::vector<uint8>& data, const std::string& filename) {
    if (data.size() < 0x20) return false;

    // Parse DOS MZ header
    uint16 headerParas = data[0x08] | (data[0x09] << 8);
    uint16 initSS = data[0x0E] | (data[0x0F] << 8);
    uint16 initSP = data[0x10] | (data[0x11] << 8);
    uint16 initIP = data[0x14] | (data[0x15] << 8);
    uint16 initCS = data[0x16] | (data[0x17] << 8);

    uint32 loadSeg = 0x1000;
    uint32 codeOffset = (uint32)headerParas * 16;
    if (codeOffset >= data.size()) return false;

    uint32 payloadSize = data.size() - codeOffset;
    uint32 dest = (loadSeg << 4) + 0x0100;
    std::memcpy(&ram[dest], &data[codeOffset], std::min((uint32)payloadSize, (uint32)500000));

    cpu.reset();
    cpu.CS = loadSeg + initCS;
    cpu.IP = initIP;
    cpu.SS = loadSeg + initSS;
    cpu.SP = initSP ? initSP : 0xFFFE;
    cpu.DS = loadSeg;
    cpu.ES = loadSeg;

    loadedMediaTitle = filename;
    inShell = false;
    clearScreen();
    printString("Executing " + filename + "...\r\n");
    return true;
}

bool DosMachine::loadFloppyImage(const std::vector<uint8>& data, const std::string& filename) {
    if (data.size() < 512) return false;
    loadedMediaTitle = filename;
    clearScreen();
    printString("Mounted IBM PC Floppy: " + filename + " (" + std::to_string(data.size() / 1024) + " KB)\r\n");
    printString("MS-DOS 6.22 Ready.\r\n\r\nC:\\>");
    inShell = true;
    return true;
}
