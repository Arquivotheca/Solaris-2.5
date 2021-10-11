/*
 * Copyright (c) 1990-1994, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)rootnex.c	1.50	95/01/12 SMI"

/*
 * sun4m root nexus driver
 *
 * XXX	Now that we no longer handle DMA in this nexus
 *	many of the includes below should be omitted.
 */

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/sysmacros.h>
#include <sys/debug.h>
#include <sys/spl.h>
#include <sys/scb.h>
#include <sys/mmu.h>
#include <sys/pte.h>
#include <sys/cpu.h>
#include <sys/kmem.h>
#include <sys/cmn_err.h>
#include <vm/seg.h>
#include <vm/seg_kmem.h>
#include <vm/seg_dev.h>
#include <sys/map.h>
#include <sys/mman.h>
#include <vm/hat.h>
#include <vm/as.h>
#include <vm/page.h>
#include <sys/vmmac.h>
#include <sys/avintr.h>

#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/modctl.h>
#include <sys/bt.h>
#include <sys/ddi_impldefs.h>
#include <sys/ddi_subrdefs.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>

extern nodev(), nulldev();
extern int impl_bustype(u_int);

/*
 * Hack to handle poke faults
 */
extern int pokefault;
static kmutex_t pokefault_mutex;

/*
 * config information
 */

static int
rootnex_map(dev_info_t *dip, dev_info_t *rdip, ddi_map_req_t *mp,
    off_t offset, off_t len, caddr_t *vaddrp);

static ddi_intrspec_t
rootnex_get_intrspec(dev_info_t *dip, dev_info_t *rdip, u_int inumber);

static int
rootnex_add_intrspec(dev_info_t *dip, dev_info_t *rdip,
    ddi_intrspec_t intrspec, ddi_iblock_cookie_t *iblock_cookiep,
    ddi_idevice_cookie_t *idevice_cookiep,
    u_int (*int_handler)(caddr_t int_handler_arg),
    caddr_t int_handler_arg, int kind);

static void
rootnex_remove_intrspec(dev_info_t *dip, dev_info_t *rdip,
    ddi_intrspec_t intrspec, ddi_iblock_cookie_t iblock_cookie);

static int
rootnex_map_fault(dev_info_t *dip, dev_info_t *rdip,
    struct hat *hat, struct seg *seg, caddr_t addr,
    struct devpage *dp, u_int pfn, u_int prot, u_int lock);

static int
rootnex_ctlops(dev_info_t *, dev_info_t *, ddi_ctl_enum_t, void *, void *);

static struct bus_ops rootnex_bus_ops = {
	BUSO_REV,
	rootnex_map,
	rootnex_get_intrspec,
	rootnex_add_intrspec,
	rootnex_remove_intrspec,
	rootnex_map_fault,
	ddi_no_dma_map,		/* all DMA vectors are in iommu nexus */
	ddi_no_dma_allochdl,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	rootnex_ctlops,
	ddi_bus_prop_op,
};

static int rootnex_identify(dev_info_t *devi);
static int rootnex_attach(dev_info_t *devi, ddi_attach_cmd_t cmd);

static struct dev_ops rootnex_ops = {
	DEVO_REV,
	0,		/* refcnt */
	ddi_no_info,	/* info */
	rootnex_identify,
	0,		/* probe */
	rootnex_attach,
	nodev,		/* detach */
	nodev,		/* reset */
	0,		/* cb_ops */
	&rootnex_bus_ops
};

extern struct mod_ops mod_driverops;
extern struct dev_ops rootnex_ops;

/*
 * Module linkage information for the kernel.
 */

