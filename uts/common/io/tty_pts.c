/*
 * PTY - Stream "pseudo-tty" device.
 * This is the "slave" side.
 */

#ident	"@(#)tty_pts.c	1.14	94/06/06 SMI"	/* SunOS-4.1 2.54	*/

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/filio.h>
#include <sys/ioccom.h>
#include <sys/termios.h>
#include <sys/termio.h>
#include <sys/ttold.h>
#include <sys/stropts.h>
#include <sys/stream.h>
#include <sys/tty.h>
#include <sys/user.h>
#include <sys/conf.h>
#include <sys/file.h>
#include <sys/vnode.h>	/* 1/0 on the vomit meter */
#include <sys/proc.h>
#include <sys/uio.h>
#include <sys/errno.h>
#include <sys/strsubr.h>
#include <sys/poll.h>
#include <sys/sysmacros.h>
#include <sys/debug.h>
#include <sys/procset.h>
#include <sys/cred.h>
#include <sys/ptyvar.h>
#include <sys/suntty.h>
#include <sys/stat.h>

#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>

extern	int npty;	/* number of pseudo-ttys configured in */
extern struct pty *pty_softc;

extern struct pollhead	ptcph;	/* poll head for ptcpoll() use */

#define	IFLAGS	(CS7|CREAD|PARENB)


/*
 * Most of these should be "void", but the people who defined the "streams"
 * data structure for S5 didn't understand data types.
 */

/*
 * Slave side.  This is a streams device.
 */
/* static */ int ptslopen(/*queue_t *q, int dev, int flag, int sflag*/);
/* static */ int	ptslclose(/*queue_t *q*/);
/* static */int	ptslwput(/*queue_t *q, mblk_t *mp*/);
/* static */int	ptslrserv(/*queue_t *q*/);

static struct module_info ptslm_info = {
	0,
	"ptys",
	0,
	INFPSZ,
	2048,
	128
};

static struct qinit ptslrinit = {
	putq,
	ptslrserv,
	ptslopen,
	ptslclose,
	NULL,
	&ptslm_info,
	NULL
};

static struct qinit ptslwinit = {
	ptslwput,
	NULL,
	NULL,
	NULL,
	NULL,
	&ptslm_info,
	NULL
};

static char *ptslmodlist[] = {
	"ldterm",
	"ttcompat",
	NULL
};

struct	streamtab ptysinfo = {
	&ptslrinit,
	&ptslwinit,
	NULL,
	NULL
};

/* static  */void	ptslreioctl(/*long unit*/);
/* static  */void	ptslioctl(/*struct pty *pty, queue_t *q, mblk_t *bp*/);
/* static  */void	pt_sendstop(/*struct pty *pty*/);
/* static  */void	ptcpollwakeup(/*struct pty *pty, int flag*/);


static int ptsl_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg,
		void **result);
static int ptsl_identify(dev_info_t *devi);
static int ptsl_attach(dev_info_t *devi, ddi_attach_cmd_t cmd);
static dev_info_t *ptsl_dip;	/* for dev-to-dip conversions */

#define	PTSL_CONF_FLAG	D_NEW
	DDI_DEFINE_STREAM_OPS(ptsl_ops, ptsl_identify, nulldev,	\
			ptsl_attach, nodev, nodev,		\
			ptsl_info, PTSL_CONF_FLAG, &ptysinfo);

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/modctl.h>


extern nodev(), nulldev();
extern dseekneg_flag;
extern struct mod_ops mod_driverops;
extern struct dev_ops ptsl_ops;

char _depends_on[] = "drv/ptc";

/*
 * Module linkage information for the kernel.
 */

static struct modldrv modldrv = {
	&mod_driverops, /* Type of module.  This one is a pseudo driver */
	"tty pseudo driver slave 'ptsl'",
	&ptsl_ops,	/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1,
	(void *)&modldrv,
	NULL
};

_init()
{
	return (mod_install(&modlinkage));
}


