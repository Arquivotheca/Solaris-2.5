/*
 * Copyright (c) 1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)fiximp_sun4m.c	1.1	95/07/18 SMI"

#include <sys/types.h>

extern u_int	getmcr(void);
extern int	ross_module_identify(u_int);
extern int	ross625_module_identify(u_int);
extern void	ross625_module_ftd();
extern int	icache_flush;

/*ARGSUSED*/
void
fiximp_sun4m(short cputype)
{
	u_int mcr;

	/*
	 * Read the module control register.
	 */
	mcr = getmcr();

	/*
	 * Set the value of icache_flush.  This value is
	 * passed to the kernel linker so that it knows
	 * whether or not to iflush when relocating text.
	 * Because of a bug in the Ross605, the iflush
	 * instruction causes an illegal instruction
	 * trap therefore we don't iflush in that case.
	 */
	if (ross_module_identify(mcr))
		icache_flush = 0;
	else
		icache_flush = 1;

	/*
	 * On modules which allow FLUSH instructions
	 * to cause a T_UNIMP_FLUSH trap, make sure
	 * the trap is disabled since the kernel linker will
	 * be iflush'ing prior to the kernel taking over
	 * the trap table.
	 */
	if (ross625_module_identify(mcr))
		ross625_module_ftd();
}
