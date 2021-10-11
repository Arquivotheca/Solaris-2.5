/*
 * Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T
 *		All Rights Reserved
 */

/*
 *	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
 *	The copyright notice above does not evidence any
 *	actual or intended publication of such source code.
 */

#ident	"@(#)clnt_cots.c 1.31	95/10/20 SMI"	/* SVr4.0 1.10  */

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
#include <sys/user.h>
#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/proc.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/file.h>
#include <sys/stream.h>
#include <sys/strsubr.h>
#include <sys/stropts.h>
#include <sys/timod.h>
#include <sys/tiuser.h>
#include <sys/tihdr.h>
#include <sys/t_kuser.h>
#include <sys/fcntl.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/debug.h>
#include <sys/systm.h>
#include <sys/kstat.h>
#include <sys/t_lock.h>
#include <sys/ddi.h>
#include <sys/cmn_err.h>
#include <sys/time.h>

#include <netinet/in.h>
#include <netinet/tcp.h>

#include <rpc/types.h>
#include <rpc/xdr.h>
#include <rpc/auth.h>
#include <rpc/clnt.h>
#include <rpc/rpc_msg.h>

#define	COTS_DEFAULT_ALLOCSIZE	2048

#define	WIRE_HDR_SIZE	20	/* serialized call header, sans proc number */
#define	MSG_OFFSET	128	/* offset of call into the mblk */

static int		clnt_cots_ksettimers(CLIENT *, struct rpc_timers *,
				struct rpc_timers *, int,
				void(*)(int, int, caddr_t), caddr_t, u_long);
static enum clnt_stat	clnt_cots_kcallit(CLIENT *, u_long, xdrproc_t, caddr_t,
				xdrproc_t, caddr_t, struct timeval);
static void		clnt_cots_kabort(void);
static void		clnt_cots_kerror(CLIENT *, struct rpc_err *);
static bool_t		clnt_cots_kfreeres(CLIENT *, xdrproc_t, caddr_t);
static void		clnt_cots_kdestroy(CLIENT *);
static bool_t		clnt_cots_kcontrol(CLIENT *, int, char *);

/* List of outstanding calls awaiting replies, for COTS */
typedef struct calllist_s {
	u_int		call_xid;	/* the xid on the call */
	mblk_t		*call_reply;	/* the reply to the call */
	kcondvar_t	call_cv;	/* cv to notify when reply is done */
	enum clnt_stat	call_status;	/* error on reply (rep is invalid) */
	queue_t		*call_wq;	/* the write queue the call is using */
	struct calllist_s *call_next;
	struct calllist_s *call_prev;
} calllist_t;

/* List of transports managed by the connection manager. */
struct cm_xprt {
	TIUSER		*x_tiptr;	/* transport handle */
	queue_t		*x_wq;		/* send queue */
	u_long		x_time;		/* last time we handed this xprt out */
	int		x_tidu_size;    /* TIDU size of this transport */
	union {
	    struct {
		unsigned int
		b_closing:	1,	/* we've sent a ord rel on this conn */
		b_dead:		1,	/* transport is closed or disconn */
		b_doomed:	1,	/* too many conns, let this go idle */
		b_connected:	1,	/* this connection is connected */
		b_ordrel:	1,	/* do an orderly release? */
		b_pad:		27;
	    } bit;
	    int word;
#define	x_closing	x_state.bit.b_closing
#define	x_dead		x_state.bit.b_dead
#define	x_doomed	x_state.bit.b_doomed
#define	x_connected	x_state.bit.b_connected
#define	x_ordrel	x_state.bit.b_ordrel

	}		x_state;
	int		x_ref;		/* number of users of this xprt */
	int		x_family;	/* address family of transport */
	dev_t		x_rdev;		/* device number of transport */
	struct cm_xprt	*x_next;
	struct netbuf	x_server;	/* destination address */
	struct netbuf	x_src;		/* src address (for retries) */
	kmutex_t	x_lock;		/* lock on this entry */
	kcondvar_t	x_cv;		/* to signal when can be closed */
};

/*
 * Private data per rpc handle.  This structure is allocated by
 * clnt_cots_kcreate, and freed by clnt_cots_kdestroy.
 */
typedef struct cku_private_s {
	CLIENT			cku_client;	/* client handle */
	calllist_t		cku_call;	/* for dispatching calls */
	struct netbuf		cku_srcaddr;	/* source address for retries */
	int			cku_addrfmly;  /* for binding port */
	struct netbuf		cku_addr;	/* remote address */
	dev_t			cku_device;	/* device to use */
	u_long			cku_xid;	/* current XID */
	struct rpc_err		cku_err;	/* error status */
	XDR			cku_outxdr;	/* xdr routine for output */
	XDR			cku_inxdr;	/* xdr routine for input */
	char			cku_rpchdr[WIRE_HDR_SIZE];
						/* pre-serialized rpc header */
	u_int			cku_outbuflen;	/* default output mblk length */
	struct cred		*cku_cred;	/* credentials */
} cku_private_t;

static bool_t	connmgr_connect(queue_t *, struct netbuf *, int,
			calllist_t *, int *, bool_t reconnect, struct timeval);
static bool_t	connmgr_reconnect(struct cm_xprt *, cku_private_t *,
				struct cm_xprt **, struct timeval);
static bool_t	connmgr_setopt(queue_t *, long, long, calllist_t *);
static void	connmgr_sndrel(queue_t *);
static void	connmgr_close(struct cm_xprt *);
static void	connmgr_release(struct cm_xprt *);
static struct cm_xprt *connmgr_get(struct netbuf *, struct timeval,
		cku_private_t *);

void		clnt_dispatch_send(queue_t *, mblk_t *, calllist_t *, u_int);

/*
 * Operations vector for TCP/IP based RPC
 */
static struct clnt_ops tcp_ops = {
	clnt_cots_kcallit,	/* do rpc call */
	clnt_cots_kabort,	/* abort call */
	clnt_cots_kerror,	/* return error status */
	clnt_cots_kfreeres,	/* free results */
	clnt_cots_kdestroy,	/* destroy rpc handle */
	clnt_cots_kcontrol,	/* the ioctl() of rpc */
	clnt_cots_ksettimers,	/* set retry timers */
};

static struct cm_xprt *cm_hd = (struct cm_xprt *)0;
extern kmutex_t connmgr_lock;

extern kmutex_t clnt_max_msg_lock;

static calllist_t *clnt_pending = (calllist_t *)0;
extern kmutex_t clnt_pending_lock;

struct {
	kstat_named_t	rccalls;
	kstat_named_t	rcbadcalls;
	kstat_named_t	rcbadxids;
	kstat_named_t	rctimeouts;
	kstat_named_t	rcnewcreds;
	kstat_named_t	rcbadverfs;
	kstat_named_t	rctimers;
	kstat_named_t	rccantconn;
	kstat_named_t	rcnomem;
	kstat_named_t	rcintrs;
} cotsrcstat = {
	{ "calls",	KSTAT_DATA_ULONG },
	{ "badcalls",	KSTAT_DATA_ULONG },
	{ "badxids",	KSTAT_DATA_ULONG },
	{ "timeouts",	KSTAT_DATA_ULONG },
	{ "newcreds",	KSTAT_DATA_ULONG },
	{ "badverfs",	KSTAT_DATA_ULONG },
	{ "timers",	KSTAT_DATA_ULONG },
	{ "cantconn",	KSTAT_DATA_ULONG },
	{ "nomem",	KSTAT_DATA_ULONG },
	{ "interrupts", KSTAT_DATA_ULONG }
};


#define	COTSRCSTAT_INCR(x)	cotsrcstat.x.value.ul++
#define	COTS_DEQUEUE_CALL(call) \
	mutex_enter(&clnt_pending_lock); \
	if ((call)->call_prev) \
		(call)->call_prev->call_next = (call)->call_next; \
	else \
		clnt_pending = (call)->call_next; \
	if ((call)->call_next) \
		(call)->call_next->call_prev = (call)->call_prev; \
	mutex_exit(&clnt_pending_lock)


kstat_named_t *cotsrcstat_ptr = (kstat_named_t *) &cotsrcstat;
ulong_t cotsrcstat_ndata = sizeof (cotsrcstat) / sizeof (kstat_named_t);

#define	MAX_CONNS	1	/* concurrent connections between clnt/srvr */
static int clnt_max_conns = MAX_CONNS;

u_long *clnt_max_msg_sizep;
void (*clnt_stop_idle)(queue_t * wq);

#define	ptoh(p)		(&((p)->cku_client))
#define	htop(h)		((cku_private_t *)((h)->cl_private))

