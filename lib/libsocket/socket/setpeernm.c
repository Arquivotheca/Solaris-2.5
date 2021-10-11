/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T */
/*	  All Rights Reserved   */

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)setpeernm.c	1.9	93/09/30 SMI"   /* SVr4.0 1.5   */

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
#include <tiuser.h>
#include <sys/sockmod.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "sock.h"

static int __setpeername(struct _si_user *siptr, struct sockaddr *name,
		int namelen);

#pragma weak setpeername = _setpeername

int
_setpeername(s, name, namelen)
	register int			s;
	register struct sockaddr	*name;
	register int			namelen;
{
	register struct _si_user	*siptr;
	sigset_t			mask;
	int				retval;

	if ((siptr = _s_checkfd(s)) == (struct _si_user *)NULL)
		return (-1);

	MUTEX_LOCK_SIGMASK(&siptr->lock, mask);
	if (namelen <= 0) {
		MUTEX_UNLOCK_SIGMASK(&siptr->lock, mask);
		errno = EINVAL;
		return (-1);
	}

	retval = __setpeername(siptr, name,
			_s_min(namelen, siptr->udata.addrsize));
	MUTEX_UNLOCK_SIGMASK(&siptr->lock, mask);
	return (retval);
}


static int
__setpeername(siptr, name, namelen)
	register struct	_si_user 	*siptr;
	register struct sockaddr	*name;
	register int			namelen;
{
	sigset_t			procmask;

	_s_blockallsignals(&procmask);
	if (!_s_do_ioctl(siptr->fd, (caddr_t)name, namelen,
				SI_SETPEERNAME, NULL)) {
		_s_restoresigmask(&procmask);
		return (-1);
	}
	_s_restoresigmask(&procmask);

	return (0);
}
