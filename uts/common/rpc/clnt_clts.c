/*
 * Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T
 *		All Rights Reserved
 */

/*
 *	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
 *	The copyright notice above does not evidence any
 *	actual or intended publication of such source code.
 */

#pragma ident   "@(#)clnt_clts.c 1.46     95/10/12 SMI"
/* SVr4.0 1.14	*/

/*
 *		PROPRIETARY NOTICE (Combined)
 *
 * This source code is unpublished proprietary information
 * constituting, or derived under license from AT&T's Unix(r) System V.
 * In addition, portions of such source code were derived from Berkeley
 * 4.3 BSD under license from the Regents of the University of
 * California.
 *
 *
 *
 *			Copyright Notice
 *
 * Notice of copyright on this source code product does not indicate
 * publication.
 *
 *	(c) 1986, 1987, 1988, 1989, 1990, 1991  Sun Microsystems, Inc.
 *	(c) 1983, 1984, 1985, 1986, 1987, 1988, 1989  AT&T.
 *			All rights reserved.
 */

/*
 * Implements a kernel based, client side RPC.
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/stream.h>
#include <sys/strsubr.h>
#include <sys/tiuser.h>
#include <sys/tihdr.h>
#include <sys/t_kuser.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/debug.h>
#include <sys/kstat.h>
#include <sys/t_lock.h>

#include <rpc/types.h>
#include <rpc/xdr.h>
#include <rpc/auth.h>
#include <rpc/clnt.h>
#include <rpc/rpc_msg.h>

static enum clnt_stat	clnt_clts_kcallit(CLIENT *, u_long, xdrproc_t, caddr_t,
			xdrproc_t, caddr_t, struct timeval);
static void		clnt_clts_kabort(void);
static void		clnt_clts_kerror(CLIENT *, struct rpc_err *);
static bool_t		clnt_clts_kfreeres(CLIENT *, xdrproc_t, caddr_t);
static bool_t		clnt_clts_kcontrol(CLIENT *, int, char *);
static void		clnt_clts_kdestroy(CLIENT *);
static int		clnt_clts_ksettimers(CLIENT *, struct rpc_timers *,
			struct rpc_timers *, int, void (*)(), caddr_t,
			u_long);

/*
 * Operations vector for CLTS based RPC
 */
static struct clnt_ops clts_ops = {
	clnt_clts_kcallit,	/* do rpc call */
	clnt_clts_kabort,	/* abort call */
	clnt_clts_kerror,	/* return error status */
	clnt_clts_kfreeres,	/* free results */
	clnt_clts_kdestroy,	/* destroy rpc handle */
	clnt_clts_kcontrol,	/* the ioctl() of rpc */
	clnt_clts_ksettimers	/* set retry timers */
};

/*
 * The size of the preserialized RPC header information.
 */
#define	CKU_HDRSIZE	20
/*
 * The initial allocation size.  It is small to reduce space requirements.
 */
#define	CKU_INITSIZE	2048
/*
 * The size of additional allocations, if required.  It is larger to
 * reduce the number of actual allocations.
 */
#define	CKU_ALLOCSIZE	8192

/*
 * Private data per rpc handle.  This structure is allocated by
 * clnt_clts_kcreate, and freed by clnt_clts_kdestroy.
 */
struct cku_private {
	CLIENT			 cku_client;	/* client handle */
	int			 cku_retrys;	/* request retrys */
	TIUSER 			*cku_tiptr;	/* open tli file pointer */
	struct netbuf		 cku_addr;	/* remote address */
	struct rpc_err		 cku_err;	/* error status */
	XDR			 cku_outxdr;	/* xdr stream for output */
	XDR			 cku_inxdr;	/* xdr stream for input */
	char			 cku_rpchdr[CKU_HDRSIZE]; /* rpc header */
	struct cred		*cku_cred;	/* credentials */
	struct rpc_timers	*cku_timers;	/* for estimating RTT */
	struct rpc_timers	*cku_timeall;	/* for estimating RTT */
	void			 (*cku_feedback) /* ptr to feedback rtn */
				(int, int, caddr_t);
	caddr_t			 cku_feedarg;	/* argument for feedback func */
	u_long			 cku_xid;	/* current XID */
};

