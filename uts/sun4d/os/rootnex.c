/*
 * Copyright (c) 1991-1994, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)rootnex.c	1.61	95/01/12 SMI"

/*
 * sun4d root nexus driver
 */

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/sysmacros.h>
#include <sys/debug.h>
#include <sys/spl.h>
#include <sys/mmu.h>
#include <sys/pte.h>
#include <sys/ddidmareq.h>
#include <sys/devops.h>
#include <sys/sunddi.h>
#include <sys/ddi_impldefs.h>
#include <sys/ddi_implfuncs.h>
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

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/modctl.h>
#include <sys/bt.h>


extern nodev(), nulldev();
extern int impl_bustype(u_int);
extern void decode_address(u_int, u_int);

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
	ddi_no_dma_map,		/* all the DMA functions are in sbusnex.c */
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

struct dev_ops rootnex_ops = {
	DEVO_REV,		/* rev */
	0,			/* refcnt */
	ddi_no_info,		/* getinfo */
	rootnex_identify,	/* identify */
	0,			/* probe */
	rootnex_attach,		/* attach */
	nodev,			/* detach */
	nodev,			/* reset */
	0,			/* cb_ops */
	&rootnex_bus_ops	/* bus_ops */
};

extern struct mod_ops mod_driverops;

/*
 * Module linkage information for the kernel.
 */

