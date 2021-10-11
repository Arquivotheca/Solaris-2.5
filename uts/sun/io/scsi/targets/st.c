/*
 * Copyright (c) 1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)st.c 1.130	95/06/15 SMI"

/*
 * SCSI	 SCSA-compliant and not-so-DDI-compliant Tape Driver
 */

#if defined(lint) && !defined(DEBUG)
#define	DEBUG	1
#endif

#include <sys/modctl.h>
#include <sys/scsi/scsi.h>
#include <sys/mtio.h>
#include <sys/scsi/targets/stdef.h>
#include <sys/file.h>
#include <sys/stat.h>

/*
 * Global External Data Definitions
 */
extern struct scsi_key_strings scsi_cmds[];

/*
 * Local Static Data
 */
static void *st_state;
static int st_max_instance;
static int st_last_scanned_instance;	/* used in st_runout */
static char *st_label = "st";

/*
 * Tunable parameters. See <scsi/targets/stdef.h>
 * for what the initial values are.
 */

static int st_selection_retry_count = ST_SEL_RETRY_COUNT;
static int st_retry_count	= ST_RETRY_COUNT;
static int st_io_time		= ST_IO_TIME;
static int st_space_time	= ST_SPACE_TIME;
static int st_error_level	= SCSI_ERR_RETRYABLE;

extern int st_ndrivetypes;	/* defined in st_conf.c */
extern struct st_drivetype st_drivetypes[];

/*
 * the st_attach_mutex protects st_max_instance during multi-threaded attach
 * situations
 */
static kmutex_t st_attach_mutex;

static int st_report_soft_errors_on_close = 1;
static int st_enable_buffered_writes = 1;
static int st_allow_large_xfer = 1;
#ifdef STDEBUG
static int st_soft_error_report_debug = 0;
static int st_debug = 0;
#endif

static struct driver_minor_data {
	char	*name;
	int	minor;
	int	type;
} st_minor_data[] = {
	/*
	 *	top 3 entries use default densities - don't alter their
	 *	position - they will be changed later in st_attach
	 */
	{"", MT_DENSITY2, S_IFCHR},
	{"n", MT_DENSITY2 | MT_NOREWIND, S_IFCHR},
	{"b", MT_DENSITY2 | MT_BSD, S_IFCHR},

	{"l", MT_DENSITY1, S_IFCHR},
	{"m", MT_DENSITY2, S_IFCHR},
	{"h", MT_DENSITY3, S_IFCHR},
	{"c", MT_DENSITY4, S_IFCHR},
	{"u", MT_DENSITY4, S_IFCHR},
	{"ln", MT_DENSITY1 | MT_NOREWIND, S_IFCHR},
	{"mn", MT_DENSITY2 | MT_NOREWIND, S_IFCHR},
	{"hn", MT_DENSITY3 | MT_NOREWIND, S_IFCHR},
	{"cn", MT_DENSITY4 | MT_NOREWIND, S_IFCHR},
	{"un", MT_DENSITY4 | MT_NOREWIND, S_IFCHR},
	{"lb", MT_DENSITY1 | MT_BSD, S_IFCHR},
	{"mb", MT_DENSITY2 | MT_BSD, S_IFCHR},
	{"hb", MT_DENSITY3 | MT_BSD, S_IFCHR},
	{"cb", MT_DENSITY4 | MT_BSD, S_IFCHR},
	{"ub", MT_DENSITY4 | MT_BSD, S_IFCHR},
	{"lbn", MT_DENSITY1 | MT_NOREWIND | MT_BSD, S_IFCHR},
	{"mbn", MT_DENSITY2 | MT_NOREWIND | MT_BSD, S_IFCHR},
	{"bn", MT_DENSITY2 | MT_NOREWIND | MT_BSD, S_IFCHR},
	{"hbn", MT_DENSITY3 | MT_NOREWIND | MT_BSD, S_IFCHR},
	{"cbn", MT_DENSITY4 | MT_NOREWIND | MT_BSD, S_IFCHR},
	{"ubn", MT_DENSITY4 | MT_NOREWIND | MT_BSD, S_IFCHR},
	{0}
};

/* default density offsets in the table above */
#define	DEF_BLANK	0
#define	DEF_NOREWIND	1
#define	DEF_BSD		2

/*
 * Private reserved option bits, that are being hidden from the user
 */
#define	ST_NOPARITY	ST_RESERVED_BIT1


/*
 * Configuration Data:
 *
 * Device driver ops vector
 */
static int st_open(), st_close(), st_read(), st_write();
static int st_strategy(), st_ioctl();
extern int nulldev(), nodev();

static struct cb_ops st_cb_ops = {
	st_open,			/* open */
	st_close,		/* close */
	st_strategy,		/* strategy */
	nodev,			/* print */
	nodev,			/* dump */
	st_read,			/* read */
	st_write,		/* write */
	st_ioctl,		/* ioctl */
	nodev,			/* devmap */
	nodev,			/* mmap */
	nodev,			/* segmap */
	nochpoll,		/* poll */
	ddi_prop_op,		/* cb_prop_op */
	0,			/* streamtab  */
	D_64BIT | D_MP | D_NEW	/* Driver compatibility flag */
};

static int stinfo(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg,
		void **result);
static int st_identify(dev_info_t *dev);
static int st_probe(dev_info_t *dev);
static int st_attach(dev_info_t *dev, ddi_attach_cmd_t cmd);
static int st_detach(dev_info_t *dev, ddi_detach_cmd_t cmd);

static struct dev_ops st_ops = {
	DEVO_REV,		/* devo_rev, */
	0,			/* refcnt  */
	stinfo,			/* info */
	st_identify,		/* identify */
	st_probe,		/* probe */
	st_attach,		/* attach */
	st_detach,		/* detach */
	nodev,			/* reset */
	&st_cb_ops,		/* driver operations */
	(struct bus_ops *)0,	/* bus operations */
	NULL			/* power */
};

/*
 * Local Function Declarations
 */
static void st_clean_print(dev_info_t *dev, char *label, u_int level,
	char *title, char *data, int len);
static int st_doattach(struct scsi_device *devp, int (*canwait)());
static void st_known_tape_type(struct scsi_tape *un);
static int st_rw(dev_t dev, struct uio *uio, int flag);
static int st_find_eom(dev_t dev);
static int st_check_density_or_wfm(dev_t dev, int wfm, int mode, int stepflag);
static int st_ioctl(dev_t dev, int cmd, int arg, int flag,
	cred_t *cred_p, int *rval_p);
static int st_ioctl_cmd(dev_t dev, struct uscsi_cmd *,
	enum uio_seg, enum uio_seg, enum uio_seg);
static int st_mtioctop(struct scsi_tape *un, int arg, int flag);
static void st_start(struct scsi_tape *un, int flag);
static int st_handle_start_busy(struct scsi_tape *un, struct buf *bp,
    long timeout_interval);
static int st_handle_intr_busy(struct scsi_tape *un, struct buf *bp,
    long timeout_interval);
static void st_done_and_mutex_exit(struct scsi_tape *un, struct buf *bp);
static void st_init(struct scsi_tape *un);
static void st_make_cmd(struct scsi_tape *un, struct buf *bp,
	int (*func)());
static void st_make_uscsi_cmd(struct scsi_tape *, struct uscsi_cmd *,
	struct buf *, int (*)());
static void st_intr(struct scsi_pkt *pkt);
static void st_set_state(struct scsi_tape *un);
static void st_test_append(struct buf *bp);
static int st_runout();
static int st_cmd(dev_t dev, int com, int count, int wait);
static int st_write_fm(dev_t dev, int wfm);
static int st_determine_generic(dev_t dev);
static int st_determine_density(dev_t dev, int rw);
static int st_get_density(dev_t dev);
static int st_set_density(dev_t dev);
static int st_loadtape(dev_t dev);
static int st_modeselect(dev_t dev);
static int st_handle_incomplete(struct scsi_tape *un);
static int st_wrongtapetype(struct scsi_tape *un);
static int st_check_error(struct scsi_tape *un, struct scsi_pkt *pkt);
static int st_handle_sense(struct scsi_tape *un);
static int st_handle_autosense(struct scsi_tape *un);
static int st_decode_sense(struct scsi_tape *un, int amt,
    struct scsi_status *);
static int st_report_soft_errors(dev_t dev, int flag);
static void st_intr_restart(caddr_t arg);
static void st_start_restart(caddr_t arg);
static int st_gen_mode_sense(dev_t dev, int page, char *page_data, int
    page_size);
static int st_gen_mode_select(dev_t dev, char *page_data, int page_size);
static int st_tape_init(dev_t dev);

#ifdef STDEBUG
static void
st_debug_cmds(struct scsi_tape *un, int com, int count, int wait);
#endif /* STDEBUG */

/* VARIABLES PROTECTED BY "Unshared Data": scsi_pkt::, buf::, uio:: */
/* VARIABLES PROTECTED BY "No Mutex Needed": scsi_device::, st_drivetype:: */
/* VARIABLES PROTECTED BY "No Mutex Needed": scsi_address:: */
/* VARIABLES PROTECTED BY "No Mutex Needed": st_max_instance */
/* VARIABLES PROTECTED BY "No Mutex Needed": st_minor_data.minor */

/*
 * autoconfiguration routines.
 */
char _depends_on[] = "misc/scsi";

static struct modldrv modldrv = {
	&mod_driverops,		/* Type of module. This one is a driver */
	"SCSI Sequential Access Driver",	/* Name of the module. */
	&st_ops			/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1, &modldrv, NULL
};

int
_init(void)
{
	int e;

	if (((e = ddi_soft_state_init(&st_state,
	    sizeof (struct scsi_tape), ST_MAXUNIT)) != 0)) {
		return (e);
	}

	mutex_init(&st_attach_mutex, "st_attach_mutex", MUTEX_DRIVER, NULL);
	if ((e = mod_install(&modlinkage)) != 0) {
		mutex_destroy(&st_attach_mutex);
		ddi_soft_state_fini(&st_state);
	}

	return (e);
}

int
_fini(void)
{
	int e;

	if ((e = mod_remove(&modlinkage)) != 0) {
		return (e);
	}

	mutex_destroy(&st_attach_mutex);
	ddi_soft_state_fini(&st_state);

	return (e);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}


static int
st_identify(dev_info_t *dev)
{
	if (strcmp(ddi_get_name(dev), st_label) == 0) {
		return (DDI_IDENTIFIED);
	} else {
		return (DDI_NOT_IDENTIFIED);
	}
}


static int
st_probe(dev_info_t *devi)
{
	int instance;
	register struct scsi_device *devp;
	int rval = DDI_PROBE_PARTIAL;

	devp = (struct scsi_device *)ddi_get_driver_private(devi);
	instance = ddi_get_instance(devi);

	/*
	 * Keep a count of max number of tape instances we may have for the
	 * Runout Routine (this is *not* the actual number of tape drives).
	 */
	mutex_enter(&st_attach_mutex);
	if (instance > st_max_instance)
		st_max_instance = instance;
	mutex_exit(&st_attach_mutex);

	if (ddi_get_soft_state(st_state, instance) != NULL)
		return (DDI_PROBE_PARTIAL);


	/*
	 * Turn around and call probe routine to see whether
	 * we actually have a tape at this SCSI nexus.
	 */
	switch (scsi_probe(devp, NULL_FUNC)) {
	case SCSIPROBE_EXISTS:
		if ((devp->sd_inq->inq_dtype & DTYPE_MASK) ==
		    DTYPE_SEQUENTIAL) {
			ST_DEBUG6(devi, st_label, SCSI_DEBUG,
			    "probe exists\n");
			rval = DDI_PROBE_SUCCESS;
			break;

		}
		rval = DDI_PROBE_FAILURE;
		break;

	case SCSIPROBE_BUSY:
	case SCSIPROBE_NONCCS:
	case SCSIPROBE_NOMEM:
	case SCSIPROBE_NORESP:
	case SCSIPROBE_FAILURE:
	default:
		ST_DEBUG6(devi, st_label, SCSI_DEBUG,
		    "probe failure: nothing there\n");
		break;
	}
	scsi_unprobe(devp);
	return (rval);
}

/*ARGSUSED*/
static int
st_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	int instance, wide;
	register struct scsi_device *devp;
	struct driver_minor_data *dmdp;
	register struct scsi_tape *un;

	/* NO OTHER THREADS ARE RUNNING */

	devp  = (struct scsi_device *)ddi_get_driver_private(devi);
	instance = ddi_get_instance(devi);


	switch (cmd) {
		case DDI_ATTACH:
			if (st_doattach(devp, NULL_FUNC) == DDI_FAILURE) {
				return (DDI_FAILURE);
			}
			break;
		case DDI_RESUME:
			if (!(un = ddi_get_soft_state(st_state, instance)))
				return (DDI_FAILURE);
			mutex_enter(ST_MUTEX);
			un->un_suspended = 0;
			mutex_exit(ST_MUTEX);
			return (DDI_SUCCESS);
		default:
			return (DDI_FAILURE);
	}

	un = ddi_get_soft_state(st_state, instance);

	ST_DEBUG(devi, st_label, SCSI_DEBUG,
	    "st_attach: instance=%x\n", instance);

	/*
	 * find the drive type for this target
	 */
	st_known_tape_type(un);

	/*
	 * For default devices change the
	 * density to the preferred default density for this device
	 */
	st_minor_data[DEF_BLANK].minor = un->un_dp->default_density;
	st_minor_data[DEF_NOREWIND].minor = un->un_dp->default_density |
					MT_NOREWIND;
	st_minor_data[DEF_BSD].minor = un->un_dp->default_density |
					MT_BSD;

	for (dmdp = st_minor_data; dmdp->name != NULL; dmdp++) {
		if (ddi_create_minor_node(devi, dmdp->name, dmdp->type,
		    (MTMINOR(instance)) | dmdp->minor,
		    DDI_NT_TAPE, NULL) == DDI_FAILURE) {

			ddi_remove_minor_node(devi, NULL);
			if (un) {
				cv_destroy(&un->un_clscv);
				cv_destroy(&un->un_sbuf_cv);
				cv_destroy(&un->un_clscv);
				if (un->un_sbufp) {
					freerbuf(un->un_sbufp);
				}
				if (un->un_mspl) {
					ddi_iopb_free((caddr_t)un->un_mspl);
				}
				scsi_destroy_pkt(un->un_rqs);
				scsi_free_consistent_buf(un->un_rqs_bp);
				ddi_soft_state_free(st_state, instance);
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

	ddi_report_dev(devi);

	/*
	 * If it's a SCSI-2 tape drive which supports wide,
	 * tell the host adapter to use wide.
	 */
	wide = ((devp->sd_inq->inq_rdf == RDF_SCSI2) &&
	    (devp->sd_inq->inq_wbus16 || devp->sd_inq->inq_wbus32)) ?
		1 : 0;

	if (scsi_ifsetcap(ROUTE, "wide-xfer", wide, 1) == 1) {
		ST_DEBUG(devi, st_label, SCSI_DEBUG,
		    "Wide Transfer %s\n", wide ? "enabled" : "disabled");
	}

	/*
	 * enable autorequest sense; keep the rq packet around in case
	 * the autorequest sense fails because of a busy condition
	 */
	un->un_arq_enabled =
	    ((scsi_ifsetcap(ROUTE, "auto-rqsense", 1, 1) == 1) ? 1 : 0);

	ST_DEBUG(devi, st_label, SCSI_DEBUG, "auto request sense %s\n",
		(un->un_arq_enabled ? "enabled" : "disabled"));

	/*
	 * check if untagged queuing is supported
	 */
	if (un->un_arq_enabled &&
	    ((scsi_ifgetcap(ROUTE, "untagged-qing", 0) == 1))) {
		un->un_throttle =  ddi_getprop(DDI_DEV_T_ANY, devi,
				DDI_PROP_DONTPASS,
				"tape-driver-buffering", 0);
		un->un_max_buf_size =  ddi_getprop(DDI_DEV_T_ANY, devi,
				DDI_PROP_DONTPASS,
				"tape-driver-buf-max-size",
				1024 * 1024);
	} else {
		un->un_throttle = 1;
	}

	un->un_last_throttle = un->un_throttle;

	ST_DEBUG(devi, st_label, SCSI_DEBUG, "throttle=%x, max_buf=%x\n",
		un->un_throttle, un->un_max_buf_size);

	/*
	 * Get dma-max from HBA driver. If it is not defined, use 64k
	 */
	un->un_maxdma	= scsi_ifgetcap(&devp->sd_address, "dma-max", 1);
	if (un->un_maxdma == -1) {
		un->un_maxdma = (64 * 1024);
	}
	un->un_allow_large_xfer = (u_char)st_allow_large_xfer;
	un->un_maxbsize = MAXBSIZE_UNKNOWN;

	return (DDI_SUCCESS);
}

/*
 * st_detach:
 * we allow a detach if and only if:
 * - no tape is currently inserted
 * - tape position is at BOT or unknown
 *   (if it is not at BOT then a no rewind
 *   device was opened and we have to preserve state)
 */
/*ARGSUSED*/
static int
st_detach(dev_info_t *devi, ddi_detach_cmd_t cmd)
{
	int instance;
	register struct scsi_device *devp;
	register struct scsi_tape *un;

	/* NO OTHER THREADS ARE RUNNING */

	switch (cmd) {

	case DDI_DETACH:
		/*
		 * check if every instance can be unloaded before actually
		 * starting to unload this one; this prevents the needless
		 * detach/re-attach sequence
		 */
		for (instance = 0; instance < st_max_instance; instance++) {
			if ((un = (struct scsi_tape *)
			    ddi_get_soft_state(st_state, instance)) == NULL) {
				continue;
			}

			if (((un->un_dp->options & ST_UNLOADABLE) == 0) ||
			    (un->un_state != ST_STATE_CLOSED)) {
				/*
				 * we cannot unload some targets because the
				 * inquiry returns junk unless immediately
				 * after a reset
				 */
				ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "cannot unload instance %x\n", instance);
				return (DDI_FAILURE);
			}

			/*
			 * if we are not at BOT then it is not safe to unload
			 */
			if (!(((un->un_fileno == 0) && (un->un_blkno == 0)) ||
			    (un->un_fileno < 0))) {
				ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "cannot detach instance %x: ", instance);
				ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "fileno=%x, blkno=%x\n",
				    un->un_fileno, un->un_blkno);
				return (DDI_FAILURE);
			}
		}

		/*
		 * Undo what we did in st_attach & st_doattach,
		 * freeing resources and removing things we installed.
		 * The system framework guarantees we are not active
		 * with this devinfo node in any other entry points at
		 * this time.
		 */
		instance = ddi_get_instance(devi);

		if ((un = (struct scsi_tape *)
		    ddi_get_soft_state(st_state, instance)) == NULL) {
			return (DDI_FAILURE);
		} else {
			int err = 0;

			ST_DEBUG(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_detach: instance=%x, un=%x\n", instance, un);

			/*
			 * if the tape has been removed then we may unload;
			 * do a test unit ready and if it returns NOT READY
			 * then we assume that it is safe to unload.
			 * as a side effect, fileno may be set to -1 if the
			 * the test unit ready fails;
			 * also un_state may be set to non-closed, so reset it
			 */
			if (un->un_dev) {
				mutex_enter(ST_MUTEX);
				err = st_cmd(un->un_dev, SCMD_TEST_UNIT_READY,
				    0, SYNC_CMD);
				mutex_exit(ST_MUTEX);
			}
			un->un_state = ST_STATE_CLOSED;
			ST_DEBUG(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "err=%x, un_status=%x, fileno=%x, blkno=%x\n",
			    err, un->un_status, un->un_fileno, un->un_blkno);

			/*
			 * check again:
			 * if we are not at BOT then it is not safe to unload
			 */
			if (!(((un->un_fileno == 0) && (un->un_blkno == 0)) ||
			    (un->un_fileno < 0))) {
				ST_DEBUG(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "cannot detach: fileno=%x, blkno=%x\n",
				    un->un_fileno, un->un_blkno);
				return (DDI_FAILURE);
			}
		}

		/*
		 * now remove other data structures allocated in st_doattach()
		 */
		ST_DEBUG(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "destroying/freeing\n");
		cv_destroy(&un->un_clscv);
		cv_destroy(&un->un_sbuf_cv);
		cv_destroy(&un->un_clscv);	/* XXX mem fix */

		if (un->un_sbufp) {
			freerbuf(un->un_sbufp);
		}
		if (un->un_mspl) {
			ddi_iopb_free((caddr_t)un->un_mspl);
		}
		if (un->un_rqs) {
			scsi_destroy_pkt(un->un_rqs);
			scsi_free_consistent_buf(un->un_rqs_bp);

		}
		if (un->un_arq_enabled) {
			scsi_ifsetcap(ROUTE, "auto-rqsense", 1, 0);
		}
		if (un->un_dp->options & ST_DYNAMIC) {
			kmem_free(un->un_dp, un->un_dp_size);
		}
		devp = ST_SCSI_DEVP;
		ddi_soft_state_free(st_state, instance);
		devp->sd_private = (opaque_t)0;
		devp->sd_sense = (struct scsi_extended_sense *)0;
		scsi_unprobe(devp);
		ddi_prop_remove_all(devi);
		ddi_remove_minor_node(devi, NULL);
		ST_DEBUG(0, st_label, SCSI_DEBUG, "st_detach done\n");
		return (DDI_SUCCESS);

	case DDI_SUSPEND:
		instance = ddi_get_instance(devi);
		if (!(un = ddi_get_soft_state(st_state, instance)) ||
		    (un->un_state != ST_STATE_CLOSED)) {
			return (DDI_FAILURE);
		}
		mutex_enter(ST_MUTEX);
		un->un_suspended = 1;
		mutex_exit(ST_MUTEX);
		return (DDI_SUCCESS);

	default:
		ST_DEBUG(0, st_label, SCSI_DEBUG, "st_detach failed\n");
		return (DDI_FAILURE);
	}
}


/* ARGSUSED */
static int
stinfo(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result)
{
	register dev_t dev;
	register struct scsi_tape *un;
	register int instance, error;
	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		dev = (dev_t)arg;
		instance = MTUNIT(dev);
		if ((un = ddi_get_soft_state(st_state, instance)) == NULL)
			return (DDI_FAILURE);
		*result = (void *) ST_DEVINFO;
		error = DDI_SUCCESS;
		break;
	case DDI_INFO_DEVT2INSTANCE:
		dev = (dev_t)arg;
		instance = MTUNIT(dev);
		*result = (void *)instance;
		error = DDI_SUCCESS;
		break;
	default:
		error = DDI_FAILURE;
	}
	return (error);
}

static int
st_doattach(struct scsi_device *devp, int (*canwait)())
{
	struct scsi_pkt *rqpkt = 0;
	struct scsi_tape *un = 0;
	int km_flags = (canwait != NULL_FUNC) ? KM_SLEEP : KM_NOSLEEP;
	int instance;
	int rval = DDI_FAILURE;
	struct buf *bp;

	/* NO OTHER THREADS ARE RUNNING */

	/*
	 * Call the routine scsi_probe to do some of the dirty work.
	 * If the INQUIRY command succeeds, the field sd_inq in the
	 * device structure will be filled in.
	 */
	ST_DEBUG(devp->sd_dev, st_label, SCSI_DEBUG,
		"st_doattach(): probing %d.%d\n",
		devp->sd_address.a_target, devp->sd_address.a_lun);

	switch (scsi_probe(devp, (int (*)()) canwait)) {

	case SCSIPROBE_EXISTS:
		if ((devp->sd_inq->inq_dtype & DTYPE_MASK) ==
		    DTYPE_SEQUENTIAL) {
			ST_DEBUG(devp->sd_dev, st_label, SCSI_DEBUG,
			    "probe exists\n");
			break;
		}
		/* FALLTHROUGH */
	case SCSIPROBE_BUSY:
	case SCSIPROBE_NONCCS:
	case SCSIPROBE_NOMEM:
	case SCSIPROBE_NORESP:
	case SCSIPROBE_FAILURE:
	default:
		ST_DEBUG(devp->sd_dev, st_label, SCSI_DEBUG,
		    "probe failure: nothing there\n");
		return (rval);
	}

	bp = scsi_alloc_consistent_buf(&devp->sd_address, (struct buf *)NULL,
	    SENSE_LENGTH, B_READ, canwait, NULL);
	if (!bp) {
		return (rval);
	}
	rqpkt = scsi_init_pkt(&devp->sd_address,
	    (struct scsi_pkt *)NULL, bp, CDB_GROUP0, 1, 0,
	    PKT_CONSISTENT, canwait, NULL);
	if (!rqpkt) {
		goto error;
	}
	devp->sd_sense = (struct scsi_extended_sense *)bp->b_un.b_addr;
	ASSERT(geterror(bp) == 0);

	makecom_g0(rqpkt, devp, FLAG_NOPARITY,
	    SCMD_REQUEST_SENSE, 0, SENSE_LENGTH);


	/*
	 * The actual unit is present.
	 * Now is the time to fill in the rest of our info..
	 */
	instance = ddi_get_instance(devp->sd_dev);

	if (ddi_soft_state_zalloc(st_state, instance) != DDI_SUCCESS) {
		goto error;
	}
	un = ddi_get_soft_state(st_state, instance);

	un->un_sbufp = getrbuf(km_flags);

	(void) ddi_iopb_alloc(devp->sd_dev, (ddi_dma_lim_t *)0, (u_int) MSIZE,
	    (caddr_t *)&un->un_mspl);

	if (!un->un_sbufp || !un->un_mspl) {
		if (un->un_mspl) {
			ddi_iopb_free((caddr_t)un->un_mspl);
		}
		ST_DEBUG6(devp->sd_dev, st_label, SCSI_DEBUG,
		    "probe partial failure: no space\n");
		goto error;
	}

	cv_init(&un->un_sbuf_cv, "st_sbuf", CV_DRIVER, NULL);
	cv_init(&un->un_write_queue_cv, "st_write_queue_cv", CV_DRIVER,
	    NULL);
	cv_init(&un->un_clscv, "st_c", CV_DRIVER, NULL);

	rqpkt->pkt_flags |= (FLAG_SENSING | FLAG_HEAD | FLAG_NODISCON);

	un->un_fileno	= -1;
	rqpkt->pkt_time = st_io_time;
	rqpkt->pkt_comp = st_intr;
	un->un_rqs	= rqpkt;
	un->un_sd	= devp;
	un->un_rqs_bp	= bp;

	ST_DEBUG6(devp->sd_dev, st_label, SCSI_DEBUG, "probe success\n");
	return (DDI_SUCCESS);

error:
	if (bp) {
		scsi_free_consistent_buf(bp);
	}

	scsi_destroy_pkt(rqpkt);
	devp->sd_sense = (struct scsi_extended_sense *)0;

	if (un) {
		if (un->un_sbufp) {
			freerbuf(un->un_sbufp);
		}
		ddi_soft_state_free(st_state, instance);
		devp->sd_private = (opaque_t)0;
	}
	return (rval);
}

/*
 * determine tape type, using tape-config-list or built-in table or
 * use a generic tape config entry
 */
