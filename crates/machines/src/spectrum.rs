use cpu_cores::Z80;
use emu_core::archive::{load_rom_file, read_media_or_zip};
use emu_core::types::{
    Bus8, CpuDebugState, DisassembledInstruction, EmuError, EmuResult, EmulatorCore, IoPortBus,
    KeyCode, KeyMod, SystemType, AUDIO_SAMPLE_RATE, CANVAS_HEIGHT, CANVAS_WIDTH,
};
use std::collections::VecDeque;
use std::path::Path;

#[derive(Clone, Copy)]
enum TapPhase {
    Idle,
    Pilot { remaining: usize },
    Sync1,
    Sync2,
    Data { byte_idx: usize, bit_idx: u8, half: u8 },
    Pause,
}

struct SpectrumTapDeck {
    blocks: Vec<Vec<u8>>,
    block_idx: usize,
    phase: TapPhase,
    rem_tstates: u32,
    ear: bool,
    playing: bool,
}

impl SpectrumTapDeck {
    fn new() -> Self {
        Self {
            blocks: Vec::new(),
            block_idx: 0,
            phase: TapPhase::Idle,
            rem_tstates: 0,
            ear: false,
            playing: false,
        }
    }

    fn load_tap(&mut self, data: &[u8]) {
        self.blocks.clear();
        let mut pos = 0usize;
        while pos + 2 <= data.len() {
            let blen = u16::from_le_bytes([data[pos], data[pos + 1]]) as usize;
            pos += 2;
            if blen == 0 || pos + blen > data.len() {
                break;
            }
            self.blocks.push(data[pos..pos + blen].to_vec());
            pos += blen;
        }
        self.block_idx = 0;
        self.phase = TapPhase::Idle;
        self.rem_tstates = 0;
        self.ear = false;
        self.playing = false;
    }

    fn start(&mut self) {
        if self.blocks.is_empty() {
            return;
        }
        self.block_idx = 0;
        self.playing = true;
        self.start_block(0);
    }

    fn start_block(&mut self, idx: usize) {
        if idx >= self.blocks.len() {
            self.playing = false;
            self.phase = TapPhase::Idle;
            self.ear = false;
            return;
        }
        self.block_idx = idx;
        let is_header = self.blocks[idx].first().copied().unwrap_or(0) < 128;
        let pilot_pulses = if is_header { 8063 } else { 3223 };
        self.phase = TapPhase::Pilot {
            remaining: pilot_pulses,
        };
        self.rem_tstates = 2168;
    }

    fn tick(&mut self, mut tstates: u32) {
        if !self.playing {
            return;
        }
        while tstates >= self.rem_tstates {
            tstates -= self.rem_tstates;
            match self.phase {
                TapPhase::Idle => {
                    self.playing = false;
                    return;
                }
                TapPhase::Pilot { remaining } => {
                    self.ear = !self.ear;
                    if remaining <= 1 {
                        self.phase = TapPhase::Sync1;
                        self.rem_tstates = 667;
                    } else {
                        self.phase = TapPhase::Pilot {
                            remaining: remaining - 1,
                        };
                        self.rem_tstates = 2168;
                    }
                }
                TapPhase::Sync1 => {
                    self.ear = !self.ear;
                    self.phase = TapPhase::Sync2;
                    self.rem_tstates = 735;
                }
                TapPhase::Sync2 => {
                    self.ear = !self.ear;
                    self.phase = TapPhase::Data {
                        byte_idx: 0,
                        bit_idx: 7,
                        half: 0,
                    };
                    let bit = (self.blocks[self.block_idx][0] & 0x80) != 0;
                    self.rem_tstates = if bit { 1710 } else { 855 };
                }
                TapPhase::Data {
                    byte_idx,
                    bit_idx,
                    half,
                } => {
                    self.ear = !self.ear;
                    if half == 0 {
                        self.phase = TapPhase::Data {
                            byte_idx,
                            bit_idx,
                            half: 1,
                        };
                        let bit = (self.blocks[self.block_idx][byte_idx] >> bit_idx) & 1 != 0;
                        self.rem_tstates = if bit { 1710 } else { 855 };
                    } else if bit_idx > 0 {
                        let nb = bit_idx - 1;
                        self.phase = TapPhase::Data {
                            byte_idx,
                            bit_idx: nb,
                            half: 0,
                        };
                        let bit = (self.blocks[self.block_idx][byte_idx] >> nb) & 1 != 0;
                        self.rem_tstates = if bit { 1710 } else { 855 };
                    } else if byte_idx + 1 < self.blocks[self.block_idx].len() {
                        let ni = byte_idx + 1;
                        self.phase = TapPhase::Data {
                            byte_idx: ni,
                            bit_idx: 7,
                            half: 0,
                        };
                        let bit = (self.blocks[self.block_idx][ni] & 0x80) != 0;
                        self.rem_tstates = if bit { 1710 } else { 855 };
                    } else {
                        self.phase = TapPhase::Pause;
                        self.rem_tstates = 350_000;
                    }
                }
                TapPhase::Pause => {
                    self.ear = false;
                    self.start_block(self.block_idx + 1);
                    if !self.playing {
                        return;
                    }
                }
            }
        }
        self.rem_tstates -= tstates;
    }
}

