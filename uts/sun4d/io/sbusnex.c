/*
 * Copyright (c) 1990-1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ident	"@(#)sbusnex.c	1.83	95/03/02 SMI"

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/ddi_impldefs.h>
#include <sys/cmn_err.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/debug.h>
#include <sys/sysmacros.h>
#include <sys/avintr.h>
#include <sys/spl.h>

#include <vm/seg.h>
#include <sys/map.h>
#include <sys/mman.h>
#include <sys/mmu.h>
#include <sys/pte.h>
#include <vm/hat.h>
#include <vm/page.h>

#include <sys/iocache.h>
#include <sys/physaddr.h>
#include <sys/iommu.h>

/*
 * Configuration information
 */

static int sbus_to_sparc(int), sparc_to_sbus(int);

static int
sbus_ctlops(dev_info_t *, dev_info_t *, ddi_ctl_enum_t, void *, void *);

static int
sbus_dma_map(dev_info_t *, dev_info_t *, struct ddi_dma_req *,
	ddi_dma_handle_t *);

static int
sbus_dma_allochdl(dev_info_t *, dev_info_t *, ddi_dma_attr_t *,
	int (*waitfp)(caddr_t), caddr_t arg, ddi_dma_handle_t *);

static int
sbus_dma_freehdl(dev_info_t *, dev_info_t *, ddi_dma_handle_t);

static int
sbus_dma_bindhdl(dev_info_t *, dev_info_t *, ddi_dma_handle_t,
	struct ddi_dma_req *, ddi_dma_cookie_t *, u_int *);

static int
sbus_dma_unbindhdl(dev_info_t *, dev_info_t *, ddi_dma_handle_t);

static int
sbus_dma_flush(dev_info_t *, dev_info_t *, ddi_dma_handle_t,
	off_t, u_int, u_int);

static int
sbus_dma_win(dev_info_t *, dev_info_t *, ddi_dma_handle_t,
	uint_t, off_t *, uint_t *, ddi_dma_cookie_t *, uint_t *);

static int
sbus_dma_mctl(dev_info_t *dip, dev_info_t *rdip, ddi_dma_handle_t handle,
	enum ddi_dma_ctlops request, off_t *offp, u_int *lenp,
	caddr_t *objp, u_int flags);

static int
sbus_add_intrspec(dev_info_t *dip, dev_info_t *rdip,
	ddi_intrspec_t intrspec, ddi_iblock_cookie_t *iblock_cookiep,
	ddi_idevice_cookie_t *idevice_cookiep,
	u_int (*int_handler)(caddr_t int_handler_arg),
	caddr_t int_handler_arg, int kind);

static void
sbus_remove_intrspec(dev_info_t *dip, dev_info_t *rdip,
	ddi_intrspec_t intrspec, ddi_iblock_cookie_t iblock_cookiep);

static struct bus_ops sbus_bus_ops = {
	BUSO_REV,
	i_ddi_bus_map,
	i_ddi_get_intrspec,
	sbus_add_intrspec,
	sbus_remove_intrspec,
	i_ddi_map_fault,
	sbus_dma_map,
	sbus_dma_allochdl,
	sbus_dma_freehdl,
	sbus_dma_bindhdl,
	sbus_dma_unbindhdl,
	sbus_dma_flush,
	sbus_dma_win,
	sbus_dma_mctl,
	sbus_ctlops,
	ddi_bus_prop_op
};

static int sbus_identify(dev_info_t *);
static int sbus_attach(dev_info_t *, ddi_attach_cmd_t cmd);

static struct dev_ops sbus_ops = {
	DEVO_REV,		/* devo_rev, */
	0,			/* refcnt  */
	ddi_no_info,		/* info */
	sbus_identify,		/* identify */
	0,			/* probe */
	sbus_attach,		/* attach */
	nodev,			/* detach */
	nodev,			/* reset */
	(struct cb_ops *)0,	/* driver operations */
	&sbus_bus_ops		/* bus operations */
};

static int fd_sbus_slot_id(dev_info_t *dip, dev_info_t *rdip);
static int fd_target_slot_id(struct pte *ptep, int cur_sbus_id);
static u_int recover_iom_flag(iommu_pte_t *piopte);
#ifdef DEV_TO_DEV_DMA
static int fd_target_sbus_id(struct dma_phys_mapc *pd);
#endif DEV_TO_DEV_DMA

static int sbus_dma_setup(dev_info_t *dip, dev_info_t *rdip,
    struct ddi_dma_req *dmareq, u_long addrlow, u_long addrhigh,
    u_int segalign, u_int burstsizes, ddi_dma_impl_t *mp);
static void sbus_map_pte(int, struct pte *, u_int, iommu_pte_t *, int);
static void sbus_map_pp(int, page_t *, u_int, iommu_pte_t *, int);
static void sbus_map_window(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_impl_t *, u_long, u_long);

extern int get_deviceid(int nodeid, int parent);
extern int nexus_note_sbus(u_int unit, struct autovec *handler);
extern int xdb_sbus_unit(int device_id);

extern struct sbus_private sbus_pri_data[];
extern int nsbus;
extern int sbus_nvect;		/* # intr's supported per level same slot */

#define	get_autovec_base(dip) \
	(((struct sbus_private *)(DEVI((dip))->devi_driver_data))->vec)

#include <sys/modctl.h>
extern struct mod_ops mod_driverops;

static struct modldrv modldrv = {
	&mod_driverops, 	/* Type of module.  This one is a driver */
	"SBus nexus driver",	/* Name of module. */
	&sbus_ops,		/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modldrv, NULL
};

