// SPDX-License-Identifier: GPL-2.0

//! Minimal standalone NVMe controller probe (M1 milestone).
//!
//! Binds an NVMe controller, brings up the admin queue, issues Identify
//! Controller (CNS=1), prints the identity and exits cleanly. No I/O queues,
//! no block device registration.
//!
//! Register layout and reset sequence follow drivers/nvme/host/pci.c and
//! include/linux/nvme.h, but this driver deliberately does not reuse nvme-core:
//! the point of M1 is to own controller bring-up in Rust.
//!
//! # Removal path
//!
//! The goal is to replace the C driver, not to sit next to it, so this stays
//! self-contained instead of registering `nvme_ctrl_ops` with `core.c`: reusing
//! nvme-core would keep that file alive forever. Consequence: M2 has to bring in
//! the block layer itself (blk-mq queues, block device registration, per-request
//! private data), it is not a transport-only replacement.
//!
//! # Differences from the C transport (M1)
//!
//! - capabilities: C derives queue depth, doorbell stride and timeout from
//!   CAP; here the depth is a fixed policy value, the stride comes from
//!   CAP.DSTRD and the timeout from CAP.TO.
//! - submission: C keeps one outstanding command per queue slot and tags it
//!   with a CID; this driver issues one command at a time and ignores CID.
//! - completion: C uses MSI-X; this driver polls the CQ phase bit.
//! - errors: C decodes the full status and retries; this driver reports the
//!   status word and fails.
//! - lifetime: both leave the controller enabled after probe; remove resets
//!   it and then releases the PCI device.
//!
//! Register access uses the fallible accessors: the BAR is mapped without a
//! size, so the compiler cannot prove the offsets are in bounds and the
//! infallible accessors would turn that into a runtime `build_error!` trap.
//! See upstream report: `IoMem<0>` exposes infallible accessors.

use core::sync::atomic::{fence, Ordering};

use kernel::{
    alloc::flags::GFP_KERNEL,
    bindings,
    device,
    dma::{Coherent, CoherentBox},
    error::{code::*, Result},
    io::Io,
    pci,
    prelude::*,
};

mod regs {
    use kernel::bindings;

    // Offset of the doorbell array; the per-queue stride comes from CAP.DSTRD.
    pub(super) const DOORBELL_BASE: usize = bindings::NVME_REG_DBS as usize;

    pub(super) const CAP: usize = bindings::NVME_REG_CAP as usize;
    pub(super) const CC: usize = bindings::NVME_REG_CC as usize;
    pub(super) const CSTS: usize = bindings::NVME_REG_CSTS as usize;
    pub(super) const AQA: usize = bindings::NVME_REG_AQA as usize;
    pub(super) const ASQ: usize = bindings::NVME_REG_ASQ as usize;
    pub(super) const ACQ: usize = bindings::NVME_REG_ACQ as usize;

    // CAP.MQES is bits 15:0 (nvme.h:165).
    pub(super) const CAP_MQES: u64 = 0xffff;
    const _: () = assert!(CAP_MQES == 0xffff);

    // CAP.DSTRD is bits 35:32; the doorbell stride is 4 << DSTRD bytes.
    // include/linux/nvme.h:165-167 has no macro for these bit positions.
    pub(super) const CAP_DSTRD_SHIFT: u64 = 32;
    pub(super) const CAP_DSTRD_MASK: u64 = 0xf;
    const _: () = assert!(CAP_DSTRD_SHIFT + 4 <= 64 && CAP_DSTRD_MASK == 0xf);

    pub(super) const CC_EN: u32 = bindings::NVME_CC_ENABLE as u32;
    pub(super) const CSTS_RDY: u32 = bindings::NVME_CSTS_RDY as u32;
    pub(super) const CSTS_CFS: u32 = bindings::NVME_CSTS_CFS as u32;

    // CAP.TO is in 500 ms units; 0 means "no limit" (use the spec 5 s advice).
    pub(super) const CAP_TO_SHIFT: u64 = 24;
    pub(super) const CAP_TO_MASK: u64 = 0xff;
    const _: () = assert!(CAP_TO_SHIFT + 8 <= 64 && CAP_TO_MASK == 0xff);

    pub(super) const CC_IOSQES: u32 = bindings::NVME_CC_IOSQES as u32;
    pub(super) const CC_IOCQES: u32 = bindings::NVME_CC_IOCQES as u32;

