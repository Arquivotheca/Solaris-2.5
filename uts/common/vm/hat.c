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
 * 	(c) 1986, 1987, 1988, 1989, 1990, 1994  Sun Microsystems, Inc
 * 	(c) 1983, 1984, 1985, 1986, 1987, 1988, 1989 AT&T.
 *		All rights reserved.
 *
 */

#pragma ident   "@(#)hat.c 1.57     95/08/07 SMI"

/*
 * Generic Hardware Address Translation interface
 */

/*
 * The hat layer is responsible for all mappings in the system. It
 * uses and maintains a list of hats active within every address
 * space and a list of mappings to every page.
 *
 * An mmu driver (object manager) is responsible for the mmu dependent
 * operations that operate on the mappings within the context of a single
 * mapping device.  This file (hat.c) is responsible for multiplexing
 * a hat request across the required mmus(hats). The operataions are:
 *
 * Opeartions for startup
 *	init - initialize any needed data structures for an mmu/hat
 *
 * Operations on hat resources of an address space
 *	alloc - allocate a hat structure for managing the mappings of a hat
 *	setup - make an address space context the current active one
 *	free - free hat resources owned by an address space
 *	swapin - load up translations at swapin time
 *	swapout - unload translations not needed while swapped
 *	dup - duplicate the translations of an address space
 *
 * Operations on a named address within a segment
 * 	memload - load/lock the given page struct
 * 	devload - load/lock the given page frame number
 *	contig_memload - load/lock translations to a range of virtual address
 *			 backed by physically contiguous memory.
 *	contig_devload - load/lock translations to a range of virtual address
 *			 backed by physically contiguous device memory.
 *	unlock - release lock to a range of address on a translation
 *	fault - validate a mapping using a cached translation
 *	map - allocate MMU resources to map address range <addr, addr + len>
 *
 * Operations over a address range within an address space
 *	chgprot - change protections for addr and len to prot
 *	unload - unmap starting at addr for len
 *	sync - synchronize mappings for a range
 *	unmap - Unload translations and free up MMU resources allocated for
 *		mapping address range <addr, addr + len>.
 *
 * Operations on all active translations of a page
 *	pageunload - unload all translations to a page
 * 	pagesync - update the pages structs ref and mod bits, zero ref
 *			and mod bits in mmu
 *	pagecachectl - control caching of a page
 *
 * Operations that return physical page numbers
 *	getkpfnum - return page frame number for given va
 *	getpfnum - return pfn for arbitrary address in an address space
 *
 * Support operations
 *	mem - allocate/free memory for a hat, use sparingly!
 *			called without per hat hat_mutex held
 */

/*
 * When a pagefault occurs, the particular hat resource which
 * the fault should be resolved in must be indentified uniquely.
 * A hat should be allocated and linked onto the address space
 * (via hat_alloc) if one does not exist.  Once the vm system
 * resolves the fault, it will use the hat when it calls the
 * hat layer to have any mapping changes made.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/t_lock.h>
#include <sys/systm.h>
#include <sys/kmem.h>
#include <sys/cpuvar.h>
#include <sys/vtrace.h>
#include <sys/var.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>

#include <vm/faultcode.h>
#include <vm/as.h>
#include <vm/seg.h>
#include <vm/hat.h>
#include <vm/page.h>
#include <vm/devpage.h>

kmutex_t	hat_statlock;		/* for the stuff in hat_refmod.c */
kmutex_t	hat_res_mutex;		/* protect global freehat list */
kmutex_t	hat_kill_procs_lock;	/* for killing process on memerr */

struct	hat	*hats;
struct	hat	*hatsNHATS;
struct	hat	*hatfree = (struct hat *)NULL;
struct	hatops	*sys_hatops = (struct hatops *)NULL;
struct	hat_lockops *hat_lockops;

int		nhats;
kcondvar_t 	hat_kill_procs_cv;
int		hat_inited = 0;

/* should be in a header file */
extern int pf_is_memory(u_int pf);

