/*
 * Copyright (c) 1989-1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef	_SYS_FDVAR_H
#define	_SYS_FDVAR_H

#pragma ident	"@(#)fdvar.h	1.26	95/01/19 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#ifndef	OTYPCNT
#define	OTYPCNT	5
#endif
#ifndef	NDKMAP
#define	NDKMAP	8
#endif

/*
 * Compile with our without high level interrupt in trap window
 */

/* #define	NO_TRAPWIN_INTR	*/

/*
 * Macros for partition/unit from floppy device number,
 * plus other manifest defines....
 */

#define	PARTITION(x)	(getminor(x) & 0x7)
#define	UNIT(x)		(getminor(x) >> 3)

/*
 * There are never more than four drives per controller in the floppy world.
 */
#define	NFDUN	4
#ifdef i386
#define	FDUNIT(x)	0 /* unit on controller */
#else
#define	FDUNIT(x)	(UNIT(x) & 0x3)	/* unit on controller */
#endif
#define	FDCTLR(x)	(UNIT(x) >> 2)	/* controller instance */
/*
 * Structure definitions for the floppy driver.
 */

/*
 * floppy disk command and status block.
 *
 * Needed to execute a command. Since the floppy chip is
 * single threaded with respect to having only one drive
 * active at a time, this block of information is only
 * valid for the length of a commnand and gets rewritten
 * for each command.
 */

#ifndef	_ASM
struct fdcsb {
	caddr_t	csb_addr;	/* Data buffer address */
	u_int	csb_len;	/* Data buffer Length */
	caddr_t	csb_raddr;	/* modified data buffer address */
	u_int	csb_rlen;	/* modified data buffer len (resid) */
	u_char	csb_opmode;	/* Current operating mode */
	u_char	csb_unit;	/* floppy slave unit number */
	u_char	csb_ncmds;	/* how many command bytes to send */
	u_char	csb_nrslts;	/* number of result bytes gotten */
	u_char	csb_opflags;	/* opflags, see below */
	u_char	csb_maxretry;	/* maximum retries this opertion */
	u_char	csb_retrys;	/* how may retrys done so far */
	u_char	csb_status;	/* status returned from hwintr */
	u_char	csb_cmdstat;	/* if 0 then success, else failure */
	u_char	csb_cmds[10];	/* commands to send to chip */
	u_char	csb_rslt[10];	/* results from chip */
	u_char  csb_dcsr_rslt;  /* set to 1 if there's an error in the DCSR */
	u_char	csb_dma_rslt;	/* set to 1 if there's an error with the DMA */
	ddi_dma_cookie_t csb_dmacookie; /* DMA cookie */

	u_int	csb_ccount;	/* no. of DMA cookies for current window */
	u_int	csb_nwin;	/* no. of DMA windows */
	u_int	csb_windex;	/* DMA window currently in use */
	u_int	csb_dma_read;	/* indicates dma read, dma write, or no DMA */
};
#endif	/* !_ASM */

/*
 * defines for csb_opflags
 */
#define	CSB_OFIMMEDIATE	0x01		/* grab results immediately */
#define	CSB_OFSEEKOPS	0x02		/* seek/recal type cmd */
#define	CSB_OFXFEROPS	0x04		/* read/write type cmd */
#define	CSB_OFRAWIOCTL	0x10		/* raw ioctl - no recovery */
#define	CSB_OFNORESULTS	0x20		/* no results at all */
#define	CSB_OFTIMEIT	0x40		/* timeout (timer) */

#define	CSB_CMDTO 0x01

/*
 * csb_dma_read flags
 */
#define	CSB_DMA_READ	0x1
#define	CSB_DMA_WRITE	0x2

#ifndef	_ASM
#ifndef	_GENASSYM

/*
 * Define a structure to hold the packed default labels,
 * based on the real dk_label structure - but shorter
 * than 512 bytes. Now only used to define default info
 */
