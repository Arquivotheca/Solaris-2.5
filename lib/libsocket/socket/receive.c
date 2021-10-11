/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T */
/*	  All Rights Reserved   */

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)receive.c	1.12	94/02/02 SMI"   /* SVr4.0 1.5	*/

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
#include <sys/uio.h>
#include <sys/socket.h>
#include <tiuser.h>
#include <sys/sockmod.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "sock.h"

#pragma weak recvmsg = _recvmsg
#pragma weak recvfrom = _recvfrom
#pragma weak recv = _recv

int
_recvmsg(s, msg, flags)
	register int			s;
	register struct msghdr		*msg;
	register int			flags;
{
	register struct _si_user	*siptr;
	char				*buf;
	int				len;
	int				retval;
	sigset_t			mask;

	if (msg->msg_iovlen > 1) {
		int i;
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
	} else {
		buf = msg->msg_iov[0].iov_base;
		len = msg->msg_iov[0].iov_len;
	}

	if ((siptr = _s_checkfd(s)) == NULL)
		return (-1);

	MUTEX_LOCK_SIGMASK(&siptr->lock, mask);
	retval = _s_soreceivexx(siptr, flags, buf, len,
				(struct sockaddr *)msg->msg_name,
				&msg->msg_namelen, msg->msg_accrights,
				&msg->msg_accrightslen, &mask);
	MUTEX_UNLOCK_SIGMASK(&siptr->lock, mask);
	if (msg->msg_iovlen > 1) {
		sigset_t	procmask;

		if (retval > 0) {
			/*
			 * Copy it all back as per the users
			 * request.
			 */
			register int count, i, len, pos;

			for (i = pos = 0, len = retval; i < msg->msg_iovlen;
			    i++) {
				count = _s_min(msg->msg_iov[i].iov_len, len);
				(void) memcpy(msg->msg_iov[i].iov_base,
						&buf[pos], count);
				pos += count;
				len -= count;
				if (len == 0)
					break;
			}
		}
		/*
		 * block all signals while freeing memory
		 */
		_s_blockallsignals(&procmask);
		free(buf);
		_s_restoresigmask(&procmask);
	}
	return (retval);
}

int
_recvfrom(s, buf, len, flags, from, fromlen)
	register int			s;
	register char			*buf;
	register u_long			len;
	register int			flags;
	register struct sockaddr	*from;
	register int			*fromlen;
{
	register struct _si_user	*siptr;
	sigset_t			mask;
	int				retval;

	if ((siptr = _s_checkfd(s)) == NULL)
		return (-1);

	MUTEX_LOCK_SIGMASK(&siptr->lock, mask);
	retval = _s_soreceivexx(siptr, flags, buf, len, from, fromlen,
					NULL, NULL, &mask);
	MUTEX_UNLOCK_SIGMASK(&siptr->lock, mask);
	return (retval);
}

int
_recv(s, buf, len, flags)
	register int			s;
	register char			*buf;
	register u_long			len;
	register int			flags;
{
	register struct _si_user	*siptr;
	sigset_t			mask;
	int				retval;

	if ((siptr = _s_checkfd(s)) == NULL)
		return (-1);

	MUTEX_LOCK_SIGMASK(&siptr->lock, mask);
	retval = _s_soreceivexx(siptr, flags, buf, len, NULL, NULL,
				NULL, NULL, &mask);
	MUTEX_UNLOCK_SIGMASK(&siptr->lock, mask);
	return (retval);
}
