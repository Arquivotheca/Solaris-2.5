/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)flush.c	1.23	95/03/06 SMI"	/* SVr4.0 1.22	*/
/*LINTLIBRARY*/		/* This file always part of stdio usage */

#include "synonyms.h"
#include "shlib.h"
#include <stdlib.h>
#include <stdio.h>
#include "stdiom.h"
#include <thread.h>
#include <synch.h>
#include <mtlib.h>

#undef _cleanup
#undef end

/* CSTYLED */
#pragma fini (_cleanup)
#define	FILE_ARY_SZ	8 /* a nice size for FILE array & end_buffer_ptrs */

/*
 * initial array of end-of-buffer ptrs
 */

extern Uchar _smbuf[][_SMBFSZ];
static Uchar *_cp_bufendtab[_NFILE + 1] = /* for alternate system - original */
	{ NULL, NULL, _smbuf[2] + _SBFSIZ, };

typedef struct _link_ Link;	/* list of iob's */

struct _link_	/* manages a list of streams */
{
	FILE	*iobp;		/* the array of FILE's */
	Uchar 	**endbuf;	/* the array of end buffer pointers */
#ifdef _REENTRANT
	rmutex_t *lockbuf;
#endif _REENTRANT
	int	niob;		/* length of the arrays */
	Link	*next;		/* next in the list */
};

/*
 * With dynamic linking, iob may be in either the library or in the user's
 * a.out, so the run time linker fixes up the first entry in __first_link at
 * process startup time.
 */
Link __first_link =	/* first in linked list */
{
#if DSHLIB
	0,
#else
	&_iob[0],
#endif
	&_cp_bufendtab[0],
#ifdef _REENTRANT
	&_locktab[0],
#endif _REENTRANT
	_NFILE,
	0
};

#ifdef _REENTRANT
static rwlock_t _first_link_lock = DEFAULTRWLOCK;
#endif _REENTRANT

int _fflush_u();
static int _fflush_u_iops();

/*
* All functions that understand the linked list of iob's follow.
*/

void
_cleanup()	/* called at process end to flush ouput streams */
{
	fflush(NULL);
}

void
_flushlbf()	/* fflush() all line-buffered streams */
{
	register FILE *fp;
	register int i;
	register Link *lp;

#ifdef _REENTRANT
	_rw_rdlock(&_first_link_lock);
#endif _REENTRANT
	lp = &__first_link;

	do {
		fp = lp->iobp;
		for (i = lp->niob; --i >= 0; fp++) {
			if ((fp->_flag & (_IOLBF | _IOWRT)) ==
				(_IOLBF | _IOWRT))
				(void) _fflush_u(fp);
		}
	} while ((lp = lp->next) != 0);
#ifdef _REENTRANT
	_rw_unlock(&_first_link_lock);
#endif _REENTRANT
}


FILE *
_findiop()	/* allocate an unused stream; 0 if cannot */
{
	register Link *lp, **prev;
	/* used so there only needs to be one malloc() */
	typedef	struct	{
		Link	hdr;
		FILE	iob[FILE_ARY_SZ];
		Uchar	*nbuf[FILE_ARY_SZ]; /* array of end buffer pointers */
#ifdef _REENTRANT
		rmutex_t nlock[FILE_ARY_SZ];
#endif _REENTRANT
	} Pkg;
	register Pkg *pkgp;
	register FILE *fp;

#ifdef _REENTRANT
	if (__threaded)
		_rw_wrlock(&_first_link_lock);
#endif _REENTRANT
	lp = &__first_link;

	/* lock to make testing of fp->_flag == 0 and acquiring the fp atomic */
	/* and for allocation of new links */
	/* low contention expected on _findiop(), hence coarse locking. */
	/* for finer granularity, use iop->_lock for allocating an iop */
	/* and make the testing of lp->next and allocation of new link atomic */
	/* using lp->_lock */
	do {
		register int i;
#ifdef _REENTRANT
		rmutex_t *lk;
#endif _REENTRANT

		prev = &lp->next;
		fp = lp->iobp;
		for (i = lp->niob; --i >= 0; fp++) {
#ifdef _REENTRANT
			lk = &lp->lockbuf[fp - lp->iobp];
			if (__threaded && _rmutex_trylock(lk))
				continue;	/* being locked: fp in use */
#endif _REENTRANT
			if (fp->_flag == 0) {	/* unused */
				fp->_cnt = 0;
				fp->_ptr = 0;
				fp->_base = 0;
#ifdef _REENTRANT
				fp->_flag = -1;
				/* claim the fp */
				FUNLOCKFILE(lk);
				if (__threaded)
					_rw_unlock(&_first_link_lock);
#endif _REENTRANT
				return (fp);
			}
			FUNLOCKFILE(lk);
		}
	} while ((lp = lp->next) != 0);
	/*
	 * Need to allocate another and put it in the linked list.
	 */
	if ((pkgp = (Pkg *) malloc(sizeof (Pkg))) == 0) {
#ifdef _REENTRANT
		if (__threaded)
			_rw_unlock(&_first_link_lock);
#endif _REENTRANT
		return (0);
	}
	(void) memset(pkgp, 0, sizeof (Pkg));
	pkgp->hdr.iobp = &pkgp->iob[0];
	pkgp->hdr.niob = sizeof (pkgp->iob) / sizeof (FILE);
	pkgp->hdr.endbuf = &pkgp->nbuf[0];
#ifdef _REENTRANT
	pkgp->hdr.lockbuf = &pkgp->nlock[0];
#endif _REENTRANT
	*prev = &pkgp->hdr;
#ifdef _REENTRANT
	pkgp->iob[0]._flag = -1; /* claim the fp */
	if (__threaded)
		_rw_unlock(&_first_link_lock);
#endif
	return (&pkgp->iob[0]);
}

