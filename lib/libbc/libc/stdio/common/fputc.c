#pragma ident	"@(#)fputc.c	1.10	92/07/20 SMI"  /* from S5R2 1.2 */

/*LINTLIBRARY*/
#include <stdio.h>

int
fputc(c, fp)
int	c;
register FILE *fp;
{
	return(putc(c, fp));
}
