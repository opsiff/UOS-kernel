// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2025 Google LLC.

use super::{
    BinaryReader,
    BinaryWriter,
    Reader,
    Writer, //
};

use crate::{
    alloc::{flags::GFP_KERNEL, KBox},
    debugfs::callback_adapters::Adapter,
    fmt,
    fs::file,
    prelude::*,
    seq_file::SeqFile,
    seq_print,
    uaccess::UserSlice, //
};

use core::marker::PhantomData;

#[cfg(CONFIG_DEBUG_FS)]
use core::ops::Deref;

/// # Invariant
///
/// `FileOps<T>` will always contain an `operations` which is safe to use for a file backed
/// off an inode which has a pointer to a `T` in its private data that is safe to convert
/// into a reference.
pub(super) struct FileOps<T> {
    #[cfg(CONFIG_DEBUG_FS)]
    operations: bindings::file_operations,
    #[cfg(CONFIG_DEBUG_FS)]
    mode: u16,
    _phantom: PhantomData<T>,
}

impl<T> FileOps<T> {
    /// # Safety
    ///
    /// The caller asserts that the provided `operations` is safe to use for a file whose
    /// inode has a pointer to `T` in its private data that is safe to convert into a reference.
    const unsafe fn new(operations: bindings::file_operations, mode: u16) -> Self {
        Self {
            #[cfg(CONFIG_DEBUG_FS)]
            operations,
            #[cfg(CONFIG_DEBUG_FS)]
            mode,
            _phantom: PhantomData,
        }
    }

    #[cfg(CONFIG_DEBUG_FS)]
    pub(crate) const fn mode(&self) -> u16 {
        self.mode
    }
}

impl<T: Adapter> FileOps<T> {
    pub(super) const fn adapt(&self) -> &FileOps<T::Inner> {
        // SAFETY: `Adapter` asserts that `T` can be legally cast to `T::Inner`.
        unsafe { core::mem::transmute(self) }
    }
}

#[cfg(CONFIG_DEBUG_FS)]
impl<T> Deref for FileOps<T> {
    type Target = bindings::file_operations;

    fn deref(&self) -> &Self::Target {
        &self.operations
    }
}

struct WriterAdapter<T>(T);

impl<'a, T: Writer> fmt::Display for WriterAdapter<&'a T> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.0.write(f)
    }
}

/// Implements `open` for `file_operations` via `single_open` to fill out a `seq_file`.
///
/// # Safety
///
/// * `inode`'s private pointer must point to a value of type `T` which will outlive the `inode`
///   and will not have any unique references alias it during the call.
/// * `file` must point to a live, not-yet-initialized file object.
unsafe extern "C" fn writer_open<T: Writer + Sync>(
    inode: *mut bindings::inode,
    file: *mut bindings::file,
) -> c_int {
    // SAFETY: The caller ensures that `inode` is a valid pointer.
    let data = unsafe { (*inode).i_private };
    // SAFETY:
    // * `file` is acceptable by caller precondition.
    // * `print_act` will be called on a `seq_file` with private data set to the third argument,
    //   so we meet its safety requirements.
    // * The `data` pointer passed in the third argument is a valid `T` pointer that outlives
    //   this call by caller preconditions.
    unsafe { bindings::single_open(file, Some(writer_act::<T>), data) }
}

/// Prints private data stashed in a seq_file to that seq file.
///
/// # Safety
///
/// `seq` must point to a live `seq_file` whose private data is a valid pointer to a `T` which may
/// not have any unique references alias it during the call.
unsafe extern "C" fn writer_act<T: Writer + Sync>(
    seq: *mut bindings::seq_file,
    _: *mut c_void,
) -> c_int {
    // SAFETY: By caller precondition, this pointer is valid pointer to a `T`, and
    // there are not and will not be any unique references until we are done.
    let data = unsafe { &*((*seq).private.cast::<T>()) };
    // SAFETY: By caller precondition, `seq_file` points to a live `seq_file`, so we can lift
    // it.
    let seq_file = unsafe { SeqFile::from_raw(seq) };
    seq_print!(seq_file, "{}", WriterAdapter(data));
    0
}

// Work around lack of generic const items.
pub(crate) trait ReadFile<T> {
    const FILE_OPS: FileOps<T>;
}

