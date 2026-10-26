#pragma once
#include "types.h"
#include <string>

namespace EmbeddedROMs {
    // Runtime decrypted ROM buffers (zero unencrypted data stored in binary)
    extern uint8 BBC_OS120[16384];
    extern uint8 BBC_BASIC[16384];
    extern uint8 BBC_DFS09[8192];
    extern uint8 BBC_OS01[16384];
    extern uint8 BBC_BPOS[16384];
    extern uint8 BBC_MOS320[131072];
    extern uint8 BBC_WELCOME_SSD[78336];

    extern uint8 MAC_128K_ROM[65536];
    extern uint8 LISA_BOOT_ROM[16384];

    extern uint8 POCKETSTATION_BIOS[16384];
    extern uint8 POCKETSTATION_CHOCOBO_GAME[16384];
    extern uint8 POCKETSTATION_DOKODEMO_GAME[16384];
    extern uint8 POCKETSTATION_CRASH_GAME[16384];
    extern uint8 POCKETSTATION_MEGAMAN_GAME[16384];
    extern uint8 POCKETSTATION_SPYRO_GAME[16384];
    extern uint8 POCKETSTATION_SPACE_GAME[16384];
    extern uint8 POCKETSTATION_DUNGEON_GAME[16384];

    extern uint8 C64_KERNAL[8192];
    extern uint8 C64_BASIC[8192];
    extern uint8 C64_CHARGEN[4096];

    extern uint8 IBM_PC_BIOS[8192];
    extern uint8 IBM_PC_BASIC[32768];

    extern uint8 ATARI_2600_DEFAULT_ROM[4096];
    extern uint8 JUPITER_ACE_ROM[8192];

    extern uint8 ENTERPRISE_EXOS[32768];
    extern uint8 ENTERPRISE_BASIC[16384];

    extern uint8 SHARP_MZ700_ROM[4096];
    extern uint8 SHARP_MZ700_FONT[4096];

    extern uint8 SINCLAIR_QL_ROM[65536];
    extern uint8 ORIC_ATMOS_ROM[16384];

    extern uint8 ATARI_LYNX_BOOT[512];
    extern uint8 CAMPUTERS_LYNX_ROM[16384];

    extern uint8 SEGA_VMU_BIOS[65536];

    extern uint8 TAMAGOTCHI_P1_ROM[12288];
    extern uint8 TAMAGOTCHI_P2_ROM[12288];

    extern uint8 SUPERVISION_CRYSTBALL[65536];

    extern uint8 TI99_SYSTEM_ROM[8192];
    extern uint8 TI99_SYSTEM_GROM[18432];

    extern uint8 TI83P_ROM[524288];

    extern uint8 CYBIKO_SNAKE_APP[26394];
    extern uint8 CYBIKO_CALC_APP[29991];

    extern uint8 STUDIO2_ROM[2048];

    extern uint8 APPLE_NEWTON_ROM[4194304];
    extern const size_t NEWTON_ROM_COMPRESSED_SIZE;
    extern const size_t NEWTON_ROM_RAW_SIZE;
    extern const uint8_t ENC_APPLE_NEWTON_ROM[];

    // Ensure all assets are decrypted in memory using key derived from AtariMachine
    void ensureDecrypted();

    // Query ROM by name or alias
    bool getROM(const std::string& name, const uint8*& data, size_t& size);
}

