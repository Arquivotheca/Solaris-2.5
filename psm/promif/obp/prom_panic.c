/*
 * Copyright (c) 1991-1993, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)prom_panic.c	1.6	94/06/10 SMI"

#include <sys/promif.h>
#include <sys/promimpl.h>

void
prom_panic(char *s)
{
	if (!s)
		s = "unknown panic";
	prom_printf("panic - %s: %s\n", promif_clntname, s);
	prom_enter_mon();
}
