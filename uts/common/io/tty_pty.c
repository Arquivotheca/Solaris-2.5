/*
 * PTY - Stream "pseudo-tty" device.  For each "controller" side
 * it connects to a "slave" side.
 */

#ident	"@(#)tty_pty.c	1.28	95/01/20 SMI"	/* SunOS-4.1 2.54	*/

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
extern char qrunflag;
extern struct pty *pty_softc;
extern struct pollhead	ptcph;	/* poll head for ptcpoll() use */

int ptcopen(), ptcclose(), ptcwrite(), ptcread(), ptcioctl();
int ptcpoll(dev_t, short, int, short *, struct pollhead **);

static int ptc_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg,
		void **result);
static int ptc_identify(dev_info_t *devi);
static int ptc_attach(dev_info_t *devi, ddi_attach_cmd_t cmd);
static dev_info_t *ptc_get_dev_info(dev_t dev, int otyp);
static dev_info_t *ptc_dip;	/* for dev-to-dip conversions */

struct cb_ops	ptc_cb_ops = {
	ptcopen,		/* open */
	ptcclose,		/* close */
	nodev,			/* strategy */
	nodev,			/* print */
	nodev,			/* dump */
	ptcread,		/* read */
	ptcwrite,		/* write */
	ptcioctl, 		/* ioctl */
	nodev,			/* devmap */
	nodev,			/* mmap */
	nodev,			/* segmap */
	ptcpoll,		/* poll */
	ddi_prop_op,		/* prop_op */
	0,			/* streamtab */
	D_NEW			/* Driver compatibility flag */
};

struct dev_ops	ptc_ops = {
	DEVO_REV,		/* devo_rev */
	0,			/* refcnt */
	ptc_info,		/* info */
	ptc_identify,		/* identify */
	nulldev,		/* probe */
	ptc_attach,		/* attach */
	nodev,			/* detach */
	nodev,			/* reset */
	&ptc_cb_ops,		/* driver operations */
	(struct bus_ops *)0	/* bus operations */
};

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/modctl.h>


extern nodev(), nulldev();
extern dseekneg_flag;
extern struct mod_ops mod_driverops;
extern struct dev_ops ptc_ops;

/*
 * Module linkage information for the kernel.
 */

static struct modldrv modldrv = {
	&mod_driverops, /* Type of module.  This one is a pseudo driver */
	"tty pseudo driver control 'ptc'",
	&ptc_ops,	/* driver ops */
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
ptc_identify(dev_info_t *devi)
{
	if (strcmp(ddi_get_name(devi), "ptc") == 0)
		return (DDI_IDENTIFIED);
	else
		return (DDI_NOT_IDENTIFIED);
}

static char	*pty_banks = PTY_BANKS;
static char	*pty_digits = PTY_DIGITS;

/* ARGSUSED */
static int
ptc_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	char	name[8];
	int	pty_num;
	char	*pty_digit = pty_digits;
	char	*pty_bank = pty_banks;

	for (pty_num = 0; pty_num < npty; pty_num++) {
		sprintf(name, "pty%c%c", *pty_bank, *pty_digit);
		if (ddi_create_minor_node(devi, name, S_IFCHR,
			pty_num, NULL, NULL) == DDI_FAILURE) {
			ddi_remove_minor_node(devi, NULL);
			return (-1);
		}
		if (*(++pty_digit) == '\0') {
			pty_digit = pty_digits;
			if (*(++pty_bank) == '\0')
				break;
		}
	}
	ptc_dip = devi;
	return (DDI_SUCCESS);
}

