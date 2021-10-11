/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)t_rcvdis.c	1.18	94/03/02 SMI"	/* SVr4.0 1.10	*/

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


t_rcvdis(fd, discon)
int fd;
struct t_discon *discon;
{
	struct strbuf ctlbuf;
	struct strbuf databuf;
	int retval;
	int flg = 0;
	union T_primitives *pptr;
	register struct _ti_user *tiptr;
	sigset_t mask, t_mask;

	trace2(TR_t_rcvdis, 0, fd);
	if ((tiptr = _t_checkfd(fd)) == NULL) {
		int sv_errno = errno;
		trace2(TR_t_rcvdis, 1, fd);
		errno = sv_errno;
		return (-1);
	}
	MUTEX_LOCK_SIGMASK(&tiptr->ti_lock, mask);

	if (tiptr->ti_servtype == T_CLTS) {
		t_errno = TNOTSUPPORT;
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace2(TR_t_rcvdis, 1, fd);
		return (-1);
	}

	/*
	 * is there a discon in look buffer
	 */
	_t_blocksigpoll(&t_mask, SIG_BLOCK);
	if (tiptr->ti_lookflg &&
	    (*((long *)tiptr->ti_lookcbuf) == T_DISCON_IND)) {
		ctlbuf.maxlen = tiptr->ti_lookcsize;
		ctlbuf.len = tiptr->ti_lookcsize;
		ctlbuf.buf = tiptr->ti_lookcbuf;
		databuf.maxlen = tiptr->ti_lookdsize;
		databuf.len = tiptr->ti_lookdsize;
		databuf.buf = tiptr->ti_lookdbuf;
	} else {

		if ((retval = _t_look_locked(fd, tiptr)) < 0) {
			int sv_errno = errno;
			_t_blocksigpoll(&t_mask, SIG_SETMASK);
			MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
			trace2(TR_t_rcvdis, 1, fd);
			errno = sv_errno;
			return (-1);
		}


		if (retval != T_DISCONNECT) {
			t_errno = TNODIS;
			_t_blocksigpoll(&t_mask, SIG_SETMASK);
			MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
			trace2(TR_t_rcvdis, 1, fd);
			return (-1);
		}

		/*
		 * get disconnect off read queue.
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
			trace2(TR_t_rcvdis, 1, fd);
			errno = sv_errno;
			return (-1);
		}
		if (databuf.len == -1) databuf.len = 0;

		/*
		 * did I get entire message?
		 */
		if (retval) {
			t_errno = TSYSERR;
			_t_blocksigpoll(&t_mask, SIG_SETMASK);
			MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
			trace2(TR_t_rcvdis, 1, fd);
			errno = EIO;
			return (-1);
		}
	}

	_t_blocksigpoll(&t_mask, SIG_SETMASK);
	tiptr->ti_lookflg = 0;

	pptr = (union T_primitives *)ctlbuf.buf;

	if ((ctlbuf.len < sizeof (struct T_discon_ind)) ||
	    (pptr->type != T_DISCON_IND)) {
		t_errno = TSYSERR;
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace2(TR_t_rcvdis, 1, fd);
		errno = EPROTO;
		return (-1);
	}

	/*
	 * clear more and expedited flags
	 */
	tiptr->ti_flags &= ~(MORE | EXPEDITED);

	if (tiptr->ti_ocnt <= 0) {
		tiptr->ti_state = TLI_NEXTSTATE(T_RCVDIS1, tiptr->ti_state);
#ifdef DEBUG
		if (tiptr->ti_state == nvs)
			syslog(LOG_ERR,
				"t_rcvdis: invalid state event T_RCVDIS1");
#endif DEBUG
	} else {
		if (tiptr->ti_ocnt == 1)
			tiptr->ti_state = TLI_NEXTSTATE(T_RCVDIS2,
							tiptr->ti_state);
		else
			tiptr->ti_state = TLI_NEXTSTATE(T_RCVDIS3,
							tiptr->ti_state);
#ifdef DEBUG
		if (tiptr->ti_state == nvs)
			syslog(LOG_ERR,
				"t_rcvdis: invalid state event T_RCVDIS2/3");
#endif DEBUG
		tiptr->ti_ocnt--;
	}

	if (discon != NULL) {
		if (databuf.len > discon->udata.maxlen) {
			t_errno = TBUFOVFLW;
			MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
			trace2(TR_t_rcvdis, 1, fd);
			return (-1);
		}

		discon->reason = pptr->discon_ind.DISCON_reason;
		memcpy(discon->udata.buf, databuf.buf, (int)databuf.len);
		discon->udata.len = databuf.len;
		discon->sequence = (long) pptr->discon_ind.SEQ_number;
	}

	MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
	trace2(TR_t_rcvdis, 1, fd);
	return (0);
}
