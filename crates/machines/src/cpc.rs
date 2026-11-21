use cpu_cores::Z80;
use emu_core::archive::load_rom_file;
use emu_core::types::{
    Bus8, CpuDebugState, DisassembledInstruction, EmuError, EmuResult, EmulatorCore, IoPortBus,
    KeyCode, KeyMod, SystemType, AUDIO_SAMPLE_RATE, CANVAS_HEIGHT, CANVAS_WIDTH,
};
use peripherals::{Ay8910, Crtc6845, FloppyImage, Pia8255, Upd765};
use std::fs;
use std::path::Path;

struct CpcBus {
    ram: Vec<u8>,
    os_rom: Vec<u8>,
    basic_rom: Vec<u8>,
    lower_rom_en: bool,
    upper_rom_en: bool,
    video_mode: u8,
    selected_pen: usize,
    palette: [u8; 17],
    crtc: Crtc6845,
    ppi: Pia8255,
    ay: Ay8910,
    fdc: Upd765,
    key_matrix: [u8; 10], // active low
    vsync_active: bool,
    irq_line_cnt: u16,
    irq_pending: bool,
    audio_buf: Vec<i16>,
    audio_accum: f64,
    media_name: String,
}

impl CpcBus {
    /// Amstrad CPC 32-entry hardware color lookup (standard RGB gate-array values).
    const HW_COLORS: [u32; 32] = [
        0xFF80_8080, 0xFF80_8080, 0xFF00_FF80, 0xFFFF_FF80,
        0xFF00_0080, 0xFFFF_0080, 0xFF00_8080, 0xFFFF_8080,
        0xFFFF_0080, 0xFFFF_FF80, 0xFFFF_FF00, 0xFFFF_FFFF,
        0xFFFF_0000, 0xFFFF_00FF, 0xFFFF_8000, 0xFFFF_80FF,
        0xFF00_0080, 0xFF00_FF80, 0xFF00_FF00, 0xFF00_FFFF,
        0xFF00_0000, 0xFF00_00FF, 0xFF00_8000, 0xFF00_80FF,
        0xFF80_0080, 0xFF80_FF80, 0xFF80_FF00, 0xFF80_FFFF,
        0xFF80_0000, 0xFF80_00FF, 0xFF80_8000, 0xFF80_80FF,
    ];

    fn new(rom_dir: &Path) -> EmuResult<Self> {
        let os_bytes = load_rom_file(rom_dir, "cpc464_os.rom")?;
        let basic_bytes = load_rom_file(rom_dir, "cpc464_basic.rom")?;
        let mut os_rom = vec![0u8; 16_384];
        let mut basic_rom = vec![0u8; 16_384];
        let olen = os_bytes.len().min(16_384);
        let blen = basic_bytes.len().min(16_384);
        os_rom[..olen].copy_from_slice(&os_bytes[..olen]);
        basic_rom[..blen].copy_from_slice(&basic_bytes[..blen]);

        let mut bus = Self {
            ram: vec![0; 65_536],
            os_rom,
            basic_rom,
            lower_rom_en: true,
            upper_rom_en: true,
            video_mode: 1,
            selected_pen: 0,
            palette: [0x14; 17],
            crtc: Crtc6845::new(),
            ppi: Pia8255::new(),
            ay: Ay8910::new(1_000_000.0),
            fdc: Upd765::new(),
            key_matrix: [0xFF; 10],
            vsync_active: false,
            irq_line_cnt: 0,
            irq_pending: false,
            audio_buf: Vec::with_capacity(1024),
            audio_accum: 0.0,
            media_name: "Amstrad CPC 464 BASIC 1.0".to_string(),
        };
        bus.reset_state();
        Ok(bus)
    }

    fn reset_state(&mut self) {
        self.ram.fill(0);
        self.lower_rom_en = true;
        self.upper_rom_en = true;
        self.video_mode = 1;
        self.selected_pen = 0;
        self.palette = [0x14, 0x04, 0x15, 0x1C, 0x18, 0x1D, 0x0C, 0x05, 0x0D, 0x16, 0x06, 0x17, 0x1E, 0x00, 0x1F, 0x0E, 0x04];
        self.crtc.reset();
        self.crtc.regs[1] = 40;
        self.crtc.regs[6] = 25;
        self.crtc.regs[9] = 7;
        self.crtc.regs[12] = 0x30;
        self.crtc.regs[13] = 0x00;
        self.ppi.reset();
        self.ay.reset();
        self.fdc.reset();
        self.vsync_active = false;
        self.irq_line_cnt = 0;
        self.irq_pending = false;
        self.sync_ppi_in();
    }

