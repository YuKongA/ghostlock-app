//! Boot image parsing: Android boot v3/v4, raw arm64 Image, gzip, LZ4 legacy.

use std::path::Path;

use flate2::read::GzDecoder;
use std::io::Read;

use crate::error::{ExtractError, Result};

pub const ANDROID_MAGIC: &[u8; 8] = b"ANDROID!";
pub const BTF_MAGIC: u16 = 0xEB9F;
pub const PAGE_SIZE: usize = 4096;
pub const LZ4_LEGACY_MAGIC: &[u8; 4] = b"\x02\x21\x4c\x18";
pub const LZ4_MAX_IMAGE: usize = 0x1000_0000; // 256 MiB decompressed arm64 Image bound

// _text VA encodes the MTK DRAM base (text_offset=0); derive phys from
// _text - MTK_VADDR_BASE; MTK_DEFAULT_PHYS_LOAD is the fallback.
pub const MTK_VADDR_BASE: u64 = 0xFFFF_FFC0_0000_0000;
pub const MTK_DEFAULT_PHYS_LOAD: u64 = 0x8000_0000;
pub const QC_PHYS_LOAD_6_6: u64 = 0xA800_0000;
pub const QC_PHYS_LOAD_6_12: u64 = 0xC780_0000;

/// MTK boot kernels are stored as LZ4 legacy frames: a 4-byte magic followed
/// by size-prefixed blocks, ending once a block decompresses under 8 MiB.
pub fn decompress_lz4_legacy(payload: &[u8]) -> Result<Vec<u8>> {
    let out = decompress_lz4_legacy_frame(payload)?;
    if out.len() < 0x3C || &out[0..2] != b"MZ" || &out[0x38..0x3C] != b"ARM\x64" {
        return Err(ExtractError::new(
            "LZ4 decompression did not yield an arm64 Image",
        ));
    }
    Ok(out)
}

fn decompress_lz4_legacy_frame(payload: &[u8]) -> Result<Vec<u8>> {
    const MAX_BLOCK: usize = 8 * 1024 * 1024;
    if payload.len() <= 8 || &payload[..4] != LZ4_LEGACY_MAGIC {
        return Err(ExtractError::new("LZ4 kernel payload too short"));
    }
    let mut out = Vec::new();
    let mut ip = 4usize;
    while ip + 4 <= payload.len() {
        let block_len = u32::from_le_bytes(payload[ip..ip + 4].try_into().unwrap()) as usize;
        ip += 4;
        if block_len == 0 {
            break; // trailing zero padding
        }
        let end = match ip.checked_add(block_len) {
            Some(end) if end <= payload.len() => end,
            _ => return Err(ExtractError::new("truncated LZ4 legacy block")),
        };
        let block_out = decompress_lz4_block(&payload[ip..end], MAX_BLOCK).map_err(|err| {
            ExtractError::new(format!("invalid LZ4-compressed kernel payload: {err}"))
        })?;
        ip = end;
        if out.len().saturating_add(block_out.len()) > LZ4_MAX_IMAGE {
            return Err(ExtractError::new("LZ4 output exceeds size bound"));
        }
        out.extend_from_slice(&block_out);
        if block_out.len() < MAX_BLOCK {
            break;
        }
    }
    Ok(out)
}