struct {
	kstat_named_t	rccalls;
	kstat_named_t	rcbadcalls;
	kstat_named_t	rcretrans;
	kstat_named_t	rcbadxids;
	kstat_named_t	rctimeouts;
	kstat_named_t	rcnewcreds;
	kstat_named_t	rcbadverfs;
	kstat_named_t	rctimers;
	kstat_named_t	rcnomem;
	kstat_named_t	rccantsend;
} rcstat = {
	{ "calls",	KSTAT_DATA_ULONG },
	{ "badcalls",	KSTAT_DATA_ULONG },
	{ "retrans",	KSTAT_DATA_ULONG },
	{ "badxids",	KSTAT_DATA_ULONG },
	{ "timeouts",	KSTAT_DATA_ULONG },
	{ "newcreds",	KSTAT_DATA_ULONG },
	{ "badverfs",	KSTAT_DATA_ULONG },
	{ "timers",	KSTAT_DATA_ULONG },
	{ "nomem",	KSTAT_DATA_ULONG },
	{ "cantsend",	KSTAT_DATA_ULONG },
};

kstat_named_t *rcstat_ptr = (kstat_named_t *)&rcstat;
ulong_t rcstat_ndata = sizeof (rcstat) / sizeof (kstat_named_t);

#ifdef accurate_stats
extern kmutex_t	rcstat_lock;	/* mutex for rcstat updates */

#define	RCSTAT_INCR(x)		\
	mutex_enter(&rcstat_lock);	\
	rcstat.x.value.ul++;			\
	mutex_exit(&rcstat_lock);
#else
#define	RCSTAT_INCR(x)		\
	rcstat.x.value.ul++;
#endif

#define	ptoh(p)		(&((p)->cku_client))
#define	htop(h)		((struct cku_private *)((h)->cl_private))

/*
 * Times to retry
 */
#define	RECVTRIES	2
#define	SNDTRIES	4
#define	REFRESHES	2	/* authentication refreshes */

/*
 * Create an rpc handle for a clts rpc connection.
 * Allocates space for the handle structure and the private data, and
 * opens a socket.  Note sockets and handles are one to one.
 */
/* ARGSUSED */
int
clnt_clts_kcreate(TIUSER *tiptr, dev_t rdev, struct netbuf *addr,
	u_long pgm, u_long vers, int retrys, struct cred *cred, CLIENT **cl)
{
	register CLIENT *h;
	register struct cku_private *p;
	struct rpc_msg call_msg;
	int error;

	if (cl == NULL)
		return (EINVAL);

	*cl = NULL;
	error = 0;

	p = (struct cku_private *)kmem_zalloc(sizeof (*p), KM_SLEEP);

	h = ptoh(p);

	/* handle */
	h->cl_ops = &clts_ops;
	h->cl_private = (caddr_t)p;
	h->cl_auth = authkern_create();

	/* call message, just used to pre-serialize below */
	call_msg.rm_xid = 0;
	call_msg.rm_direction = CALL;
	call_msg.rm_call.cb_rpcvers = RPC_MSG_VERSION;
	call_msg.rm_call.cb_prog = pgm;
	call_msg.rm_call.cb_vers = vers;

	/* private */
	clnt_clts_kinit(h, addr, retrys, cred);

	xdrmem_create(&p->cku_outxdr, p->cku_rpchdr, CKU_HDRSIZE, XDR_ENCODE);

	/* pre-serialize call message header */
	if (! xdr_callhdr(&p->cku_outxdr, &call_msg)) {
		error = EINVAL;		/* XXX */
		goto bad;
	}
	p->cku_tiptr = tiptr;
	*cl = h;
	return (0);

bad:
	kmem_free((caddr_t)p, (u_int)sizeof (struct cku_private));

	return (error);
}

