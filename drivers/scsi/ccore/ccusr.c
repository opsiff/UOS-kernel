// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Linux driver for C*Core IOP based controllers
 *
 *  Copyright (c) 2023-2024 C*Core Technology Co.,Ltd.
 *  Copyright (c) 2023-2024 VolansComputer S&T Co.,Ltd.
 */
#include <linux/version.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/spinlock.h>
#include <asm/io.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi.h>
#include <scsi/scsi_tcq.h>
#include <linux/backing-dev.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 4)
#include "hosts.h"
#else
#include <scsi/scsi_host.h>
#include <scsi/sg.h>
#endif

#undef DEBUG
#include "ccusr.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
#define CONFIG_CCUSR_IOCTL 1
#else
#define CONFIG_CCUSR_IOCTL 0
#endif

static int use_msi = 1;
module_param(use_msi, int, 0444);

static int scmd_timeout = 30;
module_param(scmd_timeout, int, 0444);

static int init_reset = 0;
module_param(init_reset, int, 0444);;

static char driver_name[] = KBUILD_MODNAME;
static const char driver_ver[] = DRIVER_VERSION;

static int os_schedule_sd_change(struct Scsi_Host *shost, int id,
				 void (*action)(struct Scsi_Host *, int));
static void os_revalidate_sdev(struct Scsi_Host *shost, int id);
static void os_add_sdev(struct Scsi_Host *shost, int id);
static void os_remove_sdev(struct Scsi_Host *shost, int id);

static void inbound_write(struct ccusr_hba *hba,
			  union cciop_inbound_entry *entry)
{
	volatile struct cciop_if_regs __iomem *regs = hba->regs;
	u32 wptr;
	u32 i;
	unsigned long flags;

	spin_lock_irqsave(&hba->inbound_lock, flags);

	wptr = hba->inbound_wptr;

	dprintk("0x%x <- 0x%llx 0x%llx\n", wptr, entry->data.qword0,
		entry->data.qword1);

	while ((wptr ^ readl(&regs->inbound_rptr)) == 0x80000000)
		udelay(1);

	i = wptr & 0x7FFFFFFF;
	if (i + 1 == hba->max_hw_requests)
		wptr = (wptr & 0x80000000ul) ^ 0x80000000ul;
	else
		wptr++;

	i = i * 2;
	writeq(entry->data.qword0, &regs->q[i]);
	writeq(entry->data.qword1, &regs->q[i + 1]);
	writel(wptr, &regs->inbound_wptr);
	hba->inbound_wptr = wptr;

	spin_unlock_irqrestore(&hba->inbound_lock, flags);
}

static int outbound_read(struct ccusr_hba *hba,
			 union cciop_outbound_entry *entry)
{
	volatile struct cciop_if_regs __iomem *regs = hba->regs;
	u32 wptr = readl(&regs->outbound_wptr);
	u32 rptr = hba->outbound_rptr;
	u32 i;

	if (rptr == wptr)
		return 0;

	i = rptr & 0x7FFFFFFF;
	if (i + 1 == hba->max_hw_requests)
		rptr = (rptr & 0x80000000ul) ^ 0x80000000ul;
	else
		rptr++;

	i = (i + hba->max_hw_requests) * 2;
	entry->data.qword0 = readq(&regs->q[i]);
	if (entry->general.type)
		entry->data.qword1 = readq(&regs->q[i + 1]);

	dprintk("0x%x -> 0x%llx 0x%llx\n", hba->outbound_rptr,
		entry->data.qword0, entry->data.qword1);

	writel(rptr, &regs->outbound_rptr);
	hba->outbound_rptr = rptr;
	return 1;
}

static inline void post_message(struct ccusr_hba *hba, u8 code, u32 param,
				void *context)
{
	union cciop_inbound_entry entry;

	entry.message.type = 1;
	entry.message.code = code;
	entry.message.reserved2 = 0;
	entry.message.param = cpu_to_le32(param);
	entry.message.reply_context = cpu_to_le64((unsigned long)context);

	inbound_write(hba, &entry);
}

static inline void ccusr_enable_intr(struct ccusr_hba *hba)
{
	writel(0, hba->ctl_regs + CCUSR_INTCTL_REG);
	writeb(1, &hba->regs->irq_mask);
}

static inline void ccusr_disable_intr(struct ccusr_hba *hba)
{
	writeb(0, &hba->regs->irq_mask);
	writel(0, hba->ctl_regs + CCUSR_INTCTL_REG);
	readl(hba->ctl_regs + CCUSR_INTCTL_REG);
}

static int ccusr_map_pci_bar(struct ccusr_hba *hba)
{
	struct pci_dev *pcidev = hba->pcidev;

	if (!(pci_resource_flags(pcidev, 0) & IORESOURCE_MEM)) {
		ccusr_printk(KERN_ERR, "host%d: pci resource invalid\n",
			     hba->host->host_no);
		return -EINVAL;
	}

	hba->regs = pci_ioremap_bar(pcidev, 0);

	if (!hba->regs) {
		ccusr_printk(KERN_ERR, "host%d: Fail to ioremap memory space\n",
			     hba->host->host_no);
		return -EINVAL;
	}

	hba->ctl_regs = pci_ioremap_bar(pcidev, 1);

	if (!hba->ctl_regs) {
		ccusr_printk(KERN_ERR, "host%d: Fail to ioremap ctl regs\n",
			     hba->host->host_no);
		iounmap(hba->regs);
		hba->regs = NULL;
		return -EINVAL;
	}

	return 0;
}

static void ccusr_unmap_pci_bar(struct ccusr_hba *hba)
{
	iounmap(hba->regs);
	iounmap(hba->ctl_regs);
}

static int ccusr_wait_ready(struct ccusr_hba *hba, u32 timeout)
{
	while (1) {
		if (readb(&hba->regs->iop_state) == 1)
			return 0;
		if (timeout == 0)
			return -ETIMEDOUT;
		msleep(100);
		--timeout;
	}
}

static inline struct ccusr_req_tracker *get_req(struct ccusr_hba *hba)
{
	struct ccusr_req_tracker *req;
	unsigned long flags;

	spin_lock_irqsave(&hba->req_list_lock, flags);

	req = hba->req_list;
	if (req) {
		hba->req_list = req->next;
		req->next = NULL;
		atomic_inc(&hba->outstanding_reqs);
	}

	spin_unlock_irqrestore(&hba->req_list_lock, flags);
	return req;
}

