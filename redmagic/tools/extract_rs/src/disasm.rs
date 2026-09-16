//! ARM64 disassembly adapter over yaxpeax-arm.
//!
//! Produces objdump-style instruction lines so the derivation logic mirrors
//! the original Python/llvm-objdump regex analysis. yaxpeax renders PC-relative
//! operands as `$+0x..`/`$-0x..`; we rewrite them to absolute `0x..` targets.

use regex::Regex;
use yaxpeax_arch::{Decoder, Reader, U8Reader};
use yaxpeax_arm::armv8::a64::{ARMv8, InstDecoder};

use crate::error::{ExtractError, Result};

pub fn disassemble_range(kernel: &[u8], start: usize, stop: usize) -> Result<Vec<String>> {
    if stop <= start || stop > kernel.len() || stop - start > 0x20000 {
        return Err(ExtractError::new(format!(
            "disassembly range invalid: 0x{start:x}..0x{stop:x}"
        )));
    }
    let bytes = &kernel[start..stop];
    let mut reader = U8Reader::new(bytes);
    let decoder = InstDecoder::default();
    let rel_re = Regex::new(r"\$([+-])0x([0-9a-fA-F]+)").unwrap();
    let mut lines = Vec::new();
    loop {
        // ARMv8 words are u8; name the impl explicitly to disambiguate
        // Reader<u64, u8> from the other U8Reader impls. Addresses are file
        // offsets (raw == vaddr), so add the range start.
        let pos = <U8Reader as Reader<u64, u8>>::total_offset(&mut reader) as usize;
        if pos >= bytes.len() {
            break;
        }
        let addr = start + pos;
        let decoded = decoder.decode(&mut reader);
        match decoded {
            Ok(instr) => {
                let text = format!("{instr}");
                // ADRP targets are page-relative: (pc & ~0xfff) + imm<<12.
                let is_adrp = text.trim_start().starts_with("adrp");
                let text = rel_re.replace_all(&text, |caps: &regex::Captures| {
                    let sign = &caps[1];
                    let value = u64::from_str_radix(&caps[2], 16).unwrap_or(0);
                    let base = if is_adrp { addr & !0xFFF } else { addr };
                    let absolute = if sign == "-" {
                        (base as i64).wrapping_sub(value as i64) as u64
                    } else {
                        (base as u64).wrapping_add(value)
                    };
                    format!("0x{absolute:x}")
                });
                lines.push(text.into_owned());
            }
            Err(_) => {
                // Every 32-bit word is a valid A64 instruction, so failures
                // mean a truncated tail; stop like objdump would at a bad byte.
                break;
            }
        }
    }
    Ok(lines)
}

/// Total stack frame: a preindex `stp x29, x30, [sp, #-0x..]!` plus the first
/// `sub sp, sp, #0x..` (preindex-only frames supported). Some prologues split
/// the frame across both forms (e.g. 6.12.58 `do_ipv6_setsockopt`: -0x60 then
/// -0x260), so counting only the `sub` under-counts them.
pub fn first_sp_frame(lines: &[String], name: &str) -> Result<u64> {
    let preindex = Regex::new(r"(?i)\bstp\s+x29,\s*x30,\s*\[sp,\s*#-0x([0-9a-f]+)\]!").unwrap();
    let sub = Regex::new(r"(?i)\bsub\s+sp,\s*sp,\s*#0x([0-9a-f]+)").unwrap();
    let mut total: u64 = 0;
    let mut sub_seen = false;
    for line in lines {
        if let Some(caps) = preindex.captures(line) {
            total += u64::from_str_radix(&caps[1], 16).unwrap();
        }
        if !sub_seen {
            if let Some(caps) = sub.captures(line) {
                total += u64::from_str_radix(&caps[1], 16).unwrap();
                sub_seen = true;
            }
        }
    }
    if total == 0 {
        return Err(ExtractError::new(format!(
            "{name} has no explicit stack frame (`stp [sp,#-imm]!`/`sub sp,sp,#imm`)"
        )));
    }
    Ok(total)
}

pub fn has_direct_call(lines: &[String], target: u64) -> bool {
    let re = Regex::new(&format!(r"(?i)\bbl\s+0x{target:x}\b")).unwrap();
    lines.iter().any(|line| re.is_match(line))
}

fn is_sp_sub_imm(line: &str) -> bool {
    Regex::new(r"(?i)\bsub\s+sp,\s*sp,\s*#0x[0-9a-f]+")
        .unwrap()
        .is_match(line)
}

fn is_sp_post_index_restore(line: &str) -> bool {
    Regex::new(r"(?i)\[\s*sp\],\s*#0x[0-9a-f]+")
        .unwrap()
        .is_match(line)
}

fn is_sp_add_or_sub(line: &str) -> bool {
    Regex::new(r"(?i)\b(?:add|sub)\s+sp,\s*sp,")
        .unwrap()
        .is_match(line)
}

