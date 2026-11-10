// SPDX-License-Identifier: GPL-2.0

//! A kernel raw spinlock.
//!
//! Unlike [`SpinLock`], which wraps `spinlock_t`, these locks wrap `raw_spinlock_t`: the kernel
//! never turns the latter into a sleeping lock, so it is the only one usable where sleeping is
//! forbidden (NMI, scheduler internals).
//!
//! C API: `include/linux/spinlock.h:217-296`.
//!
//! [`SpinLock`]: super::spinlock::SpinLock

use super::*;

/// Creates a [`RawSpinLock`] initialiser with the given name and a newly-created lock class.
///
/// It uses the name if one is given, otherwise it generates one based on the file name and line
/// number.
#[macro_export]
macro_rules! new_raw_spinlock {
    ($inner:expr $(, $name:literal)? $(,)?) => {
        $crate::sync::RawSpinLock::new(
            $inner, $crate::optional_name!($($name)?), $crate::static_lock_class!())
    };
}
pub use new_raw_spinlock;

/// A raw spinlock (`raw_spin_lock()` / `raw_spin_unlock()`, `spinlock.h:219`).
pub type RawSpinLock<T> = Lock<T, RawSpinLockBackend>;

/// A kernel `raw_spinlock_t` lock backend.
pub struct RawSpinLockBackend;

/// A [`Guard`] acquired from locking a [`RawSpinLock`].
pub type RawSpinLockGuard<'a, T> = Guard<'a, T, RawSpinLockBackend>;

// SAFETY: The underlying kernel `raw_spinlock_t` object ensures mutual exclusion.
unsafe impl Backend for RawSpinLockBackend {
    type State = bindings::raw_spinlock_t;
    type GuardState = ();

    #[inline]
    unsafe fn init(
        ptr: *mut Self::State,
        name: *const crate::ffi::c_char,
        key: *mut bindings::lock_class_key,
    ) {
        // SAFETY: The safety requirements ensure that `ptr` is valid for writes, and `name` and
        // `key` are valid for read indefinitely.
        unsafe { bindings::__raw_spin_lock_init(ptr, name, key) }
    }

    #[inline]
    unsafe fn lock(ptr: *mut Self::State) -> Self::GuardState {
        // SAFETY: The safety requirements of this function ensure that `ptr` points to valid
        // memory, and that it has been initialised before.
        unsafe { bindings::raw_spin_lock(ptr) }
    }

    #[inline]
    unsafe fn unlock(ptr: *mut Self::State, _guard_state: &Self::GuardState) {
        // SAFETY: The safety requirements of this function ensure that `ptr` is valid and that the
        // caller is the owner of the raw spinlock.
        unsafe { bindings::raw_spin_unlock(ptr) }
    }

    #[inline]
    unsafe fn try_lock(ptr: *mut Self::State) -> Option<Self::GuardState> {
        // SAFETY: The `ptr` pointer is guaranteed to be valid and initialized before use.
        let result = unsafe { bindings::raw_spin_trylock(ptr) };

        if result != 0 {
            Some(())
        } else {
            None
        }
    }

    #[inline]
    unsafe fn assert_is_held(ptr: *mut Self::State) {
        // SAFETY: The `ptr` pointer is guaranteed to be valid and initialized before use.
        unsafe { bindings::raw_spin_assert_is_held(ptr) }
    }
}
