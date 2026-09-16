//! In-image kallsyms recovery: rebuild the kernel's embedded symbol table from
//! the uncompressed arm64 Image alone (no root, no /proc/kallsyms).
//!
//! Format (kernel/kallsyms.c, scripts/kallsyms.c): a 256-entry token table
//! with a u16 index, length-prefixed compressed names (u8 or uleb128 since
//! v4.20), u32 markers every 256 names, and 32-bit addresses relative to a
//! base (since v4.6; pre-6.4 they sit before the names, 6.4+ after the token
//! index). The scan/validation follows these invariants, matching the layout
//! used by KernelSU ksud for ARM64 GKI images.

use std::collections::{BTreeMap, BTreeSet};

use crate::error::{ExtractError, Result};
use crate::kallsyms::Kallsyms;

const ALIGN: usize = 8;
const TOKEN_COUNT: usize = 256;
const TOKEN_INDEX_SIZE: usize = TOKEN_COUNT * 2;
const MAX_TOKEN_LEN: usize = 256;
const MARKER_SEARCH_WINDOW: usize = 4 * 1024 * 1024;
const NAME_TAIL_SEARCH: usize = 0x40000;
const MIN_MARKERS: usize = 8;
const MAX_MARKERS: usize = 4096;

const SYMBOL_TYPES: &[u8] = b"aAbBcCdDeEfFgGiInNpPrRsStTuUvVwW?-";
const REQUIRED_SYMBOLS: [&str; 3] = ["_text", "_end", "load_module"];

const fn align_up(value: usize, alignment: usize) -> usize {
    (value + alignment - 1) & !(alignment - 1)
}

fn read_u16(data: &[u8], offset: usize) -> Option<u16> {
    let bytes: [u8; 2] = data.get(offset..offset + 2)?.try_into().ok()?;
    Some(u16::from_le_bytes(bytes))
}

fn read_u32(data: &[u8], offset: usize) -> Option<u32> {
    let bytes: [u8; 4] = data.get(offset..offset + 4)?.try_into().ok()?;
    Some(u32::from_le_bytes(bytes))
}

fn read_i32(data: &[u8], offset: usize) -> Option<i32> {
    let bytes: [u8; 4] = data.get(offset..offset + 4)?.try_into().ok()?;
    Some(i32::from_le_bytes(bytes))
}

fn read_u64(data: &[u8], offset: usize) -> Option<u64> {
    let bytes: [u8; 8] = data.get(offset..offset + 8)?.try_into().ok()?;
    Some(u64::from_le_bytes(bytes))
}

fn find_subslice(data: &[u8], needle: &[u8], start: usize) -> Option<usize> {
    if needle.is_empty() || start > data.len() {
        return None;
    }
    data[start..]
        .windows(needle.len())
        .position(|window| window == needle)
        .map(|position| position + start)
}

/// Image size from the arm64 Image header (offset 0x10), when present.  The
/// declared size may exceed the stored bytes: vendor boot.img packers often
/// trim trailing zeros (.bss/padding) from the Image.
fn arm64_image_size(image: &[u8]) -> Option<usize> {
    if image.len() < 64 || image.get(0x38..0x3c) != Some(b"ARM\x64") {
        return None;
    }
    let size = read_u64(image, 0x10)?;
    usize::try_from(size).ok().filter(|size| *size != 0)
}

struct TokenTable {
    tokens: Vec<Vec<u8>>,
    table_offset: usize,
    index_offset: usize,
}

