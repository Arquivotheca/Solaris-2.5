/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)dbri_driver.c	1.38	94/03/31 SMI"

/*
 * SunOS STREAMS DBRI (AT&T T5900FC) driver - kernel interface module
 */

#include <sys/types.h>
#include <sys/machtypes.h>
#include <sys/modctl.h>
#include <sys/conf.h>
#include <sys/kmem.h>
#include <sys/stat.h>
#include <sys/debug.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>

#include <sys/audioio.h>
#include <sys/audiovar.h>
#include <sys/isdnio.h>
#include <sys/dbriio.h>
#include <sys/dbrireg.h>
#include <sys/dbrivar.h>
#include <sys/audiodebug.h>

/*
 * Global variables
 */
void *Dbri_state;		/* SunDDI soft state pointer */
extern aud_ops_t dbri_audio_ops; /* virtual function table */
clock_t Dbri_mshz;		/* ms per clock tick */


/*
 * Function prototypes
 */
static int dbri_identify(dev_info_t *);
static int dbri_attach(dev_info_t *, ddi_attach_cmd_t);
static int dbri_detach(dev_info_t *, ddi_detach_cmd_t);
static int dbri_info(dev_info_t *, ddi_info_cmd_t, void *, void **);
static int dbri_prop_op(dev_t, dev_info_t *, ddi_prop_op_t, int,
	    char *, caddr_t, int *);
static boolean_t dbri_initunit(dbri_unit_t *);

/*
 * STREAMS data structures
 */
static struct module_info dbri_minfo = {
	DBRI_IDNUM,		/* mi_idum */
	DBRI_NAME,		/* mi_idname */
	DBRI_MINPSZ,		/* mi_minpsz */
	DBRI_MAXPSZ,		/* mi_maxpsz */
	DBRI_HIWAT,		/* mi_hiwat */
	DBRI_LOWAT,		/* mi_lowat */
};

static struct qinit dbri_rinit = {
	audio_rput,		/* qi_putp */
	audio_rsrv,		/* qi_srvp */
	dbri_open,		/* qi_qopen */
	audio_close,		/* qi_qclose */
	NULL,			/* qi_qadmin */
	&dbri_minfo,		/* qi_minfo */
	NULL,			/* qi_mstat */
};

static struct qinit dbri_winit = {
	audio_wput,		/* qi_putp */
	audio_wsrv,		/* qi_srvp */
	NULL,			/* qi_qopen */
	NULL,			/* qi_qclose */
	NULL,			/* qi_qadmin */
	&dbri_minfo,		/* qi_minfo */
	NULL,			/* qi_mstat */
};

static struct streamtab dbri_sinfo = {
	&dbri_rinit,		/* st_rdinit */
	&dbri_winit,		/* st_wrinit */
	NULL,			/* st_muxrdinit */
	NULL,			/* st_muxwrinit */
};


static 	struct cb_ops cb_dbri_ops = {
	nulldev,		/* cb_open */
	nulldev,		/* cb_close */
	nodev,			/* cb_strategy */
	nodev,			/* cb_print */
	nodev,			/* cb_dump */
	nodev,			/* cb_read */
	nodev,			/* cb_write */
	nodev,			/* cb_ioctl */
	nodev,			/* cb_devmap */
	nodev,			/* cb_mmap */
	nodev,			/* cb_segmap */
	nochpoll,		/* cb_chpoll */
	dbri_prop_op,		/* cb_prop_op */
	&dbri_sinfo,		/* cb_stream */
	(int)(D_NEW | D_MP)	/* cb_flag */
};

struct dev_ops dbri_ops = {
	DEVO_REV,		/* devo_rev */
	0,			/* devo_refcnt */
	dbri_info,		/* devo_getinfo */
	dbri_identify,		/* devo_identify */
	nulldev,		/* devo_probe */
	dbri_attach,		/* devo_attach */
	dbri_detach,		/* devo_detach */
	nodev,			/* devo_reset */
	&(cb_dbri_ops),		/* devo_cb_ops */
	(struct bus_ops *)NULL,	/* devo_bus_ops */
	ddi_power		/* devo_power */
};

/*
 * The DBRI chip can support full 32-bit DMA addresses
 *
 * XXX - Check burstsizes, speed, and counter
 */
ddi_dma_lim_t dbri_dma_limits = {
	(ulong_t)0x00000000,	/* dlim_addr_lo */
	(ulong_t)0xffffffff,	/* dlim_addr_hi */
	(uint_t)-1,		/* dlim_cntr_max */
	(uint_t)0x74,		/* dlim_burstsizes (1,4,8,16) */
	0x04,			/* dlim_minxfer */
	1024,			/* dlim_speed */
};