static struct modldrv modldrv0 = {
	&mod_driverops, /* Type of module.  This one is a nexus driver */
	"sun4d root nexus",
	&rootnex_ops,	/* Driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modldrv0, NULL
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
 * Add statically defined root properties to this list...
 */
static int pagesize = PAGESIZE;
static int mmu_pagesize = MMU_PAGESIZE;
static int mmu_pageoffset = MMU_PAGEOFFSET;

struct prop_def {
	char *prop_name;
	int prop_len;
	caddr_t prop_value;
};

static struct prop_def root_props[] = {
	{ "PAGESIZE",		sizeof (int),	(caddr_t)&pagesize },
	{ "MMU_PAGESIZE",	sizeof (int),	(caddr_t)&mmu_pagesize},
	{ "MMU_PAGEOFFSET",	sizeof (int),	(caddr_t)&mmu_pageoffset}
};

#define	NELEMENTS(array)	(sizeof (array) / sizeof (array[0]))

static int cur_unit = 0;

typedef struct {
	char *name;
	int length;
} xdr_string_t;

static xdr_string_t nexus_names[] = {
	{ "Sun4D",	5 },
	{ "XDBus",	5 },
	{ "cpu-unit",	8 },
	{ "io-unit",	7 },
	{ "mem-unit",	8 },
	{ "fastbus",	7 },
	{ "slowbus",	7 }
};


static void
create_root_props(dev_info_t *devi)
{
	int i;
	struct prop_def *rpp = root_props;

	for (i = 0; i < NELEMENTS(root_props); ++i, ++rpp) {
		if (ddi_prop_create(DDI_DEV_T_NONE, devi, 0, rpp->prop_name,
				rpp->prop_value, rpp->prop_len)) {
			panic("create_root_props: ddi_prop_create failed");
		}
	}
}

/*
 * rootnex_identify:
 *
 *	identify the root nexus for a sun4d machine.
 */

static int
rootnex_identify(dev_info_t *devi)
{
	int i;
	char *name = ddi_get_name(devi);

	if (ddi_root_node() == devi)
		return (DDI_IDENTIFIED);

	for (i = 0; i < NELEMENTS(nexus_names); i++) {
		xdr_string_t *alias = nexus_names + i;

		if (strncmp(name, alias->name, alias->length) == 0) {
			return (DDI_IDENTIFIED);
		}
	}

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
	char *name = ddi_get_name(devi);

	/*
	 * FIXME: we should separate other unit drivers
	 *	out of rootnex.c.
	 */
	if (cur_unit == 0) {
		mutex_init(&pokefault_mutex, "pokefault lock",
		    MUTEX_SPIN_DEFAULT, (void *)ipltospl(15));
		cmn_err(CE_CONT, "?root nexus = %s\n", name);
		create_root_props(devi);
	} else {
#if	0
		cmn_err(CE_CONT, "?%s nexus\n", ddi_get_name(devi));
#else
		/*EMPTY*/
#endif	/* DEBUG */
	}

	cur_unit++;
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
	register struct regspec *rp;

	rp = mp->map_obj.rp;

	base = (u_long)rp->regspec_addr & (~MMU_PAGEOFFSET); /* base addr */
	pfn = ((rp->regspec_bustype) << (32-MMU_PAGESHIFT)) | mmu_btop(base);

	/*
	 * This check insulates us from the havoc caused by busted hwconf files
	 */
	if ((bt = impl_bustype(pfn)) == BT_DRAM || bt == BT_UNKNOWN)
		return (DDI_ME_INVAL);

	pgoffset = (u_long)rp->regspec_addr & MMU_PAGEOFFSET; /* offset */

	if (rp->regspec_size == 0) {
#ifdef  DDI_MAP_DEBUG
		ddi_map_debug("rootnex_map_regspec: zero regspec_size\n");
#endif  DDI_MAP_DEBUG
		return (DDI_ME_INVAL);
	}

	npages = mmu_btopr(rp->regspec_size + pgoffset);

#ifdef	DDI_MAP_DEBUG
	ddi_map_debug("rootnex_map_regspec: Mapping %d pages physical %x.%x ",
	    npages, OBIO, base);
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
		if (rp == (struct regspec *)0)	{
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
 *			specification.	First check to make sure there is
 *			one that matchs "inumber" and then return a pointer
 *			to it.	Return NULL if one could not be found.
 */
/*ARGSUSED*/
static ddi_intrspec_t
rootnex_get_intrspec(dev_info_t *dip, dev_info_t *rdip, u_int inumber)
{
	struct ddi_parent_private_data *ppdptr;

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

	extern struct autovec prof_handler;

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
		if (kind == IDDI_INTR_TYPE_SOFT) {
			ispec->intrspec_pri = pri + INTLEVEL_SOFT;
			if (!add_avintr(rdip, ispec->intrspec_pri,
			    int_handler, int_handler_arg,
			    (hot) ? NULL : &unsafe_driver)) {
				return (DDI_FAILURE);
			}
			ispec->intrspec_func = int_handler;
		} else {
			if (pri != 14) {
				return (DDI_FAILURE);
			}

			if (prof_handler.av_vector) {
				if (prof_handler.av_vector != int_handler ||
				    prof_handler.av_intarg != int_handler_arg) {
					return (DDI_FAILURE);
				}
			} else {
				prof_handler.av_vector = int_handler;
				prof_handler.av_intarg = int_handler_arg;
			}

			ispec->intrspec_pri = pri;
			ispec->intrspec_func = int_handler;
		}

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
			 * The idevice cookie should contain the priority as
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
	extern struct autovec prof_handler;

	if (ispec->intrspec_func == (u_int (*)()) 0) {
		return;
	} else if (ispec->intrspec_func == (u_int (*)()) 1) {
		(void) settrap(rdip, ispec->intrspec_pri, NULL);
	} else {
		if (ispec->intrspec_pri != 14) {
			rem_avintr(rdip, ispec->intrspec_pri,
			    ispec->intrspec_func);
		} else {
			if (ispec->intrspec_func == prof_handler.av_vector) {
				prof_handler.av_vector = 0;
			}
		}
	}
	ispec->intrspec_func = (u_int (*)()) 0;
}

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
	 * XXX  What about devices with their own segment drivers?
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
rootnex_compose_paddr(dev_info_t *dip, struct regspec *reg, struct regspec *val)
{
	dev_info_t *root = ddi_root_node();
	dev_info_t *this = ddi_get_parent(dip);
	dev_info_t *last = 0;

	val->regspec_bustype = reg->regspec_bustype;
	val->regspec_addr = reg->regspec_addr;
	val->regspec_size = reg->regspec_size;

	while (last != root) {
		int error = ddi_apply_range(this, dip, val);
		if (error != 0) {
			cmn_err(CE_CONT, "rootnex_compose_paddr: "
				"ddi_apply_range failed (%d)\n", error);
			return (error);
		}

		last = this;
		this = ddi_get_parent(this);
	}
	return (0);
}

static int
rootnex_ctl_reportdev(dev_info_t *dev)
{
	int i;

	cmn_err(CE_CONT, "?%s%d at root", DEVI(dev)->devi_name,
	    DEVI(dev)->devi_instance);

	for (i = 0; i < sparc_pd_getnreg(dev); i++) {
		struct regspec *reg = sparc_pd_getreg(dev, i);
		struct regspec val;

		if (i == 0)
			cmn_err(CE_CONT, "?: ");
		else
			cmn_err(CE_CONT, "? and ");

		if (rootnex_compose_paddr(dev, reg, &val) == 0) {
			decode_address(val.regspec_bustype, val.regspec_addr);
		}
	}

	for (i = 0; i < sparc_pd_getnintr(dev); i++) {

		register int pri;

		if (i == 0)
			cmn_err(CE_CONT, "? ");
		else
			cmn_err(CE_CONT, "?, ");

		pri = INT_IPL(sparc_pd_getintr(dev, i)->intrspec_pri);
		cmn_err(CE_CONT, "?sparc ipl %d", pri);
	}

	cmn_err(CE_CONT, "?\n");
	return (DDI_SUCCESS);
}

static int
rootnex_xlate_intrs(dev_info_t *dip, dev_info_t *rdip, int *in,
	struct ddi_parent_private_data *pdptr)
{
	register size_t size;
	register int n;
	register struct intrspec *new;

	static char bad_intr_fmt[] =
	    "rootnex: bad interrupt spec from %s%d - sparc ipl %d\n";

#ifdef  lint
	dip = dip;
#endif

	if ((n = *in++) < 1)
		return (DDI_FAILURE);

	pdptr->par_nintr = n;
	size = n * sizeof (struct intrspec);
	new = pdptr->par_intr = kmem_zalloc(size, KM_SLEEP);

	while (n--) {
		register int level = *in++;

		if (level < 1 || level > 15) {
			cmn_err(CE_CONT, bad_intr_fmt,
			    DEVI(rdip)->devi_name,
			    DEVI(rdip)->devi_instance, level);
			goto broken;
			/*NOTREACHED*/
		}
		new->intrspec_pri = level;
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

/*ARGSUSED*/
static int
rootnex_ctl_children(dev_info_t *dip, dev_info_t *rdip, ddi_ctl_enum_t ctlop,
    dev_info_t *child)
{
	switch (ctlop)	{

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

	int n, *ptr;
	struct ddi_parent_private_data *pdp;

	switch (ctlop) {
	/*
	 * we don't handle DDI_CTLOPS_DMAPMAPC case in rootnex.c
	 * it should be handled in sbusnex.c.
	 */
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

	case DDI_CTLOPS_XLATE_INTRS:
		return (rootnex_xlate_intrs(dip, rdip, arg, result));

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
