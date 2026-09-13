//! Output rendering for extracted kernel metadata.

use serde_json::{Value, json};
use std::collections::BTreeMap;

use crate::error::{ExtractError, Result};
use crate::symbols::{OPTIONAL_SYMBOLS, STRUCT_FIELDS, SYMBOLS};

pub const MTK_DEFAULT_PHYS_LOAD: u64 = 0x8000_0000;
pub const QC_PHYS_LOAD_6_6: u64 = 0xA800_0000;
pub const QC_PHYS_LOAD_6_1: u64 = 0xA800_0000;
pub const QC_PHYS_LOAD_6_12: u64 = 0xC780_0000;

/// Python insertion order of resolve_symbols(): header output matches the
/// Python tool byte-for-byte.
pub fn symbol_render_order() -> Vec<&'static str> {
    let mut keys: Vec<&'static str> = Vec::new();
    for (name, _) in SYMBOLS {
        keys.push(*name);
    }
    keys.push("off_slide_loggers_0_1");
    keys
}

/// Python insertion order of resolve_structs(): struct_fields output in the
/// C header matches the Python tool byte-for-byte.
fn struct_render_order() -> Vec<&'static str> {
    let mut keys: Vec<&'static str> = Vec::new();
    for (_, fields) in STRUCT_FIELDS {
        for (macro_name, _) in *fields {
            keys.push(*macro_name);
        }
    }
    keys.push("struct_page_size");
    keys.push("struct_page_compound_head");
    keys.push("struct_page_type");
    keys.push("struct_slab_cache");
    keys.push("struct_mm_struct");
    keys
}

pub fn phys_needs_override(release: Option<&str>, phys: Option<u64>) -> bool {
    let Some(phys) = phys else {
        return false;
    };
    if phys == MTK_DEFAULT_PHYS_LOAD {
        return false;
    }
    let default = match crate::symbols::kernel_struct_macro(release) {
        Some("STRUCT_OFFSETS_6_12") => QC_PHYS_LOAD_6_12,
        Some("STRUCT_OFFSETS_6_1") => QC_PHYS_LOAD_6_1,
        _ => QC_PHYS_LOAD_6_6,
    };
    phys != default
}

pub fn pselect_waiter_shift_for(release: Option<&str>) -> i64 {
    match crate::symbols::kernel_struct_macro(release) {
        Some("STRUCT_OFFSETS_6_12") => 0,
        // android14-6.1 compiles its fd_set words one qword later than
        // 6.6; the committed tables all measure 1.
        Some("STRUCT_OFFSETS_6_1") => 1,
        _ => -2,
    }
}

pub fn validate_kernel_phys_load(release: Option<&str>, phys: Option<u64>, mtk: bool) -> bool {
    let Some(phys) = phys else {
        return false;
    };
    let expected = if mtk {
        MTK_DEFAULT_PHYS_LOAD
    } else {
        match crate::symbols::kernel_struct_macro(release) {
            Some("STRUCT_OFFSETS_6_12") => QC_PHYS_LOAD_6_12,
            Some("STRUCT_OFFSETS_6_1") => QC_PHYS_LOAD_6_1,
            _ => QC_PHYS_LOAD_6_6,
        }
    };
    if phys == expected {
        return false;
    }
    let note = "the entry will carry it as an explicit override";
    eprintln!(
        "warning: kernel_phys_load=0x{phys:x} does not match the {} default 0x{expected:x}; {note}",
        if mtk { "MediaTek" } else { "Qualcomm" }
    );
    true
}

pub fn render_device(
    release: &str,
    symbols: &BTreeMap<String, Option<u64>>,
    structs: &BTreeMap<String, Option<u32>>,
    phys: Option<u64>,
    pselect_shift: i64,
) -> String {
    let mut lines = vec![format!("/* {release} */"), String::new()];
    lines.push("OFFSETS_ENTRY(".to_string());
    lines.push(format!("    \"{release}\","));
    lines.push(format!(
        "    {},",
        // unverified kernels render with the 6.6 layout as a testing start;
        // the extractor warns whenever it falls back
        crate::symbols::kernel_struct_macro(Some(release)).unwrap_or("STRUCT_OFFSETS_6_6")
    ));
    if phys_needs_override(Some(release), phys) {
        lines.push(format!("    .kernel_phys_load = 0x{:x},", phys.unwrap()));
    }
    // 6.1 entries get their mm_struct_sz=0x400 stride from the
    // STRUCT_OFFSETS_6_1 macro itself; nothing extra to emit here.
    lines.push(format!("    .pselect_waiter_shift = {pselect_shift},"));
    for key in symbol_render_order() {
        if let Some(value) = symbols.get(key).copied().flatten() {
            lines.push(format!("    .{key} = 0x{value:08x},"));
        }
    }
    lines.push("),".to_string());
    let mut reference: Vec<(&str, u32)> = Vec::new();
    for key in struct_render_order() {
        if key.starts_with("struct_page") || key == "struct_slab_cache" || key == "struct_mm_struct"
        {
            if let Some(value) = structs.get(key).copied().flatten() {
                reference.push((key, value));
            }
        }
    }
    if !reference.is_empty() {
        lines.push(String::new());
        lines.push("/* BTF reference (runtime uses target.h defaults): */".to_string());
        for (key, value) in &reference {
            lines.push(format!(
                "/* #define {} 0x{:X} */",
                key.to_uppercase(),
                value
            ));
        }
    }
    lines.join("\n") + "\n"
}

