use std::collections::VecDeque;

/// Generic parsed disk geometry and sector store supporting `.ssd`, `.dsd`,
/// Amstrad `.dsk` (Standard + Extended), and raw PC floppy sector dumps.
#[derive(Debug, Clone)]
pub struct FloppyImage {
    pub name: String,
    pub tracks: usize,
    pub sides: usize,
    /// Sectors indexed by `[side][track]` -> list of `(c, h, r, n_code, data)`.
    pub sector_table: Vec<Vec<Vec<FloppySector>>>,
}

#[derive(Debug, Clone)]
pub struct FloppySector {
    pub c: u8,
    pub h: u8,
    pub r: u8,
    pub n: u8,
    pub data: Vec<u8>,
}

impl Default for FloppyImage {
    fn default() -> Self {
        Self::new_blank("blank.ssd", 80, 1, 10, 256)
    }
}

impl FloppyImage {
    pub fn new_blank(
        name: &str,
        tracks: usize,
        sides: usize,
        sectors_per_track: usize,
        sector_size: usize,
    ) -> Self {
        let n = match sector_size {
            128 => 0,
            256 => 1,
            512 => 2,
            1024 => 3,
            _ => 1,
        };
        let sector_table: Vec<Vec<Vec<FloppySector>>> = (0..sides)
            .map(|side| {
                (0..tracks)
                    .map(|track| {
                        (0..sectors_per_track)
                            .map(|sec_idx| FloppySector {
                                c: track as u8,
                                h: side as u8,
                                r: sec_idx as u8,
                                n,
                                data: vec![0; sector_size],
                            })
                            .collect()
                    })
                    .collect()
            })
            .collect();
        Self {
            name: name.to_string(),
            tracks,
            sides,
            sector_table,
        }
    }

    pub fn from_ssd_or_dsd(name: &str, data: &[u8], double_sided: bool) -> Self {
        let sides = if double_sided { 2 } else { 1 };
        let tracks = 80;
        let mut img = Self::new_blank(name, tracks, sides, 10, 256);
        let mut off = 0usize;
        for track in 0..tracks {
            for side in 0..sides {
                for sec in 0..10usize {
                    if off + 256 <= data.len() {
                        img.sector_table[side][track][sec].data.copy_from_slice(&data[off..off + 256]);
                    } else if off < data.len() {
                        let rem = data.len() - off;
                        img.sector_table[side][track][sec].data[..rem].copy_from_slice(&data[off..]);
                    }
                    off += 256;
                }
            }
        }
        img
    }

    pub fn has_dfs_boot_file(&self) -> bool {
        let Some(sec0) = self.find_sector(0, 0, 0) else {
            return false;
        };
        let Some(sec1) = self.find_sector(0, 0, 1) else {
            return false;
        };
        if sec0.data.len() < 256 || sec1.data.len() < 8 {
            return false;
        }
        let num_files = (((sec1.data[5] & 0xF8) >> 3) as usize).min(31);
        for i in 0..num_files {
            let entry_off = 8 + i * 8;
            let dir = sec0.data[entry_off + 7] & 0x7F;
            if dir != b'$' {
                continue;
            }
            let raw_name: String = sec0.data[entry_off..entry_off + 7]
                .iter()
                .map(|&b| (b & 0x7F) as char)
                .collect();
            if raw_name.trim_end().eq_ignore_ascii_case("!BOOT") {
                return true;
            }
        }
        false
    }

    pub fn ensure_dfs_boot_option(&mut self) {
        if !self.has_dfs_boot_file() {
            return;
        }
        if let Some(sec1) = self.find_sector_mut(0, 0, 1) {
            if sec1.data.len() >= 8 && (sec1.data[6] & 0x30) == 0 {
                sec1.data[6] |= 0x30; // *OPT 4,3 (*EXEC !BOOT)
            }
        }
    }