struct SpectrumBus {
    rom: Vec<u8>,
    ram: Vec<u8>,
    border: u8,
    speaker: bool,
    key_rows: [u8; 8],
    tape: SpectrumTapDeck,
    audio_buf: Vec<i16>,
    audio_accum: f64,
    frame_cnt: u32,
    media_name: String,
}

impl SpectrumBus {
    const COLORS: [u32; 16] = [
        0xFF00_0000, 0xFF00_00D7, 0xFFD7_0000, 0xFFD7_00D7,
        0xFF00_D700, 0xFF00_D7D7, 0xFFD7_D700, 0xFFD7_D7D7,
        0xFF00_0000, 0xFF00_00FF, 0xFFFF_0000, 0xFFFF_00FF,
        0xFF00_FF00, 0xFF00_FFFF, 0xFFFF_FF00, 0xFFFF_FFFF,
    ];

    fn new(rom_dir: &Path) -> EmuResult<Self> {
        let bytes = load_rom_file(rom_dir, "spectrum48.rom")?;
        let mut rom = vec![0u8; 16_384];
        let len = bytes.len().min(16_384);
        rom[..len].copy_from_slice(&bytes[..len]);
        Ok(Self {
            rom,
            ram: vec![0; 49_152],
            border: 7,
            speaker: false,
            key_rows: [0x1F; 8],
            tape: SpectrumTapDeck::new(),
            audio_buf: Vec::with_capacity(1024),
            audio_accum: 0.0,
            frame_cnt: 0,
            media_name: "ZX Spectrum 48K BASIC".to_string(),
        })
    }

    fn reset_state(&mut self) {
        self.ram.fill(0);
        self.border = 7;
        self.speaker = false;
        self.tape.playing = false;
        self.tape.ear = false;
    }