static inline void put_req(struct ccusr_hba *hba, struct ccusr_req_tracker *req)
{
	unsigned long flags;

	spin_lock_irqsave(&hba->req_list_lock, flags);
	req->srb = NULL;
	req->next = hba->req_list;
	hba->req_list = req;
	atomic_dec(&hba->outstanding_reqs);
	spin_unlock_irqrestore(&hba->req_list_lock, flags);
}

static void finish_req(struct ccusr_hba *hba, struct ccusr_req_tracker *req,
		       u8 iop_status, u8 scsi_status, u32 xferlen)
{
	struct scsi_cmnd *srb = req->srb;

	dprintk("req=%px, iop_status=0x%x scsi_status=0x%x xferlen=0x%x\n", req,
		iop_status, scsi_status, xferlen);

	if (srb_dmamap_cnt(srb)) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 23)
		scsi_dma_unmap(srb);
#else
		if (srb->use_sg)
			pci_unmap_sg(hba->pcidev,
				     (struct scatterlist *)srb->request_buffer,
				     srb->use_sg, srb->sc_data_direction);
		else
			pci_unmap_single(hba->pcidev, srb_dma_handle(srb),
					 srb->request_bufflen,
					 srb->sc_data_direction);
#endif
	}

	switch (iop_status) {
	case CCIOP_STATUS_SUCCESS:
		srb->result = (DID_OK << 16) | scsi_status;
		break;
	case CCIOP_STATUS_BAD_TARGET:
		srb->result = (DID_BAD_TARGET << 16);
		break;
	case CCIOP_STATUS_BUSY:
		srb->result = (DID_BUS_BUSY << 16);
		break;
	case CCIOP_STATUS_RESET:
		srb->result = (DID_RESET << 16);
		break;
	case CCIOP_STATUS_FAIL:
		srb->result = (DID_ERROR << 16);
		break;
	case CCIOP_STATUS_INVALID_REQUEST:
		srb->result = (DID_ABORT << 16);
		break;
	case CCIOP_STATUS_CHECK_CONDITION:
		srb->result = SAM_STAT_CHECK_CONDITION;
		memcpy(srb->sense_buffer, req->sense,
		       min_t(u32, SCSI_SENSE_BUFFERSIZE, CCUSR_SENSE_LENGTH));
		break;
	default:
		srb->result = DID_ABORT << 16;
		break;
	}

	scsi_set_resid(srb, scsi_bufflen(srb) - xferlen);
	put_req(hba, req);

	dprintk("scsi_done(%px)\n", srb);
	if (srb->device)
		scsi_done(srb);
	else
		wake_up(&hba->ioctl_wq);
}

static void ccusr_handle_message(struct ccusr_hba *hba, u8 code, u32 param)
{
	switch (code) {
	case CCIOP_OUTBOUND_MSG_VDEV_ADDED:
		os_schedule_sd_change(hba->host, param, os_add_sdev);
		break;
	case CCIOP_OUTBOUND_MSG_VDEV_REMOVED:
		os_schedule_sd_change(hba->host, param, os_remove_sdev);
		break;
	case CCIOP_OUTBOUND_MSG_VDEV_CHANGED:
		os_schedule_sd_change(hba->host, param, os_revalidate_sdev);
		break;
	default:
		ccusr_printk(KERN_ERR, "unhandled message %d\n", code);
		break;
	}
}

static int handle_outbound_queue(struct ccusr_hba *hba)
{
	union cciop_outbound_entry reply;
	u64 context;
	struct ccusr_req_tracker *req;
	u8 iop_status, scsi_status;
	u32 xferlen;
	struct ccusr_msg_reply *msg_reply;
	int n = 0;

	while (outbound_read(hba, &reply)) {
		context = le64_to_cpu(reply.request.reply_context);
		switch (context & 3) {
		case 0:
			req = (void *)(unsigned long)context;
			iop_status = CCIOP_STATUS_SUCCESS;
			scsi_status = 0;
			xferlen = req->srb ? scsi_bufflen(req->srb) : 0;
			finish_req(hba, req, iop_status, scsi_status, xferlen);
			break;
		case 2:
			req = (void *)(unsigned long)(context & ~3ull);
			iop_status = reply.request.iop_status;
			scsi_status = reply.request.scsi_status;
			xferlen = le32_to_cpu(reply.request.dataxfer_length);
			finish_req(hba, req, iop_status, scsi_status, xferlen);
			break;
		case 3:
			msg_reply = (struct ccusr_msg_reply *)(long)le64_to_cpu(
				reply.message.reply_context);
			if (msg_reply) {
				msg_reply->iop_status =
					reply.message.iop_status;
				msg_reply->scsi_status =
					reply.message.scsi_status;
				msg_reply->param =
					le32_to_cpu(reply.message.param);
				wake_up(&hba->msg_wq);
			}
			break;
		case 1:
			ccusr_handle_message(hba, reply.message.code,
					     le32_to_cpu(reply.message.param));
			break;
		default:
			ccusr_printk(KERN_ERR, "unknown reply type\n");
			break;
		}

		n++;
	}

	return n;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 19)
static irqreturn_t ccusr_intr(int irq, void *dev_id, struct pt_regs *regs)
#else
static irqreturn_t ccusr_intr(int irq, void *dev_id)
#endif
{
	struct ccusr_hba *hba = dev_id;

	dprintk("irq %d\n", irq);

	while (handle_outbound_queue(hba))
		writel(0, hba->ctl_regs + CCUSR_INTCTL_REG);

	return IRQ_RETVAL(IRQ_HANDLED);
}

static int ccusr_build_prd(struct scsi_cmnd *srb, struct cciop_prd *prd)
{
	struct Scsi_Host *host = srb->device->host;
	struct ccusr_hba *hba = (struct ccusr_hba *)host->hostdata;
	struct scatterlist *sg;
	int idx, nseg;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 23)
	nseg = scsi_dma_map(srb);
	if (nseg <= 0)
		goto done;
	BUG_ON(nseg > hba->max_sg_count);

	dprintk("nseg=%d\n", nseg);

	scsi_for_each_sg(srb, sg, nseg, idx) {
		prd[idx].address = cpu_to_le64(sg_dma_address(sg));
		prd[idx].dbc = cpu_to_le32(sg_dma_len(sg) - 1);
		dprintk(" prd[%d] = 0x%llx +0x%x\n", idx, prd[idx].address,
			prd[idx].dbc);
	}
