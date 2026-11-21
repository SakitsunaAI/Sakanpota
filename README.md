# Sakanpota — Multi-System Low-Level Hardware Emulator (Rust + SDL2)

**Sakanpota** is a pure Rust, cycle-stepped **Low-Level Emulation (LLE)** suite supporting **14 hardware platforms** (including all **4 Acorn BBC Micro models**). Every retained system executes genuine, unmodified firmware, BIOS, mask ROM, or cartridge machine code through real CPU instruction cycles, physical memory maps, timers, interrupt controllers, audio chips, and framebuffer/CRTC/PPU pipelines—with **zero HLE OS/BIOS intercepts**, **zero UI mockups**, and **zero `todo!()`/`unimplemented!()` stubs**.

---

## Workspace Architecture

The project is organized as a modular Cargo workspace:

| Crate | Path | Responsibility |
| :--- | :--- | :--- |
| **`emu-core`** | `crates/emu-core/` | Core traits (`Bus8`, `Bus32`, `IoPortBus`, `EmulatorCore`), structured `EmuError`/`EmuResult<T>`, media detector, ZIP loader, CRT phosphor filter, and bitmap font + live debugger renderer (`gui_font.rs`) |
| **`cpu-cores`** | `crates/cpu-cores/` | Pure LLE CPU instruction interpreters & live disassemblers: `Mos6502` (NMOS 6502 + CMOS 65C02), `Z80`, `I8086` (8086/8088), `Tms9900`, `Tms1100`, `Lc8670`, `Arm7tdmi` |
| **`peripherals`** | `crates/peripherals/` | Register/signal-level chip models: `Via6522`, `Cia6526`, `Vic6569`, `Sid6581`, `Crtc6845`, `Saa5050`, `Sn76489`, `Ay8910`, `Tms9918`, `Pia8255`, `Pic8259`, `Pit8253`, and `fdc` (`I8271`, `Wd1770`, `Upd765`, `FloppyImage`) |
| **`machines`** | `crates/machines/` | Complete motherboard buses, memory mappers, clock dividers, input matrices, and machine factory (`create_machine`, `create_machine_for_media`) for all 14 LLE platforms |
| **`sakanpota`** | `crates/sakanpota/` | SDL2 video/audio frontend, drag-and-drop media loader, headless `--verify-all` verifier, and interactive live LLE debugger (`F10`) |

---

## Retained Systems (14 Authentic LLE Platforms)

| CLI ID | Platform | CPU Core | Hardware Video / Audio / I/O |
| :--- | :--- | :--- | :--- |
| `bbc` | **BBC Micro (Model A, B, B+, Master 128)** | MOS 6502 / WDC 65C02 | CRTC 6845, SAA5050 Teletext, Video ULA, dual 6522 VIA, SN76489, 8271 / WD1770 FDC, MC146818 CMOS RTC (Master 128) |
| `c64` | **Commodore 64 (PAL)** | MOS 6502 (6510 I/O port) | VIC-II 6569, SID 6581, dual CIA 6526, color RAM, Kernal/BASIC/Chargen ROMs |
| `atari` | **Atari 2600 VCS** | MOS 6502 (6507 13-bit bus) | TIA scanline renderer + audio, RIOT 6532 timer & switches |
| `oric` | **Oric Atmos 48K** | MOS 6502 | ULA text/HIRES/attrib renderer, 6522 VIA, AY-3-8912 PSG |
| `spectrum` | **Sinclair ZX Spectrum 48K** | Zilog Z80A | ULA `$4000-$5AFF` pixel+attribute renderer, 50 Hz IM 1 IRQ, beeper & `$FE` keyboard matrix |
| `cpc` | **Amstrad CPC 464** | Zilog Z80A | Gate Array ROM/mode banking, CRTC 6845, PPI 8255, AY-3-8912 PSG |
| `ace` | **Jupiter Ace** | Zilog Z80A | 24x32 tilemap & character RAM, Forth ROM + 48KB expansion RAM, speaker toggle |
| `sharpmz` | **Sharp MZ-700** | Zilog Z80A | 1Z-013A Monitor ROM, hardware CGROM (`mz700fon.int`), `$D000/$D800` text+attribute VRAM, 8255 PPI, 8253 PIT |
| `clynx` | **Camputers Lynx 48K** | Zilog Z80A | `0x7F`/`0x7FFF` RGB VRAM bank switching, CRTC 6845, 6-bit DAC audio, keyboard matrix |
| `ti99` | **Texas Instruments TI-99/4A** | TI TMS9900 (16-bit) | TMS9918A VDP, SN76489 PSG, 18KB/24KB GROM interface, scratchpad RAM, CRU I/O |
| `dos` | **IBM PC 5150** | Intel 8088 / 8086 | 640KB RAM, IBM 5150 BIOS U33 + Cassette BASIC C1.00, 8259 PIC, 8253 PIT, 8255 PPI, 8237 DMA, CGA/CRTC 6845, uPD765 FDC |
| `microvision` | **MB Microvision (1979)** | TI TMS1100 (4-bit) | Hughes 0488 16x16 LCD segment matrix, rotary paddle knob + keypad |
| `vmu` | **Sega Dreamcast VMU** | Sanyo LC8670 (Potato) | 48x32 LCD + icons, 64KB BIOS + 128KB Flash, mode/sleep/D-pad controls |
| `pocket` | **Sony PocketStation** | ARM7TDMI (ARM) | 32x32 LCD controller, 16KB PK201 kernel ROM (`pocketstation.bin`), BCD RTC, timers & interrupts |

