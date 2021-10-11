/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */
    
#pragma ident	"@(#)scrwidth.c	1.5	91/12/04 SMI"

#include	<stdlib.h>
#include	<ctype.h>
#include	<widec.h>

#pragma weak scrwidth=_scrwidth
int _scrwidth(wchar_t c)
{
	if (!iswprint(c)) /* including control characters... */
		return (0);
	switch (c&WCHAR_CSMASK) {
	case WCHAR_CS0:
		return (1);
	case WCHAR_CS1:
		return (scrw1);
	case WCHAR_CS2:
		return (scrw2);
	case WCHAR_CS3:
		return (scrw3);
	default: /* shouldn't happen.... */
		return (0);
	}
}
