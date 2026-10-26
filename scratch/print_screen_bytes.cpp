#include "spectrum_machine.h"
#include <iostream>
#include <iomanip>

int main() {
    ZXSpectrumMachine spec;
    spec.init();
    for (int f = 0; f < 250; f++) spec.runFrame();

    const uint8* mem = spec.getMemory();
    std::cout << "Screen pixels at 0x4000:\n";
    for (int i = 0; i < 32; i++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)mem[0x4000 + i] << " ";
    }
    std::cout << "\nScreen attrs at 0x5800:\n";
    for (int i = 0; i < 32; i++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)mem[0x5800 + i] << " ";
    }
    std::cout << "\n";
    return 0;
}