    pub fn from_cpc_dsk(name: &str, data: &[u8]) -> Option<Self> {
        if data.len() < 256 {
            return None;
        }
        let sig = &data[..8];
        let is_ext = sig == b"EXTENDED";
        let is_std = sig == b"MV - CPC";
        if !is_ext && !is_std {
            return None;
        }
        let tracks = (data[0x30] as usize).max(1);
        let sides = (data[0x31] as usize).clamp(1, 2);
        let std_track_size = u16::from_le_bytes([data[0x32], data[0x33]]) as usize;
        let mut sector_table = vec![vec![Vec::new(); tracks]; sides];

        let mut pos = 256usize;
        // File layout is track-major, side-minor, whereas `sector_table` is indexed `[side][track]`.
        #[allow(clippy::needless_range_loop)]
        for t in 0..tracks {
            for s in 0..sides {
                let track_size = if is_ext {
                    let sz_hi = data.get(0x34 + t * sides + s).copied().unwrap_or(0) as usize;
                    sz_hi * 256
                } else {
                    std_track_size
                };
                if track_size == 0 || pos + 24 > data.len() {
                    continue;
                }
                if &data[pos..pos + 10] != b"Track-Info" {
                    pos += track_size;
                    continue;
                }
                let n_code = data[pos + 0x14];
                let num_secs = data[pos + 0x15] as usize;
                let mut sec_data_off = pos + 256;
                let mut secs = Vec::with_capacity(num_secs);
                for i in 0..num_secs {
                    let info_off = pos + 0x18 + i * 8;
                    if info_off + 8 > data.len() {
                        break;
                    }
                    let c = data[info_off];
                    let h = data[info_off + 1];
                    let r = data[info_off + 2];
                    let n = data[info_off + 3];
                    let actual_len = if is_ext {
                        let l = u16::from_le_bytes([data[info_off + 6], data[info_off + 7]]) as usize;
                        if l == 0 { 128usize << (n.min(6)) } else { l }
                    } else {
                        128usize << (n_code.min(6))
                    };
                    let mut buf = vec![0u8; actual_len];
                    if sec_data_off < data.len() {
                        let copy_len = actual_len.min(data.len() - sec_data_off);
                        buf[..copy_len].copy_from_slice(&data[sec_data_off..sec_data_off + copy_len]);
                    }
                    sec_data_off += actual_len;
                    secs.push(FloppySector { c, h, r, n, data: buf });
                }
                sector_table[s][t] = secs;
                pos += track_size;
            }
        }

        Some(Self {
            name: name.to_string(),
            tracks,
            sides,
            sector_table,
        })
    }

    pub fn from_raw_pc(name: &str, data: &[u8]) -> Self {
        let (tracks, sides, spt) = match data.len() {
            163_840 => (40, 1, 8),
            184_320 => (40, 1, 9),
            327_680 => (40, 2, 8),
            368_640 => (40, 2, 9),
            737_280 => (80, 2, 9),
            1_474_560 => (80, 2, 18),
            _ => (40, 2, 9),
        };
        let mut img = Self::new_blank(name, tracks, sides, spt, 512);
        let mut off = 0usize;
        for t in 0..tracks {
            for s in 0..sides {
                for sec in 0..spt {
                    img.sector_table[s][t][sec].r = (sec + 1) as u8;
                    if off + 512 <= data.len() {
                        img.sector_table[s][t][sec].data.copy_from_slice(&data[off..off + 512]);
                    } else if off < data.len() {
                        let rem = data.len() - off;
                        img.sector_table[s][t][sec].data[..rem].copy_from_slice(&data[off..]);
                    }
                    off += 512;
                }
            }
        }
        img
    }

    pub fn find_sector(&self, side: usize, track: usize, sector_id: u8) -> Option<&FloppySector> {
        let track_secs = self.sector_table.get(side)?.get(track)?;
        track_secs.iter().find(|s| s.r == sector_id)
    }

    pub fn find_sector_mut(
        &mut self,
        side: usize,
        track: usize,
        sector_id: u8,
    ) -> Option<&mut FloppySector> {
        let track_secs = self.sector_table.get_mut(side)?.get_mut(track)?;
        track_secs.iter_mut().find(|s| s.r == sector_id)
    }
}

/// Intel 8271 Floppy Disk Controller (BBC Micro Model B).
#[derive(Debug, Clone)]
pub struct I8271 {
    pub status: u8,
    pub result: u8,
    pub cmd: u8,
    pub params: Vec<u8>,
    pub expected_params: usize,
    pub data_queue: VecDeque<u8>,
    pub write_pending: bool,
    pub write_track: usize,
    pub write_sector: u8,
    pub write_count: usize,
    pub write_buf: Vec<u8>,
    pub current_track: [u8; 2],
    pub selected_drive: usize,
    pub current_side: usize,
    pub nmi_pending: bool,
    pub disk: Option<FloppyImage>,
    timer_cycles: u32,
    pending_completion: Option<u8>,
}

