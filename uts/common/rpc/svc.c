/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)svc.c	1.41	95/03/10 SMI"	/* SVr4.0 1.9	*/

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
 *  	Copyright (c) 1986,1987,1988,1989,1994, by Sun Microsystems, Inc.
 *  	Copyright (c) 1983,1984,1985,1986,1987,1988,1989 AT&T.
 *	All rights reserved.
 */

/*
 * svc.c, Server-side remote procedure call interface.
 *
 * There are two sets of procedures here.  The xprt routines are
 * for handling transport handles.  The svc routines handle the
 * list of service routines.
 *
 */

#include <sys/param.h>
#include <sys/types.h>
#include <rpc/types.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <rpc/xdr.h>
#include <rpc/auth.h>
#include <rpc/clnt.h>
#include <rpc/rpc_msg.h>
#include <sys/tiuser.h>
#include <sys/t_kuser.h>
#include <rpc/svc.h>
#include <rpc/svc_auth.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/stream.h>
#include <sys/strsubr.h>
#include <sys/tihdr.h>
#include <sys/debug.h>
#include <sys/cmn_err.h>
#include <sys/file.h>
#include <sys/systm.h>
#include <sys/callb.h>

#include <sys/vtrace.h>
#ifdef	TRACE
#include <nfs/nfs.h>
#endif


#define	NULL_SVC ((struct svc_callout *)0)
#define	RQCRED_SIZE	400		/* this size is excessive */

#define	SVC_VERSQUIET 0x0001		/* keept quiet about vers mismatch */
#define	version_keepquiet(xp)  ((u_long)(xp)->xp_p3 & SVC_VERSQUIET)

static SVCXPRT *xprt_head = NULL;
extern krwlock_t xprt_lock;

#define	RPC_THREAD_TIMEOUT	(5)	/* Multiplied by HZ when used */
static long rpc_thread_timo = RPC_THREAD_TIMEOUT;

caddr_t rqcred_head;  /* head of cached, free authentication parameters */
extern kmutex_t	rqcred_lock;

/*
 * A service provider can reserve up to (maxthreads - redline) threads per
 * transport.  Reserved threads can be detached and then block indefinitely
 * long.  The redline count should be at least 1 to ensure that there is
 * always a thread to handle new requests on the transport.
 */
static int svc_redline = 1;

#define	TOTAL_THREADS(xprt) \
	((xprt)->xp_threads + (xprt)->xp_detached_threads)

/*
 * The services list
 * Each entry represents a set of procedures (an rpc program).
 * The dispatch routine takes request structs and runs the
 * apropriate procedure.
 */
static struct svc_callout {
	struct svc_callout *sc_next;
	u_long		    sc_prog;
	u_long		    sc_vers;
	void		    (*sc_dispatch)();
} *svc_head;
extern krwlock_t	svc_lock;

int (*rpc_send)(queue_t *, mblk_t *);
void (*rpc_rele)(queue_t *, mblk_t *);
void (*mir_rele)(queue_t *, mblk_t *);
void (*mir_start)(queue_t *);

/*
 * This macro picks which "rele" routine to use, based on the transport
 * type.  XXX Should this just be a function pointer in the transport
 * handle?
 */
#define	RELE_PROC(xprt)	(((xprt)->xp_type == T_CLTS) ? rpc_rele : mir_rele)

static struct svc_callout *svc_find(u_long, u_long, struct svc_callout **);
static void svc_run(SVCXPRT *);
static void svc_thread_exit(SVCXPRT *clone_xprt, bool_t holds_thread_lock);

#ifdef TRACE
/*
 * Counter to track queue depth (overall).
 */
long rpc_queued_req;

/*
 * Only for NFS now.
 */
static char *rfscallnames_v2[] = {
	"RFS2_NULL",
	"RFS2_GETATTR",
	"RFS2_SETATTR",
	"RFS2_ROOT",
	"RFS2_LOOKUP",
	"RFS2_READLINK",
	"RFS2_READ",
	"RFS2_WRITECACHE",
	"RFS2_WRITE",
	"RFS2_CREATE",
	"RFS2_REMOVE",
	"RFS2_RENAME",
	"RFS2_LINK",
	"RFS2_SYMLINK",
	"RFS2_MKDIR",
	"RFS2_RMDIR",
	"RFS2_READDIR",
	"RFS2_STATFS"
};

#define	MP2PROCNAME(mp)	\
((((struct rpc_msg *) (mp)->b_cont->b_rptr)->rm_call.cb_prog == NFS_PROGRAM && \
	((struct rpc_msg *) (mp)->b_cont->b_rptr)->rm_call.cb_vers == 2) \
	? rfscallnames_v2[ \
	((struct rpc_msg *) (mp)->b_cont->b_rptr)->rm_call.cb_proc] \
	: "unknown procedure name")
#endif

/* ***************  SVCXPRT related stuff **************** */

/*
 * Activate a transport handle.
 * Called by svc_clts_kcreate() to add server xprt handle to
 * global list.
 */
