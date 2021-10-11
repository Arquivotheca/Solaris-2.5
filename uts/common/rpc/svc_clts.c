/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma ident   "@(#)svc_clts.c 1.37     95/01/26 SMI"

/*
 *  		PROPRIETARY NOTICE (Combined)
 *
 *  This source code is unpublished proprietary information
 *  constituting, or derived under license from AT&T's Unix(r) System V.
 *  In addition, portions of such source code were derived from Berkeley
 *  4.3 BSD under license from the Regents of the University of
 *  California.
 *
 *
 *
 *  		Copyright Notice
 *
 *  Notice of copyright on this source code product does not indicate
 *  publication.
 *
 *  	(c) 1986, 1987, 1988, 1989  Sun Microsystems, Inc.
 *  	(c) 1983, 1984, 1985, 1986, 1987, 1988, 1989  AT&T.
 *		All rights reserved.
 */

/*
 * svc_clts.c
 * Server side for RPC in the kernel.
 *
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/file.h>
#include <sys/stream.h>
#include <sys/strsubr.h>
#include <sys/tihdr.h>
#include <sys/tiuser.h>
#include <sys/t_kuser.h>
#include <sys/fcntl.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/systm.h>
#include <sys/cmn_err.h>
#include <sys/kstat.h>
#include <sys/vtrace.h>
#include <sys/debug.h>

#include <rpc/types.h>
#include <rpc/xdr.h>
#include <rpc/auth.h>
#include <rpc/clnt.h>
#include <rpc/rpc_msg.h>
#include <rpc/svc.h>

/*
 * Routines exported through ops vector.
 */
static bool_t		svc_clts_krecv(SVCXPRT *, mblk_t *, struct rpc_msg *);
static bool_t		svc_clts_ksend(SVCXPRT *, struct rpc_msg *);
static enum xprt_stat	svc_clts_kstat(SVCXPRT *);
static bool_t		svc_clts_kgetargs(SVCXPRT *, xdrproc_t, caddr_t);
static bool_t		svc_clts_kfreeargs(SVCXPRT *, xdrproc_t, caddr_t);
static void		svc_clts_kdestroy(SVCXPRT *);
static int		svc_clts_kdup(struct svc_req *, caddr_t, int,
				struct dupreq **);
static void		svc_clts_kdupdone(struct dupreq *, caddr_t, int, int);
static long *		svc_clts_kgetres(SVCXPRT *, int);
static void		svc_clts_kclone(SVCXPRT *, SVCXPRT *, caddr_t);
static void		svc_clts_kclone_destroy(SVCXPRT *);
static void		svc_clts_kfreeres(SVCXPRT *);


/*
 * Server transport operations vector.
 */
struct xp_ops svc_clts_op = {
	svc_clts_krecv,		/* Get requests */
	svc_clts_kstat,		/* Return status */
	svc_clts_kgetargs,	/* Deserialize arguments */
	svc_clts_ksend,		/* Send reply */
	svc_clts_kfreeargs,	/* Free argument data space */
	svc_clts_kdestroy,	/* Destroy transport handle */
	svc_clts_kdup,		/* Check entry in dup req cache */
	svc_clts_kdupdone,	/* Mark entry in dup req cache as done */
	svc_clts_kgetres,	/* Get pointer to response buffer */
	svc_clts_kfreeres,	/* Destroy pre-serialized response header */
	svc_clts_kclone,	/* Clone a master xprt */
	svc_clts_kclone_destroy	/* Destroy a clone xprt */
};

/*
 * Transport private data.
 * Kept in xprt->xp_p2.
 */
struct udp_data {
	mblk_t	*ud_resp;			/* buffer for response */
	mblk_t	*ud_inmp;			/* mblk chain of request */
};

#define	UD_MAXSIZE	8800
#define	UD_INITSIZE	2048

/*
 * Connectionless server statistics
 */