impl<T: Writer + Sync> ReadFile<T> for T {
    const FILE_OPS: FileOps<T> = {
        let operations = bindings::file_operations {
            read: Some(bindings::seq_read),
            llseek: Some(bindings::seq_lseek),
            release: Some(bindings::single_release),
            open: Some(writer_open::<Self>),
            ..pin_init::zeroed()
        };
        // SAFETY: `operations` is all stock `seq_file` implementations except for `writer_open`.
        // `open`'s only requirement beyond what is provided to all open functions is that the
        // inode's data pointer must point to a `T` that will outlive it, which matches the
        // `FileOps` requirements.
        unsafe { FileOps::new(operations, 0o400) }
    };
}

fn read<T: Reader + Sync>(data: &T, buf: *const c_char, count: usize) -> isize {
    let mut reader = UserSlice::new(UserPtr::from_ptr(buf as *mut c_void), count).reader();

    if let Err(e) = data.read_from_slice(&mut reader) {
        return e.to_errno() as isize;
    }

    count as isize
}

/// # Safety
///
/// `file` must be a valid pointer to a `file` struct.
/// The `private_data` of the file must contain a valid pointer to a `seq_file` whose
/// `private` data in turn points to a `T` that implements `Reader`.
/// `buf` must be a valid user-space buffer.
pub(crate) unsafe extern "C" fn write<T: Reader + Sync>(
    file: *mut bindings::file,
    buf: *const c_char,
    count: usize,
    _ppos: *mut bindings::loff_t,
) -> isize {
    // SAFETY: The file was opened with `single_open`, which sets `private_data` to a `seq_file`.
    let seq = unsafe { &mut *((*file).private_data.cast::<bindings::seq_file>()) };
    // SAFETY: By caller precondition, this pointer is live and points to a value of type `T`.
    let data = unsafe { &*(seq.private as *const T) };
    read(data, buf, count)
}

// A trait to get the file operations for a type.
pub(crate) trait ReadWriteFile<T> {
    const FILE_OPS: FileOps<T>;
}

impl<T: Writer + Reader + Sync> ReadWriteFile<T> for T {
    const FILE_OPS: FileOps<T> = {
        let operations = bindings::file_operations {
            open: Some(writer_open::<T>),
            read: Some(bindings::seq_read),
            write: Some(write::<T>),
            llseek: Some(bindings::seq_lseek),
            release: Some(bindings::single_release),
            ..pin_init::zeroed()
        };
        // SAFETY: `operations` is all stock `seq_file` implementations except for `writer_open`
        // and `write`.
        // `writer_open`'s only requirement beyond what is provided to all open functions is that
        // the inode's data pointer must point to a `T` that will outlive it, which matches the
        // `FileOps` requirements.
        // `write` only requires that the file's private data pointer points to `seq_file`
        // which points to a `T` that will outlive it, which matches what `writer_open`
        // provides.
        unsafe { FileOps::new(operations, 0o600) }
    };
}

/// # Safety
///
/// `inode` must be a valid pointer to an `inode` struct.
/// `file` must be a valid pointer to a `file` struct.
unsafe extern "C" fn write_only_open(
    inode: *mut bindings::inode,
    file: *mut bindings::file,
) -> c_int {
    // SAFETY: The caller ensures that `inode` and `file` are valid pointers.
    unsafe { (*file).private_data = (*inode).i_private };
    0
}

/// # Safety
///
/// * `file` must be a valid pointer to a `file` struct.
/// * The `private_data` of the file must contain a valid pointer to a `T` that implements
///   `Reader`.
/// * `buf` must be a valid user-space buffer.
pub(crate) unsafe extern "C" fn write_only_write<T: Reader + Sync>(
    file: *mut bindings::file,
    buf: *const c_char,
    count: usize,
    _ppos: *mut bindings::loff_t,
) -> isize {
    // SAFETY: The caller ensures that `file` is a valid pointer and that `private_data` holds a
    // valid pointer to `T`.
    let data = unsafe { &*((*file).private_data as *const T) };
    read(data, buf, count)
}

pub(crate) trait WriteFile<T> {
    const FILE_OPS: FileOps<T>;
}

