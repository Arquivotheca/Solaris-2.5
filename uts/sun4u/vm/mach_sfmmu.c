/*
 * Copyright (c) 1992, 1993 by Sun Microsystems, Inc.
 */

#pragma ident   "@(#)mach_sfmmu.c 1.69     95/09/21 SMI"

#include <sys/types.h>
#include <vm/hat.h>
#include <vm/hat_sfmmu.h>
#include <vm/page.h>
#include <sys/pte.h>
#include <sys/systm.h>
#include <sys/mman.h>
#include <sys/sysmacros.h>
#include <sys/machparam.h>
#include <sys/vtrace.h>
#include <sys/kmem.h>
#include <sys/mmu.h>
#include <sys/cmn_err.h>
#include <sys/cpu.h>
#include <sys/cpuvar.h>
#include <sys/debug.h>
#include <sys/archsystm.h>
#include <sys/machsystm.h>
#include <vm/as.h>
#include <vm/seg.h>
#include <vm/devpage.h>
#include <vm/seg_kp.h>
#include <vm/rm.h>
#include <sys/t_lock.h>
#include <sys/vm_machparam.h>
#include <sys/promif.h>
#include <sys/prom_plat.h>
#include <sys/prom_debug.h>
#include <sys/privregs.h>
#include <sys/bootconf.h>

/*
 * Static routines
 */
static void	sfmmu_remap_kernel();
static void	sfmmu_set_tlb();
static void	sfmmu_map_prom_mappings(struct translation *);
static struct translation *
		read_prom_mappings();

static void	sfmmu_map_prom_mappings(struct translation *);

static struct tsbe *
		sfmmu_get_tsbp(caddr_t, int, int);

/*
 * Global Data:
 */
caddr_t	textva, datava;
tte_t	ktext_tte, kdata_tte;		/* ttes for kernel text and data */

char *hat_panic1 = "remap_kernel: didn't find immu match";
char *hat_panic2 = "hmeblk_cnt: decrement and already 0";
char *hat_panic3 = "hmeblk_cnt: increment and overflowed";
char *hat_panic4 = "ctx_ttecnt_dec: decrement and already 0";
char *hat_panic5 = "ctx_ttecnt_inc: overflowed";
char *hat_panic6 = "sfmmu_ktsb_miss: hmeblk ctx not kernel";
char *hat_panic7 = "sfmmu_tlbflush_page: interrupts already disabled";
char *hat_panic8 = "mmu_update_tsbe: interrupts already disabled";
char *hat_panic9 = "sfmmu_lock_kernel: interrupts already disabled";

/*
 * XXX Need to understand how crash uses this routine and get rid of it
 * if possible.
 */
void
mmu_setctx(struct ctx *ctx)
{
#ifdef lint
	ctx = ctx;
#endif /* lint */

	STUB(mmu_setctx);
}

/*
 * This function will load the TSB with the tte in the passed hment.
 * The TSB locking is managed as follows:
 * Since the global bit is not being used we will use it to represent an
 * invalid TTE.  A cpu who wants to modify a tte entry will first the highest
 * most byte of the tte to 0xFF.  This will cause the tag compare to fail
 * for all those cpus reading the tte.  The cpu who holds the lock can then
 * modify the data side, and the tag side.  The last write should be to the
 * word containing the lock bit which will clear the lock and allow the tte
 * to be read.  It is assumed that all cpus reading the tsb will do so with
 * atomic 128-bit loads.
 * XXX Eventually we want to xor the (context << 4) with the tsbptr to spread
 * the tte entries.
 * XXX when we add ctx stealing support we need to make sure ctxnum is valid.
 */