    /// CC.MPS: host memory page size as a power of two, offset from 4 KiB
    /// (nvme.h:236). Assuming 4 KiB silently breaks on 16 KiB-page arches.
    pub(super) const CC_MPS: u32 =
        ((bindings::PAGE_SHIFT - 12) as u32) << (bindings::NVME_CC_MPS_SHIFT as u32);
    const _: () = assert!(bindings::PAGE_SHIFT >= 12);

    /// Admin queue depth. A policy choice (how many commands we keep in flight),
    /// not a protocol constant; AQA is programmed with "entries - 1".
    pub(super) const ADMIN_QDEPTH: u32 = 4;
}

const CMD_SIZE: usize = core::mem::size_of::<bindings::nvme_command>();
const CQE_SIZE: usize = core::mem::size_of::<bindings::nvme_completion>();

// The submission/completion entry sizes are part of the NVMe spec; tying them
// to the C structs makes a layout change fail here instead of on real hardware.
const _: () = assert!(CMD_SIZE == 64);
const _: () = assert!(CQE_SIZE == 16);
const NVME_ADMIN_IDENTIFY: u32 = bindings::nvme_admin_opcode_nvme_admin_identify as u32;
const NVME_ID_CNS_CTRL: u32 = bindings::NVME_ID_CNS_CTRL as u32;
const NVME_ID_CNS_NS: u32 = bindings::NVME_ID_CNS_NS as u32;
const ID_BUF_SIZE: usize = 4096;
const QUEUE_BUF_SIZE: usize = 4096;

fn wait_until(mut cond: impl FnMut() -> bool, timeout_ms: u64) -> Result {
    // Roughly 2k spins per millisecond; the loop is only a backstop so a
    // broken controller cannot hang the probe forever.
    let iters = timeout_ms.saturating_mul(2_000).max(1);
    for _ in 0..iters {
        if cond() {
            return Ok(());
        }
        core::hint::spin_loop();
    }
    Err(ETIMEDOUT)
}

/// Error for a register access that falls outside the mapped BAR.
///
/// The BAR is mapped without a size (`Region<0>`), so the accessors have no
/// compile-time bounds to check against; the fallible variants report it here
/// instead of tripping a `build_assert!` at runtime.
fn out_of_bounds(off: usize) -> Error {
    pr_err!("nvme-rust: register access out of BAR bounds (offset={:#x})\n", off);
    EIO
}

fn cstr(field: &[u8]) -> &str {
    let end = field
        .iter()
        .rposition(|c| *c != b' ' && *c != 0)
        .map_or(0, |p| p + 1);
    core::str::from_utf8(&field[..end]).unwrap_or("<invalid>")
}

struct NvmeSkel<'a> {
    bar: pci::Bar<'a>,
    asq: Coherent<[u8]>,
    acq: Coherent<[u8]>,
    sq_tail: u16,
    cq_head: u16,
}

