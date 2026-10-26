
#include <iostream>
#include <map>
#include "src/atari_machine.h"

int main() {
    AtariMachine atari;
    std::map<int, int> writeCounts;
    std::map<int, int> resp0Clocks;

    // Hook writeMem
    auto origWrite = atari.cpu.writeMem;
    atari.cpu.writeMem = [&](uint16 addr, uint8 val) {
        origWrite(addr, val);
        addr &= 0x1FFF;
        if ((addr & 0x1000) == 0 && (addr & 0x0080) == 0) {
            writeCounts[addr & 0x3F]++;
        }
    };

    for (int i = 0; i < 60; i++) atari.runFrame();

    std::cout << "TIA register write counts across 60 frames:" << std::endl;
    for (auto& p : writeCounts) {
        std::cout << "  Reg 0x" << std::hex << p.first << ": " << std::dec << p.second << std::endl;
    }
    return 0;
}
