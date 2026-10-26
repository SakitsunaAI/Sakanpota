#include "spectrum_machine.h"
#include <iostream>
#include <iomanip>

int main() {
    ZXSpectrumMachine spec;
    spec.init();
    for (int f = 0; f < 120; f++) spec.runFrame();

    const uint8* mem = spec.getMemory();
    std::cout << "Attributes at 0x5800 (first line):\n";
    for (int i = 0; i < 32; i++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)mem[0x5800 + i] << " ";
    }
    std::cout << "\nAttributes at last line (0x5AE0):\n";
    for (int i = 0; i < 32; i++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)mem[0x5AE0 + i] << " ";
    }
    std::cout << "\nPixels at 0x5000 (last character line):\n";
    int nonZero = 0;
    for (int i = 0x5000; i < 0x5800; i++) {
        if (mem[i] != 0) nonZero++;
    }
    std::cout << "Nonzero pixel bytes in 3rd screen block: " << std::dec << nonZero << "\n";
    return 0;
}
