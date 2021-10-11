/*
 * Copyright (c) 1987-1993 by Sun Microsystems, Inc.
 */

#pragma	ident	"@(#)xyc.c	1.14	94/02/16 SMI"	/* SunOS 4.1.1 1.52 */

/*
 * Driver for Xylogics 450/451 SMD disk controllers
 */

/*
 * WARNING: This driver is still in the process of being debugged/enhanced.
 * Only Read and Writes have been tested, with 3 processes running
 * concurrently.
 * there is an internal trace to buffer xy_trace, this will be needed for
 * post-mortem debugging. Please, do not turn off this internal trace.
 */

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <sys/uio.h>
#include <sys/fcntl.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/kmem.h>
#include <sys/sysmacros.h>	/* Hmm.. */
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

#include <sys/xyvar.h>
#include <sys/xyerr.h>

/*
 * Local defines
 */
#define	KIOSP		KSTAT_IO_PTR(un->un_iostats)
#define	KIOIP		KSTAT_INTR_PTR(c->c_intrstats)

/*
 * Function Prototypes
 */

static int
xyc_bus_ctl(dev_info_t *d, dev_info_t *r, ddi_ctl_enum_t o, void *a, void *v);
static int xycidentify(dev_info_t *);
static int xycprobe(dev_info_t *);
static int xycattach(dev_info_t *, ddi_attach_cmd_t);
static int xycstart(caddr_t);

static int xyccmd(struct xycmdblock *, u_short, int,
    ddi_dma_handle_t, int, daddr_t, int, int, int);

static void xycsynch(struct xycmdblock *);
static void xycasynch(struct xycmdblock *);
static void xycexec(struct xycmdblock *);
static void xycpushiopb(struct xyiopb *, struct xyctlr *);

static int xycrecover(struct xycmdblock *, void (*)());

static struct xyerror *finderr(u_char);
static void printerr(struct xyunit *, u_char, short, char *, char *, daddr_t);

static u_int xycintr(caddr_t);
static void xycsvc(struct xyctlr *, struct xycmdblock *);

static int xyccreatecbis(struct xyctlr *, int);
static struct xycmdblock *xycgetcbi(struct xyctlr *, int, int);
static void xycputcbi(struct xycmdblock *);
static void initiopb(struct xyiopb *);
static void cleariopb(struct xyiopb *);
static void look_queue(struct xyctlr *c);
static void xycdumpiopb(struct xyiopb *xy);
static void xyc_get_codes(struct xyctlr *c);

static int isbad(struct dkbad *, int, int, int);
static	void xxprintf(char *fmt, ...);
static struct xyunit *xycunit(register struct xyunit **up, u_int instance);

/*
 * Think about why 'volatile' is needed in the prototypes.
 * Perhaps this is a compiler bug?
 */
static int xyccsrvalid(volatile struct xydevice *xyio);
static int xycfree(volatile struct xydevice *xyio);
static void xyc_set_go(struct xyiopb *xy, struct xyctlr *c);
static void xyc_set_iopb(struct xyctlr *c, int iopbaddr);
static int xycwait(volatile struct xydevice *xyio);
static int xycintwait(volatile struct xydevice *xyio);


/*
 * Static local non-autoconf data
 */
static	int	xyprint = 0;

/*
 * Settable error level.
 */
static short xyerrlvl = EL_FIXED | EL_RETRY | EL_RESTR | EL_RESET | EL_FAIL;

/*
 * Debugging macros
 */

#define	TRACE_XY if (xy_debug == 5)
#define	TRACE_NN if (xy_debug == 20000)

/*
 * Declarations for wrap-around trace
 */

#define	PRINTF1 if (xy_debug)	  xxprintf
#define	PRINTF2 if (xy_debug > 1) xxprintf
#define	PRINTF3 if (xy_debug > 2) xxprintf
#define	PRINTF4 if (xy_debug > 3) xxprintf

/*
 *
 *		Adjustable parameters
 *		at compile time
 *
 * MAXLINESIZE = upper limit for one sprintf (increase if needed)
 * TRACE_SIZE  = Size in bytes of the whole trace (increase if needed)
 * xy_debug    = level of tracing (0 no tracing, 4 maximum tracing).
 *
 *
 *		Adjustable parameters
 *		at run time
 *
 * trace_mode  = 0 for no trace
 *		 1 internal trace (wrap around	 in trace buffer)
 *		 2 internal and external trace.
 */

#define		MAXLINESIZE	120
#define		TRACE_SIZE	2048
#define		RED_ZONE	20

#define	XYDEBUG
#if	defined(XYDEBUG) || defined(lint)
static	int	xy_debug = 0;
static	int	trace_mode = 1;
static	int	to_trace = 0;
static	int	itrace;
static	char	xy_trace[TRACE_SIZE + RED_ZONE];
kmutex_t	xy_trace_mutex;

#else
#define	xy_debug 0

static	void
xxprintf(char *fmt, ...)
{

}
#endif


/*
 * List of commands for the 450/451.  Used to print nice error messages.
 */
#define	XYCMDS	(sizeof (xycmdnames) / sizeof (xycmdnames[0]))
static char *xycmdnames[] = {
	"nop",
	"write",
	"read",
	"write headers",
	"read headers",
	"seek",
	"drive reset",
	"format",
	"read all",
	"read status",
	"write all",
	"set drive size",
	"self test",
	"reserved",
	"buffer load",
	"buffer dump",
};

static u_char xythrottle = (u_char) XY_THROTTLE; /* transfer burst count */

/*
 * Flags value that turns off command chaining
 */
#define	XY_NOCHAINING	0x1

/*
 * Autoconfiguration data
 */

static void *xyc_state;

static ddi_dma_lim_t xyc_lim = {
	0x0, (u_int) 0xffffffff, (u_int) 0xffffffff, 0x6, 0x2, 4096
};

static struct bus_ops xyc_bus_ops = {
	nullbusmap,
	0,		/* ddi_intrspec_t	(*bus_get_intrspec)(); */
	0,		/* int			(*bus_add_intrspec)(); */
	0,		/* void			(*bus_remove_intrspec)(); */
	0,		/* int			(*bus_map_fault)() */
	0,		/* int			(*bus_dma_map)() */
	0,		/* int			(*bus_dma_ctl)() */
	xyc_bus_ctl,
	ddi_bus_prop_op,
};

static struct dev_ops xyc_ops = {
	DEVO_REV, 		/* devo_rev, */
	0, 			/* refcnt  */
	ddi_no_info,		/* info */
	xycidentify, 		/* identify */
	xycprobe, 		/* probe */
	xycattach, 		/* attach */
	nodev, 			/* detach */
	nodev, 			/* reset */
	(struct cb_ops *)0,	/* driver operations */
	&xyc_bus_ops, 		/* bus operations */
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
	&mod_driverops, /* Type of module.  This one is a driver */
	"Xylogics 450/451 SMD Disk Controller",
	&xyc_ops, 	/* driver ops */
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

#ifdef	XYDEBUG
	if (xy_debug)
		mutex_init(&xy_trace_mutex, "xy_trace", MUTEX_DRIVER, NULL);
#endif
	if ((e = ddi_soft_state_init(&xyc_state,
	    sizeof (struct xyctlr), 1)) != 0) {
		return (e);
	}

