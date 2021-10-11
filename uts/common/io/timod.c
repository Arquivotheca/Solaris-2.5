/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)timod.c	1.39	94/11/03 SMI"	/* SVr4.0 1.11	*/

/*
 * Transport Interface Library cooperating module - issue 2
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/stream.h>
#include <sys/stropts.h>
#include <sys/tihdr.h>
#include <sys/timod.h>
#include <sys/tiuser.h>
#include <sys/debug.h>
#include <sys/signal.h>
#include <sys/errno.h>
#include <sys/cred.h>
#include <sys/cmn_err.h>
#include <sys/kmem.h>
#include <sys/sysmacros.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>

/*
 * T_info_ack changed to support XTI.
 * Need to remain compatible with transport
 * providers written before SVR4.
 */
#define	OLD_INFO_ACK_SZ	(sizeof (struct T_info_ack)-sizeof (long))


/*
 * This is the loadable module wrapper.
 */
#include <sys/conf.h>
#include <sys/modctl.h>

static struct streamtab timinfo;

static struct fmodsw fsw = {
	"timod",
	&timinfo,
	D_NEW|D_MTQPAIR|D_MP,
};

/*
 * Module linkage information for the kernel.
 */

static struct modlstrmod modlstrmod = {
	&mod_strmodops, "transport interface str mod", &fsw
};

static struct modlinkage modlinkage = {
	MODREV_1, &modlstrmod, NULL
};

static krwlock_t	tim_list_rwlock;

int
_init(void)
{
	int	error;

	rw_init(&tim_list_rwlock, "timod: tim main list", RW_DRIVER, NULL);
	error = mod_install(&modlinkage);
	if (error != 0) {
		rw_destroy(&tim_list_rwlock);
		return (error);
	}

	return (0);
}

int
_fini(void)
{
	int	error;

	error = mod_remove(&modlinkage);
	if (error != 0)
		return (error);
	rw_destroy(&tim_list_rwlock);
	return (0);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}


/*
 * Hash list for all instances. Used to find tim_tim structure based on
 * QUEUE_ptr in T_CON_RES. Protected by tim_list_rwlock.
 */
#define	TIM_HASH_SIZE	256
#define	TIM_HASH(driverq) (((unsigned long)(driverq) >> 8) % TIM_HASH_SIZE)
static struct tim_tim	*tim_hash[TIM_HASH_SIZE];
int		tim_cnt = 0;

static int tim_setname(queue_t *q, mblk_t *mp);
static mblk_t *tim_filladdr();
static void tim_bcopy();
static void tim_addlink();
static void tim_dellink();
static struct tim_tim *tim_findlink();
static void tim_recover(queue_t *, mblk_t *, int);

#define	TIMOD_ID	3

/* stream data structure definitions */

static int timodopen(), timodclose(), timodwput(), timodrput(),
	timodrsrv(), timodwsrv();
static struct module_info timod_info =
	{TIMOD_ID, "timod", 0, INFPSZ, 512, 128};
static struct qinit timodrinit =
	{ timodrput, timodrsrv, timodopen, timodclose, nulldev, &timod_info,
	    NULL};
static struct qinit timodwinit =
	{ timodwput, timodwsrv, timodopen, timodclose, nulldev, &timod_info,
	    NULL};
static struct streamtab timinfo = { &timodrinit, &timodwinit, NULL, NULL };

/*
 * timodopen -	open routine gets called when the module gets pushed
 *		onto the stream.
 */
/*ARGSUSED*/
static int
timodopen(q, devp, flag, sflag, crp)
	register queue_t *q;
	dev_t *devp;
	int flag;
	int sflag;
	cred_t *crp;
{
	struct tim_tim *tp;

	ASSERT(q != NULL);

	if (q->q_ptr) {
		return (0);
	}

	tp = kmem_zalloc(sizeof (struct tim_tim), KM_SLEEP);
	tp->tim_rdq = q;
	tp->tim_iocsave = NULL;
	tp->tim_consave = NULL;

	/*
	 * Defer allocation of the buffers for the local address and
	 * the peer's address until we need them.
	 * Assume that timod has to handle getname until we here
	 * an iocack from the transport provider.
	 */
	tp->tim_flags |= DO_MYNAME|DO_PEERNAME;
	q->q_ptr = (caddr_t)tp;
	WR(q)->q_ptr = (caddr_t)tp;

	qprocson(q);

	/*
	 * Add this one to the list.
	 */
	tim_addlink(tp);

	return (0);
}

static void
tim_timer(q)
	queue_t	*q;
{
	struct tim_tim *tp = (struct tim_tim *)q->q_ptr;

	ASSERT(tp);

	if (q->q_flag & QREADR) {
		ASSERT(tp->tim_rtimoutid);
		tp->tim_rtimoutid = 0;
	} else {
		ASSERT(tp->tim_wtimoutid);
		tp->tim_wtimoutid = 0;
	}
	enableok(q);
	qenable(q);
}

static void
tim_buffer(q)
	queue_t	*q;
{
	struct tim_tim *tp = (struct tim_tim *)q->q_ptr;

	ASSERT(tp);

	if (q->q_flag & QREADR) {
		ASSERT(tp->tim_rbufcid);
		tp->tim_rbufcid = 0;
	} else {
		ASSERT(tp->tim_wbufcid);
		tp->tim_wbufcid = 0;
	}
	enableok(q);
	qenable(q);
}

/*
 * timodclose - This routine gets called when the module gets popped
 * off of the stream.
 */
/*ARGSUSED*/
static int
timodclose(q, flag, crp)
	register queue_t *q;
	int flag;
	cred_t *crp;
{
	register struct tim_tim *tp;
	register mblk_t *mp;
	register mblk_t *nmp;

	ASSERT(q != NULL);

	tp = (struct tim_tim *)q->q_ptr;
	q->q_ptr = NULL;

	ASSERT(tp != NULL);

	qprocsoff(q);

	/*
	 * Cancel any outstanding bufcall
	 * or timeout requests.
	 */
	if (tp->tim_wbufcid) {
		qunbufcall(q, tp->tim_wbufcid);
		tp->tim_wbufcid = 0;
	}
	if (tp->tim_rbufcid) {
		qunbufcall(q, tp->tim_rbufcid);
		tp->tim_rbufcid = 0;
	}
	if (tp->tim_wtimoutid) {
		(void) quntimeout(q, tp->tim_wtimoutid);
		tp->tim_wtimoutid = 0;
	}
	if (tp->tim_rtimoutid) {
		(void) quntimeout(q, tp->tim_rtimoutid);
		tp->tim_rtimoutid = 0;
	}

	if (tp->tim_iocsave != NULL)
		freemsg(tp->tim_iocsave);
	mp = tp->tim_consave;
	while (mp) {
		nmp = mp->b_next;
		mp->b_next = NULL;
		freemsg(mp);
		mp = nmp;
	}
	if (tp->tim_mymaxlen != 0)
		kmem_free(tp->tim_myname, tp->tim_mymaxlen);
	if (tp->tim_peermaxlen != 0)
		kmem_free(tp->tim_peername, tp->tim_peermaxlen);

	q->q_ptr = WR(q)->q_ptr = NULL;
	tim_dellink(tp);


	return (0);
}

