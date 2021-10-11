/*
 * Copyright (c) 1991, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)prom_fb.c	1.8	94/06/10 SMI"

#include <sys/promif.h>
#include <sys/promimpl.h>

int
prom_stdout_is_framebuffer(void)
{
	switch (obp_romvec_version) {

	case SUNMON_ROMVEC_VERSION:
		return (SUNMON_OUTSINK == OUTSCREEN);

	case OBP_V0_ROMVEC_VERSION:
	case OBP_V2_ROMVEC_VERSION:
		return (OBP_V0_OUTSINK == OUTSCREEN);

	default:
		return (prom_devicetype((dnode_t) prom_getphandle(
		    OBP_V2_STDOUT), OBP_DISPLAY));
	}
}

char *
prom_fbtype(void)
{
	switch (obp_romvec_version) {

	case SUNMON_ROMVEC_VERSION:
		return ((char *) SUNMON_FBTYPE);

	default:
		return ((char *)0);
	}
}
