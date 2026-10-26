#pragma once

#include "types.h"
#include <array>

class SAA5050 {
public:
    static constexpr int COLS = 40;
    static constexpr int ROWS = 25;
    static constexpr int CHAR_WIDTH = 16;  // 16 pixels per column for 640 width
    static constexpr int CHAR_HEIGHT = 20; // 20 scanlines per row for 500 height

    SAA5050();

    // Renders full Mode 7 screen into 640x500 ARGB32 framebuffer
    void renderScreen(const uint8* ram7C00, uint32* framebuffer, int pitch, bool flashState);

private:
    // 96 printable characters, each 10 scanlines of 6-bit dot patterns
    static const uint8 fontData[96][10];

    // Colors: Black, Red, Green, Yellow, Blue, Magenta, Cyan, White
    static const uint32 teletextPalette[8];
};
