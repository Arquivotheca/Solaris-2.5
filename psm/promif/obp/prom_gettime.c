/*
 * Copyright (c) 1991, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)prom_gettime.c	1.6	94/06/10 SMI"

#include <sys/promif.h>
#include <sys/promimpl.h>

/*
 * For SunMON PROMs we forge a timer as a simple counter
 * so that (at least) time is increasing ..
 *
 * This prom entry point cannot be used once the kernel is up
 * and running (after stop_mon_clock is called) since the kernel
 * takes over level 14 interrupt handling which the PROM depends
 * on to update the time.
 */
u_int
prom_gettime(void)
{
	static int pretend = 0;

	switch (obp_romvec_version) {

	case SUNMON_ROMVEC_VERSION:
		PROMIF_DPRINTF(("prom_gettime on a SUNMON?\n"));
		return (pretend++);

	default:
		return (OBP_MILLISECONDS);
	}
}
