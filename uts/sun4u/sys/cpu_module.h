
/*
 * Copyright (c) 1987-1995 by Sun Microsystems, Inc.
 */

#ifndef _SYS_CPU_MODULE_H
#define	_SYS_CPU_MODULE_H

#pragma ident	"@(#)cpu_module.h	1.1	95/03/09 SMI"

#ifdef	__cplusplus
extern "C" {
#endif


#ifdef _KERNEL

void	cache_init(void);			/* XXX required? */
						/* change to cpu_cache_init */
void	vac_flush(caddr_t, int);		/* XXX required? */
						/* change to cpu_vac_flush */
void	syncfpu(void);				/* XXX required? */
						/* change to cpu_syncfpu */

#define	CPU_CACHE_INIT()						\
	ASSERT(moduleops->cpuops.cache_init);				\
	((*moduleops->cpuops.cache_init) ())

#define	CPU_VAC_FLUSH(vaddr, sz)					\
	ASSERT(moduleops->cpuops.vac_flush);				\
	((*moduleops->cpuops.vac_flush) (vaddr, sz))

#define	CPU_ICACHE_FLUSHPAGE(vaddr)					\
	ASSERT(moduleops->cpuops.icache_flushpage);			\
	((*moduleops->cpuops.icache_flushpage) (vaddr))

#define	CPU_DCACHE_FLUSHPAGE(pfnum, vcolor)				\
	ASSERT(moduleops->cpuops.dcache_flushpage);			\
	((*moduleops->cpuops.dcache_flushpage) (pfnum, vcolor))

#define	CPU_REENABLE_CACHES()						\
	ASSERT(moduleops->cpuops.reenable_caches);			\
	((*moduleops->cpuops.reenable_caches) ())

#define	CPU_DCACHE_FLUSHPAGE_MP(pfnum, vcolor, cpuset)			\
	xt_some(cpuset, (uint)moduleops->cpuops.dcache_flushpage_tl1,	\
		pfnum, vcolor, 0, 0)
/*
 * This routine flushes all writeback caches to guarantee that memory
 * has most recent copy of this cpu's data.
 */
#define	CPU_CACHE_FLUSHALL()						\
	ASSERT(moduleops->cpuops.cache_flushall);			\
	((*moduleops->cpuops.cache_flushall) ())

#endif /* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_CPU_MODULE_H */
