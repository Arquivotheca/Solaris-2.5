/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)t_listen.c	1.16	94/03/02 SMI"	/* SVr4.0 1.5	*/

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
#include <syslog.h>
#include "tli.h"


t_listen(fd, call)
int fd;
struct t_call *call;
{
	struct strbuf ctlbuf;
	struct strbuf rcvbuf;
	int flg = 0;
	int retval;
	register union T_primitives *pptr;
	register struct _ti_user *tiptr;
	sigset_t mask;

	trace2(TR_t_listen, 0, fd);
	if ((tiptr = _t_checkfd(fd)) == NULL) {
		int sv_errno = errno;
		trace2(TR_t_listen, 1, fd);
		errno = sv_errno;
		return (-1);
	}
	MUTEX_LOCK_SIGMASK(&tiptr->ti_lock, mask);

	/*
	 * check if something in look buffer
	 */
	if (tiptr->ti_lookflg) {
		t_errno = TLOOK;
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace2(TR_t_listen, 1, fd);
		return (-1);
	}

	if (tiptr->ti_servtype == T_CLTS) {
		int sv_errno = errno;
		t_errno = TNOTSUPPORT;
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace2(TR_t_listen, 1, fd);
		errno = sv_errno;
		return (-1);
	}

	ctlbuf.maxlen = tiptr->ti_ctlsize;
	ctlbuf.len = 0;
	ctlbuf.buf = tiptr->ti_ctlbuf;
	rcvbuf.maxlen = tiptr->ti_rcvsize;
	rcvbuf.len = 0;
	rcvbuf.buf = tiptr->ti_rcvbuf;

	if ((retval = getmsg(fd, &ctlbuf, &rcvbuf, &flg)) < 0) {
		int sv_errno = errno;

		if (errno == EAGAIN)
			t_errno = TNODATA;
		else
			t_errno = TSYSERR;
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace2(TR_t_listen, 1, fd);
		errno = sv_errno;
		return (-1);
	}
	if (rcvbuf.len == -1) rcvbuf.len = 0;

	/*
	 * did I get entire message?
	 */
	if (retval) {
		t_errno = TSYSERR;
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace2(TR_t_listen, 1, fd);
		errno = EIO;
		return (-1);
	}

	/*
	 * is ctl part large enough to determine type
	 */
	if (ctlbuf.len < sizeof (long)) {
		t_errno = TSYSERR;
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace2(TR_t_listen, 1, fd);
		errno = EPROTO;
		return (-1);
	}

	pptr = (union T_primitives *)ctlbuf.buf;

	switch (pptr->type) {

		case T_CONN_IND:
			if ((ctlbuf.len < sizeof (struct T_conn_ind)) ||
			    (ctlbuf.len < (pptr->conn_ind.OPT_length
			    + pptr->conn_ind.OPT_offset))) {
				t_errno = TSYSERR;
				MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
				trace2(TR_t_listen, 1, fd);
				errno = EPROTO;
				return (-1);
			}
			if ((rcvbuf.len > call->udata.maxlen) ||
			    (pptr->conn_ind.SRC_length > call->addr.maxlen) ||
			    (pptr->conn_ind.OPT_length > call->opt.maxlen)) {
				t_errno = TBUFOVFLW;
				tiptr->ti_ocnt++;
				tiptr->ti_state = TLI_NEXTSTATE(T_LISTN,
							tiptr->ti_state);
#ifdef DEBUG
				if (tiptr->ti_state == nvs)
				    syslog(LOG_ERR,
					"t_listen:invalid state event T_LISTN");
#endif DEBUG
				MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
				trace2(TR_t_listen, 1, fd);
				return (-1);
			}

			memcpy(call->addr.buf, ctlbuf.buf +
				pptr->conn_ind.SRC_offset,
				(int)pptr->conn_ind.SRC_length);
			call->addr.len = pptr->conn_ind.SRC_length;
			memcpy(call->opt.buf, ctlbuf.buf +
				pptr->conn_ind.OPT_offset,
				(int)pptr->conn_ind.OPT_length);
			call->opt.len = pptr->conn_ind.OPT_length;
			memcpy(call->udata.buf, rcvbuf.buf, (int)rcvbuf.len);
			call->udata.len = rcvbuf.len;
			call->sequence = (long) pptr->conn_ind.SEQ_number;

			tiptr->ti_ocnt++;
			tiptr->ti_state = TLI_NEXTSTATE(T_LISTN,
							tiptr->ti_state);
#ifdef DEBUG
		if (tiptr->ti_state == nvs)
			syslog(LOG_ERR,
				"t_listen: invalid state event T_CONNECT2");
#endif DEBUG
			MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
			trace2(TR_t_listen, 1, fd);
			return (0);

		case T_DISCON_IND:
			_t_putback(tiptr, rcvbuf.buf, rcvbuf.len, ctlbuf.buf,
					ctlbuf.len);
			t_errno = TLOOK;
			MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
			trace2(TR_t_listen, 1, fd);
			return (-1);

		default:
			break;
	}

	t_errno = TSYSERR;
	MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
	trace2(TR_t_listen, 1, fd);
	errno = EPROTO;
	return (-1);
}
