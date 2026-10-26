#include "mac_machine.h"
#include "gui_font.h"
#include "embedded_roms.h"
#include <algorithm>
#include <cstring>
#include <cmath>

MacMachine::MacMachine() {
    ram.resize(128 * 1024, 0); // 128 KB RAM
    rom.resize(64 * 1024, 0);  // 64 KB ROM
    framebuffer.resize(640 * 512, 0xFF202228);
    audioBuffer.resize(SAMPLES_PER_FRAME, 0);
    init();
}

void MacMachine::init() {
    std::fill(ram.begin(), ram.end(), 0);
    std::memcpy(rom.data(), EmbeddedROMs::MAC_128K_ROM, sizeof(EmbeddedROMs::MAC_128K_ROM));

    // Setup 68000 CPU Bus callbacks
    cpu.read8 = [this](uint32 addr) -> uint8 {
        addr &= 0xFFFFFF;
        if (addr < 0x020000) return ram[addr];
        if (addr >= 0x400000 && addr < 0x410000) return rom[addr - 0x400000];
        return 0xFF;
    };
    cpu.read16 = [this](uint32 addr) -> uint16 {
        uint8 hi = cpu.read8(addr);
        uint8 lo = cpu.read8(addr + 1);
        return ((uint16)hi << 8) | lo;
    };
    cpu.read32 = [this](uint32 addr) -> uint32 {
        uint16 hi = cpu.read16(addr);
        uint16 lo = cpu.read16(addr + 2);
        return ((uint32)hi << 16) | lo;
    };
    cpu.write8 = [this](uint32 addr, uint8 val) {
        addr &= 0xFFFFFF;
        if (addr < 0x020000) ram[addr] = val;
    };
    cpu.write16 = [this](uint32 addr, uint16 val) {
        cpu.write8(addr, (uint8)(val >> 8));
        cpu.write8(addr + 1, (uint8)(val & 0xFF));
    };
    cpu.write32 = [this](uint32 addr, uint32 val) {
        cpu.write16(addr, (uint16)(val >> 16));
        cpu.write16(addr + 2, (uint16)(val & 0xFFFF));
    };

    reset();
}

void MacMachine::reset() {
    cpu.reset();
    bootTimer = 0;
    showDesktop = false;
    diskInserted = false;
    mouseX = 256;
    mouseY = 171;
    mouseButton = false;

    drawBootScreen();
}

void MacMachine::setPixel(int x, int y, bool black) {
    if (x < 0 || x >= MAC_WIDTH || y < 0 || y >= MAC_HEIGHT) return;
    uint32 byteIdx = VRAM_OFFSET + y * 64 + (x / 8);
    uint8 bitMask = 0x80 >> (x & 7);
    if (black) {
        ram[byteIdx] |= bitMask;
    } else {
        ram[byteIdx] &= ~bitMask;
    }
}

// 16x16 Happy Mac Icon Bitmap
static const uint16 HAPPY_MAC[16] = {
    0x3FFC, //   ************
    0x4002, //  *            *
    0x5FFA, //  * ********** *
    0x500A, //  * *  *  *  * *
    0x500A, //  * *  *  *  * *
    0x500A, //  * *  *  *  * *
    0x518A, //  * *   **   * *
    0x500A, //  * *        * *
    0x524A, //  * *  *  *  * *
    0x518A, //  * *   **   * *
    0x5FFA, //  * ********** *
    0x4002, //  *            *
    0x4182, //  *    **      *
    0x4002, //  *            *
    0x7FFE, // ***************
    0x0000
};

void MacMachine::drawBootScreen() {
    // Clear Mac 512x342 VRAM to paper white (0 = white)
    std::fill(ram.begin() + VRAM_OFFSET, ram.begin() + VRAM_OFFSET + 64 * MAC_HEIGHT, 0x00);

    // Draw Happy Mac Icon in center (x = 248, y = 150)
    int cx = 248;
    int cy = 150;
    for (int r = 0; r < 16; r++) {
        uint16 row = HAPPY_MAC[r];
        for (int c = 0; c < 16; c++) {
            if ((row >> (15 - c)) & 1) {
                setPixel(cx + c, cy + r, true);
            }
        }
    }
}

