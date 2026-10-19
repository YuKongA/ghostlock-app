//! Temporary helper: list every partition name in a payload.bin (metadata only).
//!
//! usage: cargo run --release --example list_partitions -- <payload.bin>

use ghostlock_extract::payload;

fn main() {
    let path = std::env::args().nth(1).unwrap_or_else(|| {
        eprintln!("usage: list_partitions <payload.bin>");
        std::process::exit(2);
    });
    let meta = payload::open_payload_meta(&path).expect("payload metadata");
    for part in meta.partitions() {
        println!("{}", part.partition_name);
    }
}
