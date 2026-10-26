
#include <iostream>
#include "src/atari_machine.h"

int main() {
    AtariMachine atari;
    int resp0Count = 0;
    int resp1Count = 0;

    auto origWrite = atari.cpu.writeMem;
    atari.cpu.writeMem = [&](uint16 addr, uint8 val) {
        origWrite(addr, val);
        addr &= 0x1FFF;
        if ((addr & 0x1000) == 0 && (addr & 0x0080) == 0) {
            uint8 reg = addr & 0x3F;
            if (reg == 0x10) {
                resp0Count++;
                if (resp0Count <= 5) std::cout << "RESP0 write at scanline " << atari.currentScanline << " timerCycles " << atari.timerCycles << std::endl;
            }
            if (reg == 0x11) {
                resp1Count++;
                if (resp1Count <= 5) std::cout << "RESP1 write at scanline " << atari.currentScanline << " timerCycles " << atari.timerCycles << std::endl;
            }
        }
    };

    for (int i = 0; i < 60; i++) atari.runFrame();
    std::cout << "Total RESP0: " << resp0Count << " RESP1: " << resp1Count << std::endl;
    return 0;
}