/* hat locking operations */
void	hat_mlist_enter(page_t *);
void	hat_mlist_exit(page_t *);
int	hat_mlist_held(page_t *);
void	hat_page_enter(page_t *);
void	hat_page_exit(page_t *);
void	hat_enter(struct as *);
void	hat_exit(struct as *);
void	hat_cachectl_enter(struct page *);
void	hat_cachectl_exit(struct page *);

/*
 * Initialize locking needed early on as the system boots.
 * Assumes the first entry in the hattab is the system MMU.
 */
void
hat_lock_init()
{
	register struct hatsw *hsw = hattab;

	(*hsw->hsw_ops->h_lock_init)();
}

/*
 * Call the init routines for every configured hat.
 */
void
hat_init()
{
	register struct hat *hat;
	register struct hatsw *hsw;

	ASSERT(sys_hatops == NULL);

	/*
	 * Allocate mmu independent hat data structures.
	 */
	nhats = v.v_proc + (v.v_proc/2);
	if ((hats = (struct hat *)kmem_zalloc(sizeof (struct hat) * nhats,
	    KM_NOSLEEP)) == NULL)
		panic("Cannot allocate memory for hat structs");
	hatsNHATS = hats + nhats;

	for (hat = hatsNHATS - 1; hat >= hats; hat--) {
		mutex_init(&hat->hat_mutex, "hat_mutex", MUTEX_DEFAULT, NULL);
		hat_freehat(hat);
	}

	for (hsw = hattab; hsw->hsw_name && hsw->hsw_ops; hsw++) {
		if (hsw->hsw_ops->h_init)
			(*hsw->hsw_ops->h_init)();
	}

	ASSERT(sys_hatops != NULL);

	/*
	 * Initialize any global state for the statistics handling.
	 * Hrm_lock protects the globally allocted memory
	 *	hrm_memlist and hrm_hashtab.
	 */
	mutex_init(&hat_statlock, "hat_statlock", MUTEX_DEFAULT, NULL);

	/*
	 * We grab the first hat for the kernel,
	 * the above initialization loop initialized sys_hatops and kctx.
	 */
	AS_LOCK_ENTER(&kas, &kas.a_lock, RW_WRITER);
	(void) hat_alloc(&kas, sys_hatops);
	AS_LOCK_EXIT(&kas, &kas.a_lock);
}

/*
 * Allocate a hat structure.
 * Called when an address space first uses a hat.
 * Links allocated hat onto the hat list for the address space (as->a_hat)
 */
struct hat *
hat_alloc(as, hatops)
	register struct as *as;
	register struct hatops *hatops;
{
	register struct hat *hat;

	if ((hat = hat_gethat()) == NULL)
		panic("no hats");
	if (hatops == NULL)
		hatops = sys_hatops;

	ASSERT(AS_WRITE_HELD(as, &as->a_lock));
	hat->hat_op = hatops;
	hat->hat_as = as;
	hat->hat_next = NULL;
	hat_add(hat, as);

	HATOP_ALLOC(hat, as);

	return (hat);
}

/*
 * Hat_setup, makes an address space context the current active one;
 * uses the default hat, calls the setup routine for the system mmu.
 */
struct as *
hat_setup(as, allocflag)
	struct as *as;
	int allocflag;
{
	struct as *oas;

	hat_enter(as);
	oas = HATOP_SETUP(as, allocflag);
	curthread->t_mmuctx = 0;
	hat_exit(as);
	return (oas);
}

/*
 * Free all of the mapping resources.
 * If a non-zero hat is passed, free only the resources used by that hat.
 */
void
hat_free(hat, as)
	register struct hat *hat;
	register struct as *as;
{
	ASSERT(AS_WRITE_HELD(as, &as->a_lock));

	if (hat == NULL) {	/* free everything, called from as_free() */

		if (as->a_vbits)
			hat_freestat(as, NULL);

		for (hat = as->a_hat; hat != NULL; hat = hat->hat_next)
			HATOP_FREE(hat, as);

		for (hat = as->a_hat; hat != NULL; hat = as->a_hat->hat_next) {
			if (hat != as->a_hat) {
				hat_sub(hat, as);
				hat_freehat(hat);
			}
		}
	} else {
		HATOP_FREE(hat, as);
		hat_sub(hat, as);
		hat_freehat(hat);
	}
}

