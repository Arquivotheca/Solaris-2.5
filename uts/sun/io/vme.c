/*
 * Copyright (c) 1991-1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)vme.c 1.25	94/12/01 SMI"	/* SVr4 5.0 */

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/cmn_err.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/ddi_impldefs.h>
#include <sys/cpu.h>
#include <sys/spl.h>
#include <sys/scb.h>
#include <sys/avintr.h>
#include <sys/kmem.h>
#include <sys/modctl.h>
#include <sys/errno.h>
#include <sys/debug.h>
#include <sys/iocache.h>

#include <sys/machsystm.h>

static int vmebus_to_sparc(int), sparc_to_vmebus(int);
static void vme_iocache_flush(ddi_dma_impl_t *mp);
static void vme_iocache_inval(ddi_dma_impl_t *mp);
static void vme_iocache_setup(ddi_dma_impl_t *mp);

static int
vme_add_intrspec(dev_info_t *, dev_info_t *, ddi_intrspec_t,
    ddi_iblock_cookie_t *, ddi_idevice_cookie_t *,
    u_int (*)(caddr_t), caddr_t, int);

static void
vme_remove_intrspec(dev_info_t *, dev_info_t *, ddi_intrspec_t,
    ddi_iblock_cookie_t);

static int
vme_dma_map(dev_info_t *, dev_info_t *, struct ddi_dma_req *,
    ddi_dma_handle_t *);

static int
vme_dma_bindhdl(dev_info_t *, dev_info_t *, ddi_dma_handle_t,
    struct ddi_dma_req *, ddi_dma_cookie_t *, u_int *);

static int
vme_dma_unbindhdl(dev_info_t *, dev_info_t *, ddi_dma_handle_t);

static int
vme_dma_flush(dev_info_t *, dev_info_t *, ddi_dma_handle_t,
    off_t, u_int, u_int);

static int
vme_dma_win(dev_info_t *, dev_info_t *, ddi_dma_handle_t,
    uint_t, off_t *, uint_t *, ddi_dma_cookie_t *, uint_t *);

static int
vme_dma_mctl(dev_info_t *, dev_info_t *, ddi_dma_handle_t,
    enum ddi_dma_ctlops, off_t *, u_int *, caddr_t *, u_int);

static int
vme_bus_ctl(dev_info_t *, dev_info_t *, ddi_ctl_enum_t, void *, void *);

static struct bus_ops vme_bus_ops = {
	BUSO_REV,
	i_ddi_bus_map,
	i_ddi_get_intrspec,
	vme_add_intrspec,
	vme_remove_intrspec,
	i_ddi_map_fault,
	vme_dma_map,
	ddi_dma_allochdl,
	ddi_dma_freehdl,
	vme_dma_bindhdl,
	vme_dma_unbindhdl,
	vme_dma_flush,
	vme_dma_win,
	vme_dma_mctl,
	vme_bus_ctl,
	ddi_bus_prop_op,
};

static int vme_identify(dev_info_t *devi);
static int vme_probe(dev_info_t *devi);
static int vme_attach(dev_info_t *devi, ddi_attach_cmd_t cmd);

static struct dev_ops vme_ops = {
	DEVO_REV,		/* devo_rev, */
	0,			/* refcnt  */
	ddi_no_info,		/* info */
	vme_identify,		/* identify */
	vme_probe,		/* probe */
	vme_attach,		/* attach */
	nodev,			/* detach */
	nodev,			/* reset */
	(struct cb_ops *)0,	/* driver operations */
	&vme_bus_ops		/* bus operations */
};

static u_long vme_base, vme_sizeA24, vme_sizeA32;
static dev_info_t *vme_list;

static struct modldrv modldrv = {
	&mod_driverops,		/* Type of module.  This one is a driver */
	"VME nexus driver",	/* Name of module. */
	&vme_ops		/* Driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modldrv, NULL
};

/*
 * This is the driver initialization routine.
 */

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

static int
vme_identify(dev_info_t *devi)
{
	if (strcmp(ddi_get_name(devi), "vme") == 0) {
		return (DDI_IDENTIFIED);
	}
	return (DDI_NOT_IDENTIFIED);
}

static int
vme_probe(dev_info_t *devi)
{
	/*
	 * sun4m is self-identifying
	 */
	if (ddi_dev_is_sid(devi) == DDI_SUCCESS) {
		return (DDI_PROBE_SUCCESS);
	} else {
		return (DDI_PROBE_FAILURE);
	}
}

/*ARGSUSED*/
static int
vme_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	/*
	 * We increment the instance number for each vme we see.
	 */
	ddi_set_driver_private(devi, (caddr_t)vme_list);
	vme_list = devi;

