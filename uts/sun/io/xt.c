/*
 * Copyright (c) 1991-1992, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)xt.c	1.22	94/03/31 SMI"	/* 4.1.1 xt.c 1.44 */

/*
 * Driver for Xylogics 472 Tape controller:
 *
 * Max Configuration: 2 controllers with one drive on each controller
 * (xt0 attached to xtc0 and xt1 attached to xtc1)
 *
 * Lay out of minor device byte:
 *
 *	7--6--5--4--3--2--1--0
 *
 *	6	BSD Behaviour (SVR4 behaviour if not set)
 *	4,3	Density select (00=800, 01=6250)
 *	2	No rewind on close
 *	1,0	Unit number (same as controller number, one unit per controller)
 */

#define	NXT	2
#define	NXTC	2
#define	XTHWSIZE 6

#include <sys/types.h>
#include <sys/devops.h>
#include <sys/open.h>
#include <sys/errno.h>
#include <sys/cmn_err.h>
#include <sys/stropts.h>
#include <sys/sysmacros.h>
#include <sys/file.h>
#include <sys/debug.h>
#include <sys/mtio.h>
#include <sys/param.h>
#include <sys/varargs.h>
#include <sys/xtvar.h>
#include <sys/xycreg.h>
#include <sys/xtreg.h>
#include <sys/kmem.h>
#include <sys/conf.h>
#include <sys/stat.h>

#include <sys/ddi.h>
#include <sys/sunddi.h>

/*
 * Debugging Declarations.
 */
#define	TRACE_XT	if (xt_debug == 5)
#define	TRACE_NN	if (xt_debug == 20000)
#define	MAXLINESIZE	120
#define	TRACE_SIZE	2048
#define	RED_ZONE	20

/* MAXLINESIZE = upper limit for one sprintf */

/* #define	stall_at_eof_eom 1 */

#ifdef	lint
#define	XTDEBUG
#endif

#ifdef	XTDEBUG
kmutex_t	xt_trace_mutex;
static	int	xt_debug = 4;
static	int	xt_trace_mode = 1;
static	int	xt_to_trace = 0;
static	int	xt_itrace = 0;
static	char	xt_trace[TRACE_SIZE + RED_ZONE];
static	u_char	xt_rand_trace = 0;
static	u_char	xt_limit_trace = 0;
static	u_char	xt_select = 0;
#else
#define	xt_debug 0
#endif

static	u_char	xt_inject = 0;

/*
 * Declarations for wrap-around trace
 */

#ifdef	XTDEBUG
#define	PRINTF		if (xt_debug)	  xxprintf
#define	PRINTF1		if (xt_debug)	  xxprintf
#define	PRINTF2		if (xt_debug > 1) xxprintf
#define	PRINTF3		if (xt_debug > 2) xxprintf
#define	PRINTF4		if (xt_debug > 3) xxprintf
#define	XT_TRACE	if (xt_debug > 3)
#else
#define	PRINTF		if (0) xxprintf
#define	PRINTF1		if (0) xxprintf
#define	PRINTF2		if (0) xxprintf
#define	PRINTF3		if (0) xxprintf
#define	PRINTF4		if (0) xxprintf
#define	XT_TRACE	if (0)
#endif	XTDEBUG

/*
 *		Adjustable parameters
 *		at compile time
 *
 * MAXLINESIZE = upper limit for one sprintf (increase if needed)
 * TRACE_SIZE  = Size in bytes of the whole trace (increase if needed)
 * xt_debug    = level of tracing (0 no tracing, 4 maximum tracing).
 *
 *
 *		Adjustable parameters
 *		at run time
 *
 * xt_trace_mode  = 0 for no trace
 *		 1 internal trace (wrap around	 in trace buffer)
 *		 2 internal and external trace.
 */

static int	debug_cnt = 0;		/* count interrupts after BOT */

/*
 * array of pointers to unit structures (one for each unit)
 */

static	struct	xtunit	*unit_array[2];

#define	search_unit(dev_num)	(unit_array[dev_num & 1])
#define	CMD(a)			((a >> 8) & 0xf)
#define	SUBFUNC(a)		(a & 0x3f)
#define	USE_DMA(a)		(a & 0x8000)

/*
 * DMA information, limits, etc.
 */

static ddi_dma_lim_t xt_lim = {
		(u_long) 0x00000000, 	/* dlim_addr_lo */
		(u_long) 0xffffffff, 	/* dlim_addr_hi */
		(u_int) ((1<<24)-1), 	/* dlim_cntr_max */
		0x00000006,		/* alignment, in bytes */
		0x2, 			/* flags. */
		4096			/* size in bytes */
};

/*
 * bits in minor device
 */
#define	NUNIT		4
#define	T_HIDENS	MT_DENSITY2	/* select high density */

#define	INF		(daddr_t)1000000L
#define	INFINITY	(daddr_t)1000000000L

#define	EOM_TWO_EOFS	2
#define	EOM_ONE_EOF	1
#define	EOM_NO_EOF	0
#define	NO_EOM		-1

#define	FOREVER		0x7fffffff

#define	FEET(n)		((n) << 2) /* 3 inch per erase, 4 times per ft */
#define	ERASE_AFT_EOT	(FEET(1))

#define	MX_RETRY	8
#define	MINPHYS_BYTES	65534

#define	XT_TIMER_INTERVAL	2000000		/* 2 seconds */

/*
 * State of the driver
 */
#define	XT_OPEN		0x1
#define	XT_DETACHING	0x2


static struct driver_minor_data {
	char	*name;
	int	minor;
	int	type;
} xt_minor_data[] = {
	{"l",	MT_DENSITY1, S_IFCHR},
	{"m",	MT_DENSITY2, S_IFCHR},
	{"",	MT_DENSITY2, S_IFCHR},
	{"ln",	MT_DENSITY1 | MT_NOREWIND, S_IFCHR},
	{"mn",	MT_DENSITY2 | MT_NOREWIND, S_IFCHR},
	{"n",	MT_DENSITY2 | MT_NOREWIND, S_IFCHR},
	{"lb",	MT_DENSITY1 | MT_BSD, S_IFCHR},
	{"mb",	MT_DENSITY2 | MT_BSD, S_IFCHR},
	{"b",	MT_DENSITY2 | MT_BSD, S_IFCHR},
	{"lbn",	MT_DENSITY1 | MT_NOREWIND | MT_BSD, S_IFCHR},
	{"mbn",	MT_DENSITY2 | MT_NOREWIND | MT_BSD, S_IFCHR},
	{"bn",	MT_DENSITY2 | MT_NOREWIND | MT_BSD, S_IFCHR},
	{0}
};

/*
 * Local Function Prototypes
 */
static int simple(struct xtunit *, int, int);
static int xt_info(dev_info_t *, ddi_info_cmd_t, void *, void **);
static int xt_identify(register dev_info_t *);
static int allocate_unit(register dev_info_t *, int);
static void deallocate_unit(register dev_info_t *, int);
static int xt_probe(dev_info_t *);
static int xt_present(dev_info_t *, struct xtunit *);
static int xt_attach(dev_info_t *, ddi_attach_cmd_t);
static int xt_detach(dev_info_t *, ddi_detach_cmd_t);
static int xt_open(dev_t *, int, int, cred_t *);
static int xt_wrt_eom(register dev_t, int);
static int xt_flush_eom(register dev_t, int);
static int xt_close(dev_t, int, int, cred_t *);
static int xt_command(dev_t, int, int, int);
static int xt_strategy(struct buf *);
static int xt_call_back(struct xtunit *);
static int xt_start(dev_t);
static void xt_save_iopb(struct xtunit *);
static void xt_get_iopb(struct xtunit *);
static void xt_cmd_timeout(struct xtunit *);
static void xt_go(dev_t);
static u_int xt_intr(caddr_t);
static void xt_check_errors(struct xtunit *);
static void xt_next_cmd(struct xtunit *);
static void xt_update_resid(struct xtunit *);
static void xt_timer(dev_t);
static int xt_csrvalid(volatile struct xydevice *);
static int xt_wait(volatile struct xydevice *);
static void xt_minphys(struct buf *);
static int xt_read(dev_t, struct uio *, cred_t *);
static int xt_write(dev_t, struct uio *, cred_t *);
static int xt_ioctl(dev_t, int, int, int, cred_t *, int *);
static void big_fsf(dev_t, int);
static void negative_bsf(dev_t, int);
static void xt_state(char, struct xtunit *);
static void xt_state2(struct xtunit *);
static void xxprintf(char *, ...);

#ifdef	XTDEBUG
static void for_debug(struct mtop *);
static void xt_dump_io(dev_t);
static void xt_dump_iopb(dev_t);
static void xt_dump_buf(struct buf *);
static void xt_dump_xtunit(dev_t);
#endif	XTDEBUG


extern int geterror(struct buf *);
extern int nulldev(), nodev();

/*
 * Device driver ops vector
 */

static struct cb_ops xt_cb_ops = {

	xt_open, 		/* open */
	xt_close, 		/* close */
	xt_strategy, 		/* strategy */
	nodev, 			/* print */
	nodev, 			/* dump */
	xt_read, 		/* read */
	xt_write, 		/* write */
	xt_ioctl, 		/* ioctl */
	nodev, 			/* devmap */
	nodev, 			/* mmap */
	nodev, 			/* segmap */
	nochpoll, 		/* poll */
	ddi_prop_op, 		/* cb_prop_op */
	0, 			/* streamtab  */
	D_NEW| D_MP		/* Driver compatibility flag */
};

static int xt_identify(dev_info_t *dip);

/*
 * probe is needed since xt is not a self identifying device
 */

static int xt_probe(dev_info_t *);
static int xt_attach(dev_info_t *, ddi_attach_cmd_t);

static struct dev_ops xt_ops = {

	DEVO_REV, 		/* devo_rev, */
	0, 			/* refcnt  */
	xt_info,		/* get_info */
	xt_identify, 		/* identify */
	xt_probe, 		/* probe */
	xt_attach, 		/* attach */
	nodev,			/* detach */
	nodev, 			/* reset */
	&xt_cb_ops, 		/* driver operations */
	(struct bus_ops *)0	/* bus operations */
};

static int xtthrot = 4;

/*
 * Give a simple command to a controller and spin until done.
 * Returns the error number or zero
 */
