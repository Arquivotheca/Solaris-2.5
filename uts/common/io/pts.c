/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma	ident	"@(#)pts.c	1.36	95/03/14 SMI"	/* SVR4 1.13	*/

#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/stream.h>
#include <sys/stropts.h>
#include <sys/stat.h>
#include <sys/errno.h>
#include <sys/debug.h>
#include <sys/cmn_err.h>
#include <sys/ptms.h>
#include <sys/systm.h>
#include <sys/modctl.h>
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>

#ifdef DBUG
int pts_debug = 0;
#define	DBG(a)	 if (pts_debug) cmn_err(CE_NOTE, a)
#else
#define	DBG(a)
#endif

static int ptsopen(queue_t *, dev_t *, int, int, cred_t *);
static int ptsclose(queue_t *, int, cred_t *);
static void ptswput(queue_t *, mblk_t *);
static void ptsrsrv(queue_t *);
static void ptswsrv(queue_t *);

/*
 * Slave Stream Pseudo Terminal Module: stream data structure definitions
 */

static struct module_info pts_info = {
	0xface,
	"pts",
	0,
	512,
	512,
	128
};

static struct qinit ptsrint = {
	NULL,
	(int (*)()) ptsrsrv,
	ptsopen,
	ptsclose,
	NULL,
	&pts_info,
	NULL
};

static struct qinit ptswint = {
	(int (*)()) ptswput,
	(int (*)()) ptswsrv,
	NULL,
	NULL,
	NULL,
	&pts_info,
	NULL
};

static struct streamtab ptsinfo = {
	&ptsrint,
	&ptswint,
	NULL,
	NULL
};

static int pts_devinfo(dev_info_t *, ddi_info_cmd_t, void *, void **);
static int pts_identify(dev_info_t *);
static int pts_attach(dev_info_t *, ddi_attach_cmd_t);
static int pts_detach(dev_info_t *, ddi_detach_cmd_t);

static dev_info_t *pts_dip;	/* private copy of devinfo ptr */

#define	PTS_CONF_FLAG	(D_NEW | D_MP)

/*
 * this will define (struct cb_ops cb_pts_ops) and (struct dev_ops pts_ops)
 */
DDI_DEFINE_STREAM_OPS(pts_ops, pts_identify, nulldev,	\
	pts_attach, pts_detach, nodev,			\
	pts_devinfo, PTS_CONF_FLAG, &ptsinfo);

/*
 * Module linkage information for the kernel.
 */

static struct modldrv modldrv = {
	&mod_driverops, /* Type of module.  This one is a pseudo driver */
	"Slave Stream Pseudo Terminal driver 'pts'",
	&pts_ops,	/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1,
	(void *)&modldrv,
	NULL
};

int
_init(void)
{
	return (mod_install(&modlinkage));
}


