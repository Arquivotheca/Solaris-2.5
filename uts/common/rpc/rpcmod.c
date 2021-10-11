/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma ident	"@(#)rpcmod.c	1.42	94/11/28 SMI"

/*
 * Kernel RPC filtering module
 */

#include "sys/param.h"
#include "sys/types.h"
#include "sys/stream.h"
#include "sys/stropts.h"
#include "sys/tihdr.h"
#include "sys/timod.h"
#include "sys/tiuser.h"
#include "sys/debug.h"
#include "sys/signal.h"
#include "sys/pcb.h"
#include "sys/user.h"
#include "sys/errno.h"
#include "sys/cred.h"
#include "sys/inline.h"
#include "sys/cmn_err.h"
#include "sys/kmem.h"
#include "sys/file.h"
#include "sys/sysmacros.h"
#include "sys/systm.h"
#include "sys/t_lock.h"
#include "sys/ddi.h"
#include "sys/vtrace.h"

#include "sys/strlog.h"
#include "rpc/rpc_com.h"
#include "inet/common.h"
#include "inet/nd.h"
#include "inet/mi.h"
#include "rpc/types.h"
#include "sys/time.h"
#include "rpc/xdr.h"
#include "rpc/auth.h"
#include "rpc/clnt.h"
#include "rpc/rpc_msg.h"
#include "rpc/clnt.h"
#include "rpc/svc.h"
#include "rpc/svc_auth.h"

/*
 * This is the loadable module wrapper.
 */
#include <sys/conf.h>
#include <sys/modctl.h>

extern struct streamtab rpcinfo;

static struct fmodsw fsw = {
	"rpcmod",
	&rpcinfo,
	D_NEW|D_MP,
};

/*
 * Module linkage information for the kernel.
 */
extern struct mod_ops mod_strmodops;

static struct modlstrmod modlstrmod = {
	&mod_strmodops, "rpc interface str mod", &fsw
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modlstrmod, NULL
};

_init()
{
	int			error;
	extern void mt_rpcclnt_init();
	extern void mt_rpcsvc_init();
	extern void mt_kstat_init();

	if (error = mod_install(&modlinkage))
		return (error);

	mt_rpcclnt_init();
	mt_rpcsvc_init();
	mt_kstat_init();

	return (0);
}

/*
 * The unload entry point fails, because we advertise entry points into
 * rpcmod from the rest of kRPC: rpcmod_send() and rpcmod_release().
 */
_fini()
{
	return (EBUSY);
}

_info(modinfop)
	struct modinfo *modinfop;
{
	return (mod_info(&modlinkage, modinfop));
}

extern nulldev();

#define	RPCMOD_ID	2049

int rmm_open(), rmm_close();
int rmm_rput(), rmm_wput();
int rmm_rsrv(), rmm_wsrv();

int rpcmodopen(), rpcmodclose();
void rpcmodrput(), rpcmodwput();
void rpcmodrsrv(), rpcmodwsrv();

static	int	mir_close(queue_t *q);
static	int	mir_open(queue_t *q, dev_t *devp, int flag, int sflag,
			cred_t *credp);
static	void	mir_rput(queue_t *q, mblk_t *mp);
static	void	mir_rsrv(queue_t *q);
static	void	mir_wput(queue_t *q, mblk_t *mp);
static	void	mir_wsrv(queue_t *q);

static struct module_info rpcmod_info =
	{RPCMOD_ID, "rpcmod", 0, INFPSZ, 256*1024, 1024};

/*
 * Read side has no service procedure.
 */
static struct qinit rpcmodrinit =
	{ rmm_rput, rmm_rsrv, rmm_open, rmm_close,
		nulldev, &rpcmod_info, NULL};

/*
 * The write put procedure is simply putnext to conserve stack space.
 * The write service procedure is not used to queue data, but instead to
 * synchronize with flow control.
 */
static struct qinit rpcmodwinit =
	{ rmm_wput, rmm_wsrv, rmm_open, rmm_close, nulldev,
	    &rpcmod_info, NULL};
struct streamtab rpcinfo = { &rpcmodrinit, &rpcmodwinit, NULL, NULL };

struct xprt_style_ops {
	int (*xo_open)();
	int (*xo_close)();
	void (*xo_wput)();
	void (*xo_wsrv)();
	void (*xo_rput)();
	void (*xo_rsrv)();
};

static struct xprt_style_ops xprt_clts_ops = {
	rpcmodopen,
	rpcmodclose,
	(void (*)())putnext,
	rpcmodwsrv,
	rpcmodrput,
	NULL
};

static struct xprt_style_ops xprt_cots_ops = {
	mir_open,
	mir_close,
	mir_wput,
	mir_wsrv,
	mir_rput,
	mir_rsrv
};

/*
 * Per rpcmod "slot" data structure. q->q_ptr points to one of these.
 */
struct rpcm {
	void		*rm_krpc_cell;	/* Reserved for use by KRPC */
	struct		xprt_style_ops	*rm_ops;
	int		rm_type;	/* Client or server side stream */
#define	RM_CLOSING	0x1		/* somebody is trying to close slot */
	ulong		rm_state;	/* state of the slot. see above */
	long		rm_flowed;	/* cnt of threads blocked on flow ctl */
	long		rm_ref;		/* cnt of external references to slot */
	kmutex_t	rm_lock;	/* mutex protecting above fields */
	kcondvar_t	rm_fwait;	/* condition for flow control */
	kcondvar_t	rm_cwait;	/* condition for closing */
};

struct temp_slot {
	void *cell;
	struct xprt_style_ops *ops;
	int type;
	mblk_t *info_ack;
	kmutex_t lock;
	kcondvar_t wait;
};

void tmp_rput(queue_t *q, mblk_t *mp);

struct xprt_style_ops tmpops = {
	NULL,
	NULL,
	(void (*)())putnext,
	NULL,
	tmp_rput,
	NULL
};

void
tmp_rput(queue_t *q, mblk_t *mp)
{
	struct temp_slot *t = (struct temp_slot *)(q->q_ptr);
	struct T_info_ack *pptr;

	switch (mp->b_datap->db_type) {
	case M_PCPROTO:
		pptr = (struct T_info_ack *)mp->b_rptr;
		switch (pptr->PRIM_type) {
		case T_INFO_ACK:
			mutex_enter(&t->lock);
			t->info_ack = mp;
			cv_signal(&t->wait);
			mutex_exit(&t->lock);
			return;
		default:
			break;
		}
	default:
		break;
	}

	/*
	 * Not an info-ack, so free it. This is ok because we should
	 * not be receiving data until the open finishes: rpcmod
	 * is pushed well before the end-point is bound to an address.
	 */
	freemsg(mp);
}


rmm_open(queue_t *q, dev_t *devp, int flag, int sflag, cred_t *crp)
{
	mblk_t *bp;
	struct temp_slot ts, *t;
	struct T_info_ack *pptr;
	int error = 0;
	int procson = 0;

	ASSERT(q != NULL);
	/*
	 * Check for re-opens.
	 */
	if (q->q_ptr) {
		TRACE_1(TR_FAC_KRPC, TR_RPCMODOPEN_END,
			"rpcmodopen_end:(%s)", "q->qptr");
		return (0);
	}

	t = &ts;
	bzero((caddr_t)t, sizeof (ts));
	q->q_ptr = (void *)t;
	/* WR(q)->q_ptr = (void *)t; */

	/*
	 * Allocate the required messages upfront.
	 */
	if ((bp = allocb(sizeof (struct T_info_req) +
	    sizeof (struct T_info_ack), BPRI_LO)) == (mblk_t *)NULL) {
		return (ENOBUFS);
	}

	mutex_init(&t->lock, "rpcmod temp slot lock", MUTEX_DEFAULT, NULL);
	cv_init(&t->wait, "rpcmod temp slot wait", CV_DEFAULT, NULL);

	t->ops = &tmpops;

	qprocson(q);
	procson = 1;
	bp->b_datap->db_type = M_PCPROTO;
	*(long *)bp->b_wptr = (long)T_INFO_REQ;
	bp->b_wptr += sizeof (struct T_info_req);
	putnext(WR(q), bp);

	mutex_enter(&t->lock);
	while ((bp = t->info_ack) == NULL) {
		if (cv_wait_sig(&t->wait, &t->lock) == 0) {
			error = EINTR;
			break;
		}
	}
	mutex_exit(&t->lock);
	mutex_destroy(&t->lock);
	cv_destroy(&t->wait);
	if (error)
		goto out;

	pptr = (struct T_info_ack *)t->info_ack->b_rptr;

	if (pptr->SERV_type == T_CLTS) {
		error = rpcmodopen(q, devp, flag, sflag, crp);
		if (error == 0) {
			t = (struct temp_slot *)q->q_ptr;
			t->ops = &xprt_clts_ops;
		}
	} else {
		error = mir_open(q, devp, flag, sflag, crp);
		if (error == 0) {
			t = (struct temp_slot *)q->q_ptr;
			t->ops = &xprt_cots_ops;
		}
	}

out:
	freemsg(bp);

	if (error && procson)
		qprocsoff(q);

	return (error);
}

rmm_rput(queue_t *q, mblk_t  *mp)
{
	(*((struct temp_slot *)q->q_ptr)->ops->xo_rput)(q, mp);
#ifdef	lint
	/*
	 * Return codes are not looked at by the STREAMS framework.
	 */
	return (0);
#endif
}

rmm_rsrv(queue_t *q)
{
	(*((struct temp_slot *)q->q_ptr)->ops->xo_rsrv)(q);
#ifdef	lint
	/*
	 * Return codes are not looked at by the STREAMS framework.
	 */
	return (0);
#endif
}

rmm_wput(queue_t *q, mblk_t *mp)
{
	(*((struct temp_slot *)q->q_ptr)->ops->xo_wput)(q, mp);
#ifdef	lint
	/*
	 * Return codes are not looked at by the STREAMS framework.
	 */
	return (0);
#endif
}

rmm_wsrv(queue_t *q)
{
	(*((struct temp_slot *)q->q_ptr)->ops->xo_wsrv)(q);
#ifdef	lint
	/*
	 * Return codes are not looked at by the STREAMS framework.
	 */
	return (0);
#endif
}

rmm_close(queue_t *q, int flag, cred_t *crp)
{
	return ((*((struct temp_slot *)q->q_ptr)->ops->xo_close)(q, flag, crp));
}

/*
 * rpcmodopen -	open routine gets called when the module gets pushed
 *		onto the stream.
 */