static int
simple(struct xtunit *unitp, int phys_unit, int cmd)
{
	volatile struct xydevice *xyio = unitp->c_io;
	register struct xtiopb *xt = unitp->c_iopb;
	int tmp;
	u_long	piopb;

	tmp = xyio->xy_resupd;		/* reset */
	if (!xt_wait(xyio))
		return (XTE_OPERATION_TIMEOUT);
	bzero((caddr_t)xt, sizeof (*xt));
	xt->xt_autoup = 1;
	xt->xt_reloc = 1;
	xt->xt_cmd = (u_char) cmd;
	xt->xt_throttle = (u_char) xtthrot;
	xt->xt_unit = (u_char) phys_unit;
	piopb = unitp->c_iopb_dmac_addr;
	tmp = XYREL(xyio, piopb);
	xyio->xy_iopbrel[0] = (u_char) (tmp >> 8);
	xyio->xy_iopbrel[1] = (u_char) tmp;
	tmp = XYOFF(piopb);
	xyio->xy_iopboff[0] = (u_char) (tmp >> 8);
	xyio->xy_iopboff[1] = (u_char) tmp;
	ddi_dma_sync(unitp->c_uiopb_handle, 0, 0, DDI_DMA_SYNC_FORDEV);
	xyio->xy_csr = XY_GO;

	/*
	 * Busy Wait. Wait until command finishes
	 */
	while (xyio->xy_csr & XY_BUSY)
		drv_usecwait(10);
	ddi_dma_sync(unitp->c_uiopb_handle, 0, 0, DDI_DMA_SYNC_FORCPU);
	return (xt->xt_errno);
} /* end simple() */

/*
 * get dip from dev_t
 * we assume we deal only with OTYP_CHR (Open as char device)
 */
/*ARGSUSED*/
static int
xt_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result)
{
	register int unit_count;
	register struct xtunit *unitp;

	register dev_t dev = (dev_t) arg;
	register int instance, error;

	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		for (unit_count = 0; unit_count < NXT; unit_count++) {
			unitp = unit_array[unit_count];
			if (unitp != (struct xtunit *) NULL) {
				if (unitp->c_ctlr == (dev & 1)) {
					*result = (void *)unitp->c_dip;
					error = DDI_SUCCESS;
					break;
				}
			}
		}
		error = DDI_FAILURE;
		break;
	case DDI_INFO_DEVT2INSTANCE:
		instance = MTUNIT(dev);
		*result = (void *)instance;
		error = DDI_SUCCESS;
		break;
	default:
		error = DDI_FAILURE;
	}
	return (error);
}

/*
 * This is the loadable module wrapper.
 */

#include <sys/modctl.h>

extern struct	mod_ops		mod_driverops;

static struct modldrv modldrv = {
	&mod_driverops, /* Type of module. This one is a driver */
	"Xylogics ctrl 472 driver", 	/* Name of the module. */
	&xt_ops, 	/* driver ops */
};
static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modldrv, NULL
};

/*
 * _init routine - called during modload.
 *	it will call ddi_get_parent  and ddi_install_parent
 *	for various VME_BUS related informations
 */
_init()
{
#ifdef	XTDEBUG
	if (xt_debug)
		mutex_init(&xt_trace_mutex, "xt_trace",
		    MUTEX_DRIVER, (void *) 0);
#endif

	return (mod_install(&modlinkage));

} /* end _init */

/*
 * _fini routine - called during modunload.
 */

_fini()
{
#ifdef	XTDEBUG
	int	err;

	if ((err = mod_remove(&modlinkage)) == 0) {
		mutex_destroy(&xt_trace_mutex);
	}
	return (err);
#else

	return (mod_remove(&modlinkage));
#endif

} /* end _fini */

_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

/*
 * Autoconfiguration Routines
 */
static int
xt_identify(register dev_info_t *dip)
{
	if (strcmp((char *)ddi_get_name(dip), "xt") == 0) {
		return (DDI_IDENTIFIED);
	} else {
		return (DDI_NOT_IDENTIFIED);
	}
} /* end xt_identify */

static int
allocate_unit(register dev_info_t *dip, int unit_i)
{
	register struct xtunit *unitp;
	ddi_dma_cookie_t iopb_cookie;

	/*
	 * allocate space for xtunit structure specific to the drive
	 */
	unitp = kmem_zalloc(sizeof (struct xtunit), KM_SLEEP);

	unit_array[unit_i] = unitp;
	unitp->c_dip = dip;
	unitp->c_ctlr = unit_i;

	/*
	 * save pointer to unit structure in the dev info node.
	 */
	ddi_set_driver_private(dip, (caddr_t) unitp);

	/*
	 * Map hardware registers
	 * the offset should be modulo PAGESIZE (13 bits for 8k pages)
	 */
	ddi_map_regs(dip, 0, (caddr_t *) &unitp->c_io,
			(off_t) 0, sizeof (struct xydevice));

	/*
	 * Set up DMA (c_iopb from CPU side, dmac_addr from ctrl side)
	 */
	(void) ddi_iopb_alloc(dip, &xt_lim, (u_int) sizeof (struct xtiopb),
	    (caddr_t *) &unitp->c_iopb);
	if (ddi_dma_addr_setup(dip, (struct as *) 0,
	    (caddr_t) unitp->c_iopb, (u_int) sizeof (struct xtiopb),
	    DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, (caddr_t) 0,
	    &xt_lim, &unitp->c_uiopb_handle)) {
		cmn_err(CE_WARN, "xt%d: cannot map IOPB space", unitp->c_ctlr);
	    ddi_iopb_free((caddr_t) &unitp->c_iopb);
	    return (1);
	}
	ddi_dma_htoc(unitp->c_uiopb_handle, 0, &iopb_cookie);
	unitp->c_iopb_dmac_addr = iopb_cookie.dmac_address;

	/*
	 * Allocate buf structures
	 */
	unitp->c_sbufp = kmem_zalloc(sizeof (struct buf), KM_SLEEP);
	return (0);
} /* end allocate_unit */

static void
deallocate_unit(register dev_info_t *dip, int unit_i)
{
	register struct xtunit *unitp;

	unit_array[unit_i] = (struct xtunit *) NULL;
	unitp = (struct xtunit *) ddi_get_driver_private(dip);

	/*
	 * stop the timer, unmap.
	 */
	mutex_enter(&unitp->c_mutex);
	unitp->c_curr_state |= XT_DETACHING;
	(void) untimeout(unitp->c_timeout_id);
	unitp->c_curr_state &= ~XT_DETACHING;
	mutex_exit(&unitp->c_mutex);
	ddi_unmap_regs(dip, 0, (caddr_t *) &unitp->c_io, 0, 0);
	ddi_iopb_free((caddr_t) unitp->c_iopb);

	/*
	 * deallocate the other buffers struct buf
	 * this bufs could be allocated only if device exists, etc
	 */
	kmem_free(unitp->c_sbufp, sizeof (struct buf));
	kmem_free(unitp, sizeof (struct xtunit));

	/*
	 * clear pointer to unit structure in the dev info node.
	 */
	ddi_set_driver_private(dip, (caddr_t) NULL);
} /* end deallocate_unit */

/*
 * Determine existence of controller
 */
static int
xt_probe(dev_info_t *dip)
{
	int unit = ddi_get_instance(dip);
	register struct xtunit *unitp;

	/*
	 * we need to check unit_count to make sure we are not
	 * exceeding 2 xt controllers in the system (0 and 1)
	 * unit = 0 corresponds to controller 0 (slave 0)
	 * unit = 1 corresponds to controller 1 (slave 0)
	 */
	if (unit > 1) {
		cmn_err(CE_WARN, "xt_probe: more than 2 controllers");
		return (DDI_PROBE_FAILURE);
	}
	if (unit < 0) {
		cmn_err(CE_WARN, "xt_probe: unit < 0 cannot happen");
		return (DDI_PROBE_FAILURE);
	}
	if (unit_array[unit] != 0) {
		cmn_err(CE_WARN, "xt_probe: controller already assigned");
		return (DDI_PROBE_FAILURE);
	}
	if (allocate_unit(dip, unit)) {
		cmn_err(CE_WARN, "xt_probe: allocation failure");
		return (DDI_PROBE_FAILURE);
	}
	unitp = unit_array[unit];

	if (xt_present(dip, unitp)) {
		unitp->c_alive = 1;
		return (DDI_PROBE_SUCCESS);
	} else {
#ifdef	XTDEBUG
		if (xt_debug > 0)
			cmn_err(CE_WARN,
			    "xt_probe:CHECK XT CONTROLLER %d", unit);
#endif	XTDEBUG
		deallocate_unit(dip, unit);
		return (DDI_PROBE_FAILURE);
	}
} /* end xt_probe */

/*
 * Check if controller present
 */
/*ARGSUSED*/
static int
xt_present(dev_info_t *dip, struct xtunit *unitp)
{
	/*
	 * check actual xt location
	 */
	if (ddi_peekc(dip, (char *)&unitp->c_io->xy_resupd,
					(char *)0) != DDI_SUCCESS) {
#ifdef	XTDEBUG
		if (xt_debug > 0)
			cmn_err(CE_WARN,
			    "xt_present: no controller at this address");
#endif	XTDEBUG
		return (0);
	}
	unitp->c_present = 1;

	/*
	 * send a NOP to the controller and check status
	 */
	(void) simple(unitp, 0, XT_NOP);
	if (unitp->c_iopb->xt_ctype != XYC_472) {
		cmn_err(CE_WARN, "xt_present: unknown controller type");
		return (0);
	}

	/*
	 * send a TEST COMMAND to the controller and check err code
	 */
	if (simple(unitp, 0, XT_NOP)) {
		cmn_err(CE_WARN, "xt_present: controller fails DIAGS");
		return (0);
	}
	return (1);
} /* end xt_present */

/*
 * Record attachment of the unit to the controller.
 */
/*ARGSUSED*/
static int
xt_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	register struct xtunit	*unitp;
	struct driver_minor_data *dmdp;


	for (dmdp = xt_minor_data; dmdp->name != NULL; dmdp++) {
		if (ddi_create_minor_node(dip, dmdp->name, dmdp->type,
		    (MTMINOR(ddi_get_instance(dip))) | dmdp->minor,
		    DDI_NT_TAPE, NULL) == DDI_FAILURE) {
			ddi_remove_minor_node(dip, NULL);
			deallocate_unit(dip, ddi_get_instance(dip));
			return (DDI_FAILURE);
		}
	}

	unitp = (struct xtunit *) ddi_get_driver_private(dip);

	/*
	 * Set up interrupt handler for the device
	 */
	ddi_add_intr(dip, (u_int) 0, &unitp->c_ibc, &unitp->c_idc,
	    xt_intr, (caddr_t) unitp);

	/*
	 * init other driver data.
	 */
	mutex_init(&unitp->c_mutex_openf, "xt_openf", MUTEX_DRIVER,
	    (void *)unitp->c_ibc);
	mutex_init(&unitp->c_mutex, "xt_critical", MUTEX_DRIVER,
	    (void *)unitp->c_ibc);
	mutex_init(&unitp->c_mutex_callbck, "xt_callback", MUTEX_DRIVER,
	    (void *)unitp->c_ibc);
	cv_init(&unitp->c_transfer_wait, "xt_transfer", CV_DRIVER,
	    (void *)unitp->c_ibc);
	cv_init(&unitp->c_alloc_buf_wait, "xt_alloc_buf", CV_DRIVER,
	    (void *)unitp->c_ibc);
	cv_init(&unitp->c_callbck_cv, "xt_callback count", CV_DRIVER,
	    (void *)unitp->c_ibc);

	/*
	 * Add a zero-length attribute to tell the world we support
	 * kernel ioctls (for layered drivers)
	 */
	(void) ddi_prop_create(DDI_DEV_T_NONE, dip, DDI_PROP_CANSLEEP,
	    DDI_KERNEL_IOCTL, NULL, 0);

	ddi_report_dev(dip);

	/*
	 * sc_firstopen is set at attach time and cleared on 1st open
	 */
	unitp->c_firstopen = 1;
	return (DDI_SUCCESS);
} /* end xt_attach */