/* ARGSUSED */
static int
ptc_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result)
{
	register dev_t dev = (dev_t) arg;
	register int instance, error;
	register struct bw2_softc *softc;

	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		if (ptc_dip == NULL) {
			*result = (void *)NULL;
			error = DDI_FAILURE;
		} else {
			*result = (void *) ptc_dip;
			error = DDI_SUCCESS;
		}
		break;
	case DDI_INFO_DEVT2INSTANCE:
		if ((instance = getminor(dev)) >= npty) {
			*result = (void *)-1;
			error = DDI_FAILURE;
		} else {
			*result = (void *)instance;
			error = DDI_SUCCESS;
		}
		break;
	default:
		error = DDI_FAILURE;
	}
	return (error);
}

/*
 * Controller side.  This is not, alas, a streams device; there are too
 * many old features that we must support and that don't work well
 * with streams.
 */

/*ARGSUSED*/
int
ptcopen(devp, flag, otyp, cred)
	dev_t *devp;
	int flag;
	int otyp;
	struct cred *cred;
{
	register dev_t dev = *devp;
	register struct pty *pty;
	register queue_t *q;

	if (getminor(dev) >= npty) {
		return (ENXIO);
	}
	pty = &pty_softc[getminor(dev)];
	if (pty->pt_flags & PF_CARR_ON) {
		return (EIO);	/* controller is exclusive use */
				/* XXX - should be EBUSY! */
	}
	if (pty->pt_flags & PF_WOPEN) {
		wakeup((caddr_t)&pty->pt_flags);
	}
	if ((q = pty->pt_ttycommon.t_readq) != NULL &&
	    (q = q->q_next) != NULL) {
		/*
		 * Send an un-hangup to the slave, since "carrier" is
		 * coming back up.  Make sure we're doing canonicalization.
		 */
		(void) putctl(q, M_UNHANGUP);
		(void) putctl1(q, M_CTL, MC_DOCANON);
	}
	pty->pt_flags |= PF_CARR_ON;
	pty->pt_send = 0;
	pty->pt_ucntl = 0;

	return (0);
}

int
ptcclose(dev, flag, cred)
	dev_t dev;
	int   flag;
	struct cred *cred;
{
	register struct pty *pty;
	register mblk_t *bp;
	register queue_t *q;

#ifdef lint
	flag = flag;
	cred = cred;
#endif

	pty = &pty_softc[getminor(dev)];
	if ((q = pty->pt_ttycommon.t_readq) != NULL) {
		/*
		 * Send a hangup to the slave, since "carrier" is dropping.
		 */
		(void) putctl(q->q_next, M_HANGUP);
	}

	/*
	 * Clear out all the controller-side state.  This also
	 * clears PF_CARR_ON, which is correct because the
	 * "carrier" is dropping since the controller process
	 * is going away.
	 */
	pty->pt_flags &= (PF_WOPEN|PF_STOPPED|PF_NOSTOP);
	while ((bp = pty->pt_stuffqfirst) != NULL) {
		if ((pty->pt_stuffqfirst = bp->b_next) == NULL)
			pty->pt_stuffqlast = NULL;
		else
			pty->pt_stuffqfirst->b_prev = NULL;
		pty->pt_stuffqlen--;
		bp->b_next = bp->b_prev = NULL;
		freemsg(bp);
	}
	return (0);
}

int
ptcread(dev, uio, cred)
	dev_t dev;
	struct uio *uio;
	struct cred *cred;
{
	register struct pty *pty = &pty_softc[getminor(dev)];
	register mblk_t *bp, *nbp;
	register queue_t *q;
	register int error, cc;

#ifdef lint
	cred = cred;
#endif