/// Validate the 256-token table starting at `start`, whose '0' entry must sit
/// exactly at `digit_offset` (the position the digit run was found at).
fn parse_token_table_at(image: &[u8], start: usize, digit_offset: usize) -> Option<TokenTable> {
    if start % ALIGN != 0 {
        return None;
    }
    let mut tokens = Vec::with_capacity(TOKEN_COUNT);
    let mut token_offsets = Vec::with_capacity(TOKEN_COUNT);
    let mut token_starts = Vec::with_capacity(TOKEN_COUNT);
    let mut position = start;
    for _ in 0..TOKEN_COUNT {
        token_starts.push(position);
        let search_end = position.checked_add(MAX_TOKEN_LEN + 1)?.min(image.len());
        let length = image
            .get(position..search_end)?
            .iter()
            .position(|byte| *byte == 0)?;
        if length == 0 {
            return None;
        }
        let end = position.checked_add(length)?;
        let token = image.get(position..end)?;
        if token.iter().any(|byte| !(0x20..=0x7e).contains(byte)) {
            return None;
        }
        token_offsets.push(position.checked_sub(start)?);
        tokens.push(token.to_vec());
        position = end.checked_add(1)?;
    }

    if *token_starts.get(b'0' as usize)? != digit_offset {
        return None;
    }
    for &value in b"0123456789_abcdefghijklmnopqrstuvwxyzT" {
        if tokens.get(value as usize)?.as_slice() != [value] {
            return None;
        }
    }

    let index_offset = align_up(position, ALIGN);
    if image
        .get(position..index_offset)?
        .iter()
        .any(|byte| *byte != 0)
    {
        return None;
    }
    let index_end = index_offset.checked_add(TOKEN_INDEX_SIZE)?;
    if index_end > image.len() || *token_offsets.last()? > u16::MAX as usize {
        return None;
    }
    for (index, &expected) in token_offsets.iter().enumerate() {
        if read_u16(image, index_offset + index * 2)? as usize != expected {
            return None;
        }
    }
    Some(TokenTable {
        tokens,
        table_offset: start,
        index_offset,
    })
}

/// Scan the image for token tables: find the "0\0..9\0" digit run, then try
/// every 8-aligned table start that could place the '0' entry at that run.
fn find_token_tables(image: &[u8]) -> Vec<TokenTable> {
    let mut digit_needle = Vec::with_capacity(20);
    for value in b'0'..=b'9' {
        digit_needle.extend_from_slice(&[value, 0]);
    }
    let mut tables = Vec::new();
    let mut search_from = 0;
    while let Some(digit_offset) = find_subslice(image, &digit_needle, search_from) {
        let earliest = digit_offset.saturating_sub(b'0' as usize * (MAX_TOKEN_LEN + 1));
        let mut start = align_up(earliest, ALIGN);
        while start <= digit_offset {
            if let Some(table) = parse_token_table_at(image, start, digit_offset) {
                tables.push(table);
            }
            start += ALIGN;
        }
        search_from = digit_offset.saturating_add(1);
        if search_from >= image.len() {
            break;
        }
    }
    tables
}

/// Read `kallsyms_markers` starting at `start`: first entry 0, every following
/// delta within one name-block (0x200..=0x40000); bounded by `limit`.
fn read_markers(image: &[u8], start: usize, limit: usize) -> Vec<u32> {
    if start % ALIGN != 0 || start.saturating_add(8) > limit {
        return Vec::new();
    }
    let Some(first) = read_u32(image, start) else {
        return Vec::new();
    };
    let Some(second) = read_u32(image, start + 4) else {
        return Vec::new();
    };
    if first != 0 || !(0x200..=0x40000).contains(&second) {
        return Vec::new();
    }

    let mut markers = vec![first, second];
    let mut position = start + 8;
    while markers.len() < MAX_MARKERS && position.saturating_add(4) <= limit {
        let Some(value) = read_u32(image, position) else {
            break;
        };
        let Some(delta) = value.checked_sub(*markers.last().expect("non-empty markers")) else {
            break;
        };
        if !(0x200..=0x40000).contains(&delta) {
            break;
        }
        markers.push(value);
        position += 4;
    }
    if markers.len() < MIN_MARKERS {
        Vec::new()
    } else {
        markers
    }
}