/*
 * These are the module initialization routines.
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

static int sbus_burst_sizes;
static int sbus_max_burst_size, sbus_min_burst_size;
static u_int sbus_dma_reserve = SBUSMAP_MAXRESERVE;

static int
sbus_identify(dev_info_t *devi)
{
	char *name = ddi_get_name(devi);
	int rc = DDI_NOT_IDENTIFIED;

	if (strcmp(name, "sbi") == 0) {
		rc = DDI_IDENTIFIED;
	}

	return (rc);
}

static char *mapstr = "sbus map space";

/*ARGSUSED*/
static int
sbus_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	static int first_sbus = 1; /* current unit number for attach */

	struct sbus_private *pprivate;
	int parent_node_id;
	int node_id;
	int device_id;
	int sbus_index, i;
	volatile u_int *sbi_ctl;	/* SBI CTL register pointer */

	parent_node_id = ddi_get_nodeid(ddi_get_parent(devi));
	node_id = ddi_get_nodeid(devi);
	device_id = get_deviceid(node_id, parent_node_id);
	sbus_index = xdb_sbus_unit(device_id);

	if (first_sbus) {
		/*
		 * We also *know* when memory is the DVMA slave,
		 * it takes all burstsizes. And we *know*
		 * SBI supports all burstsizes as a controller.
		 * So sbus_burst_sizes (which describes burst
		 * capability of system memory as DVMA slave)
		 * is 64 byte burst which is the max possible
		 * on currrent Sbus.
		 */
		sbus_burst_sizes = SBUS_BURST_64B;
		sbus_max_burst_size = 1 << (ddi_fls(sbus_burst_sizes) - 1);
		sbus_min_burst_size = 1 << (ddi_ffs(sbus_burst_sizes) - 1);

		first_sbus = 0;
	}

	pprivate = &sbus_pri_data[sbus_index];

	/* save private data ptr */
	ddi_set_driver_private(devi, (caddr_t)pprivate);

	pprivate->sbi_id = sbus_index;
	mutex_init(&pprivate->dma_pool_lock, "sbus dma pool lock",
		MUTEX_DEFAULT, NULL);

	/*
	 * Map in SBI registers. Note, rnumber 1 is for sbi registers,
	 * rnumber 2 is for IOPTEs. Sbi reg has 1 page, but IOPTEs has
	 * 16 pages (64K). This may eat up too much kernelmap when
	 * mapping all MX_SBUS of them. But kernelmap is being enlargd now ...
	 * (it's current size is about 4.5M).
	 */

	if (ddi_map_regs(devi, 1, (caddr_t *)&pprivate->va_sbi, 0, 0)) {
		cmn_err(CE_CONT, "?sbus%d: unable to map SBI registers\n",
				sbus_index);
		return (-1);
	}

	/*
	 * Modify the SBI control register so that the SDTOL field
	 * contains the default SDTOL, 0xD. This gives a much longer
	 * timeout than that programmed in by the PROM (0x1).
	 */
	sbi_ctl = (u_int *)VA_SBI_CNTL(pprivate->va_sbi);
	*sbi_ctl = sbi_set_sdtol(*sbi_ctl, DFLT_SDTOL);

	/* map in XPT */
	if (ddi_map_regs(devi, 2, (caddr_t *)&pprivate->va_xpt, 0, 0)) {
		cmn_err(CE_CONT, "?sbus%d: unable to map XPT registers\n",
				sbus_index);
		return (DDI_FAILURE);
	}

	/* initialize the resource map */
	pprivate->map = (struct map *)
		kmem_zalloc(sizeof (struct map) * SBUSMAP_FRAG, KM_NOSLEEP);

	if (pprivate->map == NULL) {
		cmn_err(CE_PANIC, "sbus_attach: kmem_zalloc of map failed\n");
	}

	mapinit(pprivate->map, (long)SBUSMAP_SIZE,
		(u_long)SBUSMAP_BASE, mapstr, SBUSMAP_FRAG);

	pprivate->dma_reserve = sbus_dma_reserve;

	/* allocate the interrupt autovector array */
	pprivate->vec = (struct autovec *)
		kmem_zalloc(sizeof (struct autovec) *
		    MX_PRI * MX_SBUS_SLOTS * sbus_nvect, KM_NOSLEEP);

	if (pprivate->vec == NULL) {
		cmn_err(CE_PANIC, "sbus_attach: kmem_zalloc of vec failed\n");
	}

	/* let intr.c knows about our interrupt list ... */
	nexus_note_sbus(sbus_index, pprivate->vec);

	/* now clean up the hardware */
	iommu_init(pprivate->va_xpt);
	stream_dvma_init(pprivate->va_sbi);


	/*
	 * init. burst mode in each slot.
	 */
	for (i = 0; i < MX_SBUS_SLOTS; i++) {
		extern u_int slot_burst_patch[MX_SBUS][MX_SBUS_SLOTS];

		if (slot_burst_patch[sbus_index][i] != 0) {
			set_sbus_burst_size(pprivate->va_sbi, i,
			    slot_burst_patch[sbus_index][i]);
			pprivate->burst_size[i] =
			    slot_burst_patch[sbus_index][i];
		} else {
			set_sbus_burst_size(pprivate->va_sbi, i, NO_SBUS_BURST);
			pprivate->burst_size[i] = NO_SBUS_BURST;
		}
	}

	nsbus++;

	ddi_report_dev(devi);
	return (DDI_SUCCESS);
}


/*
 * DMA routines for sun4d platform
 */

#define	PTECSIZE	(64)	/* 256K DMA window size */

extern int impl_read_hwmap(struct as *, caddr_t, int, struct pte *, int);
extern u_long getdvmapages(int, u_long, u_long, u_int, u_int,
	int, struct map *);
extern void putdvmapages(u_long, int, struct map *);

/* #define	DMADEBUG */

#if defined(DMADEBUG) || defined(lint)
static int dmadebug = 0;

#define	DMAPRINTF	if (dmadebug) printf
#define	DMAPRINT(x)			DMAPRINTF(x)
#define	DMAPRINT1(x, a)			DMAPRINTF(x, a)
#define	DMAPRINT2(x, a, b)		DMAPRINTF(x, a, b)
#define	DMAPRINT3(x, a, b, c)		DMAPRINTF(x, a, b, c)
#define	DMAPRINT4(x, a, b, c, d)	DMAPRINTF(x, a, b, c, d)
#define	DMAPRINT5(x, a, b, c, d, e)	DMAPRINTF(x, a, b, c, d, e)
#define	DMAPRINT6(x, a, b, c, d, e, f)	DMAPRINTF(x, a, b, c, d, e, f)

#else

#define	DMAPRINTF
#define	DMAPRINT(x)
#define	DMAPRINT1(x, a)
#define	DMAPRINT2(x, a, b)
#define	DMAPRINT3(x, a, b, c)
#define	DMAPRINT4(x, a, b, c, d)
#define	DMAPRINT5(x, a, b, c, d, e)
#define	DMAPRINT6(x, a, b, c, d, e, f)

#endif	/* DMADEBUG */

/*
 * Macros to flush and invalidate streaming buffers
 */
#define	SBI_FWB		0x1
#define	FLUSH_SBUS_WRTBUF(va_sbi)					\
{									\
	volatile u_int *ptmp;						\
									\
	ptmp = (u_int *) va_sbi;					\
	*ptmp = SBI_FWB;						\
	while (*ptmp & SBI_FWB);					\
}

#define	SBI_IRB		0x2
#define	INVALIDATE_SBUS_RDBUF(va_sbi)					\
{									\
	volatile u_int *ptmp;						\
									\
	ptmp = (u_int *) va_sbi;					\
	*ptmp = SBI_IRB;						\
	while (*ptmp & SBI_IRB);					\
}

static int
sbus_dma_allochdl(dev_info_t *dip, dev_info_t *rdip, ddi_dma_attr_t *dma_attr,
	int (*waitfp)(caddr_t), caddr_t arg, ddi_dma_handle_t *handlep)
{
	u_long addrlow, addrhigh;
	ddi_dma_impl_t *mp;
	int slot_id;
	struct sbus_private *ppri =
		(struct sbus_private *)ddi_get_driver_private(dip);

	if (dma_attr->dma_attr_burstsizes == 0) {
		return (DDI_DMA_BADATTR);
	}
	addrlow = (u_long)dma_attr->dma_attr_addr_lo;
	addrhigh = (u_long)dma_attr->dma_attr_addr_hi;
	if ((addrhigh <= addrlow) || (addrhigh < (u_long)IOMMU_DVMA_BASE)) {
		return (DDI_DMA_BADATTR);
	}

	mutex_enter(&ppri->dma_pool_lock);
	mp = kmem_fast_zalloc(&ppri->dmaimplbase, sizeof (*mp), 2,
		(waitfp == DDI_DMA_SLEEP) ? KM_SLEEP : KM_NOSLEEP);
	mutex_exit(&ppri->dma_pool_lock);
	if (mp == NULL) {
		if (waitfp != DDI_DMA_DONTWAIT) {
			ddi_set_callback(waitfp, arg, &ppri->dvma_call_list_id);
		}
		return (DDI_DMA_NORESOURCES);
	}
	mp->dmai_rdip = rdip;
	mp->dmai_minxfer = (u_int)dma_attr->dma_attr_minxfer;
	mp->dmai_burstsizes = (u_int)dma_attr->dma_attr_burstsizes;
	mp->dmai_attr = *dma_attr;
	mp->dmai_nwin = 1;
	if ((slot_id = fd_sbus_slot_id(dip, rdip)) < 0) {
		cmn_err(CE_PANIC, "sbus_dma_allochdl: bad slot_id");
	}
	mp->dmai_sbi = get_slot_ctlreg(ppri->va_sbi, slot_id);
	*handlep = (ddi_dma_handle_t)mp;
	return (DDI_SUCCESS);
}

