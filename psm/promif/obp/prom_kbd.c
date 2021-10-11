/*
 * Copyright (c) 1991, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)prom_kbd.c	1.6	94/06/10 SMI"

#include <sys/promif.h>
#include <sys/promimpl.h>

int
prom_stdin_is_keyboard(void)
{
	int i;

	switch (obp_romvec_version)  {
	case SUNMON_ROMVEC_VERSION:
		return (SUNMON_INSOURCE == INKEYB);

	case OBP_V0_ROMVEC_VERSION:
	case OBP_V2_ROMVEC_VERSION:
		return (OBP_V0_INSOURCE == INKEYB);

	default:
		i = prom_getproplen(prom_getphandle(OBP_V2_STDIN), "keyboard");
		return (i == -1 ? 0 : 1);
	}
}