	/*
	 * A manifest constant of the sun4m architecture is
	 * that the VME range is the bottom 1MB for A24 and
	 * the bottom 8MB for A32. This gets mapped to the
	 * top range of the CPU's address space. Independent
	 * of this, cpu may consume some portion of this
	 * range for other purposes, but we'll let the root
	 * nexus deal with this.
	 */

	/* 0 - 8M */
	vme_base = (u_long)(0 - (8 * (1<<20)));
	vme_sizeA24 = (1<<20) - 1;
	vme_sizeA32 = (1<<23) - 1;

	ddi_report_dev(devi);

	return (DDI_SUCCESS);
}

static int
vme_add_intrspec(dev_info_t *dip, dev_info_t *rdip,
	ddi_intrspec_t intrspec, ddi_iblock_cookie_t *iblock_cookiep,
	ddi_idevice_cookie_t *idevice_cookiep,
	u_int (*int_handler)(caddr_t int_handler_arg),
	caddr_t int_handler_arg, int kind)
{
	struct intrspec *ispec;
	struct dev_ops *dops;
	int i, hot, r;
	register u_int pri;

	dops = ddi_get_driver(rdip);
	ASSERT(dops);

	if (dops->devo_bus_ops) {
		hot = 1;	/* Nexus driver MUST be MT-safe */
	} else if (dops->devo_cb_ops->cb_flag & D_MP) {
		hot = 1;	/* Most leaves are MT-safe */
	} else
		hot = 0;	/* MT-unsafe leaf drivers allowed (for now) */

	/*
	 * We only allow 'normal' interrupts from vme devices
	 *
	 * MJ: We *force* (right now) that all VME devices must
	 * MJ: have vectors.
	 */

	if (kind != IDDI_INTR_TYPE_NORMAL)
		return (DDI_FAILURE);

	ispec = (struct intrspec *)intrspec;
	pri = INT_IPL(ispec->intrspec_pri);

	if (ispec->intrspec_vec == 0) {
		printf("vme%d: device %s%d has no vector specified\n",
		    ddi_get_instance(dip),
		    ddi_get_name(rdip), ddi_get_instance(rdip));
		return (DDI_FAILURE);
	}

	/*
	 * XXX	Warning: Architecture-dependency - VEC_MIN and VEC_MAX
	 *	are properties of the implementation architecture.
	 *
	 *	We should probably just put 0 and 255 here and punt the
	 *	decision to the rootnexus recursively.
	 */
	if (ispec->intrspec_vec < VEC_MIN || ispec->intrspec_vec > VEC_MAX) {
		printf("vme%d: device %s%d has a bad vector specified (%d)\n",
		    ddi_get_instance(dip),
		    ddi_get_name(rdip), ddi_get_instance(rdip),
		    ispec->intrspec_vec);
		return (DDI_FAILURE);
	}

	/*
	 * Check with our parent nexus driver to make sure that
	 * we can actually be mt-unsafe with the ipl given here.
	 */
	if (hot == 0 &&
	    (ddi_ctlops(dip, rdip, DDI_CTLOPS_INTR_HILEVEL, ispec, &r)
	    != DDI_SUCCESS || r != 0)) {
		printf("vme%d: %s%d isn't mt-safe for sparc ipl %d\n",
		    ddi_get_instance(dip),
		    ddi_get_name(rdip), ddi_get_instance(rdip), pri);
		return (DDI_FAILURE);
	}

	/*
	 * following test is not completely correct
	 */
	i = ispec->intrspec_vec - VEC_MIN;
	if (vme_vector[i].func) {
		printf("vme%d: device %s%d vector 0x%x already in use\n",
		    ddi_get_instance(dip),
		    ddi_get_name(rdip), ddi_get_instance(rdip),
		    ispec->intrspec_vec);
		return (DDI_FAILURE);
	}

	/*
	 * Always add the handler function last
	 */
	vme_vector[i].mutex = (hot) ? (void *) 0: (void *) &unsafe_driver;
	vme_vector[i].arg = int_handler_arg;
	vme_vector[i].func = int_handler;

	ispec->intrspec_func = int_handler;

	if (iblock_cookiep) {
		*iblock_cookiep = (ddi_iblock_cookie_t)ipltospl(pri);
	}

	if (idevice_cookiep) {
		idevice_cookiep->idev_vector = ispec->intrspec_vec;
		idevice_cookiep->idev_priority = sparc_to_vmebus(pri);
	}
	return (DDI_SUCCESS);
}

