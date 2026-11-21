use std::path::Path;

use cpu_cores::{Tms1100, Tms1100Io};
use emu_core::archive::load_rom_file;
use emu_core::{
    CANVAS_HEIGHT, CANVAS_WIDTH, CpuDebugState, DisassembledInstruction, EmuError, EmuResult,
    EmulatorCore, KeyCode, KeyMod, SystemType,
};

const CARTS: [&str; 4] = [
    "microvision/block.bin",
    "microvision/bowling.bin",
    "microvision/connect4.bin",
    "microvision/pinball.bin",
];

struct MicrovisionIo {
    // 16x16 Hughes 0488 LCD shift register & active display latches
    shift_reg: u32,
    shift_nibbles: u8,
    col_latch: u16,
    row_latch: u16,
    pla_rev_rows: bool,
    lcd: [[bool; 16]; 16],
    last_o: u8,
    last_r: u16,
    // 3x4 keypad (rows 0..3 x cols 0..2), active-high to K1/K2/K4/K8
    keys: [[bool; 3]; 4],
    paddle_pos: u8, // 0..255 rotary paddle resistance
    audio_buf: Vec<i16>,
    last_spk_cycle: u64,
    spk_state: bool,
}

impl Default for MicrovisionIo {
    fn default() -> Self {
        Self {
            shift_reg: 0,
            shift_nibbles: 0,
            col_latch: 0,
            row_latch: 0,
            pla_rev_rows: false,
            lcd: [[false; 16]; 16],
            last_o: 0,
            last_r: 0,
            keys: [[false; 3]; 4],
            paddle_pos: 128,
            audio_buf: Vec::with_capacity(735),
            last_spk_cycle: 0,
            spk_state: false,
        }
    }
}

impl MicrovisionIo {
    fn reset(&mut self) {
        self.shift_reg = 0;
        self.shift_nibbles = 0;
        self.col_latch = 0;
        self.row_latch = 0;
        self.lcd = [[false; 16]; 16];
        self.last_o = 0;
        self.last_r = 0;
        self.keys = [[false; 3]; 4];
        self.last_spk_cycle = 0;
        self.spk_state = false;
    }

    fn latch_lcd(&mut self) {
        // Hughes 0488 32-bit serial shift register:
        // Upper 4 nibbles (n0..n3) drive rows (PLA 0 uses little-endian nibble order; PLA 1 uses big-endian);
        // lower 4 nibbles (n4..n7) drive columns (MSB-left).
        let n0 = ((self.shift_reg >> 28) & 0xF) as u16;
        let n1 = ((self.shift_reg >> 24) & 0xF) as u16;
        let n2 = ((self.shift_reg >> 20) & 0xF) as u16;
        let n3 = ((self.shift_reg >> 16) & 0xF) as u16;
        let n4 = ((self.shift_reg >> 12) & 0xF) as u16;
        let n5 = ((self.shift_reg >> 8) & 0xF) as u16;
        let n6 = ((self.shift_reg >> 4) & 0xF) as u16;
        let n7 = (self.shift_reg & 0xF) as u16;

        let rows = if self.pla_rev_rows {
            (n3 << 12) | (n2 << 8) | (n1 << 4) | n0
        } else {
            (n0 << 12) | (n1 << 8) | (n2 << 4) | n3
        };
        let cols = (n4 << 12) | (n5 << 8) | (n6 << 4) | n7;
        self.col_latch = cols;
        self.row_latch = rows;
        for r_bit in 0..16usize {
            if ((rows >> r_bit) & 1) != 0 {
                let row = 15 - r_bit;
                for c_bit in 0..16usize {
                    let col = 15 - c_bit;
                    self.lcd[row][col] = ((cols >> c_bit) & 1) != 0;
                }
            }
        }
    }
}

impl Tms1100Io for MicrovisionIo {
    fn write_o(&mut self, o_val: u8, _cycles: u64) {
        self.last_o = o_val;
    }

    fn write_r(&mut self, r_latch: u16, _cycles: u64) {
        let rising = !self.last_r & r_latch;
        // R7 rising edge (!DATA_CLK) clocks 4-bit data from O0..O3 into Hughes 0488 shift register
        if (rising & 0x0080) != 0 {
            self.shift_reg = (self.shift_reg << 4) | ((self.last_o & 0x0F) as u32);
            self.shift_nibbles = self.shift_nibbles.saturating_add(1);
        }
        // R6 rising edge (LATCH_PULSE) commits frame when !DC is high, otherwise polarity handshake
        if (rising & 0x0040) != 0 {
            if (r_latch & 0x0080) != 0 && self.shift_nibbles >= 8 {
                self.latch_lcd();
            }
            self.shift_nibbles = 0;
            self.shift_reg = 0;
        }
        // Differential piezo buzzer on R0/R1 (standard) or R4/R5
        let spk = (((r_latch & 0x0001) != 0) ^ ((r_latch & 0x0002) != 0))
            || (((r_latch & 0x0010) != 0) ^ ((r_latch & 0x0020) != 0));
        self.spk_state = spk;
        self.last_r = r_latch;
    }

