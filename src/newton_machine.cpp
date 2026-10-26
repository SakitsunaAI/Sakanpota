#include "newton_machine.h"
#include "gui_font.h"
#include <iostream>
#include <algorithm>
#include <cmath>

static const char* DEFAULT_ROMS[] = {
    "/Users/bekirkarakose/Downloads/newtonmp/v13.rom",
    "/Users/bekirkarakose/Downloads/newtonmp/v100.rom",
    "roms/v13.rom",
    "roms/v100.rom",
    "roms/newton.rom"
};

NewtonMachine::NewtonMachine()
    : mouseDown(false), mouseX(0), mouseY(0), currentROMIndex(0), frameCounter(0),
      currentNoteIdx(0), cursorLine(0), cursorCol(0), cursorBlink(true), cursorBlinkTimer(0),
      scrollY(0), isStylusDrawing(false), prevPenX(0), prevPenY(0),
      showOverview(false), showKeyboard(false), showAssist(false), showFind(false),
      activeSilkscreenBtn(-1), btnFlashFrames(0), lastVirtualKeyPressed(0), virtualKeyFlashFrames(0),
      clickSamplesRemaining(0), clickPhase(0.0), clickFreq(1200.0) {
    rawFramebuffer.resize(CANVAS_WIDTH * CANVAS_HEIGHT, 0xFF141923);
    audioBuffer.resize(882, 0); // 44100Hz / 50Hz = 882 samples
}

void NewtonMachine::init() {
    cpu.read8 = [this](uint32 addr) -> uint8 { return runt.read8(addr); };
    cpu.read16 = [this](uint32 addr) -> uint16 { return runt.read16(addr); };
    cpu.read32 = [this](uint32 addr) -> uint32 { return runt.read32(addr); };
    cpu.write8 = [this](uint32 addr, uint8 val) { runt.write8(addr, val); };
    cpu.write16 = [this](uint32 addr, uint16 val) { runt.write16(addr, val); };
    cpu.write32 = [this](uint32 addr, uint32 val) { runt.write32(addr, val); };

    runt.onIRQ = [this]() { cpu.triggerIRQ(); };
    runt.onFIQ = [this]() { cpu.triggerFIQ(); };

    // Load encrypted embedded ROM first
    bool loaded = runt.loadDefaultROM();
    if (!loaded) {
        // Fallback to disk if needed
        for (size_t i = 0; i < sizeof(DEFAULT_ROMS) / sizeof(DEFAULT_ROMS[0]); i++) {
            if (runt.loadROM(DEFAULT_ROMS[i])) {
                currentROMIndex = (int)i;
                loaded = true;
                break;
            }
        }
    }

    if (!loaded) {
        std::cerr << "[Newton] Notice: Could not locate Newton ROM" << std::endl;
    }

    reset();
}


void NewtonMachine::reset() {
    runt.reset();
    cpu.reset();
    frameCounter = 0;
    mouseDown = false;

    notes.clear();

    // Default Note 1: Welcome & instructions
    NewtonNoteData note1;
    note1.title = "Welcome Note";
    note1.lines = {
        "Welcome to Apple Newton!",
        "Draw with stylus or mouse.",
        "Type keys to enter text.",
        "Click [Undo] to erase ink.",
        "Tap [K] for on-screen keys."
    };
    // Add stylish handwriting underline under title
    NewtonInkStroke s1;
    for (int x = 14; x <= 208; x += 3) {
        s1.points.push_back({(int16)x, (int16)(50 + (x % 6 < 3 ? 1 : 0))});
    }
    note1.strokes.push_back(s1);
    notes.push_back(note1);

    // Default Note 2: To-Do list
    NewtonNoteData note2;
    note2.title = "To-Do List";
    note2.lines = {
        "[x] Handcrafted ARM610 RISC",
        "[x] Handcrafted RUNT ASIC",
        "[ ] Handwriting digital ink",
        "[ ] Physical keyboard typing"
    };
    notes.push_back(note2);

    // Default Note 3: Meeting Memo
    NewtonNoteData note3;
    note3.title = "Meeting Memo";
    note3.lines = {
        "Apple Newton MessagePad",
        "Active LCD: 240 x 336",
        "Pure C++ Implementation"
    };
    notes.push_back(note3);

    currentNoteIdx = 0;
    cursorLine = 4;
    cursorCol = (int)notes[0].lines[4].length();
    cursorBlink = true;
    cursorBlinkTimer = 0;
    scrollY = 0;

    isStylusDrawing = false;
    activeStroke.points.clear();

    showOverview = false;
    showKeyboard = false;
    showAssist = false;
    showFind = false;
    findQuery = "Newton";

    activeSilkscreenBtn = -1;
    btnFlashFrames = 0;
    lastVirtualKeyPressed = 0;
    virtualKeyFlashFrames = 0;

    clickSamplesRemaining = 0;
    clickPhase = 0.0;
}

bool NewtonMachine::loadMedia(const std::string& path) {
    if (runt.loadROM(path)) {
        reset();
        return true;
    }
    return false;
}

void NewtonMachine::cycleROM() {
    int nextIdx = (currentROMIndex + 1) % 2;
    if (runt.loadROM(DEFAULT_ROMS[nextIdx])) {
        currentROMIndex = nextIdx;
        reset();
    }
}

void NewtonMachine::triggerClickSound(int freq, int lengthSamples) {
    clickFreq = (double)freq;
    clickSamplesRemaining = lengthSamples;
    clickPhase = 0.0;
}

void NewtonMachine::runFrame() {
    frameCounter++;

    // Execute genuine ARM610 instructions (~120,000 instructions per 60Hz frame = ~15 MHz execution)
    const int instructionsPerFrame = 120000;
    int executed = 0;

    while (executed < instructionsPerFrame && !cpu.halted) {
        int cycles = cpu.step();
        runt.tick(cycles);
        executed += cycles;
    }

    // Caret blink timer (~0.5s toggle)
    cursorBlinkTimer++;
    if (cursorBlinkTimer >= 30) {
        cursorBlink = !cursorBlink;
        cursorBlinkTimer = 0;
    }

    // UI flash timers
    if (btnFlashFrames > 0) btnFlashFrames--;
    if (virtualKeyFlashFrames > 0) virtualKeyFlashFrames--;

    renderCasing();
    updateAudio();
}

