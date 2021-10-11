#pragma ident	"@(#)rindex.c	1.3	92/07/21 SMI"
	  /* from UCB 4.1 80/12/21 */

/*
 * Return the ptr in sp at which the character c last
 * appears; NULL if not found
 */

#define NULL 0

char *
rindex(sp, c)
	register char *sp, c;
{
	register char *r;

	r = NULL;
	do {
		if (*sp == c)
			r = sp;
	} while (*sp++);
	return (r);
}
