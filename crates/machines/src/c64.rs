use cpu_cores::Mos6502;
use emu_core::archive::{load_rom_file, read_media_or_zip};
use emu_core::types::{
    Bus8, CpuDebugState, DisassembledInstruction, EmuError, EmuResult, EmulatorCore, KeyCode,
    KeyMod, SystemType, AUDIO_SAMPLE_RATE, CANVAS_HEIGHT, CANVAS_WIDTH,
};
use peripherals::{Cia6526, Sid6581, Vic6569};
use std::path::Path;

struct C64Bus {
    ram: Vec<u8>,
    color_ram: [u8; 1024],
    basic_rom: Vec<u8>,
    kernal_rom: Vec<u8>,
    char_rom: [u8; 4096],
    cart_rom_lo: Vec<u8>,
    cart_rom_hi: Vec<u8>,
    exrom: bool, // active low: true means no 8K lo-cart
    game: bool,  // active low: true means no 8K hi-cart
    cpu_port_ddr: u8,
    cpu_port_data: u8,
    vic: Vic6569,
    sid: Sid6581,
    cia1: Cia6526,
    cia2: Cia6526,
    key_matrix: [u8; 8], // 8 cols x 8 rows active high pressed
    audio_buf: Vec<i16>,
    audio_accum: f64,
    media_name: String,
}

impl C64Bus {
    fn new(rom_dir: &Path) -> EmuResult<Self> {
        let basic = load_rom_file(rom_dir, "c64_basic.bin")?;
        let kernal = load_rom_file(rom_dir, "c64_kernal.bin")?;
        let chargen = load_rom_file(rom_dir, "c64_chargen.bin")?;

        let mut bus = Self {
            ram: vec![0; 65_536],
            color_ram: [0; 1024],
            basic_rom: vec![0; 8192],
            kernal_rom: vec![0; 8192],
            char_rom: [0; 4096],
            cart_rom_lo: Vec::new(),
            cart_rom_hi: Vec::new(),
            exrom: true,
            game: true,
            cpu_port_ddr: 0x2F,
            cpu_port_data: 0x37,
            vic: Vic6569::new(),
            sid: Sid6581::new(985_248.0),
            cia1: Cia6526::new(),
            cia2: Cia6526::new(),
            key_matrix: [0; 8],
            audio_buf: Vec::with_capacity(1024),
            audio_accum: 0.0,
            media_name: "Commodore 64 BASIC V2".to_string(),
        };
        let b_len = basic.len().min(8192);
        let k_len = kernal.len().min(8192);
        let c_len = chargen.len().min(4096);
        bus.basic_rom[..b_len].copy_from_slice(&basic[..b_len]);
        bus.kernal_rom[..k_len].copy_from_slice(&kernal[..k_len]);
        bus.char_rom[..c_len].copy_from_slice(&chargen[..c_len]);
        Ok(bus)
    }

    fn reset_state(&mut self) {
        self.ram.fill(0);
        self.color_ram.fill(0x0E);
        self.cpu_port_ddr = 0x2F;
        self.cpu_port_data = 0x37;
        self.vic.reset();
        self.sid.reset();
        self.cia1.reset();
        self.cia2.reset();
        self.cia2.pra = 0x03; // VIC bank 0 ($0000..$3FFF)
        self.update_keyboard_matrix();
    }

    #[inline]
    fn cpu_port_effective(&self) -> u8 {
        (self.cpu_port_data & self.cpu_port_ddr) | (!self.cpu_port_ddr & 0x17)
    }

    #[inline]
    fn vic_bank_base(&self) -> u16 {
        let bits = (!self.cia2.port_a_pins()) & 0x03;
        (bits as u16) << 14
    }

    fn update_keyboard_matrix(&mut self) {
        let pa_out = self.cia1.pra | !self.cia1.ddra;
        let pb_out = self.cia1.prb | !self.cia1.ddrb;
        let mut pb_in = 0xFFu8;
        let mut pa_in = 0xFFu8;
        for row in 0..8usize {
            if (pa_out & (1 << row)) == 0 {
                pb_in &= !self.key_matrix[row];
            }
            for col in 0..8usize {
                if (pb_out & (1 << col)) == 0 && (self.key_matrix[row] & (1 << col)) != 0 {
                    pa_in &= !(1 << row);
                }
            }
        }
        self.cia1.pra_in = pa_in;
        self.cia1.prb_in = pb_in;
    }