    fn sync_ppi_in(&mut self) {
        self.ppi.port_b_in = if self.vsync_active { 0xFF } else { 0xFE };
        let kline = (self.ppi.port_c_out & 0x0F) as usize;
        self.ay.port_a_in = if kline < 10 { self.key_matrix[kline] } else { 0xFF };
        self.ppi.port_a_in = self.ay.read_data();
    }

    fn sync_ay_from_ppi(&mut self) {
        let ctrl = (self.ppi.port_c_out >> 6) & 3;
        match ctrl {
            3 => self.ay.select_register(self.ppi.port_a_out),
            2 => self.ay.write_data(self.ppi.port_a_out),
            1 => {
                self.sync_ppi_in();
            }
            _ => {}
        }
    }

    fn render_screen(&self, out: &mut [u32]) {
        let border = Self::HW_COLORS[(self.palette[16] & 0x1F) as usize];
        out.fill(border);

        let h_chars = (self.crtc.h_displayed() as usize).clamp(1, 48);
        let v_rows = (self.crtc.v_displayed() as usize).clamp(1, 32);
        let max_sl = (self.crtc.max_scanline() as usize).clamp(1, 16);
        let start = self.crtc.start_addr() as usize;
        let bank_base = ((start >> 12) & 3) * 0x4000;

        let disp_w = (h_chars * 16).min(640);
        let disp_h = v_rows * max_sl;
        let scale_y = (CANVAS_HEIGHT / disp_h.max(1)).clamp(1, 2);
        let x0 = (CANVAS_WIDTH.saturating_sub(disp_w)) / 2;
        let y0 = (CANVAS_HEIGHT.saturating_sub(disp_h * scale_y)) / 2;

        for row in 0..v_rows {
            for ra in 0..max_sl {
                let py = y0 + (row * max_sl + ra) * scale_y;
                if py >= CANVAS_HEIGHT {
                    continue;
                }
                for col in 0..h_chars {
                    let ma = (start + row * h_chars + col) & 0x03FF;
                    for sub_byte in 0..2usize {
                        let addr = bank_base | ((ra & 7) << 11) | ((ma << 1) + sub_byte);
                        let b = self.ram[addr & 0xFFFF];
                        let px_base = x0 + col * 16 + sub_byte * 8;
                        if px_base + 8 > CANVAS_WIDTH {
                            continue;
                        }
                        match self.video_mode & 3 {
                            0 => {
                                // Mode 0: 2 pixels/byte (4x wide)
                                for p in 0..2usize {
                                    let pen = ((((b >> (7 - p)) & 1)
                                        | (((b >> (3 - p)) & 1) << 1)
                                        | (((b >> (5 - p)) & 1) << 2)
                                        | (((b >> (1 - p)) & 1) << 3))
                                        & 0x0F) as usize;
                                    let rgb = Self::HW_COLORS[(self.palette[pen] & 0x1F) as usize];
                                    for dy in 0..scale_y {
                                        let row_off = (py + dy) * CANVAS_WIDTH + px_base + p * 4;
                                        for dx in 0..4usize {
                                            out[row_off + dx] = rgb;
                                        }
                                    }
                                }
                            }
                            1 => {
                                // Mode 1: 4 pixels/byte (2x wide)
                                for p in 0..4usize {
                                    let pen = ((((b >> (7 - p)) & 1)
                                        | (((b >> (3 - p)) & 1) << 1))
                                        & 0x03) as usize;
                                    let rgb = Self::HW_COLORS[(self.palette[pen] & 0x1F) as usize];
                                    for dy in 0..scale_y {
                                        let row_off = (py + dy) * CANVAS_WIDTH + px_base + p * 2;
                                        out[row_off] = rgb;
                                        out[row_off + 1] = rgb;
                                    }
                                }
                            }
                            _ => {
                                // Mode 2: 8 pixels/byte (1x wide)
                                for p in 0..8usize {
                                    let pen = ((b >> (7 - p)) & 1) as usize;
                                    let rgb = Self::HW_COLORS[(self.palette[pen] & 0x1F) as usize];
                                    for dy in 0..scale_y {
                                        out[(py + dy) * CANVAS_WIDTH + px_base + p] = rgb;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

impl Bus8 for CpcBus {
    fn read8(&mut self, addr: u16) -> u8 {
        self.peek8(addr)
    }

    fn write8(&mut self, addr: u16, val: u8) {
        self.ram[addr as usize] = val;
    }

    fn peek8(&self, addr: u16) -> u8 {
        match addr {
            0x0000..=0x3FFF if self.lower_rom_en => self.os_rom[addr as usize],
            0xC000..=0xFFFF if self.upper_rom_en => self.basic_rom[(addr - 0xC000) as usize],
            _ => self.ram[addr as usize],
        }
    }
}

impl IoPortBus for CpcBus {
    fn port_in8(&mut self, port: u16) -> u8 {
        if (port & 0x4000) == 0 && (port & 0x0300) == 0x0300 {
            return self.crtc.read(true);
        }
        if (port & 0x0800) == 0 {
            let ppi_reg = ((port >> 8) & 3) as u8;
            self.sync_ppi_in();
            return self.ppi.read(ppi_reg);
        }
        if (port & 0x0580) == 0x0100 {
            // uPD765 FDC (0xFB7E MSR, 0xFB7F Data)
            if (port & 1) == 0 {
                return self.fdc.read_msr();
            } else {
                return self.fdc.read_data();
            }
        }
        0xFF
    }

    fn port_out8(&mut self, port: u16, val: u8) {
        // Gate Array (0x7Fxx)
        if (port & 0xC000) == 0x4000 {
            match val & 0xC0 {
                0x00 => {
                    self.selected_pen = if (val & 0x10) != 0 { 16 } else { (val & 0x0F) as usize };
                }
                0x40 => {
                    self.palette[self.selected_pen] = val & 0x1F;
                }
                0x80 => {
                    self.video_mode = val & 0x03;
                    self.lower_rom_en = (val & 0x04) == 0;
                    self.upper_rom_en = (val & 0x08) == 0;
                    if (val & 0x10) != 0 {
                        self.irq_line_cnt = 0;
                    }
                }
                _ => {}
            }
        }
        // CRTC 6845 (0xBCxx / 0xBDxx)
        if (port & 0x4000) == 0 {
            let fn_bits = (port >> 8) & 3;
            if fn_bits == 0 {
                self.crtc.write(false, val);
            } else if fn_bits == 1 {
                self.crtc.write(true, val);
            }
        }
        // PPI 8255 (0xF4xx..0xF7xx)
        if (port & 0x0800) == 0 {
            let ppi_reg = ((port >> 8) & 3) as u8;
            self.ppi.write(ppi_reg, val);
            self.sync_ay_from_ppi();
            self.sync_ppi_in();
        }
        // uPD765 FDC
        if port == 0xFA7E {
            self.fdc.motor_on = (val & 1) != 0;
        } else if (port & 0x0580) == 0x0100 && (port & 1) != 0 {
            self.fdc.write_data(val);
        }
    }
}

pub struct AmstradCpc464 {
    cpu: Z80,
    bus: CpcBus,
    screen: Vec<u32>,
}

impl AmstradCpc464 {
    pub fn new(rom_dir: &Path) -> EmuResult<Self> {
        let bus = CpcBus::new(rom_dir)?;
        let cpu = Z80::new();
        Ok(Self {
            cpu,
            bus,
            screen: vec![0xFF00_0000; CANVAS_WIDTH * CANVAS_HEIGHT],
        })
    }

    fn map_key(key: KeyCode) -> Option<(usize, u8)> {
        Some(match key {
            KeyCode::Up => (0, 0x01),
            KeyCode::Right => (0, 0x02),
            KeyCode::Down => (0, 0x04),
            KeyCode::F9 => (0, 0x08),
            KeyCode::F6 => (0, 0x10),
            KeyCode::F3 => (0, 0x20),
            KeyCode::Return => (2, 0x04),
            KeyCode::Left => (1, 0x01),
            KeyCode::F7 => (1, 0x04),
            KeyCode::F8 => (1, 0x08),
            KeyCode::F5 => (1, 0x10),
            KeyCode::F1 => (1, 0x20),
            KeyCode::F2 => (1, 0x40),
            KeyCode::F4 => (2, 0x10),
            KeyCode::LShift | KeyCode::RShift => (2, 0x20),
            KeyCode::LCtrl | KeyCode::RCtrl => (2, 0x80),
            KeyCode::Num0 => (4, 0x01),
            KeyCode::Num9 => (4, 0x02),
            KeyCode::O => (4, 0x04),
            KeyCode::I => (4, 0x08),
            KeyCode::L => (4, 0x10),
            KeyCode::K => (4, 0x20),
            KeyCode::M => (4, 0x40),
            KeyCode::Comma => (4, 0x80),
            KeyCode::Num8 => (5, 0x01),
            KeyCode::Num7 => (5, 0x02),
            KeyCode::U => (5, 0x04),
            KeyCode::Y => (5, 0x08),
            KeyCode::H => (5, 0x10),
            KeyCode::J => (5, 0x20),
            KeyCode::N => (5, 0x40),
            KeyCode::Space => (5, 0x80),
            KeyCode::Num6 => (6, 0x01),
            KeyCode::Num5 => (6, 0x02),
            KeyCode::R => (6, 0x04),
            KeyCode::T => (6, 0x08),
            KeyCode::G => (6, 0x10),
            KeyCode::F => (6, 0x20),
            KeyCode::B => (6, 0x40),
            KeyCode::V => (6, 0x80),
            KeyCode::Num4 => (7, 0x01),
            KeyCode::Num3 => (7, 0x02),
            KeyCode::E => (7, 0x04),
            KeyCode::W => (7, 0x08),
            KeyCode::S => (7, 0x10),
            KeyCode::D => (7, 0x20),
            KeyCode::C => (7, 0x40),
            KeyCode::X => (7, 0x80),
            KeyCode::Num1 => (8, 0x01),
            KeyCode::Num2 => (8, 0x02),
            KeyCode::Escape => (8, 0x04),
            KeyCode::Q => (8, 0x08),
            KeyCode::Tab => (8, 0x10),
            KeyCode::A => (8, 0x20),
            KeyCode::CapsLock => (8, 0x40),
            KeyCode::Z => (8, 0x80),
            KeyCode::Backspace => (9, 0x80),
            _ => return None,
        })
    }
}

impl EmulatorCore for AmstradCpc464 {
    fn system_type(&self) -> SystemType {
        SystemType::AmstradCpc464
    }

    fn system_name(&self) -> &str {
        "Amstrad CPC 464 (Z80 + CRTC + AY-3-8912)"
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
        // 312 scanlines @ 256 Z80 T-states = 79,872 cycles/frame (4 MHz / 50 Hz).
        for line in 0..312u16 {
            self.bus.vsync_active = (272..276).contains(&line);
            self.bus.sync_ppi_in();
            let mut cyc = 0u32;
            while cyc < 256 {
                let c = self.cpu.step(&mut self.bus);
                cyc += c;
                self.bus.audio_accum += (c as f64 / 4_000_000.0) * AUDIO_SAMPLE_RATE as f64;
                while self.bus.audio_accum >= 1.0 {
                    self.bus.audio_accum -= 1.0;
                    let s = self.bus.ay.next_sample(AUDIO_SAMPLE_RATE as f64);
                    self.bus.audio_buf.push(s);
                }
            }
            self.bus.irq_line_cnt += 1;
            if self.bus.irq_line_cnt >= 52 {
                self.bus.irq_line_cnt = 0;
                self.cpu.request_irq(0xFF);
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
        if let Some((line, mask)) = Self::map_key(key) {
            self.bus.key_matrix[line] &= !mask;
            self.bus.sync_ppi_in();
        }
    }

    fn handle_key_up(&mut self, key: KeyCode, _mods: KeyMod) {
        if let Some((line, mask)) = Self::map_key(key) {
            self.bus.key_matrix[line] |= mask;
            self.bus.sync_ppi_in();
        }
    }

    fn load_media(&mut self, path: &Path) -> EmuResult<()> {
        let bytes = fs::read(path).map_err(|e| EmuError::IoError(path.display().to_string(), e))?;
        let name = path
            .file_name()
            .and_then(|s| s.to_str())
            .unwrap_or("cpc.dsk")
            .to_string();
        if let Some(dsk) = FloppyImage::from_cpc_dsk(&name, &bytes) {
            self.bus.fdc.disk = Some(dsk);
            self.bus.media_name = name;
            return Ok(());
        }
        Err(EmuError::UnsupportedMedia(format!("Expected CPC .dsk image: {name}")))
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
