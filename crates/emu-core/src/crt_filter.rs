use crate::types::{CANVAS_HEIGHT, CANVAS_WIDTH};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CrtColorMode {
    Rgb,
    GreenPhosphor,
    AmberPhosphor,
    Monochrome,
}

impl CrtColorMode {
    pub const fn name(self) -> &'static str {
        match self {
            CrtColorMode::Rgb => "RGB",
            CrtColorMode::GreenPhosphor => "Green P1",
            CrtColorMode::AmberPhosphor => "Amber P3",
            CrtColorMode::Monochrome => "B&W P4",
        }
    }

    pub fn next(self) -> Self {
        match self {
            CrtColorMode::Rgb => CrtColorMode::GreenPhosphor,
            CrtColorMode::GreenPhosphor => CrtColorMode::AmberPhosphor,
            CrtColorMode::AmberPhosphor => CrtColorMode::Monochrome,
            CrtColorMode::Monochrome => CrtColorMode::Rgb,
        }
    }
}

pub struct CrtFilter {
    pub color_mode: CrtColorMode,
    pub scanlines_enabled: bool,
    pub curvature_enabled: bool,
    pub phosphor_glow_enabled: bool,
    prev_frame: Vec<u32>,
}

impl Default for CrtFilter {
    fn default() -> Self {
        Self::new()
    }
}

impl CrtFilter {
    pub fn new() -> Self {
        Self {
            color_mode: CrtColorMode::Rgb,
            scanlines_enabled: false,
            curvature_enabled: false,
            phosphor_glow_enabled: false,
            prev_frame: vec![0xFF00_0000; CANVAS_WIDTH * CANVAS_HEIGHT],
        }
    }

    pub fn cycle_color_mode(&mut self) {
        self.color_mode = self.color_mode.next();
    }

    pub fn toggle_crt_effects(&mut self) {
        self.scanlines_enabled = !self.scanlines_enabled;
        self.curvature_enabled = self.scanlines_enabled;
        self.phosphor_glow_enabled = self.scanlines_enabled;
    }

    pub fn is_active(&self) -> bool {
        self.scanlines_enabled
            || self.curvature_enabled
            || self.phosphor_glow_enabled
            || self.color_mode != CrtColorMode::Rgb
    }

    pub fn process(&mut self, src: &[u32], dst: &mut [u32], width: usize, height: usize) {
        let len = (width * height).min(src.len()).min(dst.len());
        if self.prev_frame.len() != len {
            self.prev_frame.resize(len, 0xFF00_0000);
        }

        let half_w = width as f32 * 0.5;
        let half_h = height as f32 * 0.5;
        let curvature_strength = 0.04f32;

        for y in 0..height {
            let scanline_factor = if self.scanlines_enabled && (y & 1) == 1 {
                0.70f32
            } else {
                1.0f32
            };
            let ny = (y as f32 - half_h) / half_h;
            for x in 0..width {
                let mut sx = x;
                let mut sy = y;

                if self.curvature_enabled {
                    let nx = (x as f32 - half_w) / half_w;
                    let r2 = nx * nx + ny * ny;
                    let distort = 1.0 + curvature_strength * r2;
                    let dx = ((nx * distort) * half_w + half_w) as i32;
                    let dy = ((ny * distort) * half_h + half_h) as i32;
                    if dx < 0 || dx >= width as i32 || dy < 0 || dy >= height as i32 {
                        dst[y * width + x] = 0xFF06_0608;
                        continue;
                    }
                    sx = dx as usize;
                    sy = dy as usize;
                }

                let pixel = src[sy * width + sx];
                let mut r = ((pixel >> 16) & 0xFF) as f32;
                let mut g = ((pixel >> 8) & 0xFF) as f32;
                let mut b = (pixel & 0xFF) as f32;

                match self.color_mode {
                    CrtColorMode::Rgb => {}
                    CrtColorMode::GreenPhosphor => {
                        let luma = 0.299 * r + 0.587 * g + 0.114 * b;
                        r = luma * 0.12;
                        g = (luma * 1.05).min(255.0);
                        b = luma * 0.22;
                    }
                    CrtColorMode::AmberPhosphor => {
                        let luma = 0.299 * r + 0.587 * g + 0.114 * b;
                        r = (luma * 1.08).min(255.0);
                        g = luma * 0.68;
                        b = luma * 0.08;
                    }
                    CrtColorMode::Monochrome => {
                        let luma = 0.299 * r + 0.587 * g + 0.114 * b;
                        r = luma;
                        g = luma;
                        b = luma;
                    }
                }

                r *= scanline_factor;
                g *= scanline_factor;
                b *= scanline_factor;

                let idx = y * width + x;
                if self.phosphor_glow_enabled {
                    let prev = self.prev_frame[idx];
                    let pr = ((prev >> 16) & 0xFF) as f32;
                    let pg = ((prev >> 8) & 0xFF) as f32;
                    let pb = (prev & 0xFF) as f32;
                    r = r * 0.85 + pr * 0.15;
                    g = g * 0.85 + pg * 0.15;
                    b = b * 0.85 + pb * 0.15;
                }

                let out = 0xFF00_0000
                    | ((r.clamp(0.0, 255.0) as u32) << 16)
                    | ((g.clamp(0.0, 255.0) as u32) << 8)
                    | (b.clamp(0.0, 255.0) as u32);
                dst[idx] = out;
                self.prev_frame[idx] = out;
            }
        }
    }
}
