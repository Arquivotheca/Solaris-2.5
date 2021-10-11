/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma	ident	"@(#)simmstat.c 1.4	95/02/26 SMI"

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/ddi_impldefs.h>
#include <sys/obpdefs.h>
#include <sys/cmn_err.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/debug.h>
#include <sys/sysmacros.h>
#include <sys/ivintr.h>
#include <sys/intr.h>
#include <sys/intreg.h>
#include <sys/autoconf.h>
#include <sys/modctl.h>
#include <sys/spl.h>

#include <sys/simmstat.h>

/* Useful debugging Stuff */
#include <sys/nexusdebug.h>

/*
 * Function prototypes
 */

static int simmstat_identify(dev_info_t *);

static int simmstat_attach(dev_info_t *, ddi_attach_cmd_t);

static int simmstat_detach(dev_info_t *, ddi_detach_cmd_t);

static void simmstat_add_kstats(struct simmstat_soft_state *);

static int simmstat_kstat_update(kstat_t *, int);

/*
 * Configuration data structures
 */
static struct cb_ops simmstat_cb_ops = {
	nulldev,		/* open */
	nulldev,		/* close */
	nulldev,		/* strategy */
	nulldev,		/* print */
	nodev,			/* dump */
	nulldev,		/* read */
	nulldev,		/* write */
	nulldev,		/* ioctl */
	nodev,			/* devmap */
	nodev,			/* mmap */
	nodev,			/* segmap */
	nochpoll,		/* poll */
	ddi_prop_op,		/* cb_prop_op */
	0,			/* streamtab */
	D_MP | D_NEW		/* Driver compatibility flag */
};

static struct dev_ops simmstat_ops = {
	DEVO_REV,		/* rev */
	0,			/* refcnt  */
	ddi_no_info,		/* getinfo */
	simmstat_identify,	/* identify */
	nulldev,		/* probe */
	simmstat_attach,	/* attach */
	simmstat_detach,	/* detach */
	nulldev,		/* reset */
	&simmstat_cb_ops,	/* cb_ops */
	(struct bus_ops *)0	/* bus_ops */
};

/*
 * Driver globals
 */
void *simmstatp;

extern struct mod_ops mod_driverops;

static struct modldrv modldrv = {
	&mod_driverops,		/* Type of module.  This one is a driver */
	"SIMM-status Leaf",	/* Name of module. */
	&simmstat_ops,		/* drvier ops */
};

static struct modlinkage modlinkage = {
	MODREV_1,		/* rev */
	(void *)&modldrv,
	NULL
};

/*
 * These are the module initialization routines.
 */

int
_init(void)
{
	int error;

	if ((error = ddi_soft_state_init(&simmstatp,
	    sizeof (struct simmstat_soft_state), 1)) != 0)
		return (error);

	return (mod_install(&modlinkage));
}

int
_fini(void)
{
	int error;

	if ((error = mod_remove(&modlinkage)) != 0)
		return (error);

	ddi_soft_state_fini(&simmstatp);
	return (0);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

static int
simmstat_identify(dev_info_t *devi)
{
	char *name = ddi_get_name(devi);
	int rc = DDI_NOT_IDENTIFIED;

	if ((strcmp(name, "simm-status") == 0) ||
	    (strcmp(name, "simmstat") == 0)) {
		rc = DDI_IDENTIFIED;
	}

	return (rc);
}

static int
simmstat_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	struct simmstat_soft_state *softsp;
	int instance, error;

	switch (cmd) {
	case DDI_ATTACH:
		break;

	case DDI_RESUME:
		return (DDI_SUCCESS);

	default:
		return (DDI_FAILURE);
	}

	instance = ddi_get_instance(devi);

	if (ddi_soft_state_zalloc(simmstatp, instance) != DDI_SUCCESS)
		return (DDI_FAILURE);

	softsp = ddi_get_soft_state(simmstatp, instance);

	/* Set the dip in the soft state */
	softsp->dip = devi;

	/* Get the board number from this nodes parent device. */
	softsp->pdip = ddi_get_parent(softsp->dip);
	if ((softsp->board = (int) ddi_getprop(DDI_DEV_T_ANY, softsp->pdip,
	    DDI_PROP_DONTPASS, "board#", -1)) == -1) {
		cmn_err(CE_WARN, "simm-status%d: Unable to retrieve board#"
			"property.", instance);
		error = DDI_FAILURE;
		goto bad;
	}

	DPRINTF(SIMMSTAT_ATTACH_DEBUG, ("simm-status%d: devi= 0x%x\n, "
		" softsp=0x%x\n", instance, devi, softsp));

	/* map in the registers for this device. */
	if (ddi_map_regs(softsp->dip, 0,
	    (caddr_t *)&softsp->simmstat_base, 0, 0)) {
		cmn_err(CE_WARN, "simm-status%d: unable to map registers\n",
			instance);
		error = DDI_FAILURE;
		goto bad;
	}

	/* create the kstats for this device */
	simmstat_add_kstats(softsp);

	ddi_report_dev(devi);

	return (DDI_SUCCESS);

bad:
	ddi_soft_state_free(simmstatp, instance);
	return (error);
}

/* ARGSUSED */
static int
simmstat_detach(dev_info_t *devi, ddi_detach_cmd_t cmd)
{
	switch (cmd) {
	case DDI_SUSPEND:
		/* Don't know how to handle this case, so fail it. */
		/* TODO - Add support for this feature */
		return (DDI_FAILURE);

	case DDI_DETACH:
		/* TODO - Make this work. */
	default:
		return (DDI_FAILURE);
	}
}

static void
simmstat_add_kstats(struct simmstat_soft_state *softsp)
{
	struct kstat *simmstat_ksp;

	if ((simmstat_ksp = kstat_create("unix", softsp->board,
	    SIMMSTAT_KSTAT_NAME, "misc", KSTAT_TYPE_RAW,
	    SIMM_COUNT, KSTAT_FLAG_PERSISTENT)) == NULL) {
		cmn_err(CE_WARN, "simm-status%d kstat_create failed",
			ddi_get_instance(softsp->dip));
	}

	simmstat_ksp->ks_update = simmstat_kstat_update;
	simmstat_ksp->ks_private = (void *)softsp;
	kstat_install(simmstat_ksp);
}

/*
 * Kstats only need ks_update functions when they change dynamically
 * at run time.
 * In the case of the simm-status registers, they contain battery
 * information for NVSIMMs. These need to be updated whenever a
 * kstat_read asks for the data. There is currently no plan to
 * ship NVSIMMs on this platform, but this support must be present.
 */

static int
simmstat_kstat_update(kstat_t *ksp, int rw)
{
	struct simmstat_soft_state *softsp;
	char *statp;		/* pointer to hardware register */
	char *kstatp;		/* pointer to kstat data buffer */
	int i;

	kstatp = (char *) ksp->ks_data;
	softsp = (struct simmstat_soft_state *) ksp->ks_private;

	statp = (char *) softsp->simmstat_base;

	/* this is a read-only kstat. Bail out on a write */
	if (rw == KSTAT_WRITE) {
		return (EACCES);
	} else {

		/*
		 * copy current status of hardware into the kstat
		 * structure.
		 */
		for (i = 0; i < SIMM_COUNT; i++, statp++, kstatp++) {
			 *kstatp = *statp;
		}
	}
	return (0);
}
