#include "tamagotchi_machine.h"
#include "embedded_roms.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <cmath>

static constexpr int CANVAS_W = 640;
static constexpr int CANVAS_H = 512;

TamagotchiMachine::TamagotchiMachine(TamagotchiGen gen) : generation(gen) {
    screenBuffer.resize(CANVAS_W * CANVAS_H, 0xFF000000);
    audioBuffer.resize(882, 0);
}

void TamagotchiMachine::init() {
    // Unpack embedded genuine 12-bit Tamagotchi ROM
    const uint8* rawRom = (generation == TamagotchiGen::Gen1_P1) ?
                          EmbeddedROMs::TAMAGOTCHI_P1_ROM : EmbeddedROMs::TAMAGOTCHI_P2_ROM;

    for (size_t i = 0; i < romWords.size(); i++) {
        // Words are packed 12-bit in big-endian 2-byte pairs
        uint16 w = rawRom[i * 2 + 1] | (((uint16)rawRom[i * 2] & 0x0F) << 8);
        romWords[i] = w;
    }
    ram.fill(0x00);
    std::memset(lcdMatrix, 0, sizeof(lcdMatrix));

    reset();
}

void TamagotchiMachine::reset() {
    stage = PetStage::Egg;
    ageHours = 0;
    weightLbs = 5;
    hungerHearts = 4;
    happyHearts = 4;
    disciplineLevel = 0;
    isSleeping = false;
    isSick = false;
    hasPoop = false;
    inGame = false;
    inStats = false;
    statsPage = 0;
    selectedIcon = -1;
    callAttention = false;
    animTick = 0;
    buzzerTicks = 0;
    buttonA = false;
    buttonB = false;
    buttonC = false;
}

void TamagotchiMachine::runFrame() {
    animTick++;
    updatePetLogic();

    if (buzzerTicks > 0) buzzerTicks--;

    renderTamagotchiScreen();
    updateAudio();
}

void TamagotchiMachine::updatePetLogic() {
    // Egg hatching logic (hatches after ~10 seconds = 500 frames)
    if (stage == PetStage::Egg) {
        if (animTick > 300) {
            stage = PetStage::Baby;
            buzzerTicks = 30; // Birth chime!
        }
    }

    // Natural hunger / happiness decay over time
    if (stage != PetStage::Egg) {
        if (animTick % 1200 == 0) { // Every 24 seconds
            if (hungerHearts > 0) hungerHearts--;
            if (happyHearts > 0) happyHearts--;
            if (hungerHearts == 0 || happyHearts == 0) {
                callAttention = true;
                buzzerTicks = 15;
            }
        }
        if (animTick % 3000 == 0 && !hasPoop) {
            hasPoop = true;
            callAttention = true;
        }
    }

    // Clear LCD buffer
    std::memset(lcdMatrix, 0, sizeof(lcdMatrix));

    auto setPixel = [this](int x, int y) {
        if (x >= 0 && x < 32 && y >= 0 && y < 16) {
            lcdMatrix[y * 4 + (x / 8)] |= (0x80 >> (x % 8));
        }
    };

    if (inStats) {
        // Display health stats page (Hearts / Weight / Age)
        if (statsPage == 0) {
            // Hunger Hearts
            for (int h = 0; h < 4; h++) {
                int cx = 4 + h * 7;
                if (h < hungerHearts) {
                    setPixel(cx + 1, 4); setPixel(cx + 3, 4);
                    setPixel(cx, 5); setPixel(cx + 1, 5); setPixel(cx + 2, 5); setPixel(cx + 3, 5); setPixel(cx + 4, 5);
                    setPixel(cx + 1, 6); setPixel(cx + 2, 6); setPixel(cx + 3, 6);
                    setPixel(cx + 2, 7);
                } else {
                    // Empty heart outline
                    setPixel(cx + 1, 4); setPixel(cx + 3, 4);
                    setPixel(cx, 5); setPixel(cx + 4, 5);
                    setPixel(cx + 1, 6); setPixel(cx + 3, 6);
                    setPixel(cx + 2, 7);
                }
            }
        } else {
            // Happy Hearts
            for (int h = 0; h < 4; h++) {
                int cx = 4 + h * 7;
                if (h < happyHearts) {
                    setPixel(cx + 1, 10); setPixel(cx + 3, 10);
                    setPixel(cx, 11); setPixel(cx + 1, 11); setPixel(cx + 2, 11); setPixel(cx + 3, 11); setPixel(cx + 4, 11);
                    setPixel(cx + 1, 12); setPixel(cx + 2, 12); setPixel(cx + 3, 12);
                    setPixel(cx + 2, 13);
                }
            }
        }
        return;
    }

    // Render Egg / Pet Sprite
    int frame = (animTick / 15) % 2;

    if (stage == PetStage::Egg) {
        // Wobbling egg
        int eggX = 13 + (frame == 1 ? 1 : 0);
        int eggY = 4;
        static const uint8 eggPattern[8] = {
            0b00111100, 0b01111110, 0b11011011, 0b11111111,
            0b11111111, 0b11111111, 0b01111110, 0b00111100
        };
        for (int y = 0; y < 8; y++) {
            uint8 row = eggPattern[y];
            for (int x = 0; x < 8; x++) {
                if (row & (0x80 >> x)) setPixel(eggX + x, eggY + y);
            }
        }
    } else {
        // Animated Pet (Babytchi / Marutchi)
        int petX = 12 + (int)(sin(animTick * 0.08) * 3);
        int petY = 5 + (frame == 1 ? -1 : 0);

        // 8x8 Pet body
        static const uint8 petPattern[8] = {
            0b00111100, 0b01111110, 0b11011011, 0b11111111,
            0b11111111, 0b01100110, 0b00111100, 0b01000010
        };
        for (int y = 0; y < 8; y++) {
            uint8 row = petPattern[y];
            for (int x = 0; x < 8; x++) {
                if (row & (0x80 >> x)) setPixel(petX + x, petY + y);
            }
        }

        // Draw Poop if present
        if (hasPoop) {
            int poopX = 25;
            int poopY = 9;
            setPixel(poopX + 1, poopY);
            setPixel(poopX, poopY + 1); setPixel(poopX + 1, poopY + 1); setPixel(poopX + 2, poopY + 1);
            setPixel(poopX, poopY + 2); setPixel(poopX + 1, poopY + 2); setPixel(poopX + 2, poopY + 2);
        }

        // Sleeping Zzz
        if (isSleeping) {
            setPixel(petX + 8, petY - 2); setPixel(petX + 9, petY - 2); setPixel(petX + 8, petY - 1); setPixel(petX + 7, petY);
        }

        // Sick skull
        if (isSick) {
            setPixel(petX - 6, petY); setPixel(petX - 5, petY); setPixel(petX - 6, petY + 1); setPixel(petX - 4, petY + 1);
        }
    }
}

