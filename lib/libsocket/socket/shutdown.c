/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T */
/*	  All Rights Reserved   */

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)shutdown.c	1.10	93/09/30 SMI"   /* SVr4.0 1.7	*/

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
#include <sys/socketvar.h>
#include <sys/socket.h>
#include <tiuser.h>
#include <sys/sockmod.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "sock.h"

#pragma weak shutdown = _shutdown

int
_shutdown(s, how)
	register int		s;
	int			how;
{
	sigset_t		procmask;
	struct   _si_user	*siptr;
	sigset_t		mask;

	if ((siptr = _s_checkfd(s)) == (struct _si_user *)NULL)
		return (-1);

	MUTEX_LOCK_SIGMASK(&siptr->lock, mask);
	if (how < 0 || how > 2) {
		MUTEX_UNLOCK_SIGMASK(&siptr->lock, mask);
		errno = EINVAL;
		return (-1);
	}

	if ((siptr->udata.so_state & SS_ISCONNECTED) == 0) {
		if (_s_synch(siptr) < 0) {
			MUTEX_UNLOCK_SIGMASK(&siptr->lock, mask);
			return (-1);
		}
		if ((siptr->udata.so_state & SS_ISCONNECTED) == 0) {
			MUTEX_UNLOCK_SIGMASK(&siptr->lock, mask);
			errno = ENOTCONN;
			return (-1);
		}
	}

	_s_blockallsignals(&procmask);
	if (!_s_do_ioctl(s, (char *)&how, sizeof (how), SI_SHUTDOWN, 0)) {
		if (errno != EPIPE) {
			_s_restoresigmask(&procmask);
			MUTEX_UNLOCK_SIGMASK(&siptr->lock, mask);
			return (-1);
		} else	errno = 0;
	}
	_s_restoresigmask(&procmask);

	/*
	 * If we got EPIPE back from the ioctl, then we can
	 * no longer talk to sockmod. The best we can do now
	 * is set our local state and hope the user doesn't
	 * use read/write.
	 */
	if (how == 0 || how == 2)
		siptr->udata.so_state |= SS_CANTRCVMORE;
	if (how == 1 || how == 2)
		siptr->udata.so_state |= SS_CANTSENDMORE;

	MUTEX_UNLOCK_SIGMASK(&siptr->lock, mask);
	return (0);
}
