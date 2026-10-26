#pragma once
#include "types.h"
#include <unordered_map>
#include <functional>
#include <string>

// Micro-operation representation for dynamic basic block compilation
enum class JITOpType {
    Nop,
    LoadConst,
    Add,
    Sub,
    And,
    Or,
    Xor,
    Move,
    Jump,
    BranchIfZero,
    BranchIfNotZero,
    Call,
    Return,
    ReadMem8,
    WriteMem8,
    CustomNative
};

struct JITMicroOp {
    JITOpType type = JITOpType::Nop;
    uint32 regDst = 0;
    uint32 regSrc1 = 0;
    uint32 regSrc2 = 0;
    uint32 immediate = 0;
    uint32 cycles = 1;
    std::function<void()> nativeFunc;
};

// Compiled Basic Block
struct JITBlock {
    uint32 startPC = 0;
    uint32 endPC = 0;
    uint32 totalCycles = 0;
    uint32 executionCount = 0;
    std::vector<JITMicroOp> ops;
    std::function<int()> fastNativeExec = nullptr;
};

class JITEngine {
public:
    static JITEngine& instance() {
        static JITEngine inst;
        return inst;
    }

    bool enabled = true;
    uint64 compiledBlocksCount = 0;
    uint64 blockExecutions = 0;
    uint64 totalCyclesEmulated = 0;

    void reset() {
        blockCache.clear();
        compiledBlocksCount = 0;
        blockExecutions = 0;
        totalCyclesEmulated = 0;
    }

    // Lookup or compile block
    JITBlock* getBlock(uint32 pc) {
        if (!enabled) return nullptr;
        auto it = blockCache.find(pc);
        if (it != blockCache.end()) {
            return &it->second;
        }
        return nullptr;
    }

    void registerBlock(uint32 pc, JITBlock block) {
        compiledBlocksCount++;
        blockCache[pc] = std::move(block);
    }

    void invalidate(uint32 pc) {
        blockCache.erase(pc);
    }

    void clear() {
        blockCache.clear();
    }

    std::string getStatusString() const {
        return "JIT: ACTIVE (x86_64 Block-Trans, " + std::to_string(blockCache.size()) + " blocks)";
    }

private:
    JITEngine() = default;
    std::unordered_map<uint32, JITBlock> blockCache;
};
