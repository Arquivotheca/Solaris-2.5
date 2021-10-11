/*
 * Copyright (c) 1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef	_SYS_SCSI_TARGETS_STDEF_H
#define	_SYS_SCSI_TARGETS_STDEF_H

#pragma ident	"@(#)stdef.h	1.47	95/06/29 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Defines for SCSI tape drives.
 */

/*
 * Maximum variable length record size for a single request
 */
#define	ST_MAXRECSIZE_VARIABLE	65535

/*
 * If the requested record size exceeds ST_MAXRECSIZE_VARIABLE,
 * then the following define is used.
 */
#define	ST_MAXRECSIZE_VARIABLE_LIMIT	65534

#define	ST_MAXRECSIZE_FIXED	(63<<10)	/* maximum fixed record size */
#define	INF 1000000000

/*
 * Supported tape device types plus default type for opening.
 * Types 10 - 13, are special (ancient too) drives - *NOT SUPPORTED*
 * Types 14 - 1f, are 1/4-inch cartridge drives.
 * Types 20 - 28, are 1/2-inch cartridge or reel drives.
 * Types 28+, are rdat (vcr) drives.
 */
#define	ST_TYPE_INVALID		0x00

#define	ST_TYPE_SYSGEN1	MT_ISSYSGEN11	/* Sysgen with QIC-11 only */
#define	ST_TYPE_SYSGEN	MT_ISSYSGEN	/* Sysgen with QIC-24 and QIC-11 */

#define	ST_TYPE_DEFAULT	MT_ISDEFAULT	/* Generic 1/4" or undetermined  */
#define	ST_TYPE_EMULEX	MT_ISMT02	/* Emulex MT-02 */
#define	ST_TYPE_ARCHIVE	MT_ISVIPER1	/* Archive QIC-150 */
#define	ST_TYPE_WANGTEK	MT_ISWANGTEK1	/* Wangtek QIC-150 */

#define	ST_TYPE_CDC	MT_ISCDC	/* CDC - (not tested) */
#define	ST_TYPE_FUJI	MT_ISFUJI	/* Fujitsu - (not tested) */
#define	ST_TYPE_KENNEDY	MT_ISKENNEDY	/* Kennedy */
#define	ST_TYPE_ANRITSU	MT_ISANRITSU	/* Anritsu */
#define	ST_TYPE_HP	MT_ISHP		/* HP */
#define	ST_TYPE_HIC	MT_ISCCS23	/* Generic 1/2" Cartridge */
#define	ST_TYPE_REEL	MT_ISCCS24	/* Generic 1/2" Reel Tape */
#define	ST_TYPE_DAT	MT_ISCCS28	/* Generic DAT Tape */

#define	ST_TYPE_EXABYTE	MT_ISEXABYTE	/* Exabyte 8200 */
#define	ST_TYPE_EXB8500	MT_ISEXB8500	/* Exabyte 8500 */
#define	ST_TYPE_WANGTHS	MT_ISWANGTHS	/* Wangtek 6130HS */
#define	ST_TYPE_WANGDAT	MT_ISWANGDAT	/* WangDAT */
#define	ST_TYPE_PYTHON  MT_ISPYTHON	/* Archive Python DAT */
#define	ST_TYPE_STC3490 MT_ISSTC	/* IBM STC 3490 */
#define	ST_TYPE_TAND25G MT_ISTAND25G	/* TANDBERG 2.5G */


/* Internal flags */
#define	ST_DYNAMIC		0x2000	/* Device name has been dynamically */
					/* alloc'ed from the st.conf entry, */
					/* instead of being used from the */
					/* st_drivetypes array. */


/*
 * Defines for supported drive options
 *
 * WARNING : THESE OPTIONS SHOULD NEVER BE CHANGED, AS OLDER CONFIGURATIONS
 * 		WILL DEPEND ON THE FLAG VALUES REMAINING THE SAME
 */
#define	ST_VARIABLE		0x001	/* Device supports variable	*/
					/* length record sizes		*/
#define	ST_QIC			0x002	/* QIC tape device 		*/
#define	ST_REEL			0x004	/* 1/2-inch reel tape device	*/
#define	ST_BSF			0x008	/* Device supports backspace	*/
					/* file as in mt(1) bsf : 	*/
					/* backspace over EOF marks.	*/
					/* Devices not supporting bsf 	*/
					/* will fail with ENOTTY upon	*/
					/* use of bsf			*/