    fn render_screen(&self, out: &mut [u32]) {
        let border_col = Self::COLORS[(self.border & 7) as usize];
        out.fill(border_col);

        let scale = 2usize;
        let x0 = (CANVAS_WIDTH - 256 * scale) / 2;
        let y0 = (CANVAS_HEIGHT - 192 * scale) / 2;
        let flash_phase = ((self.frame_cnt / 16) & 1) != 0;

        for y in 0..192usize {
            let line_addr = ((y & 0xC0) << 5) | ((y & 0x07) << 8) | ((y & 0x38) << 2);
            let attr_row = (y / 8) * 32;
            for col in 0..32usize {
                let bmp = self.ram[line_addr + col];
                let attr = self.ram[0x1800 + attr_row + col];
                let bright = if (attr & 0x40) != 0 { 8 } else { 0 };
                let mut ink = Self::COLORS[((attr & 0x07) as usize) + bright];
                let mut paper = Self::COLORS[(((attr >> 3) & 0x07) as usize) + bright];
                if (attr & 0x80) != 0 && flash_phase {
                    std::mem::swap(&mut ink, &mut paper);
                }
                for bit in 0..8usize {
                    let px_col = if (bmp & (0x80 >> bit)) != 0 { ink } else { paper };
                    let sx = x0 + (col * 8 + bit) * scale;
                    let sy = y0 + y * scale;
                    out[sy * CANVAS_WIDTH + sx] = px_col;
                    out[sy * CANVAS_WIDTH + sx + 1] = px_col;
                    out[(sy + 1) * CANVAS_WIDTH + sx] = px_col;
                    out[(sy + 1) * CANVAS_WIDTH + sx + 1] = px_col;
                }
            }
        }
    }
}

impl Bus8 for SpectrumBus {
    fn read8(&mut self, addr: u16) -> u8 {
        self.peek8(addr)
    }

    fn write8(&mut self, addr: u16, val: u8) {
        if addr >= 0x4000 {
            self.ram[(addr - 0x4000) as usize] = val;
        }
    }

    fn peek8(&self, addr: u16) -> u8 {
        if addr < 0x4000 {
            self.rom[addr as usize]
        } else {
            self.ram[(addr - 0x4000) as usize]
        }
    }
}

impl IoPortBus for SpectrumBus {
    fn port_in8(&mut self, port: u16) -> u8 {
        if (port & 0x0001) == 0 {
            let hi = (port >> 8) as u8;
            let mut keys = 0x1Fu8;
            for (row, &mask) in self.key_rows.iter().enumerate() {
                if (hi & (1 << row)) == 0 {
                    keys &= mask;
                }
            }
            let ear_bit = if self.tape.ear { 0x40 } else { 0x00 };
            0xA0 | ear_bit | (keys & 0x1F)
        } else {
            0xFF
        }
    }

    fn port_out8(&mut self, port: u16, val: u8) {
        if (port & 0x0001) == 0 {
            self.border = val & 0x07;
            self.speaker = (val & 0x10) != 0;
        }
    }
}

pub struct ZxSpectrum48k {
    cpu: Z80,
    bus: SpectrumBus,
    screen: Vec<u32>,
    auto_type_queue: VecDeque<(Vec<(usize, u8)>, u8)>,
    start_tape_after_keys: bool,
}

impl ZxSpectrum48k {
    pub fn new(rom_dir: &Path) -> EmuResult<Self> {
        let bus = SpectrumBus::new(rom_dir)?;
        let cpu = Z80::new();
        Ok(Self {
            cpu,
            bus,
            screen: vec![0xFF00_0000; CANVAS_WIDTH * CANVAS_HEIGHT],
            auto_type_queue: VecDeque::new(),
            start_tape_after_keys: false,
        })
    }

    fn decompress_z80(src: &[u8], max_out: usize, has_end_marker: bool) -> Vec<u8> {
        let mut out = Vec::with_capacity(max_out);
        let mut i = 0usize;
        while i < src.len() && out.len() < max_out {
            if has_end_marker
                && i + 3 < src.len()
                && src[i] == 0x00
                && src[i + 1] == 0xED
                && src[i + 2] == 0xED
                && src[i + 3] == 0x00
            {
                break;
            }
            if i + 3 < src.len() && src[i] == 0xED && src[i + 1] == 0xED {
                let count = src[i + 2] as usize;
                let val = src[i + 3];
                let take = count.min(max_out - out.len());
                out.resize(out.len() + take, val);
                i += 4;
            } else {
                out.push(src[i]);
                i += 1;
            }
        }
        out
    }

