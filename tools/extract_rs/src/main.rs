use std::collections::{BTreeMap, BTreeSet};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::{SystemTime, UNIX_EPOCH};

use clap::Parser;

use ghostlock_extract::analysis;
use ghostlock_extract::boot::{BootImage, MTK_DEFAULT_PHYS_LOAD, MTK_VADDR_BASE};
use ghostlock_extract::btf::Btf;
use ghostlock_extract::derive::RelSymbols;
use ghostlock_extract::derive::{
    PSELECT_ROUTE_NFDS, derive_cred_5x, derive_nf_logger_registration, derive_pselect_layout,
    ensure_rtmutex_43499_unpatched, multicast_waiter_off, relative_symbols,
};
use ghostlock_extract::error::{ExtractError, Result};
use ghostlock_extract::fdt::{recover_kernel_phys_load, recover_kernel_phys_load_from_uefi};
use ghostlock_extract::iomem;
use ghostlock_extract::kallsyms;
use ghostlock_extract::kallsyms::Kallsyms;
use ghostlock_extract::kallsyms_finder;
use ghostlock_extract::payload;
use ghostlock_extract::plugin;
use ghostlock_extract::report;
use ghostlock_extract::spec;
use ghostlock_extract::symbols::{
    kernel_layout_verified, kernel_struct_macro, resolve_structs, resolve_symbols,
};

#[derive(Parser, Debug)]
#[command(
    name = "ghostlock-extract",
    about = "Extract GhostLock kernel offsets from boot.img / arm64 Image / payload.bin / OTA URL",
    after_help = "examples:\n  ghostlock-extract boot.img --format conf --out profile.conf\n  ghostlock-extract boot.img --format json --out offsets.json\n  ghostlock-extract https://host/payload.bin --format json --out offsets.json\nWhen --kallsyms is omitted, the embedded kallsyms table is recovered from the kernel image itself (no root needed); on a rooted phone /proc/kallsyms is used first."
)]
struct Cli {
    /// boot.img, raw arm64 Image, gzip Image, payload.bin, OTA ZIP, or http(s) OTA URL
    image: PathBuf,
    /// kallsyms text file (e.g. dumped from /proc/kallsyms); when omitted the
    /// embedded kallsyms table is recovered from the kernel image
    #[arg(long)]
    kallsyms: Option<PathBuf>,
    /// /proc/iomem text file (rooted device dump); when omitted a readable
    /// /proc/iomem is used to derive kernel_phys_offset and kernel_phys_load
    #[arg(long)]
    iomem: Option<PathBuf>,
    /// optional XBL xbl_config.img; derive kernel physical load from its FDT
    #[arg(long)]
    xbl_config: Option<PathBuf>,
    /// optional UEFI uefi.img; derive kernel physical load from its platform
    /// memory-map table when xbl_config carries no FDT memory map
    #[arg(long)]
    uefi: Option<PathBuf>,
    /// kernel physical load address (hex or decimal); overrides defaults
    #[arg(long, value_parser = parse_int)]
    phys: Option<u64>,
    /// output format: text (JSON), json, or conf (flattened GLK profile HOCON)
    #[arg(long, value_parser = ["text", "json", "conf"], default_value = "text")]
    format: String,
    /// route written to --format conf; defaults to the analysis suggestion
    #[arg(long, value_parser = ["tcp_zerocopy", "select_stack", "multicast_waiter"])]
    route: Option<String>,
    /// combination step path in the backend token written to --format conf;
    /// rootchild is the only path the extractor derives (shizuku/umh are App
    /// terminal choices that share the same 43499 route geometry)
    #[arg(long, value_parser = ["rootchild", "shizuku", "umh"], default_value = "rootchild")]
    steps_path: String,
    /// ⏸ DISABLED while the plugin project is frozen (user directive
    /// 2026-10-05): passing it fails closed with a named error. Native rejects
    /// the `plugin` owner outright now (the owner-list branch is commented out
    /// there), so a profile carrying a `plugin {}` block could not be loaded.
    /// The flag stays declared so existing invocations fail loudly instead of
    /// //TODO(mechanical-comment-path-fix): 机械改动：注释路径修正（计划已归档，路径改写；无语义变更）。
    /// silently producing a different profile. See docs/archive/20261007-2237-branch-plan.md.
    #[arg(long = "plugin-descriptor", value_name = "TSV")]
    plugin_descriptors: Vec<PathBuf>,
    /// treat every unresolved symbol as optional (emit 0)
    #[arg(long)]
    allow_missing: bool,
    /// write output to a file instead of stdout
    #[arg(long)]
    out: Option<PathBuf>,
    /// skip disassembly-based derivation (pselect/loggers heuristics)
    #[arg(long)]
    no_disasm: bool,
    /// analyse the kernel (family, waiter layout, primitive, route candidates)
    /// and print a report instead of offsets
    #[arg(long)]
    analysis: bool,
    /// working directory for payload extraction and temp files; defaults to
    /// the system temp dir (pass an app-writable dir when running on Android)
    #[arg(long)]
    work_dir: Option<PathBuf>,
}

fn parse_int(text: &str) -> Result<u64> {
    let trimmed = text.trim();
    if let Some(hex) = trimmed
        .strip_prefix("0x")
        .or_else(|| trimmed.strip_prefix("0X"))
    {
        u64::from_str_radix(hex, 16)
            .map_err(|_| ExtractError::new(format!("invalid --phys value: {text}")))
    } else {
        trimmed
            .parse::<u64>()
            .map_err(|_| ExtractError::new(format!("invalid --phys value: {text}")))
    }
}

