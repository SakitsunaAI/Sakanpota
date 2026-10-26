#include "bbc_machine.h"
#include "model_detector.h"
#include "embedded_roms.h"
#include <iostream>

BBCMachine::BBCMachine() {
    rawFramebuffer.resize(CRTC6845::SCREEN_WIDTH * CRTC6845::SCREEN_HEIGHT, 0xFF000000);
    crtFramebuffer.resize(CRTC6845::SCREEN_WIDTH * CRTC6845::SCREEN_HEIGHT, 0xFF000000);
    audioBuffer.resize(SAMPLES_PER_FRAME, 0);

    init();
}

void BBCMachine::init() {
    setupBus();
    switchModel(BBCModel::ModelB);
    // Pre-mount embedded authentic BBC Welcome system disc into Drive 0
    fdc.loadDiskFromMemory(0, EmbeddedROMs::BBC_WELCOME_SSD, sizeof(EmbeddedROMs::BBC_WELCOME_SSD), "Welcome.ssd");
}

void BBCMachine::setupBus() {
    // Connect CPU memory access to Memory subsystem
    cpu.readMem = [this](uint16 addr) -> uint8 {
        return memory.read(addr);
    };

    cpu.writeMem = [this](uint16 addr, uint8 val) {
        memory.write(addr, val);
    };

    // Connect FDC memory access for direct sector DMA/transfers
    fdc.memRead = [this](uint16 addr) -> uint8 {
        return memory.read(addr);
    };
    fdc.memWrite = [this](uint16 addr, uint8 val) {
        memory.write(addr, val);
    };

    // Connect Memory I/O handlers ($FE00..$FEFF)
    memory.ioRead = [this](uint16 addr) -> uint8 {
        if (addr >= 0xFE00 && addr <= 0xFE07) {
            return crtc.readCRTC(addr);
        }
        if (addr >= 0xFE08 && addr <= 0xFE0F) {
            return 0x02; // 6850 ACIA status: Tx empty, NO IRQ (bit 7 must be 0)
        }
        if (addr >= 0xFE20 && addr <= 0xFE2F) {
            return crtc.readULA(addr);
        }
        if (addr >= 0xFE40 && addr <= 0xFE4F) {
            return sysVia.read(addr & 0x0F);
        }
        if (addr >= 0xFE60 && addr <= 0xFE6F) {
            return userVia.read(addr & 0x0F);
        }
        if (addr >= 0xFE80 && addr <= 0xFE8F) {
            return fdc.readRegister(addr);
        }
        return 0xFF;
    };

    memory.ioWrite = [this](uint16 addr, uint8 val) {
        if (addr >= 0xFE00 && addr <= 0xFE07) {
            crtc.writeCRTC(addr, val);
            return;
        }
        if (addr >= 0xFE20 && addr <= 0xFE2F) {
            crtc.writeULA(addr, val);
            return;
        }
        if (addr >= 0xFE40 && addr <= 0xFE4F) {
            sysVia.write(addr & 0x0F, val);
            return;
        }
        if (addr >= 0xFE60 && addr <= 0xFE6F) {
            userVia.write(addr & 0x0F, val);
            return;
        }
        if (addr >= 0xFE80 && addr <= 0xFE8F) {
            fdc.writeRegister(addr, val);
            return;
        }
    };

    // Connect CRTC to display memory
    crtc.readScreenRAM = [this](uint16 addr) -> uint8 {
        return memory.readDisplay(addr);
    };

    // Connect Debugger to memory
    debugger.readMem = [this](uint16 addr) -> uint8 {
        return memory.read(addr);
    };

    // System VIA Handlers
    sysVia.onPortAWrite = [this](uint8 val) {
        // Output on slow data bus (Keyboard column latch or Sound data)
        uint8 col = val & 0x0F;
        keyboard.setColumn(col);
        if ((ic32Latch & 0x08) == 0 && col < 10) {
            if (keyboard.hasAnyKeyDownInColumn(col)) {
                sysVia.ifr |= 0x01; // CA2 flag in System VIA for keyboard scan
                sysVia.updateIFR();
            }
        }
    };

    sysVia.onPortARead = [this]() -> uint8 {
        uint8 val = sysVia.ora;
        uint8 row = (val >> 4) & 7;
        uint8 col = val & 0x0F;
        bool pressed = false;
        if (col < 10 && row < 8) {
            pressed = keyboard.matrix[col][row];
        }
        if (pressed) {
            val |= 0x80;
        } else {
            val &= 0x7F; // Bit 7 = 0 when not pressed
        }
        return val;
    };

    sysVia.onPortBWrite = [this](uint8 val) {
        // BBC Micro IC32 (74LS259 8-bit Addressable Latch)
        // PB0..PB2: Latch Address (0..7)
        // PB3: Latch Data bit
        uint8 bitAddr = val & 0x07;
        bool bitVal = (val & 0x08) != 0;

        if (bitVal) {
            ic32Latch |= (1 << bitAddr);
        } else {
            ic32Latch &= ~(1 << bitAddr);
        }

        // IC32 Bit 0: Sound write enable (active low)
        if (bitAddr == 0 && !bitVal) {
            sound.write(sysVia.ora);
        }

        // IC32 Bit 3: Keyboard write enable / 74LS145 enable (active low)
        keyboard.setEnable((ic32Latch & 0x08) == 0);
        if ((ic32Latch & 0x08) == 0 && keyboard.selectedColumn < 10) {
            if (keyboard.hasAnyKeyDownInColumn(keyboard.selectedColumn)) {
                sysVia.ifr |= 0x01; // CA2 flag in System VIA
                sysVia.updateIFR();
            }
        }

        if (model == BBCModel::Master128) {
            memory.writeCMOSControl(val);
        }
    };

    sysVia.onPortBRead = [this]() -> uint8 {
        uint8 val = 0xF0; // PB4, PB5, PB6, PB7 default high (pull-ups; no speech chip)
        if (model == BBCModel::Master128) {
            val |= memory.readCMOSControl();
        }
        return val;
    };

    sysVia.onIRQ = [this](bool active) {
        updateInterrupts();
    };

    userVia.onIRQ = [this](bool active) {
        updateInterrupts();
    };
}