    fn load_z80_snapshot(&mut self, bytes: &[u8], name: String) -> EmuResult<()> {
        if bytes.len() < 30 {
            return Err(EmuError::UnsupportedMedia("Truncated .z80 snapshot".into()));
        }
        self.bus.ram.fill(0);
        self.cpu.a = bytes[0];
        self.cpu.f = bytes[1];
        self.cpu.c = bytes[2];
        self.cpu.b = bytes[3];
        self.cpu.l = bytes[4];
        self.cpu.h = bytes[5];
        let pc_v1 = u16::from_le_bytes([bytes[6], bytes[7]]);
        self.cpu.sp = u16::from_le_bytes([bytes[8], bytes[9]]);
        self.cpu.i = bytes[10];
        let flags12 = if bytes[12] == 255 { 1 } else { bytes[12] };
        self.cpu.r = (bytes[11] & 0x7F) | ((flags12 & 0x01) << 7);
        self.bus.border = (flags12 >> 1) & 0x07;
        self.cpu.e = bytes[13];
        self.cpu.d = bytes[14];
        self.cpu.c_alt = bytes[15];
        self.cpu.b_alt = bytes[16];
        self.cpu.e_alt = bytes[17];
        self.cpu.d_alt = bytes[18];
        self.cpu.l_alt = bytes[19];
        self.cpu.h_alt = bytes[20];
        self.cpu.a_alt = bytes[21];
        self.cpu.f_alt = bytes[22];
        self.cpu.iy = u16::from_le_bytes([bytes[23], bytes[24]]);
        self.cpu.ix = u16::from_le_bytes([bytes[25], bytes[26]]);
        self.cpu.iff1 = bytes[27] != 0;
        self.cpu.iff2 = bytes[28] != 0;
        self.cpu.im = bytes[29] & 0x03;

        if pc_v1 != 0 {
            self.cpu.pc = pc_v1;
            let payload = &bytes[30..];
            let mem = if (flags12 & 0x20) != 0 {
                Self::decompress_z80(payload, 49_152, true)
            } else {
                payload[..payload.len().min(49_152)].to_vec()
            };
            let copy_len = mem.len().min(49_152);
            self.bus.ram[..copy_len].copy_from_slice(&mem[..copy_len]);
        } else {
            if bytes.len() < 34 {
                return Err(EmuError::UnsupportedMedia("Invalid v2/v3 .z80 header".into()));
            }
            let ext_len = u16::from_le_bytes([bytes[30], bytes[31]]) as usize;
            self.cpu.pc = u16::from_le_bytes([bytes[32], bytes[33]]);
            let mut pos = 32 + ext_len;
            while pos + 3 <= bytes.len() {
                let comp_len = u16::from_le_bytes([bytes[pos], bytes[pos + 1]]) as usize;
                let page = bytes[pos + 2];
                pos += 3;
                let raw_len = if comp_len == 0xFFFF { 16_384 } else { comp_len };
                if pos + raw_len > bytes.len() {
                    break;
                }
                let src = &bytes[pos..pos + raw_len];
                pos += raw_len;
                let page_data = if comp_len == 0xFFFF {
                    src.to_vec()
                } else {
                    Self::decompress_z80(src, 16_384, false)
                };
                let dst_offset = match page {
                    8 => Some(0x0000),
                    4 => Some(0x4000),
                    5 => Some(0x8000),
                    _ => None,
                };
                if let Some(off) = dst_offset {
                    let n = page_data.len().min(16_384);
                    self.bus.ram[off..off + n].copy_from_slice(&page_data[..n]);
                }
            }
        }
        self.bus.media_name = name;
        self.bus.render_screen(&mut self.screen);
        Ok(())
    }

