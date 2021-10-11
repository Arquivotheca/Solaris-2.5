/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * +++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 * 		PROPRIETARY NOTICE (Combined)
 *
 * This source code is unpublished proprietary information
 * constituting, or derived under license from AT&T's UNIX(r) System V.
 * In addition, portions of such source code were derived from Berkeley
 * 4.3 BSD under license from the Regents of the University of
 * California.
 *
 *
 *
 * 		Copyright Notice
 *
 * Notice of copyright on this source code product does not indicate
 * publication.
 *
 * 	(c) 1986, 1987, 1988, 1989, 1990, 1993  Sun Microsystems, Inc
 * 	(c) 1983, 1984, 1985, 1986, 1987, 1988, 1989  AT&T.
 *		All rights reserved.
 *
 */

#ident	"@(#)vm_anon.c	1.103	94/11/30 SMI"
/*	From:	SVr4.0	"kernel:vm/vm_anon.c	1.24"		*/

/*
 * VM - anonymous pages.
 *
 * This layer sits immediately above the vm_swap layer.  It manages
 * physical pages that have no permanent identity in the file system
 * name space, using the services of the vm_swap layer to allocate
 * backing storage for these pages.  Since these pages have no external
 * identity, they are discarded when the last reference is removed.
 *
 * An important function of this layer is to manage low-level sharing
 * of pages that are logically distinct but that happen to be
 * physically identical (e.g., the corresponding pages of the processes
 * resulting from a fork before one process or the other changes their
 * contents).  This pseudo-sharing is present only as an optimization
 * and is not to be confused with true sharing in which multiple
 * address spaces deliberately contain references to the same object;
 * such sharing is managed at a higher level.
 *
 * The key data structure here is the anon struct, which contains a
 * reference count for its associated physical page and a hint about
 * the identity of that page.  Anon structs typically live in arrays,
 * with an instance's position in its array determining where the
 * corresponding backing storage is allocated; however, the swap_xlate()
 * routine abstracts away this representation information so that the
 * rest of the anon layer need not know it.  (See the swap layer for
 * more details on anon struct layout.)
 *
 * In the future versions of the system, the association between an
 * anon struct and its position on backing store will change so that
 * we don't require backing store all anonymous pages in the system.
 * This is important for consideration for large memory systems.
 * We can also use this technique to delay binding physical locations
 * to anonymous pages until pageout/swapout time where we can make
 * smarter allocation decisions to improve anonymous klustering.
 *
 * Many of the routines defined here take a (struct anon **) argument,
 * which allows the code at this level to manage anon pages directly,
 * so that callers can regard anon structs as opaque objects and not be
 * concerned with assigning or inspecting their contents.
 *
 * Clients of this layer refer to anon pages indirectly.  That is, they
 * maintain arrays of pointers to anon structs rather than maintaining
 * anon structs themselves.  The (struct anon **) arguments mentioned
 * above are pointers to entries in these arrays.  It is these arrays
 * that capture the mapping between offsets within a given segment and
 * the corresponding anonymous backing storage address.
 */

#define	ANON_DEBUG

#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mman.h>
#include <sys/cred.h>
#include <sys/vnode.h>
#include <sys/cpuvar.h>
#include <sys/swap.h>
#include <sys/cmn_err.h>
#include <sys/vtrace.h>
#include <sys/kmem.h>
#include <sys/sysmacros.h>
#include <sys/bitmap.h>
#include <sys/debug.h>
#include <sys/tnf_probe.h>

#include <vm/hat.h>
#include <vm/anon.h>
#include <vm/page.h>
#include <vm/seg.h>
#include <vm/rm.h>

int anon_debug;

struct	anoninfo anoninfo;
ani_free_t	 ani_free_pool[ANI_MAX_POOL];

static int npagesteal;

/*
 * Global hash table for (vp, off) -> anon slot
 */
extern int swap_maxcontig;
int anon_hash_size;
struct anon **anon_hash;

static struct kmem_cache *anon_cache;
static struct kmem_cache *anonmap_cache;

/*ARGSUSED1*/
static void
anonmap_cache_constructor(void *buf, size_t size)
{
	struct anon_map *amp = buf;

	mutex_init(&amp->lock, "anon map lock", MUTEX_DEFAULT, NULL);
	mutex_init(&amp->serial_lock, "anon map serial lock", MUTEX_DEFAULT,
		NULL);
}

