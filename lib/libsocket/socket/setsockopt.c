/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T */
/*	  All Rights Reserved   */

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)setsockopt.c	1.11	93/09/30 SMI"   /* SVr4.0 1.6   */

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
#include <sys/socket.h>
#include <tiuser.h>
#include <sys/sockmod.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "sock.h"

#pragma weak setsockopt = _setsockopt

int
_setsockopt(s, level, optname, optval, optlen)
	register int			s;
	register int			level;
	register int			optname;
	register char			*optval;
	register int			optlen;
{
	register struct _si_user	*siptr;
	sigset_t			mask;
	int				retval;

	if ((siptr = _s_checkfd(s)) == NULL)
		return (-1);

	MUTEX_LOCK_SIGMASK(&siptr->lock, mask);
	retval = __setsockopt(siptr, level, optname, optval, optlen);
	MUTEX_UNLOCK_SIGMASK(&siptr->lock, mask);
	return (retval);
}

int
__setsockopt(siptr, level, optname, optval, optlen)
	struct _si_user			*siptr;
	register int			level;
	register int			optname;
	register char			*optval;
	register int			optlen;
{
	register struct T_optmgmt_req	*opt_req;
	register int			s;
	char				*cbuf;
	sigset_t			procmask;
	register int			size;
	register struct opthdr		*opt;
	int				didalloc = 0;

	s = siptr->fd;

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
	opt_req = (struct T_optmgmt_req *)cbuf;
	opt_req->PRIM_type = T_OPTMGMT_REQ;
	opt_req->OPT_length = sizeof (*opt) + optlen;
	opt_req->OPT_offset = sizeof (*opt_req);
	opt_req->MGMT_flags = T_NEGOTIATE;

	opt = (struct opthdr *)(cbuf + sizeof (*opt_req));
	opt->level = level;
	opt->name = optname;
	opt->len = optlen;
	(void) memcpy((caddr_t)opt + sizeof (*opt), optval, optlen);

	size = opt_req->OPT_offset + opt_req->OPT_length;

	_s_blockallsignals(&procmask);
	if (!_s_do_ioctl(s, cbuf, size, TI_OPTMGMT, 0)) {
		_s_restoresigmask(&procmask);
		if (didalloc)
			free(cbuf);
		else
			siptr->ctlbuf = cbuf;

		return (-1);
	}
	_s_restoresigmask(&procmask);
	/* Catch changes to SO_DEBUG to make socket library tracing work */
	if (level == SOL_SOCKET && optname == SO_DEBUG)
		(void) _s_synch(siptr);

	if (didalloc)
		free(cbuf);
	else
		siptr->ctlbuf = cbuf;
	return (0);
}
