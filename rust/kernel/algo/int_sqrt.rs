// SPDX-License-Identifier: GPL-2.0

//! Integer square root.
//!
//! Counterpart of C's `int_sqrt()` (`lib/math/int_sqrt.c:20`, declared in
//! `include/linux/math.h:200`). The C function is the shift-and-subtract
//! algorithm from Guy L. Steele; it is portable and has no architecture-specific
//! part, so this is an implementation rather than a wrapper.
//!
//! The loop is transcribed operation for operation. `__fls(x)` becomes
//! `usize::BITS - 1 - x.leading_zeros()` -- the same "index of the highest set
//! bit" quantity, and zero-free because the `x <= 1` case returns early.
//!
//! # Scope: 64-bit is complete, 32-bit keeps `int_sqrt64()` in C
//!
//! On 64-bit `int_sqrt64(u64)` is a `static inline` that casts to
//! `int_sqrt()` (`include/linux/math.h:202-208`), so it is covered for free.
//! On 32-bit it is a separate function (`lib/math/int_sqrt.c:49`) with its own
//! `EXPORT_SYMBOL()`, and this series does not provide a 64-bit-input Rust
//! version. **So the replacement here is complete for 64-bit and partial for
//! 32-bit**: `int_sqrt64()` stays with C there.
//!
//! The impact is bounded and known -- seven call sites, all of them inside
//! argument expressions, none in a hot path:
//!
//! | Call site | Uses |
//! | --- | --- |
//! | `drivers/iio/temperature/mlx90632.c:754` | 2 (nested) |
//! | `drivers/iio/temperature/mlx90635.c:542` | 2 (nested) |
//! | `kernel/dma/map_benchmark.c:379` | 1 |
//! | `kernel/dma/map_benchmark.c:380` | 1 |
//! | `fs/ceph/debugfs.c:168` | 1 |
//!
//! `grep -rho 'int_sqrt64(' --include='*.c' . | wc -l` gives 8: the definition
//! plus these seven. Adding a Rust `int_sqrt64()` is a 32-bit-only follow-up,
//! not a precondition for the 64-bit replacement.
//!
//! # Removal path
//!
//! The implementation can be reclaimed, but **not the whole file**: both
//! `int_sqrt()` (`lib/math/int_sqrt.c:41`) and, on 32-bit, `int_sqrt64()`
//! (`:70`) are exported with plain `EXPORT_SYMBOL()`, i.e. non-GPL. Rust can
//! only emit `EXPORT_SYMBOL_GPL` (`rust/exports.c:11`), so those two lines have
//! to stay in C. Once the last C caller migrates, `lib/math/int_sqrt.c` shrinks
//! to an export stub holding the two `EXPORT_SYMBOL()` lines -- the same
//! boundary that keeps `lib/sort.c` alive.
//!
//! Note that the same directory splits both ways: `lib/math/gcd.c:88` and
//! `lib/math/lcm.c:15`/`:26` are `EXPORT_SYMBOL_GPL`, so those two files can be
//! deleted outright once their callers move; `lib/math/int_sqrt.c` is
//! `EXPORT_SYMBOL` and keeps a stub forever. Stating the per-file export type
//! is what decides which is which -- not the size of the function.

/// Returns `floor(sqrt(x))`.
///
/// Counterpart of C's `int_sqrt()` (`lib/math/int_sqrt.c:20`).
///
/// # Examples
///
/// ```
/// assert_eq!(kernel::algo::int_sqrt::int_sqrt(0), 0);
/// assert_eq!(kernel::algo::int_sqrt::int_sqrt(15), 3);
/// assert_eq!(kernel::algo::int_sqrt::int_sqrt(16), 4);
/// ```
#[inline]
pub fn int_sqrt(x: usize) -> usize {
    if x <= 1 {
        return x;
    }

    // C: `m = 1UL << (__fls(x) & ~1UL)` -- largest power of four that is <= x.
    let mut m = 1usize << ((usize::BITS - 1 - x.leading_zeros()) & !1);
    let mut x = x;
    let mut y = 0usize;

    while m != 0 {
        let b = y.wrapping_add(m);

        y >>= 1;

        if x >= b {
            x -= b;
            y += m;
        }

        m >>= 2;
    }

    y
}
