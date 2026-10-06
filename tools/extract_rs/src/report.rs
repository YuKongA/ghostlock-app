//! Output rendering for extracted kernel metadata.

use serde_json::{Value, json};
use std::collections::{BTreeMap, BTreeSet};

use crate::derive::Cred5x;
use crate::error::{ExtractError, Result};
use crate::plugin::ExtractValue;
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

/// Full route field universe per branch (authoritative: Kotlin
/// `RouteConfig.entries()` / native `kSections`). A missing value renders as
/// `null` so every generated profile carries every field of its route.
const CONF_ROUTE_FIELDS: &[(&str, &[&str])] = &[
    ("tcp_zerocopy", &["compact_waiter"]),
    ("select_stack", &["waiter_shift"]),
    (
        "multicast_waiter",
        &[
            "waiter_off",
            "buffer_size",
            "task_offset",
            "lock_offset",
            "compact_waiter",
        ],
    ),
];

/// Full credential field universe (native `kCred` / `credential-6x.conf`).
const CONF_CRED_FIELDS: &[&str] = &[
    "copy_size",
    "usage_offset",
    "usage_value",
    "caps_offset",
    "caps_count",
    "caps_value",
    "ref_count",
    "ref0_offset",
    "ref1_offset",
    "ref2_offset",
    "ref3_offset",
    "ref0_image",
    "ref1_image",
    "ref2_image",
    "ref3_image",
];

/// Full offset field universe (native `kOffset`).
const CONF_OFFSET_FIELDS: &[&str] = &[
    "init_task",
    "init_cred",
    "empty_zero_page",
    "root_task_group",
    "selinux_enforcing",
    "selinux_blob_sizes",
    "security_hook_heads",
    "slide_nfulnl_logger",
    "slide_loggers_0_1",
    "slide_boot_id",
];

/// `cred` keys that are platform-ABI facts (layout/offsets); the rest are the
/// 43499 credential template. S4 R2 splits the wire section accordingly.
const CONF_CRED_PLATFORM_KEYS: &[&str] = &[
    "usage_offset",
    "caps_offset",
    "ref_count",
    "ref0_offset",
    "ref1_offset",
    "ref2_offset",
    "ref3_offset",
];

/// `offset` keys that are platform-ABI facts; the rest are 43499 slide offsets.
const CONF_OFFSET_PLATFORM_KEYS: &[&str] = &[
    "init_task",
    "init_cred",
    "empty_zero_page",
    "root_task_group",
    "selinux_enforcing",
    "selinux_blob_sizes",
    "security_hook_heads",
];

/// Native owner-qualified WIRE paths the extractor can cause the app to emit.
/// After the HOCON/wire rename (native 23958eb0 / b55708a8) the profile spelling
/// IS the wire spelling: a path here is exactly what `render_conf` writes, with no
/// translation layer in between. Root scalars are BARE keys (the manifest lists
/// them under owner `root`), which the empty section marks; every other entry is
/// `section.key`. The manifest test asserts each path is present in the native GLKv3
/// owner manifest (`app/src/test/resources/profile-manifest-v3.tsv`); the check is a
/// subset because the extractor only derives image-dependent fields while the
/// manifest is the full owner schema.
pub fn conf_wire_fields() -> Vec<(&'static str, &'static str)> {
    let mut out: Vec<(&'static str, &'static str)> = vec![
        // Root scalars (owner `root` in the manifest; empty section = bare key).
        ("", "kernel_major"),
        ("", "kernel_minor"),
        ("", "safe_mode"),
        ("backend.cve_2026_43499", "steps"),
        // 43284 execution tuning: the profile and the wire both spell it `execution.*`.
        ("backend.cve_2026_43284.execution", "late_load_args"),
        ("backend.cve_2026_43284.execution", "selinux_exec_context"),
        ("backend.cve_2026_43284.execution", "module_poll_attempts"),
        (
            "backend.cve_2026_43284.execution",
            "module_poll_interval_ms",
        ),
        ("backend.cve_2026_43284.execution", "wait_timeout_ms"),
        // `platform.abi.*` no longer exists: the ABI block lives under the 43499
        // backend in both the profile and the wire schema.
        ("backend.cve_2026_43499.abi.kernel", "kernel_phys_load"),
        ("backend.cve_2026_43499.abi.kernel", "kernel_phys_offset"),
        ("backend.cve_2026_43499.kernel", "kernelsnitch_collisions"),
        ("backend.cve_2026_43499.kernel", "mm_struct_sz"),
        // Every `route.<route>.compact_waiter` gate (tcp/select/multicast) maps
        // onto the shared 43499 `kernel.compact_waiter` slot.
        ("backend.cve_2026_43499.kernel", "compact_waiter"),
        // countermeasure.vivo_vr_guard.* was DELETED by user ruling (not frozen):
        // render_conf no longer emits it, so it is no longer a wire field here.
    ];
    for (_, key) in CONF_TASK_FIELDS.iter().copied() {
        out.push(("backend.cve_2026_43499.abi.task_struct", key));
    }
    for key in CONF_CRED_FIELDS.iter().copied() {
        if CONF_CRED_PLATFORM_KEYS.contains(&key) {
            out.push(("backend.cve_2026_43499.abi.cred", key));
        } else {
            out.push(("backend.cve_2026_43499.cred", key));
        }
    }
    for key in CONF_OFFSET_FIELDS.iter().copied() {
        if CONF_OFFSET_PLATFORM_KEYS.contains(&key) {
            out.push(("backend.cve_2026_43499.abi.offset", key));
        } else {
            out.push(("backend.cve_2026_43499.offset", key));
        }
    }
    out.push(("backend.cve_2026_43499.route.select_stack", "waiter_shift"));
    out.push((
        "backend.cve_2026_43499.route.multicast_waiter",
        "waiter_off",
    ));
    out.push((
        "backend.cve_2026_43499.route.multicast_waiter",
        "buffer_size",
    ));
    out.push((
        "backend.cve_2026_43499.route.multicast_waiter",
        "task_offset",
    ));
    out.push((
        "backend.cve_2026_43499.route.multicast_waiter",
        "lock_offset",
    ));
    out
}

/// The owner-qualified WIRE paths this extractor can produce — the R1 whitelist
/// of the plugin extract projection (`plugin::resolve_extract`). A plugin extract
/// name that names one of these fields receives the exact literal the rendered
/// profile carries.
pub fn conf_wire_paths() -> BTreeSet<String> {
    conf_wire_fields()
        .into_iter()
        .map(|(section, key)| wire_path(section, key))
        .collect()
}

/// `section.key`, or the bare key for a root scalar (empty section), matching
/// `profile-manifest-v3.tsv` where root rows carry owner `root` and the bare key.
pub(crate) fn wire_path(section: &str, key: &str) -> String {
    if section.is_empty() {
        key.to_string()
    } else {
        format!("{section}.{key}")
    }
}

/// Looks up a key in `(key, value)` entries, or `"null"` when absent.
fn conf_lookup(entries: &[(String, String)], key: &str) -> String {
    entries
        .iter()
        .find(|(candidate, _)| candidate == key)
        .map(|(_, value)| value.clone())
        .unwrap_or_else(|| "null".to_string())
}

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