void
xprt_register(xprt)
	const SVCXPRT *xprt;
{
	rw_enter(&xprt_lock, RW_WRITER);

	/*
	 * Add xprt handle to list, last one added is at the head.
	 */
	((SVCXPRT *)xprt)->xp_next = xprt_head;
	xprt_head = (SVCXPRT *) xprt;

	mutex_enter(&((SVCXPRT *)xprt)->xp_thread_lock);

	if (thread_create(NULL, NULL, svc_run, (caddr_t)xprt,
				0, &p0, TS_RUN, 60) != NULL) {
		((SVCXPRT *)xprt)->xp_threads++;
	} else {
		cmn_err(CE_WARN, "xprt_register: thread_create failed");
	}
	mutex_exit(&((SVCXPRT *)xprt)->xp_thread_lock);
	rw_exit(&xprt_lock);
}

/*
 * De-activate a transport handle.
 * Called from SVC_DESTROY() to remove server's handle from global list.
 */
void
xprt_unregister(xprt)
	const SVCXPRT *xprt;
{
	register SVCXPRT *xp, *xprev;

	xprev = NULL;
	rw_enter(&xprt_lock, RW_WRITER);
	for (xp = xprt_head; xp != NULL; xp = xp->xp_next) {
		if (xp == xprt) {
			if (xprev != NULL) {
				xprev->xp_next = xp->xp_next;
			} else {
				xprt_head = xp->xp_next;
			}
			rw_exit(&xprt_lock);
			return;
		}
		xprev = xp;
	}
	rw_exit(&xprt_lock);
	cmn_err(CE_PANIC, "xprt_unregister: service handle not found");
}

/*
 * xprt_lookup finds the master xprt associated with a queue.  It assumes
 * that the caller has at least a reader hold on xprt_lock.
 */
SVCXPRT *
xprt_lookup(q)
	queue_t * q;
{
	SVCXPRT * xprt;

	for (xprt = xprt_head; xprt != NULL; xprt = xprt->xp_next) {
		if (xprt->xp_wq == q)
			return (xprt);
	}
	return (NULL);
}


/* ********************** CALLOUT list related stuff ************* */

/*
 * Add a service program to the callout list.
 * The dispatch routine will be called when a rpc request for this
 * program number comes in.
 */
/*ARGSUSED*/
bool_t
svc_register(xprt, prog, vers, dispatch, protocol)
	SVCXPRT *xprt;
	u_long prog;
	u_long vers;
	void (*dispatch)();
	int protocol;
{
	struct svc_callout *prev;
	register struct svc_callout *s;

	/*
	 * Check under the reader lock to see if the program is
	 * already registered. We don't want to unnecessarily get
	 * the writer lock only to find that there is no need to
	 * modify dispatch table. Otherwise a writer lock that waits
	 * for readers to get out will cause new readers to get backed up,
	 * resulting in extended RPC latencies.
	 */
	rw_enter(&svc_lock, RW_READER);
	if ((s = svc_find(prog, vers, &prev)) != NULL_SVC) {
		if (s->sc_dispatch == dispatch)
			goto pmap_it;  /* he is registering another xprt */
		rw_exit(&svc_lock);
		return (FALSE);
	}
	rw_exit(&svc_lock);

	/*
	 * Now re-check under the writer lock to see if the program
	 * got registered in the window after the above rw_exit().
	 * If still not registered, then register now.
	 */
	rw_enter(&svc_lock, RW_WRITER);
	if ((s = svc_find(prog, vers, &prev)) != NULL_SVC) {
		if (s->sc_dispatch == dispatch)
			goto pmap_it;  /* he is registering another xprt */
		rw_exit(&svc_lock);
		return (FALSE);
	}
	s = (struct svc_callout *)mem_alloc(sizeof (struct svc_callout));
	s->sc_prog = prog;
	s->sc_vers = vers;
	s->sc_dispatch = dispatch;
	s->sc_next = svc_head;
	svc_head = s;
pmap_it:
	rw_exit(&svc_lock);
	return (TRUE);
}

/*
 * Remove a service program from the callout list.
 *
 * Note: It is possible for an rpc request to be active when this routine is
 * called.  svc_unregister does not wait for that request to finish.  It
 * simply unregisters the service.  Currently this is not a problem.  See
 * the comments in svc_getreq() for details on what needs to be done if
 * this happens to be a problem.
 */
void
svc_unregister(prog, vers)
	u_long prog;
	u_long vers;
{
	struct svc_callout *prev;
	register struct svc_callout *s;

	rw_enter(&svc_lock, RW_WRITER);
	if ((s = svc_find(prog, vers, &prev)) == NULL_SVC) {
		rw_exit(&svc_lock);
		return;
	}
	if (prev == NULL_SVC) {
		svc_head = s->sc_next;
	} else {
		prev->sc_next = s->sc_next;
	}
	s->sc_next = NULL_SVC;
	rw_exit(&svc_lock);

	mem_free((char *) s, (u_int) sizeof (struct svc_callout));
}

