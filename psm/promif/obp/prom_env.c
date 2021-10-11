/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */

#pragma	ident	"@(#)prom_env.c	1.2	94/06/10 SMI"

#include <sys/promif.h>
#include <sys/promimpl.h>

/*
 * These routines are called immediately before,
 * and immediately after entering the PROM
 */
void (*promif_preprom)(void);
void (*promif_postprom)(void);

/*
 * And these two routines are functional interfaces
 * used to set the above (promif-private) globals
 */
void (*
prom_set_preprom(void (*preprom)(void)))(void)
{
	void (*fn)(void);

	fn = promif_preprom;
	promif_preprom = preprom;
	return (fn);
}

void (*
prom_set_postprom(void (*postprom)(void)))(void)
{
	void (*fn)(void);

	fn = promif_postprom;
	promif_postprom = postprom;
	return (fn);
}