#define	ST_BSR			0x010	/* Device supports backspace	*/
					/* record as in mt(1) bsr :	*/
					/* backspace over records. If	*/
					/* the device does not support 	*/
					/* bsr, the st driver emulates	*/
					/* the action by rewinding the	*/
					/* tape and using forward space	*/
					/* file (fsf) to the correct	*/
					/* file and then uses forward	*/
					/* space record (fsr) to the	*/
					/* correct  record		*/
#define	ST_LONG_ERASE		0x020	/* Device needs a longer time	*/
					/* than normal to erase		*/
#define	ST_AUTODEN_OVERRIDE	0x040	/* Auto-Density override flag	*/
					/* Device can figure out the	*/
					/* tape density automatically,	*/
					/* without issuing a		*/
					/* mode-select/mode-sense 	*/
#define	ST_NOBUF		0x080	/* Don't use buffered mode.	*/
					/* This disables the device's	*/
					/* ability for buffered	writes	*/
					/* I.e. The device acknowledges	*/
					/* write completion after the	*/
					/* data is written to the	*/
					/* device's buffer, but before	*/
					/* all the data is actually	*/
					/* written to tape		*/
#define	ST_RESERVED_BIT1	0x100	/* resreved bit 		*/
					/* parity while talking to it. 	*/
#define	ST_KNOWS_EOD		0x200	/* Device knows when EOD (End	*/
					/* of Data) has been reached.	*/
					/* If the device knows EOD, st	*/
					/* uses fast file skipping.	*/
					/* If it does not know EOD,	*/
					/* file skipping happens one	*/
					/* file at a time. 		*/
#define	ST_UNLOADABLE		0x400	/* Device will not complain if	*/
					/* the st driver is unloaded &	*/
					/* loaded again; e.g. will	*/
					/* return the correct inquiry	*/
					/* string			*/
#define	ST_SOFT_ERROR_REPORTING 0x800	/* Do request or log sense on	*/
					/* close to report soft errors.	*/
					/* Currently only Exabyte and	*/
					/* DAT drives support this	*/
					/* feature.  			*/
#define	ST_LONG_TIMEOUTS	0x1000	/* Device needs 5 times longer	*/
					/* timeouts for normal		*/
					/* operation			*/
#define	ST_BUFFERED_WRITES	0x4000	/* The data is buffered in the	*/
					/* driver and pre-acked to the	*/
					/* application 			*/
#define	ST_NO_RECSIZE_LIMIT	0x8000	/* For variable record size	*/
					/* devices only. If flag is	*/
					/* set, then don't limit	*/
					/* record size to 64k as in	*/
					/* pre-Solaris 2.4 releases.	*/
					/* The only limit on the	*/
					/* record size will be the max	*/
					/* record size the device can	*/
					/* handle or the max DMA	*/
					/* transfer size of the		*/
					/* machine, which ever is	*/
					/* smaller. Beware of		*/
					/* incompatabilities with	*/
					/* tapes of pre-Solaris 2.4	*/
					/* OS's written with large	*/
					/* (>64k) block sizes, as	*/
					/* their true block size is	*/
					/* a max of approx 64k		*/
#define	ST_MODE_SEL_COMP	0x10000	/* use mode select of device	*/
					/* configuration page (0x10) to */
					/* enable/disable compression	*/
					/* instead of density codes for */
					/* the "c" and "u" devices	*/

#define	NDENSITIES	MT_NDENSITIES
#define	NSPEEDS		MT_NSPEEDS

struct st_drivetype {
	char	*name;			/* Name, for debug */
	char	length;			/* Length of vendor id */
	char	vid[24];		/* Vendor id and model (product) id */
	char	type;			/* Drive type for driver */
	int	bsize;			/* Block size */
	int	options;		/* Drive options */
	int	max_rretries;		/* Max read retries */
	int	max_wretries;		/* Max write retries */
	u_char	densities[NDENSITIES];	/* density codes, low->hi */
	u_char	default_density;	/* default density for this drive */
	u_char	speeds[NSPEEDS];	/* speed codes, low->hi */
};

