//! usage: symbols_grep <boot.img> <substring> [limit]
//!
//! Lists every kernel symbol whose name contains <substring>, with its offset
//! from the image base. Use it to find symbols that target.h/offsets.h left as
//! 0 ("absent") but that the device kernel actually has, e.g. ashmem_misc.
//! The kallsyms table is recovered from the image itself, so no root is needed.

use std::path::Path;

use ghostlock_extract::boot::BootImage;
use ghostlock_extract::derive::relative_symbols;
use ghostlock_extract::kallsyms;
use ghostlock_extract::kallsyms_finder;

fn main() {
    let mut args = std::env::args().skip(1);
    let boot_path = args.next().unwrap_or_default();
    let needle = args.next().unwrap_or_default();
    let limit: usize = args.next().and_then(|v| v.parse().ok()).unwrap_or(64);
    if boot_path.is_empty() || needle.is_empty() {
        eprintln!("usage: symbols_grep <boot.img> <substring> [limit]");
        std::process::exit(2);
    }

    let boot = BootImage::load(Path::new(&boot_path)).expect("load boot");
    let btf_at = boot.embedded_btf_at();
    let pair = btf_at.as_ref().map(|(o, b)| (*o, b.len()));
    let ks = kallsyms_finder::recover(&boot.kernel, pair).expect("kallsyms");
    let base = kallsyms::unique(&ks.symbols, "_text")
        .or_else(|| kallsyms::unique(&ks.symbols, "_head"))
        .expect("base");
    let (rel, _) = relative_symbols(&ks.symbols, base);

    let mut rows: Vec<(u64, String)> = Vec::new();
    for (name, offsets) in &rel {
        if name.contains(&needle) {
            for off in offsets {
                rows.push((*off, name.clone()));
            }
        }
    }
    rows.sort();
    rows.dedup();

    println!("_text base offset = 0x{base:x}");
    println!(
        "{} match(es) for {:?} (showing {})",
        rows.len(),
        needle,
        limit
    );
    for (off, name) in rows.into_iter().take(limit) {
        println!("  +0x{off:x}  {name}");
    }
}
