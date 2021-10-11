/*
 * Copyright (c) 1990-1992, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)iommu.c 1.42	95/10/13 SMI"

#include <sys/types.h>
#include <sys/param.h>
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/ddi_impldefs.h>
#include <sys/cmn_err.h>
#include <sys/kmem.h>

#include <sys/ddidmareq.h>
#include <sys/sysiosbus.h>
#include <sys/iommu.h>
#include <sys/iocache.h>
#include <sys/dvma.h>

#include <vm/as.h>
#include <vm/hat_sfmmu.h>
#include <vm/page.h>
#include <sys/machparam.h>

/* Useful debugging Stuff */
#include <sys/nexusdebug.h>
#include <sys/debug.h>
/* Bitfield debugging definitions for this file */
#define	IOMMU_GETDVMAPAGES_DEBUG	0x1
#define	IOMMU_DMAMAP_DEBUG		0x2
#define	IOMMU_DMAMCTL_DEBUG		0x4
#define	IOMMU_DMAMCTL_SYNC_DEBUG	0x8
#define	IOMMU_DMAMCTL_HTOC_DEBUG	0x10
#define	IOMMU_DMAMCTL_KVADDR_DEBUG	0x20
#define	IOMMU_DMAMCTL_NEXTWIN_DEBUG	0x40
#define	IOMMU_DMAMCTL_NEXTSEG_DEBUG	0x80
#define	IOMMU_DMAMCTL_MOVWIN_DEBUG	0x100
#define	IOMMU_DMAMCTL_REPWIN_DEBUG	0x200
#define	IOMMU_DMAMCTL_GETERR_DEBUG	0x400
#define	IOMMU_DMAMCTL_COFF_DEBUG	0x800
#define	IOMMU_DMAMCTL_DMA_FREE_DEBUG	0x1000
#define	IOMMU_REGISTERS_DEBUG		0x2000
#define	IOMMU_DMA_SETUP_DEBUG		0x4000
#define	IOMMU_DMA_UNBINDHDL_DEBUG	0x8000
#define	IOMMU_DMA_BINDHDL_DEBUG		0x10000
#define	IOMMU_DMA_WIN_DEBUG		0x20000
#define	IOMMU_DMA_ALLOCHDL_DEBUG	0x40000
#define	IOMMU_DMA_LIM_SETUP_DEBUG	0x80000
#define	IOMMU_BYPASS_RESERVE		0x100000
#define	IOMMU_BYPASS_LOAD		0x200000
#define	IOMMU_INTER_INTRA_XFER		0x400000
#define	IOMMU_TTE			0x800000


static struct dvma_ops iommu_dvma_ops = {
	DVMAO_REV,
	iommu_dvma_kaddr_load,
	iommu_dvma_unload,
	iommu_dvma_sync
};

extern void *sbusp;		/* sbus soft state hook */
extern u_ll_t iommu_tsb_physaddr[];
static char *mapstr = "sbus map space";

static int iommu_map_window(dev_info_t *, ddi_dma_impl_t *,
    u_long, u_long);

int
iommu_init(struct sbus_soft_state *softsp, int address)
{
#ifdef	DEBUG
	debug_info = 1;
	debug_print_level = 0;
#endif

	/*
	 * Simply add each registers offset to the base address
	 * to calculate the already mapped virtual address of
	 * the device register...
	 *
	 * define a macro for the pointer arithmetic; all registers
	 * are 64 bits wide and are defined as u_ll_t's.
	 */

#define	REG_ADDR(b, o)	(u_ll_t *)(unsigned)((unsigned)(b) + (unsigned)(o))

	softsp->iommu_ctrl_reg = REG_ADDR(address, OFF_IOMMU_CTRL_REG);
	softsp->tsb_base_addr = REG_ADDR(address, OFF_TSB_BASE_ADDR);
	softsp->iommu_flush_reg = REG_ADDR(address, OFF_IOMMU_FLUSH_REG);

#undef	REG_ADDR

	DPRINTF(IOMMU_REGISTERS_DEBUG, ("IOMMU Control reg: 0x%x, IOMMU TSB "
	    "base reg: 0x%x, IOMMU flush reg: 0x%x", softsp->iommu_ctrl_reg,
	    softsp->tsb_base_addr, softsp->iommu_flush_reg));

	mutex_init(&softsp->dma_pool_lock, "sbus dma pool lock",
		MUTEX_DEFAULT, NULL);

	mutex_init(&softsp->intr_poll_list_lock, "sbus intr_poll list lock",
		MUTEX_DEFAULT, NULL);

	/* initialize the DVMA resource map */
	softsp->dvmamap = (struct map *)
		kmem_zalloc(sizeof (struct map) * SBUSMAP_FRAG, KM_NOSLEEP);

	if (softsp->dvmamap == NULL) {
		cmn_err(CE_WARN, "sbus_attach: kmem_zalloc failed\n");
		return (DDI_FAILURE);
	}

	mapinit(softsp->dvmamap, (long) SBUSMAP_SIZE,
		(u_long) SBUSMAP_BASE, mapstr, SBUSMAP_FRAG);

	softsp->dma_reserve = SBUSMAP_MAXRESERVE;

#if	defined(DEBUG) && defined(IO_MEMUSAGE)
	mutex_init(&softsp->iomemlock, "DMA iomem lock", MUTEX_DEFAULT, NULL);
	softsp->iomem = (struct io_mem_list *) 0;
#endif /* DEBUG && IO_MEMUSAGE */
	/*
	 * Get the base address of the TSB table and store it in the hardware
	 */
	if (!iommu_tsb_physaddr[softsp->upa_id]) {
		cmn_err(CE_WARN, "Unable to retrieve IOMMU array.");
		return (DDI_FAILURE);
	} else {
		*softsp->tsb_base_addr = iommu_tsb_physaddr[softsp->upa_id];
	}

	/*
	 * We plan on the PROM flushing all TLB entries.  If this is not the
	 * case, this is where we should flush the hardware TLB.
	 */

	/* OK, lets flip the "on" switch of the IOMMU */
	*softsp->iommu_ctrl_reg |= (TSB_SIZE << TSB_SIZE_SHIFT | IOMMU_ENABLE);

	/* Save a convenient copy of TSB base, and flush write buffers */
	softsp->soft_tsb_base_addr = (u_ll_t *) *softsp->tsb_base_addr;

	return (DDI_SUCCESS);
}

/*
 * Initialize iommu hardware registers when the system is being resumed.
 * (Subset of iommu_init())
 */
int
iommu_resume_init(struct sbus_soft_state *softsp)
{
	/*
	 * Reset the base address of the TSB table in the hardware
	 */
	*softsp->tsb_base_addr = (u_ll_t) softsp->soft_tsb_base_addr;

	/* Turn "on" IOMMU */
	*softsp->iommu_ctrl_reg |= (TSB_SIZE << TSB_SIZE_SHIFT | IOMMU_ENABLE);

	return (DDI_SUCCESS);
}

#if defined(DEBUG) || defined(lint)
static int
chk_map_addr(u_int addr, char * str)
{
	if ((addr != 0) && (addr < IOMMU_DVMA_DVFN)) {
		cmn_err(CE_CONT, "*** chk_map_addr(%s) failed: addr=0x%x\n",
		    str, addr);
		return (1);
	}
	return (0);
}
#else
#define	chk_map_addr(addr, str) (0)
#endif	/* DEBUG || lint */


#define	ALIGN_REQUIRED(align)	(align != (u_int) -1)
#define	COUNTER_RESTRICTION(cntr)	(cntr != (u_int) -1)
#define	SEG_ALIGN(addr, seg)		(iommu_btop(((((addr) + (u_long) 1) +  \
					    (seg)) & ~(seg))))
#define	WITHIN_DVMAMAP(page)	\
	((page >= SBUSMAP_BASE) && (page - SBUSMAP_BASE < SBUSMAP_SIZE))

