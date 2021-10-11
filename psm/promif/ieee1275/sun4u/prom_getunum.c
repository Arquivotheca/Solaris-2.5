/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)prom_getunum.c	1.4	95/06/01 SMI"

#include <sys/promif.h>
#include <sys/promimpl.h>

/*
 * This service converts the given physical address into a text string,
 * representing the name of the field-replacable part for the given
 * physical address. In other words, it tells the kernel which chip got
 * the (un)correctable ECC error, which info is hopefully relayed to the user!
 */
int
prom_get_unum(int syn_code, u_int physlo, u_int physhi, char *buf,
		u_int buflen, int *ustrlen)
{
	cell_t ci[12];
	int rv;
	ihandle_t imemory = prom_memory_ihandle();

	*ustrlen = -1;
	if ((imemory == (ihandle_t)-1))
		return (-1);

	ci[0] = p1275_ptr2cell("call-method");		/* Service name */
	ci[1] = (cell_t)7;				/* #argument cells */
	ci[2] = (cell_t)4;				/* #result cells */
	ci[3] = p1275_ptr2cell("SUNW,get-unumber");	/* Arg1: Method name */
	ci[4] = p1275_ihandle2cell(imemory);		/* Arg2: mem. ihandle */
	ci[5] = p1275_uint2cell(buflen);		/* Arg3: buflen */
	ci[6] = p1275_ptr2cell(buf);			/* Arg4: buf */
	ci[7] = p1275_uint2cell(physhi);		/* Arg5: physhi */
	ci[8] = p1275_uint2cell(physlo);		/* Arg6: physlo */
	ci[9] = p1275_int2cell(syn_code);		/* Arg7: bit # */

	promif_preprom();
	rv = p1275_cif_handler(&ci);
	promif_postprom();

	if (rv != 0)
		return (rv);
	if (p1275_cell2int(ci[10]) != 0)	/* Res1: catch result */
		return (-1);			/* "SUNW,get-unumber" failed */
	*ustrlen = p1275_cell2uint(ci[11]);	/* Res3: unum str length */
	return (0);
}
