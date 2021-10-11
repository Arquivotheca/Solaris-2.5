/*
 * Copyright (c) 1990-93, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)obio.c	1.14	94/10/18 SMI"	/* SVr4 5.0 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/cmn_err.h>
#include <sys/cpu.h>
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/avintr.h>
#include <sys/ddi_impldefs.h>
#include <sys/kmem.h>
#include <sys/modctl.h>

static dev_info_t *obio_devi;

static int obio_dma_map(dev_info_t *, dev_info_t *, struct ddi_dma_req *,
    ddi_dma_handle_t *);
static int obio_dma_mctl(dev_info_t *, dev_info_t *, ddi_dma_handle_t,
    enum ddi_dma_ctlops, off_t *, u_int *, caddr_t *, u_int);
static int obio_bus_ctl(dev_info_t *, dev_info_t *, ddi_ctl_enum_t,
    void *, void *);
static int obio4_bus_ctl(dev_info_t *, dev_info_t *, ddi_ctl_enum_t,
    void *, void *);
static int obio_info(dev_info_t *, ddi_info_cmd_t, void *, void **);
static int obio_identify(dev_info_t *);
static int obio_probe(dev_info_t *);
static int obio_attach(dev_info_t *, ddi_attach_cmd_t);
static int obio_detach(dev_info_t *devi, ddi_detach_cmd_t cmd);

/*
 * This version of the struct is used only if we're running on a sun4
 */
static struct bus_ops sun4_obio_bus_ops = {
	BUSO_REV,
	i_ddi_bus_map,
	i_ddi_get_intrspec,
	i_ddi_add_intrspec,
	i_ddi_remove_intrspec,
	i_ddi_map_fault,
	obio_dma_map,
	ddi_dma_allochdl,
	ddi_dma_freehdl,
	ddi_dma_bindhdl,
	ddi_dma_unbindhdl,
	ddi_dma_flush,
	ddi_dma_win,
	obio_dma_mctl,
	obio4_bus_ctl,
	ddi_bus_prop_op
};

/*
 * This version otherwise ..
 */
static struct bus_ops obio_bus_ops = {
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
	obio_bus_ctl,
	ddi_bus_prop_op
};

static struct dev_ops obio_ops = {
	DEVO_REV,		/* devo_rev, */
	0,			/* refcnt  */
	obio_info,		/* info */
	obio_identify,		/* identify */
	obio_probe,		/* probe */
	obio_attach,		/* attach */
	obio_detach,		/* detach */
	nodev,			/* reset */
	(struct cb_ops *)0,	/* driver operations */
	&obio_bus_ops		/* bus operations */
};

static struct modldrv obio_modldrv = {
	&mod_driverops,		/* Type of module. This one is a driver */
	"obio nexus driver",	/* Name of the module. */
	&obio_ops		/* Driver ops */
};

static struct modlinkage obio_modlinkage = {
	MODREV_1, (void *)&obio_modldrv, NULL
};

/*
 * This is the driver initialization routine.
 */
int
_init(void)
{
	/*
	 * If we are on a sun4, override the defaults
	 */
	if ((cputype & CPU_ARCH) == SUN4_ARCH) {
		obio_modldrv.drv_linkinfo = "Sun4 obio nexus";
		obio_ops.devo_bus_ops = &sun4_obio_bus_ops;
	}
	return (mod_install(&obio_modlinkage));
}

int
_fini(void)
{
	return (mod_remove(&obio_modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&obio_modlinkage, modinfop));
}

/*ARGSUSED*/
static int
obio_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result)
{
	register int error;

	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		if (obio_devi == NULL) {
			error = DDI_FAILURE;
		} else {
			*result = (void *) obio_devi;
			error = DDI_SUCCESS;
		}
		break;
	case DDI_INFO_DEVT2INSTANCE:
		*result = (void *)0;
		error = DDI_SUCCESS;
		break;
	default:
		error = DDI_FAILURE;
	}
	return (error);
}

static int
obio_identify(dev_info_t *devi)
{
	if (strcmp(ddi_get_name(devi), "obio") == 0) {
		return (DDI_IDENTIFIED);
	}
	return (DDI_NOT_IDENTIFIED);
}

