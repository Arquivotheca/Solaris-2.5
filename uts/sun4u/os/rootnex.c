/*
 * Copyright (c) 1990-1995, by Sun Microsystems, Inc.
 */

#pragma ident   "@(#)rootnex.c 1.37     95/08/01 SMI"

/*
 * sun4u root nexus driver
 *
 * XXX	Now that we no longer handle DMA in this nexus
 *	many of the includes below should be omitted.
 */

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/debug.h>
#include <sys/devops.h>
#include <sys/sunddi.h>
#include <sys/obpdefs.h>
#include <sys/ddi_impldefs.h>
#include <sys/ddi_implfuncs.h>
#include <sys/kmem.h>
#include <sys/cmn_err.h>
#include <vm/seg_kmem.h>
#include <vm/seg_dev.h>
#include <sys/map.h>
#include <sys/vmmac.h>
#include <sys/machparam.h>
#include <sys/ivintr.h>
#include <sys/intr.h>
#include <sys/modctl.h>
#include <sys/errno.h>
#include <sys/spl.h>
#include <sys/sysiosbus.h>

/* Useful debugging Stuff */
#include <sys/nexusdebug.h>
#define	ROOTNEX_MAP_DEBUG		0x1
#define	ROOTNEX_INTR_DEBUG		0x2

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
	ddi_no_dma_map,		/* no rootnex_dma_map- now in sysio nexus */
	ddi_no_dma_allochdl,
	ddi_no_dma_freehdl,
	ddi_no_dma_bindhdl,
	ddi_no_dma_unbindhdl,
	ddi_no_dma_flush,
	ddi_no_dma_win,
	ddi_no_dma_mctl,	/* no rootnex_dma_mctl- now in sysio nexus */
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
extern char Sysbase[];

/*
 * Module linkage information for the kernel.
 */

static struct modldrv modldrv = {
	&mod_driverops,	/* Type of module.  This one is a nexus driver */
	"sun4u root nexus",
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
	int length;
	char *valuep = 0;

	mutex_init(&pokefault_mutex, "pokefault lock",
	    MUTEX_SPIN_DEFAULT, (void *)ipltospl(15));

	if (ddi_prop_op(DDI_DEV_T_ANY, devi, PROP_LEN_AND_VAL_ALLOC,
		DDI_PROP_DONTPASS, "banner-name", (caddr_t) &valuep, &length)
			== DDI_PROP_SUCCESS) {
			cmn_err(CE_CONT, "?root nexus = %s\n", valuep);
			kmem_free((void *) valuep, (size_t) length);
	}
	return (DDI_SUCCESS);
}


static int
rootnex_map_regspec(ddi_map_req_t *mp, caddr_t *vaddrp, u_int mapping_flags)
{
	extern struct seg kvseg;
	u_long base, a, cvaddr;
	u_int npages, pfn, pgoffset;
	register struct regspec *rp = mp->map_obj.rp;

	base = (u_long) rp->regspec_addr & (~MMU_PAGEOFFSET); /* base addr */

	/*
	 * Take the bustype and the physical page base within the
	 * bus space and turn it into a 28 bit page frame number.
	 */
	pfn = BUSTYPE_TO_PFN(rp->regspec_bustype, mmu_btop(base));

	/*
	 * Do a quick sanity check to make sure we are in I/O space.
	 */
	if (!IO_BUSTYPE(pfn))
		return (DDI_ME_INVAL);

	if (rp->regspec_size == 0) {
		DPRINTF(ROOTNEX_MAP_DEBUG, ("rootnex_map_regspec: zero "
		    "regspec_size\n"));
		return (DDI_ME_INVAL);
	}

	pgoffset = (u_long) rp->regspec_addr & MMU_PAGEOFFSET; /* offset */
	npages = mmu_btopr(rp->regspec_size + pgoffset);

	DPRINTF(ROOTNEX_MAP_DEBUG, ("rootnex_map_regspec: Mapping %d pages "
	    "physical %x.%x ", npages, rp->regspec_bustype, base));

	a = rmalloc(kernelmap, (long) npages);
	if (a == NULL) {
		return (DDI_ME_NORESOURCES);
	}
	cvaddr = (u_long) kmxtob(a);

	/*
	 * Now map in the pages we've allocated...
	 */
	segkmem_mapin(&kvseg, (caddr_t) cvaddr,
	    (u_int) mmu_ptob(npages), mp->map_prot, pfn, mapping_flags);

	*vaddrp = (caddr_t) (kmxtob(a) + pgoffset);
	DPRINTF(ROOTNEX_MAP_DEBUG, ("at virtual 0x%x\n", *vaddrp));
	return (0);
}