void BBCMachine::updateInterrupts() {
    if (sysVia.isIRQPending() || userVia.isIRQPending()) {
        cpu.triggerIRQ();
    } else {
        cpu.clearIRQ();
    }
}

void BBCMachine::switchModel(BBCModel m) {
    model = m;
    cpu.is65C02 = (model == BBCModel::Master128);
    memory.reset(model);
    sysVia.reset();
    userVia.reset();
    sound.reset();
    crtc.reset();
    fdc.reset();
    keyboard.reset();
    debugger.reset();

    loadModelROMs();
    reset();
}

void BBCMachine::reset() {
    cpu.reset();
    sysVia.reset();
    userVia.reset();
    sound.reset();
    crtc.reset();
    ic32Latch = 0;

    if (fdc.driveLoaded[0] && fdc.diskData[0].size() >= 512) {
        std::memcpy(memory.mainRAM.data() + 0x0E00, fdc.diskData[0].data(), 256);
        std::memcpy(memory.mainRAM.data() + 0x0F00, fdc.diskData[0].data() + 256, 256);
    }
}

bool BBCMachine::loadModelROMs() {
    bool ok = true;
    switch (model) {
        case BBCModel::ModelA:
            ok &= memory.loadOSROM(romDirectory + "/os01.rom");
            ok &= memory.loadSidewaysROM(15, romDirectory + "/BASIC.ROM");
            break;

        case BBCModel::ModelB:
            ok &= memory.loadOSROM(romDirectory + "/os.rom");
            ok &= memory.loadSidewaysROM(15, romDirectory + "/BASIC.ROM");
            // Also load DFS into bank 14
            memory.loadSidewaysROM(14, romDirectory + "/DFS-0.9.rom");
            break;

        case BBCModel::ModelBPlus:
            ok &= memory.loadOSROM(romDirectory + "/bpos.rom");
            ok &= memory.loadSidewaysROM(15, romDirectory + "/BASIC.ROM");
            memory.loadSidewaysROM(14, romDirectory + "/DFS-0.9.rom");
            break;

        case BBCModel::Master128:
            ok &= memory.loadMasterMegaROM(romDirectory + "/mos3.20");
            break;
    }

    if (!ok) {
        std::cerr << "Warning: Could not load some ROMs for " << getModelName(model)
                  << " from directory '" << romDirectory << "'\n";
    }
    return ok;
}