struct packed_label {
	char		dkl_vname[128];	/* for ascii compatibility */
	unsigned short	dkl_rpm;	/* rotations per minute */
	unsigned short	dkl_pcyl;	/* # physical cylinders */
	unsigned short	dkl_apc;	/* alternates per cylinder */
	unsigned short	dkl_intrlv;	/* interleave factor */
	unsigned short	dkl_ncyl;	/* # of data cylinders */
	unsigned short	dkl_acyl;	/* # of alternate cylinders */
	unsigned short	dkl_nhead;	/* # of heads in this partition */
	unsigned short	dkl_nsect;	/* # of 512 byte sectors per track */
	struct dk_map	dkl_map[NDKMAP]; /* partition map, see dkio.h */
	struct dk_vtoc  dkl_vtoc;	/* vtoc stuff from AT&T SVr4 */
};

/*
 * Per drive data
 */
struct fdunit {
	/*
	 * Packed label for this unit
	 */
	struct	dk_label un_label;

	/*
	 * Pointer to iostat statistics
	 */
	struct kstat *un_iostat;	/* iostat numbers */

	/*
	 * Layered open counters
	 */
	u_long	un_lyropen[NDKMAP];

	/*
	 * Regular open type flags. If
	 * NDKMAP gets > 8, change the
	 * u_char type.
	 *
	 * Open types BLK, MNT, CHR, SWP
	 * assumed to be values 0-3.
	 */
	u_char	un_regopen[OTYPCNT - 1];

	/*
	 * Exclusive open flags (per partition).
	 *
	 * The rules are that in order to open
	 * a partition exclusively, the partition
	 * must be completely closed already. Once
	 * any partition of the device is opened
	 * exclusively, no other open on that
	 * partition may succeed until the partition
	 * is closed.
	 *
	 * If NDKMAP gets > 8, this must change.
	 */
	u_char	un_exclmask;		/* set to indicate exclusive open */

	struct	fd_char *un_chars;	/* pointer to drive characteristics */
	char	un_curfdtype;		/* current driver characteristics */
					/* type. If -1, then it was set */
					/* via an ioctl. Note that a close */
					/* and then and open loses the */
					/* ioctl set characteristics. */
	u_char	un_flags;		/* state information */
	int	un_media_timeout;	/* media detection timeout */
	int	un_media_timeout_id;	/* media detection timeout id */
	enum dkio_state	un_media_state;	/* up-to-date media state */
	int	un_ejected;
};

/* unit flags (state info) */
#define	FDUNIT_DRVCHECKED	0x01	/* this is drive present */
#define	FDUNIT_DRVPRESENT	0x02	/* this is drive present */
/* (the presence of a diskette is another matter) */
#define	FDUNIT_CHAROK		0x04	/* characteristics are known */
#define	FDUNIT_UNLABELED	0x10	/* no label using default */
#define	FDUNIT_CHANGED		0x20	/* diskette was changed after open */
#define	FDUNIT_MEDIUM		0x40	/* fd drive is in medium density */

#endif	/* !_GENASSYM */

/* a place to keep some statistics on what's going on */
struct fdstat {
	/* first operations */
	int rd;		/* count reads */
	int wr;		/* count writes */
	int recal;	/* count recalibrates */
	int form;	/* count format_tracks */
	int other;	/* count other ops */

	/* then errors */
	int reset;	/* count resets */
	int to;		/* count timeouts */
	int run;	/* count overrun/underrun */
	int de;		/* count data errors */
	int bfmt;	/* count bad format errors */
};

/*
 * Per controller data
 */