static int
obio_probe(dev_info_t *devi)
{
	/*
	 * If we are a self-identifying instantiation,
	 * then we unconditionally believe we are here.
	 */
	if (ddi_dev_is_sid(devi) == DDI_SUCCESS) {
		return (DDI_PROBE_SUCCESS);
	}
	/*
	 * Otherwise, we, as chessin puts it, "predict the past"
	 * via the use of the cputype variable.
	 */
	if ((cputype & CPU_ARCH) == SUN4_ARCH) {
		return (DDI_PROBE_SUCCESS);
	} else {
		return (DDI_PROBE_FAILURE);
	}
}

/*ARGSUSED1*/
static int
obio_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	switch (cmd) {
	case DDI_ATTACH:
		/*
		 * We should only have one obio nexus!
		 */
		obio_devi = devi;
		ddi_report_dev(devi);
		return (DDI_SUCCESS);

	case DDI_RESUME:
		return (DDI_SUCCESS);

	default:
		return (DDI_FAILURE);
	}
}

/* ARGSUSED */
static int
obio_detach(dev_info_t *devi, ddi_detach_cmd_t cmd)
{
	switch (cmd) {
	case DDI_SUSPEND:
		return (DDI_SUCCESS);

	case DDI_DETACH:
	default:
		return (DDI_FAILURE);
	}
}

static dev_info_t *sie_node;

static int
obio_dma_map(dev_info_t *dip, dev_info_t *rdip,
    struct ddi_dma_req *dmareq, ddi_dma_handle_t *handlep)
{
	if ((cputype & CPU_ARCH) == SUN4_ARCH) {
		if (cputype != CPU_SUN4_330 && sie_node == (dev_info_t *)0) {
			/*
			 * XXX	'sie' disappeared before 2.0 FCS. Nuke this.
			 */
			sie_node = ddi_find_devinfo("sie", 0, 1);
			if (!sie_node)
				sie_node = ddi_find_devinfo("ie", 0, 1);
		}
		if (cputype == CPU_SUN4_110) {
			/*
			 * Oh, ick. There is no good way to do this. We
			 * need to find out whether one of the nexi in
			 * the calling path was the ncr node, which has
			 * really funny constraints on a 4/110. We can't
			 * do this directly because the requestor for
			 * ncr may be some random child of ncr. Instead
			 * we'll check to see whether or not the requestor
			 * is the IE driver (the only other OBIO DMA
			 * master).
			 */
			if (rdip != sie_node) {
				dmareq->dmar_limits->dlim_addr_lo =
				    (u_long) (0 - (1<<20));
				dmareq->dmar_limits->dlim_addr_hi =
				    (u_long) -1;
			}
		}
	}
	return (ddi_dma_map(dip, rdip, dmareq, handlep));
}


static int
obio_dma_mctl(dev_info_t *dip, dev_info_t *rdip, ddi_dma_handle_t handle,
    enum ddi_dma_ctlops request, off_t *offp, u_int *lenp,
    caddr_t *objp, u_int flags)
{
	int rval;

	if ((rval = ddi_dma_mctl(dip, rdip, handle, request, offp,
	    lenp, objp, flags)) != DDI_SUCCESS) {
		return (rval);
	}

	if (cputype == CPU_SUN4_110 && rdip != sie_node &&
	    (request == DDI_DMA_HTOC || request == DDI_DMA_MOVWIN ||
		request == DDI_DMA_SEGTOC)) {
		/*
		 * If the requestor is the 4/110 "ncr", we have to translate
		 * the result from this implementation's VME address base
		 * (ick) to its address base (0xf00000).
		 */
		ddi_dma_cookie_t *cp = (ddi_dma_cookie_t *)objp;
		if (cp) {
			cp->dmac_address =
			    (cp->dmac_address - 0xfff00000) | 0xf00000;
		}
	}
	return (rval);
}