static struct modldrv modldrv = {
	&mod_driverops,	/* Type of module.  This one is a nexus driver */
	"sun4m root nexus",
	&rootnex_ops,	/* Driver ops */
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
_fini(void)
{
	return (EBUSY);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

/*
 * rootnex_identify:
 *
 * 	identify the root nexus.
 */
static int
rootnex_identify(dev_info_t *devi)
{
	if (ddi_root_node() == devi)
		return (DDI_IDENTIFIED);
	else
		return (DDI_NOT_IDENTIFIED);
}

/*
 * rootnex_attach:
 *
 *	attach the root nexus.
 */
/*ARGSUSED*/
static int
rootnex_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	mutex_init(&pokefault_mutex, "pokefault lock",
	    MUTEX_SPIN_DEFAULT, (void *)ipltospl(15));

	cmn_err(CE_CONT, "?root nexus = %s\n", ddi_get_name(devi));
	return (DDI_SUCCESS);
}


/*
 * #define DDI_MAP_DEBUG (c.f. ddi_impl.c)
 */
#ifdef	DDI_MAP_DEBUG
extern int ddi_map_debug_flag;
#define	ddi_map_debug	if (ddi_map_debug_flag) printf
#endif	DDI_MAP_DEBUG


static int
rootnex_map_regspec(ddi_map_req_t *mp, caddr_t *vaddrp)
{
	extern struct seg kvseg;
	u_long base, a, cvaddr;
	u_int npages, pfn, pgoffset, bt;
	register struct regspec *rp = mp->map_obj.rp;

	base = (u_long)rp->regspec_addr & (~MMU_PAGEOFFSET); /* base addr */

	/*
	 * Take the bustype and the physical page base within the
	 * bus space and turn it into a 24 bit page frame number.
	 */
	pfn = PTE_BUSTYPE_PFN(rp->regspec_bustype, mmu_btop(base));

	/*
	 * This check insulates us from the havoc caused by busted hwconf files
	 */
	if ((bt = impl_bustype(pfn)) == BT_DRAM || bt == BT_UNKNOWN)
		return (DDI_ME_INVAL);

	if (rp->regspec_size == 0) {
#ifdef  DDI_MAP_DEBUG
		ddi_map_debug("rootnex_map_regspec: zero regspec_size\n");
#endif  DDI_MAP_DEBUG
		return (DDI_ME_INVAL);
	}

	pgoffset = (u_long)rp->regspec_addr & MMU_PAGEOFFSET; /* offset */
	npages = mmu_btopr(rp->regspec_size + pgoffset);

#ifdef	DDI_MAP_DEBUG
	ddi_map_debug("rootnex_map_regspec: Mapping %d pages physical %x.%x ",
	    npages, rp->regspec_bustype, base);
#endif	DDI_MAP_DEBUG

	a = rmalloc(kernelmap, (long)npages);
	if (a == NULL) {
		return (DDI_ME_NORESOURCES);
	}
	cvaddr = (u_long)kmxtob(a);

	/*
	 * Now map in the pages we've allocated...
	 */
	segkmem_mapin(&kvseg, (caddr_t)cvaddr,
	    (u_int)mmu_ptob(npages), mp->map_prot, pfn, 0);

	*vaddrp = (caddr_t)(kmxtob(a) + pgoffset);
#ifdef	DDI_MAP_DEBUG
	ddi_map_debug("at virtual 0x%x\n", *vaddrp);
#endif	DDI_MAP_DEBUG
	return (0);
}

static int
rootnex_unmap_regspec(ddi_map_req_t *mp, caddr_t *vaddrp)
{
	extern struct seg kvseg;
	caddr_t addr = (caddr_t)*vaddrp;
	u_int npages, pgoffset;
	register struct regspec *rp;
	long a;

	rp = mp->map_obj.rp;
	pgoffset = (u_int)addr & MMU_PAGEOFFSET;

	if (rp->regspec_size == 0) {
#ifdef  DDI_MAP_DEBUG
		ddi_map_debug("rootnex_unmap_regspec: zero regspec_size\n");
#endif  DDI_MAP_DEBUG
		return (DDI_ME_INVAL);
	}

	npages = mmu_btopr(rp->regspec_size + pgoffset);
	segkmem_mapout(&kvseg, (caddr_t)((int)addr & (~MMU_PAGEOFFSET)),
	    (u_int)mmu_ptob(npages));
	a = btokmx(addr);
	rmfree(kernelmap, (long)npages, (u_long)a);

	/*
	 * Destroy the pointer - the mapping has logically gone
	 */
	*vaddrp = (caddr_t)0;

	return (0);
}

static int
rootnex_map_handle(ddi_map_req_t *mp)
{
	ddi_acc_hdl_t *hp;
	u_long base;
	u_int pgoffset;
	u_int flags;
	register struct regspec *rp;

	/*
	 * Set up the hat_flags for the mapping.
	 */
	hp = mp->map_handlep;

	hp->ah_hat_flags = 0;
	flags = hp->ah_acc.devacc_attr_endian_flags &
				(DDI_NEVERSWAP_ACC| DDI_STRUCTURE_LE_ACC|
					DDI_STRUCTURE_BE_ACC);
	switch (flags) {
	case DDI_NEVERSWAP_ACC:
		break;
	case DDI_STRUCTURE_BE_ACC:
		hp->ah_hat_flags |= HAT_STRUCTURE_BE;
		break;
	case DDI_STRUCTURE_LE_ACC:
		return (DDI_FAILURE);
	default:
		return (DDI_REGS_ACC_CONFLICT);
	}

	flags = hp->ah_acc.devacc_attr_dataorder;
	if (flags & DDI_UNORDERED_OK_ACC)
		hp->ah_hat_flags |= HAT_UNORDERED_OK;
	if (flags & DDI_MERGING_OK_ACC)
		hp->ah_hat_flags |= HAT_MERGING_OK;
	if (flags & DDI_LOADCACHING_OK_ACC)
		hp->ah_hat_flags |= HAT_LOADCACHING_OK;
	if (flags & DDI_STORECACHING_OK_ACC)
		hp->ah_hat_flags |= HAT_LOADCACHING_OK;

	rp = mp->map_obj.rp;
	base = (u_long)rp->regspec_addr & (~MMU_PAGEOFFSET); /* base addr */
	pgoffset = (u_long)rp->regspec_addr & MMU_PAGEOFFSET; /* offset */

	if (rp->regspec_size == 0)
		return (DDI_ME_INVAL);

	hp->ah_pfn = mmu_btop(base);
	hp->ah_pnum = mmu_btopr(rp->regspec_size + pgoffset);

	return (DDI_SUCCESS);
}

static int
rootnex_map(dev_info_t *dip, dev_info_t *rdip, ddi_map_req_t *mp,
	off_t offset, off_t len, caddr_t *vaddrp)
{
	struct regspec *rp, tmp_reg;
	ddi_map_req_t mr = *mp;		/* Get private copy of request */
	int error;

	mp = &mr;

	switch (mp->map_op)  {
	case DDI_MO_MAP_LOCKED:
	case DDI_MO_UNMAP:
	case DDI_MO_MAP_HANDLE:
		break;
	default:
#ifdef	DDI_MAP_DEBUG
		cmn_err(CE_WARN, "rootnex_map: unimplemented map op %d.",
		    mp->map_op);
#endif	DDI_MAP_DEBUG
		return (DDI_ME_UNIMPLEMENTED);
	}

	if (mp->map_flags & DDI_MF_USER_MAPPING)  {
#ifdef	DDI_MAP_DEBUG
		cmn_err(CE_WARN, "rootnex_map: unimplemented map type: user.");
#endif	DDI_MAP_DEBUG
		return (DDI_ME_UNIMPLEMENTED);
	}

	/*
	 * First, if given an rnumber, convert it to a regspec...
	 * (Presumably, this is on behalf of a child of the root node?)
	 */

	if (mp->map_type == DDI_MT_RNUMBER)  {

		int rnumber = mp->map_obj.rnumber;
#ifdef	DDI_MAP_DEBUG
		static char *out_of_range =
		    "rootnex_map: Out of range rnumber <%d>, device <%s>";
#endif	DDI_MAP_DEBUG

		rp = i_ddi_rnumber_to_regspec(rdip, rnumber);
		if (rp == (struct regspec *)0)  {
#ifdef	DDI_MAP_DEBUG
			cmn_err(CE_WARN, out_of_range, rnumber,
			    ddi_get_name(rdip));
#endif	DDI_MAP_DEBUG
			return (DDI_ME_RNUMBER_RANGE);
		}

		/*
		 * Convert the given ddi_map_req_t from rnumber to regspec...
		 */

		mp->map_type = DDI_MT_REGSPEC;
		mp->map_obj.rp = rp;
	}

	/*
	 * Adjust offset and length correspnding to called values...
	 * XXX: A non-zero length means override the one in the regspec
	 * XXX: regardless of what's in the parent's range?.
	 */

	tmp_reg = *(mp->map_obj.rp);		/* Preserve underlying data */
	rp = mp->map_obj.rp = &tmp_reg;		/* Use tmp_reg in request */

	rp->regspec_addr += (u_int)offset;
	if (len != 0)
		rp->regspec_size = (u_int)len;

	/*
	 * Apply any parent ranges at this level, if applicable.
	 * (This is where nexus specific regspec translation takes place.
	 * Use of this function is implicit agreement that translation is
	 * provided via ddi_apply_range.)
	 */

#ifdef	DDI_MAP_DEBUG
	ddi_map_debug("applying range of parent <%s> to child <%s>...\n",
	    ddi_get_name(dip), ddi_get_name(rdip));
#endif	DDI_MAP_DEBUG

	if ((error = i_ddi_apply_range(dip, rdip, mp->map_obj.rp)) != 0)
		return (error);

	switch (mp->map_op)  {
	case DDI_MO_MAP_LOCKED:

		/*
		 * Set up the locked down kernel mapping to the regspec...
		 */
		error = rootnex_map_regspec(mp, vaddrp);

		/*
		 * Fill in the access handle if needed.
		 */
		if ((error == 0) && (mp->map_handlep))
			impl_acc_hdl_init((ddi_acc_hdl_t *) mp->map_handlep);

		return (error);

	case DDI_MO_UNMAP:

		/*
		 * Release mapping...
		 */

		return (rootnex_unmap_regspec(mp, vaddrp));

	case DDI_MO_MAP_HANDLE:

		return (rootnex_map_handle(mp));

	}

	return (DDI_ME_UNIMPLEMENTED);
}

/*
 * rootnex_get_intrspec: rootnex convert an interrupt number to an interrupt
 *			specification. The interrupt number determines
 *			which interrupt spec will be returned if more than
 *			one exists. Look into the parent private data
 *			area of the dev_info structure to find the interrupt
 *			specification.  First check to make sure there is
 *			one that matchs "inumber" and then return a pointer
 *			to it.  Return NULL if one could not be found.
 */
static ddi_intrspec_t
rootnex_get_intrspec(dev_info_t *dip, dev_info_t *rdip, u_int inumber)
{
	struct ddi_parent_private_data *ppdptr;

#ifdef	lint
	dip = dip;
#endif

	/*
	 * convert the parent private data pointer in the childs dev_info
	 * structure to a pointer to a sunddi_compat_hack structure
	 * to get at the interrupt specifications.
	 */
	ppdptr = (struct ddi_parent_private_data *)
	    (DEVI(rdip))->devi_parent_data;

	/*
	 * validate the interrupt number.
	 */
	if (inumber >= ppdptr->par_nintr) {
		return (NULL);
	}

	/*
	 * return the interrupt structure pointer.
	 */
	return ((ddi_intrspec_t)&ppdptr->par_intr[inumber]);
}

/*
 * rootnex_add_intrspec:
 *
 *	Add an interrupt specification.
 */
static int
rootnex_add_intrspec(dev_info_t *dip, dev_info_t *rdip,
	ddi_intrspec_t intrspec, ddi_iblock_cookie_t *iblock_cookiep,
	ddi_idevice_cookie_t *idevice_cookiep,
	u_int (*int_handler)(caddr_t int_handler_arg),
	caddr_t int_handler_arg, int kind)
{
	register struct intrspec *ispec;
	register u_int pri;

#ifdef	lint
	dip = dip;
#endif

	ispec = (struct intrspec *)intrspec;
	pri = INT_IPL(ispec->intrspec_pri);

	if (kind == IDDI_INTR_TYPE_FAST) {
		if (!settrap(rdip, ispec->intrspec_pri, int_handler)) {
			return (DDI_FAILURE);
		}
		ispec->intrspec_func = (u_int (*)()) 1;
	} else {
		struct dev_ops *dops = DEVI(rdip)->devi_ops;
		int hot;

		if (dops->devo_bus_ops) {
			hot = 1;	/* Nexus drivers MUST be MT-safe */
		} else if (dops->devo_cb_ops->cb_flag & D_MP) {
			hot = 1;	/* Most leaves are MT-safe */
		} else {
			hot = 0;	/* MT-unsafe drivers ok (for now) */
		}

		/*
		 * Convert 'soft' pri to "fit" with 4m model
		 */
		if (kind == IDDI_INTR_TYPE_SOFT)
			ispec->intrspec_pri = pri + INTLEVEL_SOFT;

		if (!add_avintr(rdip, ispec->intrspec_pri,
		    int_handler, int_handler_arg,
		    (hot) ? NULL : &unsafe_driver)) {
			return (DDI_FAILURE);
		}
		ispec->intrspec_func = int_handler;
	}

	if (iblock_cookiep) {
		*iblock_cookiep = (ddi_iblock_cookie_t)ipltospl(pri);
	}

	if (idevice_cookiep) {
		idevice_cookiep->idev_vector = 0;
		if (kind == IDDI_INTR_TYPE_SOFT) {
			idevice_cookiep->idev_softint = pri;
		} else {
			/*
			 * The idevice cookie contains the priority as
			 * understood by the device itself on the bus it
			 * lives on.  Let the nexi beneath sort out the
			 * translation (if any) that's needed.
			 */
			idevice_cookiep->idev_priority = (u_short)pri;
		}
	}

	return (DDI_SUCCESS);
}

/*
 * rootnex_remove_intrspec:
 *
 *	remove an interrupt specification.
 *
 */
/*ARGSUSED*/
static void
rootnex_remove_intrspec(dev_info_t *dip, dev_info_t *rdip,
	ddi_intrspec_t intrspec, ddi_iblock_cookie_t iblock_cookie)
{
	struct intrspec *ispec = (struct intrspec *)intrspec;

	if (ispec->intrspec_func == (u_int (*)()) 0) {
		return;
	} else if (ispec->intrspec_func == (u_int (*)()) 1) {
		(void) settrap(rdip, ispec->intrspec_pri, NULL);
	} else {
		rem_avintr(rdip, ispec->intrspec_pri, ispec->intrspec_func);
	}
	ispec->intrspec_func = (u_int (*)()) 0;
}

/*
 * Shorthand defines
 */

#define	DMAOBJ_PP_PP	dmao_obj.pp_obj.pp_pp
#define	DMAOBJ_PP_OFF	dmao_ogj.pp_obj.pp_offset
#define	ALO		dma_lim->dlim_addr_lo
#define	AHI		dma_lim->dlim_addr_hi
#define	OBJSIZE		dmareq->dmar_object.dmao_size
#define	ORIGVADDR	dmareq->dmar_object.dmao_obj.virt_obj.v_addr
#define	RED		((mp->dmai_rflags & DDI_DMA_REDZONE)? 1 : 0)
#define	DIRECTION	(mp->dmai_rflags & DDI_DMA_RDWR)

/*
 * rootnex_map_fault:
 *
 *	fault in mappings for requestors
 */

/*ARGSUSED*/
static int
rootnex_map_fault(dev_info_t *dip, dev_info_t *rdip,
	struct hat *hat, struct seg *seg, caddr_t addr,
	struct devpage *dp, u_int pfn, u_int prot, u_int lock)
{
	extern struct seg kvseg;
	extern struct seg_ops segdev_ops;

#ifdef	DDI_MAP_DEBUG
	ddi_map_debug("rootnex_map_fault: address <%x> pfn <%x>", addr, pfn);
	ddi_map_debug(" Seg <%s>\n",
	    seg->s_ops == &segdev_ops ? "segdev" :
	    seg == &kvseg ? "segkmem" : "NONE!");
#endif	DDI_MAP_DEBUG

