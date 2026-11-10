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
    /// The core resets it to nr_to_scan before each scan_objects() call
    /// (mm/shrinker.c:444) and uses what is left behind to account deferred
    /// work, so a shrinker that scans fewer objects than requested must update
    /// it. It is *not* initialised before count_objects(); a value read there
    /// is left over from the previous scan.
    pub fn nr_scanned(&self) -> usize {
        self.inner.nr_scanned
    }

    /// Sets how many objects the shrinker examined.
    ///
    /// # Contract
    ///
    /// `do_shrink_slab()` subtracts this from its remaining scan budget
    /// (`total_scan -= shrinkctl->nr_scanned`, mm/shrinker.c:451) and keeps
    /// calling `scan_objects()` until that budget is spent. Returning 0 freed
    /// therefore **must not** be paired with `nr_scanned == 0`: the budget
    /// would never shrink and reclaim would spin. Two correct patterns:
    ///
    /// - report the range that was examined, e.g.
    ///   `sc.set_nr_scanned(sc.nr_to_scan())` when the pass looked at
    ///   everything it was asked for (this is also what a bounded scan of an
    ///   already exhausted cache should report);
    /// - or return [`SHRINK_STOP`] from `scan_objects()` to leave the loop.
    ///
    /// Reporting the number of *freed* objects is only correct when every
    /// examined object was in fact freed, and it then **must** return
    /// [`SHRINK_STOP`] when `freed == 0` (cf. `sc->nr_scanned = num_freed`
    /// followed by `return num_freed ?: SHRINK_STOP`,
    /// drivers/gpu/drm/ttm/ttm_pool.c:1316-1318).
    ///
    /// Not calling this is equivalent to passing `nr_to_scan`, which is what
    /// `do_shrink_slab()` pre-loads (mm/shrinker.c:444); passing the number
    /// of objects *freed* instead makes reclaim spin whenever that number
    /// is 0.
    ///
    /// The `kernel` crate itself never calls this; it exists so that a
    /// shrinker can report a partial scan the way a C shrinker can.
    ///
    /// The value is not clamped: silently fixing it up would hide the
    /// mistake from the implementation that has to reason about it.
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
/// which is what makes it safe for T to be freed afterwards. `shrinker_free()`
/// takes a mutex and waits for a completion, so **dropping may sleep**: it must
/// not happen in atomic context, and must not happen from inside a reclaim
/// callback (that would deadlock against the registry lock).
///
/// Two paths can reach the callbacks and both are quiesced before `T` is
/// freed: reclaim holds a reference (`shrinker_try_get()`/`shrinker_put()`,
/// waited on through `wait_for_completion(&shrinker->done)`), while the
/// debugfs counters call `count_objects()` directly *without* taking that
/// reference. There the quiescing is done by debugfs itself: opening a
/// counter takes the debugfs active reference in the core's
/// `full_proxy_open()` (`fs/debugfs/file.c:441`, via
/// `__debugfs_file_get()`), and teardown goes through
/// `shrinker_debugfs_remove()` (`mm/shrinker_debug.c:241`) which calls
/// `debugfs_remove_recursive()`.
///
/// `seeks` and `batch` are inherited from the C defaults (2 and
/// `SHRINK_BATCH`) and are not exposed yet; "not set" does not mean "no
/// limit".
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
    pub fn new(name: &CStr, data: T) -> Result<Self, Error> {
        Self::new_with_flags(name, data, 0)
    }

    /// Same as [`Registration::new`], with the given SHRINKER_* flags.
    ///
    /// SHRINKER_NUMA_AWARE may be passed only if the callbacks honour
    /// `ShrinkControl::nid()`. SHRINKER_MEMCG_AWARE is rejected for now: the
    /// core calls a memcg-aware shrinker once per memory cgroup
    /// (`shrink_slab_memcg()` in `mm/shrinker.c`), and this abstraction has no
    /// way to tell the callbacks which cgroup they are shrinking, so declaring
    /// the capability would scan one global cache once per cgroup.
    pub fn new_with_flags(name: &CStr, data: T, flags: u32) -> Result<Self, Error> {
        // Whitelist: SHRINKER_NUMA_AWARE and SHRINKER_NONSLAB are the only
        // flags this abstraction can honour today. Everything else --
        // including the core-owned SHRINKER_REGISTERED/SHRINKER_ALLOCATED bits
        // and SHRINKER_MEMCG_AWARE, which cannot be supported without a memcg
        // accessor -- is rejected instead of being forwarded blindly.
        if flags & !(SHRINKER_NUMA_AWARE | SHRINKER_NONSLAB) != 0 {
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
