/*
 * Copyright (c) 1986-1991 by Sun Microsystems Inc.
 */

#ident	"@(#)clnt_dg.c	1.27	94/10/13 SMI"

#if !defined(lint) && defined(SCCSIDS)
static char sccsid[] = "@(#)clnt_dg.c 1.19 89/03/16 Copyr 1988 Sun Micro";
#endif

/*
 * Implements a connectionless client side RPC.
 */


#include <assert.h>

#include "rpc_mt.h"
#include <rpc/rpc.h>
#include <rpc/trace.h>
#include <errno.h>
#include <sys/poll.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/kstat.h>
#include <sys/time.h>
#include <stdlib.h>
#include <unistd.h>


#define	RPC_MAX_BACKOFF		30 /* seconds */

extern int _sigfillset();
extern int __rpc_timeval_to_msec();
extern bool_t xdr_opaque_auth();

static struct clnt_ops *clnt_dg_ops();
static bool_t time_not_ok();

/*
 *	This machinery implements per-fd locks for MT-safety.  It is not
 *	sufficient to do per-CLIENT handle locks for MT-safety because a
 *	user may create more than one CLIENT handle with the same fd behind
 *	it.  Therfore, we allocate an array of flags (dg_fd_locks), protected
 *	by the clnt_fd_lock mutex, and an array (dg_cv) of condition variables
 *	similarly protected.  Dg_fd_lock[fd] == 1 => a call is activte on some
 *	CLIENT handle created for that fd.
 *	The current implementation holds locks across the entire RPC and reply,
 *	including retransmissions.  Yes, this is silly, and as soon as this
 *	code is proven to work, this should be the first thing fixed.  One step
 *	at a time.
 */
extern int lock_value;
extern mutex_t clnt_fd_lock;
static int	*dg_fd_locks;
static cond_t	*dg_cv;
#define	release_fd_lock(fd, mask) {		\
	mutex_lock(&clnt_fd_lock);	\
	dg_fd_locks[fd] = 0;		\
	mutex_unlock(&clnt_fd_lock);	\
	thr_sigsetmask(SIG_SETMASK, &(mask), (sigset_t *) NULL);	\
	cond_signal(&dg_cv[fd]);	\
}

static const char mem_err_clnt_dg[] = "clnt_dg_create: out of memory";

/* VARIABLES PROTECTED BY clnt_fd_lock: dg_fd_locks, dg_cv */

/*
 * Private data kept per client handle
 */
struct cu_data {
	int			cu_fd;		/* connections fd */
	bool_t			cu_closeit;	/* opened by library */
	struct netbuf		cu_raddr;	/* remote address */
	struct timeval		cu_wait;	/* retransmit interval */
	struct timeval		cu_total;	/* total time for the call */
	struct rpc_err		cu_error;
	struct t_unitdata	*cu_tr_data;
	XDR			cu_outxdrs;
	u_int			cu_xdrpos;
	u_int			cu_sendsz;	/* send size */
	char			*cu_outbuf;
	u_int			cu_recvsz;	/* recv size */
	struct pollfd		pfdp;
	char			cu_inbuf[1];
};

/*
 * Connection less client creation returns with client handle parameters.
 * Default options are set, which the user can change using clnt_control().
 * fd should be open and bound.
 * NB: The rpch->cl_auth is initialized to null authentication.
 * 	Caller may wish to set this something more useful.
 *
 * sendsz and recvsz are the maximum allowable packet sizes that can be
 * sent and received. Normally they are the same, but they can be
 * changed to improve the program efficiency and buffer allocation.
 * If they are 0, use the transport default.
 *
 * If svcaddr is NULL, returns NULL.
 */
