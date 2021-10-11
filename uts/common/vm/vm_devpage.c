/*
 * Copyright (c) 1988, 1989, 1990, 1991, 1993 by Sun Microsystems, Inc.
 */

#ident	"@(#)vm_devpage.c	1.19	94/03/31 SMI"

/*
 * Devpage - device page management.
 */
#include <sys/param.h>
#include <sys/vnode.h>
#include <sys/vm.h>
#include <sys/debug.h>
#include <sys/vtrace.h>
#include <sys/cmn_err.h>
#include <sys/kmem.h>
#include <sys/t_lock.h>

#include <vm/hat.h>
#include <vm/anon.h>
#include <vm/page.h>
#include <vm/devpage.h>
#include <vm/seg.h>

/*
 * This implementation is still experimental.
 *
 * Devpages are used to keep track of multiple mappings.  The
 * struct devpage is actually a struct page, but most of the
 * fields are meaningless.
 *
 * Devpages can be located using devpage_locate(vp, off, excl), where
 * vp is the common vnode associated with the underlying device, and
 * off is the offset in the devices address space.  If the devpage
 * exists, the indicated lock is applied, and the devpage returned.  If
 * the devpage does not exist, then NULL is returned.
 *
 * Existing locked devpages can be found using devpage_find(vp, off),
 * where vp is the common vnode associated with the underlying device,
 * and off is the offset in the devices address space.  It is an error
 * if the devpage does not exist, or is not locked.
 *
 * Devpages are created using devpage_create(vp, off).  A devpage is
 * created for the given <vp, off>, added to the hashlist, and returned
 * to the caller with a single level of reader lock.
 *
 * Devpages are released using devpage_free(dp).  If the lock on
 * the devpage can be promoted from shared to exclusive, the
 * implication is that the current thread is the last user of the
 * mapping, and the devpage can be destroyed, otherwise a devpage
 * reader lock is released for that particular threads mapping.
 *
 * Notes:
 *
 * a)	This is where we naughtily reuse the routines from vm_page.c
 * b)	This reuse of the page stuff could be dangerous!
 * c)	Note the implicit dependence on the implementation of the
 *	multiple reader, single writer lock to perform reference
 *	counting.
 */


/*
 * Devpage find - called from spec_getpage()
 *
 * Given a <vp, off> find the associated page.
 * The page must exist, and have a shared or
 * exclusive lock, otherwise the routine
 * will panic.
 */
#ifndef devpage_find
devpage_t *
devpage_find(struct vnode *vp, u_int off)
{
	ASSERT(vp != NULL);
	return ((devpage_t *)page_find(vp, off));
}
#endif /* devpage_find */


/*
 * Devpage lookup - called from spec_addmap()
 *
 * Given a <vp, off> find the associated page,
 * adding an appropriate shared/exclusive lock.
 * If the page doesn't exist, the routine will
 * return NULL.
 */
#ifndef devpage_lookup
devpage_t *
devpage_lookup(struct vnode *vp, u_int off, int excl)
{
	ASSERT(vp != NULL);
	return ((devpage_t *)page_lookup_dev(vp, off, excl));
}
#endif /* devpage_lookup */


/*
 * Devpage creation - called from spec_addmap()
 *
 * Allocate a fresh devpage and hash it in on the
 * vp at the given offset, with a single "shared"
 * lock. Return the allocated devpage, or NULL if
 * something went wrong.
 *
 * Precondition: hold the s_lock mutex on the common
 * snode to prevent two devpage_create's happening at
 * the same time, also need to exclude devpage_lookup()
 * from running by the same expedient.
 */
struct devpage *
devpage_create(struct vnode *vp, u_int off)
{
	register struct devpage *dp;

	ASSERT(vp->v_type == VCHR);

	/*
	 * Allocate a new devpage.  We deliberately *avoid*
	 * holding the page_idlock here, in case kmem_alloc()
	 * needs to use it implicitly to satisfy the request.
	 *
	 * We're protected only by the s_lock mutex on the snode.
	 */
	dp = kmem_zalloc(sizeof (struct devpage), KM_SLEEP);

	/*
	 * Initialize the per-page shared/exclusive lock,
	 * the iolock semaphore and the devpage mutex.
	 */
	se_init(&dp->p_selock, "devpage selock", SE_DEFAULT, DEFAULT_WT);

	/*
	 * Lock the page before inserting it into the hashlist.
	 */
	if (!page_lock((page_t *)dp, SE_EXCL, NULL, P_RECLAIM))
		cmn_err(CE_PANIC, "devpage_create: couldn't lock page: %x",
		    (int)dp);

	sema_init(&dp->p_iolock, 1, "devpage iolock", SEMA_DEFAULT, DEFAULT_WT);

	/*
	 * Set the referenced bit (why?), logically lock the "page" and
	 * hash it onto the vp, and return it, with a "shared" lock.
	 * Also, by default, mark it as permanently non-cacheable.
	 */
	hat_page_enter((page_t *)dp);
	PP_SETREF((page_t *)dp);
	PP_SETPNC((page_t *)dp);
	hat_page_exit((page_t *)dp);
	dp->p_lckcnt = 1;

	if (!page_hashin((page_t *)dp, vp, off, NULL))
		cmn_err(CE_PANIC, "devpage_create: duplicate page :%x",
		    (int)dp);

	/*
	 * Now, downgrade to a "shared" lock.
	 */
	page_downgrade((page_t *)dp);

	ASSERT(se_shared_assert(&dp->p_selock));

	return (dp);
}


/*
 * Devpage release - called from spec_delmap()
 *
 * Remove a shared lock, and if it's the last, destroy
 * the devpage.
 */
void
devpage_destroy(register struct devpage *dp)
{
	ASSERT(se_shared_assert(&dp->p_selock));

#ifdef DEVPAGE_DEBUG
	page_struct_lock((struct page *)dp);
	/*
	 * sanity check
	 */
	ASSERT(dp->p_lckcnt == 1 && dp->p_cowcnt == 0);
	page_struct_unlock((struct page *)dp);
#endif /* DEVPAGE_DEBUG */

	/*
	 * Try and obtain an "exclusive" lock on the page.
	 */
	if (page_tryupgrade((page_t *)dp)) {
		/*
		 * If we got the exclusive lock, it implies we
		 * have the last surviving devpage .. so we can
		 * destroy it, tee hee.
		 */
		dp->p_lckcnt = 0;
		if (dp->p_mapping != NULL) {
			hat_clrrefmod(dp);
			/*
			 * Should be ok to just unload now.
			 */
			hat_pageunload((page_t *)dp);
			ASSERT(dp->p_mapping == NULL);
		} else {
			hat_clrrefmod(dp);
		}

		ASSERT(dp->p_free == 0 && dp->p_vnode != NULL);
		page_hashout((page_t *)dp, (kmutex_t *)NULL);

		/*
		 * Threads waiting to get a lock on this devpage
		 * are woken up in page_unlock(), so they can either
		 * create a new devpage or fail since we've already
		 * hashed it out.
		 */
		page_unlock((page_t *)dp);
		kmem_free(dp, sizeof (*dp));
	} else {
		/*
		 * Still some other "shared" locks exist so release
		 * our "shared" lock on the page.
		 */
		page_unlock((page_t *)dp);
	}
}
