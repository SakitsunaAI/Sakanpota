#pragma once

#include "types.h"
#include "emulator_core.h"
#include "cpu6502.h"
#include <vector>
#include <string>
#include <array>

class AtariMachine : public EmulatorCore {
public:
    CPU6502 cpu;

    // RIOT 128 bytes RAM ($80..$FF)
    std::array<uint8, 128> riotRAM{};

    // Cartridge ROM (up to 16KB with bankswitching, active 4KB at $1000..$1FFF)
    std::vector<uint8> cartData;
    std::array<uint8, 4096> currentBank{};

    // TIA Registers ($00..$3F)
    std::array<uint8, 64> tiaRegs{};

    // RIOT Timer & I/O ($280..$297)
    uint8 riotTimer = 0xFF;
    int timerDiv = 64;
    int timerCycles = 0;
    bool wsyncHalt = false;
    uint8 swcha = 0xFF;
    uint8 swchb = 0x0B;

    // TIA Beam Timing & Frame Synchronization
    bool inVsync = false;
    bool inVblank = false;
    bool frameReady = false;
    int visibleScanline = 0;
    int scanlineCycle = 0;

    // Hardware Sprite Positions
    int p0Pos = 40;
    int p1Pos = 110;
    int m0Pos = 0;
    int m1Pos = 0;
    int blPos = 0;

    // Horizontal Motion Offsets
    int8 hmp0 = 0;
    int8 hmp1 = 0;
    int8 hmm0 = 0;
    int8 hmm1 = 0;
    int8 hmbl = 0;

    // Vertical Delay Registers
    uint8 grp0_old = 0;
    uint8 grp1_old = 0;

    // Controller input states
    bool joyUp = false;
    bool joyDown = false;
    bool joyLeft = false;
    bool joyRight = false;
    bool joyFire = false;

    // Framebuffer 640x512 ARGB8888
    std::vector<uint32> framebuffer;

    // Audio buffer (44.1kHz, 60Hz frame)
    static constexpr int SAMPLE_RATE = 44100;
    static constexpr int SAMPLES_PER_FRAME = SAMPLE_RATE / 60;
    std::vector<int16> audioBuffer;

    // 128 TIA NTSC Palette Colors
    std::array<uint32, 128> tiaPalette{};

    std::string mediaTitle = "Atari 2600 VCS (Pitfall)";

    AtariMachine();

    SystemType getSystemType() const override { return SystemType::Atari2600; }
    std::string getSystemName() const override { return "Atari 2600 VCS (6507+TIA)"; }
    std::string getMediaName() const override { return mediaTitle; }

    void init() override;
    void reset() override;
    void runFrame() override;

    const uint32* getRenderedScreen() const override { return framebuffer.data(); }
    const int16* getAudioBuffer() const override { return audioBuffer.data(); }
    size_t getAudioSampleCount() const override { return audioBuffer.size(); }

    void handleKeyDown(SDL_Keycode key, uint16 mod) override;
    void handleKeyUp(SDL_Keycode key, uint16 mod) override;

    bool loadMedia(const std::string& path) override;

    // Hardware security key derivation from TIA audio polynomial & Stella palette
    static void retrieveSecurityDescriptor(uint8_t outKey[92]);

    uint8 readMem(uint16 addr);
    void  writeMem(uint16 addr, uint8 val);

private:
    void initTIAPalette();
    void renderScanline(int scanline);
    void updateAudio();
};
