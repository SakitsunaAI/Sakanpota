use crate::types::{EmuError, EmuResult};
use std::fs;
use std::path::{Path, PathBuf};

/// Resolves a ROM path relative to the given `rom_dir` or fallback workspace paths.
pub fn resolve_rom_path(rom_dir: &Path, filename: &str) -> EmuResult<PathBuf> {
    let direct = rom_dir.join(filename);
    if direct.is_file() {
        return Ok(direct);
    }
    let rel_roms = Path::new("roms").join(filename);
    if rel_roms.is_file() {
        return Ok(rel_roms);
    }
    let rel_discs = Path::new("discs").join(filename);
    if rel_discs.is_file() {
        return Ok(rel_discs);
    }
    if let Ok(exe) = std::env::current_exe() {
        if let Some(parent) = exe.parent() {
            let sibling = parent.join("roms").join(filename);
            if sibling.is_file() {
                return Ok(sibling);
            }
            if let Some(grandparent) = parent.parent().and_then(|p| p.parent()) {
                let root_roms = grandparent.join("roms").join(filename);
                if root_roms.is_file() {
                    return Ok(root_roms);
                }
            }
        }
    }
    Err(EmuError::MissingRom(filename.to_string()))
}

pub fn load_rom_file(rom_dir: &Path, filename: &str) -> EmuResult<Vec<u8>> {
    let path = resolve_rom_path(rom_dir, filename)?;
    fs::read(&path).map_err(|e| EmuError::IoError(path.display().to_string(), e))
}

pub fn load_rom_exact(rom_dir: &Path, filename: &str, expected_size: usize) -> EmuResult<Vec<u8>> {
    let data = load_rom_file(rom_dir, filename)?;
    if data.len() != expected_size {
        return Err(EmuError::InvalidRomSize {
            name: filename.to_string(),
            expected: expected_size,
            actual: data.len(),
        });
    }
    Ok(data)
}

/// Extracted entry from a `.zip` archive.
#[derive(Debug, Clone)]
pub struct ZipEntry {
    pub name: String,
    pub data: Vec<u8>,
}

