/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Linux driver for C*Core IOP based controllers
 *
 *  Copyright (c) 2023-2024 C*Core Technology Co.,Ltd.
 *  Copyright (c) 2023-2024 VolansComputer S&T Co.,Ltd.
 */
#ifndef _CCUSR_H_
#define _CCUSR_H_

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 30)
#ifndef IRQ_HANDLED
typedef void irqreturn_t;
#define IRQ_NONE
#define IRQ_HANDLED
#define IRQ_RETVAL(x)
#endif
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 19)
#define reset_devices 0
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 20)
#define IRQF_SHARED SA_SHIRQ
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 23)
#define scsi_bufflen(srb) ((srb)->request_bufflen)
#define scsi_set_resid(srb, n) (srb)->resid = (n)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 24)
#define sg_page(sg) (sg)->page
#define scsi_sglist(srb) ((struct scatterlist *)(srb)->request_buffer)
#define scsi_sg_count(srb) ((srb)->use_sg)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 28)
typedef unsigned __bitwise__ fmode_t;
#define pci_ioremap_bar(pdev, bar) \
	ioremap(pci_resource_start(pdev, bar), pci_resource_len(pdev, bar))
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 16, 0)
#define scsi_done(srb) (srb)->scsi_done(srb)
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 17, 0)
struct ccusr_cmd_priv {
	dma_addr_t dma_handle;
	int dmamap_cnt;
};
#define srb_dma_handle(srb) \
	(((struct ccusr_cmd_priv *)scsi_cmd_priv(srb))->dma_handle)
#define srb_dmamap_cnt(srb) \
	(((struct ccusr_cmd_priv *)scsi_cmd_priv(srb))->dmamap_cnt)
#else
#define srb_dma_handle(srb) ((srb)->SCp.dma_handle)
#define srb_dmamap_cnt(srb) ((srb)->SCp.this_residual)
#endif

typedef u64 VRC_U64;
typedef u32 VRC_U32;
typedef u16 VRC_U16;
typedef u8 VRC_U8;
typedef __le64 VRC_LE64;
typedef __le32 VRC_LE32;
typedef __le16 VRC_LE16;

#include "cciop.h"

#define CCUSR_SENSE_LENGTH 32
#define CCUSR_INTCTL_REG 0x24004

struct ccusr_req_tracker {
	struct ccusr_req_tracker *next;
	struct scsi_cmnd *srb;
	struct cciop_request *request;
	void *sense;
	dma_addr_t request_phy;
	dma_addr_t sense_phy;
};

struct ccusr_msg_reply {
	u32 param;
	u8 iop_status, scsi_status;
};

struct ccusr_hba {
	struct pci_dev *pcidev;
	volatile struct cciop_if_regs __iomem *regs;
	volatile struct cciop_if_ext __iomem *ext_regs;
	void __iomem *ctl_regs;

	struct Scsi_Host *host;

	u32 max_hw_requests;
	u32 max_requests;
	u32 max_sg_count;
	u32 dataxfer_length;
	u32 max_devices;
	u32 inbound_wptr;
	u32 outbound_rptr;

	struct dma_pool *req_pool;
	struct dma_pool *sense_pool;
	struct ccusr_req_tracker *req_list;
	atomic_t outstanding_reqs;

	spinlock_t req_list_lock;
	spinlock_t inbound_lock;

	wait_queue_head_t msg_wq;
	wait_queue_head_t ioctl_wq;
	struct mutex ioctl_lock;
};

#define ccusr_printk(level, fmt, args...) \
	printk(level KBUILD_MODNAME ": " fmt, ##args)

#ifdef DEBUG
#define dprintk(fmt, args...) \
	ccusr_printk(KERN_DEBUG, __func__ ": " fmt, ##args)
#else
#define dprintk(fmt, args...)
#endif

#endif
