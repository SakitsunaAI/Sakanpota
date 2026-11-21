use emu_core::gui_font::glyph_row;

/// Mullard SAA5050 Teletext Character Generator (BBC Micro Mode 7).
#[derive(Debug, Clone)]
pub struct Saa5050 {
    pub flash_frame: u32,
}

impl Default for Saa5050 {
    fn default() -> Self {
        Self::new()
    }
}

impl Saa5050 {
    const TELETEXT_COLORS: [u32; 8] = [
        0xFF00_0000, // 0 Black
        0xFFFF_0000, // 1 Red
        0xFF00_FF00, // 2 Green
        0xFFFF_FF00, // 3 Yellow
        0xFF00_00FF, // 4 Blue
        0xFFFF_00FF, // 5 Magenta
        0xFF00_FFFF, // 6 Cyan
        0xFFFF_FFFF, // 7 White
    ];

    pub fn new() -> Self {
        Self { flash_frame: 0 }
    }

    pub fn tick_frame(&mut self) {
        self.flash_frame = self.flash_frame.wrapping_add(1);
    }

    /// Render a 40x25 Teletext page (`vram` = 1000 bytes, e.g. BBC `0x7C00..0x7FE8`)
    /// into a `640x512` ARGB8888 canvas (480x500 centered, 12x20 pixels per cell).
    pub fn render_screen(&self, vram: &[u8], out: &mut [u32], width: usize, height: usize, cursor_offset: Option<usize>) {
        out.fill(0xFF00_0000);
        let cell_w = 12usize;
        let cell_h = 20usize;
        let x_off = (width.saturating_sub(40 * cell_w)) / 2;
        let y_off = (height.saturating_sub(25 * cell_h)) / 2;

        let flash_visible = ((self.flash_frame / 24) & 1) == 0;
        let mut prev_row_dbl = false;

        for row in 0..25usize {
            let mut fg = 7usize;
            let mut bg = 0usize;
            let mut graphics_mode = false;
            let mut separated = false;
            let mut hold_graphics = false;
            let mut held_char: u8 = b' ';
            let mut dbl_height = false;
            let mut flash = false;
            let mut conceal = false;

            let use_bottom_half = prev_row_dbl;
            let mut row_has_dbl = false;

            for col in 0..40usize {
                let addr = row * 40 + col;
                let code = vram.get(addr).copied().unwrap_or(b' ') & 0x7F;
                let mut is_ctrl = false;

                if code < 0x20 {
                    is_ctrl = true;
                    match code {
                        0x01..=0x07 => {
                            fg = code as usize;
                            graphics_mode = false;
                            conceal = false;
                            held_char = b' ';
                        }
                        0x08 => flash = true,
                        0x09 => flash = false,
                        0x0C => dbl_height = false,
                        0x0D => {
                            dbl_height = true;
                            row_has_dbl = true;
                        }
                        0x11..=0x17 => {
                            fg = (code - 0x10) as usize;
                            graphics_mode = true;
                            conceal = false;
                        }
                        0x18 => conceal = true,
                        0x19 => separated = false,
                        0x1A => separated = true,
                        0x1C => bg = 0,
                        0x1D => bg = fg,
                        0x1E => hold_graphics = true,
                        0x1F => {
                            hold_graphics = false;
                            held_char = b' ';
                        }
                        _ => {}
                    }
                } else if graphics_mode && ((0x20..=0x3F).contains(&code) || (0x60..=0x7F).contains(&code)) {
                    held_char = code;
                }

                let draw_code = if is_ctrl {
                    if hold_graphics && graphics_mode {
                        held_char
                    } else {
                        b' '
                    }
                } else {
                    code
                };

                let fg_rgb = Self::TELETEXT_COLORS[fg & 7];
                let bg_rgb = Self::TELETEXT_COLORS[bg & 7];
                let hidden = conceal || (flash && !flash_visible);
                let is_cursor = cursor_offset == Some(addr) && ((self.flash_frame / 16) & 1) == 0;

                let base_x = x_off + col * cell_w;
                let base_y = y_off + row * cell_h;

                for py in 0..cell_h {
                    let sy = base_y + py;
                    if sy >= height {
                        continue;
                    }
                    // When on bottom half of double-height row without double-height re-set, draw blank
                    let src_py = if use_bottom_half {
                        if dbl_height {
                            (cell_h + py) / 2
                        } else {
                            continue;
                        }
                    } else if dbl_height {
                        py / 2
                    } else {
                        py
                    };

                    for px in 0..cell_w {
                        let sx = base_x + px;
                        if sx >= width {
                            continue;
                        }
                        let mut pixel_on = false;
                        if !hidden {
                            if graphics_mode && ((0x20..=0x3F).contains(&draw_code) || (0x60..=0x7F).contains(&draw_code)) {
                                // 2x3 sixel mosaic
                                let sixel_x = (px * 2) / cell_w; // 0..1
                                let sixel_y = (src_py * 3) / cell_h; // 0..2
                                let bit_idx = match (sixel_x, sixel_y) {
                                    (0, 0) => 0,
                                    (1, 0) => 1,
                                    (0, 1) => 2,
                                    (1, 1) => 3,
                                    (0, 2) => 4,
                                    _ => 6,
                                };
                                let bit_set = (draw_code & (1 << bit_idx)) != 0;
                                if separated {
                                    let mx = px % (cell_w / 2);
                                    let my = src_py % (cell_h / 3);
                                    pixel_on = bit_set && mx > 0 && my > 0;
                                } else {
                                    pixel_on = bit_set;
                                }
                            } else if draw_code >= 0x20 {
                                // SAA5050 character generator with UK Teletext symbols and diagonal rounding.
                                let teletext_row = |code: u8, row: usize| -> u8 {
                                    const POUND: [u8; 8] = [0x3C, 0x66, 0x60, 0xF8, 0x60, 0x62, 0xFC, 0x00];
                                    const LEFT_ARROW: [u8; 8] = [0x00, 0x10, 0x30, 0x7E, 0x30, 0x10, 0x00, 0x00];
                                    const HALF: [u8; 8] = [0x40, 0xC2, 0x44, 0xE8, 0x16, 0x29, 0x02, 0x0F];
                                    const RIGHT_ARROW: [u8; 8] = [0x00, 0x08, 0x0C, 0x7E, 0x0C, 0x08, 0x00, 0x00];
                                    const UP_ARROW: [u8; 8] = [0x00, 0x18, 0x3C, 0x7E, 0x18, 0x18, 0x18, 0x00];
                                    const EM_DASH: [u8; 8] = [0x00, 0x00, 0x00, 0x7E, 0x7E, 0x00, 0x00, 0x00];
                                    const QUARTER: [u8; 8] = [0x40, 0xC2, 0x44, 0xE8, 0x12, 0x26, 0x0F, 0x02];
                                    const DOUBLE_PIPE: [u8; 8] = [0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x00];
                                    const THREE_QUARTERS: [u8; 8] = [0xC0, 0x42, 0x84, 0xC8, 0x12, 0x26, 0x0F, 0x02];
                                    const DIVIDE: [u8; 8] = [0x00, 0x18, 0x18, 0x00, 0x7E, 0x00, 0x18, 0x18];
                                    let r = row.min(7);
                                    match code {
                                        0x23 => POUND[r],
                                        0x5B => LEFT_ARROW[r],
                                        0x5C => HALF[r],
                                        0x5D => RIGHT_ARROW[r],
                                        0x5E => UP_ARROW[r],
                                        0x5F => glyph_row('#', r),
                                        0x60 => EM_DASH[r],
                                        0x7B => QUARTER[r],
                                        0x7C => DOUBLE_PIPE[r],
                                        0x7D => THREE_QUARTERS[r],
                                        0x7E => DIVIDE[r],
                                        0x7F => 0xFF,
                                        c => glyph_row(c as char, r),
                                    }
                                };
                                let sample = |c: usize, r: usize| -> bool {
                                    (teletext_row(draw_code, r) & (0x80 >> c)) != 0
                                };
                                let grow = ((src_py * 8) / cell_h).min(7);
                                let gcol = ((px * 8) / cell_w).min(7);
                                pixel_on = sample(gcol, grow);
                                if !pixel_on && cell_w >= 8 && cell_h >= 8 {
                                    let right_half = (px * 2 + 1) * 8 >= (gcol * 2 + 1) * cell_w;
                                    let bottom_half = (src_py * 2 + 1) * 8 >= (grow * 2 + 1) * cell_h;
                                    let nx = if right_half { (gcol + 1 < 8).then_some(gcol + 1) } else { gcol.checked_sub(1) };
                                    let ny = if bottom_half { (grow + 1 < 8).then_some(grow + 1) } else { grow.checked_sub(1) };
                                    if let (Some(cx), Some(cy)) = (nx, ny) {
                                        pixel_on = sample(cx, grow) && sample(gcol, cy) && !sample(cx, cy);
                                    }
                                }
                            }
                        }

                        if is_cursor && py >= cell_h - 3 {
                            pixel_on = !pixel_on;
                        }
                        out[sy * width + sx] = if pixel_on { fg_rgb } else { bg_rgb };
                    }
                }
            }

            prev_row_dbl = row_has_dbl && !use_bottom_half;
        }
    }
}