/// Walk `count` length-prefixed name entries; every 256th entry must match the
/// corresponding marker, and the block must end exactly at `markers_offset`.
fn parse_name_spans(
    image: &[u8],
    names_offset: usize,
    count: usize,
    markers_offset: usize,
    markers: &[u32],
    uleb128_lengths: bool,
) -> Option<Vec<(usize, usize)>> {
    if count == 0 || markers.len() != count.div_ceil(256) {
        return None;
    }

    let mut spans = Vec::with_capacity(count);
    let mut position = names_offset;
    for index in 0..count {
        if index.is_multiple_of(256)
            && position.checked_sub(names_offset)? != markers[index / 256] as usize
        {
            return None;
        }
        if position >= markers_offset {
            return None;
        }
        let mut length = *image.get(position)? as usize;
        position += 1;
        if uleb128_lengths && length & 0x80 != 0 {
            if position >= markers_offset {
                return None;
            }
            let high = *image.get(position)? as usize;
            if high & 0x80 != 0 {
                return None;
            }
            length = (length & 0x7f) | (high << 7);
            position += 1;
        }
        if length == 0 || length > 0x3fff || position.checked_add(length)? > markers_offset {
            return None;
        }
        spans.push((position, length));
        position += length;
    }

    if align_up(position, ALIGN) != markers_offset
        || image
            .get(position..markers_offset)?
            .iter()
            .any(|byte| *byte != 0)
    {
        return None;
    }
    Some(spans)
}

/// Expand token runs into `(type, name)` pairs and require the anchor symbols
/// that every real kallsyms table carries.
fn decode_names(
    image: &[u8],
    spans: &[(usize, usize)],
    tokens: &[Vec<u8>],
) -> Option<Vec<(u8, String)>> {
    let mut decoded = Vec::with_capacity(spans.len());
    for &(position, length) in spans {
        let encoded = image.get(position..position.checked_add(length)?)?;
        let expanded_length = encoded.iter().try_fold(0usize, |total, &index| {
            total.checked_add(tokens.get(index as usize)?.len())
        })?;
        if expanded_length > 4096 {
            return None;
        }
        let mut expanded = Vec::with_capacity(expanded_length);
        for &index in encoded {
            expanded.extend_from_slice(tokens.get(index as usize)?);
        }
        let (&kind, name_bytes) = expanded.split_first()?;
        if name_bytes.is_empty()
            || !SYMBOL_TYPES.contains(&kind)
            || name_bytes.iter().any(|byte| !(0x21..=0x7e).contains(byte))
        {
            return None;
        }
        decoded.push((kind, String::from_utf8(name_bytes.to_vec()).ok()?));
    }

    let names: BTreeSet<&str> = decoded.iter().map(|(_, name)| name.as_str()).collect();
    if REQUIRED_SYMBOLS
        .iter()
        .any(|required| !names.contains(required))
    {
        return None;
    }
    Some(decoded)
}

/// (num_syms offset, names offset, markers offset, decoded symbols)
type NameTable = (usize, usize, usize, Vec<(u8, String)>);

/// Recover the names block: search for markers before the token table, then
/// hunt for the `num_syms` word whose count and padding line up with the
/// markers, decoding both u8 and uleb128 length encodings.
fn find_name_tables(image: &[u8], tokens: &[Vec<u8>], token_table_offset: usize) -> Vec<NameTable> {
    let lower = token_table_offset.saturating_sub(MARKER_SEARCH_WINDOW);
    let mut marker_candidates = Vec::new();
    let mut position = align_up(lower, ALIGN);
    while position.saturating_add(8) <= token_table_offset {
        let markers = read_markers(image, position, token_table_offset);
        if !markers.is_empty() {
            marker_candidates.push((position, markers));
        }
        position += ALIGN;
    }

    let mut recovered = Vec::new();
    for (markers_offset, markers) in marker_candidates {
        let minimum_count = (markers.len() - 1) * 256 + 1;
        let maximum_count = markers.len() * 256;
        let Some(approximate_names) =
            markers_offset.checked_sub(*markers.last().expect("non-empty markers") as usize)
        else {
            continue;
        };
        let search_start = approximate_names.saturating_sub(NAME_TAIL_SEARCH);
        let mut num_syms_offset = approximate_names - approximate_names % ALIGN;
        loop {
            if num_syms_offset.saturating_add(8) <= image.len() {
                let count = read_u32(image, num_syms_offset).unwrap_or(0) as usize;
                let padding_is_zero = matches!(
                    image.get(num_syms_offset + 4..num_syms_offset + 8),
                    Some(padding) if padding == [0u8; 4]
                );
                if (minimum_count..=maximum_count).contains(&count) && padding_is_zero {
                    let names_offset = num_syms_offset + ALIGN;
                    let mut seen_spans: Vec<Vec<(usize, usize)>> = Vec::new();
                    for uleb128_lengths in [true, false] {
                        let Some(spans) = parse_name_spans(
                            image,
                            names_offset,
                            count,
                            markers_offset,
                            &markers,
                            uleb128_lengths,
                        ) else {
                            continue;
                        };
                        if seen_spans.contains(&spans) {
                            continue;
                        }
                        seen_spans.push(spans.clone());
                        if let Some(decoded) = decode_names(image, &spans, tokens) {
                            recovered.push((
                                num_syms_offset,
                                names_offset,
                                markers_offset,
                                decoded,
                            ));
                        }
                    }
                }
            }
            if num_syms_offset < search_start.saturating_add(ALIGN) {
                break;
            }
            num_syms_offset -= ALIGN;
        }
    }
    recovered
}