/*
 * timodrput -	Module read put procedure.  This is called from
 *		the module, driver, or stream head upstream/downstream.
 *		Handles M_FLUSH, M_DATA and some M_PROTO (T_DATA_IND,
 *		and T_UNITDATA_IND) messages. All others are queued to
 *		be handled by the service procedures.
 */
static int
timodrput(q, mp)
register queue_t *q;
register mblk_t *mp;
{
	register union T_primitives *pptr;

	switch (mp->b_datap->db_type) {
	case M_DATA:
		if (q->q_count != 0) {
			(void) putq(q, mp);
			break;
		}
		if (canput(q->q_next))
			putnext(q, mp);
		else
			(void) putq(q, mp);
		break;
	case M_PROTO:
	case M_PCPROTO:
		pptr = (union T_primitives *)mp->b_rptr;
		switch (pptr->type) {
		case T_DATA_IND:
		case T_UNITDATA_IND:
			if (q->q_count != 0) {
				(void) putq(q, mp);
				break;
			}
			if (canput(q->q_next))
				putnext(q, mp);
			else
				(void) putq(q, mp);
			break;
		default:
			(void) putq(q, mp);
		}
		break;
	case M_FLUSH:
		if (*mp->b_rptr & FLUSHR) {
			if (*mp->b_rptr & FLUSHBAND)
				flushband(q, FLUSHDATA, *(mp->b_rptr + 1));
			else
				flushq(q, FLUSHDATA);
		}
		putnext(q, mp);
		break;
	default:
		(void) putq(q, mp);
		break;

	}
#ifdef lint
	return (0);
#endif /* lint */
}

/*
 * timodrsrv -	Module read queue service procedure.  This is called when
 *		messages are placed on an empty queue, when high priority
 *		messages are placed on the queue, and when flow control
 *		restrictions subside.  This code used to be included in a
 *		put procedure, but it was moved to a service procedure
 *		because several points were added where memory allocation
 *		could fail, and there is no reasonable recovery mechanism
 *		from the put procedure.
 */