void
clnt_clts_kinit(CLIENT *h, struct netbuf *addr, int retrys, cred_t *cred)
{
	/* LINTED pointer alignment */
	struct cku_private *p = htop(h);

	p->cku_retrys = retrys;

	if (p->cku_addr.maxlen < addr->len) {
		if (p->cku_addr.maxlen != 0 && p->cku_addr.buf != NULL)
			(void) kmem_free(p->cku_addr.buf, p->cku_addr.maxlen);

		p->cku_addr.buf = (char *)kmem_zalloc(addr->maxlen, KM_SLEEP);
		p->cku_addr.maxlen = addr->maxlen;
	}

	p->cku_addr.len = addr->len;
	bcopy(addr->buf, p->cku_addr.buf, addr->len);

	p->cku_cred = cred;
	p->cku_xid = 0;
}

/*
 * set the timers.  Return current retransmission timeout.
 */
static int
clnt_clts_ksettimers(CLIENT *h, struct rpc_timers *t, struct rpc_timers *all,
	int minimum, void (*feedback)(int, int, caddr_t), caddr_t arg,
	u_long xid)
{
	/* LINTED pointer alignment */
	struct cku_private *p = htop(h);
	int value;

	p->cku_feedback = feedback;
	p->cku_feedarg = arg;
	p->cku_timers = t;
	p->cku_timeall = all;
	if (xid)
		p->cku_xid = xid;
	value = all->rt_rtxcur;
	value += t->rt_rtxcur;
	if (value < minimum)
		return (minimum);
	RCSTAT_INCR(rctimers);
	return (value);
}

/*
 * Time out back off function. tim is in HZ
 */
#define	MAXTIMO	(20 * HZ)
#define	backoff(tim)	((((tim) << 1) > MAXTIMO) ? MAXTIMO : ((tim) << 1))

#define	RETRY_POLL_TIMO	30

/*
 * Call remote procedure.
 * Most of the work of rpc is done here.  We serialize what is left
 * of the header (some was pre-serialized in the handle), serialize
 * the arguments, and send it off.  We wait for a reply or a time out.
 * Timeout causes an immediate return, other packet problems may cause
 * a retry on the receive.  When a good packet is received we deserialize
 * it, and check verification.  A bad reply code will cause one retry
 * with full (longhand) credentials.
 */

enum clnt_stat
clnt_clts_kcallit_addr(CLIENT *h, u_long procnum, xdrproc_t xdr_args,
	caddr_t argsp, xdrproc_t xdr_results, caddr_t resultsp,
	struct timeval wait, struct netbuf *sin)
{
	/* LINTED pointer alignment */
	register struct cku_private *p = htop(h);
	register XDR *xdrs;
	register TIUSER *tiptr = p->cku_tiptr;
	int rtries;
	int stries = p->cku_retrys;
	int timohz;
	int ret;
	int refreshes = REFRESHES;	/* number of times to refresh cred */
	int round_trip;			/* time the RPC */
	struct t_kunitdata *unitdata;
	int type;
	int uderr;
	int error;
	mblk_t *mp;
	unsigned int len;
	mblk_t *mpdup;

	RCSTAT_INCR(rccalls);

	if (p->cku_xid == 0)
		p->cku_xid = alloc_xid();

	/*
	 * This is dumb but easy: keep the time out in units of hz
	 * so it is easy to call timeout and modify the value.
	 * We approximate division by MICROSEC as (x + (x >> 4)) >> 20,
	 * since this is much faster and precision is not critical.
	 * The approximation is within 1% of the true value.
	 */
	timohz = wait.tv_usec * HZ;
	timohz = (timohz + (timohz >> 4)) >> 20;
	timohz += wait.tv_sec * HZ;

	mpdup = NULL;

call_again:
	unitdata = NULL;