CLIENT *
clnt_dg_create(fd, svcaddr, program, version, sendsz, recvsz)
	int fd;				/* open file descriptor */
	const struct netbuf *svcaddr;		/* servers address */
	u_long program;			/* program number */
	u_long version;			/* version number */
	u_int sendsz;			/* buffer recv size */
	u_int recvsz;			/* buffer send size */
{
	CLIENT *cl = NULL;			/* client handle */
	register struct cu_data *cu = NULL;	/* private data */
	struct t_unitdata *tr_data;
	struct t_info tinfo;
	struct timeval now;
	struct rpc_msg call_msg;
	sigset_t mask, newmask;

	trace5(TR_clnt_dg_create, 0, program, version, sendsz, recvsz);

	_sigfillset(&newmask);
	thr_sigsetmask(SIG_SETMASK, &newmask, &mask);
	mutex_lock(&clnt_fd_lock);
	if (dg_fd_locks == (int *) NULL) {
		int cv_allocsz, fd_allocsz;
		int dtbsize = __rpc_dtbsize();

		fd_allocsz = dtbsize * sizeof (int);
		dg_fd_locks = (int *) mem_alloc(fd_allocsz);
		if (dg_fd_locks == (int *) NULL) {
			mutex_unlock(&clnt_fd_lock);
			thr_sigsetmask(SIG_SETMASK, &(mask), NULL);
			goto err1;
		} else
			memset(dg_fd_locks, '\0', fd_allocsz);

		assert(dg_cv == (cond_t *) NULL);
		cv_allocsz = dtbsize * sizeof (cond_t);
		dg_cv = (cond_t *) mem_alloc(cv_allocsz);
		if (dg_cv == (cond_t *) NULL) {
			mem_free(dg_fd_locks, fd_allocsz);
			dg_fd_locks = (int *) NULL;
			mutex_unlock(&clnt_fd_lock);
			thr_sigsetmask(SIG_SETMASK, &(mask), NULL);
			goto err1;
		} else {
			int i;

			for (i = 0; i < dtbsize; i++)
				cond_init(&dg_cv[i], 0, (void *) 0);
		}
	} else
		assert(dg_cv != (cond_t *) NULL);

	mutex_unlock(&clnt_fd_lock);
	thr_sigsetmask(SIG_SETMASK, &(mask), NULL);

	if (svcaddr == (struct netbuf *)NULL) {
		rpc_createerr.cf_stat = RPC_UNKNOWNADDR;
		trace3(TR_clnt_dg_create, 1, program, version);
		return ((CLIENT *)NULL);
	}
	if (t_getinfo(fd, &tinfo) == -1) {
		rpc_createerr.cf_stat = RPC_TLIERROR;
		rpc_createerr.cf_error.re_errno = 0;
		rpc_createerr.cf_error.re_terrno = t_errno;
		trace3(TR_clnt_dg_create, 1, program, version);
		return ((CLIENT *)NULL);
	}
	/*
	 * Find the receive and the send size
	 */
	sendsz = __rpc_get_t_size((int)sendsz, tinfo.tsdu);
	recvsz = __rpc_get_t_size((int)recvsz, tinfo.tsdu);
	if ((sendsz == 0) || (recvsz == 0)) {
		rpc_createerr.cf_stat = RPC_TLIERROR; /* XXX */
		rpc_createerr.cf_error.re_errno = 0;
		rpc_createerr.cf_error.re_terrno = 0;
		trace3(TR_clnt_dg_create, 1, program, version);
		return ((CLIENT *)NULL);
	}

	if ((cl = (CLIENT *)mem_alloc(sizeof (CLIENT))) == (CLIENT *)NULL)
		goto err1;
	/*
	 * Should be multiple of 4 for XDR.
	 */
	sendsz = ((sendsz + 3) / 4) * 4;
	recvsz = ((recvsz + 3) / 4) * 4;
	cu = (struct cu_data *)mem_alloc(sizeof (*cu) + sendsz + recvsz);
	if (cu == (struct cu_data *)NULL)
		goto err1;
	if ((cu->cu_raddr.buf = mem_alloc(svcaddr->len)) == NULL)
		goto err1;
	(void) memcpy(cu->cu_raddr.buf, svcaddr->buf, (int)svcaddr->len);
	cu->cu_raddr.len = cu->cu_raddr.maxlen = svcaddr->len;
	cu->cu_outbuf = &cu->cu_inbuf[recvsz];
	/* Other values can also be set through clnt_control() */
	cu->cu_wait.tv_sec = 15;	/* heuristically chosen */
	cu->cu_wait.tv_usec = 0;
	cu->cu_total.tv_sec = -1;
	cu->cu_total.tv_usec = -1;
	cu->cu_sendsz = sendsz;
	cu->cu_recvsz = recvsz;
	(void) gettimeofday(&now, (struct timezone *)NULL);
	call_msg.rm_xid = getpid() ^ now.tv_sec ^ now.tv_usec;
	call_msg.rm_call.cb_prog = program;
	call_msg.rm_call.cb_vers = version;
	xdrmem_create(&(cu->cu_outxdrs), cu->cu_outbuf, sendsz, XDR_ENCODE);
	if (! xdr_callhdr(&(cu->cu_outxdrs), &call_msg)) {
		rpc_createerr.cf_stat = RPC_CANTENCODEARGS;  /* XXX */
		rpc_createerr.cf_error.re_errno = 0;
		rpc_createerr.cf_error.re_terrno = 0;
		goto err2;
	}
	cu->cu_xdrpos = XDR_GETPOS(&(cu->cu_outxdrs));

	tr_data = (struct t_unitdata *)t_alloc(fd,
				T_UNITDATA, T_ADDR | T_OPT);
	if (tr_data == (struct t_unitdata *)NULL) {
		goto err1;
	}
	tr_data->udata.maxlen = cu->cu_recvsz;
	tr_data->udata.buf = cu->cu_inbuf;
	cu->cu_tr_data = tr_data;

	/*
	 * By default, closeit is always FALSE. It is users responsibility
	 * to do a t_close on it, else the user may use clnt_control
	 * to let clnt_destroy do it for him/her.
	 */
	cu->cu_closeit = FALSE;
	cu->cu_fd = fd;
	cl->cl_ops = clnt_dg_ops();
	cl->cl_private = (caddr_t)cu;
	cl->cl_auth = authnone_create();
	cl->cl_tp = (char *) NULL;
	cl->cl_netid = (char *) NULL;
	cu->pfdp.fd = cu->cu_fd;
	cu->pfdp.events = POLLIN | POLLPRI | POLLRDNORM | POLLRDBAND;
	trace3(TR_clnt_dg_create, 1, program, version);
	return (cl);
err1:
	(void) syslog(LOG_ERR, mem_err_clnt_dg);
	rpc_createerr.cf_stat = RPC_SYSTEMERROR;
	rpc_createerr.cf_error.re_errno = errno;
	rpc_createerr.cf_error.re_terrno = 0;
err2:
	if (cl) {
		mem_free((caddr_t)cl, sizeof (CLIENT));
		if (cu) {
			mem_free(cu->cu_raddr.buf, cu->cu_raddr.len);
			mem_free((caddr_t)cu, sizeof (*cu) + sendsz + recvsz);
		}
	}
	trace3(TR_clnt_dg_create, 1, program, version);
	return ((CLIENT *)NULL);
}