/*ARGSUSED*/
rpcmodopen(queue_t *q, dev_t *devp, int flag, int sflag, cred_t *crp)
{
	struct rpcm *rmp;
#if defined(_LOCKTEST) || defined(_MPSTATS)
	char buf[20];
#endif

	extern int (*rpc_send)(queue_t *, mblk_t *);
	static int rpcmod_send(queue_t *, mblk_t *);

	extern void (*rpc_rele)(queue_t *, mblk_t *);
	static void rpcmod_release(queue_t *, mblk_t *);

	TRACE_0(TR_FAC_KRPC, TR_RPCMODOPEN_START,
		"rpcmodopen_start:");

	/*
	 * Initialize entry points to release a rpcmod slot (and an input
	 * message if supplied) and to send an output message to the module
	 * below rpcmod.
	 */
	if (rpc_send == NULL) {
		rpc_rele = rpcmod_release;
		rpc_send = rpcmod_send;
	}
	/*
	 * The write put procedure is just putnext(), and thus does
	 * not call canput() to check for downstream flow control.
	 * We could implement a rpcmod_wput to do this, but if we
	 * did, we would have to queue data on canput() failures, making
	 * rpcmod_wsrv() more complex (slower). If we allow any user
	 * to push rpcmod, it would be a trivial thing for the user to
	 * run the system out of memory by abusing the module.
	 *
	 * Only root can use this module, and it is assumed that root
	 * will use this module properly, and NOT send bulk data from
	 * downstream.
	 */
	if (suser(crp) == 0)
		return (EPERM);

	/*
	 * Allocate slot data structure.
	 */
	rmp = kmem_zalloc(sizeof (*rmp), KM_SLEEP);

#if defined(_LOCKTEST) || defined(_MPSTATS)
	(void) sprintf(buf, "rpcm slot %lx", rmp);
	mutex_init(&rmp->rm_lock, buf, MUTEX_DEFAULT, NULL);
#else
	mutex_init(&rmp->rm_lock, "rpcm slot", MUTEX_DEFAULT, NULL);
#endif

	cv_init(&rmp->rm_fwait, "rpcmod slot flow wait", CV_DEFAULT, NULL);
	cv_init(&rmp->rm_cwait, "rpcmod slot close wait", CV_DEFAULT, NULL);
	rmp->rm_type = RPC_SERVER;

	q->q_ptr = (void *)rmp;
	WR(q)->q_ptr = (void *)rmp;

	TRACE_1(TR_FAC_KRPC, TR_RPCMODOPEN_END,
		"rpcmodopen_end:(%s)", "end");
	return (0);
}

/*
 * rpcmodclose - This routine gets called when the module gets popped
 * off of the stream.
 */
/*ARGSUSED*/
rpcmodclose(queue_t *q, int flag, cred_t *crp)
{
	struct rpcm *rmp;

	ASSERT(q != NULL);
	rmp = (struct rpcm *)q->q_ptr;

	/*
	 * Mark our state as closing.
	 */
	mutex_enter(&rmp->rm_lock);
	rmp->rm_state |= RM_CLOSING;

	/*
	 * If any threads are blocked on flow control, force them
	 * wake up, and send their messages, regardless whether the downstream
	 * module is ready to accept data.
	 */
	if (rmp->rm_flowed)
		qenable(WR(q));

	/*
	 * Block while there are kRPC threads with a reference to
	 * this message.
	 */
	while (rmp->rm_ref) {
		cv_wait(&rmp->rm_cwait, &rmp->rm_lock);
	}
	mutex_exit(&rmp->rm_lock);

	/*
	 * It now safe to remove this queue from the stream. No kRPC threads
	 * have a reference to the stream, and none ever will, because
	 * RM_CLOSING is set.
	 */
	qprocsoff(q);

	/* Notify KRPC that this stream is going away. */
	svc_queueclose(q);

	q->q_ptr = NULL;
	WR(q)->q_ptr = NULL;
	mutex_destroy(&rmp->rm_lock);
	cv_destroy(&rmp->rm_fwait);
	cv_destroy(&rmp->rm_cwait);
	kmem_free((char *)rmp, sizeof (*rmp));
	return (0);
}

#ifdef	DEBUG
int	rpcmod_send_msg_up = 0;
int	rpcmod_send_uderr = 0;
int	rpcmod_send_dup = 0;
int	rpcmod_send_dup_cnt = 0;
#endif

/*
 * rpcmodrput -	Module read put procedure.  This is called from
 *		the module, driver, or stream head downstream.
 */
void
rpcmodrput(queue_t *q, mblk_t *mp)
{
	struct rpcm *rmp;
	union T_primitives *pptr;

	TRACE_0(TR_FAC_KRPC, TR_RPCMODRPUT_START,
		"rpcmodrput_start:");

	ASSERT(q != NULL);
	rmp = (struct rpcm *)q->q_ptr;

	if (rmp->rm_type == 0)
		return;

#ifdef DEBUG
	if (rpcmod_send_msg_up > 0) {
		mblk_t *nmp = copymsg(mp);
		if (nmp) {
			putnext(q, nmp);
			rpcmod_send_msg_up--;
		}
	}
	if ((rpcmod_send_uderr > 0) && mp->b_datap->db_type == M_PROTO) {
		mblk_t *nmp;
		struct T_unitdata_ind *data;
		struct T_uderror_ind *ud;
		int d;
		data = (struct T_unitdata_ind *)mp->b_rptr;
		if (data->PRIM_type == T_UNITDATA_IND) {
			d = sizeof (*ud) - sizeof (*data);
			nmp = allocb(mp->b_wptr - mp->b_rptr + d, BPRI_HI);
			if (nmp) {
				ud = (struct T_uderror_ind *)nmp->b_rptr;
				ud->PRIM_type = T_UDERROR_IND;
				ud->DEST_length = data->SRC_length;
				ud->DEST_offset = data->SRC_offset + d;
				ud->OPT_length = data->OPT_length;
				ud->OPT_offset = data->OPT_offset + d;
				ud->ERROR_type = ENETDOWN;
				if (data->SRC_length) {
					bcopy((caddr_t)mp->b_rptr
						+ data->SRC_offset,
						(caddr_t)nmp->b_rptr
						+ ud->DEST_offset,
						(size_t)data->SRC_length);
				}
				if (data->OPT_length) {
					bcopy((caddr_t)mp->b_rptr
						+ data->OPT_offset,
						(caddr_t)nmp->b_rptr
						+ ud->OPT_offset,
						(size_t)data->OPT_length);
				}
				nmp->b_wptr += d;
				nmp->b_wptr += (mp->b_wptr - mp->b_rptr);
				nmp->b_datap->db_type = M_PROTO;
				putnext(q, nmp);
				rpcmod_send_uderr--;
			}
		}
	}
#endif
	switch (mp->b_datap->db_type) {
	default:
		putnext(q, mp);
		break;

	case M_PROTO:
	case M_PCPROTO:
		ASSERT((mp->b_wptr - mp->b_rptr) >= sizeof (long));
		pptr = (union T_primitives *)mp->b_rptr;

		/*
		 * Forward this message to krpc if it is data.
		 */
		if (pptr->type == T_UNITDATA_IND) {
#ifdef DEBUG
			mblk_t *nmp;
#endif

			/*
			 * Check if the module is being popped.
			 */
			mutex_enter(&rmp->rm_lock);
			if (rmp->rm_state & RM_CLOSING) {
				mutex_exit(&rmp->rm_lock);
				putnext(q, mp);
				break;
			}
#ifdef DEBUG
			/*
			 * Test duplicate request cache and rm_ref count
			 * handling by sending a duplicate every so often,
			 * if desired.
			 */
			if (rpcmod_send_dup &&
				rpcmod_send_dup_cnt++ % rpcmod_send_dup)
				nmp = copymsg(mp);
			else
				nmp = NULL;
#endif
			/*
			 * Raise the reference count on this module to
			 * prevent it from being popped before krpc generates
			 * the reply.
			 */
			rmp->rm_ref++;
			mutex_exit(&rmp->rm_lock);

			/*
			 * Submit the message to krpc.
			 */
			svc_queuereq(q, mp);
#ifdef DEBUG
			/*
			 * Send duplicate if we created one.
			 */
			if (nmp) {
				mutex_enter(&rmp->rm_lock);
				rmp->rm_ref++;
				mutex_exit(&rmp->rm_lock);
				svc_queuereq(q, nmp);
			}
#endif

			break;
		}
		putnext(q, mp);
		break;
	}
	TRACE_0(TR_FAC_KRPC, TR_RPCMODRPUT_END,
		"rpcmodrput_end:");
	/*
	 * Return codes are not looked at by the STREAMS framework.
	 */
}

/*
 * Module write service procedure. This is called by downstream modules
 * for back enabling during flow control.
 */
void
rpcmodwsrv(queue_t *q)
{
	struct rpcm *rmp;

	rmp = (struct rpcm *)q->q_ptr;
	mutex_enter(&rmp->rm_lock);

	/*
	 * If any kRPC threads are blocked in flow control,
	 * wake them up.
	 */
	if (rmp->rm_flowed) {
		cv_broadcast(&rmp->rm_fwait);
	}

	mutex_exit(&rmp->rm_lock);
}

/*
 * Entry point for krpc into rpcmod to send response.
 */
static int
rpcmod_send(queue_t *q, mblk_t *bp)
{
	register struct rpcm *rmp;
	register int locked;

	rmp = (struct rpcm *)q->q_ptr;

	locked = 0;

	/*
	 * Block untill downstream flow control has ebbed.
	 */
	/*CONSTCOND*/
	while (1) {
		/*
		 * The probability is that canputnext() will succeed,
		 * so on the first iteration we do the check without a lock.
		 */
		if (canputnext(q))
			break;

		/*
		 * canputnext() failed. Acquire the lock if not
		 * held.
		 */
		if (!locked) {
			locked = 1;
			mutex_enter(&rmp->rm_lock);

			/*
			 * We have to recheck the flow control
			 * because the back enable could have happenned
			 * before we go to cv_wait().
			 */
			if (canputnext(q))
				break;
		}

		/*
		 * If somebody is trying to pop rpcmod, don't bother
		 * blocking in flow control. Send the response now.
		 * We don't need to do this under a lock, and so could
		 * do this check sooner. However, nothing is gained in
		 * optimizing the infrequent kRPC handle tear down state.
		 */
		if (rmp->rm_state & RM_CLOSING)
			break;

		/*
		 * Mark this thread as blocked on flow control.
		 */
		rmp->rm_flowed++;
		cv_wait(&rmp->rm_fwait, &rmp->rm_lock);

		/*
		 * We've been signaled, so flow control has apparently
		 * ebbed.
		 */
		rmp->rm_flowed--;
	}

	/*
	 * Release the mutex if we entered it.
	 */
	if (locked) {
		mutex_exit(&rmp->rm_lock);
	}

	/*
	 * Send the response.
	 */
	putnext(q, bp);

	/*
	 * Return success to the caller.
	 */
	return (0);
}

