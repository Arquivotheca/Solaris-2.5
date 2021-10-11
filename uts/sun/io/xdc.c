/*
 * Copyright (c) 1987-1993 by Sun Microsystems, Inc.
 */

#pragma	ident	"@(#)xdc.c	1.15	94/02/16 SMI"	/* SunOS 4.1.1 1.52 */

/*
 * Driver for Xylogics 7053 SMD disk controllers
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/file.h>
#include <sys/uio.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/kmem.h>
#include <sys/stat.h>

#include <sys/errno.h>
#include <sys/open.h>
#include <sys/varargs.h>

#include <sys/hdio.h>
#include <sys/dkio.h>
#include <sys/dkbad.h>
#include <sys/vtoc.h>

#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/kstat.h>

#include <sys/xdvar.h>
#include <sys/xderr.h>

/*
 * Local defines
 */
#define	KIOSP		KSTAT_IO_PTR(un->un_iostats)
#define	KIOIP		KSTAT_INTR_PTR(c->c_intrstats)

/*
 * Function Prototypes
 */

static int
xdc_bus_ctl(dev_info_t *d, dev_info_t *r, ddi_ctl_enum_t o, void *a, void *v);
static int xdcidentify(dev_info_t *);
static int xdcprobe(dev_info_t *);
static int xdcattach(dev_info_t *, ddi_attach_cmd_t);
static int xdcstart(caddr_t);

static int xdccmd(struct xdcmdblock *, u_short, int,
	ddi_dma_handle_t, int, daddr_t, int, int, int);

static void xdcsynch(struct xdcmdblock *);
static void xdcasynch(struct xdcmdblock *);
static void xdcexec(struct xdcmdblock *);
static void xdcpushiopb(struct xdiopb *, struct xdctlr *);

static int xdcrecover(struct xdcmdblock *, void (*)());

static struct xderror *finderr(u_char);
static void printerr(struct xdunit *, u_char, short, char *, char *, daddr_t);

static u_int xdcintr(caddr_t);
static void xdcsvc(struct xdctlr *, struct xdcmdblock *);

static int xdccreatecbis(struct xdctlr *, int);
static struct xdcmdblock *xdcgetcbi(struct xdctlr *, int, int);
static void xdcputcbi(struct xdcmdblock *);
static void initiopb(struct xdiopb *, u_int);
static void cleariopb(struct xdiopb *);

static int isbad(register struct dkbad *, int, int, int);
static struct xdunit *xdcunit(register struct xdunit **up, u_int instance);

/*
 * Settable error level.
 */
static short xderrlvl = EL_FIXED | EL_RETRY | EL_RESTR | EL_RESET | EL_FAIL;

#define	XDDEBUG
#if	defined(XDDEBUG) || defined(lint)
static int xddebug = 0;
#else
#define	xddebug	0
#endif

/*
 * List of commands for the 7053.  Used to print nice error messages.
 */
#define	XDCMDS	(sizeof (xdcmdnames) / sizeof (xdcmdnames[0]))
static char *xdcmdnames[] = {
	"nop",
	"write",
	"read",
	"seek",
	"drive reset",
	"write parameters",
	"read parameters",
	"write extended",
	"read extended",
	"diagnostics",
};
static int xdthrottle = XD_THROTTLE;	/* transfer burst count */

/*
 * Defines for setting ctlr, drive and format parameters.
 */
#define	XD_CTLRPAR32	0x6050
#define	XD_CTLRPARAM	0x9600
#define	XD_DRPARAM7053	0x00
#define	XD_INTERLEAVE	0x00
#define	XD_FORMPAR2	((XD_FIELD1 << 8) | XD_FIELD2)
#define	XD_FORMPAR3	((((XD_FIELD3 << 8) | XD_FIELD4) * un->un_g.dkg_nhead \
			+ (XD_FIELD5 >> 8)) * un->un_g.dkg_nsect + \
			(XD_FIELD5 & 0xff))
#define	XD_FORMPAR4	((XD_FIELD6 << 24) | (XD_FIELD7 << 16) | XD_FIELD5ALT)

/*
 * Defines for specific format parameters fields.
 */
#define	XD_FIELD1	0x01		/* sector pulse to read gate */
#define	XD_FIELD2	0x0a		/* read gate to header sync */
#define	XD_FIELD3	0x1b		/* sector pulse to header sync */
#define	XD_FIELD4	0x14		/* header ecc to data sync */
#define	XD_FIELD5	0x200		/* sector size */
#define	XD_FIELD6	0x0a		/* header ecc to read gate */
#define	XD_FIELD7	0x03		/* data ecc to write gate */
#define	XD_FIELD5ALT	0x200		/* alternate sector size */

#define	roundup(x, y)	((((x)+((y)-1))/(y))*(y))

/*
 * These shorts define the controller parameters the 7053 will get set with.
 *
 * xd_ctlrpar0 is a short containing the bits for iopb bytes 0x8 and 0x9
 * (which is the sector count field under normal operations)
 *
 * xd_ctlrpar1 is a short containing the bits for iopb byte 0xa. The driver
 * will shift this by 8, and or it into the xd_throttle field of the iopb
 * (also the cylinder field).
 *
 * see the file xdreg.h for the definitions of these bits.
 */

static u_short xd_ctlrpar0 = XD_LWRD|XD_DACF|(xd_tdtv(1))|XD_ROR;

/*
 * This *must* be a short, as it will be shifted by 8 to be folded into the
 * iopb throttle variable.
 */

static u_short xd_ctlrpar1 = XD_OVS|XD_ASR|XD_RBC|XD_ECC2;

/*
 * Autoconfiguration data
 */

static void *xdc_state;

static ddi_dma_lim_t xdc_lim = {
	0x0, (u_int) 0xffffffff, (u_int) 0xffffffff, 0x6, 0x2, 4096
};

static struct bus_ops xdc_bus_ops = {
	nullbusmap,
	0,		/* ddi_intrspec_t	(*bus_get_intrspec)(); */
	0,		/* int			(*bus_add_intrspec)(); */
	0,		/* void			(*bus_remove_intrspec)(); */
	0,		/* int			(*bus_map_fault)() */
	0,		/* int			(*bus_dma_map)() */
	0,		/* int			(*bus_dma_ctl)() */
	xdc_bus_ctl,
	ddi_bus_prop_op,
};

static struct dev_ops xd_ops = {
	DEVO_REV,		/* devo_rev, */
	0,			/* refcnt  */
	ddi_no_info,		/* info */
	xdcidentify,		/* identify */
	xdcprobe,		/* probe */
	xdcattach,		/* attach */
	nodev,			/* detach */
	nodev,			/* reset */
	(struct cb_ops *)0,	/* driver operations */
	&xdc_bus_ops,		/* bus operations */
};

/*
 * This is the loadable module wrapper.
 */
#include <sys/modctl.h>

extern struct mod_ops mod_driverops;

/*
 * Module linkage information for the kernel.
 */
static struct modldrv modldrv = {
	&mod_driverops,	/* Type of module.  This one is a driver */
	"Xylogics 7053 SMD Disk Controller",
	&xd_ops,	/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modldrv, NULL
};

/*
 * This is the module initialization routine.
 */
int
_init(void)
{
	register int e;

	if ((e = ddi_soft_state_init(&xdc_state,
	    sizeof (struct xdctlr), 1)) != 0) {
		return (e);
	}

	if ((e = mod_install(&modlinkage)) != 0) {
		ddi_soft_state_fini(&xdc_state);
	}
	return (e);
}