static void
st_known_tape_type(struct scsi_tape *un)
{
	struct st_drivetype *dp;
	auto int found = 0;
	caddr_t config_list = NULL;
	caddr_t data_list = NULL;
	int	*data_ptr;
	caddr_t vidptr, prettyptr, datanameptr;
	int	vidlen, prettylen, datanamelen, tripletlen = 0;
	int config_list_len, data_list_len, len, i;

	/* NO OTHER THREADS ARE RUNNING */

	/*
	 * Determine type of tape controller. Type is determined by
	 * checking the vendor ids of the earlier inquiry command and
	 * comparing those with vids in tape-config-list defined in st.conf
	 */
	if (ddi_getlongprop(DDI_DEV_T_ANY, ST_DEVINFO, DDI_PROP_DONTPASS,
	    "tape-config-list", (caddr_t)&config_list, &config_list_len)
	    == DDI_PROP_SUCCESS) {

		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		"st_known_tape_type(): looking at config info in st.conf\n");

		/*
		 * Compare vids in each triplet - if it matches, get value for
		 * data_name and contruct a st_drivetype struct
		 * tripletlen is not set yet!
		 */
		for (len = config_list_len, vidptr = config_list; len > 0;
		    vidptr += tripletlen, len -= tripletlen) {

			if (((vidlen = strlen(vidptr)) != 0) &&
			    bcmp(ST_INQUIRY->inq_vid, vidptr, vidlen) == 0) {

				prettyptr = vidptr + vidlen + 1;
				prettylen = strlen(prettyptr);
				datanameptr = prettyptr + prettylen + 1;
				/*
				 * if prettylen is zero then use the vid string
				 */
				if (prettylen == 0) {
					prettyptr = vidptr;
					prettylen = vidlen;
				}

				ST_DEBUG(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "vid = %s, pretty=%s, dataname = %s\n",
				    vidptr, prettyptr, datanameptr);

				/*
				 * get the data list
				 */
				if (ddi_getlongprop(DDI_DEV_T_ANY,
				    ST_DEVINFO, 0,
				    datanameptr, (caddr_t)&data_list,
				    &data_list_len) != DDI_PROP_SUCCESS) {
					/*
					 * Error in getting property value
					 * print warning!
					 */
					scsi_log(ST_DEVINFO, st_label, CE_WARN,
					    "data property (%s) has no value\n",
					    datanameptr);
					goto next;
				}

				/*
				 * now initialize the st_drivetype struct
				 */
				un->un_dp_size = (sizeof (struct st_drivetype))
				    + ST_NAMESIZE;
				dp = (struct st_drivetype *)kmem_zalloc
				    ((size_t)un->un_dp_size, KM_SLEEP);
				dp->name = (caddr_t)((u_long) dp +
				    sizeof (struct st_drivetype));
				strncpy(dp->name, prettyptr, ST_NAMESIZE - 1);
				strcpy(dp->vid, vidptr);
				dp->length = vidlen;
				data_ptr = (int *)data_list;
				/*
				 * check if data is enough for version, type,
				 * bsize, options, # of densities, density1,
				 * density2, ..., default_density
				 */
				if ((data_list_len < 5 * sizeof (int)) ||
				    (data_list_len < 6 * sizeof (int) +
				    *(data_ptr + 4) * sizeof (int))) {
					/*
					 * print warning and skip to next
					 * triplet
					 */
					scsi_log(ST_DEVINFO, st_label, CE_WARN,
					    "data property (%s) incomplete\n",
					    datanameptr);
					kmem_free(data_list, data_list_len);
					kmem_free(dp, un->un_dp_size);
					un->un_dp_size = 0;
					goto next;
				}
				/*
				 * check version
				 */
				if (*data_ptr++ != 1) {
					/* print warning but accept it */
					scsi_log(ST_DEVINFO, st_label, CE_WARN,
					    "Version # for data property (%s) "
					    "greater than 1\n", datanameptr);
				}

				dp->type = *data_ptr++;
				dp->bsize = *data_ptr++;
				dp->options = *data_ptr++;
				dp->options |= ST_DYNAMIC;
				for (len = *data_ptr++, i = 0;
				    i < len && i < NDENSITIES; i++) {
					dp->densities[i] = *data_ptr++;
				}
				dp->default_density = *data_ptr << 3;
				kmem_free(data_list, data_list_len);
				found = 1;
				ST_DEBUG(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "found in st.conf: vid = %s, pretty=%s\n",
				    dp->vid, dp->name);
				break;
			}
next:
			prettyptr = vidptr + vidlen + 1;
			prettylen = strlen(prettyptr);
			datanameptr = prettyptr + prettylen + 1;
			datanamelen = strlen(datanameptr);
			tripletlen = vidlen + prettylen + datanamelen + 3;
		}
	}

	/*
	 * free up the memory allocated by ddi_getlongprop
	 */
	if (config_list) {
		kmem_free(config_list, config_list_len);
	}

	/*
	 * Determine type of tape controller.  Type is determined by
	 * checking the result of the earlier inquiry command and
	 * comparing vendor ids with strings in a table declared in st_conf.c.
	 */
	if (!found) {
		ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "st_known_tape_type(): looking at st_drivetype table\n");

		for (dp = st_drivetypes;
		    dp < &st_drivetypes[st_ndrivetypes]; dp++) {
			if (dp->length == 0) {
				continue;
			}
			if (bcmp(ST_INQUIRY->inq_vid,
			    dp->vid, dp->length) == 0) {
				found = 1;
				break;
			}
		}
	}


	if (!found) {
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "st_known_tape_type(): making drivetype from INQ cmd\n");

		un->un_dp_size = (sizeof (struct st_drivetype)) + ST_NAMESIZE;
		dp = (struct st_drivetype *)
		    kmem_zalloc((size_t)un->un_dp_size, KM_SLEEP);
		dp->name = (caddr_t)((u_long) dp + sizeof (*dp));

		/*
		 * Make up a name
		 */
		bcopy("Vendor '", dp->name, 8);
		bcopy((caddr_t)ST_INQUIRY->inq_vid, &dp->name[8], VIDLEN);
		bcopy("' Product '", &dp->name[16], 11);
		bcopy((caddr_t)ST_INQUIRY->inq_pid, &dp->name[27], PIDLEN);
		dp->name[ST_NAMESIZE - 1] = '\'';
		dp->name[ST_NAMESIZE] = '\0';
		/*
		 * 'clean' vendor and product strings of non-printing chars
		 */
		for (i = 0; i < ST_NAMESIZE - 1; i++) {
			if (dp->name[i] < 040 || dp->name[i] > 0176)
				dp->name[i] = '.';
		}
		dp->options |= ST_DYNAMIC;
	}

	/*
	 * Store tape drive characteristics.
	 */
	un->un_dp = dp;
	un->un_status = 0;
	un->un_attached = 1;

	/* make sure if we are supposed to be variable, make it variable */
	if (dp->options & ST_VARIABLE)
		dp->bsize = 0;

	if ((dp->options & ST_NOPARITY) == 0) {
		un->un_rqs->pkt_flags &= ~FLAG_NOPARITY;
	}
	scsi_log(ST_DEVINFO, st_label, CE_NOTE, "?<%s>\n", dp->name);
}

/*
 * Regular Unix Entry points
 */

/* ARGSUSED */
static int
st_open(dev_t *dev_p, int flag, int otyp, cred_t *cred_p)
{
	register dev_t dev = *dev_p;
	int rval = 0;

	GET_SOFT_STATE(dev);

	/*
	 * validate that we are addressing a sensible unit
	 */
	mutex_enter(ST_MUTEX);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_open(dev = 0x%x, flag = %d, otyp = %d)\n", *dev_p, flag, otyp);

	if (un->un_suspended) {
		rval = EBUSY;
		goto exit;
	}

	if (!un->un_attached) {
		st_known_tape_type(un);
		if (!un->un_attached) {
			rval = ENXIO;
			goto exit;
		}

	}

	/*
	 * Check for the case of the tape in the middle of closing.
	 * This isn't simply a check of the current state, because
	 * we could be in state of sensing with the previous state
	 * that of closing.
	 *
	 * And don't allow multiple opens.
	 */
	if (!(flag & (FNDELAY | FNONBLOCK)) && IS_CLOSING(un)) {
		while (IS_CLOSING(un)) {
			if (cv_wait_sig(&un->un_clscv, ST_MUTEX) == 0) {
				rval = EINTR;
				goto exit;
			}
		}
	} else if (un->un_state != ST_STATE_CLOSED) {
		rval = EBUSY;
		goto exit;
	}

	/*
	 * record current dev
	 */
	un->un_dev = dev;
	un->un_oflags = flag;	/* save for use in st_tape_init() */

	/*
	 * If we are opening O_NDELAY, or O_NONBLOCK, we don't check for
	 * anything, leave internal states alone, if fileno >= 0
	 */
	if (flag & (FNDELAY | FNONBLOCK)) {
		if (un->un_fileno < 0) {
			un->un_state = ST_STATE_OFFLINE;
		}
		rval = 0;
	} else {
		rval = st_tape_init(dev);
		un->un_state = ST_STATE_OPEN_PENDING_IO;
	}

exit:
	/*
	 * we don't want any uninvited guests scrogging our data when we're
	 * busy with something, so for successful opens or failed opens
	 * (except for EBUSY), reset these counters and state appropriately.
	 */
	if (rval != EBUSY) {
		if (rval) {
			un->un_state = ST_STATE_CLOSED;
		}
		un->un_err_resid = 0;
		un->un_retry_ct = 0;
		un->un_tran_retry_ct = 0;
	}

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_open: return val = %x, state = %d\n", rval, un->un_state);
	mutex_exit(ST_MUTEX);
	return (rval);

}

static int
st_tape_init(register dev_t dev)
{
	int err;
	int rval = 0;

	GET_SOFT_STATE(dev);

	ASSERT(mutex_owned(ST_MUTEX));

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_tape_init(dev = 0x%x, oflags = %d)\n", dev, un->un_oflags);

	/*
	 * Clean up after any errors left by 'last' close.
	 * This also handles the case of the initial open.
	 */
	if (un->un_state != ST_STATE_INITIALIZING) {
		un->un_laststate = un->un_state;
		un->un_state = ST_STATE_OPENING;
	}

	un->un_kbytes_xferred = 0;

	/*
	 * do a throw away TUR to clear check condition
	 */
	(void) st_cmd(dev, SCMD_TEST_UNIT_READY, 0, SYNC_CMD);

	/*
	 * See whether this is a generic device that we haven't figured
	 * anything out about yet.
	 */
	if (un->un_dp->type == ST_TYPE_INVALID) {
		if (st_determine_generic(dev)) {
			un->un_state = ST_STATE_CLOSED;
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_open: EIO invalid type\n");
			rval = EIO;
			goto exit;
		}
	}

	/*
	 * If we allow_large_xfer (ie >64k) and have not yet found out
	 * the max block size supported by the drive, find it by issueing
	 * a READ_BLKLIM command. if READ_BLKLIM cmd fails, assume drive
	 * doesn't allow_large_xfer and min/max block sizes as 1 byte and
	 * 63k.
	 */
	if ((un->un_dp->options & ST_NO_RECSIZE_LIMIT) == 0) {
		un->un_allow_large_xfer = 0;
	}
	if (un->un_allow_large_xfer &&
	    (un->un_maxbsize == MAXBSIZE_UNKNOWN)) {

		un->un_rbl = (struct read_blklim *)
			kmem_zalloc(RBLSIZE, KM_SLEEP);
		if ((un->un_rbl &&
		    !st_cmd(dev, SCMD_READ_BLKLIM, RBLSIZE, SYNC_CMD)) ||
		    !st_cmd(dev, SCMD_READ_BLKLIM, RBLSIZE, SYNC_CMD)) {

			/*
			 * if cmd successful, use limit returned
			 */
			un->un_maxbsize = (un->un_rbl->max_hi << 16) +
					(un->un_rbl->max_mid << 8) +
					un->un_rbl->max_lo;
			un->un_minbsize = (un->un_rbl->min_hi << 8) +
					un->un_rbl->min_lo;
		} else {
			/*
			 * since cmd failed, do not allow large xfers.
			 * use old values in st_minphys
			 */
			un->un_allow_large_xfer = 0;

			/*
			 * we guess maxbsize and minbsize
			 */
			if (un->un_bsize) {
				un->un_maxbsize = un->un_minbsize =
					un->un_bsize;
			} else {
				un->un_maxbsize = ST_MAXRECSIZE_FIXED;
				un->un_minbsize = 1;
			}
		}
		if (un->un_rbl) {
			kmem_free(un->un_rbl, RBLSIZE);
		}
		un->un_rbl = NULL;
	}

	ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "maxdma = %d, maxbsize = %d, minbsize = %d, %s large xfer\n",
	    un->un_maxdma, un->un_maxbsize, un->un_minbsize,
	    (un->un_allow_large_xfer ? "ALLOW": "DON'T ALLOW"));

	err = st_cmd(dev, SCMD_TEST_UNIT_READY, 0, SYNC_CMD);

	if (err != 0) {
		if (err == EINTR) {
			un->un_laststate = un->un_state;
			un->un_state = ST_STATE_CLOSED;
			goto exit;
		}
		/*
		 * Make sure the tape is ready
		 */
		un->un_fileno = -1;
		if (un->un_status != KEY_UNIT_ATTENTION) {
			un->un_laststate = un->un_state;
			un->un_state = ST_STATE_CLOSED;
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_open: EIO tape is not ready\n");
			rval = EIO;
			goto exit;
		}
	}

	/*
	 * On each open, initialize block size from drivetype struct,
	 * as it could have been changed by MTSRSZ ioctl.
	 * Now, ST_VARIBLE simply means drive is capable of variable
	 * mode. All drives are assumed to support fixed records.
	 * Hence, un_bsize tells what mode the drive is in.
	 *	un_bsize	= 0	- variable record length
	 *			= x	- fixed record length is x
	 */
	un->un_bsize = un->un_dp->bsize;

	if ((un->un_fileno < 0) && st_loadtape(dev)) {
		un->un_laststate = un->un_state;
		un->un_state = ST_STATE_CLOSED;
		ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "st_open : EIO can't open tape\n");
		rval = EIO;
		goto exit;
	}

	/*
	 * do a mode sense to pick up state of current write-protect,
	 */
	(void) st_cmd(dev, SCMD_MODE_SENSE, MSIZE, SYNC_CMD);

	/*
	 * If we are opening the tape for writing, check
	 * to make sure that the tape can be written.
	 */
	if (un->un_oflags & FWRITE) {
		err = 0;
		if (un->un_mspl->wp)  {
			un->un_status = KEY_WRITE_PROTECT;
			un->un_laststate = un->un_state;
			un->un_state = ST_STATE_CLOSED;
			rval = EACCES;
			goto exit;
		} else {
			un->un_read_only = 0;
		}
	} else {
		un->un_read_only = 1;
	}

	/*
	 * If we're opening the tape write-only, we need to
	 * write 2 filemarks on the HP 1/2 inch drive, to
	 * create a null file.
	 */
	if ((un->un_oflags == FWRITE) && (un->un_dp->options & ST_REEL)) {
		un->un_fmneeded = 2;
	} else if (un->un_oflags == FWRITE) {
		un->un_fmneeded = 1;
	} else {
		un->un_fmneeded = 0;
	}

	ST_DEBUG(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "fmneeded = %x\n", un->un_fmneeded);

	/*
	 * Make sure the density can be selected correctly.
	 */
	if (st_determine_density(dev, B_WRITE)) {
		un->un_status = KEY_ILLEGAL_REQUEST;
		un->un_laststate = un->un_state;
		un->un_state = ST_STATE_CLOSED;
		ST_DEBUG(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "st_open: EIO can't determine density\n");
		rval = EIO;
		goto exit;
	}

	/*
	 * Destroy the knowledge that we have 'determined'
	 * density so that a later read at BOT comes along
	 * does the right density determination.
	 */

	un->un_density_known = 0;


	/*
	 * Okay, the tape is loaded and either at BOT or somewhere past.
	 * Mark the state such that any I/O or tape space operations
	 * will get/set the right density, etc..
	 */
	un->un_laststate = un->un_state;
	un->un_lastop = ST_OP_NIL;

	/*
	 *  Set test append flag if writing.
	 *  First write must check that tape is positioned correctly.
	 */
	un->un_test_append = (un->un_oflags & FWRITE);

	un->un_allow_buffered_writes = (st_enable_buffered_writes &&
			    (un->un_dp->options & ST_BUFFERED_WRITES) &&
			    (un->un_throttle > 0) &&
			    (un->un_arq_enabled));
exit:
	un->un_err_resid = 0;

	ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_tape_init: return val = %x\n", rval);
	return (rval);

}



/* ARGSUSED */
static int
st_close(dev_t dev, int flag, int otyp, cred_t cred_p)
{
	int err = 0;
	int norew, count, last_state;

	GET_SOFT_STATE(dev);

	/*
	 * wait till all cmds in the pipeline have been completed
	 */
	mutex_enter(ST_MUTEX);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_close(dev = 0x%x, flag = %d, otyp = %d)\n", dev, flag, otyp);

	while (un->un_ncmds) {
		cv_wait(&un->un_write_queue_cv, ST_MUTEX);
	}

	/*
	 * set state to indicate that we are in process of closing
	 */
	last_state = un->un_laststate = un->un_state;
	un->un_state = ST_STATE_CLOSING;

	/*
	 * BSD behavior:
	 * a close always causes a silent span to the next file if we've hit
	 * an EOF (but not yet read across it).
	 */
	ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_close1: fileno=%x, blkno=%x, un_eof=%x\n", un->un_fileno,
	    un->un_blkno, un->un_eof);


	if (BSD_BEHAVIOR && (un->un_eof == ST_EOF)) {
		if (un->un_fileno >= 0) {
			un->un_fileno++;
			un->un_blkno = 0;
		}
		un->un_eof = ST_NO_EOF;
	}

	/*
	 * rewinding?
	 */
	norew = (getminor(dev) & MT_NOREWIND);

	/*
	 * SVR4 behavior for skipping to next file:
	 *
	 * If we have not seen a filemark, space to the next file
	 *
	 * If we have already seen the filemark we are physically in the next
	 * file and we only increment the filenumber
	 */

	if (norew && SVR4_BEHAVIOR && (flag & FREAD) && (un->un_blkno != 0) &&
	    (un->un_lastop != ST_OP_WRITE)) {
		switch (un->un_eof) {
		case ST_NO_EOF:
			/*
			 * if we were reading and did not read the complete file
			 * skip to the next file, leaving the tape correctly
			 * positioned to read the first record of the next file
			 * Check first for REEL if we are at EOT by trying to
			 * read a block
			 */
			if ((un->un_dp->options & ST_REEL) &&
			    (un->un_blkno == 0)) {
				if (st_cmd(dev, SCMD_SPACE, Blk(1), SYNC_CMD)) {
					ST_DEBUG2(ST_DEVINFO, st_label,
					    SCSI_DEBUG,
					    "st_close : EIO can't space\n");
					err = EIO;
					break;
				}
				if (un->un_eof >= ST_EOF_PENDING) {
					un->un_eof = ST_EOT_PENDING;
					un->un_fileno += 1;
					un->un_blkno   = 0;
					break;
				}
			}
			if (st_cmd(dev, SCMD_SPACE, Fmk(1), SYNC_CMD)) {
				ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "st_close: EIO can't space #2\n");
				err = EIO;
			} else {
				ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "st_close2: fileno=%x,blkno=%x,un_eof=%x\n",
				    un->un_fileno, un->un_blkno, un->un_eof);
				un->un_eof = ST_NO_EOF;
			}
			break;

		case ST_EOF_PENDING:
		case ST_EOF:
			un->un_fileno += 1;
			un->un_blkno   = 0;
			un->un_eof = ST_NO_EOF;
			break;

		case ST_EOT:
		case ST_EOT_PENDING:
			/* nothing to do */
			break;
		}
	}


	/*
	 * For performance reasons (HP 88780), the driver should
	 * postpone writing the second tape mark until just before a file
	 * positioning ioctl is issued (e.g., rewind).	This means that
	 * the user must not manually rewind the tape because the tape will
	 * be missing the second tape mark which marks EOM.
	 * However, this small performance improvement is not worth the risk.
	 */

	/*
	 * We need to back up over the filemark we inadvertently popped
	 * over doing a read in between the two filemarks that constitute
	 * logical eot for 1/2" tapes. Note that ST_EOT_PENDING is only
	 * set while reading.
	 *
	 * If we happen to be at physical eot (ST_EOM) (writing case),
	 * the writing of filemark(s) will clear the ST_EOM state, which
	 * we don't want, so we save this state and restore it later.
	 */

	ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "flag=%x, fmneeded=%x, lastop=%x, eof=%x\n",
		flag, un->un_fmneeded, un->un_lastop, un->un_eof);

	if (un->un_eof == ST_EOT_PENDING) {
		if (norew) {
			if (st_cmd(dev, SCMD_SPACE, Fmk((-1)), SYNC_CMD)) {
				ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "st_close: EIO can't space #3\n");
				err = EIO;
			} else {
				un->un_blkno = 0;
				un->un_eof = ST_EOT;
			}
		} else {
			un->un_eof = ST_NO_EOF;
		}

	/*
	 * Do we need to write a file mark?
	 *
	 * only write filemarks if there are fmks to be written and
	 *   - open for write (possibly read/write)
	 *   - the last operation was a write
	 * or:
	 *   -	opened for wronly
	 *   -	no data was written
	 */
	} else if ((un->un_fileno >= 0) && (un->un_fmneeded > 0) &&
	    (((flag & FWRITE) && (un->un_lastop == ST_OP_WRITE)) ||
	    ((flag & FWRITE) && (un->un_lastop == ST_OP_WEOF)) ||
	    ((flag == FWRITE) && (un->un_lastop == ST_OP_NIL)))) {

		/* save ST_EOM state */
		int was_at_eom = (un->un_eof == ST_EOM) ? 1 : 0;

		/*
		 * Note that we will write a filemark if we had opened
		 * the tape write only and no data was written, thus
		 * creating a null file.
		 *
		 * If the user already wrote one, we only have to write 1 more.
		 * If they wrote two, we don't have to write any.
		 */

		count = un->un_fmneeded;
		if (count > 0) {
			if (st_cmd(dev, SCMD_WRITE_FILE_MARK,
			    count, SYNC_CMD)) {
				ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "st_close : EIO can't wfm\n");
				err = EIO;
			}
			if ((un->un_dp->options & ST_REEL) && norew) {
				if (st_cmd(dev, SCMD_SPACE, Fmk((-1)),
				    SYNC_CMD)) {
					ST_DEBUG2(ST_DEVINFO, st_label,
					    SCSI_DEBUG,
					    "st_close : EIO space fmk(-1)\n");
					err = EIO;
				}
				un->un_eof = ST_NO_EOF;
				/* fix up block number */
				un->un_blkno = 0;
			}
		}

		/*
		 * If we aren't going to be rewinding, and we were at
		 * physical eot, restore the state that indicates we
		 * are at physical eot. Once you have reached physical
		 * eot, and you close the tape, the only thing you can
		 * do on the next open is to rewind. Access to trailer
		 * records is only allowed without closing the device.
		 */
		if (norew == 0 && was_at_eom)
			un->un_eof = ST_EOM;
	}


	/*
	 * report soft errors if enabled and available, if we never accessed
	 * the drive, don't get errors. This will prevent some DAT error
	 * messages upon LOG SENSE.
	 */
	if (st_report_soft_errors_on_close &&
	    (un->un_dp->options & ST_SOFT_ERROR_REPORTING) &&
	    (last_state != ST_STATE_OFFLINE)) {
		(void) st_report_soft_errors(dev, flag);
	}


	/*
	 * Do we need to rewind? Can we rewind?
	 */
	if (norew == 0 && un->un_fileno >= 0 && err == 0) {
		/*
		 * We'd like to rewind with the
		 * 'immediate' bit set, but this
		 * causes problems on some drives
		 * where subsequent opens get a
		 * 'NOT READY' error condition
		 * back while the tape is rewinding,
		 * which is impossible to distinguish
		 * from the condition of 'no tape loaded'.
		 *
		 * Also, for some targets, if you disconnect
		 * with the 'immediate' bit set, you don't
		 * actually return right away, i.e., the
		 * target ignores your request for immediate
		 * return.
		 *
		 * Instead, we'll fire off an async rewind
		 * command. We'll mark the device as closed,
		 * and any subsequent open will stall on
		 * the first TEST_UNIT_READY until the rewind
		 * completes.
		 *
		 */
		(void) st_cmd(dev, SCMD_REWIND, 0, ASYNC_CMD);
	}

	/*
	 * clear up state
	 */
	un->un_laststate = un->un_state;
	un->un_state = ST_STATE_CLOSED;
	un->un_lastop = ST_OP_NIL;
	un->un_throttle = un->un_last_throttle;
	un->un_retry_ct = 0;
	un->un_tran_retry_ct = 0;

	ASSERT(mutex_owned(ST_MUTEX));

	/*
	 * Signal anyone awaiting a close operation to complete.
	 */
	cv_signal(&un->un_clscv);

	/*
	 * any kind of error on closing causes all state to be tossed
	 */
	if (err && un->un_status != KEY_ILLEGAL_REQUEST) {
		un->un_density_known = 0;
		/*
		 * note that st_intr has already set un_fileno to -1
		 */
	}


	ST_DEBUG(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_close3: return val = %x, fileno=%x, blkno=%x, un_eof=%x\n",
			    err, un->un_fileno, un->un_blkno, un->un_eof);

	mutex_exit(ST_MUTEX);
	return (err);
}

/*
 * These routines perform raw i/o operations.
 */

/* ARGSUSED */
static int
st_read(dev_t dev, struct uio *uiop, cred_t *cred_p)
{
	return (st_rw(dev, uiop, B_READ));
}

/* ARGSUSED */
static int
st_write(dev_t dev, struct uio *uiop, cred_t *cred_p)
{
	return (st_rw(dev, uiop, B_WRITE));
}

/*
 * Due to historical reasons, old limits are: For variable-length devices:
 * if greater than 64KB - 1 (ST_MAXRECSIZE_VARIABLE), block into 64 KB - 2
 * ST_MAXRECSIZE_VARIABLE_LIMIT) requests; otherwise,
 * (let it through unmodified. For fixed-length record devices:
 * 63K (ST_MAXRECSIZE_FIXED) is max (default minphys).
 *
 * The new limits used are un_maxdma (retrieved using scsi_ifgetcap()
 * from the HBA) and un_maxbsize (retrieved by sending SCMD_READ_BLKLIM
 * command to the drive).
 *
 */
