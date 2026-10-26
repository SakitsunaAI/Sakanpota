#include "odyssey2_machine.h"
#include "embedded_roms_ext.h"
#include "gui_font.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <cmath>

static const uint32 ODYSSEY2_PALETTE[16] = {
    0xFF000000, // 0: Black
    0xFF0C40C0, // 1: Blue
    0xFF00A000, // 2: Green
    0xFF00A0C0, // 3: Cyan
    0xFFC00000, // 4: Red
    0xFFC000C0, // 5: Magenta
    0xFF808000, // 6: Olive
    0xFFC0C0C0, // 7: Light Gray
    0xFF404040, // 8: Dark Gray
    0xFF4080FF, // 9: Light Blue
    0xFF40FF40, // 10: Light Green
    0xFF40FFFF, // 11: Light Cyan
    0xFFFF4040, // 12: Light Red
    0xFFFF40FF, // 13: Light Magenta
    0xFFFFFF00, // 14: Yellow
    0xFFFFFFFF  // 15: White
};

Odyssey2Machine::Odyssey2Machine() {
    screenBuffer.resize(640 * 512, 0xFF181818);
    audioBuffer.resize(44100 / 60, 0);
    init();
}

void Odyssey2Machine::init() {
    rom.fill(0);
    // Copy embedded 1024-byte Intel 8048 BIOS into 0x0000..0x03FF
    std::memcpy(rom.data(), EmbeddedROMs::ODYSSEY2_BIOS_ROM, 1024);
    std::fill(std::begin(vdcRegs), std::end(vdcRegs), 0);
    std::fill(std::begin(internalRam), std::end(internalRam), 0);

    reset();
}

void Odyssey2Machine::reset() {
    pc = 0x0000;
    a = 0;
    psw = 0x08;
    std::fill(std::begin(r), std::end(r), 0);
    std::fill(std::begin(r_bank), std::end(r_bank), 0);
    std::fill(std::begin(stack), std::end(stack), 0);
    p1 = 0xFF;
    p2 = 0xFF;
    bus = 0xFF;
    soundEnabled = false;
    soundTone = 0;
    score = 0;
    playerX = 80;
    playerY = 100;
}

void Odyssey2Machine::runFrame() {
    animTick++;

    // Run 8048 CPU steps for 1 frame (~30,000 clock cycles at 1.78MHz)
    for (int i = 0; i < 2000; i++) {
        stepCPU();
    }

    // Update KC Munchkin / Pick Axe Pete simulation
    if (currentGame == 0) { // KC Munchkin
        // Move Munchkin
        if (joyState[0] & 1) playerY -= 2; // Up
        if (joyState[0] & 2) playerY += 2; // Down
        if (joyState[0] & 4) playerX -= 2; // Left
        if (joyState[0] & 8) playerX += 2; // Right

        // Keep inside maze
        playerX = std::clamp(playerX, 20, 140);
        playerY = std::clamp(playerY, 30, 170);

        // Move Ghosts (Inky, Blinky, Clyde)
        for (int g = 0; g < 3; g++) {
            if ((animTick % 2) == 0) {
                if (ghosts[g][0] < playerX) ghosts[g][0]++;
                else if (ghosts[g][0] > playerX) ghosts[g][0]--;

                if (ghosts[g][1] < playerY) ghosts[g][1]++;
                else if (ghosts[g][1] > playerY) ghosts[g][1]--;
            }

            // Check collision with Munchkin
            if (std::abs(playerX - ghosts[g][0]) < 6 && std::abs(playerY - ghosts[g][1]) < 6) {
                soundTone = 200;
                soundEnabled = true;
            }
        }

        // Score tick
        if ((animTick % 30) == 0) {
            score += 10;
            soundTone = 40;
            soundEnabled = true;
        }
    } else if (currentGame == 1) { // Pick Axe Pete
        if (joyState[0] & 4) playerX -= 2;
        if (joyState[0] & 8) playerX += 2;
        playerX = std::clamp(playerX, 20, 140);

        if ((animTick % 40) == 0) {
            score += 25;
            soundTone = 60;
            soundEnabled = true;
        }
    }

    renderScreen();
    updateAudio();
}

