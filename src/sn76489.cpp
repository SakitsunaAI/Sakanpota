#include "sn76489.h"
#include <cmath>
#include <algorithm>

SN76489::SN76489() {
    // Generate exponential volume table (2dB per step, 0 = 0dB, 15 = 0)
    for (int i = 0; i < 15; i++) {
        volumeTable[i] = static_cast<float>(std::pow(10.0, -i * 2.0 / 20.0));
    }
    volumeTable[15] = 0.0f; // Silence

    reset();
}

void SN76489::reset() {
    for (auto& t : tones) {
        t.period = 1;
        t.counter = 0;
        t.volume = 15;
        t.output = 1;
    }
    noise.control = 0;
    noise.period = 16;
    noise.counter = 0;
    noise.volume = 15;
    noise.lfsr = 0x4000;
    noise.output = 1;
    latchedChannel = 0;
    latchedType = false;
}

void SN76489::updateNoisePeriod() {
    int rate = noise.control & 0x03;
    switch (rate) {
        case 0: noise.period = 16; break;
        case 1: noise.period = 32; break;
        case 2: noise.period = 64; break;
        case 3: noise.period = tones[2].period; break;
    }
}

void SN76489::write(uint8 data) {
    if (data & 0x80) {
        // LATCH / DATA byte
        latchedChannel = (data >> 5) & 0x03;
        latchedType = (data & 0x10) != 0; // 0=tone/freq, 1=volume
        uint8 val = data & 0x0F;

        if (latchedType) {
            // Volume
            if (latchedChannel < 3) {
                tones[latchedChannel].volume = val;
            } else {
                noise.volume = val;
            }
        } else {
            // Frequency / Noise control
            if (latchedChannel < 3) {
                tones[latchedChannel].period = (tones[latchedChannel].period & 0x3F0) | val;
                if (latchedChannel == 2 && (noise.control & 0x03) == 3) {
                    updateNoisePeriod();
                }
            } else {
                noise.control = val & 0x07;
                noise.lfsr = 0x4000;
                updateNoisePeriod();
            }
        }
    } else {
        // DATA byte (for Tone frequencies: top 6 bits)
        uint8 val = data & 0x3F;
        if (!latchedType && latchedChannel < 3) {
            tones[latchedChannel].period = (tones[latchedChannel].period & 0x0F) | ((uint16)val << 4);
            if (latchedChannel == 2 && (noise.control & 0x03) == 3) {
                updateNoisePeriod();
            }
        }
    }
}

void SN76489::generateSamples(int16* buffer, size_t count) {
    // 4 MHz / (32 * period) = frequency
    // Step per output sample: (CLOCK_FREQ / 32.0) / SAMPLE_RATE = 125000.0 / 44100.0 ~= 2.834467
    const double clockPerSample = (CLOCK_FREQ / 32.0) / SAMPLE_RATE;

    for (size_t s = 0; s < count; s++) {
        float mix = 0.0f;

        // Tone channels
        for (int i = 0; i < 3; i++) {
            auto& ch = tones[i];
            if (ch.period > 0) {
                ch.counter += clockPerSample;
                while (ch.counter >= ch.period) {
                    ch.counter -= ch.period;
                    ch.output = -ch.output;
                }
            }
            if (ch.period > 6) { // Filter out supersonic clicks
                mix += ch.output * volumeTable[ch.volume];
            }
        }

        // Noise channel
        if (noise.period > 0) {
            noise.counter += clockPerSample;
            while (noise.counter >= noise.period) {
                noise.counter -= noise.period;
                // LFSR shift
                bool feedback;
                if (noise.control & 0x04) {
                    // White noise: XOR bits 0 and 1
                    feedback = ((noise.lfsr & 1) ^ ((noise.lfsr >> 1) & 1)) != 0;
                } else {
                    // Periodic noise: bit 0
                    feedback = (noise.lfsr & 1) != 0;
                }
                noise.lfsr = (noise.lfsr >> 1) | (feedback ? 0x4000 : 0);
                noise.output = (noise.lfsr & 1) ? 1 : -1;
            }
            mix += noise.output * volumeTable[noise.volume];
        }

        // Clamp and scale to 16-bit
        float sample = mix * 0.25f * 30000.0f;
        sample = std::max(-32767.0f, std::min(32767.0f, sample));
        buffer[s] = static_cast<int16>(sample);
    }
}