/*
 * Loadable module wrapper
 */
#include <sys/modctl.h>

extern struct mod_ops mod_driverops;

/*
 * This driver requires that the Device-Independent Audio routines
 * be loaded in.
 */
char _depends_on[] = "misc/diaudio";

int Dbri_keepcnt = 0;	/* XXX - 0 means unloadable */

static struct modldrv modldrv = {
	&mod_driverops,		/* Type of module (driver) */
	"DBRI Audio/ISDN Driver",
	&dbri_ops
};

/*
 * Module linkage information for the kernel
 */
static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modldrv, NULL
};


int
_init(void)
{
	int e;

	/*
	 * Initialize our global variable which contains the number
	 * of ms per clock tick.  This variable is used by the TICKS
	 * macro which converts milliseconds to clock ticks for use
	 * by timeout.  If the system clock ever reaches 1kHz or more,
	 * this needs to be changed.
	 */
	Dbri_mshz = 1000/HZ;

	/*
	 * Normal driver initialization
	 */
	if (e = ddi_soft_state_init(&Dbri_state, sizeof (dbri_unit_t), 2))
		return (e);

	if (e = mod_install(&modlinkage))
		ddi_soft_state_fini(&Dbri_state);

	return (e);
}


int
_fini(void)
{
	int e;

	if ((e = mod_remove(&modlinkage)) != 0)
		return (e);

	/*
	 * Clean up our soft state
	 */
	ddi_soft_state_fini(&Dbri_state);

	return (e);
}


int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}


/*ARGSUSED*/
static int
dbri_info(dev_info_t *dip, ddi_info_cmd_t cmd, void *arg, void **result)
{
	dbri_unit_t *unitp;
	int ret;
	int instance;
	dev_t dev = (dev_t)arg;

	instance = MINORUNIT(dev);

	switch (cmd) {
	case DDI_INFO_DEVT2DEVINFO:
		unitp = (dbri_unit_t *)ddi_get_soft_state(Dbri_state,
		    instance);
		if (unitp == NULL)
			return (DDI_FAILURE);
		*result = (void *)unitp->dip;
		ret = DDI_SUCCESS;
		break;

	case DDI_INFO_DEVT2INSTANCE:
		*result = (void *)instance;
		ret = DDI_SUCCESS;
		break;

	default:
		ret = DDI_FAILURE;
	}

	return (ret);
}


/*
 * DBRI Identify routine
 *
 * NB: there is no guarantee of how many times this routine is called or
 * when it is called, so do not maintain counters of the number of
 * devices here.
 */
static int
dbri_identify(dev_info_t *dip)
{
	if (strncmp(ddi_get_name(dip), "SUNW,DBRI", 9) == 0)
		return (DDI_IDENTIFIED);
	else
		return (DDI_NOT_IDENTIFIED);
}


/*
 * dbri_attach - DBRI attach routine.  This only sets up the kernel-side
 * of things.  The real DBRI setup is done in dbri_initunit().
 */
