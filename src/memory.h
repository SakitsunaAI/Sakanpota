#pragma once

#include "types.h"
#include <functional>
#include <vector>
#include <array>

class Memory {
public:
    BBCModel model = BBCModel::ModelB;

    // RAM buffers
    std::array<uint8, 32 * 1024> mainRAM{};       // 32 KB main RAM ($0000..$7FFF)
    std::array<uint8, 20 * 1024> shadowRAM{};     // 20 KB shadow RAM ($3000..$7FFF)
    std::array<uint8, 4 * 1024>  hazelRAM{};      // 4 KB Hazel RAM ($C000..$CFFF for Master 128)
    std::array<std::array<uint8, 16 * 1024>, 16> sidewaysRAM{}; // 16 Sideways RAM banks
    std::array<bool, 16> sidewaysIsRAM{};         // true if bank is RAM

    // ROM banks
    std::array<uint8, 16 * 1024> osROM{};         // 16 KB MOS ($C000..$FFFF)
    std::array<std::array<uint8, 16 * 1024>, 16> sidewaysROM{}; // 16 Sideways ROM banks

    // Banking registers
    uint8 romSelect = 15; // Selected sideways bank (0..15)
    uint8 acccon = 0;     // Master 128 ACCCON ($FE34)
    bool shadowRAMCPU = false;     // CPU accesses shadow RAM at $3000..$7FFF
    bool shadowRAMDisplay = false; // Video CRTC displays from shadow RAM

    // Master 128 CMOS RAM (50 bytes)
    std::array<uint8, 64> cmosRAM{};
    uint8 cmosAddress = 0;
    bool cmosStrobe = false;
    bool cmosDataIn = false;
    bool cmosDataOut = false;

    // I/O Callbacks
    std::function<uint8(uint16)> ioRead;
    std::function<void(uint16, uint8)> ioWrite;

    Memory();

    void reset(BBCModel m);

    // Load ROM files
    bool loadOSROM(const std::string& path);
    bool loadSidewaysROM(int bank, const std::string& path);
    bool loadMasterMegaROM(const std::string& path);

    // CPU Bus Access
    uint8 read(uint16 addr);
    void  write(uint16 addr, uint8 val);

    // Video display memory fetch
    uint8 readDisplay(uint16 addr);

    // Master 128 CMOS interface (via System VIA PB0, PB1, PB2)
    void writeCMOSControl(uint8 pb);
    uint8 readCMOSControl();

private:
    void initCMOS();
};
