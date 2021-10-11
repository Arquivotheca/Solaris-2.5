/*
 * Copyright (c) 1990-1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)rootnex.c	1.47	95/02/20 SMI"

/*
 * sun4c root nexus driver
 */

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/sysmacros.h>
#include <sys/debug.h>
#include <sys/spl.h>
#include <sys/mmu.h>
#include <sys/pte.h>
#include <sys/devops.h>
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
#include <vm/hat_sunm.h>
#include <sys/vmmac.h>
#include <sys/avintr.h>

#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/modctl.h>
#include <sys/ddi_impldefs.h>
#include <sys/ddi_subrdefs.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>

/*
 * DMA related static data
 */
static int dvma_call_list_id = 0;	/* XXX protected by ? */
static caddr_t dmaimplbase;		/* protected by dma_pool_lock */
static kmutex_t dma_pool_lock;
static kmutex_t xdvma_res_lock;

/*
 * Hack to handle poke faults on Calvin-class machines
 */
extern int pokefault;
static kmutex_t pokefault_mutex;

static int
rootnex_map(dev_info_t *dip, dev_info_t *rdip, ddi_map_req_t *mp,
    off_t offset, off_t len, caddr_t *vaddrp);

static ddi_intrspec_t
rootnex_get_intrspec(dev_info_t *dip, dev_info_t *rdip,	u_int inumber);

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
rootnex_dma_map(dev_info_t *dip, dev_info_t *rdip,
    struct ddi_dma_req *dmareq, ddi_dma_handle_t *handlep);

static int
rootnex_dma_allochdl(dev_info_t *, dev_info_t *, ddi_dma_attr_t *,
    int (*waitfp)(caddr_t), caddr_t arg, ddi_dma_handle_t *);

static int
rootnex_dma_freehdl(dev_info_t *, dev_info_t *, ddi_dma_handle_t);

static int
rootnex_dma_bindhdl(dev_info_t *, dev_info_t *, ddi_dma_handle_t,
    struct ddi_dma_req *, ddi_dma_cookie_t *, u_int *);

static int
rootnex_dma_unbindhdl(dev_info_t *, dev_info_t *, ddi_dma_handle_t);

static int
rootnex_dma_flush(dev_info_t *, dev_info_t *, ddi_dma_handle_t,
    off_t, u_int, u_int);

static int
rootnex_dma_win(dev_info_t *, dev_info_t *, ddi_dma_handle_t,
    uint_t, off_t *, uint_t *, ddi_dma_cookie_t *, uint_t *);

static int
rootnex_dma_mctl(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle, enum ddi_dma_ctlops request,
    off_t *offp, u_int *lenp, caddr_t *objp, u_int cache_flags);

static int
rootnex_ctlops(dev_info_t *, dev_info_t *, ddi_ctl_enum_t, void *, void *);

static struct bus_ops rootnex_bus_ops = {
	BUSO_REV,
	rootnex_map,
	rootnex_get_intrspec,
	rootnex_add_intrspec,
	rootnex_remove_intrspec,
	rootnex_map_fault,
	rootnex_dma_map,
	rootnex_dma_allochdl,
	rootnex_dma_freehdl,
	rootnex_dma_bindhdl,
	rootnex_dma_unbindhdl,
	rootnex_dma_flush,
	rootnex_dma_win,
	rootnex_dma_mctl,
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
	NULL,		/* cb_ops */
	&rootnex_bus_ops
};

static struct modldrv modldrv = {
	&mod_driverops, /* Type of module.  This one is a nexus driver */
	"sun4c root nexus",
	&rootnex_ops,	/* Driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1, &modldrv, NULL
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
 * identify the root nexus for this platform.
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
 * attach the root nexus.
 */
/*ARGSUSED*/
static int
rootnex_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	mutex_init(&dma_pool_lock, "rootnex dma pool lock",
	    MUTEX_DEFAULT, NULL);
	mutex_init(&xdvma_res_lock, "rootnex xdvma resource lock",
	    MUTEX_DEFAULT, NULL);
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
	u_int npages, pfn, pgoffset;
	register struct regspec *rp;

	rp = mp->map_obj.rp;
	base = (u_long)rp->regspec_addr & (~MMU_PAGEOFFSET); /* base addr */
	pgoffset = (u_long)rp->regspec_addr & MMU_PAGEOFFSET; /* offset */

	/*
	 * For this platform, we assume that the only other bus type here would
	 * be 'obmem', ergo the 'bustype' we pass to segkmem_mapin is 'obio'.
	 */
	if (rp->regspec_bustype != OBIO)
		return (DDI_ME_INVAL);

	if (rp->regspec_size == 0) {
		return (DDI_ME_INVAL);
	}

	npages = mmu_btopr(rp->regspec_size + pgoffset);
	a = rmalloc(kernelmap, (long)npages);
	if (a == NULL) {
		return (DDI_ME_NORESOURCES);
	}
	cvaddr = (u_long)kmxtob(a);

	/*
	 * Now map in the pages we've allocated...
	 */
	pfn = MAKE_PGT(rp->regspec_bustype) | mmu_btop(base);
	segkmem_mapin(&kvseg, (caddr_t)cvaddr,
	    (u_int)mmu_ptob(npages), mp->map_prot, pfn, 0);

	*vaddrp = (caddr_t)(kmxtob(a) + pgoffset);
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
		return (DDI_ME_UNIMPLEMENTED);
	}

	if (mp->map_flags & DDI_MF_USER_MAPPING)  {
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
	 * XXX: (regardless of what's in the parent's range?)
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
		if ((error == 0) && (mp->map_handlep)) {
			impl_acc_hdl_init((ddi_acc_hdl_t *)mp->map_handlep);
		}

		return (error);

	case DDI_MO_UNMAP:

		return (rootnex_unmap_regspec(mp, vaddrp));

	case DDI_MO_MAP_HANDLE:

		return (rootnex_map_handle(mp));

	}

	return (DDI_ME_UNIMPLEMENTED);
}

/*
 * convert an interrupt number to an interrupt specification. The interrupt
 * number determines which interrupt spec will be returned if more than
 * one exists. Look into the parent private data area of the dev_info
 * structure to find the interrupt specification. First check to make
 * sure there is one that matchs "inumber" and then return a pointer to it.
 * Return NULL if one could not be found.
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
 * Add an interrupt specification.
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
		else
			ispec->intrspec_pri = pri;

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
 * remove an interrupt specification.
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
 * fault in mappings for requestors
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
 * Shorthand defines
 */
#define	PTECSIZE	(NPMENTPERPMGRP>>1)
#define	SETPTE(addr, ptep)	map_setpgmap((caddr_t)addr, *(u_int *)ptep)
#define	GETPTE(addr, ptep)	*(u_int *)ptep = map_getpgmap((caddr_t)addr)


