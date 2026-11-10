// SPDX-License-Identifier: GPL-2.0

//! Per-CPU variables.
//!
//! C equivalent: `DEFINE_PER_CPU()` / `this_cpu_*()` (`include/linux/percpu-defs.h`).
//!
//! [`ThisCpu`] disables preemption for its lifetime, which is the precondition the C accessors
//! rely on; the operations it exposes call the C macros, so their semantics (including
//! single-instruction atomicity) are unchanged.
//!
//! Not yet modelled: a safe wrapper for `DECLARE_PER_CPU()`. A C-defined per-CPU variable can be
//! referenced from Rust today through an `extern "C"` static plus `PerCpu::get`/`this_cpu`, but
//! there is no typed declaration helper yet. Also missing: `this_cpu_*` operations beyond the
//! u32/u64 types implemented below.
//!
//! One constraint from `percpu-defs.h:60-66` applies here too: the symbol must be globally
//! unique, which a Rust static already is.
//!
//! `PER_CPU_ATTRIBUTES` is different: on LoongArch it is `model("extreme")`
//! (`arch/loongarch/include/asm/percpu.h:15-21`, only for `MODULE && CONFIG_AS_HAS_EXPLICIT_RELOCS
//! && CONFIG_64BIT`), which changes how the address of a per-CPU variable is formed. A Rust
//! static cannot express it and the compiler would not warn, so `define_percpu!` refuses to
//! expand on that architecture altogether -- stricter than C (which still allows the built-in
//! case), because silently computing wrong addresses is worse than not building.

use crate::types::NotThreadSafe;
use core::cell::UnsafeCell;
use core::marker::PhantomData;

/// Defines a per-CPU variable, like `DEFINE_PER_CPU()` (`percpu-defs.h:108`).
///
/// The variable is placed in the `.data..percpu` section by the compiler, which is what makes the
/// kernel give every CPU its own copy.
#[macro_export]
macro_rules! define_percpu {
    ($(#[$attr:meta])* $vis:vis static $name:ident: $ty:ty = $init:expr;) => {
        #[cfg(target_arch = "loongarch64")]
        compile_error!("per-cpu variables need PER_CPU_ATTRIBUTES on this architecture");
        $(#[$attr])*
        #[link_section = ".data..percpu"]
        $vis static $name: $crate::percpu::PerCpu<$ty> =
            // SAFETY: The attribute above places the object in the `.data..percpu` section,
            // which is the invariant required by `PerCpu`.
            unsafe { $crate::percpu::PerCpu::new($init) };
    };
}

/// A per-CPU variable.
///
/// # Invariants
///
/// The object lives in the `.data..percpu` section, so the kernel maps one copy per CPU and the
/// per-CPU offset arithmetic applies to its address.
#[repr(transparent)]
pub struct PerCpu<T>(UnsafeCell<T>);

// SAFETY: Every CPU owns a separate copy, and the only safe accessors resolve to the copy of the
// CPU that currently runs the code; the object itself is never written through a shared
// reference from another CPU.
unsafe impl<T: Send> Sync for PerCpu<T> {}

impl<T> PerCpu<T> {
    /// Creates the initial value of a per-CPU variable.
    ///
    /// # Safety
    ///
    /// The created object must be placed in the `.data..percpu` section. Use [`define_percpu!`],
    /// which guarantees it.
    #[inline]
    pub const unsafe fn new(value: T) -> Self {
        Self(UnsafeCell::new(value))
    }

    /// Returns a pointer to the copy of the given CPU, like `per_cpu_ptr()` (`percpu-defs.h:232`).
    ///
    /// # Safety
    ///
    /// `cpu` must be a valid CPU number, and the caller must ensure that the returned pointer is
    /// not used concurrently with other accesses to that copy.
    #[inline]
    pub unsafe fn get(&self, cpu: u32) -> *mut T {
        // SAFETY: The caller guarantees that `cpu` is a valid CPU number.
        unsafe { bindings::per_cpu_ptr(self.0.get().cast(), cpu) }.cast()
    }

    /// Returns a handle that operates on the copy of the current CPU.
    ///
    /// Preemption is disabled until the handle is dropped.
    #[inline]
    pub fn this_cpu(&self) -> ThisCpu<'_, T> {
        ThisCpu::new(self)
    }
}

/// A handle to the per-CPU copy of the current CPU.
///
/// The handle disables preemption, so the current CPU cannot change while it is alive. It exposes
/// operations rather than references, because a per-CPU variable can be accessed again from an
/// interrupt handler running on the same CPU; handing out `&T`/`&mut T` would allow aliasing.
///
/// The handle is not [`Send`]: it belongs to the CPU it was created on.
pub struct ThisCpu<'a, T> {
    ptr: *mut T,
    _not_send: NotThreadSafe,
    _p: PhantomData<&'a PerCpu<T>>,
}

