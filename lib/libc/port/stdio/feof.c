/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)feof.c	1.8	92/10/01 SMI"	/* SVr4.0 1.2	*/

/*LINTLIBRARY*/

#ifdef __STDC__
#ifdef _REENTRANT
#pragma weak feof_unlocked = _feof_unlocked
#endif
#endif

#include "synonyms.h"
#include <stdio.h>
#include <thread.h>
#include <synch.h>
#include <mtlib.h>
#include "stdiom.h"

#undef feof

#ifdef _REENTRANT
#undef _feof_unlocked

int
feof(iop)
	FILE *iop;
{
	FLOCKRETURN(iop, iop->_flag & _IOEOF);
}
#else
#define _feof_unlocked feof
#endif _REENTRANT


int
_feof_unlocked(iop)
	FILE *iop;
{
	return(iop->_flag & _IOEOF);
}
