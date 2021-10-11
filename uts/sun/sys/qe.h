/*
 * Copyright (c) 1992,1994 by Sun Microsystems, Inc.
 * All Rights Reserved
 */

#ifndef	_SYS_QE_H
#define	_SYS_QE_H

#pragma ident	"@(#)qe.h	1.13	95/04/05 SMI"

/*
 * Declarations and definitions specific to the
 * Quad Ethernet Device (QED) Driver.
 */

#include <sys/kstat.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Definitions for module_info.
 */
#define	QEIDNUM		(102)		/* module ID number */
#define	QENAME		"qe"		/* module name */
#define	QEMINPSZ	(0)		/* min packet size */
#define	QEMAXPSZ	ETHERMTU	/* max packet size */
#define	QEHIWAT		(32 * 1024)	/* hi-water mark */
#define	QELOWAT		(1)		/* lo-water mark */

/*
 * Per-Stream instance state information.
 *
 * Each instance is dynamically allocated at open() and free'd
 * at close().  Each per-Stream instance points to at most one
 * per-device structure using the sq_qep field.  All instances
 * are threaded together into one list of active instances
 * ordered on minor device number.
 */
struct	qestr {
	struct	qestr	*sq_nextp;	/* next in list */
	queue_t	*sq_rq;			/* pointer to our rq */
	struct	qe *sq_qep;		/* attached device */
	u_long	sq_state;		/* current DL state */
	u_long	sq_sap;			/* bound sap */
	u_long	sq_flags;		/* misc. flags */
	u_int	sq_mccount;		/* # enabled multicast addrs */
	struct	ether_addr *sq_mctab;	/* table of multicast addrs */
	u_long	sq_minor;		/* minor device number */
	kmutex_t	sq_lock;	/* protect this structure */
};

/* per-stream flags */
#define	QESFAST		0x01	/* "M_DATA fastpath" mode */
#define	QESRAW		0x02	/* M_DATA plain raw mode */
#define	QESALLPHYS	0x04	/* "promiscuous mode" */
#define	QESALLMULTI	0x08	/* enable all multicast addresses */
#define	QESALLSAP	0x10	/* enable all ether type values */

/*
 * Maximum # of multicast addresses per Stream.
 */
#define	QEMAXMC	64
#define	QEMCALLOC	(QEMAXMC * sizeof (struct ether_addr))

/*
 * Maximum number of receive descriptors posted to the chip.
 */
#define	QERPENDING	64
#define	QETPENDING	64

/*
 * Full DLSAP address length (in struct dladdr format).
 */
#define	QEADDRL	(sizeof (u_short) + ETHERADDRL)

/*
 * Return the address of an adjacent descriptor in the given ring.
 */
#define	NEXTRMD(qep, rmdp)	(((rmdp) + 1) == (qep)->qe_rmdlimp	\
	? (qep)->qe_rmdp : ((rmdp) + 1))
#define	NEXTTMD(qep, tmdp)	(((tmdp) + 1) == (qep)->qe_tmdlimp	\
	? (qep)->qe_tmdp : ((tmdp) + 1))
#define	PREVTMD(qep, tmdp)	((tmdp) == (qep)->qe_tmdp		\
	? ((qep)->qe_tmdlimp - 1) : ((tmdp) - 1))

/*
 * QED Per-Channel instance state information.
 *
 * Each instance is dynamically allocated on first attach.
 */
struct	qe {
	struct	qe		*qe_nextp;	/* next in a linked list */
	dev_info_t		*qe_dip;	/* associated dev_info */
	struct	ether_addr	qe_ouraddr;	/* individual address */
	int			qe_chan;	/* channel no */
	u_int			qe_flags;	/* misc. flags */
	u_int			qe_wantw;	/* xmit: out of resources */
	ddi_iblock_cookie_t	qe_cookie;	/* cookie from ddi_add_intr */

	volatile struct	qecm_chan	*qe_chanregp;	/* QEC chan regs */
	volatile struct	mace		*qe_maceregp;	/* MACE regs */