static enum clnt_stat
clnt_dg_call(cl, proc, xargs, argsp, xresults, resultsp, utimeout)
	register CLIENT	*cl;		/* client handle */
	u_long		proc;		/* procedure number */
	xdrproc_t	xargs;		/* xdr routine for args */
	caddr_t		argsp;		/* pointer to args */
	xdrproc_t	xresults;	/* xdr routine for results */
	caddr_t		resultsp;	/* pointer to results */
	struct timeval	utimeout;	/* seconds to wait before giving up */
{
	register struct cu_data *cu = (struct cu_data *)cl->cl_private;
	register XDR *xdrs;
	register int outlen;
	struct rpc_msg reply_msg;
	XDR reply_xdrs;
	struct timeval time_waited;
	bool_t ok;
	int nrefreshes = 2;		/* number of times to refresh cred */
	struct timeval timeout;
	struct timeval retransmit_time;
	struct timeval poll_time;
	struct timeval startime, curtime;
	struct t_unitdata tu_data;
	int res;			/* result of operations */
	int dtbsize = __rpc_dtbsize();
	sigset_t mask, newmask;
	trace3(TR_clnt_dg_call, 0, cl, proc);

	_sigfillset(&newmask);
	thr_sigsetmask(SIG_SETMASK, &newmask, &mask);
	mutex_lock(&clnt_fd_lock);
	while (dg_fd_locks[cu->cu_fd])
		cond_wait(&dg_cv[cu->cu_fd], &clnt_fd_lock);
	dg_fd_locks[cu->cu_fd] = lock_value;
	mutex_unlock(&clnt_fd_lock);
	if (cu->cu_total.tv_usec == -1) {
		timeout = utimeout;	/* use supplied timeout */
	} else {
		timeout = cu->cu_total;	/* use default timeout */
	}

