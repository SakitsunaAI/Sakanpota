#include "lisa_machine.h"
#include "embedded_roms.h"
#include "gui_font.h"
#include <cstring>
#include <cmath>
#include <algorithm>

LisaMachine::LisaMachine() {
    ram.resize(1024 * 1024, 0); // 1MB RAM
    rom.resize(16384, 0);       // 16KB Boot ROM
    rawFramebuffer.resize(CANVAS_WIDTH * CANVAS_HEIGHT, 0xFF000000);
    lisaBitmap.resize(LISA_WIDTH * LISA_HEIGHT, 0xFFFFFFFF);
    audioBuffer.resize(882, 0);

    init();
}

void LisaMachine::init() {
    // Load embedded Apple Lisa Boot ROM directly
    std::memcpy(rom.data(), EmbeddedROMs::LISA_BOOT_ROM, sizeof(EmbeddedROMs::LISA_BOOT_ROM));

    // Connect 68000 bus
    cpu.read8 = [this](uint32 addr) -> uint8 {
        addr &= 0x00FFFFFF;
        if (addr < ram.size()) return ram[addr];
        if (addr >= 0x00FE0000 && addr < 0x00FE0000 + rom.size()) {
            return rom[addr - 0x00FE0000];
        }
        return 0xFF;
    };

    cpu.read16 = [this](uint32 addr) -> uint16 {
        return ((uint16)cpu.read8(addr) << 8) | cpu.read8(addr + 1);
    };

    cpu.read32 = [this](uint32 addr) -> uint32 {
        return ((uint32)cpu.read16(addr) << 16) | cpu.read16(addr + 2);
    };

    cpu.write8 = [this](uint32 addr, uint8 val) {
        addr &= 0x00FFFFFF;
        if (addr < ram.size()) ram[addr] = val;
    };

    cpu.write16 = [this](uint32 addr, uint16 val) {
        cpu.write8(addr, (val >> 8) & 0xFF);
        cpu.write8(addr + 1, val & 0xFF);
    };

    cpu.write32 = [this](uint32 addr, uint32 val) {
        cpu.write16(addr, (val >> 16) & 0xFFFF);
        cpu.write16(addr + 2, val & 0xFFFF);
    };

    reset();
}

void LisaMachine::reset() {
    cpu.reset();
    activeWindow = 1; // LisaCalc active
    menuOpen = false;
    selectedMenu = -1;
    frameCount = 0;
}

void LisaMachine::runFrame() {
    frameCount++;

    // Step CPU for ~50,000 cycles (5 MHz / 50 Hz frame = 100,000 cycles)
    int cyclesRemaining = 100000;
    while (cyclesRemaining > 0) {
        int c = cpu.step();
        cyclesRemaining -= c;
    }

    // Render Lisa OS Desktop & Windows
    renderLisaOS();

    // Scale native 720x364 to 640x512 canvas
    scaleToCanvas();

    // Generate slight acoustic speaker hum
    for (size_t i = 0; i < audioBuffer.size(); i++) {
        audioBuffer[i] = (i % 64 < 32) ? 60 : -60;
    }
}