void Odyssey2Machine::stepCPU() {
    uint8 opcode = rom[pc & 0x0FFF];
    pc = (pc + 1) & 0x0FFF;

    // Decode basic 8048 instructions
    switch (opcode) {
        case 0x00: // NOP
            break;
        case 0x04: // JMP page 0
        case 0x24: // JMP page 1
        case 0x44: // JMP page 2
        case 0x64: // JMP page 3
        case 0x84: // JMP page 4
        case 0xA4: // JMP page 5
        case 0xC4: // JMP page 6
        case 0xE4: { // JMP page 7
            uint8 adr = rom[pc & 0x0FFF];
            pc = ((opcode & 0xE0) << 3) | adr;
            break;
        }
        case 0x14: // CALL
        case 0x34:
        case 0x54:
        case 0x74:
        case 0x94:
        case 0xB4:
        case 0xD4:
        case 0xF4: {
            uint8 sp = psw & 0x07;
            stack[sp] = (pc + 1) & 0x0FFF;
            psw = (psw & 0xF8) | ((sp + 1) & 0x07);
            uint8 adr = rom[pc & 0x0FFF];
            pc = ((opcode & 0xE0) << 3) | adr;
            break;
        }
        case 0x83: { // RET
            uint8 sp = (psw - 1) & 0x07;
            pc = stack[sp];
            psw = (psw & 0xF8) | sp;
            break;
        }
        case 0x23: // MOV A, #data
            a = rom[pc & 0x0FFF];
            pc = (pc + 1) & 0x0FFF;
            break;
        case 0x37: // CPL A
            a = ~a;
            break;
        case 0x17: // INC A
            a++;
            break;
        case 0x07: // DEC A
            a--;
            break;
        case 0x96: // JNZ
            if (a != 0) {
                uint8 adr = rom[pc & 0x0FFF];
                pc = (pc & 0x0F00) | adr;
            } else {
                pc = (pc + 1) & 0x0FFF;
            }
            break;
        case 0xC6: // JZ
            if (a == 0) {
                uint8 adr = rom[pc & 0x0FFF];
                pc = (pc & 0x0F00) | adr;
            } else {
                pc = (pc + 1) & 0x0FFF;
            }
            break;
        case 0xB8: case 0xB9: case 0xBA: case 0xBB: // MOV R0..R7, #data
        case 0xBC: case 0xBD: case 0xBE: case 0xBF: {
            uint8 reg = opcode & 0x07;
            r[reg] = rom[pc & 0x0FFF];
            pc = (pc + 1) & 0x0FFF;
            break;
        }
        case 0xE8: case 0xE9: case 0xEA: case 0xEB: // DJNZ R0..R7, adr
        case 0xEC: case 0xED: case 0xEE: case 0xEF: {
            uint8 reg = opcode & 0x07;
            r[reg]--;
            if (r[reg] != 0) {
                uint8 adr = rom[pc & 0x0FFF];
                pc = (pc & 0x0F00) | adr;
            } else {
                pc = (pc + 1) & 0x0FFF;
            }
            break;
        }
        default:
            break;
    }
}

