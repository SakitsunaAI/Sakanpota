#pragma once

#include "types.h"
#include "cpu6502.h"
#include "memory.h"
#include "via6522.h"
#include "sn76489.h"
#include "crtc6845.h"
#include "fdc1770.h"
#include "keyboard.h"
#include "debugger.h"
#include "crt_filter.h"
#include "emulator_core.h"
#include <string>
#include <vector>

class BBCMachine : public EmulatorCore {
public:
    BBCModel model = BBCModel::ModelB;

    CPU6502  cpu;
    Memory   memory;
    VIA6522  sysVia;
    VIA6522  userVia;
    SN76489  sound;
    CRTC6845 crtc;
    FDC1770  fdc;
    Keyboard keyboard;
    Debugger debugger;
    CRTFilter crtFilter;

    // Framebuffers (640x512 ARGB8888)
    std::vector<uint32> rawFramebuffer;
    std::vector<uint32> crtFramebuffer;

    // Audio buffer for 1 frame (882 samples at 44.1kHz for 50Hz frame)
    static constexpr int SAMPLES_PER_FRAME = SN76489::SAMPLE_RATE / 50;
    std::vector<int16> audioBuffer;

    // Timing constants
    static constexpr int CYCLES_PER_SEC = 2000000; // 2 MHz
    static constexpr int CYCLES_PER_FRAME = CYCLES_PER_SEC / 50; // 40,000 cycles

    // Directory for ROMs
    std::string romDirectory = "roms";

    // Auto-detection status
    std::string autoDetectInfo = "";

    // IC32 74LS259 addressable latch
    uint8 ic32Latch = 0;

    // Shift key release countdown during disk auto-boot
    int shiftBootFrames = 0;

    // Pending boot command: injected after BASIC prompt '>' is detected
    std::string pendingBootCmd = "";
    bool promptReady = false;       // Set when '>' seen in MOS OSWRCH output
    int promptCooldown = 0;         // Extra frames to wait after '>' before typing

    BBCMachine();

    SystemType getSystemType() const override { return SystemType::BBCMicro; }
    std::string getSystemName() const override { return getModelName(model); }
    std::string getMediaName() const override {
        std::string title = fdc.getDiskTitle(0);
        return title.empty() ? (fdc.driveLoaded[0] ? "Loaded Disk" : "No Disk") : title;
    }

    void init() override;
    void reset() override;
    void switchModel(BBCModel m);

    // Insert a disk image with automatic target model detection
    bool insertDisk(int drive, const std::string& path, bool autoSwitchModel = true);

    // Run 1 frame (40,000 cycles)
    void runFrame() override;

    // Auto-boot disk (Shift+F12)
    void autoBootDisk();

    // Load ROMs for current model
    bool loadModelROMs();

    // Audio sample retrieval
    const int16* getAudioBuffer() const override { return audioBuffer.data(); }
    size_t getAudioSampleCount() const override { return audioBuffer.size(); }

    // Video frame retrieval
    const uint32* getRenderedScreen() const override;

    // Keyboard input
    void handleKeyDown(SDL_Keycode key, uint16 mod) override {
        keyboard.handleKeyDown(key, mod);
    }
    void handleKeyUp(SDL_Keycode key, uint16 mod) override {
        keyboard.handleKeyUp(key, mod);
    }

    // Media loader
    bool loadMedia(const std::string& path) override {
        if (insertDisk(0, path, true)) {
            autoBootDisk();
            return true;
        }
        return false;
    }

private:
    void setupBus();
    void updateInterrupts();
};