u_long
getdvmapages(int npages, u_long addrlo, u_long addrhi, u_int align,
	u_int cntr, int cansleep, struct map *dvmamap)
{
	u_long alo = iommu_btop(addrlo);
	u_long ahi, amax, amin, aseg;
	u_long addr = 0;

	if (addrhi != (u_long) -1) {
		/*
		 * -1 is our magic NOOP for no high limit. If it's not -1,
		 * make addrhi 1 bigger since ahi is a non-inclusive limit,
		 * but addrhi is an inclusive limit.
		 */
		addrhi++;
		amax = iommu_btop(addrhi);
	} else {
		amax = iommu_btop(addrhi) + 1;
	}
	/*
	 * If we have a counter restriction we adjust ahi to the
	 * minimum of the maximum address and the end of the
	 * current segment. Actually it is the end+1 since ahi
	 * is always excluding. We then allocate dvma space out
	 * of a segment instead from the whole map. If the allocation
	 * fails we try the next segment.
	 */
	if (COUNTER_RESTRICTION(cntr)) {
		u_long a;

		if (WITHIN_DVMAMAP(alo)) {
			a = addrlo;
		} else {
			a = iommu_ptob(SBUSMAP_BASE);
		}
		/*
		 * check for wrap around
		 */
		if (a + (u_long) 1 + cntr <= a) {
			ahi = iommu_btop((u_long) -1) + 1;
		} else {
			ahi = SEG_ALIGN(a, cntr);
		}
		ahi = min(amax, ahi);
		aseg = ahi;
		amin = alo;
	} else {
		ahi = amax;
	}

	/*
	 * Okay. Now try and allocate the space.
	 *
	 * we may have a 'constrained' allocation;
	 * if so, we have to search dvmamap for a piece
	 * that fits the constraints.
	 */
	if (WITHIN_DVMAMAP(alo) || WITHIN_DVMAMAP(ahi) ||
	    COUNTER_RESTRICTION(cntr)) {
		register struct map *mp;

		/*
		 * Search for a piece that will fit.
		 */
		mutex_enter(&maplock(dvmamap));
again:
		for (mp = mapstart(dvmamap); mp->m_size; mp++) {
			u_int ok, end;

			end = mp->m_addr + mp->m_size;

			if (alo < mp->m_addr) {
				if (ahi >= end)
					ok = (mp->m_size >= npages);
				else {
					end = ahi;
					ok = (mp->m_addr + npages <= ahi);
				}
				addr = mp->m_addr;
			} else {
				if (ahi >= end)
					ok = (alo + npages <= end);
				else {
					end = ahi;
					ok = (alo + npages <= ahi);
				}
				addr = alo;
			}

			DPRINTF(IOMMU_DMAMAP_DEBUG, ("Map range %x:%x alo %x "
			    "ahi %x addr %x end %x\n", mp->m_addr,
			    mp->m_addr + mp->m_size, alo, ahi, addr, end));

			/* If we have a valid region, we're done */
			if (ok)
				break;

		}

		if (mp->m_size != 0) {
			u_long addrx = addr;

			/*
			 * Let rmget do the rest of the work.
			 */
			addr = rmget(dvmamap, (long)npages, addr);

			if (chk_map_addr(addr, "rmget")) {
				cmn_err(CE_CONT, "addrx=0x%x, mp=0x%x, 0x%x\n",
				    (int)addrx, (int)mp->m_addr,
				    (int)mp->m_size);
				cmn_err(CE_CONT, "addrlo=0x%x, addrhi=0x%x, "
				    "align=0x%x\n", (int)addrlo, (int)addrhi,
				    align);
			}

		} else {
			addr = 0;
		}

		if (addr == 0) {
			/*
			 * If we have a counter restriction we walk the
			 * dvma space in segments at a time. If we
			 * reach the last segment we reset alo and ahi
			 * to the original values. This allows us to
			 * walk the segments again in case we have to
			 * switch to unaligned mappings or we were out
			 * of resources.
			 */
			if (COUNTER_RESTRICTION(cntr)) {
				if (ahi < amax) {
					alo = ahi;
					ahi = min(amax,
						ahi + mmu_btopr(cntr));
					goto again;
				} else {
					/*
					 * reset alo and ahi in case we
					 * have to walk the segments again
					 */
					alo = amin;
					ahi = aseg;
				}
			}
		}

		if (addr == 0 && cansleep) {
			DPRINTF(IOMMU_DMAMAP_DEBUG, ("getdvmapages: sleep on "
			    "constrained alloc\n"));

			mapwant(dvmamap) = 1;
			cv_wait(&map_cv(dvmamap), &maplock(dvmamap));
			goto again;
		}

		mutex_exit(&maplock(dvmamap));

	} else {
		if (cansleep) {
			addr = rmalloc_wait(dvmamap, npages);

			if (chk_map_addr(addr, "rmget")) {
				cmn_err(CE_CONT, "addrlo=0x%x, addrhi=0x%x\n",
					(int)addrlo, (int)addrhi);
			}

		} else {
			addr = rmalloc(dvmamap, npages);

			if (chk_map_addr(addr, "rmget")) {
				cmn_err(CE_CONT, "addrlo=0x%x, addrhi=0x%x\n",
					(int)addrlo, (int)addrhi);
			}
		}
	}

	if (addr) {
		addr = iommu_ptob(addr);
	}

	return (addr);
}

void
putdvmapages(u_long addr, int npages, struct map *dvmamap)
{
	addr = iommu_btop(addr);
	rmfree(dvmamap, (long) npages, addr);
}

/*
 * Shorthand defines
 */

#define	DMAOBJ_PP_PP	dmao_obj.pp_obj.pp_pp
#define	DMAOBJ_PP_OFF	dmao_ogj.pp_obj.pp_offset
#define	ALO		dma_lim->dlim_addr_lo
#define	AHI		dma_lim->dlim_addr_hi
#define	CMAX		dma_lim->dlim_cntr_max
#define	OBJSIZE		dmareq->dmar_object.dmao_size
#define	ORIGVADDR	dmareq->dmar_object.dmao_obj.virt_obj.v_addr
#define	RED		((mp->dmai_rflags & DDI_DMA_REDZONE)? 1 : 0)
#define	DIRECTION	(mp->dmai_rflags & DDI_DMA_RDWR)
#define	IOTTE_NDX(vaddr, base) (base + \
		(int) (iommu_btop((vaddr & ~IOMMU_PAGEMASK) - \
		IOMMU_DVMA_BASE)))
/*
 * If DDI_DMA_PARTIAL flag is set and the request is for
 * less than MIN_DVMA_WIN_SIZE, it's not worth the hassle so
 * we turn off the DDI_DMA_PARTIAL flag
 */
#define	MIN_DVMA_WIN_SIZE	(64)

void
iommu_remove_mappings(dev_info_t *dip, ddi_dma_impl_t *mp)
{

	register u_int npages;
	register u_long ioaddr;
	u_ll_t *iotte_ndx;
	struct sbus_soft_state *softsp =
		(struct sbus_soft_state *) ddi_get_soft_state(sbusp,
		ddi_get_instance(dip));
#if	defined(DEBUG) && defined(IO_MEMUSAGE)
	struct io_mem_list **prevp, *walk;
#endif /* DEBUG && IO_MEMUSAGE */


	/*
	 * Run thru the mapped entries and free 'em
	 */

	ioaddr = mp->dmai_mapping & ~IOMMU_PAGEOFFSET;
	npages = mp->dmai_ndvmapages;

#if	defined(DEBUG) && defined(IO_MEMUSAGE)
	mutex_enter(&softsp->iomemlock);
	prevp = &softsp->iomem;
	walk = softsp->iomem;

	while (walk) {
		if (walk->ioaddr == ioaddr) {
			*prevp = walk->next;
			break;
		}

		prevp = &walk->next;
		walk = walk->next;
	}
	mutex_exit(&softsp->iomemlock);

	kmem_free(walk->pfn, sizeof (u_int) * (npages + 1));
	kmem_free(walk, sizeof (struct io_mem_list));
#endif /* DEBUG && IO_MEMUSAGE */

	while (npages) {
		iotte_ndx = IOTTE_NDX(ioaddr,
				softsp->soft_tsb_base_addr);
		DPRINTF(IOMMU_DMAMCTL_DEBUG,
		    ("dma_mctl: freeing virt "
			"addr 0x%x, with IOTTE index 0x%x.\n",
			ioaddr, iotte_ndx));
		iommu_tteunload(iotte_ndx);
		iommu_tlb_flush(softsp, ioaddr);
		npages--;
		ioaddr += IOMMU_PAGESIZE;
	}

}


