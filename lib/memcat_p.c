// SPDX-License-Identifier: GPL-2.0

#include <linux/slab.h>

/*
 * Merge two NULL-terminated pointer arrays into a newly allocated
 * array, which is also NULL-terminated. Nomenclature is inspired by
 * memset_p() and memcat() found elsewhere in the kernel source tree.
 */
#ifdef CONFIG_RUST
void **__memcat_p_rs(void **a, void **b);

void **__memcat_p(void **a, void **b)
{
	return __memcat_p_rs(a, b);
}
EXPORT_SYMBOL_GPL(__memcat_p);
#else /* !CONFIG_RUST */
void **__memcat_p(void **a, void **b)
{
	void **p = a, **new;
	int nr;

	/* count the elements in both arrays */
	for (nr = 0, p = a; *p; nr++, p++)
		;
	for (p = b; *p; nr++, p++)
		;
	/* one for the NULL-terminator */
	nr++;

	new = kmalloc_array(nr, sizeof(void *), GFP_KERNEL);
	if (!new)
		return NULL;

	/* nr -> last index; p points to NULL in b[] */
	for (nr--; nr >= 0; nr--, p = p == b ? &a[nr] : p - 1)
		new[nr] = *p;

	return new;
}
EXPORT_SYMBOL_GPL(__memcat_p);
#endif /* CONFIG_RUST */