static int
dbri_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	dbri_unit_t *unitp;	/* DBRI instance state */
	dbri_reg_t *regsp;	/* DBRI registers */
	int instance;		/* SunDDI instance number */
	char version;		/* DBRI version letter */
	boolean_t intr_added;	/* whether or not we've added the interrupt */
	int power[3];		/* For adding power property */

	instance = ddi_get_instance(dip);

	switch (cmd) {
	case DDI_ATTACH:
		break;

	case DDI_RESUME:
		if (!(unitp = ddi_get_soft_state(Dbri_state, instance)))
			return (DDI_FAILURE);
		LOCK_UNIT(unitp);
		if (!unitp->suspended) {
			UNLOCK_UNIT(unitp);
			return (DDI_SUCCESS);
		}
		dbri_initchip(unitp);
		unitp->suspended = B_FALSE;
		unitp->openinhibit = B_FALSE;
		UNLOCK_UNIT(unitp);
		return (DDI_SUCCESS);

	default:
		return (DDI_FAILURE);
	}

	ATRACEINIT();

	intr_added = B_FALSE;
	regsp = NULL;
	unitp = NULL;

	/*
	 * DBRI must be a bus master to work properly so ensure it is not
	 * installed in a slave-only S-Bus slot.
	 *
	 * XXX - This test can find an error where none exists.  See
	 * bugid# 1090158.
	 */
	if (ddi_slaveonly(dip) == 0) {
		cmn_err(CE_WARN, "A DBRI device is installed in a slave-only ");
		cmn_err(CE_CONT, "S-Bus slot.\n");
		return (DDI_FAILURE);
	}

	/*
	 * Allocate and initialize our per-unit state information
	 */
	if (ddi_soft_state_zalloc(Dbri_state, instance) != DDI_SUCCESS)
		return (DDI_FAILURE);

	unitp = (dbri_unit_t *)ddi_get_soft_state(Dbri_state, instance);

	unitp->instance = instance;
	unitp->dip = dip;

	/*
	 * Get DBRI version letter to determine whether or not we will
	 * accept the device
	 */
	version = (ddi_get_name(dip))[9]; /* this looks disgusting */
	switch (version) {
	case 'a':		/* Way way too old... */
	case 'b':		/* Way too old... */
	case 'c':		/* Too old... */
		cmn_err(CE_WARN, "DBRI revision \"%c\" is obsolete", version);
		goto failed;

	case 'd':		/* We'll accept it, but we don't like it */
		cmn_err(CE_WARN, "DBRI revision \"%c\" needs to be upgraded",
		    version);
		break;

	case 'e':		/* This one makes us happiest (so far) */
	case 'f':		/* This one might make us smile */
		break;

	default:
		cmn_err(CE_WARN, "DBRI revision \"%c\" device is unexpected",
		    version);
		goto failed;
	}
	unitp->version = version;

	unitp->isdn_disabled = (ddi_getprop(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, "isdn-disabled", -1) == 1) ? B_TRUE : B_FALSE;

#if defined(DEBUG)
	if (unitp->isdn_disabled) {
		cmn_err(CE_CONT,
		    "?ISDN disabled on instance #%d\n", unitp->instance);
	}
#endif

	/*
	 * Map in the registers
	 */
	if (ddi_map_regs(dip, 0, (caddr_t *)&regsp, 0, 0) != 0) {
		cmn_err(CE_WARN, "DBRI driver could not map in registers");
		goto failed;
	}
	unitp->regsp = regsp;

	/*
	 * Install interrupt handler
	 */
	if (ddi_add_intr(dip, 0, &unitp->icookie, NULL, dbri_intr,
	    (caddr_t)unitp) != DDI_SUCCESS) {
		cmn_err(CE_WARN, "DBRI driver could not install interrupt ");
		cmn_err(CE_CONT, "routine.\n");
		goto failed;
	}
	intr_added = B_TRUE;

	/*
	 * Initialize the mutex *after* getting a valid icookie
	 */
	mutex_init(&unitp->lock, "DBRI per-unit lock", MUTEX_DRIVER,
	    unitp->icookie);

	/*
	 * Finish initializing the unit structure.
	 */
	if (!dbri_initunit(unitp))
		goto failed;

	ddi_report_dev(dip);
	/* Add power managemnt properties */
	unitp->timestamp[0] = 0;		/* Chips are busy */
	drv_getparm(TIME, unitp->timestamp+1);
	unitp->timestamp[2] = unitp->timestamp[1];
	if (ddi_prop_create(DDI_DEV_T_NONE, dip, DDI_PROP_CANSLEEP,
	    "pm_timestamp", (caddr_t)unitp->timestamp,
	    sizeof (unitp->timestamp)) != DDI_SUCCESS)
		cmn_err(CE_WARN, "dbri: Can't create pm property\n");
	power[0] = power[1] = power[2] = 1;
	if (ddi_prop_create(DDI_DEV_T_NONE, dip, DDI_PROP_CANSLEEP,
	    "pm_norm_pwr", (caddr_t)power, sizeof (power))
	    != DDI_SUCCESS)
		cmn_err(CE_WARN, "dbri: Can't create pm property\n");

	return (DDI_SUCCESS);

failed:
	if (intr_added) {
		ddi_remove_intr(dip, 0, unitp->icookie);
		mutex_destroy(&unitp->lock);
	}

	if (regsp != NULL)
		ddi_unmap_regs(dip, 0, (caddr_t *)&regsp, 0, 0);

	if (unitp != NULL)
		ddi_soft_state_free(Dbri_state, instance);

	return (DDI_FAILURE);
}


/*
 * dbri_detach - Clean up after an attach in preperation for the driver
 * being unloaded.
 */
