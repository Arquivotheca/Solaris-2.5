/*
 * Copyright (c) 1989-1992, by Sun Microsystems, Inc.
 */
#pragma	ident	"@(#)is.c	1.24 94/11/25 SMI"

/*
 * IPI Channel driver for the Sun VME IPI-2 VME Controller
 */

/*
 * XXX: check for minimum requirements
 */
#include <sys/types.h>
#include <sys/devops.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/buf.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <sys/fcntl.h>
#include <sys/cmn_err.h>
#include <sys/stropts.h>
#include <sys/kmem.h>
#include <sys/sysmacros.h>

#include <sys/errno.h>
#include <sys/open.h>
#include <sys/varargs.h>
#include <sys/debug.h>
#include <sys/autoconf.h>
#include <sys/conf.h>

#include <sys/ipi_driver.h>
#include <sys/ipi_chan.h>
#include <sys/ipi3.h>
#include <sys/isdev.h>
#include <sys/isvar.h>

#include <sys/modctl.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/ddi_impldefs.h>

/*
 * Function prototypes
 */

static int
is_bus_ctl(dev_info_t *, dev_info_t *, ddi_ctl_enum_t, void *, void *);
static int is_identify(dev_info_t *);
static int is_probe(dev_info_t *);
static int is_attach(dev_info_t *, ddi_attach_cmd_t);

static int is_setup(ipi_addr_t, struct buf *, int ((*)()), caddr_t, ipiq_t **);
static void is_relse(ipiq_t *);
static void is_cmd(ipiq_t *);
static void is_control(ipi_addr_t, int, void *, int *);

static u_int isintr(caddr_t);
static u_int is_get_resp(is_ctlr_t *, respu_t *);
static u_int is_fault(is_ctlr_t *, u_long);

static void is_reset_slave(is_ctlr_t *);
static void is_rrc(caddr_t);
static void is_reset_complete(is_ctlr_t *);

static int is_create_commands(is_ctlr_t *);
static void is_dummy_handler(ipiq_t *);
static void is_timeout(caddr_t);

static void is_prf(is_ctlr_t *, int, int, const char *, ...);

/*
 * Local static data
 */
static is_ctlr_t *is_ctlr[IPI_NSLAVE];
static int nisc;
static u_int is_hz;
static int isdebug = 0;

static ipi_driver_t is_driver = {
	is_setup, is_relse, is_cmd, is_control
};

/*
 * autoconfiguration data
 */

static struct bus_ops is_bus_ops = {
	BUSO_REV,
	nullbusmap,
	0,
	0,
	0,
	i_ddi_map_fault,
	ddi_dma_map,
	ddi_dma_allochdl,
	ddi_dma_freehdl,
	ddi_dma_bindhdl,
	ddi_dma_unbindhdl,
	ddi_dma_flush,
	ddi_dma_win,
	ddi_dma_mctl,
	is_bus_ctl,
	ddi_bus_prop_op,
};


static struct dev_ops is_ops = {
	DEVO_REV,		/* devo_rev, */
	0,			/* refcnt  */
	ddi_no_info,
	is_identify,		/* identify */
	is_probe,		/* probe */
	is_attach,		/* attach */
	nodev,			/* detach */
	nodev,			/* reset */
	(struct cb_ops *)0,	/* driver operations */
	&is_bus_ops		/* bus operations */
};

static ddi_dma_lim_t is_lim = {
	/*
	 * Low Limit (0)
	 */
	(u_long) 0x0,

	/*
	 * High Limit (top of 32 bit range)
	 */
	(u_long) 0xffffffff,

	/*
	 * Max counter range.
	 */
	(u_int) 0xffffffff,

	/*
	 * Possible burst sizes
	 * Panther can do from 2 to 256 byte bursts
	 */
	(u_int) 0x1fe,

	/*
	 * Min cycle (16 bits)
	 */
	(u_int) 0x2,

	/*
	 * Max transfer speed (20 mb/s)
	 */
	20480
};


/*
 * This is the loadable module wrapper.
 */
extern struct mod_ops mod_driverops;

/*
 * Module linkage information for the kernel.
 */
/* from conf.c */
static struct modldrv modldrv = {
	&mod_driverops,  /* Type of module.  This one is a driver */
	"Panther VME IPI-2 Slave Controller",
	&is_ops,	/* driver ops */

};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modldrv, NULL
};

int
_init(void)
{
	return (mod_install(&modlinkage));
}

int
_info(modinfop)
	struct modinfo *modinfop;
{
	return (mod_info(&modlinkage, modinfop));
}

int
_fini(void)
{
	return (mod_remove(&modlinkage));
}

/*ARGSUSED*/
static int
is_bus_ctl(dev_info_t *d, dev_info_t *r, ddi_ctl_enum_t o, void *a, void *v)
{
	is_ctlr_t *c;
	struct regspec *child_regs;
	dev_info_t *child_dip;

	switch (o) {
	case DDI_CTLOPS_REPORTDEV:
	{
		u_int pnaddr;

		child_dip = (dev_info_t *)r;
		child_regs = ddi_rnumber_to_regspec(child_dip, 0);
		c = is_ctlr[(int)child_regs->regspec_bustype];
		if (!c)
			return (DDI_FAILURE);
		pnaddr = (u_int) sparc_pd_getreg(d,
		    child_regs->regspec_bustype)->regspec_addr;

		/*
		 * This is the reportdev for the ipi3sc node.  Since the
		 * existence of an ipi3sc node implies the existence of
		 * an acutual pn controller we use this opportunity to print
		 * out the actual controller information.  If the OBP for
		 * sun4m were done right there would be a pn node for each
		 * controller, but there is not so we do our best to make up
		 * for it here.
		 *
		 * NOTE: this code is VERY non-DDI compliant and is only
		 * here to make up for incorrect OBP information. DO NOT
		 * USE THIS CODE IN ANY OTHER NEXUS DRIVER.
		 */
		cmn_err(CE_CONT, "?%s%d at %s%d: IPI slave %d, vme32d32 "
		    "0x%X, VME level %d vector 0x%x\n",
		    ddi_get_name(r), ddi_get_instance(r),
		    ddi_get_name(d), ddi_get_instance(d),
		    child_regs->regspec_bustype, pnaddr,
		    c->is_intpri, c->is_vector);
		return (DDI_SUCCESS);
	}

	case DDI_CTLOPS_INITCHILD:
	{
		int error;

		child_dip = (dev_info_t *)a;
		/*
		 * Make the child's name unique on this controller.
		 */
		error = impl_ddi_sunbus_initchild(child_dip);
		if (error != DDI_SUCCESS)
			return (error);

		/*
		 * IPI "Slave" Address for an ipi3sc node sits
		 * in the bustype field for its registers.
		 */
		child_regs = ddi_rnumber_to_regspec(child_dip, 0);
		if ((int)child_regs->regspec_bustype >= nisc) {
			return (DDI_FAILURE);
		}

		/*
		 * make IPI Slave address info available to ipi3sc
		 */
		c = is_ctlr[(int)child_regs->regspec_bustype];
		if (!c)
			return (DDI_FAILURE);
		if (c->is_dip == NULL) {
			ddi_set_driver_private(child_dip, (caddr_t)-1);
		} else {
			ddi_set_driver_private(child_dip, (caddr_t)&c->is_cf);
		}
		return (DDI_SUCCESS);
	}

	case DDI_CTLOPS_UNINITCHILD:
	{
		dev_info_t *child_dip = (dev_info_t *)a;

		ddi_set_driver_private(child_dip, NULL);
		ddi_set_name_addr(child_dip, NULL);
		return (DDI_SUCCESS);
	}

	/*
	 * These ops correspond to functions that "shouldn't" be called
	 * by an IPI 'target' driver.  So we whinge when we're called.
	 */
	case DDI_CTLOPS_DMAPMAPC:
	case DDI_CTLOPS_REPORTINT:
	case DDI_CTLOPS_REGSIZE:
	case DDI_CTLOPS_NREGS:
	case DDI_CTLOPS_NINTRS:
	case DDI_CTLOPS_SIDDEV:
	case DDI_CTLOPS_SLAVEONLY:
	case DDI_CTLOPS_AFFINITY:
	case DDI_CTLOPS_IOMIN:
	case DDI_CTLOPS_POKE_INIT:
	case DDI_CTLOPS_POKE_FLUSH:
	case DDI_CTLOPS_POKE_FINI:
	case DDI_CTLOPS_INTR_HILEVEL:
	case DDI_CTLOPS_XLATE_INTRS:
		return (DDI_FAILURE);

	default:
		/*
		 * Pass this request up to my parent, I don't know how to deal
		 * with it.
		 */
		return (ddi_ctlops(d, r, o, a, v));
	}
}