	time_waited.tv_sec = 0;
	time_waited.tv_usec = 0;
	retransmit_time = cu->cu_wait;

	tu_data.addr = cu->cu_raddr;

call_again:
	xdrs = &(cu->cu_outxdrs);
	xdrs->x_op = XDR_ENCODE;
	XDR_SETPOS(xdrs, cu->cu_xdrpos);
	/*
	 * the transaction is the first thing in the out buffer
	 */
	(*(u_long *)(cu->cu_outbuf))++;
	if ((! XDR_PUTLONG(xdrs, (long *)&proc)) ||
	    (! AUTH_MARSHALL(cl->cl_auth, xdrs)) ||
	    (! (*xargs)(xdrs, argsp))) {
		release_fd_lock(cu->cu_fd, mask);
		trace2(TR_clnt_dg_call, 1, cl);
		return (cu->cu_error.re_status = RPC_CANTENCODEARGS);
	}
	outlen = (int)XDR_GETPOS(xdrs);

send_again:
	tu_data.udata.buf = cu->cu_outbuf;
	tu_data.udata.len = outlen;
	tu_data.opt.len = 0;
	if (t_sndudata(cu->cu_fd, &tu_data) == -1) {
		cu->cu_error.re_terrno = t_errno;
		cu->cu_error.re_errno = errno;
		release_fd_lock(cu->cu_fd, mask);
		trace2(TR_clnt_dg_call, 1, cl);
		return (cu->cu_error.re_status = RPC_CANTSEND);
	}

	/*
	 * Hack to provide rpc-based message passing
	 */
	if (timeout.tv_sec == 0 && timeout.tv_usec == 0) {
		release_fd_lock(cu->cu_fd, mask);
		trace2(TR_clnt_dg_call, 1, cl);
		return (cu->cu_error.re_status = RPC_TIMEDOUT);
	}
	/*
	 * sub-optimal code appears here because we have
	 * some clock time to spare while the packets are in flight.
	 * (We assume that this is actually only executed once.)
	 */
	reply_msg.acpted_rply.ar_verf = _null_auth;
	reply_msg.acpted_rply.ar_results.where = resultsp;
	reply_msg.acpted_rply.ar_results.proc = xresults;

	/*
	 * Set polling time so that we don't wait for
	 * longer than specified by the total time to wait,
	 * or the retransmit time.
	 */
	poll_time.tv_sec = timeout.tv_sec - time_waited.tv_sec;
	poll_time.tv_usec = timeout.tv_usec - time_waited.tv_usec;
	while (poll_time.tv_usec < 0) {
		poll_time.tv_usec += 1000000;
		poll_time.tv_sec--;
	}

	if (poll_time.tv_sec < 0 || (poll_time.tv_sec == 0 &&
					poll_time.tv_usec == 0)) {
		/*
		 * this could happen if time_waited >= timeout
		 */
		release_fd_lock(cu->cu_fd, mask);
		trace2(TR_clnt_dg_call, 1, cl);
		return (cu->cu_error.re_status = RPC_TIMEDOUT);
	}

	if (poll_time.tv_sec > retransmit_time.tv_sec ||
			(poll_time.tv_sec == retransmit_time.tv_sec &&
				poll_time.tv_usec > retransmit_time.tv_usec))
		poll_time = retransmit_time;