struct {
	kstat_named_t	rscalls;
	kstat_named_t	rsbadcalls;
	kstat_named_t	rsnullrecv;
	kstat_named_t	rsbadlen;
	kstat_named_t	rsxdrcall;
	kstat_named_t	rsdupchecks;
	kstat_named_t	rsdupreqs;
} rsstat = {
	{ "calls",	KSTAT_DATA_ULONG },
	{ "badcalls",	KSTAT_DATA_ULONG },
	{ "nullrecv",	KSTAT_DATA_ULONG },
	{ "badlen",	KSTAT_DATA_ULONG },
	{ "xdrcall",	KSTAT_DATA_ULONG },
	{ "dupchecks",	KSTAT_DATA_ULONG },
	{ "dupreqs",	KSTAT_DATA_ULONG }
};

kstat_named_t *rsstat_ptr = (kstat_named_t *) &rsstat;
ulong_t rsstat_ndata = sizeof (rsstat) / sizeof (kstat_named_t);

#ifdef accurate_stats
#define	RSSTAT_INCR(x)		\
	mutex_enter(&rsstat_lock);	\
	rsstat.x.value.ul++;			\
	mutex_exit(&rsstat_lock);

extern kmutex_t	rsstat_lock;
#else
#define	RSSTAT_INCR(x)	rsstat.x.value.ul++
#endif

/*
 * Create a transport record.
 * The transport record, output buffer, and private data structure
 * are allocated.  The output buffer is serialized into using xdrmem.
 * There is one transport record per user process which implements a
 * set of services.
 */
/* ARGSUSED */
int
svc_clts_kcreate(file_t *fp, u_int sendsz, struct T_info_ack *tinfo,
	SVCXPRT **nxprt)
{
	register struct udp_data *ud;
	SVCXPRT *xprt;

	if (nxprt == NULL)
		return (EINVAL);
	/*
	 * Check to make sure that the clts private data will fit into
	 * the stack buffer allocated by svc_run.  The compiler should
	 * remove this check, but it's a safety net if the udp_data
	 * structure ever changes.
	 */
	/*CONSTANTCONDITION*/
	ASSERT(sizeof (struct udp_data) <= SVC_MAX_P2LEN);

	xprt = (SVCXPRT *)kmem_alloc((u_int)sizeof (SVCXPRT), KM_SLEEP);

	ud = (struct udp_data *)kmem_zalloc((u_int)sizeof (struct udp_data),
					    KM_SLEEP);

	xprt->xp_p1 = NULL;
	xprt->xp_p2 = (caddr_t)ud;
	xprt->xp_p3 = NULL;
	xprt->xp_ops = &svc_clts_op;
	xprt->xp_msg_size = tinfo->TSDU_size;

	xprt->xp_rtaddr.buf = NULL;
	xprt->xp_rtaddr.maxlen = tinfo->ADDR_size;
	xprt->xp_rtaddr.len = 0;

	*nxprt = xprt;

	return (0);
}

/*
 * Destroy a transport record.
 * Frees the space allocated for a transport record.
 */
static void
svc_clts_kdestroy(SVCXPRT *xprt)
{
	/* LINTED pointer alignment */
	register struct udp_data *ud = (struct udp_data *)xprt->xp_p2;

	xprt_unregister(xprt);	/* unregister the transport handle */

	if (ud->ud_resp) {
		/*
		 * There should not be any left over results buffer.
		 */
		ASSERT(ud->ud_resp->b_cont == NULL);

		/*
		 * Free the T_UNITDATA_{REQ/IND} that svc_clts_krecv
		 * saved.
		 */
		freeb(ud->ud_resp);
	}
	if (ud->ud_inmp)
		freemsg(ud->ud_inmp);

	mutex_destroy(&xprt->xp_req_lock);
	mutex_destroy(&xprt->xp_thread_lock);

	kmem_free((caddr_t)ud, (u_int)sizeof (struct udp_data));
	kmem_free((caddr_t)xprt, (u_int)sizeof (SVCXPRT));
}

/*
 * Clone a quick copy of a master xprt for use during request processing.
 */