	if (mpdup == NULL) {

		while ((mp = allocb(CKU_INITSIZE, BPRI_LO)) == NULL) {
			if (strwaitbuf(CKU_INITSIZE, BPRI_LO)) {
				p->cku_err.re_status = RPC_SYSTEMERROR;
				p->cku_err.re_errno = ENOSR;
				goto done;
			}
		}

		/*
		 * Copy in the preserialized RPC header information.
		 */
		bcopy(p->cku_rpchdr, (caddr_t)mp->b_rptr, CKU_HDRSIZE);

		/*
		 * The transaction id is the first thing in the output buffer.
		 */
		/* LINTED pointer alignment */
		(*(u_long *)(mp->b_rptr)) = p->cku_xid;

		xdrs = &p->cku_outxdr;
		xdrmblk_init(xdrs, mp, XDR_ENCODE, CKU_ALLOCSIZE);

		/*
		 * Skip the preserialized stuff.
		 */
		XDR_SETPOS(xdrs, CKU_HDRSIZE);

		/*
		 * Serialize dynamic stuff into the output buffer.
		 */
		if ((! XDR_PUTLONG(xdrs, (long *)&procnum)) ||
		    (! AUTH_MARSHALL(h->cl_auth, xdrs, p->cku_cred)) ||
		    (! (*xdr_args)(xdrs, argsp))) {
			freemsg(mp);
			p->cku_err.re_status = RPC_CANTENCODEARGS;
			p->cku_err.re_errno = EIO;
			goto done;
		}
		len = xmsgsize(mp);
	} else
		mp = mpdup;

	mpdup = dupmsg(mp);
	if (mpdup == NULL) {
		freemsg(mp);
		p->cku_err.re_status = RPC_SYSTEMERROR;
		p->cku_err.re_errno = ENOSR;
		goto done;
	}

	round_trip = lbolt;
	if ((error = t_kalloc(tiptr, T_UNITDATA, T_UDATA,
				(char **)&unitdata)) != 0) {
		freemsg(mp);
		RCSTAT_INCR(rcnomem);
		p->cku_err.re_status = RPC_SYSTEMERROR;
		p->cku_err.re_errno = ENOSR;
		goto done;
	}
	unitdata->addr.len = p->cku_addr.len;
	unitdata->addr.maxlen = p->cku_addr.maxlen;
	unitdata->addr.buf = (char *)kmem_alloc(unitdata->addr.maxlen,
		KM_SLEEP);

	bcopy(p->cku_addr.buf, unitdata->addr.buf, p->cku_addr.len);

	unitdata->udata.udata_mp = mp;
	unitdata->udata.len = len;

	if ((error = t_ksndudata(tiptr, unitdata, NULL)) != 0) {
		p->cku_err.re_status = RPC_CANTSEND;
		p->cku_err.re_errno = error;
		RCSTAT_INCR(rccantsend);
		goto done;
	}

tryread:
	for (rtries = RECVTRIES; rtries; rtries--) {
		/*
		 * Poll for a response.  If we get interrupted, then
		 * break out.  Otherwise, keep polling.
		 */
		if ((error = t_kspoll(tiptr, timohz, READWAIT, &ret)) != 0) {
			if (error == EINTR) {
				p->cku_err.re_status = RPC_INTR;
				p->cku_err.re_errno = EINTR;
				goto done;
			}
			continue;	/* is this correct? */
		}
		/*
		 * If poll timed out, then break out of the loop.
		 */
		if (ret == 0) {
			/* XXX: declare this in some header file */
			extern void t_kadvise(TIUSER *, u_char *, int);

			p->cku_err.re_status = RPC_TIMEDOUT;
			p->cku_err.re_errno = ETIMEDOUT;
			RCSTAT_INCR(rctimeouts);
			/*
			 * Timeout may be due to a dead gateway. Send an
			 * ioctl downstream advising deletion of route when
			 * we reach the half-way point to timing out.
			 */
			if (stries == p->cku_retrys/2)
				t_kadvise(tiptr, (u_char *)p->cku_addr.buf,
					p->cku_addr.len);
			goto done;
		}
		/*
		 * Something waiting, so read it in
		 */
		if ((error = t_krcvudata(tiptr, unitdata, &type,
					&uderr)) != 0) {
			p->cku_err.re_status = RPC_SYSTEMERROR;
			p->cku_err.re_errno = error;
			goto done;
		}
		/*
		 * Filter for messages of type, T_DATA.  Skip any other
		 * types.
		 * Only count data messages and system errors against
		 * the receive retry count.  Other types such as
		 * T_UDERRs are simply discarded.
		 */
		if (type != T_DATA) {
			rtries++;
			continue;
		}
		/*
		 * Check the message length.  A response must be
		 * at least four bytes so that it can contain the
		 * reply transaction id.
		 */
		if (unitdata->udata.len < sizeof (u_long)) {
			continue;
		}
		/*
		 * If the reply transaction ID matches the ID sent, we have
		 * a good packet.  A mismatch might indicate that we
		 * somehow got an old reply or that more than one thread is
		 * trying to use the same client handle (which would be a
		 * bug).
		 */
		/* LINTED pointer alignment */
		if (*((u_long *)(unitdata->udata.buf)) != p->cku_xid) {
			RCSTAT_INCR(rcbadxids);
			continue;
		}
		/*
		 * If desired, save the server address.
		 */
		if (sin != NULL) {
			bcopy(unitdata->addr.buf, sin->buf, unitdata->addr.len);
			sin->len = unitdata->addr.len;
		}
		break;
	}