	kmutex_t	qe_xmitlock;		/* protect xmit-side fields */
	kmutex_t	qe_intrlock;		/* protect intr-side fields */
	kmutex_t	qe_buflock;		/* protect private buffers */

	struct	qmd	*qe_rmdp;	/* receive descriptor ring start */
	struct	qmd	*qe_rmdlimp;	/* receive descriptor ring end */
	struct	qmd	*qe_tmdp;	/* transmit descriptor ring start */
	struct	qmd	*qe_tmdlimp;	/* transmit descriptor ring end */
	volatile	struct	qmd	*qe_rnextp;	/* next chip rmd */
	volatile	struct	qmd	*qe_rlastp;	/* last free rmd */
	volatile	struct	qmd	*qe_tnextp;	/* next free tmd */
	volatile	struct	qmd	*qe_tcurp;	/* next tmd to reclm */

	mblk_t 	*qe_tmblkp[QEC_QMDMAX];		/* qebuf associated with TMD */
	mblk_t 	*qe_rmblkp[QEC_QMDMAX];		/* qebuf associated with RMD */

	queue_t	*qe_ipq;		/* ip read queue */

	/*
	 * DDI dma handle, kernel virtual base,
	 * and io virtual base of IOPB area.
	 */
	ddi_dma_handle_t	qe_iopbhandle;
	u_long			qe_iopbkbase;
	u_long			qe_iopbiobase;

	/*
	 * these are handles for the dvma resources reserved
	 * by dvma_reserve
	 */
	ddi_dma_handle_t	qe_dvmarh;	/* dvma recv handle */
	ddi_dma_handle_t	qe_dvmaxh;	/* dvma xmit handle */

	/*
	 * these are used if dvma reserve fails, and we have to fall
	 * back on the older ddi_dma_addr_setup routines
	 */
	ddi_dma_handle_t	*qe_dmarh;
	ddi_dma_handle_t	*qe_dmaxh;

	kstat_t	*qe_ksp;	/* kstat pointer */

	u_long	qe_ipackets;	/* # packets received */
	u_long	qe_ierrors;	/* # total input errors */
	u_long	qe_opackets;	/* # packets sent */
	u_long	qe_oerrors;	/* # total output errors */
	u_long	qe_txcoll;	/* # xmit collisions */
	u_long	qe_rxcoll;	/* # recv collisions */
	u_long	qe_defer;	/* # excessive defers */
	u_long	qe_fram;	/* # recv framing errors */
	u_long	qe_crc;		/* # recv crc errors */
	u_long	qe_buff;	/* # recv packet sizes > buffer size */
	u_long	qe_drop;	/* # recv packets dropped */
	u_long	qe_oflo;	/* # recv overflow */
	u_long	qe_uflo;	/* # xmit underflow */
	u_long	qe_missed;	/* # recv missed */
	u_long	qe_tlcol;	/* # xmit late collision */
	u_long	qe_trtry;	/* # xmit retry failures */
	u_long	qe_tnocar;	/* # loss of carrier errors */
	u_long	qe_inits;	/* # driver inits */
	u_long	qe_nocanput;	/* # canput() failures */
	u_long	qe_allocbfail;	/* # allocb() failures */
	u_long	qe_runt;	/* # recv runt packets */
	u_long	qe_jab;		/* # mace jabber errors */
	u_long	qe_babl;	/* # mace babble errors */
	u_long	qe_tmder;	/* # chained tx desc. errors */
	u_long	qe_laterr;	/* # sbus tx late error */
	u_long	qe_parerr;	/* # sbus tx parity errors */
	u_long	qe_errack;	/* # sbus tx error acks */
	u_long	qe_notmds;	/* # out of tmds */
	u_long	qe_notbufs;	/* # out of xmit buffers */
	u_long	qe_norbufs;	/* # out of recv buffers */
	u_long	qe_clsn;	/* # recv late collisions */

};

/* flags */
#define	QERUNNING	0x01	/* chip is initialized */
#define	QEPROMISC	0x02	/* promiscuous mode enabled */
#define	QESUN4C		0x04	/* this system is a sun4c */
#define	QEDMA		0x08	/* this is true when using the */
				/* the ddi_dma kind of interfaces */