static int
dbri_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	dbri_unit_t *unitp;
	dbri_reg_t *regsp;
	aud_stream_t *as;
	int instance;
	int i;
	dbri_stream_t	*ds;
	dbri_chan_t	dchan;

	instance = ddi_get_instance(dip);
	unitp = ddi_get_soft_state(Dbri_state, instance);
	if (unitp == NULL)
		return (DDI_FAILURE);

	switch (cmd) {
	case DDI_DETACH:
		break;

	case DDI_SUSPEND:
		LOCK_UNIT(unitp);
		if (unitp->suspended) {
			UNLOCK_UNIT(unitp);
			return (DDI_SUCCESS);
		}
		for (dchan = 0; dchan < Dbri_nstreams; dchan++) {
			ds = DChanToDs(unitp, dchan);
			if (DsDisabled(ds))
				continue;
			if (DsToAs(ds)->type != AUDTYPE_CONTROL &&
			    DsToAs(ds)->readq != NULL) {
				UNLOCK_UNIT(unitp);
				return (DDI_FAILURE);
			}
		}
		unitp->suspended = B_TRUE;
		unitp->regsp->n.sts = DBRI_STS_R;	/* reset DBRI chip */
		unitp->openinhibit = B_TRUE;
		UNLOCK_UNIT(unitp);
		return (DDI_SUCCESS);

	default:
		return (DDI_FAILURE);
	}

	unitp->openinhibit = B_TRUE; /* Just for consistency */
	regsp = unitp->regsp;

	/*
	 * If we don't reset the chip, it may do master-mode things into
	 * memory regions we're asking to have unmapped.  Resetting will
	 * remove knowledge of interrupt queues, etc., from the unit.
	 */
	regsp->n.sts = DBRI_STS_R;
	unitp->initialized = B_FALSE;

	/*
	 * Remove pending timeouts
	 */
	if (unitp->keepalive_id) {
		(void) untimeout(unitp->keepalive_id);
		unitp->keepalive_id = 0;
	}
	if (unitp->tetimer_id) {
		(void) untimeout(unitp->tetimer_id);
		unitp->tetimer_id = 0;
	}
	if (unitp->nttimer_t101_id) {
		(void) untimeout(unitp->nttimer_t101_id);
		unitp->nttimer_t101_id = 0;
	}
	if (unitp->nttimer_t102_id) {
		(void) untimeout(unitp->nttimer_t102_id);
		unitp->nttimer_t102_id = 0;
	}
	if (unitp->mmcwatchdog_id) {
		(void) untimeout(unitp->mmcwatchdog_id);
		unitp->mmcwatchdog_id = 0;
	}
	if (unitp->mmcvolume_id) {
		(void) untimeout(unitp->mmcvolume_id);
		unitp->mmcvolume_id = 0;
	}
	if (unitp->mmcdataok_id) {
		(void) untimeout(unitp->mmcdataok_id);
		unitp->mmcdataok_id = 0;
	}

	/*
	 * Remove all minor nodes for this unit
	 */
	ddi_remove_minor_node(dip, NULL);

	/*
	 * Destroy the per-unit mutex
	 */
	mutex_destroy(&unitp->lock);

	/*
	 * Clean-up each audio stream
	 */
	for (i = 0; i < Dbri_nstreams; i++) {
		as = DChanToAs(unitp, i);
		if (DsDisabled(AsToDs(as)))
			continue;	/* ignore this channel */

		/*
		 * Destroy condition variable on control and output
		 * streams
		 */
		if (as == as->control_as || as == as->output_as)
			cv_destroy(&as->cv);
	}

	/*
	 * Free allocated IOPB space and kernel memory
	 */
	ddi_dma_free(unitp->dma_handle);
	ddi_iopb_free(unitp->iopbkbase);
	kmem_free(unitp->cmdbase, unitp->cmdsize);

	/*
	 * Remove this unit's interrupt handler
	 */
	ddi_remove_intr(dip, 0, unitp->icookie);

	/*
	 * Unmap the device's registers
	 */
	ddi_unmap_regs(dip, 0, (caddr_t *)&unitp->regsp, 0, 0);

	/*
	 * And finally free the unit structure itself...
	 */
	ddi_soft_state_free(Dbri_state, instance);
	/*
	 * And the power management properties
	 */
	(void) ddi_prop_remove(DDI_DEV_T_NONE, dip, "pm_timestamp");
	(void) ddi_prop_remove(DDI_DEV_T_NONE, dip, "pm_norm_pwr");

	return (DDI_SUCCESS);
}