/// Human-readable byte size for download progress lines.
fn format_size(bytes: u64) -> String {
    const UNITS: [&str; 5] = ["B", "KiB", "MiB", "GiB", "TiB"];
    let mut value = bytes as f64;
    let mut unit = 0;
    while value >= 1024.0 && unit + 1 < UNITS.len() {
        value /= 1024.0;
        unit += 1;
    }
    format!("{value:.1} {}", UNITS[unit])
}

/// Progress callback for the selective payload download; prints a line about
/// every 3 seconds so the app log shows the download is alive.
fn download_progress() -> Option<payload_extract::input::ProgressCallback> {
    static NEXT_PRINT: AtomicU64 = AtomicU64::new(0);
    Some(std::sync::Arc::new(move |done: u64, total: u64| {
        let now = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map(|d| d.as_millis() as u64)
            .unwrap_or(0);
        if now < NEXT_PRINT.load(Ordering::Relaxed) {
            return;
        }
        NEXT_PRINT.store(now + 3000, Ordering::Relaxed);
        let percent = if total > 0 {
            done as f64 / total as f64 * 100.0
        } else {
            0.0
        };
        eprintln!(
            "download: {} / {} ({percent:.1}%)",
            format_size(done),
            format_size(total)
        );
    }))
}

/// Returns the file-based kallsyms source (--kallsyms or a readable
/// /proc/kallsyms), or None when the caller should recover symbols from the
/// kernel image instead.
fn obtain_kallsyms(provided: Option<&Path>) -> Result<Option<PathBuf>> {
    if let Some(provided) = provided {
        if !provided.is_file() {
            return Err(ExtractError::new(format!(
                "kallsyms file not found: {}",
                provided.display()
            )));
        }
        return Ok(Some(provided.to_path_buf()));
    }
    // On a rooted phone /proc/kallsyms is the natural source.
    // procfs stat reports len 0 for it, so probe readability with File::open.
    let proc_ksyms = Path::new("/proc/kallsyms");
    if proc_ksyms.is_file() && std::fs::File::open(proc_ksyms).is_ok() {
        return Ok(Some(proc_ksyms.to_path_buf()));
    }
    Ok(None)
}

/// Read and parse a kallsyms text file.
fn parse_kallsyms_file(path: &Path) -> Result<Kallsyms> {
    let parsed = std::fs::read_to_string(path)
        .map_err(|err| ExtractError::new(format!("cannot read kallsyms: {err}")))?;
    kallsyms::parse(&parsed)
}

/// Resolve the symbol table: --kallsyms / /proc/kallsyms first, otherwise
/// recover the embedded kallsyms table from the kernel image (no root needed).
fn resolve_kallsyms(
    boot: &BootImage,
    btf_at: Option<(usize, usize)>,
    cli: &Cli,
) -> Result<Kallsyms> {
    if let Some(path) = obtain_kallsyms(cli.kallsyms.as_deref())? {
        if cli.kallsyms.is_some() {
            // an explicit --kallsyms fails loudly instead of falling back
            return parse_kallsyms_file(&path);
        }
        // kptr_restrict zeroes every address; treat that as a missing table
        if let Ok(ks) = parse_kallsyms_file(&path) {
            if ks
                .symbols
                .values()
                .any(|addrs| addrs.iter().any(|&a| a != 0))
            {
                return Ok(ks);
            }
        }
    }
    kallsyms_finder::recover(&boot.kernel, btf_at)
        .map_err(|err| ExtractError::kallsyms(err.to_string()))
}

