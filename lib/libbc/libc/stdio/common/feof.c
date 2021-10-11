#pragma ident	"@(#)feof.c	1.3	92/07/20 SMI" 


/*LINTLIBRARY*/
#include <stdio.h>

#undef feof
#define	__feof__(p)		(((p)->_flag&_IOEOF)!=0)

int
feof(fp)
register FILE *fp;
{
	return (__feof__(fp));
}
