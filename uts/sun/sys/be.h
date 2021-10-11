/*
 * Copyright (c) 1992,1994 by Sun Microsystems, Inc.
 * All Rights Reserved
 */

#ifndef	_SYS_BE_H
#define	_SYS_BE_H

#pragma ident	"@(#)be.h	1.14	95/05/05 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#define	BE_IOC		0x80201ae1	/* random */
typedef struct {
	int		cmd;
	int		reserved[4];
} be_ioc_hdr_t;

/* cmd */
#define	BE_IOC_GET_SPEED	0x100
#define	BE_IOC_SET_SPEED	0x110

/* mode */
#define	BE_AUTO_SPEED	0
#define	BE_FORCE_SPEED	1

/* speed */
#define	BE_SPEED_10		10
#define	BE_SPEED_100	100

typedef struct {
	be_ioc_hdr_t	hdr;
	int				mode;
	int				speed;
} be_ioc_cmd_t;

#ifdef _KERNEL
extern	int	msgsize();
extern	int	usec_delay();
extern	void	merror(), miocack(), miocnak(), mcopymsg();
extern	void	dlerrorack(), dlokack(), dlbindack(), dlphysaddrack();
extern 	void	dluderrorind();
extern	mblk_t	*desballoc(unsigned char *, int, int, frtn_t *);

/*
 * Declarations and definitions specific to the
 * BigMAC Ethernet Device (BED) Driver.
 */

/*
 * Definitions for module_info.
 */
#define		BEIDNUM		(103)		/* module ID number */
#define		BENAME		"be"		/* module name */
#define		BEMINPSZ	(0)		/* min packet size */
#define		BEMAXPSZ	ETHERMTU	/* max packet size */
#define		BEHIWAT		(128 * 1024)	/* hi-water mark */
#define		BELOWAT		(1)		/* lo-water mark */

/*
 * Per-Stream instance state information.
 *
 * Each instance is dynamically allocated at open() and free'd
 * at close().  Each per-Stream instance points to at most one
 * per-device structure using the sb_bep field.  All instances
 * are threaded together into one list of active instances
 * ordered on minor device number.
 */
struct	bestr {
	struct	bestr	*sb_nextp;	/* next in list */
	queue_t	*sb_rq;			/* pointer to our read queue */
	struct	be *sb_bep;		/* attached device */
	u_long	sb_state;		/* current DL state */
	u_long	sb_sap;			/* bound sap */
	u_long	sb_flags;		/* misc. flags */
	u_int	sb_mccount;		/* # enabled multicast addrs */
	struct	ether_addr *sb_mctab;	/* table of multicast addrs */
	u_long	sb_minor;		/* minor device number */
	kmutex_t	sb_lock;	/* protect this structure */
};

/* per-stream flags */
#define	BESFAST		0x01	/* "M_DATA fastpath" mode */
#define	BESRAW		0x02	/* M_DATA plain raw mode */
#define	BESALLPHYS	0x04	/* "promiscuous mode" */
#define	BESALLMULTI	0x08	/* enable all multicast addresses */
#define	BESALLSAP	0x10	/* enable all ether type values */

/*
 * Maximum # of multicast addresses per Stream.
 */
#define	BEMAXMC	64
#define	BEMCALLOC	(BEMAXMC * sizeof (struct ether_addr))

/*
 * Maximum number of receive descriptors posted to the chip.
 */
#define	BERPENDING	64

#define	BETPENDING	64

/*
 * Full DLSAP address length (in struct dladdr format).
 */
#define	BEADDRL	(sizeof (u_short) + ETHERADDRL)

/*
 * Return the address of an adjacent descriptor in the given ring.
 */
#define	NEXTRMD(bep, rmdp)	(((rmdp) + 1) == (bep)->be_rmdlimp	\
	? (bep)->be_rmdp : ((rmdp) + 1))
#define	NEXTTMD(bep, tmdp)	(((tmdp) + 1) == (bep)->be_tmdlimp	\
	? (bep)->be_tmdp : ((tmdp) + 1))
#define	PREVTMD(bep, tmdp)	((tmdp) == (bep)->be_tmdp		\
	? ((bep)->be_tmdlimp - 1) : ((tmdp) - 1))

/* Board Revision */
#define	P1_0	0x100
#define	P1_5	0x150

#define	MSECOND(t)	t
#define	SECOND(t)	t*1000
#define	BE_TICKS	MSECOND(100)

