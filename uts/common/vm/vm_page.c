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
 * 	(c) 1986, 1987, 1988, 1989, 1990, 1991, 1993  Sun Microsystems, Inc
 * 	(c) 1983, 1984, 1985, 1986, 1987, 1988, 1989  AT&T.
 *		All rights reserved.
 *
 */

#pragma ident   "@(#)vm_page.c 1.158     95/10/09 SMI"

/*	From:	SVr4.0	"kernel:vm/vm_page.c	1.52"		*/

/*
 * VM - physical page management.
 */

#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <sys/vnode.h>
#include <sys/vm.h>
#include <sys/vtrace.h>
#include <sys/swap.h>
#include <sys/cmn_err.h>
#include <sys/tuneable.h>
#include <sys/sysmacros.h>
#include <sys/cpuvar.h>
#include <sys/callb.h>
#include <sys/debug.h>
#include <sys/tnf_probe.h>

#include <vm/hat.h>
#include <vm/anon.h>
#include <vm/page.h>
#include <vm/seg.h>
#include <vm/pvn.h>
#include <vm/seg_kmem.h>

static int nopageage = 1;

static u_int max_page_get;	/* max page_get request size in pages */
u_int total_pages = 0;		/* total number of pages (used by /proc) */

/*
 * freemem_lock protects all freemem variables:
 * availsmem, availrmem, pages_pp_locked and pages_pp_kernel.
 */
kmutex_t freemem_lock;
int availrmem;
int availsmem;
u_int pages_pp_locked = 0;	/* physical pages actually locked */
u_int pages_pp_claimed = 0;	/* physical pages reserved */
u_int pages_pp_kernel = 0;	/* physical page locks by kernel */

/*
 * new_freemem_lock protects freemem, freemem_wait & freemem_cv.
 */
kmutex_t	new_freemem_lock;
static u_int	freemem_wait;	/* someone waiting for freemem */
static kcondvar_t freemem_cv;

int fault_type;			/* used by sun4/110 to allocate pages */

/*
 * Set to non-zero to avoid reclaiming pages which are
 * busy being paged back until the i/o is completed.
 */
int nopagereclaim = 0;

/*
 * The logical page free list is maintained as two lists, the 'free'
 * and the 'cache' lists.
 * The free list contains those pages that should be reused first.
 *
 * The implementation of the lists is machine dependent.
 * page_get_freelist(), page_get_cachelist(),
 * page_list_sub(), and page_list_add()
 * form the interface to the machine dependent implementation.
 *
 * Pages with p_free set are on the cache list.
 * Pages with p_free and p_age set are on the free list,
 *
 * A page may be locked while on either list.
 */
extern page_t	*page_get_freelist();
extern page_t	*page_get_cachelist();
extern void	page_list_sub(int, page_t *);
extern void	page_list_add(int, page_t *, int);

/*
 * free list accounting stuff.
 *
 *
 * Spread out the value for the number of pages on the
 * page free and page cache lists.  If there is just one
 * value, then it must be under just one lock.
 * The lock contention and cache traffic are a real bother.
 *
 * When we acquire and then drop a single pcf lock
 * we can start in the middle of the array of pcf structures.
 * If we acquire more than one pcf lock at a time, we need to
 * start at the front to avoid deadlocking.
 *
 * pcf_count holds the number of pages in each pool.
 *
 * pcf_block is set when page_create_get_something() has asked the
 * PSM page freelist and page cachelist routines without specifying
 * a color and nothing came back.  This is used to block anything
 * else from moving pages from one list to the other while the
 * lists are searched again.  If a page is freeed while pcf_block is
 * set, then pcf_reserve is incremented.  pcgs_unblock() takes care
 * of clearning pcf_block, doing the wakeups, etc.
 */

#if NCPU <= 4
#define	PAD	1
#define	PCF_FANOUT	4
static	u_int	pcf_mask = 0x3;
#else
#define	PAD	9
#define	PCF_FANOUT	16
static	u_int	pcf_mask = 0xF;
#endif

struct pcf {
	u_int		pcf_touch;	/* just to help the cache */
	u_int		pcf_count;	/* page count */
	kmutex_t	pcf_lock;	/* protects the structure */
	u_int		pcf_wait;	/* number of waiters */
	u_int		pcf_block; 	/* pcgs flag to page_free() */
	u_int		pcf_reserve; 	/* pages freed after pcf_block set */
	u_int		pcf_fill[PAD];	/* to line up on the caches */
};

static struct	pcf	pcf[PCF_FANOUT];
#define	PCF_INDEX()	((CPU->cpu_id) & (pcf_mask))

kmutex_t	pcgs_lock;		/* serializes page_create_get_ */

page_t *pages;				/* array of all page structures */
page_t *epages;				/* end of all pages */
struct memseg *memsegs = NULL;		/* list of memory segments */
static struct memseg *last_memseg;	/* cache of last segment accessed */

#define	PAGE_LOCK_MAXIMUM \
	((1 << (sizeof (((page_t *)0)->p_lckcnt) * NBBY)) - 1)

#ifdef VM_STATS

/*
 * No locks, but so what, they are only statistics.
 */

static struct page_tcnt {
	int	pc_free_cache;		/* free's into cache list */
	int	pc_free_dontneed;	/* free's with dontneed */
	int	pc_free_pageout;	/* free's from pageout */
	int	pc_free_free;		/* free's into free list */
	int	pc_get_cache;		/* get's from cache list */
	int	pc_get_free;		/* get's from free list */
	int	pc_reclaim;		/* reclaim's */
	int	pc_abortfree;		/* abort's of free pages */
	int	pc_find_hit;		/* find's that find page */
	int	pc_find_miss;		/* find's that don't find page */
	int	pc_destroy_free;	/* # of free pages destroyed */
#define	PC_HASH_CNT	(4*PAGE_HASHAVELEN)
	int	pc_find_hashlen[PC_HASH_CNT+1];
} pagecnt;

u_int	hashin_count;
u_int	hashin_not_held;
u_int	hashin_already;

u_int	hashout_count;
u_int	hashout_not_held;

u_int	page_create_count;
u_int	page_create_not_enough;
u_int	page_create_not_enough_again;
u_int	page_create_zero;
u_int	page_create_hashout;
u_int	page_create_page_lock_failed;
u_int	page_create_trylock_failed;
u_int	page_create_found_one;
u_int	page_create_hashin_failed;
u_int	page_create_dropped_phm;

u_int	page_create_new;
u_int	page_create_exists;
u_int	page_create_putbacks;
u_int	page_create_overshoot;

u_int	page_reclaim_zero;
u_int	page_reclaim_zero_locked;

u_int	page_rename_exists;
u_int	page_rename_count;

u_int	page_lookup_cnt[20];
u_int	page_lookup_nowait_cnt[10];
u_int	page_find_cnt;
u_int	page_exists_cnt;
u_int	page_lookup_dev_cnt;
u_int	get_cachelist_cnt;
u_int	page_create_cnt[10];
/*
 * Collects statistics.
 */
#define	PAGE_HASH_SEARCH(index, pp, vp, off) { \
	u_int	mylen = 0; \
			\
	for ((pp) = page_hash[(index)]; (pp); (pp) = (pp)->p_hash, mylen++) { \
		if ((pp)->p_vnode == (vp) && (pp)->p_offset == (off)) \
			break; \
	} \
	if ((pp) != NULL) \
		pagecnt.pc_find_hit++; \
	else \
		pagecnt.pc_find_miss++; \
	if (mylen > PC_HASH_CNT) \
		mylen = PC_HASH_CNT; \
	pagecnt.pc_find_hashlen[mylen]++; \
}

#else	/* VM_STATS */

/*
 * Don't collect statistics
 */
#define	PAGE_HASH_SEARCH(index, pp, vp, off) { \
	for ((pp) = page_hash[(index)]; (pp); (pp) = (pp)->p_hash) { \
		if ((pp)->p_vnode == (vp) && (pp)->p_offset == (off)) \
			break; \
	} \
}

#endif	/* VM_STATS */

extern kmutex_t	ph_mutex[];
extern u_int	ph_mutex_shift;
#define	PAGE_HASH_MUTEX(index)	&ph_mutex[(index) >> ph_mutex_shift]

/*
 * vm subsystem related initialization
 */
void
vm_init()
{
	void callb_vm_cpr(void *, int);

	(void) callb_add(callb_vm_cpr, 0, CB_CL_CPR_VM, "vm");
}

/*
 * Initialize the physical page structures.
 * Since we cannot call the dynamic memory allocator yet,
 * we have startup() allocate memory for the page
 * structs, the hash tables and memseg struct for us.
 * num is the number of page structures and base is the
 * physical page number to be associated with the first page.
 *
 * NOTE:  We insist on being called with increasing (but not necessarily
 * contiguous) physical addresses and with contiguous page structures.
 */
void
page_init(pp, num, base, memseg)
	register page_t *pp;
	u_int num, base;
	register struct memseg *memseg;
{
	register struct memseg *tseg;
	u_int pnum;

	/*
	 * XXX:	Should probably store additional information.
	 */
	TRACE_2(TR_FAC_VM, TR_PAGE_INIT,
		"page_init:pp %x num %d", pp, num);

	/*
	 * Store away info in globals.  These can be used to walk the
	 * page struct array linearly since all the page structures in
	 * the system are allocated contiguously, although they can
	 * represent discontiguous physical pages.
	 */
	if (pages == (page_t *)NULL)
		pages = pp;
	epages = &pp[num];

	/*
	 * Add this segment to the list of physical memory.
	 */
	if (memsegs == (struct memseg *)NULL)
		memsegs = memseg;
	else {
		for (tseg = memsegs; tseg->next != (struct memseg *)NULL;
		    tseg = tseg->next) {
			/* nothing */
		}
		ASSERT(tseg->pages_base < base);
		ASSERT(tseg->epages == pp);
		tseg->next = memseg;
	}
	memseg->next = (struct memseg *)NULL;

	/*
	 * Initialize the segment cache and fill in segment info.
	 */
	last_memseg = memseg;
	memseg->pages = pp;
	memseg->epages = &pp[num];
	memseg->pages_base = base;
	memseg->pages_end = base + num * (PAGESIZE/MMU_PAGESIZE);

	/*
	 * Arbitrarily limit the max page_get request
	 * to 1/2 of the page structs we have.
	 */
	total_pages += num;
	max_page_get = total_pages / 2;

	/*
	 * The physical space for the pages array
	 * representing ram pages has already been
	 * allocated.  Here we initialize each lock
	 * in the page structure.  These page structures
	 * will be freed in memialloc().
	 */
	pnum = base;
	for (; pp < epages; pp++) {
		/*
		 * Initialize and acquire the write version
		 * of the per-page readers/writer lock.
		 */
		se_init(&pp->p_selock, "page selock", SE_DEFAULT, DEFAULT_WT);

		/*
		 * Initialize the semaphore and the mutex.
		 */
		sema_init(&pp->p_iolock, 1, "page iolock",
		    SEMA_DEFAULT, DEFAULT_WT);

		/*
		 * Initialize physical page number.
		 */
		pp->p_pagenum = pnum++;
	}

	/*
	 * Verify that the hashing stuff has been initialized (machdep.c).
	 */
	if (page_hash == NULL || page_hashsz == 0) {
		cmn_err(CE_PANIC, "page_init");
	}
}