/*
 * Times to retry
 */
#define	REFRESHES	2	/* authentication refreshes */

int
clnt_cots_kcreate(dev_t dev, struct netbuf *addr, int family, u_long prog,
	u_long vers, u_int max_msgsize, cred_t *cred, CLIENT **ncl)
{
	register CLIENT *h;
	register cku_private_t *p;
	struct rpc_msg call_msg;

	RPCLOG(16, "clnt_cots_kcreate: prog %d, ", prog);

	/* Allocate and intialize the client handle. */
	p = (cku_private_t *)kmem_zalloc(sizeof (*p), KM_SLEEP);

	h = ptoh(p);

	h->cl_private = (caddr_t) p;
	h->cl_auth = authkern_create();
	h->cl_ops = &tcp_ops;

	cv_init(&p->cku_call.call_cv, "rpc call wait", CV_DEFAULT, NULL);

	/*
	 * If the current sanity check size in rpcmod is smaller
	 * than the size needed, then increase the sanity check.
	 */
	if (max_msgsize != 0 && clnt_max_msg_sizep != NULL &&
	    max_msgsize > *clnt_max_msg_sizep) {
		mutex_enter(&clnt_max_msg_lock);
		if (max_msgsize > *clnt_max_msg_sizep)
			*clnt_max_msg_sizep = max_msgsize;
		mutex_exit(&clnt_max_msg_lock);
	}

	p->cku_outbuflen = COTS_DEFAULT_ALLOCSIZE;

	/* Preserialize the call message header */

	call_msg.rm_xid = 0;
	call_msg.rm_direction = CALL;
	call_msg.rm_call.cb_rpcvers = RPC_MSG_VERSION;
	call_msg.rm_call.cb_prog = prog;
	call_msg.rm_call.cb_vers = vers;

	xdrmem_create(&p->cku_outxdr, p->cku_rpchdr, WIRE_HDR_SIZE, XDR_ENCODE);

	if (!xdr_callhdr(&p->cku_outxdr, &call_msg)) {
		RPCLOG(1,
		"clnt_cots_kcreate - Fatal header serialization error %d\n", 0);
		kmem_free((caddr_t)p, (u_int)sizeof (cku_private_t));
		RPCLOG(1,
		"clnt_cots_kcreate: create failed error %d\n", EINVAL);
		return (EINVAL);		/* XXX */
	}

	/*
	 * The zalloc initialized the fields below.
	 * p->cku_xid = 0;
	 * p->cku_srcaddr.len = 0;
	 * p->cku_srcaddr.maxlen = 0;
	 */

	p->cku_cred = cred;
	p->cku_device = dev;
	p->cku_addrfmly = family;
	p->cku_addr.buf = (char *)kmem_zalloc(addr->maxlen, KM_SLEEP);
	p->cku_addr.maxlen = addr->maxlen;
	p->cku_addr.len = addr->len;
	bcopy(addr->buf, p->cku_addr.buf, addr->len);

	*ncl = h;
	return (0);
}

static void
clnt_cots_kabort(void)
{
}

/*
 * Return error info on this handle.
 */
static void
clnt_cots_kerror(CLIENT *h, struct rpc_err *err)
{
	/* LINTED pointer alignment */
	register cku_private_t *p = htop(h);

	*err = p->cku_err;
}

static bool_t
clnt_cots_kfreeres(CLIENT *h, xdrproc_t xdr_res, caddr_t res_ptr)
{
	/* LINTED pointer alignment */
	register cku_private_t *p = htop(h);
	register XDR *xdrs;

	xdrs = &(p->cku_outxdr);
	xdrs->x_op = XDR_FREE;
	return ((*xdr_res)(xdrs, res_ptr));
}

/* ARGSUSED */
static bool_t
clnt_cots_kcontrol(CLIENT *h, int cmd, char *arg)
{

	return (FALSE);
}

/*
 * Destroy rpc handle.  Frees the space used for output buffer,
 * private data, and handle structure.
 */
static void
clnt_cots_kdestroy(CLIENT *h)
{
	/* LINTED pointer alignment */
	register cku_private_t *p = htop(h);

	RPCLOG(16, "clnt_cots_kdestroy h: %x\n", (int)h);

	if (p->cku_call.call_reply)
		freemsg(p->cku_call.call_reply);
	kmem_free((caddr_t)p->cku_srcaddr.buf, p->cku_srcaddr.maxlen);
	kmem_free((caddr_t)p->cku_addr.buf, p->cku_addr.maxlen);
	kmem_free((caddr_t)p, sizeof (*p));
}

int clnt_cots_pulls;
#define	RM_HDR_SIZE	4	/* record mark header size */

/*
 * Call remote procedure.
 */