/// The backend whose step token carries a route prefix (S4-R6b).
pub const BACKEND_43499: &str = "cve_2026_43499";
/// The other catalogued backend. `render_conf` always emits its block (single
/// combination, `umh`) so a generated profile declares both backends; only the
/// selected one carries `steps`.
pub const BACKEND_43284: &str = "cve_2026_43284";
/// 43284 execution tuning, HOCON `backend.cve_2026_43284.execution.*`, with the
/// frozen `[literal:N]` defaults from docs/profile/PROFILE_TEMPLATE.conf.
const CONF_43284_EXECUTION: [(&str, &str); 5] = [
    ("late_load_args", "0"),
    ("selinux_exec_context", "0"),
    ("module_poll_attempts", "40"),
    ("module_poll_interval_ms", "5"),
    ("wait_timeout_ms", "15000"),
];
/// The route-less backend whose step token is a bare path name (declared above
/// with the 43284 execution table).

/// Route -> short token prefix for a backend with a route axis. This is the
/// extractor-side copy of the native token contract; the full route name still
/// selects the geometry branch. `Auto` is deliberately absent: selection never
/// guesses a route, so an unknown route must fail closed.
const ROUTE_TOKEN_PREFIX: &[(&str, &str)] = &[
    ("multicast_waiter", "mcast"),
    ("select_stack", "pselect"),
    ("tcp_zerocopy", "tcp"),
];

/// Path -> terminal the combination hands off to. `rootchild` and `shizuku`
/// both enter the root child; `umh` forwards through the kernel UMH helper.
const PATH_TERMINAL: &[(&str, &str)] = &[
    ("rootchild", "root_child"),
    ("shizuku", "root_child"),
    ("umh", "umh_forward"),
];

/// Short token prefix for a route, or `None` when the route is unknown.
pub fn route_token_prefix(route: &str) -> Option<&'static str> {
    ROUTE_TOKEN_PREFIX
        .iter()
        .find(|(name, _)| *name == route)
        .map(|(_, prefix)| *prefix)
}

/// Terminal a step path implies, or `None` when the path is unknown.
pub fn path_terminal(path: &str) -> Option<&'static str> {
    PATH_TERMINAL
        .iter()
        .find(|(name, _)| *name == path)
        .map(|(_, terminal)| *terminal)
}

/// Compose the single `backend.<id>.steps` token per the native contract
/// (S4-R6b): a backend with a route axis spells `<route-prefix>_<path>`, a
/// route-less backend spells the bare `<path>`. The extractor renders 43499
/// only, but the 43284 bare form is modelled here so the contract has one
/// definition and the unit tests pin both shapes.
///
/// Fails closed (`None`) on an unknown route (including a missing route for a
/// route-axis backend), an unknown path, or an unknown backend, so the caller
/// can never emit a bogus token.
pub fn combination_token(backend: &str, route: Option<&str>, path: &str) -> Option<String> {
    // Validate the path independently of the backend spelling.
    path_terminal(path)?;
    match backend {
        BACKEND_43499 => {
            let prefix = route_token_prefix(route?)?;
            Some(format!("{prefix}_{path}"))
        }
        BACKEND_43284 => Some(path.to_string()),
        _ => None,
    }
}

/// Everything `render_conf` writes, in one bundle.
#[derive(Debug, Clone)]
pub struct ConfInputs<'a> {
    pub release: &'a str,
    pub phys: Option<u64>,
    /// DRAM base (linear-map PHYS_OFFSET); normally supplied by hand, so the
    /// extractor writes an explicit `null` unless one is known.
    pub phys_offset: Option<u64>,
    pub symbols: &'a BTreeMap<String, Option<u64>>,
    pub structs: &'a BTreeMap<String, Option<u32>>,
    /// Backend selecting the step token spelling (`cve_2026_43499`).
    pub backend: &'a str,
    pub route: Option<&'a str>,
    /// Combination step path (`rootchild`/`shizuku`/`umh`). The extractor
    /// derives the rootchild path; it never infers a route.
    pub steps_path: &'a str,
    pub route_geometry: &'a [(&'static str, i64)],
    pub cred: &'a [(String, String)],
    pub extra_offsets: &'a ConfExtraOffsets,
}

