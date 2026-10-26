#!/usr/bin/env python3
import os

rom_definitions = [
    ("BBC_OS120", "roms/os.rom", 16384, "BBC Micro OS 1.20 ROM (16KB)"),
    ("BBC_BASIC", "roms/BASIC.ROM", 16384, "BBC Micro BASIC II ROM (16KB)"),
    ("BBC_DFS09", "roms/DFS-0.9.rom", 8192, "BBC Micro DFS 0.9 Disc Filing System ROM (8KB)"),
    ("BBC_OS01", "roms/os01.rom", 16384, "BBC Micro OS 0.10 ROM (16KB)"),
    ("BBC_BPOS", "roms/bpos.rom", 16384, "BBC Micro B+ OS ROM (16KB)"),
    ("BBC_MOS320", "roms/mos3.20", 131072, "BBC Master 128 MegaROM MOS 3.20 (128KB)"),
    ("BBC_WELCOME_SSD", "discs/Welcome.ssd", 78336, "BBC Micro Welcome System Disc Image (78KB)"),
    ("MAC_128K_ROM", "roms/mac128k.rom", 65536, "Apple Macintosh 128K QuickDraw ROM (64KB, Checksum 28BA61CE)"),
    ("LISA_BOOT_ROM", "roms/lisa_boot.rom", 16384, "Apple Lisa 2/5 68000 Boot ROM (16KB, 341-0175-H + 341-0176-H)"),
    ("POCKETSTATION_BIOS", "roms/pocketstation.bin", 16384, "Sony PocketStation ARM Kernel BIOS (16KB, J110-Vita)"),
    ("C64_KERNAL", "roms/c64_kernal.bin", 8192, "Commodore 64 KERNAL ROM (8KB, 901227-03)"),
    ("C64_BASIC", "roms/c64_basic.bin", 8192, "Commodore 64 BASIC V2 ROM (8KB, 901226-01)"),
    ("C64_CHARGEN", "roms/c64_chargen.bin", 4096, "Commodore 64 Character Generator ROM (4KB, 901225-01)"),
    ("IBM_PC_BIOS", "roms/BIOS_IBM5150_19OCT81_5700671_U33.BIN", 8192, "IBM PC 5150 Motherboard BIOS (8KB, 19-OCT-1981)"),
    ("IBM_PC_BASIC", "roms/ibm-basic-1.00.rom", 32768, "IBM Personal Computer Cassette BASIC C1.00 (32KB, U29-U32)"),
    ("ATARI_2600_DEFAULT_ROM", "roms/atari_pitfall.bin", 4096, "Atari 2600 VCS Pitfall Cartridge ROM (4KB, Activision 1982)"),
    ("POCKETSTATION_CHOCOBO_GAME", "roms/pocket_chocobo.bin", 16384, "Sony PocketStation Chocobo World Game (16KB)"),
    ("POCKETSTATION_DOKODEMO_GAME", "roms/pocket_dokodemo.bin", 16384, "Sony PocketStation Dokodemo Issho Game (16KB)"),
    ("POCKETSTATION_CRASH_GAME", "roms/pocket_crash.bin", 16384, "Sony PocketStation Crash Bandicoot Game (16KB)"),
    ("POCKETSTATION_MEGAMAN_GAME", "roms/pocket_megaman.bin", 16384, "Sony PocketStation Pocket Rockman / Mega Man Game (16KB)"),
    ("POCKETSTATION_SPYRO_GAME", "roms/pocket_spyro.bin", 16384, "Sony PocketStation Spyro the Dragon Game (16KB)"),
    ("POCKETSTATION_SPACE_GAME", "roms/pocket_space.bin", 16384, "Sony PocketStation Space Invaders Game (16KB)"),
    ("POCKETSTATION_DUNGEON_GAME", "roms/pocket_dungeon.bin", 16384, "Sony PocketStation Pocket Dungeon RPG Game (16KB)")
]

