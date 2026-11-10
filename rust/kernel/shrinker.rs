// SPDX-License-Identifier: GPL-2.0

//! Shrinker API.
//!
//! Allows Rust code to register a cache that the memory reclaim path can query
//! for freeable objects and later reclaim them, without rewriting any part of
//! the reclaim machinery.
//!
//! C header: [include/linux/shrinker.h](srctree/include/linux/shrinker.h)
//!
//! This is the migration step, not the end state: the C registry, lifetime and
//! reclaim loop still drive the callbacks, and `mm/shrinker.c` is meant to be
//! deleted eventually. The removal path is: (1) register through C, as here;
//! (2) move `shrinker_list`, the memcg `id` and `nr_deferred` into Rust behind
//! a C-callable dispatch; (3) port `do_shrink_slab()` and `shrink_slab()`;
//! (4) delete `mm/shrinker.c` once no C struct (e.g. `super_block`) embeds a
//! `struct shrinker` any more. Keeping the object behind an opaque
//! `Registration` is what makes step 2 possible without an API break.

use crate::{bindings, ffi, prelude::*, str::CStr};
use core::ptr::NonNull;

/// Value returned by [`Shrinker::scan_objects`] when no further progress can be
/// made, e.g. because reclaim would deadlock on a lock the caller holds.
///
/// C: SHRINK_STOP, include/linux/shrinker.h:58.
pub const SHRINK_STOP: usize = !0;

/// Value returned by [`Shrinker::count_objects`] when the cache is empty.
///
/// Returning 0 instead means "unknown or not worth shrinking right now", which
/// makes reclaim defer work rather than skip the cache.
///
/// C: SHRINK_EMPTY, include/linux/shrinker.h:59.
pub const SHRINK_EMPTY: usize = !0 - 1;

// Keep the constants above in sync with the C macros. bindgen represents both
// as negative i32, so the sign-extending cast is what makes this portable.
const _: () = assert!(SHRINK_STOP == bindings::SHRINK_STOP as usize);
const _: () = assert!(SHRINK_EMPTY == bindings::SHRINK_EMPTY as usize);

/// The shrinker is NUMA aware: the callbacks receive a useful nid.
///
/// C: SHRINKER_NUMA_AWARE, include/linux/shrinker.h:126.
pub const SHRINKER_NUMA_AWARE: u32 = bindings::SHRINKER_NUMA_AWARE;

/// The shrinker is memcg aware and is charged per memory cgroup.
///
/// C: SHRINKER_MEMCG_AWARE, include/linux/shrinker.h:127.
pub const SHRINKER_MEMCG_AWARE: u32 = bindings::SHRINKER_MEMCG_AWARE;

/// The shrinker manages objects that are not accounted as slab.
///
/// C: SHRINKER_NONSLAB, include/linux/shrinker.h:132.
pub const SHRINKER_NONSLAB: u32 = bindings::SHRINKER_NONSLAB;

/// Reclaim context passed to a [`Shrinker`].
///
/// Wraps struct shrink_control; only the fields meaningful to a shrinker are
/// exposed, so that the layout stays owned by the C side.
pub struct ShrinkControl<'a> {
    inner: &'a mut bindings::shrink_control,
}

impl<'a> ShrinkControl<'a> {
    /// # Safety
    ///
    /// sc must point to a valid struct shrink_control that is exclusively
    /// borrowed for the lifetime of the returned value.
    unsafe fn from_raw(sc: *mut bindings::shrink_control) -> Self {
        Self {
            inner: unsafe { &mut *sc },
        }
    }

    /// The GFP mask of the allocation that triggered reclaim.
    pub fn gfp_mask(&self) -> u32 {
        self.inner.gfp_mask
    }

    /// The node being shrunk; only meaningful with [`SHRINKER_NUMA_AWARE`].
    pub fn nid(&self) -> i32 {
        self.inner.nid
    }

    /// How many objects the shrinker is asked to scan.
    pub fn nr_to_scan(&self) -> usize {
        self.inner.nr_to_scan
    }

    /// How many objects the shrinker reports as scanned.
    ///
    /// The core initializes this to nr_to_scan before every call and uses the
    /// value left behind to account deferred work, so a shrinker that scans
    /// fewer objects than requested must update it.
    pub fn nr_scanned(&self) -> usize {
        self.inner.nr_scanned
    }

    /// Sets the number of objects actually scanned.
    pub fn set_nr_scanned(&mut self, nr: usize) {
        self.inner.nr_scanned = nr;
    }

    /// Whether reclaim is running on behalf of a memory cgroup.
    pub fn is_memcg(&self) -> bool {
        !self.inner.memcg.is_null()
    }
}

