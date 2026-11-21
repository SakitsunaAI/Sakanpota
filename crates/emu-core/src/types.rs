use std::fmt;
use std::path::Path;

pub const CANVAS_WIDTH: usize = 640;
pub const CANVAS_HEIGHT: usize = 512;
pub const AUDIO_SAMPLE_RATE: usize = 44_100;

/// Structured error type across all LLE crates.
#[derive(Debug)]
pub enum EmuError {
    IoError(String, std::io::Error),
    MissingRom(String),
    InvalidRomSize {
        name: String,
        expected: usize,
        actual: usize,
    },
    UnsupportedMedia(String),
    ArchiveError(String),
    ExecutionFault(String),
    FrontendError(String),
}

impl fmt::Display for EmuError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            EmuError::IoError(ctx, err) => write!(f, "I/O error ({ctx}): {err}"),
            EmuError::MissingRom(name) => write!(f, "Missing required hardware ROM: {name}"),
            EmuError::InvalidRomSize {
                name,
                expected,
                actual,
            } => write!(
                f,
                "Invalid ROM size for {name}: expected {expected} bytes, got {actual}"
            ),
            EmuError::UnsupportedMedia(msg) => write!(f, "Unsupported media: {msg}"),
            EmuError::ArchiveError(msg) => write!(f, "Archive error: {msg}"),
            EmuError::ExecutionFault(msg) => write!(f, "Execution fault: {msg}"),
            EmuError::FrontendError(msg) => write!(f, "Frontend error: {msg}"),
        }
    }
}

impl std::error::Error for EmuError {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            EmuError::IoError(_, err) => Some(err),
            _ => None,
        }
    }
}

pub type EmuResult<T> = Result<T, EmuError>;

/// Retained, genuinely LLE-emulated hardware platforms.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum SystemType {
    BbcMicro,
    Commodore64,
    Atari2600,
    OricAtmos,
    ZxSpectrum48k,
    AmstradCpc464,
    JupiterAce,
    SharpMz700,
    CamputersLynx,
    Ti99_4A,
    IbmPc5150,
    Microvision,
    SegaVmu,
    PocketStation,
}

impl SystemType {
    /// Ordered list of all 14 retained LLE systems.
    pub const ALL: [SystemType; 14] = [
        SystemType::BbcMicro,
        SystemType::Commodore64,
        SystemType::Atari2600,
        SystemType::OricAtmos,
        SystemType::ZxSpectrum48k,
        SystemType::AmstradCpc464,
        SystemType::JupiterAce,
        SystemType::SharpMz700,
        SystemType::CamputersLynx,
        SystemType::Ti99_4A,
        SystemType::IbmPc5150,
        SystemType::Microvision,
        SystemType::SegaVmu,
        SystemType::PocketStation,
    ];

    pub const fn display_name(self) -> &'static str {
        match self {
            SystemType::BbcMicro => "BBC Micro",
            SystemType::Commodore64 => "Commodore 64 (PAL)",
            SystemType::Atari2600 => "Atari 2600 VCS",
            SystemType::OricAtmos => "Oric Atmos 48K",
            SystemType::ZxSpectrum48k => "Sinclair ZX Spectrum 48K",
            SystemType::AmstradCpc464 => "Amstrad CPC 464",
            SystemType::JupiterAce => "Jupiter Ace",
            SystemType::SharpMz700 => "Sharp MZ-700",
            SystemType::CamputersLynx => "Camputers Lynx 48K",
            SystemType::Ti99_4A => "Texas Instruments TI-99/4A",
            SystemType::IbmPc5150 => "IBM PC 5150",
            SystemType::Microvision => "MB Microvision (1979)",
            SystemType::SegaVmu => "Sega Dreamcast VMU",
            SystemType::PocketStation => "Sony PocketStation",
        }
    }

    pub const fn cli_id(self) -> &'static str {
        match self {
            SystemType::BbcMicro => "bbc",
            SystemType::Commodore64 => "c64",
            SystemType::Atari2600 => "atari",
            SystemType::OricAtmos => "oric",
            SystemType::ZxSpectrum48k => "spectrum",
            SystemType::AmstradCpc464 => "cpc",
            SystemType::JupiterAce => "ace",
            SystemType::SharpMz700 => "sharpmz",
            SystemType::CamputersLynx => "clynx",
            SystemType::Ti99_4A => "ti99",
            SystemType::IbmPc5150 => "dos",
            SystemType::Microvision => "microvision",
            SystemType::SegaVmu => "vmu",
            SystemType::PocketStation => "pocket",
        }
    }

    pub fn next(self) -> SystemType {
        let idx = Self::ALL.iter().position(|&s| s == self).unwrap_or(0);
        Self::ALL[(idx + 1) % Self::ALL.len()]
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum BbcModel {
    ModelA,
    ModelB,
    ModelBPlus,
    Master128,
}

impl BbcModel {
    pub const fn display_name(self) -> &'static str {
        match self {
            BbcModel::ModelA => "BBC Micro Model A (16KB)",
            BbcModel::ModelB => "BBC Micro Model B (32KB)",
            BbcModel::ModelBPlus => "BBC Micro Model B+ (64KB/128KB)",
            BbcModel::Master128 => "BBC Master 128 (65C02 128KB)",
        }
    }
}