/*
 * Search the callout list for a program number, return the callout
 * struct.
 */
static struct svc_callout *
svc_find(prog, vers, prev)
	u_long prog;
	u_long vers;
	struct svc_callout **prev;
{
	register struct svc_callout *s, *p;

	ASSERT(RW_LOCK_HELD(&svc_lock));
	p = NULL_SVC;
	for (s = svc_head; s != NULL_SVC; s = s->sc_next) {
		if ((s->sc_prog == prog) && (s->sc_vers == vers))
			goto done;
		p = s;
	}
done:
	*prev = p;
	return (s);
}

/* ******************* REPLY GENERATION ROUTINES  ************ */

/*
 * Send a reply to an rpc request
 */
bool_t
svc_sendreply(xprt, xdr_results, xdr_location)
	const  SVCXPRT *xprt;
	xdrproc_t xdr_results;
	caddr_t xdr_location;
{
	struct rpc_msg rply;

	rply.rm_direction = REPLY;
	rply.rm_reply.rp_stat = MSG_ACCEPTED;
	rply.acpted_rply.ar_verf = xprt->xp_verf;
	rply.acpted_rply.ar_stat = SUCCESS;
	rply.acpted_rply.ar_results.where = xdr_location;
	rply.acpted_rply.ar_results.proc = xdr_results;

	return (SVC_REPLY((SVCXPRT *) xprt, &rply));
}

/*
 * No procedure error reply
 */
void
svcerr_noproc(xprt)
	const  SVCXPRT *xprt;
{
	struct rpc_msg rply;

	rply.rm_direction = REPLY;
	rply.rm_reply.rp_stat = MSG_ACCEPTED;
	rply.acpted_rply.ar_verf = xprt->xp_verf;
	rply.acpted_rply.ar_stat = PROC_UNAVAIL;
	SVC_FREERES((SVCXPRT *) xprt);
	SVC_REPLY((SVCXPRT *) xprt, &rply);
}

/*
 * Can't decode args error reply
 */
void
svcerr_decode(xprt)
	const SVCXPRT *xprt;
{
	struct rpc_msg rply;

	rply.rm_direction = REPLY;
	rply.rm_reply.rp_stat = MSG_ACCEPTED;
	rply.acpted_rply.ar_verf = xprt->xp_verf;
	rply.acpted_rply.ar_stat = GARBAGE_ARGS;
	SVC_FREERES((SVCXPRT *) xprt);
	SVC_REPLY((SVCXPRT *) xprt, &rply);
}

/*
 * Some system error
 */
void
svcerr_systemerr(xprt)
	const SVCXPRT *xprt;
{
	struct rpc_msg rply;

	rply.rm_direction = REPLY;
	rply.rm_reply.rp_stat = MSG_ACCEPTED;
	rply.acpted_rply.ar_verf = xprt->xp_verf;
	rply.acpted_rply.ar_stat = SYSTEM_ERR;
	SVC_FREERES((SVCXPRT *) xprt);
	SVC_REPLY((SVCXPRT *) xprt, &rply);
}

/*
 * Authentication error reply
 */
void
svcerr_auth(xprt, why)
	const SVCXPRT *xprt;
	enum auth_stat why;
{
	struct rpc_msg rply;

	rply.rm_direction = REPLY;
	rply.rm_reply.rp_stat = MSG_DENIED;
	rply.rjcted_rply.rj_stat = AUTH_ERROR;
	rply.rjcted_rply.rj_why = why;
	SVC_FREERES((SVCXPRT *) xprt);
	SVC_REPLY((SVCXPRT *) xprt, &rply);
}

/*
 * Auth too weak error reply
 */
void
svcerr_weakauth(xprt)
	const SVCXPRT *xprt;
{

	svcerr_auth((SVCXPRT *) xprt, AUTH_TOOWEAK);
}

/*
 * Program unavailable error reply
 */
void
svcerr_noprog(xprt)
	const SVCXPRT *xprt;
{
	struct rpc_msg rply;

	rply.rm_direction = REPLY;
	rply.rm_reply.rp_stat = MSG_ACCEPTED;
	rply.acpted_rply.ar_verf = xprt->xp_verf;
	rply.acpted_rply.ar_stat = PROG_UNAVAIL;
	SVC_FREERES((SVCXPRT *) xprt);
	SVC_REPLY((SVCXPRT *) xprt, &rply);
}

/*
 * Program version mismatch error reply
 */
void
svcerr_progvers(xprt, low_vers, high_vers)
	const SVCXPRT *xprt;
	u_long low_vers;
	u_long high_vers;
{
	struct rpc_msg rply;

	rply.rm_direction = REPLY;
	rply.rm_reply.rp_stat = MSG_ACCEPTED;
	rply.acpted_rply.ar_verf = xprt->xp_verf;
	rply.acpted_rply.ar_stat = PROG_MISMATCH;
	rply.acpted_rply.ar_vers.low = low_vers;
	rply.acpted_rply.ar_vers.high = high_vers;
	SVC_FREERES((SVCXPRT *) xprt);
	SVC_REPLY((SVCXPRT *) xprt, &rply);
}

