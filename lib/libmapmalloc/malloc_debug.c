/*
 * Copyright (c) 1991, Sun Microsytems, Inc.
 */

#ident	"@(#)malloc_debug.c	1.1	91/07/19 SMI"

/*
 * malloc_debug(level) - empty routine
 */

int
malloc_debug(level)
        int level;
{
	return(1);
}


/*
 * malloc_verify() - empty routine
 */

int
malloc_verify()
{
	return(1);
}


/*
 * mallocmap() - empty routine
 */

void
mallocmap()
{
	;
}

