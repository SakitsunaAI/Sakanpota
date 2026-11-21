use crate::archive::read_zip_entries;
use crate::types::{BbcModel, SystemType};
use std::fs;
use std::path::Path;

#[derive(Debug, Clone)]
pub struct DetectedMedia {
    pub system: SystemType,
    pub model_details: String,
    pub confidence: u8,
    pub reason: String,
}

fn detect_by_ext_and_data(
    ext: &str,
    filename: &str,
    bytes: &[u8],
    from_zip: bool,
) -> Option<DetectedMedia> {
    let prefix = if from_zip { "ZIP containing " } else { "" };
    match ext {
        "ssd" | "dsd" | "adl" | "uef" => Some(DetectedMedia {
            system: SystemType::BbcMicro,
            model_details: detect_bbc_model(bytes).display_name().to_string(),
            confidence: 99,
            reason: format!("{prefix}BBC Micro .{ext}"),
        }),
        "d64" | "t64" | "prg" | "crt" => Some(DetectedMedia {
            system: SystemType::Commodore64,
            model_details: "Commodore 64 PAL (6510)".into(),
            confidence: 99,
            reason: format!("{prefix}Commodore 64 .{ext}"),
        }),
        "a26" => Some(DetectedMedia {
            system: SystemType::Atari2600,
            model_details: "Atari 2600 VCS (6507 + TIA)".into(),
            confidence: 99,
            reason: format!("{prefix}Atari 2600 cartridge .a26"),
        }),
        "oric" => Some(DetectedMedia {
            system: SystemType::OricAtmos,
            model_details: "Oric Atmos 48K".into(),
            confidence: 95,
            reason: format!("{prefix}Oric Atmos tape image"),
        }),
        "tap" if filename.contains("oric") || bytes.starts_with(&[0x16, 0x16, 0x16, 0x24]) => {
            Some(DetectedMedia {
                system: SystemType::OricAtmos,
                model_details: "Oric Atmos 48K".into(),
                confidence: 96,
                reason: format!("{prefix}Oric Atmos tape image .tap"),
            })
        }
        "sna" | "z80" | "tzx" | "tap" => Some(DetectedMedia {
            system: SystemType::ZxSpectrum48k,
            model_details: "Sinclair ZX Spectrum 48K".into(),
            confidence: 98,
            reason: format!("{prefix}ZX Spectrum .{ext}"),
        }),
        "dsk" | "cdt" | "cpc" => Some(DetectedMedia {
            system: SystemType::AmstradCpc464,
            model_details: "Amstrad CPC 464".into(),
            confidence: 95,
            reason: format!("{prefix}Amstrad CPC .{ext}"),
        }),
        "ace" => Some(DetectedMedia {
            system: SystemType::JupiterAce,
            model_details: "Jupiter Ace Forth (Z80)".into(),
            confidence: 99,
            reason: format!("{prefix}Jupiter Ace snapshot/tape .ace"),
        }),
        "mzf" | "mzt" => Some(DetectedMedia {
            system: SystemType::SharpMz700,
            model_details: "Sharp MZ-700".into(),
            confidence: 99,
            reason: format!("{prefix}Sharp MZ-700 tape .{ext}"),
        }),
        "cld" => Some(DetectedMedia {
            system: SystemType::CamputersLynx,
            model_details: "Camputers Lynx 48K".into(),
            confidence: 95,
            reason: format!("{prefix}Camputers Lynx cassette .cld"),
        }),
        "com" | "exe" | "img" | "ima" => Some(DetectedMedia {
            system: SystemType::IbmPc5150,
            model_details: "IBM PC 5150 (8088)".into(),
            confidence: 98,
            reason: format!("{prefix}IBM PC / DOS media .{ext}"),
        }),
        "mv" => Some(DetectedMedia {
            system: SystemType::Microvision,
            model_details: "MB Microvision (TMS1100)".into(),
            confidence: 99,
            reason: format!("{prefix}Microvision cartridge .mv"),
        }),
        "vms" | "dci" => Some(DetectedMedia {
            system: SystemType::SegaVmu,
            model_details: "Sega Dreamcast VMU (LC8670)".into(),
            confidence: 99,
            reason: format!("{prefix}Sega VMU file .{ext}"),
        }),
        "mcs" | "gme" => Some(DetectedMedia {
            system: SystemType::PocketStation,
            model_details: "Sony PocketStation (ARM7TDMI)".into(),
            confidence: 99,
            reason: format!("{prefix}PocketStation save/app .{ext}"),
        }),
        _ => None,
    }
}