void MacMachine::drawDesktop() {
    // 50% checkerboard pattern: alternating lines 0xAA and 0x55
    for (int y = 0; y < MAC_HEIGHT; y++) {
        uint8 pat = (y % 2 == 0) ? 0xAA : 0x55;
        // Menu bar at y = 0..19 is pure white
        if (y < 20) pat = 0x00;
        std::fill(ram.begin() + VRAM_OFFSET + y * 64, ram.begin() + VRAM_OFFSET + (y + 1) * 64, pat);
    }

    // 1px black divider under menu bar
    std::fill(ram.begin() + VRAM_OFFSET + 19 * 64, ram.begin() + VRAM_OFFSET + 20 * 64, 0xFF);

    // Draw Menu Bar text: Apple logo, File, Edit, View, Special
    std::string menu = "  File   Edit   View   Special";
    int startX = 24;
    for (size_t i = 0; i < menu.length(); i++) {
        char ch = menu[i];
        for (int r = 0; r < 8; r++) {
            uint8 bits = GUIFont::getGlyphRow(ch, r);
            for (int c = 0; c < 8; c++) {
                if ((bits >> (7 - c)) & 1) {
                    setPixel(startX + (int)i * 8 + c, 6 + r, true);
                }
            }
        }
    }

    // Draw Apple Logo at (8, 5)
    for (int r = 0; r < 8; r++) {
        uint8 bits = GUIFont::getGlyphRow('@', r);
        for (int c = 0; c < 8; c++) {
            if ((bits >> (7 - c)) & 1) setPixel(8 + c, 6 + r, true);
        }
    }

    // Draw Floppy Disk Icon on upper right desktop (x = 450, y = 35)
    int diskX = 450;
    int diskY = 35;
    // Disk outline
    for (int x = 0; x < 32; x++) {
        setPixel(diskX + x, diskY, true);
        setPixel(diskX + x, diskY + 32, true);
    }
    for (int y = 0; y < 32; y++) {
        setPixel(diskX, diskY + y, true);
        setPixel(diskX + 31, diskY + y, true);
    }
    // Disk label
    for (int y = 1; y < 32; y++) {
        for (int x = 1; x < 31; x++) {
            setPixel(diskX + x, diskY + y, false); // White inside
        }
    }
    for (int x = 4; x < 28; x++) {
        setPixel(diskX + x, diskY + 6, true);
        setPixel(diskX + x, diskY + 16, true);
    }

    std::string diskLabel = mediaTitle;
    if (diskLabel.length() > 14) diskLabel = diskLabel.substr(0, 12) + "..";
    int labelX = diskX - (int)diskLabel.length() * 4 + 16;
    for (size_t i = 0; i < diskLabel.length(); i++) {
        char ch = diskLabel[i];
        for (int r = 0; r < 8; r++) {
            uint8 bits = GUIFont::getGlyphRow(ch, r);
            for (int c = 0; c < 8; c++) {
                if ((bits >> (7 - c)) & 1) setPixel(labelX + (int)i * 8 + c, diskY + 36 + r, true);
            }
        }
    }

    // Draw Trash Can at lower right (x = 460, y = 280)
    int trashX = 460;
    int trashY = 280;
    for (int x = 0; x < 24; x++) {
        setPixel(trashX + x, trashY, true);
        setPixel(trashX + x, trashY + 28, true);
    }
    for (int y = 0; y < 28; y++) {
        setPixel(trashX + 2, trashY + y, true);
        setPixel(trashX + 21, trashY + y, true);
    }
    for (int y = 1; y < 28; y++) {
        for (int x = 3; x < 21; x++) setPixel(trashX + x, trashY + y, false);
    }
    std::string trashLabel = "Trash";
    for (size_t i = 0; i < trashLabel.length(); i++) {
        char ch = trashLabel[i];
        for (int r = 0; r < 8; r++) {
            uint8 bits = GUIFont::getGlyphRow(ch, r);
            for (int c = 0; c < 8; c++) {
                if ((bits >> (7 - c)) & 1) setPixel(trashX + (int)i * 8 - 8 + c, trashY + 32 + r, true);
            }
        }
    }
}