static void
rpcmod_release(queue_t *q, mblk_t *bp)
{
	struct rpcm *rmp;

	/*
	 * For now, just free the message.
	 */
	if (bp)
		freemsg(bp);
	rmp = (struct rpcm *)q->q_ptr;

	mutex_enter(&rmp->rm_lock);
	rmp->rm_ref--;

	if (rmp->rm_ref == 0 && (rmp->rm_state & RM_CLOSING)) {
		cv_broadcast(&rmp->rm_cwait);
	}

	mutex_exit(&rmp->rm_lock);
}


/*
 * Copyright (c) 1993  Mentat Inc.
 */

/*
 * This part of rpcmod is pushed on a connection-oriented transport for use
 * by RPC.  It serves to bypass the Stream head, implements
 * the record marking protocol, and dispatches incoming RPC messages.
 */

/* Default idle timer values */
#define	MIR_CLNT_IDLE_TIMEOUT	(5 * (60 * 1000L))	/* 5 minutes */
#define	MIR_SVC_IDLE_TIMEOUT	(6 * (60 * 1000L))	/* 6 minutes */
#define	MIR_BUFCALL_TIMEOUT	(500L)			/* 1/2 second */
#define	MILLISEC2HZ(x)		((x) * HZ / 1000)	/* convert ms to HZ */
#define	HZ2MILLISEC(x)		((x) * 1000 / HZ)	/* convert HZ to ms */

#define	MIR_LASTFRAG	0x80000000	/* Record marker */

#define	DLEN(mp) (mp->b_cont ? msgdsize(mp) : (mp->b_wptr - mp->b_rptr))

#ifndef OK_LONG_PTR
#define	OK_LONG_PTR(p)  ((((unsigned long)p) & (sizeof (long)-1)) == 0)
#endif

typedef struct mir_s {
	void	*mir_krpc_cell;	/* Reserved for KRPC use. This field */
					/* must be first in the structure. */
	struct xprt_style_ops	*rm_ops;
	int	mir_type;		/* Client or server side stream */

	mblk_t	*mir_head_mp;		/* RPC msg in progress */
		/*
		 * mir_head_mp points the first mblk being collected in
		 * the current RPC message.  Record headers are removed
		 * before data is linked into mir_head_mp.
		 */
	mblk_t	*mir_tail_mp;		/* Last mblk in mir_head_mp */
		/*
		 * mir_tail_mp points to the last mblk in the message
		 * chain starting at mir_head_mp.  It is only valid
		 * if mir_head_mp is non-NULL and is used to add new
		 * data blocks to the end of chain quickly.
		 */

	i32	mir_frag_len;		/* Bytes seen in the current frag */
		/*
		 * mir_frag_len starts at -4 for beginning of each fragment.
		 * When this length is negative, it indicates the number of
		 * bytes that rpcmod needs to complete the record marker
		 * header.  When it is positive or zero, it holds the number
		 * of bytes that have arrived for the current fragment and
		 * are held in mir_header_mp.
		 */

	i32	mir_frag_header;
		/*
		 * Fragment header as collected for the current fragment.
		 * It holds the last-fragment indicator and the number
		 * of bytes in the fragment.
		 */

	i32	mir_excess;
		/* Excess bytes in this mblk, only used in case of bufcall. */


	unsigned int
		mir_priv_stream : 1,	/* User is privileged. */
		mir_ordrel_pending : 1,	/* Connection is broken.  Don't send */
					/* or receive any more data. */
		mir_hold_inbound : 1,	/* Hold inbound messages on server */
					/* side until outbound flow control */
					/* is relieved. */
		mir_closing : 1,	/* The stream is being closed */
		mir_awaiting_memory : 1, /* dupb() failed, recovery pending */
		mir_inrservice : 1,	/* data queued or rd srv proc running */
		mir_inwservice : 1,	/* data queued or wr srv proc running */
		mir_junk_fill_thru_bit_31 : 24;


	int	mir_bufcall_id;		/* id of outstanding bufcall */
	mblk_t	*mir_first_non_processed_mblk;	/* used in bufcall recovery */

	int	mir_req_cnt;		/* Request count */
		/*
		 * On client streams, mir_req_cnt is 0 or 1; it is set
		 * to 1 whenever a new request is sent out (mir_wput)
		 * and cleared when the timer fires (mir_timer).  If
		 * the timer fires with this value equal to 0, then the
		 * stream is considered idle and KRPC is notified.
		 *
		 * On server streams, this is an actual count of the
		 * inbound requests received (incremented by mir_rput).
		 * The stream is considered idle if mir_req_cnt equals
		 * mir_rele_cnt and mir_reply_cnt.  mir_req_cnt is never
		 * decremented.
		 */

	mblk_t	*mir_timer_mp;		/* Timer message for idle checks */
	u_long	mir_idle_timeout;	/* Allowed idle time before shutdown */
		/*
		 * This value is copied from clnt_idle_timeout or
		 * svc_idle_timeout during the appropriate ioctl.
		 * Kept in milliseconds
		 */
	clock_t	mir_use_timestamp;	/* updated on client with each use */
		/*
		 * This value is set to lbolt
		 * every time a client stream sends or receives data.
		 * Even if the timer message arrives, we don't shutdown
		 * client unless:
		 *    lbolt >= MILLISEC2HZ(mir_idle_timeout)+mir_use_timestamp.
		 * This value is kept in HZ.
		 */

	u_long	*mir_max_msg_sizep;	/* Reference to sanity check size */
		/*
		 * This pointer is set to &clnt_max_msg_size or
		 * &svc_max_msg_size during the appropriate ioctl.
		 */

	/* Server-side fields. */
	int	mir_reply_cnt;		/* Reply count */
		/*
		 * mir_reply_cnt is incremented in mir_wput when a RPC
		 * reply is passed downstream.  It is reset to mir_rele_cnt
		 * in mir_wsrv if the two values are not equal; this can
		 * happen if KRPC releases a request without sending a
		 * reply.  This count is never decremented.
		 */

	int	mir_rele_cnt;		/* Release count */
		/*
		 * mir_rele_cnt is incremented by mir_svc_release.  If
		 * it is not equal to mir_reply_cnt, mir_svc_release
		 * enables the write-side queue, and mir_wsrv reconciles
		 * the counts and starts the idle timer if necessary.
		 * This field is guarded by mir_mutex. It is never decremented.
		 */

	mblk_t		*mir_ordrel_mp;	/* Pending T_ORDREL_REQ. */
	kmutex_t	mir_mutex;	/* Mutex and condvar for close */
	krwlock_t	mir_send_lock;	/* synchronization for transmit */
	kcondvar_t	mir_condvar;	/* synchronization. */
} mir_t;

#define	MIR_LOCK(mir)	mutex_enter(&(mir)->mir_mutex)
#define	MIR_UNLOCK(mir)	mutex_exit(&(mir)->mir_mutex)
#define	MIR_LOCK_HELD(mir)	MUTEX_HELD(&(mir)->mir_mutex)
#define	MIR_LOCK_NOT_HELD(mir)	MUTEX_NOT_HELD(&(mir)->mir_mutex)

#define	MIR_TRY_SEND_LOCK(mir, rw)	rw_tryenter(&(mir)->mir_send_lock, (rw))
#define	MIR_SEND_LOCK(mir, rw)	rw_enter(&(mir)->mir_send_lock, (rw))
#define	MIR_SEND_UNLOCK(mir)	rw_exit(&(mir)->mir_send_lock)

static	void	mir_bufcall_callback(long q_param);
static boolean_t	mir_clnt_dup_request(queue_t *q, mblk_t *mp);
static	int	mir_close(queue_t *q);
static	int	mir_open(queue_t *q, dev_t *devp, int flag, int sflag,
			cred_t *credp);
static	void	mir_rput(queue_t *q, mblk_t *mp);
static	void	mir_rput_proto(queue_t *q, mblk_t *mp);
static	void	mir_rsrv(queue_t *q);
#if 0
static int	mir_set_idle_timeout(queue_t *q, mblk_t *mp,
			char *value, caddr_t arg);
#endif
static	int	mir_svc_policy_notify(queue_t *q, int event);
static void    mir_svc_release(queue_t *wq, mblk_t *mp);
static void    mir_svc_start(queue_t *wq);
static void    mir_clnt_stop_idle(queue_t * wq);
static	void	mir_wput(queue_t *q, mblk_t *mp);
static void	mir_wput_other(queue_t *q, mblk_t *mp);
static	void	mir_wsrv(queue_t *q);

char	_depends_on[] = "drv/ip";	/* Necessary for mi_timer references. */

extern  void    (*mir_rele)(queue_t *, mblk_t *);
extern  void    (*mir_start)(queue_t *);
extern  void    (*clnt_stop_idle)(queue_t *);
extern	u_long	*clnt_max_msg_sizep;
extern	u_long	*svc_max_msg_sizep;

u_long	clnt_idle_timeout = MIR_CLNT_IDLE_TIMEOUT;
u_long	svc_idle_timeout = MIR_SVC_IDLE_TIMEOUT;
u_long	clnt_max_msg_size = RPC_MAXDATASIZE;
u_long	svc_max_msg_size = RPC_MAXDATASIZE;

static void
mir_bufcall_callback(long q_param)
{
	queue_t	*q = (queue_t *)q_param;
	mir_t	*mir = (mir_t *)q->q_ptr;

	if (!mir)
		return;
	if (mir->mir_bufcall_id == 0)
		return;
	ASSERT(MIR_LOCK_NOT_HELD(mir));
	MIR_LOCK(mir);
	mir->mir_bufcall_id = 0;
	MIR_UNLOCK(mir);
	qenable(q);
}

static boolean_t
mir_clnt_dup_request(queue_t *q, mblk_t *mp)
{
	mblk_t  *mp1;
	u_long  new_xid;
	u_long  old_xid;

	ASSERT(MIR_LOCK_HELD((mir_t *)q->q_ptr));
	new_xid = BE32_TO_U32(&mp->b_rptr[4]);
	/*
	 * This loop is a bit tacky -- it walks the STREAMS list of
	 * flow-controlled messages.
	 */
	if ((mp1 = q->q_first) != NULL) {
		do {
			old_xid = BE32_TO_U32(&mp1->b_rptr[4]);
			if (new_xid == old_xid)
				return (1);
		} while ((mp1 = mp1->b_next) != NULL);
	}
	return (0);
}