/*
 * Detach the driver
 */
/*ARGSUSED*/
static int
xt_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	int		unit;
	struct xtunit	*un;

	switch (cmd) {
	case DDI_DETACH:
		unit = ddi_get_instance(dip);
		un = (struct xtunit *) ddi_get_driver_private(dip);

		/*
		 * Cannot detach unless we're closed
		 */
		mutex_enter(&un->c_mutex_openf);
		if (un->c_curr_state & XT_OPEN) {
			mutex_exit(&un->c_mutex_openf);
			return (DDI_FAILURE);
		}
		mutex_exit(&un->c_mutex_openf);

		/*
		 * Wait for uncancellable callbacks after close before
		 * detaching.
		 */
		mutex_enter(&un->c_mutex_callbck);
		while (un->c_callbck_cnt > 0) {
			cv_wait(&un->c_callbck_cv, &un->c_mutex_callbck);
		}
		mutex_exit(&un->c_mutex_callbck);

		/*
		 * Remove interrupt handler for the device
		 */
		ddi_remove_intr(dip, (u_int) 0, un->c_ibc);

		/*
		 * Free mutexes and cv's
		 */
		mutex_destroy(&un->c_mutex_openf);
		mutex_destroy(&un->c_mutex);
		mutex_destroy(&un->c_mutex_callbck);
		cv_destroy(&un->c_transfer_wait);
		cv_destroy(&un->c_alloc_buf_wait);
		cv_destroy(&un->c_callbck_cv);

		/*
		 * Free everything we have allocated for this unit
		 */
		deallocate_unit(dip, unit);

		return (DDI_SUCCESS);

	default:
		cmn_err(CE_NOTE, "xt_detach: 0x%x\n", cmd);
		return (DDI_FAILURE);
	}
}


static	int	xtdefdens = 0;
static	int	xtdefspeed = 0;

/*
 * Open the device (Exclusive open thru c_mutex_openf)
 */

/*ARGSUSED*/
static int
xt_open(dev_t *devp, int flag, int otyp, cred_t *credp)
{
	register dev_t dev = *devp;
	volatile struct xydevice *xyio;
	register struct xtunit	*unitp = search_unit(dev);
	int err, retry, stat, xtctlr;

	PRINTF("\n::OPEN %x", dev);

	if (!unitp || !unitp->c_present || !unitp->c_alive)
		return (ENXIO);
	xyio = unitp->c_io;
	xtctlr = unitp->c_ctlr;
	stat = unitp->c_xtstat;

	/*
	 * Check for valid minor number (see minor # description)
	 */
	if ((getminor(dev) & MT_DENSITY3) || MTUNIT(dev) > 2)
		return (ENXIO);

	/*
	 * If device already opened, return (EBUSY).
	 */
	mutex_enter(&unitp->c_mutex_openf);
	if ((unitp->c_curr_state & XT_OPEN) != 0) {
		mutex_exit(&unitp->c_mutex_openf);
		return (EBUSY);
	}
	unitp->c_curr_state |= XT_OPEN;
	mutex_exit(&unitp->c_mutex_openf);

	unitp->c_dev = dev;
	unitp->c_resid = 0;
	if (getminor(dev) & MT_BSD)
		unitp->c_svr4 = 0;
	else
		unitp->c_svr4 = 1;

retry:
	retry = 3;
	while (retry--) {
		err = xt_command(dev, XT_DSTAT, 0, 1);
		if (err == EINTR || !retry) {
			mutex_enter(&unitp->c_mutex_openf);
			unitp->c_curr_state &= ~XT_OPEN;
			mutex_exit(&unitp->c_mutex_openf);
			return (retry ? EINTR :EIO);
		}
		if (err) {
#ifndef	lint
			int tmp = xyio->xy_resupd;
#endif	lint
			cmn_err(CE_WARN, "xt%d: resetting ctrl", unitp->c_ctlr);
			(void) xt_wait(xyio);
		} else
			break;
	}
	stat = unitp->c_xtstat;
	if ((stat & (XTS_ONL|XTS_RDY)) != (XTS_ONL|XTS_RDY)) {
		if (!(stat & XTS_ONL))
			cmn_err(CE_NOTE, "xt%d: not online", xtctlr);
		else
			cmn_err(CE_NOTE, "xt%d: not ready", xtctlr);
		mutex_enter(&unitp->c_mutex_openf);
		unitp->c_curr_state &= ~XT_OPEN;
		mutex_exit(&unitp->c_mutex_openf);
		return (EIO);
	}
	if ((flag & FWRITE) && (stat & XTS_FPT)) {
		cmn_err(CE_NOTE, "xt%d: no write ring", xtctlr);
		mutex_enter(&unitp->c_mutex_openf);
		unitp->c_curr_state &= ~XT_OPEN;
		mutex_exit(&unitp->c_mutex_openf);
		return (EACCES);
	}
	/* activate timer if inactive */
	if (unitp->c_tact == 0) {
		unitp->c_timo = INF;
		unitp->c_tact = 1;
		unitp->c_timeout_id = timeout(xt_timer, (caddr_t)dev,
			drv_usectohz((clock_t) XT_TIMER_INTERVAL));
	}

	/*
	 * At BOT, or first time we open this device
	 */
	if ((stat & XTS_BOT) || unitp->c_firstopen) {
		/*
		 * First time through, ensure we're at BOT.
		 */
		/* debug_cnt = 0; */
		if (unitp->c_firstopen) {
			if (!(stat & XTS_BOT) &&
			    xt_command(dev, XT_SEEK, XT_REWIND, 1)) {
				cmn_err(CE_WARN, "xt%d: cannot rewind", xtctlr);
				mutex_enter(&unitp->c_mutex_openf);
				unitp->c_curr_state &= ~XT_OPEN;
				mutex_exit(&unitp->c_mutex_openf);
				return (EIO);
			}
			unitp->c_firstopen = 0;
		}

		/*
		 * suninstall was not rewinding or offline the tape between
		 * volumes, so reset accounting variables here.
		 */
		unitp->c_fileno = 0;
		unitp->c_recno = 0;

		/*
		 * In 4.x, flags was specified in master file as 0, 1 or 2.
		 * In 5.x, it is given in xt.conf file.
		 * If not specified in xt.conf file, the default value is 1.
		 */
		unitp->c_flags = ddi_getprop(DDI_DEV_T_ANY, unitp->c_dip,
			DDI_PROP_DONTPASS, "flags", 1);

		switch (unitp->c_flags) {
		default:
		case 0:		/* unknown drive type */
			if (xtdefdens) {
				if (xtdefdens > 0) {
					if (xt_command(dev, XT_PARAM,
					    XT_HI_DENSITY, 1))
						goto retry;
				} else {
					if (xt_command(dev, XT_PARAM,
					    XT_LO_DENSITY, 1))
						goto retry;
				}
			}
			if (xtdefspeed) {
				if (xtdefspeed > 0) {
					if (xt_command(dev, XT_PARAM,
					    XT_HI_SPEED, 1))
						goto retry;
				} else {
					if (xt_command(dev, XT_PARAM,
					    XT_LO_SPEED, 1))
						goto retry;
				}
			}
			break;
		case 1:		/* CDC Keystone III and Telex 9250 */
			if (getminor(dev) & T_HIDENS) {

				if (xt_command(dev, XT_PARAM, XT_HI_DENSITY, 1))
					goto retry;
			} else {
				if (xt_command(dev, XT_PARAM, XT_LO_DENSITY, 1))
					goto retry;
			}
			break;
		case 2:		/* Kennedy triple density */
			if (getminor(dev) & T_HIDENS) {
				if (xt_command(dev, XT_PARAM, XT_HI_SPEED, 1))
					goto retry;
			} else
				if (xt_command(dev, XT_PARAM, XT_LO_SPEED, 1))
					goto retry;
			break;
		}
	}
	unitp->c_open_opt = flag;
	unitp->c_lstpos = 0;

	/* invalidate the tape */
	err = 0;
	if ((unitp->c_open_opt & (FWRITE|FREAD)) == FWRITE)
		err = xt_wrt_eom(dev, EOM_TWO_EOFS);
	if (err) {
		mutex_enter(&unitp->c_mutex_openf);
		unitp->c_curr_state &= ~XT_OPEN;
		mutex_exit(&unitp->c_mutex_openf);
	}
	return (err);

} /* end open */

static int
xt_wrt_eom(register dev_t dev, int  n_eof)
{
	register struct xtunit *unitp = search_unit(dev);
	int i, stat = 0;
	daddr_t save_recno;

	save_recno = unitp->c_recno;
	for (i = 0; i < 2; i++)
		if (xt_command(dev, XT_FMARK, 0, 1)) {
			cmn_err(CE_WARN,
			    "xt_wrt_eom:failed in writing EOM %d", i);
			return (EIO);
		}

	if (n_eof != EOM_NO_EOF) {
		if (stat = xt_command(dev, XT_SEEK, XT_BACK_MARK, n_eof))
			cmn_err(CE_WARN,
			    "xt_wrt_eom: failed in backup %d eof", n_eof);
		else {
			unitp->c_fileno_eom = unitp->c_fileno;
			/* we're positioned at EOM so recno = 0 */
			unitp->c_recno = 0;
			if (n_eof == EOM_TWO_EOFS)
				unitp->c_recno = save_recno;
		}
	}
	return (stat);
}

static int
xt_flush_eom(register dev_t dev, int n_eof)
{
	register struct xtunit *unitp = search_unit(dev);

	if (unitp->c_open_opt & FWRITE)
		if (unitp->c_last_cmd == CMD_WRITE ||
		    unitp->c_last_cmd == CMD_BACK_REC_W_EOT)
			return (xt_wrt_eom(dev, n_eof));
	return (0);
}

/*
 * Close tape device.
 *
 * If tape was open for writing or last operation was
 * a write, then write two EOF's and backspace over the last one.
 * Unless this is a non-rewinding special file, rewind the tape.
 */