#else
	if (srb->use_sg) {
		sg = (struct scatterlist *)srb->request_buffer;
		nseg = pci_map_sg(hba->pcidev, sg, srb->use_sg,
				  srb->sc_data_direction);
		if (nseg <= 0)
			goto done;
		BUG_ON(nseg > hba->max_sg_count);

		for (idx = 0; idx < nseg; idx++) {
			prd[idx].address =
				cpu_to_le64(sg_dma_address(&sg[idx]));
			prd[idx].dbc = cpu_to_le32(sg_dma_len(&sg[idx]) - 1);
		}
	} else {
		srb_dma_handle(srb) = pci_map_single(hba->pcidev,
						     srb->request_buffer,
						     srb->request_bufflen,
						     srb->sc_data_direction);
		prd->address = cpu_to_le64(srb_dma_handle(srb));
		prd->dbc = cpu_to_le32(srb->request_bufflen - 1);
		nseg = 1;
	}
#endif
done:
	srb_dmamap_cnt(srb) = nseg > 0 ? nseg : 0;
	return nseg;
}

static int __ccusr_qcmd(struct Scsi_Host *host, struct scsi_cmnd *srb)
{
	struct ccusr_hba *hba = (struct ccusr_hba *)host->hostdata;
	struct ccusr_req_tracker *req;
	union cciop_inbound_entry entry;
	int nprd;

	dprintk("srb=%px %d/%d/%d/%d cdb=(%08x-%08x-%08x-%08x)\n", srb,
		host->host_no, srb->device->channel, srb->device->id,
		(int)srb->device->lun, cpu_to_be32(((u32 *)srb->cmnd)[0]),
		cpu_to_be32(((u32 *)srb->cmnd)[1]),
		cpu_to_be32(((u32 *)srb->cmnd)[2]),
		cpu_to_be32(((u32 *)srb->cmnd)[3]));

	srb->result = 0;

	if (srb->device->channel || srb->device->lun ||
	    srb->device->id > hba->max_devices) {
		srb->result = DID_BAD_TARGET << 16;
		scsi_done(srb);
		return 0;
	}

	req = get_req(hba);
	if (!req) {
		ccusr_printk(KERN_ERR, "no free req\n");
		return SCSI_MLQUEUE_HOST_BUSY;
	}

	req->srb = srb;
	req->request->type = cpu_to_le32(CCIOP_REQUEST_TYPE_SCSI);

	switch (srb->sc_data_direction) {
	case DMA_FROM_DEVICE:
		req->request->flags = cpu_to_le32(CCIOP_REQUEST_FLAG_DATA_IN);
		break;
	case DMA_TO_DEVICE:
		req->request->flags = cpu_to_le32(CCIOP_REQUEST_FLAG_DATA_OUT);
		break;
	case DMA_BIDIRECTIONAL:
		req->request->flags = cpu_to_le32(CCIOP_REQUEST_FLAG_DATA_IN |
						  CCIOP_REQUEST_FLAG_DATA_OUT);
		break;
	default:
		req->request->flags = cpu_to_le32(0);
		break;
	}

	req->request->reply_context = cpu_to_le64((unsigned long)req);
	req->request->devid = cpu_to_le32(srb->device->id);
	req->request->dataxfer_length = cpu_to_le32(scsi_bufflen(srb));

	memcpy(req->request->cdb, srb->cmnd, srb->cmd_len);

	nprd = ccusr_build_prd(srb, req->request->prdt);
	if (nprd < 0) {
		put_req(hba, req);
		return SCSI_MLQUEUE_HOST_BUSY;
	}

	req->request->prd_length = cpu_to_le32(nprd);

	entry.data.qword0 = 0;
	entry.request.type = 0;
	entry.request.size = cpu_to_le16(offsetof(struct cciop_request, prdt) +
					 sizeof(struct cciop_prd) * nprd);
	entry.request.request_address = cpu_to_le64(req->request_phy);
	inbound_write(hba, &entry);
	return 0;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 37)
static int ccusr_qcmd(struct scsi_cmnd *srb, void (*done)(struct scsi_cmnd *))
{
	struct Scsi_Host *host = srb->device->host;

	srb->scsi_done = done;
	return __ccusr_qcmd(host, srb);
}
#else
#define ccusr_qcmd __ccusr_qcmd
#endif

static const char *ccusr_info(struct Scsi_Host *host)
{
	return driver_name;
}

static void __ccusr_reset_hba(struct ccusr_hba *hba,
			      struct ccusr_msg_reply *reply)
{
	union cciop_inbound_entry entry;
	unsigned long flags;

	entry.data.qword0 = 0;
	entry.general.type = 1;
	entry.message.code = CCIOP_INBOUND_MSG_RESET;
	entry.message.param = cpu_to_le32(0xFFFFFFFF);
	entry.message.reply_context = cpu_to_le64((u64)(long)reply);

	spin_lock_irqsave(&hba->inbound_lock, flags);
	writeq(entry.data.qword1, &hba->regs->highpri_request_qword1);
	writeq(entry.data.qword0, &hba->regs->highpri_request_qword0);
	spin_unlock_irqrestore(&hba->inbound_lock, flags);
}

static int ccusr_reset_hba(struct ccusr_hba *hba)
{
	struct ccusr_msg_reply reply;

	reply.iop_status = CCIOP_STATUS_PENDING;
	__ccusr_reset_hba(hba, &reply);
	wait_event(hba->msg_wq, reply.iop_status != CCIOP_STATUS_PENDING);

	return reply.iop_status != CCIOP_STATUS_SUCCESS;
}

static int ccusr_reset(struct scsi_cmnd *srb)
{
	struct Scsi_Host *host = srb->device->host;
	struct ccusr_hba *hba = (struct ccusr_hba *)host->hostdata;

	ccusr_printk(KERN_WARNING, "resetting host%d, target %d, active %d\n",
		     host->host_no, srb->device->id,
		     atomic_read(&hba->outstanding_reqs));

	while (atomic_read(&hba->outstanding_reqs)) {
		if (ccusr_reset_hba(hba)) {
			/* IOP is in unknown state, abort reset */
			ccusr_printk(KERN_ERR, "reset failed\n");
			return FAILED;
		}
		msleep(1000);
	}

	ccusr_printk(KERN_INFO, "reset complete\n");
	return SUCCESS;
}

static void os_revalidate_sdev(struct Scsi_Host *shost, int id)
{
	struct scsi_device *sdev = scsi_device_lookup(shost, 0, id, 0);

	if (sdev) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 5, 7)
		scsi_rescan_device(sdev);
#else
		scsi_rescan_device(&sdev->sdev_gendev);
#endif
		scsi_device_put(sdev);
	}
}