/*
 * Duplicate the translations of an as into another newas
 */
int
hat_dup(as, newas)
	struct as *as, *newas;
{
	register struct hat *hat;
	register int	err = 0;

	ASSERT(AS_WRITE_HELD(as, &as->a_lock));
	for (hat = as->a_hat; hat != NULL; hat = hat->hat_next) {
		if (err = HATOP_DUP(hat, as, newas)) {
			break;
		}
	}
	return (err);
}


/*
 * Set up any translation structures, for the specified address space,
 * that are needed or preferred when the process is being swapped in.
 */
void
hat_swapin(as)
	struct as *as;
{
	struct hat *hat;

	ASSERT(AS_LOCK_HELD(as, &as->a_lock));
	for (hat = as->a_hat; hat != NULL; hat = hat->hat_next) {
		HATOP_SWAPIN(hat, as);
	}
}


/*
 * Free all of the translation resources, for the specified address space,
 * that can be freed while the process is swapped out. Called from as_swapout.
 */
void
hat_swapout(as)
	struct as *as;
{
	struct hat *hat;

	ASSERT(AS_LOCK_HELD(as, &as->a_lock));
	for (hat = as->a_hat; hat != NULL; hat = hat->hat_next) {
		HATOP_SWAPOUT(hat, as);
	}
}


/*
 * Make a mapping at addr to map page pp with protection prot.
 *
 * No locking at this level.  Hat_memload and hat_devload
 * are wrappers for xxx_pteload, where xxx is instance of
 * a hat module like the sun mmu.  Xxx_pteload is called from
 * machine dependent code directly.  The locking (acquiring
 * the per hat hat_mutex) that is done in xxx_pteload allows both the
 * hat layer and the machine dependent code to work properly.
 */
void
hat_memload(hat, as, addr, pp, prot, attr)
	register struct hat *hat;
	register struct as *as;
	register caddr_t addr;
	register page_t *pp;
	register u_int prot;
	register int attr;
{
	ASSERT((as == &kas) || AS_LOCK_HELD(as, &as->a_lock));
	ASSERT(se_assert(&pp->p_selock));
	ASSERT(hat != NULL);

	if (pp->p_free)
		cmn_err(CE_PANIC,
		    "hat_memload: loading a mapping to free page %x", (int)pp);

	HATOP_MEMLOAD(hat, as, addr, pp, prot, attr);
}

/*
 * Cons up a struct pte using the device's pf bits and protection
 * prot to load into the hardware for address addr; treat as minflt.
 */
void
hat_devload(hat, as, addr, dp, pf, prot, attr)
	register struct hat *hat;
	register struct as *as;
	register caddr_t addr;
	register devpage_t *dp;
	register u_int pf;
	register u_int prot;
	register int attr;
{
	/*
	 * XXX - ASSERT((as == &kas) || AS_LOCK_HELD(as, &as->a_lock));
	 *
	 * Some graphics drivers call this from timeout routines
	 * to load translations for an arbitrary address space and they
	 * don't have the address space locked. They can't lock it because
	 * it might disappear out from under them while they're trying to
	 * acquire the lock. They do, however, have a private lock, which
	 * protects the segment containing the addr and thus protects the
	 * address space from being destroyed. This is good enough for now.
	 * For the longer term need to have a way to acquire an address
	 * space from an arbitrary context.
	 */

	ASSERT(hat != NULL);

	/*
	 * If it's a memory page find its pp
	 */
	if (!(attr & HAT_NOCONSIST) && pf_is_memory(pf) && dp == NULL) {
		dp = (devpage_t *) page_numtopp_nolock(pf);
	}

	HATOP_DEVLOAD(hat, as, addr, dp, pf, prot, attr);
}

/*
 * Set up translations to a range of virtual addresses backed by
 * physically contiguous memory. The MMU driver can take advantage
 * of underlying hardware to set up translations using larger than
 * 4K bytes size pages. The caller must ensure that the pages are
 * locked down and that their identity will not change.
 */
