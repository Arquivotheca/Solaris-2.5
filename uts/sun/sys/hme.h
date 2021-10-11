/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#ifndef	_SYS_HME_H
#define	_SYS_HME_H

#pragma ident	"@(#)hme.h	1.12	95/10/16 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#define	HME_IOC		0x80201ae1	/* random */
typedef struct {
	int		cmd;
	int		reserved[4];
} hme_ioc_hdr_t;

/* cmd */
#define	HME_IOC_GET_SPEED	0x100
#define	HME_IOC_SET_SPEED	0x110

/* mode */
#define	HME_AUTO_SPEED	0
#define	HME_FORCE_SPEED	1

/* speed */
#define	HME_SPEED_10		10
#define	HME_SPEED_100	100

typedef struct {
	hme_ioc_hdr_t	hdr;
	int		mode;
	int		speed;
} hme_ioc_cmd_t;

/* half-duplex or full-duplex mode */

#define	HME_HALF_DUPLEX	0
#define	HME_FULL_DUPLEX	1

#ifdef _KERNEL

/* Named Dispatch Parameter Management Structure */
typedef struct hmeparam_s {
	u_long hme_param_min;
	u_long hme_param_max;
	u_long hme_param_val;
	char   * hme_param_name;
} hmeparam_t;


static hmeparam_t	hme_param_arr[] = {
	/* min		max		value		name */
	{  0,		1,		1,		"transceiver_inuse"},
	{  0,		1,		1,		"link_status"},
	{  0,		1,		1,		"link_speed"},
	{  0,		1,		1,		"link_mode"},
	{  0,		255,		8,		"ipg1"},
	{  0,		255,		4,		"ipg2"},
	{  0,		1,		0,		"use_int_xcvr"},
	{  0,		255,		0,		"pace_size"},
	{  0,		1,		1,		"adv_autoneg_cap"},
	{  0,		1,		1,		"adv_100fdx_cap"},
	{  0,		1,		1,		"adv_100hdx_cap"},
	{  0,		1,		1,		"adv_10fdx_cap"},
	{  0,		1,		1,		"adv_10hdx_cap"},
	{  0,		1,		1,		"autoneg_cap"},
	{  0,		1,		1,		"100fdx_cap"},
	{  0,		1,		1,		"100hdx_cap"},
	{  0,		1,		1,		"10fdx_cap"},
	{  0,		1,		1,		"10hdx_cap"},
	{  0,		1,		0,		"lp_autoneg_cap"},
	{  0,		1,		0,		"lp_100fdx_cap"},
	{  0,		1,		0,		"lp_100hdx_cap"},
	{  0,		1,		0,		"lp_10fdx_cap"},
	{  0,		1,		0,		"lp_10hdx_cap"},
	{  0,		255,		0,		"instance"},
	{  0,		1,		1,		"lance_mode"},
	{  0,		255,		8,		"ipg0"},
};


#define	hme_param_transceiver	(hmep->hme_param_arr[0].hme_param_val)
#define	hme_param_linkup	(hmep->hme_param_arr[1].hme_param_val)
#define	hme_param_speed		(hmep->hme_param_arr[2].hme_param_val)
#define	hme_param_mode		(hmep->hme_param_arr[3].hme_param_val)
#define	hme_param_ipg1		(hmep->hme_param_arr[4].hme_param_val)
#define	hme_param_ipg2		(hmep->hme_param_arr[5].hme_param_val)
#define	hme_param_use_intphy	(hmep->hme_param_arr[6].hme_param_val)
#define	hme_param_pace_count	(hmep->hme_param_arr[7].hme_param_val)
#define	hme_param_autoneg	(hmep->hme_param_arr[8].hme_param_val)
#define	hme_param_anar_100fdx	(hmep->hme_param_arr[9].hme_param_val)
#define	hme_param_anar_100hdx	(hmep->hme_param_arr[10].hme_param_val)
#define	hme_param_anar_10fdx	(hmep->hme_param_arr[11].hme_param_val)
#define	hme_param_anar_10hdx	(hmep->hme_param_arr[12].hme_param_val)
#define	hme_param_bmsr_ancap	(hmep->hme_param_arr[13].hme_param_val)
#define	hme_param_bmsr_100fdx	(hmep->hme_param_arr[14].hme_param_val)
#define	hme_param_bmsr_100hdx	(hmep->hme_param_arr[15].hme_param_val)
#define	hme_param_bmsr_10fdx	(hmep->hme_param_arr[16].hme_param_val)
#define	hme_param_bmsr_10hdx	(hmep->hme_param_arr[17].hme_param_val)
#define	hme_param_aner_lpancap	(hmep->hme_param_arr[18].hme_param_val)
#define	hme_param_anlpar_100fdx	(hmep->hme_param_arr[19].hme_param_val)
#define	hme_param_anlpar_100hdx	(hmep->hme_param_arr[20].hme_param_val)
#define	hme_param_anlpar_10fdx	(hmep->hme_param_arr[21].hme_param_val)
#define	hme_param_anlpar_10hdx	(hmep->hme_param_arr[22].hme_param_val)
#define	hme_param_device	(hmep->hme_param_arr[23].hme_param_val)
#define	hme_param_lance_mode	(hmep->hme_param_arr[24].hme_param_val)
#define	hme_param_ipg0		(hmep->hme_param_arr[25].hme_param_val)