static int
sbus_dma_freehdl(dev_info_t *dip, dev_info_t *rdip,
	ddi_dma_handle_t handle)
{
	ddi_dma_impl_t *mp = (ddi_dma_impl_t *)handle;
	struct sbus_private *ppri =
		(struct sbus_private *)ddi_get_driver_private(dip);

#ifdef lint
	rdip = rdip;
#endif
	mutex_enter(&ppri->dma_pool_lock);
	kmem_fast_free(&ppri->dmaimplbase, (caddr_t)mp);
	mutex_exit(&ppri->dma_pool_lock);

	if (ppri->dvma_call_list_id != 0) {
		ddi_run_callback(&ppri->dvma_call_list_id);
	}
	return (DDI_SUCCESS);
}

static int
sbus_dma_bindhdl(dev_info_t *dip, dev_info_t *rdip,
	ddi_dma_handle_t handle, struct ddi_dma_req *dmareq,
	ddi_dma_cookie_t *cp, u_int *ccountp)
{
	ddi_dma_impl_t *mp = (ddi_dma_impl_t *)handle;
	ddi_dma_attr_t *dma_attr;
	u_long addrlow, addrhigh;
	int rval;

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

	rval = sbus_dma_setup(dip, rdip, dmareq, addrlow, addrhigh,
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

static u_int
recover_iom_flag(iommu_pte_t *piopte)
{
	iommu_pte_t tmp_pte;

	tmp_pte = *piopte;	/* read in pte. */

	return (tmp_pte.iopte & IOPTE_FLAG_MSK);
}

static int
sbus_dma_unbindhdl(dev_info_t *dip, dev_info_t *rdip,
	ddi_dma_handle_t handle)
{
	ddi_dma_impl_t *mp = (ddi_dma_impl_t *)handle;
	u_long addr;
	u_int npages;
	struct sbus_private *ppri =
		(struct sbus_private *)ddi_get_driver_private(dip);
	int red;

#ifdef lint
	rdip = rdip;
#endif
	addr = mp->dmai_mapping & ~MMU_PAGEOFFSET;
	npages = mp->dmai_ndvmapages;

	if (!(mp->dmai_rflags & DDI_DMA_CONSISTENT) &&
		(mp->dmai_rflags & DDI_DMA_READ)) {
		/* flush stream write buffers */
		FLUSH_SBUS_WRTBUF(mp->dmai_sbi);
	}

#ifdef DEBUG
	if (npages)
		iommu_unload(ppri->va_xpt, (caddr_t)addr, npages);
#endif DEBUG

	if (mp->dmai_minfo) {
		u_long addr;
		u_int naptes;

		addr = (u_long) mp->dmai_object.
			    dmao_obj.virt_obj.v_addr;
		naptes = mmu_btopr(mp->dmai_object.dmao_size +
			    (addr & MMU_PAGEOFFSET));
		kmem_free((caddr_t)mp->dmai_minfo,
		    naptes * sizeof (struct pte));
		mp->dmai_minfo = NULL;
	}

	red = ((mp->dmai_rflags & DDI_DMA_REDZONE)? 1 : 0);
	if (npages) {
		putdvmapages(addr, npages + red, ppri->map);
	}
	mp->dmai_ndvmapages = 0;
	mp->dmai_inuse = 0;

	if (ppri->dvma_call_list_id != 0) {
		ddi_run_callback(&ppri->dvma_call_list_id);
	}

	return (DDI_SUCCESS);
}

/*ARGSUSED*/
static int
sbus_dma_flush(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle, off_t off, u_int len,
    u_int cache_flags)
{
	ddi_dma_impl_t *mp = (ddi_dma_impl_t *)handle;

	if (!(mp->dmai_rflags & DDI_DMA_CONSISTENT)) {
		if (cache_flags == DDI_DMA_SYNC_FORDEV) {
			INVALIDATE_SBUS_RDBUF(mp->dmai_sbi);
		} else {
			FLUSH_SBUS_WRTBUF(mp->dmai_sbi);
		}
	}
	return (DDI_SUCCESS);
}

static int
sbus_dma_win(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle, uint_t win, off_t *offp,
    uint_t *lenp, ddi_dma_cookie_t *cookiep, uint_t *ccountp)
{
	ddi_dma_impl_t *mp = (ddi_dma_impl_t *)handle;
	u_long offset;
	u_long winsize, newoff;

#ifdef lint
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

	sbus_map_window(dip, rdip, mp, newoff, winsize);
	/*
	 * last window might be shorter.
	 */
	cookiep->dmac_size = mp->dmai_size;

	return (DDI_SUCCESS);
}

static void
sbus_map_window(dev_info_t *dip, dev_info_t *rdip, ddi_dma_impl_t *mp,
    u_long newoff, u_long winsize)
{
	struct pte *ptep;
	page_t *pp;
	u_long addr, flags;
	iommu_pte_t *piopte;
	u_int npages, iom_flag;
	struct sbus_private *ppri =
		(struct sbus_private *)ddi_get_driver_private(dip);

#ifdef lint
	rdip = rdip;
#endif
	addr = mp->dmai_mapping & ~MMU_PAGEOFFSET;
	npages = mp->dmai_ndvmapages;

	piopte = iommu_ptefind(ppri->va_xpt, (caddr_t)addr);
	ASSERT(piopte);

	iom_flag = recover_iom_flag(piopte);
	iom_flag |= IOPTE_VALID;

	if (!(mp->dmai_rflags & DDI_DMA_CONSISTENT) &&
		(mp->dmai_rflags & DDI_DMA_READ)) {
		/* flush stream buffers */
		FLUSH_SBUS_WRTBUF(mp->dmai_sbi);
	}

#ifdef DEBUG
	if (npages)
		iommu_unload(ppri->va_xpt, (caddr_t)addr, npages);
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
	npages = mmu_btopr(mp->dmai_size + (mp->dmai_mapping & MMU_PAGEOFFSET));

	if (pp) {
		sbus_map_pp(npages, pp, iom_flag, piopte, 0);
	} else {
		sbus_map_pte(npages, ptep, iom_flag, piopte, 0);
	}

	/*
	 * also invalidate read stream buffer
	 */
	if ((iom_flag & IOPTE_STREAM) && !(iom_flag & IOPTE_WRITE)) {
		INVALIDATE_SBUS_RDBUF(mp->dmai_sbi);
	}
}

static int
sbus_dma_map(dev_info_t *dip, dev_info_t *rdip,
    struct ddi_dma_req *dmareq, ddi_dma_handle_t *handlep)
{
	ddi_dma_lim_t *dma_lim = dmareq->dmar_limits;
	ddi_dma_impl_t *mp;
	u_long addrlow, addrhigh;
	int rval, slot_id;
	struct sbus_private *ppri =
		(struct sbus_private *)ddi_get_driver_private(dip);

	/*
	 * If not an advisory call, get a DMA handle
	 */
	if (handlep) {
		mutex_enter(&ppri->dma_pool_lock);
		mp = kmem_fast_alloc(&ppri->dmaimplbase, sizeof (*mp), 2,
		    (dmareq->dmar_fp == DDI_DMA_SLEEP) ? KM_SLEEP : KM_NOSLEEP);
		mutex_exit(&ppri->dma_pool_lock);
		if (mp == NULL) {
			if (dmareq->dmar_fp != DDI_DMA_DONTWAIT) {
				ddi_set_callback(dmareq->dmar_fp,
				    dmareq->dmar_arg, &ppri->dvma_call_list_id);
			}
			return (DDI_DMA_NORESOURCES);
		}
		mp->dmai_rdip = rdip;
		mp->dmai_rflags = dmareq->dmar_flags & DMP_DDIFLAGS;
		mp->dmai_minxfer = dma_lim->dlim_minxfer;
		mp->dmai_offset = 0;
		mp->dmai_ndvmapages = 0;
		mp->dmai_minfo = 0;
		if ((slot_id = fd_sbus_slot_id(dip, rdip)) < 0) {
			cmn_err(CE_PANIC, "sbus_dma_map: bad slot_id");
		}
		mp->dmai_sbi = get_slot_ctlreg(ppri->va_sbi, slot_id);
	} else {
		mp = NULL;
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
	rval = sbus_dma_setup(dip, rdip, dmareq, addrlow, addrhigh,
		dma_lim->dlim_cntr_max, dma_lim->dlim_burstsizes, mp);
bad:
	if (rval && (rval != DDI_DMA_PARTIAL_MAP)) {
		if (mp) {
			mutex_enter(&ppri->dma_pool_lock);
			kmem_fast_free(&ppri->dmaimplbase, (caddr_t)mp);
			mutex_exit(&ppri->dma_pool_lock);
		}
	} else {
		if (mp) {
			*handlep = (ddi_dma_handle_t)mp;
		}
	}
	return (rval);
}


static int
sbus_dma_setup(dev_info_t *dip, dev_info_t *rdip, struct ddi_dma_req *dmareq,
    u_long addrlow, u_long addrhigh, u_int segalign, u_int burstsizes,
    ddi_dma_impl_t *mp)
{
	extern struct as kas;
	auto struct pte ptes[PTECSIZE + 1];
	auto struct pte *allocpte;
	struct pte *ptep;
	page_t *pp;
	u_int smask;
	struct as *as;
	u_int dvma_pfn, size;
	u_long addr, ioaddr, offset;
	int red, npages, rval;
	int memtype = 0;
	int naptes = 0;
	iommu_pte_t *piopte;
	u_int iom_flag = IOPTE_VALID;
	struct sbus_private *ppri =
		(struct sbus_private *)ddi_get_driver_private(dip);
	int target_slot = -1;	/* slot id for the DVMA slave */
	int target_burst_sizes = 0;

#ifdef lint
	rdip = rdip;
#endif
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
		if (npages > PTECSIZE + 1) {
			allocpte = (struct pte *)
			    kmem_zalloc(npages * sizeof (struct pte), KM_SLEEP);
			if (mp)
				mp->dmai_minfo = (void *)allocpte;
			naptes = npages;
			ptep = allocpte;
		} else {
			ptep = ptes;
		}

		memtype = impl_read_hwmap(as, (caddr_t)addr, npages, ptep, 1);

		if (memtype < 0) {
			/*
			 * Memory type changes in the middle of the object.
			 */
			rval = DDI_DMA_NOMAPPING;
			goto bad;
		} else if (memtype > 0) {
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
			target_slot = fd_target_slot_id(ptep, ppri->sbi_id);
			if (target_slot < 0) {
				rval = DDI_DMA_NOMAPPING;
				goto bad;
			}

			target_burst_sizes = ppri->burst_size[target_slot];
			iom_flag |= IOPTE_INTRA;

		} else {
			target_burst_sizes = sbus_burst_sizes;
		}
		pp = NULL;
		break;

	case DMA_OTYP_PAGES:
		pp = dmareq->dmar_object.dmao_obj.pp_obj.pp_pp;
		offset = dmareq->dmar_object.dmao_obj.pp_obj.pp_offset;
		npages = mmu_btopr(dmareq->dmar_object.dmao_size + offset);
		ptep = ptes;

		target_burst_sizes = sbus_burst_sizes;
		break;

	case DMA_OTYP_PADDR:
	default:
		/*
		 * Not a supported type for this implementation
		 */
		rval = DDI_DMA_NOMAPPING;
		goto bad;
	}

	burstsizes &= target_burst_sizes;
	if (burstsizes == 0) {
		rval = DDI_DMA_NOMAPPING;
		goto bad;
	}

	if (mp == NULL) {
		if (naptes) {
			kmem_free(allocpte, naptes * sizeof (struct pte));
		}
		goto out;
	} else {
		mp->dmai_burstsizes = burstsizes;
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
		if (npages >= SBUSMAP_SIZE - 0x40) {
			rval = DDI_DMA_TOOBIG;
			goto bad;
		}
	}
	mp->dmai_size = size;
	mp->dmai_ndvmapages = npages;
	red = ((mp->dmai_rflags & DDI_DMA_REDZONE)? 1 : 0);

	ioaddr = getdvmapages(npages + red, addrlow, addrhigh,
			(u_int)-1, segalign,
			(dmareq->dmar_fp == DDI_DMA_SLEEP)? 1 : 0, ppri->map);
	if (ioaddr == 0) {
		if (dmareq->dmar_fp == DDI_DMA_SLEEP)
			rval = DDI_DMA_NOMAPPING;
		else
			rval = DDI_DMA_NORESOURCES;
		goto bad;
	}
	ASSERT((caddr_t)ioaddr >= (caddr_t)IOMMU_DVMA_BASE);
	dvma_pfn = iommu_btop((caddr_t)ioaddr - (caddr_t)IOMMU_DVMA_BASE);
	piopte = &ppri->va_xpt[dvma_pfn];
	ASSERT(piopte != NULL);

	if (mp->dmai_rflags & DDI_DMA_READ)
		iom_flag |= IOPTE_WRITE;

	if (!(iom_flag & IOPTE_INTRA)) {
		/* DVMA to memory */
		iom_flag |= IOPTE_CACHE;

		if (mp->dmai_rflags & DDI_DMA_CONSISTENT) {
			mp->dmai_rflags |= DMP_NOSYNC;
		} else {
			/* stream mode */
			iom_flag |= IOPTE_STREAM;
		}
	}

	mp->dmai_mapping = (u_long)(ioaddr + offset);
	ASSERT((mp->dmai_mapping & ~segalign) ==
	    ((mp->dmai_mapping + (mp->dmai_size - 1)) & ~segalign));

	if (pp) {
		sbus_map_pp(npages, pp, iom_flag, piopte, red);
	} else {
		sbus_map_pte(npages, ptep, iom_flag, piopte, red);
	}

	/*
	 * invalidate stream read buffer
	 */
	if ((iom_flag & IOPTE_STREAM) && !(iom_flag & IOPTE_WRITE)) {
		INVALIDATE_SBUS_RDBUF(mp->dmai_sbi);
	}

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
		    dmareq->dmar_arg, &ppri->dvma_call_list_id);
	}
	return (rval);
}

