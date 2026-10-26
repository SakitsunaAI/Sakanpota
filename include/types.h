#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <array>
#include <memory>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>

using uint8 = uint8_t;
using int8 = int8_t;
using uint16 = uint16_t;
using int16 = int16_t;
using uint32 = uint32_t;
using int32 = int32_t;
using uint64 = uint64_t;
using int64 = int64_t;

enum class SystemType {
    None,
    BBCMicro,           // BBC Micro family (Model A, B, B+, Master 128)
    DosPC,              // IBM PC / MS-DOS (8086/Real Mode x86, CGA/VGA, FAT12/COM/EXE)
    Commodore64,        // Commodore 64 (6510, VIC-II, SID 6581, CIA, PRG/D64)
    Macintosh128K,      // Apple Macintosh 128K (Motorola 68000, 512x342 1-bit, DSK)
    AppleLisa,          // Apple Lisa (Motorola 68000, 720x364 1-bit, Lisa OS / Desktop)
    PocketStation,      // Sony PocketStation (ARM7 / PK201 core, 32x32 LCD, Flash, Sound)
    Atari2600,          // Atari 2600 (6507, TIA, A26/BIN)
    JupiterAce,         // Jupiter Ace (Z80 Forth computer)
    Enterprise,         // Enterprise 64 / 128 (Z80, Nick, Dave, EXOS 2.1)
    SharpMZ,            // Sharp MZ-700 / MZ-80K (Z80 Clean Computer, 1Z-013A)
    SinclairQL,         // Sinclair QL (Motorola 68008, QDOS & SuperBASIC)
    Oric,               // Oric-1 / Oric Atmos (6502A, ULA, BASIC v1.1)
    AtariLynx,          // Atari Lynx (65SC02 Mikey + Suzy 16-bit sprite engine)
    CamputersLynx,      // Camputers Lynx 48K/96K (Z80A, CRTC 6845)
    SegaVMU,            // Sega Dreamcast VMU (Sanyo LC86104C Potato, 48x32 LCD)
    TamagotchiP1,       // Bandai Tamagotchi P1 (1996 Gen 1, E0C6S46 12-bit)
    TamagotchiP2,       // Bandai Tamagotchi P2 (1997 Gen 2, E0C6S46 12-bit)
    WataraSupervision,  // Watara Supervision (65C02, 160x160 4-shade LCD, 1992)
    TexasInstruments,   // Texas Instruments Series (TI-99/4A TMS9900 & TI-83+ Z80)
    RCAStudio2,         // RCA Studio II (CDP1802 COSMAC, 64x32 Pixie, 5 Built-in Games)
    PokemonMini,        // Nintendo Pokémon Mini (S1C88, 96x64 LCD, Rumble, All 10 Games Embedded)
    Microvision,        // Milton Bradley Microvision (1979, TMS1100, 16x16 LCD, Paddle Knob, Block Buster)
    ZXSpectrum,         // Sinclair ZX Spectrum 48K/128K (Z80A, ULA 256x192, Sinclair BASIC, TAP/SNA)
    AmstradCPC,         // Amstrad CPC 464 (Z80A, CRTC 6845, Locomotive BASIC, Mode 0/1/2, DSK)
    Odyssey2,           // Magnavox Odyssey2 / Philips Videopac
    AppleNewton,        // Apple Newton MessagePad (OMP/100/110/120/130, ARM610, RUNT ASIC, 240x336 LCD)
    DreamwareVM2,       // Dreamware Enterprises VM2 (Next-Gen Dreamcast VMU, ARM OS, OLED, Multi-Page Flash)
    VMUPro,             // 8BitMods VMU Pro (Next-Gen Color VMU, ARM OS, Full-Color IPS, Multi-Bank)
    Pico8,              // PICO-8 Fantasy Console (128x128 16-color, 64KB RAM, 4ch Chiptune, Lua Carts)
    FlipperZero         // Flipper Zero Portable Multi-Tool (STM32WB55 Dual-Core, 128x64 Orange LCD, Sub-GHz, NFC, RFID, IR, BadUSB)
};

inline const char* getSystemTypeName(SystemType sys) {
    switch (sys) {
        case SystemType::BBCMicro: return "BBC Micro";
        case SystemType::DosPC: return "IBM PC / MS-DOS";
        case SystemType::Commodore64: return "Commodore 64";
        case SystemType::Macintosh128K: return "Apple Macintosh 128K";
        case SystemType::AppleLisa: return "Apple Lisa 2 (Lisa OS)";
        case SystemType::PocketStation: return "Sony PocketStation (PK201 Core)";
        case SystemType::Atari2600: return "Atari 2600 VCS";
        case SystemType::JupiterAce: return "Jupiter Ace (Forth Z80)";
        case SystemType::Enterprise: return "Enterprise 64/128 (EXOS)";
        case SystemType::SharpMZ: return "Sharp MZ-700 (Clean Computer)";
        case SystemType::SinclairQL: return "Sinclair QL (68008 QDOS)";
        case SystemType::Oric: return "Oric Atmos / Oric-1 (6502)";
        case SystemType::AtariLynx: return "Atari Lynx (Mikey/Suzy)";
        case SystemType::CamputersLynx: return "Camputers Lynx 48K/96K";
        case SystemType::SegaVMU: return "Sega Dreamcast VMU";
        case SystemType::TamagotchiP1: return "Bandai Tamagotchi P1 (1996)";
        case SystemType::TamagotchiP2: return "Bandai Tamagotchi P2 (1997)";
        case SystemType::WataraSupervision: return "Watara Supervision";
        case SystemType::TexasInstruments: return "Texas Instruments Series";
        case SystemType::RCAStudio2: return "RCA Studio II (COSMAC)";
        case SystemType::PokemonMini: return "Nintendo Pokémon Mini (S1C88)";
        case SystemType::Microvision: return "Milton Bradley Microvision (1979)";
        case SystemType::ZXSpectrum: return "Sinclair ZX Spectrum 48K/128K";
        case SystemType::AmstradCPC: return "Amstrad CPC 464 (Locomotive BASIC)";
        case SystemType::Odyssey2: return "Magnavox Odyssey2 / Videopac";
        case SystemType::AppleNewton: return "Apple Newton MessagePad (ARM610)";
        case SystemType::DreamwareVM2: return "Dreamware Enterprises VM2";
        case SystemType::VMUPro: return "8BitMods VMU Pro";
        case SystemType::Pico8: return "PICO-8 Fantasy Console";
        case SystemType::FlipperZero: return "Flipper Zero Portable Multi-Tool";
        default: return "Universal Retro Hub";
    }
}

enum class BBCModel {
    ModelA,      // BBC Model A (16KB RAM, MOS 0.10/1.20)
    ModelB,      // BBC Model B (32KB RAM, MOS 1.20, Sideways ROMs)
    ModelBPlus,  // BBC Model B+ (64KB/128KB RAM, Shadow RAM, WD1770)
    Master128    // BBC Master 128 (65C02, 128KB RAM, CMOS RTC, MegaROM MOS 3.20)
};

inline const char* getModelName(BBCModel model) {
    switch (model) {
        case BBCModel::ModelA: return "BBC Micro Model A (16KB)";
        case BBCModel::ModelB: return "BBC Micro Model B (32KB)";
        case BBCModel::ModelBPlus: return "BBC Micro Model B+ (64KB/128KB)";
        case BBCModel::Master128: return "BBC Master 128 (65C02 128KB)";
        default: return "Unknown BBC Model";
    }
}
