/// Texas Instruments TMS9918A / TMS9928A Video Display Processor (VDP).
/// Used in TI-99/4A (16KB private VRAM, 256x192 resolution, 16-color palette, 32 sprites).
#[derive(Debug, Clone)]
pub struct Tms9918 {
    pub vram: Vec<u8>,
    pub regs: [u8; 8],
    pub status: u8,
    pub vram_addr: u16,
    pub addr_latch: u8,
    pub first_write: bool,
    pub read_buffer: u8,
}

impl Default for Tms9918 {
    fn default() -> Self {
        Self::new()
    }
}

impl Tms9918 {
    pub const PALETTE: [u32; 16] = [
        0xFF00_0000, // 0 Transparent -> Backdrop
        0xFF00_0000, // 1 Black
        0xFF21_C842, // 2 Medium Green
        0xFF5E_DC78, // 3 Light Green
        0xFF54_55ED, // 4 Dark Blue
        0xFF7D_76FC, // 5 Light Blue
        0xFFD4_524D, // 6 Dark Red
        0xFF42_EBF5, // 7 Cyan
        0xFFFC_5554, // 8 Medium Red
        0xFFFF_7978, // 9 Light Red
        0xFFD4_C154, // 10 Dark Yellow
        0xFFE6_CE80, // 11 Light Yellow
        0xFF21_B03B, // 12 Dark Green
        0xFFC9_5BBA, // 13 Magenta
        0xFFCC_CCCC, // 14 Gray
        0xFFFF_FFFF, // 15 White
    ];

    pub fn new() -> Self {
        let mut v = Self {
            vram: vec![0; 16_384],
            regs: [0; 8],
            status: 0,
            vram_addr: 0,
            addr_latch: 0,
            first_write: false,
            read_buffer: 0,
        };
        v.reset();
        v
    }

    pub fn reset(&mut self) {
        self.vram.fill(0);
        self.regs = [0; 8];
        self.status = 0;
        self.vram_addr = 0;
        self.addr_latch = 0;
        self.first_write = false;
        self.read_buffer = 0;
    }

    #[inline]
    pub fn irq_active(&self) -> bool {
        (self.status & 0x80) != 0 && (self.regs[1] & 0x20) != 0
    }

    pub fn read_data(&mut self) -> u8 {
        self.first_write = false;
        let out = self.read_buffer;
        self.read_buffer = self.vram[(self.vram_addr as usize) & 0x3FFF];
        self.vram_addr = (self.vram_addr.wrapping_add(1)) & 0x3FFF;
        out
    }

    pub fn write_data(&mut self, val: u8) {
        self.first_write = false;
        self.vram[(self.vram_addr as usize) & 0x3FFF] = val;
        self.read_buffer = val;
        self.vram_addr = (self.vram_addr.wrapping_add(1)) & 0x3FFF;
    }

    pub fn read_status(&mut self) -> u8 {
        self.first_write = false;
        let s = self.status;
        self.status &= 0x1F;
        s
    }

    pub fn write_ctrl(&mut self, val: u8) {
        if !self.first_write {
            self.addr_latch = val;
            self.first_write = true;
        } else {
            self.first_write = false;
            if (val & 0x80) != 0 {
                // Write VDP register
                let reg = (val & 0x07) as usize;
                self.regs[reg] = self.addr_latch;
            } else {
                // Set VRAM address (read or write)
                self.vram_addr = (((val & 0x3F) as u16) << 8) | (self.addr_latch as u16);
                if (val & 0x40) == 0 {
                    self.read_buffer = self.vram[(self.vram_addr as usize) & 0x3FFF];
                    self.vram_addr = (self.vram_addr.wrapping_add(1)) & 0x3FFF;
                }
            }
        }
    }

    pub fn begin_vblank(&mut self) {
        self.status |= 0x80;
    }