static enum clnt_stat
clnt_cots_kcallit(CLIENT *h, u_long procnum, xdrproc_t xdr_args, caddr_t argsp,
	xdrproc_t xdr_results, caddr_t resultsp, struct timeval wait)
{
	/* LINTED pointer alignment */
	register cku_private_t *p = htop(h);
	register XDR *xdrs;
	struct rpc_msg reply_msg;
	mblk_t *mp;
	mblk_t *tmp, *tmp_prev;
	calllist_t *call;
#ifdef	RPCDEBUG
	long time_sent;
#endif
	struct netbuf *retry_addr;
	struct cm_xprt *cm_entry;
	queue_t *wq;
	int len;
	int mpsize;
	int refreshes = REFRESHES;
	int interrupted;
	int tidu_size;
	enum clnt_stat call_status;

	RPCLOG(2, "clnt_cots_kcallit, procnum %d\n", procnum);
	COTSRCSTAT_INCR(rccalls);

	RPCLOG(2, "clnt_cots_kcallit: wait.tv_sec: %d\n", wait.tv_sec);
	RPCLOG(2, "clnt_cots_kcallit: wait.tv_usec: %d\n", wait.tv_usec);

call_again:
	mp = (mblk_t *)0;

	/*
	 * If the call is not a retry, allocate a new xid and cache it
	 * for future retries.
	 */
	if (p->cku_xid == 0) {
		p->cku_xid = alloc_xid();
		retry_addr = NULL;
	} else {
		retry_addr = &p->cku_srcaddr;
		RPCLOG(1, "clnt_cots_callit: retrying xid %x", p->cku_xid);
		RPCLOG(1, " with wait of %d\n", wait.tv_sec);
	}

	cm_entry = connmgr_get(retry_addr, wait, p);
	if (cm_entry == NULL) {
		RPCLOG(16, "clnt_cots_kcallit: can't connect status %d\n",
			p->cku_call.call_status);
		/*
		 * Even if we end up returning EINTR, we still count a
		 * a "can't connect", because the connection manager
		 * might have been committed to waiting for or timing out on
		 * a connection.
		 */
		COTSRCSTAT_INCR(rccantconn);
		p->cku_err.re_status = p->cku_call.call_status;

		if (p->cku_call.call_status == RPC_INTR) {
			p->cku_err.re_errno = EINTR;
		} else if (p->cku_call.call_status == RPC_TIMEDOUT) {
			p->cku_err.re_errno = ETIMEDOUT;
		} else {
			p->cku_err.re_errno = EIO;
		}
		goto cots_done;
	}

	/*
	 * Now we create the RPC request in a STREAMS message.  We have to do
	 * this after the call to connmgr_get so that we have the correct
	 * TIDU size for the transport.
	 */
	tidu_size = cm_entry->x_tidu_size;
	len = MSG_OFFSET + MAX(tidu_size, RM_HDR_SIZE + WIRE_HDR_SIZE);

	while ((mp = allocb(len, BPRI_MED)) == NULL) {
		if (strwaitbuf(len, BPRI_MED)) {
			connmgr_release(cm_entry);
			p->cku_err.re_status = RPC_SYSTEMERROR;
			p->cku_err.re_errno = ENOSR;
			COTSRCSTAT_INCR(rcnomem);
			goto cots_done;
		}
	}
	xdrs = &p->cku_outxdr;
	xdrmblk_init(xdrs, mp, XDR_ENCODE, tidu_size);
	mpsize = bpsize(mp);
	ASSERT(mpsize >= len);
	ASSERT(mp->b_rptr == mp->b_datap->db_base);

	/*
	 * If the size of mblk is not appreciably larger then what we
	 * asked, then resize the mblk to exactly len bytes. The reason for
	 * this: suppose len is 1600 bytes, the tidu is 1460 bytes
	 * (from TCP over ethernet), and the arguments to the RPC require
	 * 2800 bytes. Ideally we want the protocol to render two
	 * ~1400 byte segments over the wire. However if allocb() gives us a 2k
	 * mblk, and we allocate a second mblk for the remainder, the protocol
	 * module may generate 3 segments over the wire:
	 * 1460 bytes for the first, 448 (2048 - 1600) for the second, and
	 * 892 for the third. If we "waste" 448 bytes in the first mblk,
	 * the XDR encoding will generate two ~1400 byte mblks, and the
	 * protocol module is more likely to produce properly sized segments.
	 */
	if ((mpsize >> 1) <= len) {
		mp->b_rptr += (mpsize - len);
	}

	/*
	 * Adjust b_rptr to reserve space for the non-data protocol headers
	 * any downstream modules might like to add, and for the
	 * record marking header.
	 */
	mp->b_rptr += (MSG_OFFSET + RM_HDR_SIZE);

	/*
	 * Copy in the preserialized RPC header information.
	 */
	bcopy(p->cku_rpchdr, (caddr_t)mp->b_rptr, WIRE_HDR_SIZE);

	/*
	 * Use XDR_SETPOS() to set the b_wptr to past the RPC header.
	 */
	XDR_SETPOS(xdrs, mp->b_rptr - mp->b_datap->db_base + WIRE_HDR_SIZE);

	ASSERT((mp->b_wptr - mp->b_rptr) == WIRE_HDR_SIZE);

	/*
	 * Serialize the procedure number and the arguments.
	 */
	if ((!XDR_PUTLONG(xdrs, (long *)&procnum)) ||
		(!AUTH_MARSHALL(h->cl_auth, xdrs, p->cku_cred)) ||
		(!(*xdr_args)(xdrs, argsp))) {
		p->cku_err.re_status = RPC_CANTENCODEARGS;
		p->cku_err.re_errno = EIO;
		connmgr_release(cm_entry);
		goto cots_done;
	}
	(*(u_long *)(mp->b_rptr)) = p->cku_xid;

	RPCLOG(16, "clnt_cots_callit: connected, sending call, tidu_size %d\n",
		tidu_size);

	call = &p->cku_call;
	wq = cm_entry->x_wq;
	clnt_dispatch_send(wq, mp, call, p->cku_xid);

	/* Release our reference on the connection. */
	connmgr_release(cm_entry);

#ifdef	RPCDEBUG
	time_sent = lbolt;
#endif
	/*
	 * Wait for a reply or a timeout.  If there is no error or timeout,
	 * (both indicated by call_status), p->cku_call.call_reply will contain
	 * the RPC reply message.
	 */
read_again:
	mutex_enter(&clnt_pending_lock);
	interrupted = 0;
	if (p->cku_call.call_status == RPC_TIMEDOUT) {
		/*
		 * Indicate that the lwp is not to be stopped while waiting
		 * for this network traffic.  This is to avoid deadlock while
		 * debugging a process via /proc and also to avoid recursive
		 * mutex_enter()s due to NFS page faults while stopping
		 * (NFS holds locks when it calls here).
		 */
		int cv_wait_ret;
		long timeout;

		klwp_t *lwp = ttolwp(curthread);

		if (lwp != NULL)
			lwp->lwp_nostop++;

		timeout = wait.tv_sec * drv_usectohz(1000000) +
			drv_usectohz(wait.tv_usec) + lbolt;
		while ((cv_wait_ret = cv_timedwait_sig(&p->cku_call.call_cv,
			&clnt_pending_lock, timeout)) > 0 &&
			p->cku_call.call_status == RPC_TIMEDOUT);
		if (cv_wait_ret == 0)
			interrupted = 1;

		if (lwp != NULL)
			lwp->lwp_nostop--;
	}

	/*
	 * Get the reply message, if any.  This will be freed at the end
	 * whether or not an error occurred.
	 */
	mp = p->cku_call.call_reply;
	p->cku_call.call_reply = (mblk_t *)0;
	call_status = p->cku_call.call_status;
	mutex_exit(&clnt_pending_lock);

	/*
	 * Make sure the mblks are aligned correctly.
	 */
	tmp = mp;
	tmp_prev = NULL;
	while (tmp) {
		if (str_aligned(tmp->b_rptr) == 0)
			break;
		/*
		 * The length of each mblk needs to aligned. Since b_rptr
		 * aligned, so must b_wptr. However, don't bother worrying
		 * about extra bytes on the last mblk.
		 */
		if (str_aligned(tmp->b_wptr) == 0 && tmp->b_cont != NULL)
			break;
		/*
		 * Save the previous mblk.
		 */
		tmp_prev = tmp;
		tmp = tmp->b_cont;
	}

	if (tmp) {
		mblk_t *old_mp;

		clnt_cots_pulls++;
		old_mp = tmp;
		while ((tmp = msgpullup(old_mp, -1)) == NULL)
			delay(HZ);
		freemsg(old_mp);
		if (tmp_prev)
			tmp_prev->b_cont = tmp;
		else
			mp = tmp;
	}

	if (call_status != RPC_SUCCESS) {
		p->cku_err.re_status = call_status;
		switch (call_status) {
		case RPC_TIMEDOUT:
			RPCLOG(1, "clnt_cots_kcallit: xid %x", p->cku_xid);
			if (interrupted) {
				COTSRCSTAT_INCR(rcintrs);
				p->cku_err.re_status = RPC_INTR;
				p->cku_err.re_errno = EINTR;
				RPCLOG(1, " signal interrupted at %d", lbolt);
				RPCLOG(1, ", was sent at %d\n", time_sent);
			} else {
				COTSRCSTAT_INCR(rctimeouts);
				p->cku_err.re_errno = ETIMEDOUT;
				RPCLOG(1, " timed out at %d", lbolt);
				RPCLOG(1, ", was sent at %d\n", time_sent);
				RPCLOG(1, "clnt_cots_kcallit: timeout\n", 0);
			}
			break;

		case RPC_XPRTFAILED:
			p->cku_err.re_errno = EIO;
			RPCLOG(1, "clnt_cots_kcallit: transport failed\n", 0);
			break;

		default:
			p->cku_err.re_status = RPC_SYSTEMERROR;
			p->cku_err.re_errno = EIO;
			RPCLOG(1, "clnt_cots_kcallit: unknown error: %d\n",
				call_status);
			break;
		}
		COTS_DEQUEUE_CALL(call);
		goto cots_done;
	}

	xdrs = &p->cku_inxdr;
	xdrmblk_init(xdrs, mp, XDR_DECODE, 0);

	reply_msg.rm_direction = REPLY;
	reply_msg.rm_reply.rp_stat = MSG_ACCEPTED;
	reply_msg.acpted_rply.ar_stat = SUCCESS;

	reply_msg.acpted_rply.ar_verf = _null_auth;
	reply_msg.acpted_rply.ar_results.where = resultsp;
	reply_msg.acpted_rply.ar_results.proc = xdr_results;

	if (xdr_replymsg(xdrs, &reply_msg)) {
		_seterr_reply(&reply_msg, &p->cku_err);

		if (p->cku_err.re_status == RPC_SUCCESS) {
			/*
			 * Reply is good, check auth.
			 */
			if (! AUTH_VALIDATE(h->cl_auth,
				&reply_msg.acpted_rply.ar_verf)) {
				COTSRCSTAT_INCR(rcbadverfs);
				RPCLOG(1,
				"clnt_cots_kcallit: validation failure\n", 0);
				freemsg(mp);
				(void) xdr_rpc_free_verifier(xdrs, &reply_msg);
				mutex_enter(&clnt_pending_lock);
				if (call->call_reply == (mblk_t *)0)
					call->call_status = RPC_TIMEDOUT;
				mutex_exit(&clnt_pending_lock);
				goto read_again;
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
				refreshes--;
				(void) xdr_rpc_free_verifier(xdrs, &reply_msg);
				freemsg(mp);
				mp = NULL;
				COTS_DEQUEUE_CALL(call);
				if (call->call_reply) {
					freemsg(call->call_reply);
					call->call_reply = (mblk_t *)0;
				}
				COTSRCSTAT_INCR(rcbadcalls);
				COTSRCSTAT_INCR(rcnewcreds);
				goto call_again;
			}
		}
	} else {
		/* reply didn't decode properly. */
		p->cku_err.re_status = RPC_CANTDECODERES;
		p->cku_err.re_errno = EIO;
	}
	(void) xdr_rpc_free_verifier(xdrs, &reply_msg);

	COTS_DEQUEUE_CALL(call);
cots_done:
	if (mp)
		freemsg(mp);
	if (p->cku_call.call_reply) {
		freemsg(p->cku_call.call_reply);
		p->cku_call.call_reply = (mblk_t *)0;
	}
	if (p->cku_err.re_status != RPC_SUCCESS) {
		COTSRCSTAT_INCR(rcbadcalls);
	}
	return (p->cku_err.re_status);
}

