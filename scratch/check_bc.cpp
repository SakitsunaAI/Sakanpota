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
    cpu.memWrite = [&](uint16 a, uint8 v) { if (a >= 0x4000) mem[a] = v; };
    cpu.ioRead = [&](uint16 p) { return 0xFF; };
    cpu.ioWrite = [&](uint16 p, uint8 v) {};

    cpu.reset();

    for (int i = 0; i < 700000; i++) {
        if (cpu.PC == 0x11F1) {
            std::cout << "At 0x11F1: BC=0x" << std::hex << cpu.BC.w 
                      << " HL=0x" << cpu.HL.w << " DE=0x" << cpu.DE.w << std::dec << "\n";
            cpu.step();
            std::cout << "After 0x11F1: mem[0x5CB4]=0x" << std::hex << (int)mem[0x5CB4] 
                      << " mem[0x5CB5]=0x" << (int)mem[0x5CB5] << std::dec << "\n";
            break;
        }
        cpu.step();
    }
    return 0;
}
