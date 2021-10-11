/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T */
/*	  All Rights Reserved   */

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)listen.c	1.12	94/01/04 SMI"   /* SVr4.0 1.8	*/

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
#include <syslog.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "sock.h"

#pragma weak listen = _listen

/*
 * We make the socket module do the unbind,
 * if necessary, to make the timing window
 * of error as small as possible.
 */
int
_listen(s, qlen)
	register int			s;
	register int			qlen;
{
	register struct T_bind_req	*bind_req;
	register struct _si_user	*siptr;
	char				*cbuf;
	sigset_t			procmask;
	sigset_t			mask;
	int				didalloc = 0;

	if ((siptr = _s_checkfd(s)) == NULL)
		return (-1);

	MUTEX_LOCK_SIGMASK(&siptr->lock, mask);
	if (siptr->udata.servtype == T_CLTS) {
		MUTEX_UNLOCK_SIGMASK(&siptr->lock, mask);
		errno = EOPNOTSUPP;
		return (-1);
	}

	/*
	 * If the socket is ready to accept connections already, then
	 * return without doing anything.  This avoids a problem where
	 * a second listen() call fails if a connection is pending and
	 * leaves the socket unbound.
	 */
	if ((siptr->udata.so_options & SO_ACCEPTCONN) != 0) {
		MUTEX_UNLOCK_SIGMASK(&siptr->lock, mask);
		return (0);
	}

	if (siptr->ctlbuf) {
		cbuf = siptr->ctlbuf;
		siptr->ctlbuf = NULL;
	} else {
		/*
		 * siptr->ctlbuf is in use
		 * allocate and free after use.
		 */
		if (_s_cbuf_alloc(siptr, &cbuf) < 0)
			return (-1);
		didalloc = 1;
	}
	bind_req = (struct T_bind_req *)cbuf;

	bind_req->PRIM_type = T_BIND_REQ;
	bind_req->ADDR_offset = sizeof (*bind_req);
	bind_req->CONIND_number = qlen + 1;

	if ((siptr->udata.so_state & SS_ISBOUND) == 0) {
		/*
		 * Must have been explicitly bound in the UNIX domain.
		 */
		if (siptr->udata.sockparams.sp_family == AF_UNIX) {
			errno = EINVAL;
			goto err_out;
		}

		bind_req->ADDR_length = 0;
	} else	bind_req->ADDR_length = siptr->udata.addrsize;

	_s_blockallsignals(&procmask);
	if (!_s_do_ioctl(s, cbuf, sizeof (*bind_req) +
				bind_req->ADDR_length, SI_LISTEN, NULL)) {
		_s_restoresigmask(&procmask);
		goto err_out;
	}
	_s_restoresigmask(&procmask);

	siptr->udata.so_options |= SO_ACCEPTCONN;
	if (didalloc)
		free(cbuf);
	else
		siptr->ctlbuf = cbuf;

	MUTEX_UNLOCK_SIGMASK(&siptr->lock, mask);
	return (0);

err_out:
	if (didalloc)
		free(cbuf);
	else
		siptr->ctlbuf = cbuf;
	MUTEX_UNLOCK_SIGMASK(&siptr->lock, mask);
	return (-1);
}
