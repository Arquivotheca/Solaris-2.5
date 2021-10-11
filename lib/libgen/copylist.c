/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)copylist.c	1.9	93/11/10 SMI"	/* SVr4.0 1.1.3.2	*/

/*
	copylist copies a file into a block of memory, replacing newlines
	with null characters, and returns a pointer to the copy.
*/

#pragma weak copylist = _copylist

#include "synonyms.h"
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>

char *
copylist(filenm, szptr)
#ifdef __STDC__
const char	*filenm;
#else
char	*filenm;
#endif
off_t	*szptr;
{
	FILE		*strm;
	struct	stat	stbuf;
	register int	c;
	register char	*ptr, *p;

	/* get size of file */
	if (stat(filenm, &stbuf) == -1) {
		return (NULL);
	}
	*szptr = stbuf.st_size;

	/* get block of memory */
	if ((ptr = malloc((unsigned) *szptr)) == NULL) {
		return (NULL);
	}

	/* copy contents of file into memory block, replacing newlines */
	/* with null characters */
	if ((strm = fopen(filenm, "r")) == NULL) {
		return (NULL);
	}
	for (p = ptr; p < ptr + *szptr && (c = getc(strm)) != EOF; p++) {
		if (c == '\n')
			*p = '\0';
		else
			*p = c;
	}
	(void) fclose(strm);

	return (ptr);
}
