use cpu_cores::Mos6502;
use emu_core::archive::load_rom_file;
use emu_core::types::{
    Bus8, CpuDebugState, DisassembledInstruction, EmuError, EmuResult, EmulatorCore, KeyCode,
    KeyMod, SystemType, AUDIO_SAMPLE_RATE, CANVAS_HEIGHT, CANVAS_WIDTH,
};
use std::fs;
use std::path::Path;

/// NTSC TIA 128-color palette (subset derived mathematically from NTSC YIQ phases).
fn ntsc_color(code: u8) -> u32 {
    let hue = (code >> 4) & 0x0F;
    let lum = ((code >> 1) & 0x07) as f32 / 7.0;
    let y = 0.08 + lum * 0.85;
    if hue == 0 {
        let g = (y * 255.0).clamp(0.0, 255.0) as u32;
        return 0xFF00_0000 | (g << 16) | (g << 8) | g;
    }
    let angle = ((hue as f32 - 1.0) * 24.0 + 33.0).to_radians();
    let sat = 0.28f32;
    let i = sat * angle.cos();
    let q = sat * angle.sin();
    let r = ((y + 0.956 * i + 0.621 * q) * 255.0).clamp(0.0, 255.0) as u32;
    let g = ((y - 0.272 * i - 0.647 * q) * 255.0).clamp(0.0, 255.0) as u32;
    let b = ((y - 1.106 * i + 1.703 * q) * 255.0).clamp(0.0, 255.0) as u32;
    0xFF00_0000 | (r << 16) | (g << 8) | b
}

struct TiaAudioChannel {
    audc: u8,
    audf: u8,
    audv: u8,
    div_cnt: u8,
    poly4: u8,
    poly5: u8,
    out_bit: bool,
}

impl TiaAudioChannel {
    fn new() -> Self {
        Self {
            audc: 0,
            audf: 0,
            audv: 0,
            div_cnt: 0,
            poly4: 0x0F,
            poly5: 0x1F,
            out_bit: false,
        }
    }

    fn tick_31khz(&mut self) -> i16 {
        if self.audv == 0 || (self.audc & 0x0F) == 0 {
            return 0;
        }
        if self.div_cnt == 0 {
            self.div_cnt = self.audf & 0x1F;
            let p5_bit0 = self.poly5 & 1;
            let p5_bit2 = (self.poly5 >> 2) & 1;
            self.poly5 = (self.poly5 >> 1) | ((p5_bit0 ^ p5_bit2) << 4);
            let p4_bit0 = self.poly4 & 1;
            let p4_bit1 = (self.poly4 >> 1) & 1;
            self.poly4 = (self.poly4 >> 1) | ((p4_bit0 ^ p4_bit1) << 3);

            self.out_bit = match self.audc & 0x0F {
                0x1..=0x3 => p5_bit0 != 0 && p4_bit0 != 0,
                0x4 | 0x5 => !self.out_bit,
                0x6 | 0x7 | 0x9 | 0xA | 0xE | 0xF => (self.poly5 & 1) != 0,
                0x8 => p5_bit0 != 0,
                0xC | 0xD => p4_bit0 != 0,
                _ => true,
            };
        } else {
            self.div_cnt -= 1;
        }
        let amp = (self.audv as i16 & 0x0F) * 700;
        if self.out_bit { amp } else { -amp }
    }
}

struct AtariBus {
    rom: Vec<u8>,
    rom_bank: usize,
    ram: [u8; 128],
    // RIOT 6532
    swcha_in: u8,
    swchb_in: u8,
    riot_timer: u8,
    riot_prescaler: u32,
    riot_accum: u32,
    riot_underflowed: bool,
    // TIA
    wsync_halt: bool,
    vsync: u8,
    vblank: u8,
    nusiz0: u8,
    nusiz1: u8,
    colup0: u8,
    colup1: u8,
    colupf: u8,
    colubk: u8,
    ctrlpf: u8,
    refp0: bool,
    refp1: bool,
    pf: [u8; 3],
    p0_x: i32,
    p1_x: i32,
    m0_x: i32,
    m1_x: i32,
    bl_x: i32,
    grp0: u8,
    grp0_old: u8,
    grp1: u8,
    grp1_old: u8,
    enam0: bool,
    enam1: bool,
    enabl: bool,
    enabl_old: bool,
    vdelp0: bool,
    vdelp1: bool,
    vdelbl: bool,
    hmp0: i8,
    hmp1: i8,
    hmm0: i8,
    hmm1: i8,
    hmbl: i8,
    resmp0: bool,
    resmp1: bool,
    cx: [u8; 8],
    inpt4: bool,
    aud: [TiaAudioChannel; 2],
    cur_scanline_clk: i32, // 0..228 color clocks
    audio_buf: Vec<i16>,
    audio_accum: f64,
    tia_aud_accum: f64,
    last_aud_sample: i16,
    media_name: String,
}

