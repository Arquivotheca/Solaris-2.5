/*
 * Copyright (c) 1986-1991 by Sun Microsystems Inc.
 */

#ident	"@(#)clnt_vc.c	1.30	95/06/28 SMI"

#if !defined(lint) && defined(SCCSIDS)
static char sccsid[] = "@(#)clnt_vc.c 1.19 89/03/16 Copyr 1988 Sun Micro";
#endif

/*
 * clnt_vc.c
 *
 * Implements a connectionful client side RPC.
 *
 * Connectionful RPC supports 'batched calls'.
 * A sequence of calls may be batched-up in a send buffer. The rpc call
 * return immediately to the client even though the call was not necessarily
 * sent. The batching occurs if the results' xdr routine is NULL (0) AND
 * the rpc timeout value is zero (see clnt.h, rpc).
 *
 * Clients should NOT casually batch calls that in fact return results; that
 * is the server side should be aware that a call is batched and not produce
 * any return message. Batched calls that produce many result messages can
 * deadlock (netlock) the client and the server....
 */


#include "rpc_mt.h"
#include <assert.h>
#include <rpc/rpc.h>
#include <rpc/trace.h>
#include <errno.h>
#include <sys/byteorder.h>
#include <sys/mkdev.h>
#include <sys/poll.h>
#include <syslog.h>
#include <stdlib.h>
#include <unistd.h>

#define	MCALL_MSG_SIZE 24
#ifndef MIN
#define	MIN(a, b)	(((a) < (b)) ? (a) : (b))
#endif

extern int _sigfillset();
extern int __rpc_timeval_to_msec();
extern int __rpc_select_to_poll();
extern bool_t xdr_opaque_auth();

static struct clnt_ops	*clnt_vc_ops();
#ifdef __STDC__
static int		read_vc(void *, caddr_t, int);
static int		write_vc(void *, caddr_t, int);
#else
static int		read_vc();
static int		write_vc();
#endif
static int		t_rcvall();
static bool_t		time_not_ok();
static bool_t		set_up_connection();

/*
 *	This machinery implements per-fd locks for MT-safety.  It is not
 *	sufficient to do per-CLIENT handle locks for MT-safety because a
 *	user may create more than one CLIENT handle with the same fd behind
 *	it.  Therfore, we allocate an array of flags (vc_fd_locks), protected
 *	by the clnt_fd_lock mutex, and an array (vc_cv) of condition variables
 *	similarly protected.  Vc_fd_lock[fd] == 1 => a call is activte on some
 *	CLIENT handle created for that fd.
 *	The current implementation holds locks across the entire RPC and reply.
 *	Yes, this is silly, and as soon as this code is proven to work, this
 *	should be the first thing fixed.  One step at a time.
 */
extern int lock_value;
extern mutex_t	clnt_fd_lock;
static int	*vc_fd_locks;
static cond_t	*vc_cv;
#define	release_fd_lock(fd, mask) {		\
	mutex_lock(&clnt_fd_lock);	\
	vc_fd_locks[fd] = 0;		\
	mutex_unlock(&clnt_fd_lock);	\
	thr_sigsetmask(SIG_SETMASK, &(mask), (sigset_t *) NULL);	\
	cond_signal(&vc_cv[fd]);	\
}
static const char clnt_vc_errstr[] = "%s : %s";
static const char clnt_vc_str[] = "clnt_vc_create";
static const char clnt_read_vc_str[] = "read_vc";
static const char __no_mem_str[] = "out of memory";

/* VARIABLES PROTECTED BY clnt_fd_lock: vc_fd_locks, vc_cv */

/*
 * Private data structure
 */
struct ct_data {
	int		ct_fd;		/* connection's fd */
	bool_t		ct_closeit;	/* close it on destroy */
	long		ct_tsdu;	/* size of tsdu */
	int		ct_wait;	/* wait interval in milliseconds */
	bool_t		ct_waitset;	/* wait set by clnt_control? */
	struct netbuf	ct_addr;	/* remote addr */
	struct rpc_err	ct_error;
	char		ct_mcall[MCALL_MSG_SIZE]; /* marshalled callmsg */
	u_int		ct_mpos;	/* pos after marshal */
	XDR		ct_xdrs;	/* XDR stream */
};

/*
 * Create a client handle for a connection.
 * Default options are set, which the user can change using clnt_control()'s.
 * The rpc/vc package does buffering similar to stdio, so the client
 * must pick send and receive buffer sizes, 0 => use the default.
 * NB: fd is copied into a private area.
 * NB: The rpch->cl_auth is set null authentication. Caller may wish to
 * set this something more useful.
 *
 * fd should be open and bound.
 */