/*ARGSUSED1*/
static void
anonmap_cache_destructor(void *buf, size_t size)
{
	struct anon_map *amp = buf;

	mutex_destroy(&amp->lock);
	mutex_destroy(&amp->serial_lock);
}

kmutex_t	anonhash_lock[AH_LOCK_SIZE];

void
anon_init()
{
	extern int physmem;
	char buf[100];
	int i;

	anon_hash_size = 1 << highbit(physmem / ANON_HASHAVELEN);

	for (i = 0; i < AH_LOCK_SIZE; i++) {
		(void) sprintf(buf, "ah_lock.%d", i);
		mutex_init(&anonhash_lock[i], buf, MUTEX_DEFAULT, NULL);
	}

	anon_hash = (struct anon **)
		kmem_zalloc(sizeof (struct anon *) * anon_hash_size, KM_SLEEP);
	anon_cache = kmem_cache_create("anon_cache", sizeof (struct anon),
		0, NULL, NULL, NULL);
	anonmap_cache = kmem_cache_create("anonmap_cache",
		sizeof (struct anon_map), 0,
		anonmap_cache_constructor, anonmap_cache_destructor, NULL);
	swap_maxcontig = 1024 * 1024 / PAGESIZE;	/* 1MB of pages */
}

/*
 * Global anon slot hash table manipulation.
 */

static void
anon_addhash(ap)
	struct anon *ap;
{
	u_int	index;

	ASSERT(MUTEX_HELD(&anonhash_lock[AH_LOCK(ap->an_vp, ap->an_off)]));
	index = ANON_HASH(ap->an_vp, ap->an_off);
	ap->an_hash = anon_hash[index];
	anon_hash[index] = ap;
}

static void
anon_rmhash(ap)
	struct anon *ap;
{
	struct anon **app;

	ASSERT(MUTEX_HELD(&anonhash_lock[AH_LOCK(ap->an_vp, ap->an_off)]));

	for (app = &anon_hash[ANON_HASH(ap->an_vp, ap->an_off)];
	    *app; app = &((*app)->an_hash)) {
		if (*app == ap) {
			*app = ap->an_hash;
			break;
		}
	}
}


/*
 * Called from clock handler to sync ani_free value.
 */

void
set_anoninfo()
{
	u_int	ix, total = 0;

	for (ix = 0; ix < ANI_MAX_POOL; ix++) {
		total += ani_free_pool[ix].ani_count;
	}
	anoninfo.ani_free = total;
}

/* Amount of swap space taken from availrmem. */
int mem_resv;
/*
 * Reserve anon space.
 *
 * It's no longer simply a matter of incrementing ani_resv to
 * reserve swap space, we need to check memory-based as well
 * as disk-backed (physical) swap.  The following algorithm
 * is used:
 * 	Check the space on physical swap
 * 		i.e. amount needed < ani_max - ani_resv
 * 	If we are swapping on swapfs check
 *		amount needed < (availrmem - swapfs_minfree) +
 *		    (ani_max - ani_resv)
 * Since the algorithm to check for the quantity of swap space is
 * almost the same as that for reserving it, we'll just use anon_resvmem
 * with a flag to decrement availrmem.
 *
 * Return non-zero on success.
 */
