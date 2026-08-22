use std::collections::{BTreeMap, BTreeSet};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::{SystemTime, UNIX_EPOCH};

use clap::Parser;

use ghostlock_extract::boot::{BootImage, MTK_DEFAULT_PHYS_LOAD, MTK_VADDR_BASE};
use ghostlock_extract::btf::Btf;
use ghostlock_extract::derive::{
    PSELECT_ROUTE_NFDS, derive_nf_logger_registration, derive_pselect_layout,
    ensure_rtmutex_43499_unpatched, relative_symbols,
};
use ghostlock_extract::error::{ExtractError, Result};
use ghostlock_extract::fdt::recover_kernel_phys_load;
use ghostlock_extract::kallsyms;
use ghostlock_extract::kallsyms::Kallsyms;
use ghostlock_extract::kallsyms_finder;
use ghostlock_extract::payload;
use ghostlock_extract::report;
use ghostlock_extract::symbols::{kernel_struct_macro, resolve_structs, resolve_symbols};

#[derive(Parser, Debug)]
#[command(
    name = "ghostlock-extract",
    about = "Extract GhostLock kernel offsets from boot.img / arm64 Image / payload.bin / OTA URL",
    after_help = "examples:\n  ghostlock-extract boot.img --format json --out offsets.json\n  ghostlock-extract boot.img --xbl-config xbl_config.img --register\n  ghostlock-extract payload.bin --register\n  ghostlock-extract https://host/payload.bin --format json --out offsets.json\n  ghostlock-extract boot.img --format c --out offsets.h --name device\nWhen --kallsyms is omitted, the embedded kallsyms table is recovered from the kernel image itself (no root needed); on a rooted phone /proc/kallsyms is used first."
)]
struct Cli {
    /// boot.img, raw arm64 Image, gzip Image, payload.bin, OTA ZIP, or http(s) OTA URL
    image: PathBuf,
    /// kallsyms text file (e.g. dumped from /proc/kallsyms); when omitted the
    /// embedded kallsyms table is recovered from the kernel image
    #[arg(long)]
    kallsyms: Option<PathBuf>,
    /// optional XBL xbl_config.img; derive kernel physical load from its FDT
    #[arg(long)]
    xbl_config: Option<PathBuf>,
    /// kernel physical load address (hex or decimal); overrides defaults
    #[arg(long, value_parser = parse_int)]
    phys: Option<u64>,
    /// device name used in the --format c output header
    #[arg(long, default_value = "target")]
    name: String,
    /// output format: text (JSON), json, or c
    #[arg(long, value_parser = ["text", "json", "c"], default_value = "text")]
    format: String,
    /// register the kernel table under src/kernels/<release>/offsets.h
    #[arg(long)]
    register: bool,
    /// treat every unresolved symbol as optional (emit 0)
    #[arg(long)]
    allow_missing: bool,
    /// overwrite an existing device header that differs
    #[arg(long)]
    force: bool,
    /// write output to a file instead of stdout
    #[arg(long)]
    out: Option<PathBuf>,
    /// skip disassembly-based derivation (pselect/loggers heuristics)
    #[arg(long)]
    no_disasm: bool,
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
            if ks.symbols.values().any(|addrs| addrs.iter().any(|&a| a != 0)) {
                return Ok(ks);
            }
        }
    }
    kallsyms_finder::recover(&boot.kernel, btf_at)
        .map_err(|err| ExtractError::kallsyms(err.to_string()))
}