static void
sbus_map_pp(int npages, page_t *pp, u_int iom_flag,
    iommu_pte_t *piopte, int red)
{
	u_int pfn;

	while (npages > 0) {
		pfn = page_pptonum(pp);

		*(u_int *)piopte = (pfn << IOPTE_PFN_SHIFT) | iom_flag;

		npages--;
		piopte++;
		pp = pp->p_next;
	}

	if (red) {
		iommu_pteunload(piopte);
	}
}

static void
sbus_map_pte(int npages, struct pte *ptep, u_int iom_flag,
    iommu_pte_t *piopte, int red)
{
	u_int pfn;

	while (npages > 0) {
		pfn = MAKE_PFNUM(ptep);

		*(u_int *)piopte = (pfn << IOPTE_PFN_SHIFT) | iom_flag;

		npages--;
		piopte++;
		ptep++;
	}

	if (red) {
		iommu_pteunload(piopte);
	}
}

static int
sbus_dma_mctl(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle, enum ddi_dma_ctlops request,
    off_t *offp, u_int *lenp,
    caddr_t *objp, u_int cache_flags)
{
	u_long addr, offset;
	u_int npages;
	ddi_dma_cookie_t *cp;
	ddi_dma_impl_t *mp = (ddi_dma_impl_t *)handle;
	struct sbus_private *ppri =
		(struct sbus_private *)ddi_get_driver_private(dip);

	DMAPRINT1("dma_mctl: handle %x ", (int)mp);

	switch (request) {
	case DDI_DMA_FREE:
	{
		int red;

		addr = mp->dmai_mapping & ~MMU_PAGEOFFSET;
		npages = mp->dmai_ndvmapages;

		if (!(mp->dmai_rflags & DDI_DMA_CONSISTENT) &&
			(mp->dmai_rflags & DDI_DMA_READ)) {
			/* flush stream write buffers */
			FLUSH_SBUS_WRTBUF(mp->dmai_sbi);
		}

#ifdef DEBUG
		if (npages)
			iommu_unload(ppri->va_xpt, (caddr_t)addr, npages);
#endif DEBUG

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

		red = ((mp->dmai_rflags & DDI_DMA_REDZONE)? 1 : 0);
		if (npages) {
			putdvmapages(addr, npages + red, ppri->map);
		}

		/*
		 * put impl struct back on free list
		 */
		mutex_enter(&ppri->dma_pool_lock);
		kmem_fast_free(&ppri->dmaimplbase, (caddr_t)mp);
		mutex_exit(&ppri->dma_pool_lock);

		if (ppri->dvma_call_list_id != 0) {
			ddi_run_callback(&ppri->dvma_call_list_id);
		}
		break;
	}
	case DDI_DMA_SYNC:

		if (!(mp->dmai_rflags & DDI_DMA_CONSISTENT)) {
			if (cache_flags == DDI_DMA_SYNC_FORDEV) {
				INVALIDATE_SBUS_RDBUF(mp->dmai_sbi);
			} else {
				FLUSH_SBUS_WRTBUF(mp->dmai_sbi);
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

		if (mp->dmai_rflags & DMP_PHYSADDR) {
			return (DDI_FAILURE);
		}

		/*
		 * Unfortunately on an IOMMU machine, the dvma cookie
		 * is not valid on the SRMMU at all. So we simply
		 * returns failure here.
		 *
		 * XXX: maybe we want to do a map in instead? but this
		 *	would create a leak since there is no
		 *	corresponding map out call in DDI. So just
		 *	return a error status and let driver
		 *	do map in/map out.
		 */
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

			offset = mp->dmai_mapping & MMU_PAGEOFFSET;
			winsize = mmu_ptob(mp->dmai_ndvmapages -
				mmu_btopr(offset));
			newoff = mp->dmai_offset + winsize;
			if (newoff > mp->dmai_object.dmao_size -
				mp->dmai_minxfer) {
				return (DDI_DMA_DONE);
			}
			sbus_map_window(dip, rdip, mp, newoff, winsize);

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

		sbus_map_window(dip, rdip, mp, newoff, winsize);

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
		*lenp = (u_int) mmu_ptob(addr);
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
		int slot_id;

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
		mutex_enter(&ppri->dma_pool_lock);
		if (np > ppri->dma_reserve) {
			mutex_exit(&ppri->dma_pool_lock);
			return (DDI_DMA_NORESOURCES);
		}
		ppri->dma_reserve -= np;
		mp = (ddi_dma_impl_t *)kmem_fast_zalloc(&ppri->dmaimplbase,
			sizeof (*mp), 2, KM_SLEEP);
		mutex_exit(&ppri->dma_pool_lock);
		ASSERT(mp);
		mp->dmai_rdip = rdip;
		mp->dmai_minxfer = dma_lim->dlim_minxfer;
		mp->dmai_burstsizes = dma_lim->dlim_burstsizes;
		mp->dmai_minfo = (void *)ppri;
		mp->dmai_rflags = DMP_BYPASSNEXUS;
		if ((slot_id = fd_sbus_slot_id(dip, rdip)) < 0)
			cmn_err(CE_PANIC, "sbus_dma_mctl: bad slot_id");
		mp->dmai_sbi = get_slot_ctlreg(ppri->va_sbi, slot_id);

		ioaddr = getdvmapages(np, addrlow, addrhigh, (u_int)-1,
			    dma_lim->dlim_cntr_max, 1, ppri->map);
		if (ioaddr == 0) {
			mutex_enter(&ppri->dma_pool_lock);
			ppri->dma_reserve += np;
			kmem_fast_free(&ppri->dmaimplbase, (caddr_t)mp);
			mutex_exit(&ppri->dma_pool_lock);
			return (DDI_DMA_NOMAPPING);
		}
		dvma_pfn = iommu_btop(ioaddr - IOMMU_DVMA_BASE);
		mp->dmai_ndvmapages = np;
		mp->dmai_mapping = (u_long)dvma_pfn;
		handlep = (ddi_dma_handle_t *)objp;
		*handlep = (ddi_dma_handle_t)mp;
		break;
	}
	case DDI_DMA_RELEASE:
	{
		u_long ioaddr, dvma_pfn;

		dvma_pfn = mp->dmai_mapping;
		ioaddr = iommu_ptob(dvma_pfn) + IOMMU_DVMA_BASE;
		putdvmapages(ioaddr, mp->dmai_ndvmapages, ppri->map);

		mutex_enter(&ppri->dma_pool_lock);
		kmem_fast_free(&ppri->dmaimplbase, (caddr_t)mp);
		ppri->dma_reserve += mp->dmai_ndvmapages;
		mutex_exit(&ppri->dma_pool_lock);

		if (ppri->dvma_call_list_id != 0) {
			ddi_run_callback(&ppri->dvma_call_list_id);
		}
		break;
	}

	default:
		DMAPRINT1("unknown 0%x\n", request);
		return (DDI_FAILURE);
	}
	return (DDI_SUCCESS);
}

/* #define SBUS_DEBUG */

#ifdef  SBUS_DEBUG
int sbus_debug_flag;
#define	sbus_debug	if (sbus_debug_flag) printf
#endif  SBUS_DEBUG

static int
sbus_ctlops(dev_info_t *dip, dev_info_t *rdip,
    ddi_ctl_enum_t op, void *arg, void *result)
{
	/*
	 * XXX  Why isn't this a big 'case' statement?
	 */

	if (op == DDI_CTLOPS_INITCHILD) {
		struct sbus_private *ppri = (struct sbus_private *)
			ddi_get_driver_private(dip);
		int slot_id, burst_sizes, retval;

		extern int impl_ddi_sbus_initchild(dev_info_t *);
		extern u_int slot_burst_patch[MX_SBUS][MX_SBUS_SLOTS];

		if (strcmp(ddi_get_name((dev_info_t *)arg), "sbusmem") == 0) {
			register dev_info_t *cdip = (dev_info_t *)arg;
			register int i, n;
			int slot, size;
			char ident[10];

			/* should we set DDI_PROP_NOTPROM? */

			slot = ddi_getprop(DDI_DEV_T_NONE, cdip,
			    DDI_PROP_DONTPASS | DDI_PROP_CANSLEEP, "slot", -1);
			if (slot == -1) {
#ifdef  SBUS_DEBUG
				sbus_debug("can't get slot property\n");
#endif  SBUS_DEBUG
				return (DDI_FAILURE);
			}

#ifdef  SBUS_DEBUG
			sbus_debug("number of range properties (slots) %d\n",
			    sparc_pd_getnrng(dip));
#endif  SBUS_DEBUG

			for (i = 0, n = sparc_pd_getnrng(dip); i < n; i++) {
				struct rangespec *rp = sparc_pd_getrng(dip, i);

				if (rp->rng_cbustype == (u_int) slot) {
					struct regspec r;

					/* create reg property */

					r.regspec_bustype = (u_int) slot;
					r.regspec_addr = 0;
					r.regspec_size = rp->rng_size;
					(void) ddi_prop_create(DDI_DEV_T_NONE,
					    cdip, DDI_PROP_CANSLEEP, "reg",
					    (caddr_t)&r,
					    sizeof (struct regspec));

#ifdef	SBUS_DEBUG
		sbus_debug("range property: cbustype %x\n", rp->rng_cbustype);
		sbus_debug("		    coffset  %x\n", rp->rng_coffset);
		sbus_debug("		    bustype  %x\n", rp->rng_bustype);
		sbus_debug("		    offset   %x\n", rp->rng_offset);
		sbus_debug("		    size     %x\n\n", rp->rng_size);
		sbus_debug("reg property:   bustype  %x\n", r.regspec_bustype);
		sbus_debug("		    addr     %x\n", r.regspec_addr);
		sbus_debug("                size     %x\n", r.regspec_size);
#endif	SBUS_DEBUG
					/* create size property for slot */

					size = rp->rng_size;
					(void) ddi_prop_create(DDI_DEV_T_NONE,
					    cdip, DDI_PROP_CANSLEEP, "size",
					    (caddr_t)&size, sizeof (int));

					sprintf(ident, "slot%x", slot);
					(void) ddi_prop_create(DDI_DEV_T_NONE,
					    cdip, DDI_PROP_CANSLEEP, "ident",
					    ident, sizeof (ident));

					return (impl_ddi_sbus_initchild(arg));
				}
			}
			return (DDI_FAILURE);
		}

		retval = impl_ddi_sbus_initchild((dev_info_t *)arg);
		if (retval != DDI_SUCCESS)
			/* node not properly initialized- NOT_WELL_FORMED. */
			return (retval);

		if ((slot_id = fd_sbus_slot_id(dip, (dev_info_t *)arg)) < 0) {
			panic("SBUS_INITCHILD: bad sbus_slot");
		}

		/*
		 * Disable this slot for auto slot config.
		 */
		if (slot_burst_patch[ppri-> sbi_id][slot_id] != 0) {
			/*
			 * Someone patched this slot to be not autoconfigable.
			 */
			return (retval);
		}

		burst_sizes = ddi_getprop(DDI_DEV_T_ANY, (dev_info_t *)
			arg, DDI_PROP_DONTPASS, "burst-sizes", -1);

		/* if there is no burst-size property, we return */
		if (burst_sizes <= NO_SBUS_BURST)
			return (retval);

		/*
		 * We update burst size tracking on this slot
		 * either this is the first burst size property
		 * or when burst size changes for that slot.
		 */
		if (ppri->burst_size[slot_id] == NO_SBUS_BURST) {
			ppri->burst_size[slot_id] = burst_sizes;
			set_sbus_burst_size(ppri->va_sbi, slot_id,
				burst_sizes);
		} else {
			u_int new;

			new = burst_sizes & ppri->burst_size[slot_id];
			if (ppri->burst_size[slot_id] != new) {
				ppri->burst_size[slot_id] = new;
				set_sbus_burst_size(ppri->va_sbi, slot_id,
					new);
			}
		}

		return (retval);
	}

	if (op == DDI_CTLOPS_UNINITCHILD) {
		extern void impl_ddi_sunbus_removechild(dev_info_t *);

		impl_ddi_sunbus_removechild(arg);
		return (DDI_SUCCESS);
	}

	if (op == DDI_CTLOPS_IOMIN) {
		register int val = *((int *)result);

		/*
		 * The 'arg' value of nonzero indicates 'streaming' mode.
		 * If in streaming mode, pick the largest of our burstsizes
		 * available and say that that is our minimum value (modulo
		 * what mincycle is).
		 */
		if ((int)arg)
			val = maxbit(val, sbus_max_burst_size);
		else
			val = maxbit(val, sbus_min_burst_size);

		*((int *)result) = val;
		return (ddi_ctlops(dip, rdip, op, arg, result));
	}

	/*
	 * XX: These specific uglinesses are needed right now because
	 * XX: we do not store our child addresses as 'sbus' addresses
	 * XX: as yet. This will also have to change for OBPV2. This
	 * XX: could also be much more robust in that it should check
	 * XX: to make sure that the address really is an SBus address.
	 */

	if (op == DDI_CTLOPS_REPORTDEV) {

		register dev_info_t *pdev;
		register int i, n;

		int sbusid = DEVI(dip)->devi_instance;

		if (DEVI_PD(rdip) == NULL)
			return (DDI_FAILURE);

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
			cmn_err(CE_CONT, "? SBus%d slot %x 0x%x", sbusid,
			    rp->regspec_bustype, rp->regspec_addr);
		}

		for (i = 0, n = sparc_pd_getnintr(rdip); i < n; i++) {

			register int pri, sbuslevel;

			if (i == 0)
				cmn_err(CE_CONT, "? ");
			else
				cmn_err(CE_CONT, "?, ");

			pri = INT_IPL(sparc_pd_getintr(rdip, i)->intrspec_pri);

			if ((sbuslevel = sparc_to_sbus(pri)) != -1)
				cmn_err(CE_CONT, "?SBus level %d ", sbuslevel);

			cmn_err(CE_CONT, "?sparc ipl %d", pri);
		}

		cmn_err(CE_CONT, "?\n");
		return (DDI_SUCCESS);
	}

	if (op == DDI_CTLOPS_DVMAPAGESIZE) {
		*(u_long *)result = IOMMU_PAGE_SIZE;
		return (DDI_SUCCESS);
	}

	if (op == DDI_CTLOPS_XLATE_INTRS) {
		static int sbus_ctl_xlate_intrs(dev_info_t *, dev_info_t *,
			int *, struct ddi_parent_private_data *);

		return (sbus_ctl_xlate_intrs(dip, rdip, arg, result));
	}

	if (op == DDI_CTLOPS_SLAVEONLY) {
		/* sun4d does not have slave only slots */
		return (DDI_FAILURE);
	} else if (op == DDI_CTLOPS_AFFINITY) {
		dev_info_t *dipb = (dev_info_t *)arg;
		int r_slot, b_slot;

		if ((b_slot = fd_sbus_slot_id(dip, dipb)) < 0)
			return (DDI_FAILURE);

		if ((r_slot = fd_sbus_slot_id(dip, rdip)) < 0)
			return (DDI_FAILURE);

		return ((b_slot == r_slot)? DDI_SUCCESS : DDI_FAILURE);

	} else if (op == DDI_CTLOPS_DMAPMAPC) {

#ifdef DEV_TO_DEV_DMA
		/*
		 * we sort of optimize of our DEV to DEV DVMA setup
		 * such that DMAPMAPC would never be called.
		 */
		struct dma_phys_mapc *pd = (struct dma_phys_mapc *)arg;

		/*
		 * here we verify that the target slot is on the same Sbus
		 * that master is on.
		 */
		if (fd_target_sbus_id(pd) == ddi_get_unit(dip))
			/* DDI_DMA_PARTIAL tells dma_setup to setup mappins. */
			return (DDI_DMA_PARTIAL);
		else
			return (DDI_FAILURE);
#endif DEV_TO_DEV_DMA
		cmn_err(CE_CONT, "?DDI_DMAPMAPC called!!\n");
		return (DDI_FAILURE);

	} else {
		return (ddi_ctlops(dip, rdip, op, arg, result));
	}
}