	/*
	 * This is all terribly broken, but it is a start
	 *
	 * XXX	Note that this test means that segdev_ops
	 *	must be exported from seg_dev.c.
	 * XXX	What about devices with their own segment drivers?
	 */
	if (seg->s_ops == &segdev_ops) {
		register struct segdev_data *sdp =
			(struct segdev_data *)seg->s_data;

		if (hat == NULL) {
			/*
			 * This is one plausible interpretation of
			 * a null hat i.e. use the first hat on the
			 * address space hat list which by convention is
			 * the hat of the system MMU.  At alternative
			 * would be to panic .. this might well be better ..
			 */
			ASSERT(AS_READ_HELD(seg->s_as, &seg->s_as->a_lock));
			hat = seg->s_as->a_hat;
			cmn_err(CE_NOTE, "rootnex_map_fault: nil hat");
		}
		hat_devload(hat, seg->s_as, addr, dp, pfn, prot,
		    (lock ? HAT_LOCK : HAT_LOAD) | sdp->hat_flags);
	} else if (seg == &kvseg && dp == (struct devpage *)0) {
		segkmem_mapin(seg, (caddr_t)addr, (u_int)MMU_PAGESIZE,
		    prot, pfn, 0);
	} else
		return (DDI_FAILURE);
	return (DDI_SUCCESS);
}

/*
 * Root nexus ctl functions
 */

static int
rootnex_ctl_reportdev(dev_info_t *dev)
{
	register int i, n;
	register char *name;

	cmn_err(CE_CONT, "?%s%d at root", DEVI(dev)->devi_name,
	    DEVI(dev)->devi_instance);

	for (i = 0, n = sparc_pd_getnreg(dev); i < n; i++) {

		register struct regspec *rp = sparc_pd_getreg(dev, i);

		if (i == 0)
			cmn_err(CE_CONT, "?: ");
		else
			cmn_err(CE_CONT, "? and ");

		switch (impl_bustype(PTE_BUSTYPE_PFN(rp->regspec_bustype,
		    mmu_btop(rp->regspec_addr)))) {

		case BT_OBIO:
			name = "obio";
			break;

		default:
			cmn_err(CE_CONT, "?space %x offset %x",
			    rp->regspec_bustype, rp->regspec_addr);
			continue;
		}
		cmn_err(CE_CONT, "?%s 0x%x", name, rp->regspec_addr);
	}

	/*
	 * We'd report interrupts here if any of our immediate
	 * children had any.
	 */
	cmn_err(CE_CONT, "?\n");
	return (DDI_SUCCESS);
}

/*ARGSUSED*/
static int
rootnex_ctl_children(dev_info_t *dip, dev_info_t *rdip, ddi_ctl_enum_t ctlop,
    dev_info_t *child)
{
	extern int impl_ddi_sunbus_initchild(dev_info_t *);
	extern void impl_ddi_sunbus_removechild(dev_info_t *);

	switch (ctlop)  {

	case DDI_CTLOPS_INITCHILD:
		return (impl_ddi_sunbus_initchild(child));

	case DDI_CTLOPS_UNINITCHILD:
		impl_ddi_sunbus_removechild(child);
		return (DDI_SUCCESS);
	}

	return (DDI_FAILURE);
}


static int
rootnex_ctlops(dev_info_t *dip, dev_info_t *rdip,
    ddi_ctl_enum_t ctlop, void *arg, void *result)
{
	extern void flush_writebuffers_to(caddr_t);

	register int n, *ptr;
	register struct ddi_parent_private_data *pdp;

	switch (ctlop) {
	case DDI_CTLOPS_DMAPMAPC:
		return (DDI_FAILURE);

	case DDI_CTLOPS_BTOP:
		/*
		 * Convert byte count input to physical page units.
		 * (byte counts that are not a page-size multiple
		 * are rounded down)
		 */
		*(u_long *)result = btop(*(u_long *)arg);
		return (DDI_SUCCESS);

	case DDI_CTLOPS_PTOB:
		/*
		 * Convert size in physical pages to bytes
		 */
		*(u_long *)result = ptob(*(u_long *)arg);
		return (DDI_SUCCESS);

	case DDI_CTLOPS_BTOPR:
		/*
		 * Convert byte count input to physical page units
		 * (byte counts that are not a page-size multiple
		 * are rounded up)
		 */
		*(u_long *)result = btopr(*(u_long *)arg);
		return (DDI_SUCCESS);

	/*
	 * XXX	This pokefault_mutex clutter needs to be done differently.
	 *	Note that i_ddi_poke() calls this routine in the order
	 *	INIT then optionally FLUSH then always FINI.
	 */
	case DDI_CTLOPS_POKE_INIT:
		mutex_enter(&pokefault_mutex);
		pokefault = -1;
		return (DDI_SUCCESS);

	case DDI_CTLOPS_POKE_FLUSH:
		flush_writebuffers_to(arg);
		return (pokefault == 1 ? DDI_FAILURE : DDI_SUCCESS);

	case DDI_CTLOPS_POKE_FINI:
		pokefault = 0;
		mutex_exit(&pokefault_mutex);
		return (DDI_SUCCESS);

	case DDI_CTLOPS_INITCHILD:
	case DDI_CTLOPS_UNINITCHILD:
		return (rootnex_ctl_children(dip, rdip, ctlop, arg));

	case DDI_CTLOPS_REPORTDEV:
		return (rootnex_ctl_reportdev(rdip));

	case DDI_CTLOPS_IOMIN:
		/*
		 * Nothing to do here but reflect back..
		 */
		return (DDI_SUCCESS);

	case DDI_CTLOPS_REGSIZE:
	case DDI_CTLOPS_NREGS:
	case DDI_CTLOPS_NINTRS:
		break;

	case DDI_CTLOPS_SIDDEV:
		/*
		 * Oh, a hack...
		 */
		if (ddi_get_nodeid(rdip) != DEVI_PSEUDO_NODEID)
			return (DDI_SUCCESS);
		else
			return (DDI_FAILURE);

	case DDI_CTLOPS_INTR_HILEVEL:
		/*
		 * Indicate whether the interrupt specified is to be handled
		 * above lock level.  In other words, above the level that
		 * cv_signal and default type mutexes can be used.
		 */
		*(int *)result =
		    (INT_IPL(((struct intrspec *)arg)->intrspec_pri)
		    > LOCK_LEVEL);
		return (DDI_SUCCESS);

	case DDI_CTLOPS_POWER:
	{
		int		(*pwr_fn)(dev_info_t *, int, int);
		int		length = sizeof (pwr_fn);
		power_req	*req = (power_req *)arg;

		if (ddi_prop_op(DDI_DEV_T_ANY, dip,
		    PROP_LEN_AND_VAL_BUF, DDI_PROP_DONTPASS, "platform-pm",
		    (caddr_t)&pwr_fn, &length) != DDI_PROP_SUCCESS)
			return (DDI_FAILURE);
		return ((*pwr_fn)(req->who, req->cmpt, req->level));
	}
	default:
		return (DDI_FAILURE);
	}

	/*
	 * The rest are for "hardware" properties
	 */

	pdp = (struct ddi_parent_private_data *)
	    (DEVI(rdip))->devi_parent_data;

	if (!pdp) {
		return (DDI_FAILURE);
	} else if (ctlop == DDI_CTLOPS_NREGS) {
		ptr = (int *)result;
		*ptr = pdp->par_nreg;
	} else if (ctlop == DDI_CTLOPS_NINTRS) {
		ptr = (int *)result;
		*ptr = pdp->par_nintr;
	} else {
		off_t *size = (off_t *)result;

		ptr = (int *)arg;
		n = *ptr;
		if (n > pdp->par_nreg) {
			return (DDI_FAILURE);
		}
		*size = (off_t)pdp->par_reg[n].regspec_size;
	}
	return (DDI_SUCCESS);
}