/*
 * Kinit routine for cots.  This sets up the correct operations in
 * the client handle, as the handle may have previously been a clts
 * handle, and clears the xid field so there is no way a new call
 * could be mistaken for a retry.  It also sets in the handle the
 * information that is passed at create/kinit time but needed at
 * call time, as cots creates the transport at call time - device,
 * address of the server, protocol family.
 */
void
clnt_cots_kinit(CLIENT *h, dev_t dev, int family, struct netbuf *addr,
	int max_msgsize, cred_t *cred)
{
	/* LINTED pointer alignment */
	register cku_private_t *p = htop(h);

	h->cl_ops = &tcp_ops;
	p->cku_xid = 0;

	p->cku_device = dev;
	p->cku_addrfmly = family;
	p->cku_cred = cred;

	if (p->cku_addr.maxlen < addr->len) {
		if (p->cku_addr.maxlen != 0 && p->cku_addr.buf != NULL)
			(void) kmem_free(p->cku_addr.buf, p->cku_addr.maxlen);
		p->cku_addr.buf = (char *)kmem_zalloc(addr->maxlen, KM_SLEEP);
		p->cku_addr.maxlen = addr->maxlen;
	}

	p->cku_addr.len = addr->len;
	bcopy(addr->buf, p->cku_addr.buf, addr->len);

	/*
	 * If the current sanity check size in rpcmod is smaller
	 * than the size needed, then increase the sanity check.
	 */
	if (max_msgsize != 0 && clnt_max_msg_sizep != NULL &&
	    max_msgsize > *clnt_max_msg_sizep) {
		mutex_enter(&clnt_max_msg_lock);
		if (max_msgsize > *clnt_max_msg_sizep)
			*clnt_max_msg_sizep = max_msgsize;
		mutex_exit(&clnt_max_msg_lock);
	}
}

/*
 * ksettimers is a no-op for cots, with the exception of setting the xid.
 */
/* ARGSUSED */
static int
clnt_cots_ksettimers(CLIENT *h, struct rpc_timers *t, struct rpc_timers *all,
	int minimum, void (*feedback)(int, int, caddr_t), caddr_t arg,
	u_long xid)
{
	/* LINTED pointer alignment */
	register cku_private_t *p = htop(h);

	if (xid)
		p->cku_xid = xid;
	COTSRCSTAT_INCR(rctimers);
	return (0);
}

extern void rpc_poptimod(struct vnode *);
extern int kstr_push(struct vnode *, char *);

struct cm_kstat_xprt {
	kstat_named_t	x_wq;
	kstat_named_t	x_server;
	kstat_named_t	x_family;
	kstat_named_t	x_rdev;
	kstat_named_t	x_time;
	kstat_named_t	x_state;
	kstat_named_t	x_ref;
} cm_kstat_template = {
	{ "write queue", KSTAT_DATA_ULONG },
	{ "server",	KSTAT_DATA_ULONG },
	{ "addr. family", KSTAT_DATA_ULONG },
	{ "device",	KSTAT_DATA_ULONG },
	{ "time stamp",	KSTAT_DATA_ULONG },
	{ "status",	KSTAT_DATA_ULONG },
	{ "ref count",	KSTAT_DATA_LONG }
};

conn_kstat_update(kstat_t *ksp, int rw)
{
	int n = 0;
	struct cm_xprt *cm_entry;

	if (rw == KSTAT_WRITE)
		return (EACCES);
	cm_entry = cm_hd;
	while (cm_entry) {
		ASSERT(cm_entry != cm_entry->x_next);
		cm_entry = cm_entry->x_next;
		n++;
	}
	if (n == 0)
		n = 1;
	ksp->ks_data_size = n * sizeof (struct cm_kstat_xprt);
	ksp->ks_ndata = ksp->ks_data_size / sizeof (kstat_named_t);
	return (0);
}

conn_kstat_snapshot(kstat_t *ksp, void *buf, int rw)
{
	struct cm_xprt *cm_entry;
	struct cm_kstat_xprt *ck_entry, *temp;
	int n = ksp->ks_ndata /
		(sizeof (struct cm_kstat_xprt) / sizeof (kstat_named_t));

	ksp->ks_snaptime = gethrtime();
	if (rw == KSTAT_WRITE)
		return (EACCES);

	temp = &cm_kstat_template;
	ck_entry = (struct cm_kstat_xprt *)buf;

	cm_entry = cm_hd;

	while (n > 0 && cm_entry) {
		*ck_entry = *temp;
		ck_entry->x_wq.value.ul = (ulong)cm_entry->x_wq;
		ck_entry->x_family.value.ul = (ulong)cm_entry->x_family;
		if (cm_entry->x_server.buf) {
			if (cm_entry->x_family == AF_INET &&
			    cm_entry->x_server.len >= 8) {
				bcopy(&cm_entry->x_server.buf[4],
					(caddr_t)&ck_entry->x_server.value.ul,
					sizeof (long));
			} else {
				bcopy(cm_entry->x_server.buf,
					(caddr_t)&ck_entry->x_server.value.ul,
					MIN(sizeof (long),
					cm_entry->x_server.len));
			}
		}
		ck_entry->x_rdev.value.ul = (ulong)cm_entry->x_rdev;
		ck_entry->x_time.value.ul = cm_entry->x_time;
		bcopy((caddr_t)&cm_entry->x_state,
			(caddr_t)&ck_entry->x_state.value.ul, 4);
		ck_entry->x_ref.value.ul = cm_entry->x_ref;
		n--;
		ASSERT(cm_entry != cm_entry->x_next);
		cm_entry = cm_entry->x_next;
		ck_entry++;
	}
	return (0);
}

/*
 * Obtains a transport to the server specified in addr.  If a suitable transport
 * does not already exist in the list of cached transports, a new connection
 * is created, connected, and added to the list. The connection is for sending
 * only - the reply message may come back on another transport connection.
 */
