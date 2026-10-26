#include "spectrum_machine.h"
#include <iostream>
#include <fstream>

int main() {
    ZXSpectrumMachine spec;
    spec.init();
    for (int f = 0; f < 250; f++) spec.runFrame();

    const uint8* mem = spec.getMemory();
    int nonZero = 0;
    for (int i = 0x4000; i < 0x5800; i++) {
        if (mem[i] != 0) nonZero++;
    }
    std::cout << "Nonzero pixel bytes at frame 250: " << std::dec << nonZero << "\n";

    std::ofstream out("/Users/bekirkarakose/.gemini/antigravity-ide/brain/feef4c13-843b-4879-87b7-b45cebf1d580/scratch/spectrum_250.ppm", std::ios::binary);
    out << "P6\n640 512\n255\n";
    const uint32* buf = spec.getRenderedScreen();
    for (int i = 0; i < 640 * 512; i++) {
        uint32 c = buf[i];
        out.put((c >> 16) & 0xFF);
        out.put((c >> 8) & 0xFF);
        out.put(c & 0xFF);
    }
    return 0;
}
