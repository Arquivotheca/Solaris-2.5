/*
 * Copyright (c) 1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)sd.c 1.173	95/07/27 SMI"

/*
 * SCSI disk driver
 * Tagged Queuing Support
 */

/*
 * Includes, Declarations and Local Data
 */


#include <sys/scsi/scsi.h>
#include <sys/dkbad.h>
#include <sys/dklabel.h>
#include <sys/dkio.h>
#include <sys/cdio.h>
#include <sys/mhd.h>
#include <sys/vtoc.h>
#include <sys/scsi/targets/sddef.h>
#include <sys/scsi/targets/sdwatch.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/kstat.h>
#include <sys/vtrace.h>
#include <sys/aio_req.h>

/*
 * Global Error Levels for Error Reporting
 */
int sd_error_level	= SCSI_ERR_RETRYABLE;

/*
 * Local Static Data
 */

static int sd_io_time		= SD_IO_TIME;
static int sd_retry_count	= SD_RETRY_COUNT;
static int sd_rot_delay		= 4;	/* default 4ms Rotation delay */
static int sd_check_media_time	= 3000000;	/* 3 Second State Check */
static int sd_failfast_enable	= 1;	/* Really Halt */
static int sd_max_throttle	= MAX_THROTTLE;
static int sd_reset_throttle_timeout = SD_RESET_THROTTLE_TIMEOUT;
static int sd_retry_on_reservation_conflict = 1;
static int reinstate_resv_delay = SD_REINSTATE_RESV_DELAY;

/*
 * Local Function Prototypes
 */

static int sdopen(dev_t *dev_p, int flag, int otyp, cred_t *cred_p);
static int sdclose(dev_t dev, int flag, int otyp, cred_t *cred_p);
static int sdstrategy(struct buf *bp);
static int sddump(dev_t dev, caddr_t addr, daddr_t blkno, int nblk);
static int sdioctl(dev_t, int, int, int, cred_t *, int *);
static int sdread(dev_t dev, struct uio *uio, cred_t *cred_p);
static int sdwrite(dev_t dev, struct uio *uio, cred_t *cred_p);
static int sd_prop_op(dev_t, dev_info_t *, ddi_prop_op_t, int,
    char *, caddr_t, int *);
static int sdaread(dev_t dev, struct aio_req *aio, cred_t *cred_p);
static int sdawrite(dev_t dev, struct aio_req *aio, cred_t *cred_p);


static int sd_doattach(dev_info_t *devi, int (*f)());
static void sd_validate_geometry(struct scsi_disk *un, int (*f)());
static int sd_winchester_exists(struct scsi_disk *un,
	struct scsi_pkt *rqpkt, int (*canwait)());
static int sd_read_capacity(struct scsi_disk *un,
	struct scsi_capacity *cptr);
static void sd_uselabel(struct scsi_disk *un, struct dk_label *l);

static int sd_clear_cont_alleg(struct scsi_disk *un, struct scsi_pkt *pkt);
static int sd_scsi_poll(struct scsi_pkt *pkt);
static void sdmin(struct buf *bp);
static void sduscsimin(struct buf *bp);

static int sdioctl_cmd(dev_t, struct uscsi_cmd *,
	enum uio_seg, enum uio_seg, enum uio_seg);

static void sdstart(struct scsi_disk *un);
static int sdrunout(caddr_t arg);
static void sddone(struct scsi_disk *un, struct buf *bp);
static void make_sd_cmd(struct scsi_disk *un, struct buf *bp, int (*f)());

static int sd_check_wp(dev_t dev);
static int sd_unit_ready(dev_t dev);
static int sd_lock_unlock(dev_t dev, int flag);
static void sdrestart(caddr_t arg);
static void sd_reset_throttle(caddr_t arg);
static void sd_handle_tran_busy(struct buf *bp, struct diskhd *dp,
				struct scsi_disk *un);
static int sd_mhd_watch_cb(caddr_t arg, struct scsi_watch_result *resultp);
static void sd_mhd_watch_incomplete(struct scsi_disk *un, struct scsi_pkt *pkt);
static void sd_mhd_resvd_recover(caddr_t arg);
static void sd_mhd_reset_notify_cb(caddr_t arg);
static void sd_restart_unit(register struct scsi_disk *un);
static void sd_restart_unit_callback(struct scsi_pkt *pkt);
static int sd_start_stop(dev_t dev, caddr_t  data);
static int sd_eject(dev_t dev);
static int sd_take_ownership(dev_t dev, struct mhioctkown *p);
static int sd_reserve_release(dev_t dev, int cmd);
static int sd_check_media(dev_t dev, enum dkio_state state);
static int sd_check_mhd(dev_t dev, int interval);

static void sdintr(struct scsi_pkt *pkt);
static int sd_handle_incomplete(struct scsi_disk *un, struct buf *bp);
static int sd_handle_sense(struct scsi_disk *un, struct buf *bp);
static int sd_handle_autosense(struct scsi_disk *un, struct buf *bp);
static int sd_decode_sense(struct scsi_disk *un, struct buf *bp,
    struct scsi_status *statusp, u_long state, u_char resid);
static int sd_handle_resv_conflict(struct scsi_disk *un, struct buf *bp);
static int sd_check_error(struct scsi_disk *un, struct buf *bp);
static int sd_handle_ua(struct scsi_disk *un);
static int sd_handle_mchange(struct scsi_disk *un);
static void sd_ejected(struct scsi_disk *un);
static void sd_not_ready(struct scsi_disk *un, struct scsi_pkt *pkt);
static void sd_offline(struct scsi_disk *un, int bechatty);
static char *sd_sname(u_char status);

/*
 * CDROM specific stuff
 */
static int sr_sector_mode(dev_t dev, int mode);

static int sr_pause_resume(dev_t dev, caddr_t data);
static int sr_play_msf(dev_t dev, caddr_t data, int flag);
static int sr_play_trkind(dev_t dev, caddr_t data, int flag);
static int sr_volume_ctrl(dev_t dev, caddr_t  data, int flag);
static int sr_read_subchannel(dev_t dev, caddr_t data, int flag);
static int sr_read_mode2(dev_t dev, caddr_t data, int flag);
static int sr_read_mode1(dev_t dev, caddr_t data, int flag);
static int sr_read_tochdr(dev_t dev, caddr_t data, int flag);
static int sr_read_tocentry(dev_t dev, caddr_t data, int flag);
#ifdef CDROMREADOFFSET
static int sr_read_sony_session_offset(dev_t dev, caddr_t data, int flag);
#endif
static int sr_check_blk_size(dev_t dev);
static int sr_mode_sense(dev_t dev, int page, char *page_data, int page_size);
static int sr_mode_select(dev_t dev, char *page_data, int page_size);
static int sr_change_blkmode(dev_t dev, int cmd, int data, int flag);
static int sr_change_speed(dev_t dev, int cmd, int data, int flag);
static int sr_read_cdda(dev_t dev, caddr_t data, int flag);
static int sr_read_cdxa(dev_t dev, caddr_t data, int flag);
static int sr_read_all_subcodes(dev_t dev, caddr_t data, int flag);

/*
 * Vtoc Control
 */
static void sd_build_user_vtoc(struct scsi_disk *un, struct vtoc *vtoc);
static int sd_build_label_vtoc(struct scsi_disk *un, struct vtoc *vtoc);
static int sd_write_label(dev_t dev);

/*
 * Error and Logging Functions
 */
static void clean_print(dev_info_t *dev, char *label, u_int level,
	char *title, char *data, int len);
static void inq_fill(char *p, int l, char *s);


/*
 * Configuration Routines
 */
static int sdinfo(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg,
		void **result);
static int sdidentify(dev_info_t *devi);
static int sdprobe(dev_info_t *devi);
static int sdattach(dev_info_t *devi, ddi_attach_cmd_t cmd);
static int sddetach(dev_info_t *devi, ddi_detach_cmd_t cmd);
static int sdpower(dev_info_t *devi, int component, int level);

extern struct scsi_key_strings scsi_cmds[];

extern void scsi_watch_suspend();
extern void scsi_watch_resume();
/*
 * scsi watch routines
 */

void scsi_watch_init();
void scsi_watch_fini();
void scsi_watch_request_terminate(opaque_t token);
opaque_t scsi_watch_request_submit(struct scsi_device *devp,
    int interval, int sense_length, int (*callback)(), caddr_t cb_arg);

/*
 *
 * Drive Type Table
 *
 * The only use for this table is to note which *special*
 * drives have problems that we should watch out for, else
 * generic CCS should suffice.
 *
 */

/* FUNCTIONS CALLED THROUGH POINTER scsi_pkt::pkt_comp:
	"sd.c":sdintr */
/* FUNCTIONS CALLED THROUGH POINTER scsi_pkt::pkt_comp:
	"sd.c":sd_mhd_watch_cb */
/* FUNCTIONS CALLED THROUGH POINTER scsi_pkt::pkt_comp:
	"sd.c":sd_media_watch_cb */

static struct sd_drivetype sdspecial[] = {
/* Emulex MD21 */
	{ "DASD", CTYPE_MD21, 0, 8, "EMULEX  ", 4, "MD21"},
};
#define	SDNSPECIAL	(sizeof (sdspecial) / (sizeof (sdspecial[0])))

static void *sd_state;
static int sd_max_instance;
static char *sd_label = "sd";

static char *diskokay = "disk okay\n";

#if DEBUG || lint
#define	SDDEBUG
#endif

/*
 * Debugging macros
 */
#ifdef	SDDEBUG
static int sddebug = 0;
#define	DEBUGGING	(sddebug > 1)
#define	SD_DEBUG	if (sddebug == 1) scsi_log
#define	SD_DEBUG2	if (sddebug > 1) scsi_log
#else	/* SDDEBUG */
#define	sddebug		(0)
#define	DEBUGGING	(0)
#define	SD_DEBUG	if (0) scsi_log
#define	SD_DEBUG2	if (0) scsi_log
#endif

/*
 * we use pkt_private area for storing bp and retry_count
 */
struct sd_pkt_private {
	struct buf	*sdpp_bp;
	int		 sdpp_retry_count;
};
/* VARIABLES PROTECTED BY "Unshared data": sd_pkt_private:: */

#define	PP_LEN	(sizeof (struct sd_pkt_private))

#define	PKT_SET_BP(pkt, bp)	\
	((struct sd_pkt_private *)pkt->pkt_private)->sdpp_bp = bp
#define	PKT_GET_BP(pkt) \
	(((struct sd_pkt_private *)pkt->pkt_private)->sdpp_bp)


#define	PKT_SET_RETRY_CNT(pkt, n) \
	((struct sd_pkt_private *)pkt->pkt_private)->sdpp_retry_count = n

#define	PKT_GET_RETRY_CNT(pkt) \
	(((struct sd_pkt_private *)pkt->pkt_private)->sdpp_retry_count)

#define	PKT_INCR_RETRY_CNT(pkt, n) \
	((struct sd_pkt_private *)pkt->pkt_private)->sdpp_retry_count += n

static struct driver_minor_data {
	char	*name;
	int	minor;
	int	type;
} sd_minor_data[] = {
	{"a", 0, S_IFBLK},
	{"b", 1, S_IFBLK},
	{"c", 2, S_IFBLK},
	{"d", 3, S_IFBLK},
	{"e", 4, S_IFBLK},
	{"f", 5, S_IFBLK},
	{"g", 6, S_IFBLK},
	{"h", 7, S_IFBLK},
	{"a,raw", 0, S_IFCHR},
	{"b,raw", 1, S_IFCHR},
	{"c,raw", 2, S_IFCHR},
	{"d,raw", 3, S_IFCHR},
	{"e,raw", 4, S_IFCHR},
	{"f,raw", 5, S_IFCHR},
	{"g,raw", 6, S_IFCHR},
	{"h,raw", 7, S_IFCHR},
	{0}
};

/*
 * Urk!
 */
#define	SET_BP_ERROR(bp, err)	\
	(bp)->b_oerror = (err), bioerror(bp, err);

#define	IOSP	KSTAT_IO_PTR(un->un_stats)

#define	GET_SOFT_STATE(dev)						\
	register struct scsi_disk *un;					\
	register int instance, part;					\
	register u_long minor = getminor(dev);				\
									\
	part = minor & SDPART_MASK;					\
	instance = minor >> SDUNIT_SHIFT;				\
	if ((un = ddi_get_soft_state(sd_state, instance)) == NULL)	\
		return (ENXIO);

/*
 * XXX this is not ddi compliant but it helps performance
 */
#define	makecom_g0	MAKECOM_G0
#define	makecom_g1	MAKECOM_G1

/*
 * Configuration Data
 */

/*
 * Device driver ops vector
 */

static struct cb_ops sd_cb_ops = {

	sdopen,			/* open */
	sdclose,		/* close */
	sdstrategy,		/* strategy */
	nodev,			/* print */
	sddump,			/* dump */
	sdread,			/* read */
	sdwrite,		/* write */
	sdioctl,		/* ioctl */
	nodev,			/* devmap */
	nodev,			/* mmap */
	nodev,			/* segmap */
	nochpoll,		/* poll */
	sd_prop_op,		/* cb_prop_op */
	0,			/* streamtab  */
	D_64BIT | D_MP | D_NEW,	/* Driver compatibility flag */
	CB_REV,			/* cb_rev */
	sdaread, 		/* async I/O read entry point */
	sdawrite		/* async I/O write entry point */
};

static struct dev_ops sd_ops = {
	DEVO_REV,		/* devo_rev, */
	0,			/* refcnt  */
	sdinfo,			/* info */
	sdidentify,		/* identify */
	sdprobe,		/* probe */
	sdattach,		/* attach */
	sddetach,		/* detach */
	nodev,			/* reset */
	&sd_cb_ops,		/* driver operations */
	(struct bus_ops *)0,	/* bus operations */
	sdpower			/* power */
};


/*
 * This is the loadable module wrapper.
 */
#include <sys/modctl.h>

static struct modldrv modldrv = {
	&mod_driverops,		/* Type of module. This one is a driver */
	"SCSI Direct Access Disk Driver",	/* Name of the module. */
	&sd_ops,	/* driver ops */
};



static struct modlinkage modlinkage = {
	MODREV_1, &modldrv, NULL
};

/*
 * the sd_attach_mutex only protects sd_max_instance in multi-threaded
 * attach situations
 */
static kmutex_t sd_attach_mutex;

char _depends_on[] = "misc/scsi";

int
_init(void)
{
	int e;

	if ((e = ddi_soft_state_init(&sd_state, sizeof (struct scsi_disk),
	    SD_MAXUNIT)) != 0)
		return (e);

	mutex_init(&sd_attach_mutex, "sd_attach_mutex", MUTEX_DRIVER, NULL);
	e = mod_install(&modlinkage);
	if (e != 0) {
		mutex_destroy(&sd_attach_mutex);
		ddi_soft_state_fini(&sd_state);
		return (e);
	}

	scsi_watch_init();

	return (e);
}

int
_fini(void)
{
	int e;

	if ((e = mod_remove(&modlinkage)) != 0)
		return (e);

	ddi_soft_state_fini(&sd_state);
	mutex_destroy(&sd_attach_mutex);

	scsi_watch_fini();

	return (e);
}

int
_info(struct modinfo *modinfop)
{

	return (mod_info(&modlinkage, modinfop));
}


/*
 * Autoconfiguration Routines
 */

static int
sdidentify(dev_info_t *dev)
{
	if (strcmp(ddi_get_name(dev), sd_label) == 0) {
		return (DDI_IDENTIFIED);
	} else {
		return (DDI_NOT_IDENTIFIED);
	}
}

static int
sdprobe(dev_info_t *devi)
{
	register struct scsi_device *devp;
	register int rval = DDI_PROBE_PARTIAL;
	int instance;

	devp = (struct scsi_device *)ddi_get_driver_private(devi);
	instance = ddi_get_instance(devi);

	/*
	 * Keep a count of how many disks (ie. highest instance no) we have
	 * XXX currently not used but maybe useful later again
	 */
	mutex_enter(&sd_attach_mutex);
	if (instance > sd_max_instance)
		sd_max_instance = instance;
	mutex_exit(&sd_attach_mutex);

	SD_DEBUG2(devp->sd_dev, sd_label, SCSI_DEBUG,
		    "sdprobe:\n");

	if (ddi_get_soft_state(sd_state, instance) != NULL)
		return (DDI_PROBE_PARTIAL);

	/*
	 * Turn around and call utility probe routine
	 * to see whether we actually have a disk at
	 * this SCSI nexus.
	 */

	SD_DEBUG2(devp->sd_dev, sd_label, SCSI_DEBUG,
	    "sdprobe: %x\n", scsi_probe(devp, NULL_FUNC));

	switch (scsi_probe(devp, NULL_FUNC)) {
	default:
	case SCSIPROBE_NORESP:
	case SCSIPROBE_NONCCS:
	case SCSIPROBE_NOMEM:
	case SCSIPROBE_FAILURE:
	case SCSIPROBE_BUSY:
		break;

	case SCSIPROBE_EXISTS:
		switch (devp->sd_inq->inq_dtype) {
		case DTYPE_DIRECT:
			SD_DEBUG2(devp->sd_dev, sd_label, SCSI_DEBUG,
			    "sdprobe: DTYPE_DIRECT\n");
			rval = DDI_PROBE_SUCCESS;
			break;
		case DTYPE_RODIRECT:
			SD_DEBUG2(devp->sd_dev, sd_label, SCSI_DEBUG,
			    "sdprobe: DTYPE_RODIRECT\n");
			rval = DDI_PROBE_SUCCESS;
			break;
		case DTYPE_NOTPRESENT:
		default:
			rval = DDI_PROBE_FAILURE;
			SD_DEBUG2(devp->sd_dev, sd_label, SCSI_DEBUG,
			    "sdprobe: DTYPE_FAILURE\n");
			break;
		}
	}
	scsi_unprobe(devp);

	return (rval);
}

/*ARGSUSED*/
static int
sdattach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
/* NO OTHER THREADS ARE RUNNING */
	int instance;
	register struct scsi_device *devp;
	struct driver_minor_data *dmdp;
	struct scsi_disk *un;
	char *node_type;
	char name[48];
	int norm_pwr;
	int rval;

	devp = (struct scsi_device *)ddi_get_driver_private(devi);
	instance = ddi_get_instance(devi);

	switch (cmd) {
		case DDI_ATTACH:
			break;

		case DDI_RESUME:
			if (!(un = ddi_get_soft_state(sd_state, instance)))
				return (DDI_FAILURE);
			mutex_enter(SD_MUTEX);
			if (!un->un_suspended) {
				mutex_exit(SD_MUTEX);
				return (DDI_SUCCESS);
			}
			un->un_suspended = 0;
			un->un_throttle = un->un_save_throttle;

			/*
			 * Resume the scsi_watch_thread
			 */
			scsi_watch_resume();
			mutex_exit(SD_MUTEX);
			return (DDI_SUCCESS);

		default:
			return (DDI_FAILURE);
	}

	if (sd_doattach(devi, NULL_FUNC) == DDI_FAILURE)
		return (DDI_FAILURE);

	un = (struct scsi_disk *)ddi_get_soft_state(sd_state, instance);
	devp->sd_private = (opaque_t)un;

	for (dmdp = sd_minor_data; dmdp->name != NULL; dmdp++) {
		switch (un->un_dp->ctype) {
		case CTYPE_CDROM:
			node_type = DDI_NT_CD_CHAN;
			break;
		default:
			node_type = DDI_NT_BLOCK_CHAN;
			break;
		}
		sprintf(name, "%s", dmdp->name);
		if (ddi_create_minor_node(devi, name, dmdp->type,
		    (instance << SDUNIT_SHIFT) | dmdp->minor,
		    node_type, NULL) == DDI_FAILURE) {
			ddi_remove_minor_node(devi, NULL);
			/*
			 * Free Resouces allocated in sd_doattach
			 */
			if (un) {
				register struct sd_drivetype *dp;

				sema_destroy(&un->un_semoclose);
				sema_destroy(&un->un_rqs_sema);
				cv_destroy(&un->un_sbuf_cv);
				cv_destroy(&un->un_state_cv);
				freerbuf(un->un_sbufp);

				scsi_destroy_pkt(un->un_rqs);
				scsi_free_consistent_buf(un->un_rqs_bp);
				for (dp = sdspecial;
				    dp < &sdspecial[SDNSPECIAL]; dp++) {
					/*
					 * It turns out that order
					 * is important for strcmp()!
					 */
					if (bcmp(devp->sd_inq->inq_vid,
					    dp->vid,
					    strlen(dp->vid)) == 0 &&
					    bcmp(devp->sd_inq->inq_pid,
						dp->pid,
						strlen(dp->pid)) == 0) {
						un->un_dp = 0;
						break;
					}
				}
				if (un->un_dp != NULL) {
					kmem_free((caddr_t)un->un_dp,
					    sizeof (*un->un_dp));
				}
				ddi_soft_state_free(sd_state, instance);
				devp->sd_private = (opaque_t)0;
				devp->sd_sense =
					(struct scsi_extended_sense *)0;
			}
			ddi_prop_remove_all(devi);
			return (DDI_FAILURE);
		}
	}

	/*
	 * Add a zero-length attribute to tell the world we support
	 * kernel ioctls (for layered drivers)
	 */
	(void) ddi_prop_create(DDI_DEV_T_NONE, devi, DDI_PROP_CANSLEEP,
	    DDI_KERNEL_IOCTL, NULL, 0);

	/* Add power management timestamp */
	un->un_timestamp = 0;
	drv_getparm(TIME, &un->un_timestamp);
	(void) ddi_prop_create(DDI_DEV_T_NONE, devi, DDI_PROP_CANSLEEP,
	    "pm_timestamp", (caddr_t)&un->un_timestamp,
	    sizeof (un->un_timestamp));
	norm_pwr = 1;
	(void) ddi_prop_create(DDI_DEV_T_NONE, devi, DDI_PROP_CANSLEEP,
	    "pm_norm_pwr", (caddr_t)&norm_pwr, sizeof (norm_pwr));
	un->un_suspended = 0;

	ddi_report_dev(devi);
	mutex_enter(SD_MUTEX);

	un->un_gvalid = 0;
	sd_validate_geometry(un, NULL_FUNC);

	/*
	 * If this is a removable media device,
	 * invalidate the geometry in order to
	 * force the geometry to be re-examined
	 * at open time.
	 */
	if (ISREMOVABLE(un)) {
		un->un_gvalid = 0;
		un->un_mediastate = DKIO_NONE;
	}
	mutex_exit(SD_MUTEX);

	/*
	 * This property is set to 0 by HA software to avoid retries on a
	 * reserved disk. The first one is the preferred name (1189689)
	 */
	sd_retry_on_reservation_conflict = ddi_getprop(DDI_DEV_T_ANY, devi,
	    DDI_PROP_DONTPASS, "retry-on-reservation-conflict",
		sd_retry_on_reservation_conflict);
	if (sd_retry_on_reservation_conflict != 0) {
	    sd_retry_on_reservation_conflict = ddi_getprop(DDI_DEV_T_ANY, devi,
		DDI_PROP_DONTPASS, "sd_retry_on_reservation_conflict",
		sd_retry_on_reservation_conflict);
	}

	if ((rval = ddi_getprop(DDI_DEV_T_ANY, devi, 0,
		"qfull-retries", -1)) != -1) {
		(void) scsi_ifsetcap(ROUTE, "qfull-retries", rval, 1);
	}
	if ((rval = ddi_getprop(DDI_DEV_T_ANY, devi, 0,
		"qfull-retry-interval", -1)) != -1) {
		(void) scsi_ifsetcap(ROUTE, "qfull-retry-interval", rval, 1);
	}

	return (DDI_SUCCESS);
/* OTHER THREADS ARE RUNNING */
}

static int
sddetach(dev_info_t *devi, ddi_detach_cmd_t cmd)
{
	int instance;
	struct scsi_disk *un;
	register struct diskhd *dp;

	instance = ddi_get_instance(devi);

	if (!(un = ddi_get_soft_state(sd_state, instance)))
		return (DDI_FAILURE);

	switch (cmd) {
		case DDI_DETACH:
			return (DDI_FAILURE);

		case DDI_SUSPEND:
			mutex_enter(SD_MUTEX);
			if (un->un_suspended) {
				mutex_exit(SD_MUTEX);
				return (DDI_SUCCESS);
			}
			un->un_suspended = 1;
			un->un_throttle = 0;
			if (un->un_ncmds > 0) {
				mutex_exit(SD_MUTEX);
				if (scsi_reset(ROUTE, RESET_ALL) == 0) {
					mutex_enter(SD_MUTEX);
					un->un_suspended = 0;
					un->un_throttle = un->un_save_throttle;
					mutex_exit(SD_MUTEX);
					return (DDI_FAILURE);
				}
			} else {
				mutex_exit(SD_MUTEX);
			}

			/*
			 * Suspend the scsi_watch_thread
			 */
			scsi_watch_suspend();

			return (DDI_SUCCESS);

		case DDI_PM_SUSPEND:
			mutex_enter(SD_MUTEX);
			dp = &un->un_utab;
			if (un->un_suspended || dp->b_forw || dp->b_actf ||
			    un->un_ncmds) {
				mutex_exit(SD_MUTEX);
				return (DDI_FAILURE);
			}
			un->un_suspended = 1;
			mutex_exit(SD_MUTEX);
			return (DDI_SUCCESS);
	}
	return (DDI_FAILURE);
}

static int
sdpower(dev_info_t *devi, int component, int level)
{
	int		instance;
	struct scsi_disk *un;
	dev_t		dev;

	instance = ddi_get_instance(devi);

	if (!(un = ddi_get_soft_state(sd_state, instance)) ||
	    (0 > level) || (level > 1) || component != 0)
		return (DDI_FAILURE);
	ASSERT(un->un_suspended);
	dev = makedevice(ddi_name_to_major("sd"), instance<<3);
	if (!ISREMOVABLE(un)) {
		/*
		 * If the first command gets a check condition and
		 * returns an error, we do it again.  The check condition
		 * reults from a SCSI bus reset or power down
		 */
		if (sd_start_stop(dev, (level ? SD_START : SD_STOP))) {
			(void) sd_start_stop(dev, (level ? SD_START : SD_STOP));
		}
	}
	return (DDI_SUCCESS);
}

sd_doattach(dev_info_t *devi, int (*canwait)())
{
	struct scsi_device *devp;
	auto struct scsi_capacity cbuf;
	register struct scsi_pkt *rqpkt = (struct scsi_pkt *)0;
	register struct scsi_disk *un = (struct scsi_disk *)0;
	register struct sd_drivetype *dp;
	register int rval = DDI_FAILURE;
	int made_dp = 0;
	int instance;
	int km_flags = (canwait != NULL_FUNC)? KM_SLEEP : KM_NOSLEEP;
	struct buf *bp;
	char *eliteII = "ST42400N";

	devp = (struct scsi_device *)ddi_get_driver_private(devi);

	/*
	 * Call the routine scsi_probe to do some of the dirty work.
	 * If the INQUIRY command succeeds, the field sd_inq in the
	 * device structure will be filled in. The sd_sense structure
	 * will also be allocated.
	 */

	switch (scsi_probe(devp, canwait)) {
	default:
	case SCSIPROBE_NORESP:
	case SCSIPROBE_NONCCS:
	case SCSIPROBE_NOMEM:
	case SCSIPROBE_FAILURE:
	case SCSIPROBE_BUSY:
		return (rval);

	case SCSIPROBE_EXISTS:
		switch (devp->sd_inq->inq_dtype) {
		case DTYPE_DIRECT:
			SD_DEBUG2(devp->sd_dev, sd_label, SCSI_DEBUG,
			    "sdprobe: DTYPE_DIRECT\n");
			break;
		case DTYPE_RODIRECT:
			SD_DEBUG2(devp->sd_dev, sd_label, SCSI_DEBUG,
			    "sdprobe: DTYPE_RODIRECT\n");
			break;
		case DTYPE_NOTPRESENT:
		default:
			return (rval);
		}
	}


	/*
	 * Allocate a request sense packet.
	 */
	bp = scsi_alloc_consistent_buf(&devp->sd_address, (struct buf *)NULL,
	    SENSE_LENGTH, B_READ, canwait, NULL);
	if (!bp) {
		return (rval);
	}

	rqpkt = scsi_init_pkt(&devp->sd_address,
	    (struct scsi_pkt *)NULL, bp, CDB_GROUP0, 1, PP_LEN,
	    PKT_CONSISTENT, canwait, NULL);
	if (!rqpkt) {
		goto error;
	}
	devp->sd_sense = (struct scsi_extended_sense *)bp->b_un.b_addr;


	makecom_g0(rqpkt, devp, FLAG_NOPARITY,
	    SCMD_REQUEST_SENSE, 0, SENSE_LENGTH);

	/*
	 * The actual unit is present.
	 *
	 * The attach routine will check validity of label
	 * (and print out whether it is there).
	 *
	 * Now is the time to fill in the rest of our info..
	 */
	instance = ddi_get_instance(devp->sd_dev);

	if (ddi_soft_state_zalloc(sd_state, instance) != DDI_SUCCESS)
		goto error;

	un = ddi_get_soft_state(sd_state, instance);

	un->un_sbufp = getrbuf(km_flags);
	if (un->un_sbufp == (struct buf *)NULL) {
		goto error;
	}

	rqpkt->pkt_comp = sdintr;
	rqpkt->pkt_time = sd_io_time;

	rqpkt->pkt_flags |= (FLAG_SENSING | FLAG_HEAD | FLAG_NODISCON);

	un->un_rqs = rqpkt;
	un->un_sd = devp;
	un->un_rqs_bp = bp;

	sema_init(&un->un_semoclose, 1, "sd_o", SEMA_DRIVER, NULL);
	sema_init(&un->un_rqs_sema, 1, "sd_rqs", SEMA_DRIVER, NULL);
	cv_init(&un->un_sbuf_cv, "sd_sbuf_cv", CV_DRIVER, NULL);
	cv_init(&un->un_state_cv, "sd_state_cv", CV_DRIVER, NULL);

	if (un->un_dp == 0) {
		/*
		 * Assume CCS drive, assume parity, but call
		 * it a CDROM if it is a RODIRECT device.
		 */
		un->un_dp = (struct sd_drivetype *)
		    kmem_zalloc(sizeof (struct sd_drivetype), km_flags);
		if (!un->un_dp) {
			goto error;
		}
		made_dp = 1;
		bcopy(devp->sd_inq->inq_vid, un->un_dp->vid, VIDMAX);
		bcopy(devp->sd_inq->inq_pid, un->un_dp->pid, PIDMAX);
		un->un_dp->pidlen = PIDMAX;
		if (devp->sd_inq->inq_dtype == DTYPE_RODIRECT)
			un->un_dp->ctype = CTYPE_CDROM;
		else
			un->un_dp->ctype = CTYPE_CCS;
		un->un_dp->vidlen = VIDMAX;
		un->un_dp->name = "CCS";
		un->un_dp->options = 0;
	}

	/*
	 * Set a default block size of 512. Will change to actual
	 * block size for CD-ROMs in sdopen once sd_unit_ready() is
	 * done.
	 */

	un->un_secsize = SECSIZE_FIVETWELVE;

	/*
	 * If the device is not a removable media device, make sure that
	 * it can be started (if possible) with the START command, and
	 * attempt to read it's capacity too (if possible).
	 */

	cbuf.capacity = (u_long)-1;
	cbuf.lbasize = (u_long)-1;
	if (devp->sd_inq->inq_dtype == DTYPE_DIRECT && !ISREMOVABLE(un)) {
		switch (sd_winchester_exists(un, rqpkt, canwait)) {
		case 0:
			/*
			 * try to read capacity, do not get upset if it fails.
			 */
			(void) sd_read_capacity(un, &cbuf);
			break;
		case SD_EACCES:	/* disk is reserved */
			break;
		default:
			goto error;
		}
	}

	/*
	 * Look through our list of special drives to see whether
	 * this drive is known to have special problems.
	 */

	for (dp = sdspecial; dp < &sdspecial[SDNSPECIAL]; dp++) {
		/*
		 * It turns out that order is important for strcmp()!
		 */
#define	ISVID	sd_inq->inq_vid
#define	ISPID	sd_inq->inq_pid
		if (bcmp(devp->ISVID, dp->vid, strlen(dp->vid)) == 0 &&
		    bcmp(devp->ISPID, dp->pid, strlen(dp->pid)) == 0) {
			if (made_dp) {
				kmem_free((caddr_t)un->un_dp,
				    sizeof (*un->un_dp));
			}
			un->un_dp = dp;
			break;
		}
	}

	SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG,
	    "sdprobe: drive selected\n");

	if ((un->un_dp->options & SD_NOPARITY) == 0)
		rqpkt->pkt_flags &= ~FLAG_NOPARITY;
	if (un->un_dp->options & SD_NODISC)
		rqpkt->pkt_flags |= FLAG_NODISCON;

	if (cbuf.capacity != 0) {
		un->un_capacity = cbuf.capacity;
		un->un_lbasize = cbuf.lbasize;
	} else if (!ISCD(un)) {
		un->un_capacity = 0;
		un->un_lbasize = DEV_BSIZE;
	} else if (ISCD(un)) {
		un->un_capacity = -1;
		un->un_lbasize = -1;
	}

	/*
	 * enable autorequest sense; keep the rq packet around in case
	 * the autorequest sense fails because of a busy condition
	 */
	un->un_arq_enabled =
	    ((scsi_ifsetcap(ROUTE, "auto-rqsense", 1, 1) == 1)? 1: 0);

	SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG,
	    "auto request sense %s\n",
	    (un->un_arq_enabled ? "enabled" : "disabled"));


	/*
	 * If SCSI-2 tagged queueing is supported by the disk drive and
	 * by the host adapter then we will enable it.
	 */
	un->un_tagflags = 0;
	if ((devp->sd_inq->inq_rdf == RDF_SCSI2) &&
	    (devp->sd_inq->inq_cmdque) && un->un_arq_enabled) {
		if (scsi_ifsetcap(ROUTE, "tagged-qing", 1, 1) == 1) {
			un->un_tagflags = FLAG_STAG;
				SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG,
				    "tagged queueing enabled\n");

			un->un_save_throttle = un->un_throttle =
			    sd_max_throttle;
			if (bcmp(SD_INQUIRY->inq_pid, eliteII,
			    strlen(eliteII)) == 0) {
				un->un_save_throttle = 10;
			}
		} else if (scsi_ifgetcap(ROUTE, "untagged-qing", 0) == 1) {
			un->un_dp->options |= SD_QUEUEING;
			un->un_save_throttle = un->un_throttle = 3;
		} else {
			un->un_dp->options &= ~SD_QUEUEING;
			un->un_save_throttle = un->un_throttle = 1;
		}
	/*
	 * Does the Host Adapter Support Internal Queueing?
	 */
	} else if ((scsi_ifgetcap(ROUTE, "untagged-qing", 0) == 1) &&
			un->un_arq_enabled) {
		un->un_dp->options |= SD_QUEUEING;
		un->un_save_throttle = un->un_throttle = 3;
	} else {
		un->un_dp->options &= ~SD_QUEUEING;
		un->un_save_throttle = un->un_throttle = 1;
	}

	/*
	 * Setup or tear down default wide operations for
	 * disks
	 */
	if ((devp->sd_inq->inq_rdf == RDF_SCSI2) &&
	    (devp->sd_inq->inq_wbus16 || devp->sd_inq->inq_wbus32)) {
		if (scsi_ifsetcap(ROUTE, "wide-xfer", 1, 1) == 1) {
			SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG,
				"Wide Transfer enabled\n");
		}
	} else {
		if (scsi_ifsetcap(ROUTE, "wide-xfer", 0, 1) == 1) {
			SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG,
				"Wide Transfer disabled\n");
		}
	}

	return (DDI_SUCCESS);