static int
mir_close(queue_t *q)
{
	mir_t	*mir;
	mblk_t	*mp;
	int	bid;

	mir = (mir_t *)q->q_ptr;
	ASSERT(MIR_LOCK_NOT_HELD(mir));
	MIR_LOCK(mir);
	if ((mp = mir->mir_head_mp) != NULL) {
		mir->mir_head_mp = (mblk_t *)0;
		freemsg(mp);
	}
	if ((mp = mir->mir_timer_mp) != NULL) {
		mir->mir_timer_mp = (mblk_t *)0;
		mi_timer_free(mp);
	}

	if (mir->mir_type == RPC_SERVER) {
		flushq(q, FLUSHDATA);	/* Ditch anything waiting on read q */

		/*
		 * Set ordrel_pending so that no new RPC messages are
		 * passed to KRPC.
		 */

		mir->mir_ordrel_pending = 1;

		while (mir->mir_reply_cnt != mir->mir_req_cnt ||
		    mir->mir_rele_cnt != mir->mir_req_cnt) {
			mir->mir_closing = 1;
			cv_wait(&mir->mir_condvar, &mir->mir_mutex);
		}
		if ((mp = mir->mir_ordrel_mp) != NULL) {
			mir->mir_ordrel_mp = (mblk_t *)0;
			freemsg(mp);
		}
		bid = mir->mir_bufcall_id;
		mir->mir_bufcall_id = 0;
		MIR_UNLOCK(mir);
		qprocsoff(q);

		/* Notify KRPC that this stream is going away. */
		svc_queueclose(q);
	} else {
		bid = mir->mir_bufcall_id;
		mir->mir_bufcall_id = 0;
		MIR_UNLOCK(mir);
		qprocsoff(q);
	}

	if (bid)
		unbufcall(bid);
	mutex_destroy(&mir->mir_mutex);
	rw_destroy(&mir->mir_send_lock);
	cv_destroy(&mir->mir_condvar);
	kmem_free((void *)mir, sizeof (mir_t));
	return (0);
}

#if 0
static int
mir_dump(queue_t *q, mblk_t *mp, caddr_t data)
{
	mir_t	*mir;

	mir = *((mir_t **)data);
	if (!mir)
		return (0);
	mi_mpprintf(mp, "mir 0x%x, frag_header 0x%x, frag_len %d",
		mir, mir->mir_frag_header, mir->mir_frag_len);
	mi_mpprintf(mp, " head_mp 0x%x size %d, tail_mp 0x%x",
		mir->mir_head_mp,
		mir->mir_head_mp ? DLEN(mir->mir_head_mp) : 0,
		mir->mir_tail_mp);
	return (0);
}
#endif

/*
 * This is server side only (RPC_SERVER).
 */
static void
mir_idle_start(queue_t *q, mir_t *mir)
{
	mblk_t  *mp;

	ASSERT(MIR_LOCK_HELD(mir));
	if (mir->mir_ordrel_mp || mir->mir_closing) {
		if (mir->mir_closing)
			cv_signal(&mir->mir_condvar);
		/*
		 * If we are holding a T_ORDREL_REQ message, then now is the
		 * time to pass it downstream.  mir_ordrel_pending is set so
		 * that new inbound messages will be discarded in mir_rput
		 * (after passing the orderly release downstream, we will not
		 * be able to reply to such requests, so we shouldn't try).
		 */

		if ((mp = mir->mir_ordrel_mp) != NULL) {
			mir->mir_ordrel_mp = (mblk_t *)0;
			mir->mir_ordrel_pending = 1;
			MIR_UNLOCK(mir);
			putnext(q, mp);
			MIR_LOCK(mir);
		}
	} else {
		/* Normal condition, start the idle timer. */
		mi_timer(q, mir->mir_timer_mp, mir->mir_idle_timeout);
	}
}

/* ARGSUSED */
static int
mir_open(queue_t *q, dev_t *devp, int flag, int sflag, cred_t *credp)
{
	mir_t	*mir;
#if defined(_LOCKTEST) || defined(_MPSTATS)
	char	buf[20];
#endif

	/* Set variables used directly by KRPC. */
	if (!mir_rele)
		mir_rele = mir_svc_release;
	if (!mir_start)
		mir_start = mir_svc_start;
	if (!clnt_stop_idle)
		clnt_stop_idle = mir_clnt_stop_idle;
	if (!clnt_max_msg_sizep)
		clnt_max_msg_sizep = &clnt_max_msg_size;
	if (!svc_max_msg_sizep)
		svc_max_msg_sizep = &svc_max_msg_size;

	/* Allocate a mir structure and timer mblk for this stream. */
	mir = (mir_t *)kmem_zalloc(sizeof (mir_t), KM_SLEEP);
	mir->mir_type = 0;
	mir->mir_timer_mp = mi_timer_alloc(0);
	if (!mir->mir_timer_mp) {
		kmem_free((char *)mir, sizeof (mir_t));
		return (ENOMEM);
	}

	/*
	 * We set hold inbound here so that incoming messages will
	 * be held on the read-side queue until the stream is completely
	 * initialized with a RPC_CLIENT or RPC_SERVER ioctl.  During
	 * the ioctl processing, the flag is cleared and any messages that
	 * arrived between the open and the ioctl are delivered to KRPC.
	 *
	 * Early data should never arrive on a client stream since
	 * servers only respond to our requests and we do not send any.
	 * until after the stream is initialized.  Early data is
	 * very common on a server stream where the client will start
	 * sending data as soon as the connection is made (and this
	 * is especially true with TCP where the protocol accepts the
	 * connection before nfsd or KRPC is notified about it).
	 */

	mir->mir_hold_inbound = 1;

	/*
	 * Start the record marker looking for a 4-byte header.  When
	 * this length is negative, it indicates that rpcmod is looking
	 * for bytes to consume for the record marker header.  When it
	 * is positive, it holds the number of bytes that have arrived
	 * for the current fragment and are being held in mir_header_mp.
	 */

	mir->mir_frag_len = -sizeof (u32);

#if defined(_LOCKTEST) || defined(_MPSTATS)
	(void) sprintf(buf, "mir slot %lx", mir);
	mutex_init(&mir->mir_mutex, buf, MUTEX_DEFAULT, (void *)0);
#else
	mutex_init(&mir->mir_mutex, "mir slot", MUTEX_DEFAULT, (void *)0);
#endif

#if defined(_LOCKTEST) || defined(_MPSTATS)
	(void) sprintf(buf, "mir send %lx", mir);
	rw_init(&mir->mir_send_lock, buf, RW_DEFAULT, (void *)0);
#else
	rw_init(&mir->mir_send_lock, "mir send", RW_DEFAULT, (void *)0);
#endif
	cv_init(&mir->mir_condvar, "rpcmod condvar for close synch",
		CV_DRIVER, (void *)0);

	if (credp && drv_priv(credp) == 0)
		mir->mir_priv_stream = 1;

	q->q_ptr = (char *)mir;
	WR(q)->q_ptr = (char *)mir;

	/*
	 * We noenable the read-side queue because we don't want it
	 * automatically enabled by putq.  We enable it explicitly
	 * in mir_wsrv when appropriate. (See additional comments on
	 * flow control at the beginning of mir_rsrv.)
	 */
	noenable(q);

	qprocson(q);
	return (0);
}

#define	ADD_PUTNEXT(putnext_head, putnext_tail, mp) \
	if (putnext_head == NULL)  { \
		putnext_head = mp; \
	} else { \
		putnext_tail->b_next = mp; \
	} \
	putnext_tail = mp

#define	SEND_PUTNEXT(q, putnext_head) { \
	mblk_t *mp; \
	while ((mp = putnext_head) != NULL) { \
		putnext_head = mp->b_next; \
		mp->b_next = NULL; \
		putnext(q, mp); \
	} \
}

/*
 * Read-side put routine for both the client and server side.  Does the
 * record marking for incoming RPC messages, and when complete, dispatches
 * the message to either the client or server.
 */
