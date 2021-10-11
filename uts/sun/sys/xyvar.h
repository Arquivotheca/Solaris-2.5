/*
 * Copyright (c) 1987-1991, by Sun Microsystems, Inc.
 */

#ifndef _SYS_XYVAR_H
#define	_SYS_XYVAR_H

#pragma ident	"@(#)xyvar.h	1.26	94/08/08 SMI"
/* From SunOS 4.1.1 1.16 */

/*
 * Structure definitions for Xylogics 451 disk driver.
 */

#include <sys/xycreg.h>
#include <sys/xyreg.h>
#include <sys/xycom.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Structure needed to execute a command.  Contains controller & iopb ptrs
 * and some error recovery information.  The link is used to identify
 * chained iopbs.
 */
struct xycmdblock {
	struct xyctlr		*c;	/* ptr to controller */
	struct xyunit		*un;	/* ptr to unit */
	struct xyiopb		*iopb;	/* ptr to IOPB */
	union {
		/*
		 * This is either a freelist forward link or
		 * the number of seconds this command has until expiry.
		 */
		struct xycmdblock	*_next;	/* next iopb in queue */
		int			_time;	/* watchdog time */
	} xycmd_un;
#define	xycmd_next	xycmd_un._next
#define	xycmd_time	xycmd_un._time
	kcondvar_t		cw;	/* ASYNCHWAIT variable */
	struct buf		*breq;	/* buffer for this request */
	ddi_dma_handle_t	handle;	/* DMA mapping handle */
	int			flags;	/* state information */
	daddr_t			blkno;	/* current block */
	off_t			boff;	/* offset from start */
	daddr_t			altblk;	/* alternate block (forwarding) */
	int			device;
	u_short			nsect;	/* sector count active */
	u_short			cmd;	/* current command */
	u_char			slave;
	u_char			retries;	/* retry count */
	u_char			restores;	/* restore count */
	u_char			resets;		/* reset count */
	u_char			failed;		/* command failure */
	u_char			busy;	/* cmdblock in use */
	/* added */
	caddr_t			baddr;	/* copy of phys buffer address */
};

/*
 * Data per unit
 */

#define	XYGO(cmdblk)	(*(cmdblk)->un->un_go)(cmdblk)

struct xyunit {
	dev_info_t	*un_dip;	/* dev_info for this unit */
	struct xyctlr	*un_c;		/* controller */
	ksema_t		un_semoclose;	/* serial lock for opens/cls */
	union	ocmap	un_ocmap;	/* map of open devices */
	struct  dk_map  un_map[NDKMAP]; /* logical partitions */
	struct	dk_geom	un_g;		/* disk geometry */
	struct	dkbad	un_bad;		/* bad sector info */
	struct	dk_vtoc	un_vtoc;	/* disk vtoc */
	char	un_asciilabel[LEN_DKL_ASCII]; /* disk label */
	struct	kstat	*un_iostats;	/* unit i/o statistics */

	struct	buf *un_sbufp;		/* for 'special' operations */
	kmutex_t un_sbmutex;		/* mutex on the 'special' buffer */
	int	un_ltick;		/* last time active */
	int	un_errsect;		/* sector in error */
	u_char	un_slave;		/* slave on controller */
	u_char	un_errno;		/* error number */
	u_char	un_errsevere;		/* error severity */
	u_char	un_flags;		/* state information */
	u_short	un_errcmd;		/* command in error */
	u_char  un_drtype;		/* drive type  XXX added */
	long    un_residual;		/* bytes not transfered */
	/*
	 * Think of this as a cache of ddi_get_instance(un->un_dip)
	 */
	int	un_instance;		/* instance number */
	void	(*un_go)(struct xycmdblock *);
};

/*
 * Data per controller
 */

#ifdef  __STDC__
#define	VOLATL	volatile
#else
#define	VOLATL
#endif  /* __STDC__ */

