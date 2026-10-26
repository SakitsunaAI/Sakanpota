#include "memory.h"
#include "embedded_roms.h"
#include <fstream>
#include <iostream>
#include <cstring>

Memory::Memory() {
    reset(BBCModel::ModelB);
}

void Memory::reset(BBCModel m) {
    model = m;
    mainRAM.fill(0);
    shadowRAM.fill(0);
    hazelRAM.fill(0);
    romSelect = 15;
    acccon = 0;
    shadowRAMCPU = false;
    shadowRAMDisplay = false;

    // By default for Model B / Master 128
    for (int i = 0; i < 16; i++) {
        sidewaysIsRAM[i] = false;
        sidewaysRAM[i].fill(0);
    }

    if (model == BBCModel::Master128) {
        // Banks 0..3 are Sideways RAM on Master 128
        for (int i = 0; i < 4; i++) {
            sidewaysIsRAM[i] = true;
        }
        initCMOS();
    } else if (model == BBCModel::ModelBPlus) {
        // Banks 4..7 can be Sideways RAM
        for (int i = 4; i < 8; i++) {
            sidewaysIsRAM[i] = true;
        }
    }
}

void Memory::initCMOS() {
    cmosRAM.fill(0);
    // Standard BBC Master 128 CMOS configuration
    // Address 0..9: Clock/Alarm
    // Address 10..49: Configuration options
    // Config byte 0 (CMOS addr 5): Default screen mode (Mode 7)
    cmosRAM[5] = 0x07;  // Mode 7
    cmosRAM[6] = 0x00;
    cmosRAM[7] = 0x00;
    cmosRAM[8] = 0x00;
    cmosRAM[9] = 0x00;

    // Bytes 10..48
    cmosRAM[10] = 0x00;
    cmosRAM[11] = 0x00;
    cmosRAM[12] = 0x00;
    cmosRAM[13] = 0x00;
    cmosRAM[14] = 0x00;
    cmosRAM[15] = 0x00;
    cmosRAM[16] = 0x00;
    cmosRAM[17] = 0x00;
    cmosRAM[18] = 0x00;
    cmosRAM[19] = 0x00;
    cmosRAM[20] = 0x00; // Filing system default

    // Calculate Master 128 CMOS Checksum at byte 49
    uint8 sum = 0;
    for (int i = 10; i < 49; i++) {
        sum += cmosRAM[i];
    }
    cmosRAM[49] = sum;
}

bool Memory::loadOSROM(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (file.is_open()) {
        file.read(reinterpret_cast<char*>(osROM.data()), osROM.size());
        if (file.gcount() == static_cast<std::streamsize>(osROM.size())) return true;
    }
    // Extract filename from path
    std::string filename = path;
    size_t lastSlash = filename.find_last_of("/\\");
    if (lastSlash != std::string::npos) filename = filename.substr(lastSlash + 1);

    const uint8* romData = nullptr;
    size_t romSize = 0;
    if (EmbeddedROMs::getROM(filename, romData, romSize) && romData && romSize >= osROM.size()) {
        std::memcpy(osROM.data(), romData, osROM.size());
        return true;
    }
    return false;
}

bool Memory::loadSidewaysROM(int bank, const std::string& path) {
    if (bank < 0 || bank > 15) return false;
    std::ifstream file(path, std::ios::binary);
    if (file.is_open()) {
        file.read(reinterpret_cast<char*>(sidewaysROM[bank].data()), sidewaysROM[bank].size());
        sidewaysIsRAM[bank] = false;
        return true;
    }
    // Fallback to Embedded ROMs
    std::string filename = path;
    size_t lastSlash = filename.find_last_of("/\\");
    if (lastSlash != std::string::npos) filename = filename.substr(lastSlash + 1);

    const uint8* romData = nullptr;
    size_t romSize = 0;
    if (EmbeddedROMs::getROM(filename, romData, romSize) && romData) {
        size_t copySize = std::min(romSize, sidewaysROM[bank].size());
        std::memcpy(sidewaysROM[bank].data(), romData, copySize);
        sidewaysIsRAM[bank] = false;
        return true;
    }
    return false;
}

bool Memory::loadMasterMegaROM(const std::string& path) {
    std::vector<uint8> buffer(128 * 1024);
    bool loaded = false;

    std::ifstream file(path, std::ios::binary);
    if (file.is_open()) {
        file.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
        if (file.gcount() == 128 * 1024) loaded = true;
    }
    if (!loaded) {
        // Fallback to Embedded MegaROM
        std::string filename = path;
        size_t lastSlash = filename.find_last_of("/\\");
        if (lastSlash != std::string::npos) filename = filename.substr(lastSlash + 1);

        const uint8* romData = nullptr;
        size_t romSize = 0;
        if (EmbeddedROMs::getROM(filename, romData, romSize) && romData && romSize >= buffer.size()) {
            std::memcpy(buffer.data(), romData, buffer.size());
            loaded = true;
        }
    }
    if (!loaded) return false;

    // Bank 8 is MOS ROM ($C000..$FFFF)
    std::memcpy(osROM.data(), buffer.data(), 16 * 1024);

    // Banks 9..15 are sideways ROMs
    for (int i = 1; i < 8; i++) {
        std::memcpy(sidewaysROM[8 + i].data(), buffer.data() + i * 16 * 1024, 16 * 1024);
        sidewaysIsRAM[8 + i] = false;
    }
    return true;
}

