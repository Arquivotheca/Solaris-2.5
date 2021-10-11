/*
 * Copyright (c) 1991-1993, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)prom_wrtestr.c	1.6	94/06/10 SMI"

#include <sys/promif.h>
#include <sys/promimpl.h>

/*
 * Write string to PROM's notion of stdout.
 */
void
prom_writestr(char *buf, u_int bufsize)
{
	u_int written = 0;
	int i;

	promif_preprom();

	switch (obp_romvec_version)  {

	case SUNMON_ROMVEC_VERSION:
		(SUNMON_FWRITESTR)(buf, bufsize, SUNMON_FBADDR);
		break;

	case OBP_V0_ROMVEC_VERSION:
	case OBP_V2_ROMVEC_VERSION:
		(OBP_V0_FWRITESTR)(buf, bufsize);
		break;

	default:
		while (written < bufsize)  {
			if ((i =  OBP_V2_WRITE(OBP_V2_STDOUT, buf,
			    bufsize - written)) == -1)
				continue;
			written += i;
		}
		break;
	}

	promif_postprom();
}
