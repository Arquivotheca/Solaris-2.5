/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)t_rcvuderr.c	1.17	94/03/02 SMI"	/* SVr4.0 1.5	*/

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


t_rcvuderr(fd, uderr)
int fd;
struct t_uderr *uderr;
{
	struct strbuf ctlbuf, rcvbuf;
	int flg;
	int retval;
	register union T_primitives *pptr;
	register struct _ti_user *tiptr;
	sigset_t mask;

	trace2(TR_t_rcvuderr, 0, fd);
	if ((tiptr = _t_checkfd(fd)) == NULL) {
		int sv_errno = errno;
		trace2(TR_t_rcvuderr, 1, fd);
		errno = sv_errno;
		return (-1);
	}
	MUTEX_LOCK_SIGMASK(&tiptr->ti_lock, mask);

	if (tiptr->ti_servtype != T_CLTS) {
		t_errno = TNOTSUPPORT;
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace2(TR_t_rcvuderr, 1, fd);
		return (-1);
	}
	/*
	 * is there an error indication in look buffer
	 */
	if (tiptr->ti_lookflg) {
		ctlbuf.maxlen = tiptr->ti_lookcsize;
		ctlbuf.len = tiptr->ti_lookcsize;
		ctlbuf.buf = tiptr->ti_lookcbuf;
		rcvbuf.maxlen = 0;
		rcvbuf.len = 0;
		rcvbuf.buf = NULL;
	} else {
		if ((retval = _t_look_locked(fd, tiptr)) < 0) {
			int sv_errno = errno;
			MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
			trace2(TR_t_rcvuderr, 1, fd);
			errno = sv_errno;
			return (-1);
		}
		if (retval != T_UDERR) {
			int sv_errno = errno;
			t_errno = TNOUDERR;
			MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
			trace2(TR_t_rcvuderr, 1, fd);
			errno = sv_errno;
			return (-1);
		}

		ctlbuf.maxlen = tiptr->ti_ctlsize;
		ctlbuf.len = 0;
		ctlbuf.buf = tiptr->ti_ctlbuf;
		rcvbuf.maxlen = 0;
		rcvbuf.len = 0;
		rcvbuf.buf = NULL;

		flg = 0;

		if ((retval = getmsg(fd, &ctlbuf, &rcvbuf, &flg)) < 0) {
			int sv_errno = errno;

			t_errno = TSYSERR;
			MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
			trace2(TR_t_rcvuderr, 1, fd);
			errno = sv_errno;
			return (-1);
		}
		/*
		 * did I get entire message?
		 */
		if (retval) {
			t_errno = TSYSERR;
			MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
			trace2(TR_t_rcvuderr, 1, fd);
			errno = EIO;
			return (-1);
		}

	}

	tiptr->ti_lookflg = 0;

	pptr = (union T_primitives *)ctlbuf.buf;

	if ((ctlbuf.len < sizeof (struct T_uderror_ind)) ||
	    (pptr->type != T_UDERROR_IND)) {
		t_errno = TSYSERR;
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace2(TR_t_rcvuderr, 1, fd);
		errno = EPROTO;
		return (-1);
	}

	if (uderr) {
		if ((uderr->addr.maxlen < pptr->uderror_ind.DEST_length) ||
		    (uderr->opt.maxlen < pptr->uderror_ind.OPT_length)) {
			t_errno = TBUFOVFLW;
			MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
			trace2(TR_t_rcvuderr, 1, fd);
			return (-1);
		}

		uderr->error = pptr->uderror_ind.ERROR_type;
		memcpy(uderr->addr.buf, ctlbuf.buf +
			pptr->uderror_ind.DEST_offset,
			(int)pptr->uderror_ind.DEST_length);
		uderr->addr.len = (unsigned int)pptr->uderror_ind.DEST_length;
		memcpy(uderr->opt.buf, ctlbuf.buf +
			pptr->uderror_ind.OPT_offset,
			(int)pptr->uderror_ind.OPT_length);
		uderr->opt.len = (unsigned int)pptr->uderror_ind.OPT_length;
	}

	tiptr->ti_state = TLI_NEXTSTATE(T_RCVUDERR, tiptr->ti_state);
#ifdef DEBUG
	if (tiptr->ti_state == nvs)
		syslog(LOG_ERR,
			"t_rcvuderr: invalid state event T_RCVUDERR");
#endif DEBUG
	MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
	trace2(TR_t_rcvuderr, 1, fd);
	return (0);
}