impl Default for I8271 {
    fn default() -> Self {
        Self::new()
    }
}

impl I8271 {
    pub const STAT_BUSY: u8 = 0x80;
    pub const STAT_CMD_FULL: u8 = 0x40;
    pub const STAT_PARAM_FULL: u8 = 0x20;
    pub const STAT_RESULT_FULL: u8 = 0x10;
    pub const STAT_INT_REQ: u8 = 0x08;
    pub const STAT_NON_DMA_DATA: u8 = 0x04;

    pub fn new() -> Self {
        Self {
            status: 0,
            result: 0,
            cmd: 0,
            params: Vec::new(),
            expected_params: 0,
            data_queue: VecDeque::new(),
            write_pending: false,
            write_track: 0,
            write_sector: 0,
            write_count: 0,
            write_buf: Vec::new(),
            current_track: [0, 0],
            selected_drive: 0,
            current_side: 0,
            nmi_pending: false,
            disk: None,
            timer_cycles: 0,
            pending_completion: None,
        }
    }

    pub fn reset(&mut self) {
        self.status = 0;
        self.result = 0;
        self.cmd = 0;
        self.params.clear();
        self.expected_params = 0;
        self.data_queue.clear();
        self.write_pending = false;
        self.nmi_pending = false;
        self.timer_cycles = 0;
        self.pending_completion = None;
    }

    pub fn insert_disk(&mut self, img: FloppyImage) {
        self.disk = Some(img);
    }

    /// Consumes a pending NMI edge (one edge per byte transfer or command completion).
    pub fn take_nmi(&mut self) -> bool {
        std::mem::replace(&mut self.nmi_pending, false)
    }

    /// Advances internal 8271 byte/seek timing and returns true if an NMI edge fired.
    pub fn tick(&mut self, cycles: u32) -> bool {
        if self.timer_cycles > 0 {
            if cycles >= self.timer_cycles {
                self.timer_cycles = 0;
                if let Some(code) = self.pending_completion.take() {
                    self.finish_transfer(code);
                } else if !self.data_queue.is_empty() || self.write_pending {
                    self.status |= Self::STAT_NON_DMA_DATA | Self::STAT_INT_REQ;
                    self.nmi_pending = true;
                }
            } else {
                self.timer_cycles -= cycles;
            }
        }
        self.take_nmi()
    }

    pub fn read(&mut self, reg: u8) -> u8 {
        match reg & 7 {
            0 => self.status,
            1 => {
                self.status &= !(Self::STAT_RESULT_FULL | Self::STAT_INT_REQ);
                self.nmi_pending = false;
                self.result
            }
            2..=7 => {
                if let Some(b) = self.data_queue.pop_front() {
                    self.status &= !(Self::STAT_NON_DMA_DATA | Self::STAT_INT_REQ);
                    self.nmi_pending = false;
                    if self.data_queue.is_empty() {
                        self.pending_completion = Some(0x00);
                        self.timer_cycles = 64;
                    } else {
                        self.timer_cycles = 64;
                    }
                    b
                } else {
                    0x00
                }
            }
            _ => 0xFF,
        }
    }

    fn finish_transfer(&mut self, result_code: u8) {
        self.status &= !(Self::STAT_BUSY | Self::STAT_NON_DMA_DATA);
        self.result = result_code;
        self.status |= Self::STAT_RESULT_FULL | Self::STAT_INT_REQ;
        self.nmi_pending = true;
    }

