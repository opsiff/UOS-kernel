// SPDX-License-Identifier: GPL-2.0
//! MS-DOS (MBR) partition table parser.
//
// The rules mirror block/partitions/msdos.c one for one; comments here only
// record why a rule exists or where behaviour is subtle. Field offsets come
// from struct msdos_partition (include/linux/msdos_partition.h); the constants
// below sit next to the struct they describe instead of being copied around.

use core::ffi::c_void;

/// Bytes of one logical sector used by the MBR format. Both sides always read
/// 512-byte units; larger logical blocks are handled by the scale factor.
pub const SECTOR_LEN: usize = 512;

const SLOT_BASE: usize = 0x1be;
const SLOT_LEN: usize = 16;
const MAGIC_OFF: usize = 510;
const DISKSIG_OFF: usize = 0x1b8;

// struct msdos_partition field offsets (packed, 16 bytes per slot).
const SLOT_BOOT_IND: usize = 0;
const SLOT_SYS_IND: usize = 4;
const SLOT_START: usize = 8;
const SLOT_NR: usize = 12;

// enum msdos_sys_ind
const DOS_EXTENDED: u8 = 0x05;
const LINUX_EXTENDED: u8 = 0x85;
const WIN98_EXTENDED: u8 = 0x0f;
const LINUX_RAID: u8 = 0xfd;
const DM6_PARTITION: u8 = 0x54;
const EZD_PARTITION: u8 = 0x55;

/// ADDPART_FLAG_RAID from block/blk.h
pub const ADDPART_FLAG_RAID: i32 = 1;

/// Number used for the first logical partition; 1..4 are the primary slots.
pub const FIRST_LOGICAL: i32 = 5;

/// Upper bound on extended-chain walks; C uses the same 100 to stop loops
/// created by self-referencing tables.
const MAX_EXTENDED_LINKS: i32 = 100;

/// One partition as the scanner wants it. The number is the slot index (1..4)
/// or the logical index (5..) and is needed because the caller stores entries
/// at exactly that index.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Entry {
	/// Slot index (1..4) or logical partition index (5..).
	pub number: i32,
	/// First sector of the partition.
	pub from: u64,
	/// Length in sectors.
	pub size: u64,
	/// ADDPART_FLAG_* bits.
	pub flags: i32,
	/// MBR disk signature used to build the partition UUID.
	pub disksig: u32,
	/// Whether disksig/carries meta info worth exporting.
	pub has_info: bool,
	/// 0 = primary, 1 = extended head, 2 = logical. Needed only so the caller can
	/// reproduce the "< >" grouping msdos.c prints into pp_buf.
	pub kind: u8,
	/// 1 = [DM], 2 = [EZD]; printed after the partition name, as msdos.c does.
	pub tag: u8,
}

const KIND_PRIMARY: u8 = 0;
const KIND_EXTENDED: u8 = 1;
const KIND_LOGICAL: u8 = 2;
const TAG_NONE: u8 = 0;
const TAG_DM: u8 = 1;
const TAG_EZD: u8 = 2;

/// Sector source. Implementations may cache; the parser requires the returned
/// slice to be exactly SECTOR_LEN bytes.
pub trait SectorReader {
	/// Returns the 512-byte sector at `sector`, or None on I/O error.
	fn read(&mut self, sector: u64) -> Option<&[u8]>;
}

/// Collects entries in scan order; the caller decides how to store them.
pub struct Writer<'a> {
	buf: &'a mut [Entry],
	len: usize,
	full: bool,
}

impl<'a> Writer<'a> {
	/// Creates a writer over a caller-owned buffer.
	pub fn new(buf: &'a mut [Entry]) -> Self {
		Self { buf, len: 0, full: false }
	}

	/// Number of entries written so far.
	pub fn len(&self) -> usize {
		self.len
	}

	/// True when an entry had to be dropped; the caller must treat the parse as
	/// failed rather than silently losing partitions.
	pub fn is_full(&self) -> bool {
		self.full
	}

	/// Appends one entry, dropping it if the caller's buffer is full.
	fn push(&mut self, e: Entry) {
		if self.len < self.buf.len() {
			self.buf[self.len] = e;
			self.len += 1;
		} else {
			self.full = true;
		}
	}
}

fn le32(s: &[u8], off: usize) -> u32 {
	u32::from_le_bytes([s[off], s[off + 1], s[off + 2], s[off + 3]])
}

fn slot<'a>(s: &'a [u8], i: usize) -> &'a [u8] {
	let off = SLOT_BASE + i * SLOT_LEN;
	&s[off..off + SLOT_LEN]
}

fn boot_ind(e: &[u8]) -> u8 {
	e[SLOT_BOOT_IND]
}