int
_fini(void)
{
	register int e;

	if ((e = mod_remove(&modlinkage)) != 0) {
		return (e);
	}
	ddi_soft_state_fini(&xdc_state);
	return (0);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

/*ARGSUSED*/
static int
xdc_bus_ctl(dev_info_t *d, dev_info_t *r, ddi_ctl_enum_t o, void *a, void *v)
{
	register dev_info_t *cdev;
	char name[MAXNAMELEN];
	int s_len, slave;

	switch (o)  {
	case DDI_CTLOPS_INITCHILD:

		cdev = (dev_info_t *)a;
		s_len = sizeof (slave);
		if (ddi_prop_op(DDI_DEV_T_NONE, cdev, PROP_LEN_AND_VAL_BUF,
		    DDI_PROP_DONTPASS | DDI_PROP_CANSLEEP, "slave",
		    (caddr_t)&slave, &s_len) != DDI_SUCCESS || slave > XDUNPERC)
			return (DDI_NOT_WELL_FORMED);

		sprintf(name, "%d,0", slave);
		ddi_set_name_addr(cdev, name);
		return (DDI_SUCCESS);

	case DDI_CTLOPS_UNINITCHILD:

		cdev = (dev_info_t *)a;
		ddi_set_name_addr(cdev, NULL);
		return (DDI_SUCCESS);

	case DDI_CTLOPS_REPORTDEV:

		slave = ddi_getprop(DDI_DEV_T_NONE, r, DDI_PROP_DONTPASS,
		    "slave", -1);
		cmn_err(CE_CONT, "?%s%d at xdc%d: slave %d\n",
		    ddi_get_name(r), ddi_get_instance(r),
		    ddi_get_instance(d), slave);
		return (DDI_SUCCESS);

	default:
		return (ddi_ctlops(d, r, o, a, v));
	}
}

/*
 * Autoconfiguration Routines
 */

static int
xdcidentify(dev_info_t *dev)
{
	char *name = ddi_get_name(dev);

	/*
	 * This module drives only "xdc"
	 */

	if (strcmp(name, "xdc") == 0) {
		return (DDI_IDENTIFIED);
	} else {
		return (DDI_NOT_IDENTIFIED);
	}
}

static int
xdcprobe(dev_info_t *dev)
{
	auto char local[16];
	auto volatile struct xddevice *reg;
	register struct xdctlr *c;
	register struct xdcmdblock *xdcbi = (struct xdcmdblock *) 0;
	register instance = ddi_get_instance(dev);
	u_char err;

	/*
	 * Since we know that some instantiations of this device can
	 * be plugged into slave-only VME slots, check to see whether
	 * this is one such.
	 */
	if (ddi_slaveonly(dev) == DDI_SUCCESS) {
		return (DDI_PROBE_FAILURE);
	}

	(void) sprintf(local, "xdc%d", instance);
	if (ddi_soft_state_zalloc(xdc_state, instance) != DDI_SUCCESS) {
		cmn_err(CE_WARN, "%s: cannot allocate softstate", local);
		return (DDI_PROBE_FAILURE);
	}
	c = ddi_get_soft_state(xdc_state, instance);
	ASSERT(c);
	reg = (struct xddevice *) 0;

	/*
	 * Always add in the device's interrupt first
	 */
	if (ddi_add_intr(dev, 0, &c->c_ibc, &c->c_idc, xdcintr, (caddr_t)c)) {
		cmn_err(CE_WARN, "%s: cannot add interrupt", local);
		goto failure;
	}

	mutex_init(&c->c_mutex, "xdctlr", MUTEX_DRIVER, (void *)c->c_ibc);
	cv_init(&c->c_iopbcvp, "xdiobp", CV_DRIVER, (void *)c->c_ibc);

	/*
	 * Map in device registers
	 */

	if (ddi_map_regs(dev, (u_int)0, (caddr_t *)&reg, (off_t)0, XDHWSIZE)) {
		cmn_err(CE_WARN, "%s: unable to map registers", local);
		goto failure;
	}

	/*
	 * See if there's hardware present by trying to reset it.
	 */

	mutex_enter(&c->c_mutex);
	if (ddi_pokec(dev, (char *)&reg->xd_csr, (char)XD_RST)
	    != DDI_SUCCESS) {
		mutex_exit(&c->c_mutex);
		goto failure;
	}
	XDCDELAY(((reg->xd_csr & XD_RACT) == 0), 100000);

	/*
	 * A reset should never take more than .1 sec.
	 */
	if (reg->xd_csr & XD_RACT) {
		mutex_exit(&c->c_mutex);
		cmn_err(CE_WARN, "%s: controller reset failed", local);
		goto failure;
	}
	mutex_exit(&c->c_mutex);

	c->c_dip = dev;
	c->c_io = reg;
	c->c_start = xdcstart;
	c->c_getcbi = xdcgetcbi;
	c->c_putcbi = xdcputcbi;
	c->c_cmd = xdccmd;
	c->c_lim = &xdc_lim;

	/*
	 * Allocate iopbs and command blocks now
	 * (for probing purposes).
	 */

	if (xdccreatecbis(c, NCBIFREE) == DDI_FAILURE) {
		goto failure;
	}

	/*
	 * Read the controller parameters to make sure it's a 7053.
	 */
	if ((xdcbi = xdcgetcbi(c, 1, XY_SYNCH)) == NULL) {
		goto failure;
	}

	mutex_enter(&c->c_mutex);
	err = xdccmd(xdcbi, XD_RPAR | XD_CTLR, NOLPART,
	    0, 0, 0, 0, XY_SYNCH, 0);
	mutex_exit(&c->c_mutex);
	if (err) {
		cmn_err(CE_WARN, "%s: unable to read controller parameters",
		    local);
		goto failure;
	} else if (xdcbi->iopb->xd_ctype != XDC_7053) {
		cmn_err(CE_WARN, "%s: unsupported controller type %x",
		    local, xdcbi->iopb->xd_ctype);
		goto failure;
	}

	xdcputcbi(xdcbi);

	/*
	 * probe is stateless (probe(9E)).
	 */
	ddi_unmap_regs(dev, 0, (caddr_t *)&reg, (off_t)0, (off_t)0);
	cv_destroy(&c->c_iopbcvp);
	mutex_destroy(&c->c_mutex);
	ddi_remove_intr(dev, 0, c->c_ibc);
	(void) xdccreatecbis(c, 0);
	ddi_soft_state_free(xdc_state, instance);
	ddi_set_driver_private(dev, (caddr_t)0);
	return (DDI_PROBE_SUCCESS);

failure:

	if (xdcbi) {
		xdcputcbi(xdcbi);
	}
	if (reg) {
		ddi_unmap_regs(dev, 0, (caddr_t *)&reg, (off_t)0, (off_t)0);
	}
	if (c) {
		if (c->c_ibc) {
			cv_destroy(&c->c_iopbcvp);
			mutex_destroy(&c->c_mutex);
			ddi_remove_intr(dev, 0, c->c_ibc);
		}
		if (c->c_nfree)
			(void) xdccreatecbis(c, 0);
		ddi_soft_state_free(xdc_state, instance);
	}
	ddi_set_driver_private(dev, (caddr_t)0);
	return (DDI_PROBE_FAILURE);
}


static int
xdcattach(dev_info_t *dev, ddi_attach_cmd_t cmd)
{
	auto char local[16];
	auto volatile struct xddevice *reg;
	register struct xdctlr *c;
	register struct xdcmdblock *xdcbi = (struct xdcmdblock *) 0;
	register instance = ddi_get_instance(dev);
	u_char err;

	switch (cmd) {

	case DDI_ATTACH:


		/*
		 * We know that we probed successfully
		 */
		(void) sprintf(local, "xdc%d", instance);
		if (ddi_soft_state_zalloc(xdc_state, instance) != DDI_SUCCESS) {
			cmn_err(CE_WARN, "%s: cannot allocate softstate",
			    local);
			return (DDI_PROBE_FAILURE);
		}
		c = ddi_get_soft_state(xdc_state, instance);
		ASSERT(c);
		reg = (struct xddevice *) 0;

		/*
		 * Aways add in the device's interrupt first
		 */
		if (ddi_add_intr(dev, 0, &c->c_ibc, &c->c_idc, xdcintr,
		    (caddr_t)c)) {
			cmn_err(CE_WARN, "%s: cannot add interrupt", local);
			goto failure;
		}

		mutex_init(&c->c_mutex, "xdctlr", MUTEX_DRIVER,
		    (void *)c->c_ibc);
		cv_init(&c->c_iopbcvp, "xdiobp", CV_DRIVER, (void *)c->c_ibc);

		/*
		 * Map in device registers
		 */

		if (ddi_map_regs(dev, (u_int)0, (caddr_t *)&reg, (off_t)0,
		    XDHWSIZE)) {
			cmn_err(CE_WARN, "%s: unable to map registers", local);
			goto failure;
		}

		c->c_dip = dev;
		c->c_io = reg;
		c->c_start = xdcstart;
		c->c_getcbi = xdcgetcbi;
		c->c_putcbi = xdcputcbi;
		c->c_cmd = xdccmd;
		c->c_lim = &xdc_lim;

		/*
		 * Allocate iopbs and command blocks now
		 */

		if (xdccreatecbis(c, NCBIFREE) == DDI_FAILURE) {
			goto failure;
		}

		/*
		 * Set the controller parameters.
		 */
		if ((xdcbi = xdcgetcbi(c, 1, XY_SYNCH)) == NULL) {
			goto failure;
		}

		mutex_enter(&c->c_mutex);
		err = xdccmd(xdcbi, XD_WPAR | XD_CTLR, NOLPART, 0, 0,
		    0, (int)xd_ctlrpar0, XY_SYNCH, 0);
		mutex_exit(&c->c_mutex);

		xdcputcbi(xdcbi);

		if (err) {
			cmn_err(CE_WARN, "%s: controller initialization failed",
			    local);
			goto failure;
		}

		mutex_enter(&c->c_mutex);
		if ((c->c_intrstats = kstat_create("xdc", instance, local,
		    "controller", KSTAT_TYPE_INTR, 1,
		    KSTAT_FLAG_PERSISTENT)) != NULL) {
			kstat_install(c->c_intrstats);
		}
		c->c_flags |= XY_C_PRESENT;
		c->rdyiopbq = NULL;
		mutex_exit(&c->c_mutex);
		ddi_set_driver_private(dev, (caddr_t)c);
		ddi_report_dev(dev);
		return (DDI_SUCCESS);
failure:

		if (xdcbi) {
			xdcputcbi(xdcbi);
		}
		if (reg) {
			ddi_unmap_regs(dev, 0, (caddr_t *)&reg, (off_t)0,
			    (off_t)0);
		}
		if (c) {
			if (c->c_ibc) {
				cv_destroy(&c->c_iopbcvp);
				mutex_destroy(&c->c_mutex);
				ddi_remove_intr(dev, 0, c->c_ibc);
			}
			if (c->c_nfree)
				(void) xdccreatecbis(c, 0);
			ddi_soft_state_free(xdc_state, instance);
		}
		ddi_set_driver_private(dev, (caddr_t)0);
		return (DDI_FAILURE);

	default:
		return (DDI_FAILURE);
	}
}


/*
 * Called from xdstrategy and xdcintr to run the buf queue.
 */

static int
xdcstart(caddr_t arg)
{
	register struct xdctlr *c = (struct xdctlr *) arg;
	register struct xdunit *un;
	register struct xdcmdblock *xdcbi;
	register struct buf *bp;
	ddi_dma_handle_t handle;
	register int err;

	/*
	 * While we still have IOPBs, try and get some command started.
	 *
	 * XXX: We need to do a better job of round-robining the
	 * XXX: requests to the disks.
	 */

	mutex_enter(&c->c_mutex);
	while ((bp = c->c_waitqf) != NULL) {
		mutex_exit(&c->c_mutex);
		if (!(xdcbi = xdcgetcbi(c, 0, XY_ASYNCH))) {
			return (0);
		}
		mutex_enter(&c->c_mutex);
		/*
		 * We have a window while getting a cbi that
		 * an interrupt can come in and do our current
		 * work for us, so we have to check to make
		 * sure that we still have work to do *after*
		 * getting a cbi.
		 */
		if ((bp = c->c_waitqf) == NULL) {
			mutex_exit(&c->c_mutex);
			xdcputcbi(xdcbi);
			return (0);
		}
		handle = (ddi_dma_handle_t)0;
		un = xdcunit(c->c_units, INSTANCE(bp->b_edev));
		err = ddi_dma_buf_setup(c->c_dip, bp,
		    (bp->b_flags & B_READ)? DDI_DMA_READ:
		    DDI_DMA_WRITE, xdcstart, (caddr_t)c,
		    &xdc_lim, &handle);
		if (err ==  DDI_DMA_NORESOURCES) {
			mutex_exit(&c->c_mutex);
			/*
			 * We'll be called back later
			 */
			xdcputcbi(xdcbi);
			return (0);

		} else if (err != DDI_DMA_MAPPED) {
			/*
			 * Advance the queue anyway..
			 */
			c->c_waitqf = bp->av_forw;
			mutex_exit(&c->c_mutex);
			switch (err) {
			case DDI_DMA_NOMAPPING:
				bp->b_error = EFAULT;
				break;
			case DDI_DMA_LOCKED:
				bp->b_error = EBUSY;
				break;
			default:
				bp->b_error = EIO;
				break;
			}
			bp->b_flags |= B_ERROR;
			bp->b_resid = bp->b_bcount;
			biodone(bp);
			xdcputcbi(xdcbi);
			mutex_enter(&c->c_mutex);
			continue;
		}

		/*
		 * Okay, we're now committed to run the command
		 *
		 *
		 * If measuring stats, mark exit from wait queue and
		 * entrance into run 'queue' if and only if we are
		 * going to actually start a command.
		 */
		if (un->un_iostats) {
			kstat_waitq_to_runq(KIOSP);
		}
		c->c_waitqf = bp->av_forw;
		bp->av_forw = 0;
		xdcbi->un = un;
		xdcbi->breq = bp;
		xdcbi->handle = handle;
		XDGO(xdcbi);
	}
	mutex_exit(&c->c_mutex);
	return (1);
}

/*
 * This routine is the focal point of all commands to the controller.
 * Every command passes through here, independent of its source or
 * reason.  The mode determines whether we are synchronous, asynchronous,
 * or asynchronous but waiting for completion.  The flags are used to
 * suppress error recovery and messages when we are doing special operations.
 *
 * It is used by xdprobe(), findslave(), doattach(), usegeom(),
 * XDGO(), xdioctl(), xdwatch(), and xddump().
 *
 * It is always called at disk interrupt priority.
 *
 * NOTE: this routine assumes that all operations done before the disk's
 * geometry is defined are done on block 0.  This impacts both this routine
 * and the error recovery scheme (even the restores must use block 0).
 * Failure to abide by this restriction could result in an arithmetic trap.
 */

static int
xdccmd(register struct xdcmdblock *cmdblk, u_short cmd, int device,
	ddi_dma_handle_t handle, int unit, daddr_t blkno, int secnt,
	int mode, int flags)
{
	register struct xdiopb *xd = cmdblk->iopb;
	register struct xdctlr *c = cmdblk->c;
	register struct xdunit *un = c->c_units[unit];
	int stat = 0;

	/*
	 * Fill in the cmdblock fields.
	 */
	if (mode == XY_ASYNCHWAIT) {
		flags |= XY_WAIT;
	}
	cmdblk->flags = flags;

	cmdblk->retries = cmdblk->restores = cmdblk->resets = 0;
	cmdblk->slave = (u_char) unit;
	cmdblk->cmd = cmd;
	cmdblk->device = device;
	cmdblk->blkno = blkno;
	cmdblk->boff = 0;
	cmdblk->nsect = (u_short) secnt;
	cmdblk->failed = 0;

	if (xddebug > 1) {
		cmn_err(CE_CONT,
		    "xdc%d: xdccmd- cmd %x hndle %x, un %d blk 0x%x sec 0x%x\n",
		    ddi_get_instance(c->c_dip), cmd, (int)handle, unit,
		    (int)blkno, secnt);
	}

	/*
	 * Watchdog info?
	 */

	/*
	 * Initialize the diagnostic info if necessary.
	 */
	if ((cmdblk->flags & XY_DIAG) && (un != NULL))
		un->un_errsevere = HDK_NOERROR;
	/*
	 * Clear out the iopb fields that need it.
	 */

	cleariopb(xd);

	/*
	 * Set the iopb fields that are the same for all commands.
	 */

	xd->xd_cmd = cmd >> 8;
	xd->xd_subfunc = cmd;
	xd->xd_llength = 0;
	xd->xd_unit = (u_char) unit;

	xd->xd_chain = 0;

	/*
	 * If the blockno is 0, we don't bother calculating the disk
	 * address.  NOTE: this is a necessary test, since some of the
	 * operations on block 0 are done while un is not yet defined.
	 * Removing the test would cause bad pointer references.
	 */
	if (blkno != 0) {
		xd->xd_cylinder = blkno /
		    ((daddr_t)(un->un_g.dkg_nhead * un->un_g.dkg_nsect));
		xd->xd_head = (blkno / (daddr_t)un->un_g.dkg_nsect) %
		    (daddr_t)un->un_g.dkg_nhead;
		xd->xd_sector = blkno % (daddr_t)un->un_g.dkg_nsect;
	} else
		xd->xd_cylinder = xd->xd_head = xd->xd_sector = 0;
	xd->xd_nsect = (u_short) secnt;

	xd->xd_bufaddr = 0;
	switch (cmd) {
	case XD_RESTORE:
	case XD_WEXT | XD_FORMVER:
	case XD_RPAR | XD_CTLR:
	case XD_RPAR | XD_DRIVE:

		break;
	case XD_WPAR | XD_CTLR:
		/*
		 * If we are doing a set controller params command,
		 * we need to hack fields of the iopb.
		 */
		xd->xd_throttle = (xd_ctlrpar1<<8) | xdthrottle;

		/*
		 * 4 is the known default for all extant
		 * SVr4/5.0 platforms.
		 */
		xd->xd_sector = ddi_getprop(DDI_DEV_T_ANY,
		    c->c_dip, 0, "xd-throttle-delay", 4);
		break;

	case XD_WPAR | XD_DRIVE:
		/*
		 * If we are doing a set drive parameters command,
		 * we need to hack in a field of the iopb.
		 *
		 * Note: xd_drparam also contains the interrupt
		 * level field. You *must* set the interrupt
		 * level *after* you set xd_drparam if you expect
		 * to get an interrupt back.
		 */

		xd->xd_drparam = (u_char)handle;
		break;

	case XD_WPAR | XD_FORMAT:
		xd->xd_bufaddr = (u_int) handle;
		/*
		 * If we are doing a set format parameters command,
		 * we need to hack in a field of the iopb.
		 */
		xd->xd_interleave = XD_INTERLEAVE;
		break;

	case XD_REXT | XD_DEFECT:
	case XD_REXT | XD_EXTDEF:
	case XD_WEXT | XD_DEFECT:
	case XD_WEXT | XD_EXTDEF:
	case XD_REXT | XD_THEAD:
	case XD_WEXT | XD_THEAD:
	case XD_READ:
	case XD_WRITE:
	{
		ddi_dma_cookie_t cookie;
		cmdblk->handle = handle;
		if (ddi_dma_htoc(handle, cmdblk->boff, &cookie)) {
			cmn_err(CE_PANIC, "xdccmd: ddi_dma_htoc fails");
			/* NOTREACHED */
		}
		xd->xd_bufaddr = cookie.dmac_address;
		xd->xd_bufmod = cookie.dmac_type;

		if ((xddebug > 1) || (xddebug && mode == XY_SYNCH) ||
		    (xddebug && un && cmdblk->breq == un->un_sbufp)) {
			cmn_err(CE_CONT,
			    "xdc%d %s %d sec slave %d %d/%d/%d %s %x.%x\n",
			    ddi_get_instance(c->c_dip),
			    (cmd == XD_READ)? "read" : "write", secnt, unit,
			    xd->xd_cylinder, xd->xd_head,
			    xd->xd_sector, (cmd == XD_READ)? "to" : "from",
			    xd->xd_bufmod, xd->xd_bufaddr);
		}
		break;
	}
	default:
		if (mode != XY_SYNCH) {
			cmn_err(CE_PANIC, "xdccmd: unkown cmd 0x%x\n", cmd);
			/* NOTREACHED */
		} else {
			return (-1);
		}
		break;
	}

	/*
	 * If command is synchronous, execute it.  We continue to call
	 * error recovery (which will continue to execute commands) until
	 * it returns either success or complete failure.
	 */
	if (mode == XY_SYNCH) {
		xdcsynch(cmdblk);
		while ((stat = xdcrecover(cmdblk, xdcsynch)) > 0)
			;
		if (xddebug) {
			cmn_err(CE_CONT,
			    "xdc%d: xdcrecover of xdcsynch returns %d\n",
			    ddi_get_instance(c->c_dip), stat);
		}
		return (stat);
	}

	/*
	 * If command is asynchronous, set up it's execution.  We only
	 * start the execution if the controller is in a state where it
	 * can accept another command via xdcexec().
	 */

	xdcasynch(cmdblk);
	xdcexec(cmdblk);

	/*
	 * If we are waiting for the command to finish (XY_ASYNCHWAIT)
	 * the caller will block on it (we set the flag XY_WAIT above).
	 */

	if (mode == XY_ASYNCHWAIT) {
		while ((cmdblk->flags & XY_DONE) == 0)
			cv_wait(&cmdblk->cw, &c->c_mutex);
		stat = cmdblk->flags & XY_FAILED;
	}

	/*
	 * Always zero for ASYNCH case, true result for ASYNCHWAIT case.
	 */

	if (stat)
		return (-1);
	else
		return (0);
}


/*
 * This routine executes a command synchronously.
 *
 * Note: Since the 7053 can take a lot of commands,
 * we have to handle having some of them complete
 * while we are here waiting for our synchronous
 * command to complete. This presents a problem.
 * We cannot just pass completed commands that
 * aren't ours off to the interrupt service routine
 * because that will involved releasing a lock
 * on the controller which would open up a window
 * in which the synchronous command we are doing
 * would complete and trigger an interrupt vectored
 * through xdcintr(). xdcintr() could be taught
 * to distinguish synchronous commands from
 * other commands, but it gets hairy in trying
 * to get back here. Instead of doing that, we'll
 * store up commands that complete here that aren't
 * ours and when ours completes, we'll call xdcsvc()
 * on the others that completed while we were
 * here.
 *
 * This has the implication that all commands that
 * are to be executed synchronously should be commands
 * that have a very short duration. If this turns
 * out to not be the case, another method should
 * be chosen.
 *
 */

static void
xdcsynch(register struct xdcmdblock *cmdblk)
{
	register struct xdcmdblock *pending = (struct xdcmdblock *) 0;
	register struct xdiopb *iopb, *xd_iopb;
	register volatile struct xddevice *c_io = cmdblk->c->c_io;
	register struct xdctlr *c = cmdblk->c;
	auto ddi_dma_cookie_t ck;
	auto off_t offset;
	u_char csr;

	/*
	 * Set necessary iopb fields then have the command executed.
	 */

	iopb = cmdblk->iopb;
	iopb->xd_intvec = 0;
	iopb->xd_intpri = 0;	/* no interrupt */
	xdcexec(cmdblk);

again:

	/*
	 * Wait for the command to complete or until a timeout occurs.
	 */

	XDCDELAY((c_io->xd_csr & XD_RIO), 1000000 * XYLOSTINTTIMO);

	/*
	 * If we timed out, use the lost interrupt error to pass
	 * back status or just reset the controller if the command
	 * had already finished.
	 */

	csr = c_io->xd_csr;

	if (csr & XD_FERR) {
		csr = c->c_io->xd_fatal;
		c->c_io->xd_csr = XD_RST;
		/*
		 * Until we get things set up to retry all commands
		 * blown away by a reset, this will be a panic.
		 */
		mutex_exit(&c->c_mutex);
		cmn_err(CE_PANIC, "xdc%d: xdcsynch: fatal error set (0x%x)",
		    ddi_get_instance(c->c_dip), csr);
		/* NOTREACHED */
	}

	if ((csr & XD_RIO) == 0) {
		iopb->xd_iserr = 1;
		iopb->xd_errno = XDE_LINT;
		cmn_err(CE_WARN, "xdc%d: xdcsynch- lost interrupt",
		    ddi_get_instance(c->c_dip));
		return;
	}

	ck.dmac_address = XDC_GET_IOPBADDR(c);
	ck.dmac_type = c_io->xd_modifier;

	/*
	 * Figure out which iopb completed
	 */

	if (ddi_dma_coff(c->c_ihandle, &ck, &offset)) {
		cmn_err(CE_PANIC, "xdc%d: xdcsynch- ddi_dma_coff fails on %x",
		    ddi_get_instance(c->c_dip), ck);
		/* NOTREACHED */
	}
	xd_iopb = (struct xdiopb *) ((u_long) c->c_iopbbase + (u_long) offset);

	/*
	 * DDI required synchronization.
	 */

	(void) ddi_dma_sync(c->c_ihandle, offset, sizeof (struct xdiopb),
	    DDI_DMA_SYNC_FORCPU);

	/*
	 * Allow the controller to proceed
	 */
	c_io->xd_csr = XD_CLRIO;

	/*
	 * Now see if the completed command is truly ours.
	 */
	if (iopb != xd_iopb) {
		int idx = (int)((u_int)offset / (u_int)c->c_iopbsize);
		register struct xdcmdblock *thiscmd = &c->c_cmdbase[idx];
		/*
		 * It isn't. save it up to be completed later.
		 */
		thiscmd->xdcmd_next = pending;
		pending = thiscmd;
		goto again;
	}

	/*
	 * Our command is (finally) done.
	 * Complete any other commands that
	 * finished while we were here.
	 */

	while ((cmdblk = pending) != (struct xdcmdblock *) 0) {
		pending = pending->xdcmd_next;
		cmdblk->xdcmd_next = 0;
		mutex_exit(&c->c_mutex);
		xdcsvc(c, cmdblk);
		mutex_enter(&c->c_mutex);
	}
}

/*
 * This routine sets the fields in the iopb that are needed for an
 * asynchronous operation.  It does not start the operation.
 * It is used by xdccmd() and xdcintr().
 */
static void
xdcasynch(register struct xdcmdblock *cmdblk)
{
	register struct xdiopb *xd = cmdblk->iopb;

	xd->xd_intvec = cmdblk->c->c_idc.idev_vector;
	xd->xd_intpri = cmdblk->c->c_idc.idev_priority;
}

/*
 * This routine is the actual interface to the controller registers.
 * It starts the controller up on the iopb passed.
 *
 * Callers guarantee the appropriate blocking has been done.
 */

static void
xdcexec(register struct xdcmdblock *xdcbi)
{
	register struct xdctlr *c = xdcbi->c;
	register volatile struct xddevice *xdio = c->c_io;
	register struct xdiopb *riopb;

	if (c->rdyiopbq == NULL) {
		if ((xdio->xd_csr & XD_AIOP) == 0) {
			xdcpushiopb(xdcbi->iopb, c);
			return;
		}
	}

	/*
	 * If we're here, then either there is something on the
	 * iopb ready queue or the controller isn't ready so
	 * first put this iopb on the queue.
	 */
	if (c->rdyiopbq == NULL) {
		c->rdyiopbq = xdcbi->iopb;
	} else {
		c->lrdy->xd_nxtaddr = xdcbi->iopb;
	}
	xdcbi->iopb->xd_nxtaddr = NULL;
	c->lrdy = xdcbi->iopb;

	/*
	 * now pull iopbs off the ready queue as long as the controller is
	 * ready to accept them.
	 */

	while (!(xdio->xd_csr & XD_AIOP) && c->rdyiopbq) {
		riopb = c->rdyiopbq;
		/*
		 * xd_nxtaddr is in terms of what the xd sees,
		 * but we never allow the xd to use this (we do not
		 * support iopb chaining), so we use the field for
		 * CPU addresses only.
		 */
		c->rdyiopbq = riopb->xd_nxtaddr;
		riopb->xd_nxtaddr = NULL;
		xdcpushiopb(riopb, c);
	}
}

static void
xdcpushiopb(register struct xdiopb *xd, register struct xdctlr *c)
{
	register volatile struct xddevice *xdio = c->c_io;
	ddi_dma_cookie_t iopbaddr;
	register off_t offset;

	/*
	 * Calculate the address of the iopb.
	 */
	offset = (off_t)((u_long) xd - (u_long) c->c_iopbbase);
	if (ddi_dma_htoc(c->c_ihandle, offset, &iopbaddr)) {
		cmn_err(CE_PANIC, "xdc%d: xdcpushiopb: ddi_dma_htoc failed",
		    ddi_get_instance(c->c_dip));
		/* NOTREACHED */
	}

	/*
	 * Okay, flush it (with respect to the device)
	 */
	(void) ddi_dma_sync(c->c_ihandle, offset, sizeof (*xd),
	    DDI_DMA_SYNC_FORDEV);

	/*
	 * Set the iopb address registers and the address modifier.
	 */

	xdio->xd_iopbaddr1 = XDOFF(iopbaddr.dmac_address, 0);
	xdio->xd_iopbaddr2 = XDOFF(iopbaddr.dmac_address, 1);
	xdio->xd_iopbaddr3 = XDOFF(iopbaddr.dmac_address, 2);
	xdio->xd_iopbaddr4 = XDOFF(iopbaddr.dmac_address, 3);
	xdio->xd_modifier = iopbaddr.dmac_type;

	/*
	 * Set the go bit.
	 */
	xdio->xd_csr = XD_AIO;
}

/*
 * This routine provides the error recovery for all commands to the 7053.
 * It examines the results of a just-executed command, and performs the
 * appropriate action.  It will set up at most one followup command, so
 * it needs to be called repeatedly until the error is resolved.  It
 * returns three possible values to the calling routine : 0 implies that
 * the command succeeded, 1 implies that recovery was initiated but not
 * yet finished, and -1 implies that the command failed.  By passing the
 * address of the execution function in as a parameter, the routine is
 * completely isolated from the differences between synchronous and
 * asynchronous commands.  It is used by xdccmd() r().  It is
 * always called at disk interrupt priority.
 */
static int
xdcrecover(register struct xdcmdblock *cmdblk, register void (*execptr)())
{
	struct xdctlr *c = cmdblk->c;
	register struct xdiopb *xd = cmdblk->iopb;
	register struct xdunit *un = c->c_units[cmdblk->slave];
	struct xderract *actptr;
	struct xderror *errptr;
	int bn, ns, ndone;

	/*
	 * This tests whether an error occured.  NOTE: errors reported by
	 * the status register of the controller must be folded into the
	 * iopb before this routine is called or they will not be noticed.
	 */
	if (xd->xd_iserr) {
		errptr = finderr(xd->xd_errno);
		/*
		 * If drive isn't even attached, we want to use error
		 * recovery but must be careful not to reference null
		 * pointers.
		 */
		if (un != NULL) {
			/*
			 * Drive has been taken offline.  Since the operation
			 * can't succeed, there's no use trying any more.
			 */
			if (!(un->un_flags & XY_UN_PRESENT)) {
				bn = cmdblk->blkno;
				ndone = 0;
				goto fail;
			}
			/*
			 * Extract from the iopb the sector in error and
			 * how many sectors succeeded.
			 */
			bn = ((xd->xd_cylinder * un->un_g.dkg_nhead) +
			    xd->xd_head) * un->un_g.dkg_nsect + xd->xd_sector;
			ndone = bn - cmdblk->blkno;
			/*
			 * Log the error for diagnostics if appropriate.
			 */
			if (cmdblk->flags & XY_DIAG) {
				un->un_errsect = bn;
				un->un_errno = errptr->errno;
				un->un_errcmd =
				    (xd->xd_cmd << 8) | xd->xd_subfunc;
			}
		} else
			bn = ndone = 0;
		if (errptr->errlevel != XD_ERCOR) {
			/*
			 * If the error wasn't corrected, see if it's a
			 * bad block.  If we are already in the middle of
			 * forwarding a bad block, we are not allowed to
			 * encounter another one.  NOTE: the check of the
			 * command is to avoid false mappings during initial
			 * stuff like trying to reset a drive
			 * (the bad map hasn't been initialized).
			 */
			if (((xd->xd_cmd == (XD_READ >> 8)) ||
			    (xd->xd_cmd == (XD_WRITE >> 8))) &&
			    (ns = isbad(&un->un_bad, (int)xd->xd_cylinder,
			    (int)xd->xd_head, (int)xd->xd_sector)) >= 0) {
				if (cmdblk->flags & XY_INFRD) {
					cmn_err(CE_WARN,
				    "xd%d: recursive mapping of block %d",
					    ddi_get_instance(un->un_dip),
					    bn);
					goto fail;
				}

				/*
				 * Verify the presence of error sector in
				 * the range of the current I/O request.
				 * If so, forward the request and execute the
				 * command on alternate block.
				 * Else, look up error action and retry the
				 * same request.
				 */
				if ((bn >= cmdblk->blkno) &&
				    (bn < cmdblk->blkno + (int)cmdblk->nsect)) {
				/*
				 * We have a bad block.  Advance the state
				 * info up to the block that failed.
				 */
				    cmdblk->boff += (off_t)(ndone * SECSIZE);
				    cmdblk->blkno += ndone;
				    cmdblk->nsect -= ndone;
				/*
				 * Calculate the location of the alternate
				 * block.
				 */
				    cmdblk->altblk = bn = (((un->un_g.dkg_ncyl +
					un->un_g.dkg_acyl) *
					un->un_g.dkg_nhead) - 1) *
					un->un_g.dkg_nsect - ns - 1;
				/*
				 * Set state to 'forwarding' and print msg
				 */
				    cmdblk->flags |= XY_INFRD;
				    if ((xderrlvl & EL_FORWARD) &&
					(!(cmdblk->flags & XY_NOMSG))) {
					    cmn_err(CE_NOTE,
				"xd%d: forwarding %d/%d/%d to altblk %d",
					ddi_get_instance(un->un_dip),
					xd->xd_cylinder, xd->xd_head,
					xd->xd_sector, ns);
				    }
				    ns = 1;
				/*
				 * Execute the cmd on the alternate block
				 */
				    goto exec;
				}
			}
			/*
			 * Error was 'real'.  Look up action to take.
			 */
			if (cmdblk->flags & XY_DIAG)
				cmdblk->failed = 1;
			actptr = &xderracts[errptr->errlevel];
			/*
			 * Attempt to retry the entire operation if appropriate.
			 */
			if (cmdblk->retries++ < actptr->retry) {
				if ((xderrlvl & EL_RETRY) &&
				    (!(cmdblk->flags & XY_NOMSG)) &&
				    (errptr->errlevel != XD_ERBSY))
					printerr(un, xd->xd_cmd,
					    cmdblk->device, "retry",
					    errptr->errmsg, bn);
				if (cmdblk->flags & XY_INFRD) {
					bn = cmdblk->altblk;
					ns = 1;
				} else {
					bn = cmdblk->blkno;
					ns = cmdblk->nsect;
				}
				goto exec;
			}
			/*
			 * Attempt to restore the drive if appropriate.  We
			 * set the state to 'restoring' so we know where we
			 * are.  NOTE: there is no check for a recursive
			 * restore, since that is a non-destructive condition.
			 */
			if (cmdblk->restores++ < actptr->restore) {
				if ((xderrlvl & EL_RESTR) &&
				    (!(cmdblk->flags & XY_NOMSG)))
					printerr(un, xd->xd_cmd, cmdblk->device,
					    "restore", errptr->errmsg, bn);
				cmdblk->retries = 0;
				bn = ns = 0;
				xd->xd_cmd = XD_RESTORE >> 8;
				xd->xd_subfunc = 0;
				cmdblk->flags |= XY_INRST;
				goto exec;
			}
			/*
			 * Attempt to reset the controller if appropriate.
			 */
			if (cmdblk->resets++ < actptr->reset) {
				/*
				 * Until we handle retrying commands
				 * blown away by a controller reset,
				 * this is a panic.
				 */
				cmn_err(CE_PANIC,
				    "xdc%d: xdcrecover- fatal reset 1",
				    ddi_get_instance(cmdblk->c->c_dip));
				/* NOTREACHED */
			}

			/*
			 * Command has failed.  We either fell through to
			 * here by running out of recovery or jumped here
			 * for a good reason.
			 */
fail:
			if ((xderrlvl & EL_FAIL) &&
			    (!(cmdblk->flags & XY_NOMSG)))
				printerr(un, xd->xd_cmd, cmdblk->device,
				    "failed", errptr->errmsg, bn);
			/*
			 * If the failure was caused by
			 * a 'drive busy' type error, the drive has probably
			 * been taken offline, so we mark it as gone.
			 */
			if ((errptr->errlevel == XD_ERBSY) &&
			    (un != NULL) && (un->un_flags & XY_UN_PRESENT)) {
				un->un_flags &= ~XY_UN_PRESENT;
				cmn_err(CE_NOTE, "xd%d: drive offline",
				    ddi_get_instance(un->un_dip));
			}
			/*
			 * If the failure implies the drive is faulted, do
			 * one final restore in hopes of clearing the fault.
			 */
			if ((errptr->errlevel == XD_ERFLT) &&
			    (!(cmdblk->flags & XY_FNLRST))) {
				bn = ns = 0;
				xd->xd_cmd = XD_RESTORE >> 8;
				xd->xd_subfunc = 0;
				cmdblk->flags &= ~(XY_INFRD | XY_INRST);
				cmdblk->flags |= XY_FNLRST;
				cmdblk->failed = 1;
				goto exec;
			}
			/*
			 * If the failure implies the controller is hung, do
			 * a controller reset in hopes of fixing its state.
			 */
			if (errptr->errlevel == XD_ERHNG) {
				/*
				 * Until we handle retrying commands
				 * blown away by a controller reset,
				 * this is a panic.
				 */
				cmn_err(CE_PANIC,
				    "xdc%d: xdcrecover- fatal reset 2",
				    ddi_get_instance(cmdblk->c->c_dip));
				/* NOTREACED */
			}

			/*
			 * Note the failure for diagnostics and return it.
			 */
			if (cmdblk->flags & XY_DIAG)
				un->un_errsevere = HDK_FATAL;
			return (-1);
		}
		/*
		 * A corrected error.  Simply print a message and go on.
		 */
		if (cmdblk->flags & XY_DIAG) {
			cmdblk->failed = 1;
			un->un_errsevere = HDK_CORRECTED;
		}
		if ((xderrlvl & EL_FIXED) && (!(cmdblk->flags & XY_NOMSG)))
			printerr(un, xd->xd_cmd, cmdblk->device,
			    "fixed", errptr->errmsg, bn);
	}

	/*
	 * Command succeeded.  Either there was no error or it was corrected.
	 * We aren't done yet, since the command executed may have been part
	 * of the recovery procedure.  We check for 'restoring' and
	 * 'forwarding' states and restart the original operation if we
	 * find one.
	 */

	if (cmdblk->flags & XY_INRST) {
		xd->xd_cmd = cmdblk->cmd >> 8;
		xd->xd_subfunc = cmdblk->cmd;
		if (cmdblk->flags & XY_INFRD) {
			bn = cmdblk->altblk;
			ns = 1;
		} else {
			bn = cmdblk->blkno;
			ns = cmdblk->nsect;
		}
		cmdblk->flags &= ~XY_INRST;
		goto exec;
	}
	if (cmdblk->flags & XY_INFRD) {
		cmdblk->boff += (off_t)SECSIZE;
		bn = ++cmdblk->blkno;
		ns = --cmdblk->nsect;
		cmdblk->flags &= ~XY_INFRD;
		if (ns > 0)
			goto exec;
	}

	/*
	 * Last command succeeded. However, since the overall command
	 * may have failed, we return the appropriate status.
	 */
	if (cmdblk->failed) {
		/*
		 * Figure out whether or not the work got done so
		 * diagnostics knows what happened.
		 */
		if ((cmdblk->flags & XY_DIAG) &&
		    (un->un_errsevere == HDK_NOERROR)) {
			if (cmdblk->flags & XY_FNLRST)
				un->un_errsevere = HDK_FATAL;
			else
				un->un_errsevere = HDK_RECOVERED;
		}
		return (-1);
	} else
		return (0);
	/*
	 * Executes the command set up above.
	 * Only calculate the disk address if block isn't zero.  This is
	 * necessary since some of the operations of block 0 occur before
	 * the disk geometry is known (could result in zero divides).
	 */
exec:
	if (bn > 0) {
		xd->xd_cylinder = bn /
		    ((daddr_t)(un->un_g.dkg_nhead * un->un_g.dkg_nsect));
		xd->xd_head = (bn / (daddr_t)un->un_g.dkg_nsect) %
		    (daddr_t)un->un_g.dkg_nhead;
		xd->xd_sector = bn % (daddr_t)un->un_g.dkg_nsect;
	} else
		xd->xd_cylinder = xd->xd_head = xd->xd_sector = 0;
	xd->xd_nsect = (u_short) ns;
	xd->xd_unit = cmdblk->slave;
	if (cmdblk->handle) {
		ddi_dma_cookie_t cookie;
		if (ddi_dma_htoc(cmdblk->handle, cmdblk->boff, &cookie)) {
			cmn_err(CE_PANIC,
			    "xdc%d: xdcrecover: ddi_dma_htoc fails on slave %d",
			    ddi_get_instance(c->c_dip), cmdblk->slave);
			/* NOTREACHED */
		}
		xd->xd_bufaddr = cookie.dmac_address;
		xd->xd_bufmod = cookie.dmac_type;
	} else {
		xd->xd_bufaddr = 0;
	}

	/*
	 * Clear out the iopb fields that need it.
	 */

	cleariopb(xd);

	/*
	 * If we are doing a set controller params command, we need to
	 * hack in a field of the iopb.
	 */

	if (cmdblk->cmd == (XD_WPAR | XD_CTLR))
		xd->xd_throttle = (xd_ctlrpar1<<8) | xdthrottle;

	/*
	 * Execute the command and return a 'to be continued' status.
	 */
	(*execptr)(cmdblk);
	return (1);
}

/*
 * This routine searches the error structure for the specified error and
 * returns a pointer to the structure entry.  If the error is not found,
 * the last entry in the error table is returned (this MUST be left as
 * unknown error).  It is used by xdcrecover().  It is always called at
 * disk interrupt priority.
 */
static struct xderror *
finderr(register u_char errno)
{
	register struct xderror *errptr;

	for (errptr = xderrors; errptr->errno != XDE_UNKN; errptr++)
		if (errptr->errno == errno)
			break;
	return (errptr);
}

/*
 * This routine prints out an error message containing as much data
 * as is known.
 */
static void
printerr(struct xdunit *un, u_char cmd, short device,
	char *action, char *msg, daddr_t bn)
{

	if (device != NOLPART) {
		cmn_err(CE_CONT, "xd%d%c: ",
		    (int)INSTANCE(device), (int)LPART(device) + 'a');
	} else if (un != NULL) {
		cmn_err(CE_CONT, "xd%d: ", ddi_get_instance(un->un_dip));
	} else {
		cmn_err(CE_CONT, "xd: ");
	}
	if (cmd < XDCMDS) {
		cmn_err(CE_CONT, "%s ", xdcmdnames[cmd]);
	} else {
		cmn_err(CE_CONT, "[cmd 0x%x] ", cmd);
	}
	cmn_err(CE_CONT, "%s (%s) -- ", action, msg);
	if ((device != NOLPART) && (un != NULL)) {
		cmn_err(CE_CONT, "blk #%d, ",
		    (int)bn - un->un_map[LPART(device)].dkl_cylno *
		    un->un_g.dkg_nsect * un->un_g.dkg_nhead);
	}
	cmn_err(CE_CONT, "abs blk #%d\n", (int)bn);
}

/*
 * This routine handles controller interrupts.
 */

static u_int
xdcintr(caddr_t arg)
{
	register struct xdctlr *c = (struct xdctlr *) arg;
	register struct xdcmdblock *thiscmd;
	auto struct xdiopb *findaddr;
	ddi_dma_cookie_t xdioa;
	off_t offset;
	int idx;

	mutex_enter(&c->c_mutex);

	/*
	 * First, check for fatal error condition
	 */

	if (c->c_io->xd_csr & XD_FERR) {
		u_char ferr;
		/*
		 * Until we get things set up to retry all commands
		 * blown away by a reset, this will be a panic.
		 */
		ferr = c->c_io->xd_fatal;
		c->c_io->xd_csr = XD_RST;
		mutex_exit(&c->c_mutex);
		cmn_err(CE_PANIC, "xdc%d: xdcintr- fatal error set (0x%x)",
		    ddi_get_instance(c->c_dip), ferr);
		/* NOTREACHED */
	}

	/*
	 * Now, check for a spurious interrupt.
	 */
	if ((c->c_io->xd_csr & XD_RIO) == 0) {
		if (c->c_intrstats) {
			KIOIP->intrs[KSTAT_INTR_SPURIOUS]++;
		}
		mutex_exit(&c->c_mutex);
		return (DDI_INTR_UNCLAIMED);
	}

	if (c->c_intrstats) {
		KIOIP->intrs[KSTAT_INTR_HARD]++;
	}
	/*
	 * Grab the address of the complete iopb and -
	 */

	xdioa.dmac_address = XDC_GET_IOPBADDR(c);
	xdioa.dmac_type = c->c_io->xd_modifier;

	if (ddi_dma_coff(c->c_ihandle, &xdioa, &offset)) {
		mutex_exit(&c->c_mutex);
		cmn_err(CE_WARN, "xdc%d: xdcintr- ddi_dma_coff fails on %x",
		    ddi_get_instance(c->c_dip), xdioa);
		return (DDI_INTR_CLAIMED);
	}
	/*
	 * DDI required synchronization.
	 */
	(void) ddi_dma_sync(c->c_ihandle, offset, sizeof (struct xdiopb),
	    DDI_DMA_SYNC_FORCPU);

	findaddr = (struct xdiopb *) ((u_long)c->c_iopbbase + (u_long) offset);

	/*
	 * Ready the controller for a new command.
	 */
	c->c_io->xd_csr = XD_CLRIO;

	/*
	 * We know from the offset for the iopb and the c_iopbsize field
	 * which ordinal number out of the pool it is. We can find the
	 * associated command block by using this number (since there
	 * is a one-to-one correspondence between the iopb pool and
	 * command block pool).
	 */
	idx = (int)((u_int)offset / (u_int)c->c_iopbsize);
	thiscmd = &c->c_cmdbase[idx];
	if (thiscmd->iopb != findaddr) {
		mutex_exit(&c->c_mutex);
		cmn_err(CE_WARN, "xdc%d: xdcintr- unmatched iopb at %x",
		    ddi_get_instance(c->c_dip), (int)findaddr);
		return (DDI_INTR_CLAIMED);
	}

	/*
	 * Pass the rest of this to the common interrupt service code
	 */

	mutex_exit(&c->c_mutex);

	xdcsvc(c, thiscmd);

	return (DDI_INTR_CLAIMED);

}

static void
xdcsvc(register struct xdctlr *c, register struct xdcmdblock *thiscmd)
{
	register int stat;
	register struct xdiopb *riopb;

	mutex_enter(&c->c_mutex);

	/*
	 * Execute the error recovery on the iopb.
	 * If stat came back greater than zero, the
	 * error recovery has re-executed the command.
	 * but it needs to be continued ...
	 */
	if ((stat = xdcrecover(thiscmd, xdcasynch)) > 0) {
		xdcexec(thiscmd);
		mutex_exit(&c->c_mutex);
		return;
	}

	/*
	 * In the ASYNCHWAIT case, we pass back
	 * status via the flags and wakeup the
	 * calling process.
	 */
	if (thiscmd->flags & XY_WAIT) {
		thiscmd->flags &= ~XY_WAIT;
		if (stat < 0)
			thiscmd->flags |= XY_FAILED;
		thiscmd->flags |= XY_DONE;
		/*
		 * We can use a cv_signal because we
		 * know that we only will have one
		 * thread waiting on this condition
		 * variable at a time.
		 */
		cv_signal(&thiscmd->cw);
	} else {
		/*
		 * In the ASYNCH case, we pass back status
		 * via the buffer.  If the command used
		 * mainbus space, we release that.  If
		 * someone wants the iopb, wake them up,
		 * otherwise start up the next buf operation.
		 *
		 * If measuring stats, mark an exit from
		 * the busy timing and tote up the number
		 * of bytes moved.
		 */
		register struct xdunit *un = thiscmd->un;
		register struct buf *bp = thiscmd->breq;
		register ddi_dma_handle_t *handle = thiscmd->handle;

		mutex_exit(&c->c_mutex);
		xdcputcbi(thiscmd);
		if (handle) {
			ddi_dma_free(handle);
		}
		if (stat == -1) {
			bp->b_resid = bp->b_bcount;
			bp->b_flags |= B_ERROR;
		} else if (bp != un->un_sbufp && un->un_iostats) {
			u_long n_done = bp->b_bcount - bp->b_resid;
			mutex_enter(&c->c_mutex);
			if (bp->b_flags & B_READ) {
				KIOSP->reads++;
				KIOSP->nread += n_done;
			} else {
				KIOSP->writes++;
				KIOSP->nwritten += n_done;
			}
			kstat_runq_exit(KIOSP);
			mutex_exit(&c->c_mutex);
		}
		biodone(bp);
		mutex_enter(&c->c_mutex);
	}

	if (c->rdyiopbq != NULL) {
		/*
		 * need to run the ready iopb q and
		 * xdcexec isn't quite right
		 * XXX: FIX THIS
		 */

		/*
		 * are there any ready and waiting iopbs?
		 */

		while (!(c->c_io->xd_csr & XD_AIOP) && c->rdyiopbq) {
			/*
			 * pull an iopb off the queue and go
			 */
			riopb = c->rdyiopbq;
			/*
			 * xd_nxtaddr is in terms of what the xd sees,
			 * but we never allow the xd to use this (we do not
			 * support iopb chaining), so we use the field for
			 * CPU addresses only.
			 */
			c->rdyiopbq = riopb->xd_nxtaddr;
			riopb->xd_nxtaddr = NULL;
			xdcpushiopb(riopb, c);
		}
	}
	if (c->c_waitqf) {
		mutex_exit(&c->c_mutex);
		(void) xdcstart((caddr_t)c);
	} else {
		mutex_exit(&c->c_mutex);
	}
}

/*
 * Create xd cbis and iopbs for a controller.
 *
 * Called at probe time for controller probe stuff
 * and later to increase size of iopb pool for the
 * controller. The caller guarantees that the the
 * controller is quiescent when this routine is
 * called (i.e., no need to block).
 */

static int
xdccreatecbis(register struct xdctlr *c, int ncbis)
{
	register struct xdcmdblock *xdcbi = (struct xdcmdblock *) 0;
	ddi_dma_handle_t h = (ddi_dma_handle_t)0;
	ddi_dma_cookie_t ck;
	caddr_t iopb = (caddr_t)0;
	u_int size, align, mineffect, niopb;

	/*
	 * Sanity check some things before proceeding
	 */

	if (ncbis == 0 && c->c_niopbs) {
		if (c->c_ihandle) {
			ddi_dma_free(c->c_ihandle);
			c->c_ihandle = 0;
		}
		ddi_iopb_free(c->c_iopbbase);
		kmem_free(c->c_freecmd,
			(u_int)c->c_niopbs * sizeof (struct xdcmdblock));
		c->c_niopbs = 0;
		return (DDI_SUCCESS);
	}

	if (c->c_niopbs && (char)ncbis <= c->c_niopbs)
		return (DDI_SUCCESS);

	/*
	 * Do all allocation and mapping prior to releasing old resources.
	 */

	/*
	 * MJ: FIX ALLOCATOR OR FIX xdc_lim so that this jerk doesn't happen
	 */

	size = (ncbis + 1) * sizeof (struct xdiopb);
	if (ddi_iopb_alloc(c->c_dip, &xdc_lim, size, &iopb)) {
		cmn_err(CE_WARN, "xdc%d: cannot allocate iopbs",
		    ddi_get_instance(c->c_dip));
		goto failure;
	}
	if (ddi_dma_addr_setup(c->c_dip, (struct as *) 0, iopb, size,
	    DDI_DMA_RDWR|DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, (caddr_t)0,
	    &xdc_lim, &h)) {
		cmn_err(CE_WARN, "xdc%d: cannot map iopbs",
		    ddi_get_instance(c->c_dip));
		goto failure;
	}

	if (ddi_dma_devalign(h, &align, &mineffect)) {
		cmn_err(CE_WARN, "xdc%d: cannot get iopb dma alignment",
		    ddi_get_instance(c->c_dip));
		goto failure;
	}
	align = max(align, mineffect);
	align = roundup(sizeof (struct xdiopb), align);
	if (align >= 256) {
		cmn_err(CE_WARN, "xdc%d: unmanageable iopb size: %d",
		    ddi_get_instance(c->c_dip), align);
		goto failure;
	}
	niopb = ((ncbis + 1) * sizeof (struct xdiopb))/align;
	if (niopb < ncbis) {
		cmn_err(CE_WARN, "xdc%d: iopb align truncation",
		    ddi_get_instance(c->c_dip));
		goto failure;
	}
	xdcbi = kmem_zalloc(niopb * sizeof (struct xdcmdblock), KM_SLEEP);

	/*
	 * Free old resources
	 */
	if (c->c_ihandle) {
		ddi_dma_free(c->c_ihandle);
		c->c_ihandle = 0;
	}
	if (c->c_niopbs) {
		ddi_iopb_free(c->c_iopbbase);
		kmem_free(c->c_freecmd,
			(u_int)c->c_niopbs * sizeof (struct xdcmdblock));
	}

	/*
	 * Install and initialize the new resources
	 */

	c->c_ihandle = h;
	c->c_nfree = c->c_niopbs = (char)niopb;
	c->c_iopbsize = (u_char) align;
	c->c_cmdbase = xdcbi;
	c->c_freecmd = xdcbi;
	c->c_iopbbase = iopb;

	size = 0;
	while (xdcbi < &c->c_freecmd[niopb]) {
		xdcbi->c = c;
		cv_init(&xdcbi->cw, "xd cmd cv", CV_DRIVER, (void *)c->c_ibc);
		xdcbi->xdcmd_next = xdcbi + 1;
		xdcbi->iopb = (struct xdiopb *) &iopb[size];
		if (ddi_dma_htoc(h, (off_t)size, &ck)) {
			xdcbi = c->c_cmdbase;
			cmn_err(CE_WARN, "xdc%d: bad cookie in initiopb",
			    ddi_get_instance(c->c_dip));
			c->c_ihandle = 0;
			c->c_niopbs = 0;
			goto failure;
		}
		initiopb(xdcbi->iopb, ck.dmac_type);
		xdcbi++;
		size += align;
	}
	(--xdcbi)->xdcmd_next = 0;
	return (DDI_SUCCESS);

failure:

	if (xdcbi)
		kmem_free(xdcbi, niopb * sizeof (struct xdcmdblock));

	if (h)
		ddi_dma_free(h);

	if (iopb)
		ddi_iopb_free(iopb);

	return (DDI_FAILURE);
}

/*
 * Remove xd cbi from free list and put on the active list for the controller.
 */

static struct xdcmdblock *
xdcgetcbi(struct xdctlr *c, int lastone, int mode)
{
	register struct xdcmdblock *xdcbi = NULL;

	mutex_enter(&c->c_mutex);
again:
	if ((xdcbi = c->c_freecmd) != NULL) {
		if (c->c_nfree == 1 && lastone == 0) {
			if (mode == XY_ASYNCHWAIT) {
				cv_wait(&c->c_iopbcvp, &c->c_mutex);
				goto again;
			} else {
				mutex_exit(&c->c_mutex);
				return ((struct xdcmdblock *) NULL);
			}
		}
		/*
		 * Remove the cbi from the free list for the controller
		 */
		c->c_freecmd = xdcbi->xdcmd_next;
		xdcbi->xdcmd_next = 0;
		xdcbi->c = c;
		xdcbi->breq = 0;
		xdcbi->handle = 0;
		xdcbi->busy = 1;
		c->c_nfree--;
	} else if (mode == XY_ASYNCHWAIT) {
		c->c_wantcmd++;
		cv_wait(&c->c_iopbcvp, &c->c_mutex);
		goto again;
	}
	mutex_exit(&c->c_mutex);
	return (xdcbi);
}

/*
 * Put a command block and its related iopb back on the free list.
 */

static void
xdcputcbi(register struct xdcmdblock *xdcbi)
{
	register struct xdctlr *c = xdcbi->c;

	mutex_enter(&c->c_mutex);
	xdcbi->busy = 0;
	xdcbi->xdcmd_next = c->c_freecmd;
	c->c_freecmd = xdcbi;
	c->c_nfree++;
	if (c->c_wantcmd) {
		c->c_wantcmd -= 1;
		cv_signal(&c->c_iopbcvp);
	}
	mutex_exit(&c->c_mutex);
}


/*
 * This routine sets the fields of the iopb that never change.
 */
static void
initiopb(register struct xdiopb *xd, u_int type)
{
	bzero((caddr_t)xd, sizeof (struct xdiopb));
	xd->xd_fixd = 1;
	xd->xd_nxtmod = type;
	xd->xd_prio = 0;
}

/*
 * This routine clears the fields of the iopb that must be zero before a
 * command is executed.
 */
static void
cleariopb(register struct xdiopb *xd)
{

	xd->xd_errno = 0;
	xd->xd_iserr = 0;
	xd->xd_complete = 0;
}

/*
 * Look up a unit struct by instance number of the child
 * called only by xdcstart
 */
static struct xdunit *
xdcunit(register struct xdunit **up, u_int instance)
{
	register int i;

	for (i = 0; i < XDUNPERC; i++, up++)
		if (*up != NULL && (*up)->un_instance == instance)
			return (*up);
	cmn_err(CE_PANIC, "xdcunit");
	/*NOTREACHED*/
}

/*
 * Search the bad sector table looking for
 * the specified sector.  Return index if found.
 * Return -1 if not found.
 */
static int
isbad(register struct dkbad *bt, int cyl, int trk, int sec)
{
	register int i;
	register long blk, bblk;

	blk = ((long)cyl << 16) + (trk << 8) + sec;
	for (i = 0; i < NDKBAD; i++) {
		bblk = ((long)bt->bt_bad[i].bt_cyl << 16) +
		    bt->bt_bad[i].bt_trksec;
		if (blk == bblk)
			return (i);
		if (bblk < 0)
			break;
	}
	return (-1);
}
