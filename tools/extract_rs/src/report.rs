//! Output rendering for extracted kernel metadata.

use serde_json::{Value, json};
use std::collections::BTreeMap;

use crate::derive::Cred5x;
use crate::error::{ExtractError, Result};
use crate::symbols::{OPTIONAL_SYMBOLS, kernel_layout_verified};

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
/// table (kallsyms-only symbols). Kept in its bundled-profile position.
#[derive(Debug, Clone, Default)]
pub struct ConfExtraOffsets {
    pub empty_zero_page: Option<u64>,
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
        // The 5.x multicast branch keeps only what this image can supply: the
        // BTF-derived rt_mutex_waiter task/lock offsets and the waiter-layout
        // flag. The frame/copy-window constants (`waiter_off` / `buffer_size`)
        // are added by the caller only after the static derivation from the
        // image succeeds, so an unverified candidate never inherits the
        // hardware-probed 5.x constants.
        "multicast_waiter" if major == Some(5) => {
            let mut geometry = crate::derive::multicast_geometry_btf_only(structs);
            geometry.push(("compact_waiter", 1));
            geometry
        }
        _ => Vec::new(),
    }
}

/// `offset` keys in the bundled profiles' order; `empty_zero_page` comes from
/// kallsyms instead of an `off_*` symbol.
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
        ("root_task_group", symbol("off_root_task_group")),
        ("selinux_enforcing", symbol("off_selinux_enforcing")),
        ("selinux_blob_sizes", symbol("off_selinux_blob_sizes")),
        ("security_hook_heads", symbol("off_security_hook_heads")),
        ("slide_nfulnl_logger", symbol("off_slide_nfulnl_logger")),
        ("slide_boot_id", symbol("off_slide_boot_id")),
        ("slide_loggers_0_1", symbol("off_slide_loggers_0_1")),
        // Ancillary vr.ko guard: the tracepoint the vendor probe hangs off.
        ("vr_sys_exit_tp", symbol("off_vr_sys_exit_tp")),
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

    // KernelSnitch defaults are emitted for a verified train, and for every
    // 5.x kernel (the android13-5.15 measured defaults are 5.x-wide and are
    // required for the profile to run). An unverified release omits them.
    let mut snitch = Vec::new();
    if kernel_layout_verified(Some(release)) || major == Some(5) {
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

    // Ancillary vr.ko guard. The gate mirrors recommend_shizuku: it says the
    // profile enables the behavior; whether vr.ko is on the running device is a
    // separate decision, taken there (see docs/analysis/ancillary-controller-guide.md).
    // Both facts are per-image and come from this image's BTF — nothing here
    // reads the kernel release. The layout travels as a u8, so an offset that
    // does not fit is dropped instead of being narrowed to a different member;
    // without the layout the guard stays off (fail closed).
    if let Some(funcs) = input
        .structs
        .get("vr_tracepoint_funcs")
        .copied()
        .flatten()
        .filter(|value| (1..=u8::MAX as u32).contains(value))
    {
        let gate = lines
            .iter()
            .position(|line| line.starts_with("recommend_shizuku"))
            .map(|index| index + 1)
            .unwrap_or(lines.len());
        lines.insert(gate, "recommend_vr_guard = 1".to_string());
        push_conf_block(
            &mut lines,
            "vr_guard",
            &[("tracepoint_funcs".to_string(), funcs.to_string())],
        );
    }

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
/// `slab` type, so `struct_slab_cache` is missing there, and a stripped or
/// vendor BTF may not describe `struct tracepoint` at all. Reported as missing,
/// but not failing the extract — the consumers of these fields either carry a
/// fallback or treat their absence as "feature off".
const OPTIONAL_STRUCT_FIELDS: &[&str] = &["struct_slab_cache", "vr_tracepoint_funcs"];

pub fn optional_struct_fields() -> BTreeSet<&'static str> {
    OPTIONAL_STRUCT_FIELDS.iter().copied().collect()
}

