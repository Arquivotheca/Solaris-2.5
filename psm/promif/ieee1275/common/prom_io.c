/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)prom_io.c	1.13	95/09/15 SMI"

#include <sys/promif.h>
#include <sys/promimpl.h>

/*
 *  Returns 0 on error. Otherwise returns a handle.
 */
int
prom_open(char *path)
{
	cell_t ci[5];

	ci[0] = p1275_ptr2cell("open");		/* Service name */
	ci[1] = (cell_t)1;			/* #argument cells */
	ci[2] = (cell_t)1;			/* #result cells */
	ci[3] = p1275_ptr2cell(path);		/* Arg1: Pathname */
	ci[4] = (cell_t)0;			/* Res1: Prime result */

	promif_preprom();
	(void) p1275_cif_handler(&ci);
	promif_postprom();

	return (p1275_cell2int(ci[4]));		/* Res1: ihandle */
}


int
prom_seek(int fd, int high, int low)
{
	cell_t ci[7];

	ci[0] = p1275_ptr2cell("seek");		/* Service name */
	ci[1] = (cell_t)3;			/* #argument cells */
	ci[2] = (cell_t)1;			/* #result cells */
	ci[3] = p1275_uint2cell((u_int) fd);	/* Arg1: ihandle */
	ci[4] = p1275_uint2cell(high);		/* Arg2: pos.hi */
	ci[5] = p1275_uint2cell(low);		/* Arg3: pos.lo */
	ci[6] = (cell_t)-1;			/* Res1: Prime result */

	promif_preprom();
	(void) p1275_cif_handler(&ci);
	promif_postprom();

	return (p1275_cell2int(ci[6]));		/* Res1: actual */
}

/*ARGSUSED3*/
int
prom_read(ihandle_t fd, caddr_t buf, u_int len, u_int startblk, char devtype)
{
	cell_t ci[7];

	ci[0] = p1275_ptr2cell("read");		/* Service name */
	ci[1] = (cell_t)3;			/* #argument cells */
	ci[2] = (cell_t)1;			/* #result cells */
	ci[3] = p1275_uint2cell((u_int) fd);	/* Arg1: ihandle */
	ci[4] = p1275_ptr2cell(buf);		/* Arg2: buffer address */
	ci[5] = p1275_uint2cell(len);		/* Arg3: buffer length */
	ci[6] = (cell_t)-1;			/* Res1: Prime result */

	promif_preprom();
	(void) p1275_cif_handler(&ci);
	promif_postprom();

	return (p1275_cell2int(ci[6]));		/* Res1: actual length */
}

/*ARGSUSED3*/
int
prom_write(ihandle_t fd, caddr_t buf, u_int len, u_int startblk, char devtype)
{
	cell_t ci[7];

	ci[0] = p1275_ptr2cell("write");	/* Service name */
	ci[1] = (cell_t)3;			/* #argument cells */
	ci[2] = (cell_t)1;			/* #result cells */
	ci[3] = p1275_uint2cell((u_int)fd);	/* Arg1: ihandle */
	ci[4] = p1275_ptr2cell(buf);		/* Arg2: buffer address */
	ci[5] = p1275_uint2cell(len);		/* Arg3: buffer length */
	ci[6] = (cell_t)-1;			/* Res1: Prime result */

	promif_preprom();
	(void) p1275_cif_handler(&ci);
	promif_postprom();

	return (p1275_cell2int(ci[6]));		/* Res1: actual length */
}

int
prom_close(int fd)
{
	cell_t ci[4];

	ci[0] = p1275_ptr2cell("close");	/* Service name */
	ci[1] = (cell_t)1;			/* #argument cells */
	ci[2] = (cell_t)0;			/* #result cells */
	ci[3] = p1275_uint2cell((u_int)fd);	/* Arg1: ihandle */

	promif_preprom();
	(void) p1275_cif_handler(&ci);
	promif_postprom();

	return (0);
}