/*ARGSUSED*/
static int
timodrsrv(q)
register queue_t *q;
{
	register mblk_t *mp;
	register union T_primitives *pptr;
	register struct tim_tim *tp;
	register struct iocblk *iocbp;
	register mblk_t *nbp;
	mblk_t *tmp;
	int size;

	ASSERT(q != NULL);

	tp = (struct tim_tim *)q->q_ptr;
	if (!tp)
	    return (0);

rgetnext:
	if ((mp = getq(q)) == NULL)
	    return (0);

	if ((mp->b_datap->db_type < QPCTL) && !bcanput(q->q_next, mp->b_band)) {
	    (void) putbq(q, mp);
	    return (0);
	}

	switch (mp->b_datap->db_type) {
	default:
	    putnext(q, mp);
	    goto rgetnext;

	case M_PROTO:
	case M_PCPROTO:
	    /* assert checks if there is enough data to determine type */

	    ASSERT((mp->b_wptr - mp->b_rptr) >= sizeof (long));

	    pptr = (union T_primitives *)mp->b_rptr;
	    switch (pptr->type) {
	    default:

#ifdef C2_AUDIT
		if (audit_active)
		    audit_sock(T_UNITDATA_IND, q, mp, TIMOD_ID);
#endif
		putnext(q, mp);
		goto rgetnext;

	    case T_ERROR_ACK:
		/* Restore db_type - recover() might have change it */
		mp->b_datap->db_type = M_PCPROTO;
error_ack:
		ASSERT((mp->b_wptr - mp->b_rptr) ==
		    sizeof (struct T_error_ack));

		if (tp->tim_flags & WAITIOCACK) {

		    ASSERT(tp->tim_iocsave != NULL);

		    if (tp->tim_iocsave->b_cont == NULL ||
			pptr->error_ack.ERROR_prim !=
			*(long *)tp->tim_iocsave->b_cont->b_rptr) {
			putnext(q, mp);
			goto rgetnext;
		    }

		    switch (pptr->error_ack.ERROR_prim) {
		    case T_INFO_REQ:
		    case T_OPTMGMT_REQ:
		    case T_BIND_REQ:
		    case T_UNBIND_REQ:
			/* get saved ioctl msg and set values */
			iocbp = (struct iocblk *)tp->tim_iocsave->b_rptr;
			iocbp->ioc_error = 0;
			iocbp->ioc_rval = pptr->error_ack.TLI_error;
			if (iocbp->ioc_rval == TSYSERR)
			    iocbp->ioc_rval |= pptr->error_ack.UNIX_error << 8;
			tp->tim_iocsave->b_datap->db_type = M_IOCACK;
			putnext(q, tp->tim_iocsave);
			tp->tim_iocsave = NULL;
			tp->tim_flags &= ~WAITIOCACK;
			freemsg(mp);
			goto rgetnext;
		    }
		}
		putnext(q, mp);
		goto rgetnext;

	    case T_OK_ACK:
		/* Restore db_type - recover() might have change it */
		mp->b_datap->db_type = M_PCPROTO;

		if (tp->tim_flags & WAITIOCACK) {

		    ASSERT(tp->tim_iocsave != NULL);

		    if (tp->tim_iocsave->b_cont == NULL ||
			pptr->ok_ack.CORRECT_prim !=
			*(long *)tp->tim_iocsave->b_cont->b_rptr) {
			    putnext(q, mp);
			    goto rgetnext;
		    }
		    if (pptr->ok_ack.CORRECT_prim == T_UNBIND_REQ)
			tp->tim_mylen = 0;
		    goto out;
		}
		putnext(q, mp);
		goto rgetnext;

	    case T_BIND_ACK:
		/* Restore db_type - recover() might have change it */
		mp->b_datap->db_type = M_PCPROTO;

		if (tp->tim_flags & WAITIOCACK) {
		    struct T_bind_ack *ackp = (struct T_bind_ack *)mp->b_rptr;
		    caddr_t p;

		    ASSERT(tp->tim_iocsave != NULL);

		    if (tp->tim_iocsave->b_cont == NULL ||
			*(long *)tp->tim_iocsave->b_cont->b_rptr !=
			T_BIND_REQ) {
			    putnext(q, mp);
			    goto rgetnext;
		    }
		    if (tp->tim_flags & DO_MYNAME) {
			if (ackp->ADDR_length > tp->tim_mymaxlen) {
			    p = kmem_alloc(ackp->ADDR_length, KM_NOSLEEP);
			    if (p == NULL) {
				tim_recover(q, mp, ackp->ADDR_length);
				return (0);
			    }
			    if (tp->tim_mymaxlen)
				kmem_free(tp->tim_myname, tp->tim_mymaxlen);
			    tp->tim_myname = p;
			    tp->tim_mymaxlen = ackp->ADDR_length;
			}
			tp->tim_mylen = ackp->ADDR_length;
			p = (caddr_t)mp->b_rptr + ackp->ADDR_offset;
			bcopy(p, tp->tim_myname, tp->tim_mylen);
		    }
		    goto out;
		}
		putnext(q, mp);
		goto rgetnext;

	    case T_OPTMGMT_ACK:
		/* Restore db_type - recover() might have change it */
		mp->b_datap->db_type = M_PCPROTO;

		if (tp->tim_flags & WAITIOCACK) {

		    ASSERT(tp->tim_iocsave != NULL);

		    if (tp->tim_iocsave->b_cont == NULL ||
			*(long *)tp->tim_iocsave->b_cont->b_rptr !=
			T_OPTMGMT_REQ) {
			    putnext(q, mp);
			    goto rgetnext;
		    }
		    goto out;
		}
		putnext(q, mp);
		goto rgetnext;

	    case T_INFO_ACK:
		/* Restore db_type - recover() might have change it */
		mp->b_datap->db_type = M_PCPROTO;

		if (tp->tim_flags & WAITIOCACK) {

		    ASSERT(tp->tim_iocsave != NULL);
		    size = mp->b_wptr - mp->b_rptr;
		    ASSERT((size == sizeof (struct T_info_ack)) ||
			(size == OLD_INFO_ACK_SZ));
		    if (tp->tim_iocsave->b_cont == NULL ||
			*(long *)tp->tim_iocsave->b_cont->b_rptr !=
			T_INFO_REQ) {
			    putnext(q, mp);
			    return (0);
		    }
		    freezestr(q);
		    (void) strqset(q, QMAXPSZ, 0, pptr->info_ack.TIDU_size);
		    (void) strqset(OTHERQ(q), QMAXPSZ, 0,
				pptr->info_ack.TIDU_size);
		    unfreezestr(q);
		    if ((pptr->info_ack.SERV_type == T_COTS) ||
			    (pptr->info_ack.SERV_type == T_COTS_ORD)) {
			tp->tim_flags = (tp->tim_flags & ~CLTS) | COTS;
		    } else if (pptr->info_ack.SERV_type == T_CLTS) {
			tp->tim_flags = (tp->tim_flags & ~COTS) | CLTS;
		    }

			/*
			 * make sure the message sent back is the size of
			 * a T_info_ack.
			 */
		    if (size == OLD_INFO_ACK_SZ) {
			if (mp->b_datap->db_lim - mp->b_wptr < sizeof (long)) {
			    tmp = allocb(sizeof (struct T_info_ack), BPRI_HI);
			    if (tmp == NULL) {
				ASSERT((mp->b_datap->db_lim -
			mp->b_datap->db_base) < sizeof (struct T_error_ack));
				mp->b_rptr = mp->b_datap->db_base;
				mp->b_wptr = mp->b_rptr +
				    sizeof (struct T_error_ack);
				pptr = (union T_primitives *)mp->b_rptr;
				pptr->error_ack.ERROR_prim = T_INFO_ACK;
				pptr->error_ack.TLI_error = TSYSERR;
				pptr->error_ack.UNIX_error = EAGAIN;
				pptr->error_ack.PRIM_type = T_ERROR_ACK;
				mp->b_datap->db_type = M_PCPROTO;
				goto error_ack;
			    } else {
				bcopy((char *)mp->b_rptr, (char *)tmp->b_rptr,
				    size);
				tmp->b_wptr += size;
				pptr = (union T_primitives *)tmp->b_rptr;
				freemsg(mp);
				mp = tmp;
			    }
			}
			mp->b_wptr += sizeof (long);
			pptr->info_ack.PROVIDER_flag = 0;
		    }
		    goto out;
		}
		putnext(q, mp);
		goto rgetnext;

out:
		iocbp = (struct iocblk *)tp->tim_iocsave->b_rptr;
		ASSERT(tp->tim_iocsave->b_datap != NULL);
		tp->tim_iocsave->b_datap->db_type = M_IOCACK;
		mp->b_datap->db_type = M_DATA;
		freemsg(tp->tim_iocsave->b_cont);
		tp->tim_iocsave->b_cont = mp;
		iocbp->ioc_error = 0;
		iocbp->ioc_rval = 0;
		iocbp->ioc_count = mp->b_wptr - mp->b_rptr;
		putnext(q, tp->tim_iocsave);
		tp->tim_iocsave = NULL;
		tp->tim_flags &= ~WAITIOCACK;
		goto rgetnext;

	    case T_CONN_IND:
		if (tp->tim_flags & DO_PEERNAME) {
		    if (((nbp = dupmsg(mp)) != NULL) ||
			((nbp = copymsg(mp)) != NULL)) {
			nbp->b_next = tp->tim_consave;
			tp->tim_consave = nbp;
		    } else {
			tim_recover(q, mp, sizeof (mblk_t));
			return (0);
		    }
		}
#ifdef C2_AUDIT
	if (audit_active)
		audit_sock(T_CONN_IND, q, mp, TIMOD_ID);
#endif
		putnext(q, mp);
		goto rgetnext;

	    case T_CONN_CON:
		tp->tim_flags &= ~CONNWAIT;
		putnext(q, mp);
		goto rgetnext;

	    case T_DISCON_IND: {
		struct T_discon_ind *disp;
		struct T_conn_ind *conp;
		mblk_t *pbp = NULL;

		disp = (struct T_discon_ind *)mp->b_rptr;
		tp->tim_flags &= ~(CONNWAIT|LOCORDREL|REMORDREL);
		tp->tim_peerlen = 0;
		for (nbp = tp->tim_consave; nbp; nbp = nbp->b_next) {
		    conp = (struct T_conn_ind *)nbp->b_rptr;
		    if (conp->SEQ_number == disp->SEQ_number)
			break;
		    pbp = nbp;
		}
		if (nbp) {
		    if (pbp)
			pbp->b_next = nbp->b_next;
		    else
			tp->tim_consave = nbp->b_next;
		    nbp->b_next = NULL;
		    freemsg(nbp);
		}
		putnext(q, mp);
		goto rgetnext;
	    }

	    case T_ORDREL_IND:
		if (tp->tim_flags & LOCORDREL) {
		    tp->tim_flags &= ~(LOCORDREL|REMORDREL);
		    tp->tim_peerlen = 0;
		} else {
		    tp->tim_flags |= REMORDREL;
		}
		putnext(q, mp);
		goto rgetnext;
	    }

	case M_IOCACK:
	    iocbp = (struct iocblk *)mp->b_rptr;
	    if (iocbp->ioc_cmd == TI_GETMYNAME) {

		/*
		 * Transport provider supports this ioctl,
		 * so I don't have to.
		 */
		tp->tim_flags &= ~DO_MYNAME;
		if (tp->tim_mymaxlen != 0) {
		    kmem_free(tp->tim_myname, tp->tim_mymaxlen);
		    tp->tim_myname = NULL;
		    tp->tim_mymaxlen = 0;
		    freemsg(tp->tim_iocsave);
		    tp->tim_iocsave = NULL;
		}
	    } else if (iocbp->ioc_cmd == TI_GETPEERNAME) {
		register mblk_t *bp;

		/*
		 * Transport provider supports this ioctl,
		 * so I don't have to.
		 */
		tp->tim_flags &= ~DO_PEERNAME;
		if (tp->tim_peermaxlen != 0) {
		    kmem_free(tp->tim_peername, tp->tim_peermaxlen);
		    tp->tim_peername = NULL;
		    tp->tim_peermaxlen = 0;
		    freemsg(tp->tim_iocsave);
		    tp->tim_iocsave = NULL;
		    bp = tp->tim_consave;
		    while (bp) {
			nbp = bp->b_next;
			bp->b_next = NULL;
			freemsg(bp);
			bp = nbp;
		    }
		    tp->tim_consave = NULL;
		}
	    }
	    putnext(q, mp);
	    goto rgetnext;

	case M_IOCNAK:
	    iocbp = (struct iocblk *)mp->b_rptr;
	    if (((iocbp->ioc_cmd == TI_GETMYNAME) ||
		(iocbp->ioc_cmd == TI_GETPEERNAME)) &&
		((iocbp->ioc_error == EINVAL) || (iocbp->ioc_error == 0))) {
			freemsg(mp);
			if (tp->tim_iocsave) {
			    mp = tp->tim_iocsave;
			    tp->tim_iocsave = NULL;
			    tp->tim_flags |= NAMEPROC;
			    if (ti_doname(WR(q), mp, tp->tim_myname,
				(uint) tp->tim_mylen, tp->tim_peername,
				(uint) tp->tim_peerlen) != DONAME_CONT) {
				    tp->tim_flags &= ~NAMEPROC;
				}
			    goto rgetnext;
			}
	    }
	    putnext(q, mp);
	    goto rgetnext;
	}
}


