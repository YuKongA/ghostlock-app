//! Full-OTA extraction support: pull boot/xbl_config partitions from a
//! payload.bin or OTA ZIP using the payload-extract library.

use std::path::{Path, PathBuf};

use anyhow::Context;

pub const PAYLOAD_MAGIC: &[u8; 4] = b"CrAU";

/// Detect a payload.bin or OTA ZIP by magic bytes.
pub fn looks_like_payload(path: &str) -> bool {
    // payload-extract supports HTTP(S) payload sources directly.
    if path.starts_with("http://") || path.starts_with("https://") {
        return true;
    }
    let path = Path::new(path);
    let Ok(mut file) = std::fs::File::open(path) else {
        return false;
    };
    let mut head = [0u8; 4];
    use std::io::Read;
    if file.read(&mut head).unwrap_or(0) < 4 {
        return false;
    }
    head == *PAYLOAD_MAGIC || head == [0x50, 0x4B, 0x03, 0x04]
}

/// Read only the payload header and manifest (no data download), enough to
/// list partitions.  Works for local payload.bin / OTA ZIP files too.
pub fn open_payload_meta(input: &str) -> anyhow::Result<payload_extract::payload::PayloadView> {
    payload_extract::input::open(input, false, None)
        .with_context(|| format!("failed to read payload metadata '{input}'"))
}

/// Open a payload for extraction, downloading only the operation data of the
/// requested partitions.  An empty partition list would download the entire
/// payload, so callers must pass [`analysis_partition_names`]' output.
pub fn open_payload_for(
    input: &str,
    out_dir: &Path,
    partitions: &[String],
    on_progress: Option<payload_extract::input::ProgressCallback>,
) -> anyhow::Result<payload_extract::payload::PayloadView> {
    let opts = payload_extract::input::OpenOptions {
        temp_dir: Some(out_dir.to_path_buf()),
        download_progress: on_progress,
        ..Default::default()
    };
    payload_extract::input::open_for_extract_with(input, partitions, &opts)
        .with_context(|| format!("failed to open payload '{input}'"))
}

/// Partitions the analysis needs: boot (kernel image; BTF/kallsyms source)
/// plus xbl_config when present.  The GKI init_boot partition only carries
/// the generic ramdisk and is not an analysis input.
pub fn analysis_partition_names(
    payload: &payload_extract::payload::PayloadView,
) -> anyhow::Result<Vec<String>> {
    let available: Vec<String> = payload
        .partitions()
        .iter()
        .map(|p| p.partition_name.clone())
        .collect();
    if !available.iter().any(|n| n == "boot") {
        anyhow::bail!(
            "payload has no boot partition; init_boot only holds the GKI \
             ramdisk and cannot be analyzed"
        );
    }
    let mut want: Vec<String> = vec!["boot".to_string()];
    if available.iter().any(|n| n == "xbl_config") {
        want.push("xbl_config".to_string());
    }
    Ok(want)
}

/// Extract the requested partitions from an opened payload into `out_dir` and
/// return the produced files.
pub fn extract_partitions(
    payload: &payload_extract::payload::PayloadView,
    out_dir: &Path,
    partitions: &[String],
) -> anyhow::Result<Vec<PathBuf>> {
    std::fs::create_dir_all(out_dir)
        .with_context(|| format!("failed to create '{}'", out_dir.display()))?;
    let config = payload_extract::extract::ExtractConfig {
        verify_ops: false,
        threads: 0,
        quiet: true,
        source_dir: None,
        out_config: None,
        progress: None,
    };
    payload_extract::extract::extract_partitions(payload, out_dir, partitions, &config)
        .context("failed to extract partitions")?;
    let mut produced = Vec::new();
    for partition in partitions {
        let mut found: Vec<PathBuf> = Vec::new();
        for entry in std::fs::read_dir(out_dir).with_context(|| "read out dir")? {
            let entry = entry?;
            let name = entry.file_name().to_string_lossy().into_owned();
            if name == *partition || name == format!("{partition}.img") {
                found.push(entry.path());
            }
        }
        match found.len() {
            0 => anyhow::bail!("partition '{partition}' not present in payload"),
            1 => produced.push(found.remove(0)),
            _ => anyhow::bail!("multiple outputs for partition '{partition}'"),
        }
    }
    Ok(produced)
}

/// Pick the analysis inputs from a full OTA payload: the boot partition
/// (kernel image; BTF/kallsyms source) plus xbl_config when present.  The
/// GKI init_boot partition only carries the generic ramdisk and is not an
/// analysis input.
pub fn extract_analysis_inputs(
    payload: &payload_extract::payload::PayloadView,
    out_dir: &Path,
) -> anyhow::Result<(PathBuf, Option<PathBuf>)> {
    let want = analysis_partition_names(payload)?;
    let produced = extract_partitions(payload, out_dir, &want)?;
    let boot_name = "boot".to_string();
    let boot_out = produced
        .iter()
        .find(|p| {
            p.file_name()
                .map(|n| {
                    n.to_string_lossy() == boot_name
                        || n.to_string_lossy() == format!("{boot_name}.img")
                })
                .unwrap_or(false)
        })
        .cloned()
        .ok_or_else(|| anyhow::anyhow!("boot partition output not found"))?;
    let xbl_out = produced
        .iter()
        .find(|p| {
            p.file_name()
                .map(|n| n.to_string_lossy().starts_with("xbl_config"))
                .unwrap_or(false)
        })
        .cloned();
    Ok((boot_out, xbl_out))
}
