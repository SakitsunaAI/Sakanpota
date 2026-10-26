#include "spectrum_machine.h"
#include <iostream>
#include <iomanip>

int main() {
    ZXSpectrumMachine spec;
    spec.init();
    for (int f = 0; f < 100; f++) {
        spec.runFrame();
        const uint8* mem = spec.getMemory();
        uint16 ramtop = mem[0x5CB4] | (mem[0x5CB5] << 8);
        if (ramtop != 0) {
            std::cout << "Frame " << f << ": RAMTOP=0x" << std::hex << ramtop << std::dec << "\n";
        }
    }
    return 0;
}
