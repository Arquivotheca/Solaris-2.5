/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T */
/*	  All Rights Reserved   */

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)s_ioctl.c	1.18	95/03/10 SMI"   /* SVr4.0 1.7	*/

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
#include <sys/sockio.h>
#include <sys/filio.h>
#include <sys/file.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stropts.h>
#include <signal.h>
#include <sys/termios.h>
#include <sys/socket.h>
#include <tiuser.h>
#include <sys/stream.h>
#include <sys/sockmod.h>
#include <errno.h>
#include <syslog.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "sock.h"

/*
 * Multi-threading note:
 * The following ioctls and fcntls can not be issued from within the socket
 * library while holding the siptr->lock (since the s_ioctl or s_fcntl code
 * acquires that lock themselves):
 *	FIOCASYNC
 *	SIOCSPGRP
 *	FIOCSETOWN
 */


#pragma weak fcntl = s_fcntl
#pragma weak ioctl = s_ioctl

#define		ISSOCK(A)	(_ioctl((A), I_FIND, "sockmod"))

static int _s_dofioasync(int des, int *arg);
int s_ioctl(int des, int request, char *arg);

int
s_fcntl(des, cmd, arg)
	register int	des;
	register int	cmd;
	int		arg;

{
	int		res;

	switch (cmd) {
	case F_SETOWN:
		SOCKDEBUG((struct _si_user *)NULL,
					"fcntl: got fcntl-F_SETOWN\n", 0);
		return (s_ioctl(des, FIOSETOWN, (char *)&arg));

	case F_GETOWN:
		SOCKDEBUG((struct _si_user *)NULL,
					"fcntl: got fcntl-F_GETOWN\n", 0);
		if (s_ioctl(des, FIOGETOWN, (char *)&res) < 0)
			return (-1);
		return (res);

	case F_SETFL:
		SOCKDEBUG((struct _si_user *)NULL,
					"fcntl: got fcntl-F_SETFL\n", 0);
		if (ISSOCK(des) == 1) {
			register struct _si_user *siptr;

			/* Need to have fflags track the kernel flags */
			if ((siptr = _s_checkfd(des)) != NULL)
				s_invalfflags(siptr);
			res = arg & FASYNC;
			if (_s_dofioasync(des, &res) < 0)
				return (-1);
		}
		errno = 0;
		return (_fcntl(des, cmd, arg));

	case F_GETFL: {
		register int flags;

		SOCKDEBUG((struct _si_user *)NULL,
					"fcntl: got fcntl-F_GETFL\n", 0);
		if ((flags = _fcntl(des, cmd, arg)) < 0)
			return (-1);

		if (ISSOCK(des) == 1) {
			register struct _si_user *siptr;

			/*
			 * See if SIGIO is in force.
			 */
			if ((siptr = _s_checkfd(des)) !=
					(struct _si_user *)NULL) {
				if ((siptr->flags & S_SIGIO) != 0)
					flags |= FASYNC;
			}
		}
		errno = 0;
		return (flags);
	}

	default:
		return (_fcntl(des, cmd, arg));
	}
}

