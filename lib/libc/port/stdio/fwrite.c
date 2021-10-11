/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident  "@(#)fwrite.c 1.22	95/08/18 SMI"		/* SVr4.0 3.24  */

#include "synonyms.h"
#include "shlib.h"
#include <stdio.h>
#include "stdiom.h"
#include <values.h>
#include <memory.h>
#include <thread.h>
#include <synch.h>
#include <mtlib.h>
#include <sys/types.h>

size_t
_fwrite_unlocked(const void *ptr, size_t size, size_t count, FILE *iop);

size_t
fwrite(const void *ptr, size_t size, size_t count, FILE *iop)
{
#ifdef _REENTRANT
	if (__threaded) {
		rmutex_t *lk = _flockget(iop);
		size_t retval = _fwrite_unlocked(ptr, size, count, iop);
		_flockrel(lk);
		return (retval);
	}
#endif
	return (_fwrite_unlocked(ptr, size, count, iop));
}

size_t
_fwrite_unlocked(const void *ptr, size_t size, size_t count, FILE *iop)
{
	int s;
	int n;
	char *dptr = (char *)ptr;
	unsigned char *bufend;

	if (_WRTCHK(iop))
		return (0);

	/*
	 * This test is here to avoid the expensive multiply
	 */
	if (count == 1)
		s = size;
	else if (size == 1)
		s = count;
	else
		s = size * count;

	if (iop->_flag & _IOLBF) {
		bufend = _bufend(iop);
		if (iop->_ptr == iop->_base)
			iop->_cnt = 0;
		while (s > 0) {
			int buflen = bufend - iop->_base;
			if (--iop->_cnt >= (-buflen) && *dptr != '\n')
				*iop->_ptr++ = *dptr++;
			else if (_flsbuf(*dptr++, iop) == EOF)
				break;
			s--;
		}
	} else if (iop->_flag & _IONBF) {
		int bytes;
		int written = 0;
		char *data;

		if (size < 1 || count < 1)
			return (0);

		if (iop->_base != iop->_ptr) {
		/*
		 * Flush any existing data. Doesn't count towards return
		 * value.
		 */
			bytes = iop->_ptr - iop->_base;
			data = (char *)iop->_base;

			while ((n = write(fileno(iop), data, bytes)) != bytes) {
				if (n == -1) {
					iop->_flag |= _IOERR;
					return (0);
				} else {
					data += n;
					bytes -= n;
				}
			}
			iop->_cnt = 0;
			iop->_ptr = iop->_base;
		}
		/*
		 * written is in bytes until the return.
		 * Then it is divided by size to produce items.
		 */
		while ((n = write(fileno(iop), dptr, s)) != s) {
			if (n == -1) {
				iop->_flag |= _IOERR;
				return (written / size);
			} else {
				dptr += n;
				s -= n;
				written += n;
			}
		}
		written += n;
		return (written / size);
	} else while (s > 0) {
		if (iop->_cnt < s) {
			if (iop->_cnt > 0) {
				memcpy(iop->_ptr, (void*)dptr, iop->_cnt);
				dptr += iop->_cnt;
				iop->_ptr += iop->_cnt;
				s -= iop->_cnt;
			}
			if (_xflsbuf(iop) == EOF)
				break;
		}
		if (iop->_cnt >= s) {
			char *tmp = (char *)iop->_ptr;

			switch (s) {
			case 8:
				*tmp++ = *dptr++;
			case 7:
				*tmp++ = *dptr++;
			case 6:
				*tmp++ = *dptr++;
			case 5:
				*tmp++ = *dptr++;
			case 4:
				*tmp++ = *dptr++;
			case 3:
				*tmp++ = *dptr++;
			case 2:
				*tmp++ = *dptr++;
			case 1:
				*tmp++ = *dptr++;
				break;
			case 0:
				return (count);
			default:
				memcpy(iop->_ptr, (void *)dptr, s);
			}
			iop->_ptr += s;
			iop->_cnt -= s;

			return (count);
		}
	}

	return (size != 0 ? count - ((s + size - 1) / size) : 0);
}