---

## Purged Systems (Un-Emulatable / Mockup Legacy Devices Removed)

During the Phase 1 LLE audit, systems that relied on C++ HLE mockups, fantasy VMs without a real hardware ISA, closed proprietary SoCs without obtainable firmware, or unbundled/missing system disks/controller ROMs were completely removed from the repository:

- **Fantasy / Non-Hardware VMs**: `PICO-8` (proprietary Lua 5.2 fantasy VM with hardcoded C++ game clones and synthetic cart stubs).
- **Closed Proprietary Hardware / Encrypted Firmware**: `Flipper Zero` (STM32WB55RG dual-core + BLE/Sub-GHz/NFC/RFID stack with zero firmware ROM), `Dreamware Enterprises VM2` & `8BitMods VMU Pro` (closed commercial STM32/ESP32-S3 hardware), `Apple Newton MessagePad` (requires undocumented Runt/Voyager ASICs and tablet ADC/calibration EEPROMs).
- **Orphan Mobile / Cellular Mockups**: `Motorola RAZR2 V9`, `Nokia 3310`, `Luxury Mobile`, `Symbian S60`, `Cybiko` (closed proprietary baseband/application SoCs with fake `pc += 4` loops and corrupted/absent firmware).
- **Unbundled / Missing Required Controller ROMs or Boot Disks**: `Apple Lisa 2/5` (missing COPS421 MCU ROM & ProFile/Twiggy Lisa OS disks), `Apple Macintosh 128K` (no Macintosh System 1.0/Finder 400K GCR boot disk), `Sinclair QL` (missing Intel 8049 IPC mask ROM), `Atari Lynx` (missing `.lnx` game cartridge ROMs), `Magnavox Odyssey²` (no cartridge ROMs in repository).

---

## Building, Running & Testing

### Prerequisites
- Rust toolchain (`cargo` / `rustc`, Edition 2021)
- SDL2 (`sdl2` installed at `/opt/local` on macOS or system library path on Linux)
- External system ROM/BIOS files placed in `roms/` (git-ignored), or specified via `--rom-dir <path>` / `SAKANPOTA_ROM_DIR=<path>` (no ROMs are embedded in the repository or binary)

### Build Release Binary
```bash
cargo build --release
```
Convenience symlinks `./sakanpota` and `./bbc_micro` at the repository root point to `target/release/sakanpota`.

### Launch Any System or Media File
```bash
# Default BBC Micro (Model B) or specific model
./sakanpota
./sakanpota --bbc master

# Launch any retained system by CLI ID
./sakanpota -s c64
./sakanpota -s dos
./sakanpota -s ti99
./sakanpota -s microvision

# Auto-detect and boot a cartridge, tape, or disk image (or drag & drop onto window)
./sakanpota discs/elite.ssd
./sakanpota roms/atari_pitfall.bin
```

### Headless LLE Boot & Integration Verification
```bash
# Headless 17-target live CPU + framebuffer verification (all 14 systems + 4 BBC models)
./sakanpota --verify-all

# Workspace unit & integration test suite (CPU, peripherals, boot, reset, media autodetect)
cargo test --workspace --release

# Strict Clippy static analysis
cargo clippy --workspace --all-targets -- -D warnings
```

---

## Global Hotkeys & Live LLE Debugger

- **`F10`**: Toggle Live LLE Debugger Overlay (live CPU registers, flags, cycle counter, PC disassembly, and memory hex dump)
  - **`F1`** *(debugger open)*: Pause / Resume live execution
  - **`F2`** *(debugger open)*: Single-step one CPU instruction
  - **`F3`** *(debugger open)*: Run exactly one video frame
  - **`PageUp` / `PageDown`** *(debugger open)*: Scroll memory inspector by `0x100` bytes
- **`F12`**: Cycle through all 14 LLE systems
- **`F11`**: Cycle media / cartridge (where supported, e.g., Microvision, BBC Micro discs)
- **`F7`**: Cycle BBC Micro model (`Model A` → `Model B` → `Model B+` → `Master 128`) when running BBC Micro
- **`F8`**: Toggle CRT scanline/curvature filter
- **`F9`**: Cycle CRT phosphor mode (Color / Green / Amber / Grayscale)
- **`F5`**: Hardware reset active system
