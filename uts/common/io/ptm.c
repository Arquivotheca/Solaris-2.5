/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma	ident	"@(#)ptm.c	1.32	94/11/17 SMI"

#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/stream.h>
#include <sys/stropts.h>
#include <sys/proc.h>
#include <sys/errno.h>
#include <sys/debug.h>
#include <sys/cmn_err.h>
#include <sys/ptms.h>
#include <sys/stat.h>
#include <sys/systm.h>
#include <sys/modctl.h>
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>

#ifdef DBUG
int ptm_debug = 0;
#define	DBG(a)	 if (ptm_debug) cmn_err(CE_NOTE, a)
#else
#define	DBG(a)
#endif

static int ptmopen(queue_t *, dev_t *, int, int, cred_t *);
static int ptmclose(queue_t *, int, cred_t *);
static void ptmwput(queue_t *, mblk_t *);
static void ptmrsrv(queue_t *);
static void ptmwsrv(queue_t *);

/*
 * Master Stream Pseudo Terminal Module: stream data structure definitions
 */

static struct module_info ptm_info = {
	0xdead,
	"ptm",
	0,
	512,
	512,
	128
};

static struct qinit ptmrint = {
	NULL,
	(int (*)()) ptmrsrv,
	ptmopen,
	ptmclose,
	NULL,
	&ptm_info,
	NULL
};

static struct qinit ptmwint = {
	(int (*)()) ptmwput,
	(int (*)()) ptmwsrv,
	NULL,
	NULL,
	NULL,
	&ptm_info,
	NULL
};

static struct streamtab ptminfo = {
	&ptmrint,
	&ptmwint,
	NULL,
	NULL
};

static int ptm_identify(dev_info_t *);
static int ptm_attach(dev_info_t *, ddi_attach_cmd_t);
static int ptm_detach(dev_info_t *, ddi_detach_cmd_t);
static int ptm_devinfo(dev_info_t *, ddi_info_cmd_t, void *, void **);

static dev_info_t	*ptm_dip;		/* private devinfo pointer */

static void ptm_init(void), ptm_uninit(void);	/* move into _init/_fini */

/*
 * The objective is to protect the association of a slave to a
 * master, and the association of pty to q.
 * A global mutex "pt_lock" is used to protect the access to the counter
 * "pt_access" in each struct ptms. The scheme is to
 * get this mutex before increasing pt_access and then dropping it.
 * The condition variable field "pt_cv" allows for exclusive
 * write access (needed by open/close).
 */

#define	PTM_CONF_FLAG	(D_NEW | D_MP)

/*
 * this will define (struct cb_ops cb_ptm_ops) and (struct dev_ops ptm_ops)
 */
DDI_DEFINE_STREAM_OPS(ptm_ops, ptm_identify, nulldev,	\
	ptm_attach, ptm_detach, nodev,			\
	ptm_devinfo, PTM_CONF_FLAG, &ptminfo);

/*
 * Module linkage information for the kernel.
 */

static struct modldrv modldrv = {
	&mod_driverops, /* Type of module.  This one is a pseudo driver */
	"Master streams driver 'ptm'",
	&ptm_ops,	/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1,
	&modldrv,
	NULL
};

int
_init(void)
{
	register int rc;

	ptm_init();
	if ((rc = mod_install(&modlinkage)) != 0) {
		ptm_uninit();
	}
	return (rc);
}


int
_fini(void)
{
	register int rc;

	if ((rc = mod_remove(&modlinkage)) == 0)
		ptm_uninit();
	return (rc);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

static int
ptm_identify(dev_info_t *devi)
{
	if (strcmp(ddi_get_name(devi), "ptm") == 0)
		return (DDI_IDENTIFIED);
	else
		return (DDI_NOT_IDENTIFIED);
}

static int
ptm_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	if (cmd != DDI_ATTACH)
		return (DDI_FAILURE);

	if (ddi_create_minor_node(devi, "ptmajor", S_IFCHR,
	    0, NULL, NULL) == DDI_FAILURE) {
		ddi_remove_minor_node(devi, NULL);
		return (DDI_FAILURE);
	}
	if (ddi_create_minor_node(devi, "ptmx", S_IFCHR,
	    0, NULL, CLONE_DEV) == DDI_FAILURE) {
		ddi_remove_minor_node(devi, NULL);
		return (DDI_FAILURE);
	}
	ptm_dip = devi;
	return (DDI_SUCCESS);
}