void
hat_contig_memload(hat, as, addr, pp, prot, attr, len)
	register struct hat *hat;
	register struct as *as;
	register caddr_t addr;
	register page_t *pp;
	register u_int prot;
	register int attr;
	register u_int len;
{
	ASSERT(hat != NULL);

	HATOP_CONTIG_MEMLOAD(hat, as, addr, pp, prot, attr, len);
}


/*
 * Set up translations to a range of virtual addresses backed by
 * physically contiguous device memory. The MMU driver can take advantage
 * of underlying hardware to set up translations using larger than
 * 4K bytes size pages.
 */
void
hat_contig_devload(hat, as, addr, dp, pf, prot, attr, len)
	register struct hat *hat;
	register struct as *as;
	register caddr_t addr;
	register devpage_t *dp;
	register u_int pf;
	register u_int prot;
	register int attr;
	register u_int len;
{

	ASSERT(hat != NULL);

	HATOP_CONTIG_DEVLOAD(hat, as, addr, dp, pf, prot, attr, len);
}

/*
 * Release one hardware address translation lock on the given address.
 */
void
hat_unlock(hat, as, addr, len)
	struct hat *hat;
	struct as *as;
	caddr_t addr;
	u_int len;
{
	ASSERT(as == &kas || AS_LOCK_HELD(as, &as->a_lock));
	ASSERT(hat != NULL);

	HATOP_UNLOCK(hat, as, addr, len);
}


/*
 * Change the protections in the virtual address range
 * given to the specified virtual protection.  If
 * vprot == ~PROT_WRITE, then all the write permission
 * is taken away for the current translations, else if
 * vprot == ~PROT_USER, then all the user permissions
 * are taken away for the current translations, otherwise
 * vprot gives the new virtual protections to load up.
 *
 * addr and len must be MMU_PAGESIZE aligned.
 */
void
hat_chgprot(as, addr, len, vprot)
	struct as *as;
	caddr_t addr;
	u_int len;
	u_int vprot;
{
	struct hat *hat;

	ASSERT(AS_LOCK_HELD(as, &as->a_lock));

	for (hat = as->a_hat; hat != NULL; hat = hat->hat_next) {
		HATOP_CHGPROT(hat, as, addr, len, vprot);
	}
}


/*
 * Unload all the mappings in the range [addr..addr+len).
 */
void
hat_unload(as, addr, len, flags)
	register struct as *as;
	register caddr_t addr;
	register u_int len;
	int flags;
{
	register struct hat *hat;

	/*
	 * Would like to assert the following but we have segments
	 * drivers that take faults in adress space A and remove
	 * mappings in address space B.  Address space B may
	 * have no lock at that point so the segments doing this
	 * sort of operation must protect themselves by a having a
	 * lock that must be held for the duration of segment
	 * operation. This keeps the segment and the as from
	 * disappearing by an unmap or free; see a similar comment
	 * devload.
	 *
	 * ASSERT(as == &kas || AS_LOCK_HELD(as, &as->a_lock));
	 */

	/* XXX - push as locking into caller? */

	for (hat = as->a_hat; hat != NULL; hat = hat->hat_next) {
		HATOP_UNLOAD(hat, as, addr, len, flags);
	}

}

/*
 * Synchronize all the mappings in the range [addr..addr+len).
 */
void
hat_sync(as, addr, len, flags)
	register struct as *as;
	register caddr_t addr;
	register u_int len;
	register u_int flags;
{
	register struct hat *hat;

	ASSERT(as == &kas || AS_LOCK_HELD(as, &as->a_lock));


	for (hat = as->a_hat; hat != NULL; hat = hat->hat_next) {
		HATOP_SYNC(hat, as, addr, len, flags);
	}

}

/*
 * Remove all mappings to page 'pp'.
 */
void
hat_pageunload(pp)
	register page_t *pp;
{
	(*(sys_hatops->h_pageunload))(pp, NULL);
}

/*
 * synchronize software page struct with hardware,
 * zeros the reference and modified bits
 */
void
hat_pagesync(pp, clearflag)
	register page_t *pp;
	u_int clearflag;
{
	(*(sys_hatops->h_pagesync))(NULL, pp, NULL, clearflag);
}

