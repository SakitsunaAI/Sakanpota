use cpu_cores::Z80;
use emu_core::archive::{load_rom_file, read_media_or_zip, read_zip_entries};
use emu_core::types::{
    Bus8, CpuDebugState, DisassembledInstruction, EmuError, EmuResult, EmulatorCore, IoPortBus,
    KeyCode, KeyMod, SystemType, AUDIO_SAMPLE_RATE, CANVAS_HEIGHT, CANVAS_WIDTH,
};
use peripherals::{Pia8255, Pit8253};
use std::path::Path;

struct SharpMzBus {
    ram: Vec<u8>,
    vram: [u8; 4096],
    monitor_rom: [u8; 4096],
    cg_rom: [u8; 4096],
    lo_ram_mapped: bool,
    hi_ram_mapped: bool,
    inhibit_hi: bool,
    pia: Pia8255,
    pit: Pit8253,
    key_rows: [u8; 10], // active low
    vblank: bool,
    cursor_blink: bool,
    sound_gate: bool,
    speaker_out: bool,
    audio_buf: Vec<i16>,
    audio_accum: f64,
    pit_accum: f64,
    frame_cnt: u32,
    media_name: String,
}

impl SharpMzBus {
    const COLORS: [u32; 8] = [
        0xFF00_0000, // 0 Black
        0xFF00_00FF, // 1 Blue
        0xFFFF_0000, // 2 Red
        0xFFFF_00FF, // 3 Magenta
        0xFF00_FF00, // 4 Green
        0xFF00_FFFF, // 5 Cyan
        0xFFFF_FF00, // 6 Yellow
        0xFFFF_FFFF, // 7 White
    ];

    fn new(rom_dir: &Path) -> EmuResult<Self> {
        let mon = Self::load_component(rom_dir, "1z-013a.rom")?;
        let fon = Self::load_component(rom_dir, "mz700fon.int")?;
        let mut monitor_rom = [0u8; 4096];
        let mut cg_rom = [0u8; 4096];
        monitor_rom[..mon.len().min(4096)].copy_from_slice(&mon[..mon.len().min(4096)]);
        cg_rom[..fon.len().min(4096)].copy_from_slice(&fon[..fon.len().min(4096)]);
        Ok(Self {
            ram: vec![0; 65_536],
            vram: [0; 4096],
            monitor_rom,
            cg_rom,
            lo_ram_mapped: false,
            hi_ram_mapped: false,
            inhibit_hi: false,
            pia: Pia8255::new(),
            pit: Pit8253::new(),
            key_rows: [0xFF; 10],
            vblank: false,
            cursor_blink: false,
            sound_gate: false,
            speaker_out: false,
            audio_buf: Vec::with_capacity(1024),
            audio_accum: 0.0,
            pit_accum: 0.0,
            frame_cnt: 0,
            media_name: "Sharp MZ-700 1Z-013A".to_string(),
        })
    }

    fn load_component(rom_dir: &Path, name: &str) -> EmuResult<Vec<u8>> {
        if let Ok(b) = load_rom_file(rom_dir, name) {
            return Ok(b);
        }
        let zip_bytes = load_rom_file(rom_dir, "mz700.zip")?;
        let entries = read_zip_entries(&zip_bytes)?;
        entries
            .into_iter()
            .find(|e| e.name.to_ascii_lowercase().ends_with(name))
            .map(|e| e.data)
            .ok_or_else(|| EmuError::MissingRom(name.to_string()))
    }

    fn reset_state(&mut self) {
        self.ram.fill(0);
        self.vram.fill(0);
        self.lo_ram_mapped = false;
        self.hi_ram_mapped = false;
        self.inhibit_hi = false;
        self.pia.reset();
        self.pit.reset();
        self.pia.port_b_in = 0xFF;
    }

    fn update_pia_inputs(&mut self) {
        let row = (self.pia.port_a_out & 0x0F) as usize;
        self.pia.port_b_in = if row < 10 { self.key_rows[row] } else { 0xFF };
        self.pia.port_c_in = (if !self.vblank { 0x80 } else { 0x00 }) | 0x20;
    }

    fn render_screen(&self, out: &mut [u32]) {
        out.fill(0xFF00_0000);
        // 320x200 -> 2x = 640x400 centered vertically in 640x512
        let x0 = 0usize;
        let y0 = (CANVAS_HEIGHT - 400) / 2;

        for row in 0..25usize {
            for col in 0..40usize {
                let offset = row * 40 + col;
                let ch = self.vram[offset] as usize;
                let attr = self.vram[0x0800 + offset];
                let fg = Self::COLORS[((attr >> 4) & 7) as usize];
                let bg = Self::COLORS[(attr & 7) as usize];
                let alt = if (attr & 0x80) != 0 { 2048 } else { 0 };
                for cy in 0..8usize {
                    let bits = self.cg_rom[(alt + ch * 8 + cy) & 0xFFF];
                    let sy = y0 + (row * 8 + cy) * 2;
                    for cx in 0..8usize {
                        let c = if (bits & (1 << cx)) != 0 { fg } else { bg };
                        let sx = x0 + (col * 8 + cx) * 2;
                        out[sy * CANVAS_WIDTH + sx] = c;
                        out[sy * CANVAS_WIDTH + sx + 1] = c;
                        out[(sy + 1) * CANVAS_WIDTH + sx] = c;
                        out[(sy + 1) * CANVAS_WIDTH + sx + 1] = c;
                    }
                }
            }
        }
    }
}

