/*
 * Copyright (c) 1991-1993, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)prom_reboot.c	1.6	94/06/10 SMI"

#include <sys/promif.h>
#include <sys/promimpl.h>

/*
 * There should be no return from this function
 */
void
prom_reboot(char *bootstr)
{
	promif_preprom();
	switch (obp_romvec_version) {
	case SUNMON_ROMVEC_VERSION:
		SUNMON_BOOT(bootstr);
		break;

	default:
		OBP_BOOT(bootstr);
		break;
	}
	promif_postprom();
	/* NOTREACHED */
}