void NewtonMachine::drawScreenThickLine(int x0, int y0, int x1, int y1, uint32 color) {
    int dx = std::abs(x1 - x0);
    int dy = std::abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    int curX = x0;
    int curY = y0;

    while (true) {
        // Draw 2x2 pen nib for smooth digital ink
        for (int ox = 0; ox <= 1; ox++) {
            for (int oy = 0; oy <= 1; oy++) {
                int px = curX + ox;
                int py = curY + oy;
                if (px >= 0 && px < NewtonRunt::PORTRAIT_WIDTH && py >= 32 && py < 336) {
                    rawFramebuffer[(LCD_Y + py) * CANVAS_WIDTH + (LCD_X + px)] = color;
                }
            }
        }

        if (curX == x1 && curY == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            curX += sx;
        }
        if (e2 < dx) {
            err += dx;
            curY += sy;
        }
    }
}

void NewtonMachine::renderVirtualKeyboard() {
    const int KB_X = LCD_X + 4;
    const int KB_Y = LCD_Y + 236;
    const int KB_W = 232;
    const int KB_H = 96;

    // Keyboard background & frame
    GUIFont::fillRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, KB_X, KB_Y, KB_W, KB_H, 0xFF8E9F7B);
    GUIFont::drawRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, KB_X, KB_Y, KB_W, KB_H, 0xFF1C2216);

    // Title & Close [x]
    GUIFont::fillRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, KB_X + 1, KB_Y + 1, KB_W - 2, 14, 0xFF7A8C68);
    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, KB_X + 6, KB_Y + 4, "Newton Keyboard", 0xFF1C2216, 0x00000000);
    GUIFont::drawRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, KB_X + KB_W - 16, KB_Y + 2, 12, 12, 0xFF1C2216);
    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, KB_X + KB_W - 14, KB_Y + 4, "x", 0xFF1C2216, 0x00000000);

    // Row 0: Q W E R T Y U I O P
    const char* r0 = "QWERTYUIOP";
    for (int i = 0; i < 10; i++) {
        int kx = KB_X + 6 + i * 22;
        int ky = KB_Y + 18;
        bool pressed = (virtualKeyFlashFrames > 0 && lastVirtualKeyPressed == r0[i]);
        GUIFont::fillRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, kx, ky, 20, 16, pressed ? 0xFF1C2216 : 0xFF9DB288);
        GUIFont::drawRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, kx, ky, 20, 16, 0xFF1C2216);
        std::string s(1, r0[i]);
        GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, kx + 6, ky + 4, s, pressed ? 0xFF9DB288 : 0xFF1C2216, 0x00000000);
    }

    // Row 1: A S D F G H J K L
    const char* r1 = "ASDFGHJKL";
    for (int i = 0; i < 9; i++) {
        int kx = KB_X + 17 + i * 22;
        int ky = KB_Y + 36;
        bool pressed = (virtualKeyFlashFrames > 0 && lastVirtualKeyPressed == r1[i]);
        GUIFont::fillRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, kx, ky, 20, 16, pressed ? 0xFF1C2216 : 0xFF9DB288);
        GUIFont::drawRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, kx, ky, 20, 16, 0xFF1C2216);
        std::string s(1, r1[i]);
        GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, kx + 6, ky + 4, s, pressed ? 0xFF9DB288 : 0xFF1C2216, 0x00000000);
    }

    // Row 2: Del, Z X C V B N M, Clr
    int kDelX = KB_X + 6;
    int kDelY = KB_Y + 54;
    bool delPressed = (virtualKeyFlashFrames > 0 && lastVirtualKeyPressed == '<');
    GUIFont::fillRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, kDelX, kDelY, 26, 16, delPressed ? 0xFF1C2216 : 0xFF9DB288);
    GUIFont::drawRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, kDelX, kDelY, 26, 16, 0xFF1C2216);
    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, kDelX + 4, kDelY + 4, "Del", delPressed ? 0xFF9DB288 : 0xFF1C2216, 0x00000000);

    const char* r2 = "ZXCVBNM";
    for (int i = 0; i < 7; i++) {
        int kx = KB_X + 36 + i * 22;
        int ky = KB_Y + 54;
        bool pressed = (virtualKeyFlashFrames > 0 && lastVirtualKeyPressed == r2[i]);
        GUIFont::fillRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, kx, ky, 20, 16, pressed ? 0xFF1C2216 : 0xFF9DB288);
        GUIFont::drawRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, kx, ky, 20, 16, 0xFF1C2216);
        std::string s(1, r2[i]);
        GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, kx + 6, ky + 4, s, pressed ? 0xFF9DB288 : 0xFF1C2216, 0x00000000);
    }

    int kClrX = KB_X + 194;
    GUIFont::fillRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, kClrX, kDelY, 32, 16, 0xFF9DB288);
    GUIFont::drawRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, kClrX, kDelY, 32, 16, 0xFF1C2216);
    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, kClrX + 4, kDelY + 4, "Clr", 0xFF1C2216, 0x00000000);

    // Row 3: 123, Space, Enter
    int k123X = KB_X + 6;
    int kRow3Y = KB_Y + 72;
    GUIFont::fillRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, k123X, kRow3Y, 32, 18, 0xFF9DB288);
    GUIFont::drawRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, k123X, kRow3Y, 32, 18, 0xFF1C2216);
    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, k123X + 4, kRow3Y + 5, "123", 0xFF1C2216, 0x00000000);

    int kSpaceX = KB_X + 42;
    bool spacePressed = (virtualKeyFlashFrames > 0 && lastVirtualKeyPressed == ' ');
    GUIFont::fillRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, kSpaceX, kRow3Y, 114, 18, spacePressed ? 0xFF1C2216 : 0xFF9DB288);
    GUIFont::drawRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, kSpaceX, kRow3Y, 114, 18, 0xFF1C2216);
    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, kSpaceX + 38, kRow3Y + 5, "Space", spacePressed ? 0xFF9DB288 : 0xFF1C2216, 0x00000000);

    int kRetX = KB_X + 160;
    bool retPressed = (virtualKeyFlashFrames > 0 && lastVirtualKeyPressed == '\n');
    GUIFont::fillRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, kRetX, kRow3Y, 66, 18, retPressed ? 0xFF1C2216 : 0xFF9DB288);
    GUIFont::drawRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, kRetX, kRow3Y, 66, 18, 0xFF1C2216);
    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, kRetX + 12, kRow3Y + 5, "Return", retPressed ? 0xFF9DB288 : 0xFF1C2216, 0x00000000);
}

