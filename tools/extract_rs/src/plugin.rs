//! S4 P2 plugin extractor projection (contract-design 3.14.6 / 3.14.7).
//!
//! The extractor reads the FROZEN probe TSV (native
//! `ghostlock --plugin-probe <path.so>` stdout, contract-design 3.14.7.2) and
//! fills `plugin.<id>.extract.<key>` values for the profile it renders. The
//! descriptor is the ONLY authority for the key set and the declared type; this
//! module carries no plugin-specific key list of its own.
//!
//! Resolution ladder (approved P2 design, D2):
//!
//! * **R1** owner-qualified profile path (the same literal the rendered profile
//!   carries, restricted to `report::conf_wire_fields()`) — so an extract value
//!   is the profile value by construction, never a second derivation;
//! * **R3** BTF reference: `struct.<struct>.<field>` (byte offset) or
//!   `sizeof.<struct>`;
//! * **R2** kallsyms symbol name, reported base-relative like every other
//!   offset field.
//!
//! A recognized `struct.` / `sizeof.` reference is terminal: it never falls
//! through to a symbol of the same spelling. Fail-closed rules: a descriptor
//! type that contradicts the resolved value is a hard error; a required entry
//! that cannot be resolved is a hard error; an optional unresolvable entry is
//! omitted and reported. The descriptor default is NEVER substituted — writing
//! it would disguise "not derived" as a legal offset (the default's authority
//! is the plugin registration, not this crate).

use std::collections::{BTreeMap, BTreeSet};

use crate::btf::Btf;
use crate::error::{ExtractError, Result};
use crate::kallsyms;
use crate::spec::{ExtractSpec, MAX_SPECS, parse_spec_row};

/// Mirrors native `plugin::kMaxPluginsPerDocument` (src/core/plugin/wire.hpp):
/// one document carries at most 16 plugins, and the extractor refuses to build
/// more instead of silently dropping one.
pub const MAX_PLUGINS: usize = 16;

/// Wire kind literals — the exact spellings of the frozen probe TSV and of the
/// GLKv3 `WireKind` (contract-design 3.14.7.2).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ExtractKind {
    UInt,
    Int,
    Bool,
    Str,
}

impl ExtractKind {
    pub fn token(self) -> &'static str {
        match self {
            ExtractKind::UInt => "uint",
            ExtractKind::Int => "int",
            ExtractKind::Bool => "bool",
            ExtractKind::Str => "str",
        }
    }

    pub(crate) fn parse(text: &str) -> Option<ExtractKind> {
        match text {
            "uint" => Some(ExtractKind::UInt),
            "int" => Some(ExtractKind::Int),
            "bool" => Some(ExtractKind::Bool),
            "str" => Some(ExtractKind::Str),
            _ => None,
        }
    }
}

/// One declared `param` / `extract` row (the same seven frozen columns).
#[derive(Debug, Clone, PartialEq)]
pub struct DeclaredField {
    pub name: String,
    pub kind: ExtractKind,
    pub required: bool,
    /// Raw TSV default text (`-` = none). Diagnostics only: the descriptor's
    /// default is never written into the profile.
    pub default: Option<String>,
    pub doc: String,
}

/// One `hook` row; the extractor keeps it for parser fidelity (and so the
/// device golden can be asserted row for row), it does not act on hooks.
#[derive(Debug, Clone, PartialEq)]
pub struct HookRow {
    pub trigger: String,
    pub stage: String,
    pub priority: i64,
    pub name: String,
}

/// The subset of the probe TSV the extractor consumes, parsed fail-closed.
#[derive(Debug, Clone, PartialEq)]
pub struct PluginDescriptor {
    pub id: String,
    pub version: String,
    pub sha256: String,
    /// Declared hook stages (the plugin row's `stages` column, comma list).
    /// Retained so a conformance walker can prove the parsed view equals the
    /// raw row instead of trusting the parser.
    pub stages: BTreeSet<String>,
    /// Required capability tokens (the plugin row's `required_caps` column).
    pub required_caps: BTreeSet<String>,
    pub hooks: Vec<HookRow>,
    pub params: Vec<DeclaredField>,
    pub extract: Vec<DeclaredField>,
    /// P3 extraction declarations (native §10): what the plugin needs AND how
    /// to obtain it. An explicit chain never falls back to the implicit ladder.
    pub specs: Vec<ExtractSpec>,
    pub rejects: Vec<String>,
}

/* Frozen header vocabulary (contract-design 3.14.7.2, P1 revision 2026-10-05):
 * five keys, fixed order. The extractor consumes none of them — they delimit the
 * description block — so only host_abi / countermeasures_root are required, the
 * same two the Kotlin parser requires; stage_availability is recognized and its
 * shape validated when present. Its token vocabulary stays native's authority
 * and is deliberately NOT re-declared here. */
const HEADER_KEYS: [&str; 5] = [
    "host_abi",
    "countermeasures_root",
    "host_stages",
    "host_caps",
    "stage_availability",
];
const DESCRIPTION_KINDS: [&str; 6] = ["plugin", "hook", "param", "extract", "spec", "reject"];
const NONE: &str = "-";

fn fail(reason: impl std::fmt::Display) -> ExtractError {
    ExtractError::new(format!("plugin probe: {reason}"))
}

/// Empty TSV cells (and the null marker `-`) become an empty string.
pub(crate) fn text(value: &str) -> String {
    if value.is_empty() || value == NONE {
        String::new()
    } else {
        value.to_string()
    }
}

/// Byte bound every emitted or parsed name obeys (module id, hook name,
/// param/extract name). It mirrors the loader-visible limit (D7 ruling: a probe
/// and its consumers must share one bound, not each test their own).
const MAX_NAME_BYTES: usize = 64;

/// Comma-separated token list, mirroring the Kotlin `list()`: a null marker or
/// empty cell is the empty set, elements are trimmed and empty elements dropped.
fn token_list(value: &str) -> BTreeSet<String> {
    if value.is_empty() || value == NONE {
        return BTreeSet::new();
    }
    value
        .split(',')
        .map(str::trim)
        .filter(|token| !token.is_empty())
        .map(str::to_string)
        .collect()
}

/// A name that must be present, ASCII-clean and within the shared byte bound.
pub(crate) fn check_name(label: &str, name: &str, line: &str) -> Result<()> {
    if name.trim().is_empty() {
        return Err(fail(format!("empty {label}: {line}")));
    }
    if name.chars().any(char::is_control) {
        return Err(fail(format!("{label} has a control character: {line}")));
    }
    if name.len() > MAX_NAME_BYTES {
        return Err(fail(format!(
            "{label} exceeds {MAX_NAME_BYTES} bytes: {line}"
        )));
    }
    Ok(())
}