static struct cm_xprt *
connmgr_get(struct netbuf *retry_addr, struct timeval wait, cku_private_t *p)
{
	struct cm_xprt *cm_entry;
	struct cm_xprt *lru_entry;
	struct cm_xprt **cmp;
	queue_t *wq;
	TIUSER *tiptr;
	cred_t *savecred;
	cred_t *tmpcred;
	int i;
	int retval;
	u_long prev_time;
	struct netbuf *dest_addr = &p->cku_addr;
	int tidu_size;

	/*
	 * If the call is not a retry, look for a transport entry that
	 * goes to the server of interest.
	 */
	mutex_enter(&connmgr_lock);

	if (retry_addr == NULL) {
use_new_conn:
		i = 0;
		cm_entry = lru_entry = (struct cm_xprt *)0;
		prev_time = lbolt;

		cmp = &cm_hd;
		while ((cm_entry = *cmp) != NULL) {
			ASSERT(cm_entry != cm_entry->x_next);
			if (cm_entry->x_dead) {
				*cmp = cm_entry->x_next;
				mutex_exit(&connmgr_lock);
				connmgr_close(cm_entry);
				mutex_enter(&connmgr_lock);
				goto use_new_conn;
			}


			if (cm_entry->x_closing == FALSE &&
			    cm_entry->x_doomed == FALSE &&
			    cm_entry->x_rdev == p->cku_device &&
			    dest_addr->len == cm_entry->x_server.len &&
			    bcmp(dest_addr->buf, cm_entry->x_server.buf,
			    dest_addr->len) == 0) {
				/*
				 * If the matching entry isn't connected,
				 * attempt to reconnect it.
				 */
				if (cm_entry->x_connected == FALSE) {
					if (connmgr_reconnect(cm_entry, p, cmp,
							wait)
					    == TRUE) {
						/*
						 * Copy into the handle the
						 * source address of the
						 * connection, which we will use
						 * in case of a later retry.
						 */
						if (p->cku_srcaddr.len !=
							cm_entry->x_src.len) {
						    if (p->cku_srcaddr.len > 0)
							kmem_free((caddr_t)
							    p->cku_srcaddr.buf,
							    p->cku_srcaddr.len);
						    p->cku_srcaddr.buf =
							kmem_zalloc(
							    cm_entry->x_src.len,
							    KM_SLEEP);
						    p->cku_srcaddr.maxlen =
							p->cku_srcaddr.len =
							cm_entry->x_src.len;
						}
						bcopy(cm_entry->x_src.buf,
							p->cku_srcaddr.buf,
							p->cku_srcaddr.len);
						cm_entry->x_time = lbolt;
						mutex_enter(&cm_entry->x_lock);
						cm_entry->x_ref++;
						mutex_exit(&cm_entry->x_lock);
						mutex_exit(&connmgr_lock);
						/*
						 * We don't go through trying
						 * to find the least recently
						 * used connected because
						 * connmgr_reconnect() briefly
						 * dropped the connmgr_lock,
						 * allowing a window for our
						 * accounting to be messed up.
						 * In any case, an unconnected
						 * connection is as good as
						 * a LRU connection.
						 */
						return (cm_entry);
					} else {
						/*
						 * Even though this isn't
						 * a retry, there is no point
						 * trying to connect to the
						 * same server with a different
						 * connection ... it is going to
						 * timeout just the same.
						 */
						mutex_exit(&connmgr_lock);
						delay((wait.tv_sec *
						    drv_usectohz(1000000)) +
						    drv_usectohz(wait.tv_usec));
						return ((struct cm_xprt *)0);
					}
				}
				i++;
				if (cm_entry->x_time <= prev_time ||
					lru_entry == NULL) {
					prev_time = cm_entry->x_time;
					lru_entry = cm_entry;
				}
			}
			cmp = &cm_entry->x_next;
		}

		if (i > clnt_max_conns) {
			RPCLOG(16,
			"connmgr_get: too many conns, dooming entry %x\n",
			lru_entry->x_tiptr);
			lru_entry->x_doomed = TRUE;
			goto use_new_conn;
		}

		/*
		 * If we are at the maximum number of connections to
		 * the server, hand back the least recently used one.
		 */
		if (i == clnt_max_conns) {

			/*
			 * Copy into the handle the source address of
			 * the connection, which we will use in case of
			 * a later retry.
			 */
			if (p->cku_srcaddr.len != lru_entry->x_src.len) {
				if (p->cku_srcaddr.len > 0)
					kmem_free((caddr_t)p->cku_srcaddr.buf,
						p->cku_srcaddr.len);
				p->cku_srcaddr.buf = kmem_zalloc(
					lru_entry->x_src.len, KM_SLEEP);
				p->cku_srcaddr.maxlen = p->cku_srcaddr.len =
					lru_entry->x_src.len;
			}
			bcopy(lru_entry->x_src.buf, p->cku_srcaddr.buf,
					p->cku_srcaddr.len);
			RPCLOG(16, "connmgr_get: call going out on %x\n",
				(int)lru_entry);
			lru_entry->x_time = lbolt;
			mutex_enter(&lru_entry->x_lock);
			lru_entry->x_ref++;
			mutex_exit(&lru_entry->x_lock);
			mutex_exit(&connmgr_lock);
			return (lru_entry);
		}

	} else {
		/*
		 * This is the retry case (retry_addr != NULL).  Retries must
		 * be sent on the same source port as the original call.
		 */

		/*
		 * Walk the list looking for a connection with a source address
		 * that matches the retry address.
		 */

		cmp = &cm_hd;
		while ((cm_entry = *cmp) != NULL) {
			ASSERT(cm_entry != cm_entry->x_next);
			if (p->cku_device != cm_entry->x_rdev ||
				retry_addr->len != cm_entry->x_src.len ||
				bcmp(retry_addr->buf, cm_entry->x_src.buf,
					retry_addr->len) != 0) {
				cmp = &cm_entry->x_next;
				continue;
			}

			/*
			 * Sanity check: if the connection with our source
			 * port is going to some other server, something went
			 * wrong, as we never delete connections (i.e. release
			 * ports) unless they have been idle.  In this case,
			 * it is probably better to send the call out using
			 * a new source address than to fail it altogether,
			 * since that port may never be released.
			 */
			if (dest_addr->len != cm_entry->x_server.len ||
				bcmp(dest_addr->buf, cm_entry->x_server.buf,
				dest_addr->len) != 0) {
				RPCLOG(1, "connmgr_get: tiptr %x",
					cm_entry->x_tiptr);
				RPCLOG(1, " is going to a different server", 0);
				RPCLOG(1, " with the port that belongs", 0);
				RPCLOG(1, " to us!\n", 0);
				retry_addr = NULL;
				goto use_new_conn;
			}

			/*
			 * If the connection of interest is not connected and we
			 * can't reconnect it, then the server is probably
			 * still down.  Return NULL to the caller and let it
			 * retry later if it wants to.  We have a delay so the
			 * machine doesn't go into a tight retry loop.  If the
			 * entry was already connected, or the reconnected was
			 * successful, return this entry.
			 */
			if (cm_entry->x_connected == FALSE &&
				connmgr_reconnect(cm_entry, p, cmp, wait)
							== FALSE) {
				mutex_exit(&connmgr_lock);
				delay((wait.tv_sec * drv_usectohz(1000000)) +
					drv_usectohz(wait.tv_usec));
				return ((struct cm_xprt *)0);
			}

			cm_entry->x_time = lbolt;
			mutex_enter(&cm_entry->x_lock);
			cm_entry->x_ref++;
			mutex_exit(&cm_entry->x_lock);
			mutex_exit(&connmgr_lock);
			RPCLOG(16,
			"connmgr_get: found old transport for retry\n", 0);
			return (cm_entry);
		}

		/*
		 * We cannot find an entry in the list for this retry.
		 * Either the entry has been removed temporarily to be
		 * reconnected by another thread, or the original call
		 * got a port but never got connected,
		 * and hence the transport never got put in the
		 * list.  Fall through to the "create new connection" code -
		 * the former case will fail there trying to rebind the port,
		 * and the later case (and any other pathological cases) will
		 * rebind and reconnect and not hang the client machine.
		 */
		RPCLOG(16, "connmgr_get: no entry in list for retry\n", 0);
	}
	mutex_exit(&connmgr_lock);

	/*
	 * Either we didn't find an entry to the server of interest, or we
	 * don't have the maximum number of connections to that server -
	 * create a new connection.
	 */
	RPCLOG(16, "connmgr_get: creating new connection\n", 0);
	savecred = CRED();
	tmpcred = crdup(savecred);
	tmpcred->cr_uid = 0;
	p->cku_call.call_status = RPC_TLIERROR;

	i = t_kopen(NULL, p->cku_device, FREAD|FWRITE|FNDELAY, &tiptr, tmpcred);
	crfree(tmpcred);
	if (i) {
		long timeout;
		int cv_wait_ret;

		RPCLOG(1, "connmgr_get: can't open cots device, %d\n", i);
		mutex_enter(&clnt_pending_lock);
		timeout = wait.tv_sec * drv_usectohz(1000000) +
			drv_usectohz(wait.tv_usec) + lbolt;
		while ((cv_wait_ret = cv_timedwait_sig(&p->cku_call.call_cv,
			&clnt_pending_lock, timeout)) > 0 &&
			p->cku_call.call_status == RPC_TLIERROR);
		if (cv_wait_ret == 0)
			p->cku_call.call_status = RPC_INTR;
		mutex_exit(&clnt_pending_lock);
		return ((struct cm_xprt *)0);
	}
	rpc_poptimod(tiptr->fp->f_vnode);

	if (i = kstr_push(tiptr->fp->f_vnode, "rpcmod")) {
		RPCLOG(1, "connmgr_get: can't push cots module, %d\n", (int) i);
		t_kclose(tiptr, 1);
		return ((struct cm_xprt *)0);
	}

	if (i = strioctl(tiptr->fp->f_vnode, RPC_CLIENT, 0, 0, K_TO_K,
		CRED(), &retval)) {
		RPCLOG(1,
		"connmgr_get: can't set client status with cots module\n", i);
		t_kclose(tiptr, 1);
		return ((struct cm_xprt *)0);
	}

	wq = tiptr->fp->f_vnode->v_stream->sd_wrq->q_next;

	if (i = kstr_push(tiptr->fp->f_vnode, "timod")) {
		RPCLOG(1, "connmgr_get: can't push timod, %d\n", i);
		t_kclose(tiptr, 1);
		return ((struct cm_xprt *)0);
	}

	if (p->cku_addrfmly == AF_INET) {
		bool_t alloc_src = FALSE;

		if (p->cku_srcaddr.len != dest_addr->len) {
			kmem_free((caddr_t)p->cku_srcaddr.buf,
				p->cku_srcaddr.len);
			p->cku_srcaddr.buf =
				kmem_zalloc(dest_addr->len, KM_SLEEP);
			p->cku_srcaddr.maxlen = dest_addr->len;
			p->cku_srcaddr.len = dest_addr->len;
			alloc_src = TRUE;
		}

		if ((i = bindresvport(tiptr, retry_addr, &p->cku_srcaddr,
		    TRUE)) != 0) {
			t_kclose(tiptr, 1);
			RPCLOG(1, "connmgr_get: couldn't bind,", 0);
			RPCLOG(1, " retry_addr: %x\n", (int)retry_addr);

			/*
			 * Refrain from putting the machine in a tight spin
			 * trying to grab a port that is already in use.
			 */
			delay((wait.tv_sec * drv_usectohz(1000000)) +
				drv_usectohz(wait.tv_usec));

			/*
			 * 1225408: If we allocated a source address, then it
			 * is either garbage or all zeroes. In that case
			 * we need to clear cku_srcaddr.
			 */
			if (alloc_src == TRUE) {
				kmem_free((caddr_t)p->cku_srcaddr.buf,
					p->cku_srcaddr.len);
				p->cku_srcaddr.maxlen = p->cku_srcaddr.len = 0;
				p->cku_srcaddr.buf = NULL;
			}
			return ((struct cm_xprt *)0);
		}
	} else {
		if ((i = t_kbind(tiptr, NULL, NULL)) != 0) {
			RPCLOG(1, "clnt_cots_kcreate: t_kbind: %d\n", i);
			t_kclose(tiptr, 1);
			return ((struct cm_xprt *)0);
		}
	}

	if (connmgr_connect(wq, dest_addr, p->cku_addrfmly,
			&p->cku_call, &tidu_size, FALSE, wait) == FALSE) {
		t_kclose(tiptr, 1);

		/*
		 * Don't delay if we got interrupted.
		 */
		if (p->cku_call.call_status != RPC_INTR) {
			delay((wait.tv_sec * drv_usectohz(1000000)) +
				drv_usectohz(wait.tv_usec));
		}
		return ((struct cm_xprt *)0);
	}

	/*
	 * Set up a transport entry in the connection manager's list.
	 */
	cm_entry = (struct cm_xprt *)
		kmem_zalloc(sizeof (struct cm_xprt), KM_SLEEP);

	cm_entry->x_server.buf = kmem_zalloc(dest_addr->len, KM_SLEEP);
	bcopy(dest_addr->buf, cm_entry->x_server.buf, dest_addr->len);
	cm_entry->x_server.len = cm_entry->x_server.maxlen = dest_addr->len;

	cm_entry->x_src.buf = kmem_zalloc(p->cku_srcaddr.len, KM_SLEEP);
	bcopy(p->cku_srcaddr.buf, cm_entry->x_src.buf, p->cku_srcaddr.len);
	cm_entry->x_src.len = cm_entry->x_src.maxlen = p->cku_srcaddr.len;

	cm_entry->x_tiptr = tiptr;
	cm_entry->x_closing = FALSE;
	cm_entry->x_dead = FALSE;
	cm_entry->x_doomed = FALSE;
	cm_entry->x_connected = TRUE;
	cm_entry->x_time = lbolt;
	cm_entry->x_wq = wq;
	cm_entry->x_ref = 1;
	if (tiptr->tp_info.servtype == T_COTS_ORD)
		cm_entry->x_ordrel = TRUE;
	else
		cm_entry->x_ordrel = FALSE;
	cm_entry->x_family = p->cku_addrfmly;
	cm_entry->x_rdev = p->cku_device;
	mutex_init(&cm_entry->x_lock, "conn entry lock",
			MUTEX_DEFAULT, DEFAULT_WT);
	cv_init(&cm_entry->x_cv, "conn entry wait", CV_DEFAULT, NULL);

	cm_entry->x_tidu_size = tidu_size;

	mutex_enter(&connmgr_lock);
	cm_entry->x_next = cm_hd;
	cm_hd = cm_entry;
	mutex_exit(&connmgr_lock);

	return (cm_entry);
}

