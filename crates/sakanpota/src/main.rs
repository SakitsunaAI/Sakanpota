use std::collections::HashSet;
use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process;
use std::time::{Duration, Instant};

use emu_core::crt_filter::CrtFilter;
use emu_core::gui_font::{
    draw_string, fill_rect, render_debugger_overlay, render_launcher_overlay,
};
use emu_core::{
    AUDIO_SAMPLE_RATE, BbcModel, CANVAS_HEIGHT, CANVAS_WIDTH, EmuError, EmuResult, EmulatorCore,
    KeyCode, KeyMod, SystemType,
};
use machines::{create_machine, create_machine_for_media};
use sdl2::audio::AudioSpecDesired;
use sdl2::event::Event;
use sdl2::keyboard::{Keycode as SdlKey, Mod as SdlMod};
use sdl2::pixels::PixelFormatEnum;

struct CliArgs {
    system: SystemType,
    bbc_model: BbcModel,
    rom_dir: Option<PathBuf>,
    rom_path: Option<PathBuf>,
    verify_all: bool,
    show_help: bool,
}

fn find_rom_dir(override_dir: Option<&Path>) -> PathBuf {
    if let Some(dir) = override_dir {
        return dir.to_path_buf();
    }
    if let Some(env_dir) = env::var_os("SAKANPOTA_ROM_DIR").filter(|v| !v.is_empty()) {
        return PathBuf::from(env_dir);
    }
    let local = PathBuf::from("roms");
    if local.is_dir() {
        return local;
    }
    if let Ok(exe) = env::current_exe() {
        if let Some(dir) = exe.parent() {
            let beside = dir.join("roms");
            if beside.is_dir() {
                return beside;
            }
            if let Some(root) = dir.parent().and_then(|p| p.parent()) {
                let workspace_roms = root.join("roms");
                if workspace_roms.is_dir() {
                    return workspace_roms;
                }
            }
        }
    }
    local
}

fn parse_system(s: &str) -> Option<SystemType> {
    let q = s.trim().to_ascii_lowercase();
    SystemType::ALL
        .iter()
        .copied()
        .find(|sys| sys.cli_id() == q)
}

fn parse_bbc_model(s: &str) -> Option<BbcModel> {
    match s.trim().to_ascii_lowercase().as_str() {
        "a" | "modela" | "model-a" => Some(BbcModel::ModelA),
        "b" | "modelb" | "model-b" => Some(BbcModel::ModelB),
        "b+" | "bplus" | "modelbplus" => Some(BbcModel::ModelBPlus),
        "m" | "master" | "master128" => Some(BbcModel::Master128),
        _ => None,
    }
}

fn next_bbc_model(model: BbcModel) -> BbcModel {
    match model {
        BbcModel::ModelA => BbcModel::ModelB,
        BbcModel::ModelB => BbcModel::ModelBPlus,
        BbcModel::ModelBPlus => BbcModel::Master128,
        BbcModel::Master128 => BbcModel::ModelA,
    }
}

fn parse_args() -> EmuResult<CliArgs> {
    let mut cli = CliArgs {
        system: SystemType::BbcMicro,
        bbc_model: BbcModel::ModelB,
        rom_dir: None,
        rom_path: None,
        verify_all: false,
        show_help: false,
    };

    let mut args = env::args().skip(1);
    while let Some(arg) = args.next() {
        match arg.as_str() {
            "-h" | "--help" => cli.show_help = true,
            "--verify-all" => cli.verify_all = true,
            "-s" | "--system" => {
                let val = args.next().ok_or_else(|| {
                    EmuError::FrontendError("--system requires a system ID".into())
                })?;
                cli.system = parse_system(&val).ok_or_else(|| {
                    EmuError::FrontendError(format!("Unknown system ID '{val}'"))
                })?;
            }
            "--bbc" => {
                let val = args.next().ok_or_else(|| {
                    EmuError::FrontendError("--bbc requires one of: a, b, bplus, master".into())
                })?;
                cli.bbc_model = parse_bbc_model(&val).ok_or_else(|| {
                    EmuError::FrontendError(format!("Unknown BBC model '{val}'"))
                })?;
            }
            "--rom-dir" => {
                let val = args.next().ok_or_else(|| {
                    EmuError::FrontendError("--rom-dir requires a directory path".into())
                })?;
                cli.rom_dir = Some(PathBuf::from(val));
            }
            "-r" | "--rom" => {
                let val = args.next().ok_or_else(|| {
                    EmuError::FrontendError("--rom requires a file path".into())
                })?;
                cli.rom_path = Some(PathBuf::from(val));
            }
            other if !other.starts_with('-') => {
                let p = PathBuf::from(other);
                if p.exists() && cli.rom_path.is_none() {
                    cli.rom_path = Some(p);
                } else if let Some(sys) = parse_system(other) {
                    cli.system = sys;
                } else if cli.rom_path.is_none() {
                    cli.rom_path = Some(p);
                } else {
                    return Err(EmuError::FrontendError(format!(
                        "Unrecognized argument: {other}"
                    )));
                }
            }
            unknown => {
                return Err(EmuError::FrontendError(format!(
                    "Unrecognized argument: {unknown}"
                )));
            }
        }
    }
    Ok(cli)
}