#define	BE_LINKCHECK_TIMER	SECOND(30)

#define	BE_NTRIES_LOW		(SECOND(2)/BE_TICKS)	/* 2 Seconds */
#define	BE_NTRIES_HIGH		(SECOND(64)/BE_TICKS)	/* 64 Seconds */

#define	BE_EXTERNAL_TRANSCEIVER	0
#define	BE_INTERNAL_TRANSCEIVER	1

/*
 * BED Channel instance state information.
 *
 * Each instance is dynamically allocated on first attach.
 */
struct	be {
	struct	be		*be_nextp;	/* next in a linked list */
	dev_info_t		*be_dip;	/* associated dev_info */

	int				be_boardrev;
	int				be_transceiver;
	int				be_linkcheck;
	int				be_mode;
	int				be_forcespeed;
	int				be_tryspeed;
	int				be_nlasttries;
	int				be_ntries;
	int				be_delay;
	int				be_linkup_10;
	int				be_linkup;
	int				be_timerid;
	int				be_intr_flag; /* bug 1204247 */

	struct	ether_addr	be_ouraddr;	/* individual address */
	int			be_chan;	/* channel no */
	u_int			be_flags;	/* misc. flags */
	u_int			be_wantw;	/* xmit: out of resources */

	volatile struct	qecb_chan	*be_chanregp;	/* QEC channel regs */
	volatile struct	bmac		*be_bmacregp;	/* BigMAC registers */
#ifndef	MPSAS
	volatile struct	bmactcvr	*be_tcvrregp;	/* BigMAC transceiver */
#endif	/* MPSAS */
	volatile struct	qec_global	*be_globregp;	/* QEC global regs */

	kmutex_t	be_xmitlock;		/* protect xmit-side fields */
	kmutex_t	be_intrlock;		/* protect intr-side fields */
	ddi_iblock_cookie_t	be_cookie;	/* interrupt cookie */

	struct	qmd	*be_rmdp;	/* receive descriptor ring start */
	struct	qmd	*be_rmdlimp;	/* receive descriptor ring end */
	struct	qmd	*be_tmdp;	/* transmit descriptor ring start */
	struct	qmd	*be_tmdlimp;	/* transmit descriptor ring end */
	volatile struct	qmd	*be_rnextp;	/* next chip rmd */
	volatile struct	qmd	*be_rlastp;	/* last free rmd */
	volatile struct	qmd	*be_tnextp;	/* next free tmd */
	volatile struct	qmd	*be_tcurp;	/* next tmd to reclaim (used) */

	mblk_t	*be_tmblkp[QEC_QMDMAX];		/* bebuf associated with TMD */
	mblk_t	*be_rmblkp[QEC_QMDMAX];		/* bebuf associated with RMD */

	queue_t	*be_ipq;		/* ip read queue */

	/*
	 * DDI dma handle, kernel virtual base,
	 * and io virtual base of IOPB area.
	 */
	ddi_dma_handle_t	be_iopbhandle;
	u_long			be_iopbkbase;
	u_long			be_iopbiobase;

	/*
	 * these are handles for the dvma resources reserved
	 * by dvma_reserve
	 */
	ddi_dma_handle_t	be_dvmarh;	/* dvma recv handle */
	ddi_dma_handle_t	be_dvmaxh;	/* dvma xmit handle */

	/*
	 * these are used if dvma reserve fails, and we have to fall
	 * back on the older ddi_dma_addr_setup routines
	 */
	ddi_dma_handle_t	*be_dmarh;
	ddi_dma_handle_t	*be_dmaxh;

#ifndef	MPSAS
	kstat_t	*be_ksp;	/* kstat pointer */
#endif	/* MPSAS */
	u_long	be_ipackets;
	u_long	be_ierrors;
	u_long	be_opackets;
	u_long	be_oerrors;
	u_long	be_coll;
	u_long	be_defer;
	u_long	be_fram;
	u_long	be_crc;
	u_long	be_drop;
	u_long	be_buff;
	u_long	be_oflo;
	u_long	be_uflo;
	u_long	be_missed;
	u_long	be_tlcol;
	u_long	be_trtry;
	u_long	be_tnocar;
	u_long	be_inits;
	u_long	be_nocanput;
	u_long	be_allocbfail;
	u_long	be_runt;
	u_long	be_jab;
	u_long	be_babl;
	u_long	be_tmder;
	u_long	be_laterr;
	u_long	be_parerr;
	u_long	be_errack;
	u_long	be_notmds;
	u_long	be_notbufs;
	u_long	be_norbufs;
	u_long	be_clsn;
};

