// SPDX-License-Identifier: GPL-2.0

//! C ABI facade for the heapsort implementation in `kernel::algo::sort`.
//!
//! Only the transitional `*_rs` symbols live here: they decode the C calling
//! convention (the 0..3 swap sentinels, the callback pointers and the opaque
//! `priv`) and hand everything to the single Rust implementation. Delete this
//! file once every C caller uses the Rust API directly.

use core::ffi::{c_int, c_void};

use crate::algo::sort::{
    auto_swap, cmp_from_ptr, decode_swap, sort_r_impl, CmpSel, SwapSel, Wrapper,
};

/// cmp_func_t, see include/linux/sort.h.
type CmpFunc = unsafe extern "C" fn(*const c_void, *const c_void) -> c_int;
/// swap_func_t, see include/linux/sort.h.
type SwapFunc = unsafe extern "C" fn(*mut c_void, *mut c_void, c_int);

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
    let cmp = unsafe { cmp_from_ptr(cmp) };
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
    let cmp = unsafe { cmp_from_ptr(cmp) };
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
