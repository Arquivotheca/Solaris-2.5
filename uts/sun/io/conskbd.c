/*
 * Copyright (c) 1987 by Sun Microsystems, Inc.
 */

#ident	"@(#)conskbd.c	5.31	94/08/30 SMI"	/* SunOS4.0 5.13	*/

/*
 * Console kbd multiplexor driver for Sun.
 * The console "zs" port is linked under us, with the "kbd" module pushed
 * on top of it.
 * Minor device 0 is what programs normally use.
 * Minor device 1 is used to feed predigested keystrokes to the "workstation
 * console" driver, which it is linked beneath.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/stropts.h>
#include <sys/stream.h>
#include <sys/conf.h>
#include <sys/stat.h>
#include <sys/errno.h>
#include <sys/modctl.h>
#include <sys/kbio.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>

static int	conskbdopen();
static int	conskbdclose();
static void	conskbduwput();
static void	conskbdlrput();
static void	conskbdlwserv();
static void	conskbdioctl(queue_t *q, mblk_t *mp);


static struct module_info conskbdm_info = {
	0,
	"conskbd",
	0,
	1024,
	2048,
	128
};

static struct qinit conskbdurinit = {
	putq,
	(int (*)())NULL,
	conskbdopen,
	conskbdclose,
	(int (*)())NULL,
	&conskbdm_info,
	NULL
};

static struct qinit conskbduwinit = {
	(int (*)())conskbduwput,
	(int (*)())NULL,
	conskbdopen,
	conskbdclose,
	(int (*)())NULL,
	&conskbdm_info,
	NULL
};

static struct qinit conskbdlrinit = {
	(int (*)())conskbdlrput,
	(int (*)())NULL,
	(int (*)())NULL,
	(int (*)())NULL,
	(int (*)())NULL,
	&conskbdm_info,
	NULL
};

static struct qinit conskbdlwinit = {
	putq,
	(int (*)())conskbdlwserv,
	(int (*)())NULL,
	(int (*)())NULL,
	(int (*)())NULL,
	&conskbdm_info,
	NULL
};

static struct streamtab conskbd_str_info = {
	&conskbdurinit,
	&conskbduwinit,
	&conskbdlrinit,
	&conskbdlwinit,
};

static int conskbd_prop_op(dev_t, dev_info_t *, ddi_prop_op_t, int,
	    char *, caddr_t, int *);
static int conskbd_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg,
		void **result);
static int conskbd_identify(dev_info_t *devi);
static int conskbd_attach(dev_info_t *devi, ddi_attach_cmd_t cmd);
static int conskbd_detach(dev_info_t *devi, ddi_detach_cmd_t cmd);
static int conskbd_power(dev_info_t *devi, int cmpt, int level);

static 	struct cb_ops cb_conskbd_ops = {
	nulldev,		/* cb_open */
	nulldev,		/* cb_close */
	nodev,			/* cb_strategy */
	nodev,			/* cb_print */
	nodev,			/* cb_dump */
	nodev,			/* cb_read */
	nodev,			/* cb_write */
	nodev,			/* cb_ioctl */
	nodev,			/* cb_devmap */
	nodev,			/* cb_mmap */
	nodev,			/* cb_segmap */
	nochpoll,		/* cb_chpoll */
	conskbd_prop_op,	/* cb_prop_op */
	&conskbd_str_info,	/* cb_stream */
	D_NEW|D_MP|D_MTOUTPERIM	/* cb_flag */
};

static struct dev_ops conskbd_ops = {
	DEVO_REV,		/* devo_rev */
	0,			/* devo_refcnt */
	conskbd_info,		/* devo_getinfo */
	conskbd_identify,	/* devo_identify */
	nulldev,		/* devo_probe */
	conskbd_attach,		/* devo_attach */
	conskbd_detach,		/* devo_detach */
	nodev,			/* devo_reset */
	&(cb_conskbd_ops),	/* devo_cb_ops */
	(struct bus_ops *)NULL,	/* devo_bus_ops */
	conskbd_power		/* devo_power */
};

