/*
 * Copyright (c) 1991-1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma	ident	"@(#)iommunex.c	1.69	95/02/20 SMI"

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/sysmacros.h>
#include <sys/errno.h>
#include <sys/debug.h>
#include <sys/mmu.h>
#include <sys/pte.h>
#include <sys/ddidmareq.h>
#include <sys/devops.h>
#include <sys/ddi_impldefs.h>
#include <sys/ddi_implfuncs.h>
#include <sys/modctl.h>
#include <sys/cpu.h>
#include <sys/kmem.h>
#include <sys/cmn_err.h>
#include <vm/seg.h>
#include <sys/map.h>
#include <sys/mman.h>
#include <vm/hat.h>
#include <vm/as.h>
#include <vm/page.h>
#include <sys/autoconf.h>
#include <sys/vmmac.h>
#include <sys/avintr.h>
#include <sys/machsystm.h>
#include <sys/bt.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>

#include <vm/hat_srmmu.h>
#include <sys/iommu.h>

extern int do_pg_coloring;
extern void vac_color_sync();

#define	VCOLOR(pp) (pp->p_vcolor << MMU_PAGESHIFT)
#define	ALIGNED(pp, addr) (VCOLOR(pp) == (addr & vac_mask))

static int
iommunex_dma_map(dev_info_t *dip, dev_info_t *rdip,
    struct ddi_dma_req *dmareq, ddi_dma_handle_t *handlep);

static int
iommunex_dma_allochdl(dev_info_t *, dev_info_t *, ddi_dma_attr_t *,
    int (*waitfp)(caddr_t), caddr_t arg, ddi_dma_handle_t *);

static int
iommunex_dma_freehdl(dev_info_t *, dev_info_t *, ddi_dma_handle_t);

static int
iommunex_dma_bindhdl(dev_info_t *, dev_info_t *, ddi_dma_handle_t,
    struct ddi_dma_req *, ddi_dma_cookie_t *, u_int *);

static int
iommunex_dma_unbindhdl(dev_info_t *, dev_info_t *, ddi_dma_handle_t);

static int
iommunex_dma_flush(dev_info_t *, dev_info_t *, ddi_dma_handle_t,
    off_t, u_int, u_int);

static int
iommunex_dma_win(dev_info_t *, dev_info_t *, ddi_dma_handle_t,
    uint_t, off_t *, uint_t *, ddi_dma_cookie_t *, uint_t *);

static int
iommunex_dma_mctl(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle, enum ddi_dma_ctlops request,
    off_t *offp, u_int *lenp, caddr_t *objp, u_int cache_flags);

static int
iommunex_ctlops(dev_info_t *, dev_info_t *, ddi_ctl_enum_t,
    void *, void *);

static struct bus_ops iommunex_bus_ops = {
	BUSO_REV,
	i_ddi_bus_map,
	i_ddi_get_intrspec,
	i_ddi_add_intrspec,
	i_ddi_remove_intrspec,
	i_ddi_map_fault,
	iommunex_dma_map,
	iommunex_dma_allochdl,
	iommunex_dma_freehdl,
	iommunex_dma_bindhdl,
	iommunex_dma_unbindhdl,
	iommunex_dma_flush,
	iommunex_dma_win,
	iommunex_dma_mctl,
	iommunex_ctlops,
	ddi_bus_prop_op
};

static int iommunex_identify(dev_info_t *);
static int iommunex_attach(dev_info_t *, ddi_attach_cmd_t);

static struct dev_ops iommu_ops = {
	DEVO_REV,
	0,		/* refcnt */
	ddi_no_info,	/* info */
	iommunex_identify,
	0,		/* probe */
	iommunex_attach,
	nodev,		/* detach */
	nodev,		/* reset */
	0,		/* cb_ops */
	&iommunex_bus_ops
};

static struct modldrv modldrv = {
	&mod_driverops,	/* Type of module.  This one is a driver */
	"iommu nexus driver", /* Name of module. */
	&iommu_ops,	/* driver ops */
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

#define	TSUNAMI_CONTROL_STORE_BUG
#ifdef TSUNAMI_CONTROL_STORE_BUG
extern tsunami_control_store_bug;
#endif

/*
 * protected by ddi_callback_mutex (in ddi_set_callback(),
 * and in real_callback_run())
 */
static int dvma_call_list_id = 0;

static int dma_reserve = SBUSMAP_MAXRESERVE;

static caddr_t dmaimplbase;		/* protected by dma_pool_lock */
static kmutex_t dma_pool_lock;

static int
iommunex_identify(dev_info_t *devi)
{
	if (strcmp(ddi_get_name(devi), "iommu") == 0) {
		return (DDI_IDENTIFIED);
	}
	return (DDI_NOT_IDENTIFIED);
}

static int
iommunex_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	if (cmd != DDI_ATTACH)
		return (DDI_FAILURE);

	mutex_init(&dma_pool_lock, "iommu dma pool lock",
	    MUTEX_DRIVER, NULL);

	/*
	 * If this *ever* changes, we'd better fail horribly fast..
	 */
	/*CONSTANTCONDITION*/
	ASSERT(MMU_PAGESIZE == IOMMU_PAGE_SIZE);

