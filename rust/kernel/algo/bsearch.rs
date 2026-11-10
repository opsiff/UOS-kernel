// SPDX-License-Identifier: GPL-2.0

//! Binary search over sorted slices.
//!
//! Counterpart of C's `bsearch()` (`include/linux/bsearch.h:30`,
//! `lib/bsearch.c:31`).
//!
//! # Why this is not a wrapper
//!
//! `bsearch()` predates the `_r` convention and takes no `priv` argument, so a
//! Rust closure cannot be passed through it: the only way would be a global,
//! which is neither reentrant nor safe for the concurrent callers the kernel
//! has (including interrupt context). The search itself is index arithmetic
//! with no kernel-specific behaviour, so it runs on top of
//! [`slice::binary_search_by()`], which halves the same way
//! (`__inline_bsearch()`, `include/linux/bsearch.h:8`).
//!
//! # Difference from the C API
//!
//! C returns a pointer or `NULL`. Here the index is returned, and a miss
//! reports the insertion point (`Err(index)`) that `__inline_bsearch()` computes
//! internally but discards. [`bsearch_by_key()`] covers C's heterogeneous key
//! case.
//!
//! The comparison order must match the one used to sort the slice beforehand
//! (see [`super::sort`]); the result is unspecified otherwise.

use core::cmp::Ordering;

/// Searches `haystack` for an element for which `compare` returns
/// [`Ordering::Equal`].
///
/// `haystack` must be sorted in the ascending order defined by `compare`.
///
/// Returns `Ok(index)` of a matching element, or `Err(insertion_index)` where
/// `insertion_index` is the position at which the element could be inserted to
/// keep the slice sorted.
///
/// If several elements compare equal, any one of them may be returned; the
/// same holds for C's `bsearch()`.
///
/// # Complexity
///
/// O(log n) comparisons.
///
/// # Examples
///
/// ```
/// let haystack = [1u32, 3, 5];
/// assert_eq!(kernel::algo::bsearch::bsearch_by(&haystack, |v| v.cmp(&3)), Ok(1));
/// assert_eq!(kernel::algo::bsearch::bsearch_by(&haystack, |v| v.cmp(&4)), Err(2));
/// ```
pub fn bsearch_by<T, F>(haystack: &[T], compare: F) -> Result<usize, usize>
where
    F: FnMut(&T) -> Ordering,
{
    haystack.binary_search_by(compare)
}

/// Searches `haystack` for an element for which `compare(element, key)`
/// returns [`Ordering::Equal`].
///
/// Heterogeneous-key form of [`bsearch_by()`], matching the C API where the key
/// does not have to share the element type.
///
/// `haystack` must be sorted in the ascending order defined by `compare`.
///
/// Returns `Ok(index)` or `Err(insertion_index)`, like [`bsearch_by()`].
///
/// # Complexity
///
/// O(log n) comparisons.
///
/// # Examples
///
/// ```
/// struct Entry {
///     key: u32,
/// }
///
/// let haystack = [Entry { key: 1 }, Entry { key: 3 }];
/// let index = kernel::algo::bsearch::bsearch_by_key(&haystack, &3, |e, k| e.key.cmp(k));
/// assert_eq!(index, Ok(1));
/// ```
pub fn bsearch_by_key<T, K, F>(haystack: &[T], key: &K, mut compare: F) -> Result<usize, usize>
where
    F: FnMut(&T, &K) -> Ordering,
{
    haystack.binary_search_by(|element| compare(element, key))
}