void
_setbufend(iop, end)	/* set the end pointer for this iop */
	register FILE *iop;
	Uchar *end;
{
	register Link *lp;

#ifdef _REENTRANT
	if (__threaded)
		_rw_rdlock(&_first_link_lock);
#endif _REENTRANT
	lp = &__first_link;

	/*
	 * Old mechanism.  Retained for binary compatibility.
	 */
	if (iop->_file < _NFILE)
		_bufendtab[iop->_file] = end;

	/*
	 * New mechanism.  Allows more than _NFILE iop's.
	 */
	do {
		if ((lp->iobp <= iop) && (iop < (lp->iobp + lp->niob))) {
			lp->endbuf[iop - lp->iobp] = end;
			break;
		}
	} while ((lp = lp->next) != 0);
#ifdef _REENTRANT
	if (__threaded)
		_rw_unlock(&_first_link_lock);
#endif	_REENTRANT
}

Uchar *
_realbufend(iop) 	/* get the end pointer for this iop */
FILE * iop;
{
	register Link *lp;
	Uchar *result = 0;

#ifdef _REENTRANT
	if (__threaded)
		_rw_rdlock(&_first_link_lock);
#endif _REENTRANT
	lp = &__first_link;

	/*
	 * Use only the new mechanism here.
	 */
	do {
		if ((lp->iobp <= iop) && (iop < (lp->iobp + lp->niob))) {
			result = lp->endbuf[iop - lp->iobp];
			break;
		}
	} while ((lp = lp->next) != 0);
#ifdef _REENTRANT
	if (__threaded)
		_rw_unlock(&_first_link_lock);
#endif _REENTRANT
	return (result);
}

rmutex_t *
_reallock(iop)
FILE *iop;
{
	register Link *lp;
	rmutex_t *result = 0;

#ifdef _REENTRANT
	_rw_rdlock(&_first_link_lock);
#endif _REENTRANT
	lp = &__first_link;

	do {
		if ((lp->iobp <= iop) && (iop < (lp->iobp + lp->niob))) {
			result = &lp->lockbuf[iop - lp->iobp];
			break;
		}
	} while ((lp = lp->next) != 0);
#ifdef _REENTRANT
	_rw_unlock(&_first_link_lock);
#endif _REENTRANT
	return (result);
}

void
_bufsync(iop, bufend)	/* make sure _cnt, _ptr are correct */
	register FILE *iop;
	register Uchar *bufend;
{
	register int spaceleft;

	if ((spaceleft = bufend - iop->_ptr) < 0)
	{
		iop->_ptr = bufend;
		iop->_cnt = 0;
	} else if (spaceleft < iop->_cnt)
		iop->_cnt = spaceleft;
}

extern int write();

int
_xflsbuf(iop)	/* really write out current buffer contents */
	register FILE *iop;
{
	register int n;
	register Uchar *base = iop->_base;
	register Uchar *bufend;
	int num_wrote;

	/*
	* Hopefully, be stable with respect to interrupts...
	*/
	n = iop->_ptr - base;
	iop->_ptr = base;
	bufend = _bufend(iop);
	if (iop->_flag & (_IOLBF | _IONBF))
		iop->_cnt = 0; /* always go to a flush */
	else
		iop->_cnt = bufend - base;
	if (_needsync(iop, bufend))   /* recover from interrupts */
		_bufsync(iop, bufend);
	if (n > 0) {
		while ((num_wrote =
			write(iop->_file, (char *)base, (unsigned)n)) != n) {
			if (num_wrote <= 0) {
				iop->_flag |= _IOERR;
				return (EOF);
			}
			n -= num_wrote;
			base += num_wrote;
		}
	}
	return (0);
}