static void
st_minphys(struct buf *bp)
{
	register struct scsi_tape *un;

	/* LOCK UNNEEDED BECAUSE "stable data": un->un_sd->sd_mutex */

	un = ddi_get_soft_state(st_state, MTUNIT(bp->b_edev));

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_minphys(bp = 0x%x): b_bcount = 0x%x\n", bp, bp->b_bcount);

	if (un->un_allow_large_xfer) {

		/*
		 * check un_maxbsize for variable length devices only
		 */
		if (un->un_bsize == 0 && bp->b_bcount > un->un_maxbsize) {
			bp->b_bcount = un->un_maxbsize;
		}
		/*
		 * can't go more that HBA maxdma limit in either fixed-length
		 * or variable-length tape drives.
		 */
		if (bp->b_bcount > un->un_maxdma) {
			bp->b_bcount = un->un_maxdma;
		}
	} else {

		/*
		 *  use old fixed limits
		 */
		if (un->un_bsize == 0) {
			if (bp->b_bcount > ST_MAXRECSIZE_VARIABLE) {
				bp->b_bcount = ST_MAXRECSIZE_VARIABLE_LIMIT;
			}
		} else {
			if (bp->b_bcount > ST_MAXRECSIZE_FIXED) {
				bp->b_bcount = ST_MAXRECSIZE_FIXED;
			}
		}
	}
}

static int
st_rw(dev_t dev, struct uio *uio, int flag)
{
	register int rval, len = uio->uio_resid;

	GET_SOFT_STATE(dev);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_rw(dev = 0x%x, flag = %s)\n", dev,
	    (flag == B_READ ? "READ": "WRITE"));

	if (un->un_bsize != 0) {
		if (uio->uio_iov->iov_len % un->un_bsize) {
			scsi_log(ST_DEVINFO, st_label, CE_WARN,
			    "%s: not modulo %d block size\n",
			    (flag == B_WRITE) ? "write" : "read",
			    un->un_bsize);
			return (EINVAL);
		}
	}


	rval = physio(st_strategy, (struct buf *)NULL,
		dev, flag, st_minphys, uio);
	/*
	 * if we have hit logical EOT during this xfer and there is not a
	 * full residue, then set un_eof back  to ST_EOM to make sure that
	 * the user will see at least one zero write
	 * after this short write
	 */
	mutex_enter(ST_MUTEX);
	if (un->un_eof > ST_NO_EOF) {
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		"un_eof=%d resid=%x\n", un->un_eof, uio->uio_resid);
	}
	if (un->un_eof >= ST_EOM && (flag == B_WRITE)) {
		if ((uio->uio_resid != len) && (uio->uio_resid != 0)) {
			un->un_eof = ST_EOM;
		} else if (uio->uio_resid == len) {
			un->un_eof = ST_NO_EOF;
		}
	}
	mutex_exit(ST_MUTEX);

	return (rval);
}


static int
st_strategy(struct buf *bp)
{
	register struct scsi_tape *un;
	dev_t dev = bp->b_edev;

	/*
	 * validate arguments
	 */
	if ((un = ddi_get_soft_state(st_state, MTUNIT(bp->b_edev))) == NULL) {
		bp->b_resid = bp->b_bcount;
		bioerror(bp, ENXIO);
		goto error;
	}

	mutex_enter(ST_MUTEX);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_strategy(): bcount = 0x%x, fileno = %d, blkno = %d, eof = %d\n",
	    bp->b_bcount, un->un_fileno, un->un_blkno, un->un_eof);


	/*
	 * If we are offline, we have to initialize everything first...
	 */
	if (un->un_state == ST_STATE_OFFLINE) {
		/* reset state to avoid recursion */
		un->un_state = ST_STATE_INITIALIZING;
		if (st_tape_init(dev)) {
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "stioctl : OFFLINE init failure ");
			un->un_state = ST_STATE_OFFLINE;
			un->un_fileno = -1;
			goto b_done_err;
		}
	}

	if (bp != un->un_sbufp) {
		char reading = bp->b_flags & B_READ;
		int wasopening = 0;

		/*
		 * Check for legal operations
		 */
		if (un->un_fileno < 0) {
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "strategy with un->un_fileno < 0\n");
			goto b_done_err;
		}

		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "st_strategy(): regular io\n");

		/*
		 * Process this first. If we were reading, and we're pending
		 * logical eot, that means we've bumped one file mark too far.
		 */

		/*
		 * Recursion warning: st_cmd will route back through here.
		 */
		if (un->un_eof == ST_EOT_PENDING) {
			if (st_cmd(dev, SCMD_SPACE, Fmk((-1)), SYNC_CMD)) {
				un->un_fileno = -1;
				un->un_density_known = 0;
				goto b_done_err;
			}
			un->un_blkno = 0; /* fix up block number.. */
			un->un_eof = ST_EOT;
		}

		/*
		 * If we are in the process of opening, we may have to
		 * determine/set the correct density. We also may have
		 * to do a test_append (if QIC) to see whether we are
		 * in a position to append to the end of the tape.
		 *
		 * If we're already at logical eot, we transition
		 * to ST_NO_EOF. If we're at physical eot, we punt
		 * to the switch statement below to handle.
		 */
		if ((un->un_state == ST_STATE_OPEN_PENDING_IO) ||
		    (un->un_test_append && (un->un_dp->options & ST_QIC))) {

			if (un->un_state == ST_STATE_OPEN_PENDING_IO) {
				if (st_determine_density(dev, (int)reading)) {
					goto b_done_err;
				}
			}

			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "pending_io@fileno %d rw %d qic %d eof %d\n",
			    un->un_fileno, (int)reading,
			    (un->un_dp->options & ST_QIC) ? 1 : 0,
			    un->un_eof);

			if (!reading && un->un_eof != ST_EOM) {
				if (un->un_eof == ST_EOT) {
					un->un_eof = ST_NO_EOF;
				} else if (un->un_fileno > 0 &&
				    (un->un_dp->options & ST_QIC)) {
					/*
					 * st_test_append() will do it all
					 */
					st_test_append(bp);
					goto done;
				}
			}
			if (un->un_state == ST_STATE_OPEN_PENDING_IO) {
				wasopening = 1;
			}
			un->un_laststate = un->un_state;
			un->un_state = ST_STATE_OPEN;
		}


		/*
		 * Process rest of END OF FILE and END OF TAPE conditions
		 */

		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "un_eof=%x, wasopening=%x\n",
		    un->un_eof, wasopening);

		switch (un->un_eof) {
		case ST_EOM:
			/*
			 * This allows writes to proceed past physical
			 * eot. We'll *really* be in trouble if the
			 * user continues blindly writing data too
			 * much past this point (unwind the tape).
			 * Physical eot really means 'early warning
			 * eot' in this context.
			 *
			 * Every other write from now on will succeed
			 * (if sufficient  tape left).
			 * This write will return with resid == count
			 * but the next one should be successful
			 *
			 * Note that we only transition to logical EOT
			 * if the last state wasn't the OPENING state.
			 * We explicitly prohibit running up to physical
			 * eot, closing the device, and then re-opening
			 * to proceed. Trailer records may only be gotten
			 * at by keeping the tape open after hitting eot.
			 *
			 * Also note that ST_EOM cannot be set by reading-
			 * this can only be set during writing. Reading
			 * up to the end of the tape gets a blank check
			 * or a double-filemark indication (ST_EOT_PENDING),
			 * and we prohibit reading after that point.
			 *
			 */
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG, "EOM\n");
			if (wasopening == 0) {
				/*
				 * this allows st_rw() to reset it back to
				 * ST_EOM to make sure that the application
				 * will see a zero write
				 */
				un->un_eof = ST_WRITE_AFTER_EOM;
			}
			un->un_status = SUN_KEY_EOT;
			goto b_done;

		case ST_WRITE_AFTER_EOM:
		case ST_EOT:
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG, "EOT\n");
			un->un_status = SUN_KEY_EOT;
			if (SVR4_BEHAVIOR && reading) {
				goto b_done_err;
			}

			if (reading) {
				goto b_done;
			}
			un->un_eof = ST_NO_EOF;
			break;

		case ST_EOF_PENDING:
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "EOF PENDING\n");
			un->un_status = SUN_KEY_EOF;
			if (SVR4_BEHAVIOR) {
				un->un_eof = ST_EOF;
				goto b_done;
			}
			/* FALLTHROUGH */
		case ST_EOF:
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG, "EOF\n");
			un->un_status = SUN_KEY_EOF;
			if (SVR4_BEHAVIOR) {
				goto b_done_err;
			}

			if (BSD_BEHAVIOR) {
				un->un_eof = ST_NO_EOF;
				un->un_fileno += 1;
				un->un_blkno   = 0;
			}

			if (reading) {
				ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "now file %d (read)\n",
				    un->un_fileno);
				goto b_done;
			}
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "now file %d (write)\n", un->un_fileno);
			break;
		default:
			un->un_status = 0;
			break;
		}
	}

	bp->b_flags &= ~(B_DONE);
	bioerror(bp, 0);
	bp->av_forw = 0;
	bp->b_resid = 0;
	SET_BP_PKT(bp, 0);


	ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_strategy: cmd=0x%x  count=%d  resid=%d	flags=0x%x  pkt=0x%x\n",
	    (int)bp->b_forw, bp->b_bcount,
	    bp->b_resid, bp->b_flags, BP_PKT(bp));

	ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_strategy: un->un_quef = 0x%x, bp = 0x%x\n",
	    un->un_quef, bp);

	if (un->un_quef) {
		un->un_quel->b_actf = bp;
	} else {
		un->un_quef = bp;
	}
	un->un_quel = bp;
	st_start(un, ST_USER_CONTEXT);

done:
	mutex_exit(ST_MUTEX);
	return (0);

b_done_err:
	bioerror(bp, EIO);
	ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_strategy : EIO b_done_err\n");

	un->un_err_resid = bp->b_resid = bp->b_bcount;
	mutex_exit(ST_MUTEX);

error:
	ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_strategy: error exit\n");
	goto exit;

b_done:
	ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_strategy: b_done\n");
	un->un_err_resid = bp->b_resid = bp->b_bcount;
	mutex_exit(ST_MUTEX);

exit:
	biodone(bp);
	ASSERT(mutex_owned(ST_MUTEX) == 0);
	return (0);
}



/*
 * this routine spaces forward over filemarks
 */
static int
st_space_fmks(dev_t dev, int count)
{
	int rval = 0;

	GET_SOFT_STATE(dev);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_space_fmks(dev = 0x%x, count = %d)\n", dev, count);

	ASSERT(mutex_owned(ST_MUTEX));

	/*
	 * the risk with doing only one space operation is that we
	 * may accidentily jump in old data
	 * the exabyte 8500 reading 8200 tapes cannot use KNOWS_EOD
	 * because the 8200 does not append a marker; in order not to
	 * sacrifice the fast file skip, we do a slow skip if the low
	 * density device has been opened
	 */

	if ((un->un_dp->options & ST_KNOWS_EOD) &&
	    !((un->un_dp->type == ST_TYPE_EXB8500 && MT_DENSITY(dev) == 0))) {
		if (st_cmd(dev, SCMD_SPACE, Fmk(count), SYNC_CMD)) {
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "space_fmks : EIO can't do space cmd #1\n");
			rval = EIO;
		}
	} else {
		while (count > 0) {
			if (st_cmd(dev, SCMD_SPACE, Fmk(1), SYNC_CMD)) {
				ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "space_fmks : EIO can't do space cmd #2\n");
				rval = EIO;
				break;
			}
			count -= 1;
			/*
			 * read a block to see if we have reached
			 * end of medium (double filemark for reel or
			 * medium error for others)
			 */
			if (count > 0) {
				if (st_cmd(dev, SCMD_SPACE, Blk(1),
				    SYNC_CMD)) {
					ST_DEBUG2(ST_DEVINFO, st_label,
					    SCSI_DEBUG,
					    "space_fmks : EIO can't do "
					    "space cmd #3\n");
					rval = EIO;
					break;
				}
				if ((un->un_eof >= ST_EOF_PENDING) &&
				    (un->un_dp->options & ST_REEL)) {
					un->un_status = SUN_KEY_EOT;
					ST_DEBUG2(ST_DEVINFO, st_label,
					    SCSI_DEBUG,
					    "space_fmks : EIO ST_REEL\n");
					rval = EIO;
					break;
				} else if (IN_EOF(un)) {
					un->un_eof = ST_NO_EOF;
					un->un_fileno++;
					un->un_blkno = 0;
					count--;
				} else if (un->un_eof > ST_EOF) {
					ST_DEBUG2(ST_DEVINFO, st_label,
					    SCSI_DEBUG,
					    "space_fmks, EIO > ST_EOF\n");
					rval = EIO;
					break;
				}

			}
		}
		un->un_err_resid = count;
	}
	ASSERT(mutex_owned(ST_MUTEX));
	return (rval);
}

/*
 * this routine spaces to EOM
 * it keeps track of the current filenumber and returns the
 * filenumber after the last successful space operation
 */
#define	MAX_SKIP	0x1000 /* somewhat arbitrary */

static int
st_find_eom(dev_t dev)
{
	int count, savefile;
	register struct scsi_tape *un;
	register int instance;

	instance = MTUNIT(dev);
	if ((un = ddi_get_soft_state(st_state, instance)) == NULL)
		return (-1);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_find_eom(dev = 0x%x): fileno = %d\n", dev, un->un_fileno);

	ASSERT(mutex_owned(ST_MUTEX));

	savefile = un->un_fileno;

	/*
	 * see if the drive is smart enough to do the skips in
	 * one operation; 1/2" use two filemarks
	 * the exabyte 8500 reading 8200 tapes cannot use KNOWS_EOD
	 * because the 8200 does not append a marker; in order not to
	 * sacrifice the fast file skip, we do a slow skip if the low
	 * density device has been opened
	 */
	if ((un->un_dp->options & ST_KNOWS_EOD) &&
	    !((un->un_dp->type == ST_TYPE_EXB8500 && MT_DENSITY(dev) == 0))) {
		count = MAX_SKIP;
	} else {
		count = 1;
	}

	while (st_cmd(dev, SCMD_SPACE, Fmk(count), SYNC_CMD) == 0) {

		savefile = un->un_fileno;
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			"count=%x, eof=%x, status=%x\n", count,  un->un_eof,
			un->un_status);

		/*
		 * If we're not EOM smart,  space a record
		 * to see whether we're now in the slot between
		 * the two sequential filemarks that logical
		 * EOM consists of (REEL) or hit nowhere land
		 * (8mm).
		 */
		if (count == 1) {
			/*
			 * no fast skipping, check a record
			 */
			if (st_cmd(dev, SCMD_SPACE, Blk((1)), SYNC_CMD))
				break;
			else if ((un->un_eof >= ST_EOF_PENDING) &&
			    (un->un_dp->options & ST_REEL)) {
				un->un_status = KEY_BLANK_CHECK;
				un->un_fileno++;
				un->un_blkno = 0;
				break;
			} else if (IN_EOF(un)) {
				un->un_eof = ST_NO_EOF;
				un->un_fileno++;
				un->un_blkno = 0;
			} else if (un->un_eof > ST_EOF) {
				break;
			}
		} else {
			if (un->un_eof >  ST_EOF) {
				break;
			}
		}
	}

	if (un->un_dp->options & ST_KNOWS_EOD) {
		savefile = un->un_fileno;
	}
	ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_find_eom: %x\n", savefile);
	ASSERT(mutex_owned(ST_MUTEX));
	return (savefile);
}


/*
 * this routine is frequently used in ioctls below;
 * it determines whether we know the density and if not will
 * determine it
 * if we have written the tape before, one or more filemarks are written
 *
 * depending on the stepflag, the head is repositioned to where it was before
 * the filemarks were written in order not to confuse step counts
 */
#define	STEPBACK    0
#define	NO_STEPBACK 1

static int
st_check_density_or_wfm(dev_t dev, int wfm, int mode, int stepflag)
{

	GET_SOFT_STATE(dev);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	"st_check_density_or_wfm(dev= 0x%x, wfm= %d, mode= %d, stpflg= %d)\n",
	dev, wfm, mode, stepflag);

	ASSERT(mutex_owned(ST_MUTEX));

	/*
	 * If we don't yet know the density of the tape we have inserted,
	 * we have to either unconditionally set it (if we're 'writing'),
	 * or we have to determine it. As side effects, check for any
	 * write-protect errors, and for the need to put out any file-marks
	 * before positioning a tape.
	 *
	 * If we are going to be spacing forward, and we haven't determined
	 * the tape density yet, we have to do so now...
	 */
	if (un->un_state == ST_STATE_OPEN_PENDING_IO) {
		if (st_determine_density(dev, mode)) {
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "check_density_or_wfm : EIO can't determine "
			    "density\n");
			return (EIO);
		}
		/*
		 * Presumably we are at BOT. If we attempt to write, it will
		 * either work okay, or bomb. We don't do a st_test_append
		 * unless we're past BOT.
		 */
		un->un_laststate = un->un_state;
		un->un_state = ST_STATE_OPEN;
	} else if (un->un_fmneeded > 0 ||
	    (un->un_lastop == ST_OP_WEOF && wfm) ||
	    (un->un_lastop == ST_OP_WRITE && wfm)) {
		int blkno = un->un_blkno;
		int fileno = un->un_fileno;

		/*
		 * We need to write one or two filemarks.
		 * In the case of the HP, we need to
		 * position the head between the two
		 * marks.
		 */
		if ((un->un_fmneeded > 0) || (un->un_lastop == ST_OP_WEOF)) {
			wfm = un->un_fmneeded;
			un->un_fmneeded = 0;
		}

		if (st_write_fm(dev, wfm)) {
			un->un_fileno = -1;
			un->un_density_known = 0;
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "check_density_or_wfm : EIO can't write fm\n");
			return (EIO);
		}

		if (stepflag == STEPBACK) {
			if (st_cmd(dev, SCMD_SPACE, Fmk((-wfm)), SYNC_CMD)) {
				ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "check_density_or_wfm : EIO can't space "
				    "(-wfm)\n");
				return (EIO);
			}
			un->un_blkno = blkno;
			un->un_fileno = fileno;
		}
	}

	/*
	 * Whatever we do at this point clears the state of the eof flag.
	 */

	un->un_eof = ST_NO_EOF;

	/*
	 * If writing, let's check that we're positioned correctly
	 * at the end of tape before issuing the next write.
	 */
	if (!un->un_read_only) {
		un->un_test_append = 1;
	}

	ASSERT(mutex_owned(ST_MUTEX));
	return (0);
}

/*
 * This routine implements the ioctl calls.  It is called
 * from the device switch at normal priority.
 */
/*ARGSUSED*/
static int
st_ioctl(dev_t dev, int cmd, int arg, int flag, cred_t *cred_p, int *rval_p)
{
	int tmp, rval = 0;

	GET_SOFT_STATE(dev);

	/*
	 * wait till outstanding commands completed
	 */
	mutex_enter(ST_MUTEX);
	while (un->un_ncmds) {
		cv_wait(&un->un_write_queue_cv, ST_MUTEX);
	}

	/*
	 * first and foremost, handle any ST_EOT_PENDING cases.
	 * That is, if a logical eot is pending notice, notice it.
	 */
	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_ioctl(): fileno=%x, blkno=%x, un_eof=%x, state = %d\n",
	    un->un_fileno, un->un_blkno, un->un_eof, un->un_state);

	if (un->un_eof == ST_EOT_PENDING) {
		int resid = un->un_err_resid;
		u_char status = un->un_status;
		u_char lastop = un->un_lastop;

		if (st_cmd(dev, SCMD_SPACE, Fmk((-1)), SYNC_CMD)) {
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "stioctl : EIO can't space fmk(-1)\n");
			rval = EIO;
			goto exit;
		}
		un->un_lastop = lastop; /* restore last operation */
		if (status == SUN_KEY_EOF) {
			un->un_status = SUN_KEY_EOT;
		} else {
			un->un_status = status;
		}
		un->un_err_resid  = resid;
		un->un_err_blkno = un->un_blkno = 0; /* fix up block number */
		un->un_eof = ST_EOT;	/* now we're at logical eot */
	}

	/*
	 * now, handle the rest of the situations
	 */
	switch (cmd) {
	case MTIOCGET:
		{
			/* Get tape status */
			struct mtget mtg_local;
			struct mtget *mtget = &mtg_local;
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
				"st_ioctl: MTIOCGET\n");

			bzero((caddr_t)mtget, sizeof (struct mtget));
			mtget->mt_erreg = un->un_status;
			mtget->mt_resid = un->un_err_resid;
			mtget->mt_dsreg = un->un_retry_ct;
			mtget->mt_fileno = un->un_err_fileno;
			mtget->mt_blkno = un->un_err_blkno;
			mtget->mt_type = un->un_dp->type;
			mtget->mt_flags = MTF_SCSI | MTF_ASF;
			if (un->un_dp->options & ST_REEL) {
				mtget->mt_flags |= MTF_REEL;
				mtget->mt_bf = 20;
			} else {		/* 1/4" cartridges */
				switch (mtget->mt_type) {
				/* Emulex cartridge tape */
				case MT_ISMT02:
					mtget->mt_bf = 40;
					break;
				default:
					mtget->mt_bf = 126;
					break;
				}
			}
			un->un_status = 0;		/* Reset status */
			un->un_err_resid = 0;
			tmp = sizeof (struct mtget);
			if (ddi_copyout((caddr_t)mtget,
			    (caddr_t)arg, tmp, flag)) {
				rval = EFAULT;
			}
			break;
		}

	case MTIOCGETDRIVETYPE:
		{
			/*
			 * return mtdrivetype
			 */
			struct mtdrivetype_request mtdtrq;
			struct mtdrivetype mtdrtyp;
			struct mtdrivetype *mtdt = &mtdrtyp;
			struct st_drivetype *stdt = un->un_dp;

			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
				"st_ioctl: MTIOCGETDRIVETYPE\n");

			if (ddi_copyin((caddr_t)arg, (caddr_t)&mtdtrq,
			    sizeof (struct mtdrivetype_request), flag)) {
				rval = EFAULT;
				break;
			}
			bzero((caddr_t)mtdt, sizeof (struct mtdrivetype));
			strncpy(mtdt->name, stdt->name, ST_NAMESIZE);
			strncpy(mtdt->vid, stdt->vid, VIDLEN + PIDLEN);
			mtdt->type = stdt->type;
			mtdt->bsize = stdt->bsize;
			mtdt->options = stdt->options;
			mtdt->max_rretries = stdt->max_rretries;
			mtdt->max_wretries = stdt->max_wretries;
			for (tmp = 0; tmp < NDENSITIES; tmp++)
				mtdt->densities[tmp] = stdt->densities[tmp];
			mtdt->default_density = stdt->default_density;
			for (tmp = 0; tmp < NSPEEDS; tmp++) {
				mtdt->speeds[tmp] = stdt->speeds[tmp];
			}
			tmp = sizeof (struct mtdrivetype);
			if (ddi_copyout((caddr_t)mtdt,
			    (caddr_t)mtdtrq.mtdtp, mtdtrq.size, flag)) {
				rval = EFAULT;
			}
			break;
		}

	case USCSICMD:
		{
			/*
			 * Run a generic USCSI command
			 */
			struct uscsi_cmd ucmd;
			enum uio_seg uioseg;

			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
				"st_ioctl: USCSICMD\n");

			if (drv_priv(cred_p) != 0) {
				rval = EPERM;
				break;
			}

			if (ddi_copyin((caddr_t)arg, (caddr_t)&ucmd,
			    sizeof (ucmd), flag)) {
				rval = EFAULT;
				break;
			}

			/*
			 * although st_ioctl_cmd() never makes use of these
			 * now, we are just being safe and consistent
			 */
			ucmd.uscsi_flags &= ~(USCSI_NOINTR | USCSI_NOPARITY |
			    USCSI_OTAG | USCSI_HTAG | USCSI_HEAD);


			uioseg = (flag & FKIOCTL) ?
			    UIO_SYSSPACE : UIO_USERSPACE;

			rval = st_ioctl_cmd(dev, &ucmd, uioseg, uioseg, uioseg);
			if (ddi_copyout((caddr_t)&ucmd, (caddr_t)arg,
			    sizeof (ucmd), flag)) {
				if (rval != 0) {
					rval = EFAULT;
				}
			}
			break;
		}

	case MTIOCTOP:
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			"st_ioctl: MTIOCTOP\n");
		rval = st_mtioctop(un, arg, flag);
		break;

	default:
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			"st_ioctl: unknown ioctl\n");
		rval = ENOTTY;
	}

exit:
	mutex_exit(ST_MUTEX);
	return (rval);
}