void NewtonMachine::renderOverviewScreen() {
    const int OV_X = LCD_X + 8;
    const int OV_Y = LCD_Y + 36;
    const int OV_W = 224;
    const int OV_H = 288;

    GUIFont::fillRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, OV_X, OV_Y, OV_W, OV_H, 0xFF98AA84);
    GUIFont::drawRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, OV_X, OV_Y, OV_W, OV_H, 0xFF1C2216);

    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, OV_X + 12, OV_Y + 10, "=== NOTES OVERVIEW ===", 0xFF1C2216, 0x00000000);

    for (size_t i = 0; i < notes.size(); i++) {
        int ry = OV_Y + 34 + (int)i * 28;
        bool isCur = (currentNoteIdx == (int)i);
        GUIFont::fillRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, OV_X + 8, ry, OV_W - 16, 24, isCur ? 0xFF859772 : 0xFF9DB288);
        GUIFont::drawRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, OV_X + 8, ry, OV_W - 16, 24, 0xFF1C2216);

        std::string label = std::to_string(i + 1) + ". " + notes[i].title;
        if (label.length() > 18) label = label.substr(0, 18);
        GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, OV_X + 14, ry + 8, label, 0xFF1C2216, 0x00000000);

        std::string count = std::to_string(notes[i].lines.size()) + "L";
        GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, OV_X + OV_W - 42, ry + 8, count, 0xFF2A3324, 0x00000000);
    }

    // New Note button
    int nny = OV_Y + 34 + (int)notes.size() * 28 + 6;
    GUIFont::fillRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, OV_X + 8, nny, OV_W - 16, 24, 0xFF9DB288);
    GUIFont::drawRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, OV_X + 8, nny, OV_W - 16, 24, 0xFF1C2216);
    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, OV_X + 44, nny + 8, "+ New Note...", 0xFF1C2216, 0x00000000);
}

void NewtonMachine::renderAssistDialog() {
    const int AS_X = LCD_X + 16;
    const int AS_Y = LCD_Y + 68;
    const int AS_W = 208;
    const int AS_H = 136;

    GUIFont::fillRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, AS_X, AS_Y, AS_W, AS_H, 0xFF94A77E);
    GUIFont::drawRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, AS_X, AS_Y, AS_W, AS_H, 0xFF1C2216);
    GUIFont::drawRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, AS_X + 1, AS_Y + 1, AS_W - 2, AS_H - 2, 0xFF1C2216);

    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, AS_X + 14, AS_Y + 10, "Please assist me to:", 0xFF1C2216, 0x00000000);

    // Button 1: Clear Note
    GUIFont::fillRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, AS_X + 16, AS_Y + 34, AS_W - 32, 22, 0xFF9DB288);
    GUIFont::drawRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, AS_X + 16, AS_Y + 34, AS_W - 32, 22, 0xFF1C2216);
    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, AS_X + 32, AS_Y + 41, "Clear Active Note", 0xFF1C2216, 0x00000000);

    // Button 2: Add Sample Note
    GUIFont::fillRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, AS_X + 16, AS_Y + 62, AS_W - 32, 22, 0xFF9DB288);
    GUIFont::drawRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, AS_X + 16, AS_Y + 62, AS_W - 32, 22, 0xFF1C2216);
    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, AS_X + 32, AS_Y + 69, "Add Sample Memo", 0xFF1C2216, 0x00000000);

    // Button 3: Close
    GUIFont::fillRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, AS_X + 16, AS_Y + 90, AS_W - 32, 22, 0xFF9DB288);
    GUIFont::drawRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, AS_X + 16, AS_Y + 90, AS_W - 32, 22, 0xFF1C2216);
    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, AS_X + 70, AS_Y + 97, "Dismiss", 0xFF1C2216, 0x00000000);
}

void NewtonMachine::renderFindDialog() {
    const int FD_X = LCD_X + 14;
    const int FD_Y = LCD_Y + 44;
    const int FD_W = 212;
    const int FD_H = 56;

    GUIFont::fillRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, FD_X, FD_Y, FD_W, FD_H, 0xFF94A77E);
    GUIFont::drawRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, FD_X, FD_Y, FD_W, FD_H, 0xFF1C2216);

    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, FD_X + 8, FD_Y + 8, "Find in Notes:", 0xFF1C2216, 0x00000000);

    // Input box
    GUIFont::fillRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, FD_X + 8, FD_Y + 24, 140, 22, 0xFF9DB288);
    GUIFont::drawRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, FD_X + 8, FD_Y + 24, 140, 22, 0xFF1C2216);
    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, FD_X + 12, FD_Y + 31, findQuery, 0xFF1C2216, 0x00000000);

    // Go Button
    GUIFont::fillRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, FD_X + 154, FD_Y + 24, 48, 22, 0xFF9DB288);
    GUIFont::drawRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, FD_X + 154, FD_Y + 24, 48, 22, 0xFF1C2216);
    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, FD_X + 166, FD_Y + 31, "Go", 0xFF1C2216, 0x00000000);
}