fn sys_ind(e: &[u8]) -> u8 {
	e[SLOT_SYS_IND]
}

fn start_sect(e: &[u8]) -> u64 {
	le32(e, SLOT_START) as u64
}

fn nr_sects(e: &[u8]) -> u64 {
	le32(e, SLOT_NR) as u64
}

fn is_extended(e: &[u8]) -> bool {
	matches!(sys_ind(e), DOS_EXTENDED | LINUX_EXTENDED | WIN98_EXTENDED)
}

fn magic_present(s: &[u8]) -> bool {
	s[MAGIC_OFF] == 0x55 && s[MAGIC_OFF + 1] == 0xaa
}

/// AIX keeps its LVM label in the boot area; msdos.c hands those disks to the
/// AIX parser before the 0xAA55 check, so we simply report "not ours".
fn aix_magic_present(s: &[u8]) -> bool {
	s[0] == 0xc9 && s[1] == 0xc2 && s[2] == 0xd4 && s[3] == 0xc1
}

/// fat_valid_media() from include/linux/msdos_fs.h.
fn fat_valid_media(media: u8) -> bool {
	media >= 0xf8 || media == 0xf0
}

/// A FAT boot sector carries no partition table but does carry 0xAA55.
/// msdos.c accepts it as "recognised, zero partitions" when the first slot's
/// boot indicator is invalid but the BPB still looks like FAT.
fn looks_like_fat(s: &[u8]) -> bool {
	let reserved = u16::from_le_bytes([s[14], s[15]]);
	let fats = s[16];
	let media = s[21];
	reserved != 0 && fats != 0 && fat_valid_media(media)
}

fn set_entry(
	w: &mut Writer<'_>,
	number: i32,
	from: u64,
	size: u64,
	flags: i32,
	disksig: u32,
	has_info: bool,
	kind: u8,
	tag: u8,
) {
	w.push(Entry { number, from, size, flags, disksig, has_info, kind, tag });
}

/// Walks the logical-partition chain hanging off an extended partition.
///
/// Only the first two slots of a logical MBR are meaningful: slot 0 is the data
/// partition and slot 1 links to the next table. Slots 2/3 are used by OS/2 and
/// DR-DOS in ways that produce garbage, hence the range checks that apply only
/// to those two.
fn parse_extended<R: SectorReader>(
	rd: &mut R,
	scale: u64,
	limit: i32,
	first_sector: u64,
	first_size: u64,
	disksig: u32,
	w: &mut Writer<'_>,
	next: &mut i32,
) {
	let mut this_sector = first_sector;
	let mut this_size = first_size;
	let mut loopct = 0i32;

	loop {
		loopct += 1;
		if loopct > MAX_EXTENDED_LINKS || *next == limit {
			return;
		}

		let data = match rd.read(this_sector) {
			Some(d) => d,
			None => return,
		};
		if !magic_present(data) {
			return;
		}

		for i in 0..4 {
			let e = slot(data, i);
			if nr_sects(e) == 0 || is_extended(e) {
				continue;
			}

			let offs = start_sect(e) * scale;
			let size = nr_sects(e) * scale;
			let part_start = this_sector + offs;

			// Slots 2 and 3 sometimes hold stale entries written by another
			// partitioning tool; accept them only when they stay inside the
			// extended partition they claim to belong to.
			if i >= 2 {
				if offs + size > this_size {
					continue;
				}
				if part_start < first_sector {
					continue;
				}
				if part_start + size > first_sector + first_size {
					continue;
				}
			}

			let flags = if sys_ind(e) == LINUX_RAID { ADDPART_FLAG_RAID } else { 0 };
			set_entry(w, *next, part_start, size, flags, disksig, true,
				  KIND_LOGICAL, TAG_NONE);
			loopct = 0;
			*next += 1;
			if *next == limit {
				return;
			}
		}

		// The link to the next table is the first extended slot, if any.
		let mut link = None;
		for i in 0..4 {
			let e = slot(data, i);
			if nr_sects(e) != 0 && is_extended(e) {
				link = Some(e);
				break;
			}
		}
		let e = match link {
			Some(e) => e,
			None => return,
		};

		this_sector = first_sector + start_sect(e) * scale;
		this_size = nr_sects(e) * scale;
	}
}

