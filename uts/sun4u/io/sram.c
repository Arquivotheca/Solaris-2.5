/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma	ident	"@(#)sram.c 1.4	95/03/01 SMI"

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

#include <sys/sram.h>
#include <sys/promif.h>

/* Useful debugging Stuff */
#include <sys/nexusdebug.h>

/*
 * Function protoypes
 */

static int sram_attach(dev_info_t *, ddi_attach_cmd_t);

static int sram_identify(dev_info_t *);

static int sram_detach(dev_info_t *, ddi_detach_cmd_t);

static void sram_add_kstats(struct sram_soft_state *);

/*
 * Configuration data structures
 */
static struct cb_ops sram_cb_ops = {
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

static struct dev_ops sram_ops = {
	DEVO_REV,		/* rev */
	0,			/* refcnt  */
	ddi_no_info,		/* getinfo */
	sram_identify,		/* identify */
	nulldev,		/* probe */
	sram_attach,		/* attach */
	sram_detach,		/* detach */
	nulldev,		/* reset */
	&sram_cb_ops,		/* cb_ops */
	(struct bus_ops *)0	/* bus_ops */
};


/*
 * Driver globals
 */
void *sramp;			/* sram soft state hook */
static struct kstat *resetinfo_ksp;

extern struct mod_ops mod_driverops;

static struct modldrv modldrv = {
	&mod_driverops,		/* Type of module.  This one is a driver */
	"Sram Leaf",		/* name of module */
	&sram_ops,		/* driver ops */
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

	if ((error = ddi_soft_state_init(&sramp,
	    sizeof (struct sram_soft_state), 1)) != 0)
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
sram_identify(dev_info_t *devi)
{
	char *name = ddi_get_name(devi);
	int rc = DDI_NOT_IDENTIFIED;

	if (strcmp(name, "sram") == 0) {
		rc = DDI_IDENTIFIED;
	}

	return (rc);
}

static int
sram_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	int instance, error;
	struct sram_soft_state *softsp;

	switch (cmd) {
	case DDI_ATTACH:
		break;

	case DDI_RESUME:
		return (DDI_SUCCESS);

	default:
		return (DDI_FAILURE);
	}

	instance = ddi_get_instance(devi);

	if (ddi_soft_state_zalloc(sramp, instance) != DDI_SUCCESS)
		return (DDI_FAILURE);

	softsp = ddi_get_soft_state(sramp, instance);

	/* Set the dip in the soft state */
	softsp->dip = devi;

	/* get the board number from this devices parent. */
	softsp->pdip = ddi_get_parent(softsp->dip);
	if ((softsp->board = (int) ddi_getprop(DDI_DEV_T_ANY, softsp->pdip,
	    DDI_PROP_DONTPASS, "board#", -1)) == -1) {
		cmn_err(CE_WARN, "sram%d: Unable to retrieve board#"
			"property.", instance);
		error = DDI_FAILURE;
		goto bad;
	}

	DPRINTF(SRAM_ATTACH_DEBUG, ("sram%d: devi= 0x%x\n, "
		" softsp=0x%x\n", instance, devi, softsp));

	/* map in the registers for this device. */
	if (ddi_map_regs(softsp->dip, 0,
	    (caddr_t *)&softsp->sram_base, 0, 0)) {
		cmn_err(CE_WARN, "sram%d: unable to map sram\n",
			instance);
		error = DDI_FAILURE;
		goto bad;
	}

	/* create the kstats for this device. */
	sram_add_kstats(softsp);

	ddi_report_dev(devi);

	return (DDI_SUCCESS);

bad:
	ddi_soft_state_free(sramp, instance);
	return (error);
}

/* ARGSUSED */
static int
sram_detach(dev_info_t *devi, ddi_detach_cmd_t cmd)
{
	switch (cmd) {
	case DDI_SUSPEND:
		/* Dont know how to handle this case, so fail it. */
		/* TODO - We need to add this functionality */
		return (DDI_FAILURE);

	case DDI_DETACH:
		/* TODO - We need to add this functionality */
		return (DDI_FAILURE);

	default:
		return (DDI_FAILURE);
	}
}

/*
 * The Reset-info structure passed up by POST has it's own kstat.
 * It only needs to get created once. So the first sram instance
 * that gets created will check for the OBP property 'reset-info'
 * in the root node of the OBP device tree. If this property exists,
 * then the reset-info kstat will get created. Otherwise it will
 * not get created. This will inform users whether or not a fatal
 * hardware reset has recently occurred.
 */
static void
sram_add_kstats(struct sram_soft_state *softsp)
{
	int reset_size;

	/*
	 * only one reset_info kstat per system, so don't create it if
	 * it exists already.
	 */
	if (resetinfo_ksp == NULL) {
		/* does the root node have a 'reset-info' property? */
		if (prom_getprop(prom_rootnode(), "reset-info",
		    (caddr_t)&softsp->reset_info) != -1) {
			/* First read version and size */
			reset_size = *(int *)((int)softsp->sram_base +
				(int)softsp->reset_info + sizeof (int));

			/* Check for illegal size values. */
			if (((u_int)reset_size > MX_RSTINFO_SZ) ||
			    ((u_int)reset_size == 0)) {
				cmn_err(CE_NOTE, "sram%d: Illegal "
					"reset_size: 0x%x\n",
					ddi_get_instance(softsp->dip),
					reset_size);
				return;
			}

			/* create the reset-info kstat */
			resetinfo_ksp = kstat_create("unix",
				ddi_get_instance(softsp->dip),
				RESETINFO_KSTAT_NAME, "misc", KSTAT_TYPE_RAW,
				reset_size, KSTAT_FLAG_PERSISTENT);
			if (resetinfo_ksp == NULL) {
				cmn_err(CE_WARN, "FHC%d kstat_create failed",
					ddi_get_instance(softsp->dip));
				return;
			}

			/* now copy the data into the kstat */
			bcopy((void *)&resetinfo_ksp->ks_data, (void *)
				((int)softsp->sram_base +
				(int)softsp->reset_info), reset_size);

			kstat_install(resetinfo_ksp);
		}
	}
}
