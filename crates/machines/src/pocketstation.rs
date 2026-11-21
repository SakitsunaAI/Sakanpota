use std::fs;
use std::path::Path;

use cpu_cores::Arm7tdmi;
use emu_core::archive::load_rom_file;
use emu_core::{
    Bus32, CANVAS_HEIGHT, CANVAS_WIDTH, CpuDebugState, DisassembledInstruction, EmuError,
    EmuResult, EmulatorCore, KeyCode, KeyMod, SystemType,
};

struct PocketStationBus {
    rom: Vec<u8>,       // 16 KB Kernel ROM at 0x04000000 (mirrored at 0 until remap)
    sram: [u8; 2048],   // 2 KB Work SRAM at 0x00000000..=0x000007FF
    flash: Vec<u8>,     // 128 KB Flash at 0x08000000..=0x0801FFFF
    boot_mirrored: bool,
    // INTC (0x0A000000)
    int_latch: u32,
    int_input: u32,
    int_enable: u32,
    // Mode / Clock / IOP (0x06000000)
    iop_ctrl: u32,
    clk_ctrl: u32,
    // Timers (0x0A800000 / 0x0A000200)
    tmr_reload: [u32; 3],
    tmr_count: [u32; 3],
    tmr_ctrl: [u32; 3],
    // RTC (0x0B800000..=0x0B80000C)
    rtc_mode: u32,
    rtc_sec: u8,
    rtc_min: u8,
    rtc_hour: u8,
    rtc_dow: u8,
    rtc_day: u8,
    rtc_month: u8,
    rtc_year: u8,
    rtc_century: u8,
    rtc_cycles: u64,
    // LCD (0x0D000000 + VRAM 0x0D000100..=0x0D00017F = 32x32)
    lcd_ctrl: u32,
    lcd_cal: u32,
    lcd_vram: [u32; 32],
    // Keypad & DAC (0x0D800000)
    buttons: u32, // Bits 0..4: Action, Right, Left, Down, Up
    dac_ctrl: u32,
    dac_val: u32,
    prescale: u32,
    audio_buf: Vec<i16>,
    media_name: String,
}

impl PocketStationBus {
    fn new(rom_dir: &Path) -> EmuResult<Self> {
        let mut rom = load_rom_file(rom_dir, "pocketstation.bin")?;
        rom.resize(16_384, 0xFF);
        let mut bus = Self {
            rom,
            sram: [0; 2048],
            flash: vec![0xFF; 131_072],
            boot_mirrored: true,
            int_latch: 0,
            int_input: 0,
            int_enable: 0,
            iop_ctrl: 0,
            clk_ctrl: 0,
            tmr_reload: [0; 3],
            tmr_count: [0; 3],
            tmr_ctrl: [0; 3],
            rtc_mode: 0,
            rtc_sec: 0x00,
            rtc_min: 0x00,
            rtc_hour: 0x12,
            rtc_dow: 0x01,
            rtc_day: 0x01,
            rtc_month: 0x01,
            rtc_year: 0x99,
            rtc_century: 0x19,
            rtc_cycles: 0,
            lcd_ctrl: 0x07,
            lcd_cal: 0,
            lcd_vram: [0; 32],
            buttons: 0,
            dac_ctrl: 0,
            dac_val: 0,
            prescale: 0,
            audio_buf: Vec::with_capacity(735),
            media_name: String::from("PocketStation Kernel ROM"),
        };
        bus.reset_state();
        Ok(bus)
    }

    fn reset_state(&mut self) {
        self.sram.fill(0);
        self.boot_mirrored = true;
        self.int_latch = 0;
        self.int_input = 0;
        self.int_enable = 0;
        self.iop_ctrl = 0;
        self.clk_ctrl = 0;
        self.tmr_reload = [0; 3];
        self.tmr_count = [0; 3];
        self.tmr_ctrl = [0; 3];
        self.rtc_mode = 0;
        self.rtc_sec = 0x00;
        self.rtc_min = 0x00;
        self.rtc_hour = 0x12;
        self.rtc_dow = 0x01;
        self.rtc_day = 0x01;
        self.rtc_month = 0x01;
        self.rtc_year = 0x99;
        self.rtc_century = 0x19;
        self.rtc_cycles = 0;
        self.lcd_ctrl = 0x07;
        self.lcd_cal = 0;
        self.lcd_vram.fill(0);
        self.buttons = 0;
        self.dac_ctrl = 0;
        self.dac_val = 0;
        self.prescale = 0;
    }

