/*
 * Copyright (c) 1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)setbuffer.c	1.1	95/02/28 SMI"

/*
 * Compatibility wrappers for setbuffer and setlinebuf
 */

/*LINTLIBRARY*/
#include "synonyms.h"

#include <stdio.h>
#include <stdlib.h>

/*
 * Associate a buffer with an "unused" stream.
 * If the buffer is NULL, then make the stream completely unbuffered.
 */
void
setbuffer(FILE *iop, char *abuf, size_t asize)
{
	if (abuf == NULL)
		setvbuf(iop, NULL, _IONBF, 0);
	else
		setvbuf(iop, abuf, _IOFBF, asize);
}

/*
 * Convert a block buffered or line buffered stream to be line buffered
 * Allowed while the stream is still active; relies on the implementation
 * not the interface!
 */
void
setlinebuf(FILE *iop)
{
	(void) fflush(iop);
	setvbuf(iop, NULL, _IOLBF, 128);
}