static int
st_mtioctop(struct scsi_tape *un, int arg, int flag)
{
	struct mtop *mtop, local;
	int savefile, tmp, rval = 0;
	dev_t dev = un->un_dev;

	ASSERT(mutex_owned(ST_MUTEX));

	mtop = &local;
	if (ddi_copyin((caddr_t)arg, (caddr_t)mtop,
	    sizeof (struct mtop), flag)) {
		return (EFAULT);
	}

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_ioctl(): mt_op=%x\n", mtop->mt_op);
	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "fileno=%x, blkno=%x, un_eof=%x\n", un->un_fileno, un->un_blkno,
	    un->un_eof);

	rval = 0;
	un->un_status = 0;

	/*
	 * if we are going to mess with a tape, we have to make sure we have
	 * one and are not offline (i.e. no tape is initialized).  We let
	 * commands pass here that don't actually touch the tape, except for
	 * loading and initialization (rewinding).
	 */
	if (un->un_state == ST_STATE_OFFLINE) {
		switch (mtop->mt_op) {
		case MTLOAD:
		case MTNOP:
			/*
			 * We don't want strategy calling st_tape_init here,
			 * so, change state
			 */
			un->un_state = ST_STATE_INITIALIZING;
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "stioctl : OFFLINE state = %d\n",
			    un->un_state);
			break;
		default:
			/*
			 * reinitialize by normal means
			 */
			if (st_tape_init(dev)) {
				un->un_state = ST_STATE_INITIALIZING;
				ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "stioctl : OFFLINE init failure ");
				un->un_state = ST_STATE_OFFLINE;
				un->un_fileno = -1;
				return (EIO);
			}
		}
	}

	switch (mtop->mt_op) {
	case MTERASE:
		/*
		 * MTERASE rewinds the tape, erase it completely, and returns
		 * to the beginning of the tape
		 */
		if (un->un_dp->options & ST_REEL)
			un->un_fmneeded = 2;

		if (un->un_mspl->wp || un->un_read_only) {
			un->un_status = KEY_WRITE_PROTECT;
			un->un_err_resid = mtop->mt_count;
			un->un_err_fileno = un->un_fileno;
			un->un_err_blkno = un->un_blkno;
			return (EACCES);
		}
		if (st_check_density_or_wfm(dev, 1, B_WRITE, NO_STEPBACK) ||
		    st_cmd(dev, SCMD_REWIND, 0, SYNC_CMD) ||
		    st_cmd(dev, SCMD_ERASE, 0, SYNC_CMD)) {
			un->un_fileno = -1;
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "stioctl : EIO space or erase or check den)\n");
			rval = EIO;
		} else {
			/* QIC and helical scan rewind after erase */
			if (un->un_dp->options & ST_REEL) {
				(void) st_cmd(dev, SCMD_REWIND, 0, ASYNC_CMD);
			}
		}
		break;

	case MTWEOF:
		/*
		 * write an end-of-file record
		 */
		if (un->un_mspl->wp || un->un_read_only) {
			un->un_status = KEY_WRITE_PROTECT;
			un->un_err_resid = mtop->mt_count;
			un->un_err_fileno = un->un_fileno;
			un->un_err_blkno = un->un_blkno;
			return (EACCES);
		}

		/*
		 * zero count means just flush buffers
		 * negative count is not permitted
		 */
		if (mtop->mt_count < 0)
			return (EINVAL);

		un->un_eof = ST_NO_EOF;

		if (!un->un_read_only) {
			un->un_test_append = 1;
		}

		if (un->un_state == ST_STATE_OPEN_PENDING_IO) {
			if (st_determine_density(dev, B_WRITE)) {
				ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "stioctl : EIO : MTWEOF can't determine"
				    "density");
				return (EIO);
			}
		}

		if (st_write_fm(dev, (int)mtop->mt_count)) {
			/*
			 * Failure due to something other than illegal
			 * request results in loss of state (st_intr).
			 */
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "stioctl : EIO : MTWEOF can't write file mark");
			rval = EIO;
		}
		break;

	case MTRETEN:
		/*
		 * retension the tape
		 * only for cartridge tape
		 */
		if ((un->un_dp->options & ST_QIC) == 0)
			return (ENOTTY);

		if (st_check_density_or_wfm(dev, 1, 0, NO_STEPBACK) ||
		    st_cmd(dev, SCMD_LOAD, 3, SYNC_CMD)) {
			un->un_fileno = -1;
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "stioctl : EIO : MTRETEN ");
			rval = EIO;
		}
		break;

	case MTREW:
		/*
		 * rewind  the tape
		 */
		if (st_check_density_or_wfm(dev, 1, 0, NO_STEPBACK)) {
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "stioctl : EIO : MTREW check density/wfm failed");
			return (EIO);
		}
		if (st_cmd(dev, SCMD_REWIND, 0, SYNC_CMD)) {
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "stioctl : EIO : MTREW ");
			rval = EIO;
		}
		break;

	case MTOFFL:
		/*
		 * rewinds, and, if appropriate, takes the device offline by
		 * unloading the tape
		 */
		if (st_check_density_or_wfm(dev, 1, 0, NO_STEPBACK)) {
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "stioctl : EIO : MTOFFL check density/wfm failed");
			return (EIO);
		}
		(void) st_cmd(dev, SCMD_REWIND, 0, SYNC_CMD);
		if (st_cmd(dev, SCMD_LOAD, 0, SYNC_CMD)) {
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "stioctl : EIO : MTOFFL");
			return (EIO);
		}
		un->un_eof = ST_NO_EOF;
		un->un_laststate = un->un_state;
		un->un_state = ST_STATE_OFFLINE;
		break;

	case MTLOAD:
		/*
		 * This is to load a tape into the drive
		 * Note that if the tape is not loaded, the device will have
		 * to be opened via O_NDELAY or O_NONBLOCK.
		 */
		/*
		 * Let's try and clean things up, if we are not
		 * initializing, and then send in the load command, no
		 * matter what.
		 *
		 * we try the load twice because some drives fail the first
		 * load after a media change by the user.
		 */

		if (un->un_state > ST_STATE_INITIALIZING)
			st_check_density_or_wfm(dev, 1, 0, NO_STEPBACK);

		if (st_cmd(dev, SCMD_LOAD, 1, SYNC_CMD) &&
		    st_cmd(dev, SCMD_LOAD, 1, SYNC_CMD)) {
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "stioctl : EIO : MTLOAD\n");
			rval = EIO;

			/*
			 * If load tape fails, who knows what happened...
			 */
			un->un_fileno = -1;
			rval = EIO;
			break;
		}

		/*
		 * reset all counters appropriatly using rewind, as if LOAD
		 * succeeds, we are at BOT
		 */
		un->un_state = ST_STATE_INITIALIZING;

		if (st_tape_init(dev)) {
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "stioctl : EIO : MTLOAD calls st_tape_init\n");
			rval = EIO;
			un->un_state = ST_STATE_OFFLINE;
		}

		break;

	case MTNOP:
		un->un_status = 0;		/* Reset status */
		un->un_err_resid = 0;
		break;

	case MTEOM:
		/*
		 * positions the tape at a location just after the last file
		 * written on the tape. For cartridge and 8 mm, this after
		 * the last file mark; for reel, this is inbetween the two
		 * last 2 file marks
		 */
		if (un->un_eof >= ST_EOT) {
			/*
			 * If the command wants to move to logical end
			 * of media, and we're already there, we're done.
			 * If we were at logical eot, we reset the state
			 * to be *not* at logical eot.
			 *
			 * If we're at physical or logical eot, we prohibit
			 * forward space operations (unconditionally).
			 */
			return (0);
		}

		if (st_check_density_or_wfm(dev, 1, B_READ, NO_STEPBACK)) {
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "stioctl : EIO : MTEOM check density/wfm failed");
			return (EIO);
		}

		/*
		 * st_find_eom() returns the last fileno we knew about;
		 */
		savefile = st_find_eom(dev);

		if ((un->un_status != KEY_BLANK_CHECK) &&
		    (un->un_status != SUN_KEY_EOT)) {
			un->un_fileno = -1;
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "stioctl : EIO : MTEOM status check failed");
			rval = EIO;
		} else {
			/*
			 * For 1/2" reel tapes assume logical EOT marked
			 * by two file marks or we don't care that we may
			 * be extending the last file on the tape.
			 */
			if (un->un_dp->options & ST_REEL) {
				if (st_cmd(dev, SCMD_SPACE, Fmk((-1)),
				    SYNC_CMD)) {
					un->un_fileno = -1;
					ST_DEBUG2(ST_DEVINFO, st_label,
					    SCSI_DEBUG,
					    "stioctl : EIO : MTEOM space "
					    "cmd failed");
					rval = EIO;
					break;
				}
				/*
				 * Fix up the block number.
				 */
				un->un_blkno = 0;
				un->un_err_blkno = 0;
			}
			un->un_err_resid = 0;
			un->un_fileno = savefile;
			un->un_eof = ST_EOT;
		}
		un->un_status = 0;
		break;

	case MTFSF:
		ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "fsf: count=%x, eof=%x\n", mtop->mt_count,
			un->un_eof);
		/*
		 * forward space over filemark
		 *
		 * For ASF we allow a count of 0 on fsf which means
		 * we just want to go to beginning of current file.
		 * Equivalent to "nbsf(0)" or "bsf(1) + fsf".
		 * Allow stepping over double fmk with reel
		 */
		if ((un->un_eof >= ST_EOT) && (mtop->mt_count > 0) &&
		    ((un->un_dp->options & ST_REEL) == 0)) {
			/* we're at EOM */
			un->un_err_resid = mtop->mt_count;
			un->un_status = KEY_BLANK_CHECK;
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "stioctl : EIO : MTFSF at EOM");
			return (EIO);
		}

		/*
		 * physical tape position may not be what we've been
		 * telling the user; adjust the request accordingly
		 */
		if (IN_EOF(un)) {
			un->un_fileno++;
			un->un_blkno = 0;
			/*
			 * For positive direction case, we're now covered.
			 * For zero or negative direction, we're covered
			 * (almost)
			 */
			mtop->mt_count--;
		}

		if (st_check_density_or_wfm(dev, 1, B_READ, STEPBACK)) {
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "stioctl : EIO : MTFSF density/wfm failed");
			return (EIO);
		}


		/*
		 * Forward space file marks.
		 * We leave ourselves at block zero
		 * of the target file number.
		 */
		if (mtop->mt_count < 0) {
			mtop->mt_count = -mtop->mt_count;
			mtop->mt_op = MTNBSF;
			goto bspace;
		}
fspace:
		ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "fspace: count=%x, eof=%x\n", mtop->mt_count,
			un->un_eof);
		if ((tmp = mtop->mt_count) == 0) {
			if (un->un_blkno == 0) {
				un->un_err_resid = 0;
				un->un_err_fileno = un->un_fileno;
				un->un_err_blkno = un->un_blkno;
				break;
			} else if (un->un_fileno == 0) {
				rval = st_cmd(dev, SCMD_REWIND, 0, SYNC_CMD);
			} else if (un->un_dp->options & ST_BSF) {
				rval = (st_cmd(dev, SCMD_SPACE, Fmk((-1)),
				    SYNC_CMD) ||
				    st_cmd(dev, SCMD_SPACE, Fmk(1), SYNC_CMD));
			} else {
				tmp = un->un_fileno;
				rval = (st_cmd(dev, SCMD_REWIND, 0, SYNC_CMD) ||
				    st_cmd(dev, SCMD_SPACE, Fmk(tmp),
				    SYNC_CMD));
			}
			if (rval) {
				un->un_fileno = -1;
				ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "stioctl : EIO : fspace fileno = -1");

				rval = EIO;
			}
		} else {
			rval = st_space_fmks(dev, tmp);
		}

		if (mtop->mt_op == MTBSF && rval != EIO) {
			/*
			 * we came here with a count < 0; we now need
			 * to skip back to end up before the filemark
			 */
			mtop->mt_count = 1;
			goto bspace;
		}
		break;


	case MTFSR:
		ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "fsr: count=%x, eof=%x\n", mtop->mt_count,
			un->un_eof);
		/*
		 * forward space to inter-record gap
		 *
		 */
		if ((un->un_eof >= ST_EOT) && (mtop->mt_count > 0)) {
			/* we're at EOM */
			un->un_err_resid = mtop->mt_count;
			un->un_status = KEY_BLANK_CHECK;
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "stioctl : EIO : MTFSR un_eof > ST_EOT");
			return (EIO);
		}

		if (mtop->mt_count == 0) {
			un->un_err_fileno = un->un_fileno;
			un->un_err_blkno = un->un_blkno;
			un->un_err_resid = 0;
			if (IN_EOF(un) && SVR4_BEHAVIOR) {
				un->un_status = SUN_KEY_EOF;
			}
			return (0);
		}

		/*
		 * physical tape position may not be what we've been
		 * telling the user; adjust the position accordingly
		 */
		if (IN_EOF(un)) {
			int blkno = un->un_blkno;
			int fileno = un->un_fileno;
			u_char lastop = un->un_lastop;
			if (st_cmd(dev, SCMD_SPACE, Fmk((-1)), SYNC_CMD)
			    == -1) {
				ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "stioctl : EIO : MTFSR count && IN_EOF");
				return (EIO);
			}

			un->un_blkno = blkno;
			un->un_fileno = fileno;
			un->un_lastop = lastop;
		}

		if (st_check_density_or_wfm(dev, 1, B_READ, STEPBACK)) {
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "stioctl : EIO : MTFSR st_check_den");
			return (EIO);
		}

space_records:
		ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "space_records: count=%x, eof=%x\n", mtop->mt_count,
			un->un_eof);
		tmp = un->un_blkno + mtop->mt_count;
		if (tmp == un->un_blkno) {
			un->un_err_resid = 0;
			un->un_err_fileno = un->un_fileno;
			un->un_err_blkno = un->un_blkno;
			break;
		} else if (un->un_blkno < tmp ||
		    (un->un_dp->options & ST_BSR)) {
			/*
			 * If we're spacing forward, or the device can
			 * backspace records, we can just use the SPACE
			 * command.
			 */
			tmp = tmp - un->un_blkno;
			if (st_cmd(dev, SCMD_SPACE, Blk(tmp), SYNC_CMD)) {
				ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "stioctl : EIO : space_records can't spc");
				rval = EIO;
			} else if (un->un_eof >= ST_EOF_PENDING) {
				/*
				 * check if we hit BOT/EOT
				 */
				if (tmp < 0 && un->un_eof == ST_EOM) {
					un->un_status = SUN_KEY_BOT;
					un->un_eof = ST_NO_EOF;
				} else if (tmp < 0 && un->un_eof ==
				    ST_EOF_PENDING) {
					int residue = un->un_err_resid;
					/*
					 * we skipped over a filemark
					 * and need to go forward again
					 */
					if (st_cmd(dev, SCMD_SPACE, Fmk(1),
					    SYNC_CMD)) {
						ST_DEBUG2(ST_DEVINFO,
						    st_label, SCSI_DEBUG,
						    "stioctl : EIO : "
						    "space_records can't "
						    "space #2");
						rval = EIO;
					}
					un->un_err_resid = residue;
				}
				if (rval == 0) {
					ST_DEBUG2(ST_DEVINFO, st_label,
					    SCSI_DEBUG,
					    "stioctl : EIO : space_rec rval"
					    " == 0");
					rval = EIO;
				}
			}
		} else {
			/*
			 * else we rewind, space forward across filemarks to
			 * the desired file, and then space records to the
			 * desired block.
			 */

			int t = un->un_fileno;	/* save current file */

			if (tmp < 0) {
				/*
				 * Wups - we're backing up over a filemark
				 */
				if (un->un_blkno != 0 &&
				    (st_cmd(dev, SCMD_REWIND, 0, SYNC_CMD) ||
				    st_cmd(dev, SCMD_SPACE, Fmk(t), SYNC_CMD)))
					un->un_fileno = -1;
				un->un_err_resid = -tmp;
				if (un->un_fileno == 0 && un->un_blkno == 0) {
					un->un_status = SUN_KEY_BOT;
					un->un_eof = ST_NO_EOF;
				} else if (un->un_fileno > 0) {
					un->un_status = SUN_KEY_EOF;
					un->un_eof = ST_NO_EOF;
				}
				un->un_err_fileno = un->un_fileno;
				un->un_err_blkno = un->un_blkno;
				ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "stioctl : EIO : space_records : tmp < 0");
				rval = EIO;
			} else if (st_cmd(dev, SCMD_REWIND, 0, SYNC_CMD) ||
				    st_cmd(dev, SCMD_SPACE, Fmk(t), SYNC_CMD) ||
				    st_cmd(dev, SCMD_SPACE, Blk(tmp),
					SYNC_CMD)) {
				ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "stioctl : EIO : space_records : rewind "
				    "and space failed");
				un->un_fileno = -1;
				rval = EIO;
			}
		}
		break;


	case MTBSF:
		ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "bsf: count=%x, eof=%x\n", mtop->mt_count,
			un->un_eof);
		/*
		 * backward space of file filemark (1/2" and 8mm)
		 * tape position will end on the beginning of tape side
		 * of the desired file mark
		 */
		if ((un->un_dp->options & ST_BSF) == 0) {
			return (ENOTTY);
		}

		/*
		 * If a negative count (which implies a forward space op)
		 * is specified, and we're at logical or physical eot,
		 * bounce the request.
		 */

		if (un->un_eof >= ST_EOT && mtop->mt_count < 0) {
			un->un_err_resid = mtop->mt_count;
			un->un_status = SUN_KEY_EOT;
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_ioctl : EIO : MTBSF : un_eof > ST_EOF");
			return (EIO);
		}
		/*
		 * physical tape position may not be what we've been
		 * telling the user; adjust the request accordingly
		 */
		if (IN_EOF(un)) {
			un->un_fileno++;
			un->un_blkno = 0;
			mtop->mt_count++;
			ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
			"bsf in eof: count=%d, op=%x\n",
			mtop->mt_count, mtop->mt_op);

		}

		if (st_check_density_or_wfm(dev, 1, 0, STEPBACK)) {
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_ioctl : EIO : MTBSF : check den wfm");
			return (EIO);
		}

		if (mtop->mt_count <= 0) {
			/*
			 * for a negative count, we need to step forward
			 * first and then step back again
			 */
			mtop->mt_count = -mtop->mt_count+1;
			goto fspace;
		}

bspace:
	{
		int skip_cnt, end_at_eof;

		ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "bspace: count=%x, eof=%x\n", mtop->mt_count,
			un->un_eof);
		/*
		 * Backspace files (MTNBSF):
		 *
		 *	For tapes that can backspace, backspace
		 *	count+1 filemarks and then run forward over
		 *	a filemark
		 *
		 *	For tapes that can't backspace,
		 *		calculate desired filenumber
		 *		(un->un_fileno - count), rewind,
		 *		and then space forward this amount
		 *
		 * Backspace filemarks (MTBSF)
		 *
		 *	For tapes that can backspace, backspace count
		 *	filemarks
		 *
		 *	For tapes that can't backspace, calculate
		 *	desired filenumber (un->un_fileno - count),
		 *	add 1, rewind, space forward this amount,
		 *	and mark state as ST_EOF_PENDING appropriately.
		 */

		if (mtop->mt_op == MTBSF) {
			end_at_eof = 1;
		} else {
			end_at_eof = 0;
		}

		ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "bspace: mt_op=%x, count=%x, fileno=%x, blkno=%x\n",
		    mtop->mt_op, mtop->mt_count, un->un_fileno, un->un_blkno);

		/*
		 * Handle the simple case of BOT
		 * playing a role in these cmds.
		 * We do this by calculating the
		 * ending file number. If the ending
		 * file is < BOT, rewind and set an
		 * error and mark resid appropriately.
		 * If we're backspacing a file (not a
		 * filemark) and the target file is
		 * the first file on the tape, just
		 * rewind.
		 */

		tmp = un->un_fileno - mtop->mt_count;
		if ((end_at_eof && tmp < 0) || (end_at_eof == 0 && tmp <= 0)) {
			if (st_cmd(dev, SCMD_REWIND, 0, SYNC_CMD)) {
				ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "st_ioctl : EIO : bspace : end_at_eof && "
				    "tmp < 0");
				rval = EIO;
			}
			if (tmp < 0) {
				ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "st_ioctl : EIO : bspace : tmp < 0");
				rval = EIO;
				un->un_err_resid = -tmp;
				un->un_status = SUN_KEY_BOT;
			}
			break;
		}

		if (un->un_dp->options & ST_BSF) {
			skip_cnt = 1 - end_at_eof;
			/*
			 * If we are going to end up at the beginning
			 * of the file, we have to space one extra file
			 * first, and then space forward later.
			 */
			tmp = -(mtop->mt_count + skip_cnt);
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "skip_cnt=%x, tmp=%x\n", skip_cnt, tmp);
			if (st_cmd(dev, SCMD_SPACE, Fmk(tmp), SYNC_CMD)) {
				ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "st_ioctl : EIO : bspace : can't space "
				    "tmp");
				rval = EIO;
			}
		} else {
			if (st_cmd(dev, SCMD_REWIND, 0, SYNC_CMD)) {
				rval = EIO;
			} else {
				skip_cnt = tmp + end_at_eof;
			}
		}

		/*
		 * If we have to space forward, do so...
		 */
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "space forward skip_cnt=%x, rval=%x\n", skip_cnt, rval);
		if (rval == 0 && skip_cnt) {
			if (st_cmd(dev, SCMD_SPACE, Fmk(skip_cnt), SYNC_CMD)) {
				ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "st_ioctl : EIO : bspace : can't space "
				    "skip_cnt");
				rval = EIO;
			} else if (end_at_eof) {
				/*
				 * If we had to space forward, and we're
				 * not a tape that can backspace, mark state
				 * as if we'd just seen a filemark during a
				 * a read.
				 */
				if ((un->un_dp->options & ST_BSF) == 0) {
					un->un_eof = ST_EOF_PENDING;
					un->un_fileno -= 1;
					un->un_blkno = INF;
				}
			}
		}

		if (rval)
			un->un_fileno = -1;
		break;
	}

	case MTNBSF:
		ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "nbsf: count=%x, eof=%x\n", mtop->mt_count,
			un->un_eof);
		/*
		 * backward space file to beginning of file
		 *
		 * If a negative count (which implies a forward space op)
		 * is specified, and we're at logical or physical eot,
		 * bounce the request.
		 */

		if (un->un_eof >= ST_EOT && mtop->mt_count < 0) {
			un->un_err_resid = mtop->mt_count;
			un->un_status = SUN_KEY_EOT;
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_ioctl : EIO : > EOT and count < 0");
			return (EIO);
		}
		/*
		 * physical tape position may not be what we've been
		 * telling the user; adjust the request accordingly
		 */
		if (IN_EOF(un)) {
			un->un_fileno++;
			un->un_blkno = 0;
			mtop->mt_count++;
		}

		if (st_check_density_or_wfm(dev, 1, 0, STEPBACK)) {
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_ioctl : EIO : MTNBSF check den and wfm");
			return (EIO);
		}

mtnbsf:
		ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "mtnbsf: count=%x, eof=%x\n", mtop->mt_count,
			un->un_eof);
		if (mtop->mt_count <= 0) {
			mtop->mt_op = MTFSF;
			mtop->mt_count = -mtop->mt_count;
			goto fspace;
		}
		goto bspace;

	case MTBSR:
		ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "bsr: count=%x, eof=%x\n", mtop->mt_count,
			un->un_eof);
		/*
		 * backward space into inter-record gap
		 *
		 * If a negative count (which implies a forward space op)
		 * is specified, and we're at logical or physical eot,
		 * bounce the request.
		 */
		if (un->un_eof >= ST_EOT && mtop->mt_count < 0) {
			un->un_err_resid = mtop->mt_count;
			un->un_status = SUN_KEY_EOT;
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_ioctl : EIO : MTBSR > EOT");
			return (EIO);
		}

		if (mtop->mt_count == 0) {
			un->un_err_fileno = un->un_fileno;
			un->un_err_blkno = un->un_blkno;
			un->un_err_resid = 0;
			if (IN_EOF(un) && SVR4_BEHAVIOR) {
				un->un_status = SUN_KEY_EOF;
			}
			return (0);
		}

		/*
		 * physical tape position may not be what we've been
		 * telling the user; adjust the position accordingly.
		 * bsr can not skip filemarks and continue to skip records
		 * therefore if we are logically before the filemark but
		 * physically at the EOT side of the filemark, we need to step
		 * back; this allows fsr N where N > number of blocks in file
		 * followed by bsr 1 to position at the beginning of last block
		 */
		if (IN_EOF(un)) {
			int blkno = un->un_blkno;
			int fileno = un->un_fileno;
			u_char lastop = un->un_lastop;
			if (st_cmd(dev, SCMD_SPACE, Fmk((-1)), SYNC_CMD)
			    == -1) {
				ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "st_write_fm : EIO : MTBSR can't space");
				return (EIO);
			}

			un->un_blkno = blkno;
			un->un_fileno = fileno;
			un->un_lastop = lastop;
		}

		un->un_eof = ST_NO_EOF;

		if (st_check_density_or_wfm(dev, 1, 0, STEPBACK)) {
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_ioctl : EIO : MTBSR : can't set density or "
			    "wfm");
			return (EIO);
		}

		mtop->mt_count = -mtop->mt_count;
		goto space_records;

	case MTSRSZ:
	{
		/*
		 * Set record-size to that sent by user
		 * Update un->un_bsize and issue a mode select
		 */
		int old_bsize = un->un_bsize;

		if (mtop->mt_count == 0 && (un->un_dp->options & ST_VARIABLE)) {
			un->un_bsize = 0;
		} else if (mtop->mt_count >= un->un_minbsize &&
		    (mtop->mt_count <= un->un_maxbsize ||
		    un->un_maxbsize == 0) &&
		    mtop->mt_count <= un->un_maxdma) {
			un->un_bsize = mtop->mt_count;
		} else {
			return (EINVAL);
		}

		if (st_modeselect(dev)) {
			un->un_bsize = old_bsize;
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_ioctl : MTSRSZ : EIO : cant set block size");
			return (EIO);
		}

		return (0);
		/*NOTREACHED*/
		break;
	}

	case MTGRSZ:
		/*
		 * Get record-size to the user
		 */
		mtop->mt_count = un->un_bsize;

		if (ddi_copyout((caddr_t)mtop, (caddr_t)arg,
			sizeof (struct mtop), flag)) {
			return (EFAULT);
		} else {
			return (0);
		}
		/*NOTREACHED*/
		break;

	default:
		rval = ENOTTY;
	}

	ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_ioctl: fileno=%x, blkno=%x, un_eof=%x\n", un->un_fileno,
	    un->un_blkno, un->un_eof);

	if (un->un_fileno < 0) {
		un->un_density_known = 0;
	}

	ASSERT(mutex_owned(ST_MUTEX));
	return (rval);
}


/*
 * Run a command for uscsi ioctl.
 * cdbspace is address space of cdb.
 * dataspace is address space of the uscsi data buffer.
 */
static int
st_ioctl_cmd(dev_t dev, struct uscsi_cmd *ucmd,
	enum uio_seg cdbspace, enum uio_seg dataspace,
	enum uio_seg rqbufspace)
{
	struct buf *bp;
	struct uscsi_cmd *kcmd;
	caddr_t kcdb;
	int flag;
	int err;
	int rqlen;
	int offline_state = 0;
	char *krqbuf = NULL;

	GET_SOFT_STATE(dev);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_ioctl_cmd(dev = 0x%x)\n", dev);

	ASSERT(mutex_owned(ST_MUTEX));

	/*
	 * We really don't know what commands are coming in here and
	 * we don't want to limit the commands coming in.
	 *
	 * If st_init_tape() gets called from st_strategy(), then we
	 * will hang the process waiting for un->un_sbuf_busy to be cleared,
	 * which it never will, as we set it below.  To prevent
	 * st_init_tape() from getting called, we have to set state to other
	 * than ST_STATE_OFFLINE, so we choose ST_STATE_INITIALIZING, which
	 * achieves this purpose already
	 *
	 * We use offline_state to preserve the OFFLINE state, if it exists,
	 * so other entry points to the driver might have the chance to call
	 * st_tape_init().
	 *
	 */
	if (un->un_state == ST_STATE_OFFLINE) {
		un->un_laststate = ST_STATE_OFFLINE;
		un->un_state = ST_STATE_INITIALIZING;
		offline_state = 1;
	}


	/*
	 * Is this a request to reset the bus?
	 * If so, we need go no further.
	 */
	if (ucmd->uscsi_flags & (USCSI_RESET|USCSI_RESET_ALL)) {
		flag = ((ucmd->uscsi_flags & USCSI_RESET_ALL)) ?
			RESET_ALL : RESET_TARGET;

		mutex_exit(ST_MUTEX);
		err = (scsi_reset(ROUTE, flag)) ? 0 : EIO;
		mutex_enter(ST_MUTEX);

		ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
			"reset %s %s\n",
			(flag == RESET_ALL) ? "all" : "target",
			(err == 0) ? "ok" : "failed");
		/*
		 * If scsi reset successful, don't write any filemarks.
		 */
		ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "st_ioctl_cmd : EIO : scsi_reset failed");
		if (err)
			un->un_fmneeded = 0;
		goto exit;
	}

	/*
	 * In order to not worry about where the uscsi structure
	 * or cdb it points to came from, we kmem_alloc copies
	 * of them here.  This will allow reference to the data
	 * they contain long after this process has gone to
	 * sleep and its kernel stack has been unmapped, etc.
	 */

	kcdb = kmem_alloc((size_t)ucmd->uscsi_cdblen, KM_SLEEP);
	if (cdbspace == UIO_SYSSPACE) {
		bcopy(ucmd->uscsi_cdb, kcdb, ucmd->uscsi_cdblen);
	} else {
		if (ddi_copyin(ucmd->uscsi_cdb, kcdb,
				(size_t)ucmd->uscsi_cdblen, 0)) {
			kmem_free(kcdb, (size_t)ucmd->uscsi_cdblen);
			err = EFAULT;
			goto exit;
		}
	}

	kcmd = kmem_alloc(sizeof (struct uscsi_cmd), KM_SLEEP);
	bcopy((caddr_t)ucmd, (caddr_t)kcmd, sizeof (struct uscsi_cmd));
	kcmd->uscsi_cdb = kcdb;

	flag = (kcmd->uscsi_flags & USCSI_READ) ? B_READ : B_WRITE;