#define	HME_PARAM_CNT	26


/* command */

#define	HME_ND_GET	ND_GET
#define	HME_ND_SET	ND_SET

/* default IPG settings */
#define	IPG1	8
#define	IPG2	4


extern	int	msgsize();
extern	void	usec_delay();
extern	void	merror(), miocack(), miocnak(), mcopymsg();
extern	void	dlerrorack(), dlokack(), dlbindack(), dlphysaddrack();
extern 	void	dluderrorind();
extern	mblk_t	*desballoc(unsigned char *, int, int, frtn_t *);

/*
 * Declarations and definitions specific to the
 * FEPS 10/100 Mbps Ethernet (hme) device.
 */

/*
 * Definitions for module_info.
 */
#define		HMEIDNUM	(109)		/* module ID number */
#define		HMENAME		"hme"		/* module name */
#define		HMEMINPSZ	(0)		/* min packet size */
#define		HMEMAXPSZ	ETHERMTU	/* max packet size */
#define		HMEHIWAT	(128 * 1024)	/* hi-water mark */
#define		HMELOWAT	(1)		/* lo-water mark */

/*
 * Per-Stream instance state information.
 *
 * Each instance is dynamically allocated at open() and free'd
 * at close().  Each per-Stream instance points to at most one
 * per-device structure using the sb_hmep field.  All instances
 * are threaded together into one list of active instances
 * ordered on minor device number.
 */
struct	hmestr {
	struct	hmestr	*sb_nextp;	/* next in list */
	queue_t	*sb_rq;			/* pointer to our read queue */
	struct	hme *sb_hmep;		/* attached device */
	u_long	sb_state;		/* current DL state */
	u_long	sb_sap;			/* bound sap */
	u_long	sb_flags;		/* misc. flags */
	u_int	sb_mccount;		/* # enabled multicast addrs */
	struct	ether_addr *sb_mctab;	/* table of multicast addrs */
	u_long	sb_minor;		/* minor device number */
	kmutex_t	sb_lock;	/* protect this structure */
};

/* per-stream flags */
#define	HMESFAST	0x01	/* "M_DATA fastpath" mode */
#define	HMESRAW		0x02	/* M_DATA plain raw mode */
#define	HMESALLPHYS	0x04	/* "promiscuous mode" */
#define	HMESALLMULTI	0x08	/* enable all multicast addresses */
#define	HMESALLSAP	0x10	/* enable all ether type values */

/*
 * Maximum # of multicast addresses per Stream.
 */
#define	HMEMAXMC	64
#define	HMEMCALLOC	(HMEMAXMC * sizeof (struct ether_addr))

/*
 * Maximum number of receive descriptors posted to the chip.
 */
#define	HMERPENDING	64

/*
 * Maximum number of transmit descriptors for lazy reclaim.
 */
#define	HMETPENDING	64

/*
 * Full DLSAP address length (in struct dladdr format).
 */
#define	HMEADDRL	(sizeof (u_short) + ETHERADDRL)

/*
 * Return the address of an adjacent descriptor in the given ring.
 */
#define	NEXTRMD(hmep, rmdp)	(((rmdp) + 1) == (hmep)->hme_rmdlimp	\
	? (hmep)->hme_rmdp : ((rmdp) + 1))
#define	NEXTTMD(hmep, tmdp)	(((tmdp) + 1) == (hmep)->hme_tmdlimp	\
	? (hmep)->hme_tmdp : ((tmdp) + 1))
