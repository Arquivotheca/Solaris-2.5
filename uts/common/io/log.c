/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)log.c	1.32	94/11/08 SMI"	/* SVr4.0 1.14	*/

/*
 * Streams log driver.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/stropts.h>
#include <sys/stream.h>
#include <sys/strlog.h>
#include <sys/debug.h>
#include <sys/cred.h>
#include <sys/file.h>
#include <sys/ddi.h>
#include <sys/cmn_err.h>
#include <sys/stat.h>
#include <sys/syslog.h>
#include <sys/log.h>
#include <sys/msgbuf.h>

#include <sys/conf.h>
#include <sys/sunddi.h>


extern void loginit();
extern int shouldtrace(short mid, short sid, char level);
extern int log_internal(register mblk_t *mp, int seq_no, int type_flag);
extern int sendmsg(register struct log *lp, mblk_t *mp);
extern int log_errseq, log_trcseq, log_conseq;  /* logger sequence numbers */

/* now defined in space.c because log is loadable */
extern int numlogtrc;		/* number of processes reading trace log */
/* now defined in space.c because log is loadable */
extern int numlogerr;		/* number of processes reading error log */
/* now defined in space.c because log is loadable */
extern int numlogcons;		/* number of processes reading console log */

/* now defined in space.c because log is loadable */
extern int conslogging;		/* set when someone is logging console output */

/* now defined in space.c because log is loadable */
extern kmutex_t log_lock;

static int logopen(queue_t *q, dev_t *devp, int flag, int sflag, cred_t *cr);
static int logclose(queue_t *q, int flag, cred_t *cr);
static int logwput(queue_t *q, mblk_t *bp);
static int logrsrv(queue_t *q);

static struct module_info logm_info = {
	LOG_MID,
	LOG_NAME,
	LOG_MINPS,
	LOG_MAXPS,
	LOG_HIWAT,
	LOG_LOWAT
};

static struct qinit logrinit = {
	NULL,
	logrsrv,
	logopen,
	logclose,
	NULL,
	&logm_info,
	NULL
};

static struct qinit logwinit = {
	logwput,
	NULL,
	NULL,
	NULL,
	NULL,
	&logm_info,
	NULL
};

static struct streamtab loginfo = {
	&logrinit,
	&logwinit,
	NULL,
	NULL
};

static int log_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg,
		void **result);
static int log_identify(dev_info_t *devi);
static int log_attach(dev_info_t *devi, ddi_attach_cmd_t cmd);
static dev_info_t *log_dip;		/* private copy of devinfo pointer */

#define	LOG_CONF_FLAG		(D_NEW | D_MP)
	DDI_DEFINE_STREAM_OPS(log_ops, log_identify, nulldev,	\
			log_attach, nodev, nodev,		\
			log_info, LOG_CONF_FLAG, &loginfo);

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/modctl.h>


extern struct mod_ops mod_driverops;
static struct dev_ops log_ops;

/*
 * Module linkage information for the kernel.
 */

static struct modldrv modldrv = {
	&mod_driverops, /* Type of module.  This one is a pseudo driver */
	"streams log driver 'log'",
	&log_ops,	/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1,
	(void *)&modldrv,
	NULL
};


_init()
{
	int retval;

	retval = mod_install(&modlinkage);
	if (retval == 0)
		loginit();
	return (retval);
}


int
_fini()
{
	return (mod_remove(&modlinkage));
}

_info(modinfop)
	struct modinfo *modinfop;
{
	return (mod_info(&modlinkage, modinfop));
}

static int
log_identify(dev_info_t *devi)
{
	if (strcmp(ddi_get_name(devi), "log") == 0)
		return (DDI_IDENTIFIED);
	else
		return (DDI_NOT_IDENTIFIED);
}

