/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)t_open.c	1.21	94/03/02 SMI"	/* SVr4.0 1.5.3.3	*/

#include "timt.h"
#include <sys/param.h>
#include <sys/types.h>
#include <rpc/trace.h>
#include <errno.h>
#include <sys/stream.h>
#include <sys/stropts.h>
#include <sys/tihdr.h>
#include <sys/timod.h>
#include <tiuser.h>
#include <sys/signal.h>
#include <sys/fcntl.h>
#include "tli.h"


t_open(path, flags, info)
char *path;
int flags;
register struct t_info *info;
{
	int retval, fd;
	struct T_info_ack inforeq;
	register struct _ti_user *tiptr;
	int retlen;
	sigset_t mask, t_mask;

	trace2(TR_t_open, 0, flags);
	if (!(flags & O_RDWR)) {
		t_errno = TBADFLAG;
		trace2(TR_t_open, 1, flags);
		return (-1);
	}

	if ((fd = open(path, flags)) < 0) {
		int sv_errno = errno;

		t_errno = TSYSERR;
		trace2(TR_t_open, 1, flags);
		errno = sv_errno;
		return (-1);
	}
	/*
	 * is module already pushed
	 */
	if ((retval = ioctl(fd, I_FIND, "timod")) < 0) {
		int sv_errno = errno;

		t_errno = TSYSERR;
		close(fd);
		trace2(TR_t_open, 1, flags);
		errno = sv_errno;
		return (-1);
	}

	_t_blocksigpoll(&t_mask, SIG_BLOCK);
	if (!retval)
		if (ioctl(fd, I_PUSH, "timod") < 0) {
			int sv_errno = errno;

			t_errno = TSYSERR;
			close(fd);
			_t_blocksigpoll(&t_mask, SIG_SETMASK);
			trace2(TR_t_open, 1, flags);
			errno = sv_errno;
			return (-1);
		}

	MUTEX_LOCK_PROCMASK(&_ti_userlock, mask);
	tiptr = _t_create(fd, info);
	if (tiptr == NULL) {
		int sv_errno = errno;

		MUTEX_UNLOCK_PROCMASK(&_ti_userlock, mask);
		_t_blocksigpoll(&t_mask, SIG_SETMASK);
		errno = sv_errno;
		return (-1);
	}
	/*
	 * _t_state synchronizes state witk kernel timod and
	 * already sets it to T_UNBND - what it needs to be
	 * be on T_OPEN event. No TLI_NEXTSTATE needed here.
	 */
	MUTEX_UNLOCK_PROCMASK(&_ti_userlock, mask);
	if (ioctl(fd, I_FLUSH, FLUSHRW) < 0) {
		int sv_errno = errno;

		_t_blocksigpoll(&t_mask, SIG_SETMASK);
		errno = sv_errno;
		return (-1);
	}
	_t_blocksigpoll(&t_mask, SIG_SETMASK);
	trace2(TR_t_open, 1, flags);
	return (fd);
}