fn valid_sha256(value: &str) -> bool {
    value.len() == 64
        && value
            .bytes()
            .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
}

fn validate_default(kind: ExtractKind, raw: &str, line: &str) -> Result<()> {
    let ok = match kind {
        ExtractKind::UInt => raw.parse::<u64>().is_ok(),
        ExtractKind::Int => raw.parse::<i64>().is_ok(),
        ExtractKind::Bool => raw == "0" || raw == "1",
        ExtractKind::Str => true,
    };
    if ok {
        Ok(())
    } else {
        Err(fail(format!(
            "{} default is not a {} literal: {line}",
            kind.token(),
            kind.token()
        )))
    }
}

fn declared_field(kind_label: &str, parts: &[&str], line: &str) -> Result<DeclaredField> {
    let name = parts[2];
    check_name(&format!("{kind_label} name"), name, line)?;
    let kind = ExtractKind::parse(parts[3])
        .ok_or_else(|| fail(format!("unknown {kind_label} type: {line}")))?;
    let required = match parts[4] {
        "0" => false,
        "1" => true,
        _ => return Err(fail(format!("required must be 0 or 1: {line}"))),
    };
    let default = match parts[5] {
        "" | NONE => None,
        raw => {
            validate_default(kind, raw, line)?;
            Some(raw.to_string())
        }
    };
    Ok(DeclaredField {
        name: name.to_string(),
        kind,
        required,
        default,
        doc: text(parts[6]),
    })
}

/// A row names its plugin; `-` (unparseable path) and rows before the plugin
/// row are accepted, any other id is a contract violation.
fn owner_check(owner: &str, id: Option<&str>, line: &str) -> Result<()> {
    if owner == NONE {
        return Ok(());
    }
    if let Some(id) = id {
        if owner != id {
            return Err(fail(format!("row belongs to another plugin: {line}")));
        }
    }
    Ok(())
}

/// Shape check for the P1 revision's fifth header row,
/// `<backend>:<stage>[,<stage>];<backend>:<stage>[;…]`, mirroring the Kotlin
/// parser: a missing colon, an empty backend or stage list, a repeated backend
/// group or a repeated stage inside one group fails closed; `-` means "the
/// probe reports nothing". The backend/stage VOCABULARY is native's authority
/// and is not re-declared here.
fn validate_stage_availability(value: &str, line: &str) -> Result<()> {
    if value.is_empty() || value == NONE {
        return Ok(());
    }
    let mut backends: BTreeSet<&str> = BTreeSet::new();
    for group in value.split(';') {
        let Some((backend, stages)) = group.split_once(':') else {
            return Err(fail(format!(
                "stage_availability needs <backend>:<stages>: {line}"
            )));
        };
        let backend = backend.trim();
        if backend.is_empty() {
            return Err(fail(format!(
                "stage_availability has an empty backend: {line}"
            )));
        }
        let stages: Vec<&str> = stages
            .split(',')
            .map(str::trim)
            .filter(|stage| !stage.is_empty())
            .collect();
        if stages.is_empty() {
            return Err(fail(format!(
                "stage_availability has an empty stage list: {line}"
            )));
        }
        let unique: BTreeSet<&str> = stages.iter().copied().collect();
        if unique.len() != stages.len() {
            return Err(fail(format!("stage_availability repeats a stage: {line}")));
        }
        if !backends.insert(backend) {
            return Err(fail(format!(
                "stage_availability repeats a backend: {line}"
            )));
        }
    }
    Ok(())
}

fn expect_columns(parts: &[&str], count: usize, line: &str) -> Result<()> {
    if parts.len() == count {
        Ok(())
    } else {
        Err(fail(format!(
            "line needs {count} tab-separated columns: {line}"
        )))
    }
}