int
iommu_create_mappings(dev_info_t *dip, ddi_dma_impl_t *mp, u_long addr,
page_t *pp)
{
	extern struct as kas;
	register u_long offset;
	u_int pfn;
	struct as *as = 0;
	register int npages;
	register u_long ioaddr;
	u_ll_t *iotte_ndx;
	u_ll_t tmp_iotte_flag = IOTTE_CACHE;	/* Set Cache for mem xfer */
	int rval = DDI_DMA_MAPPED;
	struct sbus_soft_state *softsp =
		(struct sbus_soft_state *) ddi_get_soft_state(sbusp,
		ddi_get_instance(dip));
#if	defined(DEBUG) && defined(IO_MEMUSAGE)
	struct io_mem_list *iomemp;
	u_int *pfnp;
#endif /* DEBUG && IO_MEMUSAGE */

	offset = mp->dmai_mapping & IOMMU_PAGEOFFSET;
	npages = iommu_btopr(mp->dmai_size + offset);
	ioaddr = mp->dmai_mapping & ~IOMMU_PAGEOFFSET;
	iotte_ndx = IOTTE_NDX(ioaddr, softsp->soft_tsb_base_addr);

	if (mp->dmai_object.dmao_type == DMA_OTYP_VADDR) {
		as = mp->dmai_object.dmao_obj.virt_obj.v_as;
		if (as == (struct as *) 0)
			as = &kas;
	}

	/*
	 * Set the per object bits of the TTE here. We optimize this for
	 * the memory case so that the while loop overhead is minimal.
	 */
	/* Set the writable bit if necesary */
	if (mp->dmai_rflags & DDI_DMA_READ)
		tmp_iotte_flag |= IOTTE_WRITE;

	/* Turn on NOSYNC if we need consistent mem */
	if (mp->dmai_rflags & DDI_DMA_CONSISTENT)
		mp->dmai_rflags |= DMP_NOSYNC;

	/* Set streaming mode if not consistent mem */
	if (!(mp->dmai_rflags & DDI_DMA_CONSISTENT) &&
	    !softsp->stream_buf_off)
		tmp_iotte_flag |= IOTTE_STREAM;

#if	defined(DEBUG) && defined(IO_MEMUSAGE)
	iomemp = (struct io_mem_list *) kmem_alloc(sizeof (struct io_mem_list),
	    KM_SLEEP);
	iomemp->rdip = mp->dmai_rdip;
	iomemp->ioaddr = ioaddr;
	iomemp->addr = addr;
	iomemp->npages = npages;
	pfnp = iomemp->pfn = (u_int *) kmem_zalloc(
	    sizeof (u_int) * (npages + 1), KM_SLEEP);
#endif /* DEBUG && IO_MEMUSAGE */
	/*
	 * Grab the mappings from the dmmu and stick 'em into the
	 * iommu.
	 */
	while (npages > 0) {
		int upaid;
		u_ll_t iotte_flag;

		iotte_flag = tmp_iotte_flag;

		/*
		 * First, fetch the pte(s) we're interested in.
		 */
		if (pp) {
			pfn = page_pptonum(pp);

		} else {
			extern u_int sfmmu_getpfnum(struct as *, caddr_t);

			ASSERT(as);
			/*
			 * We should really be calling hat_getpfnum and
			 * grabbing the as lock.
			 * Unfortunately, this routine can be called at
			 * interrupt time.  If a thread has the address space
			 * lock and is waiting for io, then the interrupt
			 * thread (responsible for doing the io) could
			 * potentially block in this routine and create a
			 * deadlock.
			 */
			pfn = sfmmu_getpfnum(as, (caddr_t) addr);
			ASSERT(pfn != (u_int) -1);

			if (IO_BUSTYPE(pfn)) {
				/* DVMA'ing to IO space */

				/* Turn off cache bit if set */
				if (iotte_flag & IOTTE_CACHE)
					iotte_flag ^= IOTTE_CACHE;

				/* Turn off stream bit if set */
				if (iotte_flag & IOTTE_STREAM)
					iotte_flag ^= IOTTE_STREAM;

				if ((upaid = PFN_TO_UPAID(pfn)) ==
				    softsp->upa_id) {
					/* Intra sbus transfer */

					/* Turn on intra flag */
					iotte_flag |= IOTTE_INTRA;
#ifdef DEBUG
					{
					u_int hi, lo;
					hi = (u_int)(iotte_flag >> 32);
					lo = (u_int)(iotte_flag & 0xffffffff);
					DPRINTF(IOMMU_INTER_INTRA_XFER, (
					    "Intra xfer "
					    "pfnum 0x%x TTE hi0x%x lo0x%x\n",
					    pfn, hi, lo));
					}
#endif /* DEBUG */
				} else {
					extern int get_upa_dma_flag(int);

					/*
					 * Transfer to IO space we need to
					 * check if the recipient is capable
					 * of accepting DMA transfers
					 */
					if (get_upa_dma_flag(upaid) == 1) {
#ifdef lint
						dip = dip;
#endif /* lint */
#ifdef DEBUG
						{
						u_int hi, lo;
						hi = (u_int)(iotte_flag >> 32);
						lo = (u_int)(iotte_flag &
						    0xffffffff);
						DPRINTF(IOMMU_INTER_INTRA_XFER,
						    ("Intra xfer pfnum 0x%x "
						    "TTE hi 0x%x lo 0x%x\n",
						    pfn, hi, lo));
						}
#endif /* DEBUG */
					} else {
						rval = DDI_DMA_NOMAPPING;
						goto out;
					}
				}
			}
		}

#ifdef DEBUG
		{
		u_int hi, lo;
		hi = (u_int)(iotte_flag >> 32);
		lo = (u_int)(iotte_flag & 0xffffffff);
		DPRINTF(IOMMU_TTE, ("TTE index 0x%x, pfn 0x%x, tte flag hi "
		    "0x%x lo 0x%x\n", iotte_ndx, pfn, hi, lo));
		}
#endif /* DEBUG */

		iommu_tteload(iotte_ndx, pfn, iotte_flag);

		/*
		 * adjust values of interest
		 */
		npages--;
		iotte_ndx++;
#if	defined(DEBUG) && defined(IO_MEMUSAGE)
		*pfnp = pfn;
		pfnp++;
#endif /* DEBUG && IO_MEMUSAGE */

		if (pp) {
			pp = pp->p_next;
		} else {
			addr += IOMMU_PAGESIZE;
		}
	}

#if	defined(DEBUG) && defined(IO_MEMUSAGE)
	mutex_enter(&softsp->iomemlock);
	iomemp->next = softsp->iomem;
	softsp->iomem = iomemp;
	mutex_exit(&softsp->iomemlock);
#endif /* DEBUG && IO_MEMUSAGE */

out:
	if (rval == DDI_DMA_NOMAPPING) {
		/* If we fail a mapping, free up any mapping resources used */
		iommu_remove_mappings(dip, mp);
	}

	return (rval);
}


int
iommu_dma_lim_setup(dev_info_t *dip, dev_info_t *rdip,
    struct sbus_soft_state *softsp, u_int *burstsizep, u_int burstsize64,
    u_int *minxferp, u_int dma_flags)
{

	/* Take care of 64 byte limits. */
	if (!(dma_flags & DDI_DMA_SBUS_64BIT)) {
		/*
		 * return burst size for 32-bit mode
		 */
		*burstsizep &= softsp->sbus_burst_sizes;
		return (DDI_FAILURE);
	} else {
		/*
		 * check if SBus supports 64 bit and if caller
		 * is child of SBus. No support through bridges
		 */
		if (softsp->sbus64_burst_sizes &&
		    (ddi_get_parent(rdip) == dip)) {
			struct regspec *rp;

			rp = ddi_rnumber_to_regspec(rdip, 0);
			if (rp == (struct regspec *)0) {
				*burstsizep &=
					softsp->sbus_burst_sizes;
				return (DDI_FAILURE);
			} else {
				/* Check for old-style 64 bit burstsizes */
				if (burstsize64 & SYSIO64_BURST_MASK) {
					/* Scale back burstsizes if Necessary */
					*burstsizep &=
					    (softsp->sbus64_burst_sizes |
					    softsp->sbus_burst_sizes);
				} else {
					/* Get the 64 bit burstsizes. */
					*burstsizep = burstsize64;

					/* Scale back burstsizes if Necessary */
					*burstsizep &=
					    (softsp->sbus64_burst_sizes >>
					    SYSIO64_BURST_SHIFT);
				}

				/*
				 * Set the largest value of the smallest
				 * burstsize that the device or the bus
				 * can manage.
				 */
				*minxferp = max(*minxferp, (1 <<
				    (ddi_ffs(softsp->sbus64_burst_sizes) -1)));

				return (DDI_SUCCESS);
			}
		} else {
			/*
			 * SBus doesn't support it or bridge. Do 32-bit
			 * xfers
			 */
			*burstsizep &= softsp->sbus_burst_sizes;
			return (DDI_FAILURE);
		}
	}
}


