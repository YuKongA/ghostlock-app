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
pub const OPTIONAL_SYMBOLS: &[&str] = &[
    "off_security_hook_heads",
];

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

pub fn resolve_symbols(
    symbols: &BTreeMap<String, BTreeSet<u64>>,
    base: u64,
) -> ResolvedSymbols {
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

pub fn kernel_struct_macro(release: Option<&str>) -> &'static str {
    if let Some(release) = release {
        let mut parts = release.split('.');
        if let (Some(major), Some(minor)) = (parts.next(), parts.next()) {
            if let (Ok(major), Ok(minor)) = (major.parse::<u32>(), minor.parse::<u32>()) {
                if (major, minor) >= (6, 12) {
                    return "STRUCT_OFFSETS_6_12";
                }
                // 6.1 android14 builds use the flat compact-waiter layout.
                if (major, minor) <= (6, 1) {
                    return "STRUCT_OFFSETS_6_1";
                }
            }
        }
    }
    "STRUCT_OFFSETS_6_6"
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
    result.insert("struct_page_type".to_string(), btf.field("page", "page_type"));
    result.insert(
        "struct_slab_cache".to_string(),
        btf.field("slab", "slab_cache"),
    );
    result.insert("struct_mm_struct".to_string(), btf.size("mm_struct"));
    result
}