/* flags */
#define	BERUNNING	0x01	/* chip is initialized */
#define	BEPROMISC	0x02	/* promiscuous mode enabled */
#define	BESUN4C		0x04	/* this system is a sun4c */

struct	bekstat {
	struct kstat_named	bk_ipackets;	/* packets received */
	struct kstat_named	bk_ierrors;	/* input errors */
	struct kstat_named	bk_opackets;	/* packets transmitted */
	struct kstat_named	bk_oerrors;	/* output errors */
	struct kstat_named	bk_coll;	/* collisions encountered */
	struct kstat_named	bk_defer;	/* slots deferred */
	struct kstat_named	bk_fram;	/* framing errors */
	struct kstat_named	bk_crc;		/* crc errors */
	struct kstat_named	bk_drop;	/* missed/drop errors */
	struct kstat_named	bk_buff;	/* buff errors */
	struct kstat_named	bk_oflo;	/* overflow errors */
	struct kstat_named	bk_uflo;	/* underflow errors */
	struct kstat_named	bk_missed;	/* missed/dropped packets */
	struct kstat_named	bk_tlcol;	/* late collisions */
	struct kstat_named	bk_trtry;	/* retry errors */
	struct kstat_named	bk_tnocar;	/* no carrier */
	struct kstat_named	bk_inits;	/* initialization */
	struct kstat_named	bk_nocanput;	/* nocanput errors */
	struct kstat_named	bk_allocbfail;	/* allocb failures */
	struct kstat_named	bk_runt;	/* runt errors */
	struct kstat_named	bk_jab;		/* jabber errors */
	struct kstat_named	bk_babl;	/* runt errors */
	struct kstat_named	bk_tmder;	/* tmd errors */
	struct kstat_named	bk_laterr;	/* late errors */
	struct kstat_named	bk_parerr;	/* parity errors */
	struct kstat_named	bk_errack;	/* error acks */
	struct kstat_named	bk_notmds;	/* tmd errors */
	struct kstat_named	bk_notbufs;	/* tx buf errors */
	struct kstat_named	bk_norbufs;	/* rx buf errors */
	struct kstat_named	bk_clsn;	/* clsn errors */
};

#define	QEDRAINTIME	(400000)	/* # microseconds xmit drain */

#define	BEROUNDUP(a, n)	(((a) + ((n) - 1)) & ~((n) - 1))
#define	BEROUNDUP2(a, n) (u_char *)((((u_int)(a)) + ((n) - 1)) & ~((n) - 1) + 2)

/*
 * Xmit/receive buffer structure.
 * This structure is organized to meet the following requirements:
 * - bb_buf starts on an QEBURSTSIZE boundary.
 * - bebuf is an even multiple of QEBURSTSIZE
 * - bb_buf[] is large enough to contain max frame (1518) plus
 *   (3 x QEBURSTSIZE) rounded up to the next QEBURSTSIZE
 * XXX What about another 128 bytes (QEC requirement).
 * Fast aligned copy requires both the source and destination
 * addresses have the same offset from some N-byte boundary.
 */
#define		QEBURSTSIZE	(64)
#define		QEBURSTMASK	(QEBURSTSIZE - 1)
#ifdef	notdef
#define		BEBUFSIZE	(1728)
#endif	/* notdef */
#define		BEBUFSIZE	(1728 - sizeof (struct be *) - sizeof (frtn_t))
#define		BEBUFPAD	(QEBURSTSIZE - sizeof (struct be *) \
				- sizeof (frtn_t))
struct	bebuf {
	u_char	bb_buf[BEBUFSIZE];	/* raw buffer */
	struct	be	*bb_bep;	/* link to device structure */
	frtn_t	bb_frtn;		/* for esballoc() */
	u_char	pad[BEBUFPAD];
};

/*
 * Define offset from start of bb_buf[] to point receive descriptor.
 * Requirements:
 * - must be 14 bytes back of a 4-byte boundary so the start of
 *   the network packet is 4-byte aligned.
 * - leave some headroom for others
 */
#define		QEHEADROOM	(34)

/*
 * Private DLPI full dlsap address format.
 */
struct	qedladdr {
	struct	ether_addr	dl_phys;
	u_short	dl_sap;
};
#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_BE_H */