#define	PREVTMD(hmep, tmdp)	((tmdp) == (hmep)->hme_tmdp		\
	? ((hmep)->hme_tmdlimp - 1) : ((tmdp) - 1))

#define	MSECOND(t)	t
#define	SECOND(t)	t*1000
#define	HME_TICKS	MSECOND(100)

#define	HME_LINKCHECK_TIMER	SECOND(30)

#define	HME_2P0_REVID		0xa0
#define	HME_2P1_REVID		0x20
#define	HME_2P1_REVID_OBP		0x21

#define	HME_NTRIES_LOW		(SECOND(5)/HME_TICKS)	/* 5 Seconds */
#define	HME_NTRIES_HIGH		(SECOND(5)/HME_TICKS)	/* 5 Seconds */
#define	HME_NTRIES_LOW_10	(SECOND(2)/HME_TICKS)	/* 2 Seconds */
#define	HME_LINKDOWN_TIME	(SECOND(2)/HME_TICKS)	/* 2 Seconds */

#define	HME_LINKDOWN_OK		0
#define	HME_FORCE_LINKDOWN	1
#define	HME_LINKDOWN_STARTED	2
#define	HME_LINKDOWN_DONE	3

#define	P1_0    0x100

#define	HME_EXTERNAL_TRANSCEIVER	0
#define	HME_INTERNAL_TRANSCEIVER	1
#define	HME_NO_TRANSCEIVER		2

#define	HME_HWAN_TRY		0 /* Try Hardware autonegotiation */
#define	HME_HWAN_INPROGRESS	1 /* Hardware autonegotiation in progress */
#define	HME_HWAN_SUCCESFUL	2 /* Hardware autonegotiation succesful */
#define	HME_HWAN_FAILED		3 /* Hardware autonegotiation failed */

/*
 * HME Device Channel instance state information.
 *
 * Each instance is dynamically allocated on first attach.
 */
struct	hme {
	struct	hme		*hme_nextp;	/* next in a linked list */
	dev_info_t		*hme_dip;	/* associated dev_info */
	int			hme_asicrev;
	int			hme_mifpoll_enable;
	int			hme_frame_enable;
	int			hme_lance_mode_enable;
	int			hme_rxcv_enable;

	int			hme_burstsizes; /* binary encoded val */
	int			hme_64bit_xfer;	/* 64-bit Sbus xfers */
	int			hme_phyad;
	int			hme_autoneg;

	caddr_t			hme_g_nd;	/* head of the */
						/* named dispatch table */
	hmeparam_t		hme_param_arr[HME_PARAM_CNT];
	int			hme_transceiver;  /* current PHY in use */
	u_short			hme_bmcr;	/* PHY control register */
	u_short			hme_bmsr;	/* PHY status register */
	u_short			hme_idr1;	/* PHY IDR1 register */
	u_short			hme_idr2;	/* PHY IDR2 register */
	u_short			hme_anar;	/* PHY ANAR register */
	u_short			hme_anlpar;	/* PHY ANLPAR register */
	u_short			hme_aner;	/* PHY ANER register */
	int			hme_mode;	/* auto/forced mode */
	int			hme_linkup;	/* link status */
	int			hme_forcespeed; /* speed in forced mode */
	int			hme_tryspeed;	/* speed in auto mode */
	int			hme_fdx;	/* full-duplex mode */
	int			hme_pace_count;	/* pacing pkt count */

	int			hme_macfdx;
	int			hme_linkcheck;
	int			hme_linkup_msg;
	int			hme_force_linkdown;
	int			hme_nlasttries;
	int			hme_ntries;
	int			hme_delay;
	int			hme_linkup_10;
	int			hme_linkup_cnt;
	int			hme_timerid;

	int			hme_polling_on;
	int			hme_mifpoll_data;
	int			hme_mifpoll_flag;

	struct	ether_addr	hme_ouraddr;	/* individual address */
	u_int			hme_flags;	/* misc. flags */
	u_int			hme_wantw;	/* xmit: out of resources */

	volatile struct	hme_global	*hme_globregp;	/* HME global regs */
	volatile struct	hme_etx		*hme_etxregp;	/* HME ETX regs */
	volatile struct	hme_erx		*hme_erxregp;	/* HME ERX regs */
	volatile struct	hme_bmac	*hme_bmacregp;	/* BigMAC registers */
	volatile struct	hme_mif		*hme_mifregp;	/* HME transceiver */

