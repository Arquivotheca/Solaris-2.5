#pragma ident	"@(#)ctime.c	1.3	92/07/21 SMI"
	  /* from Arthur Olson's 3.1 */

/*LINTLIBRARY*/

#include <sys/types.h>
#include <time.h>

char *
ctime(timep)
time_t *	timep;
{
	return asctime(localtime(timep));
}
