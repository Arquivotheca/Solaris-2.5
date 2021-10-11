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

#ifndef	_VM_PVN_H
#define	_VM_PVN_H

#pragma ident	"@(#)pvn.h	1.39	93/05/30 SMI"
/*	From:	SVr4.0	"kernel:vm/pvn.h	1.4"	*/

#include <sys/buf.h>
#include <vm/seg.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef	_KERNEL

/*
 * VM - paged vnode.
 *
 * The VM system manages memory as a cache of paged vnodes.
 * This file desribes the interfaces to common subroutines
 * used to help implement the VM/file system routines.
 */

struct page	*pvn_read_kluster(struct vnode *, u_int, struct seg *,
		    caddr_t, u_int *, u_int *, u_int, u_int, int);
struct page	*pvn_write_kluster(struct vnode *, struct page *,
		    u_int *, u_int *, u_int, u_int, int);
void		pvn_read_done(struct page *, int);
void		pvn_write_done(struct page *, int);
void		pvn_io_done(struct page *);
int		pvn_vplist_dirty(struct vnode *, u_int, int (*)(),
		    int, struct cred *);
int		pvn_getdirty(struct page *, int);
void		pvn_vpzero(struct vnode *, u_int, u_int);
int		pvn_getpages(int (*)(), struct vnode *, u_int, u_int,
		    u_int *, struct page **, u_int, struct seg *,
		    caddr_t, enum seg_rw, struct cred *);
void		pvn_plist_init(struct page *, struct page **, u_int,
		    u_int, u_int, enum seg_rw);

/*
 * When requesting pages from the getpage routines, pvn_getpages will
 * allocate space to return PVN_GETPAGE_NUM pages which map PVN_GETPAGE_SZ
 * worth of bytes.  These numbers are chosen to be the minimum of the max's
 * given in terms of bytes and pages.
 */
#define	PVN_MAX_GETPAGE_SZ	0x10000		/* getpage size limit */
#define	PVN_MAX_GETPAGE_NUM	0x8		/* getpage page limit */

#if PVN_MAX_GETPAGE_SZ > PVN_MAX_GETPAGE_NUM * PAGESIZE

#define	PVN_GETPAGE_SZ	ptob(PVN_MAX_GETPAGE_NUM)
#define	PVN_GETPAGE_NUM	PVN_MAX_GETPAGE_NUM

#else

#define	PVN_GETPAGE_SZ	PVN_MAX_GETPAGE_SZ
#define	PVN_GETPAGE_NUM	btop(PVN_MAX_GETPAGE_SZ)

#endif

#endif	_KERNEL

#ifdef	__cplusplus
}
#endif

#endif	/* _VM_PVN_H */
