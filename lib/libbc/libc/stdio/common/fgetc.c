#pragma ident	"@(#)fgetc.c	1.10	92/07/20 SMI"  /* from S5R2 1.2 */

/*LINTLIBRARY*/
#include <stdio.h>

int
fgetc(fp)
register FILE *fp;
{
	return(getc(fp));
}