static void os_add_sdev(struct Scsi_Host *shost, int id)
{
	struct scsi_device *sdev;

	mutex_lock(&shost->scan_mutex);
	sdev = scsi_device_lookup(shost, 0, id, 0);
	mutex_unlock(&shost->scan_mutex);

	if (sdev)
		scsi_device_put(sdev);
	else
		scsi_add_device(shost, 0, id, 0);
}

/* scsi_device_lookup() will fail when a deleted device exists with
 * the same ID. so do own version
 */
static struct scsi_device *__os_scsi_device_lookup(struct Scsi_Host *shost,
						   uint channel, uint id,
						   uint lun)
{
	struct scsi_device *sdev;

	list_for_each_entry(sdev, &shost->__devices, siblings) {
		if (sdev->sdev_state != SDEV_DEL && sdev->channel == channel &&
		    sdev->id == id && sdev->lun == lun)
			return sdev;
	}
	return NULL;
}

static struct scsi_device *
os_scsi_device_lookup(struct Scsi_Host *shost, uint channel, uint id, uint lun)
{
	struct scsi_device *sdev;
	unsigned long flags;

	spin_lock_irqsave(shost->host_lock, flags);
	sdev = __os_scsi_device_lookup(shost, channel, id, lun);
	if (sdev && scsi_device_get(sdev))
		sdev = NULL;
	spin_unlock_irqrestore(shost->host_lock, flags);

	return sdev;
}

static void os_remove_sdev(struct Scsi_Host *shost, int id)
{
	struct scsi_device *sdev;

	sdev = os_scsi_device_lookup(shost, 0, id, 0);
	if (sdev) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 23)
		/*@will be checked(SDEV_CANCEL) in scsi_remove_device*/
		scsi_device_cancel(sdev, 0);
#endif
		scsi_remove_device(sdev);
		scsi_device_put(sdev);
	}
}

struct sd_change_work {
	struct work_struct work;
	void (*action)(struct Scsi_Host *shost, int id);
	struct Scsi_Host *shost;
	int id;
};

static void sd_change_worker(struct work_struct *work)
{
	struct sd_change_work *change =
		container_of(work, struct sd_change_work, work);

	change->action(change->shost, change->id);
	kfree(change);
}

static int os_schedule_sd_change(struct Scsi_Host *shost, int id,
				 void (*action)(struct Scsi_Host *, int))
{
	struct sd_change_work *sd_change;

	sd_change = kmalloc(sizeof(*sd_change), GFP_ATOMIC);
	if (!sd_change)
		return -ENOMEM;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 20)
	INIT_WORK(&sd_change->work, (void (*)(void *))sd_change_worker,
		  &sd_change->work);
#else
	INIT_WORK(&sd_change->work, sd_change_worker);
#endif

	sd_change->shost = shost;
	sd_change->id = id;
	sd_change->action = action;
	schedule_work(&sd_change->work);
	return 0;
}

static u32 ccusr_get_blkfeat(struct scsi_device *sdev)
{
	struct ccusr_hba *hba = (struct ccusr_hba *)sdev->host->hostdata;
	struct ccusr_msg_reply reply;

	if (sdev->id == hba->max_devices)
		return 0;

	reply.iop_status = CCIOP_STATUS_PENDING;

	post_message(hba, CCIOP_INBOUND_MSG_BLKFEAT, sdev->id, &reply);
	wait_event(hba->msg_wq, reply.iop_status != CCIOP_STATUS_PENDING);

	if (reply.iop_status != CCIOP_STATUS_SUCCESS || reply.scsi_status) {
		ccusr_printk(
			KERN_WARNING,
			"host:%d target:%d MSG_BLKFEAT failed status %d scsistat %d\n",
			sdev->host->host_no, sdev->id, reply.iop_status,
			reply.scsi_status);
		reply.param = 0;
	}

	return reply.param;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 10, 0)
static int ccusr_sdev_configure(struct scsi_device *sdev,
				struct queue_limits *lim)
{
	blk_queue_rq_timeout(sdev->request_queue, scmd_timeout * HZ);

	if (ccusr_get_blkfeat(sdev) & CCIOP_BLKFEAT_STABLE_WRITES) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
		lim->features |= BLK_FEAT_STABLE_WRITES;
#else
		blk_queue_flag_set(QUEUE_FLAG_STABLE_WRITES,
				   sdev->request_queue);
#endif
	}

	return 0;
}
#else

static void ccusr_stable_writes_set(struct scsi_device *sdev)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0) || \
     (defined(RHEL_MAJOR) && (RHEL_MAJOR >= 8) && (RHEL_MINOR >= 6)))
	blk_queue_flag_set(QUEUE_FLAG_STABLE_WRITES, sdev->request_queue);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0)
	struct request_queue *q = sdev->request_queue;
	struct backing_dev_info *bdi = __builtin_choose_expr(
		__builtin_types_compatible_p(typeof(q->backing_dev_info),
					     typeof(bdi)),
		q->backing_dev_info, &q->backing_dev_info);
	bdi->capabilities |= BDI_CAP_STABLE_WRITES;
#endif
}

static int ccusr_slave_configure(struct scsi_device *sdev)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)
	blk_queue_update_dma_alignment(sdev->request_queue, 0xF);
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28)
	blk_queue_rq_timeout(sdev->request_queue, scmd_timeout * HZ);
#else
	sdev->timeout = scmd_timeout * HZ;
#endif

	if (ccusr_get_blkfeat(sdev) & CCIOP_BLKFEAT_STABLE_WRITES)
		ccusr_stable_writes_set(sdev);

	return 0;
}
#endif

#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 18, 0)
static int ccusr_adjust_disk_queue_depth(struct scsi_device *sdev,
					 int queue_depth)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 32)
static int ccusr_adjust_disk_queue_depth(struct scsi_device *sdev,
					 int queue_depth, int reason)
#else
static int ccusr_adjust_disk_queue_depth(struct scsi_device *sdev,
					 int queue_depth)
#endif
{
	struct ccusr_hba *hba = (struct ccusr_hba *)sdev->host->hostdata;

	if (queue_depth > hba->max_requests - CONFIG_CCUSR_IOCTL)
		queue_depth = hba->max_requests - CONFIG_CCUSR_IOCTL;

#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 18, 0)
	return scsi_change_queue_depth(sdev, queue_depth);
#else
	scsi_adjust_queue_depth(sdev, MSG_ORDERED_TAG, queue_depth);
	return queue_depth;
#endif
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 26)
typedef struct device_attribute ccusr_device_attribute;
#else
typedef struct class_device_attribute ccusr_device_attribute;
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 26)
static ssize_t ccusr_show_version(struct device *dev,
				  struct device_attribute *attr, char *buf)
