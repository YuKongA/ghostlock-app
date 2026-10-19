//! Temporary helper: print kernel symbols at/after a given symbol so the
//! forged-object placement (fake_*) can be chosen from the BSS gap.
//!
//! usage: cargo run --release --example symbols_near -- <boot.img> <symbol> [count]

use std::path::Path;

use ghostlock_extract::boot::BootImage;
use ghostlock_extract::derive::relative_symbols;
use ghostlock_extract::kallsyms;
use ghostlock_extract::kallsyms_finder;

fn main() {
    let mut args = std::env::args().skip(1);
    let boot_path = args.next().unwrap_or_default();
    let symbol = args.next().unwrap_or_default();
    let count: usize = args.next().and_then(|v| v.parse().ok()).unwrap_or(12);
    let boot = BootImage::load(Path::new(&boot_path)).expect("load boot");
    let btf_at = boot.embedded_btf_at();
    let pair = btf_at.as_ref().map(|(o, b)| (*o, b.len()));
    let ks = kallsyms_finder::recover(&boot.kernel, pair).expect("kallsyms");
    let base = kallsyms::unique(&ks.symbols, "_text")
        .or_else(|| kallsyms::unique(&ks.symbols, "_head"))
        .expect("base");
    let (rel, _) = relative_symbols(&ks.symbols, base);
    let target = rel
        .get(&symbol)
        .and_then(|s| s.iter().next().copied())
        .expect("symbol not found in kallsyms");
    let mut rows: Vec<(u64, String)> = Vec::new();
    for (name, offsets) in &rel {
        for off in offsets {
            if *off >= target {
                rows.push((*off, name.clone()));
            }
        }
    }
    rows.sort();
    rows.dedup_by(|a, b| a.0 == b.0);
    println!("{symbol} @ +0x{target:x}");
    for (off, name) in rows.into_iter().take(count) {
        println!("  +0x{off:x}  (+0x{:x})  {name}", off - target);
    }
}
