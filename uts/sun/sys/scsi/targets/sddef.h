/*
 * Copyright (c) 1989-1991, by Sun Microsytems, Inc.
 */

#ifndef	_SYS_SCSI_TARGETS_SDDEF_H
#define	_SYS_SCSI_TARGETS_SDDEF_H

#pragma ident	"@(#)sddef.h	1.48	94/10/27 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Defines for SCSI direct access devices
 */

/*
 * Compile options
 */

#define	FIVETWELVE	/* define for 512 bytes/sector */
#define	FIXEDFIRMWARE	/* fixed firmware for volume control */

/*
 * Define various buffer and I/O block sizes.
 *
 * The sector size for CDROM is nominally 2048 bytes, but we fix
 * this by doing a mode sense and checking the default block length.
 */

#if	defined(_KERNEL) || defined(_KMEMUSER)
/*
 * Manifest defines
 */

#define	SECSIZE_DEV		DEV_BSIZE	/* Bytes/sector */
#define	SECDIV_DEV		DEV_BSHIFT	/* log2 (SECSIZE) */
#define	SECSIZE_FIVETWELVE	512		/* Bytes/sector */
#define	SECDIV_FIVETWELVE	9		/* log2 (SECSIZE) */
#define	SECSIZE_TWOK		2048		/* Bytes/sector */
#define	SECDIV_TWOK		11		/* log2 (SECSIZE) */

#define	SD_UNIT_ATTENTION_RETRY	40
#define	MAX_READ_CAP_RETRY	20

#define	USCSI_DEFAULT_MAXPHYS	0x80000

/*
 * Local definitions, for clarity of code
 */
#define	SD_SCSI_DEVP	(un->un_sd)
#define	SD_DEVINFO	(SD_SCSI_DEVP->sd_dev)
#define	SD_INQUIRY	(SD_SCSI_DEVP->sd_inq)
#define	SD_RQSENSE	(SD_SCSI_DEVP->sd_sense)
#define	SD_MUTEX	(&SD_SCSI_DEVP->sd_mutex)
#define	ROUTE		(&SD_SCSI_DEVP->sd_address)
#define	SECDIV		(un->un_secdiv)
#define	SECSIZE		(un->un_secsize)
#define	SCBP(pkt)	((struct scsi_status *)(pkt)->pkt_scbp)
#define	SCBP_C(pkt)	((*(pkt)->pkt_scbp) & STATUS_MASK)
#define	CDBP(pkt)	((union scsi_cdb *)(pkt)->pkt_cdbp)
#define	NO_PKT_ALLOCATED ((struct buf *)0)
#define	ALLOCATING_PKT	((struct buf *)-1)
#define	BP_PKT(bp)	((struct scsi_pkt *)bp->av_back)
#define	BP_HAS_NO_PKT(bp) (bp->av_back == NO_PKT_ALLOCATED)

#define	STATUS_SCBP_C(statusp)	(*(u_char *)(statusp) & STATUS_MASK)

#define	Tgt(devp)	(devp->sd_address.a_target)
#define	Lun(devp)	(devp->sd_address.a_lun)

#define	ISCD(un)	((un)->un_dp->ctype == CTYPE_CDROM)
#define	ISREMOVABLE(un)	(ISCD(un) || un->un_sd->sd_inq->inq_rmb)
#define	New_state(un, s)	\
	(un)->un_last_state = (un)->un_state,  (un)->un_state = (s)
#define	Restore_state(un)	\
	{ u_char tmp = (un)->un_last_state; New_state((un), tmp); }



/*
 * Structure for recording whether a device is fully open or closed.
 * Assumptions:
 *
 *	+ There are only 8 partitions possible.
 *	+ BLK, MNT, CHR, SWP don't change in some future release!
 *
 */

#define	SDUNIT_SHIFT	3
#define	SDPART_MASK	7
#define	SDUNIT(dev)	(getminor((dev))>>SDUNIT_SHIFT)
#define	SDPART(dev)	(getminor((dev))&SDPART_MASK)