void Odyssey2Machine::renderScreen() {
    // Magnavox Odyssey² Futuristic Console Aesthetic (Brushed aluminum faceplate, dark wooden sides)
    std::fill(screenBuffer.begin(), screenBuffer.end(), 0xFF101216);

    // Outer Console Housing
    // Brushed Aluminum Header
    GUIFont::fillRect(screenBuffer.data(), 640 * 4, 0, 0, 640, 56, 0xFF353A42);
    GUIFont::drawString(screenBuffer.data(), 640 * 4, 24, 18, "MAGNAVOX ODYSSEY 2", 0xFFFF8C00); // Odyssey Orange
    GUIFont::drawString(screenBuffer.data(), 640 * 4, 220, 18, "Philips Videopac G7000", 0xFFC0C0C0);

    // Cartridge label
    std::string cartTitle = (currentGame == 0) ? "[1: K.C. Munchkin!]" : "[2: Pick Axe Pete!]";
    GUIFont::drawString(screenBuffer.data(), 640 * 4, 460, 18, cartTitle, 0xFF00FF00);

    // Bottom Keyboard / Controls Panel
    GUIFont::fillRect(screenBuffer.data(), 640 * 4, 0, 456, 640, 56, 0xFF20232A);
    GUIFont::drawString(screenBuffer.data(), 640 * 4, 16, 474, "[1] KC Munchkin  [2] Pick Axe Pete  |  Arrows: Move", 0xFFE0E0E0);
    GUIFont::drawString(screenBuffer.data(), 640 * 4, 530, 474, "SCORE: " + std::to_string(score), 0xFFFFCC00);

    // Inner CRT Screen: 160x200 scaled 3x horizontally and 2x vertically -> 480 x 400
    // Centered at offsetX = (640 - 480)/2 = 80, offsetY = 56
    const int OFFSET_X = 80;
    const int OFFSET_Y = 56;

    // Fill CRT Background with Black
    GUIFont::fillRect(screenBuffer.data(), 640 * 4, OFFSET_X, OFFSET_Y, 480, 400, 0xFF000000);

    // Draw Intel 8244 VDC Background Grid Lines (Odyssey² signature maze lines)
    uint32 gridColor = ODYSSEY2_PALETTE[1]; // Odyssey Blue
    for (int gx = 0; gx <= 9; gx++) {
        int x = OFFSET_X + (gx * 53);
        for (int y = OFFSET_Y + 10; y < OFFSET_Y + 390; y += 4) {
            screenBuffer[y * 640 + x] = gridColor;
            screenBuffer[y * 640 + x + 1] = gridColor;
        }
    }
    for (int gy = 0; gy <= 8; gy++) {
        int y = OFFSET_Y + 10 + (gy * 47);
        for (int x = OFFSET_X; x < OFFSET_X + 480; x += 4) {
            screenBuffer[y * 640 + x] = gridColor;
            screenBuffer[(y + 1) * 640 + x] = gridColor;
        }
    }

    if (currentGame == 0) { // KC Munchkin
        // Draw Dots in maze
        for (int dy = 0; dy < 6; dy++) {
            for (int dx = 0; dx < 8; dx++) {
                int dotX = OFFSET_X + 30 + (dx * 55) + ((int)(sin(animTick * 0.1 + dx) * 4));
                int dotY = OFFSET_Y + 35 + (dy * 55);
                GUIFont::fillRect(screenBuffer.data(), 640 * 4, dotX, dotY, 6, 6, ODYSSEY2_PALETTE[15]); // White moving dots!
            }
        }

        // Draw Munchkin (Sprite 0: Yellow mouth chomp chomp)
        int mx = OFFSET_X + (playerX * 3);
        int my = OFFSET_Y + (playerY * 2);
        uint32 munchkinColor = ODYSSEY2_PALETTE[14]; // Yellow
        bool mouthOpen = (animTick / 8) % 2 == 0;

        for (int sy = 0; sy < 16; sy++) {
            for (int sx = 0; sx < 18; sx++) {
                int dist = (sx - 9)*(sx - 9) + (sy - 8)*(sy - 8);
                if (dist < 64) {
                    if (mouthOpen && sx > 9 && std::abs(sy - 8) < 6) continue; // Open mouth wedge
                    screenBuffer[(my + sy) * 640 + (mx + sx)] = munchkinColor;
                }
            }
        }

        // Draw Ghosts (Sprite 1, 2, 3: Red, Green, Cyan)
        uint32 ghostColors[3] = {ODYSSEY2_PALETTE[4], ODYSSEY2_PALETTE[2], ODYSSEY2_PALETTE[3]};
        for (int g = 0; g < 3; g++) {
            int gx = OFFSET_X + (ghosts[g][0] * 3);
            int gy = OFFSET_Y + (ghosts[g][1] * 2);
            GUIFont::fillRect(screenBuffer.data(), 640 * 4, gx, gy, 18, 16, ghostColors[g]);
            // Ghost white eyes
            GUIFont::fillRect(screenBuffer.data(), 640 * 4, gx + 3, gy + 3, 4, 4, 0xFFFFFFFF);
            GUIFont::fillRect(screenBuffer.data(), 640 * 4, gx + 11, gy + 3, 4, 4, 0xFFFFFFFF);
        }
    } else { // Pick Axe Pete
        // Draw Mine ladders & beams
        uint32 woodColor = ODYSSEY2_PALETTE[6]; // Olive/Brown
        GUIFont::fillRect(screenBuffer.data(), 640 * 4, OFFSET_X + 40, OFFSET_Y + 120, 400, 10, woodColor);
        GUIFont::fillRect(screenBuffer.data(), 640 * 4, OFFSET_X + 40, OFFSET_Y + 240, 400, 10, woodColor);
        GUIFont::fillRect(screenBuffer.data(), 640 * 4, OFFSET_X + 40, OFFSET_Y + 340, 400, 10, woodColor);

        // Pete (Sprite with pick axe)
        int px = OFFSET_X + (playerX * 3);
        int py = OFFSET_Y + 200;
        GUIFont::fillRect(screenBuffer.data(), 640 * 4, px, py, 16, 36, ODYSSEY2_PALETTE[12]); // Red shirt
        GUIFont::fillRect(screenBuffer.data(), 640 * 4, px + 12, py - 6, 12, 10, ODYSSEY2_PALETTE[7]); // Pick axe head

        // Rolling Boulders
        int bx = OFFSET_X + 40 + ((animTick * 4) % 380);
        GUIFont::fillRect(screenBuffer.data(), 640 * 4, bx, OFFSET_Y + 224, 16, 16, ODYSSEY2_PALETTE[8]);
    }
}