void NewtonMachine::renderNewtonScreen() {
    bool power = runt.isPoweredOn();

    if (!power) {
        // Sleep state
        GUIFont::fillRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, LCD_X, LCD_Y, NewtonRunt::PORTRAIT_WIDTH, NewtonRunt::TOTAL_HEIGHT, 0xFF6D7D62);
        GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, LCD_X + 28, LCD_Y + 140, "Apple Newton is asleep", 0xFF353E30, 0x00000000);
        GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, LCD_X + 20, LCD_Y + 160, "Press F1 or switch to wake", 0xFF353E30, 0x00000000);
        return;
    }

    // 1. Reflective Olive-Green LCD Background (240 x 336)
    GUIFont::fillRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, LCD_X, LCD_Y, NewtonRunt::PORTRAIT_WIDTH, NewtonRunt::PORTRAIT_HEIGHT, 0xFF9DB288);

    // 2. Ruled Notebook Lines (y = 48, 66, 84, 102 ... 330)
    for (int y = 48; y <= 330; y += 18) {
        for (int rx = 10; rx < 230; rx += 2) {
            rawFramebuffer[(LCD_Y + y) * CANVAS_WIDTH + (LCD_X + rx)] = 0xFF889C74;
        }
    }

    // 3. Header Bar at Top (y = 0..30)
    GUIFont::fillRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, LCD_X, LCD_Y, NewtonRunt::PORTRAIT_WIDTH, 30, 0xFF92A67D);
    GUIFont::drawRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, LCD_X, LCD_Y + 29, NewtonRunt::PORTRAIT_WIDTH, 1, 0xFF1C2216);

    // Folder Tab [Unfiled v]
    GUIFont::fillRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, LCD_X + 6, LCD_Y + 5, 78, 20, 0xFF9DB288);
    GUIFont::drawRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, LCD_X + 6, LCD_Y + 5, 78, 20, 0xFF1C2216);
    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, LCD_X + 10, LCD_Y + 11, "Unfiled v", 0xFF1C2216, 0x00000000);

    // Header Title: Notes
    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, LCD_X + 96, LCD_Y + 11, "Notes", 0xFF1C2216, 0x00000000);

    // Header Right: Date "Oct 25" & Keyboard Toggle Icon [K]
    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, LCD_X + 154, LCD_Y + 11, "Oct 25", 0xFF1C2216, 0x00000000);
    GUIFont::fillRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, LCD_X + 214, LCD_Y + 5, 20, 20, showKeyboard ? 0xFF1C2216 : 0xFF9DB288);
    GUIFont::drawRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, LCD_X + 214, LCD_Y + 5, 20, 20, 0xFF1C2216);
    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, LCD_X + 220, LCD_Y + 11, "K", showKeyboard ? 0xFF9DB288 : 0xFF1C2216, 0x00000000);

    // 4. Render Typed Text Lines
    if (currentNoteIdx >= 0 && currentNoteIdx < (int)notes.size()) {
        const auto& note = notes[currentNoteIdx];
        for (size_t i = 0; i < note.lines.size(); i++) {
            int lineY = 38 + (int)i * 18 - scrollY;
            if (lineY >= 32 && lineY <= 324) {
                GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, LCD_X + 14, LCD_Y + lineY, note.lines[i], 0xFF1C2216, 0x00000000);
            }
        }

        // Blinking Text Cursor Caret
        if (cursorBlink && !showOverview) {
            int curY = 38 + cursorLine * 18 - scrollY;
            int curX = 14 + cursorCol * 8;
            if (curY >= 32 && curY <= 324 && curX >= 10 && curX <= 228) {
                GUIFont::fillRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, LCD_X + curX, LCD_Y + curY - 1, 2, 10, 0xFF1C2216);
            }
        }

        // 5. Render Digital Ink Handwriting Strokes
        for (const auto& stroke : note.strokes) {
            for (size_t i = 0; i + 1 < stroke.points.size(); i++) {
                int p0x = stroke.points[i].x;
                int p0y = stroke.points[i].y - scrollY;
                int p1x = stroke.points[i + 1].x;
                int p1y = stroke.points[i + 1].y - scrollY;
                drawScreenThickLine(p0x, p0y, p1x, p1y, 0xFF1C2216);
            }
        }
    }

    // Active Drawing Stroke in real-time
    if (isStylusDrawing && activeStroke.points.size() > 1) {
        for (size_t i = 0; i + 1 < activeStroke.points.size(); i++) {
            int p0x = activeStroke.points[i].x;
            int p0y = activeStroke.points[i].y - scrollY;
            int p1x = activeStroke.points[i + 1].x;
            int p1y = activeStroke.points[i + 1].y - scrollY;
            drawScreenThickLine(p0x, p0y, p1x, p1y, 0xFF1C2216);
        }
    }

    // 6. Dialog Modals (Overview, Assist, Find, Virtual Keyboard)
    if (showOverview) {
        renderOverviewScreen();
    } else if (showAssist) {
        renderAssistDialog();
    } else if (showFind) {
        renderFindDialog();
    }

    if (showKeyboard) {
        renderVirtualKeyboard();
    }

    // 7. Silkscreen Glass Area (y: 336..383)
    const int SS_Y = LCD_Y + NewtonRunt::PORTRAIT_HEIGHT;
    GUIFont::fillRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, LCD_X, SS_Y, NewtonRunt::PORTRAIT_WIDTH, NewtonRunt::SILKSCREEN_HEIGHT, 0xFF8E9E7C);
    GUIFont::drawRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, LCD_X, SS_Y, NewtonRunt::PORTRAIT_WIDTH, 1, 0xFF252C20);

    // 6 Silkscreen Buttons: [Undo] [Find] [Assist] [Overview] [Up] [Down]
    struct SilkscreenBtn {
        int x, w;
        const char* label;
    };
    SilkscreenBtn btns[6] = {
        {8,   34, "Undo"},
        {46,  34, "Find"},
        {84,  34, "Asst"},
        {122, 36, "Over"},
        {164, 32, "^"},
        {200, 32, "v"}
    };

    for (int b = 0; b < 6; b++) {
        bool flash = (btnFlashFrames > 0 && activeSilkscreenBtn == b);
        uint32 bg = flash ? 0xFF2A3324 : 0xFF8E9E7C;
        uint32 fg = flash ? 0xFF9DB288 : 0xFF2A3324;
        uint32 border = flash ? 0xFF1C2216 : 0xFF5A6650;

        int bx = LCD_X + btns[b].x;
        int by = SS_Y + 6;
        int bw = btns[b].w;
        int bh = 36;

        GUIFont::fillRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, bx, by, bw, bh, bg);
        GUIFont::drawRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, bx, by, bw, bh, border);

        // Center label in button
        int textOffset = (bw - (int)strlen(btns[b].label) * 8) / 2;
        GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, bx + textOffset, by + 14, btns[b].label, fg, 0x00000000);
    }
}