impl<T: Reader + Sync> WriteFile<T> for T {
    const FILE_OPS: FileOps<T> = {
        let operations = bindings::file_operations {
            open: Some(write_only_open),
            write: Some(write_only_write::<T>),
            llseek: Some(bindings::noop_llseek),
            ..pin_init::zeroed()
        };
        // SAFETY:
        // * `write_only_open` populates the file private data with the inode private data
        // * `write_only_write`'s only requirement is that the private data of the file point to
        //   a `T` and be legal to convert to a shared reference, which `write_only_open`
        //   satisfies.
        unsafe { FileOps::new(operations, 0o200) }
    };
}

extern "C" fn blob_read<T: BinaryWriter>(
    file: *mut bindings::file,
    buf: *mut c_char,
    count: usize,
    ppos: *mut bindings::loff_t,
) -> isize {
    // SAFETY:
    // - `file` is a valid pointer to a `struct file`.
    // - The type invariant of `FileOps` guarantees that `private_data` points to a valid `T`.
    let this = unsafe { &*((*file).private_data.cast::<T>()) };

    // SAFETY:
    // - `ppos` is a valid `file::Offset` pointer.
    // - We have exclusive access to `ppos`.
    let pos: &mut file::Offset = unsafe { &mut *ppos };

    let mut writer = UserSlice::new(UserPtr::from_ptr(buf.cast()), count).writer();

    let ret = || -> Result<isize> {
        let written = this.write_to_slice(&mut writer, pos)?;

        Ok(written.try_into()?)
    }();

    match ret {
        Ok(n) => n,
        Err(e) => e.to_errno() as isize,
    }
}

/// Representation of [`FileOps`] for read only binary files.
pub(crate) trait BinaryReadFile<T> {
    const FILE_OPS: FileOps<T>;
}

impl<T: BinaryWriter + Sync> BinaryReadFile<T> for T {
    const FILE_OPS: FileOps<T> = {
        let operations = bindings::file_operations {
            read: Some(blob_read::<T>),
            llseek: Some(bindings::default_llseek),
            open: Some(bindings::simple_open),
            ..pin_init::zeroed()
        };

        // SAFETY:
        // - The private data of `struct inode` does always contain a pointer to a valid `T`.
        // - `simple_open()` stores the `struct inode`'s private data in the private data of the
        //   corresponding `struct file`.
        // - `blob_read()` re-creates a reference to `T` from the `struct file`'s private data.
        // - `default_llseek()` does not access the `struct file`'s private data.
        unsafe { FileOps::new(operations, 0o400) }
    };
}

extern "C" fn blob_write<T: BinaryReader>(
    file: *mut bindings::file,
    buf: *const c_char,
    count: usize,
    ppos: *mut bindings::loff_t,
) -> isize {
    // SAFETY:
    // - `file` is a valid pointer to a `struct file`.
    // - The type invariant of `FileOps` guarantees that `private_data` points to a valid `T`.
    let this = unsafe { &*((*file).private_data.cast::<T>()) };

    // SAFETY:
    // - `ppos` is a valid `file::Offset` pointer.
    // - We have exclusive access to `ppos`.
    let pos: &mut file::Offset = unsafe { &mut *ppos };

    let mut reader = UserSlice::new(UserPtr::from_ptr(buf.cast_mut().cast()), count).reader();

    let ret = || -> Result<isize> {
        let read = this.read_from_slice(&mut reader, pos)?;

        Ok(read.try_into()?)
    }();

    match ret {
        Ok(n) => n,
        Err(e) => e.to_errno() as isize,
    }
}

/// Representation of [`FileOps`] for write only binary files.
pub(crate) trait BinaryWriteFile<T> {
    const FILE_OPS: FileOps<T>;
}

impl<T: BinaryReader + Sync> BinaryWriteFile<T> for T {
    const FILE_OPS: FileOps<T> = {
        let operations = bindings::file_operations {
            write: Some(blob_write::<T>),
            llseek: Some(bindings::default_llseek),
            open: Some(bindings::simple_open),
            ..pin_init::zeroed()
        };

        // SAFETY:
        // - The private data of `struct inode` does always contain a pointer to a valid `T`.
        // - `simple_open()` stores the `struct inode`'s private data in the private data of the
        //   corresponding `struct file`.
        // - `blob_write()` re-creates a reference to `T` from the `struct file`'s private data.
        // - `default_llseek()` does not access the `struct file`'s private data.
        unsafe { FileOps::new(operations, 0o200) }
    };
}