void
sfmmu_load_tsb(caddr_t addr, int ctxnum, tte_t *ttep)
{
	struct tsbe *tsbp;
	union tsb_tag tag;

	ASSERT(ctxnum != INVALID_CONTEXT);
	tag.tag_inthi = 0;
	tag.tag_intlo = 0;
	tsbp = sfmmu_get_tsbp(addr, ctxnum, ttep->tte_size);
	tag.tag_ctxnum = ctxnum;
	/* We will need to set vahi when we go to 64 bits */
	tag.tag_valo = (u_int)addr >> TAG_VALO_SHIFT;
	mmu_update_tsbe(tsbp, ttep, &tag);
}

void
sfmmu_unload_tsb(caddr_t addr, int ctxnum, int size)
{
	struct tsbe *tsbp;
	union tsb_tag tsbtag;

	tsbp = sfmmu_get_tsbp(addr, ctxnum, size);
	tsbtag.tag_inthi = 0;
	tsbtag.tag_ctxnum = ctxnum;
	tsbtag.tag_valo = (u_int)addr >> TAG_VALO_SHIFT;
	mmu_invalidate_tsbe(tsbp, &tsbtag);
}

/*
 * Function that calculates the tte pointer for a global shared TSB with
 * a TSB_SIZE_CODE.  It performs the same function the hardware uses
 * to calculate the I/D TSB 8k/64k Direct Pointer Registers.  It expects
 * the vaddr of the missing virtual address, and the tte sz.
 */
static
struct tsbe *
sfmmu_get_tsbp(caddr_t vaddr, int ctxnum, int sz)
{
	extern caddr_t tsb_base;
	uint offset;

	offset = ((((uint) vaddr >> TTE_PAGE_SHIFT(sz)) & TSB_OFFSET_MASK)
		^ (ctxnum << TSB_CTX_SHIFT)) << TSB_ENTRY_SHIFT;
	return ((struct tsbe *)((uint)tsb_base | offset));
}

/*
 * Global Routines called from within:
 *	usr/src/uts/sun4u
 *	usr/src/uts/sfmmu
 *	usr/src/uts/sun
 */

u_int
va_to_pfn(caddr_t vaddr)
{
	extern u_int tba_taken_over;
	u_int phys_hi, phys_lo;
	int mode, valid;

	if (!tba_taken_over) {
		if (prom_translate_virt(vaddr, &valid, &phys_hi,
			&phys_lo, &mode) != -1) {
			if (valid == -1)
				return (btop(((u_longlong_t)phys_hi <<
				(32 - MMU_PAGESHIFT) | (u_longlong_t)phys_lo)));
			else
				return ((uint)-1);
		} else {
			return ((uint)-1);
		}
	} else {
		return (sfmmu_vatopfn(vaddr, KHATID));
	}
}

u_longlong_t
va_to_pa(caddr_t vaddr)
{
	uint pfn;

	if ((pfn = va_to_pfn(vaddr)) == -1)
		return ((u_longlong_t)-1);
	return ((pfn << MMU_PAGESHIFT) | ((uint)vaddr & MMU_PAGEOFFSET));
}

void
hat_kern_setup(void)
{
	extern	caddr_t tsb_base;
	extern	int hblkalloc_inprog;

	struct translation *trans_root;

	/*
	 * These are the steps we take to take over the mmu from the prom.
	 *
	 * (1)	Read the prom's mappings through the translation property.
	 * (2)	Remap the kernel text and kernel data with 2 locked 4MB ttes.
	 *	Create the the hmeblks for these 2 ttes at this time.
	 * (3)	Create hat structures for all other prom mappings.  Since the
	 *	kernel text and data hme_blks have already been created we
	 *	skip the equivalent prom's mappings.
	 * (4)	Initialize the tsb and its corresponding hardware regs.
	 * (5)	Take over the trap table (currently in startup).
	 * (6)	Up to this point it is possible the prom required some of its
	 *	locked tte's.  Now that we own the trap table we remove them.
	 */
	hblkalloc_inprog = 1;	/* make sure hmeblks come from nucleus */
	trans_root = read_prom_mappings();
	sfmmu_remap_kernel();
	sfmmu_map_prom_mappings(trans_root);
	sfmmu_init_tsb(tsb_base, TSB_BYTES);
	mmu_set_itsb(tsb_base, TSB_SPLIT_CODE, TSB_SIZE_CODE);
	mmu_set_dtsb(tsb_base, TSB_SPLIT_CODE, TSB_SIZE_CODE);
	hblkalloc_inprog = 0;
}

