/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)t_bind.c	1.16	94/03/02 SMI"	/* SVr4.0 1.3.4.1	*/

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


t_bind(fd, req, ret)
int fd;
register struct t_bind *req;
register struct t_bind *ret;
{
	register char *buf;
	register struct T_bind_req *ti_bind;
	int size;
	register struct _ti_user *tiptr;
	sigset_t mask, t_mask;

	trace2(TR_t_bind, 0, fd);
	if ((tiptr = _t_checkfd(fd)) == NULL) {
		int sv_errno = errno;
		trace2(TR_t_bind, 1, fd);
		errno = sv_errno;
		return (-1);
	}
	MUTEX_LOCK_SIGMASK(&tiptr->ti_lock, mask);
	_t_blocksigpoll(&t_mask, SIG_BLOCK);

	buf = tiptr->ti_ctlbuf;
	ti_bind = (struct T_bind_req *)buf;
	size = sizeof (struct T_bind_req);

	ti_bind->PRIM_type = T_BIND_REQ;
	ti_bind->ADDR_length = (req == NULL? 0: req->addr.len);
	ti_bind->ADDR_offset = 0;
	ti_bind->CONIND_number = (req == NULL? 0: req->qlen);


	if (ti_bind->ADDR_length) {
		_t_aligned_copy(buf, (int)ti_bind->ADDR_length, size,
				req->addr.buf, &ti_bind->ADDR_offset);
		size = ti_bind->ADDR_offset + ti_bind->ADDR_length;
	}


	if (!_t_do_ioctl(fd, buf, size, TI_BIND, NULL)) {
		int sv_errno = errno;
		_t_blocksigpoll(&t_mask, SIG_SETMASK);
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace2(TR_t_bind, 1, fd);
		errno = sv_errno;
		return (-1);
	}
	_t_blocksigpoll(&t_mask, SIG_SETMASK);

	tiptr->ti_ocnt = 0;
	tiptr->ti_state = TLI_NEXTSTATE(T_BIND, tiptr->ti_state);
#ifdef DEBUG
	if (tiptr->ti_state == nvs)
		syslog(LOG_ERR,
			"t_bind: invalid state event T_BIND");
#endif DEBUG

	if ((ret != NULL) && (ti_bind->ADDR_length > ret->addr.maxlen)) {
		t_errno = TBUFOVFLW;
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace2(TR_t_bind, 1, fd);
		return (-1);
	}

	if (ret != NULL) {
		memcpy(ret->addr.buf, (char *)(buf + ti_bind->ADDR_offset),
			(int)ti_bind->ADDR_length);
		ret->addr.len = ti_bind->ADDR_length;
		ret->qlen = ti_bind->CONIND_number;
	}
	MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
	trace2(TR_t_bind, 1, fd);
	return (0);
}
