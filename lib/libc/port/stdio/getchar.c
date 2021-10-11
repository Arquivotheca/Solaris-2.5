/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)getchar.c	1.7	92/09/05 SMI"	/* SVr4.0 1.8	*/

/*	3.0 SID #	1.2	*/
/*LINTLIBRARY*/
/*
 * A subroutine version of the macro getchar.
 */

#ifdef __STDC__
#ifdef _REENTRANT
#pragma weak getchar_unlocked = _getchar_unlocked
#endif
#endif

#include "synonyms.h"
#include "shlib.h"
#include <stdio.h>
#include <thread.h>
#include <synch.h>
#include <mtlib.h>

#undef getchar

#ifdef _REENTRANT
#undef _getchar_unlocked

int
getchar()
{
	register FILE *iop = stdin;

	return getc(iop);
}
#else
#define _getchar_unlocked getchar
#endif

/*
 *A subroutine version of the macro getchar_unlocked.
 */


int
_getchar_unlocked()
{
	register FILE *iop = stdin;

	return GETC(iop);
}
