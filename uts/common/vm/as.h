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
 *	(c) 1986, 1987, 1988, 1989, 1990  Sun Microsystems, Inc
 *	(c) 1983, 1984, 1985, 1986, 1987, 1988, 1989  AT&T.
 *		All rights reserved.
 *
 */

#ifndef	_VM_AS_H
#define	_VM_AS_H

#pragma ident	"@(#)as.h	1.50	94/04/13 SMI"
/*	From: SVr4.0 "kernel:vm/as.h	1.11"			*/

#include <vm/seg.h>
#include <vm/faultcode.h>
#include <vm/hat.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * VM - Address spaces.
 */

/*
 * Each address space consists of a sorted list of segments
 * and machine dependent address translation information.
 *
 * All the hard work is in the segment drivers and the
 * hardware address translation code.
 *
 * The segment list can be represented as a simple linked list or as a
 * skiplist.  The skiplist facilitates faster searches when there are
 * many segments.  A linked list is used until a_nsegs grows beyond
 * AS_MUTATION_THRESH.  At that point, the linked list mutates into a
 * skiplist.  a_lrep indicates the list's current representation.
 *
 * The address space lock (a_lock) is a long term lock which serializes
 * access to certain operations (as_map, as_unmap) and protects the
 * underlying generic segment data (seg.h) along with some fields in the
 * address space structure as shown below:
 *
 *	address space structure 	segment structure
 *
 *	a_segs				s_base
 *	a_size				s_size
 *	a_nsegs				s_as
 *	a_hilevel			s_next
 *	a_cache				s_prev
 *	a_lrep				s_ops
 *	a_tail				s_data
 *
 * The address space contents lock (a_contents) is a short term
 * lock that protects most of the data in the address space structure.
 * This lock is always acquired after the "a_lock" in all situations
 * except while dealing with a_claimgap to avoid deadlocks.
 *
 * The following fields are protected by this lock:
 *
 *	a_paglck
 *	a_claimgap
 *	a_seglast
 *	a_unmapwait
 *	a_cache
 *
 * Note that a_cache can be updated while a_lock is held for read, but
 * when the segment list representation is a skiplist (a_lrep ==
 * AS_LREP_SKIPLIST), updates to (*a_cache.spath) are protected by the
 * mutex lock a_contents.  This ensures the atomicity of the update,
 * and guarantees internal consistency of spath's contents.  During a
 * skiplist insertion or deletion, however, a_cache is also protected
 * by a_lock being held for write.
 *
 * The address space lock (a_lock) is always held prior to any segment
 * operation.  Some segment drivers use the address space lock to protect
 * some or all of their segment private data, provided the version of
 * "a_lock" (read vs. write) is consistent with the use of the data.
 *
 * The following fields are protected by the hat layer lock:
 *
 *	a_vbits
 *	a_hat
 *	a_hrm
 *	a_rss
 */
struct as {
	kmutex_t a_contents;	/* protect certain fields in the structure */
	u_int	a_paglck: 1;	/* lock mappings into address space */
	u_int	a_claimgap: 1;	/* serialize access to ranges from as_gap */
	u_int	a_unmapwait: 1;	/* waiting to unmap a segment */
	u_int	: 5;
	u_char	a_vbits;	/* used for collecting statistics */
	kcondvar_t a_cv;	/* used by as_rangelock */
	struct	hat *a_hat;	/* hat structure */
	struct	hrmstat *a_hrm; /* ref and mod bits */
	int	a_rss;		/* pages used by this address space */
	union {
		struct seg *seglast;	/* last segment hit on the addr space */
		ssl_spath *spath;	/* last search path in seg skiplist */
	} a_cache;
	krwlock_t a_lock;	/* protects fields below + a_cache */
	seg_next a_segs;	/* segments in this address space. */
	size_t	a_size;		/* size of address space */
	struct 	seg *a_tail;	/* last element in the segment list. */
	u_int	a_nsegs;	/* number of elements in segment list */
	u_char	a_lrep;		/* representation of a_segs: see #defines */
	u_char	a_hilevel;	/* highest level in the a_segs skiplist */
	u_short	a_unused1;
};

/*
 * List representations; values for a_listrep
 */
#define	AS_LREP_LINKEDLIST	0
#define	AS_LREP_SKIPLIST	1

#ifdef _KERNEL

/*
 * Segment list representation mutation threshold.
 */
#define	AS_MUTATION_THRESH	225

/*
 * Macro to turn a (seg_next) into a (seg *)
 */
#define	AS_SEGP(as, n)	((as)->a_lrep == AS_LREP_LINKEDLIST ? \
			    ((n).list) : ((n).skiplist->segs[0]))

/*
 * Flags for as_gap.
 */
#define	AH_DIR		0x1	/* direction flag mask */
#define	AH_LO		0x0	/* find lowest hole */
#define	AH_HI		0x1	/* find highest hole */
#define	AH_CONTAIN	0x2	/* hole must contain `addr' */

extern struct as kas;		/* kernel's address space */

/*
 * Macros for address space locking.
 */
#define	AS_LOCK_ENTER(as, lock, type)	rw_enter((lock), (type))
#define	AS_LOCK_EXIT(as, lock)		rw_exit((lock))
#define	AS_LOCK_DESTROY(as, lock)	rw_destroy((lock))

/*
 * Macros to test lock states.
 */
#define	AS_LOCK_HELD(as, lock)		RW_LOCK_HELD((lock))
#define	AS_READ_HELD(as, lock)		RW_READ_HELD((lock))
#define	AS_WRITE_HELD(as, lock)		RW_WRITE_HELD((lock))

void	as_init(void);
struct	seg *as_segat(struct as *, caddr_t);
void	as_rangelock(struct as *);
void	as_rangeunlock(struct as *);
void	as_rangewait(struct as *, kcondvar_t *);
void	as_rangebroadcast(struct as *, kcondvar_t *);
struct	as *as_alloc(void);
void	as_free(struct as *);
int	as_dup(struct as *, struct as **);
struct	seg *as_findseg(struct as *, caddr_t, int);
int	as_addseg(struct as *, struct seg *);
struct	seg *as_removeseg(struct as *, caddr_t);
faultcode_t as_fault(struct hat *, struct as *, caddr_t, u_int,
	    enum fault_type, enum seg_rw);
faultcode_t as_faulta(struct as *, caddr_t, u_int);
int	as_setprot(struct as *, caddr_t, u_int, u_int);
int	as_checkprot(struct as *, caddr_t, u_int, u_int);
int	as_unmap(struct as *, caddr_t, u_int);
int	as_map(struct as *, caddr_t, u_int, int ((*)()), caddr_t);
int	as_gap(struct as *, u_int, caddr_t *, u_int *, int, caddr_t);
int	as_memory(struct as *, caddr_t *, u_int *);
u_int	as_swapout(struct as *);
int	as_incore(struct as *, caddr_t, u_int, char *, u_int *);
int	as_ctl(struct as *, caddr_t, u_int, int, int, caddr_t,
	    ulong *, size_t);
u_int	as_getprot(struct as *, caddr_t *, caddr_t *);
int	as_exec(struct as *, caddr_t, int, struct as *, caddr_t, u_int);

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _VM_AS_H */
