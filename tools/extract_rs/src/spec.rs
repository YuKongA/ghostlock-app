//! Extractor-side `spec` rows: explicit extraction methods + AArch64
//! disassembly (design: docs/analysis/plugin-extract-disasm-design.md; frozen
//! columns: docs/analysis/plugin-extract-spec-design.md §10/§11).
//!
//! Two hard rules from the frozen design:
//!   * an explicit `methods` list is the ONLY ladder walked (a full miss never
//!     falls back to the implicit profile -> btf -> kallsyms order), while
//!     `methods=-` means the frozen default ladder profile -> btf -> kallsyms
//!     -> disasm;
//!   * instruction matching happens on DECODED instructions (yaxpeax-arm). The
//!     rendered text is consulted for ONE thing only: resolving the mnemonic
//!     alias (`cmp` renders as an alias of `subs`). Operands are never
//!     matched as text.

use yaxpeax_arch::{Decoder, U8Reader};
use yaxpeax_arm::armv8::a64::{InstDecoder, Instruction, Operand, SizeCode};

use crate::derive::{RelSymbols, unique_offset};
use crate::disasm::MAX_DISASM_RANGE;
use crate::error::{ExtractError, Result};
use crate::plugin::{
    ExtractContext, ExtractKind, ExtractValue, check_name, resolve_btf, resolve_kallsyms,
    resolve_profile_path, text,
};

/// Frozen `spec` row width (native §10.1).
pub const SPEC_COLUMNS: usize = 16;
/// Frozen cap on the number of spec rows a module may declare (native §7).
pub const MAX_SPECS: usize = 32;
/// Default scan window when `max_scan` is `-` (native §11.6).
pub const DEFAULT_MAX_SCAN: usize = 0x2000;
/// Hard cap on `hit` (native §10.1).
pub const MAX_HIT: u32 = 64;

fn fail(reason: impl std::fmt::Display) -> ExtractError {
    ExtractError::new(format!("plugin spec: {reason}"))
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Method {
    Profile,
    Btf,
    Kallsyms,
    Disasm,
}

impl Method {
    pub fn token(self) -> &'static str {
        match self {
            Method::Profile => "profile",
            Method::Btf => "btf",
            Method::Kallsyms => "kallsyms",
            Method::Disasm => "disasm",
        }
    }

    fn parse(token: &str) -> Option<Method> {
        match token {
            "profile" => Some(Method::Profile),
            "btf" => Some(Method::Btf),
            "kallsyms" => Some(Method::Kallsyms),
            "disasm" => Some(Method::Disasm),
            _ => None,
        }
    }
}