/// Reads all stored or DEFLATE-compressed entries from a ZIP archive using pure Rust.
pub fn read_zip_entries(zip_bytes: &[u8]) -> EmuResult<Vec<ZipEntry>> {
    // Locate End of Central Directory (EOCD: 0x06054b50)
    if zip_bytes.len() < 22 {
        return Err(EmuError::ArchiveError("ZIP file too small".to_string()));
    }
    let search_start = zip_bytes.len().saturating_sub(65_557);
    let mut eocd_pos = None;
    for i in (search_start..=zip_bytes.len() - 22).rev() {
        if &zip_bytes[i..i + 4] == b"PK\x05\x06" {
            eocd_pos = Some(i);
            break;
        }
    }
    let eocd = eocd_pos.ok_or_else(|| EmuError::ArchiveError("Missing ZIP EOCD".to_string()))?;
    let total_entries = u16::from_le_bytes([zip_bytes[eocd + 10], zip_bytes[eocd + 11]]) as usize;
    let cd_offset = u32::from_le_bytes([
        zip_bytes[eocd + 16],
        zip_bytes[eocd + 17],
        zip_bytes[eocd + 18],
        zip_bytes[eocd + 19],
    ]) as usize;

    let mut entries = Vec::with_capacity(total_entries);
    let mut pos = cd_offset;
    for _ in 0..total_entries {
        if pos + 46 > zip_bytes.len() || &zip_bytes[pos..pos + 4] != b"PK\x01\x02" {
            break;
        }
        let method = u16::from_le_bytes([zip_bytes[pos + 10], zip_bytes[pos + 11]]);
        let comp_size = u32::from_le_bytes([
            zip_bytes[pos + 20],
            zip_bytes[pos + 21],
            zip_bytes[pos + 22],
            zip_bytes[pos + 23],
        ]) as usize;
        let uncomp_size = u32::from_le_bytes([
            zip_bytes[pos + 24],
            zip_bytes[pos + 25],
            zip_bytes[pos + 26],
            zip_bytes[pos + 27],
        ]) as usize;
        let name_len = u16::from_le_bytes([zip_bytes[pos + 28], zip_bytes[pos + 29]]) as usize;
        let extra_len = u16::from_le_bytes([zip_bytes[pos + 30], zip_bytes[pos + 31]]) as usize;
        let comment_len = u16::from_le_bytes([zip_bytes[pos + 32], zip_bytes[pos + 33]]) as usize;
        let local_offset = u32::from_le_bytes([
            zip_bytes[pos + 42],
            zip_bytes[pos + 43],
            zip_bytes[pos + 44],
            zip_bytes[pos + 45],
        ]) as usize;

        if pos + 46 + name_len > zip_bytes.len() {
            return Err(EmuError::ArchiveError("Truncated central directory entry".to_string()));
        }
        let name = String::from_utf8_lossy(&zip_bytes[pos + 46..pos + 46 + name_len]).into_owned();
        pos += 46 + name_len + extra_len + comment_len;

        if name.ends_with('/') {
            continue;
        }
        if local_offset + 30 > zip_bytes.len() || &zip_bytes[local_offset..local_offset + 4] != b"PK\x03\x04" {
            return Err(EmuError::ArchiveError(format!("Bad local header for {name}")));
        }
        let l_name_len =
            u16::from_le_bytes([zip_bytes[local_offset + 26], zip_bytes[local_offset + 27]])
                as usize;
        let l_extra_len =
            u16::from_le_bytes([zip_bytes[local_offset + 28], zip_bytes[local_offset + 29]])
                as usize;
        let data_start = local_offset + 30 + l_name_len + l_extra_len;
        let data_end = data_start.saturating_add(comp_size);
        if data_end > zip_bytes.len() {
            return Err(EmuError::ArchiveError(format!("Truncated payload for {name}")));
        }
        let payload = &zip_bytes[data_start..data_end];
        let decompressed = match method {
            0 => payload.to_vec(),
            8 => inflate_raw(payload, uncomp_size)?,
            other => {
                return Err(EmuError::ArchiveError(format!(
                    "Unsupported ZIP method {other} for {name}"
                )));
            }
        };
        entries.push(ZipEntry {
            name,
            data: decompressed,
        });
    }
    Ok(entries)
}

/// Reads a raw media file or extracts the first matching entry inside a `.zip` archive.
/// Returns `(filename, lowercase_extension, bytes)`.
pub fn read_media_or_zip(
    path: &Path,
    allowed_exts: &[&str],
) -> EmuResult<(String, String, Vec<u8>)> {
    let bytes = fs::read(path).map_err(|e| EmuError::IoError(path.display().to_string(), e))?;
    let name = path
        .file_name()
        .and_then(|s| s.to_str())
        .unwrap_or("media")
        .to_string();
    let ext = path
        .extension()
        .and_then(|s| s.to_str())
        .map(|s| s.to_ascii_lowercase())
        .unwrap_or_default();

    if ext == "zip" {
        let entries = read_zip_entries(&bytes)?;
        for entry in entries {
            let entry_name = Path::new(&entry.name)
                .file_name()
                .and_then(|s| s.to_str())
                .unwrap_or(&entry.name)
                .to_string();
            let entry_ext = Path::new(&entry_name)
                .extension()
                .and_then(|s| s.to_str())
                .map(|s| s.to_ascii_lowercase())
                .unwrap_or_default();
            if allowed_exts.iter().any(|&a| a == entry_ext) {
                return Ok((entry_name, entry_ext, entry.data));
            }
        }
        return Err(EmuError::UnsupportedMedia(format!(
            "No supported file ({}) found inside ZIP archive {}",
            allowed_exts.join(", "),
            name
        )));
    }

    Ok((name, ext, bytes))
}

