# Sakanpota - Universal Multi-System Retro Computer, Console & Handheld Hardware Emulator (C++ & SDL2)

**Sakanpota** is an all-in-one, cycle-accurate multi-system retro emulator unifying the Acorn **BBC Micro** family, **IBM PC / MS-DOS**, **Commodore 64**, **Apple Macintosh 128K**, **Apple Lisa 2/5**, **Sony PocketStation (PK201 Core)**, **Atari 2600 VCS**, **Bandai Tamagotchi (P1 & P2)**, **Sega Dreamcast VMU**, **Watara Supervision**, **Texas Instruments Series (TI-99/4A & TI-83 Plus)**, **Jupiter Ace**, **Enterprise 64/128**, **Sharp MZ-700**, **Sinclair QL**, **Oric Atmos**, **Atari Lynx**, **Camputers Lynx**, **RCA Studio II**, **Nintendo Pokémon Mini**, **Milton Bradley Microvision (1979 - TMS1100 CPU)**, **Sinclair ZX Spectrum 48K**, **Amstrad CPC 464**, **Magnavox Odyssey² / Philips Videopac**, the **Apple Newton MessagePad** (ARM610 CPU, RUNT ASIC, touch digitizer), and the **Flipper Zero Portable Multi-Tool** (ST7567 amber LCD, piezo sound, Sub-GHz RF, RFID, NFC, IR, BadUSB, and FAP apps).

Built with a **pure cycle-accurate interpreter (NO JIT)** engine, Sakanpota contains zero fake UI mockups: each machine is faithfully simulated through its genuine CPU instruction set, video display controllers, timers, and audio synthesis hardware.

---

## 🌟 Key Highlights

1. **100% Automatic Platform & Media Detection (Zero Manual Setup):**
   - No need to select a machine or model beforehand.
   - Simply drag and drop any disk, tape, cartridge image, or executable file onto the emulator window. Sakanpota instantly analyzes file extensions and magic headers to identify the exact target platform, boots the corresponding machine, and mounts/launches the media.

2. **Comprehensive Subsystem & Hardware Architecture:**
   - Accurate simulation of authentic CPUs, memory mappers, custom ASICs, audio synthesizers, and display controllers across all 26 platforms.
   - High-precision timing, scanline-accurate beam rasterization, and hardware interrupt delivery.

3. **Pure Cycle-Accurate Interpreter Architecture (NO JIT):**
   - High-precision instruction interpreters without JIT overhead:
     - **MOS 6502 / WDC 65C02 / 65SC02:** BBC Micro, Atari 2600, Oric Atmos, Atari Lynx, Watara Supervision.
     - **MOS 6510:** Commodore 64.
     - **Zilog Z80 / Z80A:** Sinclair ZX Spectrum, Amstrad CPC 464, Jupiter Ace, Enterprise 64/128, Sharp MZ-700, Camputers Lynx, TI-83 Plus.
     - **TMS9900 (16-bit):** Texas Instruments TI-99/4A.
     - **Motorola 68000 / 68008:** Apple Macintosh 128K, Apple Lisa, Sinclair QL.
     - **Intel 8086 / Real Mode x86:** IBM PC / MS-DOS.
     - **Intel 8048 / 8244 VDC:** Magnavox Odyssey² / Philips Videopac.
     - **RCA CDP1802 COSMAC / CDP1861:** RCA Studio II.
     - **Epson Minx S1C88:** Nintendo Pokémon Mini (PokeMini core).
     - **ARM610 32-bit RISC (ARMv3):** Apple Newton MessagePad (OMP, 100, 110, 120, 130) with Apple RUNT ASIC and Sharp LCD controller.
     - **ARM7TDMI 32-bit RISC:** Sony PocketStation (PK201 core).
     - **Sanyo LC86104C (Potato 8-bit):** Sega Dreamcast VMU.
     - **Seiko Epson E0C6S46 (12-bit Core):** Bandai Tamagotchi P1 and P2.
     - **Texas Instruments TMS1100 (4-bit MCU):** Milton Bradley Microvision.

---

## ⚡ Platform Launch Commands

Launch any supported platform directly from the command line:

