#pragma ident	"@(#)strchr.c	1.2	92/07/20 SMI"  /* from S5R2 1.2 */

/*LINTLIBRARY*/
/*
 * Return the ptr in sp at which the character c appears;
 * NULL if not found
 */

#define	NULL	0

char *
strchr(sp, c)
register char *sp, c;
{
	do {
		if(*sp == c)
			return(sp);
	} while(*sp++);
	return(NULL);
}