	ddi_report_dev(devi);
	return (DDI_SUCCESS);
}

/*
 * DMA routines for sun4m platform
 */

#define	PTECSIZE	(64)

/*
 * XXX	what really needs to happen here is we need to move the
 *	VME iocache stuff out into the sun4m VME nexus. It just doesn't
 *	belong here, folks.
 */
#ifdef IOC
#define	IS_ODD_PFN(pfn) ((pfn) & 1)
#endif /* IOC */

extern void srmmu_vacsync(u_int);
extern int fd_page_vac_align(page_t *);
extern void flush_writebuffers(void);
extern void pac_flushall(void);

static void iommunex_vacsync(ddi_dma_impl_t *, int, u_int, off_t, u_int);
static void iommunex_map_pte(int, struct pte *, u_int, iommu_pte_t *,
		u_long, int);
static void iommunex_map_pp(int, page_t *, u_int, u_long, iommu_pte_t *, int);
static void iommunex_map_window(ddi_dma_impl_t *, u_long, u_long);

#ifdef DEBUG
extern int sema_held(ksema_t *);
#endif

extern int impl_read_hwmap(struct as *, caddr_t, int, struct pte *, int);
extern u_long getdvmapages(int, u_long, u_long, u_int, u_int, int);
extern void putdvmapages(u_long, int);

static int dev2devdma(dev_info_t *, struct ddi_dma_req *, ddi_dma_impl_t *,
    void *, int);

/* #define	DMADEBUG */
#if defined(DMADEBUG) || defined(lint) || defined(__lint)
int dmadebug;
#else
#define	dmadebug	0
#endif	/* DMADEBUG */

#define	DMAPRINTF			if (dmadebug) printf
#define	DMAPRINT(x)			DMAPRINTF(x)
#define	DMAPRINT1(x, a)			DMAPRINTF(x, a)
#define	DMAPRINT2(x, a, b)		DMAPRINTF(x, a, b)
#define	DMAPRINT3(x, a, b, c)		DMAPRINTF(x, a, b, c)
#define	DMAPRINT4(x, a, b, c, d)	DMAPRINTF(x, a, b, c, d)
#define	DMAPRINT5(x, a, b, c, d, e)	DMAPRINTF(x, a, b, c, d, e)
#define	DMAPRINT6(x, a, b, c, d, e, f)	DMAPRINTF(x, a, b, c, d, e, f)