/* ARGSUSED */
static void
svc_clts_kclone(SVCXPRT *old_xprt, SVCXPRT *new_xprt, caddr_t p2buf)
{

	new_xprt->xp_p1 = NULL;
	new_xprt->xp_p2 = p2buf;
}

/*
 * Frees the message buffer space allocated for a clone of a transport record
 */
static void
svc_clts_kclone_destroy(SVCXPRT *xprt)
{
	/* LINTED pointer alignment */
	register struct udp_data *ud = (struct udp_data *)xprt->xp_p2;

	if (ud->ud_resp) {
		/*
		 * There should not be any left over results buffer.
		 */
		ASSERT(ud->ud_resp->b_cont == NULL);

		/*
		 * Free the T_UNITDATA_{REQ/IND} that svc_clts_krecv
		 * saved.
		 */
		freeb(ud->ud_resp);
	}
	if (ud->ud_inmp)
		freemsg(ud->ud_inmp);
}

/*
 * Receive rpc requests.
 * Pulls a request in off the socket, checks if the packet is intact,
 * and deserializes the call packet.
 */
static bool_t
svc_clts_krecv(SVCXPRT *xprt, mblk_t *mp, struct rpc_msg *msg)
{
	/* LINTED pointer alignment */
	register struct udp_data *ud = (struct udp_data *)xprt->xp_p2;
	register XDR *xdrs = &xprt->xp_xdrin;
	union T_primitives *pptr;
	int hdrsz;

	TRACE_0(TR_FAC_KRPC, TR_SVC_CLTS_KRECV_START,
		"svc_clts_krecv_start:");

	RSSTAT_INCR(rscalls);

	/*
	 * The incoming request should start with an M_PROTO message.
	 */
	if (mp->b_datap->db_type != M_PROTO) {
		goto bad;
	}

	/*
	 * The incoming request should be an T_UNITDTA_IND.  There
	 * might be other messages coming up the stream, but we can
	 * ignore them.
	 */
	pptr = (union T_primitives *)mp->b_rptr;
	if (pptr->type != T_UNITDATA_IND) {
		goto bad;
	}
	/*
	 * Do some checking to make sure that the header at least looks okay.
	 */
	hdrsz = mp->b_wptr - mp->b_rptr;
	if (hdrsz < TUNITDATAINDSZ ||
	    hdrsz < (pptr->unitdata_ind.OPT_offset +
		    pptr->unitdata_ind.OPT_length) ||
	    hdrsz < (pptr->unitdata_ind.SRC_offset +
		    pptr->unitdata_ind.SRC_length)) {
		goto bad;
	}

	/*
	 * Make sure that the transport provided a usable address.
	 */
	if (pptr->unitdata_ind.SRC_length <= 0) {
		goto bad;
	}
	/*
	 * Point the remote transport address in the service_transport
	 * handle at the address in the request.
	 */
	xprt->xp_rtaddr.buf = (char *)mp->b_rptr +
					pptr->unitdata_ind.SRC_offset;
	xprt->xp_rtaddr.len = pptr->unitdata_ind.SRC_length;

	/*
	 * Save the first mblk which contains the T_unidata_ind in
	 * ud_resp.  It will be used to generate the T_unitdata_req
	 * during the reply.
	 */
	if (ud->ud_resp) {
		if (ud->ud_resp->b_cont != NULL) {
			cmn_err(CE_WARN,
				"svc_clts_krecv: ud_resp %x, b_cont %x",
				(int)ud->ud_resp, (int)ud->ud_resp->b_cont);
		}
		freeb(ud->ud_resp);
	}
	ud->ud_resp = mp;
	mp = mp->b_cont;
	ud->ud_resp->b_cont = NULL;

	xdrmblk_init(xdrs, mp, XDR_DECODE, 0);

	TRACE_0(TR_FAC_KRPC, TR_XDR_CALLMSG_START,
		"xdr_callmsg_start:");
	if (! xdr_callmsg(xdrs, msg)) {
		TRACE_1(TR_FAC_KRPC, TR_XDR_CALLMSG_END,
			"xdr_callmsg_end:(%S)", "bad");
		RSSTAT_INCR(rsxdrcall);
		goto bad;
	}
	TRACE_1(TR_FAC_KRPC, TR_XDR_CALLMSG_END,
		"xdr_callmsg_end:(%S)", "good");

	xprt->xp_xid = msg->rm_xid;
	ud->ud_inmp = mp;

	TRACE_1(TR_FAC_KRPC, TR_SVC_CLTS_KRECV_END,
		"svc_clts_krecv_end:(%S)", "good");
	return (TRUE);

bad:
	if (mp)
		freemsg(mp);
	if (ud->ud_resp) {
		/*
		 * There should not be any left over results buffer.
		 */
		ASSERT(ud->ud_resp->b_cont == NULL);
		freeb(ud->ud_resp);
		ud->ud_resp = NULL;
	}

	RSSTAT_INCR(rsbadcalls);
	TRACE_1(TR_FAC_KRPC, TR_SVC_CLTS_KRECV_END,
		"svc_clts_krecv_end:(%S)", "bad");
	return (FALSE);
}

