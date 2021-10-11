
/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)pci_pci.c	1.10	95/10/05 SMI"

/*
 *	PCI to PCI bus bridge nexus driver
 */

#include <sys/conf.h>
#include <sys/kmem.h>
#include <sys/debug.h>
#include <sys/modctl.h>
#include <sys/autoconf.h>
#include <sys/ddi_impldefs.h>
#include <sys/pci.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>

static int ppb_bus_map(dev_info_t *, dev_info_t *, ddi_map_req_t *,
	off_t, off_t, caddr_t *);
static int ppb_ctlops(dev_info_t *, dev_info_t *, ddi_ctl_enum_t,
	void *, void *);

struct bus_ops ppb_bus_ops = {
	BUSO_REV,
	ppb_bus_map,
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
	ppb_ctlops,
	ddi_bus_prop_op
};

static int ppb_identify(dev_info_t *devi);
static int ppb_probe(dev_info_t *);
static int ppb_attach(dev_info_t *devi, ddi_attach_cmd_t cmd);
static int ppb_detach(dev_info_t *devi, ddi_detach_cmd_t cmd);
static int ppb_info(dev_info_t *dip, ddi_info_cmd_t infocmd,
	void *arg, void **result);

struct dev_ops ppb_ops = {
	DEVO_REV,		/* devo_rev */
	0,			/* refcnt  */
	ppb_info,		/* info */
	ppb_identify,		/* identify */
	ppb_probe,		/* probe */
	ppb_attach,		/* attach */
	ppb_detach,		/* detach */
	nulldev,		/* reset */
	(struct cb_ops *)0,	/* driver operations */
	&ppb_bus_ops		/* bus operations */

};

static void ppb_removechild(dev_info_t *);
static int ppb_initchild(dev_info_t *child);
static int ppb_create_pci_prop(dev_info_t *child, uint_t *, uint_t *);

/*
 * Module linkage information for the kernel.
 */

static struct modldrv modldrv = {
	&mod_driverops, /* Type of module */
	"PCI to PCI bridge nexus driver",
	&ppb_ops,	/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1,
	(void *)&modldrv,
	NULL
};

int
_init(void)
{
	return (mod_install(&modlinkage));
}

