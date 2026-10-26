#include "studio2_machine.h"
#include "embedded_roms.h"
#include "gui_font.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>

Studio2Machine::Studio2Machine() {
    screenBuffer.resize(640 * 512, 0xFF000000);
    audioBuffer.resize(44100 / 60, 0);
    init();
}

void Studio2Machine::init() {
    memory.fill(0);
    EmbeddedROMs::ensureDecrypted();
    std::memcpy(memory.data(), EmbeddedROMs::STUDIO2_ROM, 2048);

    if (!cartData.empty()) {
        if (cartData.size() <= 1024) {
            std::memcpy(&memory[0x0400], cartData.data(), cartData.size());
        } else {
            std::memcpy(&memory[0x0400], cartData.data(), std::min((size_t)1024, cartData.size()));
            if (cartData.size() > 1024) {
                std::memcpy(&memory[0x0A00], &cartData[1024], std::min((size_t)1536, cartData.size() - 1024));
            }
        }
    }

    reset();
}

void Studio2Machine::reset() {
    R.fill(0);
    D = 0;
    DF = 0;
    P = 0;
    X = 0;
    T = 0;
    Q = 0;
    IE = 1;
    screenEnabled = true;
    keyboardLatch = 0;
    clearPressed = false;
    for (int i = 0; i < 16; i++) {
        keyStateP1[i] = false;
        keyStateP2[i] = false;
    }
}

uint8 Studio2Machine::readMem(uint16 addr) {
    addr &= 0x0FFF;
    return memory[addr];
}

void Studio2Machine::writeMem(uint16 addr, uint8 val) {
    addr &= 0x0FFF;
    // Only RAM ($0800..$09FF) is writable
    if (addr >= 0x0800 && addr <= 0x09FF) {
        memory[addr] = val;
    }
}

