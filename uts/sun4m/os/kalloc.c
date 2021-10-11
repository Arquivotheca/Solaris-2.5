/*
 * Copyright (c) 1990-1992, 1993 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)kalloc.c	1.10	95/01/16 SMI"

#include <sys/t_lock.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/vm.h>
#include <sys/map.h>
#include <sys/mman.h>
#include <sys/kmem.h>
#include <sys/cmn_err.h>
#include <sys/cpu.h>
#include <vm/as.h>
#include <vm/page.h>
#include <vm/seg_kmem.h>

extern int do_pg_coloring;

/*
 * Global Data:
 */

/*
 * Static Routines:
 */

static caddr_t	kncalloc(u_int nbytes);
static void	kncfree(caddr_t addr, u_int nbytes);

/*
 * Kernel No-cache list allocations
 */

struct knclist {
	struct knclist	*knc_next;	/* next in a list */
	struct page	*knc_pp;	/* page list for us */
	u_long		knc_addr;	/* base kernel virtual */
	u_long		knc_size;	/* size of space (bytes) */
	int		knc_nmapent;	/* number of map entries in map */
	struct map	*knc_map;	/* map we're using */
	char 		knc_mname[8];	/* name of this map */
};

static int kncnbytes = 0;
static struct knclist *knclist;
static kmutex_t	knc_lock;		/* mutex protecting knclist data */
static int kcadebug = 0;
static int kcused, kctotasked, kctotused, kncused, knctotasked, knctotused;

void
kncinit()
{
	mutex_init(&knc_lock, "knc_lock", MUTEX_DEFAULT, DEFAULT_WT);
}

static caddr_t
kncalloc(u_int nbytes)
{
	register struct page *pp;
	register struct knclist *kp;
	int nparts, npages;
	u_long a, addr;


	addr = 0;
	mutex_enter(&knc_lock);
	for (kp = knclist; kp != 0; kp = kp->knc_next) {
		if (addr = rmalloc(kp->knc_map, nbytes))
			break;
	}
	mutex_exit(&knc_lock);
	if (addr) {
		if (kcadebug) {
			printf("kncalloc: alloc addr 0x%x size %d\n",
			    addr, nbytes);
		}
		return ((caddr_t) addr);
	}

	/*
	 * Not enough space in any knc map segment. Make a
	 * new segment big enough to cover the request.
	 */

	/*
	 * Calculate how many pages we'll need...
	 */
	npages = mmu_btopr((int) nbytes);

	/*
	 * Get some non-cached kernel virtual addresses to cover this request.
	 */
	a = rmalloc_wait(kernelmap, npages);
	addr = (u_long)kmxtob(a);

	/*
	 * ...and get them
	 */
	pp = page_create_va(&kvp, (u_int)addr, (u_int)mmu_ptob(npages),
			PG_EXCL | PG_WAIT, &kas, (caddr_t)addr);

	/*
	 * Update global information about available memory.
	 */
	mutex_enter(&freemem_lock);
	availrmem -= npages;
	pages_pp_kernel += npages;
	mutex_exit(&freemem_lock);

	/*
	 * Figure out how big the resource should be.
	 *
	 * mapinit takes two entries, and the current
	 * allocation will take one entry. Given the
	 * space we'll have left after the first allocation,
	 * and assuming that each call to kncalloc is for an
	 * average of 32 bytes (which is a bit low), we can
	 * calculate the optimum number of pieces this map
	 * should be.
	 */

	nparts = ((mmu_ptob(npages) - nbytes) >> 5);

	/*
	 * Allocate the knc structure and space for the
	 * associated resource map in one pop.
	 */

	kp = (struct knclist *)
	    kmem_zalloc(sizeof (*kp) + nparts * sizeof (struct map), KM_SLEEP);

	/*
	 * initialized returned structure
	 */

	kp->knc_map = rmallocmap((ulong_t) nparts);
	if (kp->knc_map == (struct map *) 0) {
		cmn_err(CE_PANIC, "Too bad- should have had rmallocmap sleep");
		/* NOTREACHED */
	}
	kp->knc_pp = pp;
	kp->knc_addr = addr;
	kp->knc_size = mmu_ptob(npages);

	if (kcadebug) {
		printf("kncalloc: new seg addr=%x npages %d\n", addr, npages);
	}

	/*
	 * Now establish the mappings to the addresses we've made...
	 */

	do {
		hat_pagecachectl(pp, HAT_UNCACHE);
		hat_memload(kas.a_hat, &kas, (caddr_t)pp->p_offset, pp,
			    PROT_ALL & ~PROT_USER, HAT_LOCK);
		page_downgrade(pp);
		pp = pp->p_next;
	} while (pp != kp->knc_pp);

	/*
	 * free the allocated space into the map (initializes it)
	 */
	rmfree(kp->knc_map, (size_t) kp->knc_size, kp->knc_addr);

	/*
	 * link this map into the list
	 */

	mutex_enter(&knc_lock);
	kncnbytes += ptob(npages);
	kp->knc_next = knclist;
	knclist = kp;
	mutex_exit(&knc_lock);

	/*
	 * and recurse to get the allocation
	 */
	return (kncalloc(nbytes));
}