/*
 * timodwput -	Module write put procedure.  This is called from
 *		the module, driver, or stream head upstream/downstream.
 *		Handles M_FLUSH, M_DATA and some M_PROTO (T_DATA_REQ,
 *		and T_UNITDATA_REQ) messages. All others are queued to
 *		be handled by the service procedures.
 */
static int
timodwput(q, mp)
register queue_t *q;
register mblk_t *mp;
{
	register union T_primitives *pptr;
	register struct tim_tim *tp;

	switch (mp->b_datap->db_type) {
	case M_DATA:
		if (q->q_count != 0) {
			(void) putq(q, mp);
			break;
		}
		tp = (struct tim_tim *)q->q_ptr;
		ASSERT(tp);
		if (tp->tim_flags & CLTS) {
			mblk_t	*tmp;

			if ((tmp = tim_filladdr(q, mp)) == NULL) {
				(void) putq(q, mp);
				break;
			} else {
				mp = tmp;
			}
		}
		if (canput(q->q_next))
			putnext(q, mp);
		else
			(void) putq(q, mp);
		break;
	case M_PROTO:
	case M_PCPROTO:
		pptr = (union T_primitives *)mp->b_rptr;
		switch (pptr->type) {
		case T_UNITDATA_REQ:
			if (q->q_count != 0) {
				(void) putq(q, mp);
				break;
			}
			tp = (struct tim_tim *)q->q_ptr;
			ASSERT(tp);
			if (tp->tim_flags & CLTS) {
				mblk_t	*tmp;

				if ((tmp = tim_filladdr(q, mp)) == NULL) {
					(void) putq(q, mp);
					break;
				} else {
					mp = tmp;
				}
			}
			if (canput(q->q_next))
				putnext(q, mp);
			else
				(void) putq(q, mp);
			break;

		case T_DATA_REQ:
			if (q->q_count != 0) {
				(void) putq(q, mp);
				break;
			}
			if (canput(q->q_next))
				putnext(q, mp);
			else
				(void) putq(q, mp);
			break;
		default:
			(void) putq(q, mp);
		}
		break;
	case M_FLUSH:
		if (*mp->b_rptr & FLUSHW) {
			if (*mp->b_rptr & FLUSHBAND)
				flushband(q, FLUSHDATA, *(mp->b_rptr + 1));
			else
				flushq(q, FLUSHDATA);
		}
		putnext(q, mp);
		break;
	default:
		(void) putq(q, mp);
		break;
	}
#ifdef lint
	return (0);
#endif /* lint */
}

/*
 * timodwsrv -	Module write queue service procedure.
 *		This is called when messages are placed on an empty queue,
 *		when high priority messages are placed on the queue, and
 *		when flow control restrictions subside.  This code used to
 *		be included in a put procedure, but it was moved to a
 *		service procedure because several points were added where
 *		memory allocation could fail, and there is no reasonable
 *		recovery mechanism from the put procedure.
 */
