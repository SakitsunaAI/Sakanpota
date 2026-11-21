/// Texas Instruments SN76489 Programmable Sound Generator (3 tone + 1 noise).
/// Used in BBC Micro and TI-99/4A.
#[derive(Debug, Clone)]
pub struct Sn76489 {
    pub regs: [u16; 8],
    pub latch: usize,
    counters: [i32; 4],
    polarity: [bool; 4],
    lfsr: u16,
    clock_hz: f64,
    phase_accum: f64,
}

impl Default for Sn76489 {
    fn default() -> Self {
        Self::new(4_000_000.0)
    }
}

impl Sn76489 {
    const VOL_TABLE: [i32; 16] = [
        4095, 3253, 2584, 2052, 1630, 1295, 1028, 817, 649, 515, 409, 325, 258, 205, 163, 0,
    ];

    pub fn new(clock_hz: f64) -> Self {
        let mut s = Self {
            regs: [0, 15, 0, 15, 0, 15, 0, 15],
            latch: 0,
            counters: [0; 4],
            polarity: [true; 4],
            lfsr: 0x8000,
            clock_hz,
            phase_accum: 0.0,
        };
        s.reset();
        s
    }

    pub fn reset(&mut self) {
        self.regs = [0, 15, 0, 15, 0, 15, 0, 15];
        self.latch = 0;
        self.counters = [0; 4];
        self.polarity = [true; 4];
        self.lfsr = 0x8000;
        self.phase_accum = 0.0;
    }

    pub fn write(&mut self, val: u8) {
        if (val & 0x80) != 0 {
            self.latch = ((val >> 4) & 0x07) as usize;
            let low4 = (val & 0x0F) as u16;
            if (self.latch & 1) != 0 || self.latch == 6 {
                self.regs[self.latch] = low4;
                if self.latch == 6 {
                    self.lfsr = 0x8000;
                }
            } else {
                self.regs[self.latch] = (self.regs[self.latch] & 0x3F0) | low4;
            }
        } else if (self.latch & 1) != 0 || self.latch == 6 {
            self.regs[self.latch] = (val & 0x0F) as u16;
            if self.latch == 6 {
                self.lfsr = 0x8000;
            }
        } else {
            self.regs[self.latch] = (self.regs[self.latch] & 0x00F) | (((val & 0x3F) as u16) << 4);
        }
    }

    fn step_psg_tick(&mut self) {
        for ch in 0..3usize {
            self.counters[ch] -= 1;
            if self.counters[ch] <= 0 {
                let period = self.regs[ch * 2].max(1) as i32;
                self.counters[ch] = period;
                self.polarity[ch] = !self.polarity[ch];
            }
        }

        self.counters[3] -= 1;
        if self.counters[3] <= 0 {
            let noise_ctrl = self.regs[6] & 0x03;
            let period = match noise_ctrl {
                0 => 0x10,
                1 => 0x20,
                2 => 0x40,
                _ => self.regs[4].max(1) as i32,
            };
            self.counters[3] = period;
            self.polarity[3] = !self.polarity[3];
            if self.polarity[3] {
                let white = (self.regs[6] & 0x04) != 0;
                let feedback = if white {
                    (self.lfsr & 1) ^ ((self.lfsr >> 3) & 1)
                } else {
                    self.lfsr & 1
                };
                self.lfsr = (self.lfsr >> 1) | (feedback << 15);
            }
        }
    }

    pub fn next_sample(&mut self, sample_rate_hz: f64) -> i16 {
        let ticks_per_sample = (self.clock_hz / 16.0) / sample_rate_hz;
        self.phase_accum += ticks_per_sample;
        let steps = self.phase_accum as u32;
        self.phase_accum -= steps as f64;
        for _ in 0..steps {
            self.step_psg_tick();
        }

        let mut acc = 0i32;
        for ch in 0..3usize {
            let vol = Self::VOL_TABLE[(self.regs[ch * 2 + 1] & 0xF) as usize];
            let active = if self.regs[ch * 2] <= 1 || self.polarity[ch] {
                1
            } else {
                -1
            };
            acc += vol * active;
        }
        let n_vol = Self::VOL_TABLE[(self.regs[7] & 0xF) as usize];
        let n_out = if (self.lfsr & 1) != 0 { 1 } else { -1 };
        acc += n_vol * n_out;
        (acc.clamp(-32767, 32767)) as i16
    }

    pub fn render_samples(&mut self, out: &mut Vec<i16>, count: usize, sample_rate_hz: f64) {
        out.reserve(count);
        for _ in 0..count {
            out.push(self.next_sample(sample_rate_hz));
        }
    }
}