static void
connmgr_close(struct cm_xprt *cm_entry)
{

	mutex_enter(&cm_entry->x_lock);
	while (cm_entry->x_ref != 0) {
		/*
		 * Must be a noninterruptible wait.
		 */
		cv_wait(&cm_entry->x_cv, &cm_entry->x_lock);
	}
	t_kclose(cm_entry->x_tiptr, 1);
	mutex_exit(&cm_entry->x_lock);
	mutex_destroy(&cm_entry->x_lock);
	cv_destroy(&cm_entry->x_cv);

	kmem_free((caddr_t)cm_entry->x_server.buf, cm_entry->x_server.maxlen);
	kmem_free((caddr_t)cm_entry->x_src.buf, cm_entry->x_src.maxlen);
	kmem_free((caddr_t)cm_entry, sizeof (struct cm_xprt));
}

/*
 * Called by KRPC after sending the call message to release the connection
 * it was using.
 */
static void
connmgr_release(struct cm_xprt *cm_entry)
{

	mutex_enter(&cm_entry->x_lock);
	cm_entry->x_ref--;
	if (cm_entry->x_ref == 0)
		cv_signal(&cm_entry->x_cv);
	mutex_exit(&cm_entry->x_lock);
}

/*
 * Given an open stream, connect to the remote.  Returns true if connected,
 * false otherwise.
 */
static bool_t
connmgr_connect(queue_t *wq, struct netbuf *addr, int addrfmly, calllist_t *e,
	int *tidu_ptr, bool_t reconnect, struct timeval wait)
{
	mblk_t *mp;
	struct T_conn_req *tcr;
	struct T_info_ack *tinfo;
	int interrupted;
	int tidu_size;

	mp = allocb(sizeof (struct T_conn_req) + addr->len, BPRI_LO);
	if (mp == NULL) {
		RPCLOG(1,
		"connmgr_get: cannot alloc mp for sending conn request\n", 0);
		COTSRCSTAT_INCR(rcnomem);
		e->call_status = RPC_SYSTEMERROR;
		return (FALSE);
	}

	mp->b_datap->db_type = M_PROTO;
	tcr = (struct T_conn_req *)(mp->b_rptr);
	bzero((char *)tcr, sizeof (struct T_conn_req));
	tcr->PRIM_type = T_CONN_REQ;
	tcr->DEST_length = addr->len;
	tcr->DEST_offset = sizeof (struct T_conn_req);
	mp->b_wptr = mp->b_rptr + sizeof (struct T_conn_req);

	bcopy(addr->buf, (char *)(mp->b_wptr), tcr->DEST_length);
	mp->b_wptr += tcr->DEST_length;

	/*
	 * We use the entry in the handle that is normally used for waiting
	 * for RPC replies to wait for the connection accept.
	 */
	clnt_dispatch_send(wq, mp, e, 0);

	mutex_enter(&clnt_pending_lock);

	/*
	 * We wait for the transport connection to be made, or an
	 * indication that it could not be made.
	 */
	interrupted = 0;
	if (e->call_status == RPC_TIMEDOUT) {
		/*
		 * Do an interruptible timed wait. Timed wait helps
		 * to satisfy CLNT_CALL() that expect to return on
		 * a timeout basis like for example mounts with -o
		 * soft option.  If we get interrupted, we
		 * still have the matter of the connection request sent on
		 * the transport. If we are re-connecting, we want to
		 * wait for the connection acknowlegement, or connection
		 * timeout. In that event, we wait for the connection
		 * to timeout or succeed. If the connection times out,
		 * we will at least return an interrupted indication to
		 * the caller of CLNT_CALL(). If the connection eventually
		 * succeeds, then the interrupt is ignored and success is
		 * returned.
		 * Now loop until cv_timedwait_sig returns because of
		 * a signal(0) or timeout(-1) or cv_signal(>0); But it may be
		 * cv_signalled for various other reasons too. So loop
		 * until the status remains RPC_TIMEDOUT(this is our real
		 * real cv_signal).
		 */

		long timeout, cv_stat;

		timeout = wait.tv_sec * drv_usectohz(1000000) +
			drv_usectohz(wait.tv_usec) + lbolt;

		while ((cv_stat = cv_timedwait_sig(&e->call_cv,
			&clnt_pending_lock, timeout)) > 0 &&
			e->call_status == RPC_TIMEDOUT);

		if (cv_stat == 0) /* got intr signal? */
			interrupted = 1;

		if (e->call_status == RPC_TIMEDOUT && reconnect == TRUE)
			cv_wait(&e->call_cv, &clnt_pending_lock);
	}

	if (e->call_prev)
		e->call_prev->call_next = e->call_next;
	else
		clnt_pending = e->call_next;
	if (e->call_next)
		e->call_next->call_prev = e->call_prev;
	mutex_exit(&clnt_pending_lock);

	if (e->call_status != RPC_SUCCESS) {
		if (interrupted) {
			e->call_status = RPC_INTR;
		}
		RPCLOG(1, "connmgr_connect: can't connect, error:\n",
			e->call_status);

		if (e->call_reply) {
			freemsg(e->call_reply);
			e->call_reply = NULL;
		}

		return (FALSE);
	}

	/*
	 * The result of the "connection accept" is a T_info_ack
	 * in the call_reply field.
	 */
	ASSERT(e->call_reply != (mblk_t *)0);
	tinfo = (struct T_info_ack *)e->call_reply->b_rptr;

	tidu_size = tinfo->TIDU_size;
	tidu_size -= (tidu_size % BYTES_PER_XDR_UNIT);
	if (tidu_size > COTS_DEFAULT_ALLOCSIZE ||
		(tidu_size <= 0))
		tidu_size = COTS_DEFAULT_ALLOCSIZE;
	*tidu_ptr = tidu_size;
	freemsg(e->call_reply);
	e->call_reply = (mblk_t *)0;

	/*
	 * Set up the pertinent options.  NODELAY is so the transport doesn't
	 * buffer up RPC messages on either end.  This may not be valid for
	 * all transports.  Failure to set this option is not cause to
	 * bail out so we return success anyway.  Note that lack of NODELAY
	 * or some other way to flush the message on both ends will cause
	 * lots of retries and terrible performance.
	 */
	if (addrfmly == AF_INET)
		(void) connmgr_setopt(wq, IPPROTO_TCP, TCP_NODELAY, e);

	return (TRUE);
}

