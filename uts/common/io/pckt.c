/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)pckt.c	1.29	94/07/15 SMI"	/* from S5R4 1.10	*/

/*
 * Description: The pckt module packetizes messages on
 *		its read queue by pre-fixing an M_PROTO
 *		message type to certain incoming messages.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/stream.h>
#include <sys/stropts.h>
#include <sys/kmem.h>
#include <sys/errno.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>

/*
 * This is the loadable module wrapper.
 */
#include <sys/conf.h>
#include <sys/modctl.h>

static struct streamtab pcktinfo;

/*
 * Per queue instances are single-threaded since the q_ptr
 * field of queues need to be shared among threads.
 */
static struct fmodsw fsw = {
	"pckt",
	&pcktinfo,
	D_NEW | D_MTPERQ | D_MP
};

/*
 * Module linkage information for the kernel.
 */
extern struct mod_ops mod_strmodops;

static struct modlstrmod modlstrmod = {
	&mod_strmodops,
	"pckt module",
	&fsw
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modlstrmod, NULL
};


_init()
{
	return (mod_install(&modlinkage));
}

_fini()
{
	return (mod_remove(&modlinkage));
}

_info(
	struct modinfo *modinfop
)
{
	return (mod_info(&modlinkage, modinfop));
}

static int	pcktopen(queue_t *q, dev_t *devp, int oflag, int sflag,
			cred_t *credp);
static int	pcktclose(queue_t *q, int flag, cred_t *credp);
static void	pcktrput(queue_t *q, mblk_t *mp);
static void	pcktrsrv(queue_t *q);
static void	pcktwput(queue_t *q, mblk_t *mp);
static mblk_t	*add_ctl_info(queue_t *q, mblk_t *mp);
static void	add_ctl_wkup(queue_t *q);


/*
 * Stream module data structure definitions.
 * Sits over the ptm module generally.
 *
 * Read side flow control strategy: Since we may be putting messages on
 * the read q due to allocb failures, these failures must get
 * reflected fairly quickly to the module below us.
 * No sense in piling on messages in times of memory shortage.
 * Further, for the case of upper level flow control, there is no
 * compelling reason to have more buffering in this module.
 * Thus use a hi-water mark of one.
 * This module imposes no max packet size, there is no inherent reason
 * in the code to do so.
 */
static struct module_info pcktiinfo = {
	0x9898,					/* module id number */
	"pckt",					/* module name */
	0,					/* minimum packet size */
	INFPSZ,					/* maximum packet size */
	1,					/* hi-water mark */
	0					/* lo-water mark */
};

/*
 * Write side flow control strategy: There is no write service procedure.
 * The write put function is pass thru, thus there is no reason to have any
 * limits on the maximum packet size.
 */
static struct module_info pcktoinfo = {
	0x9898,					/* module id number */
	"pckt",					/* module name */
	0,					/* minimum packet size */
	INFPSZ,					/* maximum packet size */
	0,					/* hi-water mark */
	0					/* lo-water mark */
};

static struct qinit pcktrinit = {
	(int (*)())pcktrput,
	(int (*)())pcktrsrv,
	pcktopen,
	pcktclose,
	(int (*)())NULL,
	&pcktiinfo,
	NULL
};

static struct qinit pcktwinit = {
	(int (*)())pcktwput,
	(int (*)())NULL,
	(int (*)())NULL,
	(int (*)())NULL,
	(int (*)())NULL,
	&pcktoinfo,
	NULL
};

static struct streamtab pcktinfo = {
	&pcktrinit,
	&pcktwinit,
	NULL,
	NULL
};


/*
 * Per-instance state struct for the pckt module.
 */
struct pckt_info
{
	queue_t		*pi_qptr;		/* back pointer to q */
	int		pi_bufcall_id;
};

/*
 * Dummy qbufcall callback routine used by open and close.
 * The framework will wake up qwait_sig when we return from
 * this routine (as part of leaving the perimeters.)
 * (The framework enters the perimeters before calling the qbufcall() callback
 * and leaves the perimeters after the callback routine has executed. The
 * framework performs an implicit wakeup of any thread in qwait/qwait_sig
 * when it leaves the perimeter. See qwait(9E).)
 */
/* ARGSUSED */
static void dummy_callback(arg)
	int arg;
{}


/*
 * pcktopen - open routine gets called when the
 *	    module gets pushed onto the stream.
 */
