#pragma ident	"@(#)index.c	1.3	92/07/21 SMI"
	  /* from UCB 4.1 80/12/21 */

/*
 * Return the ptr in sp at which the character c appears;
 * NULL if not found
 */

#define	NULL	0

char *
index(sp, c)
	register char *sp, c;
{

	do {
		if (*sp == c)
			return (sp);
	} while (*sp++);
	return (NULL);
}