/*
 * Send rpc reply.
 * Serialize the reply packet into the output buffer then
 * call t_ksndudata to send it.
 */
static bool_t
svc_clts_ksend(SVCXPRT *xprt, struct rpc_msg *msg)
{
	/* LINTED pointer alignment */
	register struct udp_data *ud = (struct udp_data *)xprt->xp_p2;
	register XDR *xdrs = &xprt->xp_xdrout;
	register int stat = FALSE;
	int error;
	mblk_t *mp;
	int msgsz;
	struct T_unitdata_req *udreq;
	extern int (*rpc_send)(queue_t *, mblk_t *);


	TRACE_0(TR_FAC_KRPC, TR_SVC_CLTS_KSEND_START,
		"svc_clts_ksend_start:");

	ASSERT(ud->ud_resp != NULL);
	if (ud->ud_resp->b_cont == NULL) {
		/*
		 * Allocate an initial mblk for the response data.
		 */
		while ((mp = allocb(UD_INITSIZE, BPRI_LO)) == NULL) {
			if (error = strwaitbuf(UD_INITSIZE, BPRI_LO)) {
				TRACE_1(TR_FAC_KRPC, TR_SVC_CLTS_KSEND_END,
					"svc_clts_ksend_end:(%S)",
					"strwaitbuf");
				return (FALSE);
			}
		}

		/*
		 * Initialize the XDR decode stream.  Additional mblks
		 * will be allocated if necessary.  They will be UD_MAXSIZE
		 * sized.
		 */
		xdrmblk_init(xdrs, mp, XDR_ENCODE, UD_MAXSIZE);

		/*
		 * Leave some space for protocol headers.
		 */
		(void) XDR_SETPOS(xdrs, 512);
		mp->b_rptr += 512;

		msg->rm_xid = xprt->xp_xid;

		TRACE_0(TR_FAC_KRPC, TR_XDR_REPLYMSG_START,
			"xdr_replymsg_start:");
		if (!xdr_replymsg(xdrs, msg)) {
			TRACE_1(TR_FAC_KRPC, TR_XDR_REPLYMSG_END,
				"xdr_replymsg_end:(%S)", "bad");
			goto out;
		}
		TRACE_1(TR_FAC_KRPC, TR_XDR_REPLYMSG_END,
			"xdr_replymsg_end:(%S)", "good");

		ud->ud_resp->b_cont = mp;
	} else {
		if (!xdr_replymsg_body(xdrs, msg))
			goto out;
	}

	msgsz = xmsgsize(ud->ud_resp->b_cont);

	if (msgsz <= 0 || (xprt->xp_msg_size != -1 &&
	    msgsz > xprt->xp_msg_size)) {
#ifdef	DEBUG
		cmn_err(CE_NOTE,
"KRPC: server response message of %d bytes; transport limits are [0, %d]",
			msgsz, xprt->xp_msg_size);
#endif
		goto out;
	}

	/*
	 * Construct the T_unitdata_req.  We take advantage
	 * of the fact that T_unitdata_ind looks just like
	 * T_unitdata_req, except for the primitive type.
	 */
	udreq = (struct T_unitdata_req *)ud->ud_resp->b_rptr;
	udreq->PRIM_type = T_UNITDATA_REQ;

	error = (*rpc_send)(xprt->xp_wq, ud->ud_resp);

	if (!error) {
		stat = TRUE;
		ud->ud_resp = NULL;
	}

out:
	if (stat == FALSE) {
		freemsg(ud->ud_resp);
		ud->ud_resp = NULL;
	}

	/*
	 * This is completely disgusting.  If public is set it is
	 * a pointer to a structure whose first field is the address
	 * of the function to free that structure and any related
	 * stuff.  (see rrokfree in nfs_xdr.c).
	 */
	if (xdrs->x_public) {
		/* LINTED pointer alignment */
		(**((int (**)())xdrs->x_public))(xdrs->x_public);
	}

	TRACE_1(TR_FAC_KRPC, TR_SVC_CLTS_KSEND_END,
		"svc_clts_ksend_end:(%S)", "done");
	return (stat);
}

