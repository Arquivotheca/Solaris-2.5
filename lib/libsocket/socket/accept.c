/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T */
/*	  All Rights Reserved   */

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)accept.c	1.19	94/08/17 SMI"   /* SVr4.0 1.12  */

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
#include <sys/mkdev.h>
#include <errno.h>
#include <sys/stream.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <stropts.h>
#include <sys/tihdr.h>
#include <sys/socketvar.h>
#include <sys/socket.h>
#include <tiuser.h>
#include <signal.h>
#include <sys/sockmod.h>
#include <sys/stat.h>
#include <sys/sockio.h>
#include <fcntl.h>
#include <syslog.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "sock.h"

#pragma weak accept = _accept

static void _so_disconnect(struct _si_user *siptr, int seqno,
				struct strbuf *ctlbufp);

int
_accept(s, addr, addrlen)
	register int			s;
	register struct sockaddr	*addr;
	register int			*addrlen;

{
	register struct _si_user	*siptr;
	sigset_t			mask;
	int				retval;

	if ((siptr = _s_checkfd(s)) == NULL)
		return (-1);

	MUTEX_LOCK_SIGMASK(&siptr->lock, mask);
	if (siptr->udata.servtype == T_CLTS) {
		MUTEX_UNLOCK_SIGMASK(&siptr->lock, mask);
		errno = EOPNOTSUPP;
		return (-1);
	}

	/*
	 * Make sure a listen() has been done
	 * actually if the accept() has not been done, then the
	 * effect will be that the user blocks forever.
	 */
	if ((siptr->udata.so_options & SO_ACCEPTCONN) == 0) {
		MUTEX_UNLOCK_SIGMASK(&siptr->lock, mask);
		errno = EINVAL;
		return (-1);
	}

	retval = __accept(siptr, addr, addrlen, &mask);
	MUTEX_UNLOCK_SIGMASK(&siptr->lock, mask);
	return (retval);
}

