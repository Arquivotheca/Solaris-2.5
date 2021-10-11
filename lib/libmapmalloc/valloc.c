/*
 * Copyright (c) 1991, Sun Microsytems, Inc.
 */

#ident	"@(#)valloc.c	1.1	91/07/19 SMI"

#include <stdlib.h>
#include <errno.h>

/*
 * valloc(size) - do nothing
 */

void *
valloc(size)
	size_t size;
{
	return (0);
}


/*
 * memalign(align,nbytes) - do nothing
 */

void *
memalign(align, nbytes)
	size_t	align;
	size_t	nbytes;
{
	errno = EINVAL;
	return(NULL);
}
