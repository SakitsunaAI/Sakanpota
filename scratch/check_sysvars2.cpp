#include "spectrum_machine.h"
#include <iostream>
#include <iomanip>

int main() {
    ZXSpectrumMachine spec;
    spec.init();
    for (int f = 0; f < 150; f++) spec.runFrame();
    const uint8* mem = spec.getMemory();
    uint16 v = mem[0x5C51] | (mem[0x5C52] << 8);
    std::cout << "Value at 0x5C51: 0x" << std::hex << v << std::dec << "\n";
    std::cout << "RAMTOP at 0x5CB4: 0x" << std::hex << (mem[0x5CB4] | (mem[0x5CB5] << 8)) << std::dec << "\n";
    std::cout << "ATTR_P at 0x5C8D: 0x" << std::hex << (int)mem[0x5C8D] << std::dec << "\n";
    std::cout << "BORDCR at 0x5C48: 0x" << std::hex << (int)mem[0x5C48] << std::dec << "\n";
    return 0;
}
