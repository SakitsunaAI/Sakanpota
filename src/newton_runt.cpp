#include "newton_runt.h"
#include "embedded_newton_rom.h"
#include <fstream>
#include <iostream>
#include <cstring>
#include <algorithm>

NewtonRunt::NewtonRunt() {
    rom.resize(4 * 1024 * 1024, 0); // 4MB ROM
    ram1.resize(1024 * 1024, 0);    // 1MB RAM Bank 1
    ram2.resize(512 * 1024, 0);     // 512KB SRAM Bank 2
    lcdMemory.resize(LCD_WIDTH * LCD_HEIGHT, 0xFF); // 336x240 LCD (White)
    portraitFB.resize(PORTRAIT_WIDTH * TOTAL_HEIGHT, 0xFF); // 240x384 Framebuffer

    reset();
}

void NewtonRunt::reset() {
    std::fill(ram1.begin(), ram1.end(), 0);
    std::fill(ram2.begin(), ram2.end(), 0);
    std::fill(lcdMemory.begin(), lcdMemory.end(), 0xFF);
    std::fill(portraitFB.begin(), portraitFB.end(), 0xFF);

    machineType = 0x10001000; // Junior / MessagePad 100
    runtControl = 0x0000073D;
    intStatus = 0;
    intEnable = 0;
    ticks = 0;
    tickAlarm1 = 0;
    tickAlarm2 = 0;
    tickAlarm3 = 0;
    rtcCounter = 0x20000000; // Epoch timestamp
    rtcAlarm = 0;
    adcSource = 0;
    adcValue = 512;

    penDown = false;
    touchX = touchY = 0;
    adcTouchX = adcTouchY = 512;
    powerOn = true;
    displayDirty = true;

    writeX = writeY = 0;
    readX = readY = 0;
    windowLeft = windowTop = 0;
    windowRight = LCD_WIDTH - 1;
    windowBottom = LCD_HEIGHT - 1;
    idw = 0;
    idr = 0;
    contrast = 0x80;
    fillMode = 0;
    bitMask = 0xFF;

    renderSilkscreen();
}

bool NewtonRunt::loadDefaultROM() {
    EmbeddedNewtonROM::ensureDecrypted();
    if (loadROMData(EmbeddedNewtonROM::NEWTON_ROM, 4194304)) {
        romName = "Apple Newton MessagePad (OS 1.3 - Junior)";
        return true;
    }
    return false;
}

bool NewtonRunt::loadROM(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[Newton Runt] Could not open ROM file: " << path << std::endl;
        return false;
    }

    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);

    if (size > rom.size()) {
        rom.resize(size);
    }

    file.read(reinterpret_cast<char*>(rom.data()), size);
    romName = path;

    // Detect version / model details
    if (size >= 4) {
        uint32 firstInst = (rom[3] << 24) | (rom[2] << 16) | (rom[1] << 8) | rom[0];
        if (firstInst == 0xEA08878D) {
            romName = "Apple Newton MessagePad (OS 1.3 - Junior)";
        } else if (firstInst == 0xEA085503) {
            romName = "Apple Newton MessagePad (OS 1.0 - OMP)";
        }
    }

    reset();
    std::cout << "[Newton Runt] Loaded " << size << " bytes of ROM: " << romName << std::endl;
    return true;
}

bool NewtonRunt::loadROMData(const uint8* data, size_t size) {
    if (!data || size == 0) return false;
    if (size > rom.size()) rom.resize(size);
    std::memcpy(rom.data(), data, size);
    romName = "Embedded Apple Newton ROM";
    reset();
    return true;
}

