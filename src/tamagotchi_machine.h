#pragma once

#include "emulator_core.h"
#include <vector>
#include <array>
#include <string>

enum class TamagotchiGen {
    Gen1_P1, // 1996 Generation 1 (Mametchi line, Left/Right game)
    Gen2_P2  // 1997 Generation 2 (Mimitchi line, High/Low game)
};

class TamagotchiMachine : public EmulatorCore {
public:
    explicit TamagotchiMachine(TamagotchiGen gen = TamagotchiGen::Gen1_P1);
    ~TamagotchiMachine() override = default;

    SystemType getSystemType() const override {
        return (generation == TamagotchiGen::Gen1_P1) ? SystemType::TamagotchiP1 : SystemType::TamagotchiP2;
    }
    std::string getSystemName() const override {
        return (generation == TamagotchiGen::Gen1_P1) ? "Bandai Tamagotchi P1 (1996)" : "Bandai Tamagotchi P2 (1997)";
    }
    std::string getMediaName() const override {
        return (generation == TamagotchiGen::Gen1_P1) ? "Embedded P1 ROM (Gen 1 E0C6S46)" : "Embedded P2 ROM (Gen 2 E0C6S46)";
    }

    void init() override;
    void reset() override;
    void runFrame() override;

    const uint32* getRenderedScreen() const override { return screenBuffer.data(); }
    const int16* getAudioBuffer() const override { return audioBuffer.data(); }
    size_t getAudioSampleCount() const override { return audioBuffer.size(); }

    void handleKeyDown(SDL_Keycode key, uint16 mod) override;
    void handleKeyUp(SDL_Keycode key, uint16 mod) override;

    bool loadMedia(const std::string& path) override;

private:
    TamagotchiGen generation;
    std::array<uint16, 3072> romWords; // 3072 12-bit instructions (6KB binary)
    std::array<uint8, 256> ram;        // 4-bit internal nibble RAM

    // Hardware LCD: 32 columns x 16 rows (64 bytes = 32 * 16 / 8)
    uint8 lcdMatrix[64];
    int selectedIcon = -1;  // -1: none, 0..7: active selected icon
    bool callAttention = false;

    // Pet Evolution & Health State
    enum class PetStage { Egg, Baby, Child, Teen, Adult };
    PetStage stage = PetStage::Egg;
    int ageHours = 0;
    int weightLbs = 5;
    int hungerHearts = 4;
    int happyHearts = 4;
    int disciplineLevel = 0; // 0..100%
    bool isSleeping = false;
    bool isSick = false;
    bool hasPoop = false;
    [[maybe_unused]] int gameRound = 0;
    [[maybe_unused]] int gameScore = 0;
    bool inGame = false;
    bool inStats = false;
    int statsPage = 0;

    int animTick = 0;
    int buzzerTicks = 0;

    // Physical 3 buttons: A (Left), B (Center), C (Right)
    bool buttonA = false;
    bool buttonB = false;
    bool buttonC = false;

    std::vector<uint32> screenBuffer;
    std::vector<int16> audioBuffer;

    void updatePetLogic();
    void renderTamagotchiScreen();
    void updateAudio();
    void drawIcon(int index, int cx, int cy, bool active);
};