/*
 * Find a page representing the specified [vp, offset].
 * If we find the page but it is intransit coming in,
 * it will have an "exclusive" lock and we wait for
 * the i/o to complete.  A page found on the free list
 * is always reclaimed and then locked.  On success, the page
 * is locked, its data is valid and it isn't on the free
 * list, while a NULL is returned if the page doesn't exist.
 */
page_t *
page_lookup(vp, off, se)
	struct vnode *vp;
	u_int off;
	register se_t se;
{
	register page_t	*pp;
	register se_t	ose = se;
	kmutex_t	*phm;
	u_int		index;
	u_int		hash_locked;

	ASSERT(MUTEX_NOT_HELD(page_vnode_mutex(vp)));
	VM_STAT_ADD(page_lookup_cnt[0]);

	/*
	 * Acquire the appropriate page hash lock since
	 * we have to search the hash list.  Pages that
	 * hash to this list can't change identity while
	 * this lock is held.
	 */
	hash_locked = 0;
	index = PAGE_HASH_FUNC(vp, off);
	phm = NULL;
top:
	PAGE_HASH_SEARCH(index, pp, vp, off);
	if (pp != NULL) {
		VM_STAT_ADD(page_lookup_cnt[1]);
		/*
		 * If "nopagereclaim" is set and the "shared" lock
		 * is requested then we must not reclaim a page
		 * involved in i/o, so acquire the "exclusive"
		 * lock to ensure that the caller is blocked
		 * until the i/o completes.
		 */
		if (nopagereclaim && se == SE_SHARED)
			se = SE_EXCL;

		if (!hash_locked) {
			VM_STAT_ADD(page_lookup_cnt[2]);
			if (!page_try_reclaim_lock(pp, se)) {
				/*
				 * On a miss, acquire the phm.  Then
				 * next time, page_lock() will be called,
				 * causing a wait if the page is busy.
				 * just looping with page_trylock() would
				 * get pretty boring.
				 */
				VM_STAT_ADD(page_lookup_cnt[3]);
				phm = PAGE_HASH_MUTEX(index);
				mutex_enter(phm);
				hash_locked = 1;
				goto top;
			}
		} else {
			VM_STAT_ADD(page_lookup_cnt[4]);
			if (!page_lock(pp, se, phm, P_RECLAIM)) {
				VM_STAT_ADD(page_lookup_cnt[5]);
				goto top;
			}
		}

		/*
		 * Since `pp' is locked it can not change identity now.
		 * Reconfirm we locked the correct page.
		 *
		 * Both the p_vnode and p_offset *must* be cast volatile
		 * to force a reload of their values: The PAGE_HASH_SEARCH
		 * macro will have stuffed p_vnode and p_offset into
		 * registers before calling page_trylock(); another thread,
		 * actually holding the hash lock, could have changed the
		 * page's identity in memory, but our registers would not
		 * be changed, fooling the reconfirmation.  If the hash
		 * lock was held during the search, the casting would
		 * not be needed.
		 */
		VM_STAT_ADD(page_lookup_cnt[6]);
		if (((volatile struct vnode *)(pp->p_vnode) != vp) ||
		    ((volatile u_int)(pp->p_offset) != off)) {
			VM_STAT_ADD(page_lookup_cnt[7]);
			if (hash_locked) {
				cmn_err(CE_PANIC,
				    "page_lookup: lost page %x",
				    pp);
			}
			page_unlock(pp);
			phm = PAGE_HASH_MUTEX(index);
			mutex_enter(phm);
			hash_locked = 1;
			goto top;
		}

		/*
		 * If page_trylock() was called, then pp may still be on
		 * the cachelist (can't be on the free list, it would not
		 * have been found in the search).  If it is on the
		 * cachelist it must be pulled now. To pull the page from
		 * the cachelist, it must be exclusively locked.
		 *
		 * The other big difference between page_trylock() and
		 * page_lock(), is that page_lock() will pull the
		 * page from whatever free list (the cache list in this
		 * case) the page is on.  If page_trylock() was used
		 * above, then we have to do the reclaim ourselves.
		 */
		if ((!hash_locked) && (pp->p_free)) {
			ASSERT(pp->p_age == 0);
			VM_STAT_ADD(page_lookup_cnt[8]);

			/*
			 * page_relcaim will insure that we
			 * have this page exclusively
			 */

			if (!page_reclaim(pp, NULL)) {
				/*
				 * Page_reclaim dropped whatever lock
				 * we held.
				 */
				VM_STAT_ADD(page_lookup_cnt[10]);
				phm = PAGE_HASH_MUTEX(index);
				mutex_enter(phm);
				hash_locked = 1;
				goto top;
			} else if (se == SE_SHARED) {
				VM_STAT_ADD(page_lookup_cnt[11]);
				page_downgrade(pp);
				ose = SE_EXCL;	/* prevent double downgrade */
			}
		}
		if (nopagereclaim && ose == SE_SHARED) {
			page_downgrade(pp);
		}
	} else if (!hash_locked) {
		VM_STAT_ADD(page_lookup_cnt[12]);
		phm = PAGE_HASH_MUTEX(index);
		mutex_enter(phm);
		hash_locked = 1;
		goto top;
	} else {
		VM_STAT_ADD(page_lookup_cnt[13]);
	}

	if (hash_locked) {
		VM_STAT_ADD(page_lookup_cnt[14]);
		mutex_exit(phm);
	}

	ASSERT(pp ? ((se == SE_EXCL) ? pp->p_selock == -1 :
	    pp->p_selock > 0) : 1);
	ASSERT(pp ? ((pp->p_free == 0) && (pp->p_age == 0)) : 1);

	return (pp);
}

/*
 * Search the hash list for the page representing the
 * specified [vp, offset] and return it locked.  Skip
 * free pages and pages that cannot be locked as requested.
 * Used while attempting to kluster pages.
 */
page_t *
page_lookup_nowait(vp, off, se)
	struct vnode *vp;
	register u_int off;
	register se_t se;
{
	register page_t	*pp;
	kmutex_t	*phm;
	u_int		index;
	u_int		locked;

	ASSERT(MUTEX_NOT_HELD(page_vnode_mutex(vp)));
	VM_STAT_ADD(page_lookup_nowait_cnt[0]);

	index = PAGE_HASH_FUNC(vp, off);
	PAGE_HASH_SEARCH(index, pp, vp, off);
	locked = 0;
	if (pp == NULL) {
top:
		VM_STAT_ADD(page_lookup_nowait_cnt[1]);
		locked = 1;
		phm = PAGE_HASH_MUTEX(index);
		mutex_enter(phm);
		PAGE_HASH_SEARCH(index, pp, vp, off);
	}

	if (pp == NULL || pp->p_free) {
		VM_STAT_ADD(page_lookup_nowait_cnt[2]);
		pp = NULL;
	} else {
		if (!page_trylock(pp, se)) {
			VM_STAT_ADD(page_lookup_nowait_cnt[3]);
			pp = NULL;
		} else {
			VM_STAT_ADD(page_lookup_nowait_cnt[4]);
			/*
			 * See the comment in page_lookup()
			 */
			if (((volatile struct vnode *)(pp->p_vnode) != vp) ||
			    ((volatile u_int)(pp->p_offset) != off)) {
				VM_STAT_ADD(page_lookup_nowait_cnt[5]);
				if (locked) {
					cmn_err(CE_PANIC,
					    "page_lookup_nowait %x", pp);
				}
				page_unlock(pp);
				goto top;
			}
			if (pp->p_free) {
				VM_STAT_ADD(page_lookup_nowait_cnt[6]);
				page_unlock(pp);
				pp = NULL;
			}
		}
	}
	if (locked) {
		VM_STAT_ADD(page_lookup_nowait_cnt[7]);
		mutex_exit(phm);
	}

	ASSERT(pp? ((se == SE_EXCL)? pp->p_selock == -1 :
			pp->p_selock > 0) : 1);

	return (pp);
}

/*
 * Search the hash list for a page with the specified [vp, off]
 * that is known to exist and is already locked.  This routine
 * is typically used by segment SOFTUNLOCK routines.
 */
page_t *
page_find(vp, off)
	register struct vnode *vp;
	register u_int off;
{
	register page_t	*pp;
	kmutex_t	*phm;
	u_int		index;

	ASSERT(MUTEX_NOT_HELD(page_vnode_mutex(vp)));
	VM_STAT_ADD(page_find_cnt);

	index = PAGE_HASH_FUNC(vp, off);
	phm = PAGE_HASH_MUTEX(index);

	mutex_enter(phm);
	PAGE_HASH_SEARCH(index, pp, vp, off);
	mutex_exit(phm);

	ASSERT(pp != NULL);
	ASSERT(se_assert(&pp->p_selock) || panicstr);
	return (pp);
}

/*
 * Determine whether a page with the specified [vp, off]
 * currently exists in the system.  Obviously this should
 * only be considered as a hint since nothing prevents the
 * page from disappearing or appearing immediately after
 * the return from this routine. Subsequently, we don't
 * even bother to lock the list.
 */
int
page_exists(vp, off)
	register struct vnode *vp;
	register u_int off;
{
	register page_t	*pp;
	u_int		index;

	ASSERT(MUTEX_NOT_HELD(page_vnode_mutex(vp)));
	VM_STAT_ADD(page_exists_cnt);

	index = PAGE_HASH_FUNC(vp, off);
	PAGE_HASH_SEARCH(index, pp, vp, off);

	return (pp != NULL);
}

/*
 * Find a device page representing the specified [vp, offset].  This is
 * similar to page_lookup, but we don't do the "nopagereclaim" hack,
 * since these pages are really devpages and always have at least one
 * "shared" lock on them.  A page found on the free list is an error
 * (devpages can't be on the free list).  On success, the page is locked
 * as requested, while a NULL is returned if the page doesn't exist.
 *
 * Used only by devpage_lookup().
 */
page_t *
page_lookup_dev(vp, off, se)
	struct vnode *vp;
	u_int off;
	register se_t se;
{
	register page_t	*pp;
	kmutex_t	*phm;
	u_int		index;

	ASSERT(MUTEX_NOT_HELD(page_vnode_mutex(vp)));
	VM_STAT_ADD(page_lookup_dev_cnt);

	/*
	 * Acquire the appropriate page hash lock since
	 * we have to search the hash list.  Pages that
	 * hash to this list can't change identity while
	 * this lock is held.
	 */
	index = PAGE_HASH_FUNC(vp, off);
	phm = PAGE_HASH_MUTEX(index);
	mutex_enter(phm);
top:
	PAGE_HASH_SEARCH(index, pp, vp, off);
	if (pp != NULL) {
		if (pp->p_free) {
			cmn_err(CE_PANIC, "page_lookup_dev: free page");
			mutex_exit(phm);
		}

		if (!page_lock(pp, se, phm, P_RECLAIM))
			goto top;
	}
	mutex_exit(phm);
	return (pp);
}

