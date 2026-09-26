//! Symbol resolution tables and ashmem file_operations scanning.

use std::collections::{BTreeMap, BTreeSet};

use crate::btf::Btf;
use crate::kallsyms::unique;

pub const SYMBOLS: &[(&str, &str)] = &[
    ("off_init_task", "init_task"),
    ("off_init_cred", "init_cred"),
    ("off_root_task_group", "root_task_group"),
    ("off_selinux_enforcing", "selinux_state"),
    ("off_selinux_blob_sizes", "selinux_blob_sizes"),
    ("off_security_hook_heads", "security_hook_heads"),
    ("off_slide_nfulnl_logger", "nfulnl_logger"),
    ("off_slide_boot_id", "sysctl_bootid"),
];

/// GKI kernels drop some data symbols; unresolved optionals emit 0 and the
/// runtime falls back to target.h defaults.
pub const OPTIONAL_SYMBOLS: &[&str] = &["off_security_hook_heads"];

/// struct name -> (offset macro, BTF field)
pub const STRUCT_FIELDS: &[(&str, &[(&str, &str)])] = &[
    (
        "task_struct",
        &[
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
        ],
    ),
    (
        "rt_mutex_waiter",
        &[
            // 6.6+ names the rb_nodes tree/pi_tree; 6.1 calls them
            // tree_entry/pi_tree_entry (both are plain members, same layout).
            ("waiter_tree", "tree"),
            ("waiter_pi_tree", "pi_tree"),
            ("waiter_task", "task"),
            ("waiter_lock", "lock"),
            ("waiter_wake_state", "wake_state"),
            ("waiter_ww_ctx", "ww_ctx"),
            ("waiter_tree", "tree_entry"),
            ("waiter_pi_tree", "pi_tree_entry"),
        ],
    ),
    (
        "cred",
        &[
            ("cred_uid", "uid"),
            ("cred_securebits", "securebits"),
            ("cred_caps", "cap_inheritable"),
            ("cred_security", "security"),
        ],
    ),
    (
        "seccomp",
        &[
            ("seccomp_mode", "mode"),
            ("seccomp_filter_count", "filter_count"),
            ("seccomp_filter", "filter"),
        ],
    ),
];

pub type ResolvedSymbols = BTreeMap<String, Option<u64>>;

pub fn resolve_symbols(symbols: &BTreeMap<String, BTreeSet<u64>>, base: u64) -> ResolvedSymbols {
    let mut result: ResolvedSymbols = BTreeMap::new();
    for (name, symbol) in SYMBOLS {
        result.insert((*name).to_string(), unique(symbols, symbol));
    }
    result.insert(
        "off_slide_loggers_0_1".to_string(),
        unique(symbols, "loggers").map(|value| value + 0x10),
    );
    result
        .iter_mut()
        .for_each(|(_, value)| *value = value.and_then(|v| v.checked_sub(base)));
    result
}

/// Layout template selector for a release series. A returned template is not
/// sufficient evidence to emit family-derived geometry; use
/// `kernel_layout_verified` for that decision.
pub fn kernel_struct_macro(release: Option<&str>) -> Option<&'static str> {
    let release = release?;
    let mut parts = release.split('.');
    let major = parts.next()?.parse::<u32>().ok()?;
    let minor = parts.next()?.parse::<u32>().ok()?;
    match (major, minor) {
        // 6.1 android14 builds use the flat compact-waiter layout.
        (6, 1) => Some("STRUCT_OFFSETS_6_1"),
        (6, 6) => Some("STRUCT_OFFSETS_6_6"),
        (6, 12) => Some("STRUCT_OFFSETS_6_12"),
        _ => None,
    }
}