    fn render_screen(&mut self, out: &mut [u32]) {
        let bank_base = self.vic_bank_base();
        let bank_idx = (bank_base >> 14) & 3;
        let ram = &self.ram;
        let char_rom = &self.char_rom;
        let color_ram = &self.color_ram;
        self.vic.render_screen(out, CANVAS_WIDTH, CANVAS_HEIGHT, color_ram, |addr14| {
            if (bank_idx == 0 || bank_idx == 2) && (0x1000..0x2000).contains(&addr14) {
                char_rom[(addr14 - 0x1000) as usize]
            } else {
                ram[(bank_base | (addr14 & 0x3FFF)) as usize]
            }
        });
    }
}

impl Bus8 for C64Bus {
    fn read8(&mut self, addr: u16) -> u8 {
        if addr == 0x0000 {
            return self.cpu_port_ddr;
        }
        if addr == 0x0001 {
            return self.cpu_port_effective();
        }
        let port = self.cpu_port_effective();
        let loram = (port & 0x01) != 0;
        let hiram = (port & 0x02) != 0;
        let charen = (port & 0x04) != 0;

        match addr {
            0x8000..=0x9FFF => {
                if !self.exrom && !self.cart_rom_lo.is_empty() && hiram {
                    self.cart_rom_lo[(addr - 0x8000) as usize % self.cart_rom_lo.len()]
                } else {
                    self.ram[addr as usize]
                }
            }
            0xA000..=0xBFFF => {
                if !self.game && !self.cart_rom_hi.is_empty() && hiram {
                    self.cart_rom_hi[(addr - 0xA000) as usize % self.cart_rom_hi.len()]
                } else if loram && hiram {
                    self.basic_rom[(addr - 0xA000) as usize]
                } else {
                    self.ram[addr as usize]
                }
            }
            0xD000..=0xDFFF => {
                if !loram && !hiram {
                    return self.ram[addr as usize];
                }
                if !charen {
                    return self.char_rom[(addr - 0xD000) as usize];
                }
                match addr {
                    0xD000..=0xD3FF => self.vic.read((addr & 0x3F) as u8),
                    0xD400..=0xD7FF => self.sid.read((addr & 0x1F) as u8),
                    0xD800..=0xDBFF => self.color_ram[(addr - 0xD800) as usize] | 0xF0,
                    0xDC00..=0xDCFF => {
                        self.update_keyboard_matrix();
                        self.cia1.read((addr & 0x0F) as u8)
                    }
                    0xDD00..=0xDDFF => self.cia2.read((addr & 0x0F) as u8),
                    _ => 0xFF,
                }
            }
            0xE000..=0xFFFF => {
                if !self.game && self.exrom && !self.cart_rom_hi.is_empty() {
                    self.cart_rom_hi[(addr - 0xE000) as usize % self.cart_rom_hi.len()]
                } else if hiram {
                    self.kernal_rom[(addr - 0xE000) as usize]
                } else {
                    self.ram[addr as usize]
                }
            }
            _ => self.ram[addr as usize],
        }
    }

    fn write8(&mut self, addr: u16, val: u8) {
        if addr == 0x0000 {
            self.cpu_port_ddr = val;
            return;
        }
        if addr == 0x0001 {
            self.cpu_port_data = val;
            return;
        }
        let port = self.cpu_port_effective();
        let loram = (port & 0x01) != 0;
        let hiram = (port & 0x02) != 0;
        let charen = (port & 0x04) != 0;

        if (0xD000..0xE000).contains(&addr) && (loram || hiram) && charen {
            match addr {
                0xD000..=0xD3FF => self.vic.write((addr & 0x3F) as u8, val),
                0xD400..=0xD7FF => self.sid.write((addr & 0x1F) as u8, val),
                0xD800..=0xDBFF => self.color_ram[(addr - 0xD800) as usize] = val & 0x0F,
                0xDC00..=0xDCFF => {
                    self.cia1.write((addr & 0x0F) as u8, val);
                    self.update_keyboard_matrix();
                }
                0xDD00..=0xDDFF => self.cia2.write((addr & 0x0F) as u8, val),
                _ => {}
            }
            return;
        }

        self.ram[addr as usize] = val;
    }