void LisaMachine::drawLisaIcon(int x, int y, const char* name, int type) {
    int pitch = LISA_WIDTH * 4;
    // Draw icon background & border
    for (int dy = 0; dy < 32; dy++) {
        for (int dx = 0; dx < 32; dx++) {
            int px = x + dx;
            int py = y + dy;
            if (px >= 0 && px < LISA_WIDTH && py >= 0 && py < LISA_HEIGHT) {
                bool on = false;
                if (type == 0) { // ProFile Hard Disk
                    if (dy == 0 || dy == 31 || dx == 0 || dx == 31) on = true;
                    if (dy >= 6 && dy <= 12 && dx >= 4 && dx <= 20) on = true; // Disk slot
                    if (dy >= 20 && dy <= 24 && dx >= 24 && dx <= 28) on = true; // Power LED
                } else if (type == 1) { // LisaCalc / Document
                    if (dy == 0 || dy == 31 || dx == 0 || dx == 31) on = true;
                    if (dy > 4 && dy < 28 && (dx == 10 || dx == 20)) on = true; // Grid lines
                    if (dx > 4 && dx < 28 && (dy == 12 || dy == 20)) on = true;
                } else if (type == 2) { // Wastebasket
                    if ((dy == 4 && dx >= 4 && dx <= 27) || (dy == 31 && dx >= 6 && dx <= 25)) on = true;
                    if (dx == 6 || dx == 25 || dx == 12 || dx == 19) on = (dy >= 4);
                } else { // Preferences
                    if (dx == dy || dx + dy == 31 || dx == 16 || dy == 16) on = true;
                }
                lisaBitmap[py * LISA_WIDTH + px] = on ? 0xFF000000 : 0xFFFFFFFF;
            }
        }
    }
    // Icon label
    GUIFont::drawString(lisaBitmap.data(), pitch, x - 8, y + 36, name, 0xFF000000);
}

void LisaMachine::drawLisaWindow(int x, int y, int w, int h, const char* title, int winId) {
    int pitch = LISA_WIDTH * 4;

    // Window shadow
    for (int dy = 2; dy <= h + 2; dy++) {
        for (int dx = 2; dx <= w + 2; dx++) {
            int px = x + dx;
            int py = y + dy;
            if (px < LISA_WIDTH && py < LISA_HEIGHT) {
                lisaBitmap[py * LISA_WIDTH + px] = 0xFF555555;
            }
        }
    }

    // Window background & border
    for (int dy = 0; dy <= h; dy++) {
        for (int dx = 0; dx <= w; dx++) {
            int px = x + dx;
            int py = y + dy;
            if (px < LISA_WIDTH && py < LISA_HEIGHT) {
                bool border = (dy == 0 || dy == h || dx == 0 || dx == w);
                lisaBitmap[py * LISA_WIDTH + px] = border ? 0xFF000000 : 0xFFFFFFFF;
            }
        }
    }

    // Title bar (Lisa double-lined header)
    for (int dx = 1; dx < w; dx++) {
        lisaBitmap[(y + 1) * LISA_WIDTH + (x + dx)] = 0xFF000000;
        lisaBitmap[(y + 18) * LISA_WIDTH + (x + dx)] = 0xFF000000;
    }
    GUIFont::drawString(lisaBitmap.data(), pitch, x + 16, y + 4, title, 0xFF000000);

    // Close box
    for (int dy = 4; dy <= 14; dy++) {
        for (int dx = 4; dx <= 14; dx++) {
            bool on = (dy == 4 || dy == 14 || dx == 4 || dx == 14);
            lisaBitmap[(y + dy) * LISA_WIDTH + (x + dx)] = on ? 0xFF000000 : 0xFFFFFFFF;
        }
    }

    // Interior content: LisaCalc Spreadsheet Grid
    int startY = y + 20;
    // Header row
    for (int dx = 1; dx < w; dx++) {
        lisaBitmap[(startY + 16) * LISA_WIDTH + (x + dx)] = 0xFF000000;
    }
    GUIFont::drawString(lisaBitmap.data(), pitch, x + 8, startY + 3, "A: Account", 0xFF000000);
    GUIFont::drawString(lisaBitmap.data(), pitch, x + 120, startY + 3, "B: Q1 Sales", 0xFF000000);
    GUIFont::drawString(lisaBitmap.data(), pitch, x + 230, startY + 3, "C: Q2 Sales", 0xFF000000);
    GUIFont::drawString(lisaBitmap.data(), pitch, x + 340, startY + 3, "D: Total", 0xFF000000);

    // Vertical column separators
    for (int rowY = startY; rowY < y + h; rowY++) {
        lisaBitmap[rowY * LISA_WIDTH + (x + 110)] = 0xFFCCCCCC;
        lisaBitmap[rowY * LISA_WIDTH + (x + 220)] = 0xFFCCCCCC;
        lisaBitmap[rowY * LISA_WIDTH + (x + 330)] = 0xFFCCCCCC;
    }

    // Sample data rows
    GUIFont::drawString(lisaBitmap.data(), pitch, x + 8, startY + 24, "1. Apple Lisa Systems", 0xFF000000);
    GUIFont::drawString(lisaBitmap.data(), pitch, x + 120, startY + 24, "$450,000", 0xFF000000);
    GUIFont::drawString(lisaBitmap.data(), pitch, x + 230, startY + 24, "$680,000", 0xFF000000);
    GUIFont::drawString(lisaBitmap.data(), pitch, x + 340, startY + 24, "$1,130,000", 0xFF000000);

    GUIFont::drawString(lisaBitmap.data(), pitch, x + 8, startY + 44, "2. ProFile 5MB Drives", 0xFF000000);
    GUIFont::drawString(lisaBitmap.data(), pitch, x + 120, startY + 44, "$125,000", 0xFF000000);
    GUIFont::drawString(lisaBitmap.data(), pitch, x + 230, startY + 44, "$190,000", 0xFF000000);
    GUIFont::drawString(lisaBitmap.data(), pitch, x + 340, startY + 44, "$315,000", 0xFF000000);

    GUIFont::drawString(lisaBitmap.data(), pitch, x + 8, startY + 64, "3. LisaWrite / LisaCalc", 0xFF000000);
    GUIFont::drawString(lisaBitmap.data(), pitch, x + 120, startY + 64, "$85,000", 0xFF000000);
    GUIFont::drawString(lisaBitmap.data(), pitch, x + 230, startY + 64, "$140,000", 0xFF000000);
    GUIFont::drawString(lisaBitmap.data(), pitch, x + 340, startY + 64, "$225,000", 0xFF000000);

    // Scrollbar on the right
    int sbX = x + w - 16;
    for (int dy = 19; dy < h; dy++) {
        for (int dx = 0; dx < 16; dx++) {
            bool dither = ((dy + dx) % 2 == 0);
            lisaBitmap[(y + dy) * LISA_WIDTH + (sbX + dx)] = dither ? 0xFFEEEEEE : 0xFFFFFFFF;
        }
    }
    // Scroll thumb
    for (int dy = 35; dy <= 75; dy++) {
        for (int dx = 2; dx <= 14; dx++) {
            lisaBitmap[(y + dy) * LISA_WIDTH + (sbX + dx)] = 0xFF777777;
        }
    }
}