/* ******************* SERVER INPUT STUFF ******************* */

/*
 * Get server side input from some transport.
 *
 * Statement of authentication parameters management:
 * This function owns and manages all authentication parameters, specifically
 * the "raw" parameters (msg.rm_call.cb_cred and msg.rm_call.cb_verf) and
 * the "cooked" credentials (rqst->rq_clntcred).
 * However, this function does not know the structure of the cooked
 * credentials, so it make the following assumptions:
 *   a) the structure is contiguous (no pointers), and
 *   b) the cred structure size does not exceed RQCRED_SIZE bytes.
 * In all events, all three parameters are freed upon exit from this routine.
 * The storage is trivially managed on the call stack in user land, but
 * is malloced in kernel land.
 *
 * Note: procesing a request is not synchronous with svc_unregister() (i.e. it
 * is possible for a process to come along and unregister a service while
 * a request is being processed).  This is because svc_lock is not held
 * while the service's dispatch routine is running (see comment below).
 * If this is a problem, consider implementing the following solution:
 *
 *	svc_getreq() acquires the reader svc_lock,
 *		searches the dispatch table,
 *		acquires a per dispatch table entry lock or ref count,
 *		gives up reader lock.
 *
 *	svc_register does what it does today.
 *
 *	svc_unregister does what it does today, but after removing entry
 *		from table, and gives up svc_lock,
 *		it acquires entry's writer lock (or it blocks
 *		on a condition var. till ref count goes to zero).
 *		When svc_unregister is sure the entry is no longer in
 *		use, it frees it.
 *
 */

static void
svc_getreq(xprt, mp)
	SVCXPRT *xprt;			/* clone transport handle */
	mblk_t *mp;
{
	struct rpc_msg msg;
	int prog_found;
	u_long low_vers;
	u_long high_vers;
	struct svc_req r;
	char *cred_area;  /* too big to allocate on call stack */
	void (*dispatchroutine)();

	TRACE_0(TR_FAC_KRPC, TR_SVC_GETREQ_START,
		"svc_getreq_start:");

	ASSERT(xprt->xp_master != NULL);

	/*
	 * Firstly, allocate the authentication parameters' storage
	 */
	mutex_enter(&rqcred_lock);
	if (rqcred_head) {
		cred_area = rqcred_head;
		/* LINTED pointer alignment */
		rqcred_head = *(caddr_t *)rqcred_head;
		mutex_exit(&rqcred_lock);
	} else {
		mutex_exit(&rqcred_lock);
		cred_area = (char *)mem_alloc(2*MAX_AUTH_BYTES + RQCRED_SIZE);
	}
	msg.rm_call.cb_cred.oa_base = cred_area;
	msg.rm_call.cb_verf.oa_base = &(cred_area[MAX_AUTH_BYTES]);
	r.rq_clntcred = &(cred_area[2*MAX_AUTH_BYTES]);

	/* now receive msgs from xprtprt (support batch calls) */
	do {
		TRACE_0(TR_FAC_KRPC, TR_SVC_GETREQ_LOOP_START,
			"svc_getreq_loop_start:");

		if (SVC_RECV(xprt, mp, &msg)) {

			/* now find the exported program and call it */
			register struct svc_callout *s;
			enum auth_stat why;

			r.rq_xprt = xprt;
			r.rq_prog = msg.rm_call.cb_prog;
			r.rq_vers = msg.rm_call.cb_vers;
			r.rq_proc = msg.rm_call.cb_proc;
			r.rq_cred = msg.rm_call.cb_cred;
			/* first authenticate the message */
			TRACE_0(TR_FAC_KRPC, TR_SVC_GETREQ_AUTH_START,
				"svc_getreq_auth_start:");
			if ((why = _authenticate(&r, &msg)) != AUTH_OK) {
				TRACE_1(TR_FAC_KRPC, TR_SVC_GETREQ_AUTH_END,
					"svc_getreq_auth_end:(%S)", "failed");
				svcerr_auth(xprt, why);
				/*
				 * Free the arguments
				 */
				(void) SVC_FREEARGS(xprt, (xdrproc_t)0,
						    (caddr_t)0);
				continue;
			}
			TRACE_1(TR_FAC_KRPC, TR_SVC_GETREQ_AUTH_END,
				"svc_getreq_auth_end:(%S)", "good");
			/* now match message with a registered service */
			prog_found = FALSE;
			low_vers = (u_long)-1;
			high_vers = 0;
			rw_enter(&svc_lock, RW_READER);
			for (s = svc_head; s != NULL_SVC; s = s->sc_next) {
				if (s->sc_prog == r.rq_prog) {
					if (s->sc_vers == r.rq_vers) {
						/*
						 * release the svc_lock now so
						 * another request can be
						 * processed in parallel with
						 * this one.
						 * N.B. the in-kernel * lock
						 * manager requires this
						 */
						dispatchroutine =
							s->sc_dispatch;
						rw_exit(&svc_lock);
						(*dispatchroutine)(&r, xprt);
						break;
					}  /* found correct version */
					prog_found = TRUE;
					if (s->sc_vers < low_vers)
						low_vers = s->sc_vers;
					if (s->sc_vers > high_vers)
						high_vers = s->sc_vers;
				}   /* found correct program */
			}
			if (s == NULL_SVC) {
				/*
				 * if we got here, the program or version
				 * is not served ...
				 */
				/*
				 * since the correct program, version was not
				 * found, svc_lock was not released.
				 * Release it now.
				 */
				rw_exit(&svc_lock);
				if (prog_found && !version_keepquiet(xprt)) {
					svcerr_progvers(xprt, low_vers,
							high_vers);
				} else {
					svcerr_noprog(xprt);
				}
				/*
				 * Free the arguments.  This is done
				 * by the dispatch routine for successful
				 * calls.
				 */
				(void) SVC_FREEARGS(xprt, (xdrproc_t)0,
						    (caddr_t)0);
				/* Fall through to ... */
			}
		}
		TRACE_0(TR_FAC_KRPC, TR_SVC_GETREQ_LOOP_END,
			"svc_getreq_loop_end:");
	} while (SVC_STAT(xprt) == XPRT_MOREREQS);

	/*
	 * free authentication parameters' storage
	 */
	mutex_enter(&rqcred_lock);
	/* LINTED pointer alignment */
	*(caddr_t *)cred_area = rqcred_head;
	rqcred_head = cred_area;
	mutex_exit(&rqcred_lock);
}

