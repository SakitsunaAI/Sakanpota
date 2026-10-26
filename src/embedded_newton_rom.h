#pragma once

#include "types.h"
#include <cstddef>

namespace EmbeddedNewtonROM {
    extern uint8 NEWTON_ROM[4194304];
    extern bool isDecrypted;

    void ensureDecrypted();
}