/* ARGSUSED */
static void
vme_remove_intrspec(dev_info_t *dip, dev_info_t *rdip,
	ddi_intrspec_t intrspec, ddi_iblock_cookie_t iblock_cookie)
{
	struct intrspec *ispec = (struct intrspec *)intrspec;
	int i;

	if (ispec->intrspec_vec < VEC_MIN || ispec->intrspec_vec > VEC_MAX) {
		return;
	}

	i = ispec->intrspec_vec - VEC_MIN;
	if (vme_vector[i].func != ispec->intrspec_func) {
		return;
	}

	/*
	 * The low level VME interrupt handler interprets
	 * a zero func as a spurious interrupt vector. Do
	 * not load a vector to 'spurious' any more.
	 */
	vme_vector[i].func = 0;
	vme_vector[i].arg = 0;
	vme_vector[i].mutex = (void *) 0;
}

static int
vme_dma_map(dev_info_t *dip, dev_info_t *rdip,
    struct ddi_dma_req *dmareq, ddi_dma_handle_t *handlep)
{
	ddi_dma_lim_t *lim;
	int vme_size_used, rval;

	if (dmareq->dmar_flags & DDI_DMA_VME_USEA32) {
		vme_size_used = vme_sizeA32;
	} else {
		vme_size_used = vme_sizeA24;
	}

	/*
	 * This will not work for physical address requests. We are
	 * assuming that all objects going up this way are mappable
	 * in the kernel.
	 */
	if ((lim = dmareq->dmar_limits)->dlim_addr_lo >= vme_size_used)
		return (DDI_DMA_NOMAPPING);
	lim->dlim_addr_hi = (u_long) min(lim->dlim_addr_hi, vme_size_used);
	lim->dlim_addr_lo = lim->dlim_addr_lo | vme_base;
	lim->dlim_addr_hi = lim->dlim_addr_hi | vme_base;

	dmareq->dmar_flags |= DMP_VMEREQ;
	rval = ddi_dma_map(dip, rdip, dmareq, handlep);

	switch (rval) {
	case DDI_DMA_MAPPED:
	case DDI_DMA_PARTIAL_MAP:
		/*
		 * This test is necessary because advisory calls
		 * return DDI_DMA_MAPOK which has the same binary
		 * return value as DDI_DMA_MAPPED but no DMA
		 * handle attached.
		 */
		if (handlep) {
			vme_iocache_setup((ddi_dma_impl_t *)*handlep);
		}
		break;
	default:
		break;
	}

	return (rval);
}

static int
vme_dma_bindhdl(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle, struct ddi_dma_req *dmareq,
    ddi_dma_cookie_t *cp, u_int *ccountp)
{
	ddi_dma_impl_t *mp = (ddi_dma_impl_t *)handle;
	ddi_dma_attr_t *attr;
	int vme_size_used, rval;


	if (dmareq->dmar_flags & DDI_DMA_VME_USEA32) {
		vme_size_used = vme_sizeA32;
	} else {
		vme_size_used = vme_sizeA24;
	}

	attr = &mp->dmai_attr;
	if ((u_long)(attr->dma_attr_addr_lo) >= vme_size_used)
		return (DDI_DMA_NOMAPPING);
	attr->dma_attr_addr_hi =
		(u_long) min((u_long)attr->dma_attr_addr_hi, vme_size_used);
	attr->dma_attr_addr_lo = (u_long)attr->dma_attr_addr_lo | vme_base;
	attr->dma_attr_addr_hi = (u_long)attr->dma_attr_addr_hi | vme_base;

	dmareq->dmar_flags |= DMP_VMEREQ;
	rval = ddi_dma_bindhdl(dip, rdip, handle, dmareq, cp, ccountp);

	switch (rval) {
	case DDI_DMA_MAPPED:
	case DDI_DMA_PARTIAL_MAP:
		if (mp->dmai_rflags & DMP_PHYSADDR) {
			cp->dmac_size = mp->dmai_object.dmao_size;
			/*
			 * fix this
			 */
			*ccountp = 1;
			rval = pte2atype((void *)&mp->dmai_ndvmapages, 0,
			    &cp->dmac_address, &cp->dmac_type);
		} else {
			vme_iocache_setup(mp);

			/*
			 * adjust IO address to VME window
			 */
			cp->dmac_address -= vme_base;
			cp->dmac_type = SP_VME_A24D32_SUPV_D;
		}
		break;
	default:
		break;
	}
	return (rval);
}

