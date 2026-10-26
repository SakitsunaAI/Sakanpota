#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

struct PokeGameEntry {
    const char* id;
    const char* title;
    const uint8_t* compData;
    size_t compSize;
    size_t origSize;
};

namespace EmbeddedPokeMini {
    extern const PokeGameEntry GAMES[];
    extern const size_t NUM_GAMES;
    extern const uint8_t BIOS_COMP[];
    extern const size_t BIOS_COMP_SIZE;
    extern const size_t BIOS_ORIG_SIZE;

    bool decompressGame(int index, std::vector<uint8_t>& out);
    bool decompressGameById(const std::string& id, std::vector<uint8_t>& out);
    bool decompressBios(std::vector<uint8_t>& out);
}