impl AtariBus {
    fn new(rom_dir: &Path) -> EmuResult<Self> {
        let rom = load_rom_file(rom_dir, "atari_pitfall.bin")?;
        Ok(Self {
            rom,
            rom_bank: 0,
            ram: [0; 128],
            swcha_in: 0xFF,
            swchb_in: 0x3F,
            riot_timer: 0xFF,
            riot_prescaler: 1024,
            riot_accum: 0,
            riot_underflowed: false,
            wsync_halt: false,
            vsync: 0,
            vblank: 0,
            nusiz0: 0,
            nusiz1: 0,
            colup0: 0,
            colup1: 0,
            colupf: 0,
            colubk: 0,
            ctrlpf: 0,
            refp0: false,
            refp1: false,
            pf: [0; 3],
            p0_x: 0,
            p1_x: 0,
            m0_x: 0,
            m1_x: 0,
            bl_x: 0,
            grp0: 0,
            grp0_old: 0,
            grp1: 0,
            grp1_old: 0,
            enam0: false,
            enam1: false,
            enabl: false,
            enabl_old: false,
            vdelp0: false,
            vdelp1: false,
            vdelbl: false,
            hmp0: 0,
            hmp1: 0,
            hmm0: 0,
            hmm1: 0,
            hmbl: 0,
            resmp0: false,
            resmp1: false,
            cx: [0; 8],
            inpt4: false,
            aud: [TiaAudioChannel::new(), TiaAudioChannel::new()],
            cur_scanline_clk: 0,
            audio_buf: Vec::with_capacity(1024),
            audio_accum: 0.0,
            tia_aud_accum: 0.0,
            last_aud_sample: 0,
            media_name: "atari_pitfall.bin".to_string(),
        })
    }

    fn reset_state(&mut self) {
        self.rom_bank = 0;
        self.ram.fill(0);
        self.riot_timer = 0xFF;
        self.riot_prescaler = 1024;
        self.riot_accum = 0;
        self.riot_underflowed = false;
        self.wsync_halt = false;
        self.vsync = 0;
        self.vblank = 0;
        self.pf = [0; 3];
        self.grp0 = 0;
        self.grp1 = 0;
        self.cx = [0; 8];
    }

    fn tick_riot(&mut self, cpu_cycles: u32) {
        if self.riot_underflowed {
            self.riot_timer = self.riot_timer.wrapping_sub(cpu_cycles as u8);
            return;
        }
        self.riot_accum += cpu_cycles;
        while self.riot_accum >= self.riot_prescaler {
            self.riot_accum -= self.riot_prescaler;
            if self.riot_timer == 0 {
                self.riot_underflowed = true;
                self.riot_timer = 0xFF;
                break;
            }
            self.riot_timer -= 1;
        }
    }

    fn read_cart(&self, addr12: u16) -> u8 {
        if self.rom.is_empty() {
            return 0xFF;
        }
        let bank_off = if self.rom.len() <= 4096 {
            (addr12 as usize) % self.rom.len()
        } else {
            self.rom_bank * 4096 + (addr12 as usize & 0x0FFF)
        };
        self.rom.get(bank_off).copied().unwrap_or(0xFF)
    }

    fn check_bankswitch(&mut self, addr12: u16) {
        let len = self.rom.len();
        if len == 8192 {
            if addr12 == 0x0FF8 {
                self.rom_bank = 0;
            } else if addr12 == 0x0FF9 {
                self.rom_bank = 1;
            }
        } else if len == 16_384 && (0x0FF6..=0x0FF9).contains(&addr12) {
            self.rom_bank = (addr12 - 0x0FF6) as usize;
        } else if len == 32_768 && (0x0FF4..=0x0FFB).contains(&addr12) {
            self.rom_bank = (addr12 - 0x0FF4) as usize;
        }
    }

