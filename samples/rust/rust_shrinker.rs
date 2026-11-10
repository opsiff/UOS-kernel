// SPDX-License-Identifier: GPL-2.0

//! Rust shrinker sample.
//!
//! Registers a shrinker that reports and releases cached objects, so that the
//! reclaim path can query it and make progress under memory pressure. Each
//! unit stands for one cached object; the sample deliberately keeps no
//! allocation in the reclaim path.

use core::sync::atomic::{AtomicUsize, Ordering};

use kernel::{
    mm::shrinker::{Registration, ShrinkControl, Shrinker, SHRINK_EMPTY},
    prelude::*,
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

/// Number of callbacks to trace; enough to prove reclaim reached us.
const TRACE_CALLS: usize = 8;

/// Upper bound on objects released per `scan_objects()` call.
///
/// Deliberately smaller than a typical `nr_to_scan` batch so that the
/// partial-scan path (`nr_scanned < nr_to_scan`) is exercised at run time,
/// not just documented.
const MAX_PER_CALL: usize = 32;

/// Cache whose objects can be released by the reclaim path.
struct Cache {
    nr_free: AtomicUsize,
    count_calls: AtomicUsize,
    scan_calls: AtomicUsize,
}

impl Shrinker for Cache {
    fn count_objects(&self, _sc: &mut ShrinkControl<'_>) -> usize {
        let nr_free = self.nr_free.load(Ordering::Relaxed);
        let call = self.count_calls.fetch_add(1, Ordering::Relaxed);

        if call < TRACE_CALLS {
            pr_info!("rust_shrinker: count_objects #{} nr_free={}\n", call, nr_free);
        }

        if nr_free == 0 {
            SHRINK_EMPTY
        } else {
            nr_free
        }
    }

    fn scan_objects(&self, sc: &mut ShrinkControl<'_>) -> usize {
        let nr_to_scan = sc.nr_to_scan();
        let mut nr_free = self.nr_free.load(Ordering::Relaxed);

        loop {
            let freed = nr_to_scan.min(nr_free).min(MAX_PER_CALL);

            match self.nr_free.compare_exchange_weak(
                nr_free,
                nr_free - freed,
                Ordering::Relaxed,
                Ordering::Relaxed,
            ) {
                Ok(_) => {
                    // Report the number of objects *examined*, not the number
                    // freed: do_shrink_slab() subtracts nr_scanned from its
                    // remaining budget, so reporting 0 on an exhausted cache
                    // keeps the loop running forever. When nothing is left,
                    // report the whole requested range so the budget still
                    // shrinks and the loop terminates.
                    let scanned = if freed == 0 { nr_to_scan } else { freed };
                    sc.set_nr_scanned(scanned);

                    let call = self.scan_calls.fetch_add(1, Ordering::Relaxed);
                    if call < TRACE_CALLS {
                        pr_info!(
                            "rust_shrinker: scan_objects #{} to_scan={} scanned={} freed={}\n",
                            call,
                            nr_to_scan,
                            scanned,
                            freed
                        );
                    }

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
            count_calls: AtomicUsize::new(0),
            scan_calls: AtomicUsize::new(0),
        };

        let shrinker = Registration::new(c"rust_shrinker_sample", cache)?;

        pr_info!("rust_shrinker: registered with {} objects\n", NR_OBJECTS);

        Ok(RustShrinker { _shrinker: shrinker })
    }
}

impl Drop for RustShrinker {
    fn drop(&mut self) {
        pr_info!("rust_shrinker: unregistered\n");
    }
}
