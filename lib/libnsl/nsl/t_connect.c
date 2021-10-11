/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)t_connect.c	1.19	95/05/10 SMI"	/* SVr4.0 1.7	*/

#include "timt.h"
#include <sys/param.h>
#include <sys/types.h>
#include <rpc/trace.h>
#include <sys/stropts.h>
#include <sys/timod.h>
#include <tiuser.h>
#include <fcntl.h>
#include <sys/signal.h>
#include <errno.h>
#include <syslog.h>
#include "tli.h"

extern struct _ti_user *_t_checkfd();
extern void _t_blockallsignals(sigset_t *maskp);
extern void _t_restoresigmask(sigset_t *maskp);

/*
 * If a system call fails with EINTR after T_CONN_REQ is sent out,
 * we change state for caller to continue with t_rcvconnect(). This
 * semantics is not documented for TLI but is the direction taken with
 * XTI so we adopt it. With this the call establishment is completed
 * by calling t_rcvconnect() even for synchronous endpoints.
 */
t_connect(fd, sndcall, rcvcall)
int fd;
struct t_call *sndcall;
struct t_call *rcvcall;
{
	int fctlflg;
	register struct _ti_user *tiptr;
	sigset_t mask, procmask;

	trace2(TR_t_connect, 0, fd);
	if ((tiptr = _t_checkfd(fd)) == NULL) {
		int sv_errno = errno;
		trace2(TR_t_connect, 1, fd);
		errno = sv_errno;
		return (-1);
	}
	MUTEX_LOCK_SIGMASK(&tiptr->ti_lock, mask);

	_t_blockallsignals(&procmask);
	if (_snd_conn_req(tiptr, sndcall) < 0) {
		int sv_errno = errno;
		_t_restoresigmask(&procmask);
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace2(TR_t_connect, 1, fd);
		errno = sv_errno;
		return (-1);
	}

	_t_restoresigmask(&procmask);
	if ((fctlflg = fcntl(fd, F_GETFL, 0)) < 0) {
		int sv_errno = errno;
		if (errno == EINTR) {
			tiptr->ti_state = TLI_NEXTSTATE(T_CONNECT2,
							tiptr->ti_state);
		}
		t_errno = TSYSERR;
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace2(TR_t_connect, 1, fd);
		errno = sv_errno;
		return (-1);
	}

	if (fctlflg & (O_NDELAY | O_NONBLOCK)) {
		tiptr->ti_state = TLI_NEXTSTATE(T_CONNECT2, tiptr->ti_state);
#ifdef DEBUG
		if (tiptr->ti_state == nvs)
			syslog(LOG_ERR,
				"t_connect: invalid state event T_CONNECT2");
#endif DEBUG
		t_errno = TNODATA;
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace2(TR_t_connect, 1, fd);
		return (-1);
	}

	if (_rcv_conn_con(tiptr, rcvcall) < 0) {
		int sv_errno = errno;
		if ((t_errno == TSYSERR && errno == EINTR) ||
		    t_errno == TLOOK) {
			tiptr->ti_state = TLI_NEXTSTATE(T_CONNECT2,
							tiptr->ti_state);
#ifdef DEBUG
		if (tiptr->ti_state == nvs)
			syslog(LOG_ERR,
				"t_connect: invalid state event T_CONNECT2");
#endif DEBUG
		} else if (t_errno == TBUFOVFLW) {
			tiptr->ti_state = TLI_NEXTSTATE(T_CONNECT1,
							tiptr->ti_state);
#ifdef DEBUG
		if (tiptr->ti_state == nvs)
			syslog(LOG_ERR,
				"t_connect: invalid state event T_CONNECT1");
#endif DEBUG
		}
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace2(TR_t_connect, 1, fd);
		errno = sv_errno;
		return (-1);
	}
	tiptr->ti_state = TLI_NEXTSTATE(T_CONNECT1, tiptr->ti_state);
#ifdef DEBUG
		if (tiptr->ti_state == nvs)
			syslog(LOG_ERR,
				"t_connect: invalid state event T_CONNECT1");
#endif DEBUG
	MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
	trace2(TR_t_connect, 1, fd);
	return (0);
}
