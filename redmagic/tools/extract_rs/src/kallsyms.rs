//! /proc/kallsyms text parsing.

use std::collections::{BTreeMap, BTreeSet};

use regex::Regex;

use crate::error::{ExtractError, Result};

#[derive(Debug, Default)]
pub struct Kallsyms {
    pub symbols: BTreeMap<String, BTreeSet<u64>>,
    pub types: BTreeMap<String, BTreeSet<char>>,
}

pub fn parse(text: &str) -> Result<Kallsyms> {
    let pattern = Regex::new(r"^([0-9a-fA-F]{8,16})\s+(\S)\s+(.+?)\s*$").unwrap();
    let mut symbols: BTreeMap<String, BTreeSet<u64>> = BTreeMap::new();
    let mut types: BTreeMap<String, BTreeSet<char>> = BTreeMap::new();
    for line in text.lines() {
        let Some(captures) = pattern.captures(line) else {
            continue;
        };
        let Ok(value) = u64::from_str_radix(&captures[1], 16) else {
            continue;
        };
        if value == 0 {
            continue;
        }
        let symbol_type = captures[2].chars().next().unwrap();
        let name = captures[3].to_string();
        symbols.entry(name.clone()).or_default().insert(value);
        types.entry(name).or_default().insert(symbol_type);
    }
    if !symbols.contains_key("_text") && !symbols.contains_key("_head") {
        return Err(ExtractError::new("kallsyms has no _text or _head symbol"));
    }
    Ok(Kallsyms { symbols, types })
}

pub fn unique(symbols: &BTreeMap<String, BTreeSet<u64>>, name: &str) -> Option<u64> {
    let values = symbols.get(name)?;
    if values.len() == 1 {
        values.iter().next().copied()
    } else {
        None
    }
}

pub fn unique_or_err(symbols: &BTreeMap<String, BTreeSet<u64>>, name: &str) -> Result<u64> {
    let values = symbols.get(name).cloned().unwrap_or_default();
    if values.len() != 1 {
        let hex: Vec<String> = values.iter().map(|v| format!("{v:#x}")).collect();
        return Err(ExtractError::new(format!(
            "kallsyms symbol {name:?} not unique: {hex:?}"
        )));
    }
    Ok(*values.iter().next().unwrap())
}

pub fn unique_offset_optional(
    symbols: &BTreeMap<String, BTreeSet<u64>>,
    name: &str,
) -> Option<u64> {
    unique_or_err(symbols, name).ok()
}

pub fn find_data_symbol(
    symbols: &BTreeMap<String, BTreeSet<u64>>,
    types: &BTreeMap<String, BTreeSet<char>>,
    exact: &str,
    fragments: &[&str],
) -> Option<u64> {
    if let Some(address) = unique(symbols, exact) {
        return Some(address);
    }
    let mut matches: BTreeSet<u64> = BTreeSet::new();
    for (name, values) in symbols {
        if fragments.is_empty() {
            continue;
        }
        if !fragments.iter().all(|fragment| {
            name.to_ascii_lowercase()
                .contains(&fragment.to_ascii_lowercase())
        }) {
            continue;
        }
        let is_data = types
            .get(name)
            .map(|set| set.iter().any(|c| "dDbB".contains(*c)))
            .unwrap_or(false);
        if !is_data {
            continue;
        }
        matches.extend(values.iter().copied());
    }
    if matches.len() == 1 {
        matches.iter().next().copied()
    } else {
        None
    }
}

pub fn find_function(
    symbols: &BTreeMap<String, BTreeSet<u64>>,
    exact: &str,
    fragments: &[&str],
) -> Option<u64> {
    if let Some(address) = unique(symbols, exact) {
        return Some(address);
    }
    let mut matches: BTreeSet<u64> = BTreeSet::new();
    for (name, values) in symbols {
        if fragments.iter().all(|fragment| {
            name.to_ascii_lowercase()
                .contains(&fragment.to_ascii_lowercase())
        }) {
            matches.extend(values.iter().copied());
        }
    }
    if matches.len() == 1 {
        matches.iter().next().copied()
    } else {
        None
    }
}
