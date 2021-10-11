/*
 * Copyright (c) 1990 - 1991, 1993 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)module_spitfire.c	1.23	95/08/08 SMI"

#include <sys/machparam.h>
#include <sys/module.h>
#include <sys/cpu.h>
#include <sys/elf_SPARC.h>
#include <vm/hat_sfmmu.h>

/*
 * Support for spitfire modules
 */

extern int use_page_coloring;
extern int do_pg_coloring;
extern int use_virtual_coloring;
extern int do_virtual_coloring;

/*
 * Maximum number of contexts for Spitfire.
 */
#define	MAX_NCTXS	(1 << 13)

int spitfire_mod_mcr = 0;

extern void	spitfire_cache_init();
extern void	spitfire_vac_flush();
extern void	spitfire_cache_flushall_tl1();
extern void	spitfire_dcache_flushpage();
extern void	spitfire_icache_flushpage();
extern void	spitfire_cache_nop();
extern void	spitfire_reenable_caches_tl1();
extern void	spitfire_dcache_flushpage_tl1();

struct module_ops spitfireops = {
	{
		spitfire_cache_nop,	/* or spitfire_cache_debug_init if */
					/* we are debugging/bringup and we */
					/* need to selectively turn caches on */
		spitfire_vac_flush,
		spitfire_dcache_flushpage,
		spitfire_icache_flushpage,
		spitfire_cache_flushall_tl1,
		spitfire_reenable_caches_tl1,
		spitfire_dcache_flushpage_tl1,
		NULL
	},
	{
		sfmmu_tlbflush_page,
		sfmmu_tlbflush_ctx,
		sfmmu_tlbflush_page_tl1,
		sfmmu_tlbflush_ctx_tl1,
		sfmmu_tlbcache_flushpage_tl1,
		sfmmu_itlb_ld,
		sfmmu_dtlb_ld,
		sfmmu_copytte,
		sfmmu_modifytte,
		sfmmu_modifytte_try,
	},
	{
		NULL
	}
};

int /* ARGSUSED */
spitfire_module_identify(u_int mcr)
{
	return (1);
}

void	/*ARGSUSED*/
spitfire_module_setup(mcr)
	register int	mcr;
{
	extern u_int nctxs;
	extern struct module_ops *moduleops;
	extern int at_flags;

	moduleops = &spitfireops;
	spitfire_mod_mcr = mcr;

	cache |= (CACHE_VAC | CACHE_PTAG | CACHE_IOCOHERENT);

	at_flags = EF_SPARC_32PLUS | EF_SPARC_SUN_US1;

	/*
	 * Use the maximum number of contexts available for Spitfire.
	 */
	nctxs = MAX_NCTXS;

	if (use_page_coloring) {
		do_pg_coloring = 1;
		if (use_virtual_coloring)
			do_virtual_coloring = 1;
	}

}