/// Renders a canonical self-contained GLK profile (`--format conf`) in the
/// **frozen HOCON shape** (user ruling 2026-10-05; authority
/// `docs/profile/PROFILE_TEMPLATE.conf`): root scalars
/// (schema_version / release / kernel_major / kernel_minor / safe_mode),
/// `available { <backend> = [ <combination token> ] }`, then
/// `backend.<id> { steps, abi { task_struct, cred, kernel, offset }, ... }`.
/// The `common` / `platform` / `selection` owners no longer exist: the platform
/// ABI block moved under `backend.cve_2026_43499.abi`, the selection became
/// `available`, and `vr_guard` / `defex_symbol` are deleted. Fields without a
/// derived value are emitted as explicit `null`.
pub fn render_conf(input: &ConfInputs<'_>) -> String {
    let release = input.release;
    let major = release
        .split('.')
        .next()
        .and_then(|part| part.parse::<u32>().ok());
    let minor = release
        .split('.')
        .nth(1)
        .and_then(|part| part.trim().parse::<u32>().ok());

    // The step token under the selected backend (a route-axis backend qualifies
    // it with the route prefix). An unknown/absent route fails closed (no token)
    // so the app rejects the incomplete profile instead of running a guessed
    // combination. `terminal` is gone from HOCON: the token implies it.
    let steps_token = combination_token(input.backend, input.route, input.steps_path);

    let mut lines = vec![
        format!("# GhostLock kernel profile: {release} (HOCON, GLKv3 schema 3)."),
        "ghostlock {".to_string(),
        "  schema_version = 3".to_string(),
        format!("  release = \"{release}\""),
        format!("  kernel_major = {}", conf_scalar(major.map(u64::from))),
        format!("  kernel_minor = {}", conf_scalar(minor.map(u64::from))),
        "  safe_mode = false".to_string(),
        /* Two levels: pick an available backend, then a combination token under
         * it. The profile only DECLARES availability; the App/user selects. */
        "  available {".to_string(),
    ];
    if let Some(token) = &steps_token {
        lines.push(format!(
            "    {} = [ \"{token}\" ]",
            hocon_key(input.backend)
        ));
    }
    if input.backend != BACKEND_43284 {
        lines.push(format!("    {BACKEND_43284} = [ \"umh\" ]"));
    }
    lines.push("  }".to_string());

    lines.push("  backend {".to_string());
    lines.push(format!("    {} {{", hocon_key(input.backend)));
    if let Some(token) = &steps_token {
        lines.push(format!("      steps = \"{token}\""));
    }

    /* `platform.abi.*` moved here (`platform` owner deleted). The block order follows
     * the frozen template: task_struct, cred, kernel, offset. */
    lines.push("      abi {".to_string());
    lines.push("        task_struct {".to_string());
    for (macro_name, key) in CONF_TASK_FIELDS {
        let value = input
            .structs
            .get(*macro_name)
            .copied()
            .flatten()
            .map(|value| value.to_string())
            .unwrap_or_else(|| "null".to_string());
        lines.push(format!("          {key} = {value}"));
    }
    lines.push("        }".to_string());
    lines.push("        cred {".to_string());
    for key in CONF_CRED_FIELDS.iter().copied() {
        if CONF_CRED_PLATFORM_KEYS.contains(&key) {
            lines.push(format!(
                "          {key} = {}",
                conf_lookup(input.cred, key)
            ));
        }
    }
    lines.push("        }".to_string());
    lines.push("        kernel {".to_string());
    lines.push(format!(
        "          kernel_phys_load = {}",
        conf_scalar(input.phys)
    ));
    lines.push(format!(
        "          kernel_phys_offset = {}",
        conf_scalar(input.phys_offset)
    ));
    lines.push("        }".to_string());
    let offset_entries = conf_offsets(input.symbols, input.extra_offsets);
    lines.push("        offset {".to_string());
    for key in CONF_OFFSET_FIELDS.iter().copied() {
        if CONF_OFFSET_PLATFORM_KEYS.contains(&key) {
            lines.push(format!(
                "          {key} = {}",
                conf_lookup(&offset_entries, key)
            ));
        }
    }
    lines.push("        }".to_string());
    lines.push("      }".to_string());

    /* Backend-scoped blocks (the 43499 route geometry and the values the App
     * folds onto backend.cve_2026_43499.*). */
    if let Some(route) = input.route {
        let route_fields: Vec<&'static str> =
            match CONF_ROUTE_FIELDS.iter().find(|(name, _)| *name == route) {
                Some((_, fields)) => fields
                    .iter()
                    .copied()
                    .filter(|field| *field != "compact_waiter")
                    .collect(),
                None => input
                    .route_geometry
                    .iter()
                    .map(|(key, _)| *key)
                    .filter(|key| *key != "compact_waiter")
                    .collect(),
            };
        lines.push("      route {".to_string());
        if route_fields.is_empty() {
            lines.push(format!("        {route} {{}}"));
        } else {
            lines.push(format!("        {route} {{"));
            for field in route_fields {
                let value = input
                    .route_geometry
                    .iter()
                    .find(|(key, _)| *key == field)
                    .map(|(_, value)| value.to_string())
                    .unwrap_or_else(|| "null".to_string());
                lines.push(format!("          {field} = {value}"));
            }
            lines.push("        }".to_string());
        }
        lines.push("      }".to_string());
    }

    lines.push("      cred {".to_string());
    for key in CONF_CRED_FIELDS.iter().copied() {
        if !CONF_CRED_PLATFORM_KEYS.contains(&key) {
            lines.push(format!("        {key} = {}", conf_lookup(input.cred, key)));
        }
    }
    lines.push("      }".to_string());

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
    lines.push("      kernel {".to_string());
    if let Some((_, value)) = input
        .route_geometry
        .iter()
        .find(|(key, _)| *key == "compact_waiter")
    {
        lines.push(format!(
            "        compact_waiter = {}",
            if *value != 0 { "true" } else { "false" }
        ));
    }
    lines.push(format!(
        "        kernelsnitch_collisions = {}",
        conf_lookup(&snitch, "collisions")
    ));
    lines.push(format!(
        "        mm_struct_sz = {}",
        conf_lookup(&snitch, "mm_struct_sz")
    ));
    lines.push("      }".to_string());

    lines.push("      offset {".to_string());
    for key in CONF_OFFSET_FIELDS.iter().copied() {
        if !CONF_OFFSET_PLATFORM_KEYS.contains(&key) {
            lines.push(format!(
                "        {key} = {}",
                conf_lookup(&offset_entries, key)
            ));
        }
    }
    lines.push("      }".to_string());

    lines.push("    }".to_string());

    /* The second catalogued backend is always declared (single combination). It
     * carries no extractor-derived values: its execution tuning is the frozen
     * `[literal:N]` set, and kmi / lkm_path / carrier_path are computed at
     * runtime inside the GhostLock directory (they must NOT appear here). */
    if input.backend != BACKEND_43284 {
        lines.push(format!("    {BACKEND_43284} {{"));
        lines.push("      steps = \"umh\"".to_string());
        lines.push("      execution {".to_string());
        for (key, value) in CONF_43284_EXECUTION {
            lines.push(format!("        {key} = {value}"));
        }
        lines.push("      }".to_string());
        lines.push("    }".to_string());
    }

    lines.push("  }".to_string());
    lines.push("}".to_string());
    lines.join("\n") + "\n"
}

/// `null` for an underived value, else the decimal literal (HOCON has no
/// unsigned type; every extractor value fits u64).
fn conf_scalar(value: Option<u64>) -> String {
    value
        .map(|value| value.to_string())
        .unwrap_or_else(|| "null".to_string())
}

/// One plugin's resolved extract entries, in descriptor declaration order.
#[derive(Debug, Clone)]
pub struct ResolvedPlugin {
    pub id: String,
    pub entries: Vec<(String, ExtractValue)>,
}

/// HOCON quoting helpers for the plugin extract block. The backslash is written
/// as a unicode escape so the quoting rules stay readable next to the HOCON
/// grammar the App parses (Typesafe Config).
const HOCON_QUOTE: char = '"';
const HOCON_BACKSLASH: char = '\u{5c}';

/// Flattens a rendered `--format conf` document into `path -> literal`, with the
/// `ghostlock` wrapper stripped. Comments and brace-only lines are ignored and a
/// quoted key is unquoted. The plugin extract projection reads back the values
/// this crate just rendered, so an extract value is the profile value by
/// construction instead of a second derivation.
///
/// The profile spelling IS the wire spelling (native rename 23958eb0 / b55708a8),
/// so the keys produced here are exactly the native owner paths the App folds and
/// the plugin R1 lookup matches: bare root scalars (`kernel_major` / `kernel_minor` /
/// `safe_mode`), `available.<backend>` (profile-only, no wire path) and
/// `backend.<id>.…` including `backend.cve_2026_43499.abi.*` and
/// `backend.cve_2026_43284.execution.*`.
///
/// NOTE: there is deliberately **no path rewriting** here. A document written in
/// the pre-rename spelling (`platform.abi.*`, `common.*`) flattens to exactly
/// those legacy keys and therefore matches NOTHING in the wire vocabulary — such a
/// profile is rejected upstream instead of being silently accepted.
pub fn flatten_conf_values(text: &str) -> BTreeMap<String, String> {
    let mut out = BTreeMap::new();
    let mut stack: Vec<String> = Vec::new();
    for raw in text.lines() {
        let line = raw.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        if let Some(open) = line.strip_suffix('{') {
            stack.push(unquote_hocon_key(open.trim()));
            continue;
        }
        if line == "}" {
            stack.pop();
            continue;
        }
        let Some((key, value)) = line.split_once('=') else {
            continue;
        };
        let key = unquote_hocon_key(key.trim());
        let path = if stack.is_empty() {
            key
        } else {
            format!("{}.{key}", stack.join("."))
        };
        let path = path
            .strip_prefix("ghostlock.")
            .unwrap_or(path.as_str())
            .to_string();
        out.insert(path, value.trim().to_string());
    }
    out
}

/// Appends the `plugin { <id> { extract { ... } } }` block to a rendered profile,
/// immediately before the document's final closing brace (after
/// `countermeasure`, the approved P2 placement). Only `extract` is written:
/// `enabled` / `stage` / `module_path` / `module_hash` belong to the App registry
/// and import flow, never to the extractor, so this output is an importable
/// profile FRAGMENT, not a wire document. With no plugins (or only plugins
/// without entries) the input is returned byte-for-byte unchanged.
pub fn append_plugin_block(base: &str, plugins: &[ResolvedPlugin]) -> String {
    let emitted: Vec<&ResolvedPlugin> = plugins
        .iter()
        .filter(|plugin| !plugin.entries.is_empty())
        .collect();
    if emitted.is_empty() {
        return base.to_string();
    }
    let mut block: Vec<String> = vec!["  plugin {".to_string()];
    for plugin in emitted {
        block.push(format!("    {} {{", hocon_key(&plugin.id)));
        block.push("      extract {".to_string());
        for (key, value) in &plugin.entries {
            block.push(format!(
                "        {} = {}",
                hocon_key(key),
                hocon_literal(value)
            ));
        }
        block.push("      }".to_string());
        block.push("    }".to_string());
    }
    block.push("  }".to_string());

    let mut lines: Vec<String> = base.lines().map(str::to_string).collect();
    let Some(close) = lines.iter().rposition(|line| line.trim() == "}") else {
        return base.to_string();
    };
    lines.splice(close..close, block);
    lines.join("\n") + "\n"
}

