// SPDX-License-Identifier: GPL-2.0

//! Small, allocation-free algorithm helpers.
//!
//! Counterparts of the generic algorithms in `lib/` and `lib/math/`:
//!
//! | Module | C counterpart | Kind |
//! | --- | --- | --- |
//! | [`sort`] | `sort_r()` in `lib/sort.c` | wrapper |
//! | [`gcd`] | `gcd()` in `lib/math/gcd.c` | wrapper |
//! | [`bsearch`] | `bsearch()` in `lib/bsearch.c` | Rust implementation |
//! | [`hweight`] | `hweightN()` in `include/asm-generic/bitops/const_hweight.h` | intrinsic |
//!
//! Where C offers a callable function that Rust can reach, the Rust side is a
//! thin wrapper calling it through `bindings::*`, so a change in `lib/` is
//! picked up mechanically. The two that are implemented in Rust have no
//! callable C counterpart and are documented as such: [`bsearch`] (C takes no
//! `priv` argument) and [`hweight`] (C expands to inline assembly).
//!
//! # Removal path
//!
//! None of this is meant to coexist with C forever: each module documents how
//! its C side gets deleted once the last C user is migrated. The transition
//! symbol list tracks that.
//!
//! # Panics
//!
//! Functions that call into C run their comparison closures from a C callback.
//! A panic reaches the kernel panic handler (`BUG()`); it does not unwind into
//! C.
//!
//! # Testing
//!
//! The unit tests are KUnit based and gated behind `CONFIG_RUST_ALGO_KUNIT_TEST`.

pub mod bsearch;
pub mod gcd;
pub mod hweight;
pub mod sort;

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

    #[test]
    fn sort_basic() {
        let mut values = [5u32, 3, 8, 1, 9, 2, 7, 4, 6, 0];
        sort::sort_r(&mut values, |a, b| a.cmp(b));
        assert_eq!(values, [0, 1, 2, 3, 4, 5, 6, 7, 8, 9]);
    }

    #[test]
    fn sort_edge_cases() {
        let mut empty: [u32; 0] = [];
        sort::sort_r(&mut empty, |a, b| a.cmp(b));
        assert!(empty.is_empty());

        let mut one = [42u32];
        sort::sort_r(&mut one, |a, b| a.cmp(b));
        assert_eq!(one, [42]);

        // Non-trivial comparator: descending order.
        let mut values = [1u32, 4, 2, 5, 3];
        sort::sort_r(&mut values, |a, b| b.cmp(a));
        assert_eq!(values, [5, 4, 3, 2, 1]);
    }

    #[test]
    fn sort_nonatomic_and_duplicates() {
        let mut values = [2u32, 1, 2, 1, 2, 1];
        sort::sort_r_nonatomic(&mut values, |a, b| a.cmp(b));
        assert_eq!(values, [1, 1, 1, 2, 2, 2]);
    }
}
