/*
 * Copyright (c) 1991, Sun Microsytems, Inc.
 */

#ident	"@(#)calloc.c	1.1	91/07/19 SMI"	/* SunOS 1.10 */

/*
 * calloc - allocate and clear memory block
 */
#define NULL 0

void *
calloc(num, size)
	unsigned num, size;
{
	register void *mp;
	void	*malloc();

	num *= size;
	mp = malloc(num);
	if (mp == NULL)
		return(NULL);
	memset(mp, 0, num);
	return (mp);
}

cfree(p, num, size)
	void *p;
	unsigned num, size;
{
	free(p);
}