fn run(cli: &Cli) -> Result<i32> {
    /* ⏸ PLUGIN PROJECT FROZEN (user directive 2026-10-05; see
     * //TODO(mechanical-comment-path-fix): 机械改动：注释路径修正（计划已归档，路径改写；无语义变更）。
     * docs/archive/20261007-2237-branch-plan.md). --plugin-descriptor would append a
     * `plugin {}` block, but native now rejects the `plugin` owner outright, so
     * any such output is unusable. Production of that block is commented out at
     * the two call sites below (the implementation is kept intact); passing the
     * flag fails closed here instead of silently emitting a profile WITHOUT the
     * requested block.
     * UNFREEZE = delete this guard + uncomment the two marked blocks
     *            (conf call site, text/json call site) + drop the
     *            #[allow(dead_code)] on apply_plugin_descriptors. */
    if !cli.plugin_descriptors.is_empty() {
        return Err(ExtractError::new(
            "--plugin-descriptor is disabled while the plugin project is frozen (user directive 2026-10-05)",
        ));
    }
    let mut boot_path = cli.image.clone();
    let mut xbl_path = cli.xbl_config.clone();
    let mut uefi_path = cli.uefi.clone();
    let work_root = cli.work_dir.clone().unwrap_or_else(std::env::temp_dir);

    if payload::looks_like_payload(cli.image.to_string_lossy().as_ref()) {
        let work_dir = work_root.join(format!("ghostlock-payload-{}", std::process::id()));
        let input = cli.image.to_string_lossy();
        // Header + manifest only, then selective download of the partitions
        // the analysis needs; an empty list would pull the whole payload.
        let meta_view = payload::open_payload_meta(&input)
            .map_err(|err| ExtractError::new(format!("{err:#}")))?;
        let want = payload::analysis_partition_names(&meta_view)
            .map_err(|err| ExtractError::new(format!("{err:#}")))?;
        eprintln!("info: analyzing partitions: {}", want.join(", "));
        let payload_view = payload::open_payload_for(&input, &work_dir, &want, download_progress())
            .map_err(|err| ExtractError::new(format!("{err:#}")))?;
        let (extracted_boot, extracted_xbl, extracted_uefi) =
            payload::extract_analysis_inputs(&payload_view, &work_dir)
                .map_err(|err| ExtractError::new(format!("{err:#}")))?;
        boot_path = extracted_boot;
        xbl_path = extracted_xbl;
        uefi_path = extracted_uefi;
        eprintln!(
            "info: extracted boot={} xbl_config={} uefi={} from payload",
            boot_path.display(),
            xbl_path
                .as_ref()
                .map(|p| p.display().to_string())
                .unwrap_or_else(|| "none".to_string()),
            uefi_path
                .as_ref()
                .map(|p| p.display().to_string())
                .unwrap_or_else(|| "none".to_string())
        );
    }

    let boot = BootImage::load(&boot_path)?;
    let mut phys_source = "unset";
    let mut kernel_phys_load: Option<u64> = None;
    if let Some(xbl) = &xbl_path {
        match recover_kernel_phys_load(xbl) {
            Ok(phys) => {
                phys_source = "xbl_config FDT";
                kernel_phys_load = Some(phys);
            }
            Err(err) => {
                eprintln!("warning: xbl_config FDT parse failed: {err}; trying the uefi memory map")
            }
        }
    }
    if kernel_phys_load.is_none() {
        if let Some(uefi) = &uefi_path {
            match recover_kernel_phys_load_from_uefi(uefi) {
                Ok(phys) => {
                    phys_source = "uefi memory map";
                    eprintln!("info: kernel_phys_load=0x{phys:x} (uefi memory map)");
                    kernel_phys_load = Some(phys);
                }
                Err(err) => eprintln!(
                    "warning: uefi memory-map parse failed: {err}; proceeding with boot-only analysis"
                ),
            }
        }
    }
    if kernel_phys_load.is_none() && cli.phys.is_some() {
        phys_source = "--phys";
        kernel_phys_load = cli.phys;
    }

    let btf_at = boot.embedded_btf_at();
    let ks = resolve_kallsyms(
        &boot,
        btf_at.as_ref().map(|(offset, blob)| (*offset, blob.len())),
        cli,
    )?;
    let symbols = ks.symbols;
    let text_base = kallsyms::unique(&symbols, "_text");
    let base = text_base.or_else(|| kallsyms::unique(&symbols, "_head"));
    let Some(base) = base else {
        return Err(ExtractError::new("_text/_head is not unique in kallsyms"));
    };

    // DRAM base / linear-map offset, and (with the kallsyms delta) the kernel
    // physical load, from /proc/iomem when available (rooted device or --iomem).
    let mut kernel_phys_offset: Option<u64> = None;
    let iomem_info = match iomem::load(cli.iomem.as_deref()) {
        Ok(info) => info,
        Err(err) => {
            eprintln!("warning: {err}");
            None
        }
    };
    if let Some(info) = iomem_info {
        kernel_phys_offset = info.dram_base;
        if kernel_phys_load.is_none() {
            if let (Some(kc), Some(text), Some(stext)) = (
                info.kernel_code_start,
                kallsyms::unique(&symbols, "_text"),
                kallsyms::unique(&symbols, "_stext"),
            ) {
                let delta = stext.wrapping_sub(text) & 0xffff_ffff;
                let derived = kc.wrapping_sub(delta) & 0xffff_ffff;
                eprintln!(
                    "info: iomem; kernel_phys_load=0x{derived:x} kernel_phys_offset=0x{:x}",
                    kernel_phys_offset.unwrap_or(0)
                );
                kernel_phys_load = Some(derived);
                phys_source = "iomem";
            }
        }
    }

    let (rel_symbols, sorted_offsets) = relative_symbols(&symbols, base);
    // stop early when remove_waiter() is fixed; --analysis reports it instead.
    let primitive = ensure_rtmutex_43499_unpatched(&boot.kernel, &rel_symbols, &sorted_offsets);
    match &primitive {
        Ok(remove_waiter) => eprintln!(
            "info: (CVE-2026-43499 primitive present \
             (remove_waiter@{remove_waiter:#x} still uses current)"
        ),
        Err(err) => {
            if !cli.analysis {
                eprintln!("error: {err}");
                return Ok(6);
            }
            eprintln!("warning: analysis continues: {err}");
        }
    }

    let btf_raw = btf_at.as_ref().map(|(_, blob)| blob.clone());
    let btf = btf_raw.as_deref().map(Btf::new).transpose()?;
    if btf.is_none() {
        eprintln!(
            "warning: embedded BTF not found; symbols come from kallsyms \
             and struct offsets fall back to target.h defaults"
        );
    }

    let release = boot.release();
    match release.as_deref() {
        Some(release) => {
            if !kernel_layout_verified(Some(release)) {
                let template = kernel_struct_macro(Some(release));
                eprintln!(
                    "warning: {release} is unverified{}; no family-derived \
                     geometry will be emitted",
                    template.map_or(String::new(), |name| format!(" (template {name} only)"))
                );
            }
        }
        None => {
            eprintln!("warning: boot image carries no kernel release string");
        }
    }
    if kernel_phys_load.is_none() && (boot.mtk_lz4 || boot.mtk_gzip) {
        if let Some(text_base) = text_base {
            match text_base.checked_sub(MTK_VADDR_BASE) {
                Some(derived) if derived == MTK_DEFAULT_PHYS_LOAD => {
                    eprintln!(
                        "info: MediaTek compressed image; _text confirms \
                         kernel_phys_load=0x{derived:x} (DRAM base)"
                    );
                    kernel_phys_load = Some(derived);
                    phys_source = "MediaTek DRAM base";
                }
                _ => {
                    eprintln!(
                        "warning: _text=0x{text_base:x} does not match the mtk \
                         DRAM-base mapping; kernel_phys_load left unset so the \
                         runtime derives it (pass --phys to override)"
                    );
                }
            }
        } else {
            eprintln!(
                "info: MediaTek compressed image; _text unavailable, leaving \
                 kernel_phys_load unset so the runtime derives it"
            );
        }
    }
    if cli.route.is_some() && cli.format != "conf" {
        eprintln!("warning: --route only affects --format conf; ignoring");
    }

    if cli.analysis {
        let analysis_report = analysis::build(analysis::Input {
            release: release.as_deref(),
            kernel: &boot.kernel,
            symbols: &symbols,
            rel_symbols: &rel_symbols,
            sorted_offsets: &sorted_offsets,
            btf: btf.as_ref(),
            phys: kernel_phys_load,
            phys_source,
            primitive,
            allow_disasm: !cli.no_disasm,
        });
        let text = analysis::render(&analysis_report);
        if let Some(out) = &cli.out {
            std::fs::write(out, text).map_err(|err| ExtractError::new(format!("{err}")))?;
        } else {
            print!("{text}");
        }
        return Ok(0);
    }

    let mut symbol_offsets = resolve_symbols(&symbols, base);

    let mut derived: BTreeMap<String, u64> = BTreeMap::new();
    // Infeasible pselect geometry only rules out the select_stack route; it is
    // recorded here and re-checked after route selection instead of aborting.
    let mut pselect_infeasible: Option<String> = None;
    if !cli.no_disasm {
        if let Some(btf) = &btf {
            match derive_pselect_layout(
                &boot.kernel,
                &rel_symbols,
                &sorted_offsets,
                btf,
                PSELECT_ROUTE_NFDS,
            ) {
                Ok(layout) => {
                    let shift = layout.shift as i64 - 2;
                    let frame_parts: Vec<String> = [
                        "frame_pselect_wrapper",
                        "frame_pselect_dispatch",
                        "frame_pselect_core",
                        "frame_futex_wrapper",
                        "frame_futex_dispatch",
                        "frame_futex_wait",
                    ]
                    .iter()
                    .filter_map(|key| {
                        layout.frames.get(*key).map(|value| {
                            format!("{}={:#x}", key.split('_').nth(1).unwrap_or(key), value)
                        })
                    })
                    .collect();
                    eprintln!(
                        "info: pselect chain {} frames={} buffer={:#x} waiter={:#x} \
                         shift={shift} (derived {} - 2)",
                        layout.chain,
                        frame_parts.join(" "),
                        layout.pselect_buffer,
                        layout.waiter_local,
                        layout.shift,
                    );
                    derived.insert("pselect_waiter_shift_value".to_string(), shift as u64);
                }
                Err(ExtractError::Infeasible(message)) => {
                    eprintln!("warning: pselect route not feasible on this kernel: {message}");
                    pselect_infeasible = Some(message);
                }
                Err(err) => {
                    eprintln!("warning: pselect_waiter_shift derivation failed: {err}");
                }
            }
            match derive_nf_logger_registration(&boot.kernel, &rel_symbols, &sorted_offsets, btf) {
                Ok(info) => {
                    derived.insert("off_slide_loggers_0_1".to_string(), info.loggers_0_1);
                    eprintln!(
                        "info: nf_logger loggers={:#x} nfulnl_logger={:#x} ulog={} slot={:#x}",
                        info.loggers, info.nfulnl_logger, info.nf_log_type_ulog, info.loggers_0_1
                    );
                }
                Err(err) => {
                    eprintln!("warning: loggers_0_1 derivation failed: {err}");
                }
            }
        } else {
            eprintln!("warning: no BTF; loggers_0_1 falls back to the loggers+0x10 heuristic");
        }
    } else {
        eprintln!(
            "warning: --no-disasm; pselect_waiter_shift and loggers_0_1 fall back to \
             heuristics"
        );
    }

    let pselect_shift = derived
        .get("pselect_waiter_shift_value")
        .copied()
        .map(|value| value as i64)
        .or_else(|| {
            // An infeasible derivation means this image's layout does not exist;
            // a family default would be wrong, so it is not substituted.
            if pselect_infeasible.is_some() {
                return None;
            }
            let shift = report::pselect_waiter_shift_for(release.as_deref());
            if let Some(shift) = shift {
                eprintln!(
                    "warning: using verified-family pselect_waiter_shift={shift}; \
                     image-specific disassembly derivation was unavailable"
                );
            } else {
                eprintln!(
                    "warning: pselect_waiter_shift unavailable; no family fallback \
                     is defined for this release"
                );
            }
            shift
        });
    if let Some(slot) = derived.get("off_slide_loggers_0_1").copied() {
        symbol_offsets.insert("off_slide_loggers_0_1".to_string(), Some(slot));
    }
    let struct_offsets = resolve_structs(btf.as_ref());

    let missing: Vec<String> = symbol_offsets
        .iter()
        .filter(|(_, value)| value.is_none())
        .map(|(key, _)| key.clone())
        .collect();
    let mut tolerated: BTreeSet<String> = report::optional_symbols()
        .into_iter()
        .map(str::to_string)
        .collect();
    if cli.allow_missing {
        tolerated.extend(missing.iter().cloned());
    }
    let mut tolerated_missing: Vec<String> = missing
        .iter()
        .filter(|key| tolerated.contains(*key))
        .cloned()
        .collect();
    tolerated_missing.sort();
    for key in tolerated_missing {
        symbol_offsets.insert(key.clone(), Some(0));
        eprintln!(
            "warning: {key} not found in kallsyms; emitted 0x00000000 (runtime \
             falls back to target.h default)"
        );
    }
    // A conf candidate carries whatever the image yielded; the app's
    // pre-execution validation reports the gaps. Every other format keeps the
    // hard requirement.
    let candidate = cli.format == "conf";
    match report::require_fields(&symbol_offsets, &BTreeSet::new()) {
        Ok(()) => {}
        Err(err) if candidate => eprintln!(
            "warning: {err}; writing an unverified candidate (missing fields are omitted and \
             validated by the app)"
        ),
        Err(err) => return Err(err),
    }
    if btf.is_some() {
        let struct_fields_u64: BTreeMap<String, Option<u64>> = struct_offsets
            .iter()
            .map(|(key, value)| (key.clone(), value.map(|v| v as u64)))
            .collect();
        match report::require_fields(&struct_fields_u64, &report::optional_struct_fields()) {
            Ok(()) => {}
            Err(err) if candidate => eprintln!(
                "warning: {err}; writing an unverified candidate (missing fields are omitted and \
                 validated by the app)"
            ),
            Err(err) => return Err(err),
        }
    }
    if let Some(mm_size) = struct_offsets.get("struct_mm_struct").copied().flatten() {
        eprintln!(
            "info: sizeof(mm_struct)=0x{mm_size:X} (MM_STRUCT_SZ=0x500 in src/core/common.h)"
        );
        if mm_size > 0x500 {
            eprintln!("warning: sizeof(mm_struct) exceeds the hardcoded MM_STRUCT_SZ slab stride");
        }
    }

    let btf_size = btf_raw.as_ref().map(|b| b.len()).unwrap_or(0);
    // ⏸ FROZEN: only the (commented) plugin projection fills this in.
    // let mut plugin_diagnostics: Vec<serde_json::Value> = Vec::new();
    let output = if cli.format == "conf" {
        let release_text = release
            .as_deref()
            .ok_or_else(|| ExtractError::new("--format conf requires a kernel release string"))?;
        let major = release_text
            .split('.')
            .next()
            .and_then(|part| part.parse::<u32>().ok());
        if major != Some(5) && major != Some(6) {
            return Err(ExtractError::new(format!(
                "--format conf needs a 5.x/6.x release, got {release_text}"
            )));
        }
        // Candidate output: emit everything the image actually yielded and
        // leave the rest out. Whether the result is complete enough to run is
        // decided by the app's pre-execution validation, not here.
        let route = match cli.route.as_deref() {
            Some(route) => Some(route.to_string()),
            None => {
                let paths =
                    analysis::probe_paths(&symbols, &boot.kernel, &rel_symbols, &sorted_offsets);
                let pselect_derived = derived.contains_key("pselect_waiter_shift_value");
                let (suggested, _, _) =
                    analysis::suggest_route(pselect_derived, &paths, release.as_deref());
                suggested.map(str::to_string)
            }
        };
        // select_stack is the only route that needs the derived pselect layout;
        // reject it here, after selection, so tcp/multicast can still proceed.
        if route
            .as_deref()
            .is_some_and(analysis::route_depends_on_pselect_layout)
        {
            if let Some(message) = &pselect_infeasible {
                eprintln!("error: pselect route not feasible on this kernel: {message}");
                return Ok(3);
            }
        }
        let mut geometry = route
            .as_deref()
            .map(|route| {
                report::conf_route_geometry(route, release_text, pselect_shift, &struct_offsets)
            })
            .unwrap_or_default();
        // 5.x multicast: the frame/copy-window constants (`waiter_off` /
        // `buffer_size`) are image-derived. They are emitted only when the
        // static derivation from this image's setsockopt/futex stack frames
        // succeeds; otherwise the candidate stays without them instead of
        // borrowing the hardware-probed A301SO 0x60. No device or root is
        // involved; the A301SO image reproduces its hardware-probed 0x60.
        if route.as_deref() == Some("multicast_waiter") {
            const MCAST_BUFFER_SIZE: u64 = 264;
            const RT_MUTEX_WAITER_PI_TREE_ENTRY: u64 = 0x18;
            match multicast_waiter_off(
                &boot.kernel,
                &rel_symbols,
                &sorted_offsets,
                MCAST_BUFFER_SIZE,
                RT_MUTEX_WAITER_PI_TREE_ENTRY,
            ) {
                Ok(geom) => {
                    eprintln!(
                        "info: multicast waiter_off derived = 0x{:x} \
                         (setsockopt depth 0x{:x} - futex depth 0x{:x})",
                        geom.waiter_off, geom.setsockopt_depth, geom.waiter_depth
                    );
                    geometry.insert(0, ("waiter_off", geom.waiter_off as i64));
                    geometry.insert(1, ("buffer_size", MCAST_BUFFER_SIZE as i64));
                }
                Err(err) => eprintln!(
                    "warning: static multicast waiter_off derivation failed: {err}; \
                     omitting the frame/copy-window geometry (the candidate stays incomplete)"
                ),
            }
        }
        match route.as_deref() {
            Some(route) if geometry.is_empty() => eprintln!(
                "warning: no image-derived geometry for route {route}; writing an unverified \
                 candidate route branch"
            ),
            Some(_) => {}
            None => eprintln!(
                "warning: no route could be inferred; writing release/common fields only \
                 (pass --route to select one)"
            ),
        }
        if route.as_deref() == Some("multicast_waiter")
            && !geometry.iter().any(|(key, _)| *key == "task_offset")
        {
            eprintln!(
                "warning: rt_mutex_waiter task/lock offsets are missing from BTF; the multicast \
                 geometry is partial"
            );
        }
        eprintln!(
            "info: {release_text} profile is an unverified candidate; the app validates fields \
             before any execution"
        );
        let cred: Vec<(String, String)> = if major == Some(5) {
            let derived_cred = btf.as_ref().and_then(|btf| {
                let init_cred_off = symbol_offsets.get("off_init_cred").copied().flatten()?;
                let size = btf.size("cred")?;
                match derive_cred_5x(btf, &boot.kernel, init_cred_off) {
                    Ok(cred) => Some(report::conf_cred_5x(&cred, size)),
                    Err(err) => {
                        eprintln!("warning: 5.x credential derivation failed: {err}");
                        None
                    }
                }
            });
            match derived_cred {
                Some(cred) => cred,
                None => {
                    eprintln!(
                        "warning: credential template omitted; the built-in profile must \
                         supply it"
                    );
                    Vec::new()
                }
            }
        } else {
            report::conf_cred_6x()
        };
        let extra_offsets = report::ConfExtraOffsets {
            empty_zero_page: kallsyms::unique(&symbols, "empty_zero_page")
                .and_then(|value| value.checked_sub(base)),
        };
        let rendered = report::render_conf(&report::ConfInputs {
            release: release_text,
            phys: kernel_phys_load,
            /* DRAM base: from /proc/iomem when the extractor ran on a rooted
             * device, otherwise left for the profile author to fill. */
            phys_offset: kernel_phys_offset,
            symbols: &symbol_offsets,
            structs: &struct_offsets,
            backend: report::BACKEND_43499,
            route: route.as_deref(),
            steps_path: &cli.steps_path,
            route_geometry: &geometry,
            cred: &cred,
            extra_offsets: &extra_offsets,
        });
        /* ⏸ PLUGIN PROJECT FROZEN (user directive 2026-10-05) -- UNFREEZE:
         * delete the freeze guard at the top of run() and uncomment this call. */
        // apply_plugin_descriptors(
        //     cli,
        //     &rendered,
        //     &symbols,
        //     btf.as_ref(),
        //     base,
        //     &boot.kernel,
        //     &rel_symbols,
        //     &sorted_offsets,
        //     &mut plugin_diagnostics,
        // )?
        rendered
    } else {
        /* ⏸ PLUGIN PROJECT FROZEN (user directive 2026-10-05) -- UNFREEZE:
         * delete the freeze guard at the top of run() and uncomment this block
         * plus the plugin_extract insertion below.
         * text/json: no profile is written, but a descriptor run used to report
         * the resolution diagnostics (resolved against a route-less candidate:
         * only conf mode performs route inference). */
        // if !cli.plugin_descriptors.is_empty() {
        //     let rendered = report::render_conf(&report::ConfInputs {
        //         release: release.as_deref().unwrap_or("0.0.0-unknown"),
        //         phys: kernel_phys_load,
        //         phys_offset: kernel_phys_offset,
        //         symbols: &symbol_offsets,
        //         structs: &struct_offsets,
        //         backend: report::BACKEND_43499,
        //         route: cli.route.as_deref(),
        //         steps_path: &cli.steps_path,
        //         route_geometry: &[],
        //         cred: &report::conf_cred_6x(),
        //         extra_offsets: &report::ConfExtraOffsets::default(),
        //     });
        //     let _ = apply_plugin_descriptors(
        //         cli,
        //         &rendered,
        //         &symbols,
        //         btf.as_ref(),
        //         base,
        //         &boot.kernel,
        //         &rel_symbols,
        //         &sorted_offsets,
        //         &mut plugin_diagnostics,
        //     )?;
        // }
        // FROZEN: 'mut' only served the commented plugin_extract insertion.
        #[allow(unused_mut)]
        let mut report_value = report::build_report(
            release.as_deref(),
            base,
            kernel_phys_load,
            &symbol_offsets,
            &struct_offsets,
            btf_size,
            pselect_shift,
        );
        // ⏸ FROZEN (see the banner in the branch above): the plugin_extract
        // diagnostics array is not emitted while the plugin project is frozen.
        // if !plugin_diagnostics.is_empty() {
        //     report_value["plugin_extract"] =
        //         serde_json::Value::Array(plugin_diagnostics.clone());
        // }
        serde_json::to_string_pretty(&report_value).unwrap() + "\n"
    };
    if let Some(out) = &cli.out {
        std::fs::write(out, output).map_err(|err| ExtractError::new(format!("{err}")))?;
    } else {
        print!("{output}");
    }
    let remaining: Vec<String> = symbol_offsets
        .iter()
        .map(|(key, value)| (key.clone(), value.map(|v| v as u64)))
        .chain(
            struct_offsets
                .iter()
                .map(|(key, value)| (key.clone(), value.map(|v| v as u64))),
        )
        .filter(|(_, value)| value.is_none())
        .map(|(key, _)| key.clone())
        .collect();
    if !remaining.is_empty() {
        eprintln!("missing: {}", remaining.join(", "));
    }
    Ok(0)
}