static int
rootnex_unmap_regspec(ddi_map_req_t *mp, caddr_t *vaddrp)
{
	extern struct seg kvseg;
	caddr_t addr = (caddr_t) *vaddrp;
	u_int npages, pgoffset;
	register struct regspec *rp;
	long a;

	rp = mp->map_obj.rp;
	pgoffset = (u_int) addr & MMU_PAGEOFFSET;

	if (rp->regspec_size == 0) {
		DPRINTF(ROOTNEX_MAP_DEBUG, ("rootnex_unmap_regspec: "
		    "zero regspec_size\n"));
		return (DDI_ME_INVAL);
	}

	npages = mmu_btopr(rp->regspec_size + pgoffset);
	segkmem_mapout(&kvseg, (caddr_t)((int) addr & (~MMU_PAGEOFFSET)),
	    (u_int) mmu_ptob(npages));
	a = btokmx(addr);
	rmfree(kernelmap, (long) npages, (u_long) a);

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
	u_int flags, hat_flags;
	register struct regspec *rp;

	/*
	 * Set up the hat_flags for the mapping.
	 */
	hp = mp->map_handlep;

	hat_flags = 0;
	flags = hp->ah_acc.devacc_attr_endian_flags &
				(DDI_NEVERSWAP_ACC| DDI_STRUCTURE_LE_ACC|
					DDI_STRUCTURE_BE_ACC);
	switch (flags) {
	case DDI_NEVERSWAP_ACC:
		hat_flags = HAT_NEVERSWAP;
		break;
	case DDI_STRUCTURE_BE_ACC:
		hat_flags = HAT_STRUCTURE_BE;
		break;
	case DDI_STRUCTURE_LE_ACC:
		hat_flags = HAT_STRUCTURE_LE;
		break;
	default:
		return (DDI_REGS_ACC_CONFLICT);
	}

	flags = hp->ah_acc.devacc_attr_dataorder;
	if (flags & DDI_UNORDERED_OK_ACC)
		hat_flags |= HAT_UNORDERED_OK;
	if (flags & DDI_MERGING_OK_ACC)
		hat_flags |= HAT_MERGING_OK;
	if (flags & DDI_STORECACHING_OK_ACC)
		hat_flags |= HAT_STORECACHING_OK;

	rp = mp->map_obj.rp;
	if (rp->regspec_size == 0)
		return (DDI_ME_INVAL);

	hp->ah_hat_flags = hat_flags;
	hp->ah_pfn = mmu_btop((u_long)rp->regspec_addr & (~MMU_PAGEOFFSET));
	hp->ah_pnum = mmu_btopr(rp->regspec_size +
				(u_long)rp->regspec_addr & MMU_PAGEOFFSET);
	return (DDI_SUCCESS);
}