/*
 *
 * Parameter list for the MODE_SELECT and MODE_SENSE commands.
 * The parameter list contains a header, followed by zero or more
 * block descriptors, followed by vendor unique parameters, if any.
 *
 */

#define	MSIZE	(sizeof (struct seq_mode))
struct seq_mode {
	u_char	reserved1;	/* reserved, sense data length */
	u_char	reserved2;	/* reserved, medium type */
	u_char	wp	:1,	/* write protected */
		bufm	:3,	/* buffered mode */
		speed	:4;	/* speed */
	u_char	bd_len;		/* block length in bytes */
	u_char	density;	/* density code */
	u_char	high_nb;	/* number of logical blocks on the medium */
	u_char	mid_nb;		/* that are to be formatted with the density */
	u_char	low_nb;		/* code and block length in block descriptor */
	u_char	reserved;	/* reserved */
	u_char	high_bl;	/* block length */
	u_char	mid_bl;		/*   "      "   */
	u_char	low_bl;		/*   "      "   */
};

/*
 * Data returned from the READ BLOCK LIMITS command.
 */

#define	RBLSIZE	(sizeof (struct read_blklim))
struct read_blklim {
	u_char	reserved;	/* reserved */
	u_char	max_hi;		/* Maximum block length, high byte */
	u_char	max_mid;	/* Maximum block length, middle byte */
	u_char	max_lo;		/* Maximum block length, low byte */
	u_char	min_hi;		/* Minimum block length, high byte */
	u_char	min_lo;		/* Minimum block length, low byte */
};

/*
 * Private info for scsi tapes. Pointed to by the un_private pointer
 * of one of the SCSI_DEVICE chains.
 */

struct scsi_tape {
	struct scsi_device *un_sd;	/* back pointer to SCSI_DEVICE */
	struct scsi_pkt *un_rqs;	/* ptr to request sense command */
	kcondvar_t un_sbuf_cv;		/* cv on ownership of special buf */
	kcondvar_t un_write_queue_cv;	/* cv on write queue */
	struct	buf *un_sbufp;		/* for use in special io */
	char	*un_srqbufp;		/* sense buffer for special io */
	kcondvar_t un_clscv;		/* closing cv */
	struct	buf *un_quef;		/* head of wait queue */
	struct	buf *un_quel;		/* tail of wait queue */
	struct	buf *un_runqf;		/* head of run queue */
	struct	buf *un_runql;		/* tail of run queue */
	struct seq_mode *un_mspl;	/* ptr to mode select info */
	struct st_drivetype *un_dp;	/* ptr to drive table entry */
	u_int	un_dp_size;		/* size of un_dp alloc'ed */
	caddr_t	un_tmpbuf;		/* buf for append, autodens ops */
	daddr_t	un_blkno;		/* block # in file (512 byte blocks) */
	int	un_oflags;		/* open flags */
	int	un_fileno;		/* current file number on tape */
	int	un_err_fileno;		/* file where error occurred */
	daddr_t	un_err_blkno;		/* block in file where err occurred */
	u_int	un_err_resid;		/* resid from last error */
	short	un_fmneeded;		/* filemarks to be written - HP only */
	dev_t	un_dev;			/* unix device */
	u_char	un_attached;		/* unit known && attached */
	u_char	un_suspended;		/* True, if suspended */
	u_char	un_density_known;	/* density is known */
	u_char	un_curdens;		/* index into density table */
	u_char	un_lastop;		/* last I/O was: read/write/ctl */
	u_char	un_eof;			/* eof states */
	u_char	un_laststate;		/* last state */
	u_char	un_state;		/* current state */
	u_char	un_status;		/* status from last sense */
	u_char	un_retry_ct;		/* retry count */
	u_char	un_tran_retry_ct;	/* transport retry count */
	u_char	un_read_only;		/* 1 == opened O_RDONLY */
	u_char	un_test_append;		/* check writing at end of tape */
	u_char  un_arq_enabled;		/* auto request sense enabled */
	u_char	un_allow_buffered_writes; /* buffered writes enabled */
	u_char	un_allow_large_xfer;	/* allow >64k xfers if requested */
	u_char	un_sbuf_busy;		/* sbuf busy flag */
	u_char	un_ncmds;		/* number of commands outstanding */
	u_char  un_throttle;		/* max number of cmds outstanding */
	u_char  un_last_throttle;	/* saved max number of cmds outst. */
	u_int	un_kbytes_xferred;	/* bytes (in K) counter */
	struct buf *un_rqs_bp;		/* bp used in rqpkt */
	struct	buf *un_wf;		/* head of write queue */
	struct	buf *un_wl;		/* tail of write queue */
	struct	read_blklim *un_rbl;	/* ptr to read block limit info */
	int	un_maxdma;		/* max dma xfer allowed by HBA */
	u_int	un_bsize;		/* block size currently being used */
	int	un_maxbsize;		/* max block size allowed by drive */
	u_int	un_minbsize;		/* min block size allowed by drive */
	u_int	un_max_buf_size;	/* max buf size for buffering	*/
};