static void
mir_do_rput(queue_t *q, mblk_t *mp, int srv)
{
	mblk_t	*cont_mp;
	int	excess;
	i32	frag_len;
	i32	frag_header;
	mblk_t	*head_mp;
	int	len;
	mir_t	*mir;
	mblk_t	*mp1;
	unsigned char	*rptr;
	mblk_t	*tail_mp;
	unsigned char	*wptr;
	mblk_t	*putnext_head = NULL;
	mblk_t	*putnext_tail = NULL;

	mir = (mir_t *)q->q_ptr;
	ASSERT(mir != NULL);

	/*
	 * If the stream has not been set up as a RPC_CLIENT, RPC_SERVER
	 * or RPC_TEST with the corresponding ioctl, then don't accept
	 * any inbound data.  This should never happen for streams
	 * created by nfsd or client-side KRPC because they are careful
	 * to set the mode of the stream before doing anything else.
	 */
	if (mir->mir_type == 0) {
		freemsg(mp);
		return;
	}

	ASSERT(MIR_LOCK_NOT_HELD(mir));

	switch (mp->b_datap->db_type) {
	case M_DATA:
		break;
	case M_PROTO:
	case M_PCPROTO:
		rptr = mp->b_rptr;
		if (mp->b_wptr - rptr < sizeof (u_long)) {
			RPCLOG(1, "mir_rput: runt TPI message (%d bytes)\n",
				mp->b_wptr - rptr);
			freemsg(mp);
			return;
		}
		if (((union T_primitives *)rptr)->type != T_DATA_IND) {
			mir_rput_proto(q, mp);
			return;
		}

		/* Throw away the T_DATA_IND block and continue with data. */
		mp1 = mp;
		mp = mp->b_cont;
		freeb(mp1);
		break;
	case M_SETOPTS:
		/*
		 * If a module on the stream is trying set the Stream head's
		 * high water mark, then set our hiwater to the requested
		 * value.  We are the "stream head" for all inbound
		 * data messages since messages are passed directly to KRPC.
		 */
		if ((mp->b_wptr - mp->b_rptr) >= sizeof (struct stroptions)) {
			struct stroptions	*stropts;
			stropts = (struct stroptions *)mp->b_rptr;
			if ((stropts->so_flags & SO_HIWAT) &&
				!(stropts->so_flags & SO_BAND)) {
				freezestr(q);
				strqset(q, QHIWAT, 0, stropts->so_hiwat);
				unfreezestr(q);
			}
		}
		/* fallthru */;
	default:
		putnext(q, mp);
		return;
	}

	MIR_LOCK(mir);

	/*
	 * If this connection is closing, don't accept any new messages.
	 */
	if (mir->mir_ordrel_pending) {
		MIR_UNLOCK(mir);
		/*
		 * If this connection is closing, don't accept any
		 * new messages for it.
		 */
		freemsg(mp);
		return;
	}

	/*
	 * If there is a bufcall pending, then we have to wait
	 * for it to complete before processing this new message.
	 * mir_rsrv is called as the bufcall callback routine.
	 */
	if (mir->mir_awaiting_memory) {
		if (!mir->mir_first_non_processed_mblk)
			mir->mir_first_non_processed_mblk = mp;
		if (srv)
			(void) putbq(q, mp);
		else
			(void) putq(q, mp);
		mir->mir_inrservice = 1;
		MIR_UNLOCK(mir);
		return;
	}

	/* Get local copies for quicker access. */
	frag_len = mir->mir_frag_len;
	frag_header = mir->mir_frag_header;
	head_mp = mir->mir_head_mp;
	tail_mp = mir->mir_tail_mp;

	/* Loop, processing each message block in the mp chain separately. */
	do {
		/*
		 * cont_mp is used in the do/while condition below to
		 * walk to the next block in the STREAMS message.
		 * mp->b_cont may be nil'ed during processing so we
		 * can't rely on it to find the next block.
		 */
		cont_mp = mp->b_cont;

		/*
		 * Get local copies of rptr and wptr for our processing.
		 * These always point into "mp" (the current block being
		 * processed), but rptr is updated as we consume any
		 * record header in this message, and wptr is updated to
		 * point to the end of the data for the current fragment,
		 * if it ends in this block.  The main point is that
		 * they are not always the same as b_rptr and b_wptr.
		 * b_rptr and b_wptr will be updated when appropriate.
		 */
		rptr = mp->b_rptr;
		wptr = mp->b_wptr;
same_mblk:;
		len = wptr - rptr;
		if (len <= 0) {
			/*
			 * If we have processed all of the data in the message
			 * or the block is empty to begin with, then we're
			 * done with this block and can go on to cont_mp,
			 * if there is one.
			 *
			 * First, we check to see if the current block is
			 * now zero-length and, if so, we free it.
			 * This happens when either the block was empty
			 * to begin with or we consumed all of the data
			 * for the record marking header.
			 */
			if (rptr <= mp->b_rptr) {
				/*
				 * If head_mp is non-NULL, add cont_mp to the
				 * mblk list. XXX But there is a possibility
				 * that tail_mp = mp or even head_mp = mp XXX
				 */
				if (head_mp) {
					if (head_mp == mp)
						head_mp = nilp(mblk_t);
					else if (tail_mp != mp) {
		ASSERT((tail_mp->b_cont == NULL) || (tail_mp->b_cont == mp));
						tail_mp->b_cont = cont_mp;
						tail_mp = cont_mp;
					} else {
						mblk_t *smp = head_mp;

						while ((smp->b_cont != NULL) &&
							(smp->b_cont != mp))
							smp = smp->b_cont;
						smp->b_cont = cont_mp;
						tail_mp = cont_mp;
					}
				}
				freeb(mp);
			}
			continue;
		}

		/*
		 * frag_len starts at -4 and is incremented past the record
		 * marking header to 0, and then becomes positive as real data
		 * bytes are received for the message.  While frag_len is less
		 * than zero, we need more bytes for the record marking
		 * header.
		 */
		if (frag_len < 0) {
			u_char	* up = rptr;
			/*
			 * Collect as many bytes as we need for the record
			 * marking header and that are available in this block.
			 */
			do {
				--len;
				frag_len++;
				frag_header <<= 8;
				frag_header += (*up++ & 0xFF);
			} while (len > 0 && frag_len < 0);

			if (rptr == mp->b_rptr) {
				/*
				 * The record header is located at the
				 * beginning of the block, so just walk
				 * b_rptr past it.
				 */
				mp->b_rptr = rptr = up;
			} else {
				/*
				 * The record header is located in the middle
				 * of a block, so copy any remaining data up.
				 * This happens when an RPC message is
				 * fragmented into multiple pieces and
				 * a middle (or end) fragment immediately
				 * follows a previous fragment in the same
				 * message block.
				 */
				wptr = &rptr[len];
				mp->b_wptr = wptr;
				if (len)
					bcopy((char *)up, (char *)rptr, len);
			}

			/*
			 * If we haven't received the complete record header
			 * yet, then loop around to get the next block in the
			 * STREAMS message. The logic at same_mblk label will
			 * free the current block if it has become empty.
			 */
			if (frag_len < 0)
				goto same_mblk;

			/*
			 * At this point we have retrieved the complete record
			 * header for this fragment.  If the current block is
			 * empty, then we need to free it and walk to the next
			 * block.
			 */
			if (mp->b_rptr >= wptr) {
				/*
				 * If this is not the last fragment or if we
				 * have not received all the data for this
				 * RPC message, then loop around to the next
				 * block.
				 */
				if (!(frag_header & MIR_LASTFRAG) ||
					(frag_len -
					(frag_header & ~MIR_LASTFRAG)) ||
					!head_mp)
					goto same_mblk;

				/*
				 * Quick walk to next block in the
				 * STREAMS message.
				 */
				tail_mp->b_cont = cont_mp;
				freeb(mp);
				mp = tail_mp;
			}
		}

		/*
		 * We've collected the complete record header.  The data
		 * in the current block is added to the end of the RPC
		 * message.  Note that tail_mp is the same as mp after
		 * this linkage.
		 */
		if (!head_mp)
			head_mp = mp;
		else if (tail_mp != mp) {
		ASSERT((tail_mp->b_cont == NULL) || (tail_mp->b_cont == mp));
			tail_mp->b_cont = mp;
		}
		tail_mp = mp;

		/*
		 * Add the length of this block to the accumulated
		 * fragment length.
		 */
		frag_len += len;
		excess = frag_len - (frag_header & ~MIR_LASTFRAG);
		/*
		 * If we have not received all the data for this fragment,
		 * then walk to the next block.
		 */
		if (excess < 0)
			continue;

		/*
		 * We've received a complete fragment, so reset frag_len
		 * for the next one.
		 */
		frag_len = -sizeof (u32);

		/*
		 * Update rptr to point to the beginning of the next
		 * fragment in this block.  If there are no more bytes
		 * in the block (excess is 0), then rptr will be equal
		 * to wptr.
		 */
		rptr = wptr - excess;

		/*
		 * Now we check to see if this fragment is the last one in
		 * the RPC message.
		 */
		if (!(frag_header & MIR_LASTFRAG)) {
			/*
			 * This isn't the last one, so start processing the
			 * next fragment.
			 */
			frag_header = 0;

			/*
			 * If excess is 0, the next fragment
			 * starts at the beginning of the next block --
			 * we "continue" to the end of the while loop and
			 * walk to cont_mp.
			 */
			if (excess == 0)
				continue;

			/*
			 * If excess is non-0, then the next fragment starts
			 * in this block.  rptr points to the beginning
			 * of the next fragment and we "goto same_mblk"
			 * to continue processing.
			 */
			goto same_mblk;
		}

		/*
		 * We've got a complete RPC message.  Before passing it
		 * upstream, check to see if there is extra data in this
		 * message block. If so, then we separate the excess
		 * from the complete message. The excess data is processed
		 * after the current message goes upstream.
		 */
		if (excess > 0) {
			/* Duplicate only the overlapping block. */
			mp1 = dupb(tail_mp);

			/*
			 * dupb() might have failed due to ref count wrap around
			 * so try a copyb().
			 */
			if (mp1 == NULL)
				mp1 = copyb(tail_mp);

			if (mp1 == NULL) {
				mir->mir_excess = excess;
				mir->mir_awaiting_memory = 1;
					RPCLOG(1, "mir_rput: dupb failed\n", 0);
				mir->mir_bufcall_id = bufcall(1, BPRI_MED,
				mir_bufcall_callback, (long)q);

				/*
				 * If bufcall fails, then set a timer to
				 * call mir_rsrv instead.
				 */
				if (mir->mir_bufcall_id == 0) {
					mi_timer(q, mir->mir_timer_mp,
						MIR_BUFCALL_TIMEOUT);
				}

				frag_header = 0;
				break;
			}

			/*
			 * The new message block is linked with the
			 * continuation block in cont_mp.  We then point
			 * cont_mp to the new block so that we will
			 * process it next.
			 */
			mp1->b_cont = cont_mp;
			cont_mp = mp1;
			/*
			 * Data in the new block begins at the
			 * next fragment (rptr).
			 */
			cont_mp->b_rptr += (rptr - tail_mp->b_rptr);
			ASSERT(cont_mp->b_rptr >= cont_mp->b_datap->db_base);
			ASSERT(cont_mp->b_rptr <= cont_mp->b_wptr);

			/* Data in the current fragment ends at rptr. */
			tail_mp->b_wptr = rptr;
			ASSERT(tail_mp->b_wptr <= tail_mp->b_datap->db_lim);
			ASSERT(tail_mp->b_wptr >= tail_mp->b_rptr);

		}

		/* tail_mp is the last block with data for this RPC message. */
		tail_mp->b_cont = nilp(mblk_t);

		/* Pass the RPC message to the current consumer. */
		switch (mir->mir_type) {
		case RPC_CLIENT:
			if (clnt_dispatch_notify(head_mp)) {
				/*
				 * Mark this stream as active.  This marker
				 * is used in mir_timer().
				 */

				mir->mir_req_cnt = 1;
				mir->mir_use_timestamp = lbolt;
			} else
				freemsg(head_mp);
			break;

		case RPC_SERVER:
			/*
			 * Check for flow control before passing the
			 * message to KRPC.
			 */

			if (!mir->mir_hold_inbound) {
				svc_queuereq(q, head_mp);	/* to KRPC */
				/*
				 * If the request count is equal to the reply
				 * count, then the stream is transitioning
				 * from idle to non-idle.  In this case,
				 * we cancel the idle timer.
				 */
				if (mir->mir_req_cnt == mir->mir_reply_cnt)
					mi_timer(WR(q), mir->mir_timer_mp, -1);
				/* Increment the request count. */
				mir->mir_req_cnt++;

			} else {
				/*
				 * If the outbound side of the stream is
				 * flow controlled, then hold this message
				 * until client catches up. mir_hold_inbound
				 * is set in mir_wput and cleared in mir_wsrv.
				 */
				if (srv)
					(void) putbq(q, head_mp);
				else
					(void) putq(q, head_mp);
				mir->mir_inrservice = 1;
			}
			break;

		case RPC_TEST:
			if (canputnext(q)) {
				ADD_PUTNEXT(putnext_head, putnext_tail,
					head_mp);
			} else {
				if (srv)
					(void) putbq(q, head_mp);
				else
					(void) putq(q, head_mp);
				mir->mir_inrservice = 1;
			}
			break;
		default:
			RPCLOG(1, "mir_rput: unknown mir_type %d\n",
				mir->mir_type);
			freemsg(head_mp);
			break;
		}

		/*
		 * Reset head_mp and frag_header since we're starting on a
		 * new RPC fragment and message.
		 */
		head_mp = nilp(mblk_t);
		frag_header = 0;
	} while ((mp = cont_mp) != NULL);

	/*
	 * Do a sanity check on the message length.  If this message is
	 * getting excessively large, shut down the connection.
	 */
	if (frag_len > 0 && mir->mir_max_msg_sizep &&
	    frag_len >= *mir->mir_max_msg_sizep) {
		freemsg(head_mp);
		mir->mir_head_mp = (mblk_t *)0;
		mir->mir_frag_len = -sizeof (u32);

		cmn_err(CE_NOTE,
		    "KRPC: record fragment from %s of size(%d) exceeds "
		    "maximum (%lu). Disconnecting",

			(mir->mir_type == RPC_CLIENT) ? "server"
			: (mir->mir_type == RPC_SERVER) ? "client"
			: "test tool",

			frag_len, *mir->mir_max_msg_sizep);

		switch (mir->mir_type) {
		case RPC_CLIENT:
			/*
			 * We are disconnecting, but not necessarily
			 * closing. By not closing, we will fail to
			 * pick up a possibly changed global timeout value,
			 * unless we store it now.
			 */
			mir->mir_idle_timeout = clnt_idle_timeout;
			mi_timer(WR(q), mir->mir_timer_mp, -1);
			mi_timer(WR(q), mir->mir_timer_mp,
				mir->mir_idle_timeout);
			MIR_UNLOCK(mir);
			/*
			 * T_DISCON_REQ is passed to KRPC as an integer value
			 * (this is not a TPI message).  It is used as a
			 * convenient value to indicate a sanity check
			 * failure -- the same KRPC routine is also called
			 * for T_DISCON_INDs and T_ORDREL_INDs.
			 */

			clnt_dispatch_notifyall(q, T_DISCON_REQ);
			break;
		case RPC_SERVER:
			MIR_UNLOCK(mir);
			(void) mir_svc_policy_notify(RD(q), 2);
			break;
		case RPC_TEST:
			MIR_UNLOCK(mir);
			SEND_PUTNEXT(q, putnext_head);
			break;
		default:
			MIR_UNLOCK(mir);
			break;
		}
		return;
	}

	/* Save our local copies back in the mir structure. */
	mir->mir_frag_header = frag_header;
	mir->mir_frag_len = frag_len;
	mir->mir_head_mp = head_mp;
	mir->mir_tail_mp = tail_mp;
	MIR_UNLOCK(mir);
	SEND_PUTNEXT(q, putnext_head);
}

