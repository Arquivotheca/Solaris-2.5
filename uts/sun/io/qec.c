/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */
#pragma	ident	"@(#)qec.c 1.22     95/10/13 SMI"

/*
 * SunOS MT STREAMS Nexus Device Driver for QuadMACE and BigMAC
 * Ethernet deives.
 */

#include	<sys/types.h>
#include	<sys/debug.h>
#include	<sys/stropts.h>
#include	<sys/stream.h>
#include	<sys/cmn_err.h>
#include	<sys/vtrace.h>
#include	<sys/kmem.h>
#include	<sys/ddi.h>
#include	<sys/sunddi.h>
#include	<sys/strsun.h>
#include	<sys/stat.h>
#include	<sys/kstat.h>
#include	<sys/ethernet.h>
#include	<sys/qec.h>

#define	QECLIMADDRLO	(0x00000000)
#define	QECLIMADDRHI	(0xffffffff)
#define	QECBUFSIZE	1024	/* size of dummy buffer for dvma burst */
#define	QECBURSTSZ	(0x7f)
/*
 * Function prototypes.
 */
static	int qecidentify(dev_info_t *);
static	int qecattach(dev_info_t *, ddi_attach_cmd_t);
static	u_int qecintr();
static	u_int qecgreset();
static	int qecburst(dev_info_t *);
static	void qecerror(dev_info_t *dip, char *fmt, ...);
static	int qecinit(dev_info_t *);

extern struct cb_ops no_cb_ops;

static struct bus_ops qec_bus_ops = {
	BUSO_REV,
	i_ddi_bus_map,		/* bus_map */
	i_ddi_get_intrspec,	/* bus_get_intrspec */
	i_ddi_add_intrspec,	/* bus_add_intrspec */
	i_ddi_remove_intrspec,	/* bus_remove_intrspec */
	i_ddi_map_fault,	/* bus_map_fault */
	ddi_dma_map,		/* bus_dma_map */
	ddi_dma_allochdl,
	ddi_dma_freehdl,
	ddi_dma_bindhdl,
	ddi_dma_unbindhdl,
	ddi_dma_flush,
	ddi_dma_win,
	ddi_dma_mctl,		/* bus_dma_ctl */
	ddi_ctlops,		/* bus_ctl */
	ddi_bus_prop_op		/* bus_prop_op */
};

/*
 * Device ops - copied from dmaga.c .
 */
static struct dev_ops qec_ops = {
	DEVO_REV,		/* devo_rev */
	0,			/* devo_refcnt */
	ddi_no_info,		/* devo_info */
	qecidentify,		/* devo_identify */
	nulldev,		/* devo_probe */
	qecattach,		/* devo_attach */
	nodev,			/* devo_detach */
	nodev,			/* devo_reset */
	&no_cb_ops,		/* driver operations */
	&qec_bus_ops		/* bus operations */
};

/*
 * Claim the device is ultra-capable of burst in the beginning.  Use
 * the value returned by ddi_dma_burstsizes() to actually set the QEC
 * global control register later.
 */
static ddi_dma_lim_t qec_dma_limits = {
	(u_long) QECLIMADDRLO,	/* dlim_addr_lo */
	(u_long) QECLIMADDRHI,	/* dlim_addr_hi */
	(u_int) QECLIMADDRHI,	/* dlim_cntr_max */
	(u_int) QECBURSTSZ,	/* dlim_burstsizes */
	0x1,			/* dlim_minxfer */
	1024			/* dlim_speed */
};

/*
 * Single private "global" lock for the few rare conditions
 * we want single-threaded.
 */
kmutex_t	qeclock;

/*
 * Patchable packet size for BigMAC.
 */
static	u_int	qecframesize = 0;

#include	 <sys/modctl.h>

extern struct mod_ops	mod_driverops;
/*
 * This is the loadable module wrapper.
 */

static struct modldrv modldrv = {
	&mod_driverops,	/* Type of module. This one is a driver */
	"QEC driver v1.22",	/* Name of the module. */
	&qec_ops,	/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modldrv, 0
};

int
_init(void)
{
	int	status;

	mutex_init(&qeclock, "qeclock lock", MUTEX_DRIVER, NULL);
	status = mod_install(&modlinkage);
	if (status != 0) {
		mutex_destroy(&qeclock);
		return (status);
	}
	return (0);
}