/// HOCON key: bare for a simple token, quoted otherwise (plugin ids may contain
/// dots; an R1 extract key is an owner-qualified path and is always quoted).
fn hocon_key(key: &str) -> String {
    let bare = !key.is_empty()
        && key
            .chars()
            .all(|ch| ch.is_ascii_alphanumeric() || ch == '_' || ch == '-');
    if bare {
        key.to_string()
    } else {
        hocon_quote(key)
    }
}

fn hocon_literal(value: &ExtractValue) -> String {
    match value {
        ExtractValue::UInt(number) => number.to_string(),
        ExtractValue::Int(number) => number.to_string(),
        ExtractValue::Bool(flag) => flag.to_string(),
        ExtractValue::Str(text) => hocon_quote(text),
    }
}

/// HOCON string quoting, escaping what the App parser treats specially.
fn hocon_quote(text: &str) -> String {
    let mut out = String::with_capacity(text.len() + 2);
    out.push(HOCON_QUOTE);
    for ch in text.chars() {
        match ch {
            HOCON_QUOTE | HOCON_BACKSLASH => {
                out.push(HOCON_BACKSLASH);
                out.push(ch);
            }
            '\n' => {
                out.push(HOCON_BACKSLASH);
                out.push('n');
            }
            '\r' => {
                out.push(HOCON_BACKSLASH);
                out.push('r');
            }
            '\t' => {
                out.push(HOCON_BACKSLASH);
                out.push('t');
            }
            other if other.is_control() => {
                out.push(HOCON_BACKSLASH);
                out.push('u');
                out.push_str(&format!("{:04x}", other as u32));
            }
            other => out.push(other),
        }
    }
    out.push(HOCON_QUOTE);
    out
}