static int
timodwsrv(q)
register queue_t *q;
{
	register mblk_t *mp;
	register union T_primitives *pptr;
	register struct tim_tim *tp;
	register mblk_t *tmp;
	struct iocblk *iocbp;

	ASSERT(q != NULL);
	tp = (struct tim_tim *)q->q_ptr;
	if (!tp)
	    return (0);

wgetnext:
	if ((mp = getq(q)) == NULL)
	    return (0);
	if ((mp->b_datap->db_type < QPCTL) && !bcanput(q->q_next, mp->b_band)) {
	    (void) putbq(q, mp);
	    return (0);
	}

	switch (mp->b_datap->db_type) {
	default:
	    putnext(q, mp);
	    goto wgetnext;

	case M_DATA:
	    if (tp->tim_flags & CLTS) {
		if ((tmp = tim_filladdr(q, mp)) == NULL) {
			tim_recover(q, mp, sizeof (struct T_unitdata_req) +
				tp->tim_peerlen);
			return (0);
		} else {
			mp = tmp;
		}
	    }
	    putnext(q, mp);
	    goto wgetnext;

	case M_IOCTL:
	    iocbp = (struct iocblk *)mp->b_rptr;

	    ASSERT((mp->b_wptr - mp->b_rptr) == sizeof (struct iocblk));

	    if (tp->tim_flags & WAITIOCACK) {
		mp->b_datap->db_type = M_IOCNAK;
		iocbp->ioc_error = EPROTO;
		qreply(q, mp);
		goto wgetnext;
	    }

	    switch (iocbp->ioc_cmd) {
	    default:
		putnext(q, mp);
		goto wgetnext;

	    case TI_BIND:
	    case TI_UNBIND:
	    case TI_GETINFO:
	    case TI_OPTMGMT:
		if (iocbp->ioc_count == TRANSPARENT) {
		    mp->b_datap->db_type = M_IOCNAK;
		    iocbp->ioc_error = EINVAL;
		    qreply(q, mp);
		    goto wgetnext;
		}
		if (mp->b_cont == NULL) {
		    mp->b_datap->db_type = M_IOCNAK;
		    iocbp->ioc_error = EINVAL;
		    qreply(q, mp);
		    goto wgetnext;
		}
		if (!pullupmsg(mp->b_cont, -1)) {
		    mp->b_datap->db_type = M_IOCNAK;
		    iocbp->ioc_error = EAGAIN;
		    qreply(q, mp);
		    goto wgetnext;
		}
		if ((tmp = copymsg(mp->b_cont)) == NULL) {
		    int i = 0;

		    for (tmp = mp; tmp; tmp = tmp->b_next)
			i += (int)(tmp->b_wptr - tmp->b_rptr);
		    tim_recover(q, mp, i);
		    return (0);
		}
		tp->tim_iocsave = mp;
		tp->tim_flags |= WAITIOCACK;
		if (iocbp->ioc_cmd == TI_GETINFO)
		    tmp->b_datap->db_type = M_PCPROTO;
		else
		    tmp->b_datap->db_type = M_PROTO;
		putnext(q, tmp);
		goto wgetnext;

	    case TI_GETMYNAME:
		if (!(tp->tim_flags & DO_MYNAME)) {
		    putnext(q, mp);
		    goto wgetnext;
		}
		goto getname;

	    case TI_GETPEERNAME:
		if (!(tp->tim_flags & DO_PEERNAME)) {
		    putnext(q, mp);
		    goto wgetnext;
		}
getname:
		if ((tmp = copymsg(mp)) == NULL) {
		    int i = 0;

		    for (tmp = mp; tmp; tmp = tmp->b_next)
			i += (int)(tmp->b_wptr - tmp->b_rptr);
		    tim_recover(q, mp, i);
		    return (0);
		}
		tp->tim_iocsave = mp;
		putnext(q, tmp);
		goto wgetnext;

	    case TI_SETMYNAME:

		/*
		 * Kludge ioctl for root only.  If TIMOD is pushed
		 * on a stream that is already "bound", we want
		 * to be able to support the TI_GETMYNAME ioctl if the
		 * transport provider doesn't support it.
		 */
		if (iocbp->ioc_uid != 0)
		    iocbp->ioc_error = EPERM;

		/*
		 * If DO_MYNAME is not set, the transport provider supports
		 * the TI_GETMYNAME ioctl, so setting the name here won't
		 * be of any use.
		 */
		if (!(tp->tim_flags & DO_MYNAME))
		    iocbp->ioc_error = EBUSY;

		goto setname;

	    case TI_SETPEERNAME:

		/*
		 * Kludge ioctl for root only.  If TIMOD is pushed
		 * on a stream that is already "connected", we want
		 * to be able to support the TI_GETPEERNAME ioctl if the
		 * transport provider doesn't support it.
		 */
		if (iocbp->ioc_uid != 0)
		    iocbp->ioc_error = EPERM;

		/*
		 * If DO_PEERNAME is not set, the transport provider supports
		 * the TI_GETPEERNAME ioctl, so setting the name here won't
		 * be of any use.
		 */
		if (!(tp->tim_flags & DO_PEERNAME))
		    iocbp->ioc_error = EBUSY;

setname:
		if (iocbp->ioc_error == 0) {
		    if (!tim_setname(q, mp))
			return (0);
		} else {
		    mp->b_datap->db_type = M_IOCNAK;
		    freemsg(mp->b_cont);
		    mp->b_cont = NULL;
		    qreply(q, mp);
		}
		goto wgetnext;
	    }

	case M_IOCDATA:
	    if (tp->tim_flags & NAMEPROC) {
		if (ti_doname(q, mp, tp->tim_myname, (uint) tp->tim_mylen,
		    tp->tim_peername, (uint) tp->tim_peerlen) != DONAME_CONT) {
			tp->tim_flags &= ~NAMEPROC;
		}
		goto wgetnext;
	    }
	    putnext(q, mp);
	    goto wgetnext;

	case M_PROTO:
	case M_PCPROTO:
	    /* assert checks if there is enough data to determine type */
	    ASSERT((mp->b_wptr - mp->b_rptr) >= sizeof (long));

	    pptr = (union T_primitives *)mp->b_rptr;
	    switch (pptr->type) {
	    default:
		putnext(q, mp);
		goto wgetnext;

	    case T_UNITDATA_REQ:
		if (tp->tim_flags & CLTS) {
			if ((tmp = tim_filladdr(q, mp)) == NULL) {
				tim_recover(q, mp,
					    sizeof (struct T_unitdata_req) +
					    tp->tim_peerlen);
				return (0);
			} else {
				mp = tmp;
			}
		}
#ifdef C2_AUDIT
	if (audit_active)
		audit_sock(T_UNITDATA_REQ, q, mp, TIMOD_ID);
#endif
		putnext(q, mp);
		goto wgetnext;

	    case T_CONN_REQ: {
		struct T_conn_req *reqp = (struct T_conn_req *)mp->b_rptr;
		caddr_t p;

		if (tp->tim_flags & DO_PEERNAME) {
		    if (reqp->DEST_length > tp->tim_peermaxlen) {
			p = kmem_alloc(reqp->DEST_length, KM_NOSLEEP);
			if (p == NULL) {
			    tim_recover(q, mp, reqp->DEST_length);
			    return (0);
			}
			if (tp->tim_peermaxlen)
			    kmem_free(tp->tim_peername, tp->tim_peermaxlen);
			tp->tim_peername = p;
			tp->tim_peermaxlen = reqp->DEST_length;
		    }
		    tp->tim_peerlen = reqp->DEST_length;
		    p = (caddr_t)mp->b_rptr + reqp->DEST_offset;
		    bcopy(p, tp->tim_peername, tp->tim_peerlen);
		    if (tp->tim_flags & COTS)
			tp->tim_flags |= CONNWAIT;
		}
#ifdef C2_AUDIT
	if (audit_active)
		audit_sock(T_CONN_REQ, q, mp, TIMOD_ID);
#endif
		putnext(q, mp);
		goto wgetnext;
	    }

	    case T_CONN_RES: {
		struct T_conn_res *resp;
		struct T_conn_ind *indp;
		mblk_t *pmp = NULL;
		struct tim_tim *ntp;
		caddr_t p;

		resp = (struct T_conn_res *)mp->b_rptr;
		for (tmp = tp->tim_consave; tmp; tmp = tmp->b_next) {
		    indp = (struct T_conn_ind *)tmp->b_rptr;
		    if (indp->SEQ_number == resp->SEQ_number)
			break;
		    pmp = tmp;
		}
		if (!tmp)
		    goto cresout;
		if (pmp)
		    pmp->b_next = tmp->b_next;
		else
		    tp->tim_consave = tmp->b_next;
		tmp->b_next = NULL;

		rw_enter(&tim_list_rwlock, RW_READER);
		if ((ntp = tim_findlink(resp->QUEUE_ptr)) == NULL) {
			rw_exit(&tim_list_rwlock);
			goto cresout;
		}
		if (ntp->tim_flags & DO_PEERNAME) {
		    if (indp->SRC_length > ntp->tim_peermaxlen) {
			p = kmem_alloc(indp->SRC_length, KM_NOSLEEP);
			if (p == NULL) {
			    tmp->b_next = tp->tim_consave;
			    tp->tim_consave = tmp;
			    tim_recover(q, mp, indp->SRC_length);
			    rw_exit(&tim_list_rwlock);
			    return (0);
			}
			if (ntp->tim_peermaxlen)
			    kmem_free(ntp->tim_peername, ntp->tim_peermaxlen);
			ntp->tim_peername = p;
			ntp->tim_peermaxlen = indp->SRC_length;
		    }
		    ntp->tim_peerlen = indp->SRC_length;
		    p = (caddr_t)tmp->b_rptr + indp->SRC_offset;
		    bcopy(p, ntp->tim_peername, ntp->tim_peerlen);
		}
		rw_exit(&tim_list_rwlock);
cresout:
		freemsg(tmp);
		putnext(q, mp);
		goto wgetnext;
	    }

	    case T_DISCON_REQ: {
		struct T_discon_req *disp;
		struct T_conn_ind *conp;
		mblk_t *pmp = NULL;

		disp = (struct T_discon_req *)mp->b_rptr;
		tp->tim_flags &= ~(CONNWAIT|LOCORDREL|REMORDREL);
		tp->tim_peerlen = 0;

		/*
		 * If we are already connected, there won't
		 * be any messages on tim_consave.
		 */
		for (tmp = tp->tim_consave; tmp; tmp = tmp->b_next) {
		    conp = (struct T_conn_ind *)tmp->b_rptr;
		    if (conp->SEQ_number == disp->SEQ_number)
			break;
		    pmp = tmp;
		}
		if (tmp) {
		    if (pmp)
			pmp->b_next = tmp->b_next;
		    else
			tp->tim_consave = tmp->b_next;
		    tmp->b_next = NULL;
		    freemsg(tmp);
		}
		putnext(q, mp);
		goto wgetnext;
	    }

	    case T_ORDREL_REQ:
		if (tp->tim_flags & REMORDREL) {
		    tp->tim_flags &= ~(LOCORDREL|REMORDREL);
		    tp->tim_peerlen = 0;
		} else {
		    tp->tim_flags |= LOCORDREL;
		}
		putnext(q, mp);
		goto wgetnext;
	    }

	}
}