int
anon_resvmem(size, takemem)
	u_int size;
	u_int takemem;
{
	register int spages = btopr(size);
	register int mswap_pages = 0;
	register int pswap_pages = 0;
	extern int swapfs_minfree;

	mutex_enter(&anoninfo_lock);
	/*
	 * pswap_pages is the number of pages we can take from
	 * physical (i.e. disk-backed) swap.  If pswap_pages is
	 * negative, this means we've already begun reserving
	 * from memory
	 */
	pswap_pages = anoninfo.ani_max - anoninfo.ani_resv;
	ANON_PRINT(A_RESV,
		("anon_resvmem: spages %d takemem %d pswap %d caller %x\n",
		spages, takemem, pswap_pages, (int)caller()));
	if (spages <= pswap_pages) {
		/*
		 * we have the space on a physical swap
		 */
		if (takemem) {
			/*
			 * Avoid the memory swap checking
			 */
			goto reserve;
		} else {
			mutex_exit(&anoninfo_lock);
			return (1);
		}
	}

	/*
	 * mswap_pages is the number of pages needed from availrmem
	 */
	mswap_pages = pswap_pages > 0 ? spages - pswap_pages : spages;
	ANON_PRINT(A_RESV, ("anon_resvmem: need %d pages from memory\n",
	    mswap_pages));

	/*
	 * Since swapfs is available, we can use up to
	 * (availrmem - swapfs_minfree) bytes of physical memory
	 * as reservable swap space
	 */
	mutex_enter(&freemem_lock);
	if (mswap_pages > (availrmem - swapfs_minfree)) {
		/*
		 * Fail if not enough memory
		 */
		mutex_exit(&freemem_lock);
		mutex_exit(&anoninfo_lock);
		ANON_PRINT(A_RESV,
		    ("anon_resvmem: not enough space from swapfs\n"));
		return (0);
	} else if (takemem) {
		/*
		 * Take the memory from the rest of the system.
		 */
		availrmem -= mswap_pages;
		mem_resv += mswap_pages;
		mutex_exit(&freemem_lock);
		ANI_ADD(mswap_pages);
		ANON_PRINT((A_RESV | A_MRESV),
		    ("anon_resvmem: took %d pages of availrmem\n",
		    mswap_pages));
	} else {
		mutex_exit(&freemem_lock);
	}
reserve:
	if (takemem) {
		anoninfo.ani_resv += spages;
		mutex_enter(&freemem_lock);
		availsmem -= spages;
		mutex_exit(&freemem_lock);
	}
	ASSERT(!mem_resv || mem_resv == anoninfo.ani_resv - anoninfo.ani_max);
	mutex_exit(&anoninfo_lock);
	ASSERT(mem_resv >= 0);
	return (1);
}

/*
 * Give back an anon reservation.
 */
void
anon_unresv(size)
	u_int size;
{
	register u_int spages = btopr(size);
	register u_int mempages = 0;
	u_int resv;

	mutex_enter(&freemem_lock);
	availsmem += spages;
	mutex_exit(&freemem_lock);

	mutex_enter(&anoninfo_lock);
	/*
	 * If some of this reservation belonged to swapfs
	 * give it back to availrmem.
	 * ani_resv - ani_max is the amount of availrmem
	 * swapfs has allocated.
	 */
	if (anoninfo.ani_resv > anoninfo.ani_max) {
		ANON_PRINT((A_RESV | A_MRESV),
		    ("anon_unresv: growing availrmem by %d pages\n",
		    MIN(anoninfo.ani_resv - anoninfo.ani_max, spages)));
		mempages = MIN(anoninfo.ani_resv - anoninfo.ani_max, spages);
		mutex_enter(&freemem_lock);
		availrmem += mempages;
		mem_resv -= mempages;
		mutex_exit(&freemem_lock);
		ANI_ADD(-mempages);
	}
	anoninfo.ani_resv -= spages;
	ASSERT(!mem_resv || mem_resv == anoninfo.ani_resv - anoninfo.ani_max);
	resv = anoninfo.ani_resv;
	mutex_exit(&anoninfo_lock);
	ASSERT(mem_resv >= 0);
	ANON_PRINT(A_RESV, ("anon_unresv: %d, tot %d, caller %x\n",
		spages, resv, (int)caller()));
	if ((int)resv < 0)
		cmn_err(CE_WARN, "anon: reservations below zero?\n");
}

/*
 * Allocate an anon slot and return it with the lock held.
 */
struct anon *
anon_alloc(vp, off)
	struct vnode *vp;
	u_int off;
{
	register struct anon *ap;
	kmutex_t	*ahm;

	ap = kmem_cache_alloc(anon_cache, KM_SLEEP);
	if (vp == NULL) {
		swap_alloc(ap);
	} else {
		ap->an_vp = vp;
		ap->an_off = off;
	}
	ap->an_refcnt = 1;
	ap->an_pvp = NULL;
	ap->an_poff = 0;
	ahm = &anonhash_lock[AH_LOCK(ap->an_vp, ap->an_off)];
	mutex_enter(ahm);
	anon_addhash(ap);
	mutex_exit(ahm);
	ANI_ADD(-1);
	ANON_PRINT(A_ANON, ("anon_alloc: returning ap %x, vp %x\n",
		(int)ap, (int)(ap ? ap->an_vp : NULL)));
	return (ap);
}