/*
 * On sun4c/e we support what we call 'Large DVMA'. DVMA may go into
 * two different DVMA maps. One map, in the highest cpu address range
 * is used for small/fast mappings. Resources for this map are pre-allocated
 * at boot time (see: machdep.c). Larger mappings will switch to the kernelmap
 * for DVMA access. Here we may have a little more runtime overhead
 * because we still have to allocate/free pmegs. DVMA into kernelmap works
 * only on the sun4c/e because we are able to DVMA into the whole kernel
 * context (context 0). The strategy for switching between the two maps
 * is as follows: If the partial flag is *not* set, we allow at most mappings
 * up to dvmalim pages. Mappings between dvmafastrq + 1 and dvmalim will go
 * to the kernelmap. Mappings smaller than dvmafastrq + 1 will go to the fast
 * map. From the kernelmap, we can at most grab dvmamaxres pages at a time.
 * dvmamaxres may be changed up to the kernelmap size. (watch out for side
 * effects!) From the fastmap, we may grab dvmasize pages at a time. This
 * *can't* be changed because it already reflects the size of the map.
 * If the partial flag *is* set, we allow arbitrary large mappings but
 * there must be at least space for the window. Mappings larger than
 * dvmamaxwin pages will get a window size of dvmamaxwin pages. Mappings
 * smaller than dvmamaxwin pages will be transformed into normal mappings
 * which means, we clear the partial flag (too much overhead!). We then
 * switch between the two maps as described above.
 */

/* everything is in pages */
#define	DVMALIM		((8*1024*1024)/MMU_PAGESIZE)	/* 8Mb */
#define	DVMAMAXRES	((8*1024*1024)/MMU_PAGESIZE)	/* 8Mb */
#define	DVMAMAXWIN	((1*1024*1024)/MMU_PAGESIZE)	/* 1Mb */
#define	DVMAFASTRQ	PTECSIZE

/* default settings */
int dvmafastrq = DVMAFASTRQ;	/* switch between the two maps */
int dvmamaxwin = DVMAMAXWIN;	/* window size for partial mapppings */
int dvmamaxres = DVMAMAXRES;	/* max resources from kernelmap at a time */
int dvmalim = DVMALIM;		/* max req size without partial flag set */
#define	LARGE_DVMA(np) ((np) > dvmafastrq + 1)

static int largedvmastat = 0;

extern void sunm_vacsync(u_int);
extern void map_setpgmap(caddr_t, u_int);
extern u_int map_getpgmap(caddr_t);
extern int locked_kaddr(caddr_t, u_int, u_int, u_int, u_int);
extern int impl_read_hwmap(struct as *, caddr_t, int, struct pte *, int);
extern u_long getdvmapages(int, u_long, u_long, u_int, u_int, int,
    struct map *, int, u_long);
extern void putdvmapages(u_long, int, struct map *, u_long);
extern int dvmasize;
extern struct map *dvmamap;
extern char DVMA[];

static void rootnex_map_pp(int, page_t *, u_long, int, int);
static void rootnex_map_pte(int, struct pte *, u_long, int, int);
static void rootnex_map_window(ddi_dma_impl_t *, u_long, u_long);
static int dev2devdma(dev_info_t *, struct ddi_dma_req *,
    ddi_dma_impl_t *, void *, int);

/* #define	DMADEBUG */
#if defined(DMADEBUG) || defined(lint)
static int dmadebug;
#else
#define	dmadebug	0
#endif	/* DMADEBUG */

#define	DMAPRINTF	if (dmadebug) printf
#define	DMAPRINT(x)			DMAPRINTF(x)
#define	DMAPRINT1(x, a)			DMAPRINTF(x, a)
#define	DMAPRINT2(x, a, b)		DMAPRINTF(x, a, b)
#define	DMAPRINT3(x, a, b, c)		DMAPRINTF(x, a, b, c)
#define	DMAPRINT4(x, a, b, c, d)	DMAPRINTF(x, a, b, c, d)
#define	DMAPRINT5(x, a, b, c, d, e)	DMAPRINTF(x, a, b, c, d, e)
#define	DMAPRINT6(x, a, b, c, d, e, f)	DMAPRINTF(x, a, b, c, d, e, f)