/// Strict parser for the frozen probe TSV (contract-design 3.14.7.2, five-key
/// P1 revision). Mirrors the Kotlin `PluginProbe.parse` rules: header rows
/// first over the five frozen keys (`host_abi`, `countermeasures_root`,
/// `host_stages`, `host_caps`, `stage_availability`; only the first two are
/// required, the same two Kotlin requires), one `plugin` row, frozen column
/// counts per kind, `0|1` required flags, the four type literals, a
/// 64-character lower-case hex digest and no duplicate declared names. Names
/// (plugin id, hook name, param/extract name) are non-empty, control-free and
/// at most `MAX_NAME_BYTES` bytes — the same bound the loader enforces (D7), so
/// the probe and its consumers cannot disagree about a name the host will later
/// refuse. The plugin row's `stages` / `required_caps` lists are retained for
/// the conformance walker. Every violation fails closed.
pub fn parse_probe_tsv(text_value: &str) -> Result<PluginDescriptor> {
    let mut id: Option<String> = None;
    let mut version = String::new();
    let mut sha256 = String::new();
    let mut stages: BTreeSet<String> = BTreeSet::new();
    let mut required_caps: BTreeSet<String> = BTreeSet::new();
    let mut hooks: Vec<HookRow> = Vec::new();
    let mut params: Vec<DeclaredField> = Vec::new();
    let mut extract: Vec<DeclaredField> = Vec::new();
    let mut specs: Vec<ExtractSpec> = Vec::new();
    let mut rejects: Vec<String> = Vec::new();
    let mut header_keys: BTreeSet<String> = BTreeSet::new();
    let mut description_started = false;

    for raw in text_value.lines() {
        let line = raw.trim_end_matches('\r');
        if line.trim().is_empty() {
            continue;
        }
        let kind = line.split('\t').next().unwrap_or("");
        if !DESCRIPTION_KINDS.contains(&kind) {
            if description_started {
                return Err(fail(format!(
                    "header row after the description block: {line}"
                )));
            }
            let parts: Vec<&str> = line.split('\t').collect();
            expect_columns(&parts, 2, line)?;
            if !HEADER_KEYS.contains(&parts[0]) {
                return Err(fail(format!("unknown header key: {line}")));
            }
            if !header_keys.insert(parts[0].to_string()) {
                return Err(fail(format!("duplicate header key: {line}")));
            }
            if parts[0] == "host_abi" && parts[1].parse::<u32>().is_err() {
                return Err(fail(format!("host_abi is not a number: {line}")));
            }
            if parts[0] == "stage_availability" {
                validate_stage_availability(parts[1], line)?;
            }
            continue;
        }

        description_started = true;
        let parts: Vec<&str> = line.split('\t').collect();
        match kind {
            "plugin" => {
                expect_columns(&parts, 8, line)?;
                if id.is_some() {
                    return Err(fail(format!("duplicate plugin row: {line}")));
                }
                let plugin_id = parts[1];
                if plugin_id == NONE {
                    return Err(fail(format!("empty plugin id: {line}")));
                }
                check_name("plugin id", plugin_id, line)?;
                if parts[3].parse::<u32>().is_err() {
                    return Err(fail(format!("abi_version is not a number: {line}")));
                }
                if parts[4].parse::<u64>().is_err() {
                    return Err(fail(format!("size is not a number: {line}")));
                }
                let digest = text(parts[5]);
                if !valid_sha256(&digest) {
                    return Err(fail(format!(
                        "sha256 must be 64 lower-case hex characters: {line}"
                    )));
                }
                version = text(parts[2]);
                sha256 = digest;
                stages = token_list(parts[6]);
                required_caps = token_list(parts[7]);
                id = Some(plugin_id.to_string());
            }
            "hook" => {
                expect_columns(&parts, 6, line)?;
                owner_check(parts[1], id.as_deref(), line)?;
                let trigger = text(parts[2]);
                if trigger.is_empty() {
                    return Err(fail(format!("hook trigger is missing: {line}")));
                }
                let stage = text(parts[3]);
                if stage.is_empty() {
                    return Err(fail(format!("hook stage is missing: {line}")));
                }
                let priority = parts[4]
                    .parse::<i64>()
                    .map_err(|_| fail(format!("hook priority is not a number: {line}")))?;
                let name = text(parts[5]);
                check_name("hook name", &name, line)?;
                hooks.push(HookRow {
                    trigger,
                    stage,
                    priority,
                    name,
                });
            }
            "param" | "extract" => {
                expect_columns(&parts, 7, line)?;
                owner_check(parts[1], id.as_deref(), line)?;
                let field = declared_field(kind, &parts, line)?;
                if kind == "param" {
                    params.push(field);
                } else {
                    extract.push(field);
                }
            }
            "spec" => {
                expect_columns(&parts, crate::spec::SPEC_COLUMNS, line)?;
                owner_check(parts[1], id.as_deref(), line)?;
                if specs.len() >= MAX_SPECS {
                    return Err(fail(format!("more than {MAX_SPECS} spec rows: {line}")));
                }
                specs.push(parse_spec_row(&parts, line)?);
            }
            "reject" => {
                expect_columns(&parts, 3, line)?;
                owner_check(parts[1], id.as_deref(), line)?;
                let reason = text(parts[2]);
                if reason.is_empty() {
                    return Err(fail(format!("reject reason is missing: {line}")));
                }
                rejects.push(reason);
            }
            _ => return Err(fail(format!("unknown row kind: {line}"))),
        }
    }

    let id = id.ok_or_else(|| fail("no plugin row"))?;
    if !header_keys.contains("host_abi") {
        return Err(fail("header has no host_abi"));
    }
    if !header_keys.contains("countermeasures_root") {
        return Err(fail("header has no countermeasures_root"));
    }
    for (label, fields) in [("parameter", &params), ("extract", &extract)] {
        let mut names = BTreeSet::new();
        for field in fields.iter() {
            if !names.insert(field.name.clone()) {
                return Err(fail(format!(
                    "duplicate {label} name for {id}: {}",
                    field.name
                )));
            }
        }
    }
    /* Native §9.3/§10.2: one name, one authority. An extract entry and a spec
     * entry with the same name reject the whole module (no precedence rule). */
    for spec in &specs {
        if extract.iter().any(|field| field.name == spec.name) {
            return Err(fail(format!(
                "{id}: extract and spec both declare {}",
                spec.name
            )));
        }
    }

    Ok(PluginDescriptor {
        id,
        version,
        sha256,
        stages,
        required_caps,
        hooks,
        params,
        extract,
        specs,
        rejects,
    })
}

/// Conservative lower bound for a plugin id the extractor may emit (P2 ruling
/// D6): ASCII, non-empty, at most 64 bytes, no whitespace and no control
/// characters. The stricter App rule (Kotlin `PluginPaths.isValidId`) is
/// deliberately NOT copied here — the extractor must not become a third
/// authority for it.
pub fn id_is_emit_safe(id: &str) -> bool {
    !id.is_empty()
        && id.len() <= 64
        && id.is_ascii()
        && !id.chars().any(|ch| ch.is_whitespace() || ch.is_control())
}

/// Cross-descriptor checks: at most `MAX_PLUGINS`, emit-safe unique ids, and no
/// descriptor the host itself rejects (a `reject` row means the module would
/// not register, so no extract value may be produced for it).
pub fn validate_descriptors(descriptors: &[PluginDescriptor]) -> Result<()> {
    if descriptors.len() > MAX_PLUGINS {
        return Err(ExtractError::new(format!(
            "at most {MAX_PLUGINS} plugin descriptors are supported, got {}",
            descriptors.len()
        )));
    }
    let mut ids: BTreeSet<&str> = BTreeSet::new();
    for descriptor in descriptors {
        if !id_is_emit_safe(&descriptor.id) {
            return Err(ExtractError::new(format!(
                "plugin id {:?} is not safe to emit (need ASCII, non-empty, <= 64 bytes, no whitespace or control characters)",
                descriptor.id
            )));
        }
        if !descriptor.rejects.is_empty() {
            return Err(ExtractError::new(format!(
                "plugin {} would be rejected by the host probe: {}",
                descriptor.id,
                descriptor.rejects.join("; ")
            )));
        }
        if !ids.insert(descriptor.id.as_str()) {
            return Err(ExtractError::new(format!(
                "duplicate plugin descriptor for id {}",
                descriptor.id
            )));
        }
    }
    Ok(())
}

/// One typed value written to `plugin.<id>.extract.<key>`.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ExtractValue {
    UInt(u64),
    Int(i64),
    Bool(bool),
    Str(String),
}

impl ExtractValue {
    pub fn kind(&self) -> ExtractKind {
        match self {
            ExtractValue::UInt(_) => ExtractKind::UInt,
            ExtractValue::Int(_) => ExtractKind::Int,
            ExtractValue::Bool(_) => ExtractKind::Bool,
            ExtractValue::Str(_) => ExtractKind::Str,
        }
    }
}

