/// MOS 6522 Versatile Interface Adapter (VIA).
/// Used in BBC Micro (System VIA & User VIA) and Oric Atmos.
#[derive(Debug, Clone)]
pub struct Via6522 {
    pub ora: u8,
    pub orb: u8,
    pub ira: u8,
    pub irb: u8,
    pub ddra: u8,
    pub ddrb: u8,
    pub t1c: u16,
    pub t1l: u16,
    pub t1_active: bool,
    pub t1_pb7: bool,
    pub t2c: u16,
    pub t2ll: u8,
    pub t2_active: bool,
    pub sr: u8,
    pub acr: u8,
    pub pcr: u8,
    pub ifr: u8,
    pub ier: u8,
    pub ca1: bool,
    pub ca2: bool,
    pub cb1: bool,
    pub cb2: bool,
    pub ca2_out: bool,
    pub cb2_out: bool,
}

impl Default for Via6522 {
    fn default() -> Self {
        Self::new()
    }
}

impl Via6522 {
    pub const IFR_CA2: u8 = 0x01;
    pub const IFR_CA1: u8 = 0x02;
    pub const IFR_SR: u8 = 0x04;
    pub const IFR_CB2: u8 = 0x08;
    pub const IFR_CB1: u8 = 0x10;
    pub const IFR_T2: u8 = 0x20;
    pub const IFR_T1: u8 = 0x40;

    pub fn new() -> Self {
        let mut v = Self {
            ora: 0,
            orb: 0,
            ira: 0xFF,
            irb: 0xFF,
            ddra: 0,
            ddrb: 0,
            t1c: 0xFFFF,
            t1l: 0xFFFF,
            t1_active: false,
            t1_pb7: true,
            t2c: 0xFFFF,
            t2ll: 0xFF,
            t2_active: false,
            sr: 0,
            acr: 0,
            pcr: 0,
            ifr: 0,
            ier: 0,
            ca1: true,
            ca2: true,
            cb1: true,
            cb2: true,
            ca2_out: true,
            cb2_out: true,
        };
        v.reset();
        v
    }

    pub fn reset(&mut self) {
        self.ora = 0;
        self.orb = 0;
        self.ira = 0xFF;
        self.irb = 0xFF;
        self.ddra = 0;
        self.ddrb = 0;
        self.t1c = 0xFFFF;
        self.t1l = 0xFFFF;
        self.t1_active = false;
        self.t1_pb7 = true;
        self.t2c = 0xFFFF;
        self.t2ll = 0xFF;
        self.t2_active = false;
        self.sr = 0;
        self.acr = 0;
        self.pcr = 0;
        self.ifr = 0;
        self.ier = 0;
        self.ca2_out = true;
        self.cb2_out = true;
    }

    #[inline]
    pub fn irq_active(&self) -> bool {
        (self.ifr & self.ier & 0x7F) != 0
    }

    #[inline]
    pub fn port_a_pins(&self) -> u8 {
        (self.ora & self.ddra) | (self.ira & !self.ddra)
    }

    #[inline]
    pub fn port_b_pins(&self) -> u8 {
        let mut out = (self.orb & self.ddrb) | (self.irb & !self.ddrb);
        if (self.acr & 0x80) != 0 {
            out = (out & 0x7F) | (if self.t1_pb7 { 0x80 } else { 0 });
        }
        out
    }

    pub fn set_ca1(&mut self, level: bool) {
        let pos_edge = (self.pcr & 0x01) != 0;
        if (pos_edge && !self.ca1 && level) || (!pos_edge && self.ca1 && !level) {
            self.ifr |= Self::IFR_CA1;
        }
        self.ca1 = level;
    }

    pub fn set_ca2(&mut self, level: bool) {
        let ca2_ctrl = (self.pcr >> 1) & 0x07;
        if ca2_ctrl < 4 {
            let pos_edge = (ca2_ctrl & 2) != 0;
            if (pos_edge && !self.ca2 && level) || (!pos_edge && self.ca2 && !level) {
                self.ifr |= Self::IFR_CA2;
            }
        }
        self.ca2 = level;
    }

    pub fn set_cb1(&mut self, level: bool) {
        let pos_edge = (self.pcr & 0x10) != 0;
        if (pos_edge && !self.cb1 && level) || (!pos_edge && self.cb1 && !level) {
            self.ifr |= Self::IFR_CB1;
        }
        self.cb1 = level;
    }

    pub fn set_cb2(&mut self, level: bool) {
        let cb2_ctrl = (self.pcr >> 5) & 0x07;
        if cb2_ctrl < 4 {
            let pos_edge = (cb2_ctrl & 2) != 0;
            if (pos_edge && !self.cb2 && level) || (!pos_edge && self.cb2 && !level) {
                self.ifr |= Self::IFR_CB2;
            }
        }
        self.cb2 = level;
    }

    pub fn tick(&mut self, cycles: u32) {
        if self.t1_active {
            let mut rem = cycles;
            while rem > 0 {
                if rem > self.t1c as u32 {
                    rem -= (self.t1c as u32) + 1;
                    self.ifr |= Self::IFR_T1;
                    self.t1_pb7 = !self.t1_pb7;
                    if (self.acr & 0x40) != 0 {
                        // Continuous free-running
                        self.t1c = self.t1l;
                    } else {
                        self.t1_active = false;
                        self.t1c = self.t1l;
                        self.t1c = self.t1c.wrapping_sub(rem as u16);
                        break;
                    }
                } else {
                    self.t1c = self.t1c.wrapping_sub(rem as u16);
                    break;
                }
            }
        } else {
            self.t1c = self.t1c.wrapping_sub(cycles as u16);
        }

        // T2 runs in timed mode unless ACR bit 5 == 1 (PB6 pulse counter)
        if (self.acr & 0x20) == 0 {
            let old = self.t2c;
            self.t2c = self.t2c.wrapping_sub(cycles as u16);
            if self.t2_active && cycles > old as u32 {
                self.ifr |= Self::IFR_T2;
                self.t2_active = false;
            }
        }
    }