/*ARGSUSED*/
static int
rootnex_dma_setup(dev_info_t *rdip, struct ddi_dma_req *dmareq,
    u_long addrlow, u_long addrhigh, u_int segalign, u_int burstsizes,
    ddi_dma_impl_t *mp)
{
	extern struct as kas;
	auto struct pte ptes[PTECSIZE + 1];
	auto struct pte *allocpte;
	struct pte *ptep;
	page_t *pp;
	u_int smask;
	u_int rflags;
	struct as *as;
	u_int size;
	u_long addr, kaddr, offset;
	int npages, np, rval, red;
	int klocked = 0;
	int large_dvma = 0;
	int memtype = 0;
	int naptes = 0;

	/*
	 * save flags from req structure. This platform has a
	 * write through cache so a sync for device is not required
	 */
	rflags = (dmareq->dmar_flags & DMP_DDIFLAGS) | DMP_NODEVSYNC;
	red = ((rflags & DDI_DMA_REDZONE)? 1 : 0);
	if ((rflags & DDI_DMA_RDWR) == DDI_DMA_WRITE) {
		rflags |= DMP_NOCPUSYNC;
	}

	size = dmareq->dmar_object.dmao_size;
	smask = size - 1;
	if (smask > segalign) {
		if ((dmareq->dmar_flags & DDI_DMA_PARTIAL) == 0) {
			rval = DDI_DMA_TOOBIG;
			goto bad;
		}
		size = segalign + 1;
	}
	if (addrlow + smask > addrhigh || addrlow + smask < addrlow) {
		if (!((addrlow + dmareq->dmar_object.dmao_size == 0) &&
		    (addrhigh == (u_long)-1))) {
			if ((dmareq->dmar_flags & DDI_DMA_PARTIAL) == 0) {
				rval = DDI_DMA_TOOBIG;
				goto bad;
			}
			size = min(addrhigh - addrlow + 1, size);
		}
	}

	/*
	 * Validate the DMA request. At the same time, determine whether
	 * or not the virtual address of the object to be mapped for I/O
	 * is already mapped (and locked) and addressable by the requestors
	 * DMA engine.
	 */
	switch (dmareq->dmar_object.dmao_type) {
	case DMA_OTYP_VADDR:
		addr = (u_long)dmareq->dmar_object.dmao_obj.virt_obj.v_addr;
		offset = addr & MMU_PAGEOFFSET;
		as = dmareq->dmar_object.dmao_obj.virt_obj.v_as;
		if (as == NULL)
			as = &kas;
		if (as == &kas) {
			/*
			 * See whether the address is a locked address
			 * directly addressable by the device. We use
			 * the original entire size of the object because
			 * we do not allow partial mappings to locked
			 * kernel addresses (we do not want to dork with
			 * kernel virtual mappings in this way).
			 */
			klocked = locked_kaddr((caddr_t)addr,
			    dmareq->dmar_object.dmao_size,
			    addrlow, addrhigh, segalign);
		}

		addr &= ~MMU_PAGEOFFSET;

		/*
		 * As a safety check and an optimization, check the entire
		 * passed range for being of the same type of memory and
		 * (usual case) all primary memory and at the same time,
		 * fetch the first chunk of ptes that we will need for
		 * doing any dma mapping. If the memory object is not all
		 * of the same type, that is an error. If the memory object
		 * is not primary memory, we will have to get clever here
		 * and now.
		 */
		npages = mmu_btopr(dmareq->dmar_object.dmao_size + offset);
		if (npages > PTECSIZE + 1) {
			allocpte =
			    kmem_alloc(npages * sizeof (struct pte), KM_SLEEP);
			if (mp)
				mp->dmai_minfo = (void *)allocpte;
			naptes = npages;
			ptep = allocpte;
		} else {
			ptep = ptes;
		}

		memtype = impl_read_hwmap(as, (caddr_t)addr, npages, ptep, 1);

		switch (memtype) {
		case OBMEM:
			break;

		case OBIO:
			/*
			 * Object is not primary memory. Call another function
			 * to deal with this case. This function will check
			 * the legality of such a transfer, and fiddle with
			 * the dma handle, if appropriate, to finish setting
			 * it up. In the case where specific bus address
			 * values would go into a DMA cookie, the appropriate
			 * nexus drivers will then be required to deal with
			 * them. In the case where an MMU mapping is needed
			 * for the device to device transfer, well, we'll see.
			 */
			rval = dev2devdma(rdip, dmareq, mp,
				(void *)ptep, npages);
			if (rval < 0)
				goto bad;
			else if (rval == DDI_SUCCESS)
				goto out;
			/*
			 * else, we still have to map it in..
			 */
			break;

		case -1:
			/*
			 * Memory type changes in the middle of the object.
			 */
			rval = DDI_DMA_NOMAPPING;
			goto bad;
		}
		pp = NULL;
		break;

	case DMA_OTYP_PAGES:
		/*
		 * If this is an advisory call, then we're done.
		 */
		if (mp == NULL) {
			goto out;
		}
		pp = dmareq->dmar_object.dmao_obj.pp_obj.pp_pp;
		offset = dmareq->dmar_object.dmao_obj.pp_obj.pp_offset;
		npages = mmu_btopr(dmareq->dmar_object.dmao_size + offset);
		ptep = ptes;
		break;

	case DMA_OTYP_PADDR:
	default:
		rval = DDI_DMA_NOMAPPING;
		goto bad;
	}

	/*
	 * At this point, we know that we are doing DMA to or from memory
	 * that we have to allocate translation resources for and map.
	 * Now see whether or not this a kernel virtual address
	 * that'll just plain work without any extra mapping.
	 */
	switch (klocked) {
	default:
	case 0:	/* Not a directly addressable locked kernel virtual address */
		break;
	case 1:	/* Locked directly addressable kernel virtual address */
	case 2:	/* "" + IOPB address */
		if (mp) {
			mp->dmai_mapping = addr + offset;
			mp->dmai_size = size;
			if (klocked == 1) {
				rflags |= DMP_LKSYSV;
			} else {
				/*
				 * We assume that this implementation
				 * makes sure that IOPB mappings never
				 * need syncing.
				 */
				rflags |= DMP_LKIOPB|DMP_NOSYNC;
			}
			rflags &= ~DDI_DMA_PARTIAL;
		}
		goto out;
	}

	/*
	 * At this point, we know for sure that we are going to need
	 * to do some mapping. If this is an advisory call, we're done
	 * because we already checked the legality of the DMA_OTYP_VADDR
	 * case above.
	 */
	if (mp == NULL) {
		if (naptes) {
			kmem_free(allocpte, naptes * sizeof (struct pte));
		}
		goto out;
	}

	/*
	 * Get the number of pages we need to allocate. If the request
	 * is marked DDI_DMA_PARTIAL, do the work necessary to set this
	 * up right. Up until now, npages is the total number of pages
	 * needed to map the entire object. We may rewrite npages to
	 * be the number of pages necessary to map a dvmamaxwin window
	 * onto the object (including any beginning offset).
	 */
	if (rflags & DDI_DMA_PARTIAL) {
		/*
		 * If the size was rewritten above due to device dma
		 * constraints, make sure that it still makes sense
		 * to attempt anything. Also, in this case, the
		 * ability to do a dma mapping at all predominates
		 * over any attempt at optimizing the size of such
		 * a mapping.
		 */
		if (size != dmareq->dmar_object.dmao_size) {
			/*
			 * If the request is for partial mapping arrangement,
			 * the device has to be able to address at least the
			 * size of the window we are establishing.
			 */
			if (size < mmu_ptob(PTECSIZE + mmu_btopr(offset))) {
				rval = DDI_DMA_NOMAPPING;
				goto bad;
			}
			npages = mmu_btopr(size + offset);
		}
		/*
		 * If the size requested is less than a moderate amt, skip
		 * the partial mapping stuff - it's not worth the effort.
		 */
		if (npages > dvmamaxwin + 1) {
			npages = dvmamaxwin + mmu_btopr(offset);
			size = mmu_ptob(dvmamaxwin);
		} else {
			rflags ^= DDI_DMA_PARTIAL;
		}
	} else {
		/*
		 * We give more slack to requestors who cannot do partial
		 * mappings, but we do not give them carte blanche.
		 */
		if (LARGE_DVMA(npages + red)) {
			if (npages + red > dvmalim) {
				rval = DDI_DMA_TOOBIG;
				goto bad;
			}
		}
	}

	/*
	 * Establish dmai_size to be the size of the area we are
	 * mapping, not including any redzone, but accounting for
	 * any offset we are starting from. Note that this may be
	 * quite distinct from the actual size of the object itself.
	 */
	mp->dmai_size = size;
	mp->dmai_ndvmapages = npages;

	/*
	 * We have to do some mapping here. We either have to
	 * produce a mapping for a passed virtual address,
	 * or to produce a new mapping for a list of pages.
	 */
	np = npages + red;
	if (LARGE_DVMA(np)) {
		if (addrhigh < (u_long)Sysbase) {
			rval = DDI_DMA_NOMAPPING;
			goto bad;
		}
		mutex_enter(&xdvma_res_lock);
		if (np > dvmamaxres) {
			mutex_exit(&xdvma_res_lock);
			kaddr = 0;
		} else {
			extern int xdvmasize;

			dvmamaxres -= np;
			mutex_exit(&xdvma_res_lock);

			large_dvma = 1;
			kaddr = getdvmapages(np, addrlow, addrhigh, (u_int)-1,
			    segalign, (dmareq->dmar_fp == DDI_DMA_SLEEP)? 1 : 0,
			    kernelmap, xdvmasize, (u_long)Sysbase);
		}
	} else {
		if (addrhigh < (u_long)DVMA) {
			rval = DDI_DMA_NOMAPPING;
			goto bad;
		}
		kaddr = getdvmapages(np, addrlow, addrhigh, (u_int)-1,
		    segalign, (dmareq->dmar_fp == DDI_DMA_SLEEP)? 1 : 0,
		    dvmamap, dvmasize, (u_long)DVMA);
	}

	if (kaddr == 0) {
		if (dmareq->dmar_fp == DDI_DMA_SLEEP)
			rval = DDI_DMA_NOMAPPING;
		else
			rval = DDI_DMA_NORESOURCES;
		goto bad;
	}

	/*
	 * establish real virtual address for caller. This field
	 * is invariant throughout the life of the mapping.
	 */
	mp->dmai_mapping = (u_long)(kaddr + offset);
	ASSERT((mp->dmai_mapping & ~segalign) ==
	    ((mp->dmai_mapping + (mp->dmai_size - 1)) & ~segalign));

	/*
	 * At this point we have a range of virtual address allocated
	 * with which we now have to map to the requested object.
	 */
	if (pp)
		rootnex_map_pp(npages, pp, kaddr, large_dvma, red);
	else
		rootnex_map_pte(npages, ptep, kaddr, large_dvma, red);

out:
	if (mp) {
		mp->dmai_rflags = rflags;
		mp->dmai_object = dmareq->dmar_object;
		if (rflags & DDI_DMA_PARTIAL) {
			size = mmu_ptob(
				mp->dmai_ndvmapages - mmu_btopr(offset));
			mp->dmai_nwin =
			    (dmareq->dmar_object.dmao_size + (size - 1)) / size;
			return (DDI_DMA_PARTIAL_MAP);
		} else {
			mp->dmai_nwin = 0;
			if (naptes) {
				kmem_free(allocpte,
				    naptes * sizeof (struct pte));
				mp->dmai_minfo = NULL;
			}
			return (DDI_DMA_MAPPED);
		}
	} else {
		return (DDI_DMA_MAPOK);
	}
bad:
	if (naptes) {
		kmem_free(allocpte, naptes * sizeof (struct pte));
	}
	if (rval == DDI_DMA_NORESOURCES &&
	    dmareq->dmar_fp != DDI_DMA_DONTWAIT) {
		ddi_set_callback(dmareq->dmar_fp,
		    dmareq->dmar_arg, &dvma_call_list_id);
	}
	return (rval);
}

