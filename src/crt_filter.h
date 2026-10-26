#pragma once

#include "types.h"
#include <vector>

enum class CRTColorMode {
    RGB,        // Full authentic Acorn RGB
    Green,      // Classic P1 Green Phosphor CRT
    Amber,      // Warm P3 Amber Phosphor CRT
    Monochrome  // Black & White CRT
};

class CRTFilter {
public:
    CRTColorMode colorMode = CRTColorMode::RGB;
    bool scanlinesEnabled = false;      // Crystal-clear sharp pixels by default!
    bool curvatureEnabled = false;      // No fishbowl curvature by default!
    bool phosphorGlowEnabled = false;   // No ghosting/smearing by default!
    float scanlineStrength = 0.20f;

    std::vector<uint32> prevFrame;

    CRTFilter();

    void reset();

    // Applies CRT shader effects from input 640x512 buffer into output buffer
    void process(const uint32* src, uint32* dst, int width, int height, int pitch);

    void cycleColorMode();
    const char* getColorModeName() const;

private:
    uint32 applyColorPalette(uint32 color);
};