static void
kncfree(caddr_t addr, u_int nbytes)
{
	register struct knclist *kp;
	u_long a;

	a = ((u_long) addr);
	mutex_enter(&knc_lock);
	for (kp = knclist; kp != 0; kp = kp->knc_next) {
		if (a >= kp->knc_addr && a < kp->knc_addr + kp->knc_size) {
			rmfree(kp->knc_map, (long) nbytes, a);
			mutex_exit(&knc_lock);
			if (kcadebug) {
				printf("kncfree: freeing addr 0x%x size %d\n",
				    addr, nbytes);
			}
			return;
		}
	}
	mutex_exit(&knc_lock);
	cmn_err(CE_PANIC, "kncfree: freeing freed addr 0x%x size %d\n",
	    addr, nbytes);
}

/*
 * Allocate from the system, aligned on a specific boundary.
 *
 * It is assumed that alignment, if non-zero, is a simple power of two.
 *
 * It is also assumed that the power of two alignment is
 * less than or equal to the system's pagesize.
 */


caddr_t
kalloca(u_int nbytes, int align, int nocache, int cansleep)
{
	u_long addr, raddr, alloc, *saddr;

	if (kcadebug) {
		printf("kalloca: request is %d%%%d nc=%d slp=%d\n", nbytes,
		    align, nocache, cansleep);
	}

	/*
	 * Validate and fiddle with alignment
	 */

	if (align < 8)
		align = 8;
	if (align & (align - 1))
		return ((caddr_t) 0);

	/*
	 * Round up the allocation to an alignment boundary,
	 * and add in padding for both alignment and storage
	 * of the 'real' address and length.
	 */

	alloc = roundup(nbytes, align) + 8 + align;

	/*
	 * Allocate the requested space from the desired space
	 */

	if (nocache) {
		raddr = addr = (u_long) kncalloc(alloc);
	} else {
		raddr = addr = (u_long)
		    kmem_alloc(alloc, (cansleep)? KM_SLEEP : KM_NOSLEEP);
	}

	if (addr) {
		mutex_enter(&knc_lock);
		if (nocache) {
			knctotasked += nbytes;
			knctotused += alloc;
			kncused += alloc;
		} else {
			kctotasked += nbytes;
			kctotused += alloc;
			kcused += alloc;
		}
		mutex_exit(&knc_lock);
		if (addr & (align - 1)) {
			addr = roundup(addr, align);
			if (addr - raddr < 8)
				addr = roundup(addr, align);
		} else
			addr += align;

		saddr = (u_long *) addr;
		saddr[-1] = raddr;
		saddr[-2] = alloc;
	}

	if (kcadebug) {
		printf("kalloca: %d%%%d from %s got %x.%x returns %x\n",
		    nbytes, align, (nocache)? "kncmap" : "heap", raddr,
		    raddr + alloc, addr);
	}

	return ((caddr_t) addr);
}

void
kfreea(caddr_t addr, int nocache)
{
	void kncfree(caddr_t, u_int n);
	u_long *saddr, raddr;
	u_int nbytes;

	saddr = (u_long *) (((u_long) addr) & ~0x3);
	raddr = saddr[-1];
	nbytes = (u_int) saddr[-2];
	if (kcadebug) {
		printf("kfreea: freeing %x (real %x.%x) from %s area\n",
		    addr, raddr, raddr + nbytes, (nocache)? "kncmap" : "heap");
	}
	if (nocache) {
		mutex_enter(&knc_lock);
		kncused -= nbytes;
		mutex_exit(&knc_lock);
		kncfree((caddr_t) raddr, nbytes);
	} else {
		mutex_enter(&knc_lock);
		kcused -= nbytes;
		mutex_exit(&knc_lock);
		kmem_free((caddr_t) raddr, nbytes);
	}
}

#ifdef sun4c
int
locked_kaddr(caddr_t addr, u_int size, u_int addr_lo, u_int addr_hi)
{
	register struct knclist *kp;
	register u_long a;

	a = (u_long) addr;
	if (a >= (u_long) Sysbase && a <= (u_long) Syslimit &&
	    (addr_lo + size) < (u_int) Syslimit) {
		return (1);
	}

#ifdef	here_just_for_comment_different_from_sun4
	if ((cputype & CPU_ARCH) == SUN4_ARCH) {
		/*
		 * Onboard Ethernet page?
		 */
		if (a >= (u_long) (0-ptob(1))) {
			return (2);
		}
	}
#endif

	mutex_enter(&knc_lock);
	for (kp = knclist; kp != 0; kp = kp->knc_next) {
		if (a >= kp->knc_addr && a < kp->knc_addr + kp->knc_size) {
			break;
		}
	}
	mutex_exit(&knc_lock);
	/*
	 * The address is in the kncmap.
	 *
	 * Better make sure that the specified
	 * device can actually get at this address
	 */
	if (kp && addr_lo <= a && (a + size) < addr_hi)
		return (2);
	return (0);
}
#endif sun4c