error:
	if (bp) {
		scsi_free_consistent_buf(un->un_rqs_bp);
	}
	if (rqpkt) {
		scsi_destroy_pkt(un->un_rqs);
		devp->sd_sense = (struct scsi_extended_sense *)0;
	}

	if (un) {
		sema_destroy(&un->un_semoclose);
		sema_destroy(&un->un_rqs_sema);
		cv_destroy(&un->un_sbuf_cv);
		cv_destroy(&un->un_state_cv);
		if (un->un_sbufp) {
			freerbuf(un->un_sbufp);
		}
		if (made_dp) {
			kmem_free((caddr_t)un->un_dp, sizeof (*un->un_dp));
		}
		ddi_soft_state_free(sd_state, instance);
		devp->sd_private = (opaque_t)0;
	}
	return (rval);
}

static int
sd_winchester_exists(struct scsi_disk *un,
    struct scsi_pkt *rqpkt, int (*canwait)())
{
	static char *emustring = "EMULEX  MD21/S2";
	register struct scsi_pkt *pkt;
	auto caddr_t wrkbuf;
	register rval = -1;
	struct buf *bp;

	/*
	 * Get a work packet to play with. Get one with a buffer big
	 * enough for another INQUIRY command, and get one with
	 * a cdb big enough for the READ CAPACITY command.
	 */


	bp = scsi_alloc_consistent_buf(ROUTE, (struct buf *)NULL,
	    SUN_INQSIZE, B_READ, canwait, NULL);
	if (!bp) {
		return (rval);
	}
	pkt = scsi_init_pkt(ROUTE, (struct scsi_pkt *)NULL,
	    bp, CDB_GROUP1, 1, PP_LEN,
	    PKT_CONSISTENT, canwait, NULL);
	if (!pkt) {
		return (rval);
	}

	*((caddr_t *)&wrkbuf) = bp->b_un.b_addr;

	/*
	 * Send a TUR & throwaway START UNIT command.
	 *
	 * If we fail on this, we don't care present what
	 * precisely is wrong. Fire off a throwaway REQUEST
	 * SENSE command if the failure is a CHECK_CONDITION.
	 *
	 */
	makecom_g0(pkt, SD_SCSI_DEVP, FLAG_NOPARITY, SCMD_TEST_UNIT_READY,
	    0, 0);
	(void) sd_scsi_poll(pkt);
	if (SCBP(pkt)->sts_chk) {
		(void) sd_clear_cont_alleg(un, rqpkt);
	} else if (SCBP_C(pkt) == STATUS_RESERVATION_CONFLICT) {
		/*
		 * if the disk is reserved by other host, then the inquiry
		 * data we got earlier is probably OK. Also there is no
		 * need for START and REZERO commands.
		 */
		rval = SD_EACCES;
		goto out;
	}

	pkt->pkt_time = 200;
	makecom_g0(pkt, SD_SCSI_DEVP, FLAG_NOPARITY, SCMD_START_STOP, 0, 1);
	(void) sd_scsi_poll(pkt);
	if (SCBP(pkt)->sts_chk) {
		(void) sd_clear_cont_alleg(un, rqpkt);
	} else if (SCBP_C(pkt) == STATUS_RESERVATION_CONFLICT) {
		rval = SD_EACCES;
		goto out;
	}

	/*
	 * Send another Inquiry command to the target. This is necessary
	 * for non-removable media direct access devices because their
	 * Inquiry data may not be fully qualified until they are spun up
	 * (perhaps via the START command above)
	 */

	bzero(wrkbuf, SUN_INQSIZE);
	makecom_g0(pkt, SD_SCSI_DEVP, FLAG_NOPARITY, SCMD_INQUIRY, 0,
	    SUN_INQSIZE);
	if (sd_scsi_poll(pkt) < 0)	 {
		goto out;
	} else if (SCBP(pkt)->sts_chk) {
		(void) sd_clear_cont_alleg(un, rqpkt);
	} else {
		if ((pkt->pkt_state & STATE_XFERRED_DATA) &&
		    (SUN_INQSIZE - pkt->pkt_resid) >= SUN_MIN_INQLEN) {
			bcopy(wrkbuf, (caddr_t)SD_INQUIRY, SUN_INQSIZE);
		}
	}

	/*
	 * It would be nice to attempt to make sure that there is a disk there,
	 * but that turns out to be very hard- We used to use the REZERO
	 * command to verify that a disk was attached, but some SCSI disks
	 * can't handle a REZERO command. We can't use a READ command
	 * because the disk may not be formatted yet. Therefore, we just
	 * have to believe a disk is there until proven otherwise, *except*
	 * (hack hack hack) for the MD21.
	 */

	if (bcmp(SD_INQUIRY->inq_vid, emustring, strlen(emustring)) == 0) {
		/*
		 * Don't need to set noparity- the MD21 supports it
		 */
		makecom_g0(pkt, SD_SCSI_DEVP, 0, SCMD_REZERO_UNIT, 0, 0);
		if (sd_scsi_poll(pkt) || SCBP_C(pkt) != STATUS_GOOD) {
			if (SCBP(pkt)->sts_chk) {
				(void) sd_clear_cont_alleg(un, rqpkt);
			}
			goto out;
		}
	}

	/*
	 * At this point, we've 'succeeded' with this winchester
	 */

	rval = 0;
out:
	scsi_destroy_pkt(pkt);
	scsi_free_consistent_buf(bp);
	return (rval);
}

sd_read_capacity(struct scsi_disk *un, struct scsi_capacity *cptr)
{
	struct	scsi_pkt *pkt;
	caddr_t buffer;
	register rval = -1;
	struct buf *bp;

	SD_DEBUG2(SD_DEVINFO, sd_label, SCSI_DEBUG,
	    "reading the drive's capacity\n");

	bp = scsi_alloc_consistent_buf(ROUTE, (struct buf *)NULL,
	    sizeof (struct scsi_capacity), B_READ, NULL_FUNC, NULL);
	if (!bp) {
		return (ENOMEM);
	}
	pkt = scsi_init_pkt(ROUTE, (struct scsi_pkt *)NULL, bp,
	    CDB_GROUP1, 1, PP_LEN, PKT_CONSISTENT, NULL_FUNC, NULL);
	if (!pkt) {
		return (ENOMEM);
	}
	*((caddr_t *)&buffer) = bp->b_un.b_addr;

	makecom_g1(pkt, SD_SCSI_DEVP, FLAG_NOPARITY, SCMD_READ_CAPACITY, 0, 0);
	if (sd_scsi_poll(pkt) >= 0 && SCBP_C(pkt) == STATUS_GOOD &&
	    (pkt->pkt_state & STATE_XFERRED_DATA) && (pkt->pkt_resid == 0)) {
		/*
		 * The returned capacity is the LBA of the last
		 * addressable logical block, so the real capacity
		 * is one greater
		 */
		cptr->capacity =
		    ((struct scsi_capacity *)buffer)->capacity+1;
		cptr->lbasize =
		    ((struct scsi_capacity *)buffer)->lbasize;
		rval = 0;
	} else {
		if (SCBP(pkt)->sts_chk) {
			(void) sd_clear_cont_alleg(un, un->un_rqs);
		}
		rval = EIO;
	}

	scsi_destroy_pkt(pkt);
	scsi_free_consistent_buf(bp);
	SD_DEBUG2(SD_DEVINFO, sd_label, SCSI_DEBUG,
	    "drive capacity %d block size %d\n",
	    cptr->capacity, cptr->lbasize);

	if (cptr->capacity <= 0 || cptr->lbasize <= 0) {
		cptr->capacity = (u_long)-1;
		cptr->lbasize = (u_long)-1;
		rval = EIO;
	}
	return (rval);
}


/*
 * Validate the geometry for this disk, e.g.,
 * see whether it has a valid label.
 */

static void
sd_validate_geometry(struct scsi_disk *un, int (*f)())
{
	register struct scsi_pkt *pkt;
	register int fnoparity, secsize = 0;
	auto struct dk_label *dkl;
	auto char *label = 0, labelstring[128];
	auto char buf[256];
	int count;
	int secdiv;

	secsize = un->un_secsize;

	/*
	 * take a log base 2 of sector size (sorry)
	 */
	for (secdiv = 0; secsize = secsize >> 1; secdiv++)
		;
	un->un_secdiv = secdiv;

	if (un->un_dp->options & SD_NOPARITY)
		fnoparity = FLAG_NOPARITY;
	else
		fnoparity = 0;

	/*
	 * Only DIRECT ACCESS devices will have Sun labels.
	 * CD's supposedly have a Sun label, too
	 */

	if (SD_INQUIRY->inq_dtype == DTYPE_DIRECT || ISCD(un)) {
		int i;
		struct buf *bp;
		bp = scsi_alloc_consistent_buf(ROUTE,
		    (struct buf *)NULL, un->un_secsize, B_READ, f, NULL);
		if (!bp) {
			scsi_log(SD_DEVINFO, sd_label, CE_WARN,
			    "no bp for disk label\n");
			return;
		}
		pkt = scsi_init_pkt(ROUTE, (struct scsi_pkt *)NULL,
		    bp, CDB_GROUP1, 1, PP_LEN,
		    PKT_CONSISTENT, f, NULL);
		if (!pkt) {
			scsi_log(SD_DEVINFO, sd_label, CE_WARN,
			    "no memory for disk label\n");
			scsi_free_consistent_buf(bp);
			return;
		}

		*((caddr_t *)&dkl) = bp->b_un.b_addr;

		bzero((caddr_t)dkl,  un->un_secsize);
		makecom_g0(pkt, SD_SCSI_DEVP, fnoparity, SCMD_READ, 0, 1);
		/*
		 * Ok, it's ready - try to read and use the label.
		 */
		mutex_exit(SD_MUTEX);
		for (i = 0; i < 3; i++) {
			if (sd_scsi_poll(pkt) ||
			    SCBP_C(pkt) != STATUS_GOOD ||
			    (pkt->pkt_state & STATE_XFERRED_DATA) == 0 ||
			    (pkt->pkt_resid != 0)) {
				if (SCBP_C(pkt) ==
				    STATUS_RESERVATION_CONFLICT) {
					break;
				}
				if (i > 2 && un->un_state == SD_STATE_NIL) {
					scsi_log(SD_DEVINFO, sd_label, CE_WARN,
					    "unable to read label\n");
				}
				if (SCBP(pkt)->sts_chk) {
					(void) sd_clear_cont_alleg(un,
							un->un_rqs);
				}
			} else {
				/*
				 * sd_uselabel will establish
				 * that the geometry is valid
				 */
				sd_uselabel(un, dkl);
				break;
			}
		}
		mutex_enter(SD_MUTEX);
		label = (char *)un->un_asciilabel;
		scsi_destroy_pkt(pkt);
		scsi_free_consistent_buf(bp);
	} else if (un->un_capacity < 0) {
		return;
	} else	{
		/*
		 * This is a totally bogus geometry- really need to do a mode
		 * sense.
		 */
		label = 0;
		bzero((caddr_t)&un->un_g, sizeof (struct dk_geom));
		bzero((caddr_t)&un->un_vtoc, sizeof (struct dk_vtoc));
		un->un_g.dkg_pcyl = un->un_capacity >> 5;
		un->un_g.dkg_ncyl = un->un_g.dkg_pcyl - 2;
		un->un_g.dkg_acyl = 2;
		un->un_g.dkg_nhead = 1;
		un->un_g.dkg_nsect = 32;
		un->un_g.dkg_intrlv = 1;
		un->un_g.dkg_rpm = 3000;

		un->un_g.dkg_read_reinstruct = 0;
		un->un_g.dkg_write_reinstruct = 0;

		bzero((caddr_t)un->un_map,
			NDKMAP * (sizeof (struct dk_map)));
		un->un_map['c'-'a'].dkl_cylno = 0;
		un->un_map['c'-'a'].dkl_nblk = un->un_capacity;
		inq_fill(SD_INQUIRY->inq_vid, VIDMAX, labelstring);
		scsi_log(SD_DEVINFO, sd_label, CE_CONT,
		    "?<%s %d blocks>\n", labelstring, (int)un->un_capacity);
	}

	/*
	 * Special exception for CD's without a valid label.
	 * XXX: We should do this for all disks.
	 */

	if (ISCD(un) && !un->un_gvalid) {
		/*
		 * initialize the geometry and partition table.
		 */
		bzero((caddr_t)&un->un_g, sizeof (struct dk_geom));
		bzero((caddr_t)&un->un_vtoc, sizeof (struct dk_vtoc));
		bzero((caddr_t)un->un_map, NDKMAP * (sizeof (struct dk_map)));
		un->un_asciilabel[0] = '\0';

		/*
		 * It's CDROM, therefore, no label. But it is still necessary
		 * to set up various geometry information, and we are doing
		 * this here.
		 *
		 * For the number of sector per track, we put the maximum
		 * number, assuming a CDROM can hold up to 600 Mbytes of
		 * data. For the rpm, we use the minimum for the disk.
		 */

		un->un_g.dkg_ncyl = 1;
		un->un_g.dkg_acyl = 0;
		un->un_g.dkg_bcyl = 0;
		un->un_g.dkg_nhead = 1;
		un->un_g.dkg_nsect = 0;
		un->un_g.dkg_intrlv = 1;
		un->un_g.dkg_rpm = 200;

		un->un_g.dkg_read_reinstruct = 0;
		un->un_g.dkg_write_reinstruct = 0;

		un->un_map['a'-'a'].dkl_cylno = 0;
		un->un_map['a'-'a'].dkl_nblk = un->un_capacity;
		un->un_map['c'-'a'].dkl_cylno = 0;
		un->un_map['c'-'a'].dkl_nblk = un->un_capacity;
		un->un_gvalid = 1;
	}

	if (un->un_state == SD_STATE_NIL) {
		New_state(un, SD_STATE_CLOSED);
		/*
		 * Print out a message indicating who and what we are.
		 * If the geometry is valid, print the label string,
		 * else print vendor and product info, if available
		 */
		if (un->un_gvalid && label) {
			scsi_log(SD_DEVINFO, sd_label, CE_CONT,
			    "?<%s>\n", label);
		} else {
			inq_fill(SD_INQUIRY->inq_vid, VIDMAX, labelstring);
			inq_fill(SD_INQUIRY->inq_pid, PIDMAX, &labelstring[64]);
			sprintf(buf, "?Vendor '%s', product '%s'",
			    labelstring, &labelstring[64]);
			if (un->un_capacity > 0) {
				sprintf(&buf[strlen(buf)],
					", %d %d byte blocks\n",
					(int)un->un_capacity,
					(int)un->un_lbasize);
			} else {
				sprintf(&buf[strlen(buf)],
					", (unknown capacity)\n");
			}
			scsi_log(SD_DEVINFO, sd_label, CE_CONT, buf);
		}
		if (sddebug) {
			inq_fill(SD_INQUIRY->inq_vid, VIDMAX, labelstring);
			inq_fill(SD_INQUIRY->inq_serial, 12, &labelstring[16]);
			inq_fill(SD_INQUIRY->inq_revision, 4, &labelstring[32]);
			scsi_log(SD_DEVINFO, sd_label, CE_CONT,
			    "?%s serial number: %s - %s\n",
			    labelstring, &labelstring[16],
			    &labelstring[32]);
		}
	}

	for (count = 0; count < NDKMAP; count++) {
		struct dk_map *lp  = &un->un_map[count];
		un->un_offset[count] =
		    un->un_g.dkg_nhead *
		    un->un_g.dkg_nsect *
		    lp->dkl_cylno;
	}
}

/*
 * Check the label for righteousity, and snarf yummos from validated label.
 * Marks the geometry of the unit as being valid.
 */

static void
sd_uselabel(struct scsi_disk *un, struct dk_label *l)
{
	static char *geom = "Label says %d blocks, Drive says %d blocks\n";
	static char *badlab = "corrupt label - %s\n";
	short *sp, sum, count;
	long capacity;

	/*
	 * Check magic number of the label
	 */
	if (l->dkl_magic != DKL_MAGIC) {
		if (un->un_state == SD_STATE_NIL && !ISREMOVABLE(un))
			scsi_log(SD_DEVINFO, sd_label, CE_WARN, badlab,
			    "wrong magic number");
		return;
	}

	/*
	 * Check the checksum of the label,
	 */
	sp = (short *)l;
	sum = 0;
	count = sizeof (struct dk_label) / sizeof (short);
	while (count--)	 {
		sum ^= *sp++;
	}

	if (sum) {
		if (un->un_state == SD_STATE_NIL && !ISREMOVABLE(un)) {
			scsi_log(SD_DEVINFO, sd_label, CE_WARN, badlab,
			    "label checksum failed");
		}
		return;
	}

	mutex_enter(SD_MUTEX);
	/*
	 * Fill in disk geometry from label.
	 */
	un->un_g.dkg_ncyl = l->dkl_ncyl;
	un->un_g.dkg_acyl = l->dkl_acyl;
	un->un_g.dkg_bcyl = 0;
	un->un_g.dkg_nhead = l->dkl_nhead;
	un->un_g.dkg_bhead = l->dkl_bhead;
	un->un_g.dkg_nsect = l->dkl_nsect;
	un->un_g.dkg_gap1 = l->dkl_gap1;
	un->un_g.dkg_gap2 = l->dkl_gap2;
	un->un_g.dkg_intrlv = l->dkl_intrlv;
	un->un_g.dkg_pcyl = l->dkl_pcyl;
	un->un_g.dkg_rpm = l->dkl_rpm;

	/*
	 * The Read and Write reinstruct values may not be vaild
	 * for older disks.
	 */
	un->un_g.dkg_read_reinstruct = l->dkl_read_reinstruct;
	un->un_g.dkg_write_reinstruct = l->dkl_write_reinstruct;

	/*
	 * If labels don't have pcyl in them, make a guess at it.
	 * The right thing, of course, to do, is to do a MODE SENSE
	 * on the Rigid Disk Geometry mode page and check the
	 * information with that. Won't work for non-CCS though.
	 */

	if (un->un_g.dkg_pcyl == 0)
		un->un_g.dkg_pcyl = un->un_g.dkg_ncyl + un->un_g.dkg_acyl;

	/*
	 * Fill in partition table.
	 */

	bcopy((caddr_t)l->dkl_map, (caddr_t)un->un_map,
	    NDKMAP * sizeof (struct dk_map));

	/*
	 * Fill in VTOC Structure.
	 */
	bcopy((caddr_t)&l->dkl_vtoc, (caddr_t)&un->un_vtoc,
	    sizeof (struct dk_vtoc));

	un->un_gvalid = 1;			/* "it's here..." */

	capacity = un->un_g.dkg_ncyl * un->un_g.dkg_nhead * un->un_g.dkg_nsect;
	if (un->un_g.dkg_acyl)
		capacity +=  (un->un_g.dkg_nhead * un->un_g.dkg_nsect);

	if (un->un_capacity > 0) {
		if (capacity > un->un_capacity &&
		    un->un_state == SD_STATE_NIL) {
			if (ISCD(un)) { /* trust the label */
				un->un_capacity = capacity;
			} else {
				scsi_log(SD_DEVINFO, sd_label, CE_WARN, badlab,
				    "bad geometry");
				scsi_log(SD_DEVINFO, sd_label, CE_CONT, geom,
				    capacity, un->un_capacity);
				un->un_gvalid = 0;	/* "No it's not!.." */
			}
		} else {
			un->un_capacity = capacity;
			SD_DEBUG2(SD_DEVINFO, sd_label, SCSI_DEBUG, geom,
			    capacity, un->un_capacity);
		}
	} else {
		/*
		 * We have a situation where the target didn't give us a
		 * good 'read capacity' command answer, yet there appears
		 * to be a valid label. In this case, we'll
		 * fake the capacity.
		 */
		un->un_capacity = capacity;
	}
	/*
	 * XXX: Use Mode Sense to determine things like
	 *	rpm, geometry, from SCSI-2 compliant
	 *	peripherals
	 */
	if (un->un_g.dkg_rpm == 0)
		un->un_g.dkg_rpm = 3600;
	bcopy((caddr_t)l->dkl_asciilabel,
	    (caddr_t)un->un_asciilabel, LEN_DKL_ASCII);
	mutex_exit(SD_MUTEX);
}


/*
 * Unix Entry Points
 */

/* ARGSUSED3 */
static int
sdopen(dev_t *dev_p, int flag, int otyp, cred_t *cred_p)
{
	register dev_t dev = *dev_p;
	register int rval;
	auto struct scsi_capacity capbuf;
	register int partmask;

	GET_SOFT_STATE(dev);
	partmask = 1 << part;
	/*
	 * We use a semaphore here in order to serialize
	 * open and close requests on the device.
	 */
	sema_p(&un->un_semoclose);

	mutex_enter(SD_MUTEX);

	if (un->un_suspended) {
		mutex_exit(SD_MUTEX);
		ddi_dev_is_needed(SD_DEVINFO, 0, 1);
		mutex_enter(SD_MUTEX);
	}
	drv_getparm(TIME, &un->un_timestamp);

	/*
	 * calculate make_sd_cmd() flags and stat_size	here since these
	 * are unlikely to change
	 */
	un->un_cmd_flags =
		(un->un_dp->options & SD_NOPARITY)? FLAG_NOPARITY : 0;

	if ((un->un_dp->options & SD_NODISC) != 0) {
		un->un_cmd_flags |= FLAG_NODISCON;
	}

	un->un_cmd_stat_size =	(un->un_arq_enabled ?
		    sizeof (struct scsi_arq_status) : 1);

	SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG, "sdopen un=%x\n", un);
	/*
	 * check for previous exclusive open
	 */
	SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG,
		"exclopen=%x, flag=%x, regopen=%x\n",
		un->un_exclopen, flag, un->un_ocmap.regopen[otyp]);

	if (un->un_exclopen & (partmask)) {
failed_exclusive:
		mutex_exit(SD_MUTEX);
		sema_v(&un->un_semoclose);
		SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG,
			"exclusive open fails\n");
		return (EBUSY);
	}

	if (flag & FEXCL) {
		int i;
		if (un->un_ocmap.lyropen[part]) {
			goto failed_exclusive;
		}
		for (i = 0; i < (OTYPCNT - 1); i++) {
			if (un->un_ocmap.regopen[i] & (partmask)) {
				goto failed_exclusive;
			}
		}
	}

	if (ISREMOVABLE(un)) {
		if (flag & FWRITE) {
			mutex_exit(SD_MUTEX);
			if (ISCD(un) || sd_check_wp(dev)) {
				sema_v(&un->un_semoclose);
				return (EROFS);
			}
			mutex_enter(SD_MUTEX);
		}

		if (sd_unit_ready(dev) != 0) {
			goto fail;
		}

		if (!un->un_gvalid || (int)un->un_capacity < 0 ||
		    un->un_lbasize < 0) {

			/*
			 * New Disk has been inserted.
			 * Find out the sector size, and set disk to that.
			 */
			if (ISCD(un)) {
				mutex_exit(SD_MUTEX);
				(void) sr_check_blk_size(dev);
				mutex_enter(SD_MUTEX);
				if (sr_sector_mode(dev, 0)) {
					goto fail;
				}
			}

			/*
			 * capacity has to be read every open.
			 */
			un->un_capacity = -1;
			un->un_lbasize = -1;

			mutex_exit(SD_MUTEX);
			rval = sd_read_capacity(un, &capbuf);
			mutex_enter(SD_MUTEX);
			if (rval == 0) {
				un->un_capacity = capbuf.capacity;
				un->un_lbasize = capbuf.lbasize;
			}
			if ((int)un->un_capacity < 0 || un->un_lbasize < 0) {
				un->un_gvalid = 0;
				goto fail;
			}
		}
	} else {
		/*
		 * do a test unit ready to clear any unit attention
		 * from non-cd devices.
		 * note: some TQ devices get hung (ie. discon. cmd timeouts)
		 * when they receive a TUR when the queue is non-empty
		 * therefore, only issue a TUR if no cmds outstanding
		 */
		if (un->un_ncmds == 0) {
			(void) sd_unit_ready(dev);
		}
	}

	if (un->un_gvalid == 0 && un->un_format_in_progress == 0) {
		sd_validate_geometry(un, SLEEP_FUNC);
	}


	/*
	 * No, don't put an else here: sdattach may validate the geometry....
	 * Attempt to guess open for format by open flags && char device
	 */

	if (!ISCD(un)) {
		if (un->un_gvalid == 0 || un->un_map[part].dkl_nblk <= 0) {
			/*
			 * Are we opening this for format(8)?
			 */
			if ((flag & (FNDELAY|FNONBLOCK)) == 0 ||
			    otyp != OTYP_CHR) {
				goto fail;
			}
		}
	}

	if (un->un_state == SD_STATE_DETACHING) {
		New_state(un, SD_STATE_OPENING);
		rval = sd_unit_ready(dev);
		if (rval != 0 && rval != EACCES) {
			sd_offline(un, 1);
			goto fail;
		}
		/*
		 * disk now back on line...
		 */
		scsi_log(SD_DEVINFO, sd_label, CE_NOTE, diskokay);

	} else if ((un->un_state == SD_STATE_CLOSED) ||
	    (un->un_state == SD_STATE_NIL) ||
	    (un->un_state == SD_STATE_EJECTED)) {
		rval = sd_unit_ready(dev);
		if (rval != 0 && rval != EACCES) {
			sd_offline(un, 1);
			goto fail;
		}
		New_state(un, SD_STATE_OPENING);
	}

#ifdef DOESNTWORK /* on eliteII, see 1118607 */
	/*
	 * check to see if this disk is write protected,
	 * if it is and we have not set read-only, then fail
	 */
	mutex_exit(SD_MUTEX);
	if ((flag & FWRITE) && (sd_check_wp(dev))) {
		mutex_enter(SD_MUTEX);
		New_state(un, SD_STATE_CLOSED);
		mutex_exit(SD_MUTEX);
		sema_v(&un->un_semoclose);
		return (EROFS);
	}
	mutex_enter(SD_MUTEX);
#endif

	/*
	 * If this is a removable media device, try and send
	 * a PREVENT MEDIA REMOVAL command, but don't get upset
	 * if it fails.
	 * For a CD, however, it is an error
	 */
	if (ISREMOVABLE(un) && (un->un_ocmap.regopen[otyp] == 0)) {
		mutex_exit(SD_MUTEX);
		rval = sd_lock_unlock(dev, SD_REMOVAL_PREVENT);
		mutex_enter(SD_MUTEX);
		if (ISCD(un) && rval != 0) {
			goto fail;
		}
	}

	if (otyp >= OTYPCNT) {
		New_state(un, SD_STATE_CLOSED);
		mutex_exit(SD_MUTEX);
		sema_v(&un->un_semoclose);
		return (EINVAL);
	} else if (otyp == OTYP_LYR) {
		un->un_ocmap.lyropen[part]++;
	} else {
		un->un_ocmap.regopen[otyp] |= partmask;
	}

	/*
	 * the state has changed, inform the media watch
	 */
	New_state(un, SD_STATE_OPEN);
	un->un_mediastate = DKIO_INSERTED;
	cv_broadcast(&un->un_state_cv);

	/*
	 * set up open and exclusive open flags
	 */
	if (flag & FEXCL)
		un->un_exclopen |= (partmask);

	mutex_exit(SD_MUTEX);

	SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG, "open of part %d type %d\n",
		part, otyp);
	if (un->un_stats == (kstat_t *)0) {
		un->un_stats = kstat_create("sd", instance, NULL, "disk",
			KSTAT_TYPE_IO, 1, KSTAT_FLAG_PERSISTENT);
		if (un->un_stats) {
			un->un_stats->ks_lock = SD_MUTEX;
			kstat_install(un->un_stats);
		}
	}
	sema_v(&un->un_semoclose);
	return (0);

fail:
	if (flag & (FNONBLOCK|FNDELAY)) {
		if (otyp >= OTYPCNT) {
			New_state(un, SD_STATE_CLOSED);
			mutex_exit(SD_MUTEX);
			sema_v(&un->un_semoclose);
			return (EINVAL);
		} else if (otyp == OTYP_LYR) {
			un->un_ocmap.lyropen[part]++;
		} else {
			un->un_ocmap.regopen[otyp] |= partmask;
		}

		/*
		 * set up open and exclusive open flags
		 */
		if (flag & FEXCL)
			un->un_exclopen |= (partmask);

		un->un_mediastate = DKIO_NONE;
		mutex_exit(SD_MUTEX);
		sema_v(&un->un_semoclose);
		return (0);
	}

	mutex_exit(SD_MUTEX);
	sema_v(&un->un_semoclose);
	return (ENXIO);

}