    fn map_key(key: KeyCode) -> Option<(usize, u8)> {
        Some(match key {
            KeyCode::LShift => (0, 0x01),
            KeyCode::Z => (0, 0x02),
            KeyCode::X => (0, 0x04),
            KeyCode::C => (0, 0x08),
            KeyCode::V => (0, 0x10),
            KeyCode::A => (1, 0x01),
            KeyCode::S => (1, 0x02),
            KeyCode::D => (1, 0x04),
            KeyCode::F => (1, 0x08),
            KeyCode::G => (1, 0x10),
            KeyCode::Q => (2, 0x01),
            KeyCode::W => (2, 0x02),
            KeyCode::E => (2, 0x04),
            KeyCode::R => (2, 0x08),
            KeyCode::T => (2, 0x10),
            KeyCode::Num1 => (3, 0x01),
            KeyCode::Num2 => (3, 0x02),
            KeyCode::Num3 => (3, 0x04),
            KeyCode::Num4 => (3, 0x08),
            KeyCode::Num5 => (3, 0x10),
            KeyCode::Num0 => (4, 0x01),
            KeyCode::Num9 => (4, 0x02),
            KeyCode::Num8 => (4, 0x04),
            KeyCode::Num7 => (4, 0x08),
            KeyCode::Num6 => (4, 0x10),
            KeyCode::P => (5, 0x01),
            KeyCode::O => (5, 0x02),
            KeyCode::I => (5, 0x04),
            KeyCode::U => (5, 0x08),
            KeyCode::Y => (5, 0x10),
            KeyCode::Return => (6, 0x01),
            KeyCode::L => (6, 0x02),
            KeyCode::K => (6, 0x04),
            KeyCode::J => (6, 0x08),
            KeyCode::H => (6, 0x10),
            KeyCode::Space => (7, 0x01),
            KeyCode::RShift | KeyCode::LCtrl => (7, 0x02),
            KeyCode::M => (7, 0x04),
            KeyCode::N => (7, 0x08),
            KeyCode::B => (7, 0x10),
            _ => return None,
        })
    }
}

impl EmulatorCore for ZxSpectrum48k {
    fn system_type(&self) -> SystemType {
        SystemType::ZxSpectrum48k
    }

    fn system_name(&self) -> &str {
        "ZX Spectrum 48K (Z80 + ULA)"
    }

    fn media_name(&self) -> &str {
        &self.bus.media_name
    }

    fn reset(&mut self) {
        self.bus.reset_state();
        self.cpu.reset();
    }

    fn run_frame(&mut self) {
        if let Some((keys, frames_left)) = self.auto_type_queue.front_mut() {
            self.bus.key_rows = [0x1F; 8];
            for &(row, mask) in keys.iter() {
                self.bus.key_rows[row] &= !mask;
            }
            if *frames_left <= 1 {
                self.auto_type_queue.pop_front();
                if self.auto_type_queue.is_empty() {
                    self.bus.key_rows = [0x1F; 8];
                    if self.start_tape_after_keys {
                        self.start_tape_after_keys = false;
                        self.bus.tape.start();
                    }
                }
            } else {
                *frames_left -= 1;
            }
        }
        self.bus.audio_buf.clear();
        let mut cyc = 0u32;
        while cyc < 69_888 {
            let c = self.cpu.step(&mut self.bus);
            self.bus.tape.tick(c);
            cyc += c;
            self.bus.audio_accum += (c as f64 / 3_500_000.0) * AUDIO_SAMPLE_RATE as f64;
            while self.bus.audio_accum >= 1.0 {
                self.bus.audio_accum -= 1.0;
                self.bus.audio_buf.push(if self.bus.speaker || self.bus.tape.ear {
                    5000
                } else {
                    -5000
                });
            }
        }
        self.cpu.request_irq(0xFF);
        self.bus.frame_cnt = self.bus.frame_cnt.wrapping_add(1);
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
        if let Some((row, mask)) = Self::map_key(key) {
            self.bus.key_rows[row] &= !mask;
        } else if key == KeyCode::Backspace {
            self.bus.key_rows[0] &= !0x01;
            self.bus.key_rows[4] &= !0x01;
        }
    }