/*ARGSUSED*/
static int
xt_close(dev_t dev, int flag, int otyp, cred_t *cred_p)
{
	register struct xtunit *unitp = search_unit(dev);

	/*
	 * do a fsf if we are not open O_WRONLY.
	 * used to be:
	 * do a fsf on close if c_svr4 (c_read_hit_eof set if c_svr4 set)
	 * and if we are not at EOM (End Of recorded Media)
	 * if (unitp->c_read_hit_eof &&
	 *	unitp->c_fileno != unitp->c_fileno_eom)
	 */
	/*
	 * SVR4 behaviour for skipping to next file, after
	 * reading the current file on a non rewinding tape.
	 */
	if ((unitp->c_open_opt & FREAD) &&
	    unitp->c_last_cmd != CMD_WRITE &&
	    unitp->c_last_cmd != CMD_BACK_REC_W_EOT &&
	    (getminor(dev) & MT_NOREWIND) &&
	    unitp->c_recno && unitp->c_svr4) {
		(void) xt_command(dev, XT_SEEK, XT_SKIP_MARK, 1);
	}

	/*
	 * If user did write on the tape, write two file marks and
	 * back space over the last one.
	 */
	if (xt_flush_eom(dev, EOM_ONE_EOF)) {
		cmn_err(CE_WARN, "xt_close: flush eom failed");
	}

	if ((getminor(dev)&MT_NOREWIND) == 0 &&
	    unitp->c_last_cmd != CMD_UNLOAD &&
	    unitp->c_last_cmd != CMD_REWIND) {
		(void) xt_command(dev, XT_SEEK, XT_REWIND, 1);
	}
	mutex_enter(&unitp->c_mutex_openf);
	unitp->c_curr_state &= ~XT_OPEN;
	mutex_exit(&unitp->c_mutex_openf);
	PRINTF("\n:CLOSE %x", dev);
	return (0);
} /* end xt_close */

/*
 * IOCTL commands. Execute command "cmd, subfunc" "count" times.
 * Returns bp->b_error for error, or return (0) for OK.
 */
static int
xt_command(dev_t dev, int cmd, int subfunc, int count)
{
	register struct xtunit	*unitp = search_unit(dev);
	register struct buf *bp = unitp->c_sbufp;
	int next_cmd = (cmd << 8) + subfunc;
	int error;
	PRINTF("\n::xt_command");

	if ((next_cmd == CMD_REWIND) && (unitp->c_xtstat & XTS_BOT))
		return (0);
	if ((unitp->c_last_cmd == CMD_REWIND) && next_cmd != CMD_REWIND) {
		if (unitp->c_xtstat & XTS_REW) {
			(void) xt_command(dev, XT_SEEK, XT_REWIND, 1);
		}
	}
	unitp->c_next_cmd = next_cmd;
	unitp->c_next_cnt = (u_short) count;

	/*
	 * Stall at EOM.
	 */
#ifdef stall_at_eof_eom
	if ((unitp->c_next_cmd == CMD_SKIP_MARK) && unitp->c_fileno &&
	    (unitp->c_fileno == unitp->c_fileno_eom)) {
		cmn_err(CE_WARN, "xt: fsf at eom not allowed");
		return (EIO);
	}
#endif
	bp->b_edev = dev;
	bp->b_blkno = 0;
	bp->b_flags = 0;
	(void) xt_strategy(bp);
	error = unitp->c_bp_error;
	if (!error && unitp->c_resid)
		switch (unitp->c_last_cmd) {
		case CMD_SKIP_REC:
		case CMD_BACK_REC:
		case CMD_SKIP_MARK:
		case CMD_BACK_MARK:
		case CMD_SKIP_MARK_BSR_EOF:
		case CMD_BACK_MARK_FSR_EOF:
			return (EIO);
		default:
			break;
		}
	return (error);
} /* end xt_command */

/*
 * Process a tape operation (we use one buf at a time)
 * Return value is ignored by physio (we put 0)
 *
 * the interrupt routine will clear c_transfer
 * (all commands enable interrupts (xt_ie = 1).
 */
static int
xt_strategy(struct buf *dp)
{
	register struct xtunit *unitp = search_unit(dp->b_edev);
	dev_info_t	*dip = unitp->c_dip;
	struct buf *bp;
	int err, retry, next_cmd, got_sig;

	mutex_enter(&unitp->c_mutex);
	unitp->c_dma_buf = (struct buf *)0;
	retry = MX_RETRY;
	if (dp != unitp->c_sbufp) {
		unitp->c_dma_buf = dp;
		unitp->c_next_cmd =
		    (dp->b_flags & B_READ) ? CMD_READ : CMD_WRITE;
		if (unitp->c_read_hit_eof && unitp->c_svr4) {
			dp->b_flags |= B_ERROR;
			biodone(dp);
			mutex_exit(&unitp->c_mutex);
			return (0);
		}
		if (unitp->c_read_hit_eof && unitp->c_svr4 == 0) {
			unitp->c_fileno++;
			unitp->c_recno = 0;
			unitp->c_read_hit_eof = 0;
		}
		/* Allocate DMA resources (strategy called from physio) */
		do {
			unitp->c_udma_handle = 0;
			mutex_enter(&unitp->c_mutex_callbck);
			unitp->c_callbck_cnt++;
			err = ddi_dma_buf_setup(dip, dp,
			    (unitp->c_next_cmd == CMD_READ) ?
			    DDI_DMA_READ : DDI_DMA_WRITE,
			    xt_call_back, (caddr_t) unitp, &xt_lim,
			    &unitp->c_udma_handle);
			if (err != DDI_DMA_MAPPED) {
				mutex_exit(&unitp->c_mutex_callbck);
				cmn_err(CE_WARN, "xt%d: DMA alloc failed",
				    unitp->c_ctlr);
				cv_wait(&unitp->c_alloc_buf_wait,
				    &unitp->c_mutex);
			} else {
				unitp->c_callbck_cnt--;
				cv_signal(&unitp->c_callbck_cv);
				mutex_exit(&unitp->c_mutex_callbck);
			}
		} while (err != DDI_DMA_MAPPED);
	}
	next_cmd = unitp->c_next_cmd;

	/* ---------------------- begin retry loop ------------------- */
	while (next_cmd != CMD_DONE && next_cmd != CMD_ABORT && --retry) {
		if (USE_DMA(next_cmd))
			bp = dp;
		else
			bp = unitp->c_sbufp;
		unitp->c_bp_error = 0;
		unitp->c_stuck = 0;
		unitp->c_transfer = 1;
		unitp->c_bp_used = bp;
		(void) xt_start(bp->b_edev);

		/*
		 * Wait until command is finished.
		 */
		got_sig = 0;
		while (unitp->c_transfer) {
			PRINTF("\nW");
			got_sig += !cv_wait_sig(&unitp->c_transfer_wait,
			    &unitp->c_mutex);
			if (got_sig) {
				PRINTF("A^");
				/*
				 * Giveup the mutex and sleep for 0.1 sec.
				 * xt interrupt or timeout will acquire mutex
				 * and reset the unitp->c_transfer.
				 */
				mutex_exit(&unitp->c_mutex);
				delay(drv_usectohz(100000));
				mutex_enter(&unitp->c_mutex);
			}
		}
		if (unitp->c_stuck || got_sig) {
			unitp->c_timo = INF;
			bp->b_resid = bp->b_bcount;
			unitp->c_transfer = 0;
			if (got_sig)
				cmn_err(CE_WARN, "xt%d: operator abort",
				    unitp->c_ctlr);
			else {
#ifndef	lint
				int tmp = unitp->c_io->xy_resupd;
#endif	lint
				cmn_err(CE_WARN,
				    "xt%d: ctrl hung, please REWIND/restart",
				    unitp->c_ctlr);
			}
			(void) xt_wait(unitp->c_io);
			mutex_exit(&unitp->c_mutex);
			delay(drv_usectohz(1000000));
			mutex_enter(&unitp->c_mutex);
			unitp->c_next_cmd = CMD_ABORT;
		} else {
			PRINTF("\nS");
			xt_get_iopb(unitp);
			xt_check_errors(unitp);
			if (unitp->c_next_cmd != CMD_ABORT)
				xt_next_cmd(unitp);
		}
		xt_state('S', unitp);
		next_cmd = unitp->c_next_cmd;
	} /* ---------------------- end retry loop ------------------- */

	if (next_cmd != CMD_DONE) {

		/*
		 * ABORT case: (CMD_ABORT, or retry reached zero)
		 * xt_command() will return (unitp->c_bp_error).
		 */
		bp->b_flags |= B_ERROR;
		unitp->c_bp_error = geterror(bp);
		if (got_sig)
			unitp->c_bp_error = EINTR;
	}
	xt_update_resid(unitp);
	if (unitp->c_udma_handle) {
		/* Release DMA buffers */
		ddi_dma_free(unitp->c_udma_handle);
		unitp->c_udma_handle = (ddi_dma_handle_t) 0;
		biodone(unitp->c_dma_buf);
	}
	mutex_exit(&unitp->c_mutex);
	return (0);
} /* end strategy */

static int
xt_call_back(struct xtunit *unitp)
{
	cv_signal(&unitp->c_alloc_buf_wait);
	return (0);
}

/*
 * Start transfer for a given command associated with bp_used
 */
static int
xt_start(dev_t dev)
{
	register struct xtunit *unitp = search_unit(dev);
	register struct buf *bp = unitp->c_bp_used;
	register struct xtiopb *xt = unitp->c_iopb;
	int next_cmd = unitp->c_next_cmd;

	PRINTF("\n :xt_start");

	xt_save_iopb(unitp);

	/*
	 * Prepare new IOPB
	 */
	bzero((caddr_t)xt, sizeof (*xt));
	xt->xt_autoup = 1;
	xt->xt_reloc = 1;
	xt->xt_ie = 1;
	xt->xt_cmd = CMD(next_cmd);
	xt->xt_throttle = (u_char) xtthrot;
	xt->xt_subfunc = SUBFUNC(next_cmd);
	xt->xt_unit = 0;

	if (USE_DMA(next_cmd)) {
		unitp->c_next_cnt = bp->b_bcount;
		xt->xt_swab = 1;
		xt->xt_retry = 1;
	}
	xt->xt_cnt = unitp->c_next_cnt;
	/* XT_TRACE xt_dump_buf(bp); */
	/* XT_TRACE xt_dump_xtunit(dev); */
	xt_cmd_timeout(unitp);
	xt_go(dev);
	return (0);
} /* end xt_start */

static void
xt_save_iopb(struct xtunit *unitp)
{
	char *ptr0 = (char *) unitp->c_iopb;
	char *ptr1 = (char *) &unitp->c_old1_iopb;
	char *ptr2 = (char *) &unitp->c_old2_iopb;
	register int i;

	for (i = 0; i < 6; i++) {
		*ptr2++ = *ptr1;
		*ptr1++ = *ptr0++;
	}
}

static void
xt_get_iopb(struct xtunit *unitp)
{
	struct xtiopb *xt = unitp->c_iopb;

	ddi_dma_sync(unitp->c_uiopb_handle, 0, 0, DDI_DMA_SYNC_FORCPU);
	unitp->c_old2_cmd = unitp->c_old1_cmd;
	unitp->c_old2_cnt = unitp->c_old1_cnt;
	unitp->c_old1_cmd = unitp->c_last_cmd;
	unitp->c_old1_cnt = unitp->c_last_cnt;
	unitp->c_last_cmd = unitp->c_next_cmd;
	unitp->c_last_cnt = unitp->c_next_cnt;
	unitp->c_next_cmd = CMD_DONE;
	/* next_cnt will be overwritten if needed */
	unitp->c_next_cnt = 1;
	unitp->c_old2_acnt = unitp->c_old1_acnt;
	unitp->c_old1_acnt = unitp->c_last_acnt;
	unitp->c_last_acnt = xt->xt_acnt;
}