	if ((e = mod_install(&modlinkage)) != 0) {
		ddi_soft_state_fini(&xyc_state);
	}
	return (e);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

int
_fini(void)
{
	register int e;

	if ((e = mod_remove(&modlinkage)) != 0) {
		return (e);
	}
	ddi_soft_state_fini(&xyc_state);
	return (0);
}

/*ARGSUSED*/
static int
xyc_bus_ctl(dev_info_t *d, dev_info_t *r, ddi_ctl_enum_t o, void *a, void *v)
{
	register dev_info_t *cdev = (dev_info_t *)a;
	char name[MAXNAMELEN];
	int s_len, slave;

	switch (o) {
	case DDI_CTLOPS_INITCHILD:

		s_len = sizeof (slave);
		if (ddi_prop_op(DDI_DEV_T_NONE, cdev, PROP_LEN_AND_VAL_BUF,
		    DDI_PROP_DONTPASS | DDI_PROP_CANSLEEP, "slave",
		    (caddr_t)&slave, &s_len) != DDI_SUCCESS || slave > XYUNPERC)
			return (DDI_NOT_WELL_FORMED);

		sprintf(name, "%d,0", slave);
		ddi_set_name_addr(cdev, name);
		return (DDI_SUCCESS);

	case DDI_CTLOPS_UNINITCHILD:
		ddi_set_name_addr(cdev, NULL);
		return (DDI_SUCCESS);

	case DDI_CTLOPS_REPORTDEV:

		slave = ddi_getprop(DDI_DEV_T_NONE, r, DDI_PROP_DONTPASS,
		    "slave", -1);
		cmn_err(CE_CONT, "?%s%d at xyc%d: slave %d\n",
		    ddi_get_name(r), ddi_get_instance(r),
		    ddi_get_instance(d), slave);
		return (DDI_SUCCESS);

	default:
		return (ddi_ctlops(d, r, o, a, v));
	}
}


/*
 * Hardware action routine:
 * Wait for controller csr to become valid.
 * Waits for at most 200 usec. Returns true if wait succeeded.
 */
static int
xyccsrvalid(volatile struct xydevice *xyio)
{
	register int i;

	drv_usecwait(15);
	for (i = 20; i && (xyio->xy_csr & (XY_BUSY|XY_DBLERR)); i--)
		drv_usecwait(10);
	drv_usecwait(15);
	return ((xyio->xy_csr & (XY_BUSY|XY_DBLERR)) == 0);
}


/*
 * Hardware action routine:
 * Wait for controller become ready. Used after reset or interrupt.
 * Waits for at most .1 sec. Returns true if wait succeeded.
 * A reset should never take more than .1 sec
 */
static int
xycwait(volatile struct xydevice *xyio)
{
	register int i;

	drv_usecwait(15);
	for (i = 10000; i && (xyio->xy_csr & XY_BUSY); i--)
		drv_usecwait(10);
	drv_usecwait(15);
	return ((xyio->xy_csr & XY_BUSY) == 0);
}

/*
 * Wait for controller to become ready after an interrupt
 */
static int
xycintwait(volatile struct xydevice *xyio)
{
	register int i;

	for (i = 20; i && (xyio->xy_csr & (XY_BUSY | XY_INTR)); i--) {
		drv_usecwait(10);
	}
	drv_usecwait(10);
	return ((xyio->xy_csr & (XY_BUSY | XY_INTR)) == 0);
}

/*
 * Hardware action routine:
 * Wait for controller become ready. Used before sending new command.
 */
static int
xycfree(volatile struct xydevice *xyio)
{
	register int i, j;

	drv_usecwait(15);
	for (j = 3; j && (xyio->xy_csr & XY_BUSY); j--) {

		for (i = 1000; i && (xyio->xy_csr & XY_BUSY); i--) {
			drv_usecwait(10);
		}
		if (xyio->xy_csr & XY_BUSY) {
			cmn_err(CE_WARN, "XY_BUSY exceeds 10ms");
		}
	}
	drv_usecwait(15);

	return ((xyio->xy_csr & XY_BUSY) == 0);
}


#ifdef	XYC_RESET
/*
 * Hardware action routine: XY RESET
 */
static void
xyc_reset(struct xycmdblock *cbi)
{
	volatile struct xydevice *xyio = cbi->c->c_io;
	int ctlr, junk;

	junk = xyio->xy_resupd;

	ctlr = ddi_get_instance(cbi->c->c_dip);

	if (!xycwait(xyio)) {
		cmn_err(CE_PANIC,
		    "xyc%d: controller reset failed", ctlr);
		/* NOTREACHED */
	}
#ifdef	lint
	junk = junk;
#endif
}
#endif	/* XYC_RESET */

/*
 * Hardware action routine: SET GO BIT
 */
static void
xyc_set_go(struct xyiopb *xy, struct xyctlr *c)
{
	volatile struct xydevice *xyio = c->c_io;

	xyio->xy_csr = XY_GO;
	drv_usecwait(15);
	if (!(xyio->xy_csr & (XY_GO | XY_INTR | XY_DBLERR))) {
		if (xy->xy_complete)
			return;
		xyio->xy_csr = XY_GO;
		drv_usecwait(15);
		if (!(xyio->xy_csr & (XY_GO | XY_INTR | XY_DBLERR))) {
			if (xy->xy_complete)
				return;
			cmn_err(CE_PANIC,
				"xyc: csr double miscompare 0x%x",
					xyio->xy_csr);
			/* NOTREACHED */
		}
	}
}

/*
 * Hardware action routine: Writing iopb address in io registers
 * - because of the nature of xy registers, we need to check
 *   for glitches -
 */
static void
xyc_set_iopb(struct xyctlr *c, int iopbaddr)
{
	volatile struct xydevice *xyio = c->c_io;
	int ctlr;

	ctlr = ddi_get_instance(c->c_dip);

	/*
	 * Check for an already busy controller.  In the
	 * asynchronous case, this implies that something
	 * is corrupted.  In the synchronous case, we
	 * just cleared the controller state so this
	 * should never happen.
	 */
	if (xyio->xy_csr & (XY_BUSY | XY_INTR)) {
		cmn_err(CE_NOTE,
			"xyc%d: csr accessed while busy 0x%x\n",
			ctlr, xyio->xy_csr);
		/*
		 * Call new wait routine
		 */
		(void) xycintwait(xyio);

		/*
		 * If we're still busy, we're in trouble
		 */
		if (xyio->xy_csr & (XY_BUSY | XY_INTR)) {
			cmn_err(CE_PANIC,
				"xyc%d: csr accessed while busy 0x%x\n",
				ctlr, xyio->xy_csr);
		}
	}

	/*
	 * Set the iopb address registers, checking for glitches
	 */
	xyio->xy_iopboff[0] = iopbaddr >> 8;

	drv_usecwait(15);

	if (xyio->xy_iopboff[0] != (iopbaddr >> 8)) {
		xyio->xy_iopboff[0] = iopbaddr >> 8;
		drv_usecwait(15);
		if (xyio->xy_iopboff[0] != (iopbaddr >> 8)) {
			cmn_err(CE_PANIC,
			    "xyc%d: iopboff[0] double error", ctlr);
			/* NOTREACHED */
		}
	}

	xyio->xy_iopboff[1] = iopbaddr & 0xff;
	drv_usecwait(15);
	if (xyio->xy_iopboff[1] != (iopbaddr & 0xff)) {
		xyio->xy_iopboff[1] = iopbaddr & 0xff;
		drv_usecwait(15);
		if (xyio->xy_iopboff[1] != (iopbaddr & 0xff)) {
			cmn_err(CE_PANIC,
			    "xyc%d: iopboff[0] double error", ctlr);
			/* NOTREACHED */
		}
	}
}

/*
 * Autoconfiguration Routines
 */

static int
xycidentify(dev_info_t *dev)
{
	char *name = ddi_get_name(dev);

	/*
	 * This module drives only "xyc"
	 */

	if (strcmp(name, "xyc") == 0) {
		return (DDI_IDENTIFIED);
	} else {
		return (DDI_NOT_IDENTIFIED);
	}
}

static int
xycprobe(dev_info_t *dev)
{
	auto char local[8];
	auto volatile struct xydevice *reg;
	register struct xyctlr *c;
	register struct xycmdblock *xycbi = (struct xycmdblock *)0;
	register instance = ddi_get_instance(dev);
	u_char err;

	PRINTF3("\n...xy_ctlr_probe  V E R  c");

	/*
	 * Since we know that some instantiations of this device can
	 * be plugged into slave-only VME slots, check to see whether
	 * this is one such.
	 */
	if (ddi_slaveonly(dev) == DDI_SUCCESS) {
		return (DDI_PROBE_FAILURE);
	}

	(void) sprintf(local, "xyc%d", instance);
	if (ddi_soft_state_zalloc(xyc_state, instance) != DDI_SUCCESS) {
		cmn_err(CE_WARN, "%s: cannot allocate softstate", local);
		return (DDI_PROBE_FAILURE);
	}
	c = ddi_get_soft_state(xyc_state, instance);
	ASSERT(c);
	reg = (struct xydevice *)0;

	/*
	 * Aways add in the device's interrupt first
	 */
	if (ddi_add_intr(dev, 0, &c->c_ibc, &c->c_idc, xycintr, (caddr_t)c)) {
		cmn_err(CE_WARN, "%s: cannot add interrupt", local);
		goto failure;
	}

	mutex_init(&c->c_mutex, "xyctlr", MUTEX_DRIVER, (void *)c->c_ibc);
	cv_init(&c->c_iopbcvp, "xyiobp", CV_DRIVER, (void *)c->c_ibc);

	/*
	 * Map in device registers
	 */

	if (ddi_map_regs(dev, (u_int)0, (caddr_t *)&reg, (off_t)0, XYHWSIZE)) {
		cmn_err(CE_WARN, "%s: unable to map registers", local);
		goto failure;
	}

	/*
	 * See if there's hardware present by trying to reset it.
	 */

	mutex_enter(&c->c_mutex);
	if (ddi_peekc(dev, (char *)&reg->xy_resupd, (char *)0)
	    != DDI_SUCCESS) {	/* XXX HR modified  */
		mutex_exit(&c->c_mutex);
		goto failure;
	}

	/*
	 * Give up to .1 sec for the controller to reset itself
	 */
	(void) xycwait(reg);

	/*
	 * A reset should never take more than .1 sec.
	 */
	if (reg->xy_csr & XY_BUSY) {
		mutex_exit(&c->c_mutex);
		cmn_err(CE_WARN, "%s: controller reset failed", local);
		goto failure;
	}
	mutex_exit(&c->c_mutex);

	c->c_dip = dev;
	c->c_io = reg;
	c->c_start = xycstart;
	c->c_getcbi = xycgetcbi;
	c->c_putcbi = xycputcbi;
	c->c_cmd = xyccmd;
	c->c_lim = &xyc_lim;

	PRINTF3("\t\t======= > dev=0x%x reg=0x%x ", dev, reg);

	/*
	 * Allocate iopbs and command blocks now
	 * (for probing purposes).
	 */
	if (xyccreatecbis(c, NCBIFREE) == DDI_FAILURE) {
		goto failure;
	}

	/*
	 * Read the controller parameters to make sure it's a 450/451.
	 */
	if ((xycbi = xycgetcbi(c, 1, XY_SYNCH)) == NULL) {
		goto failure;
	}

	PRINTF3("\t\txycbi	=0x%x \n", xycbi);

	mutex_enter(&c->c_mutex);
	err = xyccmd(xycbi, XY_NOP, NOLPART,
	    0, 0, 0, 0, XY_SYNCH, 0);
	mutex_exit(&c->c_mutex);
	if (err) {
		cmn_err(CE_WARN, "%s: unable to Read controller paramters",
		    local);
		goto failure;
	} else if (xycbi->iopb->xy_ctype != XYC_450) {
		cmn_err(CE_WARN, "%s: unsupported controller type 0x%x\n",
		    local, xycbi->iopb->xy_ctype);
		goto failure;
	}

	/*
	 * Run the controller self tests.
	 */

	mutex_enter(&c->c_mutex);
	err = xyccmd(xycbi, XY_TEST, NOLPART, 0, 0, 0, 0, XY_SYNCH, 0);
	mutex_exit(&c->c_mutex);

	xycputcbi(xycbi);

	if (err) {
		cmn_err(CE_WARN, "%s: self test error", local);
		goto failure;
	}

	/*
	 * probe is stateless (probe(9E))
	 */
	ddi_unmap_regs(dev, 0, (caddr_t *)&reg, (off_t)0, (off_t)0);
	cv_destroy(&c->c_iopbcvp);
	mutex_destroy(&c->c_mutex);
	ddi_remove_intr(dev, 0, c->c_ibc);
	(void) xyccreatecbis(c, 0);
	ddi_soft_state_free(xyc_state, instance);
	ddi_set_driver_private(dev, (caddr_t)0);
	return (DDI_PROBE_SUCCESS);

failure:

	if (xycbi) {
		xycputcbi(xycbi);
	}
	if (reg) {
		ddi_unmap_regs(dev, 0, (caddr_t *)&reg, (off_t)0, (off_t)0);
	}
	if (c) {
		if (c->c_ibc) {
			mutex_destroy(&c->c_mutex);
			ddi_remove_intr(dev, 0, c->c_ibc);
		}
		if (c->c_nfree)
			(void) xyccreatecbis(c, 0);
		ddi_soft_state_free(xyc_state, instance);
	}
	return (DDI_PROBE_FAILURE);
}

static int
xycattach(dev_info_t *dev, ddi_attach_cmd_t cmd)
{
	auto char local[8];
	auto volatile struct xydevice *reg;
	register struct xyctlr *c;
	register struct xycmdblock *xycbi = (struct xycmdblock *)0;
	register instance = ddi_get_instance(dev);
	u_char err;

	PRINTF3("\n...xycattach  ");
	switch (cmd) {

	case DDI_ATTACH:

		/*
		 * We do know we've successfully probed the device.
		 */
		(void) sprintf(local, "xyc%d", instance);
		if (ddi_soft_state_zalloc(xyc_state, instance) != DDI_SUCCESS) {
			cmn_err(CE_WARN, "%s: cannot allocate softstate",
			    local);
			return (DDI_PROBE_FAILURE);
		}
		c = ddi_get_soft_state(xyc_state, instance);
		ASSERT(c);
		reg = (struct xydevice *)0;

		/*
		 * Aways add in the device's interrupt first
		 */
		if (ddi_add_intr(dev, 0, &c->c_ibc, &c->c_idc, xycintr,
		    (caddr_t)c)) {
			cmn_err(CE_WARN, "%s: cannot add interrupt", local);
			goto failure;
		}

		mutex_init(&c->c_mutex, "xyctlr", MUTEX_DRIVER,
			(void *)c->c_ibc);
		cv_init(&c->c_iopbcvp, "xyiobp", CV_DRIVER, (void *)c->c_ibc);

		/*
		 * Map in device registers
		 */

		if (ddi_map_regs(dev, (u_int)0, (caddr_t *)&reg, (off_t)0,
		    XYHWSIZE)) {
			cmn_err(CE_WARN, "%s: unable to map registers", local);
			goto failure;
		}

		c->c_dip = dev;
		c->c_io = reg;
		c->c_start = xycstart;
		c->c_getcbi = xycgetcbi;
		c->c_putcbi = xycputcbi;
		c->c_cmd = xyccmd;
		c->c_lim = &xyc_lim;

		PRINTF3("\t\t======= > dev= 0x%x reg=0x%x ", dev, reg);

		/*
		 * Allocate iopbs and command blocks now
		 */
		if (xyccreatecbis(c, NCBIFREE) == DDI_FAILURE) {
			goto failure;
		}

		/*
		 * Run the controller self tests.
		 */
		if ((xycbi = xycgetcbi(c, 1, XY_SYNCH)) == NULL) {
			goto failure;
		}

		mutex_enter(&c->c_mutex);
		err = xyccmd(xycbi, XY_TEST, NOLPART, 0, 0, 0, 0, XY_SYNCH, 0);
		mutex_exit(&c->c_mutex);

		xycputcbi(xycbi);

		if (err) {
			cmn_err(CE_WARN, "%s: self test error", local);
			goto failure;
		}
		mutex_enter(&c->c_mutex);
		if ((c->c_intrstats = kstat_create("xyc", instance, local,
		    "controller", KSTAT_TYPE_INTR, 1,
		    KSTAT_FLAG_PERSISTENT)) != NULL) {
			kstat_install(c->c_intrstats);
		}
		if (reg->xy_csr & XY_ADDR24)
			c->c_flags |= XY_C_24BIT;
		c->c_flags |= XY_C_PRESENT;
		c->rdyiopbq = NULL;
		mutex_exit(&c->c_mutex);
		ddi_set_driver_private(dev, (caddr_t)c);
		ddi_report_dev(dev);
		return (DDI_SUCCESS);

failure:

		if (xycbi) {
			xycputcbi(xycbi);
		}
		if (reg) {
			ddi_unmap_regs(dev, 0, (caddr_t *)&reg, (off_t)0,
			    (off_t)0);
		}
		if (c) {
			if (c->c_ibc) {
				mutex_destroy(&c->c_mutex);
				ddi_remove_intr(dev, 0, c->c_ibc);
			}
			if (c->c_nfree)
				(void) xyccreatecbis(c, 0);
			ddi_soft_state_free(xyc_state, instance);
		}
		ddi_set_driver_private(dev, (caddr_t)0);
		return (DDI_FAILURE);
	default:
		return (DDI_FAILURE);
	}
}

/*
 * Called from xystrategy and xycintr to run the buf queue.
 */

static int
xycstart(caddr_t arg)
{
	register struct xyctlr *c = (struct xyctlr *)arg;
	register struct xyunit *un;
	register struct xycmdblock *xycbi;
	register struct buf *bp;
	ddi_dma_handle_t handle;
	register int err;

	PRINTF3("\n...xycstart xyctlr=0x%x ", c);
	/*
	 * While we still have IOPBs, try and get some command started.
	 *
	 * XXX mj: We need to do a better job of round-robining the
	 * XXX mj: requests to the disks.
	 */
	mutex_enter(&c->c_mutex);
	while ((bp = c->c_waitqf) != NULL) {
		mutex_exit(&c->c_mutex);
		if (!(xycbi = xycgetcbi(c, 0, XY_ASYNCH))) {
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
			xycputcbi(xycbi);
			return (0);
		}
		handle = (ddi_dma_handle_t)0;
		un = xycunit(c->c_units, INSTANCE(bp->b_edev));
		err = ddi_dma_buf_setup(c->c_dip, bp,
		    (bp->b_flags & B_READ)? DDI_DMA_READ:
		    DDI_DMA_WRITE, xycstart, (caddr_t)c,
		    c->c_lim, &handle);
		if (err ==  DDI_DMA_NORESOURCES) {
			mutex_exit(&c->c_mutex);
			/*
			 * We'll be called back later
			 */
			xycputcbi(xycbi);
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
			xycputcbi(xycbi);
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
		xycbi->un = un;
		xycbi->breq = bp;
		xycbi->handle = handle;
		XYGO(xycbi);
	}
	mutex_exit(&c->c_mutex);
	return (1);
}

/*
 * This routine is the focal point of all commands to the controller.
 * Every command passes through here, independent of its source or
 * reason.  The mode determines whether we are synchronous, asynchronous,
 * or asynchronous but waiting for completion.	The flags are used to
 * suppress error recovery and messages when we are doing special operations.
 *
 * It is used by xyprobe(), findslave(), doattach(), usegeom(),
 * XYGO(), xyioctl(), xywatch(), and xydump().
 *
 * It is always called at disk interrupt priority.
 *
 * NOTE: this routine assumes that all operations done before the disk's
 * geometry is defined are done on block 0.  This impacts both this routine
 * and the error recovery scheme (even the restores must use block 0).
 * Failure to abide by this restriction could result in an arithmetic trap.
 *
 * Returns -1 if fail and 0 if succeeds
 *
 */

static int
xyccmd(register struct xycmdblock *cmdblk, u_short cmd, int device,
	ddi_dma_handle_t handle, int unit, daddr_t blkno, int secnt,
	int mode, int flags)
{
	register struct xyiopb *xy = cmdblk->iopb;
	register struct xyctlr *c = cmdblk->c;
	register struct xyunit *un = c->c_units[unit];
	int stat = 0;

#ifdef xy_protect_label

	/*
	 * temporary protection of label
	 */
	if ((cmd == XY_WRITE) && (blkno == 0)) {
		cmn_err(CE_WARN, "\n\t\t>>>refusing to update label\n");
		return (-1);
	}
#endif

	/*
	 * Fill in the cmdblock fields.
	 */
	if (mode == XY_ASYNCHWAIT) {
		flags |= XY_WAIT;
	}
	cmdblk->flags = flags;

	cmdblk->retries = cmdblk->restores = cmdblk->resets = 0;
	cmdblk->slave = (u_char)unit;
	cmdblk->cmd = cmd;
	cmdblk->device = device;
	cmdblk->blkno = blkno;
	cmdblk->boff = 0;
	ASSERT(secnt < 0x10000);
	cmdblk->nsect = (u_short) secnt;
	cmdblk->failed = 0;
	/* XXX HR recover DOES check for cmdblk->handle for xy_bufoff */
	cmdblk->handle = handle;

	xyprint++;

	if (xy_debug > 1 || (xyprint & 0x1ff) == 0x1ff) {
		PRINTF3(
		    "xyc%d: C M D 0x%x hl 0x%x, un %d blkno 0x%x secnt 0x%x\n",
		    ddi_get_instance(c->c_dip), cmd, (int)handle, unit,
		    (int)blkno, secnt);
#if	defined(LOOK_QUEUE) || defined(lint)
		look_queue(c);
#endif
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

	cleariopb(xy);

	/*
	 * Set the iopb fields that are the same for all commands.
	 */

	xy->xy_cmd = cmd >> 8;
	xy->xy_subfunc = cmd;
	xy->xy_unit = (u_char) unit;

	if (un != NULL)
		xy->xy_drive = un->un_drtype;
	else
		xy->xy_drive = 0;

	xy->xy_chain = 0;

	/*
	 * If the blockno is 0, we don't bother calculating the disk
	 * address.  NOTE: this is a necessary test, since some of the
	 * operations on block 0 are done while un is not yet defined.
	 * Removing the test would cause bad pointer references.
	 */
	if (blkno != 0) {
		xy->xy_cylinder = blkno /
		    ((daddr_t)(un->un_g.dkg_nhead * un->un_g.dkg_nsect));
		xy->xy_head = (blkno / (daddr_t)un->un_g.dkg_nsect) %
		    (daddr_t)un->un_g.dkg_nhead;
		xy->xy_sector = blkno % (daddr_t)un->un_g.dkg_nsect;
	} else
		xy->xy_cylinder = xy->xy_head = xy->xy_sector = 0;
	ASSERT(secnt < 0x10000);
	xy->xy_nsect = (u_short) secnt;

	switch (cmd) {
	/* XXX HR double check these DO not need to DMA data */
	case XY_NOP:
	case XY_SEEK:
	case XY_RESTORE:
	case XY_FORMAT:
	case XY_STATUS:
	case XY_TEST:
		break;

	case XY_INIT:
		/*
		 * If we are doing a set controller params command,
		 * we need to hack fields of the iopb.
		 */
		xy->xy_throttle = xythrottle;

		/*
		 * 4 is the known default for all extant
		 * SVr4/5.0 platforms.
		 */

		break;


	/* XXX HR double check these that need to DMA data */
	case XY_WRITE:
	case XY_READ:
	case XY_WRITEHDR:
	case XY_READHDR:
	case XY_READALL:
	case XY_READALL | XY_DEFLST:
	case XY_WRITEALL:
	case XY_BUFLOAD:
	case XY_BUFDUMP:
	{
		ddi_dma_cookie_t cookie;
		if (ddi_dma_htoc(handle, cmdblk->boff, &cookie)) {
			cmn_err(CE_PANIC, "xyccmd: ddi_dma_htoc fails");
			/* NOTREACHED */
		}

		xy->xy_bufoff = XYOFF(cookie.dmac_address);
		xy->xy_bufrel = XYNEWREL(c->c_flags, cookie.dmac_address);


		if (xy_debug > 1 || (xy_debug && mode == XY_SYNCH) ||
		    (xy_debug && un && cmdblk->breq == un->un_sbufp)) {
			PRINTF3(
			    "xyc%d %s %d sec slave %d %d/%d/%d %s 0x%x.0x%x\n",
			    ddi_get_instance(c->c_dip),
			    (cmd == XY_READ)? "read" : "write",
			    secnt, unit, xy->xy_cylinder, xy->xy_head,
			    xy->xy_sector, (cmd == XY_READ)? "to" : "from",
			    xy->xy_bufrel, xy->xy_bufoff);
		}
		break;
	}
	default:
		if (mode != XY_SYNCH) {
			cmn_err(CE_PANIC, "xyccmd: unkown cmd 0x%x\n", cmd);
			/* NOTREACHED */
		} else {
			return (-1);
		}
		break;
	}

	/*
	 * If command is synchronous, execute it.  We continue to call
	 * error recovery (which will continue to execute commands) until
	 * it return s either success or complete failure.
	 */
	if (mode == XY_SYNCH) {
		xycsynch(cmdblk);
		while ((stat = xycrecover(cmdblk, xycsynch)) > 0)
			;
		if (xy_debug) {
			cmn_err(CE_CONT,
			    "xyc%d: xycrecover of xycsynch return s %d\n",
			    ddi_get_instance(c->c_dip), stat);
		}
		return (stat);
	}

	/*
	 * If command is asynchronous, set up it's execution.  We only
	 * start the execution if the controller is in a state where it
	 * can accept another command via xycexec().
	 */

	xycasynch(cmdblk);

	xycexec(cmdblk);

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
 * the 451 will handle only one command at a time
 * since it does not have an internal FIFO to
 * store IOPB addresses.
 * The 451 driver does the same thing as the 7053 driver
 * (xd), but does not write an IOPB address without
 * making sure the previous interrupt has been serviced.
 * see flag c_wantint, cv_wait().
 * THe following comment applies to the 7053.
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
 * through xycintr(). xycintr() could be taught
 * to distinguish synchronous commands from
 * other commands, but it gets hairy in trying
 * to get back here. Instead of doing that, we'll
 * store up commands that complete here that aren't
 * ours and when ours completes, we'll call xycsvc()
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
xycsynch(register struct xycmdblock *cmdblk)
{
	register struct xycmdblock *pending = (struct xycmdblock *)0;
	register struct xyiopb *iopb, *xy_iopb;
	register volatile struct xydevice *c_io = cmdblk->c->c_io;
	register struct xyctlr *c = cmdblk->c;
	auto ddi_dma_cookie_t ck;
	auto off_t offset;
	volatile u_long getiopb;
	register char junk;
	register int usec = 0;
	register struct xyiopb *riopb;
	PRINTF3("\n...xycsynch  ");

	/*
	 * If any request or controller interrupt is pending, wait for
	 * a while to get these conditions cleared.
	 */
	while (c->c_wantint == 1 && usec < 2000000) {
		c->c_wantsynch = 1;
		mutex_exit(&c->c_mutex);
		drv_usecwait(100000);
		usec += 100000;
		mutex_enter(&c->c_mutex);
		c->c_wantsynch = 0;
	}
	if (c->c_wantint == 1) {
		cmn_err(CE_WARN, "xyc%d: xycsynch - missing interrupt",
		ddi_get_instance(c->c_dip));
		c->c_wantint = 0;
	}

	/*
	 * Clean out any old state left in the controller.
	 */

	if (c_io->xy_csr & XY_BUSY) {
		junk = c_io->xy_resupd;
		if (!xycwait(c_io)) {
			cmn_err(CE_PANIC, "xyc%d: xycsynch- ctlr reset failed",
			ddi_get_instance(c->c_dip));
	}
	}
	if (c_io->xy_csr & XY_INTR) {
		c_io->xy_csr = XY_INTR;
		(void) xyccsrvalid(c_io);
	}
	if (c_io->xy_csr & (XY_ERROR | XY_DBLERR)) {
		c_io->xy_csr = XY_ERROR;
		(void) xyccsrvalid(c_io);
	}
	/*
	 * Set necessary iopb fields then have the command executed.
	 */

	iopb = cmdblk->iopb;

	iopb->xy_ie = 0;	/* no interrupt (int enable OFF) */
	iopb->xy_chain = 0;

	/* XXX HR rm iopb->xy_intvec = 0; */
	/* iopb->xy_intpri = 0; no interrupt */

	/* xycexec(cmdblk); */

	/*
	 * Changed xycexec to xycpushiopb. We may jump the c->rdyiopbq here,
	 * but it is OK for the synchronous commands.
	 */
	xycpushiopb(iopb, c);

again:

	/*
	 * Wait for the command to complete or until a timeout occurs.
	 */

	/* XXX HR replace with delay stuff !! */
	/* XYCDELAY((c_io->xy_csr & XY_INTR), 1000000 * XYLOSTINTTIMO); */
	/* xycwait(c_io); */

	XYCDELAY(((c_io->xy_csr & XY_BUSY) == 0), 1000000 * XYLOSTINTTIMO);

	/*
	 * If we timed out, use the lost interrupt error to pass
	 * back status or just reset the controller if the command
	 * had already finished.
	 */
	if (c_io->xy_csr & XY_BUSY) {
		if (iopb->xy_complete) {
			junk = c_io->xy_resupd;
			if (!xycwait(c_io)) {
				mutex_exit(&c->c_mutex);
				cmn_err(CE_PANIC,
				    "xyc%d: xycsynch- ctlr reset failed",
				    ddi_get_instance(c->c_dip));
			}
		} else {
			iopb->xy_iserr = 1;
			iopb->xy_errno = XYE_LINT;
		}
	/*
	 * If one of the error bits is set in the controller status
	 * register, we need to convey this to the recovery routine
	 * via the iopb.  However, if the iopb has a specific error
	 * reported, we don't want to overwrite it.  Therefore, we
	 * only fold the error bits into the iopb if the iopb thinks
	 * things were ok.
	 */
	} else if (c_io->xy_csr & XY_DBLERR) {
		c_io->xy_csr = XY_ERROR;	/* clears the error */
		(void) xyccsrvalid(c_io);
		if (!(iopb->xy_iserr && iopb->xy_errno)) {
			iopb->xy_iserr = 1;
			iopb->xy_errno = XYE_DERR;
		}
	} else if (c_io->xy_csr & XY_ERROR) {
		drv_usecwait(10);
		c_io->xy_csr = XY_ERROR;	/* clears the error */
		(void) xyccsrvalid(c_io);
		if (!(iopb->xy_iserr && iopb->xy_errno)) {
			iopb->xy_iserr = 1;
			iopb->xy_errno = XYE_ERR;
		}
	}


	/*
	 * Get iopb address  XXX HR added
	 * XXX HR we may have to check for CSR valid here
	 */

	getiopb = c_io->xy_iopboff[0] << 8;
	drv_usecwait(15);
	getiopb = getiopb | c_io->xy_iopboff[1];
	drv_usecwait(15);
	ck.dmac_address = (u_long) getiopb;


	/*
	 * Figure out which iopb completed
	 */

	if (ddi_dma_coff(c->c_ihandle, &ck, &offset)) {
		cmn_err(CE_PANIC, "xyc%d: xycsynch- ddi_dma_coff fails (0x%x)",
		    ddi_get_instance(c->c_dip), (int)&ck);
		/* NOTREACHED */
	}
	xy_iopb = (struct xyiopb *)((u_long)c->c_iopbbase + (u_long)offset);

	/*
	 * DDI required synchronization.
	 */

	(void) ddi_dma_sync(c->c_ihandle, offset, sizeof (struct xyiopb),
	    DDI_DMA_SYNC_FORCPU);

	/*
	 * Allow the controller to proceed
	 */

	(void) xyccsrvalid(c_io);
	c_io->xy_csr = XY_INTR; /* XXX HR replacing CLRIO */
	drv_usecwait(15); /* XXX HR needed ? */
	(void) xyccsrvalid(c_io);

	/*
	 * Now see if the completed command is truly ours.
	 */
	if (iopb != xy_iopb) {
		int idx = (int)((u_int)offset / (u_int)c->c_iopbsize);
		register struct xycmdblock *thiscmd = &c->c_cmdbase[idx];

		/*
		 * It isn't. save it up to be completed later.
		 */
		thiscmd->xycmd_next = pending;
		pending = thiscmd;
		goto again;
	}
	/*
	 * Our command is (finally) done.
	 * Complete any other commands that
	 * finished while we were here.
	 */

	while ((cmdblk = pending) != (struct xycmdblock *)0) {
		pending = pending->xycmd_next;
		cmdblk->xycmd_next = 0;
		mutex_exit(&c->c_mutex);
		xycsvc(c, cmdblk);
		mutex_enter(&c->c_mutex);
	}
	while (!(c->c_io->xy_csr & XY_BUSY) && c->rdyiopbq) {
		/*
		 * Do not start new requests if we are waiting for
		 * controller interrupt for an earlier request or
		 * if we want to do a synchronous cmd.
		 */
		if (c->c_wantint == 1 || c->c_wantsynch == 1)
			break;
		/*
		 * pull an iopb off the queue and go
		 */
		riopb = c->rdyiopbq;
		/*
		 * xy_nxtaddr is in terms of what the xy sees,
		 * but we never allow the xy to use this (we do not
		 * support iopb chaining), so we use the field for
		 * CPU addresses only.
		 */
		c->rdyiopbq = riopb->xy_nxtaddr;
		riopb->xy_nxtaddr = NULL;
		xycpushiopb(riopb, c);
	}
#ifdef	lint
	junk = junk;
#endif
}

/*
 * This routine sets the fields in the iopb that are needed for an
 * asynchronous operation.  It does not start the operation.
 * It is used by xyccmd() and xycintr().
 */
static void
xycasynch(register struct xycmdblock *cmdblk)
{
	register struct xyiopb *xy = cmdblk->iopb;

	PRINTF3("\n...A S Y N C H ");
	xy->xy_ie = 1;
	xy->xy_intrall = 0;

	/* XXX HR xy->xy_intvec = cmdblk->c->c_idc.idev_vector; */
	/* xy->xy_intpri = cmdblk->c->c_idc.idev_priority; */
}

/*
 * This routine is the actual interface to the controller registers.
 * It starts the controller up on the iopb passed.
 *
 * Callers guarantee the appropriate blocking has been done.
 */

static void
xycexec(register struct xycmdblock *xycbi)
{
	register struct xyctlr *c = xycbi->c;
	register volatile struct xydevice *xyio = c->c_io;
	register struct xyiopb *riopb;

	PRINTF3("\n...xycexec  ");
	if (c->rdyiopbq == NULL) {
		if ((xyio->xy_csr & XY_BUSY) == 0) {
			/*
			 * Do not start new requests if we are waiting for
			 * controller interrupt for an earlier request or
			 * if we want to do a synchronous cmd.
			 */
			if (c->c_wantint == 0 && c->c_wantsynch == 0) {
				xycpushiopb(xycbi->iopb, c);
				return;
			}
		}
	}

	/*
	 * If we're here, then either there is something on the
	 * iopb ready queue or the controller isn't ready so
	 * first put this iopb on the queue.
	 */
	if (c->rdyiopbq == NULL) {
		c->rdyiopbq = xycbi->iopb;
	} else {
		c->lrdy->xy_nxtaddr = xycbi->iopb;
	}
	xycbi->iopb->xy_nxtaddr = NULL;
	c->lrdy = xycbi->iopb;

	/*
	 * now pull iopbs off the ready queue as long as the controller is
	 * ready to accept them.
	 */

		/* XXX HR _AIOP replaced with _BUSY */
	while (!(xyio->xy_csr & XY_BUSY) && c->rdyiopbq) {
		/*
		 * Do not start new requests if we are waiting for
		 * controller interrupt for an earlier request or
		 * if we want to do a synchronous cmd.
		 */
		if (c->c_wantint == 1 || c->c_wantsynch == 1)
			break;
		riopb = c->rdyiopbq;
		/*
		 * xy_nxtaddr is beyond what the xy controller sees
		 * xy_nxtaddr is for CPU addresses only.
		 * when there is chaining, xy_nxtoff is set for the
		 * controller (along with xy_chain = 1)
		 */
		c->rdyiopbq = riopb->xy_nxtaddr;
		riopb->xy_nxtaddr = NULL;
		xycpushiopb(riopb, c);
	}
}

static void
xycpushiopb(register struct xyiopb *xy, register struct xyctlr *c)
{
	register volatile struct xydevice *xyio = c->c_io;
	ddi_dma_cookie_t iopbaddr;
	register off_t offset;

	/* PRINTF3("\n...xycpushiopb  "); */
	/*
	 * Calculate the address of the iopb.
	 */
	offset = (off_t)((u_long)xy - (u_long)c->c_iopbbase);
	if (ddi_dma_htoc(c->c_ihandle, offset, &iopbaddr)) {
		cmn_err(CE_PANIC, "xyc%d: xycpushiopb: ddi_dma_htoc failed",
		    ddi_get_instance(c->c_dip));
		/* NOTREACHED */
	}

	/*
	 * Okay, flush it (with respect to the device)
	 */
	(void) ddi_dma_sync(c->c_ihandle, offset, sizeof (*xy),
	    DDI_DMA_SYNC_FORDEV);

	/*
	 * Make sure the controller is free before touching
	 * the io registers.
	 */
	while (c->c_wantint == 1) {
		cv_wait(&c->c_cw, &c->c_mutex);
	}
	c->c_wantint = xy->xy_ie;

	/*
	 * Set the iopb address registers
	 */
	xyc_set_iopb(c, iopbaddr.dmac_address);

	/* XXX HR added : wait for device to be free */
	(void) xycfree(xyio);

	TRACE_XY xycdumpiopb(xy);

	/*
	 * Set the go bit.
	 */
	xyc_set_go(xy, c);
}

/*
 * This routine provides the error recovery for all commands to the 451.
 * It examines the results of a just-executed command, and performs the
 * appropriate action.	It will set up at most one followup command, so
 * it needs to be called repeatedly until the error is resolved.  It
 * return s three possible values to the calling routine : 0 implies that
 * the command succeeded, 1 implies that recovery was initiated but not
 * yet finished, and -1 implies that the command failed.  By passing the
 * address of the execution function in as a parameter, the routine is
 * completely isolated from the differences between synchronous and
 * asynchronous commands.  It is used by xyccmd() and ndintr().	It is
 * always called at disk interrupt priority.
 */
static int
xycrecover(register struct xycmdblock *cmdblk, register void (*execptr)())
{
	struct xyctlr *c = cmdblk->c;
	register struct xyiopb *xy = cmdblk->iopb;
	register struct xyunit *un = c->c_units[cmdblk->slave];
	struct xyerract *actptr;
	struct xyerror *errptr;
	int bn, ns, ndone;
	char *emsgfmt = "\tcmdblk=0x%x, errno=%d, errlevel=%d, errtype=%d\n";

	PRINTF3("\n...xycrecover	 ");
	/*
	 * This tests whether an error occured.	 NOTE: errors reported by
	 * the status register of the controller must be folded into the
	 * iopb before this routine is called or they will not be noticed.
	 */
	if (xy->xy_iserr) {
		errptr = finderr(xy->xy_errno);
		if (xy_debug) {
			cmn_err(CE_CONT,
			    "xyc%d: xycrecover handling error: %s\n",
			    ddi_get_instance(c->c_dip), errptr->errmsg);
			cmn_err(CE_CONT, emsgfmt, cmdblk, errptr->errno,
			    errptr->errlevel, errptr->errtype);
		}
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
			bn = ((xy->xy_cylinder * un->un_g.dkg_nhead) +
			    xy->xy_head) * un->un_g.dkg_nsect + xy->xy_sector;
			ndone = bn - cmdblk->blkno;
			/*
			 * Log the error for diagnostics if appropriate.
			 */
			if (cmdblk->flags & XY_DIAG) {
				un->un_errsect = bn;
				un->un_errno = errptr->errno;
				un->un_errcmd =
				    (xy->xy_cmd << 8) | xy->xy_subfunc;
			}
		} else
			bn = ndone = 0;
		if (errptr->errlevel != XY_ERCOR) {
			/*
			 * If the error wasn't corrected, see if it's a
			 * bad block.  If we are already in the middle of
			 * forwarding a bad block, we are not allowed to
			 * encounter another one.  NOTE: the check of the
			 * command is to avoid false mappings during initial
			 * stuff like trying to reset a drive
			 * (the bad map hasn't been initialized).
			 */
			if (((xy->xy_cmd == (XY_READ >> 8)) ||
			    (xy->xy_cmd == (XY_WRITE >> 8))) &&
			    (ns = isbad(&un->un_bad, (int)xy->xy_cylinder,
			    (int)xy->xy_head, (int)xy->xy_sector)) >= 0) {
				if (cmdblk->flags & XY_INFRD) {
					cmn_err(CE_WARN,
				    "xy%d: recursive mapping of block %d",
					    ddi_get_instance(un->un_dip),
					    bn);
					goto fail;
				}
				/*
				 * We have a bad block.	 Advance the state
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
				if ((xyerrlvl & EL_FORWARD) &&
				    (!(cmdblk->flags & XY_NOMSG))) {
					cmn_err(CE_NOTE,
				    "xy%d: forwarding %d/%d/%d to altblk %d",
					    ddi_get_instance(un->un_dip),
					    xy->xy_cylinder, xy->xy_head,
					    xy->xy_sector, ns);
				}
				ns = 1;
				/*
				 * Execute the command on the alternate block
				 */
				goto exec;
			}
			/*
			 * Error was 'real'.  Look up action to take.
			 */
			if (cmdblk->flags & XY_DIAG)
				cmdblk->failed = 1;
			actptr = &xyerracts[errptr->errlevel];
			/*
			 * Attempt to retry the entire operation if appropriate.
			 */
			if (cmdblk->retries++ < actptr->retry) {
				if ((xyerrlvl & EL_RETRY) &&
				    (!(cmdblk->flags & XY_NOMSG)) &&
				    (errptr->errlevel != XY_ERBSY))
					printerr(un, xy->xy_cmd,
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
			 * Attempt to restore the drive if appropriate.	 We
			 * set the state to 'restoring' so we know where we
			 * are.	 NOTE: there is no check for a recursive
			 * restore, since that is a non-destructive condition.
			 */
			if (cmdblk->restores++ < actptr->restore) {
				if ((xyerrlvl & EL_RESTR) &&
				    (!(cmdblk->flags & XY_NOMSG)))
					printerr(un, xy->xy_cmd, cmdblk->device,
					    "restore", errptr->errmsg, bn);
				cmdblk->retries = 0;
				bn = ns = 0;
				xy->xy_cmd = XY_RESTORE >> 8;
				xy->xy_subfunc = 0;
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
				    "xyc%d: xycrecover- fatal reset 1",
				    ddi_get_instance(cmdblk->c->c_dip));
				/* NOTREACHED */
			}

			/*
			 * Command has failed.	We either fell through to
			 * here by running out of recovery or jumped here
			 * for a good reason.
			 */
fail:
			if ((xyerrlvl & EL_FAIL) &&
			    (!(cmdblk->flags & XY_NOMSG)))
				printerr(un, xy->xy_cmd, cmdblk->device,
				    "failed", errptr->errmsg, bn);
			/*
			 * If the failure was caused by
			 * a 'drive busy' type error, the drive has probably
			 * been taken offline, so we mark it as gone.
			 */
			if ((errptr->errlevel == XY_ERBSY) &&
			    (un != NULL) && (un->un_flags & XY_UN_PRESENT)) {
				un->un_flags &= ~XY_UN_PRESENT;
				cmn_err(CE_NOTE, "xy%d: drive offline",
				    ddi_get_instance(un->un_dip));
			}
			/*
			 * If the failure implies the drive is faulted, do
			 * one final restore in hopes of clearing the fault.
			 */
			if ((errptr->errlevel == XY_ERFLT) &&
			    (!(cmdblk->flags & XY_FNLRST))) {
				bn = ns = 0;
				xy->xy_cmd = XY_RESTORE >> 8;
				xy->xy_subfunc = 0;
				cmdblk->flags &= ~(XY_INFRD | XY_INRST);
				cmdblk->flags |= XY_FNLRST;
				cmdblk->failed = 1;
				goto exec;
			}
			/*
			 * If the failure implies the controller is hung, do
			 * a controller reset in hopes of fixing its state.
			 */
			if (errptr->errlevel == XY_ERHNG) {
				/*
				 * Until we handle retrying commands
				 * blown away by a controller reset,
				 * this is a panic.
				 */
				cmn_err(CE_PANIC,
				    "xyc%d: xycrecover- fatal reset 2",
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
		if ((xyerrlvl & EL_FIXED) && (!(cmdblk->flags & XY_NOMSG)))
			printerr(un, xy->xy_cmd, cmdblk->device,
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
		xy->xy_cmd = cmdblk->cmd >> 8;
		xy->xy_subfunc = cmdblk->cmd;
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
	 * Only calculate the disk address if block isn't zero.	 This is
	 * necessary since some of the operations of block 0 occur before
	 * the disk geometry is known (could result in zero divides).
	 */
exec:
	if (bn > 0) {
		xy->xy_cylinder = bn /
		    ((daddr_t)(un->un_g.dkg_nhead * un->un_g.dkg_nsect));
		xy->xy_head = (bn / (daddr_t)un->un_g.dkg_nsect) %
		    (daddr_t)un->un_g.dkg_nhead;
		xy->xy_sector = bn % (daddr_t)un->un_g.dkg_nsect;
	} else
		xy->xy_cylinder = xy->xy_head = xy->xy_sector = 0;
	ASSERT(ns < 0x10000);
	xy->xy_nsect = (u_short) ns;
	xy->xy_unit = cmdblk->slave;
	if (cmdblk->handle) {
		ddi_dma_cookie_t cookie;
		if (ddi_dma_htoc(cmdblk->handle, cmdblk->boff, &cookie)) {
			cmn_err(CE_PANIC,
			    "xyc%d: xycrecover: ddi_dma_htoc fails on slave %d",
			    ddi_get_instance(c->c_dip), cmdblk->slave);
			/* NOTREACHED */
		}
		xy->xy_bufoff = XYOFF(cookie.dmac_address);
		xy->xy_bufrel = XYNEWREL(c->c_flags, cookie.dmac_address);
	} else {
		xy->xy_bufoff = 0;
		xy->xy_bufrel = 0;
	}

	/*
	 * Clear out the iopb fields that need it.
	 */

	cleariopb(xy);

	/*
	 * If we are doing a set controller params command, we need to
	 * hack in a field of the iopb.
	 */

	if (cmdblk->cmd == (XY_INIT))
		xy->xy_throttle = xythrottle;

	/*
	 * Execute the command and return a 'to be continued' status.
	 */
	(*execptr)(cmdblk);
	return (1);
}

/*
 * This routine searches the error structure for the specified error and
 * return s a pointer to the structure entry.  If the error is not found,
 * the last entry in the error table is return ed (this MUST be left as
 * unknown error).  It is used by xycrecover().	It is always called at
 * disk interrupt priority.
 */
static struct xyerror *
finderr(register u_char errno)
{
	register struct xyerror *errptr;

	for (errptr = xyerrors; errptr->errno != XYE_UNKN; errptr++)
		if (errptr->errno == errno)
			break;
	return (errptr);
}

/*
 * This routine prints out an error message containing as much data
 * as is known.
 */
static void
printerr(struct xyunit *un, u_char cmd, short device,
	char *action, char *msg, daddr_t bn)
{

	if (device != NOLPART) {
		cmn_err(CE_CONT, "xy%d%c: ",
		    (int)INSTANCE(device), (int)LPART(device) + 'a');
	} else if (un != NULL) {
		cmn_err(CE_CONT, "xy%d: ", ddi_get_instance(un->un_dip));
	} else {
		cmn_err(CE_CONT, "xy: ");
	}
	if (cmd < XYCMDS) {
		cmn_err(CE_CONT, "%s ", xycmdnames[cmd]);
	} else {
		cmn_err(CE_CONT, "[cmd 0x%x] ", cmd);
	}
	cmn_err(CE_CONT, "%s (%s) -- ", action, msg);
	if ((device != NOLPART) && (un != NULL)) {
		cmn_err(CE_CONT, "blk #%d, ",
		    (int)((int)bn - un->un_map[LPART(device)].dkl_cylno *
		    un->un_g.dkg_nsect * un->un_g.dkg_nhead));
	}
	cmn_err(CE_CONT, "abs blk #%d\n", (int)bn);
}

/*
 * This routine handles controller interrupts.
 */

static u_int
xycintr(caddr_t arg)
{
	register struct xyctlr *c = (struct xyctlr *)arg;
	register struct xycmdblock *thiscmd;
	auto struct xyiopb *findaddr;
	ddi_dma_cookie_t xyioa;
	off_t offset;
	volatile u_long getiopb;
	int idx;
	char *emsgfmt = "xyc%d: xycintr: error in csr (0x%x)\n%s";

	mutex_enter(&c->c_mutex);

	PRINTF3("\n...xycintr  (0x%x)", (u_int) xycintr);
	/* wait for the controller to settle down */
	(void) xyccsrvalid(c->c_io);
	/* wait for the controller to be ready */
	(void) xycfree(c->c_io);

	/*
	 * First, check for fatal error condition
	 */

	if (c->c_io->xy_csr & (XY_ERROR | XY_DBLERR)) {
		/*
		 * At this point, we clear the error and let xyrecover()
		 * attempt to resolve the problem.
		 */
		if (xy_debug)
			cmn_err(CE_CONT, emsgfmt, ddi_get_instance(c->c_dip),
			c->c_io->xy_csr, "\tletting xycrecover() handle it.\n");
		xyc_get_codes(c);
		c->c_io->xy_csr = XY_ERROR;
		(void) xyccsrvalid(c->c_io);
	}

	/*
	 * Now, check for a spurious interrupt.
	 */
	if ((c->c_io->xy_csr & XY_INTR) == 0) { /* XXX HR replaced _RIO */
		if (c->c_intrstats) {
			KIOIP->intrs[KSTAT_INTR_SPURIOUS]++;
		}
		mutex_exit(&c->c_mutex);
		(void) xyccsrvalid(c->c_io);
		return (DDI_INTR_UNCLAIMED);
	}

	if (c->c_intrstats) {
		KIOIP->intrs[KSTAT_INTR_HARD]++;
	}

	/*
	 * Mark this controller as free and wakeup process waiting
	 */
	if (c->c_wantint == 1) {
		c->c_wantint = 0;
		cv_broadcast(&c->c_cw);
	}

	/*
	 * Get iopb address  XXX HR added
	 */

	getiopb = c->c_io->xy_iopboff[0] << 8;
	drv_usecwait(15);
	getiopb = getiopb | c->c_io->xy_iopboff[1];
	drv_usecwait(15);
	xyioa.dmac_address = (u_long) getiopb;


	if (ddi_dma_coff(c->c_ihandle, &xyioa, &offset)) {
		mutex_exit(&c->c_mutex);
		cmn_err(CE_WARN, "xyc%d: xycintr- ddi_dma_coff fails on 0x%x",
		    ddi_get_instance(c->c_dip), (int)&xyioa);
		(void) xyccsrvalid(c->c_io);
		return (DDI_INTR_CLAIMED);
	}
	/*
	 * DDI required synchronization.
	 */
	(void) ddi_dma_sync(c->c_ihandle, offset, sizeof (struct xyiopb),
	    DDI_DMA_SYNC_FORCPU);

	findaddr = (struct xyiopb *)((u_long)c->c_iopbbase + (u_long) offset);

	/*
	 * Ready the controller for a new command.
	 */
	(void) xyccsrvalid(c->c_io);
	c->c_io->xy_csr = XY_INTR;

	/*
	 * Use new wait routine which waits for INTR and
	 * BUSY to be deasserted before proceeding
	 */
	(void) xyccsrvalid(c->c_io);

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
		cmn_err(CE_WARN, "xyc%d: xycintr- unmatched iopb at 0x%x",
		    ddi_get_instance(c->c_dip), (int)findaddr);
		(void) xyccsrvalid(c->c_io);
		return (DDI_INTR_CLAIMED);
	}

	/*
	 * Pass the rest of this to the common interrupt service code
	 */

	mutex_exit(&c->c_mutex);

	xycsvc(c, thiscmd);

	(void) xyccsrvalid(c->c_io);
	return (DDI_INTR_CLAIMED);

}

static void
xycsvc(register struct xyctlr *c, register struct xycmdblock *thiscmd)
{
	register int stat;
	register struct xyiopb *riopb;
	mutex_enter(&c->c_mutex);

	PRINTF3("\n...xycsvc  ");
	/*
	 * Execute the error recovery on the iopb.
	 * If stat came back greater than zero, the
	 * error recovery has re-executed the command.
	 * but it needs to be continued ...
	 */
	if ((stat = xycrecover(thiscmd, xycasynch)) > 0) {
		xycexec(thiscmd);
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
		PRINTF3("\t\t>>> svc: cv_signal 0x%x\n", thiscmd);

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
		 * someone wants the iopb, wake them up
		 * otherwise start up the next buf operation.
		 *
		 * If measuring stats, mark an exit from
		 * the busy timing and tote up the number
		 * of bytes moved.
		 */
		register struct xyunit *un = thiscmd->un;
		register struct buf *bp = thiscmd->breq;
		register ddi_dma_handle_t *handle = thiscmd->handle;

		mutex_exit(&c->c_mutex);
		xycputcbi(thiscmd);
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

		PRINTF3("\t\t>>> svc: before rdyiopbq\n");
	if (c->rdyiopbq != NULL) {
		/*
		 * need to run the ready iopb q and
		 * xycexec isn't quite right
		 * XXX mj?: FIX THIS
		 */

		/*
		 * are there any ready and waiting iopbs?
		 * XXX HR _AIOP replaced with _BUSY
		 */

		PRINTF3("\t\t>>> svc: before c_io csr\n");
		while (!(c->c_io->xy_csr & XY_BUSY) && c->rdyiopbq) {
			/*
			 * Do not start new requests if we are waiting for
			 * controller interrupt for an earlier request or
			 * if we want to do a synchronous cmd.
			 */
			if (c->c_wantint == 1 || c->c_wantsynch == 1)
				break;
			/*
			 * pull an iopb off the queue and go
			 */
			riopb = c->rdyiopbq;
			/*
			 * xy_nxtaddr is in terms of what the xy sees,
			 * but we never allow the xy to use this (we do not
			 * support iopb chaining), so we use the field for
			 * CPU addresses only.
			 */
			c->rdyiopbq = riopb->xy_nxtaddr;
			riopb->xy_nxtaddr = NULL;
			xycpushiopb(riopb, c);
		}
	}
	if (c->c_waitqf) {
		mutex_exit(&c->c_mutex);
		(void) xycstart((caddr_t)c);
	} else {
		mutex_exit(&c->c_mutex);
	}
}

/*
 * Create xy cbis and iopbs for a controller.
 *
 * Called at probe time for controller probe stuff
 * and later to increase size of iopb pool for the
 * controller. The caller guarantees that the the
 * controller is quiescent when this routine is
 * called (i.e., no need to block).
 */

static int
xyccreatecbis(register struct xyctlr *c, int ncbis)
{
	register struct xycmdblock *xycbi = (struct xycmdblock *)0;
	ddi_dma_handle_t h = (ddi_dma_handle_t)0;
	ddi_dma_cookie_t ck;
	caddr_t iopb = (caddr_t)0;
	u_int size, align, mineffect, niopb;

	PRINTF3("\n...xyccreatecbis  ");
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
			(u_int)c->c_niopbs * sizeof (struct xycmdblock));
		c->c_niopbs = 0;
		return (DDI_SUCCESS);
	}

	if (c->c_niopbs && (char)ncbis <= c->c_niopbs)
		return (DDI_SUCCESS);

	/*
	 * Do all allocation and mapping prior to releasing old resources.
	 */

	/*
	 * MJ: FIX ALLOCATOR OR FIX xyc_lim so that this jerk doesn't happen
	 */

	size = (ncbis + 1) * sizeof (struct xyiopb);
	if (ddi_iopb_alloc(c->c_dip, c->c_lim, size, &iopb)) {
		cmn_err(CE_WARN, "xyc%d: cannot allocate iopbs",
		    ddi_get_instance(c->c_dip));
		goto failure;
	}
	if (ddi_dma_addr_setup(c->c_dip, (struct as *)0, iopb, size,
	    DDI_DMA_RDWR|DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, (caddr_t)0,
	    c->c_lim, &h)) {
		cmn_err(CE_WARN, "xyc%d: cannot map iopbs",
		    ddi_get_instance(c->c_dip));
		goto failure;
	}

	if (ddi_dma_devalign(h, &align, &mineffect)) {
		cmn_err(CE_WARN, "xyc%d: cannot get iopb dma alignment",
		    ddi_get_instance(c->c_dip));
		goto failure;
	}
	align = max(align, mineffect);
	align = roundup(sizeof (struct xyiopb), align);
	if (align >= 256) {
		cmn_err(CE_WARN, "xyc%d: unmanageable iopb size: %d",
		    ddi_get_instance(c->c_dip), align);
		goto failure;
	}
	niopb = ((ncbis + 1) * sizeof (struct xyiopb))/align;
	if (niopb < ncbis) {
		cmn_err(CE_WARN, "xyc%d: iopb align truncation",
		    ddi_get_instance(c->c_dip));
		goto failure;
	}
	xycbi = kmem_zalloc(niopb * sizeof (struct xycmdblock), KM_SLEEP);

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
			(u_int)c->c_niopbs * sizeof (struct xycmdblock));
	}

	/*
	 * Install and initialize the new resources
	 */

	c->c_ihandle = h;
	c->c_nfree = c->c_niopbs = (char)niopb;
	c->c_iopbsize = (u_char) align;
	c->c_cmdbase = xycbi;
	c->c_freecmd = xycbi;
	c->c_iopbbase = iopb;

	size = 0;
	while (xycbi < &c->c_freecmd[niopb]) {
		xycbi->c = c;
		cv_init(&xycbi->cw, "xy cmd cv", CV_DRIVER, (void *)c->c_ibc);
		xycbi->xycmd_next = xycbi + 1;
		xycbi->iopb = (struct xyiopb *)&iopb[size];
		if (ddi_dma_htoc(h, (off_t)size, &ck)) {
			xycbi = c->c_cmdbase;
			cmn_err(CE_WARN, "xyc%d: bad cookie in initiopb",
			    ddi_get_instance(c->c_dip));
			c->c_ihandle = 0;
			c->c_niopbs = 0;
			goto failure;
		}
		initiopb(xycbi->iopb);
		xycbi++;
		size += align;
	}
	(--xycbi)->xycmd_next = 0;
	return (DDI_SUCCESS);

failure:

	if (xycbi)
		kmem_free(xycbi, niopb * sizeof (struct xycmdblock));

	if (h)
		ddi_dma_free(h);

	if (iopb)
		ddi_iopb_free(iopb);

	return (DDI_FAILURE);
}

/*
 * Remove xy cbi from free list and put on the active list for the controller.
 */

static struct xycmdblock *
xycgetcbi(struct xyctlr *c, int lastone, int mode)
{
	register struct xycmdblock *xycbi = NULL;

	mutex_enter(&c->c_mutex);
again:
	if ((xycbi = c->c_freecmd) != NULL) {
		if (c->c_nfree == 1 && lastone == 0) {
			if (mode == XY_ASYNCHWAIT) {
				cv_wait(&c->c_iopbcvp, &c->c_mutex);
				goto again;
			} else {
				mutex_exit(&c->c_mutex);
				return ((struct xycmdblock *)NULL);
			}
		}
		/*
		 * Remove the cbi from the free list for the controller
		 */
		c->c_freecmd = xycbi->xycmd_next;
		xycbi->xycmd_next = 0;
		xycbi->c = c;
		xycbi->breq = 0;
		xycbi->handle = 0;
		xycbi->busy = 1;
		c->c_nfree--;
	} else if (mode == XY_ASYNCHWAIT) {
		c->c_wantcmd++;
		cv_wait(&c->c_iopbcvp, &c->c_mutex);
		goto again;
	}
	mutex_exit(&c->c_mutex);
	return (xycbi);
}

/*
 * Remove xy cbi from active list for the controller and put on the free list.
 *
 * Called from xyccmd() for synchronous and asynchronous_wait commands and
 * from xycintr() for asynchronous commands.
 *
 * Caller guarantees RW access to the controller structure is blocked.
 */

static void
xycputcbi(register struct xycmdblock *xycbi)
{
	register struct xyctlr *c = xycbi->c;

	mutex_enter(&c->c_mutex);
	xycbi->busy = 0;
	xycbi->xycmd_next = c->c_freecmd;
	c->c_freecmd = xycbi;
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
/*ARGSUSED1*/
static void
initiopb(register struct xyiopb *xy)
{
	bzero((caddr_t)xy, sizeof (struct xyiopb));
	xy->xy_recal = (u_char) 1;
	xy->xy_enabext = 1;
	xy->xy_eccmode = 2;
	xy->xy_autoup = 1;
	xy->xy_reloc = (u_char) 1;
	xy->xy_throttle = xythrottle;

}

/*
 * This routine clears the fields of the iopb that must be zero before a
 * command is executed.
 */
static void
cleariopb(register struct xyiopb *xy)
{

	xy->xy_errno = 0;
	xy->xy_iserr = 0;
	xy->xy_complete = 0;
	xy->xy_ctype = 0;
}

/*
 * Look up a unit struct by instance number of the child
 * called only by xycstart
 */
static struct xyunit *
xycunit(register struct xyunit **up, u_int instance)
{
	register int i;

	for (i = 0; i < XYUNPERC; i++, up++)
		if (*up != NULL && (*up)->un_instance == instance)
			return (*up);
	cmn_err(CE_PANIC, "xycunit");
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


static void
xycdumpiopb(register struct xyiopb *xy)
{
	register unsigned char *ptr;
	int	cnt;
	u_char	char1;
	u_char	char2;


	PRINTF3("\n\t\t............I O P B ...(0x%x)\n", xy);

	ptr = (unsigned char *)xy;

	for (cnt = 0; cnt < 28; cnt += 2) {

		char1 = *ptr++;
		char2 = *ptr++;

		if (char2)
		PRINTF3("\t\t>>> iopb byte 0x%x	   = 0x%x\n", cnt, char2);
		if (char1)
		PRINTF3("\t\t>>> iopb byte 0x%x	   = 0x%x\n", cnt + 1, char1);
	}
}

static void
xyc_get_codes(register struct xyctlr *c)
{
	u_char *ptr;
	u_char char2;
	u_char char3;
	int  cnt;

	/* force the IOPB to be updated */
	c->c_io->xy_resupd = 0;
	(void) xyccsrvalid(c->c_io);


	ptr = (u_char *)c->c_iopbbase + 2;

	for (cnt = 0; cnt < 4; cnt += 1) {
		char2 = *ptr;
		char3 = *(ptr+1);
		PRINTF3("\t\t>>> ERRS  0x%x CODE 0x%x at 0x%x\n", char3,
		    char2, (u_int) (ptr-2));
		ptr += c->c_iopbsize;
	}

}

#if	defined(LOOK_QUEUE) || defined(lint)
static void
look_queue(struct xyctlr *c)
{
	register struct xyiopb *aiopb;
	struct buf *ibuf;

	cmn_err(CE_CONT, " ");

	aiopb = c->rdyiopbq;

	while (aiopb) {
		cmn_err(CE_CONT, " 0x%x", aiopb->xy_unit);
		aiopb = aiopb->xy_nxtaddr;
	}

	/* now look at the buf queue */

	/* cmn_err(CE_CONT, "\n\t\t Q U E U E  BUF= "); */

	ibuf = c->c_waitqf;

	while (ibuf) {
		cmn_err(CE_CONT, " (0x%x)", (int)ibuf->b_edev);
		ibuf = ibuf->av_forw;
	}
	cmn_err(CE_CONT, "\n");
}
#endif	/* LOOK_QUEUE || lint */

#if	defined(XYDEBUG) || defined(lint)
static	void
xxprintf(char *fmt, ...)
{
	va_list adx;
	int j;

	mutex_enter(&xy_trace_mutex);
	va_start(adx, fmt);

	if (trace_mode > 0) {
		(void) vsprintf(&xy_trace[to_trace], fmt, adx);
		j = 0;
		while (xy_trace[to_trace]) {
			j++;
			to_trace++;
		}
		if (j > MAXLINESIZE || xy_trace[TRACE_SIZE])
			cmn_err(CE_WARN,
			"sprintf output > MAXLINESIZE");
		if (xy_trace[TRACE_SIZE + RED_ZONE -1])
			cmn_err(CE_PANIC,
			"sprintf output > MAXLINESIZE + RED_ZONE");
		xy_trace[to_trace] = '\n';

		if (to_trace < TRACE_SIZE - MAXLINESIZE) {
			for (j = 1; j < 79; j++) {
				if (j < 41)
					xy_trace[to_trace + j] = 'E';
				if (j < 2 || j == 39 || j == 40)
					xy_trace[to_trace + j] = '\n';
				if (j > 40 && xy_trace[to_trace + j]) {
					xy_trace[to_trace + j] = 'B';
					if (j == 78)
					    xy_trace[to_trace + j] = '\n';
				}
			}

		} else {
			for (j = 1; j < MAXLINESIZE; j++) {
				if (to_trace + j >= TRACE_SIZE)
					break;
				xy_trace[to_trace + j] = 'W';
				if (j < 2 || (j % 60) == 0)
					xy_trace[to_trace + j] = '\n';
			}

			/*
			 * Wrap around to beginning of buffer.
			 */

			to_trace = 0;
		}
		itrace = to_trace;
	}

	if (trace_mode ==  2) {
		vcmn_err(CE_CONT, fmt, adx);
	}
	va_end(adx);
	mutex_exit(&xy_trace_mutex);
#ifdef	lint
	itrace = itrace;
#endif
}

#endif
