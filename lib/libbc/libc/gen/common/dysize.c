#pragma ident	"@(#)dysize.c	1.3	92/07/21 SMI"
	 /* from Arthur Olson's 3.1 */

/*LINTLIBRARY*/

#include <tzfile.h>

dysize(y)
{
	/*
	** The 4.[0123]BSD version of dysize behaves as if the return statement
	** below read
	**	return ((y % 4) == 0) ? DAYS_PER_LYEAR : DAYS_PER_NYEAR;
	** but since we'd rather be right than (strictly) compatible. . .
	*/
	return isleap(y) ? DAYS_PER_LYEAR : DAYS_PER_NYEAR;
}
