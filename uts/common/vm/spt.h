/*
 * Copyright (c) 1993 by Sun Microsystems, Inc.
 */

#ifndef	_VM_SPT_H
#define	_VM_SPT_H

#pragma ident	"@(#)spt.h	1.6	94/03/17 SMI"

#include <sys/types.h>
#include <sys/t_lock.h>

#ifdef	__cplusplus
extern "C" {
#endif

struct spt_data {
	struct vnode	*vp;
	struct anon_map	*amp;
	u_int realsize;
};

/*
 * Private data for spt_shm segment.
 */
struct sptshm_data {
	struct as	*sptas;
	struct anon_map *amp;
	int	softlockcnt;
	kmutex_t lock;
};

#ifdef _KERNEL

/*
 * Functions used in shm.c to call ISM.
 */
int	sptcreate(u_int, struct as **, struct anon_map *);
void	sptdestroy(struct as *, struct anon_map *);
int	segspt_shmattach(struct seg *, caddr_t *);

#define	isspt(sp)	((sp)->shm_sptas)
#define	spt_on(a)	(share_page_table || ((a) & SHM_SHARE_MMU))

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _VM_SPT_H */
