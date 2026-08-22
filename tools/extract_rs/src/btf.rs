//! Minimal BTF (BPF Type Format) parser used for struct-offset derivation.

use std::collections::{HashMap, HashSet};

use crate::boot::BTF_MAGIC;
use crate::error::{ExtractError, Result};

pub const KIND_INT: u32 = 1;
pub const KIND_PTR: u32 = 2;
pub const KIND_ARRAY: u32 = 3;
pub const KIND_STRUCT: u32 = 4;
pub const KIND_UNION: u32 = 5;
pub const KIND_ENUM: u32 = 6;
pub const KIND_FWD: u32 = 7;
pub const KIND_TYPEDEF: u32 = 8;
pub const KIND_VOLATILE: u32 = 9;
pub const KIND_CONST: u32 = 10;
pub const KIND_RESTRICT: u32 = 11;
pub const KIND_FUNC: u32 = 12;
pub const KIND_FUNC_PROTO: u32 = 13;
pub const KIND_VAR: u32 = 14;
pub const KIND_DATASEC: u32 = 15;
pub const KIND_FLOAT: u32 = 16;
pub const KIND_DECL_TAG: u32 = 17;
pub const KIND_TYPE_TAG: u32 = 18;
pub const KIND_ENUM64: u32 = 19;

#[derive(Debug, Clone)]
pub struct BtfMember {
    pub name: String,
    pub type_id: u32,
    pub bit_offset: u32,
}

#[derive(Debug, Clone)]
pub struct BtfType {
    pub type_id: u32,
    pub name: String,
    pub kind: u32,
    /// For qualifiers this holds the referenced type id; for sized kinds the size.
    pub size_or_type: u32,
    pub members: Vec<BtfMember>,
    pub enum_values: Vec<(String, i64)>,
}

pub struct Btf {
    strings: Vec<u8>,
    types: HashMap<u32, BtfType>,
    by_name: HashMap<String, Vec<u32>>,
}

fn rd_u32(data: &[u8], off: usize) -> u32 {
    u32::from_le_bytes(data[off..off + 4].try_into().unwrap())
}

impl Btf {
    pub fn new(raw: &[u8]) -> Result<Btf> {
        if raw.len() < 24 {
            return Err(ExtractError::new("truncated BTF header"));
        }
        let magic = u16::from_le_bytes(raw[0..2].try_into().unwrap());
        let version = raw[2];
        let hdr_len = rd_u32(raw, 4) as usize;
        let type_off = rd_u32(raw, 8) as usize;
        let type_len = rd_u32(raw, 12) as usize;
        let str_off = rd_u32(raw, 16) as usize;
        let str_len = rd_u32(raw, 20) as usize;
        if magic != BTF_MAGIC || version != 1 || hdr_len < 24 {
            return Err(ExtractError::new("invalid BTF header"));
        }
        let types_raw = &raw[hdr_len + type_off..hdr_len + type_off + type_len];
        let declared = hdr_len + (type_off + type_len).max(str_off + str_len);
        let strings = if raw.len() > declared {
            // embedded_btf extended the blob with strings past str_len
            raw[hdr_len + str_off..].to_vec()
        } else {
            raw[hdr_len + str_off..hdr_len + str_off + str_len].to_vec()
        };
        let mut btf = Btf {
            strings,
            types: HashMap::new(),
            by_name: HashMap::new(),
        };
        btf.parse(types_raw)?;
        Ok(btf)
    }

    fn string(&self, offset: usize) -> Result<String> {
        if offset == 0 {
            return Ok(String::new());
        }
        if offset >= self.strings.len() {
            return Err(ExtractError::new(format!(
                "invalid BTF string offset {offset}"
            )));
        }
        let end = find_subslice_in(&self.strings, &[0], offset, self.strings.len());
        let end = end.unwrap_or(self.strings.len());
        Ok(String::from_utf8_lossy(&self.strings[offset..end]).into_owned())
    }