static int
vme_dma_unbindhdl(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle)
{
	ddi_dma_impl_t *mp = (ddi_dma_impl_t *)handle;

	if (mp->dmai_rflags & DMP_PHYSADDR) {
		mp->dmai_ndvmapages = 0;
		mp->dmai_rflags |= DMP_INVALID;
	} else {
		if (mp->dmai_rflags & DDI_DMA_READ) {
			vme_iocache_flush(mp);
		}
	}
	return (ddi_dma_unbindhdl(dip, rdip, handle));
}

static int
vme_dma_flush(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle, off_t off, u_int len,
    u_int cache_flags)
{
	ddi_dma_impl_t *mp = (ddi_dma_impl_t *)handle;

	if (mp->dmai_rflags & DMP_PHYSADDR) {
		return (DDI_SUCCESS);
	} else {
		if (cache_flags == DDI_DMA_SYNC_FORKERNEL ||
		    cache_flags == DDI_DMA_SYNC_FORCPU) {
			if (mp->dmai_rflags & DDI_DMA_READ) {
				vme_iocache_flush(mp);
			}
		} else {
			vme_iocache_inval(mp);
		}
	}
	return (ddi_dma_flush(dip, rdip, handle, off, len, cache_flags));
}

static int
vme_dma_win(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle, uint_t win, off_t *offp,
    uint_t *lenp, ddi_dma_cookie_t *cookiep, uint_t *ccountp)
{
	ddi_dma_impl_t *mp = (ddi_dma_impl_t *)handle;
	int rval;

	if (mp->dmai_rflags & DMP_PHYSADDR)
		return (DDI_FAILURE);

	if (mp->dmai_rflags & DDI_DMA_READ)
		vme_iocache_flush(mp);

	rval = ddi_dma_win(dip, rdip, handle, win, offp, lenp,
			cookiep, ccountp);
	if (rval == 0) {
		vme_iocache_setup(mp);

		/*
		 * adjust IO address to VME window
		 */
		cookiep->dmac_address -= vme_base;
		cookiep->dmac_type = SP_VME_A24D32_SUPV_D;
	}
	return (rval);
}

static int
vme_dma_mctl(dev_info_t *dip, dev_info_t *rdip, ddi_dma_handle_t handle,
    enum ddi_dma_ctlops request, off_t *offp, u_int *lenp,
    caddr_t *objp, u_int flags)
{
	register ddi_dma_impl_t *mp = (ddi_dma_impl_t *)handle;
	register ddi_dma_cookie_t *cp;
	auto ddi_dma_cookie_t new_addr;
	int rval;

	if (mp->dmai_rflags & DMP_PHYSADDR) {
		u_long base, offset;

		switch (request) {
		case DDI_DMA_KVADDR:
			offset = (u_long) *offp;
			if (offset > mp->dmai_object.dmao_size)
				return (DDI_FAILURE);
			base = (u_long)
			    mp->dmai_object.dmao_obj.virt_obj.v_addr;
			*objp = (caddr_t)(base + *offp);
			return (DDI_SUCCESS);
		case DDI_DMA_FREE:
			mp->dmai_ndvmapages = 0;
			mp->dmai_rflags |= DMP_INVALID;
			return (ddi_dma_mctl(dip, rdip, handle, request,
			    offp, lenp, objp, flags));
		case DDI_DMA_SYNC:
		case DDI_DMA_GETERR:
			return (DDI_SUCCESS);
		case DDI_DMA_HTOC:
			offset = (u_long) *offp;
			if (offset > mp->dmai_object.dmao_size)
				return (DDI_FAILURE);
			cp = (ddi_dma_cookie_t *)objp;
			cp->dmac_size = mp->dmai_object.dmao_size - offset;
			return (pte2atype((void *)&mp->dmai_ndvmapages, offset,
			    &cp->dmac_address, &cp->dmac_type));
		case DDI_DMA_SEGTOC:
		{
			register ddi_dma_seg_impl_t *seg;

			seg = (ddi_dma_seg_impl_t *)handle;
			cp = (ddi_dma_cookie_t *)objp;
			cp->dmac_notused = 0;
			cp->dmac_size = *lenp = seg->dmai_size;
			*offp = seg->dmai_offset;
			return (pte2atype((void *)&seg->dmai_ndvmapages,
			    seg->dmai_offset, &cp->dmac_address,
			    &cp->dmac_type));
		}

		case DDI_DMA_COFF:
			/*
			 * XXX: fix this!
			 */
			return (DDI_FAILURE);
		case DDI_DMA_REPWIN:
		case DDI_DMA_MOVWIN:
		case DDI_DMA_NEXTWIN:
		default:
			return (DDI_FAILURE);
		}
	}

	switch (request) {
	case DDI_DMA_COFF:
		cp = (ddi_dma_cookie_t *)offp;
		new_addr = *cp;
		new_addr.dmac_address += vme_base;
		offp = (off_t *)&new_addr;
		break;
	case DDI_DMA_SYNC:
		if (flags == DDI_DMA_SYNC_FORKERNEL ||
		    flags == DDI_DMA_SYNC_FORCPU) {
			if (mp->dmai_rflags & DDI_DMA_READ) {
				vme_iocache_flush(mp);
			}
		} else {
			vme_iocache_inval(mp);
		}
		break;
	case DDI_DMA_FREE:
	case DDI_DMA_MOVWIN:
	case DDI_DMA_NEXTWIN:
		if (mp->dmai_rflags & DDI_DMA_READ) {
			vme_iocache_flush(mp);
		}
		break;
	default:
		break;
	}

	rval = ddi_dma_mctl(dip, rdip, handle, request, offp,
	    lenp, objp, flags);

	if (rval == 0) {
		switch (request) {
		case DDI_DMA_NEXTWIN:
			vme_iocache_setup(mp);
			break;
		case DDI_DMA_MOVWIN:
			vme_iocache_setup(mp);
			/*FALLTHROUGH*/
		case DDI_DMA_SEGTOC:
		case DDI_DMA_HTOC:
			/*
			 * Translate results from this implementation's
			 * IO base to VME address base. Also select the
			 * appropriate type bits.
			 */
			cp = (ddi_dma_cookie_t *)objp;
			if (cp) {
				cp->dmac_address -= vme_base;
				cp->dmac_type = SP_VME_A24D32_SUPV_D;
			}
			break;

		case DDI_DMA_RESERVE:
		{
			ddi_dma_impl_t *hp;

			hp = (ddi_dma_impl_t *)(*objp);
			hp->dmai_rflags &= ~DMP_BYPASSNEXUS;
			break;
		}
		default:
			break;
		}
	}
	return (rval);
}