/*
 * Autoconfiguration Routines
 */

static int
is_identify(dev_info_t *dev)
{
	char *name = ddi_get_name(dev);

	/*
	 * This module drives PN_NAME devices
	 */

	if (strcmp(name, PN_NAME) == 0 || strcmp(name, PN_NAME_ALT) == 0) {
		return (DDI_IDENTIFIED);
	} else {
		return (DDI_NOT_IDENTIFIED);
	}
}


/*
 * Determine how many, if any, Panther boards are present.
 */
static int
is_probe(dev_info_t *dev)
{
	caddr_t reg;
	int slave, nc, exists;
	long iv;
	is_reg_t *rp;

	/*
	 * We know how many slaves we will support
	 * simply from the number of register sets
	 * we have in our device node. It cannot
	 * be more than IPI_NSLAVE (one virtual
	 * IPI channel).
	 */

	if (ddi_dev_nregs(dev, &nc) != DDI_SUCCESS) {
		return (DDI_PROBE_FAILURE);
	}
	if (nc > IPI_NSLAVE) {
		nc = IPI_NSLAVE;
	}

	for (exists = slave = 0; slave < nc; slave++) {
		if (ddi_map_regs(dev, slave, &reg, 0, PN_REGSIZE)) {
			continue;
		}
		rp = (is_reg_t *)reg;
		/*
		 * Attempt to read board ID register.
		 */
		if (ddi_peekl(dev, (long *)&rp->dev_bir, &iv) == DDI_SUCCESS) {
			if (iv != PM_ID) {
				exists++;
			}
		}
		ddi_unmap_regs(dev, slave, &reg, (off_t)0, (off_t)0);
	}
	if (exists == 0) {
		return (DDI_PROBE_FAILURE);
	} else {
		return (DDI_PROBE_SUCCESS);
	}
}


/*
 * Attach IPI slave boards
 */
/* ARGSUSED */
static int
is_attach(dev_info_t *dev, ddi_attach_cmd_t cmd)
{

	register is_ctlr_t *c;
	register is_reg_t *rp;
	ddi_idevice_cookie_t id;
	ddi_iblock_cookie_t ib;
	caddr_t reg;
	u_int slave;
	int nattach;

	if (ddi_dev_nregs(dev, &nisc) != DDI_SUCCESS) {
		return (DDI_FAILURE);
	}
	if (nisc > IPI_NSLAVE) {
		nisc = IPI_NSLAVE;
	}

	for (slave = 0; slave < nisc; slave++) {
		is_ctlr[slave] = (is_ctlr_t *)
		    kmem_zalloc(sizeof (is_ctlr_t), KM_SLEEP);
	}
	is_hz = drv_usectohz(1000000);
	nattach = 0;

	/*
	 * Loop thru slaves, reprobing (it's simpler).
	 */

	for (slave = 0; slave < nisc; slave++) {
#ifndef	lint
		volatile u_long cresp;
#endif
		long iv;

		c = is_ctlr[slave];
		ib = (ddi_iblock_cookie_t *)0;
		reg = (caddr_t)0;

		if (ddi_add_intr(dev, slave, &ib, &id, isintr, (caddr_t)c)) {
			goto failure;
		}
		mutex_init(&c->is_lock, "pn", MUTEX_DRIVER, (void *) ib);
		mutex_init(&c->is_pool.pnp_lock, "pn_a",
		    MUTEX_DRIVER, (void *) ib);
		cv_init(&c->is_pool.pnp_cv, "pn_a", CV_DRIVER, (void *) ib);
		c->is_maxq = PN_MAX_CMDS;
		c->is_qact = 0;
		if (ddi_map_regs(dev, slave, &reg, 0, PN_REGSIZE)) {
			goto failure;
		}

		rp = c->is_reg = (is_reg_t *)reg;

		if (ddi_peekl(dev, (long *)&rp->dev_bir, &iv) != DDI_SUCCESS) {
			goto failure;
		}
		if (iv == PM_ID) {
			goto failure;
		}

		/*
		 * Wait for the reset which was (possibly)
		 * started by initial bootstrap to complete.
		 */

		CDELAY((rp->dev_csr & CSR_RESET) == 0, IS_INI_DELAY);
		if (rp->dev_csr & CSR_RESET) {
			is_prf(c, slave, CE_WARN, "board failed to reset");
			goto failure;
		}
#ifndef lint
		cresp = rp->dev_resp;		/* clear response reg valid */
#endif
		c->is_dip = dev;
		if (is_create_commands(c) == DDI_FAILURE) {
			goto failure;
		}
		c->is_id = id;
		c->is_cf.ic_addr = IPI_MAKE_ADDR(0, slave, IPI_NO_ADDR);
		c->is_cf.ic_lkinfo = (void *) ib;
		c->is_cf.ic_vector = is_driver;
		c->is_slvint = is_dummy_handler;
		c->is_facint = is_dummy_handler;
		c->is_flags = IS_PRESENT;	/* indicate probe worked */

		/*
		 * There is a race condition here where an asynchronous
		 * response packet can come in prior to any handler
		 * for it being set up.
		 */
		mutex_enter(&c->is_lock);
		c->is_reg->dev_vector = c->is_vector;
		c->is_reg->dev_csr |= CSR_EIRRV;
		(void) timeout(is_timeout, (caddr_t)c, is_hz);
		mutex_exit(&c->is_lock);

		/*
		 * We have at least 1 valid panther board (slave) attached.
		 */
		nattach++;
		continue;
failure:
		mutex_destroy(&c->is_lock);
		mutex_destroy(&c->is_pool.pnp_lock);
		cv_destroy(&c->is_pool.pnp_cv);
		if (ib) {
			ddi_remove_intr(dev, slave, ib);
		}
		if (reg) {
			ddi_unmap_regs(dev, slave, &reg, (off_t)0, (off_t)0);
		}
		is_ctlr[slave] = (is_ctlr_t *)NULL;
		kmem_free((caddr_t)c, sizeof (is_ctlr_t));
	}

	if (nattach == 0) {
		nisc = 0;
		return (DDI_FAILURE);
	} else {
		/*
		 * Normally we would do a ddi_report_dev(dev); here but the pn
		 * nexus has an inappropriate OBP node (regs for each ipi3sc).
		 * So here we just print something like "pn0 at vme0" and the
		 * REPORTDEV ctlop takes care of printing the correct register
		 * information for each ipi3sc that really exists.
		 */
		cmn_err(CE_CONT, "?%s%d at %s%d\n", ddi_get_name(dev),
		    ddi_get_instance(dev), ddi_get_name(ddi_get_parent(dev)),
		    ddi_get_instance(ddi_get_parent(dev)));
		return (DDI_SUCCESS);
	}
}


