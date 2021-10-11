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

#ifndef	_VM_SEG_MAP_H
#define	_VM_SEG_MAP_H

#pragma ident	"@(#)seg_map.h	1.35	94/04/20 SMI"
/*	From:	SVr4.0	"kernel:vm/seg_map.h	1.11"		*/

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * When segmap is created it is possible to program its behavior,
 *	using the create args [needed for performance reasons].
 * Segmap creates n lists of pages one
 *	list for each offset in the VAC, allocations will use a VAC
 *	consistent addresses for one of the n list. There is a lock
 *	for each list of pages.
 */
struct segmap_crargs {
	u_int	prot;
	u_int	vamask;
};

/*
 * Each smap struct represents a MAXBSIZE sized mapping to the
 * <sm_vp, sm_off> given in the structure.  The location of the
 * the structure in the array gives the virtual address of the
 * mapping.
 */
struct	smap {
	struct	vnode	*sm_vp;		/* vnode pointer (if mapped) */
	u_int		sm_off;		/* file offset for mapping */
	u_short		sm_bitmap;	/* bit map for locked translations */
	u_short		sm_refcnt;	/* reference count for uses */
	/*
	 * These next 3 entries can be coded as
	 * u_shorts if we are tight on memory.
	 */
	struct	smap	*sm_hash;	/* hash pointer */
	struct	smap	*sm_next;	/* next pointer */
	struct	smap	*sm_prev;	/* previous pointer */
	u_short		sm_flags;	/* flags */
	kcondvar_t sm_cv;		/* condition variable */
};

#define	SF_UNLOAD	0x01	/* unload in progress on smap slot */

/*
 * (Semi) private data maintained by the segmap driver per SEGMENT mapping
 *
 * The segment lock (short term lock) is necessary to protect fields
 * in the private data structure as well as in each smap entry.
 *
 * The following fields in segmap_data are read-only when the address
 * space is "read" locked, and don't require the segment lock:
 *
 *	smd_prot
 *	smd_hashsz
 */

struct	smcolor {
	struct	smap	*smc_free;	/* free list array pointer */
	struct	smap	**smc_hash;	/* pointer to hash table array */
	kmutex_t	smc_lock;	/* protects smap data of this color */
	int		smc_hashsz;	/* power-of-two hash tables size */
	u_char		smc_want;	/* someone wants a slot of this color */
	kcondvar_t	smc_free_cv;
};

struct	segmap_data {
	struct	smap	*smd_sm;	/* array of smap structures */
	struct  smcolor	*smd_colors;	/* array of separately locked colors */
	short		smd_ncolors;	/* number of colors */
	u_char		smd_prot;	/* protections for all smap's */
};

/*
 * Statistics for segmap operations.
 *
 * No explicit locking to protect these stats.
 */
struct segmapcnt {
	kstat_named_t	smc_fault;	/* number of segmap_faults */
	kstat_named_t	smc_faulta;	/* number of segmap_faultas */
	kstat_named_t	smc_getmap;	/* number of segmap_getmaps */
	kstat_named_t	smc_get_use;	/* getmaps that reuse existing map */
	kstat_named_t	smc_get_reclaim; /* getmaps that do a reclaim */
	kstat_named_t	smc_get_reuse;	/* getmaps that reuse a slot */
	kstat_named_t	smc_get_unused;	/* getmaps that reuse existing map */
	kstat_named_t	smc_get_nofree;	/* getmaps with no free slots */
	kstat_named_t	smc_rel_async;	/* releases that are async */
	kstat_named_t	smc_rel_write;	/* releases that write */
	kstat_named_t	smc_rel_free;	/* releases that free */
	kstat_named_t	smc_rel_abort;	/* releases that abort */
	kstat_named_t	smc_rel_dontneed; /* releases with dontneed set */
	kstat_named_t	smc_release;	/* releases with no other action */
	kstat_named_t	smc_pagecreate;	/* pagecreates */
};

/*
 * These are flags used on release.  Some of these might get handled
 * by segment operations needed for msync (when we figure them out).
 * SM_ASYNC modifies SM_WRITE.  SM_DONTNEED modifies SM_FREE.  SM_FREE
 * and SM_INVAL are mutually exclusive.
 */
#define	SM_WRITE	0x01		/* write back the pages upon release */
#define	SM_ASYNC	0x02		/* do the write asynchronously */
#define	SM_FREE		0x04		/* put pages back on free list */
#define	SM_INVAL	0x08		/* invalidate page (no caching) */
#define	SM_DONTNEED	0x10		/* less likely to be needed soon */

#define	MAXBSHIFT	13		/* log2(MAXBSIZE) */

#define	MAXBOFFSET	(MAXBSIZE - 1)
#define	MAXBMASK	(~MAXBOFFSET)

/*
 * SMAP_HASHAVELEN is the average length desired for this chain, from
 * which the size of the smd_hash table is derived at segment create time.
 * SMAP_HASHVPSHIFT is defined so that 1 << SMAP_HASHVPSHIFT is the
 * approximate size of a vnode struct.
 */
#define	SMAP_HASHAVELEN		4
#define	SMAP_HASHVPSHIFT	6

#define	SMAP_HASHFUNC(smc, vp, off) \
	(hash2ints((int)vp, (int)off) & ((smc)->smc_hashsz - 1))


#ifdef _KERNEL
/*
 * The kernel generic mapping segment.
 */
extern struct seg *segkmap;

/*
 * Public seg_map segment operations.
 */
extern int	segmap_create(struct seg *, void *);
extern void	segmap_pagecreate(struct seg *, caddr_t, u_int, int);
extern faultcode_t segmap_fault(struct hat *, struct seg *, caddr_t, u_int,
		enum fault_type, enum seg_rw);
extern caddr_t	segmap_getmap(struct seg *, struct vnode *, u_int);
extern caddr_t	segmap_getmapflt(struct seg *, struct vnode *, u_int, u_int,
			int, enum seg_rw);
extern int	segmap_release(struct seg *, caddr_t, u_int);
extern void	segmap_flush(struct seg *, struct vnode *);
extern void	segmap_inval(struct seg *, struct vnode *, u_int);

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _VM_SEG_MAP_H */
