/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

#ident	"@(#)lebuffer.c	1.9	94/10/18 SMI"

/*
 * ESC "lebuffer" driver.
 *
 * This driver identifies "lebuffer", maps in the memory associated
 * with the device and exports the property "learg"
 * describing the lebuffer-specifics.
 */

#include	<sys/types.h>
#include	<sys/kmem.h>
#include	<sys/stream.h>
#include	<sys/ethernet.h>
#include	<sys/cmn_err.h>
#include	<sys/conf.h>
#include	<sys/ddi.h>
#include	<sys/sunddi.h>
#include	<sys/le.h>
#include	<sys/errno.h>
#include	<sys/modctl.h>

static int lebufidentify(dev_info_t *dip);
static int lebufattach(dev_info_t *dip, ddi_attach_cmd_t cmd);

struct bus_ops lebuf_bus_ops = {
	BUSO_REV,
	i_ddi_bus_map,
	i_ddi_get_intrspec,
	i_ddi_add_intrspec,
	i_ddi_remove_intrspec,
	i_ddi_map_fault,
	ddi_dma_map,
	ddi_dma_allochdl,
	ddi_dma_freehdl,
	ddi_dma_bindhdl,
	ddi_dma_unbindhdl,
	ddi_dma_flush,
	ddi_dma_win,
	ddi_dma_mctl,
	ddi_ctlops,
	ddi_bus_prop_op
};

/*
 * Device ops - copied from dmaga.c .
 */
struct	dev_ops lebuf_ops = {
	DEVO_REV,		/* devo_rev */
	0,			/* devo_refcnt */
	ddi_no_info,		/* devo_info */
	lebufidentify,		/* devo_identify */
	nulldev,		/* devo_probe */
	lebufattach,		/* devo_attach */
	nodev,			/* devo_detach */
	nodev,			/* devo_reset */
	(struct cb_ops *)0,	/* driver operations */
	&lebuf_bus_ops		/* bus operations */
};

/*
 * This is the loadable module wrapper.
 */

extern struct mod_ops mod_driverops;

static struct modldrv modldrv = {
	&mod_driverops,	/* Type of module. This one is a driver */
	"le local buffer driver",	/* Name of the module. */
	&lebuf_ops,	/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modldrv, 0
};

int
_init()
{
	return (mod_install(&modlinkage));
}

int
_fini()
{
	return (mod_remove(&modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

static int
lebufidentify(dev_info_t *dip)
{
	if (strcmp(ddi_get_name(dip), "lebuffer") == 0)
		return (DDI_IDENTIFIED);
	else
		return (DDI_NOT_IDENTIFIED);
}

/* ARGSUSED1 */
static int
lebufattach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	int	unit = ddi_get_instance(dip);
	caddr_t	base;
	struct	leops	*lop;

	/* map in the buffer */
	if (ddi_map_regs(dip, 0, &base, 0, 0)) {
		cmn_err(CE_NOTE, "%s%d:  unable to map registers!",
			ddi_get_name(dip), unit);
		return (DDI_FAILURE);
	}

	ddi_report_dev(dip);

	/*
	 * Create "learg" property to pass info to child le driver.
	 */
	lop = kmem_alloc(sizeof (*lop), KM_SLEEP);
	lop->lo_dip = ddi_get_child(dip);
	lop->lo_flags = LOSLAVE;
	lop->lo_base = (u_long) base;
	lop->lo_size = 128 * 1024;	/* XXX use ddi_get_regsize() soon */
	lop->lo_init = NULL;
	lop->lo_intr = NULL;
	lop->lo_arg = NULL;

	if (ddi_prop_create(DDI_DEV_T_NONE, dip, 0, "learg",
		(caddr_t)&lop, sizeof (struct leops *)) != DDI_PROP_SUCCESS) {
		cmn_err(CE_NOTE, "lebuffer:  cannot create learg property");
		ddi_unmap_regs(dip, 0, &base, 0, 0);
		kmem_free((caddr_t)lop, sizeof (*lop));
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}
