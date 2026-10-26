#pragma once

#include "types.h"
#include <string>
#include <vector>
#include <functional>

struct DFSFileEntry {
    std::string name;
    char directory = '$';
    uint32 loadAddress = 0;
    uint32 execAddress = 0;
    uint32 length = 0;
    uint32 startSector = 0;
    bool locked = false;
};

class FDC1770 {
public:
    // Drive status
    bool driveLoaded[2] = { false, false };
    std::string diskPath[2];
    std::vector<uint8> diskData[2];
    bool isDoubleSided[2] = { false, false };

    // Current drive / head
    int selectedDrive = 0;
    int selectedSide = 0;
    bool motorOn = false;

    // WD1770 Registers
    uint8 status = 0;
    uint8 track = 0;
    uint8 sector = 0;
    uint8 data = 0;
    uint8 command = 0;

    // Buffer for sector read/write
    std::vector<uint8> sectorBuffer;
    size_t sectorBufIndex = 0;
    bool dataReady = false;
    bool busy = false;

    // Intel 8271 Controller Emulation (for BBC Model A / B / B+ DFS 0.90)
    uint8 i8271Status = 0;
    uint8 i8271Result = 0;
    uint8 i8271Cmd = 0;
    std::vector<uint8> i8271Params;
    int i8271ExpectedParams = 0;

    // Direct memory callbacks for sector transfers
    std::function<uint8(uint16)> memRead;
    std::function<void(uint16, uint8)> memWrite;

    FDC1770();

    void reset();

    bool loadDisk(int drive, const std::string& path);
    bool loadDiskFromMemory(int drive, const uint8* data, size_t size, const std::string& name = "embedded.ssd");
    void ejectDisk(int drive);

    // Register access at $FE80..$FE8F
    uint8 readRegister(uint16 addr);
    void  writeRegister(uint16 addr, uint8 val);

    // Step / transfer
    void step(int cycles);

    // DFS catalog parser
    std::vector<DFSFileEntry> getCatalog(int drive) const;
    std::string getDiskTitle(int drive) const;
    uint8 getBootOption(int drive) const; // 0=None, 1=*LOAD, 2=*RUN, 3=*EXEC

    uint32 getSectorOffset(int drive, int trk, int sec, int side);

private:
    void executeCommand(uint8 cmd);
    void execute8271Command();
};