struct	qekstat {
	struct kstat_named	qk_ipackets;	/* # packets received */
	struct kstat_named	qk_ierrors;	/* # total input errors */
	struct kstat_named	qk_opackets;	/* # packets sent */
	struct kstat_named	qk_oerrors;	/* # total output errors */
	struct kstat_named	qk_txcoll;	/* # xmit collisions */
	struct kstat_named	qk_rxcoll;	/* # recv collisions */
	struct kstat_named	qk_defer;	/* # defers */
	struct kstat_named	qk_fram;	/* # recv framing errors */
	struct kstat_named	qk_crc;		/* # recv crc errors */
	struct kstat_named	qk_drop;	/* # recv packets dropped */
	struct kstat_named	qk_buff;	/* # rx pkt sizes > buf size */
	struct kstat_named	qk_oflo;	/* # recv overflow */
	struct kstat_named	qk_uflo;	/* # xmit underflow */
	struct kstat_named	qk_missed;	/* # recv missed */
	struct kstat_named	qk_tlcol;	/* # xmit late collision */
	struct kstat_named	qk_trtry;	/* # xmit retry failures */
	struct kstat_named	qk_tnocar;	/* # loss of carrier errors */
	struct kstat_named	qk_inits;	/* # driver inits */
	struct kstat_named	qk_nocanput;	/* # canput() failures */
	struct kstat_named	qk_allocbfail;	/* # allocb() failures */
	struct kstat_named	qk_runt;	/* # recv runt packets */
	struct kstat_named	qk_jab;		/* # mace jabber errors */
	struct kstat_named	qk_babl;	/* # mace babble errors */
	struct kstat_named	qk_tmder;	/* # chained tx desc. errors */
	struct kstat_named	qk_laterr;	/* # sbus tx late error */
	struct kstat_named	qk_parerr;	/* # sbus tx parity errors */
	struct kstat_named	qk_errack;	/* # sbus tx error acks */
	struct kstat_named	qk_notmds;	/* # out of tmds */
	struct kstat_named	qk_notbufs;	/* # out of xmit buffers */
	struct kstat_named	qk_norbufs;	/* # out of recv buffers */
	struct kstat_named	qk_clsn;	/* # late collisions */
};

/*
 * Fast aligned copy requires both the source and destination
 * addresses have the same offset from some N-byte boundary.
 */
#define	QEBCOPYALIGN	(64)
#define	QEBCOPYMASK	(QEBCOPYALIGN-1)

#define	QEDRAINTIME	(400000)	/* # microseconds xmit drain */
#define	QELNKTIME	(500000)	/* time to mark link state up */

#define	QEROUNDUP(a, n)	(((a) + ((n) - 1)) & ~((n) - 1))
#define	QEROUNDUP2(a, n) (u_char *)((((u_int)(a)) + ((n) - 1)) & ~((n) - 1) + 2)

/*
 * Xmit/receive buffer structure.
 * This structure is organized to meet the following requirements:
 * - qb_buf starts on an QEBURSTSIZE boundary.
 * - qebuf is an even multiple of QEBURSTSIZE
 * - qb_buf[] is large enough to contain max frame (1518) plus
 *   QEBURSTSIZE for alignment adjustments
 */
#define	QEBURSTSIZE	(64)
#define	QEBURSTMASK	(QEBURSTSIZE - 1)
#define	QEBUFSIZE	(1728 - sizeof (struct qe *) - sizeof (frtn_t))

struct	qebuf {
	u_char	qb_buf[QEBUFSIZE];	/* raw buffer */
	struct	qe	*qb_qep;	/* link to device structure */
	frtn_t	qb_frtn;		/* for esballoc() */
};

/*
 * Define offset from start of qb_buf[] to point receive descriptor.
 * Requirements:
 * - must be 14 bytes back of a 4-byte boundary so the start of
 *   the network packet is 4-byte aligned.
 * - leave some headroom for others
 */
#define	QEHEADROOM	34

/*
 * Private DLPI full dlsap address format.
 */
struct	qedladdr {
	struct	ether_addr	dl_phys;
	u_short	dl_sap;
};

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_QE_H */
