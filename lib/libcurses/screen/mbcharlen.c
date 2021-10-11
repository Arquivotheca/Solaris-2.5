/*  Copyright (c) 1988 AT&T */
/*    All Rights Reserved   */

/*  THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T */
/*  The copyright notice above does not evidence any    */
/*  actual or intended publication of such source code. */

#ident  "@(#)mbcharlen.c 1.1 93/05/05 SMI"
 
#include	"curses_inc.h"

/*
**	Get the # of valid characters
*/
mbcharlen(sp)
char	*sp;
{
	reg int		n, m, k, ty;
	reg chtype	c;

	n = 0;
	for(; *sp != '\0'; ++sp, ++n)
		if(ISMBIT(*sp))
		{
			c = RBYTE(*sp);
			ty = TYPE(c & 0377);
			m  = cswidth[ty] - (ty == 0 ? 1 : 0);
			for(sp += 1, k = 1; *sp != '\0' && k <= m; ++k, ++sp)
			{
				c = RBYTE(*sp);
				if(TYPE(c) != 0)
					break;
			}
			if(k <= m)
				break;
		}
	return n;
}