	for (;;) {
		/*
		 * If there's a TIOCPKT packet waiting, pass it back.
		 */
		if (pty->pt_flags&(PF_PKT|PF_UCNTL) && pty->pt_send) {
			error = ureadc((int)pty->pt_send, uio);
			if (error)
				return (error);
			pty->pt_send = 0;
			return (0);
		}

		/*
		 * If there's a user-control packet waiting, pass the
		 * "ioctl" code back.
		 */
		if ((pty->pt_flags & (PF_UCNTL|PF_43UCNTL)) && pty->pt_ucntl) {
			error = ureadc((int)pty->pt_ucntl, uio);
			if (error)
				return (error);
			pty->pt_ucntl = 0;
			return (0);
		}

		/*
		 * If there's any data waiting, pass it back.
		 */
		if ((q = pty->pt_ttycommon.t_writeq) != NULL &&
		    q->q_first != NULL &&
		    !(pty->pt_flags & PF_STOPPED)) {
			if (pty->pt_flags & (PF_PKT|PF_UCNTL|PF_43UCNTL)) {
				/*
				 * We're about to begin a move in packet or
				 * user-control mode; precede the data with a
				 * data header.
				 */
				error = ureadc(TIOCPKT_DATA, uio);
				if (error != 0)
					return (error);
			}
			bp = getq(q);
			while (uio->uio_resid > 0) {
				while ((cc = bp->b_wptr - bp->b_rptr) == 0) {
					nbp = bp->b_cont;
					freeb(bp);
					if ((bp = nbp) == NULL) {
						if ((bp = getq(q)) == NULL)
							return (0);
					}
				}
				cc = MIN(cc, uio->uio_resid);
				error = uiomove((caddr_t)bp->b_rptr,
				    cc, UIO_READ, uio);
				if (error != 0) {
					freemsg(bp);
					return (error);
				}
				bp->b_rptr += cc;
			}
			/*
			 * Strip off zero-length blocks from the front of
			 * what we're putting back on the queue.
			 */
			while ((bp->b_wptr - bp->b_rptr) == 0) {
				nbp = bp->b_cont;
				freeb(bp);
				if ((bp = nbp) == NULL)
					return (0);	/* nothing left */
			}
			(void) putbq(q, bp);
			return (0);
		}

		/*
		 * If there's any TIOCSTI-stuffed characters, pass
		 * them back.  (They currently arrive after all output;
		 * is this correct?)
		 */
		if (pty->pt_flags&PF_UCNTL && pty->pt_stuffqfirst != NULL) {
			error = ureadc(TIOCSTI&0xff, uio);
			while (error == 0 &&
			    (bp = pty->pt_stuffqfirst) != NULL &&
			    uio->uio_resid > 0) {
				pty->pt_stuffqlen--;
				error = ureadc((int)*bp->b_rptr, uio);
				if ((pty->pt_stuffqfirst = bp->b_next) == NULL)
					pty->pt_stuffqlast = NULL;
				else
					pty->pt_stuffqfirst->b_prev = NULL;
				bp->b_next = bp->b_prev = NULL;
				freemsg(bp);
			}
			return (error);
		}

		/*
		 * There's no data available.
		 * We want to block until the slave is open, and there's
		 * something to read; but if we lost the slave or we're NBIO,
		 * then return the appropriate error instead.  POSIX-style
		 * non-block has top billing and gives -1 with errno = EAGAIN,
		 * BSD-style comes next and gives -1 with errno = EWOULDBLOCK,
		 * SVID-style comes last and gives 0.
		 */
		if (pty->pt_flags & PF_SLAVEGONE)
			return (EIO);
		if (uio->uio_fmode & FNONBLOCK)
			return (EAGAIN);
		if (pty->pt_flags & PF_NBIO)
			return (EWOULDBLOCK);
		if (uio->uio_fmode & FNDELAY)
			return (0);
		(void) sleep((caddr_t)&pty->pt_ttycommon.t_writeq, STIPRI);
	}
}

int
ptcwrite(dev, uio, cred)
	dev_t dev;
	register struct uio *uio;
	struct cred *cred;
{
	register struct pty *pty = &pty_softc[getminor(dev)];
	register struct vnode *vp;
	register queue_t *q;
	register int written;
	mblk_t *mp;
	int fmode = 0;
	int error = 0;


#ifdef lint
	cred = cred;
#endif


