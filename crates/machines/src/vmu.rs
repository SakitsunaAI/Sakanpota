use std::fs;
use std::path::Path;

use cpu_cores::{Lc8670, Lc8670Bus};
use emu_core::archive::load_rom_file;
use emu_core::gui_font::draw_string;
use emu_core::{
    CANVAS_HEIGHT, CANVAS_WIDTH, CpuDebugState, DisassembledInstruction, EmuError, EmuResult,
    EmulatorCore, KeyCode, KeyMod, SystemType,
};

const ICON_NAMES: [&str; 4] = ["FILE", "CLOCK", "GAME", "SLEEP"];

struct VmuBus {
    bios: Vec<u8>,       // 64 KB
    flash: Vec<u8>,      // 128 KB
    ram: [[u8; 256]; 2], // Two 256-byte internal work RAM banks
    wram: [u8; 512],     // 512-byte work RAM mapped via VSEL/VRMAD1/VRMAD2/VTRBF ($163..$166)
    xram: [[u8; 128]; 3],
    sfr: [u8; 128],
    buzzer_freq: u8,
    buzzer_on: bool,
    t0_prescale: u16,
    t0_low: u8,
    t0_high: u8,
    t1_low: u8,
    t1_high: u8,
    base_div: u32,
    audio_buf: Vec<i16>,
    media_name: String,
}

impl VmuBus {
    fn new(rom_dir: &Path) -> EmuResult<Self> {
        let mut bios = load_rom_file(rom_dir, "vmu_bios.bin")?;
        bios.resize(65_536, 0xFF);
        let mut bus = Self {
            bios,
            flash: vec![0; 131_072],
            ram: [[0; 256]; 2],
            wram: [0; 512],
            xram: [[0; 128]; 3],
            sfr: [0; 128],
            buzzer_freq: 0,
            buzzer_on: false,
            t0_prescale: 0,
            t0_low: 0,
            t0_high: 0,
            t1_low: 0,
            t1_high: 0,
            base_div: 0,
            audio_buf: Vec::with_capacity(735),
            media_name: String::from("VMU BIOS + Empty 128KB Flash"),
        };
        bus.format_flash();
        bus.reset_state();
        Ok(bus)
    }

    fn format_flash(&mut self) {
        self.flash.fill(0x00);
        // Block 255 (0x1FE00): Root block signature
        let root = 255 * 512;
        for b in &mut self.flash[root..root + 16] {
            *b = 0x55;
        }
        // BCD timestamp (2026-11-15)
        self.flash[root + 0x30] = 0x20;
        self.flash[root + 0x31] = 0x26;
        self.flash[root + 0x32] = 0x11;
        self.flash[root + 0x33] = 0x15;
        self.flash[root + 0x44] = 255; // root block low
        self.flash[root + 0x46] = 254; // FAT block
        self.flash[root + 0x48] = 1; // FAT blocks count
        self.flash[root + 0x4A] = 253; // Directory start
        self.flash[root + 0x4C] = 13; // Directory size
        self.flash[root + 0x50] = 200; // User blocks = 200

        // Block 254 (0x1FC00): FAT (0xFFFC = unallocated)
        let fat = 254 * 512;
        for i in 0..256usize {
            self.flash[fat + i * 2] = 0xFC;
            self.flash[fat + i * 2 + 1] = 0xFF;
        }
        for i in 241..=253usize {
            self.flash[fat + i * 2] = if i == 241 { 0xFA } else { (i - 1) as u8 };
            self.flash[fat + i * 2 + 1] = if i == 241 { 0xFF } else { 0x00 };
        }
        self.flash[fat + 254 * 2] = 0xFA;
        self.flash[fat + 254 * 2 + 1] = 0xFF;
        self.flash[fat + 255 * 2] = 0xFA;
        self.flash[fat + 255 * 2 + 1] = 0xFF;
    }

