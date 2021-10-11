/*
 * Copyright (c) 1988, 1989, 1990, 1993, by Sun Microsystems, Inc.
 */

#pragma ident   "@(#)page_lock.c 1.27     95/09/13 SMI"

/*
 * VM - page locking primitives
 */
#include <sys/param.h>
#include <sys/t_lock.h>
#include <sys/vtrace.h>
#include <sys/debug.h>
#include <sys/cmn_err.h>
#include <sys/vnode.h>
#include <sys/bitmap.h>
#include <vm/page.h>
#include <vm/seg_enum.h>

/*
 * This global mutex is for logical page locking.
 * The following fields in the page structure are protected
 * by this lock:
 *
 *	p_lckcnt
 *	p_cowcnt
 */
kmutex_t page_llock;

/*
 * This is a global lock for the logical page free list.  The
 * logical free list, in this implementation, is maintained as two
 * separate physical lists - the cache list and the free list.
 */
kmutex_t  page_freelock;

/*
 * The hash table, page_hash[], the p_selock fields, and the
 * list of pages associated with vnodes are protected by arrays of mutexes.
 *
 * Unless the hashes are changed radically, the table sizes must be
 * a power of two.  Also, we typically need more mutexes for the
 * vnodes since these locks are occasionally held for long periods.
 * And since there seem to be two special vnodes (kvp and swapvp),
 * we make room for private mutexes for them.
 *
 * The pse_mutex[] array holds the mutexes to protect the p_selock
 * fields of all page_t structures.
 *
 * PAGE_SE_MUTEX(pp) returns the address of the appropriate mutex
 * when given a pointer to a page_t.
 *
 * PSE_TABLE_SIZE must be a power of two.  One could argue that we
 * should go to the trouble of setting it up at run time and base it
 * on memory size rather than the number of compile time CPUs.
 */
#if NCPU < 4
#define	PH_TABLE_SIZE	16
#define	VP_SHIFT	7
#else
#define	PH_TABLE_SIZE	128
#define	VP_SHIFT	9
#endif

#define	PSE_SHIFT	6		/* next power of 2 bigger than page_t */
#define	PSE_TABLE_SIZE	32		/* number of mutexes to have */

kmutex_t	ph_mutex[PH_TABLE_SIZE];
kmutex_t	pse_mutex[PSE_TABLE_SIZE];
u_int		ph_mutex_shift;

#define	PAGE_SE_MUTEX(pp) \
	    &pse_mutex[(((u_int)pp) >> PSE_SHIFT) & (PSE_TABLE_SIZE - 1)]

/*
 * The vph_mutex[] array  holds the mutexes to protect the vnode chains,
 * (i.e., the list of pages anchored by v_pages and connected via p_vpprev
 * and p_vpnext).
 *
 * The page_vnode_mutex(vp) function returns the address of the appropriate
 * mutex from this array given a pointer to a vnode.  It is complicated
 * by the fact that the kernel's vnode and the swapfs vnode are referenced
 * frequently enough to warrent their own mutexes.
 *
 * The VP_HASH_FUNC returns the index into the vph_mutex array given
 * an address of a vnode.
 */
#define	VPH_TABLE_SIZE	(2 << VP_SHIFT)

#define	VP_HASH_FUNC(vp) \
	((((u_int)(vp) >> 6) + \
	    ((u_int)(vp) >> 10) + \
	    ((u_int)(vp) >> 10) + \
	    ((u_int)(vp) >> 12)) \
	    & (VPH_TABLE_SIZE - 1))

extern	struct vnode	kvp;

kmutex_t	vph_mutex[VPH_TABLE_SIZE + 2];

/*
 * Initialize the locks used by the Virtual Memory Management system.
 *
 * These include the page_freelock, page_llock, and the three arrays
 * protecting the hash lists, the vp->v_page lists and the p_selock
 * fields of the page_t structures.
 *
 * page_hashsz gets set up at startup time.
 */
