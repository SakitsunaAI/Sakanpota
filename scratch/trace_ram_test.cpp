#include "spectrum_machine.h"
#include <iostream>
#include <iomanip>

int main() {
    ZXSpectrumMachine spec;
    spec.init();
    // Step instructions and trace PC around 0x11D0..0x1250
    // Let's see what happens after 100,000 instructions
    // To do that, we can run frames and monitor
    for (int f = 0; f < 10; f++) {
        spec.runFrame();
    }
    std::cout << "Ran 10 frames\n";
    return 0;
}