/*
 * This routine remaps the kernel using large ttes
 * All entries except locked ones will be removed from the tlb.
 * It assumes that both the text and data segments reside in a separate
 * 4mb virtual and physical contigous memory chunk.  This routine
 * is only executed by the first cpu.  The remaining cpus execute
 * sfmmu_mp_startup() instead.
 * XXX It assumes that the start of the text segment is KERNELBASE.  It should
 * actually be based on start.
 */
static void
sfmmu_remap_kernel()
{
	u_int	pfn;
	u_int	prot;
	int	flags;

	extern char end[];
	extern struct as kas;

	textva = (caddr_t)(KERNELBASE & MMU_PAGEMASK4M);
	pfn = va_to_pfn(textva);
	if ((int)pfn == -1)
		prom_panic("can't find kernel text pfn");
	pfn &= TTE_PFNMASK(TTE4M);

#ifdef NOTYET
	/*
	 * OBP needs to change text mapping to writable before setting
	 * breakpoints.  Until this support is added map kernel writable.
	 */
	prot = PROC_TEXT;
#else
	prot = PROC_DATA;
#endif /* NOTYET */
	flags = HAT_LOCK | HAT_NOSYNCLOAD | SFMMU_NO_TSBLOAD;
	sfmmu_memtte(&ktext_tte, pfn, prot, flags, TTE4M);
	/*
	 * We set the lock bit in the tte to lock the translation in
	 * the tlb.
	 */
	ktext_tte.tte_lock = 1;
	sfmmu_tteload(kas.a_hat, &ktext_tte, textva, (struct page *)NULL,
		flags);

	datava = (caddr_t)((u_int)end & MMU_PAGEMASK4M);
	pfn = va_to_pfn(datava);
	if ((int)pfn == -1)
		prom_panic("can't find kernel data pfn");
	pfn &= TTE_PFNMASK(TTE4M);

	prot = PROC_DATA;
	sfmmu_memtte(&kdata_tte, pfn, prot, flags, TTE4M);
	/*
	 * We set the lock bit in the tte to lock the translation in
	 * the tlb.  We also set the mod bit to avoid taking dirty bit
	 * traps on kernel data.
	 */
	TTE_SET_LOFLAGS(&kdata_tte, TTE_LCK_INT | TTE_HWWR_INT,
		TTE_LCK_INT | TTE_HWWR_INT);
	sfmmu_tteload(kas.a_hat, &kdata_tte, datava, (struct page *)NULL,
		flags);

	sfmmu_set_tlb();
}

/*
 * Setup the kernel's locked tte's
 */
static void
sfmmu_set_tlb()
{
	dnode_t node;
	u_int len, index;

	node = cpunodes[getprocessorid()].nodeid;
	len = prom_getprop(node, "#itlb-entries", (caddr_t)&index);
	if (len != sizeof (index))
		prom_panic("bad #itlb-entries property");
	prom_itlb_load(index - 1, *(u_longlong_t *)&ktext_tte, textva);
	len = prom_getprop(node, "#dtlb-entries", (caddr_t)&index);
	if (len != sizeof (index))
		prom_panic("bad #dtlb-entries property");
	prom_dtlb_load(index - 1, *(u_longlong_t *)&kdata_tte, datava);
	prom_dtlb_load(index - 2, *(u_longlong_t *)&ktext_tte, textva);
}

/*
 * This routine is executed by all other cpus except the first one
 * at initialization time.  It is responsible for taking over the
 * mmu from the prom.  We follow these steps.
 * Lock the kernel's ttes in the TLB
 * Initialize the tsb hardware registers
 * Take over the trap table
 * Flush the prom's locked entries from the TLB
 */