/*
 * Set up the locks.
 */
void
page_create_init()
{
	char		buffer[100];
	u_int		i;
	struct pcf	*p;

	p = pcf;
	for (i = 0; i < PCF_FANOUT; i++) {
		(void) sprintf(buffer, "pagecreate_%d", i);
		mutex_init(&p->pcf_lock, buffer, MUTEX_DEFAULT, DEFAULT_WT);
		p++;
	}
	mutex_init(&pcgs_lock, "pcgs_lock", MUTEX_DEFAULT, DEFAULT_WT);
}

/*
 * 'freemem' is used all over the kernel as an indication of how many
 * pages are free (either on the cache list or on the free page list)
 * in the system.  In very few places is a really accurate 'freemem'
 * needed.  To avoid contention of the lock protecting a the
 * single freemem, it was spread out into NCPU buckets.  Set_freemem
 * sets freemem to the total of all NCPU buckets.  It is called from
 * clock() on each TICK.
 */
void
set_freemem()
{
	struct pcf	*p;
	u_int		t, i;

	t = 0;
	p = pcf;
	for (i = 0;  i < PCF_FANOUT; i++) {
		t += p->pcf_count;
		p++;
	}
	freemem = t;
}

u_int
get_freemem()
{
	struct pcf	*p;
	u_int		t, i;

	t = 0;
	p = pcf;
	for (i = 0; i < PCF_FANOUT; i++) {
		t += p->pcf_count;
		p++;
	}
	/*
	 * We just calculated it, might as well set it.
	 */
	freemem = t;
	return (t);
}

/*
 * Acquire all of the page cache & free (pcf) locks.
 */
void
pcf_acquire_all()
{
	struct pcf	*p;
	u_int		i;

	p = pcf;
	for (i = 0; i < PCF_FANOUT; i++) {
		p->pcf_touch = 1;
		mutex_enter(&p->pcf_lock);
		p++;
	}
}

/*
 * Release all the pcf_locks.
 */
void
pcf_release_all()
{
	struct pcf	*p;
	u_int		i;

	p = pcf;
	for (i = 0; i < PCF_FANOUT; i++) {
		mutex_exit(&p->pcf_lock);
		p++;
	}
}

/*
 * page_create_wait() is called to either coalecse pages from the
 * different pcf buckets or to wait because there simply are not
 * enough pages to satisfy the caller's request.
 *
 * Sadly, this is called from platform/vm/vm_machdep.c
 */
int
page_create_wait(npages, flags)
	u_int		npages;
	u_int		flags;
{
	u_int		i, total;
	struct pcf	*p;
	extern int	needfree;

	/*
	 * Wait until there are enough free pages to satisfy our
	 * entire request.
	 * We set needfree += npages before prodding pageout, to make sure
	 * it does real work when npages > lotsfree > freemem.
	 */
	VM_STAT_ADD(page_create_not_enough);

checkagain:
	/*
	 * Since page_create_va() looked at every
	 * bucket, assume we are going to have to wait.
	 * Get all of the pcf locks.
	 */
	total = 0;
	p = pcf;
	for (i = 0; i < PCF_FANOUT; i++) {
		p->pcf_touch = 1;
		mutex_enter(&p->pcf_lock);
		total += p->pcf_count;
		if (total >= npages) {
			/*
			 * Wow!  There are enough pages laying around
			 * to satisfy the request.  Do the accounting,
			 * drop the locks we acquired, and go back.
			 */
			freemem -= npages;

			while (p >= pcf) {
				if (p->pcf_count <= npages) {
					npages -= p->pcf_count;
					p->pcf_count = 0;
				} else {
					p->pcf_count -= npages;
					npages = 0;
				}
				mutex_exit(&p->pcf_lock);
				p--;
			}
			ASSERT(npages == 0);
			return (1);
		}
		p++;
	}

	/*
	 * All of the pcf locks are held, there are not enough pages
	 * to satisfy the request (npages < total).
	 * Be sure to acquire the new_freemem_lock before dropping
	 * the pcf locks.  This prevents dropping wakeups in page_free().
	 * The order is always pcf_lock then new_freemem_lock.
	 *
	 * Since we hold all the pcf locks, it is a good time to set freemem.
	 *
	 * If the caller does not want to wait, return now.
	 * Else turn the pageout daemon loose to find something
	 * and wait till it does.
	 *
	 */
	freemem = total;

	if ((flags & PG_WAIT) == 0) {
		pcf_release_all();

		TRACE_2(TR_FAC_VM, TR_PAGE_CREATE_NOMEM,
		"page_create_nomem:npages %d freemem %d", npages, freemem);
		return (0);
	}

	cv_signal(&proc_pageout->p_cv);

	TRACE_2(TR_FAC_VM, TR_PAGE_CREATE_SLEEP_START,
	    "page_create_sleep_start: freemem %d needfree %d",
	    freemem, needfree);

	/*
	 * We are going to wait.
	 * We currently hold all of the pcf_locks,
	 * get the new_freemem_lock (it protects freemem_wait),
	 * before dropping the pcf_locks.
	 */
	mutex_enter(&new_freemem_lock);

	p = pcf;
	for (i = 0; i < PCF_FANOUT; i++) {
		p->pcf_wait++;
		mutex_exit(&p->pcf_lock);
		p++;
	}

	needfree += npages;
	freemem_wait++;

	cv_wait(&freemem_cv, &new_freemem_lock);

	freemem_wait--;
	needfree -= npages;

	mutex_exit(&new_freemem_lock);

	TRACE_2(TR_FAC_VM, TR_PAGE_CREATE_SLEEP_END,
	    "page_create_sleep_end: freemem %d needfree %d", freemem, needfree);

	VM_STAT_ADD(page_create_not_enough_again);
	goto checkagain;
}

/*
 * A routine to do the opposite of page_create_wait().
 */
void
page_create_putback(int npages)
{
	struct pcf	*p;
	u_int		lump;
	u_int		*which;

	/*
	 * When a contiguos lump is broken up, we have to
	 * deal with lots of pages (min 64) so lets spread
	 * the wealth around.
	 */
	lump = npages/PCF_FANOUT;
	freemem += npages;

	for (p = pcf; (npages > 0) && (p < &pcf[PCF_FANOUT]); p++) {
		which = &p->pcf_count;
		if (p->pcf_block) {
			which = &p->pcf_reserve;
		}

		mutex_enter(&p->pcf_lock);

		if (lump < npages) {
			*which += lump;
			npages -= lump;
		} else {
			*which += npages;
			npages = 0;
		}

		if (p->pcf_wait) {
			mutex_enter(&new_freemem_lock);
			/*
			 * Check to see if some other thread
			 * is actually waiting.  Another bucket
			 * may have woken it up by now.  If there
			 * are no waiters, then set our pcf_wait
			 * count to zero to avoid coming in here
			 * next time.
			 */
			if (freemem_wait) {
				if (npages > 1) {
					cv_broadcast(&freemem_cv);
				} else {
					cv_signal(&freemem_cv);
				}
				p->pcf_wait--;
			} else {
				p->pcf_wait = 0;
			}
			mutex_exit(&new_freemem_lock);
		}
		mutex_exit(&p->pcf_lock);
	}
}


/*
 * A helper routine for page_create_get_something.
 * The indenting got to deep down there.
 * Unblock the pcf counters.  Any pages freed after
 * pcf_block got set are moved to pcf_count and
 * wakeups (cv_broadcast() or cv_signal()) are done as needed.
 */
static void
pcgs_unblock(void)
{
	int		i;
	struct pcf	*p;

	p = pcf;
	for (i = 0; i < PCF_FANOUT; i++) {
		mutex_enter(&p->pcf_lock);
		ASSERT(p->pcf_count == 0);
		p->pcf_count = p->pcf_reserve;
		p->pcf_block = 0;
		if (p->pcf_wait) {
			mutex_enter(&new_freemem_lock);
			if (freemem_wait) {
				if (p->pcf_reserve > 1) {
					cv_broadcast(&freemem_cv);
					p->pcf_wait = 0;
				} else {
					cv_signal(&freemem_cv);
					p->pcf_wait--;
				}
			} else {
				p->pcf_wait = 0;
			}
			mutex_exit(&new_freemem_lock);
		}
		p->pcf_reserve = 0;
		mutex_exit(&p->pcf_lock);
		p++;
	}
}

/*
 * Called from page_create_va() when both the cache and free lists
 * have been checked once.
 *
 * Either returns a page or panics since the accounting was done
 * way before we got here.
 *
 * We don't come here often, so leave the accounting on permanently.
 */

#define	MAX_PCGS	100

#ifdef	DEBUG
#define	PCGS_TRIES	100
#else	DEBUG
#define	PCGS_TRIES	10
#endif	DEBUG

#ifdef	VM_STATS
u_int	pcgs_counts[PCGS_TRIES];
u_int	pcgs_too_many;
u_int	pcgs_entered;
u_int	pcgs_locked;
#endif	VM_STATS

static page_t *
page_create_get_something(vp, off, as, vaddr, flags)
	struct vnode	*vp;
	register u_int	off;
	struct as	*as;
	caddr_t		vaddr;
	u_int		flags;
{
	u_int		count;
	page_t		*pp;
	u_int		locked, i;
	struct	pcf	*p;

	VM_STAT_ADD(pcgs_entered);
	/*
	 * Time to get serious.
	 * We failed to get a `correctly colored' page from both the
	 * free and cache lists.
	 * We escalate in stage.
	 *
	 * First try both lists without worring about color.
	 *
	 * Then, grab all page accounting locks (ie. pcf[]) and
	 * steal any pages that they have and set the pcf_block flag to
	 * stop deletions from the lists.  This will help because
	 * a page can get added to the free list while we are looking
	 * at the cache list, then another page could be added to the cache
	 * list allowing the page on the free list to be removed as we
	 * move from looking at the cache list to the free list. This
	 * could happen over and over. We would never find the page
	 * we have accounted for.
	 */

	flags &= ~PG_MATCH_COLOR;
	locked = 0;

	for (count = 0; count < MAX_PCGS; count++) {
		pp = page_get_freelist(vp, off, as, vaddr, flags);
		if (pp == NULL) {
			pp = page_get_cachelist(vp, off, as, vaddr, flags);
		}

		if (pp == NULL) {
			/*
			 * Serialize.  Don't fight with other pcgs().
			 */
			if (!locked) {
				mutex_enter(&pcgs_lock);
				VM_STAT_ADD(pcgs_locked);
				locked = 1;
				p = pcf;
				for (i = 0; i < PCF_FANOUT; i++) {
					mutex_enter(&p->pcf_lock);
					ASSERT(p->pcf_block == 0);
					p->pcf_block = 1;
					p->pcf_reserve = p->pcf_count;
					p->pcf_count = 0;
					mutex_exit(&p->pcf_lock);
					p++;
				}
			}

			if (count) {
				/*
				 * Since page_free() puts pages on
				 * a list then accounts for it, we
				 * just have to wait for page_free()
				 * to unlock any page it was working
				 * with. The page_lock()-page_reclaim()
				 * path falls in the same boat.
				 *
				 * We don't need to check on the
				 * PG_WAIT flag, we have already
				 * accounted for the page we are
				 * looking for in page_create_va().
				 *
				 * We just wait a moment to let any
				 * locked pages on the lists free up,
				 * then continue around and try again.
				 */
				delay(1);
			}
		} else {
			if (count >= PCGS_TRIES) {
				VM_STAT_ADD(pcgs_too_many);
			} else {
				VM_STAT_ADD(pcgs_counts[count]);
			}
			if (locked) {
				pcgs_unblock();
				mutex_exit(&pcgs_lock);
			}
			return (pp);
		}
	}
	/*
	 * we go down holding the pcf locks.
	 */
	cmn_err(CE_PANIC, "no page found %d", count);
	/*NOTREACHED*/
}

