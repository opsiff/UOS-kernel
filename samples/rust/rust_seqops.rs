// SPDX-License-Identifier: GPL-2.0

//! Self test for S2: traversal (start/next/stop/show) over a small node tree.
//!
//! Mirrors the gcov shape: `open` walks the tree once to compute the traversal order and
//! per-node subtree sizes; `show` only prints. One node is named with a `skip` prefix and
//! exercises the `SEQ_SKIP` path.

use kernel::debugfs::{Dir, Scope, SeqFileOps, SeqShow};
use kernel::prelude::*;
use kernel::seq_file::SeqFile;

module! {
    type: RustSeqOps,
    name: "rust_seqops",
    authors: ["Rust for Linux Contributors"],
    description: "S2 seq_file traversal self test",
    license: "GPL",
}

/// A node of the demo tree. Children are indices into the same array.
struct Node {
    name: &'static str,
    kids: &'static [u32],
    self_size: u64,
}

const TREE: &[Node] = &[
    Node { name: "root", kids: &[1, 2, 3], self_size: 4 },
    Node { name: "lib", kids: &[4, 5], self_size: 8 },
    Node { name: "skip-me", kids: &[], self_size: 16 },
    Node { name: "kernel", kids: &[], self_size: 32 },
    Node { name: "a.o", kids: &[], self_size: 64 },
    Node { name: "b.o", kids: &[], self_size: 128 },
];

#[pin_data]
struct Ops {}

impl SeqFileOps for Ops {
    /// Per-open state: flat order, and the subtree size of every node in that order.
    type Ctx = (KVec<u64>, KVec<u64>);

    fn open(&self) -> Result<Self::Ctx> {
        let mut order = KVec::with_capacity(TREE.len(), GFP_KERNEL)?;
        let mut stack: KVec<u32> = KVec::with_capacity(TREE.len(), GFP_KERNEL)?;

        stack.push(0, GFP_KERNEL)?;
        while let Some(idx) = stack.pop() {
            order.push(idx as u64, GFP_KERNEL)?;
            for kid in TREE[idx as usize].kids.iter().rev() {
                stack.push(*kid, GFP_KERNEL)?;
            }
        }

        // Pass 1 of the two-pass scheme: subtree sizes for the whole traversal order.
        //
        // Pre-allocate and assign by *position*, not by insertion order: `total[kpos]`
        // below is indexed with a position in `order`, so `total` must already be laid
        // out that way. Building it with `push` while walking backwards put the first
        // computed value at index 0 regardless of the position it describes, which made
        // the child lookup read the wrong slot (and overflow once the last node had kids).
        let mut total: KVec<u64> = KVec::from_elem(0u64, order.len(), GFP_KERNEL)?;
        for pos in (0..order.len()).rev() {
            let node = &TREE[order[pos] as usize];
            let mut sum = node.self_size;
            for kid in node.kids {
                for kpos in 0..order.len() {
                    if order[kpos] == *kid as u64 {
                        sum += total[kpos];
                        break;
                    }
                }
            }
            total[pos] = sum;
        }

        pr_info!("rust_seqops: opened with {} nodes\n", order.len());
        Ok((order, total))
    }

    fn start(&self, ctx: &mut Self::Ctx, pos: u64) -> Result<Option<u64>> {
        Ok((pos < ctx.0.len() as u64).then_some(pos))
    }

    fn next(&self, ctx: &mut Self::Ctx, pos: u64) -> Result<Option<u64>> {
        let next = pos + 1;
        Ok((next < ctx.0.len() as u64).then_some(next))
    }

    fn show(&self, ctx: &mut Self::Ctx, seq: &SeqFile, pos: u64) -> Result<SeqShow> {
        let idx = ctx.0[pos as usize];
        let node = &TREE[idx as usize];
        let size = ctx.1[pos as usize];

        if node.name.starts_with("skip") {
            return Ok(SeqShow::Skip);
        }
        kernel::seq_print!(seq, "{} {}\n", node.name, size);
        Ok(SeqShow::Written)
    }
}

struct RustSeqOps {
    _scope: Pin<KBox<Scope<Ops>>>,
}

impl kernel::Module for RustSeqOps {
    fn init(_module: &'static ThisModule) -> Result<Self> {
        let base = Dir::new(c"rust_seqops");
        let scope = base.scope(pin_init!(Ops {}), c"selftest", |ops, dir| {
            dir.seq_file_with_context(c"demo", ops);
        });

        Ok(Self {
            _scope: KBox::pin_init(scope, GFP_KERNEL)?,
        })
    }
}