/* #define VMEBUS_DEBUG */

#ifdef	VMEBUS_DEBUG
int vmebus_debug_flag;
#define	vmebus_debug   if (vmebus_debug_flag) printf
#endif	VMEBUS_DEBUG

static int
vme_bus_ctl(dev_info_t *dip, dev_info_t *rdip,
    ddi_ctl_enum_t opt, void *a, void *v)
{
	static int vme_dmapmap(dev_info_t *, void *);
	static int vme_reportdev(dev_info_t *, dev_info_t *);
	static int vme_xlate_intrs(dev_info_t *, dev_info_t *,
		int *, struct ddi_parent_private_data *);

	switch (opt) {
	case DDI_CTLOPS_INITCHILD:

		if (strcmp(ddi_get_name((dev_info_t *)a), "vmemem") == 0) {
			register dev_info_t *cdip = (dev_info_t *)a;
			register int i, n;
			int space, size, xfersize;
			char *ident;

			/* should we set DDI_PROP_NOTPROM? */

			space = ddi_getprop(DDI_DEV_T_NONE, cdip,
			    DDI_PROP_DONTPASS | DDI_PROP_CANSLEEP, "space", -1);
			if (space == -1) {
#ifdef	VMEBUS_DEBUG
				vmebus_debug("can't get space property\n");
#endif	VMEBUS_DEBUG
				return (DDI_FAILURE);
			}

#ifdef	VMEBUS_DEBUG
			vmebus_debug("number of range properties (spaces) %d\n",
			    sparc_pd_getnrng(dip));
#endif	VMEBUS_DEBUG

			for (i = 0, n = sparc_pd_getnrng(dip); i < n; i++) {
				struct rangespec *rp = sparc_pd_getrng(dip, i);

				if (rp->rng_cbustype == (u_int) space) {
					struct regspec r;

					/* create reg property */

					r.regspec_bustype = (u_int) space;
					r.regspec_addr = 0;
					r.regspec_size = rp->rng_size;
					(void) ddi_prop_create(DDI_DEV_T_NONE,
					    cdip, DDI_PROP_CANSLEEP, "reg",
					    (caddr_t)&r,
					    sizeof (struct regspec));

					switch (r.regspec_bustype) {

					case SP_VME16D16:
						ident = "a16d16";
						xfersize = sizeof (short);
						break;
					case SP_VME24D16:
						ident = "a24d16";
						xfersize = sizeof (short);
						break;
					case SP_VME32D16:
						ident = "a32d16";
						xfersize = sizeof (short);
						break;
					case SP_VME16D32:
						ident = "a16d32";
						xfersize = sizeof (long);
						break;
					case SP_VME24D32:
						ident = "a24d32";
						xfersize = sizeof (long);
						break;
					case SP_VME32D32:
						ident = "a32d32";
						xfersize = sizeof (long);
						break;
					default:
						break;

					}

#ifdef	VMEBUS_DEBUG
		vmebus_debug("range property: cbustype %x\n", rp->rng_cbustype);
		vmebus_debug("                coffset  %x\n", rp->rng_coffset);
		vmebus_debug("                bustype  %x\n", rp->rng_bustype);
		vmebus_debug("                offset   %x\n", rp->rng_offset);
		vmebus_debug("                size     %x\n\n", rp->rng_size);
		vmebus_debug("reg property:   bustype %x\n", r.regspec_bustype);
		vmebus_debug("                addr    %x\n", r.regspec_addr);
		vmebus_debug("                size    %x\n", r.regspec_size);
#endif	VMEBUS_DEBUG

					/* create size property for space */

					size = rp->rng_size;
					(void) ddi_prop_create(DDI_DEV_T_NONE,
					    cdip, DDI_PROP_CANSLEEP, "size",
					    (caddr_t)&size, sizeof (int));

					(void) ddi_prop_create(DDI_DEV_T_NONE,
					    cdip, DDI_PROP_CANSLEEP, "ident",
					    ident, strlen(ident) + 1);

					/* create xfersize property for space */

					(void) ddi_prop_create(DDI_DEV_T_NONE,
					    cdip, DDI_PROP_CANSLEEP, "xfersize",
					    (caddr_t)&xfersize, sizeof (int));

					return (impl_ddi_sunbus_initchild(a));
				}
			}
			return (DDI_FAILURE);
		}
		return (impl_ddi_sunbus_initchild(a));

	case DDI_CTLOPS_UNINITCHILD:
		impl_ddi_sunbus_removechild(a);
		return (DDI_SUCCESS);

	case DDI_CTLOPS_REPORTDEV:
		return (vme_reportdev(dip, rdip));

	case DDI_CTLOPS_XLATE_INTRS:
		return (vme_xlate_intrs(dip, rdip, a, v));

	case DDI_CTLOPS_SLAVEONLY:
		/*
		 * The 4/110 VME interface is a slave only interface
		 */
		if (cputype == CPU_SUN4_110)
			return (DDI_SUCCESS);
		else
			return (DDI_FAILURE);

	case DDI_CTLOPS_AFFINITY:
		/*
		 * We couldn't have gotten here unless dip is one
		 * of our children or grandchildren. The question
		 * we need to answer is whether dip is a direct
		 * child, and whether 'a' is a direct child.
		 */
		if (ddi_get_parent(rdip) == dip &&
		    ddi_get_parent((dev_info_t *)a) == dip)
			return (DDI_SUCCESS);
		else
			return (DDI_FAILURE);

	case DDI_CTLOPS_DVMAPAGESIZE:
		*(u_long *)v = 0x2000;
		return (DDI_SUCCESS);

	case DDI_CTLOPS_DMAPMAPC:
		return (vme_dmapmap(rdip, v));

		/*
		 * *sigh* - for now just do this based upon
	case DDI_CTLOPS_IOMIN:
		 */

		/*
		 * MJ: We probably need to do something here to check
		 * MJ: and make sure which width data bus the requestor
		 * MJ: is on. Fix later.
		 */
		/* FALLTHROUGH */
	default:
		return (ddi_ctlops(dip, rdip, opt, a, v));
	}
}