    fn player_pixel_at(&self, px_vis: i32, x0: i32, nusiz: u8, grp: u8, reflect: bool) -> bool {
        if grp == 0 {
            return false;
        }
        let size_mode = nusiz & 7;
        let (copies, spacing, scale) = match size_mode {
            0 => (1, 0, 1),
            1 => (2, 16, 1),
            2 => (2, 32, 1),
            3 => (3, 16, 1),
            4 => (2, 64, 1),
            5 => (1, 0, 2),
            6 => (3, 32, 1),
            _ => (1, 0, 4),
        };
        for c in 0..copies {
            let start = x0 + c * spacing;
            let dx = px_vis - start;
            if dx >= 0 && dx < 8 * scale {
                let bit_idx = dx / scale;
                let bit = if reflect { bit_idx } else { 7 - bit_idx };
                if (grp & (1 << bit)) != 0 {
                    return true;
                }
            }
        }
        false
    }

    fn missile_pixel_at(&self, px_vis: i32, x0: i32, nusiz: u8, en: bool) -> bool {
        if !en {
            return false;
        }
        let width = 1 << ((nusiz >> 4) & 3);
        let dx = (px_vis - x0).rem_euclid(160);
        dx < width
    }

    fn playfield_pixel_at(&self, px_vis: i32) -> bool {
        let cell = px_vis / 4; // 0..39
        let half_cell = if cell < 20 {
            cell
        } else if (self.ctrlpf & 0x01) != 0 {
            39 - cell // Reflected
        } else {
            cell - 20
        };
        match half_cell {
            0..=3 => (self.pf[0] & (0x10 << half_cell)) != 0,
            4..=11 => (self.pf[1] & (0x80 >> (half_cell - 4))) != 0,
            _ => (self.pf[2] & (0x01 << (half_cell - 12))) != 0,
        }
    }

    fn draw_tia_pixel(&mut self, px_vis: i32) -> u32 {
        if (self.vblank & 0x02) != 0 || !(0..160).contains(&px_vis) {
            return 0xFF00_0000;
        }

        let grp0_active = if self.vdelp0 { self.grp0_old } else { self.grp0 };
        let grp1_active = if self.vdelp1 { self.grp1_old } else { self.grp1 };
        let enabl_active = if self.vdelbl { self.enabl_old } else { self.enabl };

        let p0 = self.player_pixel_at(px_vis, self.p0_x, self.nusiz0, grp0_active, self.refp0);
        let p1 = self.player_pixel_at(px_vis, self.p1_x, self.nusiz1, grp1_active, self.refp1);
        let m0 = self.missile_pixel_at(px_vis, self.m0_x, self.nusiz0, self.enam0 && !self.resmp0);
        let m1 = self.missile_pixel_at(px_vis, self.m1_x, self.nusiz1, self.enam1 && !self.resmp1);
        let bl = enabl_active && {
            let bw = 1 << ((self.ctrlpf >> 4) & 3);
            let dx = (px_vis - self.bl_x).rem_euclid(160);
            dx < bw
        };
        let pf = self.playfield_pixel_at(px_vis);

        if m0 && p1 { self.cx[0] |= 0x80; }
        if m0 && p0 { self.cx[0] |= 0x40; }
        if m1 && p0 { self.cx[1] |= 0x80; }
        if m1 && p1 { self.cx[1] |= 0x40; }
        if p0 && pf { self.cx[2] |= 0x80; }
        if p0 && bl { self.cx[2] |= 0x40; }
        if p1 && pf { self.cx[3] |= 0x80; }
        if p1 && bl { self.cx[3] |= 0x40; }
        if m0 && pf { self.cx[4] |= 0x80; }
        if m0 && bl { self.cx[4] |= 0x40; }
        if m1 && pf { self.cx[5] |= 0x80; }
        if m1 && bl { self.cx[5] |= 0x40; }
        if bl && pf { self.cx[6] |= 0x80; }
        if p0 && p1 { self.cx[7] |= 0x80; }
        if m0 && m1 { self.cx[7] |= 0x40; }

        let score_mode = (self.ctrlpf & 0x02) != 0;
        let pf_prio = (self.ctrlpf & 0x04) != 0;
        let pf_col = if score_mode {
            if px_vis < 80 { self.colup0 } else { self.colup1 }
        } else {
            self.colupf
        };

        let col = if pf_prio && (pf || bl) {
            pf_col
        } else if p0 || m0 {
            self.colup0
        } else if p1 || m1 {
            self.colup1
        } else if pf || bl {
            pf_col
        } else {
            self.colubk
        };

        ntsc_color(col)
    }
}