/*
 * Reconnects an unconnected connection.  Called by connmgr_get for both the
 * retry case and the new call case.  It must be called holding the connection
 * manager's lock, and returns holding the lock.  Returns false if unable
 * to reconnect, true if reconnect was successful and transport connection
 * can be used.
 */
static bool_t
connmgr_reconnect(struct cm_xprt *cm_entry, cku_private_t *p,
	struct cm_xprt **cmp, struct timeval wait)
{
	bool_t connected;

	/*
	 * Remove the entry from the list while we are trying to reconnect it so
	 * that nobody else mucks with it.
	 */
	*cmp = cm_entry->x_next;
	mutex_exit(&connmgr_lock);
	connected = connmgr_connect(cm_entry->x_wq,
			&p->cku_addr, p->cku_addrfmly, &p->cku_call,
			&cm_entry->x_tidu_size, TRUE, wait);

	/*
	 * Insert entry back in list
	 */
	mutex_enter(&connmgr_lock);
	if (connected)
		cm_entry->x_connected = TRUE;
	cm_entry->x_next = cm_hd;
	cm_hd = cm_entry;
	ASSERT(cm_entry != cm_entry->x_next);
	return (connected);
}

/*
 * Called by connmgr_connect to set an option on the new stream.
 */
static bool_t
connmgr_setopt(queue_t *wq, long level, long name, calllist_t *e)
{
	mblk_t *mp;
	struct opthdr *opt;
	struct T_optmgmt_req *tor;

	mp = allocb(sizeof (struct T_optmgmt_req) + sizeof (struct opthdr) +
		sizeof (int), BPRI_LO);
	if (mp == NULL) {
		RPCLOG(1,
		"connmgr_setopt: cannot alloc mp for option request\n", 0);
		return (FALSE);
	}

	mp->b_datap->db_type = M_PROTO;
	tor = (struct T_optmgmt_req *)(mp->b_rptr);
	tor->PRIM_type = T_OPTMGMT_REQ;
	tor->MGMT_flags = T_NEGOTIATE;
	tor->OPT_length = sizeof (struct opthdr) + sizeof (int);
	tor->OPT_offset = sizeof (struct T_optmgmt_req);

	opt = (struct opthdr *)(mp->b_rptr + sizeof (struct T_optmgmt_req));
	opt->level = level;
	opt->name = name;
	opt->len = sizeof (int);
	*(int *)((char *)opt + sizeof (*opt)) = 1;
	mp->b_wptr += sizeof (struct T_optmgmt_req) + sizeof (struct opthdr) +
		sizeof (int);

	/*
	 * We will use this connection regardless
	 * of whether or not the option is settable.
	 */
	clnt_dispatch_send(wq, mp, e, 0);

	mutex_enter(&clnt_pending_lock);
	if (e->call_status == RPC_TIMEDOUT)
		/*
		 * No interrruptible wait here ... we've got a connection
		 * so no use in returning an error to caller of CLNT_CALL().
		 */
		(void) cv_wait(&e->call_cv, &clnt_pending_lock);
	if (e->call_prev)
		e->call_prev->call_next = e->call_next;
	else
		clnt_pending = e->call_next;
	if (e->call_next)
		e->call_next->call_prev = e->call_prev;
	mutex_exit(&clnt_pending_lock);

	if (e->call_status != RPC_SUCCESS) {
		RPCLOG(1, "connmgr_setopt: can't set option: %d \n", name);
		return (FALSE);
	}
	RPCLOG(16, "connmgr_setopt: successfully set option: %d \n", name);
	return (TRUE);
}

/*
 * Sends an orderly release on the specified queue.
 */
static void
connmgr_sndrel(queue_t *q)
{
	struct T_ordrel_req *torr;
	mblk_t *mp;

	mp = allocb(sizeof (struct T_ordrel_req), BPRI_LO);
	if (mp == NULL) {
		/*
		 * XXX! need a way to recover from this.
		 */
		RPCLOG(1,
		"connmgr_get: cannot alloc mp for sending ordrel\n", 0);
		return;
	}

	mp->b_datap->db_type = M_PROTO;
	torr = (struct T_ordrel_req *)(mp->b_rptr);
	torr->PRIM_type = T_ORDREL_REQ;
	mp->b_wptr = mp->b_rptr + sizeof (struct T_ordrel_req);

	clnt_dispatch_send(q, mp, (calllist_t *)0, 0);
}

/*
 * Sets up the entry for receiving replies, and calls rpcmod's write put proc
 * (through put) to send the call.  If e is null, the caller does
 * not care about getting a reply, and so there is no need to
 * setup a call entry to wait for it.
 */
void
clnt_dispatch_send(queue_t *q, mblk_t *mp, calllist_t *e, u_int xid)
{

	if (e != NULL) {
		e->call_status = RPC_TIMEDOUT;	/* optimistic, eh? */
		ASSERT(e->call_reply == (mblk_t *)0);
		e->call_wq = q;
		e->call_xid = xid;

		mutex_enter(&clnt_pending_lock);
		if (clnt_pending)
			clnt_pending->call_prev = e;
		e->call_next = clnt_pending;
		e->call_prev = (struct calllist_s *)0;
		clnt_pending = e;
		mutex_exit(&clnt_pending_lock);
	}

	put(q, mp);
}

/*
 * Called by rpcmod to notify a client with a clnt_pending call that its reply
 * has arrived.  If we can't find a client waiting for this reply, we log
 * the error and return.
 */
bool_t
clnt_dispatch_notify(mblk_t *mp)
{
	calllist_t *e;
	u_int xid;

	if (str_aligned(mp->b_rptr) &&
	    (mp->b_wptr - mp->b_rptr) >= sizeof (xid))
		xid = *((u_int *)mp->b_rptr);
	else {
		int i = 0;
		unsigned char *p = (unsigned char *)&xid;
		unsigned char *rptr;
		mblk_t *tmp = mp;

		/*
		 * Copy the xid, byte-by-byte into xid.
		 */
		while (tmp) {
			rptr = tmp->b_rptr;
			while (rptr < tmp->b_wptr) {
				*p++ = *rptr++;
				if (++i >= sizeof (xid))
					goto done_xid_copy;
			}
			tmp = tmp->b_cont;
		}

		/*
		 * If we got here, we ran out of mblk space before the
		 * xid could be copied.
		 */
		ASSERT(tmp == NULL && i < sizeof (xid));

		RPCLOG(16,
		"clnt_dispatch_notify: message less than size of xid\n", 0);
		return (FALSE);

	}
done_xid_copy:

	mutex_enter(&clnt_pending_lock);
	for (e = clnt_pending; e; e = e->call_next) {
		if (e->call_xid == xid) {
			RPCLOG(16,
			"clnt_dispatch_notify: found caller for reply\n", 0);

			/*
			 * This can happen under the following scenario:
			 * clnt_cots_kcallit() times out on the response,
			 * rfscall() repeats the CLNT_CALL() with
			 * the same xid, clnt_cots_kcallit() sends the retry,
			 * thereby putting the clnt handle on the pending list,
			 * the first response arrives, signalling the thread
			 * in clnt_cots_kcallit(). Before that thread is
			 * dispatched, the second response arrives as well,
			 * and clnt_dispatch_notify still finds the handle on
			 * the pending list, with call_reply set. So free the
			 * old reply now.
			 */
			if (e->call_reply)
				freemsg(e->call_reply);
			e->call_reply = mp;
			e->call_status = RPC_SUCCESS;
			cv_signal(&e->call_cv);
			mutex_exit(&clnt_pending_lock);
			return (TRUE);
		}
	}
	mutex_exit(&clnt_pending_lock);
	COTSRCSTAT_INCR(rcbadxids);
	RPCLOG(1, "clnt_dispatch_notify: no caller for reply\n", 0);
	return (FALSE);
}

