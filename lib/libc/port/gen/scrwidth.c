/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)scrwidth.c	1.3	92/01/29 SMI"

#pragma weak scrwidth=_simple_scrwidth

#include	"synonyms.h"
#include	<stdlib.h>
#include	<ctype.h>
#include	<widec.h>

int _simple_scrwidth(wchar_t c)
{
	/*
	 * Ideally the following statement should exist here:
	 *	if (!iswprint(c)) return (0);
	 * but we ommitted this because that would introduce dependency
	 * on libw where iswprint() is defined.
	 * We simply assume all characters from supplenebtart codesets
	 * are valid and printable.
	 */
	switch (c&WCHAR_CSMASK) {
	case WCHAR_CS0:
		if (c&~0xff==0 /* Invalid strange code... */ ||
		    !isprint(c) /* An 8-bit but not printable char */)
			return (0);
		else
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