int
iommu_dma_allochdl(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_attr_t *dma_attr, int (*waitfp)(caddr_t), caddr_t arg,
    ddi_dma_handle_t *handlep)
{
	u_long addrlow, addrhigh;
	ddi_dma_impl_t *mp;
	struct sbus_soft_state *softsp =
		(struct sbus_soft_state *) ddi_get_soft_state(sbusp,
		ddi_get_instance(dip));


	/*
	 * Setup dma burstsizes and min-xfer counts.
	 */
	(void) iommu_dma_lim_setup(dip, rdip, softsp,
	    &dma_attr->dma_attr_burstsizes,
	    (u_int) dma_attr->dma_attr_burstsizes, &dma_attr->dma_attr_minxfer,
	    dma_attr->dma_attr_flags);

	if (dma_attr->dma_attr_burstsizes == 0) {
		return (DDI_DMA_BADATTR);
	}
	addrlow = (u_long)dma_attr->dma_attr_addr_lo;
	addrhigh = (u_long)dma_attr->dma_attr_addr_hi;

	DPRINTF(IOMMU_DMA_ALLOCHDL_DEBUG, ("dma_allochdl: (%s) hi %x lo 0x%x "
	    "min 0x%x burst 0x%x\n", ddi_get_name(dip), addrhigh, addrlow,
	    dma_attr->dma_attr_minxfer, dma_attr->dma_attr_burstsizes));

	/*
	 * Check sanity for hi and lo address limits
	 */
	if ((addrhigh <= addrlow) || (addrhigh < (u_long)IOMMU_DVMA_BASE)) {
		return (DDI_DMA_BADATTR);
	}

	mutex_enter(&softsp->dma_pool_lock);
	mp = (ddi_dma_impl_t *)kmem_fast_zalloc(&softsp->dmaimplbase,
	    sizeof (*mp), 2, (waitfp == DDI_DMA_SLEEP) ? KM_SLEEP : KM_NOSLEEP);
	mutex_exit(&softsp->dma_pool_lock);

	if (mp == NULL) {
		if (waitfp != DDI_DMA_DONTWAIT) {
		    ddi_set_callback(waitfp, arg, &softsp->dvma_call_list_id);
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

/* ARGSUSED */
int
iommu_dma_freehdl(dev_info_t *dip, dev_info_t *rdip,
	ddi_dma_handle_t handle)
{
	ddi_dma_impl_t *mp = (ddi_dma_impl_t *)handle;
	struct sbus_soft_state *softsp =
		(struct sbus_soft_state *) ddi_get_soft_state(sbusp,
		ddi_get_instance(dip));


	mutex_enter(&softsp->dma_pool_lock);
	kmem_fast_free(&softsp->dmaimplbase, (caddr_t)mp);
	mutex_exit(&softsp->dma_pool_lock);

	if (softsp->dvma_call_list_id != 0) {
		ddi_run_callback(&softsp->dvma_call_list_id);
	}
	return (DDI_SUCCESS);
}


static int
iommu_dma_setup(dev_info_t *dip, struct ddi_dma_req *dmareq,
    u_long addrlow, u_long addrhigh, u_int segalign,
    ddi_dma_impl_t *mp)
{
	extern struct as kas;
	page_t *pp;
	u_int off;
	struct as *as;
	u_int size;
/*LINTED warning: constant truncated by assignment */
	u_int align = (u_int) -1;
	u_long addr, ioaddr, offset;
	int npages, rval;
	u_ll_t *iotte_ndx;
	struct sbus_soft_state *softsp =
		(struct sbus_soft_state *) ddi_get_soft_state(sbusp,
		ddi_get_instance(dip));

	DPRINTF(IOMMU_DMA_SETUP_DEBUG, ("dma_setup: hi %x lo 0x%x\n",
	    addrhigh, addrlow));

	size = OBJSIZE;
	off = size - 1;
	if (off > segalign) {
		if ((dmareq->dmar_flags & DDI_DMA_PARTIAL) == 0) {
			rval = DDI_DMA_TOOBIG;
			goto bad;
		}
		size = segalign + 1;
	}
	if (addrlow + off > addrhigh || addrlow + off < addrlow) {
		if (!((addrlow + OBJSIZE == 0) && (addrhigh == (u_long) -1))) {
			if ((dmareq->dmar_flags & DDI_DMA_PARTIAL) == 0) {
				rval = DDI_DMA_TOOBIG;
				goto bad;
			}
			size = min(addrhigh - addrlow + 1, size);
		}
	}

	/*
	 * Validate the dma request.
	 *
	 * At the same time, determine whether or not the virtual address
	 * of the object to be mapped for I/O is already mapped (and locked)
	 * and addressable by the requestors dma engine.
	 */
	switch (dmareq->dmar_object.dmao_type) {
	default:
	case DMA_OTYP_PADDR:
		/*
		 * Not a supported type for this implementation
		 */
		rval = DDI_DMA_NOMAPPING;
		goto bad;

	case DMA_OTYP_VADDR:
		addr = (u_long) dmareq->dmar_object.dmao_obj.virt_obj.v_addr;
		offset = addr & IOMMU_PAGEOFFSET;
		as = dmareq->dmar_object.dmao_obj.virt_obj.v_as;
		if (as == (struct as *) 0)
			as = &kas;
		addr &= ~IOMMU_PAGEOFFSET;

		npages = iommu_btopr(OBJSIZE + offset);

		DPRINTF(IOMMU_DMAMAP_DEBUG, ("dma_map vaddr: # of pages 0x%x "
			    "request addr 0x%x off 0x%x OBJSIZE  0x%x \n",
			    npages, addr, offset, OBJSIZE));
		/*
		 * For now, I'm punting on the mixed mode memory types.
		 * This is where the code should be added to perform the
		 * the memory check and take any appropriate actions.  (RAZ)
		 */

		pp = (page_t *) 0;
		break;

	case DMA_OTYP_PAGES:
		/*
		 * If this is an advisory call, then we're done.
		 */
		if (mp == 0) {
			goto out;
		}
		pp = dmareq->dmar_object.dmao_obj.pp_obj.pp_pp;
		offset = dmareq->dmar_object.dmao_obj.pp_obj.pp_offset;
		npages = iommu_btopr(OBJSIZE + offset);

		DPRINTF(IOMMU_DMA_SETUP_DEBUG, ("dma_setup pages: pg %x"
				"pp = %x , offset = %x OBJSIZE = %x \n",
				npages, pp, offset, OBJSIZE));
		break;
	}

	/*
	 * At this point, we know for sure that we are going to need
	 * to do some mapping. If this is an advisory call, we're done
	 * because we already checked the legality of the DMA_OTYP_VADDR
	 * case above.
	 */

	if (mp == 0) {
		goto out;
	}

	/*
	 * Get the number of pages we need to allocate. If the request
	 * is marked DDI_DMA_PARTIAL, do the work necessary to set this
	 * up right. Up until now, npages is the total number of pages
	 * needed to map the entire object. We may rewrite npages to
	 * be the number of pages necessary to map a MIN_DVMA_WIN_SIZE window
	 * onto the object (including any beginning offset).
	 *
	 */


	if (mp->dmai_rflags & DDI_DMA_PARTIAL) {
		/*
		 * If the size was rewritten above due to device dma
		 * constraints, make sure that it still makes sense
		 * to attempt anything. Also, in this case, the
		 * ability to do a dma mapping at all predominates
		 * over any attempt at optimizing the size of such
		 * a mapping.
		 *
		 * XXX: Well, we don't really do any optimization here.
		 * XXX: We have the device's dma speed (in kb/s), but
		 * XXX: that is for some future microoptimization.
		 */

		if (size != OBJSIZE) {
			/*
			 * If the request is for partial mapping arrangement,
			 * the device has to be able to address at least the
			 * size of the window we are establishing.
			 */
			if (size < iommu_ptob(MIN_DVMA_WIN_SIZE)) {
				rval = DDI_DMA_NOMAPPING;
				goto bad;
			}
			npages = iommu_btopr(size + offset);
		}
		/*
		 * If the size requested is less than a moderate amt,
		 * skip the partial mapping stuff- it's not worth the
		 * effort.
		 */
		if (npages > MIN_DVMA_WIN_SIZE) {
			npages = MIN_DVMA_WIN_SIZE + iommu_btopr(offset);
			size = iommu_ptob(MIN_DVMA_WIN_SIZE);
			DPRINTF(IOMMU_DMA_SETUP_DEBUG, ("dma_setup: SZ %x pg "
			    "%x sz %x \n", OBJSIZE, npages, size));
		} else {
			mp->dmai_rflags ^= DDI_DMA_PARTIAL;
		}
	} else {
		/*
		 * We really need to have a running check
		 * of the amount of dvma pages available,
		 * but that is too hard. We hope that the
		 * amount of space 'permanently' taken
		 * up out of the beginning pool of dvma
		 * pages is not significant.
		 *
		 * We give more slack to requestors who
		 * cannot do partial mappings, but we
		 * do not give them carte blanche.
		 */
		if (npages >= SBUSMAP_SIZE - MIN_DVMA_WIN_SIZE) {
			rval = DDI_DMA_TOOBIG;
			goto bad;
		}
	}

	/*
	 * At this point, we know that we are doing dma to or from memory
	 * that we have to allocate translation resources for and map.
	 *
	 * Establish dmai_size to be the size of the
	 * area we are mapping, not including any redzone,
	 * but accounting for any offset we are starting
	 * from. Note that this may be quite distinct from
	 * the actual size of the object itself.
	 */

	/*
	 * save dmareq-object, size and npages into mp
	 */
	mp->dmai_object = dmareq->dmar_object;
	mp->dmai_size = size;
	mp->dmai_ndvmapages = npages;

	/*
	 * Okay- we have to do some mapping here.
	 */
	ioaddr = getdvmapages(npages + RED, addrlow, addrhigh, align, segalign,
	    (dmareq->dmar_fp == DDI_DMA_SLEEP)? 1 : 0, softsp->dvmamap);

	if (ioaddr == 0) {
		if (dmareq->dmar_fp == DDI_DMA_SLEEP)
			rval = DDI_DMA_NOMAPPING;
		else
			rval = DDI_DMA_NORESOURCES;
		goto bad;
	}

	/*
	 * establish real virtual address for caller
	 * This field is invariant throughout the
	 * life of the mapping.
	 */

	mp->dmai_mapping = (u_long) (ioaddr + offset);

#ifdef	DEBUG
	if (mp->dmai_mapping < IOMMU_DVMA_BASE) {
		DPRINTF(IOMMU_DMA_SETUP_DEBUG, ("*** bad DVMA addr=0x%x, "
		    "ioaddr=0x%x, offset=0x%x\n", mp->dmai_mapping, ioaddr,
		    offset));
	}
#endif	/* DEBUG */

	/*
	 * Calculate the iotte entry we need to start with.  Since we're
	 * accessing the tsb array in MMU bypass mode, we only need the
	 * index of the virtual page.
	 */
	iotte_ndx = IOTTE_NDX(ioaddr, softsp->soft_tsb_base_addr);

	/*
	 * At this point we have a range of virtual address allocated
	 * with which we now have to map to the requested object.
	 */
	if ((rval = iommu_create_mappings(dip, mp, addr, pp)) ==
	    DDI_DMA_NOMAPPING)
		goto bad;


	/*
	 * Establish the redzone, if required.
	 */
	if (RED) {
		ioaddr += (npages * IOMMU_PAGESIZE);
		iotte_ndx += npages;
		iommu_tteunload(iotte_ndx);
		iommu_tlb_flush(softsp, ioaddr);
	}
out:
	/*
	 * return success
	 */
	if (mp) {
		DPRINTF(IOMMU_DMA_SETUP_DEBUG, ("dma_setup: handle %x flags %x "
		    "kaddr %x size %x\n", mp, mp->dmai_rflags,
		    mp->dmai_mapping, mp->dmai_size));
		if (mp->dmai_rflags & DDI_DMA_PARTIAL) {
			size = iommu_ptob(
				mp->dmai_ndvmapages - iommu_btopr(offset));
			mp->dmai_nwin =
			    (dmareq->dmar_object.dmao_size + (size - 1)) / size;
			return (DDI_DMA_PARTIAL_MAP);
		} else {
			mp->dmai_nwin = 0;
			return (DDI_DMA_MAPPED);
		}
	} else {
		return (DDI_DMA_MAPOK);
	}
bad:

	DPRINTF(IOMMU_DMA_SETUP_DEBUG, ("?*** iommu_dma_setup: failure(%d)\n",
	    rval));

	if (rval == DDI_DMA_NORESOURCES &&
	    dmareq->dmar_fp != DDI_DMA_DONTWAIT) {
		ddi_set_callback(dmareq->dmar_fp,
		    dmareq->dmar_arg, &softsp->dvma_call_list_id);
	}
	return (rval);
}

/* ARGSUSED */
int
iommu_dma_bindhdl(dev_info_t *dip, dev_info_t *rdip,
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


	rval = iommu_dma_setup(dip, dmareq, addrlow, addrhigh,
			(u_int)dma_attr->dma_attr_seg, mp);
	if (rval && (rval != DDI_DMA_PARTIAL_MAP)) {
		mp->dmai_inuse = 0;
		return (rval);
	}
	cp->dmac_notused = 0;
	cp->dmac_address = mp->dmai_mapping;
	cp->dmac_size = mp->dmai_size;
	cp->dmac_type = 0;
	*ccountp = 1;

	DPRINTF(IOMMU_DMA_BINDHDL_DEBUG, ("iommu_dma_bindhdl :"
	    "Cookie addr 0x%x, size 0x%x \n", cp->dmac_address,
	    cp->dmac_size));

	return (rval);
}

/* ARGSUSED */
int
iommu_dma_unbindhdl(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle)
{
	register u_long addr;
	register u_int npages;
	register ddi_dma_impl_t *mp = (ddi_dma_impl_t *) handle;
	struct sbus_soft_state *softsp =
		(struct sbus_soft_state *) ddi_get_soft_state(sbusp,
		ddi_get_instance(dip));

	addr = mp->dmai_mapping & ~IOMMU_PAGEOFFSET;
	DPRINTF(IOMMU_DMA_UNBINDHDL_DEBUG, ("iommu_dma_unbindhdl :"
	    "unbinding Virt addr 0x%x, for 0x%x pages.\n", addr,
	    mp->dmai_ndvmapages));

	/* sync the entire object */
	if (!(mp->dmai_rflags & DDI_DMA_CONSISTENT)) {
		/* flush stream write buffers */
		sync_stream_buf(softsp, mp->dmai_mapping,
			mp->dmai_object.dmao_size);
	}

	/*
	 * 'Free' the dma mappings.
	 */
	iommu_remove_mappings(dip, mp);

	addr = mp->dmai_mapping & ~IOMMU_PAGEOFFSET;
	npages = mp->dmai_ndvmapages;

	if (npages) {
		putdvmapages(addr, npages + RED, softsp->dvmamap);
	}


	mp->dmai_ndvmapages = 0;
	mp->dmai_inuse = 0;

	/*
	 * Now that we've freed some resource,
	 * if there is anybody waiting for it
	 * try and get them going.
	 */
	if (softsp->dvma_call_list_id != 0) {
		ddi_run_callback(&softsp->dvma_call_list_id);
	}

	return (DDI_SUCCESS);
}

/*ARGSUSED*/
int
iommu_dma_flush(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle, off_t off, u_int len,
    u_int cache_flags)
{
	register ddi_dma_impl_t *mp = (ddi_dma_impl_t *) handle;
	struct sbus_soft_state *softsp =
		(struct sbus_soft_state *) ddi_get_soft_state(sbusp,
		ddi_get_instance(dip));

	/* Make sure our mapping structure is valid */
	if (!mp)
		return (DDI_FAILURE);

	if (!(mp->dmai_rflags & DDI_DMA_CONSISTENT)) {
		sync_stream_buf(softsp, mp->dmai_mapping,
		    mp->dmai_size);
	}
	return (DDI_SUCCESS);
}

/*ARGSUSED*/
int
iommu_dma_win(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle, uint_t win, off_t *offp,
    uint_t *lenp, ddi_dma_cookie_t *cookiep, uint_t *ccountp)
{
	ddi_dma_impl_t *mp = (ddi_dma_impl_t *)handle;
	u_long offset;
	u_long winsize, newoff;
	int rval;


	offset = mp->dmai_mapping & IOMMU_PAGEOFFSET;
	winsize = iommu_ptob(mp->dmai_ndvmapages - iommu_btopr(offset));

	DPRINTF(IOMMU_DMA_WIN_DEBUG, ("getwin win %d winsize %x\n", win,
	    (int)winsize));

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
	cookiep->dmac_type = 0;
	cookiep->dmac_address = mp->dmai_mapping;
	cookiep->dmac_size = mp->dmai_size;
	*ccountp = 1;
	*offp = (off_t)newoff;
	*lenp = (u_int)winsize;

	if (newoff == mp->dmai_offset) {
		/*
		 * Nothing to do...
		 */
		return (DDI_SUCCESS);
	}

	if ((rval = iommu_map_window(dip, mp, newoff, winsize)) !=
	    DDI_SUCCESS) {
		return (rval);
	}

	/*
	 * Set this again in case iommu_map_window() has changed it
	 */
	cookiep->dmac_size = mp->dmai_size;

	return (DDI_SUCCESS);
}

static int
iommu_map_window(dev_info_t *dip, ddi_dma_impl_t *mp, u_long newoff,
    u_long winsize)
{
	u_long addr;
	page_t *pp;
	u_long flags;
	struct sbus_soft_state *softsp =
		(struct sbus_soft_state *) ddi_get_soft_state(sbusp,
		ddi_get_instance(dip));


	/* Free mappings for current window */
	iommu_remove_mappings(dip, mp);

	mp->dmai_offset = newoff;
	mp->dmai_size = mp->dmai_object.dmao_size - newoff;
	mp->dmai_size = min(mp->dmai_size, winsize);

	if (mp->dmai_object.dmao_type == DMA_OTYP_VADDR) {
		addr = (u_long)
		    mp->dmai_object.dmao_obj.virt_obj.v_addr;
		addr = (addr + newoff) & ~IOMMU_PAGEOFFSET;

		pp = NULL;
	} else {
		pp = mp->dmai_object.dmao_obj.pp_obj.pp_pp;
		flags = 0;
		while (flags < newoff) {
			pp = pp->p_next;
			flags += MMU_PAGESIZE;
		}
	}

	/* Set up mappings for next window */
	if (iommu_create_mappings(dip, mp, addr, pp) < 0)
		return (DDI_FAILURE);

	/*
	 * also invalidate read stream buffer
	 */
	if (!(mp->dmai_rflags & DDI_DMA_CONSISTENT)) {
		sync_stream_buf(softsp, mp->dmai_mapping, mp->dmai_size);
	}

	return (DDI_SUCCESS);

}

int
iommu_dma_map(dev_info_t *dip, dev_info_t *rdip,
    struct ddi_dma_req *dmareq, ddi_dma_handle_t *handlep)
{
	ddi_dma_lim_t *dma_lim = dmareq->dmar_limits;
	ddi_dma_impl_t *mp;
	u_long addrlow, addrhigh;
	int rval;
	struct sbus_soft_state *softsp =
		(struct sbus_soft_state *) ddi_get_soft_state(sbusp,
		ddi_get_instance(dip));


	/*
	 * Setup dma burstsizes and min-xfer counts.
	 */
	(void) iommu_dma_lim_setup(dip, rdip, softsp, &dma_lim->dlim_burstsizes,
	    (u_int) dma_lim->dlim_burstsizes, &dma_lim->dlim_minxfer,
	    dmareq->dmar_flags);


	DPRINTF(IOMMU_DMAMAP_DEBUG, ("dma_map: %s (%s) hi %x lo 0x%x min 0x%x "
	    "burst 0x%x\n", (handlep)? "alloc" : "advisory",
	    ddi_get_name(rdip), AHI, ALO, dma_lim->dlim_minxfer,
	    dma_lim->dlim_burstsizes));

	/*
	 * If not an advisory call, get a dma record.
	 */
	if (handlep) {
		mutex_enter(&softsp->dma_pool_lock);
		mp = (ddi_dma_impl_t *) kmem_fast_alloc(&softsp->dmaimplbase,
		    sizeof (*mp), 2, (dmareq->dmar_fp == DDI_DMA_SLEEP) ?
		    KM_SLEEP : KM_NOSLEEP);
		mutex_exit(&softsp->dma_pool_lock);

		DPRINTF(IOMMU_DMAMAP_DEBUG, ("iommu_dma_map: handlep != 0, "
		    "mp= 0x%x\n", (u_int)(mp)));

		if (mp == 0) {
			rval = DDI_DMA_NORESOURCES;
			goto bad;
		}

		/*
		 * Save requestor's information
		 */
		mp->dmai_rdip = rdip;
		mp->dmai_rflags = dmareq->dmar_flags & DMP_DDIFLAGS;
		mp->dmai_minxfer = dma_lim->dlim_minxfer;
		mp->dmai_burstsizes = dma_lim->dlim_burstsizes;
		mp->dmai_offset = 0;
		mp->dmai_ndvmapages = 0;
		mp->dmai_minfo = 0;
	} else {
		mp = (ddi_dma_impl_t *) 0;
	}

	/*
	 * Validate device burstsizes
	 */
	if (dma_lim->dlim_burstsizes == 0) {
		rval = DDI_DMA_NOMAPPING;
		goto bad;
	}

	addrlow = dma_lim->dlim_addr_lo;
	addrhigh = dma_lim->dlim_addr_hi;

	/*
	 * Check sanity for hi and lo address limits
	 */
	if ((addrhigh <= addrlow) || (addrhigh < (u_long)IOMMU_DVMA_BASE)) {
		rval = DDI_DMA_NOMAPPING;
		goto bad;
	}

	rval = iommu_dma_setup(dip, dmareq, addrlow, addrhigh,
		dma_lim->dlim_cntr_max, mp);
bad:
	if (rval && (rval != DDI_DMA_PARTIAL_MAP)) {
		if (mp) {
			mutex_enter(&softsp->dma_pool_lock);
			kmem_fast_free(&softsp->dmaimplbase, (caddr_t) mp);
			mutex_exit(&softsp->dma_pool_lock);
		}
	} else {
		if (mp) {
			*handlep = (ddi_dma_handle_t)mp;
		}
	}
	return (rval);
}

/*ARGSUSED*/
int
iommu_dma_mctl(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle, enum ddi_dma_ctlops request,
    off_t *offp, u_int *lenp,
    caddr_t *objp, u_int cache_flags)
{
	register u_long addr, offset;
	register u_int npages;
	register ddi_dma_cookie_t *cp;
	register ddi_dma_impl_t *mp = (ddi_dma_impl_t *) handle;
	struct sbus_soft_state *softsp =
		(struct sbus_soft_state *) ddi_get_soft_state(sbusp,
		ddi_get_instance(dip));

	DPRINTF(IOMMU_DMAMCTL_DEBUG, ("dma_mctl: handle %x ", mp));
	switch (request) {
	case DDI_DMA_FREE:
	{
		/*
		 * 'Free' the dma mappings.
		 */
		addr = mp->dmai_mapping & ~IOMMU_PAGEOFFSET;
		DPRINTF(IOMMU_DMAMCTL_DMA_FREE_DEBUG, ("iommu_dma_mctl dmafree:"
		    "freeing Virt addr 0x%x, for 0x%x pages.\n", addr,
		    mp->dmai_ndvmapages));
		/* sync the entire object */
		if (!(mp->dmai_rflags & DDI_DMA_CONSISTENT)) {
			/* flush stream write buffers */
			sync_stream_buf(softsp, mp->dmai_mapping,
				mp->dmai_object.dmao_size);
		}


		iommu_remove_mappings(dip, mp);

		addr = mp->dmai_mapping & ~IOMMU_PAGEOFFSET;
		npages = mp->dmai_ndvmapages;

		if (npages) {
			putdvmapages(addr, npages + RED, softsp->dvmamap);
		}

		/*
		 * put impl struct back on free list
		 */
		mutex_enter(&softsp->dma_pool_lock);
		kmem_fast_free(&softsp->dmaimplbase, (caddr_t)mp);
		mutex_exit(&softsp->dma_pool_lock);

		/*
		 * Now that we've freed some resource,
		 * if there is anybody waiting for it
		 * try and get them going.
		 */
		if (softsp->dvma_call_list_id != 0) {
			ddi_run_callback(&softsp->dvma_call_list_id);
		}

		break;
	}

	case DDI_DMA_SYNC:
		DPRINTF(IOMMU_DMAMCTL_SYNC_DEBUG, ("sync\n"));

		/* Make sure our mapping structure is valid */
		if (!mp)
			return (DDI_FAILURE);

		if (!(mp->dmai_rflags & DDI_DMA_CONSISTENT)) {
			sync_stream_buf(softsp, mp->dmai_mapping,
			    mp->dmai_size);
		}

		break;

	case DDI_DMA_SET_SBUS64:
		return (iommu_dma_lim_setup(dip, rdip, softsp,
		    &mp->dmai_burstsizes, (u_int) *lenp, &mp->dmai_minxfer,
		    DDI_DMA_SBUS_64BIT));

	case DDI_DMA_HTOC:
		/*
		 * Note that we are *not* cognizant of partial mappings
		 * at this level. We only support offsets for cookies
		 * that would then stick within the current mapping for
		 * a device.
		 *
		 * XXX: should we return an error if the resultant cookie
		 * XXX: is less than minxfer?
		 */
		DPRINTF(IOMMU_DMAMCTL_HTOC_DEBUG, ("htoc off %x mapping %x "
		    "size %x\n", (u_long) *offp, mp->dmai_mapping,
		    mp->dmai_size));

		if ((u_int) *offp >= mp->dmai_size) {
			return (DDI_FAILURE);
		}

		cp = (ddi_dma_cookie_t *) objp;
		cp->dmac_notused = 0;
		cp->dmac_address = (mp->dmai_mapping + (u_long) *offp);
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
			DPRINTF(IOMMU_DMAMCTL_KVADDR_DEBUG, ("kvaddr of phys "
			    "mapping\n"));
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
		register ddi_dma_win_t *owin, *nwin;
		u_long winsize, newoff;
		int rval;

		DPRINTF(IOMMU_DMAMCTL_NEXTWIN_DEBUG, ("nextwin\n"));

		mp = (ddi_dma_impl_t *) handle;
		owin = (ddi_dma_win_t *) offp;
		nwin = (ddi_dma_win_t *) objp;
		if (mp->dmai_rflags & DDI_DMA_PARTIAL) {
			if (*owin == NULL) {
				DPRINTF(IOMMU_DMAMCTL_NEXTWIN_DEBUG,
				    ("nextwin: win == NULL\n"));
				mp->dmai_offset = 0;
				*nwin = (ddi_dma_win_t) mp;
				return (DDI_SUCCESS);
			}

			offset = mp->dmai_mapping & IOMMU_PAGEOFFSET;
			winsize = iommu_ptob(mp->dmai_ndvmapages -
				iommu_btopr(offset));

			newoff = mp->dmai_offset + winsize;
			if (newoff > mp->dmai_object.dmao_size -
				mp->dmai_minxfer) {
				return (DDI_DMA_DONE);
			}


			if ((rval = iommu_map_window(dip, mp, newoff,
				    winsize)) != DDI_SUCCESS) {
				return (rval);
			}
		} else {
			DPRINTF(IOMMU_DMAMCTL_NEXTWIN_DEBUG, ("nextwin: no "
			    "partial mapping\n"));
			if (*owin != NULL) {
				return (DDI_DMA_DONE);
			}
			mp->dmai_offset = 0;
			*nwin = (ddi_dma_win_t) mp;
		}
		break;
	}

	case DDI_DMA_NEXTSEG:
	{
		register ddi_dma_seg_t *oseg, *nseg;

		DPRINTF(IOMMU_DMAMCTL_NEXTSEG_DEBUG, ("nextseg:\n"));

		oseg = (ddi_dma_seg_t *) lenp;
		if (*oseg != NULL) {
			return (DDI_DMA_DONE);
		} else {
			nseg = (ddi_dma_seg_t *) objp;
			*nseg = *((ddi_dma_seg_t *) offp);
		}
		break;
	}

	case DDI_DMA_SEGTOC:
	{
		register ddi_dma_seg_impl_t *seg;

		seg = (ddi_dma_seg_impl_t *) handle;
		cp = (ddi_dma_cookie_t *) objp;
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
		int rval;

		offset = mp->dmai_mapping & IOMMU_PAGEOFFSET;
		winsize = iommu_ptob(mp->dmai_ndvmapages - iommu_btopr(offset));

		DPRINTF(IOMMU_DMAMCTL_MOVWIN_DEBUG, ("movwin off %x len %x "
		    "winsize %x\n", *offp, *lenp, winsize));

		if ((mp->dmai_rflags & DDI_DMA_PARTIAL) == 0) {
			return (DDI_FAILURE);
		}

		if (*lenp != (u_int) -1 && *lenp != winsize) {
			DPRINTF(IOMMU_DMAMCTL_MOVWIN_DEBUG, ("bad length\n"));
			return (DDI_FAILURE);
		}
		newoff = (u_long) *offp;
		if (newoff & (winsize - 1)) {
			DPRINTF(IOMMU_DMAMCTL_MOVWIN_DEBUG, ("bad off\n"));
			return (DDI_FAILURE);
		}

		if (newoff == mp->dmai_offset) {
			/*
			 * Nothing to do...
			 */
			break;
		}

		/*
		 * Check out new address...
		 */
		if (newoff > mp->dmai_object.dmao_size - mp->dmai_minxfer) {
			DPRINTF(IOMMU_DMAMCTL_MOVWIN_DEBUG, ("newoff out of "
			    "range\n"));
			return (DDI_FAILURE);
		}

		if ((rval = iommu_map_window(dip, mp, newoff,
			    winsize)) != DDI_SUCCESS) {
			return (rval);
		}

		if ((cp = (ddi_dma_cookie_t *) objp) != 0) {
			cp->dmac_notused = 0;
			cp->dmac_address = mp->dmai_mapping;
			cp->dmac_size = mp->dmai_size;
			cp->dmac_type = 0;
		}
		*offp = (off_t) newoff;
		*lenp = (u_int) winsize;
		break;
	}

	case DDI_DMA_REPWIN:
		if ((mp->dmai_rflags & DDI_DMA_PARTIAL) == 0) {
			DPRINTF(IOMMU_DMAMCTL_REPWIN_DEBUG, ("repwin fail\n"));

			return (DDI_FAILURE);
		}

		*offp = (off_t) mp->dmai_offset;

		addr = mp->dmai_ndvmapages -
		    iommu_btopr(mp->dmai_mapping & IOMMU_PAGEOFFSET);

		*lenp = (u_int) iommu_ptob(addr);

		DPRINTF(IOMMU_DMAMCTL_REPWIN_DEBUG, ("repwin off %x len %x\n",
		    mp->dmai_offset, mp->dmai_size));

		break;

	case DDI_DMA_GETERR:
		DPRINTF(IOMMU_DMAMCTL_GETERR_DEBUG,
		    ("iommu_dma_mctl: geterr\n"));

		break;

	case DDI_DMA_COFF:
		cp = (ddi_dma_cookie_t *) offp;
		addr = cp->dmac_address;

		if (addr < mp->dmai_mapping ||
		    addr >= mp->dmai_mapping + mp->dmai_size)
			return (DDI_FAILURE);

		*objp = (caddr_t) (addr - mp->dmai_mapping);

		DPRINTF(IOMMU_DMAMCTL_COFF_DEBUG, ("coff off %x mapping %x "
		    "size %x\n", (u_long) *objp, mp->dmai_mapping,
		    mp->dmai_size));

		break;

	case DDI_DMA_RESERVE:
	{
		struct ddi_dma_req *dmareq = (struct ddi_dma_req *) offp;
		ddi_dma_lim_t *dma_lim;
		ddi_dma_handle_t *handlep;
		u_int np;
		u_long ioaddr;
		struct fast_dvma *iommu_fast_dvma;

		/* Some simple sanity checks */
		dma_lim = dmareq->dmar_limits;
		if (dma_lim->dlim_burstsizes == 0) {
			DPRINTF(IOMMU_BYPASS_RESERVE,
			    ("Reserve: bad burstsizes\n"));
			return (DDI_DMA_BADLIMITS);
		}
		if ((AHI <= ALO) || (AHI < IOMMU_DVMA_BASE)) {
			DPRINTF(IOMMU_BYPASS_RESERVE,
			    ("Reserve: bad limits\n"));
			return (DDI_DMA_BADLIMITS);
		}

		np = dmareq->dmar_object.dmao_size;
		mutex_enter(&softsp->dma_pool_lock);
		if (np > softsp->dma_reserve) {
			mutex_exit(&softsp->dma_pool_lock);
			DPRINTF(IOMMU_BYPASS_RESERVE,
			    ("Reserve: dma_reserve is exhausted\n"));
			return (DDI_DMA_NORESOURCES);
		}

		softsp->dma_reserve -= np;
		mp = (ddi_dma_impl_t *) kmem_fast_zalloc(&softsp->dmaimplbase,
		    sizeof (*mp), 2, KM_SLEEP);
		mutex_exit(&softsp->dma_pool_lock);

		ASSERT(mp);
		mp->dmai_rflags = DMP_BYPASSNEXUS;
		mp->dmai_rdip = rdip;
		mp->dmai_minxfer = dma_lim->dlim_minxfer;
		mp->dmai_burstsizes = dma_lim->dlim_burstsizes;

		ioaddr = getdvmapages(np, ALO, AHI, (u_int)-1,
				dma_lim->dlim_cntr_max,
				(dmareq->dmar_fp == DDI_DMA_SLEEP)? 1 : 0,
				softsp->dvmamap);

		if (ioaddr == 0) {
			mutex_enter(&softsp->dma_pool_lock);
			softsp->dma_reserve += np;
			kmem_fast_free(&softsp->dmaimplbase, (caddr_t)mp);
			mutex_exit(&softsp->dma_pool_lock);
			DPRINTF(IOMMU_BYPASS_RESERVE,
			    ("Reserve: No dvma resources available\n"));
			return (DDI_DMA_NOMAPPING);
		}

		/* create a per request structure */
		iommu_fast_dvma = (struct fast_dvma *) kmem_alloc(
		    sizeof (struct fast_dvma), KM_SLEEP);

		/*
		 * We need to remember the size of the transfer so that
		 * we can figure the virtual pages to sync when the transfer
		 * is complete.
		 */
		iommu_fast_dvma->pagecnt = (u_int *) kmem_zalloc(np *
		    sizeof (u_int), KM_SLEEP);

		mp->dmai_mapping = ioaddr;
		mp->dmai_ndvmapages = np;
		iommu_fast_dvma->ops = &iommu_dvma_ops;
		iommu_fast_dvma->softsp = (caddr_t)softsp;
		mp->dmai_nexus_private = (caddr_t)iommu_fast_dvma;
		handlep = (ddi_dma_handle_t *) objp;
		*handlep = (ddi_dma_handle_t) mp;

		DPRINTF(IOMMU_BYPASS_RESERVE,
		    ("Reserve: Base addr 0x%x, size 0x%x\n", mp->dmai_mapping,
		    mp->dmai_ndvmapages));

		break;
	}
	case DDI_DMA_RELEASE:
	{
		ddi_dma_impl_t *mp = (ddi_dma_impl_t *)handle;
		u_int np = npages = mp->dmai_ndvmapages;
		u_long ioaddr = mp->dmai_mapping;
		u_ll_t *iotte_ndx;
		struct fast_dvma *iommu_fast_dvma = (struct fast_dvma *)
		    mp->dmai_nexus_private;

		/* Unload stale mappings and flush stale tlb's */
		iotte_ndx = IOTTE_NDX(ioaddr, softsp->soft_tsb_base_addr);

		while (npages > (u_int) 0) {
			iommu_tteunload(iotte_ndx);
			iommu_tlb_flush(softsp, ioaddr);

			npages--;
			iotte_ndx++;
			ioaddr += IOMMU_PAGESIZE;
		}

		ioaddr = mp->dmai_mapping;
		mutex_enter(&softsp->dma_pool_lock);
		kmem_fast_free(&softsp->dmaimplbase, (caddr_t)mp);
		softsp->dma_reserve += np;
		mutex_exit(&softsp->dma_pool_lock);

		putdvmapages(ioaddr, np, softsp->dvmamap);

		kmem_free(iommu_fast_dvma->pagecnt, np * sizeof (u_int));
		kmem_free(iommu_fast_dvma, sizeof (struct fast_dvma));


		DPRINTF(IOMMU_BYPASS_RESERVE,
		    ("Release: Base addr 0x%x, size 0x%x\n", ioaddr, np));
		/*
		 * Now that we've freed some resource,
		 * if there is anybody waiting for it
		 * try and get them going.
		 */
		if (softsp->dvma_call_list_id != 0) {
			ddi_run_callback(&softsp->dvma_call_list_id);
		}

		break;
	}

	default:
		DPRINTF(IOMMU_DMAMCTL_DEBUG, ("iommu_dma_mctl: unknown option "
		    "0%x\n", request));

		return (DDI_FAILURE);
	}
	return (DDI_SUCCESS);
}

/*ARGSUSED*/
void
iommu_dvma_kaddr_load(ddi_dma_handle_t h, caddr_t a, u_int len, u_int index,
    ddi_dma_cookie_t *cp)
{

	u_long ioaddr, addr, offset;
	u_int pfn;
	int npages;
	u_ll_t *iotte_ndx;
	u_ll_t iotte_flag = 0;
	struct as *as = 0;
	extern struct as kas;
	register ddi_dma_impl_t *mp = (ddi_dma_impl_t *)h;
	extern u_int sfmmu_getpfnum(struct as *, caddr_t);
	struct fast_dvma *iommu_fast_dvma =
	    (struct fast_dvma *)mp->dmai_nexus_private;
	struct sbus_soft_state *softsp = (struct sbus_soft_state *)
	    iommu_fast_dvma->softsp;

#if	defined(DEBUG) && defined(IO_MEMUSAGE)
	struct io_mem_list *iomemp;
	u_int *pfnp;
#endif /* DEBUG && IO_MEMUSAGE */

	addr = (u_long)a;
	ioaddr =  mp->dmai_mapping + iommu_ptob(index);
	offset = addr & IOMMU_PAGEOFFSET;
	iommu_fast_dvma->pagecnt[index] = iommu_btopr(len + offset);
	as = &kas;
	addr &= ~IOMMU_PAGEOFFSET;
	npages = iommu_btopr(len + offset);

#if	defined(DEBUG) && defined(IO_MEMUSAGE)
	iomemp = (struct io_mem_list *) kmem_alloc(sizeof (struct io_mem_list),
	    KM_SLEEP);
	iomemp->rdip = mp->dmai_rdip;
	iomemp->ioaddr = ioaddr;
	iomemp->addr = addr;
	iomemp->npages = npages;
	pfnp = iomemp->pfn = (u_int *) kmem_zalloc(
	    sizeof (u_int) * (npages + 1), KM_SLEEP);
#endif /* DEBUG && IO_MEMUSAGE */

	cp->dmac_address = ioaddr | offset;
	cp->dmac_size = len;

	iotte_ndx = IOTTE_NDX(ioaddr, softsp->soft_tsb_base_addr);
	/* read/write and streaming io on */
	iotte_flag = IOTTE_WRITE | IOTTE_CACHE | IOTTE_STREAM;

	DPRINTF(IOMMU_BYPASS_LOAD, ("kaddr_load: ioaddr 0x%x, "
	    "size 0x%x, offset 0x%x, index 0x%x, kernel addr 0x%x\n",
	    ioaddr, len, offset, index, addr));
	while (npages > 0) {

		if ((pfn = sfmmu_getpfnum(as, (caddr_t) addr)) == (u_int) -1) {
#ifdef lint
			h = h;
#endif /* lint */
			DPRINTF(IOMMU_BYPASS_LOAD, ("kaddr_load: invalid pfn "
			    "from sfmmu_getpfnum()\n"));
		}

		iommu_tlb_flush(softsp, ioaddr);

		iommu_tteload(iotte_ndx, pfn, iotte_flag);

		npages--;
		iotte_ndx++;

		addr += IOMMU_PAGESIZE;
		ioaddr += IOMMU_PAGESIZE;

#if	defined(DEBUG) && defined(IO_MEMUSAGE)
		*pfnp = pfn;
		pfnp++;
#endif /* DEBUG && IO_MEMUSAGE */

	}

#if	defined(DEBUG) && defined(IO_MEMUSAGE)
	mutex_enter(&softsp->iomemlock);
	iomemp->next = softsp->iomem;
	softsp->iomem = iomemp;
	mutex_exit(&softsp->iomemlock);
#endif /* DEBUG && IO_MEMUSAGE */

}

/*ARGSUSED*/
void
iommu_dvma_unload(ddi_dma_handle_t h, u_int index, u_int view)
{
	register ddi_dma_impl_t *mp = (ddi_dma_impl_t *)h;
	register u_long ioaddr;
	u_int	npages;
	struct fast_dvma *iommu_fast_dvma =
	    (struct fast_dvma *)mp->dmai_nexus_private;
	struct sbus_soft_state *softsp = (struct sbus_soft_state *)
	    iommu_fast_dvma->softsp;

#if	defined(DEBUG) && defined(IO_MEMUSAGE)
	struct io_mem_list **prevp, *walk;
#endif /* DEBUG && IO_MEMUSAGE */


#if	defined(DEBUG) && defined(IO_MEMUSAGE)
	ioaddr =  mp->dmai_mapping + iommu_ptob(index);
	npages = iommu_fast_dvma->pagecnt[index];
	mutex_enter(&softsp->iomemlock);
	prevp = &softsp->iomem;
	walk = softsp->iomem;

	while (walk) {
		if (walk->ioaddr == ioaddr) {
			*prevp = walk->next;
			break;
		}

		prevp = &walk->next;
		walk = walk->next;
	}
	mutex_exit(&softsp->iomemlock);

	kmem_free(walk->pfn, sizeof (u_int) * (npages + 1));
	kmem_free(walk, sizeof (struct io_mem_list));
#endif /* DEBUG && IO_MEMUSAGE */

	ioaddr =  mp->dmai_mapping + iommu_ptob(index);
	npages = iommu_fast_dvma->pagecnt[index];

	sync_stream_buf(softsp, ioaddr, iommu_ptob(npages));
}

/*ARGSUSED*/
void
iommu_dvma_sync(ddi_dma_handle_t h, u_int index, u_int view)
{
	register ddi_dma_impl_t *mp = (ddi_dma_impl_t *)h;
	register u_long ioaddr;
	u_int   npages;
	struct fast_dvma *iommu_fast_dvma =
	    (struct fast_dvma *)mp->dmai_nexus_private;
	struct sbus_soft_state *softsp = (struct sbus_soft_state *)
	    iommu_fast_dvma->softsp;

	if (view == DDI_DMA_SYNC_FORKERNEL ||
	    view == DDI_DMA_SYNC_FORDEV) {
		ioaddr =  mp->dmai_mapping + iommu_ptob(index);
		npages = iommu_fast_dvma->pagecnt[index];
		sync_stream_buf(softsp, ioaddr, iommu_ptob(npages));
	}
}
