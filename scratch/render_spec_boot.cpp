#include "spectrum_machine.h"
#include <iostream>
#include <fstream>

int main() {
    ZXSpectrumMachine spec;
    spec.init();
    // Run 100 frames so RAM test completes and BASIC boots
    for (int f = 0; f < 100; f++) {
        spec.runFrame();
    }
    std::ofstream out("/Users/bekirkarakose/.gemini/antigravity-ide/brain/feef4c13-843b-4879-87b7-b45cebf1d580/scratch/spectrum_booted.ppm", std::ios::binary);
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