void NewtonRunt::renderSilkscreen() {
    // Draw the permanent printed silkscreen buttons in the bottom 48 pixels of portraitFB
    // Icons: [Overview] [Up] [Down] [Undo] [Find] [Assist]
    const int startY = PORTRAIT_HEIGHT;

    for (int y = startY; y < TOTAL_HEIGHT; y++) {
        for (int x = 0; x < PORTRAIT_WIDTH; x++) {
            portraitFB[y * PORTRAIT_WIDTH + x] = 0xEE; // Soft off-white silkscreen glass
        }
    }

    // Dividers and button outlines
    for (int x = 0; x < PORTRAIT_WIDTH; x++) {
        portraitFB[startY * PORTRAIT_WIDTH + x] = 0x66; // Top border line
        portraitFB[(TOTAL_HEIGHT - 1) * PORTRAIT_WIDTH + x] = 0x88;
    }

    auto drawIconBox = [&](int bx, int by, int bw, int bh, const char* label) {
        for (int x = bx; x < bx + bw; x++) {
            portraitFB[by * PORTRAIT_WIDTH + x] = 0x88;
            portraitFB[(by + bh) * PORTRAIT_WIDTH + x] = 0x88;
        }
        for (int y = by; y <= by + bh; y++) {
            portraitFB[y * PORTRAIT_WIDTH + bx] = 0x88;
            portraitFB[y * PORTRAIT_WIDTH + (bx + bw)] = 0x88;
        }
    };

    // 6 Iconic Buttons
    drawIconBox(10,  startY + 8, 32, 32, "UNDO");
    drawIconBox(48,  startY + 8, 32, 32, "FIND");
    drawIconBox(86,  startY + 8, 32, 32, "ASSIST");
    drawIconBox(124, startY + 8, 32, 32, "VIEW");
    drawIconBox(162, startY + 8, 32, 32, "UP");
    drawIconBox(200, startY + 8, 32, 32, "DOWN");
}

void NewtonRunt::touchDown(int portraitX, int portraitY) {
    penDown = true;
    touchX = std::clamp(portraitX, 0, PORTRAIT_WIDTH - 1);
    touchY = std::clamp(portraitY, 0, TOTAL_HEIGHT - 1);

    // ADC expects 10-bit range (0..1023)
    adcTouchX = (touchX * 1023) / PORTRAIT_WIDTH;
    adcTouchY = (touchY * 1023) / TOTAL_HEIGHT;

    intStatus |= IRQ_TABLET;
    updateInterrupts();
}

void NewtonRunt::touchUp() {
    penDown = false;
    intStatus &= ~IRQ_TABLET;
}

void NewtonRunt::togglePower() {
    powerOn = !powerOn;
    if (powerOn) {
        runtControl |= 1;
        intStatus |= IRQ_POWER;
        updateInterrupts();
    }
}

void NewtonRunt::updateInterrupts() {
    if (intStatus & intEnable) {
        if (onIRQ) onIRQ();
    }
}

void NewtonRunt::tick(uint32 cycles) {
    ticks += cycles;

    // Timer alarms (approx 1024 Hz ticks)
    if (tickAlarm1 && ticks >= tickAlarm1) {
        intStatus |= IRQ_TIMER1;
        tickAlarm1 = 0;
        updateInterrupts();
    }
    if (tickAlarm2 && ticks >= tickAlarm2) {
        intStatus |= IRQ_TIMER2;
        tickAlarm2 = 0;
        updateInterrupts();
    }
    if (tickAlarm3 && ticks >= tickAlarm3) {
        intStatus |= IRQ_TIMER3;
        tickAlarm3 = 0;
        updateInterrupts();
    }

    // 1 Hz RTC
    if (ticks % 20000000 < cycles) {
        rtcCounter++;
        if (rtcAlarm && rtcCounter >= rtcAlarm) {
            intStatus |= IRQ_RTC;
            rtcAlarm = 0;
            updateInterrupts();
        }
    }
}