/// Unquotes a bare or double-quoted HOCON key produced by `hocon_key`.
fn unquote_hocon_key(key: &str) -> String {
    let Some(body) = key
        .strip_prefix(HOCON_QUOTE)
        .and_then(|rest| rest.strip_suffix(HOCON_QUOTE))
    else {
        return key.to_string();
    };
    let mut out = String::with_capacity(body.len());
    let mut chars = body.chars();
    while let Some(ch) = chars.next() {
        if ch != HOCON_BACKSLASH {
            out.push(ch);
            continue;
        }
        match chars.next() {
            Some(HOCON_QUOTE) => out.push(HOCON_QUOTE),
            Some(HOCON_BACKSLASH) => out.push(HOCON_BACKSLASH),
            Some('n') => out.push('\n'),
            Some('r') => out.push('\r'),
            Some('t') => out.push('\t'),
            Some(other) => out.push(other),
            None => {}
        }
    }
    out
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
        CONF_TASK_FIELDS, ConfExtraOffsets, ConfInputs, ResolvedPlugin, append_plugin_block,
        build_report, combination_token, conf_cred_5x, conf_cred_6x, conf_route_geometry,
        conf_wire_paths, flatten_conf_values, path_terminal, pselect_waiter_shift_for, render_conf,
        route_token_prefix,
    };
    use crate::derive::Cred5x;
    use crate::plugin::{
        DeclaredField, ExtractContext, ExtractKind, ExtractValue, PluginDescriptor,
        resolve_descriptor,
    };
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

    /// User ruling 2026-10-05: vr_guard / defex are DELETED (not frozen). The
    /// generated HOCON must not carry either, whatever the BTF supplies.
    #[test]
    fn conf_never_emits_the_deleted_vr_guard_or_defex_fields() {
        let (symbols, base_structs) = conf_fixture();
        let geometry: Vec<(&'static str, i64)> = vec![("waiter_shift", -2)];
        for funcs in [Some(0x40u32), Some(0x140), Some(0), None] {
            let mut structs = base_structs.clone();
            structs.insert("vr_tracepoint_funcs".to_string(), funcs);
            let out = render_conf(&ConfInputs {
                release: "6.1.145-android14-11-maybe-dirty",
                phys: None,
                phys_offset: None,
                symbols: &symbols,
                structs: &structs,
                backend: super::BACKEND_43499,
                route: Some("select_stack"),
                steps_path: "rootchild",
                route_geometry: &geometry,
                cred: &conf_cred_6x(),
                extra_offsets: &no_extra_offsets(),
            });
            assert!(!out.contains("vr_guard"), "vr_guard emitted: {out}");
            assert!(!out.contains("tracepoint_funcs"));
            assert!(!out.contains("defex_symbol"));
            assert!(!out.contains("countermeasure"));
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
            phys_offset: None,
            symbols: &symbols,
            structs: &structs,
            backend: super::BACKEND_43499,
            route: Some("select_stack"),
            steps_path: "rootchild",
            route_geometry: &geometry,
            cred: &conf_cred_6x(),
            extra_offsets: &no_extra_offsets(),
        });
        assert!(!out.contains("include"));
        assert!(out.contains("kernel_phys_load = 1073741824"));
        assert!(out.contains("select_stack {") && out.contains("waiter_shift = -2"));
        assert!(out.contains("kernelsnitch_collisions = 4"));
        assert!(out.contains("mm_struct_sz = null"));
        assert!(!out.contains("task_prio"));
        assert!(out.contains("prio = 132"));
        assert!(out.contains("cred {"));
        assert!(out.contains("copy_size = 136"));
        assert!(out.contains("caps_offset = 48"));
        assert!(out.contains("caps_value = -1"));
        assert!(out.contains("init_task = 34595456"));
        assert!(out.contains("security_hook_heads = 0"));
        assert!(!out.contains("off_absent"));
        // S4-R6b: one token under the backend, no top-level selection.steps.
        assert!(out.contains("steps = \"pselect_rootchild\""));
        assert!(!out.contains("w1_w3"));
        assert!(!out.contains("selection {\n    steps"));
    }

    /// The frozen HOCON shape (user ruling 2026-10-05; authority
    /// `docs/profile/PROFILE_TEMPLATE.conf`): root scalars, `available{}` and
    /// `backend.<id>` with the platform ABI moved under the 43499 backend. Keys the
    /// refactor DELETED must stay deleted.
    #[test]
    fn conf_emits_the_frozen_hocon_shape() {
        let (symbols, structs) = conf_fixture();
        let geometry: Vec<(&'static str, i64)> = vec![("waiter_shift", -2)];
        let out = render_conf(&ConfInputs {
            release: "5.15.189-android13-8-00016-g51bba4309aac-ab14546557",
            phys: Some(0x4000_0000),
            phys_offset: None,
            symbols: &symbols,
            structs: &structs,
            backend: super::BACKEND_43499,
            route: Some("select_stack"),
            steps_path: "rootchild",
            route_geometry: &geometry,
            cred: &conf_cred_6x(),
            extra_offsets: &no_extra_offsets(),
        });
        /* Printed so `cargo test -- --nocapture` shows the real document. */
        println!("{out}");

        // Root scalars (the `common` owner is gone).
        assert!(out.contains("\n  kernel_major = 5\n"), "{out}");
        assert!(out.contains("\n  kernel_minor = 15\n"), "{out}");
        assert!(out.contains("\n  safe_mode = false\n"), "{out}");
        // available{}: two levels, backend -> combination token(s).
        assert!(out.contains(
            "  available {\n    cve_2026_43499 = [ \"pselect_rootchild\" ]\n    cve_2026_43284 = [ \"umh\" ]\n  }\n"
        ));
        // 43499: steps, then abi{task_struct, cred, kernel, offset}, then the
        // backend-scoped blocks.
        assert!(out.contains(
            "    cve_2026_43499 {\n      steps = \"pselect_rootchild\"\n      abi {\n        task_struct {\n"
        ));
        assert!(out.contains("        task_struct {\n          prio = 132\n"));
        assert!(out.contains("        cred {\n"));
        assert!(out.contains("          caps_offset = 48\n"));
        assert!(out.contains("        kernel {\n          kernel_phys_load = 1073741824\n"));
        assert!(out.contains("        offset {\n"));
        assert!(out.contains("          init_task = 34595456\n"));
        assert!(
            out.contains("      route {\n        select_stack {\n          waiter_shift = -2\n")
        );
        // 43284: steps + the frozen execution literals.
        assert!(out.contains("    cve_2026_43284 {\n      steps = \"umh\"\n      execution {\n"));
        for (key, value) in [
            ("late_load_args", "0"),
            ("selinux_exec_context", "0"),
            ("module_poll_attempts", "40"),
            ("module_poll_interval_ms", "5"),
            ("wait_timeout_ms", "15000"),
        ] {
            assert!(
                out.contains(&format!("        {key} = {value}\n")),
                "43284 execution field {key} missing\n{out}"
            );
        }
        // Deleted owners / keys: any of these coming back is a regression.
        for dead in [
            "  common {",
            "  platform {",
            "  selection {",
            "vr_guard",
            "tracepoint_funcs",
            "defex_symbol",
            "kmi =",
            "lkm_path",
            "carrier_path",
            "countermeasure {",
            "terminal",
        ] {
            assert!(
                !out.contains(dead),
                "removed key came back ({dead}):\n{out}"
            );
        }
    }

    /// Positive: a rendered profile flattens onto EXACTLY the native wire paths.
    /// The profile spelling IS the wire spelling after the rename, so there is no
    /// translation layer left to hide a mismatch.
    #[test]
    fn flatten_conf_values_matches_the_wire_paths_end_to_end() {
        let (symbols, structs) = conf_fixture();
        let geometry: Vec<(&'static str, i64)> = vec![("waiter_shift", -2)];
        let out = render_conf(&ConfInputs {
            release: "5.15.189-android13-8-00016-g51bba4309aac-ab14546557",
            phys: Some(0x4000_0000),
            phys_offset: None,
            symbols: &symbols,
            structs: &structs,
            backend: super::BACKEND_43499,
            route: Some("select_stack"),
            steps_path: "rootchild",
            route_geometry: &geometry,
            cred: &conf_cred_6x(),
            extra_offsets: &no_extra_offsets(),
        });
        let flat = super::flatten_conf_values(&out);
        // Root scalars are BARE wire keys (manifest owner `root`).
        assert_eq!(flat.get("kernel_major").map(String::as_str), Some("5"));
        assert_eq!(flat.get("kernel_minor").map(String::as_str), Some("15"));
        assert_eq!(flat.get("safe_mode").map(String::as_str), Some("false"));
        // The ABI block keeps its profile spelling on the wire.
        assert_eq!(
            flat.get("backend.cve_2026_43499.abi.kernel.kernel_phys_load")
                .map(String::as_str),
            Some("1073741824")
        );
        assert_eq!(
            flat.get("backend.cve_2026_43499.abi.task_struct.prio")
                .map(String::as_str),
            Some("132")
        );
        assert_eq!(
            flat.get("backend.cve_2026_43499.abi.offset.init_task")
                .map(String::as_str),
            Some("34595456")
        );
        /* String literals keep their HOCON quotes in the flattened view (that is
         * what the profile carries); only numbers are bare. */
        assert_eq!(
            flat.get("backend.cve_2026_43499.steps").map(String::as_str),
            Some("\"pselect_rootchild\"")
        );
        assert_eq!(
            flat.get("backend.cve_2026_43499.route.select_stack.waiter_shift")
                .map(String::as_str),
            Some("-2")
        );
        // 43284: profile and wire both spell the tuning `execution.*`.
        assert_eq!(
            flat.get("backend.cve_2026_43284.steps").map(String::as_str),
            Some("\"umh\"")
        );
        assert_eq!(
            flat.get("backend.cve_2026_43284.execution.wait_timeout_ms")
                .map(String::as_str),
            Some("15000")
        );
        // No pre-rename spelling and no wrapper prefix survives.
        assert!(flat.keys().all(|key| !key.starts_with("platform.")));
        assert!(flat.keys().all(|key| !key.starts_with("common.")));
        assert!(flat.keys().all(|key| !key.starts_with("ghostlock.")));
    }

    /// Negative (the point of deleting the transitional layer): the pre-rename
    /// spelling is NOT rewritten any more. A legacy document keeps its legacy
    /// keys, so it matches nothing in the wire vocabulary and is rejected by the
    /// native/App validators instead of being silently accepted.
    #[test]
    fn flatten_conf_values_does_not_rewrite_legacy_paths() {
        let legacy = "ghostlock {\n  common {\n    kernel_major = 5\n    safe_mode = false\n  }\n  platform {\n    abi {\n      offset {\n        init_task = 123\n      }\n    }\n  }\n}\n";
        let flat = super::flatten_conf_values(legacy);
        // The legacy keys survive verbatim ...
        assert_eq!(
            flat.get("common.kernel_major").map(String::as_str),
            Some("5")
        );
        assert_eq!(
            flat.get("common.safe_mode").map(String::as_str),
            Some("false")
        );
        assert_eq!(
            flat.get("platform.abi.offset.init_task")
                .map(String::as_str),
            Some("123")
        );
        // ... and are NOT mapped onto the post-rename names.
        assert_eq!(flat.get("kernel_major"), None);
        assert_eq!(
            flat.get("backend.cve_2026_43499.abi.offset.init_task"),
            None
        );
        // The wire vocabulary only knows the post-rename spelling.
        let wire = super::conf_wire_paths();
        assert!(!wire.contains("platform.abi.offset.init_task"));
        assert!(!wire.contains("common.kernel_major"));
        assert!(wire.contains("backend.cve_2026_43499.abi.offset.init_task"));
        assert!(wire.contains("kernel_major"));
        assert!(wire.contains("kernel_minor"));
        assert!(wire.contains("safe_mode"));
        assert!(wire.contains("backend.cve_2026_43284.execution.wait_timeout_ms"));
    }

    #[test]
    fn conf_61_writes_the_compact_waiter_and_slub_stride() {
        let (symbols, structs) = conf_fixture();
        let geometry: Vec<(&'static str, i64)> = vec![("compact_waiter", 1)];
        let out = render_conf(&ConfInputs {
            release: "6.1.118-android14-11-gca0ef6d17716-ab13624819",
            phys: None,
            phys_offset: None,
            symbols: &symbols,
            structs: &structs,
            backend: super::BACKEND_43499,
            route: Some("tcp_zerocopy"),
            steps_path: "rootchild",
            route_geometry: &geometry,
            cred: &conf_cred_6x(),
            extra_offsets: &no_extra_offsets(),
        });
        assert!(out.contains("tcp_zerocopy {}"));
        assert!(out.contains("compact_waiter = true"));
        assert!(out.contains("mm_struct_sz = 1024"));
        assert!(out.contains("kernel_phys_load = null"));
        assert!(out.contains("steps = \"tcp_rootchild\""));
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
            phys_offset: None,
            symbols: &symbols,
            structs: &structs,
            backend: super::BACKEND_43499,
            route: Some("multicast_waiter"),
            steps_path: "rootchild",
            route_geometry: &geometry,
            cred: &conf_cred_5x(&cred, 176),
            extra_offsets: &ConfExtraOffsets {
                empty_zero_page: Some(47_529_984),
            },
        });
        assert!(out.contains("multicast_waiter {") && out.contains("waiter_off = 96"));
        assert!(out.contains("buffer_size = 264"));
        assert!(out.contains("task_offset = 48"));
        assert!(out.contains("lock_offset = 56"));
        assert!(out.contains("compact_waiter = true"));
        assert!(out.contains("kernelsnitch_collisions = 8"));
        assert!(out.contains("mm_struct_sz = 1024"));
        assert!(out.contains("cred {"));
        assert!(out.contains("copy_size = 176"));
        assert!(out.contains("usage_value = 256"));
        assert!(out.contains("caps_count = 3"));
        assert!(out.contains("caps_value = 2199023255551"));
        assert!(out.contains("ref0_offset = 128"));
        assert!(out.contains("ref3_offset = 152"));
        assert!(out.contains("ref_count = 4"));
        assert!(out.contains("ref0_image = -274698454400"));
        assert!(out.contains("ref3_image = -274698454232"));
        assert!(out.contains("empty_zero_page = 47529984"));
        assert!(out.contains("steps = \"mcast_rootchild\""));
        assert!(!out.contains("w1_w3"));
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
            phys_offset: None,
            symbols: &symbols,
            structs: &structs,
            backend: super::BACKEND_43499,
            route: Some("multicast_waiter"),
            steps_path: "rootchild",
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
            phys_offset: None,
            symbols: &symbols,
            structs: &structs,
            backend: super::BACKEND_43499,
            route: None,
            steps_path: "rootchild",
            route_geometry: &[],
            cred: &[],
            extra_offsets: &no_extra_offsets(),
        });
        assert!(out.contains("kernel {"));
        assert!(out.contains("kernelsnitch_collisions = null"));
        assert!(out.contains("mm_struct_sz = null"));
        // Unknown route: fail closed -- the SELECTED backend gets no token.
        let selected = out.split("cve_2026_43284 {").next().unwrap_or_default();
        assert!(
            !selected.contains("steps = "),
            "selected backend got a token: {out}"
        );
        assert!(!out.contains("cve_2026_43499 = ["));
        // The 43284 block is independent of the 43499 route axis.
        assert!(out.contains("cve_2026_43284 = [ \"umh\" ]"));
        assert!(out.contains("cve_2026_43284 {\n      steps = \"umh\""));
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
            phys_offset: None,
            symbols: &symbols,
            structs: &structs,
            backend: super::BACKEND_43499,
            route: Some("multicast_waiter"),
            steps_path: "rootchild",
            route_geometry: &geometry,
            cred: &[],
            extra_offsets: &no_extra_offsets(),
        });
        // Without this image's static derivation the unverified candidate must
        // not borrow the hardware-probed frame/copy-window constants.
        assert!(out.contains("waiter_off = null"));
        assert!(out.contains("buffer_size = null"));
        assert!(out.contains("task_offset = 48"));
        assert!(out.contains("lock_offset = 56"));
        assert!(out.contains("compact_waiter = true"));
        // The 5.x KernelSnitch defaults are required to run and are emitted even
        // without the "-android13-" train tag.
        assert!(out.contains("kernelsnitch_collisions"));
        assert!(out.contains("kernelsnitch_collisions = 8"));
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

    #[test]
    fn combination_token_covers_the_route_catalog_and_paths() {
        for (route, prefix) in [
            ("multicast_waiter", "mcast"),
            ("select_stack", "pselect"),
            ("tcp_zerocopy", "tcp"),
        ] {
            assert_eq!(route_token_prefix(route), Some(prefix));
            let expected = format!("{prefix}_rootchild");
            assert_eq!(
                combination_token(super::BACKEND_43499, Some(route), "rootchild").as_deref(),
                Some(expected.as_str())
            );
        }
        // 43499 planned paths keep the route prefix.
        assert_eq!(
            combination_token(super::BACKEND_43499, Some("multicast_waiter"), "umh").as_deref(),
            Some("mcast_umh")
        );
        // 43284 has no route axis: the token is the bare path name.
        for path in ["umh", "rootchild", "shizuku"] {
            assert_eq!(
                combination_token(super::BACKEND_43284, None, path).as_deref(),
                Some(path)
            );
        }
    }

    #[test]
    fn combination_token_fails_closed_on_unknown_inputs() {
        // Missing route on a route-axis backend: no token, no guess.
        assert_eq!(
            combination_token(super::BACKEND_43499, None, "rootchild"),
            None
        );
        // Unknown route, including the removed Auto axis.
        assert_eq!(
            combination_token(super::BACKEND_43499, Some("auto"), "rootchild"),
            None
        );
        // Unknown path, on either backend.
        assert_eq!(
            combination_token(super::BACKEND_43499, Some("multicast_waiter"), "bogus"),
            None
        );
        assert_eq!(combination_token(super::BACKEND_43284, None, "bogus"), None);
        // Unknown backend.
        assert_eq!(combination_token("cve_2026_99999", None, "umh"), None);
    }

    #[test]
    fn path_terminal_maps_the_three_paths() {
        assert_eq!(path_terminal("rootchild"), Some("root_child"));
        assert_eq!(path_terminal("shizuku"), Some("root_child"));
        assert_eq!(path_terminal("umh"), Some("umh_forward"));
        assert_eq!(path_terminal("bogus"), None);
    }

    /* The old 'flatten_conf' helper (wrapper-preserving, no HOCON->wire
     * translation) was replaced wholesale by 'super::flatten_conf_values', which
     * is what the App and the plugin R1 lookup actually share. */

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
            phys_offset: None,
            symbols: &symbols,
            structs: &structs,
            backend: super::BACKEND_43499,
            route: Some("multicast_waiter"),
            steps_path: "rootchild",
            route_geometry: &geometry,
            cred: &cred,
            extra_offsets: &extra,
        });
        let bundled = std::fs::read_to_string(concat!(
            env!("CARGO_MANIFEST_DIR"),
            "/../../app/src/main/assets/profile/5.15.189-android13-8-00016-g51bba4309aac-ab14546557.conf"
        ))
        .expect("bundled 5.15.189 profile");
        /* Keys that exist ONLY because of the HOCON refactor: the pre-refactor
         * bundle cannot carry them (the root safe_mode/kernel_minor scalars and
         * the whole second-backend block are new). */
        /* Root scalars the bundled reference profile does not carry yet (the
         * rename moved them OUT of the deleted `common` owner). */
        const REFACTOR_ONLY: &[&str] = &["kernel_minor", "safe_mode"];
        /* flatten_conf_values strips the wrapper AND translates the refactored
         * source layout onto wire paths, so the new-shape generated document and
         * the old-shape bundled profile become directly comparable. */
        let generated = super::flatten_conf_values(&generated);
        let bundled = super::flatten_conf_values(&bundled);

        assert!(generated.contains_key("backend.cve_2026_43499.route.multicast_waiter.waiter_off"));
        /* The bundled profile is still in the PRE-refactor shape; flattening both
         * documents onto wire paths makes the two comparable field by field. */
        for (key, value) in &generated {
            match bundled.get(key) {
                Some(bundled_value) => assert_eq!(
                    value, bundled_value,
                    "field {key} differs between generated and bundled profile"
                ),
                /* Fields the bundled reference profile does not carry yet:
                 * `available.*` is profile-only (the App turns it into UI choices
                 * and the wire receives `backend.<id>.steps`), and the generated
                 * profile always declares the second backend's block. */
                None => assert!(
                    REFACTOR_ONLY.contains(&key.as_str())
                        || key.starts_with("available.")
                        || key.starts_with("backend.cve_2026_43284."),
                    "generated profile has unexpected field {key}"
                ),
            }
        }
        // The deleted vr_guard/defex fields must not come back through the bundle.
        assert!(!generated.contains_key("common.vr_guard"));
        assert!(!generated.keys().any(|key| key.starts_with("common.")));
        assert!(!generated.keys().any(|key| key.starts_with("platform.")));
        assert!(
            !generated
                .keys()
                .any(|key| key.contains("vivo_vr_guard") || key.contains("defex"))
        );
    }

    /// S4 R2 three-end manifest agreement (extractor leg): every owner-qualified
    /// `section.key` the extractor can emit must be declared by the native GLKv3
    /// owner FieldSpecs. A rename on either side fails here instead of surfacing
    /// as a startup rejection in the field.
    #[test]
    fn conf_wire_fields_are_in_the_native_glkv3_manifest() {
        let manifest = std::fs::read_to_string(concat!(
            env!("CARGO_MANIFEST_DIR"),
            "/../../app/src/test/resources/profile-manifest-v3.tsv"
        ))
        .expect("native GLKv3 owner manifest");
        let paths: BTreeSet<String> = manifest
            .lines()
            .filter(|line| {
                let trimmed = line.trim();
                !trimmed.is_empty() && !trimmed.starts_with('#')
            })
            .map(|line| {
                let path = line.split('\t').nth(1).expect("manifest path column");
                path.to_string()
            })
            .collect();
        assert!(!paths.is_empty(), "manifest is empty");
        for (section, key) in super::conf_wire_fields() {
            let path = super::wire_path(section, key);
            assert!(
                paths.contains(&path),
                "extractor path {path} is missing from the native GLKv3 owner manifest"
            );
        }
    }

    /// S4 R6b/F4 three-end combination agreement (extractor leg): every token
    /// this crate can render must be a catalogue row of the native combination
    /// manifest, and the extractor vocabulary must not invent a third spelling.
    ///
    /// The extractor renders cve_2026_43499 profiles (`--route` selects the
    /// route, `--steps-path` the path). Its `*_umh` projections are catalogued
    /// but PLANNED (available=0): the extractor may render them and the device
    /// selection gate rejects them, so that projection is asserted explicitly
    /// instead of being silently treated as wired.
    #[test]
    fn conf_combination_tokens_are_in_the_native_combination_manifest() {
        let manifest = std::fs::read_to_string(concat!(
            env!("CARGO_MANIFEST_DIR"),
            "/../../app/src/test/resources/combination-manifest.tsv"
        ))
        .expect("native combination manifest");

        /* token -> available; every row carries the 8 documented columns. */
        let mut tokens: BTreeMap<String, bool> = BTreeMap::new();
        for line in manifest.lines() {
            let line = line.trim_end_matches('\r');
            if line.trim().is_empty() || line.starts_with('#') {
                continue;
            }
            let columns: Vec<&str> = line.split('\t').collect();
            assert_eq!(columns.len(), 8, "combination manifest row: {line}");
            let available = match columns[6] {
                "1" => true,
                "0" => false,
                other => panic!("available column must be 1 or 0, got {other}"),
            };
            assert!(
                tokens.insert(columns[0].to_string(), available).is_none(),
                "duplicate combination token {}",
                columns[0]
            );
        }
        assert_eq!(
            tokens.len(),
            12,
            "manifest must list the 12 catalogue tokens"
        );

        /* Everything the extractor CLI can render: 43499 x 3 routes x 3 paths. */
        let mut emitted = 0usize;
        for route in ["tcp_zerocopy", "select_stack", "multicast_waiter"] {
            for path in ["rootchild", "shizuku", "umh"] {
                let token = combination_token(super::BACKEND_43499, Some(route), path)
                    .expect("extractor renders this route/path pair");
                let available = *tokens.get(&token).unwrap_or_else(|| {
                    panic!("extractor token {token} is missing from the native manifest")
                });
                assert_eq!(
                    available,
                    path != "umh",
                    "availability drift for extractor token {token}"
                );
                emitted += 1;
            }
        }
        assert_eq!(emitted, 9);

        /* Route/path vocabulary agreement: the manifest columns use the same
         * spellings the extractor composes tokens from. */
        for route in ["tcp_zerocopy", "select_stack", "multicast_waiter"] {
            assert!(super::route_token_prefix(route).is_some(), "{route}");
        }
        for path in ["rootchild", "shizuku", "umh"] {
            assert!(super::path_terminal(path).is_some(), "{path}");
        }
    }

    /// A rendered candidate profile the plugin projection can read back.
    fn plugin_conf() -> String {
        let (symbols, structs) = conf_fixture();
        let geometry: Vec<(&'static str, i64)> = vec![("waiter_shift", -2)];
        render_conf(&ConfInputs {
            release: "6.1.145-android14-11-maybe-dirty",
            phys: None,
            phys_offset: None,
            symbols: &symbols,
            structs: &structs,
            backend: super::BACKEND_43499,
            route: Some("select_stack"),
            steps_path: "rootchild",
            route_geometry: &geometry,
            cred: &conf_cred_6x(),
            extra_offsets: &no_extra_offsets(),
        })
    }

    #[test]
    fn flatten_conf_values_reads_back_the_rendered_profile() {
        let flat = flatten_conf_values(&plugin_conf());
        /* The ghostlock wrapper is stripped, exactly as the App unwraps it. */
        assert_eq!(flat.get("kernel_major").map(String::as_str), Some("6"));
        assert_eq!(
            flat.get("backend.cve_2026_43499.abi.task_struct.prio")
                .map(String::as_str),
            Some("132")
        );
        assert_eq!(
            flat.get("backend.cve_2026_43499.abi.offset.init_task")
                .map(String::as_str),
            Some("34595456")
        );
        assert_eq!(
            flat.get("backend.cve_2026_43499.route.select_stack.waiter_shift")
                .map(String::as_str),
            Some("-2")
        );
        assert_eq!(
            flat.get("backend.cve_2026_43499.steps")
                .map(|value| value.trim_matches('"')),
            Some("pselect_rootchild")
        );
        assert!(!flat.contains_key("ghostlock.schema_version"));
    }

    #[test]
    fn plugin_block_is_appended_before_the_final_brace_and_round_trips() {
        let base = plugin_conf();
        /* No plugins: the rendered profile is returned byte for byte. */
        assert_eq!(append_plugin_block(&base, &[]), base);
        assert_eq!(
            append_plugin_block(
                &base,
                &[ResolvedPlugin {
                    id: "empty.plugin".to_string(),
                    entries: Vec::new(),
                }],
            ),
            base
        );

        let plugins = vec![ResolvedPlugin {
            id: "test.schema".to_string(),
            entries: vec![
                (
                    "task_defex_enforce".to_string(),
                    ExtractValue::UInt(46_205_952),
                ),
                (
                    "backend.cve_2026_43499.steps".to_string(),
                    ExtractValue::Str("pselect_rootchild".to_string()),
                ),
                (
                    "backend.cve_2026_43499.kernel.compact_waiter".to_string(),
                    ExtractValue::Bool(true),
                ),
                ("waiter_shift".to_string(), ExtractValue::Int(-2)),
            ],
        }];
        let augmented = append_plugin_block(&base, &plugins);
        /* The exact emitted layout is a contract for the App's HOCON parser:
         * owner blocks, two-space nesting, dotted keys quoted. */
        /* A dotted plugin id is quoted: bare, HOCON would nest it. */
        let block = "  plugin {\n    \"test.schema\" {\n      extract {\n        task_defex_enforce = 46205952\n        \"backend.cve_2026_43499.steps\" = \"pselect_rootchild\"\n        \"backend.cve_2026_43499.kernel.compact_waiter\" = true\n        waiter_shift = -2\n      }\n    }\n  }\n";
        assert!(
            augmented.contains(block),
            "plugin block layout drifted:\n{augmented}"
        );
        let flat = flatten_conf_values(&augmented);
        assert_eq!(
            flat.get("plugin.test.schema.extract.task_defex_enforce")
                .map(String::as_str),
            Some("46205952")
        );
        assert_eq!(
            flat.get("plugin.test.schema.extract.backend.cve_2026_43499.kernel.compact_waiter")
                .map(String::as_str),
            Some("true")
        );
        assert_eq!(
            flat.get("plugin.test.schema.extract.waiter_shift")
                .map(String::as_str),
            Some("-2")
        );
        assert_eq!(
            flat.get("plugin.test.schema.extract.backend.cve_2026_43499.steps")
                .map(|value| value.trim_matches('"')),
            Some("pselect_rootchild")
        );
        /* Only extract is written: enabled/stage/module_path/module_hash stay
         * the App registry's authority. */
        for key in ["enabled", "stage", "module_path", "module_hash"] {
            assert!(
                !flat.contains_key(&format!("plugin.test.schema.{key}")),
                "the extractor must not write plugin.test.schema.{key}"
            );
        }
        /* Every base field survived unchanged. */
        let base_flat = flatten_conf_values(&base);
        assert!(!base_flat.is_empty());
        for (key, value) in &base_flat {
            assert_eq!(flat.get(key), Some(value), "base field {key} changed");
        }
    }

    /// A declared path matches either an exact manifest row or one of the two
    /// declared dynamic rows (the only wildcards the manifest may carry).
    fn manifest_declares(declared: &[(String, String)], path: &str) -> bool {
        for (pattern, _) in declared {
            if pattern == path {
                return true;
            }
            let Some(prefix) = pattern.strip_suffix('*') else {
                continue;
            };
            let Some((head, tail)) = prefix.split_once("<id>") else {
                continue;
            };
            if !path.starts_with(head) {
                continue;
            }
            let rest = &path[head.len()..];
            if let Some(at) = rest.find(tail) {
                if at > 0 && at + tail.len() < rest.len() {
                    return true;
                }
            }
        }
        false
    }

    /// S4 P2 three-end agreement (extractor leg): every plugin extract path this
    /// crate can emit is declared by the native GLKv3 owner manifest, and the
    /// dynamic rows keep the frozen four-member union spelling. A drift on
    /// either side fails here instead of surfacing as a startup rejection.
    #[test]
    fn conf_plugin_extract_fields_are_in_the_native_glkv3_manifest() {
        let manifest = std::fs::read_to_string(concat!(
            env!("CARGO_MANIFEST_DIR"),
            "/../../app/src/test/resources/profile-manifest-v3.tsv"
        ))
        .expect("native GLKv3 owner manifest");
        let declared: Vec<(String, String)> = manifest
            .lines()
            .map(str::trim)
            .filter(|line| !line.is_empty() && !line.starts_with('#'))
            .map(|line| {
                let columns: Vec<&str> = line.split('\t').collect();
                assert!(columns.len() >= 3, "manifest row: {line}");
                (columns[1].to_string(), columns[2].to_string())
            })
            .collect();
        let wildcards: Vec<&(String, String)> = declared
            .iter()
            .filter(|(path, _)| path.contains('*'))
            .collect();
        let wildcard_paths: Vec<&str> = wildcards.iter().map(|(path, _)| path.as_str()).collect();
        assert_eq!(
            wildcard_paths,
            ["plugin.<id>.extract.*", "plugin.<id>.params.*"],
            "the manifest's dynamic rows are the frozen two plugin wildcards"
        );
        for (path, wire) in wildcards {
            assert_eq!(wire, "uint|int|bool|str", "union spelling of {path}");
        }
        /* The matcher is not vacuous. */
        assert!(!manifest_declares(
            &declared,
            "plugin.test.schema.bogus.key"
        ));
        assert!(!manifest_declares(&declared, "plugin.test.schema.extract."));

        /* Resolve through the real path: R1 (profile path), R2 (kallsyms symbol)
         * and R1 str. R3 is covered by the plugin module tests. */
        let base = plugin_conf();
        let profile = flatten_conf_values(&base);
        let paths = conf_wire_paths();
        let mut symbols: BTreeMap<String, BTreeSet<u64>> = BTreeMap::new();
        symbols.insert(
            "task_defex_enforce".to_string(),
            BTreeSet::from([0xffff_8000_0000_0000u64 + 0x2c0_0000]),
        );
        let context = ExtractContext {
            profile: &profile,
            profile_paths: &paths,
            btf: None,
            symbols: &symbols,
            base: 0xffff_8000_0000_0000,
        };
        let declared_field = |name: &str, kind: ExtractKind| DeclaredField {
            name: name.to_string(),
            kind,
            required: true,
            default: None,
            doc: String::new(),
        };
        let descriptor = PluginDescriptor {
            id: "test.schema".to_string(),
            version: "1.0".to_string(),
            sha256: "0".repeat(64),
            stages: BTreeSet::new(),
            required_caps: BTreeSet::new(),
            hooks: Vec::new(),
            params: Vec::new(),
            specs: Vec::new(),
            extract: vec![
                declared_field(
                    "backend.cve_2026_43499.abi.task_struct.prio",
                    ExtractKind::UInt,
                ),
                declared_field("backend.cve_2026_43499.steps", ExtractKind::Str),
                declared_field("task_defex_enforce", ExtractKind::UInt),
            ],
            rejects: Vec::new(),
        };
        let outcome = resolve_descriptor(&descriptor, &context).expect("descriptor resolves");
        assert_eq!(outcome.entries.len(), 3);
        let rendered = append_plugin_block(
            &base,
            &[ResolvedPlugin {
                id: descriptor.id.clone(),
                entries: outcome.entries,
            }],
        );
        let flat = flatten_conf_values(&rendered);
        let emitted: Vec<&String> = flat
            .keys()
            .filter(|key| key.starts_with("plugin."))
            .collect();
        assert_eq!(emitted.len(), 3);
        for path in emitted {
            assert!(
                manifest_declares(&declared, path),
                "extractor path {path} is missing from the native GLKv3 owner manifest"
            );
        }
    }
}
