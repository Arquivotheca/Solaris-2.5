/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T */
/*	  All Rights Reserved   */

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)connect.c	1.16	94/04/08 SMI"   /* SVr4.0 1.7	*/

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
#include <sys/ioctl.h>
#include <stropts.h>
#include <errno.h>
#include <tiuser.h>
#include <sys/socketvar.h>
#include <sys/socket.h>
#include <sys/stream.h>
#include <sys/sockmod.h>
#include <fcntl.h>
#include <signal.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "sock.h"

static int __connect(struct _si_user *siptr, struct sockaddr *name,
		int namelen, int nameflag, sigset_t *connmaskp);

#pragma weak connect = _connect

int
_connect(s, name, namelen)
	register int			s;
	register struct sockaddr	*name;
	register int			namelen;
{
	struct _si_user			*siptr;
	sigset_t			mask;
	int				retval;

	if ((siptr = _s_checkfd(s)) == NULL)
		return (-1);

	MUTEX_LOCK_SIGMASK(&siptr->lock, mask);
	retval = __connect(siptr, name, namelen, 1, &mask);
	MUTEX_UNLOCK_SIGMASK(&siptr->lock, mask);
	return (retval);
}

static int
__connect(siptr, name, namelen, nameflag, connmaskp)
	struct _si_user			*siptr;
	register struct sockaddr	*name;
	register int			namelen;
	register int			nameflag;
	sigset_t			*connmaskp;
{
	struct t_call			sndcall;
	struct bind_ux			bind_ux;

	(void) memset((caddr_t)&sndcall, 0, sizeof (sndcall));

	if (name && name->sa_family == AF_INET) {
		struct sockaddr_in 	*saddr_in;

		if (namelen < sizeof (*saddr_in)) {
			errno = EINVAL;
			return (-1);
		}
		saddr_in = (struct sockaddr_in *)name;
		(void) memset(&saddr_in->sin_zero, 0, 8);
	}

	if (name && name->sa_family == AF_UNIX) {
		struct stat		rstat;
		struct sockaddr_un	*un;
		int			len;

		if (siptr->udata.sockparams.sp_family != AF_UNIX) {
			errno = EINVAL;
			return (-1);
		}
		if (namelen > sizeof (name->sa_family)) {
			un = (struct sockaddr_un *)name;

			if (namelen > sizeof (*un) ||
					(len = _s_uxpathlen(un)) ==
						sizeof (un->sun_path)) {
				errno = EMSGSIZE;
				return (-1);
			}

			un->sun_path[len] = 0;	/* Null terminate */

			/*
			 * Stat the file.
			 */
#if defined(i386)
			if (_xstat(_STAT_VER, (caddr_t)un->sun_path,
					&rstat) < 0)
#elif defined(sparc)
			if (_stat((caddr_t) un->sun_path, &rstat) < 0)
#else
#error Unknown architecture!
#endif
				return (-1);

			if ((rstat.st_mode & S_IFMT) != S_IFIFO) {
				errno = ENOTSOCK;
				return (-1);
			}

			(void) memset((caddr_t)&bind_ux, 0, sizeof (bind_ux));
			bind_ux.extdev = rstat.st_dev;
			bind_ux.extino = rstat.st_ino;
			bind_ux.extsize = sizeof (struct ux_dev);
			if (nameflag == 0)
				namelen = sizeof (name->sa_family);
			(void) memcpy((caddr_t)&bind_ux.name,
				(caddr_t)name, namelen);

			sndcall.addr.buf = (caddr_t)&bind_ux;
			sndcall.addr.len = sizeof (bind_ux);
		} else	{
			sndcall.addr.buf = NULL;
			sndcall.addr.len = 0;
		}
	} else	{
		sndcall.addr.buf = (caddr_t)name;
		sndcall.addr.len = _s_min(namelen, siptr->udata.addrsize);
	}

	return (_connect2(siptr, &sndcall, connmaskp));
}

