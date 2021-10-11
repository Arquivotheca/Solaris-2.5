/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)t_optmgmt.c	1.17	94/03/02 SMI"
		/* SVr4.0 1.3.4.1	*/

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


t_optmgmt(fd, req, ret)
int fd;
struct t_optmgmt *req;
struct t_optmgmt *ret;
{
	int size;
	register char *buf;
	register struct T_optmgmt_req *optreq;
	register struct _ti_user *tiptr;
	sigset_t mask, t_mask;

	trace2(TR_t_optmgmt, 0, fd);
	if ((tiptr = _t_checkfd(fd)) == NULL) {
		int sv_errno = errno;
		trace2(TR_t_optmgmt, 1, fd);
		errno = sv_errno;
		return (-1);
	}
	MUTEX_LOCK_SIGMASK(&tiptr->ti_lock, mask);
	_t_blocksigpoll(&t_mask, SIG_BLOCK);

	buf = tiptr->ti_ctlbuf;
	optreq = (struct T_optmgmt_req *)buf;
	optreq->PRIM_type = T_OPTMGMT_REQ;
	optreq->OPT_length = req->opt.len;
	optreq->OPT_offset = 0;
	optreq->MGMT_flags = req->flags;
	size = sizeof (struct T_optmgmt_req);

	if (req->opt.len) {
		_t_aligned_copy(buf, req->opt.len, size,
				req->opt.buf, &optreq->OPT_offset);
		size = optreq->OPT_offset + optreq->OPT_length;
	}

	if (!_t_do_ioctl(fd, buf, size, TI_OPTMGMT, NULL)) {
		int sv_errno = errno;
		_t_blocksigpoll(&t_mask, SIG_SETMASK);
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace2(TR_t_optmgmt, 1, fd);
		errno = sv_errno;
		return (-1);
	}
	_t_blocksigpoll(&t_mask, SIG_SETMASK);

	if (optreq->OPT_length > ret->opt.maxlen) {
		t_errno = TBUFOVFLW;
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace2(TR_t_optmgmt, 1, fd);
		return (-1);
	}

	memcpy(ret->opt.buf, (char *) (buf + optreq->OPT_offset),
		(int)optreq->OPT_length);
	ret->opt.len = optreq->OPT_length;
	ret->flags = optreq->MGMT_flags;

	tiptr->ti_state = TLI_NEXTSTATE(T_OPTMGMT, tiptr->ti_state);
#ifdef DEBUG
	if (tiptr->ti_state == nvs)
		syslog(LOG_ERR,
			"t_optmgmt: invalid state event T_OPTMGMT");
#endif DEBUG
	MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
	trace2(TR_t_optmgmt, 1, fd);
	return (0);
}
