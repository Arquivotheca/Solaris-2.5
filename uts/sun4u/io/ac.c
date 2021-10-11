/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma	ident	"@(#)ac.c 1.4	95/03/29 SMI"

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
#include <sys/autoconf.h>
#include <sys/modctl.h>

#include <sys/ac.h>

/* Useful debugging Stuff */
#include <sys/nexusdebug.h>

/*
 * Function prototypes
 */

static int ac_attach(dev_info_t *, ddi_attach_cmd_t);
static int ac_identify(dev_info_t *);
static int ac_detach(dev_info_t *, ddi_detach_cmd_t);
static void ac_add_kstats(struct ac_soft_state *);
static int ac_kstat_update(kstat_t *, int);

/*
 * Configuration data structures
 */
static struct cb_ops ac_cb_ops = {
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

static struct dev_ops ac_ops = {
	DEVO_REV,		/* devo_rev, */
	0,			/* refcnt */
	ddi_no_info,		/* getinfo */
	ac_identify,		/* identify */
	nulldev,		/* probe */
	ac_attach,		/* attach */
	ac_detach,		/* detach */
	nulldev,		/* reset */
	&ac_cb_ops,		/* cb_ops */
	(struct bus_ops *)0,	/* bus_ops */
};

/*
 * Driver globals
 * TODO - We need to investigate what locking eneds to be done here.
 */
void *acp;				/* ac soft state hook */

extern struct mod_ops mod_driverops;

static struct modldrv modldrv = {
	&mod_driverops,		/* Type of module.  This one is a driver */
	"AC Leaf",		/* name of module */
	&ac_ops,		/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1,
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

	if ((error = ddi_soft_state_init(&acp,
	    sizeof (struct ac_soft_state), 1)) != 0)
		return (error);

	return (mod_install(&modlinkage));
}

int
_fini(void)
{
	int error;

	if ((error = mod_remove(&modlinkage)) != 0)
		return (error);

	return (0);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

static int
ac_identify(dev_info_t *devi)
{
	char *name = ddi_get_name(devi);
	int rc = DDI_NOT_IDENTIFIED;

	if (strcmp(name, "ac") == 0) {
		rc = DDI_IDENTIFIED;
	}

	return (rc);
}

static int
ac_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	int instance, error;
	struct ac_soft_state *softsp;

	switch (cmd) {
	case DDI_ATTACH:
		break;

	case DDI_RESUME:
		return (DDI_SUCCESS);

	default:
		return (DDI_FAILURE);
	}

	instance = ddi_get_instance(devi);

	if (ddi_soft_state_zalloc(acp, instance) != DDI_SUCCESS)
		return (DDI_FAILURE);

	softsp = ddi_get_soft_state(acp, instance);

	/* Set the dip in the soft state */
	softsp->dip = devi;

	/* Get the board number from this nodes parent */
	softsp->pdip = ddi_get_parent(softsp->dip);
	if ((softsp->board = (int) ddi_getprop(DDI_DEV_T_ANY, softsp->pdip,
	    DDI_PROP_DONTPASS, "board#", -1)) == -1) {
		cmn_err(CE_WARN, "ac%d: Unable to retrieve board#"
			"property.", instance);
		error = DDI_FAILURE;
		goto bad;
	}

	DPRINTF(AC_ATTACH_DEBUG, ("ac%d: devi= 0x%x\n, "
		" softsp=0x%x\n", instance, devi, softsp));

	/* map in the registers for this device. */
	if (ddi_map_regs(softsp->dip, 0,
		(caddr_t *)&softsp->ac_base, 0, 0)) {
		cmn_err(CE_WARN, "ac%d: unable to map AC registers\n",
			ddi_get_instance(softsp->dip));
		error = DDI_FAILURE;
		goto bad;
	}

	/* Setup the pointers to the hardware registers */
	softsp->ac_memctl = (u_longlong_t *)((char *) softsp->ac_base +
		AC_OFF_MEMCTL);
	softsp->ac_memdecode0 = (u_longlong_t *)((char *) softsp->ac_base +
		AC_OFF_MEMDEC0);
	softsp->ac_memdecode1 = (u_longlong_t *)((char *) softsp->ac_base +
		AC_OFF_MEMDEC1);

	/* create the kstats for this device. */
	ac_add_kstats(softsp);

	ddi_report_dev(devi);

	return (DDI_SUCCESS);

bad:
	ddi_soft_state_free(acp, instance);
	return (error);
}

/* ARGSUSED */
static int
ac_detach(dev_info_t *devi, ddi_detach_cmd_t cmd)
{
	switch (cmd) {
	case DDI_SUSPEND:
		/* Dont know how to handle this case, so fail it. */
		return (DDI_FAILURE);

	case DDI_DETACH:
	default:
		return (DDI_FAILURE);
	}
}

static void
ac_add_kstats(struct ac_soft_state *softsp)
{
	struct kstat *ac_ksp;
	struct ac_kstat *ac_named_ksp;

	if ((ac_ksp = kstat_create("unix", softsp->board,
	    AC_KSTAT_NAME, "misc", KSTAT_TYPE_NAMED,
	    sizeof (struct ac_kstat) / sizeof (kstat_named_t),
	    KSTAT_FLAG_PERSISTENT)) == NULL) {
		cmn_err(CE_WARN, "ac%d kstat_create failed",
			ddi_get_instance(softsp->dip));
		return;
	}

	ac_named_ksp = (struct ac_kstat *)(ac_ksp->ks_data);

	/* initialize the named kstats */
	kstat_named_init(&ac_named_ksp->ac_memctl,
		MEMCTL_KSTAT_NAMED,
		KSTAT_DATA_ULONGLONG);

	kstat_named_init(&ac_named_ksp->ac_memdecode0,
		MEMDECODE0_KSTAT_NAMED,
		KSTAT_DATA_ULONGLONG);

	kstat_named_init(&ac_named_ksp->ac_memdecode1,
		MEMDECODE1_KSTAT_NAMED,
		KSTAT_DATA_ULONGLONG);

	ac_ksp->ks_update = ac_kstat_update;
	ac_ksp->ks_private = (void *)softsp;
	kstat_install(ac_ksp);
}

static int
ac_kstat_update(kstat_t *ksp, int rw)
{
	struct ac_kstat *acksp;
	struct ac_soft_state *softsp;

	acksp = (struct ac_kstat *) ksp->ks_data;
	softsp = (struct ac_soft_state *) ksp->ks_private;

	/* this is a read-only kstat. Bail out on a write */
	if (rw == KSTAT_WRITE) {
		return (EACCES);
	} else {
		/*
		 * copy the current state of the hardware into the
		 * kstat structure.
		 */
		acksp->ac_memctl.value.ull = *softsp->ac_memctl;
		acksp->ac_memdecode0.value.ull = *softsp->ac_memdecode0;
		acksp->ac_memdecode1.value.ull = *softsp->ac_memdecode1;
	}
	return (0);
}
