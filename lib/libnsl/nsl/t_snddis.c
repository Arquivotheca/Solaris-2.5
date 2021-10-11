/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)t_snddis.c	1.18	94/03/02 SMI"	/* SVr4.0 1.4.3.1	*/

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


t_snddis(fd, call)
int fd;
struct t_call *call;
{
	struct T_discon_req dreq;
	struct strbuf ctlbuf;
	struct strbuf databuf;
	register struct _ti_user *tiptr;
	sigset_t mask, t_mask;


	trace2(TR_t_snddis, 0, fd);
	if ((tiptr = _t_checkfd(fd)) == NULL) {
		int sv_errno = errno;
		trace2(TR_t_snddis, 1, fd);
		errno = sv_errno;
		return (-1);
	}
	MUTEX_LOCK_SIGMASK(&tiptr->ti_lock, mask);

	if (tiptr->ti_servtype == T_CLTS) {
		t_errno = TNOTSUPPORT;
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace2(TR_t_snddis, 1, fd);
		return (-1);
	}

	/*
	 * look at look buffer to see if there is a discon there
	 */

	if (_t_look_locked(fd, tiptr) == T_DISCONNECT) {
		int sv_errno = errno;
		t_errno = TLOOK;
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace2(TR_t_snddis, 1, fd);
		errno = sv_errno;
		return (-1);
	}

	tiptr->ti_lookflg = 0;

	if (ioctl(fd, I_FLUSH, FLUSHW) < 0) {
		int sv_errno = errno;

		t_errno = TSYSERR;
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace2(TR_t_snddis, 1, fd);
		errno = sv_errno;
		return (-1);
	}

	_t_blocksigpoll(&t_mask, SIG_BLOCK);

	dreq.PRIM_type = T_DISCON_REQ;
	dreq.SEQ_number = (call? call->sequence: -1);


	ctlbuf.maxlen = sizeof (struct T_discon_req);
	ctlbuf.len = sizeof (struct T_discon_req);
	ctlbuf.buf = (caddr_t)&dreq;

	databuf.maxlen = (call? call->udata.len: 0);
	databuf.len = (call? call->udata.len: 0);
	databuf.buf = (call? call->udata.buf: NULL);

	if (putmsg(fd, &ctlbuf, (databuf.len? &databuf: NULL), 0) < 0) {
		int sv_errno = errno;

		t_errno = TSYSERR;
		_t_blocksigpoll(&t_mask, SIG_SETMASK);
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace2(TR_t_snddis, 1, fd);
		errno = sv_errno;
		return (-1);
	}

	if (!_t_is_ok(fd, tiptr, T_DISCON_REQ)) {
		int sv_errno = errno;
		_t_blocksigpoll(&t_mask, SIG_SETMASK);
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace2(TR_t_snddis, 1, fd);
		errno = sv_errno;
		return (-1);
	}

	tiptr->ti_flags &= ~MORE;

	if (tiptr->ti_ocnt <= 1) {
		if (tiptr->ti_state == T_INCON)
			tiptr->ti_ocnt--;
		tiptr->ti_state = TLI_NEXTSTATE(T_SNDDIS1, tiptr->ti_state);
#ifdef DEBUG
		if (tiptr->ti_state == nvs)
			syslog(LOG_ERR,
				"t_snddis: invalid state event T_SNDDIS1");
#endif DEBUG
	} else {
		if (tiptr->ti_state == T_INCON)
			tiptr->ti_ocnt--;
		tiptr->ti_state = TLI_NEXTSTATE(T_SNDDIS2, tiptr->ti_state);
#ifdef DEBUG
		if (tiptr->ti_state == nvs)
			syslog(LOG_ERR,
				"t_snddis: invalid state event T_SNDDIS2");
#endif DEBUG
	}
	_t_blocksigpoll(&t_mask, SIG_SETMASK);
	MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
	trace2(TR_t_snddis, 1, fd);
	return (0);
}