fn is_ret(line: &str) -> bool {
    Regex::new(r"(?i)\bd65f03c0\b|\bret\b")
        .unwrap()
        .is_match(line)
}

/// Prove the first explicit frame allocation is still live at one anchor.
pub fn validate_frame_live_at(lines: &[String], anchor: &Regex, name: &str) -> Result<()> {
    let anchor_indices: Vec<usize> = lines
        .iter()
        .enumerate()
        .filter(|(_, line)| anchor.is_match(line))
        .map(|(index, _)| index)
        .collect();
    if anchor_indices.len() != 1 {
        return Err(ExtractError::new(format!(
            "{name} frame anchor not unique: {}",
            anchor_indices.len()
        )));
    }
    let anchor_index = anchor_indices[0];
    let subs: Vec<usize> = lines[..anchor_index]
        .iter()
        .enumerate()
        .filter(|(_, line)| is_sp_sub_imm(line))
        .map(|(index, _)| index)
        .collect();
    if subs.len() != 1 {
        return Err(ExtractError::new(format!(
            "{name} has {} SP frames before anchor",
            subs.len()
        )));
    }
    for index in subs[0] + 1..anchor_index {
        let line = &lines[index];
        if is_sp_post_index_restore(line) {
            return Err(ExtractError::new(format!(
                "{name} restores SP post-index before anchor"
            )));
        }
        if is_sp_add_or_sub(line) {
            let rest = &lines[index + 1..anchor_index];
            if !rest.iter().any(|tail| is_ret(tail)) {
                return Err(ExtractError::new(format!(
                    "{name} adjusts SP again before anchor"
                )));
            }
        }
    }
    Ok(())
}

/// adrp `register, page` followed within 3 lines by `add register, register, #off`.
pub fn materialized_address(lines: &[String], register: &str, address: u64) -> bool {
    let page = address & !0xFFF;
    let page_off = address & 0xFFF;
    let adrp_re = Regex::new(&format!(r"(?i)\badrp\s+{register},\s*0x{page:x}\b")).unwrap();
    let add_re = Regex::new(&format!(
        r"(?i)\badd\s+{register},\s*{register},\s*#0x{page_off:x}\b"
    ))
    .unwrap();
    for (index, line) in lines.iter().enumerate() {
        if !adrp_re.is_match(line) {
            continue;
        }
        let nearby = &lines[index + 1..(index + 4).min(lines.len())];
        if nearby.iter().any(|line| add_re.is_match(line)) {
            return true;
        }
    }
    false
}

pub fn add_sp_immediates(lines: &[String]) -> Vec<(String, u64)> {
    let re = Regex::new(r"(?i)\badd\s+(x\d+),\s*sp,\s*#0x([0-9a-f]+)").unwrap();
    let mut out = Vec::new();
    for line in lines {
        for caps in re.captures_iter(line) {
            let reg = caps[1].to_ascii_lowercase();
            let imm = u64::from_str_radix(&caps[2], 16).unwrap();
            out.push((reg, imm));
        }
    }
    out
}

pub fn cmp_immediates(lines: &[String]) -> Vec<u64> {
    let re = Regex::new(r"(?i)\bcmp\s+x\d+,\s*#0x([0-9a-f]+)").unwrap();
    let mut out = Vec::new();
    for line in lines {
        for caps in re.captures_iter(line) {
            out.push(u64::from_str_radix(&caps[1], 16).unwrap());
        }
    }
    out
}

pub fn bl_targets(lines: &[String]) -> Vec<u64> {
    let re = Regex::new(r"(?i)\bbl\s+0x([0-9a-f]+)\b").unwrap();
    let mut out = Vec::new();
    for line in lines {
        for caps in re.captures_iter(line) {
            out.push(u64::from_str_radix(&caps[1], 16).unwrap());
        }
    }
    out
}

/// Parse a line as (mnemonic, operand string) for simple pattern checks.
pub fn split_line(line: &str) -> (String, String) {
    let trimmed = line.trim();
    match trimmed.find(char::is_whitespace) {
        Some(index) => (
            trimmed[..index].to_string(),
            trimmed[index..].trim().to_string(),
        ),
        None => (trimmed.to_string(), String::new()),
    }
}

pub fn is_mov_x1(line: &str) -> Option<String> {
    let re = Regex::new(r"(?i)^mov\s+(x\d+),\s*x1\s*$").unwrap();
    let caps = re.captures(line)?;
    Some(caps[1].to_ascii_lowercase())
}

pub fn is_mov_w0_wzr(line: &str) -> bool {
    Regex::new(r"(?i)^mov\s+w0,\s*wzr\s*$")
        .unwrap()
        .is_match(line)
}

/// Alias for ARMv8 decoder used by callers that need a decoder type.
pub type Armv8 = ARMv8;