/// Representation of [`FileOps`] for read/write binary files.
pub(crate) trait BinaryReadWriteFile<T> {
    const FILE_OPS: FileOps<T>;
}

impl<T: BinaryWriter + BinaryReader + Sync> BinaryReadWriteFile<T> for T {
    const FILE_OPS: FileOps<T> = {
        let operations = bindings::file_operations {
            read: Some(blob_read::<T>),
            write: Some(blob_write::<T>),
            llseek: Some(bindings::default_llseek),
            open: Some(bindings::simple_open),
            ..pin_init::zeroed()
        };

        // SAFETY:
        // - The private data of `struct inode` does always contain a pointer to a valid `T`.
        // - `simple_open()` stores the `struct inode`'s private data in the private data of the
        //   corresponding `struct file`.
        // - `blob_read()` re-creates a reference to `T` from the `struct file`'s private data.
        // - `blob_write()` re-creates a reference to `T` from the `struct file`'s private data.
        // - `default_llseek()` does not access the `struct file`'s private data.
        unsafe { FileOps::new(operations, 0o600) }
    };
}

// ---------------------------------------------------------------------------
// Traversal. C: fs/seq_file.c:58 seq_open(), :353 seq_release(),
// include/linux/seq_file.h:31 struct seq_operations, :38 SEQ_SKIP.
// ---------------------------------------------------------------------------

/// Outcome of [`SeqFileOps::show`] for a single record.
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum SeqShow {
    /// The record was written to the seq file.
    Written,
    /// Drop this record without emitting it (C: `show()` returns `SEQ_SKIP`).
    Skip,
}

/// C: `include/linux/seq_file.h:38`. Matched here rather than hard-coding the value, and kept
/// private so callers cannot return a magic number by accident.
const SEQ_SKIP: c_int = 1;

/// A seq file whose per-open state lives in `seq_file::private`.
///
/// Positions are `u64` indices chosen by the implementation. They must be strictly increasing
/// and start at 0, otherwise `seq_read` either loops or truncates the output.
pub trait SeqFileOps {
    /// State created by `open(2)` and destroyed by `close(2)`; one instance per open file.
    type Ctx;

    /// Called once per `open(2)`, before any `read(2)`.
    ///
    /// An error aborts the open; the context is then dropped without ever being published.
    fn open(&self) -> Result<Self::Ctx>;

    /// First record at or after `pos`, or `None` when the file is exhausted.
    fn start(&self, ctx: &mut Self::Ctx, pos: u64) -> Result<Option<u64>>;

    /// Record following `pos`, or `None` when the file is exhausted.
    fn next(&self, ctx: &mut Self::Ctx, pos: u64) -> Result<Option<u64>>;

    /// Called once for every record `show` was invoked on.
    fn stop(&self, _ctx: &mut Self::Ctx, _pos: u64) {}

    /// Emit the record at `pos`.
    fn show(&self, ctx: &mut Self::Ctx, seq: &SeqFile, pos: u64) -> Result<SeqShow>;
}

// Not `Clone` on purpose: the context may be moved but never duplicated, which makes a second
// `from_raw` at release time impossible by construction.
struct CtxHolder<T: SeqFileOps> {
    /// The `T` behind `inode::i_private`. It outlives every open file (see `ctx_open`).
    data: *const T,
    ctx: T::Ctx,
}

/// Encodes a position as the cookie that `seq_file` hands back to `next`/`stop`/`show`.
///
/// C treats the cookie as an opaque iterator; only the position is needed here, and `null` has
/// to mean "end of file", so the encoding is `pos + 1`.
#[inline]
fn cookie(pos: u64) -> *mut c_void {
    (pos as usize).wrapping_add(1) as *mut c_void
}

/// # Safety
///
/// `v` must come from [`cookie`].
#[inline]
unsafe fn cookie_pos(v: *mut c_void) -> u64 {
    (v as usize).wrapping_sub(1) as u64
}

