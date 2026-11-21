use std::collections::HashSet;
use std::path::{Path, PathBuf};

use emu_core::{BbcModel, CANVAS_HEIGHT, CANVAS_WIDTH, EmuError, EmulatorCore, SystemType};
use machines::{create_machine, create_machine_for_media};

fn workspace_root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("../..")
        .canonicalize()
        .expect("workspace root must resolve")
}

fn resolve_test_rom_dir() -> Option<PathBuf> {
    if let Some(env_dir) = std::env::var_os("SAKANPOTA_ROM_DIR").filter(|v| !v.is_empty()) {
        let p = PathBuf::from(env_dir);
        if p.is_dir() {
            return Some(p);
        }
    }
    let local = workspace_root().join("roms");
    if local.is_dir() {
        return Some(local);
    }
    None
}

fn assert_live_lle_state(label: &str, emu: &mut Box<dyn EmulatorCore>) {
    let before = emu.cpu_state();
    assert!(before.cycles > 0, "{label}: CPU executed 0 cycles");
    assert!(!before.arch.is_empty(), "{label}: empty CPU architecture string");
    assert!(
        !before.registers.is_empty(),
        "{label}: empty CPU register snapshot"
    );

    let step_cycles = emu.step_instruction();
    assert!(step_cycles > 0, "{label}: step_instruction returned 0 cycles");

    let after = emu.cpu_state();
    assert!(
        after.cycles >= before.cycles,
        "{label}: CPU cycle counter did not advance on step_instruction"
    );

    let fb = emu.rendered_screen();
    assert_eq!(
        fb.len(),
        CANVAS_WIDTH * CANVAS_HEIGHT,
        "{label}: unexpected framebuffer dimensions"
    );
    let colors: HashSet<u32> = fb.iter().copied().collect();
    assert!(
        colors.len() >= 2,
        "{label}: framebuffer remained uniform ({} color)",
        colors.len()
    );

    let mem = emu.inspect_memory(after.pc & !0x0F, 32);
    assert_eq!(mem.len(), 32, "{label}: live memory inspector failed");

    let disasm = emu.disassemble(after.pc, 6);
    assert!(!disasm.is_empty(), "{label}: live disassembler returned 0 entries");
    for insn in &disasm {
        assert!(
            !insn.mnemonic.trim().is_empty(),
            "{label}: disassembler emitted empty mnemonic at {:06X}",
            insn.address
        );
    }
}

fn assert_live_lle_boot(label: &str, emu: &mut Box<dyn EmulatorCore>) {
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

    assert!(
        changed_pixels >= 64,
        "{label}: display unchanged from baseline ({changed_pixels} changed pixels)"
    );
    assert_live_lle_state(label, emu);
}

#[test]
fn all_bbc_models_boot_and_render_genuine_frames() {
    let Some(rom_dir) = resolve_test_rom_dir() else {
        eprintln!(
            "Skipping all_bbc_models_boot_and_render_genuine_frames: ROM directory not found (place ROMs in roms/ or set SAKANPOTA_ROM_DIR)"
        );
        return;
    };
    let models = [
        ("BBC Model A", BbcModel::ModelA, 0x3C00u32),
        ("BBC Model B", BbcModel::ModelB, 0x7C00u32),
        ("BBC Model B+", BbcModel::ModelBPlus, 0x7C00u32),
        ("BBC Master 128", BbcModel::Master128, 0x7C00u32),
    ];

    for (label, model, vram_base) in models {
        let mut emu = create_machine(SystemType::BbcMicro, model, &rom_dir)
            .unwrap_or_else(|e| panic!("{label}: failed to initialize: {e}"));
        assert_eq!(emu.system_type(), SystemType::BbcMicro);
        assert_live_lle_boot(label, &mut emu);

        for _ in 0..30 {
            emu.run_frame();
        }
        let vram = emu.inspect_memory(vram_base, 1000);
        let text: String = vram
            .iter()
            .map(|&b| if (32..=126).contains(&b) { b as char } else { ' ' })
            .collect();
        assert!(
            text.contains("BASIC"),
            "{label}: expected BASIC prompt in Mode 7 VRAM, got {text:?}"
        );
        assert!(
            !text.contains("This is not a language") && !text.contains("Bad command"),
            "{label}: unexpected MOS error banner in Mode 7 VRAM: {text:?}"
        );
        if model == BbcModel::ModelBPlus {
            assert_ne!(
                emu.cpu_state().pc,
                0x8CC6,
                "BBC Model B+ stuck polling $FE80 inside DFS"
            );
            let mut active_audio_frames = 0usize;
            for _ in 0..30 {
                emu.run_frame();
                if emu.audio_buffer().iter().any(|&s| s != 0) {
                    active_audio_frames += 1;
                }
            }
            assert!(
                active_audio_frames < 30,
                "BBC Model B+ startup audio stuck continuously buzzing ({active_audio_frames}/30 frames)"
            );
        }

        emu.reset();
        assert_live_lle_boot(&format!("{label} (post-reset)"), &mut emu);
    }
}

#[test]
fn all_14_lle_systems_boot_execute_and_reset_cleanly() {
    let Some(rom_dir) = resolve_test_rom_dir() else {
        eprintln!(
            "Skipping all_14_lle_systems_boot_execute_and_reset_cleanly: ROM directory not found (place ROMs in roms/ or set SAKANPOTA_ROM_DIR)"
        );
        return;
    };

    for sys in SystemType::ALL {
        let mut emu = create_machine(sys, BbcModel::ModelB, &rom_dir)
            .unwrap_or_else(|e| panic!("{}: failed to initialize: {e}", sys.display_name()));
        assert_eq!(emu.system_type(), sys);
        assert_live_lle_boot(sys.display_name(), &mut emu);

        emu.reset();
        assert_live_lle_boot(&format!("{} (post-reset)", sys.display_name()), &mut emu);
    }
}

