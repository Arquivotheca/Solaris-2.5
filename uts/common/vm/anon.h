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

#ifndef	_VM_ANON_H
#define	_VM_ANON_H

#pragma ident	"@(#)anon.h	1.46	94/11/30 SMI"
/*	From: SVr4.0 "kernel:vm/anon.h	1.8"			*/

#include <sys/cred.h>
#include <vm/seg.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * VM - Anonymous pages.
 */

/*
 *	Each anonymous page, either in memory or in swap, has an anon structure.
 * The structure (slot) provides a level of indirection between anonymous pages
 * and their backing store.
 *
 *	(an_vp, an_off) names the vnode of the anonymous page for this slot.
 *
 * 	(an_pvp, an_poff) names the location of the physical backing store
 * 	for the page this slot represents. If the name is null there is no
 * 	associated physical store. The physical backing store location can
 *	change while the slot is in use.
 *
 *	an_hash is a hash list of anon slots. The list is hashed by
 * 	(an_vp, an_off) of the associated anonymous page and provides a
 *	method of going from the name of an anonymous page to its
 * 	associated anon slot.
 *
 *	an_refcnt holds a reference count which is the number of separate
 * 	copies that will need to be created in case of copy-on-write.
 *	A refcnt > 0 protects the existence of the slot. The refcnt is
 * 	initialized to 1 when the anon slot is created in anon_alloc().
 *	If a client obtains an anon slot and allows multiple threads to
 * 	share it, then it is the client's responsibility to insure that
 *	it does not allow one thread to try to reference the slot at the
 *	same time as another is trying to decrement the last count and
 *	destroy the anon slot. E.g., the seg_vn segment type protects
 *	against this with higher level locks.
 */


struct anon {
	struct vnode *an_vp;	/* vnode of anon page */
	u_int an_off;		/* offset of anon page */
	struct vnode *an_pvp;	/* vnode of physical backing store */
	u_int an_poff;		/* offset in vnode */
	int an_refcnt;		/* # of people sharing slot */
	struct anon *an_hash;	/* hash table of anon slots */
};

#ifdef _KERNEL
/*
 * The swapinfo_lock protects:
 *		swapinfo list
 *		individual swapinfo structures
 *
 * The anoninfo_lock protects:
 *		anoninfo counters
 *
 * The anonhash_lock protects:
 *		anon hash lists
 *		anon slot fields
 *
 * Fields in the anon slot which are read-only for the life of the slot
 * (an_vp, an_off) do not require the anonhash_lock be held to access them.
 * If you access a field without the anonhash_lock held you must be holding
 * the slot with an_refcnt to make sure it isn't destroyed.
 * To write (an_pvp, an_poff) in a given slot you must also hold the
 * p_iolock of the anonymous page for slot.
 */
extern kmutex_t anoninfo_lock;
extern kmutex_t swapinfo_lock;
extern kmutex_t anonhash_lock[];

/*
 * Global hash table to provide a function from (vp, off) -> ap
 */
extern int anon_hash_size;
extern struct anon **anon_hash;
#define	ANON_HASH_SIZE	anon_hash_size
#define	ANON_HASHAVELEN	4
#define	ANON_HASH(VP, OFF)	\
	((((uint)(VP) >> 7)  ^ ((OFF) >> PAGESHIFT)) & (ANON_HASH_SIZE - 1))

#define	AH_LOCK_SIZE	64
#define	AH_LOCK(vp, off) (ANON_HASH((vp), (off)) & (AH_LOCK_SIZE -1))

#endif	/* _KERNEL */

/*
 * Anonymous backing store accounting structure.
 * ani_max = total reservable slots on physical (disk-backed) swap
 * ani_resv = total slots reserved for use by clients
 * 	if ani_resv > ani_max then we have reserved space for memory backed
 * swap by taking allocatable memory from the system pool (i.e. from availrmem)
 * ani_free = # unallocated physical slots + # of reserved unallocated memory
 * slots
 * 	(ani_max - ani_free <= ani_resv)
 * Total slots currently available for reservation =
 *	MAX(ani_max - ani_resv, 0) + (availrmem - swapfs_minfree)
 */
struct anoninfo {
	u_int	ani_max;
	u_int	ani_free;
	u_int	ani_resv;
};

