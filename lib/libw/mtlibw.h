#ident "@(#)mtlibw.h 1.2 92/10/06 SMI"

#ifndef _MTLIBW_H
#define	_MTLIBW_H

#ifdef _REENTRANT

#define	mutex_lock(m)			_mutex_lock(m)
#define	mutex_unlock(m)			_mutex_unlock(m)

#define	FILENO(s) _fileno_unlocked(s)
#define	FEOF(s) _feof_unlocked(s)
#define	FERROR(s) _ferror_unlocked(s)
#define	CLEARERR(s) _clearerr_unlocked(s)
#define	GETC(s) _getc_unlocked(s)
#define	UNGETC(c, s) _ungetc_unlocked(c, s)
#define	PUTC(c, s) _putc_unlocked(c, s)

#else

#define	mutex_lock(m)
#define	mutex_unlock(m)
#define	_thr_getspecific(x, y)
#define	_thr_setspecific(x, y)
#define	_thr_keycreate(x, y)

#define	FILENO(s) fileno(s)
#define	FEOF(s) feof(s)
#define	FERROR(s) ferror(s)
#define	CLEARERR(s) clearerr(s)
#define	GETC(s) getc(s)
#define	UNGETC(c, s) ungetc(c, s)
#define	PUTC(c, s) putc(c, s)

#endif _REENTRANT

#endif _MTLIBW_H_
