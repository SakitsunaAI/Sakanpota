#pragma once

#include "types.h"
#include <string>
#include <vector>
#include <set>
#include <functional>

struct DisasmLine {
    uint16 address = 0;
    std::string bytesHex;
    std::string mnemonic;
    std::string operand;
    int length = 1;
};

class Debugger {
public:
    bool enabled = false;
    bool paused = false;
    bool stepInstruction = false;
    bool stepFrame = false;

    uint16 viewAddress = 0x0000;
    std::set<uint16> breakpoints;

    std::function<uint8(uint16)> readMem;

    Debugger();

    void reset() {
        paused = false;
        stepInstruction = false;
        stepFrame = false;
        viewAddress = 0x0000;
    }

    void toggle();
    void pause();
    void resume();
    void step();
    void stepOverFrame();

    void toggleBreakpoint(uint16 addr);
    bool checkBreakpoint(uint16 pc);

    // Disassembly
    DisasmLine disassemble(uint16 addr, bool is65C02 = false);
    std::vector<DisasmLine> disassembleBlock(uint16 startAddr, int count, bool is65C02 = false);

    // Memory Dump
    std::string getMemoryDumpLine(uint16 addr);

    // Format registers
    std::string formatRegisters(uint16 pc, uint8 a, uint8 x, uint8 y, uint8 sp, uint8 p, uint64 cycles);
};
