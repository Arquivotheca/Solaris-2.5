/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)prom_macaddr.c	1.11	95/02/14 SMI"

/*
 * Return our machine address in the single argument.
 * XXX: Stop looking for idprom property in sun4u.
 */

#include <sys/promif.h>
#include <sys/promimpl.h>
#include <sys/idprom.h>

/*ARGSUSED*/
int
prom_getmacaddr(ihandle_t hd, caddr_t ea)
{
	idprom_t idprom;

	/*
	 * Extract it from the root node idprom property
	 */
	if (prom_getidprom((caddr_t) &idprom, sizeof (idprom)) == 0) {
		register char *f = (char *) idprom.id_ether;
		register char *t = ea;
		int i;

		for (i = 0; i < sizeof (idprom.id_ether); ++i)
			*t++ = *f++;

		return (0);
	} else
		return (-1); /* our world must be starting to explode */
}