static int
ptm_detach(dev_info_t *devi, ddi_detach_cmd_t cmd)
{
	if (cmd != DDI_DETACH)
		return (DDI_FAILURE);

	ddi_remove_minor_node(devi, NULL);
	return (DDI_SUCCESS);
}

/*ARGSUSED*/
static int
ptm_devinfo(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg,
    void **result)
{
	register int error;

	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		if (ptm_dip == NULL) {
			error = DDI_FAILURE;
		} else {
			*result = (void *) ptm_dip;
			error = DDI_SUCCESS;
		}
		break;
	case DDI_INFO_DEVT2INSTANCE:
		*result = (void *)0;
		error = DDI_SUCCESS;
		break;
	default:
		error = DDI_FAILURE;
	}
	return (error);
}


static void
ptm_init(void)
{
	register u_long dev;

	mutex_init(&pt_lock, "ptm/pts lock", MUTEX_DEFAULT, NULL);
	for (dev = 0; dev < pt_cnt; dev++)
		cv_init(&ptms_tty[dev].pt_cv, "ptms cv", CV_DEFAULT, NULL);
}

static void
ptm_uninit(void)
{
	register u_long	dev;

	mutex_destroy(&pt_lock);
	for (dev = 0; dev < pt_cnt; dev++)
		cv_destroy(&ptms_tty[dev].pt_cv);
}


/*
 * Open a minor of the master device. Find an unused entry in the
 * ptms_tty array. Store the write queue pointer and set the
 * pt_state field to (PTMOPEN | PTLOCK).
 */
static int
ptmopen(
	register queue_t *rqp,	/* pointer to the read side queue */
	dev_t   *devp,		/* pointer to stream tail's dev */
	int	oflag,		/* the user open(2) supplied flags */
	int	sflag,		/* open state flag */
	cred_t  *credp)		/* credentials */
{
	register struct pt_ttys	*ptmp;
	register mblk_t		*mop;	/* ptr to a setopts message block */
	register minor_t	dev;

	DBG(("entering ptmopen\n"));

	if (sflag != CLONEOPEN) {
		DBG(("invalid sflag\n"));
		return (EINVAL);
	}

	for (dev = 0; dev < pt_cnt; dev++) {
		ptmp = &ptms_tty[dev];
		PT_ENTER_WRITE(ptmp);
		if (ptmp->pt_state & (PTMOPEN | PTSOPEN | PTLOCK)) {
			PT_EXIT_WRITE(ptmp);
		} else
			break;
	}

	if (dev >= pt_cnt) {
		DBG(("no more devices left to allocate\n"));
		return (ENODEV);
	}

	/*
	 * set up the entries in the pt_ttys structure for this
	 * device.
	 */
	ptmp->pt_state = (PTMOPEN | PTLOCK);
	ptmp->ptm_rdq = rqp;
	ptmp->pts_rdq = NULL;
	ptmp->tty = curproc->p_pgrp;

	/*
	 * If the slave is already opened and pt_bufp is a real
	 * message, this is an error.
	 */
	if (ptmp->pt_bufp != NULL)
		cmn_err(CE_PANIC, "ptmopen: slave already open dev=%d",
		    (int)dev);

	ptmp->pt_bufp = NULL;
	WR(rqp)->q_ptr = (char *) ptmp;
	rqp->q_ptr = (char *) ptmp;
	PT_EXIT_WRITE(ptmp);
	qprocson(rqp);

	PT_ENTER_READ(ptmp);
	if (ptmp->pts_rdq) {
		if (ptmp->pts_rdq->q_next) {
			DBG(("send hangup to an already existing slave\n"));
			putnextctl(ptmp->pts_rdq, M_HANGUP);
		}
	}
	PT_EXIT_READ(ptmp);

	/*
	 * set up hi/lo water marks on stream head read queue
	 * and add controlling tty if not set
	 */
	if (mop = allocb(sizeof (struct stroptions), BPRI_MED)) {
		register struct stroptions *sop;

		mop->b_datap->db_type = M_SETOPTS;
		mop->b_wptr += sizeof (struct stroptions);
		sop = (struct stroptions *)mop->b_rptr;
		sop->so_flags = SO_HIWAT | SO_LOWAT | SO_ISTTY;
		sop->so_hiwat = 512;
		sop->so_lowat = 256;
		putnext(rqp, mop);
	} else {
		(void) ptmclose(rqp, oflag, credp);
		return (EAGAIN);
	}

	DBG(("returning from ptmopen()\n"));
	/*
	 * The input, devp, is a major device number, the output is put into
	 * into the same parm as a major,minor pair.
	 */
	*devp = makedevice(getmajor(*devp), dev);
	return (0);
}