/// Fills the P2 plugin extract projection into a rendered profile: parses the
/// probe TSV descriptors (native --plugin-probe stdout, frozen format), resolves
/// every declared extract name (R1 profile path, R3 BTF, R2 kallsyms symbol) and
/// appends the `plugin` block. Required entries that cannot be derived fail
/// closed; optional ones are omitted with a diagnostic; the descriptor default is
/// never substituted. Without descriptors the rendered profile is returned
/// unchanged, byte for byte.
/* ⏸ PLUGIN PROJECT FROZEN (user directive 2026-10-05): this projection is no
 * longer called by `run` -- the two call sites are commented out and passing
 * `--plugin-descriptor` fails closed. The implementation is kept intact so
 * //TODO(mechanical-comment-path-fix): 机械改动：注释路径修正（计划已归档，路径改写；无语义变更）。
 * unfreezing is "uncomment + drop this attribute" (see docs/archive/20261007-2237-branch-plan.md). */
#[allow(dead_code)]
fn apply_plugin_descriptors(
    cli: &Cli,
    rendered: &str,
    symbols: &BTreeMap<String, BTreeSet<u64>>,
    btf: Option<&Btf>,
    base: u64,
    kernel: &[u8],
    rel_symbols: &RelSymbols,
    sorted_offsets: &[u64],
    diagnostics: &mut Vec<serde_json::Value>,
) -> Result<String> {
    if cli.plugin_descriptors.is_empty() {
        return Ok(rendered.to_string());
    }
    let mut descriptors = Vec::with_capacity(cli.plugin_descriptors.len());
    for path in &cli.plugin_descriptors {
        let text = std::fs::read_to_string(path)
            .map_err(|err| ExtractError::new(format!("{}: {err}", path.display())))?;
        descriptors.push(plugin::parse_probe_tsv(&text)?);
    }
    plugin::validate_descriptors(&descriptors)?;

    let profile = report::flatten_conf_values(rendered);
    let profile_paths = report::conf_wire_paths();
    let context = plugin::ExtractContext {
        profile: &profile,
        profile_paths: &profile_paths,
        btf,
        symbols,
        base,
    };
    let spec_context = spec::SpecContext {
        extract: &context,
        kernel,
        rel_symbols,
        sorted_offsets,
    };
    let mut resolved: Vec<report::ResolvedPlugin> = Vec::with_capacity(descriptors.len());
    for descriptor in &descriptors {
        let outcome = plugin::resolve_descriptor(descriptor, &context)?;
        let mut entries = outcome.entries;
        for (name, detail) in &outcome.skipped {
            eprintln!(
                "warning: plugin {} extract '{name}' omitted (optional, not derivable: {detail})",
                descriptor.id
            );
        }
        /* P3 spec rows: explicit method chain, same output namespace as the
         * extract rows. The actual hitting method is recorded for forensics
         * (stderr always; --format json gets the structured array). */
        for declared in &descriptor.specs {
            let resolution = spec::resolve_spec(declared, &spec_context)?;
            match &resolution.value {
                Some(value) => {
                    eprintln!(
                        "info: plugin {} extract {} -> {}",
                        descriptor.id,
                        declared.name,
                        resolution.method.map(spec::Method::token).unwrap_or("")
                    );
                    entries.push((declared.name.clone(), value.clone()));
                }
                None => {
                    let reason = resolution.reason.clone().unwrap_or_default();
                    if declared.required {
                        return Err(ExtractError::unsupported(format!(
                            "plugin {} spec '{}' is required but not derivable ({reason})",
                            descriptor.id, declared.name
                        )));
                    }
                    eprintln!(
                        "warning: plugin {} spec '{}' omitted (optional, not derivable: {reason})",
                        descriptor.id, declared.name
                    );
                }
            }
            diagnostics.push(serde_json::json!({
                "id": descriptor.id,
                "name": declared.name,
                "resolved": resolution.value.is_some(),
                "method": resolution.method.map(spec::Method::token),
                "evidence": resolution.evidence,
                "reason": resolution.reason,
            }));
        }
        if entries.is_empty() {
            eprintln!(
                "info: plugin {} has no derivable extract entry; no plugin block written for it",
                descriptor.id
            );
        }
        resolved.push(report::ResolvedPlugin {
            id: descriptor.id.clone(),
            entries,
        });
    }
    let written = resolved.iter().filter(|p| !p.entries.is_empty()).count();
    eprintln!(
        "info: plugin extract projection wrote {written} block(s) from {} descriptor(s)",
        descriptors.len()
    );
    Ok(report::append_plugin_block(rendered, &resolved))
}

