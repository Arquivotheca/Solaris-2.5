/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T */
/*	  All Rights Reserved   */

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)send.c	1.11	94/02/02 SMI"   /* SVr4.0 1.5	*/

/*
 * +++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 *		PROPRIETARY NOTICE (Combined)
 *
 * This source code is unpublished proprietary information
 * constituting, or derived under license from AT&T's UNIX(r) System V.
 * In addition, portions of such source code were derived from Berkeley
 * 4.3 BSD under license from the Regents of the University of
 * California.
 *
 *
 *
 *		Copyright Notice
 *
 * Notice of copyright on this source code product does not indicate
 * publication.
 *
 *	(c) 1986,1987,1988.1989  Sun Microsystems, Inc
 *	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *		  All rights reserved.
 *
 */

#include "sockmt.h"
#include <sys/param.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/stream.h>
#include <sys/ioctl.h>
#include <stropts.h>
#include <sys/tihdr.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <tiuser.h>
#include <sys/sockmod.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "sock.h"

#pragma weak sendmsg = _sendmsg
#pragma weak sendto = _sendto
#pragma weak send = _send


int
_sendmsg(s, msg, flags)
	register int			s;
	register struct msghdr		*msg;
	register int			flags;
{
	register struct _si_user	*siptr;
	int				len;
	char				*buf;
	int				retval;
	sigset_t			mask;

	if ((siptr = _s_checkfd(s)) == NULL)
		return (-1);

	if (msg->msg_iovlen > 1) {
		/*
		 * Have to make one buffer
		 */
		int i, pos;
		sigset_t procmask;

		for (i = 0, len = 0; i < msg->msg_iovlen; i++)
			len += msg->msg_iov[i].iov_len;
		/*
		 * block all signals while allocating memory
		 */
		_s_blockallsignals(&procmask);
		if ((buf = malloc(len)) == NULL) {
			_s_restoresigmask(&procmask);
			errno = ENOMEM;
			return (-1);
		}
		_s_restoresigmask(&procmask);

		for (i = 0, pos = 0; i < msg->msg_iovlen; i++) {
			(void) memcpy(&buf[pos], msg->msg_iov[i].iov_base,
					msg->msg_iov[i].iov_len);
			pos += msg->msg_iov[i].iov_len;
		}
	} else {
		buf = msg->msg_iov[0].iov_base;
		len = msg->msg_iov[0].iov_len;
	}

	MUTEX_LOCK_SIGMASK(&siptr->lock, mask);
	retval = _s_sosendxx(siptr, flags, buf, len,
				(struct sockaddr *)msg->msg_name,
				msg->msg_namelen,
				msg->msg_accrights, msg->msg_accrightslen,
				&mask);
	MUTEX_UNLOCK_SIGMASK(&siptr->lock, mask);
	if (msg->msg_iovlen > 1) {
		/*
		 * block all signals while freeing memory
		 */
		sigset_t procmask;

		_s_blockallsignals(&procmask);
		free(buf);
		_s_restoresigmask(&procmask);
	}
	return (retval);
}

int
_sendto(s, buf, len, flags, to, tolen)
	register int			s;
	register char			*buf;
	register int			len;
	register int			flags;
	register struct sockaddr	*to;
	register int			tolen;
{
	register struct _si_user	*siptr;
	sigset_t			mask;
	int				retval;

	if ((siptr = _s_checkfd(s)) == NULL)
		return (-1);

	MUTEX_LOCK_SIGMASK(&siptr->lock, mask);
	retval = _s_sosendxx(siptr, flags, buf, len, to, tolen, NULL, 0,
				&mask);
	MUTEX_UNLOCK_SIGMASK(&siptr->lock, mask);
	return (retval);
}

int
_send(s, buf, len, flags)
	register int			s;
	register char			*buf;
	register int			len;
	register int			flags;
{
	register struct _si_user	*siptr;
	sigset_t			mask;
	int				retval;

	if ((siptr = _s_checkfd(s)) == NULL)
		return (-1);

	MUTEX_LOCK_SIGMASK(&siptr->lock, mask);
	retval = _s_sosendxx(siptr, flags, buf, len, NULL, 0, NULL, 0, &mask);
	MUTEX_UNLOCK_SIGMASK(&siptr->lock, mask);
	return (retval);
}
