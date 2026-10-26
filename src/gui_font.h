#pragma once

#include "types.h"
#include <string>

class GUIFont {
public:
    static uint8 getGlyphRow(char c, int row);
    static void drawChar(uint32* buffer, int pitch, int x, int y, char c, uint32 fg, uint32 bg = 0x00000000);
    static void drawString(uint32* buffer, int pitch, int x, int y, const std::string& text, uint32 fg, uint32 bg = 0x00000000);
    static void fillRect(uint32* buffer, int pitch, int x, int y, int w, int h, uint32 color);
    static void drawRect(uint32* buffer, int pitch, int x, int y, int w, int h, uint32 color);
};
