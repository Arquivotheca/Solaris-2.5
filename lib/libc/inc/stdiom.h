/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)stdiom.h	1.17	95/03/07 SMI"	/* SVr4.0 1.9	*/

/*
* stdiom.h - shared guts of stdio therefore it doesn't need a surrounding #ifndef 
*/

#ifndef _STDIOM_H
#define _STDIOM_H
#include <thread.h>
#include <synch.h>
#include <mtlib.h>

/*
 * Cheap check to tell if library needs to lock for MT progs.
 * Referenced directly in port/stdio/flush.c and FLOCKFILE
 * and FUNLOCKFILE macros.
 *
 * Initial value is set in _init section of libc.so by _check_threaded.
 * __threaded gets set to 1 by _init section of libthread.so via call
 * to _libc_set_threaded.
 */
extern int __threaded;

typedef unsigned char	Uchar;

typedef struct {
	mutex_t _mutex;		/* protects all the fields in this struct */
	cond_t _cond;
	unsigned short _wait_cnt;
	unsigned short _lock_cnt;
	thread_t _owner;
} rmutex_t;

extern rmutex_t *_flockget();

#define MAXVAL (MAXINT - (MAXINT % BUFSIZ))

/*
* The number of actual pushback characters is the value
* of PUSHBACK plus the first byte of the buffer. The FILE buffer must,
* for performance reasons, start on a word aligned boundry so the value
* of PUSHBACK should be a multiple of word. 
* At least 4 bytes of PUSHBACK are needed. If sizeof(int) = 1 this breaks.
*/
#define PUSHBACK (((3 + sizeof(int) - 1) / sizeof(int)) * sizeof(int))

/* minimum buffer size must be at least 8 or shared library will break */
#define _SMBFSZ (((PUSHBACK + 4) < 8) ? 8 : (PUSHBACK + 4))

extern Uchar *_bufendtab[];

#if BUFSIZ == 1024
#	define MULTIBFSZ(SZ)	((SZ) & ~0x3ff)
#elif BUFSIZ == 512
# 	define MULTIBFSZ(SZ)    ((SZ) & ~0x1ff)
#else
#	define MULTIBFSZ(SZ)    ((SZ) - (SZ % BUFSIZ))
#endif

#undef _bufend
#define _bufend(iop) _realbufend(iop)

	/*
	* Internal routines from _iob.c
	*/
extern void	_cleanup(	/* void */	);
extern void	_flushlbf(	/* void */	);
extern FILE	*_findiop(	/* void */	);
extern Uchar 	*_realbufend(	/* FILE *iop */ );
extern rmutex_t	*_reallock(	/* FILE *iop */ );
extern void	_setbufend(	/* FILE *iop, Uchar *end */);
extern int	_wrtchk(	/* FILE *iop */	);

	/*
	* Internal routines from flush.c
	*/
extern void	_bufsync(	/* FILE *iop , Uchar *bufend */	);
extern int	_xflsbuf(	/* FILE *iop */	);

	/*
	* Internal routines from _findbuf.c
	*/
extern Uchar 	*_findbuf(	/* FILE *iop */	);

/* The following macros improve performance of the stdio by reducing the
	number of calls to _bufsync and _wrtchk.  _needsync checks whether 
	or not _bufsync needs to be called.  _WRTCHK has the same effect as
	_wrtchk, but often these functions have no effect, and in those cases
	the macros avoid the expense of calling the functions.  */

#define _needsync(p, bufend)	((bufend - (p)->_ptr) < \
					 ((p)->_cnt < 0 ? 0 : (p)->_cnt))

#define _WRTCHK(iop)	((((iop->_flag & (_IOWRT | _IOEOF)) != _IOWRT) \
				|| (iop->_base == 0)  \
				|| (iop->_ptr == iop->_base && iop->_cnt == 0 \
					&& !(iop->_flag & (_IONBF | _IOLBF)))) \
			? _wrtchk(iop) : 0 )

/* definition of recursive mutex lock for flockfile and friends */

#define DEFAULTRMUTEX   {DEFAULTMUTEX, DEFAULTCV, 0, 0, 0}

extern rmutex_t _locktab[];

#define IOB_LCK(iop) (((iop)->_file < _NFILE) ? &_locktab[(iop)->_file] : \
		              _reallock(iop))


#endif /* _STDIOM_H */
