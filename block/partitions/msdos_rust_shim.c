// SPDX-License-Identifier: GPL-2.0
//
// Glue between block/partitions/core.c and the Rust MS-DOS parser.
//
// The parser itself lives in msdos_rust.rs; this file only adapts the kernel's
// sector cache and parsed_partitions state to the parser's callback interface.
// It is transitional scaffolding: once the parser owns the state type, this
// file and msdos.c both go away.
#include <linux/kconfig.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "check.h"
#include "msdos_rust.h"

/*
 * The parser only needs 512-byte sectors, so copy out of the folio instead of
 * handing out a pointer that could outlive the sector.
 */
static bool msdos_rust_read(void *ctx, u64 sector, u8 *out)
{
	struct parsed_partitions *state = ctx;
	Sector sect;
	unsigned char *data;

	data = read_part_sector(state, sector, &sect);
	if (!data)
		return false;
	memcpy(out, data, 512);
	put_dev_sector(sect);
	return true;
}

int msdos_partition_rust(struct parsed_partitions *state)
{
	struct rust_part *parts;
	u64 scale;
	size_t n = 0, i;
	int ret, chain_open = 0;

	parts = kcalloc(state->limit, sizeof(*parts), GFP_KERNEL);
	if (!parts)
		return -1;

	scale = queue_logical_block_size(state->disk->queue) / 512;
	ret = rust_msdos_parse(state, msdos_rust_read, scale, state->limit,
			       parts, state->limit, IS_ENABLED(CONFIG_EFI_PARTITION),
			       &n);
	if (ret != 1) {
		kfree(parts);
		/*
		 * -2: AIX label, the AIX parser only exists in C.
		 * -3: entry buffer exhausted, which must never be silently ignored.
		 * Both fall back to the C parser rather than dropping partitions.
		 */
		if (ret == -2 || ret == -3)
			return msdos_partition(state);
		return ret;
	}

	/*
	 * state->next is intentionally not updated: nothing reads it after the
	 * parser returns (core.c); update this if a caller ever does.
	 */

	/*
	 * Reproduce the pp_buf text of msdos.c:661-676 exactly: the extended
	 * partition is followed by " <", its logicals, then " >", and the whole
	 * line ends with a newline. Getting this wrong glues the next printk onto
	 * the partition list.
	 */
	for (i = 0; i < n; i++) {
		struct rust_part *p = &parts[i];
		/* parts[] is sized by limit; the pp_buf markers are not. */
		bool in_range = p->number < state->limit;

		if (p->kind != 2 && chain_open) {
			seq_buf_puts(&state->pp_buf, " >");
			chain_open = 0;
		}
		if (in_range)
			put_partition(state, p->number, p->from, p->size);
		if (p->kind == 1) {
			seq_buf_puts(&state->pp_buf, " <");
			chain_open = 1;
		} else if (p->tag == 1) {
			seq_buf_puts(&state->pp_buf, "[DM]");
		} else if (p->tag == 2) {
			seq_buf_puts(&state->pp_buf, "[EZD]");
		}
		if (!in_range)
			continue;
		state->parts[p->number].flags = p->flags;
		if (p->has_info) {
			struct partition_meta_info *info;

			info = &state->parts[p->number].info;
			snprintf(info->uuid, sizeof(info->uuid), "%08x-%02x",
				 p->disksig, p->number);
			info->volname[0] = 0;
			state->parts[p->number].has_info = true;
		}
	}
	kfree(parts);
	if (chain_open)
		seq_buf_puts(&state->pp_buf, " >");
	seq_buf_puts(&state->pp_buf, "\n");
	return 1;
}
