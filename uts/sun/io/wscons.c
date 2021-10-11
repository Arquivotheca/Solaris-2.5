/*
 * Copyright (c) 1987-1990, 1993, by Sun Microsystems, Inc.
 */

#pragma	ident	"@(#)wscons.c	1.60	94/11/16 SMI"

/*
 * "Workstation console" multiplexor driver for Sun.
 *
 * Sends output to the primary frame buffer using the PROM monitor;
 * gets input from a stream linked below us that is the "keyboard
 * driver", below which is linked the primary keyboard.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/signal.h>
#include <sys/cred.h>
#include <sys/vnode.h>
#include <sys/termios.h>
#include <sys/termio.h>
#include <sys/ttold.h>
#include <sys/stropts.h>
#include <sys/stream.h>
#include <sys/tty.h>
#include <sys/buf.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <sys/kmem.h>
#include <sys/cpuvar.h>
#include <sys/kbio.h>
#include <sys/strredir.h>
#include <sys/fs/snode.h>
#include <sys/consdev.h>
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/debug.h>

#define	MINLINES	10
#define	MAXLINES	48
#define	LOSCREENLINES	34
#define	HISCREENLINES	48

#define	MINCOLS		10
#define	MAXCOLS		120
#define	LOSCREENCOLS	80
#define	HISCREENCOLS	120

#define	IFLAGS	(CS7|CREAD|PARENB)

static struct {
	int	wc_flags;		/* random flags (protected by */
					/* write-side exclusion lock  */
	dev_t	wc_dev;			/* major/minor for this device */
	tty_common_t wc_ttycommon;	/* data common to all tty drivers */
	int	wc_pendc;		/* pending output character */
	queue_t	*wc_kbdqueue;		/* "console keyboard" device queue */
					/* below us */
	int	wc_defer_output;	/* set if output device is "slow" */
	int	wc_bufcallid;		/* id returned by qbufcall */
	int	wc_timeoutid;		/* id returned by qtimeout */
} wscons;

#define	WCS_ISOPEN	0x00000001	/* open is complete */
#define	WCS_STOPPED	0x00000002	/* output is stopped */
#define	WCS_DELAY	0x00000004	/* waiting for delay to finish */
#define	WCS_BUSY	0x00000008	/* waiting for transmission to finish */

static int	wcopen();
static int	wcclose();
static int	wcuwput();
static int	wclrput();

static struct module_info wcm_info = {
	0,
	"wc",
	0,
	INFPSZ,
	2048,
	128
};

static struct qinit wcurinit = {
	putq,
	NULL,
	wcopen,
	wcclose,
	NULL,
	&wcm_info,
	NULL
};

static struct qinit wcuwinit = {
	wcuwput,
	NULL,
	wcopen,
	wcclose,
	NULL,
	&wcm_info,
	NULL
};

static struct qinit wclrinit = {
	wclrput,
	NULL,
	NULL,
	NULL,
	NULL,
	&wcm_info,
	NULL
};

static struct qinit wclwinit = {
	putq,
	NULL,
	NULL,
	NULL,
	NULL,
	&wcm_info,
	NULL
};

static struct streamtab wcinfo = {
	&wcurinit,
	&wcuwinit,
	&wclrinit,
	&wclwinit,
};

static int wc_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg,
		void **result);
static int wc_identify(dev_info_t *devi);
static int wc_attach(dev_info_t *devi, ddi_attach_cmd_t cmd);
static dev_info_t *wc_dip;

#define	WC_CONF_FLAG	(D_NEW|D_MTPERMOD|D_MP)
	DDI_DEFINE_STREAM_OPS(wc_ops, wc_identify, nulldev,	\
		wc_attach, nodev, nodev,			\
		wc_info, WC_CONF_FLAG, &wcinfo);

static void	wcreioctl(long unit);
static void 	wcioctl(queue_t *q, mblk_t *mp);
static void	wcopoll(void);
static void	wcrstrt(void);
static void	wcstart(void);
static void	wconsout(caddr_t);

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/modctl.h>

extern	int	prom_stdout_is_framebuffer(void);
extern	int	prom_mayput(char c);

extern nodev(), nulldev();
extern dseekneg_flag;
extern struct mod_ops mod_driverops;
static struct dev_ops wc_ops;

/*
 * Module linkage information for the kernel.
 */