fn print_help() {
    println!("sakanpota — Multi-System Low-Level Hardware Emulator (14 LLE Systems)");
    println!("Usage: sakanpota [OPTIONS] [MEDIA_FILE]");
    println!();
    println!("Options:");
    println!("  -s, --system <id>   Select initial system (default: bbc)");
    println!("      --bbc <model>   BBC model: a, b (default), bplus, master");
    println!("      --rom-dir <dir> Directory containing system BIOS/ROMs (or SAKANPOTA_ROM_DIR)");
    println!("  -r, --rom <path>    Load cartridge/tape/disk/ROM with auto-detection");
    println!("      --verify-all    Headless LLE boot verification across all 14 systems");
    println!("  -h, --help          Show this help text");
    println!();
    println!("Systems:");
    for sys in SystemType::ALL {
        println!("  {:<12} {}", sys.cli_id(), sys.display_name());
    }
    println!();
    println!(
        "Hotkeys: F6 Launcher | F12 System | F11 Media | F7 BBC Model | F10 Debugger | F8 CRT | F9 Phosphor | Alt+T Turbo | Alt+S Screenshot | Shift+F5/F5 Reset"
    );
}

fn discover_media_library(rom_dir: &Path) -> Vec<(String, PathBuf)> {
    const EXTS: &[&str] = &[
        "ssd", "dsd", "d64", "prg", "crt", "tap", "z80", "sna", "mzf", "mzt", "zip", "bin",
        "gg", "sms", "pce", "c8", "gba", "gb", "gbc", "nes", "ngp", "chf", "col", "vb", "dsk",
        "cdt", "rom",
    ];
    let mut items = Vec::new();
    let mut dirs = Vec::new();
    let parent_discs = rom_dir.parent().map(|p| p.join("discs")).filter(|p| p.is_dir());
    if let Some(discs) = parent_discs {
        dirs.push(("discs", discs));
    } else if Path::new("discs").is_dir() {
        dirs.push(("discs", PathBuf::from("discs")));
    }
    if rom_dir.is_dir() {
        dirs.push(("roms", rom_dir.to_path_buf()));
    }
    for (tag, dir) in dirs {
        let mut stack = vec![dir];
        while let Some(cur) = stack.pop() {
            if let Ok(entries) = fs::read_dir(&cur) {
                for entry in entries.flatten() {
                    let p = entry.path();
                    if p.is_dir() {
                        stack.push(p);
                        continue;
                    }
                    let ext = p
                        .extension()
                        .and_then(|s| s.to_str())
                        .map(|s| s.to_ascii_lowercase())
                        .unwrap_or_default();
                    if EXTS.iter().any(|&e| e == ext) {
                        let fname = p
                            .file_name()
                            .and_then(|s| s.to_str())
                            .unwrap_or("media")
                            .to_string();
                        items.push((format!("[{tag}] {fname}"), p));
                    }
                }
            }
        }
    }
    items.sort_by(|a, b| a.0.cmp(&b.0));
    items
}

fn save_screenshot_bmp(buf: &[u32], sys_id: &str, counter: u32) -> EmuResult<PathBuf> {
    fs::create_dir_all("screenshots")
        .map_err(|e| EmuError::IoError("screenshots".to_string(), e))?;
    let path = PathBuf::from(format!("screenshots/sakanpota_{sys_id}_{counter:03}.bmp"));
    let row_bytes = CANVAS_WIDTH * 3;
    let img_bytes = row_bytes * CANVAS_HEIGHT;
    let file_size = 54 + img_bytes;
    let mut out = Vec::with_capacity(file_size);
    out.extend_from_slice(b"BM");
    out.extend_from_slice(&(file_size as u32).to_le_bytes());
    out.extend_from_slice(&[0u8; 4]);
    out.extend_from_slice(&54u32.to_le_bytes());
    out.extend_from_slice(&40u32.to_le_bytes());
    out.extend_from_slice(&(CANVAS_WIDTH as i32).to_le_bytes());
    out.extend_from_slice(&(CANVAS_HEIGHT as i32).to_le_bytes());
    out.extend_from_slice(&1u16.to_le_bytes());
    out.extend_from_slice(&24u16.to_le_bytes());
    out.extend_from_slice(&0u32.to_le_bytes());
    out.extend_from_slice(&(img_bytes as u32).to_le_bytes());
    out.extend_from_slice(&2835i32.to_le_bytes());
    out.extend_from_slice(&2835i32.to_le_bytes());
    out.extend_from_slice(&[0u8; 8]);
    for y in (0..CANVAS_HEIGHT).rev() {
        let row_start = y * CANVAS_WIDTH;
        for x in 0..CANVAS_WIDTH {
            let px = buf[row_start + x];
            out.push((px & 0xFF) as u8);
            out.push(((px >> 8) & 0xFF) as u8);
            out.push(((px >> 16) & 0xFF) as u8);
        }
    }
    fs::write(&path, out).map_err(|e| EmuError::IoError(path.display().to_string(), e))?;
    Ok(path)
}

