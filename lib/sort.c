// SPDX-License-Identifier: GPL-2.0
/*
 * Reverse-replacement shim: the heapsort implementation now lives in
 * rust/kernel/sort.rs.  This file only keeps the C ABI and the exports so the
 * rest of the tree stays untouched; delete it once every consumer calls the
 * Rust API directly.
 *
 * sort_r() heapsorts @num elements of @size bytes at @base.  Comparison is
 * @cmp_func(base + i, base + j, @priv); @swap_func, when non-NULL, replaces the
 * default memory copy.  sort_r_nonatomic() additionally calls cond_resched()
 * while sorting.  See lib/sort.c history for the original documentation.
 */

#include <linux/types.h>
#include <linux/export.h>
#include <linux/sort.h>

void sort_r_rs(void *base, size_t num, size_t size,
	       cmp_r_func_t cmp_func, swap_r_func_t swap_func,
	       const void *priv);
void sort_r_nonatomic_rs(void *base, size_t num, size_t size,
			 cmp_r_func_t cmp_func, swap_r_func_t swap_func,
			 const void *priv);
void sort_rs(void *base, size_t num, size_t size,
	     cmp_func_t cmp_func, swap_func_t swap_func);
void sort_nonatomic_rs(void *base, size_t num, size_t size,
		       cmp_func_t cmp_func, swap_func_t swap_func);

void sort_r(void *base, size_t num, size_t size,
	    cmp_r_func_t cmp_func, swap_r_func_t swap_func, const void *priv)
{
	sort_r_rs(base, num, size, cmp_func, swap_func, priv);
}
EXPORT_SYMBOL(sort_r);

void sort_r_nonatomic(void *base, size_t num, size_t size,
		      cmp_r_func_t cmp_func, swap_r_func_t swap_func,
		      const void *priv)
{
	sort_r_nonatomic_rs(base, num, size, cmp_func, swap_func, priv);
}
EXPORT_SYMBOL(sort_r_nonatomic);

void sort(void *base, size_t num, size_t size,
	  cmp_func_t cmp_func, swap_func_t swap_func)
{
	sort_rs(base, num, size, cmp_func, swap_func);
}
EXPORT_SYMBOL(sort);

void sort_nonatomic(void *base, size_t num, size_t size,
		    cmp_func_t cmp_func, swap_func_t swap_func)
{
	sort_nonatomic_rs(base, num, size, cmp_func, swap_func);
}
EXPORT_SYMBOL(sort_nonatomic);