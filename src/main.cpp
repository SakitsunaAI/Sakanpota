#include "types.h"
#include "emulator_core.h"
#include "bbc_machine.h"
#include "dos_machine.h"
#include "c64_machine.h"
#include "mac_machine.h"
#include "lisa_machine.h"
#include "pocketstation_machine.h"
#include "atari_machine.h"
#include "jupiter_ace_machine.h"
#include "enterprise_machine.h"
#include "sharp_mz_machine.h"
#include "sinclair_ql_machine.h"
#include "oric_machine.h"
#include "lynx_machine.h"
#include "camputers_lynx_machine.h"
#include "vmu_machine.h"
#include "tamagotchi_machine.h"
#include "supervision_machine.h"
#include "ti_machine.h"
#include "studio2_machine.h"
#include "pokemon_mini_machine.h"
#include "microvision_machine.h"
#include "spectrum_machine.h"
#include "cpc_machine.h"
#include "odyssey2_machine.h"
#include "embedded_roms.h"
#include "embedded_roms_ext.h"
#include "newton_machine.h"
#include "vm2_machine.h"
#include "vmu_pro_machine.h"
#include "pico8_machine.h"
#include "embedded_pico8_carts.h"
#include "flipper_machine.h"
#include "embedded_flipper_assets.h"
#include "universal_detector.h"
#include "crt_filter.h"
#include "gui_font.h"
#include <SDL.h>
#include <iostream>
#include <memory>
#include <chrono>
#include <cstring>

static void printHelp() {
    std::cout << "===================================================================\n"
              << "       Sakanpota - Universal Retro Multi-System Emulator           \n"
              << "===================================================================\n"
              << "Usage: ./sakanpota [command | file_to_run] [options]\n\n"
              << "Direct System Commands:\n"
              << "  ./sakanpota poke           -> Nintendo Pokémon Mini\n"
              << "  ./sakanpota microvision    -> Milton Bradley Microvision (1979 TMS1100 Paddle)\n"
              << "  ./sakanpota studio2        -> RCA Studio II (CDP1802 COSMAC)\n"
              << "  ./sakanpota odyssey2       -> Magnavox Odyssey² / Videopac (KC Munchkin)\n"
              << "  ./sakanpota spectrum       -> Sinclair ZX Spectrum 48K (Sinclair BASIC)\n"
              << "  ./sakanpota cpc            -> Amstrad CPC 464 (Locomotive BASIC 1.0)\n"
              << "  ./sakanpota tamap1         -> Bandai Tamagotchi P1 (1996 Generation 1)\n"
              << "  ./sakanpota tamap2         -> Bandai Tamagotchi P2 (1997 Generation 2)\n"
              << "  ./sakanpota vmu            -> Sega Dreamcast Visual Memory Unit (VMU)\n"
              << "  ./sakanpota 8bitmods [game]-> 8BitMods VMU Pro (Full-Color IPS, 128 Banks)\n"
              << "  ./sakanpota vmuvm2 [game]  -> Dreamware Enterprises VM2 (High-Contrast OLED)\n"
              << "  ./sakanpota supervision    -> Watara Supervision (65C02 Handheld 160x160)\n"
              << "  ./sakanpota ti99           -> Texas Instruments TI-99/4A (TMS9900/BASIC)\n"
              << "  ./sakanpota ti83           -> TI-83 Plus Graphing Calculator (Z80/TI-OS)\n"
              << "  ./sakanpota ace            -> Jupiter Ace (Forth Z80 Computer)\n"
              << "  ./sakanpota enterprise     -> Enterprise 64/128 (EXOS 2.1 & IS-BASIC)\n"
              << "  ./sakanpota sharpmz        -> Sharp MZ-700 / MZ-80K (Clean Computer)\n"
              << "  ./sakanpota ql             -> Sinclair QL (Motorola 68008, QDOS)\n"
              << "  ./sakanpota oric           -> Oric Atmos (6502A, BASIC v1.1)\n"
              << "  ./sakanpota lynx           -> Atari Lynx (Mikey 65SC02 + Suzy)\n"
              << "  ./sakanpota clynx          -> Camputers Lynx 48K/96K (Z80A)\n"
              << "  ./sakanpota pocket [game]  -> Sony PocketStation PK201 (chocobo/crash/etc.)\n"
              << "  ./sakanpota dos            -> IBM PC / MS-DOS 6.22\n"
              << "  ./sakanpota c64            -> Commodore 64 (PAL 6510)\n"
              << "  ./sakanpota mac            -> Apple Macintosh 128K (68000)\n"
              << "  ./sakanpota lisa           -> Apple Lisa 2 (Lisa OS)\n"
              << "  ./sakanpota atari          -> Atari 2600 VCS\n"
              << "  ./sakanpota bbc            -> BBC Micro Model B (32KB)\n  ./sakanpota newton         -> Apple Newton MessagePad (ARM610 / RUNT ASIC)\n"
              << "  ./sakanpota pico8 [game]   -> PICO-8 Fantasy Console (celeste/dusk/maichan/racers/mistigri)\n"
              << "  ./sakanpota flipper [tool] -> Flipper Zero (subghz/nfc/rfid/ir/ibutton/badusb/games)\n\n"
              << "Automatic Platform Detection (Drag & Drop or CLI File):\n"
              << "  Simply drop or pass ANY disk, tape, cartridge, or program file!\n"
              << ""
              << "  High-precision pure interpreter mode for all CPUs (NO JIT).\n\n"
              << "Hotkeys & Shortcuts:\n"
              << "  F5                  Load BBC 'discs/Welcome.ssd'\n"
              << "  F6                  Load BBC 'discs/elite.ssd'\n"
              << "  F7                  Cycle CRT Color: RGB -> Green -> Amber -> B&W\n"
              << "  F8                  Toggle CRT Scanlines & Screen Curvature\n"
              << "  F9                  Toggle Audio Mute\n"
              << "  F11 / Tab           Toggle On-Screen Info Header\n"
              << "  F12                 System Reset\n"
              << "  Drag & Drop         Drop ANY retro file to auto-detect and run!\n"
              << "===================================================================\n";
}