#else
static ssize_t ccusr_show_version(struct class_device *dev, char *buf)
#endif
{
	return snprintf(buf, PAGE_SIZE, "%s\n", driver_ver);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 26)
static ssize_t ccusr_show_fw_version(struct device *dev,
				     struct device_attribute *attr, char *buf)
#else
static ssize_t ccusr_show_fw_version(struct class_device *dev, char *buf)
#endif
{
	struct Scsi_Host *host = class_to_shost(dev);
	struct ccusr_hba *hba = (struct ccusr_hba *)host->hostdata;

	return snprintf(buf, PAGE_SIZE, "%s\n",
			hba->ext_regs ? hba->ext_regs->fw_version : "N/A");
}

static ccusr_device_attribute ccusr_attr_version = {
	.attr = {
		.name = "driver-version",
		.mode = 0444,
	},
	.show = ccusr_show_version,
};

static ccusr_device_attribute ccusr_attr_fw_version = {
	.attr =	{
		.name = "fw-version",
		.mode = 0444,
	},
	.show = ccusr_show_fw_version,
};

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 16, 0)
static ccusr_device_attribute *ccusr_attrs[] = {
	&ccusr_attr_version,
	&ccusr_attr_fw_version,
	NULL,
};
#else
static struct attribute *ccusr_sysfs_attrs[] = {
	&ccusr_attr_version.attr,
	&ccusr_attr_fw_version.attr,
	NULL,
};

static const struct attribute_group ccusr_sysfs_group = {
	.attrs = ccusr_sysfs_attrs
};

const struct attribute_group *ccusr_host_groups[] = {
	&ccusr_sysfs_group,
	NULL,
};
#endif

#if CONFIG_CCUSR_IOCTL
struct ccusr_map_data {
	struct sg_io_hdr hdr;
	void *kbuf;
	u32 buflen, insize, outsize;
	dma_addr_t dma_handle;
	struct scsi_cmnd srb;
	struct ccusr_cmd_priv srb_priv;
	u8 sense[SCSI_SENSE_BUFFERSIZE];
};

static int ccusr_ioctl_map(struct Scsi_Host *host, void __user *arg,
			   struct ccusr_map_data *md)
{
	int r = -EINVAL;

	if (copy_from_user(&md->hdr, arg, sizeof(struct sg_io_hdr)))
		return -EFAULT;

	if (md->hdr.interface_id != 'S' || md->hdr.cmd_len > 16)
		return -EINVAL;

	if (copy_from_user(md->srb.cmnd, md->hdr.cmdp, md->hdr.cmd_len))
		return -EFAULT;

	/* setup minimal md->srb fileds for finish_req to work */
	md->srb.cmd_len = md->hdr.cmd_len;
	md->srb.sdb.length = md->hdr.dxfer_len;
	md->srb.sense_buffer = md->sense;
	md->srb.result = -1;
	md->srb.cmd_len = md->hdr.cmd_len;

	md->buflen = md->hdr.dxfer_len;
	if (!md->buflen)
		return 0;

	md->buflen = (md->buflen + 3) & ~3;
	md->kbuf = kmalloc(md->buflen, GFP_KERNEL);
	if (!md->kbuf)
		return -ENOMEM;

	switch (md->hdr.dxfer_direction) {
	case SG_DXFER_TO_DEV:
		md->insize = md->hdr.dxfer_len;
		md->outsize = 0;
		break;
	case SG_DXFER_FROM_DEV:
		md->insize = 0;
		md->outsize = md->hdr.dxfer_len;
		break;
	case SG_DXFER_TO_FROM_DEV:
		if (md->srb.cmnd[0] == 2) {
			md->insize = le32_to_cpu(*(__le32 *)&md->srb.cmnd[8]);
			md->outsize = le32_to_cpu(*(__le32 *)&md->srb.cmnd[12]);
			if (!md->insize && !md->outsize)
				goto free_kbuf;
		} else
			md->insize = md->outsize = md->hdr.dxfer_len;
		break;
	default:
		goto free_kbuf;
	}

	if (md->insize == 0)
		md->srb.sc_data_direction = DMA_FROM_DEVICE;
	else if (md->outsize == 0)
		md->srb.sc_data_direction = DMA_TO_DEVICE;
	else
		md->srb.sc_data_direction = DMA_BIDIRECTIONAL;

	if (md->insize &&
	    copy_from_user(md->kbuf, md->hdr.dxferp, md->insize)) {
		r = -EFAULT;
		goto free_kbuf;
	}

	md->dma_handle = dma_map_single(host->dma_dev, md->kbuf, md->buflen,
					md->srb.sc_data_direction);

	if (dma_mapping_error(host->dma_dev, md->dma_handle)) {
		r = -ENOMEM;
		goto free_kbuf;
	}

	return 0;
free_kbuf:
	kfree(md->kbuf);
	return r;
}

static int ccusr_ioctl_unmap(struct Scsi_Host *host, void __user *arg,
			     struct ccusr_map_data *md)
{
	int r = 0;

	md->hdr.host_status = (md->srb.result >> 16) & 0xff;
	md->hdr.resid = scsi_get_resid(&md->srb);
	if (copy_to_user(arg, &md->hdr, sizeof(struct sg_io_hdr)))
		r = -EFAULT;

	if (md->buflen) {
		dma_unmap_single(host->dma_dev, md->dma_handle, md->buflen,
				 md->srb.sc_data_direction);
		if (r == 0 && md->outsize &&
		    copy_to_user(md->hdr.dxferp, md->kbuf,
				 md->hdr.dxfer_len - md->hdr.resid))
			r = -EFAULT;
		kfree(md->kbuf);
	}

	return r;
}