/*
 * IPI channel entry points
 */

/*
 * Allocate command packet and do DMA setup.
 */

static int
is_setup(ipi_addr_t addr, struct buf *bp, int (*cback)(),
	caddr_t cbarg, ipiq_t **rq)
{
	is_ctlr_t *c;
	ipiq_t *q;
	register pnpkt_t *pn;
	register pnpool_t *pnp;

	if (!rq)
		return (EINVAL);

	c = is_ctlr[IPI_SLAVE(addr)];
	if (!c || (c->is_flags & IS_PRESENT) == 0) {
		return (ENXIO);
	}
	pnp = &c->is_pool;

	if (cback == DDI_DMA_SLEEP) {
		mutex_enter(&pnp->pnp_lock);
		while ((pn = pnp->pnp_free) == (pnpkt_t *)0) {
			pnp->pnp_waiters++;
			cv_wait(&pnp->pnp_cv, &pnp->pnp_lock);
		}
	} else {
		mutex_enter(&pnp->pnp_lock);
		pn = pnp->pnp_free;
		if (pn == NULL) {
			if (cback != DDI_DMA_DONTWAIT) {
				/*
				 * XXX: Due to a bug in ddi_set_callback,
				 * XXX: we don't want to call it if we
				 * XXX: don't have to (system may panic).
				 * XXX: Therefore, only call ddi_set_callback
				 * XXX: if and only if pnp_clist is 0.
				 *
				 * NOTE NOTE NOTE NOTE NOTE NOTE NOTE
				 * This only works properly because we
				 * "know" that there really is only one
				 * callback function/argument pair at
				 * this time.
				 */
				if (pnp->pnp_clist == 0) {
					ddi_set_callback(cback, cbarg,
					    &pnp->pnp_clist);
				}
			}
			mutex_exit(&pnp->pnp_lock);
			return (-1);
		}
	}
	if ((c->is_flags & IS_PRESENT) == 0) {
		mutex_exit(&pnp->pnp_lock);
		return (ENXIO);
	}
	pnp->pnp_free = (pnpkt_t *)pn->pn_ipiq.q_next;
	q = &pn->pn_ipiq;
	if (q->q_result != IP_FREE) {
		mutex_exit(&pnp->pnp_lock);
		is_prf(c, IPI_SLAVE(addr), CE_PANIC, "corrupt ipiq free list");
		/* NOTREACHED */
	}
	q->q_result = IP_ALLOCATED;
	mutex_exit(&pnp->pnp_lock);

	q->q_next = NULL;
	q->q_addr = addr;
	q->q_resp = (struct ipi3resp *)0;
	q->q_flag = 0;
	q->q_retry = 0;
	q->q_time = 0;
	q->q_private[0] = q->q_private[1] = 0;
	q->q_cmd = (struct ipi3header *)
	    (&pnp->pnp_iopbbase[(pn - pnp->pnp_pool) * PN_CMD_SIZE]);

	if (bp == NULL) {
		q->q_tnp = (caddr_t)NULL;
		q->q_tnp_len = 0;
		pn->pn_dh = (ddi_dma_handle_t)NULL;
	} else {
		ddi_dma_cookie_t cookie;
		int flags, err;

		if (bp->b_flags & B_READ)
			flags = DDI_DMA_READ;
		else
			flags = DDI_DMA_WRITE;

		flags |= DDI_DMA_VME_USEA32;

		err = ddi_dma_buf_setup(c->is_dip, bp, flags,
		    cback, cbarg, &is_lim, &pn->pn_dh);

		switch (err) {
		case DDI_DMA_MAPPED:
			err = 0;
			break;

		case DDI_DMA_NORESOURCES:
			err = -1;
			break;

		case DDI_DMA_TOOBIG:
		case DDI_DMA_TOOSMALL:
		case DDI_DMA_NOMAPPING:
			err = EFAULT;
			break;

		default:
			err = EIO;
			break;
		}

		if (err) {
			is_relse(q);
			return (err);
		}

		if (ddi_dma_htoc(pn->pn_dh, 0, &cookie) != DDI_SUCCESS) {
			is_relse(q);	/* this should free the handle */
			return (EFAULT);
		}

		/*
		 * It would be nice to propagate VME address modifiers
		 * here, but since the panther board doesn't use anything
		 * but 32 bit supervisor mode accesses. There's no
		 * point in complaining.
		 */
		q->q_tnp_len = sizeof (caddr_t);
		pn->pn_daddr = (caddr_t)cookie.dmac_address;
		q->q_tnp = (caddr_t)&pn->pn_daddr;
	}
	*rq = q;
	return (0);
}