    pub fn write(&mut self, reg: u8, val: u8) {
        match reg & 7 {
            0 => {
                // Command register
                let drive_sel = (val >> 6) & 3;
                if (drive_sel & 1) != 0 {
                    self.selected_drive = 0;
                } else if (drive_sel & 2) != 0 {
                    self.selected_drive = 1;
                }
                self.cmd = val & 0x3F;
                self.params.clear();
                self.status &= !(Self::STAT_RESULT_FULL | Self::STAT_INT_REQ);
                self.nmi_pending = false;
                self.pending_completion = None;
                self.timer_cycles = 0;
                self.expected_params = match self.cmd {
                    0x35 => 4, // Specify
                    0x29 => 1, // Seek
                    0x2C => 0, // Read drive status
                    0x3D => 1, // Read special register
                    0x3A => 2, // Write special register
                    0x12 | 0x0A => 2, // Read/Write single sector
                    0x13 | 0x0B | 0x1F | 0x1B => 3, // Multi-sector read/write/verify
                    0x0F => 0, // Format?
                    _ => 0,
                };
                self.status |= Self::STAT_BUSY;
                if self.expected_params == 0 {
                    self.execute_cmd();
                }
            }
            1 => {
                // Parameter register
                if self.params.len() < self.expected_params {
                    self.params.push(val);
                    if self.params.len() == self.expected_params {
                        self.execute_cmd();
                    }
                }
            }
            2 => {
                // Reset register
                if (val & 1) != 0 {
                    self.reset();
                }
            }
            _ => {
                // Write DMA/Non-DMA data register (0xFE84 / offset 4, or offset 3)
                if self.write_pending {
                    self.write_buf.push(val);
                    self.status &= !(Self::STAT_NON_DMA_DATA | Self::STAT_INT_REQ);
                    self.nmi_pending = false;
                    if self.write_buf.len() >= self.write_count * 256 {
                        self.commit_write();
                    } else {
                        self.timer_cycles = 64;
                    }
                }
            }
        }
    }

    fn execute_cmd(&mut self) {
        match self.cmd {
            0x2C => {
                // Read drive status: Drive 0/1 present + Ready bits + Track 0
                let ready0 = if self.disk.is_some() && self.selected_drive == 0 {
                    0x04 | 0x40 | 0x80
                } else {
                    0x00
                };
                let ready1 = if self.disk.is_some() && self.selected_drive == 1 {
                    0x40
                } else {
                    0x00
                };
                let trk0 = if self.current_track[self.selected_drive] == 0 {
                    0x02
                } else {
                    0x00
                };
                self.status &= !Self::STAT_BUSY;
                self.result = ready0 | ready1 | trk0;
                self.status |= Self::STAT_RESULT_FULL;
            }
            0x3D => {
                // Read special reg
                let reg_id = self.params[0];
                let res = match reg_id {
                    0x12 => self.current_track[0],
                    0x1A => self.current_track[1],
                    0x23 => (self.current_side as u8) << 5,
                    _ => 0,
                };
                self.status &= !Self::STAT_BUSY;
                self.result = res;
                self.status |= Self::STAT_RESULT_FULL;
            }
            0x3A => {
                // Write special reg
                let reg_id = self.params[0];
                let val = self.params[1];
                match reg_id {
                    0x12 => self.current_track[0] = val,
                    0x1A => self.current_track[1] = val,
                    0x23 => self.current_side = if (val & 0x20) != 0 { 1 } else { 0 },
                    _ => {}
                }
                self.status &= !Self::STAT_BUSY;
            }
            0x35 => {
                self.status &= !Self::STAT_BUSY;
            }
            0x29 => {
                self.current_track[self.selected_drive] = self.params[0];
                self.pending_completion = Some(0x00);
                self.timer_cycles = 64;
            }
            0x12 | 0x13 => {
                let mut track = self.params[0] as usize;
                let mut s_id = self.params[1];
                let count = if self.cmd == 0x13 {
                    (self.params[2] & 0x1F).max(1) as usize
                } else {
                    1
                };
                self.current_track[self.selected_drive] = track as u8;
                self.data_queue.clear();
                let side = self.current_side;
                let mut ok = true;
                if let Some(disk) = &self.disk {
                    for _ in 0..count {
                        if let Some(sec) = disk.find_sector(side, track, s_id) {
                            self.data_queue.extend(&sec.data);
                            s_id = s_id.wrapping_add(1);
                            if s_id >= 10 {
                                s_id = 0;
                                track += 1;
                            }
                        } else {
                            ok = false;
                            break;
                        }
                    }
                } else {
                    ok = false;
                }
                if ok && !self.data_queue.is_empty() {
                    self.timer_cycles = 64;
                } else {
                    self.pending_completion = Some(0x18); // Drive not ready / Sector not found
                    self.timer_cycles = 64;
                }
            }
            0x0A | 0x0B => {
                self.write_track = self.params[0] as usize;
                self.write_sector = self.params[1];
                self.write_count = if self.cmd == 0x0B {
                    (self.params[2] & 0x1F).max(1) as usize
                } else {
                    1
                };
                self.write_buf.clear();
                self.write_pending = true;
                self.timer_cycles = 64;
            }
            0x1F | 0x1B => {
                let track = self.params[0] as usize;
                self.current_track[self.selected_drive] = track as u8;
                self.pending_completion = Some(if self.disk.is_some() { 0x00 } else { 0x10 });
                self.timer_cycles = 64;
            }
            _ => {
                self.pending_completion = Some(0x00);
                self.timer_cycles = 64;
            }
        }
    }