impl Bus8 for AtariBus {
    fn read8(&mut self, addr: u16) -> u8 {
        let a = addr & 0x1FFF;
        if (a & 0x1000) != 0 {
            let caddr = a & 0x0FFF;
            self.check_bankswitch(caddr);
            return self.read_cart(caddr);
        }
        if (a & 0x0080) == 0 {
            // TIA read (A7 = 0, A12 = 0)
            return match a & 0x0F {
                0x00..=0x07 => self.cx[(a & 0x07) as usize],
                0x08..=0x0B => 0x80,
                0x0C => {
                    if self.inpt4 { 0x00 } else { 0x80 }
                }
                0x0D => 0x80,
                _ => 0x00,
            };
        }
        if (a & 0x0200) == 0 {
            // RIOT RAM
            self.ram[(a & 0x7F) as usize]
        } else {
            // RIOT I/O & Timer
            match a & 0x07 {
                0x00 => self.swcha_in,
                0x02 => self.swchb_in,
                0x04 | 0x06 => {
                    self.riot_underflowed = false;
                    self.riot_timer
                }
                0x05 | 0x07 => {
                    if self.riot_underflowed { 0x80 } else { 0x00 }
                }
                _ => 0xFF,
            }
        }
    }

    fn write8(&mut self, addr: u16, val: u8) {
        let a = addr & 0x1FFF;
        if (a & 0x1000) != 0 {
            self.check_bankswitch(a & 0x0FFF);
            return;
        }
        if (a & 0x0080) == 0 {
            // TIA write
            let hm = (val as i8) >> 4;
            match a & 0x3F {
                0x00 => self.vsync = val,
                0x01 => self.vblank = val,
                0x02 => self.wsync_halt = true,
                0x03 => self.cur_scanline_clk = 0,
                0x04 => self.nusiz0 = val,
                0x05 => self.nusiz1 = val,
                0x06 => self.colup0 = val,
                0x07 => self.colup1 = val,
                0x08 => self.colupf = val,
                0x09 => self.colubk = val,
                0x0A => self.ctrlpf = val,
                0x0B => self.refp0 = (val & 0x08) != 0,
                0x0C => self.refp1 = (val & 0x08) != 0,
                0x0D => self.pf[0] = val,
                0x0E => self.pf[1] = val,
                0x0F => self.pf[2] = val,
                0x10 => self.p0_x = (self.cur_scanline_clk - 68 + 5).clamp(0, 159),
                0x11 => self.p1_x = (self.cur_scanline_clk - 68 + 5).clamp(0, 159),
                0x12 => self.m0_x = (self.cur_scanline_clk - 68 + 4).clamp(0, 159),
                0x13 => self.m1_x = (self.cur_scanline_clk - 68 + 4).clamp(0, 159),
                0x14 => self.bl_x = (self.cur_scanline_clk - 68 + 4).clamp(0, 159),
                0x15 => self.aud[0].audc = val,
                0x16 => self.aud[1].audc = val,
                0x17 => self.aud[0].audf = val,
                0x18 => self.aud[1].audf = val,
                0x19 => self.aud[0].audv = val,
                0x1A => self.aud[1].audv = val,
                0x1B => {
                    self.grp0 = val;
                    self.grp1_old = self.grp1;
                }
                0x1C => {
                    self.grp1 = val;
                    self.grp0_old = self.grp0;
                    self.enabl_old = self.enabl;
                }
                0x1D => self.enam0 = (val & 0x02) != 0,
                0x1E => self.enam1 = (val & 0x02) != 0,
                0x1F => self.enabl = (val & 0x02) != 0,
                0x20 => self.hmp0 = hm,
                0x21 => self.hmp1 = hm,
                0x22 => self.hmm0 = hm,
                0x23 => self.hmm1 = hm,
                0x24 => self.hmbl = hm,
                0x25 => self.vdelp0 = (val & 0x01) != 0,
                0x26 => self.vdelp1 = (val & 0x01) != 0,
                0x27 => self.vdelbl = (val & 0x01) != 0,
                0x28 => {
                    self.resmp0 = (val & 0x02) != 0;
                    if self.resmp0 {
                        self.m0_x = (self.p0_x + 4) % 160;
                    }
                }
                0x29 => {
                    self.resmp1 = (val & 0x02) != 0;
                    if self.resmp1 {
                        self.m1_x = (self.p1_x + 4) % 160;
                    }
                }
                0x2A => {
                    // HMOVE
                    self.p0_x = (self.p0_x - self.hmp0 as i32).rem_euclid(160);
                    self.p1_x = (self.p1_x - self.hmp1 as i32).rem_euclid(160);
                    self.m0_x = (self.m0_x - self.hmm0 as i32).rem_euclid(160);
                    self.m1_x = (self.m1_x - self.hmm1 as i32).rem_euclid(160);
                    self.bl_x = (self.bl_x - self.hmbl as i32).rem_euclid(160);
                }
                0x2B => {
                    self.hmp0 = 0;
                    self.hmp1 = 0;
                    self.hmm0 = 0;
                    self.hmm1 = 0;
                    self.hmbl = 0;
                }
                0x2C => self.cx = [0; 8],
                _ => {}
            }
        } else if (a & 0x0200) == 0 {
            self.ram[(a & 0x7F) as usize] = val;
        } else {
            let prescaler = match a & 0x1F {
                0x14 => Some(1),
                0x15 => Some(8),
                0x16 => Some(64),
                0x17 => Some(1024),
                _ => None,
            };
            if let Some(p) = prescaler {
                self.riot_prescaler = p;
                self.riot_timer = val;
                self.riot_accum = 0;
                self.riot_underflowed = false;
            }
        }
    }

