#include "fdc1770.h"
#include <fstream>
#include <iostream>
#include <cstring>

FDC1770::FDC1770() {
    reset();
}

void FDC1770::reset() {
    selectedDrive = 0;
    selectedSide = 0;
    motorOn = false;
    status = 0x04; // Track 0 set
    track = 0;
    sector = 0;
    data = 0;
    command = 0;
    sectorBuffer.clear();
    sectorBufIndex = 0;
    dataReady = false;
    busy = false;

    // Intel 8271 state reset
    i8271Status = 0x00;
    i8271Result = 0x00;
    i8271Cmd = 0x00;
    i8271Params.clear();
    i8271ExpectedParams = 0;
}

bool FDC1770::loadDisk(int drive, const std::string& path) {
    if (drive < 0 || drive > 1) return false;
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;

    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);

    diskData[drive].resize(size);
    file.read(reinterpret_cast<char*>(diskData[drive].data()), size);
    diskPath[drive] = path;
    driveLoaded[drive] = true;

    // Check if DSD (double sided, typically > 200KB or .dsd extension)
    isDoubleSided[drive] = (size > 204800) || (path.size() >= 4 && path.substr(path.size() - 4) == ".dsd");

    return true;
}

bool FDC1770::loadDiskFromMemory(int drive, const uint8* dataPtr, size_t size, const std::string& name) {
    if (drive < 0 || drive > 1 || !dataPtr || size == 0) return false;
    diskData[drive].assign(dataPtr, dataPtr + size);
    diskPath[drive] = name;
    driveLoaded[drive] = true;
    isDoubleSided[drive] = (size > 204800);
    return true;
}

void FDC1770::ejectDisk(int drive) {
    if (drive >= 0 && drive <= 1) {
        driveLoaded[drive] = false;
        diskPath[drive].clear();
        diskData[drive].clear();
    }
}

uint32 FDC1770::getSectorOffset(int drive, int trk, int sec, int side) {
    if (!driveLoaded[drive]) return 0;
    if (isDoubleSided[drive]) {
        // Interleaved sides: track * 20 + side * 10 + sector
        return ((trk * 2 + side) * 10 + sec) * 256;
    } else {
        return (trk * 10 + sec) * 256;
    }
}

uint8 FDC1770::readRegister(uint16 addr) {
    uint8 reg = (addr & 0x07);
    switch (reg) {
        case 0: // Status Register ($FE80)
            if (i8271Params.size() < (size_t)i8271ExpectedParams) {
                return 0x80; // Busy receiving parameters, param register ready (bit 5=0)
            }
            if (i8271Status != 0) {
                return i8271Status;
            }
            return status;

        case 1: // Result Register ($FE81 for 8271) / Track Register (1770)
            if (i8271Status & 0x10) {
                uint8 res = i8271Result;
                i8271Status &= ~0x18; // Clear result ready
                return res;
            }
            return track;

        case 2: // Sector Register ($FE82 or $FE88)
            return sector;

        case 3: // Data Register ($FE83 or $FE8C)
            if (busy && dataReady && sectorBufIndex < sectorBuffer.size()) {
                data = sectorBuffer[sectorBufIndex++];
                if (sectorBufIndex >= sectorBuffer.size()) {
                    busy = false;
                    dataReady = false;
                    status &= ~0x03; // Clear Busy and DRQ
                }
            }
            return data;

        case 4: // Drive control / status ($FE84)
            if (busy && dataReady && sectorBufIndex < sectorBuffer.size()) {
                data = sectorBuffer[sectorBufIndex++];
                if (sectorBufIndex >= sectorBuffer.size()) {
                    busy = false;
                    dataReady = false;
                    status &= ~0x03;
                }
                return data;
            }
            return (driveLoaded[selectedDrive] ? 0x00 : 0x80) | (motorOn ? 0x20 : 0);
    }
    return 0xFF;
}

