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
 *	(c) 1986,1987,1988,1989,1990  Sun Microsystems, Inc
 *	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *		All rights reserved.
 *
 */

#ifndef	_VM_SEG_VN_H
#define	_VM_SEG_VN_H

#pragma ident	"@(#)seg_vn.h	1.37	95/01/11 SMI"
/*	From:	SVr4.0	"kernel:vm/seg_vn.h	1.7"		*/

#include <vm/anon.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * A pointer to this structure is passed to segvn_create().
 */
struct segvn_crargs {
	struct	vnode *vp;	/* vnode mapped from */
	u_int	offset;		/* starting offset of vnode for mapping */
	struct	cred *cred;	/* credentials */
	u_char	type;		/* type of sharing done */
	u_char	prot;		/* protections */
	u_char	maxprot;	/* maximum protections */
	u_int	flags;		/* flags */
	struct	anon_map *amp;	/* anon mapping to map to */
};

/*
 * (Semi) private data maintained by the seg_vn driver per segment mapping.
 *
 * The read/write segment lock protects all of segvn_data including the
 * vpage array.  All fields in segvn_data are treated as read-only when
 * the "read" version of the address space and the segment locks are held.
 * The "write" version of the segment lock, however, is required in order to
 * update the following fields:
 *
 *	pageprot
 *	prot
 *	amp
 *	vpage
 *
 * 	softlockcnt
 * is written by acquiring either the readers lock on the segment and
 * freemem lock, or any lock combination which guarantees exclusive use
 * of this segment (e.g., adress space writers lock,
 * address space readers lock + segment writers lock).
 */
struct	segvn_data {
	krwlock_t lock;		/* protect segvn_data and vpage array */
	u_char	pageprot;	/* true if per page protections present */
	u_char	prot;		/* current segment prot if pageprot == 0 */
	u_char	maxprot;	/* maximum segment protections */
	u_char	type;		/* type of sharing done */
	struct	vnode *vp;	/* vnode that segment mapping is to */
	u_int	offset;		/* starting offset of vnode for mapping */
	u_int	anon_index;	/* starting index into anon_map anon array */
	struct	anon_map *amp;	/* pointer to anon share structure, if needed */
	struct	vpage *vpage;	/* per-page information, if needed */
	struct	cred *cred;	/* mapping credentials */
	u_int	swresv;		/* swap space reserved for this segment */
	u_char	advice;		/* madvise flags for segment */
	u_char	pageadvice;	/* true if per page advice set */
	u_short  flags;		/* flags - from sys/mman.h */
	int 	softlockcnt;	/* # of pages SOFTLOCKED in seg */
};

#ifdef _KERNEL

/*
 * Macros for segvn segment driver locking.
 */
#define	SEGVN_LOCK_ENTER(as, lock, type)	rw_enter((lock), (type))
#define	SEGVN_LOCK_EXIT(as, lock)		rw_exit((lock))

/*
 * Macros to test lock states.
 */
#define	SEGVN_LOCK_HELD(as, lock)		RW_LOCK_HELD((lock))
#define	SEGVN_READ_HELD(as, lock)		RW_READ_HELD((lock))
#define	SEGVN_WRITE_HELD(as, lock)		RW_WRITE_HELD((lock))

/*
 * Macro used to detect the need to Break the sharing of COW pages
 *
 * The rw == S_WRITE is for the COW case
 * rw == S_READ and type == SOFTLOCK is for the physio case
 * We don't want to share a softlocked page because it can cause problems
 * with multithreaded apps.
 */
#define	BREAK_COW_SHARE(rw, type, seg_type) ((rw == S_WRITE || \
	(rw == S_READ && type == F_SOFTLOCK)) && \
	seg_type == MAP_PRIVATE)

extern void	segvn_init(void);
extern int	segvn_create(struct seg *, void *);

extern	struct seg_ops segvn_ops;

/*
 * Provided as shorthand for creating user zfod segments.
 */
extern	caddr_t zfod_argsp;
extern	caddr_t kzfod_argsp;

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _VM_SEG_VN_H */
