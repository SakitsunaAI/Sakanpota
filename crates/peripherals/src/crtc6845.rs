/// Motorola MC6845 / Hitachi HD6845 CRT Controller (CRTC).
/// Used in BBC Micro, Amstrad CPC 464, and Camputers Lynx.
#[derive(Debug, Clone)]
pub struct Crtc6845 {
    pub reg_sel: u8,
    pub regs: [u8; 18],
    pub h_cnt: u8,
    pub v_cnt: u8,
    pub row_cnt: u8,
    pub cur_addr: u16,
    pub row_start_addr: u16,
    pub in_hsync: bool,
    pub in_vsync: bool,
    pub hsync_cnt: u8,
    pub vsync_cnt: u8,
    pub disp_en: bool,
}

impl Default for Crtc6845 {
    fn default() -> Self {
        Self::new()
    }
}

impl Crtc6845 {
    pub fn new() -> Self {
        let mut c = Self {
            reg_sel: 0,
            regs: [0; 18],
            h_cnt: 0,
            v_cnt: 0,
            row_cnt: 0,
            cur_addr: 0,
            row_start_addr: 0,
            in_hsync: false,
            in_vsync: false,
            hsync_cnt: 0,
            vsync_cnt: 0,
            disp_en: true,
        };
        c.reset();
        c
    }

    pub fn reset(&mut self) {
        self.reg_sel = 0;
        // Standard PAL 40-col defaults until programmed by MOS/firmware
        self.regs = [
            63, 40, 49, 0x24, 38, 0, 32, 34, 0, 7, 0x67, 8, 0x08, 0x00, 0x08, 0x00, 0, 0,
        ];
        self.h_cnt = 0;
        self.v_cnt = 0;
        self.row_cnt = 0;
        self.cur_addr = 0x0800;
        self.row_start_addr = 0x0800;
        self.in_hsync = false;
        self.in_vsync = false;
        self.hsync_cnt = 0;
        self.vsync_cnt = 0;
        self.disp_en = true;
    }

    #[inline]
    pub fn start_addr(&self) -> u16 {
        (((self.regs[12] & 0x3F) as u16) << 8) | (self.regs[13] as u16)
    }

    #[inline]
    pub fn cursor_addr(&self) -> u16 {
        (((self.regs[14] & 0x3F) as u16) << 8) | (self.regs[15] as u16)
    }

    #[inline]
    pub fn h_displayed(&self) -> u8 {
        self.regs[1]
    }

    #[inline]
    pub fn v_displayed(&self) -> u8 {
        self.regs[6]
    }

    #[inline]
    pub fn max_scanline(&self) -> u8 {
        (self.regs[9] & 0x1F).saturating_add(1)
    }

    pub fn write(&mut self, addr_rs: bool, val: u8) {
        if !addr_rs {
            self.reg_sel = val & 0x1F;
        } else if (self.reg_sel as usize) < 18 {
            self.regs[self.reg_sel as usize] = val;
        }
    }

    pub fn read(&self, addr_rs: bool) -> u8 {
        if !addr_rs {
            0xFF
        } else if (12..18).contains(&self.reg_sel) {
            self.regs[self.reg_sel as usize]
        } else {
            0x00
        }
    }

    /// Advance by one character clock. Returns `(hsync_ended, vsync_started)`.
    pub fn tick_char(&mut self) -> (bool, bool) {
        let mut vsync_started = false;
        let mut hsync_ended = false;

        self.h_cnt = self.h_cnt.wrapping_add(1);
        self.cur_addr = self.cur_addr.wrapping_add(1) & 0x3FFF;

        if self.h_cnt == self.regs[2] {
            self.in_hsync = true;
            self.hsync_cnt = 0;
        }
        if self.in_hsync {
            self.hsync_cnt = self.hsync_cnt.wrapping_add(1);
            let h_width = (self.regs[3] & 0x0F).max(1);
            if self.hsync_cnt >= h_width {
                self.in_hsync = false;
                hsync_ended = true;
            }
        }

        if self.h_cnt > self.regs[0] {
            self.h_cnt = 0;
            let max_sl = self.regs[9] & 0x1F;
            if self.row_cnt >= max_sl {
                self.row_cnt = 0;
                self.row_start_addr =
                    self.row_start_addr.wrapping_add(self.regs[1] as u16) & 0x3FFF;
                self.v_cnt = self.v_cnt.wrapping_add(1);
                if self.v_cnt == self.regs[7] {
                    self.in_vsync = true;
                    self.vsync_cnt = 0;
                    vsync_started = true;
                }
                let v_total_rows = (self.regs[4] & 0x7F).saturating_add(1);
                if self.v_cnt >= v_total_rows {
                    self.v_cnt = 0;
                    self.row_start_addr = self.start_addr();
                }
            } else {
                self.row_cnt = self.row_cnt.wrapping_add(1);
            }

            if self.in_vsync {
                self.vsync_cnt = self.vsync_cnt.wrapping_add(1);
                let vw = ((self.regs[3] >> 4) & 0x0F).max(1);
                if self.vsync_cnt >= vw {
                    self.in_vsync = false;
                }
            }
            self.cur_addr = self.row_start_addr;
        }

        self.disp_en = self.h_cnt < self.regs[1] && self.v_cnt < self.regs[6];
        (hsync_ended, vsync_started)
    }
}