    fn install_vms_game(&mut self, name: &str, vms: &[u8]) {
        self.format_flash();
        let blocks = vms.len().div_ceil(512).clamp(1, 200);
        let copy_len = vms.len().min(blocks * 512);
        self.flash[..copy_len].copy_from_slice(&vms[..copy_len]);

        let fat = 254 * 512;
        for b in 0..blocks {
            if b + 1 == blocks {
                self.flash[fat + b * 2] = 0xFA;
                self.flash[fat + b * 2 + 1] = 0xFF;
            } else {
                self.flash[fat + b * 2] = (b + 1) as u8;
                self.flash[fat + b * 2 + 1] = 0x00;
            }
        }

        // First directory entry in block 253
        let dir = 253 * 512;
        self.flash[dir] = 0xCC; // GAME type
        self.flash[dir + 1] = 0x00; // copy ok
        self.flash[dir + 2] = 0x00; // start block = 0
        self.flash[dir + 3] = 0x00;
        let short = name.as_bytes();
        for i in 0..12usize {
            self.flash[dir + 4 + i] = *short.get(i).unwrap_or(&b' ');
        }
        self.flash[dir + 0x18] = (blocks & 0xFF) as u8;
        self.flash[dir + 0x19] = (blocks >> 8) as u8;
        self.flash[dir + 0x1A] = 0x01; // header block offset
    }

    fn reset_state(&mut self) {
        self.ram = [[0; 256]; 2];
        self.wram.fill(0);
        self.xram = [[0; 128]; 3];
        self.sfr = [0; 128];
        self.sfr[0x08] = 0x80; // IE (master interrupt enable)
        self.sfr[0x4C] = 0xFF; // P3 keypad (active low, all released)
        self.sfr[0x5C] = 0x02; // P7 battery OK, standalone mode (not docked)
        self.sfr[0x7F] = 0x41; // BTCR default
        self.t0_prescale = 0;
        self.t0_low = 0;
        self.t0_high = 0;
        self.t1_low = 0;
        self.t1_high = 0;
        self.base_div = 0;
        self.buzzer_on = false;
    }

    fn tick(&mut self, cycles: u32) -> Option<u16> {
        let master_ie = (self.sfr[0x08] & 0x80) != 0;

        // Timer 0 (T0CNT = 0x10)
        if (self.sfr[0x10] & 0xC0) != 0 {
            self.t0_prescale += cycles as u16;
            let div = (256u16.saturating_sub(self.sfr[0x11] as u16)).max(1);
            if self.t0_prescale >= div {
                self.t0_prescale -= div;
                if self.t0_low == 0xFF {
                    self.t0_low = self.sfr[0x13];
                    self.sfr[0x10] |= 0x02;
                    if (self.sfr[0x10] & 0x01) != 0 && master_ie {
                        return Some(0x0013);
                    }
                } else {
                    self.t0_low += 1;
                }
            }
        }

        // Timer 1 (T1CNT = 0x18) + buzzer audio output -> vector 0x002B
        if (self.sfr[0x18] & 0xC0) != 0 {
            for _ in 0..cycles {
                if self.t1_low == 0xFF {
                    self.t1_low = self.sfr[0x1B];
                    self.sfr[0x18] |= 0x02;
                    self.buzzer_on = !self.buzzer_on;
                    if (self.sfr[0x18] & 0x01) != 0 && master_ie {
                        return Some(0x002B);
                    }
                } else {
                    self.t1_low += 1;
                }
            }
        }

        // Base timer (BTCR = $17F: bit6=BTON, bits 0/2=IE, bits 1/3=IF) -> vector 0x001B
        let prev = self.base_div;
        self.base_div = self.base_div.wrapping_add(cycles);
        if (self.sfr[0x7F] & 0x40) != 0 && (prev >> 14) != (self.base_div >> 14) {
            self.sfr[0x7F] |= 0x0A;
            if master_ie && (self.sfr[0x7F] & 0x05) != 0 {
                return Some(0x001B);
            }
        }

        None
    }

    fn render_screen(&self, fb: &mut [u32]) {
        fb.fill(0xFF93_A28A);
        let lcd_on = (self.sfr[0x27] & 0x80) != 0;
        let dot = 11usize;
        let x0 = (CANVAS_WIDTH - 48 * dot) / 2;
        let y0 = (CANVAS_HEIGHT - 32 * dot) / 2;

        for y in 0..32usize {
            let bank = if y < 16 { 0 } else { 1 };
            let local_y = y & 15;
            let base = (local_y >> 1) * 16 + (local_y & 1) * 6;
            for x in 0..48usize {
                let b_idx = base + (x / 8);
                let bit = 7 - (x & 7);
                let px_on = lcd_on && ((self.xram[bank][b_idx.min(127)] >> bit) & 1) != 0;
                let color = if px_on { 0xFF1A_241C } else { 0xFF83_927B };
                let sx = x0 + x * dot;
                let sy = y0 + y * dot;
                for dy in 1..(dot - 1) {
                    for dx in 1..(dot - 1) {
                        fb[(sy + dy) * CANVAS_WIDTH + (sx + dx)] = color;
                    }
                }
            }
        }

        for (i, label) in ICON_NAMES.iter().enumerate() {
            let active = (self.xram[2][1 + i] & 0x01) != 0;
            let fg = if active { 0xFF1A_241C } else { 0xFF7B_8873 };
            draw_string(
                fb,
                (x0 + i * 130 + 20) as i32,
                (y0 + 32 * dot + 24) as i32,
                label,
                fg,
            );
        }
    }
}

