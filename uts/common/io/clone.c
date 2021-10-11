/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)clone.c	1.36	94/09/29 SMI" /* from S5R4 1.10 */

/*
 * Clone Driver.
 */

#include "sys/types.h"
#include "sys/param.h"
#include "sys/errno.h"
#include "sys/signal.h"
#include "sys/vfs.h"
#include "sys/vnode.h"
#include "sys/pcb.h"
#include "sys/user.h"
#include "sys/stropts.h"
#include "sys/stream.h"
#include "sys/errno.h"
#include "sys/sysinfo.h"
#include "sys/ddi.h"
#include "sys/systm.h"
#include "sys/conf.h"
#include "sys/debug.h"
#include "sys/cred.h"
#include "sys/mkdev.h"
#include "sys/open.h"
#include "sys/strsubr.h"

#include "sys/sunddi.h"

struct vnode *makespecvp();	/* Device file system */

int clnopen();
static struct module_info clnm_info = { 0, "CLONE", 0, 0, 0, 0 };
static struct qinit clnrinit = { NULL, NULL, clnopen, NULL, NULL,
					&clnm_info, NULL };
static struct qinit clnwinit = { NULL, NULL, NULL, NULL, NULL,
					&clnm_info, NULL };

struct streamtab clninfo = { &clnrinit, &clnwinit };

/*
 * XXX: old conf.c entry had d_open filled plus a d_str filled in?
 * XXX: can't see why, thus it's not here.
 */

static int cln_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg,
		void **result);
static int cln_identify(dev_info_t *devi);
static int cln_attach(dev_info_t *devi, ddi_attach_cmd_t cmd);
static dev_info_t *cln_dip;		/* private copy of devinfo pointer */

#define	CLONE_CONF_FLAG		(D_NEW|D_MP)
	DDI_DEFINE_STREAM_OPS(clone_ops, cln_identify, nulldev,	\
			cln_attach, nodev, nodev,		\
			cln_info, CLONE_CONF_FLAG, &clninfo);

#define	CBFLAG(maj)	(devopsp[(maj)]->devo_cb_ops->cb_flag)

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/modctl.h>


extern nodev(), nulldev();
extern dseekneg_flag;
extern struct mod_ops mod_driverops;
extern struct dev_ops clone_ops;

/*
 * Module linkage information for the kernel.
 */

static struct modldrv modldrv = {
	&mod_driverops, /* Type of module.  This one is a pseudo driver */
	"Clone Pseudodriver 'clone'",
	&clone_ops,	/* driver ops */
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
	/*
	 * Since the clone driver's reference count is unreliable,
	 * make sure we are never unloaded.
	 */
	return (EBUSY);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

static int
cln_identify(dev_info_t *devi)
{
	if (strcmp(ddi_get_name(devi), "clone") == 0)
		return (DDI_IDENTIFIED);
	else
		return (DDI_NOT_IDENTIFIED);
}

/* ARGSUSED */
static int
cln_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	cln_dip = devi;
	return (DDI_SUCCESS);
}

/* ARGSUSED */
static int
cln_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg,
	void **result)
{
	register int error;

	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		if (cln_dip == NULL) {
			error = DDI_FAILURE;
		} else {
			*result = (void *) cln_dip;
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
 * Clone open.  Maj is the major device number of the streams
 * device to open.  Look up the device in the cdevsw[].  Attach
 * its qinit structures to the read and write queues and call its
 * open with the sflag set to CLONEOPEN.  Swap in a new vnode with
 * the real device number constructed from either
 *	a) for old-style drivers:
 *		maj and the minor returned by the device open, or
 *	b) for new-style drivers:
 *		the whole dev passed back as a reference parameter
 *		from the device open.
 */