/*
 * Return transport status.
 */
/* ARGSUSED */
static enum xprt_stat
svc_clts_kstat(SVCXPRT *xprt)
{

	return (XPRT_IDLE);
}

/*
 * Deserialize arguments.
 */
static bool_t
svc_clts_kgetargs(SVCXPRT *xprt, xdrproc_t xdr_args, caddr_t args_ptr)
{

	/* LINTED pointer alignment */
	return ((*xdr_args)(&xprt->xp_xdrin, args_ptr));
}

static bool_t
svc_clts_kfreeargs(SVCXPRT *xprt, xdrproc_t xdr_args, caddr_t args_ptr)
{
	/* LINTED pointer alignment */
	register struct udp_data *ud = (struct udp_data *)xprt->xp_p2;
	register XDR *xdrs = &xprt->xp_xdrin;
	register bool_t retval;

	if (args_ptr) {
		xdrs->x_op = XDR_FREE;
		retval = (*xdr_args)(xdrs, args_ptr);
	} else
		retval = TRUE;

	if (ud->ud_inmp) {
		freemsg(ud->ud_inmp);
		ud->ud_inmp = NULL;
	}

	return (retval);
}

static long *
svc_clts_kgetres(SVCXPRT *xprt, int size)
{
	/* LINTED pointer alignment */
	register struct udp_data *ud = (struct udp_data *)xprt->xp_p2;
	register XDR *xdrs = &xprt->xp_xdrout;
	mblk_t *mp;
	long *buf;
	struct rpc_msg rply;

	/*
	 * Allocate an initial mblk for the response data.
	 */
	while ((mp = allocb(UD_INITSIZE, BPRI_LO)) == NULL) {
		if (strwaitbuf(UD_INITSIZE, BPRI_LO)) {
			return (FALSE);
		}
	}

	mp->b_cont = NULL;

	/*
	 * Initialize the XDR decode stream.  Additional mblks
	 * will be allocated if necessary.  They will be UD_MAXSIZE
	 * sized.
	 */
	xdrmblk_init(xdrs, mp, XDR_ENCODE, UD_MAXSIZE);

	/*
	 * Leave some space for protocol headers.
	 */
	(void) XDR_SETPOS(xdrs, 512);
	mp->b_rptr += 512;

	/*
	 * Assume a successful RPC since most of them are.
	 */
	rply.rm_xid = xprt->xp_xid;
	rply.rm_direction = REPLY;
	rply.rm_reply.rp_stat = MSG_ACCEPTED;
	rply.acpted_rply.ar_verf = xprt->xp_verf;
	rply.acpted_rply.ar_stat = SUCCESS;

	if (!xdr_replymsg_hdr(xdrs, &rply)) {
		freeb(mp);
		return (NULL);
	}

	buf = XDR_INLINE(xdrs, size);

	if (buf == NULL)
		freeb(mp);
	else
		ud->ud_resp->b_cont = mp;

	return (buf);
}