/*
 * Create enough pages for "bytes" worth of data starting at
 * "off" in "vp".
 *
 *	Where flag must be one of:
 *
 *		PG_EXCL:	Exclusive create (fail if any page already
 *				exists in the page cache) which does not
 *				wait for memory to become available.
 *
 *		PG_WAIT:	Non-exclusive create which can wait for
 *				memory to become available.
 *
 *		PG_PHYSCONTIG:	Allocate physically contiguous pages.
 *				(Not Supported)
 *
 * A doubly linked list of pages is returned to the caller.  Each page
 * on the list has the "exclusive" (p_selock) lock and "iolock" (p_iolock)
 * lock.
 *
 * Unable to change the parameters to page_create() in a minor release,
 * we renamed page_create() to page_create_va(), changed all known calls
 * from page_create() to page_create_va(), and created this wrapper.
 *
 * Upon a major release, we should break compatibility by deleting this
 * wrapper, and replacing all the strings "page_create_va", with "page_create".
 */
page_t *
page_create(vp, off, bytes, flags)
	struct vnode *vp;
	register u_int off;
	u_int bytes;
	u_int flags;
{
	caddr_t random_vaddr;

	random_vaddr = (caddr_t)(((u_int)vp >> 7) ^ (off >> PAGESHIFT));

	return (page_create_va(vp, off, bytes, flags, &kas, random_vaddr));
}


page_t *
page_create_va(vp, off, bytes, flags, as, vaddr)
	struct vnode	*vp;
	register u_int	off;
	u_int		bytes;
	u_int		flags;
	struct as	*as;
	caddr_t		vaddr;
{
	page_t		*plist = NULL;
	register int	npages;
	register u_int	found_on_free = 0;
	u_int		pages_req;
	page_t		*npp = NULL;
	u_int		enough;
	u_int		i;
	u_int		pcf_index;
	struct pcf	*p;
	struct pcf	*q;

	TRACE_5(TR_FAC_VM, TR_PAGE_CREATE_START,
		"page_create_start:vp %x off %x bytes %u flags %x freemem %d",
		vp, off, bytes, flags, freemem);

	ASSERT(bytes != 0 && vp != NULL);

	if ((flags & PG_EXCL) == 0 && (flags & PG_WAIT) == 0) {
		cmn_err(CE_PANIC, "page_create: invalid flags");
	}
	ASSERT((flags & ~(PG_EXCL | PG_WAIT)) == 0);	/* but no others */

	pages_req = npages = btopr(bytes);
	/*
	 * Try to see whether request is too large to *ever* be
	 * satisfied, in order to prevent deadlock.  We arbitrarily
	 * decide to limit maximum size requests to max_page_get.
	 */
	if (npages >= max_page_get) {
		if ((flags & PG_WAIT) == 0) {
			TRACE_4(TR_FAC_VM, TR_PAGE_CREATE_TOOBIG,
"page_create_toobig:vp %x off %x npages %d max_page_get %d",
			    vp, off, npages, max_page_get);
			return (NULL);
		} else {
			cmn_err(CE_WARN,
"Request for too much kernel memory (%d bytes), will hang forever\n",
			    bytes);
			for (;;)
				delay(1000000000);
		}
	}

	VM_STAT_ADD(page_create_cnt[0]);

	enough = 0;
	pcf_index = PCF_INDEX();

	p = &pcf[pcf_index];
	p->pcf_touch = 1;
	q = &pcf[PCF_FANOUT];
	for (i = 0; i < PCF_FANOUT; i++) {
		if (p->pcf_count > npages) {
			/*
			 * a good one to try.
			 */
			mutex_enter(&p->pcf_lock);
			if (p->pcf_count > npages) {
				p->pcf_count -= npages;
				freemem -= npages;
				enough = 1;
				mutex_exit(&p->pcf_lock);
				break;
			}
			mutex_exit(&p->pcf_lock);
		}
		p++;
		if (p >= q) {
			p = pcf;
		}
		p->pcf_touch = 1;
	}

	if (!enough) {
		/*
		 * Have to look harder.  If npages is greater than
		 * one, then we might have to coalecse the counters.
		 *
		 * Go wait.  We come back having accounted
		 * for the memory.
		 */
		VM_STAT_ADD(page_create_cnt[1]);
		if (!page_create_wait(npages, flags)) {
			VM_STAT_ADD(page_create_cnt[2]);
			return (NULL);
		}
	}

	TRACE_3(TR_FAC_VM, TR_PAGE_CREATE_SUCCESS,
		"page_create_success:vp %x off %x freemem %d",
		vp, off, freemem);

	/*
	 * If satisfying this request has left us with too little
	 * memory, start the wheels turning to get some back.  The
	 * first clause of the test prevents waking up the pageout
	 * daemon in situations where it would decide that there's
	 * nothing to do.
	 */
	if (nscan < desscan && freemem < minfree) {
		TRACE_1(TR_FAC_VM, TR_PAGEOUT_CV_SIGNAL,
			"pageout_cv_signal:freemem %d", freemem);
		cv_signal(&proc_pageout->p_cv);
	}

	/*
	 * Loop around collecting the requested number of pages.
	 * Most of the time, we have to `create' a new page. With
	 * this in mind, pull the page off the free list before
	 * getting the hash lock.  This will minimize the hash
	 * lock hold time, nesting, and the like.  If it turns
	 * out we don't need the page, we put it back at the end.
	 */
	while (npages--) {
		register page_t	*pp;
		kmutex_t	*phm = NULL;
		u_int		index;

		index = PAGE_HASH_FUNC(vp, off);
top:
		ASSERT(phm == NULL);
		ASSERT(index == PAGE_HASH_FUNC(vp, off));
		ASSERT(MUTEX_NOT_HELD(page_vnode_mutex(vp)));

		if (npp == NULL) {
			/*
			 * Try to get a page from the freelist (ie,
			 * a page with no [vp, off] tag).  If that
			 * fails, use the cachelist.
			 *
			 * During the first attempt at both the free
			 * and cache lists we try for the correct color.
			 */
			/*
			 * XXXX-how do we deal with virtual indexed
			 * caches and and colors?
			 */
			VM_STAT_ADD(page_create_cnt[4]);
			npp = page_get_freelist(vp, off,
			    as, vaddr, flags | PG_MATCH_COLOR);
			if (npp == NULL) {
				npp = page_get_cachelist(vp, off, as,
				    vaddr, flags | PG_MATCH_COLOR);
				if (npp == NULL) {
					npp = page_create_get_something(vp,
					    off, as, vaddr,
					    flags & ~PG_MATCH_COLOR);
				}

				if (npp->p_age == 0) {
					/*
					 * Since this page came from the
					 * cachelist, we must destroy the
					 * old vnode association.
					 */
					page_hashout(npp, (kmutex_t *)NULL);
				}
			}
		}

		/*
		 * We own this page!
		 */
		ASSERT(se_excl_assert(&npp->p_selock));
		ASSERT(npp->p_vnode == NULL);
		ASSERT(npp->p_mapping == NULL);
		npp->p_free = npp->p_age = 0;

		/*
		 * Here we have a page in our hot little mits and are
		 * just waiting to stuff it on the appropriate lists.
		 * Get the mutex and check to see if it really does
		 * not exist.
		 */
		phm = PAGE_HASH_MUTEX(index);
		mutex_enter(phm);
		PAGE_HASH_SEARCH(index, pp, vp, off);
		if (pp == NULL) {
			VM_STAT_ADD(page_create_new);
			pp = npp;
			npp = NULL;
			if (!page_hashin(pp, vp, off, phm)) {
				/*
				 * Since we hold the page hash mutex and
				 * just searched for this page, page_hashin
				 * had better not fail.  If it does, that
				 * means somethread did not follow the
				 * page hash mutex rules.  Panic now and
				 * get it over with.  As usual, go down
				 * holding all the locks.
				 */
				ASSERT(MUTEX_NOT_HELD(phm));
				cmn_err(CE_PANIC,
				    "page_create: hashin failed %x %x %x %x",
				    pp, vp, off, phm);

			}
			ASSERT(MUTEX_NOT_HELD(phm));	/* hashin dropped it */
			phm = NULL;

			/*
			 * Hat layer locking need not be done to set
			 * the following bits since the page is not hashed
			 * and was on the free list (i.e., had no mappings).
			 *
			 * Set the reference bit to protect
			 * against immediate pageout
			 */
			PP_SETREF(pp);
			found_on_free++;
		} else {
			VM_STAT_ADD(page_create_exists);
			if (flags & PG_EXCL) {
				/*
				 * Found an existing page, and the caller
				 * wanted all new pages.  Undo all of the work
				 * we have done.
				 */
				mutex_exit(phm);
				phm = NULL;
				while (plist != NULL) {
					pp = plist;
					page_sub(&plist, pp);
					page_io_unlock(pp);
					/*LINTED: constant in conditional ctx*/
					VN_DISPOSE(pp, B_INVAL, 0, kcred);
				}
				VM_STAT_ADD(page_create_found_one);
				goto fail;
			}
			ASSERT(flags & PG_WAIT);
			if (!page_lock(pp, SE_EXCL, phm, P_NO_RECLAIM)) {
				/*
				 * Start all over again if we blocked trying
				 * to lock the page.
				 */
				mutex_exit(phm);
				VM_STAT_ADD(page_create_page_lock_failed);
				phm = NULL;
				goto top;
			}
			mutex_exit(phm);
			phm = NULL;

			if (pp->p_free) {
				ASSERT(pp->p_age == 0);
				VM_STAT_ADD(pagecnt.pc_get_cache);
				page_list_sub(PG_CACHE_LIST, pp);
				pp->p_free = 0;
				found_on_free++;
			}
		}

		/*
		 * Got a page!  It is locked.  Acquire the i/o
		 * lock since we are going to use the p_next and
		 * p_prev fields to link the requested pages together.
		 */
		page_io_lock(pp);
		page_add(&plist, pp);
		plist = plist->p_next;
		off += PAGESIZE;
		vaddr += PAGESIZE;
	}

	ASSERT((flags & PG_EXCL) ? (found_on_free == pages_req) : 1);
fail:
	if (npp != NULL) {
		/*
		 * Did not need this page after all.
		 * Put it back on the free list.
		 */
		VM_STAT_ADD(page_create_putbacks);
		npp->p_free = npp->p_age = 1;
		npp->p_offset = (u_int)-1;
		page_list_add(PG_FREE_LIST, npp, PG_LIST_TAIL);
		page_unlock(npp);

	}

	ASSERT(pages_req >= found_on_free);

	{
		u_int overshoot = pages_req - found_on_free;

		if (overshoot) {
			VM_STAT_ADD(page_create_overshoot);
			p = &pcf[pcf_index];
			p->pcf_touch = 1;
			mutex_enter(&p->pcf_lock);
			if (p->pcf_block) {
				p->pcf_reserve += overshoot;
			} else {
				p->pcf_count += overshoot;
				if (p->pcf_wait) {
					mutex_enter(&new_freemem_lock);
					if (freemem_wait) {
						cv_signal(&freemem_cv);
						p->pcf_wait--;
					} else {
						p->pcf_wait = 0;
					}
					mutex_exit(&new_freemem_lock);
				}
			}
			mutex_exit(&p->pcf_lock);
			freemem += overshoot;
		}
	}
	fault_type = 0;		/* ugly Sun4/110 advisory coloring hook */

	return (plist);
}

