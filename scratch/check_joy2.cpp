
#include <iostream>
#include <map>
#include "src/atari_machine.h"

int main() {
    AtariMachine atari;
    std::map<uint16, int> joyAddrs;

    auto origRead = atari.cpu.readMem;
    atari.cpu.readMem = [&](uint16 addr) -> uint8 {
        addr &= 0x1FFF;
        if (addr == 0x0280 || (addr & 0x1F) == 0x0C) {
            joyAddrs[addr]++;
        }
        return origRead(addr);
    };

    for (int i = 0; i < 60; i++) atari.runFrame();
    for (auto& p : joyAddrs) {
        std::cout << "  Joy addr 0x" << std::hex << p.first << ": " << std::dec << p.second << std::endl;
    }
    return 0;
}
