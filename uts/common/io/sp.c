/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)sp.c	1.19	94/03/10 SMI"	/* from SVR4 1.1	*/

/*
 * SP - Stream "pipe" device.  Any two minor devices may
 * be opened and connected to each other so that each user
 * is at the end of a single stream.  This provides a full
 * duplex communications path and allows for the passing
 * of file descriptors as well.
 *
 * WARNING - an interprocess stream does not have the same
 *		semantics as a pipe, and this does not replace
 *		pipes.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/errno.h>
#include <sys/stropts.h>
#include <sys/stream.h>
#include <sys/stat.h>

#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>

int spopen(), spclose(), spput();
static struct module_info spm_info = {1111, "sp", 0, INFPSZ, 5120, 1024 };
static struct qinit sprinit =
		    { NULL, putnext, spopen, spclose, NULL, &spm_info, NULL };
static struct qinit spwinit = {
    spput, putnext, NULL, NULL, NULL, &spm_info, NULL
};
struct streamtab spinfo = { &sprinit, &spwinit, NULL, NULL };

static int sp_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg,
		void **result);
static int sp_identify(dev_info_t *devi);
static int sp_attach(dev_info_t *devi, ddi_attach_cmd_t cmd);
static int sp_detach(dev_info_t *devi, ddi_detach_cmd_t cmd);
static dev_info_t *sp_dip;		/* private copy of devinfo pointer */

#define	SP_CONF_FLAG	(D_NEW)
	DDI_DEFINE_STREAM_OPS(sp_ops, sp_identify, nulldev,	\
		sp_attach, sp_detach, nodev,			\
		sp_info, SP_CONF_FLAG, &spinfo);

extern struct sp {
	queue_t *sp_rdq;		/* this stream's read queue */
	queue_t *sp_ordq;		/* other stream's read queue */
		} sp_sp[];
extern spcnt;

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/modctl.h>


extern nodev(), nulldev();
extern dseekneg_flag;
extern struct mod_ops mod_driverops;
extern struct dev_ops sp_ops;

/*
 * Module linkage information for the kernel.
 */

static struct modldrv modldrv = {
	&mod_driverops, /* Type of module.  This one is a pseudo driver */
	"Streams Pipe device 'sp'",
	&sp_ops,	/* driver ops */
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
sp_identify(dev_info_t *devi)
{
	if (strcmp(ddi_get_name(devi), "sp") == 0)
		return (DDI_IDENTIFIED);
	else
		return (DDI_NOT_IDENTIFIED);
}

static int
sp_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	if (cmd != DDI_ATTACH)
		return (DDI_FAILURE);

	if (ddi_create_minor_node(devi, "sp", S_IFCHR,
	    0, NULL, CLONE_DEV) == DDI_FAILURE) {
		ddi_remove_minor_node(devi, NULL);
		return (DDI_FAILURE);
	}
	sp_dip = devi;
	return (DDI_SUCCESS);
}

static int
sp_detach(dev_info_t *devi, ddi_detach_cmd_t cmd)
{
	if (cmd != DDI_DETACH)
		return (DDI_FAILURE);

	ddi_remove_minor_node(devi, NULL);
	return (DDI_SUCCESS);
}

/* ARGSUSED */
static int
sp_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg,
	void **result)
{
	register dev_t dev = (dev_t) arg;
	register int error;

	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		if (sp_dip == NULL) {
			error = DDI_FAILURE;
		} else {
			*result = (void *) sp_dip;
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


spopen(q, devp, flag, sflag, crp)
register queue_t *q;
dev_t		 *devp;
int		 flag, sflag;
cred_t		 *crp;
{
	register struct sp *spp;
	register dev_t	dev;

	dev = getminor(*devp);
	switch (sflag) {
	case MODOPEN:
		dev = (struct sp *)q->q_ptr - sp_sp;
		break;

	case CLONEOPEN:
		for (dev = 0, spp = sp_sp; ((dev < spcnt) && spp->sp_rdq);
							    dev++, spp++);
		break;
	}
	if (dev >= spcnt)
		return (ENODEV);
	spp = &sp_sp[dev];
	if (! spp->sp_rdq) {
		spp->sp_rdq = q;
		q->q_ptr = WR(q)->q_ptr = (caddr_t)spp;
	}

	/*
	 * Return a new device number, same major number, new minor number.
	 */
	if (sflag == CLONEOPEN)
		*devp = makedevice(getmajor(*devp), dev);

	return (0);
}

spclose(q, flag, crp)
register queue_t *q;
int		 flag;
cred_t		 *crp;
{
	register struct sp *spp, *osp;
	register queue_t *orq;
	register mblk_t *mp;

	spp = (struct sp *)q->q_ptr;
	spp->sp_rdq = NULL;
	if (orq = spp->sp_ordq) {
		osp = (struct sp *)orq->q_ptr;
		osp->sp_ordq = NULL;
		spp->sp_ordq = NULL;
		WR(orq)->q_next = NULL;
		WR(q)->q_next = NULL;
		if (mp = allocb(0, BPRI_MED)) {
			mp->b_datap->db_type = M_HANGUP;
			putnext(orq, mp);
		} else
			printf("SP: WARNING- close could not allocate block\n");
	}
	q->q_ptr = WR(q)->q_ptr = NULL;

	return (0);
}

spput(q, bp)
register queue_t *q;
register mblk_t *bp;
{
	register btype;

	switch (btype = bp->b_datap->db_type) {

	case M_IOCTL:
		bp->b_datap->db_type = M_IOCNAK;
		qreply(q, bp);
		return;

	case M_FLUSH:
		/*
		 * The meaning of read and write sides must be reversed
		 * for the destination stream head.
		 */
		if (!q->q_next) {
			*bp->b_rptr &= ~FLUSHW;
			if (*bp->b_rptr & FLUSHR) qreply(q, bp);
			return;
		}
		switch (*bp->b_rptr) {
		case FLUSHR: *bp->b_rptr = FLUSHW; break;
		case FLUSHW: *bp->b_rptr = FLUSHR; break;
		}
		putnext(q, bp);
		return;

	default:
		if (q->q_next) {
			putnext(q, bp);
			return;
		} else if (btype == M_PROTO) {

			register queue_t *oq;
			register struct sp *spp, *osp;
			register i;

			if (bp->b_cont)
				goto errout;
			if ((bp->b_wptr - bp->b_rptr) != sizeof (queue_t *))
				goto errout;
			oq = *((queue_t **)bp->b_rptr);
			for (i = 0, osp = sp_sp;
			    ((i < spcnt) && (oq != osp->sp_rdq)); i++, osp++);
			if (i == spcnt) goto errout;
			if (osp->sp_ordq) goto errout;

			spp = (struct sp *)q->q_ptr;
			spp->sp_ordq = oq;
			osp->sp_ordq = RD(q);
			WR(oq)->q_next = RD(q)->q_next;
			q->q_next = oq->q_next;
			freemsg(bp);
			return;
		}
		break;
	}

errout:
	/*
	 * The stream has not been connected yet.
	 */
	bp->b_datap->db_type = M_ERROR;
	bp->b_wptr = bp->b_rptr = bp->b_datap->db_base;
	*bp->b_wptr++ = EIO;
	qreply(q, bp);
}