impl Bus8 for SharpMzBus {
    fn read8(&mut self, addr: u16) -> u8 {
        if addr < 0x1000 && !self.lo_ram_mapped {
            return self.monitor_rom[addr as usize];
        }
        if !self.hi_ram_mapped && !self.inhibit_hi {
            match addr {
                0xD000..=0xDFFF => return self.vram[(addr - 0xD000) as usize],
                0xE000..=0xE003 => {
                    self.update_pia_inputs();
                    return self.pia.read((addr & 3) as u8);
                }
                0xE004..=0xE007 => return self.pit.read((addr & 3) as u8),
                0xE008 => {
                    let clk = if self.cursor_blink { 0x01 } else { 0x00 };
                    return 0x7E | clk;
                }
                _ => {}
            }
        }
        self.ram[addr as usize]
    }

    fn write8(&mut self, addr: u16, val: u8) {
        if addr < 0x1000 && !self.lo_ram_mapped {
            return;
        }
        if !self.hi_ram_mapped && !self.inhibit_hi {
            match addr {
                0xD000..=0xDFFF => {
                    self.vram[(addr - 0xD000) as usize] = val;
                    return;
                }
                0xE000..=0xE003 => {
                    self.pia.write((addr & 3) as u8, val);
                    if (addr & 3) == 2 || (addr & 3) == 3 {
                        self.sound_gate = (self.pia.port_c_out & 0x01) != 0;
                        self.pit.channels[0].gate = self.sound_gate;
                    }
                    self.update_pia_inputs();
                    return;
                }
                0xE004..=0xE007 => {
                    self.pit.write((addr & 3) as u8, val);
                    return;
                }
                0xE008 => {
                    self.sound_gate = (val & 0x01) != 0;
                    self.pit.channels[0].gate = self.sound_gate;
                    return;
                }
                _ => {}
            }
        }
        self.ram[addr as usize] = val;
    }

    fn peek8(&self, addr: u16) -> u8 {
        if addr < 0x1000 && !self.lo_ram_mapped {
            return self.monitor_rom[addr as usize];
        }
        if !self.hi_ram_mapped && !self.inhibit_hi {
            match addr {
                0xD000..=0xDFFF => return self.vram[(addr - 0xD000) as usize],
                0xE000..=0xE003 => return self.pia.read((addr & 3) as u8),
                _ => {}
            }
        }
        self.ram[addr as usize]
    }
}

impl IoPortBus for SharpMzBus {
    fn port_in8(&mut self, _port: u16) -> u8 {
        0xFF
    }

    fn port_out8(&mut self, port: u16, _val: u8) {
        match port & 0xFF {
            0xE0 => self.lo_ram_mapped = true,
            0xE1 => self.hi_ram_mapped = true,
            0xE2 => self.lo_ram_mapped = false,
            0xE3 => self.hi_ram_mapped = false,
            0xE4 => {
                self.lo_ram_mapped = false;
                self.hi_ram_mapped = false;
                self.inhibit_hi = false;
            }
            0xE5 => self.inhibit_hi = true,
            0xE6 => self.inhibit_hi = false,
            _ => {}
        }
    }
}

pub struct SharpMz700 {
    cpu: Z80,
    bus: SharpMzBus,
    screen: Vec<u32>,
    total_cycles: u64,
}

impl SharpMz700 {
    pub fn new(rom_dir: &Path) -> EmuResult<Self> {
        let mut bus = SharpMzBus::new(rom_dir)?;
        bus.reset_state();
        let cpu = Z80::new();
        Ok(Self {
            cpu,
            bus,
            screen: vec![0xFF00_0000; CANVAS_WIDTH * CANVAS_HEIGHT],
            total_cycles: 0,
        })
    }