#[test]
fn media_autodetection_loads_and_boots_target_platforms() {
    let Some(rom_dir) = resolve_test_rom_dir() else {
        eprintln!(
            "Skipping media_autodetection_loads_and_boots_target_platforms: ROM directory not found (place ROMs in roms/ or set SAKANPOTA_ROM_DIR)"
        );
        return;
    };
    let root = workspace_root();

    let cases = [
        (rom_dir.join("atari_pitfall.bin"), SystemType::Atari2600),
        (
            rom_dir.join("microvision/block.bin"),
            SystemType::Microvision,
        ),
        (root.join("discs/elite.ssd"), SystemType::BbcMicro),
    ];

    for (media_path, expected_sys) in cases {
        assert!(
            media_path.exists(),
            "missing test media asset {}",
            media_path.display()
        );
        let (mut emu, reason) = create_machine_for_media(&media_path, &rom_dir)
            .unwrap_or_else(|e| panic!("failed to load {}: {e}", media_path.display()));
        assert!(!reason.is_empty(), "empty media detection reason");
        assert_eq!(emu.system_type(), expected_sys);
        assert_live_lle_boot(&media_path.display().to_string(), &mut emu);
    }
}

#[test]
fn bbc_micro_loads_and_autoboots_ssd_discs() {
    let Some(rom_dir) = resolve_test_rom_dir() else {
        eprintln!(
            "Skipping bbc_micro_loads_and_autoboots_ssd_discs: ROM directory not found (place ROMs in roms/ or set SAKANPOTA_ROM_DIR)"
        );
        return;
    };
    let root = workspace_root();

    let mut idle_bbc = create_machine(SystemType::BbcMicro, BbcModel::ModelB, &rom_dir)
        .expect("idle BBC Model B must boot");
    for _ in 0..45 {
        idle_bbc.run_frame();
    }
    let idle_fb = idle_bbc.rendered_screen().to_vec();

    for disc_name in ["Welcome.ssd", "elite.ssd"] {
        let disc_path = root.join("discs").join(disc_name);
        assert!(
            disc_path.exists(),
            "missing disc {}",
            disc_path.display()
        );

        let (mut emu, reason) = create_machine_for_media(&disc_path, &rom_dir)
            .unwrap_or_else(|e| panic!("failed to load {disc_name}: {e}"));
        assert_eq!(emu.system_type(), SystemType::BbcMicro);
        assert!(!reason.is_empty());

        for _ in 0..90 {
            emu.run_frame();
        }
        let diff = emu
            .rendered_screen()
            .iter()
            .zip(idle_fb.iter())
            .filter(|(a, b)| a != b)
            .count();
        assert!(
            diff >= 64,
            "{disc_name}: screen remained stuck on idle BASIC prompt without auto-booting ({diff} px diff)"
        );
        assert_live_lle_state(disc_name, &mut emu);
    }
}

#[test]
fn cycle_media_switches_bbc_discs() {
    let Some(rom_dir) = resolve_test_rom_dir() else {
        eprintln!(
            "Skipping cycle_media_switches_bbc_discs: ROM directory not found (place ROMs in roms/ or set SAKANPOTA_ROM_DIR)"
        );
        return;
    };
    let root = workspace_root();

    // BBC Micro F11 disc cycling across discs/
    let mut bbc = create_machine(SystemType::BbcMicro, BbcModel::ModelB, &rom_dir)
        .expect("BBC Model B must initialize");
    let disc_dir = root.join("discs");
    bbc.load_media(&disc_dir.join("Welcome.ssd"))
        .expect("load Welcome.ssd");
    let first_disc = bbc.media_name().to_string();
    bbc.cycle_media().expect("BBC cycle_media should succeed");
    let second_disc = bbc.media_name().to_string();
    assert_ne!(first_disc, second_disc, "BBC F11 must advance to next disc");
    assert_live_lle_boot(&format!("BBC F11 -> {second_disc}"), &mut bbc);
}

#[test]
fn verify_microvision_all_four_cartridges() {
    let Some(rom_dir) = resolve_test_rom_dir() else {
        eprintln!(
            "Skipping verify_microvision_all_four_cartridges: ROM directory not found (place ROMs in roms/ or set SAKANPOTA_ROM_DIR)"
        );
        return;
    };
    let mut mv = create_machine(SystemType::Microvision, BbcModel::ModelB, &rom_dir)
        .expect("Microvision must initialize");

    let mut seen = Vec::new();
    for i in 0..4 {
        let name = mv.media_name().to_string();
        assert_live_lle_boot(&format!("Microvision cart {name}"), &mut mv);
        seen.push(name);
        if i < 3 {
            mv.cycle_media().expect("Microvision cycle_media must succeed");
        }
    }
    let unique: HashSet<String> = seen.into_iter().collect();
    assert_eq!(unique.len(), 4, "Expected all 4 Microvision cartridges to be visited");
}

#[test]
fn missing_roms_return_error_without_embedded_fallback() {
    let nonexistent = Path::new("/nonexistent/sakanpota/roms");
    assert!(matches!(
        emu_core::archive::load_rom_file(nonexistent, "__missing_test__.rom"),
        Err(EmuError::MissingRom(_))
    ));
    if !workspace_root().join("roms").is_dir() {
        for sys in SystemType::ALL {
            assert!(
                matches!(
                    create_machine(sys, BbcModel::ModelB, nonexistent),
                    Err(EmuError::MissingRom(_))
                ),
                "{}: expected MissingRom when external ROMs are absent",
                sys.display_name()
            );
        }
    }
}