/// Everything the three resolution rungs may consult.
pub struct ExtractContext<'a> {
    /// Flattened `path -> rendered literal` of the profile being produced (R1).
    pub profile: &'a BTreeMap<String, String>,
    /// Owner-qualified paths the extractor declares it can produce
    /// (`report::conf_wire_fields()`); R1 is restricted to this set so no
    /// implicit path vocabulary appears.
    pub profile_paths: &'a BTreeSet<String>,
    /// Image BTF, when the boot image carried one (R3).
    pub btf: Option<&'a Btf>,
    /// Full recovered kallsyms table, not the profile whitelist (R2).
    pub symbols: &'a BTreeMap<String, BTreeSet<u64>>,
    /// Kernel image base; symbol values are reported base-relative, matching
    /// every profile offset field (R2).
    pub base: u64,
}

/// Outcome of one declared name: the value, or why nothing was produced.
#[derive(Debug)]
pub struct Resolution {
    pub value: Option<ExtractValue>,
    /// Named rung/result, used verbatim in diagnostics.
    pub detail: String,
}

fn unresolved(detail: impl Into<String>) -> Resolution {
    Resolution {
        value: None,
        detail: detail.into(),
    }
}

fn resolved(detail: impl Into<String>, value: ExtractValue) -> Resolution {
    Resolution {
        value: Some(value),
        detail: detail.into(),
    }
}

fn number_value(source: &str, kind: ExtractKind, value: u64) -> Result<ExtractValue> {
    match kind {
        ExtractKind::UInt => Ok(ExtractValue::UInt(value)),
        ExtractKind::Int => i64::try_from(value).map(ExtractValue::Int).map_err(|_| {
            ExtractError::new(format!(
                "plugin extract '{source}' is declared int but the source value {value} exceeds i64"
            ))
        }),
        other => Err(ExtractError::new(format!(
            "plugin extract '{source}' is declared {} but the source is a number",
            other.token()
        ))),
    }
}

fn type_mismatch(source: &str, kind: ExtractKind, value: &ExtractValue) -> ExtractError {
    ExtractError::new(format!(
        "plugin extract '{source}' is declared {} but the resolved value is {}",
        kind.token(),
        value.kind().token()
    ))
}

/// Parses one rendered profile literal under a declared kind.
fn parse_literal(source: &str, literal: &str, kind: ExtractKind) -> Result<ExtractValue> {
    let value = match literal {
        "true" => ExtractValue::Bool(true),
        "false" => ExtractValue::Bool(false),
        other if other.starts_with('"') => ExtractValue::Str(unquote(other)?),
        other => {
            let number = other.parse::<i128>().map_err(|_| {
                ExtractError::new(format!(
                    "plugin extract '{source}': profile literal {other} is not a number, bool or string"
                ))
            })?;
            if number < 0 {
                ExtractValue::Int(i64::try_from(number).map_err(|_| {
                    ExtractError::new(format!(
                        "plugin extract '{source}': profile literal {number} exceeds i64"
                    ))
                })?)
            } else {
                ExtractValue::UInt(u64::try_from(number).map_err(|_| {
                    ExtractError::new(format!(
                        "plugin extract '{source}': profile literal {number} exceeds u64"
                    ))
                })?)
            }
        }
    };
    match (kind, &value) {
        (ExtractKind::Bool, ExtractValue::Bool(_)) => Ok(value),
        (ExtractKind::Str, ExtractValue::Str(_)) => Ok(value),
        (ExtractKind::UInt, ExtractValue::UInt(_)) => Ok(value),
        (ExtractKind::Int, ExtractValue::Int(_)) => Ok(value),
        (ExtractKind::Int, ExtractValue::UInt(number)) => i64::try_from(*number)
            .map(ExtractValue::Int)
            .map_err(|_| type_mismatch(source, kind, &value)),
        (ExtractKind::UInt, ExtractValue::Int(number)) if *number >= 0 => {
            Ok(ExtractValue::UInt(*number as u64))
        }
        _ => Err(type_mismatch(source, kind, &value)),
    }
}

/// Unquotes a HOCON string literal produced by the profile renderer.
fn unquote(literal: &str) -> Result<String> {
    let body = literal
        .strip_prefix('"')
        .and_then(|rest| rest.strip_suffix('"'))
        .ok_or_else(|| {
            ExtractError::new(format!("profile literal {literal} is not a closed string"))
        })?;
    let mut out = String::with_capacity(body.len());
    let mut chars = body.chars();
    while let Some(ch) = chars.next() {
        if ch != '\\' {
            out.push(ch);
            continue;
        }
        match chars.next() {
            Some('"') => out.push('"'),
            Some('\\') => out.push('\\'),
            Some('n') => out.push('\n'),
            Some('t') => out.push('\t'),
            Some(other) => {
                return Err(ExtractError::new(format!(
                    "profile literal {literal} has an unsupported escape \\{other}"
                )));
            }
            None => {
                return Err(ExtractError::new(format!(
                    "profile literal {literal} ends with a dangling escape"
                )));
            }
        }
    }
    Ok(out)
}

/// R1 only: an owner-qualified profile path, restricted to the declared set.
/// Shared by the implicit ladder and by explicit `spec` method chains.
pub(crate) fn resolve_profile_path(
    name: &str,
    kind: ExtractKind,
    context: &ExtractContext<'_>,
) -> Result<Resolution> {
    if !context.profile_paths.contains(name) {
        return Ok(unresolved(format!(
            "R1 profile path {name}: not a declared profile path"
        )));
    }
    match context.profile.get(name) {
        None => Ok(unresolved(format!(
            "R1 profile path {name}: not emitted by this profile"
        ))),
        Some(literal) if literal == "null" => Ok(unresolved(format!(
            "R1 profile path {name}: this image yielded no value"
        ))),
        Some(literal) => {
            let value = parse_literal(name, literal, kind)?;
            Ok(resolved(format!("R1 profile path {name}"), value))
        }
    }
}

