//! Cross-end conformance corpus for the frozen probe TSV (contract-design
//! 3.14.7.2). One byte set, three ends: native produces the bytes, and the
//! Kotlin and Rust parsers must agree on the verdict.
//!
//! `app/src/test/resources/plugin-probe-conformance/manifest.tsv` is the single
//! case list. Every walker checks the manifest against the two directories in
//! BOTH directions, so a case nobody runs cannot exist and a listed case cannot
//! go missing.
//!
//! accept cases: the parse must succeed AND the parsed view must equal the raw
//! rows (id, params, extracts, stages, caps) recomputed here from the bytes;
//! reject cases: the parse must fail (the error text is per-end and is not
//! asserted).

use ghostlock_extract::plugin::{PluginDescriptor, parse_probe_tsv};
use std::collections::{BTreeMap, BTreeSet};
use std::path::{Path, PathBuf};

/// Approved corpus floor: additions are welcome, silent shrinkage is not.
const CORPUS_FLOOR: usize = 60;

fn corpus_root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("../../app/src/test/resources/plugin-probe-conformance")
}

/// manifest.tsv -> case name -> verdict, fail-closed on shape.
fn read_manifest(root: &Path) -> BTreeMap<String, String> {
    let text = std::fs::read_to_string(root.join("manifest.tsv")).expect("manifest.tsv");
    let mut out = BTreeMap::new();
    for (index, raw) in text.lines().enumerate() {
        let line = raw.trim_end_matches('\r');
        if line.trim().is_empty() || line.starts_with('#') {
            continue;
        }
        let columns: Vec<&str> = line.split('\t').collect();
        assert_eq!(columns.len(), 2, "manifest.tsv line {}: {line}", index + 1);
        assert!(
            columns[0] == "accept" || columns[0] == "reject",
            "manifest.tsv line {} has an unknown verdict: {line}",
            index + 1
        );
        assert!(
            !columns[1].is_empty(),
            "manifest.tsv line {} has no file",
            index + 1
        );
        assert!(
            out.insert(columns[1].to_string(), columns[0].to_string())
                .is_none(),
            "manifest.tsv lists {} twice",
            columns[1]
        );
    }
    out
}

fn directory_files(dir: &Path) -> BTreeSet<String> {
    let mut out = BTreeSet::new();
    for entry in std::fs::read_dir(dir).unwrap_or_else(|err| panic!("{}: {err}", dir.display())) {
        let entry = entry.expect("dir entry");
        if entry.file_type().expect("file type").is_file() {
            out.insert(entry.file_name().to_string_lossy().into_owned());
        }
    }
    out
}

/// Comma-separated token list, the same reading both parsers document.
fn token_set(value: &str) -> BTreeSet<String> {
    if value.is_empty() || value == "-" {
        return BTreeSet::new();
    }
    value
        .split(',')
        .map(str::trim)
        .filter(|token| !token.is_empty())
        .map(str::to_string)
        .collect()
}

/// The parsed view must equal the raw rows, recomputed from the bytes here.
/// One uint32_t-typed TSV column (ABI `glk_module.abi_version` / `.size`,
/// `glk_hook.priority`, header `host_abi`). An accept case may not exceed it.
fn assert_uint32(case: &str, column: &str, value: &str) {
    assert!(
        value.parse::<u32>().is_ok(),
        "{case}: {column} = {value} is outside the ABI uint32 domain and cannot be produced"
    );
}

