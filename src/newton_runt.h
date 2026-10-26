#pragma once

#include "types.h"
#include <vector>
#include <string>
#include <functional>

// Handcrafted Apple Newton RUNT ASIC & Display Subsystem
// Handles Memory Mapping, Timers, Interrupts, ADC Digitizer, and Sharp LCD Controller
class NewtonRunt {
public:
    static constexpr int LCD_WIDTH = 336;
    static constexpr int LCD_HEIGHT = 240;
    static constexpr int PORTRAIT_WIDTH = 240;
    static constexpr int PORTRAIT_HEIGHT = 336;
    static constexpr int SILKSCREEN_HEIGHT = 48;
    static constexpr int TOTAL_HEIGHT = PORTRAIT_HEIGHT + SILKSCREEN_HEIGHT; // 384 pixels

    NewtonRunt();
    ~NewtonRunt() = default;

    void reset();
    bool loadDefaultROM();
    bool loadROM(const std::string& path);
    bool loadROMData(const uint8* data, size_t size);

    // Memory Bus Access
    uint8  read8(uint32 addr);
    uint16 read16(uint32 addr);
    uint32 read32(uint32 addr);

    void write8(uint32 addr, uint8 val);
    void write16(uint32 addr, uint16 val);
    void write32(uint32 addr, uint32 val);

    // Tick & Step Timers
    void tick(uint32 cycles);

    // Touchscreen / Digitizer Input
    void touchDown(int portraitX, int portraitY);
    void touchUp();

    // Power & Hardware Switches
    void togglePower();
    bool isPoweredOn() const { return powerOn; }

    // Interrupt Callback to CPU
    std::function<void()> onIRQ;
    std::function<void()> onFIQ;

    // Framebuffer Output (240x384 portrait, 8-bit grayscale: 0x00=black, 0xFF=white, 0xCC=sleep)
    const uint8* getPortraitFramebuffer() const { return portraitFB.data(); }
    bool isDisplayDirty() const { return displayDirty; }
    void clearDisplayDirty() { displayDirty = false; }

    std::string getLoadedROMName() const { return romName; }
    uint32 getMachineType() const { return machineType; }

private:
    // Memory Banks
    std::vector<uint8> rom;      // 4MB System ROM
    std::vector<uint8> ram1;     // 1MB System RAM (0x01000000)
    std::vector<uint8> ram2;     // 512KB Battery SRAM / Flash Store (0x01200000)
    std::vector<uint8> lcdMemory;// Sharp LCD internal RAM (336x240 pixels)
    std::vector<uint8> portraitFB;// Rotated portrait framebuffer (240x384)

    std::string romName;
    uint32 machineType;

    // RUNT ASIC Registers
    uint32 runtControl;
    uint32 intStatus;
    uint32 intEnable;
    uint32 ticks;
    uint32 tickAlarm1;
    uint32 tickAlarm2;
    uint32 tickAlarm3;
    uint32 rtcCounter;
    uint32 rtcAlarm;
    uint32 adcSource;
    uint32 adcValue;

    // Touchscreen Digitizer State
    bool penDown;
    int touchX; // 0..240
    int touchY; // 0..384
    int adcTouchX; // 0..1023
    int adcTouchY; // 0..1023

    bool powerOn;
    bool displayDirty;

    // Sharp LCD Controller State
    int16 writeX, writeY;
    int16 readX, readY;
    int16 windowLeft, windowTop;
    int16 windowRight, windowBottom;
    uint8 idw, idr;
    uint8 contrast;
    uint8 fillMode;
    uint8 bitMask;

    // Interrupt Flags
    static constexpr uint32 IRQ_TIMER1  = 0x0001;
    static constexpr uint32 IRQ_TIMER2  = 0x0002;
    static constexpr uint32 IRQ_TIMER3  = 0x0004;
    static constexpr uint32 IRQ_TABLET  = 0x0008;
    static constexpr uint32 IRQ_RTC     = 0x0010;
    static constexpr uint32 IRQ_POWER   = 0x0020;
    static constexpr uint32 IRQ_SERIAL  = 0x0040;

    void updateInterrupts();
    void updatePortraitDisplay();
    void renderSilkscreen();

    // LCD Controller helper
    void lcdSetPixel(int x, int y, uint8 color);
    void lcdStepWriteCursor();
    void lcdWritePixelData(uint8 byteVal);
};