/*ARGSUSED*/
static int
pcktopen(
	register queue_t *q,	/* pointer to the read side queue */
	dev_t   *devp,		/* pointer to stream tail's dev */
	int	oflag,		/* the user open(2) supplied flags */
	int	sflag,		/* open state flag */
	cred_t  *credp		/* credentials */
)
{
	register struct pckt_info	*pip;
	register mblk_t			*mop; /* ptr to a setopts msg block */
	register struct stroptions	*sop;

	if (sflag != MODOPEN)
		return (EINVAL);

	if (q->q_ptr != NULL) {
		/* It's already attached. */
		return (0);
	}

	/*
	 * Allocate state structure.
	 */
	pip = (struct pckt_info *)kmem_alloc(sizeof (*pip), KM_SLEEP);
	bzero((caddr_t) pip, sizeof (*pip));

	/*
	 * Cross-link.
	 */
	pip->pi_qptr = q;
	q->q_ptr = (caddr_t)pip;
	WR(q)->q_ptr = (caddr_t)pip;

	qprocson(q);

	/*
	 * Initialize an M_SETOPTS message to set up hi/lo water marks on
	 * stream head read queue.
	 */

	while ((mop = allocb(sizeof (struct stroptions), BPRI_MED)) == NULL) {
		int id;

		id = qbufcall(q, sizeof (struct stroptions), BPRI_MED,
				dummy_callback, 0);
		if (!qwait_sig(q)) {
			qunbufcall(q, id);
			kmem_free(pip, sizeof (*pip));
			qprocsoff(q);
			return (EINTR);
		}
		qunbufcall(q, id);
	}


	/*
	 * XXX: Should this module really control the hi/low water marks?
	 * Is there any reason in this code to do so?
	 */
	mop->b_datap->db_type = M_SETOPTS;
	mop->b_wptr += sizeof (struct stroptions);
	sop = (struct stroptions *)mop->b_rptr;
	sop->so_flags = SO_HIWAT | SO_LOWAT;
	sop->so_hiwat = 512;
	sop->so_lowat = 256;

	/*
	 * Commit to the open and send the M_SETOPTS off to the stream head.
	 */
	putnext(q, mop);

	return (0);
}


/*
 * pcktclose - This routine gets called when the module
 *	gets popped off of the stream.
 */

/*ARGSUSED*/
static int
pcktclose(
	register queue_t *q,	/* Pointer to the read queue */
	int	flag,
	cred_t  *credp
)
{
	register struct pckt_info	*pip = (struct pckt_info *)q->q_ptr;

	qprocsoff(q);
	/*
	 * Cancel outstanding qbufcall
	 */
	if (pip->pi_bufcall_id) {
		qunbufcall(q, pip->pi_bufcall_id);
		pip->pi_bufcall_id = 0;
	}
	/*
	 * Do not worry about msgs queued on the q, the framework
	 * will free them up.
	 */
	kmem_free(q->q_ptr, sizeof (struct pckt_info));
	q->q_ptr = WR(q)->q_ptr = NULL;
	return (0);
}

/*
 * pcktrput - Module read queue put procedure.
 *	This is called from the module or
 *	driver downstream.
 */
static void
pcktrput(
	register queue_t *q,	/* Pointer to the read queue */
	register mblk_t *mp	/* Pointer to the current message block */
)
{
	register mblk_t		*pckt_msgp;


	switch (mp->b_datap->db_type) {
	case M_FLUSH:
		/*
		 * The PTS driver swaps the FLUSHR and FLUSHW flags
		 * we need to swap them back to reflect the actual
		 * slave side FLUSH mode.
		 */
		if ((*mp->b_rptr & FLUSHRW) != FLUSHRW)
			if ((*mp->b_rptr & FLUSHRW) == FLUSHR)
				*mp->b_rptr = FLUSHW;
			else if ((*mp->b_rptr & FLUSHRW) == FLUSHW)
				*mp->b_rptr = FLUSHR;

		pckt_msgp = copymsg(mp);
		if (*mp->b_rptr & FLUSHW) {
			/*
			 * In the packet model we are not allowing
			 * flushes of the master's stream head read
			 * side queue. This is because all packet
			 * state information is stored there and
			 * a flush could destroy this data before
			 * it is read.
			 */
			*mp->b_rptr = FLUSHW;
			putnext(q, mp);
		} else {
			/*
			 * Free messages that only flush the
			 * master's read queue.
			 */
			freemsg(mp);
		}

		if (pckt_msgp == NULL)
			break;

		mp = pckt_msgp;
		/*
		 * Prefix M_PROTO and putnext.
		 */
		goto prefix_head;

	case M_DATA:
	case M_IOCTL:
	case M_PROTO:
		/*
		 * For non-priority messages, follow flow-control rules.
		 * Also, if there are messages on the q already, keep
		 * queueing them since they need to be processed in order.
		 */
		if (!canputnext(q) || (qsize(q) > 0)) {
			(void) putq(q, mp);
			break;
		}
		/* FALLTHROUGH */

	/*
	 * For high priority messages, skip flow control checks.
	 */
	case M_PCPROTO:
	case M_READ:
	case M_STOP:
	case M_START:
	case M_STARTI:
	case M_STOPI:
prefix_head:
		/*
		 * Prefix an M_PROTO header to message and pass upstream.
		 */
		if ((mp = add_ctl_info(q, mp)) != NULL)
			putnext(q, mp);
		break;

	default:
		/*
		 * For data messages, queue them back on the queue if
		 * there are messages on the queue already. This is
		 * done to preserve the order of messages.
		 * For high priority messages or for no messages on the
		 * q, simply putnext() and pass it on.
		 */
		if ((datamsg(mp->b_datap->db_type)) && (qsize(q) > 0))
			(void) putq(q, mp);
		else
			putnext(q, mp);
		break;
	}
}