void NewtonMachine::renderCasing() {
    std::fill(rawFramebuffer.begin(), rawFramebuffer.end(), 0xFF141923);

    // Apple Newton Casing Dimensions (Center in 640x512)
    const int CASE_X = 180;
    const int CASE_Y = 16;
    const int CASE_W = 280;
    const int CASE_H = 480;

    // Outer Bezel: Iconic Dark Slate Gray Rubberized Finish
    GUIFont::fillRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, CASE_X, CASE_Y, CASE_W, CASE_H, 0xFF282F38);
    GUIFont::drawRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, CASE_X, CASE_Y, CASE_W, CASE_H, 0xFF3D4653);
    GUIFont::drawRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, CASE_X + 1, CASE_Y + 1, CASE_W - 2, CASE_H - 2, 0xFF1C2229);

    // Speaker Grille at Top (Slots)
    for (int i = 0; i < 5; i++) {
        GUIFont::fillRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, CASE_X + 24 + i * 10, CASE_Y + 14, 6, 2, 0xFF181C22);
    }

    // Power Switch / LED at Top Right
    bool power = runt.isPoweredOn();
    GUIFont::fillRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, CASE_X + CASE_W - 54, CASE_Y + 10, 36, 12, 0xFF1A1F26);
    GUIFont::drawRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, CASE_X + CASE_W - 54, CASE_Y + 10, 36, 12, 0xFF4A5462);
    int knobX = power ? (CASE_X + CASE_W - 36) : (CASE_X + CASE_W - 52);
    GUIFont::fillRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, knobX, CASE_Y + 11, 16, 10, power ? 0xFF4CAF50 : 0xFF788496);

    // Apple Newton Script Logo
    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, CASE_X + 105, CASE_Y + 12, "Newton", 0xFF8E9BAE, 0xFF282F38);

    // LCD Inset Bezel Shadow
    GUIFont::drawRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, LCD_X - 2, LCD_Y - 2, NewtonRunt::PORTRAIT_WIDTH + 4, NewtonRunt::TOTAL_HEIGHT + 4, 0xFF14181E);
    GUIFont::drawRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, LCD_X - 1, LCD_Y - 1, NewtonRunt::PORTRAIT_WIDTH + 2, NewtonRunt::TOTAL_HEIGHT + 2, 0xFF353D4A);

    // Render 240x384 Newton Screen (Notepad, Ink, Typing & Silkscreen)
    renderNewtonScreen();

    // Stylus Cursor (Draw pen tip if mouse is over screen or pressed down)
    if (mouseX >= LCD_X && mouseX < LCD_X + NewtonRunt::PORTRAIT_WIDTH &&
        mouseY >= LCD_Y && mouseY < LCD_Y + NewtonRunt::TOTAL_HEIGHT) {
        uint32 penColor = mouseDown ? 0xFFE04040 : 0xFF2A3324;
        GUIFont::fillRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, mouseX - 1, mouseY - 1, 3, 3, penColor);
    }

    // Bottom Stylus Bay Dock Indicator
    GUIFont::fillRect(rawFramebuffer.data(), CANVAS_WIDTH * 4, CASE_X + CASE_W - 18, CASE_Y + CASE_H - 40, 6, 28, 0xFF181D24);

    // Left Sidebar: Hardware Specs & ARM Registers
    const int LX = 14;
    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, LX, 20, "APPLE NEWTON", 0xFF61AFEF, 0xFF141923);
    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, LX, 36, "MessagePad 120", 0xFF98C379, 0xFF141923);
    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, LX, 54, "ARM610 (32-bit)", 0xFFE5C07B, 0xFF141923);
    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, LX, 70, "Apple RUNT ASIC", 0xFFABB2BF, 0xFF141923);

    char regStr[64];
    snprintf(regStr, sizeof(regStr), "PC: 0x%08X", cpu.r[15]);
    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, LX, 94, regStr, 0xFFABB2BF, 0xFF141923);

    snprintf(regStr, sizeof(regStr), "LR: 0x%08X", cpu.r[14]);
    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, LX, 110, regStr, 0xFFABB2BF, 0xFF141923);

    snprintf(regStr, sizeof(regStr), "SP: 0x%08X", cpu.r[13]);
    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, LX, 126, regStr, 0xFFABB2BF, 0xFF141923);

    snprintf(regStr, sizeof(regStr), "CPSR: 0x%08X", cpu.cpsr);
    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, LX, 142, regStr, 0xFFABB2BF, 0xFF141923);

    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, LX, 175, "NOTE STATUS:", 0xFFE5C07B, 0xFF141923);
    if (currentNoteIdx >= 0 && currentNoteIdx < (int)notes.size()) {
        const auto& note = notes[currentNoteIdx];
        snprintf(regStr, sizeof(regStr), "Page: %d of %d", currentNoteIdx + 1, (int)notes.size());
        GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, LX, 195, regStr, 0xFFABB2BF, 0xFF141923);

        snprintf(regStr, sizeof(regStr), "Lines: %d", (int)note.lines.size());
        GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, LX, 211, regStr, 0xFFABB2BF, 0xFF141923);

        snprintf(regStr, sizeof(regStr), "Strokes: %d", (int)note.strokes.size());
        GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, LX, 227, regStr, 0xFFABB2BF, 0xFF141923);
    }

    // Right Sidebar: Controls & Instructions
    const int RX = 475;
    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, RX, 20, "INPUT & CONTROLS", 0xFF61AFEF, 0xFF141923);
    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, RX, 40, "Stylus : Mouse Drag Ink", 0xFF98C379, 0xFF141923);
    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, RX, 56, "Keyboard : Type & Enter", 0xFF98C379, 0xFF141923);
    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, RX, 72, "Silkscreen : Undo/View/^/v", 0xFFABB2BF, 0xFF141923);
    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, RX, 88, "[K] : On-screen Keyboard", 0xFFABB2BF, 0xFF141923);
    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, RX, 104, "F1 : Power Switch", 0xFFABB2BF, 0xFF141923);
    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, RX, 120, "F2 : Cycle ROM v13/v100", 0xFFABB2BF, 0xFF141923);
    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, RX, 136, "F12 : System Reset", 0xFFABB2BF, 0xFF141923);

    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, RX, 160, "Digitizer Status:", 0xFFE5C07B, 0xFF141923);
    snprintf(regStr, sizeof(regStr), "Pen: %s", mouseDown ? "DOWN (Writing)" : "UP (Hover)");
    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, RX, 176, regStr, mouseDown ? 0xFF98C379 : 0xFFABB2BF, 0xFF141923);

    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, RX, 204, "Loaded ROM:", 0xFF61AFEF, 0xFF141923);
    std::string rname = runt.getLoadedROMName();
    if (rname.length() > 18) rname = rname.substr(rname.length() - 18);
    GUIFont::drawString(rawFramebuffer.data(), CANVAS_WIDTH * 4, RX, 220, rname.c_str(), 0xFF98C379, 0xFF141923);
}

void NewtonMachine::updateAudio() {
    std::fill(audioBuffer.begin(), audioBuffer.end(), 0);

    if (clickSamplesRemaining > 0) {
        double phaseStep = (2.0 * M_PI * clickFreq) / 44100.0;
        for (size_t i = 0; i < audioBuffer.size() && clickSamplesRemaining > 0; i++, clickSamplesRemaining--) {
            double env = (double)clickSamplesRemaining / 160.0;
            int16 sample = (int16)(std::sin(clickPhase) * 9000.0 * env);
            audioBuffer[i] = sample;
            clickPhase += phaseStep;
        }
    }
}

void NewtonMachine::handleTypingChar(char c) {
    if (currentNoteIdx < 0 || currentNoteIdx >= (int)notes.size()) return;
    auto& note = notes[currentNoteIdx];
    while ((int)note.lines.size() <= cursorLine) {
        note.lines.push_back("");
    }
    if (cursorCol > (int)note.lines[cursorLine].length()) {
        cursorCol = (int)note.lines[cursorLine].length();
    }
    note.lines[cursorLine].insert(note.lines[cursorLine].begin() + cursorCol, c);
    cursorCol++;

    // Max 25 chars per line for clean margin
    if (cursorCol >= 25) {
        note.lines.insert(note.lines.begin() + cursorLine + 1, "");
        cursorLine++;
        cursorCol = 0;
        if (cursorLine * 18 - scrollY > 260) {
            scrollY += 18;
        }
    }

    cursorBlink = true;
    cursorBlinkTimer = 0;
}