#ifdef STDEBUG
	if (st_debug > 6) {
		st_clean_print(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "uscsi cdb", kcdb, kcmd->uscsi_cdblen);
		if (kcmd->uscsi_buflen) {
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			"uscsi %s of %d bytes %s %s space\n",
			(flag == B_READ) ? "read" : "write",
			kcmd->uscsi_buflen,
			(flag == B_READ) ? "to" : "from",
			(dataspace == UIO_SYSSPACE) ? "system" : "user");
		}
	}
#endif ST_DEBUG

	/*
	 * Initialize Request Sense buffering, if requested.
	 * For user processes, allocate a kernel copy of the sense buffer
	 */
	if ((kcmd->uscsi_flags & USCSI_RQENABLE) &&
			kcmd->uscsi_rqlen && kcmd->uscsi_rqbuf) {
		if (rqbufspace == UIO_USERSPACE) {
			krqbuf = kmem_alloc(SENSE_LENGTH, KM_SLEEP);
		}
		kcmd->uscsi_rqlen = SENSE_LENGTH;
		kcmd->uscsi_rqresid = SENSE_LENGTH;
	} else {
		kcmd->uscsi_rqlen = 0;
		kcmd->uscsi_rqresid = 0;
	}
	un->un_srqbufp = krqbuf;

	/*
	 * Get buffer resources...
	 */
	while (un->un_sbuf_busy)
		cv_wait(&un->un_sbuf_cv, ST_MUTEX);
	un->un_sbuf_busy = 1;

	bp = un->un_sbufp;

	/*
	 * Force asynchronous mode, if necessary.
	 */
	if (ucmd->uscsi_flags & USCSI_ASYNC) {
		if (scsi_ifgetcap(ROUTE, "synchronous", 1) == 1) {
			if (scsi_ifsetcap(ROUTE, "synchronous", 0, 1) == 1) {
				ST_DEBUG(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "forced async ok\n");
			} else {
				ST_DEBUG(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "forced async failed\n");
				err = EINVAL;
				goto done;
			}
		}
	}

	/*
	 * Re-enable synchronous mode, if requested
	 */
	if (ucmd->uscsi_flags & USCSI_SYNC) {
		if (scsi_ifgetcap(ROUTE, "synchronous", 1) == 0) {
			int i = scsi_ifsetcap(ROUTE, "synchronous", 1, 1);
			ST_DEBUG(ST_DEVINFO, st_label, SCSI_DEBUG,
				"re-enabled sync %s\n",
				(i == 1) ? "ok" : "failed");
		}
	}

	if (kcmd->uscsi_buflen) {
		/*
		 * We're going to do actual I/O.
		 * Set things up for physio.
		 */
		struct iovec aiov;
		struct uio auio;
		struct uio *uio = &auio;

		bzero((caddr_t)&auio, sizeof (struct uio));
		bzero((caddr_t)&aiov, sizeof (struct iovec));
		aiov.iov_base = kcmd->uscsi_bufaddr;
		aiov.iov_len = kcmd->uscsi_buflen;

		uio->uio_iov = &aiov;
		uio->uio_iovcnt = 1;
		uio->uio_resid = aiov.iov_len;
		uio->uio_segflg = dataspace;
		uio->uio_loffset = 0;
		uio->uio_fmode = 0;

		/*
		 * Let physio do the rest...
		 */
		bp->b_forw = (struct buf *)kcdb[0];
		bp->b_back = (struct buf *)kcmd;

		mutex_exit(ST_MUTEX);
		err = physio(st_strategy, bp, dev, flag, st_minphys, uio);
		mutex_enter(ST_MUTEX);
	} else {
		/*
		 * Mimic physio
		 */
		bp->b_forw = (struct buf *)kcdb[0];
		bp->b_back = (struct buf *)kcmd;
		bp->b_flags = B_BUSY | flag;
		bp->b_edev = dev;
		bp->b_dev = cmpdev(dev);
		bp->b_bcount = 0;
		bp->b_blkno = 0;
		bp->b_resid = 0;

		mutex_exit(ST_MUTEX);
		(void) st_strategy(bp);
		err = biowait(bp);
		mutex_enter(ST_MUTEX);

		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "st_ioctl_cmd: biowait returns %d\n", err);
	}

	/*
	 * Copy status from kernel copy of uscsi_cmd to user copy
	 * of uscsi_cmd - this was saved in st_done_and_mutex_exit()
	 */
	ucmd->uscsi_status = kcmd->uscsi_status;

done:
	ucmd->uscsi_resid = bp->b_resid;

	/*
	 * Update the Request Sense status and resid
	 */
	rqlen = kcmd->uscsi_rqlen - kcmd->uscsi_rqresid;
	rqlen = min(((int)ucmd->uscsi_rqlen), rqlen);
	ucmd->uscsi_rqresid = ucmd->uscsi_rqlen - rqlen;
	ucmd->uscsi_rqstatus = kcmd->uscsi_rqstatus;
	/*
	 * Copy out the sense data for user processes
	 */
	if (ucmd->uscsi_rqbuf && rqlen && rqbufspace == UIO_USERSPACE) {
		if (copyout(krqbuf, ucmd->uscsi_rqbuf, rqlen)) {
			err = EFAULT;
		}
	}

	ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_ioctl_cmd status is 0x%x, resid is 0x%x\n",
	    ucmd->uscsi_status, ucmd->uscsi_resid);
	if (DEBUGGING && (rqlen != 0)) {
		int i, n, len;
		char *data = krqbuf;
		scsi_log(ST_DEVINFO, st_label, SCSI_DEBUG,
			"rqstatus=0x%x	rqlen=0x%x  rqresid=0x%x\n",
			ucmd->uscsi_rqstatus, ucmd->uscsi_rqlen,
			ucmd->uscsi_rqresid);
		len = (int)ucmd->uscsi_rqlen - ucmd->uscsi_rqresid;
		for (i = 0; i < len; i += 16) {
			n = min(16, len-1);
			st_clean_print(ST_DEVINFO, st_label, CE_NOTE,
				"  ", &data[i], n);
		}
	}

	/*
	 * Free resources
	 */
	un->un_sbuf_busy = 0;
	un->un_srqbufp = NULL;
	cv_signal(&un->un_sbuf_cv);

	if (krqbuf) {
		kmem_free(krqbuf, SENSE_LENGTH);
	}
	kmem_free(kcdb, kcmd->uscsi_cdblen);
	kmem_free(kcmd, sizeof (struct uscsi_cmd));

	ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_ioctl_cmd returns 0x%x\n", err);


exit:
	/* don't lose offline state */
	if (offline_state)
		un->un_state = ST_STATE_OFFLINE;

	ASSERT(mutex_owned(ST_MUTEX));
	return (err);
}

static int
st_write_fm(dev_t dev, int wfm)
{
	int i;

	GET_SOFT_STATE(dev);

	ASSERT(mutex_owned(ST_MUTEX));

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_write_fm(dev = 0x%x, wfm = %d)\n", dev, wfm);

	/*
	 * write one filemark at the time after EOT
	 */
	if (un->un_eof >= ST_EOT) {
		for (i = 0; i < wfm; i++) {
			if (st_cmd(dev, SCMD_WRITE_FILE_MARK, 1, SYNC_CMD)) {
				ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
				    "st_write_fm : EIO : write EOT file mark");
				return (EIO);
			}
		}
	} else if (st_cmd(dev, SCMD_WRITE_FILE_MARK, wfm, SYNC_CMD)) {
		ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "st_write_fm : EIO : write file mark");
		return (EIO);
	}

	ASSERT(mutex_owned(ST_MUTEX));
	return (0);
}


/*
 * Command start && done functions
 */
static void
st_start(struct scsi_tape *un, int flag)
{
	register struct buf *bp;
	struct buf *save_bp = 0;
	struct buf *lastbuf_on_waitq = 0;
	int status;

retry:
	ASSERT(mutex_owned(ST_MUTEX));

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_start(): dev = 0x%x, flag = %d\n", un->un_dev, flag);

	if ((bp = un->un_quef) == NULL) {
		return;
	}

	ASSERT((bp->b_flags & B_DONE) == 0);

	/*
	 * make a copy of this	bp iff
	 * - buffered writes are enabled
	 * - if enabled in config entry
	 * - if we are in user context (otherwise copyin fails)
	 * - we are writing and not passed LEOT
	 * - status = 0
	 * - auto request sense is enabled
	 * - it is not a special buf
	 * - a pkt has not been assigned yet
	 * - if last operation was a write, ie. always try a normal
	 *   write first before starting with buffering
	 */
	if (un->un_allow_buffered_writes &&
	    (un->un_state != ST_STATE_RESOURCE_WAIT) &&
	    (flag == ST_USER_CONTEXT) &&
	    (un->un_eof == ST_NO_EOF) &&
	    (un->un_throttle) &&
	    (un->un_status == 0) &&
	    (bp->b_flags & B_WRITE) &&
	    (un->un_lastop == ST_OP_WRITE) &&
	    (bp != un->un_sbufp) &&
	    (!BP_PKT(bp))) {
		register struct buf *new_bp;
		register caddr_t buf;

		if (bp->b_bcount > un->un_max_buf_size) {
			goto alloc_pkt;
		}

		/*
		 * This should only happen in user context, as is
		 * gurantee'ed by the if
		 */
		while (un->un_ncmds >= un->un_throttle) {
			cv_wait(&un->un_write_queue_cv, ST_MUTEX);
		}

		/*
		 * is there still work to do?
		 * (another thread may have empties un_quef)
		 */
		if ((bp = un->un_quef) == NULL) {
			return;
		}

		/*
		 * allocate a new bp and a buffer
		 */
		new_bp = getrbuf(KM_NOSLEEP);
		if (new_bp == NULL) {
			goto alloc_pkt;
		}
		buf = (caddr_t)kmem_alloc(bp->b_bcount, KM_NOSLEEP);
		if (buf == NULL) {
			freerbuf(new_bp);
			goto alloc_pkt;
		}
		*new_bp = *bp;

		if (ddi_copyin(bp->b_un.b_addr, buf, bp->b_bcount, 0) == -1) {
			freerbuf(new_bp);
			kmem_free(buf, bp->b_bcount);
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "ddi_copyin failed\n");
			goto alloc_pkt;
		}

		new_bp->b_un.b_addr = buf;
		new_bp->b_flags = B_BUSY | B_WRITE;
		new_bp->b_proc = NULL;
		save_bp = bp;
		bp = new_bp;
		un->un_ncmds++;
		if (un->un_wf) {
			lastbuf_on_waitq = un->un_wl;
			un->un_wl->b_forw = bp;
		} else {
			un->un_wf = bp;
		}
		un->un_wl = bp;
		bp->b_forw = 0;

		ASSERT((bp->b_flags & B_DONE) == 0);

		ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "created duplicate bp %x of %x, count=%x ncmds=%d\n",
		    new_bp, bp, bp->b_bcount, un->un_ncmds);

	} else {
		if (un->un_runqf) {
			return;
		}
	}

alloc_pkt:
	if (!BP_PKT(bp)) {
		ASSERT((bp->b_flags & B_DONE) == 0);
		st_make_cmd(un, bp, st_runout);
		ASSERT((bp->b_flags & B_DONE) == 0);
		if (!BP_PKT(bp) && !(bp->b_flags & B_ERROR)) {
			un->un_state = ST_STATE_RESOURCE_WAIT;
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "no resources for pkt\n");
			if (save_bp) {
				/*
				 * remove the copy and free bp and data buf
				 */
				un->un_ncmds--;
				cv_signal(&un->un_write_queue_cv);
				if (un->un_wf == bp) {
					un->un_wf = un->un_wl = NULL;
				} else {
					lastbuf_on_waitq->b_forw = 0;
					un->un_wl = lastbuf_on_waitq;
				}
				kmem_free(bp->b_un.b_addr, bp->b_bcount);
				SET_BP_PKT(bp, NULL);
				freerbuf(bp);
			}
			return;
		} else if (bp->b_flags & B_ERROR) {
			scsi_log(ST_DEVINFO, st_label, CE_WARN,
			    "errors after pkt alloc "
			    "(b_flags=0x%x, b_error=0x%x)\n",
			    bp->b_flags, geterror(bp));
			ASSERT((bp->b_flags & B_DONE) == 0);
			if (save_bp) {
				ASSERT((save_bp->b_flags & B_DONE) == 0);
				un->un_quef = save_bp->b_actf;
				if (un->un_quel == save_bp) {
					un->un_quel = 0;
				}
				save_bp->b_actf = 0;

				/* delete from waitq */
				un->un_ncmds--;
				cv_signal(&un->un_write_queue_cv);
				if (un->un_wf == bp) {
					un->un_wf = un->un_wl = NULL;
				} else {
					lastbuf_on_waitq->b_forw = 0;
					un->un_wl = lastbuf_on_waitq;
				}
				save_bp->b_flags = bp->b_flags;
				kmem_free(bp->b_un.b_addr, bp->b_bcount);
				SET_BP_PKT(bp, NULL);
				freerbuf(bp);
				save_bp->b_resid = save_bp->b_bcount;
				ASSERT((save_bp->b_flags & B_DONE) == 0);
				mutex_exit(ST_MUTEX);
				biodone(save_bp);
			} else {
				un->un_quef = bp->b_actf;
				if (un->un_quel == bp) {
					un->un_quel = 0;
				}
				bp->b_actf = 0;

				ASSERT((bp->b_flags & B_DONE) == 0);
				bp->b_resid = bp->b_bcount;
				mutex_exit(ST_MUTEX);
				biodone(bp);
			}
			mutex_enter(ST_MUTEX);
			goto retry;
		}

		if (un->un_state != ST_STATE_INITIALIZING)
		    un->un_state = ST_STATE_OPEN;
	}

	/*
	 * move from waitq to runq
	 */
	if (save_bp) {
		un->un_quef = save_bp->b_actf;
		if (un->un_quel == save_bp) {
			un->un_quel = 0;
		}
		save_bp->b_actf = 0;
	} else {
		un->un_quef = bp->b_actf;
		if (un->un_quel == bp) {
			un->un_quel = 0;
		}
	}
	bp->b_actf = 0;

	if (un->un_runqf) {
		un->un_runql->b_actf = bp;
	} else {
		un->un_runqf = bp;
	}
	un->un_runql = bp;

#ifdef STDEBUG
	ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_start: cmd=0x%x count=%d resid=%d flags=0x%x pkt=0x%x\n",
		(int)bp->b_forw, bp->b_bcount,
		bp->b_resid, bp->b_flags, BP_PKT(bp));


	{
		struct scsi_pkt *pkt = BP_PKT(bp);
		u_char *cdbp = (u_char *)pkt->pkt_cdbp;

		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "st_start: cdb %x %x %x %x %x %x, fileno=%d, blk=%d\n",
			*cdbp++, *cdbp++, *cdbp++,
			*cdbp++, *cdbp++, *cdbp, un->un_fileno,
			un->un_blkno);
	}
#endif
	un->un_throttle = 0;	/* nothing else will come in here */

	mutex_exit(ST_MUTEX);

	if ((status = scsi_transport(BP_PKT(bp))) != TRAN_ACCEPT) {
		if (status == TRAN_BUSY) {
			/* if too many retries, fail the transport */
			if (st_handle_start_busy(un, bp,
			    ST_TRAN_BUSY_TIMEOUT) == 0)
				goto done;
		}

		scsi_log(ST_DEVINFO, st_label, CE_WARN,
		    "transport rejected\n");
		bp->b_resid = bp->b_bcount;
		bioerror(bp, EIO);
#ifndef __lock_lint
		/*
		 * warlock doesn't understand this potential
		 * recursion?
		 */
		mutex_enter(ST_MUTEX);
		st_done_and_mutex_exit(un, bp);
#endif
	} else {
		mutex_enter(ST_MUTEX);
		un->un_tran_retry_ct = 0;
		un->un_throttle = un->un_last_throttle;
		mutex_exit(ST_MUTEX);
	}

done:
	/*
	 * if we have made a copy of the bp, we can do a biodone now
	 */
	if (save_bp) {
		biodone(save_bp);
	}
	mutex_enter(ST_MUTEX);
}

/*
 * if the transport is busy, then put this bp back on the waitq
 */
static int
st_handle_start_busy(struct scsi_tape *un, struct buf *bp,
    long timeout_interval)
{
	struct buf *last_quef, *runq_bp;
	int rval = 0;

	mutex_enter(ST_MUTEX);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_handle_start_busy()\n");

	/*
	 * Check to see if we hit the retry timeout and one last check for
	 * making sure this is the last on the runq, if it is not, we have
	 * to fail
	 */
	if (((int)un->un_tran_retry_ct++ > st_retry_count) ||
	    (un->un_runql != bp)) {
		rval = -1;
		goto exit;
	}

	/* put the bp back on the waitq */
	if (un->un_quef) {
		last_quef = un->un_quef;
		un->un_quef = bp;
		bp->b_actf = last_quef;
	} else  {
		bp->b_actf = 0;
		un->un_quef = bp;
		un->un_quel = bp;
	}

	/*
	 * since this is an error case, we won't have to do
	 * this list walking much.  We've already made sure this bp was the
	 * last on the runq
	 */
	runq_bp = un->un_runqf;

	if (un->un_runqf == bp) {
		un->un_runqf = 0;
		un->un_runql = 0;
	} else {
		while (runq_bp) {
			if (runq_bp->b_actf == bp) {
				runq_bp->b_actf = 0;
				un->un_runql = runq_bp;
				break;
			}
			runq_bp = runq_bp->b_actf;
		}
	}


	/*
	 * all queues are aligned, we are just waiting to
	 * transport, don't alloc any more buf p's, when
	 * st_start is reentered.
	 */
	(void) timeout(st_start_restart, (caddr_t)un, timeout_interval);

exit:
	mutex_exit(ST_MUTEX);
	return (rval);
}



/*
 * st_runout a callback that is called what a resource allocatation failed
 */
static int
st_runout(caddr_t arg)
{
	register int serviced;
	register int instance;
	register struct scsi_tape *un;

	serviced = 1;

	un = (struct scsi_tape *)arg;
	ASSERT(un != NULL);

	mutex_enter(ST_MUTEX);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_runout()\n");

	instance = st_last_scanned_instance;
	st_last_scanned_instance = 0;

	if (un->un_state == ST_STATE_RESOURCE_WAIT) {
		st_start(un, 0);
		if (un->un_state ==  ST_STATE_RESOURCE_WAIT) {
			serviced = 0;
			if (!st_last_scanned_instance) {
				st_last_scanned_instance = instance;
			}
		}
	}
	mutex_exit(ST_MUTEX);

	return (serviced);
}

/*
 * st_done_and_mutex_exit()
 *	- remove bp from runq
 *	- start up the next request
 *	- if this was a copied bp, clean up
 *	- if this was an asynch bp, clean up
 *	- exit with released mutex
 */
static void
st_done_and_mutex_exit(struct scsi_tape *un, struct buf *bp)
{
	register struct buf *runqbp, *prevbp;
	int was_buffered_write = 0;

	/* LOCK HELD ON ENTRY: un->un_sd->sd_mutex */
	/* LOCK RELEASED AS SIDE EFFECT: un->un_sd->sd_mutex */
	ASSERT(mutex_owned(ST_MUTEX));

	/*
	 * if bp is still on the runq (anywhere), then remove it
	 */
	prevbp = NULL;
	for (runqbp = un->un_runqf; runqbp != 0; runqbp = runqbp->b_actf) {
		if (runqbp == bp) {
			if (runqbp == un->un_runqf) {
				un->un_runqf = bp->b_actf;
			} else {
				prevbp->b_actf = bp->b_actf;
			}
			if (un->un_runql == bp) {
				un->un_runql = prevbp;
			}
		}
		prevbp = runqbp;
	}
	bp->b_actf = 0;

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	"st_done_and_mutex_exit(): cmd=0x%x count=%d resid=%d  flags=0x%x\n",
		(int)*((caddr_t)(BP_PKT(bp))->pkt_cdbp),
		bp->b_bcount, bp->b_resid, bp->b_flags);

	if (un->un_wf) {
		if (bp == un->un_wf) {
			un->un_wf = bp->b_forw;
			if (un->un_wl == bp) {
				un->un_wl = NULL;
			}
			un->un_ncmds--;
			cv_signal(&un->un_write_queue_cv);
			was_buffered_write = 1;
		} else {
			/*
			 * XXX just checking for now
			 */
			struct buf *b = un->un_wf->b_forw;
			while (b) {
				ASSERT(bp != b);
				b = b->b_forw;
			}
		}
	}


	/*
	 * Start the next one before releasing resources on this one
	 */
	if (un->un_quef) {
		st_start(un, 0);
	}

	if (was_buffered_write) {
		ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "was_buffered_write: copied buf %x done, ncmds=%d\n",
		    bp, un->un_ncmds);

		if (bp->b_flags & B_ERROR) {
			scsi_log(ST_DEVINFO, st_label, CE_WARN,
			    "buffered request failed\n");
			un->un_fileno = -1; /* causes the next write to fail */
		}
		if (bp->b_resid) {
			scsi_log(ST_DEVINFO, st_label, CE_WARN,
			    "buffered request had a resid (0x%x)\n",
			    bp->b_resid);
			un->un_fileno = -1;
		}
		mutex_exit(ST_MUTEX);
		scsi_destroy_pkt(BP_PKT(bp));
		kmem_free(bp->b_un.b_addr, bp->b_bcount);
		SET_BP_PKT(bp, NULL);
		freerbuf(bp);
		return;

	} else if (bp == un->un_sbufp && (bp->b_flags & B_ASYNC)) {
		/*
		 * Since we marked this ourselves as ASYNC,
		 * there isn't anybody around waiting for
		 * completion any more.
		 */
		int com = (int)bp->b_forw;
		if (com == SCMD_READ || com == SCMD_WRITE) {
			bp->b_un.b_addr = (caddr_t)0;
		}
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "st_done_and_mutex_exit(async): freeing pkt\n");
		scsi_destroy_pkt(BP_PKT(bp));
		un->un_sbuf_busy = 0;
		cv_signal(&un->un_sbuf_cv);
		mutex_exit(ST_MUTEX);
		return;
	}

	if (bp == un->un_sbufp && BP_UCMD(bp)) {
		/*
		 * Copy status from scsi_pkt to uscsi_cmd
		 * since st_ioctl_cmd needs it
		 */
		BP_UCMD(bp)->uscsi_status = SCBP_C(BP_PKT(bp));
	}

	mutex_exit(ST_MUTEX);
	ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_done_and_mutex_exit: freeing pkt\n");

	scsi_destroy_pkt(BP_PKT(bp));

	biodone(bp);
}

/*
 * Utility functions
 */
static int
st_determine_generic(dev_t dev)
{
	int bsize;
	static char *cart = "0.25 inch cartridge";
	char *sizestr;

	GET_SOFT_STATE(dev);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_determine_generic(dev = 0x%x)\n", dev);

	ASSERT(mutex_owned(ST_MUTEX));

	if (st_cmd(dev, SCMD_MODE_SENSE, MSIZE, SYNC_CMD))
		return (-1);

	bsize = (un->un_mspl->high_bl << 16)	|
		(un->un_mspl->mid_bl << 8)	|
		(un->un_mspl->low_bl);

	if (bsize == 0) {
		un->un_dp->options |= ST_VARIABLE;
		un->un_dp->bsize = 0;
		un->un_bsize = 0;
	} else if (bsize > ST_MAXRECSIZE_FIXED) {
		/*
		 * record size of this device too big.
		 * try and convert it to variable record length.
		 *
		 */
		un->un_dp->options |= ST_VARIABLE;
		un->un_bsize = 0;
		if (st_modeselect(dev)) {
			ST_DEBUG6(ST_DEVINFO, st_label, CE_WARN,
			    "Fixed Record Size %d is too large\n",
			    bsize);
			ST_DEBUG6(ST_DEVINFO, st_label, CE_WARN,
			    "Cannot switch to variable record size\n");
			un->un_dp->options &= ~ST_VARIABLE;
			return (-1);
		}
		un->un_mspl->high_bl = un->un_mspl->mid_bl =
		    un->un_mspl->low_bl = 0;
	} else {
		un->un_dp->bsize = bsize;
		un->un_bsize = bsize;
	}


	switch (un->un_mspl->density) {
	default:
	case 0x0:
		/*
		 * default density, cannot determine any other
		 * information.
		 */
		sizestr = "Unknown type- assuming 0.25 inch cartridge";
		un->un_dp->type = ST_TYPE_DEFAULT;
		un->un_dp->options |= (ST_AUTODEN_OVERRIDE|ST_QIC);
		break;
	case 0x1:
	case 0x2:
	case 0x3:
	case 0x6:
		/*
		 * 1/2" reel
		 */
		sizestr = "0.50 inch reel";
		un->un_dp->type = ST_TYPE_REEL;
		un->un_dp->options |= ST_REEL;
		un->un_dp->densities[0] = 0x1;
		un->un_dp->densities[1] = 0x2;
		un->un_dp->densities[2] = 0x6;
		un->un_dp->densities[3] = 0x3;
		break;
	case 0x4:
	case 0x5:
	case 0x7:
	case 0x0b:

		/*
		 * Quarter inch.
		 */
		sizestr = cart;
		un->un_dp->type = ST_TYPE_DEFAULT;
		un->un_dp->options |= ST_QIC;

		un->un_dp->densities[1] = 0x4;
		un->un_dp->densities[2] = 0x5;
		un->un_dp->densities[3] = 0x7;
		un->un_dp->densities[0] = 0x0b;
		break;

	case 0x0f:
	case 0x10:
	case 0x11:
	case 0x12:
		/*
		 * QIC-120, QIC-150, QIC-320, QIC-600
		 */
		sizestr = cart;
		un->un_dp->type = ST_TYPE_DEFAULT;
		un->un_dp->options |= ST_QIC;
		un->un_dp->densities[0] = 0x0f;
		un->un_dp->densities[1] = 0x10;
		un->un_dp->densities[2] = 0x11;
		un->un_dp->densities[3] = 0x12;
		break;

	case 0x09:
	case 0x0a:
	case 0x0c:
	case 0x0d:
		/*
		 * 1/2" cartridge tapes. Include HI-TC.
		 */
		sizestr = cart;
		sizestr[2] = '5';
		sizestr[3] = '0';
		un->un_dp->type = ST_TYPE_HIC;
		un->un_dp->densities[0] = 0x09;
		un->un_dp->densities[1] = 0x0a;
		un->un_dp->densities[2] = 0x0c;
		un->un_dp->densities[3] = 0x0d;
		break;

	case 0x13:
		sizestr = "DAT Data Storage (DDS)";
		un->un_dp->type = ST_TYPE_DAT;
		un->un_dp->options |= ST_AUTODEN_OVERRIDE;
		break;

	case 0x14:
		/*
		 * Helical Scan (Exabyte) devices
		 */
		sizestr = "8mm helical scan cartridge";
		un->un_dp->type = ST_TYPE_EXABYTE;
		un->un_dp->options |= ST_AUTODEN_OVERRIDE;
		break;
	}

	/*
	 * Assume LONG ERASE, BSF and BSR
	 */

	un->un_dp->options |= (ST_LONG_ERASE|ST_UNLOADABLE|ST_BSF|
				ST_BSR);

	/*
	 * Only if mode sense data says no buffered write, set NOBUF
	 */
	if (un->un_mspl->bufm == 0)
		un->un_dp->options |= ST_NOBUF;

	/*
	 * set up large read and write retry counts
	 */

	un->un_dp->max_rretries = un->un_dp->max_wretries = 1000;

	/*
	 * If this is a 0.50 inch reel tape, and
	 * it is *not* variable mode, try and
	 * set it to variable record length
	 * mode.
	 */
	if ((un->un_dp->options & ST_REEL) && un->un_bsize != 0 &&
	    (un->un_dp->options & ST_VARIABLE)) {
		int old_bsize = un->un_bsize;
		un->un_bsize = 0;
		if (st_modeselect(dev)) {
			un->un_bsize = old_bsize;
		} else {
			un->un_dp->bsize = 0;
			un->un_mspl->high_bl = un->un_mspl->mid_bl =
			    un->un_mspl->low_bl = 0;
		}
	}

	/*
	 * Write to console about type of device found
	 */
	ST_DEBUG6(ST_DEVINFO, st_label, CE_NOTE,
	    "Generic Drive, Vendor=%s\n\t%s", un->un_dp->name,
	    sizestr);
	if (un->un_dp->options & ST_VARIABLE) {
		scsi_log(ST_DEVINFO, st_label, CE_NOTE,
		    "Variable record length I/O\n");
	} else {
		scsi_log(ST_DEVINFO, st_label, CE_NOTE,
		    "Fixed record length (%d byte blocks) I/O\n",
		    un->un_dp->bsize);
	}
	ASSERT(mutex_owned(ST_MUTEX));
	return (0);
}

