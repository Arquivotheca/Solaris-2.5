/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)t_unbind.c	1.17	94/03/02 SMI"	/* SVr4.0 1.3.4.1	*/

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
#include <syslog.h>
#include "tli.h"


t_unbind(fd)
int fd;
{
	register struct _ti_user *tiptr;
	struct T_unbind_req *unbind_req;
	sigset_t mask, t_mask;

	trace2(TR_t_unbind, 0, fd);
	if ((tiptr = _t_checkfd(fd)) == NULL) {
		int sv_errno = errno;
		trace2(TR_t_unbind, 1, fd);
		errno = sv_errno;
		return (-1);
	}
	MUTEX_LOCK_SIGMASK(&tiptr->ti_lock, mask);
	_t_blocksigpoll(&t_mask, SIG_BLOCK);
	if (_t_is_event(fd, tiptr)) {
		int sv_errno = errno;
		_t_blocksigpoll(&t_mask, SIG_SETMASK);
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace2(TR_t_unbind, 1, fd);
		errno = sv_errno;
		return (-1);
	}

	unbind_req = (struct T_unbind_req *)tiptr->ti_ctlbuf;
	unbind_req->PRIM_type = T_UNBIND_REQ;

	if (!_t_do_ioctl(fd, (caddr_t)unbind_req, sizeof (struct T_unbind_req),
			TI_UNBIND, NULL)) {
		int sv_errno = errno;
		_t_blocksigpoll(&t_mask, SIG_SETMASK);
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace2(TR_t_unbind, 1, fd);
		errno = sv_errno;
		return (-1);
	}

	_t_blocksigpoll(&t_mask, SIG_SETMASK);

	if (ioctl(fd, I_FLUSH, FLUSHRW) < 0) {
		int sv_errno = errno;

		t_errno = TSYSERR;
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace2(TR_t_unbind, 1, fd);
		errno = sv_errno;
		return (-1);
	}

	/*
	 * clear more data in TSDU bit
	 */
	tiptr->ti_flags &= ~MORE;

	tiptr->ti_state = TLI_NEXTSTATE(T_UNBIND, tiptr->ti_state);
#ifdef DEBUG
	if (tiptr->ti_state == nvs)
		syslog(LOG_ERR,
			"t_unbind: invalid state event T_UNBIND");
#endif DEBUG
	MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
	trace2(TR_t_unbind, 1, fd);
	return (0);
}
