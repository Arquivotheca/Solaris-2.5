/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)t_rcvrel.c	1.18	94/03/02 SMI"	/* SVr4.0 1.7.3.1	*/

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



t_rcvrel(fd)
int fd;
{
	struct strbuf ctlbuf;
	struct strbuf databuf;
	int retval;
	int flg = 0;
	union T_primitives *pptr;
	struct _ti_user *tiptr;
	sigset_t mask, t_mask;

	trace2(TR_t_rcvrel, 0, fd);
	if ((tiptr = _t_checkfd(fd)) == 0) {
		int sv_errno = errno;
		trace2(TR_t_rcvrel, 1, fd);
		errno = sv_errno;
		return (-1);
	}
	MUTEX_LOCK_SIGMASK(&tiptr->ti_lock, mask);

	if (tiptr->ti_servtype != T_COTS_ORD) {
		t_errno = TNOTSUPPORT;
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace2(TR_t_rcvrel, 1, fd);
		return (-1);
	}

	_t_blocksigpoll(&t_mask, SIG_BLOCK);
	if ((retval = _t_look_locked(fd, tiptr)) < 0) {
		int sv_errno = errno;
		_t_blocksigpoll(&t_mask, SIG_SETMASK);
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace2(TR_t_rcvrel, 1, fd);
		errno = sv_errno;
		return (-1);
	}

	if (retval == T_DISCONNECT) {
		int sv_errno = errno;
		t_errno = TLOOK;
		_t_blocksigpoll(&t_mask, SIG_SETMASK);
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace2(TR_t_rcvrel, 1, fd);
		errno = sv_errno;
		return (-1);
	}

	if (tiptr->ti_lookflg &&
	    (*((long *)tiptr->ti_lookcbuf) == T_ORDREL_IND)) {
		tiptr->ti_lookflg = 0;

		tiptr->ti_state = TLI_NEXTSTATE(T_RCVREL, tiptr->ti_state);
#ifdef DEBUG
		if (tiptr->ti_state == nvs)
			syslog(LOG_ERR,
				"t_rcv: invalid state event T_RCVREL");
#endif DEBUG
		_t_blocksigpoll(&t_mask, SIG_SETMASK);
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace2(TR_t_rcvrel, 1, fd);
		return (0);
	} else {
		if (retval != T_ORDREL) {
			t_errno = TNOREL;
		_t_blocksigpoll(&t_mask, SIG_SETMASK);
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
			trace2(TR_t_rcvrel, 1, fd);
			return (-1);
		}
	}

	/*
	 * get ordrel off read queue.
	 * use ctl and rcv buffers
	 */
	ctlbuf.maxlen = tiptr->ti_ctlsize;
	ctlbuf.len = 0;
	ctlbuf.buf = tiptr->ti_ctlbuf;
	databuf.maxlen = tiptr->ti_rcvsize;
	databuf.len = 0;
	databuf.buf = tiptr->ti_rcvbuf;

	if ((retval = getmsg(fd, &ctlbuf, &databuf, &flg)) < 0) {
		int sv_errno = errno;

		t_errno = TSYSERR;
		_t_blocksigpoll(&t_mask, SIG_SETMASK);
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace2(TR_t_rcvrel, 1, fd);
		errno = sv_errno;
		return (-1);
	}

	_t_blocksigpoll(&t_mask, SIG_SETMASK);
	/*
	 * did I get entire message?
	 */
	if (retval) {
		t_errno = TSYSERR;
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace2(TR_t_rcvrel, 1, fd);
		errno = EIO;
		return (-1);
	}
	pptr = (union T_primitives *)ctlbuf.buf;

	if ((ctlbuf.len < sizeof (struct T_ordrel_ind)) ||
	    (pptr->type != T_ORDREL_IND)) {
		t_errno = TSYSERR;
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace2(TR_t_rcvrel, 1, fd);
		errno = EPROTO;
		return (-1);
	}

	tiptr->ti_state = TLI_NEXTSTATE(T_RCVREL, tiptr->ti_state);
#ifdef DEBUG
	if (tiptr->ti_state == nvs)
		syslog(LOG_ERR,
			"t_rcvrel: invalid state event T_RCVREL");
#endif DEBUG
	MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
	trace2(TR_t_rcvrel, 1, fd);
	return (0);
}