    fn peek8(&self, addr: u16) -> u8 {
        if addr == 0x0000 {
            return self.cpu_port_ddr;
        }
        if addr == 0x0001 {
            return self.cpu_port_effective();
        }
        let port = self.cpu_port_effective();
        let loram = (port & 0x01) != 0;
        let hiram = (port & 0x02) != 0;
        let charen = (port & 0x04) != 0;
        match addr {
            0xA000..=0xBFFF if loram && hiram => self.basic_rom[(addr - 0xA000) as usize],
            0xD000..=0xDFFF if (loram || hiram) && !charen => self.char_rom[(addr - 0xD000) as usize],
            0xDC00..=0xDCFF if (loram || hiram) && charen => self.cia1.peek((addr & 0x0F) as u8),
            0xDD00..=0xDDFF if (loram || hiram) && charen => self.cia2.peek((addr & 0x0F) as u8),
            0xE000..=0xFFFF if hiram => self.kernal_rom[(addr - 0xE000) as usize],
            _ => self.ram[addr as usize],
        }
    }
}

pub struct Commodore64 {
    cpu: Mos6502,
    bus: C64Bus,
    screen: Vec<u32>,
    total_cycles: u64,
}

impl Commodore64 {
    pub fn new(rom_dir: &Path) -> EmuResult<Self> {
        let mut bus = C64Bus::new(rom_dir)?;
        bus.reset_state();
        let mut cpu = Mos6502::new(false);
        cpu.reset(&mut bus);
        Ok(Self {
            cpu,
            bus,
            screen: vec![0xFF00_0000; CANVAS_WIDTH * CANVAS_HEIGHT],
            total_cycles: 0,
        })
    }

    fn d64_sector_offset(track: u8, sector: u8) -> Option<usize> {
        if track == 0 || track > 40 {
            return None;
        }
        let sectors_per_track = |t: u8| -> usize {
            match t {
                1..=17 => 21,
                18..=24 => 19,
                25..=30 => 18,
                _ => 17,
            }
        };
        if (sector as usize) >= sectors_per_track(track) {
            return None;
        }
        let mut block = 0usize;
        for t in 1..track {
            block += sectors_per_track(t);
        }
        Some((block + sector as usize) * 256)
    }

    fn extract_first_prg_from_d64(d64: &[u8]) -> EmuResult<Vec<u8>> {
        let mut dir_t = 18u8;
        let mut dir_s = 1u8;
        let mut first_prg: Option<(u8, u8)> = None;
        for _ in 0..20 {
            let Some(off) = Self::d64_sector_offset(dir_t, dir_s) else {
                break;
            };
            if off + 256 > d64.len() {
                break;
            }
            let sec = &d64[off..off + 256];
            for entry_idx in 0..8 {
                let e = &sec[entry_idx * 32..entry_idx * 32 + 32];
                let ftype = e[2];
                // Closed PRG file: bit 7 set (or non-zero PRG type == 2), valid start track.
                if (ftype & 0x07) == 2 && e[3] > 0 && e[3] <= 40 {
                    first_prg = Some((e[3], e[4]));
                    break;
                }
            }
            if first_prg.is_some() || sec[0] == 0 {
                break;
            }
            dir_t = sec[0];
            dir_s = sec[1];
        }
        let (mut t, mut s) = first_prg
            .ok_or_else(|| EmuError::UnsupportedMedia("No PRG file found in D64 directory".into()))?;
        let mut prg = Vec::new();
        for _ in 0..768 {
            let Some(off) = Self::d64_sector_offset(t, s) else {
                break;
            };
            if off + 256 > d64.len() {
                break;
            }
            let sec = &d64[off..off + 256];
            let next_t = sec[0];
            let next_s = sec[1];
            if next_t == 0 {
                let last_idx = (next_s as usize).clamp(1, 255);
                prg.extend_from_slice(&sec[2..=last_idx]);
                break;
            }
            prg.extend_from_slice(&sec[2..256]);
            t = next_t;
            s = next_s;
        }
        if prg.len() < 2 {
            return Err(EmuError::UnsupportedMedia("Empty PRG stream in D64".into()));
        }
        Ok(prg)
    }

