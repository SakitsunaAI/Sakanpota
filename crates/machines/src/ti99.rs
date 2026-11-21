use cpu_cores::{Tms9900, Tms9900Bus};
use emu_core::archive::load_rom_file;
use emu_core::types::{
    CpuDebugState, DisassembledInstruction, EmuError, EmuResult, EmulatorCore, KeyCode, KeyMod,
    SystemType, AUDIO_SAMPLE_RATE, CANVAS_HEIGHT, CANVAS_WIDTH,
};
use peripherals::{Sn76489, Tms9918};
use std::fs;
use std::path::Path;

struct Ti99Bus {
    rom: Vec<u8>,
    grom: Vec<u8>,
    grom_addr: u16,
    grom_read_hi_next: bool,
    grom_write_hi_next: bool,
    grom_prefetch: u8,
    scratch_ram: [u8; 256],
    exp_ram: Vec<u8>,
    cart_rom: Vec<u8>,
    vdp: Tms9918,
    sn: Sn76489,
    kbd_col: u8,
    alpha_lock: bool,
    vdp_irq_mask: bool,
    key_cols: [u8; 8], // 6 keyboard cols + joysticks active low
    audio_buf: Vec<i16>,
    audio_accum: f64,
    media_name: String,
}

impl Ti99Bus {
    fn new(rom_dir: &Path) -> EmuResult<Self> {
        let rom_bytes = load_rom_file(rom_dir, "ti99_rom.bin").or_else(|_| {
            let hb = load_rom_file(rom_dir, "994a_rom_hb.u610")?;
            let lb = load_rom_file(rom_dir, "994a_rom_lb.u611")?;
            let mut merged = Vec::with_capacity(8192);
            for i in 0..4096 {
                merged.push(hb.get(i).copied().unwrap_or(0));
                merged.push(lb.get(i).copied().unwrap_or(0));
            }
            Ok::<Vec<u8>, EmuError>(merged)
        })?;
        let grom_bytes = load_rom_file(rom_dir, "ti99_grom.bin").or_else(|_| {
            let g0 = load_rom_file(rom_dir, "994a_grom0.u500")?;
            let g1 = load_rom_file(rom_dir, "994a_grom1.u501")?;
            let g2 = load_rom_file(rom_dir, "994a_grom2.u502")?;
            let mut merged = Vec::with_capacity(24_576);
            merged.extend_from_slice(&g0);
            merged.resize(8192, 0);
            merged.extend_from_slice(&g1);
            merged.resize(16_384, 0);
            merged.extend_from_slice(&g2);
            Ok::<Vec<u8>, EmuError>(merged)
        })?;

        let mut rom = vec![0u8; 8192];
        rom[..rom_bytes.len().min(8192)].copy_from_slice(&rom_bytes[..rom_bytes.len().min(8192)]);

        let mut grom = vec![0xFFu8; 65_536];
        // Support flat 18KB (3 x 6KB) or 24KB (3 x 8KB) GROM image layouts cleanly
        if grom_bytes.len() == 18_432 {
            grom[0x0000..0x1800].copy_from_slice(&grom_bytes[0x0000..0x1800]);
            grom[0x2000..0x3800].copy_from_slice(&grom_bytes[0x1800..0x3000]);
            grom[0x4000..0x5800].copy_from_slice(&grom_bytes[0x3000..0x4800]);
        } else {
            let len = grom_bytes.len().min(65_536);
            grom[..len].copy_from_slice(&grom_bytes[..len]);
        }

        Ok(Self {
            rom,
            grom,
            grom_addr: 0,
            grom_read_hi_next: true,
            grom_write_hi_next: true,
            grom_prefetch: 0,
            scratch_ram: [0; 256],
            exp_ram: vec![0; 32_768],
            cart_rom: Vec::new(),
            vdp: Tms9918::new(),
            sn: Sn76489::new(3_579_545.0),
            kbd_col: 0,
            alpha_lock: true,
            vdp_irq_mask: false,
            key_cols: [0xFF; 8],
            audio_buf: Vec::with_capacity(1024),
            audio_accum: 0.0,
            media_name: "TI-99/4A Console ROM/GROM".to_string(),
        })
    }

    fn reset_state(&mut self) {
        self.grom_addr = 0;
        self.grom_read_hi_next = true;
        self.grom_write_hi_next = true;
        self.grom_prefetch = 0;
        self.scratch_ram.fill(0);
        self.exp_ram.fill(0);
        self.vdp.reset();
        self.sn.reset();
        self.kbd_col = 0;
        self.vdp_irq_mask = false;
    }

    fn grom_advance(&mut self) {
        // GROM increments low 13 bits within an 8KB page slot.
        let slot = self.grom_addr & 0xE000;
        let off = (self.grom_addr.wrapping_add(1)) & 0x1FFF;
        self.grom_addr = slot | off;
    }
}

