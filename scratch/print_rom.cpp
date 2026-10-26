#include "embedded_roms_ext.h"
#include <iostream>
#include <iomanip>

int main() {
    for (int i = 0; i < 64; i++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)EmbeddedROMs::SPECTRUM_48_ROM[i] << " ";
        if ((i + 1) % 16 == 0) std::cout << "\n";
    }
    return 0;
}