    fn commit_write(&mut self) {
        self.write_pending = false;
        let side = self.current_side;
        if let Some(disk) = &mut self.disk {
            let mut trk = self.write_track;
            let mut s_id = self.write_sector;
            for i in 0..self.write_count {
                if let Some(sec) = disk.find_sector_mut(side, trk, s_id) {
                    let start = i * 256;
                    let end = (start + sec.data.len()).min(self.write_buf.len());
                    if start < end {
                        let len = end - start;
                        sec.data[..len].copy_from_slice(&self.write_buf[start..end]);
                    }
                }
                s_id = s_id.wrapping_add(1);
                if s_id >= 10 {
                    s_id = 0;
                    trk += 1;
                }
            }
            self.pending_completion = Some(0x00);
            self.timer_cycles = 64;
        } else {
            self.pending_completion = Some(0x10);
            self.timer_cycles = 64;
        }
    }
}

/// Western Digital WD1770 / WD1772 Floppy Disk Controller (BBC Master / B+ / Enterprise).
#[derive(Debug, Clone)]
pub struct Wd1770 {
    pub status: u8,
    pub track: u8,
    pub sector: u8,
    pub data: u8,
    pub side: usize,
    pub data_queue: VecDeque<u8>,
    pub write_pending: bool,
    pub write_buf: Vec<u8>,
    pub intrq: bool,
    pub drq: bool,
    pub disk: Option<FloppyImage>,
    step_dir: i8,
    timer_cycles: u32,
    nmi_edge: bool,
}

impl Default for Wd1770 {
    fn default() -> Self {
        Self::new()
    }
}

impl Wd1770 {
    pub fn new() -> Self {
        Self {
            status: 0,
            track: 0,
            sector: 0,
            data: 0,
            side: 0,
            data_queue: VecDeque::new(),
            write_pending: false,
            write_buf: Vec::new(),
            intrq: false,
            drq: false,
            disk: None,
            step_dir: 1,
            timer_cycles: 0,
            nmi_edge: false,
        }
    }

    pub fn reset(&mut self) {
        self.status = 0;
        self.track = 0;
        self.sector = 1;
        self.data = 0;
        self.data_queue.clear();
        self.write_pending = false;
        self.intrq = false;
        self.drq = false;
        self.timer_cycles = 0;
        self.nmi_edge = false;
    }

    /// Advances WD1770 byte/command timing and returns true when a new NMI edge occurs.
    pub fn tick(&mut self, cycles: u32) -> bool {
        if self.timer_cycles > 0 {
            if cycles >= self.timer_cycles {
                self.timer_cycles = 0;
                if !self.data_queue.is_empty() || self.write_pending {
                    self.drq = true;
                    self.nmi_edge = true;
                } else if self.intrq {
                    self.nmi_edge = true;
                }
            } else {
                self.timer_cycles -= cycles;
            }
        }
        std::mem::replace(&mut self.nmi_edge, false)
    }

    pub fn read(&mut self, reg: u8) -> u8 {
        match reg & 3 {
            0 => {
                self.intrq = false;
                self.nmi_edge = false;
                self.status
            }
            1 => self.track,
            2 => self.sector,
            _ => {
                if let Some(b) = self.data_queue.pop_front() {
                    self.data = b;
                    self.drq = false;
                    self.nmi_edge = false;
                    if self.data_queue.is_empty() {
                        self.status &= !0x03;
                        self.intrq = true;
                        self.timer_cycles = 64;
                    } else {
                        self.timer_cycles = 64;
                    }
                }
                self.data
            }
        }
    }