const fn is_arm64_kernel_address(address: u64) -> bool {
    address >> 48 == 0xffff && address.is_multiple_of(4096)
}

/// Decode `kallsyms_offsets` + `kallsyms_relative_base` into absolute
/// addresses, requiring a sorted table that spans exactly the image.
fn decode_addresses(
    image: &[u8],
    image_size: usize,
    names: &[(u8, String)],
    offsets_offset: usize,
    relative_base_offset: usize,
) -> Option<Vec<(u64, u8, String)>> {
    let count = names.len();
    let offsets_end = offsets_offset.checked_add(count.checked_mul(4)?)?;
    if offsets_end > image.len()
        || relative_base_offset < offsets_end
        || relative_base_offset.checked_add(8)? > image.len()
        || image
            .get(offsets_end..relative_base_offset)?
            .iter()
            .any(|byte| *byte != 0)
    {
        return None;
    }
    let relative_base = read_u64(image, relative_base_offset)?;
    if !is_arm64_kernel_address(relative_base) {
        return None;
    }

    let signed_offsets = (0..count)
        .map(|index| read_i32(image, offsets_offset + index * 4))
        .collect::<Option<Vec<_>>>()?;
    let negative_count = signed_offsets.iter().filter(|offset| **offset < 0).count();
    let addresses = if negative_count * 2 >= count {
        signed_offsets
            .iter()
            .map(|&offset| {
                let address = if offset < 0 {
                    i128::from(relative_base) - 1 - i128::from(offset)
                } else {
                    i128::from(offset)
                };
                u64::try_from(address).ok()
            })
            .collect::<Option<Vec<_>>>()?
    } else {
        (0..count)
            .map(|index| {
                relative_base.checked_add(u64::from(read_u32(image, offsets_offset + index * 4)?))
            })
            .collect::<Option<Vec<_>>>()?
    };

    if addresses.windows(2).any(|pair| pair[0] > pair[1]) {
        return None;
    }
    let text_addresses = addresses
        .iter()
        .zip(names)
        .filter_map(|(&address, (_, name))| (name == "_text").then_some(address))
        .collect::<Vec<_>>();
    let end_addresses = addresses
        .iter()
        .zip(names)
        .filter_map(|(&address, (_, name))| (name == "_end").then_some(address))
        .collect::<Vec<_>>();
    if text_addresses.len() != 1
        || end_addresses.len() != 1
        || text_addresses[0] != relative_base
        || end_addresses[0].checked_sub(text_addresses[0])? != image_size as u64
    {
        return None;
    }

    let ordinary_addresses = addresses
        .iter()
        .zip(names)
        .filter_map(|(&address, (kind, _))| {
            (!b"Aa".contains(kind) && address != 0).then_some(address)
        })
        .collect::<Vec<_>>();
    let in_image = ordinary_addresses
        .iter()
        .filter(|&&address| text_addresses[0] <= address && address <= end_addresses[0])
        .count();
    if ordinary_addresses.is_empty() || in_image * 100 < ordinary_addresses.len() * 90 {
        return None;
    }

    Some(
        addresses
            .into_iter()
            .zip(names)
            .map(|(address, (kind, name))| (address, *kind, name.clone()))
            .collect(),
    )
}

