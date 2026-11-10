// SPDX-License-Identifier: GPL-2.0

//! A sample for per-CPU variables and raw spinlocks.

use kernel::prelude::*;
use kernel::sync::{
    new_raw_spinlock, new_raw_spinlock_bh, new_raw_spinlock_irqsave, RawSpinLock, RawSpinLockBh,
    RawSpinLockIrqSave,
};

kernel::define_percpu!(
    /// Number of times `init` ran on this CPU.
    static ENTRIES: u64 = 0;
);

module! {
    type: RustPerCpu,
    name: "rust_percpu",
    authors: ["Rust for Linux Contributors"],
    description: "Rust per-CPU and raw spinlock sample",
    license: "GPL",
}

struct RustPerCpu {
    counter: Pin<KBox<RawSpinLock<u64>>>,
    irqsave: Pin<KBox<RawSpinLockIrqSave<u64>>>,
    bh: Pin<KBox<RawSpinLockBh<u64>>>,
}

impl kernel::Module for RustPerCpu {
    fn init(_module: &'static ThisModule) -> Result<Self> {
        let seen = {
            let this_cpu = ENTRIES.this_cpu();
            this_cpu.inc_return()
        };

        let counter = KBox::pin_init(new_raw_spinlock!(seen), GFP_KERNEL)?;
        let irqsave = KBox::pin_init(new_raw_spinlock_irqsave!(0u64), GFP_KERNEL)?;
        let bh = KBox::pin_init(new_raw_spinlock_bh!(0u64), GFP_KERNEL)?;

        {
            let mut value = counter.lock();
            *value *= 2;
        }

        // Exercise the irqsave and bh variants so their helpers are instantiated.
        {
            let mut value = irqsave.lock_irqsave();
            *value = seen;
        }
        let irqsave_seen = *irqsave.lock_irqsave();

        let bh_seen = if let Some(mut value) = bh.try_lock_bh() {
            *value = irqsave_seen + 1;
            *value
        } else {
            0
        };

        pr_info!(
            "rust_percpu: {seen} entries on this CPU, counter = {}, \
             irqsave = {irqsave_seen}, bh = {bh_seen}\n",
            *counter.lock()
        );

        Ok(RustPerCpu {
            counter,
            irqsave,
            bh,
        })
    }
}

impl Drop for RustPerCpu {
    fn drop(&mut self) {
        pr_info!(
            "rust_percpu: final counter = {}, irqsave = {}, bh = {}\n",
            *self.counter.lock(),
            *self.irqsave.lock_irqsave(),
            *self.bh.lock_bh()
        );
    }
}