	for (;;) {
		int fds;

		(void) gettimeofday(&startime, NULL);

		switch (fds = poll(&cu->pfdp, 1,
				__rpc_timeval_to_msec(&poll_time))) {
		case -1:
			if (errno != EINTR && errno != EAGAIN) {
				cu->cu_error.re_errno = errno;
				cu->cu_error.re_terrno = 0;
				release_fd_lock(cu->cu_fd, mask);
				trace2(TR_clnt_dg_call, 1, cl);
				return (cu->cu_error.re_status = RPC_CANTRECV);
			}
			/*FALLTHROUGH*/

		case 0:
			/*
			 * update time waited
			 */
			(void) gettimeofday(&curtime, NULL);
			time_waited.tv_sec += curtime.tv_sec - startime.tv_sec;
			time_waited.tv_usec += curtime.tv_usec -
							startime.tv_usec;
			while (time_waited.tv_usec >= 1000000) {
				time_waited.tv_usec -= 1000000;
				time_waited.tv_sec++;
			}
			while (time_waited.tv_usec < 0) {
				time_waited.tv_usec += 1000000;
				time_waited.tv_sec--;
			}

			/*
			 * decrement time left to poll by same amount
			 */
			poll_time.tv_sec -= curtime.tv_sec - startime.tv_sec;
			poll_time.tv_usec -= curtime.tv_usec - startime.tv_usec;
			while (poll_time.tv_usec >= 1000000) {
				poll_time.tv_usec -= 1000000;
				poll_time.tv_sec++;
			}
			while (poll_time.tv_usec < 0) {
				poll_time.tv_usec += 1000000;
				poll_time.tv_sec--;
			}

			/*
			 * if there's time left to poll, poll again
			 */
			if (poll_time.tv_sec > 0 ||
					(poll_time.tv_sec == 0 &&
						poll_time.tv_usec > 0))
				continue;

			/*
			 * if there's more time left, retransmit;
			 * otherwise, return timeout error
			 */
			if (time_waited.tv_sec < timeout.tv_sec ||
				(time_waited.tv_sec == timeout.tv_sec &&
				    time_waited.tv_usec < timeout.tv_usec)) {
				/*
				 * update retransmit_time
				 */
				retransmit_time.tv_usec *= 2;
				retransmit_time.tv_sec *= 2;
				while (retransmit_time.tv_usec >= 1000000) {
					retransmit_time.tv_usec -= 1000000;
					retransmit_time.tv_sec++;
				}
				if (retransmit_time.tv_sec >= RPC_MAX_BACKOFF) {
					retransmit_time.tv_sec =
							RPC_MAX_BACKOFF;
					retransmit_time.tv_usec = 0;
				}
				/*
				 * redo AUTH_MARSHAL if flavor is AUTH_DES.
				 */
				if (cl->cl_auth->ah_cred.oa_flavor == AUTH_DES)
					goto call_again;
				else
					goto send_again;
			}
			release_fd_lock(cu->cu_fd, mask);
			trace2(TR_clnt_dg_call, 1, cl);
			return (cu->cu_error.re_status = RPC_TIMEDOUT);

		default:
			break;
		}

		if (cu->pfdp.revents & POLLNVAL || (cu->pfdp.revents == 0)) {
			cu->cu_error.re_status = RPC_CANTRECV;
			/*
			 *	Note:  we're faking errno here because we
			 *	previously would have expected select() to
			 *	return -1 with errno EBADF.  Poll(BA_OS)
			 *	returns 0 and sets the POLLNVAL revents flag
			 *	instead.
			 */
			cu->cu_error.re_errno = errno = EBADF;
			release_fd_lock(cu->cu_fd, mask);
			trace2(TR_clnt_dg_call, 1, cl);
			return (-1);
		}

		/* We have some data now */
		do {
			int moreflag;		/* flag indicating more data */

			moreflag = 0;
			if (errno == EINTR) {
				/*
				 * Must make sure errno was not already
				 * EINTR in case t_rcvudata() returns -1.
				 * This way will only stay in the loop
				 * if getmsg() sets errno to EINTR.
				 */
				errno = 0;
			}
			res = t_rcvudata(cu->cu_fd, cu->cu_tr_data, &moreflag);
			if (moreflag & T_MORE) {
				/*
				 * Drop this packet. I aint got any
				 * more space.
				 */
				res = -1;
				/* I should not really be doing this */
				errno = 0;
				/*
				 * XXX: Not really Buffer overflow in the
				 * sense of TLI.
				 */
				t_errno = TBUFOVFLW;
			}
		} while (res < 0 && errno == EINTR);
		if (res < 0) {
#ifdef sun
			if (errno == EWOULDBLOCK)
#else
			if (errno == EAGAIN)
#endif
				continue;
			if (t_errno == TLOOK) {
				int old;

				old = t_errno;
				if (t_rcvuderr(cu->cu_fd, NULL) == 0)
					continue;
				else
					cu->cu_error.re_terrno = old;
			} else {
				cu->cu_error.re_terrno = t_errno;
			}
			cu->cu_error.re_errno = errno;
			release_fd_lock(cu->cu_fd, mask);
			trace2(TR_clnt_dg_call, 1, cl);
			return (cu->cu_error.re_status = RPC_CANTRECV);
		}
		if (cu->cu_tr_data->udata.len < sizeof (u_long))
			continue;
		/* see if reply transaction id matches sent id */
		if (*((u_long *)(cu->cu_inbuf)) != *((u_long *)(cu->cu_outbuf)))
			continue;
		/* we now assume we have the proper reply */
		break;
	}

