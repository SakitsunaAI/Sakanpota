/// Intel 8259A Programmable Interrupt Controller (PIC).
/// Used in IBM PC 5150 (I/O ports 0x20 / 0x21, IRQ0..IRQ7 -> INT 0x08..0x0F).
#[derive(Debug, Clone)]
pub struct Pic8259 {
    pub irr: u8,
    pub isr: u8,
    pub imr: u8,
    pub icw_step: u8,
    pub icw1: u8,
    pub vector_base: u8,
    pub icw4: u8,
    pub auto_eoi: bool,
    pub read_isr: bool,
}

impl Default for Pic8259 {
    fn default() -> Self {
        Self::new()
    }
}

impl Pic8259 {
    pub fn new() -> Self {
        let mut p = Self {
            irr: 0,
            isr: 0,
            imr: 0,
            icw_step: 0,
            icw1: 0,
            vector_base: 0x08,
            icw4: 0,
            auto_eoi: false,
            read_isr: false,
        };
        p.reset();
        p
    }

    pub fn reset(&mut self) {
        self.irr = 0;
        self.isr = 0;
        self.imr = 0;
        self.icw_step = 0;
        self.icw1 = 0;
        self.vector_base = 0x08;
        self.icw4 = 0;
        self.auto_eoi = false;
        self.read_isr = false;
    }

    pub fn raise_irq(&mut self, line: u8) {
        self.irr |= 1 << (line & 7);
    }

    pub fn clear_irq(&mut self, line: u8) {
        self.irr &= !(1 << (line & 7));
    }

    pub fn acknowledge_irq(&mut self) -> Option<u8> {
        let pending = self.irr & !self.imr;
        if pending == 0 {
            return None;
        }
        let highest_pending = pending.trailing_zeros() as u8;
        if self.isr != 0 {
            let highest_in_service = self.isr.trailing_zeros() as u8;
            if highest_pending >= highest_in_service {
                return None;
            }
        }
        let mask = 1u8 << highest_pending;
        self.irr &= !mask;
        if !self.auto_eoi {
            self.isr |= mask;
        }
        Some((self.vector_base & 0xF8) | highest_pending)
    }

    pub fn read(&self, addr_low_bit: bool) -> u8 {
        if !addr_low_bit {
            if self.read_isr { self.isr } else { self.irr }
        } else {
            self.imr
        }
    }

    pub fn write(&mut self, addr_low_bit: bool, val: u8) {
        if !addr_low_bit {
            if (val & 0x10) != 0 {
                // ICW1
                self.icw1 = val;
                self.imr = 0;
                self.isr = 0;
                self.irr = 0;
                self.icw_step = 2;
                self.auto_eoi = false;
            } else if (val & 0x08) != 0 {
                // OCW3
                if (val & 0x02) != 0 {
                    self.read_isr = (val & 0x01) != 0;
                }
            } else {
                // OCW2
                let eoi_cmd = (val >> 5) & 7;
                if eoi_cmd == 1 {
                    // Non-specific EOI: clear highest-priority in-service bit
                    if self.isr != 0 {
                        let bit = self.isr.trailing_zeros();
                        self.isr &= !(1u8 << bit);
                    }
                } else if eoi_cmd == 3 {
                    // Specific EOI
                    self.isr &= !(1u8 << (val & 7));
                }
            }
        } else {
            match self.icw_step {
                2 => {
                    self.vector_base = val & 0xF8;
                    if (self.icw1 & 0x02) != 0 {
                        self.icw_step = if (self.icw1 & 0x01) != 0 { 4 } else { 0 };
                    } else {
                        self.icw_step = 3;
                    }
                }
                3 => {
                    self.icw_step = if (self.icw1 & 0x01) != 0 { 4 } else { 0 };
                }
                4 => {
                    self.icw4 = val;
                    self.auto_eoi = (val & 0x02) != 0;
                    self.icw_step = 0;
                }
                _ => {
                    // OCW1 (IMR)
                    self.imr = val;
                }
            }
        }
    }
}