/*
 * Find the address to private data identifying the slave's write queue.
 * Send a hang-up message up the slave's read queue to designate the
 * master/slave pair is tearing down. Uattach the master and slave by
 * nulling out the write queue fields in the private data structure.
 * Finally, unlock the master/slave pair and mark the master as closed.
 */
/*ARGSUSED1*/
static int
ptmclose(register queue_t *rqp,	int flag, cred_t *credp)
{
	register struct pt_ttys	*ptmp;
	register queue_t	*pts_rdq;

	ASSERT(rqp->q_ptr);

	DBG(("entering ptmclose\n"));
	ptmp = (struct pt_ttys *)rqp->q_ptr;
	PT_ENTER_READ(ptmp);
	if (ptmp->pts_rdq) {
		pts_rdq = ptmp->pts_rdq;
		if (pts_rdq->q_next) {
			DBG(("send hangup message to slave\n"));
			putnextctl(pts_rdq, M_HANGUP);
		}
	}
	PT_EXIT_READ(ptmp);
	/*
	 * This is needed in order to make the clearing of ptm_rdq
	 * occur before qprocsoff (in order to prevent the slave
	 * from passing up messages to ptm_rdq->q_next).
	 */
	PT_ENTER_WRITE(ptmp);
	ptmp->ptm_rdq = NULL;
	/*
	 * qenable slave side write queue so that it can flush
	 * its messages as master's read queue is going away
	 */
	if (ptmp->pts_rdq)
		qenable(WR(ptmp->pts_rdq));
	PT_EXIT_WRITE(ptmp);
	/*
	 * Drop the lock across qprocsoff to avoid deadlock with the service
	 * procedures.
	 */
	qprocsoff(rqp);
	PT_ENTER_WRITE(ptmp);
	freemsg(ptmp->pt_bufp);
	ptmp->pt_bufp = NULL;
	ptmp->pt_state &= ~(PTMOPEN | PTLOCK);
	ptmp->tty = 0;
	rqp->q_ptr = NULL;
	WR(rqp)->q_ptr = NULL;

	DBG(("returning from ptmclose\n"));
	PT_EXIT_WRITE(ptmp);
	return (0);
}

/*
 * The wput procedure will only handle ioctl and flush messages.
 */
static void
ptmwput(queue_t *qp, mblk_t *mp)
{
	register struct pt_ttys	*ptmp;
	struct iocblk		*iocp;

	DBG(("entering ptmwput\n"));
	ASSERT(qp->q_ptr);

	ptmp = (struct pt_ttys *) qp->q_ptr;
	PT_ENTER_READ(ptmp);

	switch (mp->b_datap->db_type) {
	/*
	 * if write queue request, flush master's write
	 * queue and send FLUSHR up slave side. If read
	 * queue request, convert to FLUSHW and putctl1().
	 */
	case M_FLUSH:
		DBG(("ptm got flush request\n"));
		if (*mp->b_rptr & FLUSHW) {
			DBG(("flush ptm write Q\n"));
			flushq(qp, FLUSHDATA);
			if (ptmp->pts_rdq &&
			    !(ptmp->pt_state & PTLOCK)) {
				DBG(("putctl1 FLUSHR to pts\n"));
				putnextctl1(ptmp->pts_rdq,
					M_FLUSH, FLUSHR);
			}
		}
		if (*mp->b_rptr & FLUSHR) {
			DBG(("got read, putctl1 FLUSHW\n"));
			if (ptmp->pts_rdq &&
			    !(ptmp->pt_state & PTLOCK)) {
				putnextctl1(ptmp->pts_rdq,
					M_FLUSH, FLUSHW);
			}
		}
		freemsg(mp);
		break;

	case M_IOCTL:
		iocp = (struct iocblk *)mp->b_rptr;
		switch (iocp->ioc_cmd) {
		default:
			if ((ptmp->pt_state & PTLOCK) ||
			    (ptmp->pts_rdq == NULL)) {
				DBG(("got M_IOCTL but no slave\n"));
				mp->b_datap->db_type = M_IOCNAK;
				freemsg(mp->b_cont);
				mp->b_cont = NULL;
				qreply(qp, mp);
				PT_EXIT_READ(ptmp);
				return;
			}
			(void) putq(qp, mp);
			break;
		case UNLKPT:
			ptmp->pt_state &= ~PTLOCK;
			/*FALLTHROUGH*/
		case ISPTM:
			mp->b_datap->db_type = M_IOCACK;
			iocp->ioc_error = 0;
			iocp->ioc_count = 0;
			DBG(("ack the UNLKPT/ISPTM\n"));
			qreply(qp, mp);
			break;
		}
		break;

	case M_READ:
		/* Caused by ldterm - can not pass to slave */
		freemsg(mp);
		break;

	/*
	 * send other messages to slave
	 */
	default:
		if ((ptmp->pt_state  & PTLOCK) ||
		    (ptmp->pts_rdq == NULL)) {
			DBG(("got msg. but no slave\n"));
			putnextctl1(RD(qp), M_ERROR, EINVAL);
			PT_EXIT_READ(ptmp);
			freemsg(mp);
			return;
		}
		DBG(("put msg on master's write queue\n"));
		(void) putq(qp, mp);
		break;
	}
	DBG(("return from ptmwput()\n"));
	PT_EXIT_READ(ptmp);
}