    fn inc_bcd(val: u8, min: u8, max: u8) -> u8 {
        let mut dec = ((val >> 4) & 0x0F) * 10 + (val & 0x0F);
        dec = dec.saturating_add(1);
        if dec > max || dec < min {
            dec = min;
        }
        ((dec / 10) << 4) | (dec % 10)
    }

    fn read_word_io(&self, addr: u32) -> u32 {
        match addr & 0x0FFF_FFFC {
            0x0600_0000 => self.iop_ctrl,
            0x0600_0300 => self.buttons,
            0x0A00_0000 => self.int_latch & self.int_enable,
            0x0A00_0004 => self.int_input | (self.buttons & 0x1F),
            0x0A00_0008 => self.int_enable,
            0x0A00_0200 => self.tmr_reload[0],
            0x0A00_0204 => self.tmr_count[0],
            0x0A00_0208 => self.tmr_ctrl[0],
            0x0A80_0000 => self.tmr_reload[0],
            0x0A80_0004 => self.tmr_count[0],
            0x0A80_0008 => self.tmr_ctrl[0],
            0x0A80_0010 => self.tmr_reload[1],
            0x0A80_0014 => self.tmr_count[1],
            0x0A80_0018 => self.tmr_ctrl[1],
            0x0A80_0020 => self.tmr_reload[2],
            0x0A80_0024 => self.tmr_count[2],
            0x0A80_0028 => self.tmr_ctrl[2],
            0x0B00_0000 => self.clk_ctrl | 0x10,
            0x0B80_0000 => self.rtc_mode,
            0x0B80_0008 => {
                ((self.rtc_dow as u32) << 24)
                    | ((self.rtc_hour as u32) << 16)
                    | ((self.rtc_min as u32) << 8)
                    | (self.rtc_sec as u32)
            }
            0x0B80_000C => {
                ((self.rtc_century as u32) << 24)
                    | ((self.rtc_year as u32) << 16)
                    | ((self.rtc_month as u32) << 8)
                    | (self.rtc_day as u32)
            }
            0x0D00_0000 => self.lcd_ctrl,
            0x0D00_0004 => self.lcd_cal,
            a if (0x0D00_0100..=0x0D00_017C).contains(&a) => {
                self.lcd_vram[((a - 0x0D00_0100) >> 2) as usize]
            }
            0x0D80_0000 => self.dac_ctrl,
            0x0D80_0020 => self.dac_val,
            _ => 0,
        }
    }

    fn write_word_io(&mut self, addr: u32, val: u32) {
        match addr & 0x0FFF_FFFC {
            0x0600_0000 => {
                self.iop_ctrl = val;
                if (val & 1) != 0 {
                    self.boot_mirrored = false;
                }
            }
            0x0A00_0008 => self.int_enable |= val,
            0x0A00_000C => self.int_enable &= !val,
            0x0A00_0010 => self.int_latch &= !val,
            0x0A00_0200 => {
                self.tmr_reload[0] = val & 0xFFFF;
                self.tmr_count[0] = self.tmr_reload[0];
            }
            0x0A00_0208 => self.tmr_ctrl[0] = val,
            0x0A80_0000 => {
                self.tmr_reload[0] = val & 0xFFFF;
                self.tmr_count[0] = self.tmr_reload[0];
            }
            0x0A80_0008 => self.tmr_ctrl[0] = val,
            0x0A80_0010 => {
                self.tmr_reload[1] = val & 0xFFFF;
                self.tmr_count[1] = self.tmr_reload[1];
            }
            0x0A80_0018 => self.tmr_ctrl[1] = val,
            0x0A80_0020 => {
                self.tmr_reload[2] = val & 0xFFFF;
                self.tmr_count[2] = self.tmr_reload[2];
            }
            0x0A80_0028 => self.tmr_ctrl[2] = val,
            0x0B00_0000 => self.clk_ctrl = val,
            0x0B80_0000 => self.rtc_mode = val,
            0x0B80_0004 if (val & 1) != 0 => match (self.rtc_mode >> 1) & 7 {
                0 => self.rtc_sec = Self::inc_bcd(self.rtc_sec, 0, 59),
                1 => self.rtc_min = Self::inc_bcd(self.rtc_min, 0, 59),
                2 => self.rtc_hour = Self::inc_bcd(self.rtc_hour, 0, 23),
                3 => self.rtc_dow = Self::inc_bcd(self.rtc_dow, 1, 7),
                4 => self.rtc_day = Self::inc_bcd(self.rtc_day, 1, 31),
                5 => self.rtc_month = Self::inc_bcd(self.rtc_month, 1, 12),
                6 => self.rtc_year = Self::inc_bcd(self.rtc_year, 0, 99),
                _ => self.rtc_century = Self::inc_bcd(self.rtc_century, 0, 99),
            },
            0x0D00_0000 => self.lcd_ctrl = val,
            0x0D00_0004 => self.lcd_cal = val,
            a if (0x0D00_0100..=0x0D00_017C).contains(&a) => {
                self.lcd_vram[((a - 0x0D00_0100) >> 2) as usize] = val;
            }
            0x0D80_0000 => self.dac_ctrl = val,
            0x0D80_0020 => self.dac_val = val,
            _ => {}
        }
    }