struct fdctlr {
	struct	fdctlr	*c_next;	/* next in a linked list */
	union  fdcreg   *c_reg;		/* controller registers */
	volatile u_char	*c_control; 	/* addr of c_reg->fdc_control */
	u_char		*c_fifo;	/* addr of c_reg->fdc_fifo */
	u_char		*c_dor;		/* addr of c_reg->fdc_dor (077) */
	u_char		*c_dir;		/* addr of c_reg->fdc_dir (077) */
	struct fdc_dma_reg *c_dma_regs;	/* DMA engine registers */
	u_long		c_fdtype;	/* type of ctlr */
	ulong_t		*c_hiintct;	/* for convenience.. */
	u_long		c_softic;	/* for use by hi level interrupt */
	u_char		c_fasttrap;	/* 1 if fast traps enabled, else 0 */
	struct	fdcsb	c_csb;		/* current csb */
	kmutex_t	c_hilock;	/* high level mutex */
	kmutex_t	c_lolock;	/* low level mutex */
	kcondvar_t	c_iocv;		/* condition var for I/O done */
	kcondvar_t	c_csbcv;	/* condition var for owning csb */
	kcondvar_t	c_motoncv;	/* condition var for motor on */
	kcondvar_t	c_statecv;	/* condition var for media state */
	ksema_t		c_ocsem;	/* sem for serializing opens/closes */
	ddi_iblock_cookie_t c_block;	/* returned from ddi_add_fastintr */
	ddi_softintr_t	c_softid;	/* returned from ddi_add_softintr */
	dev_info_t	*c_dip;		/* controller's dev_info node */
	int		c_timeid;	/* watchdog timer id */
	int		c_mtimeid;	/* motor off timer id */
	struct	fdunit	*c_un[NFDUN];	/* slave on controller */
	struct	buf	*c_actf;	/* head of wait list */
	struct	buf	*c_actl;	/* tail of wait list */
	struct	buf	*c_current;	/* currently active buf */
	struct kstat	*c_intrstat;	/* interrupt stats pointer */
	struct	fdstat	fdstats;	/* statistics */
	u_char		c_flags;	/* state information */
	caddr_t		c_auxiova;	/* auxio virtual address */
	u_char		c_auxiodata;	/* auxio data to enable TC */
	u_char		c_auxiodata2;	/* auxio data to disable TC */
	u_char		c_suspended;    /* true if controller is suspended */
	ddi_acc_handle_t c_handlep_cont;
					/* data access handle for controller */
	ddi_acc_handle_t c_handlep_dma; /* data access handle for DMA engine */
	ddi_acc_handle_t c_handlep_aux;  /* data access handle for aux regs */
	ddi_dma_handle_t c_dmahandle; 	/* DMA handle */
	u_long		 *c_auxio_reg; 	/* auxio registers */
	ddi_dma_attr_t 	c_fd_dma_lim;	/* DMA limit structure */
};
#endif	/* !_ASM */

/* types of controllers supported by this driver */
#define	FDCTYPE_82072	0x0001
#define	FDCTYPE_82077   0x0002
#define	FDCTYPE_CTRLMASK 0x000f

/* types of io chips which indicates the type of auxio register */
#define	FDCTYPE_MUCHIO		0x0010
#define	FDCTYPE_SLAVIO		0x0020
#define	FDCTYPE_CHEERIO		0x0040
#define	FDCTYPE_AUXIOMASK 	0x00f0

/* Method used for transferring data */
#define	FDCTYPE_DMA		0x1000	/* supports DMA for the floppy */
#define	FDCTYPE_PIO		0x0000	/* platform doesn't support DMA */
#define	FDCTYPE_TRNSFER_MASTK	0xf000

/*
 * Early revs of the 82077 have a bug by which they
 * will not respond to the TC (Terminal count) signal.
 * Because this behavior is exhibited on the clone machines
 * for which the 077 code has been targeted, special workaround
 * logic has had to implemented for read/write commands.
 */
#define	FDCTYPE_TCBUG	0x0100
#define	FDCTYPE_BUGMASK	0x0f00

/*
 * Controller flags
 */
