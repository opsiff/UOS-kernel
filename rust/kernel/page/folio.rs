// SPDX-License-Identifier: GPL-2.0

//! Typed views over `struct page` / `struct folio`.
//!
//! C header: [`include/linux/mm_types.h`](srctree/include/linux/mm_types.h)
//! C header: [`include/linux/page-flags.h`](srctree/include/linux/page-flags.h)
//!
//! A `struct page` is one 64-byte vmemmap entry shared by several unrelated
//! uses; the C side keeps them apart by convention. This module turns those
//! conventions into explicit types.
//!
//! Tail pages are folded to their head in [`BorrowedFolio::from_page`] via
//! `page_folio()`; every API below works on a folio head.

use crate::bindings;
use crate::page::{BorrowedPage, Page};
use core::ptr::NonNull;

/// The kind of a folio, classified from the C side.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum FolioKind {
    /// Anonymous memory.
    Anon,
    /// Page cache.
    PageCache,
    /// Slab (SLUB) page: `PGTY_slab`.
    Slab,
    /// Buddy allocator page: `PGTY_buddy`.
    Buddy,
    /// Page table page: `PGTY_table`.
    Table,
    /// HugeTLB page: `PGTY_hugetlb`.
    Hugetlb,
    /// Page is going offline: `PGTY_offline`.
    Offline,
    /// Any other type byte, kept verbatim.
    Other(u32),
}

/// A borrowed folio head.
///
/// Carries the same lifetime guarantee as [`BorrowedPage`]: the underlying
/// `struct page` stays valid for `'a`.
pub struct BorrowedFolio<'a> {
    page: BorrowedPage<'a>,
}

impl<'a> BorrowedFolio<'a> {
    /// Folds `page` to its head and borrows the resulting folio.
    ///
    /// # Safety
    ///
    /// - `page` must point to a valid `bindings::page` and stay valid for `'a`.
    /// - If no reference is held, the caller must re-validate the folio after
    ///   taking one, as required by the `page_folio()` contract
    ///   (include/linux/mm.h).
    pub unsafe fn from_page(page: NonNull<bindings::page>) -> Self {
        // SAFETY: by the contract above, `page` is valid.
        let head = unsafe { bindings::page_folio(page.as_ptr()) };
        // A non-NULL page input always yields a non-NULL head; a NULL result
        // means the contract was violated, so fail loudly.
        let head = NonNull::new(head).expect("page_folio() returned NULL");
        // SAFETY: `head` is derived from a page valid for `'a`.
        let page = unsafe { BorrowedPage::from_raw(head) };
        Self { page }
    }

    /// Returns the raw head page pointer.
    pub fn as_ptr(&self) -> *mut bindings::page {
        self.page.as_ptr()
    }

    /// Returns the borrowed underlying page.
    pub fn as_page(&self) -> &Page {
        &self.page
    }

    /// Classifies the folio head.
    ///
    /// # Safety
    ///
    /// The page type must not change concurrently: hold the lock protecting
    /// the transition (zone lock, slab lock) or a reference pinning the folio.
    pub unsafe fn kind(&self) -> FolioKind {
        let ptr = self.as_ptr();
        // The type is the top byte of `page_type` (page-flags.h:935,943);
        // `page_has_type()` (page-flags.h:927) also folds the mapcount
        // overflow case, so never decode the field here.
        // SAFETY: `self` holds a valid head page.
        if !unsafe { bindings::page_has_type(ptr) } {
            // SAFETY: same memory viewed as a folio; head page only.
            return if unsafe { bindings::folio_test_anon(ptr) } {
                FolioKind::Anon
            } else {
                FolioKind::PageCache
            };
        }

        // SAFETY: typed folio; decoding done by the C helper.
        match unsafe { bindings::page_type_high(ptr) } {
            bindings::pagetype_PGTY_slab => FolioKind::Slab,
            bindings::pagetype_PGTY_buddy => FolioKind::Buddy,
            bindings::pagetype_PGTY_table => FolioKind::Table,
            bindings::pagetype_PGTY_hugetlb => FolioKind::Hugetlb,
            bindings::pagetype_PGTY_offline => FolioKind::Offline,
            other => FolioKind::Other(other),
        }
    }

    /// Returns the page frame number of the head page.
    pub fn pfn(&self) -> u64 {
        // SAFETY: valid head page.
        unsafe { bindings::page_to_pfn(self.as_ptr()) as u64 }
    }
}

impl core::fmt::Debug for BorrowedFolio<'_> {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        f.debug_struct("BorrowedFolio")
            .field("pfn", &self.pfn())
            .finish()
    }
}

// Values branched on come from bindgen, never from a hand-copied table.
const _: () = {
    assert!(bindings::pagetype_PGTY_slab != bindings::pagetype_PGTY_buddy);
    assert!(bindings::pagetype_PGTY_buddy != bindings::pagetype_PGTY_table);
};