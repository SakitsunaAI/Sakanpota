use cpu_cores::Z80;
use emu_core::archive::load_rom_file;
use emu_core::types::{
    Bus8, CpuDebugState, DisassembledInstruction, EmuError, EmuResult, EmulatorCore, IoPortBus,
    KeyCode, KeyMod, SystemType, AUDIO_SAMPLE_RATE, CANVAS_HEIGHT, CANVAS_WIDTH,
};
use std::fs;
use std::path::Path;

struct JupiterAceBus {
    rom: Vec<u8>,
    vram: [u8; 1024],
    char_ram: [u8; 1024],
    work_ram: Vec<u8>,
    speaker: bool,
    key_rows: [u8; 8],
    audio_buf: Vec<i16>,
    audio_accum: f64,
    media_name: String,
}

impl JupiterAceBus {
    fn new(rom_dir: &Path) -> EmuResult<Self> {
        let bytes = load_rom_file(rom_dir, "jupiter_ace.rom")?;
        let mut rom = vec![0u8; 8192];
        let len = bytes.len().min(8192);
        rom[..len].copy_from_slice(&bytes[..len]);
        Ok(Self {
            rom,
            vram: [b' '; 1024],
            char_ram: [0; 1024],
            work_ram: vec![0; 49_152],
            speaker: false,
            key_rows: [0x1F; 8],
            audio_buf: Vec::with_capacity(1024),
            audio_accum: 0.0,
            media_name: "Jupiter Ace Forth 48K".to_string(),
        })
    }

    fn reset_state(&mut self) {
        self.vram.fill(b' ');
        self.char_ram.fill(0);
        self.work_ram.fill(0);
        self.speaker = false;
    }

    fn render_screen(&self, out: &mut [u32]) {
        let fg = 0xFFFF_FFFFu32;
        let bg = 0xFF00_0000u32;
        out.fill(bg);

        let scale = 2usize;
        let x0 = (CANVAS_WIDTH - 256 * scale) / 2;
        let y0 = (CANVAS_HEIGHT - 192 * scale) / 2;

        for row in 0..24usize {
            for col in 0..32usize {
                let raw = self.vram[row * 32 + col];
                let inverse = (raw & 0x80) != 0;
                let ch = (raw & 0x7F) as usize;
                for cy in 0..8usize {
                    let bits = self.char_ram[ch * 8 + cy];
                    let sy = y0 + (row * 8 + cy) * scale;
                    for cx in 0..8usize {
                        let bit_set = (bits & (0x80 >> cx)) != 0;
                        let col_rgb = if bit_set ^ inverse { fg } else { bg };
                        let sx = x0 + (col * 8 + cx) * scale;
                        out[sy * CANVAS_WIDTH + sx] = col_rgb;
                        out[sy * CANVAS_WIDTH + sx + 1] = col_rgb;
                        out[(sy + 1) * CANVAS_WIDTH + sx] = col_rgb;
                        out[(sy + 1) * CANVAS_WIDTH + sx + 1] = col_rgb;
                    }
                }
            }
        }
    }
}

impl Bus8 for JupiterAceBus {
    fn read8(&mut self, addr: u16) -> u8 {
        self.peek8(addr)
    }

    fn write8(&mut self, addr: u16, val: u8) {
        match addr {
            0x2000..=0x27FF => self.vram[(addr & 0x3FF) as usize] = val,
            0x2800..=0x2FFF => self.char_ram[(addr & 0x3FF) as usize] = val,
            0x3000..=0x3FFF => self.work_ram[(addr & 0x3FF) as usize] = val,
            0x4000..=0xFFFF => self.work_ram[(addr - 0x4000) as usize] = val,
            _ => {}
        }
    }

    fn peek8(&self, addr: u16) -> u8 {
        match addr {
            0x0000..=0x1FFF => self.rom[addr as usize],
            0x2000..=0x27FF => self.vram[(addr & 0x3FF) as usize],
            0x2800..=0x2BFF => 0xFF,
            0x2C00..=0x2FFF => self.char_ram[(addr & 0x3FF) as usize],
            0x3000..=0x3FFF => self.work_ram[(addr & 0x3FF) as usize],
            0x4000..=0xFFFF => self.work_ram[(addr - 0x4000) as usize],
        }
    }
}

impl IoPortBus for JupiterAceBus {
    fn port_in8(&mut self, port: u16) -> u8 {
        if (port & 0xFF) == 0xFE {
            self.speaker = false;
            let hi = (port >> 8) as u8;
            let mut keys = 0x1Fu8;
            for (row, &mask) in self.key_rows.iter().enumerate() {
                if (hi & (1 << row)) == 0 {
                    keys &= mask;
                }
            }
            0xE0 | keys
        } else {
            0xFF
        }
    }

    fn port_out8(&mut self, port: u16, _val: u8) {
        if (port & 0xFF) == 0xFE {
            self.speaker = true;
        }
    }
}

pub struct JupiterAce {
    cpu: Z80,
    bus: JupiterAceBus,
    screen: Vec<u32>,
}

impl JupiterAce {
    pub fn new(rom_dir: &Path) -> EmuResult<Self> {
        let bus = JupiterAceBus::new(rom_dir)?;
        let cpu = Z80::new();
        Ok(Self {
            cpu,
            bus,
            screen: vec![0xFF00_0000; CANVAS_WIDTH * CANVAS_HEIGHT],
        })
    }