/// A cache that can be shrunk by the reclaim path.
///
/// The callbacks take &self because the same shrinker can run concurrently on
/// several CPUs; implementations that need mutable state must provide their own
/// interior mutability.
///
/// They run in direct reclaim/kswapd context: they must not allocate memory
/// (that would re-enter reclaim) and must not block on locks reclaim may
/// already hold. When progress is impossible, return [`SHRINK_STOP`] from
/// scan_objects instead of blocking.
///
/// A panic inside a callback aborts the kernel: it is built with panic=abort
/// and there is no way back into C. Implementations must be panic-free, i.e.
/// avoid indexing, unwrap() and arithmetic that can overflow on untrusted
/// values.
pub trait Shrinker {
    /// Returns the number of freeable objects, [`SHRINK_EMPTY`] when the cache
    /// is empty, or 0 when the number cannot be determined or the cache should
    /// be skipped this time.
    fn count_objects(&self, sc: &mut ShrinkControl<'_>) -> usize;

    /// Scans the cache and tries to free objects, returning how many were
    /// actually freed, or [`SHRINK_STOP`] if no progress can be made.
    fn scan_objects(&self, sc: &mut ShrinkControl<'_>) -> usize;
}

unsafe extern "C" fn count_objects_shim<T: Shrinker>(
    shrinker: *mut bindings::shrinker,
    sc: *mut bindings::shrink_control,
) -> ffi::c_ulong {
    // SAFETY: the C side only calls this for a shrinker created by
    // Registration::new(), which stores a *const T in private_data and keeps
    // it alive until after shrinker_free() has returned.
    let data = unsafe { &*((*shrinker).private_data.cast::<T>()) };
    let mut ctl = unsafe { ShrinkControl::from_raw(sc) };

    data.count_objects(&mut ctl) as ffi::c_ulong
}

unsafe extern "C" fn scan_objects_shim<T: Shrinker>(
    shrinker: *mut bindings::shrinker,
    sc: *mut bindings::shrink_control,
) -> ffi::c_ulong {
    // SAFETY: see count_objects_shim().
    let data = unsafe { &*((*shrinker).private_data.cast::<T>()) };
    let mut ctl = unsafe { ShrinkControl::from_raw(sc) };

    data.scan_objects(&mut ctl) as ffi::c_ulong
}

/// A shrinker registered with the reclaim path.
///
/// Dropping this unregisters it and waits for any running callback to finish,
/// which is what makes it safe for T to be freed afterwards.
pub struct Registration<T: Shrinker> {
    shrinker: NonNull<bindings::shrinker>,
    // Heap-allocated so that the address stored in private_data stays valid
    // when `Registration` itself is moved.
    data: KBox<T>,
}

// SAFETY: Registration owns both the C shrinker and T; there is no
// unsynchronised access to either through &Registration.
unsafe impl<T: Shrinker + Send + Sync> Send for Registration<T> {}

// SAFETY: all shared access to T goes through the C callbacks, which the C side
// serialises against `shrinker_free()`; `&Registration` exposes nothing mutable.
unsafe impl<T: Shrinker + Send + Sync> Sync for Registration<T> {}

impl<T: Shrinker + Send + Sync> Registration<T> {
    /// Returns the shrinker object, e.g. so an owner can inspect its cache.
    pub fn data(&self) -> &T {
        &self.data
    }

    /// Allocates and registers a shrinker named name for data.
    pub fn new(name: &CStr, data: T) -> Result<Self> {
        Self::new_with_flags(name, data, 0)
    }

    /// Same as [`Registration::new`], with the given SHRINKER_* flags.
    ///
    /// The flags must match what the callbacks expect, i.e. SHRINKER_NUMA_AWARE
    /// only if they honour sc.nid().
    pub fn new_with_flags(name: &CStr, data: T, flags: u32) -> Result<Self> {
        // SHRINKER_REGISTERED and SHRINKER_ALLOCATED are owned by the core;
        // accepting them from a caller would corrupt its bookkeeping.
        if flags & !(SHRINKER_NUMA_AWARE | SHRINKER_MEMCG_AWARE | SHRINKER_NONSLAB) != 0 {
            return Err(EINVAL);
        }

        let data = KBox::new(data, GFP_KERNEL)?;

        // SAFETY: "%s" with a NUL-terminated string is a valid format/argument
        // pair for the kvasprintf_const() underneath.
        let fmt: *const ffi::c_char = c"%s".as_ptr().cast();
        let name_ptr: *const ffi::c_char = name.as_ptr().cast();
        let shrinker = unsafe { bindings::shrinker_alloc(flags, fmt, name_ptr) };
        let shrinker = NonNull::new(shrinker).ok_or(ENOMEM)?;

        // SAFETY: shrinker is freshly allocated and not registered yet, so
        // these fields can be written without racing the reclaim path.
        unsafe {
            let raw = shrinker.as_ptr();
            (*raw).count_objects = Some(count_objects_shim::<T>);
            (*raw).scan_objects = Some(scan_objects_shim::<T>);
            (*raw).private_data = (&*data as *const T).cast_mut().cast();
            bindings::shrinker_register(raw);
        }

        Ok(Self { shrinker, data })
    }
}

impl<T: Shrinker> Drop for Registration<T> {
    fn drop(&mut self) {
        // SAFETY: the shrinker was registered above. After this returns no
        // callback can be running or started again, so dropping data
        // afterwards is sound.
        unsafe { bindings::shrinker_free(self.shrinker.as_ptr()) };
    }
}