/*
 * Module linkage information for the kernel.
 */

static struct modldrv modldrv = {
	&mod_driverops, /* Type of module.  This one is a pseudo driver */
	"Console kbd Multiplexer driver 'conskbd'",
	&conskbd_ops,	/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1,
	&modldrv,
	NULL
};


/*
 * Module global data are protected by the per-module inner perimeter.
 */
static int	directio;

static queue_t	*regqueue;	/* regular keyboard queue above us */
static queue_t	*consqueue;	/* console queue above us */
static queue_t	*lowerqueue;	/* queue below us */

static dev_info_t *conskbd_dip;		/* private copy of devinfo pointer */
static u_long	 conskbd_timestamp;	/* time of last access */
static u_char	 conskbd_suspended;	/* True, if suspended */

/*
 * Single private "global" lock for the few rare conditions
 * we want single-threaded.
 */
static	kmutex_t	conskbdlock;

int
_init(void)
{
	int	error;

	mutex_init(&conskbdlock, "conskbd autoconfig lock", MUTEX_DRIVER, NULL);
	error = mod_install(&modlinkage);
	if (error != 0) {
		mutex_destroy(&conskbdlock);
	}
	return (error);
}


int
_fini(void)
{
	int	error;

	error = mod_remove(&modlinkage);
	if (error != 0)
		return (error);
	mutex_destroy(&conskbdlock);
	return (0);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

/*ARGSUSED*/
static int
conskbd_identify(dev_info_t *devi)
{
	if (strcmp(ddi_get_name(devi), "conskbd") == 0)
		return (DDI_IDENTIFIED);
	else
		return (DDI_NOT_IDENTIFIED);
}


/*ARGSUSED*/
static int
conskbd_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	int	power;

	switch (cmd) {
	case DDI_ATTACH:
		break;

	case DDI_RESUME:
		conskbd_suspended = 0;
		return (DDI_SUCCESS);

	default:
		return (DDI_FAILURE);

	}
	if (ddi_create_minor_node(devi, "kbd", S_IFCHR,
	    0, NULL, NULL) == DDI_FAILURE) {
		ddi_remove_minor_node(devi, NULL);
		return (-1);
	}
	conskbd_dip = devi;

	drv_getparm(TIME, &conskbd_timestamp);
	if (ddi_prop_create(DDI_DEV_T_NONE, devi, DDI_PROP_CANSLEEP,
	    "pm_timestamp", (caddr_t) &conskbd_timestamp,
	    sizeof (conskbd_timestamp)) != DDI_SUCCESS)
		cmn_err(CE_WARN, "conskbd: Can't create property\n");
	power = 1;
	if (ddi_prop_create(DDI_DEV_T_NONE, devi, DDI_PROP_CANSLEEP,
	    "pm_norm_pwr", (caddr_t) &power, sizeof (power)) != DDI_SUCCESS)
		cmn_err(CE_WARN, "conskbd: Can't create property\n");
	return (DDI_SUCCESS);
}

/*ARGSUSED*/
static int
conskbd_detach(dev_info_t *devi, ddi_detach_cmd_t cmd)
{
	switch (cmd) {
	case DDI_PM_SUSPEND:
		conskbd_suspended = 1;
		return (DDI_SUCCESS);

	case DDI_DETACH:
	default:
		return (DDI_FAILURE);
	}
}

/*ARGSUSED*/
static int
conskbd_power(dev_info_t *devi, int cmpt, int level)
{
	if (cmpt != 0 || 0 > level || level > 1)
		return (DDI_FAILURE);
	return (DDI_SUCCESS);
}

