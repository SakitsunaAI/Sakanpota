use cpu_cores::Mos6502;
use emu_core::archive::{load_rom_file, read_media_or_zip};
use emu_core::types::{
    Bus8, CpuDebugState, DisassembledInstruction, EmuError, EmuResult, EmulatorCore, KeyCode,
    KeyMod, SystemType, AUDIO_SAMPLE_RATE, CANVAS_HEIGHT, CANVAS_WIDTH,
};
use peripherals::{Ay8910, Via6522};
use std::path::Path;

struct OricBus {
    ram: Vec<u8>,
    rom: Vec<u8>,
    via: Via6522,
    ay: Ay8910,
    key_matrix: [u8; 8], // 8 rows x 8 cols active high
    audio_buf: Vec<i16>,
    audio_accum: f64,
    media_name: String,
    frame_cnt: u32,
}

impl OricBus {
    const COLORS: [u32; 8] = [
        0xFF00_0000, // 0 Black
        0xFFFF_0000, // 1 Red
        0xFF00_FF00, // 2 Green
        0xFFFF_FF00, // 3 Yellow
        0xFF00_00FF, // 4 Blue
        0xFFFF_00FF, // 5 Magenta
        0xFF00_FFFF, // 6 Cyan
        0xFFFF_FFFF, // 7 White
    ];

    fn new(rom_dir: &Path) -> EmuResult<Self> {
        let rom_bytes = load_rom_file(rom_dir, "oric_atmos.rom")?;
        let mut rom = vec![0u8; 16_384];
        let len = rom_bytes.len().min(16_384);
        rom[..len].copy_from_slice(&rom_bytes[..len]);
        Ok(Self {
            ram: vec![0; 65_536],
            rom,
            via: Via6522::new(),
            ay: Ay8910::new(1_000_000.0),
            key_matrix: [0; 8],
            audio_buf: Vec::with_capacity(1024),
            audio_accum: 0.0,
            media_name: "Oric Atmos BASIC 1.1".to_string(),
            frame_cnt: 0,
        })
    }

    fn reset_state(&mut self) {
        self.ram.fill(0);
        self.via.reset();
        self.ay.reset();
        self.via.irb = 0xFF;
    }

    fn update_keyboard(&mut self) {
        let row = (self.via.orb & 0x07) as usize;
        let col_mask = !self.ay.regs[14];
        let pressed = (self.key_matrix[row] & col_mask) != 0;
        self.via.irb = if pressed { 0x08 } else { 0x00 };
    }

    fn sync_ay_from_via(&mut self) {
        let bc1 = self.via.ca2_out;
        let bdir = self.via.cb2_out;
        let pa = self.via.port_a_pins();
        if bdir && bc1 {
            self.ay.select_register(pa);
        } else if bdir && !bc1 {
            self.ay.write_data(pa);
            self.update_keyboard();
        } else if !bdir && bc1 {
            self.via.ira = self.ay.read_data();
        }
    }