/*ARGSUSED*/
static int
sdclose(dev_t dev, int flag, int otyp, cred_t *cred_p)
{
	register u_char *cp;

	GET_SOFT_STATE(dev);

	if (otyp >= OTYPCNT)
		return (ENXIO);

	SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG, "close of part %d type %d\n",
	    part, otyp);
	sema_p(&un->un_semoclose);

	mutex_enter(SD_MUTEX);

	if (un->un_exclopen & (1<<part)) {
		un->un_exclopen &= ~(1<<part);
	}
	if (un->un_suspended) {
		mutex_exit(SD_MUTEX);
		ddi_dev_is_needed(SD_DEVINFO, 0, 1);
		mutex_enter(SD_MUTEX);
	}
	drv_getparm(TIME, &un->un_timestamp);

	if (otyp == OTYP_LYR) {
		un->un_ocmap.lyropen[part] -= 1;
	} else {
		un->un_ocmap.regopen[otyp] &= ~(1<<part);
	}

	if (un->un_state < SD_STATE_OPEN) {
		mutex_exit(SD_MUTEX);
		sema_v(&un->un_semoclose);
		return (ENXIO);
	}
	cp = &un->un_ocmap.chkd[0];
	while (cp < &un->un_ocmap.chkd[OCSIZE]) {
		if (*cp != (u_char)0) {
			break;
		}
		cp++;
	}

	if (cp == &un->un_ocmap.chkd[OCSIZE]) {
		SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG, "last close\n");
		if (un->un_state == SD_STATE_DETACHING) {
			sd_offline(un, 1);
		} else {
			int rval;
			New_state(un, SD_STATE_CLOSED);
			/*
			 * If this is a removable media device, try and send
			 * an ALLOW MEDIA REMOVAL command, but don't get upset
			 * if it fails.
			 *
			 * For now, if it is a removable media device,
			 * invalidate the geometry.
			 *
			 * XXX: Later investigate whether or not it
			 *	would be a good idea to invalidate
			 *	the geometry for all devices.
			 */

			if ((ISCD(un) && un->un_state != SD_STATE_EJECTED) ||
			    ISREMOVABLE(un)) {
				mutex_exit(SD_MUTEX);
				rval = sd_lock_unlock(dev, SD_REMOVAL_ALLOW);
				if (ISCD(un) && rval != 0) {
					sema_v(&un->un_semoclose);
					return (ENXIO);
				}
				mutex_enter(SD_MUTEX);
			}

			if (ISREMOVABLE(un)) {
				sd_ejected(un);
			}
		}
		if (un->un_stats) {
			mutex_exit(SD_MUTEX);
			kstat_delete(un->un_stats);
			un->un_stats = 0;
			mutex_enter(SD_MUTEX);
		}
	}
	mutex_exit(SD_MUTEX);
	sema_v(&un->un_semoclose);
	return (0);
}

static void
sd_offline(struct scsi_disk *un, int bechatty)
{
	ASSERT(mutex_owned(SD_MUTEX));

	if (bechatty)
		scsi_log(SD_DEVINFO, sd_label, CE_WARN, "offline\n");

	un->un_capacity = -1;
	un->un_lbasize = -1;
	un->un_gvalid = 0;
}

static void
sd_not_ready(struct scsi_disk *un, struct scsi_pkt *pkt)
{
	ASSERT(mutex_owned(SD_MUTEX));

	switch (SD_RQSENSE->es_add_code) {
	case 0x04:
		/*
		 * do not call restart unit immediately since more
		 * requests may come back from the target with check condition
		 * the start motor cmd would just hit the busy condition
		 * (contingent allegiance)
		 * sd_decode_sense() will execute a timeout() on sdrestart
		 * with the same delay, so don't increase this delay.
		 */
		if (pkt->pkt_cdbp[0] != SCMD_START_STOP) {
			/*
			 * If the original cmd was SCMD_START_STOP then do
			 * not initiate another one via sd_restart_unit
			 */
			(void) timeout(sd_restart_unit, (caddr_t)un,
			    SD_BSY_TIMEOUT/2);
		}
		break;
	case 0x05:
		if (sd_error_level < SCSI_ERR_RETRYABLE)
			scsi_log(SD_DEVINFO, sd_label, CE_WARN,
			    "unit does not respond to selection\n");
		break;
	case 0x3a:
		if (sd_error_level >= SCSI_ERR_FATAL)
			scsi_log(SD_DEVINFO, sd_label, CE_WARN,
			    "Caddy not inserted in drive\n");
		/*
		 * Invalidate capacity
		 */
		sd_ejected(un);
		un->un_mediastate = DKIO_EJECTED;
		cv_broadcast(&un->un_state_cv);

		/*
		 * Force no more retries
		 */
		PKT_SET_RETRY_CNT(pkt, sd_retry_count);

		break;
	default:
		if (sd_error_level < SCSI_ERR_RETRYABLE)
			scsi_log(SD_DEVINFO, sd_label, CE_NOTE,
			    "Unit not Ready. Additional sense code 0x%x\n",
				SD_RQSENSE->es_add_code);
		break;
	}
}

/*
 * Invalidate geometry information after detecting an ejected* media.
 */
static void
sd_ejected(register struct scsi_disk *un)
{
	ASSERT(mutex_owned(SD_MUTEX));

	un->un_capacity = -1;
	un->un_lbasize = -1;
	un->un_gvalid = 0;

}

/*
 * restart disk. this function is called thru timeout(9F) and it
 * would be bad practice to sleep till completion in the timeout
 * thread. therefore, use the callback function to clean up
 * we don't care here whether transport succeeds or not; it will get
 * retried again later
 */
static void
sd_restart_unit(register struct scsi_disk *un)
{

	register struct scsi_pkt *pkt;

	SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG, "sd_restart_unit\n");

	/* LOCK UNNEEDED BECAUSE "Read Only": un->un_sd->sd_mutex */
	if (un->un_format_in_progress) {
		/*
		 * Some unformatted drives report not ready error,
		 * no need to restart if format has been initiated.
		 */
		return;
	}
	/* LOCK NEEDED: un->un_sd->sd_mutex */

	pkt = scsi_init_pkt(ROUTE, (struct scsi_pkt *)NULL, NULL,
	    CDB_GROUP0, un->un_cmd_stat_size, 0, 0, NULL_FUNC, NULL);
	if (!pkt) {
		return;
	}
	pkt->pkt_time = 90;	/* this is twice what we require */
	makecom_g0(pkt, SD_SCSI_DEVP, FLAG_NOPARITY, SCMD_START_STOP, 0, 1);
	pkt->pkt_comp = sd_restart_unit_callback;
	pkt->pkt_private = (opaque_t)un;
	if (scsi_transport(pkt) != TRAN_ACCEPT) {
		scsi_destroy_pkt(pkt);
	}
}

static void
sd_restart_unit_callback(struct scsi_pkt *pkt)
{
	struct scsi_disk *un = (struct scsi_disk *)pkt->pkt_private;

	SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG,
		"sd_restart_unit_callback\n");

	if ((SCBP(pkt)->sts_chk) &&
	    ((pkt->pkt_state & STATE_ARQ_DONE) == 0)) {
		(void) sd_clear_cont_alleg(un, un->un_rqs);
	}
	scsi_destroy_pkt(pkt);
}

/*
 * Given the device number return the devinfo pointer
 * from the scsi_device structure.
 */
/*ARGSUSED*/
static int
sdinfo(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result)
{
	register dev_t dev;
	register struct scsi_disk *un;
	register int instance, error;


	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		dev = (dev_t)arg;
		instance = SDUNIT(dev);
		if ((un = ddi_get_soft_state(sd_state, instance)) == NULL)
			return (DDI_FAILURE);
		*result = (void *) SD_DEVINFO;
		error = DDI_SUCCESS;
		break;
	case DDI_INFO_DEVT2INSTANCE:
		dev = (dev_t)arg;
		instance = SDUNIT(dev);
		*result = (void *)instance;
		error = DDI_SUCCESS;
		break;
	default:
		error = DDI_FAILURE;
	}
	return (error);
}

/*
 * property operation routine.	return the number of blocks for the partition
 * in question or forward the request to the propery facilities.
 */
static int
sd_prop_op(dev_t dev, dev_info_t *dip, ddi_prop_op_t prop_op, int mod_flags,
    char *name, caddr_t valuep, int *lengthp)
{
	int nblocks, length, km_flags;
	caddr_t buffer;
	struct scsi_disk *un;
	int instance;

	if (dev != DDI_DEV_T_ANY)
		instance = SDUNIT(dev);
	else
		instance = ddi_get_instance(dip);

	if ((un = ddi_get_soft_state(sd_state, instance)) == NULL)
		return (DDI_PROP_NOT_FOUND);


	if (strcmp(name, "nblocks") == 0) {
		if (!un->un_gvalid) {
			return (DDI_PROP_NOT_FOUND);
		}
		mutex_enter(SD_MUTEX);
		nblocks = (int)un->un_map[SDPART(dev)].dkl_nblk;
		mutex_exit(SD_MUTEX);

		/*
		* get callers length set return length.
		*/
		length = *lengthp;		/* Get callers length */
		*lengthp = sizeof (int);	/* Set callers length */

		/*
		* If length only request or prop length == 0, get out now.
		* (Just return length, no value at this level.)
		*/
		if (prop_op == PROP_LEN)  {
			*lengthp = sizeof (int);
			return (DDI_PROP_SUCCESS);
		}

		/*
		* Allocate buffer, if required.	 Either way,
		* set `buffer' variable.
		*/
		switch (prop_op)  {

		case PROP_LEN_AND_VAL_ALLOC:

			km_flags = KM_NOSLEEP;

			if (mod_flags & DDI_PROP_CANSLEEP)
				km_flags = KM_SLEEP;

			buffer = (caddr_t)kmem_alloc((size_t)sizeof (int),
			km_flags);
			if (buffer == NULL)  {
				scsi_log(SD_DEVINFO, sd_label, CE_WARN,
				    "no mem for property\n");
				return (DDI_PROP_NO_MEMORY);
			}
			*(caddr_t *)valuep = buffer; /* Set callers buf ptr */
			break;

		case PROP_LEN_AND_VAL_BUF:

			if (sizeof (int) > (length))
			return (DDI_PROP_BUF_TOO_SMALL);

			buffer = valuep; /* get callers buf ptr */
			break;
		}
		*((int *)buffer) = nblocks;
		return (DDI_PROP_SUCCESS);
	}
	if (strcmp(name, "pm_timestamp") == 0 &&
	    ddi_prop_modify(DDI_DEV_T_NONE, dip, mod_flags, name,
	    (caddr_t)&un->un_timestamp, sizeof (un->un_timestamp))
	    != DDI_PROP_SUCCESS) {
		scsi_log(SD_DEVINFO, sd_label, CE_WARN,
		    "sd_prop_op: Can't modify property\n");
	}

	/*
	* not mine pass it on.
	*/
	return (ddi_prop_op(dev, dip, prop_op, mod_flags,
		name, valuep, lengthp));
}

/*
 * These routines perform raw i/o operations.
 */
static int uscsi_maxphys = USCSI_DEFAULT_MAXPHYS;

static void
sduscsimin(struct buf *bp)
{
	if (bp->b_bcount > uscsi_maxphys)
		bp->b_bcount = uscsi_maxphys;
}


static int
sd_scsi_poll(struct scsi_pkt *pkt)
{
	if (scsi_ifgetcap(&pkt->pkt_address, "tagged-qing", 1) == 1) {
		pkt->pkt_flags |= FLAG_STAG;
		pkt->pkt_flags &= ~FLAG_NODISCON;
	}
	return (scsi_poll(pkt));
}


static int
sd_clear_cont_alleg(struct scsi_disk *un, struct scsi_pkt *pkt)
{
	int	ret;

#ifdef __lock_lint
	mutex_enter(SD_MUTEX);
	mutex_exit(SD_MUTEX);
#endif
	ASSERT(pkt == un->un_rqs);

	/*
	 * The request sense should be sent as an untagged command,
	 * otherwise we might get busy error from disk. So we make
	 * a direct call to scsi_poll since sd_scsi_poll uses tags.
	 */
	sema_p(&un->un_rqs_sema);
	ret = scsi_poll(pkt);
	sema_v(&un->un_rqs_sema);
	return (ret);
}


static void
sdmin(struct buf *bp)
{
	extern int maxphys;
	if (bp->b_bcount > maxphys)
		bp->b_bcount = maxphys;
}


/* ARGSUSED2 */
static int
sdread(dev_t dev, struct uio *uio, cred_t *cred_p)
{
	register int secmask;
	GET_SOFT_STATE(dev);
#ifdef lint
	part = part;
#endif /* lint */
	secmask = un->un_secsize - 1;

	if (!un->un_gvalid)
		return (ENXIO);

	if (uio->uio_loffset & ((offset_t)(secmask))) {
		SD_DEBUG2(SD_DEVINFO, sd_label, SCSI_DEBUG,
		    "file offset not modulo %d\n",
		    un->un_secsize);
		return (EINVAL);
	} else if (uio->uio_iov->iov_len & (secmask)) {
		SD_DEBUG2(SD_DEVINFO, sd_label, SCSI_DEBUG,
		    "transfer length not modulo %d\n", un->un_secsize);
		return (EINVAL);
	}
	return (physio(sdstrategy, (struct buf *)0, dev, B_READ, sdmin, uio));
}

/* ARGSUSED2 */
static int
sdaread(dev_t dev, struct aio_req *aio, cred_t *cred_p)
{
	register int secmask;
	struct uio *uio = aio->aio_uio;
	GET_SOFT_STATE(dev);
#ifdef lint
	part = part;
#endif /* lint */
	secmask = un->un_secsize - 1;

	if (!un->un_gvalid)
		return (ENXIO);

	if (uio->uio_loffset & ((offset_t)(secmask))) {
		SD_DEBUG2(SD_DEVINFO, sd_label, SCSI_DEBUG,
		    "file offset not modulo %d\n",
		    un->un_secsize);
		return (EINVAL);
	} else if (uio->uio_iov->iov_len & (secmask)) {
		SD_DEBUG2(SD_DEVINFO, sd_label, SCSI_DEBUG,
		    "transfer length not modulo %d\n", un->un_secsize);
		return (EINVAL);
	}
	return (aphysio(sdstrategy, anocancel, dev, B_READ, sdmin, aio));
}

/* ARGSUSED2 */
static int
sdwrite(dev_t dev, struct uio *uio, cred_t *cred_p)
{
	register int secmask;
	GET_SOFT_STATE(dev);
#ifdef lint
	part = part;
#endif /* lint */
	secmask = un->un_secsize - 1;

	if (!un->un_gvalid)
		return (ENXIO);

	if (uio->uio_loffset & ((offset_t)(secmask))) {
		SD_DEBUG2(SD_DEVINFO, sd_label, SCSI_DEBUG,
		    "file offset not modulo %d\n",
		    un->un_secsize);
		return (EINVAL);
	} else if (uio->uio_iov->iov_len & (secmask)) {
		SD_DEBUG2(SD_DEVINFO, sd_label, SCSI_DEBUG,
		    "transfer length not modulo %d\n", un->un_secsize);
		return (EINVAL);
	}
	return (physio(sdstrategy, (struct buf *)0, dev, B_WRITE, sdmin, uio));
}

/* ARGSUSED2 */
static int
sdawrite(dev_t dev, struct aio_req *aio, cred_t *cred_p)
{
	register int secmask;
	struct uio *uio = aio->aio_uio;
	GET_SOFT_STATE(dev);
#ifdef lint
	part = part;
#endif /* lint */
	secmask = un->un_secsize - 1;

	if (!un->un_gvalid)
		return (ENXIO);

	if (uio->uio_loffset & ((offset_t)(secmask))) {
		SD_DEBUG2(SD_DEVINFO, sd_label, SCSI_DEBUG,
		    "file offset not modulo %d\n",
		    un->un_secsize);
		return (EINVAL);
	} else if (uio->uio_iov->iov_len & (secmask)) {
		SD_DEBUG2(SD_DEVINFO, sd_label, SCSI_DEBUG,
		    "transfer length not modulo %d\n", un->un_secsize);
		return (EINVAL);
	}
	return (aphysio(sdstrategy, anocancel, dev, B_WRITE, sdmin, aio));
}

/*
 * strategy routine
 */

static int
sdstrategy(struct buf *bp)
{
	register struct scsi_disk *un;
	register struct diskhd *dp;
	register i;
	register u_long minor = getminor(bp->b_edev);
	int	 part = minor & SDPART_MASK;

	if ((un = ddi_get_soft_state(sd_state,
	    minor >> SDUNIT_SHIFT)) == NULL ||
	    un->un_state == SD_STATE_DUMPING) {
nothere:
		bp->b_resid = bp->b_bcount;
		SET_BP_ERROR(bp, ENXIO);
		biodone(bp);
		return (0);
	}


	TRACE_2(TR_FAC_SCSI, TR_SDSTRATEGY_START,
	    "sdstrategy_start: bp %x ncmds %d", bp, un->un_ncmds);

	/*
	 * For a CD, a write is an error
	 */

	if (ISCD(un) && (bp != un->un_sbufp)) {
		if ((bp->b_flags & B_READ) == 0) {
			bp->b_resid = bp->b_bcount;
			SET_BP_ERROR(bp, ENXIO);
			biodone(bp);
			return (0);
		}
		/*
		 * If CD is still in mode 2, cannot read
		 */
		mutex_enter(SD_MUTEX);
		if (un->un_state == SD_STATE_MODE2) {
			mutex_exit(SD_MUTEX);
			goto nothere;
		}
		mutex_exit(SD_MUTEX);
	}

	if (un->un_state == SD_STATE_DETACHING && bp != un->un_sbufp) {
		mutex_enter(SD_MUTEX);
		if (un->un_suspended) {
			mutex_exit(SD_MUTEX);
			ddi_dev_is_needed(SD_DEVINFO, 0, 1);
			mutex_enter(SD_MUTEX);
		}
		if (sd_unit_ready(bp->b_edev) != 0) {
			mutex_exit(SD_MUTEX);
			goto nothere;
		}
		/*
		 * disk now back on line...
		 */
		New_state(un, SD_STATE_OPEN);
		mutex_exit(SD_MUTEX);
		scsi_log(SD_DEVINFO, sd_label, CE_NOTE, diskokay);
	}

	bp->b_flags &= ~(B_DONE|B_ERROR);
	bp->b_resid = 0;
	bp->av_forw = 0;

	if (bp != un->un_sbufp) {
		if (un->un_gvalid == 0) {
			scsi_log(SD_DEVINFO, sd_label, CE_WARN,
			    "i/o to invalid geometry\n");
			SET_BP_ERROR(bp, ENXIO);
		} else {
			struct dk_map *lp = &un->un_map[minor & SDPART_MASK];
			register daddr_t bn = dkblock(bp);

			SD_DEBUG2(SD_DEVINFO, sd_label, SCSI_DEBUG,
			    "dkblock(bp) is %d\n",
			    dkblock(bp));

			i = 0;
			if (bn < 0) {
				i = -1;
			} else if (bn >= lp->dkl_nblk) {
				/*
				 * For proper comparison, file system block
				 * number has to be scaled to actual CD
				 * transfer size.
				 * Since all the CDROM operations
				 * that have Sun Labels are in the correct
				 * block size this will work for CD's.	This
				 * will have to change when we have different
				 * sector sizes.
				 *
				 * if bn == lp->dkl_nblk,
				 * Not an error, resid == count
				 */
				if (bn > lp->dkl_nblk) {
					i = -1;
				} else {
					i = 1;
				}
			} else if (bp->b_bcount & (un->un_secsize-1)) {
				/*
				 * This should really be:
				 *
				 * ... if (bp->b_bcount & (un->un_lbasize-1))
				 *
				 */
				i = -1;
			} else {
				/*
				 * sort by absolute block number.
				 */
				bp->b_resid = bn;
				if (!ISCD(un)) {
					bp->b_resid += un->un_offset[part];
				}
				/*
				 * zero out av_back - this will be a signal
				 * to sdstart to go and fetch the resources
				 */
				bp->av_back = NO_PKT_ALLOCATED;
			}

			/*
			 * Check to see whether or not we are done
			 * (with or without errors).
			 */

			if (i != 0) {
				bp->b_resid = bp->b_bcount;
				if (i < 0) {
					bp->b_flags |= B_ERROR;
				} else {
					biodone(bp);
					return (0);
				}
			}
		}
	}

	if ((bp->b_flags & B_ERROR) == 0) {
		/*
		 * We are doing it a bit non-standard. That is, the
		 * head of the b_actf chain is *not* the active command-
		 * it is just the head of the wait queue. The reason
		 * we do this is that the head of the b_actf chain is
		 * guaranteed to not be moved by disksort(), so that
		 * our restart command (pointed to by
		 * b_forw) and the head of the wait queue (b_actf) can
		 * have resources granted without it getting lost in
		 * the queue at some later point (where we would have
		 * to go and look for it).
		 */
		mutex_enter(SD_MUTEX);
		if (un->un_stats) {
			kstat_waitq_enter(IOSP);
		}
		dp = &un->un_utab;

		if (dp->b_actf == NULL) {
			dp->b_actf = bp;
			dp->b_actl = bp;
		} else if (un->un_suspended && bp == un->un_sbufp) {
			bp->b_actf = dp->b_actf;
			dp->b_actf = bp;
		} else {
			TRACE_3(TR_FAC_SCSI, TR_SDSTRATEGY_DISKSORT_START,
"sdstrategy_disksort_start: dp %x bp %x un_ncmds %d",
			    dp, bp, un->un_ncmds);
			disksort(dp, bp);
			TRACE_0(TR_FAC_SCSI, TR_SDSTRATEGY_DISKSORT_END,
			    "sdstrategy_disksort_end");
		}

		ASSERT(un->un_ncmds >= 0);
		ASSERT(un->un_throttle >= 0);
		if ((un->un_ncmds < un->un_throttle) && (dp->b_forw == NULL)) {
			sdstart(un);
		} else if (BP_HAS_NO_PKT(dp->b_actf)) {
			struct buf *cmd_bp;

			cmd_bp = dp->b_actf;
			cmd_bp->av_back = ALLOCATING_PKT;
			mutex_exit(SD_MUTEX);
			/*
			 * try and map this one
			 */
			TRACE_0(TR_FAC_SCSI, TR_SDSTRATEGY_SMALL_WINDOW_START,
			    "sdstrategy_small_window_call (begin)");

			make_sd_cmd(un, cmd_bp, NULL_FUNC);

			TRACE_0(TR_FAC_SCSI, TR_SDSTRATEGY_SMALL_WINDOW_END,
			    "sdstrategy_small_window_call (end)");

			/*
			 * there is a small window where the active cmd
			 * completes before make_sd_cmd returns.
			 * consequently, this cmd never gets started so
			 * we start it from here
			 */
			mutex_enter(SD_MUTEX);
			if ((un->un_ncmds < un->un_throttle) &&
			    (dp->b_forw == NULL)) {
				sdstart(un);
			}
		}
		mutex_exit(SD_MUTEX);
	} else {
		biodone(bp);
	}

done:
	TRACE_0(TR_FAC_SCSI, TR_SDSTRATEGY_END, "sdstrategy_end");
	return (0);
}


/*
 * Unit start and Completion
 * NOTE: we assume that the caller has at least checked for:
 *		(un->un_ncmds < un->un_throttle)
 *	if not, there is no real harm done, scsi_transport() will
 *	return BUSY
 */

static void
sdstart(struct scsi_disk *un)
{
	register int status, sort_key;
	register struct buf *bp;
	register struct diskhd *dp;
	register u_char state = un->un_last_state;

	TRACE_1(TR_FAC_SCSI, TR_SDSTART_START, "sdstart_start: un %x", un);

retry:
	ASSERT(mutex_owned(SD_MUTEX));

	dp = &un->un_utab;
	if (((bp = dp->b_actf) == NULL) ||
		(bp->av_back == ALLOCATING_PKT) ||
		(dp->b_forw != NULL)) {
		TRACE_0(TR_FAC_SCSI, TR_SDSTART_NO_WORK_END,
		    "sdstart_end (no work)");
		return;
	}

	/*
	 * remove from active queue
	 */
	dp->b_actf = bp->b_actf;
	bp->b_actf = 0;

	un->un_timestamp = 0;
	if (un->un_suspended && (bp != un->un_sbufp ||
	    *(char *)(((struct uscsi_cmd *)bp->b_forw)->uscsi_cdb)
	    != SCMD_START_STOP)) {
		mutex_exit(SD_MUTEX);
		ddi_dev_is_needed(SD_DEVINFO, 0, 1);
		mutex_enter(SD_MUTEX);
	}

	/*
	 * If measuring stats, mark exit from wait queue and
	 * entrance into run 'queue' if and only if we are
	 * going to actually start a command.
	 */
	if (un->un_stats) {
		kstat_waitq_to_runq(IOSP);
	}

	/*
	 * increment ncmds before calling scsi_transport because sdintr
	 * may be called before we return from scsi_transport!
	 */
	un->un_ncmds++;
	/*
	 * we assume that things go well and transition to OPEN
	 */
	New_state(un, SD_STATE_OPEN);
	mutex_exit(SD_MUTEX);

	if (BP_HAS_NO_PKT(bp)) {
		make_sd_cmd(un, bp, sdrunout);
		if (BP_HAS_NO_PKT(bp) && !(bp->b_flags & B_ERROR)) {
			mutex_enter(SD_MUTEX);
			bp->b_actf = dp->b_actf;
			dp->b_actf = bp;
			New_state(un, SD_STATE_RWAIT);
			un->un_ncmds--;
			if (un->un_stats)
				kstat_runq_back_to_waitq(IOSP);
			TRACE_0(TR_FAC_SCSI, TR_SDSTART_NO_RESOURCES_END,
				"sdstart_end (No Resources)");
			goto done;
		} else if (bp->b_flags & B_ERROR) {
			mutex_enter(SD_MUTEX);
			un->un_ncmds--;
			bp->b_resid = bp->b_bcount;
			/*
			 * restore old state
			 */
			un->un_state = un->un_last_state;
			un->un_last_state = state;
			if (un->un_stats)
				kstat_runq_exit(IOSP);
			mutex_exit(SD_MUTEX);
			biodone(bp);
			mutex_enter(SD_MUTEX);
			if ((un->un_ncmds < un->un_throttle) &&
			    (dp->b_forw == NULL)) {
				goto retry;
			} else {
				goto done;
			}
		}
	}

	/*
	 * Restore resid from the packet, b_resid had been the
	 * disksort key.
	 */
	sort_key = bp->b_resid;
	bp->b_resid = BP_PKT(bp)->pkt_resid;
	BP_PKT(bp)->pkt_resid = 0;

	/*
	 * We used to check whether or not to try and link commands here.
	 * Since we have found that there is no performance improvement
	 * for linked commands, this has not made much sense. At some
	 * point in the future, linked commands might make sense for
	 * atomicity of operations. XXX unlikely!
	 */

	if ((status = scsi_transport(BP_PKT(bp))) != TRAN_ACCEPT) {
		mutex_enter(SD_MUTEX);
		un->un_ncmds--;
		if (status == TRAN_BUSY) {
			sd_handle_tran_busy(bp, dp, un);
			if (un->un_ncmds > 0) {
				bp->b_resid = sort_key;
			}
		} else {
			mutex_exit(SD_MUTEX);
			scsi_log(SD_DEVINFO, sd_label, CE_WARN,
			    "transport rejected (%d)\n",
			    status);
			bp->b_flags |= B_ERROR;
			sddone(un, bp);
			mutex_enter(SD_MUTEX);
		}
	} else {
		mutex_enter(SD_MUTEX);

		if (dp->b_actf && BP_HAS_NO_PKT(dp->b_actf)) {
			struct buf *cmd_bp;

			cmd_bp = dp->b_actf;
			cmd_bp->av_back = ALLOCATING_PKT;
			mutex_exit(SD_MUTEX);
			/*
			 * try and map this one
			 */
			TRACE_0(TR_FAC_SCSI, TR_SDSTART_SMALL_WINDOW_START,
			    "sdstart_small_window_start");

			make_sd_cmd(un, cmd_bp, NULL_FUNC);

			TRACE_0(TR_FAC_SCSI, TR_SDSTART_SMALL_WINDOW_END,
			    "sdstart_small_window_end");
			/*
			 * there is a small window where the active cmd
			 * completes before make_sd_cmd returns.
			 * consequently, this cmd never gets started so
			 * we start it from here
			 */
			mutex_enter(SD_MUTEX);
			if ((un->un_ncmds < un->un_throttle) &&
			    (dp->b_forw == NULL)) {
				goto retry;
			}
		}
	}

done:
	ASSERT(mutex_owned(SD_MUTEX));
	TRACE_0(TR_FAC_SCSI, TR_SDSTART_END, "sdstart_end");
}

/*
 * make_sd_cmd: create a pkt
 *	no mutex is held in this function
 */