    fn read_k(&self, r_latch: u16, _cycles: u64) -> u8 {
        let mut k = 0u8;
        // Columns scanned on R8, R9, R10 (and fallback R0..R2)
        for col in 0..3usize {
            let active = (r_latch & (1u16 << (8 + col))) != 0 || (r_latch & (1u16 << col)) != 0;
            if active {
                for row in 0..4usize {
                    if self.keys[row][col] {
                        k |= 1u8 << row;
                    }
                }
            }
        }
        // Rotary paddle timing sense on R6/K8 when paddle pressed toward extreme
        if (r_latch & 0x0040) != 0 && self.paddle_pos > 192 {
            k |= 0x08;
        }
        k & 0x0F
    }
}

pub struct Microvision {
    cpu: Tms1100,
    io: MicrovisionIo,
    rom_dir: std::path::PathBuf,
    cart_idx: usize,
    media_name: String,
    screen: Vec<u32>,
}

impl Microvision {
    pub fn new(rom_dir: &Path) -> EmuResult<Self> {
        let mut m = Self {
            cpu: Tms1100::new(),
            io: MicrovisionIo::default(),
            rom_dir: rom_dir.to_path_buf(),
            cart_idx: 0,
            media_name: String::new(),
            screen: vec![0xFF7B_8569; CANVAS_WIDTH * CANVAS_HEIGHT],
        };
        m.load_cart_index(0)?;
        Ok(m)
    }

    fn crc32(data: &[u8]) -> u32 {
        let mut crc = 0xFFFF_FFFFu32;
        for &b in data {
            crc ^= b as u32;
            for _ in 0..8 {
                let mask = (crc & 1).wrapping_neg();
                crc = (crc >> 1) ^ (0xEDB8_8320 & mask);
            }
        }
        !crc
    }

    fn rom_uses_pla0(name: &str, data: &[u8]) -> bool {
        let crc = Self::crc32(data);
        if matches!(crc, 0xFD18_EA6D | 0x9065_44EA) {
            return true;
        }
        if matches!(crc, 0x460A_C30E | 0x6A4C_F60B) {
            return false;
        }
        let lower = name.to_ascii_lowercase();
        lower.contains("block") || lower.contains("pinball")
    }

    fn load_cart_index(&mut self, idx: usize) -> EmuResult<()> {
        let rel = CARTS[idx % CARTS.len()];
        let data = load_rom_file(&self.rom_dir, rel)?;
        let name = rel
            .rsplit('/')
            .next()
            .unwrap_or("block.bin")
            .to_string();
        self.io.pla_rev_rows = Self::rom_uses_pla0(&name, &data);
        self.cpu.load_rom(&data);
        self.io.reset();
        self.cart_idx = idx % CARTS.len();
        self.media_name = name;
        Ok(())
    }

    fn render_to(io: &MicrovisionIo, fb: &mut [u32]) {
        fb.fill(0xFF4F_5845); // Olive Microvision cartridge bezel
        // Centered 16x16 LCD square (28 px per cell -> 448x448 inside 640x512)
        let cell = 28usize;
        let x0 = (CANVAS_WIDTH - 16 * cell) / 2;
        let y0 = (CANVAS_HEIGHT - 16 * cell) / 2;

        for row in 0..16usize {
            for col in 0..16usize {
                let on = io.lcd[row][col];
                let fg = if on { 0xFF1A_2018 } else { 0xFF98_A68B };
                let sx = x0 + col * cell;
                let sy = y0 + row * cell;
                for dy in 0..cell {
                    let border = dy == 0 || dy + 1 == cell;
                    for dx in 0..cell {
                        let grid = border || dx == 0 || dx + 1 == cell;
                        let c = if grid { 0xFF84_9178 } else { fg };
                        fb[(sy + dy) * CANVAS_WIDTH + (sx + dx)] = c;
                    }
                }
            }
        }
    }

