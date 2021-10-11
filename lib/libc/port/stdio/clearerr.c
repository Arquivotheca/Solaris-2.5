/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)clearerr.c	1.9	92/10/01 SMI"	/* SVr4.0 1.3	*/

/*LINTLIBRARY*/

#ifdef __STDC__
#ifdef _REENTRANT
#pragma weak clearerr_unlocked = _clearerr_unlocked
#endif
#endif

#include "synonyms.h"
#include <stdio.h>
#include <thread.h>
#include <synch.h>
#include <mtlib.h>
#include "stdiom.h"

#undef clearerr

#ifdef _REENTRANT
#undef _clearerr_unlocked

void
clearerr(iop)
	FILE *iop;
{
	rmutex_t *lk;

	FLOCKFILE(lk,iop);
	iop->_flag &= (unsigned char)~(_IOERR | _IOEOF);
	FUNLOCKFILE(lk);
}
#else
#define _clearerr_unlocked clearerr
#endif _REENTRANT

void
_clearerr_unlocked(iop)
	FILE *iop;
{
	iop->_flag &= (unsigned char)~(_IOERR | _IOEOF);
}
