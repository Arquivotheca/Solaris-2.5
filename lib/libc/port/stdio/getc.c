/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)getc.c	1.10	92/09/23 SMI"	/* SVr4.0 1.3	*/

/*LINTLIBRARY*/

#ifdef __STDC__
#ifdef _REENTRANT
#pragma weak getc_unlocked = _getc_unlocked
#endif
#endif

#include "synonyms.h"
#include <stdio.h>
#include "stdiom.h"
#include <thread.h>
#include <synch.h>
#include <mtlib.h>

#undef getc

#ifdef _REENTRANT
#undef _getc_unlocked

getc(iop)
	register FILE *iop;
{
	rmutex_t *lk;

	register int c;
	FLOCKFILE(lk, iop);
	c = (--iop->_cnt < 0) ? _filbuf(iop) : *iop->_ptr++;
	FUNLOCKFILE(lk);
	return c;
}
#else
#define _getc_unlocked getc
#endif _REENTRANT


int
_getc_unlocked(iop)
	register FILE *iop;
{
	register int c;
	return (--iop->_cnt < 0) ? _filbuf(iop) : *iop->_ptr++;
}