CLIENT *
clnt_vc_create(fd, svcaddr, prog, vers, sendsz, recvsz)
	register int fd;		/* open file descriptor */
	const struct netbuf *svcaddr;		/* servers address */
	u_long prog;			/* program number */
	u_long vers;			/* version number */
	u_int sendsz;			/* buffer recv size */
	u_int recvsz;			/* buffer send size */
{
	CLIENT *cl;			/* client handle */
	register struct ct_data *ct;	/* private data */
	struct timeval now;
	struct rpc_msg call_msg;
	struct t_info tinfo;
	sigset_t mask, newmask;

	trace5(TR_clnt_vc_create, 0, prog, vers, sendsz, recvsz);

	cl = (CLIENT *)mem_alloc(sizeof (*cl));
	ct = (struct ct_data *)mem_alloc(sizeof (*ct));
	if ((cl == (CLIENT *)NULL) || (ct == (struct ct_data *)NULL)) {
		(void) syslog(LOG_ERR, clnt_vc_errstr,
				clnt_vc_str, __no_mem_str);
		rpc_createerr.cf_stat = RPC_SYSTEMERROR;
		rpc_createerr.cf_error.re_errno = errno;
		rpc_createerr.cf_error.re_terrno = 0;
		goto err;
	}
	ct->ct_addr.buf = NULL;
	_sigfillset(&newmask);
	thr_sigsetmask(SIG_SETMASK, &newmask, &mask);
	mutex_lock(&clnt_fd_lock);
	if (vc_fd_locks == (int *) NULL) {
		int cv_allocsz, fd_allocsz;
		int dtbsize = __rpc_dtbsize();

		fd_allocsz = dtbsize * sizeof (int);
		vc_fd_locks = (int *) mem_alloc(fd_allocsz);
		if (vc_fd_locks == (int *) NULL) {
			mutex_unlock(&clnt_fd_lock);
			thr_sigsetmask(SIG_SETMASK, &(mask), NULL);
			goto err;
		} else
			memset(vc_fd_locks, '\0', fd_allocsz);

		assert(vc_cv == (cond_t *) NULL);
		cv_allocsz = dtbsize * sizeof (cond_t);
		vc_cv = (cond_t *) mem_alloc(cv_allocsz);
		if (vc_cv == (cond_t *) NULL) {
			mem_free(vc_fd_locks, fd_allocsz);
			vc_fd_locks = (int *) NULL;
			mutex_unlock(&clnt_fd_lock);
			thr_sigsetmask(SIG_SETMASK, &(mask), NULL);
			goto err;
		} else {
			int i;

			for (i = 0; i < dtbsize; i++)
				cond_init(&vc_cv[i], 0, (void *) 0);
		}
	} else
		assert(vc_cv != (cond_t *) NULL);
	if (set_up_connection(fd, svcaddr, ct) == FALSE) {
		mutex_unlock(&clnt_fd_lock);
		thr_sigsetmask(SIG_SETMASK, &(mask), NULL);
		goto err;
	}
	mutex_unlock(&clnt_fd_lock);
	thr_sigsetmask(SIG_SETMASK, &(mask), NULL);

	/*
	 * Set up other members of private data struct
	 */
	ct->ct_fd = fd;
	/*
	 * The actual value will be set by clnt_call or clnt_control
	 */
	ct->ct_wait = 30000;
	ct->ct_waitset = FALSE;
	/*
	 * By default, closeit is always FALSE. It is users responsibility
	 * to do a t_close on it, else the user may use clnt_control
	 * to let clnt_destroy do it for him/her.
	 */
	ct->ct_closeit = FALSE;

	/*
	 * Initialize call message
	 */
	(void) gettimeofday(&now, (struct timezone *)0);
	call_msg.rm_xid = getpid() ^ now.tv_sec ^ now.tv_usec;
	call_msg.rm_call.cb_prog = prog;
	call_msg.rm_call.cb_vers = vers;

	/*
	 * pre-serialize the static part of the call msg and stash it away
	 */
	xdrmem_create(&(ct->ct_xdrs), ct->ct_mcall, MCALL_MSG_SIZE, XDR_ENCODE);
	if (! xdr_callhdr(&(ct->ct_xdrs), &call_msg)) {
		goto err;
	}
	ct->ct_mpos = XDR_GETPOS(&(ct->ct_xdrs));
	XDR_DESTROY(&(ct->ct_xdrs));

	if (t_getinfo(fd, &tinfo) == -1) {
		rpc_createerr.cf_stat = RPC_TLIERROR;
		rpc_createerr.cf_error.re_terrno = t_errno;
		rpc_createerr.cf_error.re_errno = 0;
		goto err;
	}
	/*
	 * Find the receive and the send size
	 */
	sendsz = __rpc_get_t_size((int)sendsz, tinfo.tsdu);
	recvsz = __rpc_get_t_size((int)recvsz, tinfo.tsdu);
	if ((sendsz == 0) || (recvsz == 0)) {
		rpc_createerr.cf_stat = RPC_TLIERROR;
		rpc_createerr.cf_error.re_terrno = 0;
		rpc_createerr.cf_error.re_errno = 0;
		goto err;
	}
	ct->ct_tsdu = tinfo.tsdu;
	/*
	 * Create a client handle which uses xdrrec for serialization
	 * and authnone for authentication.
	 */
	xdrrec_create(&(ct->ct_xdrs), sendsz, recvsz, (caddr_t)ct,
			read_vc, write_vc);
	cl->cl_ops = clnt_vc_ops();
	cl->cl_private = (caddr_t) ct;
	cl->cl_auth = authnone_create();
	cl->cl_tp = (char *) NULL;
	cl->cl_netid = (char *) NULL;
	trace3(TR_clnt_vc_create, 1, prog, vers);
	return (cl);

err:
	if (cl) {
		if (ct) {
			if (ct->ct_addr.len)
				mem_free(ct->ct_addr.buf, ct->ct_addr.len);
			mem_free((caddr_t)ct, sizeof (struct ct_data));
		}
		mem_free((caddr_t)cl, sizeof (CLIENT));
	}
	trace3(TR_clnt_vc_create, 1, prog, vers);
	return ((CLIENT *)NULL);
}