	if (rtries == 0) {
		p->cku_err.re_status = RPC_CANTRECV;
		p->cku_err.re_errno = EIO;
		goto done;
	}

	round_trip = lbolt - round_trip;
	/*
	 * Van Jacobson timer algorithm here, only if NOT a retransmission.
	 */
	if (p->cku_timers != (struct rpc_timers *)0 &&
	    stries == p->cku_retrys) {
		register int rt;

		rt = round_trip;
		rt -= (p->cku_timers->rt_srtt >> 3);
		p->cku_timers->rt_srtt += rt;
		if (rt < 0)
			rt = - rt;
		rt -= (p->cku_timers->rt_deviate >> 2);
		p->cku_timers->rt_deviate += rt;
		p->cku_timers->rt_rtxcur =
			(u_long)((p->cku_timers->rt_srtt >> 2) +
			    p->cku_timers->rt_deviate) >> 1;

		rt = round_trip;
		rt -= (p->cku_timeall->rt_srtt >> 3);
		p->cku_timeall->rt_srtt += rt;
		if (rt < 0)
			rt = - rt;
		rt -= (p->cku_timeall->rt_deviate >> 2);
		p->cku_timeall->rt_deviate += rt;
		p->cku_timeall->rt_rtxcur =
			(u_long)((p->cku_timeall->rt_srtt >> 2) +
			    p->cku_timeall->rt_deviate) >> 1;
		if (p->cku_feedback != NULL)
		    (*p->cku_feedback)(FEEDBACK_OK, procnum, p->cku_feedarg);
	}

	/*
	 * Process reply
	 */

	xdrs = &(p->cku_inxdr);
	xdrmblk_init(xdrs, unitdata->udata.udata_mp, XDR_DECODE, 0);

	{
		/*
		 * Declare this variable here to have smaller
		 * demand for stack space in this procedure.
		 */
		struct rpc_msg		   reply_msg;

		reply_msg.rm_direction = REPLY;
		reply_msg.rm_reply.rp_stat = MSG_ACCEPTED;
		reply_msg.acpted_rply.ar_stat = SUCCESS;
		reply_msg.acpted_rply.ar_verf = _null_auth;
		reply_msg.acpted_rply.ar_results.where = resultsp;
		reply_msg.acpted_rply.ar_results.proc = xdr_results;

		/*
		 * Decode and validate the response.
		 */
		if (xdr_replymsg(xdrs, &reply_msg)) {
			_seterr_reply(&reply_msg, &(p->cku_err));

			if (p->cku_err.re_status == RPC_SUCCESS) {
				/*
				 * Reply is good, check auth.
				 */
				if (! AUTH_VALIDATE(h->cl_auth,
				    &reply_msg.acpted_rply.ar_verf)) {
					p->cku_err.re_status = RPC_AUTHERROR;
					p->cku_err.re_why = AUTH_INVALIDRESP;
					RCSTAT_INCR(rcbadverfs);
					/*
					 * See if another message is here. If
					 * so, maybe it is the right response.
					 */
					(void) t_kspoll(tiptr,
					    RETRY_POLL_TIMO * HZ,
					    READWAIT, &ret);
					if (ret != 0) {
						(void) xdr_rpc_free_verifier(
							xdrs, &reply_msg);
						goto tryread;
					}
				}
			} else {
				/* set errno in case we can't recover */
				p->cku_err.re_errno = EIO;

				/*
				 * Maybe our credential needs refreshed
				 */
				if (p->cku_err.re_status == RPC_AUTHERROR &&
				    refreshes > 0 &&
				    AUTH_REFRESH(h->cl_auth, p->cku_cred)) {
					/*
					 *  On the first refresh, force at
					 *  least one send retry.  This
					 *  prevents a soft authentication
					 *  error turning into a hard one
					 *  at an upper level.
					 */
					if (refreshes == REFRESHES &&
					    stries == 0) {
						stries++;
					}
					refreshes--;
					RCSTAT_INCR(rcnewcreds);
					freemsg(mpdup);
					mpdup = NULL;
				}
			}
		} else {
			p->cku_err.re_status = RPC_CANTDECODERES;
			p->cku_err.re_errno = EIO;
		}
		(void) xdr_rpc_free_verifier(xdrs, &reply_msg);
	}

done:
	if (unitdata != NULL) {
		kmem_free(unitdata->addr.buf, unitdata->addr.maxlen);
		bzero((char *)&unitdata->addr, sizeof (unitdata->addr));
		t_kfree(tiptr, (caddr_t)unitdata, T_UNITDATA);
	}