extern int ncpus;

int Rpccnt;

/*
 * This is the rpc server side idle loop
 * Wait for input, call server program.
 * See the definition for SVCXPRT for a description of which mutex
 * variables guard which fields.
 */
static void
svc_run(xprt)
	SVCXPRT	* xprt;
{
	mblk_t	*mp;
	int	timeleft;
	SVCXPRT clone_xprt;
	char	p2buf[SVC_MAX_P2LEN];
	callb_cpr_t cprinfo;
	kmutex_t	cpr_mutex;

	/* Clear out the new xprt before grabbing any locks. */
	bzero((caddr_t)&clone_xprt, sizeof (SVCXPRT));
	bzero(p2buf, sizeof (p2buf));

	mutex_enter(&xprt->xp_lock);

	/* Create a clone xprt for our thread, once. */
	clone_xprt.xp_fp = xprt->xp_fp;
	clone_xprt.xp_ops = xprt->xp_ops;
	clone_xprt.xp_wq = xprt->xp_wq;
	clone_xprt.xp_type = xprt->xp_type;
	clone_xprt.xp_msg_size = xprt->xp_msg_size;
	clone_xprt.xp_cred = crget();
	clone_xprt.xp_master = xprt;

	/*
	 * Call the transport-specific routine to finish setting up
	 * the new xprt.  It is responsible for initializing
	 * xp_p1, xp_p2, and xp_rtaddr if necessary.
	 */
	SVC_CLONE(xprt, &clone_xprt, p2buf);

	mutex_exit(&xprt->xp_lock);

	/*
	 * Set up the checkpoint/resume callback.  It would be more natural
	 * to use the xprt's xp_req_lock for synchronizing the callback,
	 * but that would tie the thread (and callback) to the transport,
	 * which bollixes up services that need to disassociate the thread
	 * from the transport (e.g., the NFS lock manager).
	 */
	mutex_init(&cpr_mutex, "xprt thread CPR lock", MUTEX_DEFAULT,
		DEFAULT_WT);
	CALLB_CPR_INIT(&cprinfo, &cpr_mutex, callb_generic_cpr,
		"svc_run");
	clone_xprt.xp_cprp = &cprinfo;

	/*
	 * for loop iterates until the thread becomes
	 * idle too long.
	 */
	for (;;) {
		TRACE_0(TR_FAC_KRPC, TR_SVC_RUN,
			"svc_run");
		/*
		 * Inner while loop iterates until we get a request.
		 */
		timeleft = 1;
		mutex_enter(&xprt->xp_req_lock);
		while (xprt->xp_req_head == NULL) {
			/*
			 * If time expired the last time we blocked,
			 * exit if there are other idle threads alive.
			 * However, if there is only one thread left,
			 * we have to stay present so that we can fork
			 * off more threads in case the load goes back up.
			 */
			if (timeleft <= 0 &&
			    xprt->xp_threads > xprt->xp_min_threads) {
				mutex_exit(&xprt->xp_req_lock);

				/*
				 * Check thread count again, this time
				 * under proper mutex. Otherwise, two
				 * threads could simultaneously make this
				 * check and exit when only one thread
				 * should exit.
				 */
				mutex_enter(&xprt->xp_thread_lock);
				if (xprt->xp_threads > xprt->xp_min_threads) {
					svc_thread_exit(&clone_xprt, TRUE);
					/* NOTREACHED */
				}

				/*
				 * We're not allowed to exit, so
				 * exit thread mutex, and re-acquire
				 * request mutex. Since we dropped
				 * request mutex, there was a window
				 * where another message could have arrived.
				 * So, we need to test again for an empty
				 * queue before going to sleep.
				 */
				mutex_exit(&xprt->xp_thread_lock);
				mutex_enter(&xprt->xp_req_lock);
				continue;
			}

			/*
			 * No work to do, count us as asleep,
			 * and wait for a request.
			 */
			ASSERT(xprt->xp_asleep >= 0);
			xprt->xp_asleep++;
			mutex_enter(&cpr_mutex);
			CALLB_CPR_SAFE_BEGIN(&cprinfo);
			mutex_exit(&cpr_mutex);
			timeleft = cv_timedwait(&xprt->xp_req_cv,
				&xprt->xp_req_lock,
				(rpc_thread_timo * HZ) + lbolt);
			/*
			 * Mark this thread as not safe for suspend before
			 * updating asleep/drowsy counters.  This is to
			 * ensure that the counters are correct after a
			 * resume.  The CPR call might block, so we must
			 * drop the xp_req_lock before making the call.
			 * This is safe because as far as global state
			 * goes, this is equivalent to an additional delay
			 * in cv_timedwait.
			 */
			mutex_exit(&xprt->xp_req_lock);
			mutex_enter(&cpr_mutex);
			CALLB_CPR_SAFE_END(&cprinfo, &cpr_mutex);
			mutex_exit(&cpr_mutex);
			mutex_enter(&xprt->xp_req_lock);

			/*
			 * If drowsy == 0 then we have timed out
			 * before a cv_signal.
			 */
			if (xprt->xp_drowsy == 0)
				xprt->xp_asleep--;
			else
				xprt->xp_drowsy--;
			ASSERT(xprt->xp_asleep >= 0);
			ASSERT(xprt->xp_drowsy >= 0);

			/* If our stream is gone, then exit. */
			if (!xprt->xp_wq) {
				mutex_exit(&xprt->xp_req_lock);
				svc_thread_exit(&clone_xprt, FALSE);
				/* NOTREACHED */
			}
		}

		/*
		 * De-queue request.
		 */
		mp = xprt->xp_req_head;
		xprt->xp_req_head = mp->b_next;
		mp->b_next = (mblk_t *)0;
#ifdef TRACE
		/*
		 * HACK: I assume good packet.
		 */
		rpc_queued_req--;
		TRACE_4(TR_FAC_KRPC, TR_NFSFP_QUE_REQ_DEQ,
		"rpc_que_req_deq:entries on que %d (%S) proc_num %d xid %x",
		    rpc_queued_req, MP2PROCNAME(mp),
		    ((struct rpc_msg *)mp->b_cont->b_rptr) ->rm_call.cb_proc,
		    ((struct rpc_msg *)mp->b_cont->b_rptr)->rm_xid);
#endif
		/*
		 * If I was the only drowsy thread and there are
		 * more requests on the queue, then wake one up.
		 */
		ASSERT(xprt->xp_drowsy >= 0);
		ASSERT(xprt->xp_asleep >= 0);
		if ((xprt->xp_req_head != NULL) &&
		    (xprt->xp_asleep != 0 && xprt->xp_drowsy < 1)) {
			cv_signal(&xprt->xp_req_cv);
			ASSERT(xprt->xp_asleep > 0);
			xprt->xp_asleep--;
			xprt->xp_drowsy++;
		}
		mutex_exit(&xprt->xp_req_lock);

		/*
		 * Check whether we should create a new idle thread.  The
		 * asleep and drowsy checks aren't protected because (1) it
		 * hurts performance and (2) a wrong decision isn't
		 * critical.  Because we are already have at least the
		 * minimum number of threads running, there's no need to
		 * use the min. count to decide whether to create a new
		 * thread.
		 */
#ifdef DEBUG
		mutex_enter(&xprt->xp_thread_lock);
		ASSERT(xprt->xp_threads >= xprt->xp_min_threads);
		ASSERT(TOTAL_THREADS(xprt) <= xprt->xp_max_threads);
		mutex_exit(&xprt->xp_thread_lock);
#endif
		if (xprt->xp_asleep + xprt->xp_drowsy == 0 &&
		    TOTAL_THREADS(xprt) < xprt->xp_max_threads) {
			mutex_enter(&xprt->xp_thread_lock);
			if ((TOTAL_THREADS(xprt) < xprt->xp_max_threads) &&
			    thread_create(NULL, NULL, svc_run,
				(caddr_t)xprt, 0, &p0, TS_RUN, 60) != NULL) {
				xprt->xp_threads++;
			}
			mutex_exit(&xprt->xp_thread_lock);
		}

		/*
		 * Process the request.  If the clone is marked detached,
		 * exit.  (The rpcmod slot should have already been
		 * released.)
		 */
		Rpccnt++;
		svc_getreq(&clone_xprt, mp);
		if (clone_xprt.xp_detached) {
			svc_thread_exit(&clone_xprt, FALSE);
			/* NOTREACHED */
		}

		/*
		 * Release our reference on the rpcmod
		 * slot attached to xp_wq->q_ptr.
		 */
		(*RELE_PROC(xprt))(xprt->xp_wq, NULL);
	}

	/* NOTREACHED */
}

