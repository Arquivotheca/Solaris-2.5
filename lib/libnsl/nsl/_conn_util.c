/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)_conn_util.c	1.15	94/03/02 SMI"
		/* SVr4.0 1.6.3.2	*/
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


/*
 * Snd_conn_req - send connect request message to
 * transport provider
 */
int
_snd_conn_req(tiptr, call)
register struct _ti_user *tiptr;
register struct t_call *call;
{
	register struct T_conn_req *creq;
	struct strbuf ctlbuf;
	char *buf;
	int size;
	int fd;
	sigset_t mask;

	trace2(TR__snd_conn_req, 0, fd);
	fd = tiptr->ti_fd;

	if (tiptr->ti_servtype == T_CLTS) {
		t_errno = TNOTSUPPORT;
		trace2(TR__snd_conn_req, 1, fd);
		return (-1);
	}

	_t_blocksigpoll(&mask, SIG_BLOCK);
	if (_t_is_event(fd, tiptr)) {
		int sv_errno = errno;
		_t_blocksigpoll(&mask, SIG_SETMASK);
		trace2(TR__snd_conn_req, 1, fd);
		errno = sv_errno;
		return (-1);
	}


	buf = tiptr->ti_ctlbuf;
	creq = (struct T_conn_req *)buf;
	creq->PRIM_type = T_CONN_REQ;
	creq->DEST_length = call->addr.len;
	creq->DEST_offset = 0;
	creq->OPT_length = call->opt.len;
	creq->OPT_offset = 0;
	size = sizeof (struct T_conn_req);

	if (call->addr.len) {
		_t_aligned_copy(buf, call->addr.len, size,
				call->addr.buf, &creq->DEST_offset);
		size = creq->DEST_offset + creq->DEST_length;
	}
	if (call->opt.len) {
		_t_aligned_copy(buf, call->opt.len, size,
				call->opt.buf, &creq->OPT_offset);
		size = creq->OPT_offset + creq->OPT_length;
	}

	ctlbuf.maxlen = tiptr->ti_ctlsize;
	ctlbuf.len = size;
	ctlbuf.buf = buf;

	if (putmsg(fd, &ctlbuf, (call->udata.len? &call->udata: NULL), 0) < 0) {
		int sv_errno = errno;
		t_errno = TSYSERR;
		_t_blocksigpoll(&mask, SIG_SETMASK);
		trace2(TR__snd_conn_req, 1, fd);
		errno = sv_errno;
		return (-1);
	}

	if (!_t_is_ok(fd, tiptr, T_CONN_REQ)) {
		int sv_errno = errno;
		_t_blocksigpoll(&mask, SIG_SETMASK);
		trace2(TR__snd_conn_req, 1, fd);
		errno = sv_errno;
		return (-1);
	}

	_t_blocksigpoll(&mask, SIG_SETMASK);
	trace2(TR__snd_conn_req, 1, fd);
	return (0);
}



/*
 * Rcv_conn_con - get connection confirmation off
 * of read queue
 */
int
_rcv_conn_con(tiptr, call)
register struct _ti_user *tiptr;
register struct t_call *call;
{
	struct strbuf ctlbuf;
	struct strbuf rcvbuf;
	int flg = 0;
	register union T_primitives *pptr;
	int retval, fd;

	trace2(TR__rcv_conn_con, 0, fd);

	fd = tiptr->ti_fd;

	if (tiptr->ti_servtype == T_CLTS) {
		t_errno = TNOTSUPPORT;
		trace2(TR__rcv_conn_con, 1, fd);
		return (-1);
	}

	/*
	 * see if there is something in look buffer
	 */
	if (tiptr->ti_lookflg) {
		t_errno = TLOOK;
		trace2(TR__rcv_conn_con, 1, fd);
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
		trace2(TR__rcv_conn_con, 1, fd);
		errno = sv_errno;
		return (-1);
	}
	if (rcvbuf.len == -1) rcvbuf.len = 0;


	/*
	 * did we get entire message
	 */
	if (retval) {
		t_errno = TSYSERR;
		trace2(TR__rcv_conn_con, 1, fd);
		errno = EIO;
		return (-1);
	}

	/*
	 * is cntl part large enough to determine message type?
	 */
	if (ctlbuf.len < sizeof (long)) {
		t_errno = TSYSERR;
		trace2(TR__rcv_conn_con, 1, fd);
		errno = EPROTO;
		return (-1);
	}

	pptr = (union T_primitives *)ctlbuf.buf;

	switch (pptr->type) {

		case T_CONN_CON:

			if ((ctlbuf.len < sizeof (struct T_conn_con)) ||
			    (pptr->conn_con.OPT_length != 0 &&
			    (ctlbuf.len < (pptr->conn_con.OPT_length +
				pptr->conn_con.OPT_offset)))) {
				t_errno = TSYSERR;
				trace2(TR__rcv_conn_con, 1, fd);
				errno = EPROTO;
				return (-1);
			}

			if (call != NULL) {
				if ((rcvbuf.len > call->udata.maxlen) ||
				    (pptr->conn_con.RES_length >
					call->addr.maxlen) ||
				    (pptr->conn_con.OPT_length >
					call->opt.maxlen)) {
					t_errno = TBUFOVFLW;
					trace2(TR__rcv_conn_con, 1, fd);
					return (-1);
				}
				memcpy(call->addr.buf, ctlbuf.buf +
					pptr->conn_con.RES_offset,
					(int)pptr->conn_con.RES_length);
				call->addr.len = pptr->conn_con.RES_length;
				memcpy(call->opt.buf, ctlbuf.buf +
					pptr->conn_con.OPT_offset,
					(int)pptr->conn_con.OPT_length);
				call->opt.len = pptr->conn_con.OPT_length;
				memcpy(call->udata.buf, rcvbuf.buf,
					(int)rcvbuf.len);
				call->udata.len = rcvbuf.len;
				/*
				 * since a confirmation seq number
				 * is -1 by default
				 */
				call->sequence = (long) -1;
			}

			trace2(TR__rcv_conn_con, 1, fd);
			return (0);

		case T_DISCON_IND:

			/*
			 * if disconnect indication then put it in look buf
			 */
			_t_putback(tiptr, rcvbuf.buf, rcvbuf.len,
					ctlbuf.buf, ctlbuf.len);
			t_errno = TLOOK;
			trace2(TR__rcv_conn_con, 1, fd);
			return (-1);

		default:
			break;
	}

	t_errno = TSYSERR;
	trace2(TR__rcv_conn_con, 1, fd);
	errno = EPROTO;
	return (-1);
}
