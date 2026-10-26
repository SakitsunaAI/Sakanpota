#pragma once

#include "types.h"
#include "saa5050.h"
#include <functional>
#include <array>

class CRTC6845 {
public:
    // 6845 CRTC Registers
    uint8 addressRegister = 0;
    std::array<uint8, 18> registers{};

    // Video ULA Registers
    uint8 videoControl = 0x9C; // $FE20
    uint8 paletteControl = 0;  // $FE21
    std::array<uint8, 16> logicalPalette{}; // 16 logical color mappings

    // Teletext generator for Mode 7
    SAA5050 teletext;

    // Flash timer & state
    int flashCounter = 0;
    bool flashState = false;

    // Display buffer dimensions
    static constexpr int SCREEN_WIDTH = 640;
    static constexpr int SCREEN_HEIGHT = 512;

    // Memory read callback for fetching screen RAM
    std::function<uint8(uint16)> readScreenRAM;

    CRTC6845();

    void reset();

    // I/O Register access
    uint8 readCRTC(uint16 addr);
    void  writeCRTC(uint16 addr, uint8 val);
    uint8 readULA(uint16 addr);
    void  writeULA(uint16 addr, uint8 val);

    // Frame render: outputs 640x512 ARGB8888
    void renderFrame(uint32* framebuffer, int pitch);

    bool isTeletextMode() const {
        return (videoControl & 0x02) != 0;
    }

private:
    static const uint32 physicalRGB[8];
    uint32 getPaletteColor(uint8 logical);
};