static int
vme_reportdev(dev_info_t *dev, dev_info_t *rdev)
{
	register char *name;
	register int i, n;
	register dev_info_t *pdev;

#ifdef	lint
	dev = dev;
#endif

	if (DEVI(rdev)->devi_parent_data == (caddr_t)0) {
		/*
		 * No registers or ranges
		 */
		return (DDI_FAILURE);
	}
	pdev = (dev_info_t *)DEVI(rdev)->devi_parent;
	cmn_err(CE_CONT, "?%s%d at %s%d",
	    DEVI(rdev)->devi_name, DEVI(rdev)->devi_instance,
	    DEVI(pdev)->devi_name, DEVI(pdev)->devi_instance);

	for (i = 0, n = sparc_pd_getnreg(rdev); i < n; i++) {

		register struct regspec *rp = sparc_pd_getreg(rdev, i);

		if (i == 0)
			cmn_err(CE_CONT, "?: ");
		else
			cmn_err(CE_CONT, "? and ");

		switch (rp->regspec_bustype) {

		case SP_VME16D16:
			name = "vme16d16";
			break;

		case SP_VME24D16:
			name = "vme24d16";
			break;

		case SP_VME32D16:
			name = "vme32d16";
			break;

		case SP_VME16D32:
			name = "vme16d32";
			break;

		case SP_VME24D32:
			name = "vme24d32";
			break;

		case SP_VME32D32:
			name = "vme32d32";
			break;

		default:
			cmn_err(CE_CONT, "?space 0x%x offset 0x%x\n",
			    rp->regspec_bustype, rp->regspec_addr);
			continue;
		}
		cmn_err(CE_CONT, "?%s 0x%x", name, rp->regspec_addr);
	}

	for (i = 0, n = sparc_pd_getnintr(rdev); i < n; i++) {

		register struct intrspec *intrp = sparc_pd_getintr(rdev, i);
		register int pri = INT_IPL(intrp->intrspec_pri);

		if (i == 0)
			cmn_err(CE_CONT, "? ");
		else
			cmn_err(CE_CONT, "?, ");

		cmn_err(CE_CONT, "?VME level %d vector 0x%x sparc ipl %d",
		    sparc_to_vmebus(pri), intrp->intrspec_vec, pri);
	}

	cmn_err(CE_CONT, "?\n");
	return (DDI_SUCCESS);
}


