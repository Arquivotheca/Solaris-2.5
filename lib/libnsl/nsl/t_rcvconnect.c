/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)t_rcvconnect.c	1.15	94/03/02 SMI"	/* SVr4.0 1.3	*/

#include "timt.h"
#include <sys/param.h>
#include <sys/types.h>
#include <errno.h>
#include <rpc/trace.h>
#include <tiuser.h>
#include <sys/timod.h>
#include "tli.h"


t_rcvconnect(fd, call)
int fd;
struct t_call *call;
{
	register struct _ti_user *tiptr;
	int retval, sv_errno;
	sigset_t mask;

	trace2(TR_t_rcvconnect, 0, fd);
	if ((tiptr = _t_checkfd(fd)) == NULL) {
		int sv_errno = errno;
		trace2(TR_t_rcvconnect, 1, fd);
		errno = sv_errno;
		return (-1);
	}
	MUTEX_LOCK_SIGMASK(&tiptr->ti_lock, mask);

	if (((retval = _rcv_conn_con(tiptr, call)) == 0) ||
		(t_errno == TBUFOVFLW))
		tiptr->ti_state = TLI_NEXTSTATE(T_RCVCONNECT, tiptr->ti_state);
	sv_errno = errno;
	MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
	trace2(TR_t_rcvconnect, 1, fd);
	errno = sv_errno;
	return (retval);
}