void FDC1770::writeRegister(uint16 addr, uint8 val) {
    uint8 reg = (addr & 0x07);
    switch (reg) {
        case 0: { // Command Register ($FE80)
            command = val;
            i8271Cmd = val;
            i8271Params.clear();

            uint8 baseCmd = val & 0x1F;
            if (baseCmd == 0x13 || baseCmd == 0x12 || baseCmd == 0x17 || baseCmd == 0x16) {
                // Read Data: 3 parameters (Track, Sector, Count)
                i8271ExpectedParams = 3;
                i8271Status = 0x80;
            } else if (baseCmd == 0x0B || baseCmd == 0x0A) {
                // Write Data: 3 parameters (Track, Sector, Count)
                i8271ExpectedParams = 3;
                i8271Status = 0x80;
            } else if (baseCmd == 0x09 || val == 0x29) {
                // Seek: 1 parameter (Track)
                i8271ExpectedParams = 1;
                i8271Status = 0x80;
            } else if (val == 0x35) {
                // Specify: 4 parameters
                i8271ExpectedParams = 4;
                i8271Status = 0x80;
            } else if (val == 0x3A) {
                // Write Special: 2 parameters
                i8271ExpectedParams = 2;
                i8271Status = 0x80;
            } else if (val == 0x3D) {
                // Read Special: 1 parameter
                i8271ExpectedParams = 1;
                i8271Status = 0x80;
            } else if (baseCmd == 0x0C || val == 0x2C || val == 0x6C || val == 0xAC || val == 0xEC) {
                // Read Drive Status: 0 parameters, complete immediately!
                i8271ExpectedParams = 0;
                i8271Status = 0x18;
                i8271Result = 0x84; // Bit 2 = 1 (Drive 0 Ready)
            } else if (baseCmd == 0x23 || baseCmd == 0x22) {
                // Read ID: 1 parameter
                i8271ExpectedParams = 1;
                i8271Status = 0x80;
            } else {
                i8271ExpectedParams = 0;
                i8271Status = 0x18;
                i8271Result = 0x00;
            }

            executeCommand(val);
            break;
        }

        case 1: // Track Register ($FE81) / 8271 Parameter Register
            track = val;
            if (i8271Params.size() < (size_t)i8271ExpectedParams) {
                i8271Params.push_back(val);
                if (i8271Params.size() >= (size_t)i8271ExpectedParams) {
                    execute8271Command();
                }
            }
            break;

        case 2: // Sector Register ($FE82) / 8271 Reset Register
            sector = val;
            if (val == 0x01) {
                i8271Status = 0x00;
                i8271Result = 0x00;
                i8271Params.clear();
                i8271ExpectedParams = 0;
            }
            break;

        case 3: // Data Register ($FE83)
            data = val;
            break;

        case 4: // Drive / Control select ($FE84 / $FE90)
            selectedDrive = val & 0x01;
            selectedSide = (val & 0x10) ? 1 : 0;
            motorOn = (val & 0x20) != 0;
            data = val;
            break;
    }
}

void FDC1770::executeCommand(uint8 cmd) {
    uint8 type = cmd >> 4;

    switch (type) {
        case 0x00: // Restore (seek to track 0)
            track = 0;
            status = 0x04; // Track 0 flag set
            busy = false;
            break;

        case 0x01: // Seek
            track = data;
            status = (track == 0) ? 0x04 : 0x00;
            busy = false;
            break;

        case 0x02: // Step
        case 0x03:
        case 0x04: // Step In
        case 0x05:
            track++;
            status = 0x00;
            busy = false;
            break;

        case 0x06: // Step Out
        case 0x07:
            if (track > 0) track--;
            status = (track == 0) ? 0x04 : 0x00;
            busy = false;
            break;

        case 0x08: // Read Sector
        case 0x09: {
            if (!driveLoaded[selectedDrive]) {
                status = 0x10; // Record not found
                busy = false;
                break;
            }
            uint32 offset = getSectorOffset(selectedDrive, track, sector, selectedSide);
            if (offset + 256 <= diskData[selectedDrive].size()) {
                sectorBuffer.resize(256);
                std::memcpy(sectorBuffer.data(), diskData[selectedDrive].data() + offset, 256);
                sectorBufIndex = 0;
                busy = true;
                dataReady = true;
                status = 0x03; // Busy (bit 0) | DRQ (bit 1)
            } else {
                status = 0x10; // Record not found
                busy = false;
            }
            break;
        }

        case 0x0D: // Force Interrupt
            busy = false;
            dataReady = false;
            status = (track == 0) ? 0x04 : 0x00;
            break;

        default:
            busy = false;
            break;
    }
}

