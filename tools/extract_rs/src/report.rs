//! Output rendering for extracted kernel metadata.

use serde_json::{Value, json};
use std::collections::BTreeMap;

use crate::derive::Cred5x;
use crate::error::{ExtractError, Result};
use crate::symbols::{OPTIONAL_SYMBOLS, kernel_device_geometry_verified, kernel_layout_verified};

pub fn pselect_waiter_shift_for(release: Option<&str>) -> Option<i64> {
    if !kernel_layout_verified(release) {
        return None;
    }
    match crate::symbols::kernel_struct_macro(release) {
        Some("STRUCT_OFFSETS_6_12") => Some(0),
        // android14-6.1 compiles its fd_set words one qword later than
        // 6.6; the committed tables all measure 1.
        Some("STRUCT_OFFSETS_6_1") => Some(1),
        Some("STRUCT_OFFSETS_6_6") => Some(-2),
        _ => None,
    }
}

pub fn build_report(
    release: Option<&str>,
    base: u64,
    phys: Option<u64>,
    symbols: &BTreeMap<String, Option<u64>>,
    structs: &BTreeMap<String, Option<u32>>,
    btf_size: usize,
    pselect_shift: Option<i64>,
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
    if kernel_layout_verified(release)
        && crate::symbols::kernel_struct_macro(release) == Some("STRUCT_OFFSETS_6_1")
    {
        // 0x400 is the device SLUB stride, not the BTF 0x3c0
        report["compact_waiter"] = json!(1);
        report["mm_struct_sz"] = json!(0x400);
    }
    report
}

/// `task_struct` keys in the bundled profiles' order.
const CONF_TASK_FIELDS: &[(&str, &str)] = &[
    ("task_prio", "prio"),
    ("task_normal_prio", "normal_prio"),
    ("task_sched_task_group", "sched_task_group"),
    ("task_pi_lock", "pi_lock"),
    ("task_pi_waiters", "pi_waiters"),
    ("task_pi_top_task", "pi_top_task"),
    ("task_pi_blocked_on", "pi_blocked_on"),
    ("task_pid", "pid"),
    ("task_tgid", "tgid"),
    ("task_atomic_flags", "atomic_flags"),
    ("task_real_cred", "real_cred"),
    ("task_cred", "cred"),
    ("task_comm", "comm"),
    ("task_tasks", "tasks"),
    ("task_seccomp", "seccomp"),
];

/// Extra `offset.*` keys the extractor resolves outside the `off_*` symbol
/// table (kallsyms-only symbols). Both keep their bundled-profile position.
#[derive(Debug, Clone, Default)]
pub struct ConfExtraOffsets {
    pub empty_zero_page: Option<u64>,
    pub mcast_fake_bss: Option<u64>,
}

/// The shared 6.x credential template (`credential-6x.conf`), in the bundled
/// order. The flatten rule inlines it instead of an include line; the values
/// stay pinned to that asset by `BuiltinProfilesTest`.
pub fn conf_cred_6x() -> Vec<(String, String)> {
    [
        ("caps_offset", 48),
        ("copy_size", 136),
        ("usage_value", 1),
        ("caps_count", 5),
        ("caps_value", -1),
    ]
    .into_iter()
    .map(|(key, value)| (key.to_string(), value.to_string()))
    .collect()
}

/// The 5.x credential template from the derived `init_cred` values, in the
/// bundled profile order. Reference images are pre-KASLR kernel VAs rendered
/// as signed decimals (the profile's spelling).
pub fn conf_cred_5x(cred: &Cred5x, copy_size: u32) -> Vec<(String, String)> {
    let mut entries: Vec<(String, String)> = vec![
        ("caps_offset".to_string(), cred.caps_offset.to_string()),
        ("copy_size".to_string(), copy_size.to_string()),
        (
            "usage_value".to_string(),
            crate::derive::CRED_5X_USAGE_VALUE.to_string(),
        ),
        ("caps_count".to_string(), cred.caps_count.to_string()),
        ("caps_value".to_string(), cred.caps_value.to_string()),
    ];
    for (index, (offset, _)) in cred.refs.iter().enumerate() {
        entries.push((format!("ref{index}_offset"), offset.to_string()));
    }
    entries.push(("ref_count".to_string(), cred.refs.len().to_string()));
    for (index, (_, image)) in cred.refs.iter().enumerate() {
        entries.push((format!("ref{index}_image"), (*image as i64).to_string()));
    }
    entries
}

