#include "embedded_roms_ext.h"
#include <iostream>
#include <iomanip>

int main() {
    int start = 0x11DC;
    for (int i = 0; i < 40; i++) {
        std::cout << "0x" << std::hex << (start + i) << ": "
                  << std::setw(2) << std::setfill('0') << (int)EmbeddedROMs::SPECTRUM_48_ROM[start + i] << "\n";
    }
    return 0;
}
