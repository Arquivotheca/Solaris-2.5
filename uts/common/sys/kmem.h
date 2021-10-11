/*
 * Copyright (c) 1993 by Sun Microsystems, Inc.
 */

#ifndef _SYS_KMEM_H
#define	_SYS_KMEM_H

#pragma ident	"@(#)kmem.h	1.16	95/08/08 SMI"

#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * kernel memory allocator: public interfaces
 */

#ifdef _KERNEL

extern int kmem_ready;
extern int kmem_reapahead;

/*
 * Flags for kmem_*alloc()
 */
#define	KM_SLEEP	0	/* can block for memory; success guaranteed */
#define	KM_NOSLEEP	1	/* cannot block for memory; may fail */

struct kmem_cache;		/* cache structure is opaque to kmem clients */

extern void kmem_init(void);
extern void kmem_reap(void);
extern void kmem_async_thread(void);
extern u_long kmem_avail(void);
extern u_longlong_t kmem_maxavail(void);
extern u_long kmem_maxvirt(void);

extern void *kmem_perm_alloc(size_t, int, int);

extern void *kmem_alloc(size_t, int);
extern void *kmem_zalloc(size_t, int);
extern void kmem_free(void *, size_t);

extern void *kmem_fast_alloc(char **, size_t, int, int);
extern void *kmem_fast_zalloc(char **, size_t, int, int);
extern void kmem_fast_free(char **, void *);

extern struct kmem_cache *kmem_cache_create(char *, size_t, int,
	void (*)(void *, size_t), void (*)(void *, size_t), void (*)(void));
extern void kmem_cache_destroy(struct kmem_cache *);
extern void *kmem_cache_alloc(struct kmem_cache *, int);
extern void kmem_cache_free(struct kmem_cache *, void *);

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_KMEM_H */
