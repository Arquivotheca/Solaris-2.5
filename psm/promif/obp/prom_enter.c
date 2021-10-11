/*
 * Copyright (c) 1991-1993, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)prom_enter.c	1.8	94/06/10 SMI"

#include <sys/promif.h>
#include <sys/promimpl.h>

void
prom_enter_mon(void)
{
	switch (obp_romvec_version) {

	case SUNMON_ROMVEC_VERSION:
		promif_preprom();
		(*SUNMON_ABORTENT)();
		promif_postprom();
		break;

	default:
		promif_preprom();
		(*OBP_ENTER_MON)();
		promif_postprom();
		break;
	}
}
