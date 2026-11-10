// SPDX-License-Identifier: GPL-2.0

//! Greatest common divisor.
//!
//! Counterpart of C's `gcd()` (`lib/math/gcd.c:50`, declared in
//! `include/linux/gcd.h:10`). This is an implementation, not a wrapper: the
//! binary GCD from C is transcribed here, so the C function no longer has to
//! exist for Rust callers.
//!
//! # Return value
//!
//! C returns `a | b` when either argument is zero, which yields
//! `gcd(0, 0) == 0` and `gcd(a, 0) == a`. Preserved exactly.
//!
//! # The static branch in C is a performance choice, not a semantic one
//!
//! `lib/math/gcd.c:58` picks `binary_gcd()` when `efficient_ffs_key` is true and
//! falls back to the even/odd loop otherwise. The key is only ever disabled by
//! `arch/riscv/kernel/setup.c:368` on cores with a slow `__ffs`; **both paths
//! compute the same function**, so always taking the binary path -- as done here
//! -- changes speed on those cores, never results. (`u*::trailing_zeros()`
//! lowers to `tzcnt`/`bsf`, the same instruction `__ffs()` compiles to.)
//!
//! # Removal path
//!
//! `lib/math/gcd.c` can be deleted once its last C caller migrates -- 83 C call
//! sites plus `lib/math/lcm.c` at the time of writing, tracked in
//! `transition-symbols.md`. Unlike the other helpers in this directory the
//! export is not a blocker: `gcd` is `EXPORT_SYMBOL_GPL` (`lib/math/gcd.c:88`),
//! which is exactly what `rust/exports.c` emits for Rust symbols automatically.
//!
//! `efficient_ffs_key` itself is still declared in `include/linux/gcd.h` and
//! disabled from RISC-V setup code, so that declaration outlives the function.

/// Returns the greatest common divisor of `a` and `b`.
///
/// Counterpart of C's `gcd()` (`lib/math/gcd.c:50`). Returns `0` when both
/// arguments are `0`, and the non-zero argument when exactly one of them is `0`.
///
/// # Examples
///
/// ```
/// assert_eq!(kernel::algo::gcd::gcd(12, 18), 6);
/// assert_eq!(kernel::algo::gcd::gcd(0, 7), 7);
/// assert_eq!(kernel::algo::gcd::gcd(0, 0), 0);
/// ```
#[inline]
pub fn gcd(a: usize, b: usize) -> usize {
    let r = a | b;

    if a == 0 || b == 0 {
        return r;
    }

    let mut a = a;
    let mut b = b;

    // C: `b >>= __ffs(b); if (b == 1) return r & -r;`
    b >>= b.trailing_zeros();
    if b == 1 {
        return r & r.wrapping_neg();
    }

    loop {
        a >>= a.trailing_zeros();
        if a == 1 {
            return r & r.wrapping_neg();
        }
        if a == b {
            return a << r.trailing_zeros();
        }
        if a < b {
            core::mem::swap(&mut a, &mut b);
        }
        a -= b;
    }
}