int
__accept(siptr, addr, addrlen, accmaskp)
	register struct _si_user	*siptr;
	register struct sockaddr	*addr;
	register int			*addrlen;
	sigset_t			*accmaskp;
{

	register struct T_conn_res	*cres;
	register struct _si_user	*nsiptr;
	register int			s;
	register int			s2;
	int				retval;
	register union T_primitives	*pptr;
	struct strfdinsert		strfdinsert;
	int				flg;
	struct strbuf			ctlbuf;
	struct strbuf			databuf;
	char				dbuf[256];
	sigset_t			mask;
	sigset_t			procmask;
	int				didalloc = 0;

	flg = 0;
	s = siptr->fd;

	/*
	 * Get/wait for the T_CONN_IND.
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

	databuf.maxlen = sizeof (dbuf);
	databuf.len = 0;
	databuf.buf = dbuf;
	/*
	 * User data read and discarded for transport providers that
	 * use connect data.
	 * This is a call that may block indeinitely so we drop the lock and
	 * allow  signals in MT case here and reacquire it.
	 */
	MUTEX_UNLOCK_SIGMASK(&siptr->lock, *accmaskp);
	if ((retval = getmsg(s, &ctlbuf, &databuf, &flg)) < 0) {
		MUTEX_LOCK_SIGMASK(&siptr->lock, *accmaskp);
		goto err_out;
	}
	MUTEX_LOCK_SIGMASK(&siptr->lock, *accmaskp);
	/*
	 * did I get entire control message?
	 */
	if (retval & MORECTL) {
		errno = EIO;
		goto err_out;
	}
	/*
	 * We recieved  a T_CONN_IND. We are commited to it and
	 * do not want to fail with EINTR which may result in lost
	 * connection requests
	 */
	_s_blockallsignals(&procmask);
	while (retval & MOREDATA) {
		/*
		 * More connect data is present.
		 * We try to read it all and discard it.
		 */
		if ((retval = getmsg(siptr->fd, NULL, &databuf, &flg)) < 0) {
			int saved_errno = errno;
			_s_restoresigmask(&procmask);
			errno = saved_errno;
			goto err_out;
		}
		if (retval & MORECTL) {
			/*
			 * Error if any control part on stream head
			 * while remaining user data is being retrieved.
			 * ??Can also happen if any higher priority message
			 * is recived??
			 */
			_s_restoresigmask(&procmask);
			errno = EIO;
			goto err_out;
		}
	}

	/*
	 * is ctl part large enough to determine type
	 */
	if (ctlbuf.len < sizeof (long)) {
		_s_restoresigmask(&procmask);
		errno = EPROTO;
		goto err_out;
	}

	pptr = (union T_primitives *)ctlbuf.buf;
	switch (pptr->type) {
		case T_CONN_IND:
			if (ctlbuf.len < (sizeof (struct T_conn_ind)+
				pptr->conn_ind.SRC_length)) {
				_s_restoresigmask(&procmask);
				errno = EPROTO;
				goto err_out;
			}
			if (addr && addrlen) {
				*addrlen = _s_cpaddr(siptr, (char *)addr,
					*addrlen,
					ctlbuf.buf + pptr->conn_ind.SRC_offset,
					pptr->conn_ind.SRC_length);
			}
			break;

		default:
			_s_restoresigmask(&procmask);
			errno = EPROTO;
			goto err_out;
	}

	/*
	 * Open a new instance to do the accept on
	 *
	 */
	if ((nsiptr = _s_socreate(siptr->udata.sockparams.sp_family,
				siptr->udata.sockparams.sp_type,
				siptr->udata.sockparams.sp_protocol))
			== NULL) {
		int saved_errno = errno;

		_so_disconnect(siptr, pptr->conn_ind.SEQ_number, &ctlbuf);
		_s_restoresigmask(&procmask);
		errno = saved_errno;
		goto err_out;
	}

	s2 = nsiptr->fd;

	MUTEX_LOCK_SIGMASK(&nsiptr->lock, mask);
	/*
	 * must be bound for TLI except TCP.
	 */
	if (nsiptr->udata.sockparams.sp_family == AF_INET &&
	    nsiptr->udata.sockparams.sp_type == SOCK_STREAM) {
		nsiptr->udata.so_state |= SS_ISBOUND;
	} else if (__bind(nsiptr, NULL, 0, NULL, NULL) < 0) {
		int saved_errno = errno;

		MUTEX_UNLOCK_SIGMASK(&nsiptr->lock, mask);
		_s_close(nsiptr);
		_so_disconnect(siptr, pptr->conn_ind.SEQ_number, &ctlbuf);
		_s_restoresigmask(&procmask);
		errno = saved_errno;
		goto err_out;
	}

	cres = (struct T_conn_res *) ctlbuf.buf;
	cres->PRIM_type = T_CONN_RES;
	cres->OPT_length = 0;
	cres->OPT_offset = 0;
	cres->SEQ_number = pptr->conn_ind.SEQ_number;

	strfdinsert.ctlbuf.maxlen = ctlbuf.maxlen;
	strfdinsert.ctlbuf.len = sizeof (*cres);
	strfdinsert.ctlbuf.buf = (caddr_t)cres;

	strfdinsert.databuf.maxlen = 0;
	strfdinsert.databuf.len = -1;
	strfdinsert.databuf.buf = NULL;

	strfdinsert.fildes = s2;
	strfdinsert.offset = sizeof (long);
	strfdinsert.flags = 0;

	if (_ioctl(s, I_FDINSERT, &strfdinsert) < 0) {
		int saved_errno = errno;
		MUTEX_UNLOCK_SIGMASK(&nsiptr->lock, mask);
		_s_close(nsiptr);
		errno = saved_errno;
		_s_restoresigmask(&procmask);
		goto err_out;
	}

	if (!_s_is_ok(siptr, T_CONN_RES, &ctlbuf)) {
		int saved_errno = errno;
		MUTEX_UNLOCK_SIGMASK(&nsiptr->lock, mask);
		_s_close(nsiptr);
		_s_restoresigmask(&procmask);
		errno = saved_errno;
		goto err_out;
	}

	/*
	 * New socket must have attributes of the
	 * accepting socket.
	 */
	nsiptr->udata.so_state |= SS_ISCONNECTED;
	nsiptr->udata.so_options = siptr->udata.so_options & ~SO_ACCEPTCONN;
	MUTEX_UNLOCK_SIGMASK(&nsiptr->lock, mask);

	_s_restoresigmask(&procmask);

	/*
	 * Make the ownership of the new socket the
	 * same as the original.
	 */
	if (siptr->flags & (S_SIGIO|S_SIGURG)) {
		retval = 0;

		if (s_ioctl(s, SIOCGPGRP, &retval) == 0) {
			if (retval != 0) {
				(void) s_ioctl(nsiptr->fd, SIOCSPGRP, &retval);
			}
		} else	{
			(void) syslog(LOG_ERR,
					"accept: SIOCGPGRP failed errno %d\n",
					errno);
			errno = 0;
		}
	}

	/*
	 * The accepted socket inherits the non-blocking and SIGIO
	 * attributes of the accepting socket.
	 */
	flg = s_getfflags(siptr);
	/*
	 * fcntl(F_GETFL) (but not _fcntl) sets FASYNC when S_SIGIO is
	 * set. We do the same thing here. The fcntl(F_SETFL) will handle
	 * the FASYNC flag by doing the I_SETSIG etc.
	 * XXX Note: s_fcntl called directly instead of fcntl till
	 * linking problems when calling with BCP or search paths with
	 * libc ahead of libsocket are fixed.
	 */
	if (siptr->flags & S_SIGIO)
		flg |= FASYNC;
	if (flg & (FASYNC|FNDELAY|FNONBLOCK)) {
		flg &= (FREAD|FWRITE|FASYNC|FNDELAY|FNONBLOCK);
		(void) s_fcntl(nsiptr->fd, F_SETFL, flg);
	}
	if (didalloc)
		free(ctlbuf.buf);
	else
		siptr->ctlbuf = ctlbuf.buf;

	return (s2);

err_out:
	if (didalloc)
		free(ctlbuf.buf);
	else
		siptr->ctlbuf = ctlbuf.buf;
	return (-1);
}

/*
 * Must be called holding the mutex on siptr
 */
static void
_so_disconnect(struct _si_user *siptr, int seqno, struct strbuf *ctlbufp)
{
	register struct T_discon_req	*dreq;

	ctlbufp->len = sizeof (*dreq);

	dreq = (struct T_discon_req *) ctlbufp->buf;
	dreq->PRIM_type = T_DISCON_REQ;
	dreq->SEQ_number = seqno;

	(void) putmsg(siptr->fd, ctlbufp, NULL, 0);
}
