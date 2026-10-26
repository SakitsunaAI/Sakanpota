#pragma once
#include "types.h"
#include <cstddef>

namespace EmbeddedNextGenVMU {
    extern uint8 VM2_FIRMWARE[65536];
    extern uint8 VM2_CHAO_EXPEDITION[16384];
    extern uint8 VM2_SKIES_VOYAGE[16384];
    extern uint8 VM2_POWER_STONE_DUEL[16384];
    extern uint8 VM2_SONIC_SPEED[16384];
    extern uint8 VM2_SPACE_RAID[16384];
    extern uint8 VMUPRO_FIRMWARE[65536];
    extern uint8 VMUPRO_CHAO_COLOR[16384];
    extern uint8 VMUPRO_PINTA_COLOR[16384];
    extern uint8 VMUPRO_POWER_STONE_COLOR[16384];
    extern uint8 VMUPRO_JET_SET_RADIO[16384];
    extern uint8 VMUPRO_CRAZY_TAXI[16384];

    void ensureDecrypted();
} // namespace EmbeddedNextGenVMU
