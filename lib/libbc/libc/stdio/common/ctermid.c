#pragma ident	"@(#)ctermid.c	1.6	92/07/20 SMI"  /* from S5R2 1.3 */

/*LINTLIBRARY*/
#include <stdio.h>

extern char *strcpy();
static char res[L_ctermid];

char *
ctermid(s)
register char *s;
{
	return (strcpy(s != NULL ? s : res, "/dev/tty"));
}