void
page_lock_init()
{
	u_int		i;
	char		buf[100];
	extern	void	page_create_init();

	/*
	 * Initialize the global "page_freelock" lock.
	 */
	mutex_init(&page_freelock, "page_freelock", MUTEX_DEFAULT, DEFAULT_WT);

	/*
	 * Initialize the global "page_llock".
	 */
	mutex_init(&page_llock, "page struct lock", MUTEX_DEFAULT, DEFAULT_WT);

	/*
	 * Initialize each mutex in the ph_mutex[] array.
	 */
	ph_mutex_shift = highbit(page_hashsz / PH_TABLE_SIZE);

	for (i = 0; i < PH_TABLE_SIZE; i++) {
		(void) sprintf(buf, "ph_mutex %d", i);
		mutex_init(&ph_mutex[i], buf, MUTEX_DEFAULT, DEFAULT_WT);
	}

	/*
	 * Initialize each mutex in the vph_mutex[] array.
	 *
	 * The `plus two' is to make room for private mutexes for
	 * the kvp and swapfsvp vnodes.
	 */
	for (i = 0; i < VPH_TABLE_SIZE + 2; i++) {
		(void) sprintf(buf, "vph_mutex %d", i);
		mutex_init(&vph_mutex[i], buf, MUTEX_DEFAULT, DEFAULT_WT);
	}

	/*
	 * Initialize each mutex in the pse_mutex[] array.
	 */
	for (i = 0; i < PSE_TABLE_SIZE; i++) {
		(void) sprintf(buf, "pse_mutex %d", i);
		mutex_init(&pse_mutex[i], buf, MUTEX_DEFAULT, DEFAULT_WT);
	}

	/*
	 * And finally, go set up the locks for page_create().
	 */
	page_create_init();
}

#ifdef VM_STATS
u_int	vph_kvp_count;
u_int	vph_swapfsvp_count;
u_int	vph_other;
#endif /* VM_STATS */


/*ARGSUSED*/
void
se_init(lock, str, type, arg)
	selock_t *lock;
	caddr_t str;
	se_type_t type;
	void *arg;
{
	*lock = 0;
}

#ifdef VM_STATS
u_int	page_lock_count;
u_int	page_lock_miss;
u_int	page_lock_miss_lock;
u_int	page_lock_reclaim;
u_int	page_lock_bad_reclaim;
u_int	page_lock_same_page;
u_int	page_lock_upgrade;
u_int	page_lock_upgrade_failed;

u_int	page_trylock_locked;
u_int	page_trylock_missed;

u_int	page_try_reclaim_upgrade;
#endif /* VM_STATS */


/*
 * Acquire the "shared/exclusive" lock on a page.
 *
 * Returns 1 on success and locks the page appropriately.
 *	   0 on failure and does not lock the page.
 *
 * If `lock' is non-NULL, it will be dropped and and reacquired in the
 * failure case.  This routine can block, and if it does
 * it will always return a failure since the page identity [vp, off]
 * or state may have changed.
 */