int main(int argc, char* argv[]) {
    // Initialize decrypted assets from Atari hardware key
    EmbeddedROMs::ensureDecrypted();
    EmbeddedROMs::ensureExtDecrypted();
    EmbeddedPico8Carts::ensureDecrypted();
    EmbeddedFlipperAssets::ensureDecrypted();
    std::string startFile = "";
    std::string romDir = "roms";
    bool launchPocket = false;
    std::string pocketGame = "chocobo";
    bool launchVM2 = false;
    std::string vm2Game = "";
    bool launchVMUPro = false;
    std::string vmuProGame = "";
    bool launchPico8 = false;
    std::string pico8Game = "";
    bool launchFlipper = false;
    std::string flipperTool = "";
    SystemType forceSystem = SystemType::None;
    TIMode tiMode = TIMode::TI99_4A;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printHelp();
            return 0;
        } else if (arg == "flipper" || arg == "f0" || arg == "flipperzero" || arg == "--flipper" || arg == "--f0") {
            launchFlipper = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                flipperTool = argv[++i];
            }
        } else if (arg == "pico8" || arg == "p8" || arg == "--pico8" || arg == "--p8") {
            launchPico8 = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                pico8Game = argv[++i];
            }
        } else if (arg == "pocket" || arg == "pocketstation" || arg == "--pocket") {
            launchPocket = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                pocketGame = argv[++i];
            }
        } else if (arg == "8bitmods" || arg == "vmupro" || arg == "8bitmodsvmu" || arg == "--8bitmods" || arg == "--vmupro") {
            launchVMUPro = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                vmuProGame = argv[++i];
            }
        } else if (arg == "vmuvm2" || arg == "vm2" || arg == "dreamware" || arg == "dreamwarevm2" || arg == "--vmuvm2" || arg == "--vm2") {
            launchVM2 = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                vm2Game = argv[++i];
            }
        } else if (arg == "tamap1" || arg == "tama1" || arg == "tamagotchip1" || arg == "--tamap1") {
            forceSystem = SystemType::TamagotchiP1;
        } else if (arg == "tamap2" || arg == "tama2" || arg == "tamagotchip2" || arg == "--tamap2") {
            forceSystem = SystemType::TamagotchiP2;
        } else if (arg == "vmu" || arg == "segavmu" || arg == "--vmu") {
            forceSystem = SystemType::SegaVMU;
        } else if (arg == "supervision" || arg == "watara" || arg == "--supervision") {
            forceSystem = SystemType::WataraSupervision;
        } else if (arg == "ti" || arg == "ti99" || arg == "ti-99" || arg == "ti994a" || arg == "ti-994a" ||
                   arg == "ti-99/4a" || arg == "ti99/4a" || arg == "texas" || arg == "texasinstruments" ||
                   arg == "texasinsturements" || arg == "--ti" || arg == "--ti99" || arg == "--texas") {
            forceSystem = SystemType::TexasInstruments;
            tiMode = TIMode::TI99_4A;
        } else if (arg == "ti83" || arg == "ti-83" || arg == "ti83p" || arg == "ti83plus" ||
                   arg == "ti-83+" || arg == "ti-83plus" || arg == "--ti83") {
            forceSystem = SystemType::TexasInstruments;
            tiMode = TIMode::TI83_Plus;
        } else if (arg == "ace" || arg == "jupiter" || arg == "jupiterace" || arg == "--ace") {
            forceSystem = SystemType::JupiterAce;
        } else if (arg == "enterprise" || arg == "ep64" || arg == "ep128" || arg == "--enterprise") {
            forceSystem = SystemType::Enterprise;
        } else if (arg == "sharpmz" || arg == "mz700" || arg == "mz80k" || arg == "--sharpmz") {
            forceSystem = SystemType::SharpMZ;
        } else if (arg == "ql" || arg == "sinclairql" || arg == "--ql") {
            forceSystem = SystemType::SinclairQL;
        } else if (arg == "oric" || arg == "atmos" || arg == "oric1" || arg == "--oric") {
            forceSystem = SystemType::Oric;
        } else if (arg == "lynx" || arg == "atarilynx" || arg == "--lynx") {
            forceSystem = SystemType::AtariLynx;
        } else if (arg == "clynx" || arg == "camputers" || arg == "--clynx") {
            forceSystem = SystemType::CamputersLynx;
        } else if (arg == "dos" || arg == "ibmpc" || arg == "--dos") {
            forceSystem = SystemType::DosPC;
        } else if (arg == "c64" || arg == "commodore" || arg == "--c64") {
            forceSystem = SystemType::Commodore64;
        } else if (arg == "mac" || arg == "macintosh" || arg == "--mac") {
            forceSystem = SystemType::Macintosh128K;
        } else if (arg == "lisa" || arg == "applelisa" || arg == "--lisa") {
            forceSystem = SystemType::AppleLisa;
        } else if (arg == "poke" || arg == "pokemini" || arg == "pokemonmini" || arg == "--poke") {
            forceSystem = SystemType::PokemonMini;
        } else if (arg == "studio2" || arg == "rcastudio2" || arg == "--studio2") {
            forceSystem = SystemType::RCAStudio2;
        } else if (arg == "microvision" || arg == "mv" || arg == "--microvision") {
            forceSystem = SystemType::Microvision;
        } else if (arg == "spectrum" || arg == "zxspectrum" || arg == "zx" || arg == "--spectrum") {
            forceSystem = SystemType::ZXSpectrum;
        } else if (arg == "cpc" || arg == "cpc464" || arg == "amstrad" || arg == "--cpc") {
            forceSystem = SystemType::AmstradCPC;
        } else if (arg == "odyssey" || arg == "odyssey2" || arg == "videopac" || arg == "--odyssey2") {
            forceSystem = SystemType::Odyssey2;
        } else if (arg == "newton" || arg == "applenewton" || arg == "messagepad" || arg == "--newton") {
            forceSystem = SystemType::AppleNewton;
        } else if (arg == "atari" || arg == "atari2600" || arg == "--atari") {
            forceSystem = SystemType::Atari2600;
        } else if (arg == "bbc" || arg == "bbcmicro" || arg == "--bbc") {
            forceSystem = SystemType::BBCMicro;
        } else if (arg == "--roms" && i + 1 < argc) {
            romDir = argv[++i];
        } else if (arg == "--disk" && i + 1 < argc) {
            startFile = argv[++i];
        } else if (arg[0] != '-') {
            startFile = arg;
        }
    }

    // Initialize SDL2
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        std::cerr << "Error: Failed to initialize SDL2: " << SDL_GetError() << "\n";
        return 1;
    }

    // Create SDL Window & Renderer
    int winWidth = 1024;
    int winHeight = 820;
    SDL_Window* window = SDL_CreateWindow(
        "Sakanpota - Universal Multi-System Retro Emulator",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        winWidth, winHeight,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
    );

    if (!window) {
        std::cerr << "Error: Could not create SDL Window: " << SDL_GetError() << "\n";
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(
        window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );

    if (!renderer) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }

    // Set pixel-perfect nearest neighbor scaling
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    SDL_RenderSetLogicalSize(renderer, CRTC6845::SCREEN_WIDTH, CRTC6845::SCREEN_HEIGHT);

    // Streaming texture for 640x512 canvas
    SDL_Texture* screenTexture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        CRTC6845::SCREEN_WIDTH, CRTC6845::SCREEN_HEIGHT
    );

    // Audio setup (44.1 kHz, 16-bit PCM Mono)
    SDL_AudioSpec desiredAudio, obtainedAudio;
    SDL_zero(desiredAudio);
    desiredAudio.freq = 44100;
    desiredAudio.format = AUDIO_S16SYS;
    desiredAudio.channels = 1;
    desiredAudio.samples = 1024;
    desiredAudio.callback = nullptr;

    SDL_AudioDeviceID audioDevice = SDL_OpenAudioDevice(nullptr, 0, &desiredAudio, &obtainedAudio, 0);
    bool audioMuted = false;
    if (audioDevice) {
        SDL_PauseAudioDevice(audioDevice, 0);
    }

    // Enable SDL Drag and Drop
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);

    // CRT Filter for rich retro aesthetic
    CRTFilter crtFilter;

    // Active Machine Polymorphic Core
    std::unique_ptr<EmulatorCore> activeMachine;

    // OSD timer (visible on boot / media insert, auto-hides after 5 seconds)
    int osdTimer = 250;
    bool osdManualToggle = false;

    // Lambda to instantiate and switch system dynamically based on auto-detection or command
    auto switchSystem = [&](SystemType sys, const std::string& mediaPath = "") {
        if (sys == SystemType::DosPC) {
            activeMachine = std::make_unique<DosMachine>();
        } else if (sys == SystemType::Commodore64) {
            activeMachine = std::make_unique<C64Machine>();
        } else if (sys == SystemType::Macintosh128K) {
            activeMachine = std::make_unique<MacMachine>();
        } else if (sys == SystemType::AppleLisa) {
            activeMachine = std::make_unique<LisaMachine>();
        } else if (sys == SystemType::PocketStation) {
            activeMachine = std::make_unique<PocketStationMachine>();
        } else if (sys == SystemType::Atari2600) {
            activeMachine = std::make_unique<AtariMachine>();
        } else if (sys == SystemType::JupiterAce) {
            activeMachine = std::make_unique<JupiterAceMachine>();
        } else if (sys == SystemType::Enterprise) {
            activeMachine = std::make_unique<EnterpriseMachine>();
        } else if (sys == SystemType::SharpMZ) {
            activeMachine = std::make_unique<SharpMZMachine>();
        } else if (sys == SystemType::SinclairQL) {
            activeMachine = std::make_unique<SinclairQLMachine>();
        } else if (sys == SystemType::Oric) {
            activeMachine = std::make_unique<OricMachine>();
        } else if (sys == SystemType::AtariLynx) {
            activeMachine = std::make_unique<LynxMachine>();
        } else if (sys == SystemType::CamputersLynx) {
            activeMachine = std::make_unique<CamputersLynxMachine>();
        } else if (sys == SystemType::SegaVMU) {
            activeMachine = std::make_unique<SegaVMUMachine>();
        } else if (sys == SystemType::TamagotchiP1) {
            activeMachine = std::make_unique<TamagotchiMachine>(TamagotchiGen::Gen1_P1);
        } else if (sys == SystemType::TamagotchiP2) {
            activeMachine = std::make_unique<TamagotchiMachine>(TamagotchiGen::Gen2_P2);
        } else if (sys == SystemType::WataraSupervision) {
            activeMachine = std::make_unique<WataraSupervisionMachine>();
        } else if (sys == SystemType::TexasInstruments) {
            activeMachine = std::make_unique<TexasInstrumentsMachine>(tiMode);
        } else if (sys == SystemType::RCAStudio2) {
            activeMachine = std::make_unique<Studio2Machine>();
        } else if (sys == SystemType::PokemonMini) {
            activeMachine = std::make_unique<PokemonMiniMachine>();
        } else if (sys == SystemType::Microvision) {
            activeMachine = std::make_unique<MicrovisionMachine>();
        } else if (sys == SystemType::ZXSpectrum) {
            activeMachine = std::make_unique<ZXSpectrumMachine>();
        } else if (sys == SystemType::AmstradCPC) {
            activeMachine = std::make_unique<AmstradCPCMachine>();
        } else if (sys == SystemType::Odyssey2) {
            activeMachine = std::make_unique<Odyssey2Machine>();
        } else if (sys == SystemType::AppleNewton) {
            activeMachine = std::make_unique<NewtonMachine>();
        } else if (sys == SystemType::DreamwareVM2) {
            activeMachine = std::make_unique<DreamwareVM2Machine>();
        } else if (sys == SystemType::VMUPro) {
            activeMachine = std::make_unique<VMUProMachine>();
        } else if (sys == SystemType::Pico8) {
            activeMachine = std::make_unique<Pico8Machine>();
        } else if (sys == SystemType::FlipperZero) {
            activeMachine = std::make_unique<FlipperMachine>();
        } else {
            auto bbc = std::make_unique<BBCMachine>();
            bbc->romDirectory = romDir;
            bbc->switchModel(BBCModel::ModelB);
            activeMachine = std::move(bbc);
        }

        activeMachine->init();
        if (!mediaPath.empty()) {
            activeMachine->loadMedia(mediaPath);
        }
        osdTimer = 250; // Show HUD for 5 seconds on system switch
    };

    // Check direct CLI commands first
    if (forceSystem != SystemType::None) {
        std::cout << ">>> Starting " << getSystemTypeName(forceSystem) << " <<<\n";
        switchSystem(forceSystem, startFile);
    } else if (launchFlipper) {
        std::cout << ">>> [FLIPPER ZERO] Booting Flipper Zero Portable Multi-Tool" << (flipperTool.empty() ? "" : ": " + flipperTool) << " <<<\n";
        switchSystem(SystemType::FlipperZero);
        if (auto f0 = dynamic_cast<FlipperMachine*>(activeMachine.get())) {
            if (!flipperTool.empty()) {
                f0->loadToolByName(flipperTool);
            }
        }
    } else if (launchPico8) {
        std::cout << ">>> [PICO-8] Booting PICO-8 Fantasy Console" << (pico8Game.empty() ? "" : ": " + pico8Game) << " <<<\n";
        switchSystem(SystemType::Pico8);
        if (auto p8 = dynamic_cast<Pico8Machine*>(activeMachine.get())) {
            if (!pico8Game.empty()) {
                p8->loadGameByName(pico8Game);
            }
        }
    } else if (launchPocket) {
        std::cout << ">>> [POCKETSTATION] Booting Sony PocketStation: " << pocketGame << " <<<\n";
        switchSystem(SystemType::PocketStation);
        if (auto ps = dynamic_cast<PocketStationMachine*>(activeMachine.get())) {
            ps->loadGameByName(pocketGame);
        }
    } else if (launchVMUPro) {
        std::cout << ">>> [8BITMODS VMU PRO] Booting 8BitMods VMU Pro" << (vmuProGame.empty() ? "" : ": " + vmuProGame) << " <<<\n";
        switchSystem(SystemType::VMUPro);
        if (auto vp = dynamic_cast<VMUProMachine*>(activeMachine.get())) {
            if (!vmuProGame.empty()) {
                vp->loadGameByName(vmuProGame);
            }
        }
    } else if (launchVM2) {
        std::cout << ">>> [DREAMWARE VM2] Booting Dreamware VM2" << (vm2Game.empty() ? "" : ": " + vm2Game) << " <<<\n";
        switchSystem(SystemType::DreamwareVM2);
        if (auto vm = dynamic_cast<DreamwareVM2Machine*>(activeMachine.get())) {
            if (!vm2Game.empty()) {
                vm->loadGameByName(vm2Game);
            }
        }
    } else if (!startFile.empty()) {
        UniversalTarget target = UniversalDetector::detect(startFile);
        std::cout << ">>> [AUTO-DETECT] Starting file: " << startFile << " <<<\n"
                  << "    System: " << target.systemName << "\n"
                  << "    Model: " << target.modelDetails << "\n"
                  << "    Confidence: " << target.confidence << "% (" << target.reason << ")\n";
        switchSystem(target.system, startFile);
    } else {
        // Default to BBC Micro with Welcome.ssd
        switchSystem(SystemType::BBCMicro, "discs/Welcome.ssd");
    }

    // Timing and FPS
    auto lastFrameTime = std::chrono::steady_clock::now();
    auto fpsTimer = std::chrono::steady_clock::now();
    int frameCounter = 0;
    float currentFPS = 50.0f;
    bool running = true;

    // Display buffer for final composition
    std::vector<uint32> displayBuffer(CRTC6845::SCREEN_WIDTH * CRTC6845::SCREEN_HEIGHT);
    std::vector<uint32> crtFilteredBuffer(CRTC6845::SCREEN_WIDTH * CRTC6845::SCREEN_HEIGHT);

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
                break;
            }

            // Universal Drag & Drop handler: auto-detects ANY retro platform!
            if (event.type == SDL_DROPFILE) {
                char* droppedFile = event.drop.file;
                if (droppedFile) {
                    UniversalTarget target = UniversalDetector::detect(droppedFile);
                    std::cout << "\n=======================================================\n"
                              << ">>> [UNIVERSAL AUTO-DETECT] Media File Dropped! <<<\n"
                              << "  File: " << droppedFile << "\n"
                              << "  Detected Platform: " << target.systemName << "\n"
                              << "  Target Model:      " << target.modelDetails << "\n"
                              << "  Confidence Score:  " << target.confidence << "%\n"
                              << "  Detection Reason:  " << target.reason << "\n"
                              << "=======================================================\n";

                    switchSystem(target.system, droppedFile);
                    SDL_free(droppedFile);
                }
                continue;
            }

            if (event.type == SDL_KEYDOWN) {
                SDL_Keycode sym = event.key.keysym.sym;
                uint16 mod = event.key.keysym.mod;

                // Universal Function Hotkeys (NO manual model switching!)
                if (sym == SDLK_F5) {
                    switchSystem(SystemType::BBCMicro, "discs/Welcome.ssd");
                    continue;
                }
                if (sym == SDLK_F6) {
                    switchSystem(SystemType::BBCMicro, "discs/elite.ssd");
                    continue;
                }
                if (sym == SDLK_F7) {
                    crtFilter.cycleColorMode();
                    std::cout << "CRT Color Mode: " << crtFilter.getColorModeName() << "\n";
                    continue;
                }
                if (sym == SDLK_F8) {
                    crtFilter.scanlinesEnabled = !crtFilter.scanlinesEnabled;
                    crtFilter.curvatureEnabled = crtFilter.scanlinesEnabled;
                    crtFilter.phosphorGlowEnabled = crtFilter.scanlinesEnabled;
                    std::cout << "CRT Filter (Curvature & Scanlines): " << (crtFilter.scanlinesEnabled ? "ON" : "OFF") << "\n";
                    continue;
                }
                if (sym == SDLK_F9) {
                    audioMuted = !audioMuted;
                    std::cout << "Audio: " << (audioMuted ? "MUTED" : "UNMUTED") << "\n";
                    continue;
                }
                if (sym == SDLK_F11 || sym == SDLK_TAB) {
                    osdManualToggle = !osdManualToggle;
                    osdTimer = osdManualToggle ? 999999 : 0;
                    std::cout << "OSD Overlay: " << (osdTimer > 0 ? "SHOWN" : "HIDDEN") << "\n";
                    continue;
                }
                if (sym == SDLK_F12) {
                    std::cout << "System Reset Triggered\n";
                    if (activeMachine) activeMachine->reset();
                    osdTimer = 200;
                    continue;
                }

                // Route key event to active emulator core
                if (activeMachine) {
                    activeMachine->handleKeyDown(sym, mod);
                }
            }

            if (event.type == SDL_KEYUP) {
                if (activeMachine) {
                    activeMachine->handleKeyUp(event.key.keysym.sym, event.key.keysym.mod);
                }
            }

            if (event.type == SDL_MOUSEMOTION) {
                if (activeMachine) activeMachine->handleMouseMotion(event.motion.x, event.motion.y);
            }

            if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) {
                bool pressed = (event.type == SDL_MOUSEBUTTONDOWN);
                if (activeMachine) activeMachine->handleMouseButton(event.button.button, pressed, event.button.x, event.button.y);
            }
        }

        // Run 1 frame of the active retro system
        if (activeMachine) {
            activeMachine->runFrame();
        }

        // Stream audio from active machine
        if (audioDevice && !audioMuted && activeMachine) {
            uint32 queuedBytes = SDL_GetQueuedAudioSize(audioDevice);
            if (queuedBytes < 8192) {
                SDL_QueueAudio(audioDevice, activeMachine->getAudioBuffer(),
                               activeMachine->getAudioSampleCount() * sizeof(int16));
            }
        }

        // Get rendered screen buffer
        const uint32* rawPixels = activeMachine ? activeMachine->getRenderedScreen() : nullptr;
        if (rawPixels) {
            if (crtFilter.scanlinesEnabled || crtFilter.curvatureEnabled || crtFilter.colorMode != CRTColorMode::RGB) {
                crtFilter.process(rawPixels, crtFilteredBuffer.data(),
                                  CRTC6845::SCREEN_WIDTH, CRTC6845::SCREEN_HEIGHT,
                                  CRTC6845::SCREEN_WIDTH * 4);
                std::memcpy(displayBuffer.data(), crtFilteredBuffer.data(), displayBuffer.size() * sizeof(uint32));
            } else {
                std::memcpy(displayBuffer.data(), rawPixels, displayBuffer.size() * sizeof(uint32));
            }
        } else {
            std::fill(displayBuffer.begin(), displayBuffer.end(), 0xFF000000);
        }

        int pitch = CRTC6845::SCREEN_WIDTH * 4;

        if (osdTimer > 0) {
            osdTimer--;
            // Render Top Status Header (Sleek dark translucent banner, y: 0..20)
            GUIFont::fillRect(displayBuffer.data(), pitch, 0, 0, 640, 20, 0xEE0B0E14);
            std::string sysTitle = activeMachine ? ("[" + activeMachine->getSystemName() + "]") : "[Retro Hub]";
            GUIFont::drawString(displayBuffer.data(), pitch, 8, 6, sysTitle, 0xFFFFCC00);

            if (activeMachine) {
                std::string mediaStr = "[Auto-Detect: " + activeMachine->getMediaName() + "]";
                if (mediaStr.length() > 32) mediaStr = mediaStr.substr(0, 30) + "..]";
                GUIFont::drawString(displayBuffer.data(), pitch, 320, 6, mediaStr, 0xFF40E0D0);
            }

            // Render Bottom Info Footer (y: 492..512)
            GUIFont::fillRect(displayBuffer.data(), pitch, 0, 492, 640, 20, 0xEE0B0E14);
            char statBuf[128];
            snprintf(statBuf, sizeof(statBuf), "%.1f FPS | Pure Interpreter Active", currentFPS);
            GUIFont::drawString(displayBuffer.data(), pitch, 8, 498, statBuf, 0xFF80D0FF);

            char crtBuf[128];
            snprintf(crtBuf, sizeof(crtBuf), "CRT: %s %s [F7/F8]",
                     crtFilter.getColorModeName(),
                     crtFilter.scanlinesEnabled ? "(Scanlines)" : "");
            GUIFont::drawString(displayBuffer.data(), pitch, 310, 498, crtBuf, 0xFFFFB86C);

            GUIFont::drawString(displayBuffer.data(), pitch, 550, 498, "Tab: HUD", 0xFFAAAAAA);
        }

        // Upload to SDL Texture
        SDL_UpdateTexture(screenTexture, nullptr, displayBuffer.data(), pitch);

        // Render to window
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, screenTexture, nullptr, nullptr);
        SDL_RenderPresent(renderer);

        // Calculate FPS
        frameCounter++;
        auto now = std::chrono::steady_clock::now();
        std::chrono::duration<float> elapsed = now - fpsTimer;
        if (elapsed.count() >= 1.0f) {
            currentFPS = frameCounter / elapsed.count();
            frameCounter = 0;
            fpsTimer = now;

            // Update Window Title with dynamic metrics
            char title[256];
            snprintf(title, sizeof(title),
                     "Universal Retro Emulator - %s | %s | %.1f FPS | CRT: %s",
                     activeMachine ? activeMachine->getSystemName().c_str() : "Retro Hub",
                     activeMachine ? activeMachine->getMediaName().c_str() : "No Media",
                     currentFPS, crtFilter.getColorModeName());
            SDL_SetWindowTitle(window, title);
        }

        // 50/60 Hz Frame limiter (18 ms per frame)
        auto frameEndTime = std::chrono::steady_clock::now();
        std::chrono::duration<double, std::milli> frameDuration = frameEndTime - lastFrameTime;
        if (frameDuration.count() < 18.0) {
            SDL_Delay(static_cast<Uint32>(18.0 - frameDuration.count()));
        }
        lastFrameTime = std::chrono::steady_clock::now();
    }

    // Cleanup
    if (audioDevice) {
        SDL_CloseAudioDevice(audioDevice);
    }
    SDL_DestroyTexture(screenTexture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    std::cout << "Universal Retro Emulator exited cleanly.\n";
    return 0;
}