	if ((p->cku_err.re_status != RPC_SUCCESS) &&
	    (p->cku_err.re_status != RPC_INTR) &&
	    (p->cku_err.re_status != RPC_CANTENCODEARGS)) {
		if (p->cku_feedback != NULL && stries == p->cku_retrys) {
			(*p->cku_feedback)(FEEDBACK_REXMIT1, procnum,
					    p->cku_feedarg);
		}
		timohz = backoff(timohz);
		if (p->cku_timeall != (struct rpc_timers *)0)
			p->cku_timeall->rt_rtxcur = timohz;
		if (p->cku_err.re_status == RPC_SYSTEMERROR ||
		    p->cku_err.re_status == RPC_CANTSEND) {
			/*
			 * Errors due to lack of resources, wait a bit
			 * and try again.
			 */
			(void) delay(10);
			/* (void) sleep((caddr_t)&lbolt, PZERO-4); */
		}
		if (stries-- > 0) {
			RCSTAT_INCR(rcretrans);
			goto call_again;
		}
	}

	if (mpdup != NULL)
		freemsg(mpdup);

	if (p->cku_err.re_status != RPC_SUCCESS) {
		RCSTAT_INCR(rcbadcalls);
	}
	return (p->cku_err.re_status);
}

static enum clnt_stat
clnt_clts_kcallit(CLIENT *h, u_long procnum, xdrproc_t xdr_args,
	caddr_t argsp, xdrproc_t xdr_results, caddr_t resultsp,
	struct timeval wait)
{

	return (clnt_clts_kcallit_addr(h, procnum, xdr_args, argsp,
				xdr_results, resultsp, wait, NULL));
}

/*
 * Return error info on this handle.
 */
static void
clnt_clts_kerror(CLIENT *h, struct rpc_err *err)
{
	/* LINTED pointer alignment */
	register struct cku_private *p = htop(h);

	*err = p->cku_err;
}

static bool_t
clnt_clts_kfreeres(CLIENT *h, xdrproc_t xdr_res, caddr_t res_ptr)
{
	/* LINTED pointer alignment */
	register struct cku_private *p = htop(h);
	register XDR *xdrs;

	xdrs = &(p->cku_outxdr);
	xdrs->x_op = XDR_FREE;
	return ((*xdr_res)(xdrs, res_ptr));
}

static void
clnt_clts_kabort(void)
{
}

/* ARGSUSED */
static bool_t
clnt_clts_kcontrol(CLIENT *h, int cmd, char *arg)
{

	return (FALSE);
}

/*
 * Destroy rpc handle.
 * Frees the space used for output buffer, private data, and handle
 * structure, and the file pointer/TLI data on last reference.
 */
static void
clnt_clts_kdestroy(CLIENT *h)
{
	/* LINTED pointer alignment */
	register struct cku_private *p = htop(h);
	register TIUSER *tiptr;

	tiptr = p->cku_tiptr;

	kmem_free((caddr_t)p->cku_addr.buf, p->cku_addr.maxlen);
	kmem_free((caddr_t)p, sizeof (*p));

	(void) t_kclose(tiptr, 1);
}
