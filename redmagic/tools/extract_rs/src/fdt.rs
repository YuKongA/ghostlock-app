//! XBL xbl_config.img FDT memory-map parsing: recover kernel physical load.

use std::path::Path;

use crate::boot::{PAGE_SIZE, align};
use crate::error::{ExtractError, Result};

pub const FDT_MAGIC: u32 = 0xD00D_FEED;
pub const FDT_BEGIN_NODE: u32 = 1;
pub const FDT_END_NODE: u32 = 2;
pub const FDT_PROP: u32 = 3;
pub const FDT_NOP: u32 = 4;
pub const FDT_END: u32 = 9;
pub const ARM64_MEMSTART_ALIGN: u64 = 1 << 30;

fn rd_be_u32(data: &[u8], off: usize) -> u32 {
    u32::from_be_bytes(data[off..off + 4].try_into().unwrap())
}

fn rd_be_u64(data: &[u8], off: usize) -> u64 {
    u64::from_be_bytes(data[off..off + 8].try_into().unwrap())
}

/// Recover the XBL Kernel physical base from embedded FDT memory maps.
pub fn recover_kernel_phys_load(path: &Path) -> Result<u64> {
    let data = std::fs::read(path)?;
    let mut candidates: Vec<(u64, u64, u64, u64)> = Vec::new();
    let mut cursor = 0usize;
    while let Some(pos) = find_fdt_magic(&data, cursor) {
        cursor = pos + 4;
        if pos + 40 > data.len() {
            continue;
        }
        let parse_result = (|| -> Result<(u64, u64, u64, u64)> {
            let magic = rd_be_u32(&data, pos);
            let total = rd_be_u32(&data, pos + 4) as usize;
            let struct_off = rd_be_u32(&data, pos + 8) as usize;
            let strings_off = rd_be_u32(&data, pos + 12) as usize;
            let version = rd_be_u32(&data, pos + 20);
            let last_version = rd_be_u32(&data, pos + 24);
            let strings_size = rd_be_u32(&data, pos + 32) as usize;
            let struct_size = rd_be_u32(&data, pos + 36) as usize;
            if magic != FDT_MAGIC || version < 16 || last_version > 17 {
                return Err(ExtractError::new("bad FDT header"));
            }
            if total < 40 || pos + total > data.len() {
                return Err(ExtractError::new("bad FDT total size"));
            }
            if struct_off > total || struct_size > total - struct_off {
                return Err(ExtractError::new("bad FDT struct section"));
            }
            if strings_off > total || strings_size > total - strings_off {
                return Err(ExtractError::new("bad FDT strings section"));
            }
            let struct_start = pos + struct_off;
            let struct_end = pos + struct_off + struct_size;
            let strings_start = pos + strings_off;
            let strings_end = pos + strings_off + strings_size;
            let mut stack: Vec<FdtNode> = Vec::new();
            let mut regions: Vec<(String, String, u64, u64)> = Vec::new();
            let mut p = struct_start;
            let mut terminated = false;
            while p + 4 <= struct_end {
                let token = rd_be_u32(&data, p);
                if token == FDT_BEGIN_NODE {
                    let Some(nul) = find_byte(&data, p + 4, struct_end) else {
                        return Err(ExtractError::new("unterminated node name"));
                    };
                    let name_bytes = &data[p + 4..nul];
                    let name = std::str::from_utf8(name_bytes)
                        .map_err(|_| ExtractError::new("non-ascii node name"))?
                        .to_string();
                    let (parent_ac, parent_sc) = match stack.last() {
                        Some(parent) => (parent.child_ac, parent.child_sc),
                        None => (2, 1),
                    };
                    let path_name = match stack.last() {
                        Some(parent) => {
                            let base = parent.path.trim_end_matches('/');
                            if base.is_empty() {
                                format!("/{name}")
                            } else {
                                format!("{base}/{name}")
                            }
                        }
                        None => {
                            if name.is_empty() {
                                "/".to_string()
                            } else {
                                format!("/{name}")
                            }
                        }
                    };
                    stack.push(FdtNode {
                        path: path_name,
                        parent_ac,
                        parent_sc,
                        child_ac: 2,
                        child_sc: 1,
                        props: std::collections::HashMap::new(),
                    });
                    p = align(nul + 1, 4);
                } else if token == FDT_END_NODE {
                    let Some(node) = stack.pop() else {
                        return Err(ExtractError::new("FDT end without node"));
                    };
                    // SM8650 xbl_config spells it "MemLabel"; the dt-binding
                    // name is "mem-label". Accept either.
                    let label = node
                        .props
                        .get("MemLabel")
                        .or_else(|| node.props.get("mem-label"))
                        .map(|v| {
                            let end = find_byte(v, 0, v.len()).unwrap_or(v.len());
                            String::from_utf8_lossy(&v[..end]).into_owned()
                        })
                        .unwrap_or_default();
                    let reg = node.props.get("reg").cloned();
                    if node.path.contains("/memorymap/")
                        && (label == "NOMAP" || label == "Kernel")
                        && reg.is_some()
                    {
                        let reg = reg.unwrap();
                        let ac = node.parent_ac;
                        let sc = node.parent_sc;
                        if ac != 1 && ac != 2 {
                            return Err(ExtractError::new("unsupported memory-map reg"));
                        }
                        if sc != 1 && sc != 2 {
                            return Err(ExtractError::new("unsupported memory-map reg"));
                        }
                        if reg.len() != (ac + sc) as usize * 4 {
                            return Err(ExtractError::new("unsupported memory-map reg"));
                        }
                        let split = (ac * 4) as usize;
                        let base = if ac == 1 {
                            rd_be_u32(&reg, 0) as u64
                        } else {
                            rd_be_u64(&reg, 0)
                        };
                        let size = if sc == 1 {
                            rd_be_u32(&reg, split) as u64
                        } else {
                            rd_be_u64(&reg, split)
                        };
                        regions.push((label, node.path.clone(), base, size));
                    }
                    p += 4;
                } else if token == FDT_PROP {
                    if p + 12 > data.len() || stack.is_empty() {
                        return Err(ExtractError::new("invalid property"));
                    }
                    let size = rd_be_u32(&data, p + 4) as usize;
                    let name_off = rd_be_u32(&data, p + 8) as usize;
                    if name_off >= strings_size || p + 12 + size > struct_end {
                        return Err(ExtractError::new("invalid property"));
                    }
                    let ns = strings_start + name_off;
                    let Some(ne) = find_byte(&data, ns, strings_end) else {
                        return Err(ExtractError::new("unterminated property name"));
                    };
                    let prop_name = std::str::from_utf8(&data[ns..ne])
                        .map_err(|_| ExtractError::new("non-ascii property name"))?
                        .to_string();
                    let value = data[p + 12..p + 12 + size].to_vec();
                    let props = &mut stack.last_mut().unwrap().props;
                    props.insert(prop_name.clone(), value.clone());
                    if prop_name == "#address-cells" && size == 4 {
                        stack.last_mut().unwrap().child_ac = rd_be_u32(&value, 0);
                    } else if prop_name == "#size-cells" && size == 4 {
                        stack.last_mut().unwrap().child_sc = rd_be_u32(&value, 0);
                    }
                    p = align(p + 12 + size, 4);
                } else if token == FDT_NOP {
                    p += 4;
                } else if token == FDT_END {
                    terminated = true;
                    break;
                } else {
                    return Err(ExtractError::new("unknown FDT token"));
                }
            }
            if !terminated {
                return Err(ExtractError::new("truncated FDT: no end token"));
            }
            let nomap: Vec<(u64, u64)> = regions
                .iter()
                .filter(|(label, _, _, _)| label == "NOMAP")
                .map(|(_, _, base, size)| (*base, *size))
                .collect();
            let kernel: Vec<(u64, u64)> = regions
                .iter()
                .filter(|(label, _, _, _)| label == "Kernel")
                .map(|(_, _, base, size)| (*base, *size))
                .collect();
            if nomap.len() != 1 || kernel.len() != 1 {
                return Err(ExtractError::new("non-unique memory map"));
            }
            let (nb, ns) = nomap[0];
            let (kb, ks) = kernel[0];
            if nb & ((PAGE_SIZE - 1) as u64) != 0
                || kb & ((PAGE_SIZE - 1) as u64) != 0
                || ns == 0
                || ks == 0
            {
                return Err(ExtractError::new("unaligned or empty memory map"));
            }
            let aligned = nb & !(ARM64_MEMSTART_ALIGN - 1);
            if !(aligned <= nb && nb < aligned + ARM64_MEMSTART_ALIGN) {
                return Err(ExtractError::new("invalid NOMAP phys offset"));
            }
            Ok((nb, ns, kb, ks))
        })();
        if let Ok((nb, ns, kb, ks)) = parse_result {
            candidates.push((nb, ns, kb, ks));
        }
    }
    if candidates.is_empty() {
        return Err(ExtractError::new(
            "xbl_config contains no unique NOMAP/Kernel memory map",
        ));
    }
    candidates.sort();
    candidates.dedup();
    if candidates.len() != 1 {
        return Err(ExtractError::new(format!(
            "xbl_config contains conflicting memory maps: {candidates:?}"
        )));
    }
    Ok(candidates[0].2)
}

struct FdtNode {
    path: String,
    parent_ac: u32,
    parent_sc: u32,
    child_ac: u32,
    child_sc: u32,
    props: std::collections::HashMap<String, Vec<u8>>,
}

fn find_fdt_magic(data: &[u8], from: usize) -> Option<usize> {
    let magic = FDT_MAGIC.to_be_bytes();
    if data.len() < magic.len() {
        return None;
    }
    let from = from.min(data.len() - magic.len());
    (from..=data.len() - magic.len()).find(|&i| &data[i..i + 4] == magic)
}

fn find_byte(data: &[u8], from: usize, to: usize) -> Option<usize> {
    data[from.min(data.len())..to.min(data.len())]
        .iter()
        .position(|b| *b == 0)
        .map(|i| from.min(data.len()) + i)
}
