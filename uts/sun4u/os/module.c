/*
 * Copyright (c) 1990 - 1991, 1993 by Sun Microsystems, Inc.
 */

#pragma	ident	"@(#)module.c	1.14	95/08/04 SMI"

#include <sys/debug.h>
#include <sys/machparam.h>
#include <sys/module.h>
#include <sys/types.h>
#include <sys/cpu.h>
#include <sys/promif.h>
#include <sys/prom_debug.h>
#include <sys/param.h>

/*
 * Generic pointer to specific routine related to modules.
 */
struct module_ops *moduleops;

/*
 * module_setup() is called from locore.s, very early. For each
 * known module type it will call the xx_module_identify() routine.
 * The xx_module_setup() routine is then called for the first module
 * that is identified. The details of module identification is left
 * to the module specific code. Typical this is just based on
 * decoding the IMPL, VERS field of the MCR. Other schemes may be
 * necessary for some module module drivers that may support multiple
 * implementations.
 *
 * module_conf.c contains the only link between module independent
 * and module dependent code. The file can be distributed in source
 * form to make porting to new modules a lot easier.
 * -- think about "module drivers".
 *
 */

void
module_setup(int mcr)
{
	int	i = module_info_size;
	struct module_linkage *p = module_info;

	while (i-- > 0) {
		if ((*p->identify_func)(mcr)) {
			(*p->setup_func)(mcr);
			return;
		}
		++p;
	}
	prom_printf("Unsupported module IMPL=%d VERS=%d\n\n",
		(mcr >> 28), ((mcr >> 24) & 0xf));
	prom_exit_to_mon();
	/*NOTREACHED*/
}

/*
 * The following routines provide an interface to the #define	macros
 * to assembly routines and maintain the old interface to callers in
 * common code.
 * XXX Why does common code need to call these primitives?
 * XXX They should move to a cache only file.
 */

/*
 * XXX Do we need this routine?  Change name to cpu_cache_init
 */
void
cache_init(void)
{
	ASSERT(moduleops->cpuops.cache_init);
	(*moduleops->cpuops.cache_init) ();
}

/*
 * XXX Can we split this routine into icache vs dcache?  Change name to start
 * with cpu and be more descriptive.
 */
void
vac_flush(caddr_t vaddr, int sz)
{
/* #ifdef FIXME */
	ASSERT(moduleops->cpuops.vac_flush);
	(*moduleops->cpuops.vac_flush) (vaddr, sz);
}

/*
 * This routine checks for any pending fp exceptions, and is done on a
 * per-module basis because there are *no* fp exceptions with Spitfire
 * because the floating point instructions are all precise.
 */
void
syncfpu()
{
	if (moduleops->cpuops.syncfpu == NULL) {
		return;
	} else {
		(*moduleops->cpuops.syncfpu) ();
	}
}