/*
 * Compute the timeout value for each command
 * a rewind takes 140 seconds, and a full write ~330 seconds.
 */
static void
xt_cmd_timeout(struct xtunit *unitp)
{
	int next_cmd = unitp->c_next_cmd;
	int next_cnt = unitp->c_next_cnt;
	int sec;

	switch (next_cmd) {
	case CMD_REWIND:
	case CMD_UNLOAD:
	case CMD_BACK_MARK:
	case CMD_SKIP_MARK:
		sec = 180;
		break;
	case CMD_NOP:
	case CMD_LO_DENSITY:
	case CMD_HI_DENSITY:
	case CMD_LO_SPEED:
	case CMD_HI_SPEED:
	case CMD_STATUS:
		sec = min(4 * next_cnt, 60);
		break;
	case CMD_WRITE:
	case CMD_READ:
		sec = min(max(20 * (next_cnt >> 16) + 20, 30), 5 * 60);
		break;
	case CMD_SHORT_ERASE:
	case CMD_SHORT_ERASE_W:
		sec = 60;
		break;
	default:
		sec = min(max(20 * next_cnt, 30), 5 * 60);
		break;
	}
	if (unitp->c_last_cmd == CMD_REWIND && !(unitp->c_xtstat & XTS_BOT))
		sec = 180;
	unitp->c_timo = sec;
} /* end xt_cmd_timeout() */

/*
 * start the device  (XY_GO bit).
 */
static void
xt_go(dev_t dev)
{
	register struct xtunit *unitp = search_unit(dev);
	register struct xtiopb *xt = unitp->c_iopb;
	volatile struct xydevice *xyio = unitp->c_io;
	register int dmaddr, tmp;
	ddi_dma_cookie_t cookie;
	u_long piopb;

	/*
	 * Get the DMA address value from the handle
	 */
	if (unitp->c_udma_handle) {
		if (ddi_dma_htoc(unitp->c_udma_handle, 0, &cookie))
			cmn_err(CE_PANIC,
			    "xt%d: cannot return DMA cookie", unitp->c_ctlr);
		dmaddr = (int) cookie.dmac_address;
		if ((int)(dmaddr + xt->xt_cnt) > (int)0x100000 &&
		    (int)(xyio->xy_csr & XY_ADDR24) == (int)0)
			cmn_err(CE_PANIC,
			    "xt%d: exceeded 20 bit address", unitp->c_ctlr);
		xt->xt_bufoff = XYOFF(dmaddr);
		xt->xt_bufrel = XYREL(xyio, dmaddr);
	}

	/* Store IOPB address in IO registers */
	piopb = unitp->c_iopb_dmac_addr;
	tmp = XYREL(xyio, piopb);
	xyio->xy_iopbrel[0] = (u_char) (tmp >> 8);
	xyio->xy_iopbrel[1] = (u_char) tmp;
	tmp = XYOFF(piopb);
	xyio->xy_iopboff[0] = (u_char) (tmp >> 8);
	xyio->xy_iopboff[1] = (u_char) tmp;
	/* xt_state(' ', unitp); */
	/* sync IOPB and GO */
	ddi_dma_sync(unitp->c_uiopb_handle, 0, 0, DDI_DMA_SYNC_FORDEV);
	(void) xt_wait(xyio);
	xyio->xy_csr = XY_GO;
	(void) xt_csrvalid(xyio);
} /* end xt_go */

/*
 * Interrupt routine (Returns 1 for OK, 0 for Fail)
 */
static u_int
xt_intr(caddr_t intr_arg)
{
	register struct xtunit *unitp = (struct xtunit *) intr_arg;
	volatile struct xydevice *xyio = unitp->c_io;

	mutex_enter(&unitp->c_mutex);
	/* wait for controller to settle down  */
	(void) xt_csrvalid(xyio);
	(void) xt_wait(xyio);
	debug_cnt++;
	/*
	 * Check for stray interrupt (locore.s will give message also)
	 */
	if (unitp->c_transfer == 0) {
		cmn_err(CE_NOTE, "xt%d: stray interrupt", unitp->c_ctlr);
		(void) xt_csrvalid(xyio); /* don't return too fast */
		(void) xt_wait(xyio);
		mutex_exit(&unitp->c_mutex);
		return (0);
	}
	if (xyio->xy_csr & (XY_ERROR | XY_DBLERR)) {
		xyio->xy_csr = XY_ERROR;
		(void) xt_csrvalid(xyio);
	}
	(void) xt_csrvalid(xyio);
	xyio->xy_csr = XY_INTR;		/* clear the interrupt */
	unitp->c_timo = INF;		/* stop timing out */

	unitp->c_transfer = 0;
	cv_signal(&unitp->c_transfer_wait);
	(void) xt_wait(xyio);
	mutex_exit(&unitp->c_mutex);
	return (1);
} /* end xt_intr() */

/*
 * Checking errors.
 * unitp->c_next_cmd is set to CMD_ABORT if fatal error
 * unitp->c_next_cmd is set to CMD_RETRY if retry error
 * unitp->c_next_cmd stays at  CMD_DONE	 if no error
 */
static void
xt_check_errors(struct xtunit *unitp)
{
	struct xtiopb *xt = unitp->c_iopb;
	struct buf *bp = unitp->c_bp_used;
	int last_cmd = unitp->c_last_cmd;
	int xtc = unitp->c_ctlr;
	int stat = xt->xt_status;
	int err = xt->xt_errno;

	unitp->c_xtstat = (u_short) stat;

	if (err == XTE_REVERSE_INTO_BOT && (stat & XTS_BOT))
		err = XTE_NO_ERROR;
	if (err == XTE_CORRECTED_DATA) {
		cmn_err(CE_NOTE, "xt%d: soft error", xtc);
		err = XTE_NO_ERROR;
	}
	if (err == XTE_ID_BURST_DETECTED)
		err = XTE_NO_ERROR;
	unitp->c_error = (u_short) err;

	/* inject error here for debug */
	if (xt_inject > 1)
		xt_inject--;
	if (xt_inject && (debug_cnt & 0x7f) == 0x7f)
		if (USE_DMA(last_cmd)) {
			err = XTE_HARD_ERROR;
			unitp->c_error = (u_short) err;
			xt_inject = 6;
		}
	switch (err) {
	/* case XTE_REVERSE_INTO_BOT: */
	case XTE_INTERRUPT_PENDING:
	case XTE_BUSY_CONFLICT:
	case XTE_PARITY_ERROR:
	case XTE_SLAVE_ACK_ERROR:
	case XTE_TAPE_MARK_FAILURE:
		cmn_err(CE_WARN, "xt%d: Fatal Error, Code=%x", xtc, err);
		unitp->c_next_cmd = CMD_ABORT;
		/* CMD_ABORT should cause the device to close */
		break;
	case XTE_OPERATION_TIMEOUT:
	case XTE_HARD_ERROR:
	case XTE_DATA_LATE_DETECTED:
		cmn_err(CE_WARN, "xt%d: Retry Error, Code=%x", xtc, err);
		xt_state2(unitp);
		if (USE_DMA(last_cmd))
			unitp->c_next_cmd = CMD_RETRY;
		else
			unitp->c_next_cmd = CMD_ABORT;
		/* (CMD_RETRY used *ONLY* to pass to xt_next_cmd()) */
		break;
	case XTE_WRITE_PROTECT_ERR:
		cmn_err(CE_NOTE, "xt%d: Write Ring Missing", xtc);
		unitp->c_next_cmd = CMD_ABORT;
		break;
	case XTE_DRIVE_OFF_LINE:
		cmn_err(CE_NOTE, "xt%d: Drive Off Line", xtc);
		unitp->c_next_cmd = CMD_ABORT;
		break;
	case XTE_TAPE_MARK_ON_READ:
		break;
	case XTE_REC_LENGTH_SHORT:
		if (!USE_DMA(last_cmd)) {
			bp->b_flags |= B_ERROR;
			unitp->c_bp_error = geterror(bp);
		}
		break;
	case XTE_REC_LENGTH_LONG:
		cmn_err(CE_NOTE, "xt%d: Record Length Long", xtc);
		bp->b_flags |= B_ERROR;
		bp->b_error = EINVAL;
		unitp->c_recno++;	/* this rec has been read */
		break;
	default:
		break;
	}
} /* end check_errors */

/*
 * Take actions for last command, and Compute next command
 * unitp->c_next_cmd and other variables in unitp->  are updated.
 * The order here is important: check RETRY, then EOT, then DONE
 */
static void
xt_next_cmd(struct xtunit *unitp)
{
	register struct xtiopb *xt = unitp->c_iopb;
	int last_cmd = unitp->c_last_cmd;
	int stat = unitp->c_xtstat;
	int old_recno = unitp->c_recno;
	int old_fileno = unitp->c_fileno;

	unitp->c_next_cnt = 1;

	/*
	 * Set next command (unitp->c_next_cmd) in case of retry.
	 */
	if (unitp->c_next_cmd == CMD_RETRY) {
		switch (last_cmd) {

		case CMD_WRITE:
			unitp->c_next_cmd = CMD_BACK_REC_W;
			break;
		case CMD_READ:
			unitp->c_next_cmd = CMD_BACK_REC_R;
			break;
		default:
			unitp->c_next_cmd = unitp->c_last_cmd;
			break;
		}
	}

	/*
	 * EOT handling: we let user read/write pass EOT. In READ, EOT is
	 * transparent to the users. In WRITE, users will be notified at
	 * the first time WRITE past EOT by 0 byte count return .
	 */
	if (stat & XTS_EOT && unitp->c_last_cmd == CMD_WRITE) {
		unitp->c_next_cmd = CMD_BACK_REC_W_EOT;
		PRINTF("W_EOM\n");
	}

	/*
	 * Update variables, if we are done
	 */
	if (unitp->c_next_cmd == CMD_DONE) {
		switch (last_cmd) {

		case CMD_READ:
			if (unitp->c_xtstat & XTS_FMK) {
				if (unitp->c_fileno != 0 &&
				    (unitp->c_recno == 0 ||
				    unitp->c_read_hit_eof)) {
					unitp->c_eom = 1;
					unitp->c_next_cmd = CMD_BACK_MARK_R_EOF;
				} else {
					if (unitp->c_svr4) {
						unitp->c_next_cmd =
						    CMD_BACK_MARK_R_EOF;
					}
					unitp->c_read_hit_eof = 1;
				}
			} else
				unitp->c_recno++;
			unitp->c_lstpos += xt->xt_acnt;
			break;
		case CMD_WRITE:
			unitp->c_recno++;
			unitp->c_lstpos += xt->xt_acnt;
			break;
		case CMD_SKIP_REC:
			unitp->c_resid = xt->xt_cnt - xt->xt_acnt;
			unitp->c_recno += xt->xt_acnt;
			if (unitp->c_xtstat & XTS_FMK) {
				unitp->c_next_cmd = CMD_BACK_MARK_FSR_EOF;
			}
			break;
		case CMD_BACK_REC:
			unitp->c_resid = xt->xt_cnt - xt->xt_acnt;
			unitp->c_recno -= xt->xt_acnt;
			if (unitp->c_xtstat & XTS_FMK) {
				unitp->c_next_cmd = CMD_SKIP_MARK_BSR_EOF;
			}
			break;
		case CMD_SKIP_MARK:
			unitp->c_fileno += (daddr_t) xt->xt_acnt;
			unitp->c_recno = 0;
			break;
		case CMD_BACK_MARK:
			unitp->c_fileno -= (daddr_t) xt->xt_acnt;
			unitp->c_recno = INFINITY;
			if (unitp->c_fileno == 0 &&
			    (unitp->c_xtstat & XTS_BOT))
				unitp->c_recno = 0;
			break;
		case CMD_REWIND:
		case CMD_UNLOAD:
			unitp->c_recno = 0;
			unitp->c_fileno = 0;
			break;
		case CMD_WRITE_MARK:
			unitp->c_fileno++;		/* 1 at a time */
			unitp->c_recno = 0;
			unitp->c_fileno_eom = unitp->c_fileno;
			break;
		case CMD_SHORT_ERASE_W:
			/* ready to write again */
			unitp->c_next_cmd = CMD_WRITE;
			break;
		case CMD_BACK_REC_R:
			/* ready to read  again */
			unitp->c_next_cmd = CMD_READ;
			break;
		case CMD_BACK_REC_W:
			/* erase before rewrite */
			unitp->c_next_cmd = CMD_SHORT_ERASE_W;
			break;
		case CMD_BACK_REC_W_EOT:
			break;
		default:
			break;
		}
	}
	if (old_recno != unitp->c_recno || old_fileno != unitp->c_fileno)
		unitp->c_read_hit_eof = 0;
} /* end xt_next_cmd() */

