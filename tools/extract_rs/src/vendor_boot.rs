//! `vendor_boot.img` header parsing (MediaTek only): recover
//! `kernel_phys_load` / `kernel_phys_offset` without root, from the stock
//! image alone.
use std::path::Path;

use crate::error::{ExtractError, Result};

const VNDRBOOT_MAGIC: &[u8; 8] = b"VNDRBOOT";

// No 32 Bit support, Just use 64 Bit text align
const ARM64_TEXT_ALIGN: u64 = 0x8_0000; // 512 KiB

/// Reads `vendor_boot.img`'s `kernel_addr` field and derives
/// `kernel_phys_load`, plus `kernel_phys_offset` when the alignment pattern
/// proves it rather than merely being consistent with it.
pub fn recover_kernel_phys_from_vendor_boot(path: &Path) -> Result<(u64, Option<u64>)> {
    let mut header = [0u8; 20];
    let mut file = std::fs::File::open(path)?;
    std::io::Read::read_exact(&mut file, &mut header)?;
    parse_vendor_boot_header(&header)
}

/// Layout (`vendor_boot_img_hdr_v3/v4` common prefix, little-endian):
/// `magic[8] header_version[4] page_size[4] kernel_addr[4] ...`
fn parse_vendor_boot_header(data: &[u8]) -> Result<(u64, Option<u64>)> {
    if data.len() < 20 || &data[0..8] != VNDRBOOT_MAGIC {
        return Err(ExtractError::new(
            "not a vendor_boot.img (missing VNDRBOOT magic)",
        ));
    }
    let header_version = u32::from_le_bytes(data[8..12].try_into().unwrap());
    let kernel_addr = u32::from_le_bytes(data[16..20].try_into().unwrap()) as u64;
    if header_version < 3 {
        return Err(ExtractError::new(format!(
            "unexpected vendor_boot header_version={header_version} (expected >= 3)"
        )));
    }
    classify(kernel_addr)
}

/// `kernel_phys_load = kernel_addr` (vendor_boot carries no separate base
/// field; it is already the combined physical load address).
///
/// `kernel_phys_offset` (the DRAM base) is only ever returned when the
/// low-20-bit pattern *proves* it, not merely when it is consistent with it:
/// - `low == ARM64_TEXT_ALIGN` unambiguously means `phys_load = dram_base +
///   ARM64_TEXT_ALIGN`, so `phys_offset = phys_load - ARM64_TEXT_ALIGN`.
/// - `low == 0` only tells us `phys_load` is MiB-aligned. That is true both
///   when `phys_load == dram_base` *and* when the kernel is loaded some
///   whole number of MiB above the base (e.g. `phys_load=0x40200000` above
///   `dram_base=0x40000000`). Without a second, independent source (e.g.
///   `/proc/iomem`) we cannot tell those apart, so we return `phys_load`
///   with no offset rather than guessing one.
fn classify(phys_load: u64) -> Result<(u64, Option<u64>)> {
    let low = phys_load & 0xF_FFFF;
    let phys_offset = if low == ARM64_TEXT_ALIGN {
        Some(phys_load - ARM64_TEXT_ALIGN)
    } else if low == 0 {
        None
    } else {
        return Err(ExtractError::new(format!(
            "kernel_phys_load=0x{phys_load:x} matches neither known MediaTek \
             alignment pattern (low 20 bits=0x{low:05x}); refusing to guess \
             kernel_phys_offset"
        )));
    };
    Ok((phys_load, phys_offset))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn header(kernel_addr: u32) -> Vec<u8> {
        let mut data = VNDRBOOT_MAGIC.to_vec();
        data.extend_from_slice(&3u32.to_le_bytes()); // header_version
        data.extend_from_slice(&4096u32.to_le_bytes()); // page_size
        data.extend_from_slice(&kernel_addr.to_le_bytes()); // kernel_addr
        data
    }

    #[test]
    fn case_a_arm64_gki_alignment() {
        // Use an example (Helio G81 Ultra): kernel_addr=0x40080000 proves
        // phys_offset=0x40000000 via the text-align pattern.
        let (load, offset) = parse_vendor_boot_header(&header(0x4008_0000)).unwrap();
        assert_eq!(load, 0x4008_0000);
        assert_eq!(offset, Some(0x4000_0000));
    }

    #[test]
    fn mib_aligned_load_does_not_imply_dram_base() {
        // kernel_addr=0x40000000 is consistent with dram_base=0x40000000
        // (Dimensity 6300), but is equally consistent with a kernel loaded
        // some whole number of MiB above a lower, unseen base. The pattern
        // alone cannot distinguish these, so phys_offset must stay unknown.
        let (load, offset) = parse_vendor_boot_header(&header(0x4000_0000)).unwrap();
        assert_eq!(load, 0x4000_0000);
        assert_eq!(offset, None);
    }

    #[test]
    fn mib_aligned_load_above_base_is_not_mistaken_for_it() {
        // Regression for the exact case the reviewer flagged: a kernel
        // loaded at 0x40200000 above a 0x40000000 base is also MiB-aligned
        // and must NOT yield phys_offset=0x40200000.
        let (load, offset) = parse_vendor_boot_header(&header(0x4020_0000)).unwrap();
        assert_eq!(load, 0x4020_0000);
        assert_eq!(offset, None);
    }

    #[test]
    fn unknown_alignment_is_refused_not_guessed() {
        let err = parse_vendor_boot_header(&header(0x4012_3456)).unwrap_err();
        assert!(err.to_string().contains("refusing to guess"));
    }

    #[test]
    fn wrong_magic_is_rejected() {
        let err = parse_vendor_boot_header(b"ANDROID!\x00\x00\x00\x00\x00\x00\x00\x00").unwrap_err();
        assert!(err.to_string().contains("VNDRBOOT"));
    }

    #[test]
    fn old_header_version_is_rejected() {
        let mut data = VNDRBOOT_MAGIC.to_vec();
        data.extend_from_slice(&2u32.to_le_bytes()); // header_version < 3
        data.extend_from_slice(&4096u32.to_le_bytes());
        data.extend_from_slice(&0x4008_0000u32.to_le_bytes());
        let err = parse_vendor_boot_header(&data).unwrap_err();
        assert!(err.to_string().contains("header_version"));
    }
}