/*
 * pcktrsrv - module read service procedure
 * This function deals with messages left in the queue due to
 *	(a) not enough memory to allocate the header M_PROTO message
 *	(b) flow control reasons
 * The function will attempt to get the messages off the queue and
 * process them.
 */
static void
pcktrsrv(
	register queue_t	*q
)
{
	mblk_t			*mp;

	while ((mp = getq(q)) != NULL) {
		if (!canputnext(q)) {
			/*
			 * For high priority messages, make sure there is no
			 * infinite loop. Disable the queue for this case.
			 * High priority messages get here only for buffer
			 * allocation failures. Thus the bufcall callout
			 * will reenable the q.
			 * XXX bug alert - nooenable will *not* prevent
			 * putbq of a hipri messages frm enabling the queue.
			 */
			if (!datamsg(mp->b_datap->db_type))
				noenable(q);
			(void) putbq(q, mp);
			return;
		}

		/*
		 * M_FLUSH msgs may also be here if there was a memory
		 * failure.
		 */
		switch (mp->b_datap->db_type) {
		case M_FLUSH:
		case M_PROTO:
		case M_PCPROTO:
		case M_STOP:
		case M_START:
		case M_IOCTL:
		case M_DATA:
		case M_READ:
		case M_STARTI:
		case M_STOPI:
			/*
			 * Prefix an M_PROTO header to msg and pass upstream.
			 */
			if ((mp = add_ctl_info(q, mp)) == NULL) {
				/*
				 * Running into memory or flow ctl problems.
				 */
				return;
			}
			/* FALL THROUGH */

		default:
			putnext(q, mp);
			break;
		}
	}
}

/*
 * pcktwput - Module write queue put procedure.
 *	All messages are send downstream unchanged
 */

static void
pcktwput(
	register queue_t *q,	/* Pointer to the read queue */
	register mblk_t *mp	/* Pointer to current message block */
)
{
	putnext(q, mp);
}

/*
 * add_ctl_info: add message control information to in coming
 * 	message.
 */
static mblk_t *
add_ctl_info(
	register queue_t *q,	/* pointer to the read queue */
	mblk_t	*mp		/* pointer to the raw data input message */
)
{
	register struct pckt_info	*pip = (struct pckt_info *)q->q_ptr;
	register mblk_t	*bp;	/* pointer to the unmodified message block */

	/*
	 * Waiting on space for previous message?
	 */
	if (pip->pi_bufcall_id) {
		/*
		 * Chain this message on to q for later processing.
		 */
		(void) putq(q, mp);
		return (NULL);
	}

	/*
	 * Need to add the message block header as
	 * an M_PROTO type message.
	 */
	if ((bp = allocb(sizeof (char), BPRI_MED)) == (mblk_t *)NULL) {

		/*
		 * There are two reasons to disable the q:
		 * (1) Flow control reasons should not wake up the q.
		 * (2) High priority messages will wakeup the q
		 *	immediately. Disallow this.
		 */
		noenable(q);
		if (pip->pi_bufcall_id = qbufcall(q, sizeof (char), BPRI_MED,
			add_ctl_wkup, (long)q)) {
			/*
			 * Add the message to the q.
			 */
			(void) putq(q, mp);
		} else {
			/*
			 * Things are pretty bad and serious if bufcall fails!
			 * Drop the message in this case.
			 */
			freemsg(mp);
		}

		return (NULL);
	}

	/*
	 * Copy the message type information to this message.
	 */
	bp->b_datap->db_type = M_PROTO;
	*(unsigned char *)bp->b_rptr = mp->b_datap->db_type;
	bp->b_wptr++;

	/*
	 * Now change the orginal message type to M_DATA and tie them up.
	 */
	mp->b_datap->db_type = M_DATA;
	bp->b_cont = mp;

	return (bp);
}

static void
add_ctl_wkup(
	register queue_t	*q	/* ptr to the read queue */
)
{
	register struct pckt_info	*pip = (struct pckt_info *)q->q_ptr;

	pip->pi_bufcall_id = 0;
	/*
	 * Allow enabling of the q to allow the service
	 * function to do its job.
	 *
	 * Also, qenable() to schedule the q immediately.
	 * This is to ensure timely processing of high priority
	 * messages if they are on the q.
	 */
	enableok(q);
	qenable(q);
}
