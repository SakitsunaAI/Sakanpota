use cpu_cores::Mos6502;
use emu_core::archive::{load_rom_file, read_media_or_zip};
use emu_core::types::{
    BbcModel, Bus8, CpuDebugState, DisassembledInstruction, EmuError, EmuResult, EmulatorCore,
    KeyCode, KeyMod, SystemType, AUDIO_SAMPLE_RATE, CANVAS_HEIGHT, CANVAS_WIDTH,
};
use peripherals::{Crtc6845, FloppyImage, I8271, Saa5050, Sn76489, Via6522, Wd1770};
use std::collections::{HashMap, VecDeque};
use std::path::{Path, PathBuf};

struct BbcBus {
    model: BbcModel,
    ram: Vec<u8>,
    hazel_ram: Vec<u8>,
    os_rom: Vec<u8>,
    sideways_roms: Vec<Vec<u8>>,
    rom_select: u8,
    acccon: u8,
    vdu_code_pc: bool,
    cmos_ram: [u8; 64],
    cmos_addr: u8,
    prev_orb: u8,
    sys_via: Via6522,
    user_via: Via6522,
    crtc: Crtc6845,
    saa5050: Saa5050,
    sn: Sn76489,
    fdc_8271: I8271,
    fdc_1770: Wd1770,
    ic32: u8,
    video_ula_ctrl: u8,
    palette: [u8; 16],
    /// 8 rows x 10 cols BBC keyboard matrix (true = pressed).
    key_matrix: [[bool; 16]; 8],
    disc_dir: PathBuf,
    audio_buf: Vec<i16>,
    audio_accum: f64,
    media_name: String,
}

impl BbcBus {
    const PHYS_COLORS: [u32; 8] = [
        0xFF00_0000, // 0 Black
        0xFFFF_0000, // 1 Red
        0xFF00_FF00, // 2 Green
        0xFFFF_FF00, // 3 Yellow
        0xFF00_00FF, // 4 Blue
        0xFFFF_00FF, // 5 Magenta
        0xFF00_FFFF, // 6 Cyan
        0xFFFF_FFFF, // 7 White
    ];

    fn new(model: BbcModel, rom_dir: &Path) -> EmuResult<Self> {
        let mut cmos_ram = [0u8; 64];
        cmos_ram[0x0A] = 0x26;
        cmos_ram[0x0B] = 0x02;
        cmos_ram[0x0C] = 0x00;
        cmos_ram[0x0D] = 0x80; // MC146818 VRT = 1 valid battery bit
        cmos_ram[0x13] = 0xC9; // Language slot 12 (BASIC IV) in high nibble, Filing System 9 (DFS) in low nibble
        cmos_ram[0x14] = 0xFF; // Enable sideways ROM slots 0..7
        cmos_ram[0x15] = 0xFF; // Enable sideways ROM slots 8..15 (DFS, BASIC IV, etc.)
        cmos_ram[0x18] = 0x07; // Startup Screen Mode 7 (read directly at 0x8E76 via LDX #$18)
        cmos_ram[0x1C] = 0xDF;
        cmos_ram[0x1D] = 0xFF;
        cmos_ram[0x1E] = 0xEF; // NoBoot default (bit 4 clear); SHIFT+BREAK boots discs
        cmos_ram[0x1F] = 0xDF;
        cmos_ram[0x20] = 0x0C;
        cmos_ram[0x21] = 0xDF;

        let disc_dir = rom_dir
            .parent()
            .map(|p| p.join("discs"))
            .filter(|p| p.is_dir())
            .unwrap_or_else(|| PathBuf::from("discs"));

        let mut bus = Self {
            model,
            ram: vec![0; 131_072],
            hazel_ram: vec![0; 8192],
            os_rom: vec![0; 16_384],
            sideways_roms: vec![vec![0xFF; 16_384]; 16],
            rom_select: 15,
            acccon: 0,
            vdu_code_pc: false,
            cmos_ram,
            cmos_addr: 0,
            prev_orb: 0xFF,
            sys_via: Via6522::new(),
            user_via: Via6522::new(),
            crtc: Crtc6845::new(),
            saa5050: Saa5050::new(),
            sn: Sn76489::new(4_000_000.0),
            fdc_8271: I8271::new(),
            fdc_1770: Wd1770::new(),
            ic32: 0xFF,
            video_ula_ctrl: 0x4B,
            palette: [0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 2, 3, 4, 5, 6, 7],
            key_matrix: [[false; 16]; 8],
            disc_dir,
            audio_buf: Vec::with_capacity(1024),
            audio_accum: 0.0,
            media_name: model.display_name().to_string(),
        };
        bus.load_firmware(rom_dir)?;
        bus.reset_state();
        Ok(bus)
    }