/*
 * Mark the page as cached or non-cached (depending on flag). Make all mappings
 * to page 'pp' cached or non-cached. This is permanent as long as the page
 * identity remains the same.
 */

void
hat_pagecachectl(pp, flag)
	page_t *pp;
	u_int flag;
{
	ASSERT(se_assert(&pp->p_selock));

	hat_cachectl_enter(pp);

	hat_page_enter(pp);
	if (flag & HAT_TMPNC)
		PP_SETTNC(pp);
	else if (flag & HAT_UNCACHE)
		PP_SETPNC(pp);
	else {
		PP_CLRPNC(pp);
		PP_CLRTNC(pp);
	}
	hat_page_exit(pp);

	(*(sys_hatops->h_pagecachectl))(pp, flag);

	hat_cachectl_exit(pp);
}

/*
 * Get the page frame number for a particular kernel virtual address.
 */
u_int
hat_getkpfnum(addr)
	register caddr_t addr;
{
	return (HATOP_GETKPFNUM(kas.a_hat, addr));
}

/*
 * Get the page frame number for a particular user virtual address.
 * Walk the hat list for the address space and call the getpfnum
 * op for each one; the first one to return a non-zero value is used
 * since they should all point to the same page.
 */
u_int
hat_getpfnum(as, addr)
	register struct as *as;
	register caddr_t addr;
{
	register struct hat *hat;
	u_int pf;

	ASSERT(AS_LOCK_HELD(as, &as->a_lock));

	for (hat = as->a_hat; hat != NULL; hat = hat->hat_next) {
		pf = HATOP_GETPFNUM(hat, as, addr);
		if (pf != NULL)
			return (pf);
	}

	return (NULL);
}

/*
 * Return the number of mappings to a particular page.
 * This number is an approximation of the number of
 * number of people sharing the page.
 */
hat_pageshare(pp)
	page_t *pp;
{
	return (pp->p_share);
}

/*
 * It would be nice if other parity recovery schemes used this mechanism.
 */

#define	ASCHUNK	64

static void
hat_kill_procs_wakeup(hat_kill_procs_cvp)
	kcondvar_t *hat_kill_procs_cvp;
{
	cv_broadcast(hat_kill_procs_cvp);
}

/*
 * Kill process(es) that use the given page. (Used for parity recovery)
 * If we encounter the kernel's address space, give up (return -1).
 * Otherwise, we return 0.
 */
hat_kill_procs(pp, addr)
	page_t	*pp;
	caddr_t	addr;
{
	register struct hment *hme;
	register struct hat *hat;
	struct	as	*as;
	struct	proc	*p;
	struct	as	*as_array[ASCHUNK];
	int	loop	= 0;
	int	opid	= -1;
	int	i;

	hat_mlist_enter(pp);
again:
	if (pp->p_mapping) {
		bzero((caddr_t)&as_array[0], ASCHUNK * sizeof (int));
		for (i = 0; i < ASCHUNK; i++) {
			hme = (struct hment *)pp->p_mapping;
			hat = &hats[hme->hme_hat];
			as = hat->hat_as;

			/*
			 * If the address space is the kernel's, then fail.
			 * The only thing to do with corrupted kernel memory
			 * is die.  The caller is expected to panic if this
			 * is true.
			 */
			if (as == &kas) {
				hat_mlist_exit(pp);
				printf("parity error: ");
				printf("kernel address space\n");
				return (-1);
			}
			as_array[i] = as;

			if (hme->hme_next)
				hme = hme->hme_next;
			else
				break;
		}
	}

	for (i = 0; i < ASCHUNK; i++) {

		as = as_array[i];
		if (as == NULL)
			break;

		/*
		 * Note that more than one process can share the
		 * same address space, if vfork() was used to create it.
		 * This means that we have to look through the entire
		 * process table and not stop at the first match.
		 */
		mutex_enter(&pidlock);
		for (p = practive; p; p = p->p_next) {
			k_siginfo_t siginfo;

			if (p->p_as == as) {
				/* limit messages about same process */
				if (opid != p->p_pid) {
					printf("parity error: killing pid %d\n",
					    (int)p->p_pid);
					opid =  p->p_pid;
					uprintf("pid %d killed: parity error\n",
					    (int)p->p_pid);
				}

				bzero((caddr_t)&siginfo, sizeof (siginfo));
				siginfo.si_addr = addr;
				siginfo.si_signo = SIGBUS;
				/*
				 * the following code should probably be
				 * something from siginfo.h
				 */
				siginfo.si_code = FC_HWERR;

				mutex_enter(&p->p_lock);
				sigaddq(p, NULL, &siginfo, KM_NOSLEEP);
				mutex_exit(&p->p_lock);
			}
		}
		mutex_exit(&pidlock);
	}

	/*
	 * Wait for previously signaled processes to die off,
	 * thus removing their mappings from the mapping list.
	 * XXX - change me to cv_timed_wait.
	 */
	(void) timeout(hat_kill_procs_wakeup, (caddr_t)&hat_kill_procs_cv, HZ);

	mutex_enter(&hat_kill_procs_lock);
	cv_wait(&hat_kill_procs_cv, &hat_kill_procs_lock);
	mutex_exit(&hat_kill_procs_lock);

	/*
	 * More than ASCHUNK mappings on the list for the page,
	 * loop to kill off the rest of them.  This will terminate
	 * with failure if there are more than ASCHUNK*20 mappings
	 * or a process will not die.
	 */
	if (pp->p_mapping) {
		loop++;
		if (loop > 20) {
			hat_mlist_exit(pp);
			return (-1);
		}
		goto again;
	}
	hat_mlist_exit(pp);

	return (0);
}

