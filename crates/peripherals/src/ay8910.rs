/// General Instrument AY-3-8910 / AY-3-8912 Programmable Sound Generator.
/// Used in Oric Atmos and Amstrad CPC 464.
#[derive(Debug, Clone)]
pub struct Ay8910 {
    pub regs: [u8; 16],
    pub selected: u8,
    pub port_a_in: u8,
    pub port_b_in: u8,
    tone_cnt: [u16; 3],
    tone_out: [bool; 3],
    noise_cnt: u16,
    noise_lfsr: u32,
    env_cnt: u32,
    env_step: i8,
    env_holding: bool,
    env_attack: bool,
    clock_hz: f64,
    phase_accum: f64,
}

impl Default for Ay8910 {
    fn default() -> Self {
        Self::new(1_000_000.0)
    }
}

impl Ay8910 {
    const VOL_TABLE: [i32; 16] = [
        0, 64, 91, 128, 181, 256, 362, 512, 724, 1024, 1448, 2048, 2896, 4096, 5793, 8191,
    ];

    pub fn new(clock_hz: f64) -> Self {
        let mut ay = Self {
            regs: [0; 16],
            selected: 0,
            port_a_in: 0xFF,
            port_b_in: 0xFF,
            tone_cnt: [0; 3],
            tone_out: [false; 3],
            noise_cnt: 0,
            noise_lfsr: 1,
            env_cnt: 0,
            env_step: 15,
            env_holding: false,
            env_attack: false,
            clock_hz,
            phase_accum: 0.0,
        };
        ay.reset();
        ay
    }

    pub fn reset(&mut self) {
        self.regs = [0; 16];
        self.regs[7] = 0x3F;
        self.selected = 0;
        self.tone_cnt = [0; 3];
        self.tone_out = [false; 3];
        self.noise_cnt = 0;
        self.noise_lfsr = 1;
        self.env_cnt = 0;
        self.env_step = 15;
        self.env_holding = false;
        self.env_attack = false;
        self.phase_accum = 0.0;
    }

    pub fn select_register(&mut self, reg: u8) {
        self.selected = reg & 0x0F;
    }

    pub fn write_data(&mut self, val: u8) {
        let idx = (self.selected & 0x0F) as usize;
        let masked = match idx {
            1 | 3 | 5 => val & 0x0F,
            6 | 8 | 9 | 10 => val & 0x1F,
            13 => val & 0x0F,
            _ => val,
        };
        self.regs[idx] = masked;
        if idx == 13 {
            self.env_cnt = 0;
            self.env_holding = false;
            self.env_attack = (masked & 0x04) != 0;
            self.env_step = if self.env_attack { 0 } else { 15 };
        }
    }

    pub fn read_data(&self) -> u8 {
        match self.selected & 0x0F {
            14 => {
                if (self.regs[7] & 0x40) != 0 {
                    self.regs[14]
                } else {
                    self.port_a_in
                }
            }
            15 => {
                if (self.regs[7] & 0x80) != 0 {
                    self.regs[15]
                } else {
                    self.port_b_in
                }
            }
            idx => self.regs[idx as usize],
        }
    }

    fn step_tick(&mut self) {
        for ch in 0..3usize {
            let period = (((self.regs[ch * 2 + 1] as u16) & 0x0F) << 8) | (self.regs[ch * 2] as u16);
            self.tone_cnt[ch] = self.tone_cnt[ch].wrapping_add(1);
            if self.tone_cnt[ch] >= period.max(1) {
                self.tone_cnt[ch] = 0;
                self.tone_out[ch] = !self.tone_out[ch];
            }
        }

        self.noise_cnt = self.noise_cnt.wrapping_add(1);
        let noise_period = ((self.regs[6] & 0x1F) as u16).max(1) * 2;
        if self.noise_cnt >= noise_period {
            self.noise_cnt = 0;
            let bit0 = self.noise_lfsr & 1;
            let bit3 = (self.noise_lfsr >> 3) & 1;
            self.noise_lfsr = (self.noise_lfsr >> 1) | ((bit0 ^ bit3) << 16);
        }

        if !self.env_holding {
            let env_period = (((self.regs[12] as u32) << 8) | (self.regs[11] as u32)).max(1) * 2;
            self.env_cnt += 1;
            if self.env_cnt >= env_period {
                self.env_cnt = 0;
                if self.env_attack {
                    self.env_step += 1;
                } else {
                    self.env_step -= 1;
                }
                if !(0..=15).contains(&self.env_step) {
                    let shape = self.regs[13];
                    let cont = (shape & 0x08) != 0;
                    let alt = (shape & 0x02) != 0;
                    let hold = (shape & 0x01) != 0;
                    if !cont {
                        self.env_step = 0;
                        self.env_holding = true;
                    } else if hold {
                        self.env_step = if self.env_attack ^ alt { 15 } else { 0 };
                        self.env_holding = true;
                    } else if alt {
                        self.env_attack = !self.env_attack;
                        self.env_step = if self.env_attack { 0 } else { 15 };
                    } else {
                        self.env_step = if self.env_attack { 0 } else { 15 };
                    }
                }
            }
        }
    }

    pub fn next_sample(&mut self, sample_rate_hz: f64) -> i16 {
        let ticks_per_sample = (self.clock_hz / 8.0) / sample_rate_hz;
        self.phase_accum += ticks_per_sample;
        let steps = self.phase_accum as u32;
        self.phase_accum -= steps as f64;
        for _ in 0..steps {
            self.step_tick();
        }

        let mixer = self.regs[7];
        let noise_hi = (self.noise_lfsr & 1) != 0;
        let mut acc = 0i32;
        for ch in 0..3usize {
            let tone_en = (mixer & (1 << ch)) == 0;
            let noise_en = (mixer & (8 << ch)) == 0;
            let gate = (!tone_en || self.tone_out[ch]) && (!noise_en || noise_hi);
            let vol_reg = self.regs[8 + ch];
            let level = if (vol_reg & 0x10) != 0 {
                self.env_step.clamp(0, 15) as usize
            } else {
                (vol_reg & 0x0F) as usize
            };
            let amp = Self::VOL_TABLE[level];
            acc += if gate { amp } else { -amp };
        }
        acc.clamp(-32767, 32767) as i16
    }
}