uint8 Memory::read(uint16 addr) {
    // --- 0x0000 .. 0x7FFF (RAM) ---
    if (addr < 0x8000) {
        if (model == BBCModel::ModelA) {
            // Model A has 16KB RAM at $0000..$3FFF
            if (addr < 0x4000) return mainRAM[addr];
            return mainRAM[addr & 0x3FFF]; // echo
        }

        if (shadowRAMCPU && addr >= 0x3000) {
            return shadowRAM[addr - 0x3000];
        }
        return mainRAM[addr];
    }

    // --- 0x8000 .. 0xBFFF (Sideways ROM / RAM) ---
    if (addr < 0xC000) {
        uint16 offset = addr - 0x8000;
        uint8 bank = romSelect & 0x0F;
        if (sidewaysIsRAM[bank]) {
            return sidewaysRAM[bank][offset];
        }
        return sidewaysROM[bank][offset];
    }

    // --- 0xC000 .. 0xFFFF (MOS / Hazel / I/O) ---
    if (addr >= 0xFE00 && addr <= 0xFEFF) {
        // Memory mapped I/O
        if (ioRead) return ioRead(addr);
        return 0xFF;
    }

    // Master 128 Hazel RAM at $C000..$CFFF
    if (model == BBCModel::Master128 && (acccon & 0x08) && addr < 0xD000) {
        return hazelRAM[addr - 0xC000];
    }

    // MOS ROM ($C000..$FFFF)
    return osROM[addr - 0xC000];
}

void Memory::write(uint16 addr, uint8 val) {
    // --- 0x0000 .. 0x7FFF (RAM) ---
    if (addr < 0x8000) {
        if (model == BBCModel::ModelA) {
            if (addr < 0x4000) mainRAM[addr] = val;
            return;
        }

        if (shadowRAMCPU && addr >= 0x3000) {
            shadowRAM[addr - 0x3000] = val;
            return;
        }
        mainRAM[addr] = val;
        return;
    }

    // --- 0x8000 .. 0xBFFF (Sideways ROM / RAM) ---
    if (addr < 0xC000) {
        uint8 bank = romSelect & 0x0F;
        if (sidewaysIsRAM[bank]) {
            sidewaysRAM[bank][addr - 0x8000] = val;
        }
        return;
    }

    // --- 0xC000 .. 0xFFFF (MOS / Hazel / I/O) ---
    if (addr >= 0xFE00 && addr <= 0xFEFF) {
        // ROM select register ($FE30)
        if (addr >= 0xFE30 && addr <= 0xFE33) {
            romSelect = val & 0x0F;
            return;
        }
        // Master 128 ACCCON ($FE34)
        if (model == BBCModel::Master128 && addr == 0xFE34) {
            acccon = val;
            shadowRAMCPU = (acccon & 0x04) != 0;
            shadowRAMDisplay = (acccon & 0x01) != 0;
            return;
        }

        if (ioWrite) ioWrite(addr, val);
        return;
    }

    // Master 128 Hazel RAM write at $C000..$CFFF
    if (model == BBCModel::Master128 && (acccon & 0x08) && addr < 0xD000) {
        hazelRAM[addr - 0xC000] = val;
        return;
    }

    // Writes to ROM are ignored
}

uint8 Memory::readDisplay(uint16 addr) {
    addr &= 0x7FFF;
    if (shadowRAMDisplay && addr >= 0x3000) {
        return shadowRAM[addr - 0x3000];
    }
    return mainRAM[addr];
}

void Memory::writeCMOSControl(uint8 pb) {
    // PB0: CMOS Data
    // PB1: CMOS Clock
    // PB2: CMOS Chip Select / Strobe
    bool cs = (pb & 0x04) != 0;
    bool clk = (pb & 0x02) != 0;
    bool data = (pb & 0x01) != 0;

    static bool oldClk = false;
    static int bitCount = 0;
    static uint8 shiftReg = 0;
    static bool reading = false;

    if (!cs) {
        bitCount = 0;
        reading = false;
        cmosDataOut = false;
        oldClk = clk;
        return;
    }

    // Rising edge of clock
    if (clk && !oldClk) {
        if (!reading) {
            shiftReg = (shiftReg << 1) | (data ? 1 : 0);
            bitCount++;
            if (bitCount == 8) {
                // Command byte: bit 7 = read(1)/write(0), bits 0..5 = address
                reading = (shiftReg & 0x80) != 0;
                cmosAddress = shiftReg & 0x3F;
                bitCount = 0;
                if (reading) {
                    shiftReg = cmosRAM[cmosAddress % cmosRAM.size()];
                }
            }
        } else {
            // Outputting data
            cmosDataOut = (shiftReg & 0x80) != 0;
            shiftReg <<= 1;
            bitCount++;
            if (bitCount == 8) {
                reading = false;
                bitCount = 0;
            }
        }
    }
    oldClk = clk;
}

uint8 Memory::readCMOSControl() {
    return cmosDataOut ? 0x01 : 0x00;
}
