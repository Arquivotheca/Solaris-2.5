/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)prom_retain.c	1.5	95/08/29 SMI"

#include <sys/promif.h>
#include <sys/promimpl.h>

/*
 * Allocate retained physical memory
 * Returns 0: Success; Non-zero: failure.
 * Returns *phys_hi, *phys_lo only if successful.
 */
int
prom_retain(char *id, u_int size, u_int align,
	u_int *base_hi, u_int *base_lo)
{
	cell_t ci[11];
	int rv;
	ihandle_t imemory = prom_memory_ihandle();

	if ((imemory == (ihandle_t)-1))
		return (-1);

	/*
	 * XXX delete it later
	 */
	if (prom_test_method("SUNW,retain", prom_getphandle(imemory)) != 0) {
#ifdef DEBUG
		prom_printf("SUNW,retain doesn't exist\n");
#endif DEBUG
		*base_hi = 0;
		*base_lo = 0x2000;
		return (prom_claim_phys(size, *base_hi, *base_lo));
	}

	ci[0] = p1275_ptr2cell("call-method");	/* Service name */
	ci[1] = (cell_t) 5;			/* #argument cells */
	ci[2] = (cell_t) 3;			/* #result cells */
	ci[3] = p1275_ptr2cell("SUNW,retain");	/* Arg1: Method name */
	ci[4] = p1275_ihandle2cell(imemory);	/* Arg2: memory ihandle */
	ci[5] = p1275_uint2cell(align);		/* Arg2: SA1: align */
	ci[6] = p1275_uint2cell(size);		/* Arg3: SA2: size */
	ci[7] = p1275_ptr2cell(id);		/* Arg4: SA3: id name */

	promif_preprom();
	rv = p1275_cif_handler(&ci);
	promif_postprom();

	if (rv != 0)
		return (rv);		/* Service "call-method" failed */
	if (ci[8] != 0)			/* Res1: catch-result */
		return (-1);		/* Method "SUNW,retain" failed */

	*base_hi = p1275_cell2uint(ci[9]);	/* Res3: base.hi */
	*base_lo = p1275_cell2uint(ci[10]);	/* Res4: base.lo */
	return (0);
}
