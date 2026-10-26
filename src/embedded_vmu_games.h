#pragma once
#include "types.h"
#include <string>
#include <cstddef>

namespace EmbeddedVMUGames {
    constexpr int GAME_COUNT = 6;
    constexpr size_t VMU_GAME_SIZE = 16384;

    struct VMUGameInfo {
        const char* varName;
        const char* shortName;
        const char* title;
        const char* author;
        const char* description;
    };

    extern const VMUGameInfo GAME_INFO[GAME_COUNT];

    extern uint8 CHAO_ADVENTURE[VMU_GAME_SIZE];
    extern uint8 CHAO_ADVENTURE_2[VMU_GAME_SIZE];
    extern uint8 PINTAS_QUEST[VMU_GAME_SIZE];
    extern uint8 POWER_STONE[VMU_GAME_SIZE];
    extern uint8 GODZILLA_VMU[VMU_GAME_SIZE];
    extern uint8 ZOMBIE_REVENGE[VMU_GAME_SIZE];

    void ensureDecrypted();
    const uint8* getGameData(int index, size_t& outSize);
    const VMUGameInfo* getGameInfo(int index);
} // namespace EmbeddedVMUGames