/*
 * This routine is called by rpcmod to inform KRPC that a
 * queue is closing.  Wakeup all threads running on this xprt.  The
 * last one out will destroy the xprt.
 */
void
svc_queueclose(rq)
	queue_t * rq;
{
	SVCXPRT * xprt;
	void    ** vp;

	vp = (void **)rq->q_ptr;
	xprt = vp[0];
	if (!xprt) {
		/*
		 * If there is no master xprt associated with this stream,
		 * then there is nothing to do.  This happens regularly
		 * with connection-oriented listening streams created by
		 * nfsd.
		 */
		return;
	}

	mutex_enter(&xprt->xp_req_lock);
	xprt->xp_wq = (queue_t *)0;
	cv_broadcast(&xprt->xp_req_cv);
	mutex_exit(&xprt->xp_req_lock);
}

void
svc_queuereq(q, mp)
	queue_t *q;
	mblk_t *mp;
{
	SVCXPRT * xprt;
	void    ** vp;

	TRACE_0(TR_FAC_KRPC, TR_SVC_QUEUEREQ_START,
		"svc_queuereq_start");

	vp = (void **)q->q_ptr;
	xprt = vp[0];
	if (!xprt) {
		(*rpc_rele)(q, mp);
		return;
	}

	mutex_enter(&xprt->xp_req_lock);
	if (xprt->xp_req_head == NULL) {
		xprt->xp_req_head = mp;
		xprt->xp_req_tail = mp;
	} else {
		xprt->xp_req_tail->b_next = mp;
		xprt->xp_req_tail = mp;
	}
#ifdef TRACE
	rpc_queued_req++;
	TRACE_4(TR_FAC_KRPC, TR_NFSFP_QUE_REQ_ENQ,
		"rpc_que_req_enq:entries on que %d (%S) proc_num %d xid %x",
		rpc_queued_req, MP2PROCNAME(mp),
		((struct rpc_msg *)mp->b_cont->b_rptr)->
			rm_call.cb_proc,
		((struct rpc_msg *)mp->b_cont->b_rptr)->rm_xid);
#endif

	/*
	 * If we have sleeping threads, and none are pending on the run queue,
	 * wake one up.
	 */
	ASSERT(xprt->xp_asleep >= 0);
	ASSERT(xprt->xp_drowsy >= 0);
	if (xprt->xp_asleep != 0 && xprt->xp_drowsy ==  0) {
		cv_signal(&xprt->xp_req_cv);
		xprt->xp_asleep--;
		xprt->xp_drowsy++;
	}
	mutex_exit(&xprt->xp_req_lock);

	TRACE_1(TR_FAC_KRPC, TR_SVC_QUEUEREQ_END,
		"svc_queuereq_end:(%S)", "end");
}