/// Pure-Rust RFC 1951 DEFLATE raw stream decoder.
pub fn inflate_raw(src: &[u8], expected_len: usize) -> EmuResult<Vec<u8>> {
    struct BitReader<'a> {
        data: &'a [u8],
        byte_pos: usize,
        bit_buf: u32,
        bit_cnt: u8,
    }
    impl<'a> BitReader<'a> {
        fn new(data: &'a [u8]) -> Self {
            Self {
                data,
                byte_pos: 0,
                bit_buf: 0,
                bit_cnt: 0,
            }
        }
        fn ensure(&mut self, n: u8) -> EmuResult<()> {
            while self.bit_cnt < n {
                if self.byte_pos >= self.data.len() {
                    return Err(EmuError::ArchiveError("Unexpected EOF in deflate stream".into()));
                }
                self.bit_buf |= (self.data[self.byte_pos] as u32) << self.bit_cnt;
                self.byte_pos += 1;
                self.bit_cnt += 8;
            }
            Ok(())
        }
        fn read_bits(&mut self, n: u8) -> EmuResult<u32> {
            if n == 0 {
                return Ok(0);
            }
            self.ensure(n)?;
            let val = self.bit_buf & ((1u32 << n) - 1);
            self.bit_buf >>= n;
            self.bit_cnt -= n;
            Ok(val)
        }
        fn align_byte(&mut self) {
            let discard = self.bit_cnt & 7;
            self.bit_buf >>= discard;
            self.bit_cnt -= discard;
        }
        fn read_aligned_u16(&mut self) -> EmuResult<u16> {
            self.align_byte();
            let lo = self.read_bits(8)? as u16;
            let hi = self.read_bits(8)? as u16;
            Ok(lo | (hi << 8))
        }
    }

    #[derive(Clone)]
    struct HuffTree {
        counts: [u16; 16],
        symbols: Vec<u16>,
    }
    impl HuffTree {
        fn from_lengths(lengths: &[u8]) -> EmuResult<Self> {
            let mut counts = [0u16; 16];
            for &len in lengths {
                if len > 15 {
                    return Err(EmuError::ArchiveError("Huffman code > 15 bits".into()));
                }
                if len > 0 {
                    counts[len as usize] += 1;
                }
            }
            let mut offs = [0u16; 16];
            let mut total = 0u16;
            for bits in 1..=15 {
                offs[bits] = total;
                total += counts[bits];
            }
            let mut symbols = vec![0u16; total as usize];
            for (sym, &len) in lengths.iter().enumerate() {
                if len > 0 {
                    let idx = offs[len as usize] as usize;
                    offs[len as usize] += 1;
                    symbols[idx] = sym as u16;
                }
            }
            Ok(Self { counts, symbols })
        }

        fn decode(&self, reader: &mut BitReader<'_>) -> EmuResult<u16> {
            let mut code = 0i32;
            let mut first = 0i32;
            let mut index = 0i32;
            for len in 1..=15 {
                code |= reader.read_bits(1)? as i32;
                let count = self.counts[len] as i32;
                if code - count < first {
                    let sym_idx = (index + (code - first)) as usize;
                    return self
                        .symbols
                        .get(sym_idx)
                        .copied()
                        .ok_or_else(|| EmuError::ArchiveError("Invalid Huffman symbol".into()));
                }
                index += count;
                first += count;
                first <<= 1;
                code <<= 1;
            }
            Err(EmuError::ArchiveError("Huffman decode overflow".into()))
        }
    }

    const LEN_BASE: [u16; 29] = [
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115,
        131, 163, 195, 227, 258,
    ];
    const LEN_EXTRA: [u8; 29] = [
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0,
    ];
    const DIST_BASE: [u16; 30] = [
        1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025,
        1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577,
    ];
    const DIST_EXTRA: [u8; 30] = [
        0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12,
        13, 13,
    ];
    const CL_ORDER: [usize; 19] = [
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15,
    ];

    let mut out = Vec::with_capacity(expected_len);
    let mut reader = BitReader::new(src);

    loop {
        let bfinal = reader.read_bits(1)?;
        let btype = reader.read_bits(2)?;
        match btype {
            0 => {
                let len = reader.read_aligned_u16()?;
                let nlen = reader.read_aligned_u16()?;
                if len ^ 0xFFFF != nlen {
                    return Err(EmuError::ArchiveError("Invalid stored block length".into()));
                }
                for _ in 0..len {
                    out.push(reader.read_bits(8)? as u8);
                }
            }
            1 | 2 => {
                let (lit_tree, dist_tree) = if btype == 1 {
                    let mut lit_lens = [0u8; 288];
                    for b in &mut lit_lens[0..=143] {
                        *b = 8;
                    }
                    for b in &mut lit_lens[144..=255] {
                        *b = 9;
                    }
                    for b in &mut lit_lens[256..=279] {
                        *b = 7;
                    }
                    for b in &mut lit_lens[280..=287] {
                        *b = 8;
                    }
                    let dist_lens = [5u8; 32];
                    (
                        HuffTree::from_lengths(&lit_lens)?,
                        HuffTree::from_lengths(&dist_lens)?,
                    )
                } else {
                    let hlit = reader.read_bits(5)? as usize + 257;
                    let hdist = reader.read_bits(5)? as usize + 1;
                    let hclen = reader.read_bits(4)? as usize + 4;
                    let mut cl_lens = [0u8; 19];
                    for i in 0..hclen {
                        cl_lens[CL_ORDER[i]] = reader.read_bits(3)? as u8;
                    }
                    let cl_tree = HuffTree::from_lengths(&cl_lens)?;
                    let total_codes = hlit + hdist;
                    let mut lengths = Vec::with_capacity(total_codes);
                    while lengths.len() < total_codes {
                        let sym = cl_tree.decode(&mut reader)?;
                        match sym {
                            0..=15 => lengths.push(sym as u8),
                            16 => {
                                let prev = *lengths.last().ok_or_else(|| {
                                    EmuError::ArchiveError("Invalid repeat code 16".into())
                                })?;
                                let rep = reader.read_bits(2)? as usize + 3;
                                lengths.resize((lengths.len() + rep).min(total_codes), prev);
                            }
                            17 => {
                                let rep = reader.read_bits(3)? as usize + 3;
                                lengths.resize((lengths.len() + rep).min(total_codes), 0);
                            }
                            18 => {
                                let rep = reader.read_bits(7)? as usize + 11;
                                lengths.resize((lengths.len() + rep).min(total_codes), 0);
                            }
                            _ => return Err(EmuError::ArchiveError("Bad code length sym".into())),
                        }
                    }
                    (
                        HuffTree::from_lengths(&lengths[..hlit])?,
                        HuffTree::from_lengths(&lengths[hlit..])?,
                    )
                };

                loop {
                    let sym = lit_tree.decode(&mut reader)?;
                    if sym < 256 {
                        out.push(sym as u8);
                    } else if sym == 256 {
                        break;
                    } else {
                        let len_idx = (sym - 257) as usize;
                        if len_idx >= LEN_BASE.len() {
                            return Err(EmuError::ArchiveError("Invalid length symbol".into()));
                        }
                        let length = LEN_BASE[len_idx] as usize
                            + reader.read_bits(LEN_EXTRA[len_idx])? as usize;
                        let dist_sym = dist_tree.decode(&mut reader)? as usize;
                        if dist_sym >= DIST_BASE.len() {
                            return Err(EmuError::ArchiveError("Invalid distance symbol".into()));
                        }
                        let distance = DIST_BASE[dist_sym] as usize
                            + reader.read_bits(DIST_EXTRA[dist_sym])? as usize;
                        if distance == 0 || distance > out.len() {
                            return Err(EmuError::ArchiveError("Invalid backreference".into()));
                        }
                        for _ in 0..length {
                            let b = out[out.len() - distance];
                            out.push(b);
                        }
                    }
                }
            }
            _ => return Err(EmuError::ArchiveError("Reserved deflate block type".into())),
        }
        if bfinal != 0 {
            break;
        }
    }
    Ok(out)
}