static int
dbri_prop_op(dev_t dev, dev_info_t *dip, ddi_prop_op_t prop_op, int mod_flags,
    char *name, caddr_t valuep, int *lengthp)
{
	dbri_unit_t	*unitp;
	int		instance;

	if (dev != DDI_DEV_T_ANY)
		instance = MINORUNIT(dev);
	else
		instance = ddi_get_instance(dip);

	unitp = ddi_get_soft_state(Dbri_state, instance);

	if (strcmp(name, "pm_timestamp") == 0 &&
	    ddi_prop_modify(DDI_DEV_T_NONE, unitp->dip, mod_flags, name,
	    (caddr_t)unitp->timestamp, sizeof (unitp->timestamp))
	    != DDI_PROP_SUCCESS)
		cmn_err(CE_WARN, "dbri_prop_op: Can't modify property");

	return (ddi_prop_op(dev, dip, prop_op, mod_flags, name, valuep,
	    lengthp));
}

/*
 * dbri_chan_maxbufs - XXX
 *
 * XXX - The number of command buffers changes per format, the rest of
 * the driver should respect this changing parameter instead of treating
 * it like a constant.  Use the max number of buffers from all legal
 * formats for this DBRI channel.
 */
int
dbri_chan_maxbufs(dbri_chantab_t  *ctep)
{
	dbri_chanformat_t *cfp;
	int maxbufs;
	int n;

	maxbufs = 0;
	for (n = 0; ctep->legal_formats[n] != NULL; ++n) {
		cfp = ctep->legal_formats[n];
		if (cfp->numbufs > maxbufs)
			maxbufs = cfp->numbufs;
	}
	return (maxbufs);
} /* dbri_chan_maxbufs */


/*
 * dbri_initunit - Initialize an instance of a DBRI chip
 */