static void
xt_update_resid(struct xtunit *unitp)
{
	register struct xtiopb *xt = unitp->c_iopb;
	int last_cmd = unitp->c_last_cmd;
	int next_cmd = unitp->c_next_cmd;
	long resid = unitp->c_resid;

	/* for IOCTLs */
	switch (last_cmd) {
	case CMD_REWIND:
		break;			/* keep previous resid */
	case CMD_UNLOAD:
		resid = 0;		/* reinit resid */
		break;
	case CMD_STATUS:
		PRINTF("file no= %d  block no= %d\n",
			unitp->c_fileno, unitp->c_recno);
		break;			/* keep previous resid */
	case CMD_SKIP_REC:
	case CMD_BACK_REC:
	case CMD_SKIP_MARK:
	case CMD_BACK_MARK:
		resid = (long) xt->xt_cnt - (long) xt->xt_acnt;
	default:
		break;			/* keep previous resid */
	}

	/* for DMA */
	if (unitp->c_udma_handle) {
		if (unitp->c_error == XTE_REC_LENGTH_LONG ||
		    unitp->c_error == XTE_TAPE_MARK_ON_READ ||
		    last_cmd == CMD_BACK_REC_W_EOT ||
		    last_cmd == CMD_BACK_MARK_R_EOF ||
		    next_cmd == CMD_ABORT)
			resid = unitp->c_dma_buf->b_bcount;
		else
			resid = xt->xt_cnt - xt->xt_acnt;
		unitp->c_dma_buf->b_resid = resid;
	}
	unitp->c_resid = resid;

	if (resid < (long) 0)
		cmn_err(CE_WARN, "resid <0 SHOULD NOT HAPPEN (%ld)\n",
			resid);
} /* end xt_update_resid() */

static void
xt_timer(dev_t dev)
{
	register struct xtunit *unitp = search_unit(dev);

	mutex_enter(&unitp->c_mutex);
	if (unitp->c_transfer && unitp->c_timo != INF) {
	PRINTF("\ntimeout = %d", unitp->c_timo);
		if ((unitp->c_timo -= 2) <= 0) {
			cmn_err(CE_WARN, "xt%d: cmd timed out", unitp->c_ctlr);
			unitp->c_stuck = 1;
			unitp->c_transfer = 0;
			cv_signal(&unitp->c_transfer_wait);
		}
	}

	/*
	 * Reschedule timer 2 sec. later unless we are in the process of
	 * detatching the device, then don't do any more timeouts
	 */
	if ((unitp->c_curr_state & XT_DETACHING) == 0) {
		unitp->c_timeout_id = timeout(xt_timer, (caddr_t)dev,
			drv_usectohz((clock_t) XT_TIMER_INTERVAL));
	}
	mutex_exit(&unitp->c_mutex);
} /* end xt_timer */

/*
 * Wait for controller csr to become valid.
 * Waits for at most 200 usec. Returns true if wait succeeded.
 */
static int
xt_csrvalid(volatile struct xydevice *xyio)
{
	register int i;

	drv_usecwait(100);
	for (i = 10; i && (xyio->xy_csr & (XY_BUSY|XY_DBLERR)); i--)
		drv_usecwait(20);
	drv_usecwait(15);
	return ((xyio->xy_csr & (XY_BUSY|XY_DBLERR)) == 0);
}

#ifdef	THIS_CODE_IS_NOT_USED
/*
 * Return csr (after waiting for controller csr to become valid)
 */
static int
xt_csr(volatile struct xydevice *xyio)
{
	register int i;

	drv_usecwait(15);
	for (i = 10; i && (xyio->xy_csr & (XY_BUSY|XY_DBLERR)); i--)
		drv_usecwait(10);
	drv_usecwait(15);
	return (xyio->xy_csr);
}
#endif	THIS_CODE_IS_NOT_USED

/*
 * Wait for controller to become ready. Used after reset or interrupt.
 * Waits for at most 0.1 sec. Returns true if wait succeeded.
 */
static int
xt_wait(volatile struct xydevice *xyio)
{
	register int i;

	drv_usecwait(100);
	for (i = 10000; i && (xyio->xy_csr & XY_BUSY); i--)
		drv_usecwait(20);
	drv_usecwait(15);
	return ((xyio->xy_csr & XY_BUSY) == 0);
}

static void
xt_minphys(struct buf *bp)
{
	if (bp->b_bcount > MINPHYS_BYTES)
		bp->b_bcount = MINPHYS_BYTES;
}

/*ARGSUSED*/
static int
xt_read(dev_t dev, struct uio *uio, cred_t *cred_p)
{
	register struct xtunit *unitp = search_unit(dev);

	PRINTF("\n::xt_read");
	if (unitp->c_alive == 0)
		return (ENXIO);
	if (xt_flush_eom(dev, EOM_TWO_EOFS))
		return (EIO);
	return (physio(xt_strategy, (struct buf *) 0, dev, B_READ,
	    xt_minphys, uio));
}

/*ARGSUSED*/
static int
xt_write(dev_t dev, struct uio *uio, cred_t *cred_p)
{
	register struct xtunit *unitp = search_unit(dev);

	PRINTF("\n::xt_write");
	if (unitp->c_alive == 0)
		return (ENXIO);
	return (physio(xt_strategy, (struct buf *) 0, dev, B_WRITE,
	    xt_minphys, uio));
}

/*
 * IOCTL COMMANDS
 * NOTE: do not change order of tmops[] constants
 */
static tmops[] = {
	(XT_FMARK<<8), 				/* MTWEOF */
	(XT_SEEK<<8)+XT_SKIP_MARK, 		/* MTFSF */
	(XT_SEEK<<8)+XT_BACK_MARK, 		/* MTBSF */
	(XT_SEEK<<8)+XT_SKIP_REC, 		/* MTFSR */
	(XT_SEEK<<8)+XT_BACK_REC, 		/* MTBSR */
	(XT_SEEK<<8)+XT_REWIND, 		/* MTREW */
	(XT_SEEK<<8)+XT_UNLOAD, 		/* MTOFFL */
	(XT_DSTAT<<8), 				/* MTNOP */
	(XT_NOP<<8), 				/* MTRETEN */
	(XT_FMARK<<8)+XT_ERASE, 		/* MTERASE */
	(XT_NOP<<8), 				/* MTEOM */
	(XT_SEEK<<8)+XT_BACK_MARK, 		/* MTNBSF */
};
#define	COPYOUT(a, b, c, f)	\
	ddi_copyout((caddr_t) (a), (caddr_t) (b), sizeof (c), f)
#define	COPYIN(a, b, c, f)	\
	ddi_copyin((caddr_t) (a), (caddr_t) (b), sizeof (c), f)

/*
 * IOCTL CALLS
 */