static int
rootnex_map(dev_info_t *dip, dev_info_t *rdip, ddi_map_req_t *mp,
	off_t offset, off_t len, caddr_t *vaddrp)
{
	struct regspec *rp, tmp_reg;
	ddi_map_req_t mr = *mp;		/* Get private copy of request */
	int error;
	u_int flags, mapping_flags;
	ddi_acc_hdl_t *hp = NULL;
	uchar_t endian_flags;

	mp = &mr;

	switch (mp->map_op)  {
	case DDI_MO_MAP_LOCKED:
	case DDI_MO_UNMAP:
	case DDI_MO_MAP_HANDLE:
		break;
	default:
		DPRINTF(ROOTNEX_MAP_DEBUG, ("rootnex_map: unimplemented map "
		    "op %d.", mp->map_op));
		return (DDI_ME_UNIMPLEMENTED);
	}

	if (mp->map_flags & DDI_MF_USER_MAPPING)  {
		DPRINTF(ROOTNEX_MAP_DEBUG, ("rootnex_map: unimplemented map "
		    "type: user."));
		return (DDI_ME_UNIMPLEMENTED);
	}

	/*
	 * First, if given an rnumber, convert it to a regspec...
	 * (Presumably, this is on behalf of a child of the root node?)
	 */

	if (mp->map_type == DDI_MT_RNUMBER)  {

		int rnumber = mp->map_obj.rnumber;

		rp = i_ddi_rnumber_to_regspec(rdip, rnumber);
		if (rp == (struct regspec *)0)  {
			DPRINTF(ROOTNEX_MAP_DEBUG, ("rootnex_map: Out of "
			    "range rnumber <%d>, device <%s>", rnumber,
			    ddi_get_name(rdip)));
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

	DPRINTF(ROOTNEX_MAP_DEBUG, ("rootnex_map: applying range of parent "
	    "<%s> to child <%s>...\n", ddi_get_name(dip),
	    ddi_get_name(rdip)));

	if ((error = i_ddi_apply_range(dip, rdip, mp->map_obj.rp)) != 0)
		return (error);

	switch (mp->map_op)  {
	case DDI_MO_MAP_LOCKED:

		/*
		 * Set up the locked down kernel mapping to the regspec...
		 */

		/*
		 * If we were passed an access handle we need to determine
		 * the "endian-ness" of the mapping and fill in the handle.
		 */
		if (mp->map_handlep) {
			hp = mp->map_handlep;
			endian_flags = hp->ah_acc.devacc_attr_endian_flags &
					(DDI_NEVERSWAP_ACC|
						DDI_STRUCTURE_LE_ACC|
						DDI_STRUCTURE_BE_ACC);
			mapping_flags = 0;
			switch (endian_flags) {
			case DDI_NEVERSWAP_ACC:
				mapping_flags = HAT_NEVERSWAP;
				break;
			case DDI_STRUCTURE_BE_ACC:
				mapping_flags = HAT_STRUCTURE_BE;
				break;
			case DDI_STRUCTURE_LE_ACC:
				mapping_flags = HAT_STRUCTURE_LE;
				break;
			default:
				return (DDI_REGS_ACC_CONFLICT);
			}

			flags = hp->ah_acc.devacc_attr_dataorder;
			if (flags & DDI_UNORDERED_OK_ACC)
				mapping_flags |= HAT_UNORDERED_OK;
			if (flags & DDI_MERGING_OK_ACC)
				mapping_flags |= HAT_MERGING_OK;
			if (flags & DDI_STORECACHING_OK_ACC)
				mapping_flags |= HAT_STORECACHING_OK;
		} else
			mapping_flags = HAT_NEVERSWAP|DDI_STRICTORDER_ACC;

		/*
		 * Set up the mapping.
		 */
		error = rootnex_map_regspec(mp, vaddrp, mapping_flags);

		/*
		 * Fill in the access handle if needed.
		 */
		if (hp) {
			hp->ah_addr = *vaddrp;
			hp->ah_hat_flags = mapping_flags;
		}
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
/*ARGSUSED*/
static int
rootnex_add_intrspec(dev_info_t *dip, dev_info_t *rdip,
	ddi_intrspec_t intrspec, ddi_iblock_cookie_t *iblock_cookiep,
	ddi_idevice_cookie_t *idevice_cookiep,
	u_int (*int_handler)(caddr_t int_handler_arg),
	caddr_t int_handler_arg, int kind)
{
	register struct intrspec *ispec;
	register u_int pri;

	ispec = (struct intrspec *) intrspec;
	pri = ispec->intrspec_pri;

	switch (kind) {
	case IDDI_INTR_TYPE_FAST:
		return (DDI_FAILURE);

	case IDDI_INTR_TYPE_SOFT: {
		u_int rval;

		if ((rval = (u_int) add_softintr((u_int) pri, int_handler,
		    int_handler_arg, NULL)) == 0)
			return (DDI_FAILURE);

		ispec->intrspec_pri = rval;		/* Save the index */
		ispec->intrspec_func = int_handler;	/* Save the intr func */

		break;
	}

	case IDDI_INTR_TYPE_NORMAL: {
		struct intr_vector intr_node;
		volatile u_ll_t *intr_mapping_reg;
		volatile u_ll_t mondo_vector;
		int r_upaid = -1;
		extern u_ll_t * get_intr_mapping_reg(int, int);
		extern int getprocessorid();

		if ((r_upaid = ddi_getprop(DDI_DEV_T_ANY, rdip,
		    DDI_PROP_DONTPASS, "upa-portid", -1)) != -1) {
			if (ddi_getprop(DDI_DEV_T_ANY, rdip, DDI_PROP_DONTPASS,
			    "upa-interrupt-slave", 0)) {
				intr_mapping_reg = get_intr_mapping_reg(
				    r_upaid, 1);
			} else {
				intr_mapping_reg = get_intr_mapping_reg(
				    r_upaid, 0);
			}

			/* We better have an interrupt mapping register here. */
			if (intr_mapping_reg == (u_ll_t *) 0)
				return (DDI_FAILURE);
		}

		/* program the rest of the ispec */
		ispec->intrspec_func = int_handler;

		/* Sanity check the entry we're about to add */
		rem_ivintr(ispec->intrspec_vec, &intr_node);

		if (intr_node.iv_handler) {
			cmn_err(CE_WARN, "UPA device mondo 0x%x in use\n",
			    ispec->intrspec_vec);
			return (DDI_FAILURE);
		}

		add_ivintr((u_int) ispec->intrspec_vec,
		    (u_int) ispec->intrspec_pri, int_handler,
		    int_handler_arg, NULL);


		if (r_upaid != -1) {
			/* Program the interrupt mapping register */
			mondo_vector = (u_int) getprocessorid()
			    << IMR_TID_SHIFT;
			mondo_vector |= (IMR_VALID | ispec->intrspec_vec);

			/* Set the mapping register */
			*intr_mapping_reg = mondo_vector;

			/* Flush write buffers */
			mondo_vector = *intr_mapping_reg;
		}

		break;
	}

	default:
		return (DDI_INTR_NOTFOUND);
	}

	/* Program the iblock cookie */
	if (iblock_cookiep) {
		*iblock_cookiep = (ddi_iblock_cookie_t)
		    ipltospl(pri);
	}

	/* Program the device cookie */
	if (idevice_cookiep) {
		idevice_cookiep->idev_vector = 0;
		idevice_cookiep->idev_priority = (u_short)
		    ispec->intrspec_pri;
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
	struct intrspec *ispec = (struct intrspec *) intrspec;

	if (ispec->intrspec_vec == 0) {
		rem_softintr(ispec->intrspec_pri);
		return;
	} else {
		int r_upaid;
		volatile u_ll_t *intr_mapping_reg;
		volatile u_ll_t mondo_vector;
		extern u_ll_t * get_intr_mapping_reg(int, int);

		if ((r_upaid = ddi_getprop(DDI_DEV_T_ANY, rdip,
		    DDI_PROP_DONTPASS, "upa-portid", -1)) != -1) {
			/*
			 * It doesn't matter if slave arg is set to one, we
			 * end up doing the right thing for either case.
			 */
			intr_mapping_reg = get_intr_mapping_reg(r_upaid, 0);

			/* Paranoid check */
			if (intr_mapping_reg == (u_ll_t *) 0)
				return;

			mondo_vector = (u_ll_t) 0;
			*intr_mapping_reg = mondo_vector;

			/* Flush system write buffers. */
			mondo_vector = *intr_mapping_reg;
		}

		rem_ivintr(ispec->intrspec_vec, (struct intr_vector *) NULL);
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

	DPRINTF(ROOTNEX_MAP_DEBUG, ("rootnex_map_fault: address <%x> pfn <%x>",
	    addr, pfn));
	DPRINTF(ROOTNEX_MAP_DEBUG, (" Seg <%s>\n",
	    seg->s_ops == &segdev_ops ? "segdev" :
	    seg == &kvseg ? "segkmem" : "NONE!"));

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

static int
rootnex_ctl_initchild(dev_info_t *dip)
{
	int n, size_cells;
	dev_info_t *parent;
	struct regspec *rp;
	struct ddi_parent_private_data *pd;
	int r_upaid;
	extern void set_upa_dma_flag(int);

	if ((n = impl_ddi_sunbus_initchild(dip)) != DDI_SUCCESS)
		return (n);

	/*
	 * If there are no "reg"s in the child node, return.
	 */

	pd = (struct ddi_parent_private_data *)ddi_get_parent_data(dip);
	if ((pd == NULL) || (pd->par_nreg == 0))
		return (DDI_SUCCESS);

	parent = ddi_get_parent(dip);

	/* If this UPA device can support dma, register it. */
	if ((r_upaid = ddi_getprop(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, "upa-portid", -1)) != -1)
		if (ddi_getprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
		    "upa-interrupt-slave", 0))
			set_upa_dma_flag(r_upaid);

	/*
	 * If the parent #size-cells is 2, convert the upa-style
	 * upa-style reg property from 2-size cells to 1 size cell
	 * format, ignoring the size_hi, which must be zero for devices.
	 * (It won't be zero in the memory list properties in the memory
	 * nodes, but that doesn't matter here.)
	 */

	size_cells = ddi_getprop(DDI_DEV_T_ANY, parent,
	    DDI_PROP_DONTPASS | DDI_PROP_CANSLEEP,
	    "#size-cells", 1);

	if (size_cells != 1)  {

		int len, j;
		struct regspec *irp;
		struct upa_reg {
			u_int addr_hi, addr_lo, size_hi, size_lo;
		} *upa_rp;

		ASSERT(size_cells == 2);

		(void) ddi_getproplen(DDI_DEV_T_ANY, dip,
		    DDI_PROP_DONTPASS | DDI_PROP_CANSLEEP, OBP_REG, &len);

		n = len / sizeof (struct upa_reg);
		/*
		 * We're allocating a buffer the size of the PROM's property,
		 * but we're only using a smaller portion when we assign it
		 * to a regspec.  We do this so that in unitchild, we will
		 * always free the right amount of memory.
		 */
		irp = rp = kmem_zalloc((size_t) len, KM_SLEEP);
		upa_rp = (struct upa_reg *)pd->par_reg;

		for (j = 0; j < n; ++j, ++rp, ++upa_rp) {
			ASSERT(upa_rp->size_hi == 0);
			rp->regspec_bustype = upa_rp->addr_hi;
			rp->regspec_addr = upa_rp->addr_lo;
			rp->regspec_size = upa_rp->size_lo;
		}

		kmem_free((void *)pd->par_reg, (size_t)len);
		pd->par_nreg = n;
		pd->par_reg = irp;
	}

	/*
	 * This check is here for fhc nodes which are not a child of
	 * root. They are a child of central, and are treated differently.
	 */
	if (parent != ddi_root_node()) {
		char name[MAXNAMELEN];
		struct regspec *rp = sparc_pd_getreg(dip, 0);

		name[0] = '\0';
		sprintf(name, "%x,%x", (rp->regspec_bustype >> 1) & 0x1f,
			rp->regspec_addr);

		ddi_set_name_addr(dip, name);
	}

	return (DDI_SUCCESS);
}


int
rootnex_ctl_uninitchild(dev_info_t *dip)
{
	dev_info_t *parent;
	int size_cells;
	struct ddi_parent_private_data *pd;
	extern void impl_ddi_sunbus_removechild(dev_info_t *);

	/*
	 * If there are no "reg"s in the child node, uninit the child
	 * and return.
	 */

	pd = (struct ddi_parent_private_data *)ddi_get_parent_data(dip);
	if ((pd == NULL) || (pd->par_nreg == 0))  {
		impl_ddi_sunbus_removechild(dip);
		return (DDI_SUCCESS);
	}

	/*
	 * If the parent number of size cells is 2, we have
	 * to free the correct sized cached "reg" property.
	 */

	parent = ddi_get_parent(dip);
	size_cells = ddi_getprop(DDI_DEV_T_ANY, parent,
	    DDI_PROP_DONTPASS | DDI_PROP_CANSLEEP,
	    "#size-cells", 1);

	if (size_cells != 1)  {
		int len;

		/*
		 * Make sure we always free the correct size for the
		 * cached reg property. Free it here if the size could
		 * be different.
		 */
		ASSERT(size_cells == 2);
		(void) ddi_getproplen(DDI_DEV_T_ANY, dip,
		    DDI_PROP_DONTPASS | DDI_PROP_CANSLEEP, OBP_REG, &len);

		kmem_free((void *)pd->par_reg, (size_t)len);
		pd->par_nreg = 0;
		pd->par_reg = 0;
	}

	impl_ddi_sunbus_removechild(dip);
	return (DDI_SUCCESS);
}


static int
rootnex_ctl_reportdev(dev_info_t *dev)
{
	register int n;
	struct regspec *rp;
	char buf[80];
	char *p = buf;

	(void) sprintf(p, "%s%d at root", DEVI(dev)->devi_name,
	    DEVI(dev)->devi_instance);
	p += strlen(p);

	if ((n = sparc_pd_getnreg(dev)) > 0) {
		rp = sparc_pd_getreg(dev, 0);

		(void) strcpy(p, ": ");
		p += strlen(p);

		/*
		 * This stuff needs to be fixed correctly for the FFB
		 * devices and the UPA add-on devices. (RAZ)
		 */
		sprintf(p, "UPA 0x%x 0x%x%s",
		    (rp->regspec_bustype >> 1) & 0x1f,
		    rp->regspec_addr,
		    (n > 1 ? "" : " ..."));
		p += strlen(p);
	}

	/*
	 * This is where we need to print out the interrupt specifications
	 * for the FFB device and any UPA add-on devices.  Not sure how to
	 * do this yet? (RAZ)
	 */

	cmn_err(CE_CONT, "?%s\n", buf);
	return (DDI_SUCCESS);
}



#define	MAX_UPA_MONDO 0x7ff

/*ARGSUSED*/
static int
rootnex_xlate_intrs(dev_info_t *dip, dev_info_t *rdip, int *in,
	struct ddi_parent_private_data *pdptr)
{
	register int n;
	register size_t size;
	register struct intrspec *new;

	/*
	 * List of UPA mondos
	 */
	if ((n = *in++) < 1)
		return (DDI_FAILURE);

	pdptr->par_nintr = n;
	size = n * sizeof (struct intrspec);
	new = kmem_zalloc(size, KM_SLEEP);
	pdptr->par_intr = (struct intrspec *) new;

	while (n--) {
		int mondo = *in++;
		int level, r_upaid;

		if (mondo > MAX_UPA_MONDO)
			goto broken;

		/* Get the upa id to construct the 11 bit interrupt number */
		if ((r_upaid = ddi_getprop(DDI_DEV_T_ANY, rdip,
		    DDI_PROP_DONTPASS, "upa-portid", -1)) == -1)
			goto broken;

		/*
		 * If the lower 6 bits of the mondo range from 1 - 15,
		 * treat this as a UPA level interrupt ranging from 1 - 15.
		 * We map these directly to system PIL level.
		 * Otherwise we must be an IO bus error interrupt
		 * number and we map these levels to PIL 14.
		 */
		level = mondo & 0x3f;

		new->intrspec_vec = (r_upaid << 6) | level;

		if (level <= 15)
			new->intrspec_pri = level;
		else
			/*
			 * if we have a level > 15, we probably have a fixed
			 * level for a bus error interrupt.  We'll bind these
			 * to level 14.
			 */
			new->intrspec_pri = 14;

		DPRINTF(ROOTNEX_INTR_DEBUG, ("Interrupt info for device %s "
		    "Mondo: 0x%x, Pil: 0x%x\n", ddi_get_name(rdip),
		    new->intrspec_vec, new->intrspec_pri));
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
rootnex_ctlops(dev_info_t *dip, dev_info_t *rdip,
    ddi_ctl_enum_t ctlop, void *arg, void *result)
{
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

	case DDI_CTLOPS_INITCHILD:
		return (rootnex_ctl_initchild((dev_info_t *)arg));

	case DDI_CTLOPS_UNINITCHILD:
		return (rootnex_ctl_uninitchild((dev_info_t *) arg));

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
		*(int *) result =
		    (((struct intrspec *)arg)->intrspec_pri > LOCK_LEVEL);
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

	case DDI_CTLOPS_XLATE_INTRS: {

		return (rootnex_xlate_intrs(dip, rdip, arg, result));
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
		ptr = (int *) result;
		*ptr = pdp->par_nreg;
	} else if (ctlop == DDI_CTLOPS_NINTRS) {
		ptr = (int *) result;
		*ptr = pdp->par_nintr;
	} else {	/* ctlop == DDI_CTLOPS_REGSIZE */
		off_t *size = (off_t *) result;

		ptr = (int *) arg;
		n = *ptr;
		if (n > pdp->par_nreg) {
			return (DDI_FAILURE);
		}
		*size = (off_t) pdp->par_reg[n].regspec_size;
	}
	return (DDI_SUCCESS);
}