static int
rootnex_dma_map(dev_info_t *dip, dev_info_t *rdip,
    struct ddi_dma_req *dmareq, ddi_dma_handle_t *handlep)
{
	ddi_dma_lim_t *dma_lim = dmareq->dmar_limits;
	ddi_dma_impl_t *mp;
	u_long addrlow, addrhigh;
	int rval;

#ifdef lint
	dip = dip;
#endif

	/*
	 * If not an advisory call, get a DMA hande
	 */
	if (handlep) {
		mutex_enter(&dma_pool_lock);
		mp = kmem_fast_alloc(&dmaimplbase, sizeof (*mp), 2,
		    (dmareq->dmar_fp == DDI_DMA_SLEEP) ? KM_SLEEP : KM_NOSLEEP);
		mutex_exit(&dma_pool_lock);
		if (mp == NULL) {
			if (dmareq->dmar_fp != DDI_DMA_DONTWAIT) {
				ddi_set_callback(dmareq->dmar_fp,
					dmareq->dmar_arg, &dvma_call_list_id);
			}
			return (DDI_DMA_NORESOURCES);
		}
		mp->dmai_rdip = rdip;
		mp->dmai_rflags = dmareq->dmar_flags & DMP_DDIFLAGS;
		mp->dmai_minxfer = dma_lim->dlim_minxfer;
		mp->dmai_burstsizes = dma_lim->dlim_burstsizes;
		mp->dmai_offset = 0;
		mp->dmai_ndvmapages = 0;
		mp->dmai_minfo = 0;
	} else {
		mp = NULL;
	}
	if (dma_lim->dlim_burstsizes == 0) {
		rval = DDI_DMA_NOMAPPING;
		goto bad;
	}

	/*
	 * Check sanity for high and low address limits
	 * The only valid address references we deal with here are in the
	 * Kernel's address range. This is because we are either going to
	 * map something across DVMA or we have the ability to directly
	 * access a kernel mapping.
	 */
	addrlow = dma_lim->dlim_addr_lo;
	addrhigh = dma_lim->dlim_addr_hi;
	if ((addrhigh <= addrlow) || (addrhigh < (u_long)KERNELBASE)) {
		rval = DDI_DMA_NOMAPPING;
		goto bad;
	}

	rval = rootnex_dma_setup(rdip, dmareq, addrlow, addrhigh,
		dma_lim->dlim_cntr_max, dma_lim->dlim_burstsizes, mp);
bad:
	if (rval && (rval != DDI_DMA_PARTIAL_MAP)) {
		if (mp) {
			mutex_enter(&dma_pool_lock);
			kmem_fast_free(&dmaimplbase, (caddr_t)mp);
			mutex_exit(&dma_pool_lock);
		}
	} else {
		if (mp) {
			*handlep = (ddi_dma_handle_t)mp;
		}
	}
	return (rval);
}


static int
rootnex_dma_allochdl(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_attr_t *dma_attr, int (*waitfp)(caddr_t), caddr_t arg,
    ddi_dma_handle_t *handlep)
{
	u_long addrlow, addrhigh;
	ddi_dma_impl_t *mp;

#ifdef lint
	dip = dip;
#endif

	if (dma_attr->dma_attr_burstsizes == 0) {
		return (DDI_DMA_BADATTR);
	}
	addrlow = (u_long)dma_attr->dma_attr_addr_lo;
	addrhigh = (u_long)dma_attr->dma_attr_addr_hi;
	if ((addrhigh <= addrlow) || (addrhigh < (u_long)KERNELBASE)) {
		return (DDI_DMA_BADATTR);
	}

	mutex_enter(&dma_pool_lock);
	mp = kmem_fast_zalloc(&dmaimplbase, sizeof (*mp), 2,
		(waitfp == DDI_DMA_SLEEP) ? KM_SLEEP : KM_NOSLEEP);
	mutex_exit(&dma_pool_lock);
	if (mp == NULL) {
		if (waitfp != DDI_DMA_DONTWAIT) {
			ddi_set_callback(waitfp, arg, &dvma_call_list_id);
		}
		return (DDI_DMA_NORESOURCES);
	}

	mp->dmai_rdip = rdip;
	mp->dmai_minxfer = (u_int)dma_attr->dma_attr_minxfer;
	mp->dmai_burstsizes = (u_int)dma_attr->dma_attr_burstsizes;
	mp->dmai_attr = *dma_attr;
	*handlep = (ddi_dma_handle_t)mp;
	return (DDI_SUCCESS);
}

/*ARGSUSED*/
static int
rootnex_dma_freehdl(dev_info_t *dip, dev_info_t *rdip,
	ddi_dma_handle_t handle)
{
	ddi_dma_impl_t *mp = (ddi_dma_impl_t *)handle;

	mutex_enter(&dma_pool_lock);
	kmem_fast_free(&dmaimplbase, (caddr_t)mp);
	mutex_exit(&dma_pool_lock);

	if (dvma_call_list_id != 0) {
		ddi_run_callback(&dvma_call_list_id);
	}
	return (DDI_SUCCESS);
}

