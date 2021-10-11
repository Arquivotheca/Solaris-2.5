/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)ungetc.c	1.8	92/09/05 SMI"	/* SVr4.0 2.11	*/

/*	3.0 SID #	1.3	*/
/*LINTLIBRARY*/

#include "synonyms.h"
#include "shlib.h"
#include <stdio.h>
#include "stdiom.h"
#include <thread.h>
#include <synch.h>
#include <mtlib.h>

#ifdef _REENTRANT
int
ungetc(c, iop)
	int c;
	register FILE *iop;
{
	FLOCKRETURN(iop, _ungetc_unlocked(c, iop));
}
#else
#define _ungetc_unlocked ungetc
#endif
/* 
 * Called internally by the library (instead of the safe "ungetc") when 
 * iop->_lock is already held at a higher level - required since we do not 
 * have recursive locks.
 */
int
_ungetc_unlocked(c, iop)
	int c;
	register FILE *iop;
{
	if (c == EOF)   
		return EOF;
	if (iop->_ptr <= iop->_base)
	{
		if (iop->_base == 0)
		{
			if (_findbuf(iop) == 0) 
				return EOF;
		}
		else if (iop->_ptr <= iop->_base - PUSHBACK) 
			return EOF;
	}
	if ((iop->_flag & _IOREAD) == 0) /* basically a no-op on write stream */
		++iop->_ptr;
	if (*--iop->_ptr != c) *iop->_ptr = c;  /* was *--iop->_ptr = c; */
	++iop->_cnt;
	iop->_flag &= (unsigned short)~_IOEOF;
	return c;
}

