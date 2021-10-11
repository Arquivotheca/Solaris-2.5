/*
 * Copyright (c) 1990,1992 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)prom_idprom.c	1.7	94/06/10 SMI"

#include <sys/promif.h>
#include <sys/promimpl.h>
#include <sys/idprom.h>

/*
 * Get idprom property from root node, return to callers buffer.
 */

int
prom_getidprom(caddr_t addr, int size)
{
	u_char *cp, val = 0;
	/*LINTED [idprom unused]*/
	idprom_t idprom;
	int i;

	switch (obp_romvec_version)  {

	case SUNMON_ROMVEC_VERSION:
		prom_sunmon_getidprom(addr);
		break;

	default: {
		int length;

		length = prom_getproplen(prom_rootnode(), OBP_IDPROM);
		if (length == -1)  {
			prom_printf("Missing OBP idprom property.\n");
			return (-1);
		}

		if (length > size) {
			prom_printf("Buffer size too small.\n");
			return (-1);
		}

		(void) prom_getprop(prom_rootnode(), OBP_IDPROM,
			(caddr_t) addr);
		}
		break;
	}

	/*
	 * Test the checksum for sanity
	 */
	for (cp = (u_char *)addr, i = 0;
			i < (sizeof (idprom) - sizeof (idprom.id_undef)); i++)
		val ^= *cp++;

	if (val != 0)
		prom_printf("Warning: IDprom checksum error.\n");

	return (0);
}
