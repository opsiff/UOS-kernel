// SPDX-License-Identifier: GPL-2.0
/*
 * sm3_zhaoxin_gmi.c - wrapper code for Zhaoxin GMI.
 *
 * Copyright (C) 2023 Shanghai Zhaoxin Semiconductor LTD.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <crypto/internal/hash.h>
#include <crypto/sm3.h>
#include <crypto/sm3_base.h>
#include <linux/cpufeature.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/cpu_device_id.h>

#define GMI_SM3_CRA_PRIORITY 400

static void sm3_generic_block_fn(struct sm3_state *sst, const u8 *inp, int blockcnt)
{
	unsigned int cnt, ctrl = 0x20;
	long padding = -1;
	unsigned char *out = (unsigned char *)sst->state;

	if (blockcnt < 0)
		return;

	cnt = (unsigned int)blockcnt;

	__asm__ __volatile__(
		".byte 0xf3, 0x0f, 0xa6, 0xe8\n"
		: "+S"(inp), "+D"(out), "+c"(cnt)
		: "b"(ctrl), "a"(padding)
		: "memory");
}

static inline int zx_sm3_init(struct shash_desc *desc)
{
	struct sm3_state *sctx;

	if (!desc)
		return -EINVAL;

	sctx = shash_desc_ctx(desc);

	sctx->state[0] = 0x6f168073UL;
	sctx->state[1] = 0xb9b21449UL;
	sctx->state[2] = 0xd7422417UL;
	sctx->state[3] = 0x00068adaUL;
	sctx->state[4] = 0xbc306fa9UL;
	sctx->state[5] = 0xaa383116UL;
	sctx->state[6] = 0x4dee8de3UL;
	sctx->state[7] = 0x4e0efbb0UL;

	sctx->count = 0;

	return 0;
}

static int zx_sm3_update(struct shash_desc *desc, const u8 *data, unsigned int len)
{
	return sm3_base_do_update_blocks(desc, data, len, sm3_generic_block_fn);
}

static int zx_sm3_finup(struct shash_desc *desc, const u8 *data, unsigned int len, u8 *out)
{
	sm3_base_do_finup(desc, data, len, sm3_generic_block_fn);

	return sm3_base_finish(desc, out);
}

static struct shash_alg zx_sm3_alg = {
	.digestsize = SM3_DIGEST_SIZE,
	.init = zx_sm3_init,
	.update = zx_sm3_update,
	.finup = zx_sm3_finup,
	.descsize = SM3_STATE_SIZE,
	.base = {
		.cra_name = "sm3",
		.cra_driver_name = "sm3-zhaoxin-gmi",
		.cra_priority = GMI_SM3_CRA_PRIORITY,
		.cra_flags = CRYPTO_AHASH_ALG_BLOCK_ONLY |
			     CRYPTO_AHASH_ALG_FINUP_MAX,
		.cra_blocksize = SM3_BLOCK_SIZE,
		.cra_module = THIS_MODULE,
	}
};

static const struct x86_cpu_id zhaoxin_ccs_cpu_ids[] = {
	X86_MATCH_VENDOR_FAM_FEATURE(ZHAOXIN, 7, X86_FEATURE_CCS, NULL),
	X86_MATCH_VENDOR_FAM_FEATURE(CENTAUR, 7, X86_FEATURE_CCS, NULL),
	{}
};
MODULE_DEVICE_TABLE(x86cpu, zhaoxin_ccs_cpu_ids);

static int __init zx_sm3_generic_mod_init(void)
{
	if (!x86_match_cpu(zhaoxin_ccs_cpu_ids) || !boot_cpu_has(X86_FEATURE_CCS_EN))
		return -ENODEV;

	return crypto_register_shash(&zx_sm3_alg);
}

static void __exit zx_sm3_generic_mod_fini(void)
{
	crypto_unregister_shash(&zx_sm3_alg);
}

module_init(zx_sm3_generic_mod_init);
module_exit(zx_sm3_generic_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SM3 Secure Hash Algorithm");

MODULE_ALIAS_CRYPTO("sm3-zhaoxin");
MODULE_ALIAS_CRYPTO("sm3-zhaoxin-gmi");
MODULE_VERSION("2.0.1");