/*
 * enable the write side of the slave. This triggers the
 * slave to send any messages queued on its write side to
 * the read side of this master.
 */
static void
ptmrsrv(queue_t *qp)
{
	register struct pt_ttys	*ptmp;

	DBG(("entering ptmrsrv\n"));
	ASSERT(qp->q_ptr);

	ptmp = (struct pt_ttys *) qp->q_ptr;
	PT_ENTER_READ(ptmp);
	if (ptmp->pts_rdq) {
		qenable(WR(ptmp->pts_rdq));
	}
	PT_EXIT_READ(ptmp);
	DBG(("leaving ptmrsrv\n"));
}


/*
 * If there are messages on this queue that can be sent to
 * slave, send them via putnext(). Else, if queued messages
 * cannot be sent, leave them on this queue. If priority
 * messages on this queue, send them to slave no matter what.
 */
static void
ptmwsrv(queue_t	*qp)
{
	register struct pt_ttys	*ptmp;
	mblk_t			*mp;

	DBG(("entering ptmwsrv\n"));
	ASSERT(qp->q_ptr);

	ptmp = (struct pt_ttys *) qp->q_ptr;
	PT_ENTER_READ(ptmp);
	if ((ptmp->pt_state  & PTLOCK) || (ptmp->pts_rdq == NULL)) {
		DBG(("in master write srv proc but no slave\n"));
		/*
		 * Free messages on the write queue and send
		 * NAK for any M_IOCTL type messages to wakeup
		 * the user process waiting for ACK/NAK from
		 * the ioctl invocation
		 */

		while ((mp = getq(qp)) != NULL) {
			if (mp->b_datap->db_type == M_IOCTL) {
				mp->b_datap->db_type = M_IOCNAK;
				freemsg(mp->b_cont);
				mp->b_cont = NULL;
				qreply(qp, mp);
			} else
				freemsg(mp);
		}
		flushq(qp, FLUSHALL);
		putnextctl1(RD(qp), M_ERROR, EINVAL);
		PT_EXIT_READ(ptmp);
		return;
	}
	/*
	 * while there are messages on this write queue...
	 */
	while ((mp = getq(qp)) != NULL) {
		/*
		 * if don't have control message and cannot put
		 * msg. on slave's read queue, put it back on
		 * this queue.
		 */
		if (mp->b_datap->db_type <= QPCTL &&
		    !canputnext(ptmp->pts_rdq)) {
			DBG(("put msg. back on queue\n"));
			(void) putbq(qp, mp);
			break;
		}
		/*
		 * else send the message up slave's stream
		 */
		DBG(("send message to slave\n"));
		putnext(ptmp->pts_rdq, mp);
	}
	DBG(("leaving ptmwsrv\n"));
	PT_EXIT_READ(ptmp);
}