/// R3 only: `struct.<struct>.<field>` or `sizeof.<struct>`. A malformed
/// reference is a hard error; a well-formed miss is `Ok` without a value.
pub(crate) fn resolve_btf(
    name: &str,
    kind: ExtractKind,
    context: &ExtractContext<'_>,
) -> Result<Resolution> {
    if let Some(rest) = name.strip_prefix("struct.") {
        let (struct_name, field_name) = rest.rsplit_once('.').ok_or_else(|| {
            ExtractError::new(format!(
                "plugin extract '{name}': expected struct.<struct>.<field>"
            ))
        })?;
        if struct_name.is_empty() || field_name.is_empty() {
            return Err(ExtractError::new(format!(
                "plugin extract '{name}': expected struct.<struct>.<field>"
            )));
        }
        let Some(btf) = context.btf else {
            return Ok(unresolved(format!("R3 {name}: the image carries no BTF")));
        };
        return match btf.field(struct_name, field_name) {
            Some(offset) => {
                let value = number_value(name, kind, u64::from(offset))?;
                Ok(resolved(
                    format!("R3 BTF {struct_name}.{field_name} byte offset"),
                    value,
                ))
            }
            None => Ok(unresolved(format!(
                "R3 BTF has no {struct_name}.{field_name}"
            ))),
        };
    }
    if let Some(struct_name) = name.strip_prefix("sizeof.") {
        if struct_name.is_empty() {
            return Err(ExtractError::new(format!(
                "plugin extract '{name}': expected sizeof.<struct>"
            )));
        }
        let Some(btf) = context.btf else {
            return Ok(unresolved(format!("R3 {name}: the image carries no BTF")));
        };
        return match btf.size(struct_name) {
            Some(size) => {
                let value = number_value(name, kind, u64::from(size))?;
                Ok(resolved(format!("R3 BTF sizeof({struct_name})"), value))
            }
            None => Ok(unresolved(format!("R3 BTF has no struct {struct_name}"))),
        };
    }

    Ok(unresolved(format!(
        "R3 {name}: not a struct./sizeof. reference"
    )))
}

/// R2 only: a kallsyms symbol name, reported base-relative.
pub(crate) fn resolve_kallsyms(
    name: &str,
    kind: ExtractKind,
    context: &ExtractContext<'_>,
) -> Result<Resolution> {
    match kallsyms::unique(context.symbols, name) {
        Some(address) => match address.checked_sub(context.base) {
            Some(offset) => {
                let value = number_value(name, kind, offset)?;
                Ok(resolved(
                    format!("R2 kallsyms symbol {name} (base-relative)"),
                    value,
                ))
            }
            None => Ok(unresolved(format!(
                "R2 symbol {name} resolves below the kernel image base"
            ))),
        },
        None => Ok(unresolved(format!(
            "R2 symbol {name}: not a unique kallsyms symbol"
        ))),
    }
}

/// Resolves one declared `extract` name with the P2 implicit ladder
/// R1(profile) -> R3(btf) -> R2(kallsyms). `Err` is a hard contract violation
/// (a declared type that contradicts the value, or a malformed `struct.`
/// reference); `Ok` with no value is "not derivable from this image".
pub fn resolve_extract(
    name: &str,
    kind: ExtractKind,
    context: &ExtractContext<'_>,
) -> Result<Resolution> {
    /* R1: restricted to the declared set, and terminal when the name is one. */
    if context.profile_paths.contains(name) {
        return resolve_profile_path(name, kind, context);
    }
    /* R3: an explicit BTF reference. A recognized prefix is terminal. */
    let r3 = resolve_btf(name, kind, context)?;
    if r3.value.is_some() || name.starts_with("struct.") || name.starts_with("sizeof.") {
        return Ok(r3);
    }
    /* R2: a kallsyms symbol name, base-relative. A symbol that exists but
     * cannot form a value (e.g. below the image base) keeps its own reason. */
    let r2 = resolve_kallsyms(name, kind, context)?;
    if r2.value.is_some() || kallsyms::unique(context.symbols, name).is_some() {
        return Ok(r2);
    }
    Ok(unresolved(format!(
        "not a declared profile path, not a struct./sizeof. BTF reference, and not a unique kallsyms symbol: {name}"
    )))
}

/// Per-plugin resolution result: the entries to emit, plus the optional names
/// that were omitted (with the named reason for the diagnostic).
#[derive(Debug, Default)]
pub struct ExtractOutcome {
    pub entries: Vec<(String, ExtractValue)>,
    pub skipped: Vec<(String, String)>,
}

/// Applies the approved failure policy (D4): a hard resolution error and a
/// required-but-underivable entry are fatal; an optional underivable entry is
/// omitted, never defaulted.
pub fn resolve_descriptor(
    descriptor: &PluginDescriptor,
    context: &ExtractContext<'_>,
) -> Result<ExtractOutcome> {
    let mut outcome = ExtractOutcome::default();
    for declared in &descriptor.extract {
        let resolution = resolve_extract(&declared.name, declared.kind, context)?;
        match resolution.value {
            Some(value) => outcome.entries.push((declared.name.clone(), value)),
            None if declared.required => {
                return Err(ExtractError::unsupported(format!(
                    "plugin {} extract '{}' is required but not derivable ({})",
                    descriptor.id, declared.name, resolution.detail
                )));
            }
            None => outcome
                .skipped
                .push((declared.name.clone(), resolution.detail)),
        }
    }
    Ok(outcome)
}

#[cfg(test)]
mod tests {
    use super::{
        DeclaredField, ExtractContext, ExtractKind, ExtractValue, PluginDescriptor,
        id_is_emit_safe, parse_probe_tsv, resolve_descriptor, resolve_extract,
        validate_descriptors,
    };
    use crate::btf::{Btf, KIND_INT, KIND_STRUCT};
    use std::collections::{BTreeMap, BTreeSet};

    const HEADER: &str = "host_abi\t1\ncountermeasures_root\tcountermeasures\nhost_stages\tpre_spawn,post_terminal\nhost_caps\tkernel_read,alias\n";
    const PLUGIN_ROW: &str = "plugin\ttest.schema\t1.0\t1\t80\tdecc767346129b6dea4a8fb8d907daa13c48d9a44fd60645d5e57e42614205cf\tpre_spawn,post_terminal\tkernel_read,alias\n";

    fn tsv(body: &str) -> String {
        format!("{HEADER}{PLUGIN_ROW}{body}")
    }

    fn golden() -> String {
        std::fs::read_to_string(concat!(
            env!("CARGO_MANIFEST_DIR"),
            "/../../app/src/test/resources/plugin-probe-golden.tsv"
        ))
        .expect("device probe golden")
    }