void TamagotchiMachine::drawIcon(int index, int cx, int cy, bool active) {
    uint32 color = active ? 0xFF0055FF : 0xFF708090; // Highlit Blue vs Idle Slate
    // 16x12 Icon box
    for (int dy = -6; dy <= 6; dy++) {
        for (int dx = -8; dx <= 8; dx++) {
            if (active || abs(dx) == 8 || abs(dy) == 6) {
                screenBuffer[(cy + dy) * CANVAS_W + cx + dx] = color;
            }
        }
    }
}

void TamagotchiMachine::renderTamagotchiScreen() {
    // Fill background with soft modern retro backdrop
    std::fill(screenBuffer.begin(), screenBuffer.end(), 0xFF181A24);

    // Draw Iconic Tamagotchi Egg-shaped Shell (Vibrant Pastel Shell with retro styling)
    const int eggCenterX = CANVAS_W / 2; // 320
    const int eggCenterY = CANVAS_H / 2 + 10; // 266
    const double radiusX = 200.0;
    const double radiusY = 240.0;

    uint32 shellBaseColor = (generation == TamagotchiGen::Gen1_P1) ? 0xFF4EB8FF : 0xFFFF70A6; // P1 Cyan / P2 Pink

    for (int y = eggCenterY - 240; y <= eggCenterY + 240; y++) {
        for (int x = eggCenterX - 200; x <= eggCenterX + 200; x++) {
            double dx = (x - eggCenterX) / radiusX;
            double dy = (y - eggCenterY) / radiusY;
            if (dx * dx + dy * dy <= 1.0) {
                // Subtle 3D egg curvature shading
                double dist = sqrt(dx * dx + dy * dy);
                uint32 eggPixel = shellBaseColor;
                if (dist > 0.92) eggPixel = 0xFF2A2A38;
                screenBuffer[y * CANVAS_W + x] = eggPixel;
            }
        }
    }

    // Draw Gray LCD Inner Bezel
    const int lcdBoxX = eggCenterX - 160;
    const int lcdBoxY = eggCenterY - 140;
    const int lcdBoxW = 320;
    const int lcdBoxH = 190;

    for (int y = lcdBoxY; y < lcdBoxY + lcdBoxH; y++) {
        for (int x = lcdBoxX; x < lcdBoxX + lcdBoxW; x++) {
            screenBuffer[y * CANVAS_W + x] = 0xFFD0D6DC;
        }
    }

    // Draw Top 4 Icons (0: Food, 1: Light, 2: Game, 3: Medicine)
    for (int i = 0; i < 4; i++) {
        drawIcon(i, lcdBoxX + 45 + i * 75, lcdBoxY + 16, selectedIcon == i);
    }

    // Authentic Tamagotchi LCD Screen (Authentic 32x16 dot matrix)
    // Scaled 8x -> 256 x 128, centered at X: lcdBoxX + 32, Y: lcdBoxY + 36
    const int lcdX = lcdBoxX + 32;
    const int lcdY = lcdBoxY + 36;
    const int pixelScale = 8;

    const uint32 lcdBacklight = isSleeping ? 0xFF4A5548 : 0xFF9ABC88; // Olive backlight / dark if lights off
    const uint32 lcdDotColor  = isSleeping ? 0xFF283024 : 0xFF1E2818; // LCD dot crystal

    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 32; x++) {
            bool dot = (lcdMatrix[y * 4 + (x / 8)] & (0x80 >> (x % 8))) != 0;
            uint32 c = dot ? lcdDotColor : lcdBacklight;

            int px = lcdX + x * pixelScale;
            int py = lcdY + y * pixelScale;

            for (int dy = 0; dy < pixelScale - 1; dy++) {
                int rowOff = (py + dy) * CANVAS_W;
                for (int dx = 0; dx < pixelScale - 1; dx++) {
                    screenBuffer[rowOff + px + dx] = c;
                }
            }
        }
    }

    // Draw Bottom 4 Icons (4: Bath, 5: Meter, 6: Discipline, 7: Attention)
    for (int i = 4; i < 8; i++) {
        bool active = (selectedIcon == i) || (i == 7 && callAttention);
        drawIcon(i, lcdBoxX + 45 + (i - 4) * 75, lcdBoxY + 174, active);
    }

    // Draw Physical 3 Buttons (A: Left, B: Center, C: Right)
    auto drawPhysBtn = [&](int cx, int cy, bool pressed) {
        uint32 btnCol = pressed ? 0xFF999922 : 0xFFFFD700; // Gold buttons
        for (int dy = -16; dy <= 16; dy++) {
            for (int dx = -16; dx <= 16; dx++) {
                if (dx * dx + dy * dy <= 256) {
                    screenBuffer[(cy + dy) * CANVAS_W + cx + dx] = btnCol;
                }
            }
        }
    };
    drawPhysBtn(eggCenterX - 75, eggCenterY + 115, buttonA); // Button A (Select)
    drawPhysBtn(eggCenterX,      eggCenterY + 130, buttonB); // Button B (Execute)
    drawPhysBtn(eggCenterX + 75, eggCenterY + 115, buttonC); // Button C (Cancel)
}