void NewtonMachine::handleKeyDown(SDL_Keycode key, uint16 mod) {
    // 1. Function keys & System controls
    if (key == SDLK_F1) {
        runt.togglePower();
        triggerClickSound(600, 200);
        return;
    } else if (key == SDLK_F2) {
        cycleROM();
        triggerClickSound(900, 200);
        return;
    } else if (key == SDLK_F12 || ((mod & KMOD_CTRL) && key == SDLK_r)) {
        reset();
        triggerClickSound(700, 250);
        return;
    } else if ((mod & KMOD_CTRL) && key == SDLK_z) {
        // Undo
        if (currentNoteIdx >= 0 && currentNoteIdx < (int)notes.size()) {
            auto& note = notes[currentNoteIdx];
            if (!note.strokes.empty()) {
                note.strokes.pop_back();
                activeSilkscreenBtn = 0;
                btnFlashFrames = 6;
                triggerClickSound(800, 150);
                return;
            } else if (!note.lines.empty() && cursorLine < (int)note.lines.size() && cursorCol > 0) {
                note.lines[cursorLine].erase(cursorCol - 1, 1);
                cursorCol--;
                activeSilkscreenBtn = 0;
                btnFlashFrames = 6;
                triggerClickSound(800, 150);
                return;
            }
        }
        return;
    }

    if (!runt.isPoweredOn()) {
        // If powered off, waking up on any key
        runt.togglePower();
        triggerClickSound(600, 200);
        return;
    }

    if (currentNoteIdx < 0 || currentNoteIdx >= (int)notes.size()) return;
    auto& note = notes[currentNoteIdx];
    while ((int)note.lines.size() <= cursorLine) {
        note.lines.push_back("");
    }

    // Navigation
    if (key == SDLK_UP) {
        if (cursorLine > 0) {
            cursorLine--;
            if (cursorCol > (int)note.lines[cursorLine].length()) {
                cursorCol = (int)note.lines[cursorLine].length();
            }
            if (cursorLine * 18 - scrollY < 20) {
                scrollY = std::max(0, scrollY - 36);
            }
            cursorBlink = true;
            cursorBlinkTimer = 0;
            triggerClickSound(1200, 80);
        }
        return;
    } else if (key == SDLK_DOWN) {
        if (cursorLine + 1 < (int)note.lines.size()) {
            cursorLine++;
            if (cursorCol > (int)note.lines[cursorLine].length()) {
                cursorCol = (int)note.lines[cursorLine].length();
            }
            if (cursorLine * 18 - scrollY > 260) {
                scrollY += 36;
            }
            cursorBlink = true;
            cursorBlinkTimer = 0;
            triggerClickSound(1200, 80);
        }
        return;
    } else if (key == SDLK_LEFT) {
        if (cursorCol > 0) {
            cursorCol--;
        } else if (cursorLine > 0) {
            cursorLine--;
            cursorCol = (int)note.lines[cursorLine].length();
        }
        cursorBlink = true;
        cursorBlinkTimer = 0;
        triggerClickSound(1200, 80);
        return;
    } else if (key == SDLK_RIGHT) {
        if (cursorCol < (int)note.lines[cursorLine].length()) {
            cursorCol++;
        } else if (cursorLine + 1 < (int)note.lines.size()) {
            cursorLine++;
            cursorCol = 0;
        }
        cursorBlink = true;
        cursorBlinkTimer = 0;
        triggerClickSound(1200, 80);
        return;
    } else if (key == SDLK_HOME) {
        cursorCol = 0;
        cursorBlink = true;
        cursorBlinkTimer = 0;
        return;
    } else if (key == SDLK_END) {
        cursorCol = (int)note.lines[cursorLine].length();
        cursorBlink = true;
        cursorBlinkTimer = 0;
        return;
    } else if (key == SDLK_PAGEUP) {
        scrollY = std::max(0, scrollY - 72);
        return;
    } else if (key == SDLK_PAGEDOWN) {
        scrollY = std::min(400, scrollY + 72);
        return;
    }

    // Editing: Backspace
    if (key == SDLK_BACKSPACE) {
        if (cursorCol > 0) {
            note.lines[cursorLine].erase(cursorCol - 1, 1);
            cursorCol--;
            triggerClickSound(1000, 100);
        } else if (cursorLine > 0) {
            int prevLen = (int)note.lines[cursorLine - 1].length();
            note.lines[cursorLine - 1] += note.lines[cursorLine];
            note.lines.erase(note.lines.begin() + cursorLine);
            cursorLine--;
            cursorCol = prevLen;
            triggerClickSound(900, 120);
        }
        cursorBlink = true;
        cursorBlinkTimer = 0;
        return;
    }

    // Editing: Return / Enter
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        std::string after = note.lines[cursorLine].substr(cursorCol);
        note.lines[cursorLine] = note.lines[cursorLine].substr(0, cursorCol);
        note.lines.insert(note.lines.begin() + cursorLine + 1, after);
        cursorLine++;
        cursorCol = 0;
        if (cursorLine * 18 - scrollY > 260) {
            scrollY += 36;
        }
        cursorBlink = true;
        cursorBlinkTimer = 0;
        triggerClickSound(800, 150);
        return;
    }

    // Editing: Delete
    if (key == SDLK_DELETE) {
        if (cursorCol < (int)note.lines[cursorLine].length()) {
            note.lines[cursorLine].erase(cursorCol, 1);
            triggerClickSound(1000, 100);
        } else if (cursorLine + 1 < (int)note.lines.size()) {
            note.lines[cursorLine] += note.lines[cursorLine + 1];
            note.lines.erase(note.lines.begin() + cursorLine + 1);
            triggerClickSound(900, 120);
        }
        cursorBlink = true;
        cursorBlinkTimer = 0;
        return;
    }

    // Character typing:
    bool shift = (mod & (KMOD_SHIFT | KMOD_CAPS)) != 0;
    char ch = 0;
    if (key >= SDLK_a && key <= SDLK_z) {
        ch = shift ? ('A' + (key - SDLK_a)) : ('a' + (key - SDLK_a));
    } else if (key >= SDLK_0 && key <= SDLK_9) {
        const char* shiftSymbols = ")!@#$%^&*(";
        ch = shift ? shiftSymbols[key - SDLK_0] : ('0' + (key - SDLK_0));
    } else if (key == SDLK_SPACE) {
        ch = ' ';
    } else if (key == SDLK_MINUS) {
        ch = shift ? '_' : '-';
    } else if (key == SDLK_EQUALS) {
        ch = shift ? '+' : '=';
    } else if (key == SDLK_LEFTBRACKET) {
        ch = shift ? '{' : '[';
    } else if (key == SDLK_RIGHTBRACKET) {
        ch = shift ? '}' : ']';
    } else if (key == SDLK_SEMICOLON) {
        ch = shift ? ':' : ';';
    } else if (key == SDLK_QUOTE) {
        ch = shift ? '"' : '\'';
    } else if (key == SDLK_COMMA) {
        ch = shift ? '<' : ',';
    } else if (key == SDLK_PERIOD) {
        ch = shift ? '>' : '.';
    } else if (key == SDLK_SLASH) {
        ch = shift ? '?' : '/';
    } else if (key == SDLK_BACKSLASH) {
        ch = shift ? '|' : '\\';
    } else if (key == SDLK_BACKQUOTE) {
        ch = shift ? '~' : '`';
    }

    if (ch != 0) {
        handleTypingChar(ch);
        triggerClickSound(1200, 90);
    }
}

