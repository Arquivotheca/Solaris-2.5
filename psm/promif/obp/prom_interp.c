/*
 * Copyright (c) 1991-1993, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)prom_interp.c	1.9	94/06/10 SMI"

#include <sys/promif.h>
#include <sys/promimpl.h>

void
prom_interpret(char *string, int arg1, int arg2, int arg3, int arg4, int arg5)
{
	switch (obp_romvec_version) {
	case SUNMON_ROMVEC_VERSION:
		PROMIF_DPRINTF(("prom_interpret: no interpreter!\n"));
		break;

	case OBP_V0_ROMVEC_VERSION:
		promif_preprom();
		OBP_INTERPRET(prom_strlen(string), string, arg1, arg2, arg3,
		    arg4);
		promif_postprom();
		break;

	default:
	case OBP_V2_ROMVEC_VERSION:
	case OBP_V3_ROMVEC_VERSION:
		promif_preprom();
		OBP_INTERPRET(string, arg1, arg2, arg3, arg4, arg5);
		promif_postprom();
		break;
	}
}
