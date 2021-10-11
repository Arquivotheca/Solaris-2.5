/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)outch.c	1.6	93/05/05 SMI"	/* SVr4.0 1.1	*/
 
#include	"curses_inc.h"

int	outchcount;

/* Write out one character to the tty and increment outchcount. */

_outch(c)
chtype	c;
{
	return(_outwch(c));
}

_outwch(c)
chtype	c;
{
    register chtype	o;

#ifdef	DEBUG
#ifndef	LONGDEBUG
    if (outf)
	if (c < ' ' || c == 0177)
	    fprintf(outf, "^%c", c^0100);
	else
	    fprintf(outf, "%c", c&0177);
#else	/* LONGDEBUG */
	if (outf)
	    fprintf(outf, "_outch: char '%s' term %x file %x=%d\n",
		unctrl(c&0177), SP, cur_term->Filedes, fileno(SP->term_file));
#endif	/* LONGDEBUG */
#endif	/* DEBUG */

    outchcount++;

    /* ASCII code */
    if(!ISMBIT(c))
	putc((int)c, SP->term_file);
    /* international code */
    else if((o = RBYTE(c)) != MBIT)
	{
	putc(o,SP->term_file);
	if(_csmax > 1 && (((o = LBYTE(c))|MBIT) != MBIT))
		{
		SETMBIT(o);
		putc(o,SP->term_file);
		}
	}
}
