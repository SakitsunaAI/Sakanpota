pub mod ay8910;
pub mod cia6526;
pub mod crtc6845;
pub mod fdc;
pub mod pia8255;
pub mod pic8259;
pub mod pit8253;
pub mod saa5050;
pub mod sid6581;
pub mod sn76489;
pub mod tms9918;
pub mod via6522;
pub mod vic6569;

pub use ay8910::Ay8910;
pub use cia6526::Cia6526;
pub use crtc6845::Crtc6845;
pub use fdc::{FloppyImage, FloppySector, I8271, Upd765, Wd1770};
pub use pia8255::Pia8255;
pub use pic8259::Pic8259;
pub use pit8253::Pit8253;
pub use saa5050::Saa5050;
pub use sid6581::Sid6581;
pub use sn76489::Sn76489;
pub use tms9918::Tms9918;
pub use via6522::Via6522;
pub use vic6569::Vic6569;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_via6522_timer1_irq() {
        let mut via = Via6522::new();
        via.write(0xE, 0xC0); // Enable T1 IRQ
        via.write(0x4, 0x09); // T1LL = 9
        via.write(0x5, 0x00); // T1LH = 0, starts timer = 9
        via.tick(5);
        assert!(!via.irq_active());
        via.tick(5);
        assert!(via.irq_active());
        let _ = via.read(0x4);
        assert!(!via.irq_active());
    }

    #[test]
    fn test_pit8253_and_pic8259_irq_flow() {
        let mut pit = Pit8253::new();
        pit.write(3, 0x36); // Ch0 LSB+MSB mode 3 binary
        pit.write(0, 100);
        pit.write(0, 0);
        let (edges, _) = pit.tick(120);
        assert!(edges >= 1);

        let mut pic = Pic8259::new();
        pic.write(false, 0x13); // ICW1
        pic.write(true, 0x08);  // ICW2 base 0x08
        pic.write(true, 0x01);  // ICW4
        pic.write(true, 0xFE);  // Unmask IRQ0 only
        pic.raise_irq(0);
        pic.raise_irq(1);
        assert_eq!(pic.acknowledge_irq(), Some(0x08));
        assert_eq!(pic.acknowledge_irq(), None);
        pic.write(false, 0x20); // Non-specific EOI
    }

    #[test]
    fn test_floppy_ssd_and_i8271_read_sector() {
        let mut ssd = vec![0u8; 40 * 10 * 256];
        ssd[0..8].copy_from_slice(b"AcornDFS");
        let img = FloppyImage::from_ssd_or_dsd("test.ssd", &ssd, false);
        let mut fdc = I8271::new();
        fdc.insert_disk(img);
        fdc.write(0, 0x13); // Read multi-sector drive 0
        fdc.write(1, 0);    // Track 0
        fdc.write(1, 0);    // Sector 0
        fdc.write(1, 1);    // 1 sector
        let mut first8 = Vec::new();
        for _ in 0..8 {
            first8.push(fdc.read(2));
        }
        assert_eq!(first8.as_slice(), b"AcornDFS");
    }

    #[test]
    fn test_psg_and_sid_render_audio() {
        let mut sn = Sn76489::new(4_000_000.0);
        sn.write(0x8A);
        sn.write(0x04);
        sn.write(0x90); // Ch0 max volume
        let mut buf = Vec::new();
        sn.render_samples(&mut buf, 64, 44_100.0);
        assert!(buf.iter().any(|&s| s != 0));

        let mut ay = Ay8910::new(1_000_000.0);
        ay.select_register(7);
        ay.write_data(0x3E);
        ay.select_register(8);
        ay.write_data(0x0F);
        ay.select_register(0);
        ay.write_data(50);
        let samples: Vec<i16> = (0..64).map(|_| ay.next_sample(44_100.0)).collect();
        assert!(samples.iter().any(|&s| s != 0));

        let mut sid = Sid6581::new(985_248.0);
        sid.write(0x18, 0x0F);
        sid.write(0x01, 0x10);
        sid.write(0x05, 0x09);
        sid.write(0x06, 0xF0);
        sid.write(0x04, 0x21); // Sawtooth + gate
        let sid_samples: Vec<i16> = (0..64).map(|_| sid.next_sample(44_100.0)).collect();
        assert!(sid_samples.iter().any(|&s| s != 0));
    }

    #[test]
    fn test_vic6569_and_tms9918_and_cia6526() {
        let mut vic = Vic6569::new();
        let mut out = vec![0u32; 640 * 512];
        let color_ram = [1u8; 1024];
        vic.render_screen(&mut out, 640, 512, &color_ram, |_| 0x20);
        assert!(out.iter().any(|&p| p == Vic6569::PALETTE[6]));

        let mut vdp = Tms9918::new();
        vdp.write_ctrl(0x40);
        vdp.write_ctrl(0x81); // Enable display + M1? Bit6 = 0x40
        vdp.write_ctrl(0x07);
        vdp.write_ctrl(0x87); // Backdrop cyan
        vdp.render_screen(&mut out, 640, 512);
        assert!(out.iter().any(|&p| p == Tms9918::PALETTE[7]));

        let mut cia = Cia6526::new();
        cia.write(0xD, 0x81);
        cia.write(0x4, 10);
        cia.write(0x5, 0);
        cia.write(0xE, 0x11);
        cia.tick(15);
        assert!(cia.irq_active());
    }
}
