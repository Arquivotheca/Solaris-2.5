/*
 * Copyright (c) 1987-1993 by Sun Microsystems, Inc.
 */

#ifndef _SYS_MODULE_H
#define	_SYS_MODULE_H

#pragma ident	"@(#)module.h	1.20	95/08/04 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#if defined(_KERNEL) && !defined(_ASM)
#include <sys/types.h>
#include <sys/pte.h>

/*
 * for all module type IDs,
 * the low 8 bits is the same
 * as the value read from
 * the module control register.
 * the remainder reflects
 * additional supported modules.
 */

#define	SPITFIRE	0x0000		/* Spitfire module */

/*
 * We can dynamically add or remove support for
 * modules of various sorts by adding them
 * to, or removing them from, this table.
 *
 * The semantics are: VERY VERY early in the
 * execution of the kernel the identify_func
 * are called in sequence. The first that
 * returns non-zero identifies the current
 * module and the specified setup_func is called.
 */

struct module_linkage {
	int 	(*identify_func)();
	void 	(*setup_func)();
};

/*
 * So we can see the "module_info" table
 * where we need it. The table itsself
 * is allocated and filled in the file
 * module_conf.c
 * which is available in binary configurations
 * so "module drivers" may be added.
 */
extern struct module_linkage    module_info[];
extern int module_info_size;

/*
 * The following structures define sets of operators for
 * handling low levels primitives.  It is expected that subsequent
 * V9 implementations will only require a new set of operators.
 *
 * The cpu operators control all functions relevant to the cpu module.  For the
 * 	most part it consists of cache primitives.
 *
 * The mmu operators control all functions relevant to the mmu.  For the most
 *	part it consists of tlb primitives.
 *
 * The sys operators control all functions that deal with the system as a whole
 *	and not just the cpu chip.  For example, memory error handling, etc.
 *
 * All operators point to the spitfire/electron operators by default.
 *
 * XXX Add more or change as required.
 */

struct cpu_ops {
	void		(*cache_init) ();		/* XXX- needed? */
	void		(*vac_flush) (caddr_t, int);	/* XXX- needed? */
	void		(*dcache_flushpage) (int, int);
	void		(*icache_flushpage) (caddr_t);
	void		(*cache_flushall_tl1) ();
	void		(*reenable_caches_tl1) ();
	void		(*dcache_flushpage_tl1) (int, int);
	void		(*syncfpu) ();
};

struct mmu_ops {
	void		(*tlbflush_page) (caddr_t, int);
	void		(*tlbflush_ctx) (int);
	void		(*tlbflush_page_tl1) (caddr_t, int);
	void		(*tlbflush_ctx_tl1) (int);
	void		(*tlbcacheflush_page_tl1) (caddr_t, int, uint);
	void		(*itlb_ld) (caddr_t, int, tte_t *);
	void		(*dtlb_ld) (caddr_t, int, tte_t *);
	void		(*copytte) (tte_t *, tte_t *);
	int		(*modifytte) (tte_t *, tte_t *, tte_t *);
	int		(*modifytte_try) (tte_t *, tte_t *, tte_t *);
};

struct sys_ops {
	void		(*addentrieshere) ();
};

struct module_ops {
	struct cpu_ops cpuops;
	struct mmu_ops mmuops;
	struct sys_ops sysops;
};

#endif /* _KERNEL &&  !_ASM */

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_MODULE_H */
