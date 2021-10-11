/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)putchar.c	1.7	92/09/05 SMI"	/* SVr4.0 1.8	*/

/*	3.0 SID #	1.2	*/
/*LINTLIBRARY*/
/*
 * A subroutine version of the macro putchar
 */

#ifdef __STDC__
#ifdef _REENTRANT
#pragma weak putchar_unlocked = _putchar_unlocked
#endif
#endif

#include "synonyms.h"
#include "shlib.h"
#include <stdio.h>
#include <thread.h>
#include <synch.h>
#include <mtlib.h>

#undef putchar

int
putchar(ch)
	int ch;
{
	register FILE *iop = stdout;

	return putc(ch, iop);
}

#ifdef _REENTRANT
#undef _putchar_unlocked

/*
 * A subroutine version of the macro putchar_unlocked
 */

int
_putchar_unlocked(ch)
	int ch;
{
	register FILE *iop = stdout;

	return PUTC(ch, iop);
}
#endif
