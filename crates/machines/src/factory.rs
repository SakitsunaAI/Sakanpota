use std::path::Path;

use emu_core::detector::detect_media;
use emu_core::{BbcModel, EmuResult, EmulatorCore, SystemType};

use crate::{
    AmstradCpc464, Atari2600, BbcMicro, CamputersLynx, Commodore64, IbmPc5150, JupiterAce,
    Microvision, OricAtmos, PocketStation, SegaVmu, SharpMz700, Ti99_4A, ZxSpectrum48k,
};

pub fn create_machine(
    system: SystemType,
    bbc_model: BbcModel,
    rom_dir: &Path,
) -> EmuResult<Box<dyn EmulatorCore>> {
    let machine: Box<dyn EmulatorCore> = match system {
        SystemType::BbcMicro => Box::new(BbcMicro::new(bbc_model, rom_dir)?),
        SystemType::Commodore64 => Box::new(Commodore64::new(rom_dir)?),
        SystemType::Atari2600 => Box::new(Atari2600::new(rom_dir)?),
        SystemType::OricAtmos => Box::new(OricAtmos::new(rom_dir)?),
        SystemType::ZxSpectrum48k => Box::new(ZxSpectrum48k::new(rom_dir)?),
        SystemType::AmstradCpc464 => Box::new(AmstradCpc464::new(rom_dir)?),
        SystemType::JupiterAce => Box::new(JupiterAce::new(rom_dir)?),
        SystemType::SharpMz700 => Box::new(SharpMz700::new(rom_dir)?),
        SystemType::CamputersLynx => Box::new(CamputersLynx::new(rom_dir)?),
        SystemType::Ti99_4A => Box::new(Ti99_4A::new(rom_dir)?),
        SystemType::IbmPc5150 => Box::new(IbmPc5150::new(rom_dir)?),
        SystemType::Microvision => Box::new(Microvision::new(rom_dir)?),
        SystemType::SegaVmu => Box::new(SegaVmu::new(rom_dir)?),
        SystemType::PocketStation => Box::new(PocketStation::new(rom_dir)?),
    };
    Ok(machine)
}

pub fn create_machine_for_media(
    path: &Path,
    rom_dir: &Path,
) -> EmuResult<(Box<dyn EmulatorCore>, String)> {
    let detected = detect_media(path);
    let bbc_model = if detected.model_details.contains("Master") {
        BbcModel::Master128
    } else if detected.model_details.contains("B+") {
        BbcModel::ModelBPlus
    } else if detected.model_details.contains("Model A") {
        BbcModel::ModelA
    } else {
        BbcModel::ModelB
    };
    let mut machine = create_machine(detected.system, bbc_model, rom_dir)?;
    machine.load_media(path)?;
    Ok((machine, detected.reason))
}
