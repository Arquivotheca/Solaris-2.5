/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)fseek.c	1.13	92/10/01 SMI"	/* SVr4.0 1.15	*/

/*	3.0 SID #	1.2	*/
/*LINTLIBRARY*/
/*
 * Seek for standard library.  Coordinates with buffering.
 */
#include "synonyms.h"
#include "shlib.h"
#include <stdio.h>
#include <errno.h>
#include <thread.h>
#include <synch.h>
#include <mtlib.h>
#include "stdiom.h"

extern long lseek();
extern int _fflush_u();

int
fseek(iop, offset, ptrname)
register FILE *iop;
long	offset;
int	ptrname;
{
	long	p;
#ifdef _REENTRANT
	rmutex_t *lk;
#endif _REENTRANT

	FLOCKFILE(lk, iop);
	iop->_flag &= (unsigned short)~_IOEOF;

	if (!(iop->_flag & _IOREAD) && !(iop->_flag & (_IOWRT | _IORW))) {
		errno = EBADF;
		FUNLOCKFILE(lk);
		return(-1);
	}

	if(iop->_flag & _IOREAD) {
		if(ptrname == 1 && iop->_base && !(iop->_flag&_IONBF)) {
			offset -= iop->_cnt;
		}
	} else if(iop->_flag & (_IOWRT | _IORW)) {
		if (_fflush_u(iop) == EOF) {
			FUNLOCKFILE(lk);
			return(-1);
		}
	}
	iop->_cnt = 0;
	iop->_ptr = iop->_base;
	if(iop->_flag & _IORW) {
		iop->_flag &= (unsigned short)~(_IOREAD | _IOWRT);
	}
	p = lseek(FILENO(iop), offset, ptrname);
	FUNLOCKFILE(lk);
	return((p == -1)? -1: 0);
}