```bash
# 🎮 Handhelds & Virtual Pets
./sakanpota poke           # Nintendo Pokémon Mini ([TAB] Cycle Game, [S] Shake)
./sakanpota microvision    # Milton Bradley Microvision (1979 - TMS1100 CPU, Rotary Paddle Knob)
./sakanpota pocket         # Sony PocketStation (PK201 ARM7 Core, Chocobo World, Crash, etc.)
./sakanpota tamap1         # Tamagotchi P1 (1996 Generation 1 - Egg, Evolution, Feeding, Game)
./sakanpota tamap2         # Tamagotchi P2 (1997 Generation 2 - New patterns and characters)
./sakanpota vmu            # Sega Dreamcast VMU (Clock, VMS file manager and mini-game)
./sakanpota supervision    # Watara Supervision (65C02, 160x160 4-shade LCD, Crystball)
./sakanpota newton         # Apple Newton MessagePad (ARM610, RUNT ASIC, Stylus touch digitizer)
./sakanpota flipper        # Flipper Zero Portable Multi-Tool & Virtual Pet (Sub-GHz, RFID, NFC, IR, Apps)
./sakanpota flipper doom   # Flipper Zero Micro-Doom 3D Raycaster
./sakanpota flipper subghz # Flipper Zero Sub-GHz RF Analyzer & Spectrum Waterfall
./sakanpota flipper music  # Flipper Zero Chiptune Music Player

# 🕹️ Classic Consoles
./sakanpota studio2        # RCA Studio II (1977 - CDP1802 COSMAC)
./sakanpota odyssey2       # Magnavox Odyssey² / Videopac (Intel 8048, KC Munchkin & Pick Axe Pete)
./sakanpota atari          # Atari 2600 VCS (6507 + TIA)
./sakanpota lynx           # Atari Lynx (Mikey + Suzy 16-bit graphics engine)

# 🎨 Fantasy Consoles
./sakanpota pico8          # PICO-8 Fantasy Console (128x128 16-color, 4-channel sound synthesizer)
./sakanpota pico8 celeste  # Celeste (Mountain climbing platformer with air dash)
./sakanpota pico8 dusk     # Dusk Child (Puzzle exploration adventure)
./sakanpota pico8 maichan  # Mai-Chan's Sweet Bakery (Pastry match puzzle)
./sakanpota pico8 racers   # Pico Racers (Top-down drifting arcade racer)
./sakanpota pico8 mistigri # Mistigri (Cat puzzle platformer)

# 🖥️ Legendary Computers
./sakanpota bbc            # BBC Micro Model B (32KB, 6502, Acorn DFS)
./sakanpota c64            # Commodore 64 (PAL 6510, VIC-II, SID 6581)
./sakanpota spectrum       # Sinclair ZX Spectrum 48K
./sakanpota cpc            # Amstrad CPC 464
./sakanpota dos            # IBM PC / MS-DOS 6.22 (8086, CGA/VGA)
./sakanpota mac            # Apple Macintosh 128K (Motorola 68000, Mac OS System 1.0)
./sakanpota lisa           # Apple Lisa 2/5 (Motorola 68000, Lisa OS Desktop)
./sakanpota ace            # Jupiter Ace (Z80 Forth programming environment)
./sakanpota enterprise     # Enterprise 64/128 (EXOS 2.1 and IS-BASIC)
./sakanpota sharpmz        # Sharp MZ-700 / MZ-80K (Clean Computer Monitor)
./sakanpota ql             # Sinclair QL (Motorola 68008, QDOS and SuperBASIC)
./sakanpota oric           # Oric Atmos (6502A, Oric BASIC v1.1)
./sakanpota clynx          # Camputers Lynx (Camputers 48K/96K BASIC)

# 🧮 Texas Instruments Series
./sakanpota ti99           # Texas Instruments TI-99/4A (TMS9900, Color Splash & TI BASIC)
./sakanpota ti83           # TI-83 Plus Graphing Calculator (Z80, 96x64 LCD)
```

---

## 🎮 Device Controls & Key Combinations

### 📱 Apple Newton MessagePad (ARM610 / RUNT ASIC)
- **Stylus Pen / Touch Digitizer:** Click and drag with `Mouse` on the 240x336 reflective LCD screen to write, draw, or tap with the high-precision digitizer tablet ADC.
- **Silkscreen Buttons:** Click directly on Undo, Find, Assist, Overview, Up, and Down on the bottom silkscreen glass.
- **Power Switch:** Press `Space` or click the physical power slider on the upper right of the MessagePad casing.
- **Cycle Version:** Press `Tab` to toggle system versions.
- **Reset:** `F12` or `R`.