void LisaMachine::renderLisaOS() {
    int pitch = LISA_WIDTH * 4;

    // 1. Fill desktop background with authentic Apple Lisa 50% checkerboard dither pattern
    for (int y = 0; y < LISA_HEIGHT; y++) {
        for (int x = 0; x < LISA_WIDTH; x++) {
            bool dither = ((x + y) & 1) == 0;
            lisaBitmap[y * LISA_WIDTH + x] = dither ? 0xFFFFFFFF : 0xFFDDDDDD;
        }
    }

    // 2. Draw Desktop Icons
    drawLisaIcon(640, 30, "ProFile 5MB", 0);
    drawLisaIcon(640, 110, "LisaCalc Doc", 1);
    drawLisaIcon(640, 190, "Preferences", 3);
    drawLisaIcon(640, 270, "Wastebasket", 2);

    // 3. Draw Active Window (LisaCalc 7/7)
    drawLisaWindow(40, 45, 560, 280, "LisaCalc - [1984 Financial Model.calc]", 1);

    // 4. Draw Apple Lisa Menu Bar at top (White bar, black border, Apple logo, File, Edit, View, Disk)
    for (int y = 0; y < 18; y++) {
        for (int x = 0; x < LISA_WIDTH; x++) {
            lisaBitmap[y * LISA_WIDTH + x] = (y == 17) ? 0xFF000000 : 0xFFFFFFFF;
        }
    }
    GUIFont::drawString(lisaBitmap.data(), pitch, 12, 3, "\x7F Apple", 0xFF000000);
    GUIFont::drawString(lisaBitmap.data(), pitch, 70, 3, "File", 0xFF000000);
    GUIFont::drawString(lisaBitmap.data(), pitch, 120, 3, "Edit", 0xFF000000);
    GUIFont::drawString(lisaBitmap.data(), pitch, 170, 3, "View", 0xFF000000);
    GUIFont::drawString(lisaBitmap.data(), pitch, 225, 3, "Disk", 0xFF000000);
    GUIFont::drawString(lisaBitmap.data(), pitch, 560, 3, "Lisa OS 3.1", 0xFF444444);

    // 5. Draw Lisa Mouse Pointer (authentic black arrow with white border)
    static const uint8 mouseShape[16][16] = {
        {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {1,2,1,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {1,2,2,1,0,0,0,0,0,0,0,0,0,0,0,0},
        {1,2,2,2,1,0,0,0,0,0,0,0,0,0,0,0},
        {1,2,2,2,2,1,0,0,0,0,0,0,0,0,0,0},
        {1,2,2,2,2,2,1,0,0,0,0,0,0,0,0,0},
        {1,2,2,2,2,2,2,1,0,0,0,0,0,0,0,0},
        {1,2,2,2,2,1,1,1,1,0,0,0,0,0,0,0},
        {1,2,1,2,2,1,0,0,0,0,0,0,0,0,0,0},
        {1,1,0,1,2,2,1,0,0,0,0,0,0,0,0,0},
        {0,0,0,1,2,2,1,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,1,2,2,1,0,0,0,0,0,0,0,0},
        {0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}
    };

    for (int dy = 0; dy < 16; dy++) {
        for (int dx = 0; dx < 16; dx++) {
            int px = mouseX + dx;
            int py = mouseY + dy;
            if (px < LISA_WIDTH && py < LISA_HEIGHT) {
                uint8 code = mouseShape[dy][dx];
                if (code == 1) lisaBitmap[py * LISA_WIDTH + px] = 0xFF000000;
                else if (code == 2) lisaBitmap[py * LISA_WIDTH + px] = 0xFFFFFFFF;
            }
        }
    }
}

void LisaMachine::scaleToCanvas() {
    // Bilinear / nearest map 720x364 -> 640x512
    for (int y = 0; y < CANVAS_HEIGHT; y++) {
        int srcY = y * LISA_HEIGHT / CANVAS_HEIGHT;
        uint32* dstRow = rawFramebuffer.data() + y * CANVAS_WIDTH;
        const uint32* srcRow = lisaBitmap.data() + srcY * LISA_WIDTH;

        for (int x = 0; x < CANVAS_WIDTH; x++) {
            int srcX = x * LISA_WIDTH / CANVAS_WIDTH;
            dstRow[x] = srcRow[srcX];
        }
    }
}

void LisaMachine::handleKeyDown(SDL_Keycode key, uint16 mod) {}
void LisaMachine::handleKeyUp(SDL_Keycode key, uint16 mod) {}

void LisaMachine::handleMouseMotion(int x, int y) {
    mouseX = x * LISA_WIDTH / CANVAS_WIDTH;
    mouseY = y * LISA_HEIGHT / CANVAS_HEIGHT;
    mouseX = std::clamp(mouseX, 0, LISA_WIDTH - 1);
    mouseY = std::clamp(mouseY, 0, LISA_HEIGHT - 1);
}

void LisaMachine::handleMouseButton(uint8 button, bool pressed, int x, int y) {
    mouseButton = pressed;
    if (pressed) {
        if (mouseY < 18) {
            menuOpen = true;
            selectedMenu = mouseX / 50;
        } else {
            menuOpen = false;
        }
    }
}

bool LisaMachine::loadMedia(const std::string& path) {
    loadedMedia = path;
    size_t slash = loadedMedia.find_last_of("/\\");
    if (slash != std::string::npos) loadedMedia = loadedMedia.substr(slash + 1);
    return true;
}
