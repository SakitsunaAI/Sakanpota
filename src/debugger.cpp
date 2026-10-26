#include "debugger.h"
#include <sstream>
#include <iomanip>

Debugger::Debugger() {}

void Debugger::toggle() {
    enabled = !enabled;
    if (enabled) paused = true;
}

void Debugger::pause() {
    paused = true;
}

void Debugger::resume() {
    paused = false;
    stepInstruction = false;
    stepFrame = false;
}

void Debugger::step() {
    paused = true;
    stepInstruction = true;
}

void Debugger::stepOverFrame() {
    paused = false;
    stepFrame = true;
}

void Debugger::toggleBreakpoint(uint16 addr) {
    if (breakpoints.count(addr)) {
        breakpoints.erase(addr);
    } else {
        breakpoints.insert(addr);
    }
}

bool Debugger::checkBreakpoint(uint16 pc) {
    if (breakpoints.count(pc)) {
        paused = true;
        enabled = true;
        return true;
    }
    return false;
}

DisasmLine Debugger::disassemble(uint16 addr, bool is65C02) {
    DisasmLine line;
    line.address = addr;

    auto read = [&](uint16 a) -> uint8 {
        return readMem ? readMem(a) : 0xFF;
    };

    uint8 op = read(addr);
    uint8 b1 = read(addr + 1);
    uint8 b2 = read(addr + 2);

    char hexBuf[32];
    std::string mnem = "???";
    std::string oper = "";
    int len = 1;

    auto fmtHex16 = [](uint16 v) {
        std::stringstream ss;
        ss << "$" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << v;
        return ss.str();
    };

    auto fmtHex8 = [](uint8 v) {
        std::stringstream ss;
        ss << "$" << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << (int)v;
        return ss.str();
    };

    // Decode opcode table
    switch (op) {
        // ADC
        case 0x69: mnem = "ADC"; oper = "#" + fmtHex8(b1); len = 2; break;
        case 0x65: mnem = "ADC"; oper = fmtHex8(b1); len = 2; break;
        case 0x75: mnem = "ADC"; oper = fmtHex8(b1) + ",X"; len = 2; break;
        case 0x6D: mnem = "ADC"; oper = fmtHex16(b1 | (b2 << 8)); len = 3; break;
        case 0x7D: mnem = "ADC"; oper = fmtHex16(b1 | (b2 << 8)) + ",X"; len = 3; break;
        case 0x79: mnem = "ADC"; oper = fmtHex16(b1 | (b2 << 8)) + ",Y"; len = 3; break;
        case 0x61: mnem = "ADC"; oper = "(" + fmtHex8(b1) + ",X)"; len = 2; break;
        case 0x71: mnem = "ADC"; oper = "(" + fmtHex8(b1) + "),Y"; len = 2; break;
        case 0x72: mnem = "ADC"; oper = "(" + fmtHex8(b1) + ")"; len = 2; break;

        // AND
        case 0x29: mnem = "AND"; oper = "#" + fmtHex8(b1); len = 2; break;
        case 0x25: mnem = "AND"; oper = fmtHex8(b1); len = 2; break;
        case 0x35: mnem = "AND"; oper = fmtHex8(b1) + ",X"; len = 2; break;
        case 0x2D: mnem = "AND"; oper = fmtHex16(b1 | (b2 << 8)); len = 3; break;
        case 0x3D: mnem = "AND"; oper = fmtHex16(b1 | (b2 << 8)) + ",X"; len = 3; break;
        case 0x39: mnem = "AND"; oper = fmtHex16(b1 | (b2 << 8)) + ",Y"; len = 3; break;
        case 0x21: mnem = "AND"; oper = "(" + fmtHex8(b1) + ",X)"; len = 2; break;
        case 0x31: mnem = "AND"; oper = "(" + fmtHex8(b1) + "),Y"; len = 2; break;

        // ASL
        case 0x0A: mnem = "ASL"; oper = "A"; len = 1; break;
        case 0x06: mnem = "ASL"; oper = fmtHex8(b1); len = 2; break;
        case 0x16: mnem = "ASL"; oper = fmtHex8(b1) + ",X"; len = 2; break;
        case 0x0E: mnem = "ASL"; oper = fmtHex16(b1 | (b2 << 8)); len = 3; break;
        case 0x1E: mnem = "ASL"; oper = fmtHex16(b1 | (b2 << 8)) + ",X"; len = 3; break;

        // Branches
        case 0x90: mnem = "BCC"; oper = fmtHex16(addr + 2 + (int8)b1); len = 2; break;
        case 0xB0: mnem = "BCS"; oper = fmtHex16(addr + 2 + (int8)b1); len = 2; break;
        case 0xF0: mnem = "BEQ"; oper = fmtHex16(addr + 2 + (int8)b1); len = 2; break;
        case 0xD0: mnem = "BNE"; oper = fmtHex16(addr + 2 + (int8)b1); len = 2; break;
        case 0x30: mnem = "BMI"; oper = fmtHex16(addr + 2 + (int8)b1); len = 2; break;
        case 0x10: mnem = "BPL"; oper = fmtHex16(addr + 2 + (int8)b1); len = 2; break;
        case 0x50: mnem = "BVC"; oper = fmtHex16(addr + 2 + (int8)b1); len = 2; break;
        case 0x70: mnem = "BVS"; oper = fmtHex16(addr + 2 + (int8)b1); len = 2; break;
        case 0x80: mnem = "BRA"; oper = fmtHex16(addr + 2 + (int8)b1); len = 2; break;

        // BIT
        case 0x24: mnem = "BIT"; oper = fmtHex8(b1); len = 2; break;
        case 0x2C: mnem = "BIT"; oper = fmtHex16(b1 | (b2 << 8)); len = 3; break;
        case 0x89: mnem = "BIT"; oper = "#" + fmtHex8(b1); len = 2; break;

        // CMP
        case 0xC9: mnem = "CMP"; oper = "#" + fmtHex8(b1); len = 2; break;
        case 0xC5: mnem = "CMP"; oper = fmtHex8(b1); len = 2; break;
        case 0xD5: mnem = "CMP"; oper = fmtHex8(b1) + ",X"; len = 2; break;
        case 0xCD: mnem = "CMP"; oper = fmtHex16(b1 | (b2 << 8)); len = 3; break;
        case 0xDD: mnem = "CMP"; oper = fmtHex16(b1 | (b2 << 8)) + ",X"; len = 3; break;
        case 0xD9: mnem = "CMP"; oper = fmtHex16(b1 | (b2 << 8)) + ",Y"; len = 3; break;
        case 0xC1: mnem = "CMP"; oper = "(" + fmtHex8(b1) + ",X)"; len = 2; break;
        case 0xD1: mnem = "CMP"; oper = "(" + fmtHex8(b1) + "),Y"; len = 2; break;

        // CPX, CPY
        case 0xE0: mnem = "CPX"; oper = "#" + fmtHex8(b1); len = 2; break;
        case 0xE4: mnem = "CPX"; oper = fmtHex8(b1); len = 2; break;
        case 0xEC: mnem = "CPX"; oper = fmtHex16(b1 | (b2 << 8)); len = 3; break;
        case 0xC0: mnem = "CPY"; oper = "#" + fmtHex8(b1); len = 2; break;
        case 0xC4: mnem = "CPY"; oper = fmtHex8(b1); len = 2; break;
        case 0xCC: mnem = "CPY"; oper = fmtHex16(b1 | (b2 << 8)); len = 3; break;

        // DEC, DEX, DEY
        case 0xC6: mnem = "DEC"; oper = fmtHex8(b1); len = 2; break;
        case 0xD6: mnem = "DEC"; oper = fmtHex8(b1) + ",X"; len = 2; break;
        case 0xCE: mnem = "DEC"; oper = fmtHex16(b1 | (b2 << 8)); len = 3; break;
        case 0xDE: mnem = "DEC"; oper = fmtHex16(b1 | (b2 << 8)) + ",X"; len = 3; break;
        case 0x3A: mnem = "DEC"; oper = "A"; len = 1; break;
        case 0xCA: mnem = "DEX"; len = 1; break;
        case 0x88: mnem = "DEY"; len = 1; break;

        // INC, INX, INY
        case 0xE6: mnem = "INC"; oper = fmtHex8(b1); len = 2; break;
        case 0xF6: mnem = "INC"; oper = fmtHex8(b1) + ",X"; len = 2; break;
        case 0xEE: mnem = "INC"; oper = fmtHex16(b1 | (b2 << 8)); len = 3; break;
        case 0xFE: mnem = "INC"; oper = fmtHex16(b1 | (b2 << 8)) + ",X"; len = 3; break;
        case 0x1A: mnem = "INC"; oper = "A"; len = 1; break;
        case 0xE8: mnem = "INX"; len = 1; break;
        case 0xC8: mnem = "INY"; len = 1; break;

        // JMP, JSR
        case 0x4C: mnem = "JMP"; oper = fmtHex16(b1 | (b2 << 8)); len = 3; break;
        case 0x6C: mnem = "JMP"; oper = "(" + fmtHex16(b1 | (b2 << 8)) + ")"; len = 3; break;
        case 0x7C: mnem = "JMP"; oper = "(" + fmtHex16(b1 | (b2 << 8)) + ",X)"; len = 3; break;
        case 0x20: mnem = "JSR"; oper = fmtHex16(b1 | (b2 << 8)); len = 3; break;

        // LDA
        case 0xA9: mnem = "LDA"; oper = "#" + fmtHex8(b1); len = 2; break;
        case 0xA5: mnem = "LDA"; oper = fmtHex8(b1); len = 2; break;
        case 0xB5: mnem = "LDA"; oper = fmtHex8(b1) + ",X"; len = 2; break;
        case 0xAD: mnem = "LDA"; oper = fmtHex16(b1 | (b2 << 8)); len = 3; break;
        case 0xBD: mnem = "LDA"; oper = fmtHex16(b1 | (b2 << 8)) + ",X"; len = 3; break;
        case 0xB9: mnem = "LDA"; oper = fmtHex16(b1 | (b2 << 8)) + ",Y"; len = 3; break;
        case 0xA1: mnem = "LDA"; oper = "(" + fmtHex8(b1) + ",X)"; len = 2; break;
        case 0xB1: mnem = "LDA"; oper = "(" + fmtHex8(b1) + "),Y"; len = 2; break;
        case 0xB2: mnem = "LDA"; oper = "(" + fmtHex8(b1) + ")"; len = 2; break;

        // LDX
        case 0xA2: mnem = "LDX"; oper = "#" + fmtHex8(b1); len = 2; break;
        case 0xA6: mnem = "LDX"; oper = fmtHex8(b1); len = 2; break;
        case 0xB6: mnem = "LDX"; oper = fmtHex8(b1) + ",Y"; len = 2; break;
        case 0xAE: mnem = "LDX"; oper = fmtHex16(b1 | (b2 << 8)); len = 3; break;
        case 0xBE: mnem = "LDX"; oper = fmtHex16(b1 | (b2 << 8)) + ",Y"; len = 3; break;

        // LDY
        case 0xA0: mnem = "LDY"; oper = "#" + fmtHex8(b1); len = 2; break;
        case 0xA4: mnem = "LDY"; oper = fmtHex8(b1); len = 2; break;
        case 0xB4: mnem = "LDY"; oper = fmtHex8(b1) + ",X"; len = 2; break;
        case 0xAC: mnem = "LDY"; oper = fmtHex16(b1 | (b2 << 8)); len = 3; break;
        case 0xBC: mnem = "LDY"; oper = fmtHex16(b1 | (b2 << 8)) + ",X"; len = 3; break;

        // STA
        case 0x85: mnem = "STA"; oper = fmtHex8(b1); len = 2; break;
        case 0x95: mnem = "STA"; oper = fmtHex8(b1) + ",X"; len = 2; break;
        case 0x8D: mnem = "STA"; oper = fmtHex16(b1 | (b2 << 8)); len = 3; break;
        case 0x9D: mnem = "STA"; oper = fmtHex16(b1 | (b2 << 8)) + ",X"; len = 3; break;
        case 0x99: mnem = "STA"; oper = fmtHex16(b1 | (b2 << 8)) + ",Y"; len = 3; break;
        case 0x81: mnem = "STA"; oper = "(" + fmtHex8(b1) + ",X)"; len = 2; break;
        case 0x91: mnem = "STA"; oper = "(" + fmtHex8(b1) + "),Y"; len = 2; break;
        case 0x92: mnem = "STA"; oper = "(" + fmtHex8(b1) + ")"; len = 2; break;

        // STX, STY, STZ
        case 0x86: mnem = "STX"; oper = fmtHex8(b1); len = 2; break;
        case 0x96: mnem = "STX"; oper = fmtHex8(b1) + ",Y"; len = 2; break;
        case 0x8E: mnem = "STX"; oper = fmtHex16(b1 | (b2 << 8)); len = 3; break;
        case 0x84: mnem = "STY"; oper = fmtHex8(b1); len = 2; break;
        case 0x94: mnem = "STY"; oper = fmtHex8(b1) + ",X"; len = 2; break;
        case 0x8C: mnem = "STY"; oper = fmtHex16(b1 | (b2 << 8)); len = 3; break;
        case 0x64: mnem = "STZ"; oper = fmtHex8(b1); len = 2; break;
        case 0x74: mnem = "STZ"; oper = fmtHex8(b1) + ",X"; len = 2; break;
        case 0x9C: mnem = "STZ"; oper = fmtHex16(b1 | (b2 << 8)); len = 3; break;
        case 0x9E: mnem = "STZ"; oper = fmtHex16(b1 | (b2 << 8)) + ",X"; len = 3; break;

        // Transfers
        case 0xAA: mnem = "TAX"; len = 1; break;
        case 0xA8: mnem = "TAY"; len = 1; break;
        case 0xBA: mnem = "TSX"; len = 1; break;
        case 0x8A: mnem = "TXA"; len = 1; break;
        case 0x9A: mnem = "TXS"; len = 1; break;
        case 0x98: mnem = "TYA"; len = 1; break;

        // Stack
        case 0x48: mnem = "PHA"; len = 1; break;
        case 0x08: mnem = "PHP"; len = 1; break;
        case 0x68: mnem = "PLA"; len = 1; break;
        case 0x28: mnem = "PLP"; len = 1; break;
        case 0xDA: mnem = "PHX"; len = 1; break;
        case 0x5A: mnem = "PHY"; len = 1; break;
        case 0xFA: mnem = "PLX"; len = 1; break;
        case 0x7A: mnem = "PLY"; len = 1; break;

        // Flags & Misc
        case 0x18: mnem = "CLC"; len = 1; break;
        case 0x38: mnem = "SEC"; len = 1; break;
        case 0x58: mnem = "CLI"; len = 1; break;
        case 0x78: mnem = "SEI"; len = 1; break;
        case 0xB8: mnem = "CLV"; len = 1; break;
        case 0xD8: mnem = "CLD"; len = 1; break;
        case 0xF8: mnem = "SED"; len = 1; break;
        case 0x00: mnem = "BRK"; len = 1; break;
        case 0xEA: mnem = "NOP"; len = 1; break;
        case 0x40: mnem = "RTI"; len = 1; break;
        case 0x60: mnem = "RTS"; len = 1; break;

        default:
            mnem = "???";
            oper = fmtHex8(op);
            len = 1;
            break;
    }

    if (len == 1) {
        snprintf(hexBuf, sizeof(hexBuf), "%02X", op);
    } else if (len == 2) {
        snprintf(hexBuf, sizeof(hexBuf), "%02X %02X", op, b1);
    } else {
        snprintf(hexBuf, sizeof(hexBuf), "%02X %02X %02X", op, b1, b2);
    }

    line.bytesHex = hexBuf;
    line.mnemonic = mnem;
    line.operand = oper;
    line.length = len;
    return line;
}

