/// MOS 6569 VIC-II PAL Video Interface Chip (Commodore 64).
#[derive(Debug, Clone)]
pub struct Vic6569 {
    pub regs: [u8; 64],
    pub raster_y: u16,
    pub cycle_in_line: u16,
    pub irq_status: u8,
    pub irq_enable: u8,
    pub sprite_sprite_coll: u8,
    pub sprite_bg_coll: u8,
}

impl Default for Vic6569 {
    fn default() -> Self {
        Self::new()
    }
}

impl Vic6569 {
    /// Authentic C64 Pepto PAL 16-color palette (ARGB8888).
    pub const PALETTE: [u32; 16] = [
        0xFF00_0000, // 0 Black
        0xFFFF_FFFF, // 1 White
        0xFF81_3338, // 2 Red
        0xFF75_CEC8, // 3 Cyan
        0xFF8E_3C97, // 4 Purple
        0xFF56_AC4D, // 5 Green
        0xFF2E_2C9B, // 6 Blue
        0xFFED_F171, // 7 Yellow
        0xFF8E_5029, // 8 Orange
        0xFF55_3800, // 9 Brown
        0xFFC4_6C71, // 10 Light Red
        0xFF4A_4A4A, // 11 Dark Grey
        0xFF7B_7B7B, // 12 Grey
        0xFFA9_FF9F, // 13 Light Green
        0xFF70_6DEB, // 14 Light Blue
        0xFFB2_B2B2, // 15 Light Grey
    ];

    pub const CYCLES_PER_LINE: u16 = 63;
    pub const LINES_PER_FRAME: u16 = 312;

    pub fn new() -> Self {
        let mut v = Self {
            regs: [0; 64],
            raster_y: 0,
            cycle_in_line: 0,
            irq_status: 0,
            irq_enable: 0,
            sprite_sprite_coll: 0,
            sprite_bg_coll: 0,
        };
        v.reset();
        v
    }

    pub fn reset(&mut self) {
        self.regs = [0; 64];
        self.regs[0x11] = 0x1B; // DEN=1, RSEL=1, YSCROLL=3
        self.regs[0x16] = 0x08; // CSEL=1
        self.regs[0x18] = 0x15; // Screen $0400, Char $1000
        self.regs[0x20] = 14;   // Light blue border
        self.regs[0x21] = 6;    // Blue background
        self.raster_y = 0;
        self.cycle_in_line = 0;
        self.irq_status = 0;
        self.irq_enable = 0;
        self.sprite_sprite_coll = 0;
        self.sprite_bg_coll = 0;
    }

    #[inline]
    pub fn irq_active(&self) -> bool {
        (self.irq_status & self.irq_enable & 0x0F) != 0
    }

    #[inline]
    pub fn raster_compare(&self) -> u16 {
        (((self.regs[0x11] as u16) & 0x80) << 1) | (self.regs[0x12] as u16)
    }

    /// Advance VIC-II raster timing by `cpu_cycles`. Returns `true` when entering VBlank (frame end).
    pub fn tick(&mut self, cpu_cycles: u32) -> bool {
        let mut frame_done = false;
        self.cycle_in_line += cpu_cycles as u16;
        while self.cycle_in_line >= Self::CYCLES_PER_LINE {
            self.cycle_in_line -= Self::CYCLES_PER_LINE;
            self.raster_y += 1;
            if self.raster_y >= Self::LINES_PER_FRAME {
                self.raster_y = 0;
                frame_done = true;
            }
            if self.raster_y == self.raster_compare() {
                self.irq_status |= 0x01;
            }
        }
        frame_done
    }

    pub fn read(&mut self, reg: u8) -> u8 {
        let r = (reg & 0x3F) as usize;
        match r {
            0x11 => (self.regs[0x11] & 0x7F) | if (self.raster_y & 0x100) != 0 { 0x80 } else { 0 },
            0x12 => (self.raster_y & 0xFF) as u8,
            0x19 => {
                let irq = if self.irq_active() { 0x80 } else { 0 };
                (self.irq_status & 0x0F) | irq | 0x70
            }
            0x1A => self.irq_enable | 0xF0,
            0x1E => {
                let v = self.sprite_sprite_coll;
                self.sprite_sprite_coll = 0;
                v
            }
            0x1F => {
                let v = self.sprite_bg_coll;
                self.sprite_bg_coll = 0;
                v
            }
            0x20..=0x2E => self.regs[r] | 0xF0,
            0x2F..=0x3F => 0xFF,
            _ => self.regs[r],
        }
    }

    pub fn write(&mut self, reg: u8, val: u8) {
        let r = (reg & 0x3F) as usize;
        match r {
            0x19 => {
                self.irq_status &= !(val & 0x0F);
            }
            0x1A => {
                self.irq_enable = val & 0x0F;
            }
            0x1E | 0x1F => {}
            _ => {
                self.regs[r] = val;
            }
        }
    }

