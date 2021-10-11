/*
 * Copyright (c) 1991, by Sun Microsystems, Inc.
 */

#ifndef	_SYS_KSTR_H
#define	_SYS_KSTR_H

#pragma ident	"@(#)kstr.h	1.4	95/02/24 SMI"

#include <sys/stream.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Autopush operation numbers.
 */
#define	SET_AUTOPUSH	0
#define	GET_AUTOPUSH	1
#define	CLR_AUTOPUSH	2

extern int	kstr_open(dev_t, dev_t, vnode_t **, int *);
extern int	kstr_plink(vnode_t *, int, int *);
extern int	kstr_unplink(vnode_t *, int);
extern int	kstr_push(vnode_t *, char *);
extern int	kstr_pop(vnode_t *);
extern int	kstr_close(vnode_t *, int);
extern int	kstr_ioctl(vnode_t *, int, int);
extern int	kstr_msg(vnode_t *, mblk_t *, mblk_t **, timestruc_t *);
extern int	kstr_autopush(int, dev_t *, dev_t *, dev_t *, char *[]);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_KSTR_H */