/* ARGSUSED */
static int
conskbd_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg,
	void **result)
{
	register int error;

	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		if (conskbd_dip == NULL) {
			error = DDI_FAILURE;
		} else {
			*result = (void *) conskbd_dip;
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

static int
conskbd_prop_op(dev_t dev, dev_info_t *dip, ddi_prop_op_t prop_op,
	    int mod_flags, char *name, caddr_t valuep, int *lengthp)
{
	if (strcmp(name, "pm_timestamp") == 0 &&
	    ddi_prop_modify(DDI_DEV_T_NONE, conskbd_dip, mod_flags, name,
	    (caddr_t)&conskbd_timestamp, sizeof (conskbd_timestamp))
	    != DDI_PROP_SUCCESS) {
		cmn_err(CE_WARN, "conskbd: Can't modify property");
	}

	return (ddi_prop_op(dev, dip, prop_op, mod_flags, name, valuep,
	    lengthp));

}

/*ARGSUSED*/
static int
conskbdopen(q, devp, flag, sflag, crp)
	queue_t *q;
	dev_t	*devp;
	int	flag, sflag;
	cred_t	*crp;
{
	register dev_t unit;

	unit = getminor(*devp);
	if (unit == 0) {
		/*
		 * Opening "/dev/kbd".
		 */
		if (lowerqueue == NULL) {
			/* nothing linked below us - you lose */
			return (EIO);
		}
		regqueue = q;
	} else if (unit == 1) {
		/*
		 * Opening the device to be linked under the console.
		 */
		consqueue = q;
	} else {
		/* we don't do that under Bozo's Big Tent */
		return (ENODEV);
	}
	qprocson(q);
	return (0);
}


/*ARGSUSED*/
static int
conskbdclose(q, flag, crp)
	queue_t *q;
	int	flag;
	cred_t	*crp;
{
	qprocsoff(q);
	if (q == regqueue) {
		directio = 0;	/* nothing to send it to */
		regqueue = NULL;
	} else if (q == consqueue) {
		/*
		 * Well, this is probably a mistake, but we will permit you
		 * to close the path to the console if you really insist.
		 */
		consqueue = NULL;
	}

	return (0);
}

/*
 * Put procedure for upper write queue.
 */
static void
conskbduwput(q, mp)
	register queue_t *q;
	register mblk_t *mp;
{
	switch (mp->b_datap->db_type) {

	case M_IOCTL:
		conskbdioctl(q, mp);
		break;

	case M_FLUSH:
		if (*mp->b_rptr & FLUSHW) {
			flushq(q, FLUSHDATA);
			*mp->b_rptr &= ~FLUSHW;
		}
		if (*mp->b_rptr & FLUSHR) {
			flushq(RD(q), FLUSHDATA);
			qreply(q, mp);
		} else
			freemsg(mp);
		break;

	case M_DATA:
		if (lowerqueue == NULL)
			goto bad;
		(void) putq(lowerqueue, mp);
		break;

	default:
	bad:
		/*
		 * Pass an error message up.
		 */
		mp->b_datap->db_type = M_ERROR;
		if (mp->b_cont) {
			freemsg(mp->b_cont);
			mp->b_cont = NULL;
		}
		mp->b_rptr = mp->b_datap->db_base;
		mp->b_wptr = mp->b_rptr + sizeof (char);
		*mp->b_rptr = EINVAL;
		qreply(q, mp);
	}
}

static void
conskbdioctl(q, mp)
	register queue_t *q;
	register mblk_t *mp;
{
	register struct iocblk *iocp;
	register struct linkblk *linkp;

	iocp = (struct iocblk *)mp->b_rptr;

	switch (iocp->ioc_cmd) {

	case I_LINK:	/* stupid, but permitted */
	case I_PLINK:
		if (lowerqueue != NULL) {
			iocp->ioc_error = EINVAL;	/* XXX */
			goto iocnak;
		}
		mutex_enter(&conskbdlock);
		linkp = (struct linkblk *)mp->b_cont->b_rptr;
		lowerqueue = linkp->l_qbot;
		mutex_exit(&conskbdlock);
		iocp->ioc_count = 0;
		break;

	case I_UNLINK:	/* stupid, but permitted */
	case I_PUNLINK:
		mutex_enter(&conskbdlock);
		linkp = (struct linkblk *)mp->b_cont->b_rptr;
		if (lowerqueue != linkp->l_qbot) {
			iocp->ioc_error = EINVAL;	/* XXX */
			mutex_exit(&conskbdlock);
			goto iocnak;	/* not us */
		}
		lowerqueue = NULL;
		mutex_exit(&conskbdlock);
		iocp->ioc_count = 0;
		break;

	case KIOCGDIRECT: {
		register mblk_t *datap;

		if ((datap = allocb((int) sizeof (int), BPRI_MED)) == NULL) {
			iocp->ioc_error = ENOMEM;
			goto iocnak;
		}

		*(int *)datap->b_wptr = directio;
		datap->b_wptr += (sizeof (int)/sizeof *datap->b_wptr);
		if (mp->b_cont) {
			freemsg(mp->b_cont);
			mp->b_cont = NULL;
		}
		mp->b_cont = datap;
		iocp->ioc_count = sizeof (int);
		break;
	}

	case KIOCSDIRECT:
		directio = *(int *)mp->b_cont->b_rptr;

		/*
		 * Pass this through, if there's something to pass
		 * it through to, so the system keyboard can reset
		 * itself.
		 */
		if (lowerqueue != NULL) {
			(void) putq(lowerqueue, mp);
			return;
		}
		iocp->ioc_count = 0;
		break;

	default:
		/*
		 * Pass this through, if there's something to pass it
		 * through to; otherwise, reject it.
		 */
		if (lowerqueue != NULL) {
			(void) putq(lowerqueue, mp);
			return;
		}
		iocp->ioc_error = EINVAL;
		goto iocnak;	/* nobody below us; reject it */
	}

	/*
	 * Common exit path for calls that return a positive
	 * acknowledgment with a return value of 0.
	 */
	iocp->ioc_rval = 0;
	iocp->ioc_error = 0;	/* brain rot */
	mp->b_datap->db_type = M_IOCACK;
	qreply(q, mp);
	return;

iocnak:
	iocp->ioc_rval = 0;
	mp->b_datap->db_type = M_IOCNAK;
	qreply(q, mp);
}

/*
 * Service procedure for lower write queue.
 * Puts things on the queue below us, if it lets us.
 */
static void
conskbdlwserv(q)
	register queue_t *q;
{
	register mblk_t *mp;

	while (canput(q->q_next) && (mp = getq(q)) != NULL)
		putnext(q, mp);
}

/*
 * Put procedure for lower read queue.
 * Pass everything up to minor device 0 if "directio" set, otherwise to minor
 * device 1.
 */
static void
conskbdlrput(q, mp)
	register queue_t *q;
	register mblk_t *mp;
{
	if (conskbd_suspended)
		ddi_dev_is_needed(conskbd_dip, 0, 1);
	drv_getparm(TIME, &conskbd_timestamp);

	switch (mp->b_datap->db_type) {

	case M_FLUSH:
		if (*mp->b_rptr == FLUSHR) {
			flushq(q, FLUSHDATA);	/* XXX doesn't flush M_DELAY */
			*mp->b_rptr &= ~FLUSHR;	/* it has been flushed */
		}
		if (*mp->b_rptr == FLUSHW) {
			flushq(WR(q), FLUSHDATA);
			qreply(q, mp);	/* give the read queues a crack at it */
		} else
			freemsg(mp);
		break;

	case M_ERROR:
	case M_HANGUP:
		/* XXX - should we tell the upper queue(s) about this? */
		freemsg(mp);
		break;

	case M_DATA:
		if (directio)
			putnext(regqueue, mp);
		else {
			if (consqueue != NULL)
				putnext(consqueue, mp);
		}
		break;

	case M_IOCACK:
	case M_IOCNAK:
		if (regqueue != NULL)
			putnext(regqueue, mp);
		else
			freemsg(mp);
		break;

	default:
		freemsg(mp);	/* anything useful here? */
		break;
	}
}
