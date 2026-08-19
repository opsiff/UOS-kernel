/*
 * Copyright (C) VolansComputer. All rights reserved.
 */
#ifndef _CCIOP_H_
#define _CCIOP_H_

#define CCIOP_VERSION 1

/*
  Inbound queue entry format (16 bytes)

   Normal request

   63     55      47      39      31      23      15       76543210
   +-------+-------+-------+-------+-------+-------+-------+-------
   |                               |    ReqSize    |       |     00
   ----------------------------------------------------------------
   |                InboundDescriptorAddress
   ----------------------------------------------------------------

   Message request

   63     55      47      39      31      23      15       76543210
   +-------+-------+-------+-------+-------+-------+-------+-------
   |       MsgParameter            |               | Code  |     01
   ----------------------------------------------------------------
   |                ReplyContext
   ----------------------------------------------------------------

  Outbound queue entry format (16 bytes)

   Success w/o additional data

   63     55      47      39      31      23      15       76543210
   +-------+-------+-------+-------+-------+-------+-------+-------
   |                ReplyContext[63:2]                          |00
   ----------------------------------------------------------------

   Reply w/ additional data
   63     55      47      39      31      23      15       76543210
   +-------+-------+-------+-------+-------+-------+-------+-------
   |                ReplyContext[63:2]                           10
   ----------------------------------------------------------------
   |        DataTransferLength     |   reserved    |ScsiSt |iopStat
   ----------------------------------------------------------------

   Message reply
   63     55      47      39      31      23      15       76543210
   +-------+-------+-------+-------+-------+-------+-------+-------
   |       MsgParameter            |ScsiSt |iopStat| Code  |     11
   ----------------------------------------------------------------
   |                ReplyContext
   ----------------------------------------------------------------

   Outbound message
   63     55      47      39      31      23      15       76543210
   +-------+-------+-------+-------+-------+-------+-------+-------
   |       MsgParameter            |               | Code  |     01
   ----------------------------------------------------------------
 */
struct cciop_if_regs {
	/* read-only registers */
	VRC_LE16 iop_version;
	VRC_U8 iop_state;
	VRC_U8 irq_mask;
	VRC_LE32 max_requests;
	VRC_LE32 max_sg_count;
	VRC_LE32 dataxfer_length;
	VRC_LE32 max_devices;
	VRC_LE32 sdram_size_mb;
	/* read-write registers */
	VRC_LE32 inbound_wptr;
	VRC_LE32 inbound_rptr;
	VRC_LE32 outbound_wptr;
	VRC_LE32 outbound_rptr;
	VRC_U64 highpri_request_qword0;
	VRC_U64 highpri_request_qword1;
	VRC_U64 q[];
};

#define CCIOP_IF_EXT_SIG 0x504F4943

struct cciop_if_ext {
	VRC_LE32 signature;
	char fw_version[24];
	char driver_name[8];
	char driver_version[24];
	/* ... */
};

union cciop_inbound_entry {
	struct {
		VRC_U32 dword0;
		VRC_U32 dword1;
		VRC_U32 dword2;
		VRC_U32 dword3;
	} dword;
	struct {
		VRC_U64 qword0;
		VRC_U64 qword1;
	} data;
	struct {
		VRC_U8 type;
		VRC_U8 reserved;
		VRC_LE16 size;
		VRC_U32 reserved2;
		VRC_LE64 request_address;
	} request;
	struct {
		VRC_U8 type;
		VRC_U8 reserved;
		VRC_LE16 reserved2;
		VRC_LE32 reserved3;
	} general;
	struct {
		VRC_U8 type;
		VRC_U8 code;
		VRC_U16 reserved2;
		VRC_LE32 param;
		VRC_LE64 reply_context;
	} message;
};

union cciop_outbound_entry {
	struct {
		VRC_U64 qword0;
		VRC_U64 qword1;
	} data;
	struct {
#ifdef __BIG_ENDIAN_BITFIELD
		VRC_U8 reserved1 : 6;
		VRC_U8 type : 2;
#else
		VRC_U8 type : 2;
		VRC_U8 reserved1 : 6;
#endif
	} general;
	struct {
		VRC_LE64 reply_context;
		VRC_U8 iop_status;
		VRC_U8 scsi_status;
		VRC_U16 reserved;
		VRC_LE32 dataxfer_length;
	} request;
	struct {
		VRC_U8 type;
		VRC_U8 code;
		VRC_U8 iop_status;
		VRC_U8 scsi_status;
		VRC_LE32 param;
		VRC_LE64 reply_context;
	} message;
};

/* use AHCI PRD format */
struct cciop_prd {
	VRC_LE64 address;
	VRC_U32 reserved;
	VRC_LE32 dbc;
};

struct cciop_request {
	VRC_LE32 type;
	VRC_LE32 flags;
	VRC_LE64 reply_context;
	VRC_LE32 prd_length;
	VRC_LE32 dataxfer_length;
	VRC_LE64 sense_address;
	VRC_LE32 sense_length;
	VRC_LE32 devid;
	VRC_U8 cdb[16];
	struct cciop_prd prdt[];
};

enum cciop_request_type {
	CCIOP_REQUEST_TYPE_SCSI = 0,
	CCIOP_REQUEST_TYPE_MAX,
};

#define CCIOP_REQUEST_FLAG_DATA_IN  1
#define CCIOP_REQUEST_FLAG_DATA_OUT 2

#define CCIOP_BLKFEAT_STABLE_WRITES 1

enum cciop_inbound_message {
	CCIOP_INBOUND_MSG_NOP = 0,
	CCIOP_INBOUND_MSG_RESET,    /* MsgParameter: TargetId or 0xFFFFFFFF */
	CCIOP_INBOUND_MSG_FLUSH,    /* MsgParameter: TargetId or 0xFFFFFFFF */
	CCIOP_INBOUND_MSG_SHUTDOWN, /* MsgParameter: TargetId or 0xFFFFFFFF */
	CCIOP_INBOUND_MSG_TASK,     /* MsgParameter: 1 - Enable, 0 - Disable */
	CCIOP_INBOUND_MSG_SETID,    /* MsgParameter: vcore_id */
	CCIOP_INBOUND_MSG_SETADDR,  /* MsgParameter: PCI address */
	CCIOP_INBOUND_MSG_BLKFEAT,  /* MsgParameter: TargetId */
};

enum cciop_outbound_message {
	CCIOP_OUTBOUND_MSG_VDEV_ADDED = 0x80, /* MsgParameter: TargetId */
	CCIOP_OUTBOUND_MSG_VDEV_REMOVED,      /* MsgParameter: TargetId */
	CCIOP_OUTBOUND_MSG_VDEV_CHANGED,      /* MsgParameter: TargetId */
};

enum cciop_status {
	CCIOP_STATUS_PENDING = 0,
	CCIOP_STATUS_SUCCESS,
	CCIOP_STATUS_FAIL,
	CCIOP_STATUS_BUSY,
	CCIOP_STATUS_RESET,
	CCIOP_STATUS_INVALID_REQUEST,
	CCIOP_STATUS_BAD_TARGET,
	CCIOP_STATUS_CHECK_CONDITION,
};

#endif