	/*
	 * now decode and validate the response
	 */

	xdrmem_create(&reply_xdrs, cu->cu_inbuf,
		(u_int)cu->cu_tr_data->udata.len, XDR_DECODE);
	ok = xdr_replymsg(&reply_xdrs, &reply_msg);
	/* XDR_DESTROY(&reply_xdrs);	save a few cycles on noop destroy */
	if (ok) {
		if ((reply_msg.rm_reply.rp_stat == MSG_ACCEPTED) &&
			(reply_msg.acpted_rply.ar_stat == SUCCESS))
			cu->cu_error.re_status = RPC_SUCCESS;
		else
			__seterr_reply(&reply_msg, &(cu->cu_error));

		if (cu->cu_error.re_status == RPC_SUCCESS) {
			if (! AUTH_VALIDATE(cl->cl_auth,
					    &reply_msg.acpted_rply.ar_verf)) {
				cu->cu_error.re_status = RPC_AUTHERROR;
				cu->cu_error.re_why = AUTH_INVALIDRESP;
			}
			if (reply_msg.acpted_rply.ar_verf.oa_base != NULL) {
				xdrs->x_op = XDR_FREE;
				(void) xdr_opaque_auth(xdrs,
					&(reply_msg.acpted_rply.ar_verf));
			}
		}		/* end successful completion */
		/*
		 * If unsuccesful AND error is an authentication error
		 * then refresh credentials and try again, else break
		 */
		else if (cu->cu_error.re_status == RPC_AUTHERROR)
			/* maybe our credentials need to be refreshed ... */
			if (nrefreshes > 0 && AUTH_REFRESH(cl->cl_auth)) {
				nrefreshes--;
				goto call_again;
			}
		/* end of unsuccessful completion */
	}	/* end of valid reply message */
	else {
		cu->cu_error.re_status = RPC_CANTDECODERES;

	}
	release_fd_lock(cu->cu_fd, mask);
	trace2(TR_clnt_dg_call, 1, cl);
	return (cu->cu_error.re_status);
}

static void
clnt_dg_geterr(cl, errp)
	CLIENT *cl;
	struct rpc_err *errp;
{
	register struct cu_data *cu = (struct cu_data *)cl->cl_private;

	trace2(TR_clnt_dg_geterr, 0, cl);
	*errp = cu->cu_error;
	trace2(TR_clnt_dg_geterr, 1, cl);
}

static bool_t
clnt_dg_freeres(cl, xdr_res, res_ptr)
	CLIENT *cl;
	xdrproc_t xdr_res;
	caddr_t res_ptr;
{
	register struct cu_data *cu = (struct cu_data *)cl->cl_private;
	register XDR *xdrs = &(cu->cu_outxdrs);
	bool_t dummy;
	sigset_t mask, newmask;

	trace2(TR_clnt_dg_freeres, 0, cl);

	_sigfillset(&newmask);
	thr_sigsetmask(SIG_SETMASK, &newmask, &mask);
	mutex_lock(&clnt_fd_lock);
	while (dg_fd_locks[cu->cu_fd])
		cond_wait(&dg_cv[cu->cu_fd], &clnt_fd_lock);
	xdrs->x_op = XDR_FREE;
	dummy = (*xdr_res)(xdrs, res_ptr);
	mutex_unlock(&clnt_fd_lock);
	thr_sigsetmask(SIG_SETMASK, &mask, NULL);
	cond_signal(&dg_cv[cu->cu_fd]);
	trace2(TR_clnt_dg_freeres, 1, cl);
	return (dummy);
}

static void
clnt_dg_abort(/* h */)
	/* CLIENT *h; */
{
	trace1(TR_clnt_dg_abort, 0);
	trace1(TR_clnt_dg_abort, 1);
}