/*
 * Define the NCPU pool of the ani_free counters. Update the counter
 * of the cpu on which the thread is running and in every clock intr
 * sync anoninfo.ani_free with the current total off all the NCPU entries.
 */

typedef	struct	ani_free {
	kmutex_t	ani_lock;
	long		ani_count;
} ani_free_t;

#define	ANI_MAX_POOL	8
extern	ani_free_t	ani_free_pool[];

#define	ANI_ADD(inc)	{ \
	ani_free_t	*anifp; \
	int		index; \
	index = (CPU->cpu_id & (ANI_MAX_POOL - 1)); \
	anifp = &ani_free_pool[index]; \
	mutex_enter(&anifp->ani_lock); \
	anifp->ani_count += inc; \
	mutex_exit(&anifp->ani_lock); \
}

/*
 * The anon_map structure is used by various clients of the anon layer to
 * manage anonymous memory.   When anonymous memory is shared,
 * then the different clients sharing it will point to the
 * same anon_map structure.  Also, if a segment is unmapped
 * in the middle where an anon_map structure exists, the
 * newly created segment will also share the anon_map structure,
 * although the two segments will use different ranges of the
 * anon array.  When mappings are private (or shared with
 * a reference count of 1), an unmap operation will free up
 * a range of anon slots in the array given by the anon_map
 * structure.  Because of fragmentation due to this unmapping,
 * we have to store the size of the anon array in the anon_map
 * structure so that we can free everything when the referernce
 * count goes to zero.
 */
struct anon_map {
	kmutex_t serial_lock;	/* serialize anon allocation operations */
	kmutex_t lock;		/* protect anon_map and anon ptr array */
	u_int	refcnt;		/* reference count on this structure */
	u_int	size;		/* size in bytes mapped by the anon array */
	struct	anon **anon;	/* pointer to an array of anon * pointers */
	u_int	swresv;		/* swap space reserved for this anon_map */
};

#ifdef _KERNEL

extern	struct anoninfo anoninfo;

#if	defined(__STDC__)	/* prototypes not for use by adbgen */

extern void	anon_init();
extern struct	anon *anon_alloc(struct vnode *, u_int);
extern void	anon_dup(struct anon **, struct anon **, u_int);
extern void	anon_free(struct anon **, u_int);
extern int	anon_getpage(struct anon **, u_int *, struct page **, u_int,
		    struct seg *, caddr_t, enum seg_rw, struct cred *);
extern struct page	*anon_private(struct anon **, struct seg *, caddr_t,
		    struct page *, u_int, struct cred *);
extern struct page	*anon_zero(struct seg *, caddr_t, struct anon **,
		    struct cred *);
extern int	anon_resvmem(u_int, u_int);
extern void	anon_unresv(u_int);
struct anon_map	*anonmap_alloc(u_int, u_int);
void		anonmap_free(struct anon_map *);
void		anon_decref(struct anon *);
int		non_anon(struct anon **, u_int *, u_int *);
u_int		anon_pages(struct anon **, u_int, u_int);

#endif	/* __STDC__ */

/*
 * anon_resv checks to see if there is enough swap space to fulfill a
 * request and if so, reserves the appropriate anonymous memory resources.
 * anon_checkspace just checks to see if there is space to fulfill the request,
 * without taking any resources.  Both return 1 if successful and 0 if not.
 */
#define	anon_resv(pages)	anon_resvmem((pages), 1)
#define	anon_checkspace(pages)	anon_resvmem((pages), 0)

/*
 * Flags to anon_private
 */
#define	STEAL_PAGE	0x1	/* page can be stolen */
#define	LOCK_PAGE	0x2	/* page must be ``logically'' locked */

extern int anon_debug;

#ifdef ANON_DEBUG

#define	A_ANON	0x01
#define	A_RESV	0x02
#define	A_MRESV	0x04

/* vararg-like debugging macro. */
#define	ANON_PRINT(f, printf_args) \
		if (anon_debug & f) \
			printf printf_args

#else	/* ANON_DEBUG */

#define	ANON_PRINT(f, printf_args)

#endif	/* ANON_DEBUG */

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _VM_ANON_H */