std::vector<DisasmLine> Debugger::disassembleBlock(uint16 startAddr, int count, bool is65C02) {
    std::vector<DisasmLine> lines;
    uint16 curr = startAddr;
    for (int i = 0; i < count; i++) {
        DisasmLine line = disassemble(curr, is65C02);
        lines.push_back(line);
        curr += line.length;
    }
    return lines;
}

std::string Debugger::getMemoryDumpLine(uint16 addr) {
    std::stringstream ss;
    ss << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << addr << ": ";

    std::string ascii = "";
    for (int i = 0; i < 16; i++) {
        uint8 val = readMem ? readMem(addr + i) : 0xFF;
        ss << std::setw(2) << std::setfill('0') << (int)val << " ";
        if (val >= 32 && val < 127) {
            ascii += (char)val;
        } else {
            ascii += '.';
        }
    }
    ss << " |" << ascii << "|";
    return ss.str();
}

std::string Debugger::formatRegisters(uint16 pc, uint8 a, uint8 x, uint8 y, uint8 sp, uint8 p, uint64 cycles) {
    char buf[128];
    snprintf(buf, sizeof(buf),
        "PC:%04X  A:%02X  X:%02X  Y:%02X  SP:%02X  [%c%c-%c%c%c%c%c]  CYC:%llu",
        pc, a, x, y, sp,
        (p & 0x80) ? 'N' : '.',
        (p & 0x40) ? 'V' : '.',
        (p & 0x10) ? 'B' : '.',
        (p & 0x08) ? 'D' : '.',
        (p & 0x04) ? 'I' : '.',
        (p & 0x02) ? 'Z' : '.',
        (p & 0x01) ? 'C' : '.',
        (unsigned long long)cycles
    );
    return std::string(buf);
}