/*
 * Process the TI_GETNAME ioctl.  If no name exists, return len = 0
 * in netbuf structures.  The state transitions are determined by what
 * is hung of cq_private (cp_private) in the copyresp (copyreq) structure.
 * The high-level steps in the ioctl processing are as follows:
 *
 * 1) we recieve an transparent M_IOCTL with the arg in the second message
 *	block of the message.
 * 2) we send up an M_COPYIN request for the netbuf structure pointed to
 *	by arg.  The block containing arg is hung off cq_private.
 * 3) we receive an M_IOCDATA response with cp->cp_private->b_cont == NULL.
 *	This means that the netbuf structure is found in the message block
 *	mp->b_cont.
 * 4) we send up an M_COPYOUT request with the netbuf message hung off
 *	cq_private->b_cont.  The address we are copying to is netbuf.buf.
 *	we set netbuf.len to 0 to indicate that we should copy the netbuf
 *	structure the next time.  The message mp->b_cont contains the
 *	address info.
 * 5) we receive an M_IOCDATA with cp_private->b_cont != NULL and
 *	netbuf.len == 0.  Restore netbuf.len to either llen ot rlen.
 * 6) we send up an M_COPYOUT request with a copy of the netbuf message
 *	hung off mp->b_cont.  In the netbuf structure in the message hung
 *	off cq_private->b_cont, we set netbuf.len to 0 and netbuf.maxlen
 *	to 0.  This means that the next step is to ACK the ioctl.
 * 7) we receive an M_IOCDATA message with cp_private->b_cont != NULL and
 *	netbuf.len == 0 and netbuf.maxlen == 0.  Free up cp->private and
 *	send an M_IOCACK upstream, and we are done.
 *
 */
int
ti_doname(q, mp, lname, llen, rname, rlen)
	queue_t *q;		/* queue message arrived at */
	mblk_t *mp;		/* M_IOCTL or M_IOCDATA message only */
	caddr_t lname;		/* local name */
	uint llen;		/* length of local name (0 if not set) */
	caddr_t rname;		/* remote name */
	uint rlen;		/* length of remote name (0 if not set) */
{
	struct iocblk *iocp;
	struct copyreq *cqp;
	struct copyresp *csp;
	struct netbuf *np;
	int ret;
	mblk_t *bp;

	switch (mp->b_datap->db_type) {
	case M_IOCTL:
		iocp = (struct iocblk *)mp->b_rptr;
		if ((iocp->ioc_cmd != TI_GETMYNAME) &&
		    (iocp->ioc_cmd != TI_GETPEERNAME)) {
			cmn_err(CE_WARN, "ti_doname: bad M_IOCTL command\n");
			iocp->ioc_error = EINVAL;
			freemsg(mp->b_cont);
			mp->b_cont = NULL;
			mp->b_datap->db_type = M_IOCNAK;
			qreply(q, mp);
			ret = DONAME_FAIL;
			break;
		}
		if ((iocp->ioc_count != TRANSPARENT) ||
		    (mp->b_cont == NULL) || ((mp->b_cont->b_wptr -
		    mp->b_cont->b_rptr) != sizeof (caddr_t))) {
			iocp->ioc_error = EINVAL;
			freemsg(mp->b_cont);
			mp->b_cont = NULL;
			mp->b_datap->db_type = M_IOCNAK;
			qreply(q, mp);
			ret = DONAME_FAIL;
			break;
		}
		cqp = (struct copyreq *)mp->b_rptr;
		cqp->cq_private = mp->b_cont;
		cqp->cq_addr = (caddr_t)*(long *)mp->b_cont->b_rptr;
		mp->b_cont = NULL;
		cqp->cq_size = sizeof (struct netbuf);
		cqp->cq_flag = 0;
		mp->b_datap->db_type = M_COPYIN;
		mp->b_wptr = mp->b_rptr + sizeof (struct copyreq);
		qreply(q, mp);
		ret = DONAME_CONT;
		break;

	case M_IOCDATA:
		csp = (struct copyresp *)mp->b_rptr;
		iocp = (struct iocblk *)mp->b_rptr;
		cqp = (struct copyreq *)mp->b_rptr;
		if ((csp->cp_cmd != TI_GETMYNAME) &&
		    (csp->cp_cmd != TI_GETPEERNAME)) {
			cmn_err(CE_WARN, "ti_doname: bad M_IOCDATA command\n");
			iocp->ioc_error = EINVAL;
			freemsg(mp->b_cont);
			mp->b_cont = NULL;
			mp->b_datap->db_type = M_IOCNAK;
			qreply(q, mp);
			ret = DONAME_FAIL;
			break;
		}
		if (csp->cp_rval) {	/* error */
			freemsg(csp->cp_private);
			freemsg(mp);
			ret = DONAME_FAIL;
			break;
		}
		ASSERT(csp->cp_private != NULL);
		if (csp->cp_private->b_cont == NULL) {	/* got netbuf */
			ASSERT(mp->b_cont);
			np = (struct netbuf *)mp->b_cont->b_rptr;
			if (csp->cp_cmd == TI_GETMYNAME) {
				if (llen == 0) {
					np->len = 0;	/* copy just netbuf */
				} else if (llen > np->maxlen) {
					iocp->ioc_error = ENAMETOOLONG;
					freemsg(csp->cp_private);
					freemsg(mp->b_cont);
					mp->b_cont = NULL;
					mp->b_datap->db_type = M_IOCNAK;
					qreply(q, mp);
					ret = DONAME_FAIL;
					break;
				} else {
					np->len = llen;	/* copy buffer */
				}
			} else {	/* REMOTENAME */
				if (rlen == 0) {
					np->len = 0;	/* copy just netbuf */
				} else if (rlen > np->maxlen) {
					iocp->ioc_error = ENAMETOOLONG;
					freemsg(mp->b_cont);
					mp->b_cont = NULL;
					mp->b_datap->db_type = M_IOCNAK;
					qreply(q, mp);
					ret = DONAME_FAIL;
					break;
				} else {
					np->len = rlen;	/* copy buffer */
				}
			}
			csp->cp_private->b_cont = mp->b_cont;
			mp->b_cont = NULL;
		}
		np = (struct netbuf *)csp->cp_private->b_cont->b_rptr;
		if (np->len == 0) {
			if (np->maxlen == 0) {

				/*
				 * ack the ioctl
				 */
				freemsg(csp->cp_private);
				iocp->ioc_count = 0;
				iocp->ioc_rval = 0;
				iocp->ioc_error = 0;
				mp->b_datap->db_type = M_IOCACK;
				freemsg(mp->b_cont);
				mp->b_cont = NULL;
				qreply(q, mp);
				ret = DONAME_DONE;
				break;
			}

			/*
			 * copy netbuf to user
			 */
			if (csp->cp_cmd == TI_GETMYNAME)
				np->len = llen;
			else	/* TI_GETPEERNAME */
				np->len = rlen;
			if ((bp = allocb(sizeof (struct netbuf), BPRI_MED))
			    == NULL) {
				iocp->ioc_error = EAGAIN;
				freemsg(csp->cp_private);
				freemsg(mp->b_cont);
				bp->b_cont = NULL;
				mp->b_datap->db_type = M_IOCNAK;
				qreply(q, mp);
				ret = DONAME_FAIL;
				break;
			}
			bp->b_wptr += sizeof (struct netbuf);
			bcopy((caddr_t) np, (caddr_t) bp->b_rptr,
			    sizeof (struct netbuf));
			cqp->cq_addr =
			    (caddr_t)*(long *)csp->cp_private->b_rptr;
			cqp->cq_size = sizeof (struct netbuf);
			cqp->cq_flag = 0;
			mp->b_datap->db_type = M_COPYOUT;
			mp->b_cont = bp;
			np->len = 0;
			np->maxlen = 0; /* ack next time around */
			qreply(q, mp);
			ret = DONAME_CONT;
			break;
		}

		/*
		 * copy the address to the user
		 */
		if ((bp = allocb(np->len, BPRI_MED)) == NULL) {
			iocp->ioc_error = EAGAIN;
			freemsg(csp->cp_private);
			freemsg(mp->b_cont);
			mp->b_cont = NULL;
			mp->b_datap->db_type = M_IOCNAK;
			qreply(q, mp);
			ret = DONAME_FAIL;
			break;
		}
		bp->b_wptr += np->len;
		if (csp->cp_cmd == TI_GETMYNAME)
			bcopy((caddr_t) lname, (caddr_t) bp->b_rptr, llen);
		else	/* TI_GETPEERNAME */
			bcopy((caddr_t) rname, (caddr_t) bp->b_rptr, rlen);
		cqp->cq_addr = (caddr_t)np->buf;
		cqp->cq_size = np->len;
		cqp->cq_flag = 0;
		mp->b_datap->db_type = M_COPYOUT;
		mp->b_cont = bp;
		np->len = 0;	/* copy the netbuf next time around */
		qreply(q, mp);
		ret = DONAME_CONT;
		break;

	default:
		cmn_err(CE_WARN,
		    "ti_doname: freeing bad message type = %d\n",
		    mp->b_datap->db_type);
		freemsg(mp);
		ret = DONAME_FAIL;
		break;
	}
	return (ret);
}

