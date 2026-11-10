// SPDX-License-Identifier: GPL-2.0

//! Rust shrinker sample.
//!
//! Registers a shrinker that reports and releases cached objects, so that the
//! reclaim path can query it and make progress under memory pressure. Each
//! unit stands for one cached object; the sample deliberately keeps no
//! allocation in the reclaim path.

use core::sync::atomic::{AtomicUsize, Ordering};

use kernel::{
    prelude::*,
    shrinker::{Registration, ShrinkControl, Shrinker, SHRINK_EMPTY},
};

module! {
    type: RustShrinker,
    name: "rust_shrinker",
    authors: ["Rust for Linux Contributors"],
    description: "Rust shrinker sample",
    license: "GPL",
}

/// Number of cached objects the sample starts with.
const NR_OBJECTS: usize = 64;

/// Cache whose objects can be released by the reclaim path.
struct Cache {
    nr_free: AtomicUsize,
}

impl Shrinker for Cache {
    fn count_objects(&self, _sc: &mut ShrinkControl<'_>) -> usize {
        match self.nr_free.load(Ordering::Relaxed) {
            0 => SHRINK_EMPTY,
            nr => nr,
        }
    }

    fn scan_objects(&self, sc: &mut ShrinkControl<'_>) -> usize {
        let nr_to_scan = sc.nr_to_scan();
        let mut nr_free = self.nr_free.load(Ordering::Relaxed);

        loop {
            let freed = nr_to_scan.min(nr_free);

            match self.nr_free.compare_exchange_weak(
                nr_free,
                nr_free - freed,
                Ordering::Relaxed,
                Ordering::Relaxed,
            ) {
                Ok(_) => {
                    sc.set_nr_scanned(freed);
                    return freed;
                }
                Err(actual) => nr_free = actual,
            }
        }
    }
}

struct RustShrinker {
    _shrinker: Registration<Cache>,
}

impl kernel::Module for RustShrinker {
    fn init(_module: &'static ThisModule) -> Result<Self> {
        let cache = Cache {
            nr_free: AtomicUsize::new(NR_OBJECTS),
        };

        let shrinker = Registration::new(c"rust_shrinker_sample", cache)?;

        pr_info!("Rust shrinker sample registered with {} objects\n", NR_OBJECTS);

        Ok(RustShrinker { _shrinker: shrinker })
    }
}

impl Drop for RustShrinker {
    fn drop(&mut self) {
        pr_info!("Rust shrinker sample unregistered\n");
    }
}
