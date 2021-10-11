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
 *	(c) 1986,1987,1988,1989,1990,1993,1994  Sun Microsystems, Inc
 *	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *		All rights reserved.
 *
 */


#ifndef	_SYS_SEG_MAPDEV_H
#define	_SYS_SEG_MAPDEV_H

#pragma ident	"@(#)seg_mapdev.h	1.7	95/01/12 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Structure whose pointer is passed to the segmapdev_create routine
 */

struct segmapdev_crargs {
	int	(*mapfunc)(dev_t dev, off_t off, int prot); /* map function */
	dev_t	dev;		/* device number */
	u_int	offset;		/* starting offset */
	u_char	prot;		/* protection */
	u_char	maxprot;	/* maximum protection */
	u_int	flags;
	struct ddi_mapdev_ctl *m_ops;	/* Mapdev ops struct */
	void	*private_data;		/* Driver private data */
	ddi_mapdev_handle_t *handle;	/* Return the address of the segment */
};

struct segmapdev_ctx {
	kmutex_t		lock;
	kcondvar_t		cv;
	dev_t			dev; /* Device to which we are mapping */
	int			refcnt; /* Number of threads with mappings */
	u_int			oncpu;	/* this context is running on a cpu */
	int			timeout; /* Timeout ID */
	struct segmapdev_ctx	*next;
	u_int			id;	/* handle grouping id */
};

/*
 * (Semi) private data maintained by the segmapdev driver per
 * segment mapping
 *
 * The segment lock is necessary to protect fields that are modified
 * when the "read" version of the address space lock is held.  This lock
 * is not needed when the segment operation has the "write" version of
 * the address space lock (it would be redundant).
 *
 * The following fields in segdev_data are read-only when the address
 * space is "read" locked, and don't require the segment lock:
 *
 *	vp
 *	offset
 *	mapfunc
 *	maxprot
 */
struct segmapdev_data {
	kmutex_t	lock;	/* protects segdev_data */
	kcondvar_t	wait;	/* makes driver callback single threaded */
	int	(*mapfunc)(dev_t dev, off_t off, int prot);
				/* really returns struct pte, not int */
	u_int	offset;		/* device offset for start of mapping */
	struct	vnode *vp;	/* vnode associated with device */
	u_char	pageprot;	/* true if per page protections present */
	u_char	prot;		/* current segment prot if pageprot == 0 */
	u_char	maxprot;	/* maximum segment protections */
	u_int	flags;		/* Fault handling flags */
	struct	vpage *vpage;	/* per-page protection information, if needed */
	u_char	pagehat_flags;	/* true if per page hat_access_flags */
	u_int	hat_flags;	/* current HAT FLAGS segment */
	u_int	*vpage_hat_flags;	/* per-page information, if needed */
	u_char	*vpage_inter;	/* per-pages to intercept/nointercpet */
	struct hat	*hat;	/* hat used to fault segment in */
	enum fault_type	type;	/* type of fault */
	enum seg_rw	rw;	/* type of access at fault */
	void	*private_data;
	dev_t	dev;		/* Device doing the mapping */
	struct segmapdev_ctx *devctx;
	struct ddi_mapdev_ctl m_ops;  /* Mapdev ops struct */
	u_int	timeout_length; /* Number of clock ticks to keep ctx */
};

/*
 * Flags used by the segment fault handling routines.
 */
#define	SEGMAPDEV_INTER		0x01 /* Driver interested in faults */
#define	SEGMAPDEV_FAULTING	0x02 /* Segment is faulting */

#ifdef _KERNEL

static int segmapdev_create(struct seg *, void *);
static int segmapdev_inter(struct seg *, caddr_t, off_t);
static int segmapdev_nointer(struct seg *, caddr_t, off_t);
static int segmapdev_set_access_attr(struct seg *, caddr_t, off_t, u_int);

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SEG_MAPDEV_H */