impl Tms9900Bus for Ti99Bus {
    fn read8(&mut self, addr: u16) -> u8 {
        match addr {
            0x0000..=0x1FFF => self.rom[addr as usize],
            0x2000..=0x3FFF => self.exp_ram[(addr - 0x2000) as usize],
            0x6000..=0x7FFF if !self.cart_rom.is_empty() => {
                self.cart_rom[(addr - 0x6000) as usize % self.cart_rom.len()]
            }
            0x8000..=0x83FF => self.scratch_ram[(addr & 0xFF) as usize],
            0x8800..=0x8BFF if (addr & 1) == 0 => {
                if (addr & 2) == 0 {
                    self.vdp.read_data()
                } else {
                    self.vdp.read_status()
                }
            }
            0x9800..=0x9BFF if (addr & 1) == 0 => {
                if (addr & 2) == 0 {
                    let out = self.grom_prefetch;
                    self.grom_prefetch = self.grom[self.grom_addr as usize];
                    self.grom_advance();
                    self.grom_read_hi_next = true;
                    self.grom_write_hi_next = true;
                    out
                } else {
                    self.grom_write_hi_next = true;
                    let b = if self.grom_read_hi_next {
                        (self.grom_addr >> 8) as u8
                    } else {
                        (self.grom_addr & 0xFF) as u8
                    };
                    self.grom_read_hi_next = !self.grom_read_hi_next;
                    b
                }
            }
            0xA000..=0xFFFF => self.exp_ram[0x2000 + ((addr - 0xA000) as usize)],
            _ => 0xFF,
        }
    }

    fn write8(&mut self, addr: u16, val: u8) {
        match addr {
            0x2000..=0x3FFF => self.exp_ram[(addr - 0x2000) as usize] = val,
            0x8000..=0x83FF => self.scratch_ram[(addr & 0xFF) as usize] = val,
            0x8400..=0x87FF => self.sn.write(val),
            0x8C00..=0x8FFF if (addr & 1) == 0 => {
                if (addr & 2) == 0 {
                    self.vdp.write_data(val);
                } else {
                    self.vdp.write_ctrl(val);
                }
            }
            0x9C00..=0x9FFF if (addr & 1) == 0 && (addr & 2) != 0 => {
                self.grom_read_hi_next = true;
                if self.grom_write_hi_next {
                    self.grom_addr = (self.grom_addr & 0x00FF) | ((val as u16) << 8);
                    self.grom_write_hi_next = false;
                } else {
                    self.grom_addr = (self.grom_addr & 0xFF00) | (val as u16);
                    self.grom_prefetch = self.grom[self.grom_addr as usize];
                    self.grom_advance();
                    self.grom_write_hi_next = true;
                }
            }
            0xA000..=0xFFFF => self.exp_ram[0x2000 + ((addr - 0xA000) as usize)] = val,
            _ => {}
        }
    }

    fn read16(&mut self, addr: u16) -> u16 {
        let a = addr & !1;
        let hi = self.read8(a) as u16;
        let lo = self.read8(a.wrapping_add(1)) as u16;
        (hi << 8) | lo
    }

    fn write16(&mut self, addr: u16, val: u16) {
        let a = addr & !1;
        self.write8(a, (val >> 8) as u8);
        self.write8(a.wrapping_add(1), (val & 0xFF) as u8);
    }

    fn peek8(&self, addr: u16) -> u8 {
        match addr {
            0x0000..=0x1FFF => self.rom[addr as usize],
            0x2000..=0x3FFF => self.exp_ram[(addr - 0x2000) as usize],
            0x8000..=0x83FF => self.scratch_ram[(addr & 0xFF) as usize],
            0xA000..=0xFFFF => self.exp_ram[0x2000 + ((addr - 0xA000) as usize)],
            _ => 0xFF,
        }
    }

    fn cru_read_bit(&mut self, bit_addr: u16) -> bool {
        match bit_addr {
            2 => !self.vdp.irq_active(),
            3..=10 => {
                let row = bit_addr - 3;
                let col = (self.kbd_col & 7) as usize;
                let pressed_bits = self.key_cols[col];
                ((pressed_bits >> row) & 1) != 0
            }
            _ => true,
        }
    }

    fn cru_write_bit(&mut self, bit_addr: u16, val: bool) {
        match bit_addr {
            2 => self.vdp_irq_mask = val,
            18..=20 => {
                let b = bit_addr - 18;
                if val {
                    self.kbd_col |= 1 << b;
                } else {
                    self.kbd_col &= !(1 << b);
                }
            }
            21 => self.alpha_lock = val,
            _ => {}
        }
    }
}

pub struct Ti99_4A {
    cpu: Tms9900,
    bus: Ti99Bus,
    screen: Vec<u32>,
}

impl Ti99_4A {
    pub fn new(rom_dir: &Path) -> EmuResult<Self> {
        let mut bus = Ti99Bus::new(rom_dir)?;
        let mut cpu = Tms9900::new();
        cpu.reset(&mut bus);
        let mut m = Self {
            cpu,
            bus,
            screen: vec![0xFF00_0000; CANVAS_WIDTH * CANVAS_HEIGHT],
        };
        for _ in 0..25 {
            m.run_frame();
        }
        Ok(m)
    }