fn verify_instance(label: &str, mut emu: Box<dyn EmulatorCore>) -> EmuResult<()> {
    let _ = emu.step_instruction();
    let baseline = emu.rendered_screen().to_vec();
    let mut changed_pixels = 0usize;
    for frame in 0..180u32 {
        emu.run_frame();
        changed_pixels = baseline
            .iter()
            .zip(emu.rendered_screen().iter())
            .filter(|(a, b)| a != b)
            .count();
        if frame >= 14 && changed_pixels >= 64 {
            break;
        }
    }
    let state = emu.cpu_state();
    if state.cycles == 0 {
        return Err(EmuError::ExecutionFault(format!(
            "{label}: CPU executed 0 cycles"
        )));
    }
    let fb = emu.rendered_screen();
    if fb.len() != CANVAS_WIDTH * CANVAS_HEIGHT {
        return Err(EmuError::ExecutionFault(format!(
            "{label}: unexpected framebuffer size {}",
            fb.len()
        )));
    }
    let colors: HashSet<u32> = fb.iter().copied().collect();
    if colors.len() < 2 {
        return Err(EmuError::ExecutionFault(format!(
            "{label}: framebuffer remained uniform ({} color)",
            colors.len()
        )));
    }
    if changed_pixels < 64 {
        return Err(EmuError::ExecutionFault(format!(
            "{label}: display unchanged from baseline ({changed_pixels} changed pixels)"
        )));
    }
    let mem = emu.inspect_memory(state.pc, 16);
    if mem.len() != 16 {
        return Err(EmuError::ExecutionFault(format!(
            "{label}: live memory inspector returned {} bytes",
            mem.len()
        )));
    }
    let disasm = emu.disassemble(state.pc, 4);
    if disasm.is_empty() {
        return Err(EmuError::ExecutionFault(format!(
            "{label}: live disassembler returned 0 instructions"
        )));
    }
    println!(
        "[PASS] {:<32} | {} | PC:{:06X} | Cycles:{:>8} | Colors:{:>3} | Changed:{:>6} | {}",
        label, state.arch, state.pc, state.cycles, colors.len(), changed_pixels, emu.media_name()
    );
    Ok(())
}

fn run_verify_all(rom_dir: &Path) -> EmuResult<()> {
    let mut failures = Vec::new();
    let models = [
        ("BBC Model A", BbcModel::ModelA),
        ("BBC Model B", BbcModel::ModelB),
        ("BBC Model B+", BbcModel::ModelBPlus),
        ("BBC Master 128", BbcModel::Master128),
    ];

    for (name, model) in models {
        match create_machine(SystemType::BbcMicro, model, rom_dir) {
            Ok(emu) => {
                if let Err(e) = verify_instance(name, emu) {
                    eprintln!("[FAIL] {e}");
                    failures.push(e.to_string());
                }
            }
            Err(e) => {
                eprintln!("[FAIL] {name}: {e}");
                failures.push(format!("{name}: {e}"));
            }
        }
    }

    for sys in SystemType::ALL {
        if sys == SystemType::BbcMicro {
            continue;
        }
        match create_machine(sys, BbcModel::ModelB, rom_dir) {
            Ok(emu) => {
                if let Err(e) = verify_instance(sys.display_name(), emu) {
                    eprintln!("[FAIL] {e}");
                    failures.push(e.to_string());
                }
            }
            Err(e) => {
                eprintln!("[FAIL] {}: {e}", sys.display_name());
                failures.push(format!("{}: {e}", sys.display_name()));
            }
        }
    }

    if failures.is_empty() {
        println!(
            "All 14 LLE systems and all 4 BBC Micro models verified with live CPU execution."
        );
        Ok(())
    } else {
        Err(EmuError::ExecutionFault(format!(
            "{} verification failure(s)",
            failures.len()
        )))
    }
}

