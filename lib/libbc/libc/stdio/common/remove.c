#pragma ident	"@(#)remove.c	1.3	92/07/20 SMI" 

/*LINTLIBRARY*/
#include <stdio.h>

#undef remove

int
remove(fname)
register char *fname;
{
	return (unlink(fname));
}