/// Parses an MBR-style partition table.
///
/// scale is logical_block_size / 512. Returns 1 when the sector is an MBR we
/// own, 0 when it is not (caller tries the next parser), -1 when sector 0 could
/// not be read, mirroring msdos_partition()'s return values.
pub fn parse<R: SectorReader>(
	rd: &mut R,
	scale: u64,
	limit: i32,
	efi_partition: bool,
	w: &mut Writer<'_>,
) -> i32 {
	// Copy sector 0 so the borrow of the reader ends before the extended
	// chain walk, which needs the reader again.
	let mut sec0 = [0u8; SECTOR_LEN];
	match rd.read(0) {
		Some(d) => sec0.copy_from_slice(d),
		None => return -1,
	}
	let data: &[u8] = &sec0;

	// AIX labels are parsed by the AIX parser, which only exists in C for now.
	// Report "defer" so the caller can hand the disk over instead of silently
	// claiming it is not an MBR.
	if aix_magic_present(data) {
		return -2;
	}
	if !magic_present(data) {
		return 0;
	}

	// A valid boot indicator is 0x00 or 0x80. Anything else means either a
	// FAT boot sector or a table we should not touch.
	for i in 0..4 {
		let bi = boot_ind(slot(data, i));
		if bi != 0 && bi != 0x80 {
			return if i == 0 && looks_like_fat(data) { 1 } else { 0 };
		}
	}

	// A protective MBR belongs to GPT; leave it for the EFI parser. When the
	// EFI parser is compiled out there is nothing to hand the disk to, and C
	// keeps scanning the MBR in that case (msdos.c:621-630).
	if efi_partition {
		for i in 0..4 {
			if sys_ind(slot(data, i)) == 0xee {
				return 0;
			}
		}
	}

	let disksig = le32(data, DISKSIG_OFF);

	// Logical partition numbers are shared across every extended chain, exactly
	// like state->next in msdos.c; restarting at 5 per chain would overwrite the
	// earlier chain's entries.
	let mut next = FIRST_LOGICAL;

	for i in 0..4 {
		let e = slot(data, i);
		let start = start_sect(e) * scale;
		let size = nr_sects(e) * scale;
		if size == 0 {
			continue;
		}

		if is_extended(e) {
			// Keep one logical sector (or one block when blocks are larger)
			// so mkfs on the extended partition itself fails instead of
			// scribbling over the chain; LILO still has room there.
			let n = core::cmp::min(size, core::cmp::max(scale, 2));
			set_entry(w, (i + 1) as i32, start, n, 0, 0, false,
				  KIND_EXTENDED, TAG_NONE);
			parse_extended(rd, scale, limit, start, size, disksig, w, &mut next);
			continue;
		}

		let flags = if sys_ind(e) == LINUX_RAID { ADDPART_FLAG_RAID } else { 0 };
		// msdos.c:670-673 prints these tags right after the partition name.
		let tag = match sys_ind(e) {
			DM6_PARTITION => TAG_DM,
			EZD_PARTITION => TAG_EZD,
			_ => TAG_NONE,
		};
		set_entry(w, (i + 1) as i32, start, size, flags, disksig, true, KIND_PRIMARY, tag);
	}

	1
}

// --- FFI glue -------------------------------------------------------------
//
// The kernel keeps its own state type (parsed_partitions) and sector cache, so
// both are reached through callbacks. Keeping this boundary small is what lets
// the parser above be tested byte-for-byte from userspace.

/// Reads one 512-byte sector into `out`; returns false on I/O error.
pub type ReadSectorFn = unsafe extern "C" fn(ctx: *mut c_void, sector: u64, out: *mut u8) -> bool;

struct CReader {
	ctx: *mut c_void,
	read: ReadSectorFn,
	buf: [u8; SECTOR_LEN],
}

impl SectorReader for CReader {
	fn read(&mut self, sector: u64) -> Option<&[u8]> {
		let ok = unsafe { (self.read)(self.ctx, sector, self.buf.as_mut_ptr()) };
		if ok { Some(&self.buf[..]) } else { None }
	}
}

/// Entry point called from the C shim.
///
/// # Safety
/// ctx/read must stay valid for the duration of the call, out must point to
/// cap writable Entry slots, out_len must be a valid pointer.
#[no_mangle]
pub unsafe extern "C" fn rust_msdos_parse(
	ctx: *mut c_void,
	read: Option<ReadSectorFn>,
	scale: u64,
	limit: i32,
	out: *mut Entry,
	cap: i32,
	efi_partition: bool,
	out_len: *mut usize,
) -> i32 {
	let read = match read {
		Some(f) => f,
		None => return 0,
	};
	if out.is_null() || out_len.is_null() || cap <= 0 || limit <= 0 {
		return 0;
	}

	let mut rd = CReader { ctx, read, buf: [0; SECTOR_LEN] };
	// SAFETY: the caller guarantees out points to cap writable entries and
	// out_len is a valid pointer, per the function contract.
	let out_slice = unsafe { core::slice::from_raw_parts_mut(out, cap as usize) };
	let mut w = Writer::new(out_slice);
	let ret = parse(&mut rd, scale, limit, efi_partition, &mut w);
	unsafe { *out_len = w.len() };
	if w.is_full() {
		return -3;
	}
	ret
}