static int
iommunex_dma_setup(dev_info_t *rdip, struct ddi_dma_req *dmareq,
    u_long addrlow, u_long addrhigh, u_int segalign, u_int burstsizes,
    ddi_dma_impl_t *mp)
{
	extern struct as kas;
	auto struct pte stackptes[PTECSIZE + 1];
	auto struct pte *allocpte;
	struct pte *ptep;
	page_t *pp;
	u_int smask;
	struct as *as;
	u_int size, align;
	u_long addr, offset;
	int red, npages, rval;
	int memtype, naptes = 0;
	int mappages;
	iommu_pte_t *piopte;
	u_int iom_flag;
	u_long ioaddr;

#ifdef IOC
	int start_padding;
	int vmereq;
#else /* IOC */
#define	start_padding		0
#endif /* IOC */

#ifdef IOC
	vmereq = dmareq->dmar_flags & DMP_VMEREQ;
#endif /* IOC */

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
	 * Validate the DMA request.
	 */
	switch (dmareq->dmar_object.dmao_type) {
	case DMA_OTYP_VADDR:
		addr = (u_long)dmareq->dmar_object.dmao_obj.virt_obj.v_addr;
		offset = addr & MMU_PAGEOFFSET;
		as = dmareq->dmar_object.dmao_obj.virt_obj.v_as;
		if (as == NULL)
			as = &kas;
		addr &= ~MMU_PAGEOFFSET;
		npages = mmu_btopr(dmareq->dmar_object.dmao_size + offset);
		if (npages > (PTECSIZE + 1)) {
			allocpte = (struct pte *)
			    kmem_alloc(npages * sizeof (struct pte), KM_SLEEP);
			if (mp)
				mp->dmai_minfo = (void *)allocpte;
			naptes = npages;
			ptep = allocpte;
		} else {
			ptep = stackptes;
		}

		memtype = impl_read_hwmap(as, (caddr_t)addr, npages, ptep, 1);

		switch (memtype) {
		case BT_DRAM:
		case BT_NVRAM:

			/*
			 * just up to 32 byte bursts to memory on sun4m
			 */
			if (dmareq->dmar_flags & DDI_DMA_SBUS_64BIT) {
				burstsizes &= 0x3F003F;
			} else {
				burstsizes &= 0x3F;
			}
			if (burstsizes == 0) {
				rval = DDI_DMA_NOMAPPING;
				goto bad;
			}
			if (mp)
				mp->dmai_burstsizes = burstsizes;
			break;

			/*
			 * Object is not primary memory.
			 */
#ifdef IOC
		case BT_VME:
			/*
			 * (One reason why it needs more thought is because
			 * we really haven't got dev-to-dev DMA sorted out
			 * properly w.r.t. arbitrary bus configurations yet)
			 */
			if (!vmereq) {
				/* SBus -> VME not allowed */
				rval = DDI_DMA_NOMAPPING;
				goto bad;
			}

			/*
			 * must be VME -> VME
			 */
			rval = dev2devdma(rdip, dmareq, mp,
			    (void *)ptep, npages);
			if (rval == DDI_SUCCESS)
				goto out;
			else
				goto bad;
			/*NOTREACHED*/
#endif	/* IOC */

		case BT_SBUS:
		case BT_VIDEO:
			/* go ahead and map it as usual. */
			break;
		default:
			rval = DDI_DMA_NOMAPPING;
			goto bad;
		}
		pp = NULL;
		break;

	case DMA_OTYP_PAGES:
		pp = dmareq->dmar_object.dmao_obj.pp_obj.pp_pp;
		ASSERT(SEMA_HELD(&pp->p_iolock));
		offset = dmareq->dmar_object.dmao_obj.pp_obj.pp_offset;
		npages = mmu_btopr(dmareq->dmar_object.dmao_size + offset);
		ptep = stackptes;
		break;

	case DMA_OTYP_PADDR:
	default:
		rval = DDI_DMA_NOMAPPING;
		goto bad;
	}

	if (mp == NULL) {
		if (naptes) {
			kmem_free((caddr_t)allocpte,
			    naptes * sizeof (struct pte));
		}
		goto out;
	}

	if (mp->dmai_rflags & DDI_DMA_PARTIAL) {
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
		 * If the size requested is less than a moderate amt,
		 * skip the partial mapping stuff - it's not worth the
		 * effort.
		 */
		if (npages > PTECSIZE + 1) {
			npages = PTECSIZE + mmu_btopr(offset);
			size = mmu_ptob(PTECSIZE);
		} else {
			mp->dmai_rflags ^= DDI_DMA_PARTIAL;
		}
	} else {
		if (npages >= mmu_btop(IOMMU_DVMA_RANGE) - 0x40) {
			rval = DDI_DMA_TOOBIG;
			goto bad;
		}
	}
	mp->dmai_size = size;
	mp->dmai_ndvmapages = npages;
	mappages = npages;

	/*
	 * Try and get a vac aligned DVMA mapping (if VAC_IOCOHERENT).
	 */

#ifdef IOC
	if (vmereq) {
		/*
		 * On VME devices with IOC, we default an even page
		 * alignment requirement but this may be overridden
		 * by the even stronger requirement, vac alignment,
		 * which is the following if (vac) test.
		 *
		 *  XXX fix this
		 *	this should be even page alignment however
		 *	0 is 0 modolu cache size which is a bit
		 *	of an overkill
		 */
		align = (u_int)0;
	} else
#endif /* IOC */
		align = (u_int)-1;

	/*
	 * we only want to do the aliasing dance if the
	 * platform provides I/O cache coherence.
	 */
	if (vac && (cache & CACHE_IOCOHERENT)) {
		if (pp != NULL) {
			if (do_pg_coloring)
				align = VCOLOR(pp);
			else {
				hat_mlist_enter(pp);
				align = fd_page_vac_align(pp);
				hat_mlist_exit(pp);
				align = mmu_ptob(align);
			}
		} else {
			align = addr & vac_mask;
		}
	}
	red = ((mp->dmai_rflags & DDI_DMA_REDZONE)? 1 : 0);

#ifdef IOC
	/*
	 * we need a padding if we start with an odd pfn.
	 */
	start_padding = 0;
	if (vmereq) {
		if (!IS_ODD_PFN(iommu_btop(align) + npages - 1)) {
			mappages++;
			/*
			 * don't waste a page for red zone since
			 * we just padded a page at the end.
			 */
			if (red)
				red = 0;
		}

		if (IS_ODD_PFN(iommu_btop(align))) {
			start_padding = 1;
			mappages++;
			align -= IOMMU_PAGE_SIZE;
		}
	}
#endif /* IOC */

	ioaddr = getdvmapages(mappages + red, addrlow, addrhigh, align,
			segalign, (dmareq->dmar_fp == DDI_DMA_SLEEP) ? 1 : 0);
	if (ioaddr == 0) {
		if (dmareq->dmar_fp == DDI_DMA_SLEEP)
			rval = DDI_DMA_NOMAPPING;
		else
			rval = DDI_DMA_NORESOURCES;
		goto bad;
	}
	ASSERT((caddr_t)ioaddr >= (caddr_t)IOMMU_DVMA_BASE);
	if (start_padding)
		ioaddr += IOMMU_PAGE_SIZE;

	mp->dmai_mapping = (u_long)(ioaddr + offset);
	ASSERT((mp->dmai_mapping & ~segalign) ==
	    ((mp->dmai_mapping + (mp->dmai_size - 1)) & ~segalign));

	piopte = &ioptes[iommu_btop((caddr_t)ioaddr - IOMMU_DVMA_BASE)];
	ASSERT(piopte != NULL);

	iom_flag = (mp->dmai_rflags & DDI_DMA_READ) ? IOPTE_WRITE : 0;

	if (pp)
		iommunex_map_pp(npages, pp, iom_flag, ioaddr, piopte, red);
	else
		iommunex_map_pte(npages, ptep, iom_flag, piopte, ioaddr, red);

out:
	if (mp) {
		mp->dmai_object = dmareq->dmar_object;
		if (mp->dmai_rflags & DDI_DMA_PARTIAL) {
			size = iommu_ptob(
				mp->dmai_ndvmapages - iommu_btopr(offset));
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
iommunex_dma_map(dev_info_t *dip, dev_info_t *rdip,
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
	 * If not an advisory call, get a DMA handle
	 */
	if (handlep) {
		mutex_enter(&dma_pool_lock);
		mp = (ddi_dma_impl_t *)kmem_fast_alloc(&dmaimplbase,
		    sizeof (*mp), 2, (dmareq->dmar_fp == DDI_DMA_SLEEP) ?
		    KM_SLEEP : KM_NOSLEEP);
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
	 */
	addrlow = dma_lim->dlim_addr_lo;
	addrhigh = dma_lim->dlim_addr_hi;
	if ((addrhigh <= addrlow) || (addrhigh < (u_long)IOMMU_DVMA_BASE)) {
		rval = DDI_DMA_NOMAPPING;
		goto bad;
	}
	rval = iommunex_dma_setup(rdip, dmareq, addrlow, addrhigh,
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

int
iommunex_dma_allochdl(dev_info_t *dip, dev_info_t *rdip,
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
	if ((addrhigh <= addrlow) || (addrhigh < (u_long)IOMMU_DVMA_BASE)) {
		return (DDI_DMA_BADATTR);
	}

	mutex_enter(&dma_pool_lock);
	mp = (ddi_dma_impl_t *)kmem_fast_zalloc(&dmaimplbase, sizeof (*mp), 2,
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

static int
iommunex_dma_freehdl(dev_info_t *dip, dev_info_t *rdip,
	ddi_dma_handle_t handle)
{
	ddi_dma_impl_t *mp = (ddi_dma_impl_t *)handle;

#ifdef lint
	dip = dip;
	rdip = rdip;
#endif

	mutex_enter(&dma_pool_lock);
	kmem_fast_free(&dmaimplbase, (caddr_t)mp);
	mutex_exit(&dma_pool_lock);

	if (dvma_call_list_id != 0) {
		ddi_run_callback(&dvma_call_list_id);
	}
	return (DDI_SUCCESS);
}

static int
iommunex_dma_bindhdl(dev_info_t *dip, dev_info_t *rdip,
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
	mp->dmai_rflags = dmareq->dmar_flags & DMP_DDIFLAGS;
	dma_attr = &mp->dmai_attr;
	addrlow = (u_long)dma_attr->dma_attr_addr_lo;
	addrhigh = (u_long)dma_attr->dma_attr_addr_hi;

	rval = iommunex_dma_setup(rdip, dmareq, addrlow, addrhigh,
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

static void
iommunex_map_pte(int npages, struct pte *ptep, u_int iom_flag,
	iommu_pte_t *piopte, u_long ioaddr, int red)
{
	u_int pfn;
	page_t *pp;

	/*
	 * Note: Since srmmu and iommu pte fit per sun4m arch spec
	 *	 we could just do a
	 *		 *((u_int *)piopte) = *ptep
	 *	 and skip that jazz but who knows what might break
	 */
	while (npages > 0) {
		/* always starts with $ DVMA */
		iom_flag |= IOPTE_CACHE|IOPTE_VALID;

		pfn = MAKE_PFNUM(ptep);
		/*
		 * whether it's VAC or PAC, it works
		 * the same way here.
		 */
		if (!ptep->Cacheable) {
			iom_flag &= ~IOPTE_CACHE;
		}

		if (do_pg_coloring && vac) {
			pp = page_numtopp_nolock(pfn);
			if (pp && !ALIGNED(pp, ioaddr))
				vac_color_sync(VCOLOR(pp), pfn);
		}

		*((u_int *)piopte) = (pfn << IOPTE_PFN_SHIFT) | iom_flag;

#ifdef	TSUNAMI_CONTROL_STORE_BUG
		if (!tsunami_control_store_bug) {
			iommu_addr_flush((int)ioaddr & IOMMU_FLUSH_MSK);
		}
#else
		iommu_addr_flush((int)ioaddr & IOMMU_FLUSH_MSK);
#endif	/* TSUNAMI_CONTROL_STORE_BUG */

		piopte++;
		npages--;
		ptep++;
		ioaddr += IOMMU_PAGE_SIZE;
	}
	if (red) {
		iommu_pteunload(piopte);
	}

#ifdef	TSUNAMI_CONTROL_STORE_BUG
	if (tsunami_control_store_bug) {
		mmu_flushall();
	}
#endif	/* TSUNAMI_CONTROL_STORE_BUG */

}

static void
iommunex_map_pp(int npages, page_t *pp, u_int iom_flag, u_long ioaddr,
	iommu_pte_t *piopte, int red)
{
	u_int pfn;
	int align;

	while (npages > 0) {
		/* always start with $ DVMA */
		iom_flag |= IOPTE_CACHE|IOPTE_VALID;

		ASSERT(SEMA_HELD(&pp->p_iolock));
		pfn = page_pptonum(pp);
		if (vac && (cache & CACHE_IOCOHERENT)) {
			/*
			 * For I/O cache coherent VAC machines.
			 */
			hat_mlist_enter(pp);
			if (do_pg_coloring) {
				if (!ALIGNED(pp, ioaddr) && !PP_ISNC(pp)) {
					if (pp->p_mapping)
						iom_flag &= ~IOPTE_CACHE;
					vac_color_sync(VCOLOR(pp), pfn);
				}
			} else if (pp->p_mapping != NULL && !PP_ISNC(pp)) {
				align = fd_page_vac_align(pp);
				align = mmu_ptob(align);
				if (align != (ioaddr & vac_mask)) {
					/*
					 * NOTE: this is the case
					 * where the page is marked as
					 * $able in system MMU but we
					 * cannot alias it on the
					 * IOMMU, so we have to flush
					 * out the cache and do a
					 * non-$ DVMA on this page.
					 */
					iom_flag &= ~IOPTE_CACHE;
					srmmu_vacsync(pfn);
				}
			}
			hat_mlist_exit(pp);
		} else if (PP_ISNC(pp)) {
			iom_flag &= ~IOPTE_CACHE;
		}

		*((u_int *)piopte) = (pfn << IOPTE_PFN_SHIFT) | iom_flag;

		/*
		 * XXX This statement needs to be before the iommu tlb
		 * flush. For yet unknown reasons there is some timing
		 * problem in flushing the iommu tlb on SS5 which will
		 * affect the srmmu tlb lookup if pp->p_next misses the
		 * D$. You might get the wrong data loaded into the D$.
		 * see 1185222 for details.
		 */
		pp = pp->p_next;

#ifdef	TSUNAMI_CONTROL_STORE_BUG
		if (!tsunami_control_store_bug) {
			iommu_addr_flush((int)ioaddr & IOMMU_FLUSH_MSK);
		}
#else
		iommu_addr_flush((int)ioaddr & IOMMU_FLUSH_MSK);
#endif	/* TSUNAMI_CONTROL_STORE_BUG */

		piopte++;
		ioaddr += IOMMU_PAGE_SIZE;
		npages--;
	}

	if (red) {
		iommu_pteunload(piopte);
	}

#ifdef	TSUNAMI_CONTROL_STORE_BUG
	if (tsunami_control_store_bug) {
		mmu_flushall();
	}
#endif	/* TSUNAMI_CONTROL_STORE_BUG */
}

/*
 * For non-coherent caches (small4m), we always flush reads.
 */
#define	IOMMU_NC_FLUSH_READ(c, npages, mp, cflags, off, len)		\
{									\
	if (((c & (CACHE_VAC|CACHE_IOCOHERENT)) == CACHE_VAC) && npages)\
	    iommunex_vacsync(mp, npages, cflags, off, len);		\
	flush_writebuffers();						\
	if ((c & (CACHE_PAC|CACHE_IOCOHERENT)) == CACHE_PAC) {		\
		pac_flushall();						\
	}								\
}

/*
 * XXX	This ASSERT needs to be replaced by some code when machines
 *	that trip over it appear.
 */
#define	IOMMU_NC_FLUSH_WRITE(c)						\
{									\
	ASSERT((c & CACHE_IOCOHERENT) || !(c & CACHE_WRITEBACK));	\
}


static int
iommunex_dma_unbindhdl(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle)
{
	ddi_dma_impl_t *mp = (ddi_dma_impl_t *)handle;
	u_long addr;
	int red, npages;

#ifdef lint
	dip = dip;
	rdip = rdip;
#endif

	addr = mp->dmai_mapping & ~IOMMU_PAGE_OFFSET;
	ASSERT(iommu_ptefind(addr) != NULL);
	npages = mp->dmai_ndvmapages;

	/*
	 * flush IOC and do a free DDI_DMA_SYNC_FORCPU.
	 */
	if (mp->dmai_rflags & DDI_DMA_READ) {
		IOMMU_NC_FLUSH_READ(cache, npages, mp,
					DDI_DMA_SYNC_FORCPU, 0, 0);
	} else {
		IOMMU_NC_FLUSH_WRITE(cache);
	}

#ifdef DEBUG
	if (npages)
		iommu_unload(addr, npages);
#endif DEBUG

	red = ((mp->dmai_rflags & DDI_DMA_REDZONE)? 1 : 0);

#ifdef IOC
	/*
	 * need to recover the padding pages.
	 */
	if (mp->dmai_rflags & DMP_VMEREQ) {
		/*
		 * check whether we padded at the end.
		 */
		if (!IS_ODD_PFN(iommu_btop(addr) + npages - 1)) {
			npages++;
			if (red)
				red = 0;
		}
		/*
		 * see if we padded at the beginning, and if we did,
		 * adjust the addr and bump up number of pages
		 * to be unloaded
		 */
		if (IS_ODD_PFN(iommu_btop(addr))) {
			npages++;
			addr -= IOMMU_PAGE_SIZE;
		}

	}
#endif /* IOC */

	if (mp->dmai_minfo) {
		u_long addr;
		u_int naptes;

		addr = (u_long)mp->dmai_object.
			    dmao_obj.virt_obj.v_addr;
		naptes = mmu_btopr(mp->dmai_object.dmao_size +
			    (addr & MMU_PAGEOFFSET));
		kmem_free((caddr_t)mp->dmai_minfo,
		    naptes * sizeof (struct pte));
		mp->dmai_minfo = NULL;
	}

	if (npages) {
		putdvmapages(addr, npages + red);
	}
	mp->dmai_ndvmapages = 0;
	mp->dmai_inuse = 0;

	if (dvma_call_list_id != 0) {
		ddi_run_callback(&dvma_call_list_id);
	}
	return (DDI_SUCCESS);
}

/*ARGSUSED*/
static int
iommunex_dma_flush(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle, off_t off, u_int len,
    u_int cache_flags)
{
	ddi_dma_impl_t *mp = (ddi_dma_impl_t *)handle;
	int npages;

	npages = mp->dmai_ndvmapages;

	if ((cache_flags == DDI_DMA_SYNC_FORCPU) ||
		(cache_flags == DDI_DMA_SYNC_FORKERNEL)) {
		if (mp->dmai_rflags & DDI_DMA_READ) {
			IOMMU_NC_FLUSH_READ(cache, npages, mp,
						cache_flags, off, len);
		} else {
			IOMMU_NC_FLUSH_WRITE(cache);
		}
	}
	return (DDI_SUCCESS);
}

static int
iommunex_dma_win(dev_info_t *dip, dev_info_t *rdip,
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

	offset = mp->dmai_mapping & IOMMU_PAGE_OFFSET;
	winsize = iommu_ptob(mp->dmai_ndvmapages - iommu_btopr(offset));

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

	iommunex_map_window(mp, newoff, winsize);
	/*
	 * last window might be shorter.
	 */
	cookiep->dmac_size = mp->dmai_size;

	return (DDI_SUCCESS);
}

static void
iommunex_map_window(ddi_dma_impl_t *mp, u_long newoff, u_long winsize)
{
	u_long addr;
	int npages;
	struct pte *ptep;
	page_t *pp;
	u_long flags;
	iommu_pte_t *piopte;
	u_int iom_flag;

	addr = mp->dmai_mapping & ~IOMMU_PAGE_OFFSET;
	npages = mp->dmai_ndvmapages;

	/*
	 * flush IOC and do a free DDI_DMA_SYNC_FORCPU.
	 */
	if (mp->dmai_rflags & DDI_DMA_READ) {
		IOMMU_NC_FLUSH_READ(cache, npages, mp,
					DDI_DMA_SYNC_FORCPU, 0, 0);
	} else {
		IOMMU_NC_FLUSH_WRITE(cache);
	}

#ifdef DEBUG
	if (npages)
		iommu_unload(addr, npages);
#endif DEBUG

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
			ASSERT(SEMA_HELD(&pp->p_iolock));
			pp = pp->p_next;
			flags += MMU_PAGESIZE;
		}
	}

	/*
	 * The original page offset is always held in dmai_mapping
	 */
	npages = mmu_btopr(mp->dmai_size +
			(mp->dmai_mapping & MMU_PAGEOFFSET));

	DMAPRINT1("iommunex_map_window: remapping %d pages\n", npages);

	piopte = iommu_ptefind(mp->dmai_mapping);
	ASSERT(piopte != NULL);

	iom_flag = (mp->dmai_rflags & DDI_DMA_READ) ? IOPTE_WRITE : 0;

	if (pp) {
		iommunex_map_pp(npages, pp, iom_flag, mp->dmai_mapping,
			piopte, 0);
	} else {
		iommunex_map_pte(npages, ptep, iom_flag, piopte, addr, 0);
	}
}


static int
iommunex_dma_mctl(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle, enum ddi_dma_ctlops request,
    off_t *offp, u_int *lenp,
    caddr_t *objp, u_int cache_flags)
{
	ddi_dma_impl_t *mp = (ddi_dma_impl_t *)handle;
	ddi_dma_cookie_t *cp;
	u_long addr, offset;
	int npages;

#ifdef lint
	dip = dip;
#endif

	DMAPRINT1("dma_mctl: handle %x ", (int)mp);

	switch (request) {
	case DDI_DMA_FREE:
	{
		int red;

		addr = mp->dmai_mapping & ~IOMMU_PAGE_OFFSET;
		ASSERT(iommu_ptefind(addr) != NULL);
		npages = mp->dmai_ndvmapages;

		/*
		 * flush IOC and do a free DDI_DMA_SYNC_FORCPU.
		 */
		if (mp->dmai_rflags & DDI_DMA_READ) {
			IOMMU_NC_FLUSH_READ(cache, npages, mp,
						cache_flags, 0, 0);
		} else {
			IOMMU_NC_FLUSH_WRITE(cache);
		}

#ifdef DEBUG
		/*
		 * only flush TLB in debugging mode. TLB gets flushed
		 * at load time
		 */
		if (npages)
			iommu_unload(addr, npages);
#endif DEBUG

		red = ((mp->dmai_rflags & DDI_DMA_REDZONE)? 1 : 0);

#ifdef IOC
		/*
		 * need to recover the padding pages.
		 */
		if (mp->dmai_rflags & DMP_VMEREQ) {
			if (!IS_ODD_PFN(iommu_btop(addr) + npages - 1)) {
				npages++;
				if (red)
					red = 0;
			}
			if (IS_ODD_PFN(iommu_btop(addr))) {
				npages++;
				addr -= IOMMU_PAGE_SIZE;
			}
		}
#endif /* IOC */

		if (mp->dmai_minfo) {
			u_long addr;
			u_int naptes;

			addr = (u_long) mp->dmai_object.
				    dmao_obj.virt_obj.v_addr;
			naptes = mmu_btopr(mp->dmai_object.dmao_size +
				    (addr & MMU_PAGEOFFSET));
			kmem_free((caddr_t)mp->dmai_minfo,
			    naptes * sizeof (struct pte));
		}

		if (npages) {
			putdvmapages(addr, npages + red);
		}

		/*
		 * put impl struct back on free list
		 */
		mutex_enter(&dma_pool_lock);
		kmem_fast_free(&dmaimplbase, (caddr_t)mp);
		mutex_exit(&dma_pool_lock);

		if (dvma_call_list_id != 0) {
			ddi_run_callback(&dvma_call_list_id);
		}
		break;
	}

	case DDI_DMA_SYNC:
		addr = mp->dmai_mapping & ~IOMMU_PAGE_OFFSET;
		ASSERT(iommu_ptefind(addr) != NULL);
		npages = mp->dmai_ndvmapages;

		if ((cache_flags == DDI_DMA_SYNC_FORCPU) ||
			(cache_flags == DDI_DMA_SYNC_FORKERNEL)) {
			if (mp->dmai_rflags & DDI_DMA_READ) {
				ASSERT(offp != (off_t)NULL);
				ASSERT(lenp != (u_int *)NULL);
				IOMMU_NC_FLUSH_READ(cache, npages, mp,
						cache_flags, *offp, *lenp);
			} else {
				IOMMU_NC_FLUSH_WRITE(cache);
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
		return (DDI_FAILURE);

	case DDI_DMA_NEXTWIN:
	{
		ddi_dma_win_t *owin, *nwin;
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

			offset = mp->dmai_mapping & IOMMU_PAGE_OFFSET;
			winsize = iommu_ptob(mp->dmai_ndvmapages -
				iommu_btopr(offset));
			newoff = mp->dmai_offset + winsize;
			if (newoff > mp->dmai_object.dmao_size -
				mp->dmai_minxfer) {
				return (DDI_DMA_DONE);
			}
			iommunex_map_window(mp, newoff, winsize);

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
		ddi_dma_seg_impl_t *seg;

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

		offset = mp->dmai_mapping & IOMMU_PAGE_OFFSET;
		winsize = iommu_ptob(mp->dmai_ndvmapages - iommu_btopr(offset));

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

		iommunex_map_window(mp, newoff, winsize);

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
		    iommu_btopr(mp->dmai_mapping & IOMMU_PAGE_OFFSET);
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
		u_int np, dvma_pfn;
		u_long ioaddr;

		dmareqp = (struct ddi_dma_req *)offp;
		dma_lim = dmareqp->dmar_limits;
		if (dma_lim->dlim_burstsizes == 0) {
			return (DDI_DMA_BADLIMITS);
		}
		addrlow = dma_lim->dlim_addr_lo;
		addrhigh = dma_lim->dlim_addr_hi;
		if ((addrhigh <= addrlow) ||
		    (addrhigh < (u_long)IOMMU_DVMA_BASE)) {
			return (DDI_DMA_BADLIMITS);
		}
		np = dmareqp->dmar_object.dmao_size;
		mutex_enter(&dma_pool_lock);
		if (np > dma_reserve) {
			mutex_exit(&dma_pool_lock);
			return (DDI_DMA_NORESOURCES);
		}
		dma_reserve -= np;
		mp = (ddi_dma_impl_t *)kmem_fast_zalloc(&dmaimplbase,
		    sizeof (*mp), 2, KM_SLEEP);
		mutex_exit(&dma_pool_lock);
		ASSERT(mp);
		mp->dmai_rdip = rdip;
		mp->dmai_minxfer = dma_lim->dlim_minxfer;
		mp->dmai_burstsizes = dma_lim->dlim_burstsizes;
		ioaddr = getdvmapages(np, addrlow, addrhigh, (u_int)-1,
				dma_lim->dlim_cntr_max, 1);
		if (ioaddr == 0) {
			mutex_enter(&dma_pool_lock);
			dma_reserve += np;
			kmem_fast_free(&dmaimplbase, (caddr_t)mp);
			mutex_exit(&dma_pool_lock);
			return (DDI_DMA_NOMAPPING);
		}
		dvma_pfn = iommu_btop(ioaddr - IOMMU_DVMA_BASE);
		mp->dmai_mapping = (u_long)dvma_pfn;
		mp->dmai_rflags = DMP_BYPASSNEXUS;
		mp->dmai_ndvmapages = np;
		handlep = (ddi_dma_handle_t *)objp;
		*handlep = (ddi_dma_handle_t)mp;
		break;
	}
	case DDI_DMA_RELEASE:
	{
		u_long ioaddr, dvma_pfn;

		dvma_pfn = mp->dmai_mapping;
		ioaddr = iommu_ptob(dvma_pfn) + IOMMU_DVMA_BASE;
		putdvmapages(ioaddr, mp->dmai_ndvmapages);
		mutex_enter(&dma_pool_lock);
		dma_reserve += mp->dmai_ndvmapages;
		mutex_exit(&dma_pool_lock);

		mutex_enter(&dma_pool_lock);
		kmem_fast_free(&dmaimplbase, (caddr_t)mp);
		mutex_exit(&dma_pool_lock);

		if (dvma_call_list_id != 0) {
			ddi_run_callback(&dvma_call_list_id);
		}
		break;
	}

	default:
		DMAPRINT1("unknown 0x%x\n", request);
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

static int
iommunex_report_dev(dev_info_t *dip, dev_info_t *rdip)
{
	register int i, n;
	register dev_info_t *pdev;
	extern int impl_bustype(u_int);

#ifdef lint
	dip = dip;
#endif

	if (DEVI_PD(rdip) == NULL)
		return (DDI_FAILURE);

	pdev = (dev_info_t *)DEVI(rdip)->devi_parent;
	cmn_err(CE_CONT, "?%s%d at %s%d",
	    DEVI(rdip)->devi_name, DEVI(rdip)->devi_instance,
	    DEVI(pdev)->devi_name, DEVI(pdev)->devi_instance);

	for (i = 0, n = sparc_pd_getnreg(rdip); i < n; i++) {

		register struct regspec *rp = sparc_pd_getreg(rdip, i);
		register char *name;

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

static int
iommunex_ctlops(dev_info_t *dip, dev_info_t *rdip, ddi_ctl_enum_t op,
    void *a, void *r)
{
	switch (op) {

	default:
		return (ddi_ctlops(dip, rdip, op, a, r));

	case DDI_CTLOPS_REPORTDEV:
		return (iommunex_report_dev(dip, rdip));

	case DDI_CTLOPS_DVMAPAGESIZE:
		*(u_long *)r = IOMMU_PAGE_SIZE;
		return (DDI_SUCCESS);
	/*
	 * XXX	Bugid 1087610 - we need to deal with DDI_CTLOPS_PTOB etc.
	 * XXX	At the risk of sounding like a broken record, this stuff
	 *	belongs in the VME nexus.
	 */
	}
}

static void
iommunex_vacsync(ddi_dma_impl_t *mp, int npages,
	u_int cache_flags, off_t offset, u_int length)
{

	page_t *pp;
	u_int pfn;
	register u_int addr, endmap;

	switch (mp->dmai_object.dmao_type) {

	case DMA_OTYP_VADDR:
		/*
		 * This indicates that the object was mapped
		 * non-cached, so we needn't flush it.
		 */
		if (mp->dmai_rflags & DDI_DMA_CONSISTENT)
			break;

		addr = mp->dmai_mapping + offset;
		endmap = mp->dmai_mapping + mp->dmai_size;

		ASSERT((addr >= mp->dmai_mapping) && (addr <= endmap));

		if ((length == 0) || (length == (u_int) -1) ||
		    ((addr + length) >= endmap))
			length = endmap - addr;

		/*
		 * If the object vaddr is below KERNELBASE then we need to
		 * flush in the correct context. Also, if the type of flush
		 * is not FORKERNEL then there may be more than one mapping
		 * for this object and we must flush them all.
		 */
		if ((mp->dmai_object.dmao_obj.virt_obj.v_addr <
			(caddr_t)KERNELBASE) ||
			(cache_flags != DDI_DMA_SYNC_FORKERNEL)) {

			npages = mmu_btopr(length + (addr & MMU_PAGEOFFSET));
			addr &= ~MMU_PAGEOFFSET;

			while (npages-- > 0) {
				ASSERT(iommu_ptefind(addr) != NULL);
				pfn = IOMMU_MK_PFN(iommu_ptefind(addr));
				pp = page_numtopp_nolock(pfn);
				if (pp) {
					hat_mlist_enter(pp);
					if (pp->p_mapping != NULL &&
					    !PP_ISNC(pp)) {
						srmmu_vacsync(pfn);
					}
					hat_mlist_exit(pp);
				}
				addr += MMU_PAGESIZE;
			}
		} else {
			vac_flush(mp->dmai_object.dmao_obj.virt_obj.v_addr
					+ offset, length);
		}
		break;

	case DMA_OTYP_PAGES:
		pp = mp->dmai_object.dmao_obj.pp_obj.pp_pp;
		while (npages-- > 0) {
			pfn = page_pptonum(pp);
			ASSERT(pp != (page_t *)NULL);
			ASSERT(SEMA_HELD(&pp->p_iolock));
			hat_mlist_enter(pp);
			if (pp->p_mapping != NULL && !PP_ISNC(pp)) {
				srmmu_vacsync(pfn);
			}
			hat_mlist_exit(pp);
			pp = pp->p_next;
		}
		break;

	case DMA_OTYP_PADDR:
		/* not support by IOMMU nexus */
	default:
		break;
	}
}