static void
is_relse(ipiq_t *q)
{
	register is_ctlr_t *c;
	register pnpkt_t *pn;
	register pnpool_t *pnp;

	/*
	 * Trap any frees of async response packets here
	 */
	if (q->q_result == IP_ASYNC) {
		return;
	}

	q->q_result = IP_FREE;
	pn = (pnpkt_t *)q;
	if (pn->pn_dh) {
		ddi_dma_free(pn->pn_dh);
		pn->pn_dh = (ddi_dma_handle_t)NULL;
	}
	c = is_ctlr[IPI_SLAVE(q->q_addr)];
	pnp = &c->is_pool;

	mutex_enter(&pnp->pnp_lock);
	q->q_next = (ipiq_t *)pnp->pnp_free;
	pnp->pnp_free = (pnpkt_t *)q;
	if (pnp->pnp_waiters) {
		pnp->pnp_waiters--;
		cv_signal(&c->is_pool.pnp_cv);
	} else if (pnp->pnp_clist != 0) {
		ddi_run_callback(&pnp->pnp_clist);
	}
	mutex_exit(&pnp->pnp_lock);
}

/*
 * Send a command to the IPI channel.
 */
static void
is_cmd(ipiq_t *q)
{
	volatile u_long status;
	register is_ctlr_t *c;
	register pnpkt_t *pn;
	register pnpool_t *pnp;
	int crit, slave;
	u_int ix;

	q->q_next = NULL;
	slave = IPI_SLAVE(q->q_addr);
	if (slave >= nisc || (c = is_ctlr[slave]) == NULL ||
	    c->is_dip == NULL) {
		is_prf(c, slave, CE_PANIC, "bad ipi address in ipiq (%x)",
		    q->q_addr);
		/* NOTREACHED */
	}

	pn = (pnpkt_t *)q;
	pnp = &c->is_pool;
	ix = pn - pnp->pnp_pool;

	/*
	 * XXX Debug
	 */
	if (ix >= PN_MAX_CMDS) {
		is_prf(c, slave, CE_PANIC, "bad pnpkt");
		/* NOTREACHED */
	}

	/*
	 * Assign a reference number for this transaction.
	 */
	if (pn->pn_refseq == 0)
		pn->pn_refseq++;
	pn->pn_lastref = (pn->pn_refseq++ << PN_CSHIFT) | ix;
	q->q_cmd->hdr_refnum = pn->pn_lastref;

	/*
	 * Make sure the address is correct
	 */
	q->q_cmd->hdr_slave = (u_char)slave;
	q->q_cmd->hdr_facility = IPI_FAC(q->q_addr);

	/*
	 * Reset the result code to something sane
	 */

	q->q_result = IP_ALLOCATED;

	/*
	 * Copy in the timeout value from the command packet.
	 * Make it infinitely large if it is zero. I call
	 * circa 4 billion seconds infinitely large.
	 */
	if ((pn->pn_time = q->q_time) == 0)
		pn->pn_time = (1 << ((NBBY * sizeof (long)) - 2));

	/*
	 * If you DMA sync the command here, you shouldn't have to
	 * ever do it again since nobody should touch this command
	 * again until it is processed by the panther board.
	 *
	 * It is a gross system failure to have this dma_sync fail.
	 */
	if (ddi_dma_sync(pnp->pnp_ih, (off_t)(ix * PN_CMD_SIZE),
	    PN_CMD_SIZE, DDI_DMA_SYNC_FORDEV) != DDI_SUCCESS) {
		is_prf(c, slave, CE_PANIC,
		    "ddi_dma_sync DDI_DMA_SYNC_FORDEV failed");
		/* NOTREACHED */
	}

	mutex_enter(&c->is_lock);

	/*
	 * If we have just started resetting the Panther,
	 * blow this command away. It's a harsh thing to do,
	 * but because state may have been lost by the reset
	 * and due to MP race conditions this command must
	 * have been sent prior to the reset occurring, the
	 * only sensible thing to do is to punt the command
	 * back to the caller for eventual disposition.
	 *
	 */

	if ((c->is_flags & IS_RESETTING) == IS_INRESET) {
		q->q_result = IP_RESET;
		mutex_exit(&c->is_lock);
		if (IPI_FAC(q->q_addr) == IPI_NO_ADDR)
			(*c->is_slvint)(q);
		else
			(*c->is_facint)(q);
		return;
	}

	/*
	 * Put the command onto the tail of the command queue,
	 * unless it is a priority command, in which case
	 * it goes at the front.
	 */

	if (q->q_flag & IP_PRIORITY_CMD) {
		q->q_next = c->is_qhead;
		c->is_qhead = q;
	} else {
		if (c->is_qhead) {
			c->is_qtail->q_next = q;
		} else {
			c->is_qhead = q;
		}
		c->is_qtail = q;
	}

	/*
	 * If we are in the middle of resetting, just leave
	 * with the command queued up to be rerun later.
	 */

	if (c->is_flags & IS_RESETTING) {
		mutex_exit(&c->is_lock);
		return;
	}

	/*
	 * If we have too many commands outstanding, we're done,
	 * unless this is a priority command, in which case we
	 * shove it down and hope for the best.
	 */

	q = c->is_qhead;
	if ((q->q_flag & IP_PRIORITY_CMD) == 0) {
		if (c->is_qact >= c->is_maxq) {
			mutex_exit(&c->is_lock);
			return;
		}
	}

	/*
	 * If the command register is busy, schedule an interrupt
	 * when it isn't. Set up a timer so that if we never get
	 * a EICRNB interrupt that we don't silently constipate
	 * and die.
	 *
	 * This is a critical section, so mark it so. Note that
	 * this doesn't obviate locking, but it should at least
	 * give us a fighting chance to be not preempted away
	 * somewhere else.
	 */
	crit = ddi_enter_critical();
	status = c->is_reg->dev_csr;

	if (status & CSR_CRBUSY) {
		if ((status & CSR_EICRNB) == 0) {
			c->is_reg->dev_csr |= CSR_EICRNB;
			c->is_eicrwatch = IS_EICRWATCH;
		}
	} else {
		/*
		 * If we were waiting for an EICRNB interrupt,
		 * disable the watchdog for it because the
		 * condition has been met (CSR_CRBUSY is false).
		 *
		 * Don't disable the interrupt, because we would
		 * like to take the interrupt anyhow in order
		 * to (possibly) shove another command.
		 */

		if (status & CSR_EICRNB) {
			c->is_eicrwatch = 0;
		}

		/*
		 * Ready to send a command. We do this by putting its
		 * DMA addressable address into the command register.
		 *
		 * We have to recalculate our offset because we may have
		 * loaded a different command than we were just called with.
		 * We don't have to do any other range checking because that
		 * was done already when we put this command onto the queue.
		 *
		 * Note that we are doing simple arithmetic from the DMA
		 * cookie we got originally from mapping in all iopbs.
		 */

		c->is_reg->dev_cmdreg = c->is_pool.pnp_ic.dmac_address +
		    ((((pnpkt_t *)q) - c->is_pool.pnp_pool) * PN_CMD_SIZE);

		c->is_qhead = q->q_next;	/* Advance the queue pointer */
		c->is_qact++;			/* note one more active cmd */
		q->q_result = IP_INPROGRESS;	/* set correct state */
	}
	ddi_exit_critical(crit);
	mutex_exit(&c->is_lock);
}

