#pragma ident	"@(#)strrchr.c	1.2	92/07/20 SMI"  /* from S5R2 1.2 */

/*LINTLIBRARY*/
/*
 * Return the ptr in sp at which the character c last
 * appears; NULL if not found
*/

#define NULL 0

char *
strrchr(sp, c)
register char *sp, c;
{
	register char *r;

	r = NULL;
	do {
		if(*sp == c)
			r = sp;
	} while(*sp++);
	return(r);
}