fn main() {
    let cli = Cli::parse();
    match run(&cli) {
        Ok(code) => std::process::exit(code),
        Err(err) => {
            eprintln!("error: {err}");
            // Exit codes let the app distinguish failure classes:
            // 2 generic parse failure,
            // 3 pselect route infeasible,
            // 4 missing required offsets,
            // 5 kallsyms recovery failure,
            // 6 primitive already fixed.
            let code = match &err {
                ExtractError::Infeasible(_) => 3,
                ExtractError::Unsupported(_) => 4,
                ExtractError::AlreadyFixed(_) => 6,
                ExtractError::Kallsyms(_) => 5,
                ExtractError::Message(_) => 2,
            };
            std::process::exit(code);
        }
    }
}

#[cfg(test)]
mod plugin_projection_tests {
    use super::*;

    const SHA: &str = "decc767346129b6dea4a8fb8d907daa13c48d9a44fd60645d5e57e42614205cf";

    /// Renders the smallest 6.1 candidate profile the projection can read back.
    fn rendered_profile() -> String {
        let symbols: BTreeMap<String, Option<u64>> = BTreeMap::new();
        let structs: BTreeMap<String, Option<u32>> = BTreeMap::new();
        let geometry: Vec<(&'static str, i64)> = Vec::new();
        let cred: Vec<(String, String)> = Vec::new();
        let extra = report::ConfExtraOffsets::default();
        report::render_conf(&report::ConfInputs {
            release: "6.1.145-android14-11-maybe-dirty",
            phys: None,
            phys_offset: None,
            symbols: &symbols,
            structs: &structs,
            backend: report::BACKEND_43499,
            route: None,
            steps_path: "rootchild",
            route_geometry: &geometry,
            cred: &cred,
            extra_offsets: &extra,
        })
    }

    /// Writes one probe TSV and returns the CLI that consumes it.
    fn descriptor_cli(tag: &str, extract_rows: &[&str]) -> (Cli, PathBuf) {
        let path = std::env::temp_dir().join(format!(
            "ghostlock-plugin-test-{}-{tag}.tsv",
            std::process::id()
        ));
        let mut text = String::from(
            "host_abi\t1\ncountermeasures_root\tcountermeasures\nhost_stages\tpre_spawn\nhost_caps\tkernel_read,log\n",
        );
        text.push_str(&format!(
            /* Batch B vocabulary: "log" must survive the whole --plugin-descriptor
             * path (the extractor only carries the token; the ABI bit is native's). */
            "plugin\ttest.schema\t1.0\t1\t80\t{SHA}\tpre_spawn\tkernel_read,log\n"
        ));
        for row in extract_rows {
            text.push_str(row);
            text.push('\n');
        }
        std::fs::write(&path, text).expect("descriptor file");
        let cli = Cli::parse_from([
            "ghostlock-extract",
            "unused.img",
            "--format",
            "conf",
            "--plugin-descriptor",
            path.to_string_lossy().as_ref(),
        ]);
        (cli, path)
    }

    #[test]
    fn plugin_descriptor_fills_the_rendered_profile_end_to_end() {
        let (cli, path) = descriptor_cli(
            "ok",
            &[
                // Root scalars are bare wire keys after the rename.
                "extract\ttest.schema\tkernel_major\tuint\t1\t0\tmajor",
                "extract\ttest.schema\tinit_task\tuint\t1\t0\tsymbol",
            ],
        );
        let mut raw: BTreeMap<String, BTreeSet<u64>> = BTreeMap::new();
        raw.insert(
            "init_task".to_string(),
            BTreeSet::from([0xffff_8000_0000_0000u64 + 0x1000]),
        );
        let mut diagnostics = Vec::new();
        let output = apply_plugin_descriptors(
            &cli,
            &rendered_profile(),
            &raw,
            None,
            0xffff_8000_0000_0000,
            &[],
            &BTreeMap::<String, BTreeSet<u64>>::new(),
            &[],
            &mut diagnostics,
        )
        .expect("both entries resolve");
        let flat = report::flatten_conf_values(&output);
        assert_eq!(
            flat.get("plugin.test.schema.extract.kernel_major")
                .map(String::as_str),
            Some("6")
        );
        assert_eq!(
            flat.get("plugin.test.schema.extract.init_task")
                .map(String::as_str),
            Some("4096")
        );
        std::fs::remove_file(&path).ok();
    }

    #[test]
    fn required_plugin_extract_fails_closed_before_any_output() {
        let (cli, path) = descriptor_cli(
            "required",
            &["extract\ttest.schema\tno_such_thing\tuint\t1\t0\tmissing"],
        );
        let raw: BTreeMap<String, BTreeSet<u64>> = BTreeMap::new();
        let mut diagnostics = Vec::new();
        let err = apply_plugin_descriptors(
            &cli,
            &rendered_profile(),
            &raw,
            None,
            0,
            &[],
            &BTreeMap::<String, BTreeSet<u64>>::new(),
            &[],
            &mut diagnostics,
        )
        .expect_err("a required entry that cannot be derived must fail closed");
        assert!(err.to_string().contains("required but not derivable"));
        std::fs::remove_file(&path).ok();
    }

    #[test]
    fn optional_plugin_extract_is_omitted_without_a_default() {
        let (cli, path) = descriptor_cli(
            "optional",
            &["extract\ttest.schema\tno_such_thing\tuint\t0\t0\tmissing"],
        );
        let raw: BTreeMap<String, BTreeSet<u64>> = BTreeMap::new();
        let mut diagnostics = Vec::new();
        let output = apply_plugin_descriptors(
            &cli,
            &rendered_profile(),
            &raw,
            None,
            0,
            &[],
            &BTreeMap::<String, BTreeSet<u64>>::new(),
            &[],
            &mut diagnostics,
        )
        .expect("an optional miss is not fatal");
        /* Nothing derivable: no plugin block, and no default substitution. */
        assert_eq!(output, rendered_profile());
        assert!(!output.contains("plugin"));
        std::fs::remove_file(&path).ok();
    }

    /// ⏸ Freeze (user directive 2026-10-05): the flag must fail closed instead
    /// of silently emitting a profile WITHOUT the requested plugin block.
    /// Removing this guard is part of the unfreeze step list in `run`.
    #[test]
    fn descriptor_flag_fails_closed_while_the_plugin_project_is_frozen() {
        let (cli, path) =
            descriptor_cli("frozen", &["extract\ttest.schema\tvalue\tuint\t1\t0\tdoc"]);
        let err = run(&cli).expect_err("--plugin-descriptor must be disabled while frozen");
        assert!(err.to_string().contains("frozen"), "{err}");
        std::fs::remove_file(&path).ok();
    }

    /// The flag stays DECLARED (existing invocations must fail loudly, not with
    /// an unknown-argument error) even though it is disabled.
    #[test]
    fn descriptor_flag_stays_declared_while_frozen() {
        let cli = Cli::parse_from([
            "ghostlock-extract",
            "unused.img",
            "--format",
            "json",
            "--plugin-descriptor",
            "/tmp/does-not-matter.tsv",
        ]);
        assert!(!cli.plugin_descriptors.is_empty());
        assert_ne!(cli.format, "conf");
    }
}