int
page_lock(pp, se, lock, reclaim)
	register page_t	*pp;
	register se_t	se;
	kmutex_t	*lock;
	reclaim_t	reclaim;
{
	register int	retval;
	kmutex_t	*pse;
	int		upgraded;
	int		reclaim_it;

	ASSERT(lock != NULL ? MUTEX_HELD(lock) : 1);

	VM_STAT_ADD(page_lock_count);

	upgraded = 0;
	reclaim_it = 0;

	pse = PAGE_SE_MUTEX(pp);
	mutex_enter(pse);

	if ((reclaim == P_RECLAIM) && (pp->p_free)) {

		reclaim_it = 1;
		if (se == SE_SHARED) {
			/*
			 * This is an interesting situation.
			 *
			 * Remember that p_free can only change if
			 * p_selock == -1.
			 * p_free does not depend on our holding `pse'.
			 * And, since we hold `pse', p_selock can not change.
			 * So, if p_free changes on us, the page is already
			 * exclusively held, and we would fail se_trylock()
			 * regardless.
			 *
			 * We want to avoid getting the share
			 * lock on a free page that needs to be reclaimed.
			 * It is possible that some other thread has the share
			 * lock and has left the free page on the cache list.
			 * pvn_vplist_dirty() does this for brief periods.
			 * If the se_share is currently SE_EXCL, we will fail
			 * the following se_trylock anyway.  Blocking is the
			 * right thing to do.
			 * If we need to reclaim this page, we must get
			 * exclusive access to it, force the upgrade now. Again,
			 * we will fail the following se_trylock if the
			 * page is not free and block.
			 */
			upgraded = 1;
			se = SE_EXCL;
			VM_STAT_ADD(page_lock_upgrade);
		}
	}

	if (!se_trylock(&pp->p_selock, se)) {

		VM_STAT_ADD(page_lock_miss);
		if (upgraded) {
			VM_STAT_ADD(page_lock_upgrade_failed);
		}

		if (lock) {
			VM_STAT_ADD(page_lock_miss_lock);
			mutex_exit(lock);
		}

		/*
		 * Now, wait for the page to be unlocked and
		 * release the lock protecting p_cv and p_selock.
		 */
		cv_wait(&pp->p_cv, pse);
		mutex_exit(pse);

		/*
		 * The page identity may have changed while we were
		 * blocked.  If we are willing to depend on "pp"
		 * still pointing to a valid page structure (i.e.,
		 * assuming page structures are not dynamically allocated
		 * or freed), we could try to lock the page if its
		 * identity hasn't changed.
		 *
		 * This needs to be measured, since we come back from
		 * cv_wait holding pse (the expensive part of this
		 * operation) we might as well try the cheap part.
		 * Though we would also have to confirm that dropping
		 * `lock' did not cause any grief to the callers.
		 */
		if (lock) {
			mutex_enter(lock);
		}
		retval = 0;
	} else {
		/*
		 * We have the page lock.
		 * If we needed to reclaim the page, and the page
		 * needed reclaiming (ie, it was free), then we
		 * have the page exclusively locked.  We may need
		 * to downgrade the page.
		 */
		ASSERT((upgraded) ?
		    ((pp->p_free) && se_excl_assert(&pp->p_selock)) : 1);
		mutex_exit(pse);

		retval = 1;

		/*
		 * We now hold this page's lock, either shared or
		 * exclusive.  This will prevent its identity from changing.
		 * The page, however, may or may not be free.  If the caller
		 * requested, and it is free, go reclaim it from the
		 * free list.  If the page can't be reclaimed, return failure
		 * so that the caller can start all over again.
		 *
		 * NOTE:page_reclaim() releases the page lock (p_selock)
		 *	if it can't be reclaimed.
		 */
		if (reclaim_it) {
			if (!page_reclaim(pp, lock)) {
				VM_STAT_ADD(page_lock_bad_reclaim);
				retval = 0;
			} else {
				VM_STAT_ADD(page_lock_reclaim);
				if (upgraded) {
					page_downgrade(pp);
				}
			}
		}
	}
	return (retval);
}

/*
 * Read the comments inside of page_lock() carefully.
 */
int
page_try_reclaim_lock(pp, se)
	page_t		*pp;
	se_t		se;
{
	kmutex_t	*pse;
	int		rc;

	pse = PAGE_SE_MUTEX(pp);
	mutex_enter(pse);

	if ((se == SE_SHARED) && (pp->p_free)) {
		VM_STAT_ADD(page_try_reclaim_upgrade);
		se = SE_EXCL;
	}
	rc = se_trylock(&pp->p_selock, se);

	mutex_exit(pse);
	return (rc);
}

/*
 * Acquire a page's "shared/exclusive" lock, but never block.
 * Returns 1 on success, 0 on failure.
 */
int
page_trylock(pp, se)
	register page_t *pp;
	register se_t se;
{
	register int	retval;
	kmutex_t	*pse;

	pse = PAGE_SE_MUTEX(pp);
	mutex_enter(pse);
	retval = se_trylock(&pp->p_selock, se);

	ASSERT(retval? ((se == SE_EXCL)? pp->p_selock == -1 :
	    pp->p_selock > 0) : 1);

	mutex_exit(pse);
	return (retval);
}

