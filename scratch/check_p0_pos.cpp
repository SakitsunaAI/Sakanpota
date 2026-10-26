
#include <iostream>
#include "scratch/test_perfect_atari.cpp"

int main() {
    PerfectAtari atari;
    for (int i = 0; i < 60; i++) atari.runFrame();
    
    // Track p0Pos on each visible scanline
    std::cout << "Harry on scanline 80 initial: " << atari.p0Pos << std::endl;

    // Now press right for 30 frames
    atari.swcha &= ~0x80;
    for (int i = 0; i < 30; i++) atari.runFrame();

    std::cout << "Done!" << std::endl;
    return 0;
}
