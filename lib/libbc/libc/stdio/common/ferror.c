#pragma ident	"@(#)ferror.c	1.3	92/07/20 SMI" 

/*LINTLIBRARY*/
#include <stdio.h>

#undef ferror
#define	__ferror__(p)	(((p)->_flag&_IOERR)!=0)

int
ferror(fp)
register FILE *fp;
{
	return (__ferror__(fp));
}