static int ccusr_ioctl(struct scsi_device *dev, unsigned int cmd,
		       void __user *arg)
{
	struct Scsi_Host *host = dev->host;
	struct ccusr_hba *hba = (struct ccusr_hba *)host->hostdata;
	struct ccusr_req_tracker *req;
	union cciop_inbound_entry entry;
	struct ccusr_map_data *md;
	int r;

	if (cmd != 0xFFFF2285 || dev->id != hba->max_devices)
		return -EINVAL;

	md = kzalloc(sizeof(*md), GFP_KERNEL);
	if (!md)
		return -ENOMEM;

	mutex_lock(&hba->ioctl_lock);

	r = ccusr_ioctl_map(host, arg, md);
	if (r)
		goto unlock_ret;

	req = get_req(hba);

	if (!req) {
		ccusr_printk(KERN_ERR, "no free req\n");
		r = -EBUSY;
		goto unmap;
	}

	entry.data.qword0 = 0;
	entry.request.type = 0;
	entry.request.request_address = cpu_to_le64(req->request_phy);

	req->srb = &md->srb;
	req->request->type = cpu_to_le32(CCIOP_REQUEST_TYPE_SCSI);
	req->request->flags =
		cpu_to_le32((md->outsize ? CCIOP_REQUEST_FLAG_DATA_IN : 0) |
			    (md->insize ? CCIOP_REQUEST_FLAG_DATA_OUT : 0));
	req->request->reply_context = cpu_to_le64((unsigned long)req);
	req->request->devid = cpu_to_le32(dev->id);
	req->request->dataxfer_length = cpu_to_le32(md->buflen);

	memcpy(req->request->cdb, md->srb.cmnd, md->srb.cmd_len);

	if (md->buflen) {
		req->request->prd_length = cpu_to_le32(1);
		req->request->prdt->address = cpu_to_le64(md->dma_handle);
		req->request->prdt->dbc = cpu_to_le32(md->buflen - 1);
		entry.request.size =
			cpu_to_le16(offsetof(struct cciop_request, prdt) +
				    sizeof(struct cciop_prd));
	} else {
		req->request->prd_length = 0;
		entry.request.size =
			cpu_to_le16(offsetof(struct cciop_request, prdt));
	}

	inbound_write(hba, &entry);
	while (!wait_event_interruptible_timeout(
		hba->ioctl_wq, md->srb.result != -1,
		msecs_to_jiffies(md->hdr.timeout))) {
		ccusr_printk(KERN_ERR, "ioctl timeout\n");
		__ccusr_reset_hba(hba, NULL);
	}

unmap:
	r = ccusr_ioctl_unmap(host, arg, md);
unlock_ret:
	mutex_unlock(&hba->ioctl_lock);
	kfree(md);
	return r;
}
#endif

static struct scsi_host_template driver_template = {
	.module = THIS_MODULE,
	.name = driver_name,
	.queuecommand = ccusr_qcmd,
	.eh_host_reset_handler = ccusr_reset,
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 10, 0)
	.slave_configure = ccusr_slave_configure,
#elif LINUX_VERSION_CODE < KERNEL_VERSION(6, 14, 0)
	.device_configure = ccusr_sdev_configure,
#else
	.sdev_configure = ccusr_sdev_configure,
#endif
	.info = ccusr_info,
	.emulated = 0,
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 0, 0)
	.use_clustering = ENABLE_CLUSTERING,
#endif
	.proc_name = driver_name,
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 16, 0)
	.shost_attrs = ccusr_attrs,
#else
	.shost_groups = ccusr_host_groups,
#endif
	.this_id = -1,
	.change_queue_depth = ccusr_adjust_disk_queue_depth,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 17, 0)
	.cmd_size = sizeof(struct ccusr_cmd_priv),
#endif
#if CONFIG_CCUSR_IOCTL
	.ioctl = ccusr_ioctl,
#endif
};

static struct ccusr_req_tracker *alloc_req(struct ccusr_hba *hba)
{
	struct ccusr_req_tracker *req;

	req = kzalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		return NULL;

	req->request =
		dma_pool_alloc(hba->req_pool, GFP_KERNEL, &req->request_phy);
	if (!req->request)
		goto free_req;

	req->sense =
		dma_pool_alloc(hba->sense_pool, GFP_KERNEL, &req->sense_phy);
	if (!req->sense)
		goto free_req_pool;

	req->request->sense_address = cpu_to_le64(req->sense_phy);
	req->request->sense_length = cpu_to_le32(CCUSR_SENSE_LENGTH);
	return req;

free_req_pool:
	dma_pool_free(hba->req_pool, req->request, req->request_phy);
free_req:
	kfree(req);
	return NULL;
}

static int ccusr_memalloc(struct ccusr_hba *hba)
{
	u32 i;
	struct ccusr_req_tracker *req;

	hba->req_pool = dma_pool_create(
		driver_name, &hba->pcidev->dev,
		offsetof(struct cciop_request, prdt) +
			hba->host->sg_tablesize * sizeof(struct cciop_prd),
		16, 0);
	if (!hba->req_pool)
		return -ENOMEM;

	hba->sense_pool = dma_pool_create(driver_name, &hba->pcidev->dev,
					  CCUSR_SENSE_LENGTH, 16, 0);
	if (!hba->sense_pool)
		goto destroy_req_pool;

	for (i = 0; i < hba->max_requests; i++) {
		req = alloc_req(hba);
		if (!req)
			goto alloc_err;
		req->next = hba->req_list;
		hba->req_list = req;
	}

	return 0;

alloc_err:
	while ((req = get_req(hba))) {
		dma_pool_free(hba->req_pool, req->request, req->request_phy);
		dma_pool_free(hba->sense_pool, req->sense, req->sense_phy);
		kfree(req);
	}

	dma_pool_destroy(hba->sense_pool);
	hba->sense_pool = NULL;
destroy_req_pool:
	dma_pool_destroy(hba->req_pool);
	hba->req_pool = NULL;
	return -ENOMEM;
}

static void ccusr_memfree(struct ccusr_hba *hba)
{
	struct ccusr_req_tracker *req;

	while (hba->req_list) {
		req = hba->req_list;
		hba->req_list = req->next;
		dma_pool_free(hba->req_pool, req->request, req->request_phy);
		dma_pool_free(hba->sense_pool, req->sense, req->sense_phy);
		kfree(req);
	}

	dma_pool_destroy(hba->sense_pool);
	hba->sense_pool = NULL;

	dma_pool_destroy(hba->req_pool);
	hba->req_pool = NULL;
}

static void ccusr_reset_pci_bus(struct pci_dev *dev)
{
	u16 ctrl;

	pci_read_config_word(dev, PCI_BRIDGE_CONTROL, &ctrl);
	ctrl |= PCI_BRIDGE_CTL_BUS_RESET;
	pci_write_config_word(dev, PCI_BRIDGE_CONTROL, ctrl);
	msleep(2);
	ctrl &= ~PCI_BRIDGE_CTL_BUS_RESET;
	pci_write_config_word(dev, PCI_BRIDGE_CONTROL, ctrl);
	msleep(1000);
}

static void ccusr_pci_reset(struct pci_dev *dev)
{
	pci_save_state(dev);
	pci_write_config_word(dev, PCI_COMMAND, PCI_COMMAND_INTX_DISABLE);
	ccusr_reset_pci_bus(dev->bus->self);
	pci_restore_state(dev);
}

