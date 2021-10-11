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

#ifndef	_VM_SEG_H
#define	_VM_SEG_H

#pragma ident	"@(#)seg.h	1.42	94/03/28 SMI"
/*	From:	SVr4.0	"kernel:vm/seg.h	1.10"		*/

#include <sys/vnode.h>
#include <vm/seg_enum.h>
#include <vm/faultcode.h>
#include <vm/hat.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * VM - Segments.
 */

/*
 * Segment skiplist parameters.
 *
 * SSL_BFACTOR must be a power of 2, and
 * SSL_BFACTOR * SSL_NLEVELS must be <= 32
 * or ssl_random() can run out of bits.
 */

#define	SSL_NLEVELS		4	/* Number of levels in segs_skiplist */
#define	SSL_BFACTOR		4	/* Branch factor in skip list */
#define	SSL_LOG2BF		2	/* log2 of SSL_BFACTOR */
#define	SSL_UNUSED		((struct seg *)-1)

typedef struct {
	struct seg *segs[SSL_NLEVELS];	/* head/next in a segs skiplist */
} seg_skiplist;

typedef struct {
	seg_skiplist *ssls[SSL_NLEVELS];	/* an ssl search path */
} ssl_spath;

typedef union {
	struct seg	*list;		/* "next" of simple linked list */
	seg_skiplist	*skiplist;	/* "next" of segment skiplist */
} seg_next;

/*
 * An address space contains a set of segments, managed by drivers.
 * Drivers support mapped devices, sharing, copy-on-write, etc.
 *
 * The seg structure contains a lock to prevent races, the base virtual
 * address and size of the segment, a back pointer to the containing
 * address space, pointers to maintain a circularly doubly linked list
 * of segments in the same address space, and procedure and data hooks
 * for the driver.  The seg list on the address space is sorted by
 * ascending base addresses and overlapping segments are not allowed.
 *
 * After a segment is created, faults may occur on pages of the segment.
 * When a fault occurs, the fault handling code must get the desired
 * object and set up the hardware translation to the object.  For some
 * objects, the fault handling code also implements copy-on-write.
 *
 * When the hat wants to unload a translation, it can call the unload
 * routine which is responsible for processing reference and modify bits.
 *
 * Each segment is protected by it's containing address space lock.  To
 * access any field in the segment structure, the "as" must be locked.
 * If a segment field is to be modified, the address space lock must be
 * write locked.
 */

struct seg {
	caddr_t	s_base;			/* base virtual address */
	u_int	s_size;			/* size in bytes */
	struct	as *s_as;		/* containing address space */
	seg_next s_next;		/* next seg in this address space */
	struct	seg *s_prev;		/* prev seg in this address space */
	struct	seg_ops {
		int	(*dup)(struct seg *, struct seg *);
		int	(*unmap)(struct seg *, caddr_t, u_int);
		void	(*free)(struct seg *);
		faultcode_t (*fault)(struct hat *, struct seg *, caddr_t,
				u_int, enum fault_type, enum seg_rw);
		faultcode_t (*faulta)(struct seg *, caddr_t);
		int	(*setprot)(struct seg *, caddr_t, u_int, u_int);
		int	(*checkprot)(struct seg *, caddr_t, u_int, u_int);
		int	(*kluster)(struct seg *, caddr_t, int);
		u_int	(*swapout)(struct seg *);
		int	(*sync)(struct seg *, caddr_t, u_int, int, u_int);
		int	(*incore)(struct seg *, caddr_t, u_int, char *);
		int	(*lockop)(struct seg *, caddr_t, u_int, int, int,
				ulong *, size_t);
		int	(*getprot)(struct seg *, caddr_t, u_int, u_int *);
		off_t	(*getoffset)(struct seg *, caddr_t);
		int	(*gettype)(struct seg *, caddr_t);
		int	(*getvp)(struct seg *, caddr_t, struct vnode **);
		int	(*advise)(struct seg *, caddr_t, u_int, int);
		void	(*dump)(struct seg *);
	} *s_ops;
	void *s_data;			/* private data for instance */
};

#ifdef _KERNEL
/*
 * Generic segment operations
 */
extern	void	seg_init(void);
extern	struct	seg *seg_alloc(struct as *, caddr_t, u_int);
extern	int	seg_attach(struct as *, caddr_t, u_int, struct seg *);
extern	void	seg_unmap(struct seg *);
extern	void	seg_free(struct seg *);

#define	SEGOP_DUP(s, n)		    (*(s)->s_ops->dup)((s), (n))
#define	SEGOP_UNMAP(s, a, l)	    (*(s)->s_ops->unmap)((s), (a), (l))
#define	SEGOP_FREE(s)		    (*(s)->s_ops->free)((s))
#define	SEGOP_FAULT(h, s, a, l, t, rw) \
		(*(s)->s_ops->fault)((h), (s), (a), (l), (t), (rw))
#define	SEGOP_FAULTA(s, a)	    (*(s)->s_ops->faulta)((s), (a))
#define	SEGOP_SETPROT(s, a, l, p)   (*(s)->s_ops->setprot)((s), (a), (l), (p))
#define	SEGOP_CHECKPROT(s, a, l, p) (*(s)->s_ops->checkprot)((s), (a), (l), (p))
#define	SEGOP_KLUSTER(s, a, d)	    (*(s)->s_ops->kluster)((s), (a), (d))
#define	SEGOP_SWAPOUT(s)	    (*(s)->s_ops->swapout)((s))
#define	SEGOP_SYNC(s, a, l, atr, f) \
		(*(s)->s_ops->sync)((s), (a), (l), (atr), (f))
#define	SEGOP_INCORE(s, a, l, v)    (*(s)->s_ops->incore)((s), (a), (l), (v))
#define	SEGOP_LOCKOP(s, a, l, atr, op, b, p) \
		(*(s)->s_ops->lockop)((s), (a), (l), (atr), (op), (b), (p))
#define	SEGOP_GETPROT(s, a, l, p)   (*(s)->s_ops->getprot)((s), (a), (l), (p))
#define	SEGOP_GETOFFSET(s, a)	    (*(s)->s_ops->getoffset)((s), (a))
#define	SEGOP_GETTYPE(s, a)	    (*(s)->s_ops->gettype)((s), (a))
#define	SEGOP_GETVP(s, a, vpp)	    (*(s)->s_ops->getvp)((s), (a), (vpp))
#define	SEGOP_ADVISE(s, a, l, b)    (*(s)->s_ops->advise)((s), (a), (l), (b))
#define	SEGOP_DUMP(s)		    (*(s)->s_ops->dump)((s))

#define	seg_page(seg, addr) \
	((u_int)(((addr) - (seg)->s_base) >> PAGESHIFT))

#define	seg_pages(seg) \
	((u_int)(((seg)->s_size + PAGEOFFSET) >> PAGESHIFT))

#ifdef VMDEBUG

u_int	seg_page(struct seg *, caddr_t);
u_int	seg_pages(struct seg *);

#endif	/* VMDEBUG */

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _VM_SEG_H */