static
int
fd_target_slot_id(struct pte *ptep, int cur_sbus_id)
{
	int pfn, slot_id, sbus_id;

	pfn = MAKE_PFNUM(ptep);

	/* Sbus (or board) id is at PA<30:33> */
	sbus_id = (pfn >> 18) & 0xf;
	ASSERT((sbus_id >= 0) && (sbus_id < MX_SBUS));

	/*
	 * Verify that the slave is on the same Sbus
	 * that master is on.
	 */
	if (sbus_id != cur_sbus_id)
		return (-1);

	/* slot id is at PA<28:29> */
	slot_id = (pfn >> 16) & 0x3;

	return (slot_id);
}

#ifdef DEV_TO_DEV_DMA
static int
fd_target_sbus_id(struct dma_phys_mapc *pd)
{
	int pfn, sbus_id;

	pfn = MAKE_PFNUM(pd->ptes);

	/* Sbus id is at PA<30:33> */
	sbus_id = (pfn >> 18) & 0xf;

	ASSERT((sbus_id >= 0) && (sbus_id < MX_SBUS));

	return (sbus_id);
}
#endif DEV_TO_DEV_DMA

static int
fd_sbus_slot_id(dev_info_t *dip, dev_info_t *rdip)
{
	dev_info_t *ancestor = rdip;
	dev_info_t *tmp;

	/*
	 * look for the node that's a direct child of Sbus node.
	 */
	while (ancestor && (tmp = ddi_get_parent(ancestor)) != dip) {
		ancestor = tmp;
	}

	if (tmp != dip)
		/* this node is not on the Sbus root'ed by dip */
		return (-1);

	if ((DEVI_PD(ancestor))->par_nreg > 0) {
		/*
		 * resspec_bustype is the slot #.
		 */
		struct ddi_parent_private_data *ppd = DEVI_PD(ancestor);
		struct regspec *reg = ppd->par_reg;

		if (reg->regspec_bustype >= 0 &&
				reg->regspec_bustype < MX_SBUS_SLOTS)
			return ((int)reg->regspec_bustype);
		else {
			return (-1);
		}
	} else {
		return (-1);
	}
}

