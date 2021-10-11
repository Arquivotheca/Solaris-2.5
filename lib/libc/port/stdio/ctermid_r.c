/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)ctermid.c	1.4	89/10/23 SMI"	/* SVr4.0 1.12	*/

/*	3.0 SID #	1.2	*/
/*LINTLIBRARY*/
#ifdef __STDC__
	#pragma weak ctermid_r = _ctermid_r
#endif
#include "synonyms.h"
#include <stdio.h>
#include <string.h>
#include <thread.h>
#include <synch.h>
#include <mtlib.h>

/*
 * re-entrant version of ctermid()
 */

char *
_ctermid_r(s)
	register char *s;
{
	return (s ? strcpy(s, "/dev/tty") : NULL);
}