    pub fn write(&mut self, reg: u8, val: u8) {
        match reg & 3 {
            0 => {
                self.intrq = false;
                self.nmi_edge = false;
                self.timer_cycles = 0;
                let cmd_hi = val >> 4;
                match cmd_hi {
                    0x0 => {
                        self.track = 0;
                        self.status = 0x04;
                        self.intrq = true;
                        self.timer_cycles = 64;
                    }
                    0x1 => {
                        self.track = self.data;
                        self.status = if self.track == 0 { 0x04 } else { 0x00 };
                        self.intrq = true;
                        self.timer_cycles = 64;
                    }
                    0x2 | 0x3 => {
                        let upd = (val & 0x10) != 0;
                        let next = (self.track as i16 + self.step_dir as i16).clamp(0, 82) as u8;
                        if upd {
                            self.track = next;
                        }
                        self.status = if next == 0 { 0x04 } else { 0x00 };
                        self.intrq = true;
                        self.timer_cycles = 64;
                    }
                    0x4 | 0x5 => {
                        self.step_dir = 1;
                        if (val & 0x10) != 0 {
                            self.track = self.track.saturating_add(1);
                        }
                        self.intrq = true;
                        self.timer_cycles = 64;
                    }
                    0x6 | 0x7 => {
                        self.step_dir = -1;
                        if (val & 0x10) != 0 {
                            self.track = self.track.saturating_sub(1);
                        }
                        self.status = if self.track == 0 { 0x04 } else { 0x00 };
                        self.intrq = true;
                        self.timer_cycles = 64;
                    }
                    0x8 | 0x9 => {
                        // Read Sector
                        self.data_queue.clear();
                        if let Some(disk) = &self.disk {
                            if let Some(sec) = disk.find_sector(self.side, self.track as usize, self.sector) {
                                self.data_queue.extend(&sec.data);
                                self.drq = false;
                                self.status = 0x03;
                                self.timer_cycles = 64;
                                return;
                            }
                        }
                        self.status = 0x10; // Record Not Found
                        self.intrq = true;
                        self.timer_cycles = 64;
                    }
                    0xA | 0xB => {
                        self.write_buf.clear();
                        self.write_pending = true;
                        self.drq = false;
                        self.status = 0x03;
                        self.timer_cycles = 64;
                    }
                    0xC => {
                        // Read Address (C H R N CRC1 CRC2)
                        self.data_queue.clear();
                        self.data_queue.extend([self.track, self.side as u8, self.sector, 1, 0, 0]);
                        self.drq = false;
                        self.status = 0x03;
                        self.timer_cycles = 64;
                    }
                    0xD => {
                        // Force Interrupt
                        self.status &= !0x03;
                        self.drq = false;
                        self.write_pending = false;
                        if (val & 0x0F) != 0 {
                            self.intrq = true;
                            self.timer_cycles = 64;
                        }
                    }
                    _ => {
                        self.status = 0;
                        self.intrq = true;
                        self.timer_cycles = 64;
                    }
                }
            }
            1 => self.track = val,
            2 => self.sector = val,
            _ => {
                self.data = val;
                if self.write_pending {
                    self.write_buf.push(val);
                    self.drq = false;
                    self.nmi_edge = false;
                    if self.write_buf.len() >= 256 {
                        self.write_pending = false;
                        self.status &= !0x03;
                        self.intrq = true;
                        self.timer_cycles = 64;
                        if let Some(disk) = &mut self.disk {
                            if let Some(sec) = disk.find_sector_mut(self.side, self.track as usize, self.sector) {
                                let len = sec.data.len().min(self.write_buf.len());
                                sec.data[..len].copy_from_slice(&self.write_buf[..len]);
                            }
                        }
                    } else {
                        self.timer_cycles = 64;
                    }
                }
            }
        }
    }
}

/// NEC uPD765A / Intel 8272A Floppy Disk Controller (Amstrad CPC / IBM PC).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum UpdPhase {
    Command,
    ExecRead,
    ExecWrite,
    Result,
}