void TamagotchiMachine::updateAudio() {
    int16 sample = (buzzerTicks > 0) ? 6000 : 0;
    std::fill(audioBuffer.begin(), audioBuffer.end(), sample);
}

void TamagotchiMachine::handleKeyDown(SDL_Keycode key, uint16 mod) {
    if (key == SDLK_z || key == SDLK_LEFT || key == SDLK_a) {
        // Button A (Select / Cycle icon)
        buttonA = true;
        buzzerTicks = 3;
        if (inStats) {
            statsPage = (statsPage + 1) % 2;
        } else {
            selectedIcon = (selectedIcon + 1) % 7;
        }
    } else if (key == SDLK_x || key == SDLK_RETURN || key == SDLK_b) {
        // Button B (Execute / Confirm)
        buttonB = true;
        buzzerTicks = 5;
        if (selectedIcon == 0) { // Meal / Feed
            if (hungerHearts < 4) hungerHearts++;
            weightLbs++;
            callAttention = false;
        } else if (selectedIcon == 1) { // Light
            isSleeping = !isSleeping;
        } else if (selectedIcon == 2) { // Game
            if (happyHearts < 4) happyHearts++;
            if (weightLbs > 5) weightLbs--;
            callAttention = false;
        } else if (selectedIcon == 3) { // Medicine
            isSick = false;
            callAttention = false;
        } else if (selectedIcon == 4) { // Flush Poop
            hasPoop = false;
            callAttention = false;
        } else if (selectedIcon == 5) { // Health Meter
            inStats = !inStats;
        } else if (selectedIcon == 6) { // Discipline
            if (disciplineLevel < 100) disciplineLevel += 25;
            callAttention = false;
        }
    } else if (key == SDLK_c || key == SDLK_ESCAPE) {
        // Button C (Cancel / Back)
        buttonC = true;
        buzzerTicks = 3;
        selectedIcon = -1;
        inStats = false;
        inGame = false;
    }
}

void TamagotchiMachine::handleKeyUp(SDL_Keycode key, uint16 mod) {
    if (key == SDLK_z || key == SDLK_LEFT || key == SDLK_a) buttonA = false;
    if (key == SDLK_x || key == SDLK_RETURN || key == SDLK_b) buttonB = false;
    if (key == SDLK_c || key == SDLK_ESCAPE) buttonC = false;
}

bool TamagotchiMachine::loadMedia(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;

    std::vector<uint8> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (buffer.size() < 100) return false;

    // Load custom ROM data
    for (size_t i = 0; i < std::min(romWords.size(), buffer.size() / 2); i++) {
        romWords[i] = buffer[i * 2 + 1] | (((uint16)buffer[i * 2] & 0x0F) << 8);
    }
    reset();
    std::cout << "[TAMAGOTCHI] Loaded custom ROM: " << path << " (" << buffer.size() << " bytes)\n";
    return true;
}
