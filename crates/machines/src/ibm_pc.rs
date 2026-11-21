use std::collections::VecDeque;
use std::fs;
use std::path::Path;

use cpu_cores::I8086;
use emu_core::archive::load_rom_file;
use emu_core::gui_font::glyph_row;
use emu_core::{
    Bus32, CANVAS_HEIGHT, CANVAS_WIDTH, CpuDebugState, DisassembledInstruction, EmuError,
    EmuResult, EmulatorCore, IoPortBus, KeyCode, KeyMod, SystemType,
};
use peripherals::fdc::UpdPhase;
use peripherals::{Crtc6845, FloppyImage, Pia8255, Pic8259, Pit8253, Upd765};

/// Standard CGA 16-color IRGB palette (ARGB8888).
const CGA_PALETTE: [u32; 16] = [
    0xFF00_0000, // 0 Black
    0xFF00_00AA, // 1 Blue
    0xFF00_AA00, // 2 Green
    0xFF00_AAAA, // 3 Cyan
    0xFFAA_0000, // 4 Red
    0xFFAA_00AA, // 5 Magenta
    0xFFAA_5500, // 6 Brown
    0xFFAA_AAAA, // 7 Light Gray
    0xFF55_5555, // 8 Dark Gray
    0xFF55_55FF, // 9 Light Blue
    0xFF55_FF55, // 10 Light Green
    0xFF55_FFFF, // 11 Light Cyan
    0xFFFF_5555, // 12 Light Red
    0xFFFF_55FF, // 13 Light Magenta
    0xFFFF_FF55, // 14 Yellow
    0xFFFF_FFFF, // 15 White
];

#[derive(Clone, Default)]
struct Dma8237 {
    base_addr: [u16; 4],
    cur_addr: [u16; 4],
    base_count: [u16; 4],
    cur_count: [u16; 4],
    mode: [u8; 4],
    page: [u8; 4],
    mask: u8,
    status: u8,
    command: u8,
    flip_flop: bool,
}

impl Dma8237 {
    fn reset(&mut self) {
        *self = Self::default();
        self.mask = 0x0F;
    }

    fn read(&mut self, reg: u8) -> u8 {
        let r = reg & 0x0F;
        match r {
            0x00 | 0x02 | 0x04 | 0x06 => {
                let ch = (r >> 1) as usize;
                let v = self.cur_addr[ch];
                let lo = !self.flip_flop;
                self.flip_flop = !self.flip_flop;
                if lo { (v & 0xFF) as u8 } else { (v >> 8) as u8 }
            }
            0x01 | 0x03 | 0x05 | 0x07 => {
                let ch = (r >> 1) as usize;
                let v = self.cur_count[ch];
                let lo = !self.flip_flop;
                self.flip_flop = !self.flip_flop;
                if lo { (v & 0xFF) as u8 } else { (v >> 8) as u8 }
            }
            0x08 => {
                let s = self.status;
                self.status &= 0xF0;
                s
            }
            _ => 0xFF,
        }
    }

    fn write(&mut self, reg: u8, val: u8) {
        let r = reg & 0x0F;
        match r {
            0x00 | 0x02 | 0x04 | 0x06 => {
                let ch = (r >> 1) as usize;
                if !self.flip_flop {
                    self.base_addr[ch] = (self.base_addr[ch] & 0xFF00) | (val as u16);
                    self.cur_addr[ch] = self.base_addr[ch];
                } else {
                    self.base_addr[ch] = (self.base_addr[ch] & 0x00FF) | ((val as u16) << 8);
                    self.cur_addr[ch] = self.base_addr[ch];
                }
                self.flip_flop = !self.flip_flop;
            }
            0x01 | 0x03 | 0x05 | 0x07 => {
                let ch = (r >> 1) as usize;
                if !self.flip_flop {
                    self.base_count[ch] = (self.base_count[ch] & 0xFF00) | (val as u16);
                    self.cur_count[ch] = self.base_count[ch];
                } else {
                    self.base_count[ch] = (self.base_count[ch] & 0x00FF) | ((val as u16) << 8);
                    self.cur_count[ch] = self.base_count[ch];
                }
                self.flip_flop = !self.flip_flop;
            }
            0x08 => self.command = val,
            0x0A => {
                let ch = val & 3;
                if (val & 4) != 0 {
                    self.mask |= 1 << ch;
                } else {
                    self.mask &= !(1 << ch);
                }
            }
            0x0B => {
                let ch = (val & 3) as usize;
                self.mode[ch] = val;
            }
            0x0C => self.flip_flop = false,
            0x0D => self.reset(),
            0x0E => self.mask = 0,
            0x0F => self.mask = val & 0x0F,
            _ => {}
        }
    }
}

