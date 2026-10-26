
#include <iostream>
#include <map>
#include "src/atari_machine.h"

int main() {
    AtariMachine atari;
    std::map<int, int> readCounts;

    auto origRead = atari.cpu.readMem;
    atari.cpu.readMem = [&](uint16 addr) -> uint8 {
        addr &= 0x1FFF;
        if ((addr & 0x1000) == 0 && (addr & 0x0080) == 0) {
            readCounts[addr & 0x3F]++;
        }
        return origRead(addr);
    };

    for (int i = 0; i < 60; i++) atari.runFrame();

    std::cout << "TIA register read counts across 60 frames:" << std::endl;
    for (auto& p : readCounts) {
        std::cout << "  Reg 0x" << std::hex << p.first << ": " << std::dec << p.second << std::endl;
    }
    return 0;
}