static int
st_determine_density(dev_t dev, int rw)
{
	int rval = 0;

	GET_SOFT_STATE(dev);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_determine_density(dev = 0x%x, rw = %s)\n",
	    dev, (rw == B_WRITE ? "WRITE": "READ"));

	ASSERT(mutex_owned(ST_MUTEX));

	/*
	 * If we're past BOT, density is determined already.
	 */
	if (un->un_fileno > 0) {
		/*
		 * XXX: put in a bitch message about attempting to
		 * XXX: change density past BOT.
		 */
		goto exit;
	}

	/*
	 * If we're going to be writing, we set the density
	 */
	if (rw == 0 || rw == B_WRITE) {
		/* un_curdens is used as an index into densities table */
		un->un_curdens = MT_DENSITY(un->un_dev);
		if (st_set_density(dev)) {
			rval = -1;
		}
		goto exit;
	}

	/*
	 * If density is known already,
	 * we don't have to get it again.(?)
	 */
	if (!un->un_density_known) {
		if (st_get_density(dev)) {
			rval = -1;
		}
	}

exit:
	ASSERT(mutex_owned(ST_MUTEX));
	return (rval);
}


/*
 * Try to determine density. We do this by attempting to read the
 * first record off the tape, cycling through the available density
 * codes as we go.
 */

static int
st_get_density(dev_t dev)
{
	int succes = 0, rval = -1, i;
	u_int size;
	u_char dens, olddens;

	GET_SOFT_STATE(dev);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_get_density(dev = 0x%x)\n", dev);

	ASSERT(mutex_owned(ST_MUTEX));

	if (un->un_dp->options & ST_AUTODEN_OVERRIDE) {
		un->un_density_known = 1;
		rval = 0;
		goto exit;
	}

	/*
	 * This will only work on variable record length tapes
	 * if and only if all variable record length tapes autodensity
	 * select.
	 */
	size = (unsigned)(un->un_dp->bsize ? un->un_dp->bsize : SECSIZE);
	un->un_tmpbuf = (caddr_t)kmem_alloc(size, KM_SLEEP);

	/*
	 * Start at the specified density
	 */

	dens = olddens = un->un_curdens = MT_DENSITY(un->un_dev);

	for (i = 0; i < NDENSITIES; i++, ((un->un_curdens == NDENSITIES - 1) ?
					(un->un_curdens = 0) :
					(un->un_curdens += 1))) {
		/*
		 * If we've done this density before,
		 * don't bother to do it again.
		 */
		dens = un->un_dp->densities[un->un_curdens];
		if (i > 0 && dens == olddens)
			continue;
		olddens = dens;
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "trying density 0x%x\n", dens);
		if (st_set_density(dev)) {
			continue;
		}

		succes = (st_cmd(dev, SCMD_READ, (int)size, SYNC_CMD) == 0);
		if (st_cmd(dev, SCMD_REWIND, 0, SYNC_CMD)) {
			break;
		}
		if (succes) {
			st_init(un);
			rval = 0;
			un->un_density_known = 1;
			break;
		}
	}
	(void) kmem_free(un->un_tmpbuf, size);
	un->un_tmpbuf = 0;

exit:
	ASSERT(mutex_owned(ST_MUTEX));
	return (rval);
}

static int
st_set_density(dev_t dev)
{
	int rval = 0;

	GET_SOFT_STATE(dev);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_set_density(dev = 0x%x): density = 0x%x\n", dev,
	    un->un_dp->densities[un->un_curdens]);

	ASSERT(mutex_owned(ST_MUTEX));

	if ((un->un_dp->options & ST_AUTODEN_OVERRIDE) == 0) {
		un->un_mspl->density = un->un_dp->densities[un->un_curdens];
		if (st_modeselect(dev)) {
			rval = -1;
			goto exit;
		}
	}
	un->un_density_known = 1;

exit:
	ASSERT(mutex_owned(ST_MUTEX));
	return (rval);
}

static int
st_loadtape(dev_t dev)
{
	int rval = 0;

	GET_SOFT_STATE(dev);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_loadtape(dev = 0x%x)\n", dev);

	ASSERT(mutex_owned(ST_MUTEX));

	/*
	 * 'LOAD' the tape to BOT by rewinding
	 */
	if (st_cmd(dev, SCMD_REWIND, 1, SYNC_CMD)) {
		rval = -1;
		goto exit;
	}

	/*
	 * run a MODE SENSE to get the write protect status, then run
	 * a MODESELECT operation in order to set any modes that might
	 * be appropriate for this device (like VARIABLE, etc..)
	 */
	if (st_cmd(dev, SCMD_MODE_SENSE, MSIZE, SYNC_CMD) ||
	    st_modeselect(dev)) {
		rval = -1;
		goto exit;
	}
	st_init(un);
	un->un_density_known = 0;

exit:
	ASSERT(mutex_owned(ST_MUTEX));
	return (rval);
}


/*
 * Note: QIC devices aren't so smart.  If you try to append
 * after EOM, the write can fail because the device doesn't know
 * it's at EOM.	 In that case, issue a read.  The read should fail
 * because there's no data, but the device knows it's at EOM,
 * so a subsequent write should succeed.  To further confuse matters,
 * the target returns the same error if the tape is positioned
 * such that a write would overwrite existing data.  That's why
 * we have to do the append test.  A read in the middle of
 * recorded data would succeed, thus indicating we're attempting
 * something illegal.
 */

void bp_mapin(struct buf *bp);

static void
st_test_append(struct buf *bp)
{
	dev_t dev = bp->b_edev;
	register struct scsi_tape *un;
	u_char status;

	un = ddi_get_soft_state(st_state, MTUNIT(dev));

	ASSERT(mutex_owned(ST_MUTEX));

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_test_append(): fileno %d\n", un->un_fileno);

	un->un_laststate = un->un_state;
	un->un_state = ST_STATE_APPEND_TESTING;
	un->un_test_append = 0;

	/*
	 * first, map in the buffer, because we're doing a double write --
	 * first into the kernel, then onto the tape.
	 */
	bp_mapin(bp);

	/*
	 * get a copy of the data....
	 */
	un->un_tmpbuf = (caddr_t)kmem_alloc((unsigned)bp->b_bcount,
	    KM_SLEEP);
	bcopy(bp->b_un.b_addr, un->un_tmpbuf, (u_int)bp->b_bcount);

	/*
	 * attempt the write..
	 */

	if (st_cmd(dev, SCMD_WRITE, (int)bp->b_bcount, SYNC_CMD) == 0) {
success:
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "append write succeeded\n");
		bp->b_resid = un->un_sbufp->b_resid;
		mutex_exit(ST_MUTEX);
		biodone(bp);
		mutex_enter(ST_MUTEX);
		un->un_laststate = un->un_state;
		un->un_state = ST_STATE_OPEN;
		(void) kmem_free(un->un_tmpbuf, (unsigned)bp->b_bcount);
		un->un_tmpbuf = 0;
		return;
	}

	/*
	 * The append failed. Do a short read. If that fails,  we are at EOM
	 * so we can retry the write command. If that succeeds, than we're
	 * all screwed up (the controller reported a real error).
	 *
	 * XXX: should the dummy read be > SECSIZE? should it be the device's
	 * XXX: block size?
	 *
	 */
	status = un->un_status;
	un->un_status = 0;
	(void) st_cmd(dev, SCMD_READ, SECSIZE, SYNC_CMD);
	if (un->un_status == KEY_BLANK_CHECK) {
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "append at EOM\n");
		/*
		 * Okay- the read failed. We should actually have confused
		 * the controller enough to allow writing. In any case, the
		 * i/o is on its own from here on out.
		 */
		un->un_laststate = un->un_state;
		un->un_state = ST_STATE_OPEN;
		bcopy(bp->b_un.b_addr, un->un_tmpbuf, (u_int)bp->b_bcount);
		if (st_cmd(dev, SCMD_WRITE, (int)bp->b_bcount,
		    SYNC_CMD) == 0) {
			goto success;
		}
	}

	ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "append write failed- not at EOM\n");
	bp->b_resid = bp->b_bcount;
	bioerror(bp, EIO);
	ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_test_append : EIO : append write failed - not at EOM");

	/*
	 * backspace one record to get back to where we were
	 */
	if (st_cmd(dev, SCMD_SPACE, Blk(-1), SYNC_CMD)) {
		un->un_fileno = -1;
	}

	un->un_err_resid = bp->b_resid;
	un->un_status = status;

	/*
	 * Note: biodone will do a bp_mapout()
	 */
	mutex_exit(ST_MUTEX);
	biodone(bp);
	mutex_enter(ST_MUTEX);
	un->un_laststate = un->un_state;
	un->un_state = ST_STATE_OPEN_PENDING_IO;
	(void) kmem_free(un->un_tmpbuf, (unsigned)bp->b_bcount);
	un->un_tmpbuf = 0;
}

/*
 * Special command handler
 */

/*
 * common st_cmd code. The fourth parameter states
 * whether the caller wishes to await the results
 * Note the release of the mutex during most of the function
 */
static int
st_cmd(dev_t dev, int com, int count, int wait)
{
	register struct buf *bp;
	register int err;

	GET_SOFT_STATE(dev);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_cmd(dev = 0x%x, com = 0x%x, count = %x, wait = %d)\n",
	    dev, com, count, wait);

	/* LOCK HELD ON ENTRY: un->un_sd->sd_mutex */
	ASSERT(mutex_owned(ST_MUTEX));

#ifdef STDEBUG
	if (st_debug)
		st_debug_cmds(un, com, count, wait);
#endif

	while (un->un_sbuf_busy)
		cv_wait(&un->un_sbuf_cv, ST_MUTEX);
	un->un_sbuf_busy = 1;

	bp = un->un_sbufp;

	bp->b_flags = (wait) ? B_BUSY : B_BUSY|B_ASYNC;

	/*
	 * Set count to the actual size of the data tranfer.
	 * For commands with no data transfer, set bp->b_bcount
	 * to the value to be used when constructing the
	 * cdb in st_make_cmd().
	 */
	switch (com) {
	case SCMD_READ:
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "special read %d\n", count);
		bp->b_flags |= B_READ;
		bp->b_un.b_addr = un->un_tmpbuf;
		break;

	case SCMD_WRITE:
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "special write %d\n", count);
		bp->b_un.b_addr = un->un_tmpbuf;
		break;

	case SCMD_WRITE_FILE_MARK:
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "write %d file marks\n", count);
		bp->b_bcount = count;
		count = 0;
		break;

	case SCMD_REWIND:
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG, "rewind\n");
		bp->b_bcount = 0;
		count = 0;
		break;

	case SCMD_SPACE:
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG, "space\n");
		bp->b_bcount = count;
		count = 0;
		break;

	case SCMD_LOAD:
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "%s tape\n", (count) ? "load" : "unload");
		bp->b_bcount = count;
		count = 0;
		break;

	case SCMD_ERASE:
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "erase tape\n");
		bp->b_bcount = 0;
		count = 0;
		break;

	case SCMD_MODE_SENSE:
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "mode sense\n");
		bp->b_flags |= B_READ;
		bp->b_un.b_addr = (caddr_t)(un->un_mspl);
		break;

	case SCMD_MODE_SELECT:
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "mode select\n");
		bp->b_un.b_addr = (caddr_t)(un->un_mspl);
		break;

	case SCMD_READ_BLKLIM:
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "read block limits\n");
		bp->b_flags |= B_READ;
		bp->b_un.b_addr = (caddr_t)(un->un_rbl);
		break;

	case SCMD_TEST_UNIT_READY:
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "test unit ready\n");
		bp->b_bcount = 0;
		count = 0;
		break;
	}

	mutex_exit(ST_MUTEX);

	if (count > 0) {
		/*
		 * We're going to do actual I/O.
		 * Set things up for physio.
		 */
		struct iovec aiov;
		struct uio auio;
		struct uio *uio = &auio;

		bzero((caddr_t)&auio, sizeof (struct uio));
		bzero((caddr_t)&aiov, sizeof (struct iovec));
		aiov.iov_base = bp->b_un.b_addr;
		aiov.iov_len = count;

		uio->uio_iov = &aiov;
		uio->uio_iovcnt = 1;
		uio->uio_resid = aiov.iov_len;
		uio->uio_segflg = UIO_SYSSPACE;
		uio->uio_loffset = 0;
		uio->uio_fmode = 0;

		/*
		 * Let physio do the rest...
		 */
		bp->b_forw = (struct buf *)com;
		bp->b_back = NULL;
		err = physio(st_strategy, bp, dev,
			(bp->b_flags & B_READ) ? B_READ : B_WRITE,
			st_minphys, uio);
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "st_cmd: physio returns %d\n", err);
	} else {
		/*
		 * Mimic physio
		 */
		bp->b_forw = (struct buf *)com;
		bp->b_back = NULL;
		bp->b_edev = dev;
		bp->b_dev = cmpdev(dev);
		bp->b_blkno = 0;
		bp->b_resid = 0;
		(void) st_strategy(bp);
		if (!wait) {
			/*
			 * This is an async command- the caller won't wait
			 * and doesn't care about errors.
			 */
			mutex_enter(ST_MUTEX);
			return (0);
		}
		err = biowait(bp);
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "st_cmd: biowait returns %d\n", err);
	}
	mutex_enter(ST_MUTEX);

	un->un_sbuf_busy = 0;
	cv_signal(&un->un_sbuf_cv);
	return (err);
}

/*
 * We assume someone else has set the density code
 */
static int
st_modeselect(dev_t dev)
{
	int rval = 0;

	GET_SOFT_STATE(dev);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_modeselect(dev = 0x%x): density = 0x%x\n",
	    dev, un->un_mspl->density);

	ASSERT(mutex_owned(ST_MUTEX));

	un->un_mspl->reserved1 = un->un_mspl->reserved2 = 0;
	un->un_mspl->wp = 0;
	if (un->un_dp->options & ST_NOBUF) {
		un->un_mspl->bufm = 0;
	} else {
		un->un_mspl->bufm = 1;
	}
	un->un_mspl->bd_len = 8;

	un->un_mspl->high_nb = un->un_mspl->mid_nb = un->un_mspl->low_nb = 0;
	un->un_mspl->high_bl = (un->un_bsize >> 16) & 0xff;
	un->un_mspl->mid_bl = (un->un_bsize >> 8) & 0xff;
	un->un_mspl->low_bl = (un->un_bsize) & 0xff;

	/*
	 * if this is configured to use modeselect to enable/disable
	 * compression, then get the device configuration page (0x10) and
	 * set the value accordingly, 0x00 = no compression, 0x01 = default
	 * compression algorithm.
	 */
	if (un->un_dp->options & ST_MODE_SEL_COMP) {
		register char *sense;

		/* get the current device configuration page */
		sense = kmem_zalloc(ST_DEV_CONFIG_ALLOC_LEN, KM_SLEEP);
		if ((rval = st_gen_mode_sense(dev, ST_DEV_CONFIG_PAGE, sense,
		    ST_DEV_CONFIG_ALLOC_LEN)) != 0) {
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_mode_select: compression mode sense failed\n");
			if (un->un_state >= ST_STATE_OPEN) {
				un->un_fileno = -1;
				rval = EIO;
			} else {
				rval = -1;
			}
			goto device_config_done;
		}

		/* zero out some reserved bytes. */
		sense[0] = 0x00;
		sense[1] = 0x00;
		sense[8] = 0x00;

#ifdef STDEBUG
		if (st_debug >= 6) {
			st_clean_print(ST_DEVINFO, st_label, CE_NOTE,
			    "Mode Select Buffer", sense,
			    ST_DEV_CONFIG_ALLOC_LEN);
		}
#endif

		/*
		 * if we are already set to the appropriate compression
		 * mode, don't set it again
		 */
		if (MT_DENSITY(dev) == 3) {
			/* They have selected  compression */

			if (sense[ST_DEV_CONFIG_COMP_BYTE] ==
			    ST_DEV_CONFIG_DEF_COMP) {
				goto device_config_done;
			} else {
				sense[ST_DEV_CONFIG_COMP_BYTE] =
					ST_DEV_CONFIG_DEF_COMP;
			}

		} else {
			/* no compression was selected */

			if (sense[ST_DEV_CONFIG_COMP_BYTE] ==
			    ST_DEV_CONFIG_NO_COMP) {
				goto device_config_done;
			} else {
				sense[ST_DEV_CONFIG_COMP_BYTE] =
					ST_DEV_CONFIG_NO_COMP;
			}
		}

		/*
		 * The size here is tricky, since some drives have 15 byte
		 * control pages and some have 16 byte.  On the sense, we
		 * know how many bytes after page len in dc page, so we key
		 * off this and calculate the page size.
		 *
		 * length offset + 1 + pagelen = full mode page params
		 */
		if ((rval = st_gen_mode_select(dev, sense,
		    ST_DEV_CONFIG_PL_BYTE + 1 +
		    sense[ST_DEV_CONFIG_PL_BYTE])) != 0) {
			ST_DEBUG2(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_mode_select: compression mode select failed\n");
			if (un->un_state >= ST_STATE_OPEN) {
				ST_DEBUG6(ST_DEVINFO, st_label, CE_WARN,
				    "unable to set tape mode 2\n");
				un->un_fileno = -1;
				rval = EIO;
			} else {
				rval = -1;
			}
		}

device_config_done:
		kmem_free((caddr_t)sense, ST_DEV_CONFIG_ALLOC_LEN);
		if (rval)
			goto exit;
	}

	if (st_cmd(dev, SCMD_MODE_SELECT, MSIZE, SYNC_CMD) ||
	    st_cmd(dev, SCMD_MODE_SENSE, MSIZE, SYNC_CMD)) {
		if (un->un_state >= ST_STATE_OPEN) {
			ST_DEBUG6(ST_DEVINFO, st_label, CE_WARN,
			    "unable to set tape mode\n");
			un->un_fileno = -1;
			rval = ENXIO;
		} else {
			rval = -1;
		}
	}

exit:
	ASSERT(mutex_owned(ST_MUTEX));
	return (rval);
}

/*
 * st_gen_mode_sense
 *
 * generic mode sense.. it allows for any page
 */
static int
st_gen_mode_sense(dev_t dev, int page, char *page_data, int page_size)
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

	r = st_ioctl_cmd(dev, com, UIO_SYSSPACE, UIO_SYSSPACE,
		UIO_SYSSPACE);
	kmem_free((caddr_t)com, sizeof (*com));
	return (r);
}

/*
 * st_gen_mode_select
 *
 * generic mode select.. it allows for any page
 */
static int
st_gen_mode_select(dev_t dev, char *page_data, int page_size)
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

	r = st_ioctl_cmd(dev, com, UIO_SYSSPACE, UIO_SYSSPACE,
		UIO_SYSSPACE);

	kmem_free((caddr_t)com, sizeof (*com));
	return (r);
}

static void
st_init(struct scsi_tape *un)
{
	ASSERT(mutex_owned(ST_MUTEX));

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	"st_init(): dev = 0x%x, will reset fileno, blkno, eof\n", un->un_dev);

	un->un_blkno = 0;
	un->un_fileno = 0;
	un->un_lastop = ST_OP_NIL;
	un->un_eof = ST_NO_EOF;
	if (st_error_level != SCSI_ERR_ALL) {
		if (DEBUGGING) {
			st_error_level = SCSI_ERR_ALL;
		} else {
			st_error_level = SCSI_ERR_RETRYABLE;
		}
	}
}


static void
st_make_cmd(struct scsi_tape *un, struct buf *bp, int (*func)())
{
	register struct scsi_pkt *pkt;
	register struct uscsi_cmd *ucmd;
	register count, com, tval = st_io_time;
	register flags = 0;
	char fixbit = (un->un_bsize == 0) ? 0 : 1;

	ASSERT(mutex_owned(ST_MUTEX));

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_make_cmd(): dev = 0x%x\n", un->un_dev);

	/*
	 * for very slow devices, multiply timeout value with LONG_TIMEOUTS
	 */
	tval *= ((un->un_dp->options & ST_LONG_TIMEOUTS) ?
	    LONG_TIMEOUTS : 1);

	if (un->un_dp->options & ST_NOPARITY)
		flags |= FLAG_NOPARITY;
	/*
	 * fixbit is for setting the Fixed Mode and Suppress Incorrect
	 * Length Indicator bits on read/write commands, for setting
	 * the Long bit on erase commands, and for setting the Code
	 * Field bits on space commands.
	 * XXX why do we set lastop here?
	 */

	if (bp != un->un_sbufp) {		/* regular raw I/O */
		register int stat_size = (un->un_arq_enabled ?
			sizeof (struct scsi_arq_status) : 1);
		pkt = scsi_init_pkt(ROUTE, NULL, bp,
		    CDB_GROUP0, stat_size, 0, 0, func, (caddr_t)un);
		if (pkt == (struct scsi_pkt *)0) {
			goto exit;
		}
		SET_BP_PKT(bp, pkt);
		if (un->un_bsize == 0) {
			count = bp->b_bcount;
		} else {
			count = bp->b_bcount / un->un_bsize;
		}
		if (bp->b_flags & B_READ) {
			com = SCMD_READ;
			un->un_lastop = ST_OP_READ;
		} else {
			com = SCMD_WRITE;
			un->un_lastop = ST_OP_WRITE;
		}

		/*
		 * For really large xfers, increase timeout
		 */
		if (bp->b_bcount > (10 * ONE_MEG))
			tval *= bp->b_bcount/(10 * ONE_MEG);

		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "%s %d amt 0x%x\n", (com == SCMD_WRITE) ?
		    "write": "read", un->un_blkno, bp->b_bcount);

	} else if ((ucmd = BP_UCMD(bp)) != 0) {
		/*
		 * uscsi - build command, allocate scsi resources
		 */
		st_make_uscsi_cmd(un, ucmd, bp, func);
		goto exit;

	} else {				/* special I/O */
		char saved_lastop = un->un_lastop;
		register struct buf *allocbp = 0;
		register int stat_size = (un->un_arq_enabled ?
			sizeof (struct scsi_arq_status) : 1);

		un->un_lastop = ST_OP_CTL;	/* usual */

		com = (int)bp->b_forw;
		count = bp->b_bcount;

		switch (com) {
		case SCMD_READ:
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "special read %d\n", count);
			if (un->un_bsize == 0) {
				fixbit = 2;	/* suppress SILI */
			} else {
				count /= un->un_bsize;
			}
			allocbp = bp;
			un->un_lastop = ST_OP_READ;
			break;

		case SCMD_WRITE:
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "special write %d\n", count);
			if (un->un_bsize != 0)
				count /= un->un_bsize;
			allocbp = bp;
			un->un_lastop = ST_OP_WRITE;
			break;

		case SCMD_WRITE_FILE_MARK:
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "write %d file marks\n", count);
			un->un_lastop = ST_OP_WEOF;
			fixbit = 0;
			break;

		case SCMD_REWIND:
			fixbit = 0;
			count = 0;
			tval = st_space_time;
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "rewind\n");
			break;

		case SCMD_SPACE:
			fixbit = Isfmk(count);
			count = space_cnt(count);
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "space %d %s from file %d blk %d\n",
			    count, (fixbit) ? "filemarks" : "records",
			    un->un_fileno, un->un_blkno);
			tval = st_space_time;
			break;

		case SCMD_LOAD:
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "%s tape\n", (count) ? "load" : "unload");
			if ((flags&FLAG_NODISCON) == 0)
				fixbit = 0;
			tval = st_space_time;
			break;

		case SCMD_ERASE:
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "erase tape\n");
			count = 0;
			/*
			 * We support long erase only
			 */
			fixbit = 1;
			if (un->un_dp->options & ST_LONG_ERASE) {
				tval = 3 * st_space_time;
			} else {
				tval = st_space_time;
			}
			break;

		case SCMD_MODE_SENSE:
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "mode sense\n");
			allocbp = bp;
			fixbit = 0;
			un->un_lastop = saved_lastop;
			break;

		case SCMD_MODE_SELECT:
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "mode select\n");
			allocbp = bp;
			fixbit = 0;
			un->un_lastop = saved_lastop;
			break;

		case SCMD_READ_BLKLIM:
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "read block limits\n");
			allocbp = bp;
			fixbit = count = 0;
			un->un_lastop = saved_lastop;
			break;

		case SCMD_TEST_UNIT_READY:
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "test unit ready\n");
			fixbit = 0;
			un->un_lastop = saved_lastop;
			break;
		}
		pkt = scsi_init_pkt(ROUTE, NULL, allocbp,
			CDB_GROUP0, stat_size, 0, 0, func, (caddr_t)un);
		if (pkt == (struct scsi_pkt *)0) {
			goto exit;
		}
		if (allocbp)
			ASSERT(geterror(allocbp) == 0);

	}

	makecom_g0_s(pkt, ST_SCSI_DEVP, flags, com, count, fixbit);
	pkt->pkt_time = tval;
	pkt->pkt_comp = st_intr;
	pkt->pkt_private = (opaque_t)bp;
	SET_BP_PKT(bp, pkt);

exit:
	ASSERT(mutex_owned(ST_MUTEX));
}


/*
 * Build a command based on a uscsi command;
 */
static void
st_make_uscsi_cmd(struct scsi_tape *un, struct uscsi_cmd *ucmd,
    struct buf *bp, int (*func)())
{
	struct scsi_pkt *pkt;
	caddr_t cdb;
	int	cdblen;
	int	flags;
	register int stat_size;

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_make_uscsi_cmd(): dev = 0x%x\n", un->un_dev);