/* ALL MEMBERS PROTECTED BY scsi_device::sd_mutex */
/* MEMBERS PROTECTED BY "stable data": scsi_tape::un_dp */
/* MEMBERS PROTECTED BY "stable data": scsi_tape::un_sd */
/* MEMBERS PROTECTED BY "not shared": scsi_tape::un_rqs */
/* MEMBERS READABLE WITHOUT LOCK: scsi_tape::un_bsize */
/* MEMBERS PROTECTED BY "not shared": scsi_arq_status:: */

/*
 * driver states..
 */

#define	ST_STATE_CLOSED			0
#define	ST_STATE_OFFLINE		1
#define	ST_STATE_INITIALIZING		2
#define	ST_STATE_OPENING		3
#define	ST_STATE_OPEN_PENDING_IO	4
#define	ST_STATE_APPEND_TESTING		5
#define	ST_STATE_OPEN			6
#define	ST_STATE_RESOURCE_WAIT		7
#define	ST_STATE_CLOSING		8
#define	ST_STATE_SENSING		9

/*
 * operation codes
 */

#define	ST_OP_NIL	0
#define	ST_OP_CTL	1
#define	ST_OP_READ	2
#define	ST_OP_WRITE	3
#define	ST_OP_WEOF	4

/*
 * eof/eot/eom codes.
 */

#define	ST_NO_EOF		0x00
#define	ST_EOF_PENDING		0x01	/* filemark pending */
#define	ST_EOF			0x02	/* at filemark */
#define	ST_EOT_PENDING		0x03	/* logical eot pending */
#define	ST_EOT			0x04	/* at logical eot */
#define	ST_EOM			0x05	/* at physical eot */
#define	ST_WRITE_AFTER_EOM	0x06	/* flag for allowing writes after EOM */

#define	IN_EOF(un)	(un->un_eof == ST_EOF_PENDING || un->un_eof == ST_EOF)

/*
 * stintr codes
 */

#define	COMMAND_DONE			0
#define	COMMAND_DONE_ERROR		1
#define	COMMAND_DONE_ERROR_RECOVERED	2
#define	QUE_COMMAND			3
#define	QUE_BUSY_COMMAND		4
#define	QUE_SENSE			5
#define	JUST_RETURN			6

/*
 * Parameters
 */

#define	ST_NAMESIZE	44	/* size of pretty string for vid/pid */
#define	VIDLEN		8	/* size of vendor identifier length */
#define	PIDLEN		16	/* size of product identifier length */

/*
 * 60 minutes seems a reasonable amount of time
 * to wait for tape space operations to complete.
 *
 */
#define	ST_SPACE_TIME		60*60	/* 60 minutes per space operation */

/*
 * 2 minutes seems a reasonable amount of time
 * to wait for tape i/o operations to complete.
 *
 */
#define	ST_IO_TIME		2*60	/* minutes per i/o */
#define	LONG_TIMEOUTS		5	/* multiplier for very long timeouts */


/*
 * 10 seconds is what we'll wait if we get a Busy Status back
 */
#define	ST_STATUS_BUSY_TIMEOUT	10*hz	/* seconds Busy Waiting */
#define	ST_TRAN_BUSY_TIMEOUT	1*hz	/* seconds retry on TRAN_BSY */
#define	ST_INTERRUPT_CONTEXT	1
#define	ST_START_CONTEXT	2

