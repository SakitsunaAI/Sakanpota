#include "spectrum_machine.h"
#include <iostream>
#include <iomanip>

int main() {
    ZXSpectrumMachine spec;
    spec.init();
    // Trace first 30 instructions
    for (int i = 0; i < 50; i++) {
        // We can check memory at 0x5800
    }
    spec.runFrame();
    std::cout << "Memory at 0x5800..0x5820 (attrs):\n";
    for (int i = 0; i < 32; i++) {
        std::cout << std::hex << (int)spec.getRenderedScreen()[0] << " ";
    }
    std::cout << "\n";
    return 0;
}