    /// Minimal BTF blob: INT "int" plus STRUCT "cred" of size 16 with one member
    /// "security" at bit offset 96 (byte 12).
    fn image_btf() -> Btf {
        let strings: &[u8] = b"\0int\0cred\0security\0";
        let mut types: Vec<u8> = Vec::new();
        /* type 1: INT "int", size 4; INT carries four extra bytes. */
        types.extend_from_slice(&1u32.to_le_bytes());
        types.extend_from_slice(&(KIND_INT << 24).to_le_bytes());
        types.extend_from_slice(&4u32.to_le_bytes());
        types.extend_from_slice(&[0u8, 0, 0, 0]);
        /* type 2: STRUCT "cred", size 16, one member at bit 96. */
        types.extend_from_slice(&5u32.to_le_bytes());
        types.extend_from_slice(&((KIND_STRUCT << 24) | 1u32).to_le_bytes());
        types.extend_from_slice(&16u32.to_le_bytes());
        types.extend_from_slice(&10u32.to_le_bytes());
        types.extend_from_slice(&1u32.to_le_bytes());
        types.extend_from_slice(&96u32.to_le_bytes());

        let mut raw: Vec<u8> = Vec::new();
        raw.extend_from_slice(&crate::boot::BTF_MAGIC.to_le_bytes());
        raw.push(1);
        raw.push(0);
        raw.extend_from_slice(&24u32.to_le_bytes());
        raw.extend_from_slice(&0u32.to_le_bytes());
        raw.extend_from_slice(&(types.len() as u32).to_le_bytes());
        raw.extend_from_slice(&(types.len() as u32).to_le_bytes());
        raw.extend_from_slice(&(strings.len() as u32).to_le_bytes());
        raw.extend_from_slice(&types);
        raw.extend_from_slice(strings);
        Btf::new(&raw).expect("fixture BTF")
    }

    #[test]
    fn probe_tsv_parses_the_device_golden() {
        let descriptor = parse_probe_tsv(&golden()).expect("golden parses");
        assert_eq!(descriptor.id, "test.schema");
        assert_eq!(descriptor.version, "1.2.3");
        assert_eq!(
            descriptor.sha256,
            "decc767346129b6dea4a8fb8d907daa13c48d9a44fd60645d5e57e42614205cf"
        );
        assert_eq!(descriptor.hooks.len(), 1);
        assert_eq!(descriptor.hooks[0].stage, "post_terminal");
        assert_eq!(descriptor.hooks[0].priority, 10);
        let kinds: Vec<&str> = descriptor
            .params
            .iter()
            .map(|param| param.kind.token())
            .collect();
        assert_eq!(kinds, ["uint", "str", "bool", "int"]);
        assert_eq!(descriptor.extract.len(), 1);
        let extract = &descriptor.extract[0];
        assert_eq!(extract.name, "task_offset");
        assert_eq!(extract.kind, ExtractKind::UInt);
        assert!(extract.required);
        assert_eq!(extract.default.as_deref(), Some("0"));
        assert!(descriptor.rejects.is_empty());
    }

    /// The reference plugin (`ghostlock-plugin-example`, formerly
    /// tools/plugins) as the REAL probe reports it. It pins the frozen five-key
    /// header plus the reference descriptor this crate is expected to consume.
    ///
    /// The `host_caps` line carries the batch B vocabulary
    /// (`…,child_task,log`, per the device golden
    /// `build/gate-logs/plugin-probe-golden-device.tsv`, sha256 1a6e49d8…);
    /// the sha256 column stays a HOST build of the example module, not a device
    /// capture, so it is only a fixture value here.
    #[test]
    fn probe_tsv_parses_the_reference_plugin_output() {
        const REFERENCE: &str = "host_abi\t1\ncountermeasures_root\t-\nhost_stages\tpre_spawn,post_spawn,pre_terminal,post_terminal\nhost_caps\tkernel_read,kernel_write,alias,child_task,log\nstage_availability\t43499:pre_terminal;43284:post_terminal\nplugin\tglk.probe\t1.0\t1\t80\t6040107978f6211083d3730c833e0c9bd0aa74933204eb581d00eec90c97dd6b\tpost_terminal\tkernel_read,kernel_write\nhook\tglk.probe\ton_stage\tpost_terminal\t0\tglk.probe\nparam\tglk.probe\ttarget_va\tuint\t1\t18446743524671239168\tkernel VA the probe reads and writes back\nparam\tglk.probe\tlabel\tstr\t0\t-\tdiagnostic label for the glk.probe log line\nextract\tglk.probe\tplatform.abi.offset.init_task\tuint\t1\t0\tinit_task image offset (base-relative), from the extractor profile path\n";
        let descriptor = parse_probe_tsv(REFERENCE).expect("reference plugin output parses");
        assert_eq!(descriptor.id, "glk.probe");
        assert_eq!(descriptor.version, "1.0");
        assert_eq!(descriptor.hooks.len(), 1);
        assert_eq!(descriptor.hooks[0].stage, "post_terminal");
        assert_eq!(descriptor.hooks[0].trigger, "on_stage");
        assert_eq!(descriptor.params.len(), 2);
        assert_eq!(descriptor.params[0].name, "target_va");
        assert_eq!(descriptor.params[0].kind, ExtractKind::UInt);
        assert!(descriptor.params[0].required);
        assert_eq!(
            descriptor.params[0].default.as_deref(),
            Some("18446743524671239168")
        );
        assert_eq!(descriptor.params[1].name, "label");
        assert_eq!(descriptor.params[1].kind, ExtractKind::Str);
        assert!(!descriptor.params[1].required);
        assert_eq!(descriptor.params[1].default, None);
        assert_eq!(descriptor.extract.len(), 1);
        assert_eq!(descriptor.extract[0].name, "platform.abi.offset.init_task");
        assert_eq!(descriptor.extract[0].kind, ExtractKind::UInt);
        assert!(descriptor.extract[0].required);
        assert!(descriptor.rejects.is_empty());

        /* Batch B vocabulary: a module that REQUIRES host logging declares the
         * `log` token; the crate must retain it (the ABI bit itself is native's
         * authority, the extractor only carries the token through). */
        let with_log = REFERENCE.replace(
            "post_terminal\tkernel_read,kernel_write",
            "post_terminal\tkernel_read,kernel_write,log",
        );
        let descriptor = parse_probe_tsv(&with_log).expect("log capability token parses");
        assert!(descriptor.required_caps.contains("log"));
        assert!(descriptor.required_caps.contains("kernel_read"));

        /* "-" is the legal "no availability reported" value (older probe). */
        let without = REFERENCE.replace(
            "stage_availability\t43499:pre_terminal;43284:post_terminal",
            "stage_availability\t-",
        );
        assert!(parse_probe_tsv(&without).is_ok());
    }