    fn render_screen(&self, out: &mut [u32]) {
        out.fill(0xFF00_0000);
        let flash_off = ((self.frame_cnt / 24) & 1) != 0;
        let mut hires = false;
        let mut is_50hz = true;

        // First render HIRES lines 0..200 if HIRES or TEXT rows 0..28
        for row in 0..28usize {
            let mut fg = 7usize;
            let mut bg = 0usize;
            let mut alt_charset = false;
            let mut dbl_height = false;
            let mut blink = false;

            // Check attribute byte at end/start or track mode cleanly:
            for y_in_row in 0..8usize {
                let y = row * 8 + y_in_row;
                if hires && y < 200 {
                    // HIRES scanline: 40 bytes per line starting at 0xA000
                    let mut h_fg = 7usize;
                    let mut h_bg = 0usize;
                    let mut h_blink = false;
                    for col in 0..40usize {
                        let byte = self.ram[0xA000 + y * 40 + col];
                        let inv = (byte & 0x80) != 0;
                        let draw_bg = Self::COLORS[if inv { h_bg ^ 7 } else { h_bg }];
                        let draw_fg = Self::COLORS[if inv { h_fg ^ 7 } else { h_fg }];
                        if (byte & 0x60) == 0 {
                            match byte & 0x1F {
                                0x00..=0x07 => h_fg = (byte & 0x07) as usize,
                                0x08..=0x0F => h_blink = (byte & 0x04) != 0,
                                0x10..=0x17 => h_bg = (byte & 0x07) as usize,
                                0x18..=0x1B => hires = false,
                                0x1C..=0x1F => {
                                    hires = true;
                                    is_50hz = (byte & 0x02) == 0;
                                }
                                _ => {}
                            }
                            self.fill_oric_cell_pixels(out, col, y, 0x00, draw_fg, draw_bg);
                        } else {
                            let pat = if h_blink && flash_off { 0 } else { byte & 0x3F };
                            self.fill_oric_cell_pixels(out, col, y, pat, draw_fg, draw_bg);
                        }
                    }
                } else {
                    if y_in_row == 0 {
                        fg = 7;
                        bg = 0;
                        alt_charset = false;
                        dbl_height = false;
                        blink = false;
                    }
                    let mut t_fg = fg;
                    let mut t_bg = bg;
                    let mut t_alt = alt_charset;
                    let mut t_dbl = dbl_height;
                    let mut t_blink = blink;
                    for col in 0..40usize {
                        let ch = self.ram[0xBB80 + row * 40 + col];
                        let inv = (ch & 0x80) != 0;
                        let code = ch & 0x7F;
                        let draw_bg = Self::COLORS[if inv { t_bg ^ 7 } else { t_bg }];
                        let draw_fg = Self::COLORS[if inv { t_fg ^ 7 } else { t_fg }];
                        if code < 0x20 {
                            match code {
                                0x00..=0x07 => t_fg = (code & 7) as usize,
                                0x08..=0x0F => {
                                    t_alt = (code & 1) != 0;
                                    t_dbl = (code & 2) != 0;
                                    t_blink = (code & 4) != 0;
                                }
                                0x10..=0x17 => t_bg = (code & 7) as usize,
                                0x18..=0x1B => hires = false,
                                0x1C..=0x1F => {
                                    hires = true;
                                    is_50hz = (code & 2) == 0;
                                }
                                _ => {}
                            }
                            self.fill_oric_cell_pixels(out, col, y, 0x00, draw_fg, draw_bg);
                        } else {
                            let cbase = if t_alt { 0xB800 } else { 0xB400 };
                            let cy = if t_dbl { (y_in_row / 2) + if (row & 1) != 0 { 4 } else { 0 } } else { y_in_row };
                            let pat = if t_blink && flash_off {
                                0
                            } else {
                                self.ram[cbase + (code as usize) * 8 + cy] & 0x3F
                            };
                            self.fill_oric_cell_pixels(out, col, y, pat, draw_fg, draw_bg);
                        }
                    }
                }
            }
        }
        let _ = is_50hz;
    }

    #[inline]
    fn fill_oric_cell_pixels(&self, out: &mut [u32], col: usize, y: usize, pat6: u8, fg: u32, bg: u32) {
        // 240x224 -> scale 2x (480x448 centered in 640x512)
        let x_off = (CANVAS_WIDTH - 480) / 2;
        let y_off = (CANVAS_HEIGHT - 448) / 2;
        let sy = y_off + y * 2;
        if sy + 1 >= CANVAS_HEIGHT {
            return;
        }
        for px in 0..6usize {
            let color = if (pat6 & (0x20 >> px)) != 0 { fg } else { bg };
            let sx = x_off + (col * 6 + px) * 2;
            out[sy * CANVAS_WIDTH + sx] = color;
            out[sy * CANVAS_WIDTH + sx + 1] = color;
            out[(sy + 1) * CANVAS_WIDTH + sx] = color;
            out[(sy + 1) * CANVAS_WIDTH + sx + 1] = color;
        }
    }
}

impl Bus8 for OricBus {
    fn read8(&mut self, addr: u16) -> u8 {
        match addr {
            0x0300..=0x03FF => {
                if (addr & 0xF) == 1 || (addr & 0xF) == 0xF {
                    self.sync_ay_from_via();
                    self.update_keyboard();
                } else if (addr & 0xF) == 0 {
                    self.update_keyboard();
                }
                self.via.read((addr & 0x0F) as u8)
            }
            0x0000..=0xBFFF => self.ram[addr as usize],
            0xC000..=0xFFFF => self.rom[(addr - 0xC000) as usize],
        }
    }