int
_fini(void)
{
	return (mod_remove(&modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

/*ARGSUSED*/
static int
ppb_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result)
{
	return (DDI_FAILURE);
}

/*ARGSUSED*/
static int
ppb_identify(dev_info_t *dip)
{
	return (DDI_IDENTIFIED);
}

/*ARGSUSED*/
static int
ppb_probe(register dev_info_t *devi)
{
	return (DDI_PROBE_SUCCESS);
}

/*ARGSUSED*/
static int
ppb_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	ddi_prop_create(DDI_DEV_T_NONE, devi, DDI_PROP_CANSLEEP,
		"device_type", (caddr_t)"pci", 4);
	ddi_report_dev(devi);
	return (DDI_SUCCESS);
}

/*ARGSUSED*/
static int
ppb_detach(dev_info_t *devi, ddi_detach_cmd_t cmd)
{
	return (DDI_SUCCESS);
}

/*ARGSUSED*/
static int
ppb_bus_map(dev_info_t *dip, dev_info_t *rdip, ddi_map_req_t *mp,
        off_t offset, off_t len, caddr_t *vaddrp)
{
        register dev_info_t *pdip;

        pdip = (dev_info_t *)DEVI(dip)->devi_parent;
        return ((DEVI(pdip)->devi_ops->devo_bus_ops->bus_map)(pdip,
            rdip, mp, offset, len, vaddrp));
}

/*ARGSUSED*/
static int
ppb_ctlops(dev_info_t *dip, dev_info_t *rdip,
	ddi_ctl_enum_t ctlop, void *arg, void *result)
{
	pci_regspec_t *drv_regp;
	int	reglen;
	int	rn;
	int	totreg;

	switch (ctlop) {
	case DDI_CTLOPS_REPORTDEV:
		if (rdip == (dev_info_t *)0)
			return (DDI_FAILURE);
		cmn_err(CE_CONT, "?PCI-device: %s%d\n",
		    ddi_get_name(rdip), ddi_get_instance(rdip));
		return (DDI_SUCCESS);

	case DDI_CTLOPS_INITCHILD:
		return (ppb_initchild((dev_info_t *)arg));

	case DDI_CTLOPS_UNINITCHILD:
		ppb_removechild((dev_info_t *)arg);
		return (DDI_SUCCESS);

	case DDI_CTLOPS_NINTRS:
		if (ddi_get_parent_data(rdip))
			*(int *)result = 1;
		else
			*(int *)result = 0;
		return (DDI_SUCCESS);

	case DDI_CTLOPS_XLATE_INTRS:
		return (DDI_SUCCESS);

	case DDI_CTLOPS_SIDDEV:
		return (DDI_SUCCESS);

	case DDI_CTLOPS_REGSIZE:
	case DDI_CTLOPS_NREGS:
		if (rdip == (dev_info_t *)0)
			return (DDI_FAILURE);
		break;

	default:
		return (ddi_ctlops(dip, rdip, ctlop, arg, result));
	}

	*(int *)result = 0;
	if (ddi_getlongprop(DDI_DEV_T_NONE, rdip,
		DDI_PROP_DONTPASS | DDI_PROP_CANSLEEP, "reg",
		(caddr_t)&drv_regp, &reglen) != DDI_SUCCESS)
		return (DDI_FAILURE);

	totreg = reglen / sizeof (pci_regspec_t);
	if (ctlop == DDI_CTLOPS_NREGS)
		*(int *)result = totreg;
	else if (ctlop == DDI_CTLOPS_REGSIZE) {
		rn = *(int *)arg;
		if (rn > totreg)
			return (DDI_FAILURE);
		*(off_t *)result = drv_regp[rn].pci_size_low;
	}
	return (DDI_SUCCESS);
}

static int
ppb_initchild(dev_info_t *child)
{
	struct ddi_parent_private_data *pdptr;
	char name[MAXNAMELEN];
	int ret;
	uint_t slot, func;

	if ((ret = ppb_create_pci_prop(child, &slot, &func)) != DDI_SUCCESS)
		return (ret);
	if (func != 0)
		sprintf(name, "%x,%x", slot, func);
	else
		sprintf(name, "%x", slot);

	ddi_set_name_addr(child, name);

	if (ddi_getprop(DDI_DEV_T_NONE, child, DDI_PROP_DONTPASS, "interrupts",
		-1) != -1) {
		pdptr = (struct ddi_parent_private_data *)
			kmem_zalloc((sizeof (struct ddi_parent_private_data) +
			sizeof (struct intrspec)), KM_SLEEP);
		pdptr->par_intr = (struct intrspec *)(pdptr + 1);
		pdptr->par_nintr = 1;
		ddi_set_parent_data(child, (caddr_t)pdptr);
	} else
		ddi_set_parent_data(child, NULL);

	return (DDI_SUCCESS);
}

static void
ppb_removechild(dev_info_t *dip)
{
	register struct ddi_parent_private_data *pdptr;

	pdptr = (struct ddi_parent_private_data *)ddi_get_parent_data(dip);
	if (pdptr != NULL) {
		kmem_free(pdptr, (sizeof (*pdptr) + sizeof (struct intrspec)));
		ddi_set_parent_data(dip, NULL);
	}
	ddi_set_name_addr(dip, NULL);

	/*
	 * Strip the node to properly convert it back to prototype form
	 */
	ddi_remove_minor_node(dip, NULL);

	impl_rem_dev_props(dip);
}

static int
ppb_create_pci_prop(dev_info_t *child, uint_t *foundslot, uint_t *foundfunc)
{
	pci_regspec_t *pci_rp;
	int	length;
	int	value;

	/* get child "reg" property */
	value = ddi_getlongprop(DDI_DEV_T_NONE, child, DDI_PROP_CANSLEEP,
		"reg", (caddr_t)&pci_rp, &length);
	if (value != DDI_SUCCESS)
		return (value);

	ddi_prop_create(DDI_DEV_T_NONE, child, DDI_PROP_CANSLEEP, "reg",
		(caddr_t)pci_rp, length);

	/* copy the device identifications */
	*foundslot = PCI_REG_DEV_G(pci_rp->pci_phys_hi);
	*foundfunc = PCI_REG_FUNC_G(pci_rp->pci_phys_hi);

	/*
	 * free the memory allocated by ddi_getlongprop ().
	 */
	kmem_free(pci_rp, length);

	/* assign the basic PCI Properties */

	value = ddi_getprop(DDI_DEV_T_ANY, child, DDI_PROP_CANSLEEP,
		"vendor-id", -1);
	if (value != -1)
		ddi_prop_create(DDI_DEV_T_NONE, child, DDI_PROP_CANSLEEP,
		"vendor-id", (caddr_t)&value, sizeof (int));

	value = ddi_getprop(DDI_DEV_T_ANY, child, DDI_PROP_CANSLEEP,
		"device-id", -1);
	if (value != -1)
		ddi_prop_create(DDI_DEV_T_NONE, child, DDI_PROP_CANSLEEP,
		"device-id", (caddr_t)&value, sizeof (int));

	value = ddi_getprop(DDI_DEV_T_ANY, child, DDI_PROP_CANSLEEP,
		"interrupts", -1);
	if (value != -1)
		ddi_prop_create(DDI_DEV_T_NONE, child, DDI_PROP_CANSLEEP,
		"interrupts", (caddr_t)&value, sizeof (int));
	return (DDI_SUCCESS);
}
