/*
 * Copyright (c) 1991-1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)prom_panic.c	1.7	94/11/12 SMI"

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
