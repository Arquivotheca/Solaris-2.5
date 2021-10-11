/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T */
/*	  All Rights Reserved   */

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)socketpair.c	1.15	94/04/08 SMI"   /* SVr4.0 1.11  */

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
#include <sys/time.h>
#include <errno.h>
#include <sys/stream.h>
#include <sys/ioctl.h>
#include <stropts.h>
#include <sys/tihdr.h>
#include <sys/socket.h>
#include <tiuser.h>
#include <sys/sockmod.h>
#include <fcntl.h>
#include <sys/poll.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "sock.h"

#pragma weak socketpair = _socketpair

int
_socketpair(family, type, protocol, sv)
	register int			family;
	register int			type;
	register int			protocol;
	register int			sv[2];

{
	register struct _si_user	*siptr;
	register struct _si_user	*nsiptr;
	struct bind_ux			bind_ux;
	struct bind_ux			nbind_ux;
	struct t_call			sndcall;
	int				size;
	sigset_t			mask;
	int				saved_errno;


	if (family != AF_UNIX) {
		errno = EPROTONOSUPPORT;
		return (-1);
	}

	/*
	 * Create endpoints ns and s
	 */
	nsiptr = _s_socreate(family, type, protocol);
	siptr = _s_socreate(family, type, protocol);

	if (nsiptr == NULL || siptr == NULL)
		return (-1);

	(void) memset((caddr_t)&nbind_ux, 0, sizeof (nbind_ux));
	(void) memset((caddr_t)&bind_ux, 0, sizeof (bind_ux));
	(void) memset((caddr_t)&sndcall, 0, sizeof (sndcall));

	/*
	 * Bind each end.
	 */
	MUTEX_LOCK_SIGMASK(&nsiptr->lock, mask);
	size = sizeof (nbind_ux);
	if (__bind(nsiptr, NULL, 0, (caddr_t)&nbind_ux, &size) < 0) {
		MUTEX_UNLOCK_SIGMASK(&nsiptr->lock, mask);
		goto bad;
	}
	MUTEX_UNLOCK_SIGMASK(&nsiptr->lock, mask);
	if (size != sizeof (nbind_ux)) {
		errno = EPROTO;
		goto bad;
	}

	MUTEX_LOCK_SIGMASK(&siptr->lock, mask);
	if (__bind(siptr, NULL, 0, (caddr_t)&bind_ux, &size) < 0) {
		MUTEX_UNLOCK_SIGMASK(&siptr->lock, mask);
		goto bad;
	}
	MUTEX_UNLOCK_SIGMASK(&siptr->lock, mask);
	if (size != sizeof (nbind_ux)) {
		errno = EPROTO;
		goto bad;
	}

	if (type == SOCK_DGRAM) {
		/*
		 * connect s->ns
		 */
		sndcall.addr.buf = (caddr_t)&nbind_ux;
		sndcall.addr.len = sizeof (nbind_ux);
		MUTEX_LOCK_SIGMASK(&siptr->lock, mask);
		if (_connect2(siptr, &sndcall, &mask) < 0) {
			MUTEX_UNLOCK_SIGMASK(&siptr->lock, mask);
			goto bad;
		}
		MUTEX_UNLOCK_SIGMASK(&siptr->lock, mask);

		/*
		 * connect ns->s
		 */
		sndcall.addr.buf = (caddr_t)&bind_ux;
		sndcall.addr.len = sizeof (bind_ux);
		MUTEX_LOCK_SIGMASK(&nsiptr->lock, mask);
		if (_connect2(nsiptr, &sndcall, &mask) < 0) {
			MUTEX_UNLOCK_SIGMASK(&nsiptr->lock, mask);
			goto bad;
		}
		MUTEX_UNLOCK_SIGMASK(&nsiptr->lock, mask);

		/*
		 * return descripters for each end
		 */
		sv[0] = nsiptr->fd;
		sv[1] = siptr->fd;

		return (0);
	} else	{
		int		s2;
		int		cntlflag;
		struct pollfd	pfd[1];

		/*
		 * Set the queue length on s.
		 */
		if (_listen(siptr->fd, 1) < 0)
			goto bad;
		/*
		 * Set ns no delay mode.
		 */
		cntlflag = s_getfflags(nsiptr);
		if ((cntlflag & O_NDELAY) == 0) {
			(void) _fcntl(nsiptr->fd, F_SETFL,
			    (cntlflag | O_NDELAY) & ~O_NONBLOCK);
			nsiptr->fflags = (cntlflag | O_NDELAY) & ~O_NONBLOCK;
		}
		/*
		 * Send the connect ns->s.
		 */
		sndcall.addr.buf = (caddr_t)&bind_ux;
		sndcall.addr.len = sizeof (bind_ux);
		MUTEX_LOCK_SIGMASK(&nsiptr->lock, mask);
		if (_connect2(nsiptr, &sndcall, &mask) < 0 &&
		    errno != EINPROGRESS) {
			MUTEX_UNLOCK_SIGMASK(&nsiptr->lock, mask);
			goto bad;
		}
		MUTEX_UNLOCK_SIGMASK(&nsiptr->lock, mask);

		/*
		 * Pick up the connect indication on s2
		 */
		MUTEX_LOCK_SIGMASK(&siptr->lock, mask);
		if ((s2 = __accept(siptr, NULL, NULL, &mask)) < 0) {
			MUTEX_UNLOCK_SIGMASK(&siptr->lock, mask);
			goto bad;
		}
		MUTEX_UNLOCK_SIGMASK(&siptr->lock, mask);

		/*
		 * Wait at most 5 seconds for the
		 * confirmation to arrive
		 */
		pfd[0].events = POLLOUT;
		pfd[0].fd = nsiptr->fd;
		pfd[0].revents = 0;
		if (poll(pfd, (u_long)1, 5*1000) < 0 ||
			pfd[0].revents != POLLOUT) {
			if (pfd[0].revents == 0)
				errno = ETIMEDOUT;
			else	errno = ECONNABORTED;
			(void) close(s2);
			goto bad;
		}

		/*
		 * Reset the O_NDELAY flag if necessary.
		 */
		if ((cntlflag & O_NDELAY) == 0) {
			(void) _fcntl(nsiptr->fd, F_SETFL, cntlflag);
			nsiptr->fflags = cntlflag;
		}
		sv[0] = nsiptr->fd;
		sv[1] = s2;

		_s_close(siptr);

		return (0);
	}
bad:
	saved_errno = errno;

	_s_close(nsiptr);
	_s_close(siptr);

	errno = saved_errno;
	return (-1);
}