void Studio2Machine::executeInstruction() {
    uint8 op = readMem(R[P]++);
    uint8 hi = op >> 4;
    uint8 lo = op & 0x0F;

    switch (hi) {
        case 0x0:
            if (lo == 0) R[P]--; // IDL: Idle
            else D = readMem(R[lo]); // LDN Rn
            break;
        case 0x1:
            R[lo]++; // INC Rn
            break;
        case 0x2:
            R[lo]--; // DEC Rn
            break;
        case 0x3: {
            // Short Branches
            uint8 target = readMem(R[P]++);
            bool take = false;
            switch (lo) {
                case 0x0: take = true; break; // BR
                case 0x1: take = (Q != 0); break; // BQ
                case 0x2: take = (D == 0); break; // BZ
                case 0x3: take = (DF != 0); break; // BDF
                case 0x4: take = true; break; // B1 (EF1=1)
                case 0x5: take = false; break; // B2
                case 0x6: take = keyStateP1[keyboardLatch]; break; // B3 (Player 1 Key)
                case 0x7: take = keyStateP2[keyboardLatch]; break; // B4 (Player 2 Key)
                case 0x8: take = false; break; // NBR
                case 0x9: take = (Q == 0); break; // BNQ
                case 0xA: take = (D != 0); break; // BNZ
                case 0xB: take = (DF == 0); break; // BNF
                case 0xC: take = false; break; // BN1
                case 0xD: take = true; break; // BN2
                case 0xE: take = !keyStateP1[keyboardLatch]; break; // BN3
                case 0xF: take = !keyStateP2[keyboardLatch]; break; // BN4
            }
            if (take) {
                R[P] = (R[P] & 0xFF00) | target;
            }
            break;
        }
        case 0x4:
            D = readMem(R[lo]++); // LDA Rn
            break;
        case 0x5:
            writeMem(R[lo], D); // STR Rn
            break;
        case 0x6:
            if (lo == 0) {
                R[X]++; // IRX
            } else if (lo == 1) {
                // OUT 1: Screen off
                screenEnabled = false;
            } else if (lo == 2) {
                // OUT 2: Keyboard latch
                keyboardLatch = D & 0x0F;
            } else if (lo == 9) {
                // INP 1: Screen on
                screenEnabled = true;
            }
            break;
        case 0x7:
            switch (lo) {
                case 0x0: { // RET
                    uint8 b = readMem(R[X]++);
                    P = b & 0x0F;
                    X = (b >> 4) & 0x0F;
                    IE = 1;
                    break;
                }
                case 0x1: { // DIS
                    uint8 b = readMem(R[X]++);
                    P = b & 0x0F;
                    X = (b >> 4) & 0x0F;
                    IE = 0;
                    break;
                }
                case 0x2: D = readMem(R[X]++); break; // LDXA
                case 0x3: writeMem(R[X]--, D); break; // STXD
                case 0x4: { // ADC
                    int sum = D + readMem(R[X]) + DF;
                    D = (uint8)sum;
                    DF = (sum > 0xFF) ? 1 : 0;
                    break;
                }
                case 0x5: { // SDB
                    int diff = readMem(R[X]) - D - (DF ^ 1);
                    D = (uint8)diff;
                    DF = (diff >= 0) ? 1 : 0;
                    break;
                }
                case 0x6: { // SHRC
                    uint8 carry = D & 1;
                    D = (D >> 1) | (DF << 7);
                    DF = carry;
                    break;
                }
                case 0x7: { // SMB
                    int diff = D - readMem(R[X]) - (DF ^ 1);
                    D = (uint8)diff;
                    DF = (diff >= 0) ? 1 : 0;
                    break;
                }
                case 0x8: writeMem(R[X], T); break; // SAV
                case 0x9: T = (X << 4) | P; writeMem(R[2], T); X = P; R[2]--; break; // MARK
                case 0xA: Q = 0; break; // REQ
                case 0xB: Q = 1; break; // SEQ
                case 0xC: { // ADCI
                    uint8 imm = readMem(R[P]++);
                    int sum = D + imm + DF;
                    D = (uint8)sum;
                    DF = (sum > 0xFF) ? 1 : 0;
                    break;
                }
                case 0xD: { // SDBI
                    uint8 imm = readMem(R[P]++);
                    int diff = imm - D - (DF ^ 1);
                    D = (uint8)diff;
                    DF = (diff >= 0) ? 1 : 0;
                    break;
                }
                case 0xE: { // SHLC
                    uint8 carry = (D >> 7) & 1;
                    D = (D << 1) | DF;
                    DF = carry;
                    break;
                }
                case 0xF: { // SMBI
                    uint8 imm = readMem(R[P]++);
                    int diff = D - imm - (DF ^ 1);
                    D = (uint8)diff;
                    DF = (diff >= 0) ? 1 : 0;
                    break;
                }
            }
            break;
        case 0x8:
            D = (uint8)(R[lo] & 0xFF); // GLO Rn
            break;
        case 0x9:
            D = (uint8)(R[lo] >> 8); // GHI Rn
            break;
        case 0xA:
            R[lo] = (R[lo] & 0xFF00) | D; // PLO Rn
            break;
        case 0xB:
            R[lo] = (R[lo] & 0x00FF) | ((uint16)D << 8); // PHI Rn
            break;
        case 0xC: {
            // Long branches & NOPs
            uint16 target = ((uint16)readMem(R[P]) << 8) | readMem(R[P] + 1);
            R[P] += 2;
            bool take = false;
            switch (lo) {
                case 0x0: take = true; break; // LBR
                case 0x1: take = (Q != 0); break; // LBQ
                case 0x2: take = (D == 0); break; // LBZ
                case 0x3: take = (DF != 0); break; // LBDF
                case 0x4: take = false; break; // NOP
                case 0x5: if (Q == 0) R[P] += 2; break; // LSNQ
                case 0x6: if (D != 0) R[P] += 2; break; // LSNZ
                case 0x7: if (DF == 0) R[P] += 2; break; // LSNF
                case 0x8: take = false; break; // NLBR
                case 0x9: take = (Q == 0); break; // LBNQ
                case 0xA: take = (D != 0); break; // LBNZ
                case 0xB: take = (DF == 0); break; // LBNF
                case 0xC: if (IE != 0) R[P] += 2; break; // LSIE
                case 0xD: if (Q != 0) R[P] += 2; break; // LSQ
                case 0xE: if (D == 0) R[P] += 2; break; // LSZ
                case 0xF: if (DF != 0) R[P] += 2; break; // LSDF
            }
            if (take) R[P] = target;
            break;
        }
        case 0xD:
            P = lo; // SEP Rn
            break;
        case 0xE:
            X = lo; // SEX Rn
            break;
        case 0xF:
            switch (lo) {
                case 0x0: D = readMem(R[X]); break; // LDX
                case 0x1: D |= readMem(R[X]); break; // OR
                case 0x2: D &= readMem(R[X]); break; // AND
                case 0x3: D ^= readMem(R[X]); break; // XOR
                case 0x4: { // ADD
                    int sum = D + readMem(R[X]);
                    D = (uint8)sum;
                    DF = (sum > 0xFF) ? 1 : 0;
                    break;
                }
                case 0x5: { // SD
                    int diff = readMem(R[X]) - D;
                    D = (uint8)diff;
                    DF = (diff >= 0) ? 1 : 0;
                    break;
                }
                case 0x6: { // SHR
                    DF = D & 1;
                    D >>= 1;
                    break;
                }
                case 0x7: { // SM
                    int diff = D - readMem(R[X]);
                    D = (uint8)diff;
                    DF = (diff >= 0) ? 1 : 0;
                    break;
                }
                case 0x8: D = readMem(R[P]++); break; // LDI
                case 0x9: D |= readMem(R[P]++); break; // ORI
                case 0xA: D &= readMem(R[P]++); break; // ANI
                case 0xB: D ^= readMem(R[P]++); break; // XRI
                case 0xC: { // ADI
                    int sum = D + readMem(R[P]++);
                    D = (uint8)sum;
                    DF = (sum > 0xFF) ? 1 : 0;
                    break;
                }
                case 0xD: { // SDI
                    uint8 imm = readMem(R[P]++);
                    int diff = imm - D;
                    D = (uint8)diff;
                    DF = (diff >= 0) ? 1 : 0;
                    break;
                }
                case 0xE: { // SHL
                    DF = (D >> 7) & 1;
                    D <<= 1;
                    break;
                }
                case 0xF: { // SMI
                    uint8 imm = readMem(R[P]++);
                    int diff = D - imm;
                    D = (uint8)diff;
                    DF = (diff >= 0) ? 1 : 0;
                    break;
                }
            }
            break;
    }
}