struct Candidate {
    entries: Vec<(u64, u8, String)>,
    layout: &'static str,
}

impl Candidate {
    fn resolve(&self, name: &str) -> Option<u64> {
        let mut found = None;
        for &(address, _, ref candidate) in &self.entries {
            if candidate == name {
                if found.replace(address).is_some() {
                    return None;
                }
            }
        }
        found
    }
}

/// Recover the full kallsyms table from an uncompressed arm64 Image.
///
/// `btf` optionally carries the embedded BTF blob's (file offset, size); when
/// several candidates survive, the one whose `__start_BTF`/`__stop_BTF`
/// delimit that blob wins.
pub fn recover(image: &[u8], btf: Option<(usize, usize)>) -> Result<Kallsyms> {
    let image_size = arm64_image_size(image).unwrap_or(image.len());
    let token_tables = find_token_tables(image);
    let mut candidates = Vec::new();
    let mut name_blocks = 0usize;

    for table in &token_tables {
        let name_tables = find_name_tables(image, &table.tokens, table.table_offset);
        for (num_syms_offset, _, _, names) in name_tables {
            name_blocks += 1;
            let count = names.len();
            let old_relative_base = num_syms_offset.saturating_sub(ALIGN);
            let old_offsets = old_relative_base.saturating_sub(align_up(count * 4, ALIGN));
            let new_offsets = align_up(table.index_offset + TOKEN_INDEX_SIZE, ALIGN);
            let new_relative_base = align_up(new_offsets + count * 4, ALIGN);
            for (layout, offsets_offset, relative_base_offset) in [
                ("pre-6.4", old_offsets, old_relative_base),
                ("6.4+", new_offsets, new_relative_base),
            ] {
                if let Some(entries) = decode_addresses(
                    image,
                    image_size,
                    &names,
                    offsets_offset,
                    relative_base_offset,
                ) {
                    candidates.push(Candidate { entries, layout });
                }
            }
        }
    }

    if candidates.is_empty() {
        // No name block at all points at a stripped/moved kallsyms table;
        // name blocks without a matching address table point at an unusual
        // offsets/relative_base or image-size layout.
        return Err(ExtractError::new(format!(
            "no kallsyms table found in the kernel image ({} token tables, {} name blocks, image_size=0x{image_size:x})",
            token_tables.len(),
            name_blocks,
        )));
    }

    let mut boundary_matches = Vec::new();
    if let Some((btf_offset, btf_size)) = btf {
        for (index, candidate) in candidates.iter().enumerate() {
            let Some(base) = candidate.resolve("_text") else {
                continue;
            };
            let Some(btf_start) = candidate.resolve("__start_BTF") else {
                continue;
            };
            let Some(btf_stop) = candidate.resolve("__stop_BTF") else {
                continue;
            };
            let Some(recovered_offset) = btf_start.checked_sub(base) else {
                continue;
            };
            let Some(recovered_size) = btf_stop.checked_sub(btf_start) else {
                continue;
            };
            if recovered_offset == btf_offset as u64 && recovered_size == btf_size as u64 {
                boundary_matches.push(index);
            }
        }
    }

    let selected = if boundary_matches.len() == 1 {
        candidates.swap_remove(boundary_matches[0])
    } else if boundary_matches.is_empty() && candidates.len() == 1 {
        candidates.pop().expect("one candidate")
    } else {
        return Err(ExtractError::new(format!(
            "ambiguous kallsyms recovery in the kernel image ({} token tables, {} complete candidates, {} BTF boundary matches)",
            token_tables.len(),
            candidates.len(),
            boundary_matches.len(),
        )));
    };
    eprintln!(
        "info: kallsyms layout {} recovered ({} symbols)",
        selected.layout,
        selected.entries.len()
    );

    let mut symbols: BTreeMap<String, BTreeSet<u64>> = BTreeMap::new();
    let mut types: BTreeMap<String, BTreeSet<char>> = BTreeMap::new();
    for (address, kind, name) in selected.entries {
        symbols.entry(name.clone()).or_default().insert(address);
        types.entry(name).or_default().insert(kind as char);
    }
    Ok(Kallsyms { symbols, types })
}
