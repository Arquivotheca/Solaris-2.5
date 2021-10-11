/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)t_sndrel.c	1.16	94/03/02 SMI"	/* SVr4.0 1.4	*/

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


t_sndrel(fd)
int fd;
{
	struct T_ordrel_req orreq;
	struct strbuf ctlbuf;
	register struct _ti_user *tiptr;
	sigset_t mask;


	trace2(TR_t_sndrel, 0, fd);
	if ((tiptr = _t_checkfd(fd)) == NULL) {
		int sv_errno = errno;
		trace2(TR_t_sndrel, 1, fd);
		errno = sv_errno;
		return (-1);
	}
	MUTEX_LOCK_SIGMASK(&tiptr->ti_lock, mask);

	if (tiptr->ti_servtype != T_COTS_ORD) {
		t_errno = TNOTSUPPORT;
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace2(TR_t_sndrel, 1, fd);
		return (-1);
	}

	orreq.PRIM_type = T_ORDREL_REQ;
	ctlbuf.maxlen = sizeof (struct T_ordrel_req);
	ctlbuf.len = sizeof (struct T_ordrel_req);
	ctlbuf.buf = (caddr_t)&orreq;

	if (putmsg(fd, &ctlbuf, NULL, 0) < 0) {
		int sv_errno = errno;

		if (errno == EAGAIN)
			t_errno = TFLOW;
		else
			t_errno = TSYSERR;
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace2(TR_t_sndrel, 1, fd);
		errno = sv_errno;
		return (-1);
	}

	tiptr->ti_state = TLI_NEXTSTATE(T_SNDREL, tiptr->ti_state);
	MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
	trace2(TR_t_sndrel, 1, fd);
	return (0);
}
