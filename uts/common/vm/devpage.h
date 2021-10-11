/*
 * Copyright (c) 1988, 1990, 1991 by Sun Microsystems, Inc.
 */

#ifndef	_VM_DEVPAGE_H
#define	_VM_DEVPAGE_H

#pragma ident	"@(#)devpage.h	1.13	94/02/16 SMI"

/*
 * Devpage - Device pages for mmap'able devices.
 *
 * Each active device page has a page structure, which is used to maintain
 * the mappings for the page.  A page can be found via a hashed lookup
 * based on the [vp, offset].  If a page has an [vp, offset] identity,
 * then it is entered on a doubly linked circular list off the
 * vnode using the vpnext/vpprev pointers.
 *
 * A devpage is the same structure as a page; we just cheat.
 * Also, there is no free list, and a lot of the bits are meaningless.
 */

#ifdef	__cplusplus
extern "C" {
#endif

#define	devpage	page

typedef struct devpage devpage_t;

#ifdef _KERNEL

struct vnode;
extern devpage_t	*devpage_find(struct vnode *vp, u_int off);
extern devpage_t	*devpage_lookup(struct vnode *vp, u_int off, int excl);
extern devpage_t	*devpage_create(struct vnode *vp, u_int off);
extern void		devpage_destroy(devpage_t *dp);

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _VM_DEVPAGE_H */