    fn write8(&mut self, addr: u16, val: u8) {
        match addr {
            0x0300..=0x03FF => {
                let r = (addr & 0x0F) as u8;
                self.via.write(r, val);
                if r == 0 || r == 2 {
                    self.update_keyboard();
                }
                self.sync_ay_from_via();
            }
            0x0000..=0xBFFF => self.ram[addr as usize] = val,
            0xC000..=0xFFFF => {}
        }
    }

    fn peek8(&self, addr: u16) -> u8 {
        match addr {
            0x0300..=0x03FF => self.via.peek((addr & 0x0F) as u8),
            0x0000..=0xBFFF => self.ram[addr as usize],
            0xC000..=0xFFFF => self.rom[(addr - 0xC000) as usize],
        }
    }
}

pub struct OricAtmos {
    cpu: Mos6502,
    bus: OricBus,
    screen: Vec<u32>,
    total_cycles: u64,
}

impl OricAtmos {
    pub fn new(rom_dir: &Path) -> EmuResult<Self> {
        let mut bus = OricBus::new(rom_dir)?;
        bus.reset_state();
        let mut cpu = Mos6502::new(false);
        cpu.reset(&mut bus);
        let mut m = Self {
            cpu,
            bus,
            screen: vec![0xFF00_0000; CANVAS_WIDTH * CANVAS_HEIGHT],
            total_cycles: 0,
        };
        for _ in 0..115 {
            m.run_frame();
        }
        Ok(m)
    }

    fn map_key(key: KeyCode) -> Option<(usize, u8)> {
        Some(match key {
            KeyCode::Num7 => (0, 0x80),
            KeyCode::N => (0, 0x40),
            KeyCode::Num5 => (0, 0x20),
            KeyCode::V => (0, 0x10),
            KeyCode::Num1 => (0, 0x04),
            KeyCode::X => (0, 0x02),
            KeyCode::Num3 => (0, 0x01),
            KeyCode::J => (1, 0x80),
            KeyCode::T => (1, 0x40),
            KeyCode::R => (1, 0x20),
            KeyCode::F => (1, 0x10),
            KeyCode::Escape => (1, 0x04),
            KeyCode::Q => (1, 0x02),
            KeyCode::D => (1, 0x01),
            KeyCode::M => (2, 0x80),
            KeyCode::Num6 => (2, 0x40),
            KeyCode::B => (2, 0x20),
            KeyCode::Num4 => (2, 0x10),
            KeyCode::Z => (2, 0x02),
            KeyCode::Num2 => (2, 0x01),
            KeyCode::K => (3, 0x80),
            KeyCode::Num9 => (3, 0x40),
            KeyCode::Minus => (3, 0x20),
            KeyCode::Y => (3, 0x10),
            KeyCode::Num8 => (3, 0x08),
            KeyCode::L => (3, 0x04),
            KeyCode::Equals => (3, 0x02),
            KeyCode::H => (3, 0x01),
            KeyCode::Space => (4, 0x80),
            KeyCode::Comma => (4, 0x40),
            KeyCode::Period => (4, 0x20),
            KeyCode::Up => (4, 0x10),
            KeyCode::LShift => (4, 0x08),
            KeyCode::Left => (4, 0x04),
            KeyCode::Down => (4, 0x02),
            KeyCode::Right => (4, 0x01),
            KeyCode::Backspace => (5, 0x20),
            KeyCode::Return => (7, 0x04),
            KeyCode::A => (6, 0x01),
            KeyCode::S => (6, 0x02),
            KeyCode::W => (6, 0x04),
            KeyCode::E => (6, 0x08),
            KeyCode::C => (6, 0x10),
            KeyCode::G => (6, 0x20),
            KeyCode::I => (5, 0x01),
            KeyCode::O => (5, 0x02),
            KeyCode::P => (5, 0x04),
            KeyCode::U => (5, 0x08),
            KeyCode::Num0 => (5, 0x10),
            _ => return None,
        })
    }
}

impl EmulatorCore for OricAtmos {
    fn system_type(&self) -> SystemType {
        SystemType::OricAtmos
    }