header_lines = [
    "#pragma once",
    '#include "types.h"',
    "#include <string>",
    "",
    "namespace EmbeddedROMs {",
    "    // BBC Micro Family ROMs & Built-in System Disc",
    "    extern const uint8 BBC_OS120[16384];",
    "    extern const uint8 BBC_BASIC[16384];",
    "    extern const uint8 BBC_DFS09[8192];",
    "    extern const uint8 BBC_OS01[16384];",
    "    extern const uint8 BBC_BPOS[16384];",
    "    extern const uint8 BBC_MOS320[131072];",
    "    extern const uint8 BBC_WELCOME_SSD[78336];",
    "",
    "    // Apple Macintosh 128K QuickDraw ROM (Genuine 64KB)",
    "    extern const uint8 MAC_128K_ROM[65536];",
    "",
    "    // Apple Lisa 2/5 Boot ROM (Genuine 16KB)",
    "    extern const uint8 LISA_BOOT_ROM[16384];",
    "",
    "    // Sony PocketStation BIOS (Genuine 16KB ARM)",
    "    extern const uint8 POCKETSTATION_BIOS[16384];",
    "",
    "    // Sony PocketStation Embedded Mini-Game Library (16KB each)",
    "    extern const uint8 POCKETSTATION_CHOCOBO_GAME[16384];",
    "    extern const uint8 POCKETSTATION_DOKODEMO_GAME[16384];",
    "    extern const uint8 POCKETSTATION_CRASH_GAME[16384];",
    "    extern const uint8 POCKETSTATION_MEGAMAN_GAME[16384];",
    "    extern const uint8 POCKETSTATION_SPYRO_GAME[16384];",
    "    extern const uint8 POCKETSTATION_SPACE_GAME[16384];",
    "    extern const uint8 POCKETSTATION_DUNGEON_GAME[16384];",
    "",
    "    // Commodore 64 Genuine ROMs (KERNAL, BASIC V2, CHARGEN)",
    "    extern const uint8 C64_KERNAL[8192];",
    "    extern const uint8 C64_BASIC[8192];",
    "    extern const uint8 C64_CHARGEN[4096];",
    "",
    "    // IBM PC 5150 Genuine Motherboard BIOS (8KB) & Cassette BASIC C1.00 (32KB)",
    "    extern const uint8 IBM_PC_BIOS[8192];",
    "    extern const uint8 IBM_PC_BASIC[32768];",
    "",
    "    // Atari 2600 VCS Cartridge ROM (Pitfall 4KB)",
    "    extern const uint8 ATARI_2600_DEFAULT_ROM[4096];",
    "",
    "    // Query embedded ROM by name or alias",
    "    bool getROM(const std::string& name, const uint8*& data, size_t& size);",
    "}",
    ""
]

with open("src/embedded_roms.h", "w") as f:
    f.write("\n".join(header_lines))

print("Wrote src/embedded_roms.h")