static int
rootnex_dma_bindhdl(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle, struct ddi_dma_req *dmareq,
    ddi_dma_cookie_t *cp, u_int *ccountp)
{
	ddi_dma_impl_t *mp = (ddi_dma_impl_t *)handle;
	ddi_dma_attr_t *dma_attr;
	u_long addrlow, addrhigh;
	int rval;

#ifdef lint
	dip = dip;
#endif

	/*
	 * no mutex for speed
	 */
	if (mp->dmai_inuse) {
		return (DDI_DMA_INUSE);
	}
	mp->dmai_inuse = 1;
	mp->dmai_offset = 0;
	mp->dmai_rflags = (dmareq->dmar_flags & DMP_DDIFLAGS) | DMP_NODEVSYNC;
	dma_attr = &mp->dmai_attr;
	addrlow = (u_long)dma_attr->dma_attr_addr_lo;
	addrhigh = (u_long)dma_attr->dma_attr_addr_hi;

	rval = rootnex_dma_setup(rdip, dmareq, addrlow, addrhigh,
			(u_int)dma_attr->dma_attr_seg,
			dma_attr->dma_attr_burstsizes, mp);
	if (rval && (rval != DDI_DMA_PARTIAL_MAP)) {
		mp->dmai_inuse = 0;
		return (rval);
	}
	cp->dmac_notused = 0;
	cp->dmac_address = mp->dmai_mapping;
	cp->dmac_size = mp->dmai_size;
	cp->dmac_type = 0;
	*ccountp = 1;
	return (rval);
}

/*ARGSUSED*/
static int
rootnex_dma_unbindhdl(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle)
{
	ddi_dma_impl_t *mp = (ddi_dma_impl_t *)handle;
	u_int npages;
	u_int rflags;
	u_long addr, offset;
	int isdvma;

	rflags = mp->dmai_rflags;
	addr = mp->dmai_mapping & ~MMU_PAGEOFFSET;
	offset = mp->dmai_mapping & MMU_PAGEOFFSET;
	isdvma = ((rflags & (DMP_LKIOPB|DMP_LKSYSV)) == 0);

	/*
	 * We use the rounded size of the entire object just in
	 * case we had been doing a DDI_DMA_PARTIAL type transfer.
	 */
	npages = mmu_btopr(mp->dmai_object.dmao_size + offset);

	if ((rflags & DMP_NOCPUSYNC) == 0) {
		extern u_int page_pptonum(page_t *);
		register u_int pfnum, np;
		register page_t *pp;
		register struct pte *ptep;

		pp = NULL;
		ptep = NULL;

		/*
		 * For partial mappings, we either get page frame
		 * numbers out of a page structure or get them
		 * from a retained array of ptes.
		 */
		if (rflags & DDI_DMA_PARTIAL) {
			if (mp->dmai_object.dmao_type ==
			    DMA_OTYP_VADDR) {
				ptep = (struct pte *)mp->dmai_minfo;
				ASSERT(ptep != NULL);
			} else {
				pp = mp->dmai_object.dmao_obj.pp_obj.pp_pp;
			}
		}

		for (np = npages; np > (u_int)0; np--) {
			if (pp) {
				pfnum = page_pptonum(pp);
				pp = pp->p_next;
			} else if (ptep) {
				pfnum = ptep->pg_pfnum;
				ptep++;
			} else {
				struct pte pte;
				GETPTE(addr, &pte);
				pfnum = pte.pg_pfnum;
				addr += MMU_PAGESIZE;
			}
			sunm_vacsync(pfnum);
		}
	}

	if (mp->dmai_minfo) {
		kmem_free((caddr_t)mp->dmai_minfo,
		    npages * sizeof (struct pte));
		mp->dmai_minfo = NULL;
	}

	if (isdvma && mp->dmai_ndvmapages) {
		int red = ((rflags & DDI_DMA_REDZONE)? 1 : 0);
		int np = mp->dmai_ndvmapages + red;

		addr = mp->dmai_mapping & ~MMU_PAGEOFFSET;
		if (LARGE_DVMA(np)) {
			segkmem_mapout(&kvseg, (caddr_t)addr,
			    mmu_ptob(mp->dmai_ndvmapages));
			putdvmapages(addr, np, kernelmap,
			    (u_long)Sysbase);

			mutex_enter(&xdvma_res_lock);
			dvmamaxres += np;
			largedvmastat++;
			mutex_exit(&xdvma_res_lock);
		} else {
			putdvmapages(addr, np, dvmamap, (u_long)DVMA);
		}
		mp->dmai_ndvmapages = 0;
	}
	mp->dmai_inuse = 0;

	/*
	 * Now that we've freed some resource, if there is
	 * anybody waiting for it try and get them going.
	 */
	if (dvma_call_list_id != 0) {
		ddi_run_callback(&dvma_call_list_id);
	}
	return (DDI_SUCCESS);
}

/*ARGSUSED*/
static int
rootnex_dma_flush(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle, off_t off, u_int len,
    u_int cache_flags)
{
	ddi_dma_impl_t *mp = (ddi_dma_impl_t *)handle;
	u_long addr;

	if (cache_flags == DDI_DMA_SYNC_FORDEV &&
	    (mp->dmai_rflags & DMP_NODEVSYNC)) {
		return (DDI_SUCCESS);
	} else if (mp->dmai_rflags & DMP_NOCPUSYNC) {
		return (DDI_SUCCESS);
	} else {
		u_int npf;
		u_long endmap;

		addr = mp->dmai_mapping + off;
		endmap = mp->dmai_mapping + mp->dmai_size;

		/*
		 * Test below mapping to catch integer overflow
		 */
		if (addr < mp->dmai_mapping || addr >= endmap) {
			return (DDI_FAILURE);
		}

		if ((npf = len) == 0 || npf == (u_int)-1 ||
		    addr + npf >= endmap) {
			npf = endmap - addr;
		}

		if (cache_flags == DDI_DMA_SYNC_FORKERNEL &&
			mp->dmai_object.dmao_type == DMA_OTYP_VADDR) {
			addr = (u_long)mp->dmai_object.
				dmao_obj.virt_obj.v_addr + off;
			if (npf < MMU_PAGESIZE) {
				vac_flush((caddr_t)addr, npf);
			} else {
				npf = mmu_btopr(npf +
					(addr & MMU_PAGEOFFSET));
				addr &= ~MMU_PAGEOFFSET;
				while (npf != 0) {
					vac_pageflush((caddr_t)addr);
					addr += MMU_PAGESIZE;
					npf--;
				}
			}
		} else {
			/*
			 * We don't need to truncate the base address
			 * down or length up to vac_linesize boundaries
			 * since we are going to be doing whole pages.
			 *
			 * Adjust length upwards by the offset into a
			 * page that addr is and then round that up a
			 * page. Truncate address down to the beginning
			 * of the page.
			 */
			npf = mmu_btopr(npf + (addr & MMU_PAGEOFFSET));
			addr &= ~MMU_PAGEOFFSET;
			while (npf != 0 && addr < endmap) {
				struct pte pte;
				GETPTE(addr, &pte);
				sunm_vacsync(pte.pg_pfnum);
				addr += MMU_PAGESIZE;
				npf--;
			}
		}
	}
	return (DDI_SUCCESS);
}