void
sfmmu_mp_startup()
{
	extern struct scb trap_table;
	extern void setwstate(u_int);
	extern void install_va_to_tte();

	sfmmu_set_tlb();
	mmu_set_itsb(tsb_base, TSB_SPLIT_CODE, TSB_SIZE_CODE);
	mmu_set_dtsb(tsb_base, TSB_SPLIT_CODE, TSB_SIZE_CODE);
	setwstate(WSTATE_KERN);
	prom_set_traptable((caddr_t)&trap_table);
	install_va_to_tte();
	obp_remove_locked_ttes();
}

/*
 * This function traverses the prom mapping list and creates equivalent
 * mappings in the sfmmu mapping hash.
 */
static void
sfmmu_map_prom_mappings(struct translation *trans_root)
{
	struct translation *promt;
	tte_t tte, *ttep;
	u_int offset;
	u_int pfn, oldpfn, basepfn;
	u_int vaddr;
	int size;
	u_int prot = PROC_DATA;		/* can we change this to PROC_TEXT? */
	int flags;
	struct page *pp;
	extern int pf_is_memory(u_int);
#ifdef DEBUG
	extern struct memlist *virt_avail;
#endif /* DEBUG */

	if (trans_root == NULL) {
		prom_panic("No translation property!");
	}
	ttep = &tte;
	for (promt = trans_root; promt && promt->tte_hi; promt++) {
		/* hack until we get rid of map-for-unix */
		if (promt->virt_lo < KERNELBASE)
			continue;
		ttep->tte_inthi = promt->tte_hi;
		ttep->tte_intlo = promt->tte_lo;
		flags = HAT_LOCK | HAT_NOSYNCLOAD | SFMMU_NO_TSBLOAD;
		if (TTE_IS_LOCKED(ttep)) {
			if (!TTE_IS_GLOBAL(ttep)) {
				/*
				 * XXX get rid of this panic if I don't
				 * depend on it.
				 */
				prom_panic("locked tte but is not global");
			}
			/* clear the lock and global bits */
			TTE_SET_LOFLAGS(ttep, TTE_LCK_INT | TTE_GLB_INT, 0);
		}
		if (!TTE_IS_VCACHEABLE(ttep)) {
			flags |= SFMMU_UNCACHEVTTE;
		}
		if (!TTE_IS_PCACHEABLE(ttep)) {
			flags |= SFMMU_UNCACHEPTTE;
		}
		if (TTE_IS_SIDEFFECT(ttep)) {
			flags |= SFMMU_SIDEFFECT;
		}

		/*
		 * Since this is still just a 32 bit machine ignore
		 * virth_hi and size_hi
		 */
		size = promt->size_lo;
		offset = 0;
		basepfn = TTE_TO_PFN((caddr_t)promt->virt_lo, ttep);
		while (size) {
			vaddr = promt->virt_lo + offset;
			/*
			 * make sure address is not in virt-avail list
			 */
			ASSERT(!address_in_memlist(virt_avail, (caddr_t)vaddr,
				size));

			pfn = basepfn + mmu_btop(offset);
			if (pf_is_memory(pfn)) {
				if (flags & SFMMU_UNCACHEPTTE) {
					cmn_err(CE_PANIC,
						"prom mapped mem uncached");
				}
			} else {
				if (!(flags & SFMMU_SIDEFFECT)) {
					cmn_err(CE_PANIC,
						"prom mapped io with no e");
				}
			}
			pp = page_numtopp_nolock(pfn);
			if ((oldpfn = sfmmu_vatopfn((caddr_t)vaddr, KHATID))
				!= -1) {
				/*
				 * mapping already exists.
				 * Verify they are equal
				 */
				if (pfn != oldpfn) {
					prom_panic("map_prom: conflict!");
				}
				size -= MMU_PAGESIZE;
				offset += MMU_PAGESIZE;
				continue;
			}
			ASSERT(pp == NULL || pp->p_free == 0);
			if (!pp && size >= MMU_PAGESIZE4M &&
			    !(vaddr & MMU_PAGEOFFSET4M) &&
			    !(mmu_ptob(pfn) & MMU_PAGEOFFSET4M)) {
				sfmmu_memtte(ttep, pfn, prot, flags, TTE4M);
				sfmmu_tteload(kas.a_hat, ttep, (caddr_t)vaddr,
					pp, flags);
				size -= MMU_PAGESIZE4M;
				offset += MMU_PAGESIZE4M;
				continue;
			}
#ifdef NOTYET
	/*
	 * OBP needs to support large page size before we can reenable this.
	 */
			if (!pp && size >= MMU_PAGESIZE512K &&
			    !(vaddr & MMU_PAGEOFFSET512K) &&
			    !(mmu_ptob(pfn) & MMU_PAGEOFFSET512K)) {
				sfmmu_memtte(ttep, pfn, prot, flags, TTE512K);
				sfmmu_tteload(kas.a_hat, ttep, (caddr_t)vaddr,
					pp, flags);
				size -= MMU_PAGESIZE512K;
				offset += MMU_PAGESIZE512K;
				continue;
			}
			if (!pp && size >= MMU_PAGESIZE64K &&
			    !(vaddr & MMU_PAGEOFFSET64K) &&
			    !(mmu_ptob(pfn) & MMU_PAGEOFFSET64K)) {
				sfmmu_memtte(ttep, pfn, prot, flags, TTE64K);
				sfmmu_tteload(kas.a_hat, ttep, (caddr_t)vaddr,
					pp, flags);
				size -= MMU_PAGESIZE64K;
				offset += MMU_PAGESIZE64K;
				continue;
			}
#endif /* NOTYET */
			sfmmu_memtte(ttep, pfn, prot, flags, TTE8K);
			sfmmu_tteload(kas.a_hat, ttep, (caddr_t)vaddr,
				pp, flags);
			size -= MMU_PAGESIZE;
			offset += MMU_PAGESIZE;
		}
	}
}