static void
is_control(ipi_addr_t addr, int request, void *arg, int *result_p)
{
	unsigned int maxq;
	struct icparg *icp;
	struct icprarg *icpr;
	is_ctlr_t *c = is_ctlr[IPI_SLAVE(addr)];
	int result = 0;

	switch (request) {
	case IPI_CTRL_RESET_SLAVE:

		is_reset_slave(c);
		break;

	case IPI_CTRL_REGISTER_IFUNC:

		mutex_enter(&c->is_lock);
		if (IPI_FAC(addr) == IPI_NO_ADDR)
			c->is_slvint = (void (*)()) arg;
		else
			c->is_facint = (void (*)()) arg;
		mutex_exit(&c->is_lock);
		break;

	case IPI_CTRL_LIMIT_SQ:
		maxq = (unsigned int) arg;
		if (maxq > PN_MAX_CMDS)
			maxq = PN_MAX_CMDS;
		mutex_enter(&c->is_lock);
		if (maxq)
			c->is_maxq = maxq;
		mutex_exit(&c->is_lock);
		break;

	case IPI_CTRL_NACTSLV:
		mutex_enter(&c->is_lock);
		result = (int)((unsigned int) c->is_qact);
		mutex_exit(&c->is_lock);
		break;

	case IPI_CTRL_DMASYNC:
	{
		pnpkt_t *pn = (pnpkt_t *)arg;
		if (pn && pn >= &c->is_pool.pnp_pool[0] &&
		    pn <= &c->is_pool.pnp_pool[PN_MAX_CMDS] &&
		    pn->pn_dh != (ddi_dma_handle_t)NULL) {
			ddi_dma_sync(pn->pn_dh, (off_t)0, (u_int) -1,
			    DDI_DMA_SYNC_FORCPU);
		}
		break;
	}
	case IPI_CTRL_PRINTCMD:
		icp = (struct icparg *)arg;
		ipi_print_cmd((struct ipi3header *)icp->arg, icp->msg);
		break;
	case IPI_CTRL_PRINTRESP:
		icp = (struct icparg *)arg;
		ipi_print_resp((struct ipi3resp *)icp->arg, icp->msg);
		break;
	case IPI_CTRL_PARSERESP:
		icpr = (struct icprarg *)arg;
		ipi_parse_resp(icpr->q, icpr->rt, icpr->a);
		break;
	default:
		result = -1;
	}
	if (result_p)
		*result_p = result;
}

/*
 * Interrupt handling routines
 */

