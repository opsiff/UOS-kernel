// SPDX-License-Identifier: GPL-2.0

#include <linux/gfp.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <asm/pgtable.h>
#include <linux/page_ext.h>
#include <linux/page_table_check.h>

__rust_helper struct page *rust_helper_alloc_pages(gfp_t gfp_mask,
						   unsigned int order)
{
	return alloc_pages(gfp_mask, order);
}

__rust_helper void *rust_helper_kmap_local_page(struct page *page)
{
	return kmap_local_page(page);
}

__rust_helper void rust_helper_kunmap_local(const void *addr)
{
	kunmap_local(addr);
}

#ifndef NODE_NOT_IN_PAGE_FLAGS
__rust_helper int rust_helper_page_to_nid(const struct page *page)
{
	return page_to_nid(page);
}
#endif

__rust_helper struct page *rust_helper_page_folio(struct page *page)
{
	return (struct page *)_compound_head(page);
}

/* The type lives in the top byte of page_type; see page-flags.h:935,943. */
__rust_helper bool rust_helper_page_has_type(const struct page *page)
{
	return page_has_type(page);
}

__rust_helper unsigned int rust_helper_page_type_high(const struct page *page)
{
	return page->page_type >> 24;
}

__rust_helper bool rust_helper_folio_test_anon(const struct page *page)
{
	return folio_test_anon((const struct folio *)page);
}

__rust_helper unsigned long rust_helper_page_to_pfn(const struct page *page)
{
	return page_to_pfn(page);
}


__rust_helper struct page *rust_helper_pfn_to_page(unsigned long pfn)
{
	return pfn_to_page(pfn);
}

__rust_helper bool rust_helper_pfn_valid(unsigned long pfn)
{
	return pfn_valid(pfn);
}

#ifdef CONFIG_PAGE_EXTENSION
__rust_helper struct page_ext *rust_helper_page_ext_get(const struct page *page)
{
	return page_ext_get(page);
}

__rust_helper void rust_helper_page_ext_put(struct page_ext *page_ext)
{
	page_ext_put(page_ext);
}

__rust_helper struct page_ext *rust_helper_page_ext_next(struct page_ext *page_ext)
{
	return page_ext_next(page_ext);
}
#endif



#ifdef CONFIG_PAGE_TABLE_CHECK
__rust_helper void *rust_helper_ptc_data(struct page_ext *page_ext)
{
	return page_ext_data(page_ext, &page_table_check_ops);
}

__rust_helper unsigned int rust_helper_ptc_anon_read(void *data)
{
	return page_table_check_anon_count(data);
}

__rust_helper unsigned int rust_helper_ptc_file_read(void *data)
{
	return page_table_check_file_count(data);
}

__rust_helper int rust_helper_ptc_anon_inc(void *data)
{
	return page_table_check_anon_inc(data);
}

__rust_helper int rust_helper_ptc_file_inc(void *data)
{
	return page_table_check_file_inc(data);
}

__rust_helper int rust_helper_ptc_anon_dec(void *data)
{
	return page_table_check_anon_dec(data);
}

__rust_helper int rust_helper_ptc_file_dec(void *data)
{
	return page_table_check_file_dec(data);
}

/*
 * Both guards from __page_table_check_pte_clear() (mm/page_table_check.c:150-156)
 * must live on the C side: pte_user_accessible_page() needs the arch pte accessors
 * and &init_mm, neither of which is usable from Rust without re-deriving the
 * semantics the C hook already relies on.
 */
__rust_helper bool rust_helper_page_table_check_should_check(struct mm_struct *mm,
							     unsigned long addr,
							     pte_t pte)
{
	if (&init_mm == mm)
		return false;

	return pte_user_accessible_page(mm, addr, pte) && !pte_special(pte);
}
#endif

__rust_helper unsigned long rust_helper_pte_pfn(pte_t pte)
{
	return pte_pfn(pte);
}

__rust_helper bool rust_helper_pte_write(pte_t pte)
{
	return pte_write(pte);
}