static int
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
ptsl_identify(dev_info_t *devi)
{
	if (strcmp(ddi_get_name(devi), "ptsl") == 0)
		return (DDI_IDENTIFIED);
	else
		return (DDI_NOT_IDENTIFIED);
}

static char	*tty_banks = PTY_BANKS;
static char	*tty_digits = PTY_DIGITS;

/* ARGSUSED */
static int
ptsl_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	char	name[8];
	int	tty_num;
	char	*tty_digit = tty_digits;
	char	*tty_bank = tty_banks;

	for (tty_num = 0; tty_num < npty; tty_num++) {
		sprintf(name, "tty%c%c", *tty_bank, *tty_digit);
		if (ddi_create_minor_node(devi, name, S_IFCHR,
			tty_num, NULL, NULL) == DDI_FAILURE) {
			ddi_remove_minor_node(devi, NULL);
			return (-1);
		}
		if (*(++tty_digit) == '\0') {
			tty_digit = tty_digits;
			if (*(++tty_bank) == '\0')
				break;
		}
	}
	ptsl_dip = devi;
	return (DDI_SUCCESS);
}

/* ARGSUSED */
static int
ptsl_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg,
	void **result)
{
	register dev_t dev = (dev_t) arg;
	register int error;

	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		if (ptsl_dip == NULL) {
			error = DDI_FAILURE;
		} else {
			*result = (void *) ptsl_dip;
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
 * Open the slave side of a pty.
 */
/*ARGSUSED*/
/* static */ int
ptslopen(q, devp, flag, sflag, cred)
	register queue_t *q;
	dev_t *devp;
	int   flag;
	int   sflag;
	cred_t *cred;
{
	register int unit;
	register dev_t dev = *devp;
	register struct pty *pty;
	register struct vnode *vp, *cvp;
	extern struct vnode *specfind(), *common_specvp();

	unit = getminor(dev);
	if (unit >= npty)
		return (ENXIO);

	pty = &pty_softc[unit];

	/*
	 * Block waiting for controller to open, unless this is a no-delay
	 * open.
	 */
again:
	if (pty->pt_ttycommon.t_writeq == NULL) {
		pty->pt_ttycommon.t_iflag = 0;
		pty->pt_ttycommon.t_cflag = (B38400 << IBSHIFT)|B38400|IFLAGS;
		pty->pt_ttycommon.t_iocpending = NULL;
		pty->pt_wbufcid = 0;
		pty->pt_ttycommon.t_size.ws_row = 0;
		pty->pt_ttycommon.t_size.ws_col = 0;
		pty->pt_ttycommon.t_size.ws_xpixel = 0;
		pty->pt_ttycommon.t_size.ws_ypixel = 0;
	} else if (pty->pt_ttycommon.t_flags & TS_XCLUDE &&
	    CRED()->cr_uid != 0) {
		return (EBUSY);
	}
	if (!(flag & (FNONBLOCK|FNDELAY)) &&
	    !(pty->pt_ttycommon.t_cflag & CLOCAL)) {
		if (!(pty->pt_flags & PF_CARR_ON)) {
			pty->pt_flags |= PF_WOPEN;
			if (sleep((caddr_t)&pty->pt_flags, STIPRI|PCATCH)) {
				pty->pt_flags &= ~PF_WOPEN;
				return (EINTR);
			}
			goto again;
		}
	}

	/*
	 * XXX Find the stream that we are at the end of.
	 * This is a gross breach of all principles of good,
	 * modular code.  However, the same can be said about
	 * the pseudo-tty features that require us to obtain
	 * this information, and unfortunately we're stuck with them.
	 */

	mutex_exit(&unsafe_driver);
	vp = specfind(dev, (vtype_t)VCHR);
	mutex_enter(&unsafe_driver);
	if (vp != NULL) {
		cvp = common_specvp(vp);
		pty->pt_stream = cvp->v_stream;
		VN_RELE(vp);	/* specfind() VN_HELD it */
	} else
		pty->pt_stream = NULL;

	pty->pt_sdev = dev;
	pty->pt_ttycommon.t_readq = q;
	pty->pt_ttycommon.t_writeq = WR(q);
	q->q_ptr = WR(q)->q_ptr = (caddr_t)pty;
	pty->pt_flags &= ~(PF_WOPEN|PF_SLAVEGONE);

	return (0);
}

/* static */int
ptslclose(q, flag, cred)
	register queue_t *q;
	int	flag;
	cred_t	*cred;
{
	register struct pty *pty;

#ifdef lint
	flag = flag;
	cred = cred;
#endif

	if ((pty = (struct pty *)q->q_ptr) == NULL)
		return (0);		/* already been closed once */

	ttycommon_close(&pty->pt_ttycommon);

	/*
	 * Cancel outstanding "bufcall" request.
	 */
	if (pty->pt_wbufcid) {
		unbufcall(pty->pt_wbufcid);
		pty->pt_wbufcid = 0;
	}

	/*
	 * Clear out all the slave-side state.
	 */
	pty->pt_flags &= ~(PF_WOPEN|PF_STOPPED|PF_NOSTOP);
	if (pty->pt_flags & PF_CARR_ON) {
		pty->pt_flags |= PF_SLAVEGONE;	/* let the controller know */
		ptcpollwakeup(pty, 0);	/* wake up readers/selectors */
		ptcpollwakeup(pty, FWRITE);	/* wake up writers/selectors */
	}
	pty->pt_stream = NULL;
	pty->pt_sdev = 0;
	pty->pt_ttycommon.t_readq = NULL;
	pty->pt_ttycommon.t_writeq = NULL;
	wakeup((caddr_t)&pty->pt_flags);
	q->q_ptr = WR(q)->q_ptr = NULL;
	return (0);
}

/*
 * Put procedure for write queue.
 * Respond to M_STOP, M_START, M_IOCTL, and M_FLUSH messages here;
 * queue up M_DATA messages for processing by the controller "read"
 * routine; discard everything else.
 */
/* static */ int
ptslwput(q, mp)
	register queue_t *q;
	register mblk_t *mp;
{
	register struct pty *pty;
	register mblk_t *bp;

	pty = (struct pty *)q->q_ptr;

	switch (mp->b_datap->db_type) {

	case M_STOP:
		if (!(pty->pt_flags & PF_STOPPED)) {
			pty->pt_flags |= PF_STOPPED;
			pty->pt_send |= TIOCPKT_STOP;
			ptcpollwakeup(pty, 0);
		}
		freemsg(mp);
		break;

	case M_START:
		if (pty->pt_flags & PF_STOPPED) {
			pty->pt_flags &= ~PF_STOPPED;
			pty->pt_send = TIOCPKT_START;
			ptcpollwakeup(pty, 0);
		}
		ptcpollwakeup(pty, FREAD);	/* permit controller to read */
		freemsg(mp);
		break;

	case M_IOCTL:
		ptslioctl(pty, q, mp);
		break;

	case M_FLUSH:
		if (*mp->b_rptr & FLUSHW) {
			/*
			 * Set the "flush write" flag, so that we
			 * notify the controller if they're in packet
			 * or user control mode.
			 */
			if (!(pty->pt_send & TIOCPKT_FLUSHWRITE)) {
				pty->pt_send |= TIOCPKT_FLUSHWRITE;
				ptcpollwakeup(pty, 0);
			}
			/*
			 * Flush our write queue.
			 */
			flushq(q, FLUSHDATA);	/* XXX doesn't flush M_DELAY */
			*mp->b_rptr &= ~FLUSHW;	/* it has been flushed */
		}
		if (*mp->b_rptr & FLUSHR) {
			/*
			 * Set the "flush read" flag, so that we
			 * notify the controller if they're in packet
			 * mode.
			 */
			if (!(pty->pt_send & TIOCPKT_FLUSHREAD)) {
				pty->pt_send |= TIOCPKT_FLUSHREAD;
				ptcpollwakeup(pty, 0);
			}
			flushq(RD(q), FLUSHDATA);
			qreply(q, mp);	/* give the read queues a crack at it */
		} else
			freemsg(mp);
		break;

	case M_DATA:
		/*
		 * Throw away any leading zero-length blocks, and queue it up
		 * for the controller to read.
		 */
		if (pty->pt_flags & PF_CARR_ON) {
			bp = mp;
			while ((bp->b_wptr - bp->b_rptr) == 0) {
				mp = bp->b_cont;
				freeb(bp);
				if (mp == NULL)
					return;	/* damp squib of a message */
			}
			(void) putq(q, mp);
			ptcpollwakeup(pty, FREAD);	/* soup's on! */
		} else
			freemsg(mp);	/* nobody listening */
		break;

	case M_CTL:
		switch (*mp->b_rptr) {

		case MC_CANONQUERY:
			/*
			 * We're being asked whether we do canonicalization
			 * or not.  Send a reply back up indicating whether
			 * we do or not.
			 */
			(void) putctl1(RD(q)->q_next, M_CTL,
			    (pty->pt_flags & PF_REMOTE) ?
				MC_NOCANON : MC_DOCANON);
			break;
		}
		freemsg(mp);
		break;

	default:
		/*
		 * "No, I don't want a subscription to Chain Store Age,
		 * thank you anyway."
		 */
		freemsg(mp);
		break;
	}
}

/*
 * Retry an "ioctl", now that "bufcall" claims we may be able to allocate
 * the buffer we need.
 */
/* static */void
ptslreioctl(unit)
	long unit;
{
	register struct pty *pty = &pty_softc[unit];
	queue_t *q;
	register mblk_t *mp;

	/*
	 * The bufcall is no longer pending.
	 */
	pty->pt_wbufcid = 0;
	if ((q = pty->pt_ttycommon.t_writeq) == NULL)
		return;
	if ((mp = pty->pt_ttycommon.t_iocpending) != NULL) {
		/* It's not pending any more. */
		pty->pt_ttycommon.t_iocpending = NULL;
		ptslioctl(pty, q, mp);
	}
}

/*
 * Process an "ioctl" message sent down to us.
 */
/* static */ void
ptslioctl(pty, q, mp)
	register struct pty *pty;
	queue_t *q;
	register mblk_t *mp;
{
	register struct iocblk *iocp;
	register int cmd;
	register unsigned datasize;
	int error = 0;

	iocp = (struct iocblk *)mp->b_rptr;
	cmd = iocp->ioc_cmd;

	switch (cmd) {

	case TIOCSTI: {
		/*
		 * The permission checking has already been done at the stream
		 * head, since it has to be done in the context of the process
		 * doing the call.
		 */
		register mblk_t *bp;

		/*
		 * Simulate typing of a character at the terminal.
		 */
		if ((bp = allocb(1, BPRI_MED)) != NULL) {
			*bp->b_wptr++ = *mp->b_cont->b_rptr;
			if (!(pty->pt_flags & PF_REMOTE)) {
				if (!canput(
					pty->pt_ttycommon.t_readq->q_next)) {
					ttycommon_qfull(&pty->pt_ttycommon, q);
					freemsg(bp);
				} else
					putnext(pty->pt_ttycommon.t_readq, bp);
			} else {
				if (pty->pt_flags & PF_UCNTL) {
					/*
					 * XXX - flow control; don't overflow
					 * this "queue".
					 */
					if (pty->pt_stuffqfirst != NULL) {
						pty->pt_stuffqlast->b_next = bp;
						bp->b_prev = pty->pt_stuffqlast;
					} else {
						pty->pt_stuffqfirst = bp;
						bp->b_prev = NULL;
					}
					bp->b_next = NULL;
					pty->pt_stuffqlast = bp;
					pty->pt_stuffqlen++;
					ptcpollwakeup(pty, 0);
				}
			}
		}

		/*
		 * Turn the ioctl message into an ioctl ACK message.
		 */
		iocp->ioc_count = 0;	/* no data returned */
		mp->b_datap->db_type = M_IOCACK;
		goto out;
	}

	case TIOCSSIZE: {
		register tty_common_t *tc = &pty->pt_ttycommon;

		/*
		 * Set the window size, but don't send a SIGWINCH.
		 */
		tc->t_size.ws_row =
		    ((struct ttysize *)mp->b_cont->b_rptr)->ts_lines;
		tc->t_size.ws_col =
		    ((struct ttysize *)mp->b_cont->b_rptr)->ts_cols;
		tc->t_size.ws_xpixel = 0;
		tc->t_size.ws_ypixel = 0;

		/*
		 * Send an ACK back.
		 */
		iocp->ioc_count = 0;	/* no data returned */
		mp->b_datap->db_type = M_IOCACK;
		goto out;
	}

	case TIOCGSIZE: {
		register tty_common_t *tc = &pty->pt_ttycommon;
		register mblk_t *datap;
		struct ttysize *tp;

		if ((datap = allocb((int)sizeof (struct ttysize),
		    BPRI_HI)) == NULL) {
			if (pty->pt_wbufcid)
				unbufcall(pty->pt_wbufcid);
			pty->pt_wbufcid =
				bufcall(sizeof (struct ttysize), BPRI_HI,
				ptslreioctl, (long)(pty - pty_softc));
			return;
		}
		/*
		 * Return the current size.
		 */
		tp = (struct ttysize *)datap->b_wptr;
		tp->ts_lines = (int) tc->t_size.ws_row;
		tp->ts_cols = (int) tc->t_size.ws_col;
		datap->b_wptr +=
			(sizeof (struct ttysize))/(sizeof *datap->b_wptr);
		iocp->ioc_count = sizeof (struct ttysize);

		if (mp->b_cont != NULL)
			freemsg(mp->b_cont);
		mp->b_cont = datap;
		mp->b_datap->db_type = M_IOCACK;
		goto out;
	}

	/*
	 * If they were just trying to drain output, that's OK.
	 * If they are actually trying to send a break it's an error.
	 */
	case TCSBRK:
		if (*(int *)mp->b_cont->b_rptr != 0) {
			/*
			 * Turn the ioctl message into an ioctl ACK message.
			 */
			iocp->ioc_count = 0;	/* no data returned */
			mp->b_datap->db_type = M_IOCACK;
		} else {
			error = ENOTTY;
		}
		goto out;
	}

	/*
	 * The only way in which "ttycommon_ioctl" can fail is if the "ioctl"
	 * requires a response containing data to be returned to the user,
	 * and no mblk could be allocated for the data.
	 * No such "ioctl" alters our state.  Thus, we always go ahead and
	 * do any state-changes the "ioctl" calls for.  If we couldn't allocate
	 * the data, "ttycommon_ioctl" has stashed the "ioctl" away safely, so
	 * we just call "bufcall" to request that we be called back when we
	 * stand a better chance of allocating the data.
	 */
	if ((datasize =
	    ttycommon_ioctl(&pty->pt_ttycommon, q, mp, &error)) != 0) {
		if (pty->pt_wbufcid)
			unbufcall(pty->pt_wbufcid);
		pty->pt_wbufcid = bufcall(datasize, BPRI_HI, ptslreioctl,
		    (long)(pty - pty_softc));
		return;
	}

	if (error == 0) {
		/*
		 * "ttycommon_ioctl" did most of the work; we just use the
		 * data it set up.
		 */
		switch (cmd) {

		case TCSETSF:
		case TCSETAF:
			/*
			 * Set the "flush read" flag, so that we
			 * notify the controller if they're in packet
			 * mode.
			 */
			if (!(pty->pt_send & TIOCPKT_FLUSHREAD)) {
				pty->pt_send |= TIOCPKT_FLUSHREAD;
				ptcpollwakeup(pty, 0);
			}

		case TCSETSW:
		case TCSETAW:
			cmd = TIOCSETP;	/* map backwards to old codes */
			pt_sendstop(pty);
			break;

		case TCSETS:
		case TCSETA:
			cmd = TIOCSETN;	/* map backwards to old codes */
			pt_sendstop(pty);
			break;
		}
	}

	if (pty->pt_flags & PF_43UCNTL) {
		if (error < 0) {
			if ((cmd & ~0xff) == _IO('u', 0)) {
				if (cmd & 0xff) {
					pty->pt_ucntl = (u_char)cmd & 0xff;
					ptcpollwakeup(pty, FREAD);
				}
				error = 0; /* XXX */
				goto out;
			}
			error = ENOTTY;
		}
	} else {
		if ((pty->pt_flags & PF_UCNTL) &&
		    (cmd & (IOC_INOUT | 0xff00)) == (IOC_IN|('t'<<8)) &&
		    (cmd & 0xff)) {
			pty->pt_ucntl = (u_char)cmd & 0xff;
			ptcpollwakeup(pty, FREAD);
			goto out;
		}
		if (error < 0)
			error = ENOTTY;
	}

out:
	if (error != 0) {
		((struct iocblk *)mp->b_rptr)->ioc_error = error;
		mp->b_datap->db_type = M_IOCNAK;
	}
	qreply(q, mp);
}

/*
 * Service routine for read queue.
 * Just wakes the controller side up so it can write some more data
 * to that queue.
 */
/* static */int
ptslrserv(q)
	queue_t *q;
{

	ptcpollwakeup((struct pty *)q->q_ptr, FWRITE);
	return (0);
}

/* static */void
pt_sendstop(pty)
	register struct pty *pty;
{
	int stop;

	if ((pty->pt_ttycommon.t_cflag&CBAUD) == 0) {
		if (pty->pt_flags & PF_CARR_ON) {
			/*
			 * Let the controller know, then wake up
			 * readers/selectors and writers/selectors.
			 */
			pty->pt_flags |= PF_SLAVEGONE;
			ptcpollwakeup(pty, 0);
			ptcpollwakeup(pty, FWRITE);
		}
	}

	stop = (pty->pt_ttycommon.t_iflag & IXON) &&
	    pty->pt_ttycommon.t_stopc == CTRL('s') &&
	    pty->pt_ttycommon.t_startc == CTRL('q');

	if (pty->pt_flags & PF_NOSTOP) {
		if (stop) {
			pty->pt_send &= ~TIOCPKT_NOSTOP;
			pty->pt_send |= TIOCPKT_DOSTOP;
			pty->pt_flags &= ~PF_NOSTOP;
			ptcpollwakeup(pty, 0);
		}
	} else {
		if (!stop) {
			pty->pt_send &= ~TIOCPKT_DOSTOP;
			pty->pt_send |= TIOCPKT_NOSTOP;
			pty->pt_flags |= PF_NOSTOP;
			ptcpollwakeup(pty, 0);
		}
	}
}

/*
 * Wake up controller side.  "flag" is 0 if a special packet or
 * user control mode message has been queued up (this data is readable,
 * so we also treat it as a regular data event; should we send SIGIO,
 * though?), FREAD if regular data has been queued up, or FWRITE if
 * the slave's read queue has drained sufficiently to allow writing.
 */
/* static */void
ptcpollwakeup(pty, flag)
	register struct pty *pty;
	int flag;
{
	if (flag == 0) {
		/*
		 * "Exceptional condition" occurred.  This means that
		 * a "read" is now possible, so do a "read" wakeup.
		 */
		flag = FREAD;
		pollwakeup(&ptcph, POLLIN | POLLRDBAND);
		if (pty->pt_flags & PF_ASYNC)
			gsignal(pty->pt_pgrp, SIGURG);
	}
	if (flag & FREAD) {
		pollwakeup(&ptcph, POLLIN | POLLRDNORM);
		wakeup((caddr_t)&pty->pt_ttycommon.t_writeq);
		if (pty->pt_flags & PF_ASYNC)
			gsignal(pty->pt_pgrp, SIGIO);
	}
	if (flag & FWRITE) {
		pollwakeup(&ptcph, POLLOUT | POLLWRNORM);
		wakeup((caddr_t)&pty->pt_ttycommon.t_readq);
		if (pty->pt_flags & PF_ASYNC)
			gsignal(pty->pt_pgrp, SIGIO);
	}
}