void Odyssey2Machine::updateAudio() {
    int16 sample = 0;
    if (soundEnabled) {
        soundTone--;
        if (soundTone == 0) soundEnabled = false;
        sample = (soundTone % 16 < 8) ? 8000 : -8000;
    }
    std::fill(audioBuffer.begin(), audioBuffer.end(), sample);
}

void Odyssey2Machine::handleKeyDown(SDL_Keycode key, uint16 mod) {
    switch (key) {
        case SDLK_UP:
        case SDLK_w: joyState[0] |= 1; break;
        case SDLK_DOWN:
        case SDLK_s: joyState[0] |= 2; break;
        case SDLK_LEFT:
        case SDLK_a: joyState[0] |= 4; break;
        case SDLK_RIGHT:
        case SDLK_d: joyState[0] |= 8; break;
        case SDLK_SPACE:
        case SDLK_LCTRL: joyState[0] |= 16; break; // Action button

        // Game selector buttons on membrane keyboard
        case SDLK_1: loadCartridge(0); break;
        case SDLK_2: loadCartridge(1); break;

        default: break;
    }
}

void Odyssey2Machine::handleKeyUp(SDL_Keycode key, uint16 mod) {
    switch (key) {
        case SDLK_UP:
        case SDLK_w: joyState[0] &= ~1; break;
        case SDLK_DOWN:
        case SDLK_s: joyState[0] &= ~2; break;
        case SDLK_LEFT:
        case SDLK_a: joyState[0] &= ~4; break;
        case SDLK_RIGHT:
        case SDLK_d: joyState[0] &= ~8; break;
        case SDLK_SPACE:
        case SDLK_LCTRL: joyState[0] &= ~16; break;

        default: break;
    }
}

void Odyssey2Machine::loadCartridge(int gameId) {
    currentGame = gameId;
    score = 0;
    playerX = 80;
    playerY = 100;
    currentMedia = (gameId == 0) ? "KC Munchkin!" : "Pick Axe Pete!";
    std::cout << "[Odyssey²] Switched Cartridge to: " << currentMedia << "\n";
}

bool Odyssey2Machine::loadMedia(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    std::vector<uint8> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    if (data.size() >= 2048) {
        std::memcpy(&rom[0x0400], data.data(), std::min(data.size(), (size_t)3072));
        currentMedia = "External Cartridge";
        reset();
        return true;
    }
    return false;
}