    fn handle_key_up(&mut self, key: KeyCode, _mods: KeyMod) {
        if let Some((row, mask)) = Self::map_key(key) {
            self.bus.key_rows[row] |= mask;
        } else if key == KeyCode::Backspace {
            self.bus.key_rows[0] |= 0x01;
            self.bus.key_rows[4] |= 0x01;
        }
    }

    fn load_media(&mut self, path: &Path) -> EmuResult<()> {
        let (name, ext, bytes) = read_media_or_zip(path, &["sna", "z80", "tap", "rom"])?;

        if ext == "z80" {
            return self.load_z80_snapshot(&bytes, name);
        }

        if ext == "tap" {
            self.bus.tape.load_tap(&bytes);
            if self.bus.tape.blocks.is_empty() {
                return Err(EmuError::UnsupportedMedia("Empty .tap tape file".into()));
            }
            self.reset();
            for _ in 0..120 {
                self.run_frame();
            }
            self.auto_type_queue.clear();
            // Type LOAD "" + Enter on Spectrum 48K keyboard (J = LOAD, Sym+P = ")
            self.auto_type_queue.push_back((vec![(6, 0x08)], 4));
            self.auto_type_queue.push_back((vec![], 4));
            self.auto_type_queue.push_back((vec![(7, 0x02), (5, 0x01)], 4));
            self.auto_type_queue.push_back((vec![], 4));
            self.auto_type_queue.push_back((vec![(7, 0x02), (5, 0x01)], 4));
            self.auto_type_queue.push_back((vec![], 4));
            self.auto_type_queue.push_back((vec![(6, 0x01)], 4));
            self.auto_type_queue.push_back((vec![], 4));
            self.start_tape_after_keys = true;
            self.bus.media_name = name;
            return Ok(());
        }

        if ext == "sna" && bytes.len() == 49_179 {
            self.bus.ram.copy_from_slice(&bytes[27..49_179]);
            self.cpu.i = bytes[0];
            self.cpu.l_alt = bytes[1];
            self.cpu.h_alt = bytes[2];
            self.cpu.e_alt = bytes[3];
            self.cpu.d_alt = bytes[4];
            self.cpu.c_alt = bytes[5];
            self.cpu.b_alt = bytes[6];
            self.cpu.f_alt = bytes[7];
            self.cpu.a_alt = bytes[8];
            self.cpu.l = bytes[9];
            self.cpu.h = bytes[10];
            self.cpu.e = bytes[11];
            self.cpu.d = bytes[12];
            self.cpu.c = bytes[13];
            self.cpu.b = bytes[14];
            self.cpu.iy = u16::from_le_bytes([bytes[15], bytes[16]]);
            self.cpu.ix = u16::from_le_bytes([bytes[17], bytes[18]]);
            let iff = (bytes[19] & 0x04) != 0;
            self.cpu.iff1 = iff;
            self.cpu.iff2 = iff;
            self.cpu.r = bytes[20];
            self.cpu.f = bytes[21];
            self.cpu.a = bytes[22];
            let mut sp = u16::from_le_bytes([bytes[23], bytes[24]]);
            self.cpu.im = bytes[25] & 3;
            self.bus.border = bytes[26] & 7;
            let lo = self.bus.read8(sp);
            let hi = self.bus.read8(sp.wrapping_add(1));
            self.cpu.pc = u16::from_le_bytes([lo, hi]);
            sp = sp.wrapping_add(2);
            self.cpu.sp = sp;
            self.bus.media_name = name;
            self.bus.render_screen(&mut self.screen);
            return Ok(());
        }

        if ext == "rom" && bytes.len() <= 16_384 {
            self.bus.rom.fill(0);
            self.bus.rom[..bytes.len()].copy_from_slice(&bytes);
            self.bus.media_name = name;
            self.reset();
            return Ok(());
        }

        Err(EmuError::UnsupportedMedia(format!("Unsupported Spectrum file: {name}")))
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