    fn peek8(&self, addr: u16) -> u8 {
        let a = addr & 0x1FFF;
        if (a & 0x1000) != 0 {
            self.read_cart(a & 0x0FFF)
        } else if (a & 0x0080) != 0 && (a & 0x0200) == 0 {
            self.ram[(a & 0x7F) as usize]
        } else if (a & 0x0200) != 0 {
            match a & 7 {
                0 => self.swcha_in,
                2 => self.swchb_in,
                4 | 6 => self.riot_timer,
                _ => 0xFF,
            }
        } else {
            0
        }
    }
}

pub struct Atari2600 {
    cpu: Mos6502,
    bus: AtariBus,
    ntsc_frame: Vec<u32>, // 160 x 192
    screen: Vec<u32>,     // 640 x 512
}

impl Atari2600 {
    pub fn new(rom_dir: &Path) -> EmuResult<Self> {
        let mut bus = AtariBus::new(rom_dir)?;
        let mut cpu = Mos6502::new(false);
        cpu.reset(&mut bus);
        Ok(Self {
            cpu,
            bus,
            ntsc_frame: vec![0xFF00_0000; 160 * 192],
            screen: vec![0xFF00_0000; CANVAS_WIDTH * CANVAS_HEIGHT],
        })
    }

    fn advance_audio(&mut self, cpu_cycles: u32) {
        let tia_rate = 31_400.0f64;
        self.bus.tia_aud_accum += (cpu_cycles as f64 / 1_193_182.0) * tia_rate;
        while self.bus.tia_aud_accum >= 1.0 {
            self.bus.tia_aud_accum -= 1.0;
            let s0 = self.bus.aud[0].tick_31khz();
            let s1 = self.bus.aud[1].tick_31khz();
            self.bus.last_aud_sample = ((s0 as i32 + s1 as i32) / 2).clamp(-32767, 32767) as i16;
        }
        self.bus.audio_accum += (cpu_cycles as f64 / 1_193_182.0) * AUDIO_SAMPLE_RATE as f64;
        while self.bus.audio_accum >= 1.0 {
            self.bus.audio_accum -= 1.0;
            self.bus.audio_buf.push(self.bus.last_aud_sample);
        }
    }