static struct modldrv modldrv = {
	&mod_driverops, /* Type of module.  This one is a pseudo driver */
	"Workstation multiplexer Driver 'wc'",
	&wc_ops,	/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1,
	&modldrv,
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
wc_identify(dev_info_t *devi)
{
	if (strcmp(ddi_get_name(devi), "wc") == 0)
		return (DDI_IDENTIFIED);
	else
		return (DDI_NOT_IDENTIFIED);
}

/*ARGSUSED*/
static int
wc_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	if (ddi_create_minor_node(devi, "wscons", S_IFCHR,
	    0, NULL, NULL) == DDI_FAILURE) {
		ddi_remove_minor_node(devi, NULL);
		return (-1);
	}
	wc_dip = devi;
	return (DDI_SUCCESS);
}

/* ARGSUSED */
static int
wc_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg,
	void **result)
{
	register int error;

	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		if (wc_dip == NULL) {
			error = DDI_FAILURE;
		} else {
			*result = (void *) wc_dip;
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
 * Output buffer. Protected by the per-module inner perimeter.
 */
#define	MAXHIWAT	2000
static char obuf[MAXHIWAT];

/*ARGSUSED*/
static int
wcopen(q, devp, flag, sflag, crp)
	register queue_t *q;
	dev_t		 *devp;
	int		 flag, sflag;
	cred_t		 *crp;
{
	struct termios *termiosp;
	int len;

	if (getminor(*devp) != 0)
		return (ENXIO);		/* sorry, only one per customer */

	if (!(wscons.wc_flags & WCS_ISOPEN)) {
		mutex_init(&wscons.wc_ttycommon.t_excl,
			"wscons ttycommon", MUTEX_DEFAULT,
			DEFAULT_WT);
		wscons.wc_ttycommon.t_iflag = 0;
		/*
		 * Get the default termios settings (cflag).
		 * These are stored as a property in the
		 * "options" node.
		 */
		if (ddi_getlongprop(DDI_DEV_T_ANY,
		    ddi_root_node(), 0, "ttymodes",
		    (caddr_t) &termiosp, &len) == DDI_PROP_SUCCESS &&
		    len == sizeof (struct termios)) {

			wscons.wc_ttycommon.t_cflag = termiosp->c_cflag;
			kmem_free(termiosp, len);
		} else {
			/*
			 * Gack!  Whine about it.
			 */
			cmn_err(CE_WARN,
			    "wc: Couldn't get ttymodes property!\n");
		}
		wscons.wc_ttycommon.t_iocpending = NULL;
		wscons.wc_flags = WCS_ISOPEN;
		/*
		 * If we can have either a normal or high-resolution screen,
		 * we should indicate the size of the screen.  Otherwise, we
		 * just say zero and let the program get the standard 34x80
		 * value from the "termcap" or "terminfo" file.
		 */
		wscons.wc_ttycommon.t_size.ws_row = 0;
		wscons.wc_ttycommon.t_size.ws_col = 0;
		wscons.wc_ttycommon.t_size.ws_xpixel = 0;
		wscons.wc_ttycommon.t_size.ws_ypixel = 0;

		/*
		 * If we're talking direct to a framebuffer, we assume
		 * that it's a "slow" device, so that rendering should be
		 * deferred to a timeout or softcall so that we write
		 * a bunch of characters at once.
		 */
		wscons.wc_defer_output = prom_stdout_is_framebuffer();
#ifdef	sun
		{
			u_short t_cols = 0;
			u_short t_rows = 0;
			register char *p;
			auto char wrkbuf[16];
			static char *cols = "screen-#columns";
			static char *rows = "screen-#rows";
			int plen, r;

			plen = 15;
			r = ddi_getlongprop_buf(DDI_DEV_T_ANY, wc_dip, 0,
			    cols, (caddr_t) wrkbuf, &plen);
			if (r != DDI_PROP_SUCCESS || plen < 0 || plen > 15) {
				goto gotcol;
			}
			p = wrkbuf;
			while (p < &wrkbuf[plen]) {
				t_cols *= 10;
				t_cols += *p++ - '0';
				if (*p == 0)
					break;
			}
gotcol:
			if (t_cols < MINCOLS)
				t_cols = LOSCREENCOLS;
			else if (t_cols > MAXCOLS)
				t_cols = HISCREENCOLS;
			wscons.wc_ttycommon.t_size.ws_col = t_cols;

			plen = 15;
			r = ddi_getlongprop_buf(DDI_DEV_T_ANY, wc_dip, 0,
			    rows, (caddr_t) wrkbuf, &plen);
			if (r != DDI_PROP_SUCCESS || plen < 0 || plen > 15) {
				goto gotrows;
			}
			p = wrkbuf;
			while (p < &wrkbuf[plen]) {
				t_rows *= 10;
				t_rows += *p++ - '0';
				if (*p == 0)
					break;
			}
gotrows:
			if (t_rows < MINLINES)
				t_rows = LOSCREENLINES;
			else if (t_rows > MAXLINES)
				t_rows = HISCREENLINES;
			wscons.wc_ttycommon.t_size.ws_row = t_rows;
		}
#endif	/* sun */
	}
	if (wscons.wc_ttycommon.t_flags & TS_XCLUDE) {
		if (drv_priv(crp)) {
			return (EBUSY);
		}
	}
	wscons.wc_ttycommon.t_readq = q;
	wscons.wc_ttycommon.t_writeq = WR(q);
	qprocson(q);
	return (0);
}

/*ARGSUSED*/
static int
wcclose(q, flag, crp)
	queue_t *q;
	int	flag;
	cred_t	*crp;
{
	qprocsoff(q);
	if (wscons.wc_bufcallid != 0) {
		qunbufcall(q, wscons.wc_bufcallid);
		wscons.wc_bufcallid = 0;
	}
	if (wscons.wc_timeoutid != 0) {
		quntimeout(q, wscons.wc_timeoutid);
		wscons.wc_timeoutid = 0;
	}
	ttycommon_close(&wscons.wc_ttycommon);
	wscons.wc_flags = 0;
	return (0);
}

/*
 * Put procedure for upper write queue.
 * Respond to M_STOP, M_START, M_IOCTL, and M_FLUSH messages here;
 * queue up M_BREAK, M_DELAY, and M_DATA messages for processing by
 * the start routine, and then call the start routine; discard
 * everything else.
 */
static int
wcuwput(q, mp)
	register queue_t *q;
	register mblk_t *mp;
{
	switch (mp->b_datap->db_type) {

	case M_STOP:
		wscons.wc_flags |= WCS_STOPPED;
		freemsg(mp);
		break;

	case M_START:
		wscons.wc_flags &= ~WCS_STOPPED;
		wcstart();
		freemsg(mp);
		break;

	case M_IOCTL: {
		register struct iocblk *iocp;
		register struct linkblk *linkp;

		iocp = (struct iocblk *)mp->b_rptr;
		switch (iocp->ioc_cmd) {

		case I_LINK:	/* stupid, but permitted */
		case I_PLINK:
			if (wscons.wc_kbdqueue != NULL)
				goto iocnak;	/* somebody already linked */
			linkp = (struct linkblk *)mp->b_cont->b_rptr;
			wscons.wc_kbdqueue = linkp->l_qbot;
			mp->b_datap->db_type = M_IOCACK;
			iocp->ioc_count = 0;
			qreply(q, mp);
			break;

		case I_UNLINK:	/* stupid, but permitted */
		case I_PUNLINK:
			linkp = (struct linkblk *)mp->b_cont->b_rptr;
			if (wscons.wc_kbdqueue != linkp->l_qbot)
				goto iocnak;	/* not us */
			wscons.wc_kbdqueue = NULL;
			mp->b_datap->db_type = M_IOCACK;
			iocp->ioc_count = 0;
			qreply(q, mp);
			break;

		iocnak:
			mp->b_datap->db_type = M_IOCNAK;
			qreply(q, mp);
			break;

		case TCSETSW:
		case TCSETSF:
		case TCSETAW:
		case TCSETAF:
		case TCSBRK:
			/*
			 * The changes do not take effect until all
			 * output queued before them is drained.
			 * Put this message on the queue, so that
			 * "wcstart" will see it when it's done
			 * with the output before it.  Poke the
			 * start routine, just in case.
			 */
			(void) putq(q, mp);
			wcstart();
			break;

		case KIOCSDIRECT:
			if (wscons.wc_kbdqueue != NULL) {
				(void) putq(wscons.wc_kbdqueue, mp);
				qenable(wscons.wc_kbdqueue);
				break;
			}
			/* fall through */

		default:
			/*
			 * Do it now.
			 */
			wcioctl(q, mp);
			break;
		}
		break;
	}

	case M_FLUSH:
		if (*mp->b_rptr & FLUSHW) {
			/*
			 * Flush our write queue.
			 */
			flushq(q, FLUSHDATA);	/* XXX doesn't flush M_DELAY */
			*mp->b_rptr &= ~FLUSHW;	/* it has been flushed */
		}
		if (*mp->b_rptr & FLUSHR) {
			flushq(RD(q), FLUSHDATA);
			qreply(q, mp);	/* give the read queues a crack at it */
		} else
			freemsg(mp);
		break;

	case M_BREAK:
		/*
		 * Ignore these, as they make no sense.
		 */
		freemsg(mp);
		break;

	case M_DELAY:
	case M_DATA:
		/*
		 * Queue the message up to be transmitted,
		 * and poke the start routine.
		 */
		(void) putq(q, mp);
		wcstart();
		break;

	default:
		/*
		 * "No, I don't want a subscription to Chain Store Age,
		 * thank you anyway."
		 */
		freemsg(mp);
		break;
	}

	return (0);
}

/*
 * Retry an "ioctl", now that "qbufcall" claims we may be able to allocate
 * the buffer we need.
 */
/*ARGSUSED*/
static void
wcreioctl(unit)
	long unit;
{
	queue_t *q;
	register mblk_t *mp;

	wscons.wc_bufcallid = 0;
	q = wscons.wc_ttycommon.t_writeq;
	if ((mp = wscons.wc_ttycommon.t_iocpending) != NULL) {
		/* not pending any more */
		wscons.wc_ttycommon.t_iocpending = NULL;
		wcioctl(q, mp);
	}
}

/*
 * Process an "ioctl" message sent down to us.
 */
static void
wcioctl(q, mp)
	queue_t *q;
	register mblk_t *mp;
{
	register struct iocblk *iocp;
	register unsigned datasize;
	int error;

	iocp = (struct iocblk *)mp->b_rptr;

	if (iocp->ioc_cmd == TIOCSWINSZ) {
		/*
		 * Ignore all attempts to set the screen size; the value in the
		 * EEPROM is guaranteed (modulo PROM bugs) to be the value used
		 * by the PROM monitor code, so it is by definition correct.
		 * Many programs (e.g., "login" and "tset") will attempt to
		 * reset the size to (0, 0) or (34, 80), neither of which is
		 * necessarily correct.
		 * We just ACK the message, so as not to disturb programs that
		 * set the sizes.
		 */
		iocp->ioc_count = 0;	/* no data returned */
		mp->b_datap->db_type = M_IOCACK;
		qreply(q, mp);
		return;
	}

	/*
	 * The only way in which "ttycommon_ioctl" can fail is if the "ioctl"
	 * requires a response containing data to be returned to the user,
	 * and no mblk could be allocated for the data.
	 * No such "ioctl" alters our state.  Thus, we always go ahead and
	 * do any state-changes the "ioctl" calls for.  If we couldn't allocate
	 * the data, "ttycommon_ioctl" has stashed the "ioctl" away safely, so
	 * we just call "qbufcall" to request that we be called back when we
	 * stand a better chance of allocating the data.
	 */
	if ((datasize =
	    ttycommon_ioctl(&wscons.wc_ttycommon, q, mp, &error)) != 0) {
		if (wscons.wc_bufcallid != 0)
			qunbufcall(q, wscons.wc_bufcallid);
		wscons.wc_bufcallid = qbufcall(q, (int)datasize, BPRI_HI,
						wcreioctl, (long)0);
		return;
	}

	if (error < 0) {
		if (iocp->ioc_cmd == TCSBRK)
			error = 0;
		else
			error = EINVAL;
	}
	if (error != 0) {
		iocp->ioc_error = error;
		mp->b_datap->db_type = M_IOCNAK;
	}
	qreply(q, mp);
}

static void
wcopoll()
{
	queue_t *q;

	q = wscons.wc_ttycommon.t_writeq;
	wscons.wc_timeoutid = 0;
	/* See if we can continue output */
	if ((wscons.wc_flags & WCS_BUSY) && wscons.wc_pendc != -1) {
		if (prom_mayput(wscons.wc_pendc) == 0) {
			wscons.wc_pendc = -1;
			wscons.wc_flags &= ~WCS_BUSY;
			if (!(wscons.wc_flags&(WCS_DELAY|WCS_STOPPED)))
				wcstart();
		} else
			wscons.wc_timeoutid = qtimeout(q, wcopoll,
							(caddr_t) 0, 1);
	}
}

/*
 * Restart output on the console after a timeout.
 */
static void
wcrstrt()
{
	ASSERT(wscons.wc_ttycommon.t_writeq != NULL);
	wscons.wc_flags &= ~WCS_DELAY;
	wcstart();
}

/*
 * Start console output
 */
static void
wcstart()
{
	register int c;
	register int cc;
	register queue_t *q;
	register mblk_t *bp;
	register mblk_t *nbp;

	/*
	 * If we're waiting for something to happen (delay timeout to
	 * expire, current transmission to finish, output to be
	 * restarted, output to finish draining), don't grab anything
	 * new.
	 */
	if (wscons.wc_flags & (WCS_DELAY|WCS_BUSY|WCS_STOPPED))
		goto out;

	q = wscons.wc_ttycommon.t_writeq;
	/*
	 * assumes that we have been called by whoever holds the
	 * exclusionary lock on the write-side queue (protects
	 * wc_flags and wc_pendc).
	 */
	for (;;) {
		if ((bp = getq(q)) == NULL)
			goto out;	/* nothing to transmit */

		/*
		 * We have a new message to work on.
		 * Check whether it's a delay or an ioctl (the latter
		 * occurs if the ioctl in question was waiting for the output
		 * to drain).  If it's one of those, process it immediately.
		 */
		switch (bp->b_datap->db_type) {

		case M_DELAY:
			/*
			 * Arrange for "wcrstrt" to be called when the
			 * delay expires; it will turn WCS_DELAY off,
			 * and call "wcstart" to grab the next message.
			 */
			if (wscons.wc_timeoutid != 0)
				quntimeout(q, wscons.wc_timeoutid);
			wscons.wc_timeoutid = qtimeout(q, wcrstrt, (caddr_t)0,
			    (int)(*(unsigned char *)bp->b_rptr + 6));
			wscons.wc_flags |= WCS_DELAY;
			freemsg(bp);
			goto out;	/* wait for this to finish */

		case M_IOCTL:
			/*
			 * This ioctl was waiting for the output ahead of
			 * it to drain; obviously, it has.  Do it, and
			 * then grab the next message after it.
			 */
			wcioctl(q, bp);
			continue;
		}

		if ((cc = bp->b_wptr - bp->b_rptr) == 0) {
			freemsg(bp);
			continue;
		}

		/*
		 * Direct output to the frame buffer if this device
		 * is not the "hardware" console.
		 */
		if (wscons.wc_defer_output) {
			/*
			 * Never do output here;
			 * it takes forever.
			 */
			wscons.wc_flags |= WCS_BUSY;
			wscons.wc_pendc = -1;
			if (q->q_count > 128) {	/* do it soon */
				softcall(wconsout, (caddr_t)0);
			} else {		/* wait a bit */
				if (wscons.wc_timeoutid != 0)
					quntimeout(q, wscons.wc_timeoutid);
				wscons.wc_timeoutid = qtimeout(q, wconsout,
							(caddr_t) 0, hz/30);
			}
			(void) putbq(q, bp);
			goto out;
		}

		for (;;) {
			c = *bp->b_rptr++;
			cc--;
			if (prom_mayput(c) != 0) {
				wscons.wc_flags |= WCS_BUSY;
				wscons.wc_pendc = c;
				if (wscons.wc_timeoutid != 0)
					quntimeout(q, wscons.wc_timeoutid);
				wscons.wc_timeoutid = qtimeout(q, wcopoll,
								(caddr_t)0, 1);
				if (bp != NULL)
				/* not done with this message yet */
					(void) putbq(q, bp);
				goto out;
			}
			while (cc <= 0) {
				nbp = bp;
				bp = bp->b_cont;
				freeb(nbp);
				if (bp == NULL)
					goto out;
				cc = bp->b_wptr - bp->b_rptr;
			}
		}
	}
out:
	;
}

/*
 * Output to frame buffer console.
 * It takes a long time to scroll.
 */
/* ARGSUSED */
static void
wconsout(caddr_t dummy)
{
	register u_char *cp;
	register int cc;
	register queue_t *q;
	register mblk_t *bp;
	mblk_t *nbp;
	register char *current_position;
	register int bytes_left;

	if ((q = wscons.wc_ttycommon.t_writeq) == NULL) {
		return;	/* not attached to a stream */
	}

	/*
	 * Set up to copy up to MAXHIWAT bytes.
	 */
	current_position = &obuf[0];
	bytes_left = MAXHIWAT;
	while ((bp = getq(q)) != NULL) {
		if (bp->b_datap->db_type == M_IOCTL) {
			/*
			 * This ioctl was waiting for the output ahead of
			 * it to drain; obviously, it has.  Put it back
			 * so that "wcstart" can handle it, and transmit
			 * what we've got.
			 */
			(void) putbq(q, bp);
			goto transmit;
		}

		do {
			cp = bp->b_rptr;
			cc = bp->b_wptr - cp;
			while (cc != 0) {
				if (bytes_left == 0) {
					/*
					 * Out of buffer space; put this
					 * buffer back on the queue, and
					 * transmit what we have.
					 */
					bp->b_rptr = cp;
					(void) putbq(q, bp);
					goto transmit;
				}
				*current_position++ = *cp++;
				cc--;
				bytes_left--;
			}
			nbp = bp;
			bp = bp->b_cont;
			freeb(nbp);
		} while (bp != NULL);
	}

transmit:
	if ((cc = MAXHIWAT - bytes_left) != 0) {
		if (ncpus > 1 && fbvp) {
			struct snode *csp = VTOS(VTOS(fbvp)->s_commonvp);

			/*
			 * Holding this mutex prevents other cpus from adding
			 * mappings while we're in the PROM on this cpu.
			 */
			mutex_enter(&csp->s_lock);
			cnputs(obuf, cc, csp->s_mapcnt != 0);
			mutex_exit(&csp->s_lock);
		} else {
			cnputs(obuf, cc, 0);
		}
	}
	wscons.wc_flags &= ~WCS_BUSY;
	wcstart();
}

/*
 * Put procedure for lower read queue.
 * Pass everything up to queue above "upper half".
 */
static int
wclrput(q, mp)
	register queue_t *q;
	register mblk_t *mp;
{
	register queue_t *upq;

	switch (mp->b_datap->db_type) {

	case M_FLUSH:
		if (*mp->b_rptr == FLUSHW || *mp->b_rptr == FLUSHRW) {
			/*
			 * Flush our write queue.
			 */
			/* XXX doesn't flush M_DELAY */
			flushq(WR(q), FLUSHDATA);
			*mp->b_rptr = FLUSHR;	/* it has been flushed */
		}
		if (*mp->b_rptr == FLUSHR || *mp->b_rptr == FLUSHRW) {
			flushq(q, FLUSHDATA);
			*mp->b_rptr = FLUSHW;	/* it has been flushed */
			qreply(q, mp);	/* give the read queues a crack at it */
		} else
			freemsg(mp);
		break;

	case M_ERROR:
	case M_HANGUP:
		if ((upq = wscons.wc_ttycommon.t_readq) != NULL)
			putnext(q, mp);
		else
			freemsg(mp);
		break;

	case M_DATA:
		if ((upq = wscons.wc_ttycommon.t_readq) != NULL) {
			if (!canput(upq->q_next)) {
				ttycommon_qfull(&wscons.wc_ttycommon, upq);
				wcstart();
				freemsg(mp);
			} else
				putnext(upq, mp);
		} else
			freemsg(mp);
		break;

	default:
		freemsg(mp);	/* anything useful here? */
		break;
	}

	return (0);
}

/*
 * Auxiliary routines, for allowing the workstation console to be redirected.
 */

/*
 * Given a minor device number for a wscons instance, return a held vnode for
 * it.
 *
 * We currently support only one instance, for the "workstation console".
 */
int
wcvnget(unit, vpp)
	int	unit;
	vnode_t	**vpp;
{
	if (unit != 0)
		return (ENXIO);

	/*
	 * rwsconsvp is already held, so we don't have to do it here.
	 */
	*vpp = rwsconsvp;
	return (0);
}

/*
 * Release the vnode that wcvnget returned.
 */
/* ARGSUSED */
void
wcvnrele(unit, vp)
	int	unit;
	vnode_t	*vp;
{
	/*
	 * Nothing to do, since we only support the workstation console
	 * instance that's held throughout the system's lifetime.
	 */
}

/*
 * The declaration and initialization of the wscons_srvnops has been
 * moved to space.c to allow "wc" to become a loadable module.
 */
