/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)t_close.c	1.17	95/02/16 SMI"	/* SVr4.0 1.5	*/

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
#include "tli.h"


t_close(fd)
int fd;
{
	register struct _ti_user *tiptr;
	sigset_t mask;

	trace2(TR_t_close, 0, fd);
	if ((tiptr = _t_checkfd(fd)) == NULL) {
		int sv_errno = errno;
		trace2(TR_t_close, 1, fd);
		errno = sv_errno;
		return (-1);
	}

	MUTEX_LOCK_PROCMASK(&_ti_userlock, mask);
	if (delete_tilink(fd) < 0) {
		int sv_errno = errno;
		MUTEX_UNLOCK_PROCMASK(&_ti_userlock, mask);
		trace2(TR_t_close, 1, fd);
		errno = sv_errno;
		return (-1);
	}
	/*
	 * Note: close() needs to be inside the lock. If done
	 * outside, another process may inherit the desriptor
	 * and recreate library level instance structures
	 */
	close(fd);

	MUTEX_UNLOCK_PROCMASK(&_ti_userlock, mask);
	trace2(TR_t_close, 1, fd);
	return (0);
}