struct ocinfo {
	/*
	* Types BLK, MNT, CHR, SWP,
	* assumed to be types 0-3.
	*/
	u_long  lyr_open[NDKMAP];
	u_char  reg_open[OTYPCNT - 1];
};
#define	OCSIZE  sizeof (struct ocinfo)
union ocmap {
	u_char chkd[OCSIZE];
	struct ocinfo rinfo;
};
#define	lyropen rinfo.lyr_open
#define	regopen rinfo.reg_open

/*
 * Private info for scsi disks.
 *
 * Pointed to by the un_private pointer
 * of one of the SCSI_DEVICE structures.
 */

struct scsi_disk {
	struct scsi_device *un_sd;	/* back pointer to SCSI_DEVICE */
	struct scsi_pkt *un_rqs;	/* ptr to request sense command pkt */
	struct buf *un_rqs_bp;		/* ptr to request sense bp */
	ksema_t	un_rqs_sema;		/* sema to protect req sense pkt */
	struct sd_drivetype *un_dp;	/* drive type table */
	struct	buf *un_sbufp;		/* for use in special io */
	char		*un_srqbufp;	/* sense buffer for special io */
	kcondvar_t	un_sbuf_cv;	/* Conditional Variable on sbufp */
	union	ocmap un_ocmap;		/* open partition map, block && char */
	struct	dk_map un_map[NDKMAP];	/* logical partitions */
	u_long	un_offset[NDKMAP];	/* starting block for partitions */
	struct	dk_geom un_g;		/* disk geometry */
	u_char	un_arq_enabled;		/* auto request sense enabled */
	u_char	un_last_pkt_reason;	/* used for suppressing multiple msgs */
	struct	dk_vtoc un_vtoc;	/* disk Vtoc */
	struct	diskhd un_utab;		/* for queuing */
	struct	kstat *un_stats;	/* for statistics */
	ksema_t	un_semoclose;		/* lock for serializing opens/closes */
	u_int	un_err_blkno;		/* disk block where error occurred */
	long	un_capacity;		/* capacity of drive */
	long	un_lbasize;		/* logical block size */
	int	un_secsize;		/* CD sector size */
	int	un_secdiv;		/* log2 of secsize */
	u_char	un_exclopen;		/* exclusive open bits */
	u_char	un_gvalid;		/* geometry is valid */
	u_char	un_state;		/* current state */
	u_char	un_last_state;		/* last state */
	u_char	un_suspended;		/* True, if driver suspended */
	u_char	un_format_in_progress;	/* disk is formatting currently */
	ulong_t un_timestamp;		/* Time of last device access */
	u_char	un_asciilabel[LEN_DKL_ASCII];	/* Copy of asciilabel */
	short	un_throttle;		/* max outstanding cmds */
	short	un_save_throttle;	/* max outstanding cmds saved */
	short	un_ncmds;		/* number of cmds in transport */
	long	un_tagflags;		/* Pkt Flags for Tagged Queueing  */
	short	un_sbuf_busy;		/* Busy wait flag for the sbuf */
	short	un_resvd_status;	/* Reservation Status */
	kcondvar_t	un_state_cv;	/* Cond Var on mediastate */
	enum dkio_state un_mediastate;	/* current media state */
	enum dkio_state un_specified_mediastate; /* expected state */
	opaque_t	un_mhd_token;	/* scsi watch request */
	int	un_cmd_flags;		/* cache some frequently used values */
	int	un_cmd_stat_size;	/* in make_sd_cmd */
	u_int	un_resvd_timeid;	/* timeout id for resvd recover */
	int	un_reset_throttle_timeid; /* timeout id to reset throttle */
};
/* ALL MEMBERS PROTECTED BY scsi_device::sd_mutex */
/* MEMBERS PROTECTED BY "Set Once": scsi_disk::un_sd */
/* MEMBERS PROTECTED BY "Set Once": scsi_disk::un_cmd_stat_size */
/* MEMBERS PROTECTED BY "Set Once": scsi_disk::un_mhd_token */
/* MEMBERS PROTECTED BY "Set Once": scsi_disk::un_secdiv */
/* MEMBERS PROTECTED BY "Set Once": scsi_disk::un_secsize */
/* MEMBERS PROTECTED BY "Set Once": scsi_disk::un_arq_enabled */
/* MEMBERS PROTECTED BY "Set Once": scsi_disk::un_throttle */
/* MEMBERS PROTECTED BY "Stable data": scsi_disk::un_cmd_flags */
/* MEMBERS PROTECTED BY "Stable data": scsi_disk::un_tagflags */
/* MEMBERS PROTECTED BY "stable data": scsi_disk::un_dp */
/* MEMBERS PROTECTED BY "semaphore": scsi_disk::un_rqs */
/* MEMBERS PROTECTED BY "semaphore": scsi_disk::un_rqs_bp */
/* MEMBERS PROTECTED BY "Set Once": scsi_disk::un_sbufp */
/* MEMBERS PROTECTED BY "Stable Data": scsi_disk::un_sbuf_busy */
/* MEMBERS PROTECTED BY "Stable Data": scsi_disk::un_srqbufp */
/* MEMBERS PROTECTED BY "Perf Group": scsi_disk::un_stats */
/* MEMBERS READABLE WITHOUT LOCK: scsi_disk::un_state */
/* MEMBERS READABLE WITHOUT LOCK: scsi_disk::un_offset */
/* MEMBERS READABLE WITHOUT LOCK: scsi_disk::un_gvalid */
/* MEMBERS READABLE WITHOUT LOCK: scsi_disk::un_suspended */

