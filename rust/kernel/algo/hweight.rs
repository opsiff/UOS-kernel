// SPDX-License-Identifier: GPL-2.0

//! Hamming weight (population count).
//!
//! Counterparts of C's `hweight8/16/32/64()` and `hweight_long()`
//! (`include/asm-generic/bitops/const_hweight.h:26`), and of the software
//! fallbacks `__sw_hweight*()` in `lib/hweight.c`.
//!
//! # Why these are intrinsics and not wrappers
//!
//! `hweightN()` is a macro: it expands to `__const_hweightN()` for constant
//! arguments and to the architecture's `__arch_hweightN()` otherwise, the
//! latter being inline assembly (`POPCNT`) on most architectures. The only
//! linkable symbols are the `__sw_hweight*()` *fallbacks*, which C uses only
//! when the architecture has no fast instruction -- calling those would be
//! slower than the C code being replaced.
//!
//! [`u32::count_ones()`] is what both C forms lower to, and being a `const fn`
//! it also keeps `HWEIGHTN()`'s compile-time folding. Nothing can drift: both
//! sides name the same intrinsic.
//!
//! # Removal path
//!
//! `include/asm-generic/bitops/const_hweight.h` and `lib/hweight.c` **can be
//! removed** once no C caller depends on the macro's constant folding. That is a
//! long-term blocker rather than a per-call-site migration, since the folding is
//! the reason the macro exists. No C call site is migrated by this series; see
//! the transition-symbol list.
//!
//! These exist to give C call sites a same-named Rust target to migrate to; they
//! are not here to save Rust callers a `count_ones()`.
//!
//! All helpers return [`u32`]; the C return types are inconsistent: `hweight64()`
//! has type `unsigned int`, while both `__sw_hweight64()` and
//! `__arch_hweight64()` return `unsigned long`. `u128` has no C counterpart.

/// Returns the number of set bits in `value`.
///
/// Counterpart of C's `hweight8()` / `HWEIGHT8()`. O(1): one `POPCNT` where
/// available.
#[inline]
pub const fn hweight8(value: u8) -> u32 {
    value.count_ones()
}

/// Returns the number of set bits in `value`.
///
/// Counterpart of C's `hweight16()` / `HWEIGHT16()`. O(1).
#[inline]
pub const fn hweight16(value: u16) -> u32 {
    value.count_ones()
}

/// Returns the number of set bits in `value`.
///
/// Counterpart of C's `hweight32()` / `HWEIGHT32()`. O(1).
#[inline]
pub const fn hweight32(value: u32) -> u32 {
    value.count_ones()
}

/// Returns the number of set bits in `value`.
///
/// Counterpart of C's `hweight64()` / `HWEIGHT64()`. O(1).
#[inline]
pub const fn hweight64(value: u64) -> u32 {
    value.count_ones()
}

/// Returns the number of set bits in `value`.
///
/// Counterpart of C's `hweight_long()`: the width follows the pointer width.
/// O(1).
#[inline]
pub const fn hweight_long(value: usize) -> u32 {
    value.count_ones()
}

/// Returns the number of set bits in `value`.
///
/// Rust-only convenience; `u128` has no C counterpart. O(1).
#[inline]
pub const fn hweight128(value: u128) -> u32 {
    value.count_ones()
}