static bool_t
set_up_connection(fd, svcaddr, ct)
	register int fd;
	struct netbuf *svcaddr;		/* servers address */
	register struct ct_data *ct;
{
	int state;
	struct t_call sndcallstr, *rcvcall;
	int nconnect;
	bool_t connected, do_rcv_connect;

	ct->ct_addr.len = 0;
	state = t_getstate(fd);
	if (state == -1) {
		rpc_createerr.cf_stat = RPC_TLIERROR;
		rpc_createerr.cf_error.re_errno = 0;
		rpc_createerr.cf_error.re_terrno = t_errno;
		return (FALSE);
	}

#ifdef DEBUG
	fprintf(stderr, "set_up_connection: state = %d\n", state);
#endif
	switch (state) {
	case T_IDLE:
		if (svcaddr == (struct netbuf *)NULL) {
			rpc_createerr.cf_stat = RPC_UNKNOWNADDR;
			return (FALSE);
		}
		/*
		 * Connect only if state is IDLE and svcaddr known
		 */
		rcvcall = (struct t_call *)t_alloc(fd, T_CALL, T_OPT|T_ADDR);
		if (rcvcall == NULL) {
			rpc_createerr.cf_stat = RPC_TLIERROR;
			rpc_createerr.cf_error.re_terrno = t_errno;
			rpc_createerr.cf_error.re_errno = errno;
			return (FALSE);
		}
		rcvcall->udata.maxlen = 0;
		sndcallstr.addr = *svcaddr;
		sndcallstr.opt.len = 0;
		sndcallstr.udata.len = 0;
		/*
		 * Even NULL could have sufficed for rcvcall, because
		 * the address returned is same for all cases except
		 * for the gateway case, and hence required.
		 */
		connected = FALSE;
		do_rcv_connect = FALSE;
		for (nconnect = 0; nconnect < 3; nconnect++) {
			if (t_connect(fd, &sndcallstr, rcvcall) != -1) {
				connected = TRUE;
				break;
			}
			if (!(t_errno == TSYSERR && errno == EINTR)) {
				break;
			}
			if ((state = t_getstate(fd)) == T_OUTCON) {
				do_rcv_connect = TRUE;
				break;
			}
			if (state != T_IDLE)
				break;
		}
		if (do_rcv_connect) {
			do {
				if (t_rcvconnect(fd, rcvcall) != -1) {
					connected = TRUE;
					break;
				}
			} while (t_errno == TSYSERR && errno == EINTR);
		}
		if (!connected) {
			rpc_createerr.cf_stat = RPC_TLIERROR;
			rpc_createerr.cf_error.re_terrno = t_errno;
			rpc_createerr.cf_error.re_errno = errno;
			(void) t_free((char *)rcvcall, T_CALL);
#ifdef DEBUG
			fprintf(stderr, "clnt_vc: t_connect error %d\n",
				rpc_createerr.cf_error.re_terrno);
#endif
			return (FALSE);
		}

		/* Free old area if allocated */
		if (ct->ct_addr.buf)
			free(ct->ct_addr.buf);
		ct->ct_addr = rcvcall->addr;	/* To get the new address */
		/* So that address buf does not get freed */
		rcvcall->addr.buf = NULL;
		(void) t_free((char *)rcvcall, T_CALL);
		break;
	case T_DATAXFER:
	case T_OUTCON:
		if (svcaddr == (struct netbuf *)NULL) {
			/*
			 * svcaddr could also be NULL in cases where the
			 * client is already bound and connected.
			 */
			ct->ct_addr.len = 0;
		} else {
			ct->ct_addr.buf = malloc(svcaddr->len);
			if (ct->ct_addr.buf == (char *)NULL) {
				(void) syslog(LOG_ERR, clnt_vc_errstr,
					clnt_vc_str, __no_mem_str);
				rpc_createerr.cf_stat = RPC_SYSTEMERROR;
				rpc_createerr.cf_error.re_errno = errno;
				rpc_createerr.cf_error.re_terrno = 0;
				return (FALSE);
			}
			memcpy(ct->ct_addr.buf, svcaddr->buf,
					(int)svcaddr->len);
			ct->ct_addr.len = ct->ct_addr.maxlen = svcaddr->len;
		}
		break;
	default:
		rpc_createerr.cf_stat = RPC_UNKNOWNADDR;
		return (FALSE);
	}
	return (TRUE);
}

