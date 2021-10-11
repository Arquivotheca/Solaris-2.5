#pragma ident	"@(#)putchar.c	1.10	92/07/20 SMI" 
 /* from S5R2 1.2 */

/*LINTLIBRARY*/
/*
 * A subroutine version of the macro putchar
 */
#include <stdio.h>
#undef putchar

int
putchar(c)
register char c;
{
	return(putc(c, stdout));
}