struct IbmPcBus {
    ram: Vec<u8>,       // 640 KB (0x00000..0x9FFFF)
    cga_vram: Vec<u8>,  // 16 KB (0xB8000..0xBBFFF, mirrored up to 0xBFFFF)
    mda_vram: Vec<u8>,  // 4 KB (0xB0000..0xB0FFF)
    basic_rom: Vec<u8>, // 32 KB (0xF6000..0xFDFFF)
    bios_rom: Vec<u8>,  // 8 KB (0xFE000..0xFFFFF)
    pic: Pic8259,
    pit: Pit8253,
    ppi: Pia8255,
    dma: Dma8237,
    crtc: Crtc6845,
    fdc: Upd765,
    fdc_dor: u8,
    cga_mode: u8,
    cga_color: u8,
    retrace_phase: u32,
    key_queue: VecDeque<u8>,
    current_scancode: u8,
    kbd_reset_delay: u32,
    nmi_mask: u8,
    frame_counter: u64,
    audio_buf: Vec<i16>,
    media_name: String,
}

impl IbmPcBus {
    fn new(rom_dir: &Path) -> EmuResult<Self> {
        let bios_rom = Self::load_bios(rom_dir)?;
        let basic_rom = Self::load_basic(rom_dir)?;
        let mut bus = Self {
            ram: vec![0; 0xA_0000],
            cga_vram: vec![0; 0x4000],
            mda_vram: vec![0; 0x1000],
            basic_rom,
            bios_rom,
            pic: Pic8259::new(),
            pit: Pit8253::new(),
            ppi: Pia8255::new(),
            dma: Dma8237::default(),
            crtc: Crtc6845::new(),
            fdc: Upd765::new(),
            fdc_dor: 0x0C,
            cga_mode: 0x09, // 80x25 text + video enable
            cga_color: 0,
            retrace_phase: 0,
            key_queue: VecDeque::new(),
            current_scancode: 0,
            kbd_reset_delay: 0,
            nmi_mask: 0,
            frame_counter: 0,
            audio_buf: Vec::with_capacity(1024),
            media_name: String::from("Cassette BASIC C1.00"),
        };
        bus.reset_state();
        Ok(bus)
    }

    fn load_bios(rom_dir: &Path) -> EmuResult<Vec<u8>> {
        let candidates = [
            "BIOS_IBM5150_19OCT81_5700671_U33.BIN",
            "BIOS_5150_19OCT81_U33.BIN",
            "BIOS_IBM5150_24APR81_5700051_U33.BIN",
            "BIOS_5150_24APR81_U33.BIN",
        ];
        for name in candidates {
            if let Ok(mut bytes) = load_rom_file(rom_dir, name) {
                bytes.resize(8192, 0xFF);
                return Ok(bytes);
            }
        }
        Err(EmuError::MissingRom(
            "IBM PC 5150 BIOS U33 (BIOS_IBM5150_19OCT81_5700671_U33.BIN)".into(),
        ))
    }

    fn load_basic(rom_dir: &Path) -> EmuResult<Vec<u8>> {
        if let Ok(mut bytes) = load_rom_file(rom_dir, "ibm-basic-1.00.rom") {
            bytes.resize(32_768, 0xFF);
            return Ok(bytes);
        }
        let parts = [
            "IBM_5150_BASIC_1.00_U29.BIN",
            "IBM_5150_BASIC_1.00_U30.BIN",
            "IBM_5150_BASIC_1.00_U31.BIN",
            "IBM_5150_BASIC_1.00_U32.BIN",
        ];
        let mut combined = Vec::with_capacity(32_768);
        for name in parts {
            let chunk = load_rom_file(rom_dir, name)?;
            combined.extend_from_slice(&chunk);
        }
        combined.resize(32_768, 0xFF);
        Ok(combined)
    }