/*
 * Put page on the "free" list.
 * The free list is really two lists maintained by
 * the PSM of whatever machine we happen to be on.
 */
void
page_free(pp, dontneed)
	register page_t *pp;
	int dontneed;
{
	struct pcf	*p;
	u_int		pcf_index;

	ASSERT((se_excl_assert(&pp->p_selock) &&
	    !page_iolock_assert(pp)) || panicstr);

	if (pp->p_free)
		cmn_err(CE_PANIC, "page_free: page %x is free",
		    (int)pp);

	/*
	 * The page_struct_lock need not be acquired to examine these
	 * fields since the page has an "exclusive" lock.
	 */
	if (pp->p_mapping != NULL || pp->p_lckcnt != 0 || pp->p_cowcnt != 0)
		cmn_err(CE_PANIC, "page_free");

	ASSERT(pp->p_share == 0);

	pp->p_free = 1;
	pp->p_nrm = 0;
	ASSERT(pp->p_share == 0);

	/*
	 * Now we add the page to the head of the free list.
	 * But if this page is associated with a paged vnode
	 * then we adjust the head forward so that the page is
	 * effectively at the end of the list.
	 */
	if (pp->p_vnode == NULL) {
		/*
		 * Page has no identity, put it on the free list.
		 */
		pp->p_age = 1;
		pp->p_offset = (u_int)-1;
		page_list_add(PG_FREE_LIST, pp, PG_LIST_TAIL);
		VM_STAT_ADD(pagecnt.pc_free_free);
		TRACE_5(TR_FAC_VM, TR_PAGE_FREE_FREE,
			"page_free_free:\
pp %x vp %x off %x dontneed %x freemem %x",
			pp, pp->p_vnode, pp->p_offset, dontneed, freemem);
	} else {
		pp->p_age = 0;

		if (!dontneed || nopageage) {
			/* move it to the tail of the list */
			page_list_add(PG_CACHE_LIST, pp, PG_LIST_TAIL);

			VM_STAT_ADD(pagecnt.pc_free_cache);
			TRACE_5(TR_FAC_VM, TR_PAGE_FREE_CACHE_TAIL,
				"page_free_cache_tail:\
pp %x vp %x off %x dontneed %x freemem %x",
				pp, pp->p_vnode, pp->p_offset,
				dontneed, freemem);
		} else {
			page_list_add(PG_CACHE_LIST, pp, PG_LIST_HEAD);

			VM_STAT_ADD(pagecnt.pc_free_dontneed);
			TRACE_5(TR_FAC_VM, TR_PAGE_FREE_CACHE_HEAD,
				"page_free_cache_head:\
pp %x vp %x off %x dontneed %x freemem %x",
				pp, pp->p_vnode, pp->p_offset,
				dontneed, freemem);
		}
	}
	page_unlock(pp);

	/*
	 * Now do the `freemem' accounting.
	 */
	pcf_index = PCF_INDEX();
	p = &pcf[pcf_index];
	p->pcf_touch = 1;

	mutex_enter(&p->pcf_lock);
	if (p->pcf_block) {
		p->pcf_reserve += 1;
	} else {
		p->pcf_count += 1;
		if (p->pcf_wait) {
			mutex_enter(&new_freemem_lock);
			/*
			 * Check to see if some other thread
			 * is actually waiting.  Another bucket
			 * may have woken it up by now.  If there
			 * are no waiters, then set our pcf_wait
			 * count to zero to avoid coming in here
			 * next time.  Also, since only one page
			 * was put on the free list, just wake
			 * up one waiter.
			 */
			if (freemem_wait) {
				cv_signal(&freemem_cv);
				p->pcf_wait--;
			} else {
				p->pcf_wait = 0;
			}
			mutex_exit(&new_freemem_lock);
		}
	}
	mutex_exit(&p->pcf_lock);

	freemem += 1;
}

static int free_pages = 0;

/*
 * XXX - Turn this into a MACRO.
 *
 * It also looks like only segvn_unmap and segvn_free
 * call it.
 */
void
free_vp_pages(vp, off, len)
	register struct vnode *vp;
	register u_int off;
	u_int len;
{
	extern int swap_in_range();
	register page_t *pp, *epp;
	register u_int eoff;
	kmutex_t	*vphm;

	eoff = off + len;

	if (free_pages == 0)
		return;
	if (swap_in_range(vp, off, len))
		return;

	/*
	 * Acquire the appropriate vnode hash lock since
	 * we are going through the "vp" list.
	 */
	vphm = page_vnode_mutex(vp);
	mutex_enter(vphm);
	if ((pp = epp = vp->v_pages) != 0) {
		do {
			ASSERT(pp->p_vnode == vp);
			if (pp->p_offset < off || pp->p_offset >= eoff)
				continue;
			/* XXX - somebody needs to handle these (pageout) */
			if (PP_ISMOD(pp) || pp->p_mapping)
				continue;

			if (!page_trylock(pp, SE_EXCL))
				continue;
			else {
				if (pp->p_free) {
					page_unlock(pp);
					continue;
				}
			}

			/*
			 * The page_struct_lock need not be acquired to
			 * examine these fields since the page has an
			 * "exclusive" lock.
			 */
			if (pp->p_lckcnt != 0 || pp->p_cowcnt != 0 ||
			    pp->p_mapping) {
				page_unlock(pp);
				continue;
			}
			/*LINTED: constant in conditional context*/
			VN_DISPOSE(pp, B_FREE, 0, kcred);
		} while ((pp = pp->p_vpnext) != epp);
	}
	mutex_exit(vphm);
}

/*
 * Reclaim the given page from the free list.
 * Returns 1 on success or 0 on failure.
 *
 * The page is unlocked if it can't be reclaimed (when freemem == 0).
 * If `lock' is non-null, it will be dropped and re-acquired if
 * the routine must wait while freemem is 0.
 *
 * As it turns out, boot_getpages() does this.  It picks a page,
 * based on where OBP mapped in some address, gets its pfn, searches
 * the memsegs, locks the page, then pulls it off the free list!
 */
int
page_reclaim(pp, lock)
	register page_t *pp;
	kmutex_t *lock;
{
	struct pcf	*p;
	u_int		pcf_index;
	struct cpu	*cpup;
	int		enough;
	u_int		i;

	ASSERT(lock != NULL ? MUTEX_HELD(lock) : 1);
	ASSERT(se_excl_assert(&pp->p_selock) && pp->p_free);

	/*
	 * If `freemem' is 0, we cannot reclaim this page from the
	 * freelist, so release every lock we might hold: the page,
	 * and the `lock' before blocking.
	 *
	 * The only way `freemem' can become 0 while there are pages
	 * marked free (have their p->p_free bit set) is when the
	 * system is low on memory and doing a page_create().  In
	 * order to guarantee that once page_create() starts acquiring
	 * pages it will be able to get all that it needs since `freemem'
	 * was decreased by the requested amount.  So, we need to release
	 * this page, and let page_create() have it.
	 *
	 * Since `freemem' being zero is not supposed to happen, just
	 * use the usual hash stuff as a starting point.  If that bucket
	 * is empty, then assume the worst, and start at the beginning
	 * of the pcf array.  If we always start at the beginning
	 * when acquiring more than one pcf lock, there won't be any
	 * deadlock problems.
	 */
	enough = 0;
	pcf_index = PCF_INDEX();
	p = &pcf[pcf_index];
	p->pcf_touch = 1;
	mutex_enter(&p->pcf_lock);
	if (p->pcf_count >= 1) {
		enough = 1;
		p->pcf_count--;
	}
	mutex_exit(&p->pcf_lock);

	if (!enough) {
		VM_STAT_ADD(page_reclaim_zero);
		/*
		 * Check again. Its possible that some other thread
		 * could have been right behind us, and added one
		 * to a list somewhere.  Acquire each of the pcf locks
		 * until we find a page.
		 */
		p = pcf;
		for (i = 0; i < PCF_FANOUT; i++) {
			p->pcf_touch = 1;
			mutex_enter(&p->pcf_lock);
			if (p->pcf_count >= 1) {
				p->pcf_count -= 1;
				enough = 1;
				break;
			}
			p++;
		}

		if (!enough) {
			/*
			 * We really can't have page `pp'.
			 * Time for the no-memory dance with
			 * page_free().  This is just like
			 * page_create_wait().  Plus the added
			 * attraction of releasing whatever mutex
			 * we held when we were called with in `lock'.
			 * Page_unlock() will wakeup any thread
			 * waiting around for this page.
			 */
			if (lock) {
				VM_STAT_ADD(page_reclaim_zero_locked);
				mutex_exit(lock);
			}
			page_unlock(pp);

			/*
			 * get this before we drop all the pcf locks.
			 */
			mutex_enter(&new_freemem_lock);

			p = pcf;
			for (i = 0; i < PCF_FANOUT; i++) {
				p->pcf_wait++;
				mutex_exit(&p->pcf_lock);
				p++;
			}

			freemem_wait++;
			cv_wait(&freemem_cv, &new_freemem_lock);
			freemem_wait--;

			mutex_exit(&new_freemem_lock);

			if (lock) {
				mutex_enter(lock);
			}
			return (0);
		}

		/*
		 * There was a page to be found.
		 * The pcf accounting has been done,
		 * though none of the pcf_wait flags have been set,
		 * drop the locks and continue on.
		 */
		while (p >= pcf) {
			mutex_exit(&p->pcf_lock);
			p--;
		}
	}
	freemem -= 1;

	VM_STAT_ADD(pagecnt.pc_reclaim);
	if (pp->p_age) {
		page_list_sub(PG_FREE_LIST, pp);
		TRACE_3(TR_FAC_VM, TR_PAGE_UNFREE_FREE,
		    "page_reclaim_free:pp %x age %x freemem %d",
		    pp, pp->p_age, freemem);
	} else {
		page_list_sub(PG_CACHE_LIST, pp);
		TRACE_2(TR_FAC_VM, TR_PAGE_UNFREE_CACHE,
		    "page_reclaim_cache:pp %x age %x",
		    pp, pp->p_age);
	}

	/*
	 * clear the p_free & p_age bits since this page is no longer
	 * on the free list.  Notice that there was a brief time where
	 * a page is marked as free, but is not on the list.
	 *
	 * Set the reference bit to protect against immediate pageout.
	 */
	pp->p_free = pp->p_age = 0;
	PP_SETREF(pp);

	CPU_STAT_ENTER_K();
	cpup = CPU;	/* get cpup now that CPU cannot change */
	CPU_STAT_ADDQ(cpup, cpu_vminfo.pgrec, 1);
	CPU_STAT_ADDQ(cpup, cpu_vminfo.pgfrec, 1);
	CPU_STAT_EXIT_K();

	return (1);
}



