#include "cpu_z80.h"
#include "embedded_roms_ext.h"
#include <iostream>
#include <iomanip>
#include <vector>

int main() {
    CPUZ80 cpu;
    std::vector<uint8> mem(65536, 0);
    std::memcpy(mem.data(), EmbeddedROMs::SPECTRUM_48_ROM, 16384);

    cpu.memRead = [&](uint16 a) { return mem[a]; };
    cpu.memWrite = [&](uint16 a, uint8 v) {
        if (a == 0x5800) {
            std::cout << "Write to 0x5800: val=0x" << std::hex << (int)v 
                      << " from PC=0x" << cpu.PC << std::dec << "\n";
        }
        if (a >= 0x4000) mem[a] = v;
    };
    cpu.ioRead = [&](uint16 p) { return 0xFF; };
    cpu.ioWrite = [&](uint16 p, uint8 v) {};

    cpu.reset();

    for (int i = 0; i < 2000000; i++) {
        cpu.step();
    }
    return 0;
}