    /// Render the full 256x192 TMS9918A frame upscaled into `out` (`width x height` e.g. 640x512).
    pub fn render_screen(&mut self, out: &mut [u32], width: usize, height: usize) {
        let backdrop_idx = (self.regs[7] & 0x0F) as usize;
        let backdrop = Self::PALETTE[backdrop_idx.max(1)];
        out.fill(backdrop);

        let mut frame = vec![backdrop; 256 * 192];
        if (self.regs[1] & 0x40) != 0 {
            let m1 = (self.regs[1] & 0x10) != 0; // Text
            let m2 = (self.regs[1] & 0x08) != 0; // Multicolor
            let m3 = (self.regs[0] & 0x02) != 0; // Graphics II

            let name_base = ((self.regs[2] & 0x0F) as usize) * 0x400;
            let color_base = ((self.regs[3]) as usize) * 0x40;
            let pattern_base = ((self.regs[4] & 0x07) as usize) * 0x800;

            if m1 {
                // 40x24 Text Mode (6x8 cells, centered in 256px width with 8px margins)
                let fg_idx = ((self.regs[7] >> 4) & 0x0F) as usize;
                let fg_color = if fg_idx == 0 { backdrop } else { Self::PALETTE[fg_idx] };
                for row in 0..24usize {
                    for col in 0..40usize {
                        let ch = self.vram[(name_base + row * 40 + col) & 0x3FFF] as usize;
                        for cy in 0..8usize {
                            let bits = self.vram[(pattern_base + ch * 8 + cy) & 0x3FFF];
                            let y = row * 8 + cy;
                            for cx in 0..6usize {
                                if (bits & (0x80 >> cx)) != 0 {
                                    frame[y * 256 + 8 + col * 6 + cx] = fg_color;
                                }
                            }
                        }
                    }
                }
            } else if m2 {
                // Multicolor 64x48 blocks
                for row in 0..24usize {
                    for col in 0..32usize {
                        let ch = self.vram[(name_base + row * 32 + col) & 0x3FFF] as usize;
                        let block_y_base = (row & 3) * 2;
                        for by in 0..2usize {
                            let cbyte = self.vram[(pattern_base + ch * 8 + block_y_base + by) & 0x3FFF];
                            let left = (cbyte >> 4) as usize;
                            let right = (cbyte & 0x0F) as usize;
                            let lc = if left == 0 { backdrop } else { Self::PALETTE[left] };
                            let rc = if right == 0 { backdrop } else { Self::PALETTE[right] };
                            for suby in 0..4usize {
                                let y = row * 8 + by * 4 + suby;
                                for subx in 0..4usize {
                                    frame[y * 256 + col * 8 + subx] = lc;
                                    frame[y * 256 + col * 8 + 4 + subx] = rc;
                                }
                            }
                        }
                    }
                }
            } else if m3 {
                // Graphics II (bitmap 256x192)
                let pg_base = ((self.regs[4] & 0x04) as usize) * 0x800;
                let pg_mask = ((((self.regs[4] & 0x03) as usize) << 8) | 0xFF).max(0xFF);
                let ct_base = ((self.regs[3] & 0x80) as usize) * 0x40;
                let ct_mask = ((((self.regs[3] & 0x7F) as usize) << 3) | 0x07).max(0x07);
                for row in 0..24usize {
                    let third = row / 8;
                    for col in 0..32usize {
                        let ch = self.vram[(name_base + row * 32 + col) & 0x3FFF] as usize;
                        let idx = (third * 256) + ch;
                        for cy in 0..8usize {
                            let p_off = ((idx & pg_mask) * 8 + cy) + pg_base;
                            let c_off = ((idx & ct_mask) * 8 + cy) + ct_base;
                            let pbyte = self.vram[p_off & 0x3FFF];
                            let cbyte = self.vram[c_off & 0x3FFF];
                            let fg = (cbyte >> 4) as usize;
                            let bg = (cbyte & 0x0F) as usize;
                            let fg_c = if fg == 0 { backdrop } else { Self::PALETTE[fg] };
                            let bg_c = if bg == 0 { backdrop } else { Self::PALETTE[bg] };
                            let y = row * 8 + cy;
                            for cx in 0..8usize {
                                frame[y * 256 + col * 8 + cx] =
                                    if (pbyte & (0x80 >> cx)) != 0 { fg_c } else { bg_c };
                            }
                        }
                    }
                }
            } else {
                // Graphics I (32x24 tiles)
                for row in 0..24usize {
                    for col in 0..32usize {
                        let ch = self.vram[(name_base + row * 32 + col) & 0x3FFF] as usize;
                        let cbyte = self.vram[(color_base + (ch >> 3)) & 0x3FFF];
                        let fg = (cbyte >> 4) as usize;
                        let bg = (cbyte & 0x0F) as usize;
                        let fg_c = if fg == 0 { backdrop } else { Self::PALETTE[fg] };
                        let bg_c = if bg == 0 { backdrop } else { Self::PALETTE[bg] };
                        for cy in 0..8usize {
                            let pbyte = self.vram[(pattern_base + ch * 8 + cy) & 0x3FFF];
                            let y = row * 8 + cy;
                            for cx in 0..8usize {
                                frame[y * 256 + col * 8 + cx] =
                                    if (pbyte & (0x80 >> cx)) != 0 { fg_c } else { bg_c };
                            }
                        }
                    }
                }
            }

            // Sprites (disabled in Text mode)
            if !m1 {
                self.render_sprites(&mut frame);
            }
        }

        // Aspect-fit 256x192 -> centered 512x384 or scaled into width x height (640x512 -> 2x or fill)
        let scale_x = (width / 256).max(1);
        let scale_y = (height / 192).max(1);
        let scale = scale_x.min(scale_y);
        let dest_w = 256 * scale;
        let dest_h = 192 * scale;
        let x0 = (width - dest_w) / 2;
        let y0 = (height - dest_h) / 2;

        for sy in 0..192usize {
            for sx in 0..256usize {
                let c = frame[sy * 256 + sx];
                for dy in 0..scale {
                    let oy = y0 + sy * scale + dy;
                    let row_off = oy * width + x0 + sx * scale;
                    for dx in 0..scale {
                        out[row_off + dx] = c;
                    }
                }
            }
        }
    }

