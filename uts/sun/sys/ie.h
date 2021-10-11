/*
 * Copyright (c) 1992,1994 by Sun Microsystems, Inc.
 * All Rights Reserved
 */

#ifndef	_SYS_IE_H
#define	_SYS_IE_H

#pragma ident	"@(#)ie.h	1.14	94/12/23 SMI"

/*
 * ie.h header for STREAMS Intel 82586 Ethernet Driver.
 */

#include <sys/kstat.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Definitions for module_info.
 */
#define	IEIDNUM		(69)		/* module ID number */
#define	IENAME		"ie"		/* module name */
#define	IEMINPSZ	(0)		/* min packet size */
#define	IEMAXPSZ	ETHERMTU	/* max packet size */
#define	IEHIWAT		(32768)		/* hi-water mark */
#define	IELOWAT		(1)		/* lo-water mark */

/*
 * Per-Stream instance state information.
 *
 * Each instance is dynamically allocated at open() and free'd
 * at close().  Each per-Stream instance points to at most one
 * per-device structure using the is_iep field.  All instances
 * are threaded together into one list of active instances
 * ordered on minor device number.
 */
struct	iestr {
	struct	iestr	*is_nextp;	/* next in list */
	queue_t	*is_rq;			/* pointer to our rq */
	u_long	is_minor;		/* minor device number */
	u_long	is_state;		/* current DL state */
	u_long	is_sap;			/* bound sap */
	u_long	is_flags;		/* misc. flags */
	struct	ie *is_iep;		/* attached device */
	u_int	is_mccount;		/* # of enabled multicast addrs */
	struct	ether_addr *is_mctab;	/* table of multicast addrs */
	kmutex_t	is_lock;	/* protect this structure */
};

/* per-stream flags */
#define	ISFAST		0x01	/* "M_DATA fastpath" mode */
#define	ISRAW		0x02	/* M_DATA plain raw mode */
#define	ISALLPHYS	0x04	/* we enabled promiscuous mode */
#define	ISALLMULTI	0x08	/* enable all multicasts addresses */
#define	ISALLSAP	0x10	/* enable all ether type values */

/*
 * Maximum valid sap (ethernet type) value.
 */

/*
 * Maximum # of multicast addresses per Stream.
 */
#define	IEMAXMC	64
#define	IEMCALLOC	(IEMAXMC * sizeof (struct ether_addr))

/*
 * Full DLSAP address length (in struct dladdr format).
 */
#define	IEADDRL	(sizeof (u_short) + ETHERADDRL)

/*
 * Per-Device instance state information.
 *
 * Each instance is dynamically allocated on first attach.
 */
struct	ie {
	struct	ie		*ie_nextp;	/* next in a linked list */
	dev_info_t		*ie_dip;	/* associated dev_info */
	struct	ether_addr	ie_ouraddr;	/* individual address */
	u_int			ie_flags;	/* misc. flags */
	u_int			ie_wantw;	/* xmit: out of resources */
	u_int			ie_jamtime;	/* last jammed time in secs */
	int			ie_dogid;	/* id returned from timeout() */
	struct iecb		*ie_cbsyncp;	/* synchronous command block */

	caddr_t		ie_cb_base;	/* control block base address */
	u_int		ie_cb_size;	/* control block size in bytes */

	kmutex_t	ie_buflock;	/* protect per-device buffer pool */
	kmutex_t	ie_devlock;	/* protect other per-device data */

	volatile	struct iescp	*ie_scp;	/* SCP pointer */
	volatile	struct ieiscp	*ie_iscp;	/* ISCP pointer */
	volatile	struct iescb	*ie_scbp;	/* SCB pointer */
	volatile	caddr_t	ie_csr;		/* control & status register */
	volatile	caddr_t	ie_iocache;	/* IC cache xmit/recv lines */
	volatile	caddr_t	ie_ram;		/* RAM on 3E or IORAM on */
						/* Sun4/4XX */

	/* Receive Frame Descriptors */
	struct ierfd	*ie_rfdring;	/* rfd ring start */
	struct ierfd	*ie_rfdlim;	/* one past end of ring */
	volatile struct ierfd	*ie_rfdhd;	/* rfd pointed to by scb */
	volatile struct ierfd	*ie_rfdtl;	/* last rfd (for reclaiming) */

	/* Receive Buffer Descriptors */
	struct ierbd	*ie_rbdring;	/* rbd ring start */
	struct ierbd	*ie_rbdlim;	/* one past end of ring */
	struct ierbd	*ie_rbdhd;	/* first rbd pointed to by first rfd */
	struct ierbd	*ie_rbdtl;	/* last rbd (for reclaim purpose) */

	/* Transmit and Receive Buffers */
	struct  iebuf	*ie_bufbase;	/* buffers base address */
	struct	iebuf	**ie_buftab;	/* buffer pointer stack (filo) */
	u_int	ie_bufi;		/* index of top of the above stack */
	struct  iebuf	**ie_rbp;	/* iebuf associated with rbd */
	struct  iebuf	**ie_tbp;	/* iebuf associated with tbd */
	mblk_t  **ie_tmblk;		/* streams msg associated with tbd */

	/* Transmit Frame Descriptors */
	struct ietcb	*ie_tcbring;	/* tcb ring start */
	struct ietcb	*ie_tcblim;	/* one past end of ring */
	struct ietcb	*ie_tcbtl;	/* last tcb to transmit */
	struct ietcb	*ie_tcbclaimed;	/* last tcb claimed */

	/* Transmit Buffer Descriptors */
	struct ietbd	*ie_tbdring;	/* tbd ring start */
	struct ietbd	*ie_tbdlim;	/* one past end of ring */