static int
rootnex_dma_win(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle, uint_t win, off_t *offp,
    uint_t *lenp, ddi_dma_cookie_t *cookiep, uint_t *ccountp)
{
	ddi_dma_impl_t *mp = (ddi_dma_impl_t *)handle;
	u_long offset;
	u_long winsize, newoff;

#ifdef lint
	dip = dip;
	rdip = rdip;
#endif

	offset = mp->dmai_mapping & MMU_PAGEOFFSET;
	winsize = mmu_ptob(mp->dmai_ndvmapages - mmu_btopr(offset));

	DMAPRINT2("getwin win %d winsize %x\n", win, (int)winsize);

	/*
	 * win is in the range [0 .. dmai_nwin-1]
	 */
	if (win >= mp->dmai_nwin) {
		return (DDI_FAILURE);
	}
	newoff = win * winsize;
	if (newoff > mp->dmai_object.dmao_size - mp->dmai_minxfer) {
		return (DDI_FAILURE);
	}
	ASSERT(cookiep);
	cookiep->dmac_notused = 0;
	cookiep->dmac_address = mp->dmai_mapping;
	cookiep->dmac_type = 0;
	*ccountp = 1;
	*offp = (off_t)newoff;
	*lenp = (u_int)winsize;
	if (newoff == mp->dmai_offset) {
		cookiep->dmac_size = mp->dmai_size;
		return (DDI_SUCCESS);
	}

	rootnex_map_window(mp, newoff, winsize);
	/*
	 * last window might be shorter.
	 */
	cookiep->dmac_size = mp->dmai_size;

	return (DDI_SUCCESS);
}


static void
rootnex_map_pp(int npages, page_t *pp, u_long kaddr,
    int large_dvma, int red)
{
	struct pte pte_tmp, *ptep;

	ptep = &pte_tmp;
	while (npages > 0) {
		*ptep = mmu_pteinvalid;
		ptep->pg_nc = PP_ISNC(pp) ? 1 : 0;
		ptep->pg_v = 1;
		ptep->pg_type = OBMEM;
		ptep->pg_pfnum = page_pptonum(pp);
		ptep->pg_prot = KW;

		if (!large_dvma) {
			SETPTE(kaddr, ptep);
		} else {
			segkmem_mapin(&kvseg, (caddr_t)kaddr,
			    MMU_PAGESIZE, PROT_READ | PROT_WRITE,
			    MAKE_PFNUM(ptep), 0);
		}
		kaddr += MMU_PAGESIZE;
		npages--;
		pp = pp->p_next;
	}

	if (red) {
		SETPTE(kaddr, &mmu_pteinvalid);
	}
}

static void
rootnex_map_pte(int npages, struct pte *ptep, u_long kaddr,
    int large_dvma, int red)
{
	while (npages > 0) {
		ptep->pg_prot = KW;

		if (!large_dvma) {
			SETPTE(kaddr, ptep);
		} else {
			segkmem_mapin(&kvseg, (caddr_t)kaddr,
			    MMU_PAGESIZE, PROT_READ | PROT_WRITE,
			    MAKE_PFNUM(ptep), 0);
		}
		kaddr += MMU_PAGESIZE;
		npages--;
		ptep++;
	}

	if (red) {
		SETPTE(kaddr, &mmu_pteinvalid);
	}
}

static void
rootnex_map_window(ddi_dma_impl_t *mp, u_long newoff, u_long winsize)
{
	extern struct seg kvseg;
	struct pte *ptep;
	page_t *pp;
	u_long kaddr, offset, flags;
	int npages, red, large_dvma = 0;

	mp->dmai_offset = newoff;
	mp->dmai_size = mp->dmai_object.dmao_size - newoff;
	mp->dmai_size = MIN(mp->dmai_size, winsize);

	if (mp->dmai_object.dmao_type == DMA_OTYP_VADDR) {
		ptep = (struct pte *)mp->dmai_minfo;
		ASSERT(ptep != NULL);
		ptep = ptep + (newoff >> MMU_PAGESHIFT);
		pp = NULL;
	} else {
		pp = mp->dmai_object.dmao_obj.pp_obj.pp_pp;
		flags = 0;
		while (flags < newoff) {
			pp = pp->p_next;
			flags += MMU_PAGESIZE;
		}
	}

	/*
	 * The original page offset is always held in dmai_mapping
	 */
	kaddr = mp->dmai_mapping;
	offset = mp->dmai_mapping & MMU_PAGEOFFSET; /* XXX */
	npages = mmu_btopr(mp->dmai_size + offset);

	DMAPRINT1("dma_mctl: remapping %d pages\n", npages);

	red = ((mp->dmai_rflags & DDI_DMA_REDZONE)? 1 : 0);
	if (LARGE_DVMA(npages + red)) {
		large_dvma = 1;
		segkmem_mapout(&kvseg, (caddr_t)kaddr,
		    mmu_ptob(npages));
	}

	if (pp)
		rootnex_map_pp(npages, pp, kaddr, large_dvma, 0);
	else
		rootnex_map_pte(npages, ptep, kaddr, large_dvma, 0);
}