/*
 * Add a hat to the address space hat list
 * The main mmu hat is always allocated at the time the address space
 * is created. It is always the first hat on the list.  All others
 * are added after it.
 */
void
hat_add(hat, as)
	struct hat *hat;
	struct as *as;
{
	struct hat *nhat;

	ASSERT(AS_WRITE_HELD(as, &as->a_lock));

	if (as->a_hat == NULL)
		as->a_hat = hat;
	else {
		nhat = as->a_hat->hat_next;
		hat->hat_next = nhat;
		as->a_hat->hat_next = hat;
	}
}

void
hat_sub(hat, as)
	struct hat *hat;
	struct as *as;
{
	ASSERT(AS_WRITE_HELD(as, &as->a_lock));

	if (as->a_hat == hat) {
		as->a_hat = as->a_hat->hat_next;
	} else {
		struct hat *prv, *cur;
		for (prv = as->a_hat, cur = as->a_hat->hat_next;
		    cur != NULL;
		    cur = cur->hat_next) {
			if (cur == hat) {
				prv->hat_next = cur->hat_next;
				return;
			} else
				prv = cur;
		}
		panic("no hat to remove");
	}
}


/*
 * generic routines for manipulating the mapping list of a page.
 */

/*
 * Enter a hme on the mapping list for page pp
 */
void
hme_add(hme, pp)
	register struct hment *hme;
	register page_t *pp;
{
	ASSERT(hat_mlist_held(pp));

	hme->hme_prev = NULL;
	hme->hme_next = pp->p_mapping;
	hme->hme_page = pp;
	if (pp->p_mapping) {
		pp->p_mapping->hme_prev = hme;
		ASSERT(pp->p_share > 0);
	} else  {
		ASSERT(pp->p_share == 0);
	}
	pp->p_mapping = hme;
	pp->p_share++;
}

/*
 * remove a hme from the mapping list for page pp
 */
void
hme_sub(hme, pp)
	register struct hment *hme;
	register page_t *pp;
{
	ASSERT(hat_mlist_held(pp));
	ASSERT(hme->hme_page == pp);

	if (pp->p_mapping == NULL)
		panic("hme_remove - no mappings");

	ASSERT(pp->p_share > 0);
	pp->p_share--;

	if (hme->hme_prev) {
		ASSERT(pp->p_mapping != hme);
		ASSERT(hme->hme_prev->hme_page == pp);
		hme->hme_prev->hme_next = hme->hme_next;
	} else {
		ASSERT(pp->p_mapping == hme);
		pp->p_mapping = hme->hme_next;
		ASSERT((pp->p_mapping == NULL) ? (pp->p_share == 0) : 1);
	}

	if (hme->hme_next) {
		ASSERT(hme->hme_next->hme_page == pp);
		hme->hme_next->hme_prev = hme->hme_prev;
	}

	/*
	 * zero out the entry
	 */
	hme->hme_next = NULL;
	hme->hme_prev = NULL;
	hme->hme_hat = NULL;
	hme->hme_page = (page_t *)NULL;
}