static int ccusr_probe(struct pci_dev *pcidev, const struct pci_device_id *id)
{
	struct Scsi_Host *host = NULL;
	struct ccusr_hba *hba;
	u64 dmamask;
	int err;

	ccusr_printk(KERN_INFO, "adapter at PCI bus %d\n", pcidev->bus->number);

	if (init_reset || reset_devices)
		ccusr_pci_reset(pcidev);

	if (pci_enable_device(pcidev)) {
		ccusr_printk(KERN_ERR, "fail to enable pci device\n");
		return -ENODEV;
	}

	pci_set_master(pcidev);

	/* Enable 64bit DMA if possible */
	dmamask = 0xffffffffffffffffULL;
	err = dma_set_mask(&pcidev->dev, dmamask);
	if (err) {
		dmamask = 0xffffffffUL;
		err = dma_set_mask(&pcidev->dev, dmamask);
	}

	if (err) {
		ccusr_printk(KERN_ERR, "fail to set dma_mask\n");
		goto disable_pci_device;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 34)
	/* no need check the return value as the DMA API guarantees that the
	 * coherent DMA mask can be set to the same or smaller than the
	 * streaming DMA mask.
	 */
	dma_set_coherent_mask(&pcidev->dev, dmamask);
#endif

	err = pci_request_regions(pcidev, driver_name);
	if (err) {
		ccusr_printk(KERN_ERR, "pci_request_regions failed\n");
		goto disable_pci_device;
	}

	if (use_msi) {
		err = pci_enable_msi(pcidev);
		if (err) {
			ccusr_printk(KERN_ERR,
				     "fail to enable MSI - use legacy mode\n");
			use_msi = 0;
		}
	}

	host = scsi_host_alloc(&driver_template, sizeof(struct ccusr_hba));
	if (!host) {
		err = -ENOMEM;
		ccusr_printk(KERN_ERR, "fail to alloc scsi host\n");
		goto disable_msi;
	}

	hba = (struct ccusr_hba *)host->hostdata;
	memset(hba, 0, sizeof(struct ccusr_hba));

	hba->pcidev = pcidev;
	hba->host = host;

	spin_lock_init(&hba->req_list_lock);
	spin_lock_init(&hba->inbound_lock);

	init_waitqueue_head(&hba->msg_wq);
	init_waitqueue_head(&hba->ioctl_wq);
	mutex_init(&hba->ioctl_lock);
	atomic_set(&hba->outstanding_reqs, 0);

	host->max_lun = 1;
	host->max_channel = 0;
	host->io_port = 0;
	host->n_io_port = 0;
	host->irq = pcidev->irq;

	err = ccusr_map_pci_bar(hba);
	if (err)
		goto free_scsi_host;

	err = ccusr_wait_ready(hba, 200);
	if (err) {
		ccusr_printk(KERN_ERR, "host%d: firmware not ready\n",
			     hba->host->host_no);
		goto unmap_pci_bar;
	}

	if (readw(&hba->regs->iop_version) != CCIOP_VERSION) {
		ccusr_printk(KERN_ERR, "host%d: CCIOP_VERSION mismatch\n",
			     hba->host->host_no);
		goto unmap_pci_bar;
	}

	hba->max_hw_requests = readl(&hba->regs->max_requests);
	hba->max_requests = hba->max_hw_requests;
	if (reset_devices && hba->max_requests > 32)
		hba->max_requests = 32;

	hba->max_sg_count = readl(&hba->regs->max_sg_count);
	hba->dataxfer_length = readl(&hba->regs->dataxfer_length);
	hba->max_devices = readl(&hba->regs->max_devices);
	hba->inbound_wptr = readl(&hba->regs->inbound_wptr);
	hba->outbound_rptr = readl(&hba->regs->outbound_rptr);

	hba->ext_regs = (void *)&hba->regs->q[hba->max_hw_requests * 4];
	if (readl(&hba->ext_regs->signature) == CCIOP_IF_EXT_SIG) {
		ccusr_printk(KERN_INFO, "fw_version: %s\n",
			     hba->ext_regs->fw_version);
		memcpy_toio(hba->ext_regs->driver_name, driver_name,
			    min(sizeof(hba->ext_regs->driver_name),
				strlen(driver_name)));
		memcpy_toio(hba->ext_regs->driver_version, driver_ver,
			    min(sizeof(hba->ext_regs->driver_version),
				strlen(driver_ver)));
	} else
		hba->ext_regs = NULL;

	host->max_sectors = hba->dataxfer_length >> 9;
	host->max_id = hba->max_devices + 1;
	host->sg_tablesize =
		min_t(u32, hba->max_sg_count,
		      (PAGE_SIZE - offsetof(struct cciop_request, prdt)) /
			      sizeof(struct cciop_prd));
	host->can_queue = hba->max_requests - CONFIG_CCUSR_IOCTL;
	host->cmd_per_lun = hba->max_requests - CONFIG_CCUSR_IOCTL;
	host->max_cmd_len = 16;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 10, 0)
	host->dma_alignment = 0xF;
#endif

	ccusr_printk(KERN_INFO, "max_requests: 0x%x\n", hba->max_requests);
	ccusr_printk(KERN_INFO, "max_sg_count: 0x%x\n", hba->max_sg_count);
	ccusr_printk(KERN_INFO, "dataxfer_length: 0x%x\n",
		     hba->dataxfer_length);
	ccusr_printk(KERN_INFO, "max_devices: 0x%x\n", hba->max_devices);

	err = ccusr_memalloc(hba);
	if (err) {
		ccusr_printk(KERN_ERR, "host%d: memalloc failed\n",
			     hba->host->host_no);
		goto unmap_pci_bar;
	}

	pci_set_drvdata(pcidev, host);

	err = request_irq(pcidev->irq, ccusr_intr, IRQF_SHARED, driver_name,
			  hba);
	if (err) {
		ccusr_printk(KERN_ERR, "request irq %d failed\n", pcidev->irq);
		goto free_mem;
	}

	ccusr_enable_intr(hba);

	/* config and enable background tasks */
	post_message(hba, CCIOP_INBOUND_MSG_SETID, host->host_no, NULL);
	post_message(hba, CCIOP_INBOUND_MSG_SETADDR, pcidev->bus->number << 16,
		     NULL);
	post_message(hba, CCIOP_INBOUND_MSG_TASK, 1, NULL);

	err = scsi_add_host(host, &pcidev->dev);
	if (err) {
		ccusr_printk(KERN_ERR, "host%d: scsi_add_host failed\n",
			     hba->host->host_no);
		goto disable_irq;
	}

	scsi_scan_host(host);

	dprintk("host%d probed successfully\n", hba->host->host_no);
	return 0;

	free_irq(hba->pcidev->irq, hba);