/// The selected route's branch geometry, or an empty list when the extractor
/// cannot derive a layout for it (the built-in profile then supplies it).
pub fn conf_route_geometry(
    route: &str,
    release: &str,
    pselect_shift: Option<i64>,
    structs: &BTreeMap<String, Option<u32>>,
) -> Vec<(&'static str, i64)> {
    let major = release
        .split('.')
        .next()
        .and_then(|part| part.parse::<u32>().ok());
    match route {
        "select_stack" => pselect_shift
            .map(|shift| vec![("waiter_shift", shift)])
            .unwrap_or_default(),
        // android14-6.1 is the compact-waiter family; no other family has a
        // measured tcp layout.
        "tcp_zerocopy"
            if kernel_layout_verified(Some(release))
                && crate::symbols::kernel_struct_macro(Some(release))
                    == Some("STRUCT_OFFSETS_6_1") =>
        {
            vec![("compact_waiter", 1)]
        }
        // A verified 5.x train gets the corroborated geometry; the
        // device-measured forged-object placement is emitted only for the exact
        // validated release. An unverified release keeps just the BTF-derived
        // waiter/lock offsets.
        "multicast_waiter" if major == Some(5) => {
            if !kernel_layout_verified(Some(release)) {
                crate::derive::multicast_geometry_btf_only(structs)
            } else if kernel_device_geometry_verified(Some(release)) {
                crate::derive::multicast_geometry_5x(structs)
            } else {
                crate::derive::multicast_geometry_corroborated(structs)
            }
        }
        _ => Vec::new(),
    }
}

/// `offset` keys in the bundled profiles' order; `empty_zero_page` and
/// `mcast_fake_bss` come from kallsyms instead of an `off_*` symbol.
fn conf_offsets(
    symbols: &BTreeMap<String, Option<u64>>,
    extra: &ConfExtraOffsets,
) -> Vec<(String, String)> {
    let symbol = |key: &str| {
        symbols
            .get(key)
            .copied()
            .flatten()
            .map(|value| value.to_string())
    };
    [
        ("init_task", symbol("off_init_task")),
        ("init_cred", symbol("off_init_cred")),
        (
            "empty_zero_page",
            extra.empty_zero_page.map(|v| v.to_string()),
        ),
        (
            "mcast_fake_bss",
            extra.mcast_fake_bss.map(|v| v.to_string()),
        ),
        ("root_task_group", symbol("off_root_task_group")),
        ("selinux_enforcing", symbol("off_selinux_enforcing")),
        ("selinux_blob_sizes", symbol("off_selinux_blob_sizes")),
        ("security_hook_heads", symbol("off_security_hook_heads")),
        ("slide_nfulnl_logger", symbol("off_slide_nfulnl_logger")),
        ("slide_boot_id", symbol("off_slide_boot_id")),
        ("slide_loggers_0_1", symbol("off_slide_loggers_0_1")),
    ]
    .into_iter()
    .filter_map(|(key, value)| value.map(|value| (key.to_string(), value)))
    .collect()
}

fn push_conf_block(lines: &mut Vec<String>, name: &str, entries: &[(String, String)]) {
    if entries.is_empty() {
        return;
    }
    lines.push(format!("{name} {{"));
    for (key, value) in entries {
        lines.push(format!("  {key} = {value}"));
    }
    lines.push("}".to_string());
}

/// Everything `render_conf` writes, in one bundle.
#[derive(Debug, Clone)]
pub struct ConfInputs<'a> {
    pub release: &'a str,
    pub phys: Option<u64>,
    pub symbols: &'a BTreeMap<String, Option<u64>>,
    pub structs: &'a BTreeMap<String, Option<u32>>,
    pub route: Option<&'a str>,
    pub route_geometry: &'a [(&'static str, i64)],
    pub cred: &'a [(String, String)],
    pub extra_offsets: &'a ConfExtraOffsets,
}