static void
mir_rput(queue_t *q, mblk_t *mp)
{
	mir_do_rput(q, mp, 0);
}

static void
mir_rput_proto(queue_t *q, mblk_t *mp)
{
	mir_t	*mir = (mir_t *)q->q_ptr;
	u_long	type;

	ASSERT(MIR_LOCK_NOT_HELD(mir));

	type = ((union T_primitives *)mp->b_rptr)->type;
	switch (mir->mir_type) {
	case RPC_CLIENT:
		switch (type) {
		case T_DISCON_IND:
		case T_ORDREL_IND:
			MIR_LOCK(mir);
			if (mir->mir_head_mp) {
				freemsg(mir->mir_head_mp);
				mir->mir_head_mp = (mblk_t *)0;
			}
			/*
			 * We are disconnecting, but not necessarily
			 * closing. By not closing, we will fail to
			 * pick up a possibly changed global timeout value,
			 * unless we store it now.
			 */
			mir->mir_idle_timeout = clnt_idle_timeout;
			mi_timer(WR(q), mir->mir_timer_mp, -1);

			/*
			 * Even though we are unconnected, we still
			 * leave the idle timer going on the client. The
			 * reason for is is that if we've disconnected due
			 * to a server-side disconnect, reset, or connection
			 * timeout, there is a possibility the client may
			 * retry the RPC request. This retry needs to done on
			 * the same bound address for the server to interpret
			 * it as such. However, we don't want
			 * to wait forever for that possibility. If the
			 * end-point stays unconnected for mir_idle_timeout
			 * units of time, then that is a signal to the
			 * connection manager to give up waiting for the
			 * application (eg. NFS) to send a retry.
			 */
			mi_timer(WR(q), mir->mir_timer_mp,
				mir->mir_idle_timeout);
			MIR_UNLOCK(mir);
			clnt_dispatch_notifyall(WR(q), type);
			freemsg(mp);
			return;
		case T_ERROR_ACK:
#ifdef RPCDEBUG
			{
			struct T_error_ack	*terror;

			terror = (struct T_error_ack *)mp->b_rptr;
			RPCLOG(1, "type: %d", terror->PRIM_type);
			RPCLOG(1, " ERROR_prim: %d", terror->ERROR_prim);
			RPCLOG(1, " TLI_error: %d", terror->TLI_error);
			RPCLOG(1, " UNIX_error%d\n", terror->UNIX_error);
			}
#endif
			/*FALLTHROUGH*/
		case T_CONN_CON:
		case T_INFO_ACK:
		case T_OK_ACK:
		case T_OPTMGMT_ACK:
			if (clnt_dispatch_notifyconn(WR(q), mp))
				return;
			break;
		case T_BIND_ACK:
			break;
		default:
			RPCLOG(1,
			"mir_rput: unexpected message %d for KRPC client\n",
				((union T_primitives *)mp->b_rptr)->type);
			break;
		}
		break;

	case RPC_SERVER:
		switch (type) {
		case T_BIND_ACK:
		{
			struct T_bind_ack	*tbind;

			/*
			 * If this is a listening stream, then shut
			 * off the idle timer.
			 */
			tbind = (struct T_bind_ack *)mp->b_rptr;
			if (tbind->CONIND_number > 0) {
				MIR_LOCK(mir);
				mi_timer(WR(q), mir->mir_timer_mp, -1);
				MIR_UNLOCK(mir);
			}
			break;
		}
#ifdef T_ADDR_ACK
		case T_ADDR_ACK:
			svc_cots_addr_ack(q, mp);
			return;
#endif
		default:
			/* nfsd handles server-side non-data messages. */
			break;
		}
		break;

	default:
		break;
	}

	putnext(q, mp);
}

/*
 * The server-side read queues are used to hold inbound messages while
 * outbound flow control is exerted.  When outbound flow control is
 * relieved, mir_wsrv qenables the read-side queue.  Read-side queues
 * are not enabled by STREAMS and are explicitly noenable'ed in mir_open.
 *
 * The read-side queue is also enabled by a bufcall callback if dupmsg
 * fails in mir_rput.
 */
static void
mir_rsrv(queue_t *q)
{
	mir_t	*mir;
	mblk_t	*mp;
	mblk_t	*putnext_head = NULL;
	mblk_t	*putnext_tail = NULL;
	int	bid = 0;

	mir = (mir_t *)q->q_ptr;
	MIR_LOCK(mir);

	mp = nilp(mblk_t);
	switch (mir->mir_type) {
	case RPC_SERVER:
		if (mir->mir_req_cnt == mir->mir_reply_cnt)
			mir->mir_hold_inbound = 0;
		if (mir->mir_hold_inbound) {
			if (q->q_first == NULL)
				mir->mir_inrservice = 0;
			MIR_UNLOCK(mir);
			return;
		}
		while (mp = getq(q)) {
			if (mp == mir->mir_first_non_processed_mblk) {
				(void) putbq(q, mp);
				break;
			}
			svc_queuereq(q, mp);
			if (mir->mir_req_cnt++ == mir->mir_reply_cnt)
				mi_timer(WR(q), mir->mir_timer_mp, -1);
		}
		break;
	case RPC_TEST:
		while (mp = getq(q)) {
			if (mp == mir->mir_first_non_processed_mblk) {
				(void) putbq(q, mp);
				break;
			}
			if (!canputnext(q)) {
				(void) putbq(q, mp);
				MIR_UNLOCK(mir);
				return;
			}
			ADD_PUTNEXT(putnext_head, putnext_tail, mp);
		}
		break;
	case RPC_CLIENT:
		break;
	default:
		RPCLOG(1, "mir_rsrv: unexpected mir_type %d\n", mir->mir_type);

		if (q->q_first == NULL)
			mir->mir_inrservice = 0;
		MIR_UNLOCK(mir);
		return;
	}

	if (mir->mir_awaiting_memory == 0 && q->q_first == NULL) {
		mir->mir_inrservice = 0;
		MIR_UNLOCK(mir);
		SEND_PUTNEXT(q, putnext_head)
		return;
	}

	/*
	 * If there was a bufcall pending and there is a fragment being
	 * collected, clear the bufcall id and pass the fragment back
	 * through mir_rput.
	 */
	{
		mblk_t *tail_mp = mir->mir_tail_mp;
		unsigned char	*rptr = tail_mp->b_wptr - mir->mir_excess;
		mblk_t *cont_mp;
		mblk_t *head_mp;

		cont_mp = dupb(tail_mp);
		if (cont_mp == NULL)
			cont_mp = copyb(tail_mp);
		if (cont_mp == NULL) {
			/*
			 * If bufcall fails, then set a timer to
			 * call mir_rsrv instead.
			 */
			if (!mir->mir_bufcall_id &&
			    !(mir->mir_bufcall_id = bufcall(1, BPRI_MED,
			    mir_bufcall_callback, (long)q))) {
				mi_timer(q, mir->mir_timer_mp,
					MIR_BUFCALL_TIMEOUT);
			}

			MIR_UNLOCK(mir);
			SEND_PUTNEXT(q, putnext_head)
			return;
		}

		bid = mir->mir_bufcall_id;
		mir->mir_bufcall_id = 0;

		tail_mp->b_wptr = rptr;
		ASSERT(tail_mp->b_wptr <= tail_mp->b_datap->db_lim);
		ASSERT(tail_mp->b_wptr >= tail_mp->b_rptr);

		cont_mp->b_rptr += (rptr - tail_mp->b_rptr);
		ASSERT(cont_mp->b_rptr >= cont_mp->b_datap->db_base);
		ASSERT(cont_mp->b_rptr <= cont_mp->b_wptr);

		cont_mp->b_cont = tail_mp->b_cont;
		tail_mp->b_cont = nilp(mblk_t);

		mir->mir_awaiting_memory = 0;
		mir->mir_first_non_processed_mblk = nilp(mblk_t);

		head_mp = mir->mir_head_mp;
		mir->mir_head_mp = nilp(mblk_t);
		switch (mir->mir_type) {
		case RPC_CLIENT:
			if (clnt_dispatch_notify(head_mp)) {
				mir->mir_req_cnt = 1;
				mir->mir_use_timestamp = lbolt;
			} else
				freemsg(head_mp);
			break;
		case RPC_SERVER:
			svc_queuereq(q, head_mp);
			if (mir->mir_req_cnt++ == mir->mir_reply_cnt)
				mi_timer(WR(q), mir->mir_timer_mp, -1);
			break;
		case RPC_TEST:
			ADD_PUTNEXT(putnext_head, putnext_tail, head_mp);
			break;
		}
		mp = cont_mp;
	}

	MIR_UNLOCK(mir);
	SEND_PUTNEXT(q, putnext_head);
	MIR_LOCK(mir);

	do {
		mir->mir_first_non_processed_mblk = q->q_first;
		MIR_UNLOCK(mir);
		mir_do_rput(q, mp, 1);
		MIR_LOCK(mir);
		if (mir->mir_awaiting_memory)
			break;
	} while (mp = getq(q));

	if (q->q_first == NULL)
		mir->mir_inrservice = 0;
	MIR_UNLOCK(mir);
	if (bid)
		unbufcall(bid);
}