static struct autovec *
sbus_find_vector(dev_info_t *dip, dev_info_t *rdip, int pri)
{
	struct autovec *base = get_autovec_base(dip);
	struct autovec *av;
	int sbus_slot;

	if ((sbus_slot = fd_sbus_slot_id(dip, rdip)) < 0)
		panic("sbus_find_vector: bad sbus_slot");

	ASSERT(pri < MX_PRI);

	/* ptr arithmetic! */
	av = base + pri * MX_SBUS_SLOTS * sbus_nvect + sbus_slot * sbus_nvect;

	return (av);
}

/*ARGSUSED*/
static kmutex_t *
select_mutex(dev_info_t *rdip, char *name, u_int pri)
{
	struct dev_ops *dops = DEVI(rdip)->devi_ops;
	kmutex_t *mutex_p = NULL;
	int hot;

	if (dops->devo_bus_ops) {
		hot = 1;	/* Nexus drivers MUST be MT-safe */
	} else if (dops->devo_cb_ops->cb_flag & D_MP) {
		hot = 1;	/* Most leaves are MT-safe */
	} else {
		hot = 0;	/* MT-unsafe drivers ok (for now) */
	}

	if (!hot) {
		extern kmutex_t unsafe_driver;
		mutex_p = &unsafe_driver;
	}

#if	0
	cmn_err(CE_CONT, "?\thard intr (%s%d) for %s\n",
		(hot ? "HOT-" : ""), pri, name);
#endif	/* DEBUG */

	return (mutex_p);
}