/*
 * Decrement the reference count of an anon page.
 * If reference count goes to zero, free it and
 * its associated page (if any).
 */
void
anon_decref(ap)
	register struct anon *ap;
{
	register page_t *pp;
	struct vnode *vp;
	u_int off;
	kmutex_t *ahm;

	ahm = &anonhash_lock[AH_LOCK(ap->an_vp, ap->an_off)];
	mutex_enter(ahm);
	ASSERT(ap->an_refcnt != 0);
	if (ap->an_refcnt == 0)
		cmn_err(CE_PANIC, "anon_decref: slot count 0\n");
	if (--ap->an_refcnt == 0) {
		swap_xlate(ap, &vp, &off);
		mutex_exit(ahm);

		/*
		 * If there is a page for this anon slot we will need to
		 * call VN_DISPOSE to get rid of the vp association and
		 * put the page back on the free list as really free.
		 * Acquire the "exclusive" lock to ensure that any
		 * pending i/o always completes before the swap slot
		 * is freed.
		 */
		pp = page_lookup(vp, off, SE_EXCL);

		/*
		 * If there was a page, we've synchronized on it (getting
		 * the exclusive lock is as good as gettting the iolock)
		 * so now we can free the physical backing store. Also, this
		 * is where we would free the name of the anonymous page
		 * (swap_free(ap)), a no-op in the current implementation.
		 */
		mutex_enter(ahm);
		ASSERT(ap->an_refcnt == 0);
		anon_rmhash(ap);
		if (ap->an_pvp)
			swap_phys_free(ap->an_pvp, ap->an_poff, PAGESIZE);
		mutex_exit(ahm);

		if (pp != NULL) {
			/*LINTED: constant in conditional context */
			VN_DISPOSE(pp, B_INVAL, 0, kcred);
		}
		ANON_PRINT(A_ANON, ("anon_decref: free ap %x, vp %x\n",
		    (int)ap, (int)ap->an_vp));
		kmem_cache_free(anon_cache, ap);

		ANI_ADD(1);
	} else {
		mutex_exit(ahm);
	}
}

/*
 * Duplicate references to size bytes worth of anon pages.
 * Used when duplicating a segment that contains private anon pages.
 * This code assumes that procedure calling this one has already used
 * hat_chgprot() to disable write access to the range of addresses that
 * that *old actually refers to.
 */
void
anon_dup(old, new, size)
	register struct anon **old, **new;
	u_int size;
{
	register int i;
	kmutex_t *ahm;

	i = btopr(size);
	while (i-- > 0) {
		if ((*new = *old) != NULL) {
			ahm = &anonhash_lock[AH_LOCK((*old)->an_vp,
			    (*old)->an_off)];
			mutex_enter(ahm);
			(*new)->an_refcnt++;
			mutex_exit(ahm);
		}
		old++;
		new++;
	}
}

/*
 * Free a group of "size" anon pages, size in bytes,
 * and clear out the pointers to the anon entries.
 */
void
anon_free(app, size)
	register struct anon **app;
	u_int size;
{
	register int i;
	struct anon *ap;

	i = btopr(size);
	while (i-- > 0) {
		if ((ap = *app) != NULL) {
			*app = NULL;
			anon_decref(ap);
		}
		app++;
	}
}

/*
 * Return the kept page(s) and protections back to the segment driver.
 */
int
anon_getpage(app, protp, pl, plsz, seg, addr, rw, cred)
	struct anon **app;
	u_int *protp;
	page_t *pl[];
	u_int plsz;
	struct seg *seg;
	caddr_t addr;
	enum seg_rw rw;
	struct cred *cred;
{
	register page_t *pp;
	register struct anon *ap = *app;
	struct vnode *vp;
	u_int off;
	int err;
	kmutex_t *ahm;

	swap_xlate(ap, &vp, &off);

	/*
	 * Lookup the page. If page is being paged in,
	 * wait for it to finish as we must return a list of
	 * pages since this routine acts like the VOP_GETPAGE
	 * routine does.
	 */
	if (pl != NULL && (pp = page_lookup(vp, off, SE_SHARED))) {
		ahm = &anonhash_lock[AH_LOCK(ap->an_vp, ap->an_off)];
		mutex_enter(ahm);
		if (ap->an_refcnt == 1)
			*protp = PROT_ALL;
		else
			*protp = PROT_ALL & ~PROT_WRITE;
		mutex_exit(ahm);
		pl[0] = pp;
		pl[1] = NULL;
		return (0);
	}