static void
svc_clts_kfreeres(SVCXPRT *xprt)
{
	/* LINTED pointer alignment */
	register struct udp_data *ud = (struct udp_data *)xprt->xp_p2;

	if (ud->ud_resp == NULL || ud->ud_resp->b_cont == NULL)
		return;

	/*
	 * SVC_FREERES() is called whenever the server decides not to
	 * send normal reply. Thus, we expect only one mblk to be allocated,
	 * because we have not attempted any XDR encoding.
	 * If we do any XDR encoding and we get an error, then SVC_REPLY()
	 * will freemsg(ud->ud_resp);
	 */
	ASSERT(ud->ud_resp->b_cont->b_cont == NULL);
	freeb(ud->ud_resp->b_cont);
	ud->ud_resp->b_cont = NULL;
}

/*
 * the dup cacheing routines below provide a cache of non-failure
 * transaction id's.  rpc service routines can use this to detect
 * retransmissions and re-send a non-failure response.
 */

/*
 * MAXDUPREQS is the number of cached items.  It should be adjusted
 * to the service load so that there is likely to be a response entry
 * when the first retransmission comes in.
 */
#define	MAXDUPREQS	1024

/*
 * This should be appropriately scaled to MAXDUPREQS.
 */
#define	DRHASHSZ	257

#if ((DRHASHSZ & (DRHASHSZ - 1)) == 0)
#define	XIDHASH(xid)	((xid) & (DRHASHSZ - 1))
#else
#define	XIDHASH(xid)	((xid) % DRHASHSZ)
#endif
#define	DRHASH(dr)	XIDHASH((dr)->dr_xid)
#define	REQTOXID(req)	((req)->rq_xprt->xp_xid)

int	ndupreqs = 0;
int	maxdupreqs = MAXDUPREQS;
extern kmutex_t dupreq_lock;
struct dupreq *drhashtbl[DRHASHSZ];
int	drhashstat[DRHASHSZ];

static void unhash(struct dupreq *);

/*
 * drmru points to the head of a circular linked list in lru order.
 * drmru->dr_next == drlru
 */
struct dupreq *drmru;

/*
 * svc_clts_kdup searches the request cache and returns 0 if the
 * request is not found in the cache.  If it is found, then it
 * returns the state of the request (in progress or done) and
 * the status or attributes that were part of the original reply.
 */