impl NvmeSkel<'_> {
    fn read64(&self, off: usize) -> Result<u64> {
        let lo = self.bar.try_read32(off).map_err(|_| out_of_bounds(off))? as u64;
        let hi = self.bar.try_read32(off + 4).map_err(|_| out_of_bounds(off + 4))? as u64;
        Ok(lo | (hi << 32))
    }

    fn write64(&self, value: u64, off: usize) -> Result {
        self.bar
            .try_write32(value as u32, off)
            .map_err(|_| out_of_bounds(off))?;
        self.bar
            .try_write32((value >> 32) as u32, off + 4)
            .map_err(|_| out_of_bounds(off + 4))
    }

    /// Doorbell stride in bytes, from CAP.DSTRD (4 << DSTRD).
    fn doorbell_stride(&self) -> Result<usize> {
        let cap = self.read64(regs::CAP)?;
        Ok(4usize << ((cap >> regs::CAP_DSTRD_SHIFT) & regs::CAP_DSTRD_MASK))
    }

    fn sq0_tdbl(&self) -> usize {
        regs::DOORBELL_BASE
    }

    fn cq0_hdbl(&self) -> Result<usize> {
        Ok(regs::DOORBELL_BASE + self.doorbell_stride()?)
    }

    fn timeout_ms(&self) -> Result<u64> {
        let to = (self.read64(regs::CAP)? >> regs::CAP_TO_SHIFT) & regs::CAP_TO_MASK;
        Ok(if to == 0 { 5_000 } else { (to + 1) * 500 })
    }

    fn wait_ready(&self, want_rdy: bool) -> Result {
        let timeout_ms = self.timeout_ms()?;
        let mut fatal = false;
        let mut access_error = None;
        wait_until(
            || match self.bar.try_read32(regs::CSTS) {
                Ok(csts) => {
                    fatal = csts & regs::CSTS_CFS != 0;
                    (csts & regs::CSTS_RDY != 0) == want_rdy || fatal
                }
                Err(_) => {
                    access_error = Some(out_of_bounds(regs::CSTS));
                    true
                }
            },
            timeout_ms,
        )?;
        if let Some(err) = access_error {
            return Err(err);
        }
        if fatal {
            pr_err!("nvme-rust: CSTS.CFS set, controller reports fatal status\n");
            return Err(EIO);
        }
        Ok(())
    }

    fn disable(&self) -> Result {
        self.bar
            .try_write32(0, regs::CC)
            .map_err(|_| out_of_bounds(regs::CC))?;
        fence(Ordering::SeqCst);
        self.wait_ready(false)
    }

    fn enable(&self) -> Result {
        // CAP.MQES is the maximum queue size minus one; refuse rather than
        // program a depth the controller does not support. nvme.h:165 spells it
        // as a function-like macro, which bindgen does not emit.
        let mqes = (self.read64(regs::CAP)? & regs::CAP_MQES) as u32;
        if mqes + 1 < regs::ADMIN_QDEPTH {
            pr_err!(
                "nvme-rust: queue depth {} exceeds CAP.MQES+1={}\n",
                regs::ADMIN_QDEPTH,
                mqes + 1
            );
            return Err(EINVAL);
        }

        let qsize = regs::ADMIN_QDEPTH - 1;
        self.bar
            .try_write32(qsize | (qsize << 16), regs::AQA)
            .map_err(|_| out_of_bounds(regs::AQA))?;
        self.write64(self.asq.dma_address() as u64, regs::ASQ)?;
        self.write64(self.acq.dma_address() as u64, regs::ACQ)?;

        let cc = regs::CC_EN | regs::CC_IOSQES | regs::CC_IOCQES | regs::CC_MPS;
        self.bar
            .try_write32(cc, regs::CC)
            .map_err(|_| out_of_bounds(regs::CC))?;
        fence(Ordering::SeqCst);

        self.wait_ready(true)
    }

    fn submit_identify(&mut self, id: &Coherent<[u8]>, cns: u32, nsid: u32) -> Result {
        let slot = self.sq_tail as usize % regs::ADMIN_QDEPTH as usize;

        // SAFETY: the queue holds ADMIN_QDEPTH commands, sq_tail never exceeds
        // that, and the device does not read the entry before the doorbell.
        let cmd = unsafe { &mut self.asq.as_mut()[slot * CMD_SIZE..][..CMD_SIZE] };
        cmd.fill(0);
        cmd[0..4].copy_from_slice(&NVME_ADMIN_IDENTIFY.to_le_bytes());
        cmd[4..8].copy_from_slice(&nsid.to_le_bytes());
        cmd[24..32].copy_from_slice(&(id.dma_address() as u64).to_le_bytes());
        cmd[40..44].copy_from_slice(&cns.to_le_bytes());

        fence(Ordering::Release);

        self.sq_tail = self.sq_tail.wrapping_add(1);
        self.bar
            .try_write32(self.sq_tail as u32, self.sq0_tdbl())
            .map_err(|_| out_of_bounds(self.sq0_tdbl()))?;

        self.wait_completion()
    }

    fn wait_completion(&mut self) -> Result {
        let want_phase = ((self.cq_head / regs::ADMIN_QDEPTH as u16) & 1) == 0;
        let mut status = 0u16;

        let timeout_ms = self.timeout_ms()?;
        wait_until(|| {
            let slot = self.cq_head as usize % regs::ADMIN_QDEPTH as usize;
            // SAFETY: slot is within the completion queue allocation.
            let cqe = unsafe { &self.acq.as_ref()[slot * CQE_SIZE..][..CQE_SIZE] };
            let raw = u16::from_le_bytes([cqe[14], cqe[15]]);
            status = raw;
            (raw & 1 != 0) == want_phase
        }, timeout_ms)?;

        self.cq_head = self.cq_head.wrapping_add(1);
        let hdbl = self.cq0_hdbl()?;
        self.bar
            .try_write32(self.cq_head as u32, hdbl)
            .map_err(|_| out_of_bounds(hdbl))?;

        if status >> 1 != 0 {
            pr_err!("nvme-rust: identify failed, status {:#x}\n", status >> 1);
            return Err(EIO);
        }
        Ok(())
    }
}