/*
 * This routine reads in the "translations" property in to a buffer and
 * returns a pointer to this buffer
 */
static struct translation *
read_prom_mappings(void)
{
	char *prop = "translations";
	int translen;
	extern caddr_t tsb_base;
	dnode_t node;

	/*
	 * the "translations" property is associated with the mmu node
	 */
	node = (dnode_t)prom_getphandle(prom_mmu_ihandle());

	/*
	 * We use the TSB space to read in the prom mappings.  This space
	 * is currently not being used because we haven't taken over the
	 * trap table yet.  It should be big enough to hold the mappings
	 * (ie. 512k).
	 */
	if ((translen = prom_getproplen(node, prop)) == -1)
		cmn_err(CE_PANIC, "no translations property");
	translen = roundup(translen, MMU_PAGESIZE);
	PRM_DEBUG(translen);
	if (translen > TSB_BYTES)
		cmn_err(CE_PANIC, "not enough space for translations");

	if (prom_getprop(node, prop, tsb_base) == -1)
		cmn_err(CE_PANIC, "translations getprop failed");

	return ((struct translation *)tsb_base);
}

int	remove_obp_ttes;

/*
 * This routine traverses the tlb, and unlocks the prom's tte entries.
 * It knows that the only kernel locked tte's are one for kernel text and
 * one for kernel data.
 */