	/*
	 * Simply treat it as a vnode fault on the anon vp.
	 */

	TRACE_3(TR_FAC_VM, TR_ANON_GETPAGE,
		"anon_getpage:seg %x addr %x vp %x",
		seg, addr, vp);

	err = VOP_GETPAGE(vp, (offset_t)off, PAGESIZE, protp, pl, plsz,
	    seg, addr, rw, cred);

	if (err == 0 && pl != NULL) {
		ahm = &anonhash_lock[AH_LOCK(ap->an_vp, ap->an_off)];
		mutex_enter(ahm);
		if (ap->an_refcnt != 1)
			*protp &= ~PROT_WRITE;	/* make read-only */
		mutex_exit(ahm);
	}
	return (err);
}

/*
 * Turn a reference to an object or shared anon page
 * into a private page with a copy of the data from the
 * original page which is always locked by the caller.
 * This routine unloads the translation and unlocks the
 * original page, if it isn't being stolen, before returning
 * to the caller.
 *
 * NOTE:  The original anon slot is not freed by this routine
 *	  It must be freed by the caller while holding the
 *	  "anon_map" lock to prevent races which can occur if
 *	  a process has multiple lwps in its address space.
 */
page_t *
anon_private(app, seg, addr, opp, oppflags, cred)
	struct anon **app;
	struct seg *seg;
	caddr_t addr;
	page_t *opp;
	u_int oppflags;
	struct cred *cred;
{
	register struct anon *old = *app;
	register struct anon *new;
	register page_t *pp;
	struct vnode *vp;
	u_int off;
	page_t *anon_pl[1 + 1];
	int err;

	if (oppflags & STEAL_PAGE)
		ASSERT(se_excl_assert(&opp->p_selock));
	else
		ASSERT(se_assert(&opp->p_selock));

	CPU_STAT_ADD_K(cpu_vminfo.cow_fault, 1);

	/* Kernel probe */
	TNF_PROBE_1(anon_private, "vm pagefault", /* CSTYLED */,
		tnf_opaque,	address,	addr);

	*app = new = anon_alloc(NULL, 0);
	swap_xlate(new, &vp, &off);

	if (oppflags & STEAL_PAGE) {
		page_rename(opp, vp, off);
		pp = opp;
		TRACE_5(TR_FAC_VM, TR_ANON_PRIVATE,
			"anon_private:seg %x addr %x pp %x vp %x off %x",
			seg, addr, pp, vp, off);
		hat_setmod(pp);
		/*
		 * If original page is ``locked'', transfer
		 * the lock from a "cowcnt" to a "lckcnt" since
		 * we know that it is not a private page.
		 */
		if (oppflags & LOCK_PAGE)
			page_pp_useclaim(pp, pp, 1);
		npagesteal++;
		return (pp);
	}

	/*
	 * Call the VOP_GETPAGE routine to create the page, thereby
	 * enabling the vnode driver to allocate any filesystem
	 * space (e.g., disk block allocation for UFS).  This also
	 * prevents more than one page from being added to the
	 * vnode at the same time.
	 */
	err = VOP_GETPAGE(vp, (offset_t)off, PAGESIZE, (u_int *)NULL,
	    anon_pl, PAGESIZE, seg, addr, S_CREATE, cred);
	if (err) {
		*app = old;
		anon_decref(new);
		page_unlock(opp);
		return ((page_t *)NULL);
	}
	pp = anon_pl[0];

	/*
	 * Now copy the contents from the original page,
	 * which is locked and loaded in the MMU by
	 * the caller to prevent yet another page fault.
	 */
	ppcopy(opp, pp);		/* XXX - should set mod bit in here */

	hat_setrefmod(pp);		/* mark as modified */

	/*
	 * If the original page was locked, we need
	 * to move the lock to the new page.
	 * If we did not have an anon page before,
	 * page_pp_useclaim should expect opp
	 * to have a cowcnt instead of lckcnt.
	 */
	if (oppflags & LOCK_PAGE)
		page_pp_useclaim(opp, pp, old == NULL);

	/*
	 * Unload the old translation.
	 */
	hat_unload(seg->s_as, addr, PAGESIZE, HAT_UNLOAD);