void Studio2Machine::runFrame() {
    // RCA Studio II 1.76MHz CPU runs ~1,876 execution cycles per 60Hz frame
    // CDP1861 Pixie triggers frame interrupt to refresh DMA pointer R0
    for (int cycles = 0; cycles < 2500; cycles++) {
        executeInstruction();
    }

    // CDP1861 Frame Display Interrupt
    if (IE && screenEnabled) {
        T = (X << 4) | P;
        P = 1; // Interrupt service pointer in Studio II
        X = 2;
        IE = 0;
        // DMA updates R0 to point to $0900 screen memory
        R[0] = 0x0900;
    }

    renderPixieDisplay();
    updateAudio();
}

void Studio2Machine::updateAudio() {
    int freq = Q ? 800 : 0;
    for (size_t i = 0; i < audioBuffer.size(); i++) {
        int16 sample = 0;
        if (freq > 0) {
            tonePhase = (tonePhase + freq) % 44100;
            sample = (tonePhase < 22050) ? 8000 : -8000;
        }
        audioBuffer[i] = sample;
    }
}

void Studio2Machine::renderPixieDisplay() {
    // 640x512 Canvas setup
    // Retro warm woodgrain console frame + CRT monitor
    uint32 woodColor = 0xFF3D2314;
    uint32 bezelColor = 0xFF2A2E35;
    uint32 tvScreenBg = 0xFF080C08; // Deep CRT black/green tint
    uint32 pixelColor = 0xFFE0ECE0; // Bright CRT white/phosphor

    for (int y = 0; y < 512; y++) {
        for (int x = 0; x < 640; x++) {
            screenBuffer[y * 640 + x] = 0xFF140C08; // Woodgrain casing background
        }
    }

    // Console TV Bezel
    GUIFont::fillRect(screenBuffer.data(), 640, 40, 20, 560, 472, bezelColor);
    GUIFont::drawRect(screenBuffer.data(), 640, 40, 20, 560, 472, 0xFF4A5260);

    // CRT Screen Area: 64x32 scaled 7x -> 448 x 224 pixels, centered at (96, 60)
    int crtX = 96;
    int crtY = 60;
    int scaleX = 7;
    int scaleY = 7;
    GUIFont::fillRect(screenBuffer.data(), 640, crtX - 8, crtY - 8, 64 * scaleX + 16, 32 * scaleY + 16, tvScreenBg);
    GUIFont::drawRect(screenBuffer.data(), 640, crtX - 8, crtY - 8, 64 * scaleX + 16, 32 * scaleY + 16, 0xFF1E2820);

    // Read Pixie video buffer at $0900..$09FF (256 bytes)
    for (int py = 0; py < 32; py++) {
        for (int byteCol = 0; byteCol < 8; byteCol++) {
            uint8 b = memory[0x0900 + py * 8 + byteCol];
            for (int bit = 0; bit < 8; bit++) {
                int px = byteCol * 8 + bit;
                bool on = (b >> (7 - bit)) & 1;
                uint32 col = on ? pixelColor : tvScreenBg;

                for (int sy = 0; sy < scaleY; sy++) {
                    for (int sx = 0; sx < scaleX; sx++) {
                        int bx = crtX + px * scaleX + sx;
                        int by = crtY + py * scaleY + sy;
                        if (by < 512 && bx < 640) {
                            // Subtle scanline dimming
                            if (sy == scaleY - 1) {
                                screenBuffer[by * 640 + bx] = on ? 0xFF90A490 : 0xFF040604;
                            } else {
                                screenBuffer[by * 640 + bx] = col;
                            }
                        }
                    }
                }
            }
        }
    }

    // Console Label & Built-in Game Instructions below CRT
    GUIFont::drawString(screenBuffer.data(), 640, 230, 316, "RCA STUDIO II", 0xFFE0C060);
    GUIFont::drawString(screenBuffer.data(), 640, 200, 336, "COSMAC 1802 MICROPROCESSOR", 0xFF9AA4B8);

    GUIFont::drawString(screenBuffer.data(), 640, 80, 370, "[1] DOODLE (Cizim)     [2] PATTERNS (Desen)", 0xFFD0D8E6);
    GUIFont::drawString(screenBuffer.data(), 640, 80, 395, "[3] BOWLING (Kuka)     [4] SPACE WAR (Savas)", 0xFFD0D8E6);
    GUIFont::drawString(screenBuffer.data(), 640, 80, 420, "[5] FREEWAY (Otoyol)   [CLEAR / C] Ana Menu", 0xFFD0D8E6);

    GUIFont::drawString(screenBuffer.data(), 640, 80, 455, "Sol Klavye: 1-9, 0 (Oyuncu 1)  |  Sag Klavye: Q,W,E,A,S,D,Z,X,C (Oyuncu 2)", 0xFF88A0C8);
    GUIFont::drawString(screenBuffer.data(), 640, 80, 475, "F12: Konsol Reset  |  Ses: Dahili 1-bit Beeper / Q Output", 0xFF7084A4);
}