fn map_sdl_key(k: SdlKey) -> KeyCode {
    match k {
        SdlKey::A => KeyCode::A,
        SdlKey::B => KeyCode::B,
        SdlKey::C => KeyCode::C,
        SdlKey::D => KeyCode::D,
        SdlKey::E => KeyCode::E,
        SdlKey::F => KeyCode::F,
        SdlKey::G => KeyCode::G,
        SdlKey::H => KeyCode::H,
        SdlKey::I => KeyCode::I,
        SdlKey::J => KeyCode::J,
        SdlKey::K => KeyCode::K,
        SdlKey::L => KeyCode::L,
        SdlKey::M => KeyCode::M,
        SdlKey::N => KeyCode::N,
        SdlKey::O => KeyCode::O,
        SdlKey::P => KeyCode::P,
        SdlKey::Q => KeyCode::Q,
        SdlKey::R => KeyCode::R,
        SdlKey::S => KeyCode::S,
        SdlKey::T => KeyCode::T,
        SdlKey::U => KeyCode::U,
        SdlKey::V => KeyCode::V,
        SdlKey::W => KeyCode::W,
        SdlKey::X => KeyCode::X,
        SdlKey::Y => KeyCode::Y,
        SdlKey::Z => KeyCode::Z,
        SdlKey::Num0 | SdlKey::Kp0 => KeyCode::Num0,
        SdlKey::Num1 | SdlKey::Kp1 => KeyCode::Num1,
        SdlKey::Num2 | SdlKey::Kp2 => KeyCode::Num2,
        SdlKey::Num3 | SdlKey::Kp3 => KeyCode::Num3,
        SdlKey::Num4 | SdlKey::Kp4 => KeyCode::Num4,
        SdlKey::Num5 | SdlKey::Kp5 => KeyCode::Num5,
        SdlKey::Num6 | SdlKey::Kp6 => KeyCode::Num6,
        SdlKey::Num7 | SdlKey::Kp7 => KeyCode::Num7,
        SdlKey::Num8 | SdlKey::Kp8 => KeyCode::Num8,
        SdlKey::Num9 | SdlKey::Kp9 => KeyCode::Num9,
        SdlKey::Minus | SdlKey::KpMinus => KeyCode::Minus,
        SdlKey::Equals | SdlKey::KpEquals => KeyCode::Equals,
        SdlKey::LeftBracket => KeyCode::LeftBracket,
        SdlKey::RightBracket => KeyCode::RightBracket,
        SdlKey::Backslash => KeyCode::Backslash,
        SdlKey::Semicolon => KeyCode::Semicolon,
        SdlKey::Quote => KeyCode::Apostrophe,
        SdlKey::Comma => KeyCode::Comma,
        SdlKey::Period | SdlKey::KpPeriod => KeyCode::Period,
        SdlKey::Slash | SdlKey::KpDivide => KeyCode::Slash,
        SdlKey::Backquote => KeyCode::Backquote,
        SdlKey::Space => KeyCode::Space,
        SdlKey::Return | SdlKey::KpEnter => KeyCode::Return,
        SdlKey::Escape => KeyCode::Escape,
        SdlKey::Backspace => KeyCode::Backspace,
        SdlKey::Tab => KeyCode::Tab,
        SdlKey::CapsLock => KeyCode::CapsLock,
        SdlKey::Delete => KeyCode::Delete,
        SdlKey::Insert => KeyCode::Insert,
        SdlKey::Home => KeyCode::Home,
        SdlKey::End => KeyCode::End,
        SdlKey::PageUp => KeyCode::PageUp,
        SdlKey::PageDown => KeyCode::PageDown,
        SdlKey::Up => KeyCode::Up,
        SdlKey::Down => KeyCode::Down,
        SdlKey::Left => KeyCode::Left,
        SdlKey::Right => KeyCode::Right,
        SdlKey::LShift => KeyCode::LShift,
        SdlKey::RShift => KeyCode::RShift,
        SdlKey::LCtrl => KeyCode::LCtrl,
        SdlKey::RCtrl => KeyCode::RCtrl,
        SdlKey::LAlt => KeyCode::LAlt,
        SdlKey::RAlt => KeyCode::RAlt,
        SdlKey::F1 => KeyCode::F1,
        SdlKey::F2 => KeyCode::F2,
        SdlKey::F3 => KeyCode::F3,
        SdlKey::F4 => KeyCode::F4,
        SdlKey::F5 => KeyCode::F5,
        SdlKey::F6 => KeyCode::F6,
        SdlKey::F7 => KeyCode::F7,
        SdlKey::F8 => KeyCode::F8,
        SdlKey::F9 => KeyCode::F9,
        SdlKey::F10 => KeyCode::F10,
        SdlKey::F11 => KeyCode::F11,
        SdlKey::F12 => KeyCode::F12,
        _ => KeyCode::Unknown,
    }
}