pub fn render_c(
    release: Option<&str>,
    name: &str,
    symbols: &BTreeMap<String, Option<u64>>,
    structs: &BTreeMap<String, Option<u32>>,
    phys: Option<u64>,
    pselect_shift: i64,
) -> String {
    let label = release.unwrap_or(name);
    let mut lines = vec![
        format!("/* Generated offsets for {label}. */"),
        String::new(),
    ];
    lines.push("#define STRUCT_OFFSETS_EXTRACTED \\".to_string());
    let task_keys = [
        "task_prio",
        "task_normal_prio",
        "task_sched_task_group",
        "task_pi_lock",
        "task_pi_waiters",
        "task_pi_top_task",
        "task_pi_blocked_on",
        "task_pid",
        "task_tgid",
        "task_atomic_flags",
        "task_real_cred",
        "task_cred",
        "task_comm",
        "task_tasks",
        "task_seccomp",
    ];
    let present: Vec<(String, u32)> = task_keys
        .iter()
        .filter_map(|key| {
            structs
                .get(*key)
                .copied()
                .flatten()
                .map(|value| ((*key).to_string(), value))
        })
        .collect();
    for (index, (key, value)) in present.iter().enumerate() {
        let suffix = if index + 1 < present.len() { " \\" } else { "" };
        lines.push(format!("  .{key} = 0x{value:X},{suffix}"));
    }
    lines.push(String::new());
    let macro_name = crate::symbols::kernel_struct_macro(release);
    lines.push(format!("OFFSETS_ENTRY(\"{label}\","));
    lines.push(format!(
        "  {},",
        // unverified kernels render with the 6.6 layout as a testing start;
        // the extractor warns whenever it falls back
        macro_name.unwrap_or("STRUCT_OFFSETS_6_6")
    ));
    if phys_needs_override(release, phys) {
        lines.push(format!("  .kernel_phys_load=0x{:X},", phys.unwrap()));
    }
    lines.push(format!("  .pselect_waiter_shift={pselect_shift},"));
    if macro_name == Some("STRUCT_OFFSETS_6_1") {
        // spell the layout fields out so a manually registered header does
        // not depend on the selector macro carrying them
        lines.push("  .compact_waiter=1,".to_string());
        lines.push("  .mm_struct_sz=0x400,".to_string());
    }
    for key in symbol_render_order() {
        if let Some(value) = symbols.get(key).copied().flatten() {
            lines.push(format!("  .{key}=0x{value:08X},"));
        }
    }
    lines.push("),".to_string());
    lines.push(String::new());
    lines.push("/* BTF fields not stored in kernel_offsets: */".to_string());
    for key in struct_render_order() {
        if key.starts_with("task_") {
            continue;
        }
        if let Some(value) = structs.get(key).copied().flatten() {
            lines.push(format!("#define {} 0x{:X}", key.to_uppercase(), value));
        }
    }
    lines.join("\n")
}