void Studio2Machine::handleKeyDown(SDL_Keycode key, uint16 mod) {
    (void)mod;
    // Clear key resets to menu
    if (key == SDLK_c || key == SDLK_BACKSPACE || key == SDLK_ESCAPE) {
        clearPressed = true;
        reset();
        return;
    }

    // Player 1 numeric keypad (0..9)
    if (key >= SDLK_0 && key <= SDLK_9) {
        keyStateP1[key - SDLK_0] = true;
    }

    // Player 2 controls (mapped to Q, W, E, A, S, D, Z, X, C, V)
    if (key == SDLK_x) keyStateP2[0] = true;
    if (key == SDLK_1 || key == SDLK_q) keyStateP2[1] = true;
    if (key == SDLK_2 || key == SDLK_w) keyStateP2[2] = true;
    if (key == SDLK_3 || key == SDLK_e) keyStateP2[3] = true;
    if (key == SDLK_4 || key == SDLK_a) keyStateP2[4] = true;
    if (key == SDLK_5 || key == SDLK_s) keyStateP2[5] = true;
    if (key == SDLK_6 || key == SDLK_d) keyStateP2[6] = true;
    if (key == SDLK_7 || key == SDLK_z) keyStateP2[7] = true;
    if (key == SDLK_8 || key == SDLK_u) keyStateP2[8] = true;
    if (key == SDLK_9 || key == SDLK_i) keyStateP2[9] = true;
}

void Studio2Machine::handleKeyUp(SDL_Keycode key, uint16 mod) {
    (void)mod;
    if (key >= SDLK_0 && key <= SDLK_9) {
        keyStateP1[key - SDLK_0] = false;
    }
    if (key == SDLK_x) keyStateP2[0] = false;
    if (key == SDLK_1 || key == SDLK_q) keyStateP2[1] = false;
    if (key == SDLK_2 || key == SDLK_w) keyStateP2[2] = false;
    if (key == SDLK_3 || key == SDLK_e) keyStateP2[3] = false;
    if (key == SDLK_4 || key == SDLK_a) keyStateP2[4] = false;
    if (key == SDLK_5 || key == SDLK_s) keyStateP2[5] = false;
    if (key == SDLK_6 || key == SDLK_d) keyStateP2[6] = false;
    if (key == SDLK_7 || key == SDLK_z) keyStateP2[7] = false;
    if (key == SDLK_8 || key == SDLK_u) keyStateP2[8] = false;
    if (key == SDLK_9 || key == SDLK_i) keyStateP2[9] = false;
}

bool Studio2Machine::loadMedia(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    cartData = std::vector<uint8>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    std::string fname = path.substr(path.find_last_of("/\\") + 1);
    currentMedia = fname;
    init();
    std::cout << "[RCA Studio II] Loaded cartridge: " << currentMedia << " (" << cartData.size() << " bytes)\n";
    return true;
}
