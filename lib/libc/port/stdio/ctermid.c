/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)ctermid.c	1.8	92/09/05 SMI"	/* SVr4.0 1.12	*/

/*	3.0 SID #	1.2	*/
/*LINTLIBRARY*/
#ifdef __STDC__
	#pragma weak ctermid = _ctermid
#endif
#include "synonyms.h"
#include <stdio.h>
#include <string.h>
#include <thread.h>
#include <synch.h>
#include <mtlib.h>

static char res[L_ctermid];

/*
 * non-reentrant version in ctermid_r.c
 */
char *
ctermid(s)
	register char *s;
{
	return strcpy((s ? s : res), "/dev/tty");
}