impl Lc8670Bus for VmuBus {
    fn read_code(&self, addr: u16) -> u8 {
        if (self.sfr[0x0D] & 1) != 0 {
            let bank = (self.sfr[0x54] & 1) as usize;
            let full = bank * 65_536 + (addr as usize);
            self.flash.get(full).copied().unwrap_or(0xFF)
        } else {
            self.bios[addr as usize]
        }
    }

    fn read_ram(&self, bank: u8, addr: u8) -> u8 {
        self.ram[(bank & 1) as usize][addr as usize]
    }

    fn write_ram(&mut self, bank: u8, addr: u8, val: u8) {
        self.ram[(bank & 1) as usize][addr as usize] = val;
    }

    fn read_sfr(&mut self, addr: u8) -> u8 {
        if addr >= 0x80 {
            let b = (self.sfr[0x25] & 3).min(2) as usize;
            return self.xram[b][(addr - 0x80) as usize];
        }
        match addr {
            0x12 => self.t0_low,
            0x14 => self.t0_high,
            0x1A => self.t1_low,
            0x1C => self.t1_high,
            0x66 => {
                let ptr = (((self.sfr[0x65] & 1) as usize) << 8) | (self.sfr[0x64] as usize);
                let v = self.wram[ptr];
                if (self.sfr[0x63] & 0x10) != 0 {
                    let next = (ptr + 1) & 0x1FF;
                    self.sfr[0x64] = (next & 0xFF) as u8;
                    self.sfr[0x65] = (self.sfr[0x65] & !1) | ((next >> 8) as u8);
                }
                v
            }
            _ => self.sfr[addr as usize],
        }
    }

    fn peek_sfr(&self, addr: u8) -> u8 {
        if addr >= 0x80 {
            let b = (self.sfr[0x25] & 3).min(2) as usize;
            return self.xram[b][(addr - 0x80) as usize];
        }
        if addr == 0x66 {
            let ptr = (((self.sfr[0x65] & 1) as usize) << 8) | (self.sfr[0x64] as usize);
            return self.wram[ptr];
        }
        self.sfr[addr as usize]
    }

    fn write_sfr(&mut self, addr: u8, val: u8) {
        if addr >= 0x80 {
            let b = (self.sfr[0x25] & 3).min(2) as usize;
            self.xram[b][(addr - 0x80) as usize] = val;
            return;
        }
        if addr < 0x80 {
            self.sfr[addr as usize] = val;
            match addr {
                0x13 => {
                    self.t0_low = val;
                    self.buzzer_freq = val;
                }
                0x15 => self.t0_high = val,
                0x1B => self.t1_low = val,
                0x1D => self.t1_high = val,
                0x66 => {
                    let ptr = (((self.sfr[0x65] & 1) as usize) << 8) | (self.sfr[0x64] as usize);
                    self.wram[ptr] = val;
                    if (self.sfr[0x63] & 0x10) != 0 {
                        let next = (ptr + 1) & 0x1FF;
                        self.sfr[0x64] = (next & 0xFF) as u8;
                        self.sfr[0x65] = (self.sfr[0x65] & !1) | ((next >> 8) as u8);
                    }
                }
                _ => {}
            }
        }
    }

    fn read_flash(&self, addr: u32) -> u8 {
        self.flash.get(addr as usize).copied().unwrap_or(0xFF)
    }

    fn write_flash(&mut self, addr: u32, val: u8) {
        if let Some(slot) = self.flash.get_mut(addr as usize) {
            *slot = val;
        }
    }
}

pub struct SegaVmu {
    cpu: Lc8670,
    bus: VmuBus,
    screen: Vec<u32>,
}

