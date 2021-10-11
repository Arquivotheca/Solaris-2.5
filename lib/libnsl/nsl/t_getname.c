/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)t_getname.c	1.16	94/03/02 SMI"
		/* SVr4.0 1.1.1.1	*/

#include "timt.h"
#include <errno.h>
#include <sys/types.h>
#include <rpc/trace.h>
#include <sys/stream.h>
#include <sys/stropts.h>
#include <sys/tihdr.h>
#include <tiuser.h>
#include <sys/timod.h>
#include <sys/signal.h>
#include "tli.h"

t_getname(fd, name, type)
int fd;
struct netbuf *name;
register int type;
{
	sigset_t mask, t_mask;
	register struct _ti_user *tiptr;

	trace3(TR_t_getname, 0, fd, type);
	if (!name || ((type != LOCALNAME) && (type != REMOTENAME))) {
		trace3(TR_t_getname, 1, fd, type);
		errno = EINVAL;
		return (-1);
	}

	if ((tiptr = _t_checkfd(fd)) == 0) {
		int sv_errno = errno;
		trace3(TR_t_getname, 1, fd, type);
		errno = sv_errno;
		return (-1);
	}
	MUTEX_LOCK_SIGMASK(&tiptr->ti_lock, mask);
	_t_blocksigpoll(&t_mask, SIG_BLOCK);

	if (type == LOCALNAME) {
		if (ioctl(fd, TI_GETMYNAME, name) < 0) {
			int sv_errno = errno;

			t_errno = TSYSERR;
			_t_blocksigpoll(&t_mask, SIG_SETMASK);
			MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
			trace3(TR_t_getname, 1, fd, type);
			errno = sv_errno;
			return (-1);
		}
	} else {	/* REMOTENAME */
		if (ioctl(fd, TI_GETPEERNAME, name) < 0) {
			int sv_errno = errno;

			t_errno = TSYSERR;
			_t_blocksigpoll(&t_mask, SIG_SETMASK);
			MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
			trace3(TR_t_getname, 1, fd, type);
			errno = sv_errno;
			return (-1);
		}
	}

	_t_blocksigpoll(&t_mask, SIG_SETMASK);
	MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);

	trace3(TR_t_getname, 1, fd, type);
	return (0);
}