static int
obio_ctl_reportdev(dev_info_t *dip, dev_info_t *rdip)
{
	register int i, n;
	register dev_info_t *pdev;

#ifdef	lint
	dip = dip;
#endif

	pdev = (dev_info_t *)DEVI(rdip)->devi_parent;
	cmn_err(CE_CONT, "?%s%d at %s%d",
	    DEVI(rdip)->devi_name, DEVI(rdip)->devi_instance,
	    DEVI(pdev)->devi_name, DEVI(pdev)->devi_instance);

	for (i = 0, n = sparc_pd_getnreg(rdip); i < n; i++) {

		register struct regspec *rp = sparc_pd_getreg(rdip, i);

		if (i == 0)
			cmn_err(CE_CONT, "?: ");
		else
			cmn_err(CE_CONT, "? and ");

		/*
		 * XXX	This is a bit broken.
		 */
		cmn_err(CE_CONT, "?obio 0x%x", rp->regspec_addr);
	}

	for (i = 0, n = sparc_pd_getnintr(rdip); i < n; i++) {

		if (i == 0)
			cmn_err(CE_CONT, "? ");
		else
			cmn_err(CE_CONT, "?, ");

		cmn_err(CE_CONT, "?sparc ipl %d",
		    INT_IPL(sparc_pd_getintr(rdip, i)->intrspec_pri));
	}

	cmn_err(CE_CONT, "?\n");
	return (DDI_SUCCESS);
}

/*
 * We're prepared to claim that the interrupt string is in the
 * form of a list of <ipl> specifications - so just 'translate' it.
 */
static int
obio_ctl_xlate_intrs(dev_info_t *dip, dev_info_t *rdip, int *in,
	struct ddi_parent_private_data *pdptr)
{
	register size_t size;
	register int n;
	register struct intrspec *new;

	static char bad_obiointr_fmt[] =
	    "obio%d: bad interrupt spec for %s%d - sparc ipl %d\n";

	/*
	 * The list consists of either <ipl> elements
	 */
	if ((n = *in++) < 1)
		return (DDI_FAILURE);

	pdptr->par_nintr = n;
	size = n * sizeof (struct intrspec);
	new = pdptr->par_intr = kmem_zalloc(size, KM_SLEEP);

	while (n--) {
		register int level = *in++;

		if (level < 1 || level > 15) {
			cmn_err(CE_CONT, bad_obiointr_fmt,
			    DEVI(dip)->devi_instance, DEVI(rdip)->devi_name,
			    DEVI(rdip)->devi_instance, level);
			goto broken;
			/*NOTREACHED*/
		}
		new->intrspec_pri = level | INTLEVEL_ONBOARD;
		new++;
	}

	return (DDI_SUCCESS);
	/*NOTREACHED*/

broken:
	kmem_free(pdptr->par_intr, size);
	pdptr->par_intr = (void *)0;
	pdptr->par_nintr = 0;
	return (DDI_FAILURE);
}

static int
obio_bus_ctl(dev_info_t *dip, dev_info_t *rdip,
	ddi_ctl_enum_t op, void *a, void *v)
{
	switch (op) {

	case DDI_CTLOPS_REPORTDEV:
		return (obio_ctl_reportdev(dip, rdip));

	case DDI_CTLOPS_XLATE_INTRS:
		return (obio_ctl_xlate_intrs(dip, rdip, a, v));

	default:
		return (ddi_ctlops(dip, rdip, op, a, v));
	}
}

/*
 * Sun 4 version
 */
static int
obio4_bus_ctl(dev_info_t *dip, dev_info_t *rdip,
	ddi_ctl_enum_t op, void *a, void *v)
{
	switch (op)  {

	case DDI_CTLOPS_INITCHILD:
		/* XXX - Should make sure the child is uniquely named */
		return (impl_ddi_sunbus_initchild(a));

	case DDI_CTLOPS_UNINITCHILD:
		impl_ddi_sunbus_removechild(a);
		return (DDI_SUCCESS);

	case DDI_CTLOPS_REPORTDEV:
		return (obio_ctl_reportdev(dip, rdip));

	case DDI_CTLOPS_XLATE_INTRS:
		return (obio_ctl_xlate_intrs(dip, rdip, a, v));

	case DDI_CTLOPS_DMAPMAPC:
		if (rdip == sie_node) {
			return (DDI_DMA_PARTIAL);
		}
		/*FALLTHRU*/
	}

	return (ddi_ctlops(dip, rdip, op, a, v));
}