// Memory Bus Access
uint8 NewtonRunt::read8(uint32 addr) {
    // 1. ROM: 0x00000000 - 0x00FFFFFF (mirrored 4MB)
    if (addr < 0x01000000) {
        uint32 romAddr = addr % (rom.size() > 0 ? rom.size() : 1);
        return rom[romAddr];
    }

    // 2. RAM Bank 1: 0x01000000 - 0x011FFFFF
    if (addr >= 0x01000000 && addr < 0x01200000) {
        uint32 offset = (addr - 0x01000000) % ram1.size();
        return ram1[offset];
    }

    // 3. RAM Bank 2 / Flash: 0x01200000 - 0x013FFFFF
    if (addr >= 0x01200000 && addr < 0x01400000) {
        uint32 offset = (addr - 0x01200000) % ram2.size();
        return ram2[offset];
    }

    // 4. RUNT ASIC & Display: 0x01400000 - 0x017FFFFF
    if (addr >= 0x01400000 && addr < 0x01800000) {
        uint32 reg = addr & 0xFF;
        switch (reg) {
            case 0x00: return (runtControl >> 0) & 0xFF;
            case 0x01: return (runtControl >> 8) & 0xFF;
            case 0x02: return (runtControl >> 16) & 0xFF;
            case 0x03: return (runtControl >> 24) & 0xFF;
            case 0x04: return (intStatus >> 0) & 0xFF;
            case 0x05: return (intStatus >> 8) & 0xFF;
            case 0x08: return (intEnable >> 0) & 0xFF;
            case 0x09: return (intEnable >> 8) & 0xFF;
            case 0x10: return (ticks >> 0) & 0xFF;
            case 0x11: return (ticks >> 8) & 0xFF;
            case 0x12: return (ticks >> 16) & 0xFF;
            case 0x13: return (ticks >> 24) & 0xFF;
            case 0x24: return (rtcCounter >> 0) & 0xFF;
            case 0x25: return (rtcCounter >> 8) & 0xFF;
            case 0x30: { // ADC Data
                if (adcSource == 4) return (adcTouchX >> 2) & 0xFF; // Tablet X
                if (adcSource == 5) return (adcTouchY >> 2) & 0xFF; // Tablet Y
                return 0x80; // Nominal battery voltage
            }
            case 0x31: {
                if (adcSource == 4) return (adcTouchX & 3) << 6;
                if (adcSource == 5) return (adcTouchY & 3) << 6;
                return 0;
            }
            case 0x34: return penDown ? 0x01 : 0x00; // Tablet Pen contact
            case 0x50: return powerOn ? 0x01 : 0x00; // Power state
            default:   return 0;
        }
    }

    // 5. Sharp LCD Controller: 0x01800000 - 0x01BFFFFF
    if (addr >= 0x01800000 && addr < 0x01C00000) {
        uint32 reg = addr & 0x7F;
        switch (reg) {
            case 0x04: return 0x01; // Not busy
            case 0x2C: return contrast;
            case 0x3C: return 0x20; // Sharp LCD Controller revision
            default:   return 0;
        }
    }

    return 0;
}

uint16 NewtonRunt::read16(uint32 addr) {
    return (uint16)read8(addr) | ((uint16)read8(addr + 1) << 8);
}

uint32 NewtonRunt::read32(uint32 addr) {
    return (uint32)read8(addr) |
           ((uint32)read8(addr + 1) << 8) |
           ((uint32)read8(addr + 2) << 16) |
           ((uint32)read8(addr + 3) << 24);
}

