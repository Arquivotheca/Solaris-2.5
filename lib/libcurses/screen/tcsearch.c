/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)tcsearch.c	1.5	92/07/14 SMI"	/* SVr4.0 1.3	*/

_tcsearch(cap, offsets, names, size, n)
char	*cap;
short	offsets[];
char	*names[];
int	size, n;
{
    register	int	l = 0, u = size - 1;
    int		m, cmp;

    while (l <= u)
    {
	m = (l + u) / 2;
	cmp = ((n == 0) ? strcmp(cap, names[offsets[m]]) :
			  strncmp(cap, names[offsets[m]], n));

	if (cmp < 0)
	    u = m - 1;
	else
	    if (cmp > 0)
		l = m + 1;
	    else
		return (offsets[m]);
    }
    return (-1);
}