    fn map_key(key: KeyCode) -> Option<(usize, u8)> {
        Some(match key {
            KeyCode::Equals => (0, 0x01),
            KeyCode::Period => (0, 0x02),
            KeyCode::Comma => (0, 0x04),
            KeyCode::M => (0, 0x08),
            KeyCode::N => (0, 0x10),
            KeyCode::Slash => (0, 0x20),
            KeyCode::Space => (1, 0x01),
            KeyCode::L => (1, 0x02),
            KeyCode::K => (1, 0x04),
            KeyCode::J => (1, 0x08),
            KeyCode::H => (1, 0x10),
            KeyCode::Semicolon => (1, 0x20),
            KeyCode::Return => (2, 0x01),
            KeyCode::O => (2, 0x02),
            KeyCode::I => (2, 0x04),
            KeyCode::U => (2, 0x08),
            KeyCode::Y => (2, 0x10),
            KeyCode::P => (2, 0x20),
            KeyCode::Num9 => (3, 0x02),
            KeyCode::Num8 => (3, 0x04),
            KeyCode::Num7 => (3, 0x08),
            KeyCode::Num6 => (3, 0x10),
            KeyCode::Num0 => (3, 0x20),
            KeyCode::Num2 => (4, 0x02),
            KeyCode::Num3 => (4, 0x04),
            KeyCode::Num4 => (4, 0x08),
            KeyCode::Num5 => (4, 0x10),
            KeyCode::Num1 => (4, 0x20),
            KeyCode::Q => (5, 0x20),
            KeyCode::W => (5, 0x02),
            KeyCode::E => (5, 0x04),
            KeyCode::R => (5, 0x08),
            KeyCode::T => (5, 0x10),
            KeyCode::A => (5, 0x40),
            KeyCode::S => (4, 0x40),
            KeyCode::D => (3, 0x40),
            KeyCode::F => (2, 0x40),
            KeyCode::G => (1, 0x40),
            KeyCode::Z => (5, 0x80),
            KeyCode::X => (4, 0x80),
            KeyCode::C => (3, 0x80),
            KeyCode::V => (2, 0x80),
            KeyCode::B => (1, 0x80),
            KeyCode::LShift | KeyCode::RShift => (0, 0x20),
            _ => return None,
        })
    }
}

impl EmulatorCore for Ti99_4A {
    fn system_type(&self) -> SystemType {
        SystemType::Ti99_4A
    }

    fn system_name(&self) -> &str {
        "TI-99/4A (TMS9900 + TMS9918A + SN76489)"
    }

    fn media_name(&self) -> &str {
        &self.bus.media_name
    }

    fn reset(&mut self) {
        self.bus.reset_state();
        self.cpu.reset(&mut self.bus);
        for _ in 0..25 {
            self.run_frame();
        }
    }

    fn run_frame(&mut self) {
        self.bus.audio_buf.clear();
        let mut cyc = 0u32;
        while cyc < 50_000 {
            let c = self.cpu.step(&mut self.bus);
            cyc += c;
            self.bus.audio_accum += (c as f64 / 3_000_000.0) * AUDIO_SAMPLE_RATE as f64;
            while self.bus.audio_accum >= 1.0 {
                self.bus.audio_accum -= 1.0;
                let s = self.bus.sn.next_sample(AUDIO_SAMPLE_RATE as f64);
                self.bus.audio_buf.push(s);
            }
        }
        self.bus.vdp.begin_vblank();
        if self.bus.vdp_irq_mask && self.bus.vdp.irq_active() {
            self.cpu.request_irq(1);
        }
        self.bus.vdp.render_screen(&mut self.screen, CANVAS_WIDTH, CANVAS_HEIGHT);
    }

    fn step_instruction(&mut self) -> u32 {
        let c = self.cpu.step(&mut self.bus);
        self.bus.vdp.render_screen(&mut self.screen, CANVAS_WIDTH, CANVAS_HEIGHT);
        c
    }

    fn rendered_screen(&self) -> &[u32] {
        &self.screen
    }

    fn audio_buffer(&self) -> &[i16] {
        &self.bus.audio_buf
    }

    fn handle_key_down(&mut self, key: KeyCode, _mods: KeyMod) {
        if let Some((col, mask)) = Self::map_key(key) {
            self.bus.key_cols[col] &= !mask;
        }
    }

    fn handle_key_up(&mut self, key: KeyCode, _mods: KeyMod) {
        if let Some((col, mask)) = Self::map_key(key) {
            self.bus.key_cols[col] |= mask;
        }
    }

    fn load_media(&mut self, path: &Path) -> EmuResult<()> {
        let bytes = fs::read(path).map_err(|e| EmuError::IoError(path.display().to_string(), e))?;
        let name = path
            .file_name()
            .and_then(|s| s.to_str())
            .unwrap_or("ti99.bin")
            .to_string();
        if bytes.len() <= 8192 {
            self.bus.cart_rom = bytes;
        } else {
            let end = (0x6000 + bytes.len()).min(self.bus.grom.len());
            self.bus.grom[0x6000..end].copy_from_slice(&bytes[..end - 0x6000]);
        }
        self.bus.media_name = name;
        self.reset();
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
