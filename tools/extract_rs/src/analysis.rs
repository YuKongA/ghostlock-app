//! Kernel analysis report for `--analysis`: kernel family, waiter layout, the
//! primitive status and the route candidates.
//!
//! The report is read-only - it never writes offsets. The suggested route is a
//! starting point based on kernel evidence (waiter layout, pselect derivation,
//! path presence); a device gate with the fixed CPU pair still decides whether
//! a route is supported.

use std::collections::{BTreeMap, BTreeSet};

use crate::btf::Btf;
use crate::derive::{PSELECT_ROUTE_NFDS, PselectLayout, RelSymbols, derive_pselect_layout};
use crate::error::ExtractError;
use crate::kallsyms;
use crate::symbols::kernel_struct_macro;

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Confidence {
    High,
    Medium,
    Low,
}

impl Confidence {
    fn label(self) -> &'static str {
        match self {
            Confidence::High => "high",
            Confidence::Medium => "medium",
            Confidence::Low => "low",
        }
    }
}

pub enum PselectOutcome {
    Derived(Box<PselectLayout>),
    Infeasible(String),
    Failed(String),
    NotAttempted(&'static str),
}

pub struct PathCandidate {
    pub route: &'static str,
    /// (symbol, address) probed for this path; an address is None when absent.
    pub probes: Vec<(&'static str, Option<u64>)>,
    pub available: bool,
}

pub struct Analysis {
    pub release: Option<String>,
    pub kernel_major: Option<u32>,
    pub kernel_minor: Option<u32>,
    pub family: Option<&'static str>,
    pub phys: Option<u64>,
    pub phys_source: &'static str,
    pub waiter_fields: BTreeMap<String, u32>,
    pub waiter_size: Option<u32>,
    pub mm_struct_size: Option<u32>,
    pub primitive: Result<u64, String>,
    pub pselect: PselectOutcome,
    pub paths: Vec<PathCandidate>,
    pub suggestion: Option<&'static str>,
    pub suggestion_confidence: Confidence,
    pub suggestion_reasons: Vec<String>,
}

pub struct Input<'a> {
    pub release: Option<&'a str>,
    pub kernel: &'a [u8],
    pub symbols: &'a BTreeMap<String, BTreeSet<u64>>,
    pub rel_symbols: &'a RelSymbols,
    pub sorted_offsets: &'a [u64],
    pub btf: Option<&'a Btf>,
    pub phys: Option<u64>,
    pub phys_source: &'static str,
    pub primitive: Result<u64, ExtractError>,
    pub allow_disasm: bool,
}

fn parse_major_minor(release: Option<&str>) -> (Option<u32>, Option<u32>) {
    let Some(release) = release else {
        return (None, None);
    };
    let mut parts = release.split('.');
    let major = parts.next().and_then(|part| part.parse::<u32>().ok());
    let minor = parts.next().and_then(|part| part.parse::<u32>().ok());
    (major, minor)
}

fn probe(
    symbols: &BTreeMap<String, BTreeSet<u64>>,
    exact: &str,
    fragments: &[&str],
) -> Option<u64> {
    kallsyms::find_function(symbols, exact, fragments)
}

pub fn build(input: Input<'_>) -> Analysis {
    let (kernel_major, kernel_minor) = parse_major_minor(input.release);
    let family = kernel_struct_macro(input.release);

    let mut waiter_fields = BTreeMap::new();
    let mut waiter_size = None;
    let mut mm_struct_size = None;
    if let Some(btf) = input.btf {
        for field in [
            "tree", "tree_entry", "pi_tree", "pi_tree_entry", "task", "lock", "wake_state",
            "ww_ctx",
        ] {
            if let Some(offset) = btf.field("rt_mutex_waiter", field) {
                waiter_fields.insert(field.to_string(), offset);
            }
        }
        waiter_size = btf.size("rt_mutex_waiter");
        mm_struct_size = btf.size("mm_struct");
    }

    let pselect = match (input.allow_disasm, input.btf) {
        (false, _) => PselectOutcome::NotAttempted("--no-disasm"),
        (true, None) => PselectOutcome::NotAttempted("no embedded BTF"),
        (true, Some(btf)) => match derive_pselect_layout(
            input.kernel,
            input.rel_symbols,
            input.sorted_offsets,
            btf,
            PSELECT_ROUTE_NFDS,
        ) {
            Ok(layout) => PselectOutcome::Derived(Box::new(layout)),
            Err(ExtractError::Infeasible(message)) => PselectOutcome::Infeasible(message),
            Err(err) => PselectOutcome::Failed(err.to_string()),
        },
    };

    let build_path = |route: &'static str,
                      probes: Vec<(&'static str, Option<u64>)>,
                      require_all: bool| {
        let available = if require_all {
            probes.iter().all(|(_, address)| address.is_some())
        } else {
            probes.iter().any(|(_, address)| address.is_some())
        };
        PathCandidate { route, probes, available }
    };
    let mut paths = vec![
        build_path(
            "select_stack",
            vec![
                ("core_sys_select", probe(input.symbols, "core_sys_select", &["core_sys_select"])),
                ("futex_wait", probe(input.symbols, "futex_wait", &["futex_wait"])),
            ],
            true,
        ),
        build_path(
            "tcp_zerocopy",
            vec![(
                "tcp_zerocopy_receive",
                probe(input.symbols, "tcp_zerocopy_receive", &["zerocopy_receive"]),
            )],
            false,
        ),
        build_path(
            "multicast_waiter",
            vec![
                ("ip_mc_msfadd", probe(input.symbols, "ip_mc_msfadd", &["ip_mc_msfadd"])),
                ("ip_mc_source", probe(input.symbols, "ip_mc_source", &["ip_mc_source"])),
            ],
            false,
        ),
    ];
    paths.sort_by_key(|path| path.route);

    let pselect_derived = matches!(pselect, PselectOutcome::Derived(_));
    let available = |route: &str| {
        paths
            .iter()
            .any(|path| path.route == route && path.available)
    };
    let (suggestion, suggestion_confidence, suggestion_reasons) =
        suggest(pselect_derived, &available, family, kernel_major, kernel_minor);

    Analysis {
        release: input.release.map(str::to_string),
        kernel_major,
        kernel_minor,
        family,
        phys: input.phys,
        phys_source: input.phys_source,
        waiter_fields,
        waiter_size,
        mm_struct_size,
        primitive: input.primitive.map_err(|err| err.to_string()),
        pselect,
        paths,
        suggestion,
        suggestion_confidence,
        suggestion_reasons,
    }
}

fn suggest(
    pselect_derived: bool,
    available: &dyn Fn(&str) -> bool,
    family: Option<&'static str>,
    major: Option<u32>,
    minor: Option<u32>,
) -> (Option<&'static str>, Confidence, Vec<String>) {
    let mut reasons = Vec::new();
    if pselect_derived {
        reasons.push("pselect/futex waiter layout derived from the kernel".to_string());
        return (Some("select_stack"), Confidence::High, reasons);
    }
    if major == Some(5) && available("multicast_waiter") {
        reasons.push("5.x kernel with the multicast path present".to_string());
        reasons.push("pselect route not derivable on this layout".to_string());
        return (Some("multicast_waiter"), Confidence::Medium, reasons);
    }
    if major == Some(6) && minor == Some(1) && available("tcp_zerocopy") {
        reasons.push("android14-6.1 compact-waiter family with the tcp path present".to_string());
        return (Some("tcp_zerocopy"), Confidence::Medium, reasons);
    }
    match family {
        Some("STRUCT_OFFSETS_6_6") | Some("STRUCT_OFFSETS_6_12") => {
            reasons.push("family default; the pselect layout was not derived".to_string());
            (Some("select_stack"), Confidence::Low, reasons)
        }
        Some("STRUCT_OFFSETS_6_1") => {
            reasons.push("family default; compact-waiter layout".to_string());
            (Some("tcp_zerocopy"), Confidence::Low, reasons)
        }
        _ => {
            reasons.push(
                "no verified family and no derivable route; review the paths above and pass \
                 --route once confirmed"
                    .to_string(),
            );
            (None, Confidence::Low, reasons)
        }
    }
}

fn render_waiter(fields: &BTreeMap<String, u32>, size: Option<u32>) -> String {
    let mut parts: Vec<String> = fields
        .iter()
        .map(|(name, offset)| format!("{name}=0x{offset:x}"))
        .collect();
    if let Some(size) = size {
        parts.push(format!("size=0x{size:x}"));
    }
    if parts.is_empty() {
        "(no rt_mutex_waiter type in BTF)".to_string()
    } else {
        parts.join(" ")
    }
}

fn render_pselect(pselect: &PselectOutcome) -> String {
    match pselect {
        PselectOutcome::Derived(layout) => {
            let shift = layout.shift as i64 - 2;
            format!(
                "derived shift={shift} waiter=0x{:x} chain={}",
                layout.waiter_local, layout.chain
            )
        }
        PselectOutcome::Infeasible(message) => format!("not derivable: {message}"),
        PselectOutcome::Failed(message) => format!("derivation failed: {message}"),
        PselectOutcome::NotAttempted(reason) => format!("not attempted ({reason})"),
    }
}

pub fn render(analysis: &Analysis) -> String {
    let mut lines = Vec::new();
    let field = |name: &str, value: String| format!("{name:<23} {value}");

    lines.push("== GhostLock kernel analysis ==".to_string());
    lines.push(field(
        "release",
        analysis.release.clone().unwrap_or_else(|| "(none)".to_string()),
    ));
    lines.push(field(
        "kernel_major",
        match (analysis.kernel_major, analysis.kernel_minor) {
            (Some(major), Some(minor)) => format!("{major}.{minor}"),
            _ => "(unknown)".to_string(),
        },
    ));
    lines.push(field(
        "kernel_family",
        match analysis.family {
            Some(macro_name) => format!("{macro_name} (verified)"),
            None => "(unverified; the extractor falls back to the 6.6 layout)".to_string(),
        },
    ));
    lines.push(field(
        "kernel_phys_load",
        match analysis.phys {
            Some(phys) => format!("0x{phys:x} ({})", analysis.phys_source),
            None if analysis.phys_source == "unset" => {
                "unset (no xbl_config.img; pass --phys or let the runtime derive it)".to_string()
            }
            None => format!("unset ({})", analysis.phys_source),
        },
    ));
    lines.push(field(
        "rt_mutex_waiter",
        render_waiter(&analysis.waiter_fields, analysis.waiter_size),
    ));
    lines.push(field(
        "mm_struct size",
        analysis
            .mm_struct_size
            .map(|size| format!("0x{size:x}"))
            .unwrap_or_else(|| "(unavailable)".to_string()),
    ));
    lines.push(field(
        "primitive",
        match &analysis.primitive {
            Ok(remove_waiter) => format!("remove_waiter@0x{remove_waiter:x} unpatched"),
            Err(message) => format!("unavailable: {message}"),
        },
    ));
    lines.push(field("pselect chain", render_pselect(&analysis.pselect)));
    for path in &analysis.paths {
        let state = if path.available { "available" } else { "missing" };
        let probes: Vec<String> = path
            .probes
            .iter()
            .map(|(name, address)| match address {
                Some(address) => format!("{name}@0x{address:x}"),
                None => format!("{name}=absent"),
            })
            .collect();
        lines.push(field(
            &format!("path {}", path.route),
            format!("{state} ({})", probes.join(" ")),
        ));
    }
    lines.push("---------------------------------".to_string());
    lines.push(field(
        "suggested route",
        match analysis.suggestion {
            Some(route) => format!("{route} ({})", analysis.suggestion_confidence.label()),
            None => format!("none ({})", analysis.suggestion_confidence.label()),
        },
    ));
    for reason in &analysis.suggestion_reasons {
        lines.push(field("reasons", format!("- {reason}")));
    }
    lines.push(field(
        "note",
        "a device gate with the fixed CPU pair still decides whether a route is supported"
            .to_string(),
    ));
    lines.join("\n") + "\n"
}

#[cfg(test)]
mod tests {
    use super::*;

    fn no_routes(_route: &str) -> bool {
        false
    }

    #[test]
    fn pselect_derivation_wins() {
        let (route, confidence, _) = suggest(true, &no_routes, None, Some(5), Some(15));
        assert_eq!(route, Some("select_stack"));
        assert_eq!(confidence, Confidence::High);
    }

    #[test]
    fn major5_multicast_fallback() {
        let available = |route: &str| route == "multicast_waiter";
        let (route, confidence, _) = suggest(false, &available, None, Some(5), Some(15));
        assert_eq!(route, Some("multicast_waiter"));
        assert_eq!(confidence, Confidence::Medium);
    }

    #[test]
    fn family_defaults_are_low_confidence() {
        let (route, confidence, _) = suggest(false, &no_routes, Some("STRUCT_OFFSETS_6_6"), Some(6), Some(6));
        assert_eq!(route, Some("select_stack"));
        assert_eq!(confidence, Confidence::Low);
        let (route, _, _) = suggest(false, &no_routes, None, Some(5), Some(15));
        assert_eq!(route, None);
    }
}