void NewtonMachine::handleKeyUp(SDL_Keycode key, uint16 mod) {
}

void NewtonMachine::handleVirtualKeyClick(int kx, int ky) {
    const int KB_X = 4;
    const int KB_Y = 236;
    const int KB_W = 232;

    // Close button
    if (kx >= KB_X + KB_W - 16 && kx <= KB_X + KB_W - 4 && ky >= KB_Y + 2 && ky <= KB_Y + 14) {
        showKeyboard = false;
        triggerClickSound(800, 150);
        return;
    }

    // Row 0: Q..P
    if (ky >= KB_Y + 18 && ky < KB_Y + 34) {
        const char* r0 = "QWERTYUIOP";
        int idx = (kx - (KB_X + 6)) / 22;
        if (idx >= 0 && idx < 10) {
            handleTypingChar(r0[idx]);
            lastVirtualKeyPressed = r0[idx];
            virtualKeyFlashFrames = 5;
            triggerClickSound(1200, 150);
        }
    }
    // Row 1: A..L
    else if (ky >= KB_Y + 36 && ky < KB_Y + 52) {
        const char* r1 = "ASDFGHJKL";
        int idx = (kx - (KB_X + 17)) / 22;
        if (idx >= 0 && idx < 9) {
            handleTypingChar(r1[idx]);
            lastVirtualKeyPressed = r1[idx];
            virtualKeyFlashFrames = 5;
            triggerClickSound(1200, 150);
        }
    }
    // Row 2: Del, Z..M, Clr
    else if (ky >= KB_Y + 54 && ky < KB_Y + 70) {
        if (kx >= KB_X + 6 && kx < KB_X + 32) {
            // Del / Backspace
            handleKeyDown(SDLK_BACKSPACE, 0);
            lastVirtualKeyPressed = '<';
            virtualKeyFlashFrames = 5;
            triggerClickSound(900, 150);
        } else if (kx >= KB_X + 36 && kx < KB_X + 190) {
            const char* r2 = "ZXCVBNM";
            int idx = (kx - (KB_X + 36)) / 22;
            if (idx >= 0 && idx < 7) {
                handleTypingChar(r2[idx]);
                lastVirtualKeyPressed = r2[idx];
                virtualKeyFlashFrames = 5;
                triggerClickSound(1200, 150);
            }
        } else if (kx >= KB_X + 194 && kx < KB_X + 226) {
            // Clr (Clear line)
            if (currentNoteIdx >= 0 && currentNoteIdx < (int)notes.size() &&
                cursorLine < (int)notes[currentNoteIdx].lines.size()) {
                notes[currentNoteIdx].lines[cursorLine].clear();
                cursorCol = 0;
            }
            triggerClickSound(600, 200);
        }
    }
    // Row 3: 123, Space, Return
    else if (ky >= KB_Y + 72 && ky < KB_Y + 92) {
        if (kx >= KB_X + 6 && kx < KB_X + 38) {
            handleTypingChar('1');
            triggerClickSound(1100, 150);
        } else if (kx >= KB_X + 42 && kx < KB_X + 156) {
            handleTypingChar(' ');
            lastVirtualKeyPressed = ' ';
            virtualKeyFlashFrames = 5;
            triggerClickSound(1000, 150);
        } else if (kx >= KB_X + 160 && kx < KB_X + 226) {
            handleKeyDown(SDLK_RETURN, 0);
            lastVirtualKeyPressed = '\n';
            virtualKeyFlashFrames = 5;
            triggerClickSound(800, 200);
        }
    }
}

void NewtonMachine::handleMouseMotion(int x, int y) {
    mouseX = x;
    mouseY = y;

    if (mouseDown && isStylusDrawing) {
        int screenX = mouseX - LCD_X;
        int screenY = mouseY - LCD_Y;

        if (screenX >= 0 && screenX < NewtonRunt::PORTRAIT_WIDTH &&
            screenY >= 30 && screenY < 336) {

            runt.touchDown(screenX, screenY);

            int dist = std::abs(screenX - prevPenX) + std::abs(screenY - prevPenY);
            if (dist >= 1) {
                activeStroke.points.push_back({(int16)screenX, (int16)(screenY + scrollY)});
                prevPenX = screenX;
                prevPenY = screenY;
            }
        }
    }
}

