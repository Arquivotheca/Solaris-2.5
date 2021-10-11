#pragma ident	"@(#)getchar.c	1.10	92/07/20 SMI"  /* from S5R2 1.2 */

/*LINTLIBRARY*/
/*
 * A subroutine version of the macro getchar.
 */
#include <stdio.h>
#undef getchar

int
getchar()
{
	return(getc(stdin));
}
