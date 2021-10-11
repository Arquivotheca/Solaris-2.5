/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)t_getinfo.c	1.17	94/03/02 SMI"	/* SVr4.0 1.5	*/

#include "timt.h"
#include <errno.h>
#include <sys/types.h>
#include <rpc/trace.h>
#include <sys/stream.h>
#include <sys/stropts.h>
#include <sys/tihdr.h>
#include <sys/timod.h>
#include <tiuser.h>
#include <sys/signal.h>
#include "tli.h"


t_getinfo(fd, info)
int fd;
register struct t_info *info;
{
	struct T_info_ack inforeq;
	int retlen;
	sigset_t mask, t_mask;
	register struct _ti_user *tiptr;

	trace2(TR_t_getinfo, 0, fd);
	if ((tiptr = _t_checkfd(fd)) == 0) {
		int sv_errno = errno;
		trace2(TR_t_getinfo, 1, fd);
		errno = sv_errno;
		return (-1);
	}
	MUTEX_LOCK_SIGMASK(&tiptr->ti_lock, mask);
	_t_blocksigpoll(&t_mask, SIG_BLOCK);

	inforeq.PRIM_type = T_INFO_REQ;

	if (!_t_do_ioctl(fd, (caddr_t)&inforeq, sizeof (struct T_info_req),
			TI_GETINFO, &retlen)) {
		int sv_errno = errno;
		_t_blocksigpoll(&t_mask, SIG_SETMASK);
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace2(TR_t_getinfo, 1, fd);
		errno = sv_errno;
		return (-1);
	}
	_t_blocksigpoll(&t_mask, SIG_SETMASK);

	if (retlen != sizeof (struct T_info_ack)) {
		t_errno = TSYSERR;
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace2(TR_t_getinfo, 1, fd);
		errno = EIO;
		return (-1);
	}

	info->addr = inforeq.ADDR_size;
	info->options = inforeq.OPT_size;
	info->tsdu = inforeq.TSDU_size;
	info->etsdu = inforeq.ETSDU_size;
	info->connect = inforeq.CDATA_size;
	info->discon = inforeq.DDATA_size;
	info->servtype = inforeq.SERV_type;

	MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
	trace2(TR_t_getinfo, 1, fd);
	return (0);
}