/*
 * We're prepared to claim that the interrupt string is in the
 * form of a list of <intr,vec> specifications.  Translate it.
 */
static int
vme_xlate_intrs(dev_info_t *dip, dev_info_t *rdip, int *in,
	struct ddi_parent_private_data *pdptr)
{
	register int n;
	register size_t size;
	register struct intrspec *new;

	static char bad_vmeintr_fmt[] =
	    "vme%d: bad interrupt spec for %s%d - VME level %d vec 0x%x\n";

	/*
	 * The list consists of <ipl,vec> elements
	 */
	if ((n = (*in++ >> 1)) < 1)
		return (DDI_FAILURE);

	pdptr->par_nintr = n;
	size = n * sizeof (struct intrspec);
	new = pdptr->par_intr = kmem_alloc(size, KM_SLEEP);

	while (n--) {
		register int level = *in++;
		register int vec = *in++;
		if (level < 1 || level > 7 || vec < VEC_MIN || vec > VEC_MAX) {
			cmn_err(CE_CONT, bad_vmeintr_fmt,
			    DEVI(dip)->devi_instance, DEVI(rdip)->devi_name,
			    DEVI(rdip)->devi_instance, level, vec);
			goto broken;
			/*NOTREACHED*/
		}
		new->intrspec_pri = vmebus_to_sparc(level) | INTLEVEL_VME;
		new->intrspec_vec = vec;
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

static void
vme_iocache_flush(ddi_dma_impl_t *mp)
{
	if (mp->dmai_rflags & DMP_IOCACHE) {
		u_long addr = mp->dmai_mapping & ~MMU_PAGEOFFSET;
		u_int tpfn = mmu_btop(addr);
		int tpages = mp->dmai_ndvmapages;
		while (tpages-- > 0) {
			ioc_flush(tpfn);
			tpfn++;
		}

		/*
		 * read tag once to make sure it's really
		 * flushed. per 4m spec.
		 */
		ioc_read_tag(--tpfn);
	}
}

static void
vme_iocache_inval(ddi_dma_impl_t *mp)
{
	u_long addr = mp->dmai_mapping & ~MMU_PAGEOFFSET;
	int tpages = mp->dmai_ndvmapages;
	u_int tpfn = mmu_btop(addr);
	u_int do_ioc;

	if (mp->dmai_rflags & DMP_IOCACHE)
		do_ioc = IOC_LINE_LDIOC;
	else
		do_ioc = IOC_LINE_NOIOC;

	while (tpages-- > 0) {
		ioc_setup(tpfn, (do_ioc |
		    ((mp->dmai_rflags & DDI_DMA_READ) ?
			IOC_LINE_WRITE : 0)));
		tpfn++;
	}
}

int vme_ioc = 0;
int vme_noioc = 0;

static void
vme_iocache_setup(ddi_dma_impl_t *mp)
{
	u_long ioaddr;
	int do_ioc, npages;

	npages = mp->dmai_ndvmapages;
	ioaddr = mp->dmai_mapping & ~MMU_PAGEOFFSET;
	if ((do_ioc = is_iocable(mp->dmai_rflags, mp->dmai_size,
		mp->dmai_mapping &
		    MMU_PAGEOFFSET)) == IOC_LINE_LDIOC) {
		mp->dmai_rflags |= DMP_IOCACHE;
		vme_ioc++;
	} else {
		vme_noioc++;
	}
	while (npages) {
		ioc_setup(mmu_btop(ioaddr), (do_ioc |
		    ((mp->dmai_rflags & DDI_DMA_READ) ?
		    IOC_LINE_WRITE : 0)));
		ioaddr += MMU_PAGESIZE;
		npages--;
	}
	if (do_ioc) {
		mp->dmai_minxfer =
			max(mp->dmai_minxfer, IOC_LINESIZE);
	}
}

/*ARGSUSED*/
static int
vme_dmapmap(dev_info_t *rdip, void *arg)
{
	struct dma_phys_mapc *pd = (struct dma_phys_mapc *)arg;

	if (pd->mp) {
		pd->mp->dmai_rflags |= DMP_PHYSADDR;
		pd->mp->dmai_ndvmapages = *((int *)pd->ptes);
		return (DDI_DMA_MAPPED);
	} else {
		return (DDI_DMA_MAPOK);
	}
}

/*
 * Any instance of a VME nexus driver that sits directly on a sun
 * platform but _doesn't_ use these translations shouldn't be called
 * 'vme' ..
 */

static int
vmebus_to_sparc(int vmelevel)
{
	static const char vme_to_sparc_tbl[] = {
		-1,	2,	3,	5,	7,	9,	11,	13
	};

	if (vmelevel < 1 || vmelevel > 7)
		return (-1);
	else
		return ((int)vme_to_sparc_tbl[vmelevel]);
}

static int
sparc_to_vmebus(int pri)
{
	register int vmelevel;

	for (vmelevel = 1; vmelevel <= 7; vmelevel++)
		if (vmebus_to_sparc(vmelevel) == pri)
			return (vmelevel);
	return (-1);
}

static int
map_addr_to_ioc_sel(int map_addr)
{
	int dvma_addr;

	dvma_addr = mmu_ptob(map_addr);
	return ((dvma_addr & IOC_ADDR_MSK) >> IOC_RW_SHIFT);
}

void
ioc_setup(int map_addr, int flags)
{
	int ioc_tag = 0;
	int ioc_addr;

	if (ioc) {
		ioc_addr = map_addr_to_ioc_sel(map_addr) | IOC_TAG_PHYS_ADDR;
		if (flags & IOC_LINE_LDIOC)
			ioc_tag = IOC_LINE_ENABLE;

		if (flags & IOC_LINE_WRITE)
			ioc_tag |= IOC_LINE_WRITE;
		do_load_ioc(ioc_addr, ioc_tag);
	}
}

void
ioc_flush(int map_addr)
{
	int ioc_addr;

	if (ioc) {
		ioc_addr = map_addr_to_ioc_sel(map_addr) | IOC_FLUSH_PHYS_ADDR;
		do_flush_ioc(ioc_addr);
	}
}

int
ioc_read_tag(int map_addr)
{
	int ioc_addr;
	int tv = 0;

	if (ioc) {
		ioc_addr = map_addr_to_ioc_sel(map_addr) | IOC_TAG_PHYS_ADDR;
		tv = do_read_ioc(ioc_addr);
	}
	return (tv);
}

/*
 * Check to see if this transaction can be IOC'ed.
 *
 * returns: IOC_LINE_INVALID: sbus/IOC is not on.
 *	  IOC_LINE_LDIOC:   vme & IOCable
 *	  IOC_LINE_NOIOC:   vme & NOT IOCable
 */
int
is_iocable(u_int dmai_rflags, u_int size, u_long offset)
{
	/* if ioc not present or disabled, don't think about it. */
	if (!ioc) {
		return (IOC_LINE_INVALID);
	}

	/* if consistent mode bit set, means we don't want IOC!! */
	if (dmai_rflags & DDI_DMA_CONSISTENT) {
		return (IOC_LINE_NOIOC);
	}

	/* DDI_DMA_WRITE does not require ioc_flush, always cache!! */
	if (!(dmai_rflags & DDI_DMA_READ)) {
		return (IOC_LINE_LDIOC);
	}

	/* if address badly aligned, dont cache it. */
	if ((u_int)(size|offset) & IOC_LINEMASK) {
		return (IOC_LINE_NOIOC);
	}

	/* properly aligned read: iocache it. */
	return (IOC_LINE_LDIOC);
}