#[cfg(test)]
mod tests {
    use super::{
        CONF_TASK_FIELDS, ConfExtraOffsets, ConfInputs, build_report, conf_cred_5x, conf_cred_6x,
        conf_route_geometry, pselect_waiter_shift_for, render_conf,
    };
    use crate::derive::Cred5x;
    use std::collections::{BTreeMap, BTreeSet};

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
    fn conf_emits_the_vr_guard_only_when_the_layout_fits_the_transport() {
        let (symbols, base_structs) = conf_fixture();
        let geometry: Vec<(&'static str, i64)> = vec![("waiter_shift", -2)];
        let render = |funcs: Option<Option<u32>>| {
            let mut structs = base_structs.clone();
            if let Some(value) = funcs {
                structs.insert("vr_tracepoint_funcs".to_string(), value);
            }
            render_conf(&ConfInputs {
                release: "6.1.145-android14-11-maybe-dirty",
                phys: None,
                symbols: &symbols,
                structs: &structs,
                route: Some("select_stack"),
                route_geometry: &geometry,
                cred: &conf_cred_6x(),
                extra_offsets: &no_extra_offsets(),
            })
        };
        let fitted = render(Some(Some(0x40)));
        assert!(fitted.contains("recommend_vr_guard = 1"));
        assert!(fitted.contains("vr_guard {\n  tracepoint_funcs = 64\n}"));
        /* An offset that cannot travel in the u8 layout is dropped instead of
         * being narrowed onto a different tracepoint member: guard off. */
        for bad in [Some(Some(0x140u32)), Some(Some(0)), None] {
            let out = render(bad);
            assert!(!out.contains("vr_guard"));
            assert!(!out.contains("recommend_vr_guard"));
        }
    }

    #[test]
    fn optional_struct_fields_cover_the_vr_guard_layout() {
        let mut structs: BTreeMap<String, Option<u64>> = BTreeMap::new();
        structs.insert("task_prio".to_string(), Some(132));
        structs.insert("vr_tracepoint_funcs".to_string(), None);
        /* A kernel whose BTF lacks `struct tracepoint` still extracts in every
         * format: the guard layout is optional, the rest is required. */
        assert!(super::require_fields(&structs, &super::optional_struct_fields()).is_ok());
        assert!(super::require_fields(&structs, &BTreeSet::new()).is_err());
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
        let mut geometry = conf_route_geometry(
            "multicast_waiter",
            "5.15.189-android13-8-00016-g51bba4309aac-ab14546557",
            Some(-2),
            &structs,
        );
        // The frame/copy-window constants are added only from this image's
        // static derivation, exactly as main.rs does on success.
        geometry.insert(0, ("waiter_off", 96));
        geometry.insert(1, ("buffer_size", 264));
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
            },
        });
        assert!(out.contains("multicast_waiter {\n    waiter_off = 96"));
        assert!(out.contains("buffer_size = 264"));
        assert!(out.contains("task_offset = 48"));
        assert!(out.contains("lock_offset = 56"));
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
                ("task_offset", 48),
                ("lock_offset", 56),
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
        // Every 5.x multicast profile keeps the BTF-derived waiter field
        // offsets and the layout flag, but never the hardware-probed
        // frame/copy-window constants: those are added only by the image's
        // static derivation, even when the release carries no "-android13-"
        // train tag.
        assert_eq!(
            conf_route_geometry(
                "multicast_waiter",
                "5.15.178-g3575c47dc7ce-dirty",
                Some(-2),
                &structs
            ),
            vec![
                ("task_offset", 48),
                ("lock_offset", 56),
                ("compact_waiter", 1),
            ]
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
        assert!(!geometry.iter().any(|(key, _)| *key == "waiter_off"));
        assert!(!geometry.iter().any(|(key, _)| *key == "buffer_size"));
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
    fn unverified_5x_candidate_omits_the_frame_constants() {
        let (symbols, structs) = conf_fixture();
        let geometry = conf_route_geometry(
            "multicast_waiter",
            "5.15.178-g3575c47dc7ce-dirty",
            Some(-2),
            &structs,
        );
        let out = render_conf(&ConfInputs {
            release: "5.15.178-g3575c47dc7ce-dirty",
            phys: None,
            symbols: &symbols,
            structs: &structs,
            route: Some("multicast_waiter"),
            route_geometry: &geometry,
            cred: &[],
            extra_offsets: &no_extra_offsets(),
        });
        // Without this image's static derivation the unverified candidate must
        // not borrow the hardware-probed frame/copy-window constants.
        assert!(!out.contains("waiter_off"));
        assert!(!out.contains("buffer_size"));
        assert!(out.contains("task_offset = 48"));
        assert!(out.contains("lock_offset = 56"));
        assert!(out.contains("compact_waiter = 1"));
        // The 5.x KernelSnitch defaults are required to run and are emitted even
        // without the "-android13-" train tag.
        assert!(out.contains("kernelsnitch"));
        assert!(out.contains("collisions = 8"));
        assert!(out.contains("mm_struct_sz = 1024"));
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

    /// Flatten a HOCON-ish profile into `section.key` -> value, ignoring
    /// comments and one level of brace nesting.
    fn flatten_conf(text: &str) -> BTreeMap<String, String> {
        let mut out = BTreeMap::new();
        let mut stack: Vec<String> = Vec::new();
        for raw in text.lines() {
            let line = raw.split('#').next().unwrap_or("").trim();
            if line.is_empty() {
                continue;
            }
            if line.ends_with('{') {
                stack.push(line[..line.len() - 1].trim().to_string());
                continue;
            }
            if line == "}" {
                stack.pop();
                continue;
            }
            if let Some((key, value)) = line.split_once('=') {
                let key = key.trim();
                let value = value.trim();
                let full = if stack.is_empty() {
                    key.to_string()
                } else {
                    format!("{}.{}", stack.join("."), key)
                };
                out.insert(full, value.to_string());
            }
        }
        out
    }

    /// The values the real extractor produces for the A301SO `5.15.189` boot
    /// image, so the rendered conf can be compared field-for-field with the
    /// bundled, hardware-validated profile.
    fn a301so_inputs() -> (
        String,
        BTreeMap<String, Option<u64>>,
        BTreeMap<String, Option<u32>>,
        Vec<(String, String)>,
        ConfExtraOffsets,
    ) {
        let release = "5.15.189-android13-8-00016-g51bba4309aac-ab14546557";
        let mut symbols: BTreeMap<String, Option<u64>> = BTreeMap::new();
        for (key, value) in [
            ("off_init_task", 46_412_800u64),
            ("off_init_cred", 46_126_472),
            ("off_root_task_group", 47_549_120),
            ("off_selinux_enforcing", 47_885_704),
            ("off_selinux_blob_sizes", 35_027_656),
            ("off_security_hook_heads", 35_018_304),
            ("off_slide_nfulnl_logger", 45_096_488),
            ("off_slide_boot_id", 47_999_001),
            ("off_slide_loggers_0_1", 45_096_280),
        ] {
            symbols.insert(key.to_string(), Some(value));
        }
        let mut structs: BTreeMap<String, Option<u32>> = BTreeMap::new();
        for (key, _) in CONF_TASK_FIELDS {
            // Values from the A301SO image's BTF.
            let v = match *key {
                "task_prio" => 124,
                "task_normal_prio" => 132,
                "task_sched_task_group" => 1024,
                "task_pi_lock" => 2180,
                "task_pi_waiters" => 2200,
                "task_pi_top_task" => 2216,
                "task_pi_blocked_on" => 2224,
                "task_pid" => 1496,
                "task_tgid" => 1500,
                "task_atomic_flags" => 1432,
                "task_real_cred" => 1936,
                "task_cred" => 1944,
                "task_comm" => 1960,
                "task_tasks" => 1232,
                "task_seccomp" => 2144,
                _ => continue,
            };
            structs.insert(key.to_string(), Some(v));
        }
        structs.insert("waiter_task".to_string(), Some(48));
        structs.insert("waiter_lock".to_string(), Some(56));
        let cred5x = Cred5x {
            caps_offset: 48,
            caps_count: 3,
            caps_value: 2_199_023_255_551,
            refs: vec![
                (128, -274_698_454_400i64 as u64),
                (136, -274_696_707_824i64 as u64),
                (144, -274_698_453_008i64 as u64),
                (152, -274_698_454_232i64 as u64),
            ],
        };
        let cred = conf_cred_5x(&cred5x, 176);
        let extra = ConfExtraOffsets {
            empty_zero_page: Some(47_529_984),
        };
        (release.to_string(), symbols, structs, cred, extra)
    }

    #[test]
    fn a301so_generated_conf_matches_the_bundled_profile() {
        let (release, symbols, structs, cred, extra) = a301so_inputs();
        let mut geometry = conf_route_geometry("multicast_waiter", &release, None, &structs);
        // A301SO's static derivation reproduces the hardware-probed 0x60, so
        // the generated profile carries the same constants as the bundled one.
        geometry.insert(0, ("waiter_off", 96));
        geometry.insert(1, ("buffer_size", 264));
        let generated = render_conf(&ConfInputs {
            release: &release,
            phys: None,
            symbols: &symbols,
            structs: &structs,
            route: Some("multicast_waiter"),
            route_geometry: &geometry,
            cred: &cred,
            extra_offsets: &extra,
        });
        let bundled = std::fs::read_to_string(concat!(
            env!("CARGO_MANIFEST_DIR"),
            "/../../app/src/main/assets/kernel_profiles/5.15.189-android13-8-00016-g51bba4309aac-ab14546557.conf"
        ))
        .expect("bundled 5.15.189 profile");
        let generated = flatten_conf(&generated);
        let bundled = flatten_conf(&bundled);

        assert!(generated.contains_key("route.multicast_waiter.waiter_off"));
        // The one intentional difference: shizuku defaults to off in generated
        // confs, the bundled profile recommends it.
        assert_eq!(
            generated.get("recommend_shizuku").map(String::as_str),
            Some("0")
        );
        assert_eq!(
            bundled.get("recommend_shizuku").map(String::as_str),
            Some("1")
        );

        for key in bundled.keys() {
            if key == "recommend_shizuku" {
                continue;
            }
            assert_eq!(
                generated.get(key),
                bundled.get(key),
                "field {key} differs between generated and bundled profile"
            );
        }
        // And the generated profile carries no extra non-comment field.
        for key in generated.keys() {
            assert!(
                bundled.contains_key(key),
                "generated profile has unexpected field {key}"
            );
        }
    }
}