    fn step_one(&mut self) -> (u32, bool) {
        let c = self.cpu.step(&mut self.bus);
        self.total_cycles += c as u64;
        self.bus.cia1.tick(c);
        self.bus.cia2.tick(c);
        let frame_done = self.bus.vic.tick(c);

        if self.bus.cia1.irq_active() || self.bus.vic.irq_active() {
            self.cpu.trigger_irq();
        }
        if self.bus.cia2.irq_active() {
            self.cpu.trigger_nmi();
        }

        self.bus.audio_accum += (c as f64 / 985_248.0) * AUDIO_SAMPLE_RATE as f64;
        while self.bus.audio_accum >= 1.0 {
            self.bus.audio_accum -= 1.0;
            let s = self.bus.sid.next_sample(AUDIO_SAMPLE_RATE as f64);
            self.bus.audio_buf.push(s);
        }

        (c, frame_done)
    }

    fn map_key(key: KeyCode) -> Option<(usize, usize)> {
        Some(match key {
            KeyCode::Backspace => (0, 0),
            KeyCode::Return => (0, 1),
            KeyCode::Right => (0, 2),
            KeyCode::F7 => (0, 3),
            KeyCode::F1 => (0, 4),
            KeyCode::F3 => (0, 5),
            KeyCode::F5 => (0, 6),
            KeyCode::Down => (0, 7),
            KeyCode::Num3 => (1, 0),
            KeyCode::W => (1, 1),
            KeyCode::A => (1, 2),
            KeyCode::Num4 => (1, 3),
            KeyCode::Z => (1, 4),
            KeyCode::S => (1, 5),
            KeyCode::E => (1, 6),
            KeyCode::LShift => (1, 7),
            KeyCode::Num5 => (2, 0),
            KeyCode::R => (2, 1),
            KeyCode::D => (2, 2),
            KeyCode::Num6 => (2, 3),
            KeyCode::C => (2, 4),
            KeyCode::F => (2, 5),
            KeyCode::T => (2, 6),
            KeyCode::X => (2, 7),
            KeyCode::Num7 => (3, 0),
            KeyCode::Y => (3, 1),
            KeyCode::G => (3, 2),
            KeyCode::Num8 => (3, 3),
            KeyCode::B => (3, 4),
            KeyCode::H => (3, 5),
            KeyCode::U => (3, 6),
            KeyCode::V => (3, 7),
            KeyCode::Num9 => (4, 0),
            KeyCode::I => (4, 1),
            KeyCode::J => (4, 2),
            KeyCode::Num0 => (4, 3),
            KeyCode::M => (4, 4),
            KeyCode::K => (4, 5),
            KeyCode::O => (4, 6),
            KeyCode::N => (4, 7),
            KeyCode::Minus => (5, 0),
            KeyCode::P => (5, 1),
            KeyCode::L => (5, 2),
            KeyCode::Equals => (5, 3),
            KeyCode::Period => (5, 4),
            KeyCode::Semicolon => (5, 5),
            KeyCode::LeftBracket => (5, 6),
            KeyCode::Comma => (5, 7),
            KeyCode::RightBracket => (6, 2),
            KeyCode::Slash => (6, 7),
            KeyCode::Num1 => (7, 0),
            KeyCode::Backquote => (7, 1),
            KeyCode::LCtrl | KeyCode::RCtrl => (7, 2),
            KeyCode::Num2 => (7, 3),
            KeyCode::Space => (7, 4),
            KeyCode::Q => (7, 6),
            KeyCode::Escape => (7, 7),
            _ => return None,
        })
    }
}

impl EmulatorCore for Commodore64 {
    fn system_type(&self) -> SystemType {
        SystemType::Commodore64
    }

    fn system_name(&self) -> &str {
        "Commodore 64 (PAL 6510 + VIC-II + SID)"
    }

    fn media_name(&self) -> &str {
        &self.bus.media_name
    }

