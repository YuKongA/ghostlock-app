//! Temporary reverse-engineering helper: disassemble named kernel symbols from
//! a boot.img / raw Image so the multicast frame geometry can be derived by
//! hand for an unverified release.
//!
//! usage: cargo run --release --example disasm_func -- <boot.img> <sym>...

use std::path::Path;

use ghostlock_extract::boot::BootImage;
use ghostlock_extract::derive::{relative_symbols, unique_offset_optional};
use ghostlock_extract::disasm::disassemble_range;
use ghostlock_extract::kallsyms;
use ghostlock_extract::kallsyms_finder;

fn main() {
    let mut args = std::env::args().skip(1);
    let boot_path = args.next().unwrap_or_else(|| {
        eprintln!("usage: disasm_func <boot.img> <sym>...");
        std::process::exit(2);
    });
    let names: Vec<String> = args.collect();
    let boot = BootImage::load(Path::new(&boot_path)).expect("load boot image");
    let btf_at = boot.embedded_btf_at();
    let btf_pair = btf_at.as_ref().map(|(offset, blob)| (*offset, blob.len()));
    let ks = kallsyms_finder::recover(&boot.kernel, btf_pair).expect("kallsyms recovery");
    let base = kallsyms::unique(&ks.symbols, "_text")
        .or_else(|| kallsyms::unique(&ks.symbols, "_head"))
        .expect("_text/_head");
    let (rel, sorted) = relative_symbols(&ks.symbols, base);
    for name in &names {
        match unique_offset_optional(&rel, name) {
            Some(off) => {
                let start = off as usize;
                let stop = sorted
                    .iter()
                    .find(|o| **o as usize > start)
                    .map(|o| (*o as usize).min(start + 0x3000))
                    .unwrap_or(start + 0x3000);
                println!("=== {name} @ +0x{off:x} ===");
                match disassemble_range(&boot.kernel, start, stop) {
                    Ok(lines) => {
                        for (i, line) in lines.iter().enumerate() {
                            println!("{:04x}: {line}", i * 4);
                        }
                    }
                    Err(err) => println!("disasm error: {err}"),
                }
            }
            None => println!("=== {name}: NOT FOUND ==="),
        }
    }
}
