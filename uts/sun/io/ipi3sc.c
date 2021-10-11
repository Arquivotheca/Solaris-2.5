/*
 * Copyright (c) 1991, 1992 by Sun Microsystems, Inc.
 */

#pragma	ident	"@(#)ipi3sc.c 1.11	94/10/18 SMI"

/*
 * IPI nexus driver.
 */

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/conf.h>
#include <sys/kmem.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/ipi_driver.h>


static int
ipi3sc_bus_ctl(dev_info_t *, dev_info_t *, ddi_ctl_enum_t, void *, void *);
static int ipi3sc_identify(dev_info_t *dip);
static int ipi3sc_probe(dev_info_t *dip);
static int ipi3sc_attach(dev_info_t *dip, ddi_attach_cmd_t cmd);

/*
 * Bus ops vector
 */
static struct bus_ops ipi3sc_bus_ops = {
	BUSO_REV,
	nullbusmap,
	0,		/* ddi_intrspec_t	(*bus_get_intrspec)(); */
	0,		/* int			(*bus_add_intrspec)(); */
	0,		/* void			(*bus_remove_intrspec)(); */
	0,		/* int			(*bus_map_fault)() */
	ddi_no_dma_map,
	ddi_no_dma_allochdl,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	ipi3sc_bus_ctl,
	ddi_bus_prop_op,
};

static struct dev_ops ipi3sc_ops = {
	DEVO_REV,		/* devo_rev, */
	0,			/* refcnt */
	ddi_no_info,		/* get_dev_info */
	ipi3sc_identify,	/* identify */
	ipi3sc_probe,		/* probe */
	ipi3sc_attach,		/* attach */
	nodev,			/* detach */
	nodev,			/* reset */
	0,			/* driver operations */
	&ipi3sc_bus_ops		/* bus operations */
};

/*
 * This is the loadable module wrapper: "module configuration section".
 */
#include <sys/modctl.h>

extern struct mod_ops mod_driverops;
/*
 * Module linkage information for the kernel.
 */
static struct modldrv modldrv = {
	&mod_driverops,	/* Type of module.  This one is a driver */
	"IPI-3 SC Node",	/* Name of the module */
	&ipi3sc_ops,	/* driver ops */
};


static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modldrv, NULL
};

int
_init(void)
{
	return (mod_install(&modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

int
_fini(void)
{
	return (mod_remove(&modlinkage));
}


/*ARGSUSED*/
static int
ipi3sc_bus_ctl(dev_info_t *d, dev_info_t *r, ddi_ctl_enum_t o,
	void *a, void *v)
{
	int facility, csf;
	ipi_config_t *ccf, *mcf;
	char name[4];
	dev_info_t *child_dip;

	switch (o) {
	case DDI_CTLOPS_REPORTDEV:
		child_dip = (dev_info_t *)r;
		/*
		 * we get the facility from the child node.  we assume
		 * it's there or we wouldn't have gotten this far.
		 */
		facility = ddi_getprop(DDI_DEV_T_ANY, child_dip,
		    DDI_PROP_DONTPASS, "facility", 0);

		cmn_err(CE_CONT, "?%s%d at %s%d: facility %d\n",
		    ddi_get_name(r), ddi_get_instance(r),
		    ddi_get_name(d), ddi_get_instance(d), facility);
		return (DDI_SUCCESS);

	case DDI_CTLOPS_INITCHILD:

		child_dip = (dev_info_t *)a;
		/*
		 * Get my facility,  If the facility property is not found,
		 * return and tell the framework to discard this dev_info
		 * because it's an incomplete hardware node.
		 */
		if ((facility = ddi_getprop(DDI_DEV_T_ANY, child_dip,
		    DDI_PROP_DONTPASS, "facility", -1)) == -1)
			return (DDI_NOT_WELL_FORMED);

		/* Create the name for id */
		name[0] = '0' + facility;
		name[1] = ',';
		name[2] = '0';
		name[3] = '\0';

		/* Set the name into the childs dev_info */
		ddi_set_name_addr(child_dip, name);

		/*
		 * Now set the CSF (channel slave facility)
		 * number into the child's node.
		 */
		ccf = (ipi_config_t *)
		    kmem_alloc(sizeof (ipi_config_t), KM_SLEEP);
		mcf = (ipi_config_t *)ddi_get_driver_private(d);
		*ccf = *mcf;
		csf = mcf->ic_addr;
		ccf->ic_addr =
		    IPI_MAKE_ADDR(IPI_CHAN(csf), IPI_SLAVE(csf), facility);
		ddi_set_driver_private(child_dip, (caddr_t)ccf);
		return (DDI_SUCCESS);

	case DDI_CTLOPS_REMOVECHILD:
		kmem_free(ddi_get_driver_private(a), sizeof (ipi_config_t));
		ddi_set_name_addr((dev_info_t *)a, NULL);
		ddi_set_driver_private((dev_info_t *)a, NULL);
		return (DDI_SUCCESS);
	default:
		/*
		 * Pass this request up to my parent, I don't know how to deal
		 * with it.
		 */
		return (ddi_ctlops(d, r, o, a, v));
	}
}

/*
 * Autoconfiguration Routines
 */

static int
ipi3sc_identify(dev_info_t *dev)
{
	char *name = ddi_get_name(dev);

	if (strcmp(name, "ipi3sc") == 0) {
		return (DDI_IDENTIFIED);
	} else {
		return (DDI_NOT_IDENTIFIED);
	}
}

static int
ipi3sc_probe(dev_info_t *dev)
{
	if (ddi_get_driver_private(dev) == (caddr_t)-1)
		return (DDI_PROBE_FAILURE);
	else
		return (DDI_PROBE_SUCCESS);
}

static int
ipi3sc_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	switch (cmd) {
	case DDI_ATTACH:
		ddi_report_dev(dip);
		return (DDI_SUCCESS);

	default:
		return (DDI_FAILURE);
	}
}
