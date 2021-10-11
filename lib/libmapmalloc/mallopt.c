/*
 * Copyright (c) 1991, Sun Microsytems, Inc.
 */

#ident	"@(#)mallopt.c	1.1	91/07/19 SMI"

#include <malloc.h>

struct  mallinfo __mallinfo;

/*
 * mallopt -- Do nothing
 */
mallopt(cmd, value)
int cmd, value;
{
	return(0);
}


/*
 * mallinfo -- Do nothing
 */
struct mallinfo
mallinfo()
{
	return(__mallinfo);
}
