/*
 * Copyright (c) 1991-1993, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)prom_map.c	1.8	94/06/10 SMI"

#include <sys/promif.h>
#include <sys/promimpl.h>

caddr_t
prom_map(caddr_t virthint, u_int space, u_int phys, u_int size)
{
	caddr_t addr;

	switch (obp_romvec_version)  {
	case SUNMON_ROMVEC_VERSION:
	case OBP_V0_ROMVEC_VERSION:
		return ((char *)0);

	default:
		promif_preprom();
		addr = OBP_V2_MAP(virthint, space, phys, size);
		promif_postprom();
		return (addr);
	}
}

void
prom_unmap(caddr_t virthint, u_int size)
{
	switch (obp_romvec_version)  {
	case SUNMON_ROMVEC_VERSION:
	case OBP_V0_ROMVEC_VERSION:
		return;

	default:
		OBP_V2_UNMAP(virthint, size);
		return;
	}
}

/*
 * For sun{4,4c,4e} (sunm MMU) only!
 */
void
prom_setcxsegmap(int c, caddr_t v, int seg)
{
	promif_preprom();

	switch (obp_romvec_version) {

	case SUNMON_ROMVEC_VERSION:
		SUNMON_SETCXSEGMAP(c, v, seg);
		break;

	default:
		OBP_SETCXSEGMAP(c, v, seg);
		break;
	}

	promif_postprom();
}