#[derive(Debug, Clone)]
pub struct Upd765 {
    pub phase: UpdPhase,
    pub cmd_buf: Vec<u8>,
    pub cmd_expected: usize,
    pub result_buf: VecDeque<u8>,
    pub data_buf: VecDeque<u8>,
    pub track: [u8; 4],
    pub st0: u8,
    pub st1: u8,
    pub st2: u8,
    pub st3: u8,
    pub motor_on: bool,
    pub irq_pending: bool,
    pub disk: Option<FloppyImage>,
    write_target: (usize, usize, u8, usize),
    write_accum: Vec<u8>,
}

impl Default for Upd765 {
    fn default() -> Self {
        Self::new()
    }
}

impl Upd765 {
    pub fn new() -> Self {
        Self {
            phase: UpdPhase::Command,
            cmd_buf: Vec::new(),
            cmd_expected: 0,
            result_buf: VecDeque::new(),
            data_buf: VecDeque::new(),
            track: [0; 4],
            st0: 0,
            st1: 0,
            st2: 0,
            st3: 0,
            motor_on: false,
            irq_pending: false,
            disk: None,
            write_target: (0, 0, 0, 512),
            write_accum: Vec::new(),
        }
    }

    pub fn reset(&mut self) {
        self.phase = UpdPhase::Command;
        self.cmd_buf.clear();
        self.cmd_expected = 0;
        self.result_buf.clear();
        self.data_buf.clear();
        self.st0 = 0xC0;
        self.irq_pending = false;
    }

    pub fn read_msr(&self) -> u8 {
        let mut msr = 0x80u8; // RQM = ready
        match self.phase {
            UpdPhase::Command => {
                if !self.cmd_buf.is_empty() {
                    msr |= 0x10;
                }
            }
            UpdPhase::ExecRead => {
                msr |= 0x40 | 0x20 | 0x10; // DIO | NDM | CB
            }
            UpdPhase::ExecWrite => {
                msr |= 0x20 | 0x10; // NDM | CB
            }
            UpdPhase::Result => {
                msr |= 0x40 | 0x10; // DIO | CB
            }
        }
        msr
    }

    pub fn read_data(&mut self) -> u8 {
        match self.phase {
            UpdPhase::ExecRead => {
                let b = self.data_buf.pop_front().unwrap_or(0);
                if self.data_buf.is_empty() {
                    self.phase = UpdPhase::Result;
                }
                b
            }
            UpdPhase::Result => {
                let b = self.result_buf.pop_front().unwrap_or(0);
                if self.result_buf.is_empty() {
                    self.phase = UpdPhase::Command;
                }
                b
            }
            _ => 0xFF,
        }
    }

    pub fn write_data(&mut self, val: u8) {
        match self.phase {
            UpdPhase::Command => {
                if self.cmd_buf.is_empty() {
                    self.cmd_expected = match val & 0x1F {
                        0x03 => 3, // Specify
                        0x04 => 2, // Sense Drive Status
                        0x05 | 0x06 | 0x0C => 9, // Write/Read/ReadDeleted
                        0x07 => 2, // Recalibrate
                        0x08 => 1, // Sense Interrupt
                        0x0A => 2, // Read ID
                        0x0F => 3, // Seek
                        _ => 1,
                    };
                }
                self.cmd_buf.push(val);
                if self.cmd_buf.len() >= self.cmd_expected {
                    self.exec_cmd();
                }
            }
            UpdPhase::ExecWrite => {
                self.write_accum.push(val);
                let (side, trk, sec_id, len) = self.write_target;
                if self.write_accum.len() >= len {
                    if let Some(disk) = &mut self.disk {
                        if let Some(sec) = disk.find_sector_mut(side, trk, sec_id) {
                            let cpy = sec.data.len().min(self.write_accum.len());
                            sec.data[..cpy].copy_from_slice(&self.write_accum[..cpy]);
                        }
                    }
                    self.phase = UpdPhase::Result;
                    self.irq_pending = true;
                }
            }
            UpdPhase::Result | UpdPhase::ExecRead => {}
        }
    }