# Generate src/embedded_roms.cpp
with open("src/embedded_roms.cpp", "w") as out:
    out.write('#include "embedded_roms.h"\n\n')
    out.write("namespace EmbeddedROMs {\n\n")

    for var_name, path, size, desc in rom_definitions:
        print(f"Embedding {var_name} from {path} ({size} bytes)...")
        with open(path, "rb") as f:
            data = f.read()
        assert len(data) == size, f"Size mismatch for {path}: expected {size}, got {len(data)}"

        out.write(f"// {desc} ({size} bytes)\n")
        out.write(f"const uint8 {var_name}[{size}] = {{\n")
        for i in range(0, size, 16):
            chunk = data[i:i+16]
            hex_vals = ", ".join(f"0x{b:02X}" for b in chunk)
            if i + 16 < size:
                out.write(f"    {hex_vals},\n")
            else:
                out.write(f"    {hex_vals}\n")
        out.write("};\n\n")

    # getROM lookup function
    lookup_code = """bool getROM(const std::string& name, const uint8*& data, size_t& size) {
    if (name == "os.rom" || name == "BBC_OS120" || name == "OS 1.20") {
        data = BBC_OS120; size = sizeof(BBC_OS120); return true;
    }
    if (name == "BASIC.ROM" || name == "BBC_BASIC" || name == "BASIC II") {
        data = BBC_BASIC; size = sizeof(BBC_BASIC); return true;
    }
    if (name == "DFS-0.9.rom" || name == "BBC_DFS09" || name == "DFS 0.9") {
        data = BBC_DFS09; size = sizeof(BBC_DFS09); return true;
    }
    if (name == "os01.rom" || name == "BBC_OS01" || name == "OS 0.10") {
        data = BBC_OS01; size = sizeof(BBC_OS01); return true;
    }
    if (name == "bpos.rom" || name == "BBC_BPOS" || name == "B+ OS") {
        data = BBC_BPOS; size = sizeof(BBC_BPOS); return true;
    }
    if (name == "mos3.20" || name == "BBC_MOS320" || name == "Master MegaROM") {
        data = BBC_MOS320; size = sizeof(BBC_MOS320); return true;
    }
    if (name == "Welcome.ssd" || name == "welcome.ssd" || name == "BBC_WELCOME_SSD") {
        data = BBC_WELCOME_SSD; size = sizeof(BBC_WELCOME_SSD); return true;
    }
    if (name == "mac128k.rom" || name == "MAC_128K" || name == "MAC_128K_ROM") {
        data = MAC_128K_ROM; size = sizeof(MAC_128K_ROM); return true;
    }
    if (name == "lisa.rom" || name == "lisa_boot.rom" || name == "LISA_BOOT" || name == "LISA_BOOT_ROM") {
        data = LISA_BOOT_ROM; size = sizeof(LISA_BOOT_ROM); return true;
    }
    if (name == "pocketstation.bin" || name == "POCKETSTATION_BIOS") {
        data = POCKETSTATION_BIOS; size = sizeof(POCKETSTATION_BIOS); return true;
    }
    if (name == "chocobo" || name == "chocobo.bin" || name == "POCKETSTATION_CHOCOBO_GAME") {
        data = POCKETSTATION_CHOCOBO_GAME; size = sizeof(POCKETSTATION_CHOCOBO_GAME); return true;
    }
    if (name == "dokodemo" || name == "dokodemo.bin" || name == "POCKETSTATION_DOKODEMO_GAME") {
        data = POCKETSTATION_DOKODEMO_GAME; size = sizeof(POCKETSTATION_DOKODEMO_GAME); return true;
    }
    if (name == "crash" || name == "crash.bin" || name == "POCKETSTATION_CRASH_GAME") {
        data = POCKETSTATION_CRASH_GAME; size = sizeof(POCKETSTATION_CRASH_GAME); return true;
    }
    if (name == "megaman" || name == "rockman" || name == "POCKETSTATION_MEGAMAN_GAME") {
        data = POCKETSTATION_MEGAMAN_GAME; size = sizeof(POCKETSTATION_MEGAMAN_GAME); return true;
    }
    if (name == "spyro" || name == "spyro.bin" || name == "POCKETSTATION_SPYRO_GAME") {
        data = POCKETSTATION_SPYRO_GAME; size = sizeof(POCKETSTATION_SPYRO_GAME); return true;
    }
    if (name == "space" || name == "invaders" || name == "POCKETSTATION_SPACE_GAME") {
        data = POCKETSTATION_SPACE_GAME; size = sizeof(POCKETSTATION_SPACE_GAME); return true;
    }
    if (name == "dungeon" || name == "dungeon.bin" || name == "POCKETSTATION_DUNGEON_GAME") {
        data = POCKETSTATION_DUNGEON_GAME; size = sizeof(POCKETSTATION_DUNGEON_GAME); return true;
    }
    if (name == "c64_kernal.rom" || name == "c64_kernal.bin" || name == "C64_KERNAL") {
        data = C64_KERNAL; size = sizeof(C64_KERNAL); return true;
    }
    if (name == "c64_basic.rom" || name == "c64_basic.bin" || name == "C64_BASIC") {
        data = C64_BASIC; size = sizeof(C64_BASIC); return true;
    }
    if (name == "c64_chargen.rom" || name == "c64_chargen.bin" || name == "C64_CHARGEN") {
        data = C64_CHARGEN; size = sizeof(C64_CHARGEN); return true;
    }
    if (name == "pc_bios.rom" || name == "IBM_PC_BIOS" || name == "BIOS_IBM5150_19OCT81_5700671_U33.BIN") {
        data = IBM_PC_BIOS; size = sizeof(IBM_PC_BIOS); return true;
    }
    if (name == "ibm-basic-1.00.rom" || name == "IBM_PC_BASIC" || name == "BASIC110.BIN") {
        data = IBM_PC_BASIC; size = sizeof(IBM_PC_BASIC); return true;
    }
    if (name == "pitfall.bin" || name == "atari_pitfall.bin" || name == "ATARI_2600_DEFAULT_ROM") {
        data = ATARI_2600_DEFAULT_ROM; size = sizeof(ATARI_2600_DEFAULT_ROM); return true;
    }
    return false;
}

} // namespace EmbeddedROMs
"""
    out.write(lookup_code)

print("Wrote src/embedded_roms.cpp successfully with all 7 PocketStation games!")