	/*
	 * Ok, now release the lock on the original page,
	 * or else the process will sleep forever in
	 * anon_decref() waiting for the "exclusive" lock
	 * on the page.
	 */
	page_unlock(opp);

	/*
	 * NOTE:  The original anon slot must be freed by the
	 * caller while holding the "anon_map" lock, if we
	 * copied away from an anonymous page.
	 */
	return (pp);
}

/*
 * Allocate a private zero-filled anon page.
 */
page_t *
anon_zero(seg, addr, app, cred)
	struct seg *seg;
	caddr_t addr;
	struct anon **app;
	struct cred *cred;
{
	register struct anon *ap;
	register page_t *pp;
	struct vnode *vp;
	u_int off;
	page_t *anon_pl[1 + 1];
	int err;

	/* Kernel probe */
	TNF_PROBE_1(anon_zero, "vm pagefault", /* CSTYLED */,
		tnf_opaque,	address,	addr);

	*app = ap = anon_alloc(NULL, 0);
	swap_xlate(ap, &vp, &off);

	/*
	 * Call the VOP_GETPAGE routine to create the page, thereby
	 * enabling the vnode driver to allocate any filesystem
	 * dependent structures (e.g., disk block allocation for UFS).
	 * This also prevents more than on page from being added to
	 * the vnode at the same time since it is locked.
	 */
	err = VOP_GETPAGE(vp, (offset_t)off, PAGESIZE, (u_int *)NULL,
	    anon_pl, PAGESIZE, seg, addr, S_CREATE, cred);
	if (err) {
		*app = NULL;
		anon_decref(ap);
		return ((page_t *)NULL);
	}
	pp = anon_pl[0];

	pagezero(pp, 0, PAGESIZE);	/* XXX - should set mod bit */
	page_downgrade(pp);
	CPU_STAT_ADD_K(cpu_vminfo.zfod, 1);
	hat_setrefmod(pp);	/* mark as modified so pageout writes back */
	return (pp);
}

/*
 * Allocate and initialize an anon_map structure for seg
 * associating the given swap reservation with the new anon_map.
 */
struct anon_map *
anonmap_alloc(size, swresv)
	u_int size;
	u_int swresv;
{
	struct anon_map *amp;		/* XXX - For locknest */

	amp = kmem_cache_alloc(anonmap_cache, KM_SLEEP);

	amp->refcnt = 1;
	amp->size = size;
	amp->anon = (struct anon **)
	    kmem_zalloc((u_int)btopr(size) * sizeof (struct anon *), KM_SLEEP);
	amp->swresv = swresv;
	return (amp);
}

void
anonmap_free(amp)
	struct anon_map *amp;
{
	ASSERT(amp->anon);
	ASSERT(amp->refcnt == 0);

	kmem_free((caddr_t)amp->anon,
	    btopr(amp->size) * sizeof (struct anon *));
	kmem_cache_free(anonmap_cache, amp);
}

/*
 * Returns true if the app array has some empty slots.
 * The offp and lenp paramters are in/out paramters.  On entry
 * these values represent the starting offset and length of the
 * mapping.  When true is returned, these values may be modified
 * to be the largest range which includes empty slots.
 */
int
non_anon(app, offp, lenp)
	register struct anon **app;
	u_int *offp, *lenp;
{
	register int i, el;
	int low, high;

	low = -1;
	for (i = 0, el = *lenp; i < el; i += PAGESIZE) {
		if (*app++ == NULL) {
			if (low == -1)
				low = i;
			high = i;
		}
	}
	if (low != -1) {
		/*
		 * Found at least one non-anon page.
		 * Set up the off and len return values.
		 */
		if (low != 0)
			*offp += low;
		*lenp = high - low + PAGESIZE;
		return (1);
	}
	return (0);
}


/*
 * Return a count of the number of existing anon pages in the anon array
 * app in the range (off, off+len). The array and slots must be guaranteed
 * stable by the caller.
 */
u_int
anon_pages(app, off, len)
	struct anon **app;
	u_int off, len;
{
	struct anon **capp, **eapp;
	int cnt = 0;

	for (capp = app + off, eapp = capp + len; capp < eapp; capp++) {
		if (*capp != NULL)
			cnt++;
	}
	return (ptob(cnt));
}
