/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)ferror.c	1.8	92/10/01 SMI"	/* SVr4.0 1.2	*/

/*LINTLIBRARY*/

#ifdef __STDC__
#ifdef _REENTRANT
#pragma weak ferror_unlocked = _ferror_unlocked
#endif
#endif

#include "synonyms.h"
#include <stdio.h>
#include <thread.h>
#include <synch.h>
#include <mtlib.h>
#include "stdiom.h"

#undef ferror

#ifdef _REENTRANT
#undef ferror_unlocked

int
ferror(iop)
	FILE *iop;
{
	FLOCKRETURN(iop, iop->_flag & _IOERR);
}
#else
#define _ferror_unlocked ferror
#endif _REENTRANT

int
_ferror_unlocked(iop)
	FILE *iop;
{
	return(iop->_flag & _IOERR);
}
