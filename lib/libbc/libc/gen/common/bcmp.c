#pragma ident	"@(#)bcmp.c	1.3	92/07/21 SMI"
	  /* from UCB X.X XX/XX/XX */

bcmp(s1, s2, len)
	register char *s1, *s2;
	register int len;
{

	while (len--)
		if (*s1++ != *s2++)
			return (1);
	return (0);
}