/*
 * Get a hat structure from the freelist
 */
struct hat *
hat_gethat()
{
	struct hat *hat;

	mutex_enter(&hat_res_mutex);
	if ((hat = hatfree) == NULL)	/* "shouldn't happen" */
		panic("out of hats");

	hatfree = hat->hat_next;
	hat->hat_next = NULL;

	mutex_exit(&hat_res_mutex);
	return (hat);
}

void
hat_freehat(hat)
	register struct hat *hat;
{
	int i;

	mutex_enter(&hat->hat_mutex);

	mutex_enter(&hat_res_mutex);
	hat->hat_op = (struct hatops *)NULL;
	hat->hat_as = (struct as *)NULL;

	for (i = 0; i < HAT_PRIVSIZ; i++)
		hat->hat_data[i] = 0;

	mutex_exit(&hat->hat_mutex);

	hat->hat_next = hatfree;
	hatfree = hat;
	mutex_exit(&hat_res_mutex);
}

/*
 * hat_probe returns 1 if the translation for the address 'addr' is
 * loaded, zero otherwise.
 *
 * hat_probe should be used only for advisorary purposes because it may
 * occasionally return the wrong value. The implementation must guarantee that
 * returning the wrong value is a very rare event. hat_probe is used
 * to implement optimizations in the segment drivers.
 *
 * hat_probe doesn't acquire hat_mutex.
 */
int
hat_probe(hat, as, addr)
	struct hat	*hat;
	struct as	*as;
	caddr_t		addr;
{
	return (HATOP_PROBE(hat, as, addr));
}

/*
 * hat_map() is called when an address range is created to pre-allocate any
 * MMU resources such as segment descriptor tables, page tables etc that are
 * necessary to map the address range <addr, addr + len>. hat_map() does not
 * pre-load virtual to physical address translations.
 */

int
hat_map(hat, as, addr, len, flags)
	struct hat	*hat;
	struct as	*as;
	caddr_t		addr;
	u_int		len;
	int		flags;
{

	ASSERT(hat != NULL);
	return (HATOP_MAP(hat, as, addr, len, flags));
}

/*
 * hat_unmap is invoked  when an address range is destroyed, to unload the
 * virtual address to physical address translations and to free up any MMU
 * resources allocated for segment descriptor tables, page tables etc. for the
 * range <addr, addr + len>.
 */

void
hat_unmap(as, addr, len, flags)
	struct as	*as;
	caddr_t		addr;
	u_int		len;
	int		flags;
{
	struct hat *hat;

	for (hat = as->a_hat; hat != NULL; hat = hat->hat_next) {
		HATOP_UNMAP(hat, as, addr, len, flags);
	}
}

/*
 * Functions to set the "p_mod" and "p_ref" fields in the page structure.
 * These fields are protected by the hat layer lock (hat_page_mutex).
 * These exist as macros so that tracing code has the opportunity
 * to note the new values.
 */

void
hat_setmod(pp)
	page_t *pp;
{
	TRACE_3(TR_FAC_VM, TR_HAT_SETMOD,
		"hat_setmod:pp %x vp %x offset %x",
		pp, pp->p_vnode, pp->p_offset);

	HATLOCK_PAGE_ENTER(pp);
	PP_SETMOD(pp);
	HATLOCK_PAGE_EXIT(pp);
}

void
hat_setref(pp)
	page_t *pp;
{
	TRACE_3(TR_FAC_VM, TR_HAT_SETREF,
		"hat_setref:pp %x vp %x offset %x",
		pp, pp->p_vnode, pp->p_offset);

	HATLOCK_PAGE_ENTER(pp);
	PP_SETREF(pp);
	HATLOCK_PAGE_EXIT(pp);
}