/*
 * Reserve a service thread for the transport so that it can be detached
 * later.  If the calling service provider does not detach the thread, it
 * should unreserve it before returning to the KRPC service loop.
 *
 * If there is room for more reserved threads, the minimum thread count for
 * the transport is incremented.  If necessary a new thread is created to
 * watch the transport.
 *
 * Returns 1 if the reservation succeeded, 0 if it failed.
 */

int
svc_reserve_thread(SVCXPRT *clone_xprt)
{
	int	result = 1;
	SVCXPRT	*xprt = clone_xprt->xp_master;
	int	reserved_threads;

	ASSERT(xprt != NULL);

	mutex_enter(&xprt->xp_thread_lock);
	ASSERT(xprt->xp_threads >= xprt->xp_min_threads);
	ASSERT(TOTAL_THREADS(xprt) <= xprt->xp_max_threads);

	/*
	 * Compute the number of threads that are reserved, i.e., either
	 * planning to detach or are already detached.  Make sure that
	 * there is still room for a thread to watch the transport if all
	 * the reserved threads detach themselves.  (Note that the min.
	 * count for the transport starts out at 1, so the number of
	 * reserved and attached threads is the min. count minus 1.)
	 */
	reserved_threads = xprt->xp_min_threads - 1;
	reserved_threads += xprt->xp_detached_threads;
	if (reserved_threads >= xprt->xp_max_threads - svc_redline) {
		result = 0;
	} else {
		xprt->xp_min_threads++;
		if (xprt->xp_threads < xprt->xp_min_threads) {
			if (thread_create(NULL, NULL, svc_run,
			    (caddr_t)xprt, 0, &p0, TS_RUN, 60) != NULL) {
				xprt->xp_threads++;
			} else {
				xprt->xp_min_threads--;
				result = 0;
			}
		}
	}
	ASSERT(TOTAL_THREADS(xprt) <= xprt->xp_max_threads);

	mutex_exit(&xprt->xp_thread_lock);

#ifdef DEBUG
	if (result == 0) {
		cmn_err(CE_NOTE,
		    "svc_reserve_thread: couldn't reserve service thread");
	}
#endif
	return (result);
}

