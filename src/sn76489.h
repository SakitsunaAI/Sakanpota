#pragma once

#include "types.h"
#include <vector>
#include <array>

class SN76489 {
public:
    static constexpr int SAMPLE_RATE = 44100;
    static constexpr double CLOCK_FREQ = 4000000.0; // 4 MHz on BBC Micro

    struct Channel {
        uint16 period = 1;
        uint16 counter = 0;
        uint8  volume = 15; // 0..15 (15 = silent)
        int    output = 1;
    };

    struct NoiseChannel {
        uint8  control = 0; // bits 0..1: rate, bit 2: type (0=periodic, 1=white)
        uint16 period = 1;
        uint16 counter = 0;
        uint8  volume = 15;
        uint16 lfsr = 0x4000;
        int    output = 1;
    };

    std::array<Channel, 3> tones{};
    NoiseChannel noise{};
    uint8 latchedChannel = 0;
    bool latchedType = false; // 0=tone/freq, 1=volume

    SN76489();

    void reset();
    void write(uint8 data);
    void generateSamples(int16* buffer, size_t count);

private:
    std::array<float, 16> volumeTable{};
    void updateNoisePeriod();
};
