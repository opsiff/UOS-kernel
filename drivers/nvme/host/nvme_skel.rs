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
    pub(super) const CAP: usize = 0x0000;
    pub(super) const CC: usize = 0x0014;
    pub(super) const CSTS: usize = 0x001c;
    pub(super) const AQA: usize = 0x0024;
    pub(super) const ASQ: usize = 0x0028;
    pub(super) const ACQ: usize = 0x0030;
    pub(super) const SQ0TDBL: usize = 0x1000;
    pub(super) const CQ0HDBL: usize = 0x1004;

    pub(super) const CC_EN: u32 = 1 << 0;
    pub(super) const CSTS_RDY: u32 = 1 << 0;
    pub(super) const CC_IOSQES: u32 = 6 << 16;
    pub(super) const CC_IOCQES: u32 = 4 << 20;

    /// Admin queue depth; AQA is programmed with "entries - 1".
    pub(super) const ADMIN_QDEPTH: u32 = 4;
}

const BAR_SIZE: usize = 0x4000;
const CMD_SIZE: usize = 64;
const CQE_SIZE: usize = 16;
const NVME_ADMIN_IDENTIFY: u32 = 0x06;
const NVME_ID_CNS_CTRL: u32 = 1;
const NVME_ID_CNS_NS: u32 = 0;
const ID_BUF_SIZE: usize = 4096;
const QUEUE_BUF_SIZE: usize = 4096;
const POLL_LIMIT: u32 = 10_000_000;

fn wait_until(mut cond: impl FnMut() -> bool) -> Result {
    for _ in 0..POLL_LIMIT {
        if cond() {
            return Ok(());
        }
        core::hint::spin_loop();
    }
    Err(ETIMEDOUT)
}

fn cstr(field: &[u8]) -> &str {
    let end = field
        .iter()
        .rposition(|c| *c != b' ' && *c != 0)
        .map_or(0, |p| p + 1);
    core::str::from_utf8(&field[..end]).unwrap_or("<invalid>")
}

struct NvmeSkel<'a> {
    bar: pci::Bar<'a, BAR_SIZE>,
    asq: Coherent<[u8]>,
    acq: Coherent<[u8]>,
    sq_tail: u16,
    cq_head: u16,
}

impl NvmeSkel<'_> {
    fn read64(&self, off: usize) -> u64 {
        let lo = self.bar.read32(off) as u64;
        let hi = self.bar.read32(off + 4) as u64;
        lo | (hi << 32)
    }

    fn write64(&self, value: u64, off: usize) {
        self.bar.write32(value as u32, off);
        self.bar.write32((value >> 32) as u32, off + 4);
    }

    fn disable(&self) -> Result {
        self.bar.write32(0, regs::CC);
        fence(Ordering::SeqCst);
        wait_until(|| self.bar.read32(regs::CSTS) & regs::CSTS_RDY == 0)
    }

    fn enable(&self) -> Result {
        let qsize = regs::ADMIN_QDEPTH - 1;
        self.bar.write32(qsize | (qsize << 16), regs::AQA);
        self.write64(self.asq.dma_address() as u64, regs::ASQ);
        self.write64(self.acq.dma_address() as u64, regs::ACQ);

        let cc = regs::CC_EN | regs::CC_IOSQES | regs::CC_IOCQES;
        self.bar.write32(cc, regs::CC);
        fence(Ordering::SeqCst);

        wait_until(|| self.bar.read32(regs::CSTS) & regs::CSTS_RDY != 0)
    }

    fn submit_identify(&mut self, id: &Coherent<[u8]>, cns: u32, nsid: u32) -> Result {
        let slot = self.sq_tail as usize;

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
        self.bar.write32(self.sq_tail as u32, regs::SQ0TDBL);

        self.wait_completion()
    }

    fn wait_completion(&mut self) -> Result {
        let want_phase = ((self.cq_head / regs::ADMIN_QDEPTH as u16) & 1) == 0;
        let mut status = 0u16;

        wait_until(|| {
            let slot = self.cq_head as usize % regs::ADMIN_QDEPTH as usize;
            // SAFETY: slot is within the completion queue allocation.
            let cqe = unsafe { &self.acq.as_ref()[slot * CQE_SIZE..][..CQE_SIZE] };
            let raw = u16::from_le_bytes([cqe[14], cqe[15]]);
            status = raw;
            (raw & 1 != 0) == want_phase
        })?;

        self.cq_head = self.cq_head.wrapping_add(1);
        self.bar.write32(self.cq_head as u32, regs::CQ0HDBL);

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

        let bar = pdev.iomap_region_sized::<BAR_SIZE>(0, c"nvme_skel")?;

        let mut ctrl = NvmeSkel {
            bar,
            asq: CoherentBox::zeroed_slice(dev, QUEUE_BUF_SIZE, GFP_KERNEL)?.into(),
            acq: CoherentBox::zeroed_slice(dev, QUEUE_BUF_SIZE, GFP_KERNEL)?.into(),
            sq_tail: 0,
            cq_head: 0,
        };

        dev_info!(dev, "nvme-rust: CAP={:#x}\n", ctrl.read64(regs::CAP));

        ctrl.disable()?;
        ctrl.enable()?;

        let id: Coherent<[u8]> = CoherentBox::zeroed_slice(dev, ID_BUF_SIZE, GFP_KERNEL)?.into();
        ctrl.submit_identify(&id, NVME_ID_CNS_CTRL, 0)?;

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
        ctrl.submit_identify(&ns, NVME_ID_CNS_NS, 1)?;

        // SAFETY: the namespace identify payload was written by the controller.
        let ns = unsafe { &*ns.as_ptr().cast::<bindings::nvme_id_ns>() };
        let nsze = u64::from_le(ns.nsze);
        let flbas = (ns.flbas & 0x0f) as usize;
        let shift = ns.lbaf[flbas].ds as u32;

        dev_info!(
            dev,
            "nvme-rust: capacity {} bytes ({} LBAs of {} bytes)\n",
            nsze << shift,
            nsze,
            1u64 << shift
        );

        ctrl.disable()?;
        Ok(ctrl)
    }

    fn unbind<'bound>(
        _pdev: &'bound pci::Device<device::Core<'_>>,
        this: Pin<&Self::Data<'bound>>,
    ) {
        let _ = this.disable();
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
