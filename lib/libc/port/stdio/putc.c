/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)putc.c	1.10	92/09/23 SMI"	/* SVr4.0 1.4	*/

/*LINTLIBRARY*/

#ifdef __STDC__
#ifdef _REENTRANT
#pragma weak putc_unlocked = _putc_unlocked
#endif
#endif

#include "synonyms.h"
#include <stdio.h>
#include "stdiom.h"
#include <thread.h>
#include <synch.h>
#include <mtlib.h>

#undef putc

#ifdef _REENTRANT
#undef putc_unlocked

int
putc(ch, iop)
	int ch;
	register FILE *iop;
{
	rmutex_t *lk;

	FLOCKFILE(lk, iop);
	if (--iop->_cnt < 0)
		ch = _flsbuf((unsigned char) ch, iop);
	else
		(*iop->_ptr++) = (unsigned char)ch;
	FUNLOCKFILE(lk);
	return(ch);
}
#else
#define _putc_unlocked putc
#endif _REENTRANT


int
_putc_unlocked(ch, iop)
	int ch;
	register FILE *iop;
{
	if (--iop->_cnt < 0)
		return _flsbuf((unsigned char) ch, iop);
	else
		return *iop->_ptr++ = (unsigned char)ch;
}