    fn exec_cmd(&mut self) {
        let cmd = self.cmd_buf[0] & 0x1F;
        let drive = (self.cmd_buf.get(1).copied().unwrap_or(0) & 3) as usize;
        self.result_buf.clear();
        self.data_buf.clear();

        match cmd {
            0x03 => {
                // Specify (no result)
                self.phase = UpdPhase::Command;
            }
            0x04 => {
                // Sense Drive Status
                let trk0 = if self.track[drive] == 0 { 0x10 } else { 0 };
                let rdy = if self.disk.is_some() && drive == 0 { 0x20 } else { 0 };
                self.st3 = 0x28 | trk0 | rdy | (drive as u8);
                self.result_buf.push_back(self.st3);
                self.phase = UpdPhase::Result;
            }
            0x07 => {
                // Recalibrate
                self.track[drive] = 0;
                self.st0 = 0x20 | (drive as u8);
                self.phase = UpdPhase::Command;
                self.irq_pending = true;
            }
            0x08 => {
                // Sense Interrupt Status
                self.result_buf.push_back(self.st0);
                self.result_buf.push_back(self.track[drive]);
                self.st0 = 0x80;
                self.phase = UpdPhase::Result;
                self.irq_pending = false;
            }
            0x0F => {
                // Seek
                let ncn = self.cmd_buf[2];
                self.track[drive] = ncn;
                self.st0 = 0x20 | (drive as u8);
                self.phase = UpdPhase::Command;
                self.irq_pending = true;
            }
            0x0A => {
                // Read ID
                let hd = ((self.cmd_buf[1] >> 2) & 1) as usize;
                let trk = self.track[drive] as usize;
                let (c, h, r, n) = self
                    .disk
                    .as_ref()
                    .and_then(|d| d.sector_table.get(hd)?.get(trk)?.first())
                    .map(|s| (s.c, s.h, s.r, s.n))
                    .unwrap_or((trk as u8, hd as u8, 0xC1, 2));
                self.st0 = drive as u8 | ((hd as u8) << 2);
                self.st1 = 0;
                self.st2 = 0;
                self.result_buf.extend([self.st0, self.st1, self.st2, c, h, r, n]);
                self.phase = UpdPhase::Result;
                self.irq_pending = true;
            }
            0x06 | 0x0C => {
                // Read Data
                let hd = ((self.cmd_buf[1] >> 2) & 1) as usize;
                let c = self.cmd_buf[2];
                let h = self.cmd_buf[3];
                let r = self.cmd_buf[4];
                let n = self.cmd_buf[5];
                let eot = self.cmd_buf[6];
                let trk = self.track[drive] as usize;
                let mut cur_r = r;
                let mut found = false;
                if let Some(disk) = &self.disk {
                    while let Some(sec) = disk.find_sector(hd, trk, cur_r) {
                        self.data_buf.extend(&sec.data);
                        found = true;
                        if cur_r >= eot {
                            break;
                        }
                        cur_r = cur_r.wrapping_add(1);
                    }
                }
                if found && !self.data_buf.is_empty() {
                    self.st0 = drive as u8 | ((hd as u8) << 2);
                    self.st1 = 0;
                    self.st2 = 0;
                    self.result_buf.extend([self.st0, self.st1, self.st2, c, h, cur_r.wrapping_add(1), n]);
                    self.phase = UpdPhase::ExecRead;
                    self.irq_pending = true;
                } else {
                    self.st0 = 0x40 | (drive as u8) | ((hd as u8) << 2);
                    self.st1 = 0x04;
                    self.st2 = 0x00;
                    self.result_buf.extend([self.st0, self.st1, self.st2, c, h, r, n]);
                    self.phase = UpdPhase::Result;
                    self.irq_pending = true;
                }
            }
            0x05 => {
                // Write Data
                let hd = ((self.cmd_buf[1] >> 2) & 1) as usize;
                let c = self.cmd_buf[2];
                let h = self.cmd_buf[3];
                let r = self.cmd_buf[4];
                let n = self.cmd_buf[5];
                let sec_len = 128usize << (n.min(5));
                self.write_target = (hd, self.track[drive] as usize, r, sec_len);
                self.write_accum.clear();
                self.st0 = drive as u8 | ((hd as u8) << 2);
                self.st1 = 0;
                self.st2 = 0;
                self.result_buf.extend([self.st0, self.st1, self.st2, c, h, r.wrapping_add(1), n]);
                self.phase = UpdPhase::ExecWrite;
            }
            _ => {
                self.st0 = 0x80;
                self.result_buf.push_back(self.st0);
                self.phase = UpdPhase::Result;
            }
        }

        self.cmd_buf.clear();
    }
}
