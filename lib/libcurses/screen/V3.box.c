/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)V3.box.c	1.5	92/07/14 SMI"	/* SVr4.0 1.1	*/

#include	"curses_inc.h"
extern	int	_outchar();

#ifdef	_VR3_COMPAT_CODE
#undef	box
box(win, v, h)
WINDOW		*win;
_ochtype 	v, h;
{
    return (wborder(win, _FROM_OCHTYPE(v), _FROM_OCHTYPE(v),
	_FROM_OCHTYPE(h), _FROM_OCHTYPE(h), 0, 0, 0, 0));
}
#endif	/* _VR3_COMPAT_CODE */
