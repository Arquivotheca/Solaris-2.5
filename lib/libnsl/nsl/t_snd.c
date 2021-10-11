/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)t_snd.c	1.19	94/03/02 SMI"	/* SVr4.0 1.3.1.2	*/

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



t_snd(fd, buf, nbytes, flags)
int fd;
register char *buf;
unsigned nbytes;
int flags;
{
	struct strbuf ctlbuf, databuf;
	struct T_data_req *datareq;
	unsigned tmpcnt, tmp;
	char *tmpbuf;
	register struct _ti_user *tiptr;
	int band;
	int ret;
	sigset_t mask;

	trace4(TR_t_snd, 0, fd, nbytes, flags);
	if ((tiptr = _t_checkfd(fd)) == NULL) {
		int sv_errno = errno;
		trace4(TR_t_snd, 1, fd, nbytes, flags);
		errno = sv_errno;
		return (-1);
	}
	MUTEX_LOCK_SIGMASK(&tiptr->ti_lock, mask);

	if (tiptr->ti_servtype == T_CLTS) {
		t_errno = TNOTSUPPORT;
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace4(TR_t_snd, 1, fd, nbytes, flags);
		return (-1);
	}
	if (!(tiptr->ti_flags & SENDZERO) && nbytes == 0) {
		t_errno = TBADDATA;
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace4(TR_t_snd, 1, fd, nbytes, flags);
		return (-1);
	}

	datareq = (struct T_data_req *)tiptr->ti_ctlbuf;
	if (flags&T_EXPEDITED) {
		datareq->PRIM_type = T_EXDATA_REQ;
		band = TI_EXPEDITED;
	} else {
		datareq->PRIM_type = T_DATA_REQ;
		band = TI_NORMAL;
	}


	ctlbuf.maxlen = sizeof (struct T_data_req);
	ctlbuf.len = sizeof (struct T_data_req);
	ctlbuf.buf = tiptr->ti_ctlbuf;
	tmp = nbytes;
	tmpbuf = buf;

	do {
		tmpcnt = tmp;
		if ((tiptr->ti_tsdusize != 0) || (flags & T_EXPEDITED)) {
			/*
			 * transport provider supports TSDU concept
			 * (unlike TCP) or it is expedited data.
			 * In this case do the fragmentation
			 */
			if (tmpcnt > tiptr->ti_maxpsz) {
				datareq->MORE_flag = 1;
				tmpcnt = tiptr->ti_maxpsz;
			} else {
				if (flags&T_MORE)
					datareq->MORE_flag = 1;
				else
					datareq->MORE_flag = 0;
			}
			databuf.maxlen = tmpcnt;
			databuf.len = tmpcnt;
			databuf.buf = tmpbuf;
			ret = putpmsg(fd, &ctlbuf, &databuf, band, MSG_BAND);
		} else {
			/*
			 * transport provider does *not* support TSDU concept
			 * (e.g. TCP) and it is not expedited data. A
			 * perf. optimization is used. Note: the T_MORE
			 * flag is ignored here even if set by the user.
			 */
			ret = write(fd, tmpbuf, tmpcnt);
			if (ret != tmpcnt && ret >= 0) {
				/* Amount that was actually sent */
				tmpcnt = ret;
			}
		}

		if (ret < 0) {
			int sv_errno = errno;
			if (nbytes == tmp) {
				if (errno == EAGAIN)
					t_errno = TFLOW;
				else
					t_errno = TSYSERR;
				MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
				trace4(TR_t_snd, 1, fd, nbytes, flags);
				errno = sv_errno;
				return (-1);
			} else {
				tiptr->ti_state =
					TLI_NEXTSTATE(T_SND, tiptr->ti_state);
#ifdef DEBUG
				if (tiptr->ti_state == nvs)
					syslog(LOG_ERR,
					"t_snd: invalid state event T_SND");
#endif DEBUG
				MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
				trace4(TR_t_snd, 1, fd, nbytes, flags);
				errno = sv_errno;
				return (nbytes - tmp);
			}
		}
		tmp = tmp - tmpcnt;
		tmpbuf = tmpbuf + tmpcnt;
	} while (tmp);

	tiptr->ti_state = TLI_NEXTSTATE(T_SND, tiptr->ti_state);
#ifdef DEBUG
	if (tiptr->ti_state == nvs)
		syslog(LOG_ERR,
			"t_snd: invalid state event T_SND");
#endif DEBUG
	MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
	trace4(TR_t_snd, 1, fd, nbytes, flags);
	return (nbytes - tmp);
}
