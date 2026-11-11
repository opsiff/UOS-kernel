// SPDX-License-Identifier: GPL-2.0

//! Page table check: reject illegal page table entries at map/unmap time.
//!
//! C counterpart: [`mm/page_table_check.c`](srctree/mm/page_table_check.c).
//! Counters live in a payload private to that file and are reached only through
//! the accessors declared in `include/linux/page_table_check.h`.
#![cfg(CONFIG_PAGE_TABLE_CHECK)]

use crate::bindings;
use crate::page::folio::{BorrowedFolio, FolioKind};
use core::ptr::NonNull;

/// Classifies `page`, panicking on a slab page like the C `BUG_ON(PageSlab())`.
///
/// # Safety
///
/// `page` must be valid and its type must not change concurrently.
unsafe fn anon_or_bug(page: *mut bindings::page) -> bool {
    // SAFETY: caller guarantees `page` is valid for this scope.
    let folio = unsafe { BorrowedFolio::from_page(NonNull::new_unchecked(page)) };
    // SAFETY: forwarded from the caller.
    match unsafe { folio.kind() } {
        FolioKind::Slab => panic!("page_table_check: slab page in page table"),
        FolioKind::Anon => true,
        _ => false,
    }
}

/// Applies `f` to the page_ext payload of `pgcnt` consecutive pages from `page`.
///
/// C counterpart: `for_each_page_ext()` (mm/page_table_check.c:78, :113, :139),
/// which walks the pfn range starting at `page`.
///
/// # Safety
///
/// `page..page + pgcnt` must be a valid range of the vmemmap and page_ext must
/// be initialized.
unsafe fn for_each_ptc<F: FnMut(*mut core::ffi::c_void)>(
    page: *mut bindings::page,
    pgcnt: usize,
    mut f: F,
) {
    for i in 0..pgcnt {
        // SAFETY: the range was validated by the caller.
        let p = unsafe { page.add(i) };
        // SAFETY: valid page, page_ext initialized.
        let ext = unsafe { bindings::page_ext_get(p) };
        if ext.is_null() {
            continue;
        }
        // SAFETY: `ext` is live.
        let data = unsafe { bindings::ptc_data(ext) };
        f(data);
        // SAFETY: paired with page_ext_get().
        unsafe { bindings::page_ext_put(ext) };
    }
}

/// Verifies entries are being removed.
///
/// C counterpart: `page_table_check_clear()` (mm/page_table_check.c:65).
///
/// # Safety
///
/// `pfn`/`pgcnt` describe a page table range being unmapped.
pub unsafe fn clear(pfn: usize, pgcnt: usize) {
    if !unsafe { bindings::pfn_valid(pfn) } {
        return;
    }
    let page = unsafe { bindings::pfn_to_page(pfn) };
    // SAFETY: `page` came from a valid pfn.
    let anon = unsafe { anon_or_bug(page) };

    // SAFETY: forwarded from the caller.
    unsafe {
        for_each_ptc(page, pgcnt, |data| {
            if anon {
                if bindings::ptc_file_read(data) != 0 {
                    panic!("page_table_check: anon page has file mappings");
                }
                if bindings::ptc_anon_dec(data) < 0 {
                    panic!("page_table_check: anon map count underflow");
                }
            } else {
                if bindings::ptc_anon_read(data) != 0 {
                    panic!("page_table_check: file page has anon mappings");
                }
                if bindings::ptc_file_dec(data) < 0 {
                    panic!("page_table_check: file map count underflow");
                }
            }
        })
    };
}

/// Verifies entries are being installed.
///
/// C counterpart: `page_table_check_set()` (mm/page_table_check.c:99).
///
/// # Safety
///
/// Same requirements as [`clear`].
pub unsafe fn set(pfn: usize, pgcnt: usize, rw: bool) {
    if !unsafe { bindings::pfn_valid(pfn) } {
        return;
    }
    let page = unsafe { bindings::pfn_to_page(pfn) };
    // SAFETY: `page` came from a valid pfn.
    let anon = unsafe { anon_or_bug(page) };

    // SAFETY: forwarded from the caller.
    unsafe {
        for_each_ptc(page, pgcnt, |data| {
            if anon {
                if bindings::ptc_file_read(data) != 0 {
                    panic!("page_table_check: anon page has file mappings");
                }
                if bindings::ptc_anon_inc(data) > 1 && rw {
                    panic!("page_table_check: anon page mapped writable twice");
                }
            } else {
                if bindings::ptc_anon_read(data) != 0 {
                    panic!("page_table_check: file page has anon mappings");
                }
                if bindings::ptc_file_inc(data) < 0 {
                    panic!("page_table_check: file map count overflow");
                }
            }
        })
    };
}

/// Verifies counters are zero before allocation/free.
///
/// C counterpart: `__page_table_check_zero()` (mm/page_table_check.c:131).
///
/// # Safety
///
/// `page` must be valid and about to be (re)allocated or freed.
pub unsafe fn zero(page: *mut bindings::page, order: u32) {
    // SAFETY: forwarded from the caller.
    let _ = unsafe { anon_or_bug(page) };
    // The C hook checks every page of the block: `1 << order` in
    // __page_table_check_zero() (mm/page_table_check.c:139). Checking only the
    // head page leaves a stale count on a tail page undetected.
    // SAFETY: forwarded from the caller; `page` heads `1 << order` pages.
    unsafe {
        for_each_ptc(page, 1usize << order, |data| {
            // SAFETY: live payload, atomic reads.
            if bindings::ptc_anon_read(data) != 0 || bindings::ptc_file_read(data) != 0 {
                panic!("page_table_check: counters not zero on alloc/free");
            }
        })
    };
}

/// C hook replacement for `page_table_check_pte_clear()` (single entry).
#[no_mangle]
pub extern "C" fn __page_table_check_pte_clear_rs(
    mm: *mut bindings::mm_struct,
    addr: usize,
    pte: bindings::pte_t,
) {
    // SAFETY: called from the C hook with the pte lock held.
    //
    // The two guards that C applies before calling page_table_check_clear()
    // (mm/page_table_check.c:150-156) are mirrored by the helper below:
    //   if (&init_mm == mm) return;
    //   if (pte_user_accessible_page(mm, addr, pte) && !pte_special(pte))
    // Skipping them makes every clear() call account a page that was never
    // accounted for, which underflows file_map_count and trips the BUG() in
    // page_table_check_clear() at boot.
    let should_check = unsafe {
        bindings::page_table_check_should_check(mm, addr, pte)
    };
    if should_check {
        // SAFETY: the guards above passed, so this pte was accounted by set().
        unsafe { clear(bindings::pte_pfn(pte) as usize, 1) };
    }
}

/// C hook replacement for `page_table_check_set()` (batch, keeps `pgcnt`).
///
/// `pgcnt` is `unsigned int` in include/linux/page_table_check.h:31, so the
/// extern signature must be 32-bit: on a big-endian 64-bit target the upper half
/// of the register is otherwise undefined and `for_each_ptc()` walks off the
/// block.
#[no_mangle]
pub extern "C" fn __page_table_check_set_rs(pfn: usize, pgcnt: u32, rw: bool) {
    // SAFETY: called from the C hook after it validated the pte range.
    unsafe { set(pfn, pgcnt as usize, rw) };
}