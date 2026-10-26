
#include <iostream>
#include <map>
#include "src/atari_machine.h"

int main() {
    AtariMachine atari;
    int swchaReads = 0;
    int inpt4Reads = 0;

    auto origRead = atari.cpu.readMem;
    atari.cpu.readMem = [&](uint16 addr) -> uint8 {
        addr &= 0x1FFF;
        if (addr == 0x0280 || (addr & 0x280) == 0x280 && (addr & 7) == 0) swchaReads++;
        if ((addr & 0xF) == 0xC) inpt4Reads++;
        return origRead(addr);
    };

    for (int i = 0; i < 60; i++) atari.runFrame();
    std::cout << "swchaReads: " << swchaReads << " inpt4Reads: " << inpt4Reads << std::endl;
    return 0;
}
