#pragma ident	"@(#)getc.c	1.3	92/07/20 SMI" 

/*LINTLIBRARY*/
#include <stdio.h>

#undef getc
#define	__getc__(p)		(--(p)->_cnt>=0? ((int)*(p)->_ptr++):_filbuf(p))

int
getc(fp)
register FILE *fp;
{
	return (__getc__(fp));
}