	kmutex_t	hme_xmitlock;		/* protect xmit-side fields */
	kmutex_t	hme_intrlock;		/* protect intr-side fields */
	kmutex_t	hme_linklock;		/* protect link-side fields */
	ddi_iblock_cookie_t	hme_cookie;	/* interrupt cookie */

	struct	hme_rmd	*hme_rmdp;	/* receive descriptor ring start */
	struct	hme_rmd	*hme_rmdlimp;	/* receive descriptor ring end */
	struct	hme_tmd	*hme_tmdp;	/* transmit descriptor ring start */
	struct	hme_tmd	*hme_tmdlimp;	/* transmit descriptor ring end */
	volatile struct	hme_rmd	*hme_rnextp;	/* next chip rmd */
	volatile struct	hme_rmd	*hme_rlastp;	/* last free rmd */
	volatile struct	hme_tmd	*hme_tnextp;	/* next free tmd */
	volatile struct	hme_tmd	*hme_tcurp;	/* next tmd to reclaim (used) */

	mblk_t	*hme_tmblkp[HME_TMDMAX];	/* hmebuf associated with TMD */
	mblk_t	*hme_rmblkp[HME_RMDMAX];	/* hmebuf associated with RMD */

	queue_t	*hme_ipq;		/* ip read queue */

	/*
	 * DDI dma handle, kernel virtual base,
	 * and io virtual base of IOPB area.
	 */
	ddi_dma_handle_t	hme_iopbhandle;
	u_long			hme_iopbkbase;
	u_long			hme_iopbiobase;

	/*
	 * these are handles for the dvma resources reserved
	 * by dvma_reserve
	 */
	ddi_dma_handle_t	hme_dvmarh;	/* dvma recv handle */
	ddi_dma_handle_t	hme_dvmaxh;	/* dvma xmit handle */

	/*
	 * these are used if dvma reserve fails, and we have to fall
	 * back on the older ddi_dma_addr_setup routines
	 */
	ddi_dma_handle_t	*hme_dmarh;
	ddi_dma_handle_t	*hme_dmaxh;

	kstat_t	*hme_ksp;	/* kstat pointer */
	u_long	hme_ipackets;
	u_long	hme_ierrors;
	u_long	hme_opackets;
	u_long	hme_oerrors;
	u_long	hme_coll;
	u_long	hme_defer;
	u_long	hme_fram;
	u_long	hme_crc;
	u_long	hme_sqerr;
	u_long	hme_cvc;
	u_long	hme_lenerr;
	u_long	hme_drop;
	u_long	hme_buff;
	u_long	hme_oflo;
	u_long	hme_uflo;
	u_long	hme_missed;
	u_long	hme_tlcol;
	u_long	hme_trtry;
	u_long	hme_fstcol;
	u_long	hme_tnocar;
	u_long	hme_inits;
	u_long	hme_nocanput;
	u_long	hme_allocbfail;
	u_long	hme_runt;
	u_long	hme_jab;
	u_long	hme_babl;
	u_long	hme_tmder;
	u_long	hme_txlaterr;
	u_long	hme_rxlaterr;
	u_long	hme_slvparerr;
	u_long	hme_txparerr;
	u_long	hme_rxparerr;
	u_long	hme_slverrack;
	u_long	hme_txerrack;
	u_long	hme_rxerrack;
	u_long	hme_txtagerr;
	u_long	hme_rxtagerr;
	u_long	hme_eoperr;
	u_long	hme_notmds;
	u_long	hme_notbufs;
	u_long	hme_norbufs;
	u_long	hme_clsn;
};

/* flags */
#define	HMERUNNING	0x01	/* chip is initialized */
#define	HMEPROMISC	0x02	/* promiscuous mode enabled */
#define	HMESUN4C	0x04	/* this system is a sun4c */
#define	HMESUSPENDED	0x08	/* suspended interface */