#if 0
/*
 * XXX -
 */
/* ARGSUSED */
static int
mir_set_idle_timeout(queue_t *q, mblk_t *mp, char *value, caddr_t arg)
{
	mir_t	*mir = (mir_t *)q->q_ptr;
	char	*cp;

	mir->mir_idle_timeout = mi_strtol(value, &cp, 0);
	return (0);
}
#endif

static int
mir_svc_policy_notify(queue_t *q, int event)
{
	mblk_t	*mp;

	ASSERT(MIR_LOCK_NOT_HELD((mir_t *)q->q_ptr));

	/*
	 * Create an M_DATA message with the event code and pass it to the
	 * Stream head (nfsd or whoever created the stream will consume it).
	 */
	mp = allocb(sizeof (int), BPRI_HI);
	if (!mp)
		return (ENOMEM);
	U32_TO_BE32(event, mp->b_rptr);
	mp->b_wptr = mp->b_rptr + sizeof (int);
	putnext(q, mp);
	return (0);
}

/*
 * This routine is called directly by KRPC after a request is completed,
 * whether a reply was sent or the request was dropped.
 */
static void
mir_svc_release(queue_t *wq, mblk_t *mp)
{
	mir_t   *mir = (mir_t *)wq->q_ptr;

	if (mp)
		freemsg(mp);

	mutex_enter(&mir->mir_mutex);
	mir->mir_rele_cnt++;

	/*
	 * If this release is for the last inbound request seen
	 * and we have not seen a reply for that request,
	 * then we need to get the idle timer restarted.  In this
	 * case, we enable the write-side queue and let mir_wsrv
	 * fix mir_reply_cnt and start the timer.
	 */
	if ((mir->mir_rele_cnt == mir->mir_req_cnt &&
		mir->mir_rele_cnt != mir->mir_reply_cnt) ||
		mir->mir_closing) {
		qenable(wq);
	}

	mutex_exit(&mir->mir_mutex);
}

/*
 * This routine is called by server-side KRPC when it is ready to
 * handle inbound messages on the stream.
 */
static void
mir_svc_start(queue_t *wq)
{
	qenable(RD(wq));
}

/*
 * client side only. Forces rpcmod to stop sending T_ORDREL_REQs on
 * end-points that aren't connected.
 */
static void
mir_clnt_stop_idle(queue_t *wq)
{
	mir_t   *mir = (mir_t *)wq->q_ptr;

	ASSERT(MIR_LOCK_NOT_HELD(mir));
	MIR_LOCK(mir);
	mi_timer(wq, mir->mir_timer_mp, -1);
	MIR_UNLOCK(mir);
}

/* Called by mir_wput/mir_wsrv to handle timer events. */
static void
mir_timer(queue_t *wq, mir_t *mir, mblk_t *mp)
{
	ASSERT(MIR_LOCK_HELD(mir));

	switch (mir->mir_type) {
	case RPC_CLIENT:
		/*
		 * For clients, the timer fires at clnt_idle_timeout
		 * intervals.  If the activity marker (mir_req_cnt) is
		 * zero, then the stream has been idle since the last
		 * timer event and we notify KRPC.  If mir_req_cnt is
		 * non-zero, then the stream is active and we just
		 * restart the timer for another interval.  mir_req_cnt
		 * is set to 1 in mir_wput for every request passed
		 * downstream.
		 *
		 * The timer is initially started in mir_wput during
		 * RPC_CLIENT ioctl processing.
		 *
		 * The timer interval can be changed for individual
		 * streams with the ND variable "mir_idle_timeout".
		 */
		if (mir->mir_req_cnt > 0 &&
		    mir->mir_use_timestamp + MILLISEC2HZ(mir->mir_idle_timeout)
		    >= lbolt) {
			long tout;

			tout = mir->mir_idle_timeout -
				HZ2MILLISEC(lbolt - mir->mir_use_timestamp);
			if (tout < 0)
				tout = 1000;
#if 0
printf("mir_timer[%d < %d + %d]: reset client timer to %d (ms)\n",
HZ2MILLISEC(lbolt), HZ2MILLISEC(mir->mir_use_timestamp), mir->mir_idle_timeout,
tout);
#endif
			mir->mir_req_cnt = 0;
			mi_timer(wq, mp, tout);
			MIR_UNLOCK(mir);
		} else {
#if 0
printf("mir_timer[%d]: doing client timeout\n", lbolt / HZ);
#endif
			/*
			 * We are disconnecting, but not necessarily
			 * closing. By not closing, we will fail to
			 * pick up a possibly changed global timeout value,
			 * unless we store it now.
			 */
			mir->mir_idle_timeout = clnt_idle_timeout;
			mi_timer(wq, mp, -1);
			mi_timer(wq, mp, mir->mir_idle_timeout);
			MIR_UNLOCK(mir);
			/*
			 * We pass T_ORDREL_REQ as an integer value
			 * to KRPC as the indication that the stream
			 * is idle.  This is not a T_ORDREL_REQ message,
			 * it is just a convenient value since we call
			 * the same KRPC routine for T_ORDREL_INDs and
			 * T_DISCON_INDs.
			 */
			clnt_dispatch_notifyall(wq, T_ORDREL_REQ);
		}
		return;

	case RPC_SERVER:
		/*
		 * For servers, the timer is only running when the stream
		 * is really idle.  The timer is started by mir_wput when
		 * mir_type is set to RPC_SERVER and by mir_idle_start
		 * whenever the stream goes idle (mir_req_cnt ==
		 * mir_reply_cnt).  The timer is cancelled in mir_rput
		 * whenever a new inbound request is passed to KRPC
		 * and the stream was previously idle.
		 *
		 * The timer interval can be changed for individual
		 * streams with the ND variable "mir_idle_timeout".
		 */
		if (mir->mir_reply_cnt == mir->mir_req_cnt) {
			MIR_UNLOCK(mir);
			if (mir_svc_policy_notify(RD(wq), 1) == 0)
				return;
		} else {
			MIR_UNLOCK(mir);
		}
		return;
	case RPC_TEST:
		MIR_UNLOCK(mir);
		return;
	default:
		RPCLOG(1, "mir_timer: unexpected mir_type %d\n",
			mir->mir_type);
		MIR_UNLOCK(mir);
		return;
	}
}


/*
 * Called by the RPC package to send either a call or a return, or a
 * transport connection request.  Adds the record marking header.
 */
static void
mir_wput(queue_t *q, mblk_t *mp)
{
	u_int	frag_header;
	mir_t	*mir = (mir_t *)q->q_ptr;
	mblk_t	*mp1;
	u_char	*rptr = mp->b_rptr;
	int	rw_ret;

	if (!mir) {
		ASSERT(mir != NULL);
		freemsg(mp);
		return;
	}

	if (mp->b_datap->db_type != M_DATA) {
		mir_wput_other(q, mp);
		return;
	}

	frag_header = DLEN(mp);
	frag_header |= MIR_LASTFRAG;

	/* Stick in the 4 byte record marking header. */
	if ((rptr - mp->b_datap->db_base) < sizeof (u32)) {
		/*
		 * Since we know that M_DATA messages are created exclusively
		 * by KRPC, we expect that KRPC will leave room for our header.
		 * If KRPC (or someone else) does not cooperate, then we
		 * just throw away the message.
		 */
		RPCLOG(1,
	"mir_wput: KRPC did not leave space for record fragment header\n", 0);
		freemsg(mp);
		return;
	}
	rptr -= sizeof (u32);
	U32_TO_BE32(frag_header, rptr);
	mp->b_rptr = rptr;

	MIR_LOCK(mir);
	/*
	 * If we have already queued some data or the downstream module
	 * cannot accept any more at this time, then we queue the message
	 * and take other actions depending on mir_type.
	 */
	if (mir->mir_inwservice || !canputnext(q)) {
		rw_ret = 0;
	} else {
		rw_ret = (mp->b_cont) ? MIR_TRY_SEND_LOCK(mir, RW_WRITER) :
			MIR_TRY_SEND_LOCK(mir, RW_READER);
	}

	if (rw_ret == 0) {
		switch (mir->mir_type) {
		case RPC_CLIENT:
			/*
			 * Check for a previous duplicate request on the
			 * queue.  If there is one, then we throw away
			 * the current message and let the previous one
			 * go through.  If we can't find a duplicate, then
			 * send this one.  This tap dance is an effort
			 * to reduce traffic and processing requirements
			 * under load conditions.
			 */
			if (mir_clnt_dup_request(q, mp)) {
				MIR_UNLOCK(mir);
				freemsg(mp);
				return;
			}
			break;
		case RPC_SERVER:
			/*
			 * Set mir_hold_inbound so that new inbound RPC
			 * messages will be held until the client catches
			 * up on the earlier replies.  This flag is cleared
			 * in mir_wsrv after flow control is relieved;
			 * the read-side queue is also enabled at that time.
			 */
			mir->mir_hold_inbound = 1;
			break;
		case RPC_TEST:
			break;
		default:
			RPCLOG(1, "mir_wput: unexpected mir_type %d\n",
				mir->mir_type);
			break;
		}
		mir->mir_inwservice = 1;
		(void) putq(q, mp);
		MIR_UNLOCK(mir);
		return;
	}


	switch (mir->mir_type) {
	case RPC_CLIENT:
		/*
		 * For the client, set mir_req_cnt to indicate that the
		 * connection is active.
		 */
		mir->mir_req_cnt = 1;
		break;

	case RPC_SERVER:
		/*
		 * The call to mir_idle_start must happen after the
		 * RPC message is passed downstream in case there is
		 * a pended T_ORDREL_REQ.
		 */
		if (++mir->mir_reply_cnt == mir->mir_req_cnt)
			mir_idle_start(q, mir);
		break;
	case RPC_TEST:
	default:
		break;
	}
	MIR_UNLOCK(mir);

	/*
	 * Now we pass the RPC message downstream.  We pull apart the
	 * chain of message blocks and pass each down individually.
	 * KRPC creates each block as a TIDU and then links them all
	 * together to make the RPC message.
	 *
	 * Note that we only check flow control once for the whole
	 * message.  If we can pass any of it downstream, it all goes.
	 * This ensures that no other data gets intermixed with this
	 * RPC message (intermixing shouldn't happen anyway, but this
	 * is an extra measure of safety).
	 */
	do {
		mp1 = mp->b_cont;
		mp->b_cont = (mblk_t *)0;
		putnext(q, mp);
	} while ((mp = mp1) != NULL);
	MIR_SEND_UNLOCK(mir);
}