static int
sbus_add_hard(dev_info_t *dip, dev_info_t *rdip, ddi_intrspec_t intrspec,
    ddi_iblock_cookie_t *iblock_cookiep, ddi_idevice_cookie_t *idevice_cookiep,
    u_int (*int_handler)(caddr_t int_handler_arg), caddr_t int_handler_arg)
{
	char *name = ddi_get_name(rdip);

	struct intrspec *ispec = (struct intrspec *)intrspec;
	u_int pri = sparc_to_sbus(INT_IPL(ispec->intrspec_pri));
	struct autovec *av = sbus_find_vector(dip, rdip, pri);
	kmutex_t *mutex_p = select_mutex(rdip, name, pri);

	if (av == NULL) {
		cmn_err(CE_CONT, "?sbus_add_hard: no new av slots!\n");
		return (DDI_FAILURE);
	}

	if (av->av_vector != NULL) {
		/*
		 * Support for more than 1 intr handler
		 * at the same level, same slot.
		 */
		int inst = ddi_get_instance(rdip);
		register int i;

#ifdef	DEBUG
		cmn_err(CE_CONT, "?sbus_add_hard: multiple vectored interrupt "
		    "name=%s%d, pri=%d, av=0x%x\n",
		    name, inst, pri, av);
#endif	/* DEBUG */

		/*
		 * look for a vacant slot; the new handler gets installed after
		 * the previously installed handlers of this level and slot;
		 * check for duplicates along the way.
		 */
		for (i = 0; i < sbus_nvect; i++, av++) {
			if ((av->av_vector == int_handler) &&
			    (av->av_intarg == int_handler_arg) &&
			    (av->av_mutex == mutex_p)) {
				cmn_err(CE_WARN, "sbus_add_hard: %s%d: "
				    "duplicate level 0x%x interrupt handler",
				    name, inst, pri);
				break;
			}
			if (av->av_vector == NULL)  /* unused slot found */
				break;
		}

		/*
		 * check if we found an unused autovector slot;
		 * the tuneable sbus_nvect determines the number of
		 * autovector slots we have available.
		 */
		if (i >= sbus_nvect) {
			cmn_err(CE_WARN, "sbus_add_hard: %s%d: autovectored "
			    "interrupt at level 0x%x exceeded tuneable "
			    "sbus_nvect", name, inst, pri);
			return (DDI_FAILURE);
		}
	}

	av->av_devi = dip;
	av->av_mutex = mutex_p;
	av->av_intarg = int_handler_arg;
	av->av_vector = int_handler;

	ispec->intrspec_func = int_handler;
	ispec->intrspec_pri = INT_IPL(ispec->intrspec_pri);

	if (iblock_cookiep) {
		*iblock_cookiep = (ddi_iblock_cookie_t)
			ipltospl(INT_IPL(ispec->intrspec_pri));
	}

	if (idevice_cookiep) {
		idevice_cookiep->idev_vector = 0;
		idevice_cookiep->idev_priority = pri;
	}

	return (DDI_SUCCESS);
}