/// Whether this release belongs to a layout family with retained verification
/// evidence. Verification is per Android train, the same way the 6.x templates
/// are keyed: `android14-6.1`, `android15-6.6`, `android16-6.12`, and
/// `android13-5.15` (whose multicast layout was measured on the A301SO image).
/// A mere `major.minor` match without the train suffix is not verified.
pub fn kernel_layout_verified(release: Option<&str>) -> bool {
    let Some(release) = release else {
        return false;
    };
    let mut parts = release.split('.');
    let major = parts.next().and_then(|part| part.parse::<u32>().ok());
    let minor = parts.next().and_then(|part| part.parse::<u32>().ok());
    match (major, minor) {
        (Some(6), Some(1)) => release.contains("-android14-"),
        (Some(6), Some(6)) => release.contains("-android15-"),
        (Some(6), Some(12)) => release.contains("-android16-"),
        (Some(5), Some(15)) => release.contains("-android13-"),
        _ => false,
    }
}

/// The exact release whose full multicast geometry (including the forged-object
/// placement) was measured on hardware. Other releases on the same train
/// inherit only the corroborated fields.
pub const MULTICAST_DEVICE_RELEASE: &str = "5.15.189-android13-8-00016-g51bba4309aac-ab14546557";

/// Whether the device-measured geometry may be emitted: only for the exact
/// release whose forged-object placement was validated, never by train.
pub fn kernel_device_geometry_verified(release: Option<&str>) -> bool {
    release == Some(MULTICAST_DEVICE_RELEASE)
}

pub type ResolvedStructs = BTreeMap<String, Option<u32>>;

pub fn resolve_structs(btf: Option<&Btf>) -> ResolvedStructs {
    let mut result: ResolvedStructs = BTreeMap::new();
    let Some(btf) = btf else {
        for (_, fields) in STRUCT_FIELDS {
            for (macro_name, _) in *fields {
                result.insert((*macro_name).to_string(), None);
            }
        }
        result.insert("struct_page_size".to_string(), None);
        result.insert("struct_page_compound_head".to_string(), None);
        result.insert("struct_page_type".to_string(), None);
        result.insert("struct_slab_cache".to_string(), None);
        result.insert("struct_mm_struct".to_string(), None);
        return result;
    };
    for (struct_name, fields) in STRUCT_FIELDS {
        if btf.named_struct(struct_name).is_none() {
            for (macro_name, _) in *fields {
                result.insert((*macro_name).to_string(), None);
            }
            continue;
        }
        for (macro_name, field_name) in *fields {
            let value = btf.field(struct_name, field_name);
            // Alias entries (e.g. tree/tree_entry) resolve on one kernel
            // naming only; never clobber a resolved value with a miss.
            match result.get(*macro_name).copied().flatten() {
                Some(old) if value.is_none() => {
                    result.insert((*macro_name).to_string(), Some(old));
                }
                _ => {
                    result.insert((*macro_name).to_string(), value);
                }
            }
        }
    }
    result.insert("struct_page_size".to_string(), btf.size("page"));
    result.insert(
        "struct_page_compound_head".to_string(),
        btf.field("page", "compound_head"),
    );
    result.insert(
        "struct_page_type".to_string(),
        btf.field("page", "page_type"),
    );
    result.insert(
        "struct_slab_cache".to_string(),
        btf.field("slab", "slab_cache"),
    );
    result.insert("struct_mm_struct".to_string(), btf.size("mm_struct"));
    result
}

#[cfg(test)]
mod tests {
    use super::{kernel_layout_verified, kernel_struct_macro};

    #[test]
    fn layout_template_and_verification_are_separate() {
        assert_eq!(
            kernel_struct_macro(Some("6.1.162-android14-11-build")),
            Some("STRUCT_OFFSETS_6_1")
        );
        assert!(kernel_layout_verified(Some("6.1.162-android14-11-build")));
        assert!(!kernel_layout_verified(Some("6.1.162-generic")));
        assert!(!kernel_layout_verified(Some("6.7.1-android16-1-build")));
    }

    #[test]
    fn five_fifteen_layout_verification_follows_the_android13_train() {
        assert!(kernel_layout_verified(Some(
            "5.15.189-android13-8-00016-g51bba4309aac-ab14546557"
        )));
        assert!(kernel_layout_verified(Some("5.15.189-android13-8-other")));
        assert!(!kernel_layout_verified(Some("5.15.189-generic")));
        assert!(!kernel_layout_verified(Some(
            "5.15.178-g3575c47dc7ce-dirty"
        )));
    }
}
