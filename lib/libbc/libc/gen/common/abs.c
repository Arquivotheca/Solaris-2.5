#pragma ident	"@(#)abs.c	1.2	92/07/20 SMI"  /* from S5R2 1.2 */

/*LINTLIBRARY*/

int
abs(arg)
register int arg;
{
	return (arg >= 0 ? arg : -arg);
}
