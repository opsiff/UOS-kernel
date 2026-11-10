// SPDX-License-Identifier: GPL-2.0

//! Kernel-side algorithm implementations.
//!
//! What moves here is *where an algorithm is owned*, not who calls it: this
//! series converts no C call site and changes no exported symbol. The heapsort
//! is implemented here while `lib/sort.c` keeps the C ABI and its exports.
//!
//! | Module | C counterpart | Kind |
//! | --- | --- | --- |
//! | `sort` | `sort_r()` in `lib/sort.c` | Rust owns the algorithm; `kernel::sort` is the C-ABI facade |
//! | [`bsearch`] | `bsearch()` in `lib/bsearch.c` | Rust implementation |
//! | [`gcd`] | `gcd()` in `lib/math/gcd.c` | wrapper over C |
//! | [`hweight`] | `hweightN()` in `include/asm-generic/bitops/const_hweight.h` | intrinsic |
//!
//! Only [`gcd`] wraps C. `sort` is owned here and exposed through a C-ABI
//! facade; [`bsearch`] and [`hweight`] have no callable C counterpart, see their
//! module documentation.
//!
//! # Scope
//!
//! The heapsort keeps the C algorithm's word-at-a-time swap and picks its chunk
//! type the same way `lib/sort.c` does (`#ifdef CONFIG_64BIT`), so 32-bit targets
//! would take the two-32-bit-transfers path rather than being excluded. Rust
//! itself is per-arch 64-bit only today (`arch/x86/Kconfig:302` selects
//! `HAVE_RUST` only `if X86_64`), but nothing here depends on that.
//!
//! # Terminal blocker: export license granularity
//!
//! Rust does export symbols automatically -- `rust/exports.c` emits them from
//! the `rust_exports` section at link time -- but every one of them goes out as
//! `EXPORT_SYMBOL_GPL`, and that is deliberate rather than a missing feature
//! (`rust/exports.c:11`, `rust/macros/lib.rs:257`). The four sort entry points
//! are exported with plain `EXPORT_SYMBOL()` (`lib/sort.c:28`, `:36`, `:43`,
//! `:50`). Turning them GPL-only is a license-visible change that belongs to
//! upstream, not to an implementation move, so the exports stay in C and the
//! `extern "C"` facade in `kernel::sort` stays with them.
//!
//! # Removal path
//!
//! The algorithm is owned by Rust now; `lib/sort.c` stays as the carrier of the
//! C ABI and of the `EXPORT_SYMBOL()`s -- permanently, because the export
//! mechanism available to Rust is GPL-only. Migrating call sites changes who
//! calls, not why that file exists. Each module documents the C code that *can*
//! still be reclaimed (bsearch, hweight, gcd).
//!
//! # Testing
//!
//! The KUnit cases below cover the three helper modules and are gated behind
//! `CONFIG_RUST_ALGO_KUNIT_TEST`. `sort` is covered by the differential harness
//! under `tools/testing/selftests/lib/sort/`, not by KUnit.

pub mod bsearch;
pub mod gcd;
pub mod hweight;

// C reaches this module only through the `extern "C"` symbols in
// `kernel::sort`, never through the Rust API, so it stays crate-private.
pub(crate) mod sort;

#[cfg(CONFIG_RUST_ALGO_KUNIT_TEST)]
#[macros::kunit_tests(rust_kernel_algo)]
mod tests {
    use super::*;

    #[test]
    fn hweight_basic() {
        assert_eq!(hweight::hweight8(0), 0);
        assert_eq!(hweight::hweight8(0xff), 8);
        assert_eq!(hweight::hweight8(0b1011_0001), 4);
        assert_eq!(hweight::hweight16(0xffff), 16);
        assert_eq!(hweight::hweight32(0x8000_0001), 2);
        assert_eq!(hweight::hweight64(u64::MAX), 64);
        assert_eq!(hweight::hweight64(0), 0);
    }

    #[test]
    fn hweight_const_eval() {
        const HS: u32 = hweight::hweight8(0b0101_0101);
        assert_eq!(HS, 4);
        const HG: u32 = hweight::hweight64(0xffff_0000_0000_ffff);
        assert_eq!(HG, 32);
    }

    #[test]
    fn gcd_basic() {
        assert_eq!(gcd::gcd(12, 18), 6);
        assert_eq!(gcd::gcd(18, 12), 6);
        assert_eq!(gcd::gcd(0, 5), 5);
        assert_eq!(gcd::gcd(5, 0), 5);
        assert_eq!(gcd::gcd(0, 0), 0);
        assert_eq!(gcd::gcd(17, 13), 1);
        assert_eq!(gcd::gcd(1 << 40, 1 << 39), 1 << 39);
    }

    #[test]
    fn bsearch_found_and_missing() {
        let haystack = [1u32, 3, 5, 7, 9];
        assert_eq!(bsearch::bsearch_by(&haystack, |v| v.cmp(&5)), Ok(2));
        assert_eq!(bsearch::bsearch_by(&haystack, |v| v.cmp(&1)), Ok(0));
        assert_eq!(bsearch::bsearch_by(&haystack, |v| v.cmp(&4)), Err(2));
        assert_eq!(bsearch::bsearch_by(&haystack, |v| v.cmp(&0)), Err(0));
        assert_eq!(bsearch::bsearch_by(&haystack, |v| v.cmp(&10)), Err(5));

        let empty: [u32; 0] = [];
        assert_eq!(bsearch::bsearch_by(&empty, |v| v.cmp(&1)), Err(0));
    }

    #[test]
    fn bsearch_by_key() {
        struct Item {
            key: u32,
            _payload: u32,
        }

        let haystack = [
            Item { key: 2, _payload: 0 },
            Item { key: 4, _payload: 0 },
            Item { key: 8, _payload: 0 },
        ];
        let cmp = |item: &Item, key: &u32| item.key.cmp(key);
        assert_eq!(bsearch::bsearch_by_key(&haystack, &4, cmp), Ok(1));
        assert_eq!(bsearch::bsearch_by_key(&haystack, &5, cmp), Err(2));
    }
}
