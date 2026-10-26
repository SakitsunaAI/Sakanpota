#pragma once
#include "types.h"

namespace EmbeddedROMs {
    extern uint8 SPECTRUM_48_ROM[16384];
    extern uint8 CPC464_OS_ROM[16384];
    extern uint8 CPC464_BASIC_ROM[16384];
    extern uint8 ODYSSEY2_BIOS_ROM[1024];

    extern uint8 MICROVISION_BLOCK_ROM[2048];
    extern uint8 MICROVISION_BOWLING_ROM[2048];
    extern uint8 MICROVISION_CONNECT4_ROM[2048];
    extern uint8 MICROVISION_PINBALL_ROM[2048];

    void ensureExtDecrypted();
}