    fn map_key(key: KeyCode) -> Option<(usize, u8)> {
        Some(match key {
            KeyCode::Return => (0, 0x01),
            KeyCode::Semicolon => (0, 0x02),
            KeyCode::LeftBracket => (0, 0x08),
            KeyCode::RightBracket => (0, 0x10),
            KeyCode::Equals => (0, 0x80),
            KeyCode::A => (4, 0x80),
            KeyCode::B => (4, 0x40),
            KeyCode::C => (4, 0x20),
            KeyCode::D => (4, 0x10),
            KeyCode::E => (4, 0x08),
            KeyCode::F => (4, 0x04),
            KeyCode::G => (4, 0x02),
            KeyCode::H => (4, 0x01),
            KeyCode::I => (3, 0x80),
            KeyCode::J => (3, 0x40),
            KeyCode::K => (3, 0x20),
            KeyCode::L => (3, 0x10),
            KeyCode::M => (3, 0x08),
            KeyCode::N => (3, 0x04),
            KeyCode::O => (3, 0x02),
            KeyCode::P => (3, 0x01),
            KeyCode::Q => (2, 0x80),
            KeyCode::R => (2, 0x40),
            KeyCode::S => (2, 0x20),
            KeyCode::T => (2, 0x10),
            KeyCode::U => (2, 0x08),
            KeyCode::V => (2, 0x04),
            KeyCode::W => (2, 0x02),
            KeyCode::X => (2, 0x01),
            KeyCode::Y => (1, 0x80),
            KeyCode::Z => (1, 0x40),
            KeyCode::Num1 => (5, 0x80),
            KeyCode::Num2 => (5, 0x40),
            KeyCode::Num3 => (5, 0x20),
            KeyCode::Num4 => (5, 0x10),
            KeyCode::Num5 => (5, 0x08),
            KeyCode::Num6 => (5, 0x04),
            KeyCode::Num7 => (5, 0x02),
            KeyCode::Num8 => (5, 0x01),
            KeyCode::Num9 => (6, 0x80),
            KeyCode::Num0 => (6, 0x40),
            KeyCode::Space => (6, 0x02),
            KeyCode::Backspace => (7, 0x02),
            KeyCode::Up => (7, 0x20),
            KeyCode::Down => (7, 0x10),
            KeyCode::Right => (7, 0x08),
            KeyCode::Left => (7, 0x04),
            KeyCode::LShift | KeyCode::RShift => (8, 0x80),
            KeyCode::LCtrl => (8, 0x40),
            _ => return None,
        })
    }
}

impl EmulatorCore for SharpMz700 {
    fn system_type(&self) -> SystemType {
        SystemType::SharpMz700
    }

    fn system_name(&self) -> &str {
        "Sharp MZ-700 (Z80 + 8255 + 8253)"
    }

    fn media_name(&self) -> &str {
        &self.bus.media_name
    }

    fn reset(&mut self) {
        self.bus.reset_state();
        self.cpu.reset();
        self.total_cycles = 0;
    }

    fn run_frame(&mut self) {
        self.bus.audio_buf.clear();
        let mut cyc = 0u32;
        while cyc < 71_600 {
            self.bus.vblank = cyc >= 58_000;
            let c = self.cpu.step(&mut self.bus);
            cyc += c;
            self.total_cycles += c as u64;
            self.bus.pit_accum += (c as f64 / 3_580_000.0) * 894_886.0;
            let pit_ticks = self.bus.pit_accum as u32;
            self.bus.pit_accum -= pit_ticks as f64;
            if pit_ticks > 0 {
                let (irq_edges, _) = self.bus.pit.tick(pit_ticks);
                self.bus.speaker_out = self.bus.pit.channels[0].out && self.bus.sound_gate;
                if irq_edges > 0 {
                    self.cpu.request_irq(0xFF);
                }
            }
            self.bus.audio_accum += (c as f64 / 3_580_000.0) * AUDIO_SAMPLE_RATE as f64;
            while self.bus.audio_accum >= 1.0 {
                self.bus.audio_accum -= 1.0;
                self.bus.audio_buf.push(if self.bus.speaker_out { 4500 } else { -4500 });
            }
        }
        self.bus.frame_cnt = self.bus.frame_cnt.wrapping_add(1);
        self.bus.cursor_blink = ((self.bus.frame_cnt / 25) & 1) == 0;
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
            self.bus.update_pia_inputs();
        }
    }

    fn handle_key_up(&mut self, key: KeyCode, _mods: KeyMod) {
        if let Some((r, m)) = Self::map_key(key) {
            self.bus.key_rows[r] |= m;
            self.bus.update_pia_inputs();
        }
    }

    fn load_media(&mut self, path: &Path) -> EmuResult<()> {
        let (name, ext, bytes) = read_media_or_zip(path, &["mzf", "mzt"])?;
        if (ext == "mzf" || ext == "mzt") && bytes.len() >= 128 {
            while self.total_cycles < 200_000 {
                self.run_frame();
            }
            // Populate Sharp MZ-700 1Z-013A Monitor tape header workspace ($10F0..$1170).
            self.bus.ram[0x10F0..0x1170].copy_from_slice(&bytes[..128]);
            let size = u16::from_le_bytes([bytes[0x12], bytes[0x13]]) as usize;
            let load_addr = u16::from_le_bytes([bytes[0x14], bytes[0x15]]) as usize;
            let exec_addr = u16::from_le_bytes([bytes[0x16], bytes[0x17]]);
            let payload = &bytes[128..];
            let copy_len = size
                .min(payload.len())
                .min(self.bus.ram.len().saturating_sub(load_addr));
            self.bus.ram[load_addr..load_addr + copy_len].copy_from_slice(&payload[..copy_len]);
            self.bus.lo_ram_mapped = true;
            self.cpu.pc = exec_addr;
            self.bus.media_name = name;
            for _ in 0..10 {
                self.run_frame();
            }
            return Ok(());
        }
        Err(EmuError::UnsupportedMedia(format!(
            "Unsupported MZ-700 media: {name}"
        )))
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