static enum clnt_stat
clnt_vc_call(cl, proc, xdr_args, args_ptr, xdr_results, results_ptr, timeout)
	register CLIENT *cl;
	u_long proc;
	xdrproc_t xdr_args;
	caddr_t args_ptr;
	xdrproc_t xdr_results;
	caddr_t results_ptr;
	struct timeval timeout;
{
	register struct ct_data *ct = (struct ct_data *) cl->cl_private;
	register XDR *xdrs = &(ct->ct_xdrs);
	struct rpc_msg reply_msg;
	u_long x_id;
	u_long *msg_x_id = (u_long *)(ct->ct_mcall);	/* yuk */
	register bool_t shipnow;
	int refreshes = 2;
	sigset_t newmask, mask;

	trace3(TR_clnt_vc_call, 0, cl, proc);
	_sigfillset(&newmask);
	thr_sigsetmask(SIG_SETMASK, &newmask, &mask);
	mutex_lock(&clnt_fd_lock);
	while (vc_fd_locks[ct->ct_fd])
		cond_wait(&vc_cv[ct->ct_fd], &clnt_fd_lock);
	vc_fd_locks[ct->ct_fd] = lock_value;
	mutex_unlock(&clnt_fd_lock);
	if (!ct->ct_waitset) {
		/* If time is not within limits, we ignore it. */
		if (time_not_ok(&timeout) == FALSE)
			ct->ct_wait = __rpc_timeval_to_msec(&timeout);
	} else {
		timeout.tv_sec = (ct->ct_wait / 1000);
		timeout.tv_usec = (ct->ct_wait % 1000) * 1000;
	}

	shipnow = ((xdr_results == (xdrproc_t)0) && (timeout.tv_sec == 0) &&
			(timeout.tv_usec == 0)) ? FALSE : TRUE;
call_again:
	xdrs->x_op = XDR_ENCODE;
	ct->ct_error.re_status = RPC_SUCCESS;
	x_id = ntohl(--(*msg_x_id));

	if ((! XDR_PUTBYTES(xdrs, ct->ct_mcall, ct->ct_mpos)) ||
	    (! XDR_PUTLONG(xdrs, (long *)&proc)) ||
	    (! AUTH_MARSHALL(cl->cl_auth, xdrs)) ||
	    (! (*xdr_args)(xdrs, args_ptr))) {
		if (ct->ct_error.re_status == RPC_SUCCESS)
			ct->ct_error.re_status = RPC_CANTENCODEARGS;
		(void) xdrrec_endofrecord(xdrs, TRUE);
		release_fd_lock(ct->ct_fd, mask);
		trace3(TR_clnt_vc_call, 1, cl, proc);
		return (ct->ct_error.re_status);
	}
	if (! xdrrec_endofrecord(xdrs, shipnow)) {
		release_fd_lock(ct->ct_fd, mask);
		trace3(TR_clnt_vc_call, 1, cl, proc);
		return (ct->ct_error.re_status = RPC_CANTSEND);
	}
	if (! shipnow) {
		release_fd_lock(ct->ct_fd, mask);
		trace3(TR_clnt_vc_call, 1, cl, proc);
		return (RPC_SUCCESS);
	}
	/*
	 * Hack to provide rpc-based message passing
	 */
	if (timeout.tv_sec == 0 && timeout.tv_usec == 0) {
		release_fd_lock(ct->ct_fd, mask);
		trace3(TR_clnt_vc_call, 1, cl, proc);
		return (ct->ct_error.re_status = RPC_TIMEDOUT);
	}


	/*
	 * Keep receiving until we get a valid transaction id
	 */
	xdrs->x_op = XDR_DECODE;
	/*CONSTANTCONDITION*/
	while (TRUE) {
		reply_msg.acpted_rply.ar_verf = _null_auth;
		reply_msg.acpted_rply.ar_results.where = NULL;
		reply_msg.acpted_rply.ar_results.proc = (xdrproc_t)xdr_void;
		if (! xdrrec_skiprecord(xdrs)) {
			release_fd_lock(ct->ct_fd, mask);
			trace3(TR_clnt_vc_call, 1, cl, proc);
			return (ct->ct_error.re_status);
		}
		/* now decode and validate the response header */
		if (! xdr_replymsg(xdrs, &reply_msg)) {
			if (ct->ct_error.re_status == RPC_SUCCESS)
				continue;
			release_fd_lock(ct->ct_fd, mask);
			trace3(TR_clnt_vc_call, 1, cl, proc);
			return (ct->ct_error.re_status);
		}
		if (reply_msg.rm_xid == x_id)
			break;
	}

	/*
	 * process header
	 */
	if ((reply_msg.rm_reply.rp_stat == MSG_ACCEPTED) &&
	    (reply_msg.acpted_rply.ar_stat == SUCCESS))
		ct->ct_error.re_status = RPC_SUCCESS;
	else
		__seterr_reply(&reply_msg, &(ct->ct_error));

