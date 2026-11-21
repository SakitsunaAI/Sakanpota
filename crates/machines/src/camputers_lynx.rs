use cpu_cores::Z80;
use emu_core::archive::load_rom_file;
use emu_core::types::{
    Bus8, CpuDebugState, DisassembledInstruction, EmuError, EmuResult, EmulatorCore, IoPortBus,
    KeyCode, KeyMod, SystemType, AUDIO_SAMPLE_RATE, CANVAS_HEIGHT, CANVAS_WIDTH,
};
use peripherals::Crtc6845;
use std::fs;
use std::path::Path;

struct CamputersLynxBus {
    rom: Vec<u8>,        // 0x0000..0x5FFF (lynx48 = 16KB at 0..0x4000)
    main_ram: Vec<u8>,   // main 64K bank 0
    vram_red: Vec<u8>,   // 0xA000..0xBFFF
    vram_blue: Vec<u8>,  // 0xC000..0xDFFF
    vram_green: Vec<u8>, // 0xA000..0xBFFF (when bank_reg bit 2 is set)
    bank_reg: u8,            // port 0x7FFF
    port80: u8,
    dac_val: u8,
    crtc: Crtc6845,
    key_rows: [u8; 10], // active low
    audio_buf: Vec<i16>,
    audio_accum: f64,
    media_name: String,
}

impl CamputersLynxBus {
    fn new(rom_dir: &Path) -> EmuResult<Self> {
        let bytes = load_rom_file(rom_dir, "camputers_lynx48.rom")
            .or_else(|_| load_rom_file(rom_dir, "lynx48-1.rom"))?;
        let mut rom = vec![0xFFu8; 24_576];
        rom[..bytes.len().min(24_576)].copy_from_slice(&bytes[..bytes.len().min(24_576)]);
        if bytes.len() <= 8192 {
            if let Ok(b2) = load_rom_file(rom_dir, "lynx48-2.rom") {
                rom[8192..8192 + b2.len().min(8192)].copy_from_slice(&b2[..b2.len().min(8192)]);
            }
        }
        Ok(Self {
            rom,
            main_ram: vec![0; 65_536],
            vram_red: vec![0; 8192],
            vram_blue: vec![0; 8192],
            vram_green: vec![0; 8192],
            bank_reg: 0,
            port80: 0,
            dac_val: 0,
            crtc: Crtc6845::new(),
            key_rows: [0xFF; 10],
            audio_buf: Vec::with_capacity(1024),
            audio_accum: 0.0,
            media_name: "Camputers Lynx 48K BASIC".to_string(),
        })
    }

    fn reset_state(&mut self) {
        self.main_ram.fill(0);
        self.vram_red.fill(0);
        self.vram_blue.fill(0);
        self.vram_green.fill(0);
        self.bank_reg = 0;
        self.port80 = 0;
        self.dac_val = 0;
        self.crtc.reset();
        self.crtc.regs[1] = 32;
        self.crtc.regs[6] = 31;
        self.crtc.regs[9] = 7;
        self.crtc.regs[12] = 0;
        self.crtc.regs[13] = 0;
    }

    fn render_screen(&self, out: &mut [u32]) {
        out.fill(0xFF00_0000);
        // 256x248 -> 2x vertical & 2x horizontal = 512x496 centered in 640x512
        let x0 = (CANVAS_WIDTH - 512) / 2;
        let y0 = (CANVAS_HEIGHT - 496) / 2;
        let start = self.crtc.start_addr() as usize;

        for y in 0..248usize {
            let row_addr = (start * 8 + y * 32) & 0x1FFF;
            let sy = y0 + y * 2;
            for col in 0..32usize {
                let off = (row_addr + col) & 0x1FFF;
                let r_b = self.vram_red[off];
                let g_b = self.vram_green[off];
                let b_b = self.vram_blue[off];
                for bit in 0..8usize {
                    let mask = 0x80 >> bit;
                    let r = if (r_b & mask) != 0 { 0xFF } else { 0 };
                    let g = if (g_b & mask) != 0 { 0xFF } else { 0 };
                    let b = if (b_b & mask) != 0 { 0xFF } else { 0 };
                    let rgb = 0xFF00_0000 | (r << 16) | (g << 8) | b;
                    let sx = x0 + (col * 8 + bit) * 2;
                    out[sy * CANVAS_WIDTH + sx] = rgb;
                    out[sy * CANVAS_WIDTH + sx + 1] = rgb;
                    out[(sy + 1) * CANVAS_WIDTH + sx] = rgb;
                    out[(sy + 1) * CANVAS_WIDTH + sx + 1] = rgb;
                }
            }
        }
    }
}

