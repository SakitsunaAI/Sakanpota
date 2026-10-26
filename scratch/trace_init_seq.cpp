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
        uint16 pc = cpu.PC;
        if (pc >= 0x11F0 && pc <= 0x1230) {
            std::cout << "Step " << i << ": PC=0x" << std::hex << pc 
                      << " AF=0x" << cpu.AF.w << " BC=0x" << cpu.BC.w 
                      << " DE=0x" << cpu.DE.w << " HL=0x" << cpu.HL.w 
                      << " SP=0x" << cpu.SP << std::dec << "\n";
            if (pc == 0x1220) break;
        }
        cpu.step();
    }
    return 0;
}