#define	FDCFLG_BUSY	0x01	/* operation in progress */
#define	FDCFLG_WANT	0x02	/* csb structure wanted */
#define	FDCFLG_WAITING	0x04	/* waiting on I/O completion */
#define	FDCFLG_TIMEDOUT	0x08	/* the current operation just timed out */


#ifndef	_ASM
/*
 * Miscellaneous
 */
#define	FDREAD	1			/* for fdrw() flag */
#define	FDWRITE	2			/* for fdrw() flag */
#define	FD_CRETRY 1000000		/* retry while sending comand */
#define	FD_RRETRY 1000000		/* retry while getting results */
#define	FDXC_SLEEP	0x1		/* tell fdexec to sleep 'till done */
#define	FDXC_CHECKCHG	0x2		/* tell fdexec to check disk chnged */


/*
 * flags/masks for error printing.
 * the levels are for severity
 */
#define	FDEP_L0		0	/* chatty as can be - for debug! */
#define	FDEP_L1		1	/* best for debug */
#define	FDEP_L2		2	/* minor errors - retries, etc. */
#define	FDEP_L3		3	/* major errors */
#define	FDEP_L4		4	/* catastophic errors, don't mask! */
#define	FDEP_LMAX	4	/* catastophic errors, don't mask! */
#define	FDERRPRINT(l, m, args)	\
	{ if (((l) >= fderrlevel) && ((m) & fderrmask)) cmn_err args; }

/*
 * for each function, we can mask off its printing by clearing its bit in
 * the fderrmask.  Some functions (attach, ident) share a mask bit
 */
#define	FDEM_IDEN 0x00000001	/* fdidentify */
#define	FDEM_ATTA 0x00000001	/* fdattach */
#define	FDEM_SIZE 0x00000002	/* fdsize */
#define	FDEM_OPEN 0x00000004	/* fdopen */
#define	FDEM_GETL 0x00000008	/* fdgetlabel */
#define	FDEM_CLOS 0x00000010	/* fdclose */
#define	FDEM_STRA 0x00000020	/* fdstrategy */
#define	FDEM_STRT 0x00000040	/* fdstart */
#define	FDEM_RDWR 0x00000080	/* fdrdwr */
#define	FDEM_CMD  0x00000100	/* fdcmd */
#define	FDEM_EXEC 0x00000200	/* fdexec */
#define	FDEM_RECO 0x00000400	/* fdrecover */
#define	FDEM_INTR 0x00000800	/* fdintr */
#define	FDEM_WATC 0x00001000	/* fdwatch */
#define	FDEM_IOCT 0x00002000	/* fdioctl */
#define	FDEM_RAWI 0x00004000	/* fdrawioctl */
#define	FDEM_DUMP 0x00008000	/* fddump */
#define	FDEM_GETC 0x00010000	/* fdgetcsb */
#define	FDEM_RETC 0x00020000	/* fdretcsb */
#define	FDEM_RESE 0x00040000	/* fdreset */
#define	FDEM_RECA 0x00080000	/* fdrecalseek */
#define	FDEM_FORM 0x00100000	/* fdformat */
#define	FDEM_RW   0x00200000	/* fdrw */
#define	FDEM_CHEK 0x00400000	/* fdcheckdisk */
#define	FDEM_DSEL 0x00800000	/* fdselect */
#define	FDEM_EJEC 0x01000000	/* fdeject */
#define	FDEM_SCHG 0x02000000	/* fdsense_chng */
#define	FDEM_PACK 0x04000000	/* fdpacklabel */
#define	FDEM_MODS 0x08000000	/* _init, _info, _fini */
#define	FDEM_MOFF 0x10000000	/* fdmotoff */
#define	FDEM_SDMA 0x20000000    /* fdstart_dma */
#define	FDEM_ALL  0xFFFFFFFF	/* all */

#endif	/* !_ASM */

#ifdef	__cplusplus
}
#endif

#endif	/* !_SYS_FDVAR_H */