impl SegaVmu {
    pub fn new(rom_dir: &Path) -> EmuResult<Self> {
        let mut cpu = Lc8670::new();
        cpu.reset();
        let bus = VmuBus::new(rom_dir)?;
        Ok(Self {
            cpu,
            bus,
            screen: vec![0xFF93_A28A; CANVAS_WIDTH * CANVAS_HEIGHT],
        })
    }

    fn button_mask(key: KeyCode) -> Option<u8> {
        Some(match key {
            KeyCode::Up => 0x01,
            KeyCode::Down => 0x02,
            KeyCode::Left => 0x04,
            KeyCode::Right => 0x08,
            KeyCode::Z | KeyCode::Space | KeyCode::Return => 0x10,
            KeyCode::X | KeyCode::LShift => 0x20,
            KeyCode::Tab | KeyCode::M => 0x40,
            KeyCode::Escape => 0x80,
            _ => return None,
        })
    }
}

impl EmulatorCore for SegaVmu {
    fn system_type(&self) -> SystemType {
        SystemType::SegaVmu
    }

    fn system_name(&self) -> &str {
        "Sega Dreamcast VMU (LC8670)"
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
        let target_cycles = 10_000u32;
        let mut cycles = 0u32;
        let mut next_sample = 0u32;
        let cyc_per_sample = target_cycles / 735;

        while cycles < target_cycles {
            let c = self.cpu.step(&mut self.bus).max(1);
            cycles += c;
            if let Some(vec) = self.bus.tick(c) {
                if self.cpu.irq_depth == 0 {
                    self.cpu.trigger_interrupt(&mut self.bus, vec);
                }
            }
            while next_sample <= cycles && self.bus.audio_buf.len() < 735 {
                let s = if (self.bus.sfr[0x18] & 0x80) != 0 && self.bus.buzzer_on {
                    3500
                } else {
                    0
                };
                self.bus.audio_buf.push(s);
                next_sample += cyc_per_sample.max(1);
            }
        }
        self.bus.render_screen(&mut self.screen);
    }

    fn step_instruction(&mut self) -> u32 {
        let c = self.cpu.step(&mut self.bus).max(1);
        if let Some(vec) = self.bus.tick(c) {
            if self.cpu.irq_depth == 0 {
                self.cpu.trigger_interrupt(&mut self.bus, vec);
            }
        }
        self.bus.render_screen(&mut self.screen);
        c
    }

    fn rendered_screen(&self) -> &[u32] {
        &self.screen
    }

    fn audio_buffer(&self) -> &[i16] {
        &self.bus.audio_buf
    }

    fn handle_key_down(&mut self, key: KeyCode, _mod_state: KeyMod) {
        if let Some(mask) = Self::button_mask(key) {
            self.bus.sfr[0x4C] &= !mask;
            self.bus.sfr[0x4E] |= 0x02;
            if (self.bus.sfr[0x4E] & 0x01) != 0
                && (self.bus.sfr[0x08] & 0x80) != 0
                && self.cpu.irq_depth == 0
            {
                self.cpu.trigger_interrupt(&mut self.bus, 0x004B);
            }
        }
    }

    fn handle_key_up(&mut self, key: KeyCode, _mod_state: KeyMod) {
        if let Some(mask) = Self::button_mask(key) {
            self.bus.sfr[0x4C] |= mask;
        }
    }

    fn load_media(&mut self, path: &Path) -> EmuResult<()> {
        let bytes = fs::read(path).map_err(|e| EmuError::IoError(path.display().to_string(), e))?;
        let name = path
            .file_name()
            .and_then(|s| s.to_str())
            .unwrap_or("game.vms")
            .to_string();
        if bytes.is_empty() {
            return Err(EmuError::UnsupportedMedia(format!(
                "Empty VMU media file: {name}"
            )));
        }
        if bytes.len() == 131_072 {
            self.bus.flash = bytes;
        } else {
            self.bus.install_vms_game(&name, &bytes);
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
            .map(|i| {
                let a = start_addr.wrapping_add(i as u32) as u16;
                if a < 0x100 {
                    self.bus.read_ram((self.cpu.psw >> 1) & 1, a as u8)
                } else if a < 0x200 {
                    self.bus.peek_sfr((a - 0x100) as u8)
                } else {
                    self.bus.read_code(a)
                }
            })
            .collect()
    }

    fn disassemble(&self, start_addr: u32, count: usize) -> Vec<DisassembledInstruction> {
        self.cpu.disassemble(&self.bus, start_addr as u16, count)
    }
}