/// Architecture-independent keyboard key representation.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum KeyCode {
    A,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,
    Num0,
    Num1,
    Num2,
    Num3,
    Num4,
    Num5,
    Num6,
    Num7,
    Num8,
    Num9,
    Minus,
    Equals,
    LeftBracket,
    RightBracket,
    Backslash,
    Semicolon,
    Apostrophe,
    Comma,
    Period,
    Slash,
    Backquote,
    Space,
    Return,
    Escape,
    Backspace,
    Tab,
    CapsLock,
    Delete,
    Insert,
    Home,
    End,
    PageUp,
    PageDown,
    Up,
    Down,
    Left,
    Right,
    LShift,
    RShift,
    LCtrl,
    RCtrl,
    LAlt,
    RAlt,
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
    Unknown,
}

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct KeyMod {
    pub shift: bool,
    pub ctrl: bool,
    pub alt: bool,
}

/// Live CPU state snapshot for the debugger and telemetry views.
#[derive(Debug, Clone)]
pub struct CpuDebugState {
    pub arch: &'static str,
    pub pc: u32,
    pub sp: u32,
    pub flags: String,
    pub registers: Vec<(String, String)>,
    pub cycles: u64,
    pub halted: bool,
}

/// Single disassembled instruction returned by a live CPU core.
#[derive(Debug, Clone)]
pub struct DisassembledInstruction {
    pub address: u32,
    pub bytes: Vec<u8>,
    pub mnemonic: String,
}

/// 8-bit physical memory bus interface (16-bit address).
pub trait Bus8 {
    fn read8(&mut self, addr: u16) -> u8;
    fn write8(&mut self, addr: u16, val: u8);
    /// Side-effect-free read for debugging/disassembly.
    fn peek8(&self, addr: u16) -> u8;
}

/// 32-bit physical address memory bus interface (for 8086 20-bit, S1C88 24-bit, ARM7 32-bit, TMS9900 16-bit word/byte).
pub trait Bus32 {
    fn read8(&mut self, addr: u32) -> u8;
    fn write8(&mut self, addr: u32, val: u8);
    fn peek8(&self, addr: u32) -> u8;

    fn read16_le(&mut self, addr: u32) -> u16 {
        let lo = self.read8(addr) as u16;
        let hi = self.read8(addr.wrapping_add(1)) as u16;
        lo | (hi << 8)
    }

    fn write16_le(&mut self, addr: u32, val: u16) {
        self.write8(addr, (val & 0xFF) as u8);
        self.write8(addr.wrapping_add(1), (val >> 8) as u8);
    }

    fn read32_le(&mut self, addr: u32) -> u32 {
        let lo = self.read16_le(addr) as u32;
        let hi = self.read16_le(addr.wrapping_add(2)) as u32;
        lo | (hi << 16)
    }

    fn write32_le(&mut self, addr: u32, val: u32) {
        self.write16_le(addr, (val & 0xFFFF) as u16);
        self.write16_le(addr.wrapping_add(2), (val >> 16) as u16);
    }
}

/// 8-bit port I/O interface (used by Z80 and x86).
pub trait IoPortBus {
    fn port_in8(&mut self, port: u16) -> u8;
    fn port_out8(&mut self, port: u16, val: u8);
    fn port_in16(&mut self, port: u16) -> u16 {
        let lo = self.port_in8(port) as u16;
        let hi = self.port_in8(port.wrapping_add(1)) as u16;
        lo | (hi << 8)
    }
    fn port_out16(&mut self, port: u16, val: u16) {
        self.port_out8(port, (val & 0xFF) as u8);
        self.port_out8(port.wrapping_add(1), (val >> 8) as u8);
    }
}

/// Unified Low-Level Emulator machine interface wired to the frontend and debugger.
pub trait EmulatorCore {
    fn system_type(&self) -> SystemType;
    fn system_name(&self) -> &str;
    fn media_name(&self) -> &str;

    fn reset(&mut self);
    fn run_frame(&mut self);
    fn step_instruction(&mut self) -> u32;

    /// 640x512 ARGB8888 output framebuffer.
    fn rendered_screen(&self) -> &[u32];

    /// 44.1 kHz 16-bit mono PCM audio buffer for the most recent frame.
    fn audio_buffer(&self) -> &[i16];

    fn handle_key_down(&mut self, key: KeyCode, mods: KeyMod);
    fn handle_key_up(&mut self, key: KeyCode, mods: KeyMod);

    fn handle_mouse_motion(&mut self, _x: i32, _y: i32) {}
    fn handle_mouse_button(&mut self, _button: u8, _pressed: bool, _x: i32, _y: i32) {}
    fn handle_mouse_wheel(&mut self, _delta_y: i32) {}

    fn load_media(&mut self, path: &Path) -> EmuResult<()>;

    /// Optional cartridge/title cycling for multi-ROM systems (e.g. Microvision, Pokemon Mini).
    fn cycle_media(&mut self) -> EmuResult<()> {
        Ok(())
    }

    fn cpu_state(&self) -> CpuDebugState;
    fn inspect_memory(&self, start_addr: u32, len: usize) -> Vec<u8>;
    fn disassemble(&self, start_addr: u32, count: usize) -> Vec<DisassembledInstruction>;
}