	if (ucmd->uscsi_flags & USCSI_RQENABLE) {
		stat_size = (un->un_arq_enabled ?
		    sizeof (struct scsi_arq_status) : 1);
	} else {
		stat_size = 1;
	}

	ASSERT(mutex_owned(ST_MUTEX));

	un->un_lastop = ST_OP_CTL;	/* usual */

	cdb = ucmd->uscsi_cdb;
	cdblen = ucmd->uscsi_cdblen;

	ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_make_uscsi_cmd: buflen=%d bcount=%d\n",
		ucmd->uscsi_buflen, bp->b_bcount);
	pkt = scsi_init_pkt(ROUTE, NULL,
		(bp->b_bcount > 0) ? bp : NULL,
		cdblen, stat_size, 0, 0, func, (caddr_t)un);
	if (pkt == (struct scsi_pkt *)NULL) {
		goto exit;
	}

	bcopy(cdb, (caddr_t)pkt->pkt_cdbp, (u_int)cdblen);

#ifdef STDEBUG
	if (st_debug >= 6) {
		st_clean_print(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "pkt_cdbp", (char *)cdb, cdblen);
	}
#endif
	flags = (un->un_dp->options & ST_NOPARITY) ? FLAG_NOPARITY : 0;

	pkt->pkt_flags = flags;
	pkt->pkt_time = ucmd->uscsi_timeout;
	pkt->pkt_comp = st_intr;
	pkt->pkt_private = (opaque_t)bp;
	SET_BP_PKT(bp, pkt);
exit:
	ASSERT(mutex_owned(ST_MUTEX));
}


/*
 * restart cmd currently at the head of the runq
 */
static void
st_intr_restart(caddr_t arg)
{
	struct scsi_tape *un = (struct scsi_tape *)arg;
	struct buf *bp;
	int status;

	mutex_enter(ST_MUTEX);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
		"st_intr_restart(), un = 0x%x\n", un);

	/*
	 * move from waitq to runq, if there is anything on the waitq
	 */
	if ((bp = un->un_quef) == 0) {
		mutex_exit(ST_MUTEX);
		return;
	}

	/*
	 * Here we know :
	 *	throttle = 0, via st_handle_intr_busy
	 *	ncmds = 0   , also via throttle == 0
	 */

	if (un->un_quel == bp) {
		un->un_quel = 0;
		un->un_quef = 0;	/* we know it's the first one */
	} else {
		un->un_quef = bp->b_actf;
	}
	bp->b_actf = 0;

	if (un->un_runqf) {
		/*
		 * not good, we don't want to requeue something after
		 * another.
		 */
		mutex_exit(ST_MUTEX);
		goto done_error;
	} else {
		un->un_runqf = bp;
		un->un_runql = bp;
	}

	mutex_exit(ST_MUTEX);

	if ((status = scsi_transport(BP_PKT(bp))) != TRAN_ACCEPT) {
		if (status == TRAN_BUSY) {
			if (st_handle_intr_busy(un, bp,
			    ST_TRAN_BUSY_TIMEOUT) == 0)
				return;	/* timeout is setup again */
		}
		goto done_error;

	} else {
		mutex_enter(ST_MUTEX);
		un->un_tran_retry_ct = 0;
		un->un_throttle = un->un_last_throttle;
		mutex_exit(ST_MUTEX);
		return;
	}

done_error:
	ST_DEBUG6(ST_DEVINFO, st_label, CE_WARN,
	    "restart transport rejected\n");
	bp->b_resid = bp->b_bcount;
	bioerror(bp, EIO);

#ifndef __lock_lint
	/*
	 * warlock doesn't understand this potential
	 * recursion?
	 */
	mutex_enter(ST_MUTEX);
	st_done_and_mutex_exit(un, bp);
#endif
	ST_DEBUG6(ST_DEVINFO, st_label, CE_WARN,
	    "busy restart aborted\n");
}

/*
 * restart cmd currently at the start of the waitq
 */
static void
st_start_restart(caddr_t arg)
{
	struct scsi_tape *un = (struct scsi_tape *)arg;

	ASSERT(un != NULL);

	mutex_enter(ST_MUTEX);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
		"st_tran_restart()\n");

	if (un->un_quef) {
		st_start(un, 0);
	}

	mutex_exit(ST_MUTEX);
}


/*
 * Command completion processing
 *
 */
static void
st_intr(struct scsi_pkt *pkt)
{
	register struct scsi_tape *un;
	register struct buf *bp;
	register action;
	long	timeout;

	bp = (struct buf *)pkt->pkt_private;
	un = ddi_get_soft_state(st_state, MTUNIT(bp->b_edev));

	mutex_enter(ST_MUTEX);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG, "st_intr()\n");

	if (pkt->pkt_reason != CMD_CMPLT) {
		action = st_handle_incomplete(un);
	/*
	 * At this point we know that the command was successfully
	 * completed. Now what?
	 */
	} else if (un->un_arq_enabled &&
	    (pkt->pkt_state & STATE_ARQ_DONE)) {
		/*
		 * the transport layer successfully completed an autorqsense
		 */
		action = st_handle_autosense(un);

	} else if (un->un_state == ST_STATE_SENSING) {
		/*
		 * okay. We were running a REQUEST SENSE. Find
		 * out what to do next.
		 */
		action = st_handle_sense(un);

		/*
		 * set pkt back to original packet in case we will have
		 * to requeue it
		 */
		pkt = BP_PKT(bp);
	} else {
		/*
		 * Okay, we weren't running a REQUEST SENSE. Call a routine
		 * to see if the status bits we're okay. As a side effect,
		 * clear state for this device, set non-error b_resid values,
		 * etc.
		 * If a request sense is to be run, that will happen.
		 */
		action = st_check_error(un, pkt);
	}

	/*
	 * Restore old state if we were sensing.
	 *
	 */
	if (un->un_state == ST_STATE_SENSING && action != QUE_SENSE) {
		un->un_state = un->un_laststate;
	}

	ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_intr: pkt=%x, bp=%x, action=%x, status=%x\n",
	    pkt, bp, action, SCBP_C(pkt));


	switch (action) {
	case COMMAND_DONE_ERROR:
		if (un->un_eof < ST_EOT_PENDING &&
		    un->un_state >= ST_STATE_OPEN) {
			/*
			 * all errors set state of the tape to 'unknown'
			 * unless we're at EOT or are doing append testing.
			 * If sense key was illegal request, preserve state.
			 */
			if (un->un_status != KEY_ILLEGAL_REQUEST) {
				un->un_fileno = -1;
			}
		}
		un->un_err_resid = bp->b_resid = bp->b_bcount;
		bioerror(bp, EIO);
		goto done;

	case COMMAND_DONE_ERROR_RECOVERED:
		un->un_err_resid = bp->b_resid = bp->b_bcount;
		bioerror(bp, EIO);

		/*FALLTHROUGH*/
	case COMMAND_DONE:
		st_set_state(un);
done:
		st_done_and_mutex_exit(un, bp);
		return;

	case QUE_SENSE:
		if (un->un_ncmds)
			goto sense_error;

		if (un->un_state != ST_STATE_SENSING) {
			un->un_laststate = un->un_state;
			un->un_state = ST_STATE_SENSING;
		}
		un->un_rqs->pkt_private = (opaque_t)bp;
		bzero((caddr_t)ST_RQSENSE, SENSE_LENGTH);

		un->un_throttle = 0;

		mutex_exit(ST_MUTEX);

		/*
		 * never retry this, some other command will have nuked the
		 * sense, anyway
		 */
		if (scsi_transport(un->un_rqs) == TRAN_ACCEPT) {
			mutex_enter(ST_MUTEX);
			un->un_throttle = un->un_last_throttle;
			mutex_exit(ST_MUTEX);
			return;
		}
		mutex_enter(ST_MUTEX);
sense_error:
		un->un_fileno = -1;
		bioerror(bp, EIO);
		goto done;

	case QUE_BUSY_COMMAND:
		/* longish timeout */
		timeout = ST_STATUS_BUSY_TIMEOUT;
		goto que_it_up;

	case QUE_COMMAND:
		/* short timeout */
		timeout = ST_TRAN_BUSY_TIMEOUT;
que_it_up:
		/*
		 * let st_handle_intr_busy put this bp back on waitq and make
		 * checks to see if it is ok to requeue the command.
		 */
		mutex_exit(ST_MUTEX);
		if (st_handle_intr_busy(un, bp, timeout) == 0)
			return;		/* timeout is setup again */

		mutex_enter(ST_MUTEX);
		un->un_fileno = -1;
		un->un_err_resid = bp->b_resid = bp->b_bcount;
		bioerror(bp, EIO);
		goto done;

	case JUST_RETURN:
	default:
		mutex_exit(ST_MUTEX);
		return;
	}
	/*NOTREACHED*/
}

static int
st_handle_incomplete(struct scsi_tape *un)
{
	static char *fail = "SCSI transport failed: reason '%s': %s\n";
	register rval = COMMAND_DONE_ERROR;
	struct buf *bp = un->un_runqf;
	struct scsi_pkt *pkt = (un->un_state == ST_STATE_SENSING) ?
			un->un_rqs : BP_PKT(bp);
	int result;

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
		"st_handle_incomplete(): dev = 0x%x\n", un->un_dev);

	ASSERT(mutex_owned(ST_MUTEX));

	switch (pkt->pkt_reason) {
	case CMD_INCOMPLETE:	/* tran stopped with not normal state */
		/*
		 * this occurs when accessing a powered down drive, no
		 * need to complain; just fail the open
		 */
#ifdef STDEBUG
		if (st_debug >= 1) {
			st_clean_print(ST_DEVINFO, st_label, CE_WARN,
			    "Failed CDB", (char *)pkt->pkt_cdbp, CDB_SIZE);
		}

#endif

		/*
		 * Since we really don't know what caused the
		 * cmd_incomplete, we don't want the target waking up in a
		 * different mode (async/sync) than we expect, so
		 * re-negotiate it when you talk to it again.
		 */
		(void) scsi_ifsetcap(ROUTE, "synchronous", 1, 1);

		/*
		 * if we have commands outstanding in HBA, and a command
		 * comes back incomplete, we're hosed, so reset target
		 * If we have the bus, but cmd_incomplete, we probably just
		 * have a failed selection, so don't reset the target, just
		 * requeue the command and try again
		 */
		if ((un->un_ncmds) || (pkt->pkt_state != STATE_GOT_BUS)) {
			goto reset_target;
		}

		/*
		 * Retry selection a couple more times if we're
		 * open.  If opening, we only try just once to
		 * reduce probe time for nonexistant devices.
		 */
		if ((un->un_laststate > ST_STATE_OPENING) &&
		    ((int)un->un_retry_ct < st_selection_retry_count)) {
			rval = QUE_COMMAND;
		}
		break;

	case CMD_TIMEOUT:	/* Command timed out */
		un->un_status = SUN_KEY_TIMEOUT;

		/*FALLTHROUGH*/
	default:
reset_target:
		ST_DEBUG6(ST_DEVINFO, st_label, CE_WARN,
		    "transport completed with %s\n",
		    scsi_rname(pkt->pkt_reason));

		if ((pkt->pkt_state & STATE_GOT_TARGET) &&
		    ((pkt->pkt_statistics & (STAT_BUS_RESET | STAT_DEV_RESET |
			STAT_ABORTED)) == 0)) {

			mutex_exit(ST_MUTEX);

			result = scsi_reset(ROUTE, RESET_TARGET);
			/*
			 * if target reset fails, then pull the chain
			 */
			if (result == 0) {
				result = scsi_reset(ROUTE, RESET_ALL);
			}
			mutex_enter(ST_MUTEX);

			if ((result == 0) && (un->un_state >= ST_STATE_OPEN)) {
				/* no hope left to recover */
				scsi_log(ST_DEVINFO, st_label, CE_WARN,
				    "recovery by resets failed\n");
				return (rval);
			}
		}
	}

	if (un->un_state == ST_STATE_SENSING) {
		if ((int)un->un_retry_ct++ < st_retry_count) {
			rval = QUE_SENSE;
		}
	} else if ((int)un->un_retry_ct++ < st_retry_count) {
		if (bp == un->un_sbufp) {
			switch ((int)bp->b_forw) {
			case SCMD_MODE_SENSE:
			case SCMD_MODE_SELECT:
			case SCMD_READ_BLKLIM:
			case SCMD_REWIND:
			case SCMD_LOAD:
			case SCMD_TEST_UNIT_READY:
				/*
				 * These commands can be rerun with impunity
				 */
				rval = QUE_COMMAND;
				break;
			}
		}
	}

	if (un->un_state >= ST_STATE_OPEN) {
		scsi_log(ST_DEVINFO, st_label, CE_WARN,
		    fail, scsi_rname(pkt->pkt_reason),
			(rval == COMMAND_DONE_ERROR)?
			"giving up" : "retrying command");
	}
	return (rval);
}

/*
 * if the transport is busy, then put this bp back on the waitq, on the
 * interrupt thread, where we want the head of the queue and not the
 * end
 */
static int
st_handle_intr_busy(struct scsi_tape *un, struct buf *bp, long timeout_interval)
{
	struct buf *last_quef;
	int rval = 0;

	mutex_enter(ST_MUTEX);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_handle_intr_busy(), un = 0x%x\n", un);

	/*
	 * Check to see if we hit the retry timeout. We check to make sure
	 * this is the first one on the the runq and make sure we have not
	 * queued up any more, so this one has to be the last on the list
	 * also. If it is not, we have to fail.  If it is not the first, but
	 * is the last we are in trouble anyway, as we are in the interrupt
	 * context here.
	 */
	if (((int)un->un_tran_retry_ct++ > st_retry_count) ||
	    (un->un_ncmds) ||
	    ((un->un_runqf != bp) && (un->un_runql != bp))) {
		rval = -1;
		goto exit;
	}


	/* put the bp back on the waitq */
	if (un->un_quef) {
		last_quef = un->un_quef;
		un->un_quef = bp;
		bp->b_actf = last_quef;
	} else  {
		bp->b_actf = 0;
		un->un_quef = bp;
		un->un_quel = bp;
	}

	/*
	 * We know that this is the first and last on the runq at this time,
	 * so we just nullify those two queues
	 */
	un->un_runqf = 0;
	un->un_runql = 0;

	/*
	 * We don't want any other commands being started in the mean time.
	 * If start had just released mutex after putting somthing on the
	 * runq, we won't even get here.
	 */
	un->un_throttle = 0;

	/*
	 * all queues are aligned, we are just waiting to
	 * transport
	 */
	(void) timeout(st_intr_restart, (caddr_t)un, timeout_interval);

exit:
	mutex_exit(ST_MUTEX);
	return (rval);
}

static int
st_handle_sense(struct scsi_tape *un)
{
	struct scsi_pkt *rqpkt = un->un_rqs;
	register int rval = COMMAND_DONE_ERROR;
	int amt;

	ASSERT(mutex_owned(ST_MUTEX));

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
		"st_handle_sense()\n");

	if (SCBP(rqpkt)->sts_busy) {
		ST_DEBUG4(ST_DEVINFO, st_label, CE_WARN,
		    "busy unit on request sense\n");
		if ((int)un->un_retry_ct++ < st_retry_count) {
			rval = QUE_BUSY_COMMAND;
		}
		return (rval);
	} else if (SCBP(rqpkt)->sts_chk) {
		ST_DEBUG6(ST_DEVINFO, st_label, CE_WARN,
		    "Check Condition on REQUEST SENSE\n");
		return (rval);
	}

	/* was there enough data? */
	amt = SENSE_LENGTH - rqpkt->pkt_resid;
	if ((rqpkt->pkt_state & STATE_XFERRED_DATA) == 0 ||
	    (amt < SUN_MIN_SENSE_LENGTH)) {
		ST_DEBUG6(ST_DEVINFO, st_label, CE_WARN,
		    "REQUEST SENSE couldn't get sense data\n");
		return (rval);
	}
	return (st_decode_sense(un, amt, SCBP(rqpkt)));
}

static int
st_handle_autosense(struct scsi_tape *un)
{
	register struct buf *bp = un->un_runqf;
	struct scsi_pkt *pkt = BP_PKT(bp);
	struct scsi_arq_status *arqstat =
	    (struct scsi_arq_status *)pkt->pkt_scbp;
	register int rval = COMMAND_DONE_ERROR;
	int amt;

	ASSERT(mutex_owned(ST_MUTEX));

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
		"st_handle_autosense()\n");

	if (arqstat->sts_rqpkt_status.sts_busy) {
		ST_DEBUG4(ST_DEVINFO, st_label, CE_WARN,
		    "busy unit on request sense\n");
		/*
		 * we return QUE_SENSE so st_intr will setup the SENSE cmd.
		 * the disadvantage is that we do not have any delay for the
		 * second retry of rqsense and we have to keep a packet around
		 */
		return (QUE_SENSE);

	} else if (arqstat->sts_rqpkt_reason != CMD_CMPLT) {
		ST_DEBUG6(ST_DEVINFO, st_label, CE_WARN,
		    "transport error on REQUEST SENSE\n");
		if ((arqstat->sts_rqpkt_state & STATE_GOT_TARGET) &&
		    ((arqstat->sts_rqpkt_statistics &
		    (STAT_BUS_RESET | STAT_DEV_RESET | STAT_ABORTED)) == 0)) {
			mutex_exit(ST_MUTEX);
			if (scsi_reset(ROUTE, RESET_TARGET) == 0) {
				/*
				 * if target reset fails, then pull the chain
				 */
				if (scsi_reset(ROUTE, RESET_ALL) == 0) {
					ST_DEBUG6(ST_DEVINFO, st_label,
					    CE_WARN,
					    "recovery by resets failed\n");
				}
			}
			mutex_enter(ST_MUTEX);
		}
		return (rval);

	} else if (arqstat->sts_rqpkt_status.sts_chk) {
		ST_DEBUG6(ST_DEVINFO, st_label, CE_WARN,
		    "Check Condition on REQUEST SENSE\n");
		return (rval);
	}


	/* was there enough data? */
	amt = SENSE_LENGTH - arqstat->sts_rqpkt_resid;
	if ((arqstat->sts_rqpkt_state & STATE_XFERRED_DATA) == 0 ||
	    (amt < SUN_MIN_SENSE_LENGTH)) {
		ST_DEBUG6(ST_DEVINFO, st_label, CE_WARN,
		    "REQUEST SENSE couldn't get sense data\n");
		return (rval);
	}

	bcopy((caddr_t)&arqstat->sts_sensedata, (caddr_t)ST_RQSENSE,
	    SENSE_LENGTH);

	return (st_decode_sense(un, amt, &arqstat->sts_rqpkt_status));
}

static int
st_decode_sense(struct scsi_tape *un, int amt,
	struct scsi_status *statusp)
{
	register struct buf *bp = un->un_runqf;
	struct scsi_pkt *pkt = BP_PKT(bp);
	register int rval = COMMAND_DONE_ERROR, resid;
	struct scsi_extended_sense *sensep = ST_RQSENSE;
	int severity;

	ASSERT(mutex_owned(ST_MUTEX));

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
		"st_decode_sense()\n");

	/*
	 * For uscsi commands, squirrel away a copy of the
	 * results of the Request Sense.
	 */
	if (USCSI_CMD(bp)) {
		struct uscsi_cmd *ucmd = BP_UCMD(bp);
		ucmd->uscsi_rqstatus = *(u_char *)statusp;
		if (ucmd->uscsi_rqlen && un->un_srqbufp) {
			u_char rqlen = min((u_char)amt, ucmd->uscsi_rqlen);
			ucmd->uscsi_rqresid = ucmd->uscsi_rqlen - rqlen;
			bcopy((caddr_t)ST_RQSENSE,
				un->un_srqbufp, rqlen);
			ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
				"st_decode_sense: stat=0x%x resid=0x%x\n",
				ucmd->uscsi_rqstatus, ucmd->uscsi_rqresid);
		}
	}

	/*
	 * If the drive is an MT-02, reposition the
	 * secondary error code into the proper place.
	 *
	 * XXX	MT-02 is non-CCS tape, so secondary error code
	 * is in byte 8.  However, in SCSI-2, tape has CCS definition
	 * so it's in byte 12.
	 */
	if (un->un_dp->type == ST_TYPE_EMULEX) {
		sensep->es_code = sensep->es_add_info[0];
	}

	if (bp != un->un_sbufp) {
		if (sensep->es_valid) {
			resid = (sensep->es_info_1 << 24) |
				(sensep->es_info_2 << 16) |
				(sensep->es_info_3 << 8)  |
				(sensep->es_info_4);
			if (un->un_bsize) {
				resid *= un->un_bsize;
			}
		} else if (pkt->pkt_state & STATE_XFERRED_DATA) {
			resid = pkt->pkt_resid;
		} else {
			resid = bp->b_bcount;
		}
		ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
		    "st_handle_sense (rw): xferred bit = %d, resid=%d (%d), "
		    "pkt_resid=%d\n", pkt->pkt_state & STATE_XFERRED_DATA,
		    resid,
		    (sensep->es_info_1 << 24) |
		    (sensep->es_info_2 << 16) |
		    (sensep->es_info_3 << 8)  |
		    (sensep->es_info_4),
		    pkt->pkt_resid);
		/*
		 * The problem is, what should we believe?
		 */
		if (resid && (pkt->pkt_resid == 0)) {
			pkt->pkt_resid = resid;
		}
	} else {
		/*
		 * If the command is SCMD_SPACE, we need to get the
		 * residual as returned in the sense data, to adjust
		 * our idea of current tape position correctly
		 */
		if ((CDBP(pkt)->scc_cmd == SCMD_SPACE ||
		    CDBP(pkt)->scc_cmd == SCMD_WRITE_FILE_MARK) &&
		    (sensep->es_valid)) {
			resid = (sensep->es_info_1 << 24) |
			    (sensep->es_info_2 << 16) |
			    (sensep->es_info_3 << 8)  |
			    (sensep->es_info_4);
			bp->b_resid = resid;
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_handle_sense(other):	resid=%d\n",
			    resid);
		} else {
			/*
			 * If the special command is SCMD_READ,
			 * the correct resid will be set later.
			 */
			resid = bp->b_bcount;
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "st_handle_sense(special read):  resid=%d\n",
				resid);
		}
	}

	if ((un->un_state >= ST_STATE_OPEN) &&
	    (DEBUGGING || st_error_level == SCSI_ERR_ALL)) {
		st_clean_print(ST_DEVINFO, st_label, CE_NOTE,
		    "Failed CDB", (char *)pkt->pkt_cdbp, CDB_SIZE);
		st_clean_print(ST_DEVINFO, st_label, CE_CONT,
		    "sense data", (char *)sensep, amt);
		scsi_log(ST_DEVINFO, st_label, CE_CONT,
		    "count 0x%x resid 0x%x pktresid 0x%x\n",
		    bp->b_bcount, resid, pkt->pkt_resid);
	}

	switch (un->un_status = sensep->es_key) {
	case KEY_NO_SENSE:
		severity = SCSI_ERR_INFO;
		goto common;

	case KEY_RECOVERABLE_ERROR:
		severity = SCSI_ERR_RECOVERED;
	common:
		/*
		 * XXX only want reads to be stopped by filemarks.
		 * Don't want them to be stopped by EOT.  EOT matters
		 * only on write.
		 */
		if (sensep->es_filmk && !sensep->es_eom) {
			rval = COMMAND_DONE;
		} else if (sensep->es_eom) {
			rval = COMMAND_DONE;
		} else if (sensep->es_ili) {
			/*
			 * Fun with variable length record devices:
			 * for specifying larger blocks sizes than the
			 * actual physical record size.
			 */
			if (un->un_bsize == 0 && resid > 0) {
				/*
				 * XXX! Ugly
				 */
				pkt->pkt_resid = resid;
				rval = COMMAND_DONE;
			} else if (un->un_bsize == 0 && resid < 0) {
				rval = COMMAND_DONE_ERROR_RECOVERED;
				bioerror(bp, EINVAL);
			} else {
				severity = SCSI_ERR_FATAL;
				rval = COMMAND_DONE_ERROR;
				bioerror(bp, EINVAL);
			}
		} else {
			/*
			 * we hope and pray for this just being
			 * something we can ignore (ie. a
			 * truly recoverable soft error)
			 */
			rval = COMMAND_DONE;
		}
		if (sensep->es_filmk) {
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "filemark\n");
			un->un_status = SUN_KEY_EOF;
			un->un_eof = ST_EOF_PENDING;
		}

		/*
		 * ignore eom when reading, a fmk should terminate reading
		 */
		if ((sensep->es_eom) &&
		    (CDBP(pkt)->scc_cmd != SCMD_READ)) {
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG, "eom\n");
			un->un_status = SUN_KEY_EOT;
			un->un_eof = ST_EOM;
		}
		break;

	case KEY_ILLEGAL_REQUEST:

		if (un->un_laststate >= ST_STATE_OPEN) {
			severity = SCSI_ERR_FATAL;
		} else {
			severity = SCSI_ERR_INFO;
		}
		rval = COMMAND_DONE_ERROR;
		break;

	case KEY_MEDIUM_ERROR:
		severity = SCSI_ERR_FATAL;

		/*
		 * for (buffered) writes, a medium error must be fatal
		 */
		if (CDBP(pkt)->scc_cmd != SCMD_WRITE) {
			rval = COMMAND_DONE_ERROR_RECOVERED;
		}