bool BBCMachine::insertDisk(int drive, const std::string& path, bool autoSwitchModel) {
    if (!fdc.loadDisk(drive, path)) {
        return false;
    }

    if (autoSwitchModel) {
        DetectionResult det = ModelDetector::detectFromDisk(path, fdc.diskData[drive], fdc, drive);
        autoDetectInfo = std::string(getModelName(det.model)) + " (" + std::to_string(det.confidence) + "%)";
        std::cout << ">>> [AUTO-DETECT] Disk '" << path << "' inserted <<<\n"
                  << "    Target Model: " << getModelName(det.model) << "\n"
                  << "    Reason: " << det.reason << " (Confidence: " << det.confidence << "%)\n";

        if (det.model != model) {
            std::cout << "    [Auto-Switch] Switching machine model to " << getModelName(det.model) << "...\n";
            switchModel(det.model);
            // Re-load the disk into the new model state
            fdc.loadDisk(drive, path);
        }
    }

    if (drive == 0 && fdc.driveLoaded[0] && fdc.diskData[0].size() >= 512) {
        std::memcpy(memory.mainRAM.data() + 0x0E00, fdc.diskData[0].data(), 256);
        std::memcpy(memory.mainRAM.data() + 0x0F00, fdc.diskData[0].data() + 256, 256);
    }

    return true;
}

void BBCMachine::autoBootDisk() {
    reset();

    // Wait for MOS + DFS ROM + BASIC to fully initialise (100 frames = 2s at 50Hz)
    keyboard.pasteDelay = 100;

    // Inspect disk catalog for boot files
    auto files = fdc.getCatalog(0);
    uint8 bootOpt = fdc.getBootOption(0);

    std::cout << "[BOOT] Disk has " << files.size() << " files, bootOpt=" << (int)bootOpt << "\n";
    for (const auto& f : files) {
        std::cout << "[BOOT]   File: '" << f.name << "' sector=" << f.startSector
                  << " len=" << f.length << "\n";
    }
    bool hasBoot = false;
    uint8 bootByte0 = 0;
    std::string bootText;

    for (const auto& f : files) {
        if (f.name == "!BOOT") {
            hasBoot = true;
            uint32 offset = f.startSector * 256;
            if (offset < fdc.diskData[0].size()) {
                bootByte0 = fdc.diskData[0][offset];
                size_t len = std::min((size_t)f.length, fdc.diskData[0].size() - offset);
                bootText = std::string(reinterpret_cast<const char*>(fdc.diskData[0].data() + offset), len);
            }
            break;
        }
    }

    if (hasBoot) {
        // Check if !BOOT contains a CHAIN directive (e.g. CHAIN "AIRGAME" or CHAIN"AIRGAME")
        std::string chainTarget;
        size_t chainPos = bootText.find("CHAIN");
        if (chainPos != std::string::npos) {
            size_t q1 = bootText.find("\"", chainPos);
            if (q1 != std::string::npos) {
                size_t q2 = bootText.find("\"", q1 + 1);
                if (q2 != std::string::npos) {
                    chainTarget = bootText.substr(q1 + 1, q2 - q1 - 1);
                }
            }
        }

        if (!chainTarget.empty()) {
            std::cout << "[BOOT] !BOOT script chains target: '" << chainTarget << "'\n";
            keyboard.pasteText("\rCHAIN \"" + chainTarget + "\"\r");
        } else if (bootOpt == 3) {
            std::cout << "[BOOT] Typing: *EXEC !BOOT\n";
            keyboard.pasteText("\r*EXEC !BOOT\r");
        } else if (bootByte0 == 0x0D || (bootByte0 >= '0' && bootByte0 <= '9')) {
            std::cout << "[BOOT] Typing: CHAIN \"!BOOT\"\n";
            keyboard.pasteText("\rCHAIN \"!BOOT\"\r");
        } else {
            switch (bootOpt) {
                case 2:  keyboard.pasteText("\r*RUN !BOOT\r");   break;
                case 1:  keyboard.pasteText("\r*LOAD !BOOT\r");  break;
                default: keyboard.pasteText("\r*RUN !BOOT\r");   break;
            }
        }
    } else if (!files.empty()) {
        // No !BOOT — inspect first catalog file
        const auto& first = files[0];
        uint32 off = first.startSector * 256;
        bool firstIsBasic = false;
        if (off + 1 < fdc.diskData[0].size()) {
            uint8 b0 = fdc.diskData[0][off];
            firstIsBasic = (b0 == 0x0D || b0 == 0x00 ||
                            (b0 >= '0' && b0 <= '9'));
        }
        if (firstIsBasic) {
            keyboard.pasteText("CHAIN \"" + first.name + "\"\r");
        } else {
            keyboard.pasteText("*RUN " + first.name + "\r");
        }
    }
}