impl Bus8 for CamputersLynxBus {
    fn read8(&mut self, addr: u16) -> u8 {
        self.peek8(addr)
    }

    fn write8(&mut self, addr: u16, val: u8) {
        if addr < 0x6000 {
            return;
        }
        if addr >= 0x8000 {
            if (self.bank_reg & 0x02) != 0 {
                if addr < 0xC000 {
                    self.vram_blue[(addr & 0x1FFF) as usize] = val;
                } else {
                    self.vram_red[(addr & 0x1FFF) as usize] = val;
                }
                return;
            } else if (self.bank_reg & 0x04) != 0 {
                self.vram_green[(addr & 0x1FFF) as usize] = val;
                return;
            }
        }
        if (0x6000..=0x9FFF).contains(&addr) {
            self.main_ram[addr as usize] = val;
        }
    }

    fn peek8(&self, addr: u16) -> u8 {
        if addr < 0x6000 {
            return self.rom[addr as usize];
        }
        if addr >= 0x8000 {
            if (self.bank_reg & 0x02) != 0 {
                return if addr < 0xC000 {
                    self.vram_blue[(addr & 0x1FFF) as usize]
                } else {
                    self.vram_red[(addr & 0x1FFF) as usize]
                };
            } else if (self.bank_reg & 0x04) != 0 {
                return self.vram_green[(addr & 0x1FFF) as usize];
            }
        }
        if (0x6000..=0x9FFF).contains(&addr) {
            self.main_ram[addr as usize]
        } else {
            0xFF
        }
    }
}

impl IoPortBus for CamputersLynxBus {
    fn port_in8(&mut self, port: u16) -> u8 {
        match port & 0xFF {
            0x80 => {
                let row = ((port >> 8) & 0x0F) as usize;
                if row < 10 { self.key_rows[row] } else { 0xFF }
            }
            0x87 => self.crtc.read(true),
            _ => 0xFF,
        }
    }

    fn port_out8(&mut self, port: u16, val: u8) {
        if (port & 0xFF) == 0x7F || port == 0x7FFF || port == 0xFFFF {
            self.bank_reg = val;
            return;
        }
        match port & 0xFF {
            0x80 => self.port80 = val,
            0x84 => self.dac_val = val & 0x3F,
            0x86 => self.crtc.write(false, val),
            0x87 => self.crtc.write(true, val),
            _ => {}
        }
    }
}

pub struct CamputersLynx {
    cpu: Z80,
    bus: CamputersLynxBus,
    screen: Vec<u32>,
}

impl CamputersLynx {
    pub fn new(rom_dir: &Path) -> EmuResult<Self> {
        let mut bus = CamputersLynxBus::new(rom_dir)?;
        bus.reset_state();
        let cpu = Z80::new();
        let mut m = Self {
            cpu,
            bus,
            screen: vec![0xFF00_0000; CANVAS_WIDTH * CANVAS_HEIGHT],
        };
        for _ in 0..35 {
            m.run_frame();
        }
        Ok(m)
    }