check_keys:
		/*
		 * attempt to process the keys in the presence of
		 * other errors
		 */
		if (sensep->es_ili && rval != COMMAND_DONE_ERROR) {
			/*
			 * Fun with variable length record devices:
			 * for specifying larger blocks sizes than the
			 * actual physical record size.
			 */
			if (un->un_bsize == 0 && resid > 0) {
				/*
				 * XXX! Ugly
				 */
				pkt->pkt_resid = resid;
			} else if (un->un_bsize == 0 && resid < 0) {
				bioerror(bp, EINVAL);
			} else {
				severity = SCSI_ERR_FATAL;
				rval = COMMAND_DONE_ERROR;
				bioerror(bp, EINVAL);
			}
		}
		if (sensep->es_filmk) {
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "filemark\n");
			un->un_status = SUN_KEY_EOF;
			un->un_eof = ST_EOF_PENDING;
		}

		/*
		 * ignore eom when reading, a fmk should terminate reading
		 */
		if ((sensep->es_eom) &&
		    (CDBP(pkt)->scc_cmd != SCMD_READ)) {
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG, "eom\n");
			un->un_status = SUN_KEY_EOT;
			un->un_eof = ST_EOM;
		}

		break;

	case KEY_VOLUME_OVERFLOW:
		un->un_eof = ST_EOM;
		severity = SCSI_ERR_FATAL;
		rval = COMMAND_DONE_ERROR;
		goto check_keys;

	case KEY_HARDWARE_ERROR:
		severity = SCSI_ERR_FATAL;
		rval = COMMAND_DONE_ERROR;
		break;

	case KEY_BLANK_CHECK:
		severity = SCSI_ERR_INFO;

		/*
		 * if not a special request and some data was xferred then it
		 * it is not an error yet
		 */
		if (bp != un->un_sbufp && (bp->b_flags & B_READ)) {
			/*
			 * no error for read with or without data xferred
			 */
			un->un_status = SUN_KEY_EOT;
			un->un_eof = ST_EOT;
			rval = COMMAND_DONE;
			goto check_keys;
		} else if (bp != un->un_sbufp &&
		    (pkt->pkt_state & STATE_XFERRED_DATA)) {
			rval = COMMAND_DONE;
		} else {
			rval = COMMAND_DONE_ERROR_RECOVERED;
		}

		if (un->un_laststate >= ST_STATE_OPEN) {
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "blank check\n");
			un->un_eof = ST_EOM;
		}
		if ((CDBP(pkt)->scc_cmd == SCMD_SPACE) &&
		    (un->un_dp->options & ST_KNOWS_EOD) &&
		    (severity = SCSI_ERR_INFO)) {
			/*
			 * we were doing a fast forward by skipping
			 * multiple fmk at the time
			 */
			bioerror(bp, EIO);
			severity = SCSI_ERR_RECOVERED;
			rval	 = COMMAND_DONE;
		}
		goto check_keys;

	case KEY_WRITE_PROTECT:
		if (st_wrongtapetype(un)) {
			un->un_status = SUN_KEY_WRONGMEDIA;
			ST_DEBUG6(ST_DEVINFO, st_label, CE_WARN,
		"wrong tape for writing- use DC6150 tape (or equivalent)\n");
			severity = SCSI_ERR_UNKNOWN;
		} else {
			severity = SCSI_ERR_FATAL;
		}
		rval = COMMAND_DONE_ERROR;
		bioerror(bp, EACCES);
		break;

	case KEY_UNIT_ATTENTION:
		ST_DEBUG6(ST_DEVINFO, st_label, CE_WARN,
		    "KEY_UNIT_ATTENTION : un_state = %d\n", un->un_state);

		if ((un->un_state == ST_STATE_OFFLINE) ||
		    (un->un_state == ST_STATE_INITIALIZING)) {
			/* media change UA, so toss it and redo it */
			rval = QUE_COMMAND;
		} else if (un->un_laststate <= ST_STATE_OPENING &&
		    un->un_lastop == ST_OP_CTL) {
			severity = SCSI_ERR_INFO;
			rval = COMMAND_DONE_ERROR;
		} else {
			/*
			 * do not print Unit Attention messages; if ARQ is
			 * disabled, the first access will result in this
			 * useless message
			 */
			severity = SCSI_ERR_INFO;
			rval = COMMAND_DONE_ERROR;
		}
		un->un_fileno = -1;

		break;

	case KEY_NOT_READY:
		/*
		 * not much we can do here; a load command is
		 * undesirable
		 */
		severity = SCSI_ERR_INFO;
		rval = COMMAND_DONE_ERROR;
		break;

	case KEY_ABORTED_COMMAND:

		/*
		 * Probably a parity error...
		 * if we retry here then this may cause data to be
		 * written twice or data skipped during reading
		 */
		severity = SCSI_ERR_FATAL;
		rval = COMMAND_DONE_ERROR;
		goto check_keys;

	default:
		/*
		 * Undecoded sense key.	 Try retries and hope
		 * that will fix the problem.  Otherwise, we're
		 * dead.
		 */
		ST_DEBUG6(ST_DEVINFO, st_label, CE_WARN,
		    "Unhandled Sense Key '%s'\n",
		    sense_keys[un->un_status]);
		if ((int)un->un_retry_ct++ < st_retry_count) {
			severity = SCSI_ERR_RETRYABLE;
			rval = QUE_COMMAND;
		} else {
			severity = SCSI_ERR_FATAL;
			rval = COMMAND_DONE_ERROR;
		}
		goto check_keys;
	}

	if ((un->un_state >= ST_STATE_OPEN) &&
	    (DEBUGGING ||
		(un->un_laststate > ST_STATE_OPENING &&
		    severity >= st_error_level))) {
		scsi_errmsg(ST_SCSI_DEVP, pkt, st_label, severity,
		    un->un_blkno, un->un_err_blkno, scsi_cmds, sensep);
		if (sensep->es_filmk) {
			scsi_log(ST_DEVINFO, st_label, CE_CONT,
			    "File Mark Detected\n");
		}
		if (sensep->es_eom) {
			scsi_log(ST_DEVINFO, st_label, CE_CONT,
			    "End-of-Media Detected\n");
		}
		if (sensep->es_ili) {
			scsi_log(ST_DEVINFO, st_label, CE_CONT,
			    "Incorrect Length Indicator Set\n");
		}
	}
	return (rval);
}

static int
st_wrongtapetype(struct scsi_tape *un)
{

	ASSERT(mutex_owned(ST_MUTEX));

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
		"st_wrongtapetype()\n");

	/*
	 * Hack to handle  600A, 600XTD, 6150 && 660 vs. 300XL tapes...
	 */
	if (un->un_dp && (un->un_dp->options & ST_QIC) && un->un_mspl) {
		switch (un->un_dp->type) {
		case ST_TYPE_WANGTEK:
		case ST_TYPE_ARCHIVE:
			/*
			 * If this really worked, we could go off of
			 * the density codes set in the modesense
			 * page. For this drive, 0x10 == QIC-120,
			 * 0xf == QIC-150, and 0x5 should be for
			 * both QIC-24 and, maybe, QIC-11. However,
			 * the h/w doesn't do what the manual says
			 * that it should, so we'll key off of
			 * getting a WRITE PROTECT error AND wp *not*
			 * set in the mode sense information.
			 */
			/*
			 * XXX but we already know that status is
			 * write protect, so don't check it again.
			 */

			if (un->un_status == KEY_WRITE_PROTECT &&
			    un->un_mspl->wp == 0) {
				return (1);
			}
			break;
		default:
			break;
		}
	}
	return (0);
}

static int
st_check_error(struct scsi_tape *un, struct scsi_pkt *pkt)
{
	struct buf *bp = un->un_runqf;
	register action;

	ASSERT(pkt == BP_PKT(bp));
	ASSERT(mutex_owned(ST_MUTEX));

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG, "st_check_error()\n");

	if (SCBP(pkt)->sts_busy) {
		ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG, "unit busy\n");
		if ((int)un->un_retry_ct++ < st_retry_count) {
			action = QUE_BUSY_COMMAND;
		} else {
			ST_DEBUG2(ST_DEVINFO, st_label, CE_WARN,
			    "unit busy too long\n");
			mutex_exit(ST_MUTEX);
			if (scsi_reset(ROUTE, RESET_TARGET) == 0) {
				(void) scsi_reset(ROUTE, RESET_ALL);
			}
			mutex_enter(ST_MUTEX);
			action = COMMAND_DONE_ERROR;
		}
	} else if (SCBP(pkt)->sts_chk) {
		/*
		 * we should only get here if the auto rqsense failed
		 * thru a uscsi cmd without autorequest sense
		 * so we just try again
		 */
		action = QUE_SENSE;
	} else {
		action = COMMAND_DONE;
	}
	return (action);
}

static void
st_calc_bnum(struct scsi_tape *un, struct buf *bp)
{
	register int n;

	ASSERT(mutex_owned(ST_MUTEX));

	if (un->un_bsize == 0) {
		n = ((bp->b_bcount - bp->b_resid  == 0) ? 0 : 1);
		un->un_kbytes_xferred += (bp->b_bcount - bp->b_resid)/1000;
	} else {
		n = ((bp->b_bcount - bp->b_resid) / un->un_bsize);
	}
	un->un_blkno += n;
}

static void
st_set_state(struct scsi_tape *un)
{
	struct buf *bp = un->un_runqf;
	struct scsi_pkt *sp = BP_PKT(bp);
	struct uscsi_cmd *ucmd;

	ASSERT(mutex_owned(ST_MUTEX));

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_set_state(): un_eof=%x	fmneeded=%x  pkt_resid=0x%x (%d)\n",
		un->un_eof, un->un_fmneeded, sp->pkt_resid, sp->pkt_resid);

	if (bp != un->un_sbufp) {
#ifdef STDEBUG
		if (DEBUGGING && sp->pkt_resid) {
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "pkt_resid %d bcount %d\n",
			    sp->pkt_resid, bp->b_bcount);
		}
#endif
		bp->b_resid = sp->pkt_resid;
		st_calc_bnum(un, bp);
		if (bp->b_flags & B_READ) {
			un->un_lastop = ST_OP_READ;
			un->un_fmneeded = 0;
		} else {
			un->un_lastop = ST_OP_WRITE;
			if (un->un_dp->options & ST_REEL) {
				un->un_fmneeded = 2;
			} else {
				un->un_fmneeded = 1;
			}
		}
	} else {
		char saved_lastop = un->un_lastop;

		un->un_lastop = ST_OP_CTL;

		switch ((int)bp->b_forw) {
		case SCMD_WRITE:
			bp->b_resid = sp->pkt_resid;
			un->un_lastop = ST_OP_WRITE;
			st_calc_bnum(un, bp);
			if (un->un_dp->options & ST_REEL) {
				un->un_fmneeded = 2;
			} else {
				un->un_fmneeded = 1;
			}
			break;
		case SCMD_READ:
			bp->b_resid = sp->pkt_resid;
			un->un_lastop = ST_OP_READ;
			st_calc_bnum(un, bp);
			un->un_fmneeded = 0;
			break;
		case SCMD_WRITE_FILE_MARK:
			un->un_eof = ST_NO_EOF;
			un->un_lastop = ST_OP_WEOF;
			un->un_fileno += (bp->b_bcount - bp->b_resid);
			un->un_blkno = 0;
			if (un->un_dp->options & ST_REEL) {
				un->un_fmneeded -=
					(bp->b_bcount - bp->b_resid);
				if (un->un_fmneeded < 0) {
					un->un_fmneeded = 0;
				}
			} else {
				un->un_fmneeded = 0;
			}

			break;
		case SCMD_REWIND:
			un->un_eof = ST_NO_EOF;
			un->un_fileno = 0;
			un->un_blkno = 0;
			break;

		case SCMD_SPACE:
		{
			int space_fmk, count, resid;

			count = space_cnt(bp->b_bcount);
			resid = space_cnt(bp->b_resid);
			space_fmk = ((bp->b_bcount) & (1<<24)) ? 1 : 0;


			if (count >= 0) {
				if (space_fmk) {
					if (un->un_eof <= ST_EOF) {
						un->un_eof = ST_NO_EOF;
					}
					un->un_fileno += (count - resid);
					un->un_blkno = 0;
				} else {
					un->un_blkno += count - resid;
				}
			} else if (count < 0) {
				if (space_fmk) {
					un->un_fileno -=
					    ((-count) - resid);
					if (un->un_fileno < 0) {
						un->un_fileno = 0;
						un->un_blkno = 0;
					} else {
						un->un_blkno = INF;
					}
				} else {
					if (un->un_eof >= ST_EOF_PENDING) {
					/*
					 * we stepped back into
					 * a previous file; we are not
					 * making an effort to pretend that
					 * we are still in the current file
					 * ie. logical == physical position
					 * and leave it to st_ioctl to correct
					 */
						if (un->un_fileno > 0) {
							un->un_fileno--;
							un->un_blkno = INF;
						} else {
							un->un_blkno = 0;
						}
					} else {
						un->un_blkno -=
						    (-count) - resid;
					}
				}
			}
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "aft_space rs %d fil %d blk %d\n",
			    resid, un->un_fileno, un->un_blkno);
			break;
		}
		case SCMD_LOAD:
			if (bp->b_bcount&0x1) {
				un->un_fileno = 0;
			} else {
				un->un_state = ST_STATE_OFFLINE;
				un->un_fileno = -1;
			}
			un->un_density_known = 0;
			un->un_eof = ST_NO_EOF;
			un->un_blkno = 0;
			break;
		case SCMD_ERASE:
			un->un_eof = ST_NO_EOF;
			un->un_blkno = 0;
			un->un_fileno = 0;
			break;
		case SCMD_MODE_SELECT:
		case SCMD_WRITE_BUFFER:
		case SCMD_INQUIRY:
		case SCMD_MODE_SENSE:
		case SCMD_READ_BLKLIM:
		case SCMD_TEST_UNIT_READY:
			un->un_lastop = saved_lastop;
			break;
		default:
			if ((ucmd = BP_UCMD(bp)) != 0) {
				if (ucmd->uscsi_flags & USCSI_SILENT) {
					break;
				}
			}
			ST_DEBUG2(ST_DEVINFO, st_label, CE_WARN,
			    "unknown cmd causes loss of state\n");
			un->un_fileno = -1;
			break;
		}
	}

	un->un_err_resid = bp->b_resid;
	un->un_err_fileno = un->un_fileno;
	un->un_err_blkno = un->un_blkno;
	un->un_retry_ct = 0;

	/*
	 * If we've seen a filemark via the last read operation
	 * advance the file counter, but mark things such that
	 * the next read operation gets a zero count. We have
	 * to put this here to handle the case of sitting right
	 * at the end of a tape file having seen the file mark,
	 * but the tape is closed and then re-opened without
	 * any further i/o. That is, the position information
	 * must be updated before a close.
	 */

	if (un->un_lastop == ST_OP_READ && un->un_eof == ST_EOF_PENDING) {
		/*
		 * If we're a 1/2" tape, and we get a filemark
		 * right on block 0, *AND* we were not in the
		 * first file on the tape, and we've hit logical EOM.
		 * We'll mark the state so that later we do the
		 * right thing (in st_close(), st_strategy() or
		 * st_ioctl()).
		 *
		 */
		if ((un->un_dp->options & ST_REEL) &&
		    un->un_blkno == 0 && un->un_fileno > 0) {
			un->un_eof = ST_EOT_PENDING;
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "eot pending\n");
			un->un_fileno++;
			un->un_blkno = 0;
		} else if (BSD_BEHAVIOR) {
			/*
			 * If the read of the filemark was a side effect
			 * of reading some blocks (i.e., data was actually
			 * read), then the EOF mark is pending and the
			 * bump into the next file awaits the next read
			 * operation (which will return a zero count), or
			 * a close or a space operation, else the bump
			 * into the next file occurs now.
			 */
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "resid=%x, bcount=%x\n",
				bp->b_resid, bp->b_bcount);
			if (bp->b_resid != bp->b_bcount) {
				un->un_eof = ST_EOF;
			} else {
				un->un_eof = ST_NO_EOF;
				un->un_fileno++;
				un->un_blkno = 0;
			}
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "eof of file %d, un_eof=%d\n",
			    un->un_fileno, un->un_eof);
		} else if (SVR4_BEHAVIOR) {
			/*
			 * If the read of the filemark was a side effect
			 * of reading some blocks (i.e., data was actually
			 * read), then the next read should return 0
			 */
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "resid=%x, bcount=%x\n",
			    bp->b_resid, bp->b_bcount);
			if (bp->b_resid == bp->b_bcount) {
				un->un_eof = ST_EOF;
			}
			ST_DEBUG6(ST_DEVINFO, st_label, SCSI_DEBUG,
			    "eof of file=%d, un_eof=%d\n",
			    un->un_fileno, un->un_eof);
		}
	}
}


static void
st_clean_print(dev_info_t *dev, char *label, u_int level,
	char *title, char *data, int len)
{
	int	i;
	char	buf[256];

	sprintf(buf, "%s: ", title);
	for (i = 0; i < len; i++) {
		sprintf(&buf[strlen(buf)], "0x%x ", (data[i] & 0xff));
	}
	sprintf(&buf[strlen(buf)], "\n");

	scsi_log(dev, label, level, "%s", buf);
}

/*
 * Conditionally enabled debugging
 */
#ifdef	STDEBUG
static void
st_debug_cmds(struct scsi_tape *un, int com, int count, int wait)
{
	char tmpbuf[64];

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "cmd=%s count=0x%x (%d)	 %ssync\n",
	    scsi_cmd_name(com, scsi_cmds, tmpbuf),
	    count, count,
	    wait == ASYNC_CMD ? "a" : "");
}

#endif	/* STDEBUG */

/*
 * Soft error reporting, so far unique to each drive
 *
 * Currently supported: exabyte and DAT soft error reporting
 */
static int
st_report_exabyte_soft_errors(dev_t dev, int flag)
{
	u_char *sensep;
	int amt;
	int rval = 0;
	char cdb[CDB_GROUP0], *c = cdb;
	register struct uscsi_cmd *com;

	GET_SOFT_STATE(dev);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_report_exabyte_soft_errors(dev = 0x%x, flag = %d)\n",
	    dev, flag);

	ASSERT(mutex_owned(ST_MUTEX));

	com = kmem_zalloc(sizeof (* com), KM_SLEEP);
	sensep = kmem_zalloc(TAPE_SENSE_LENGTH, KM_SLEEP);

	*c++ = SCMD_REQUEST_SENSE;
	*c++ = 0;
	*c++ = 0;
	*c++ = 0;
	*c++ = TAPE_SENSE_LENGTH;
	/*
	 * set CLRCNT (byte 5, bit 7 which clears the error counts)
	 */
	*c   = (char)0x80;

	com->uscsi_cdb = cdb;
	com->uscsi_cdblen = CDB_GROUP0;
	com->uscsi_bufaddr = (caddr_t)sensep;
	com->uscsi_buflen = TAPE_SENSE_LENGTH;
	com->uscsi_flags = USCSI_DIAGNOSE | USCSI_SILENT | USCSI_READ;
	com->uscsi_timeout = 10;

	rval = st_ioctl_cmd(dev, com, UIO_SYSSPACE, UIO_SYSSPACE,
		UIO_SYSSPACE);
	if (rval || com->uscsi_status) {
		goto done;
	}

	/*
	 * was there enough data?
	 */
	amt = TAPE_SENSE_LENGTH - com->uscsi_resid;

	if ((amt >= 19) && un->un_kbytes_xferred) {
		u_int count, error_rate;
		u_int small_rate, large_rate, rate;

		count = sensep[16] << 16 | sensep[17] << 8 | sensep[18];
		count &= 0xffffff;
		error_rate = (count * 100)/un->un_kbytes_xferred;

		if (sensep[21] & CLN) {
			scsi_log(ST_DEVINFO, st_label, CE_WARN,
			    "Periodic head cleaning required");
		}

#ifdef	STDEBUG
		if (st_soft_error_report_debug) {
			scsi_log(ST_DEVINFO, st_label, CE_NOTE,
			    "Exabyte Soft Error Report:\n");
			scsi_log(ST_DEVINFO, st_label, CE_CONT,
			    "read/write error counter: %d\n", count);
			scsi_log(ST_DEVINFO, st_label, CE_CONT,
			    "number of bytes transferred: %dK\n",
				un->un_kbytes_xferred);
			scsi_log(ST_DEVINFO, st_label, CE_CONT,
			    "error_rate: %d%%\n", error_rate);

			if (amt >= 22) {
				scsi_log(ST_DEVINFO, st_label, CE_CONT,
				    "unit sense: 0x%b 0x%b 0x%b\n",
				    sensep[19], SENSE_19_BITS,
				    sensep[20], SENSE_20_BITS,
				    sensep[21], SENSE_21_BITS);
			}
			if (amt >= 27) {
				scsi_log(ST_DEVINFO, st_label, CE_CONT,
				    "tracking retry counter: %d\n",
				    sensep[26]);
				scsi_log(ST_DEVINFO, st_label, CE_CONT,
				    "read/write retry counter: %d\n",
				    sensep[27]);
			}
		}
#endif

		if (flag & FWRITE) {
			small_rate = EXABYTE_SMALL_WRITE_ERROR_THRESHOLD;
			large_rate = EXABYTE_LARGE_WRITE_ERROR_THRESHOLD;
		} else {
			small_rate = EXABYTE_SMALL_READ_ERROR_THRESHOLD;
			large_rate = EXABYTE_LARGE_READ_ERROR_THRESHOLD;
		}

		rate = (un->un_kbytes_xferred <
		    SOFT_ERROR_WARNING_THRESHOLD) ? small_rate : large_rate;

		if (error_rate >= rate) {
			scsi_log(ST_DEVINFO, st_label, CE_WARN,
			    "Soft error rate (%d%%) during %s was too high",
			    error_rate,
			    ((flag & FWRITE) ? "writing" : "reading"));
			scsi_log(ST_DEVINFO, st_label, CE_CONT,
			    "Please, replace tape cartridge\n");
		}
	}

done:
	kmem_free((caddr_t)com, sizeof (*com));
	kmem_free((caddr_t)sensep, TAPE_SENSE_LENGTH);

	if (rval) {
		scsi_log(ST_DEVINFO, st_label, CE_WARN,
		    "exabyte soft error reporting failed\n");
	}
	return (rval);
}

/*
 * this is very specific to Archive 4mm dat
 */
#define	ONEGIG	(1024 * 1024 * 1024)

static int
st_report_dat_soft_errors(dev_t dev, int flag)
{
	u_char *sensep;
	int amt, i;
	int rval = 0;
	char cdb[CDB_GROUP1], *c = cdb;
	register struct uscsi_cmd *com;

	GET_SOFT_STATE(dev);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_report_dat_soft_errors(dev = 0x%x, flag = %d)\n", dev, flag);

	ASSERT(mutex_owned(ST_MUTEX));

	com = kmem_zalloc(sizeof (* com), KM_SLEEP);
	sensep = kmem_zalloc(LOG_SENSE_LENGTH, KM_SLEEP);

	*c++ = LOG_SENSE_CMD;
	*c++ = 0;
	*c++ = (flag & FWRITE) ? 0x42 : 0x43;
	*c++ = 0;
	*c++ = 0;
	*c++ = 0;
	*c++ = 2;
	*c++ = 0;
	*c++ = (char)LOG_SENSE_LENGTH;
	*c   = 0;

	com->uscsi_cdb = cdb;
	com->uscsi_cdblen = CDB_GROUP1;
	com->uscsi_bufaddr = (caddr_t)sensep;
	com->uscsi_buflen = LOG_SENSE_LENGTH;
	com->uscsi_flags = USCSI_DIAGNOSE | USCSI_SILENT | USCSI_READ;
	com->uscsi_timeout = 10;

	rval = st_ioctl_cmd(dev, com, UIO_SYSSPACE, UIO_SYSSPACE,
		UIO_SYSSPACE);
	if (rval || com->uscsi_status) {
		goto done;
	}

	/*
	 * was there enough data?
	 */
	amt = LOG_SENSE_LENGTH - com->uscsi_resid;

	if ((amt >= MIN_LOG_SENSE_LENGTH) && un->un_kbytes_xferred) {
		register int total, retries, param_code;
		long long error_rate;

		total = -1;
		retries = -1;
		amt = sensep[3] + 4;

#ifdef STDEBUG
		if (st_soft_error_report_debug) {
			printf("logsense:");
			for (i = 0; i < MIN_LOG_SENSE_LENGTH; i++) {
				if (i % 16 == 0) {
					printf("\t\n");
				}
				printf(" %x", sensep[i]);
			}
			printf("\n");
		}
#endif

		/*
		 * parse the param_codes
		 */
		if (sensep[0] == 2 || sensep[0] == 3) {
			for (i = 4; i < amt; i++) {
				param_code = (sensep[i++] << 8);
				param_code += sensep[i++];
				i++; /* skip control byte */
				if (param_code == 5) {
					if (sensep[i++] == 4) {
						total = (sensep[i++] << 24);
						total += (sensep[i++] << 16);
						total += (sensep[i++] << 8);
						total += sensep[i];
					}
				} else if (param_code == 0x8007) {
					if (sensep[i++] == 2) {
						retries = sensep[i++] << 8;
						retries += sensep[i];
					}
				} else {
					i += sensep[i];
				}
			}
		}

		/*
		 * if the log sense returned valid numbers then calculate
		 * read and write error rate.
		 * for writing, we allow about DAT_WRITE_ERROR_THRESHOLD errors
		 * per 1 GB. For example, if DAT_WRITE_ERROR_THRESHOLD is
		 * 50, we allow 200 errors per 4GB.
		 *
		 * retries_allowed = error_rate * (total GB xferred)
		 *
		 * for reading, we allow  DAT_READ_ERROR_THRESHOLD errors
		 * regardless of the transfer size
		 */
		if (total > 0 && retries > 0) {
			long long small_rate, large_rate, rate;

			error_rate = ((long long)retries * ONEGIG)/total;
			ST_DEBUG4(ST_DEVINFO, st_label, SCSI_DEBUG,
			"total xferred (%s) =%x, retries=%x, error_rate=%x\n",
				((flag & FWRITE) ? "writing" : "reading"),
				total, retries, (int)error_rate);


			if (flag & FWRITE) {
				small_rate = DAT_SMALL_WRITE_ERROR_THRESHOLD;
				large_rate = DAT_LARGE_WRITE_ERROR_THRESHOLD;
			} else {
				small_rate = DAT_SMALL_READ_ERROR_THRESHOLD;
				large_rate = DAT_LARGE_READ_ERROR_THRESHOLD;
			}

			rate = (un->un_kbytes_xferred <
			    SOFT_ERROR_WARNING_THRESHOLD) ?
				small_rate : large_rate;

			if (error_rate >= rate) {
				scsi_log(ST_DEVINFO, st_label, CE_WARN,
				    "Soft error rate (retries = %d) during "
				    "%s was too high",  retries,
				    ((flag & FWRITE) ? "writing" : "reading"));
				scsi_log(ST_DEVINFO, st_label, CE_CONT,
				    "Periodic head cleaning required "
				    "and/or replace tape cartridge\n");
			}
		} else if (total == -1 || retries == -1) {
			scsi_log(ST_DEVINFO, st_label, CE_WARN,
			    "log sense parameter code does not make sense\n");
		}
	}

	/*
	 * reset all values
	 */
	c = cdb;
	*c++ = LOG_SELECT_CMD;
	*c++ = 2;	/* this resets all values */
	*c++ = (char)0xc0;
	*c++ = 0;
	*c++ = 0;
	*c++ = 0;
	*c++ = 0;
	*c++ = 0;
	*c++ = 0;
	*c   = 0;

	rval = st_ioctl_cmd(dev, com, UIO_SYSSPACE, UIO_SYSSPACE,
		UIO_SYSSPACE);

done:
	kmem_free((caddr_t)com, sizeof (*com));
	kmem_free((caddr_t)sensep, LOG_SENSE_LENGTH);

	if (rval) {
		scsi_log(ST_DEVINFO, st_label, CE_WARN,
		    "DAT soft error reporting failed\n");
	}
	return (rval);
}



static int
st_report_soft_errors(dev_t dev, int flag)
{
	GET_SOFT_STATE(dev);

	ST_DEBUG3(ST_DEVINFO, st_label, SCSI_DEBUG,
	    "st_report_soft_errors(dev = 0x%x, flag = %d)\n", dev, flag);

	ASSERT(mutex_owned(ST_MUTEX));

	switch (un->un_dp->type) {
	case ST_TYPE_EXB8500:
	case ST_TYPE_EXABYTE:
		return (st_report_exabyte_soft_errors(dev, flag));
		/*NOTREACHED*/
	case ST_TYPE_PYTHON:
		return (st_report_dat_soft_errors(dev, flag));
		/*NOTREACHED*/
	default:
		return (-1);
	}
}