    fn load_firmware(&mut self, rom_dir: &Path) -> EmuResult<()> {
        match self.model {
            BbcModel::Master128 => {
                if let Ok(mos) = load_rom_file(rom_dir, "mos3.20") {
                    if mos.len() >= 16_384 {
                        self.os_rom.copy_from_slice(&mos[..16_384]);
                        // Master 128 mos3.20 stores 16KB sideways ROM banks 9..15 after the MOS chunk.
                        for i in 1..8usize {
                            let off = i * 16_384;
                            if off + 16_384 <= mos.len() {
                                let slot = 8 + i;
                                self.sideways_roms[slot].copy_from_slice(&mos[off..off + 16_384]);
                            }
                        }
                        return Ok(());
                    }
                }
                let os = load_rom_file(rom_dir, "os.rom")?;
                let len = os.len().min(16_384);
                self.os_rom[..len].copy_from_slice(&os[..len]);
            }
            BbcModel::ModelBPlus => {
                let os = load_rom_file(rom_dir, "bpos.rom")
                    .or_else(|_| load_rom_file(rom_dir, "os.rom"))?;
                let len = os.len().min(16_384);
                self.os_rom[..len].copy_from_slice(&os[..len]);
            }
            BbcModel::ModelA | BbcModel::ModelB => {
                let os = load_rom_file(rom_dir, "os.rom")
                    .or_else(|_| load_rom_file(rom_dir, "os01.rom"))?;
                let len = os.len().min(16_384);
                self.os_rom[..len].copy_from_slice(&os[..len]);
            }
        }

        if let Ok(basic) = load_rom_file(rom_dir, "BASIC.ROM") {
            let len = basic.len().min(16_384);
            self.sideways_roms[15][..len].copy_from_slice(&basic[..len]);
            if len <= 8_192 {
                self.sideways_roms[15][8_192..8_192 + len].copy_from_slice(&basic[..len]);
            }
        }
        if self.model != BbcModel::ModelA {
            if let Ok(dfs) = load_rom_file(rom_dir, "DFS-0.9.rom") {
                let len = dfs.len().min(16_384);
                self.sideways_roms[14][..len].copy_from_slice(&dfs[..len]);
                if len <= 8_192 {
                    self.sideways_roms[14][8_192..8_192 + len].copy_from_slice(&dfs[..len]);
                }
            }
        }
        Ok(())
    }

    fn reset_state(&mut self) {
        self.ram.fill(0);
        self.hazel_ram.fill(0);
        self.rom_select = 15;
        self.acccon = 0;
        self.vdu_code_pc = false;
        self.cmos_addr = 0;
        self.prev_orb = 0xFF;
        self.sys_via.reset();
        self.user_via.reset();
        self.crtc.reset();
        self.saa5050 = Saa5050::new();
        self.sn.reset();
        self.fdc_8271.reset();
        self.fdc_1770.reset();
        self.ic32 = 0xFF;
        self.video_ula_ctrl = 0x4B;
        self.palette = [0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 2, 3, 4, 5, 6, 7];
        self.sys_via.irb = 0xFF;
        self.sys_via.ira = 0xFF;
    }

    fn update_master_rtc(&mut self, prev_orb: u8) {
        if self.model != BbcModel::Master128 {
            return;
        }
        let orb = self.sys_via.orb;
        if (orb & 0x80) != 0 || (prev_orb & 0x80) != 0 {
            self.cmos_addr = self.sys_via.ora & 0x3F;
        }
        if (orb & 0x40) != 0 && (self.ic32 & 0x04) != 0 {
            let idx = (self.cmos_addr & 0x3F) as usize;
            if (self.ic32 & 0x02) != 0 {
                self.sys_via.ira = self.cmos_ram[idx];
            } else if self.sys_via.ddra == 0xFF {
                self.cmos_ram[idx] = self.sys_via.ora;
            }
        }
    }

    fn update_keyboard_signals(&mut self) {
        let kb_scan = (self.ic32 & 0x08) == 0;
        let ora = self.sys_via.port_a_pins();
        if kb_scan {
            let col = (ora & 0x0F) as usize;
            let row = ((ora >> 4) & 0x07) as usize;
            let pressed = self
                .key_matrix
                .get(row)
                .and_then(|r| r.get(col))
                .copied()
                .unwrap_or(false);
            // Bit 7 reflects pressed key in (row, col); CA2 goes active if any key in col (rows 1..7)
            let any_in_col = (1..8).any(|r| self.key_matrix[r][col]);
            self.sys_via.ira = (ora & 0x7F) | if pressed { 0x80 } else { 0 };
            self.sys_via.set_ca2(any_in_col);
        } else {
            // Auto-scan mode: CA2 goes active if any non-DIP key (rows 1..7) is pressed
            let any_pressed = (1..8).any(|r| self.key_matrix[r].iter().take(10).any(|&k| k));
            self.sys_via.set_ca2(any_pressed);
        }
    }