static int
rootnex_dma_mctl(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle, enum ddi_dma_ctlops request,
    off_t *offp, u_int *lenp,
    caddr_t *objp, u_int cache_flags)
{
	extern struct seg kvseg;
	u_long addr, offset;
	u_int npages, rflags;
	ddi_dma_cookie_t *cp;
	ddi_dma_impl_t *mp = (ddi_dma_impl_t *)handle;

#ifdef lint
	dip = dip;
	rdip = rdip;
#endif

	DMAPRINT1("dma_mctl: handle %x ", (int)mp);

	switch (request) {
	case DDI_DMA_FREE:
	{
		int isdvma;

		rflags = mp->dmai_rflags;
		addr = mp->dmai_mapping & ~MMU_PAGEOFFSET;
		offset = mp->dmai_mapping & MMU_PAGEOFFSET;
		isdvma = ((rflags & (DMP_LKIOPB|DMP_LKSYSV)) == 0);

		/*
		 * We use the rounded size of the entire object just in
		 * case we had been doing a DDI_DMA_PARTIAL type transfer.
		 */
		npages = mmu_btopr(mp->dmai_object.dmao_size + offset);

		if ((rflags & DMP_NOCPUSYNC) == 0) {
			extern u_int page_pptonum(page_t *);
			register u_int pfnum, np;
			register page_t *pp;
			register struct pte *ptep;

			pp = NULL;
			ptep = NULL;

			/*
			 * For partial mappings, we either get page frame
			 * numbers out of a page structure or get them
			 * from a retained array of ptes.
			 */
			if (rflags & DDI_DMA_PARTIAL) {
				if (mp->dmai_object.dmao_type ==
				    DMA_OTYP_VADDR) {
					ptep = (struct pte *)mp->dmai_minfo;
					ASSERT(ptep != NULL);
				} else {
					pp = mp->dmai_object.
						dmao_obj.pp_obj.pp_pp;
				}
			}

			for (np = npages; np > (u_int)0; np--) {
				if (pp) {
					pfnum = page_pptonum(pp);
					pp = pp->p_next;
				} else if (ptep) {
					pfnum = ptep->pg_pfnum;
					ptep++;
				} else {
					struct pte pte;
					GETPTE(addr, &pte);
					pfnum = pte.pg_pfnum;
					addr += MMU_PAGESIZE;
				}
				sunm_vacsync(pfnum);
			}
		}

		if (mp->dmai_minfo) {
			kmem_free(mp->dmai_minfo, npages * sizeof (struct pte));
			mp->dmai_minfo = NULL;
		}

		if (isdvma && mp->dmai_ndvmapages) {
			int red = ((rflags & DDI_DMA_REDZONE)? 1 : 0);
			int np = mp->dmai_ndvmapages + red;

			addr = mp->dmai_mapping & ~MMU_PAGEOFFSET;
			if (LARGE_DVMA(np)) {
				segkmem_mapout(&kvseg, (caddr_t)addr,
				    mmu_ptob(mp->dmai_ndvmapages));
				putdvmapages(addr, np, kernelmap,
				    (u_long)Sysbase);

				mutex_enter(&xdvma_res_lock);
				dvmamaxres += np;
				largedvmastat++;
				mutex_exit(&xdvma_res_lock);
			} else {
				putdvmapages(addr, np, dvmamap, (u_long)DVMA);
			}
		}

		/*
		 * put impl struct back on free list
		 */
		mutex_enter(&dma_pool_lock);
		kmem_fast_free(&dmaimplbase, (caddr_t)mp);
		mutex_exit(&dma_pool_lock);

		/*
		 * Now that we've freed some resource, if there is
		 * anybody waiting for it try and get them going.
		 */
		if (dvma_call_list_id != 0) {
			ddi_run_callback(&dvma_call_list_id);
		}
		break;
	}
	case DDI_DMA_SYNC:

		DMAPRINT("sync\n");
		if (cache_flags == DDI_DMA_SYNC_FORDEV &&
		    (mp->dmai_rflags & DMP_NODEVSYNC)) {
			break;
		} else if (mp->dmai_rflags & DMP_NOCPUSYNC) {
			break;
		} else {
			u_int npf, len;
			u_long endmap, off;

			off = (u_long)*offp;
			len = *lenp;

			addr = mp->dmai_mapping + off;
			endmap = mp->dmai_mapping + mp->dmai_size;

			/*
			 * Test below mapping to catch integer overflow
			 */

			if (addr < mp->dmai_mapping || addr >= endmap) {
				return (DDI_FAILURE);
			}

			if ((npf = len) == 0 || npf == (u_int)-1 ||
			    addr + npf >= endmap) {
				npf = endmap - addr;
			}

			if (cache_flags == DDI_DMA_SYNC_FORKERNEL &&
				mp->dmai_object.dmao_type == DMA_OTYP_VADDR) {
				addr = (u_long)mp->dmai_object.
					dmao_obj.virt_obj.v_addr + off;
				if (npf < MMU_PAGESIZE) {
					vac_flush((caddr_t)addr, npf);
				} else {
					npf = mmu_btopr(npf +
						(addr & MMU_PAGEOFFSET));
					addr &= ~MMU_PAGEOFFSET;
					while (npf != 0) {
						vac_pageflush((caddr_t)addr);
						addr += MMU_PAGESIZE;
						npf--;
					}
				}
			} else {
				/*
				 * We don't need to truncate the base address
				 * down or length up to vac_linesize boundaries
				 * since we are going to be doing whole pages.
				 *
				 * Adjust length upwards by the offset into a
				 * page that addr is and then round that up a
				 * page. Truncate address down to the beginning
				 * of the page.
				 */
				npf = mmu_btopr(npf + (addr & MMU_PAGEOFFSET));
				addr &= ~MMU_PAGEOFFSET;
				while (npf != 0 && addr < endmap) {
					struct pte pte;
					GETPTE(addr, &pte);
					sunm_vacsync(pte.pg_pfnum);
					addr += MMU_PAGESIZE;
					npf--;
				}
			}
		}
		break;

	case DDI_DMA_HTOC:
		/*
		 * Note that we are *not* cognizant of partial mappings
		 * at this level. We only support offsets for cookies
		 * that would then stick within the current mapping for
		 * a device.
		 */
		addr = (u_long)*offp;
		if (addr >= (u_long)mp->dmai_size) {
			return (DDI_FAILURE);
		}
		cp = (ddi_dma_cookie_t *)objp;
		cp->dmac_notused = 0;
		cp->dmac_address = (mp->dmai_mapping + addr);
		cp->dmac_size =
		    mp->dmai_mapping + mp->dmai_size - cp->dmac_address;
		cp->dmac_type = 0;
		break;

	case DDI_DMA_KVADDR:

		/*
		 * If a physical address mapping has percolated this high,
		 * that is an error (maybe?).
		 */
		if (mp->dmai_rflags & DMP_PHYSADDR) {
			return (DDI_FAILURE);
		}
		addr = (u_long)*lenp;
		if (addr == (u_long)-1)
			addr = 0;
		if ((*offp + *lenp) > (off_t)mp->dmai_size) {
			return (DDI_FAILURE);
		}
		*objp = (caddr_t)(mp->dmai_mapping + (u_int)*offp);
		break;

	case DDI_DMA_NEXTWIN:
	{
		register ddi_dma_win_t *owin, *nwin;
		u_long winsize, newoff;

		mp = (ddi_dma_impl_t *)handle;
		owin = (ddi_dma_win_t *)offp;
		nwin = (ddi_dma_win_t *)objp;
		if (mp->dmai_rflags & DDI_DMA_PARTIAL) {
			if (*owin == NULL) {
				mp->dmai_offset = 0;
				*nwin = (ddi_dma_win_t)mp;
				return (DDI_SUCCESS);
			}
			offset = mp->dmai_mapping & MMU_PAGEOFFSET;
			winsize = mmu_ptob(
			    mp->dmai_ndvmapages - mmu_btopr(offset));
			newoff = mp->dmai_offset + winsize;
			if (newoff > mp->dmai_object.dmao_size -
				mp->dmai_minxfer) {
				return (DDI_DMA_DONE);
			}
			rootnex_map_window(mp, newoff, winsize);

		} else {
			if (*owin != NULL) {
				return (DDI_DMA_DONE);
			}
			mp->dmai_offset = 0;
			*nwin = (ddi_dma_win_t)mp;
		}
		break;
	}

	case DDI_DMA_NEXTSEG:
	{
		ddi_dma_seg_t *oseg, *nseg;

		oseg = (ddi_dma_seg_t *)lenp;
		if (*oseg != NULL) {
			return (DDI_DMA_DONE);
		} else {
			nseg = (ddi_dma_seg_t *)objp;
			*nseg = *((ddi_dma_seg_t *)offp);
		}
		break;
	}

	case DDI_DMA_SEGTOC:
	{
		register ddi_dma_seg_impl_t *seg;

		seg = (ddi_dma_seg_impl_t *)handle;
		cp = (ddi_dma_cookie_t *)objp;
		cp->dmac_notused = 0;
		cp->dmac_address = seg->dmai_mapping;
		cp->dmac_size = *lenp = seg->dmai_size;
		cp->dmac_type = 0;
		*offp = seg->dmai_offset;
		break;
	}

	case DDI_DMA_MOVWIN:
	{
		u_long winsize, newoff;

		offset = mp->dmai_mapping & MMU_PAGEOFFSET;
		winsize = mmu_ptob(mp->dmai_ndvmapages - mmu_btopr(offset));

		if ((mp->dmai_rflags & DDI_DMA_PARTIAL) == 0) {
			return (DDI_FAILURE);
		}

		if (*lenp != (u_int)-1 && *lenp != winsize) {
			return (DDI_FAILURE);
		}
		newoff = (u_long)*offp;
		if (newoff & (winsize - 1)) {
			return (DDI_FAILURE);
		}
		if (newoff > mp->dmai_object.dmao_size - mp->dmai_minxfer) {
			return (DDI_FAILURE);
		}
		*offp = (off_t)newoff;
		*lenp = (u_int)winsize;

		rootnex_map_window(mp, newoff, winsize);

		if ((cp = (ddi_dma_cookie_t *)objp) != 0) {
			cp->dmac_notused = 0;
			cp->dmac_address = mp->dmai_mapping;
			cp->dmac_size = mp->dmai_size;
			cp->dmac_type = 0;
		}
		break;
	}
	case DDI_DMA_REPWIN:
		if ((mp->dmai_rflags & DDI_DMA_PARTIAL) == 0) {
			return (DDI_FAILURE);
		}
		*offp = (off_t)mp->dmai_offset;
		addr = mp->dmai_ndvmapages -
		    mmu_btopr(mp->dmai_mapping & MMU_PAGEOFFSET);
		*lenp = (u_int)mmu_ptob(addr);
		break;

	case DDI_DMA_GETERR:
		break;

	case DDI_DMA_COFF:
		cp = (ddi_dma_cookie_t *)offp;
		addr = cp->dmac_address;
		if (addr < mp->dmai_mapping ||
		    addr >= mp->dmai_mapping + mp->dmai_size)
			return (DDI_FAILURE);
		*objp = (caddr_t)(addr - mp->dmai_mapping);
		break;

	case DDI_DMA_RESERVE:
	{
		struct ddi_dma_req *dmareqp;
		ddi_dma_lim_t *dma_lim;
		ddi_dma_handle_t *handlep;
		u_long addrlow, addrhigh;

		dmareqp = (struct ddi_dma_req *)offp;
		dma_lim = dmareqp->dmar_limits;
		if (dma_lim->dlim_burstsizes == 0) {
			return (DDI_DMA_BADLIMITS);
		}
		addrlow = dma_lim->dlim_addr_lo;
		addrhigh = dma_lim->dlim_addr_hi;
		if ((addrhigh <= addrlow) || (addrhigh < (u_long)KERNELBASE)) {
			return (DDI_DMA_BADLIMITS);
		}
		/*
		 * If the DMA engine has any limits, don't fool
		 * around with dvma reservation. We only have
		 * a small and fast map in the top address range.
		 * If the DMA engine has no limits, we give him a carte
		 * blanche because we can pass back any incoming
		 * kernel address as the dvma address.
		 */
		if ((addrlow != 0) || (addrhigh != (u_int)-1) ||
		    (dma_lim->dlim_cntr_max != (u_int)-1)) {
			return (DDI_DMA_NORESOURCES);
		}
		mutex_enter(&dma_pool_lock);
		mp = kmem_fast_alloc(&dmaimplbase, sizeof (*mp), 2, KM_SLEEP);
		mutex_exit(&dma_pool_lock);
		ASSERT(mp);
		mp->dmai_rdip = rdip;
		mp->dmai_minxfer = dma_lim->dlim_minxfer;
		mp->dmai_burstsizes = dma_lim->dlim_burstsizes;
		mp->dmai_ndvmapages = dmareqp->dmar_object.dmao_size;
		mp->dmai_rflags = DMP_BYPASSNEXUS;
		handlep = (ddi_dma_handle_t *)objp;
		*handlep = (ddi_dma_handle_t)mp;
		break;
	}
	case DDI_DMA_RELEASE:
	{
		mutex_enter(&dma_pool_lock);
		kmem_fast_free(&dmaimplbase, (caddr_t)mp);
		mutex_exit(&dma_pool_lock);

		if (dvma_call_list_id != 0) {
			ddi_run_callback(&dvma_call_list_id);
		}
		break;
	}

	default:
		DMAPRINT1("unknown 0%x\n", request);
		return (DDI_FAILURE);
	}
	return (DDI_SUCCESS);
}