/*ARGSUSED*/
static void
sbus_rem_hard(dev_info_t *dip, dev_info_t *rdip, ddi_intrspec_t intrspec,
	ddi_iblock_cookie_t iblock_cookiep)
{
	struct intrspec *ispec = (struct intrspec *)intrspec;
	u_int pri = sparc_to_sbus(INT_IPL(ispec->intrspec_pri));
	volatile struct autovec *av = sbus_find_vector(dip, rdip, pri);
	volatile struct autovec *beginp = av;
	volatile struct autovec *endp;
	register int i;
	extern void wait_till_seen(int ipl);
	static u_int sbus_nullintr(caddr_t);

	if (ispec->intrspec_func == (u_int (*)()) 0) {
		return;
	}

	/*
	 * In support of multiple interrupts per level, same slot,
	 * we need to search the given autovector array for the interrupt
	 * to remove.  The tuneable sbus_nvect variable determines
	 * the maximum number of autovector elements supported with
	 * this instantiation of the system.
	 */
	for (i = 0; i < sbus_nvect; i++, av++) {
		if (((av->av_vector == ispec->intrspec_func) &&
		    (av->av_devi == dip)) || (av->av_vector == NULL))
			break;
	}

	if (av->av_vector == NULL || i >= sbus_nvect) {
		cmn_err(CE_CONT, "?sbus_rem_hard: no av found!\n");
		return;
	}

	/*
	 * now that we found an autovector to remove,
	 * find another one at the end of the array to move back up
	 * into its spot.  Find the end/last installed autovector.
	 */
	endp = beginp + (sbus_nvect - 1);	/* work back from the end */
	while ((endp > beginp) && (endp->av_vector == NULL))
		endp--;

	/*
	 * if the autovector to be ousted is at the end,
	 * shorten the list, else move an autovector from the end
	 * to the vacated slot.
	 */
	if (av == endp) {
		av->av_vector = NULL;
		wait_till_seen(INT_IPL(ispec->intrspec_pri));
	} else {
		/*
		 * The order of these writes is important!
		 */
		av->av_vector = sbus_nullintr;
		/*
		 * protect against calling the handler being removed
		 * with the arg meant for the handler at the end of
		 * the list
		 */
		wait_till_seen(INT_IPL(ispec->intrspec_pri));
		av->av_devi = endp->av_devi;
		av->av_intarg = endp->av_intarg;
		av->av_mutex = endp->av_mutex;
		av->av_vector = endp->av_vector;
		/*
		 * protect against removing the handler next on
		 * the list after some cpu has already passed (missed)
		 * the new copy of it in the list
		 * (prevents spurious interrupt message)
		 */
		wait_till_seen(INT_IPL(ispec->intrspec_pri));
		endp->av_vector = NULL;
		/*
		 * protect against passing the arg meant for a
		 * new handler being added to the list (by
		 * sbus_add_hard) to the handler we've just removed
		 * from the end of the list
		 */
		wait_till_seen(INT_IPL(ispec->intrspec_pri));
	}

	ispec->intrspec_func = (u_int (*)()) 0;
}


/*
 * add_intrspec - Add an interrupt specification.
 */
static int
sbus_add_intrspec(
	dev_info_t *dip,
	dev_info_t *rdip,
	ddi_intrspec_t intrspec,
	ddi_iblock_cookie_t *iblock_cookiep,
	ddi_idevice_cookie_t *idevice_cookiep,
	u_int (*int_handler)(caddr_t int_handler_arg),
	caddr_t int_handler_arg,
	int kind)
{
	register int r;

	ASSERT(intrspec != 0);
	ASSERT(rdip != 0);
	ASSERT(ddi_get_driver(rdip) != 0);

	switch (kind) {
	case IDDI_INTR_TYPE_NORMAL:
		mutex_enter(&av_lock);
		r = sbus_add_hard(dip, rdip, intrspec,
			iblock_cookiep, idevice_cookiep,
			int_handler, int_handler_arg);
		mutex_exit(&av_lock);
		break;

	default:
		/*
		 * I can't do it, pass the buck to my parent
		 */
		r = i_ddi_add_intrspec(dip, rdip, intrspec,
			iblock_cookiep, idevice_cookiep,
			int_handler, int_handler_arg, kind);

		if (r == DDI_SUCCESS && idevice_cookiep) {
			idevice_cookiep->idev_priority =
			    sparc_to_sbus(idevice_cookiep->idev_priority);
		}
		break;
	}

	return (r);
}

/*
 * remove_intrspec - Remove an interrupt specification.
 */
static void
sbus_remove_intrspec(
	dev_info_t *dip,
	dev_info_t *rdip,
	ddi_intrspec_t intrspec,
	ddi_iblock_cookie_t iblock_cookiep)
{
	register int unsafe;

	ASSERT(intrspec != 0);
	ASSERT(rdip != 0);

	unsafe = UNSAFE_DRIVER_LOCK_HELD();

	if (unsafe)
		mutex_exit(&unsafe_driver);
	mutex_enter(&av_lock);
	sbus_rem_hard(dip, rdip, intrspec, iblock_cookiep);
	mutex_exit(&av_lock);
	if (unsafe)
		mutex_enter(&unsafe_driver);
}

/*
 * We're prepared to claim that the interrupt string is in
 * the form of a list of <SBusintr> specifications. Translate it.
 */
static int
sbus_ctl_xlate_intrs(dev_info_t *dip, dev_info_t *rdip, int *in,
	struct ddi_parent_private_data *pdptr)
{
	register int n;
	register size_t size;
	register struct intrspec *new;

	static char bad_sbusintr_fmt[] =
	    "sbus%d: bad interrupt spec for %s%d - SBus level %d\n";

	/*
	 * The list consists of <SBuspri> elements
	 */
	if ((n = *in++) < 1)
		return (DDI_FAILURE);

	pdptr->par_nintr = n;
	size = n * sizeof (struct intrspec);
	new = pdptr->par_intr = kmem_zalloc(size, KM_SLEEP);

	while (n--) {
		register int level = *in++;

		if (level < 1 || level > 7) {
			cmn_err(CE_CONT, bad_sbusintr_fmt,
			    DEVI(dip)->devi_instance, DEVI(rdip)->devi_name,
			    DEVI(rdip)->devi_instance, level);
			goto broken;
			/*NOTREACHED*/
		}

		new->intrspec_pri = sbus_to_sparc(level) | INTLEVEL_SBUS;
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

/*
 * Here's a slight ugliness.  Because the sun4m architecture decided to
 * change the mapping of SBus levels to sparc ipl's, we can't both hide
 * all the details of interrupt mappings here -and- maintain implementation
 * architecture dependence, sigh.  So, we reference a table that's defined
 * in autoconf.c of the relevant kernel architecture. Either this
 * remapping should never have happened, or the sun4m SBus shouldn't have
 * had the name 'sbus'.  But -hey- we all have to live with our past
 * misdemeanours .. and this doesn't seem too horrendous.
 */

static int
sbus_to_sparc(int sbuslevel)
{
	static const char sbus_to_sparc_tbl[] = {
		-1, 2, 3, 5, 7, 9, 11, 13
	};

	if (sbuslevel < 1 || sbuslevel > 7)
		return (-1);
	else
		return ((int)sbus_to_sparc_tbl[sbuslevel]);
}

static int
sparc_to_sbus(int pri)
{
	static const char sparc_to_sbus_tbl[] = {
		-1, -1, 1, 2, -1, 3, -1, 4,
		-1, 5, -1, 6, -1, 7, -1, -1
	};

	if (pri < 1 || pri > 15)
		return (-1);
	else
		return ((int)sparc_to_sbus_tbl[pri]);
}

/*ARGSUSED*/
static u_int
sbus_nullintr(caddr_t intrg)
{
	return (DDI_INTR_UNCLAIMED);
}
