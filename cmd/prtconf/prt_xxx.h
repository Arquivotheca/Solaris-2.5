#ident	"@(#)prt_xxx.h	1.1	91/05/16 SMI"

/*
 * Copyright (c) 1991 Sun Microsystems, Inc.
 */

struct pp_data {
	char *pp_parent;		/* parent name */
	unsigned pp_size;		/* Size of data */
	void (*pp_getdata)();		/* Get extra data function */
	void (*pp_print)();		/* print function */
};