/*
 * Destroy identity of the page and put it back on
 * the page free list.  Assumes that the caller has
 * acquired the "exclusive" lock on the page.
 */
void
page_destroy(pp, dontfree)
	register page_t *pp;
	int dontfree;
{
	ASSERT((se_excl_assert(&pp->p_selock) &&
	    !page_iolock_assert(pp)) || panicstr);

	TRACE_3(TR_FAC_VM, TR_PAGE_DESTROY,
		"page_destroy:pp %x vp %x offset %x",
		pp, pp->p_vnode, pp->p_offset);

	/*
	 * Unload translations, if any, then hash out the
	 * page to erase its identity.
	 */
	hat_pageunload(pp);
	page_hashout(pp, (kmutex_t *)NULL);

	if (!dontfree) {
		/*
		 * Acquire the "freemem_lock" for availsmem, availrmem
		 * and pages_pp_locked. The page_struct_lock need not be
		 * acquired for lckcnt and cowcnt since the page has an
		 * "exclusive" lock.
		 */
		if ((pp->p_lckcnt != 0) || (pp->p_cowcnt != 0)) {
			mutex_enter(&freemem_lock);
			if (pp->p_lckcnt != 0) {
				pages_pp_locked--;
				availrmem++;
				pp->p_lckcnt = 0;
			}
			if (pp->p_cowcnt != 0) {
				pages_pp_locked -= pp->p_cowcnt;
				availrmem += pp->p_cowcnt;
				pp->p_cowcnt = 0;
			}
			mutex_exit(&freemem_lock);
		}
		/*
		 * Put the page on the "free" list.
		 */
		page_free(pp, 0);
	}
}

/*
 * Similar to page_destroy(), but destroys pages which are
 * locked and known to be on the page free list.  Since
 * the page is known to be free and locked, no one can access
 * it.
 *
 * Also, the number of free pages does not change.
 */
void
page_destroy_free(pp)
	register page_t *pp;
{
	ASSERT(se_excl_assert(&pp->p_selock));
	ASSERT(pp->p_free);
	ASSERT(pp->p_vnode);
	ASSERT(pp->p_nrm == 0);
	ASSERT(pp->p_mapping == NULL);
	ASSERT(pp->p_age == 0);

	VM_STAT_ADD(pagecnt.pc_destroy_free);
	page_list_sub(PG_CACHE_LIST, pp);

	page_hashout(pp, (kmutex_t *)NULL);
	ASSERT(pp->p_vnode == NULL);
	ASSERT(pp->p_offset == (u_int)-1);
	ASSERT(pp->p_hash == NULL);

	pp->p_age = 1;
	page_list_add(PG_FREE_LIST, pp, PG_LIST_TAIL);
	page_unlock(pp);

	mutex_enter(&new_freemem_lock);
	if (freemem_wait) {
		cv_signal(&freemem_cv);
	}
	mutex_exit(&new_freemem_lock);
}

/*
 * Rename the page "opp" to have an identity specified
 * by [vp, off].  If a page already exists with this name
 * it is locked and destroyed.  Note that the page's
 * translations are not unloaded during the rename.
 *
 * This routine is used by the anon layer to "steal" the
 * original page and is not unlike destroying a page and
 * creating a new page using the same page frame.
 *
 * XXX -- Could deadlock if caller 1 tries to rename A to B while
 * caller 2 tries to rename B to A.
 */
void
page_rename(opp, vp, off)
	register page_t *opp;
	struct vnode *vp;
	u_int off;
{
	register page_t	*pp;
	int		olckcnt = 0;
	int		ocowcnt = 0;
	kmutex_t	*phm;
	u_int		index;

	ASSERT(se_excl_assert(&opp->p_selock) && !page_iolock_assert(opp));
	ASSERT(MUTEX_NOT_HELD(page_vnode_mutex(vp)));
	ASSERT(opp->p_free == 0);

	VM_STAT_ADD(page_rename_count);

	TRACE_4(TR_FAC_VM, TR_PAGE_RENAME,
		"page rename:oldvp %x oldoff %x vp %x off %x",
		opp->p_vnode, opp->p_offset, vp, off);

	page_hashout(opp, (kmutex_t *)NULL);
	opp->p_age = 0;

	/*
	 * Acquire the appropriate page hash lock, since
	 * we're going to rename the page.
	 */
	index = PAGE_HASH_FUNC(vp, off);
	phm = PAGE_HASH_MUTEX(index);
	mutex_enter(phm);
top:
	/*
	 * Look for an existing page with this name and destroy it if found.
	 * By holding the page hash lock all the way to the page_hashin()
	 * call, we are assured that no page can be created with this
	 * identity.  In the case when the phm lock is dropped to undo any
	 * hat layer mappings, the existing page is held with an "exclusive"
	 * lock, again preventing another page from being created with
	 * this identity.
	 */
	PAGE_HASH_SEARCH(index, pp, vp, off);
	if (pp != NULL) {
		VM_STAT_ADD(page_rename_exists);

		/*
		 * As it turns out, this is one of only two places where
		 * page_lock() needs to hold the passed in lock in the
		 * successful case.  In all of the others, the lock could
		 * be dropped as soon as the attempt is made to lock
		 * the page.  It is tempting to add yet another arguement,
		 * PL_KEEP or PL_DROP, to let page_lock know what to do.
		 */
		if (!page_lock(pp, SE_EXCL, phm, P_RECLAIM)) {
			/*
			 * Went to sleep because the page could not
			 * be locked.  We were woken up when the page
			 * was unlocked, or when the page was destroyed.
			 * In either case, `phm' was dropped while we
			 * slept.  Hence we should not just roar through
			 * this loop.
			 */
			goto top;
		}

		if (pp->p_mapping) {
			/*
			 * Unload translations.  Since we hold the
			 * exclusive lock on this page, the page
			 * can not be changed while we drop phm.
			 * This is also not a lock protocol violation,
			 * but rather the proper way to do things.
			 */
			mutex_exit(phm);
			hat_pageunload(pp);
			mutex_enter(phm);
		}
		page_hashout(pp, phm);
	}
	/*
	 * Hash in the page with the new identity.
	 * page_hashin drops phm.
	 */
	if (!page_hashin(opp, vp, off, phm)) {
		/*
		 * We were holding phm while we searched for [vp, off]
		 * and only dropped phm if we found and locked a page.
		 * If we can't create this page now, then some thing
		 * is really broken.
		 */
		cmn_err(CE_PANIC, "page_rename: Can't hash in page: %x",
		    (int)pp);
	}

	ASSERT(MUTEX_NOT_HELD(phm));

	/*
	 * Now that we have dropped phm, lets get around to finishing up
	 * with pp.
	 */
	if (pp != NULL) {
		ASSERT(pp->p_mapping == NULL);
		/*
		 * Save the locks for transfer to the new page and then
		 * clear them so page_free doesn't think they're important.
		 * The page_struct_lock need not be acquired for lckcnt and
		 * cowcnt since the page has an "exclusive" lock.
		 */
		olckcnt = pp->p_lckcnt;
		ocowcnt = pp->p_cowcnt;
		pp->p_lckcnt = pp->p_cowcnt = 0;

		/*
		 * Put the page on the "free" list after we drop
		 * the lock.  The less work under the lock the better.
		 */
		/*LINTED: constant in conditional context*/
		VN_DISPOSE(pp, B_FREE, 0, kcred);
	}

	/*
	 * Transfer the lock count from the old page (if any).
	 * The page_struct_lock need not be acquired for lckcnt and
	 * cowcnt since the page has an "exclusive" lock.
	 */
	opp->p_lckcnt += olckcnt;
	opp->p_cowcnt += ocowcnt;
}

/*
 * Add page `pp' to both the hash and vp chains for [vp, offset].
 *
 * It is required that pages be added to the end of the vplist
 * in order for pvn_vplist_dirty to work properly.
 *
 * Returns 1 on success and 0 on failure.
 * If phm is non-NULL, it is dropped.
 */
int
page_hashin(pp, vp, offset, phm)
	register page_t *pp;
	register struct vnode *vp;
	u_int offset;
	kmutex_t *phm;
{
	register page_t	**hpp;
	register page_t	*tp;
	kmutex_t	*vphm;
	u_int		index;

	ASSERT(se_excl_assert(&pp->p_selock));
	ASSERT(vp != NULL);

	/*
	 * Technically, if phm was non-NULL and held, it would be fine
	 * if the vnode mutex was also held.  But since we are going
	 * to pick up the vnode mutex below, it had better not be held now.
	 */
	ASSERT(MUTEX_NOT_HELD(page_vnode_mutex(vp)));

	TRACE_3(TR_FAC_VM, TR_PAGE_HASHIN,
		"page_hashin:pp %x vp %x offset %x",
		pp, vp, offset);

	VM_STAT_ADD(hashin_count);
	/*
	 * Be sure to set these up before the page is inserted on the hash
	 * list.  As soon as the page is placed on the list some other
	 * thread might get confused and wonder how this page could
	 * possibly hash to this list.
	 */
	pp->p_vnode = vp;
	pp->p_offset = offset;

	index = PAGE_HASH_FUNC(vp, offset);
	hpp = &page_hash[index];

	if (phm == NULL) {
		VM_STAT_ADD(hashin_not_held);
		phm = PAGE_HASH_MUTEX(index);
		mutex_enter(phm);
	}
	ASSERT(phm == PAGE_HASH_MUTEX(index));
	ASSERT(MUTEX_HELD(phm));

	for (tp = *hpp; tp != NULL; tp = tp->p_hash) {
		if (tp->p_vnode == vp && tp->p_offset == offset) {
			/*
			 * This page is already here!
			 */
			mutex_exit(phm);
			VM_STAT_ADD(hashin_already);
			pp->p_vnode = NULL;
			pp->p_offset = offset;
			return (0);
		}
	}

	pp->p_hash = *hpp;
	*hpp = pp;
	mutex_exit(phm);

	/*
	 * Add the page to the end of the linked list of pages.
	 * Assuming that we are traversing the vnode in forward,
	 * sequential order, this will leave the page list sorted.
	 */
	vphm = page_vnode_mutex(vp);
	mutex_enter(vphm);
	if (vp->v_pages)
		page_vpadd(&vp->v_pages->p_vpprev->p_vpnext, pp);
	else
		page_vpadd(&vp->v_pages, pp);
	mutex_exit(vphm);
	return (1);
}

/*
 * Adds a page structure representing the physical page
 * to the hash/vp chains for [vp, offset].
 *
 * Returns 1 on success and 0 on failure.
 */