void FDC1770::step(int cycles) {
    // Sector data ready transfers happen synchronously via data register reads
}

std::string FDC1770::getDiskTitle(int drive) const {
    if (!driveLoaded[drive] || diskData[drive].size() < 512) return "";
    std::string title;
    for (int i = 0; i < 8; i++) {
        char c = diskData[drive][i];
        if (c >= 32 && c < 127) title += c;
    }
    for (int i = 0; i < 4; i++) {
        char c = diskData[drive][256 + i];
        if (c >= 32 && c < 127) title += c;
    }
    return title;
}

uint8 FDC1770::getBootOption(int drive) const {
    if (!driveLoaded[drive] || diskData[drive].size() < 512) return 0;
    // DFS Sector 1, byte 6, bits 5-4: boot option (0=none,1=LOAD,2=RUN,3=EXEC)
    return (diskData[drive][256 + 6] >> 4) & 0x03;
}

std::vector<DFSFileEntry> FDC1770::getCatalog(int drive) const {
    std::vector<DFSFileEntry> files;
    if (!driveLoaded[drive] || diskData[drive].size() < 512) return files;

    // Sector 1, byte 5: bottom bits (fileCount * 8)
    int fileCount = (diskData[drive][256 + 5] & 0x7F) / 8;
    if (fileCount > 31) fileCount = 31;

    for (int i = 0; i < fileCount; i++) {
        DFSFileEntry e;
        int nameOffset = 8 + i * 8;
        int metaOffset = 256 + 8 + i * 8;

        std::string fname;
        for (int c = 0; c < 7; c++) {
            char ch = diskData[drive][nameOffset + c] & 0x7F;
            if (ch > 32 && ch < 127) fname += ch;
        }
        e.name = fname;
        e.directory = diskData[drive][nameOffset + 7] & 0x7F;
        e.locked = (diskData[drive][nameOffset + 7] & 0x80) != 0;

        uint8 l0 = diskData[drive][metaOffset + 0];  // Load addr lo
        uint8 l1 = diskData[drive][metaOffset + 1];  // Load addr hi
        uint8 e0 = diskData[drive][metaOffset + 2];  // Exec addr lo
        uint8 e1 = diskData[drive][metaOffset + 3];  // Exec addr hi
        uint8 len0 = diskData[drive][metaOffset + 4]; // Length lo
        uint8 len1 = diskData[drive][metaOffset + 5]; // Length hi
        // Byte 6: bits 7-6=exec hi2, bits 5-4=len hi2, bits 3-2=load hi2, bits 1-0=startSector hi2
        uint8 misc = diskData[drive][metaOffset + 6];
        uint8 sec8 = diskData[drive][metaOffset + 7]; // Sector lo8

        e.loadAddress  = l0 | (l1 << 8) | (((misc >> 2) & 0x03) << 16);
        e.execAddress  = e0 | (e1 << 8) | (((misc >> 6) & 0x03) << 16);
        e.length       = len0 | (len1 << 8) | (((misc >> 4) & 0x03) << 16);
        e.startSector  = sec8 | ((misc & 0x03) << 8);

        files.push_back(e);
    }

    return files;
}

