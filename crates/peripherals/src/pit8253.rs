/// Intel 8253 / 8254 Programmable Interval Timer (PIT).
/// Used in IBM PC 5150 (1.193182 MHz) and Sharp MZ-700.
#[derive(Debug, Clone)]
pub struct PitChannel {
    pub count: u16,
    pub reload: u16,
    pub latch: u16,
    pub latched: bool,
    pub mode: u8,
    pub rw_mode: u8,
    pub bcd: bool,
    pub out: bool,
    pub gate: bool,
    pub active: bool,
    write_lsb_next: bool,
    read_lsb_next: bool,
}

impl Default for PitChannel {
    fn default() -> Self {
        Self::new()
    }
}

impl PitChannel {
    pub fn new() -> Self {
        Self {
            count: 0,
            reload: 0,
            latch: 0,
            latched: false,
            mode: 3,
            rw_mode: 3,
            bcd: false,
            out: false,
            gate: true,
            active: false,
            write_lsb_next: true,
            read_lsb_next: true,
        }
    }

    pub fn effective_reload(&self) -> u32 {
        if self.reload == 0 {
            if self.bcd { 10_000 } else { 65_536 }
        } else {
            self.reload as u32
        }
    }

    /// Advance channel by `ticks` input clock pulses. Returns number of rising OUT edges.
    pub fn tick(&mut self, ticks: u32) -> u32 {
        if !self.active || !self.gate || ticks == 0 {
            return 0;
        }
        let reload = self.effective_reload();
        let mut cur = if self.count == 0 { reload } else { self.count as u32 };
        let mut rising_edges = 0u32;

        match self.mode {
            0 => {
                // Interrupt on terminal count
                if ticks >= cur {
                    if !self.out {
                        rising_edges = 1;
                    }
                    self.out = true;
                    let rem = ticks - cur;
                    let wrapped = reload.wrapping_sub(rem % reload.max(1));
                    self.count = (wrapped & 0xFFFF) as u16;
                } else {
                    self.count = (cur - ticks) as u16;
                }
            }
            2 => {
                // Rate generator
                let mut rem = ticks;
                while rem >= cur {
                    rem -= cur;
                    cur = reload;
                    self.out = true;
                    rising_edges += 1;
                }
                cur -= rem;
                if cur <= 1 {
                    self.out = false;
                }
                self.count = (cur & 0xFFFF) as u16;
            }
            3 => {
                // Square wave generator (counts down by 2 per cycle logically -> half period)
                let step2 = ticks.saturating_mul(2);
                let mut rem = step2;
                let half = reload.max(2);
                while rem >= cur {
                    rem -= cur;
                    cur = half;
                    self.out = !self.out;
                    if self.out {
                        rising_edges += 1;
                    }
                }
                cur -= rem;
                self.count = (cur & 0xFFFF) as u16;
            }
            _ => {
                // Modes 1/4/5 fallback countdown
                if ticks >= cur {
                    if !self.out {
                        rising_edges = 1;
                    }
                    self.out = true;
                    self.active = false;
                    self.count = 0;
                } else {
                    self.count = (cur - ticks) as u16;
                }
            }
        }
        rising_edges
    }
}

#[derive(Debug, Clone)]
pub struct Pit8253 {
    pub channels: [PitChannel; 3],
}

impl Default for Pit8253 {
    fn default() -> Self {
        Self::new()
    }
}

impl Pit8253 {
    pub fn new() -> Self {
        Self {
            channels: [PitChannel::new(), PitChannel::new(), PitChannel::new()],
        }
    }

    pub fn reset(&mut self) {
        self.channels = [PitChannel::new(), PitChannel::new(), PitChannel::new()];
    }

    pub fn read(&mut self, reg: u8) -> u8 {
        let idx = (reg & 3) as usize;
        if idx == 3 {
            return 0xFF;
        }
        let ch = &mut self.channels[idx];
        let val = if ch.latched { ch.latch } else { ch.count };
        match ch.rw_mode {
            1 => {
                ch.latched = false;
                (val & 0xFF) as u8
            }
            2 => {
                ch.latched = false;
                (val >> 8) as u8
            }
            3 => {
                if ch.read_lsb_next {
                    ch.read_lsb_next = false;
                    (val & 0xFF) as u8
                } else {
                    ch.read_lsb_next = true;
                    ch.latched = false;
                    (val >> 8) as u8
                }
            }
            _ => 0xFF,
        }
    }

    pub fn write(&mut self, reg: u8, val: u8) {
        let idx = (reg & 3) as usize;
        if idx == 3 {
            let sel = ((val >> 6) & 3) as usize;
            if sel < 3 {
                let ch = &mut self.channels[sel];
                let rw = (val >> 4) & 3;
                if rw == 0 {
                    ch.latch = ch.count;
                    ch.latched = true;
                    ch.read_lsb_next = true;
                } else {
                    ch.rw_mode = rw;
                    ch.mode = ((val >> 1) & 7) % 6;
                    if ch.mode == 4 || ch.mode == 5 {
                        ch.out = true;
                    } else {
                        ch.out = ch.mode != 0;
                    }
                    ch.bcd = (val & 1) != 0;
                    ch.write_lsb_next = true;
                    ch.read_lsb_next = true;
                    ch.active = false;
                }
            }
            return;
        }

        let ch = &mut self.channels[idx];
        match ch.rw_mode {
            1 => {
                ch.reload = (ch.reload & 0xFF00) | (val as u16);
                ch.count = ch.reload;
                ch.active = true;
            }
            2 => {
                ch.reload = (ch.reload & 0x00FF) | ((val as u16) << 8);
                ch.count = ch.reload;
                ch.active = true;
            }
            3 => {
                if ch.write_lsb_next {
                    ch.reload = (ch.reload & 0xFF00) | (val as u16);
                    ch.write_lsb_next = false;
                } else {
                    ch.reload = (ch.reload & 0x00FF) | ((val as u16) << 8);
                    ch.write_lsb_next = true;
                    ch.count = ch.reload;
                    ch.active = true;
                    if ch.mode == 0 {
                        ch.out = false;
                    }
                }
            }
            _ => {}
        }
    }

    /// Tick all 3 channels. Returns `(ch0_irq_edges, ch2_out_state)`.
    pub fn tick(&mut self, ticks: u32) -> (u32, bool) {
        let irq0 = self.channels[0].tick(ticks);
        let _ = self.channels[1].tick(ticks);
        let _ = self.channels[2].tick(ticks);
        (irq0, self.channels[2].out)
    }
}