static void
make_sd_cmd(struct scsi_disk *un, struct buf *bp, int (*func)())
{
	auto int count, com;
	register struct scsi_pkt *pkt;
	register int flags, tval;


	TRACE_3(TR_FAC_SCSI, TR_MAKE_SD_CMD_START,
	    "make_sd_cmd_start: un %x bp %x un_ncmds %d",
	    un, bp, un->un_ncmds);


	flags = un->un_cmd_flags;

#ifdef B_ORDER
	/*
	 * if ordered cmd flag set, force this tagged cmd to be
	 * ordered too.	 For untagged I/O, disksort takes care if
	 * things.
	 */
	if ((bp->b_flags & B_ORDER) && (flags & FLAG_TAGMASK))
		flags |= (flags & ~FLAG_TAGMASK) | FLAG_OTAG;
#endif

	if (bp != un->un_sbufp) {
		register int partition = SDPART(bp->b_edev);
		struct dk_map *lp = &un->un_map[partition];
		long secnt;
		daddr_t blkno;
		int dkl_nblk, delta;
		long resid;

		dkl_nblk = lp->dkl_nblk;


		/*
		 * Make sure we don't run off the end of a partition.
		 *
		 * Put this test here so that we can adjust b_count
		 * to accurately reflect the actual amount we are
		 * goint to transfer.
		 */

		/*
		 * First, compute partition-relative block number
		 */

		blkno = dkblock(bp);
		secnt = (bp->b_bcount + (un->un_secsize - 1)) >> un->un_secdiv;
		count = MIN(secnt, dkl_nblk - blkno);
		if (count != secnt) {
			/*
			 * We have an overrun
			 */
			resid = (secnt - count) << un->un_secdiv;
			SD_DEBUG2(SD_DEVINFO, sd_label, SCSI_DEBUG,
			    "overrun by %d sectors\n",
			    secnt - count);
			bp->b_bcount -= resid;
		} else {
			resid = 0;
		}

		/*
		 * Allocate a scsi packet.
		 * We are called in this case with
		 * the disk mutex held, but func is
		 * either NULL_FUNC or sdrunout,
		 * so we do not have to release
		 * the disk mutex here.
		 */
		TRACE_0(TR_FAC_SCSI, TR_MAKE_SD_CMD_INIT_PKT_START,
		    "make_sd_cmd_init_pkt_call (begin)");
		pkt = scsi_init_pkt(ROUTE, NULL, bp, CDB_GROUP1,
		    un->un_cmd_stat_size, PP_LEN, 0,
		    func, (caddr_t)un);
		TRACE_1(TR_FAC_SCSI, TR_MAKE_SD_CMD_INIT_PKT_END,
		    "make_sd_cmd_init_pkt_call (end): pkt %x", pkt);
		if (!pkt) {
			bp->b_bcount += resid;
			bp->av_back = NO_PKT_ALLOCATED;
			TRACE_0(TR_FAC_SCSI,
			    TR_MAKE_SD_CMD_NO_PKT_ALLOCATED1_END,
			    "make_sd_cmd_end (NO_PKT_ALLOCATED1)");
			return;
		}
		if (bp->b_flags & B_READ) {
			com = SCMD_READ;
		} else {
			com = SCMD_WRITE;
		}

		/*
		 * Adjust block number to absolute
		 */
		delta = un->un_offset[partition];
		blkno += delta;

		/*
		 * Save the resid in the packet, temporarily until
		 * we transport the command.
		 */
		pkt->pkt_resid = resid;

		/*
		 * XXX: This needs to be reworked based upon lbasize
		 *
		 * If the block number or the sector count exceeds the
		 * capabilities of a Group 0 command, shift over to a
		 * Group 1 command. We don't blindly use use Group 1
		 * commands because a) some drives (CDC Wren IVs) get a
		 * bit confused, and b)* there is probably a fair amount
		 * of speed difference for a target to receive and decode
		 * a 10 byte command instead of a 6 byte command.
		 */

		if (blkno >= (2<<20) || count > 0xff) {
			com |= SCMD_GROUP1;
			makecom_g1(pkt, SD_SCSI_DEVP, flags, com,
			    (int)blkno, count);
		} else {
			makecom_g0(pkt, SD_SCSI_DEVP, flags, com,
			    (int)blkno, (u_char)count);
		}
		SD_DEBUG2(SD_DEVINFO, sd_label, SCSI_DEBUG,
		    "%s blk %d amt 0x%x resid %d\n", (bp->b_flags & B_READ) ?
		    "read" : "write", blkno, count << SECDIV, resid);
		pkt->pkt_flags = flags | un->un_tagflags;
		tval = sd_io_time;
		/*
		 * Restore Orignal Count
		 */
		if (resid)
			bp->b_bcount += resid;
	} else {
		struct uscsi_cmd *scmd = (struct uscsi_cmd *)bp->b_forw;

		/*
		* Set options.
		*/
		if ((scmd->uscsi_flags & USCSI_SILENT) && !(DEBUGGING)) {
			flags |= FLAG_SILENT;
		}
		if (scmd->uscsi_flags & USCSI_ISOLATE)
			flags |= FLAG_ISOLATE;
		if (scmd->uscsi_flags & USCSI_DIAGNOSE)
			flags |= FLAG_DIAGNOSE;

		/*
		* Set the pkt flags here so we save time later.
		*/
		if (scmd->uscsi_flags & USCSI_NOPARITY)
			flags |= FLAG_NOPARITY;
		if (scmd->uscsi_flags & USCSI_HEAD)
			flags |= FLAG_HEAD;
#ifdef NOTSUPPORTED
		if (scmd->uscsi_flags & USCSI_QUELOCK)
			flags |= FLAG_QUELOCK;
#endif
		if (scmd->uscsi_flags & USCSI_NOINTR)
			flags |= FLAG_NOINTR;

		/*
		 * For tagged queueing, things get a bit complicated.
		 * Check first for head of que and last for ordered que.
		 * If neither head nor order, use the default driver tag flags.
		 */

		if ((scmd->uscsi_flags & USCSI_NOTAG) == 0) {
			if (scmd->uscsi_flags & USCSI_HTAG)
				flags |= FLAG_HTAG;
			else if (scmd->uscsi_flags & USCSI_OTAG)
				flags |= FLAG_OTAG;
			else
				flags |= un->un_tagflags & FLAG_TAGMASK;
		}
		if (scmd->uscsi_flags & USCSI_NODISCON)
			flags = (flags & ~FLAG_TAGMASK) | FLAG_NODISCON;

		TRACE_0(TR_FAC_SCSI, TR_MAKE_SD_CMD_INIT_PKT_SBUF_START,
		    "make_sd_cmd_init_pkt_sbuf_call (begin)");
		pkt = scsi_init_pkt(ROUTE, (struct scsi_pkt *)NULL,
		    (bp->b_bcount)? bp: NULL,
		    scmd->uscsi_cdblen, un->un_cmd_stat_size, PP_LEN,
		    0, func, (caddr_t)un);

		TRACE_1(TR_FAC_SCSI, TR_MAKE_SD_CMD_INIT_PKT_SBUF_END,
		    "make_sd_cmd_init_pkt_sbuf_call (end): pkt %x", pkt);

		if (!pkt) {
			bp->av_back = NO_PKT_ALLOCATED;
			TRACE_0(TR_FAC_SCSI,
			    TR_MAKE_SD_CMD_NO_PKT_ALLOCATED2_END,
			    "make_sd_cmd_end (NO_PKT_ALLOCATED2)");
			return;
		}
		TRACE_1(TR_FAC_SCSI, TR_MAKE_SD_CMD_G0_SBUF_START,
		    "make_sd_cmd_g0_sbuf_start: pkt %x", pkt);
		makecom_g0(pkt, SD_SCSI_DEVP, flags, scmd->uscsi_cdb[0], 0, 0);
		TRACE_1(TR_FAC_SCSI, TR_MAKE_SD_CMD_G0_SBUF_END,
		    "make_sd_cmd_g0_sbuf_end: pkt %x", pkt);
		bcopy(scmd->uscsi_cdb,
		    (caddr_t)pkt->pkt_cdbp, scmd->uscsi_cdblen);
		if (scmd->uscsi_timeout == 0)
			/*
			 * some CD cmds take a long time so increase the
			 * the standard timeout
			 */
			tval = ((ISCD(un))? 2: 1) * sd_io_time;
		else
			tval = scmd->uscsi_timeout;
	}

	pkt->pkt_comp = sdintr;
	pkt->pkt_time = tval;
	PKT_SET_BP(pkt, bp);
	bp->av_back = (struct buf *)pkt;

	TRACE_0(TR_FAC_SCSI, TR_MAKE_SD_CMD_END, "make_sd_cmd_end");
}

/*
 * Command completion processing
 */
static void
sdintr(struct scsi_pkt *pkt)
{
	register struct scsi_disk *un;
	register struct buf *bp;
	register action;
	int status;

	bp = PKT_GET_BP(pkt);
	un = ddi_get_soft_state(sd_state, SDUNIT(bp->b_edev));

	TRACE_1(TR_FAC_SCSI, TR_SDINTR_START, "sdintr_start: un %x", un);
	SD_DEBUG2(SD_DEVINFO, sd_label, SCSI_DEBUG, "sdintr\n");

	mutex_enter(SD_MUTEX);
	un->un_ncmds--;
	ASSERT(un->un_ncmds >= 0);

	/*
	 * do most common case first
	 */
	if ((pkt->pkt_reason == CMD_CMPLT) &&
	    (SCBP_C(pkt) == 0) &&
	    ((pkt->pkt_flags & FLAG_SENSING) == 0)) {
		/*
		 * Get the low order bits of the command byte
		 */
		int com = GETCMD((union scsi_cdb *)pkt->pkt_cdbp);

		/*
		 * If the command is a read or a write, and we have
		 * a non-zero pkt_resid, that is an error. We should
		 * attempt to retry the operation if possible.
		 */
		action = COMMAND_DONE;
		if (pkt->pkt_resid && (com == SCMD_READ || com == SCMD_WRITE)) {
			if ((int)PKT_GET_RETRY_CNT(pkt) < sd_retry_count) {
				PKT_INCR_RETRY_CNT(pkt, 1);
				action = QUE_COMMAND;
			} else {
				/*
				 * if we have exhausted retries
				 * a command with a residual is in error in
				 * this case.
				 */
				action = COMMAND_DONE_ERROR;
			}
			scsi_log(SD_DEVINFO, sd_label,
			    CE_WARN, "incomplete %s- %s\n",
			    (bp->b_flags & B_READ)? "read" : "write",
			    (action == QUE_COMMAND)? "retrying" :
			    "giving up");
		}

		/*
		 * pkt_resid will reflect, at this point, a residual
		 * of how many bytes left to be transferred there were
		 * from the actual scsi command.
		 *
		 * We only care about reporting this type of error
		 * in any but read or write commands. Since we have
		 * snagged any non-zero pkt_resids with read or writes
		 * above, all we have to do here is add pkt_resid to
		 * b_resid.
		 */
		if (action != QUE_COMMAND) {
			bp->b_resid += pkt->pkt_resid;
		}

	} else if ((pkt->pkt_reason != CMD_CMPLT) &&
		(pkt->pkt_flags & FLAG_SENSING)) {
		/*
		 * We were running a REQUEST SENSE which failed; rerun
		 * the original command, if possible
		 * Release the semaphore for req sense pkt
		 */
		sema_v(&un->un_rqs_sema);
		pkt = BP_PKT(bp);
		pkt->pkt_flags &= ~FLAG_SENSING;
		if ((int)PKT_GET_RETRY_CNT(pkt) < sd_retry_count) {
			PKT_INCR_RETRY_CNT(pkt, 1);
			action = QUE_COMMAND;
		} else {
			action = COMMAND_DONE_ERROR;
		}

	} else if (pkt->pkt_reason != CMD_CMPLT) {
		action = sd_handle_incomplete(un, bp);

	} else if (un->un_arq_enabled &&
	    (pkt->pkt_state & STATE_ARQ_DONE)) {
		/*
		 * the transport layer successfully completed an autorqsense
		 */
		action = sd_handle_autosense(un, bp);

	} else if (pkt->pkt_flags & FLAG_SENSING) {
		/*
		 * We were running a REQUEST SENSE. Find out what to do next.
		 * Release the semaphore for req sense pkt
		 */
		pkt = BP_PKT(bp);
		pkt->pkt_flags &= ~FLAG_SENSING;
		action = sd_handle_sense(un, bp);
		sema_v(&un->un_rqs_sema);
	} else {
		/*
		 * check to see if the status bits were okay.
		 */
		action = sd_check_error(un, bp);
	}


	/*
	 * save pkt reason; consecutive failures are not reported unless
	 * fatal
	 * do not reset last_pkt_reason when the cmd was retried and
	 * succeeded because
	 * there maybe more commands comming back with last_pkt_reason
	 */
	if ((un->un_last_pkt_reason != pkt->pkt_reason) &&
	    ((pkt->pkt_reason != CMD_CMPLT) ||
	    (PKT_GET_RETRY_CNT(pkt) == 0))) {
		un->un_last_pkt_reason = pkt->pkt_reason;
	}
	mutex_exit(SD_MUTEX);

	switch (action) {
	case COMMAND_DONE_ERROR:
error:
		bp->b_resid = bp->b_bcount;
		bp->b_flags |= B_ERROR;
		/*FALLTHROUGH*/
	case COMMAND_DONE:
		sddone(un, bp);
		TRACE_0(TR_FAC_SCSI, TR_SDINTR_COMMAND_DONE_END,
		    "sdintr_end (COMMAND_DONE)");
		return;
	case QUE_SENSE:
		mutex_enter(SD_MUTEX);
		if (un->un_ncmds >= un->un_throttle) {
			register struct diskhd *dp = &un->un_utab;

			bp->b_actf = dp->b_actf;
			dp->b_actf = bp;
			pkt->pkt_resid = bp->b_resid;
			if (un->un_stats)
				kstat_runq_back_to_waitq(IOSP);
			mutex_exit(SD_MUTEX);
			return;
		}
		pkt->pkt_flags |= FLAG_SENSING;
		/*
		 * We grab a semaphore before using the request pkt.
		 */
		mutex_exit(SD_MUTEX);
		sema_p(&un->un_rqs_sema);
		mutex_enter(SD_MUTEX);
		PKT_SET_BP(un->un_rqs, bp);
		bzero((caddr_t)SD_RQSENSE, SENSE_LENGTH);
		un->un_ncmds++;
		mutex_exit(SD_MUTEX);
		if ((status = scsi_transport(un->un_rqs)) != TRAN_ACCEPT) {
			/*
			 * this should never return BUSY; it should go to head
			 * off the queue
			 */
			scsi_log(SD_DEVINFO, sd_label, CE_WARN,
			    "transport of request sense fails (%x)\n", status);
			mutex_enter(SD_MUTEX);
			un->un_ncmds--;
			Restore_state(un);
			mutex_exit(SD_MUTEX);
			sema_v(&un->un_rqs_sema);
			goto error;
		}
		break;
	case QUE_COMMAND:
		mutex_enter(SD_MUTEX);
		if (un->un_ncmds >= un->un_throttle) {
			register struct diskhd *dp = &un->un_utab;

			bp->b_actf = dp->b_actf;
			dp->b_actf = bp;
			pkt->pkt_resid = bp->b_resid;
			if (un->un_stats)
				kstat_runq_back_to_waitq(IOSP);
			mutex_exit(SD_MUTEX);
			return;
		}
		un->un_ncmds++;
		mutex_exit(SD_MUTEX);
		if ((status = scsi_transport(BP_PKT(bp))) != TRAN_ACCEPT) {
			register struct diskhd *dp = &un->un_utab;
			mutex_enter(SD_MUTEX);
			un->un_ncmds--;
			if (status == TRAN_BUSY) {
				sd_handle_tran_busy(bp, dp, un);
#ifndef lint
				if (un->un_ncmds > 0) {
					TRACE_0(TR_FAC_SCSI,
						TR_SDINTR_QUE_COMMAND_END,
					"sdintr_end (QUE_COMMAND waitq)");
				}
#endif lint
				mutex_exit(SD_MUTEX);
				return;
			}
			mutex_exit(SD_MUTEX);

			scsi_log(SD_DEVINFO, sd_label, CE_WARN,
			    "requeue of command fails (%x)\n", status);
			bp->b_resid = bp->b_bcount;
			bp->b_flags |= B_ERROR;
			sddone(un, bp);
			TRACE_0(TR_FAC_SCSI, TR_SDINTR_QUE_FAILED_END,
			    "sdintr_end (QUE_COMMAND, requeue failed)");
			return;
		}
		break;
	case JUST_RETURN:
		break;
	}

	TRACE_0(TR_FAC_SCSI, TR_SDINTR_END, "sdintr_end");
}


/*
 * Done with a command.
 * No mutexes are held when entering sddone()
 */
static void
sddone(struct scsi_disk *un, register struct buf *bp)
{
	register struct diskhd *dp;

	TRACE_1(TR_FAC_SCSI, TR_SDDONE_START, "sddone_start: un %x", un);

	mutex_enter(SD_MUTEX);
	dp = &un->un_utab;
	if (bp == dp->b_forw) {
		dp->b_forw = NULL;
	}

	if (un->un_stats) {
		u_long n_done = bp->b_bcount - bp->b_resid;
		if (bp->b_flags & B_READ) {
			IOSP->reads++;
			IOSP->nread += n_done;
		} else {
			IOSP->writes++;
			IOSP->nwritten += n_done;
		}
		kstat_runq_exit(IOSP);
	}


	/*
	 * Start the next one before releasing resources on this one
	 */
	if (dp->b_actf && (un->un_ncmds < un->un_throttle) &&
	    (dp->b_forw == NULL && !un->un_suspended)) {
		sdstart(un);
	}

	mutex_exit(SD_MUTEX);

	if (bp != un->un_sbufp) {
		scsi_destroy_pkt(BP_PKT(bp));
		SD_DEBUG2(SD_DEVINFO, sd_label, SCSI_DEBUG,
		    "regular done: resid %d\n", bp->b_resid);
	} else {
		ASSERT(un->un_sbuf_busy);
	}
	TRACE_0(TR_FAC_SCSI, TR_SDDONE_BIODONE_CALL, "sddone_biodone_call");
	biodone(bp);

	drv_getparm(TIME, &un->un_timestamp);
	TRACE_0(TR_FAC_SCSI, TR_SDDONE_END, "sddone end");
}


/*
 * reset the disk unless the transport layer has already
 * cleared the problem
 */
#define	C1	(STAT_BUS_RESET|STAT_DEV_RESET|STAT_ABORTED)
static void
sd_reset_disk(struct scsi_disk *un, struct scsi_pkt *pkt)
{
	if ((pkt->pkt_statistics & C1) == 0) {
		mutex_exit(SD_MUTEX);
		if (!(scsi_reset(ROUTE, RESET_TARGET)))
			(void) scsi_reset(ROUTE, RESET_ALL);
		mutex_enter(SD_MUTEX);
	}
}

static int
sd_handle_incomplete(struct scsi_disk *un, struct buf *bp)
{
	static char *fail = "SCSI transport failed: reason '%s': %s\n";
	static char *notresp = "disk not responding to selection\n";
	register rval = COMMAND_DONE_ERROR;
	register struct scsi_pkt *pkt = BP_PKT(bp);
	int be_chatty = !un->un_suspended &&
	    (bp != un->un_sbufp || !(pkt->pkt_flags & FLAG_SILENT));
	int perr = (pkt->pkt_statistics & STAT_PERR);

	ASSERT(mutex_owned(SD_MUTEX));

	switch (pkt->pkt_reason) {

	case CMD_UNX_BUS_FREE:
		/*
		 * If we had a parity error that caused the target to
		 * drop BSY*, don't be chatty about it.
		 */

		if (perr && be_chatty)
			be_chatty = 0;
		break;
	case CMD_TAG_REJECT:
		pkt->pkt_flags = 0;
		un->un_tagflags = 0;
		if (un->un_dp->options & SD_QUEUEING) {
			un->un_throttle = 3;
		} else {
			un->un_throttle = 1;
		}
		(void) scsi_ifsetcap(ROUTE, "tagged-qing", 0, 1);
		rval = QUE_COMMAND;
		break;
	case CMD_INCOMPLETE:
		/*
		 * selection did not complete; we don't want to go
		 * thru a bus reset for this reason
		 */
		if (pkt->pkt_state == STATE_GOT_BUS) {
			break;
		}
		/*FALLTHROUGH*/
	case CMD_TIMEOUT:
	default:
		/*
		 * the target may still be running the	command,
		 * so we should try and reset that target.
		 */
		sd_reset_disk(un, pkt);
		break;
	}

	if ((pkt->pkt_reason == CMD_RESET) || (pkt->pkt_statistics &
	    (STAT_BUS_RESET | STAT_DEV_RESET))) {
		if ((un->un_resvd_status & SD_RESERVE) == SD_RESERVE) {
			un->un_resvd_status |=
			    (SD_LOST_RESERVE | SD_WANT_RESERVE);
			SD_DEBUG2(SD_DEVINFO, sd_label, CE_WARN,
			    "Lost Reservation\n");
		}
	}

	if (bp == un->un_sbufp && (pkt->pkt_flags & FLAG_ISOLATE)) {
		rval = COMMAND_DONE_ERROR;
	} else {
		if ((int)PKT_GET_RETRY_CNT(pkt) < sd_retry_count) {
			PKT_INCR_RETRY_CNT(pkt, 1);
			rval = QUE_COMMAND;
		}
	}

	if (pkt->pkt_state == STATE_GOT_BUS && rval == COMMAND_DONE_ERROR) {
		/*
		 * Looks like someone turned off this shoebox.
		 */
		scsi_log(SD_DEVINFO, sd_label, CE_WARN, notresp);
		New_state(un, SD_STATE_DETACHING);
	} else if (be_chatty) {
		/*
		 * suppress messages if they are all the same pkt reason;
		 * with TQ, many (up to 256) are returned with the same
		 * pkt_reason
		 * if we are in panic, then suppress the retry messages
		 */
		int in_panic = ddi_in_panic();
		if (!in_panic || (rval == COMMAND_DONE_ERROR)) {
			if ((pkt->pkt_reason != un->un_last_pkt_reason) ||
			    (rval == COMMAND_DONE_ERROR) ||
			    (sd_error_level == SCSI_ERR_ALL)) {
				scsi_log(SD_DEVINFO, sd_label, CE_WARN,
				    fail, scsi_rname(pkt->pkt_reason),
				    (rval == COMMAND_DONE_ERROR) ?
				    "giving up": "retrying command");
				SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG,
				    "retrycount=%x\n",
				    PKT_GET_RETRY_CNT(pkt));
			}
		}
	}
	return (rval);
}

static int
sd_handle_sense(struct scsi_disk *un, struct buf *bp)
{
	struct scsi_pkt *rqpkt;

	ASSERT(mutex_owned(SD_MUTEX));

	rqpkt = un->un_rqs;

	/*
	 * reset the retry count in rqsense packet
	 */
	PKT_SET_RETRY_CNT(rqpkt, 0);

	return (sd_decode_sense(un, bp, SCBP(rqpkt),
	    rqpkt->pkt_state, (u_char) rqpkt->pkt_resid));
}

static int
sd_handle_autosense(struct scsi_disk *un, struct buf *bp)
{
	struct scsi_pkt *pkt = BP_PKT(bp);
	struct scsi_arq_status *arqstat;

	ASSERT(mutex_owned(SD_MUTEX));

	ASSERT(pkt != 0);
	arqstat = (struct scsi_arq_status *)(pkt->pkt_scbp);

	/*
	 * if ARQ failed, then retry original cmd
	 */
	if (arqstat->sts_rqpkt_reason != CMD_CMPLT) {
		int rval;

		sd_reset_disk(un, pkt);

		if ((int)PKT_GET_RETRY_CNT(pkt) < sd_retry_count) {
			PKT_INCR_RETRY_CNT(pkt, 1);
			rval = QUE_COMMAND;
		} else {
			rval = COMMAND_DONE_ERROR;
		}
		return (rval);
	}

	bcopy((caddr_t)&arqstat->sts_sensedata, (caddr_t)SD_RQSENSE,
	    SENSE_LENGTH);

	return (sd_decode_sense(un, bp, &arqstat->sts_rqpkt_status,
	    arqstat->sts_rqpkt_state, arqstat->sts_rqpkt_resid));
}


static int
sd_decode_sense(struct scsi_disk *un, struct buf *bp,
    struct scsi_status *statusp, u_long state, u_char resid)
{
	register struct diskhd *dp = &un->un_utab;
	struct scsi_pkt *pkt;
	register rval = COMMAND_DONE_ERROR;
	int severity, amt, i;
	char *p;
	static char *hex = " 0x%x";
	char *rq_err_msg = 0;
	int partition = SDPART(bp->b_edev);
	daddr_t req_blkno;

	if (bp != 0) {
		pkt = BP_PKT(bp);
	} else {
		pkt = 0;
	}
	/*
	 * For uscsi commands, squirrel away a copy of the
	 * results of the Request Sense
	 */
	if (bp == un->un_sbufp) {
		struct uscsi_cmd *ucmd = (struct uscsi_cmd *)bp->b_forw;
		ucmd->uscsi_rqstatus = *(u_char *)statusp;
		if (ucmd->uscsi_rqlen && un->un_srqbufp) {
			u_char rqlen = SENSE_LENGTH - resid;
			rqlen = min(rqlen, ucmd->uscsi_rqlen);
			ucmd->uscsi_rqresid = ucmd->uscsi_rqlen - rqlen;
			bcopy((caddr_t)SD_RQSENSE,
				un->un_srqbufp, SENSE_LENGTH);
			SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG,
				"sd_decode_sense: stat=0x%x resid=0x%x\n",
				ucmd->uscsi_rqstatus, ucmd->uscsi_rqresid);
		}
	}

	if (STATUS_SCBP_C(statusp) == STATUS_RESERVATION_CONFLICT) {
		return (sd_handle_resv_conflict(un, bp));
	} else if (statusp->sts_busy) {
		scsi_log(SD_DEVINFO, sd_label, CE_WARN,
		    "Busy Status on REQUEST SENSE\n");

		if (pkt && (int)PKT_GET_RETRY_CNT(pkt) < sd_retry_count) {
			PKT_INCR_RETRY_CNT(pkt, 1);
			rval = QUE_COMMAND;
		}
		return (rval);
	}

	if (statusp->sts_chk) {
		/*
		 * If the request sense failed, for any of a number
		 * of reasons, allow the original command to be
		 * retried.  Only log error on our last gasp.
		 */
		rq_err_msg = "Check Condition on REQUEST SENSE\n";
sense_failed:
		if ((int)PKT_GET_RETRY_CNT(pkt) < sd_retry_count) {
			PKT_INCR_RETRY_CNT(pkt, 1);
			rval = QUE_COMMAND;
		} else {
			if (rq_err_msg) {
				scsi_log(SD_DEVINFO, sd_label, CE_WARN,
				    rq_err_msg);
			}
		}
		return (rval);
	}

	amt = SENSE_LENGTH - resid;
	if ((state & STATE_XFERRED_DATA) == 0 || amt == 0) {
		rq_err_msg = "Request Sense couldn't get sense data\n";
		goto sense_failed;
	}

	/*
	 * Now, check to see whether we got enough sense data to make any
	 * sense out if it (heh-heh).
	 */

	if (amt < SUN_MIN_SENSE_LENGTH) {
		rq_err_msg = "Not enough sense information\n";
		goto sense_failed;
	}

	if (SD_RQSENSE->es_class != CLASS_EXTENDED_SENSE) {
		auto char tmp[8];
		auto char buf[128];
		p = (char *)SD_RQSENSE;
		strcpy(buf, "undecodable sense information:");
		for (i = 0; i < amt; i++) {
			(void) sprintf(tmp, hex, *(p++)&0xff);
			strcpy(&buf[strlen(buf)], tmp);
		}
		i = strlen(buf);
		strcpy(&buf[i], "-(assumed fatal)\n");
		scsi_log(SD_DEVINFO, sd_label, CE_WARN, buf);
		return (rval);
	}

	/*
	 * use absolute block number for request block
	 */
	if (un->un_gvalid) {
		req_blkno = dkblock(bp) + un->un_offset[partition];
	} else {
		req_blkno = dkblock(bp);
	}

	if (SD_RQSENSE->es_valid) {
		un->un_err_blkno =
		    (SD_RQSENSE->es_info_1 << 24) |
		    (SD_RQSENSE->es_info_2 << 16) |
		    (SD_RQSENSE->es_info_3 << 8)  |
		    (SD_RQSENSE->es_info_4);
	} else {
		/*
		 * With the valid bit not being set, we have
		 * to figure out by hand as close as possible
		 * what the real block number might have been
		 */
		un->un_err_blkno = req_blkno;
	}

	if (DEBUGGING || sd_error_level == SCSI_ERR_ALL) {
		if (pkt) {
			clean_print(SD_DEVINFO, sd_label, CE_NOTE,
			    "Failed CDB", (char *)pkt->pkt_cdbp, CDB_SIZE);
			clean_print(SD_DEVINFO, sd_label, CE_NOTE,
			    "Sense Data", (char *)SD_RQSENSE, SENSE_LENGTH);
		}
	}

	switch (SD_RQSENSE->es_key) {
	case KEY_NOT_READY:
		/*
		 * If we get a not-ready indication, wait a bit and
		 * try it again. Some drives pump this out for about
		 * 2-3 seconds after a reset.
		 */
		sd_not_ready(un, pkt);
		/*
		 * do not retry this too many times; this causes long
		 * hangs in format when the drive refuses to spin up (very
		 * common failure)
		 */
		if (pkt && (int)PKT_GET_RETRY_CNT(pkt) < sd_retry_count/2) {
			PKT_INCR_RETRY_CNT(pkt, 1);
			severity = SCSI_ERR_RETRYABLE;
			if (dp->b_forw == NULL || dp->b_forw == bp) {
				dp->b_forw = bp;
				(void) timeout(sdrestart,
				    (caddr_t)un, SD_BSY_TIMEOUT);
			} else if (dp->b_forw != bp) {
				/*
				 * put it back on the queue
				 */
				bp->b_actf = dp->b_actf;
				dp->b_actf = bp;
				if (un->un_stats)
					kstat_runq_back_to_waitq(IOSP);
			}
			rval = JUST_RETURN;
			return (rval);
		} else {
			rval = COMMAND_DONE_ERROR;
			severity = SCSI_ERR_FATAL;
		}
		break;

	case KEY_NO_SENSE:
		if (pkt && (int)PKT_GET_RETRY_CNT(pkt) < sd_retry_count) {
			PKT_INCR_RETRY_CNT(pkt, 1);
			severity = SCSI_ERR_RETRYABLE;
			rval = QUE_COMMAND;
		} else {
			rval = COMMAND_DONE_ERROR;
			severity = SCSI_ERR_FATAL;
		}
		break;

	case KEY_RECOVERABLE_ERROR:

		severity = SCSI_ERR_RECOVERED;
		rval = COMMAND_DONE;
		break;

	case KEY_HARDWARE_ERROR:
	case KEY_MEDIUM_ERROR:
		/*
		 * This really ought to be a fatal error, but we'll retry
		 * it upon a read, as some drives seem to report spurious
		 * media errors.
		 */
		if (bp != un->un_sbufp && (bp->b_flags & B_READ) != 0) {
			if (pkt &&
			    (int)PKT_GET_RETRY_CNT(pkt) < sd_retry_count) {
				PKT_INCR_RETRY_CNT(pkt, 1);
				rval = QUE_COMMAND;
				severity = SCSI_ERR_RETRYABLE;
				break;
			}
		}
		/*FALLTHROUGH*/
	case KEY_MISCOMPARE:
	case KEY_VOLUME_OVERFLOW:
	case KEY_WRITE_PROTECT:
	case KEY_BLANK_CHECK:
	case KEY_ILLEGAL_REQUEST:
totally_bad:
		severity = SCSI_ERR_FATAL;
		rval = COMMAND_DONE_ERROR;
		break;

	case KEY_ABORTED_COMMAND:
		severity = SCSI_ERR_RETRYABLE;
		rval = QUE_COMMAND;
		break;
	case KEY_UNIT_ATTENTION:
		rval = QUE_COMMAND;
		severity = SCSI_ERR_INFO;
		if (ISREMOVABLE(un)) {
			int ua_retry_count = 0;
			while (ua_retry_count++ < SD_UNIT_ATTENTION_RETRY) {
				if (sd_handle_ua(un) == 0) {
					break;
				}
				drv_usecwait(500000);
			}
			if (ua_retry_count == SD_UNIT_ATTENTION_RETRY) {
				rval = COMMAND_DONE_ERROR;
				severity = SCSI_ERR_FATAL;
				scsi_log(SD_DEVINFO, sd_label, CE_WARN,
				    "failed to handle UA\n");
				break;
			}
		} else {
			if (SD_RQSENSE->es_add_code == 0x29 &&
			    (un->un_resvd_status & SD_RESERVE) == SD_RESERVE) {
				un->un_resvd_status |=
					(SD_LOST_RESERVE | SD_WANT_RESERVE);
				SD_DEBUG(SD_DEVINFO, sd_label, CE_WARN,
				    "sd_decode_sense: Lost Reservation\n");
			}
		}

		break;
	default:
		/*
		 * Undecoded sense key.	 Try retries and hope
		 * that will fix the problem.  Otherwise, we're
		 * dead.
		 */
		scsi_log(SD_DEVINFO, sd_label, CE_WARN,
		    "Unhandled Sense Key '%s\n",
		    sense_keys[SD_RQSENSE->es_key]);
		if (pkt && (int)PKT_GET_RETRY_CNT(pkt) < sd_retry_count) {
			PKT_INCR_RETRY_CNT(pkt, 1);
			severity = SCSI_ERR_RETRYABLE;
			rval = QUE_COMMAND;
		} else {
			severity = SCSI_ERR_FATAL;
			rval = COMMAND_DONE_ERROR;
		}
	}

	if (rval == QUE_COMMAND && bp == un->un_sbufp &&
	    pkt && (pkt->pkt_flags & FLAG_DIAGNOSE)) {
		rval = COMMAND_DONE_ERROR;
	}

	if (bp == un->un_sbufp) {
		if ((pkt && pkt->pkt_flags & FLAG_SILENT) == 0) {
			scsi_errmsg(SD_SCSI_DEVP, pkt, sd_label, severity,
			    req_blkno, un->un_err_blkno, scsi_cmds,
			    SD_RQSENSE);
		}
	} else if (DEBUGGING || severity >= sd_error_level) {
		scsi_errmsg(SD_SCSI_DEVP, pkt, sd_label, severity,
		    req_blkno, un->un_err_blkno, scsi_cmds, SD_RQSENSE);
	}
	return (rval);
}

static int
sd_check_error(struct scsi_disk *un, struct buf *bp)
{
	struct diskhd *dp = &un->un_utab;
	register struct scsi_pkt *pkt = BP_PKT(bp);
	register action;

	TRACE_0(TR_FAC_SCSI, TR_SD_CHECK_ERROR_START, "sd_check_error_start");
	ASSERT(mutex_owned(SD_MUTEX));

	if (SCBP_C(pkt) == STATUS_RESERVATION_CONFLICT) {
		return (sd_handle_resv_conflict(un, bp));
	} else if (SCBP(pkt)->sts_busy) {
		if (SCBP(pkt)->sts_scsi2) {
			/*
			 * Queue Full status
			 *
			 * We will get this if the HBA doesn't handle queue full
			 * condition. This is mainly for third parties because
			 * Sun's HBA handle queue full and target will never
			 * see this case.
			 *
			 * This handling is similar to TRAN_BUSY handling.
			 *
			 * If there are some command already in the transport,
			 * then the queue full is because the queue for this
			 * nexus is actually full. If there are no commands in
			 * the transport then the queue full is because
			 * some other initiator or lun is consuming all the
			 * resources at the target.
			 */
			sd_handle_tran_busy(bp, dp, un);
			action = JUST_RETURN;
		} else {
			if ((int)PKT_GET_RETRY_CNT(pkt) < sd_retry_count) {
				PKT_INCR_RETRY_CNT(pkt, 1);
				if (dp->b_forw == NULL || dp->b_forw == bp) {
					dp->b_forw = bp;
					(void) timeout(sdrestart,
						(caddr_t)un, SD_BSY_TIMEOUT);
				} else if (dp->b_forw != bp) {
					/*
					 * put it back on the queue
					 */
					bp->b_actf = dp->b_actf;
					dp->b_actf = bp;
					if (un->un_stats)
					kstat_runq_back_to_waitq(IOSP);
				}
				action = JUST_RETURN;
			} else {
				scsi_log(SD_DEVINFO, sd_label, CE_WARN,
				    "device busy too long\n");
				mutex_exit(SD_MUTEX);
				if (scsi_reset(ROUTE, RESET_TARGET)) {
					action = QUE_COMMAND;
				} else if (scsi_reset(ROUTE, RESET_ALL)) {
					action = QUE_COMMAND;
				} else {
					action = COMMAND_DONE_ERROR;
				}
				mutex_enter(SD_MUTEX);
			}
		}
	} else if (SCBP(pkt)->sts_chk) {
		action = QUE_SENSE;
	}
	TRACE_0(TR_FAC_SCSI, TR_SD_CHECK_ERROR_END, "sd_check_error_end");
	return (action);
}