/*
 * Called by rpcmod when a non-data indication arrives.  The ones in which we
 * are interested are connection indications and options acks.  We dispatch
 * based on the queue the indication came in on.  If we are not interested in
 * what came in, we return false to rpcmod, who will then pass it upstream.
 */
bool_t
clnt_dispatch_notifyconn(queue_t *q, mblk_t *mp)
{
	calllist_t *e;
	long type;

	type = ((union T_primitives *)mp->b_rptr)->type;
	RPCLOG(16, "clnt_dispatch_notifyconn: type: %d\n", type);
	mutex_enter(&clnt_pending_lock);
	for (e = clnt_pending; /* NO CONDITION */; e = e->call_next) {
		if (e == NULL) {
			mutex_exit(&clnt_pending_lock);
			RPCLOG(1, "clnt_dispatch_notifyconn: ", 0);
			RPCLOG(1, "no one waiting for connection\n", 0);
			return (FALSE);
		}
		if (e->call_wq == q)
			break;
	}

	switch (type) {
	case T_CONN_CON:
		/*
		 * The transport is now connected, send a T_INFO_REQ to get
		 * the tidu size.
		 */
		mutex_exit(&clnt_pending_lock);
		ASSERT(mp->b_datap->db_lim - mp->b_datap->db_base >=
			sizeof (struct T_info_req));
		mp->b_rptr = mp->b_datap->db_base;
		((union T_primitives *)mp->b_rptr)->type = T_INFO_REQ;
		mp->b_wptr = mp->b_rptr + sizeof (struct T_info_req);
		mp->b_datap->db_type = M_PCPROTO;
		put(q, mp);
		return (TRUE);
	case T_INFO_ACK:
		e->call_status = RPC_SUCCESS;
		e->call_reply = mp;
		cv_signal(&e->call_cv);
		mutex_exit(&clnt_pending_lock);
		return (TRUE);
	case T_OPTMGMT_ACK:
		e->call_status = RPC_SUCCESS;
		cv_signal(&e->call_cv);
		break;
	case T_ERROR_ACK:
		e->call_status = RPC_CANTCONNECT;
		cv_signal(&e->call_cv);
		break;
	case T_OK_ACK:
		/*
		 * Great, but we are really waiting for a T_CONN_CON
		 */
		break;
	default:
		mutex_exit(&clnt_pending_lock);
		RPCLOG(1, "clnt_dispatch_notifyconn: bad type %d\n", type);
		return (FALSE);
	}

	mutex_exit(&clnt_pending_lock);
	freemsg(mp);
	return (TRUE);
}

/*
 * Called by rpcmod when the transport is (or should be) going away.  Informs
 * all callers waiting for replies and marks the entry in the connection
 * manager's list as unconnected, and either closing (close handshake in
 * progress) or dead.
 */
void
clnt_dispatch_notifyall(queue_t *q, long msg_type)
{
	calllist_t *e;
	struct cm_xprt *cm_entry;
	int have_connmgr_lock;

	/*
	 * Find the transport entry in the connection manager's list, close
	 * the transport and delete the entry.  In the case where rpcmod's
	 * idle timer goes off, it sends us a T_ORDREL_REQ, indicating we
	 * should gracefully close the connection.
	 */
	have_connmgr_lock = 1;
	mutex_enter(&connmgr_lock);
	for (cm_entry = cm_hd; cm_entry; cm_entry = cm_entry->x_next) {
		ASSERT(cm_entry != cm_entry->x_next);
		if (cm_entry->x_wq == q) {
			ASSERT(MUTEX_HELD(&connmgr_lock));
			ASSERT(have_connmgr_lock == 1);
			switch (msg_type) {
			case T_ORDREL_REQ:
				if (cm_entry->x_dead) {
#ifdef	DEBUG
cmn_err(CE_NOTE, "idle timeout on dead connection: 0x%x\n", (int) cm_entry);
#endif
					break;
				}
				/*
				 * Only mark the connection as dead if it is
				 * connected and idle.
				 * An unconnected connection has probably
				 * gone idle because the server is down,
				 * and when it comes back up there will be
				 * retries that need to use that connection.
				 */
				if (cm_entry->x_connected ||
					cm_entry->x_doomed) {
				    if (cm_entry->x_ordrel) {
					if (cm_entry->x_closing == TRUE) {
					/*
					 * The connection is obviously
					 * wedged due to a bug or problem
					 * with the transport. Mark it
					 * as dead. Otherwise we can leak
					 * connections.
					 */
					    cm_entry->x_dead = TRUE;
					    mutex_exit(&connmgr_lock);
					    have_connmgr_lock = 0;
					    if (clnt_stop_idle != NULL)
						(*clnt_stop_idle)(q);
					    break;
					}
					cm_entry->x_closing = TRUE;
					mutex_exit(&connmgr_lock);
					have_connmgr_lock = 0;
					connmgr_sndrel(q);
				    } else {
					cm_entry->x_dead = TRUE;
					mutex_exit(&connmgr_lock);
					have_connmgr_lock = 0;
					if (clnt_stop_idle != NULL)
						(*clnt_stop_idle)(q);
				    }
				} else {
					/*
					 * We don't mark the connection
					 * as dead, but we turn off the
					 * idle timer.
					 */
					mutex_exit(&connmgr_lock);
					have_connmgr_lock = 0;
					if (clnt_stop_idle != NULL)
						(*clnt_stop_idle)(q);
					RPCLOG(16, "clnt_dispatch_notifyall: ",
						0);
					RPCLOG(16, " ignoring timeout from", 0);
					RPCLOG(16, " rpcmod because we are", 0);
					RPCLOG(16, " not connected\n", 0);
				}
				break;
			case T_ORDREL_IND:
				/*
				 * If this entry is marked closing, then we are
				 * completing a close handshake, and the
				 * connection is dead.  Otherwise, the server is
				 * trying to close, so we just mark the entry
				 * unconnected, as there may be retries on it.
				 */
				if (cm_entry->x_closing) {
					cm_entry->x_dead = TRUE;
					mutex_exit(&connmgr_lock);
					have_connmgr_lock = 0;
					if (clnt_stop_idle != NULL)
						(*clnt_stop_idle)(q);
				} else {
					cm_entry->x_connected = FALSE;
					mutex_exit(&connmgr_lock);
					have_connmgr_lock = 0;
					connmgr_sndrel(q);
				}
				break;
			case T_DISCON_IND:
			default:
				RPCLOG(1, "clnt_dispatch_notifyall:", 0);
				RPCLOG(1, " received a discon ind on q %x\n",
					(int) q);
				cm_entry->x_connected = FALSE;
				break;
			}
			break;
		}
	}
	if (have_connmgr_lock)
		mutex_exit(&connmgr_lock);

	/*
	 * Then kick all the clnt_pending calls out of their wait.  There
	 * should be no clnt_pending calls in the case of rpcmod's idle
	 * timer firing.
	 */
	mutex_enter(&clnt_pending_lock);
	for (e = clnt_pending; e; e = e->call_next) {
		if (e->call_wq == q) {
			RPCLOG(1, "clnt_dispatch_notifyall: ", 0);
			RPCLOG(1, " aborting clnt_pending call\n", 0);

			/*
			 * Let the caller timeout, else he will retry
			 * immediately.
			 */
			e->call_status = RPC_XPRTFAILED;

			if (e->call_xid == 0) {
				/*
				 * This entry is not an RPC call - it is a
				 * connmgr_connect waiting for a connection to
				 * be made.  Signal, as connmgr_connect is not
				 * doing a timed wait.
				 */
				cv_signal(&e->call_cv);
			}
		}
	}
	mutex_exit(&clnt_pending_lock);
}