static u_int
isintr(caddr_t iarg)
{
	volatile u_long status;
	register u_int ix;
	register ipiq_t *q, *rq;
	register pnpkt_t *pn;
	register is_ctlr_t *c;
	int slave, rval, crit;

	c = (is_ctlr_t *)iarg;
	slave = IPI_SLAVE(c->is_addr);
	rval = DDI_INTR_UNCLAIMED;
	q = (ipiq_t *)0;

	mutex_enter(&c->is_lock);
	status = c->is_reg->dev_csr;	/* read status register */

	if (status & CSR_RRVLID) {
		auto ipiq_t asresp;
		auto respu_t ru;
		struct ipi3resp *rp;
		u_int refnum;
		u_char result;

		result = IP_SUCCESS;
		rval = DDI_INTR_CLAIMED;
		rp = (struct ipi3resp *)0;

		if ((status & (CSR_ERROR | CSR_MRINFO)) == 0) {

			/*
			 * Successful command completion.
			 *
			 * The response register has the
			 * command reference number.
			 */
			refnum = c->is_reg->dev_resp;

		} else if (status & CSR_MRINFO) {

			/*
			 * Fetch response packet.
			 *
			 * The reason the test above doesn't check
			 * for CSR_ERROR is that the response packet
			 * may be an asynchronous response packet
			 * (i.e., not associated with a previous
			 * command).
			 */
			refnum = is_get_resp(c, &ru);

			if (refnum != (u_int) -1) {
				rp = &ru.hdr;
			}

		} else {
			refnum = is_fault(c, status);	/* handle ERROR */
			result = IP_ERROR;
		}

		/*
		 * At this point, we have a reference number and
		 * possibly a pointer to a response packet. If
		 * the reference number is (u_int) -1, then we
		 * had some error actually getting a real reference
		 * number. If the reference number is (u_int) -2,
		 * then we just have an asynchronous response
		 * packet to deal with.
		 */

		if (refnum == (u_int) -2) {

			q = &asresp;
			bzero((caddr_t)q, sizeof (ipiq_t));
			/*
			 * The slave field is bogus for async responses,
			 * so we generate it based upon which slave
			 * we are.
			 */
			rp->hdr_slave = (u_char)slave;
			q->q_addr =
			    IPI_MAKE_ADDR(0, rp->hdr_slave, rp->hdr_facility);
			q->q_cmd = (struct ipi3header *)0;
			q->q_resp = rp;
			q->q_flag = 0;
			q->q_time = 1;
			result = IP_ASYNC;

		} else if (refnum != (u_int) -1) {

			ix = refnum & (PN_MAX_CMDS - 1);

			pn = &c->is_pool.pnp_pool[ix];

			/*
			 * The first two cases are almost certainly
			 * due to a missing interrupt situation. In
			 * either case, drop this one on the floor.
			 */
			if (pn->pn_lastref != refnum) {
				if (isdebug) {
					is_prf(c, slave, CE_NOTE,
					    "refnum mismatch. Should be 0x%x "
					    "got 0x%x", pn->pn_lastref, refnum);
				}
			} else if (pn->pn_ipiq.q_result != IP_INPROGRESS) {
				if (isdebug) {
					is_prf(c, slave, CE_NOTE,
					    "command not in progress (%d)",
					    pn->pn_ipiq.q_result);
				}
			} else {
				q = &pn->pn_ipiq;
			}
		}

		if (q != (ipiq_t *)0 && q != &asresp && rp) {
			/*
			 * Save response into packet.
			 */
			bcopy((caddr_t)rp, pn->pn_resp,
			    rp->hdr_pktlen + sizeof (rp->hdr_pktlen));
			q->q_resp = (struct ipi3resp *)pn->pn_resp;
			/*
			 * If major status was simple success (0x18),
			 * set result accordingly. Otherwise, just
			 * complete (probably error).
			 */
			result =
			    (rp->hdr_maj_stat == (IP_MS_SUCCESS | IP_RT_CCR)) ?
			    IP_SUCCESS : IP_COMPLETE;
		}
		/*
		 * completion will be called after we drop
		 * the lock on the controller.
		 */
		if (q) {
			q->q_result = result;
			if (q != &asresp) {
				ASSERT(c->is_qact != 0);
				c->is_qact--;
			}
		}
	}

	/*
	 * See if there are any more commands queued that
	 * can now be sent. This is somewhat complicated.
	 * We want to be able to send a command if the
	 * command register is not busy and we're not
	 * over our command queue limit. We also may have
	 * gotten here because we enabled the 'interrupt
	 * on command register not busy' interrupt (in
	 * which case we need to make sure that we note
	 * this as a being a reason for having got the
	 * interrupt in the first place). We also may
	 * need to turn on the 'enable interrupt on
	 * nont busy' interrupt.
	 */

	rq = (ipiq_t *)NULL;
	crit = ddi_enter_critical();

	if (status & CSR_EICRNB) {

		/*
		 * We should only get this as a sole source
		 * of interrupt if and only if CSR_CRBUSY is
		 * not set. However, CRBUSY can come back at
		 * any time (say, because we stuffed in a cmd
		 * in is_cmd), so we'll just claim this interrupt
		 * unconditionally if CSR_EICRNB was set.
		 */

		rval = DDI_INTR_CLAIMED;

		/*
		 * If we have commands to go, and we can send
		 * them, turn off EICRNB and send a command.
		 * If we can't send one because we're over our
		 * queue limit, just turn off EICRNB. If we
		 * can't send one because the command register
		 * is busy, leave EICRNB enabled (leave the
		 * eicr watchdog count alone for this).
		 */

		if (c->is_qhead && c->is_qact < c->is_maxq) {
			if ((status & CSR_CRBUSY) == 0) {
				rq = c->is_qhead;
				c->is_reg->dev_csr &= ~CSR_EICRNB;
				c->is_eicrwatch = 0;
			}
		} else {
			/*
			 * This case is for no commands pending, or
			 * active commands at or above our queue limit.
			 */
			c->is_reg->dev_csr &= ~CSR_EICRNB;
			c->is_eicrwatch = 0;
		}
	} else {
		/*
		 * If we have commands to go, and we are less
		 * than our queue maxima, and the command
		 * register isn't busy, send a command. If
		 * the command register is busy, enable EICRNB
		 * and set a watchdog going on it.
		 */

		if (c->is_qhead && c->is_qact < c->is_maxq) {
			if ((status & CSR_CRBUSY) == 0) {
				rq = c->is_qhead;
			} else {
				c->is_reg->dev_csr |= CSR_EICRNB;
				c->is_eicrwatch = IS_EICRWATCH;
			}
		}
	}

	if (rq != NULL && (c->is_flags & IS_RESETTING) == 0) {
		c->is_reg->dev_cmdreg = c->is_pool.pnp_ic.dmac_address +
		    ((((pnpkt_t *)rq) - c->is_pool.pnp_pool) * PN_CMD_SIZE);
		c->is_qhead = rq->q_next;
		c->is_qact++;
		rq->q_result = IP_INPROGRESS;	/* set correct state */
	}
	ddi_exit_critical(crit);
	mutex_exit(&c->is_lock);

	if (q) {
		if (isdebug && q->q_time <= 0) {
			is_prf(c, slave, CE_CONT, "saved by the bell");
		}
		if (IPI_FAC(q->q_addr) == IPI_NO_ADDR)
			(*c->is_slvint)(q);
		else
			(*c->is_facint)(q);
	}
	return (rval);
}

/*
 * Get response.
 *
 * This rarely happens: robustness is more important than speed here.
 *
 * Return (u_int) -1 for failure, else the reference number for the
 * command this response packet is associated with. Asynchronous
 * responses will return as a a reference number of 0 (and be
 * sorted out above).
 */
static u_int
is_get_resp(is_ctlr_t *c, respu_t *resp)
{
	volatile u_long	response;
	u_long *lp, *rlp;
	int slave, resp_len;

	slave = IPI_SLAVE(c->is_addr);	/* controller number for messages */

	/*
	 * There is a response packet.  It must be read before the response
	 * register is read.  Get first word, containing refnum and length.
	 */
	rlp = (u_long *) &c->is_reg->dev_resp_pkt[0];
	lp = resp->l;
	*lp++ = *rlp++;
	resp_len = resp->hdr.hdr_pktlen + sizeof (resp->hdr.hdr_pktlen);

	/*
	 * Check for short (or no) response.
	 */
	if (resp_len < sizeof (struct ipi3resp)) {
		response = c->is_reg->dev_resp;		/* clear rupt */
		is_prf(c, slave, CE_WARN, "response too short- len %d min"
		    " %d response 0x%x", resp_len,
		    sizeof (struct ipi3resp), response);
		return ((u_int) -1);
	}

	/*
	 * Check for response too long.
	 */
	if (resp_len > sizeof (respu_t)) {
		int i;
		is_prf(c, slave, CE_WARN, "response too long (max %d) "
		    "truncating to len %d", sizeof (respu_t), resp_len);
		i = resp_len = sizeof (respu_t);
		resp->hdr.hdr_pktlen = resp_len - sizeof (u_short);
		while ((i -= sizeof (u_long)) > 0)
			*lp++ = *rlp++;
		ipi_print_resp(&resp->hdr, "truncated response");
		/* XXX Do we return error here? XXX */
	}

	/*
	 * Copy rest of response.  Use only word fetches.
	 * Response buffer should be word aligned.
	 */
	while ((resp_len -= sizeof (u_long)) > 0)
		*lp++ = *rlp++;

	/*
	 * Now that response packet has been read, it is safe to read the
	 * response register to clear the pending interrupt.  It should
	 * contain the command reference number (if not an asynchronous
	 * response packet, in which case we'll set that value to (u_int)
	 * -2.
	 */

	response = c->is_reg->dev_resp;
	if (IP_RTYP(resp->hdr.hdr_maj_stat) != IP_RT_ASYNC) {
		if (response != resp->hdr.hdr_refnum) {
			is_prf(c, slave, CE_WARN, "response register 0x%x not "
			    "same as response packet refnum 0x%x",
			    response, resp->hdr.hdr_refnum);
			/*
			 * XXX: Should we return an error?
			 */
			response = resp->hdr.hdr_refnum;
		}

	} else {
		response = (u_long) -2;
	}
	return ((u_int) response);
}

