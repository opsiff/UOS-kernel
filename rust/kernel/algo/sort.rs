// SPDX-License-Identifier: GPL-2.0

//! In-place sorting, wrapping C's heapsort in `lib/sort.c`.
//!
//! | C | Rust |
//! | --- | --- |
//! | `sort_r()` (lib/sort.c:303) | [`sort_r()`] |
//! | `sort_r_nonatomic()` (lib/sort.c:324) | [`sort_r_nonatomic()`] |
//!
//! `sort()`/`sort_nonatomic()` (lib/sort.c:333,346) are the same algorithm with
//! `priv` replaced by a wrapper struct, so they get no separate wrapper here.
//! `swap_r_func_t` is not exposed: no in-tree Rust caller needs it, and adding
//! it means handing out raw element pointers in addition to the comparator's
//! `&T` borrows.
//!
//! # Removal path
//!
//! These wrappers exist to migrate callers, not to stay: once the last C user
//! of `sort_r()` is gone, the algorithm moves to Rust and `lib/sort.c` is
//! deleted. See the transition-symbol list for the remaining users.
//!
//! # Context
//!
//! [`sort_r()`] never sleeps. [`sort_r_nonatomic()`] calls `cond_resched()`
//! (lib/sort.c:268) and **must not** be used in atomic context, with preemption
//! disabled, or while holding a lock.
//!
//! # Panics
//!
//! The comparator runs inside a C callback; a panic reaches the kernel panic
//! handler (`BUG()`) instead of unwinding into C.

use core::cmp::Ordering;
use core::ffi::{c_int, c_void};
use core::mem::size_of;

use crate::bindings;

/// Sorts `slice` in place, using `compare` to order the elements.
///
/// Wraps C's `sort_r()`: unstable heapsort, O(n log n) comparisons in the worst
/// case, O(1) extra space, no allocation, no recursion.
///
/// `compare` must define a total order (antisymmetric and transitive), like the
/// C comparator documented in `lib/sort.c`.
///
/// # Context
///
/// Does not sleep; usable in atomic context.
///
/// # Examples
///
/// ```
/// let mut values = [3u32, 1, 2];
/// kernel::algo::sort::sort_r(&mut values, |a, b| a.cmp(b));
/// assert_eq!(values, [1, 2, 3]);
/// ```
pub fn sort_r<T, F>(slice: &mut [T], compare: F)
where
    F: FnMut(&T, &T) -> Ordering,
{
    // SAFETY: `slice` is a valid mutable slice and `compare` outlives the call.
    unsafe { sort_r_inner(slice, compare, bindings::sort_r) }
}

/// Sorts `slice` in place, allowing rescheduling while sorting.
///
/// Wraps C's `sort_r_nonatomic()`: same algorithm as [`sort_r()`], but C calls
/// `cond_resched()` periodically (lib/sort.c:268).
///
/// # Context
///
/// **May sleep.** Must not be called from atomic context, with preemption
/// disabled, or while holding a lock. Use [`sort_r()`] there instead.
///
/// # Examples
///
/// ```
/// let mut values = [3u32, 1, 2];
/// kernel::algo::sort::sort_r_nonatomic(&mut values, |a, b| a.cmp(b));
/// assert_eq!(values, [1, 2, 3]);
/// ```
pub fn sort_r_nonatomic<T, F>(slice: &mut [T], compare: F)
where
    F: FnMut(&T, &T) -> Ordering,
{
    // SAFETY: as above.
    unsafe { sort_r_inner(slice, compare, bindings::sort_r_nonatomic) }
}

/// Shared implementation of the two wrappers above.
///
/// # Safety
///
/// `sort` must be one of the C entry points taking `(base, num, size, cmp,
/// swap, priv)` and sort `num` elements of `size` bytes at `base`.
unsafe fn sort_r_inner<T, F>(
    slice: &mut [T],
    mut compare: F,
    sort: unsafe extern "C" fn(
        *mut c_void,
        usize,
        usize,
        bindings::cmp_r_func_t,
        bindings::swap_r_func_t,
        *const c_void,
    ),
) where
    F: FnMut(&T, &T) -> Ordering,
{
    // C returns early when `num < 2 || size == 0` (lib/sort.c:200); mirror the
    // condition to skip the FFI call in the trivial cases, not to protect C.
    if slice.len() < 2 || size_of::<T>() == 0 {
        return;
    }

    // SAFETY: `slice` is valid for `len * size_of::<T>()` bytes and `compare`
    // is a valid `F` for the duration of the call; the trampoline only
    // dereferences pointers that `sort` derives from `slice`.
    unsafe {
        sort(
            slice.as_mut_ptr().cast(),
            slice.len(),
            size_of::<T>(),
            Some(cmp_trampoline::<T, F>),
            None,
            (&mut compare as *mut F).cast(),
        )
    }
}

/// Adapter from a Rust comparison closure to C's `cmp_r_func_t`.
///
/// # Safety
///
/// `a` and `b` must point to valid `T` values, `priv` must be the pointer to
/// the closure passed to `sort`, and all three must stay valid for the call.
unsafe extern "C" fn cmp_trampoline<T, F>(
    a: *const c_void,
    b: *const c_void,
    priv_: *const c_void,
) -> c_int
where
    F: FnMut(&T, &T) -> Ordering,
{
    // SAFETY: guaranteed by the contract above.
    let compare = unsafe { &mut *(priv_ as *mut F) };
    // SAFETY: `sort` only ever passes element pointers of the slice it sorts.
    let a = unsafe { &*(a as *const T) };
    // SAFETY: as above.
    let b = unsafe { &*(b as *const T) };

    // C orders by the sign of the return value.
    match compare(a, b) {
        Ordering::Less => -1,
        Ordering::Equal => 0,
        Ordering::Greater => 1,
    }
}
