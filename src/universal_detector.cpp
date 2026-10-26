#include "universal_detector.h"
#include <fstream>
#include <algorithm>
#include <cctype>

UniversalTarget UniversalDetector::detect(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::vector<uint8> data;
    if (f.is_open()) {
        data.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    }
    return detect(path, data);
}

UniversalTarget UniversalDetector::detect(const std::string& path, const std::vector<uint8>& data) {
    UniversalTarget t;

    std::string ext = "";
    size_t dot = path.find_last_of('.');
    if (dot != std::string::npos) ext = path.substr(dot);
    for (char& c : ext) c = (char)tolower(c);

    // 1. BBC Micro Disks (.ssd, .dsd)
    if (ext == ".ssd" || ext == ".dsd") {
        t.system = SystemType::BBCMicro;
        t.systemName = "BBC Micro";
        t.modelDetails = "BBC Model B (32KB)";
        t.confidence = 98;
        t.reason = "BBC Acorn DFS single/double sided disk format";
        return t;
    }

    // 2. Commodore 64 (.d64, .prg, .t64, .crt)
    if (ext == ".d64" || ext == ".prg" || ext == ".t64" || ext == ".crt") {
        t.system = SystemType::Commodore64;
        t.systemName = "Commodore 64";
        t.modelDetails = "Commodore 64 (PAL 6510)";
        t.confidence = 99;
        t.reason = (ext == ".d64") ? "Commodore 1541 D64 disk image" : "Commodore PRG/Cartridge binary";
        return t;
    }

    // 3. MS-DOS / IBM PC Executables (.com, .exe, .bat)
    if (ext == ".com" || ext == ".exe" || ext == ".bat") {
        t.system = SystemType::DosPC;
        t.systemName = "IBM PC / MS-DOS";
        t.modelDetails = "IBM PC / MS-DOS 6.22";
        t.confidence = 99;
        t.reason = (ext == ".exe") ? "MS-DOS MZ Executable binary" : "MS-DOS Command / COM binary";
        return t;
    }

    // 4. Apple Macintosh 128K (.dsk, .mac)
    if (ext == ".dsk" || ext == ".mac") {
        t.system = SystemType::Macintosh128K;
        t.systemName = "Apple Macintosh 128K";
        t.modelDetails = "Apple Macintosh 128K (68000)";
        t.confidence = 98;
        t.reason = "Apple Macintosh floppy disk image";
        return t;
    }

    // 5. Atari 2600 (.a26)
    if (ext == ".a26") {
        t.system = SystemType::Atari2600;
        t.systemName = "Atari 2600";
        t.modelDetails = "Atari 2600 VCS";
        t.confidence = 99;
        t.reason = "Atari 2600 cartridge ROM image";
        return t;
    }

    // 5. Apple Lisa (.dc42, .lisa, .twiggy)
    if (ext == ".dc42" || ext == ".lisa" || ext == ".twiggy") {
        t.system = SystemType::AppleLisa;
        t.systemName = "Apple Lisa 2";
        t.modelDetails = "Apple Lisa (Lisa OS 7/7, 68000)";
        t.confidence = 99;
        t.reason = "Apple Lisa disk image / Lisa Office System document";
        return t;
    }

    // 6. Sony PocketStation (.gme, .mcs, .psv, .mcr, .pkx, .pks)
    if (ext == ".gme" || ext == ".mcs" || ext == ".psv" || ext == ".mcr" || ext == ".pkx" || ext == ".pks") {
        t.system = SystemType::PocketStation;
        t.systemName = "Sony PocketStation";
        t.modelDetails = "Sony PocketStation (ARM7TDMI 32x32 LCD)";
        t.confidence = 99;
        t.reason = "PocketStation / PS1 Memory Card mini-game file";
        return t;
    }

    // 7. Jupiter Ace Forth Computer (.ace)
    if (ext == ".ace") {
        t.system = SystemType::JupiterAce;
        t.systemName = "Jupiter Ace";
        t.modelDetails = "Jupiter Ace (Forth Z80)";
        t.confidence = 99;
        t.reason = "Jupiter Ace Forth dictionary / snapshot image";
        return t;
    }

    // 8. Enterprise 64 / 128 (.epx)
    if (ext == ".epx") {
        t.system = SystemType::Enterprise;
        t.systemName = "Enterprise 64/128";
        t.modelDetails = "Enterprise 64/128 (EXOS 2.1)";
        t.confidence = 99;
        t.reason = "Enterprise EXOS Program Executable binary";
        return t;
    }

    // 9. Sharp MZ series (.mzf, .mzt, .m12)
    if (ext == ".mzf" || ext == ".mzt" || ext == ".m12") {
        t.system = SystemType::SharpMZ;
        t.systemName = "Sharp MZ-700";
        t.modelDetails = "Sharp MZ-700 (Clean Computer)";
        t.confidence = 99;
        t.reason = "Sharp MZ series cassette tape header and payload";
        return t;
    }

    // 10. Sinclair QL (.ql, .mdv)
    if (ext == ".ql" || ext == ".mdv") {
        t.system = SystemType::SinclairQL;
        t.systemName = "Sinclair QL";
        t.modelDetails = "Sinclair QL (68008 QDOS)";
        t.confidence = 99;
        t.reason = "Sinclair QL Microdrive cartridge / QDOS image";
        return t;
    }

    // 11. Oric Atmos / Oric-1 (.oric)
    if (ext == ".oric") {
        t.system = SystemType::Oric;
        t.systemName = "Oric Atmos";
        t.modelDetails = "Oric Atmos (6502A / BASIC v1.1)";
        t.confidence = 99;
        t.reason = "Oric Atmos binary format";
        return t;
    }

    // 12. Atari Lynx Handheld (.lnx, .lyx)
    if (ext == ".lnx" || ext == ".lyx") {
        t.system = SystemType::AtariLynx;
        t.systemName = "Atari Lynx";
        t.modelDetails = "Atari Lynx (Mikey/Suzy)";
        t.confidence = 99;
        t.reason = "Atari Lynx cartridge ROM image";
        return t;
    }

    // 13. Camputers Lynx (.cld, .clynx)
    if (ext == ".cld" || ext == ".clynx") {
        t.system = SystemType::CamputersLynx;
        t.systemName = "Camputers Lynx";
        t.modelDetails = "Camputers Lynx 48K/96K";
        t.confidence = 99;
        t.reason = "Camputers Lynx tape / disk binary";
        return t;
    }

    // 14. Sega Dreamcast VMU (.vms, .vmu, .dci)
    if (ext == ".vms" || ext == ".vmu" || ext == ".dci") {
        t.system = SystemType::SegaVMU;
        t.systemName = "Sega Dreamcast VMU";
        t.modelDetails = "Sega Dreamcast VMU (48x32 LCD)";
        t.confidence = 99;
        t.reason = "Sega Dreamcast Visual Memory Unit minigame/save";
        return t;
    }

    // 15. Bandai Tamagotchi (.tama, .tmgc, .p1, .p2)
    if (ext == ".tama" || ext == ".tmgc" || ext == ".p1" || ext == ".p2") {
        bool isP2 = (ext == ".p2");
        t.system = isP2 ? SystemType::TamagotchiP2 : SystemType::TamagotchiP1;
        t.systemName = isP2 ? "Bandai Tamagotchi P2 (1997)" : "Bandai Tamagotchi P1 (1996)";
        t.modelDetails = "Bandai Tamagotchi (E0C6S46 12-bit)";
        t.confidence = 99;
        t.reason = "Tamagotchi virtual pet ROM file";
        return t;
    }

    // Watara Supervision (.sv, .ws)
    if (ext == ".sv" || ext == ".ws") {
        t.system = SystemType::WataraSupervision;
        t.systemName = "Watara Supervision";
        t.modelDetails = "Watara Supervision (65C02 160x160 LCD)";
        t.confidence = 99;
        t.reason = "Watara Supervision cartridge image";
        return t;
    }

    // Texas Instruments Series (.rpk, .cart, .ctg, .83p, .8xp, .82p, .85p, .86p, .83g)
    if (ext == ".rpk" || ext == ".cart" || ext == ".ctg" ||
        ext == ".83p" || ext == ".8xp" || ext == ".82p" || ext == ".85p" || ext == ".86p" || ext == ".83g") {
        t.system = SystemType::TexasInstruments;
        t.systemName = "Texas Instruments";
        t.modelDetails = (ext.find("8") != std::string::npos) ? "TI-83 Plus Graphing Calculator" : "TI-99/4A Home Computer";
        t.confidence = 99;
        t.reason = "Texas Instruments software payload";
        return t;
    }

    // RCA Studio II (.st2, .studio2)
    if (ext == ".st2" || ext == ".studio2") {
        t.system = SystemType::RCAStudio2;
        t.systemName = "RCA Studio II";
        t.modelDetails = "RCA Studio II (CDP1802 COSMAC)";
        t.confidence = 99;
        t.reason = "RCA Studio II cartridge image";
        return t;
    }

    // Nintendo Pokémon Mini (.min)
    if (ext == ".min") {
        t.system = SystemType::PokemonMini;
        t.systemName = "Nintendo Pokémon Mini";
        t.modelDetails = "Nintendo Pokémon Mini (S1C88)";
        t.confidence = 99;
        t.reason = "Pokémon Mini cartridge ROM (.min)";
        return t;
    }

    // Milton Bradley Microvision (.mv, .mvision)
    if (ext == ".mv" || ext == ".mvision") {
        t.system = SystemType::Microvision;
        t.systemName = "Milton Bradley Microvision";
        t.modelDetails = "Milton Bradley Microvision (1979)";
        t.confidence = 99;
        t.reason = "Microvision cartridge ROM";
        return t;
    }

    // Sinclair ZX Spectrum (.sna, .tzx, .z80)
    if (ext == ".sna" || ext == ".tzx" || ext == ".z80") {
        t.system = SystemType::ZXSpectrum;
        t.systemName = "Sinclair ZX Spectrum";
        t.modelDetails = "Sinclair ZX Spectrum 48K/128K";
        t.confidence = 99;
        t.reason = "Sinclair ZX Spectrum snapshot/tape format";
        return t;
    }

    // 34. Amstrad CPC 464 (.cpc, .cdt)
    if (ext == ".cpc" || ext == ".cdt") {
        t.system = SystemType::AmstradCPC;
        t.systemName = "Amstrad CPC 464";
        t.modelDetails = "Amstrad CPC 464 (Locomotive BASIC)";
        t.confidence = 99;
        t.reason = "Amstrad CPC tape / disk payload";
        return t;
    }

    // 35. Magnavox Odyssey² (.o2)
    if (ext == ".o2") {
        t.system = SystemType::Odyssey2;
        t.systemName = "Magnavox Odyssey²";
        t.modelDetails = "Magnavox Odyssey² / Videopac";
        t.confidence = 99;
        t.reason = "Magnavox Odyssey² cartridge ROM (.o2)";
        return t;
    }

    // 36. Apple Newton (.newton, .pkg, .nos)
    if (ext == ".newton" || ext == ".pkg" || ext == ".nos") {
        t.system = SystemType::AppleNewton;
        t.systemName = "Apple Newton";
        t.modelDetails = "Apple Newton MessagePad (ARM610)";
        t.confidence = 99;
        t.reason = "Apple Newton ROM image or Newton Package (.pkg)";
        return t;
    }

    // 37. PICO-8 Fantasy Console (.p8, .p8.png)
    if (ext == ".p8" || (path.size() >= 7 && path.substr(path.size() - 7) == ".p8.png")) {
        t.system = SystemType::Pico8;
        t.systemName = "PICO-8";
        t.modelDetails = "PICO-8 Fantasy Console Cartridge";
        t.confidence = 99;
        t.reason = "PICO-8 Cartridge file (.p8 / .p8.png)";
        return t;
    }

    // Check PICO-8 Cartridge text header
    if (data.size() >= 16) {
        std::string header(data.begin(), data.begin() + std::min((size_t)64, data.size()));
        if (header.find("pico-8 cartridge") != std::string::npos || header.find("__lua__") != std::string::npos) {
            t.system = SystemType::Pico8;
            t.systemName = "PICO-8";
            t.modelDetails = "PICO-8 Fantasy Console Cartridge";
            t.confidence = 99;
            t.reason = "PICO-8 Cartridge text header detected";
            return t;
        }
    }

    // 38. Flipper Zero (.sub, .nfc, .rfid, .ir, .ibtn, .fap)
    if (ext == ".sub" || ext == ".nfc" || ext == ".rfid" || ext == ".ir" || ext == ".ibtn" || ext == ".fap") {
        t.system = SystemType::FlipperZero;
        t.systemName = "Flipper Zero";
        t.modelDetails = "Flipper Zero Portable Multi-Tool";
        t.confidence = 99;
        t.reason = "Flipper Zero device payload (" + ext + ")";
        return t;
    }

    // Check Flipper Zero text file header
    if (data.size() >= 18) {
        std::string header(data.begin(), data.begin() + std::min((size_t)80, data.size()));
        if (header.find("Filetype: Flipper") != std::string::npos || header.find("Flipper SubGhz") != std::string::npos || header.find("Flipper NFC") != std::string::npos || header.find("Flipper IR") != std::string::npos) {
            t.system = SystemType::FlipperZero;
            t.systemName = "Flipper Zero";
            t.modelDetails = "Flipper Zero Portable Multi-Tool";
            t.confidence = 99;
            t.reason = "Flipper Zero file signature detected";
            return t;
        }
    }

    // Check Oric .tap tape sync pattern: 0x16 0x16 0x16 0x24
    if (data.size() >= 4 && data[0] == 0x16 && data[1] == 0x16 && data[2] == 0x16 && data[3] == 0x24) {
        t.system = SystemType::Oric;
        t.systemName = "Oric Atmos";
        t.modelDetails = "Oric Atmos (6502A / BASIC v1.1)";
        t.confidence = 98;
        t.reason = "Oric Tangerine TAP sync signature detected";
        return t;
    }

    // Check Atari Lynx header magic: 'LYNX'
    if (data.size() >= 4 && data[0] == 'L' && data[1] == 'Y' && data[2] == 'N' && data[3] == 'X') {
        t.system = SystemType::AtariLynx;
        t.systemName = "Atari Lynx";
        t.modelDetails = "Atari Lynx (Mikey/Suzy)";
        t.confidence = 99;
        t.reason = "Atari Lynx LNX magic header";
        return t;
    }

    // 5. Binary header / Magic byte analysis for ambiguous extensions (.bin, .img, .dsk)
    if (data.size() >= 2 && data[0] == 'M' && data[1] == 'Z') {
        t.system = SystemType::DosPC;
        t.systemName = "IBM PC / MS-DOS";
        t.modelDetails = "IBM PC / MS-DOS 6.22";
        t.confidence = 99;
        t.reason = "MZ magic header: MS-DOS Executable";
        return t;
    }

    // Check Commodore D64 standard sizes (174,848 bytes for 35 tracks, 175,104 with error bytes)
    if (data.size() == 174848 || data.size() == 175104 || data.size() == 196608) {
        t.system = SystemType::Commodore64;
        t.systemName = "Commodore 64";
        t.modelDetails = "Commodore 64 (PAL 6510)";
        t.confidence = 95;
        t.reason = "1541 35-track D64 sector geometry";
        return t;
    }

    // Check Macintosh Floppy Sizes (400 KB = 409600 bytes, 800 KB = 819200 bytes, 1.44 MB)
    if (data.size() == 409600 || data.size() == 819200) {
        t.system = SystemType::Macintosh128K;
        t.systemName = "Apple Macintosh 128K";
        t.modelDetails = "Apple Macintosh 128K (68000)";
        t.confidence = 95;
        t.reason = "Macintosh 400K/800K MFS/HFS floppy image";
        return t;
    }

    // Check IBM PC FAT12 floppy boot sector (0xEB xx 0x90 or 0xE9 xx xx)
    if (data.size() >= 512 && ((data[0] == 0xEB && data[2] == 0x90) || data[0] == 0xE9)) {
        t.system = SystemType::DosPC;
        t.systemName = "IBM PC / MS-DOS";
        t.modelDetails = "IBM PC / MS-DOS 6.22";
        t.confidence = 92;
        t.reason = "FAT12 x86 Floppy Boot Sector (JMP opcode)";
        return t;
    }

    // Check BBC DFS Disk Sizes (102400 bytes for 40 tracks, 204800 bytes for 80 tracks)
    if (data.size() == 102400 || data.size() == 204800) {
        t.system = SystemType::BBCMicro;
        t.systemName = "BBC Micro";
        t.modelDetails = "BBC Model B (32KB)";
        t.confidence = 90;
        t.reason = "Standard BBC Acorn DFS disk image geometry";
        return t;
    }

    // Check Atari 2600 Cartridge sizes (2KB, 4KB, 8KB, 16KB)
    if (data.size() == 2048 || data.size() == 4096 || data.size() == 8192 || data.size() == 16384) {
        // Inspect 6502 reset vector at end of ROM
        uint16 rst = data[data.size() - 4] | ((uint16)data[data.size() - 3] << 8);
        if (rst >= 0xF000) {
            t.system = SystemType::Atari2600;
            t.systemName = "Atari 2600";
            t.modelDetails = "Atari 2600 VCS";
            t.confidence = 92;
            t.reason = "Atari 2600 cartridge ROM with 6507 reset vector";
            return t;
        }
    }

    // Check Apple Newton ROM (4MB, ARM branch instruction at 0x00000000)
    if (data.size() == 4194304 && (data[0] == 0xEA || data[3] == 0xEA)) {
        std::string lowerPath = path;
        for (char& c : lowerPath) c = (char)tolower(c);
        if (lowerPath.find("newton") != std::string::npos || lowerPath.find("v13") != std::string::npos || lowerPath.find("v100") != std::string::npos) {
            t.system = SystemType::AppleNewton;
            t.systemName = "Apple Newton";
            t.modelDetails = "Apple Newton MessagePad (ARM610 / RUNT)";
            t.confidence = 99;
            t.reason = "Apple Newton 4MB OS ROM image (ARM610 entrypoint)";
            return t;
        }
    }

    // Default fallback
    t.system = SystemType::BBCMicro;
    t.systemName = "BBC Micro";
    t.modelDetails = "BBC Model B (32KB)";
    t.confidence = 50;
    t.reason = "Default retro system fallback";
    return t;
}
