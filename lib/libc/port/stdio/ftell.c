/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)ftell.c	1.12	92/10/01 SMI"	/* SVr4.0 1.13	*/

/*	3.0 SID #	1.2	*/
/*LINTLIBRARY*/
/*
 * Return file offset.
 * Coordinates with buffering.
 */
#include "synonyms.h"
#include <stdio.h>
#include <thread.h>
#include <synch.h>
#include <mtlib.h>
#include <errno.h>
#include "stdiom.h"

extern long lseek();

long
ftell(iop)
	register FILE	*iop;
{
	register int adjust;
	long	tres;
#ifdef _REENTRANT
	rmutex_t *lk;
#endif _REENTRANT

	FLOCKFILE(lk, iop);
	if (iop->_cnt < 0)
		iop->_cnt = 0;
	if (iop->_flag & _IOREAD)
		adjust = -iop->_cnt;
	else if (iop->_flag & (_IOWRT | _IORW)) 
	{
		adjust = 0;
		if (((iop->_flag & (_IOWRT | _IONBF)) == _IOWRT) && (iop->_base != 0))
			adjust = iop->_ptr - iop->_base;
	}
	else {
		errno = EBADF;	/* file descriptor refers to no open file */
		FUNLOCKFILE(lk);
		return EOF;
	}

	tres = lseek(FILENO(iop), 0L, 1);
	if (tres >= 0)
		tres += (long)adjust;
	FUNLOCKFILE(lk);
	return tres;
}
