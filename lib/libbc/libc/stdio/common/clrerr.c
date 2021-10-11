#pragma ident	"@(#)clrerr.c	1.10	92/07/20 SMI"  /* from S5R2 1.3 */

/*LINTLIBRARY*/
#include <stdio.h>
#undef clearerr

void
clearerr(iop)
register FILE *iop;
{
	iop->_flag &= ~(_IOERR | _IOEOF);
}