	while ((q = pty->pt_ttycommon.t_readq) == NULL) {
		/*
		 * Wait for slave to open.
		 */
		if (pty->pt_flags & PF_SLAVEGONE)
			return (EIO);
		if (uio->uio_fmode & FNONBLOCK)
			return (EAGAIN);
		if (pty->pt_flags & PF_NBIO)
			return (EWOULDBLOCK);
		if (uio->uio_fmode & FNDELAY)
			return (0);
		(void) sleep((caddr_t)&pty->pt_ttycommon.t_writeq, STOPRI);
	}

	/*
	 * If in remote mode, even zero-length writes generate messages.
	 */
	written = 0;
	if ((pty->pt_flags & PF_REMOTE) || uio->uio_resid > 0) {
		do {
			while (!canput(q->q_next)) {
				/*
				 * Wait for slave's read queue to unclog.
				 */
				if (pty->pt_flags & PF_SLAVEGONE)
					return (EIO);
				if (uio->uio_fmode & FNONBLOCK) {
					if (!written)
						return (EAGAIN);
					return (0);
				}
				if (pty->pt_flags & PF_NBIO) {
					if (!written)
						return (EWOULDBLOCK);
					return (0);
				}
				if (uio->uio_fmode & FNDELAY)
					return (0);
				(void) sleep((caddr_t)
				    &pty->pt_ttycommon.t_readq, STOPRI);
			}

			if ((pty->pt_flags & PF_NBIO) &&
					uio->uio_fmode != FNONBLOCK) {
				fmode = uio->uio_fmode;
				uio->uio_fmode |= FNONBLOCK;
			}

			error = strmakemsg((struct strbuf *)NULL,
			    uio->uio_resid, uio, pty->pt_stream,
			    (long)0, &mp);
			if (fmode)
				uio->uio_fmode = fmode;
			if (error != NULL) {
				if (error != EAGAIN && error != EWOULDBLOCK)
					return (error);
				if (uio->uio_fmode & FNONBLOCK) {
					if (!written)
						return (EAGAIN);
					return (0);
				}
				if (pty->pt_flags & PF_NBIO) {
					if (!written)
						return (EWOULDBLOCK);
					return (0);
				}
				if (uio->uio_fmode & FNDELAY) {
					return (0);
				}
				panic("ptcwrite: non null return from"
						" strmakemsg");
			}

			/*
			 * Check again for safety; since "uiomove" can take a
			 * page fault, there's no guarantee that "pt_flags"
			 * didn't change while it was happening.
			 */
			if ((q = pty->pt_ttycommon.t_readq) == NULL) {
				freemsg(mp);
				return (EIO);
			}
			putnext(q, mp);
			written = 1;
			if (qready()) {
				mutex_exit(&unsafe_driver);
				runqueues();
				mutex_enter(&unsafe_driver);
			}
		} while (uio->uio_resid > 0);
	}
	return (0);
}

#define	copy_in(data, d_arg) \
	if (copyin((caddr_t)data, (caddr_t)&d_arg, sizeof (int)) != 0) \
		return (EFAULT)

#define	copy_out(d_arg, data) \
	if (copyout((caddr_t)&d_arg, (caddr_t)data, sizeof (int)) != 0) \
		return (EFAULT)

