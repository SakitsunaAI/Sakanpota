
#include <iostream>
#include <map>
#include "src/atari_machine.h"

int main() {
    AtariMachine atari;
    std::map<int, int> readAddrs;

    auto origRead = atari.cpu.readMem;
    atari.cpu.readMem = [&](uint16 addr) -> uint8 {
        addr &= 0x1FFF;
        if ((addr & 0x1000) == 0) {
            readAddrs[addr]++;
        }
        return origRead(addr);
    };

    for (int i = 0; i < 60; i++) atari.runFrame();

    std::cout << "RAM / TIA read counts:" << std::endl;
    for (auto& p : readAddrs) {
        if (p.second > 100)
            std::cout << "  Addr 0x" << std::hex << p.first << ": " << std::dec << p.second << std::endl;
    }
    return 0;
}