fn map_sdl_mod(m: SdlMod) -> KeyMod {
    KeyMod {
        shift: m.intersects(SdlMod::LSHIFTMOD | SdlMod::RSHIFTMOD),
        ctrl: m.intersects(SdlMod::LCTRLMOD | SdlMod::RCTRLMOD),
        alt: m.intersects(SdlMod::LALTMOD | SdlMod::RALTMOD),
    }
}

fn run_interactive(cli: CliArgs, rom_dir: &Path) -> EmuResult<()> {
    let mut current_sys = cli.system;
    let mut current_bbc = cli.bbc_model;
    let mut machine: Box<dyn EmulatorCore> = if let Some(ref media_path) = cli.rom_path {
        let (emu, _reason) = create_machine_for_media(media_path, rom_dir)?;
        current_sys = emu.system_type();
        emu
    } else {
        create_machine(current_sys, current_bbc, rom_dir)?
    };

    let sdl = sdl2::init().map_err(EmuError::FrontendError)?;
    let video = sdl.video().map_err(EmuError::FrontendError)?;
    let mut title = format!("sakanpota - {} [{}]", machine.system_name(), machine.media_name());
    let mut window = video
        .window(&title, (CANVAS_WIDTH as u32) * 2, (CANVAS_HEIGHT as u32) * 2)
        .position_centered()
        .resizable()
        .build()
        .map_err(|e| EmuError::FrontendError(e.to_string()))?;

    let mut canvas = window
        .clone()
        .into_canvas()
        .accelerated()
        .present_vsync()
        .build()
        .map_err(|e| EmuError::FrontendError(e.to_string()))?;

    let creator = canvas.texture_creator();
    let mut texture = creator
        .create_texture_streaming(
            PixelFormatEnum::ARGB8888,
            CANVAS_WIDTH as u32,
            CANVAS_HEIGHT as u32,
        )
        .map_err(|e| EmuError::FrontendError(e.to_string()))?;

    let audio_queue = sdl.audio().ok().and_then(|audio| {
        let desired = AudioSpecDesired {
            freq: Some(AUDIO_SAMPLE_RATE as i32),
            channels: Some(1),
            samples: Some(1024),
        };
        let queue = audio.open_queue::<i16, _>(None, &desired).ok()?;
        queue.resume();
        Some(queue)
    });

    let mut event_pump = sdl.event_pump().map_err(EmuError::FrontendError)?;
    let mut crt = CrtFilter::new();
    let mut present_buf = vec![0xFF00_0000u32; CANVAS_WIDTH * CANVAS_HEIGHT];
    let mut byte_buf = vec![0u8; CANVAS_WIDTH * CANVAS_HEIGHT * 4];
    let mut debugger_open = false;
    let mut launcher_open = false;
    let mut launcher_focus_right = false;
    let mut launcher_sys_idx = SystemType::ALL
        .iter()
        .position(|&s| s == current_sys)
        .unwrap_or(0);
    let mut launcher_media_idx = 0usize;
    let mut media_library = discover_media_library(rom_dir);
    let mut active_media_path = cli.rom_path.clone();
    let mut paused = false;
    let mut turbo_mode = false;
    let mut screenshot_seq = 1u32;
    let mut mem_base: u32 = machine.cpu_state().pc & !0xFF;
    let mut hud_message = format!("{} | {}", machine.system_name(), machine.media_name());
    let mut hud_until = Instant::now() + Duration::from_secs(3);

    let frame_period = Duration::from_nanos(16_666_667);

    'main_loop: loop {
        let frame_start = Instant::now();

        for event in event_pump.poll_iter() {
            match event {
                Event::Quit { .. } => break 'main_loop,
                Event::DropFile { filename, .. } => {
                    let drop_path = PathBuf::from(&filename);
                    match create_machine_for_media(&drop_path, rom_dir) {
                        Ok((emu, reason)) => {
                            current_sys = emu.system_type();
                            machine = emu;
                            active_media_path = Some(drop_path);
                            launcher_sys_idx = SystemType::ALL
                                .iter()
                                .position(|&s| s == current_sys)
                                .unwrap_or(0);
                            mem_base = machine.cpu_state().pc & !0xFF;
                            hud_message = format!("Loaded ({reason}): {}", machine.media_name());
                            hud_until = Instant::now() + Duration::from_secs(4);
                        }
                        Err(e) => {
                            hud_message = format!("Load failed: {e}");
                            hud_until = Instant::now() + Duration::from_secs(4);
                        }
                    }
                }
                Event::KeyDown {
                    keycode: Some(k),
                    keymod,
                    repeat,
                    ..
                } => {
                    let alt = keymod.intersects(SdlMod::LALTMOD | SdlMod::RALTMOD);
                    let shift = keymod.intersects(SdlMod::LSHIFTMOD | SdlMod::RSHIFTMOD);
                    if !repeat {
                        match k {
                            SdlKey::F6 => {
                                launcher_open = !launcher_open;
                                if launcher_open {
                                    debugger_open = false;
                                    media_library = discover_media_library(rom_dir);
                                    launcher_sys_idx = SystemType::ALL
                                        .iter()
                                        .position(|&s| s == current_sys)
                                        .unwrap_or(0);
                                    if launcher_media_idx >= media_library.len() {
                                        launcher_media_idx = 0;
                                    }
                                }
                                continue;
                            }
                            SdlKey::F10 => {
                                debugger_open = !debugger_open;
                                if debugger_open {
                                    launcher_open = false;
                                    mem_base = machine.cpu_state().pc & !0xFF;
                                }
                                continue;
                            }
                            SdlKey::T if alt => {
                                turbo_mode = !turbo_mode;
                                hud_message = format!(
                                    "Turbo Mode (4x): {}",
                                    if turbo_mode { "ON" } else { "OFF" }
                                );
                                hud_until = Instant::now() + Duration::from_secs(2);
                                continue;
                            }
                            SdlKey::S if alt => {
                                match save_screenshot_bmp(
                                    &present_buf,
                                    current_sys.cli_id(),
                                    screenshot_seq,
                                ) {
                                    Ok(saved) => {
                                        screenshot_seq += 1;
                                        hud_message = format!("Screenshot: {}", saved.display());
                                    }
                                    Err(e) => {
                                        hud_message = format!("Screenshot failed: {e}");
                                    }
                                }
                                hud_until = Instant::now() + Duration::from_secs(3);
                                continue;
                            }
                            SdlKey::R if alt => {
                                if let Some(ref media_path) = active_media_path {
                                    match machine.load_media(media_path) {
                                        Ok(()) => {
                                            hud_message =
                                                format!("Reloaded: {}", machine.media_name());
                                        }
                                        Err(e) => {
                                            hud_message = format!("Reload failed: {e}");
                                        }
                                    }
                                } else {
                                    machine.reset();
                                    hud_message = format!("Reset: {}", machine.system_name());
                                }
                                hud_until = Instant::now() + Duration::from_secs(3);
                                continue;
                            }
                            SdlKey::F12 => {
                                current_sys = current_sys.next();
                                launcher_sys_idx = SystemType::ALL
                                    .iter()
                                    .position(|&s| s == current_sys)
                                    .unwrap_or(0);
                                match create_machine(current_sys, current_bbc, rom_dir) {
                                    Ok(emu) => {
                                        machine = emu;
                                        active_media_path = None;
                                        paused = false;
                                        mem_base = machine.cpu_state().pc & !0xFF;
                                        hud_message = format!(
                                            "System: {} [{}]",
                                            machine.system_name(),
                                            machine.media_name()
                                        );
                                        hud_until = Instant::now() + Duration::from_secs(3);
                                    }
                                    Err(e) => {
                                        hud_message = format!("System switch error: {e}");
                                        hud_until = Instant::now() + Duration::from_secs(4);
                                    }
                                }
                                continue;
                            }
                            SdlKey::F11 => {
                                match machine.cycle_media() {
                                    Ok(()) => {
                                        active_media_path = None;
                                        hud_message = format!("Media: {}", machine.media_name());
                                    }
                                    Err(e) => {
                                        hud_message = format!("Media cycle: {e}");
                                    }
                                }
                                hud_until = Instant::now() + Duration::from_secs(3);
                                continue;
                            }
                            SdlKey::F9 => {
                                crt.cycle_color_mode();
                                hud_message = format!("CRT Mode: {}", crt.color_mode.name());
                                hud_until = Instant::now() + Duration::from_secs(2);
                                continue;
                            }
                            SdlKey::F8 => {
                                crt.toggle_crt_effects();
                                hud_message = format!(
                                    "CRT Effects: {}",
                                    if crt.scanlines_enabled { "ON" } else { "OFF" }
                                );
                                hud_until = Instant::now() + Duration::from_secs(2);
                                continue;
                            }
                            SdlKey::F7 if current_sys == SystemType::BbcMicro || launcher_open => {
                                current_bbc = next_bbc_model(current_bbc);
                                if current_sys == SystemType::BbcMicro {
                                    match create_machine(SystemType::BbcMicro, current_bbc, rom_dir)
                                    {
                                        Ok(mut emu) => {
                                            if let Some(ref p) = active_media_path {
                                                let _ = emu.load_media(p);
                                            }
                                            machine = emu;
                                            mem_base = machine.cpu_state().pc & !0xFF;
                                            hud_message =
                                                format!("Model: {}", current_bbc.display_name());
                                        }
                                        Err(e) => {
                                            hud_message = format!("BBC switch error: {e}");
                                        }
                                    }
                                } else {
                                    hud_message =
                                        format!("BBC Model: {}", current_bbc.display_name());
                                }
                                hud_until = Instant::now() + Duration::from_secs(3);
                                continue;
                            }
                            SdlKey::F5 if !debugger_open && !launcher_open => {
                                if shift && current_sys == SystemType::BbcMicro {
                                    machine.handle_key_down(KeyCode::F5, map_sdl_mod(keymod));
                                    hud_message = "BBC SHIFT+BREAK".to_string();
                                } else {
                                    machine.reset();
                                    hud_message = format!("Reset: {}", machine.system_name());
                                }
                                hud_until = Instant::now() + Duration::from_secs(2);
                                continue;
                            }
                            SdlKey::Escape if launcher_open => {
                                launcher_open = false;
                                continue;
                            }
                            SdlKey::Tab | SdlKey::Left | SdlKey::Right if launcher_open => {
                                launcher_focus_right = !launcher_focus_right;
                                continue;
                            }
                            SdlKey::Up if launcher_open => {
                                if launcher_focus_right && !media_library.is_empty() {
                                    launcher_media_idx = launcher_media_idx
                                        .checked_sub(1)
                                        .unwrap_or(media_library.len() - 1);
                                } else if !SystemType::ALL.is_empty() {
                                    launcher_sys_idx = launcher_sys_idx
                                        .checked_sub(1)
                                        .unwrap_or(SystemType::ALL.len() - 1);
                                }
                                continue;
                            }
                            SdlKey::Down if launcher_open => {
                                if launcher_focus_right && !media_library.is_empty() {
                                    launcher_media_idx =
                                        (launcher_media_idx + 1) % media_library.len();
                                } else if !SystemType::ALL.is_empty() {
                                    launcher_sys_idx =
                                        (launcher_sys_idx + 1) % SystemType::ALL.len();
                                }
                                continue;
                            }
                            SdlKey::Return if launcher_open => {
                                if launcher_focus_right && !media_library.is_empty() {
                                    let path = media_library[launcher_media_idx].1.clone();
                                    match create_machine_for_media(&path, rom_dir) {
                                        Ok((emu, reason)) => {
                                            current_sys = emu.system_type();
                                            machine = emu;
                                            active_media_path = Some(path);
                                            launcher_open = false;
                                            paused = false;
                                            mem_base = machine.cpu_state().pc & !0xFF;
                                            hud_message = format!(
                                                "Loaded ({reason}): {}",
                                                machine.media_name()
                                            );
                                            hud_until = Instant::now() + Duration::from_secs(4);
                                        }
                                        Err(e) => {
                                            hud_message = format!("Load failed: {e}");
                                            hud_until = Instant::now() + Duration::from_secs(4);
                                        }
                                    }
                                } else {
                                    let target_sys = SystemType::ALL[launcher_sys_idx];
                                    match create_machine(target_sys, current_bbc, rom_dir) {
                                        Ok(emu) => {
                                            current_sys = target_sys;
                                            machine = emu;
                                            active_media_path = None;
                                            launcher_open = false;
                                            paused = false;
                                            mem_base = machine.cpu_state().pc & !0xFF;
                                            hud_message = format!(
                                                "System: {} [{}]",
                                                machine.system_name(),
                                                machine.media_name()
                                            );
                                            hud_until = Instant::now() + Duration::from_secs(3);
                                        }
                                        Err(e) => {
                                            hud_message = format!("Launch failed: {e}");
                                            hud_until = Instant::now() + Duration::from_secs(4);
                                        }
                                    }
                                }
                                continue;
                            }
                            SdlKey::F1 if debugger_open => {
                                paused = !paused;
                                continue;
                            }
                            SdlKey::F2 if debugger_open => {
                                paused = true;
                                machine.step_instruction();
                                continue;
                            }
                            SdlKey::F3 if debugger_open => {
                                paused = true;
                                machine.run_frame();
                                continue;
                            }
                            SdlKey::Up if debugger_open => {
                                mem_base = mem_base.saturating_sub(16);
                                continue;
                            }
                            SdlKey::Down if debugger_open => {
                                mem_base = mem_base.wrapping_add(16);
                                continue;
                            }
                            SdlKey::PageUp if debugger_open => {
                                mem_base = mem_base.saturating_sub(0x100);
                                continue;
                            }
                            SdlKey::PageDown if debugger_open => {
                                mem_base = mem_base.wrapping_add(0x100);
                                continue;
                            }
                            SdlKey::Home if debugger_open => {
                                mem_base = machine.cpu_state().pc & !0xFF;
                                continue;
                            }
                            SdlKey::End if debugger_open => {
                                mem_base = machine.cpu_state().sp & !0xFF;
                                continue;
                            }
                            _ => {}
                        }
                    }

                    if !debugger_open && !launcher_open {
                        let mapped = map_sdl_key(k);
                        if mapped != KeyCode::Unknown {
                            machine.handle_key_down(mapped, map_sdl_mod(keymod));
                        }
                    }
                }
                Event::KeyUp {
                    keycode: Some(k),
                    keymod,
                    ..
                } if !debugger_open && !launcher_open => {
                    let mapped = map_sdl_key(k);
                    if mapped != KeyCode::Unknown {
                        machine.handle_key_up(mapped, map_sdl_mod(keymod));
                    }
                }
                _ => {}
            }
        }

        if !paused && !launcher_open {
            let step_frames = if turbo_mode { 4 } else { 1 };
            for i in 0..step_frames {
                machine.run_frame();
                if i + 1 == step_frames || !turbo_mode {
                    if let Some(ref queue) = audio_queue {
                        let samples = machine.audio_buffer();
                        if !samples.is_empty() && queue.size() < 88_200 {
                            let _ = queue.queue_audio(samples);
                        }
                    }
                }
            }
        }

        let new_title = format!(
            "sakanpota - {} [{}]{}",
            machine.system_name(),
            machine.media_name(),
            if turbo_mode { " [TURBO 4x]" } else { "" }
        );
        if new_title != title {
            title = new_title;
            let _ = window.set_title(&title);
        }

        let raw_fb = machine.rendered_screen();
        if crt.is_active() {
            crt.process(raw_fb, &mut present_buf, CANVAS_WIDTH, CANVAS_HEIGHT);
        } else {
            present_buf.copy_from_slice(&raw_fb[..CANVAS_WIDTH * CANVAS_HEIGHT]);
        }

        if launcher_open {
            let systems_view: Vec<(&str, bool)> = SystemType::ALL
                .iter()
                .map(|&s| (s.display_name(), s == current_sys))
                .collect();
            let media_labels: Vec<String> =
                media_library.iter().map(|(lbl, _)| lbl.clone()).collect();
            render_launcher_overlay(
                &mut present_buf,
                &systems_view,
                launcher_sys_idx,
                &media_labels,
                launcher_media_idx,
                launcher_focus_right,
                current_bbc.display_name(),
            );
        } else if debugger_open {
            let cpu = machine.cpu_state();
            let disasm = machine.disassemble(cpu.pc, 13);
            let mem = machine.inspect_memory(mem_base, 224);
            render_debugger_overlay(
                &mut present_buf,
                machine.system_name(),
                machine.media_name(),
                paused,
                &cpu,
                &disasm,
                mem_base,
                &mem,
                machine.audio_buffer(),
            );
        } else if Instant::now() < hud_until {
            fill_rect(&mut present_buf, 0, 0, CANVAS_WIDTH as i32, 14, 0xFF10_1824);
            draw_string(&mut present_buf, 6, 3, &hud_message, 0xFF72_EFDD);
        }

        for (i, &px) in present_buf.iter().enumerate() {
            let off = i * 4;
            byte_buf[off..off + 4].copy_from_slice(&px.to_le_bytes());
        }
        texture
            .update(None, &byte_buf, CANVAS_WIDTH * 4)
            .map_err(|e| EmuError::FrontendError(e.to_string()))?;

        canvas.clear();
        canvas
            .copy(&texture, None, None)
            .map_err(EmuError::FrontendError)?;
        canvas.present();

        let elapsed = frame_start.elapsed();
        if elapsed < frame_period {
            std::thread::sleep(frame_period - elapsed);
        }
    }

    Ok(())
}

fn main() {
    let cli = match parse_args() {
        Ok(c) => c,
        Err(e) => {
            eprintln!("Error: {e}");
            process::exit(2);
        }
    };

    if cli.show_help {
        print_help();
        return;
    }

    let rom_dir = find_rom_dir(cli.rom_dir.as_deref());
    let res = if cli.verify_all {
        run_verify_all(&rom_dir)
    } else {
        run_interactive(cli, &rom_dir)
    };

    if let Err(e) = res {
        eprintln!("sakanpota error: {e}");
        process::exit(1);
    }
}