/*ARGSUSED*/
int
ptcioctl(dev, cmd, data, flag, cred, rvalp)
	dev_t dev;
	u_int	cmd;
	caddr_t data;
	int flag;
	struct cred *cred;
	int *rvalp;
{
	register struct pty *pty = &pty_softc[getminor(dev)];
	register queue_t *q;
	register struct vnode *vp;
	struct ttysize tty_arg;
	struct winsize win_arg;
	int	 d_arg, err;

	switch (cmd) {

	case ((u_int)TIOCPKT):
		copy_in(data, d_arg);
		if (d_arg) {
			if (pty->pt_flags & (PF_UCNTL|PF_43UCNTL))
				return (EINVAL);
			pty->pt_flags |= PF_PKT;
		} else
			pty->pt_flags &= ~PF_PKT;
		break;

	case ((u_int)TIOCUCNTL):
		copy_in(data, d_arg);
		if (d_arg) {
			if (pty->pt_flags & (PF_PKT|PF_UCNTL))
				return (EINVAL);
			pty->pt_flags |= PF_43UCNTL;
		} else
			pty->pt_flags &= ~PF_43UCNTL;
		break;

	case ((u_int)TIOCTCNTL):
		copy_in(data, d_arg);
		if (d_arg) {
			if (pty->pt_flags & PF_PKT)
				return (EINVAL);
			pty->pt_flags |= PF_UCNTL;
		} else
			pty->pt_flags &= ~PF_UCNTL;
		break;

	case ((u_int)TIOCREMOTE):
		copy_in(data, d_arg);
		if (d_arg) {
			if ((q = pty->pt_ttycommon.t_readq) != NULL &&
			    (q = q->q_next) != NULL)
				(void) putctl1(q, M_CTL, MC_NOCANON);
			pty->pt_flags |= PF_REMOTE;
		} else {
			if ((q = pty->pt_ttycommon.t_readq) != NULL &&
			    (q = q->q_next) != NULL)
				(void) putctl1(q, M_CTL, MC_DOCANON);
			pty->pt_flags &= ~PF_REMOTE;
		}
		break;

	case ((u_int)TIOCSIGNAL):
		/*
		 * Blast a M_PCSIG message up the slave stream; the
		 * signal number is the argument to the "ioctl".
		 */
		copy_in(data, d_arg);
		if ((q = pty->pt_ttycommon.t_readq) != NULL &&
		    (q = q->q_next) != NULL)
			(void) putctl1(q, M_PCSIG, d_arg);
		break;

	/*
	 * XXX These should not be here.  The only reason why an
	 * "ioctl" on the controller side should get the
	 * slave side's process group is so that the process on
	 * the controller side can send a signal to the slave
	 * side's process group; however, this is better done
	 * with TIOCSIGNAL, both because it doesn't require us
	 * to know about the slave side's process group and because
	 * the controller side process may not have permission to
	 * send that signal to the entire process group.
	 *
	 * However, since vanilla 4BSD doesn't provide TIOCSIGNAL,
	 * we can't just get rid of them.
	 */
	case ((u_int)TIOCGPGRP):
		if (pty->pt_stream == NULL || pty->pt_stream->sd_pgidp == NULL)
			return (EIO);
		d_arg = (pid_t) pty->pt_stream->sd_pgidp->pid_id;
		copy_out(d_arg, data);
		break;

	case ((u_int)TIOCSPGRP):
		copy_in(data, d_arg);
		if (pty->pt_stream == NULL || pty->pt_stream->sd_pgidp == NULL)
			return (EIO);
		pty->pt_stream->sd_pgidp->pid_id = d_arg;
		break;

	case ((u_int)FIONBIO):
		copy_in(data, d_arg);
		if (d_arg)
			pty->pt_flags |= PF_NBIO;
		else
			pty->pt_flags &= ~PF_NBIO;
		break;

	case ((u_int)FIOASYNC):
		copy_in(data, d_arg);
		if (d_arg)
			pty->pt_flags |= PF_ASYNC;
		else
			pty->pt_flags &= ~PF_ASYNC;
		break;

	/*
	 * These, at least, can work on the controller-side process
	 * group.
	 */
	case FIOGETOWN:
		d_arg = -pty->pt_pgrp;
		copy_out(d_arg, data);
		break;

	case ((u_int)FIOSETOWN):
		copy_in(data, d_arg);
		pty->pt_pgrp = -d_arg;
		break;

	case FIONREAD: {
		/*
		 * Return the total number of bytes of data in all messages
		 * in slave write queue, which is master read queue, unless a
		 * special message would be read.
		 */
		register mblk_t *mp;
		int count = 0;

		if (pty->pt_flags&(PF_PKT|PF_UCNTL) && pty->pt_send)
			count = 1;	/* will return 1 byte */
		else if ((pty->pt_flags & (PF_UCNTL|PF_43UCNTL)) &&
		    pty->pt_ucntl)
			count = 1;	/* will return 1 byte */
		else if ((q = pty->pt_ttycommon.t_writeq) != NULL &&
		    q->q_first != NULL && !(pty->pt_flags & PF_STOPPED)) {
			/*
			 * Will return whatever data is queued up.
			 */
			for (mp = q->q_first; mp != NULL; mp = mp->b_next)
				count += msgdsize(mp);
		} else if ((pty->pt_flags & PF_UCNTL) &&
		    pty->pt_stuffqfirst != NULL) {
			/*
			 * Will return STI'ed data.
			 */
			count = pty->pt_stuffqlen + 1;
		}
		d_arg = count;
		copy_out(d_arg, data);
		break;
	}

	case TIOCSWINSZ:
		/*
		 * Unfortunately, TIOCSWINSZ and the old TIOCSSIZE "ioctl"s
		 * share the same code.  If the upper 16 bits of the number
		 * of lines is non-zero, it was probably a TIOCSWINSZ,
		 * with both "ws_row" and "ws_col" non-zero.
		 */
		if (copyin(data,
		    (caddr_t)&tty_arg, sizeof (struct ttysize)) != 0)
			return (EFAULT);

		if ((tty_arg.ts_lines&0xffff0000) != 0) {
			/*
			 * It's a TIOCSWINSZ.
			 */
			win_arg = *(struct winsize *)&tty_arg;

			/*
			 * If the window size changed, send a SIGWINCH.
			 */
			if (bcmp((caddr_t)&pty->pt_ttycommon.t_size,
			    (caddr_t)&win_arg, sizeof (struct winsize))) {
				pty->pt_ttycommon.t_size = win_arg;
				if ((q = pty->pt_ttycommon.t_readq) != NULL &&
				    (q = q->q_next) != NULL)
					(void) putctl1(q, M_PCSIG, SIGWINCH);
			}
			break;
		}
		/* FALL THROUGH */

	case ((u_int)TIOCSSIZE):
		if (copyin(data,
		    (caddr_t)&tty_arg, sizeof (struct ttysize)) != 0)
			return (EFAULT);
		pty->pt_ttycommon.t_size.ws_row = tty_arg.ts_lines;
		pty->pt_ttycommon.t_size.ws_col = tty_arg.ts_cols;
		pty->pt_ttycommon.t_size.ws_xpixel = 0;
		pty->pt_ttycommon.t_size.ws_ypixel = 0;
		break;

	case TIOCGWINSZ:
		win_arg = pty->pt_ttycommon.t_size;
		if (copyout(
		    (caddr_t)&win_arg, data, sizeof (struct winsize)) != 0)
			return (EFAULT);
		break;

	case TIOCGSIZE:
		tty_arg.ts_lines = pty->pt_ttycommon.t_size.ws_row;
		tty_arg.ts_cols = pty->pt_ttycommon.t_size.ws_col;
		if (copyout(
		    (caddr_t)&tty_arg, data, sizeof (struct ttysize)) != 0)
			return (EFAULT);
		break;

	/*
	 * This is amazingly disgusting, but the stupid semantics of
	 * 4BSD pseudo-ttys makes us do it.  If we do one of these guys
	 * on the controller side, it really applies to the slave-side
	 * stream.  It should NEVER have been possible to do ANY sort
	 * of tty operations on the controller side, but it's too late
	 * to fix that now.  However, we won't waste our time implementing
	 * anything that the original pseudo-tty driver didn't handle.
	 */
	case TIOCGETP:
	case TIOCSETP:
	case TIOCSETN:
	case TIOCGETC:
	case TIOCSETC:
	case TIOCGLTC:
	case TIOCSLTC:
	case TIOCLGET:
	case TIOCLSET:
	case TIOCLBIS:
	case TIOCLBIC:
		copy_in(data, d_arg);
		if (pty->pt_stream == NULL ||
		    pty->pt_stream->sd_vnode == NULL ||
		    pty->pt_stream->sd_vnode->v_stream == NULL)
			return (EIO);

		mutex_exit(&unsafe_driver);
		err = strioctl(pty->pt_stream->sd_vnode, cmd,
			d_arg, flag, U_TO_K, cred, rvalp);
		mutex_enter(&unsafe_driver);
		return (err);

	default:
		return (ENOTTY);
	}

	return (0);
}


