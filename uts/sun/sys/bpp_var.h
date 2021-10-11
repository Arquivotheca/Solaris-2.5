/*
 * Copyright (c) 1990, 1991, by Sun Microsystems, Inc.
 */

#ifndef	_SYS_BPP_VAR_H
#define	_SYS_BPP_VAR_H

#pragma ident	"@(#)bpp_var.h	1.10	95/09/26 SMI"

/*
 *	Local variables header file for the bidirectional parallel port
 *	driver (bpp) for the Zebra SBus card.
 */

#ifdef __cplusplus
extern "C" {
#endif

/*	#defines (not struct elements) below */

/* Other external ioctls are defined in bpp_io.h	*/
	/* FOR TEST - request fakeout simulate partial data transfer	*/
#define	BPPIOC_SETBC		_IOW('b', 7, u_int)
	/* FOR TEST - request value of DMA_BCNT at end of last data transfer */
#define	BPPIOC_GETBC		_IOR('b', 8, u_int)
	/* FOR TEST - get contents of device registers */
#define	BPPIOC_GETREGS		_IOR('b', 9, struct bpp_regs)
	/* FOR TEST - set special error code to simulate errors */
#define	BPPIOC_SETERRCODE	_IOW('b', 10, int)
	/* FOR TEST - get pointer to (fakely) "transferred" data */
#define	BPPIOC_GETFAKEBUF	_IOW('b', 11, u_char *)
	/* FOR TEST - test nested timeout  calls */
#define	BPPIOC_TESTTIMEOUT	_IO('b', 12)


#define	BPP_PROM_NAME	"SUNW,bpp"	/* name string in FCode prom */


/*	Structure definitions and locals #defines below */

struct bpp_unit				/* Unit structure - one per unit */
{
	kmutex_t	bpp_mutex;		/* mutex for any bpp op	*/
	kcondvar_t	wr_cv;			/* multiple writes lock */
	u_char		unit_open;		/* 1 means open.	*/
	u_char		unit_suspended;		/* 1 means suspended	*/
	u_char		open_inhibit;		/* 1 means inhibit.	*/
	u_char		versatec_allowed;	/* 1 means allow	*/
						/* versatec handshakes	*/
	int		openflags;		/* read-write flags	*/
						/* this unit opened with */
	int		bpp_transfer_timeout_ident;
						/* returned from timeout() */
						/* passed to untimeout() */
	int		bpp_fakeout_timeout_ident;
						/* returned from timeout() */
						/* passed to untimeout() */
	u_char		timeouts;		/* encoded pending timeouts */
	int		sbus_clock_cycle;	/* SBus clock cycle time */
						/* for this unit	*/
	enum	trans_type {			/* saves state last transfer */
	read_trans, 				/* on this unit - needed for */
	write_trans 				/* scanners		*/
	}		last_trans;

	ddi_iblock_cookie_t	bpp_block_cookie;
						/* interrupt block cookie */
						/* from ddi_add_intr */
	ddi_dma_handle_t	bpp_dma_handle;
						/* DMA handle given by */
						/* ddi_dma_buf_setup() */
	long		transfer_remainder;	/* number of bytes which */
						/* were not transferred */
						/* since they were across */
						/* the max DVMA boundary. */
						/* value is (0) when transfer */
						/* is within the boundary */
	dev_info_t		*dip;		/* Opaque devinfo info. */
	struct buf *bpp_buffer;			/* save address of buf  */
						/* for bpp_intr */
	volatile struct bpp_regs 	*bpp_regs_p;
						/* Device control regs. */
	struct bpp_transfer_parms transfer_parms;
						/* handshake and timing */
	struct bpp_pins		pins;		/* control pins 	*/
	struct bpp_error_status	error_stat;	/* snapshotted error cond */
};

/* defines for the timeouts field */
#define	NO_TIMEOUTS		0x00		/* No timeouts pending	*/
#define	TRANSFER_TIMEOUT	0x01		/* DVMA transfer */
#define	FAKEOUT_TIMEOUT		0x10		/* Hardware simulation	*/
#define	TEST1_TIMEOUT		0x20		/* Test timeout #1	*/
#define	TEST2_TIMEOUT		0x40		/* Test timeout #2	*/


/* lint garbage */
#ifdef lint

#define	IFLINT	IFTRUE

int _ZERO_;	/* "constant in conditional context" workaround */
#define	_ONE_	(!_ZERO_)

#else	/* lint */

#define	IFLINT	IFFALSE

#define	_ZERO_	0
#define	_ONE_	1

#endif	/* lint */

/* statement macro */
#define	_STMT(op)	do { op } while (_ZERO_)

#ifdef __cplusplus
}
#endif

#endif /* !_SYS_BPP_VAR_H */
