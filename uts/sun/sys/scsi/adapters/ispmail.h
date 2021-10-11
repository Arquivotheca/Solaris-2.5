/*
 * Copyright (c) 1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef _SYS_SCSI_ADAPTERS_ISPMAIL_H
#define	_SYS_SCSI_ADAPTERS_ISPMAIL_H

#pragma ident	"@(#)ispmail.h	1.14	95/05/08 SMI"

/*
 * isp mailbox definitions
 */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Mailbox Register 0 status bit definitions.
 */
#define	ISP_MBOX_EVENT_MASK			0xF000
#define	ISP_MBOX_EVENT_ASYNCH			0x8000
#define	ISP_MBOX_EVENT_CMD			0x4000

#define	ISP_MBOX_STATUS_MASK			0x00FF
#define	ISP_MBOX_STATUS_OK			0x00
#define	ISP_MBOX_STATUS_INVALID_CMD		0x01
#define	ISP_MBOX_STATUS_INVALID_PARAMS		0x02
#define	ISP_MBOX_STATUS_BOOT_ERR		0x03
#define	ISP_MBOX_STATUS_FIRMWARE_ERR		0x04

#define	ISP_MBOX_ASYNC_RESET			0x01
#define	ISP_MBOX_ASYNC_ERR			0x02
#define	ISP_MBOX_ASYNC_REQ_DMA_ERR		0x03
#define	ISP_MBOX_ASYNC_RESP_DMA_ERR		0x04
#define	ISP_MBOX_ASYNC_WAKEUP			0x05
#define	ISP_MBOX_ASYNC_INT_RESET		0x06
#define	ISP_MBOX_ASYNC_INT_DEV_RESET		0x07
#define	ISP_MBOX_ASYNC_INT_ABORT		0x08

#define	ISP_MBOX_BUSY				0x04

#define	ISP_MBOX_EVENT_SBUS			0x01
#define	ISP_MBOX_EVENT_REQUEST			0x02

#define	ISP_GET_MBOX_STATUS(mailbox)		\
	(mailbox & ISP_MBOX_STATUS_MASK)
#define	ISP_GET_MBOX_EVENT(mailbox)		\
	(mailbox & ISP_MBOX_STATUS_MASK)

/* asynch event related defines */
#define	ISP_AEN_RESET	-1
#define	ISP_AEN_SUCCESS	1
#define	ISP_AEN_FAILURE	0

#define	ISP_MBOX_CMD_NOP				0x00
#define	ISP_MBOX_CMD_ABOUT_PROM				0x08
#define	ISP_MBOX_CMD_CHECKSUM_FIRMWARE			0x0E
#define	ISP_MBOX_CMD_STOP_FW				0x14
#define	ISP_MBOX_CMD_LOAD_RAM				0x01
#define	ISP_MBOX_CMD_START_FW				0x02
#define	ISP_MBOX_CMD_DUMP_RAM				0x03
#define	ISP_MBOX_CMD_LOAD_WORD				0x04
#define	ISP_MBOX_CMD_DUMP_WORD				0x05
#define	ISP_MBOX_CMD_WRAP_MAILBOXES			0x06
#define	ISP_MBOX_CMD_CHECKSUM				0x07
#define	ISP_MBOX_CMD_INIT_REQUEST_QUEUE			0x10
#define	ISP_MBOX_CMD_INIT_RESPONSE_QUEUE		0x11
#define	ISP_MBOX_CMD_SCSI_CMD				0x12
#define	ISP_MBOX_CMD_WAKE_UP				0x13
#define	ISP_MBOX_CMD_ABORT_IOCB				0x15
#define	ISP_MBOX_CMD_ABORT_DEVICE			0x16
#define	ISP_MBOX_CMD_ABORT_TARGET			0x17
#define	ISP_MBOX_CMD_BUS_RESET				0x18
#define	ISP_MBOX_CMD_STOP_QUEUE				0x19
#define	ISP_MBOX_CMD_START_QUEUE			0x1A
#define	ISP_MBOX_CMD_STEP_QUEUE				0x1B
#define	ISP_MBOX_CMD_ABORT_QUEUE			0x1C
#define	ISP_MBOX_CMD_GET_DEVICE_QUEUE_STATE		0x1D
#define	ISP_MBOX_CMD_GET_ISP_STAT			0x1E
#define	ISP_MBOX_CMD_GET_FIRMWARE_STATUS		0x1F
#define	ISP_MBOX_CMD_GET_SXP_CONFIG			0x2F
#define	ISP_MBOX_CMD_SET_SXP_CONFIG			0x3F
#define	ISP_MBOX_CMD_GET_SCSI_ID			0x20
#define	ISP_MBOX_CMD_SET_SCSI_ID			0x30
#define	ISP_MBOX_CMD_GET_SEL_TIMEOUT			0x21
#define	ISP_MBOX_CMD_SET_SEL_TIMEOUT			0x31
#define	ISP_MBOX_CMD_GET_RETRY_ATTEMPTS			0x22
#define	ISP_MBOX_CMD_SET_RETRY_ATTEMPTS			0x32
#define	ISP_MBOX_CMD_GET_AGE_LIMIT			0x23
#define	ISP_MBOX_CMD_SET_AGE_LIMIT			0x33
#define	ISP_MBOX_CMD_GET_CLOCK_RATE			0x24
#define	ISP_MBOX_CMD_SET_CLOCK_RATE			0x34
#define	ISP_MBOX_CMD_GET_PULL_UPS			0x25
#define	ISP_MBOX_CMD_SET_PULL_UPS			0x35
#define	ISP_MBOX_CMD_GET_DATA_TRANS_TIME		0x26
#define	ISP_MBOX_CMD_SET_DATA_TRANS_TIME		0x36
#define	ISP_MBOX_CMD_GET_SBUS_INTERFACE			0x27
#define	ISP_MBOX_CMD_SET_SBUS_INTERFACE			0x37
#define	ISP_MBOX_SBUS_INTERFACE_ENABLE_DMA_BURST	0x02
#define	ISP_MBOX_CMD_GET_TARGET_CAP			0x28
#define	ISP_MBOX_CMD_SET_TARGET_CAP			0x38
#define	ISP_MBOX_CMD_GET_DEVICE_QUEUE_PARAMS		0x29
#define	ISP_MBOX_CMD_SET_DEVICE_QUEUE_PARAMS		0x39
#define	ISP_MBOX_CMD_GET_QFULL_RETRIES			0x2A
#define	ISP_MBOX_CMD_SET_QFULL_RETRIES			0x3A
#define	ISP_MBOX_CMD_GET_QFULL_RETRY_INTERVAL		0x2B
#define	ISP_MBOX_CMD_SET_QFULL_RETRY_INTERVAL		0x3B