    /// Render a full 320x200 frame (plus border into 384x272 or upscaled directly to `width x height`).
    pub fn render_screen<F>(&mut self, out: &mut [u32], width: usize, height: usize, color_ram: &[u8; 1024], vram_read: F)
    where
        F: Fn(u16) -> u8,
    {
        let border_col = Self::PALETTE[(self.regs[0x20] & 0x0F) as usize];
        out.fill(border_col);

        if (self.regs[0x11] & 0x10) == 0 {
            return;
        }

        let bmm = (self.regs[0x11] & 0x20) != 0;
        let ecm = (self.regs[0x11] & 0x40) != 0;
        let mcm = (self.regs[0x16] & 0x10) != 0;
        let rsel_24 = (self.regs[0x11] & 0x08) == 0;
        let csel_38 = (self.regs[0x16] & 0x08) == 0;
        let xscroll = (self.regs[0x16] & 0x07) as i32;
        let yscroll = ((self.regs[0x11] & 0x07) as i32) - 3;

        let screen_base = ((self.regs[0x18] & 0xF0) as u16) << 6;
        let char_base = ((self.regs[0x18] & 0x0E) as u16) << 10;
        let bitmap_base = ((self.regs[0x18] & 0x08) as u16) << 10;

        let mut active = [border_col; 320 * 200];
        let mut fg_mask = [false; 320 * 200];

        let bg0 = Self::PALETTE[(self.regs[0x21] & 0x0F) as usize];
        let bg1 = Self::PALETTE[(self.regs[0x22] & 0x0F) as usize];
        let bg2 = Self::PALETTE[(self.regs[0x23] & 0x0F) as usize];

        for row in 0..25usize {
            for col in 0..40usize {
                let cell = row * 40 + col;
                let sbyte = vram_read(screen_base + cell as u16);
                let cbyte = color_ram[cell] & 0x0F;

                for cy in 0..8usize {
                    let y = (row * 8 + cy) as i32 + yscroll;
                    if !(0..200).contains(&y) {
                        continue;
                    }
                    if rsel_24 && !(4..196).contains(&y) {
                        continue;
                    }
                    let yu = y as usize;

                    if bmm && mcm {
                        // Multicolor Bitmap (160x200 doubled)
                        let pbyte = vram_read(bitmap_base + (cell as u16) * 8 + cy as u16);
                        let c0 = bg0;
                        let c1 = Self::PALETTE[(sbyte >> 4) as usize];
                        let c2 = Self::PALETTE[(sbyte & 0x0F) as usize];
                        let c3 = Self::PALETTE[cbyte as usize];
                        for pair in 0..4usize {
                            let idx = (pbyte >> (6 - pair * 2)) & 3;
                            let (col_rgb, is_fg) = match idx {
                                0 => (c0, false),
                                1 => (c1, false),
                                2 => (c2, true),
                                _ => (c3, true),
                            };
                            for sub in 0..2usize {
                                let x = (col * 8 + pair * 2 + sub) as i32 + xscroll;
                                if (0..320).contains(&x) && (!csel_38 || (7..311).contains(&x)) {
                                    let p = yu * 320 + x as usize;
                                    active[p] = col_rgb;
                                    fg_mask[p] = is_fg;
                                }
                            }
                        }
                    } else if bmm {
                        // Standard Hi-Res Bitmap
                        let pbyte = vram_read(bitmap_base + (cell as u16) * 8 + cy as u16);
                        let fg = Self::PALETTE[(sbyte >> 4) as usize];
                        let bg = Self::PALETTE[(sbyte & 0x0F) as usize];
                        for cx in 0..8usize {
                            let x = (col * 8 + cx) as i32 + xscroll;
                            if (0..320).contains(&x) && (!csel_38 || (7..311).contains(&x)) {
                                let set = (pbyte & (0x80 >> cx)) != 0;
                                let p = yu * 320 + x as usize;
                                active[p] = if set { fg } else { bg };
                                fg_mask[p] = set;
                            }
                        }
                    } else if ecm {
                        // Extended Background Color Text
                        let ch = (sbyte & 0x3F) as u16;
                        let bg_sel = ((sbyte >> 6) & 3) as usize;
                        let bg = Self::PALETTE[(self.regs[0x21 + bg_sel] & 0x0F) as usize];
                        let fg = Self::PALETTE[cbyte as usize];
                        let gbyte = vram_read(char_base + ch * 8 + cy as u16);
                        for cx in 0..8usize {
                            let x = (col * 8 + cx) as i32 + xscroll;
                            if (0..320).contains(&x) && (!csel_38 || (7..311).contains(&x)) {
                                let set = (gbyte & (0x80 >> cx)) != 0;
                                let p = yu * 320 + x as usize;
                                active[p] = if set { fg } else { bg };
                                fg_mask[p] = set;
                            }
                        }
                    } else if mcm && (cbyte & 0x08) != 0 {
                        // Multicolor Text
                        let gbyte = vram_read(char_base + (sbyte as u16) * 8 + cy as u16);
                        let fg3 = Self::PALETTE[(cbyte & 0x07) as usize];
                        for pair in 0..4usize {
                            let idx = (gbyte >> (6 - pair * 2)) & 3;
                            let (col_rgb, is_fg) = match idx {
                                0 => (bg0, false),
                                1 => (bg1, false),
                                2 => (bg2, true),
                                _ => (fg3, true),
                            };
                            for sub in 0..2usize {
                                let x = (col * 8 + pair * 2 + sub) as i32 + xscroll;
                                if (0..320).contains(&x) && (!csel_38 || (7..311).contains(&x)) {
                                    let p = yu * 320 + x as usize;
                                    active[p] = col_rgb;
                                    fg_mask[p] = is_fg;
                                }
                            }
                        }
                    } else {
                        // Standard Text
                        let gbyte = vram_read(char_base + (sbyte as u16) * 8 + cy as u16);
                        let fg = Self::PALETTE[cbyte as usize];
                        for cx in 0..8usize {
                            let x = (col * 8 + cx) as i32 + xscroll;
                            if (0..320).contains(&x) && (!csel_38 || (7..311).contains(&x)) {
                                let set = (gbyte & (0x80 >> cx)) != 0;
                                let p = yu * 320 + x as usize;
                                active[p] = if set { fg } else { bg0 };
                                fg_mask[p] = set;
                            }
                        }
                    }
                }
            }
        }

        // Hardware sprites (0..7)
        let sp_en = self.regs[0x15];
        if sp_en != 0 {
            let mut spr_owner = [0u8; 320 * 200];
            let sp_yexp = self.regs[0x17];
            let sp_xmsb = self.regs[0x10];
            let sp_prio = self.regs[0x1B];
            let sp_mc = self.regs[0x1C];
            let sp_xexp = self.regs[0x1D];
            let mc0 = Self::PALETTE[(self.regs[0x25] & 0x0F) as usize];
            let mc1 = Self::PALETTE[(self.regs[0x26] & 0x0F) as usize];

            for s in (0..8usize).rev() {
                if (sp_en & (1 << s)) == 0 {
                    continue;
                }
                let ptr = vram_read(screen_base + 0x03F8 + s as u16) as u16;
                let data_base = ptr * 64;
                let sx_raw = (self.regs[s * 2] as i32) | if (sp_xmsb & (1 << s)) != 0 { 256 } else { 0 };
                let sy_raw = self.regs[s * 2 + 1] as i32;
                let sx0 = sx_raw - 24;
                let sy0 = sy_raw - 50;
                let y_exp = (sp_yexp & (1 << s)) != 0;
                let x_exp = (sp_xexp & (1 << s)) != 0;
                let is_mc = (sp_mc & (1 << s)) != 0;
                let behind_bg = (sp_prio & (1 << s)) != 0;
                let sp_col = Self::PALETTE[(self.regs[0x27 + s] & 0x0F) as usize];
                let h = if y_exp { 42 } else { 21 };
                let w = if x_exp { 48 } else { 24 };

                for dy in 0..h {
                    let py = (if y_exp { dy / 2 } else { dy }) as u16;
                    let screen_y = sy0 + dy;
                    if !(0..200).contains(&screen_y) {
                        continue;
                    }
                    let syu = screen_y as usize;
                    for dx in 0..w {
                        let px = (if x_exp { dx / 2 } else { dx }) as usize;
                        let screen_x = sx0 + dx;
                        if !(0..320).contains(&screen_x) {
                            continue;
                        }
                        let byte_val = vram_read(data_base + py * 3 + (px / 8) as u16);
                        let bit_in_byte = 7 - (px & 7);
                        let drawn_col = if is_mc {
                            let pair = (byte_val >> (bit_in_byte & !1)) & 3;
                            match pair {
                                0 => continue,
                                1 => mc0,
                                2 => sp_col,
                                _ => mc1,
                            }
                        } else {
                            if (byte_val & (1 << bit_in_byte)) == 0 {
                                continue;
                            }
                            sp_col
                        };
                        let pidx = syu * 320 + screen_x as usize;
                        let bit = 1u8 << s;
                        if spr_owner[pidx] != 0 {
                            self.sprite_sprite_coll |= bit | spr_owner[pidx];
                        }
                        spr_owner[pidx] |= bit;
                        if fg_mask[pidx] {
                            self.sprite_bg_coll |= bit;
                        }
                        if !behind_bg || !fg_mask[pidx] {
                            active[pidx] = drawn_col;
                        }
                    }
                }
            }
        }

        // Upscale 320x200 into `width x height` (e.g. 640x512 with clean border around 640x400)
        let sx_scale = (width / 320).max(1);
        let sy_scale = (height / 200).max(1);
        let x_pad = (width.saturating_sub(320 * sx_scale)) / 2;
        let y_pad = (height.saturating_sub(200 * sy_scale)) / 2;

        for y in 0..200usize {
            for x in 0..320usize {
                let c = active[y * 320 + x];
                for dy in 0..sy_scale {
                    let oy = y_pad + y * sy_scale + dy;
                    if oy >= height {
                        continue;
                    }
                    let row_off = oy * width + x_pad + x * sx_scale;
                    for dx in 0..sx_scale {
                        if x_pad + x * sx_scale + dx < width {
                            out[row_off + dx] = c;
                        }
                    }
                }
            }
        }
    }
}