/* VARIABLES READABLE WITHOUT LOCK: uio:: */
/* VARIABLES PROTECTED BY "being on stack": dk_cinfo:: */
/* VARIABLES PROTECTED BY "being on stack": uio:: */
/* VARIABLES PROTECTED BY "being on stack": buf:: */
/* VARIABLES PROTECTED BY "one per buf": scsi_pkt:: */
/* VARIABLES PROTECTED BY "Unshared data": kstat:: */
/* VARIABLES PROTECTED BY "Unshared data": cdrom_subchnl:: */
/* VARIABLES PROTECTED BY "Unshared data": cdrom_tocentry:: */
/* VARIABLES PROTECTED BY "Unshared data": cdrom_tochdr:: */
/* VARIABLES PROTECTED BY "Unshared data": cdrom_read:: */
/* VARIABLES PROTECTED BY "Unshared data": uscsi_cmd:: */
/* VARIABLES PROTECTED BY "Unshared data": scsi_capacity:: */
/* VARIABLES PROTECTED BY "Unshared data": scsi_cdb:: */
/* VARIABLES PROTECTED BY "Unshared data": scsi_arq_status:: */
/* VARIABLES PROTECTED BY "Unshared data": dk_label:: */
/* VARIABLES PROTECTED BY "Unshared data": dk_map:: */

/*
 * XXX  Double check these fields (there may be race conditions):
 *	- un_offset, un_throttle, un_mediastate
 *	- un_stats
 */

#endif	/* defined(_KERNEL) || defined(_KMEMUSER) */

#define	MAX_THROTTLE	256

/*
 * Disk driver states
 */

#define	SD_STATE_NIL		0
#define	SD_STATE_CLOSED		1
#define	SD_STATE_OPENING	2
#define	SD_STATE_OPEN		3
#define	SD_STATE_SENSING	4
#define	SD_STATE_RWAIT		5
#define	SD_STATE_DETACHING	6
#define	SD_STATE_DUMPING	7
#define	SD_STATE_EJECTED	71
#define	SD_STATE_MODE2		81

/*
 * Error levels
 */