static bool_t
clnt_dg_control(cl, request, info)
	CLIENT *cl;
	int request;
	char *info;
{
	register struct cu_data *cu = (struct cu_data *)cl->cl_private;
	struct netbuf *addr;
	sigset_t mask, newmask;
	trace3(TR_clnt_dg_control, 0, cl, request);

	_sigfillset(&newmask);
	thr_sigsetmask(SIG_SETMASK, &newmask, &mask);
	mutex_lock(&clnt_fd_lock);
	while (dg_fd_locks[cu->cu_fd])
		cond_wait(&dg_cv[cu->cu_fd], &clnt_fd_lock);
	dg_fd_locks[cu->cu_fd] = lock_value;
	mutex_unlock(&clnt_fd_lock);
	switch (request) {
	case CLSET_FD_CLOSE:
		cu->cu_closeit = TRUE;
		release_fd_lock(cu->cu_fd, mask);
		trace2(TR_clnt_dg_control, 1, cl);
		return (TRUE);
	case CLSET_FD_NCLOSE:
		cu->cu_closeit = FALSE;
		release_fd_lock(cu->cu_fd, mask);
		trace2(TR_clnt_dg_control, 1, cl);
		return (TRUE);
	}

	/* for other requests which use info */
	if (info == NULL) {
		release_fd_lock(cu->cu_fd, mask);
		trace2(TR_clnt_dg_control, 1, cl);
		return (FALSE);
	}
	switch (request) {
	case CLSET_TIMEOUT:
		if (time_not_ok((struct timeval *)info)) {
			release_fd_lock(cu->cu_fd, mask);
			trace2(TR_clnt_dg_control, 1, cl);
			return (FALSE);
		}
		cu->cu_total = *(struct timeval *)info;
		break;
	case CLGET_TIMEOUT:
		*(struct timeval *)info = cu->cu_total;
		break;
	case CLGET_SERVER_ADDR:		/* Give him the fd address */
		/* Now obsolete. Only for backword compatibility */
		(void) memcpy(info, cu->cu_raddr.buf, (int)cu->cu_raddr.len);
		break;
	case CLSET_RETRY_TIMEOUT:
		if (time_not_ok((struct timeval *)info)) {
			release_fd_lock(cu->cu_fd, mask);
			trace2(TR_clnt_dg_control, 1, cl);
			return (FALSE);
		}
		cu->cu_wait = *(struct timeval *)info;
		break;
	case CLGET_RETRY_TIMEOUT:
		*(struct timeval *)info = cu->cu_wait;
		break;
	case CLGET_FD:
		*(int *)info = cu->cu_fd;
		break;
	case CLGET_SVC_ADDR:
		*(struct netbuf *)info = cu->cu_raddr;
		break;
	case CLSET_SVC_ADDR:		/* set to new address */
		addr = (struct netbuf *)info;
		if (cu->cu_raddr.maxlen < addr->len) {
			mem_free(cu->cu_raddr.buf, cu->cu_raddr.maxlen);
			if ((cu->cu_raddr.buf = mem_alloc(addr->len)) == NULL) {
				release_fd_lock(cu->cu_fd, mask);
				trace2(TR_clnt_dg_control, 1, cl);
				return (FALSE);
			}
			cu->cu_raddr.maxlen = addr->len;
		}
		cu->cu_raddr.len = addr->len;
		(void) memcpy(cu->cu_raddr.buf, addr->buf, addr->len);
		break;
	case CLGET_XID:
		/*
		 * use the knowledge that xid is the
		 * first element in the call structure *.
		 * This will get the xid of the PREVIOUS call
		 */
		*(u_long *)info = ntohl(*(u_long *)cu->cu_outbuf);
		break;

	case CLSET_XID:
		/* This will set the xid of the NEXT call */
		*(u_long *)cu->cu_outbuf =  htonl(*(u_long *)info - 1);
		/* decrement by 1 as clnt_dg_call() increments once */
		break;

	case CLGET_VERS:
		/*
		 * This RELIES on the information that, in the call body,
		 * the version number field is the fifth field from the
		 * begining of the RPC header. MUST be changed if the
		 * call_struct is changed
		 */
		*(u_long *)info = ntohl(*(u_long *)(cu->cu_outbuf +
						    4 * BYTES_PER_XDR_UNIT));
		break;

	case CLSET_VERS:
		*(u_long *)(cu->cu_outbuf + 4 * BYTES_PER_XDR_UNIT)
			= htonl(*(u_long *)info);
		break;

	case CLGET_PROG:
		/*
		 * This RELIES on the information that, in the call body,
		 * the program number field is the fourth field from the
		 * begining of the RPC header. MUST be changed if the
		 * call_struct is changed
		 */
		*(u_long *)info = ntohl(*(u_long *)(cu->cu_outbuf +
						    3 * BYTES_PER_XDR_UNIT));
		break;

	case CLSET_PROG:
		*(u_long *)(cu->cu_outbuf + 3 * BYTES_PER_XDR_UNIT)
			= htonl(*(u_long *)info);
		break;

	default:
		release_fd_lock(cu->cu_fd, mask);
		trace2(TR_clnt_dg_control, 1, cl);
		return (FALSE);
	}
	release_fd_lock(cu->cu_fd, mask);
	trace2(TR_clnt_dg_control, 1, cl);
	return (TRUE);
}