/// Decode one LZ4 block like the reference C decoder; lz4_flex rejects the
/// offset-0 matches MTK legacy streams use as zero padding.
fn decompress_lz4_block(input: &[u8], max_output: usize) -> Result<Vec<u8>> {
    let mut out = Vec::with_capacity(input.len().min(max_output));
    let mut ip = 0usize;
    while ip < input.len() {
        let token = input[ip];
        ip += 1;
        let mut lit = (token >> 4) as usize;
        if lit == 15 {
            loop {
                if ip >= input.len() {
                    return Err(ExtractError::new("truncated LZ4 literal length"));
                }
                let b = input[ip];
                ip += 1;
                lit += b as usize;
                if b != 255 {
                    break;
                }
            }
        }
        if ip + lit > input.len() {
            return Err(ExtractError::new("truncated LZ4 literals"));
        }
        out.extend_from_slice(&input[ip..ip + lit]);
        ip += lit;
        if ip >= input.len() {
            break; // final literals-only sequence
        }
        if ip + 2 > input.len() {
            return Err(ExtractError::new("truncated LZ4 match offset"));
        }
        let off = u16::from_le_bytes([input[ip], input[ip + 1]]) as usize;
        ip += 2;
        let mut mlen = (token & 0x0F) as usize + 4;
        if mlen == 19 {
            loop {
                if ip >= input.len() {
                    return Err(ExtractError::new("truncated LZ4 match length"));
                }
                let b = input[ip];
                ip += 1;
                mlen += b as usize;
                if b != 255 {
                    break;
                }
            }
        }
        if off == 0 {
            out.resize(out.len() + mlen, 0);
        } else {
            if off > out.len() {
                return Err(ExtractError::new("LZ4 match before start of output"));
            }
            let start = out.len() - off;
            for i in 0..mlen {
                let b = out[start + i];
                out.push(b);
            }
        }
        if out.len() > max_output {
            return Err(ExtractError::new("LZ4 output exceeds size bound"));
        }
    }
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn lz4_round_trip() {
        let samples: Vec<Vec<u8>> = vec![
            b"".to_vec(),
            b"hello world".to_vec(),
            vec![0u8; 4096],
            (0..65536u32).map(|i| (i % 251) as u8).collect(),
        ];
        for sample in &samples {
            let out = decompress_lz4_block(&lz4_flex::block::compress(sample), 1 << 20)
                .expect("valid block must decode");
            assert_eq!(&out, sample);
        }
    }

    #[test]
    fn lz4_offset_zero_matches_reference() {
        // 0x0e token: no literals, match length 18, offset 0 -> 18 zero bytes;
        // the stream then ends inside a literals-only sequence (7 zero bytes),
        // matching the reference decoder's output.
        let data = [0x0e, 0x00, 0x00, 0x70, 0, 0, 0, 0, 0, 0, 0];
        let out = decompress_lz4_block(&data, 256).expect("reference-compatible decode");
        assert_eq!(out.len(), 25);
        assert!(out.iter().all(|&b| b == 0));
    }

    #[test]
    fn lz4_legacy_chunked_stream() {
        // Regression: legacy frames must be decoded block by block; a single
        // pass used to break at the 8 MiB block boundary.
        let sample1: Vec<u8> = (0..(8 << 20)).map(|i| (i % 251) as u8).collect();
        let sample2: Vec<u8> = (0..(1 << 20)).map(|i| (i * 7 % 251) as u8).collect();
        let mut frame = LZ4_LEGACY_MAGIC.to_vec();
        for sample in [&sample1, &sample2] {
            let block = lz4_flex::block::compress(sample);
            frame.extend_from_slice(&(block.len() as u32).to_le_bytes());
            frame.extend_from_slice(&block);
        }
        frame.extend_from_slice(&[0u8; 8]); // trailing zero padding
        let mut expected = sample1;
        expected.extend_from_slice(&sample2);
        let out = decompress_lz4_legacy_frame(&frame).expect("chunked frame must decode");
        assert_eq!(out, expected);
    }
}

pub struct BootImage {
    pub kernel: Vec<u8>,
    pub mtk_lz4: bool,
    pub mtk_gzip: bool,
}

impl BootImage {
    pub fn load(path: &Path) -> Result<BootImage> {
        let mut raw = std::fs::read(path)?;
        if raw.len() >= 8 && &raw[..8] == ANDROID_MAGIC {
            return Self::from_android_boot(raw);
        }
        if raw.len() >= 3 && raw[0] == 0x1f && raw[1] == 0x8b && raw[2] == 0x08 {
            raw = gunzip(&raw)?;
        }
        if raw.len() < 64 || &raw[56..60] != b"ARM\x64" {
            return Err(ExtractError::new(
                "input is not an Android boot image or arm64 Image",
            ));
        }
        Ok(BootImage {
            kernel: raw,
            mtk_lz4: false,
            mtk_gzip: false,
        })
    }

    fn from_android_boot(raw: Vec<u8>) -> Result<BootImage> {
        if raw.len() < 44 {
            return Err(ExtractError::new("truncated Android boot header"));
        }
        let kernel_size = u32::from_le_bytes(raw[8..12].try_into().unwrap()) as usize;
        let header_size = u32::from_le_bytes(raw[20..24].try_into().unwrap()) as usize;
        let version = u32::from_le_bytes(raw[40..44].try_into().unwrap());
        if !(version == 3 || version == 4) {
            return Err(ExtractError::new(format!(
                "unsupported boot header version {version}"
            )));
        }
        let start = align(header_size, PAGE_SIZE);
        let end = start + kernel_size;
        if end > raw.len() {
            return Err(ExtractError::new("kernel payload exceeds boot image"));
        }
        let kernel = &raw[start..end];
        if kernel.len() >= 4 && &kernel[..4] == LZ4_LEGACY_MAGIC {
            let kernel = decompress_lz4_legacy(kernel)?;
            return Ok(BootImage {
                kernel,
                mtk_lz4: true,
                mtk_gzip: false,
            });
        }
        if kernel.len() >= 3 && kernel[0] == 0x1f && kernel[1] == 0x8b && kernel[2] == 0x08 {
            let kernel = gunzip(kernel)?;
            return Ok(BootImage {
                kernel,
                mtk_lz4: false,
                mtk_gzip: true,
            });
        }
        Ok(BootImage {
            kernel: kernel.to_vec(),
            mtk_lz4: false,
            mtk_gzip: false,
        })
    }

    pub fn release(&self) -> Option<String> {
        let needle = b"Linux version ";
        let pos = find_subslice(&self.kernel, needle)?;
        let rest = &self.kernel[pos + needle.len()..];
        let end = rest
            .iter()
            .position(|b| *b == 0 || *b == b'\r' || *b == b'\n' || *b == b' ')
            .unwrap_or(rest.len());
        Some(String::from_utf8_lossy(&rest[..end]).into_owned())
    }