#define	ISP_CAP_DISCONNECT	0x8000
#define	ISP_CAP_PARITY		0x4000
#define	ISP_CAP_WIDE		0x2000
#define	ISP_CAP_SYNC		0x1000
#define	ISP_CAP_TAG		0x0800
#define	ISP_CAP_AUTOSENSE	0x0400
#define	ISP_CAP_ERRSTOP		0x0200
#define	ISP_CAP_ERRSYNC		0x0100
#define	ISP_10M_SYNC_PERIOD	0x0019
#define	ISP_10M_SYNC_OFFSET	0x000C
#define	ISP_10M_SYNC_PARAMS	((ISP_10M_SYNC_OFFSET << 8) | \
				ISP_10M_SYNC_PERIOD)
#define	ISP_8M_SYNC_PERIOD	0x0025
#define	ISP_8M_SYNC_OFFSET	0x000C
#define	ISP_8M_SYNC_PARAMS	((ISP_8M_SYNC_OFFSET << 8) | \
				ISP_8M_SYNC_PERIOD)
#define	ISP_5M_SYNC_PERIOD	0x0032
#define	ISP_5M_SYNC_OFFSET	0x000C
#define	ISP_5M_SYNC_PARAMS	((ISP_5M_SYNC_OFFSET << 8) | \
				ISP_5M_SYNC_PERIOD)
#define	ISP_4M_SYNC_PERIOD	0x0041
#define	ISP_4M_SYNC_OFFSET	0x000C
#define	ISP_4M_SYNC_PARAMS	((ISP_4M_SYNC_OFFSET << 8) | \
				ISP_4M_SYNC_PERIOD)

/* mailbox related structures and defines */
#define	ISP_MAX_MBOX_REGS		6
#define	ISP_MBOX_CMD_TIMEOUT		10
#define	ISP_MBOX_CMD_RETRY_CNT		1


#define	ISP_MBOX_CMD_FLAGS_COMPLETE	0x01
#define	ISP_MBOX_CMD_FLAGS_Q_NOT_INIT	0x02

/* mailbox command struct */
struct isp_mbox_cmd {
	u_int		timeout;	/* timeout for cmd */
	u_char		retry_cnt;	/* retry count */
	u_char		n_mbox_out;	/* no of mbox out regs wrt driver */
	u_char		n_mbox_in;	/* no of mbox in  regs wrt driver */
	u_short		mbox_out [ISP_MAX_MBOX_REGS]; /* outgoing registers  */
	u_short		mbox_in  [ISP_MAX_MBOX_REGS]; /* incoming registers  */
};
/* MEMBERS PROTECTED BY "Semaphore": timeout, retry_cnt		*/
/* MEMBERS PROTECTED BY "Semaphore": n_mbox_out, n_mbox_in	*/
/* MEMBERS PROTECTED BY "Semaphore": mbox_out, mbox_in		*/


/* isp mailbox struct */
struct isp_mbox {
	ksema_t			mbox_sema;   /* sema to sequentialize access */
	u_char			mbox_flags;  /* mbox register flags */
	struct isp_mbox_cmd 	mbox_cmd;    /* mbox command */
};


#define	ISP_MBOX_CMD_BUSY_WAIT_TIME		1    /* sec */
#define	ISP_MBOX_CMD_BUSY_POLL_DELAY_TIME	100  /* usecs */


#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SCSI_ADAPTERS_ISPMAIL_H */