static void
clnt_dg_destroy(cl)
	CLIENT *cl;
{
	register struct cu_data *cu = (struct cu_data *)cl->cl_private;
	int cu_fd = cu->cu_fd;
	sigset_t mask, newmask;

	trace2(TR_clnt_dg_destroy, 0, cl);
	_sigfillset(&newmask);
	thr_sigsetmask(SIG_SETMASK, &newmask, &mask);
	mutex_lock(&clnt_fd_lock);
	while (dg_fd_locks[cu_fd])
		cond_wait(&dg_cv[cu_fd], &clnt_fd_lock);
	if (cu->cu_closeit)
		(void) t_close(cu_fd);
	XDR_DESTROY(&(cu->cu_outxdrs));
	cu->cu_tr_data->udata.buf = NULL;
	(void) t_free((char *)cu->cu_tr_data, T_UNITDATA);
	mem_free(cu->cu_raddr.buf, cu->cu_raddr.len);
	mem_free((caddr_t)cu,
		(sizeof (*cu) + cu->cu_sendsz + cu->cu_recvsz));
	if (cl->cl_netid && cl->cl_netid[0])
		mem_free(cl->cl_netid, strlen(cl->cl_netid) +1);
	if (cl->cl_tp && cl->cl_tp[0])
		mem_free(cl->cl_tp, strlen(cl->cl_tp) +1);
	mem_free((caddr_t)cl, sizeof (CLIENT));
	mutex_unlock(&clnt_fd_lock);
	thr_sigsetmask(SIG_SETMASK, &mask, NULL);
	cond_signal(&dg_cv[cu_fd]);
	trace2(TR_clnt_dg_destroy, 1, cl);
}

static struct clnt_ops *
clnt_dg_ops()
{
	static struct clnt_ops ops;
	extern mutex_t	ops_lock;
	sigset_t mask, newmask;

/* VARIABLES PROTECTED BY ops_lock: ops */

	trace1(TR_clnt_dg_ops, 0);
	_sigfillset(&newmask);
	thr_sigsetmask(SIG_SETMASK, &newmask, &mask);
	mutex_lock(&ops_lock);
	if (ops.cl_call == NULL) {
		ops.cl_call = clnt_dg_call;
		ops.cl_abort = clnt_dg_abort;
		ops.cl_geterr = clnt_dg_geterr;
		ops.cl_freeres = clnt_dg_freeres;
		ops.cl_destroy = clnt_dg_destroy;
		ops.cl_control = clnt_dg_control;
	}
	mutex_unlock(&ops_lock);
	thr_sigsetmask(SIG_SETMASK, &mask, NULL);
	trace1(TR_clnt_dg_ops, 1);
	return (&ops);
}

/*
 * Make sure that the time is not garbage.  -1 value is allowed.
 */
static bool_t
time_not_ok(t)
	struct timeval *t;
{
	trace1(TR_time_not_ok, 0);
	trace1(TR_time_not_ok, 1);
	return (t->tv_sec < -1 || t->tv_sec > 100000000 ||
		t->tv_usec < -1 || t->tv_usec > 1000000);
}