int
ptcpoll(dev_t dev,
	short events,
	int anyyet,
	short *reventsp,
	struct pollhead **phpp)
{
	register struct pty *pty = &pty_softc[getminor(dev)];
	register queue_t *q;
	int s;
	int pos = 0;

#ifdef lint
	anyyet = anyyet;
#endif

	*reventsp = 0;
	if (pty->pt_flags & PF_SLAVEGONE) {
		if (events & (POLLIN|POLLRDNORM))
			*reventsp |= (events & (POLLIN|POLLRDNORM));
		if (events & (POLLOUT|POLLWRNORM))
			*reventsp |= (events & (POLLOUT|POLLWRNORM));
		return (0);
	}
	s = spltty();
	if (events & (POLLIN|POLLRDNORM)) {
		if ((q = pty->pt_ttycommon.t_writeq) != NULL &&
		    q->q_first != NULL && !(pty->pt_flags & PF_STOPPED)) {
			/*
			 * Regular data is available.
			 */
			*reventsp |= (events & (POLLIN|POLLRDNORM));
			pos++;
		}
		if (pty->pt_flags & (PF_PKT|PF_UCNTL) && pty->pt_send) {
			/*
			 * A control packet is available.
			 */
			*reventsp |= (events & (POLLIN|POLLRDNORM));
			pos++;
		}
		if ((pty->pt_flags & PF_UCNTL) &&
		    (pty->pt_ucntl || pty->pt_stuffqfirst != NULL)) {
			/*
			 * "ioctl" or TIOCSTI data is available.
			 */
			*reventsp |= (events & (POLLIN|POLLRDNORM));
			pos++;
		}
		if ((pty->pt_flags & PF_43UCNTL) && pty->pt_ucntl) {
			*reventsp |= (events & (POLLIN|POLLRDNORM));
			pos++;
		}
	}
	if (events & (POLLOUT|POLLWRNORM)) {
		if ((q = pty->pt_ttycommon.t_readq) != NULL &&
		    canput(q->q_next)) {
			*reventsp |= (events & (POLLOUT|POLLWRNORM));
			pos++;
		}
	}
	if (events & POLLERR) {
		*reventsp |= POLLERR;
		pos++;
	}
	if (events == 0) {	/* "exceptional conditions" */
		if (((pty->pt_flags & (PF_PKT|PF_UCNTL)) && pty->pt_send) ||
		    ((pty->pt_flags & PF_UCNTL) &&
		    (pty->pt_ucntl || pty->pt_stuffqfirst != NULL))) {
			pos++;
		}
		if ((pty->pt_flags & PF_43UCNTL) && pty->pt_ucntl) {
			pos++;
		}
	}

	/*
	 * Arrange to have poll waken up when event occurs.
	 * if (!anyyet)
	 */
	if (!pos) {
		*phpp = &ptcph;
		*reventsp = 0;
	}

	(void) splx(s);
	return (0);
}

int
gsignal(pid, sig)
short pid;
int   sig;
{
	procset_t set;
	sigsend_t v;

	struct_zero((caddr_t)&v, sizeof (v));
	v.sig = sig;
	v.perm = 0;
	v.checkperm = 1;
	v.value.sival_ptr = NULL;

	setprocset(&set, POP_AND, P_PGID, -pid, P_ALL, P_MYID);
	sigsendset(&set, &v);
}