    fn map_key(key: KeyCode) -> Option<(usize, u8)> {
        Some(match key {
            KeyCode::LShift => (0, 0x01),
            KeyCode::RShift | KeyCode::LCtrl => (0, 0x02),
            KeyCode::Z => (0, 0x04),
            KeyCode::X => (0, 0x08),
            KeyCode::C => (0, 0x10),
            KeyCode::A => (1, 0x01),
            KeyCode::S => (1, 0x02),
            KeyCode::D => (1, 0x04),
            KeyCode::F => (1, 0x08),
            KeyCode::G => (1, 0x10),
            KeyCode::Q => (2, 0x01),
            KeyCode::W => (2, 0x02),
            KeyCode::E => (2, 0x04),
            KeyCode::R => (2, 0x08),
            KeyCode::T => (2, 0x10),
            KeyCode::Num1 => (3, 0x01),
            KeyCode::Num2 => (3, 0x02),
            KeyCode::Num3 => (3, 0x04),
            KeyCode::Num4 => (3, 0x08),
            KeyCode::Num5 => (3, 0x10),
            KeyCode::Num0 => (4, 0x01),
            KeyCode::Num9 => (4, 0x02),
            KeyCode::Num8 => (4, 0x04),
            KeyCode::Num7 => (4, 0x08),
            KeyCode::Num6 => (4, 0x10),
            KeyCode::P => (5, 0x01),
            KeyCode::O => (5, 0x02),
            KeyCode::I => (5, 0x04),
            KeyCode::U => (5, 0x08),
            KeyCode::Y => (5, 0x10),
            KeyCode::Return => (6, 0x01),
            KeyCode::L => (6, 0x02),
            KeyCode::K => (6, 0x04),
            KeyCode::J => (6, 0x08),
            KeyCode::H => (6, 0x10),
            KeyCode::Space => (7, 0x01),
            KeyCode::M => (7, 0x02),
            KeyCode::N => (7, 0x04),
            KeyCode::B => (7, 0x08),
            KeyCode::V => (7, 0x10),
            _ => return None,
        })
    }
}

impl EmulatorCore for JupiterAce {
    fn system_type(&self) -> SystemType {
        SystemType::JupiterAce
    }

    fn system_name(&self) -> &str {
        "Jupiter Ace (Z80 Forth)"
    }

    fn media_name(&self) -> &str {
        &self.bus.media_name
    }

    fn reset(&mut self) {
        self.bus.reset_state();
        self.cpu.reset();
    }

    fn run_frame(&mut self) {
        self.bus.audio_buf.clear();
        let mut cyc = 0u32;
        while cyc < 65_000 {
            let c = self.cpu.step(&mut self.bus);
            cyc += c;
            self.bus.audio_accum += (c as f64 / 3_250_000.0) * AUDIO_SAMPLE_RATE as f64;
            while self.bus.audio_accum >= 1.0 {
                self.bus.audio_accum -= 1.0;
                self.bus.audio_buf.push(if self.bus.speaker { 4500 } else { -4500 });
            }
        }
        self.cpu.request_irq(0xFF);
        self.bus.render_screen(&mut self.screen);
    }

    fn step_instruction(&mut self) -> u32 {
        let c = self.cpu.step(&mut self.bus);
        self.bus.render_screen(&mut self.screen);
        c
    }

    fn rendered_screen(&self) -> &[u32] {
        &self.screen
    }

    fn audio_buffer(&self) -> &[i16] {
        &self.bus.audio_buf
    }

    fn handle_key_down(&mut self, key: KeyCode, _mods: KeyMod) {
        if let Some((r, mask)) = Self::map_key(key) {
            self.bus.key_rows[r] &= !mask;
        }
    }

    fn handle_key_up(&mut self, key: KeyCode, _mods: KeyMod) {
        if let Some((r, mask)) = Self::map_key(key) {
            self.bus.key_rows[r] |= mask;
        }
    }

    fn load_media(&mut self, path: &Path) -> EmuResult<()> {
        let bytes = fs::read(path).map_err(|e| EmuError::IoError(path.display().to_string(), e))?;
        let name = path
            .file_name()
            .and_then(|s| s.to_str())
            .unwrap_or("ace")
            .to_string();
        if bytes.len() <= 8192 && path.extension().and_then(|s| s.to_str()) == Some("rom") {
            self.bus.rom.fill(0);
            self.bus.rom[..bytes.len()].copy_from_slice(&bytes);
            self.bus.media_name = name;
            self.reset();
            return Ok(());
        }
        Err(EmuError::UnsupportedMedia(format!("Unsupported Jupiter Ace media: {name}")))
    }

    fn cpu_state(&self) -> CpuDebugState {
        self.cpu.debug_state()
    }

    fn inspect_memory(&self, start_addr: u32, len: usize) -> Vec<u8> {
        (0..len)
            .map(|i| self.bus.peek8(start_addr.wrapping_add(i as u32) as u16))
            .collect()
    }

    fn disassemble(&self, start_addr: u32, count: usize) -> Vec<DisassembledInstruction> {
        self.cpu.disassemble(&self.bus, start_addr as u16, count)
    }
}
