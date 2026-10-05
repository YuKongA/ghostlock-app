//! usage: btf_field <boot.img> <struct> [field...]
//! Prints the real field offset/size from the image's embedded BTF. BTF is
//! generated after randstruct runs, so for __randomize_layout structs such as
//! selinux_state this is the only correct source for field offsets.

use std::path::Path;

use ghostlock_extract::boot::BootImage;
use ghostlock_extract::btf::Btf;

fn main() {
    let mut args = std::env::args().skip(1);
    let boot_path = match args.next() {
        Some(p) => p,
        None => {
            eprintln!("usage: btf_field <boot.img> <struct> [field...]");
            std::process::exit(2);
        }
    };
    let boot = BootImage::load(Path::new(&boot_path)).expect("load boot.img");
    let raw = boot.embedded_btf().expect("no embedded BTF in image");
    let btf = Btf::new(&raw).expect("parse BTF");
    println!("BTF loaded: {} bytes", raw.len());

    let mut want: Vec<(String, String)> = Vec::new();
    for a in args {
        if a.contains('.') {
            let mut it = a.splitn(2, '.');
            let s = it.next().unwrap().to_string();
            let f = it.next().unwrap().to_string();
            want.push((s, f));
        } else {
            println!("\nstruct {} size = {:?}", a, btf.size(&a));
        }
    }

    for (s, f) in &want {
        match btf.field(s, f) {
            Some(off) => println!("{}.{} = {} (0x{:x})", s, f, off, off),
            None => println!("{}.{} = NOT FOUND", s, f),
        }
    }
}