static int
tim_setname(q, mp)
	register queue_t *q;
	register mblk_t *mp;
{
	register struct iocblk *iocp;
	register struct copyreq *cqp;
	register struct copyresp *csp;
	struct tim_tim *tp;
	struct netbuf *netp;
	unsigned int len;
	caddr_t p;

	tp = (struct tim_tim *)q->q_ptr;
	iocp = (struct iocblk *)mp->b_rptr;
	cqp = (struct copyreq *)mp->b_rptr;
	csp = (struct copyresp *)mp->b_rptr;

	switch (mp->b_datap->db_type) {
	case M_IOCTL:
		if ((iocp->ioc_cmd != TI_SETMYNAME) &&
		    (iocp->ioc_cmd != TI_SETPEERNAME)) {
			cmn_err(CE_PANIC, "ti_setname: bad M_IOCTL command\n");
		}
		if ((iocp->ioc_count != TRANSPARENT) ||
		    (mp->b_cont == NULL) || ((mp->b_cont->b_wptr -
		    mp->b_cont->b_rptr) != sizeof (caddr_t))) {
			iocp->ioc_error = EINVAL;
			freemsg(mp->b_cont);
			mp->b_cont = NULL;
			mp->b_datap->db_type = M_IOCNAK;
			qreply(q, mp);
			break;
		}
		cqp->cq_addr = (caddr_t)*(long *)mp->b_cont->b_rptr;
		freemsg(mp->b_cont);
		mp->b_cont = NULL;
		cqp->cq_size = sizeof (struct netbuf);
		cqp->cq_flag = 0;
		cqp->cq_private = NULL;
		mp->b_datap->db_type = M_COPYIN;
		mp->b_wptr = mp->b_rptr + sizeof (struct copyreq);
		qreply(q, mp);
		break;

	case M_IOCDATA:
		if (csp->cp_rval) {
			freemsg(mp);
			break;
		}
		if (csp->cp_private == NULL) {	/* got netbuf */
			netp = (struct netbuf *)mp->b_cont->b_rptr;
			csp->cp_private = mp->b_cont;
			mp->b_cont = NULL;
			cqp->cq_addr = netp->buf;
			cqp->cq_size = netp->len;
			cqp->cq_flag = 0;
			mp->b_datap->db_type = M_COPYIN;
			mp->b_wptr = mp->b_rptr + sizeof (struct copyreq);
			qreply(q, mp);
			break;
		} else {			/* got addr */
			len = msgdsize(mp->b_cont);
			if (csp->cp_cmd == TI_SETMYNAME) {
				if (len > tp->tim_mymaxlen) {
					p = kmem_alloc(len, KM_NOSLEEP);
					if (p == NULL) {
						tim_recover(q, mp, len);
						return (0);
					}
					if (tp->tim_mymaxlen)
					    kmem_free(tp->tim_myname,
						tp->tim_mymaxlen);
					tp->tim_myname = p;
					tp->tim_mymaxlen = len;
				}
				tp->tim_mylen = len;
				tim_bcopy(mp->b_cont, tp->tim_myname, len);
			} else if (csp->cp_cmd == TI_SETPEERNAME) {
				if (len > tp->tim_peermaxlen) {
					p = kmem_alloc(len, KM_NOSLEEP);
					if (p == NULL) {
						tim_recover(q, mp, len);
						return (0);
					}
					if (tp->tim_peermaxlen)
					    kmem_free(tp->tim_peername,
						tp->tim_peermaxlen);
					tp->tim_peername = p;
					tp->tim_peermaxlen = len;
				}
				tp->tim_peerlen = len;
				tim_bcopy(mp->b_cont, tp->tim_peername, len);
			} else {
				cmn_err(CE_PANIC,
				    "ti_setname: bad M_IOCDATA command\n");
			}
			freemsg(csp->cp_private);
			iocp->ioc_count = 0;
			iocp->ioc_rval = 0;
			iocp->ioc_error = 0;
			mp->b_datap->db_type = M_IOCACK;
			freemsg(mp->b_cont);
			mp->b_cont = NULL;
			qreply(q, mp);
		}
		break;

	default:
		cmn_err(CE_PANIC, "ti_setname: bad message type = %d\n",
		    mp->b_datap->db_type);
	}
	return (1);
}