disable_irq:
	ccusr_disable_intr(hba);

free_mem:
	ccusr_memfree(hba);

unmap_pci_bar:
	ccusr_unmap_pci_bar(hba);

free_scsi_host:
	scsi_host_put(host);

disable_msi:
	if (use_msi)
		pci_disable_msi(pcidev);

	pci_release_regions(pcidev);

disable_pci_device:
	pci_disable_device(pcidev);
	return err;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 15)
static void ccusr_shutdown(struct device *dev)
{
	struct pci_dev *pcidev = to_pci_dev(dev);
#else
static void ccusr_shutdown(struct pci_dev *pcidev)
{
#endif
	struct Scsi_Host *host = pci_get_drvdata(pcidev);
	struct ccusr_hba *hba = (struct ccusr_hba *)host->hostdata;
	struct ccusr_msg_reply reply;

	reply.iop_status = CCIOP_STATUS_PENDING;

	ccusr_printk(KERN_INFO, "shutdown\n");

	post_message(hba, CCIOP_INBOUND_MSG_SHUTDOWN, 0xFFFFFFFF, &reply);

	while (!wait_event_timeout(hba->msg_wq,
				   reply.iop_status != CCIOP_STATUS_PENDING,
				   msecs_to_jiffies(30000))) {
		ccusr_printk(KERN_ERR, "shutdown timeout\n");
		__ccusr_reset_hba(hba, NULL);
	}

	if (reply.iop_status != CCIOP_STATUS_SUCCESS) {
		ccusr_printk(KERN_ERR, "shutdown failed status %d\n",
			     reply.iop_status);
	}

	/* disable interrupts */
	ccusr_disable_intr(hba);
}

static void ccusr_remove(struct pci_dev *pcidev)
{
	struct Scsi_Host *host = pci_get_drvdata(pcidev);
	struct ccusr_hba *hba = (struct ccusr_hba *)host->hostdata;

	dprintk("remove host%d\n", hba->host->host_no);

	scsi_remove_host(host);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 15)
	ccusr_shutdown(&pcidev->dev);
#else
	ccusr_shutdown(pcidev);
#endif

	free_irq(hba->pcidev->irq, hba);
	pci_disable_msi(hba->pcidev);
	ccusr_memfree(hba);
	ccusr_unmap_pci_bar(hba);
	pci_release_regions(hba->pcidev);
	pci_set_drvdata(hba->pcidev, NULL);
	pci_disable_device(hba->pcidev);
	scsi_host_put(host);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 32)
static int __maybe_unused ccusr_suspend(struct device *dev)
{
	struct pci_dev *pcidev = to_pci_dev(dev);
	struct Scsi_Host *host = pci_get_drvdata(pcidev);
	struct ccusr_hba *hba = (struct ccusr_hba *)host->hostdata;

	ccusr_shutdown(pcidev);
	free_irq(hba->pcidev->irq, hba);

	return 0;
}

static int __maybe_unused ccusr_resume(struct device *dev)
{
	struct pci_dev *pcidev = to_pci_dev(dev);
	struct Scsi_Host *host = pci_get_drvdata(pcidev);
	struct ccusr_hba *hba = (struct ccusr_hba *)host->hostdata;
	int err;

	err = ccusr_wait_ready(hba, 2000);
	if (err) {
		ccusr_printk(KERN_ERR, "host%d: firmware not ready\n",
			     hba->host->host_no);
		return -ENODEV;
	}

	hba->inbound_wptr = readl(&hba->regs->inbound_wptr);
	hba->outbound_rptr = readl(&hba->regs->outbound_rptr);

	err = request_irq(pcidev->irq, ccusr_intr, IRQF_SHARED, driver_name,
			  hba);
	if (err) {
		ccusr_printk(KERN_ERR, "request irq %d failed\n", pcidev->irq);
		return err;
	}

	ccusr_enable_intr(hba);

	/* config and enable background tasks */
	post_message(hba, CCIOP_INBOUND_MSG_SETID, host->host_no, NULL);
	post_message(hba, CCIOP_INBOUND_MSG_SETADDR, pcidev->bus->number << 16,
		     NULL);
	post_message(hba, CCIOP_INBOUND_MSG_TASK, 1, NULL);
	return 0;
}

static SIMPLE_DEV_PM_OPS(ccusr_pm_ops, ccusr_suspend, ccusr_resume);
#endif

static struct pci_device_id ccusr_id_table[] = {
	{ PCI_DEVICE(0x9000, 0x8108), 0, 0, 0 },
	{ PCI_DEVICE(0x9000, 0x8116), 0, 0, 0 },
	{ PCI_DEVICE(0x9000, 0x8016), 0, 0, 0 },
	{ PCI_DEVICE(0x9000, 0x8216), 0, 0, 0 },
	{ PCI_DEVICE(0x9000, 0x6104), 0, 0, 0 },
	{ PCI_DEVICE(0x9000, 0x6004), 0, 0, 0 },
	{ PCI_DEVICE(0xFACE, 0x6316), 0, 0, 0 },
	{ PCI_DEVICE(0xFACE, 0x6312), 0, 0, 0 },
	{ PCI_DEVICE(0xFACE, 0x6308), 0, 0, 0 },
	{},
};

MODULE_DEVICE_TABLE(pci, ccusr_id_table);

static struct pci_driver ccusr_pci_driver = {
	.name = driver_name,
	.id_table = ccusr_id_table,
	.probe = ccusr_probe,
	.remove = ccusr_remove,
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 15)
	.driver = {
		.shutdown = ccusr_shutdown,
	},
#else
	.shutdown = ccusr_shutdown,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 32)
	.driver.pm = &ccusr_pm_ops,
#endif
};

static int __init ccusr_module_init(void)
{
	ccusr_printk(KERN_INFO, "driver version: %s\n", driver_ver);
	return pci_register_driver(&ccusr_pci_driver);
}

static void __exit ccusr_module_exit(void)
{
	pci_unregister_driver(&ccusr_pci_driver);
}

module_init(ccusr_module_init);
module_exit(ccusr_module_exit);

MODULE_AUTHOR("VolansComputer");
MODULE_DESCRIPTION("CCUSR HW-RAID Adapter Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRIVER_VERSION);
