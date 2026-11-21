/// Intel 8255A Programmable Peripheral Interface (PPI).
/// Used in Amstrad CPC 464, IBM PC 5150, and Sharp MZ-700.
#[derive(Debug, Clone)]
pub struct Pia8255 {
    pub port_a_out: u8,
    pub port_b_out: u8,
    pub port_c_out: u8,
    pub port_a_in: u8,
    pub port_b_in: u8,
    pub port_c_in: u8,
    pub control: u8,
}

impl Default for Pia8255 {
    fn default() -> Self {
        Self::new()
    }
}

impl Pia8255 {
    pub fn new() -> Self {
        let mut p = Self {
            port_a_out: 0,
            port_b_out: 0,
            port_c_out: 0,
            port_a_in: 0xFF,
            port_b_in: 0xFF,
            port_c_in: 0xFF,
            control: 0x9B,
        };
        p.reset();
        p
    }

    pub fn reset(&mut self) {
        self.port_a_out = 0;
        self.port_b_out = 0;
        self.port_c_out = 0;
        self.control = 0x9B;
    }

    #[inline]
    pub fn port_a_is_input(&self) -> bool {
        (self.control & 0x10) != 0
    }

    #[inline]
    pub fn port_b_is_input(&self) -> bool {
        (self.control & 0x02) != 0
    }

    #[inline]
    pub fn port_c_hi_is_input(&self) -> bool {
        (self.control & 0x08) != 0
    }

    #[inline]
    pub fn port_c_lo_is_input(&self) -> bool {
        (self.control & 0x01) != 0
    }

    pub fn read(&self, port: u8) -> u8 {
        match port & 3 {
            0 => {
                if self.port_a_is_input() {
                    self.port_a_in
                } else {
                    self.port_a_out
                }
            }
            1 => {
                if self.port_b_is_input() {
                    self.port_b_in
                } else {
                    self.port_b_out
                }
            }
            2 => {
                let hi = if self.port_c_hi_is_input() {
                    self.port_c_in & 0xF0
                } else {
                    self.port_c_out & 0xF0
                };
                let lo = if self.port_c_lo_is_input() {
                    self.port_c_in & 0x0F
                } else {
                    self.port_c_out & 0x0F
                };
                hi | lo
            }
            _ => self.control,
        }
    }

    pub fn write(&mut self, port: u8, val: u8) {
        match port & 3 {
            0 => self.port_a_out = val,
            1 => self.port_b_out = val,
            2 => self.port_c_out = val,
            _ => {
                if (val & 0x80) != 0 {
                    self.control = val;
                    self.port_a_out = 0;
                    self.port_b_out = 0;
                    self.port_c_out = 0;
                } else {
                    // Bit Set/Reset (BSR) on Port C
                    let bit = (val >> 1) & 0x07;
                    if (val & 0x01) != 0 {
                        self.port_c_out |= 1 << bit;
                    } else {
                        self.port_c_out &= !(1 << bit);
                    }
                }
            }
        }
    }
}