#define	SDERR_ALL		0
#define	SDERR_UNKNOWN		1
#define	SDERR_INFORMATIONAL	2
#define	SDERR_RECOVERED		3
#define	SDERR_RETRYABLE		4
#define	SDERR_FATAL		5

/*
 * Parameters
 */

/*
 * 60 seconds is a *very* reasonable amount of time for most slow CD
 * operations.
 */

#define	SD_IO_TIME	60

/*
 * 2 hours is an excessively reasonable amount of time for format operations.
 */

#define	SD_FMT_TIME	120*60

/*
 * 5 seconds is what we'll wait if we get a Busy Status back
 */

#define	SD_BSY_TIMEOUT		(drv_usectohz(5 * 1000000))

/*
 * 60 seconds is what we will wait for to reset the
 * throttle back to it MAX_THROTTLE.
 */
#define	SD_RESET_THROTTLE_TIMEOUT	60

/*
 * Number of times we'll retry a normal operation.
 *
 * This includes retries due to transport failure
 * (need to distinguish between Target and Transport failure)
 */

#define	SD_RETRY_COUNT		5


/*
 * Maximum number of units we can support
 * (controlled by room in minor device byte)
 * XXX: this is out of date!
 */
#define	SD_MAXUNIT		32

/*
 * Prevent/allow media removal flags
 */
#define	SD_REMOVAL_ALLOW	0
#define	SD_REMOVAL_PREVENT	1

/*
 * Reservation Status's
 */
#define	SD_RELEASE		0x0000
#define	SD_RESERVE		0x0001
#define	SD_TKOWN		0x0002
#define	SD_LOST_RESERVE		0x0004
#define	SD_FAILFAST		0x0080
#define	SD_WANT_RESERVE		0x0100
#define	SD_RESERVATION_CONFLICT	0x0200

/*
 * delay before reclaiming reservation is 6 seconds, in units of micro seconds
 */
#define	SD_REINSTATE_RESV_DELAY	6000000

/*
 * sdintr action codes
 */

#define	COMMAND_DONE		0
#define	COMMAND_DONE_ERROR	1
#define	QUE_COMMAND		2
#define	QUE_SENSE		3
#define	JUST_RETURN		4

/*
 * Drive Types (and characteristics)
 */
#define	VIDMAX 8
#define	PIDMAX 16

struct sd_drivetype {
	char 	*name;		/* for debug purposes */
	char	ctype;		/* controller type */
	char	options;	/* drive options */
	char	vidlen;
	char	vid[VIDMAX];		/* Vendor id + part of Product id */
	char	pidlen;
	char	pid[PIDMAX];
};

/*
 * Commands for sd_start_stop
 */
#define	SD_STOP		((caddr_t)0)
#define	SD_START	((caddr_t)1)
#define	SD_EJECT	((caddr_t)2)

/*
 * Target 'type'.
 */
#define	CTYPE_CDROM		0
#define	CTYPE_MD21		1
#define	CTYPE_CCS		2

/*
 * Options
 */
#define	SD_NODISC	0x0001	/* has problem w/ disconnect-reconnect */
#define	SD_NOPARITY	0x0002	/* target does not generate parity */
#define	SD_MULTICMD	0x0004	/* target supports SCSI-2 multiple commands */
#define	SD_EIOENABLE	0x0008	/* Enable retruning EIO on media change	*/
#define	SD_QUEUEING	0x0010	/* Enable Command Queuing to Host Adapter */

#ifndef	LOG_EMERG
#define	LOG_WARNING	CE_NOTE
#define	LOG_NOTICE	CE_NOTE
#define	LOG_CRIT	CE_WARN
#define	LOG_ERR		CE_WARN
#define	LOG_INFO	CE_NOTE
#define	log	cmn_err
#endif

/*
 * Some internal error codes for driver functions.
 */
#define	SD_EACCES	1

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SCSI_TARGETS_SDDEF_H */
