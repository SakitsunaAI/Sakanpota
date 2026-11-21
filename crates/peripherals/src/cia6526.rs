/// MOS 6526 Complex Interface Adapter (CIA) for Commodore 64 (CIA1 & CIA2).
#[derive(Debug, Clone)]
pub struct Cia6526 {
    pub pra: u8,
    pub prb: u8,
    pub ddra: u8,
    pub ddrb: u8,
    pub pra_in: u8,
    pub prb_in: u8,
    pub ta: u16,
    pub ta_latch: u16,
    pub tb: u16,
    pub tb_latch: u16,
    pub tod_tenths: u8,
    pub tod_sec: u8,
    pub tod_min: u8,
    pub tod_hr: u8,
    pub tod_alarm: [u8; 4],
    pub sdr: u8,
    pub icr_data: u8,
    pub icr_mask: u8,
    pub cra: u8,
    pub crb: u8,
    tod_accum: u32,
}

impl Default for Cia6526 {
    fn default() -> Self {
        Self::new()
    }
}

impl Cia6526 {
    pub fn new() -> Self {
        let mut c = Self {
            pra: 0,
            prb: 0,
            ddra: 0,
            ddrb: 0,
            pra_in: 0xFF,
            prb_in: 0xFF,
            ta: 0xFFFF,
            ta_latch: 0xFFFF,
            tb: 0xFFFF,
            tb_latch: 0xFFFF,
            tod_tenths: 0,
            tod_sec: 0,
            tod_min: 0,
            tod_hr: 1,
            tod_alarm: [0; 4],
            sdr: 0,
            icr_data: 0,
            icr_mask: 0,
            cra: 0,
            crb: 0,
            tod_accum: 0,
        };
        c.reset();
        c
    }

    pub fn reset(&mut self) {
        self.pra = 0;
        self.prb = 0;
        self.ddra = 0;
        self.ddrb = 0;
        self.pra_in = 0xFF;
        self.prb_in = 0xFF;
        self.ta = 0xFFFF;
        self.ta_latch = 0xFFFF;
        self.tb = 0xFFFF;
        self.tb_latch = 0xFFFF;
        self.sdr = 0;
        self.icr_data = 0;
        self.icr_mask = 0;
        self.cra = 0;
        self.crb = 0;
        self.tod_accum = 0;
    }

    #[inline]
    pub fn irq_active(&self) -> bool {
        (self.icr_data & self.icr_mask & 0x1F) != 0
    }

    #[inline]
    pub fn port_a_pins(&self) -> u8 {
        (self.pra & self.ddra) | (self.pra_in & !self.ddra)
    }

    #[inline]
    pub fn port_b_pins(&self) -> u8 {
        (self.prb & self.ddrb) | (self.prb_in & !self.ddrb)
    }

    pub fn trigger_flag(&mut self) {
        self.icr_data |= 0x10;
    }

    pub fn tick(&mut self, cycles: u32) {
        let mut ta_underflows = 0u32;

        if (self.cra & 0x01) != 0 && (self.cra & 0x20) == 0 {
            let mut rem = cycles;
            while rem > 0 {
                if rem > self.ta as u32 {
                    rem -= (self.ta as u32) + 1;
                    self.icr_data |= 0x01;
                    ta_underflows += 1;
                    self.ta = self.ta_latch;
                    if (self.cra & 0x08) != 0 {
                        self.cra &= !0x01;
                        break;
                    }
                } else {
                    self.ta = self.ta.wrapping_sub(rem as u16);
                    break;
                }
            }
        }

        if (self.crb & 0x01) != 0 {
            let tb_mode = (self.crb >> 5) & 0x03;
            let tb_ticks = match tb_mode {
                0 => cycles,
                2 | 3 => ta_underflows,
                _ => 0,
            };
            if tb_ticks > 0 {
                let mut rem = tb_ticks;
                while rem > 0 {
                    if rem > self.tb as u32 {
                        rem -= (self.tb as u32) + 1;
                        self.icr_data |= 0x02;
                        self.tb = self.tb_latch;
                        if (self.crb & 0x08) != 0 {
                            self.crb &= !0x01;
                            break;
                        }
                    } else {
                        self.tb = self.tb.wrapping_sub(rem as u16);
                        break;
                    }
                }
            }
        }

        // Advance BCD TOD clock (~98524 Hz per 0.1s)
        self.tod_accum += cycles;
        while self.tod_accum >= 98_524 {
            self.tod_accum -= 98_524;
            self.tod_tenths = (self.tod_tenths + 1) % 10;
            if self.tod_tenths == 0 {
                let mut sec = (self.tod_sec >> 4) * 10 + (self.tod_sec & 0xF) + 1;
                if sec >= 60 {
                    sec = 0;
                    let mut min = (self.tod_min >> 4) * 10 + (self.tod_min & 0xF) + 1;
                    if min >= 60 {
                        min = 0;
                        let mut hr = ((self.tod_hr & 0x1F) >> 4) * 10 + (self.tod_hr & 0xF) + 1;
                        if hr > 12 {
                            hr = 1;
                        }
                        self.tod_hr = ((hr / 10) << 4) | (hr % 10);
                    }
                    self.tod_min = ((min / 10) << 4) | (min % 10);
                }
                self.tod_sec = ((sec / 10) << 4) | (sec % 10);
            }
            if [self.tod_tenths, self.tod_sec, self.tod_min, self.tod_hr] == self.tod_alarm {
                self.icr_data |= 0x04;
            }
        }
    }