/// Renders a flattened, self-contained GLK profile (`--format conf`): no
/// `include` lines, the shared 6.x credential and KernelSnitch constants
/// inlined, keys and nesting matching `app/src/main/assets/kernel_profiles/`.
/// Fields without a derived value are omitted rather than written as `null`.
pub fn render_conf(input: &ConfInputs<'_>) -> String {
    let release = input.release;
    let major = release
        .split('.')
        .next()
        .and_then(|part| part.parse::<u32>().ok());
    let mut lines = vec![
        format!("# GhostLock kernel profile: {release} (HOCON, self-contained)."),
        format!("release = \"{release}\""),
        "schema_version = 1".to_string(),
        format!("kernel_major = {}", major.unwrap_or(0)),
        "recommend_shizuku = 0".to_string(),
    ];
    if let Some(phys) = input.phys {
        lines.push(format!("kernel_phys_load = 0x{phys:X}"));
    }
    if let Some(route) = input.route {
        // A candidate profile keeps the chosen route even when no geometry
        // could be derived, so the import carries the recommendation and the
        // missing fields surface as invalid paths on the Kotlin side.
        lines.push("route {".to_string());
        lines.push(format!("  {route} {{"));
        for (key, value) in input.route_geometry {
            lines.push(format!("    {key} = {value}"));
        }
        lines.push("  }".to_string());
        lines.push("}".to_string());
    }
    push_conf_block(
        &mut lines,
        "fallback",
        &[("to".to_string(), "\"none\"".to_string())],
    );

    // KernelSnitch defaults are emitted only for a verified train; an
    // unverified release omits them rather than inheriting a neighbour's value.
    let mut snitch = Vec::new();
    if kernel_layout_verified(Some(release)) {
        match major {
            Some(6) => {
                // = kernelsnitch-6x.conf
                snitch.push(("collisions".to_string(), "4".to_string()));
                if crate::symbols::kernel_struct_macro(Some(release)) == Some("STRUCT_OFFSETS_6_1")
                {
                    // 0x400 is the device SLUB stride, not the BTF sizeof (0x3c0).
                    snitch.push(("mm_struct_sz".to_string(), "1024".to_string()));
                }
            }
            Some(5) => {
                // android13-5.15 measured defaults (bundled 5.15 profile).
                snitch.push(("collisions".to_string(), "8".to_string()));
                snitch.push(("mm_struct_sz".to_string(), "1024".to_string()));
            }
            _ => {}
        }
    }
    push_conf_block(&mut lines, "kernelsnitch", &snitch);

    let task: Vec<(String, String)> = CONF_TASK_FIELDS
        .iter()
        .filter_map(|(macro_name, key)| {
            input
                .structs
                .get(*macro_name)
                .copied()
                .flatten()
                .map(|value| ((*key).to_string(), value.to_string()))
        })
        .collect();
    push_conf_block(&mut lines, "task_struct", &task);

    push_conf_block(&mut lines, "cred", input.cred);

    let offset = conf_offsets(input.symbols, input.extra_offsets);
    push_conf_block(&mut lines, "offset", &offset);

    lines.join("\n") + "\n"
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

/// BTF struct fields a kernel may legitimately lack: the 5.15 GKI BTF has no
/// `slab` type, so `struct_slab_cache` is missing there. Reported as missing,
/// but not failing the extract.
const OPTIONAL_STRUCT_FIELDS: &[&str] = &["struct_slab_cache"];

pub fn optional_struct_fields() -> BTreeSet<&'static str> {
    OPTIONAL_STRUCT_FIELDS.iter().copied().collect()
}

#[cfg(test)]
mod tests {
    use super::{
        ConfExtraOffsets, ConfInputs, build_report, conf_cred_5x, conf_cred_6x,
        conf_route_geometry, pselect_waiter_shift_for, render_conf,
    };
    use crate::derive::Cred5x;
    use std::collections::BTreeMap;

    fn conf_fixture() -> (BTreeMap<String, Option<u64>>, BTreeMap<String, Option<u32>>) {
        let mut symbols: BTreeMap<String, Option<u64>> = BTreeMap::new();
        symbols.insert("off_init_task".to_string(), Some(34_595_456));
        symbols.insert("off_security_hook_heads".to_string(), Some(0));
        symbols.insert("off_absent".to_string(), None);
        let mut structs: BTreeMap<String, Option<u32>> = BTreeMap::new();
        structs.insert("task_prio".to_string(), Some(132));
        structs.insert("waiter_task".to_string(), Some(48));
        structs.insert("waiter_lock".to_string(), Some(56));
        (symbols, structs)
    }

    fn no_extra_offsets() -> ConfExtraOffsets {
        ConfExtraOffsets::default()
    }