static boolean_t
dbri_initunit(dbri_unit_t *unitp)
{
	dbri_bri_status_t *bs;
	aud_stream_t *as;
	dbri_stream_t *ds;
	int total_cmds;		/* Number of DBRI commands to allocate */
	dbri_chan_t dchan;
	dbri_chantab_t *ctep;
	boolean_t skip;

	LOCK_UNIT(unitp);

	unitp->openinhibit = B_TRUE; /* Inhibit opens on the unit */

	unitp->iopbkbase = NULL;
	unitp->cmdbase = NULL;

	/*
	 * Ensure that these timeout id's are going to cause no harm
	 * if used in an untimeout call.  Zero seems to be special-
	 * cased in untimeout (/usr/src/uts/common/os/clock.c).
	 *
	 * XXX - manual pages do not state zero is special in untimeout
	 * so this may not be a strict guarantee
	 */
	unitp->tetimer_id = 0;
	unitp->nttimer_t101_id = 0;
	unitp->nttimer_t102_id = 0;
	unitp->keepalive_id = 0;
	unitp->mmcvolume_id = 0;
	unitp->mmcwatchdog_id = 0;
	unitp->mmcdataok_id = 0;
	unitp->mmcstartup_id = 0;

	/*
	 * Initialize the device-independent state
	 */
	unitp->distate.ddstate = (void *)unitp;
	unitp->distate.monitor_gain = 0;
	unitp->distate.output_muted = B_FALSE;
	unitp->distate.ops = &dbri_audio_ops;

	/*
	 * Initialize serial status
	 */
	bzero((caddr_t)&unitp->ser_sts, sizeof (dbri_serial_status_t));

	bs = DBRI_NT_STATUS_P(unitp);
	bs->is_te = B_FALSE;
	bs->fact = B_FALSE;
	bs->dbri_f = Default_DBRI_f;
	bs->dbri_nbf = Default_DBRI_nbf; /* 2 & 3 are legal, 3 is default */
	bs->i_var.t101 = Default_T101_timer;
	bs->i_var.t102 = Default_T102_timer;
	bs->i_var.t103 = Default_T103_timer;	/* XXX TE only */
	bs->i_var.t104 = Default_T104_timer;	/* XXX TE only */
	bs->i_var.asmb = Default_asmb;
	bs->i_var.power = Default_power;

	bs = DBRI_TE_STATUS_P(unitp);
	bs->is_te = B_TRUE;
	bs->fact = B_FALSE;
	bs->dbri_f = Default_DBRI_f;
	bs->dbri_nbf = Default_DBRI_nbf; /* 2 & 3 are legal, 3 is default */
	bs->i_var.t101 = Default_T101_timer;	/* XXX NT only */
	bs->i_var.t102 = Default_T102_timer;	/* XXX NT only */
	bs->i_var.t103 = Default_T103_timer;
	bs->i_var.t104 = Default_T104_timer;
	bs->i_var.asmb = Default_asmb;
	bs->i_var.power = Default_power;

	total_cmds = 0;

	for (dchan = 0; dchan < Dbri_nstreams; dchan++) {
		ds = DChanToDs(unitp, dchan);
		as = &ds->as;

		ctep = dbri_dchan_to_ctep(dchan);

		if (ctep == NULL) {
			cmn_err(CE_WARN,
			    "dbri: bad dbri channel number: %d", dchan);
			continue;
		}

		skip = B_FALSE;
		switch (dchan) {
		case DBRI_AUDIO_IN:	/* AUDIO-related nodes */
		case DBRI_AUDIO_OUT:
		case DBRI_AUDIOCTL:
			break;

		default:		/* ISDN-related nodes */
			if (unitp->isdn_disabled)
				skip = B_TRUE;
			break;
		}
		if (skip) {
			/*
			 * For some reason we do not need to utilize
			 * this channel on the current system so skip
			 * it completely.
			 */
			bzero((caddr_t)ds, sizeof (dbri_stream_t));
			continue;
		}

		/*
		 * Create minor nodes
		 * Only /dev/isdn nodes are created here so we can
		 * skip this if isdn is disabled.  The /dev/sound
		 * nodes are a special case done afterwards.
		 */
		if (!unitp->isdn_disabled &&
		    ctep->devname != NULL &&
		    ctep->minordev != DBRI_MINOR_NODEV) {
			char name[128]; /* XXX - oh no!  a constant! */
			minor_t minornum;
			int status;

			/*
			 * Format the name and calculate the minor
			 * device number based on instance and true
			 * minor number.
			 */
			strcpy(name, ctep->devname);
			minornum = dbrimakeminor(unitp->instance,
			    ctep->minordev);

			status = ddi_create_minor_node(unitp->dip, name,
			    S_IFCHR, minornum, DDI_NT_AUDIO, 0);
			if (status == DDI_FAILURE) {
				cmn_err(CE_WARN, "dbri: Could not create ");
				cmn_err(CE_CONT, "minor node %s", name);
				ddi_remove_minor_node(unitp->dip, NULL);
				goto failed;
			}
		}

		/* XXX - right now we are using one lock per DBRI unit */
		as->lock = &unitp->lock;

		as->distate = &unitp->distate;
		as->traceq = NULL;
		ds->ctep = ctep;
		ds->dfmt = ctep->default_format;
		as->info.minordev = ctep->minordev;
		as->signals_okay = ctep->signals_okay;
		as->input_as = DChanToAs(unitp, ctep->input_ch);
		as->output_as = DChanToAs(unitp, ctep->output_ch);
		as->control_as = DChanToAs(unitp, ctep->control_ch);
		as->maxfrag_size = DBRI_MAXPSZ;
		as->info.sample_rate = ctep->default_format->samplerate;
		as->info.channels = ctep->default_format->channels;
		if (ctep->default_format->channels == 0) {
			as->info.precision = 0;
		} else {
			as->info.precision = ctep->default_format->length /
			    ctep->default_format->channels;
		}
		as->info.encoding = ctep->default_format->encoding;
		as->info.balance = AUDIO_MID_BALANCE;
		as->info.buffer_size = ctep->default_format->buffer_size;
		as->type = ctep->audtype;
		ds->pipep = NULL;
		strcpy(ds->config, ctep->config);

		/*
		 * We wait for opens using the cv in the control stream
		 * and we wait for output draining via the cv in the
		 * output stream.
		 */
		if (as == as->control_as) {
			cv_init(&as->cv, "audio open cv", CV_DRIVER,
				(void *)&unitp->icookie);
		}
		if (as == as->output_as) {
			cv_init(&as->cv, "audio drain cv", CV_DRIVER,
				(void *)&unitp->icookie);
		}

		switch (dchan) {
		case DBRI_AUDIO_OUT:
			as->info.port = AUDIO_SPEAKER;
			as->info.gain = DBRI_DEFAULT_GAIN;
			/*
			 * Include LineIn and LineOut once we determine
			 * if a SpeakerBox is connected.
			 *
			 * XXX - These should be set from properties.
			 */
			as->info.avail_ports =
			    (AUDIO_SPEAKER | AUDIO_HEADPHONE);
			break;

		case DBRI_AUDIO_IN:
			as->info.gain = DBRI_DEFAULT_GAIN;
			as->info.port = AUDIO_MICROPHONE;
			as->info.avail_ports = (AUDIO_MICROPHONE);
			break;

		default:
			as->info.gain = AUDIO_MAX_GAIN;
			as->info.avail_ports = 0;
			as->info.port = 0;
			break;
		} /* switch */

		/*
		 * XXX - The number of command buffers changes per
		 * format, the rest of the driver should respect this
		 * changing parameter instead of treating it like a
		 * constant.  Use the max number of buffers from all
		 * legal formats for this DBRI channel.
		 */
		total_cmds += dbri_chan_maxbufs(ctep);
		if (dchan == DBRI_AUDIO_OUT)
			total_cmds += dbri_chan_maxbufs(ctep);

	} /* for each dbri stream */

	/*
	 * Make the minor nodes for /dev/sound
	 */
	{
		char name[128];	/* XXX - DIC */
		minor_t minornum;
		int status;

		ctep = dbri_dchan_to_ctep(DBRI_AUDIO_OUT);
		ASSERT(ctep != NULL);
		strcpy(name, "sound,audio");
		minornum = dbrimakeminor(unitp->instance, ctep->minordev);
		status = ddi_create_minor_node(unitp->dip, name, S_IFCHR,
		    minornum, DDI_NT_AUDIO, 0);
		if (status == DDI_FAILURE) {
			cmn_err(CE_WARN, "dbri: could not create ");
			cmn_err(CE_CONT, "minor node %s", name);
			goto failed;
		}

		ctep = dbri_dchan_to_ctep(DBRI_AUDIOCTL);
		ASSERT(ctep != NULL);
		strcpy(name, "sound,audioctl");
		minornum = dbrimakeminor(unitp->instance, ctep->minordev);
		status = ddi_create_minor_node(unitp->dip, name, S_IFCHR,
		    minornum, DDI_NT_AUDIO, 0);
		if (status == DDI_FAILURE) {
			cmn_err(CE_WARN, "dbri: could not create ");
			cmn_err(CE_CONT, "minor node %s", name);
			goto failed;
		}
	}

	/*
	 * The size of the allocated IOPB space for each unit is the size
	 * of the command queue, the interrupt queue(s), and the size of
	 * the descriptors for all of the dbri streams.  There is one
	 * dbri_cmd for each message descriptor but we allocate those
	 * from normal kernel memory.  The message descriptors start on a
	 * 4-word alignment (so we need an extra 16 bytes to allow for
	 * it).
	 */
	{
		caddr_t a;	/* Allocated memory pointer */
		caddr_t c;	/* Allocated memory pointer */
		uint_t iopbsize; /* Allocated memory size */
		uint_t cmdsize;	/* Allocated memory size */
		ddi_dma_cookie_t cookie;

		iopbsize = (sizeof (dbri_cmdq_t) +
		    (DBRI_NUM_INTQS * sizeof (dbri_intq_t)) +
		    (total_cmds * sizeof (dbri_md_t)) + DBRI_MD_ALIGN +
		    (sizeof (dbri_zerobuf_t)));
		cmdsize = (total_cmds * sizeof (dbri_cmd_t));

		unitp->iopbsize = iopbsize;
		if (ddi_iopb_alloc(unitp->dip, &dbri_dma_limits,
		    iopbsize, (caddr_t *)&unitp->iopbkbase) != 0) {
			cmn_err(CE_WARN, "DBRI cannot allocate IOPB space");
			goto failed;
		}

		if (ddi_dma_addr_setup(unitp->dip, (struct as *)0,
		    (caddr_t)unitp->iopbkbase, iopbsize,
		    DDI_DMA_RDWR|DDI_DMA_CONSISTENT, DDI_DMA_DONTWAIT, 0,
		    &dbri_dma_limits, &unitp->dma_handle)) {
			cmn_err(CE_WARN, "DBRI cannot DMA map IOPB space");
			goto failed;
		}

		/*
		 * Find out what DMA burst sizes this machine supports
		 */
		unitp->ddi_burstsizes_avail =
		    ddi_dma_burstsizes(unitp->dma_handle);

		if (ddi_dma_htoc(unitp->dma_handle, 0, &cookie)) {
			cmn_err(CE_WARN,
			    "DBRI translate address of IOPB space");
			goto failed;
		}
		unitp->iopbiobase = cookie.dmac_address;

		a = unitp->iopbkbase;
		bzero(a, iopbsize);

		unitp->cmdqp = (dbri_cmdq_t *)(void *)a;
		a += sizeof (dbri_cmdq_t);
		unitp->intq.intq_bp = (dbri_intq_t *)(void *)a;
		a += DBRI_NUM_INTQS * sizeof (dbri_intq_t);
		unitp->zerobuf = (dbri_zerobuf_t *)(void *)a;
		a += sizeof (dbri_zerobuf_t);

		/*
		 * Set up the interrupt queue
		 */
		ddi_dma_htoc(unitp->dma_handle,
		    (off_t)unitp->intq.intq_bp - (off_t)unitp->cmdqp,
		    &unitp->intq.dma_cookie);

		/*
		 * Allocate memory for the command pools
		 */
		unitp->cmdsize = cmdsize;
		unitp->cmdbase = (caddr_t)kmem_zalloc(cmdsize, KM_NOSLEEP);
		if (unitp->cmdbase == NULL) {
			cmn_err(CE_WARN,
			    "DBRI cannot allocate kernel memory");
			goto failed;
		}
		c = unitp->cmdbase;

		/*
		 * Force 4-word alignment for the descriptors
		 */
		a = (caddr_t)(((ulong_t)a + (DBRI_MD_ALIGN-1)) &
		    (ulong_t)(~(DBRI_MD_ALIGN-1)));
		ASSERT(((ulong_t)a % DBRI_MD_ALIGN) == 0);

		/*
		 * Initialize all of the dbri_streams
		 */
		for (dchan = 0; dchan < Dbri_nstreams; dchan++) {
			int nmds, ncmds;
			aud_cmdlist_t *cmdlist;

			ds = DChanToDs(unitp, dchan);
			if (DsDisabled(ds))
				continue;

			as = &ds->as;
			cmdlist = &as->cmdlist;

			ncmds = dbri_chan_maxbufs(ds->ctep);
			nmds = dbri_chan_maxbufs(ds->ctep);
			if (dchan == DBRI_AUDIO_OUT)
				nmds += dbri_chan_maxbufs(ds->ctep);

			if (ncmds > 0) {
				dbri_cmd_t *cmdpool;
				dbri_md_t *mdpool;
				int j;

				mdpool = (dbri_md_t *)(void *)a;
				a += nmds * sizeof (dbri_md_t);
				cmdpool = (dbri_cmd_t *)(void *)c;
				c += ncmds * sizeof (dbri_cmd_t);

				for (j = 0; j < ncmds;
				    j++, mdpool++, cmdpool++) {
					cmdpool->cmd.next = cmdlist->free;
					if (dchan == DBRI_AUDIO_OUT)
						cmdpool->md2 = mdpool++;
					else
						cmdpool->md2 = NULL;
					cmdpool->md = mdpool;
					cmdlist->free = &cmdpool->cmd;
				}
			}
		} /* for each stream */
	} /* just a local scope */

	unitp->zerobuf->ulaw[0] = 0xff;
	unitp->zerobuf->ulaw[1] = 0xff;
	unitp->zerobuf->ulaw[2] = 0xff;
	unitp->zerobuf->ulaw[3] = 0xff;
	unitp->zerobuf->alaw[0] = 0xd5;
	unitp->zerobuf->alaw[1] = 0xd5;
	unitp->zerobuf->alaw[2] = 0xd5;
	unitp->zerobuf->alaw[3] = 0xd5;
	unitp->zerobuf->linear[0] = 0x00;
	unitp->zerobuf->linear[1] = 0x00;
	unitp->zerobuf->linear[2] = 0x00;
	unitp->zerobuf->linear[3] = 0x00;
	ddi_dma_sync(unitp->dma_handle,
	    (off_t)unitp->zerobuf - (off_t)unitp->iopbkbase,
	    sizeof (dbri_zerobuf_t), DDI_DMA_SYNC_FORDEV);

	/*
	 * Initialize this unit and start the audio interface
	 */
	dbri_initchip(unitp);

	unitp->openinhibit = B_FALSE; /* Allow opens on the unit */

	UNLOCK_UNIT(unitp);
	return (B_TRUE);

failed:
	/*
	 * De-allocate any memory we allocated before we failed
	 *
	 * XXX - check for more resources allocated above that we need
	 * to free.
	 */
	if (unitp->iopbkbase != NULL) {
		ddi_iopb_free(unitp->iopbkbase);
		unitp->iopbkbase = NULL;
		unitp->iopbsize = 0;
	}
	if (unitp->cmdbase != NULL) {
		kmem_free(unitp->cmdbase, unitp->cmdsize);
		unitp->cmdbase = NULL;
		unitp->cmdsize = 0;
	}

	UNLOCK_UNIT(unitp);
	return (B_FALSE);
}