	if (ct->ct_error.re_status == RPC_SUCCESS) {
		if (! AUTH_VALIDATE(cl->cl_auth,
				&reply_msg.acpted_rply.ar_verf)) {
			ct->ct_error.re_status = RPC_AUTHERROR;
			ct->ct_error.re_why = AUTH_INVALIDRESP;
		} else if (! (*xdr_results)(xdrs, results_ptr)) {
			if (ct->ct_error.re_status == RPC_SUCCESS)
				ct->ct_error.re_status = RPC_CANTDECODERES;
		}
		/* free verifier ... */
		if (reply_msg.acpted_rply.ar_verf.oa_base != NULL) {
			xdrs->x_op = XDR_FREE;
			(void) xdr_opaque_auth(xdrs,
				&(reply_msg.acpted_rply.ar_verf));
		}
	}	/* end successful completion */
	/*
	 * If unsuccesful AND error is an authentication error
	 * then refresh credentials and try again, else break
	 */
	else if (ct->ct_error.re_status == RPC_AUTHERROR) {
		/* maybe our credentials need to be refreshed ... */
		if (refreshes-- && AUTH_REFRESH(cl->cl_auth))
			goto call_again;
	} /* end of unsuccessful completion */
	release_fd_lock(ct->ct_fd, mask);
	trace3(TR_clnt_vc_call, 1, cl, proc);
	return (ct->ct_error.re_status);
}

static void
clnt_vc_geterr(cl, errp)
	CLIENT *cl;
	struct rpc_err *errp;
{
	register struct ct_data *ct = (struct ct_data *) cl->cl_private;

	trace2(TR_clnt_vc_geterr, 0, cl);
	*errp = ct->ct_error;
	trace2(TR_clnt_vc_geterr, 1, cl);
}

static bool_t
clnt_vc_freeres(cl, xdr_res, res_ptr)
	CLIENT *cl;
	xdrproc_t xdr_res;
	caddr_t res_ptr;
{
	register struct ct_data *ct = (struct ct_data *)cl->cl_private;
	register XDR *xdrs = &(ct->ct_xdrs);
	bool_t dummy;
	sigset_t newmask, mask;

	trace2(TR_clnt_vc_freeres, 0, cl);
	_sigfillset(&newmask);
	thr_sigsetmask(SIG_SETMASK, &newmask, &mask);
	mutex_lock(&clnt_fd_lock);
	while (vc_fd_locks[ct->ct_fd])
		cond_wait(&vc_cv[ct->ct_fd], &clnt_fd_lock);
	xdrs->x_op = XDR_FREE;
	dummy = (*xdr_res)(xdrs, res_ptr);
	mutex_unlock(&clnt_fd_lock);
	thr_sigsetmask(SIG_SETMASK, &(mask), NULL);
	cond_signal(&vc_cv[ct->ct_fd]);
	trace2(TR_clnt_vc_freeres, 1, cl);
	return (dummy);
}

static void
clnt_vc_abort()
{
	trace1(TR_clnt_vc_abort, 0);
	trace1(TR_clnt_vc_abort, 1);
}