void
obp_remove_locked_ttes()
{
	int i, ctxnum;
	tte_t tte;
	caddr_t addr;
	extern caddr_t sdata;
	extern int itlb_entries;
	extern int dtlb_entries;

	if (!remove_obp_ttes) {
		/*
		 * If we are debugging early startup - do not remove prom
		 * translations.
		 */
		return;
	}


	for (i = 0; i < itlb_entries; i++) {
		itlb_rd_entry(i, &tte, &addr, &ctxnum);
		if (TTE_IS_VALID(&tte) && TTE_IS_LOCKED(&tte)) {
			ASSERT(ctxnum == KCONTEXT);
			if ((addr != sdata) && (addr != (caddr_t)KERNELBASE)) {
				MMU_TLBFLUSH_PAGE(addr, ctxnum);
			}
		}
	}
	for (i = 0; i < dtlb_entries; i++) {
		dtlb_rd_entry(i, &tte, &addr, &ctxnum);
		if (TTE_IS_VALID(&tte) && TTE_IS_LOCKED(&tte)) {
			ASSERT(ctxnum == KCONTEXT);
			if ((addr != sdata) && (addr != (caddr_t)KERNELBASE)) {
				MMU_TLBFLUSH_PAGE(addr, ctxnum);
			}
		}
	}
}

/*
 * Allocate hat structs from the nucleus data memory.
 */