    fn upscale_screen(&mut self) {
        // 160x192 -> centered 560x480 or 640x512 (4x horizontal = 640, ~2.6x vertical -> 2x or fill)
        for dy in 0..CANVAS_HEIGHT {
            let sy = (dy * 192) / CANVAS_HEIGHT;
            for dx in 0..CANVAS_WIDTH {
                let sx = (dx * 160) / CANVAS_WIDTH;
                self.screen[dy * CANVAS_WIDTH + dx] = self.ntsc_frame[sy * 160 + sx];
            }
        }
    }
}

impl EmulatorCore for Atari2600 {
    fn system_type(&self) -> SystemType {
        SystemType::Atari2600
    }

    fn system_name(&self) -> &str {
        "Atari 2600 VCS (6507 + TIA)"
    }

    fn media_name(&self) -> &str {
        &self.bus.media_name
    }

    fn reset(&mut self) {
        self.bus.reset_state();
        self.cpu.reset(&mut self.bus);
    }

    fn run_frame(&mut self) {
        self.bus.audio_buf.clear();
        for sl in 0..262i32 {
            self.bus.wsync_halt = false;
            self.bus.cur_scanline_clk = 0;
            while self.bus.cur_scanline_clk < 228 {
                let step_clks = if self.bus.wsync_halt {
                    let rem = 228 - self.bus.cur_scanline_clk;
                    let cyc = ((rem + 2) / 3) as u32;
                    self.bus.tick_riot(cyc);
                    self.advance_audio(cyc);
                    rem
                } else {
                    let cyc = self.cpu.step(&mut self.bus);
                    self.bus.tick_riot(cyc);
                    self.advance_audio(cyc);
                    (cyc * 3) as i32
                };

                let end_clk = (self.bus.cur_scanline_clk + step_clks).min(228);
                if (40..232).contains(&sl) {
                    let vy = (sl - 40) as usize;
                    for clk in self.bus.cur_scanline_clk..end_clk {
                        if clk >= 68 {
                            let vx = (clk - 68) as usize;
                            if vx < 160 {
                                self.ntsc_frame[vy * 160 + vx] = self.bus.draw_tia_pixel(vx as i32);
                            }
                        }
                    }
                }
                self.bus.cur_scanline_clk = end_clk;
            }
        }
        self.upscale_screen();
    }

    fn step_instruction(&mut self) -> u32 {
        let cyc = self.cpu.step(&mut self.bus);
        self.bus.tick_riot(cyc);
        self.advance_audio(cyc);
        self.upscale_screen();
        cyc
    }

    fn rendered_screen(&self) -> &[u32] {
        &self.screen
    }

    fn audio_buffer(&self) -> &[i16] {
        &self.bus.audio_buf
    }

    fn handle_key_down(&mut self, key: KeyCode, _mods: KeyMod) {
        match key {
            KeyCode::Up => self.bus.swcha_in &= !0x10,
            KeyCode::Down => self.bus.swcha_in &= !0x20,
            KeyCode::Left => self.bus.swcha_in &= !0x40,
            KeyCode::Right => self.bus.swcha_in &= !0x80,
            KeyCode::Space | KeyCode::Z | KeyCode::Return => self.bus.inpt4 = true,
            KeyCode::F1 => self.bus.swchb_in &= !0x01, // Game Reset
            KeyCode::F2 => self.bus.swchb_in &= !0x02, // Game Select
            _ => {}
        }
    }

    fn handle_key_up(&mut self, key: KeyCode, _mods: KeyMod) {
        match key {
            KeyCode::Up => self.bus.swcha_in |= 0x10,
            KeyCode::Down => self.bus.swcha_in |= 0x20,
            KeyCode::Left => self.bus.swcha_in |= 0x40,
            KeyCode::Right => self.bus.swcha_in |= 0x80,
            KeyCode::Space | KeyCode::Z | KeyCode::Return => self.bus.inpt4 = false,
            KeyCode::F1 => self.bus.swchb_in |= 0x01,
            KeyCode::F2 => self.bus.swchb_in |= 0x02,
            _ => {}
        }
    }

    fn load_media(&mut self, path: &Path) -> EmuResult<()> {
        let bytes = fs::read(path).map_err(|e| EmuError::IoError(path.display().to_string(), e))?;
        self.bus.rom = bytes;
        self.bus.media_name = path
            .file_name()
            .and_then(|s| s.to_str())
            .unwrap_or("atari.a26")
            .to_string();
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