static void
mir_wput_other(queue_t *q, mblk_t *mp)
{
	mir_t	*mir = (mir_t *)q->q_ptr;
	struct iocblk	*iocp;
	u_char	*rptr = mp->b_rptr;

	ASSERT(MIR_LOCK_NOT_HELD(mir));
	switch (mp->b_datap->db_type) {
	case M_PCSIG:
		MIR_LOCK(mir);
		/* Check for an idle timer firing. */
		if (mp != mir->mir_timer_mp)
			break;
		if (mi_timer_valid(mp)) {
			mir_timer(q, mir, mp);
			ASSERT(MIR_LOCK_NOT_HELD(mir));
		} else
			MIR_UNLOCK(mir);
		return;
	case M_IOCTL:
		iocp = (struct iocblk *)rptr;
		switch (iocp->ioc_cmd) {
		case RPC_CLIENT:
			MIR_LOCK(mir);
			if (mir->mir_type != 0 &&
			    mir->mir_type != iocp->ioc_cmd) {
ioc_eperm:
				MIR_UNLOCK(mir);
				iocp->ioc_error = EPERM;
				iocp->ioc_count = 0;
				mp->b_datap->db_type = M_IOCACK;
				qreply(q, mp);
				return;
			}

			mir->mir_type = iocp->ioc_cmd;

			/*
			 * Clear mir_hold_inbound which was set to 1 by
			 * mir_open.  This flag is not used on client
			 * streams.
			 */
			mir->mir_hold_inbound = 0;
			mir->mir_max_msg_sizep = &clnt_max_msg_size;

			/*
			 * Start the idle timer.  See mir_timer() for more
			 * information on how client timers work.
			 */
			mir->mir_idle_timeout = clnt_idle_timeout;
			mi_timer(q, mir->mir_timer_mp, mir->mir_idle_timeout);
			MIR_UNLOCK(mir);

			mp->b_datap->db_type = M_IOCACK;
			qreply(q, mp);
			return;
		case RPC_SERVER:
			MIR_LOCK(mir);
			if (mir->mir_type != 0 &&
			    mir->mir_type != iocp->ioc_cmd)
				goto ioc_eperm;

			mir->mir_type = iocp->ioc_cmd;
			mir->mir_max_msg_sizep = &svc_max_msg_size;

			/*
			 * Start the idle timer.  See mir_timer() for more
			 * information on how server timers work.
			 *
			 * Note that it is important to start the idle timer
			 * here so that connections time out even if we
			 * never receive any data on them.
			 */
			mir->mir_idle_timeout = svc_idle_timeout;
			mi_timer(q, mir->mir_timer_mp, mir->mir_idle_timeout);
			MIR_UNLOCK(mir);

			mp->b_datap->db_type = M_IOCACK;
			qreply(q, mp);
			return;

		case RPC_TEST:
			MIR_LOCK(mir);
			if (mir->mir_type != 0 &&
			    mir->mir_type != iocp->ioc_cmd)
				goto ioc_eperm;

			mir->mir_type = iocp->ioc_cmd;
			mir->mir_max_msg_sizep = &svc_max_msg_size;
			MIR_UNLOCK(mir);
			freezestr(q);
			if (strqset(q, QMAXPSZ, 0, RPC_MAXDATASIZE) != 0) {
				/*EMPTY*/
				RPCLOG(1, "mir_wput: strqset failed\n", 0);
			}
			unfreezestr(q);

			mp->b_datap->db_type = M_IOCACK;
			qreply(q, mp);
			return;

		default:
			break;
		}
		break;
	case M_PROTO:
		if ((mp->b_wptr - rptr) < sizeof (long) ||
			!OK_LONG_PTR(rptr))
			break;
		switch (((union T_primitives *)rptr)->type) {
		case T_DATA_REQ:
			/* Don't pass T_DATA_REQ messages downstream. */
			freemsg(mp);
			return;
		case T_ORDREL_REQ:
			MIR_LOCK(mir);
			if (mir->mir_type != RPC_SERVER) {
				/*
				 * We are likely being called from
				 * clnt_dispatch_notifyall(). Sending
				 * a T_ORDREL_REQ will result in
				 * a some kind of _IND message being sent,
				 * will be another call to
				 * clnt_dispatch_notifyall(). To keep the stack
				 * lean, queue this message.
				 */
				mir->mir_inwservice = 1;
				(void) putq(q, mp);
				MIR_UNLOCK(mir);
				return;
			}

			/*
			 * If the stream is not idle, then we hold the
			 * orderly release until it becomes idle.  This
			 * ensures that KRPC will be able to reply to
			 * all requests that we have passed to it.  See
			 * mir_idle_start() for how this is handled later.
			 */
			if (mir->mir_req_cnt != mir->mir_reply_cnt) {
				if (mir->mir_ordrel_mp)
					freemsg(mp);
				else
					mir->mir_ordrel_mp = mp;
				MIR_UNLOCK(mir);
				return;
			}

			/*
			 * Mark the structure so that we do not accept
			 * new inbound data.
			 */
			mir->mir_ordrel_pending = 1;
			MIR_UNLOCK(mir);
			break;
		case T_CONN_REQ:
			MIR_LOCK(mir);
			/*
			 * Restart timer in case mir_clnt_stop_idle() was
			 * called.
			 */
			mir->mir_idle_timeout = clnt_idle_timeout;
			mi_timer(q, mir->mir_timer_mp, -1);
			mi_timer(q, mir->mir_timer_mp,
				mir->mir_idle_timeout);
			MIR_UNLOCK(mir);

		default:
			break;
		}
		/* fallthru */;
	default:
		if (mp->b_datap->db_type >= QPCTL)
			break;

		MIR_LOCK(mir);
		if (mir->mir_inwservice == 0 && canputnext(q)) {
			MIR_UNLOCK(mir);
			break;
		}
		mir->mir_inwservice = 1;
		(void) putq(q, mp);
		MIR_UNLOCK(mir);

		return;
	}
	putnext(q, mp);
}

static void
mir_wsrv(queue_t *q)
{
	mblk_t	*mp;
	mblk_t	*mp1;
	mir_t	*mir;
	krw_t rw = RW_READER;

	mir = (mir_t *)q->q_ptr;
	MIR_SEND_LOCK(mir, rw);
	MIR_LOCK(mir);
	while (mp = getq(q)) {
		/* Check for an idle timer firing. */
		if (mp->b_datap->db_type == M_PCSIG &&
		    mp == mir->mir_timer_mp) {
			if (mi_timer_valid(mp)) {
				mir_timer(q, mir, mp);
				ASSERT(MIR_LOCK_NOT_HELD(mir));
				MIR_LOCK(mir);
			}
			continue;
		}

		/* Make sure that the stream can really handle more data. */
		if (!canputnext(q)) {
			(void) putbq(q, mp);
			MIR_UNLOCK(mir);
			MIR_SEND_UNLOCK(mir);
			return;
		}


		/*
		 * If this is not an RPC message, then pass it downstream
		 * without carving it up.
		 */
		if (mp->b_datap->db_type != M_DATA) {
			MIR_UNLOCK(mir);
			putnext(q, mp);
			MIR_LOCK(mir);
			continue;
		}

		MIR_UNLOCK(mir);
		/*
		 * If this message has more than one mblk, then
		 * because each one is TIDU-sized, we have to break it
		 * up. We need to guarantee that other mblk's aren't
		 * injected into the stream.
		 */
		if (mp->b_cont && rw == RW_READER) {
			MIR_SEND_UNLOCK(mir);
			rw = RW_WRITER;
			MIR_SEND_LOCK(mir, rw);
		}


		/*
		 * Now we pass the RPC message downstream.  We pull apart the
		 * chain of message blocks and pass each down individually.
		 * KRPC creates each block as a TIDU and then links them all
		 * together to make the RPC message.
		 *
		 * Note that we only check flow control once for the whole
		 * message.  If we can pass any of it downstream, it all goes.
		 * This ensures that no other data gets intermixed with this
		 * RPC message (intermixing shouldn't happen anyway, but this
		 * is an extra measure of safety).
		 */
		do {
			mp1 = mp->b_cont;
			mp->b_cont = (mblk_t *)0;
			putnext(q, mp);
		} while ((mp = mp1) != NULL);
		MIR_LOCK(mir);
	}
	MIR_SEND_UNLOCK(mir);


	if (mir->mir_type != RPC_SERVER) {
		MIR_UNLOCK(mir);
		return;
	}

	/*
	 * Reconcile the reply count with the release count.  If the
	 * reply count is the same as the request count, then the stream
	 * is idle and we call mir_idle_start to start the timer (or wakeup
	 * a close).
	 */

	mir->mir_reply_cnt = mir->mir_rele_cnt;
	if (mir->mir_reply_cnt == mir->mir_req_cnt)
		mir_idle_start(q, mir);

	/*
	 * If outbound flow control has been relieved, then allow new
	 * inbound requests to be processed.
	 */
	if (mir->mir_hold_inbound) {
		mir->mir_hold_inbound = 0;
		qenable(RD(q));
	}
	if (q->q_first == NULL)
		mir->mir_inwservice = 0;
	MIR_UNLOCK(mir);
}
