/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T */
/*	  All Rights Reserved   */

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)getsocknm.c	1.11	93/12/25 SMI"   /* SVr4.0 1.6   */

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
#include <sys/timod.h>
#include <sys/socketvar.h>
#include <sys/socket.h>
#include <tiuser.h>
#include <sys/sockmod.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "sock.h"

#pragma weak getsockname = _getsockname

int
_getsockname(s, name, namelen)
	register int			s;
	register struct sockaddr	*name;
	register int			*namelen;
{
	register struct _si_user	*siptr;
	sigset_t			mask;
	int				retval;

	if ((siptr = _s_checkfd(s)) == NULL)
		return (-1);

	MUTEX_LOCK_SIGMASK(&siptr->lock, mask);
	if (name == NULL || namelen == NULL) {
		MUTEX_UNLOCK_SIGMASK(&siptr->lock, mask);
		errno = EINVAL;
		return (-1);
	}

	retval = __getsockname(siptr, name, namelen);
	MUTEX_UNLOCK_SIGMASK(&siptr->lock, mask);
	return (retval);
}

int
__getsockname(siptr, name, namelen)
	register struct _si_user	*siptr;
	register struct sockaddr	*name;
	register int			*namelen;
{
	struct netbuf			netbuf;
	int				rval;
	int				didalloc = 0;

	netbuf.len = 0;
	if (siptr->ctlbuf) {
		netbuf.buf = siptr->ctlbuf;
		siptr->ctlbuf = NULL;
		netbuf.maxlen = siptr->ctlsize;
	} else {
		/*
		 * siptr->ctlbuf is in use
		 * allocate and free after use.
		 */
		rval = _s_cbuf_alloc(siptr, &netbuf.buf);
		if (rval < 0)
			return (-1);
		netbuf.maxlen = rval;
		didalloc = 1;
	}

	/*
	 * Get it from sockmod.
	 */
	do  {
		rval = _ioctl(siptr->fd, TI_GETMYNAME, (caddr_t)&netbuf);
	} while (rval < 0 && errno == EINTR);

	if (rval < 0) {
		switch (errno) {
			case ENXIO:
			case EPIPE:
				errno = 0;
				break;

			case ENOTTY:
			case ENODEV:
			case EINVAL:
				errno = ENOTSOCK;
				break;
		}
		if (errno) {
			if (didalloc)
				free(netbuf.buf);
			else
				siptr->ctlbuf = netbuf.buf;

			return (-1);
		}
	}

	errno = 0;
	*namelen = _s_cpaddr(siptr, (char *)name, *namelen,
				netbuf.buf, netbuf.len);
	if (didalloc)
		free(netbuf.buf);
	else
		siptr->ctlbuf = netbuf.buf;
	return (0);
}
