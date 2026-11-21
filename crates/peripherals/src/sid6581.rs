/// MOS 6581 SID Sound Interface Device (Commodore 64).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum EnvState {
    Attack,
    Decay,
    Sustain,
    Release,
}

#[derive(Debug, Clone)]
pub struct SidVoice {
    pub freq: u16,
    pub pw: u16,
    pub ctrl: u8,
    pub ad: u8,
    pub sr: u8,
    pub phase: u32,
    pub lfsr: u32,
    pub env_level: u8,
    env_state: EnvState,
    env_rate_cnt: u16,
    prev_msb: bool,
}

impl Default for SidVoice {
    fn default() -> Self {
        Self::new()
    }
}

impl SidVoice {
    const RATE_PERIODS: [u16; 16] = [
        9, 32, 63, 95, 149, 220, 267, 313, 392, 977, 1954, 3126, 3907, 11_720, 19_532, 31_251,
    ];

    pub fn new() -> Self {
        Self {
            freq: 0,
            pw: 0,
            ctrl: 0,
            ad: 0,
            sr: 0,
            phase: 0,
            lfsr: 0x7F_FFFF,
            env_level: 0,
            env_state: EnvState::Release,
            env_rate_cnt: 0,
            prev_msb: false,
        }
    }

    pub fn set_ctrl(&mut self, val: u8) {
        let old_gate = (self.ctrl & 1) != 0;
        let new_gate = (val & 1) != 0;
        self.ctrl = val;
        if !old_gate && new_gate {
            self.env_state = EnvState::Attack;
            self.env_rate_cnt = 0;
        } else if old_gate && !new_gate {
            self.env_state = EnvState::Release;
            self.env_rate_cnt = 0;
        }
    }

    pub fn step_cycle(&mut self, cycles: u32, sync_source_msb: bool) -> bool {
        if (self.ctrl & 0x08) != 0 {
            self.phase = 0;
        } else {
            let delta = (self.freq as u32) * cycles;
            let old_bit19 = (self.phase & (1 << 19)) != 0;
            self.phase = (self.phase.wrapping_add(delta)) & 0x00FF_FFFF;
            let new_bit19 = (self.phase & (1 << 19)) != 0;
            if !old_bit19 && new_bit19 {
                let bit22 = (self.lfsr >> 22) & 1;
                let bit17 = (self.lfsr >> 17) & 1;
                self.lfsr = ((self.lfsr << 1) | (bit22 ^ bit17)) & 0x7F_FFFF;
            }
        }

        let msb = (self.phase & 0x80_0000) != 0;
        let rising_msb = !self.prev_msb && msb;
        self.prev_msb = msb;

        let _ = sync_source_msb; // Caller applies hard sync when requested

        // ADSR step
        self.env_rate_cnt = self.env_rate_cnt.saturating_add(cycles as u16);
        let rate_idx = match self.env_state {
            EnvState::Attack => (self.ad >> 4) & 0xF,
            EnvState::Decay => self.ad & 0xF,
            EnvState::Sustain => 0,
            EnvState::Release => self.sr & 0xF,
        };
        let threshold = Self::RATE_PERIODS[rate_idx as usize];
        if self.env_rate_cnt >= threshold {
            self.env_rate_cnt %= threshold;
            let sustain = (self.sr >> 4) * 17;
            match self.env_state {
                EnvState::Attack => {
                    if self.env_level == 255 {
                        self.env_state = EnvState::Decay;
                    } else {
                        self.env_level = self.env_level.saturating_add(1);
                    }
                }
                EnvState::Decay => {
                    if self.env_level > sustain {
                        self.env_level -= 1;
                    }
                    if self.env_level <= sustain {
                        self.env_state = EnvState::Sustain;
                    }
                }
                EnvState::Sustain => {}
                EnvState::Release => {
                    self.env_level = self.env_level.saturating_sub(1);
                }
            }
        }

        rising_msb
    }

    pub fn waveform_output(&self, ring_mod_msb: bool) -> i32 {
        let p12 = (self.phase >> 12) as i32; // 0..4095
        let mut wave = 0i32;
        let mut active_waves = 0;

        if (self.ctrl & 0x10) != 0 {
            // Triangle
            let mut msb = (self.phase & 0x80_0000) != 0;
            if (self.ctrl & 0x04) != 0 {
                msb ^= ring_mod_msb;
            }
            let tri = if msb { 4095 - p12 } else { p12 };
            wave += tri - 2048;
            active_waves += 1;
        }
        if (self.ctrl & 0x20) != 0 {
            // Sawtooth
            wave += p12 - 2048;
            active_waves += 1;
        }
        if (self.ctrl & 0x40) != 0 {
            // Pulse
            let pw = (self.pw & 0x0FFF) as i32;
            wave += if p12 >= pw { 2047 } else { -2048 };
            active_waves += 1;
        }
        if (self.ctrl & 0x80) != 0 {
            // Noise
            let n8 = (((self.lfsr >> 15) & 0xFF) as i32) << 4;
            wave += n8 - 2048;
            active_waves += 1;
        }

        if active_waves == 0 {
            return 0;
        }
        ((wave / active_waves) * (self.env_level as i32)) >> 8
    }
}