void NewtonRunt::write8(uint32 addr, uint8 val) {
    // 1. RAM Bank 1: 0x01000000 - 0x011FFFFF
    if (addr >= 0x01000000 && addr < 0x01200000) {
        uint32 offset = (addr - 0x01000000) % ram1.size();
        ram1[offset] = val;
        return;
    }

    // 2. RAM Bank 2: 0x01200000 - 0x013FFFFF
    if (addr >= 0x01200000 && addr < 0x01400000) {
        uint32 offset = (addr - 0x01200000) % ram2.size();
        ram2[offset] = val;
        return;
    }

    // 3. RUNT ASIC: 0x01400000 - 0x017FFFFF
    if (addr >= 0x01400000 && addr < 0x01800000) {
        uint32 reg = addr & 0xFF;
        switch (reg) {
            case 0x00: runtControl = (runtControl & ~0xFF) | val; break;
            case 0x04: intStatus &= ~val; break; // Clear pending interrupts
            case 0x08: intEnable = (intEnable & ~0xFF) | val; updateInterrupts(); break;
            case 0x09: intEnable = (intEnable & ~0xFF00) | (val << 8); updateInterrupts(); break;
            case 0x14: tickAlarm1 = (tickAlarm1 & ~0xFF) | val; break;
            case 0x15: tickAlarm1 = (tickAlarm1 & ~0xFF00) | (val << 8); break;
            case 0x18: tickAlarm2 = (tickAlarm2 & ~0xFF) | val; break;
            case 0x19: tickAlarm2 = (tickAlarm2 & ~0xFF00) | (val << 8); break;
            case 0x20: tickAlarm3 = (tickAlarm3 & ~0xFF) | val; break;
            case 0x21: tickAlarm3 = (tickAlarm3 & ~0xFF00) | (val << 8); break;
            case 0x28: rtcAlarm = (rtcAlarm & ~0xFF) | val; break;
            case 0x29: rtcAlarm = (rtcAlarm & ~0xFF00) | (val << 8); break;
            case 0x32: adcSource = val & 7; break; // Select ADC channel
            default:   break;
        }
        return;
    }

    // 4. Sharp LCD Controller: 0x01800000 - 0x01BFFFFF
    if (addr >= 0x01800000 && addr < 0x01C00000) {
        uint32 reg = addr & 0x7F;
        switch (reg) {
            case 0x08: lcdWritePixelData(val); break;
            case 0x0C: bitMask = val; break;
            case 0x14: idw = val; break; // Direction
            case 0x20: fillMode = val; break;
            case 0x2C: contrast = val; break;
            case 0x38: // Flush / Refresh LCD
                updatePortraitDisplay();
                break;
            case 0x40: writeX = (writeX & ~0xFF) | val; break;
            case 0x44: writeX = (writeX & ~0xFF00) | (val << 8); break;
            case 0x48: writeY = (writeY & ~0xFF) | val; break;
            case 0x4C: writeY = (writeY & ~0xFF00) | (val << 8); break;
            case 0x60: windowLeft = (windowLeft & ~0xFF) | val; break;
            case 0x64: windowLeft = (windowLeft & ~0xFF00) | (val << 8); break;
            case 0x68: windowTop = (windowTop & ~0xFF) | val; break;
            case 0x6C: windowTop = (windowTop & ~0xFF00) | (val << 8); break;
            case 0x70: windowRight = (windowRight & ~0xFF) | val; break;
            case 0x74: windowRight = (windowRight & ~0xFF00) | (val << 8); break;
            case 0x78: windowBottom = (windowBottom & ~0xFF) | val; break;
            case 0x7C: windowBottom = (windowBottom & ~0xFF00) | (val << 8); break;
            default:   break;
        }
        return;
    }
}

void NewtonRunt::write16(uint32 addr, uint16 val) {
    write8(addr, (uint8)(val & 0xFF));
    write8(addr + 1, (uint8)((val >> 8) & 0xFF));
}

void NewtonRunt::write32(uint32 addr, uint32 val) {
    write8(addr, (uint8)(val & 0xFF));
    write8(addr + 1, (uint8)((val >> 8) & 0xFF));
    write8(addr + 2, (uint8)((val >> 16) & 0xFF));
    write8(addr + 3, (uint8)((val >> 24) & 0xFF));
}

void NewtonRunt::lcdWritePixelData(uint8 byteVal) {
    uint8 effectiveBits = byteVal & bitMask;

    for (int b = 7; b >= 0; b--) {
        bool bitOn = (effectiveBits & (1 << b)) != 0;
        uint8 color = 0xFF; // Default white
        if (fillMode == 0) {
            color = bitOn ? 0x00 : 0xFF; // 0=black, 1=white
        } else if (fillMode == 1) {
            color = bitOn ? 0xFF : 0x00; // Inverted
        } else {
            color = bitOn ? 0x00 : 0xFF;
        }

        lcdSetPixel(writeX, writeY, color);
        lcdStepWriteCursor();
    }

    displayDirty = true;
}

void NewtonRunt::lcdSetPixel(int x, int y, uint8 color) {
    if (x >= 0 && x < LCD_WIDTH && y >= 0 && y < LCD_HEIGHT) {
        lcdMemory[y * LCD_WIDTH + x] = color;
    }
}

void NewtonRunt::lcdStepWriteCursor() {
    // IDW direction stepping
    writeX++;
    if (writeX > windowRight) {
        writeX = windowLeft;
        writeY++;
        if (writeY > windowBottom) {
            writeY = windowTop;
        }
    }
}

void NewtonRunt::updatePortraitDisplay() {
    // Rotate 336x240 landscape LCD buffer into 240x336 portrait display
    for (int py = 0; py < PORTRAIT_HEIGHT; py++) {
        for (int px = 0; px < PORTRAIT_WIDTH; px++) {
            int srcX = py;
            int srcY = (LCD_HEIGHT - 1) - px;
            uint8 color = lcdMemory[srcY * LCD_WIDTH + srcX];
            portraitFB[py * PORTRAIT_WIDTH + px] = color;
        }
    }

    renderSilkscreen();
    displayDirty = true;
}
