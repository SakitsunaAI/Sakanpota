import zlib
import os

files = [
    ("party", "roms/pokemon_mini/Pokemon Party Mini (USA).min", "Pokemon Party Mini"),
    ("pinball", "roms/pokemon_mini/Pokemon Pinball Mini (USA, Europe).min", "Pokemon Pinball Mini"),
    ("puzzle", "roms/pokemon_mini/Pokemon Puzzle Collection (USA, Europe).min", "Pokemon Puzzle Collection"),
    ("puzzle2", "roms/pokemon_mini/Pokemon Puzzle Collection Vol. 2 (Japan).min", "Pokemon Puzzle Collection Vol. 2"),
    ("zany", "roms/pokemon_mini/Pokemon Zany Cards (USA, Europe).min", "Pokemon Zany Cards"),
    ("tetris", "roms/pokemon_mini/Pokemon Tetris (Europe) (En,Ja,Fr).min", "Pokemon Tetris"),
    ("race", "roms/pokemon_mini/Pokemon Race Mini (Japan).min", "Pokemon Race Mini"),
    ("pichu", "roms/pokemon_mini/Pichu Bros. Mini (Japan).min", "Pichu Bros. Mini"),
    ("togepi", "roms/pokemon_mini/Togepi no Daibouken (Japan).min", "Togepi no Daibouken"),
    ("snorlax", "roms/pokemon_mini/Snorlax's Lunch Time (Europe) (GameCube).min", "Snorlax's Lunch Time"),
    ("breeder", "roms/pokemon_mini/Pokemon Sodateyasan Mini (Japan).min", "Pokemon Breeder Mini"),
    ("bios", "roms/pokemon_mini/[BIOS] Nintendo Pokemon Mini (World).min", "Nintendo Pokemon Mini BIOS")
]

with open("src/embedded_pokemon_mini.h", "w") as h_out:
    h_out.write("""#pragma once
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
""")

with open("src/embedded_pokemon_mini.cpp", "w") as cpp_out:
    cpp_out.write("""#include "embedded_pokemon_mini.h"
#include <zlib.h>
#include <cstring>

namespace EmbeddedPokeMini {
""")
    for var, path, title in files:
        data = open(path, "rb").read()
        comp = zlib.compress(data, 9)
        cpp_out.write(f"// {title} ({len(data)} -> {len(comp)} bytes)\n")
        cpp_out.write(f"static const uint8_t DATA_{var}[{len(comp)}] = {{\n")
        for i in range(0, len(comp), 16):
            chunk = comp[i:i+16]
            cpp_out.write("    " + ", ".join(f"0x{b:02X}" for b in chunk) + ",\n")
        cpp_out.write("};\n\n")

    bios_comp = zlib.compress(open("roms/pokemon_mini/[BIOS] Nintendo Pokemon Mini (World).min", "rb").read(), 9)
    cpp_out.write("const uint8_t BIOS_COMP[] = {\n")
    for i in range(0, len(bios_comp), 16):
        chunk = bios_comp[i:i+16]
        cpp_out.write("    " + ", ".join(f"0x{b:02X}" for b in chunk) + ",\n")
    cpp_out.write(f"}};\n")
    cpp_out.write(f"const size_t BIOS_COMP_SIZE = {len(bios_comp)};\n")
    cpp_out.write("const size_t BIOS_ORIG_SIZE = 4096;\n\n")

    cpp_out.write("const PokeGameEntry GAMES[] = {\n")
    for var, path, title in files[:-1]:
        data = open(path, "rb").read()
        comp = zlib.compress(data, 9)
        cpp_out.write(f'    {{"{var}", "{title}", DATA_{var}, {len(comp)}, {len(data)}}},\n')
    cpp_out.write("};\n")
    cpp_out.write(f"const size_t NUM_GAMES = {len(files) - 1};\n\n")

    cpp_out.write("""bool decompressGame(int index, std::vector<uint8_t>& out) {
    if (index < 0 || (size_t)index >= NUM_GAMES) return false;
    const auto& g = GAMES[index];
    out.resize(g.origSize);
    uLongf destLen = (uLongf)g.origSize;
    int res = uncompress(out.data(), &destLen, g.compData, (uLong)g.compSize);
    return res == Z_OK;
}

bool decompressGameById(const std::string& id, std::vector<uint8_t>& out) {
    for (size_t i = 0; i < NUM_GAMES; i++) {
        if (id == GAMES[i].id) return decompressGame(i, out);
    }
    return false;
}

bool decompressBios(std::vector<uint8_t>& out) {
    out.resize(BIOS_ORIG_SIZE);
    uLongf destLen = (uLongf)BIOS_ORIG_SIZE;
    int res = uncompress(out.data(), &destLen, BIOS_COMP, (uLong)BIOS_COMP_SIZE);
    return res == Z_OK;
}

} // namespace EmbeddedPokeMini
""")

print("Successfully generated src/embedded_pokemon_mini.h and src/embedded_pokemon_mini.cpp")