/*
 * Number of times we'll retry a normal operation.
 *
 * XXX This includes retries due to transport failure as well as
 * XXX busy timeouts- Need to distinguish between Target and Transport
 * XXX failure.
 */

#define	ST_RETRY_COUNT		20

/*
 * Number of times to retry a failed selection
 */
#define	ST_SEL_RETRY_COUNT		2

/*
 * Maximum number of units (determined by minor device byte)
 */
#define	ST_MAXUNIT	128

#ifndef	SECSIZE
#define	SECSIZE	512
#endif
#ifndef	SECDIV
#define	SECDIV	9
#endif

/*
 * convenient defines
 */
#define	ST_SCSI_DEVP		(un->un_sd)
#define	ST_DEVINFO		(ST_SCSI_DEVP->sd_dev)
#define	ST_INQUIRY		(ST_SCSI_DEVP->sd_inq)
#define	ST_RQSENSE		(ST_SCSI_DEVP->sd_sense)
#define	ST_MUTEX		(&ST_SCSI_DEVP->sd_mutex)
#define	ROUTE			(&ST_SCSI_DEVP->sd_address)

#define	BSD_BEHAVIOR		(getminor(un->un_dev) & MT_BSD)
#define	SVR4_BEHAVIOR		((getminor(un->un_dev) & MT_BSD) == 0)
#define	SCBP(pkt)		((struct scsi_status *)(pkt)->pkt_scbp)
#define	SCBP_C(pkt)		((*(pkt)->pkt_scbp) & STATUS_MASK)
#define	CDBP(pkt)		((union scsi_cdb *)(pkt)->pkt_cdbp)
#define	BP_PKT(bp)		((struct scsi_pkt *)(bp)->av_back)
#define	SET_BP_PKT(bp, pkt)	((bp)->av_back = (struct buf *)(pkt))
#define	BP_UCMD(bp)		((struct uscsi_cmd *)(bp)->b_back)
#define	USCSI_CMD(bp)		(((bp) == un->un_sbufp) && (BP_UCMD(bp)))

#define	IS_CLOSING(un)		((un)->un_state == ST_STATE_CLOSING || \
	((un)->un_state == ST_STATE_SENSING && \
		(un)->un_laststate == ST_STATE_CLOSING))

#define	ASYNC_CMD	0
#define	SYNC_CMD	1

/*
 * Macros for internal coding of count for SPACE command:
 *
 * Isfmk is 1 when spacing filemarks; 0 when spacing records:
 * bit 24 set indicates a space filemark command.
 * Fmk sets the filemark bit (24) and changes a backspace
 * count into a positive number with the sign bit set.
 * Blk changes a backspace count into a positive number with
 * the sign bit set.
 * space_cnt converts backwards counts to negative numbers.
 */
#define	Isfmk(x)	((x & (1<<24)) != 0)
#define	Fmk(x)		((1<<24)|((x < 0) ? ((-(x)) | (1<<30)): x))
#define	Blk(x)		((x < 0)? ((-(x))|(1<<30)): x)
#define	space_cnt(x)	(((x) & (1<<30))? (-((x)&((1<<24)-1))):(x)&((1<<24)-1))


#define	GET_SOFT_STATE(dev)						\
	register struct scsi_tape *un;					\
	register int instance;						\
									\
	instance = MTUNIT(dev);						\
	if ((un = ddi_get_soft_state(st_state, instance)) == NULL)	\
		return (ENXIO);

/*
 * flag for st_start(), which allows making a copy of the bp
 * (copyin only works in user context)
 */
#define	ST_USER_CONTEXT 1

/*
 * Debugging turned on via conditional compilation switch -DSTDEBUG
 */
#ifdef DEBUG
#define	STDEBUG
#endif

#ifdef	STDEBUG
#define	DEBUGGING	((scsi_options & SCSI_DEBUG_TGT) || st_debug > 1)


#define	ST_DEBUG1	if (st_debug >= 1) scsi_log	/* initialization */
#define	ST_DEBUG	ST_DEBUG1

