/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Interface between msdos_rust_shim.c and the Rust MS-DOS parser.
 * Temporary scaffolding: both this header and the shim disappear once the
 * parser owns the partition state (see msdos_rust.rs).
 */
#ifndef _BLOCK_PARTITIONS_MSDOS_RUST_H
#define _BLOCK_PARTITIONS_MSDOS_RUST_H

#include <linux/types.h>

/* Mirrors struct Entry in msdos_rust.rs; keep the field order identical. */
struct rust_part {
	int number;
	u64 from;
	u64 size;
	int flags;
	u32 disksig;
	bool has_info;
	u8 kind; /* 0 primary, 1 extended head, 2 logical */
	u8 tag;  /* 1 [DM], 2 [EZD] */
};

int rust_msdos_parse(void *ctx, bool (*read)(void *, u64, u8 *), u64 scale,
		     int limit, struct rust_part *out, int cap, bool efi,
		     size_t *out_len);

#endif /* _BLOCK_PARTITIONS_MSDOS_RUST_H */
