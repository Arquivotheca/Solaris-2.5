/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

#ifndef	_SYS_SER_ASYNC_H
#define	_SYS_SER_ASYNC_H

#pragma ident	"@(#)ser_async.h	1.22	95/01/09 SMI"

/*
 * Initial port setup parameters for async lines
 */

#include <sys/ksynch.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * The following macro can be used to generate the baud rate generator's
 * time constants.  The parameters are the input clock to the BRG (eg,
 * 5000000 for 5MHz) and the desired baud rate.  This macro assumes that
 * the clock needed is 16x the desired baud rate.
 */
#define	ZSTimeConst(InputClock, BaudRate) \
	(short)((((long)InputClock+(BaudRate*16)) \
	/ (2*(long)(BaudRate*16))) - 2)

#define	ZSDelayConst(Hertz, FifoSize, BitsByte, BaudRate) \
	(short)((((long)(Hertz)*(FifoSize)*(BitsByte)) \
	/ (long)(BaudRate)) + 1)

#define	ZSPEED(n)	ZSTimeConst(PCLK, n)

#define	ZFIFOSZ		3
#define	ZDELAY(n)	ZSDelayConst(HZ, ZFIFOSZ, NBBY, n)

#define	ISPEED		B9600
#define	ISPEED_SVID	B300
#define	IFLAGS		(CS7|CREAD|PARENB)
#define	IFLAGS_SVID	(CS8|CREAD|HUPCL)
#define	I_IFLAGS	0
#define	I_CFLAGS	((ISPEED << IBSHIFT) | ISPEED | CS8 | CREAD | HUPCL)

/*
 * Ring buffer and async line management definitions for CPU lines:
 */
#ifdef  _KERNEL
#ifndef _ASM
#define	RINGBITS	8		/* # of bits in ring ptrs */
#define	RINGSIZE	(1<<RINGBITS)	/* size of ring */
#define	RINGMASK	(RINGSIZE-1)
#define	RINGFRAC	2		/* fraction of ring to force flush */

#define	RING_INIT(zap)	((zap)->za_rput = (zap)->za_rget = 0)
#define	RING_CNT(zap)	(((zap)->za_rput - (zap)->za_rget) & RINGMASK)
#define	RING_FRAC(zap)	((int)RING_CNT(zap) >= (int)(RINGSIZE/RINGFRAC))
#define	RING_POK(zap, n) ((int)RING_CNT(zap) < (int)(RINGSIZE-(n)))
#define	RING_PUT(zap, c) \
	((zap)->za_ring[(zap)->za_rput++ & RINGMASK] =  (u_char)(c))
#define	RING_UNPUT(zap)	((zap)->za_rput--)
#define	RING_GOK(zap, n) ((int)RING_CNT(zap) >= (int)(n))
#define	RING_GET(zap)	((zap)->za_ring[(zap)->za_rget++ & RINGMASK])
#define	RING_EAT(zap, n) ((zap)->za_rget += (n))

/*
 *  To process parity errors/breaks in-band
 */
#define	SBITS		8
#define	S_UNMARK	0x00FF
#define	S_PARERR	(0x01<<SBITS)
#define	S_BREAK		(0x02<<SBITS)
#define	RING_MARK(zap, c, s) \
	((zap)->za_ring[(zap)->za_rput++ & RINGMASK] = ((u_char)(c)|(s)))
#define	RING_UNMARK(zap) \
	((zap)->za_ring[((zap)->za_rget) & RINGMASK] &= S_UNMARK)
#define	RING_ERR(zap, c) \
	((zap)->za_ring[((zap)->za_rget) & RINGMASK] & (c))


/*
 * These flags are shared with mcp_async.c and should be kept in sync.
 */
#define	ZAS_WOPEN	0x00000001	/* waiting for open to complete */
#define	ZAS_ISOPEN	0x00000002	/* open is complete */
#define	ZAS_OUT		0x00000004	/* line being used for dialout */
#define	ZAS_CARR_ON	0x00000008	/* carrier on last time we looked */
#define	ZAS_STOPPED	0x00000010	/* output is stopped */
#define	ZAS_DELAY	0x00000020	/* waiting for delay to finish */
#define	ZAS_BREAK	0x00000040	/* waiting for break to finish */
#define	ZAS_BUSY	0x00000080	/* waiting for transmission to finish */
#define	ZAS_DRAINING	0x00000100	/* waiting for output to drain */
					/* from chip */
