/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident  "@(#)fread.c 1.15	95/03/13 SMI"		/* SVr4.0 3.29  */

/*LINTLIBRARY*/

#include "synonyms.h"
#include "shlib.h"
#include <stdio.h>
#include "stdiom.h"
#include <stddef.h>
#include <values.h>
#include <memory.h>
#include <thread.h>
#include <synch.h>
#include <mtlib.h>
#include <errno.h>

size_t
fread(ptr, size, count, iop)
	void *ptr;
	size_t size, count;
	FILE *iop;
{
	int s;
	int c;
	char * dptr = (char *)ptr;
#ifdef _REENTRANT
	rmutex_t *lk;

	FLOCKFILE(lk, iop);
#endif _REENTRANT

	/* is it a readable stream */
	if (!(iop->_flag & (_IOREAD | _IORW))) {
		iop->_flag |= _IOERR;
		errno = EBADF;
#ifdef _REENTRANT
		FUNLOCKFILE(lk);
#endif _REENTRANT
		return (0);
	}

	if (iop->_flag & _IOEOF) {
#ifdef _REENTRANT
		FUNLOCKFILE(lk);
#endif _REENTRANT
		return (0);
	}

	/* These checks are here to avoid the multiply */
	if (count == 1)
		s = size;
	else if (size == 1)
		s = count;
	else
		s = size * count;

	while (s > 0) {
		if (iop->_cnt < s) {
			if (iop->_cnt > 0) {
				memcpy((void*)dptr, iop->_ptr, iop->_cnt);
				dptr += iop->_cnt;
				s -= iop->_cnt;
			}
			/*
			 * filbuf clobbers _cnt & _ptr,
			 * so don't waste time setting them.
			 */
			if ((c = _filbuf(iop)) == EOF)
				break;
			*dptr++ = c;
			s--;
		}
		if (iop->_cnt >= s) {
			char * tmp = (char *)iop->_ptr;
			switch (s) {
			case 8:
				*dptr++ = *tmp++;
			case 7:
				*dptr++ = *tmp++;
			case 6:
				*dptr++ = *tmp++;
			case 5:
				*dptr++ = *tmp++;
			case 4:
				*dptr++ = *tmp++;
			case 3:
				*dptr++ = *tmp++;
			case 2:
				*dptr++ = *tmp++;
			case 1:
				*dptr++ = *tmp++;
				break;
			default:
				memcpy((void*)dptr, iop->_ptr, s);
			}
			iop->_ptr += s;
			iop->_cnt -= s;
#ifdef _REENTRANT
			FUNLOCKFILE(lk);
#endif _REENTRANT
			return (count);
		}
	}
#ifdef _REENTRANT
	FUNLOCKFILE(lk);
#endif _REENTRANT
	return (size != 0 ? count - ((s + size - 1) / size) : 0);
}