static bool_t
clnt_vc_control(cl, request, info)
	CLIENT *cl;
	int request;
	char *info;
{
	bool_t ret;
	register struct ct_data *ct = (struct ct_data *)cl->cl_private;
	sigset_t newmask, mask;

	trace3(TR_clnt_vc_control, 0, cl, request);
	_sigfillset(&newmask);
	thr_sigsetmask(SIG_SETMASK, &newmask, &mask);
	mutex_lock(&clnt_fd_lock);
	while (vc_fd_locks[ct->ct_fd])
		cond_wait(&vc_cv[ct->ct_fd], &clnt_fd_lock);
	vc_fd_locks[ct->ct_fd] = lock_value;
	mutex_unlock(&clnt_fd_lock);
	switch (request) {
	case CLSET_FD_CLOSE:
		ct->ct_closeit = TRUE;
		release_fd_lock(ct->ct_fd, mask);
		trace3(TR_clnt_vc_control, 1, cl, request);
		return (TRUE);
	case CLSET_FD_NCLOSE:
		ct->ct_closeit = FALSE;
		release_fd_lock(ct->ct_fd, mask);
		trace3(TR_clnt_vc_control, 1, cl, request);
		return (TRUE);
	}

	/* for other requests which use info */
	if (info == NULL) {
		release_fd_lock(ct->ct_fd, mask);
		trace3(TR_clnt_vc_control, 1, cl, request);
		return (FALSE);
	}
	switch (request) {
	case CLSET_TIMEOUT:
		if (time_not_ok((struct timeval *)info)) {
			release_fd_lock(ct->ct_fd, mask);
			trace3(TR_clnt_vc_control, 1, cl, request);
			return (FALSE);
		}
		ct->ct_wait = __rpc_timeval_to_msec((struct timeval *)info);
		ct->ct_waitset = TRUE;
		break;
	case CLGET_TIMEOUT:
		((struct timeval *) info)->tv_sec = ct->ct_wait / 1000;
		((struct timeval *) info)->tv_usec =
			(ct->ct_wait % 1000) * 1000;
		break;
	case CLGET_SERVER_ADDR:	/* For compatibility only */
		(void) memcpy(info, ct->ct_addr.buf, (int)ct->ct_addr.len);
		break;
	case CLGET_FD:
		*(int *)info = ct->ct_fd;
		break;
	case CLGET_SVC_ADDR:
		/* The caller should not free this memory area */
		*(struct netbuf *)info = ct->ct_addr;
		break;
	case CLSET_SVC_ADDR:		/* set to new address */
#ifdef undef
		/*
		 * XXX: once the t_snddis(), followed by t_connect() starts to
		 * work, this ifdef should be removed.  CLIENT handle reuse
		 * would then be possible for COTS as well.
		 */
		if (t_snddis(ct->ct_fd, NULL) == -1) {
			rpc_createerr.cf_stat = RPC_TLIERROR;
			rpc_createerr.cf_error.re_terrno = t_errno;
			rpc_createerr.cf_error.re_errno = errno;
			release_fd_lock(ct->ct_fd, mask);
			trace3(TR_clnt_vc_control, 1, cl, request);
			return (FALSE);
		}
		ret = set_up_connection(ct->ct_fd, (struct netbuf *)info, ct));
		release_fd_lock(ct->ct_fd, mask);
		trace3(TR_clnt_vc_control, 1, cl, request);
		return (ret);
#else
		release_fd_lock(ct->ct_fd, mask);
		trace3(TR_clnt_vc_control, 1, cl, request);
		return (FALSE);
#endif
	case CLGET_XID:
		/*
		 * use the knowledge that xid is the
		 * first element in the call structure
		 * This will get the xid of the PREVIOUS call
		 */
		*(u_long *)info = ntohl(*(u_long *)ct->ct_mcall);
		break;
	case CLSET_XID:
		/* This will set the xid of the NEXT call */
		*(u_long *)ct->ct_mcall =  htonl(*(u_long *)info + 1);
		/* increment by 1 as clnt_vc_call() decrements once */
		break;
	case CLGET_VERS:
		/*
		 * This RELIES on the information that, in the call body,
		 * the version number field is the fifth field from the
		 * begining of the RPC header. MUST be changed if the
		 * call_struct is changed
		 */
		*(u_long *)info = ntohl(*(u_long *)(ct->ct_mcall +
						4 * BYTES_PER_XDR_UNIT));
		break;

	case CLSET_VERS:
		*(u_long *)(ct->ct_mcall + 4 * BYTES_PER_XDR_UNIT) =
			htonl(*(u_long *)info);
		break;

	case CLGET_PROG:
		/*
		 * This RELIES on the information that, in the call body,
		 * the program number field is the fourth field from the
		 * begining of the RPC header. MUST be changed if the
		 * call_struct is changed
		 */
		*(u_long *)info = ntohl(*(u_long *)(ct->ct_mcall +
						3 * BYTES_PER_XDR_UNIT));
		break;

	case CLSET_PROG:
		*(u_long *)(ct->ct_mcall + 3 * BYTES_PER_XDR_UNIT) =
			htonl(*(u_long *)info);
		break;

	default:
		release_fd_lock(ct->ct_fd, mask);
		trace3(TR_clnt_vc_control, 1, cl, request);
		return (FALSE);
	}
	release_fd_lock(ct->ct_fd, mask);
	trace3(TR_clnt_vc_control, 1, cl, request);
	return (TRUE);
}

static void
clnt_vc_destroy(cl)
	CLIENT *cl;
{
	register struct ct_data *ct = (struct ct_data *) cl->cl_private;
	int ct_fd = ct->ct_fd;
	sigset_t mask, newmask;

	trace2(TR_clnt_vc_destroy, 0, cl);
	_sigfillset(&newmask);
	thr_sigsetmask(SIG_SETMASK, &newmask, &mask);
	mutex_lock(&clnt_fd_lock);
	while (vc_fd_locks[ct_fd])
		cond_wait(&vc_cv[ct_fd], &clnt_fd_lock);
	if (ct->ct_closeit)
		(void) t_close(ct_fd);
	XDR_DESTROY(&(ct->ct_xdrs));
	if (ct->ct_addr.buf)
		(void) free(ct->ct_addr.buf);
	mem_free((caddr_t)ct, sizeof (struct ct_data));
	if (cl->cl_netid && cl->cl_netid[0])
		mem_free(cl->cl_netid, strlen(cl->cl_netid) +1);
	if (cl->cl_tp && cl->cl_tp[0])
		mem_free(cl->cl_tp, strlen(cl->cl_tp) +1);
	mem_free((caddr_t)cl, sizeof (CLIENT));
	mutex_unlock(&clnt_fd_lock);
	thr_sigsetmask(SIG_SETMASK, &(mask), NULL);

	cond_signal(&vc_cv[ct_fd]);
	trace2(TR_clnt_vc_destroy, 1, cl);
}

/*
 * Interface between xdr serializer and vc connection.
 * Behaves like the system calls, read & write, but keeps some error state
 * around for the rpc level.
 */
