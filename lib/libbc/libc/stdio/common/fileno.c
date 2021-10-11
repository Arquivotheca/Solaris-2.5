#pragma ident	"@(#)fileno.c	1.3	92/07/20 SMI" 

/*LINTLIBRARY*/
#include <stdio.h>

#undef fileno
#define	__fileno__(p)	((p)->_file)

int
fileno(fp)
register FILE *fp;
{
	return (__fileno__(fp));
}