    #[test]
    fn conf_is_flattened_and_inlines_the_6x_shared_constants() {
        let (symbols, structs) = conf_fixture();
        let geometry: Vec<(&'static str, i64)> = vec![("waiter_shift", -2)];
        let out = render_conf(&ConfInputs {
            release: "6.6.89-android15-8-g0889fe95bb10-ab14402178-4k",
            phys: Some(0x4000_0000),
            symbols: &symbols,
            structs: &structs,
            route: Some("select_stack"),
            route_geometry: &geometry,
            cred: &conf_cred_6x(),
            extra_offsets: &no_extra_offsets(),
        });
        assert!(!out.contains("include"));
        assert!(out.contains("kernel_phys_load = 0x40000000"));
        assert!(out.contains("route {\n  select_stack {\n    waiter_shift = -2\n  }\n}"));
        assert!(out.contains("collisions = 4"));
        assert!(!out.contains("mm_struct_sz"));
        assert!(!out.contains("task_prio"));
        assert!(out.contains("  prio = 132"));
        assert!(out.contains("cred {\n  caps_offset = 48\n  copy_size = 136"));
        assert!(out.contains("caps_value = -1"));
        assert!(out.contains("init_task = 34595456"));
        assert!(out.contains("security_hook_heads = 0"));
        assert!(!out.contains("off_absent"));
    }

    #[test]
    fn conf_61_writes_the_compact_waiter_and_slub_stride() {
        let (symbols, structs) = conf_fixture();
        let geometry: Vec<(&'static str, i64)> = vec![("compact_waiter", 1)];
        let out = render_conf(&ConfInputs {
            release: "6.1.118-android14-11-gca0ef6d17716-ab13624819",
            phys: None,
            symbols: &symbols,
            structs: &structs,
            route: Some("tcp_zerocopy"),
            route_geometry: &geometry,
            cred: &conf_cred_6x(),
            extra_offsets: &no_extra_offsets(),
        });
        assert!(out.contains("tcp_zerocopy {\n    compact_waiter = 1"));
        assert!(out.contains("mm_struct_sz = 1024"));
        assert!(!out.contains("kernel_phys_load"));
    }

    #[test]
    fn conf_5x_carries_the_derived_credential_and_multicast_geometry() {
        let (symbols, structs) = conf_fixture();
        let cred = Cred5x {
            caps_offset: 48,
            caps_count: 3,
            caps_value: 0x1ffffffffff,
            refs: vec![
                (0x80, 0xffffffc00ab23a80),
                (0x88, 0xffffffc00acce110),
                (0x90, 0xffffffc00ab23ff0),
                (0x98, 0xffffffc00ab23b28),
            ],
        };
        let geometry = conf_route_geometry(
            "multicast_waiter",
            "5.15.189-android13-8-00016-g51bba4309aac-ab14546557",
            Some(-2),
            &structs,
        );
        let out = render_conf(&ConfInputs {
            release: "5.15.189-android13-8-00016-g51bba4309aac-ab14546557",
            phys: None,
            symbols: &symbols,
            structs: &structs,
            route: Some("multicast_waiter"),
            route_geometry: &geometry,
            cred: &conf_cred_5x(&cred, 176),
            extra_offsets: &ConfExtraOffsets {
                empty_zero_page: Some(47_529_984),
                mcast_fake_bss: Some(47_849_664),
            },
        });
        assert!(out.contains("multicast_waiter {\n    waiter_off = 96"));
        assert!(out.contains("buffer_size = 264"));
        assert!(out.contains("task_offset = 48"));
        assert!(out.contains("lock_offset = 56"));
        assert!(out.contains("fake_task_offset = 12800"));
        assert!(out.contains("compact_waiter = 1"));
        assert!(out.contains("collisions = 8"));
        assert!(out.contains("mm_struct_sz = 1024"));
        assert!(out.contains("cred {\n  caps_offset = 48\n  copy_size = 176\n  usage_value = 256"));
        assert!(out.contains("caps_count = 3"));
        assert!(out.contains("caps_value = 2199023255551"));
        assert!(out.contains("ref0_offset = 128"));
        assert!(out.contains("ref3_offset = 152"));
        assert!(out.contains("ref_count = 4"));
        assert!(out.contains("ref0_image = -274698454400"));
        assert!(out.contains("ref3_image = -274698454232"));
        assert!(out.contains("empty_zero_page = 47529984"));
        assert!(out.contains("mcast_fake_bss = 47849664"));
    }

    #[test]
    fn conf_route_geometry_follows_the_measured_families() {
        let (_, structs) = conf_fixture();
        assert_eq!(
            conf_route_geometry("select_stack", "6.6.89-android15-8", Some(-2), &structs),
            vec![("waiter_shift", -2)]
        );
        assert!(
            conf_route_geometry("select_stack", "6.6.89-android15-8", None, &structs).is_empty()
        );
        assert_eq!(
            conf_route_geometry("tcp_zerocopy", "6.1.118-android14-11", Some(1), &structs),
            vec![("compact_waiter", 1)]
        );
        assert!(
            conf_route_geometry("tcp_zerocopy", "6.6.89-android15-8", Some(-2), &structs)
                .is_empty()
        );
        assert_eq!(
            conf_route_geometry(
                "multicast_waiter",
                "5.15.189-android13-8-00016-g51bba4309aac-ab14546557",
                Some(-2),
                &structs
            ),
            vec![
                ("waiter_off", 96),
                ("buffer_size", 264),
                ("task_offset", 48),
                ("lock_offset", 56),
                ("fake_lock_offset", 4608),
                ("fake_task_offset", 12800),
                ("lock_slots_offset", 128),
                ("lock_slot_count", 12),
                ("lock_slot_stride", 8),
                ("compact_waiter", 1),
            ]
        );
        assert!(
            conf_route_geometry("multicast_waiter", "6.6.89-android15-8", Some(-2), &structs)
                .is_empty()
        );
    }

    #[test]
    fn unverified_route_geometry_is_a_partial_candidate() {
        let (_, structs) = conf_fixture();
        // No image-derived shift: the route branch stays empty rather than
        // borrowing the -2 family default.
        assert!(conf_route_geometry("select_stack", "6.7.1-generic", None, &structs).is_empty());
        // An image-derived shift is kept.
        assert_eq!(
            conf_route_geometry("select_stack", "6.7.1-generic", Some(-1), &structs),
            vec![("waiter_shift", -1)]
        );
        // Unverified 5.x multicast keeps only BTF-derived waiter offsets; the
        // proven Xperia constants are withheld.
        assert_eq!(
            conf_route_geometry(
                "multicast_waiter",
                "5.15.178-g3575c47dc7ce-dirty",
                Some(-2),
                &structs
            ),
            vec![("task_offset", 48), ("lock_offset", 56)]
        );
    }

    #[test]
    fn verified_5x_train_without_device_evidence_omits_measured_placement() {
        let (_, structs) = conf_fixture();
        let geometry = conf_route_geometry(
            "multicast_waiter",
            "5.15.208-android13-9-gabcdef",
            Some(-2),
            &structs,
        );
        assert!(geometry.contains(&("waiter_off", 96)));
        assert!(geometry.contains(&("buffer_size", 264)));
        assert!(geometry.contains(&("task_offset", 48)));
        assert!(geometry.contains(&("lock_offset", 56)));
        assert!(geometry.contains(&("compact_waiter", 1)));
        assert!(
            !geometry.iter().any(|(key, _)| {
                let key = *key;
                key.starts_with("fake_") || key.starts_with("lock_slot")
            }),
            "device-measured placement must not be inherited by the train"
        );
    }

    #[test]
    fn candidate_conf_keeps_the_route_branch_when_geometry_is_empty() {
        let symbols: BTreeMap<String, Option<u64>> = BTreeMap::new();
        let structs: BTreeMap<String, Option<u32>> = BTreeMap::new();
        let out = render_conf(&ConfInputs {
            release: "5.15.178-g3575c47dc7ce-dirty",
            phys: None,
            symbols: &symbols,
            structs: &structs,
            route: Some("multicast_waiter"),
            route_geometry: &[],
            cred: &[],
            extra_offsets: &ConfExtraOffsets {
                empty_zero_page: None,
                mcast_fake_bss: None,
            },
        });
        assert!(out.contains("route {"));
        assert!(out.contains("multicast_waiter {"));
        assert!(out.contains("release = \"5.15.178-g3575c47dc7ce-dirty\""));
    }

    #[test]
    fn unverified_release_omits_kernelsnitch_defaults() {
        let (symbols, structs) = conf_fixture();
        let out = render_conf(&ConfInputs {
            release: "6.7.1-generic",
            phys: None,
            symbols: &symbols,
            structs: &structs,
            route: None,
            route_geometry: &[],
            cred: &[],
            extra_offsets: &no_extra_offsets(),
        });
        assert!(!out.contains("kernelsnitch"));
    }

    #[test]
    fn pselect_waiter_shift_matches_the_committed_tables() {
        assert_eq!(
            pselect_waiter_shift_for(Some("6.1.118-android14-11-gca0ef6d17716-ab13624819")),
            Some(1)
        );
        assert_eq!(
            pselect_waiter_shift_for(Some("6.6.92-android15-8")),
            Some(-2)
        );
        assert_eq!(
            pselect_waiter_shift_for(Some("6.12.30-android16-0")),
            Some(0)
        );
        assert_eq!(pselect_waiter_shift_for(Some("6.7.1-android16-1")), None);
        assert_eq!(pselect_waiter_shift_for(None), None);
    }

    #[test]
    fn json_report_keeps_unverified_pselect_shift_null() {
        let symbols = BTreeMap::new();
        let structs = BTreeMap::new();
        let report = build_report(
            Some("6.7.1-generic"),
            0,
            None,
            &symbols,
            &structs,
            0,
            pselect_waiter_shift_for(Some("6.7.1-generic")),
        );
        assert!(report["pselect_waiter_shift"].is_null());
    }
}
