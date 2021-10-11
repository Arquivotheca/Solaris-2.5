/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)_conn_util.c	1.11	93/09/30 SMI"	/* SVr4.0 1.6	*/
/*
 * +++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 * 		PROPRIETARY NOTICE (Combined)
 *
 * This source code is unpublished proprietary information
 * constituting, or derived under license from AT&T's UNIX(r) System V.
 * In addition, portions of such source code were derived from Berkeley
 * 4.3 BSD under license from the Regents of the University of
 * California.
 *
 *
 *
 * 		Copyright Notice
 *
 * Notice of copyright on this source code product does not indicate
 * publication.
 *
 * 	(c) 1986-1992  Sun Microsystems, Inc
 * 	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
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


/*
 * Snd_conn_req - send connect request message to
 * transport provider
 */
int
_s_snd_conn_req(siptr, call, ctlbufp)
	register struct _si_user	*siptr;
	register struct t_call		*call;
	struct strbuf			*ctlbufp;
{
	register struct T_conn_req	*creq;
	register char			*buf;
	register int			size;

	buf = ctlbufp->buf;
	creq = (struct T_conn_req *)buf;
	creq->PRIM_type = T_CONN_REQ;
	creq->DEST_length = call->addr.len;
	creq->DEST_offset = 0;
	creq->OPT_length = call->opt.len;
	creq->OPT_offset = 0;
	size = sizeof (struct T_conn_req);

	if (call->addr.len) {
		_s_aligned_copy(buf, call->addr.len, size,
			call->addr.buf, (int *)&creq->DEST_offset);
		size = creq->DEST_offset + creq->DEST_length;
	}
	if (call->opt.len && call->opt.len != -1) {
		_s_aligned_copy(buf, call->opt.len, size,
			call->opt.buf, (int *)&creq->OPT_offset);
		size = creq->OPT_offset + creq->OPT_length;
	}

	ctlbufp->len = size;

	if (putmsg(siptr->fd, ctlbufp,
		(struct strbuf *)(call->udata.len? &call->udata: NULL), 0) < 0)
		return (-1);

	if (!_s_is_ok(siptr, T_CONN_REQ, ctlbufp))
		return (-1);

	return (0);
}

/*
 * Rcv_conn_con - get connection confirmation off
 * of read queue
 */
int
_s_rcv_conn_con(siptr, ctlbufp)
	register struct _si_user	*siptr;
	struct strbuf			*ctlbufp;
{
	struct strbuf			databuf;
	register union T_primitives	*pptr;
	register int			retval;
	int				flg;
	char				dbuf[256];

	flg = 0;
	if (siptr->udata.servtype == T_CLTS) {
		errno = EOPNOTSUPP;
		return (-1);
	}

	ctlbufp->len = 0;

	databuf.maxlen = sizeof (dbuf);
	databuf.len = 0;
	databuf.buf = dbuf;

	/*
	 * No data expected for TCP, but we read and discard data for
	 * other transport providers.
	 */
	if ((retval = getmsg(siptr->fd, ctlbufp, &databuf, &flg)) < 0) {
		if (errno == ENXIO)
			errno = ECONNREFUSED;
		return (-1);
	}

	/*
	 * did we get entire control message
	 */
	if (retval & MORECTL) {
		/* more control part is unexpected */
		errno = EIO;
		return (-1);
	}

	while (retval & MOREDATA) {
		/*
		 * More connect data is present.
		 * We try to read it all and discard it.
		 */
		if ((retval = getmsg(siptr->fd, NULL, &databuf, &flg)) < 0) {
			if (errno == ENXIO)
				errno = ECONNREFUSED;
			return (-1);
		}
		if (retval & MORECTL) {
			/*
			 * Error if any control part on stream head
			 * while remaining user data is being retrieved.
			 * ??Can also happen if any higher priority message
			 * is recived??
			 */
			errno = EIO;
			return (-1);
		}
	}

	/*
	 * is cntl part large enough to determine message type?
	 */
	if (ctlbufp->len < sizeof (long)) {
		errno = EPROTO;
		return (-1);
	}

	pptr = (union T_primitives *)ctlbufp->buf;
	switch (pptr->type) {
		case T_CONN_CON:
			return (0);

		case T_DISCON_IND:
			if (ctlbufp->len < sizeof (struct T_discon_ind))
				errno = ECONNREFUSED;
			else	errno = pptr->discon_ind.DISCON_reason;
			return (-1);

		default:
			break;
	}

	errno = EPROTO;
	return (-1);
}
