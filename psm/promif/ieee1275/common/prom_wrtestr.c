/*
 * Copyright (c) 1991-1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)prom_wrtestr.c	1.8	94/11/12 SMI"

#include <sys/promif.h>
#include <sys/promimpl.h>

/*
 * Write string to PROM's notion of stdout.
 */
void
prom_writestr(char *buf, u_int len)
{
	u_int written = 0;
	ihandle_t istdin;
	int i;

	istdin = prom_stdout_ihandle();
	while (written < len)  {
		if ((i = prom_write(istdin, buf, len - written, 0, BYTE)) == -1)
			continue;
		written += i;
	}
}