/*ARGSUSED*/
static int
xt_ioctl(dev_t dev, int cmd, int arg, int flag, cred_t *cred_p, int *rval_p)
{
	register struct xtunit *unitp = search_unit(dev);
	register int callcount;
	auto long data[512 / (sizeof (long))];
	int fcount, op, stat;
	int big_fsfs = 0;
	struct mtop *mtop;
	struct mtget *mtget;
	int flush = NO_EOM;	/* default flush value */
	u_char negative_bsfs = 0;

	PRINTF("\n::xt_ioctl");
	switch (cmd) {

	case MTIOCTOP:	/* tape operation */
		if (COPYIN(arg, data, struct mtop, flag))
			return (EFAULT);
		mtop = (struct mtop *)data;

		switch (mtop->mt_op) {
		case MTFSF:
			if (mtop->mt_count > 0 && unitp->c_read_hit_eof &&
			    !unitp->c_svr4) {
				(void) xt_command(dev, XT_SEEK,
					XT_BACK_MARK, 1);
				unitp->c_read_hit_eof = 0;
			}
			break;
		case MTBSR:
			if (mtop->mt_count > 0 && unitp->c_read_hit_eof) {
					unitp->c_fileno++;
					unitp->c_recno = 0;
					unitp->c_read_hit_eof = 0;
			}
			break;
		case MTBSF:
		case MTFSR:
		case MTNBSF:
			if (unitp->c_read_hit_eof) {
				(void) xt_command(dev, XT_SEEK,
					XT_BACK_MARK, 1);
				unitp->c_read_hit_eof = 0;
			}
			break;
		case MTREW:
			break;
		case MTOFFL:
			break;
		case MTNOP:
			break;
		case MTERASE:
			break;
		case MTWEOF:
			break;
		default:
			break;
		} /* end switch mtop */

	case MTIOCGET:
		break;
	default:
		break;
	} /* end switch cmd */



	switch (cmd) {

	case MTIOCTOP:	/* tape operation */

		if (COPYIN(arg, data, struct mtop, flag))
			return (EFAULT);
		mtop = (struct mtop *)data;

		if (mtop->mt_count < 0) {
			if (mtop->mt_op == MTFSF) {
				mtop->mt_count = - mtop->mt_count;
				mtop->mt_op = MTNBSF;
			}
		}
		if (mtop->mt_count <= 0) {
			if (mtop->mt_op == MTNBSF) {
				mtop->mt_count = - mtop->mt_count;
				mtop->mt_op = MTFSF;
			}
		}
		switch (mtop->mt_op) {
		case MTFSF:
			if (mtop->mt_count > 0)
				big_fsfs = 1;
			/*
			 * For ASF and compatibility with st driver,
			 * we allow a count of 0 which means we just
			 * want to go to beginning of current file.
			 * Equivalent to "nbsf(0)" or "bsf(1) + fsf".
			 */
#ifdef XTDEBUG
			for_debug(mtop);
			if (mtop->mt_count > 32750)
				return (0);
#endif
			callcount = 1;
			if ((fcount = mtop->mt_count) == 0) {
				/* XXX bsf 0  is NOT nbsf 1, FIX it */
				fcount++;
				mtop->mt_op = MTNBSF;
			}
			flush = EOM_TWO_EOFS;
			break;

		case MTBSF:
			callcount = 1;
			if ((fcount = mtop->mt_count) == 0) {
				/* XXX bsf 0  is NOT nbsf 1, FIX it */
				fcount++;
				mtop->mt_op = MTNBSF;
			}
			if (mtop->mt_count < 0) {
				negative_bsfs = 1;
				fcount = - mtop->mt_count;
			}
			flush = EOM_TWO_EOFS;
			break;

		case MTNBSF:
			callcount = 1;
			/*
			 * NBSF(n) == BSF(n+1) + FSF
			 */
			fcount = mtop->mt_count + 1;
			flush = EOM_TWO_EOFS;
			break;

		case MTFSR:
		case MTBSR:
			callcount = 1;
			/*
			 * For compatibility with st driver, 0 count is NOP
			 */
			if ((fcount = mtop->mt_count) == 0) {
				if (COPYOUT(data, arg, struct mtop, flag))
					return (EFAULT);
				else
					return (0);
			}
			flush = EOM_TWO_EOFS;
			break;

		case MTREW:
		case MTOFFL:
		case MTNOP:
			callcount = 1;
			fcount = mtop->mt_count;
			flush = EOM_NO_EOF;
			break;

		case MTERASE:
			if ((unitp->c_open_opt & FWRITE) == 0)
				return (EACCES);
			if (xt_command(dev, XT_SEEK, XT_REWIND, 1))
				return (EIO);
			callcount = FOREVER;
			fcount = 1;
			break;

		case MTWEOF:
			if ((unitp->c_open_opt & FWRITE) == 0)
				return (EACCES);
			callcount = mtop->mt_count;
			fcount = 1;
			if (callcount == 1) {
				/* always write one extra for EOM */
				if (xt_wrt_eom(dev, EOM_ONE_EOF)) {
					return (EIO);
				} else
					goto opdone;
			}
			break;

		case MTEOM:
			if (xt_flush_eom(dev, EOM_TWO_EOFS))
				return (EIO);

			big_fsf(dev, INFINITY);
			if (unitp->c_resid)
				return (EIO);
			if (COPYOUT(data, arg, struct mtop, flag))
				return (EFAULT);
			else
				return (0);

		default:
			return (ENOTTY);
		} /* end switch mtop */

		if (callcount <= 0 || fcount <= 0)
			return (ENOTTY);

		if (flush != NO_EOM && xt_flush_eom(dev, flush))
			return (EIO);

		/* special case "fsf 0" or "nbsf n" to begin of file */
		if (mtop->mt_op == MTNBSF) {
			unitp->c_resid = fcount - unitp->c_fileno - 1;
			if (fcount - 1 == unitp->c_fileno ||
			    (fcount - 2 == unitp->c_fileno &&
			    unitp->c_read_hit_eof)) {
				unitp->c_resid = 0;
				mtop->mt_op = MTREW;
				fcount = 1;
			}
			if (fcount - 1 > unitp->c_fileno) {
				mtop->mt_op = MTBSF;
				fcount--;
			}
		}

		if (big_fsfs) {
			big_fsf(dev, fcount);
			if (unitp->c_resid)
				return (EIO);
		} else
		if (negative_bsfs) {
			negative_bsf(dev, fcount);

		} else
		/* XXX fix this formatting */

		while (--callcount >= 0) {
			op = tmops[mtop->mt_op];
			stat = xt_command(dev, op >> 8, op & 0xFF, fcount);

			/*
			 * Stall at EOF.
			 */
#ifdef stall_at_eof_eom
			save_resid = unitp->c_resid;
			if (mtop->mt_op == MTBSR && unitp->c_xtstat & XTS_FMK) {
				(void) xt_command(dev, XT_SEEK,
					XT_SKIP_MARK, 1);
			}

			if (mtop->mt_op == MTFSR && unitp->c_xtstat & XTS_FMK) {
				(void) xt_command(dev, XT_SEEK,
					XT_BACK_MARK, 1);
			}
			unitp->c_resid = save_resid;
#endif
			if (stat)
				return (EIO);
			/*
			 * stop erase, otherwise, it would rip the
			 *  tape off the wheel.
			 */
			if ((unitp->c_xtstat & XTS_EOT) &&
			    (mtop->mt_op == MTERASE) &&
			    callcount > ERASE_AFT_EOT) {
				callcount = ERASE_AFT_EOT;
			}
		}
		if (mtop->mt_op == MTERASE) {
			if (xt_command(dev, XT_SEEK, XT_REWIND, 1))
				return (EIO);
			break;
		}
		if (mtop->mt_op == MTNBSF) {
			/* skip over EOF for NBSF */
			op = tmops[MTFSF];
			if (xt_command(dev, op >> 8, op & 0xFF, 1))
				return (EIO);
			break;
		}

opdone:
		/* do extra accounting work */
		if (mtop->mt_op == MTWEOF)
			unitp->c_recno = 0;
		break;

	case MTIOCGET:

		if (COPYIN(arg, data, struct mtget, flag))
			return (EFAULT);
		mtget = (struct mtget *)data;
		mtget->mt_dsreg = unitp->c_xtstat;
		mtget->mt_erreg = unitp->c_error;
		mtget->mt_resid = unitp->c_resid;
		mtget->mt_type = MT_ISXY;
		if (unitp->c_xtstat & XTS_BOT) {
			/*
			 * Foolproofing in case there's manual intervention
			 * in the middle of a program which is not well-
			 * behaved.  (e.g., suninstall doesn't always issue
			 * offline/rewind the tape when it should.)  This
			 * really belongs only in xt_open, but as long as
			 * we're foolproofing, we might as well allow manual
			 * intervention in a program which does its own ioctls.
			 */
			unitp->c_fileno = 0;
			unitp->c_recno = 0;
		}
		mtget->mt_fileno = unitp->c_fileno;
		mtget->mt_blkno = unitp->c_recno;

		/*
		 * Note that ASF has really only been implemented
		 * to the extent that it will support suninstall.
		 */
		mtget->mt_flags = MTF_REEL | MTF_ASF;
		mtget->mt_bf = 20;
		PRINTF("\n|		POS= %d/%d>",
		    unitp->c_fileno, unitp->c_recno);
		break;

	default:
		return (ENOTTY);

	} /* end switch cmd */

	if (cmd == MTIOCTOP) {
		if (COPYOUT(data, arg, struct mtop, flag))
			return (EFAULT);
	} else {
		if (COPYOUT(data, arg, struct mtget, flag))
			return (EFAULT);
	}
	return (0);
} /* end xt_ioctl */


#ifdef	THIS_CODE_IS_NOT_USED
static int
xt_fwd_rec(dev_t dev, int count)
{
	register struct xtunit *unitp = search_unit(dev);
	int sv_resid;

	/*
	 * a record length short status is posted when the
	 * 472 detects a tape mark on space record forward
	 */
	if (xt_command(dev, XT_SEEK, XT_SKIP_REC, count)) {
		if (unitp->c_error == XTE_REC_LENGTH_SHORT ||
		    unitp->c_error == XTE_EOT_DETECTED) {
			sv_resid = unitp->c_resid;
			/* leave tape positioned between the two EOFs */
			(void) xt_command(dev, XT_SEEK, XT_BACK_MARK, 1);
			unitp->c_resid = sv_resid;
			unitp->c_error = XTE_REC_LENGTH_SHORT;
			unitp->c_fileno_eom = unitp->c_fileno;
		}
		return (EIO);
	} else
		return (0);
}
#endif	THIS_CODE_IS_NOT_USED

#ifdef	THIS_CODE_IS_NOT_USED
static int
xt_chk_file(dev_t dev)
{
	int stat;
	register struct xtunit *unitp = search_unit(dev);

	/*
	 * If we're just after EOF (recno == 0) and we just
	 * encountered another EOF, then we've found the EOM.
	 */
	if (unitp->c_recno == 0 && xt_fwd_rec(dev, 1)) {
		if (unitp->c_error == XTE_REC_LENGTH_SHORT)
			return (-1);	/* empty file found!! */
		else
			return (EIO);
	}
	/* not an empty file, skip it */
	stat = xt_command(dev, XT_SEEK, XT_SKIP_MARK, 1);
	return (stat);
}
#endif	THIS_CODE_IS_NOT_USED

/*
 * fsf n:  if count = INFINITY, we look for EOM.
 * return 0 for ok, 1 for EOM reached, -1 for EOT reached
 * -2 for real failure (individual command).
 */
static void
big_fsf(dev_t dev, int count)
{
	int i, actual;
	daddr_t previous_recno;
	register struct xtunit *unitp = search_unit(dev);

	actual = 0;
	for (i = 0; i < count; i++) {
		previous_recno = unitp->c_recno;
		(void) xt_command(dev, XT_SEEK, XT_SKIP_REC, 1);
		if (unitp->c_fileno == 0 ||
		    previous_recno || unitp->c_recno) {
			(void) xt_command(dev, XT_SEEK, XT_SKIP_MARK, 1);
			actual++;
			if (unitp->c_xtstat & XTS_EOT)
				break;
		} else {
			unitp->c_eom = 1;
			break;
		}
	}
	if (count != INFINITY)
		unitp->c_resid = count - actual;
	else
		unitp->c_resid = 0;
}

/*
 * bsf neg_value equivalent to fsf (-x + 1) and bsf (1)
 */
static void
negative_bsf(dev_t dev, int count)
{
	big_fsf(dev, count + 1);
	(void) xt_command(dev, XT_SEEK, XT_BACK_MARK, 1);
}

/*
 * Give xt state (for debugging only)
 */
static void
xt_state(char char1, struct xtunit *unitp)
{
	volatile struct xydevice *xyio = unitp->c_io;
	register struct xtiopb *xt = unitp->c_iopb;
	register unsigned char *ptr = (unsigned char *)xt;
	int timeout;

	timeout = unitp->c_timo;
	if (timeout == INF)
		timeout = 0xff;
	PRINTF("%c $%x IOPB=%x %x %x %x %x %x %x:%x TMO=%x CSR=%x F%d R%d\n",
	    char1, unitp->c_last_cmd, *(ptr+1), *ptr, *(ptr + 3), *(ptr + 2),
	    *(ptr + 5), *(ptr + 4), xt->xt_cnt, xt->xt_acnt, timeout,
	    xyio->xy_csr, unitp->c_fileno, unitp->c_recno);
}