#define	ZAS_SERVICEIMM	0x00000200	/* queue soft interrupt as soon as */
					/* receiver interrupt occurs */
#define	ZAS_SOFTC_ATTN	0x00000400	/* check soft carrier state in close */
#define	ZAS_PAUSED	0x00000800	/* MCP: dma interrupted and pending */
#define	ZAS_LNEXT	0x00001000	/* MCP: next input char is quoted */
#define	ZAS_XMIT_ACTIVE	0x00002000	/* MCP: Transmit dma running */
#define	ZAS_DMA_DONE	0x00004000	/* MCP: DMA done interrupt received */
#define	ZAS_ZSA_START	0x00010000	/* MCP: DMA done interrupt received */


/*
 * Asynchronous protocol private data structure for ZS and MCP/ALM2
 */
#define	ZSA_MAX_RSTANDBY	12
#define	ZSA_RDONE_MAX		60

struct asyncline {
	int		za_flags;	/* random flags */
	cond_t		*za_flags_cv;	/* condition variable for flags */
	dev_t		za_dev;		/* device major/minor numbers */
	mblk_t		*za_xmitblk;	/* transmit: active msg block */
	mblk_t		*za_rcvblk;	/* receive: active msg block */
	struct zscom	*za_common;	/* device common data */
	tty_common_t	za_ttycommon;	/* tty driver common data */
	int		za_wbufcid;	/* id of pending write-side bufcall */
	int		za_polltid;	/* softint poll timeout id */

	/*
	 * The following fields are protected by the zs_excl_hi lock.
	 * Some, such as za_flowc, are set only at the base level and
	 * cleared (without the lock) only by the interrupt level.
	 */
	u_char		*za_optr;	/* output pointer */
	int		za_ocnt;	/* output count */
	u_char		za_rput;	/* producing pointer for input */
	u_char		za_rget;	/* consuming pointer for input */
	u_char		za_flowc;	/* flow control char to send */
	u_char		za_rr0;		/* status latch for break detection */
	/*
	 * Each character stuffed into the ring has two bytes associated
	 * with it.  The first byte is used to indicate special conditions
	 * and the second byte is the actual data.  The ring buffer
	 * needs to be defined as u_short to accomodate this.
	 */
	u_short		za_ring[RINGSIZE];
	int 		za_kick_rcv_id;
	int 		za_kick_rcv_count;
	int		za_zsa_restart_id;
	int		za_bufcid;
	mblk_t		*za_rstandby[ZSA_MAX_RSTANDBY];
					/* receive: standby message blocks */
	mblk_t		*za_rdone[ZSA_RDONE_MAX];
					/* complete messages to be sent up */
	int		za_rdone_wptr;
	int		za_rdone_rptr;
	int		za_bad_count_int;
	u_int		za_rcv_flags_mask;
#ifdef ZSA_DEBUG
	int		za_wr;
	int		za_rd;
#endif
	volatile u_char za_soft_active;
	volatile u_char za_kick_active;
#define	DO_STOPC	(1<<8)
#define	DO_ESC		(1<<9)
#define	DO_SERVICEIMM	(1<<10)
#define	DO_TRANSMIT	(1<<11)
#define	DO_RETRANSMIT	(1<<12)
/*
 * ZS exclusive stuff.
 */
	short		za_break;	/* break count */
	union {
		struct {
			u_char  _hw;    /* overrun (hw) */
			u_char  _sw;    /* overrun (sw) */
		} _z;
		u_short uover_overrun;
	} za_uover;
#define	za_overrun	za_uover.uover_overrun
#define	za_hw_overrun	za_uover._z._hw
#define	za_sw_overrun	za_uover._z._sw
	short		za_ext;		/* modem status change count */
	short		za_work;	/* work to do flag */
	short		za_grace_flow_control;
	u_char		za_do_kick_rcv_in_softint;
	u_char		za_m_error;
/*
 * MCP exclusive stuff.
 * These should all be protected by a high priority lock.
 */
	u_char		*za_xoff;	/* xoff char in h/w XOFF buffer */
	u_char		za_lnext;	/* treat next char as literal */
	u_char		*za_devctl;	/* device control reg for this port */
	u_char		*za_dmabuf;	/* dma ram buffer for this port */
	int		za_breakoff;	/* SLAVIO */
	int		za_slav_break;	/* SLAVIO */
};

#endif /* _ASM */
#endif /* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* !_SYS_SER_ASYNC_H */
