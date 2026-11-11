// SPDX-License-Identifier: GPL-2.0

//! C ABI for the merge helper in `lib/memcat_p.c`.

use core::ffi::c_void;
use core::ptr;

use crate::alloc::flags::GFP_KERNEL;
use crate::alloc::KVec;

/// Counts the pointers before the NULL terminator.
///
/// # Safety
///
/// `p` must point to a NULL-terminated array. Like `lib/memcat_p.c:16-19`, this
/// reads `*p` at increasing indices and never checks `p` itself for NULL.
unsafe fn count(p: *const *mut c_void) -> usize {
    let mut n = 0;

    while unsafe { !(*p.add(n)).is_null() } {
        n += 1;
    }

    n
}

/// Merges `a` and `b` into a newly allocated NULL-terminated array: the
/// elements of `a`, then those of `b`, then the terminator. Returns NULL when
/// the allocation fails.
///
/// The allocation is `(count(a) + count(b) + 1) * size_of::<*mut c_void>()`
/// bytes from `Kmalloc`, i.e. the same memory `kmalloc_array()` returns in
/// `lib/memcat_p.c:23`; it is meant to be released by C with `kfree()`.
///
/// # Safety
///
/// `a` and `b` must be non-NULL and point to NULL-terminated arrays. Their
/// elements are copied, never dereferenced. A NULL `a` or `b` is undefined
/// behavior here exactly as in C, which dereferences `*p` without checking the
/// array pointer.
#[no_mangle]
pub unsafe extern "C" fn __memcat_p_rs(
    a: *mut *mut c_void,
    b: *mut *mut c_void,
) -> *mut *mut c_void {
    let nr_a = unsafe { count(a) };
    let nr_b = unsafe { count(b) };
    let nr = nr_a + nr_b + 1;

    let mut out = match KVec::<*mut c_void>::with_capacity(nr, GFP_KERNEL) {
        Ok(out) => out,
        Err(_) => return ptr::null_mut(),
    };

    for i in 0..nr_a {
        if out.push_within_capacity(unsafe { *a.add(i) }).is_err() {
            return ptr::null_mut();
        }
    }

    for i in 0..nr_b {
        if out.push_within_capacity(unsafe { *b.add(i) }).is_err() {
            return ptr::null_mut();
        }
    }

    if out.push_within_capacity(ptr::null_mut()).is_err() {
        return ptr::null_mut();
    }

    let (out, _, _) = out.into_raw_parts();
    out
}