    fn reset_state(&mut self) {
        self.ram.fill(0);
        // Set warm-boot flag (0040:0072 = 0x1234) so IBM 5150 BIOS runs full POST hardware
        // init into INT 18h BASIC without blocking on the 32M-cycle marching RAM test.
        self.ram[0x0472] = 0x34;
        self.ram[0x0473] = 0x12;
        self.cga_vram.fill(0);
        self.mda_vram.fill(0);
        self.pic.reset();
        self.pit.reset();
        self.ppi.reset();
        self.ppi.port_b_out = 0x00;
        self.dma.reset();
        self.crtc.reset();
        self.fdc.reset();
        self.fdc_dor = 0x0C;
        self.cga_mode = 0x09;
        self.cga_color = 0;
        self.key_queue.clear();
        self.current_scancode = 0;
        self.kbd_reset_delay = 0;
        self.nmi_mask = 0;
        self.frame_counter = 0;
    }

    /// Planar DIP switch SW1 returned on PPI Port A when `port_b_out & 0x80 != 0`.
    fn sw1_byte(&self) -> u8 {
        // Bits 3..2 = 11 (64 KB planar), Bits 5..4 = 10 (80x25 CGA)
        let floppy_bit = if self.fdc.disk.is_some() { 0x01 } else { 0x00 };
        0x2C | floppy_bit
    }

    /// Expansion RAM DIP switch SW2 (in 32KB increments above 64KB).
    /// Reports 64 KB planar RAM (`0x00`) so IBM 5150 POST tests RAM quickly
    /// while all 640 KB remains mapped on the bus.
    fn ppi_port_c(&self) -> u8 {
        let sw2 = 0x00;
        let t2_out = if self.pit.channels[2].out { 0x30 } else { 0x00 };
        sw2 | t2_out
    }

    fn run_fdc_dma_if_ready(&mut self) {
        if (self.fdc_dor & 0x08) == 0 {
            return;
        }
        match self.fdc.phase {
            UpdPhase::ExecRead => {
                while !self.fdc.data_buf.is_empty() {
                    let phys = (((self.dma.page[2] & 0x0F) as usize) << 16)
                        | (self.dma.cur_addr[2] as usize);
                    let byte = self.fdc.read_data();
                    if phys < self.ram.len() {
                        self.ram[phys] = byte;
                    }
                    self.dma.cur_addr[2] = self.dma.cur_addr[2].wrapping_add(1);
                    if self.dma.cur_count[2] == 0 {
                        break;
                    }
                    self.dma.cur_count[2] = self.dma.cur_count[2].wrapping_sub(1);
                }
                self.fdc.phase = UpdPhase::Result;
                self.dma.status |= 0x04; // TC ch2
                self.pic.raise_irq(6);
            }
            UpdPhase::ExecWrite => {
                while self.fdc.phase == UpdPhase::ExecWrite {
                    let phys = (((self.dma.page[2] & 0x0F) as usize) << 16)
                        | (self.dma.cur_addr[2] as usize);
                    let byte = if phys < self.ram.len() {
                        self.ram[phys]
                    } else {
                        0
                    };
                    self.fdc.write_data(byte);
                    self.dma.cur_addr[2] = self.dma.cur_addr[2].wrapping_add(1);
                    if self.dma.cur_count[2] == 0 && self.fdc.phase == UpdPhase::ExecWrite {
                        break;
                    }
                    self.dma.cur_count[2] = self.dma.cur_count[2].wrapping_sub(1);
                }
                if self.fdc.irq_pending {
                    self.dma.status |= 0x04;
                    self.pic.raise_irq(6);
                }
            }
            _ => {}
        }
    }

    fn queue_scancode(&mut self, code: u8) {
        self.key_queue.push_back(code);
        if self.current_scancode == 0 && (self.ppi.port_b_out & 0x80) == 0 {
            if let Some(next) = self.key_queue.pop_front() {
                self.current_scancode = next;
                self.pic.raise_irq(1);
            }
        }
    }

    fn tick_kbd(&mut self, cycles: u32) {
        if self.kbd_reset_delay > 0 {
            self.kbd_reset_delay = self.kbd_reset_delay.saturating_sub(cycles);
            if self.kbd_reset_delay == 0 {
                self.current_scancode = 0xAA;
                self.pic.raise_irq(1);
            }
        }
    }

    fn glyph_byte(&self, ch: u8, row: usize) -> u8 {
        if ch < 128 && self.bios_rom.len() >= 0x1E6E {
            // IBM 5150 U33 BIOS stores standard 8x8 ASCII 0..127 font at 0x1A6E
            let base = 0x1A6E + (ch as usize) * 8 + (row & 7);
            if base < self.bios_rom.len() {
                return self.bios_rom[base];
            }
        }
        glyph_row(ch as char, row)
    }

