/*  Copyright (c) 1988 AT&T */
/*    All Rights Reserved   */

/*  THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T */
/*  The copyright notice above does not evidence any    */
/*  actual or intended publication of such source code. */

#ident  "@(#)wctomb.c 1.1 93/05/10 SMI"
 
/*LINTLIBRARY*/

#include <widec.h>
#include "synonyms.h"
#include <ctype.h>
#include <stdlib.h>
#include "curses_wchar.h"

int
_curs_wctomb(s, wchar)
char *s;
wchar_t wchar;
{
	char *olds = s;
	register int size, index;
	unsigned char d;
	if(!s)
		return(0);
    if(wchar <= 0177 || (wchar <= 0377 && iscntrl(wchar)))  {
		*s++ = (char)wchar;
		return(1);
	}
	switch(wchar & EUCMASK) {
			
		case P11:
			size = eucw1;
			break;
			
		case P01:
			*s++ = (char)SS2;
			size = eucw2;
			break;
			
		case P10:
			*s++ = (char)SS3;
			size = eucw3;
			break;
			
		default:
			return(-1);
	}
	if((index = size) <= 0)
		return -1;	
	while(index--) {
		d = wchar | 0200;
		wchar >>= 7;
		if(iscntrl(d))
			return(-1);
		s[index] = d;
	}
	return(s + size - olds);
}