pub fn build_report(
    release: Option<&str>,
    base: u64,
    phys: Option<u64>,
    symbols: &BTreeMap<String, Option<u64>>,
    structs: &BTreeMap<String, Option<u32>>,
    btf_size: usize,
    pselect_shift: i64,
) -> Value {
    let symbol_json: BTreeMap<String, Value> = symbols
        .iter()
        .map(|(key, value)| {
            (
                key.clone(),
                match value {
                    Some(v) => json!(v),
                    None => Value::Null,
                },
            )
        })
        .collect();
    let struct_json: BTreeMap<String, Value> = structs
        .iter()
        .map(|(key, value)| {
            (
                key.clone(),
                match value {
                    Some(v) => json!(v),
                    None => Value::Null,
                },
            )
        })
        .collect();
    let mut report = json!({
        "release": release,
        "kimage_text_base": base,
        "kernel_phys_load": phys,
        "pselect_waiter_shift": pselect_shift,
        "symbols": symbol_json,
        "struct_fields": struct_json,
        "btf_size": btf_size,
    });
    let kernel_major = release
        .and_then(|value| value.split('.').next())
        .and_then(|value| value.parse::<u32>().ok())
        .unwrap_or(0);
    report["kernel_major"] = json!(kernel_major);
    report["kernelsnitch_collisions"] = json!(4);
    if kernel_major == 5 {
        report["cred_copy_size"] = json!(0xb0);
        report["cred_usage_value"] = json!(0x100);
        report["cred_caps_offset"] = json!(0x30);
        report["cred_caps_count"] = json!(3);
        report["cred_caps_value"] = json!(0x000001ffffffffff_u64);
    } else {
        report["cred_copy_size"] = json!(0x88);
        report["cred_usage_value"] = json!(1);
        report["cred_caps_offset"] = json!(0x30);
        report["cred_caps_count"] = json!(5);
        report["cred_caps_value"] = json!(u64::MAX);
    }
    if crate::symbols::kernel_struct_macro(release) == Some("STRUCT_OFFSETS_6_1") {
        // 0x400 is the device SLUB stride, not the BTF 0x3c0
        report["compact_waiter"] = json!(1);
        report["mm_struct_sz"] = json!(0x400);
    }
    report
}

pub fn require_fields(
    values: &BTreeMap<String, Option<u64>>,
    optional: &BTreeSet<&str>,
) -> Result<()> {
    let missing: Vec<String> = values
        .iter()
        .filter(|(name, value)| value.is_none() && !optional.contains(name.as_str()))
        .map(|(name, _)| name.clone())
        .collect();
    if !missing.is_empty() {
        return Err(ExtractError::unsupported(format!(
            "missing required values: {}",
            missing.join(", ")
        )));
    }
    Ok(())
}

use std::collections::BTreeSet;

pub fn optional_symbols() -> BTreeSet<&'static str> {
    OPTIONAL_SYMBOLS.iter().copied().collect()
}

pub fn task_keys_list() -> &'static [&'static str] {
    &[
        "task_prio",
        "task_normal_prio",
        "task_sched_task_group",
        "task_pi_lock",
        "task_pi_waiters",
        "task_pi_top_task",
        "task_pi_blocked_on",
        "task_pid",
        "task_tgid",
        "task_atomic_flags",
        "task_real_cred",
        "task_cred",
        "task_comm",
        "task_tasks",
        "task_seccomp",
    ]
}

pub fn struct_fields_reference()
-> &'static [(&'static str, &'static [(&'static str, &'static str)])] {
    STRUCT_FIELDS
}

#[cfg(test)]
mod tests {
    use super::{pselect_waiter_shift_for, render_c};
    use std::collections::BTreeMap;

    #[test]
    fn render_c_carries_the_layout_selector_and_6_1_scalars() {
        let symbols: BTreeMap<String, Option<u64>> = BTreeMap::new();
        let structs: BTreeMap<String, Option<u32>> = BTreeMap::new();
        let out = render_c(
            Some("6.1.118-android14-11-gca0ef6d17716-ab13624819"),
            "x",
            &symbols,
            &structs,
            None,
            1,
        );
        assert!(out.contains("STRUCT_OFFSETS_6_1"));
        assert!(out.contains(".compact_waiter=1"));
        assert!(out.contains(".mm_struct_sz=0x400"));

        let out66 = render_c(
            Some("6.6.92-android15-8"),
            "x",
            &symbols,
            &structs,
            None,
            -2,
        );
        assert!(out66.contains("STRUCT_OFFSETS_6_6"));
        assert!(!out66.contains("compact_waiter"));
    }

    #[test]
    fn pselect_waiter_shift_matches_the_committed_tables() {
        assert_eq!(
            pselect_waiter_shift_for(Some("6.1.118-android14-11-gca0ef6d17716-ab13624819")),
            1
        );
        assert_eq!(pselect_waiter_shift_for(Some("6.6.92-android15-8")), -2);
        assert_eq!(pselect_waiter_shift_for(Some("6.12.30-android16-0")), 0);
        assert_eq!(pselect_waiter_shift_for(None), -2);
    }
}