    fn parse(&mut self, types_raw: &[u8]) -> Result<()> {
        let fixed = [
            (KIND_INT, 4usize),
            (KIND_PTR, 0),
            (KIND_ARRAY, 12),
            (KIND_ENUM, 8),
            (KIND_FWD, 0),
            (KIND_TYPEDEF, 0),
            (KIND_VOLATILE, 0),
            (KIND_CONST, 0),
            (KIND_RESTRICT, 0),
            (KIND_FUNC, 0),
            (KIND_FUNC_PROTO, 8),
            (KIND_VAR, 4),
            (KIND_DATASEC, 12),
            (KIND_FLOAT, 0),
            (KIND_DECL_TAG, 8),
            (KIND_TYPE_TAG, 0),
            (KIND_ENUM64, 12),
        ];
        let mut cursor = 0usize;
        let mut type_id: u32 = 1;
        while cursor < types_raw.len() {
            if cursor + 12 > types_raw.len() {
                return Err(ExtractError::new("truncated BTF type record"));
            }
            let name_off = rd_u32(types_raw, cursor) as usize;
            let info = rd_u32(types_raw, cursor + 4);
            let size_or_type = rd_u32(types_raw, cursor + 8);
            cursor += 12;
            let kind = (info >> 24) & 0x1F;
            let vlen = (info & 0xFFFF) as usize;
            let name = self.string(name_off)?;
            let mut item = BtfType {
                type_id,
                name: name.clone(),
                kind,
                size_or_type,
                members: Vec::new(),
                enum_values: Vec::new(),
            };
            if kind == KIND_STRUCT || kind == KIND_UNION {
                let extra = vlen
                    .checked_mul(12)
                    .ok_or_else(|| ExtractError::new("truncated BTF members (overflow)"))?;
                if cursor + extra > types_raw.len() {
                    return Err(ExtractError::new("truncated BTF members"));
                }
                for index in 0..vlen {
                    let base = cursor + index * 12;
                    let name = self.string(rd_u32(types_raw, base) as usize)?;
                    let member_type = rd_u32(types_raw, base + 4);
                    let bit_offset = rd_u32(types_raw, base + 8) & 0xFF_FFFF;
                    item.members.push(BtfMember {
                        name,
                        type_id: member_type,
                        bit_offset,
                    });
                }
                cursor += extra;
            } else if kind == KIND_ENUM {
                let extra = vlen
                    .checked_mul(8)
                    .ok_or_else(|| ExtractError::new("truncated BTF enum members (overflow)"))?;
                if cursor + extra > types_raw.len() {
                    return Err(ExtractError::new("truncated BTF enum members"));
                }
                let kflag = info & (1 << 31) != 0;
                for index in 0..vlen {
                    let base = cursor + index * 8;
                    let ename = self.string(rd_u32(types_raw, base) as usize)?;
                    let raw_value = rd_u32(types_raw, base + 4);
                    let value = if kflag {
                        raw_value as i32 as i64
                    } else {
                        raw_value as i64
                    };
                    item.enum_values.push((ename, value));
                }
                cursor += extra;
            } else if kind == KIND_ENUM64 {
                let extra = vlen
                    .checked_mul(12)
                    .ok_or_else(|| ExtractError::new("truncated BTF enum64 members (overflow)"))?;
                if cursor + extra > types_raw.len() {
                    return Err(ExtractError::new("truncated BTF enum64 members"));
                }
                let kflag = info & (1 << 31) != 0;
                for index in 0..vlen {
                    let base = cursor + index * 12;
                    let ename = self.string(rd_u32(types_raw, base) as usize)?;
                    let low = rd_u32(types_raw, base + 4) as u64;
                    let high = rd_u32(types_raw, base + 8) as u64;
                    // Two's-complement cast mirrors Python's `value -= 1 << 64`
                    // for kflag enums with the sign bit set.
                    let value = (low | (high << 32)) as i64;
                    let _ = kflag;
                    item.enum_values.push((ename, value));
                }
                cursor += extra;
            } else {
                let unit = fixed
                    .iter()
                    .find(|(k, _)| *k == kind)
                    .map(|(_, u)| *u)
                    .ok_or_else(|| ExtractError::new(format!("unsupported BTF kind {kind}")))?;
                if kind == KIND_FUNC_PROTO || kind == KIND_DATASEC {
                    cursor += unit * vlen;
                } else {
                    cursor += unit;
                }
            }
            self.by_name.entry(name.clone()).or_default().push(type_id);
            self.types.insert(type_id, item);
            type_id += 1;
        }
        Ok(())
    }

    pub fn type_of(&self, type_id: u32) -> Option<&BtfType> {
        self.types.get(&type_id)
    }

    /// Largest struct/union with this name.
    pub fn named_struct(&self, name: &str) -> Option<&BtfType> {
        let candidates = self.by_name.get(name)?;
        candidates
            .iter()
            .filter_map(|id| self.types.get(id))
            .filter(|item| item.kind == KIND_STRUCT || item.kind == KIND_UNION)
            .max_by_key(|item| item.members.len())
    }