/*
 * Give xt state (called when an error occurs, timeout, etc..)
 */
static void
xt_state2(struct xtunit *unitp)
{
	register struct xtiopb *xt = unitp->c_iopb;
	register unsigned char *ptr0 = (unsigned char *) xt;
	register unsigned char *ptr1 = (unsigned char *) &unitp->c_old1_iopb;
	register unsigned char *ptr2 = (unsigned char *) &unitp->c_old2_iopb;

	cmn_err(CE_CONT, "\t$%x OLD2 IOPB=%x %x %x %x %x %x %x:%x\n",
	    unitp->c_old2_cmd, *(ptr2+1), *ptr2, *(ptr2 + 3), *(ptr2 + 2),
	    *(ptr2 + 5), *(ptr2 + 4), unitp->c_old2_cnt, unitp->c_old2_acnt);

	cmn_err(CE_CONT, "\t$%x OLD1 IOPB=%x %x %x %x %x %x %x:%x\n",
	    unitp->c_old1_cmd, *(ptr1+1), *ptr1, *(ptr1 + 3), *(ptr1 + 2),
	    *(ptr1 + 5), *(ptr1 + 4), unitp->c_old1_cnt, unitp->c_old1_acnt);

	cmn_err(CE_CONT, "\t$%x LAST IOPB=%x %x %x %x %x %x %x:%x\n",
	    unitp->c_last_cmd, *(ptr0+1), *ptr0, *(ptr0 + 3), *(ptr0 + 2),
	    *(ptr0 + 5), *(ptr0 + 4), xt->xt_cnt, xt->xt_acnt);
}

#ifdef XTDEBUG
static void
for_debug(struct mtop *mtop)
{
	switch (mtop->mt_count) {
	case 32767:	/* ext trace ON	 */
		xt_trace_mode = 2;
		xt_limit_trace = 0;
		break;
	case 32766:	/* ext trace OFF */
		xt_trace_mode = 1;
		xt_rand_trace = 0;
		xt_inject = 0;
		xt_select = 0;
		xt_limit_trace = 0;
		break;
	case 32765:	/* ext trace random */
		xt_trace_mode = 2;
		xt_rand_trace = 1;
		break;
	case 32764:	/* ext SHORT trace (only lines > 25) */
		xt_trace_mode = 2;
		xt_limit_trace = 25;
		break;
	case 32763:	/* Inject a hard error, once a while */
		xt_inject = 1;
		break;
	case 32762:	/* Select all lines starting with ^ */
		xt_trace_mode = 2;
		xt_select = 1;
		xt_limit_trace = 200;
		break;
	default:
		break;
	}
}

static	void
xxprintf(char *fmt, ...)
{
	/* char *vsprintf(char *buf, char *fmt, va_list adx); */
	va_list adx;
	int j;
	u_char size;

	mutex_enter(&xt_trace_mutex);
	va_start(adx, fmt);

	if (xt_trace_mode > 0) {
		(void) vsprintf(&xt_trace[xt_to_trace], fmt, adx);
		j = 0;
		xt_itrace = xt_to_trace;
		while (xt_trace[xt_to_trace]) {
			j++;
			xt_to_trace++;
		}
		size = (u_char) j;
		if (j > MAXLINESIZE || xt_trace[TRACE_SIZE])
			cmn_err(CE_WARN,
			"sprintf output > MAXLINESIZE");
		if (xt_trace[TRACE_SIZE + RED_ZONE -1])
			cmn_err(CE_PANIC,
			"sprintf output > MAXLINESIZE + RED_ZONE");
		xt_trace[xt_to_trace] = '\n';

		if (xt_to_trace < TRACE_SIZE - MAXLINESIZE) {
			for (j = 1; j < 79; j++) {
				if (j < 41)
					xt_trace[xt_to_trace + j] = 'E';
				if (j < 2 || j == 39 || j == 40)
					xt_trace[xt_to_trace + j] = '\n';
				if (j > 40 && xt_trace[xt_to_trace + j]) {
					xt_trace[xt_to_trace + j] = 'B';
					if (j == 78)
					    xt_trace[xt_to_trace + j] = '\n';
				}
			}
		} else {
			for (j = 1; j < MAXLINESIZE; j++) {
				if (xt_to_trace + j >= TRACE_SIZE)
					break;
				xt_trace[xt_to_trace + j] = 'W';
				if (j < 2 || (j % 60) == 0)
					xt_trace[xt_to_trace + j] = '\n';
			}

			/*
			 * Wrap around to beginning of buffer.
			 */
			xt_to_trace = 0;
		}
	}
	if (xt_trace_mode == 2 || xt_inject > 1) {
		/* eventually add a condition on xt_trace[xt_itrace + x] */
		if (!xt_rand_trace ||
		    (xt_rand_trace && (debug_cnt & 0x7f) == 0x7f))
			if (size > xt_limit_trace ||
			    (xt_select && xt_trace[xt_itrace + 1] == '^')) {
				vcmn_err(CE_CONT, fmt, adx);
			}
	}
	va_end(adx);
	mutex_exit(&xt_trace_mutex);
}

static void
xt_dump_io(dev_t dev)
{
	register struct xtunit *unitp = search_unit(dev);
	PRINTF("\t\t>>> CSR = %x\n", unitp->c_io->xy_csr);
}

static void
xt_dump_iopb(dev_t dev)
{
	register struct xtunit *unitp;
	register struct xtiopb *xt;
	register unsigned char *ptr;
	int	cnt;

	unitp = search_unit(dev);

	xt = unitp->c_iopb;

	PRINTF("\n\t\t..I O P B ..\n");

	ptr = (unsigned char *) xt;

	for (cnt = 0; cnt < 18; cnt += 2) {
		if (*(ptr+1))
		PRINTF("\t\t> b%x = %x\n", cnt, *(ptr+1));
		if (*ptr)
		PRINTF("\t\t> b%x = %x\n", cnt+1, *ptr);
		ptr += 2;
	}
}

/*
 * for debugging: usage : XT_TRACE xt_dump_buf(bp);
 *			  XT_TRACE xt_dump_xtunit(dev);
 */
static void
xt_dump_buf(struct buf *b)
{

	PRINTF("\n\t\t...............B U F...(%x)\n", b);
	if (b->b_flags)
	PRINTF("\t\t>>> b->b_flags	 =  %x\n", b->b_flags);
	if (b->b_forw)
	PRINTF("\t\t>>> b->b_forw	 =  %x\n", b->b_forw);
	if (b->b_back)
	PRINTF("\t\t>>> b->b_back	 =  %x\n", b->b_back);
	if (b->av_forw)
	PRINTF("\t\t>>> b->av_forw	 =  %x\n", b->av_forw);
	if (b->av_back)
	PRINTF("\t\t>>> b->av_back	 =  %x\n", b->av_back);
	if (b->b_dev)
	PRINTF("\t\t>>> b->b_dev	 =  %x\n", b->b_dev);
	if (b->b_bcount)
	PRINTF("\t\t>>> b->b_bcount	 =  %x\n", b->b_bcount);
	if (b->b_un.b_addr)
	PRINTF("\t\t>>> b->b_un.b_addr	 =  %x\n", b->b_un.b_addr);
	if (b->b_blkno)
	PRINTF("\t\t>>> b->_b_blkno	 =  %x\n", b->_b_blkno);
	if (b->b_oerror)
	PRINTF("\t\t>>> b->b_oerror	 =  %x\n", b->b_oerror);
	if (b->b_resid)
	PRINTF("\t\t>>> b->b_resid	 =  %x\n", b->b_resid);
	if (b->b_start)
	PRINTF("\t\t>>> b->b_start	 =  %x\n", b->b_start);
	if (b->b_proc)
	PRINTF("\t\t>>> b->b_proc	 =  %x\n", b->b_proc);
	if (b->b_pages)
	PRINTF("\t\t>>> b->b_pages	 =  %x\n", b->b_pages);
	if (b->b_reltime)
	PRINTF("\t\t>>> b->b_reltime	 =  %x\n", b->b_reltime);
	if (b->b_bufsize)
	PRINTF("\t\t>>> b->b_bufsize	 =  %x\n", b->b_bufsize);
	if (b->b_iodone)
	PRINTF("\t\t>>> b->b_iodone	 =  %x\n", b->b_iodone);
	if (b->b_vp)
	PRINTF("\t\t>>> b->b_vp		 =  %x\n", b->b_vp);
	if (b->b_chain)
	PRINTF("\t\t>>> b->b_chain	 =  %x\n", b->b_chain);
	if (b->b_reqcnt)
	PRINTF("\t\t>>> b->b_reqcnt	 =  %x\n", b->b_reqcnt);
	if (b->b_error)
	PRINTF("\t\t>>> b->b_error	 =  %x\n", b->b_error);
	if (b->b_edev)
	PRINTF("\t\t>>> b->b_edev	 =  %x\n", b->b_edev);

} /* end xt_dump_buf() */

static void
xt_dump_xtunit(dev_t dev)
{
	register struct xtunit *unitp = search_unit(dev);

	PRINTF("\n\t\t..........X T U N I T..(%x)\n", unitp);
	if (unitp->c_sbufp)
	PRINTF("\t\t>>> sbufp		 =  %x\n", unitp->c_sbufp);
	if (unitp->c_bp_used)
	PRINTF("\t\t>>> bp_used		 =  %x\n", unitp->c_bp_used);
	if (unitp->c_udma_handle)
	PRINTF("\t\t>>> udma_handle	 =  %x\n", unitp->c_udma_handle);
	if (unitp->c_xtstat)
	PRINTF("\t\t>>> sc_xtstat	 =  %x\n", unitp->c_xtstat);
	if (unitp->c_error)
	PRINTF("\t\t>>> sc_error	 =  %x\n", unitp->c_error);
	if (unitp->c_resid)
	PRINTF("\t\t>>> sc_resid	 =  %x\n", unitp->c_resid);
	if (unitp->c_timo)
	PRINTF("\t\t>>> sc_timo		 =  %x\n", unitp->c_timo);
	if (unitp->c_tact)
	PRINTF("\t\t>>> sc_tact		 =  %x\n", unitp->c_tact);
	if (unitp->c_fileno)
	PRINTF("\t\t>>> sc_fileno	 =  %x\n", unitp->c_fileno);
	if (unitp->c_recno)
	PRINTF("\t\t>>> sc_recno	 =  %x\n", unitp->c_recno);
	if (unitp->c_open_opt)
	PRINTF("\t\t>>> sc_open_opt	 =  %x\n", unitp->c_open_opt);
	if (unitp->c_transfer)
	PRINTF("\t\t>>> c_transfer	 =  %x\n", unitp->c_transfer);
	if (unitp->c_io)
	PRINTF("\t\t>>> c_io		 =  %x\n", unitp->c_io);
	if (unitp->c_iopb)
	PRINTF("\t\t>>> c_iopb		 =  %x\n", unitp->c_iopb);
}

#else

/*ARGSUSED*/
static	void
xxprintf(char *fmt, ...)
{
}

#endif