    fn write_ic32(&mut self, orb_val: u8) {
        let bit = orb_val & 0x07;
        let level = (orb_val & 0x08) != 0;
        let old = self.ic32;
        if level {
            self.ic32 |= 1 << bit;
        } else {
            self.ic32 &= !(1 << bit);
        }
        // Sound write strobe (active low bit 0)
        if bit == 0 && !level && (old & 1) != 0 {
            let sound_byte = self.sys_via.port_a_pins();
            self.sn.write(sound_byte);
        }
        self.update_keyboard_signals();
    }

    #[inline]
    fn screen_wrap_size(&self) -> usize {
        match (self.ic32 >> 4) & 3 {
            0 => 0x4000, // 16KB Mode 3
            1 => 0x2000, // 8KB Mode 6
            2 => 0x2800, // 10KB Mode 4/5
            _ => 0x5000, // 20KB Mode 0/1/2
        }
    }

    fn render_screen(&self, out: &mut [u32]) {
        // Teletext Mode 7 when Video ULA bit 1 == 1
        if (self.video_ula_ctrl & 0x02) != 0 {
            let base = if self.model == BbcModel::ModelA {
                0x3C00
            } else if (self.model == BbcModel::Master128 && (self.acccon & 0x01) != 0)
                || (self.model == BbcModel::ModelBPlus && (self.acccon & 0x80) != 0)
            {
                0x1_0000 + 0x7C00
            } else {
                0x7C00
            };
            let vram_slice = &self.ram[base..base + 1000];
            let c_addr = self.crtc.cursor_addr();
            let cursor_off = if (c_addr & 0x2000) != 0 {
                Some((c_addr & 0x03FF) as usize)
            } else {
                None
            };
            self.saa5050
                .render_screen(vram_slice, out, CANVAS_WIDTH, CANVAS_HEIGHT, cursor_off);
            return;
        }

        out.fill(0xFF00_0000);
        let h_chars = (self.crtc.h_displayed() as usize).clamp(1, 80);
        let v_rows = (self.crtc.v_displayed() as usize).clamp(1, 39);
        let scanlines_per_row = (self.crtc.max_scanline() as usize).clamp(1, 16);
        let shift_code = ((self.video_ula_ctrl >> 2) & 3)
            + if (self.video_ula_ctrl & 0x10) == 0 {
                1
            } else {
                0
            };
        let px_per_byte = match shift_code {
            3 => 8usize,
            2 => 4usize,
            _ => 2usize,
        };
        let src_w = h_chars * px_per_byte;
        let src_h = v_rows * scanlines_per_row;

        let x_scale = (CANVAS_WIDTH / src_w.max(1)).max(1);
        let y_scale = (CANVAS_HEIGHT / src_h.max(1)).max(1);
        let dest_w = src_w * x_scale;
        let dest_h = src_h * y_scale;
        let x0 = (CANVAS_WIDTH.saturating_sub(dest_w)) / 2;
        let y0 = (CANVAS_HEIGHT.saturating_sub(dest_h)) / 2;

        let wrap = self.screen_wrap_size();
        let start_ma = self.crtc.start_addr() as usize;
        let ram_offset = if (self.model == BbcModel::Master128 && (self.acccon & 0x01) != 0)
            || (self.model == BbcModel::ModelBPlus && (self.acccon & 0x80) != 0)
        {
            0x1_0000
        } else {
            0
        };
        let ram_mask = if self.model == BbcModel::ModelA {
            0x3FFF
        } else {
            0x7FFF
        };

        for row in 0..v_rows {
            for ra in 0..scanlines_per_row {
                let sy = y0 + (row * scanlines_per_row + ra) * y_scale;
                if sy >= CANVAS_HEIGHT {
                    continue;
                }
                let row_ma = start_ma + row * h_chars;
                for col in 0..h_chars {
                    let ma = (row_ma + col) & 0x1FFF;
                    let mut addr = (ma << 3) | (ra & 7);
                    while addr >= 0x8000 {
                        addr -= wrap;
                    }
                    let byte = if ra >= 8 {
                        0
                    } else {
                        self.ram[ram_offset + (addr & ram_mask)]
                    };

                    for px in 0..px_per_byte {
                        let log_col = match px_per_byte {
                            8 => {
                                if ((byte >> (7 - px)) & 1) != 0 {
                                    8
                                } else {
                                    0
                                }
                            }
                            4 => {
                                let shift = 3 - px;
                                ((((byte >> (shift + 4)) & 1) << 3) | (((byte >> shift) & 1) << 1))
                                    as usize
                            }
                            _ => {
                                let shift = 1 - px;
                                ((((byte >> (shift + 6)) & 1) << 3)
                                    | (((byte >> (shift + 4)) & 1) << 2)
                                    | (((byte >> (shift + 2)) & 1) << 1)
                                    | ((byte >> shift) & 1))
                                    as usize
                            }
                        };
                        let pal_val = self.palette[log_col & 0xF];
                        let rgb = if (pal_val & 0x08) != 0 && ((self.saa5050.flash_frame / 24) & 1) != 0 {
                            Self::PHYS_COLORS[(pal_val & 7) as usize ^ 7]
                        } else {
                            Self::PHYS_COLORS[(pal_val & 7) as usize]
                        };
                        let sx = x0 + (col * px_per_byte + px) * x_scale;
                        if sx >= CANVAS_WIDTH {
                            continue;
                        }
                        for dy in 0..y_scale {
                            if sy + dy < CANVAS_HEIGHT {
                                let base = (sy + dy) * CANVAS_WIDTH + sx;
                                for dx in 0..x_scale {
                                    if sx + dx < CANVAS_WIDTH {
                                        out[base + dx] = rgb;
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

impl Bus8 for BbcBus {
    fn read8(&mut self, addr: u16) -> u8 {
        match addr {
            0x0000..=0x2FFF => self.ram[addr as usize],
            0x3000..=0x7FFF => {
                if self.model == BbcModel::ModelA {
                    self.ram[(addr & 0x3FFF) as usize]
                } else if (self.model == BbcModel::Master128
                    && ((self.acccon & 0x04) != 0
                        || (self.vdu_code_pc && (self.acccon & 0x02) != 0)))
                    || (self.model == BbcModel::ModelBPlus
                        && self.vdu_code_pc
                        && (self.acccon & 0x80) != 0)
                {
                    self.ram[0x1_0000 + addr as usize]
                } else {
                    self.ram[addr as usize]
                }
            }
            0x8000..=0xBFFF => {
                if self.model == BbcModel::Master128
                    && addr < 0x9000
                    && (self.rom_select & 0x80) != 0
                {
                    self.ram[0x1_0000 + (addr as usize - 0x8000)]
                } else if self.model == BbcModel::ModelBPlus
                    && addr < 0xB000
                    && (self.rom_select & 0x80) != 0
                {
                    self.ram[0x1_0000 + (addr as usize)]
                } else {
                    let slot = (self.rom_select & 0x0F) as usize;
                    self.sideways_roms[slot][(addr - 0x8000) as usize]
                }
            }
            0xC000..=0xDFFF if self.model == BbcModel::Master128 && (self.acccon & 0x08) != 0 => {
                self.hazel_ram[(addr - 0xC000) as usize]
            }
            0xC000..=0xFBFF | 0xFF00..=0xFFFF => self.os_rom[(addr - 0xC000) as usize],
            0xFC00..=0xFEFF => match addr {
                0xFE00..=0xFE07 => self.crtc.read((addr & 1) != 0),
                0xFE08..=0xFE0F => {
                    // 6850 ACIA status/data
                    if (addr & 1) == 0 { 0x02 } else { 0x00 }
                }
                0xFE20..=0xFE2F => {
                    if self.model == BbcModel::Master128 && (0xFE28..=0xFE2F).contains(&addr) {
                        self.fdc_1770.read((addr & 3) as u8)
                    } else {
                        0xFF
                    }
                }
                0xFE30..=0xFE3F => {
                    if (self.model == BbcModel::Master128 || self.model == BbcModel::ModelBPlus)
                        && (addr & 0xF) >= 4
                        && (addr & 0xF) <= 7
                    {
                        self.acccon
                    } else {
                        self.rom_select
                    }
                }
                0xFE40..=0xFE5F => {
                    if (addr & 0xF) == 1 || (addr & 0xF) == 0xF {
                        self.update_keyboard_signals();
                        self.update_master_rtc(self.prev_orb);
                    }
                    self.sys_via.read((addr & 0xF) as u8)
                }
                0xFE60..=0xFE7F => self.user_via.read((addr & 0xF) as u8),
                0xFE80..=0xFE9F => match self.model {
                    BbcModel::Master128 => 0xFE,
                    _ => self.fdc_8271.read((addr & 7) as u8),
                },
                _ => 0xFE,
            },
        }
    }

    fn write8(&mut self, addr: u16, val: u8) {
        match addr {
            0x0000..=0x2FFF => self.ram[addr as usize] = val,
            0x3000..=0x7FFF => {
                if self.model == BbcModel::ModelA {
                    self.ram[(addr & 0x3FFF) as usize] = val;
                } else if (self.model == BbcModel::Master128
                    && ((self.acccon & 0x04) != 0
                        || (self.vdu_code_pc && (self.acccon & 0x02) != 0)))
                    || (self.model == BbcModel::ModelBPlus
                        && self.vdu_code_pc
                        && (self.acccon & 0x80) != 0)
                {
                    self.ram[0x1_0000 + addr as usize] = val;
                } else {
                    self.ram[addr as usize] = val;
                }
            }
            0x8000..=0xBFFF => {
                if self.model == BbcModel::Master128
                    && addr < 0x9000
                    && (self.rom_select & 0x80) != 0
                {
                    self.ram[0x1_0000 + (addr as usize - 0x8000)] = val;
                } else if self.model == BbcModel::ModelBPlus
                    && addr < 0xB000
                    && (self.rom_select & 0x80) != 0
                {
                    self.ram[0x1_0000 + addr as usize] = val;
                }
            }
            0xC000..=0xDFFF if self.model == BbcModel::Master128 && (self.acccon & 0x08) != 0 => {
                self.hazel_ram[(addr - 0xC000) as usize] = val;
            }
            0xFE00..=0xFE07 => self.crtc.write((addr & 1) != 0, val),
            0xFE20..=0xFE2F => {
                if self.model == BbcModel::Master128 && (0xFE24..=0xFE27).contains(&addr) {
                    self.fdc_1770.side = if (val & 0x10) != 0 { 1 } else { 0 };
                } else if self.model == BbcModel::Master128 && (0xFE28..=0xFE2F).contains(&addr) {
                    self.fdc_1770.write((addr & 3) as u8, val);
                } else if (addr & 1) == 0 {
                    self.video_ula_ctrl = val;
                } else {
                    let log_c = ((val >> 4) & 0xF) as usize;
                    let phys_c = (val & 0xF) ^ 0x7;
                    self.palette[log_c] = phys_c;
                }
            }
            0xFE30..=0xFE3F => {
                if (self.model == BbcModel::Master128 || self.model == BbcModel::ModelBPlus)
                    && (addr & 0xF) >= 4
                    && (addr & 0xF) <= 7
                {
                    self.acccon = val;
                } else {
                    self.rom_select = val;
                }
            }
            0xFE40..=0xFE5F => {
                let r = (addr & 0x0F) as u8;
                let prev_orb = self.sys_via.orb;
                self.sys_via.write(r, val);
                if r == 0 || r == 2 {
                    self.write_ic32(self.sys_via.port_b_pins());
                    self.update_master_rtc(prev_orb);
                    self.prev_orb = self.sys_via.orb;
                } else if r == 1 || r == 3 || r == 0xF {
                    self.update_keyboard_signals();
                    self.update_master_rtc(prev_orb);
                }
            }
            0xFE60..=0xFE7F => self.user_via.write((addr & 0x0F) as u8, val),
            0xFE80..=0xFE9F => match self.model {
                BbcModel::Master128 => {}
                _ => self.fdc_8271.write((addr & 7) as u8, val),
            },
            _ => {}
        }
    }

    fn peek8(&self, addr: u16) -> u8 {
        match addr {
            0x0000..=0x7FFF => {
                let a = if self.model == BbcModel::ModelA {
                    addr & 0x3FFF
                } else {
                    addr
                };
                self.ram[a as usize]
            }
            0x8000..=0xBFFF => {
                if self.model == BbcModel::Master128
                    && addr < 0x9000
                    && (self.rom_select & 0x80) != 0
                {
                    self.ram[0x1_0000 + (addr as usize - 0x8000)]
                } else if self.model == BbcModel::ModelBPlus
                    && addr < 0xB000
                    && (self.rom_select & 0x80) != 0
                {
                    self.ram[0x1_0000 + addr as usize]
                } else {
                    self.sideways_roms[(self.rom_select & 0x0F) as usize][(addr - 0x8000) as usize]
                }
            }
            0xFE40..=0xFE5F => self.sys_via.peek((addr & 0xF) as u8),
            0xFE60..=0xFE7F => self.user_via.peek((addr & 0xF) as u8),
            0xC000..=0xDFFF if self.model == BbcModel::Master128 && (self.acccon & 0x08) != 0 => {
                self.hazel_ram[(addr - 0xC000) as usize]
            }
            0xC000..=0xFFFF => self.os_rom[(addr - 0xC000) as usize],
        }
    }
}

pub struct BbcMicro {
    cpu: Mos6502,
    bus: BbcBus,
    screen: Vec<u32>,
    pressed_coords: HashMap<KeyCode, Vec<(usize, usize)>>,
    key_refs: [[u8; 16]; 8],
    auto_type_queue: VecDeque<(Vec<(usize, usize)>, u8)>,
    auto_type_active: Vec<(usize, usize)>,
    shift_break_held: bool,
    disc_list: Vec<PathBuf>,
    disc_index: usize,
}

impl BbcMicro {
    pub fn new(model: BbcModel, rom_dir: &Path) -> EmuResult<Self> {
        let mut bus = BbcBus::new(model, rom_dir)?;
        let is_65c02 = matches!(model, BbcModel::Master128);
        let mut cpu = Mos6502::new(is_65c02);
        cpu.reset(&mut bus);
        let disc_list = Self::scan_disc_dir(&bus.disc_dir);
        let mut m = Self {
            cpu,
            bus,
            screen: vec![0xFF00_0000; CANVAS_WIDTH * CANVAS_HEIGHT],
            pressed_coords: HashMap::new(),
            key_refs: [[0; 16]; 8],
            auto_type_queue: VecDeque::new(),
            auto_type_active: Vec::new(),
            shift_break_held: false,
            disc_list,
            disc_index: 0,
        };
        if model == BbcModel::Master128 {
            for _ in 0..20 {
                m.run_frame();
            }
        }
        Ok(m)
    }

    fn scan_disc_dir(dir: &Path) -> Vec<PathBuf> {
        let mut list = Vec::new();
        if let Ok(entries) = std::fs::read_dir(dir) {
            for entry in entries.flatten() {
                let p = entry.path();
                if let Some(ext) = p.extension().and_then(|s| s.to_str()) {
                    if matches!(ext.to_ascii_lowercase().as_str(), "ssd" | "dsd") {
                        list.push(p);
                    }
                }
            }
        }
        list.sort();
        list
    }

    fn sync_key_matrix(&mut self) {
        self.bus.key_matrix = [[false; 16]; 8];
        for (r, row) in self.key_refs.iter().enumerate() {
            for (c, &cnt) in row.iter().enumerate() {
                if cnt > 0 {
                    self.bus.key_matrix[r][c] = true;
                }
            }
        }
        for &(r, c) in &self.auto_type_active {
            if r < 8 && c < 16 {
                self.bus.key_matrix[r][c] = true;
            }
        }
        if self.shift_break_held {
            self.bus.key_matrix[0][0] = true;
        }
        self.bus.update_keyboard_signals();
    }

    fn queue_auto_type(&mut self, text: &str) {
        for ch in text.chars() {
            let coords: Vec<(usize, usize)> = match ch {
                '*' => vec![(0, 0), (4, 8)],
                ':' => vec![(4, 8)],
                ';' => vec![(5, 7)],
                '\r' | '\n' => vec![(4, 9)],
                ' ' => vec![(6, 2)],
                '"' => vec![(0, 0), (3, 1)],
                '.' => vec![(6, 7)],
                ',' => vec![(6, 6)],
                '-' => vec![(1, 7)],
                '=' => vec![(0, 0), (1, 7)],
                'A'..='Z' => {
                    if let Some(k) = Self::char_to_keycode(ch) {
                        Self::map_key(k).into_iter().collect()
                    } else {
                        Vec::new()
                    }
                }
                '0'..='9' => {
                    if let Some(k) = Self::char_to_keycode(ch) {
                        Self::map_key(k).into_iter().collect()
                    } else {
                        Vec::new()
                    }
                }
                _ => Vec::new(),
            };
            if !coords.is_empty() {
                self.auto_type_queue.push_back((coords, 3));
                self.auto_type_queue.push_back((Vec::new(), 3));
            }
        }
    }

    fn char_to_keycode(ch: char) -> Option<KeyCode> {
        Some(match ch {
            'A' => KeyCode::A,
            'B' => KeyCode::B,
            'C' => KeyCode::C,
            'D' => KeyCode::D,
            'E' => KeyCode::E,
            'F' => KeyCode::F,
            'G' => KeyCode::G,
            'H' => KeyCode::H,
            'I' => KeyCode::I,
            'J' => KeyCode::J,
            'K' => KeyCode::K,
            'L' => KeyCode::L,
            'M' => KeyCode::M,
            'N' => KeyCode::N,
            'O' => KeyCode::O,
            'P' => KeyCode::P,
            'Q' => KeyCode::Q,
            'R' => KeyCode::R,
            'S' => KeyCode::S,
            'T' => KeyCode::T,
            'U' => KeyCode::U,
            'V' => KeyCode::V,
            'W' => KeyCode::W,
            'X' => KeyCode::X,
            'Y' => KeyCode::Y,
            'Z' => KeyCode::Z,
            '0' => KeyCode::Num0,
            '1' => KeyCode::Num1,
            '2' => KeyCode::Num2,
            '3' => KeyCode::Num3,
            '4' => KeyCode::Num4,
            '5' => KeyCode::Num5,
            '6' => KeyCode::Num6,
            '7' => KeyCode::Num7,
            '8' => KeyCode::Num8,
            '9' => KeyCode::Num9,
            _ => return None,
        })
    }

    /// Performs a BBC Micro SHIFT+BREAK warm boot while retaining any inserted floppy image.
    pub fn reset_with_shift_break(&mut self) {
        self.auto_type_queue.clear();
        self.auto_type_active.clear();
        self.shift_break_held = true;
        self.sync_key_matrix();
        self.reset();
        for _ in 0..25 {
            self.run_frame();
        }
        self.shift_break_held = false;
        self.sync_key_matrix();
        for _ in 0..35 {
            self.run_frame();
        }
    }

    fn step_one(&mut self) -> u32 {
        self.bus.vdu_code_pc =
            (0xC000..=0xDFFF).contains(&self.cpu.pc) && (self.bus.acccon & 0x08) == 0;
        let c = self.cpu.step(&mut self.bus);
        // System/User VIA run at 1 MHz (half CPU clock)
        let via_cycles = c.div_ceil(2);
        self.bus.sys_via.tick(via_cycles);
        self.bus.user_via.tick(via_cycles);
        if self.bus.sys_via.irq_active() || self.bus.user_via.irq_active() {
            self.cpu.trigger_irq();
        }
        if self.bus.fdc_8271.tick(c) || self.bus.fdc_1770.tick(c) {
            self.cpu.trigger_nmi();
        }
        self.bus.audio_accum += (c as f64 / 2_000_000.0) * AUDIO_SAMPLE_RATE as f64;
        while self.bus.audio_accum >= 1.0 {
            self.bus.audio_accum -= 1.0;
            let s = self.bus.sn.next_sample(AUDIO_SAMPLE_RATE as f64);
            self.bus.audio_buf.push(s);
        }
        c
    }

    fn map_key(key: KeyCode) -> Option<(usize, usize)> {
        Some(match key {
            KeyCode::LShift | KeyCode::RShift => (0, 0),
            KeyCode::LCtrl | KeyCode::RCtrl => (0, 1),
            KeyCode::Q => (1, 0),
            KeyCode::Num3 => (1, 1),
            KeyCode::Num4 => (1, 2),
            KeyCode::Num5 => (1, 3),
            KeyCode::F4 => (1, 4),
            KeyCode::Num8 => (1, 5),
            KeyCode::F7 => (1, 6),
            KeyCode::Minus => (1, 7),
            KeyCode::F10 => (1, 8),
            KeyCode::Left => (1, 9),
            KeyCode::F1 => (7, 1),
            KeyCode::F2 => (7, 2),
            KeyCode::F3 => (7, 3),
            KeyCode::F5 => (7, 4),
            KeyCode::F6 => (7, 5),
            KeyCode::F8 => (7, 6),
            KeyCode::F9 => (7, 7),
            KeyCode::Escape => (7, 0),
            KeyCode::Num1 => (3, 0),
            KeyCode::Num2 => (3, 1),
            KeyCode::W => (2, 1),
            KeyCode::E => (2, 2),
            KeyCode::T => (2, 3),
            KeyCode::Num7 => (2, 4),
            KeyCode::I => (2, 5),
            KeyCode::Num9 => (2, 6),
            KeyCode::Num0 => (2, 7),
            KeyCode::Down => (2, 9),
            KeyCode::D => (3, 2),
            KeyCode::R => (3, 3),
            KeyCode::Num6 => (3, 4),
            KeyCode::U => (3, 5),
            KeyCode::O => (3, 6),
            KeyCode::P => (3, 7),
            KeyCode::LeftBracket => (3, 8),
            KeyCode::Up => (3, 9),
            KeyCode::CapsLock => (4, 0),
            KeyCode::A => (4, 1),
            KeyCode::X => (4, 2),
            KeyCode::F => (4, 3),
            KeyCode::Y => (4, 4),
            KeyCode::J => (4, 5),
            KeyCode::K => (4, 6),
            KeyCode::Semicolon => (5, 7),
            KeyCode::Apostrophe => (4, 8),
            KeyCode::Equals => (1, 7),
            KeyCode::Return => (4, 9),
            KeyCode::S => (5, 1),
            KeyCode::C => (5, 2),
            KeyCode::G => (5, 3),
            KeyCode::H => (5, 4),
            KeyCode::N => (5, 5),
            KeyCode::L => (5, 6),
            KeyCode::RightBracket => (5, 8),
            KeyCode::Backspace => (5, 9),
            KeyCode::Tab => (6, 0),
            KeyCode::Z => (6, 1),
            KeyCode::Space => (6, 2),
            KeyCode::V => (6, 3),
            KeyCode::B => (6, 4),
            KeyCode::M => (6, 5),
            KeyCode::Comma => (6, 6),
            KeyCode::Period => (6, 7),
            KeyCode::Slash => (6, 8),
            KeyCode::Delete => (6, 9),
            _ => return None,
        })
    }

    fn map_key_coords(key: KeyCode, shift: bool) -> Vec<(usize, usize)> {
        match (key, shift) {
            (KeyCode::Apostrophe, true) => vec![(0, 0), (3, 1)],  // '"'
            (KeyCode::Apostrophe, false) => vec![(0, 0), (2, 4)], // '\''
            (KeyCode::Equals, false) => vec![(0, 0), (1, 7)],     // '=' = Shift+'-'
            (KeyCode::Equals, true) => vec![(0, 0), (5, 7)],      // '+' = Shift+';'
            (KeyCode::Num8, true) => vec![(0, 0), (4, 8)],        // '*' = Shift+':'
            (KeyCode::Semicolon, true) => vec![(4, 8)],           // ':' (already shifted on host, unshifted on BBC)
            _ => Self::map_key(key).into_iter().collect(),
        }
    }
}

impl EmulatorCore for BbcMicro {
    fn system_type(&self) -> SystemType {
        SystemType::BbcMicro
    }

    fn system_name(&self) -> &str {
        self.bus.model.display_name()
    }

    fn media_name(&self) -> &str {
        &self.bus.media_name
    }

    fn reset(&mut self) {
        self.bus.reset_state();
        self.cpu.reset(&mut self.bus);
        if self.bus.model == BbcModel::Master128 {
            for _ in 0..20 {
                self.run_frame();
            }
        }
    }

    fn run_frame(&mut self) {
        let step_result = if let Some((coords, frames_left)) = self.auto_type_queue.front_mut() {
            let active = coords.clone();
            let done = *frames_left <= 1;
            if !done {
                *frames_left -= 1;
            }
            Some((active, done))
        } else {
            None
        };
        if let Some((active, done)) = step_result {
            self.auto_type_active = active;
            self.sync_key_matrix();
            if done {
                self.auto_type_queue.pop_front();
                if self.auto_type_queue.is_empty() {
                    self.auto_type_active.clear();
                    self.sync_key_matrix();
                }
            }
        }
        self.bus.audio_buf.clear();
        let mut cyc = 0u32;
        let vsync_cyc = 36_000;
        let mut vsync_sent = false;
        while cyc < 40_000 {
            cyc += self.step_one();
            if !vsync_sent && cyc >= vsync_cyc {
                vsync_sent = true;
                self.bus.sys_via.set_ca1(true);
                self.bus.sys_via.set_ca1(false);
            }
        }
        self.bus.saa5050.tick_frame();
        self.bus.render_screen(&mut self.screen);
    }

    fn step_instruction(&mut self) -> u32 {
        let c = self.step_one();
        self.bus.render_screen(&mut self.screen);
        c
    }

    fn rendered_screen(&self) -> &[u32] {
        &self.screen
    }

    fn audio_buffer(&self) -> &[i16] {
        &self.bus.audio_buf
    }

    fn handle_key_down(&mut self, key: KeyCode, mods: KeyMod) {
        if key == KeyCode::F5 && mods.shift {
            self.reset_with_shift_break();
            return;
        }
        let coords = Self::map_key_coords(key, mods.shift);
        if coords.is_empty() {
            return;
        }
        let prev = self.pressed_coords.insert(key, coords.clone());
        if let Some(old) = prev {
            for (r, c) in old {
                self.key_refs[r][c] = self.key_refs[r][c].saturating_sub(1);
            }
        }
        for &(r, c) in &coords {
            self.key_refs[r][c] = self.key_refs[r][c].saturating_add(1);
        }
        self.sync_key_matrix();
    }

    fn handle_key_up(&mut self, key: KeyCode, _mods: KeyMod) {
        if let Some(coords) = self.pressed_coords.remove(&key) {
            for (r, c) in coords {
                self.key_refs[r][c] = self.key_refs[r][c].saturating_sub(1);
            }
            self.sync_key_matrix();
        }
    }

    fn load_media(&mut self, path: &Path) -> EmuResult<()> {
        let (name, ext, bytes) = read_media_or_zip(path, &["ssd", "dsd"])?;
        let is_dsd = ext == "dsd";
        let mut img = FloppyImage::from_ssd_or_dsd(&name, &bytes, is_dsd);
        let has_boot = img.has_dfs_boot_file();
        if has_boot {
            img.ensure_dfs_boot_option();
        }
        self.bus.fdc_8271.insert_disk(img.clone());
        self.bus.fdc_1770.disk = Some(img);
        self.bus.media_name = name;

        if let Some(parent) = path.parent() {
            let scanned = Self::scan_disc_dir(parent);
            if !scanned.is_empty() {
                self.disc_list = scanned;
                if let Some(idx) = self.disc_list.iter().position(|p| p == path) {
                    self.disc_index = idx;
                }
            }
        }

        if has_boot {
            self.reset_with_shift_break();
        } else {
            self.reset();
            for _ in 0..25 {
                self.run_frame();
            }
            self.queue_auto_type("*CAT\r");
        }
        Ok(())
    }

    fn cycle_media(&mut self) -> EmuResult<()> {
        if self.disc_list.is_empty() {
            self.disc_list = Self::scan_disc_dir(&self.bus.disc_dir);
        }
        if self.disc_list.is_empty() {
            return Err(EmuError::UnsupportedMedia(
                "No .ssd/.dsd discs found".into(),
            ));
        }
        self.disc_index = (self.disc_index + 1) % self.disc_list.len();
        let next_path = self.disc_list[self.disc_index].clone();
        self.load_media(&next_path)
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