    fn map_key(key: KeyCode) -> Option<(usize, usize)> {
        Some(match key {
            KeyCode::Num1 => (0, 0),
            KeyCode::Num2 | KeyCode::Up => (0, 1),
            KeyCode::Num3 => (0, 2),
            KeyCode::Num4 | KeyCode::Q => (1, 0),
            KeyCode::Num5 | KeyCode::W | KeyCode::Space | KeyCode::Return => (1, 1),
            KeyCode::Num6 | KeyCode::E => (1, 2),
            KeyCode::Num7 | KeyCode::A => (2, 0),
            KeyCode::Num8 | KeyCode::S | KeyCode::Down => (2, 1),
            KeyCode::Num9 | KeyCode::D => (2, 2),
            KeyCode::Z => (3, 0),
            KeyCode::Num0 | KeyCode::X => (3, 1),
            KeyCode::C => (3, 2),
            _ => return None,
        })
    }
}

impl EmulatorCore for Microvision {
    fn system_type(&self) -> SystemType {
        SystemType::Microvision
    }

    fn system_name(&self) -> &str {
        "Milton Bradley Microvision (TMS1100)"
    }

    fn media_name(&self) -> &str {
        &self.media_name
    }

    fn reset(&mut self) {
        self.cpu.reset();
        self.io.reset();
    }

    fn run_frame(&mut self) {
        self.io.audio_buf.clear();
        // TMS1100 @ ~500 kHz, 6 osc cycles/instr => ~8333 osc ticks (~1400 instr) per 60Hz frame
        let target_ticks = 8_333u32;
        let mut ticks = 0u32;
        let mut next_sample = 0u32;
        let cyc_per_sample = target_ticks / 735;

        while ticks < target_ticks {
            let c = self.cpu.step(&mut self.io).max(1);
            ticks += c;
            while next_sample <= ticks && self.io.audio_buf.len() < 735 {
                self.io
                    .audio_buf
                    .push(if self.io.spk_state { 4000 } else { 0 });
                next_sample += cyc_per_sample.max(1);
            }
        }
        Self::render_to(&self.io, &mut self.screen);
    }

    fn step_instruction(&mut self) -> u32 {
        let c = self.cpu.step(&mut self.io);
        Self::render_to(&self.io, &mut self.screen);
        c
    }

    fn rendered_screen(&self) -> &[u32] {
        &self.screen
    }

    fn audio_buffer(&self) -> &[i16] {
        &self.io.audio_buf
    }

    fn handle_key_down(&mut self, key: KeyCode, _mod_state: KeyMod) {
        match key {
            KeyCode::Left => {
                self.io.paddle_pos = self.io.paddle_pos.saturating_sub(16);
            }
            KeyCode::Right => {
                self.io.paddle_pos = self.io.paddle_pos.saturating_add(16);
            }
            _ => {
                if let Some((r, c)) = Self::map_key(key) {
                    self.io.keys[r][c] = true;
                }
            }
        }
    }

    fn handle_key_up(&mut self, key: KeyCode, _mod_state: KeyMod) {
        if let Some((r, c)) = Self::map_key(key) {
            self.io.keys[r][c] = false;
        }
    }

    fn load_media(&mut self, path: &Path) -> EmuResult<()> {
        let bytes = std::fs::read(path).map_err(|e| EmuError::IoError(path.display().to_string(), e))?;
        let name = path
            .file_name()
            .and_then(|s| s.to_str())
            .unwrap_or("cart.bin")
            .to_string();
        if bytes.is_empty() || bytes.len() > 2048 {
            return Err(EmuError::UnsupportedMedia(format!(
                "Expected 2KB TMS1100 Microvision ROM: {name}"
            )));
        }
        self.io.pla_rev_rows = Self::rom_uses_pla0(&name, &bytes);
        self.cpu.load_rom(&bytes);
        self.io.reset();
        self.media_name = name;
        Ok(())
    }

    fn cycle_media(&mut self) -> EmuResult<()> {
        let next = (self.cart_idx + 1) % CARTS.len();
        self.load_cart_index(next)
    }

    fn cpu_state(&self) -> CpuDebugState {
        self.cpu.debug_state()
    }

    fn inspect_memory(&self, start_addr: u32, len: usize) -> Vec<u8> {
        (0..len)
            .map(|i| {
                let a = start_addr.wrapping_add(i as u32) as usize;
                if a < 128 {
                    self.cpu.ram[a]
                } else if a < 2048 {
                    self.cpu.rom[a]
                } else {
                    0xFF
                }
            })
            .collect()
    }

    fn disassemble(&self, start_addr: u32, count: usize) -> Vec<DisassembledInstruction> {
        self.cpu.disassemble(start_addr, count)
    }
}
