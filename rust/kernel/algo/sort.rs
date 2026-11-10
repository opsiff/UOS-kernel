// SPDX-License-Identifier: GPL-2.0

//! Heapsort, ported from lib/sort.c.
//!
//! This module owns the algorithm: the C-ABI entry points in `kernel::sort`
//! only decode the C calling convention (sentinels, function pointers, priv)
//! and call into `sort_r_impl()` here. Keeping a single implementation avoids
//! the C-shim layer and a Rust-side `sort()` API drifting apart.
//!
//! On 64-bit targets the port is literal: byte-offset addressing, the bottom-up
//! sift-down and the chunk width are the same as C, so the comparison and swap
//! counts match exactly. A 32-bit target differs in one place only: the swapper
//! steps in 4-byte chunks, exactly as `swap_words_64()` does without
//! CONFIG_64BIT.
//!
//! # Removal path
//!
//! The algorithm is owned by Rust now; `lib/sort.c` stays as the carrier of the
//! C ABI and of the `EXPORT_SYMBOL()`s -- permanently, because the export
//! mechanism available to Rust is GPL-only. Migrating call sites changes who
//! calls, not why that file exists.

use core::ffi::{c_int, c_void};
use core::mem::size_of;

use crate::bindings;

/// cmp_r_func_t, see include/linux/sort.h.
type CmpRFunc = unsafe extern "C" fn(*const c_void, *const c_void, *const c_void) -> c_int;
/// swap_r_func_t, see include/linux/sort.h.
type SwapRFunc = unsafe extern "C" fn(*mut c_void, *mut c_void, c_int, *const c_void);
/// cmp_func_t, see include/linux/sort.h.
type CmpFunc = unsafe extern "C" fn(*const c_void, *const c_void) -> c_int;
/// swap_func_t, see include/linux/sort.h.
type SwapFunc = unsafe extern "C" fn(*mut c_void, *mut c_void, c_int);

/// struct wrapper, lib/sort.c.
#[repr(C)]
pub(crate) struct Wrapper {
    pub(crate) cmp: Option<CmpFunc>,
    pub(crate) swap: Option<SwapFunc>,
}

/// Decoded swap strategy; produced once per call.
pub(crate) enum SwapSel {
    Words64,
    Words32,
    Bytes,
    Func(SwapRFunc),
    Wrapped(*const Wrapper),
}

/// Decoded comparison strategy.
pub(crate) enum CmpSel {
    Func(CmpRFunc),
    Wrapped(*const Wrapper),
}

/// Chunk type of the word-at-a-time swapper.
///
/// `swap_words_64()` splits on `#ifdef CONFIG_64BIT` (`lib/sort.c:121`): 64-bit
/// transfers where the target has them, two 32-bit transfers otherwise, because
/// emulating a 64-bit load costs base+index+4 addressing on most CPUs.
/// Selecting the type the same way keeps a single implementation for every
/// target, including x32, where CONFIG_64BIT is set while pointers are 32 bits;
/// on a 32-bit target the only difference is the 4-byte step.
#[cfg(CONFIG_64BIT)]
type Word = u64;
#[cfg(not(CONFIG_64BIT))]
type Word = u32;

/// Swap two elements in chunks of [`Word`].
///
/// # Safety
///
/// a/b must be valid for n bytes; n must be a multiple of `size_of::<Word>()`.
unsafe fn swap_words_64(a: *mut u8, b: *mut u8, mut n: usize) {
    loop {
        n -= size_of::<Word>();
        let t = unsafe { core::ptr::read_unaligned(a.add(n).cast::<Word>()) };
        let v = unsafe { core::ptr::read_unaligned(b.add(n).cast::<Word>()) };
        unsafe { core::ptr::write_unaligned(a.add(n).cast::<Word>(), v) };
        unsafe { core::ptr::write_unaligned(b.add(n).cast::<Word>(), t) };
        if n == 0 {
            break;
        }
    }
}

/// Swap two elements using 32-bit chunks.
///
/// # Safety
///
/// a/b must be valid for n bytes; n must be a multiple of 4.
unsafe fn swap_words_32(a: *mut u8, b: *mut u8, mut n: usize) {
    loop {
        n -= 4;
        let t = unsafe { core::ptr::read_unaligned(a.add(n).cast::<u32>()) };
        let v = unsafe { core::ptr::read_unaligned(b.add(n).cast::<u32>()) };
        unsafe { core::ptr::write_unaligned(a.add(n).cast::<u32>(), v) };
        unsafe { core::ptr::write_unaligned(b.add(n).cast::<u32>(), t) };
        if n == 0 {
            break;
        }
    }
}

/// Swap two elements byte by byte.
///
/// # Safety
///
/// a/b must be valid for n bytes.
unsafe fn swap_bytes(a: *mut u8, b: *mut u8, mut n: usize) {
    loop {
        n -= 1;
        let t = unsafe { core::ptr::read(a.add(n)) };
        let v = unsafe { core::ptr::read(b.add(n)) };
        unsafe { core::ptr::write(a.add(n), v) };
        unsafe { core::ptr::write(b.add(n), t) };
        if n == 0 {
            break;
        }
    }
}

/// Byte-offset form of is_aligned().
fn is_aligned(base: *const c_void, size: usize, align: u8) -> bool {
    let _ = base;

    // Only the architectures without cheap unaligned access need to look at
    // the base address (lib/sort.c:36).
    #[cfg(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS)]
    let base_lsbits = 0u8;
    #[cfg(not(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS))]
    let base_lsbits = base as usize as u8;

    ((size as u8) | base_lsbits) & (align - 1) == 0
}