#[derive(Debug, Clone)]
pub struct Sid6581 {
    pub voices: [SidVoice; 3],
    pub filter_cutoff: u16,
    pub res_filt: u8,
    pub mode_vol: u8,
    pub pot_x: u8,
    pub pot_y: u8,
    lp: f32,
    bp: f32,
    clock_hz: f64,
    phase_accum: f64,
}

impl Default for Sid6581 {
    fn default() -> Self {
        Self::new(985_248.0)
    }
}

impl Sid6581 {
    pub fn new(clock_hz: f64) -> Self {
        Self {
            voices: [SidVoice::new(), SidVoice::new(), SidVoice::new()],
            filter_cutoff: 0,
            res_filt: 0,
            mode_vol: 0,
            pot_x: 0xFF,
            pot_y: 0xFF,
            lp: 0.0,
            bp: 0.0,
            clock_hz,
            phase_accum: 0.0,
        }
    }

    pub fn reset(&mut self) {
        self.voices = [SidVoice::new(), SidVoice::new(), SidVoice::new()];
        self.filter_cutoff = 0;
        self.res_filt = 0;
        self.mode_vol = 0;
        self.lp = 0.0;
        self.bp = 0.0;
        self.phase_accum = 0.0;
    }

    pub fn read(&self, reg: u8) -> u8 {
        match reg & 0x1F {
            0x19 => self.pot_x,
            0x1A => self.pot_y,
            0x1B => ((self.voices[2].phase >> 16) & 0xFF) as u8,
            0x1C => self.voices[2].env_level,
            _ => 0,
        }
    }

    pub fn write(&mut self, reg: u8, val: u8) {
        let r = reg & 0x1F;
        if r < 21 {
            let v = &mut self.voices[(r / 7) as usize];
            match r % 7 {
                0 => v.freq = (v.freq & 0xFF00) | (val as u16),
                1 => v.freq = (v.freq & 0x00FF) | ((val as u16) << 8),
                2 => v.pw = (v.pw & 0x0F00) | (val as u16),
                3 => v.pw = (v.pw & 0x00FF) | (((val & 0x0F) as u16) << 8),
                4 => v.set_ctrl(val),
                5 => v.ad = val,
                _ => v.sr = val,
            }
            return;
        }
        match r {
            0x15 => self.filter_cutoff = (self.filter_cutoff & 0x7F8) | ((val & 0x07) as u16),
            0x16 => self.filter_cutoff = (self.filter_cutoff & 0x007) | ((val as u16) << 3),
            0x17 => self.res_filt = val,
            0x18 => self.mode_vol = val,
            _ => {}
        }
    }

    pub fn next_sample(&mut self, sample_rate_hz: f64) -> i16 {
        let cyc_f = self.clock_hz / sample_rate_hz;
        self.phase_accum += cyc_f;
        let cyc = self.phase_accum as u32;
        self.phase_accum -= cyc as f64;

        if cyc > 0 {
            let m0 = self.voices[0].step_cycle(cyc, false);
            let m1 = self.voices[1].step_cycle(cyc, m0);
            let m2 = self.voices[2].step_cycle(cyc, m1);
            if m0 && (self.voices[1].ctrl & 0x02) != 0 {
                self.voices[1].phase = 0;
            }
            if m1 && (self.voices[2].ctrl & 0x02) != 0 {
                self.voices[2].phase = 0;
            }
            if m2 && (self.voices[0].ctrl & 0x02) != 0 {
                self.voices[0].phase = 0;
            }
        }

        let o0 = self.voices[0].waveform_output((self.voices[2].phase & 0x80_0000) != 0) as f32;
        let o1 = self.voices[1].waveform_output((self.voices[0].phase & 0x80_0000) != 0) as f32;
        let o2 = if (self.mode_vol & 0x80) != 0 {
            0.0
        } else {
            self.voices[2].waveform_output((self.voices[1].phase & 0x80_0000) != 0) as f32
        };

        let mut filt_in = 0.0f32;
        let mut direct = 0.0f32;
        for (i, o) in [o0, o1, o2].into_iter().enumerate() {
            if (self.res_filt & (1 << i)) != 0 {
                filt_in += o;
            } else {
                direct += o;
            }
        }

        let cutoff_hz = 30.0 + (self.filter_cutoff as f32 / 2047.0) * 11_970.0;
        let f = (2.0 * std::f32::consts::PI * cutoff_hz / sample_rate_hz as f32).min(0.45);
        let q = 1.0 / (0.7 + ((self.res_filt >> 4) as f32) * 0.45);
        let hp = filt_in - self.lp - q * self.bp;
        self.bp += f * hp;
        self.lp += f * self.bp;

        let mut filt_out = 0.0f32;
        if (self.mode_vol & 0x10) != 0 {
            filt_out += self.lp;
        }
        if (self.mode_vol & 0x20) != 0 {
            filt_out += self.bp;
        }
        if (self.mode_vol & 0x40) != 0 {
            filt_out += hp;
        }
        if (self.res_filt & 0x07) == 0 {
            filt_out = 0.0;
        }

        let vol = (self.mode_vol & 0x0F) as f32 / 15.0;
        let mixed = (direct + filt_out) * vol * 3.5;
        (mixed.clamp(-32767.0, 32767.0)) as i16
    }
}