    fn render_sprites(&mut self, frame: &mut [u32]) {
        let sat_base = ((self.regs[5] & 0x7F) as usize) * 0x80;
        let spg_base = ((self.regs[6] & 0x07) as usize) * 0x800;
        let size16 = (self.regs[1] & 0x02) != 0;
        let mag = (self.regs[1] & 0x01) != 0;
        let sprite_dim = if size16 { 16 } else { 8 };
        let screen_dim = sprite_dim * if mag { 2 } else { 1 };

        let mut line_sprite_count = [0u8; 192];
        let mut occupied = vec![false; 256 * 192];

        for idx in 0..32usize {
            let attr = sat_base + idx * 4;
            let raw_y = self.vram[attr & 0x3FFF];
            if raw_y == 0xD0 {
                break;
            }
            let y_pos = if raw_y >= 224 {
                raw_y as i32 - 255
            } else {
                raw_y as i32 + 1
            };
            let raw_x = self.vram[(attr + 1) & 0x3FFF] as i32;
            let name = self.vram[(attr + 2) & 0x3FFF] as usize;
            let ec_color = self.vram[(attr + 3) & 0x3FFF];
            let x_pos = if (ec_color & 0x80) != 0 { raw_x - 32 } else { raw_x };
            let col_idx = (ec_color & 0x0F) as usize;
            let pat_name = if size16 { name & 0xFC } else { name };

            for dy in 0..screen_dim {
                let sy = y_pos + dy as i32;
                if !(0..192).contains(&sy) {
                    continue;
                }
                let syu = sy as usize;
                line_sprite_count[syu] = line_sprite_count[syu].saturating_add(1);
                if line_sprite_count[syu] > 4 {
                    if (self.status & 0x40) == 0 {
                        self.status = (self.status & 0xE0) | 0x40 | (idx as u8);
                    }
                    continue;
                }
                let py = if mag { dy / 2 } else { dy };
                for dx in 0..screen_dim {
                    let sx = x_pos + dx as i32;
                    if !(0..256).contains(&sx) {
                        continue;
                    }
                    let px = if mag { dx / 2 } else { dx };
                    let quad_off = if px >= 8 { 16 } else { 0 };
                    let pbyte = self.vram[(spg_base + pat_name * 8 + quad_off + py) & 0x3FFF];
                    if (pbyte & (0x80 >> (px & 7))) != 0 {
                        let pidx = syu * 256 + sx as usize;
                        if occupied[pidx] {
                            self.status |= 0x20; // Collision flag
                        } else {
                            occupied[pidx] = true;
                            if col_idx != 0 {
                                frame[pidx] = Self::PALETTE[col_idx];
                            }
                        }
                    }
                }
            }
        }
    }
}
