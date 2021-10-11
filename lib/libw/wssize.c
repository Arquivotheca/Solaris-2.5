/*	Copyright (c) 1986 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*	This module is created for NLS on Sep.03.86		*/

#ident	"@(#)wssize.c	1.3	92/07/14 SMI" 	/* from JAE2.0 1.0 */

/*
 * Wssize returns number of bytes in EUC for wchar_t string.
 */

#include <stdlib.h>
#include <widec.h>

extern int  _cswidth[];
extern void _xgetwidth();

int
_wssize(s)
register wchar_t *s;
{
	register size = 0;

	if (! *_cswidth)
		_xgetwidth(); /* get character width */

	while (*s) { /* end if wchar_t NULL character */
		switch (*s & WCHAR_CSMASK) {
			case WCHAR_CS0: /* Code Set 0 -or- control code */
				size += 1;
				break;
			case WCHAR_CS1: /* Code Set 1 */
				size += _cswidth[1];
				break;
			case WCHAR_CS2: /* Code Set 2 */
				size += _cswidth[2] + 1;
				break;
			case WCHAR_CS3: /* Code Set 3 */
				size += _cswidth[3] + 1;
		}
		s++; /* next wchar_t */
	}
	return(size);
}