static int
dev2devdma(dev_info_t *rdip, struct ddi_dma_req *dmareq, ddi_dma_impl_t *mp,
    void *ptes, int nptes)
{
	struct dma_phys_mapc pd;
	pd.dma_req = dmareq;
	pd.mp = mp;
	pd.nptes = nptes;
	pd.ptes = ptes;
	return (ddi_ctlops(rdip, rdip, DDI_CTLOPS_DMAPMAPC, 0, (void *)&pd));
}

/*
 * Root nexus ctl functions
 */

static int
rootnex_ctl_reportdev(dev_info_t *dev)
{
	register char *name;
	register int i, n;

	cmn_err(CE_CONT, "?%s%d at root", DEVI(dev)->devi_name,
	    DEVI(dev)->devi_instance);

	for (i = 0; i < sparc_pd_getnreg(dev); i++) {

		register struct regspec *rp = sparc_pd_getreg(dev, i);

		if (i == 0)
			cmn_err(CE_CONT, "?: ");
		else
			cmn_err(CE_CONT, "? and ");

		switch (rp->regspec_bustype) {

		case OBIO:
			name = "obio";
			break;

		default:
			cmn_err(CE_CONT, "?space %x offset %x",
			    rp->regspec_bustype, rp->regspec_addr);
			continue;
		}
		cmn_err(CE_CONT, "?%s 0x%x", name, rp->regspec_addr);
	}

	for (i = 0, n = sparc_pd_getnintr(dev); i < n; i++) {

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

/*
 * For this rootnexus, we're guaranteed to be passed a plain
 * list of ipls in the 'cpu' namespace from an onboard device
 * (The 'sbus' nexus takes care of the rest).
 */
static int
rootnex_xlate_intrs(dev_info_t *dip, dev_info_t *rdip, int *in,
	struct ddi_parent_private_data *pdptr)
{
	register size_t size;
	register int n;
	register struct intrspec *new;

	static char bad_intr_fmt[] =
	    "rootnex: bad interrupt spec from %s%d - sparc ipl %d\n";

#ifdef	lint
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

	int n, *ptr;
	struct ddi_parent_private_data *pdp;

	switch (ctlop) {
	case DDI_CTLOPS_DMAPMAPC:
		/*
		 * Return 'partial' to indicate that dma mapping
		 * has to be done in the main MMU.
		 */
		return (DDI_DMA_PARTIAL);

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

	case DDI_CTLOPS_DVMAPAGESIZE:
		*(u_long *)result = MMU_PAGESIZE;
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