static int
svc_clts_kdup(struct svc_req *req, caddr_t res, int size, struct dupreq **drpp)
{
	register struct dupreq *dr;
	u_long xid;
	u_long drhash;
	int status;

	xid = REQTOXID(req);
	mutex_enter(&dupreq_lock);
	RSSTAT_INCR(rsdupchecks);
	/*
	 * Check to see whether an entry already exists in the cache.
	 */
	dr = drhashtbl[XIDHASH(xid)];
	while (dr != NULL) {
		if (dr->dr_xid == xid &&
		    dr->dr_proc == req->rq_proc &&
		    dr->dr_prog == req->rq_prog &&
		    dr->dr_vers == req->rq_vers &&
		    dr->dr_addr.len == req->rq_xprt->xp_rtaddr.len &&
		    bcmp((caddr_t)dr->dr_addr.buf,
		    (caddr_t)req->rq_xprt->xp_rtaddr.buf,
		    dr->dr_addr.len) == 0) {
			status = dr->dr_status;
			if (status == DUP_DONE) {
				bcopy((caddr_t)dr->dr_resp.buf, res,
				    (u_int)size);
			} else {
				dr->dr_status = DUP_INPROGRESS;
				*drpp = dr;
			}
			RSSTAT_INCR(rsdupreqs);
			mutex_exit(&dupreq_lock);
			return (status);
		}
		dr = dr->dr_chain;
	}

	/*
	 * There wasn't an entry, either allocate a new one or recycle
	 * an old one.
	 */
	if (ndupreqs < maxdupreqs) {
		dr = (struct dupreq *)kmem_alloc(sizeof (*dr), KM_NOSLEEP);
		if (dr == NULL) {
			mutex_exit(&dupreq_lock);
			return (DUP_ERROR);
		}
		dr->dr_resp.buf = NULL;
		dr->dr_resp.maxlen = 0;
		dr->dr_addr.buf = NULL;
		dr->dr_addr.maxlen = 0;
		if (drmru) {
			dr->dr_next = drmru->dr_next;
			drmru->dr_next = dr;
		} else {
			dr->dr_next = dr;
		}
		ndupreqs++;
	} else {
		dr = drmru->dr_next;
		while (dr->dr_status == DUP_INPROGRESS)
			dr = dr->dr_next;
		unhash(dr);
	}
	drmru = dr;

	dr->dr_xid = REQTOXID(req);
	dr->dr_prog = req->rq_prog;
	dr->dr_vers = req->rq_vers;
	dr->dr_proc = req->rq_proc;
	if (dr->dr_addr.maxlen < req->rq_xprt->xp_rtaddr.len) {
		if (dr->dr_addr.buf != NULL)
			kmem_free((caddr_t)dr->dr_addr.buf, dr->dr_addr.maxlen);
		dr->dr_addr.maxlen = req->rq_xprt->xp_rtaddr.len;
		dr->dr_addr.buf = (char *)kmem_alloc(dr->dr_addr.maxlen,
							KM_NOSLEEP);
		if (dr->dr_addr.buf == NULL) {
			dr->dr_addr.maxlen = 0;
			dr->dr_status = DUP_DROP;
			mutex_exit(&dupreq_lock);
			return (DUP_ERROR);
		}
	}
	dr->dr_addr.len = req->rq_xprt->xp_rtaddr.len;
	bcopy((caddr_t)req->rq_xprt->xp_rtaddr.buf,
	    (caddr_t)dr->dr_addr.buf, dr->dr_addr.len);
	if (dr->dr_resp.maxlen < size) {
		if (dr->dr_resp.buf != NULL)
			kmem_free((caddr_t)dr->dr_resp.buf, dr->dr_resp.maxlen);
		dr->dr_resp.maxlen = (unsigned int)size;
		dr->dr_resp.buf = (char *)kmem_alloc((u_int)size, KM_NOSLEEP);
		if (dr->dr_resp.buf == NULL) {
			dr->dr_resp.maxlen = 0;
			dr->dr_status = DUP_DROP;
			mutex_exit(&dupreq_lock);
			return (DUP_ERROR);
		}
	}
	dr->dr_status = DUP_INPROGRESS;

	drhash = DRHASH(dr);
	dr->dr_chain = drhashtbl[drhash];
	drhashtbl[drhash] = dr;
	drhashstat[drhash]++;
	mutex_exit(&dupreq_lock);
	*drpp = dr;
	return (DUP_NEW);
}

/*
 * svc_clts_kdupdone marks the request done (DUP_DONE or DUP_DROP)
 * and stores the response.
 */
static void
svc_clts_kdupdone(struct dupreq *dr, caddr_t res, int size, int status)
{

	if (status == DUP_DONE)
		bcopy(res, dr->dr_resp.buf, (u_int)size);
	dr->dr_status = status;
}

/*
 * This routine expects that the mutex, dupreq_lock, is already held.
 */
static void
unhash(struct dupreq *dr)
{
	struct dupreq *drt;
	struct dupreq *drtprev = NULL;
	u_long drhash;

	ASSERT(MUTEX_HELD(&dupreq_lock));

	drhash = DRHASH(dr);
	drt = drhashtbl[drhash];
	while (drt != NULL) {
		if (drt == dr) {
			drhashstat[drhash]--;
			if (drtprev == NULL) {
				drhashtbl[drhash] = drt->dr_chain;
			} else {
				drtprev->dr_chain = drt->dr_chain;
			}
			return;
		}
		drtprev = drt;
		drt = drt->dr_chain;
	}
}