struct	hmekstat {
	struct kstat_named	hk_ipackets;	/* packets received */
	struct kstat_named	hk_ierrors;	/* input errors */
	struct kstat_named	hk_opackets;	/* packets transmitted */
	struct kstat_named	hk_oerrors;	/* output errors */
	struct kstat_named	hk_coll;	/* collisions encountered */
	struct kstat_named	hk_defer;	/* slots deferred */
	struct kstat_named	hk_fram;	/* framing errors */
	struct kstat_named	hk_crc;		/* crc errors */
	struct kstat_named	hk_sqerr;	/* SQE test  errors */
	struct kstat_named	hk_cvc;		/* code violation  errors */
	struct kstat_named	hk_lenerr;	/* rx len errors */
	struct kstat_named	hk_drop;	/* missed/drop errors */
	struct kstat_named	hk_buff;	/* buff errors */
	struct kstat_named	hk_oflo;	/* overflow errors */
	struct kstat_named	hk_uflo;	/* underflow errors */
	struct kstat_named	hk_missed;	/* missed/dropped packets */
	struct kstat_named	hk_tlcol;	/* late collisions */
	struct kstat_named	hk_trtry;	/* retry errors */
	struct kstat_named	hk_fstcol;	/* first collisions */
	struct kstat_named	hk_tnocar;	/* no carrier */
	struct kstat_named	hk_inits;	/* initialization */
	struct kstat_named	hk_nocanput;	/* nocanput errors */
	struct kstat_named	hk_allocbfail;	/* allocb failures */
	struct kstat_named	hk_runt;	/* runt errors */
	struct kstat_named	hk_jab;		/* jabber errors */
	struct kstat_named	hk_babl;	/* runt errors */
	struct kstat_named	hk_tmder;	/* tmd errors */
	struct kstat_named	hk_txlaterr;	/* tx late errors */
	struct kstat_named	hk_rxlaterr;	/* rx late errors */
	struct kstat_named	hk_slvparerr;	/* slave parity errors */
	struct kstat_named	hk_txparerr;	/* tx parity errors */
	struct kstat_named	hk_rxparerr;	/* rx parity errors */
	struct kstat_named	hk_slverrack;	/* slave error acks */
	struct kstat_named	hk_txerrack;	/* tx error acks */
	struct kstat_named	hk_rxerrack;	/* rx error acks */
	struct kstat_named	hk_txtagerr;	/* tx tag error */
	struct kstat_named	hk_rxtagerr;	/* rx tag error */
	struct kstat_named	hk_eoperr;	/* eop error */
	struct kstat_named	hk_notmds;	/* tmd errors */
	struct kstat_named	hk_notbufs;	/* tx buf errors */
	struct kstat_named	hk_norbufs;	/* rx buf errors */
	struct kstat_named	hk_clsn;	/* clsn errors */
};

#define	HMEDRAINTIME	(400000)	/* # microseconds xmit drain */

#define	ROUNDUP(a, n)	(((a) + ((n) - 1)) & ~((n) - 1))
#define	ROUNDUP2(a, n)	(u_char *)((((u_int)(a)) + ((n) - 1)) & ~((n) - 1))

/*
 * Xmit/receive buffer structure.
 * This structure is organized to meet the following requirements:
 * - bb_buf starts on an HMEBURSTSIZE boundary.
 * - hmebuf is an even multiple of HMEBURSTSIZE
 * - bb_buf[] is large enough to contain max frame (1518) plus
 *   (3 x HMEBURSTSIZE) rounded up to the next HMEBURSTSIZE
 * XXX What about another 128 bytes (HMEC requirement).
 * Fast aligned copy requires both the source and destination
 * addresses have the same offset from some N-byte boundary.
 */
#define		HMEBURSTSIZE	(64)
#define		HMEBURSTMASK	(HMEBURSTSIZE - 1)
#define		HMEBUFSIZE	(1728)

#ifdef	notdef
#define		HMEBUFSIZE	(1728 - sizeof (struct hme *) - sizeof (frtn_t))
#define		HMEBUFPAD	(HMEBURSTSIZE - sizeof (struct hme *) \
				- sizeof (frtn_t))
struct	hmebuf {
	u_char	bb_buf[HMEBUFSIZE];	/* raw buffer */
	struct	hme	*bb_hmep;	/* link to device structure */
	frtn_t	bb_frtn;		/* for esballoc() */
	u_char	pad[HMEBUFPAD];
};
#endif	/* notdef */

/*
 * Define offset from start of bb_buf[] to point receive descriptor.
 * Requirements:
 * - must be 14 bytes back of a 4-byte boundary so the start of
 *   the network packet is 4-byte aligned.
 * - leave some headroom for others
 */
#define		HMEHEADROOM	(34)

/* Offset for the first byte in the receive buffer */
#define	HME_FSTBYTE_OFFSET	2

/*
 * Private DLPI full dlsap address format.
 */
struct	hmedladdr {
	struct	ether_addr	dl_phys;
	u_short	dl_sap;
};
#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_HME_H */