/// Parent of byte offset i, see parent() in lib/sort.c.
#[inline]
fn parent(i: usize, lsbit: usize, size: usize) -> usize {
    let i = i - size;
    let i = i - (size & (i & lsbit).wrapping_neg());
    i / 2
}

unsafe fn do_swap(a: *mut u8, b: *mut u8, size: usize, sel: &SwapSel, priv_: *const c_void) {
    match sel {
        SwapSel::Wrapped(w) => {
            let f = unsafe { (**w).swap }.expect("wrapper without swap");
            unsafe { f(a.cast::<c_void>(), b.cast::<c_void>(), size as c_int) };
        }
        SwapSel::Words64 => unsafe { swap_words_64(a, b, size) },
        SwapSel::Words32 => unsafe { swap_words_32(a, b, size) },
        SwapSel::Bytes => unsafe { swap_bytes(a, b, size) },
        SwapSel::Func(f) => unsafe {
            f(a.cast::<c_void>(), b.cast::<c_void>(), size as c_int, priv_)
        },
    }
}

unsafe fn do_cmp(a: *const u8, b: *const u8, sel: &CmpSel, priv_: *const c_void) -> c_int {
    match sel {
        CmpSel::Wrapped(w) => {
            let f = unsafe { (**w).cmp }.expect("wrapper without cmp");
            unsafe { f(a.cast::<c_void>(), b.cast::<c_void>()) }
        }
        CmpSel::Func(f) => unsafe { f(a.cast::<c_void>(), b.cast::<c_void>(), priv_) },
    }
}

/// Decode the raw swap argument; the only place where SWAP_* is interpreted.
pub(crate) unsafe fn decode_swap(
    swap: *const c_void,
    base: *const c_void,
    size: usize,
    priv_: *const c_void,
) -> SwapSel {
    match swap as usize {
        0 => auto_swap(base, size),
        1 => SwapSel::Words32,
        2 => SwapSel::Bytes,
        3 => {
            let w = priv_.cast::<Wrapper>();
            if unsafe { (*w).swap }.is_none() {
                auto_swap(base, size)
            } else {
                SwapSel::Wrapped(w)
            }
        }
        // SAFETY: every other value is a real callback address handed in by
        // the C caller; 0..3 are the sentinels handled above.
        raw => SwapSel::Func(unsafe { core::mem::transmute::<usize, SwapRFunc>(raw) }),
    }
}

/// Default swapper picked from size/base alignment, see __sort_r().
pub(crate) fn auto_swap(base: *const c_void, size: usize) -> SwapSel {
    if is_aligned(base, size, 8) {
        SwapSel::Words64
    } else if is_aligned(base, size, 4) {
        SwapSel::Words32
    } else {
        SwapSel::Bytes
    }
}

/// Turn the C comparator pointer into a callable, rejecting NULL.
///
/// # Safety
///
/// `cmp` must be NULL or a valid `cmp_r_func_t`. C requires non-NULL here; a
/// NULL used to be an immediate call through NULL, so rejecting it explicitly
/// only turns undefined behaviour into a clean abort.
pub(crate) unsafe fn cmp_from_ptr(cmp: *const c_void) -> CmpSel {
    assert!(!cmp.is_null(), "sort_r: NULL comparator");
    // SAFETY: non-NULL and, per the C API, a valid function pointer.
    CmpSel::Func(unsafe { core::mem::transmute::<*const c_void, CmpRFunc>(cmp) })
}

/// __sort_r(), lib/sort.c.
///
/// # Safety
///
/// base must be valid for num*size bytes of writable memory; the callbacks must
/// honour the contracts documented in lib/sort.c.
pub(crate) unsafe fn sort_r_impl(
    base: *mut c_void,
    num: usize,
    size: usize,
    cmp: &CmpSel,
    swap: &SwapSel,
    priv_: *const c_void,
    may_schedule: bool,
) {
    let base = base.cast::<u8>();
    let mut n = num * size;
    let mut a = (num / 2) * size;
    let lsbit = size & size.wrapping_neg();
    let mut shift: usize = 0;

    if a == 0 {
        return;
    }

    loop {
        let mut c;
        let mut d;
        let mut b;

        if a != 0 {
            a -= size << shift;
        } else if n > 3 * size {
            n -= size;
            unsafe { do_swap(base, base.add(n), size, swap, priv_) };
            let first_le_second =
                unsafe { do_cmp(base.add(size), base.add(2 * size), cmp, priv_) } <= 0;
            shift = first_le_second as usize;
            a = size << shift;
            n -= size;
            unsafe { do_swap(base.add(a), base.add(n), size, swap, priv_) };
        } else {
            break;
        }

        b = a;
        loop {
            c = 2 * b + size;
            d = c + size;
            if d >= n {
                break;
            }
            b = if unsafe { do_cmp(base.add(c), base.add(d), cmp, priv_) } > 0 {
                c
            } else {
                d
            };
        }
        if d == n {
            b = c;
        }

        while b != a && unsafe { do_cmp(base.add(a), base.add(b), cmp, priv_) } >= 0 {
            b = parent(b, lsbit, size);
        }
        let cc = b;
        while b != a {
            b = parent(b, lsbit, size);
            unsafe { do_swap(base.add(b), base.add(cc), size, swap, priv_) };
        }

        if may_schedule {
            unsafe {
                bindings::cond_resched();
            }
        }
    }

    n -= size;
    unsafe { do_swap(base, base.add(n), size, swap, priv_) };
    if n == size * 2 && unsafe { do_cmp(base, base.add(size), cmp, priv_) } > 0 {
        unsafe { do_swap(base, base.add(size), size, swap, priv_) };
    }
}