int
_connect2(siptr, sndcall, connmaskp)
	register struct _si_user	*siptr;
	register struct t_call		*sndcall;
	sigset_t			*connmaskp;
{
	register int			fctlflg;
	sigset_t			procmask;
	struct strbuf			ctlbuf;
	int				didalloc = 0;

	fctlflg = s_getfflags(siptr);

	if (fctlflg&(O_NDELAY|O_NONBLOCK) && siptr->udata.servtype != T_CLTS) {
		/*
		 * Secretly tell sockmod not to pass
		 * up the T_CONN_CON, because we
		 * are not going to wait for it.
		 * (But dont tell anyone - especially
		 * the transport provider).
		 */
		sndcall->opt.len = (ulong)-1;	/* secret sign */
	}

	/*
	 * must be bound for TLI except TCP.
	 */
	if ((siptr->udata.so_state & SS_ISBOUND) == 0) {
		if (siptr->udata.sockparams.sp_family == AF_INET &&
		    siptr->udata.sockparams.sp_type == SOCK_STREAM) {
			siptr->udata.so_state |= SS_ISBOUND;
		} else if (__bind(siptr, NULL, 0, NULL, NULL) < 0)
			return (-1);
	}
	/*
	 * Acquire ctlbuf for use in sending/receiving
	 */
	ctlbuf.len = 0;
	if (siptr->ctlbuf) {
		ctlbuf.buf = siptr->ctlbuf;
		siptr->ctlbuf = NULL;
		ctlbuf.maxlen = siptr->ctlsize;
	} else {
		/*
		 * siptr->ctlbuf is in use
		 * allocate and free after use.
		 */
		if ((ctlbuf.maxlen = _s_cbuf_alloc(siptr,
						&ctlbuf.buf)) < 0)
			return (-1);
		didalloc = 1;
	}

	/*
	 * Block all signals till T_CONN_REQ req sent and
	 * acked with T_OK/ERROR_ACK
	 */
	_s_blockallsignals(&procmask);
	if (_s_snd_conn_req(siptr, sndcall, &ctlbuf) < 0) {
		_s_restoresigmask(&procmask);
		goto err_out;
	}
	_s_restoresigmask(&procmask);
	/*
	 * If no delay, return with error if not CLTS.
	 */
	if (fctlflg&(O_NDELAY|O_NONBLOCK) && siptr->udata.servtype != T_CLTS) {
		errno = EINPROGRESS;
		siptr->udata.so_state |= SS_ISCONNECTING;
		goto err_out;
	}

	/*
	 * If CLTS, don't get the connection confirm.
	 */
	if (siptr->udata.servtype == T_CLTS) {
		if (sndcall->addr.len == 0)
			/*
			 * Connect to Null address, breaks
			 * the connection.
			 */
			siptr->udata.so_state &= ~SS_ISCONNECTED;
		else	siptr->udata.so_state |= SS_ISCONNECTED;
		goto ok_out;
	}
	/*
	 * This is a potentially blocking call.
	 * In MT case, drop the lock and allow signals
	 * and then reacquire the lock later
	 */
	MUTEX_UNLOCK_SIGMASK(&siptr->lock, *connmaskp);
	if (_s_rcv_conn_con(siptr, &ctlbuf) < 0) {
		MUTEX_LOCK_SIGMASK(&siptr->lock, *connmaskp);
		goto err_out;
	}
	MUTEX_LOCK_SIGMASK(&siptr->lock, *connmaskp);

	siptr->udata.so_state |= SS_ISCONNECTED;

ok_out:
	if (didalloc)
		free(ctlbuf.buf);
	else
		siptr->ctlbuf = ctlbuf.buf;

	return (0);

err_out:
	if (didalloc)
		free(ctlbuf.buf);
	else
		siptr->ctlbuf = ctlbuf.buf;
	return (-1);
}