#define	ST_DEBUG2	if (st_debug >= 2) scsi_log	/* errors and UA's */
#define	ST_DEBUG3	if (st_debug >= 3) scsi_log	/* func calls */
#define	ST_DEBUG4	if (st_debug >= 4) scsi_log	/* ioctl calls */
#define	ST_DEBUG5	if (st_debug >= 5) scsi_log
#define	ST_DEBUG6	if (st_debug >= 6) scsi_log	/* full data tracking */

#define	ST_DEBUG_SP	if (st_debug == 10) scsi_log	/* special cases */

#else

#define	st_debug	(0)
#define	DEBUGGING	(0)
#define	ST_DEBUG	if (0) scsi_log
#define	ST_DEBUG1	if (0) scsi_log
#define	ST_DEBUG2	if (0) scsi_log
#define	ST_DEBUG3	if (0) scsi_log
#define	ST_DEBUG4	if (0) scsi_log
#define	ST_DEBUG5	if (0) scsi_log
#define	ST_DEBUG6	if (0) scsi_log

#define	ST_DEBUG_SP	if (0) scsi_log /* special cases */

#endif

/*
 * SCSI tape mode sense page information
 */
#define	ST_DEV_CONFIG_PAGE	0x10	/* device configuration mode page */
#define	ST_DEV_CONFIG_ALLOC_LEN	0x1C	/* max buff size needed */
#define	ST_DEV_CONFIG_COMP_BYTE	0x1A	/* index  of compression byte */
#define	ST_DEV_CONFIG_PL_BYTE	0x0d	/* index of dcp len sense data */
#define	ST_DEV_CONFIG_NO_COMP	0x00	/* use no compression */
#define	ST_DEV_CONFIG_DEF_COMP	0x01	/* use default compression alg */


/*
 * maxbsize values
 */
#define	MAXBSIZE_UNKNOWN	-2	/*  not found yet */

#define	ONE_MEG			(1024 * 1024)

/*
 * generic soft error reporting
 *
 * What we are doing here is allowing a greater number of errors to occur on
 * smaller transfers (i.e. usually at the beginning of the tape), than on
 * the rest of the tape.
 *
 * A small transfer is defined as :
 * Transfers <= SOFT_ERROR_WARNING_THRESHOLD  allow about 1.5 times more errors
 *
 * A larget tranfer is defined as :
 * Transfers >  SOFT_ERROR_WARNING_THRESHOLD  allow normal amount
 *
 */
#define	SOFT_ERROR_WARNING_THRESHOLD    (25 * ONE_MEG)

/*
 * soft error reporting for exabyte
 */
#define	TAPE_SENSE_LENGTH	32	/* allows for softerror info */

#define	SENSE_19_BITS  \
	"\20\10PF\07BPE\06FPE\05ME\04ECO\03TME\02TNP\01LBOT"
#define	SENSE_20_BITS  \
	"\20\10RSVD\07RSVD\06WP\05FMKE\04URE\03WE1\02SSE\01FW"
#define	SENSE_21_BITS  \
	"\20\10RSVD\07RSVD\06RRR\05CLND\04CLN\03PEOT\02WSEB\01WSE0"

/* these are defined in percentages */
#define	EXABYTE_SMALL_WRITE_ERROR_THRESHOLD	8
#define	EXABYTE_LARGE_WRITE_ERROR_THRESHOLD	6
#define	EXABYTE_SMALL_READ_ERROR_THRESHOLD	5
#define	EXABYTE_LARGE_READ_ERROR_THRESHOLD	3

#define	CLN	0x8
#define	CLND	0x10

/*
 * soft error reporting for Archive 4mm DAT
 */

#define	LOG_SENSE_LENGTH		0xff
#define	MIN_LOG_SENSE_LENGTH		0x2b
#define	LOG_SENSE_CMD			0x4d
#define	LOG_SELECT_CMD			0x4c
#define	DAT_SMALL_WRITE_ERROR_THRESHOLD	75	/* errors per gig */
#define	DAT_LARGE_WRITE_ERROR_THRESHOLD	50	/* errors per gig */
#define	DAT_SMALL_READ_ERROR_THRESHOLD	5	/* errors allowed */
#define	DAT_LARGE_READ_ERROR_THRESHOLD	3	/* errors allowed */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SCSI_TARGETS_STDEF_H */