int
fflush(iop)	/* flush (write) buffer */
	register FILE *iop;
{
	int res;
#ifdef _REENTRANT
	rmutex_t *lk;
#endif _REENTRANT

	if (iop) {
		FLOCKFILE(lk, iop);
		res = _fflush_u(iop);
		FUNLOCKFILE(lk);
	} else {
		res = _fflush_u_iops();		/* flush all iops */
	}
	return (res);
}

static int
_fflush_u_iops()	/* flush (write) all buffers */
{
	register FILE *iop;
	register int i;
	register Link *lp;
	int res = 0;

#ifdef _REENTRANT
	if (__threaded)
		_rw_rdlock(&_first_link_lock);
#endif _REENTRANT
	lp = &__first_link;

	do {
		/* Don't grab the locks for these file pointers */
		/* since they are supposed to be flushed anyway */
		/* It could also be the case in which the 2nd   */
		/* portion (base and lock) are not initialized	*/
		iop = lp->iobp;
		for (i = lp->niob; --i >= 0; iop++) {
			if (!(iop->_flag & _IONBF) &&
			    (iop->_flag & (_IOWRT | _IOREAD | _IORW)))
				res |= _fflush_u(iop);
		}
	} while ((lp = lp->next) != 0);
#ifdef _REENTRANT
	if (__threaded)
		_rw_unlock(&_first_link_lock);
#endif _REENTRANT
	return (res);
}

int
_fflush_u(iop)	/* flush (write) buffer */
	register FILE *iop;
{
	int res = 0;

	/* this portion is always assumed locked */
	if (!(iop->_flag & _IOWRT))
	{
		lseek(iop->_file, -iop->_cnt, SEEK_CUR);
		iop->_cnt = 0;
		/* needed for ungetc & mulitbyte pushbacks */
		iop->_ptr = iop->_base;
		if (iop->_flag & _IORW) {
			iop->_flag &= (unsigned short)~_IOREAD;
		}
		return (0);
	}
	if (iop->_base != 0 && iop->_ptr > iop->_base)
		res = _xflsbuf(iop);
	if (iop->_flag & _IORW) {
		iop->_flag &= (unsigned short)~_IOWRT;
		iop->_cnt = 0;
	}
	return (res);
}

extern int close();

int
fclose(iop)	/* flush buffer and close stream */
	register FILE *iop;
{
	register int res = 0;
#ifdef _REENTRANT
	rmutex_t *lk;
#endif _REENTRANT

	if (iop == 0) {
		return (EOF);		/* avoid passing zero to FLOCKFILE */
	}
	FLOCKFILE(lk, iop);
	if (iop->_flag == 0) {
		FUNLOCKFILE(lk);
		return (EOF);
	}
	/* Is not unbuffered and opened for read and/or write ? */
	if (!(iop->_flag & _IONBF) && (iop->_flag & (_IOWRT | _IOREAD | _IORW)))
		res = _fflush_u(iop);
	if (close(iop->_file) < 0)
		res = EOF;
	if (iop->_flag & _IOMYBUF)
	{
		free((char *)iop->_base - PUSHBACK);
		/* free((VOID *)iop->_base); */
	}
	iop->_base = 0;
	iop->_ptr = 0;
	iop->_cnt = 0;
	iop->_flag = 0;			/* marks it as available */
	FUNLOCKFILE(lk);
	return (res);
}

#ifdef _REENTRANT
int
close_fd(iop)	/* flush buffer, close fd but keep the stream */
		/* used by freopen() */
	register FILE *iop;
{
	register int res = 0;

	if (iop == 0 || iop->_flag == 0)
		return (EOF);
	/* Is not unbuffered and opened for read and/or write ? */
	if (!(iop->_flag & _IONBF) && (iop->_flag & (_IOWRT | _IOREAD | _IORW)))
		res = _fflush_u(iop);
	if (close(iop->_file) < 0)
		res = EOF;
	if (iop->_flag & _IOMYBUF)
	{
		free((char *)iop->_base - PUSHBACK);
		/* free((VOID *)iop->_base); */
	}
	iop->_base = 0;
	iop->_ptr = 0;
	iop->_cnt = 0;
	return (res);
}
#endif _REENTRANT