/*
 *	System Crash Dump routine
 */

#define	NDUMP_RETRIES	5

static int
sddump(dev_t dev, caddr_t addr, daddr_t blkno, int nblk)
{
/* NO OTHER THREADS ARE RUNNING */

	struct dk_map *lp;
	struct scsi_pkt *pkt;
	register i, pflag, r;
	struct buf local, *bp;
	int err;

	GET_SOFT_STATE(dev);

	if (!un->un_gvalid || ISCD(un))
		return (ENXIO);

	lp = &un->un_map[part];
	if (blkno+nblk > lp->dkl_nblk) {
		return (EINVAL);
	}

	/*
	 * The first time through, reset the specific target device.
	 * Do a REQUEST SENSE if required.
	 */

	pflag = FLAG_NOINTR|FLAG_NODISCON;
	if (un->un_dp->options & SD_NOPARITY)
		pflag |= FLAG_NOPARITY;
	un->un_rqs->pkt_flags |= pflag;

	/*
	 * When cpr calls sddump, we know that sd is in a
	 * a good state, so no bus reset is required
	 */
	un->un_throttle = 0;

	if (un->un_state != SD_STATE_DUMPING && !un->un_suspended) {

		New_state(un, SD_STATE_DUMPING);

		/*
		 * Reset the bus. I'd like to not have to do this,
		 * but this is the safest thing to do...
		 */

		if (scsi_reset(ROUTE, RESET_ALL) == 0) {
			return (EIO);
		}

		for (i = 0; i < NDUMP_RETRIES; i++) {
			(void) sd_clear_cont_alleg(un, un->un_rqs);
			if (SCBP_C(un->un_rqs) == STATUS_GOOD)
				break;
			drv_usecwait(10000);
		}
		if (i >= NDUMP_RETRIES) {
			return (EIO);
		}
	}

	blkno += (lp->dkl_cylno * un->un_g.dkg_nhead * un->un_g.dkg_nsect);

	/*
	 * It should be safe to call the allocator here without
	 * worrying about being locked for DVMA mapping because
	 * the address we're passed is already a DVMA mapping
	 *
	 * We are also not going to worry about semaphore ownership
	 * in the dump buffer. Dumping is single threaded at present.
	 */

	bp = &local;
	bzero((caddr_t)bp, sizeof (*bp));
	bp->b_flags = B_BUSY;
	bp->b_un.b_addr = addr;
	bp->b_bcount = nblk << DEV_BSHIFT;
	bp->b_resid = 0;

	for (i = 0; i < NDUMP_RETRIES; i++) {
		bp->b_flags &= ~B_ERROR;
		if ((pkt = scsi_init_pkt(ROUTE, NULL, bp, CDB_GROUP1, 1, PP_LEN,
		    0, NULL_FUNC, NULL)) != NULL) {
			break;
		}
		if (i == 0) {
			if (bp->b_flags & B_ERROR) {
				scsi_log(SD_DEVINFO, sd_label, CE_WARN,
					"no resources for dumping; "
					"error code: 0x%x, retrying",
					geterror(bp));
			} else {
				scsi_log(SD_DEVINFO, sd_label, CE_WARN,
					"no resources for dumping; retrying");
			}
		} else if (i != (NDUMP_RETRIES - 1)) {
			if (bp->b_flags & B_ERROR) {
				scsi_log(SD_DEVINFO, sd_label, CE_CONT,
				"no resources for dumping; error code: 0x%x, "
				"retrying\n", geterror(bp));
			}
		} else {
			if (bp->b_flags & B_ERROR) {
				scsi_log(SD_DEVINFO, sd_label, CE_CONT,
					"no resources for dumping; "
					"error code: 0x%x, retries failed, "
					"giving up.\n", geterror(bp));
			} else {
				scsi_log(SD_DEVINFO, sd_label, CE_CONT,
					"no resources for dumping; "
					"retries failed, giving up.\n");
			}
			return (EIO);
		}
		drv_usecwait(10000);
	}

	if (blkno >= (1<<20)) {
		makecom_g1(pkt, SD_SCSI_DEVP, pflag, SCMD_WRITE_G1,
		    (int)blkno, nblk);
	} else {
		makecom_g0(pkt, SD_SCSI_DEVP, pflag, SCMD_WRITE,
		    (int)blkno, (u_char)nblk);
	}

	for (err = EIO, i = 0; i < NDUMP_RETRIES && err == EIO; i++) {
		if (sd_scsi_poll(pkt)) {
			if (scsi_reset(ROUTE, RESET_ALL) == 0) {
				break;
			}
			for (r = 0; r < NDUMP_RETRIES; r++) {
				(void) sd_clear_cont_alleg(un, un->un_rqs);
				if (SCBP_C(un->un_rqs) == STATUS_GOOD)
					break;
				drv_usecwait(10000);
			}
			if (r >= NDUMP_RETRIES) {
				return (EIO);
			}
		}
		switch (SCBP_C(pkt)) {
		case STATUS_GOOD:
			err = 0;
			break;
		case STATUS_BUSY:
			drv_usecwait(5*1000000);
			break;
		case STATUS_CHECK:
			/*
			* Hope this clears it up...
			*/
			(void) sd_clear_cont_alleg(un, un->un_rqs);
			break;
		default:
			break;
		}
	}
	scsi_destroy_pkt(pkt);
	return (err);
}

/*
 * This routine implements the ioctl calls.  It is called
 * from the device switch at normal priority.
 */
/* ARGSUSED3 */
static int
sdioctl(dev_t dev, int cmd, int arg, int flag, cred_t *cred_p, int *rval_p)
{
	auto long data[512 / (sizeof (long))];
	struct dk_cinfo *info;
	struct vtoc vtoc;
	struct uscsi_cmd *scmd;
	char cdb[CDB_GROUP0];
	int i, err;
	u_short write_reinstruct;
	enum uio_seg uioseg;
	enum dkio_state state;

	GET_SOFT_STATE(dev);

	if (un->un_state == SD_STATE_DETACHING)
		return (ENXIO);
	if (un->un_suspended)
		ddi_dev_is_needed(SD_DEVINFO, 0, 1);
	drv_getparm(TIME, &un->un_timestamp);

	bzero((caddr_t)data, sizeof (data));


	switch (cmd) {

#ifdef SDDEBUG
/*
 * Following ioctl are for testing RESET/ABORTS
 */
#define	DKIOCRESET	(DKIOC|14)
#define	DKIOCABORT	(DKIOC|15)

	case DKIOCRESET:
		if (ddi_copyin((caddr_t)arg, (caddr_t)data, 4, flag))
			return (EFAULT);
		SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG,
			"DKIOCRESET: data = 0x%x\n", data[0]);
		if (scsi_reset(ROUTE, data[0])) {
			return (0);
		} else {
			return (EIO);
		}
	case DKIOCABORT:
		SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG,
			"DKIOCABORT:\n");
		if (scsi_abort(ROUTE, (struct scsi_pkt *)0)) {
			return (0);
		} else {
			return (EIO);
		}
#endif

	case DKIOCINFO:
		/*
		 * Controller Information
		 */
		info = (struct dk_cinfo *)data;
		switch (un->un_dp->ctype) {
		case CTYPE_MD21:
			info->dki_ctype = DKC_MD21;
			break;
		case CTYPE_CDROM:
			info->dki_ctype = DKC_CDROM;
			break;
		default:
			info->dki_ctype = DKC_SCSI_CCS;
			break;
		}
		info->dki_cnum = ddi_get_instance(ddi_get_parent(SD_DEVINFO));
		strcpy(info->dki_cname,
		    ddi_get_name(ddi_get_parent(SD_DEVINFO)));
		/*
		 * Unit Information
		 */
		info->dki_unit = ddi_get_instance(SD_DEVINFO);
		info->dki_slave = (Tgt(SD_SCSI_DEVP)<<3) | Lun(SD_SCSI_DEVP);
		strcpy(info->dki_dname, ddi_get_name(SD_DEVINFO));
		info->dki_flags = DKI_FMTVOL;
		info->dki_partition = SDPART(dev);

		/*
		 * Max Transfer size of this device
		 * I am using max phys here because that is what sdmin() is
		 * using.
		 */
		info->dki_maxtransfer = maxphys / DEV_BSIZE;

		/*
		 * We can't get from here to there yet
		 */
		info->dki_addr = 0;
		info->dki_space = 0;
		info->dki_prio = 0;
		info->dki_vec = 0;

		i = sizeof (struct dk_cinfo);
		if (ddi_copyout((caddr_t)data, (caddr_t)arg, i, flag))
			return (EFAULT);
		else
			return (0);

	case DKIOCGGEOM:
	/*
	 * Return the geometry of the specified unit.
	 */
		mutex_enter(SD_MUTEX);
		if (un->un_gvalid == 0) {
			sd_validate_geometry(un, SLEEP_FUNC);
		}
		if (un->un_gvalid == 0) {
			mutex_exit(SD_MUTEX);
			return (EINVAL);
		}
		write_reinstruct = un->un_g.dkg_write_reinstruct;

		i = sizeof (struct dk_geom);

		if (un->un_g.dkg_write_reinstruct == 0)
			un->un_g.dkg_write_reinstruct =
			    (int)((int)(un->un_g.dkg_nsect * un->un_g.dkg_rpm *
			    sd_rot_delay) / (int)60000);

		err = ddi_copyout((caddr_t)&un->un_g, (caddr_t)arg, i, flag);

		un->un_g.dkg_write_reinstruct = write_reinstruct;
		mutex_exit(SD_MUTEX);

		if (err)
			return (EFAULT);
		else
			return (0);

	case DKIOCSGEOM:
	/*
	 * Set the geometry of the specified unit.
	 */

		i = sizeof (struct dk_geom);
		if (ddi_copyin((caddr_t)arg, (caddr_t)data, i, flag))
			return (EFAULT);
		mutex_enter(SD_MUTEX);
		bcopy((caddr_t)data, (caddr_t)&un->un_g, i);
		for (i = 0; i < NDKMAP; i++) {
			struct dk_map *lp  = &un->un_map[i];
			un->un_offset[i] =
			    un->un_g.dkg_nhead *
			    un->un_g.dkg_nsect *
			    lp->dkl_cylno;
		}
		mutex_exit(SD_MUTEX);
		return (0);

	case DKIOCGAPART:
	/*
	 * Return the map for all logical partitions.
	 */

		i = NDKMAP * sizeof (struct dk_map);
		if (ddi_copyout((caddr_t)un->un_map, (caddr_t)arg, i, flag))
			return (EFAULT);
		else
			return (0);


	case DKIOCSAPART:
	/*
	 * Set the map for all logical partitions.  We lock
	 * the priority just to make sure an interrupt doesn't
	 * come in while the map is half updated.
	 */

		i = NDKMAP * sizeof (struct dk_map);
		if (ddi_copyin((caddr_t)arg, (caddr_t)data, i, flag))
			return (EFAULT);
		mutex_enter(SD_MUTEX);
		bcopy((caddr_t)data, (caddr_t)un->un_map, i);
		for (i = 0; i < NDKMAP; i++) {
			struct dk_map *lp  = &un->un_map[i];
			un->un_offset[i] =
			    un->un_g.dkg_nhead *
			    un->un_g.dkg_nsect *
			    lp->dkl_cylno;
		}
		mutex_exit(SD_MUTEX);
		return (0);

	case DKIOCGVTOC:
		mutex_enter(SD_MUTEX);
		/*
		 * Get the label (vtoc, geometry and partition map) directly
		 * from the disk, in case if it got modified by another host
		 * sharing the disk in a multi initiator configuration.
		 */
		sd_validate_geometry(un, SLEEP_FUNC);
		if (un->un_gvalid == 0) {
			mutex_exit(SD_MUTEX);
			return (EINVAL);
		}
		sd_build_user_vtoc(un, &vtoc);
		mutex_exit(SD_MUTEX);

		i = sizeof (struct vtoc);
		if (ddi_copyout((caddr_t)&vtoc, (caddr_t)arg, i, flag))
			return (EFAULT);
		else
			return (0);

	case DKIOCSVTOC:
		if (ddi_copyin((caddr_t)arg, (caddr_t)&vtoc,
		    sizeof (struct vtoc), flag))
			return (EFAULT);

		mutex_enter(SD_MUTEX);
		if (un->un_g.dkg_ncyl == 0) {
			mutex_exit(SD_MUTEX);
			return (EINVAL);
		}
		if ((i = sd_build_label_vtoc(un, &vtoc)) == 0) {
			if ((i = sd_write_label(dev)) == 0) {
				sd_validate_geometry(un, SLEEP_FUNC);
			}
		}
		mutex_exit(SD_MUTEX);
		return (i);

	case DKIOCLOCK:
		return (sd_lock_unlock(dev, SD_REMOVAL_PREVENT));

	case DKIOCUNLOCK:
		return (sd_lock_unlock(dev, SD_REMOVAL_ALLOW));

	case DKIOCSTATE:
		if (ddi_copyin((caddr_t)arg, (caddr_t)&state, sizeof (int),
		    flag))
			return (EFAULT);

		if ((i = sd_check_media(dev, state))) {
			return (i);
		}

		if (ddi_copyout((caddr_t)&un->un_mediastate, (caddr_t)arg,
		    sizeof (int), flag)) {
				return (EFAULT);
		}
		return (0);


	/*
	 * The Following Four Ioctl's are needed by the HADF Folks
	 */
	case MHIOCENFAILFAST:
		if ((i = drv_priv(cred_p)) != EPERM) {
			int	mh_time;

			if (ddi_copyin((caddr_t)arg,
				(caddr_t)&mh_time, sizeof (int), flag))
				return (EFAULT);
			if (mh_time) {
				mutex_enter(SD_MUTEX);
				un->un_resvd_status |= SD_FAILFAST;
				mutex_exit(SD_MUTEX);
				i = sd_check_mhd(dev, mh_time);
			} else {
				(void) sd_check_mhd(dev, 0);
				mutex_enter(SD_MUTEX);
				un->un_resvd_status &= ~SD_FAILFAST;
				mutex_exit(SD_MUTEX);
			}
		}
		return (i);

	case MHIOCTKOWN:
		if ((i = drv_priv(cred_p)) != EPERM) {
			struct	mhioctkown	*p = NULL;

			if (arg != NULL) {
				if (ddi_copyin((caddr_t)arg, (caddr_t)data,
				    sizeof (struct mhioctkown), flag))
					return (EFAULT);
				p = (struct mhioctkown *)data;
			}
			i = sd_take_ownership(dev, p);
			mutex_enter(SD_MUTEX);
			if (i == 0) {
				un->un_resvd_status |= SD_RESERVE;
				reinstate_resv_delay = (p != NULL &&
				    p->reinstate_resv_delay != 0) ?
				    p->reinstate_resv_delay * 1000 :
				    SD_REINSTATE_RESV_DELAY;
				mutex_exit(SD_MUTEX);
				(void) scsi_reset_notify(ROUTE,
				    SCSI_RESET_NOTIFY,
				    sd_mhd_reset_notify_cb, (caddr_t)un);
			} else {
				un->un_resvd_status &= ~SD_RESERVE;
				mutex_exit(SD_MUTEX);
			}
		}
		return (i);

	case MHIOCRELEASE:
		if ((i = drv_priv(cred_p)) != EPERM) {

		/* LOCK UNNEEDED BECAUSE "Read Only": un->un_sd->sd_mutex */
			if (un->un_resvd_timeid) {
				(void) untimeout(un->un_resvd_timeid);
		/* LOCK NEEDED: un->un_sd->sd_mutex */

				mutex_enter(SD_MUTEX);
				un->un_resvd_timeid = 0;
				mutex_exit(SD_MUTEX);
			}
			if ((i = sd_reserve_release(dev, SD_RELEASE)) == 0) {
				mutex_enter(SD_MUTEX);
				un->un_resvd_status &= ~SD_RESERVE;
				mutex_exit(SD_MUTEX);
			}
			(void) scsi_reset_notify(ROUTE, SCSI_RESET_CANCEL,
				sd_mhd_reset_notify_cb, (caddr_t)un);
		}
		return (i);

	case MHIOCSTATUS:
		if ((i = drv_priv(cred_p)) != EPERM) {
			mutex_enter(SD_MUTEX);
			i = sd_unit_ready(dev);
			mutex_exit(SD_MUTEX);
			if (i != 0 && i != EACCES) {
				return (i);
			} else if (i == EACCES) {
				*rval_p = 1;
			}
			return (0);
		}
		return (i);

	case USCSICMD:
		if (drv_priv(cred_p) != 0) {
			return (EPERM);
		}

		/*
		 * Run a generic ucsi.h command.
		 */
		scmd = (struct uscsi_cmd *)data;
		if (ddi_copyin((caddr_t)arg, (caddr_t)scmd, sizeof (*scmd),
		    flag)) {
			return (EFAULT);
		}
		scmd->uscsi_flags &= ~USCSI_NOINTR;
		uioseg = (flag & FKIOCTL) ? UIO_SYSSPACE : UIO_USERSPACE;

		/* LOCK UNNEEDED BECAUSE "Read Only": un->un_sd->sd_mutex */
		if (un->un_format_in_progress) {
			return (EAGAIN);
		}
		/* LOCK NEEDED: un->un_sd->sd_mutex */

		if (ddi_copyin(scmd->uscsi_cdb, cdb, CDB_GROUP0, flag) == 0 &&
		    cdb[0] == SCMD_FORMAT) {
			mutex_enter(SD_MUTEX);
			un->un_format_in_progress = 1;
			mutex_exit(SD_MUTEX);
			i = sdioctl_cmd(dev, scmd, uioseg, uioseg, uioseg);
			mutex_enter(SD_MUTEX);
			un->un_format_in_progress = 0;
			mutex_exit(SD_MUTEX);
		} else {
			i = sdioctl_cmd(dev, scmd, uioseg, uioseg, uioseg);
		}
		if (ddi_copyout((caddr_t)scmd, (caddr_t)arg, sizeof (*scmd),
		    flag)) {
			if (i != 0)
				i = EFAULT;
		}
		return (i);

	case CDROMPAUSE:	/* no data passed */

		if (!ISCD(un))
			break;
		return (sr_pause_resume(dev, (caddr_t)0));

	case CDROMRESUME:	/* no data passed */

		if (!ISCD(un))
			break;
		return (sr_pause_resume(dev, (caddr_t)1));

	case CDROMPLAYMSF:

		if (!ISCD(un))
			break;
		return (sr_play_msf(dev, (caddr_t)arg, flag));

	case CDROMPLAYTRKIND:

		if (!ISCD(un))
			break;
		return (sr_play_trkind(dev, (caddr_t)arg, flag));

	case CDROMREADTOCHDR:

		if (!ISCD(un))
			break;
		return (sr_read_tochdr(dev, (caddr_t)arg, flag));

	case CDROMREADTOCENTRY:

		if (!ISCD(un))
			break;
		return (sr_read_tocentry(dev, (caddr_t)arg, flag));

	case CDROMSTOP:		/* no data passed */

		if (!ISCD(un))
			break;
		return (sd_start_stop(dev, SD_STOP));

	case CDROMSTART:	/* no data passed */

		if (!ISCD(un))
			break;
		return (sd_start_stop(dev, SD_START));

#ifndef FDEJECT
#define	FDEJECT (('f'<<8)|112)	/* floppy eject */
#endif
	case FDEJECT:	/* for eject command */
	case DKIOCEJECT:
	case CDROMEJECT:

		if (!ISREMOVABLE(un))
			break;
		return (sd_eject(dev));

	case CDROMVOLCTRL:

		if (!ISCD(un))
			break;
		return (sr_volume_ctrl(dev, (caddr_t)arg, flag));

	case CDROMSUBCHNL:

		if (!ISCD(un))
			break;
		return (sr_read_subchannel(dev, (caddr_t)arg, flag));

	case CDROMREADMODE2:

		if (!ISCD(un))
			break;
		return (sr_read_mode2(dev, (caddr_t)arg, flag));

	case CDROMREADMODE1:

		if (!ISCD(un))
			break;
		return (sr_read_mode1(dev, (caddr_t)arg, flag));

#ifdef CDROMREADOFFSET
	case CDROMREADOFFSET:

		if (!ISCD(un))
			break;
		return (sr_read_sony_session_offset(dev, (caddr_t)arg, flag));
#endif

	case CDROMSBLKMODE:
		mutex_enter(SD_MUTEX);
		if (!(un->un_exclopen & (1<<part))) {
			mutex_exit(SD_MUTEX);
			return (EBUSY);
		}
		mutex_exit(SD_MUTEX);
		/*FALLTHROUGH*/

	case CDROMGBLKMODE:
		if (!ISCD(un))
			break;
		return (sr_change_blkmode(dev, cmd, arg, flag));

	case CDROMGDRVSPEED:
	case CDROMSDRVSPEED:
		if (!ISCD(un))
			break;
		return (sr_change_speed(dev, cmd, arg, flag));

	case CDROMCDDA:
		if (!ISCD(un))
			break;
		return (sr_read_cdda(dev, (caddr_t)arg, flag));

	case CDROMCDXA:
		if (!ISCD(un))
			break;
		return (sr_read_cdxa(dev, (caddr_t)arg, flag));

	case CDROMSUBCODE:
		if (!ISCD(un))
			break;
		return (sr_read_all_subcodes(dev, (caddr_t)arg, flag));

	default:
		break;
	}
	return (ENOTTY);
}

/*
 * Run a command for user (from sdioctl) or from someone else in the driver.
 *
 * space is for address space of cdb
 * addr_flag is for address space of the buffer
 */
static int
sdioctl_cmd(dev_t dev, struct uscsi_cmd *in,
    enum uio_seg cdbspace, enum uio_seg dataspace, enum uio_seg rqbufspace)
{
	register struct buf *bp;
	struct scsi_pkt *pkt;
	struct uscsi_cmd *scmd;
	caddr_t cdb;
	int err, rw;
	int rqlen;
	char *krqbuf = NULL;
	int flags = 0;

	GET_SOFT_STATE(dev);
#ifdef lint
	part = part;
#endif /* lint */
	/*
	 * Is this a request to reset the bus?
	 * If so, we need go no further.
	 */
	if (in->uscsi_flags & (USCSI_RESET|USCSI_RESET_ALL)) {
		int flag = ((in->uscsi_flags & USCSI_RESET_ALL)) ?
			RESET_ALL : RESET_TARGET;
		err = (scsi_reset(ROUTE, flag)) ? 0 : EIO;
		SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG,
			"reset %s %s\n",
			(flag == RESET_ALL) ? "all" : "target",
			(err == 0) ? "ok" : "failed");
		return (err);
	}

	/*
	 * In order to not worry about where the uscsi structure
	 * came from (or where the cdb it points to came from)
	 * we're going to make kmem_alloc'd copies of them
	 * here. This will also allow reference to the data
	 * they contain long after this process has gone to
	 * sleep and its kernel stack has been unmapped, etc.
	 */

	scmd = in;
	cdb = kmem_zalloc((size_t)scmd->uscsi_cdblen, KM_SLEEP);
	if (cdbspace == UIO_SYSSPACE) {
		flags |= FKIOCTL;
	}
	if (ddi_copyin(scmd->uscsi_cdb, cdb, (u_int)scmd->uscsi_cdblen,
		flags)) {
		kmem_free(cdb, (size_t)scmd->uscsi_cdblen);
		return (EFAULT);
	}

	scmd = (struct uscsi_cmd *)kmem_alloc(sizeof (*scmd), KM_SLEEP);
	bcopy((caddr_t)in, (caddr_t)scmd, sizeof (*scmd));
	scmd->uscsi_cdb = cdb;
	rw = (scmd->uscsi_flags & USCSI_READ) ? B_READ : B_WRITE;


	/*
	 * Get the 'special' buffer...
	 */
	mutex_enter(SD_MUTEX);
	while (un->un_sbuf_busy) {
		if (cv_wait_sig(&un->un_sbuf_cv, SD_MUTEX) == 0) {
			kmem_free(scmd->uscsi_cdb, (size_t)scmd->uscsi_cdblen);
			kmem_free((caddr_t)scmd, sizeof (*scmd));
			mutex_exit(SD_MUTEX);
			return (EINTR);
		}
	}
	un->un_sbuf_busy = 1;
	bp = un->un_sbufp;
	mutex_exit(SD_MUTEX);

	/*
	 * Initialize Request Sense buffering, if requested.
	 * For user processes, allocate a kernel copy of the sense buffer.
	 */
	if ((scmd->uscsi_flags & USCSI_RQENABLE) &&
			scmd->uscsi_rqlen && scmd->uscsi_rqbuf) {
		if (rqbufspace == UIO_USERSPACE) {
			krqbuf = kmem_alloc(SENSE_LENGTH, KM_SLEEP);
		}
		scmd->uscsi_rqlen = SENSE_LENGTH;
		scmd->uscsi_rqresid = SENSE_LENGTH;
	} else {
		scmd->uscsi_rqlen = 0;
		scmd->uscsi_rqresid = 0;
	}
	un->un_srqbufp = krqbuf;

	/*
	 * Force asynchronous mode, if necessary.  Doing this here
	 * has the unfortunate effect of running other queued
	 * commands async also, but since the main purpose of this
	 * capability is downloading new drive firmware, we can
	 * probably live with it.
	 */
	if (scmd->uscsi_flags & USCSI_ASYNC) {
		if (scsi_ifgetcap(ROUTE, "synchronous", 1) == 1) {
			if (scsi_ifsetcap(ROUTE, "synchronous", 0, 1) == 1) {
				SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG,
					"forced async ok\n");
			} else {
				SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG,
					"forced async failed\n");
				err = EINVAL;
				goto done;
			}
		}
	}

	/*
	 * Re-enable synchronous mode, if requested
	 */
	if (scmd->uscsi_flags & USCSI_SYNC) {
		if (scsi_ifgetcap(ROUTE, "synchronous", 1) == 0) {
			int i = scsi_ifsetcap(ROUTE, "synchronous", 1, 1);
			SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG,
				"re-enabled sync %s\n",
				(i == 1) ? "ok" : "failed");
		}
	}

	/*
	 * If we're going to do actual I/O, let physio do all the right things
	 */
	if (scmd->uscsi_buflen) {
		auto struct iovec aiov;
		auto struct uio auio;
		register struct uio *uio = &auio;

		bzero((caddr_t)&auio, sizeof (struct uio));
		bzero((caddr_t)&aiov, sizeof (struct iovec));
		aiov.iov_base = scmd->uscsi_bufaddr;
		aiov.iov_len = scmd->uscsi_buflen;
		uio->uio_iov = &aiov;

		uio->uio_iovcnt = 1;
		uio->uio_resid = scmd->uscsi_buflen;
		uio->uio_segflg = dataspace;
		uio->uio_loffset = 0;
		uio->uio_fmode = 0;

		/*
		 * Let physio do the rest...
		 */
		bp->av_back = NO_PKT_ALLOCATED;
		bp->b_forw = (struct buf *)scmd;
		err = physio(sdstrategy, bp, dev, rw, sduscsimin, uio);

	} else {
		/*
		* We have to mimic what physio would do here! Argh!
		*/
		bp->av_back = NO_PKT_ALLOCATED;
		bp->b_forw = (struct buf *)scmd;
		bp->b_flags = B_BUSY | rw;
		bp->b_edev = dev;
		bp->b_dev = cmpdev(dev);	/* maybe unnecessary */
		bp->b_bcount = bp->b_blkno = 0;
		(void) sdstrategy(bp);
		err = biowait(bp);
	}

done:
	/*
	 * get the status block, if any, and
	 * release any resources that we had.
	 */

	in->uscsi_status = 0;
	rqlen = scmd->uscsi_rqlen - scmd->uscsi_rqresid;
	rqlen = min(((int)in->uscsi_rqlen), rqlen);
	if ((pkt = BP_PKT(bp)) != NULL) {
		in->uscsi_status = SCBP_C(pkt);
		in->uscsi_resid = bp->b_resid;
		scsi_destroy_pkt(pkt);
		bp->av_back = NO_PKT_ALLOCATED;
		/*
		 * Update the Request Sense status and resid
		 */
		in->uscsi_rqresid = in->uscsi_rqlen - rqlen;
		in->uscsi_rqstatus = scmd->uscsi_rqstatus;
		/*
		 * Copy out the sense data for user processes
		 */
		if (in->uscsi_rqbuf && rqlen && rqbufspace == UIO_USERSPACE) {
			if (copyout(krqbuf, in->uscsi_rqbuf, rqlen)) {
				err = EFAULT;
			}
		}
	}
	SD_DEBUG2(SD_DEVINFO, sd_label, SCSI_DEBUG,
	    "sdioctl_cmd status = 0x%x\n", scmd->uscsi_status);
	if (DEBUGGING && rqlen != 0) {
		int i, n, len;
		char *data = krqbuf;
		scsi_log(SD_DEVINFO, sd_label, SCSI_DEBUG,
			"  rqstatus=0x%x  rqlen=0x%x  rqresid=0x%x\n",
			in->uscsi_rqstatus, in->uscsi_rqlen,
			in->uscsi_rqresid);
		len = (int)in->uscsi_rqlen - in->uscsi_rqresid;
		for (i = 0; i < len; i += 16) {
			n = min(16, len-i);
			clean_print(SD_DEVINFO, sd_label, CE_NOTE,
				"  ", &data[i], n);
		}
	}

	/*
	 * Tell anybody who cares that the buffer is now free
	 */
	mutex_enter(SD_MUTEX);
	un->un_sbuf_busy = 0;
	un->un_srqbufp = NULL;
	cv_signal(&un->un_sbuf_cv);
	mutex_exit(SD_MUTEX);
	if (krqbuf) {
		kmem_free(krqbuf, SENSE_LENGTH);
	}
	kmem_free(scmd->uscsi_cdb, (size_t)scmd->uscsi_cdblen);
	kmem_free((caddr_t)scmd, sizeof (*scmd));
	SD_DEBUG2(SD_DEVINFO, sd_label, SCSI_DEBUG,
	    "sdioctl_cmd returns %d\n", err);
	return (err);
}

/*
 * sdrunout:
 *	the callback function for resource allocation
 *
 * XXX it would be preferable that sdrunout() scans the whole
 *	list for possible candidates for sdstart(); this avoids
 *	that a bp at the head of the list whose request cannot be
 *	satisfied is retried again and again
 */
/*ARGSUSED*/
static int
sdrunout(caddr_t arg)
{
	register int serviced;
	register struct scsi_disk *un;
	register struct diskhd *dp;

	TRACE_1(TR_FAC_SCSI, TR_SDRUNOUT_START, "sdrunout_start: arg %x", arg);
	serviced = 1;

	un = (struct scsi_disk *)arg;
	dp = &un->un_utab;

	/*
	 * We now support passing a structure to the callback
	 * routine.
	 */
	ASSERT(un != NULL);
	mutex_enter(SD_MUTEX);
	if (un->un_state == SD_STATE_RWAIT) {
		if ((un->un_ncmds < un->un_throttle) && (dp->b_forw == NULL)) {
			sdstart(un);
		}
		if (un->un_state == SD_STATE_RWAIT) {
			serviced = 0;
		}
	}
	mutex_exit(SD_MUTEX);
	TRACE_1(TR_FAC_SCSI, TR_SDRUNOUT_END,
	    "sdrunout_end: serviced %d", serviced);
	return (serviced);
}


/*
 * This routine called to see whether unit is (still) there. Must not
 * be called when un->un_sbufp is in use, and must not be called with
 * an unattached disk. Soft state of disk is restored to what it was
 * upon entry- up to caller to set the correct state.
 *
 * We enter with the disk mutex held.
 */

