// Userspace harness: parses an image file with the same core the kernel uses.
//
// Build: rustc --edition 2021 -O harness.rs -o harness
use std::env;
use std::fs;

// `include!` would reject the module-level `//!` docs (E0753); a path module
// keeps the file byte-identical to the in-kernel one.
#[path = "../block/partitions/msdos_rust.rs"]
mod msdos_rust;

use msdos_rust::{parse, Entry, SectorReader, Writer, SECTOR_LEN};

struct VecReader {
	data: Vec<u8>,
}

impl SectorReader for VecReader {
	fn read(&mut self, sector: u64) -> Option<&[u8]> {
		let off = (sector as usize).checked_mul(SECTOR_LEN)?;
		let end = off.checked_add(SECTOR_LEN)?;
		if end > self.data.len() {
			return None;
		}
		Some(&self.data[off..end])
	}
}

fn main() {
	let args: Vec<String> = env::args().collect();
	if args.len() < 2 {
		eprintln!("usage: harness <image> [scale] [limit] [efi]");
		std::process::exit(2);
	}
	let scale: u64 = args.get(2).map(|s| s.parse().unwrap()).unwrap_or(1);
	let limit: i32 = args.get(3).map(|s| s.parse().unwrap()).unwrap_or(256);
	let efi: bool = args.get(4).map(|s| s != "0").unwrap_or(true);
	let data = fs::read(&args[1]).expect("read image");
	let mut rd = VecReader { data };
	let mut buf = [Entry {
		number: 0,
		from: 0,
		size: 0,
		flags: 0,
		disksig: 0,
		has_info: false,
		kind: 0,
		tag: 0,
	}; 256];
	let mut w = Writer::new(&mut buf);
	let ret = parse(&mut rd, scale, limit, efi, &mut w);
	println!("ret {}", ret);
	println!("full {}", w.is_full());
	for i in 0..w.len() {
		let e = buf[i];
		println!("p{} {} {} {}", e.number, e.from, e.size, e.flags);
	}
}