    fn tick(&mut self, cycles: u32) -> bool {
        self.prescale += cycles;
        while self.prescale >= 256 {
            self.prescale -= 256;
            for t in 0..3usize {
                if (self.tmr_ctrl[t] & 0x04) != 0 {
                    if self.tmr_count[t] == 0 {
                        self.tmr_count[t] = self.tmr_reload[t];
                        let bit = if t < 2 { 7 + t } else { 13 };
                        self.int_latch |= 1 << bit;
                    } else {
                        self.tmr_count[t] -= 1;
                    }
                }
            }
        }
        let period: u64 = if (self.rtc_mode & 1) != 0 {
            256
        } else {
            1_000_000
        };
        self.rtc_cycles += cycles as u64;
        while self.rtc_cycles >= period {
            self.rtc_cycles -= period;
            self.int_input ^= 1 << 9;
            if (self.int_input & (1 << 9)) != 0 {
                self.int_latch |= 1 << 9;
                if (self.rtc_mode & 1) == 0 {
                    self.rtc_sec = Self::inc_bcd(self.rtc_sec, 0, 59);
                    if self.rtc_sec == 0 {
                        self.rtc_min = Self::inc_bcd(self.rtc_min, 0, 59);
                        if self.rtc_min == 0 {
                            self.rtc_hour = Self::inc_bcd(self.rtc_hour, 0, 23);
                        }
                    }
                }
            }
        }
        (self.int_latch & self.int_enable) != 0
    }

    fn render_screen(&self, fb: &mut [u32]) {
        fb.fill(0xFF88_9486);
        let lcd_on = self.lcd_ctrl != 0;
        // 32x32 square LCD -> 14x14 pixels per dot = 448x448 centered
        let cell = 14usize;
        let x0 = (CANVAS_WIDTH - 32 * cell) / 2;
        let y0 = (CANVAS_HEIGHT - 32 * cell) / 2;

        for y in 0..32usize {
            let row_word = self.lcd_vram[y];
            for x in 0..32usize {
                let on = lcd_on && ((row_word >> x) & 1) != 0;
                let color = if on { 0xFF14_1E18 } else { 0xFF98_A894 };
                let sx = x0 + x * cell;
                let sy = y0 + y * cell;
                for dy in 1..(cell - 1) {
                    for dx in 1..(cell - 1) {
                        fb[(sy + dy) * CANVAS_WIDTH + (sx + dx)] = color;
                    }
                }
            }
        }
    }
}

impl Bus32 for PocketStationBus {
    fn read8(&mut self, addr: u32) -> u8 {
        self.peek8(addr)
    }

    fn write8(&mut self, addr: u32, val: u8) {
        if addr < 0x0200_0000 {
            if !self.boot_mirrored {
                self.sram[(addr as usize) & 0x07FF] = val;
            }
            return;
        }
        if (0x0800_0000..=0x0801_FFFF).contains(&addr) {
            self.flash[(addr - 0x0800_0000) as usize] = val;
            return;
        }
        let aligned = addr & !3;
        let shift = (addr & 3) * 8;
        let cur = self.read_word_io(aligned);
        let next = (cur & !(0xFF << shift)) | ((val as u32) << shift);
        self.write_word_io(aligned, next);
    }

