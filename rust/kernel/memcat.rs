// SPDX-License-Identifier: GPL-2.0

//! Entry point for the C side's STM policy attribute merge.

use core::ffi::c_void;
use core::ptr;

use crate::alloc::flags::GFP_KERNEL;
use crate::alloc::KVec;

/// Counts the pointers before the NULL terminator.
///
/// # Safety
///
/// `p` must point to a NULL-terminated array. This reads `*p` at increasing
/// indices and never checks `p` itself for NULL.
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
/// bytes from `Kmalloc`, i.e. the same memory the former C implementation's
/// `kmalloc_array()` returned; it is meant to be released by C with `kfree()`.
///
/// # Safety
///
/// `a` and `b` must be non-NULL and point to NULL-terminated arrays. Their
/// elements are copied, never dereferenced. A NULL `a` or `b` is undefined
/// behavior here exactly as in the former C implementation, which dereferenced
/// `*p` without checking the array pointer.
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

#[cfg(CONFIG_RUST_MEMCAT_KUNIT_TEST)]
#[macros::kunit_tests(rust_kernel_memcat)]
mod tests {
    use super::*;

    /// Element of the two input arrays: a payload and a magic value that must
    /// survive the merge unchanged.
    struct TestStruct {
        num: i32,
        magic: u32,
    }

    /// Value that every merged element's `magic` field must keep.
    const MAGIC: u32 = 0xf00f_f00f;
    /// Slots in each NULL-terminated input array.
    const INPUT_MAX: usize = 128;
    /// Non-NULL elements in the merged output: two arrays of `INPUT_MAX - 1`.
    const EXPECT: usize = INPUT_MAX * 2 - 2;

    #[test]
    fn memcat_p_merge() {
        // The two inputs: `INPUT_MAX - 1` elements each, plus the NULL terminator.
        let mut elems0 = KVec::<TestStruct>::with_capacity(INPUT_MAX - 1, GFP_KERNEL).unwrap();
        let mut elems1 = KVec::<TestStruct>::with_capacity(INPUT_MAX - 1, GFP_KERNEL).unwrap();
        let mut in0 = KVec::<*mut c_void>::with_capacity(INPUT_MAX, GFP_KERNEL).unwrap();
        let mut in1 = KVec::<*mut c_void>::with_capacity(INPUT_MAX, GFP_KERNEL).unwrap();

        let mut r: i32 = 1;
        for _ in 0..INPUT_MAX - 1 {
            // The C test computed this in `int`, i.e. a wrapping 32-bit multiply on
            // x86-64; `wrapping_mul()` reproduces that without signed-overflow UB. The
            // assertions only need `num` and `-num` to cancel, not the exact values.
            r = r.wrapping_mul(725861) % 6599;
            assert!(elems0
                .push_within_capacity(TestStruct { num: r, magic: MAGIC })
                .is_ok());
            assert!(elems1
                .push_within_capacity(TestStruct { num: -r, magic: MAGIC })
                .is_ok());
        }

        let base0 = elems0.as_ptr();
        let base1 = elems1.as_ptr();
        for i in 0..INPUT_MAX - 1 {
            // SAFETY: `i < INPUT_MAX - 1` is in bounds of both element vectors.
            let p0 = unsafe { base0.add(i) } as *mut c_void;
            let p1 = unsafe { base1.add(i) } as *mut c_void;
            assert!(in0.push_within_capacity(p0).is_ok());
            assert!(in1.push_within_capacity(p1).is_ok());
        }
        assert!(in0.push_within_capacity(ptr::null_mut()).is_ok());
        assert!(in1.push_within_capacity(ptr::null_mut()).is_ok());

        // SAFETY: both arrays are non-NULL and NULL-terminated, as required.
        let out = unsafe { __memcat_p_rs(in0.as_mut_ptr(), in1.as_mut_ptr()) };
        assert!(!out.is_null());

        // Check the slot bound before the load so a missing terminator fails the
        // test instead of reading past the allocation.
        let mut total: i32 = 0;
        let mut nr: usize = 0;
        while nr < INPUT_MAX * 2 - 1 {
            // SAFETY: `nr < INPUT_MAX * 2 - 1` is in bounds of the merged array.
            let p = unsafe { *out.add(nr) };
            if p.is_null() {
                break;
            }
            // SAFETY: the merged array only contains pointers into `elems0`/`elems1`.
            let elem = unsafe { &*p.cast::<TestStruct>() };
            total += elem.num;
            assert_eq!(elem.magic, MAGIC);
            nr += 1;
        }

        assert_eq!(total, 0);
        assert_eq!(nr, EXPECT);
        // The NULL terminator at index `EXPECT`: together with the elements above it
        // fills the `INPUT_MAX * 2 - 1` slots the helper allocates.
        assert!(unsafe { *out.add(EXPECT) }.is_null());

        for i in 0..INPUT_MAX - 1 {
            // SAFETY: `i + INPUT_MAX - 1 < EXPECT` and the array has `EXPECT + 1` slots.
            assert_eq!(unsafe { *out.add(i) }, in0[i]);
            assert_eq!(unsafe { *out.add(i + INPUT_MAX - 1) }, in1[i]);
        }

        // SAFETY: `out` is the helper's `Kmalloc` allocation, so `kfree()` releases it.
        unsafe { crate::bindings::kfree(out.cast::<c_void>()) };
    }
}