impl<'a> pci::Driver for NvmeSkel<'a> {
    type IdInfo = ();
    type Data<'bound> = NvmeSkel<'bound>;

    const ID_TABLE: pci::IdTable<Self::IdInfo> = &NVME_TABLE;

    fn probe<'bound>(
        pdev: &'bound pci::Device<device::Core<'_>>,
        _info: Option<&'bound Self::IdInfo>,
    ) -> impl PinInit<Self::Data<'bound>, Error> + 'bound {
        let dev = pdev.as_ref();

        pdev.enable_device_mem()?;
        pdev.set_master();

        // Map the whole BAR: like the C transport, do not assume its size.
        let bar = pdev.iomap_region(0, c"nvme_skel")?;

        let mut ctrl = NvmeSkel {
            bar,
            asq: CoherentBox::zeroed_slice(dev, QUEUE_BUF_SIZE, GFP_KERNEL)?.into(),
            acq: CoherentBox::zeroed_slice(dev, QUEUE_BUF_SIZE, GFP_KERNEL)?.into(),
            sq_tail: 0,
            cq_head: 0,
        };

        // Log the mapped BAR length: without it an early failure gives no clue
        // that the size assumption was the problem.
        let bar_len = pdev.resource_len(0)?;
        dev_info!(
            dev,
            "nvme-rust: BAR len={:#x} CAP={:#x}\n",
            bar_len,
            ctrl.read64(regs::CAP)?
        );

        ctrl.disable()?;
        ctrl.enable()?;

        let id: Coherent<[u8]> = CoherentBox::zeroed_slice(dev, ID_BUF_SIZE, GFP_KERNEL)?.into();
        ctrl.submit_identify(&id, NVME_ID_CNS_CTRL, 0)
            .map_err(|e| {
                pdev.disable_device();
                e
            })?;

        // SAFETY: the controller wrote the identify payload into id and the
        // command completed.
        let id = unsafe { &*id.as_ptr().cast::<bindings::nvme_id_ctrl>() };
        dev_info!(
            dev,
            "nvme-rust: model='{}' serial='{}' fw='{}'\n",
            cstr(&id.mn),
            cstr(&id.sn),
            cstr(&id.fr),
        );
        let ns: Coherent<[u8]> =
            CoherentBox::zeroed_slice(dev, ID_BUF_SIZE, GFP_KERNEL)?.into();
        ctrl.submit_identify(&ns, NVME_ID_CNS_NS, 1)
            .map_err(|e| {
                pdev.disable_device();
                e
            })?;

        // SAFETY: the namespace identify payload was written by the controller.
        let ns = unsafe { &*ns.as_ptr().cast::<bindings::nvme_id_ns>() };
        let nsze = u64::from_le(ns.nsze);
        let flbas = (ns.flbas & bindings::NVME_NS_FLBAS_LBA_MASK as u8) as usize;
        let shift = ns.lbaf[flbas].ds as u32;
        // The controller supplies this shift; a hostile or broken device could
        // hand us 64+ and make the multiply below panic or wrap.
        if shift >= 64 {
            pr_err!("nvme-rust: LBA shift {} out of range\n", shift);
            pdev.disable_device();
            return Err(EIO);
        }
        let capacity = nsze.checked_shl(shift).ok_or(EIO).map_err(|e| {
            pdev.disable_device();
            e
        })?;

        dev_info!(
            dev,
            "nvme-rust: capacity {} bytes ({} LBAs of {} bytes)\n",
            capacity,
            nsze,
            1u64 << shift
        );

        Ok(ctrl)
    }

    fn unbind<'bound>(
        pdev: &'bound pci::Device<device::Core<'_>>,
        this: Pin<&Self::Data<'bound>>,
    ) {
        // A controller that does not quiesce must not turn into an oops on
        // unload: log it and keep tearing down.
        if this.disable().is_err() {
            pr_warn!("nvme-rust: controller did not quiesce, continuing teardown\n");
        }
        pdev.disable_device();
    }
}

kernel::pci_device_table!(
    NVME_TABLE,
    <NvmeSkel<'static> as pci::Driver>::IdInfo,
    [(pci::DeviceId::from_class(0x010802, 0xffffff), ())]
);

kernel::module_pci_driver! {
    type: NvmeSkel<'static>,
    name: "nvme_skel",
    authors: ["Deepin Kernel Team"],
    description: "Minimal Rust NVMe controller probe (M1)",
    license: "GPL v2",
}
