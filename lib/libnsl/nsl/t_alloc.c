/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)t_alloc.c	1.17	94/03/02 SMI"	/* SVr4.0 1.4.1.2	*/

#include "timt.h"
#include <sys/types.h>
#include <stdlib.h>
#include <rpc/trace.h>
#include <sys/stream.h>
#include <sys/stropts.h>
#include <sys/tihdr.h>
#include <sys/timod.h>
#include <tiuser.h>
#include <stdio.h>
#include <errno.h>
#include <sys/signal.h>
#include "tli.h"


char *
t_alloc(fd, struct_type, fields)
int fd;
int struct_type;
int fields;
{
	struct strioctl strioc;
	struct T_info_ack info;
	sigset_t mask, t_mask;
	union structptrs {
		char	*caddr;
		struct t_bind *bind;
		struct t_call *call;
		struct t_discon *dis;
		struct t_optmgmt *opt;
		struct t_unitdata *udata;
		struct t_uderr *uderr;
		struct t_info *info;
	} p;
	unsigned dsize;
	register struct _ti_user *tiptr;

	trace4(TR_t_alloc, 0, fd, struct_type, fields);
	if ((tiptr = _t_checkfd(fd)) == NULL) {
		int sv_errno = errno;
		trace4(TR_t_alloc, 1, fd, struct_type, fields);
		errno = sv_errno;
		return (NULL);
	}
	MUTEX_LOCK_SIGMASK(&tiptr->ti_lock, mask);
	_t_blocksigpoll(&t_mask, SIG_BLOCK);

	/*
	 * Get size info for T_ADDR, T_OPT, and T_UDATA fields
	 */
	info.PRIM_type = T_INFO_REQ;
	strioc.ic_cmd = TI_GETINFO;
	strioc.ic_timout = -1;
	strioc.ic_len = sizeof (struct T_info_req);
	strioc.ic_dp = (char *)&info;
	if (ioctl(fd, I_STR, &strioc) < 0) {
		int sv_errno = errno;
		_t_blocksigpoll(&t_mask, SIG_SETMASK);
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		t_errno = TSYSERR;
		trace4(TR_t_alloc, 1, fd, struct_type, fields);
		errno = sv_errno;
		return (NULL);
	}
	_t_blocksigpoll(&t_mask, SIG_SETMASK);
	if (strioc.ic_len != sizeof (struct T_info_ack)) {
		t_errno = TSYSERR;
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace4(TR_t_alloc, 1, fd, struct_type, fields);
		errno = EIO;
		return (NULL);
	}


	/*
	 * Malloc appropriate structure and the specified
	 * fields within each structure.  Initialize the
	 * 'buf' and 'maxlen' fields of each.
	 */
	switch (struct_type) {

	case T_BIND:
		if ((p.bind = (struct t_bind *)
			calloc(1, (unsigned)sizeof (struct t_bind))) == NULL)
				goto out;
		if (fields & T_ADDR) {
			if (_alloc_buf(&p.bind->addr, info.ADDR_size) < 0)
				goto out;
		}
		trace4(TR_t_alloc, 1, fd, struct_type, fields);
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		return ((char *)p.bind);

	case T_CALL:
		if ((p.call = (struct t_call *)
			calloc(1, (unsigned)sizeof (struct t_call))) == NULL)
				goto out;
		if (fields & T_ADDR) {
			if (_alloc_buf(&p.call->addr, info.ADDR_size) < 0)
				goto out;
		}
		if (fields & T_OPT) {
			if (_alloc_buf(&p.call->opt, info.OPT_size) < 0)
				goto out;
		}
		if (fields & T_UDATA) {
			dsize = _t_max(info.CDATA_size, info.DDATA_size);
			if (_alloc_buf(&p.call->udata, dsize) < 0)
				goto out;
		}
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace4(TR_t_alloc, 1, fd, struct_type, fields);
		return ((char *)p.call);

	case T_OPTMGMT:
		if ((p.opt = (struct t_optmgmt *)
			calloc(1, (unsigned)sizeof (struct t_optmgmt))) == NULL)
				goto out;
		if (fields & T_OPT) {
			if (_alloc_buf(&p.opt->opt, info.OPT_size) < 0)
				goto out;
		}
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace4(TR_t_alloc, 1, fd, struct_type, fields);
		return ((char *)p.opt);

	case T_DIS:
		if ((p.dis = (struct t_discon *)
			calloc(1, (unsigned)sizeof (struct t_discon))) == NULL)
				goto out;
		if (fields & T_UDATA) {
			if (_alloc_buf(&p.dis->udata, info.DDATA_size) < 0)
				goto out;
		}
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace4(TR_t_alloc, 1, fd, struct_type, fields);
		return ((char *)p.dis);

	case T_UNITDATA:
		if ((p.udata = (struct t_unitdata *)
			calloc(1, (unsigned)sizeof (struct t_unitdata)))
		    == NULL)
				goto out;
		if (fields & T_ADDR) {
			if (_alloc_buf(&p.udata->addr, info.ADDR_size) < 0)
				goto out;
		}
		if (fields & T_OPT) {
			if (_alloc_buf(&p.udata->opt, info.OPT_size) < 0)
				goto out;
		}
		if (fields & T_UDATA) {
			if (_alloc_buf(&p.udata->udata, info.TSDU_size) < 0)
				goto out;
		}
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace4(TR_t_alloc, 1, fd, struct_type, fields);
		return ((char *)p.udata);

	case T_UDERROR:
		if ((p.uderr = (struct t_uderr *)
			calloc(1, (unsigned)sizeof (struct t_uderr))) == NULL)
				goto out;
		if (fields & T_ADDR) {
			if (_alloc_buf(&p.uderr->addr, info.ADDR_size) < 0)
				goto out;
		}
		if (fields & T_OPT) {
			if (_alloc_buf(&p.uderr->opt, info.OPT_size) < 0)
				goto out;
		}
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace4(TR_t_alloc, 1, fd, struct_type, fields);
		return ((char *)p.uderr);

	case T_INFO:
		if ((p.info = (struct t_info *)
			calloc(1, (unsigned)sizeof (struct t_info))) == NULL)
				goto out;
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace4(TR_t_alloc, 1, fd, struct_type, fields);
		return ((char *)p.info);

	default:
		t_errno = TSYSERR;
		MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
		trace4(TR_t_alloc, 1, fd, struct_type, fields);
		errno = EINVAL;
		return (NULL);
	}

	/*
	 * Clean up. Set errno to ENOMEM if
	 * memory could not be allocated.
	 */
out:
	if (p.caddr)
		t_free(p.caddr, struct_type);

	t_errno = TSYSERR;
	MUTEX_UNLOCK_SIGMASK(&tiptr->ti_lock, mask);
	trace4(TR_t_alloc, 1, fd, struct_type, fields);
	errno = ENOMEM;
	return (NULL);
}

static int
_alloc_buf(buf, n)
struct netbuf *buf;
{
	trace2(TR__alloc_buf, 0, n);
	switch (n)
	{
		case -1:
			if ((buf->buf = calloc(1, 1024)) == NULL) {
				int sv_errno = errno;
				trace2(TR__alloc_buf, 1, n);
				errno = sv_errno;
				return (-1);
			} else
				buf->maxlen = 1024;
			break;

		case 0:
		case -2:
			buf->buf = NULL;
			buf->maxlen = 0;
			break;

		default:
			if ((buf->buf = calloc(1, n)) == NULL) {
				int sv_errno = errno;
				trace2(TR__alloc_buf, 1, n);
				errno = sv_errno;
				return (-1);
			} else
				buf->maxlen = n;
			break;
	}
	trace2(TR__alloc_buf, 1, n);
	return (0);
}