impl<'a, T> ThisCpu<'a, T> {
    #[inline]
    fn new(pcp: &'a PerCpu<T>) -> Self {
        // SAFETY: `preempt_disable()` is safe to call, and it is re-enabled in `Drop`.
        unsafe { bindings::preempt_disable() };

        Self {
            ptr: pcp.0.get(),
            _not_send: NotThreadSafe,
            _p: PhantomData,
        }
    }
}

impl<T> Drop for ThisCpu<'_, T> {
    #[inline]
    fn drop(&mut self) {
        // SAFETY: Balanced with the `preempt_disable()` in `new()`.
        unsafe { bindings::preempt_enable() };
    }
}

macro_rules! impl_this_cpu {
    ($ty:ty, $read:ident, $write:ident, $add:ident, $inc:ident, $dec:ident,
     $inc_return:ident, $dec_return:ident, $add_return:ident) => {
        impl ThisCpu<'_, $ty> {
            /// Reads the copy of the current CPU (`this_cpu_read()`).
            #[inline]
            pub fn read(&self) -> $ty {
                // SAFETY: Preemption is disabled for as long as the handle exists, and the pointer
                // refers to a per-CPU object of this type.
                unsafe { bindings::$read(self.ptr) }
            }

            /// Writes the copy of the current CPU (`this_cpu_write()`).
            #[inline]
            pub fn write(&self, val: $ty) {
                // SAFETY: See `read()`.
                unsafe { bindings::$write(self.ptr, val) }
            }

            /// Adds to the copy of the current CPU (`this_cpu_add()`).
            #[inline]
            pub fn add(&self, val: $ty) {
                // SAFETY: See `read()`.
                unsafe { bindings::$add(self.ptr, val) }
            }

            /// Increments the copy of the current CPU (`this_cpu_inc()`).
            #[inline]
            pub fn inc(&self) {
                // SAFETY: See `read()`.
                unsafe { bindings::$inc(self.ptr) }
            }

            /// Decrements the copy of the current CPU (`this_cpu_dec()`).
            #[inline]
            pub fn dec(&self) {
                // SAFETY: See `read()`.
                unsafe { bindings::$dec(self.ptr) }
            }

            /// Increments and returns the new value (`this_cpu_inc_return()`).
            #[inline]
            pub fn inc_return(&self) -> $ty {
                // SAFETY: See `read()`.
                unsafe { bindings::$inc_return(self.ptr) }
            }

            /// Decrements and returns the new value (`this_cpu_dec_return()`).
            #[inline]
            pub fn dec_return(&self) -> $ty {
                // SAFETY: See `read()`.
                unsafe { bindings::$dec_return(self.ptr) }
            }

            /// Adds and returns the new value (`this_cpu_add_return()`).
            #[inline]
            pub fn add_return(&self, val: $ty) -> $ty {
                // SAFETY: See `read()`.
                unsafe { bindings::$add_return(self.ptr, val) }
            }
        }
    };
}

impl_this_cpu!(
    u32,
    this_cpu_read_u32,
    this_cpu_write_u32,
    this_cpu_add_u32,
    this_cpu_inc_u32,
    this_cpu_dec_u32,
    this_cpu_inc_return_u32,
    this_cpu_dec_return_u32,
    this_cpu_add_return_u32
);

impl_this_cpu!(
    u64,
    this_cpu_read_u64,
    this_cpu_write_u64,
    this_cpu_add_u64,
    this_cpu_inc_u64,
    this_cpu_dec_u64,
    this_cpu_inc_return_u64,
    this_cpu_dec_return_u64,
    this_cpu_add_return_u64
);
