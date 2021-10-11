/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)t_accept.c	1.17	94/03/02 SMI"	/* SVr4.0 1.5.2.1	*/

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


t_accept(fd, resfd, call)
int fd;
int resfd;
struct t_call *call;
{
	char *buf;
	register struct T_conn_res *cres;
	struct strfdinsert strfdinsert;
	int size;
	int retval;
	register struct _ti_user *tiptr;
	register struct _ti_user *restiptr;
	sigset_t mask, t_mask;

	trace3(TR_t_accept, 0, fd, resfd);
	if ((tiptr = _t_checkfd(fd)) == NULL) {
		int sv_errno = errno;
		trace3(TR_t_accept, 1, fd, resfd);
		errno = sv_errno;
		return (-1);
	}
	if ((restiptr = _t_checkfd(resfd)) == NULL) {
		int sv_errno = errno;
		trace3(TR_t_accept, 1, fd, resfd);
		errno = sv_errno;
		return (-1);
	}
	MUTEX_LOCK_SIGMASK(&tiptr->ti_lock, mask);

	if (tiptr->ti_servtype == T_CLTS) {
		t_errno = TNOTSUPPORT;
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace3(TR_t_accept, 1, fd, resfd);
		return (-1);
	}

	if (fd != resfd)
	{
		if ((retval = ioctl(resfd, I_NREAD, &size)) < 0)
		{
			int sv_errno = errno;

			t_errno = TSYSERR;
			MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
			trace3(TR_t_accept, 1, fd, resfd);
			errno = sv_errno;
			return (-1);
		}
		if (retval)
		{
			t_errno = TBADF;
			MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
			trace3(TR_t_accept, 1, fd, resfd);
			return (-1);
		}
	}

	_t_blocksigpoll(&t_mask, SIG_BLOCK);
	if (_t_is_event(fd, tiptr)) {
		int sv_errno = errno;
		_t_blocksigpoll(&t_mask, SIG_SETMASK);
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace3(TR_t_accept, 1, fd, resfd);
		errno = sv_errno;
		return (-1);
	}

	buf = tiptr->ti_ctlbuf;
	cres = (struct T_conn_res *)buf;
	cres->PRIM_type = T_CONN_RES;
	cres->OPT_length = call->opt.len;
	cres->OPT_offset = 0;
	cres->SEQ_number = call->sequence;
	size = sizeof (struct T_conn_res);

	if (call->opt.len) {
		_t_aligned_copy(buf, call->opt.len, size,
				call->opt.buf, &cres->OPT_offset);
		size = cres->OPT_offset + cres->OPT_length;
	}


	strfdinsert.ctlbuf.maxlen = tiptr->ti_ctlsize;
	strfdinsert.ctlbuf.len = size;
	strfdinsert.ctlbuf.buf = buf;
	strfdinsert.databuf.maxlen = call->udata.maxlen;
	strfdinsert.databuf.len = (call->udata.len? call->udata.len: -1);
	strfdinsert.databuf.buf = call->udata.buf;
	strfdinsert.fildes = resfd;
	strfdinsert.offset = sizeof (long);
	strfdinsert.flags = 0;		/* could be EXPEDITED also */

	if (ioctl(fd, I_FDINSERT, &strfdinsert) < 0) {
		int sv_errno = errno;

		if (errno == EAGAIN)
			t_errno = TFLOW;
		else
			t_errno = TSYSERR;
		_t_blocksigpoll(&t_mask, SIG_SETMASK);
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace3(TR_t_accept, 1, fd, resfd);
		errno = sv_errno;
		return (-1);
	}

	if (!_t_is_ok(fd, tiptr, T_CONN_RES)) {
		int sv_errno = errno;
		_t_blocksigpoll(&t_mask, SIG_SETMASK);
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace3(TR_t_accept, 1, fd, resfd);
		errno = sv_errno;
		return (-1);
	}

	_t_blocksigpoll(&t_mask, SIG_SETMASK);

	if (tiptr->ti_ocnt == 1) {
		if (fd == resfd) {
			tiptr->ti_state = TLI_NEXTSTATE(T_ACCEPT1,
							tiptr->ti_state);
#ifdef DEBUG
			if (tiptr->ti_state == nvs)
				syslog(LOG_ERR,
				"t_accept: invalid state event T_ACCEPT1");
#endif DEBUG
		} else {
			tiptr->ti_state = TLI_NEXTSTATE(T_ACCEPT2,
							tiptr->ti_state);
			restiptr->ti_state = TLI_NEXTSTATE(T_PASSCON,
							restiptr->ti_state);
#ifdef DEBUG
			if (tiptr->ti_state == nvs)
				syslog(LOG_ERR,
				"t_accept: invalid state event T_ACCEPT2");
			if (restiptr->ti_state == nvs)
				syslog(LOG_ERR,
				"t_accept: invalid state event T_PASSCON");
#endif DEBUG
		}
	} else {
		tiptr->ti_state = TLI_NEXTSTATE(T_ACCEPT3, tiptr->ti_state);
		restiptr->ti_state = TLI_NEXTSTATE(T_PASSCON,
							restiptr->ti_state);
#ifdef DEBUG
		if (tiptr->ti_state == nvs)
			syslog(LOG_ERR,
				"t_accept: invalid state event T_ACCEPT3");
		if (restiptr->ti_state == nvs)
			syslog(LOG_ERR,
				"t_accept: invalid state event T_PASSCON");
#endif DEBUG
	}

	tiptr->ti_ocnt--;
	MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
	trace3(TR_t_accept, 1, fd, resfd);
	return (0);
}