int
_fini(void)
{
	int	status;

	status = mod_remove(&modlinkage);
	if (status != 0)
		return (status);
	mutex_destroy(&qeclock);
	return (0);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

static int
qecidentify(dev_info_t *dip)
{
	if (strcmp(ddi_get_name(dip), "qec") == 0)
		return (DDI_IDENTIFIED);
	else
		return (DDI_NOT_IDENTIFIED);
}

static int
qecattach(dip, cmd)
dev_info_t	*dip;
ddi_attach_cmd_t	cmd;
{
	register struct qec_soft *qsp = NULL;
	volatile struct qec_global *qgp = NULL;
	ddi_iblock_cookie_t	c;
	caddr_t parp = NULL;
	void (**funp)() = NULL;

	if (cmd != DDI_ATTACH)
		return (DDI_FAILURE);

	if (ddi_slaveonly(dip) == DDI_SUCCESS) {
		qecerror(dip, "qec in slave only slot");
		return (DDI_FAILURE);
	}
	/*
	 * Allocate soft data structure
	 */
	qsp = kmem_zalloc(sizeof (struct qec_soft), KM_SLEEP);

	if (ddi_map_regs(dip, 0, (caddr_t *)&qgp, 0, 0)) {
		qecerror(dip, "unable to map registers");
		goto bad;
	}

	if (((qgp->control & QECG_CONTROL_MODE) != QECG_CONTROL_MACE) &&
		((qgp->control & QECG_CONTROL_MODE) != QECG_CONTROL_BMAC)) {
		qecerror(dip, "unknown chip identification");
		goto bad;
	}

	qsp->qs_globregp = qgp;
#ifdef	MPSAS
	qsp->qs_nchan = ddi_getprop(DDI_DEV_T_NONE, dip, 0, "nchannels", 0);
	printf("qsp->qs_nchan == %d\n", qsp->qs_nchan);
#else	MPSAS
	qsp->qs_nchan = ddi_getprop(DDI_DEV_T_NONE, dip, 0, "#channels", 0);
#endif	MPSAS

	if (qecgreset(qgp)) {
		qecerror(dip, "global reset failed");
		goto bad;
	}

	/*
	 * Allocate location to save function pointers that will be
	 * exported to the child.
	 * Export a pointer to the qec_soft structure.
	 */
	parp = kmem_alloc(sizeof (void *) * qsp->qs_nchan, KM_SLEEP);
	qsp->qs_intr_arg = (void **)parp;
	funp = (void (**)()) kmem_alloc(sizeof (void *) * qsp->qs_nchan,
		KM_SLEEP);
	qsp->qs_intr_func = funp;
	qsp->qs_reset_func = qecgreset;
	qsp->qs_reset_arg = (void *) qgp;
	qsp->qs_init_func = qecinit;
	qsp->qs_init_arg = dip;
	ddi_set_driver_private(dip, (caddr_t)qsp);

	if (qecinit(dip))
		goto bad;

	/*
	 * Add interrupt to the system in case of QED. BigMAC registers
	 * its own interrupt. Register one interrupt per qec
	 * (not one interrupt per channel).
	 */
	if ((qgp->control & QECG_CONTROL_MODE) == QECG_CONTROL_MACE) {
		if (ddi_add_intr(dip, 0, &c, 0, qecintr, (caddr_t)qsp)) {
			qecerror(dip, "ddi_add_intr failed");
			goto bad;
		}
	qsp->qs_cookie = c;
	}

	ddi_report_dev(dip);
	return (DDI_SUCCESS);

bad:
	if (qsp)
		kmem_free((caddr_t)qsp, sizeof (*qsp));
	if (parp)
		kmem_free((caddr_t)parp, sizeof (*parp));
	if (funp)
		kmem_free((caddr_t)funp, sizeof (*funp));
	if (qgp)
		ddi_unmap_regs(dip, 0, (caddr_t *)&qgp, 0, 0);

	return (DDI_FAILURE);
}

/*
 * QEC global reset.
 */
static u_int
qecgreset(qgp)
struct qec_global *qgp;
{
	volatile	u_int	*controlp;
	register int	n;

	controlp = &(qgp->control);
	*controlp = QECG_CONTROL_RST;
	n = QECMAXRSTDELAY / QECWAITPERIOD;
	while (--n > 0) {
		if ((*controlp & QECG_CONTROL_RST) == 0)
			return (0);
		drv_usecwait(QECWAITPERIOD);
	}
	return (1);
}

static u_int
qecintr(qsp)
register	struct	qec_soft	*qsp;
{
	int	i;
	volatile	u_int	global;
	u_int	serviced = 0;

	global = qsp->qs_globregp->status;

	/*
	 * this flag is set in qeinit() - fix for the fusion race condition
	 * - so that we claim any interrupt that was pending while
	 * the chip was being initialized. - see bug 1204247.
	 */

	if (qsp->qe_intr_flag) {
		serviced = 1;
		qsp->qe_intr_flag = 0;
		return (serviced);
	}

	/*
	 * Return if no bits set in global status register.
	 */

	if (global == 0)
		return (0);

	/*
	 * Service interrupting events for each channel.
	 */
	for (i = 0; i < qsp->qs_nchan; i++)
		if (QECCHANBITS(global, i) != 0)
			(*qsp->qs_intr_func[i])(qsp->qs_intr_arg[i]);

	serviced = 1;
	return (serviced);


}

/*
 * Calculate the dvma burst by setting up a dvma temporarily.  Return
 * 0 as burstsize upon failure as it signifies no burst size.
 */
static int
qecburst(dev_info_t *dip)
{
	caddr_t	addr;
	u_int size;
	int burst;
	ddi_dma_handle_t handle;

	size = QECBUFSIZE;
	addr = kmem_alloc(size, KM_SLEEP);
	if (ddi_dma_addr_setup(dip, (struct as *)0, addr, size,
		DDI_DMA_RDWR, DDI_DMA_DONTWAIT, 0, &qec_dma_limits, &handle)) {
		qecerror(dip, "ddi_dma_addr_setup failed");
		return (0);
	}
	burst = ddi_dma_burstsizes(handle);
	(void) ddi_dma_free(handle);
	kmem_free(addr, size);
	if (burst & 0x40)
		burst = QECG_CONTROL_BURST64;
	else if (burst & 0x20)
		burst = QECG_CONTROL_BURST32;
	else
		burst = QECG_CONTROL_BURST16;
#ifdef	MPSAS
		burst = QECG_CONTROL_BURST32;
#endif	MPSAS

	return (burst);
}

/*VARARGS*/
static void
qecerror(dev_info_t *dip, char *fmt, ...)
{
	char		msg_buffer[255];
	va_list	ap;

	mutex_enter(&qeclock);

	va_start(ap, fmt);
	vsprintf(msg_buffer, fmt, ap);
	cmn_err(CE_CONT, "%s%d: %s\n", ddi_get_name(dip),
					ddi_get_instance(dip),
					msg_buffer);
	va_end(ap);

	mutex_exit(&qeclock);
}

static int
qecinit(dip)
dev_info_t	*dip;
{
	register struct qec_soft *qsp;
	volatile	struct qec_global *qgp;
	off_t memsize;
	int burst;
	auto struct regtype {
		u_int hiaddr, loaddr, size;
	} regval[2];
	int reglen;

	if ((qsp = (struct qec_soft *)ddi_get_driver_private(dip)) == NULL) {
		qecerror(dip, "Failed to get driver private data");
		return (1);
	}
	qgp = qsp->qs_globregp;

	reglen = 2 * sizeof (struct regtype);
	if (ddi_getlongprop_buf(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
		"reg", (caddr_t)&regval, &reglen) != DDI_PROP_SUCCESS) {
		qecerror(dip, "missing reg property!");
		return (DDI_FAILURE);
	}
#ifdef	MPSAS
	memsize = 128 * 1024;
#else	MPSAS
	memsize = regval[1].size;
#endif	MPSAS

	qsp->qs_memsize = (u_int) memsize;

	/*
	 * Initialize all the QEC global registers.
	 */
	burst = qecburst(dip);
	qgp->control = burst;
	qgp->packetsize = qecframesize;
	qgp->memsize = memsize / qsp->qs_nchan;
	qgp->rxsize = qgp->txsize = (memsize / qsp->qs_nchan) / 2;
	return (0);
}