	ddi_dma_handle_t ie_dscpthandle;	/* iopb dma handle */
	ddi_dma_handle_t ie_bufhandle;		/* bufs dma handle */

	ulong_t	ie_ipackets;		/* # packets received */
	ulong_t	ie_ierrors;		/* # total input errors */
	ulong_t	ie_opackets;		/* # packets sent */
	ulong_t	ie_oerrors;		/* # total output errors */
	ulong_t	ie_collisions;		/* # collisions */
	ulong_t	ie_xmiturun;		/* # transmit underrun */
	ulong_t	ie_defer;		/* # defers */
	ulong_t	ie_heart;		/* # hearts */
	ulong_t	ie_crc;			/* # receive crc errors */
	ulong_t	ie_align;		/* # alignment errors */
	ulong_t	ie_recvorun;		/* # receive overrun */
	ulong_t	ie_discard;		/* # discarded packets */
	ulong_t	ie_oflo;		/* # receiver overflows */
	ulong_t	ie_uflo;		/* # transmit underflows */
	ulong_t	ie_missed;		/* # receive missed */
	ulong_t	ie_tlcol;		/* # transmit late collisions */
	ulong_t	ie_trtry;		/* # transmit retry failures */
	ulong_t	ie_tnocar;		/* # loss of carrier errors */
	ulong_t	ie_tnocts;		/* # loss of clear to send */
	ulong_t	ie_tbuff;		/* # BUFF set in tmd */
	ulong_t	ie_tsync;		/* # out of synch OWN bit occurences */
	ulong_t	ie_inits;		/* # driver inits */
	ulong_t	ie_notcbs;		/* # out of tcbs occurences */
	ulong_t	ie_notbufs;		/* # out of buffers for xmit */
	ulong_t	ie_norbufs;		/* # out of buffers for receive */
	ulong_t	ie_runotready;		/* # receive unit not ready */
	ulong_t	ie_nocanput;		/* # input canput() returned false */
	ulong_t	ie_allocbfail;		/* # esballoc/allocb failed */
	ulong_t	ie_loaned;		/* # receive biffer loaned */
	ulong_t	ie_dogreset;		/* # reset by iedog() */
};

/* flags */
#define	IERUNNING	0x01	/* chip is initialized */
#define	IESLAVE		0x02	/* slave device (no DMA) */
#define	IEPROMISC	0x04	/* promiscuous mode enabled */

/* Controller types */
#define	IE_OB	1	/* On main CPU board ("On-Board") */
#define	IE_TE	2	/* 3E Ethernet board */

/*
 * Fast aligned copy requires both the source and destination
 * addresses have the same offset from some N-byte boundary.
 */
#define	IEBURSTSIZE	(32)
#define	IEBURSTMASK	(IEBURSTSIZE-1)

#define	IEROUNDUP(a, n)	(((a) + ((n) - 1)) & ~((n) - 1))

/*
 * Xmit/receive buffer structure.
 * This structure is organized to meet the following requirements:
 * - ib_buf starts on an IEBURSTSIZE boundary.
 * - iebuf is an even multiple of IEBURSTSIZE
 * - ib_buf[] is large enough to contain max frame (1518) plus
 *   IEBURSTSIZE for alignment adjustments
 */
#define		IEBUFSIZE	(1600 - sizeof (struct ie *) - sizeof (frtn_t))
struct iebuf {
	u_char	ib_buf[IEBUFSIZE];	/* raw buffer */
	u_char	ib_fudge[IEBURSTSIZE];	/* kludge for buffer overrun */
	struct	ie	*ib_iep;	/* link to device structure */
	frtn_t	ib_frtn;		/* for esballoc() */
};

/*
 * Private DLPI full dlsap address format.
 */
struct	iedladdr {
	struct	ether_addr	dl_phys;
	u_short	dl_sap;
};

/*
 * Define offset from start of ib_buf[] to point receive descriptor.
 * Requirements:
 * - must be 14 bytes back of a 4-byte boundary so the start of
 *   the network packet is 4-byte aligned.
 * - must leave room for the largest datalink header in case the
 *   packet is routed out another interface.
 */
#define		IERBUFOFF	22

/*
 * "Export" a few of the error counters via the kstats mechanism.
 */
struct	iestat {
	struct	kstat_named	ies_ipackets;
	struct	kstat_named	ies_ierrors;
	struct	kstat_named	ies_opackets;
	struct	kstat_named	ies_oerrors;
	struct	kstat_named	ies_collisions;
	struct	kstat_named	ies_defer;
	struct	kstat_named	ies_crc;
	struct	kstat_named	ies_oflo;
	struct	kstat_named	ies_uflo;
	struct	kstat_named	ies_missed;
	struct	kstat_named	ies_tlcol;
	struct	kstat_named	ies_trtry;
	struct	kstat_named	ies_tnocar;
	struct	kstat_named	ies_inits;
	struct	kstat_named	ies_nocanput;
	struct	kstat_named	ies_allocbfail;
	struct	kstat_named	ies_xmiturun;
	struct	kstat_named	ies_recvorun;
	struct	kstat_named	ies_align;
	struct	kstat_named	ies_notcbs;
	struct	kstat_named	ies_notbufs;
	struct	kstat_named	ies_norbufs;
};

typedef struct ie ie_t;
typedef struct iebuf iebuf_t;

extern	int dlokack();
extern	int dlbindack();
extern	int dlphysaddrack();
extern	int dluderrorind();
extern	int dlerrorack();

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_IE_H */