void FDC1770::execute8271Command() {
    uint8 baseCmd = i8271Cmd & 0x1F;
    int drv = (i8271Cmd & 0x80) ? 1 : selectedDrive;
    if (drv < 0 || drv > 1) drv = 0;

    if (baseCmd == 0x13 || baseCmd == 0x12 || baseCmd == 0x17 || baseCmd == 0x16) {
        // Read Data (Sectors)
        uint8 trk = i8271Params.size() > 0 ? i8271Params[0] : 0;
        uint8 sec = i8271Params.size() > 1 ? i8271Params[1] : 0;
        uint8 cnt = i8271Params.size() > 2 ? (i8271Params[2] & 0x1F) : 1;
        if (cnt == 0) cnt = 1;

        track = trk;
        sector = sec;

        uint16 dest = 0;
        if (memRead) {
            dest = memRead(0xA6) | (memRead(0xA7) << 8);
        }

        uint32 sectorsToRead = cnt;
        if (memRead) {
            uint8 a4 = memRead(0xA4);
            uint8 a5 = memRead(0xA5);
            if (a5 > 0) {
                uint32 total = (uint32)(a5 - 1) * 256 + a4;
                if (total >= cnt) {
                    sectorsToRead = total;
                }
            }
        }

        std::cout << "[8271] READ DATA: drv=" << drv << " trk=" << (int)trk
                  << " sec=" << (int)sec << " cnt=" << (int)cnt
                  << " totalSecs=" << sectorsToRead
                  << " dest=0x" << std::hex << dest << std::dec << "\n";

        if (driveLoaded[drv]) {
            uint32 startLinear = isDoubleSided[drv] ? ((trk * 2 + selectedSide) * 10 + sec) : (trk * 10 + sec);
            for (uint32 s = 0; s < sectorsToRead; s++) {
                uint32 offset = (startLinear + s) * 256;
                for (int b = 0; b < 256; b++) {
                    uint8 byteVal = 0;
                    if (offset + b < diskData[drv].size()) {
                        byteVal = diskData[drv][offset + b];
                    }
                    if (memWrite) {
                        memWrite(dest + s * 256 + b, byteVal);
                    }
                }
            }

            uint16 newDest = dest + sectorsToRead * 256;
            if (memWrite) {
                memWrite(0xA6, newDest & 0xFF);
                memWrite(0xA7, (newDest >> 8) & 0xFF);

                memWrite(0xA3, 0);
                memWrite(0xA4, 0);
                memWrite(0xA5, 0);
            }

            track = (startLinear + sectorsToRead) / 10;
            sector = (startLinear + sectorsToRead) % 10;

            i8271Result = 0x00; // Success
            i8271Status = 0x18; // Result Full + IRQ
        } else {
            i8271Result = 0x10; // Drive not ready
            i8271Status = 0x18;
        }
    } else if (baseCmd == 0x0B || baseCmd == 0x0A) {
        // Write Data
        uint8 trk = i8271Params.size() > 0 ? i8271Params[0] : 0;
        uint8 sec = i8271Params.size() > 1 ? i8271Params[1] : 0;
        uint8 cnt = i8271Params.size() > 2 ? (i8271Params[2] & 0x1F) : 1;
        if (cnt == 0) cnt = 1;

        track = trk;
        sector = sec;

        uint16 src = 0;
        if (memRead) {
            src = memRead(0xA6) | (memRead(0xA7) << 8);
        }

        uint32 sectorsToWrite = cnt;
        if (memRead) {
            uint8 a4 = memRead(0xA4);
            uint8 a5 = memRead(0xA5);
            if (a5 > 0) {
                uint32 total = (uint32)(a5 - 1) * 256 + a4;
                if (total >= cnt) {
                    sectorsToWrite = total;
                }
            }
        }

        if (driveLoaded[drv]) {
            uint32 startLinear = isDoubleSided[drv] ? ((trk * 2 + selectedSide) * 10 + sec) : (trk * 10 + sec);
            for (uint32 s = 0; s < sectorsToWrite; s++) {
                uint32 offset = (startLinear + s) * 256;
                for (int b = 0; b < 256; b++) {
                    if (offset + b < diskData[drv].size()) {
                        diskData[drv][offset + b] = memRead ? memRead(src + s * 256 + b) : 0;
                    }
                }
            }

            uint16 newSrc = src + sectorsToWrite * 256;
            if (memWrite) {
                memWrite(0xA6, newSrc & 0xFF);
                memWrite(0xA7, (newSrc >> 8) & 0xFF);

                memWrite(0xA3, 0);
                memWrite(0xA4, 0);
                memWrite(0xA5, 0);
            }

            track = (startLinear + sectorsToWrite) / 10;
            sector = (startLinear + sectorsToWrite) % 10;

            i8271Result = 0x00;
            i8271Status = 0x18;
        } else {
            i8271Result = 0x10;
            i8271Status = 0x18;
        }
    } else if (baseCmd == 0x09 || i8271Cmd == 0x29) {
        // Seek
        if (!i8271Params.empty()) {
            track = i8271Params[0];
        }
        i8271Result = 0x00;
        i8271Status = 0x18;
    } else {
        i8271Result = 0x00;
        i8271Status = 0x18;
    }
}

