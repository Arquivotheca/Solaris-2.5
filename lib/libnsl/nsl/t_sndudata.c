/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)t_sndudata.c	1.16	94/03/02 SMI"	/* SVr4.0 1.5	*/

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


t_sndudata(fd, unitdata)
int fd;
register struct t_unitdata *unitdata;
{
	register struct T_unitdata_req *udreq;
	char *buf;
	struct strbuf ctlbuf;
	int size;
	register struct _ti_user *tiptr;
	sigset_t mask;

	trace2(TR_t_sndudata, 0, fd);
	if ((tiptr = _t_checkfd(fd)) == NULL) {
		int sv_errno = errno;
		trace2(TR_t_sndudata, 1, fd);
		errno = sv_errno;
		return (-1);
	}
	MUTEX_LOCK_SIGMASK(&tiptr->ti_lock, mask);

	if (tiptr->ti_servtype != T_CLTS) {
		t_errno = TNOTSUPPORT;
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace2(TR_t_sndudata, 1, fd);
		return (-1);
	}

	if (!(tiptr->ti_flags & SENDZERO) && (int)unitdata->udata.len == 0) {
		t_errno = TBADDATA;
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace2(TR_t_sndudata, 1, fd);
		return (-1);
	}
	if ((int)unitdata->udata.len > tiptr->ti_maxpsz) {
		t_errno = TSYSERR;
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace2(TR_t_sndudata, 1, fd);
		errno = EPROTO;
		return (-1);
	}

	buf = tiptr->ti_ctlbuf;
	udreq = (struct T_unitdata_req *)buf;
	udreq->PRIM_type = T_UNITDATA_REQ;
	udreq->DEST_length = unitdata->addr.len;
	udreq->DEST_offset = 0;
	udreq->OPT_length = unitdata->opt.len;
	udreq->OPT_offset = 0;
	size = sizeof (struct T_unitdata_req);

	if (unitdata->addr.len) {
		_t_aligned_copy(buf, unitdata->addr.len, size,
			unitdata->addr.buf, &udreq->DEST_offset);
		size = udreq->DEST_offset + udreq->DEST_length;
	}
	if (unitdata->opt.len) {
		_t_aligned_copy(buf, unitdata->opt.len, size,
			unitdata->opt.buf, &udreq->OPT_offset);
		size = udreq->OPT_offset + udreq->OPT_length;
	}

	if (size > tiptr->ti_ctlsize) {
		t_errno = TSYSERR;
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace2(TR_t_sndudata, 1, fd);
		errno = EIO;
		return (-1);
	}
	ctlbuf.maxlen = tiptr->ti_ctlsize;
	ctlbuf.len = size;
	ctlbuf.buf = buf;

	if (putmsg(fd, &ctlbuf,
			(unitdata->udata.len? &unitdata->udata: NULL), 0) < 0) {
		int sv_errno = errno;

		if (errno == EAGAIN)
			t_errno = TFLOW;
		else
			t_errno = TSYSERR;
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace2(TR_t_sndudata, 1, fd);
		errno = sv_errno;
		return (-1);
	}
	tiptr->ti_state = TLI_NEXTSTATE(T_SNDUDATA, tiptr->ti_state);
#ifdef DEBUG
	if (tiptr->ti_state == nvs)
		syslog(LOG_ERR,
			"t_sndudata: invalid state event T_SNDUDATA");
#endif DEBUG
	MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
	trace2(TR_t_sndudata, 1, fd);
	return (0);
}
