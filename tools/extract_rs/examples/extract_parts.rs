//! Temporary helper: extract arbitrary partitions from a local payload.bin.
//!
//! usage: cargo run --release --example extract_parts -- <payload.bin> <outdir> <part>...

use std::path::Path;

use ghostlock_extract::payload;

fn main() {
    let mut args = std::env::args().skip(1);
    let (path, outdir) = match (args.next(), args.next()) {
        (Some(p), Some(o)) => (p, o),
        _ => {
            eprintln!("usage: extract_parts <payload.bin> <outdir> <part>...");
            std::process::exit(2);
        }
    };
    let parts: Vec<String> = args.collect();
    let out = Path::new(&outdir);
    let view = payload::open_payload_for(&path, out, &parts, None).expect("open payload");
    let produced = payload::extract_partitions(&view, out, &parts).expect("extract");
    for p in produced {
        println!("{}", p.display());
    }
}