static int
sd_unit_ready(dev_t dev)
{
	auto struct uscsi_cmd scmd, *com = &scmd;
	auto char cmdblk[CDB_GROUP0];
	u_char state, last_state;
	int error;

	GET_SOFT_STATE(dev);
#ifdef lint
	part = part;
#endif /* lint */

	state = un->un_state;
	last_state = un->un_last_state;
	/*
	 * Now that we protect the special buffer with
	 * a mutex, we could probably do a mutex_tryenter
	 * on it here and return failure if it were held...
	 */

	New_state(un, SD_STATE_OPENING);
	bzero(cmdblk, CDB_GROUP0);
	bzero((caddr_t)com, sizeof (struct uscsi_cmd));
	cmdblk[0] = (char)SCMD_TEST_UNIT_READY;
	com->uscsi_flags = USCSI_SILENT|USCSI_WRITE;
	com->uscsi_cdb = cmdblk;
	com->uscsi_cdblen = CDB_GROUP0;

	/*
	 * allow for trying it twice, if the disk is not reserved.
	 */
	mutex_exit(SD_MUTEX);
	error = sdioctl_cmd(dev, com, UIO_SYSSPACE, UIO_SYSSPACE,
		UIO_SYSSPACE);
	if (error == 0)
		goto done;

	if (com->uscsi_status != STATUS_RESERVATION_CONFLICT) {
		error = sdioctl_cmd(dev, com, UIO_SYSSPACE, UIO_SYSSPACE,
			UIO_SYSSPACE);
	}

	if (error && com->uscsi_status == STATUS_RESERVATION_CONFLICT)
		error = EACCES;
done:
	mutex_enter(SD_MUTEX);
	if (un->un_state != SD_STATE_DETACHING) {
		un->un_state = state;
		un->un_last_state = last_state;
	}
	return (error);
}

/*
 * This routine starts the drive, stops the drive or ejects the disc
 */
static int
sd_start_stop(dev_t dev, caddr_t  data)
{
	int r;
	register struct uscsi_cmd *com;
	char	cdb[CDB_GROUP0];

	com = kmem_zalloc(sizeof (*com), KM_SLEEP);
	bzero((caddr_t)cdb, CDB_GROUP0);
	cdb[0] = SCMD_START_STOP;
	cdb[4] = (char)((int)data & 0xff);

	com->uscsi_cdb = cdb;
	com->uscsi_cdblen = CDB_GROUP0;
	com->uscsi_flags = USCSI_DIAGNOSE|USCSI_SILENT;
	com->uscsi_timeout = 90;

	r = sdioctl_cmd(dev, com, UIO_SYSSPACE, UIO_SYSSPACE,
		UIO_SYSSPACE);
	kmem_free((caddr_t)com, sizeof (*com));
	return (r);
}

static int
sr_read_capacity(dev_t dev)
{
	register struct uscsi_cmd	*com;
	register struct scsi_capacity	*capp;
	int				rval;
	char				cdb[CDB_GROUP1];

	GET_SOFT_STATE(dev);
#ifdef lint
	part = part;
#endif /* lint */

	capp = kmem_zalloc(sizeof (struct scsi_capacity), KM_SLEEP);
	com = kmem_zalloc(sizeof (*com), KM_SLEEP);
	bzero((caddr_t)cdb, CDB_GROUP1);

	cdb[0] = SCMD_READ_CAPACITY;

	com->uscsi_cdb = cdb;
	com->uscsi_cdblen = CDB_GROUP1;
	com->uscsi_bufaddr = (caddr_t)capp;
	com->uscsi_buflen = sizeof (struct scsi_capacity);
	com->uscsi_flags = USCSI_SILENT|USCSI_READ;

	rval = sdioctl_cmd(dev, com,
		UIO_SYSSPACE, UIO_SYSSPACE, UIO_SYSSPACE);

	mutex_enter(SD_MUTEX);
	if (rval == 0) {
		un->un_capacity = capp->capacity + 1;
		un->un_lbasize = capp->lbasize;
	} else {
		un->un_gvalid = 0;
	}
	mutex_exit(SD_MUTEX);

	kmem_free((caddr_t)capp, sizeof (struct scsi_capacity));
	kmem_free((caddr_t)com, sizeof (*com));

	return (rval);
}

/*
 * This routine ejects the CDROM disc
 */
static int
sd_eject(dev_t dev)
{
	int	err;

	GET_SOFT_STATE(dev);
#ifdef lint
	part = part;
#endif /* lint */
	if (un->un_state == SD_STATE_DETACHING)
		return (ENXIO);

	/* first, unlocks the eject */
	if ((err = sd_lock_unlock(dev, SD_REMOVAL_ALLOW)) != 0) {
		return (err);
	}

	/* then ejects the disc */
	if ((err = (sd_start_stop(dev, SD_EJECT))) == 0) {
		mutex_enter(SD_MUTEX);
		sd_ejected(un);
		un->un_mediastate = DKIO_EJECTED;
		cv_broadcast(&un->un_state_cv);
		mutex_exit(SD_MUTEX);
	}
	return (err);
}

/*
 * Check Write Protection of a Removable Media Disk
 * We issue a mode sense for page 2, disconnect page, and only want
 * the header; byte 2, bit 8 is the write protect bit
 */
#define	WRITE_PROTECT 0x80

static int
sd_check_wp(dev_t dev)
{
	auto struct uscsi_cmd scmd, *com = &scmd;
	auto char cmdblk[CDB_GROUP0];
	struct mode_header *header;
	int rval;

	header = (struct mode_header *)
	    kmem_zalloc((size_t)MODE_HEADER_LENGTH, KM_SLEEP);
	bzero(cmdblk, CDB_GROUP0);
	cmdblk[0] = SCMD_MODE_SENSE;
	cmdblk[2] = MODEPAGE_DISCO_RECO;
	cmdblk[4] = MODE_HEADER_LENGTH;

	bzero((caddr_t)com, sizeof (struct uscsi_cmd));
	com->uscsi_flags = USCSI_DIAGNOSE|USCSI_SILENT|USCSI_READ;
	com->uscsi_cdb = cmdblk;
	com->uscsi_cdblen = CDB_GROUP0;
	com->uscsi_bufaddr = (caddr_t)header;
	com->uscsi_buflen = MODE_HEADER_LENGTH;
	com->uscsi_timeout = 15;

	if ((sdioctl_cmd(dev, com, UIO_SYSSPACE, UIO_SYSSPACE,
	    UIO_SYSSPACE) == 0) &&
	    (header->device_specific & WRITE_PROTECT)) {
		rval = 1;
	} else {
		/*
		 * Write protect mode sense failed - oh well,
		 * not all disks understand this query.. so presume
		 * those that don't are writable anyway.
		 */
		rval = 0;
	}

	kmem_free(header, MODE_HEADER_LENGTH);
	return (rval);
}


/*
 * Lock or Unlock the door for removable media devices
 */

static int
sd_lock_unlock(dev_t dev, int flag)
{
	auto struct uscsi_cmd scmd, *com = &scmd;
	auto char cmdblk[CDB_GROUP0];

	bzero(cmdblk, CDB_GROUP0);
	bzero((caddr_t)com, sizeof (struct uscsi_cmd));
	cmdblk[0] = SCMD_DOORLOCK;
	cmdblk[4] = flag;
	com->uscsi_flags = USCSI_DIAGNOSE|USCSI_SILENT|USCSI_WRITE;
	com->uscsi_cdb = cmdblk;
	com->uscsi_cdblen = CDB_GROUP0;
	com->uscsi_timeout = 15;

	return (sdioctl_cmd(dev, com, UIO_SYSSPACE, UIO_SYSSPACE,
		UIO_SYSSPACE));

}

static int
sd_reserve_release(dev_t dev, int cmd)
{
	struct uscsi_cmd		uscsi_cmd;
	register struct uscsi_cmd	*com = &uscsi_cmd;
	register int			rval;
	char				cdb[CDB_GROUP0];

	GET_SOFT_STATE(dev);

#ifdef lint
	part = part;
#endif /* lint */

	SD_DEBUG2(SD_DEVINFO, sd_label, SCSI_DEBUG,
		"sd_reserve_release: Entering ...\n");

	bzero(cdb, CDB_GROUP0);
	if (cmd == SD_RELEASE) {
		cdb[0] = SCMD_RELEASE;
	} else {
		cdb[0] = SCMD_RESERVE;
	}

	bzero((caddr_t)com, sizeof (struct uscsi_cmd));
	com->uscsi_flags = USCSI_SILENT|USCSI_WRITE;
	com->uscsi_cdb = cdb;
	com->uscsi_cdblen = CDB_GROUP0;
	com->uscsi_timeout = 5;

	rval = sdioctl_cmd(dev, com,
		UIO_SYSSPACE, UIO_SYSSPACE, UIO_SYSSPACE);
	if (rval && com->uscsi_status == STATUS_RESERVATION_CONFLICT)
		rval = EACCES;

	SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG,
		"sd_reserve_release: rval(1)=%d\n", rval);

	if (cmd == SD_TKOWN && rval != 0) {
		if (scsi_reset(ROUTE, RESET_TARGET) == 0) {
			if (scsi_reset(ROUTE, RESET_ALL) == 0) {
				return (EIO);
			}
		}

		bzero((caddr_t)com, sizeof (struct uscsi_cmd));
		com->uscsi_flags = USCSI_SILENT|USCSI_WRITE;
		com->uscsi_cdb = cdb;
		com->uscsi_cdblen = CDB_GROUP0;
		com->uscsi_timeout = 5;

		rval = sdioctl_cmd(dev, com,
			UIO_SYSSPACE, UIO_SYSSPACE, UIO_SYSSPACE);
		if (rval && com->uscsi_status == STATUS_RESERVATION_CONFLICT)
			rval = EACCES;

		SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG,
			"sd_reserve_release: rval(2)=%d\n", rval);
	}
	return (rval);
}

/*
 * sd_take_ownership routine implements an algorithm to achieve a stable
 * reservation on the disk and makes sure that other host loses in
 * its attempts of re-reservation.
 * min_ownership_delay is the minimum amount of time for which the disk
 * must be reserved continuously devoid of resets before the MHIOCTKOWN
 * ioctl will return success.  max_ownership_delay indicates the amount of
 * time by which the take ownership should succeed or timeout with an error.
 */
static int
sd_take_ownership(dev_t	dev, struct mhioctkown *p)
{
	int	rval;
	int	err;
	u_long	start_time;	/* starting time of this algorithm */
	u_long	end_time;	/* time limit for giving up */
	u_long	ownership_time;	/* time limit indicating stable ownership */
	u_long	current_time;
	u_long	previous_current_time;
	int	reservation_count = 0;
	int	min_ownership_delay =  6000000; /* in usec */
	int	max_ownership_delay = 30000000; /* in usec */

	GET_SOFT_STATE(dev);

#ifdef lint
	part = part;
#endif /* lint */

	SD_DEBUG2(SD_DEVINFO, sd_label, SCSI_DEBUG,
		"sd_take_ownership: Entering ...\n");

	if ((rval = sd_reserve_release(dev, SD_TKOWN)) != 0) {
		SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG,
			"sd_take_ownership: return(1)=%d\n", rval);
		return (rval);
	}

	mutex_enter(SD_MUTEX);
	un->un_resvd_status |= SD_RESERVE;
	un->un_resvd_status &= ~(SD_LOST_RESERVE | SD_WANT_RESERVE |
		SD_RESERVATION_CONFLICT);
	mutex_exit(SD_MUTEX);

	if (p != NULL) {
		if (p->min_ownership_delay != 0)
			min_ownership_delay = p->min_ownership_delay * 1000;
		if (p->max_ownership_delay != 0)
			max_ownership_delay = p->max_ownership_delay * 1000;
	}
	SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG, "sd_take_ownership: min, "
	    "max delays: %d, %d\n", min_ownership_delay, max_ownership_delay);

	(void) drv_getparm(LBOLT, &start_time);
	current_time = start_time;
	ownership_time = current_time + drv_usectohz(min_ownership_delay);
	end_time = start_time + drv_usectohz(max_ownership_delay);

	while (current_time < end_time) {
		delay(drv_usectohz(500000));

		if ((err = sd_reserve_release(dev, SD_RESERVE)) != 0) {
			if ((sd_reserve_release(dev, SD_RESERVE)) != 0) {
				mutex_enter(SD_MUTEX);
				rval = (un->un_resvd_status &
				    SD_RESERVATION_CONFLICT) ? EACCES : EIO;
				mutex_exit(SD_MUTEX);
				break;
			}
		}
		previous_current_time = current_time;
		(void) drv_getparm(LBOLT, &current_time);
		mutex_enter(SD_MUTEX);
		if (err || (un->un_resvd_status & SD_LOST_RESERVE)) {
			(void) drv_getparm(LBOLT, &ownership_time);
			ownership_time += drv_usectohz(min_ownership_delay);
			reservation_count = 0;
		} else {
			reservation_count++;
		}
		un->un_resvd_status |= SD_RESERVE;
		un->un_resvd_status &= ~(SD_LOST_RESERVE | SD_WANT_RESERVE);
		mutex_exit(SD_MUTEX);

		SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG,
		    "sd_take_ownership: ticks for loop iteration=%d, "
		    "reservation=%s\n", (current_time - previous_current_time),
		    reservation_count ? "ok" : "reclaimed");

		if (current_time >= ownership_time && reservation_count >= 4) {
			rval = 0; /* Wow! Achieved a stable ownership */
			break;
		}
		if (current_time >= end_time) {
			rval = EACCES; /* No ownership in max possible time */
			break;
		}
	}
	SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG,
		"sd_take_ownership: return(2)=%d\n", rval);
	return (rval);
}

/*
 * Re-initialize the unit geometry in case of
 * power down or media change
 */
static int
sd_handle_mchange(struct scsi_disk *un)
{
	int rval = 0;
	struct scsi_capacity cbuf;
	struct scsi_pkt *pkt;
	caddr_t cdb;

	ASSERT(mutex_owned(SD_MUTEX));
	/*
	 * If not a CD or other removeable
	 * media device, do not have to do this
	 */
	if (!(ISREMOVABLE(un)))
		return (0);

	sd_ejected(un);

	/*
	 * First, try to lock the door
	 */
	SD_DEBUG2(SD_DEVINFO, sd_label, SCSI_DEBUG,
	    "locking the drive door\n");
	pkt = scsi_init_pkt(ROUTE, NULL, NULL, CDB_GROUP0, 1, PP_LEN,
	    0, NULL_FUNC, NULL);

	if (!pkt) {
		return (ENOMEM);
	}
	mutex_exit(SD_MUTEX);

	cdb = (caddr_t)CDBP(pkt);
	bzero(cdb, CDB_GROUP0);
	makecom_g0(pkt, SD_SCSI_DEVP, FLAG_NOINTR, SCMD_DOORLOCK, 0, 0);
	cdb[4] = SD_REMOVAL_PREVENT;
	if (sd_scsi_poll(pkt) || SCBP_C(pkt) != STATUS_GOOD) {
		/*
		 * Throw away Request Sense, we should probally check why
		 * we have a problem here.
		 */
		(void) sd_clear_cont_alleg(un, un->un_rqs);
		SD_DEBUG2(SD_DEVINFO, sd_label, SCSI_DEBUG,
		    "Prevent/Allow Medium Removal command failed\n");
		scsi_destroy_pkt(pkt);
		mutex_enter(SD_MUTEX);
		return (EIO);
	}
	scsi_destroy_pkt(pkt);

	if ((rval = sd_read_capacity(un, &cbuf)) == 0) {
		mutex_enter(SD_MUTEX);
		un->un_capacity = cbuf.capacity;
		un->un_lbasize = cbuf.lbasize;
	} else {
		mutex_enter(SD_MUTEX);
		return (rval);
	}

	un->un_gvalid = 0;
	sd_validate_geometry(un, NULL_FUNC);

	if (!un->un_gvalid)
		rval = ENXIO;
	return (rval);
}



/*
 * Handle unit attention in case of
 * media change or power down
 */
static int
sd_handle_ua(struct scsi_disk *un)
{
	int	err;

	ASSERT(mutex_owned(SD_MUTEX));
	SD_DEBUG2(SD_DEVINFO, sd_label, SCSI_DEBUG, "in sd_handle_ua\n");

	/*
	 * power on or reset sense code
	 */
	if (SD_RQSENSE->es_add_code == 0x29) {
		SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG,
		    "power on or reset condition detected\n");
		if (err = sd_handle_mchange(un)) {
			return (err);
		}
		return (0);

	} else if (SD_RQSENSE->es_add_code == 0x28) { /* medium change */
		SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG,
		    "medium might have changed condition detected\n");
		if (err = sd_handle_mchange(un)) {
			return (err);
		} else {
			SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG,
			    "Caddy has just been inserted\n");
			return (0);
		}
	} else {
		return (0);
	}
}

/*
 * Interrupt Service Routines
 */

static void
sdrestart(caddr_t arg)
{
	struct scsi_disk *un = (struct scsi_disk *)arg;
	register struct buf *bp;
	int status;

	SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG, "sdrestart\n");

	mutex_enter(SD_MUTEX);
	bp = un->un_utab.b_forw;
	if (bp) {
		un->un_ncmds++;
	}
	mutex_exit(SD_MUTEX);
	if (bp) {
		register struct scsi_pkt *pkt = BP_PKT(bp);

		if (pkt->pkt_flags & FLAG_SENSING) {
			pkt = un->un_rqs;
		} else {
			pkt->pkt_flags |= FLAG_HEAD;
		}

		if ((status = scsi_transport(pkt)) != TRAN_ACCEPT) {
			mutex_enter(SD_MUTEX);
			un->un_ncmds--;
			if (status == TRAN_BUSY) {
				if (un->un_throttle > 1) {
					ASSERT(un->un_ncmds >= 0);
					un->un_throttle = un->un_ncmds;
					if (un->un_reset_throttle_timeid == 0) {
						un->un_reset_throttle_timeid =
						timeout(sd_reset_throttle,
						(caddr_t)un,
						sd_reset_throttle_timeout *
						drv_usectohz(1000000));
					}
					mutex_exit(SD_MUTEX);
					(void) timeout(sdrestart, (caddr_t)un,
					    SD_BSY_TIMEOUT);
				} else {
					mutex_exit(SD_MUTEX);
					(void) timeout(sdrestart, (caddr_t)un,
					    SD_BSY_TIMEOUT/500);
				}
				return;
			}
			mutex_exit(SD_MUTEX);
			scsi_log(SD_DEVINFO, sd_label, CE_WARN,
			    "sdrestart transport failed (%x)\n", status);
			bp->b_resid = bp->b_bcount;
			bp->b_flags |= B_ERROR;
			sddone(un, bp);

			/*
			 * return from here because sddone
			 * releases the mutex.
			 */
			return;
		}
	}
	SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG, "sdrestart done\n");
}

/*
 * This routine gets called to reset the throttle to its saved
 * value wheneven we lower the throttle.
 */
static void
sd_reset_throttle(caddr_t arg)
{
	struct scsi_disk *un = (struct scsi_disk *)arg;

	mutex_enter(SD_MUTEX);
	SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG,
		"sd_reset_throttle: reseting the throttle from 0x%x to 0x%x\n",
		un->un_throttle, un->un_save_throttle);

	un->un_reset_throttle_timeid = 0;
	un->un_throttle = un->un_save_throttle;
	mutex_exit(SD_MUTEX);
}


/*
 * This routine handles the case when a TRAN_BUSY is
 * returned by HBA.
 *
 * If there are some commands already in the transport, the
 * bp can be put back on queue and it will
 * be retried when the queue is emptied after command
 * completes. But if there is no command in the tranport
 * and it still return busy, we have to retry the command
 * after some time like 10ms.
 */
static void
sd_handle_tran_busy(struct buf *bp, struct diskhd *dp, struct scsi_disk *un)
{
	if (un->un_ncmds > 0) {
		bp->b_actf = dp->b_actf;
		dp->b_actf = bp;
		ASSERT(un->un_throttle >= 0);
		un->un_throttle = un->un_ncmds;
		if (un->un_reset_throttle_timeid == 0) {
			un->un_reset_throttle_timeid =
				timeout(sd_reset_throttle, (caddr_t)un,
				sd_reset_throttle_timeout *
				drv_usectohz(1000000));
		}
		BP_PKT(bp)->pkt_resid = bp->b_resid;
		if (un->un_stats)
			kstat_runq_back_to_waitq(IOSP);
	} else {
		dp->b_forw = bp;
		(void) timeout(sdrestart, (caddr_t)un, SD_BSY_TIMEOUT/500);
	}
}

/*
 * sd_check_media():
 * Check periodically the media using scsi_watch service; this service
 * calls back after TUR and possibly request sense
 * the callback handler (sd_media_watch_cb()) decodes the request sense data
 * (if any)
 */
static int sd_media_watch_cb();

static int
sd_check_media(dev_t dev, enum dkio_state state)
{
	register struct scsi_disk *un;
	register int instance;
	register opaque_t token = NULL;
	int rval = 0;
	enum dkio_state prev_state;

	instance = SDUNIT(dev);
	if ((un = ddi_get_soft_state(sd_state, instance)) == NULL) {
		return (ENXIO);
	}

	mutex_enter(SD_MUTEX);
	SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG,
		"sd_check_media: state=%x, mediastate=%x\n",
		state, un->un_mediastate);

	prev_state = un->un_mediastate;

	/*
	 * is there anything to do?
	 */
	if (state == un->un_mediastate || un->un_mediastate == DKIO_NONE) {
		/*
		 * submit the request to the scsi_watch service;
		 * scsi_media_watch_cb() does the real work
		 */
		mutex_exit(SD_MUTEX);
		token = scsi_watch_request_submit(SD_SCSI_DEVP,
			sd_check_media_time, SENSE_LENGTH,
			sd_media_watch_cb, (caddr_t)dev);
		if (token == NULL) {
			rval = EAGAIN;
			goto done;
		}
		mutex_enter(SD_MUTEX);
		un->un_specified_mediastate = state;

		/*
		 * now wait for media change
		 * we will not be signalled unless mediastate == state but it
		 * still better to test for this condition, since there
		 * is a 2 sec cv_broadcast delay when
		 *  mediastate == DKIO_INSERTED
		 */
		SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG,
			"waiting for media state change\n");
		while (un->un_mediastate == state) {
			if (cv_wait_sig(&un->un_state_cv, SD_MUTEX) == 0) {
				mutex_exit(SD_MUTEX);
				SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG,
				"waiting for media state was interrupted\n");
				rval = EINTR;
				goto done;
			}
			SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG,
			    "received signal, state=%x\n", un->un_mediastate);
		}
	}

	/*
	 * invalidate geometry
	 */
	if (prev_state == DKIO_INSERTED && un->un_mediastate != DKIO_INSERTED)
		sd_ejected(un);
	if (un->un_mediastate == DKIO_INSERTED && prev_state != DKIO_INSERTED) {
		auto struct scsi_capacity capbuf;

		SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG,
			"media inserted\n");
		mutex_exit(SD_MUTEX);
		if ((rval = sd_read_capacity(un, &capbuf))) {
			goto done;
		}
		mutex_enter(SD_MUTEX);
		un->un_capacity = capbuf.capacity;
		un->un_lbasize = capbuf.lbasize;
		un->un_gvalid = 0;
		sd_validate_geometry(un, SLEEP_FUNC);
		mutex_exit(SD_MUTEX);
		rval = sd_lock_unlock(dev, SD_REMOVAL_PREVENT);
		goto done;
	}
	mutex_exit(SD_MUTEX);
done:
	if (token) {
		scsi_watch_request_terminate(token);
	}
	SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG,
		"sd_check_media: done\n");
	return (rval);
}

/*
 * delayed cv_broadcast to allow for target to recover
 * from media insertion
 */
static void
sd_delayed_cv_broadcast(caddr_t arg)
{
	struct scsi_disk *un = (struct scsi_disk *)arg;

	SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG,
		"delayed cv_broadcast\n");

	mutex_enter(SD_MUTEX);
	cv_broadcast(&un->un_state_cv);
	mutex_exit(SD_MUTEX);
}


/*
 * sd_media_watch_cb() is called by scsi_watch_thread for
 * verifying the request sense data (if any)
 */
#define	MEDIA_ACCESS_DELAY 2000000

static int
sd_media_watch_cb(caddr_t arg, struct scsi_watch_result *resultp)
{
	register struct scsi_status *statusp = resultp->statusp;
	register struct scsi_extended_sense *sensep = resultp->sensep;
	u_char actual_sense_length = resultp->actual_sense_length;
	register struct scsi_disk *un;
	enum dkio_state state = DKIO_NONE;
	register int instance;
	dev_t dev = (dev_t)arg;

	instance = SDUNIT(dev);
	if ((un = ddi_get_soft_state(sd_state, instance)) == NULL) {
		return (-1);
	}

	mutex_enter(SD_MUTEX);
	SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG,
		"sd_media_watch_cb: status=%x, sensep=%x, len=%x\n",
			*((char *)statusp), sensep,
			actual_sense_length);

	/*
	 * if there was a check condition then sensep points to valid
	 * sense data
	 * if status was not a check condition but a reservation or busy
	 * status then the new state is DKIO_NONE
	 */
	if (sensep) {
		if (actual_sense_length >= SENSE_LENGTH) {
			if (sensep->es_key == KEY_UNIT_ATTENTION) {
				if (sensep->es_add_code == 0x28) {
					state = DKIO_INSERTED;
				}
			} else if (sensep->es_add_code == 0x3a) {
				state = DKIO_EJECTED;
			}
		}
	} else if ((*((char *)statusp) == STATUS_GOOD) &&
		(resultp->pkt->pkt_reason == CMD_CMPLT)) {
		state = DKIO_INSERTED;
	}
	SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG,
		"state=%x, specified=%x\n",
		state, un->un_specified_mediastate);

	/*
	 * now signal the waiting thread if this is *not* the specified state;
	 * delay the signal if the state is DKIO_INSERTED
	 * to allow the target to recover
	 */
	if (state != un->un_specified_mediastate) {
		un->un_mediastate = state;
		if (state == DKIO_INSERTED) {
			/*
			 * delay the signal to give the drive a chance
			 * to do what it apparently needs to do
			 */
			SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG,
				"delayed cv_broadcast\n");
			(void) timeout(sd_delayed_cv_broadcast,
			    (caddr_t)un,
			    drv_usectohz((clock_t)MEDIA_ACCESS_DELAY));
		} else {
			SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG,
				"immediate cv_broadcast\n");
			cv_broadcast(&un->un_state_cv);
		}
	}
	mutex_exit(SD_MUTEX);
	return (0);
}

/*
 * multi-host disk watch
 * sd_check_mhd() sets up a request; sd_mhd_watch_cb() does the real work
 */

static int
sd_check_mhd(dev_t dev, int interval)
{
	register struct scsi_disk *un;
	register int instance;
	register opaque_t token;

	instance = SDUNIT(dev);
	if ((un = ddi_get_soft_state(sd_state, instance)) == NULL) {
		return (ENXIO);
	}

	SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG,
		"sd_check_mhd: Entering(interval=%x) ...\n", interval);

	if (interval == 0) {
		scsi_watch_request_terminate(un->un_mhd_token);
		return (0);
	}

	/*
	 * adjust minimum time interval to 1 second,
	 * and convert from msecs to usecs
	 */
	if (interval > 0 && interval < 1000)
		interval = 1000;
	interval *= 1000;

	/*
	 * submit the request to the scsi_watch service
	 */
	token = scsi_watch_request_submit(SD_SCSI_DEVP,
		interval, SENSE_LENGTH, sd_mhd_watch_cb, (caddr_t)dev);
	if (token == NULL) {
		return (EAGAIN);
	}

	/*
	 * save token for termination later on
	 */
	un->un_mhd_token = token;
	return (0);
}

static int
sd_mhd_watch_cb(caddr_t arg, struct scsi_watch_result *resultp)
{
	struct scsi_status		*statusp = resultp->statusp;
	struct scsi_extended_sense	*sensep = resultp->sensep;
	struct scsi_pkt			*pkt = resultp->pkt;
	struct scsi_disk		*un;
	int				instance;
	u_char				actual_sense_length =
						resultp->actual_sense_length;
	dev_t				dev = (dev_t)arg;

	instance = SDUNIT(dev);
	if ((un = ddi_get_soft_state(sd_state, instance)) == NULL) {
		return (-1);
	}

	SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG,
		"sd_mhd_watch_cb: reason '%s', status '%s'\n",
		scsi_rname(pkt->pkt_reason),
		sd_sname(*((unsigned char *)statusp)));

	if (pkt->pkt_reason != CMD_CMPLT) {
		sd_mhd_watch_incomplete(un, pkt);
		return (0);

	} else if (*((unsigned char *)statusp) != STATUS_GOOD) {
		if (*((unsigned char *)statusp)
			== STATUS_RESERVATION_CONFLICT) {
			mutex_enter(SD_MUTEX);
			if (un->un_resvd_status & SD_RESERVE) {
				if (sd_failfast_enable) {
					cmn_err(CE_PANIC,
						"Reservation Conflict\n");
				} else {
					SD_DEBUG(SD_DEVINFO, sd_label,
						CE_WARN,
						"sd_mhd_watch_cb: "
						"Reservation Conflict\n");
				}
			}
			un->un_resvd_status |= SD_RESERVATION_CONFLICT;
			mutex_exit(SD_MUTEX);
		}
	}

	if (sensep) {
		if (actual_sense_length >= (SENSE_LENGTH - 2)) {
			mutex_enter(SD_MUTEX);
			if (sensep->es_add_code == 0x29 &&
				(un->un_resvd_status & SD_RESERVE)) {
				un->un_resvd_status |=
					(SD_LOST_RESERVE | SD_WANT_RESERVE);
				SD_DEBUG(SD_DEVINFO, sd_label, CE_WARN,
					"sd_mhd_watch_cb: Lost Reservation\n");
			}
		} else {
			goto done;
		}
	} else {
		mutex_enter(SD_MUTEX);
	}

	if ((un->un_resvd_status & SD_RESERVE) &&
		(un->un_resvd_status & SD_LOST_RESERVE)) {
		if (un->un_resvd_status & SD_WANT_RESERVE) {
			/*
			 * Reschedule the timeout, a new reset occured in
			 * between last probe and this one.
			 * This will help other host if it is taking over disk
			 */
			mutex_exit(SD_MUTEX);
		/* LOCK UNNEEDED BECAUSE "Read Only": un->un_sd->sd_mutex */
			if (un->un_resvd_timeid) {
				(void) untimeout(un->un_resvd_timeid);
			}
		/* LOCK NEEDED: un->un_sd->sd_mutex */
			mutex_enter(SD_MUTEX);
			un->un_resvd_timeid = 0;
			un->un_resvd_status &= ~SD_WANT_RESERVE;
		}
		if (un->un_resvd_timeid == 0) {
			un->un_resvd_timeid = timeout(sd_mhd_resvd_recover,
			    (caddr_t)dev, drv_usectohz(reinstate_resv_delay));
		}
	}
	mutex_exit(SD_MUTEX);
done:
	return (0);
}