int
_fini(void)
{
	return (mod_remove(&modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

static int
pts_identify(dev_info_t *devi)
{
	if (strcmp(ddi_get_name(devi), "pts") == 0)
		return (DDI_IDENTIFIED);
	else
		return (DDI_NOT_IDENTIFIED);
}

static int
pts_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	int i;
	char name[7];		/* Enough room for 2^18 + one for the NUL */

	if (cmd != DDI_ATTACH)
		return (DDI_FAILURE);

	for (i = 0; i < pt_cnt; i++) {
		sprintf(name, "%d", i);
		if (ddi_create_minor_node(devi, name, S_IFCHR,
		    i, NULL, NULL) == DDI_FAILURE) {
			ddi_remove_minor_node(devi, NULL);
			return (DDI_FAILURE);
		}
	}
	pts_dip = devi;
	return (DDI_SUCCESS);
}

static int
pts_detach(dev_info_t *devi, ddi_detach_cmd_t cmd)
{
	if (cmd != DDI_DETACH)
		return (DDI_FAILURE);

	ddi_remove_minor_node(devi, NULL);
	return (DDI_SUCCESS);
}

/*ARGSUSED*/
static int
pts_devinfo(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg,
    void **result)
{
	register int error;

	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		if (pts_dip == NULL) {
			error = DDI_FAILURE;
		} else {
			*result = (void *) pts_dip;
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


/*
 * Open the master device. Reject a clone open and do not allow the
 * driver to be pushed. If the slave/master pair is locked or if
 * the slave is not open, return OPENFAIL. If cannot allocate zero
 * length data buffer, fail open.
 * Upon success, store the write queue pointer in private data and
 * set the PTSOPEN bit in the sflag field.
 */
static int
ptsopen(
	register queue_t *rqp,	/* pointer to the read side queue */
	dev_t   *devp,		/* pointer to stream tail's dev */
	int	oflag,		/* the user open(2) supplied flags */
	int	sflag,		/* open state flag */
	cred_t  *credp)		/* credentials */
{
	register struct pt_ttys	*ptsp;
	register mblk_t		*mp;
	register mblk_t		*mop;	/* ptr to a setopts message block */
	register minor_t	dev;

	DBG(("entering ptsopen\n"));

	dev = getminor(*devp);
	if (sflag) {
		DBG(("sflag is set\n"));
		return (EINVAL);
	}
	if (dev > pt_cnt) {
		DBG(("invalid minor number\n"));
		return (ENODEV);
	}

	ptsp = &ptms_tty[dev];
	PT_ENTER_WRITE(ptsp);
	if ((ptsp->pt_state & PTLOCK) || !(ptsp->pt_state & PTMOPEN)) {
		DBG(("master is locked or slave is closed\n"));
		PT_EXIT_WRITE(ptsp);
		return (EACCES);
	}

	/*
	 * if already, open simply return...
	 */
	if (ptsp->pt_state & PTSOPEN) {
		DBG(("master already open\n"));
		PT_EXIT_WRITE(ptsp);
		return (0);
	}

	if (!(mp = allocb(0, BPRI_MED))) {
		PT_EXIT_WRITE(ptsp);
		DBG(("could not allocb(0, pri)\n"));
		return (EAGAIN);
	}
	ptsp->pt_bufp = mp;
	WR(rqp)->q_ptr = (char *) ptsp;
	rqp->q_ptr = (char *) ptsp;
	ptsp->pt_state |= PTSOPEN;
	PT_EXIT_WRITE(ptsp);
	qprocson(rqp);

	/*
	 * Set pts_rdq after the call to qprocson()
	 */
	PT_ENTER_WRITE(ptsp);
	ptsp->pts_rdq = rqp;
	PT_EXIT_WRITE(ptsp);

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
		(void) ptsclose(rqp, oflag, credp);
		return (EAGAIN);
	}

	DBG(("returning from ptsopen\n"));
	return (0);
}



/*
 * Find the address to private data identifying the slave's write
 * queue. Send a 0-length msg up the slave's read queue to designate
 * the master is closing. Uattach the master from the slave by nulling
 * out master's write queue field in private data.
 */
/*ARGSUSED1*/
static int
ptsclose(queue_t *rqp, int flag, cred_t *credp)
{
	register struct pt_ttys	*ptsp;
	register queue_t *wqp;
	mblk_t *mp, *bp;

	DBG(("entering ptsclose\n"));
	/*
	 * if no private data...
	 */
	if (!rqp->q_ptr) {
		qprocsoff(rqp);
		return (0);
	}

	ptsp = (struct pt_ttys *)rqp->q_ptr;
	PT_ENTER_WRITE(ptsp);
	mp = ptsp->pt_bufp;
	ptsp->pt_bufp = NULL;
	/*
	 * This is needed in order to make the clearing of pts_rdq
	 * occur before qprocsoff (in order to prevent the master
	 * from passing up messages to pts_rdq->q_next).
	 */
	ptsp->pts_rdq = NULL;
	PT_EXIT_WRITE(ptsp);
	PT_ENTER_READ(ptsp);
	/*
	 * Drain the ouput
	 */
	wqp = WR(rqp);
	while ((bp = getq(wqp)) != NULL) {
		if (ptsp->ptm_rdq)
			putnext(ptsp->ptm_rdq, bp);
		else {
			if (bp->b_datap->db_type == M_IOCTL) {
				bp->b_datap->db_type = M_IOCNAK;
				freemsg(bp->b_cont);
				bp->b_cont = NULL;
				qreply(wqp, bp);
			} else
				freemsg(bp);
		}
	}
	/*
	 * qenable master side write queue so that it can flush
	 * its messages as slaves's read queue is going away
	 */
	if (ptsp->ptm_rdq) {
		if (mp)
			putnext(ptsp->ptm_rdq, mp);
		else
			qenable(WR(ptsp->ptm_rdq));
	} else
		freemsg(mp);
	PT_EXIT_READ(ptsp);
	/*
	 * Drop the lock across qprocsoff to avoid deadlock with the service
	 * procedures.
	 */
	qprocsoff(rqp);
	PT_ENTER_WRITE(ptsp);
	ptsp->pt_state &= ~PTSOPEN;
	ptsp->tty = 0;
	rqp->q_ptr = NULL;
	WR(rqp)->q_ptr = NULL;
	PT_EXIT_WRITE(ptsp);

	DBG(("returning from ptsclose\n"));
	return (0);
}


/*
 * The wput procedure will only handle flush messages.
 * All other messages are queued and the write side
 * service procedure sends them off to the master side.
 */
static void
ptswput(queue_t *qp, mblk_t *mp)
{
	register struct pt_ttys *ptsp;

	DBG(("entering ptswput\n"));
	ASSERT(qp->q_ptr);

	ptsp = (struct pt_ttys *) qp->q_ptr;
	PT_ENTER_READ(ptsp);
	if (ptsp->ptm_rdq == NULL) {
		DBG(("in write put proc but no master\n"));
		/*
		 * NAK ioctl as slave side read queue is gone.
		 * Or else free the message.
		 */
		if (mp->b_datap->db_type == M_IOCTL) {
			mp->b_datap->db_type = M_IOCNAK;
			freemsg(mp->b_cont);
			mp->b_cont = NULL;
			qreply(qp, mp);
		} else
			freemsg(mp);
		PT_EXIT_READ(ptsp);
		return;
	}
	switch (mp->b_datap->db_type) {
	/*
	 * if write queue request, flush slave's write
	 * queue and send FLUSHR to ptm. If read queue
	 * request, send FLUSHR to ptm.
	 */
	case M_FLUSH:
		DBG(("pts got flush request\n"));
		if (*mp->b_rptr & FLUSHW) {
			DBG(("flush pts write Q\n"));
			flushq(qp, FLUSHDATA);
			DBG(("putctl1 FLUSHR to ptm\n"));
			putnextctl1(ptsp->ptm_rdq, M_FLUSH, FLUSHR);
		}
		/*
		 * Since the packet module will toss any
		 * M_FLUSHES sent to the master's stream head
		 * read queue, we simply turn it around here.
		 */
		if (*mp->b_rptr & FLUSHR) {
			flushq(RD(qp), FLUSHDATA);
			DBG(("putnextctl1 turning FLUSHR around\n"));
			putnextctl1(RD(qp), M_FLUSH, FLUSHR);
		}
		freemsg(mp);
		break;

	case M_READ:
		/* Caused by ldterm - can not pass to master */
		freemsg(mp);
		break;

	default:
		/*
		 * send other messages to the master
		 */
		DBG(("put msg on slave's write queue\n"));
		(void) putq(qp, mp);
		break;
	}
	PT_EXIT_READ(ptsp);
	DBG(("return from ptswput()\n"));
}


/*
 * enable the write side of the master. This triggers the
 * master to send any messages queued on its write side to
 * the read side of this slave.
 */
static void
ptsrsrv(queue_t *qp)
{
	register struct pt_ttys *ptsp;

	DBG(("entering ptsrsrv\n"));
	ASSERT(qp->q_ptr);

	ptsp = (struct pt_ttys *) qp->q_ptr;
	PT_ENTER_READ(ptsp);
	if (ptsp->ptm_rdq == NULL) {
		DBG(("in read srv proc but no master\n"));
		PT_EXIT_READ(ptsp);
		return;
	}
	qenable(WR(ptsp->ptm_rdq));
	PT_EXIT_READ(ptsp);
	DBG(("leaving ptsrsrv\n"));
}

/*
 * If there are messages on this queue that can be sent to
 * master, send them via putnext(). Else, if queued messages
 * cannot be sent, leave them on this queue. If priority
 * messages on this queue, send them to master no matter what.
 */
static void
ptswsrv(queue_t *qp)
{
	register struct pt_ttys *ptsp;
	register queue_t *ptm_rdq;
	mblk_t *mp;

	DBG(("entering ptswsrv\n"));
	ASSERT(qp->q_ptr);

	ptsp = (struct pt_ttys *) qp->q_ptr;
	PT_ENTER_READ(ptsp);
	if (ptsp->ptm_rdq == NULL) {
		DBG(("in write srv proc but no master\n"));
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
		PT_EXIT_READ(ptsp);
		return;
	} else {
		ptm_rdq = ptsp->ptm_rdq;
	}

	/*
	 * while there are messages on this write queue...
	 */
	while ((mp = getq(qp)) != NULL) {
		/*
		 * if don't have control message and cannot put
		 * msg. on master's read queue, put it back on
		 * this queue.
		 */
		if (mp->b_datap->db_type <= QPCTL &&
		    !canputnext(ptm_rdq)) {
			DBG(("put msg. back on Q\n"));
			(void) putbq(qp, mp);
			break;
		}
		/*
		 * else send the message up master's stream
		 */
		DBG(("send message to master\n"));
		putnext(ptm_rdq, mp);
	}
	DBG(("leaving ptswsrv\n"));
	PT_EXIT_READ(ptsp);
}