    pub fn read(&mut self, reg: u8) -> u8 {
        match reg & 0x0F {
            0x0 => self.port_a_pins(),
            0x1 => self.port_b_pins(),
            0x2 => self.ddra,
            0x3 => self.ddrb,
            0x4 => (self.ta & 0xFF) as u8,
            0x5 => (self.ta >> 8) as u8,
            0x6 => (self.tb & 0xFF) as u8,
            0x7 => (self.tb >> 8) as u8,
            0x8 => self.tod_tenths,
            0x9 => self.tod_sec,
            0xA => self.tod_min,
            0xB => self.tod_hr,
            0xC => self.sdr,
            0xD => {
                let out = self.icr_data | if self.irq_active() { 0x80 } else { 0 };
                self.icr_data = 0;
                out
            }
            0xE => self.cra,
            _ => self.crb,
        }
    }

    pub fn peek(&self, reg: u8) -> u8 {
        match reg & 0x0F {
            0x0 => self.port_a_pins(),
            0x1 => self.port_b_pins(),
            0x2 => self.ddra,
            0x3 => self.ddrb,
            0x4 => (self.ta & 0xFF) as u8,
            0x5 => (self.ta >> 8) as u8,
            0x6 => (self.tb & 0xFF) as u8,
            0x7 => (self.tb >> 8) as u8,
            0x8 => self.tod_tenths,
            0x9 => self.tod_sec,
            0xA => self.tod_min,
            0xB => self.tod_hr,
            0xC => self.sdr,
            0xD => self.icr_data | if self.irq_active() { 0x80 } else { 0 },
            0xE => self.cra,
            _ => self.crb,
        }
    }

    pub fn write(&mut self, reg: u8, val: u8) {
        match reg & 0x0F {
            0x0 => self.pra = val,
            0x1 => self.prb = val,
            0x2 => self.ddra = val,
            0x3 => self.ddrb = val,
            0x4 => self.ta_latch = (self.ta_latch & 0xFF00) | (val as u16),
            0x5 => {
                self.ta_latch = (self.ta_latch & 0x00FF) | ((val as u16) << 8);
                if (self.cra & 0x01) == 0 {
                    self.ta = self.ta_latch;
                }
            }
            0x6 => self.tb_latch = (self.tb_latch & 0xFF00) | (val as u16),
            0x7 => {
                self.tb_latch = (self.tb_latch & 0x00FF) | ((val as u16) << 8);
                if (self.crb & 0x01) == 0 {
                    self.tb = self.tb_latch;
                }
            }
            0x8 => {
                if (self.crb & 0x80) != 0 {
                    self.tod_alarm[0] = val & 0x0F;
                } else {
                    self.tod_tenths = val & 0x0F;
                }
            }
            0x9 => {
                if (self.crb & 0x80) != 0 {
                    self.tod_alarm[1] = val & 0x7F;
                } else {
                    self.tod_sec = val & 0x7F;
                }
            }
            0xA => {
                if (self.crb & 0x80) != 0 {
                    self.tod_alarm[2] = val & 0x7F;
                } else {
                    self.tod_min = val & 0x7F;
                }
            }
            0xB => {
                if (self.crb & 0x80) != 0 {
                    self.tod_alarm[3] = val & 0x9F;
                } else {
                    self.tod_hr = val & 0x9F;
                }
            }
            0xC => {
                self.sdr = val;
                self.icr_data |= 0x08;
            }
            0xD => {
                if (val & 0x80) != 0 {
                    self.icr_mask |= val & 0x1F;
                } else {
                    self.icr_mask &= !(val & 0x1F);
                }
            }
            0xE => {
                self.cra = val & !0x10;
                if (val & 0x10) != 0 {
                    self.ta = self.ta_latch;
                }
            }
            _ => {
                self.crb = val & !0x10;
                if (val & 0x10) != 0 {
                    self.tb = self.tb_latch;
                }
            }
        }
    }
}