/* ARGSUSED */
static int
log_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg,
	void **result)
{
	register int error;

	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		if (log_dip == NULL) {
			error = DDI_FAILURE;
		} else {
			*result = (void *) log_dip;
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

/* ARGSUSED */
static int
log_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	if (ddi_create_minor_node(devi, "conslog", S_IFCHR,
	    0, NULL, NULL) == DDI_FAILURE ||
	    ddi_create_minor_node(devi, "log", S_IFCHR,
	    5, NULL, NULL) == DDI_FAILURE) {
		ddi_remove_minor_node(devi, NULL);
		return (-1);
	}
	log_dip = devi;
	return (DDI_SUCCESS);
}


int logdevflag = 0;		/* new driver interface */

/*
 * Log_init() is now in strlog.c as it is called early on in main() via
 * the init table in param.c.
 */

/*
 * Log driver open routine.  Only two ways to get here.  Normal
 * access for loggers is through the clone minor.  Only one user
 * per clone minor.  Access to writing to the console log is
 * through the console minor.  Users can't read from this device.
 * Any number of users can have it open at one time.
 */
/* ARGSUSED */
static int
logopen(
	queue_t	*q,
	dev_t	*devp,
	int	flag,
	int	sflag,
	cred_t	*cr
)
{
	int i;
	struct log *lp;

	/*
	 * A MODOPEN is invalid and so is a CLONEOPEN.
	 * This is because a clone open comes in as a CLONEMIN device open!!
	 */
	if (sflag)
		return (ENXIO);

	mutex_enter(&log_lock);
	switch (getminor(*devp)) {

	case CONSWMIN:
		if (flag & FREAD) {	/* you can only write to this minor */
			mutex_exit(&log_lock);
			return (EINVAL);
		}
		if (q->q_ptr) {		/* already open */
			mutex_exit(&log_lock);
			return (0);
		}
		lp = &log_log[CONSWMIN];
		break;

	case CLONEMIN:
		/*
		 * Find an unused minor > CLONEMIN.
		 */
		i = CLONEMIN + 1;
		for (lp = &log_log[i]; i < log_cnt; i++, lp++) {
			if (!(lp->log_state & LOGOPEN))
				break;
		}
		if (i >= log_cnt) {
			mutex_exit(&log_lock);
			return (ENXIO);
		}
		*devp = makedevice(getmajor(*devp), i);	/* clone it */
		break;

	default:
		mutex_exit(&log_lock);
		return (ENXIO);
	}

	/*
	 * Finish device initialization.
	 */
	lp->log_state = LOGOPEN;
	lp->log_rdq = q;
	q->q_ptr = (caddr_t)lp;
	WR(q)->q_ptr = (caddr_t)lp;
	mutex_exit(&log_lock);
	qprocson(q);
	return (0);
}

/*
 * Log driver close routine.
 */
/* ARGSUSED */
static int
logclose(
	queue_t	*q,
	int	flag,
	cred_t	*cr
)
{
	struct log *lp;

	ASSERT(q->q_ptr);

	/*
	 * No more threads while we tear down the struct.
	 */
	qprocsoff(q);

	mutex_enter(&log_lock);
	lp = (struct log *)q->q_ptr;
	if (lp->log_state & LOGTRC) {
		freemsg(lp->log_tracemp);
		lp->log_tracemp = NULL;
		numlogtrc--;
	}
	if (lp->log_state & LOGERR)
		numlogerr--;
	if (lp->log_state & LOGCONS) {
		numlogcons--;
		if (numlogcons == 0)
			conslogging = 0;
	}
	lp->log_state = 0;
	lp->log_rdq = NULL;
	q->q_ptr = NULL;
	WR(q)->q_ptr = NULL;
	mutex_exit(&log_lock);

	/*
	 * Do not worry about msgs queued on the q, the framework
	 * will free them up.
	 */
	return (0);
}

/*
 * Write queue put procedure.
 */
static int
logwput(
	queue_t	*q,
	mblk_t	*bp
)
{
	register s;
	register struct iocblk *iocp;
	register struct log *lp;
	struct log_ctl *lcp;
	mblk_t *cbp, *pbp;
	int size;

	lp = (struct log *)q->q_ptr;
	switch (bp->b_datap->db_type) {
	case M_FLUSH:
		if (*bp->b_rptr & FLUSHW) {
			flushq(q, FLUSHALL);
			*bp->b_rptr &= ~FLUSHW;
		}
		if (*bp->b_rptr & FLUSHR) {
			flushq(RD(q), FLUSHALL);
			qreply(q, bp);
		} else {
			freemsg(bp);
		}
		break;

	case M_IOCTL:
		mutex_enter(&log_lock);
		if (lp == &log_log[CONSWMIN]) {	/* can not ioctl CONSWMIN */
			mutex_exit(&log_lock);
			goto lognak;
		}
		mutex_exit(&log_lock);
		iocp = (struct iocblk *)bp->b_rptr;
		if (iocp->ioc_count == TRANSPARENT)
			goto lognak;
		switch (iocp->ioc_cmd) {

		case I_CONSLOG:
			mutex_enter(&log_lock);
			if (lp->log_state & LOGCONS) {
				iocp->ioc_error = EBUSY;
				mutex_exit(&log_lock);
				goto lognak;
			}
			++numlogcons;

			lp->log_state |= LOGCONS;


			mutex_exit(&log_lock);

			if (!(cbp = allocb(sizeof (struct log_ctl), BPRI_HI))) {
				iocp->ioc_error = EAGAIN;
				mutex_enter(&log_lock);
				lp->log_state &= ~LOGCONS;
				numlogcons--;
				mutex_exit(&log_lock);
				goto lognak;
			}
			size = msgbuf_size();

			if (!(pbp = allocb(size, BPRI_HI))) {
				freeb(cbp);
				iocp->ioc_error = EAGAIN;
				mutex_enter(&log_lock);
				lp->log_state &= ~LOGCONS;
				numlogcons--;
				mutex_exit(&log_lock);
				goto lognak;
			}
			cbp->b_datap->db_type = M_PROTO;
			cbp->b_cont = pbp;
			cbp->b_wptr += sizeof (struct log_ctl);
			lcp = (struct log_ctl *)cbp->b_rptr;
			lcp->mid = LOG_MID;
			lcp->sid = lp - log_log;
			(void) drv_getparm(LBOLT, (unsigned long *)&lcp->ltime);
			(void) drv_getparm(TIME, (unsigned long *)&lcp->ttime);
			lcp->level = 0;
			lcp->flags = SL_CONSOLE;
			lcp->seq_no = log_conseq;
			lcp->pri = LOG_KERN|LOG_INFO;

			pbp->b_wptr = (u_char *)
			    msgbuf_get((caddr_t)pbp->b_wptr, size);

			mutex_enter(&log_lock);
			conslogging = 1;
			s = CLONEMIN + 1;
			for (lp = &log_log[s]; s < log_cnt; s++, lp++)
				if (lp->log_state & LOGCONS)
					(void) sendmsg(lp, cbp);
			freemsg(cbp);
			goto logack;

		case I_TRCLOG:
			mutex_enter(&log_lock);
			if (!(lp->log_state & LOGTRC) && bp->b_cont) {
				lp->log_tracemp = bp->b_cont;
				bp->b_cont = NULL;
				numlogtrc++;
				lp->log_state |= LOGTRC;
				goto logack;
			}
			mutex_exit(&log_lock);
			iocp->ioc_error = EBUSY;
			goto lognak;

		case I_ERRLOG:
			mutex_enter(&log_lock);
			if (!(lp->log_state & LOGERR)) {
				numlogerr++;
				lp->log_state |= LOGERR;

logack:
				mutex_exit(&log_lock);
				iocp->ioc_count = 0;
				bp->b_datap->db_type = M_IOCACK;
				qreply(q, bp);
				break;
			}
			mutex_exit(&log_lock);
			iocp->ioc_error = EBUSY;
			goto lognak;

		default:
lognak:
			bp->b_datap->db_type = M_IOCNAK;
			qreply(q, bp);
			break;
		}
		break;

	case M_PROTO:
		if (((bp->b_wptr - bp->b_rptr) != sizeof (struct log_ctl)) ||
		    !bp->b_cont) {
			freemsg(bp);
			break;
		}
		lcp = (struct log_ctl *)bp->b_rptr;
		if (lcp->flags & SL_ERROR) {
			if (numlogerr == 0) {
				lcp->flags &= ~SL_ERROR;
			} else {
				log_errseq++;
			}
		}
		if (lcp->flags & SL_TRACE) {
			if ((numlogtrc == 0) || !shouldtrace(LOG_MID,
			    (struct log *)(q->q_ptr) - log_log, lcp->level)) {
				lcp->flags &= ~SL_TRACE;
			} else {
				log_trcseq++;
			}
		}
		if (!(lcp->flags & (SL_ERROR|SL_TRACE|SL_CONSOLE))) {
			freemsg(bp);
			break;
		}
		(void) drv_getparm(LBOLT, (unsigned long *) &lcp->ltime);
		(void) drv_getparm(TIME, (unsigned long *) &lcp->ttime);
		lcp->mid = LOG_MID;
		lcp->sid = (struct log *)q->q_ptr - log_log;
		if (lcp->flags & SL_TRACE) {
			(void) log_internal(bp, log_trcseq, LOGTRC);
		}
		if (lcp->flags & SL_ERROR) {
			(void) log_internal(bp, log_errseq, LOGERR);
		}
		if (lcp->flags & SL_CONSOLE) {
			log_conseq++;
			if ((lcp->pri & LOG_FACMASK) == LOG_KERN)
				lcp->pri |= LOG_USER;
			(void) log_internal(bp, log_conseq, LOGCONS);
		}
		freemsg(bp);
		break;

	case M_DATA:
		if (lp != &log_log[CONSWMIN]) {
			bp->b_datap->db_type = M_ERROR;
			if (bp->b_cont) {
				freemsg(bp->b_cont);
				bp->b_cont = NULL;
			}
			bp->b_rptr = bp->b_datap->db_base;
			bp->b_wptr = bp->b_rptr + sizeof (char);
			*bp->b_rptr = EIO;
			qreply(q, bp);
			break;
		}

		/*
		 * allocate message block for proto
		 */
		if (!(cbp = allocb(sizeof (struct log_ctl), BPRI_HI))) {
			freemsg(bp);
			break;
		}
		cbp->b_datap->db_type = M_PROTO;
		cbp->b_cont = bp;
		cbp->b_wptr += sizeof (struct log_ctl);
		lcp = (struct log_ctl *)cbp->b_rptr;
		lcp->mid = LOG_MID;
		lcp->sid = CONSWMIN;
		(void) drv_getparm(LBOLT, (unsigned long *) &lcp->ltime);
		(void) drv_getparm(TIME, (unsigned long *) &lcp->ttime);
		lcp->level = 0;
		lcp->flags = SL_CONSOLE;
		lcp->pri = LOG_USER|LOG_INFO;
		log_conseq++;
		(void) log_internal(cbp, log_conseq, LOGCONS);
		freemsg(cbp);
		break;

	default:
		freemsg(bp);
		break;
	}
	return (0);
}

/*
 * Send a log message up a given log stream.
 */
static int
logrsrv(
	queue_t *q
)
{
	mblk_t *mp;

	while (mp = getq(q)) {
		if (!canput(q->q_next)) {
			(void) putbq(q, mp);
			break;
		}
		putnext(q, mp);
	}
	return (0);
}