    /// Locate the embedded BTF blob (largest valid candidate), returning its
    /// file offset inside the kernel together with the blob bytes. The strings
    /// table is extended over a printable tail when vendors overrun str_len.
    pub fn embedded_btf_at(&self) -> Option<(usize, Vec<u8>)> {
        let mut signature = Vec::with_capacity(8);
        signature.extend_from_slice(&BTF_MAGIC.to_le_bytes());
        signature.extend_from_slice(&[1, 0]);
        signature.extend_from_slice(&24u32.to_le_bytes());

        let mut candidates: Vec<(usize, Vec<u8>)> = Vec::new();
        let mut cursor = 0usize;
        while let Some(pos) = find_subslice_from(&self.kernel, &signature, cursor) {
            cursor = pos + 1;
            if pos + 24 > self.kernel.len() {
                continue;
            }
            let magic = u16::from_le_bytes(self.kernel[pos..pos + 2].try_into().unwrap());
            let version = self.kernel[pos + 2];
            let hdr_len =
                u32::from_le_bytes(self.kernel[pos + 4..pos + 8].try_into().unwrap()) as usize;
            let type_off =
                u32::from_le_bytes(self.kernel[pos + 8..pos + 12].try_into().unwrap()) as usize;
            let type_len =
                u32::from_le_bytes(self.kernel[pos + 12..pos + 16].try_into().unwrap()) as usize;
            let str_off =
                u32::from_le_bytes(self.kernel[pos + 16..pos + 20].try_into().unwrap()) as usize;
            let str_len =
                u32::from_le_bytes(self.kernel[pos + 20..pos + 24].try_into().unwrap()) as usize;
            if magic != BTF_MAGIC || version != 1 || hdr_len < 24 {
                continue;
            }
            let mut total = hdr_len + (type_off + type_len).max(str_off + str_len);
            if total <= hdr_len || pos + total > self.kernel.len() {
                continue;
            }
            let strings = pos + hdr_len + str_off;
            if str_len != 0 && self.kernel[strings] == 0 {
                // Some vendors append strings past the declared str_len (only
                // when strings are the last section); extend over the
                // NUL-terminated printable tail to avoid cutting mid-string.
                if str_off + str_len >= type_off + type_len {
                    let str_end = strings + str_len;
                    let mut scan = str_end;
                    while scan < self.kernel.len() && scan - str_end < (1 << 20) {
                        let byte = self.kernel[scan];
                        if byte == 0 {
                            // Skip a NUL only if printable text follows; a
                            // lone NUL ends the table.
                            if scan + 1 < self.kernel.len()
                                && (0x20..0x7F).contains(&self.kernel[scan + 1])
                            {
                                scan += 1;
                                continue;
                            }
                            break;
                        }
                        if (0x20..0x7F).contains(&byte) {
                            let window_end = (scan + 256).min(self.kernel.len());
                            let nxt = find_subslice_in(&self.kernel, &[0], scan, window_end);
                            match nxt {
                                Some(nxt) => {
                                    scan = nxt;
                                    continue;
                                }
                                None => break,
                            }
                        }
                        break;
                    }
                    if scan > str_end {
                        eprintln!(
                            "warning: BTF strings table extends past the declared \
                             str_len by 0x{:x} bytes",
                            scan - str_end
                        );
                    }
                    total = total.max(scan - pos);
                }
                candidates.push((pos, self.kernel[pos..pos + total].to_vec()));
            }
        }
        candidates.into_iter().max_by_key(|(_, blob)| blob.len())
    }

    /// Locate the embedded BTF blob (largest valid candidate).
    pub fn embedded_btf(&self) -> Option<Vec<u8>> {
        self.embedded_btf_at().map(|(_, blob)| blob)
    }
}

pub fn align(value: usize, size: usize) -> usize {
    (value + size - 1) & !(size - 1)
}

fn gunzip(data: &[u8]) -> Result<Vec<u8>> {
    let mut decoder = GzDecoder::new(data);
    let mut out = Vec::new();
    decoder
        .read_to_end(&mut out)
        .map_err(|err| ExtractError::new(format!("invalid gzip payload: {err}")))?;
    Ok(out)
}

pub fn find_subslice(haystack: &[u8], needle: &[u8]) -> Option<usize> {
    find_subslice_from(haystack, needle, 0)
}

pub fn find_subslice_from(haystack: &[u8], needle: &[u8], from: usize) -> Option<usize> {
    if needle.is_empty() || haystack.len() < needle.len() {
        return None;
    }
    let from = from.min(haystack.len());
    (from..=haystack.len() - needle.len()).find(|&i| &haystack[i..i + needle.len()] == needle)
}

pub fn find_subslice_in(haystack: &[u8], needle: &[u8], from: usize, to: usize) -> Option<usize> {
    if needle.is_empty() {
        return Some(from);
    }
    let from = from.min(haystack.len());
    let to = to.min(haystack.len());
    if to < needle.len() {
        return None;
    }
    (from..=to - needle.len()).find(|&i| &haystack[i..i + needle.len()] == needle)
}