/*
 * Copy data from a message to a buffer taking into account
 * the possibility of the data being split between multiple
 * message blocks.
 */
static void
tim_bcopy(frommp, to, len)
	mblk_t *frommp;
	register caddr_t to;
	register unsigned int len;
{
	register mblk_t *mp;
	register int size;

	mp = frommp;
	while (mp && len) {
		size = MIN((mp->b_wptr - mp->b_rptr), len);
		bcopy((caddr_t)mp->b_rptr, to, size);
		len -= size;
		to += size;
		mp = mp->b_cont;
	}
}

/*
 * Fill in the address of a connectionless data packet if a connect
 * had been done on this endpoint.
 */
static mblk_t *
tim_filladdr(q, mp)
	register queue_t *q;
	register mblk_t *mp;
{
	register mblk_t *bp;
	register struct tim_tim *tp;
	struct T_unitdata_req *up;
	struct T_unitdata_req *nup;

	tp = (struct tim_tim *)q->q_ptr;
	if (mp->b_datap->db_type == M_DATA) {
		bp = allocb(sizeof (struct T_unitdata_req) + tp->tim_peerlen,
		    BPRI_MED);
		if (bp == NULL)
			return (bp);
		bp->b_datap->db_type = M_PROTO;
		up = (struct T_unitdata_req *)bp->b_rptr;
		up->PRIM_type = T_UNITDATA_REQ;
		up->DEST_length = tp->tim_peerlen;
		bp->b_wptr += sizeof (struct T_unitdata_req);
		up->DEST_offset = sizeof (struct T_unitdata_req);
		up->OPT_length = 0;
		up->OPT_offset = 0;
		if (tp->tim_peerlen) {
		    bcopy((caddr_t) tp->tim_peername, (caddr_t) bp->b_wptr,
			tp->tim_peerlen);
		    bp->b_wptr += tp->tim_peerlen;
		}
		bp->b_cont = mp;
		return (bp);
	} else {
		ASSERT(mp->b_datap->db_type == M_PROTO);
		up = (struct T_unitdata_req *)mp->b_rptr;
		ASSERT(up->PRIM_type == T_UNITDATA_REQ);
		if (up->DEST_length != 0)
			return (mp);
		bp = allocb(sizeof (struct T_unitdata_req) + up->OPT_length +
		    tp->tim_peerlen, BPRI_MED);
		if (bp == NULL)
			return (NULL);
		bp->b_datap->db_type = M_PROTO;
		nup = (struct T_unitdata_req *)bp->b_rptr;
		nup->PRIM_type = T_UNITDATA_REQ;
		nup->DEST_length = tp->tim_peerlen;
		bp->b_wptr += sizeof (struct T_unitdata_req);
		nup->DEST_offset = sizeof (struct T_unitdata_req);
		if (tp->tim_peerlen) {
		    bcopy((caddr_t) tp->tim_peername, (caddr_t) bp->b_wptr,
			tp->tim_peerlen);
		    bp->b_wptr += tp->tim_peerlen;
		}
		if (up->OPT_length == 0) {
			nup->OPT_length = 0;
			nup->OPT_offset = 0;
		} else {
			nup->OPT_length = up->OPT_length;
			nup->OPT_offset = sizeof (struct T_unitdata_req) +
			    tp->tim_peerlen;
			bcopy((caddr_t) (mp->b_wptr + up->OPT_offset),
			    (caddr_t) bp->b_wptr, up->OPT_length);
			bp->b_wptr += up->OPT_length;
		}
		bp->b_cont = mp->b_cont;
		mp->b_cont = NULL;
		freeb(mp);
		return (bp);
	}
}

static void
tim_addlink(tp)
	register struct tim_tim	*tp;
{
	queue_t *driverq;
	struct tim_tim **tpp;
	struct tim_tim	*next;

	/*
	 * Find my driver's read queue (for T_CON_RES handling)
	 */
	driverq = WR(tp->tim_rdq);
	while (SAMESTR(driverq))
		driverq = driverq->q_next;

	driverq = RD(driverq);

	tpp = &tim_hash[TIM_HASH(driverq)];
	rw_enter(&tim_list_rwlock, RW_WRITER);

	tp->tim_driverq = driverq;

	if ((next = *tpp) != NULL)
		next->tim_ptpn = &tp->tim_next;
	tp->tim_next = next;
	tp->tim_ptpn = tpp;
	*tpp = tp;

	tim_cnt++;

	rw_exit(&tim_list_rwlock);
}

static void
tim_dellink(tp)
	register struct tim_tim	*tp;
{
	register struct tim_tim	*next;

	rw_enter(&tim_list_rwlock, RW_WRITER);

	if ((next = tp->tim_next) != NULL)
		next->tim_ptpn = tp->tim_ptpn;
	*(tp->tim_ptpn) = next;

	tim_cnt--;
	if (tp->tim_rdq != NULL)
		tp->tim_rdq->q_ptr = WR(tp->tim_rdq)->q_ptr = NULL;

	kmem_free(tp, sizeof (struct tim_tim));

	rw_exit(&tim_list_rwlock);
}

static struct tim_tim *
tim_findlink(driverq)
	queue_t *driverq;
{
	register struct tim_tim	*tp;

	ASSERT(rw_lock_held(&tim_list_rwlock));

	for (tp = tim_hash[TIM_HASH(driverq)]; tp != NULL; tp = tp->tim_next) {
		if (tp->tim_driverq == driverq) {
			break;
		}
	}
	return (tp);
}

static void
tim_recover(q, mp, size)
	queue_t		*q;
	mblk_t		*mp;
	int		size;
{
	struct tim_tim	*tp;
	int		id;

	tp = (struct tim_tim *)q->q_ptr;

	/*
	 * Avoid re-enabling the queue.
	 */
	if (mp->b_datap->db_type == M_PCPROTO)
		mp->b_datap->db_type = M_PROTO;
	noenable(q);
	(void) putbq(q, mp);

	/*
	 * Make sure there is at most one outstanding request per queue.
	 */
	if (q->q_flag & QREADR) {
		if (tp->tim_rtimoutid || tp->tim_rbufcid)
			return;
	} else {
		if (tp->tim_wtimoutid || tp->tim_wbufcid)
			return;
	}
	if (!(id = qbufcall(RD(q), size, BPRI_MED, tim_buffer, (long)q))) {
		id = qtimeout(RD(q), tim_timer, (caddr_t)q, TIMWAIT);
		if (q->q_flag & QREADR)
			tp->tim_rtimoutid = id;
		else	tp->tim_wtimoutid = id;
	} else	{
		if (q->q_flag & QREADR)
			tp->tim_rbufcid = id;
		else	tp->tim_wbufcid = id;
	}
}