static int
read_vc(ct_tmp, buf, len)
	void *ct_tmp;
	caddr_t buf;
	register int len;
{
	struct pollfd *pfdp = (struct pollfd *) NULL;
	static struct pollfd *pfdp_main = (struct pollfd *) NULL;
	register struct ct_data *ct = ct_tmp;
	static thread_key_t pfdp_key;
	int main_thread;
	int dtbsize = __rpc_dtbsize();
	extern mutex_t tsd_lock;
	sigset_t mask, newmask;

	trace2(TR_read_vc, 0, len);
	_sigfillset(&newmask);

#ifdef DEBUG
	thr_sigsetmask(SIG_SETMASK, &newmask, &mask);
	mutex_lock(&clnt_fd_lock);	/* DEBUG */
	assert(vc_fd_locks[ct->ct_fd] == lock_value);	/* DEBUG */
	mutex_unlock(&clnt_fd_lock);	/* DEBUG */
	thr_sigsetmask(SIG_SETMASK, &(mask), NULL);
#endif
	if (len == 0) {
		trace2(TR_read_vc, 1, len);
		return (0);
	}
	if ((main_thread = _thr_main())) {
			pfdp = pfdp_main;
	} else {
		if (pfdp_key == 0) {
			thr_sigsetmask(SIG_SETMASK, &newmask, &mask);
			mutex_lock(&tsd_lock);
			if (pfdp_key == 0)
				thr_keycreate(&pfdp_key, free);
			mutex_unlock(&tsd_lock);
			thr_sigsetmask(SIG_SETMASK, &(mask), NULL);
		}
		thr_getspecific(pfdp_key, (void **) &pfdp);
	}
	if (pfdp == (struct pollfd *) NULL) {
		pfdp = (struct pollfd *) malloc(
			sizeof (struct pollfd) * dtbsize);
		if (pfdp == (struct pollfd *) NULL) {
			(void) syslog(LOG_ERR, clnt_vc_errstr,
				clnt_read_vc_str, __no_mem_str);
			ct->ct_error.re_status = RPC_SYSTEMERROR;
			ct->ct_error.re_errno = errno;
			ct->ct_error.re_terrno = 0;
			trace2(TR_read_vc, 1, len);
			return (-1);
		}
		if (main_thread)
			pfdp_main = pfdp;
		else
			thr_setspecific(pfdp_key, (void *) pfdp);
	}
	/*
	 *	N.B.:  slot 0 in the pollfd array is reserved for the file
	 *	descriptor we're really interested in (as opposed to the
	 *	callback descriptors).
	 */
	pfdp->fd = ct->ct_fd;
	pfdp->events = POLLIN | POLLPRI | POLLRDNORM | POLLRDBAND;

	/*CONSTANTCONDITION*/
	while (TRUE) {
		extern void (*_svc_getreqset_proc)();
		extern fd_set svc_fdset;
		extern rwlock_t svc_fd_lock;
		int fds;
		int nfds;

	/* VARIABLES PROTECTED BY svc_fd_lock: svc_fdset */

		if (_svc_getreqset_proc) {
			thr_sigsetmask(SIG_SETMASK, &newmask, &mask);
			rw_rdlock(&svc_fd_lock);
			/* ``+ 1'' because of pfdp[0] */
			nfds = __rpc_select_to_poll(
				dtbsize, &svc_fdset, &pfdp[1]) + 1;
			rw_unlock(&svc_fd_lock);
			thr_sigsetmask(SIG_SETMASK, &(mask), NULL);
		} else
			nfds = 1;	/* don't forget about pfdp[0] */

		switch (fds = poll(pfdp, nfds, ct->ct_wait)) {
		case 0:
			ct->ct_error.re_status = RPC_TIMEDOUT;
			trace2(TR_read_vc, 1, len);
			return (-1);

		case -1:
			continue;
		}
		if (pfdp->revents == 0) {
			/* must be for server side of the house */
			(*_svc_getreqset_proc)(&pfdp[1], fds);
			continue;	/* do poll again */
		} else if (pfdp->revents & POLLNVAL) {
			ct->ct_error.re_status = RPC_CANTRECV;
			/*
			 *	Note:  we're faking errno here because we
			 *	previously would have expected select() to
			 *	return -1 with errno EBADF.  Poll(BA_OS)
			 *	returns 0 and sets the POLLNVAL revents flag
			 *	instead.
			 */
			ct->ct_error.re_errno = errno = EBADF;
			trace2(TR_read_vc, 1, len);
			return (-1);
		} else if (pfdp->revents & (POLLERR | POLLHUP)) {
			ct->ct_error.re_status = RPC_CANTRECV;
			ct->ct_error.re_errno = errno = EPIPE;
			trace2(TR_read_vc, 1, len);
			return (-1);
		}
		break;
	}

	switch (len = t_rcvall(ct->ct_fd, buf, len)) {
	case 0:
		/* premature eof */
		ct->ct_error.re_errno = ENOLINK;
		ct->ct_error.re_terrno = 0;
		ct->ct_error.re_status = RPC_CANTRECV;
		len = -1;	/* it's really an error */
		break;

	case -1:
		ct->ct_error.re_terrno = t_errno;
		ct->ct_error.re_errno = 0;
		ct->ct_error.re_status = RPC_CANTRECV;
		break;
	}
	trace2(TR_read_vc, 1, len);
	return (len);
}