int
clnopen(rq, devp, flag, sflag, crp)
	register queue_t *rq;
	dev_t *devp;
	int flag;
	int sflag;
	cred_t *crp;
{
	register struct streamtab *stp;
	dev_t newdev;
	int error = 0;
	klwp_id_t lwp;
	major_t maj;
	minor_t emaj;
	int	safety;
	struct qinit *rinit, *winit;
	u_long	qflag, sqtype;

	if (sflag)
		return (ENXIO);

	/*
	 * Get the device to open.
	 */
	emaj = getminor(*devp); /* minor is major for a cloned drivers */
	maj = etoimajor(emaj);	/* get internal major of cloned driver */

	if (maj >= devcnt)
		return (ENXIO);

	/*
	 * XXX: there is no corresponding release for clone driver opens.
	 */
	if (ddi_hold_installed_driver(maj) == NULL)
		return (ENXIO);

	if ((stp = STREAMSTAB(maj)) == NULL) {
		ddi_rele_driver(maj);
		return (ENXIO);
	}

	newdev = makedevice(emaj, 0);	/* create new style device number  */

	safety =  CBFLAG(maj);

	/*
	 * Save so that we can restore the q on failure.
	 */
	rinit = rq->q_qinfo;
	winit = WR(rq)->q_qinfo;
	ASSERT(rq->q_syncq->sq_type == (SQ_CI|SQ_CO));
	ASSERT((rq->q_flag & QMT_TYPEMASK) == QMTSAFE);
	ASSERT(rq->q_syncq == SQ(rq) && WR(rq)->q_syncq == SQ(rq));

	error = devflg_to_qflag(safety, &qflag, &sqtype);
	if (error) {
		cmn_err(CE_CONT,
			"clnopen: bad MT flags in cb_flag: 0x%x",
			safety & D_MTSAFETY_MASK);
		return (error);
	}
	/*
	 * Set the syncq state what qattach started off with. This is safe
	 * since no other thread can access this queue at this point
	 * (stream open, close, push, and pop are single threaded
	 * by the framework.)
	 */
	leavesq(rq->q_syncq, SQ_OPENCLOSE);
	unblockq(rq);

	/*
	 * Substitute the real qinit values for the current ones.
	 */
	/* setq might sleep in kmem_alloc - avoid holding locks. */
	setq(rq, stp->st_rdinit, stp->st_wrinit, stp, &perdev_syncq[maj],
		qflag, sqtype);

	/*
	 * Open the attached module or driver.
	 */
	if (rq->q_flag & QUNSAFE) {
		/*
		 * Do not do an entersq on the queue since
		 * that would prevent entry of the put and svc procedures
		 * during a sleep() in the open. Instead we do all what entersq
		 * does except setting SQ_EXCL.
		 */
		claimq(rq);
		mutex_enter(&unsafe_driver);
		insertq(STREAM(rq), rq, 1);
	} else {
		/*
		 * If there is an outer perimeter get exclusive access during
		 * the open procedure.
		 * Bump up the reference count on the queue.
		 */
		entersq(rq->q_syncq, SQ_OPENCLOSE);
		blockq(rq);
	}

	/*
	 * Call the device open with the stream flag CLONEOPEN.  The device
	 * will either fail this or return a minor device number (for old-
	 * style drivers) or the whole device number (for new-style drivers).
	 */
	if (rq->q_flag & QOLD) {
		int oldev;

		/*
		 * newdev is minor for pre-SVR4 drivers.
		 * old style drivers get the old device format
		 * so make sure it fits.
		 */
		if ((oldev = (o_dev_t)cmpdev(newdev)) == NODEV) {
			error =  ENXIO;
		} else if ((newdev = (*rq->q_qinfo->qi_qopen)(rq, oldev,
		    flag, CLONEOPEN)) == OPENFAIL) {
			lwp = ttolwp(curthread);
			error =  (lwp->lwp_error == 0 ? ENXIO : lwp->lwp_error);
		} else {

			/* return new style dev to caller */

			*devp = makedevice(emaj, (newdev & OMAXMIN));
		}
	} else {
		if (!(error = (*rq->q_qinfo->qi_qopen)
		    (rq, &newdev, flag, CLONEOPEN, crp))) {
			if ((getmajor(newdev) >= devcnt) ||
			    !(stp = STREAMSTAB(getmajor(newdev)))) {
				(*rq->q_qinfo->qi_qclose)
					(rq, flag, crp);
				error = ENXIO;
			} else {
				major_t m = getmajor(newdev);
				if (m != maj)  {
					(void) ddi_hold_installed_driver(m);
				}
				*devp = newdev;
			}
		}
	}
	if (error) {
		/*
		 * open failed; pretty up to look like original
		 * queue.
		 */
		if ((rq->q_flag & QUNSAFE)) {
			removeq(rq, 1);
			rq->q_next = NULL; WR(rq)->q_next = NULL;
			releaseq(rq);
			mutex_exit(&unsafe_driver);
		} else {
			/* XXX fix this in qattach as well! */
			if (backq(WR(rq)) && backq(WR(rq))->q_next == WR(rq))
				qprocsoff(rq);
			else
				unblockq(rq);
			leavesq(rq->q_syncq, SQ_OPENCLOSE);
		}
		rq->q_next = WR(rq)->q_next = NULL;
		flush_syncq(rq->q_syncq, rq);
		flush_syncq(WR(rq)->q_syncq, WR(rq));
		rq->q_ptr = WR(rq)->q_ptr = NULL;
		/* setq might sleep in kmem_alloc - avoid holding locks. */
		setq(rq, rinit, winit, NULL, NULL, QMTSAFE, SQ_CI|SQ_CO);

		/* Restore back to what qattach will expect */
		entersq(rq->q_syncq, SQ_OPENCLOSE);
		blockq(rq);

		ddi_rele_driver(maj);
	}
	return (error);
}
