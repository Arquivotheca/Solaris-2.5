/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)pipemod.c	1.11	94/09/29 SMI"	/* from S5R4 1.2	*/

/*
 * This module switches the read and write flush bits for each
 * M_FLUSH control message it receives. It's intended usage is to
 * properly flush a STREAMS-based pipe.
 */
#include "sys/types.h"
#include "sys/sysmacros.h"
#include "sys/param.h"
#include "sys/systm.h"
#include "sys/errno.h"
#include "sys/signal.h"
#ifdef u3b2
#include "sys/sbd.h"
#endif	/* u3b2 */
#include "sys/pcb.h"
#include "sys/user.h"
#include "sys/fstyp.h"
#include "sys/stream.h"
#include "sys/stropts.h"
#include "sys/vnode.h"
#include "sys/file.h"

/*
 * This is the loadable module wrapper.
 */
#include <sys/conf.h>
#include <sys/modctl.h>

extern struct streamtab pipeinfo;

static struct fmodsw fsw = {
	"pipemod",
	&pipeinfo,
	D_NEW
};


/*
 * Module linkage information for the kernel.
 */
extern struct mod_ops mod_strmodops;

static struct modlstrmod modlstrmod = {
	&mod_strmodops, "pipe flushing module", &fsw
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


_info(modinfop)
struct modinfo *modinfop;
{
	return (mod_info(&modlinkage, modinfop));
}


int pipeopen(), pipeclose(), pipeput();

static struct module_info pipe_info = {
	1003,
	"pipemod",
	0,
	INFPSZ,
	STRHIGH,
	STRLOW };
static struct qinit piperinit = {
	pipeput,
	NULL,
	pipeopen,
	pipeclose,
	NULL,
	&pipe_info,
	NULL };
static struct qinit pipewinit = {
	pipeput,
	NULL,
	NULL,
	NULL,
	NULL,
	&pipe_info,
	NULL};
struct streamtab pipeinfo = { &piperinit, &pipewinit, NULL, NULL };

/*ARGSUSED*/
int
pipeopen(rqp, devp, flag, sflag, crp)
queue_t *rqp;
dev_t *devp;
int flag;
int sflag;
cred_t *crp;
{
	return (0);
}

/*ARGSUSED*/
int
pipeclose(q, cflag, crp)
queue_t *q;
int cflag;
cred_t *crp;
{
	return (0);
}

/*
 * Use same put procedure for write and read queues.
 * If mp is an M_FLUSH message, switch the FLUSHW to FLUSHR and
 * the FLUSHR to FLUSHW and send the message on.  If mp is not an
 * M_FLUSH message, send it on with out processing.
 */
int
pipeput(qp, mp)
queue_t *qp;
mblk_t *mp;
{
	switch (mp->b_datap->db_type) {
		case M_FLUSH:
			if (!(*mp->b_rptr & FLUSHR && *mp->b_rptr & FLUSHW)) {
				if (*mp->b_rptr & FLUSHW) {
					*mp->b_rptr |= FLUSHR;
					*mp->b_rptr &= ~FLUSHW;
				} else {
					*mp->b_rptr |= FLUSHW;
					*mp->b_rptr &= ~FLUSHR;
				}
			}
			break;

		default:
			break;
	}
	putnext(qp, mp);
	return (0);
}