    fn render_screen(&self, fb: &mut [u32]) {
        let border = CGA_PALETTE[(self.cga_color & 0x0F) as usize];
        fb.fill(border);

        let blink_on = (self.frame_counter / 16) & 1 == 0;
        let is_gfx = (self.cga_mode & 0x02) != 0;

        if is_gfx {
            let hi_res = (self.cga_mode & 0x10) != 0;
            let pal_sel = if (self.cga_color & 0x20) != 0 {
                [border, CGA_PALETTE[11], CGA_PALETTE[13], CGA_PALETTE[15]]
            } else {
                [border, CGA_PALETTE[10], CGA_PALETTE[12], CGA_PALETTE[14]]
            };
            let fg_hi = CGA_PALETTE[(self.cga_color & 0x0F).max(7) as usize];
            let bg_hi = CGA_PALETTE[0];
            let y_off = (CANVAS_HEIGHT.saturating_sub(400)) / 2;

            for y in 0..200 {
                let bank = (y & 1) * 0x2000;
                let row_addr = bank + (y >> 1) * 80;
                let dst_y0 = y_off + y * 2;
                let dst_y1 = dst_y0 + 1;
                if hi_res {
                    for bx in 0..80 {
                        let byte = self.cga_vram[(row_addr + bx) & 0x3FFF];
                        for bit in 0..8 {
                            let c = if ((byte >> (7 - bit)) & 1) != 0 {
                                fg_hi
                            } else {
                                bg_hi
                            };
                            let px = bx * 8 + bit;
                            if dst_y0 < CANVAS_HEIGHT && px < CANVAS_WIDTH {
                                fb[dst_y0 * CANVAS_WIDTH + px] = c;
                            }
                            if dst_y1 < CANVAS_HEIGHT && px < CANVAS_WIDTH {
                                fb[dst_y1 * CANVAS_WIDTH + px] = c;
                            }
                        }
                    }
                } else {
                    for bx in 0..80 {
                        let byte = self.cga_vram[(row_addr + bx) & 0x3FFF];
                        for p in 0..4 {
                            let idx = ((byte >> (6 - p * 2)) & 3) as usize;
                            let c = pal_sel[idx];
                            let px = (bx * 4 + p) * 2;
                            for dy in 0..2 {
                                let py = dst_y0 + dy;
                                if py < CANVAS_HEIGHT && px + 1 < CANVAS_WIDTH {
                                    fb[py * CANVAS_WIDTH + px] = c;
                                    fb[py * CANVAS_WIDTH + px + 1] = c;
                                }
                            }
                        }
                    }
                }
            }
            return;
        }

        // Text mode (80x25 or 40x25)
        let cols = if (self.cga_mode & 0x01) != 0 { 80 } else { 40 };
        let scale_x = if cols == 40 { 2 } else { 1 };
        let start = self.crtc.start_addr() as usize;
        let cursor = self.crtc.cursor_addr() as usize;
        let cur_start = (self.crtc.regs[10] & 0x1F) as usize;
        let cur_end = (self.crtc.regs[11] & 0x1F) as usize;
        let cur_enabled = (self.crtc.regs[10] & 0x60) != 0x20;

        let disp_h = 25 * 16; // 400 px tall
        let y_off = (CANVAS_HEIGHT.saturating_sub(disp_h)) / 2;

        for row in 0..25usize {
            for col in 0..cols {
                let cell = (start + row * cols + col) & 0x1FFF;
                let ch = self.cga_vram[(cell * 2) & 0x3FFF];
                let attr = self.cga_vram[(cell * 2 + 1) & 0x3FFF];
                let fg = CGA_PALETTE[(attr & 0x0F) as usize];
                let bg = CGA_PALETTE[((attr >> 4) & 0x07) as usize];
                let is_cursor = cur_enabled && blink_on && cell == cursor;

                for py in 0..8usize {
                    let bits = self.glyph_byte(ch, py);
                    let cur_line = is_cursor && py >= cur_start && py <= cur_end;
                    for px in 0..8usize {
                        let on = cur_line || ((bits >> (7 - px)) & 1) != 0;
                        let color = if on { fg } else { bg };
                        let dst_x = (col * 8 + px) * scale_x;
                        let dst_y = y_off + row * 16 + py * 2;
                        for dy in 0..2 {
                            for dx in 0..scale_x {
                                let x = dst_x + dx;
                                let y = dst_y + dy;
                                if x < CANVAS_WIDTH && y < CANVAS_HEIGHT {
                                    fb[y * CANVAS_WIDTH + x] = color;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

impl Bus32 for IbmPcBus {
    fn read8(&mut self, addr: u32) -> u8 {
        let a = addr & 0xF_FFFF;
        match a {
            0x00000..=0x9_FFFF => self.ram[a as usize],
            0xB_0000..=0xB_7FFF => self.mda_vram[(a as usize) & 0x0FFF],
            0xB_8000..=0xB_FFFF => self.cga_vram[(a as usize) & 0x3FFF],
            0xF_6000..=0xF_DFFF => self.basic_rom[(a - 0xF_6000) as usize],
            0xF_E000..=0xF_FFFF => self.bios_rom[(a - 0xF_E000) as usize],
            _ => 0xFF,
        }
    }

    fn write8(&mut self, addr: u32, val: u8) {
        let a = addr & 0xF_FFFF;
        match a {
            0x00000..=0x9_FFFF => self.ram[a as usize] = val,
            0xB_0000..=0xB_7FFF => self.mda_vram[(a as usize) & 0x0FFF] = val,
            0xB_8000..=0xB_FFFF => self.cga_vram[(a as usize) & 0x3FFF] = val,
            _ => {}
        }
    }

    fn peek8(&self, addr: u32) -> u8 {
        let a = addr & 0xF_FFFF;
        match a {
            0x00000..=0x9_FFFF => self.ram[a as usize],
            0xB_0000..=0xB_7FFF => self.mda_vram[(a as usize) & 0x0FFF],
            0xB_8000..=0xB_FFFF => self.cga_vram[(a as usize) & 0x3FFF],
            0xF_6000..=0xF_DFFF => self.basic_rom[(a - 0xF_6000) as usize],
            0xF_E000..=0xF_FFFF => self.bios_rom[(a - 0xF_E000) as usize],
            _ => 0xFF,
        }
    }
}

impl IoPortBus for IbmPcBus {
    fn port_in8(&mut self, port: u16) -> u8 {
        match port {
            0x0000..=0x000F => self.dma.read((port & 0x0F) as u8),
            0x0020..=0x0021 => self.pic.read((port & 1) != 0),
            0x0040..=0x0043 => self.pit.read((port & 3) as u8),
            0x0060 => {
                if (self.ppi.port_b_out & 0x80) != 0 {
                    self.sw1_byte()
                } else {
                    self.current_scancode
                }
            }
            0x0061 => self.ppi.port_b_out,
            0x0062 => self.ppi_port_c(),
            0x0063 => self.ppi.control,
            0x0081..=0x0083 => self.dma.page[(port & 3) as usize],
            0x03B5 => 0xFF,
            0x03BA => 0xFF,
            0x03D5 => self.crtc.read(true),
            0x03DA => {
                self.retrace_phase = self.retrace_phase.wrapping_add(1);
                let phase = self.retrace_phase & 0x1F;
                if phase < 4 {
                    0x09 // VSync + Display Disable
                } else if (phase & 3) == 0 {
                    0x01 // HSync Display Disable
                } else {
                    0x00
                }
            }
            0x03F4 => self.fdc.read_msr(),
            0x03F5 => self.fdc.read_data(),
            _ => 0xFF,
        }
    }

    fn port_out8(&mut self, port: u16, val: u8) {
        match port {
            0x0000..=0x000F => self.dma.write((port & 0x0F) as u8, val),
            0x0020..=0x0021 => self.pic.write((port & 1) != 0, val),
            0x0040..=0x0043 => self.pit.write((port & 3) as u8, val),
            0x0061 => {
                let prev_clk_low = (self.ppi.port_b_out & 0x40) == 0;
                let prev_kb_clear = (self.ppi.port_b_out & 0x80) != 0;
                self.ppi.write(1, val);
                self.pit.channels[2].gate = (val & 0x01) != 0;
                if prev_clk_low && (val & 0x40) != 0 {
                    // Rising edge of XT keyboard clock line starts 8048 self-test;
                    // 0xAA + IRQ1 arrive ~200 CPU cycles later (after STI + MOV AH, 0).
                    self.kbd_reset_delay = 200;
                }
                if (val & 0x80) != 0 && !prev_kb_clear {
                    self.current_scancode = 0;
                    self.pic.clear_irq(1);
                } else if (val & 0x80) == 0 && prev_kb_clear && self.current_scancode == 0 {
                    if let Some(next) = self.key_queue.pop_front() {
                        self.current_scancode = next;
                        self.pic.raise_irq(1);
                    }
                }
            }
            0x0063 => self.ppi.write(3, val),
            0x0080..=0x0083 => {
                let idx = match port {
                    0x0081 => 2,
                    0x0082 => 3,
                    0x0083 => 1,
                    _ => 0,
                };
                self.dma.page[idx] = val;
            }
            0x00A0 => self.nmi_mask = val,
            0x03D4 => self.crtc.write(false, val),
            0x03D5 => self.crtc.write(true, val),
            0x03D8 => self.cga_mode = val,
            0x03D9 => self.cga_color = val,
            0x03F2 => {
                let prev_reset = (self.fdc_dor & 0x04) != 0;
                self.fdc_dor = val;
                self.fdc.motor_on = (val & 0xF0) != 0;
                if !prev_reset && (val & 0x04) != 0 {
                    self.fdc.reset();
                    self.fdc.st0 = 0xC0;
                    self.fdc.irq_pending = true;
                    self.pic.raise_irq(6);
                }
            }
            0x03F5 => {
                self.fdc.write_data(val);
                self.run_fdc_dma_if_ready();
                if self.fdc.irq_pending && (self.fdc_dor & 0x08) != 0 {
                    self.pic.raise_irq(6);
                }
            }
            _ => {}
        }
    }
}

pub struct IbmPc5150 {
    cpu: I8086,
    bus: IbmPcBus,
    screen: Vec<u32>,
    pit_accum: u32,
}

impl IbmPc5150 {
    pub fn new(rom_dir: &Path) -> EmuResult<Self> {
        let mut bus = IbmPcBus::new(rom_dir)?;
        let mut cpu = I8086::new();
        cpu.reset();
        bus.reset_state();
        let mut m = Self {
            cpu,
            bus,
            screen: vec![0xFF00_0000; CANVAS_WIDTH * CANVAS_HEIGHT],
            pit_accum: 0,
        };
        for _ in 0..95 {
            m.run_frame();
        }
        Ok(m)
    }

    fn xt_scancode(key: KeyCode) -> Option<u8> {
        Some(match key {
            KeyCode::Escape => 0x01,
            KeyCode::Num1 => 0x02,
            KeyCode::Num2 => 0x03,
            KeyCode::Num3 => 0x04,
            KeyCode::Num4 => 0x05,
            KeyCode::Num5 => 0x06,
            KeyCode::Num6 => 0x07,
            KeyCode::Num7 => 0x08,
            KeyCode::Num8 => 0x09,
            KeyCode::Num9 => 0x0A,
            KeyCode::Num0 => 0x0B,
            KeyCode::Minus => 0x0C,
            KeyCode::Equals => 0x0D,
            KeyCode::Backspace => 0x0E,
            KeyCode::Tab => 0x0F,
            KeyCode::Q => 0x10,
            KeyCode::W => 0x11,
            KeyCode::E => 0x12,
            KeyCode::R => 0x13,
            KeyCode::T => 0x14,
            KeyCode::Y => 0x15,
            KeyCode::U => 0x16,
            KeyCode::I => 0x17,
            KeyCode::O => 0x18,
            KeyCode::P => 0x19,
            KeyCode::LeftBracket => 0x1A,
            KeyCode::RightBracket => 0x1B,
            KeyCode::Return => 0x1C,
            KeyCode::LCtrl => 0x1D,
            KeyCode::A => 0x1E,
            KeyCode::S => 0x1F,
            KeyCode::D => 0x20,
            KeyCode::F => 0x21,
            KeyCode::G => 0x22,
            KeyCode::H => 0x23,
            KeyCode::J => 0x24,
            KeyCode::K => 0x25,
            KeyCode::L => 0x26,
            KeyCode::Semicolon => 0x27,
            KeyCode::Apostrophe => 0x28,
            KeyCode::Backquote => 0x29,
            KeyCode::LShift => 0x2A,
            KeyCode::Backslash => 0x2B,
            KeyCode::Z => 0x2C,
            KeyCode::X => 0x2D,
            KeyCode::C => 0x2E,
            KeyCode::V => 0x2F,
            KeyCode::B => 0x30,
            KeyCode::N => 0x31,
            KeyCode::M => 0x32,
            KeyCode::Comma => 0x33,
            KeyCode::Period => 0x34,
            KeyCode::Slash => 0x35,
            KeyCode::RShift => 0x36,
            KeyCode::Space => 0x39,
            KeyCode::CapsLock => 0x3A,
            KeyCode::F1 => 0x3B,
            KeyCode::F2 => 0x3C,
            KeyCode::F3 => 0x3D,
            KeyCode::F4 => 0x3E,
            KeyCode::F5 => 0x3F,
            KeyCode::F6 => 0x40,
            KeyCode::F7 => 0x41,
            KeyCode::F8 => 0x42,
            KeyCode::F9 => 0x43,
            KeyCode::Up => 0x48,
            KeyCode::Left => 0x4B,
            KeyCode::Right => 0x4D,
            KeyCode::Down => 0x50,
            _ => return None,
        })
    }
}

impl EmulatorCore for IbmPc5150 {
    fn system_type(&self) -> SystemType {
        SystemType::IbmPc5150
    }

    fn system_name(&self) -> &str {
        "IBM PC 5150 (Intel 8088)"
    }

    fn media_name(&self) -> &str {
        &self.bus.media_name
    }

    fn reset(&mut self) {
        self.bus.reset_state();
        self.cpu.reset();
        self.pit_accum = 0;
        for _ in 0..95 {
            self.run_frame();
        }
    }

    fn run_frame(&mut self) {
        self.bus.audio_buf.clear();
        let target_cycles = 79_545u32;
        let mut cycles = 0u32;
        let mut next_sample_cyc = 0u32;
        let cyc_per_sample = target_cycles / 735;

        while cycles < target_cycles {
            let c = self.cpu.step(&mut self.bus).max(1);
            cycles += c;
            self.bus.tick_kbd(c);
            self.pit_accum += c;
            let pit_ticks = self.pit_accum / 4;
            if pit_ticks > 0 {
                self.pit_accum &= 3;
                let (irq0, _spk) = self.bus.pit.tick(pit_ticks);
                if irq0 > 0 {
                    self.bus.pic.raise_irq(0);
                }
            }
            if self.cpu.interrupts_enabled() {
                if let Some(vec) = self.bus.pic.acknowledge_irq() {
                    self.cpu.trigger_interrupt(&mut self.bus, vec);
                }
            }
            if cycles >= next_sample_cyc {
                let spk_on = (self.bus.ppi.port_b_out & 0x02) != 0 && self.bus.pit.channels[2].out;
                self.bus.audio_buf.push(if spk_on { 4000 } else { 0 });
                next_sample_cyc += cyc_per_sample.max(1);
            }
        }

        self.bus.frame_counter += 1;
        self.bus.render_screen(&mut self.screen);
    }

    fn step_instruction(&mut self) -> u32 {
        let c = self.cpu.step(&mut self.bus);
        self.bus.tick_kbd(c);
        let (irq0, _) = self.bus.pit.tick((c / 4).max(1));
        if irq0 > 0 {
            self.bus.pic.raise_irq(0);
        }
        if self.cpu.interrupts_enabled() {
            if let Some(vec) = self.bus.pic.acknowledge_irq() {
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
        if let Some(code) = Self::xt_scancode(key) {
            self.bus.queue_scancode(code);
        }
    }

    fn handle_key_up(&mut self, key: KeyCode, _mod_state: KeyMod) {
        if let Some(code) = Self::xt_scancode(key) {
            self.bus.queue_scancode(code | 0x80);
        }
    }

    fn load_media(&mut self, path: &Path) -> EmuResult<()> {
        let bytes = fs::read(path).map_err(|e| EmuError::IoError(path.display().to_string(), e))?;
        let name = path
            .file_name()
            .and_then(|s| s.to_str())
            .unwrap_or("pc_disk.img")
            .to_string();
        if bytes.len() >= 163_840 {
            self.bus.fdc.disk = Some(FloppyImage::from_raw_pc(&name, &bytes));
            self.bus.media_name = name;
            self.reset();
            return Ok(());
        }
        Err(EmuError::UnsupportedMedia(format!(
            "Unsupported IBM PC floppy image: {name}"
        )))
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