/// Identifies the exact retained LLE target system from file extension, size, and headers.
pub fn detect_media(path: &Path) -> DetectedMedia {
    let ext = path
        .extension()
        .and_then(|e| e.to_str())
        .map(|s| s.to_ascii_lowercase())
        .unwrap_or_default();

    let filename = path
        .file_name()
        .and_then(|n| n.to_str())
        .map(|s| s.to_ascii_lowercase())
        .unwrap_or_default();

    let bytes = fs::read(path).unwrap_or_default();
    let size = bytes.len();

    if ext == "zip" && !bytes.is_empty() {
        if let Ok(entries) = read_zip_entries(&bytes) {
            for e in entries {
                let ename = e.name.to_ascii_lowercase();
                let entry_ext = Path::new(&ename)
                    .extension()
                    .and_then(|s| s.to_str())
                    .unwrap_or_default();
                if let Some(det) = detect_by_ext_and_data(entry_ext, &ename, &e.data, true) {
                    return det;
                }
                if ename.contains("1z-013a") {
                    return DetectedMedia {
                        system: SystemType::SharpMz700,
                        model_details: format!("Sharp MZ-700 Media ({})", e.name),
                        confidence: 95,
                        reason: "ZIP containing Sharp MZ-700 image".into(),
                    };
                }
            }
        }
    }

    if let Some(det) = detect_by_ext_and_data(ext.as_str(), &filename, &bytes, false) {
        return det;
    }

    match ext.as_str() {
        "bin" => {
            if filename.contains("pitfall") || size == 2048 || size == 4096 {
                if filename.contains("block")
                    || filename.contains("bowling")
                    || filename.contains("connect")
                    || filename.contains("pinball")
                {
                    DetectedMedia {
                        system: SystemType::Microvision,
                        model_details: "MB Microvision (TMS1100)".into(),
                        confidence: 95,
                        reason: "2KB Microvision ROM filename".into(),
                    }
                } else if filename.contains("vmu") {
                    DetectedMedia {
                        system: SystemType::SegaVmu,
                        model_details: "Sega Dreamcast VMU".into(),
                        confidence: 95,
                        reason: "VMU BIOS/Flash binary".into(),
                    }
                } else {
                    DetectedMedia {
                        system: SystemType::Atari2600,
                        model_details: "Atari 2600 VCS".into(),
                        confidence: 80,
                        reason: format!("{size}-byte 2600 ROM"),
                    }
                }
            } else if filename.contains("pocket") {
                DetectedMedia {
                    system: SystemType::PocketStation,
                    model_details: "Sony PocketStation".into(),
                    confidence: 90,
                    reason: "PocketStation binary".into(),
                }
            } else {
                DetectedMedia {
                    system: SystemType::BbcMicro,
                    model_details: "BBC Micro Model B".into(),
                    confidence: 50,
                    reason: "Fallback binary".into(),
                }
            }
        }
        _ => DetectedMedia {
            system: SystemType::BbcMicro,
            model_details: "BBC Micro Model B".into(),
            confidence: 50,
            reason: "Default fallback to BBC Micro".into(),
        },
    }
}

/// Inspects an Acorn DFS `.ssd`/`.dsd` disk image to select the optimal BBC model.
pub fn detect_bbc_model(disk_data: &[u8]) -> BbcModel {
    if disk_data.len() < 512 {
        return BbcModel::ModelB;
    }
    let num_files = ((disk_data[0x105] & 0xF8) >> 3) as usize;
    let mut score_b = 0i32;
    let mut score_bp = 0i32;
    let mut score_master = 0i32;

    let mut title = String::new();
    for &b in &disk_data[0..8] {
        if (32..=126).contains(&b) {
            title.push(b as char);
        }
    }
    for &b in &disk_data[0x100..0x104] {
        if (32..=126).contains(&b) {
            title.push(b as char);
        }
    }
    let upper_title = title.to_ascii_uppercase();
    if upper_title.contains("MASTER") || upper_title.contains("M128") {
        score_master += 50;
    }

    for i in 0..num_files.min(31) {
        let cat_off = 8 + i * 8;
        let info_off = 0x108 + i * 8;
        if info_off + 7 >= disk_data.len() {
            break;
        }
        let mut fname = String::new();
        for j in 0..7 {
            let c = disk_data[cat_off + j] & 0x7F;
            if c > 32 {
                fname.push((c as char).to_ascii_uppercase());
            }
        }
        if fname.contains("M128") || fname.contains("CMOS") {
            score_master += 40;
        }
        if fname.contains("SHADOW") {
            score_bp += 30;
            score_master += 20;
        }
        let extra = disk_data[info_off + 6];
        let load_addr = (disk_data[info_off] as u32)
            | ((disk_data[info_off + 1] as u32) << 8)
            | (((extra >> 2) & 0x03) as u32) << 16;
        if (0xC000..=0xCFFF).contains(&load_addr) {
            score_master += 35;
        } else if (0x8000..=0xBFFF).contains(&load_addr) {
            score_bp += 25;
            score_master += 25;
        }
        if fname == "ELITE" || fname == "WELCOME" {
            score_b += 20;
        }
    }

    if score_master > score_bp && score_master > score_b && score_master >= 40 {
        BbcModel::Master128
    } else if score_bp > score_b && score_bp >= 30 {
        BbcModel::ModelBPlus
    } else {
        BbcModel::ModelB
    }
}
