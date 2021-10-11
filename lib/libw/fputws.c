/*	Copyright (c) 1986 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*	This module is created for NLS on Sep.03.86		*/

#ident	"@(#)fputws.c	1.10	92/12/16 SMI"	/* from JAE2.0 1.0 */

/*
 * fputws transforms the process code string pointed to by "ptr"
 * into a byte string in EUC, and writes the string to the named
 * output "iop".
 *
 * Use an intermediate buffer to transform a string from wchar_t to 
 * multibyte char.  In order to not overflow the intermediate buffer, 
 * impose a limit on the length of string to output to PC_MAX process 
 * codes.  If the input string exceeds PC_MAX process codes, process
 * the string in a series of smaller buffers.
 */

/* #include "shlib.h" */
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <widec.h>

#define PC_MAX 		256
#define MBBUFLEN	(PC_MAX * MB_LEN_MAX)
#define min(a, b)	((a) < (b) ? (a) : (b))

int
fputws(const wchar_t *ptr, FILE *iop)
{
	int pclen, pccnt, pcsize, ret;
	int nbytes, i;
	char mbbuf[MBBUFLEN], *mp;

	/* number of process codes in ptr */
	pclen = pccnt = wslen(ptr);

	while (pclen > 0) {
		pcsize = min(pclen, PC_MAX - 1);
		nbytes = 0;
		for (i = 0, mp = mbbuf; i < pcsize; i++, mp += ret) {
			if ((ret = _wctomb(mp, *ptr++)) == -1)
				return (EOF);
			nbytes += ret;
		}
		*mp = '\0';
		if (fputs(mbbuf, iop) != nbytes)
			return (EOF);
		pclen -= pcsize;
	}
	return (pccnt);
}