void NewtonMachine::handleMouseButton(uint8 button, bool pressed, int x, int y) {
    mouseX = x;
    mouseY = y;

    const int CASE_X = 180;
    const int CASE_Y = 16;
    const int CASE_W = 280;

    // Check if clicked on Power Switch knob (Top right of Newton case)
    if (pressed && x >= CASE_X + CASE_W - 54 && x <= CASE_X + CASE_W - 18 && y >= CASE_Y + 8 && y <= CASE_Y + 26) {
        runt.togglePower();
        triggerClickSound(600, 200);
        return;
    }

    int screenX = mouseX - LCD_X;
    int screenY = mouseY - LCD_Y;

    if (pressed) {
        mouseDown = true;

        if (screenX >= 0 && screenX < NewtonRunt::PORTRAIT_WIDTH &&
            screenY >= 0 && screenY < NewtonRunt::TOTAL_HEIGHT) {

            // Always forward touch down to genuine RUNT digitizer (ARM610 tablet interrupts)
            runt.touchDown(screenX, screenY);

            // 1. Silkscreen buttons area (screenY >= 336)
            if (screenY >= 336) {
                if (screenX >= 6 && screenX < 44) {
                    // [Undo]
                    activeSilkscreenBtn = 0;
                    btnFlashFrames = 6;
                    if (currentNoteIdx >= 0 && currentNoteIdx < (int)notes.size()) {
                        auto& note = notes[currentNoteIdx];
                        if (!note.strokes.empty()) {
                            note.strokes.pop_back();
                        } else if (!note.lines.empty() && cursorLine < (int)note.lines.size() && cursorCol > 0) {
                            note.lines[cursorLine].erase(cursorCol - 1, 1);
                            cursorCol--;
                        }
                    }
                    triggerClickSound(800, 150);
                    return;
                } else if (screenX >= 44 && screenX < 82) {
                    // [Find]
                    activeSilkscreenBtn = 1;
                    btnFlashFrames = 6;
                    showFind = !showFind;
                    showAssist = false;
                    showOverview = false;
                    triggerClickSound(1100, 120);
                    return;
                } else if (screenX >= 82 && screenX < 120) {
                    // [Assist]
                    activeSilkscreenBtn = 2;
                    btnFlashFrames = 6;
                    showAssist = !showAssist;
                    showFind = false;
                    showOverview = false;
                    triggerClickSound(1100, 120);
                    return;
                } else if (screenX >= 120 && screenX < 160) {
                    // [Overview]
                    activeSilkscreenBtn = 3;
                    btnFlashFrames = 6;
                    showOverview = !showOverview;
                    showFind = false;
                    showAssist = false;
                    triggerClickSound(1100, 120);
                    return;
                } else if (screenX >= 160 && screenX < 200) {
                    // [Scroll Up ^]
                    activeSilkscreenBtn = 4;
                    btnFlashFrames = 6;
                    scrollY = std::max(0, scrollY - 36);
                    triggerClickSound(1300, 100);
                    return;
                } else if (screenX >= 200 && screenX < 238) {
                    // [Scroll Down v]
                    activeSilkscreenBtn = 5;
                    btnFlashFrames = 6;
                    scrollY = std::min(400, scrollY + 36);
                    triggerClickSound(1300, 100);
                    return;
                }
            }

            // 2. Header Bar Area (screenY < 30)
            if (screenY < 30) {
                // Folder Tab: Unfiled / Overview
                if (screenX >= 6 && screenX <= 84) {
                    showOverview = !showOverview;
                    triggerClickSound(1000, 120);
                    return;
                }
                // Keyboard icon [K]
                if (screenX >= 214 && screenX <= 236) {
                    showKeyboard = !showKeyboard;
                    triggerClickSound(1100, 120);
                    return;
                }
            }

            // 3. Assist Dialog Click
            if (showAssist && screenY >= 68 && screenY <= 204 && screenX >= 16 && screenX <= 224) {
                if (screenY >= 102 && screenY <= 124) {
                    // Clear Note
                    if (currentNoteIdx >= 0 && currentNoteIdx < (int)notes.size()) {
                        notes[currentNoteIdx].lines.clear();
                        notes[currentNoteIdx].lines.push_back("");
                        notes[currentNoteIdx].strokes.clear();
                        cursorLine = 0;
                        cursorCol = 0;
                    }
                    showAssist = false;
                    triggerClickSound(500, 300);
                    return;
                } else if (screenY >= 130 && screenY <= 152) {
                    // Add Sample Memo
                    if (currentNoteIdx >= 0 && currentNoteIdx < (int)notes.size()) {
                        notes[currentNoteIdx].lines.push_back("Apple Newton MessagePad");
                        notes[currentNoteIdx].lines.push_back("Handwritten ink & text!");
                    }
                    showAssist = false;
                    triggerClickSound(900, 150);
                    return;
                } else if (screenY >= 158 && screenY <= 180) {
                    // Dismiss
                    showAssist = false;
                    triggerClickSound(800, 100);
                    return;
                }
            }

            // 4. Overview Click
            if (showOverview && screenY >= 36 && screenY <= 324 && screenX >= 8 && screenX <= 232) {
                int row = (screenY - 70) / 28;
                if (row >= 0 && row < (int)notes.size()) {
                    currentNoteIdx = row;
                    showOverview = false;
                    cursorLine = 0;
                    cursorCol = 0;
                    triggerClickSound(1200, 150);
                    return;
                } else if (row == (int)notes.size()) {
                    // New Note
                    NewtonNoteData newNote;
                    newNote.title = "Note " + std::to_string(notes.size() + 1);
                    newNote.lines = {""};
                    notes.push_back(newNote);
                    currentNoteIdx = (int)notes.size() - 1;
                    showOverview = false;
                    cursorLine = 0;
                    cursorCol = 0;
                    triggerClickSound(1400, 200);
                    return;
                }
            }

            // 5. Virtual Keyboard Click
            if (showKeyboard && screenY >= 236 && screenY <= 335) {
                handleVirtualKeyClick(screenX, screenY);
                return;
            }

            // 6. Active Notepad Writing Area (screenY >= 30 && screenY < 336)
            if (screenY >= 30 && screenY < 336) {
                // Place cursor at tapped text line and column
                int clickedLine = (screenY - 36 + scrollY) / 18;
                if (clickedLine < 0) clickedLine = 0;
                if (currentNoteIdx >= 0 && currentNoteIdx < (int)notes.size()) {
                    auto& note = notes[currentNoteIdx];
                    while ((int)note.lines.size() <= clickedLine) {
                        note.lines.push_back("");
                    }
                    cursorLine = clickedLine;
                    int clickedCol = (screenX - 14) / 8;
                    if (clickedCol < 0) clickedCol = 0;
                    if (clickedCol > (int)note.lines[cursorLine].length()) {
                        clickedCol = (int)note.lines[cursorLine].length();
                    }
                    cursorCol = clickedCol;
                    cursorBlink = true;
                    cursorBlinkTimer = 0;
                }

                // Start handwriting digital ink stroke
                isStylusDrawing = true;
                prevPenX = screenX;
                prevPenY = screenY;
                activeStroke.points.clear();
                activeStroke.points.push_back({(int16)screenX, (int16)(screenY + scrollY)});
                triggerClickSound(1300, 60);
            }
        }
    } else {
        // Mouse Up
        mouseDown = false;
        runt.touchUp();

        if (isStylusDrawing) {
            isStylusDrawing = false;
            if (activeStroke.points.size() > 1 && currentNoteIdx >= 0 && currentNoteIdx < (int)notes.size()) {
                notes[currentNoteIdx].strokes.push_back(activeStroke);
            }
            activeStroke.points.clear();
        }
    }
}