/// # Safety
///
/// `m` must be a live seq_file whose `private` was set by `ctx_open::<T>`.
unsafe fn holder<'a, T: SeqFileOps>(m: *mut bindings::seq_file) -> Option<&'a mut CtxHolder<T>> {
    // SAFETY: By the caller precondition `private` holds a CtxHolder<T> allocated in ctx_open.
    let p = unsafe { (*m).private.cast::<CtxHolder<T>>() };
    // SAFETY: The holder lives until ctx_release, which cannot run while a read is in flight on
    // the same file.
    (!p.is_null()).then(|| unsafe { &mut *p })
}

/// # Safety
///
/// `m`/`pos` are the arguments seq_file passes to `start`.
unsafe extern "C" fn ctx_start<T: SeqFileOps + Sync>(
    m: *mut bindings::seq_file,
    pos: *mut bindings::loff_t,
) -> *mut c_void {
    let Some(h) = (unsafe { holder::<T>(m) }) else {
        return core::ptr::null_mut();
    };
    let at = unsafe { *pos } as u64;
    // SAFETY: `data` points at the `T` behind i_private, which outlives the file.
    let data = unsafe { &*h.data };

    match data.start(&mut h.ctx, at) {
        Ok(Some(p)) => {
            // SAFETY: Valid `pos` argument of seq_file.
            unsafe { *pos = p as bindings::loff_t };
            cookie(p)
        }
        Ok(None) => core::ptr::null_mut(),
        // seq_read() checks IS_ERR() on this return value and reports the error to userspace.
        Err(e) => unsafe { bindings::ERR_PTR(e.to_errno() as crate::ffi::c_long) },
    }
}

/// # Safety
///
/// `m`/`v`/`pos` are the arguments seq_file passes to `next`.
unsafe extern "C" fn ctx_next<T: SeqFileOps + Sync>(
    m: *mut bindings::seq_file,
    v: *mut c_void,
    pos: *mut bindings::loff_t,
) -> *mut c_void {
    let Some(h) = (unsafe { holder::<T>(m) }) else {
        return core::ptr::null_mut();
    };
    let at = unsafe { cookie_pos(v) };
    // SAFETY: `data` points at the `T` behind i_private, which outlives the file.
    let data = unsafe { &*h.data };

    match data.next(&mut h.ctx, at) {
        Ok(Some(p)) => {
            // SAFETY: Valid `pos` argument of seq_file.
            unsafe { *pos = p as bindings::loff_t };
            cookie(p)
        }
        Ok(None) => {
            // seq_file requires `next` to advance `*pos` even when it returns NULL;
            // otherwise the core warns "buggy .next function ... did not update
            // position index" (fs/seq_file.c). The output would still be right --
            // the core falls back to `m->index++` -- so the only symptom is the
            // warning. Advance past the position we just rejected, matching what
            // `single_next` does for single-entry files (`++*pos; return NULL;`).
            // SAFETY: Valid `pos` argument of seq_file.
            unsafe { *pos = at.wrapping_add(1) as bindings::loff_t };
            core::ptr::null_mut()
        }
        Err(e) => unsafe { bindings::ERR_PTR(e.to_errno() as crate::ffi::c_long) },
    }
}

/// # Safety
///
/// `m`/`v` are the arguments seq_file passes to `stop`.
unsafe extern "C" fn ctx_stop<T: SeqFileOps + Sync>(m: *mut bindings::seq_file, v: *mut c_void) {
    let Some(h) = (unsafe { holder::<T>(m) }) else {
        return;
    };
    // SAFETY: `data` points at the `T` behind i_private, which outlives the file.
    let data = unsafe { &*h.data };
    data.stop(&mut h.ctx, unsafe { cookie_pos(v) });
}

/// # Safety
///
/// `m`/`v` are the arguments seq_file passes to `show`.
unsafe extern "C" fn ctx_show<T: SeqFileOps + Sync>(
    m: *mut bindings::seq_file,
    v: *mut c_void,
) -> c_int {
    let Some(h) = (unsafe { holder::<T>(m) }) else {
        return EINVAL.to_errno();
    };
    // SAFETY: seq_file guarantees `m` is valid for the duration of `show`, and no other thread
    // touches it meanwhile.
    let seq = unsafe { SeqFile::from_raw(m) };
    // SAFETY: `data` points at the `T` behind i_private, which outlives the file.
    let data = unsafe { &*h.data };

    match data.show(&mut h.ctx, seq, unsafe { cookie_pos(v) }) {
        Ok(SeqShow::Written) => 0,
        Ok(SeqShow::Skip) => SEQ_SKIP,
        Err(e) => e.to_errno(),
    }
}

