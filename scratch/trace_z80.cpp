#include "spectrum_machine.h"
#include <iostream>
#include <iomanip>

int main() {
    ZXSpectrumMachine spec;
    spec.init();
    // Let's inspect memory around 0x0000
    // We can run step by step
    for (int f = 0; f < 300; f++) {
        spec.runFrame();
    }
    // Inspect what is at 0x5C00 (system variables)
    return 0;
}
