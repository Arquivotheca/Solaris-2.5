/*
 * Copyright (c) 1991-1993, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)prom_exit.c	1.6	94/06/10 SMI"

#include <sys/promif.h>
#include <sys/promimpl.h>

/*
 * There should be no return from this function
 */
void
prom_exit_to_mon(void)
{
	promif_preprom();
	switch (obp_romvec_version) {
	case SUNMON_ROMVEC_VERSION:
		(void) prom_montrap(SUNMON_EXIT_TO_MON);
		break;

	default:
		(void) prom_montrap(OBP_EXIT_TO_MON);
		break;
	}
	promif_postprom();
	/* NOTREACHED */
}
