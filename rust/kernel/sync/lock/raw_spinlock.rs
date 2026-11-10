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
/// The interrupt state saved by `raw_spin_lock_irqsave()`.
///
/// Opaque on purpose: it can only come from the lock, so an arbitrary integer cannot be passed to
/// `raw_spin_unlock_irqrestore()`.
#[derive(Clone, Copy)]
pub struct RawSpinLockIrqSaveState(crate::ffi::c_ulong);

/// Creates a [`RawSpinLockIrqSave`] initialiser with the given name and a newly-created lock class.
#[macro_export]
macro_rules! new_raw_spinlock_irqsave {
    ($inner:expr $(, $name:literal)? $(,)?) => {
        $crate::sync::RawSpinLockIrqSave::new(
            $inner, $crate::optional_name!($($name)?), $crate::static_lock_class!())
    };
}
pub use new_raw_spinlock_irqsave;

/// A raw spinlock using `raw_spin_lock_irqsave()` / `raw_spin_unlock_irqrestore()`
/// (`spinlock.h:243` / `:283`).
///
/// Unlike [`SpinLockIrq`], this saves and restores the caller's interrupt state instead of using
/// the nested interrupt-disable mechanism.
///
/// [`SpinLockIrq`]: super::spinlock::SpinLockIrq
pub type RawSpinLockIrqSave<T> = Lock<T, RawSpinLockIrqSaveBackend>;

/// A kernel `raw_spinlock_t` lock backend using `raw_spin_lock_irqsave()`.
pub struct RawSpinLockIrqSaveBackend;

/// A [`Guard`] acquired from locking a [`RawSpinLockIrqSave`].
pub type RawSpinLockIrqSaveGuard<'a, T> = Guard<'a, T, RawSpinLockIrqSaveBackend>;

// SAFETY: The underlying kernel `raw_spinlock_t` object ensures mutual exclusion.
unsafe impl Backend for RawSpinLockIrqSaveBackend {
    type State = bindings::raw_spinlock_t;
    type GuardState = RawSpinLockIrqSaveState;

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
        let flags = unsafe { bindings::raw_spin_lock_irqsave(ptr) };

        RawSpinLockIrqSaveState(flags)
    }

    #[inline]
    unsafe fn unlock(ptr: *mut Self::State, guard_state: &Self::GuardState) {
        // SAFETY: The safety requirements of this function ensure that `ptr` is valid and that the
        // caller is the owner of the raw spinlock.
        unsafe { bindings::raw_spin_unlock_irqrestore(ptr, guard_state.0) }
    }

    #[inline]
    unsafe fn try_lock(ptr: *mut Self::State) -> Option<Self::GuardState> {
        let mut flags: crate::ffi::c_ulong = 0;

        // SAFETY: The `ptr` pointer is guaranteed to be valid and initialized before use, and
        // `flags` is a valid pointer for the duration of the call. On failure the helper has
        // already restored the previous interrupt state.
        let result = unsafe { bindings::raw_spin_trylock_irqsave(ptr, &mut flags) };

        if result != 0 {
            Some(RawSpinLockIrqSaveState(flags))
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

impl<T: ?Sized> Lock<T, RawSpinLockIrqSaveBackend> {
    /// Acquires the lock with local interrupts disabled (`raw_spin_lock_irqsave()`).
    ///
    /// The previous interrupt state is restored when the guard is dropped; the caller cannot
    /// observe it, and must not restore it itself.
    #[inline]
    pub fn lock_irqsave(&self) -> RawSpinLockIrqSaveGuard<'_, T> {
        self.lock()
    }

    /// Tries to acquire the lock with local interrupts disabled
    /// (`raw_spin_trylock_irqsave()`).
    ///
    /// If the lock is contended, returns `None` with the interrupt state left as it was.
    #[must_use = "if unused, the lock will be immediately unlocked"]
    #[inline]
    pub fn try_lock_irqsave(&self) -> Option<RawSpinLockIrqSaveGuard<'_, T>> {
        self.try_lock()
    }
}

/// Creates a [`RawSpinLockBh`] initialiser with the given name and a newly-created lock class.
#[macro_export]
macro_rules! new_raw_spinlock_bh {
    ($inner:expr $(, $name:literal)? $(,)?) => {
        $crate::sync::RawSpinLockBh::new(
            $inner, $crate::optional_name!($($name)?), $crate::static_lock_class!())
    };
}
pub use new_raw_spinlock_bh;

/// A raw spinlock using `raw_spin_lock_bh()` / `raw_spin_unlock_bh()` (`spinlock.h:278`).
pub type RawSpinLockBh<T> = Lock<T, RawSpinLockBhBackend>;

/// A kernel `raw_spinlock_t` lock backend using `raw_spin_lock_bh()`.
pub struct RawSpinLockBhBackend;

/// A [`Guard`] acquired from locking a [`RawSpinLockBh`].
pub type RawSpinLockBhGuard<'a, T> = Guard<'a, T, RawSpinLockBhBackend>;

// SAFETY: The underlying kernel `raw_spinlock_t` object ensures mutual exclusion.
unsafe impl Backend for RawSpinLockBhBackend {
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
        unsafe { bindings::raw_spin_lock_bh(ptr) }
    }

    #[inline]
    unsafe fn unlock(ptr: *mut Self::State, _guard_state: &Self::GuardState) {
        // SAFETY: The safety requirements of this function ensure that `ptr` is valid and that the
        // caller is the owner of the raw spinlock.
        unsafe { bindings::raw_spin_unlock_bh(ptr) }
    }

    #[inline]
    unsafe fn try_lock(ptr: *mut Self::State) -> Option<Self::GuardState> {
        // SAFETY: The `ptr` pointer is guaranteed to be valid and initialized before use.
        let result = unsafe { bindings::raw_spin_trylock_bh(ptr) };

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

impl<T: ?Sized> Lock<T, RawSpinLockBhBackend> {
    /// Acquires the lock with bottom halves disabled (`raw_spin_lock_bh()`).
    ///
    /// Dropping the guard reverts **one** level of the per-CPU softirq count
    /// (`__local_bh_enable_ip()`), exactly like the C API: if the caller (or an outer
    /// scope) already disabled bottom halves, they stay disabled. Do not treat the guard
    /// as "bottom halves are now enabled".
    #[inline]
    pub fn lock_bh(&self) -> RawSpinLockBhGuard<'_, T> {
        self.lock()
    }

    /// Tries to acquire the lock with bottom halves disabled (`raw_spin_trylock_bh()`).
    ///
    /// On contention returns `None` after undoing the increment it made, so the softirq
    /// count is left as the caller had it.
    #[must_use = "if unused, the lock will be immediately unlocked"]
    #[inline]
    pub fn try_lock_bh(&self) -> Option<RawSpinLockBhGuard<'_, T>> {
        self.try_lock()
    }
}