int
page_num_hashin(pfn, vp, offset)
	register u_int pfn;
	struct vnode *vp;
	register u_int offset;
{
	register page_t *pp;
	register int ret;

	/*
	 * Obtain the page structure representing the physical page.
	 */
	pp = page_numtopp(pfn, SE_EXCL);
	if (pp == NULL || pp->p_free)
		return (0);

	ret = page_hashin(pp, vp, offset, (kmutex_t *)NULL);
	return (ret);
}

/*
 * Remove page ``pp'' from the hash and vp chains and remove vp association.
 *
 * When `phm' is non-NULL it contains the address of the mutex protecting the
 * hash list pp is on.  It is not dropped.
 */
void
page_hashout(pp, phm)
	register page_t *pp;
	kmutex_t *phm;
{
	register page_t **hpp, *hp;
	register struct vnode *vp;
	u_int		index;
	kmutex_t	*nphm;
	kmutex_t	*vphm;
	kmutex_t	*sep;

	ASSERT(phm != NULL ? MUTEX_HELD(phm) : 1);
	ASSERT(pp->p_vnode != NULL);
	ASSERT((se_excl_assert(&pp->p_selock) &&
	    !page_iolock_assert(pp)) || panicstr);
	ASSERT(MUTEX_NOT_HELD(page_vnode_mutex(pp->p_vnode)));

	vp = pp->p_vnode;

	TRACE_3(TR_FAC_VM, TR_PAGE_HASHOUT,
		"page_hashout:pp %x vp %x offset %x",
		pp, vp, pp->p_offset);

	/* Kernel probe */
	TNF_PROBE_2(page_unmap, "vm pagefault", /* CSTYLED */,
	    tnf_opaque, vnode, vp,
	    tnf_offset, offset, pp->p_offset);

	/*
	 * First, take pp off of its hash chain.
	 */
	VM_STAT_ADD(hashout_count);
	index = PAGE_HASH_FUNC(vp, pp->p_offset);
	hpp = &page_hash[index];
	if (phm == NULL) {
		VM_STAT_ADD(hashout_not_held);
		nphm = PAGE_HASH_MUTEX(index);
		mutex_enter(nphm);
	}
	ASSERT(phm ? phm == PAGE_HASH_MUTEX(index) : 1);

	for (;;) {
		hp = *hpp;
		if (hp == pp)
			break;
		if (hp == NULL)
			cmn_err(CE_PANIC, "page_hashout");
		hpp = &hp->p_hash;
	}
	*hpp = pp->p_hash;

	if (phm == NULL)
		mutex_exit(nphm);

	/*
	 * Then, remove it from its associated vnode.
	 */
	vphm = page_vnode_mutex(vp);
	mutex_enter(vphm);
	if (vp->v_pages)
		page_vpsub(&vp->v_pages, pp);
	mutex_exit(vphm);

	pp->p_hash = NULL;
	pp->p_nrm = 0;
	pp->p_vnode = NULL;
	pp->p_offset = (u_int)-1;

	/*
	 * Wake up processes waiting for this page.  The page's
	 * identity has been changed, and is probably not the
	 * desired page any longer.
	 */
	sep = page_se_mutex(pp);
	mutex_enter(sep);
	cv_broadcast(&pp->p_cv);
	mutex_exit(sep);
}

/*
 * Add the page to the front of a linked list of pages
 * using the p_next & p_prev pointers for the list.
 * The caller is responsible for protecting the list pointers.
 */
void
page_add(ppp, pp)
	register page_t **ppp, *pp;
{
	ASSERT(se_excl_assert(&pp->p_selock) ||
	    (se_shared_assert(&pp->p_selock) && page_iolock_assert(pp)));

	if (*ppp == NULL) {
		pp->p_next = pp->p_prev = pp;
	} else {
		pp->p_next = *ppp;
		pp->p_prev = (*ppp)->p_prev;
		(*ppp)->p_prev = pp;
		pp->p_prev->p_next = pp;
	}
	*ppp = pp;
}

/*
 * Remove this page from a linked list of pages
 * using the p_next & p_prev pointers for the list.
 *
 * The caller is responsible for protecting the list pointers.
 */
void
page_sub(ppp, pp)
	register page_t **ppp, *pp;
{
	ASSERT((pp->p_free) ? 1 :
	    (se_excl_assert(&pp->p_selock)) ||
	    (se_shared_assert(&pp->p_selock) && page_iolock_assert(pp)));

	if (*ppp == NULL || pp == NULL)
		cmn_err(CE_PANIC, "page_sub");

	if (*ppp == pp)
		*ppp = pp->p_next;		/* go to next page */

	if (*ppp == pp)
		*ppp = NULL;			/* page list is gone */
	else {
		pp->p_prev->p_next = pp->p_next;
		pp->p_next->p_prev = pp->p_prev;
	}
	pp->p_prev = pp->p_next = pp;		/* make pp a list of one */
}

/*
 * Break page list cppp into two lists with npages in the first list.
 * The tail is returned in nppp.
 */
void
page_list_break(oppp, nppp, npages)
	page_t **oppp, **nppp;
	u_int npages;
{
	page_t *s1pp = *oppp;
	page_t *s2pp;
	page_t *e1pp, *e2pp;
	int n = 0;

	if (s1pp == NULL) {
		*nppp = NULL;
		return;
	}
	if (npages == 0) {
		*nppp = s1pp;
		*oppp = NULL;
		return;
	}
	for (n = 0, s2pp = *oppp; n < npages; n++) {
		s2pp = s2pp->p_next;
	}
	/* Fix head and tail of new lists */
	e1pp = s2pp->p_prev;
	e2pp = s1pp->p_prev;
	s1pp->p_prev = e1pp;
	e1pp->p_next = s1pp;
	s2pp->p_prev = e2pp;
	e2pp->p_next = s2pp;

	/* second list empty */
	if (s2pp == s1pp) {
		*oppp = s1pp;
		*nppp = NULL;
	} else {
		*oppp = s1pp;
		*nppp = s2pp;
	}
}

/*
 * Concatenate page list nppp onto the end of list ppp.
 */
void
page_list_concat(ppp, nppp)
	page_t **ppp, **nppp;
{
	page_t *s1pp, *s2pp, *e1pp, *e2pp;

	if (*nppp == NULL) {
		return;
	}
	if (*ppp == NULL) {
		*ppp = *nppp;
		return;
	}
	s1pp = *ppp;
	e1pp =  s1pp->p_prev;
	s2pp = *nppp;
	e2pp = s2pp->p_prev;
	s1pp->p_prev = e2pp;
	e2pp->p_next = s1pp;
	e1pp->p_next = s2pp;
	s2pp->p_prev = e1pp;
}

/*
 * Add the page to the front of the linked list of pages
 * using p_vpnext/p_vpprev pointers for the list.
 *
 * The caller is responsible for protecting the lists.
 */
void
page_vpadd(ppp, pp)
	register page_t **ppp, *pp;
{
	if (*ppp == NULL) {
		pp->p_vpnext = pp->p_vpprev = pp;
	} else {
		pp->p_vpnext = *ppp;
		pp->p_vpprev = (*ppp)->p_vpprev;
		(*ppp)->p_vpprev = pp;
		pp->p_vpprev->p_vpnext = pp;
	}
	*ppp = pp;
}

/*
 * Remove this page from the linked list of pages
 * using p_vpnext/p_vpprev pointers for the list.
 *
 * The caller is responsible for protecting the lists.
 */
void
page_vpsub(ppp, pp)
	register page_t **ppp, *pp;
{
	if (*ppp == NULL || pp == NULL)
		cmn_err(CE_PANIC, "page_vpsub");

	if (*ppp == pp)
		*ppp = pp->p_vpnext;		/* go to next page */

	if (*ppp == pp)
		*ppp = NULL;			/* page list is gone */
	else {
		pp->p_vpprev->p_vpnext = pp->p_vpnext;
		pp->p_vpnext->p_vpprev = pp->p_vpprev;
	}
	pp->p_vpprev = pp->p_vpnext = pp;	/* make pp a list of one */
}

/*
 * Lock a physical page into memory "long term".  Used to support "lock
 * in memory" functions.  Accepts the page to be locked, and a cow variable
 * to indicate whether a the lock will travel to the new page during
 * a potential copy-on-write.
 */
/*ARGSUSED*/
int
page_pp_lock(pp, cow, kernel)
	page_t *pp;			/* page to be locked */
	int cow;			/* cow lock */
	int kernel;			/* must succeed -- ignore checking */
{
	int r = 0;			/* result -- assume failure */

	ASSERT(se_assert(&pp->p_selock));

	page_struct_lock(pp);
	/*
	 * Acquire the "freemem_lock" for pages_pp_[locked, kernel]
	 * and availrmem.
	 */
	if (cow) {
		mutex_enter(&freemem_lock);
		if ((availrmem - 1) >= pages_pp_maximum) {
			availrmem--;
			pages_pp_locked++;
			if (pp->p_cowcnt < (u_short) PAGE_LOCK_MAXIMUM) {
				if (++pp->p_cowcnt == PAGE_LOCK_MAXIMUM) {
					cmn_err(CE_WARN,
					"Page frame 0x%x locked permanently\n",
					    page_pptonum(pp));
				}
			}
			r = 1;
		}
		mutex_exit(&freemem_lock);
	} else {
		if (pp->p_lckcnt) {
			if (pp->p_lckcnt < (u_short) PAGE_LOCK_MAXIMUM)
				if (++pp->p_lckcnt == PAGE_LOCK_MAXIMUM)
					cmn_err(CE_WARN,
					"Page frame 0x%x locked permanently\n",
					    page_pptonum(pp));
			r = 1;
		} else {
			mutex_enter(&freemem_lock);
			if (kernel) {
				/* availrmem accounting done by caller */
				pages_pp_kernel++;
				++pp->p_lckcnt;
				r = 1;
			} else if ((availrmem - 1) >= pages_pp_maximum) {
				pages_pp_locked++;
				--availrmem;
				++pp->p_lckcnt;
				r = 1;
			}
			mutex_exit(&freemem_lock);
		}
	}
	page_struct_unlock(pp);
	return (r);
}

/*
 * Decommit a lock on a physical page frame.  Account for cow locks if
 * appropriate.
 */
/*ARGSUSED*/
void
page_pp_unlock(pp, cow, kernel)
	page_t *pp;			/* page to be unlocked */
	int cow;			/* expect cow lock */
	int kernel;			/* this was a kernel lock */
{
	ASSERT(se_assert(&pp->p_selock));

	page_struct_lock(pp);
	/*
	 * Acquire the "freemem_lock" for pages_pp_[locked, kernel]
	 * and availrmem.
	 * If cowcnt or lcknt is already 0 do nothing; i.e., we
	 * could be called to unlock even if nothing is locked. This could
	 * happen if locked file pages were truncated (removing the lock)
	 * and the file was grown again and new pages faulted in; the new
	 * pages are unlocked but the segment still thinks they're locked.
	 */
	if (cow) {
		if (pp->p_cowcnt) {
			mutex_enter(&freemem_lock);
			pp->p_cowcnt--;
			pages_pp_locked--;
			availrmem++;
			mutex_exit(&freemem_lock);
		}
	} else {
		if (pp->p_lckcnt && --pp->p_lckcnt == 0) {
			mutex_enter(&freemem_lock);
			if (kernel) {
				pages_pp_kernel--;
			} else {
				pages_pp_locked--;
				availrmem++;
			}
			mutex_exit(&freemem_lock);
		}
	}
	page_struct_unlock(pp);
}

