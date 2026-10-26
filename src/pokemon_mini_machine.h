#pragma once

#include "emulator_core.h"
#include <array>
#include <vector>
#include <string>

class PokemonMiniMachine : public EmulatorCore {
public:
    PokemonMiniMachine(int initialGameIndex = 0);
    ~PokemonMiniMachine() override;

    SystemType getSystemType() const override { return SystemType::PokemonMini; }
    std::string getSystemName() const override { return "Nintendo Pokémon Mini (S1C88)"; }
    std::string getMediaName() const override { return currentMedia; }

    void init() override;
    void reset() override;
    void runFrame() override;

    const uint32* getRenderedScreen() const override { return screenBuffer.data(); }
    const int16* getAudioBuffer() const override { return audioBuffer.data(); }
    size_t getAudioSampleCount() const override { return audioBuffer.size(); }

    void handleKeyDown(SDL_Keycode key, uint16 mod) override;
    void handleKeyUp(SDL_Keycode key, uint16 mod) override;

    bool loadMedia(const std::string& path) override;
    bool switchGame(int gameIndex);

private:
    int currentGame = 0;
    bool inGameMenu = false;
    int menuCursor = 0;
    bool isShaking = false;
    int shakeTimer = 0;

    // Save RAM / EEPROM
    std::vector<uint8> eepromData;

    // Active Cartridge buffer
    std::vector<uint8> romData;
    std::string currentMedia = "Pokémon Party Mini";

    // Framebuffer & Audio
    std::vector<uint32> screenBuffer;
    std::vector<int16> audioBuffer;

    void loadCurrentGame();
    void renderHandheldChassis();
    void renderGameMenu();
};