    pub fn read(&mut self, reg: u8) -> u8 {
        match reg & 0x0F {
            0x0 => {
                // Reading IRB clears CB1 and independent CB2 interrupt flags
                self.ifr &= !Self::IFR_CB1;
                if ((self.pcr >> 5) & 0x07) == 0 || ((self.pcr >> 5) & 0x07) == 2 {
                    self.ifr &= !Self::IFR_CB2;
                }
                self.port_b_pins()
            }
            0x1 => {
                // Reading IRA clears CA1 and non-independent CA2 flags, and pulses CA2 if handshake
                self.ifr &= !Self::IFR_CA1;
                let ca2_ctrl = (self.pcr >> 1) & 0x07;
                if ca2_ctrl == 0 || ca2_ctrl == 2 {
                    self.ifr &= !Self::IFR_CA2;
                }
                if ca2_ctrl == 4 || ca2_ctrl == 5 {
                    self.ca2_out = false;
                }
                self.port_a_pins()
            }
            0x2 => self.ddrb,
            0x3 => self.ddra,
            0x4 => {
                self.ifr &= !Self::IFR_T1;
                (self.t1c & 0xFF) as u8
            }
            0x5 => (self.t1c >> 8) as u8,
            0x6 => (self.t1l & 0xFF) as u8,
            0x7 => (self.t1l >> 8) as u8,
            0x8 => {
                self.ifr &= !Self::IFR_T2;
                (self.t2c & 0xFF) as u8
            }
            0x9 => (self.t2c >> 8) as u8,
            0xA => {
                self.ifr &= !Self::IFR_SR;
                self.sr
            }
            0xB => self.acr,
            0xC => self.pcr,
            0xD => {
                let active = self.ifr & 0x7F;
                if (active & self.ier & 0x7F) != 0 {
                    active | 0x80
                } else {
                    active
                }
            }
            0xE => self.ier | 0x80,
            _ => self.port_a_pins(), // 0x0F ORA no handshake
        }
    }

    pub fn peek(&self, reg: u8) -> u8 {
        match reg & 0x0F {
            0x0 => self.port_b_pins(),
            0x1 | 0xF => self.port_a_pins(),
            0x2 => self.ddrb,
            0x3 => self.ddra,
            0x4 => (self.t1c & 0xFF) as u8,
            0x5 => (self.t1c >> 8) as u8,
            0x6 => (self.t1l & 0xFF) as u8,
            0x7 => (self.t1l >> 8) as u8,
            0x8 => (self.t2c & 0xFF) as u8,
            0x9 => (self.t2c >> 8) as u8,
            0xA => self.sr,
            0xB => self.acr,
            0xC => self.pcr,
            0xD => {
                let active = self.ifr & 0x7F;
                if (active & self.ier & 0x7F) != 0 {
                    active | 0x80
                } else {
                    active
                }
            }
            _ => self.ier | 0x80,
        }
    }

    pub fn write(&mut self, reg: u8, val: u8) {
        match reg & 0x0F {
            0x0 => {
                self.orb = val;
                self.ifr &= !Self::IFR_CB1;
                let cb2_ctrl = (self.pcr >> 5) & 0x07;
                if cb2_ctrl == 0 || cb2_ctrl == 2 {
                    self.ifr &= !Self::IFR_CB2;
                }
                if cb2_ctrl == 4 || cb2_ctrl == 5 {
                    self.cb2_out = false;
                }
            }
            0x1 => {
                self.ora = val;
                self.ifr &= !Self::IFR_CA1;
                let ca2_ctrl = (self.pcr >> 1) & 0x07;
                if ca2_ctrl == 0 || ca2_ctrl == 2 {
                    self.ifr &= !Self::IFR_CA2;
                }
                if ca2_ctrl == 4 || ca2_ctrl == 5 {
                    self.ca2_out = false;
                }
            }
            0x2 => self.ddrb = val,
            0x3 => self.ddra = val,
            0x4 | 0x6 => {
                self.t1l = (self.t1l & 0xFF00) | (val as u16);
            }
            0x5 => {
                self.t1l = (self.t1l & 0x00FF) | ((val as u16) << 8);
                self.t1c = self.t1l;
                self.t1_active = true;
                self.t1_pb7 = false;
                self.ifr &= !Self::IFR_T1;
            }
            0x7 => {
                self.t1l = (self.t1l & 0x00FF) | ((val as u16) << 8);
                self.ifr &= !Self::IFR_T1;
            }
            0x8 => self.t2ll = val,
            0x9 => {
                self.t2c = ((val as u16) << 8) | (self.t2ll as u16);
                self.t2_active = true;
                self.ifr &= !Self::IFR_T2;
            }
            0xA => {
                self.sr = val;
                self.ifr &= !Self::IFR_SR;
            }
            0xB => self.acr = val,
            0xC => {
                self.pcr = val;
                match (val >> 1) & 0x07 {
                    6 => self.ca2_out = false,
                    7 => self.ca2_out = true,
                    _ => {}
                }
                match (val >> 5) & 0x07 {
                    6 => self.cb2_out = false,
                    7 => self.cb2_out = true,
                    _ => {}
                }
            }
            0xD => {
                self.ifr &= !(val & 0x7F);
            }
            0xE => {
                if (val & 0x80) != 0 {
                    self.ier |= val & 0x7F;
                } else {
                    self.ier &= !(val & 0x7F);
                }
            }
            _ => {
                self.ora = val; // 0x0F ORA no handshake
            }
        }
    }
}
