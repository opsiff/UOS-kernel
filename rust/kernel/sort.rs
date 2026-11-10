// SPDX-License-Identifier: GPL-2.0

//! Heapsort, ported from lib/sort.c.
//!
//! Reverse-replacement prototype: the algorithm lives here and the C side is
//! either a forwarder (during the transition) or gone. The port is literal,
//! including the byte-offset addressing and the bottom-up sift-down, so the
//! number of comparisons is identical to the C version (verified).
//!
//! The raw swap argument follows lib/sort.c: the low values 0..3 are not
//! function pointers but sentinels for the built-in swappers (see SWAP_* in
//! lib/sort.c), so it is taken as an opaque pointer and decoded exactly once,
//! in decode_swap().

use core::ffi::{c_int, c_void};

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
struct Wrapper {
    cmp: Option<CmpFunc>,
    swap: Option<SwapFunc>,
}

/// Decoded swap strategy; produced once per call.
enum SwapSel {
    Words64,
    Words32,
    Bytes,
    Func(SwapRFunc),
    Wrapped(*const Wrapper),
}

/// Decoded comparison strategy.
enum CmpSel {
    Func(CmpRFunc),
    Wrapped(*const Wrapper),
}

/// Swap two elements using 64-bit chunks.
///
/// # Safety
///
/// a/b must be valid for n bytes; n must be a multiple of 8.
unsafe fn swap_words_64(a: *mut u8, b: *mut u8, mut n: usize) {
    loop {
        n -= 8;
        let t = unsafe { core::ptr::read_unaligned(a.add(n).cast::<u64>()) };
        let v = unsafe { core::ptr::read_unaligned(b.add(n).cast::<u64>()) };
        unsafe { core::ptr::write_unaligned(a.add(n).cast::<u64>(), v) };
        unsafe { core::ptr::write_unaligned(b.add(n).cast::<u64>(), t) };
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
    let mut lsbits = size as u8;

    #[cfg(not(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS))]
    {
        lsbits |= base as usize as u8;
    }
    let _ = base;

    (lsbits & (align - 1)) == 0
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
unsafe fn decode_swap(
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
        raw => SwapSel::Func(unsafe { core::mem::transmute::<usize, SwapRFunc>(raw) }),
    }
}

/// Default swapper picked from size/base alignment, see __sort_r().
fn auto_swap(base: *const c_void, size: usize) -> SwapSel {
    if is_aligned(base, size, 8) {
        SwapSel::Words64
    } else if is_aligned(base, size, 4) {
        SwapSel::Words32
    } else {
        SwapSel::Bytes
    }
}

/// __sort_r(), lib/sort.c.
///
/// # Safety
///
/// base must be valid for num*size bytes of writable memory; the callbacks must
/// honour the contracts documented in lib/sort.c.
unsafe fn sort_r_impl(
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
                bindings::rust_helper_cond_resched();
            }
        }
    }

    n -= size;
    unsafe { do_swap(base, base.add(n), size, swap, priv_) };
    if n == size * 2 && unsafe { do_cmp(base, base.add(size), cmp, priv_) } > 0 {
        unsafe { do_swap(base, base.add(size), size, swap, priv_) };
    }
}

/// sort_r(), temporary Rust symbol (see the transition list).
///
/// # Safety
///
/// See sort_r_impl(). cmp must be non-NULL, as required by the C API.
#[no_mangle]
pub unsafe extern "C" fn sort_r_rs(
    base: *mut c_void,
    num: usize,
    size: usize,
    cmp: *const c_void,
    swap: *const c_void,
    priv_: *const c_void,
) {
    let cmp = CmpSel::Func(unsafe { core::mem::transmute::<*const c_void, CmpRFunc>(cmp) });
    let swap = unsafe { decode_swap(swap, base, size, priv_) };
    unsafe { sort_r_impl(base, num, size, &cmp, &swap, priv_, false) }
}

/// sort_r_nonatomic(), temporary Rust symbol.
///
/// # Safety
///
/// See sort_r_impl().
#[no_mangle]
pub unsafe extern "C" fn sort_r_nonatomic_rs(
    base: *mut c_void,
    num: usize,
    size: usize,
    cmp: *const c_void,
    swap: *const c_void,
    priv_: *const c_void,
) {
    let cmp = CmpSel::Func(unsafe { core::mem::transmute::<*const c_void, CmpRFunc>(cmp) });
    let swap = unsafe { decode_swap(swap, base, size, priv_) };
    unsafe { sort_r_impl(base, num, size, &cmp, &swap, priv_, true) }
}

/// sort(), temporary Rust symbol.
///
/// # Safety
///
/// See sort_r_impl().
#[no_mangle]
pub unsafe extern "C" fn sort_rs(
    base: *mut c_void,
    num: usize,
    size: usize,
    cmp: Option<CmpFunc>,
    swap: Option<SwapFunc>,
) {
    let w = Wrapper { cmp, swap };
    let priv_ = (&w as *const Wrapper).cast::<c_void>();
    let cmp = CmpSel::Wrapped(&w);
    let swap = match swap {
        Some(_) => SwapSel::Wrapped(&w),
        None => auto_swap(base, size),
    };
    unsafe { sort_r_impl(base, num, size, &cmp, &swap, priv_, false) }
}

/// sort_nonatomic(), temporary Rust symbol.
///
/// # Safety
///
/// See sort_r_impl().
#[no_mangle]
pub unsafe extern "C" fn sort_nonatomic_rs(
    base: *mut c_void,
    num: usize,
    size: usize,
    cmp: Option<CmpFunc>,
    swap: Option<SwapFunc>,
) {
    let w = Wrapper { cmp, swap };
    let priv_ = (&w as *const Wrapper).cast::<c_void>();
    let cmp = CmpSel::Wrapped(&w);
    let swap = match swap {
        Some(_) => SwapSel::Wrapped(&w),
        None => auto_swap(base, size),
    };
    unsafe { sort_r_impl(base, num, size, &cmp, &swap, priv_, true) }
}