/*
 * Release the page's "shared/exclusive" lock and wake up anyone
 * who might be waiting for it.
 */
void
page_unlock(pp)
	register page_t *pp;
{
	kmutex_t	*pse;
	selock_t	se;

	pse = PAGE_SE_MUTEX(pp);
	mutex_enter(pse);
	se = pp->p_selock;
	if (se == 0) {
		cmn_err(CE_PANIC,
		    "page_unlock: page %x is not locked", (int)pp);
	} else if (se < 0) {
		THREAD_KPRI_RELEASE();
		pp->p_selock = 0;
		cv_broadcast(&pp->p_cv);
	} else if ((pp->p_selock = --se) == 0) {
		cv_broadcast(&pp->p_cv);
	}
	mutex_exit(pse);
}

/*
 * Try to upgrade the lock on the page from a "shared" to an
 * "exclusive" lock.  Since this upgrade operation is done while
 * holding the mutex protecting this page, no one else can acquire this page's
 * lock and change the page. Thus, it is safe to drop the "shared"
 * lock and attempt to acquire the "exclusive" lock.
 *
 * Returns 1 on success, 0 on failure.
 */
int
page_tryupgrade(pp)
	register page_t *pp;
{
	register int	retval;
	kmutex_t	*pse;

	ASSERT(se_shared_assert(&pp->p_selock));

	pse = PAGE_SE_MUTEX(pp);
	mutex_enter(pse);
	if (pp->p_selock == 1) {
		pp->p_selock = -1;	/* convert to exclusive lock */
		retval = 1;
	} else {
		retval = 0;
	}
	mutex_exit(pse);
	return (retval);
}

/*
 * Downgrade the "exclusive" lock on the page to a "shared" lock
 * while holding the mutex protecting this page's p_selock field.
 */
void
page_downgrade(pp)
	register page_t *pp;
{
	kmutex_t	*pse;

	ASSERT(se_excl_assert(&pp->p_selock));

	pse = PAGE_SE_MUTEX(pp);
	mutex_enter(pse);
	pp->p_selock = 1;
	cv_broadcast(&pp->p_cv);
	mutex_exit(pse);
}

/*
 * Acquire the i/o lock on a page.
 */
void
page_io_lock(pp)
	register page_t *pp;
{
	sema_p(&pp->p_iolock);
}

/*
 * Release the i/o lock on a page.
 */
void
page_io_unlock(pp)
	register page_t *pp;
{
	sema_v(&pp->p_iolock);
}

/*
 * Try to acquire the i/o lock on a page without blocking.
 * Returns 1 on success, 0 on failure.
 */
int
page_io_trylock(pp)
	register page_t *pp;
{
	return (sema_tryp(&pp->p_iolock));
}

/*
 * Assert that the i/o lock on a page is held.
 * Returns 1 on success, 0 on failure.
 */
int
page_iolock_assert(pp)
	register page_t *pp;
{
	return (SEMA_HELD(&pp->p_iolock));
}

/*
 * Wrapper exported to kernel routines that are built
 * platform-independent (the macro is platform-dependent;
 * the size of vph_mutex[] is based on NCPU).
 */
kmutex_t *
page_vnode_mutex(vp)
	struct vnode *vp;
{
	kmutex_t	*mp;

	if (vp == &kvp) {
		mp = &vph_mutex[VPH_TABLE_SIZE + 0];
	} else {
		mp = &vph_mutex[VP_HASH_FUNC(vp)];
	}
	return (mp);

/*
	return (PAGE_VNODE_MUTEX(vp));

#define	PAGE_VNODE_MUTEX(vp)	\
	((vp) == &kvp ? &vph_mutex[VPH_TABLE_SIZE + 0] : \
	    &vph_mutex[VP_HASH_FUNC(vp)])
*/
}


kmutex_t *
page_se_mutex(pp)
	page_t	*pp;
{
	return (&pse_mutex[(((u_int)pp) >> PSE_SHIFT) & (PSE_TABLE_SIZE - 1)]);
}
