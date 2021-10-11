/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)fopen.c	1.15	93/11/15 SMI"	/* SVr4.0 1.20	*/

/*LINTLIBRARY*/

#include "synonyms.h"
#include <stdio.h>
#include "stdiom.h"
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <thread.h>
#include <synch.h>
#include <mtlib.h>

static FILE *
_endopen(name, type, iop)	/* open UNIX file name, associate with iop */
	const char *name;
	const char *type;
	register FILE *iop;
{
	register int oflag, plus, fd;
#ifdef _REENTRANT
	rmutex_t *lk;
#endif _REENTRANT

	if (iop == 0)
		return 0;
	switch (type[0])
	{
	default:
		return 0;
	case 'r':
		oflag = O_RDONLY;
		break;
	case 'w':
		oflag = O_WRONLY | O_TRUNC | O_CREAT;
		break;
	case 'a':
		oflag = O_WRONLY | O_APPEND | O_CREAT;
		break;
	}
	/* UNIX ignores 'b' and treats text and binary the same */
	if ((plus = type[1]) == 'b')
		plus = type[2];
	if (plus == '+')
		oflag = (oflag & ~(O_RDONLY | O_WRONLY)) | O_RDWR;
	if ((fd = open(name, oflag, 0666)) < 0)
		return 0;
	if (fd > UCHAR_MAX) {
		(void)close(fd);
		return 0;
	}
	iop->_file = (unsigned char)fd; /* assume fits in unsigned char */
	FLOCKFILE(lk, iop);		/* this lock may be unnecessary */
	if (plus == '+')
		iop->_flag = _IORW;
	else if (type[0] == 'r')
		iop->_flag = _IOREAD;
	else
		iop->_flag = _IOWRT;
	FUNLOCKFILE(lk);
	if (oflag == (O_WRONLY | O_APPEND | O_CREAT))	/* type == "a" */
		if (lseek(fd, 0L, 2) < 0L) {
			close(fd);
			return NULL;
		}
	return iop;	
}

FILE *
fopen(name, type)		/* open name, return new stream */
	const char *name;
	const char *type;
{
#ifdef _REENTRANT
        register FILE *iop;
        register FILE  *rc;

        iop = _findiop();
        /*
         * Note that iop is not locked here, since no other thread could
         * possibly call _endopen with the same iop at this point.
         */
        rc = _endopen(name, type, iop);
        if (rc == 0)
                iop->_flag = 0; /* release iop */
        return rc;
#else
	return _endopen(name, type, _findiop());
#endif _REENTRANT
}

FILE *
freopen(name, type, iop)	/* open name, associate with existing stream */
	const char *name;
	const char *type;
	FILE *iop;
{
#ifdef _REENTRANT
	FILE *rc;
	rmutex_t *lk;

	FLOCKFILE(lk, iop); 
	/* 
	 * there may be concurrent calls to reopen the same stream - need 
	 * to make freopen() atomic 
	 */
	(void)close_fd(iop); 

	/* new function to do everything that fclose() does, except */
	/* to release the iop - this cannot yet be released since */
	/* _endopen() is yet to be called on this iop */

	rc = _endopen(name, type, iop);
	if (rc == 0) 
		iop->_flag = 0; /* release iop */
	FUNLOCKFILE(lk);
	return rc;
#else
	(void)fclose(iop);
	return _endopen(name, type, iop);
#endif _REENTRANT
}