static int
write_vc(ct_tmp, buf, len)
	void *ct_tmp;
	caddr_t buf;
	int len;
{
	register int i, cnt;
	struct ct_data *ct = ct_tmp;
	int flag;
	long maxsz;
	sigset_t mask, newmask;

	trace2(TR_write_vc, 0, len);

#ifdef DEBUG
	_sigfillset(&newmask);
	thr_sigsetmask(SIG_SETMASK, &newmask, &mask);
	mutex_lock(&clnt_fd_lock);	/* DEBUG */
	assert(vc_fd_locks[ct->ct_fd] == lock_value);	/* DEBUG */
	mutex_unlock(&clnt_fd_lock);	/* DEBUG */
	thr_sigsetmask(SIG_SETMASK, &(mask), NULL);
#endif

	maxsz = ct->ct_tsdu;
	if ((maxsz == 0) || (maxsz == -1)) {
		if ((len = t_snd(ct->ct_fd, buf, (unsigned)len, 0)) == -1) {
			ct->ct_error.re_terrno = t_errno;
			ct->ct_error.re_errno = 0;
			ct->ct_error.re_status = RPC_CANTSEND;
		}
		trace2(TR_write_vc, 1, len);
		return (len);
	}

	/*
	 * This for those transports which have a max size for data.
	 */
	for (cnt = len, i = 0; cnt > 0; cnt -= i, buf += i) {
		flag = cnt > maxsz ? T_MORE : 0;
		if ((i = t_snd(ct->ct_fd, buf, (unsigned)MIN(cnt, maxsz),
				flag)) == -1) {
			ct->ct_error.re_terrno = t_errno;
			ct->ct_error.re_errno = 0;
			ct->ct_error.re_status = RPC_CANTSEND;
			trace2(TR_write_vc, 1, len);
			return (-1);
		}
	}
	trace2(TR_write_vc, 1, len);
	return (len);
}

/*
 * Receive the required bytes of data, even if it is fragmented.
 */
static int
t_rcvall(fd, buf, len)
	int fd;
	char *buf;
	int len;
{
	int moreflag;
	int final = 0;
	int res;

	trace3(TR_t_rcvall, 0, fd, len);
	do {
		moreflag = 0;
		res = t_rcv(fd, buf, (unsigned)len, &moreflag);
		if (res == -1) {
			if (t_errno == TLOOK)
				switch (t_look(fd)) {
				case T_DISCONNECT:
					t_rcvdis(fd, NULL);
					t_snddis(fd, NULL);
					trace3(TR_t_rcvall, 1, fd, len);
					return (-1);
				case T_ORDREL:
				/* Received orderly release indication */
					t_rcvrel(fd);
				/* Send orderly release indicator */
					(void) t_sndrel(fd);
					trace3(TR_t_rcvall, 1, fd, len);
					return (-1);
				default:
					trace3(TR_t_rcvall, 1, fd, len);
					return (-1);
				}
		} else if (res == 0) {
			trace3(TR_t_rcvall, 1, fd, len);
			return (0);
		}
		final += res;
		buf += res;
		len -= res;
	} while ((len > 0) && (moreflag & T_MORE));
	trace3(TR_t_rcvall, 1, fd, len);
	return (final);
}

static struct clnt_ops *
clnt_vc_ops()
{
	static struct clnt_ops ops;
	extern mutex_t	ops_lock;
	sigset_t mask, newmask;

	/* VARIABLES PROTECTED BY ops_lock: ops */

	trace1(TR_clnt_vc_ops, 0);
	_sigfillset(&newmask);
	thr_sigsetmask(SIG_SETMASK, &newmask, &mask);
	mutex_lock(&ops_lock);
	if (ops.cl_call == NULL) {
		ops.cl_call = clnt_vc_call;
		ops.cl_abort = clnt_vc_abort;
		ops.cl_geterr = clnt_vc_geterr;
		ops.cl_freeres = clnt_vc_freeres;
		ops.cl_destroy = clnt_vc_destroy;
		ops.cl_control = clnt_vc_control;
	}
	mutex_unlock(&ops_lock);
	thr_sigsetmask(SIG_SETMASK, &(mask), NULL);
	trace1(TR_clnt_vc_ops, 1);
	return (&ops);
}

/*
 * Make sure that the time is not garbage.   -1 value is disallowed.
 * Note this is different from time_not_ok in clnt_dg.c
 */
static bool_t
time_not_ok(t)
	struct timeval *t;
{
	trace1(TR_time_not_ok, 0);
	trace1(TR_time_not_ok, 1);
	return (t->tv_sec <= -1 || t->tv_sec > 100000000 ||
		t->tv_usec <= -1 || t->tv_usec > 1000000);
}