    fn system_name(&self) -> &str {
        "Oric Atmos 48K (6502 + VIA + AY-3-8912)"
    }

    fn media_name(&self) -> &str {
        &self.bus.media_name
    }

    fn reset(&mut self) {
        self.bus.reset_state();
        self.cpu.reset(&mut self.bus);
        self.total_cycles = 0;
        for _ in 0..115 {
            self.run_frame();
        }
    }

    fn run_frame(&mut self) {
        self.bus.audio_buf.clear();
        let mut cyc = 0u32;
        while cyc < 20_000 {
            let c = self.cpu.step(&mut self.bus);
            cyc += c;
            self.total_cycles += c as u64;
            self.bus.via.tick(c);
            if self.bus.via.irq_active() {
                self.cpu.trigger_irq();
            }
            self.bus.audio_accum += (c as f64 / 1_000_000.0) * AUDIO_SAMPLE_RATE as f64;
            while self.bus.audio_accum >= 1.0 {
                self.bus.audio_accum -= 1.0;
                let s = self.bus.ay.next_sample(AUDIO_SAMPLE_RATE as f64);
                self.bus.audio_buf.push(s);
            }
        }
        self.bus.frame_cnt = self.bus.frame_cnt.wrapping_add(1);
        self.bus.render_screen(&mut self.screen);
    }

    fn step_instruction(&mut self) -> u32 {
        let c = self.cpu.step(&mut self.bus);
        self.bus.via.tick(c);
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
            self.bus.key_matrix[r] |= mask;
            self.bus.update_keyboard();
        }
    }

    fn handle_key_up(&mut self, key: KeyCode, _mods: KeyMod) {
        if let Some((r, mask)) = Self::map_key(key) {
            self.bus.key_matrix[r] &= !mask;
            self.bus.update_keyboard();
        }
    }

    fn load_media(&mut self, path: &Path) -> EmuResult<()> {
        let (name, ext, bytes) = read_media_or_zip(path, &["tap", "rom"])?;
        if ext == "tap" {
            let mut pos = 0usize;
            while pos < bytes.len() && bytes[pos] == 0x16 {
                pos += 1;
            }
            if pos >= bytes.len() || bytes[pos] != 0x24 || pos + 10 >= bytes.len() {
                return Err(EmuError::UnsupportedMedia("Invalid Oric .tap header".into()));
            }
            let hdr = &bytes[pos + 1..];
            if hdr.len() < 9 {
                return Err(EmuError::UnsupportedMedia("Truncated Oric .tap header".into()));
            }
            let ftype = hdr[2];
            let end_addr = u16::from_be_bytes([hdr[4], hdr[5]]) as usize;
            let start_addr = u16::from_be_bytes([hdr[6], hdr[7]]) as usize;
            let mut fn_end = 9usize;
            while fn_end < hdr.len() && hdr[fn_end] != 0 {
                fn_end += 1;
            }
            if fn_end >= hdr.len() || end_addr < start_addr {
                return Err(EmuError::UnsupportedMedia("Invalid Oric .tap addresses".into()));
            }
            let payload = &hdr[fn_end + 1..];
            while self.total_cycles < 1_000_000 {
                self.run_frame();
            }
            let copy_len = (end_addr - start_addr + 1)
                .min(payload.len())
                .min(self.bus.ram.len().saturating_sub(start_addr));
            self.bus.ram[start_addr..start_addr + copy_len].copy_from_slice(&payload[..copy_len]);
            let prog_end = (start_addr + copy_len) as u16;
            if ftype == 0x00 {
                let end_le = prog_end.to_le_bytes();
                self.bus.ram[0x009A..0x009C].copy_from_slice(&end_le);
                self.bus.ram[0x009C..0x009E].copy_from_slice(&end_le);
            } else {
                self.cpu.pc = start_addr as u16;
            }
            self.bus.media_name = name;
            for _ in 0..5 {
                self.run_frame();
            }
            return Ok(());
        }
        let len = bytes.len().min(16_384);
        if len >= 4096 && ext == "rom" {
            self.bus.rom[..len].copy_from_slice(&bytes[..len]);
            self.bus.media_name = name;
            self.reset();
            return Ok(());
        }
        Err(EmuError::UnsupportedMedia("Provide Oric .tap or .rom firmware".into()))
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