static void
sd_mhd_watch_incomplete(struct scsi_disk *un, struct scsi_pkt *pkt)
{
	int	be_chatty = (!(pkt->pkt_flags & FLAG_SILENT));
	int	perr = (pkt->pkt_statistics & STAT_PERR);

	SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG,
		"sd_mhd_watch_incomplete: Entering ...\n");

	mutex_enter(SD_MUTEX);

	if (un->un_state == SD_STATE_DUMPING) {
		mutex_exit(SD_MUTEX);
		return;
	}

	switch (pkt->pkt_reason) {
	case CMD_UNX_BUS_FREE:
		if (perr && be_chatty)
			be_chatty = 0;
		break;

	case CMD_TAG_REJECT:
		pkt->pkt_flags = 0;
		un->un_tagflags = 0;
		if (un->un_dp->options & SD_QUEUEING) {
			un->un_throttle = 3;
		} else {
			un->un_throttle = 1;
		}
		(void) scsi_ifsetcap(ROUTE, "tagged-qing", 0, 1);
		break;

	case CMD_INCOMPLETE:
		/*
		 * selection did not complete; we don't want to go
		 * thru a bus reset for this reason
		 */
		if (pkt->pkt_state == STATE_GOT_BUS) {
			break;
		}
		/*FALLTHROUGH*/

	case CMD_TIMEOUT:
	default:
		/*
		 * the target may still be running the	command,
		 * so we should try and reset that target.
		 */
		if ((pkt->pkt_statistics &
			(STAT_BUS_RESET|STAT_DEV_RESET|STAT_ABORTED)) == 0) {
			mutex_exit(SD_MUTEX);
			if (scsi_reset(ROUTE, RESET_TARGET) == 0)
				(void) scsi_reset(ROUTE, RESET_ALL);
			mutex_enter(SD_MUTEX);
		}
		break;
	}

	if ((pkt->pkt_reason == CMD_RESET) || (pkt->pkt_statistics &
	    (STAT_BUS_RESET | STAT_DEV_RESET))) {
		if ((un->un_resvd_status & SD_RESERVE) == SD_RESERVE) {
			un->un_resvd_status |=
			    (SD_LOST_RESERVE | SD_WANT_RESERVE);
			SD_DEBUG2(SD_DEVINFO, sd_label, CE_WARN,
			    "Lost Reservation\n");
		}
	}
	if (pkt->pkt_state == STATE_GOT_BUS) {

		/*
		 * Looks like someone turned off this shoebox.
		 */
		SD_DEBUG(SD_DEVINFO, sd_label, CE_WARN,
			"sd_mhd_watch_incomplete: "
			"Disk not responding to selection\n");
		New_state(un, SD_STATE_DETACHING);
	} else if (be_chatty) {

		/*
		 * suppress messages if they are all the same pkt reason;
		 * with TQ, many (up to 256) are returned with the same
		 * pkt_reason
		 */
		if ((pkt->pkt_reason != un->un_last_pkt_reason) ||
			(sd_error_level == SCSI_ERR_ALL)) {
			SD_DEBUG(SD_DEVINFO, sd_label, CE_WARN,
				"sd_mhd_watch_incomplete: "
				"SCSI transport failed: reason '%s'\n",
				scsi_rname(pkt->pkt_reason));
		}
	}
	un->un_last_pkt_reason = pkt->pkt_reason;
	mutex_exit(SD_MUTEX);
}

static void
sd_mhd_resvd_recover(caddr_t arg)
{
	dev_t				dev = (dev_t)arg;
	register struct scsi_disk	*un;
	register u_long			minor = getminor(dev);
	register int			instance;

	instance = minor >> 3;
	un = ddi_get_soft_state(sd_state, instance);

	SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG,
		"sd_mhd_resvd_recover: Entering ...\n");

	mutex_enter(SD_MUTEX);
	un->un_resvd_timeid = 0;
	if (un->un_resvd_status & SD_WANT_RESERVE) {
		/*
		 * It is not appropriate to do a reserve, there was a reset
		 * recently. Allow the sd_mhd_watch_cb callback function to
		 * notice this and reschedule the timeout for reservation.
		 */
		mutex_exit(SD_MUTEX);
		return;
	}
	/*
	 * Note that we clear the SD_LOST_RESERVE flag before reclaiming
	 * reservation. If we do this after the call to sd_reserve_release
	 * we may fail to recognize a reservation loss in the window between
	 * pkt completion of reserve cmd and mutex_enter below.
	 */
	un->un_resvd_status &= ~SD_LOST_RESERVE;
	mutex_exit(SD_MUTEX);

	if (sd_reserve_release(dev, SD_RESERVE) == 0) {
		mutex_enter(SD_MUTEX);
		un->un_resvd_status |= SD_RESERVE;
		mutex_exit(SD_MUTEX);
		SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG,
			"sd_mhd_resvd_recover: Reservation Recovered\n");
	} else {
		mutex_enter(SD_MUTEX);
		un->un_resvd_status |= SD_LOST_RESERVE;
		mutex_exit(SD_MUTEX);
	}
}

static void
sd_mhd_reset_notify_cb(caddr_t arg)
{
	struct scsi_disk	*un = (struct scsi_disk *)arg;

	mutex_enter(SD_MUTEX);
	if ((un->un_resvd_status & SD_RESERVE) == SD_RESERVE) {
		un->un_resvd_status |= (SD_LOST_RESERVE | SD_WANT_RESERVE);
		SD_DEBUG(SD_DEVINFO, sd_label, CE_WARN,
		    "sd_mhd_reset_notify_cb: Lost Reservation\n");
	}
	mutex_exit(SD_MUTEX);
}

static int
sd_handle_resv_conflict(struct scsi_disk *un, struct buf *bp)
{
	struct diskhd			*dp = &un->un_utab;
	register struct scsi_pkt	*pkt;
	register int			action = COMMAND_DONE_ERROR;

	ASSERT(mutex_owned(SD_MUTEX));

	if (bp) {
		pkt = BP_PKT(bp);
	} else {
		return (COMMAND_DONE_ERROR);
	}

	if (un->un_resvd_status & SD_FAILFAST) {
		if (un->un_resvd_status & SD_RESERVE) {
			if (sd_failfast_enable) {
				cmn_err(CE_PANIC, "Reservation Conflict\n");
			} else
				scsi_log(SD_DEVINFO, sd_label, CE_WARN,
					"sd_handle_resv_conflict: "
					"Reservation Conflict\n");
			SET_BP_ERROR(bp, EACCES);
		} else {
			SD_DEBUG(SD_DEVINFO, sd_label, SCSI_DEBUG,
				"sd_handle_resv_conflict: "
				"Disk Reserved\n");
			SET_BP_ERROR(bp, EACCES);
		}
	} else {
		int	tval = 2;

		/*
		 * 1147670: retry only if sd_retry_on_reservation_conflict
		 * property is set (default is 1). Retry will not succeed
		 * on a disk reserved by another initiator. HA systems
		 * may reset this via sd.conf to avoid these retries.
		 */
		if (((int)PKT_GET_RETRY_CNT(pkt) < sd_retry_count) &&
		    (sd_retry_on_reservation_conflict != 0)) {

			scsi_log(SD_DEVINFO, sd_label, CE_WARN,
			    "sd_handle_resv_conflict: Disk Reserved\n");
			PKT_INCR_RETRY_CNT(pkt, 1);
			if (dp->b_forw == NULL || dp->b_forw == bp) {
				dp->b_forw = bp;
				(void) timeout(sdrestart, (caddr_t)un, tval);
			} else if (dp->b_forw != bp) {
				/*
				 * put it back on the queue
				 */
				bp->b_actf = dp->b_actf;
				dp->b_actf = bp;
				if (un->un_stats)
					kstat_runq_back_to_waitq(IOSP);
			}
			action = JUST_RETURN;
		} else {
			SD_DEBUG(SD_DEVINFO, sd_label, CE_WARN,
				"sd_handle_resv_conflict: "
				"Device Busy Too Long\n");
		}
	}
	un->un_resvd_status |= SD_RESERVATION_CONFLICT;
	return (action);
}

/*
 * CD rom functions
 */

/*
 * This routine does a pause or resume to the cdrom player. Only affect
 * audio play operation.
 */
static int
sr_pause_resume(dev_t dev, caddr_t data)
{
	int	flag, r;
	char	cdb[CDB_GROUP1];
	register struct uscsi_cmd *com;

	com = (struct uscsi_cmd *)kmem_zalloc(sizeof (*com), KM_SLEEP);
	flag = (int)data;
	bzero((caddr_t)cdb, CDB_GROUP1);
	cdb[0] = SCMD_PAUSE_RESUME;
	if (flag)
		cdb[8] = 1;
	com->uscsi_cdb = cdb;
	com->uscsi_cdblen = CDB_GROUP1;
	com->uscsi_flags = USCSI_DIAGNOSE|USCSI_SILENT;
	r = sdioctl_cmd(dev, com, UIO_SYSSPACE, UIO_SYSSPACE,
		UIO_SYSSPACE);
	kmem_free((caddr_t)com, sizeof (*com));
	return (r);
}

/*
 * This routine plays audio by msf
 */
static int
sr_play_msf(dev_t dev, caddr_t data, int flag)
{
	int r;
	char	cdb[CDB_GROUP1];
	struct cdrom_msf		msf_struct;
	register struct cdrom_msf	*msf = &msf_struct;
	register struct uscsi_cmd *com;

	if (ddi_copyin((caddr_t)data, (caddr_t)msf, sizeof (struct cdrom_msf),
	    flag))
		return (EFAULT);

	com = (struct uscsi_cmd *)kmem_zalloc(sizeof (*com), KM_SLEEP);
	bzero((caddr_t)cdb, CDB_GROUP1);
	cdb[0] = SCMD_PLAYAUDIO_MSF;
	cdb[3] = msf->cdmsf_min0;
	cdb[4] = msf->cdmsf_sec0;
	cdb[5] = msf->cdmsf_frame0;
	cdb[6] = msf->cdmsf_min1;
	cdb[7] = msf->cdmsf_sec1;
	cdb[8] = msf->cdmsf_frame1;
	com->uscsi_cdb = cdb;
	com->uscsi_cdblen = CDB_GROUP1;
	com->uscsi_flags = USCSI_DIAGNOSE|USCSI_SILENT;
	r = sdioctl_cmd(dev, com, UIO_SYSSPACE, UIO_SYSSPACE,
		UIO_SYSSPACE);
	kmem_free((caddr_t)com, sizeof (*com));
	return (r);
}

/*
 * This routine plays audio by track/index
 */
static int
sr_play_trkind(dev_t dev, caddr_t data, int flag)
{
	int r;
	register struct uscsi_cmd *com;
	char	cdb[CDB_GROUP1];
	struct cdrom_ti		ti_struct;
	register struct cdrom_ti	*ti = &ti_struct;

	if (ddi_copyin((caddr_t)data, (caddr_t)ti, sizeof (struct cdrom_ti),
	    flag))
		return (EFAULT);
	com = kmem_zalloc(sizeof (*com), KM_SLEEP);
	bzero((caddr_t)cdb, CDB_GROUP1);
	cdb[0] = SCMD_PLAYAUDIO_TI;
	cdb[4] = ti->cdti_trk0;
	cdb[5] = ti->cdti_ind0;
	cdb[7] = ti->cdti_trk1;
	cdb[8] = ti->cdti_ind1;
	com->uscsi_cdb = cdb;
	com->uscsi_cdblen = CDB_GROUP1;
	com->uscsi_flags = USCSI_DIAGNOSE|USCSI_SILENT;
	r = sdioctl_cmd(dev, com, UIO_SYSSPACE, UIO_SYSSPACE,
		UIO_SYSSPACE);
	kmem_free((caddr_t)com, sizeof (*com));
	return (r);
}

#ifdef FIXEDFIRMWARE
/*
 * This routine control the audio output volume
 */
static int
sr_volume_ctrl(dev_t dev, caddr_t data, int flag)
{
	register struct uscsi_cmd *com;
	char	cdb[CDB_GROUP0];
	struct cdrom_volctrl	volume;
	struct cdrom_volctrl	*vol = &volume;
	caddr_t buffer;
	int	rtn;
	struct scsi_disk *un;

	if ((un = ddi_get_soft_state(sd_state, SDUNIT(dev))) == NULL ||
		(un->un_state == SD_STATE_DETACHING))
		return (ENXIO);

	SD_DEBUG2(SD_DEVINFO, sd_label, SCSI_DEBUG, "in sr_volume_ctrl\n");

	if (ddi_copyin((caddr_t)data, (caddr_t)vol,
	    sizeof (struct cdrom_volctrl), flag))
		return (EFAULT);

	buffer = kmem_zalloc((size_t)20, KM_SLEEP);
	bzero((caddr_t)cdb, CDB_GROUP0);
	cdb[0] = SCMD_MODE_SELECT;
	cdb[4] = 20;

	/*
	 * fill in the input data. Set the output channel 0, 1 to
	 * output port 0, 1 respestively. Set output channel 2, 3 to
	 * mute. The function only adjust the output volume for channel
	 * 0 and 1.
	 */
	buffer[4] = 0xe;
	buffer[5] = 0xe;
	buffer[6] = 0x4;	/* set the immediate bit to 1 */
	buffer[12] = 0x01;
	buffer[13] = vol->channel0;
	buffer[14] = 0x02;
	buffer[15] = vol->channel1;

	com = (struct uscsi_cmd *)kmem_zalloc(sizeof (*com), KM_SLEEP);
	com->uscsi_cdb = cdb;
	com->uscsi_cdblen = CDB_GROUP0;
	com->uscsi_bufaddr = buffer;
	com->uscsi_buflen = 20;
	com->uscsi_flags = USCSI_DIAGNOSE|USCSI_SILENT;

	rtn = sdioctl_cmd(dev, com, UIO_SYSSPACE, UIO_SYSSPACE,
		UIO_SYSSPACE);
	kmem_free(buffer, 20);
	kmem_free((caddr_t)com, sizeof (*com));
	return (rtn);
}
#else
/*
 * This routine control the audio output volume
 */
static int
sr_volume_ctrl(dev_t dev, caddr_t  data, int flag)
{
	register struct uscsi_cmd *com;
	char	cdb[CDB_GROUP1];
	struct cdrom_volctrl	volume;
	struct cdrom_volctrl	*vol = &volume;
	caddr_t buffer;
	int	rtn;

	if ((un = ddi_get_soft_state(sd_state, SDUNIT(dev))) == NULL ||
		(un->un_state == SD_STATE_DETACHING))
		return (ENXIO);

	SD_DEBUG2(SD_DEVINFO, sd_label, SCSI_DEBUG, "in sr_volume_ctrl\n");

	if (ddi_copyin((caddr_t)data, (caddr_t)vol,
	    sizeof (struct cdrom_volctrl), flag))
		return (EFAULT);

	buffer = kmem_zalloc((size_t)18, KM_SLEEP);
	bzero(cdb, CDB_GROUP1);

	cdb[0] = 0xc9;	/* vendor unique command */
	cdb[8] = 0x12;

	/*
	 * fill in the input data. Set the output channel 0, 1 to
	 * output port 0, 1 respestively. Set output channel 2, 3 to
	 * mute. The function only adjust the output volume for channel
	 * 0 and 1.
	 */
	buffer[10] = 0x01;
	buffer[11] = vol->channel0;
	buffer[12] = 0x02;
	buffer[13] = vol->channel1;

	com = (struct uscsi_cmd *)kmem_zalloc(sizeof (*com), KM_SLEEP);
	com->uscsi_cdb = cdb;
	com->uscsi_cdblen = CDB_GROUP1;
	com->uscsi_bufaddr = buffer;
	com->uscsi_buflen = 0x12;
	com->uscsi_flags = USCSI_DIAGNOSE|USCSI_SILENT;
	rtn = sdioctl_cmd(dev, com, UIO_SYSSPACE, UIO_SYSSPACE,
		UIO_SYSSPACE);
	kmem_free(buffer, 18);
	kmem_free((caddr_t)com, sizeof (*com));
	return (rtn);
}
#endif FIXEDFIRMWARE


static int
sr_read_subchannel(dev_t dev, caddr_t data, int flag)
{
	register struct uscsi_cmd *com;
	char	cdb[CDB_GROUP1];
	caddr_t buffer;
	int	rtn;
	struct	cdrom_subchnl	subchanel;
	struct	cdrom_subchnl	*subchnl = &subchanel;
	struct scsi_disk *un;

	if ((un = ddi_get_soft_state(sd_state, SDUNIT(dev))) == NULL ||
		(un->un_state == SD_STATE_DETACHING))
		return (ENXIO);

	SD_DEBUG2(SD_DEVINFO, sd_label, SCSI_DEBUG, "in sr_read_subchannel.\n");

	if (ddi_copyin((caddr_t)data, (caddr_t)subchnl,
	    sizeof (struct cdrom_subchnl), flag))
		return (EFAULT);

	buffer = kmem_zalloc((size_t)16, KM_SLEEP);
	bzero((caddr_t)cdb, CDB_GROUP1);
	cdb[0] = SCMD_READ_SUBCHANNEL;
	cdb[1] = (subchnl->cdsc_format & CDROM_LBA) ? 0 : 0x02;
	/*
	 * set the Q bit in byte 2 to 1.
	 */
	cdb[2] = 0x40;
	/*
	 * This byte (byte 3) specifies the return data format. Proposed
	 * by Sony. To be added to SCSI-2 Rev 10b
	 * Setting it to one tells it to return time-data format
	 */
	cdb[3] = 0x01;
	cdb[8] = 0x10;

	com = (struct uscsi_cmd *)kmem_zalloc(sizeof (*com), KM_SLEEP);
	com->uscsi_cdb = cdb;
	com->uscsi_cdblen = CDB_GROUP1;
	com->uscsi_bufaddr = buffer;
	com->uscsi_buflen = 0x10;
	com->uscsi_flags = USCSI_DIAGNOSE|USCSI_SILENT|USCSI_READ;

	rtn = sdioctl_cmd(dev, com, UIO_SYSSPACE, UIO_SYSSPACE,
		UIO_SYSSPACE);
	subchnl->cdsc_audiostatus = buffer[1];
	subchnl->cdsc_trk = buffer[6];
	subchnl->cdsc_ind = buffer[7];
	subchnl->cdsc_adr = buffer[5] & 0xF0;
	subchnl->cdsc_ctrl = buffer[5] & 0x0F;
	if (subchnl->cdsc_format & CDROM_LBA) {
		subchnl->cdsc_absaddr.lba =
		    ((u_char)buffer[8] << 24) +
		    ((u_char)buffer[9] << 16) +
		    ((u_char)buffer[10] << 8) +
		    ((u_char)buffer[11]);
		subchnl->cdsc_reladdr.lba =
		    ((u_char)buffer[12] << 24) +
		    ((u_char)buffer[13] << 16) +
		    ((u_char)buffer[14] << 8) +
		    ((u_char)buffer[15]);
	} else {
		subchnl->cdsc_absaddr.msf.minute = buffer[9];
		subchnl->cdsc_absaddr.msf.second = buffer[10];
		subchnl->cdsc_absaddr.msf.frame = buffer[11];
		subchnl->cdsc_reladdr.msf.minute = buffer[13];
		subchnl->cdsc_reladdr.msf.second = buffer[14];
		subchnl->cdsc_reladdr.msf.frame = buffer[15];
	}
	kmem_free(buffer, 16);
	kmem_free((caddr_t)com, sizeof (*com));
	if (ddi_copyout((caddr_t)subchnl, (caddr_t)data,
	    sizeof (struct cdrom_subchnl), flag))
		return (EFAULT);
	return (rtn);
}


static int
sr_read_mode2(dev_t dev, caddr_t data, int flag)
{
	register struct uscsi_cmd *com;
	u_char	cdb[CDB_GROUP0];
	int	rtn;
	auto struct cdrom_read mode2_struct;
	register struct	 cdrom_read *mode2 = &mode2_struct;
	register struct scsi_disk *un;

	if ((un = ddi_get_soft_state(sd_state, SDUNIT(dev))) == NULL ||
		(un->un_state == SD_STATE_DETACHING))
		return (ENXIO);

	SD_DEBUG2(SD_DEVINFO, sd_label, SCSI_DEBUG, "in sr_read_mode2.\n");

	if (ddi_copyin(data, (caddr_t)mode2, sizeof (*mode2), flag)) {
		return (EFAULT);
	}

	if (un->un_secsize == SECSIZE_FIVETWELVE)
		mode2->cdread_lba >>= 2;

	bzero((caddr_t)cdb, CDB_GROUP0);
	cdb[0] = SCMD_READ;
	cdb[1] = (u_char)((mode2->cdread_lba >> 16) & 0XFF);
	cdb[2] = (u_char)((mode2->cdread_lba >> 8) & 0xFF);
	cdb[3] = (u_char)(mode2->cdread_lba & 0xFF);
	cdb[4] = mode2->cdread_buflen / 2336;

	com = (struct uscsi_cmd *)kmem_zalloc(sizeof (*com), KM_SLEEP);
	com->uscsi_cdb = (caddr_t)cdb;
	com->uscsi_cdblen = CDB_GROUP0;
	com->uscsi_bufaddr = mode2->cdread_bufaddr;
	com->uscsi_buflen = mode2->cdread_buflen;
	com->uscsi_flags = USCSI_DIAGNOSE|USCSI_SILENT|USCSI_READ;

	mutex_enter(SD_MUTEX);
	if (sr_sector_mode(dev, 2) == 0) {
		mutex_exit(SD_MUTEX);
		rtn = sdioctl_cmd(dev, com, UIO_SYSSPACE, UIO_USERSPACE,
			UIO_SYSSPACE);
		mutex_enter(SD_MUTEX);
		if (sr_sector_mode(dev, 0) != 0) {
			scsi_log(SD_DEVINFO, sd_label,
			    CE_WARN, "can't to switch back to mode 1\n");
			if (rtn == 0)
				rtn = EIO;
		}
	}
	mutex_exit(SD_MUTEX);
	kmem_free((caddr_t)com, sizeof (*com));
	return (rtn);
}

static int
sr_read_mode1(dev_t dev, caddr_t data, int flag)
{
	register struct uscsi_cmd *com;
	u_char	cdb[CDB_GROUP0];
	int	rtn;
	struct	cdrom_read	mode1_struct;
	struct	cdrom_read	*mode1 = &mode1_struct;
	struct scsi_disk *un;

	if ((un = ddi_get_soft_state(sd_state, SDUNIT(dev))) == NULL ||
		(un->un_state == SD_STATE_DETACHING))
		return (ENXIO);

	SD_DEBUG2(SD_DEVINFO, sd_label, SCSI_DEBUG, "in sr_read_mode1.\n");

	if (ddi_copyin((caddr_t)data, (caddr_t)mode1,
	    sizeof (struct cdrom_read), flag))
		return (EFAULT);

	bzero((caddr_t)cdb, CDB_GROUP0);
	cdb[0] = SCMD_READ;
	cdb[1] = (u_char)((mode1->cdread_lba >> 16) & 0XFF);
	cdb[2] = (u_char)((mode1->cdread_lba >> 8) & 0xFF);
	cdb[3] = (u_char)(mode1->cdread_lba & 0xFF);
	cdb[4] = mode1->cdread_buflen >> 11;

	com = (struct uscsi_cmd *)kmem_zalloc(sizeof (*com), KM_SLEEP);
	com->uscsi_cdb = (caddr_t)cdb;
	com->uscsi_cdblen = CDB_GROUP0;
	com->uscsi_bufaddr = mode1->cdread_bufaddr;
	com->uscsi_buflen = mode1->cdread_buflen;
	com->uscsi_flags = USCSI_DIAGNOSE|USCSI_SILENT|USCSI_READ;

	rtn = sdioctl_cmd(dev, com, UIO_SYSSPACE, UIO_USERSPACE,
		UIO_SYSSPACE);
	kmem_free((caddr_t)com, sizeof (*com));
	return (rtn);
}


static int
sr_read_tochdr(dev_t dev, caddr_t data, int flag)
{
	register struct uscsi_cmd *com;
	char	cdb[CDB_GROUP1];
	caddr_t buffer;
	int	rtn;
	struct cdrom_tochdr	header;
	struct cdrom_tochdr	*hdr = &header;
	struct scsi_disk *un;

	if ((un = ddi_get_soft_state(sd_state, SDUNIT(dev))) == NULL ||
		(un->un_state == SD_STATE_DETACHING))
		return (ENXIO);

	SD_DEBUG2(SD_DEVINFO, sd_label, SCSI_DEBUG, "in sr_read_tochdr.\n");


	buffer = kmem_zalloc((size_t)4, KM_SLEEP);
	bzero((caddr_t)cdb, CDB_GROUP1);
	cdb[0] = SCMD_READ_TOC;
	cdb[6] = 0x00;
	/*
	 * byte 7, 8 are the allocation length. In this case, it is 4 bytes.
	 */
	cdb[8] = 0x04;
	com = (struct uscsi_cmd *)kmem_zalloc(sizeof (*com), KM_SLEEP);
	com->uscsi_cdb = cdb;
	com->uscsi_cdblen = CDB_GROUP1;
	com->uscsi_bufaddr = buffer;
	com->uscsi_buflen = 0x04;
	com->uscsi_timeout = 300;
	com->uscsi_flags = USCSI_DIAGNOSE|USCSI_SILENT|USCSI_READ;

	rtn = sdioctl_cmd(dev, com, UIO_SYSSPACE, UIO_SYSSPACE,
		UIO_SYSSPACE);
	hdr->cdth_trk0 = buffer[2];
	hdr->cdth_trk1 = buffer[3];
	kmem_free(buffer, 4);
	kmem_free((caddr_t)com, sizeof (*com));
	if (ddi_copyout((caddr_t)hdr, (caddr_t)data,
	    sizeof (struct cdrom_tochdr), flag))
		return (EFAULT);
	return (rtn);
}

#ifdef CDROMREADOFFSET

#define	SONY_SESSION_OFFSET_LEN 12
#define	SONY_SESSION_OFFSET_KEY 0x40
#define	SONY_SESSION_OFFSET_VALID	0x0a

static int
sr_read_sony_session_offset(dev_t dev, caddr_t data, int flag)
{
	register struct uscsi_cmd *com;
	char	cdb[CDB_GROUP1];
	caddr_t buffer;
	int	rtn;
	int	session_offset;
	struct scsi_disk *un;

	if ((un = ddi_get_soft_state(sd_state, SDUNIT(dev))) == NULL ||
		(un->un_state == SD_STATE_DETACHING))
		return (ENXIO);

	SD_DEBUG2(SD_DEVINFO, sd_label, SCSI_DEBUG,
	    "in sr_read_multi_session_offset.\n");

	buffer = kmem_zalloc((size_t)SONY_SESSION_OFFSET_LEN, KM_SLEEP);
	bzero((caddr_t)cdb, CDB_GROUP1);
	cdb[0] = SCMD_READ_TOC;
	/*
	 * byte 7, 8 are the allocation length. In this case, it is 4 bytes.
	 */
	cdb[8] = SONY_SESSION_OFFSET_LEN;
	cdb[9] = SONY_SESSION_OFFSET_KEY;
	com = (struct uscsi_cmd *)kmem_zalloc(sizeof (*com), KM_SLEEP);
	com->uscsi_cdb = cdb;
	com->uscsi_cdblen = CDB_GROUP1;
	com->uscsi_bufaddr = buffer;
	com->uscsi_buflen = SONY_SESSION_OFFSET_LEN;
	com->uscsi_flags = USCSI_DIAGNOSE|USCSI_SILENT|USCSI_READ;

	rtn = sdioctl_cmd(dev, com, UIO_SYSSPACE, UIO_SYSSPACE,
	    UIO_SYSSPACE);
	session_offset = 0;
	if (buffer[1] == SONY_SESSION_OFFSET_VALID) {
		session_offset = ((u_char) buffer[8] << 24) +
		    ((u_char) buffer[9] << 16) +
		    ((u_char) buffer[10] << 8) +
		    ((u_char) buffer[11]);
		session_offset = session_offset / 4;
	}
	kmem_free(buffer, SONY_SESSION_OFFSET_LEN);
	kmem_free((caddr_t)com, sizeof (*com));

	if (ddi_copyout((caddr_t)&session_offset, (caddr_t)data,
	    sizeof (int), flag))
		return (EFAULT);

	return (rtn);
}
#endif

/*
 * This routine read the toc of the disc and returns the information
 * of a particular track. The track number is specified by the ioctl
 * caller.
 */
static int
sr_read_tocentry(dev_t dev, caddr_t data, int flag)
{
	register struct uscsi_cmd *com;
	char	cdb[CDB_GROUP1];
	struct cdrom_tocentry	toc_entry;
	struct cdrom_tocentry	*entry = &toc_entry;
	caddr_t buffer;
	int	rtn, rtn1;
	int	lba;
	struct scsi_disk *un;

	if ((un = ddi_get_soft_state(sd_state, SDUNIT(dev))) == NULL ||
		(un->un_state == SD_STATE_DETACHING))
		return (ENXIO);

	SD_DEBUG2(SD_DEVINFO, sd_label, SCSI_DEBUG, "in sr_read_tocentry.\n");

	if (ddi_copyin((caddr_t)data, (caddr_t)entry,
	    sizeof (struct cdrom_tocentry), flag))
		return (EFAULT);

	if (!(entry->cdte_format & (CDROM_LBA | CDROM_MSF))) {
		return (EINVAL);
	}

	buffer = kmem_zalloc((size_t)12, KM_SLEEP);
	bzero((caddr_t)cdb, CDB_GROUP1);

	cdb[0] = SCMD_READ_TOC;
	/* set the MSF bit of byte one */
	cdb[1] = (entry->cdte_format & CDROM_LBA) ? 0 : 2;
	cdb[6] = entry->cdte_track;
	/*
	 * byte 7, 8 are the allocation length. In this case, it is 4 + 8
	 * = 12 bytes, since we only need one entry.
	 */
	cdb[8] = 0x0C;
	com = (struct uscsi_cmd *)kmem_zalloc(sizeof (*com), KM_SLEEP);
	com->uscsi_cdb = cdb;
	com->uscsi_cdblen = CDB_GROUP1;
	com->uscsi_bufaddr = buffer;
	com->uscsi_buflen = 0x0C;
	com->uscsi_flags = USCSI_DIAGNOSE|USCSI_SILENT|USCSI_READ;

	rtn = sdioctl_cmd(dev, com, UIO_SYSSPACE, UIO_SYSSPACE,
		UIO_SYSSPACE);
	entry->cdte_adr = (buffer[5] & 0xF0) >> 4;
	entry->cdte_ctrl = (buffer[5] & 0x0F);
	if (entry->cdte_format & CDROM_LBA) {
		entry->cdte_addr.lba =
		    ((u_char)buffer[8] << 24) +
		    ((u_char)buffer[9] << 16) +
		    ((u_char)buffer[10] << 8) +
		    ((u_char)buffer[11]);
	} else {
		entry->cdte_addr.msf.minute = buffer[9];
		entry->cdte_addr.msf.second = buffer[10];
		entry->cdte_addr.msf.frame = buffer[11];
	}

	if (rtn) {
		kmem_free(buffer, 12);
		kmem_free((caddr_t)com, sizeof (*com));
		return (rtn);
	}

	/*
	 * Now do a readheader to determine which data mode it is in.
	 * ...If the track is a data track
	 */
	if ((entry->cdte_ctrl & CDROM_DATA_TRACK) &&
	    (entry->cdte_track != CDROM_LEADOUT)) {
		if (entry->cdte_format & CDROM_LBA) {
			lba = entry->cdte_addr.lba;
		} else {
			lba =
			    (((entry->cdte_addr.msf.minute * 60) +
			    (entry->cdte_addr.msf.second)) * 75) +
			    entry->cdte_addr.msf.frame;
		}
		bzero((caddr_t)cdb, CDB_GROUP1);
		cdb[0] = SCMD_READ_HEADER;
		cdb[2] = (u_char)((lba >> 24) & 0xFF);
		cdb[3] = (u_char)((lba >> 16) & 0xFF);
		cdb[4] = (u_char)((lba >> 8) & 0xFF);
		cdb[5] = (u_char)(lba & 0xFF);
		cdb[8] = 0x08;
		com->uscsi_buflen = 0x08;

		rtn1 = sdioctl_cmd(dev, com, UIO_SYSSPACE, UIO_SYSSPACE,
			UIO_SYSSPACE);
		if (rtn1) {
			kmem_free(buffer, 12);
			kmem_free((caddr_t)com, sizeof (*com));
			return (rtn1);
		}
		entry->cdte_datamode = buffer[0];

	} else {
		entry->cdte_datamode = (u_char)-1;
	}

	kmem_free(buffer, 12);
	kmem_free((caddr_t)com, sizeof (*com));

	if (ddi_copyout((caddr_t)entry, data,
	    sizeof (struct cdrom_tocentry), flag))
		return (EFAULT);

	return (rtn);
}

static int
sr_mode_sense(dev_t dev, int page, char *page_data, int page_size)
{

	int r;
	char	cdb[CDB_GROUP0];
	register struct uscsi_cmd *com;

	com = kmem_zalloc(sizeof (*com), KM_SLEEP);

	bzero((caddr_t)cdb, CDB_GROUP0);
	cdb[0] = SCMD_MODE_SENSE;
	cdb[2] = (char)page;
	cdb[4] = (char)page_size;

	com->uscsi_cdb = cdb;
	com->uscsi_cdblen = CDB_GROUP0;
	com->uscsi_bufaddr = page_data;
	com->uscsi_buflen = page_size;
	com->uscsi_flags = USCSI_DIAGNOSE | USCSI_SILENT |
			    USCSI_READ;

	r = sdioctl_cmd(dev, com, UIO_SYSSPACE, UIO_SYSSPACE,
		UIO_SYSSPACE);
	kmem_free((caddr_t)com, sizeof (*com));
	return (r);
}

/*
 * This routine does a mode select for the cdrom
 */
