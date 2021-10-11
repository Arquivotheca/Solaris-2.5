/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)t_look.c	1.17	94/03/02 SMI"	/* SVr4.0 1.2	*/

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
#include "tli.h"


t_look(fd)
int fd;
{
	int state;
	int sv_errno;
	register struct _ti_user *tiptr;
	sigset_t mask;

	trace2(TR_t_look, 0, fd);
	if ((tiptr = _t_checkfd(fd)) == NULL) {
		int sv_errno = errno;
		trace2(TR__t_look_locked, 1, fd);
		errno = sv_errno;
		return (-1);
	}
	MUTEX_LOCK_SIGMASK(&tiptr->ti_lock, mask);

	state = _t_look_locked(fd, tiptr);
	sv_errno = errno;

	MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
	trace2(TR_t_look, 1, fd);
	errno = sv_errno;
	return (state);
}


/*
 * _t_look_locked() assumes tiptr->ti_lock lock is already held and signals
 * already blocked.  Intended for use by other TLI routines only.
 */
int
_t_look_locked(fd, tiptr)
int fd;
register struct _ti_user *tiptr;
{
	struct strpeek strpeek;
	int retval;
	union T_primitives *pptr;
	long type;

	trace2(TR__t_look_locked, 0, fd);

	strpeek.ctlbuf.maxlen = sizeof (long);
	strpeek.ctlbuf.len = 0;
	strpeek.ctlbuf.buf = tiptr->ti_ctlbuf;
	strpeek.databuf.maxlen = 0;
	strpeek.databuf.len = 0;
	strpeek.databuf.buf = NULL;
	strpeek.flags = 0;

	if ((retval = ioctl(fd, I_PEEK, &strpeek)) < 0) {
		int sv_errno = errno;
		trace2(TR__t_look_locked, 1, fd);
		errno = sv_errno;
		return (T_ERROR);
	}

	/*
	 * if something there and cnt part also there
	 */
	if (tiptr->ti_lookflg ||
	    (retval && (strpeek.ctlbuf.len >= sizeof (long)))) {
		pptr = (union T_primitives *)strpeek.ctlbuf.buf;
		if (tiptr->ti_lookflg) {
			if (((type = *((long *)tiptr->ti_lookcbuf))
				!= T_DISCON_IND) &&
			    (retval && (pptr->type == T_DISCON_IND))) {
				type = pptr->type;
				tiptr->ti_lookflg = 0;
			}
		} else
			type = pptr->type;

		switch (type) {

		case T_CONN_IND:
			trace2(TR__t_look_locked, 1, fd);
			return (T_LISTEN);

		case T_CONN_CON:
			trace2(TR__t_look_locked, 1, fd);
			return (T_CONNECT);

		case T_DISCON_IND:
			trace2(TR__t_look_locked, 1, fd);
			return (T_DISCONNECT);

		case T_DATA_IND:
		case T_UNITDATA_IND:
			trace2(TR__t_look_locked, 1, fd);
			return (T_DATA);

		case T_EXDATA_IND:
			trace2(TR__t_look_locked, 1, fd);
			return (T_EXDATA);

		case T_UDERROR_IND:
			trace2(TR__t_look_locked, 1, fd);
			return (T_UDERR);

		case T_ORDREL_IND:
			trace2(TR__t_look_locked, 1, fd);
			return (T_ORDREL);

		default:
			t_errno = TSYSERR;
			trace2(TR__t_look_locked, 1, fd);
			errno = EPROTO;
			return (-1);
		}
	}

	/*
	 * if something there put no control part
	 * it must be data
	 */
	if (retval && (strpeek.ctlbuf.len <= 0)) {
		trace2(TR__t_look_locked, 1, fd);
		return (T_DATA);
	}

	/*
	 * if msg there and control
	 * part not large enough to determine type?
	 * it must be illegal TLI message
	 */
	if (retval && (strpeek.ctlbuf.len > 0)) {
		t_errno = TSYSERR;
		trace2(TR__t_look_locked, 1, fd);
		errno = EPROTO;
		return (-1);
	}
	trace2(TR__t_look_locked, 1, fd);
	return (0);
}