void BBCMachine::runFrame() {
    // Release Shift key held during Shift-Break boot
    if (shiftBootFrames > 0) {
        shiftBootFrames--;
        if (shiftBootFrames == 0) {
            keyboard.shiftPressed = false;
            keyboard.setKey(0, 0, false);
        }
    }

    // Update clipboard typing / paste buffer
    keyboard.updatePaste();

    // If any key is pressed, assert CA2 interrupt flag (KBINT on BBC Micro)
    if (keyboard.hasAnyKeyDown()) {
        sysVia.ifr |= 0x01;
    }

    // Check Break key
    if (keyboard.breakPressed) {
        cpu.reset();
    }

    if (debugger.paused && !debugger.stepInstruction && !debugger.stepFrame) {
        // Paused in debugger
        return;
    }

    int cyclesRemaining = CYCLES_PER_FRAME; // 40,000 cycles for 50 Hz frame

    while (cyclesRemaining > 0) {
        if (debugger.paused && !debugger.stepInstruction && !debugger.stepFrame) {
            break;
        }

        // Check PC breakpoint
        if (debugger.checkBreakpoint(cpu.PC)) {
            break;
        }

        if (cpu.PC == 0xFFEE) {
            char c = (char)cpu.A;
            if (c >= 32 && c <= 126) std::cout << c << std::flush;
            else if (c == 13 || c == 10) std::cout << "\n[MOS] " << std::flush;
        }

        int opCycles = cpu.step();
        cyclesRemaining -= opCycles;

        sysVia.step(opCycles);
        userVia.step(opCycles);
        fdc.step(opCycles);

        if (debugger.stepInstruction) {
            debugger.stepInstruction = false;
            debugger.paused = true;
            break;
        }
    }

    if (debugger.stepFrame) {
        debugger.stepFrame = false;
        debugger.paused = true;
    }

    // End of Frame: Field Sync (VSync) at 50 Hz on System VIA CA1
    sysVia.setCA1(true);
    sysVia.setCA1(false);

    // Render video frame
    crtc.renderFrame(rawFramebuffer.data(), CRTC6845::SCREEN_WIDTH * 4);

    // Generate audio samples
    sound.generateSamples(audioBuffer.data(), audioBuffer.size());
}

const uint32* BBCMachine::getRenderedScreen() const {
    return rawFramebuffer.data();
}
