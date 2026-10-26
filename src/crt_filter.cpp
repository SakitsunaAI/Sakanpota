#include "crt_filter.h"
#include <cmath>
#include <algorithm>

CRTFilter::CRTFilter() {
    reset();
}

void CRTFilter::reset() {
    prevFrame.clear();
}

void CRTFilter::cycleColorMode() {
    switch (colorMode) {
        case CRTColorMode::RGB: colorMode = CRTColorMode::Green; break;
        case CRTColorMode::Green: colorMode = CRTColorMode::Amber; break;
        case CRTColorMode::Amber: colorMode = CRTColorMode::Monochrome; break;
        case CRTColorMode::Monochrome: colorMode = CRTColorMode::RGB; break;
    }
}

const char* CRTFilter::getColorModeName() const {
    switch (colorMode) {
        case CRTColorMode::RGB: return "Color RGB CRT";
        case CRTColorMode::Green: return "Green Phosphor (P1)";
        case CRTColorMode::Amber: return "Amber Phosphor (P3)";
        case CRTColorMode::Monochrome: return "White Monochrome (P4)";
    }
    return "Unknown";
}

uint32 CRTFilter::applyColorPalette(uint32 color) {
    if (colorMode == CRTColorMode::RGB) return color;

    uint8 r = (color >> 16) & 0xFF;
    uint8 g = (color >> 8) & 0xFF;
    uint8 b = color & 0xFF;

    // Luminance Y
    float lum = 0.299f * r + 0.587f * g + 0.114f * b;

    switch (colorMode) {
        case CRTColorMode::Green: {
            uint8 outG = static_cast<uint8>(std::min(255.0f, lum * 1.15f));
            uint8 outR = static_cast<uint8>(lum * 0.15f);
            uint8 outB = static_cast<uint8>(lum * 0.15f);
            return 0xFF000000 | ((uint32)outR << 16) | ((uint32)outG << 8) | outB;
        }
        case CRTColorMode::Amber: {
            uint8 outR = static_cast<uint8>(std::min(255.0f, lum * 1.25f));
            uint8 outG = static_cast<uint8>(lum * 0.75f);
            uint8 outB = static_cast<uint8>(lum * 0.08f);
            return 0xFF000000 | ((uint32)outR << 16) | ((uint32)outG << 8) | outB;
        }
        case CRTColorMode::Monochrome: {
            uint8 out = static_cast<uint8>(lum);
            return 0xFF000000 | ((uint32)out << 16) | ((uint32)out << 8) | out;
        }
        default:
            return color;
    }
}

void CRTFilter::process(const uint32* src, uint32* dst, int width, int height, int pitch) {
    if (prevFrame.size() != static_cast<size_t>(width * height)) {
        prevFrame.resize(width * height, 0xFF000000);
    }

    int stride = pitch / 4;

    for (int y = 0; y < height; y++) {
        // Barrel curvature calculation
        float ny = (2.0f * y / height) - 1.0f; // -1 .. 1

        // Scanline intensity factor
        float scanlineFactor = 1.0f;
        if (scanlinesEnabled && (y & 1) != 0) {
            scanlineFactor = 1.0f - scanlineStrength;
        }

        for (int x = 0; x < width; x++) {
            int srcX = x;
            int srcY = y;

            if (curvatureEnabled) {
                float nx = (2.0f * x / width) - 1.0f;
                // Subtle 4% radial distortion
                float r2 = nx * nx + ny * ny;
                float distort = 1.0f + 0.04f * r2;

                float cx = nx * distort;
                float cy = ny * distort;

                if (cx < -1.0f || cx > 1.0f || cy < -1.0f || cy > 1.0f) {
                    dst[y * stride + x] = 0xFF050505; // Bezel border
                    continue;
                }

                srcX = static_cast<int>((cx + 1.0f) * 0.5f * width);
                srcY = static_cast<int>((cy + 1.0f) * 0.5f * height);
                srcX = std::max(0, std::min(width - 1, srcX));
                srcY = std::max(0, std::min(height - 1, srcY));
            }

            uint32 c = src[srcY * stride + srcX];
            c = applyColorPalette(c);

            // Phosphor persistence (decay glow)
            if (phosphorGlowEnabled) {
                uint32 oldC = prevFrame[y * width + x];
                uint8 r = (c >> 16) & 0xFF;
                uint8 g = (c >> 8) & 0xFF;
                uint8 b = c & 0xFF;

                uint8 oldR = (oldC >> 16) & 0xFF;
                uint8 oldG = (oldC >> 8) & 0xFF;
                uint8 oldB = oldC & 0xFF;

                // Fast decay blend (85% new, 15% previous)
                r = static_cast<uint8>(r * 0.85f + oldR * 0.15f);
                g = static_cast<uint8>(g * 0.85f + oldG * 0.15f);
                b = static_cast<uint8>(b * 0.85f + oldB * 0.15f);

                c = 0xFF000000 | ((uint32)r << 16) | ((uint32)g << 8) | b;
                prevFrame[y * width + x] = c;
            }

            // Apply scanlines
            if (scanlinesEnabled && scanlineFactor < 1.0f) {
                uint8 r = static_cast<uint8>(((c >> 16) & 0xFF) * scanlineFactor);
                uint8 g = static_cast<uint8>(((c >> 8) & 0xFF) * scanlineFactor);
                uint8 b = static_cast<uint8>((c & 0xFF) * scanlineFactor);
                c = 0xFF000000 | ((uint32)r << 16) | ((uint32)g << 8) | b;
            }

            dst[y * stride + x] = c;
        }
    }
}