/*
 * Handle error from firmware.
 *
 * This type of error is indicated by status of RRVLID, ERROR, but not MRINFO.
 * The response register contains the value that was written into the command
 * register, not the reference number, since the error occurred in fetching
 * the command packet, the firmware doesn't know the reference number.
 *
 * Find the request and return it's pointer.
 */

static u_int
is_fault(is_ctlr_t *c, u_long status)
{
	char *msg, msg2[64];
	pnpool_t *pnp;
	int slave;
	u_long ioaddr;
	u_int refnum;

	slave = IPI_SLAVE(c->is_addr);	/* controller number for messages */

	/*
	 * First word in response is the DVMA address of the command packet.
	 */
	ioaddr = c->is_reg->dev_resp;

	switch ((status & CSR_FAULT) >> CSR_FAULT_SHIFT) {
	case CSR_FAULT_VME_B:
		msg = "bus error";
		break;
	case CSR_FAULT_VME_T:
		msg = "timeout";
		break;
	case CSR_INVAL_CMD:
		msg = "invalid command reg write";
		break;
	default:
		msg = "unknown fault code";
		break;
	}
	is_prf(c, slave, CE_WARN, "Fault Error Status 0x%x (%s) "
	    "response reg 0x%x", status, msg, ioaddr);

	/*
	 * Search for command packet with this I/O address.
	 */

	pnp = &c->is_pool;
	if (ioaddr < pnp->pnp_ic.dmac_address ||
	    ioaddr >=  pnp->pnp_ic.dmac_address + PN_IOPB_ALLOC_SIZE ||
	    (ioaddr & (PN_CMD_SIZE - 1))) {
		sprintf(msg2, "bad IOPB address returned (0x%x)", ioaddr);
		refnum = (u_int) -1;
	} else {
		pnpkt_t *pnpkt;
		refnum = (ioaddr - pnp->pnp_ic.dmac_address) / PN_CMD_SIZE;
		pnpkt = &pnp->pnp_pool[refnum];
		if (pnpkt->pn_ipiq.q_result != IP_INPROGRESS) {
			sprintf(msg2, "command not in progress");
			refnum = (u_int) -1;
		} else {
			refnum = pnpkt->pn_lastref;
			sprintf(msg2, "cmd refnum 0x%x for facility 0x%x",
			    refnum, IPI_FAC(pnpkt->pn_ipiq.q_addr));
		}
	}
	is_prf(c, slave, CE_WARN, "Fault Handler: %s", msg2);
	return (refnum);
}

/*
 * Reset slave.
 *	This resets the Panther controller board.
 */
static void
is_reset_slave(is_ctlr_t *c)
{
	pnpkt_t *pn;
	register ipiq_t *aq, *q;


	if (isdebug) {
		is_prf(c, IPI_SLAVE(c->is_addr), CE_CONT, "resetting slave");
	}

	mutex_enter(&c->is_lock);
	if (c->is_flags & IS_RESETTING) {
		mutex_exit(&c->is_lock);
		return;
	}

	c->is_reg->dev_csr = CSR_RESET;	/* reset controller board */
	c->is_flags |= IS_INRESET;

	/*
	 * Start things going for completion of reset.
	 */
	is_reset_complete(c);

	/*
	 * Clean up after ourselves.
	 */

	aq = c->is_qhead;
	c->is_qhead = NULL;
	c->is_qact = 0;
	c->is_eicrwatch = 0;

	for (q = aq; q != NULL; q = q->q_next) {
		q->q_result = IP_RESET;
	}
	pn = c->is_pool.pnp_pool;
	while (pn < &c->is_pool.pnp_pool[PN_MAX_CMDS]) {
		/*
		 * Nominally, the setting of IP_INPROGRESS
		 * is protected by is_lock.
		 */
		if (pn->pn_ipiq.q_result == IP_INPROGRESS) {
			pn->pn_ipiq.q_result = IP_RESET;
			pn->pn_ipiq.q_next = aq;
			aq = &pn->pn_ipiq;
		}
		pn++;
	}
	mutex_exit(&c->is_lock);

	while ((q = aq) != (ipiq_t *)0) {
		aq = q->q_next;
		if (IPI_FAC(q->q_addr) == IPI_NO_ADDR)
			(*c->is_slvint)(q);
		else
			(*c->is_facint)(q);
	}
	mutex_enter(&c->is_lock);
	c->is_flags |= IS_INRESET1;
	mutex_exit(&c->is_lock);
}

/*
 * Callout-driven routine to respond to reset slave request.
 */
static void
is_rrc(caddr_t arg)
{
	is_ctlr_t *c = (is_ctlr_t *)arg;
	mutex_enter(&c->is_lock);
	is_reset_complete(c);
	mutex_exit(&c->is_lock);
}

static void
is_reset_complete(register is_ctlr_t *c)
{
	register is_reg_t *rp;
	int w;
	ipiq_t *q;

	w = is_hz * IS_RESET_WAIT;

	if ((c->is_flags & IS_RESETWAIT) == 0) {
		/*
		 * Long delay to allow drives to (re)initialize.
		 */
		c->is_flags |= IS_RESETWAIT;
		(void) timeout(is_rrc, (caddr_t)c, w);
		return;
	}

	rp = c->is_reg;
	if (rp->dev_csr & CSR_RESET) {
		if ((c->is_flags & IS_RESETWAIT1) == 0) {
			c->is_flags |= IS_RESETWAIT1;
			(void) timeout(is_rrc, (caddr_t)c, w);
			return;
		}
		is_prf(c, IPI_SLAVE(c->is_addr), CE_WARN, "stuck reset bit");
		c->is_flags &= ~IS_PRESENT;
		rp->dev_vector = 0;
		rp->dev_csr = 0;
	} else {
		rp->dev_vector = c->is_vector;
		rp->dev_csr |= CSR_EIRRV;	/* enable rupt on response */
	}
	c->is_flags &= ~IS_RESETTING;
	q = c->is_qhead;

	if (isdebug) {
		is_prf(c, IPI_SLAVE(c->is_addr), CE_CONT,
		    "reset complete, %s", (q) ? "starting queued commands" :
		    "no commands to start");
	}

	if (q == (ipiq_t *)NULL)
		return;

	if (rp->dev_csr & CSR_CRBUSY) {
		if ((rp->dev_csr & CSR_EICRNB) == 0) {
			rp->dev_csr |= CSR_EICRNB;
			c->is_eicrwatch = IS_EICRWATCH;
		}
	} else {
		/*
		 * Ready to send a command. We do this by putting its
		 * DMA addressable address into the command register.
		 *
		 * We have to recalculate our offset because we may have
		 * loaded a different command than we were just called with.
		 * We don't have to do any other range checking because that
		 * was done already when we put this command onto the queue.
		 *
		 * Note that we are doing simple arithmetic from the DMA
		 * cookie we got originally from mapping in all iopbs.
		 */

		rp->dev_cmdreg = c->is_pool.pnp_ic.dmac_address +
		    ((((pnpkt_t *)q) - c->is_pool.pnp_pool) * PN_CMD_SIZE);

		c->is_qhead = q->q_next;	/* Advance the queue pointer */
		c->is_qact++;			/* note one more active cmd */
		q->q_result = IP_INPROGRESS;	/* set correct state */
		q->q_next = NULL;		/* safety */
	}
}