fn assert_accept_invariants(name: &str, text: &str, descriptor: &PluginDescriptor) {
    let mut plugin_rows = 0usize;
    let mut row_id = String::new();
    let mut row_stages: BTreeSet<String> = BTreeSet::new();
    let mut row_caps: BTreeSet<String> = BTreeSet::new();
    let mut params: Vec<(String, String, bool)> = Vec::new();
    let mut extracts: Vec<(String, String, bool)> = Vec::new();
    for raw in text.lines() {
        let line = raw.trim_end_matches('\r');
        if line.trim().is_empty() {
            continue;
        }
        let columns: Vec<&str> = line.split('\t').collect();
        match columns[0] {
            /* Corpus rule (Lead ruling 2026-10-05): every value in an ACCEPT
             * case must lie inside the ABI-declared domain, so the case is
             * something the probe can actually produce. These columns are
             * uint32_t in glk_contract_abi.h; a wider literal (e.g. size
             * 2^63-1) is an impossible input and must never be required to be
             * accepted — the check is mechanical so the rule cannot rot. */
            "host_abi" => assert_uint32(name, "host_abi", columns[1]),
            "plugin" => {
                plugin_rows += 1;
                assert_eq!(columns.len(), 8, "{name}: plugin columns");
                assert_uint32(name, "abi_version", columns[3]);
                assert_uint32(name, "size", columns[4]);
                row_id = columns[1].to_string();
                row_stages = token_set(columns[6]);
                row_caps = token_set(columns[7]);
            }
            "hook" => assert_uint32(name, "priority", columns[4]),
            "param" | "extract" => {
                assert_eq!(columns.len(), 7, "{name}: {line}");
                let entry = (
                    columns[2].to_string(),
                    columns[3].to_string(),
                    columns[4] == "1",
                );
                if columns[0] == "param" {
                    params.push(entry);
                } else {
                    extracts.push(entry);
                }
            }
            _ => {}
        }
    }
    assert_eq!(plugin_rows, 1, "{name}: exactly one plugin row");
    assert_eq!(row_id, descriptor.id, "{name}: plugin id");
    assert_eq!(row_stages, descriptor.stages, "{name}: stages");
    assert_eq!(row_caps, descriptor.required_caps, "{name}: required_caps");
    assert_eq!(params.len(), descriptor.params.len(), "{name}: param count");
    assert_eq!(
        extracts.len(),
        descriptor.extract.len(),
        "{name}: extract count"
    );
    for (index, (name_col, type_col, required)) in params.iter().enumerate() {
        let parsed = &descriptor.params[index];
        assert_eq!(*name_col, parsed.name, "{name}: param {index} name");
        assert_eq!(*type_col, parsed.kind.token(), "{name}: param {index} type");
        assert_eq!(*required, parsed.required, "{name}: param {index} required");
    }
    for (index, (name_col, type_col, required)) in extracts.iter().enumerate() {
        let parsed = &descriptor.extract[index];
        assert_eq!(*name_col, parsed.name, "{name}: extract {index} name");
        assert_eq!(
            *type_col,
            parsed.kind.token(),
            "{name}: extract {index} type"
        );
        assert_eq!(
            *required, parsed.required,
            "{name}: extract {index} required"
        );
    }
}

#[test]
fn probe_conformance_corpus() {
    let root = corpus_root();
    let manifest = read_manifest(&root);
    assert!(
        manifest.len() >= CORPUS_FLOOR,
        "the conformance corpus shrank below the approved floor of {CORPUS_FLOOR}"
    );
    for verdict in ["accept", "reject"] {
        let listed: BTreeSet<String> = manifest
            .iter()
            .filter(|(_, value)| value.as_str() == verdict)
            .map(|(name, _)| name.clone())
            .collect();
        let actual = directory_files(&root.join(verdict));
        assert_eq!(
            actual, listed,
            "corpus/{verdict} does not match manifest.tsv"
        );
    }
    let mut accepted = 0usize;
    let mut rejected = 0usize;
    for (name, verdict) in &manifest {
        let text = std::fs::read_to_string(root.join(verdict).join(name))
            .unwrap_or_else(|err| panic!("{verdict}/{name}: {err}"));
        if verdict == "accept" {
            let descriptor = parse_probe_tsv(&text)
                .unwrap_or_else(|err| panic!("accept case {name} was rejected: {err}"));
            assert_accept_invariants(name, &text, &descriptor);
            accepted += 1;
        } else {
            assert!(
                parse_probe_tsv(&text).is_err(),
                "reject case {name} was accepted"
            );
            rejected += 1;
        }
    }
    println!("probe conformance: {accepted} accept / {rejected} reject cases");
}