/*
 * Transfer a cow lock to a real lock on a physical page.  Used after a
 * copy-on-write of a locked page has occurred.  "npp" will be "opp" if
 * anon_private() is stealing the original page, so don't attempt to lock
 * "npp".
 */
void
page_pp_useclaim(opp, npp, cow)
	page_t *opp;		/* original page frame losing lock */
	page_t *npp;		/* new page frame gaining lock */
	int cow;		/* expect cowcnt instead of lckcnt */
{
	ASSERT(se_assert(&opp->p_selock));

	page_struct_lock(opp);

	/* Don't use claim if nothing is locked (see page_pp_unlock above) */
	if (cow && !opp->p_cowcnt || !cow && !opp->p_lckcnt) {
		page_struct_unlock(opp);
		return;
	}
	ASSERT((npp != opp) ? se_assert(&npp->p_selock) : 1);

	npp->p_lckcnt++;

	if (cow) {
		ASSERT(opp->p_cowcnt != 0);
		opp->p_cowcnt--;
	} else {
		ASSERT(opp->p_lckcnt != 0);
		opp->p_lckcnt--;
	}
	page_struct_unlock(opp);
}

/*
 * Simple claim adjust functions -- used to support changes in
 * claims due to changes in access permissions.  Used by segvn_setprot().
 */
int
page_addclaim(pp)
	page_t *pp;
{
	int r = 1;			/* result */

	ASSERT(se_assert(&pp->p_selock));

	page_struct_lock(pp);
	ASSERT(pp->p_lckcnt != 0);

	if (--pp->p_lckcnt == 0) {
		pp->p_cowcnt++;
	} else {
		mutex_enter(&freemem_lock);
		if ((availrmem - 1) >= pages_pp_maximum) {
			--availrmem;
			pages_pp_locked++;
			if (pp->p_cowcnt < (u_short) PAGE_LOCK_MAXIMUM)
				if (++pp->p_cowcnt == PAGE_LOCK_MAXIMUM)
					cmn_err(CE_WARN,
					"Page frame 0x%x locked permanently\n",
						page_pptonum(pp));
		} else {
			pp->p_lckcnt++;
			r = 0;
		}
		mutex_exit(&freemem_lock);
	}
	page_struct_unlock(pp);
	return (r);
}

void
page_subclaim(pp)
	page_t *pp;
{
	ASSERT(se_assert(&pp->p_selock));

	page_struct_lock(pp);
	ASSERT(pp->p_cowcnt != 0);

	pp->p_cowcnt--;
	if (pp->p_lckcnt) {
		/*
		 * for pages_pp_locked and availrmem
		 */
		mutex_enter(&freemem_lock);
		availrmem++;
		pages_pp_locked--;
		mutex_exit(&freemem_lock);
	}
	pp->p_lckcnt++;
	page_struct_unlock(pp);
}

/*
 * Simple functions to transform a page pointer to a
 * physical page number and vice versa.  They search the memory
 * segments to locate the desired page.  Within a segment, pages
 * increase linearly with one page structure per PAGESIZE / MMU_PAGESIZE
 * physical page frame.  The search begins with the segment that was
 * accessed last, to take advantage of locality.
 */
u_int
page_pptonum(pp)
	page_t *pp;
{
	return (pp->p_pagenum);
}

static page_t *
search_memsegs(pfnum)
	register u_int pfnum;
{
	register struct memseg *tseg;
	/* make sure it's on the stack to avoid race conditions */
	register struct memseg *l_memseg;
	register page_t *pp;

	l_memseg = last_memseg;
	if (pfnum >= l_memseg->pages_base && pfnum < l_memseg->pages_end) {
		pp = l_memseg->pages + ((pfnum - l_memseg->pages_base) /
			(PAGESIZE/MMU_PAGESIZE));
		return (pp);
	}
	for (tseg = memsegs; tseg != NULL; tseg = tseg->next) {
		if (tseg == l_memseg)
			continue;
		if (pfnum >= tseg->pages_base && pfnum < tseg->pages_end) {
			last_memseg = tseg;
			pp = tseg->pages + ((pfnum - tseg->pages_base) /
			    (PAGESIZE/MMU_PAGESIZE));
			return (pp);
		}
	}
	return ((page_t *)NULL);
}

page_t *
page_numtopp(pfnum, se)
	register u_int pfnum;
	register se_t se;
{
	register page_t *pp;

	pp = search_memsegs(pfnum);
	if (pp == NULL)
		return ((page_t *)NULL);

	/*
	 * Acquire the appropriate lock on the page.
	 */
	while (!page_lock(pp, se, (kmutex_t *)NULL, P_RECLAIM))
		continue;

	return (pp);
}

/*
 * This routine is typically used by 2 types of callers when they need
 * the page structure corresponding to the page frame.  Page structures
 * returned by this routine are NOT locked and hence can change identity
 * or loose mappings at any time.
 *
 * The first type of caller does not want the identity of the page to
 * change (e.g., hat_vacsync() wants to walk the mapping list assocaited
 * with the page inorder to flush the cache before or after an i/o operation).
 * In such cases, it is the caller's responsibility to ensure that the
 * page identity does not change by ensuring that the following conditions
 * are true:
 *
 *	The page has a "shared/exclusive" lock.
 * 	The page is on an iolist (i.e., p_iolock is held).
 *	The page is not be free.
 *
 * The second type of caller doesn't care about the page identity and just
 * wants the page structure (e.g., hat_getstat()).
 */
page_t *
page_numtopp_nolock(pfnum)
	register u_int pfnum;
{
	return (search_memsegs(pfnum));
}

/*
 * This routine is like page_numtopp, but will only return page structs
 * for pages which are ok for loading into hardware using the page struct.
 */
page_t *
page_numtopp_nowait(pfnum, se)
	register u_int pfnum;
	register se_t se;
{
	register page_t *pp;

	pp = search_memsegs(pfnum);
	if (pp == NULL)
		return ((page_t *)NULL);

	/*
	 * Try to acquire the appropriate lock on the page.
	 */
	if (pp->p_free)
		pp = NULL;
	else {
		if (!page_trylock(pp, se))
			pp = NULL;
		else {
			if (pp->p_free) {
				page_unlock(pp);
				pp = NULL;
			}
		}
	}
	return (pp);
}

/*
 * Returns a count of dirty pages that are in the process
 * of being written out.
 */
int
page_busy()
{
	int nppbusy;
	page_t *pp;

	nppbusy = 0;
	pp = epages;
	while (--pp >= pages) {
		/*
		 * XXX - What about pages going out?
		 *
		 * Also, should we count pages with
		 * their p_selock held?
		 */
		if (!pp->p_free && pp->p_vnode != NULL &&
		    PP_ISMOD(pp) && !IS_SWAPVP(pp->p_vnode)) {
			nppbusy++;
		}
	}
	return (nppbusy);
}

void page_invalidate_pages(void);

/*
 * callback handler to vm sub-system
 *
 * callers make sure no recursive entries to this func.
 */
/*ARGSUSED*/
void
callb_vm_cpr(void *arg, int code)
{
	if (code == CB_CODE_CPR_CHKPT)
		page_invalidate_pages();
}

/*
 * Invalidate all pages of the system.
 * It shouldn't be called until all user page activities are all stopped.
 */
void
page_invalidate_pages()
{
	register page_t *pp;
	u_int nbusypages, retry = 0;
	const int MAXRETRIES = 4;
#ifdef sparc
	extern struct vnode prom_ppages;
#endif sparc

top:
	/*
	 * Flush dirty pages and destory the clean ones.
	 */
	nbusypages = 0;
	pp = epages;
	while (--pp >= pages) {
		struct vnode *vp;
		u_int	offset;
		int	mod;

		/*
		 * skip the page if it has no vnode or the page associated
		 * with the kernel vnode or prom allocated kernel mem.
		 */
#ifdef sparc
		if ((vp = pp->p_vnode) == NULL || vp == &kvp ||
		    vp == &prom_ppages)
#else /* x86 doesn't have prom or prom_ppage */
		if ((vp = pp->p_vnode) == NULL || vp == &kvp)
#endif sparc
			continue;

		/*
		 * skip the page which is already free invalidated.
		 */
		if (pp->p_free && pp->p_age)
			continue;

		/*
		 * skip pages that are already locked or can't be "exclusively"
		 * locked or are already free.  After we lock the page, check
		 * the free and age bits again to be sure it's not destroied
		 * yet.
		 * To achieve max. parallelization, we use page_trylock instead
		 * of page_lock so that we don't get block on individual pages
		 * while we have thousands of other pages to process.
		 */
		if (!page_trylock(pp, SE_EXCL)) {
			nbusypages++;
			continue;
		} else if (pp->p_free) {
			if (!pp->p_age) {
				page_destroy_free(pp);
			} else {
				page_unlock(pp);
			}
			continue;
		}
		/*
		 * Is this page involved in some I/O? shared?
		 *
		 * The page_struct_lock need not be acquired to
		 * examine these fields since the page has an
		 * "exclusive" lock.
		 */
		if (pp->p_lckcnt != 0 || pp->p_cowcnt != 0) {
			page_unlock(pp);
			continue;
		}

		if (vp->v_type == VCHR) {
			cmn_err(CE_PANIC, "vp->v_type == VCHR");
		}

		/*
		 * Check the modified bit. Leave the bits alone in hardware
		 * (they will be modified if we do the putpage).
		 */
		if ((mod = PP_ISMOD(pp)) == 0) {
			hat_pagesync(pp, HAT_DONTZERO);
			mod = PP_ISMOD(pp);
		}

		offset = pp->p_offset;
		if (mod) {
			/*
			 * Hold the vnode before releasing the page lock
			 * to prevent it from being freed and re-used by
			 * some other thread.
			 */
			VN_HOLD(vp);
			page_unlock(pp);
			/*
			 * No error return is checked here. Callers such as
			 * cpr deals with the dirty pages at the dump time
			 * if this putpage fails.
			 */
			VOP_PUTPAGE(vp, (offset_t)offset, PAGESIZE,
				B_INVAL|B_FORCE, kcred);
			VN_RELE(vp);
		} else {
			page_destroy(pp, 0);
		}
	}
	if (nbusypages && retry++ < MAXRETRIES) {
		delay(1);
		goto top;
	}
}


/*
 * Debugging routine only!
 */
#include <sys/reboot.h>
#include <sys/debug/debug.h>

void
call_debug(mess)
	char *mess;
{
	if (boothowto & RB_DEBUG) {
		cmn_err(CE_WARN, mess);
		debug_enter((char *)NULL);
	} else {
		cmn_err(CE_PANIC, mess);
	}
}