/*
 * Release a reservation for a thread.
 *
 * This just decrements the minimum thread count.  Once the thread gets
 * back to svc_run it will do the right thing.
 */

void
svc_unreserve_thread(SVCXPRT *clone_xprt)
{
	SVCXPRT *xprt = clone_xprt->xp_master;

	ASSERT(xprt != NULL);
	mutex_enter(&xprt->xp_thread_lock);
	ASSERT(xprt->xp_min_threads > 1);
	xprt->xp_min_threads--;
	mutex_exit(&xprt->xp_thread_lock);
}

/*
 * Detach a reserved thread from its transport, so that it can block for an
 * extended time.  Because the transport can be closed after the thread is
 * detached, the thread should have already sent off a reply if it was
 * going to send one.
 *
 * The attached thread and minimum thread counts are decremented for the
 * transport, and the detached thread count is incremented.  The clone
 * transport is marked as detached.
 *
 * Returns a pointer to the thread's CPR information, so that the caller
 * can mark itself checkpoint-safe while blocked.
 */

callb_cpr_t *
svc_detach_thread(SVCXPRT *clone_xprt)
{
	callb_cpr_t	*cpr_info;
	SVCXPRT		*xprt;

	cpr_info = clone_xprt->xp_cprp;

	xprt = clone_xprt->xp_master;
	ASSERT(xprt != NULL);

	mutex_enter(&xprt->xp_thread_lock);
	ASSERT(xprt->xp_threads >= xprt->xp_min_threads);
	ASSERT(xprt->xp_min_threads > 1);
	ASSERT(TOTAL_THREADS(xprt) <= xprt->xp_max_threads);
	xprt->xp_threads--;
	xprt->xp_min_threads--;
	xprt->xp_detached_threads++;
	mutex_exit(&xprt->xp_thread_lock);

	(*RELE_PROC(xprt))(xprt->xp_wq, NULL);

	clone_xprt->xp_detached = TRUE;

	return (cpr_info);
}

/*
 * Clean up for a service thread and exit.  Does not return.
 */

static void
svc_thread_exit(SVCXPRT *clone_xprt,
	bool_t holds_thread_lock)	/* holds master's xp_thread_lock */
{
	callb_cpr_t *cprp;
	SVCXPRT *xprt;			/* master for the given clone */
	void	(*closeproc)(const SVCXPRT *);

	xprt = clone_xprt->xp_master;
	ASSERT(xprt != NULL);

	if (!holds_thread_lock) {
		mutex_enter(&xprt->xp_thread_lock);
	}
	ASSERT(MUTEX_HELD(&xprt->xp_thread_lock));

	/*
	 * Take care of the thread bookkeeping for the master.
	 * If this is the last active thread on the transport, then call the
	 * closeproc to do some of its own bookkeeping.
	 *
	 * N.B. xp_thread_lock is held during the callback, so it shouldn't
	 * dilly-dally.
	 *
	 * If this is the absolute last thread (no active or detatched ones)
	 * then destroy the transport.
	 */
	if (clone_xprt->xp_detached) {
		ASSERT(xprt->xp_detached_threads > 0);
		xprt->xp_detached_threads--;
	} else {
		ASSERT(xprt->xp_threads == 1 ||
		    (xprt->xp_threads > 1 &&
		    xprt->xp_threads > xprt->xp_min_threads));
		xprt->xp_threads--;
		if ((xprt->xp_threads == 0) && (xprt->xp_closeproc != NULL)) {
			closeproc = xprt->xp_closeproc;
			(*closeproc)(xprt);
		}
	}
	if (TOTAL_THREADS(xprt) == 0) {
		mutex_enter(&xprt->xp_lock);
		ASSERT(xprt->xp_wq == NULL);
		SVC_DESTROY(xprt);
		xprt = NULL;
	} else {
		mutex_exit(&xprt->xp_thread_lock);
	}

	/*
	 * Clean up the clone and destroy it.
	 */
	if (clone_xprt->xp_cred)
		crfree(clone_xprt->xp_cred);
	SVC_CLONE_DESTROY(clone_xprt);

	cprp = clone_xprt->xp_cprp;
	mutex_enter(cprp->cc_lockp);
	CALLB_CPR_SAFE_BEGIN(cprp);
	CALLB_CPR_EXIT(cprp);
	mutex_destroy(cprp->cc_lockp);

	thread_exit();
	/* NOTREACHED */
}