int
s_ioctl(des, request, arg)
	register int	des;
	register int	request;
	char		*arg;
{
	pid_t		pid;
	int		retval;
	int		sv_errno;
	struct	_si_user	*siptr;

	SOCKDEBUG((struct _si_user *)NULL,
				"ioctl: got ioctl request %x\n", request);
	switch (request) {
	case FIOASYNC:
	case FIOSETOWN:
	case FIOGETOWN:
	case SIOCSPGRP:
	case SIOCGPGRP:
	case SIOCATMARK:
		if (ISSOCK(des) != 1) {
			if (request == SIOCSPGRP || request == SIOCGPGRP ||
						request == SIOCATMARK) {
				SOCKDEBUG((struct _si_user *)NULL,
						"ioctl: %d not socket\n", des);
				errno = ENOTSOCK;
				return (-1);
			} else	{
				errno = 0;
				break;
			}
		}

		switch (request) {
		case FIOASYNC:
			/*
			 * Facilitate SIGIO.
			 */
			return (_s_dofioasync(des, (int *)arg));

		case SIOCGPGRP:
		case FIOGETOWN:
			SOCKDEBUG((struct _si_user *)NULL,
					"ioctl: got SIOCGPGRP/FIOGETOWN\n", 0);
			retval = 0;
			if (_ioctl(des, I_GETSIG, &retval) < 0) {
				if (errno == EINVAL) {
					retval = 0;
				} else	return (-1);
			}
			if (retval & (S_RDBAND|S_BANDURG|S_RDNORM|S_WRNORM))
				*(pid_t *)arg = getpid();
			else	*(pid_t *)arg = 0;
			return (0);

		case SIOCSPGRP:
		case FIOSETOWN: {
			sigset_t			mask;

			/*
			 * Facilitate receipt of SIGURG.
			 *
			 * We are forgiving in that if a
			 * process group was specified rather
			 * than a process id, we will only
			 * fail it if the process group
			 * specified is not the callers.
			 */
			SOCKDEBUG((struct _si_user *)NULL,
					"ioctl: got SIOCSPGRP/FIOSETOWN\n", 0);
			pid = *(pid_t *)arg;
			if (pid < 0) {
				pid = -pid;
				if (pid != getpgrp()) {
					errno = EINVAL;
					return (-1);
				}
			} else	if (pid != 0) {
				if (pid != getpid()) {
					errno = EINVAL;
					return (-1);
				}
			}

			if ((siptr = _s_checkfd(des)) ==
					(struct _si_user *)NULL)
				return (-1);

			MUTEX_LOCK_SIGMASK(&siptr->lock, mask);
			retval = 0;
			if (pid != 0) {
				if (siptr->flags & S_SIGIO)
					retval = S_RDNORM|S_WRNORM;
				retval |= S_RDBAND|S_BANDURG;
			}
			MUTEX_UNLOCK_SIGMASK(&siptr->lock, mask);
			if (_ioctl(des, I_SETSIG, retval) < 0) {
				(void) syslog(LOG_ERR,
					"ioctl: I_SETSIG failed %d\n", errno);
				return (-1);
			}
			return (0);
		}

		case SIOCATMARK:
			SOCKDEBUG((struct _si_user *)NULL,
						"ioctl: got SIOCATMARK\n", 0);
			if ((retval = _ioctl(des, I_ATMARK, LASTMARK)) < 0)
				return (-1);
			*(int *)arg = retval;
			return (0);
		}

	default:
		break;
	}
	SOCKDEBUG((struct _si_user *)NULL,
			"ioctl: match failed, calling regular ioctl\n", 0);

	retval = _ioctl(des, request, arg);
	sv_errno = errno;
	if ((retval != -1) &&
	    (siptr = find_silink(des)) != (struct _si_user *) NULL)
		s_invalfflags(siptr);
	errno = sv_errno;
	return (retval);
}

/*
 * s_getfflags() and s_invalfflags() operate as a cache for the file flags.
 */
int
s_getfflags(siptr)
	register struct _si_user	*siptr;
{
	int	save_errno;

	save_errno = errno;
	if (siptr->fflags == -1) {
		siptr->fflags = _fcntl(siptr->fd, F_GETFL, 0);
		if (siptr->fflags == -1) {
			(void) syslog(LOG_ERR,
			    "s_getfflags: fcntl: F_GETFL failed, errno %d\n",
				errno);
			save_errno = 0;
		}
	}
	errno = save_errno;
	return (siptr->fflags);
}

void
s_invalfflags(siptr)
	register struct _si_user	*siptr;
{
	siptr->fflags = -1;
}

/*
 * Enable or disable asynchronous I/O
 */
static int
_s_dofioasync(des, arg)
	register int	des;
	register int	*arg;
{
	register struct _si_user *siptr;
	int			retval;
	sigset_t		mask;

	SOCKDEBUG((struct _si_user *)NULL, "_s_dofioasync: Entered, %d\n",
						*arg);

	if ((siptr = _s_checkfd(des)) == (struct _si_user *)NULL)
		return (-1);

	MUTEX_LOCK_SIGMASK(&siptr->lock, mask);
	/*
	 * Turn on or off async I/O.
	 */
	retval = 0;
	if (*arg) {
		/*
		 * Turn ON SIGIO if
		 * it is not already on.
		 */
		if ((siptr->flags & S_SIGIO) != 0) {
			MUTEX_UNLOCK_SIGMASK(&siptr->lock, mask);
			return (0);
		}

		SOCKDEBUG((struct _si_user *)NULL,
			"_s_dofioasync: Setting SIGIO\n", 0);

		if (siptr->flags & S_SIGURG)
			retval = S_RDBAND|S_BANDURG;
		retval |= S_RDNORM|S_WRNORM;
		if (_ioctl(des, I_SETSIG, retval) < 0)
			return (-1);

		siptr->flags |= S_SIGIO;

		MUTEX_UNLOCK_SIGMASK(&siptr->lock, mask);
		return (0);
	}

	/*
	 * Turn OFF SIGIO if
	 * not already off.
	 */
	if ((siptr->flags & S_SIGIO) == 0) {
		MUTEX_UNLOCK_SIGMASK(&siptr->lock, mask);
		return (0);
	}

	SOCKDEBUG((struct _si_user *)NULL,
			"_s_dofioasync: Re-setting SIGIO\n", 0);

	siptr->flags &= ~S_SIGIO;

	if (siptr->flags & S_SIGURG)
		retval = S_RDBAND|S_BANDURG;
	MUTEX_UNLOCK_SIGMASK(&siptr->lock, mask);
	return (_ioctl(des, I_SETSIG, retval));
}