    fn peek8(&self, addr: u32) -> u8 {
        if addr < 0x0200_0000 {
            if self.boot_mirrored {
                return self.rom[(addr as usize) & 0x3FFF];
            }
            return self.sram[(addr as usize) & 0x07FF];
        }
        if (0x0400_0000..=0x04FF_FFFF).contains(&addr) {
            return self.rom[((addr - 0x0400_0000) as usize) & 0x3FFF];
        }
        if (0x0800_0000..=0x08FF_FFFF).contains(&addr) {
            return self.flash[((addr - 0x0800_0000) as usize) % self.flash.len()];
        }
        let w = self.read_word_io(addr);
        ((w >> ((addr & 3) * 8)) & 0xFF) as u8
    }
}

pub struct PocketStation {
    cpu: Arm7tdmi,
    bus: PocketStationBus,
    screen: Vec<u32>,
}

impl PocketStation {
    pub fn new(rom_dir: &Path) -> EmuResult<Self> {
        let mut cpu = Arm7tdmi::new();
        cpu.reset();
        let bus = PocketStationBus::new(rom_dir)?;
        Ok(Self {
            cpu,
            bus,
            screen: vec![0xFF88_9486; CANVAS_WIDTH * CANVAS_HEIGHT],
        })
    }
}

impl EmulatorCore for PocketStation {
    fn system_type(&self) -> SystemType {
        SystemType::PocketStation
    }

    fn system_name(&self) -> &str {
        "Sony PocketStation (ARM7TDMI)"
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
        // ~2 MHz default operating clock (~33,333 cycles per 60 Hz frame)
        let target_cycles = 33_333u32;
        let mut cyc = 0u32;
        let mut next_sample = 0u32;
        let cyc_per_sample = target_cycles / 735;

        while cyc < target_cycles {
            let step = self.cpu.step(&mut self.bus).max(1);
            cyc += step;
            if self.bus.tick(step) {
                self.cpu.trigger_irq();
            }
            while next_sample <= cyc && self.bus.audio_buf.len() < 735 {
                let amp = (((self.bus.dac_val & 0xFF) as i32) - 128) * 64;
                self.bus.audio_buf.push(amp.clamp(-32_768, 32_767) as i16);
                next_sample += cyc_per_sample.max(1);
            }
        }
        self.bus.render_screen(&mut self.screen);
    }

    fn step_instruction(&mut self) -> u32 {
        let c = self.cpu.step(&mut self.bus);
        if self.bus.tick(c) {
            self.cpu.trigger_irq();
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
        let mask = match key {
            KeyCode::Z | KeyCode::Space | KeyCode::Return => 0x01,
            KeyCode::Right => 0x02,
            KeyCode::Left => 0x04,
            KeyCode::Down => 0x08,
            KeyCode::Up => 0x10,
            _ => return,
        };
        self.bus.buttons |= mask;
        self.bus.int_latch |= mask;
    }

    fn handle_key_up(&mut self, key: KeyCode, _mod_state: KeyMod) {
        let mask = match key {
            KeyCode::Z | KeyCode::Space | KeyCode::Return => 0x01,
            KeyCode::Right => 0x02,
            KeyCode::Left => 0x04,
            KeyCode::Down => 0x08,
            KeyCode::Up => 0x10,
            _ => return,
        };
        self.bus.buttons &= !mask;
    }

    fn load_media(&mut self, path: &Path) -> EmuResult<()> {
        let bytes = fs::read(path).map_err(|e| EmuError::IoError(path.display().to_string(), e))?;
        let name = path
            .file_name()
            .and_then(|s| s.to_str())
            .unwrap_or("memcard.mc")
            .to_string();
        if bytes.is_empty() {
            return Err(EmuError::UnsupportedMedia(format!(
                "Empty PocketStation file: {name}"
            )));
        }
        let cpy = bytes.len().min(self.bus.flash.len());
        self.bus.flash[..cpy].copy_from_slice(&bytes[..cpy]);
        self.bus.media_name = name;
        self.reset();
        Ok(())
    }

    fn cpu_state(&self) -> CpuDebugState {
        self.cpu.debug_state()
    }

    fn inspect_memory(&self, start_addr: u32, len: usize) -> Vec<u8> {
        (0..len)
            .map(|i| self.bus.peek8(start_addr.wrapping_add(i as u32)))
            .collect()
    }

    fn disassemble(&self, start_addr: u32, count: usize) -> Vec<DisassembledInstruction> {
        self.cpu.disassemble(&self.bus, start_addr, count)
    }
}
