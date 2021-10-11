/*
 * Copyright (c) 1991-1993, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)prom_prop.c	1.6	94/10/13 SMI"

/*
 * Stuff for mucking about with properties
 */

#include <sys/promif.h>
#include <sys/promimpl.h>

int
prom_getproplen(dnode_t nodeid, caddr_t name)
{
	int len;

	switch (obp_romvec_version) {

	case SUNMON_ROMVEC_VERSION:
		return (-1);

	default:
		promif_preprom();
		len = OBP_DEVR_GETPROPLEN(nodeid, name);
		promif_postprom();
		return (len);
	}
}


int
prom_getprop(dnode_t nodeid, caddr_t name, caddr_t value)
{
	int len;

	switch (obp_romvec_version) {

	case SUNMON_ROMVEC_VERSION:
		return (-1);

	default:
		promif_preprom();
		len = OBP_DEVR_GETPROP(nodeid, name, value);
		promif_postprom();
		return (len);
	}
}



/*ARGSUSED*/
caddr_t
prom_nextprop(dnode_t nodeid, caddr_t previous, caddr_t next)
{
	caddr_t prop;

	switch (obp_romvec_version) {

	case SUNMON_ROMVEC_VERSION:
		return ((caddr_t)0);

	default:
		promif_preprom();
		prop = OBP_DEVR_NEXTPROP(nodeid, previous);
		promif_postprom();
		return (prop);
	}
}


int
prom_setprop(dnode_t nodeid, caddr_t name, caddr_t value, int len)
{
	int i;

	switch (obp_romvec_version) {

	case SUNMON_ROMVEC_VERSION:
		return (-1);

	default:
		promif_preprom();
		i = OBP_DEVR_SETPROP(nodeid, name, value, len);
		promif_postprom();
		return (i);
	}
}