    pub fn resolve(&self, mut type_id: u32) -> Option<&BtfType> {
        let mut seen: HashSet<u32> = HashSet::new();
        while type_id != 0 && !seen.contains(&type_id) {
            seen.insert(type_id);
            let item = self.types.get(&type_id)?;
            if !matches!(
                item.kind,
                KIND_TYPEDEF | KIND_VOLATILE | KIND_CONST | KIND_RESTRICT | KIND_TYPE_TAG
            ) {
                return Some(item);
            }
            type_id = item.size_or_type;
        }
        None
    }

    fn find_member(
        &self,
        item: &BtfType,
        name: &str,
        base: u32,
        depth: usize,
        seen: &mut HashSet<u32>,
    ) -> Option<u32> {
        // the shared seen set bounds nested anonymous members; the depth
        // cap catches cycles
        if depth > 64 || seen.contains(&item.type_id) {
            return None;
        }
        seen.insert(item.type_id);
        for member in &item.members {
            let offset = base.wrapping_add(member.bit_offset);
            if member.name == name {
                return Some(offset / 8);
            }
            if member.name.is_empty() {
                if let Some(child) = self.resolve(member.type_id) {
                    if child.kind == KIND_STRUCT || child.kind == KIND_UNION {
                        if let Some(found) =
                            self.find_member(child, name, offset, depth + 1, seen)
                        {
                            return Some(found);
                        }
                    }
                }
            }
        }
        None
    }

    pub fn field(&self, struct_name: &str, field_name: &str) -> Option<u32> {
        let item = self.named_struct(struct_name)?;
        self.find_member(item, field_name, 0, 0, &mut HashSet::new())
    }

    pub fn size(&self, struct_name: &str) -> Option<u32> {
        self.named_struct(struct_name).map(|item| item.size_or_type)
    }

    /// Value of one member of a named enum/enum64; None when ambiguous.
    pub fn enum_value(&self, enum_name: &str, member_name: &str) -> Option<i64> {
        let candidates: Vec<&BtfType> = self
            .by_name
            .get(enum_name)?
            .iter()
            .filter_map(|id| self.types.get(id))
            .filter(|item| item.kind == KIND_ENUM || item.kind == KIND_ENUM64)
            .collect();
        let item = candidates
            .into_iter()
            .max_by_key(|item| item.enum_values.len())?;
        let values: Vec<i64> = item
            .enum_values
            .iter()
            .filter(|(name, _)| name == member_name)
            .map(|(_, value)| *value)
            .collect();
        if values.len() == 1 {
            Some(values[0])
        } else {
            None
        }
    }

    /// Value of an enum member that appears exactly once in the whole BTF.
    pub fn unique_enum_member_value(&self, member_name: &str) -> Option<i64> {
        let mut matches: Vec<i64> = Vec::new();
        for item in self.types.values() {
            if item.kind != KIND_ENUM && item.kind != KIND_ENUM64 {
                continue;
            }
            for (name, value) in &item.enum_values {
                if name == member_name {
                    matches.push(*value);
                }
            }
        }
        if matches.len() == 1 {
            Some(matches[0])
        } else {
            None
        }
    }

    /// Byte size of a BTF type id, resolving qualifiers and arrays.
    pub fn type_size(&self, type_id: u32) -> Option<u32> {
        let resolved = self.resolve(type_id)?;
        if resolved.kind == KIND_PTR {
            return Some(8);
        }
        if resolved.kind == KIND_ARRAY {
            return None;
        }
        if matches!(
            resolved.kind,
            KIND_INT | KIND_STRUCT | KIND_UNION | KIND_ENUM | KIND_ENUM64 | KIND_FLOAT
        ) {
            return Some(resolved.size_or_type);
        }
        None
    }

    /// Byte size of a direct (non-anonymous) struct member's type.
    pub fn direct_field_size(&self, struct_name: &str, field_name: &str) -> Option<u32> {
        let item = self.named_struct(struct_name)?;
        let matches: Vec<&BtfMember> = item
            .members
            .iter()
            .filter(|member| member.name == field_name)
            .collect();
        if matches.len() != 1 {
            return None;
        }
        self.type_size(matches[0].type_id)
    }
}

fn find_subslice_in(haystack: &[u8], needle: &[u8], from: usize, to: usize) -> Option<usize> {
    if needle.is_empty() {
        return Some(from);
    }
    let from = from.min(haystack.len());
    let to = to.min(haystack.len());
    if to < needle.len() {
        return None;
    }
    (from..=to - needle.len()).find(|&i| &haystack[i..i + needle.len()] == needle)
}