### 📱 Sony PocketStation (PK201 Core)
- **D-Pad:** `Arrow Keys` (Up, Down, Left, Right).
- **Action Button:** `Space`, `Z`, or `Enter`.
- **Cycle Game:** Press `Tab` to cycle between available titles.
- **Mouse Control:** You can also click directly on the interactive buttons on the device casing.

### 🕹️ Milton Bradley Microvision (1979 - TMS1100)
- **Rotary Paddle Knob:** `Left / Right Arrow Keys` or `Mouse Scroll Wheel`.
- **Action / Push:** `Space` or `Z`.
- **Keypad Matrix:** Keys `1..9, 0, -, =` trigger the 12-button keypad on the cartridge overlay.
- **Switch Cartridge:** `1` (*Block Buster*), `2` (*Bowling*), `3` (*Connect Four*), `4` (*Pinball*).

### ⚡ Nintendo Pokémon Mini
- **D-Pad:** `Arrow Keys` (Up, Down, Left, Right).
- **A Button:** `Z` or `Space`.
- **B Button:** `X` or `Backspace`.
- **C Button:** `C` key.
- **Game Selector:** Press `Tab` to cycle between available software titles.
- **Shake Sensor (Shock Detect):** Press `S` to simulate shaking the device (required in Pikachu's race, pinball tilt, dice rolls, etc.).

### 🕹️ Watara Supervision
- **D-Pad:** `Arrow Keys` (Up, Down, Left, Right).
- **A Button:** `Z` or `A`.
- **B Button:** `X` or `S`.
- **Select:** `Right Shift` or `Tab`.
- **Start:** `Enter` or `Space`.

### 🕹️ RCA Studio II (1977)
- **Left Keypad (Player 1):** Numeric keys `1..9, 0`.
- **Right Keypad (Player 2):** Keys `Q, W, E, A, S, D, Z, X, C`.
- **Game Selection:** At startup press `1` (*Doodle*), `2` (*Patterns*), `3` (*Bowling*), `4` (*Space War*), `5` (*Freeway*).

### 🕹️ Magnavox Odyssey² / Videopac
- **Game Selection:** `1` -> *K.C. Munchkin!*, `2` -> *Pick Axe Pete!*.
- **Joystick:** `Arrow Keys`.
- **Action Button:** `Space` or `Z`.

### 🐣 Tamagotchi (P1 & P2)
- **Button A (Left):** `Z`, `Left Arrow`, or `A` -> Cycle through icons.
- **Button B (Center):** `X`, `Enter`, or `B` -> Confirm / Feed / Play game / Administer medicine.
- **Button C (Right):** `C` or `ESC` -> Cancel / Return to main clock screen.

### 🎮 Sega Dreamcast VMU
- **D-Pad:** `Arrow Keys` (Up, Down, Left, Right) or on-screen directional pad.
- **A Button:** `Z`, `A`, or `Enter`.
- **B Button:** `X`, `S`, or `Space`.
- **Mode / Sleep:** `M` or `Tab`.
- **Cycle Titles:** `F2` or click top connector cap.
- **Mouse Control:** Click directly on casing buttons.

### 💎 8BitMods VMU Pro
- **Run Standalone:** `./sakanpota 8bitmods`
- **Run Specific Title:** `./sakanpota 8bitmods [chao | pinta | powerstone | jsr | taxi]`
- **Display:** 240x160 Full-Color Backlit IPS Matrix.
- **Memory Architecture:** 128 Flash Memory Banks (16MB Storage).
- **Controls:** `Arrow Keys` (D-Pad), `Z` / `A` (A Button), `X` / `S` (B Button), `M` / `Tab` (Mode), `B` / `P` (Bank Switch), `F2` (Cycle Titles).
- **Direct Touch/Mouse:** Interactive on-screen buttons, top cap, and mode switches.

### 🔷 Dreamware Enterprises VM2
- **Run Standalone:** `./sakanpota vmuvm2`
- **Run Specific Title:** `./sakanpota vmuvm2 [chao | skies | powerstone | sonic | space]`
- **Display:** High-Contrast Emissive OLED Matrix.
- **Memory Architecture:** 16 Virtual Pages (128KB Flash Pages).
- **Controls:** `Arrow Keys` (D-Pad), `Z` / `A` (A Button), `X` / `S` (B Button), `M` / `Tab` (Mode), `P` (Page Switch), `F2` (Cycle Titles).

### 🎨 PICO-8 Fantasy Console
- **Run Standalone:** `./sakanpota pico8`
- **Run Specific Title:** `./sakanpota pico8 [celeste | dusk | maichan | racers | mistigri]`
- **Display:** 128x128 16-Color Palette rasterizer scaled 3x on custom console casing.
- **Audio:** 4-channel chiptune synthesizer (8 waveforms, 8 note effects).
- **D-Pad:** `Arrow Keys` (Left, Right, Up, Down).
- **O Button (Button 4):** `Z`, `C`, or `N` (Jump / Select / Action).
- **X Button (Button 5):** `X`, `V`, or `M` (Dash / Cancel).
- **Pause / Menu:** `Enter` or `P`.
- **Cartridge Carousel / Shell Toggle:** `Tab`.

### 🐬 Flipper Zero Multi-Tool & Virtual Pet
- **Run Standalone:** `./sakanpota flipper`
- **Run Specific Tool / Game:** `./sakanpota flipper [doom | subghz | snake | tetris | music | nfc | rfid | ir | ibutton | badusb | spectrum]`
- **Display:** 128x64 ST7567 Monochrome Amber Backlit LCD.
- **Audio:** 44.1kHz PWM Piezo Buzzer hardware sound synthesizer.
- **LED Indicator:** Tri-color RGB Notification LED with dynamic pulse effects.
- **D-Pad Navigation:** `Arrow Keys` (Up, Down, Left, Right).
- **OK / Select Button:** `Enter` or `Z`.
- **Back / Cancel Button:** `Backspace` or `X`.
- **Physical Case Interaction:** Click directly with the `Mouse` on the orange 5-way directional pad or back button on the polycarbonate casing.
- **Direct File Drag & Drop:** Drop `.sub`, `.nfc`, `.rfid`, `.ir`, `.ibtn`, or `.fap` files onto the window to automatically inspect or run them.

### 🖥️ Global Sakanpota Hotkeys
- **Drag & Drop:** Drop any retro disk, cartridge, or tape file onto the window to automatically identify and run it.
- **Tab / F11:** Toggle on-screen display (HUD / OSD).
- **F5:** Load BBC Micro `discs/Welcome.ssd`.
- **F6:** Load BBC Micro `discs/elite.ssd`.
- **F7:** Cycle CRT monitor phosphor palette (RGB -> Green -> Amber -> B&W).
- **F8:** Toggle CRT scanlines and curved screen filter.
- **F9:** Toggle audio mute.
- **F12:** Reset current active system.

---

## 🚀 Building & Running

### Prerequisites
- macOS or Linux
- C++17 compliant compiler (`clang++` or `g++`)
- `SDL2` library (`brew install sdl2` on macOS or `sudo apt install libsdl2-dev` on Linux)
- `zlib` library (standard system library)

### Build
```bash
make clean && make -j4
```

### Run
```bash
# 8BitMods VMU Pro (Standalone or with game title):
./sakanpota 8bitmods
./sakanpota 8bitmods chao
./sakanpota 8bitmods jsr

# Dreamware VM2 (Standalone or with game title):
./sakanpota vmuvm2
./sakanpota vmuvm2 sonic

# Sony PocketStation (PK201 Core):
./sakanpota pocket

# Milton Bradley Microvision:
./sakanpota microvision

# Nintendo Pokémon Mini:
./sakanpota poke

# PICO-8 Fantasy Console (Standalone or with cartridge title):
./sakanpota pico8
./sakanpota pico8 celeste
./sakanpota pico8 dusk
./sakanpota pico8 maichan
./sakanpota pico8 racers
./sakanpota pico8 mistigri

# Flipper Zero Portable Multi-Tool:
./sakanpota flipper
./sakanpota flipper doom
./sakanpota flipper subghz
./sakanpota flipper music
./sakanpota flipper tetris

# Drag & drop or pass any media file via CLI:
./sakanpota AirLineBoss_LoftcatSoftware.ssd
./sakanpota game.sv
./sakanpota game.d64
./sakanpota cart.p8
```