/// Frozen default ladder for `methods=-` (native §9.2).
pub fn default_ladder() -> Vec<Method> {
    vec![
        Method::Profile,
        Method::Btf,
        Method::Kallsyms,
        Method::Disasm,
    ]
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Scope {
    Anchor,
    Text,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Base {
    Image,
    Anchor,
    Raw,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Anchor {
    Symbol(String),
    ProfilePath(String),
    Pc(u64),
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CaptureKind {
    Pc,
    Imm,
    Disp,
    Reg,
    Pcoff,
}

impl CaptureKind {
    pub fn token(self) -> &'static str {
        match self {
            CaptureKind::Pc => "pc",
            CaptureKind::Imm => "imm",
            CaptureKind::Disp => "disp",
            CaptureKind::Reg => "reg",
            CaptureKind::Pcoff => "pcoff",
        }
    }
}

/// `[<insn_index>.]<kind>[:<operand_index>]`; the operand index is the index
/// into the PATTERN predicate list (0-based), not the decoded operand array.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Capture {
    pub insn_index: Option<usize>,
    pub kind: CaptureKind,
    pub operand_index: Option<usize>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum RegPred {
    AnyX,
    AnyW,
    Named(String),
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ImmPred {
    Any,
    Value(u64),
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct MemPred {
    pub base: RegPred,
    pub disp: Option<ImmPred>,
    pub writeback: bool,
    pub post: bool,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Pred {
    Any,
    Reg(RegPred),
    Imm(ImmPred),
    Mem(MemPred),
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct InsnPred {
    pub mnemonic: String,
    pub operands: Vec<Pred>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Pattern {
    Bytes { bytes: Vec<u8>, mask: Vec<bool> },
    Insn(Vec<InsnPred>),
}

/// One parsed `spec` row.
#[derive(Debug, Clone, PartialEq)]
pub struct ExtractSpec {
    pub name: String,
    pub kind: ExtractKind,
    pub required: bool,
    /// `None` = `methods=-` = the frozen default ladder.
    pub methods: Option<Vec<Method>>,
    pub anchor: Option<Anchor>,
    pub scope: Scope,
    pub pattern: Option<Pattern>,
    pub hit: u32,
    pub capture: Capture,
    /// `None` = `auto` (raw decoded value; mutually exclusive with signed).
    pub width: Option<u8>,
    pub signed: bool,
    pub base: Base,
    pub max_scan: usize,
    pub doc: String,
}

/// Parses the 16 frozen columns of a `spec` row, fail-closed on any
/// inconsistency (the probe guarantees well-formed rows; silently tolerating a
/// malformed one would hide a producer bug).
pub fn parse_spec_row(parts: &[&str], line: &str) -> Result<ExtractSpec> {
    if parts.len() != SPEC_COLUMNS {
        return Err(fail(format!(
            "spec row needs {SPEC_COLUMNS} tab-separated columns: {line}"
        )));
    }
    let name = parts[2];
    check_name("spec name", name, line)?;
    let kind =
        ExtractKind::parse(parts[3]).ok_or_else(|| fail(format!("unknown spec type: {line}")))?;
    let required = match parts[4] {
        "0" => false,
        "1" => true,
        _ => return Err(fail(format!("spec required must be 0 or 1: {line}"))),
    };
    let methods = parse_methods(parts[5], line)?;
    let anchor = parse_anchor(parts[6], line)?;
    let scope = parse_scope(parts[7], line)?;
    let pattern = parse_pattern(parts[8], line)?;
    let hit = parse_hit(parts[9], line)?;
    let capture = parse_capture(parts[10], line)?;
    let width = parse_width(parts[11], line)?;
    let signed = parse_signed(parts[12], line)?;
    let base = parse_base(parts[13], line)?;
    let max_scan = parse_max_scan(parts[14], line)?;
    let spec = ExtractSpec {
        name: name.to_string(),
        kind,
        required,
        methods,
        anchor,
        scope,
        pattern,
        hit,
        capture,
        width,
        signed,
        base,
        max_scan,
        doc: text(parts[15]),
    };
    validate_spec(&spec, line)?;
    Ok(spec)
}

fn parse_methods(value: &str, line: &str) -> Result<Option<Vec<Method>>> {
    if value == "-" {
        return Ok(None);
    }
    let mut out = Vec::new();
    for token in value.split(',') {
        let token = token.trim();
        let method =
            Method::parse(token).ok_or_else(|| fail(format!("unknown method {token}: {line}")))?;
        if out.contains(&method) {
            return Err(fail(format!("duplicate method {token}: {line}")));
        }
        out.push(method);
    }
    if out.is_empty() {
        return Err(fail(format!("empty method list: {line}")));
    }
    Ok(Some(out))
}

fn parse_anchor(value: &str, line: &str) -> Result<Option<Anchor>> {
    if value == "-" {
        return Ok(None);
    }
    if let Some(name) = value.strip_prefix("sym:") {
        if name.is_empty() {
            return Err(fail(format!("empty anchor symbol: {line}")));
        }
        return Ok(Some(Anchor::Symbol(name.to_string())));
    }
    if let Some(path) = value.strip_prefix("path:") {
        if path.is_empty() {
            return Err(fail(format!("empty anchor path: {line}")));
        }
        return Ok(Some(Anchor::ProfilePath(path.to_string())));
    }
    if let Some(hex) = value.strip_prefix("pc:") {
        let offset = u64::from_str_radix(hex, 16)
            .map_err(|_| fail(format!("anchor pc is not hex: {line}")))?;
        return Ok(Some(Anchor::Pc(offset)));
    }
    Err(fail(format!("anchor must be sym:/path:/pc: or -: {line}")))
}

fn parse_scope(value: &str, line: &str) -> Result<Scope> {
    match value {
        "-" | "anchor" => Ok(Scope::Anchor),
        "text" => Ok(Scope::Text),
        _ => Err(fail(format!("unknown scope: {line}"))),
    }
}

fn parse_hit(value: &str, line: &str) -> Result<u32> {
    if value == "-" {
        return Ok(1);
    }
    let hit: u32 = value
        .parse()
        .map_err(|_| fail(format!("hit is not a decimal number: {line}")))?;
    if hit < 1 || hit > MAX_HIT {
        return Err(fail(format!("hit must be 1..={MAX_HIT}: {line}")));
    }
    Ok(hit)
}

fn parse_width(value: &str, line: &str) -> Result<Option<u8>> {
    match value {
        "-" | "auto" => Ok(None),
        "1" => Ok(Some(1)),
        "2" => Ok(Some(2)),
        "4" => Ok(Some(4)),
        "8" => Ok(Some(8)),
        _ => Err(fail(format!("width must be 1|2|4|8|auto: {line}"))),
    }
}

fn parse_signed(value: &str, line: &str) -> Result<bool> {
    match value {
        "-" | "0" => Ok(false),
        "1" => Ok(true),
        _ => Err(fail(format!("signed must be 0|1: {line}"))),
    }
}

fn parse_base(value: &str, line: &str) -> Result<Base> {
    match value {
        "-" | "image" => Ok(Base::Image),
        "anchor" => Ok(Base::Anchor),
        "raw" => Ok(Base::Raw),
        _ => Err(fail(format!("base must be image|anchor|raw: {line}"))),
    }
}

fn parse_max_scan(value: &str, line: &str) -> Result<usize> {
    if value == "-" {
        return Ok(DEFAULT_MAX_SCAN);
    }
    let scan = match value
        .strip_prefix("0x")
        .or_else(|| value.strip_prefix("0X"))
    {
        Some(hex) => usize::from_str_radix(hex, 16),
        None => value.parse::<usize>(),
    }
    .map_err(|_| fail(format!("max_scan is not a number: {line}")))?;
    if scan < 1 || scan > MAX_DISASM_RANGE {
        return Err(fail(format!(
            "max_scan must be 1..={MAX_DISASM_RANGE:#x}: {line}"
        )));
    }
    Ok(scan)
}

fn parse_capture(value: &str, line: &str) -> Result<Capture> {
    if value == "-" {
        return Ok(Capture {
            insn_index: None,
            kind: CaptureKind::Pc,
            operand_index: None,
        });
    }
    let (head, operand) = match value.split_once(':') {
        Some((head, operand)) => (head, Some(operand)),
        None => (value, None),
    };
    let (insn_index, kind_token) = match head.split_once('.') {
        Some((index, kind)) => {
            let index: usize = index
                .parse()
                .map_err(|_| fail(format!("capture insn_index is not a number: {line}")))?;
            if index == 0 {
                return Err(fail(format!("capture insn_index is 1-based: {line}")));
            }
            (Some(index), kind)
        }
        None => (None, head),
    };
    let kind = match kind_token {
        "pc" => CaptureKind::Pc,
        "imm" => CaptureKind::Imm,
        "disp" => CaptureKind::Disp,
        "reg" => CaptureKind::Reg,
        "pcoff" => CaptureKind::Pcoff,
        _ => return Err(fail(format!("unknown capture kind: {line}"))),
    };
    let operand_index = match (kind, operand) {
        (CaptureKind::Pc, None) => None,
        (CaptureKind::Pc, Some(_)) => {
            return Err(fail(format!("pc capture takes no operand index: {line}")));
        }
        (_, Some(index)) => Some(
            index
                .parse()
                .map_err(|_| fail(format!("capture operand_index is not a number: {line}")))?,
        ),
        (_, None) => {
            return Err(fail(format!(
                "{} capture needs an operand index: {line}",
                kind.token()
            )));
        }
    };
    Ok(Capture {
        insn_index,
        kind,
        operand_index,
    })
}

fn parse_bytes(hex: &str, line: &str) -> Result<(Vec<u8>, Vec<bool>)> {
    let chars: Vec<char> = hex.chars().collect();
    if chars.is_empty() || chars.len() % 4 != 0 {
        return Err(fail(format!(
            "bytes pattern needs a hex string of length 4n: {line}"
        )));
    }
    let mut bytes = Vec::with_capacity(chars.len() / 2);
    let mut mask = Vec::with_capacity(chars.len() / 2);
    let mut index = 0;
    while index < chars.len() {
        let (high, low) = (chars[index], chars[index + 1]);
        if high == '?' && low == '?' {
            bytes.push(0);
            mask.push(false);
        } else {
            let high = high
                .to_digit(16)
                .ok_or_else(|| fail(format!("bytes pattern is not hex: {line}")))?;
            let low = low
                .to_digit(16)
                .ok_or_else(|| fail(format!("bytes pattern is not hex: {line}")))?;
            bytes.push((high * 16 + low) as u8);
            mask.push(true);
        }
        index += 2;
    }
    Ok((bytes, mask))
}

fn parse_pattern(value: &str, line: &str) -> Result<Option<Pattern>> {
    if value == "-" {
        return Ok(None);
    }
    if let Some(hex) = value.strip_prefix("bytes:") {
        let (bytes, mask) = parse_bytes(hex, line)?;
        return Ok(Some(Pattern::Bytes { bytes, mask }));
    }
    if let Some(rest) = value.strip_prefix("insn:") {
        let parts: Vec<&str> = rest.split(';').map(str::trim).collect();
        if parts.is_empty() || parts.iter().any(|part| part.is_empty()) {
            return Err(fail(format!("insn pattern is empty: {line}")));
        }
        if parts.len() > 3 {
            return Err(fail(format!(
                "insn pattern has more than 3 instructions: {line}"
            )));
        }
        let mut insns = Vec::with_capacity(parts.len());
        for part in parts {
            insns.push(parse_insn_pred(part, line)?);
        }
        return Ok(Some(Pattern::Insn(insns)));
    }
    Err(fail(format!(
        "pattern must start with bytes: or insn:: {line}"
    )))
}

fn parse_insn_pred(value: &str, line: &str) -> Result<InsnPred> {
    let value = value.trim();
    if value.is_empty() {
        return Err(fail(format!("empty instruction predicate: {line}")));
    }
    let (mnemonic, rest) = match value.find(char::is_whitespace) {
        Some(index) => (&value[..index], value[index..].trim()),
        None => (value, ""),
    };
    let mnemonic = mnemonic.to_ascii_lowercase();
    if mnemonic.is_empty()
        || !mnemonic
            .chars()
            .all(|ch| ch.is_ascii_alphanumeric() || ch == '.')
    {
        return Err(fail(format!("invalid mnemonic {mnemonic}: {line}")));
    }
    let operands = if rest.is_empty() {
        Vec::new()
    } else {
        parse_operands(rest, line)?
    };
    Ok(InsnPred { mnemonic, operands })
}

/// Splits on a separator that is not inside square brackets.
fn split_top_level(value: &str, separator: char) -> Result<Vec<String>> {
    let mut out = Vec::new();
    let mut current = String::new();
    let mut depth = 0usize;
    for ch in value.chars() {
        match ch {
            '[' => {
                depth += 1;
                current.push(ch);
            }
            ']' => {
                if depth == 0 {
                    return Err(fail("unbalanced memory operand"));
                }
                depth -= 1;
                current.push(ch);
            }
            _ if ch == separator && depth == 0 => {
                out.push(current.trim().to_string());
                current = String::new();
            }
            _ => current.push(ch),
        }
    }
    if depth != 0 {
        return Err(fail("unbalanced memory operand"));
    }
    out.push(current.trim().to_string());
    Ok(out)
}

fn parse_operands(value: &str, line: &str) -> Result<Vec<Pred>> {
    let tokens = split_top_level(value, ',').map_err(|err| fail(format!("{err}: {line}")))?;
    let mut out: Vec<Pred> = Vec::new();
    let mut index = 0usize;
    while index < tokens.len() {
        let token = tokens[index].trim();
        if token.is_empty() {
            return Err(fail(format!("empty operand predicate: {line}")));
        }
        if token.starts_with('[') {
            let mut inner = token.to_string();
            let mut writeback = false;
            if let Some(stripped) = inner.strip_suffix('!') {
                writeback = true;
                inner = stripped.to_string();
            }
            let body = inner
                .strip_prefix('[')
                .and_then(|rest| rest.strip_suffix(']'))
                .ok_or_else(|| fail(format!("malformed memory operand {token}: {line}")))?;
            let parts = split_top_level(body, ',').map_err(|err| fail(format!("{err}: {line}")))?;
            if parts.is_empty() || parts.len() > 2 {
                return Err(fail(format!("malformed memory operand {token}: {line}")));
            }
            let base = parse_reg_pred(parts[0].trim(), line)?;
            let disp = if parts.len() == 2 {
                Some(parse_imm_pred(parts[1].trim(), line)?)
            } else {
                None
            };
            let post_operand = !writeback
                && index + 2 == tokens.len()
                && tokens[index + 1].trim().starts_with('#');
            if post_operand {
                let post = parse_imm_pred(tokens[index + 1].trim(), line)?;
                out.push(Pred::Mem(MemPred {
                    base,
                    disp: Some(post),
                    writeback: false,
                    post: true,
                }));
                index += 2;
                continue;
            }
            out.push(Pred::Mem(MemPred {
                base,
                disp,
                writeback,
                post: false,
            }));
            index += 1;
            continue;
        }
        if token == "?" {
            out.push(Pred::Any);
        } else if token.starts_with('#') {
            out.push(Pred::Imm(parse_imm_pred(token, line)?));
        } else {
            out.push(Pred::Reg(parse_reg_pred(token, line)?));
        }
        index += 1;
    }
    Ok(out)
}

fn parse_reg_pred(token: &str, line: &str) -> Result<RegPred> {
    match token {
        "x?" => return Ok(RegPred::AnyX),
        "w?" => return Ok(RegPred::AnyW),
        "sp" | "wsp" | "xzr" | "wzr" => return Ok(RegPred::Named(token.to_string())),
        _ => {}
    }
    let mut chars = token.chars();
    let Some(prefix) = chars.next() else {
        return Err(fail(format!("empty register predicate: {line}")));
    };
    let digits: String = chars.collect();
    if (prefix == 'x' || prefix == 'w')
        && !digits.is_empty()
        && digits.chars().all(|ch| ch.is_ascii_digit())
    {
        let number: u16 = digits
            .parse()
            .map_err(|_| fail(format!("invalid register {token}: {line}")))?;
        if number > 30 {
            return Err(fail(format!(
                "register {token} must be written sp/xzr/wsp/wzr: {line}"
            )));
        }
        return Ok(RegPred::Named(token.to_string()));
    }
    Err(fail(format!("invalid register {token}: {line}")))
}

fn parse_imm_pred(token: &str, line: &str) -> Result<ImmPred> {
    let body = token
        .strip_prefix('#')
        .ok_or_else(|| fail(format!("immediate must start with #: {line}")))?;
    if body == "?" {
        return Ok(ImmPred::Any);
    }
    let value = match body.strip_prefix("0x").or_else(|| body.strip_prefix("0X")) {
        Some(hex) => u64::from_str_radix(hex, 16),
        None => body.parse::<u64>(),
    }
    .map_err(|_| fail(format!("immediate {token} is not a number: {line}")))?;
    Ok(ImmPred::Value(value))
}

fn validate_spec(spec: &ExtractSpec, line: &str) -> Result<()> {
    let ladder = spec.methods.clone().unwrap_or_else(default_ladder);
    let has = |method: Method| ladder.contains(&method);
    if has(Method::Disasm) && spec.anchor.is_none() {
        return Err(fail(format!("disasm needs an anchor: {line}")));
    }
    if spec.signed && spec.width.is_none() {
        return Err(fail(format!(
            "signed needs an explicit width (auto is unsigned): {line}"
        )));
    }
    match spec.kind {
        ExtractKind::Int if !spec.signed => {
            return Err(fail(format!("int capture needs signed=1: {line}")));
        }
        ExtractKind::UInt if spec.signed => {
            return Err(fail(format!("uint capture needs signed=0: {line}")));
        }
        ExtractKind::Bool | ExtractKind::Str if spec.signed => {
            return Err(fail(format!(
                "{} capture cannot be signed: {line}",
                spec.kind.token()
            )));
        }
        _ => {}
    }
    if spec.kind == ExtractKind::Str && spec.width.is_some() {
        return Err(fail(format!("str capture has no width: {line}")));
    }
    if spec.scope == Scope::Text && (has(Method::Btf) || has(Method::Kallsyms)) {
        return Err(fail(format!(
            "scope=text contradicts btf/kallsyms methods: {line}"
        )));
    }
    if spec.base == Base::Anchor && spec.anchor.is_none() {
        return Err(fail(format!("base=anchor needs an anchor: {line}")));
    }
    match &spec.pattern {
        None => {
            if spec.capture.kind != CaptureKind::Pc {
                return Err(fail(format!(
                    "capture {} needs a pattern: {line}",
                    spec.capture.kind.token()
                )));
            }
        }
        Some(Pattern::Bytes { .. }) => {
            if spec.capture.kind != CaptureKind::Pc {
                return Err(fail(format!("a bytes pattern can only capture pc: {line}")));
            }
            if spec.capture.insn_index.is_some() {
                return Err(fail(format!(
                    "a bytes pattern has no instruction index: {line}"
                )));
            }
        }
        Some(Pattern::Insn(insns)) => {
            let selected = spec.capture.insn_index.unwrap_or(insns.len());
            if selected == 0 || selected > insns.len() {
                return Err(fail(format!(
                    "capture insn_index {selected} is outside the pattern: {line}"
                )));
            }
            if spec.capture.kind == CaptureKind::Pc {
                return Ok(());
            }
            let operand_index = spec.capture.operand_index.ok_or_else(|| {
                fail(format!(
                    "capture {} needs an operand index: {line}",
                    spec.capture.kind.token()
                ))
            })?;
            let pred = insns[selected - 1]
                .operands
                .get(operand_index)
                .ok_or_else(|| {
                    fail(format!(
                        "capture operand_index {operand_index} is outside instruction {selected}: {line}"
                    ))
                })?;
            if spec.capture.kind == CaptureKind::Disp {
                let is_disp = matches!(pred, Pred::Mem(mem) if mem.disp.is_some());
                if !is_disp {
                    return Err(fail(format!(
                        "disp capture needs a memory predicate with a displacement: {line}"
                    )));
                }
            }
        }
    }
    Ok(())
}

/// Everything the disassembly engine may consult. The profile/btf/kallsyms
/// methods reuse the P2 context unchanged.
pub struct SpecContext<'a> {
    pub extract: &'a ExtractContext<'a>,
    pub kernel: &'a [u8],
    pub rel_symbols: &'a RelSymbols,
    pub sorted_offsets: &'a [u64],
}

/// Result of resolving one spec: the value (when produced), the method that
/// actually produced it, its evidence, or a named reason when nothing did.
#[derive(Debug, Clone)]
pub struct SpecResolution {
    pub value: Option<ExtractValue>,
    pub method: Option<Method>,
    pub evidence: String,
    pub reason: Option<String>,
}

pub fn resolve_spec(spec: &ExtractSpec, context: &SpecContext<'_>) -> Result<SpecResolution> {
    let ladder = spec.methods.clone().unwrap_or_else(default_ladder);
    let mut reasons: Vec<String> = Vec::new();
    for method in &ladder {
        match method {
            Method::Profile => {
                let resolution = resolve_profile_path(&spec.name, spec.kind, context.extract)?;
                if let Some(value) = resolution.value {
                    return Ok(hit(Method::Profile, resolution.detail, value));
                }
                reasons.push(format!("profile: {}", resolution.detail));
            }
            Method::Btf => {
                let resolution = resolve_btf(&spec.name, spec.kind, context.extract)?;
                if let Some(value) = resolution.value {
                    return Ok(hit(Method::Btf, resolution.detail, value));
                }
                reasons.push(format!("btf: {}", resolution.detail));
            }
            Method::Kallsyms => {
                let resolution = resolve_kallsyms(&spec.name, spec.kind, context.extract)?;
                if let Some(value) = resolution.value {
                    return Ok(hit(Method::Kallsyms, resolution.detail, value));
                }
                reasons.push(format!("kallsyms: {}", resolution.detail));
            }
            Method::Disasm => {
                let outcome = resolve_disasm(spec, context)?;
                if let Some(captured) = outcome.value {
                    match captured_to_value(spec, captured, context)? {
                        Some((value, detail)) => {
                            return Ok(hit(
                                Method::Disasm,
                                format!("{} {detail}", outcome.evidence),
                                value,
                            ));
                        }
                        None => reasons.push(format!(
                            "disasm: {} (capture cannot produce {})",
                            outcome.evidence,
                            spec.kind.token()
                        )),
                    }
                } else {
                    reasons.push(format!(
                        "disasm: {}",
                        outcome.reason.unwrap_or_else(|| "no value".to_string())
                    ));
                }
            }
        }
    }
    Ok(SpecResolution {
        value: None,
        method: None,
        evidence: String::new(),
        reason: Some(reasons.join("; ")),
    })
}

fn hit(method: Method, detail: String, value: ExtractValue) -> SpecResolution {
    SpecResolution {
        value: Some(value),
        method: Some(method),
        evidence: format!("method={} {detail}", method.token()),
        reason: None,
    }
}

/// A captured number plus whether it denotes an address (so `base` applies).
#[derive(Debug, Clone, Copy)]
struct Captured {
    raw: u64,
    address_valued: bool,
}

struct DisasmOutcome {
    value: Option<Captured>,
    evidence: String,
    reason: Option<String>,
}

fn captured_to_value(
    spec: &ExtractSpec,
    captured: Captured,
    context: &SpecContext<'_>,
) -> Result<Option<(ExtractValue, String)>> {
    if spec.kind == ExtractKind::Str {
        return Ok(None);
    }
    let mut value = captured.raw;
    if captured.address_valued {
        value = match spec.base {
            Base::Image => value,
            Base::Raw => value.wrapping_add(context.extract.base),
            Base::Anchor => match anchor_address(spec, context)? {
                Some(anchor) => value.wrapping_sub(anchor),
                None => return Ok(None),
            },
        };
    }
    if let Some(width) = spec.width {
        let bits = u32::from(width) * 8;
        if bits < 64 {
            let mask = (1u64 << bits) - 1;
            value &= mask;
            if spec.signed {
                value = sign_extend(value, bits);
            }
        }
    }
    let converted = match spec.kind {
        ExtractKind::UInt => ExtractValue::UInt(value),
        ExtractKind::Int => ExtractValue::Int(value as i64),
        ExtractKind::Bool => match value {
            0 => ExtractValue::Bool(false),
            1 => ExtractValue::Bool(true),
            _ => return Ok(None),
        },
        ExtractKind::Str => return Ok(None),
    };
    let detail = format!(
        "capture={} raw={} width={} signed={} base={}",
        spec.capture.kind.token(),
        captured.raw,
        spec.width
            .map_or_else(|| "auto".to_string(), |w| w.to_string()),
        u8::from(spec.signed),
        match spec.base {
            Base::Image => "image",
            Base::Anchor => "anchor",
            Base::Raw => "raw",
        }
    );
    Ok(Some((converted, detail)))
}

fn sign_extend(value: u64, bits: u32) -> u64 {
    let shift = 64 - bits;
    (((value << shift) as i64) >> shift) as u64
}

/// The anchor as an image offset (None when there is no anchor).
fn anchor_address(spec: &ExtractSpec, context: &SpecContext<'_>) -> Result<Option<u64>> {
    match &spec.anchor {
        None => Ok(None),
        Some(Anchor::Symbol(name)) => Ok(unique_offset(context.rel_symbols, name).ok()),
        Some(Anchor::ProfilePath(path)) => Ok(context
            .extract
            .profile
            .get(path)
            .filter(|literal| literal.as_str() != "null")
            .and_then(|literal| literal.parse::<u64>().ok())),
        Some(Anchor::Pc(offset)) => Ok(Some(*offset)),
    }
}

struct DecodedInsn {
    addr: usize,
    instr: Instruction,
}

fn decode_window(kernel: &[u8], start: usize, stop: usize) -> (Vec<DecodedInsn>, usize) {
    let decoder = InstDecoder::default();
    let mut decoded = Vec::new();
    let mut undecoded = 0usize;
    let mut addr = start;
    while addr + 4 <= stop {
        let mut reader = U8Reader::new(&kernel[addr..addr + 4]);
        match decoder.decode(&mut reader) {
            Ok(instr) => decoded.push(DecodedInsn { addr, instr }),
            Err(_) => undecoded += 1,
        }
        addr += 4;
    }
    (decoded, undecoded)
}

fn resolve_disasm(spec: &ExtractSpec, context: &SpecContext<'_>) -> Result<DisasmOutcome> {
    let anchor = match &spec.anchor {
        None => {
            if spec.scope == Scope::Anchor {
                return Ok(miss("disasm needs an anchor when scope=anchor"));
            }
            None
        }
        Some(Anchor::Symbol(name)) => match unique_offset(context.rel_symbols, name) {
            Ok(offset) => Some(offset),
            Err(err) => {
                return Ok(miss(format!("anchor sym:{name} unresolved: {err}")));
            }
        },
        Some(Anchor::ProfilePath(path)) => match context
            .extract
            .profile
            .get(path)
            .filter(|literal| literal.as_str() != "null")
            .and_then(|literal| literal.parse::<u64>().ok())
        {
            Some(value) => Some(value),
            None => {
                return Ok(miss(format!("anchor path:{path} unresolved")));
            }
        },
        Some(Anchor::Pc(offset)) => Some(*offset),
    };

    let (start, stop) = match spec.scope {
        Scope::Text => (0usize, context.kernel.len().min(spec.max_scan)),
        Scope::Anchor => {
            let start = usize::try_from(anchor.unwrap_or(0))
                .map_err(|_| fail("anchor does not fit the host address space"))?;
            if start >= context.kernel.len() {
                return Ok(miss("anchor is outside the kernel image"));
            }
            let next = context
                .sorted_offsets
                .iter()
                .find(|offset| **offset as usize > start)
                .map(|offset| *offset as usize);
            let stop = (start + spec.max_scan)
                .min(next.unwrap_or(usize::MAX))
                .min(context.kernel.len());
            (start, stop)
        }
    };
    if stop <= start {
        return Ok(miss("empty scan window"));
    }

    let (decoded, undecoded) = decode_window(context.kernel, start, stop);
    let mut hits: Vec<(usize, Captured)> = Vec::new();
    match &spec.pattern {
        None => {
            if spec.capture.kind == CaptureKind::Pc {
                for insn in &decoded {
                    hits.push((
                        insn.addr,
                        Captured {
                            raw: insn.addr as u64,
                            address_valued: true,
                        },
                    ));
                }
            }
        }
        Some(Pattern::Bytes { bytes, mask }) => {
            for insn in &decoded {
                let addr = insn.addr;
                if addr + bytes.len() > context.kernel.len() {
                    continue;
                }
                let matched =
                    bytes
                        .iter()
                        .zip(mask.iter())
                        .enumerate()
                        .all(|(index, (byte, expected))| {
                            !*expected || context.kernel[addr + index] == *byte
                        });
                if matched {
                    hits.push((
                        addr,
                        Captured {
                            raw: addr as u64,
                            address_valued: true,
                        },
                    ));
                }
            }
        }
        Some(Pattern::Insn(preds)) => {
            for window_start in 0..decoded.len() {
                if window_start + preds.len() > decoded.len() {
                    break;
                }
                let mut matches = Vec::with_capacity(preds.len());
                let mut ok = true;
                for (offset, pred) in preds.iter().enumerate() {
                    match match_insn(pred, &decoded[window_start + offset].instr) {
                        Some(found) => matches.push(found),
                        None => {
                            ok = false;
                            break;
                        }
                    }
                }
                if !ok {
                    continue;
                }
                let selected = spec.capture.insn_index.unwrap_or(preds.len()) - 1;
                let insn = &decoded[window_start + selected];
                if let Some(captured) = capture_value(spec, insn, &matches[selected]) {
                    hits.push((insn.addr, captured));
                }
            }
        }
    }

    let evidence = format!(
        "anchor={} scope={} pattern={} hit={}/{} undecoded={}",
        anchor_label(&spec.anchor),
        match spec.scope {
            Scope::Anchor => "anchor",
            Scope::Text => "text",
        },
        pattern_label(&spec.pattern),
        spec.hit,
        hits.len(),
        undecoded
    );
    if hits.len() < spec.hit as usize {
        return Ok(DisasmOutcome {
            value: None,
            evidence,
            reason: Some(format!("hits={} < hit={}", hits.len(), spec.hit)),
        });
    }
    let (pc, captured) = hits[spec.hit as usize - 1];
    Ok(DisasmOutcome {
        value: Some(captured),
        evidence: format!("{evidence} pc=0x{pc:x}"),
        reason: None,
    })
}

fn miss(reason: impl std::fmt::Display) -> DisasmOutcome {
    DisasmOutcome {
        value: None,
        evidence: String::new(),
        reason: Some(reason.to_string()),
    }
}

fn anchor_label(anchor: &Option<Anchor>) -> String {
    match anchor {
        None => "-".to_string(),
        Some(Anchor::Symbol(name)) => format!("sym:{name}"),
        Some(Anchor::ProfilePath(path)) => format!("path:{path}"),
        Some(Anchor::Pc(offset)) => format!("pc:0x{offset:x}"),
    }
}

fn pattern_label(pattern: &Option<Pattern>) -> String {
    match pattern {
        None => "-".to_string(),
        Some(Pattern::Bytes { bytes, mask }) => {
            let mut out = String::from("bytes:");
            for (byte, expected) in bytes.iter().zip(mask.iter()) {
                if *expected {
                    out.push_str(&format!("{byte:02x}"));
                } else {
                    out.push_str("??");
                }
            }
            out
        }
        Some(Pattern::Insn(insns)) => {
            let parts: Vec<String> = insns
                .iter()
                .map(|insn| format!("{} ({} operands)", insn.mnemonic, insn.operands.len()))
                .collect();
            format!("insn:{}", parts.join("; "))
        }
    }
}

/// One predicate-to-operand match of one instruction.
struct InsnMatch {
    operand_indices: Vec<Option<usize>>,
    mem_disp: Vec<bool>,
}

fn match_insn(pred: &InsnPred, instr: &Instruction) -> Option<InsnMatch> {
    let mnemonic_matches = {
        let opcode = instr.opcode.to_string();
        let rendered = format!("{instr}");
        let alias = rendered.split_whitespace().next().unwrap_or("").to_string();
        pred.mnemonic == opcode || pred.mnemonic == alias
    };
    if !mnemonic_matches {
        return None;
    }
    let operands: Vec<&Operand> = instr
        .operands
        .iter()
        .take_while(|operand| !matches!(operand, Operand::Nothing))
        .collect();
    let count = pred.operands.len();
    if count == 0 {
        return Some(InsnMatch {
            operand_indices: Vec::new(),
            mem_disp: Vec::new(),
        });
    }
    let skip = if operands.len() == count {
        0
    } else if operands.len() == count + 1 && is_zero_register(operands[0]) {
        /* Rendered aliases (cmp/cmn/tst) carry an implicit zero destination. */
        1
    } else {
        return None;
    };
    let mut operand_indices = vec![None; count];
    let mut mem_disp = vec![false; count];
    let mut cursor = skip;
    for (index, predicate) in pred.operands.iter().enumerate() {
        match predicate {
            Pred::Any => {
                operands.get(cursor)?;
                operand_indices[index] = Some(cursor);
                cursor += 1;
            }
            Pred::Reg(reg) => {
                let operand = operands.get(cursor)?;
                if !reg_pred_matches(reg, operand) {
                    return None;
                }
                operand_indices[index] = Some(cursor);
                cursor += 1;
            }
            Pred::Imm(imm) => {
                let operand = operands.get(cursor)?;
                match imm {
                    /* #? also accepts a PC-relative target (bl/b/adr/adrp),
                     * which is the only operand those instructions carry. */
                    ImmPred::Any => {
                        if imm_like_value(operand).is_none()
                            && !matches!(operand, Operand::PCOffset(_))
                        {
                            return None;
                        }
                    }
                    ImmPred::Value(expected) => {
                        let value = imm_like_value(operand)?;
                        if value != *expected {
                            return None;
                        }
                    }
                }
                operand_indices[index] = Some(cursor);
                cursor += 1;
            }
            Pred::Mem(mem) => {
                let operand = operands.get(cursor)?;
                let (reg, disp) = match operand {
                    Operand::RegPreIndex(reg, disp, writeback) => {
                        if mem.post || *writeback != mem.writeback {
                            return None;
                        }
                        (*reg, Some(*disp))
                    }
                    Operand::RegPostIndex(reg, disp) => {
                        if !mem.post {
                            return None;
                        }
                        (*reg, Some(*disp))
                    }
                    _ => return None,
                };
                if !reg_number_matches(&mem.base, reg) {
                    return None;
                }
                if let Some(disp_pred) = &mem.disp {
                    let value = disp? as i64 as u64;
                    if let ImmPred::Value(expected) = disp_pred {
                        if value != *expected {
                            return None;
                        }
                    }
                    mem_disp[index] = true;
                }
                operand_indices[index] = Some(cursor);
                cursor += 1;
            }
        }
    }
    if cursor != operands.len() {
        return None;
    }
    Some(InsnMatch {
        operand_indices,
        mem_disp,
    })
}

fn is_zero_register(operand: &Operand) -> bool {
    matches!(operand, Operand::Register(_, 31))
}

fn size_code_matches(expected: &RegPred, size: SizeCode, number: u16) -> bool {
    match expected {
        RegPred::AnyX => size == SizeCode::X,
        RegPred::AnyW => size == SizeCode::W,
        RegPred::Named(name) => {
            let (prefix, rest) = name.split_at(1);
            match (prefix, rest) {
                ("x", "zr") | ("w", "zr") => number == 31,
                ("s", "p") | ("w", "sp") => number == 31,
                ("x", digits) => size == SizeCode::X && digits.parse::<u16>().ok() == Some(number),
                ("w", digits) => size == SizeCode::W && digits.parse::<u16>().ok() == Some(number),
                _ => false,
            }
        }
    }
}

fn reg_pred_matches(expected: &RegPred, operand: &Operand) -> bool {
    match operand {
        Operand::Register(size, number)
        | Operand::RegisterOrSP(size, number)
        | Operand::RegisterPair(size, number) => size_code_matches(expected, *size, *number),
        _ => false,
    }
}

fn reg_number_matches(expected: &RegPred, number: u16) -> bool {
    match expected {
        RegPred::Named(name) => {
            let (prefix, rest) = name.split_at(1);
            match (prefix, rest) {
                ("x", "zr") | ("w", "zr") | ("s", "p") | ("w", "sp") => number == 31,
                (_, digits) => digits.parse::<u16>().ok() == Some(number),
            }
        }
        _ => true,
    }
}

fn reg_number(operand: &Operand) -> Option<u16> {
    match operand {
        Operand::Register(_, number)
        | Operand::RegisterOrSP(_, number)
        | Operand::RegisterPair(_, number) => Some(*number),
        _ => None,
    }
}

/// Immediate-like value used by the `#?` / `#0x..` predicates.
fn imm_like_value(operand: &Operand) -> Option<u64> {
    match operand {
        Operand::Immediate(value) => Some(u64::from(*value)),
        Operand::Imm64(value) => Some(*value),
        Operand::Imm16(value) => Some(u64::from(*value)),
        Operand::ImmShift(value, shift) => Some((u64::from(*value)) << (*shift).min(63)),
        Operand::ImmShiftMSL(value, shift) => Some((u64::from(*value)) << (*shift).min(63)),
        _ => None,
    }
}

/// Capture value for `imm` (the PCOffset target has its own `pcoff` kind).
fn capture_imm_value(operand: &Operand) -> Option<u64> {
    imm_like_value(operand)
}

fn mem_disp_value(operand: &Operand) -> Option<i64> {
    match operand {
        Operand::RegPreIndex(_, disp, _) => Some(i64::from(*disp)),
        Operand::RegPostIndex(_, disp) => Some(i64::from(*disp)),
        _ => None,
    }
}

fn pcoff_target(operand: &Operand, addr: usize, is_adrp: bool) -> Option<u64> {
    let Operand::PCOffset(offset) = operand else {
        return None;
    };
    let base = if is_adrp {
        (addr as u64) & !0xFFF
    } else {
        addr as u64
    };
    Some(base.wrapping_add(*offset as u64))
}

fn capture_value(spec: &ExtractSpec, insn: &DecodedInsn, matched: &InsnMatch) -> Option<Captured> {
    match spec.capture.kind {
        CaptureKind::Pc => Some(Captured {
            raw: insn.addr as u64,
            address_valued: true,
        }),
        CaptureKind::Imm => {
            let operand_index = matched.operand_indices[spec.capture.operand_index?]?;
            capture_imm_value(&insn.instr.operands[operand_index]).map(|raw| Captured {
                raw,
                address_valued: false,
            })
        }
        CaptureKind::Disp => {
            let predicate = spec.capture.operand_index?;
            if !matched.mem_disp[predicate] {
                return None;
            }
            let operand_index = matched.operand_indices[predicate]?;
            mem_disp_value(&insn.instr.operands[operand_index]).map(|raw| Captured {
                raw: raw as u64,
                address_valued: false,
            })
        }
        CaptureKind::Reg => {
            let operand_index = matched.operand_indices[spec.capture.operand_index?]?;
            reg_number(&insn.instr.operands[operand_index]).map(|number| Captured {
                raw: u64::from(number),
                address_valued: false,
            })
        }
        CaptureKind::Pcoff => {
            let operand_index = matched.operand_indices[spec.capture.operand_index?]?;
            let rendered = format!("{}", insn.instr);
            let is_adrp = rendered.split_whitespace().next() == Some("adrp");
            pcoff_target(&insn.instr.operands[operand_index], insn.addr, is_adrp).map(|raw| {
                Captured {
                    raw,
                    address_valued: true,
                }
            })
        }
    }
}