void
hat_setrefmod(pp)
	page_t *pp;
{
	TRACE_3(TR_FAC_VM, TR_HAT_SETREFMOD,
		"hat_setrefmod:pp %x vp %x offset %x",
		pp, pp->p_vnode, pp->p_offset);

	HATLOCK_PAGE_ENTER(pp);
	PP_SETREFMOD(pp);
	HATLOCK_PAGE_EXIT(pp);
}

void
hat_clrmod(pp)
	page_t *pp;
{
	TRACE_3(TR_FAC_VM, TR_HAT_CLRMOD,
		"hat_clrmod:pp %x vp %x offset %x",
		pp, pp->p_vnode, pp->p_offset);

	HATLOCK_PAGE_ENTER(pp);
	PP_CLRMOD(pp);
	HATLOCK_PAGE_EXIT(pp);
}

void
hat_clrref(pp)
	page_t *pp;
{
	TRACE_3(TR_FAC_VM, TR_HAT_CLRREF,
		"hat_clrref:pp %x vp %x offset %x",
		pp, pp->p_vnode, pp->p_offset);

	HATLOCK_PAGE_ENTER(pp);
	PP_CLRREF(pp);
	HATLOCK_PAGE_EXIT(pp);
}

void
hat_clrrefmod(pp)
	page_t *pp;
{
	TRACE_3(TR_FAC_VM, TR_HAT_CLRREFMOD,
		"hat_clrrefmod:pp %x vp %x offset %x",
		pp, pp->p_vnode, pp->p_offset);

	HATLOCK_PAGE_ENTER(pp);
	PP_CLRREFMOD(pp);
	HATLOCK_PAGE_EXIT(pp);
}


/*
 * Functions to allow two flavors of locking,
 * sunmmu ==> single lock, no parallelism, low locking overhead, for UP
 * srmmu ==> multiple lock, more parallelism, for MP
 */

void
hat_mlist_enter(pp)
	page_t *pp;
{
	if (pp)
		HATLOCK_MLIST_ENTER(pp);
}

void
hat_mlist_exit(pp)
	page_t *pp;
{
	if (pp)
		HATLOCK_MLIST_EXIT(pp);
}

int
hat_mlist_held(pp)
	page_t *pp;
{
	return (HATLOCK_MLIST_HELD(pp));
}

void
hat_page_enter(pp)
	page_t *pp;
{
	HATLOCK_PAGE_ENTER(pp);
}

void
hat_page_exit(pp)
	page_t *pp;
{
	HATLOCK_PAGE_EXIT(pp);
}

void
hat_enter(as)
	struct as *as;
{
	HATLOCK_HAT_ENTER(as);
}

void
hat_exit(as)
	struct as *as;
{
	HATLOCK_HAT_EXIT(as);
}

void
hat_cachectl_enter(pp)
	struct page *pp;
{
	HATLOCK_CACHECTL_ENTER(pp);
}

void
hat_cachectl_exit(pp)
	struct page *pp;
{
	HATLOCK_CACHECTL_EXIT(pp);
}

/*
 * Copy top level mapping elements (L1 ptes or whatever)
 * that map from saddr to (saddr + len) in sas
 * to top level mapping elements from daddr in das.
 *
 * Hat_share()/unshare() return an (non-zero) error
 * when saddr and daddr are not properly aligned.
 *
 * The top level mapping element determines the alignment
 * requirement for saddr and daddr, depending on different
 * architectures.
 *
 * When hat_share()/unshare() are not supported,
 * HATOP_SHARE()/UNSHARE() return 0.
 */
int
hat_share(das, daddr, sas, saddr, len)
	struct as	*das, *sas;
	caddr_t		daddr, saddr;
	u_int		len;
{
	struct hat *hat;

	hat = das->a_hat;
	return (HATOP_SHARE(hat, das, daddr, sas, saddr, len));
}

/*
 * Invalidate top level mapping elements in as
 * starting from addr to (addr + size).
 */
void
hat_unshare(as, addr, size)
	struct as  *as;
	caddr_t    addr;
	u_int	size;

{
	struct hat *hat;

	hat = as->a_hat;
	HATOP_UNSHARE(hat, as, addr, size);
}