caddr_t
ndata_alloc_hat(caddr_t hat_alloc_base, caddr_t nalloc_end, int npages)
{
	u_int 			hmehash_sz, ctx_sz;
	int			thmehash_num, nhblks, wanted_hblks, max_hblks;
	int			wanted_hblksz = 0;
	caddr_t			wanted_endva, nextra_base, nextra_end;
	int			nextra_size = 0;

	extern caddr_t		tsb_base;
	extern caddr_t		tsb_end;
	extern int		uhmehash_num;
	extern int		khmehash_num;
	extern struct hmehash_bucket	*uhme_hash;
	extern struct hmehash_bucket	*khme_hash;
	extern int		sfmmu_add_nmlist();
	extern int		highbit(u_long);
	extern int		ecache_linesize;

	PRM_DEBUG(npages);

	/*
	 * We first allocate the TSB by finding the first correctly
	 * aligned chunk of nucleus memory.
	 */
	tsb_base = (caddr_t)roundup((uint)hat_alloc_base, TSB_BYTES);
	ASSERT(!((uint)tsb_base & (TSB_BYTES - 1)));
	nextra_base = hat_alloc_base;
	nextra_size = tsb_base - nextra_base;
	hat_alloc_base = tsb_base + TSB_BYTES;
	tsb_end = hat_alloc_base - sizeof (struct tsbe);
	nextra_end = tsb_base;
	PRM_DEBUG(tsb_base);
	PRM_DEBUG(TSB_BYTES);
	PRM_DEBUG(TSB_OFFSET_MASK);
	PRM_DEBUG(nextra_base);
	PRM_DEBUG(nextra_size);

	ASSERT(hat_alloc_base < nalloc_end);

	/*
	 * Allocate ctx structures
	 *
	 * based on v_proc to calculate how many ctx structures
	 * is not possible;
	 * use whatever module_setup() assigned to nctxs
	 */
	PRM_DEBUG(nctxs);
	ctx_sz = nctxs * sizeof (struct ctx);
	if (nextra_size >= ctx_sz) {
		ctxs = (struct ctx *)nextra_base;
		nextra_base += ctx_sz;
		nextra_base = (caddr_t)roundup((u_int)nextra_base,
			ecache_linesize);
		nextra_size = nextra_end - nextra_base;
	} else {
		ctxs = (struct ctx *)hat_alloc_base;
		hat_alloc_base += ctx_sz;
		hat_alloc_base = (caddr_t)roundup((u_int)hat_alloc_base,
			ecache_linesize);
	}
	PRM_DEBUG(ctxs);
	ASSERT(hat_alloc_base < nalloc_end);

	if (nextra_size) {
		nhblks = sfmmu_add_nmlist(nextra_base, nextra_size);
	}
	PRM_DEBUG(nhblks);

	/*
	 * The number of buckets in the hme hash tables
	 * is a power of 2 such that the average hash chain length is
	 * HMENT_HASHAVELEN.  The number of buckets for the user hash is
	 * a function of physical memory and a predefined overmapping factor.
	 * The number of buckets for the kernel hash is a function of
	 * KERNELSIZE.
	 */
	uhmehash_num = (npages * HMEHASH_FACTOR) /
		(HMENT_HASHAVELEN * (HMEBLK_SPAN(TTE8K) >> MMU_PAGESHIFT));
	uhmehash_num = 1 << highbit(uhmehash_num - 1);
	uhmehash_num = min(uhmehash_num, MAX_UHME_BUCKETS);
	khmehash_num = KERNELSIZE /
		(HMENT_HASHAVELEN * HMEBLK_SPAN(TTE8K));
	khmehash_num = 1 << highbit(khmehash_num - 1);
	thmehash_num = uhmehash_num + khmehash_num;
	hmehash_sz = thmehash_num * sizeof (struct hmehash_bucket);
	khme_hash = (struct hmehash_bucket *)hat_alloc_base;
	uhme_hash = (struct hmehash_bucket *)((caddr_t)khme_hash +
		khmehash_num * sizeof (struct hmehash_bucket));
	hat_alloc_base += hmehash_sz;
	hat_alloc_base = (caddr_t)roundup((u_int)hat_alloc_base,
		ecache_linesize);
	PRM_DEBUG(khme_hash);
	PRM_DEBUG(khmehash_num);
	PRM_DEBUG(uhme_hash);
	PRM_DEBUG(uhmehash_num);
	PRM_DEBUG(hmehash_sz);
	PRM_DEBUG(hat_alloc_base);

	/*
	 * Allocate nucleus hme_blks
	 * We only use hme_blks out of the nucleus pool when we are mapping
	 * other hme_blks.  The absolute worse case if we were to use all of
	 * physical memory for hme_blks so we allocate enough nucleus
	 * hme_blks to map all of physical memory.  This is real overkill
	 * so might want to divide it by a certain factor.
	 * RFE: notice that I will only allocate as many hmeblks as
	 * there is space in the nucleus.  We should add a check at the
	 * end of sfmmu_tteload to check how many "nucleus" hmeblks we have.
	 * If we go below a certain threshold we kmem alloc more.  The
	 * "nucleus" hmeblks need not be part of the nuclues.  They just
	 * need to be preallocated to avoid the recursion on kmem alloc'ed
	 * hmeblks.
	 */
	wanted_hblks = npages / (HMEBLK_SPAN(TTE8K) >> MMU_PAGESHIFT);
	PRM_DEBUG(wanted_hblks);
	wanted_hblks -= nhblks;
	if (wanted_hblks > 0) {
		max_hblks = ((uint)nalloc_end - (uint)hat_alloc_base) /
			HME8BLK_SZ;
		wanted_hblks = min(wanted_hblks, max_hblks);
		PRM_DEBUG(wanted_hblks);
		wanted_hblksz = wanted_hblks * HME8BLK_SZ;
		wanted_endva = (caddr_t)roundup((uint)hat_alloc_base +
			wanted_hblksz, MMU_PAGESIZE);
		wanted_hblksz = wanted_endva - hat_alloc_base;
		nhblks = sfmmu_add_nmlist(hat_alloc_base, wanted_hblksz);
		PRM_DEBUG(wanted_hblksz);
		PRM_DEBUG(nhblks);
		hat_alloc_base += wanted_hblksz;
		ASSERT(!((u_int)hat_alloc_base & MMU_PAGEOFFSET));
	}
	ASSERT(hat_alloc_base <= nalloc_end);
	PRM_DEBUG(hat_alloc_base);
	PRM_DEBUG(HME8BLK_SZ);
	return (hat_alloc_base);
}

void
sfmmu_panic(struct regs *rp)
{
	printf("sfmmu_bad_trap rp = 0x%x\n", rp);
	cmn_err(CE_PANIC, "sfmmu_bad_trap rp = 0x%x\n", rp);
}