    #[test]
    fn probe_tsv_fails_closed_on_malformed_input() {
        let bad = [
            /* header key outside the frozen four */
            format!("host_abi\t1\ncountermeasures_root\tcountermeasures\nmystery\t1\n{PLUGIN_ROW}"),
            /* header row after the description block */
            format!("{HEADER}{PLUGIN_ROW}host_abi\t1\n"),
            /* missing countermeasures_root */
            format!("host_abi\t1\n{PLUGIN_ROW}"),
            /* frozen column count (param with six columns) */
            tsv("param\ttest.schema\tx\tuint\t1\n"),
            /* unknown type literal */
            tsv("extract\ttest.schema\tx\tfloat\t1\t-\tdoc\n"),
            /* required is not 0/1 */
            tsv("extract\ttest.schema\tx\tuint\t2\t-\tdoc\n"),
            /* duplicate plugin row */
            format!("{HEADER}{PLUGIN_ROW}{PLUGIN_ROW}"),
            /* row of another plugin */
            tsv("extract\tother.plugin\tx\tuint\t1\t-\tdoc\n"),
            /* duplicate extract name */
            tsv(
                "extract\ttest.schema\tx\tuint\t1\t-\tdoc\nextract\ttest.schema\tx\tuint\t0\t-\tdoc\n",
            ),
            /* empty extract name */
            tsv("extract\ttest.schema\t\tuint\t1\t-\tdoc\n"),
            /* digest not lower-case hex */
            format!("{HEADER}plugin\ttest.schema\t1.0\t1\t80\tDEADBEEF\tpre_spawn\tkernel_read\n"),
            /* no plugin row at all */
            HEADER.to_string(),
            /* uint default that is not a number */
            tsv("extract\ttest.schema\tx\tuint\t1\tlater\tdoc\n"),
            /* P1 revision: stage_availability shape violations */
            format!("{HEADER}stage_availability\t43499\n{PLUGIN_ROW}"),
            format!("{HEADER}stage_availability\t:pre_terminal\n{PLUGIN_ROW}"),
            format!("{HEADER}stage_availability\t43499:\n{PLUGIN_ROW}"),
            format!("{HEADER}stage_availability\t43499:pre_terminal;43499:pre_spawn\n{PLUGIN_ROW}"),
            format!("{HEADER}stage_availability\t43499:pre_terminal,pre_terminal\n{PLUGIN_ROW}"),
        ];
        for (index, text) in bad.iter().enumerate() {
            assert!(
                parse_probe_tsv(text).is_err(),
                "case {index} must fail closed"
            );
        }
    }

    #[test]
    fn reject_rows_are_kept_and_make_the_plugin_unusable() {
        let descriptor = parse_probe_tsv(&tsv("reject\ttest.schema\tStageRejected\n"))
            .expect("reject row parses");
        assert_eq!(descriptor.rejects, ["StageRejected"]);
        assert!(validate_descriptors(&[descriptor]).is_err());
    }

    #[test]
    fn id_emit_safety_is_a_conservative_lower_bound() {
        for good in ["a", "vivo.vr_guard", "plugin-1_2", "A"] {
            assert!(id_is_emit_safe(good), "{good} must be accepted");
        }
        for bad in ["", "has space", "tab\tid", "line\nid", "héllo"] {
            assert!(!id_is_emit_safe(bad), "{bad} must be rejected");
        }
        assert!(!id_is_emit_safe(&"a".repeat(65)));
    }

    fn descriptor_of(extract: &[(&str, ExtractKind, bool)]) -> PluginDescriptor {
        PluginDescriptor {
            id: "test.schema".to_string(),
            version: "1.0".to_string(),
            sha256: "0".repeat(64),
            stages: BTreeSet::new(),
            required_caps: BTreeSet::new(),
            hooks: Vec::new(),
            params: Vec::new(),
            specs: Vec::new(),
            extract: extract
                .iter()
                .map(|(name, kind, required)| DeclaredField {
                    name: (*name).to_string(),
                    kind: *kind,
                    required: *required,
                    /* A default is present but must never be written when the
                     * value cannot be derived. */
                    default: Some("0".to_string()),
                    doc: String::new(),
                })
                .collect(),
            rejects: Vec::new(),
        }
    }

    #[test]
    fn descriptor_validation_rejects_rejects_duplicates_and_bad_ids() {
        let clean = descriptor_of(&[("init_task", ExtractKind::UInt, true)]);
        assert!(validate_descriptors(&[clean.clone()]).is_ok());
        let mut rejected = clean.clone();
        rejected.rejects.push("StageRejected".to_string());
        assert!(validate_descriptors(&[rejected]).is_err());
        assert!(validate_descriptors(&[clean.clone(), clean.clone()]).is_err());
        let mut long_id = clean.clone();
        long_id.id = "a".repeat(65);
        assert!(validate_descriptors(&[long_id]).is_err());
        let many: Vec<PluginDescriptor> = (0..17)
            .map(|index| {
                let mut item = clean.clone();
                item.id = format!("p{index}");
                item
            })
            .collect();
        assert!(validate_descriptors(&many).is_err());
    }

    #[test]
    fn resolution_reads_back_the_rendered_profile() {
        let mut profile: BTreeMap<String, String> = BTreeMap::new();
        profile.insert(
            "platform.abi.task_struct.cred".to_string(),
            "1400".to_string(),
        );
        profile.insert(
            "backend.cve_2026_43499.steps".to_string(),
            "\"mcast_rootchild\"".to_string(),
        );
        profile.insert(
            "backend.cve_2026_43499.kernel.compact_waiter".to_string(),
            "true".to_string(),
        );
        profile.insert(
            "backend.cve_2026_43499.route.select_stack.waiter_shift".to_string(),
            "-2".to_string(),
        );
        /* Present in the rendered profile but outside the declared path set:
         * R1 must not acquire an implicit vocabulary. */
        profile.insert(
            "selection.backend".to_string(),
            "\"cve_2026_43499\"".to_string(),
        );
        let paths: BTreeSet<String> = profile
            .keys()
            .filter(|key| key.as_str() != "selection.backend")
            .cloned()
            .collect();
        let symbols: BTreeMap<String, BTreeSet<u64>> = BTreeMap::new();
        let context = ExtractContext {
            profile: &profile,
            profile_paths: &paths,
            btf: None,
            symbols: &symbols,
            base: 0,
        };

        let cred = resolve_extract("platform.abi.task_struct.cred", ExtractKind::UInt, &context)
            .expect("profile path resolves");
        assert_eq!(cred.value, Some(ExtractValue::UInt(1400)));
        assert!(cred.detail.contains("R1"));
        assert_eq!(
            resolve_extract("backend.cve_2026_43499.steps", ExtractKind::Str, &context)
                .expect("str profile path resolves")
                .value,
            Some(ExtractValue::Str("mcast_rootchild".to_string()))
        );
        assert_eq!(
            resolve_extract(
                "backend.cve_2026_43499.kernel.compact_waiter",
                ExtractKind::Bool,
                &context
            )
            .expect("bool profile path resolves")
            .value,
            Some(ExtractValue::Bool(true))
        );
        assert_eq!(
            resolve_extract(
                "backend.cve_2026_43499.route.select_stack.waiter_shift",
                ExtractKind::Int,
                &context
            )
            .expect("negative int resolves")
            .value,
            Some(ExtractValue::Int(-2))
        );
        let outside = resolve_extract("selection.backend", ExtractKind::Str, &context)
            .expect("undeclared path is a miss, not an error");
        assert!(outside.value.is_none());
    }