fn run(cli: &Cli) -> Result<i32> {
    let mut boot_path = cli.image.clone();
    let mut xbl_path = cli.xbl_config.clone();
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
        let (extracted_boot, extracted_xbl) =
            payload::extract_analysis_inputs(&payload_view, &work_dir)
                .map_err(|err| ExtractError::new(format!("{err:#}")))?;
        boot_path = extracted_boot;
        xbl_path = extracted_xbl;
        eprintln!(
            "info: extracted boot={} xbl_config={} from payload",
            boot_path.display(),
            xbl_path
                .as_ref()
                .map(|p| p.display().to_string())
                .unwrap_or_else(|| "none".to_string())
        );
    }

    let boot = BootImage::load(&boot_path)?;
    let mut kernel_phys_load = if let Some(xbl) = &xbl_path {
        Some(recover_kernel_phys_load(xbl)?)
    } else {
        cli.phys
    };

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
    let (rel_symbols, sorted_offsets) = relative_symbols(&symbols, base);
    // stop early when remove_waiter() is fixed.
    match ensure_rtmutex_43499_unpatched(&boot.kernel, &rel_symbols, &sorted_offsets) {
        Ok(remove_waiter) => eprintln!(
            "info: (CVE-2026-43499 primitive present \
             (remove_waiter@{remove_waiter:#x} still uses current)"
        ),
        Err(err) => {
            eprintln!("error: {err}");
            return Ok(6);
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
            if kernel_struct_macro(Some(release)).is_none() {
                eprintln!(
                    "warning: {release} is not a verified kernel family \
                     (6.1, 6.6, 6.12); emitting the 6.6 layout as a testing \
                     starting point, verify the waiter layout and slab \
                     stride before trusting it"
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
                Some(derived) if derived > 0 && derived <= 0xFFFF_FFFF => {
                    eprintln!(
                        "info: MediaTek compressed image; kernel_phys_load derived \
                         from _text: 0x{derived:x} (DRAM base; pass --phys to override)"
                    );
                    kernel_phys_load = Some(derived);
                }
                _ => {
                    eprintln!(
                        "warning: _text=0x{text_base:x} gives no plausible mtk \
                         kernel_phys_load; using 0x{MTK_DEFAULT_PHYS_LOAD:x} \
                         (pass --phys to override)"
                    );
                    kernel_phys_load = Some(MTK_DEFAULT_PHYS_LOAD);
                }
            }
        } else {
            kernel_phys_load = Some(MTK_DEFAULT_PHYS_LOAD);
            eprintln!(
                "info: MediaTek compressed image; _text unavailable, assuming \
                 kernel_phys_load=0x{MTK_DEFAULT_PHYS_LOAD:x} (DRAM base; pass \
                 --phys to override)"
            );
        }
    }
    report::validate_kernel_phys_load(
        release.as_deref(),
        kernel_phys_load,
        boot.mtk_lz4 || boot.mtk_gzip,
    );

    let mut symbol_offsets = resolve_symbols(&symbols, base);

    let mut derived: BTreeMap<String, u64> = BTreeMap::new();
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
                    eprintln!("error: pselect route not feasible on this kernel: {message}");
                    return Ok(3);
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
        .unwrap_or_else(|| {
            let shift = report::pselect_waiter_shift_for(release.as_deref());
            eprintln!(
                "warning: using heuristic pselect_waiter_shift={shift} (6.12=0, 6.6=-2); \
                 unreliable for kernels with a non-inlined do_pselect middle layer"
            );
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
    let existing = report::existing_entries()
        .get(release.as_deref().unwrap_or(""))
        .cloned()
        .unwrap_or_default();
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
        let carried = existing.get(&key).copied().unwrap_or(0);
        symbol_offsets.insert(key.clone(), Some(carried as u64));
        if carried != 0 {
            eprintln!(
                "warning: {key} not found in kallsyms; carried over 0x{carried:08x} \
                 from the registered {} entry",
                release.as_deref().unwrap_or("")
            );
        } else {
            eprintln!(
                "warning: {key} not found in kallsyms; emitted 0x00000000 (runtime \
                 falls back to target.h default)"
            );
        }
    }
    report::require_fields(&symbol_offsets, &BTreeSet::new())?;
    if btf.is_some() {
        let struct_fields_u64: BTreeMap<String, Option<u64>> = struct_offsets
            .iter()
            .map(|(key, value)| (key.clone(), value.map(|v| v as u64)))
            .collect();
        report::require_fields(&struct_fields_u64, &BTreeSet::new())?;
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
    if cli.register {
        let Some(release) = release.as_deref() else {
            return Err(ExtractError::new(
                "--register requires a kernel release string in the boot image",
            ));
        };
        let key = report::kernel_key(release);
        if report::existing_entries().contains_key(release) && !cli.force {
            report::warn_existing_mismatches(release, &symbol_offsets);
            if report::kernel_header_path(&key).exists() {
                eprintln!("info: {release} already registered; no duplicate table created");
                return Ok(0);
            }
        }
        let output = report::render_device(
            release,
            &symbol_offsets,
            &struct_offsets,
            kernel_phys_load,
            pselect_shift,
        );
        let target = report::kernel_header_path(&key);
        if target.exists()
            && std::fs::read_to_string(&target)
                .map(|t| t != output)
                .unwrap_or(true)
            && !cli.force
        {
            return Err(ExtractError::new(format!(
                "{} already exists and differs; pass --force to overwrite",
                target.display()
            )));
        }
        std::fs::create_dir_all(target.parent().unwrap())
            .map_err(|err| ExtractError::new(format!("{err}")))?;
        std::fs::write(&target, output).map_err(|err| ExtractError::new(format!("{err}")))?;
        eprintln!("wrote {}", target.display());
        report::register_kernel(&key)?;
        report::warn_existing_mismatches(release, &symbol_offsets);
        return Ok(0);
    }

    let output = if cli.format == "c" {
        report::render_c(
            release.as_deref(),
            &cli.name,
            &symbol_offsets,
            &struct_offsets,
            kernel_phys_load,
            pselect_shift,
        )
    } else {
        let report_value = report::build_report(
            release.as_deref(),
            base,
            kernel_phys_load,
            &symbol_offsets,
            &struct_offsets,
            btf_size,
            pselect_shift,
        );
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