static int
sr_mode_select(dev_t dev, char *page_data, int page_size)
{

	int r;
	char	cdb[CDB_GROUP0];
	register struct uscsi_cmd *com;

	com = kmem_zalloc(sizeof (*com), KM_SLEEP);

	/*
	 * then, do a mode select to set what ever info
	 */
	bzero((caddr_t)cdb, CDB_GROUP0);
	cdb[0] = SCMD_MODE_SELECT;
	cdb[1] = 0x10;		/* set PF bit for many third party drives */
	cdb[4] = (char)page_size;

	com->uscsi_cdb = cdb;
	com->uscsi_cdblen = CDB_GROUP0;
	com->uscsi_bufaddr = page_data;
	com->uscsi_buflen = page_size;
	com->uscsi_flags = USCSI_DIAGNOSE | USCSI_SILENT | USCSI_WRITE;

	r = sdioctl_cmd(dev, com, UIO_SYSSPACE, UIO_SYSSPACE,
		UIO_SYSSPACE);

	kmem_free((caddr_t)com, sizeof (*com));
	return (r);
}


/*
 * The following 20 is: (sizeof (mode_header) +  sizeof (block_descriptor) +
 * sizeof (mode_err_recov_ccs)).
 *
 * XXX change the whole mode sense/select code of CDROM portion to use proper
 * mode and other structures defined in generic/mode.h and impl/mode.h.
 */
#define	BUFLEN_CDROM_MODE_ERR_RECOV 	20

static int
sr_check_blk_size(dev_t dev)
{
	register char   *sense = NULL;
	int		rval;

	GET_SOFT_STATE(dev);
#ifdef lint
	part = part;
#endif /* lint */

	sense = kmem_zalloc(BUFLEN_CDROM_MODE_ERR_RECOV, KM_SLEEP);
	if ((rval = sr_mode_sense(dev, 0x1, sense,
				BUFLEN_CDROM_MODE_ERR_RECOV)) != 0) {
			scsi_log(SD_DEVINFO, sd_label, CE_WARN,
			"sr_check_blk_size: Mode Sense Failed\n");
	} else {
		un->un_secsize = (u_char)sense[9] << 16 |
				(u_char)sense[10] << 8 | (u_char)sense[11];
	}

	kmem_free((caddr_t)sense, BUFLEN_CDROM_MODE_ERR_RECOV);
	return (rval);
}

static int
sr_change_blkmode(dev_t dev, int cmd, int data, int flag)
{
	int		current_bsize;
	int		rval = EINVAL;
	register char	*sense = NULL;
	register char	*select = NULL;

	GET_SOFT_STATE(dev);
#ifdef lint
	part = part;
#endif /* lint */

	sense = kmem_zalloc(BUFLEN_CDROM_MODE_ERR_RECOV, KM_SLEEP);
	if ((rval = sr_mode_sense(dev, 0x1, sense,
				BUFLEN_CDROM_MODE_ERR_RECOV)) != 0) {
		scsi_log(SD_DEVINFO, sd_label, CE_WARN,
			"sr_change_blkmode: Mode Sense Failed\n");
		kmem_free((caddr_t)sense, BUFLEN_CDROM_MODE_ERR_RECOV);
		return (rval);
	}
	current_bsize = (u_char)sense[9] << 16 | (u_char)sense[10] << 8 |
				(u_char)sense[11];

	switch (cmd) {
	case CDROMGBLKMODE:
		if (ddi_copyout((caddr_t)&current_bsize,
			(caddr_t)data, sizeof (int), flag))
			rval = EFAULT;
		break;

	case CDROMSBLKMODE:
		switch (data) {
		case CDROM_BLK_512:
		case CDROM_BLK_1024:
		case CDROM_BLK_2048:
		case CDROM_BLK_2056:
		case CDROM_BLK_2336:
		case CDROM_BLK_2340:
		case CDROM_BLK_2352:
		case CDROM_BLK_2368:
		case CDROM_BLK_2448:
		case CDROM_BLK_2646:
		case CDROM_BLK_2647:
			break;

		default:
			scsi_log(SD_DEVINFO, sd_label, CE_WARN,
				"sr_change_blkmode: "
				"Block Size '%d' Not Supported\n", data);
			kmem_free((caddr_t)sense, BUFLEN_CDROM_MODE_ERR_RECOV);
			return (EINVAL);
		}

		if (current_bsize == data) {
			break;
		}

		select = kmem_zalloc(BUFLEN_CDROM_MODE_ERR_RECOV, KM_SLEEP);
		select[3] = 0x08;
		select[9] = ((data) & 0x00ff0000) >> 16;
		select[10] = ((data) & 0x0000ff00) >> 8;
		select[11] = (data) & 0x000000ff;
		select[12] = 0x1;
		select[13] = 0x06;
		select[14] = sense[14];
		select[15] = sense[15];

		if ((rval = sr_mode_select(dev, select,
					BUFLEN_CDROM_MODE_ERR_RECOV)) != 0) {
			scsi_log(SD_DEVINFO, sd_label, CE_WARN,
				"sr_change_blkmode: Mode Select Failed\n");
			select[9] = ((current_bsize) & 0x00ff0000) >> 16;
			select[10] = ((current_bsize) & 0x0000ff00) >> 8;
			select[11] = (current_bsize) & 0x000000ff;
			(void) sr_mode_select(dev, select,
					BUFLEN_CDROM_MODE_ERR_RECOV);
			break;
		}
		un->un_secsize = data;

		if ((rval = sr_read_capacity(dev)) != 0) {
			scsi_log(SD_DEVINFO, sd_label, CE_WARN,
				"sr_change_blkmode: Read Capacity Failed\n");
			break;
		}

		break;

	default:

		/*
		 * should not reach here,
		 * but check anyway
		 */
		scsi_log(SD_DEVINFO, sd_label, CE_WARN,
			"sr_change_blkmode: Command '%x' Not Supported\n", cmd);
		break;
	}

	if (select) {
		kmem_free((caddr_t)select, BUFLEN_CDROM_MODE_ERR_RECOV);
	}
	if (sense) {
		kmem_free((caddr_t)sense, BUFLEN_CDROM_MODE_ERR_RECOV);
	}
	return (rval);
}

/*
 * The following 16 is:  (sizeof (mode_header) + sizeof (block_descriptor) +
 *  sizeof (mode_speed)).
 */
#define	BUFLEN_CDROM_MODE_SPEED		16

static int
sr_change_speed(dev_t dev, int cmd, int data, int flag)
{
	int		current_speed;
	int		rval = EINVAL;
	register char	*sense = NULL;
	register char	*select = NULL;

	GET_SOFT_STATE(dev);
#ifdef lint
	part = part;
#endif /* lint */

	sense = kmem_zalloc(BUFLEN_CDROM_MODE_SPEED, KM_SLEEP);
	if ((rval = sr_mode_sense(dev, CDROM_MODE_SPEED, sense,
				BUFLEN_CDROM_MODE_SPEED)) != 0) {
		scsi_log(SD_DEVINFO, sd_label, CE_WARN,
			"sr_change_speed: Mode Sense Failed\n");
		kmem_free((caddr_t)sense, BUFLEN_CDROM_MODE_SPEED);
		return (rval);
	}
	current_speed = (u_char)sense[14];

	switch (cmd) {
	case CDROMGDRVSPEED:
		if (ddi_copyout((caddr_t)&current_speed,
			(caddr_t)data, sizeof (int), flag))
			rval = EFAULT;
		break;

	case CDROMSDRVSPEED:
		switch ((u_char)data) {
		case CDROM_NORMAL_SPEED:
		case CDROM_DOUBLE_SPEED:
		case CDROM_QUAD_SPEED:
		case CDROM_MAXIMUM_SPEED:
			break;

		default:
			scsi_log(SD_DEVINFO, sd_label, CE_WARN,
				"sr_change_speed: "
				"Drive Speed '%d' Not Supported\n",
				(u_char)data);
			kmem_free((caddr_t)sense, BUFLEN_CDROM_MODE_SPEED);
			return (EINVAL);
		}

		if (current_speed == data) {
			break;
		}

		select = kmem_zalloc(BUFLEN_CDROM_MODE_SPEED, KM_SLEEP);
		select[3] = 0x08;
		select[9] = sense[9];
		select[10] = sense[10];
		select[11] = sense[11];
		select[12] = CDROM_MODE_SPEED;
		select[13] = 0x02;
		select[14] = (u_char)data;
		select[15] = sense[15];

		if ((rval = sr_mode_select(dev, select,
					BUFLEN_CDROM_MODE_SPEED)) != 0) {
			scsi_log(SD_DEVINFO, sd_label, CE_WARN,
				"sr_drive_speed: Mode Select Failed\n");
			select[14] = (u_char)current_speed;
			(void) sr_mode_select(dev, select,
					BUFLEN_CDROM_MODE_SPEED);
			break;
		}

		break;

	default:

		/*
		 * should not reach here,
		 * but check anyway
		 */
		scsi_log(SD_DEVINFO, sd_label, CE_WARN,
			"sr_change_speed: Command '%x' Not Supported\n", cmd);
		break;
	}

	if (select) {
		kmem_free((caddr_t)select, BUFLEN_CDROM_MODE_SPEED);
	}
	if (sense) {
		kmem_free((caddr_t)sense, BUFLEN_CDROM_MODE_SPEED);
	}
	return (rval);
}

static int
sr_read_cdda(dev_t dev, caddr_t data, int flag)
{
	register struct uscsi_cmd	*com;
	register struct cdrom_cdda	*cdda;
	int				rval;
	int				buflen;
	char				cdb[CDB_GROUP5];

	GET_SOFT_STATE(dev);
#ifdef lint
	part = part;
#endif /* lint */

	cdda = (struct cdrom_cdda *)
		kmem_zalloc(sizeof (struct cdrom_cdda), KM_SLEEP);

	if (ddi_copyin((caddr_t)data, (caddr_t)cdda,
		sizeof (struct cdrom_cdda), flag)) {
		scsi_log(SD_DEVINFO, sd_label, CE_WARN,
			"sr_read_cdda: ddi_copyin Failed\n");
		kmem_free((caddr_t)cdda, sizeof (struct cdrom_cdda));
		return (EFAULT);
	}

	switch (cdda->cdda_subcode) {
	case CDROM_DA_NO_SUBCODE:
		buflen = CDROM_BLK_2352 * cdda->cdda_length;
		break;

	case CDROM_DA_SUBQ:
		buflen = CDROM_BLK_2368 * cdda->cdda_length;
		break;

	case CDROM_DA_ALL_SUBCODE:
		buflen = CDROM_BLK_2448 * cdda->cdda_length;
		break;

	case CDROM_DA_SUBCODE_ONLY:
		buflen = CDROM_BLK_SUBCODE * cdda->cdda_length;
		break;

	default:
		scsi_log(SD_DEVINFO, sd_label, CE_WARN,
			"sr_read_cdda: Sucode '0x%x' Not Supported\n",
			cdda->cdda_subcode);
		kmem_free((caddr_t)cdda, sizeof (struct cdrom_cdda));
		return (EINVAL);
	}

	com = (struct uscsi_cmd *)kmem_zalloc(sizeof (*com), KM_SLEEP);

	bzero((caddr_t)cdb, CDB_GROUP5);
	cdb[0] = (char)SCMD_READ_CDDA;
	cdb[2] = (((cdda->cdda_addr) & 0xff000000) >> 24);
	cdb[3] = (((cdda->cdda_addr) & 0x00ff0000) >> 16);
	cdb[4] = (((cdda->cdda_addr) & 0x0000ff00) >> 8);
	cdb[5] = ((cdda->cdda_addr) & 0x000000ff);
	cdb[6] = (((cdda->cdda_length) & 0xff000000) >> 24);
	cdb[7] = (((cdda->cdda_length) & 0x00ff0000) >> 16);
	cdb[8] = (((cdda->cdda_length) & 0x0000ff00) >> 8);
	cdb[9] = ((cdda->cdda_length) & 0x000000ff);
	cdb[10] = cdda->cdda_subcode;

	com->uscsi_cdb = cdb;
	com->uscsi_cdblen = CDB_GROUP5;
	com->uscsi_bufaddr = (caddr_t)cdda->cdda_data;
	com->uscsi_buflen = buflen;
	com->uscsi_flags = USCSI_SILENT|USCSI_READ;

	rval = sdioctl_cmd(dev, com,
		UIO_SYSSPACE, UIO_USERSPACE, UIO_SYSSPACE);

	kmem_free((caddr_t)cdda, sizeof (struct cdrom_cdda));
	kmem_free((caddr_t)com, sizeof (*com));
	return (rval);
}

static int
sr_read_cdxa(dev_t dev, caddr_t data, int flag)
{
	register struct uscsi_cmd	*com;
	register struct cdrom_cdxa	*cdxa;
	int				rval;
	int				buflen;
	char				cdb[CDB_GROUP5];

	GET_SOFT_STATE(dev);
#ifdef lint
	part = part;
#endif /* lint */

	cdxa = (struct cdrom_cdxa *)
		kmem_zalloc(sizeof (struct cdrom_cdxa), KM_SLEEP);

	if (ddi_copyin((caddr_t)data, (caddr_t)cdxa,
		sizeof (struct cdrom_cdxa), flag)) {
		scsi_log(SD_DEVINFO, sd_label, CE_WARN,
			"sr_read_cdxa: ddi_copyin Failed\n");
		kmem_free((caddr_t)cdxa, sizeof (struct cdrom_cdxa));
		return (EFAULT);
	}

	switch (cdxa->cdxa_format) {
	case CDROM_XA_DATA:
		buflen = CDROM_BLK_2048 * cdxa->cdxa_length;
		break;

	case CDROM_XA_SECTOR_DATA:
		buflen = CDROM_BLK_2352 * cdxa->cdxa_length;
		break;

	case CDROM_XA_DATA_W_ERROR:
		buflen = CDROM_BLK_2646 * cdxa->cdxa_length;
		break;

	default:
		scsi_log(SD_DEVINFO, sd_label, CE_WARN,
			"sr_read_cdxa: Format '0x%x' Not Supported\n",
			cdxa->cdxa_format);
		kmem_free((caddr_t)cdxa, sizeof (struct cdrom_cdxa));
		return (EINVAL);
	}

	com = (struct uscsi_cmd *)kmem_zalloc(sizeof (*com), KM_SLEEP);

	bzero((caddr_t)cdb, CDB_GROUP5);
	cdb[0] = (char)SCMD_READ_CDXA;
	cdb[2] = (((cdxa->cdxa_addr) & 0xff000000) >> 24);
	cdb[3] = (((cdxa->cdxa_addr) & 0x00ff0000) >> 16);
	cdb[4] = (((cdxa->cdxa_addr) & 0x0000ff00) >> 8);
	cdb[5] = ((cdxa->cdxa_addr) & 0x000000ff);
	cdb[6] = (((cdxa->cdxa_length) & 0xff000000) >> 24);
	cdb[7] = (((cdxa->cdxa_length) & 0x00ff0000) >> 16);
	cdb[8] = (((cdxa->cdxa_length) & 0x0000ff00) >> 8);
	cdb[9] = ((cdxa->cdxa_length) & 0x000000ff);
	cdb[10] = cdxa->cdxa_format;

	com->uscsi_cdb = cdb;
	com->uscsi_cdblen = CDB_GROUP5;
	com->uscsi_bufaddr = (caddr_t)cdxa->cdxa_data;
	com->uscsi_buflen = buflen;
	com->uscsi_flags = USCSI_SILENT|USCSI_READ;

	rval = sdioctl_cmd(dev, com,
		UIO_SYSSPACE, UIO_USERSPACE, UIO_SYSSPACE);

	kmem_free((caddr_t)cdxa, sizeof (struct cdrom_cdxa));
	kmem_free((caddr_t)com, sizeof (*com));
	return (rval);
}

static int
sr_read_all_subcodes(dev_t dev, caddr_t data, int flag)
{
	register struct uscsi_cmd	*com;
	register struct cdrom_subcode	*subcode;
	int				rval;
	int				buflen;
	char				cdb[CDB_GROUP5];

	GET_SOFT_STATE(dev);
#ifdef lint
	part = part;
#endif /* lint */

	subcode = (struct cdrom_subcode *)
		kmem_zalloc(sizeof (struct cdrom_subcode), KM_SLEEP);

	if (ddi_copyin((caddr_t)data, (caddr_t)subcode,
		sizeof (struct cdrom_subcode), flag)) {
		scsi_log(SD_DEVINFO, sd_label, CE_WARN,
			"sr_read_all_subcodes: ddi_copyin Failed\n");
		kmem_free((caddr_t)subcode, sizeof (struct cdrom_subcode));
		return (EFAULT);
	}

	buflen = CDROM_BLK_SUBCODE * subcode->cdsc_length;

	com = (struct uscsi_cmd *)kmem_zalloc(sizeof (*com), KM_SLEEP);

	bzero((caddr_t)cdb, CDB_GROUP5);
	cdb[0] = (char)SCMD_READ_ALL_SUBCODES;
	cdb[6] = (((subcode->cdsc_length) & 0xff000000) >> 24);
	cdb[7] = (((subcode->cdsc_length) & 0x00ff0000) >> 16);
	cdb[8] = (((subcode->cdsc_length) & 0x0000ff00) >> 8);
	cdb[9] = ((subcode->cdsc_length) & 0x000000ff);

	com->uscsi_cdb = cdb;
	com->uscsi_cdblen = CDB_GROUP5;
	com->uscsi_bufaddr = (caddr_t)subcode->cdsc_addr;
	com->uscsi_buflen = buflen;
	com->uscsi_flags = USCSI_SILENT|USCSI_READ;

	rval = sdioctl_cmd(dev, com,
		UIO_SYSSPACE, UIO_USERSPACE, UIO_SYSSPACE);

	kmem_free((caddr_t)subcode, sizeof (struct cdrom_subcode));
	kmem_free((caddr_t)com, sizeof (*com));
	return (rval);
}

/*
 * This routine sets the drive to read 512 bytes per sector
 */
static int
sr_sector_mode(dev_t dev, int mode)
{
	int r;
	u_char state;
	register char *sense, *select;
	struct scsi_disk *un;

	if ((un = ddi_get_soft_state(sd_state, SDUNIT(dev))) == NULL ||
		(un->un_state == SD_STATE_DETACHING))
		return (ENXIO);

	ASSERT(mutex_owned(SD_MUTEX));

	state = un->un_last_state;
	New_state(un, SD_STATE_OPENING);
	mutex_exit(SD_MUTEX);

	sense = kmem_zalloc(20, KM_SLEEP);
	if ((r = sr_mode_sense(dev, 0x81, sense, 20)) != 0) {
		SD_DEBUG2(SD_DEVINFO, sd_label, SCSI_DEBUG,
		    "sr_sector_mode: Mode Sense failed\n");
		kmem_free((caddr_t)sense, 20);
		mutex_enter(SD_MUTEX);
		un->un_state = un->un_last_state;
		un->un_last_state = state;
		return (r);
	}
	select = kmem_zalloc(20, KM_SLEEP);

	select[3] = 0x08;

	if (mode == 2) {
		select[10] = 0x09;
		select[11] = 0x20;
		select[12] = 0x01;
		select[13] = 0x06;
		select[14] = sense[14] | 0x01;
		select[15] = sense[15];
	} else {
		select[10] = ((un->un_secsize >> 8) & 0xff);
		select[11] = (un->un_secsize & 0xff);
		select[12] = 0x01;
		select[13] = 0x06;
		select[14] = sense[14];
		select[15] = sense[15];
	}

	if ((r = sr_mode_select(dev, select, 20)) != 0) {
		SD_DEBUG2(SD_DEVINFO, sd_label, SCSI_DEBUG,
		    "sr_sector_mode: Mode Select failed\n");
		kmem_free((caddr_t)select, 20);
		kmem_free((caddr_t)sense, 20);
		mutex_enter(SD_MUTEX);
		un->un_state = un->un_last_state;
		un->un_last_state = state;
		return (r);
	}
	kmem_free((caddr_t)sense, 20);
	kmem_free((caddr_t)select, 20);
	mutex_enter(SD_MUTEX);
	return (0);
}

static void
sd_build_user_vtoc(struct scsi_disk *un, struct vtoc *vtoc)
{

	int i;
	long nblks;
	struct dk_map2 *lpart;
	struct dk_map	*lmap;
	struct partition *vpart;

	ASSERT(mutex_owned(SD_MUTEX));

	/*
	 * Return vtoc structure fields in the provided VTOC area, addressed
	 * by *vtoc.
	 *
	 */

	bzero((caddr_t)vtoc, sizeof (struct vtoc));

	bcopy((caddr_t)un->un_vtoc.v_bootinfo, (caddr_t)vtoc->v_bootinfo,
	    sizeof (vtoc->v_bootinfo));

	vtoc->v_sanity		= VTOC_SANE;
	vtoc->v_version		= un->un_vtoc.v_version;

	bcopy((caddr_t)un->un_vtoc.v_volume, (caddr_t)vtoc->v_volume,
	    LEN_DKL_VVOL);

	vtoc->v_sectorsz = un->un_secsize;
	vtoc->v_nparts = un->un_vtoc.v_nparts;

	bcopy((caddr_t)un->un_vtoc.v_reserved, (caddr_t)vtoc->v_reserved,
	    sizeof (vtoc->v_reserved));
	/*
	 * Convert partitioning information.
	 *
	 * Note the conversion from starting cylinder number
	 * to starting sector number.
	 */
	lmap = un->un_map;
	lpart = un->un_vtoc.v_part;
	vpart = vtoc->v_part;

	nblks = un->un_g.dkg_nsect * un->un_g.dkg_nhead;

	for (i = 0; i < V_NUMPAR; i++) {
		vpart->p_tag	= lpart->p_tag;
		vpart->p_flag	= lpart->p_flag;
		vpart->p_start	= lmap->dkl_cylno * nblks;
		vpart->p_size	= lmap->dkl_nblk;

		lmap++;
		lpart++;
		vpart++;
	}

	bcopy((caddr_t)un->un_vtoc.v_timestamp, (caddr_t)vtoc->timestamp,
	    sizeof (vtoc->timestamp));

	bcopy((caddr_t)un->un_asciilabel, (caddr_t)vtoc->v_asciilabel,
	    LEN_DKL_ASCII);

}

static int
sd_build_label_vtoc(struct scsi_disk *un, struct vtoc *vtoc)
{

	struct dk_map		*lmap;
	struct dk_map2		*lpart;
	struct partition	*vpart;
	long			nblks;
	long			ncyl;
	int			i;

	ASSERT(mutex_owned(SD_MUTEX));

	/*
	 * Sanity-check the vtoc
	 */
	if (vtoc->v_sanity != VTOC_SANE || vtoc->v_sectorsz != un->un_secsize ||
			vtoc->v_nparts != V_NUMPAR) {
		return (EINVAL);
	}

	if ((nblks = un->un_g.dkg_nsect * un->un_g.dkg_nhead) == 0) {
		return (EINVAL);
	}

	vpart = vtoc->v_part;
	for (i = 0; i < V_NUMPAR; i++) {
		if ((vpart->p_start % nblks) != 0)
			return (EINVAL);
		ncyl = vpart->p_start / nblks;
		ncyl += vpart->p_size / nblks;
		if ((vpart->p_size % nblks) != 0)
			ncyl++;
		if (ncyl > (long)un->un_g.dkg_ncyl)
			return (EINVAL);
		vpart++;
	}


	/*
	 * Put appropriate vtoc structure fields into the disk label
	 *
	 */

	bcopy((caddr_t)vtoc->v_bootinfo, (caddr_t)un->un_vtoc.v_bootinfo,
	    sizeof (vtoc->v_bootinfo));

	un->un_vtoc.v_sanity = vtoc->v_sanity;
	un->un_vtoc.v_version = vtoc->v_version;

	bcopy((caddr_t)vtoc->v_volume, (caddr_t)un->un_vtoc.v_volume,
	    LEN_DKL_VVOL);

	un->un_vtoc.v_nparts = vtoc->v_nparts;

	bcopy((caddr_t)vtoc->v_reserved, (caddr_t)un->un_vtoc.v_reserved,
	    sizeof (vtoc->v_reserved));

	/*
	 * Note the conversion from starting sector number
	 * to starting cylinder number.
	 * Return error if division results in a remainder.
	 */
	lmap = un->un_map;
	lpart = un->un_vtoc.v_part;
	vpart = vtoc->v_part;


	for (i = 0; i < (int)vtoc->v_nparts; i++) {
		lpart->p_tag  = vpart->p_tag;
		lpart->p_flag = vpart->p_flag;
		lmap->dkl_cylno = vpart->p_start / nblks;
		lmap->dkl_nblk = vpart->p_size;

		lmap++;
		lpart++;
		vpart++;
	}

	bcopy((caddr_t)vtoc->timestamp, (caddr_t)un->un_vtoc.v_timestamp,
	    sizeof (vtoc->timestamp));

	bcopy((caddr_t)vtoc->v_asciilabel, (caddr_t)un->un_asciilabel,
	    LEN_DKL_ASCII);

	return (0);
}

/*
 * Disk geometry macros
 *
 *	spc:		sectors per cylinder
 *	chs2bn:		cyl/head/sector to block number
 */
#define	spc(l)		(((l)->dkl_nhead*(l)->dkl_nsect)-(l)->dkl_apc)

#define	chs2bn(l, c, h, s)	\
			((daddr_t)((c)*spc(l)+(h)*(l)->dkl_nsect+(s)))


static int
sd_write_label(dev_t dev)
{
	int i, status;
	int sec, blk, head, cyl;
	short sum, *sp;
	register struct scsi_disk *un;
	caddr_t buffer;
	struct dk_label *dkl;
	struct uscsi_cmd	ucmd;
	union scsi_cdb cdb;

	if ((un = ddi_get_soft_state(sd_state, SDUNIT(dev))) == NULL ||
		(un->un_state == SD_STATE_DETACHING))
		return (ENXIO);

	ASSERT(mutex_owned(SD_MUTEX));

	buffer = kmem_zalloc(sizeof (struct dk_label), KM_SLEEP);
	dkl = (struct dk_label *)buffer;
	bzero((caddr_t)dkl, sizeof (struct dk_label));

	bcopy((caddr_t)un->un_asciilabel, (caddr_t)dkl->dkl_asciilabel,
	    LEN_DKL_ASCII);
	bcopy((caddr_t)&un->un_vtoc, (caddr_t)&(dkl->dkl_vtoc),
	    sizeof (struct dk_vtoc));

	dkl->dkl_rpm	= un->un_g.dkg_rpm;
	dkl->dkl_pcyl	= un->un_g.dkg_pcyl;
	dkl->dkl_apc	= un->un_g.dkg_apc;
	dkl->dkl_obs1	= un->un_g.dkg_obs1;
	dkl->dkl_obs2	= un->un_g.dkg_obs2;
	dkl->dkl_intrlv = un->un_g.dkg_intrlv;
	dkl->dkl_ncyl	= un->un_g.dkg_ncyl;
	dkl->dkl_acyl	= un->un_g.dkg_acyl;
	dkl->dkl_nhead	= un->un_g.dkg_nhead;
	dkl->dkl_nsect	= un->un_g.dkg_nsect;
	dkl->dkl_obs3	= un->un_g.dkg_obs3;

	bcopy((caddr_t)un->un_map, (caddr_t)dkl->dkl_map,
	    NDKMAP * sizeof (struct dk_map));

	dkl->dkl_magic			= DKL_MAGIC;
	dkl->dkl_write_reinstruct	= un->un_g.dkg_write_reinstruct;
	dkl->dkl_read_reinstruct	= un->un_g.dkg_read_reinstruct;

	/*
	 * Construct checksum for the new disk label
	 */
	sum = 0;
	sp = (short *)dkl;
	i = sizeof (struct dk_label)/sizeof (short);
	while (i--) {
		sum ^= *sp++;
	}
	dkl->dkl_cksum = sum;

	/*
	 * Do the USCSI Write here
	 */

	/*
	 * Write the Primary label using the USCSI interface
	 * Write at Block 0.
	 * Build and execute the uscsi ioctl.  We build a group0
	 * or group1 command as necessary, since some targets
	 * may not support group1 commands.
	 */
	(void) bzero((caddr_t)&ucmd, sizeof (ucmd));
	(void) bzero((caddr_t)&cdb, sizeof (union scsi_cdb));
	cdb.scc_cmd = SCMD_WRITE;
	ucmd.uscsi_flags = USCSI_WRITE;
	FORMG0ADDR(&cdb, 0);
	FORMG0COUNT(&cdb, (u_char)1);
	ucmd.uscsi_cdblen = CDB_GROUP0;
	ucmd.uscsi_cdb = (caddr_t)&cdb;
	ucmd.uscsi_bufaddr = (caddr_t)dkl;
	ucmd.uscsi_buflen = un->un_secsize;
	ucmd.uscsi_flags |= USCSI_SILENT;
	mutex_exit(SD_MUTEX);
	status = sdioctl_cmd(dev, &ucmd, UIO_SYSSPACE, UIO_SYSSPACE,
		UIO_SYSSPACE);
	mutex_enter(SD_MUTEX);
	if (status != 0) {
		kmem_free(buffer, sizeof (struct dk_label));
		return (status);
	}



	/*
	 * Calculate where the backup labels go.  They are always on
	 * the last alternate cylinder, but some older drives put them
	 * on head 2 instead of the last head.	They are always on the
	 * first 5 odd sectors of the appropriate track.
	 *
	 * We have no choice at this point, but to believe that the
	 * disk label is valid.	 Use the geometry of the disk
	 * as described in the label.
	 */
	cyl = dkl->dkl_ncyl + dkl->dkl_acyl - 1;
	head = dkl->dkl_nhead-1;

	/*
	 * Write and verify the backup labels.
	 */
	for (sec = 1; sec < 5 * 2 + 1; sec += 2) {
		blk = chs2bn(dkl, cyl, head, sec);
		if (blk < (2<<20)) {
			FORMG0ADDR(&cdb, blk);
			FORMG0COUNT(&cdb, (u_char)1);
			cdb.scc_cmd = SCMD_WRITE;
			ucmd.uscsi_cdblen = CDB_GROUP0;
		} else {
			FORMG1ADDR(&cdb, blk);
			FORMG1COUNT(&cdb, 1);
			cdb.scc_cmd = SCMD_WRITE;
			ucmd.uscsi_cdblen = CDB_GROUP1;
			cdb.scc_cmd |= SCMD_GROUP1;
		}
		mutex_exit(SD_MUTEX);
		status = sdioctl_cmd(dev, &ucmd, UIO_SYSSPACE, UIO_SYSSPACE,
			UIO_SYSSPACE);
		mutex_enter(SD_MUTEX);
		if (status != 0) {
			kmem_free(buffer, sizeof (struct dk_label));
			return (status);
		}
	}

	kmem_free(buffer, sizeof (struct dk_label));
	return (0);

}


static void
clean_print(dev_info_t *dev, char *label, u_int level,
	char *title, char *data, int len)
{
	int	i;
	char	buf[256];

	sprintf(buf, "%s:", title);
	for (i = 0; i < len; i++) {
		sprintf(&buf[strlen(buf)], "0x%x ", (data[i] & 0xff));
	}
	sprintf(&buf[strlen(buf)], "\n");

	scsi_log(dev, label, level, "%s", buf);
}

/*
 * Print a piece of inquiry data- cleaned up for non-printable characters
 * and stopping at the first space character after the beginning of the
 * passed string;
 */

static void
inq_fill(char *p, int l, char *s)
{
	register unsigned i = 0;
	char c;

	while (i++ < l) {
		if ((c = *p++) < ' ' || c >= 0177) {
			c = '*';
		} else if (i != 1 && c == ' ') {
			break;
		}
		*s++ = c;
	}
	*s++ = 0;
}

static char *
sd_sname(
	u_char	status)
{
	switch (status & STATUS_MASK) {
	case STATUS_GOOD:
		return ("good status");

	case STATUS_CHECK:
		return ("check condition");

	case STATUS_MET:
		return ("condition met");

	case STATUS_BUSY:
		return ("busy");

	case STATUS_INTERMEDIATE:
		return ("intermediate");

	case STATUS_INTERMEDIATE_MET:
		return ("intermediate - condition met");

	case STATUS_RESERVATION_CONFLICT:
		return ("reservation_conflict");

	case STATUS_TERMINATED:
		return ("command terminated");

	case STATUS_QFULL:
		return ("queue full");

	default:
		return ("<unknown status>");
	}
}