    #[test]
    fn resolution_reads_btf_and_kallsyms() {
        let btf = image_btf();
        let base = 0xffff_8000_0000_0000u64;
        let mut symbols: BTreeMap<String, BTreeSet<u64>> = BTreeMap::new();
        symbols.insert("init_task".to_string(), BTreeSet::from([base + 0x2c4_0000]));
        let profile: BTreeMap<String, String> = BTreeMap::new();
        let paths: BTreeSet<String> = BTreeSet::new();
        let context = ExtractContext {
            profile: &profile,
            profile_paths: &paths,
            btf: Some(&btf),
            symbols: &symbols,
            base,
        };

        assert_eq!(
            resolve_extract("struct.cred.security", ExtractKind::UInt, &context)
                .expect("BTF member resolves")
                .value,
            Some(ExtractValue::UInt(12))
        );
        assert_eq!(
            resolve_extract("sizeof.cred", ExtractKind::UInt, &context)
                .expect("BTF size resolves")
                .value,
            Some(ExtractValue::UInt(16))
        );
        assert_eq!(
            resolve_extract("init_task", ExtractKind::UInt, &context)
                .expect("symbol resolves")
                .value,
            Some(ExtractValue::UInt(0x2c4_0000))
        );
        assert_eq!(
            resolve_extract("init_task", ExtractKind::Int, &context)
                .expect("symbol resolves as int")
                .value,
            Some(ExtractValue::Int(0x2c4_0000))
        );
    }

    #[test]
    fn resolution_is_named_and_type_strict() {
        let btf = image_btf();
        let mut symbols: BTreeMap<String, BTreeSet<u64>> = BTreeMap::new();
        symbols.insert("init_task".to_string(), BTreeSet::from([0x1000u64]));
        /* A symbol whose spelling collides with a struct. reference: the
         * recognized R3 prefix is terminal and must not fall through. */
        symbols.insert(
            "struct.ghost.field".to_string(),
            BTreeSet::from([0x2000u64]),
        );
        symbols.insert("high_symbol".to_string(), BTreeSet::from([0x3000u64]));
        let mut profile: BTreeMap<String, String> = BTreeMap::new();
        profile.insert(
            "backend.cve_2026_43499.kernel.compact_waiter".to_string(),
            "true".to_string(),
        );
        let mut paths: BTreeSet<String> = BTreeSet::new();
        paths.insert("backend.cve_2026_43499.kernel.compact_waiter".to_string());
        let context = ExtractContext {
            profile: &profile,
            profile_paths: &paths,
            btf: Some(&btf),
            symbols: &symbols,
            base: 0x2000,
        };

        let unknown = resolve_extract("no_such_thing", ExtractKind::UInt, &context)
            .expect("unknown name is a miss");
        assert!(unknown.value.is_none());
        assert!(unknown.detail.contains("not a declared profile path"));

        let ghost = resolve_extract("struct.ghost.field", ExtractKind::UInt, &context)
            .expect("missing BTF member is a miss");
        assert!(ghost.value.is_none());
        assert!(ghost.detail.contains("R3 BTF has no ghost.field"));

        /* Hard type mismatches. */
        assert!(
            resolve_extract(
                "backend.cve_2026_43499.kernel.compact_waiter",
                ExtractKind::UInt,
                &context
            )
            .is_err()
        );
        assert!(resolve_extract("high_symbol", ExtractKind::Bool, &context).is_err());
        assert!(resolve_extract("struct.cred.security", ExtractKind::Bool, &context).is_err());
        /* Ambiguous/missing symbol, and a symbol below the image base, are
         * misses rather than wrapped offsets. */
        let low = resolve_extract("init_task", ExtractKind::UInt, &context)
            .expect("below-base symbol is a miss");
        assert!(low.value.is_none());
        assert!(low.detail.contains("below the kernel image base"));
        /* A malformed struct. reference is a hard error, not a silent miss. */
        assert!(resolve_extract("struct.cred", ExtractKind::UInt, &context).is_err());
        assert!(resolve_extract("sizeof.", ExtractKind::UInt, &context).is_err());
    }

    #[test]
    fn required_entries_fail_closed_and_optional_ones_are_omitted() {
        let btf = image_btf();
        let mut symbols: BTreeMap<String, BTreeSet<u64>> = BTreeMap::new();
        symbols.insert("init_task".to_string(), BTreeSet::from([0x3000u64]));
        let mut profile: BTreeMap<String, String> = BTreeMap::new();
        profile.insert(
            "backend.cve_2026_43499.kernel.compact_waiter".to_string(),
            "true".to_string(),
        );
        let mut paths: BTreeSet<String> = BTreeSet::new();
        paths.insert("backend.cve_2026_43499.kernel.compact_waiter".to_string());
        let context = ExtractContext {
            profile: &profile,
            profile_paths: &paths,
            btf: Some(&btf),
            symbols: &symbols,
            base: 0x1000,
        };

        let ok = resolve_descriptor(
            &descriptor_of(&[
                (
                    "backend.cve_2026_43499.kernel.compact_waiter",
                    ExtractKind::Bool,
                    true,
                ),
                ("init_task", ExtractKind::UInt, true),
                ("nope", ExtractKind::UInt, false),
            ]),
            &context,
        )
        .expect("two resolved, one optional miss");
        assert_eq!(ok.entries.len(), 2);
        assert_eq!(ok.skipped.len(), 1);
        assert_eq!(ok.skipped[0].0, "nope");
        assert!(ok.skipped[0].1.contains("not a declared profile path"));

        /* Required + underivable: fatal, and the descriptor default is never
         * substituted (writing 0 would disguise "not derived" as an offset). */
        let err = resolve_descriptor(
            &descriptor_of(&[("nope", ExtractKind::UInt, true)]),
            &context,
        )
        .expect_err("required miss must fail closed");
        assert!(err.to_string().contains("required but not derivable"));
        assert!(!err.to_string().contains("= 0"));
    }
}