    fn reset(&mut self) {
        self.bus.reset_state();
        self.cpu.reset(&mut self.bus);
        self.total_cycles = 0;
    }

    fn run_frame(&mut self) {
        self.bus.audio_buf.clear();
        let mut cyc = 0u32;
        while cyc < 19_656 {
            let (c, done) = self.step_one();
            cyc += c;
            if done {
                break;
            }
        }
        self.bus.render_screen(&mut self.screen);
    }

    fn step_instruction(&mut self) -> u32 {
        let (c, _) = self.step_one();
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
        if let Some((row, col)) = Self::map_key(key) {
            self.bus.key_matrix[row] |= 1 << col;
            self.bus.update_keyboard_matrix();
        }
    }

    fn handle_key_up(&mut self, key: KeyCode, _mods: KeyMod) {
        if let Some((row, col)) = Self::map_key(key) {
            self.bus.key_matrix[row] &= !(1 << col);
            self.bus.update_keyboard_matrix();
        }
    }

    fn load_media(&mut self, path: &Path) -> EmuResult<()> {
        let (name, ext, bytes) = read_media_or_zip(path, &["crt", "prg", "d64"])?;

        if ext == "crt" && bytes.len() >= 64 && &bytes[..16] == b"C64 CARTRIDGE   " {
            self.bus.exrom = bytes[0x18] != 0;
            self.bus.game = bytes[0x19] != 0;
            self.bus.cart_rom_lo.clear();
            self.bus.cart_rom_hi.clear();
            let hdr_len =
                u32::from_be_bytes([bytes[0x10], bytes[0x11], bytes[0x12], bytes[0x13]]) as usize;
            let mut pos = hdr_len.max(0x40);
            while pos + 16 <= bytes.len() && &bytes[pos..pos + 4] == b"CHIP" {
                let pkt_len =
                    u32::from_be_bytes([bytes[pos + 4], bytes[pos + 5], bytes[pos + 6], bytes[pos + 7]])
                        as usize;
                let load_addr = u16::from_be_bytes([bytes[pos + 12], bytes[pos + 13]]);
                let rom_len = u16::from_be_bytes([bytes[pos + 14], bytes[pos + 15]]) as usize;
                let data_start = pos + 16;
                let data_end = (data_start + rom_len).min(bytes.len());
                let slice = &bytes[data_start..data_end];
                if load_addr < 0xA000 {
                    self.bus.cart_rom_lo.extend_from_slice(slice);
                } else {
                    self.bus.cart_rom_hi.extend_from_slice(slice);
                }
                pos += pkt_len.max(16);
            }
            self.bus.media_name = name;
            self.reset();
            return Ok(());
        }

        let prg = if ext == "d64" {
            Self::extract_first_prg_from_d64(&bytes)?
        } else if ext == "prg" && bytes.len() >= 2 {
            bytes
        } else {
            return Err(EmuError::UnsupportedMedia(format!(
                "Unsupported C64 format: {name}"
            )));
        };

        // Ensure KERNAL cold-boot memory clear has completed before poking BASIC RAM.
        while self.total_cycles < 1_800_000 {
            self.run_frame();
        }
        let load_addr = u16::from_le_bytes([prg[0], prg[1]]) as usize;
        let payload = &prg[2..];
        let end = (load_addr + payload.len()).min(65_536);
        self.bus.ram[load_addr..end].copy_from_slice(&payload[..end - load_addr]);
        if load_addr == 0x0801 {
            let end_u16 = (end as u16).to_le_bytes();
            self.bus.ram[0x002D..0x002F].copy_from_slice(&end_u16);
            self.bus.ram[0x002F..0x0031].copy_from_slice(&end_u16);
            self.bus.ram[0x0031..0x0033].copy_from_slice(&end_u16);
            // Inject RUN\r into C64 KERNAL keyboard buffer ($0277..$027B, length in $00C6).
            self.bus.ram[0x0277..0x027B].copy_from_slice(b"RUN\r");
            self.bus.ram[0x00C6] = 4;
        }
        self.bus.media_name = name;
        for _ in 0..15 {
            self.run_frame();
        }
        Ok(())
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
