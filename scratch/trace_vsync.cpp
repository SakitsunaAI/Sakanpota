
#include <iostream>
#include "src/atari_machine.h"

int main() {
    AtariMachine atari;
    int vsyncWrites = 0;

    auto origWrite = atari.cpu.writeMem;
    atari.cpu.writeMem = [&](uint16 addr, uint8 val) {
        origWrite(addr, val);
        addr &= 0x1FFF;
        if ((addr & 0x1000) == 0 && (addr & 0x0080) == 0) {
            uint8 reg = addr & 0x3F;
            if (reg == 0x00) {
                vsyncWrites++;
                if (vsyncWrites <= 10) std::cout << "VSYNC write: val=" << (int)val << " at scanline " << atari.currentScanline << " timerCycles " << atari.timerCycles << std::endl;
            }
            if (reg == 0x01) {
                if (vsyncWrites <= 6) std::cout << "VBLANK write: val=" << (int)val << " at scanline " << atari.currentScanline << std::endl;
            }
        }
    };

    for (int i = 0; i < 5; i++) atari.runFrame();
    return 0;
}