/// # Safety
///
/// inode must be a valid inode whose i_private points to a live T outliving the file.
/// file must be a live, not yet opened file.
unsafe extern "C" fn ctx_open<T: SeqFileOps + Sync>(
    inode: *mut bindings::inode,
    file: *mut bindings::file,
) -> c_int {
    // SAFETY: By caller precondition i_private points to a T that outlives the file.
    let data = unsafe { &*((*inode).i_private.cast::<T>()) };

    let ctx = match data.open() {
        Ok(ctx) => ctx,
        Err(e) => return e.to_errno(),
    };

    let holder = CtxHolder {
        data: data as *const T,
        ctx,
    };

    // Allocation, not publication: on any failure below the context is dropped here.
    let holder = match KBox::new(holder, GFP_KERNEL) {
        Ok(h) => h,
        Err(_) => return ENOMEM.to_errno(),
    };

    // SAFETY: T::SEQ_OPS is a compile-time table whose trampolines only touch T's context, and
    // file is a live file.
    let ret = unsafe { bindings::seq_open(file, &T::SEQ_OPS) };
    if ret != 0 {
        // holder still owns the context, so it is freed exactly once.
        return ret;
    }

    // SAFETY: seq_open succeeded, so private_data is a live seq_file owned by file.
    let seq = unsafe { (*file).private_data.cast::<bindings::seq_file>() };

    // Ownership moves to seq_file::private; this is the single owning pointer.
    //
    // SAFETY: seq is live and private is currently unused (seq_open clears it).
    unsafe { (*seq).private = KBox::into_raw(holder).cast::<c_void>() };

    0
}

/// # Safety
///
/// inode/file must be a pair previously opened by ctx_open::<T>.
unsafe extern "C" fn ctx_release<T: SeqFileOps + Sync>(
    inode: *mut bindings::inode,
    file: *mut bindings::file,
) -> c_int {
    // SAFETY: By caller precondition the file was opened by ctx_open.
    let seq = unsafe { (*file).private_data.cast::<bindings::seq_file>() };

    if !seq.is_null() {
        // SAFETY: private was set by ctx_open.
        let ctx = unsafe { (*seq).private.cast::<CtxHolder<T>>() };

        if !ctx.is_null() {
            // The only from_raw in this file: ownership returns here exactly once.
            //
            // SAFETY: ctx came from KBox::into_raw in ctx_open and has not been freed.
            unsafe { drop(KBox::from_raw(ctx)) };

            // Clear the dangling pointer so a re-entrant release cannot see it.
            //
            // SAFETY: seq is still live at this point.
            unsafe { (*seq).private = core::ptr::null_mut() };
        }
    }

    // Must run after the context is dropped: seq_release frees the seq_file itself, so reading
    // seq->private afterwards would be a use-after-free.
    //
    // SAFETY: file was opened with seq_open, which is what seq_release expects.
    unsafe { bindings::seq_release(inode, file) }
}

pub(crate) trait ContextFile<T> {
    const FILE_OPS: FileOps<T>;
    const SEQ_OPS: bindings::seq_operations;
}

impl<T: SeqFileOps + Sync> ContextFile<T> for T {
    const FILE_OPS: FileOps<T> = {
        let operations = bindings::file_operations {
            open: Some(ctx_open::<T>),
            read: Some(bindings::seq_read),
            llseek: Some(bindings::seq_lseek),
            release: Some(ctx_release::<T>),
            ..pin_init::zeroed()
        };
        // SAFETY: open/release are the pair implemented above and only rely on i_private
        // pointing at a T that outlives the file, which is the FileOps invariant.
        unsafe { FileOps::new(operations, 0o400) }
    };

    const SEQ_OPS: bindings::seq_operations = bindings::seq_operations {
        start: Some(ctx_start::<T>),
        next: Some(ctx_next::<T>),
        stop: Some(ctx_stop::<T>),
        show: Some(ctx_show::<T>),
    };
}
