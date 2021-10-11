/*
 * Copyright (c) 1994 Sun Microsystems, Inc.
 */

#ident	"@(#)netops.c	1.12	94/11/21 SMI"

#include <sys/promif.h>
#include <sys/cpu.h>

extern void v0_silence_nets();

/*
 * For V0 PROMs, we need to call a prom-specific routine to quiesce
 * the network devices. This isn't needed for post-V0 PROMs, which
 * do a complete reset.
 */
void
silence_nets()
{
	char 	buf[OBP_MAXPATHLEN];

	switch (prom_getversion()) {
	case OBP_V0_ROMVEC_VERSION:
		v0_silence_nets();
	default:
		break;
	}
}
