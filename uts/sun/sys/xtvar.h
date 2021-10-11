/*
 * Copyright (c) 1987-1991, by Sun Microsystems, Inc.
 */

#ifndef	_SYS_XTVAR_H
#define	_SYS_XTVAR_H

#pragma ident	"@(#)xtvar.h	1.6	93/03/29 SMI"	/* From SunOS  4.1.1 */

/*
 * Xylogics 472 multibus tape controller
 * controller/unit structure declaration
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Software state per tape transport.
 *
 * 1. A tape drive is a unique-open device; we refuse a second open.
 * 2. We keep track of the current position (file and record numbers).
 * 3. We remember if the last operation was a write on a tape, so if a tape
 *    is open read write and the last thing done is a write we can
 *    write a standard end of data (two eofs).
 * 4. We remember the status registers after the last command, using
 *    them internally and returning them to the SENSE ioctl.
 */

struct	xtunit	{
	dev_info_t	*c_dip;		/* device information */
	dev_t		c_dev;		/* device number like b_edev */
	int		c_ctlr;		/* controller number */
	struct buf	*c_sbufp;	/* buf for ioctl (No DMA). */
	struct buf	*c_bp_used;	/* buf currently used for IOCTLs */
	struct buf	*c_dma_buf;	/* buf currently used for DMA */
	long		c_resid;	/* copy of last bytecount */
	daddr_t 	c_timo;		/* time until timeout expires */
	int		c_timeout_id;	/* timeout id */
	daddr_t 	c_fileno;	/* file number */
	daddr_t 	c_fileno_eom;	/* file number at eom */
	daddr_t 	c_recno;	/* rec no  */
	int		c_open_opt;	/* open flags */
	u_int		c_curr_state;	/* state variable */
	u_int		c_callbck_cnt;  /* count of outstanding callbacks */
	off_t		c_lstpos;	/* last offset */
	int		c_next_cmd;	/* next command */
	int		c_last_cmd;	/* last command */
	int		c_old1_cmd;	/* old command */
	int		c_old2_cmd;	/* very old command */
	int		c_bp_error;	/* buf error, pass to xt_command */
	int		c_flags;	/* param (sys spec.) drive type */
	ddi_iblock_cookie_t	c_ibc;	/* block cookie */
	ddi_idevice_cookie_t	c_idc;	/* device cookie */
	kmutex_t	c_mutex_openf;	/* exclusive open */
	kmutex_t	c_mutex_callbck; /* callback mutex */
	kmutex_t	c_mutex;	/* General mutex */
	int		c_transfer;	/* = 1 reserves the transfer */
	volatile struct xydevice *c_io;	/* ptr to I/O space data */
	struct xtiopb	*c_iopb;	/* ptr to IOPB */

	kcondvar_t	c_transfer_wait;  /* var to wait for interrupt */
	kcondvar_t	c_alloc_buf_wait; /* var to wait for buf available */
	kcondvar_t	c_callbck_cv;	  /* callback condition variable */
	ddi_dma_handle_t c_udma_handle;   /* active DMA mapping for bp */
	ddi_dma_handle_t c_uiopb_handle;  /* active DMA mapping for iopb */
	u_long		c_iopb_dmac_addr; /* iopb addr from ctrl side */

	u_short 	c_next_cnt;	/* next command */
	u_short 	c_last_cnt;	/* last count */
	u_short 	c_old1_cnt;	/* old count */
	u_short 	c_old2_cnt;	/* very old count */
	u_short 	c_last_acnt;	/* last actual count */
	u_short 	c_old1_acnt;	/* old actual count */
	u_short 	c_old2_acnt;	/* very old actual count */
	u_short 	c_xtstat;	/* status bits from xt_status */
	u_short 	c_error;	/* copy of last erreg */
	u_char		c_alive;	/* device exists */
	u_char		c_present;	/* controller present */
	u_char		c_stuck;	/* ctrl stuck (no interrupt) */
	u_char		c_tact;		/* timeout is active */
	u_char		c_firstopen;	/* set on attach, cleared 1st open */
	u_char		c_read_hit_eof;	/* =1 if read reached EOF */
	u_char		c_svr4;		/* SVR4 behavior - if minor&Bit7 */
	u_char		c_eom;		/* Flag set if we are at EOM */
	char		c_old1_iopb[6]; /* 6 bytes of old IOPB */
	char		c_old2_iopb[6]; /* 6 bytes of older IOPB */
};

#ifdef __cplusplus
}
#endif

#endif	/* !_SYS_XTVAR_H */