// Classic Mac 11x16 Pointer
static const uint16 MOUSE_POINTER[16] = {
    0x8000, 0xC000, 0xE000, 0xF000, 0xF800, 0xFC00, 0xFE00, 0xFF00,
    0xF800, 0xD800, 0x8C00, 0x0C00, 0x0600, 0x0600, 0x0300, 0x0000
};

void MacMachine::drawMousePointer(int px, int py) {
    for (int r = 0; r < 16; r++) {
        uint16 row = MOUSE_POINTER[r];
        for (int c = 0; c < 16; c++) {
            if ((row >> (15 - c)) & 1) {
                setPixel(px + c, py + r, true);
            }
        }
    }
}

void MacMachine::runFrame() {
    bootTimer++;
    if (bootTimer > 60 && !showDesktop) {
        showDesktop = true;
    }

    // Step 68000 CPU cycles (~130,000 cycles for 8 MHz 68000 at 60 Hz)
    int cycles = 0;
    while (cycles < 50000 && !cpu.halted) {
        cycles += cpu.step();
    }

    if (showDesktop) {
        drawDesktop();
        drawMousePointer(mouseX, mouseY);
    } else {
        drawBootScreen();
    }

    renderMacScreen();
}

void MacMachine::renderMacScreen() {
    // Fill outer background with Macintosh chassis dark slate
    std::fill(framebuffer.begin(), framebuffer.end(), 0xFF181A20);

    int startX = (640 - MAC_WIDTH) / 2; // 64
    int startY = (512 - MAC_HEIGHT) / 2; // 85

    for (int y = 0; y < MAC_HEIGHT; y++) {
        uint32* dst = &framebuffer[(startY + y) * 640 + startX];
        const uint8* src = &ram[VRAM_OFFSET + y * 64];

        for (int byte = 0; byte < 64; byte++) {
            uint8 b = src[byte];
            for (int bit = 0; bit < 8; bit++) {
                bool isBlack = ((b >> (7 - bit)) & 1) != 0;
                // Paper-white CRT: White = 0xFFF8F8F8, Black = 0xFF101010
                dst[byte * 8 + bit] = isBlack ? 0xFF101010 : 0xFFF5F5FA;
            }
        }
    }
}

void MacMachine::handleKeyDown(SDL_Keycode key, uint16 mod) {
    (void)key; (void)mod;
}

void MacMachine::handleKeyUp(SDL_Keycode key, uint16 mod) {
    (void)key; (void)mod;
}

void MacMachine::handleMouseMotion(int x, int y) {
    int startX = (640 - MAC_WIDTH) / 2;
    int startY = (512 - MAC_HEIGHT) / 2;
    mouseX = std::clamp(x - startX, 0, MAC_WIDTH - 1);
    mouseY = std::clamp(y - startY, 0, MAC_HEIGHT - 1);
}

void MacMachine::handleMouseButton(uint8 button, bool pressed, int x, int y) {
    handleMouseMotion(x, y);
    if (button == SDL_BUTTON_LEFT) {
        mouseButton = pressed;
    }
}

bool MacMachine::loadMedia(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    std::vector<uint8> buffer((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    std::string fname = path.substr(path.find_last_of("/\\") + 1);
    mediaTitle = fname;
    diskInserted = true;
    showDesktop = true;
    return true;
}