/*
 * miscellaneous subroutines
 */

static int
is_create_commands(is_ctlr_t *c)
{
	ddi_dma_req_t dq;
	register pnpool_t *pnp;
	int amt, i;

	pnp = &c->is_pool;
	amt = PN_IOPB_ALLOC_SIZE;

	if (ddi_iopb_alloc(c->is_dip, &is_lim, amt, &pnp->pnp_iopbbase)) {
		/*
		 * XXX: Optimization: store 'amt' in pnpool, and keep
		 * XXX: decrementing until we can alloc.
		 */
		return (DDI_FAILURE);
	}

	dq.dmar_limits = &is_lim;
	dq.dmar_flags = DDI_DMA_RDWR|DDI_DMA_CONSISTENT|DDI_DMA_VME_USEA32;
	dq.dmar_fp = DDI_DMA_SLEEP;
	dq.dmar_arg = NULL;
	dq.dmar_object.dmao_size = amt;
	dq.dmar_object.dmao_type = DMA_OTYP_VADDR;
	dq.dmar_object.dmao_obj.virt_obj.v_addr = pnp->pnp_iopbbase;
	dq.dmar_object.dmao_obj.virt_obj.v_as = NULL;

	if (ddi_dma_setup(c->is_dip, &dq, &pnp->pnp_ih) != DDI_SUCCESS) {
		ddi_iopb_free(pnp->pnp_iopbbase);
		return (DDI_FAILURE);
	}
	if (ddi_dma_htoc(pnp->pnp_ih, 0, &pnp->pnp_ic) != DDI_SUCCESS) {
		ddi_dma_free(pnp->pnp_ih);
		ddi_iopb_free(pnp->pnp_iopbbase);
		return (DDI_FAILURE);
	}
	bzero(pnp->pnp_iopbbase, amt);
	/*
	 * Now convert this to number of commands
	 */
	amt /= PN_CMD_SIZE;
	for (i = 0; i < amt; i++) {
		pnp->pnp_pool[i].pn_ipiq.q_next =
		    (ipiq_t *)&pnp->pnp_pool[i+1];
	}
	pnp->pnp_pool[i-1].pn_ipiq.q_next = (ipiq_t *)0;
	pnp->pnp_free = pnp->pnp_pool;
	return (DDI_SUCCESS);
}

static void
is_dummy_handler(ipiq_t *q)
{
	cmn_err(CE_WARN, "pn, slave %d: no %s interrupt handler registered",
	    IPI_SLAVE(q), (IPI_FAC(q->q_addr) == IPI_NO_ADDR)?
	    "slave" : "facility");
}


/*
 * Timeout handler.
 *
 * Our purpose in life is to find any commands that have died of old age,
 * as well as watch out for dead EICRNB interrupts.
 */

static void
is_timeout(caddr_t arg)
{
	is_ctlr_t *c;
	ipiq_t *q;
	pnpkt_t *pn;
	int flags, action;

	c = (is_ctlr_t *)arg;

	if (mutex_tryenter(&c->is_lock) == 0) {
		(void) timeout(is_timeout, (caddr_t)c, is_hz);
		return;
	}

	action = 0;
	q = NULL;
	flags = c->is_flags;

	/*
	 * If we aren't resetting, walk through our command pool
	 * to check find a command that has died of old age. We
	 * give it a last chance to complete before deciding that
	 * this is a truly missing interrupt. In either case (if
	 * we find one), we call isintr (after dropping our lock)
	 * just in case it is sitting there already complete.
	 *
	 * If we don't find a dead command and we aren't resetting,
	 * we also check for a lost EICRNB interrupt.
	 */

	if ((flags & (IS_PRESENT|IS_RESETTING)) == IS_PRESENT) {
		pn = c->is_pool.pnp_pool;
		while (pn < &c->is_pool.pnp_pool[PN_MAX_CMDS]) {
			if (pn->pn_ipiq.q_result == IP_INPROGRESS) {
				if (pn->pn_time > 0) {
					pn->pn_time -= 1;
				} else {
					break;
				}
			}
			pn++;
		}

		/*
		 * If we've found a potential dead command, deal
		 * with it. Otherwise, check for a lost EICRNB
		 * interrupt if we were expecting one.
		 */

		if (pn != &c->is_pool.pnp_pool[PN_MAX_CMDS]) {
			action = 1;
			/*
			 * If the time count for this command
			 * has dropped to zero, we don't do
			 * anything (yet). If it has dropped to
			 * less than zero, we blow it away.
			 */
			if (pn->pn_time == 0) {
				pn->pn_time = -1;
			} else {
				ASSERT(c->is_qact != 0);
				c->is_qact--;
				q = (ipiq_t *)pn;
				q->q_result = IP_MISSING;
			}
		} else if (c->is_eicrwatch != 0) {
			if (--c->is_eicrwatch == 0) {
				action = 2;
			}
		}
	}
	mutex_exit(&c->is_lock);

	if (q) {
		if (IPI_FAC(q->q_addr) == IPI_NO_ADDR)
			(*c->is_slvint)(q);
		else
			(*c->is_facint)(q);
	}

	if (flags & IS_PRESENT) {
		/*
		 * action will zero if IS_RESETTING was set
		 */
		if (action == 1) {
			(void) isintr((caddr_t)c);
		} else if (action == 2) {
			is_reset_slave(c);
		}
		(void) timeout(is_timeout, (caddr_t)c, is_hz);
	}
}

static void
is_prf(is_ctlr_t *c, int slave, int level, const char *fmt, ...)
{
	extern char *vsprintf(char *, const char *, va_list);
	char lbuf[128], *mfmt, inst;
	va_list ap;

	va_start(ap, fmt);
	(void) vsprintf(lbuf, fmt, ap);
	va_end(ap);
	if (level == CE_CONT)
		mfmt = "?pn%c, slave %d: %s\n";
	else
		mfmt = "pn%c, slave %d: %s";
	if (!c || !c->is_dip)
		inst = '?';
	else
		inst = (char)ddi_get_instance(c->is_dip) + '0';
	cmn_err(level, mfmt, inst, slave, lbuf);
}
