/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)fileno.c	1.10	93/01/21 SMI"	/* SVr4.0 1.6	*/

/*LINTLIBRARY*/

#ifdef __STDC__
#ifdef _REENTRANT
#pragma weak fileno_unlocked = _fileno_unlocked
#endif
#pragma weak fileno = _fileno
#endif

#include "synonyms.h"
#include <stdio.h>
#include <thread.h>
#include <synch.h>
#include <mtlib.h>
#include "stdiom.h"

#undef fileno

#ifdef _REENTRANT
#undef _fileno_unlocked

int
_fileno(iop)
	FILE *iop;
{
	FLOCKRETURN(iop, iop->_file);
}
#else
#define _fileno_unlocked _fileno
#endif _REENTRANT


int
_fileno_unlocked(iop)
	FILE *iop;
{
	return(iop->_file);
}