    fn map_key(key: KeyCode) -> Option<(usize, u8)> {
        Some(match key {
            KeyCode::Num1 => (0, 0x01),
            KeyCode::Num2 => (1, 0x01),
            KeyCode::Num3 => (2, 0x01),
            KeyCode::Num4 => (3, 0x01),
            KeyCode::Num5 => (4, 0x01),
            KeyCode::Num6 => (5, 0x01),
            KeyCode::Num7 => (6, 0x01),
            KeyCode::Num8 => (7, 0x01),
            KeyCode::Num9 => (8, 0x01),
            KeyCode::Num0 => (9, 0x01),
            KeyCode::Q => (0, 0x02),
            KeyCode::W => (1, 0x02),
            KeyCode::E => (2, 0x02),
            KeyCode::R => (3, 0x02),
            KeyCode::T => (4, 0x02),
            KeyCode::Y => (5, 0x02),
            KeyCode::U => (6, 0x02),
            KeyCode::I => (7, 0x02),
            KeyCode::O => (8, 0x02),
            KeyCode::P => (9, 0x02),
            KeyCode::A => (0, 0x04),
            KeyCode::S => (1, 0x04),
            KeyCode::D => (2, 0x04),
            KeyCode::F => (3, 0x04),
            KeyCode::G => (4, 0x04),
            KeyCode::H => (5, 0x04),
            KeyCode::J => (6, 0x04),
            KeyCode::K => (7, 0x04),
            KeyCode::L => (8, 0x04),
            KeyCode::Return => (9, 0x08),
            KeyCode::Z => (0, 0x08),
            KeyCode::X => (1, 0x08),
            KeyCode::C => (2, 0x08),
            KeyCode::V => (3, 0x08),
            KeyCode::B => (4, 0x08),
            KeyCode::N => (5, 0x08),
            KeyCode::M => (6, 0x08),
            KeyCode::Space => (4, 0x10),
            KeyCode::LShift | KeyCode::RShift => (0, 0x80),
            KeyCode::Backspace => (9, 0x04),
            _ => return None,
        })
    }
}

impl EmulatorCore for CamputersLynx {
    fn system_type(&self) -> SystemType {
        SystemType::CamputersLynx
    }

    fn system_name(&self) -> &str {
        "Camputers Lynx 48K (Z80 + CRTC)"
    }

    fn media_name(&self) -> &str {
        &self.bus.media_name
    }

    fn reset(&mut self) {
        self.bus.reset_state();
        self.cpu.reset();
        for _ in 0..35 {
            self.run_frame();
        }
    }

    fn run_frame(&mut self) {
        self.bus.audio_buf.clear();
        let mut cyc = 0u32;
        while cyc < 80_000 {
            let c = self.cpu.step(&mut self.bus);
            cyc += c;
            self.bus.audio_accum += (c as f64 / 4_000_000.0) * AUDIO_SAMPLE_RATE as f64;
            while self.bus.audio_accum >= 1.0 {
                self.bus.audio_accum -= 1.0;
                let sample = ((self.bus.dac_val as i16) - 32) * 256;
                self.bus.audio_buf.push(sample);
            }
        }
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
        if let Some((r, m)) = Self::map_key(key) {
            self.bus.key_rows[r] &= !m;
        }
    }

    fn handle_key_up(&mut self, key: KeyCode, _mods: KeyMod) {
        if let Some((r, m)) = Self::map_key(key) {
            self.bus.key_rows[r] |= m;
        }
    }

    fn load_media(&mut self, path: &Path) -> EmuResult<()> {
        let bytes = fs::read(path).map_err(|e| EmuError::IoError(path.display().to_string(), e))?;
        let name = path
            .file_name()
            .and_then(|s| s.to_str())
            .unwrap_or("lynx48.rom")
            .to_string();
        if bytes.len() <= 24_576 {
            self.bus.rom.fill(0xFF);
            self.bus.rom[..bytes.len()].copy_from_slice(&bytes);
            self.bus.media_name = name;
            self.reset();
            return Ok(());
        }
        Err(EmuError::UnsupportedMedia(format!("Unsupported Lynx media: {name}")))
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
