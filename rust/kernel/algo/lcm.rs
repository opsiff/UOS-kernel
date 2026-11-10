// SPDX-License-Identifier: GPL-2.0

//! Least common multiple.
//!
//! Counterparts of C's `lcm()` and `lcm_not_zero()` (`lib/math/lcm.c:8` and
//! `:17`, declared in `include/linux/lcm.h`).
//!
//! Both are arithmetic around `gcd()`, so they are implemented here and take the
//! gcd from [`crate::algo::gcd`]. One source of truth for the gcd semantics
//! (including C's `gcd(0, b) == b` behaviour) instead of a second implementation
//! that could drift.
//!
//! # Overflow
//!
//! C computes `(a / gcd(a, b)) * b` and relies on unsigned wrap-around when the
//! product overflows `unsigned long`. [`usize::wrapping_mul`] preserves that
//! exactly; using `*` would panic in debug builds and, more importantly, would
//! be a behaviour change rather than a bug fix. Callers that can overflow are
//! already overflowed in C.
//!
//! # Removal path
//!
//! `lib/math/lcm.c` can be removed once its last C caller migrates. No C call
//! site is migrated by this series; see the transition-symbol list.

use super::gcd;

/// Returns the least common multiple of `a` and `b`, or `0` if either is `0`.
///
/// Counterpart of C's `lcm()` (`lib/math/lcm.c:8`).
///
/// # Examples
///
/// ```
/// assert_eq!(kernel::algo::lcm::lcm(4, 6), 12);
/// assert_eq!(kernel::algo::lcm::lcm(0, 6), 0);
/// ```
#[inline]
pub fn lcm(a: usize, b: usize) -> usize {
    if a != 0 && b != 0 {
        // C: `(a / gcd(a, b)) * b`, with unsigned wrap on overflow.
        (a / gcd::gcd(a, b)).wrapping_mul(b)
    } else {
        0
    }
}

/// Like [`lcm`], but returns the non-zero argument instead of `0`.
///
/// Counterpart of C's `lcm_not_zero()` (`lib/math/lcm.c:17`), whose tail is
/// `return (b ? : a);`.
#[inline]
pub fn lcm_not_zero(a: usize, b: usize) -> usize {
    let l = lcm(a, b);

    if l != 0 {
        return l;
    }

    if b != 0 { b } else { a }
}
