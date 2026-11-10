// SPDX-License-Identifier: GPL-2.0

//! Greatest common divisor, wrapping C's `gcd()` from `lib/math/gcd.c`
//! (declared in `include/linux/gcd.h:10`).
//!
//! The binary GCD implementation, the `efficient_ffs_key` static branch and the
//! architecture-specific `__ffs()` handling stay in C.
//!
//! C returns `a | b` when one of the arguments is zero, which yields
//! `gcd(0, 0) == 0` and `gcd(a, 0) == a`; that semantics is preserved exactly.
//!
//! # Removal path
//!
//! Wrapper for migration only: `lib/math/gcd.c` **can be removed** once its last
//! C user moves to this function. No C call site is migrated by this series; the
//! transition-symbol list tracks progress.

use crate::bindings;

/// Returns the greatest common divisor of `a` and `b`.
///
/// Wraps C's `gcd()` (`lib/math/gcd.c:50`). Returns `0` when both arguments
/// are `0`, and the non-zero argument when exactly one of them is `0`.
///
/// # Examples
///
/// ```
/// assert_eq!(kernel::algo::gcd::gcd(12, 18), 6);
/// assert_eq!(kernel::algo::gcd::gcd(0, 7), 7);
/// ```
#[inline]
pub fn gcd(a: usize, b: usize) -> usize {
    // SAFETY: FFI call with plain integers. `gcd()` has no preconditions and
    // cannot fail.
    unsafe { bindings::gcd(a, b) }
}
