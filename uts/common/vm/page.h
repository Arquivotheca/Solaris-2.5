/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * +++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 *		PROPRIETARY NOTICE (Combined)
 *
 * This source code is unpublished proprietary information
 * constituting, or derived under license from AT&T's UNIX(r) System V.
 * In addition, portions of such source code were derived from Berkeley
 * 4.3 BSD under license from the Regents of the University of
 * California.
 *
 *
 *
 *		Copyright Notice
 *
 * Notice of copyright on this source code product does not indicate
 * publication.
 *
 *	(c) 1986, 1987, 1988, 1989, 1990, 1993  Sun Microsystems, Inc
 *	(c) 1983, 1984, 1985, 1986, 1987, 1988, 1989  AT&T.
 *		All rights reserved.
 *
 */

#ifndef	_VM_PAGE_H
#define	_VM_PAGE_H

#pragma ident	"@(#)page.h	1.86	95/09/12 SMI"
/*	From:	SVr4.0	"kernel:vm/page.h	1.19"		*/

#ifdef	__cplusplus
extern "C" {
#endif

#if defined(_KERNEL) || defined(_KMEMUSER)
/*
 * Shared/Exclusive lock.
 */
typedef enum {
	SE_NOSTAT,
	SE_STAT			/* XXX - Not yet supported */
} se_type_t;

/*
 * Types of page locking supported by page_lock & friends.
 */
typedef enum {
	SE_SHARED,
	SE_EXCL			/* exclusive lock (value == -1) */
} se_t;

/*
 * For requesting that page_lock reclaim the page from the free list.
 */
typedef enum {
	P_RECLAIM,		/* reclaim page from free list */
	P_NO_RECLAIM		/* DON`T reclaim the page	*/
} reclaim_t;

#if defined(_LOCKTEST) || defined(_MPSTATS)
#define	SE_DEFAULT	SE_STAT
#else
#define	SE_DEFAULT	SE_NOSTAT
#endif

#endif	/* _KERNEL | _KMEMUSER */

typedef int	selock_t;

/*
 * Define VM_STATS to turn on all sorts of statistic gathering about
 * the VM layer.  By default, it is only turned on when DEBUG is
 * also defined.
 */
#ifdef DEBUG
#define	VM_STATS
#endif	/* DEBUG */

#ifdef VM_STATS
#define	VM_STAT_ADD(stat)	(stat)++
#else
#define	VM_STAT_ADD(stat)
#endif	/* VM_STATS */

#ifdef _KERNEL

/*
 * Macros to acquire and release the page logical lock.
 */
#define	page_struct_lock(pp)	mutex_enter(&page_llock)
#define	page_struct_unlock(pp)	mutex_exit(&page_llock)

#endif	/* _KERNEL */

#include <sys/t_lock.h>
#include <vm/hat.h>

/*
 * Each physical page has a page structure, which is used to maintain
 * these pages as a cache.  A page can be found via a hashed lookup
 * based on the [vp, offset].  If a page has an [vp, offset] identity,
 * then it is entered on a doubly linked circular list off the
 * vnode using the vpnext/vpprev pointers.   If the p_free bit
 * is on, then the page is also on a doubly linked circular free
 * list using next/prev pointers.  If the "p_selock" and "p_iolock"
 * are held, then the page is currently being read in (exclusive p_selock)
 * or written back (shared p_selock).  In this case, the next/prev pointers
 * are used to link the pages together for a consecutive i/o request.  If
 * the page is being brought in from its backing store, then other processes
 * will wait for the i/o to complete before attaching to the page since it
 * will have an "exclusive" lock.
 *
 * Each page structure has the locks described below along with
 * the fields they protect:
 *
 *	p_selock	This is a per-page shared/exclusive lock that is
 *			used to implement the logical shared/exclusive
 *			lock for each page.  The "shared" lock is normally
 *			used in most cases while the "exclusive" lock is
 *			required to destroy or retain exclusive access to
 *			a page (e.g., while reading in pages).  The appropriate
 *			lock is always held whenever there is any reference
 *			to a page structure (e.g., during i/o).
 *
 *				p_hash
 *				p_vnode
 *				p_offset
 *
 *				p_free
 *				p_age
 *
 *	p_iolock	This is a binary semaphore lock that provides
 *			exclusive access to the i/o list links in each
 *			page structure.  It is always held while the page
 *			is on an i/o list (i.e., involved in i/o).  That is,
 *			even though a page may be only `shared' locked
 *			while it is doing a write, the following fields may
 *			change anyway.  Normally, the page must be
 *			`exclusively' locked to change anything in it.
 *
 *				p_next
 *				p_prev
 *
 * The following fields are protected by the global page_llock:
 *
 *				p_lckcnt
 *				p_cowcnt
 *
 * The following lists are protected by the global page_freelock:
 *
 *				page_cachelist
 *				page_freelist
 *
 * The following, for our purposes, are protected by
 * the global freemem_lock:
 *
 *				freemem
 *				freemem_wait
 *				freemem_cv
 *
 * The following fields are protected by hat layer lock(s).  When a page
 * structure is not mapped and is not associated with a vnode (after a call
 * to page_hashout() for example) the p_nrm field may be modified with out
 * holding the hat layer lock:
 *
 *				p_nrm
 *				p_mapping
 *				p_share
 *
 * The following field is file system dependent.  How it is used and
 * the locking strategies applied are up to the individual file system
 * implementation.
 *
 *				p_fsdata
 *
 * The page structure is used to represent and control the system's
 * physical pages.  There is one instance of the structure for each
 * page that is not permenately allocated.  For example, the pages that
 * hold the page structures are permanently held by the kernel
 * and hence do not need page structures to track them.  The array
 * of page structures is allocated early on in the kernel's life and
 * is based on the amount of available physical memory.
 *
 * Each page structure may simultaneously appear on several linked lists.
 * The lists are:  hash list, free or in i/o list, and a vnode's page list.
 * Each type of list is protected by a different group of mutexes as described
 * below:
 *
 * The hash list is used to quickly find a page when the page's vnode and
 * offset within the vnode are known.  Each page that is hashed is
 * connected via the `p_hash' field.  The anchor for each hash is in the
 * array `page_hash'.  An array of mutexes, `ph_mutex', protects the
 * lists anchored by page_hash[].  To either search or modify a given hash
 * list, the appropriate mutex in the ph_mutex array must be held.
 *
 * The free list contains pages that are `free to be given away'.  For
 * efficiency reasons, pages on this list are placed in two catagories:
 * pages that are still associated with a vnode, and pages that are not
 * associated with a vnode.  Free pages always have their `p_free' bit set,
 * free pages that are still associated with a vnode also have their
 * `p_age' bit set.  Pages on the free list are connected via their
 * `p_next' and `p_prev' fields.  When a page is involved in some sort
 * of i/o, it is not free and these fields may be used to link associated
 * pages together.  At the moment, the free list is protected by a
 * single mutex `page_freelock'.  The list of free pages still associated
 * with a vnode is anchored by `page_cachelist' while other free pages
 * are anchored in architecture dependent ways (to handle page coloring etc.).
 *
 * Pages associated with a given vnode appear on a list anchored in the
 * vnode by the `v_pages' field.  They are linked together with
 * `p_vpnext' and `p_vpprev'.  The field `p_offset' contains a page's
 * offset within the vnode.  The pages on this list are not kept in
 * offset order.  These lists, in a manner similar to the hash lists,
 * are protected by an array of mutexes called `vph_hash'.  Before
 * searching or modifying this chain the appropriate mutex in the
 * vph_hash[] array must be held.
 *
 * Again, each of the lists that a page can appear on is protected by a
 * mutex.  Before reading or writing any of the fields comprising the
 * list, the appropriate lock must be held.  These list locks should only
 * be held for very short intervals.
 *
 * In addition to the list locks, each page structure contains a
 * shared/exclusive lock that protects various fields within it.
 * To modify one of these fields, the `p_selock' must be exclusively held.
 * To read a field with a degree of certainty, the lock must be at least
 * held shared.
 *
 * Removing a page structure from one of the lists requires holding
 * the appropriate list lock and the page's p_selock.  A page may be
 * prevented from changing identity, being freed, or otherwise modified
 * by acquiring p_selock shared.
 *
 * To avoid deadlocks, a strict locking protocol must be followed.  Basically
 * there are two cases:  In the first case, the page structure in question
 * is known ahead of time (e.g., when the page is to be added or removed
 * from a list).  In the second case, the page structure is not known and
 * must be found by searching one of the lists.
 *
 * When adding or removing a known page to one of the lists, first the
 * page must be exclusively locked (since at least one of its fields
 * will be modified), second the lock protecting the list must be acquired,
 * third the page inserted or deleted, and finally the list lock dropped.
 *
 * The more interesting case occures when the particular page structure
 * is not known ahead of time.  For example, when a call is made to
 * page_lookup(), it is not known if a page with the desired (vnode and
 * offset pair) identity exists.  So the appropriate mutex in ph_mutex is
 * acquired, the hash list searched, and if the desired page is found
 * an attempt is made to lock it.  The attempt to acquire p_selock must
 * not block while the hash list lock is held.  A deadlock could occure
 * if some other process was trying to remove the page from the list.
 * The removing process (following the above protocol) would have exclusively
 * locked the page, and be spinning waiting to acquire the lock protecting
 * the hash list.  Since the searching process holds the hash list lock
 * and is waiting to acquire the page lock, a deadlock occurs.
 *
 * The proper scheme to follow is: first, lock the appropriate list,
 * search the list, and if the desired page is found either use
 * page_trylock() (which will not block) or pass the address of the
 * list lock to page_lock().  If page_lock() can not acquire the page's
 * lock, it will drop the list lock before going to sleep.  page_lock()
 * returns a value to indicate if the list lock was dropped allowing the
 * calling program to react appropriately (i.e., retry the operation).
 *
 * If the list lock was dropped before the attempt at locking the page
 * was made, checks would have to be made to ensure that the page had
 * not changed identity before its lock was obtained.  This is because
 * the interval between dropping the list lock and acquiring the page
 * lock is indeterminate.
 *
 * In addition, when both a hash list lock (ph_mutex[]) and a vnode list
 * lock (vph_mutex[]) are needed, the hash list lock must be acquired first.
 * The routine page_hashin() is a good example of this sequence.
 * This sequence is ASSERTed by checking that the vph_mutex[] is not held
 * just before each acquisition of one of the mutexs in ph_mutex[].
 *
 * So, as a quick summary:
 *
 * 	pse_mutex[]'s protect the p_selock and p_cv fields.
 *
 * 	p_selock protects the p_free, p_age, p_vnode, p_offset and p_hash,
 *
 * 	ph_mutex[]'s protect the page_hash[] array and its chains.
 *
 * 	vph_mutex[]'s protect the v_pages field and the vp page chains.
 *
 *	First lock the page, then the hash chain, then the vnode chain.  When
 *	this is not possible `trylocks' must be used.  Sleeping while holding
 *	any of these mutexes (p_selock is not a mutex) is not allowed.
 *
 *
 *	field		reading		writing		    ordering
 *	======================================================================
 *	p_vnode		p_selock(E,S)	p_selock(E)
 *	p_offset
 *	p_free
 *	p_age
 *	=====================================================================
 *	p_hash		p_selock(E,S)	p_selock(E) &&	    p_selock, ph_mutex
 *					ph_mutex[]
 *	=====================================================================
 *	p_vpnext	p_selock(E,S)	p_selock(E) &&	    p_selock, vph_mutex
 *	p_vpprev			vph_mutex[]
 *	=====================================================================
 *	When the p_free bit is set:
 *
 *	p_next		p_selock(E,S)	p_selock(E) &&	    p_selock,
 *	p_prev				page_freelock	    page_freelock
 *
 *	When the p_free bit is not set:
 *
 *	p_next		p_selock(E,S)	p_selock(E) &&	    p_selock, p_iolock
 *	p_prev				p_iolock
 *	=====================================================================
 *	p_selock	pse_mutex[]	pse_mutex[]	    can`t acquire any
 *	p_cv						    other mutexes or
 *							    sleep while holding
 *							    this lock.
 *	=====================================================================
 *	p_lckcnt	p_selock(E,S)	p_selock(E) &&
 *	p_cowcnt			page_llock
 *	=====================================================================
 *	p_nrm		hat layer lock	hat layer lock
 *	p_mapping
 *	p_pagenum
 *	=====================================================================
 *
 *	where:
 *		E----> exclusive version of p_selock.
 *		S----> shared version of p_selock.
 *
 *
 *	Global data structures and variable:
 *
 *	field		reading		writing		    ordering
 *	=====================================================================
 *	page_hash[]	ph_mutex[]	ph_mutex[]	    can hold this lock
 *							    before acquiring
 *							    a vph_mutex or
 *							    pse_mutex.
 *	=====================================================================
 *	vp->v_pages	vph_mutex[]	vph_mutex[]	    can only acquire
 *							    a pse_mutex while
 *							    holding this lock.
 *	=====================================================================
 *	page_cachelist	page_freelock	page_freelock	    can't acquire any
 *	page_freelist	page_freelock	page_freelock
 *	=====================================================================
 *	freemem		freemem_lock	freemem_lock	    can't acquire any
 *	freemem_wait					    other mutexes while
 *	freemem_cv					    holding this mutex.
 *	=====================================================================
 *
 */
typedef struct page {
	u_int	p_free: 1,		/* on free list */
		p_age: 1,		/* on age free list */
		: 6;
	u_char	p_nrm;			/* non-cache, ref, mod readonly bits */
	kcondvar_t p_cv;		/* page struct's condition var */
	struct	hment *p_mapping;	/* hat specific translation info */
	selock_t p_selock;		/* shared/exclusive lock on the page */
	struct	vnode *p_vnode;		/* logical vnode this page is from */
	u_int	p_offset;		/* offset into vnode for this page */
	struct	page *p_hash;		/* hash by [vnode, offset] */
	struct	page *p_vpnext;		/* next page in vnode list */
	struct	page *p_vpprev;		/* prev page in vnode list */
	ksema_t	p_iolock;		/* protects the i/o list links */
	struct	page *p_next;		/* next page in free/intrans lists */
	struct	page *p_prev;		/* prev page in free/intrans lists */
	u_short	p_lckcnt;		/* number of locks on page data */
	u_short	p_cowcnt;		/* number of copy on write lock */
	u_int	p_pagenum;		/* physical page number */
	kcondvar_t p_mlistcv;		/* cond var for mapping list lock */
	u_int 	p_inuse: 1,		/* page mapping list in use */
		p_wanted: 1,		/* page mapping list wanted  */
		p_impl: 6;		/* hat implemenation bits */
	u_char	p_fsdata;		/* file system dependent byte */
	u_char	p_vcolor;		/* virtual color */
	u_char	p_index;		/* virtual color */
	u_short	p_share;		/* number of mappings to this page */
} page_t;

/*
 * Each segment of physical memory is described by a memseg struct. Within
 * a segment, memory is considered contiguous. The segments from a linked
 * list to describe all of physical memory. The list is ordered by increasing
 * physical addresses.
 */
struct memseg {
	page_t *pages, *epages;		/* [from, to] in page array */
	u_int pages_base, pages_end;	/* [from, to] in page numbers */
	struct memseg *next;		/* next segment in list */
};

/*
 * Page hash table is a power-of-two in size, externally chained
 * through the hash field.  PAGE_HASHAVELEN is the average length
 * desired for this chain, from which the size of the page_hash
 * table is derived at boot time and stored in the kernel variable
 * page_hashsz.  In the hash function it is given by PAGE_HASHSZ.
 * PAGE_HASHVPSHIFT is defined so that 1 << PAGE_HASHVPSHIFT is
 * the approximate size of a vnode struct.
 *
 * PAGE_HASH_FUNC returns an index into the page_hash[] array.  This
 * index is also used to derive the mutex that protects the chain.
 */
#define	PAGE_HASHSZ	page_hashsz
#define	PAGE_HASHAVELEN		4
#define	PAGE_HASHVPSHIFT	6
#define	PAGE_HASH_FUNC(vp, off) \
	((((off) >> PAGESHIFT) + ((int)(vp) >> PAGE_HASHVPSHIFT)) & \
		(PAGE_HASHSZ - 1))

#ifdef _KERNEL

/*
 * Flags used while creating pages.
 */
#define	PG_EXCL		0x0001
#define	PG_WAIT		0x0002
#define	PG_PHYSCONTIG	0x0004		/* NOT SUPPORTED */
#define	PG_MATCH_COLOR	0x0008		/* SUPPORTED by free list routines */

/*
 * se_trylock():	Try to acquire the "shared" or "exclusive" page lock.
 * se_tryupgrade():	Try to upgrade the "shared" lock on the page to an
 *			"exclusive" lock.
 * se_unlock():		Decrement a "shared" page's lock.
 *
 * se_trylock and se_tryupgrade return 1 on success and 0 on failure.
 */

#define	se_trylock(lock, se) \
	((se) == SE_EXCL ? \
		((*lock) == 0 ? \
			(THREAD_KPRI_REQUEST(), ((*(lock)) = -1), 1) : 0) : \
		((*lock) >= 0 ? ((*(lock))++, 1) : 0))

#define	se_tryupgrade(lock) \
	(*(lock) == 1 ? ((*(lock)) = -1, 1) : 0)

#define	se_unlock(lock) \
	(*(lock))--

#define	se_nolock(x)		(*(x) == 0)
#define	se_assert(x)		(*(x) > 0 || *(x) == -1)
#define	se_shared_assert(x)	(*(x) > 0)
#define	se_excl_assert(x)	(*(x) == -1)
#define	se_shared_lock(x)	(*(x) > 0)

extern	int page_hashsz;
extern	page_t **page_hash;

extern	page_t	*pages;			/* array of all page structures */
extern	page_t	*epages;		/* end of all pages */
extern	struct	memseg *memsegs;	/* list of memory segments */
extern	kmutex_t page_llock;		/* page logical lock mutex */
extern	kmutex_t freemem_lock;		/* freemem lock */

extern	u_int	total_pages;	/* total pages in the system (for /proc) */

/*
 * Variables controlling locking of physical memory.
 */
extern	u_int	pages_pp_kernel;	/* physical page locks by kernel */
extern	u_int	pages_pp_maximum;	/* tuning: lock + claim <= max */

#define	PG_FREE_LIST	1
#define	PG_CACHE_LIST	2

#define	PG_LIST_TAIL	0
#define	PG_LIST_HEAD	1

/*
 * Page frame operations.
 */
void	page_init(page_t *, u_int, u_int, struct memseg *);
page_t	*page_lookup(struct vnode *, u_int, se_t);
page_t	*page_lookup_nowait(struct vnode *, u_int, se_t);
page_t	*page_lookup_dev(struct vnode *, u_int, se_t);
page_t	*page_find(struct vnode *, u_int);
int	page_exists(struct vnode *, u_int);
page_t	*page_create(struct vnode *, u_int, u_int, u_int);
page_t	*page_create_va(struct vnode *, u_int, u_int, u_int,
	struct as *, caddr_t);
void	page_free(page_t *, int);
void	free_vp_pages(struct vnode *, u_int, u_int);
int	page_reclaim(page_t *, kmutex_t *);
void	page_destroy(page_t *, int);
void	page_destroy_free(page_t *);
void	page_rename(page_t *, struct vnode *, u_int);
int	page_hashin(page_t *, struct vnode *, u_int, kmutex_t *);
void	page_hashout(page_t *, kmutex_t *);
int	page_num_hashin(u_int, struct vnode *, u_int);
void	page_add(page_t **, page_t *);
void	page_sub(page_t **, page_t *);
page_t	**get_page_freelist(struct vnode *, u_int, struct as *, caddr_t);
/*
void	page_freelist_add(page_t *);
void	page_freelist_sub(page_t *);
*/
void	page_list_break(page_t **, page_t **, u_int);
void	page_list_concat(page_t **, page_t **);
void	page_vpadd(page_t **, page_t *);
void	page_vpsub(page_t **, page_t *);
int	page_lock(page_t *, se_t, kmutex_t *, reclaim_t);
int	page_trylock(page_t *, se_t);
int	page_try_reclaim_lock(page_t *, se_t);
int	page_tryupgrade(page_t *);
void	page_downgrade(page_t *);
void	page_unlock(page_t *);
int	page_pp_lock(page_t *, int, int);
void	page_pp_unlock(page_t *, int, int);
void	page_pp_useclaim(page_t *, page_t *, int);
int	page_addclaim(page_t *);
void	page_subclaim(page_t *);
u_int	page_pptonum(page_t *);
page_t	*page_numtopp(u_int, se_t);
page_t	*page_numtopp_nolock(u_int);
page_t	*page_numtopp_nowait(u_int, se_t);
void	ppcopy(page_t *, page_t *);
void	pagezero(page_t *, u_int, u_int);
void	se_init(selock_t *, caddr_t, se_type_t, void *);
void	page_io_lock(page_t *);
void	page_io_unlock(page_t *);
int	page_io_trylock(page_t *);
int	page_iolock_assert(page_t *);
int	page_busy(void);
void	page_lock_init(void);


kmutex_t *page_vnode_mutex(struct vnode *);
kmutex_t *page_se_mutex(struct page *);

#endif	/* _KERNEL */
/*
 * The p_nrm field holds hat layer information.
 * For example, the non-cached, referenced, and modified bits.
 * It is protected by the hat layer.
 */
#define	P_MOD	0x1		/* the modified bit */
#define	P_REF	0x2		/* the referenced bit */
#define	P_PNC	0x4		/* non-caching is permanent bit */
#define	P_TNC	0x8		/* non-caching is temporary bit */
#define	P_RO	0x10		/* Read only page */

#define	PP_ISMOD(pp)		((pp)->p_nrm & P_MOD)
#define	PP_ISREF(pp)		((pp)->p_nrm & P_REF)
#define	PP_ISNC(pp)		((pp)->p_nrm & (P_PNC|P_TNC))
#define	PP_ISPNC(pp)		((pp)->p_nrm & P_PNC)
#define	PP_ISTNC(pp)		((pp)->p_nrm & P_TNC)
#define	PP_ISRO(pp)		((pp)->p_nrm & P_RO)

#define	PP_SETMOD(pp)		((pp)->p_nrm |= P_MOD)
#define	PP_SETREF(pp)		((pp)->p_nrm |= P_REF)
#define	PP_SETREFMOD(pp)	((pp)->p_nrm |= (P_REF|P_MOD))
#define	PP_SETPNC(pp)		((pp)->p_nrm |= P_PNC)
#define	PP_SETTNC(pp)		((pp)->p_nrm |= P_TNC)
#define	PP_SETRO(pp)		((pp)->p_nrm |= P_RO)
#define	PP_SETREFRO(pp)		((pp)->p_nrm |= (P_REF|P_RO))
#define	PP_SETREFMODRO(pp)	((pp)->p_nrm |= (P_REF|P_MOD|P_RO))

#define	PP_CLRMOD(pp)		((pp)->p_nrm &= ~P_MOD)
#define	PP_CLRREF(pp)		((pp)->p_nrm &= ~P_REF)
#define	PP_CLRREFMOD(pp)	((pp)->p_nrm &= ~(P_REF|P_MOD))
#define	PP_CLRPNC(pp)		((pp)->p_nrm &= ~P_PNC)
#define	PP_CLRTNC(pp)		((pp)->p_nrm &= ~P_TNC)
#define	PP_CLRRO(pp)		((pp)->p_nrm &= ~P_RO)

#ifdef	__cplusplus
}
#endif

#endif	/* _VM_PAGE_H */
