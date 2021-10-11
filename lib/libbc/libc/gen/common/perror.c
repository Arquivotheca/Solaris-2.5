#pragma ident	"@(#)perror.c	1.2	92/07/20 SMI" 

/*
 * Print the error indicated
 * in the cerror cell.
 */
#include <stdio.h>

extern	int fflush();
extern	void _perror();

void
perror(s)
	char *s;
{

	(void)fflush(stderr);
	_perror(s);
}