#define	XYSTART(c)	(*(c)->c_start)((caddr_t)(c))
#define	XYGETCBI(ctlr, last, mode)	(*(ctlr)->c_getcbi)(ctlr, last, mode)
#define	XYPUTCBI(cmdblk)	(*(cmdblk)->c->c_putcbi)(cmdblk)
#define	XYCMD(cmdblk, cmd, dev, handle, unit, blkno, secnt, mode, flags) \
	(*(cmdblk)->c->c_cmd)(cmdblk, cmd, dev, handle, unit, blkno, secnt, \
	mode, flags)

struct xyctlr {
	struct xyunit		*c_units[XYUNPERC];	/* slave devices */
	dev_info_t		*c_dip;		/* controller dev_info */
	struct kstat		*c_intrstats;	/* interrupt statistics */
	VOLATL struct xydevice	*c_io;		/* ptr to I/O space data */
	struct buf		*c_waitqf;	/* head of waiting buffers */
	struct buf		*c_waitql;	/* tail of waiting buffers */
	struct xyiopb		*rdyiopbq;	/* ready iopbs */
	struct xyiopb		*lrdy;		/* last ready iopb */
	struct xycmdblock	*c_cmdbase;	/* base of pool of cmdblocks */
	struct xycmdblock	*c_freecmd;	/* free command queue */
	ddi_dma_handle_t	c_ihandle;	/* IOPB dma handle */
	caddr_t			c_iopbbase;	/* iopb pool base */
	kcondvar_t		c_iopbcvp;	/* for waiters on iopbs */
	kmutex_t		c_mutex;	/* mutex on controller */
	ddi_iblock_cookie_t	c_ibc;		/* interrupt block cookie */
	ddi_idevice_cookie_t	c_idc;		/* interrupt device cookie */
	u_char			c_wantint;	/* controller is busy */
	u_char			c_wantsynch;	/* want to do synchronous cmd */
	u_char			c_flags;	/* state information */
	u_char			c_iopbsize;	/* size of each iopb */
	char			c_niopbs;	/* # of iopbs for this ctlr */
	char			c_nfree;	/* # of iopbs free */
	char			c_wantcmd;	/* # of sleepers for iopbs */
	char			c_freewait;	/* waiters on free cbis */
	int			c_nextunit;	/* round-robin unit ptr */
	struct	xycmdblock	*c_chain;	/* ptr to iopb chain */
	kcondvar_t		c_cw;		/* WAIT var "got interrupt" */
	ddi_dma_lim_t		*c_lim;		/* dma limits */
	/*
	 * The routines exported by the controller driver to the
	 * slave driver.
	 */
	int			(*c_start)(caddr_t);
	struct xycmdblock 	*(*c_getcbi)(struct xyctlr *, int, int);
	void			(*c_putcbi)(struct xycmdblock *);
	int			(*c_cmd)(struct xycmdblock *, u_short, int,
				    ddi_dma_handle_t, int, daddr_t, int, int,
				    int);
};

/*
 * These defines are used by some of the ioctl calls.
 */
#define	ONTRACK(x)	(!((x) % ((u_int) un->un_g.dkg_nsect)))
#define	XY_MAXBUFSIZE	(128 * 1024)
/* XXX  check the above */

/*
 * Reinstruct values
 * n = sectors/track
 * r = rpm
 * Assume a rotdelay of 4
 */
#define	XY_ROTDELAY	4
#define	XY_READ_REINSTRUCT(n, r) \
	(((int)n) * ((int)r) * XY_ROTDELAY / 60 / 1000)
#define	XY_WRITE_REINSTRUCT(n, r) \
	(((int)n) * ((int)r) * XY_ROTDELAY / 60 / 1000)

/*
 * Maximum number of controllers supported (driven by max 32 units)
 */
#define	MAX_XYC 	(XYNUNIT/2)
#define	NCBIFREE	16


/*
 * Misc.
 */

#define	XYHWSIZE	sizeof (struct xydevice)

/*
 * Miscellaneous macros
 */

#define	XYCDELAY(c, n)  \
{ \
	register int N = (n); \
	while (--N > 0) { \
		if ((c)) \
			break; \
		drv_usecwait(1); \
	} \
}

#ifdef __cplusplus
}
#endif

#endif	/* !_SYS_XYVAR_H */
