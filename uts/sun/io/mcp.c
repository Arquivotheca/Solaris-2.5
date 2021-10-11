/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#ident "@(#)mcp.c  1.23     95/08/02 SMI"

/*
 * Sun MCP (ALM-2) Nexus Driver
 *
 *	This module implements the nexus driver for the MCP (ALM-2). The
 *	nexus allows for a firm partition between the parallel ports and
 *	serial ports on board the MCP.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/signal.h>
#include <sys/kmem.h>
#include <sys/termios.h>
#include <sys/termio.h>
#include <sys/stropts.h>
#include <sys/stream.h>
#include <sys/tty.h>
#include <sys/ptyvar.h>
#include <sys/cred.h>
#include <sys/stat.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/file.h>
#include <sys/uio.h>
#include <sys/buf.h>
#include <sys/reboot.h>
#include <sys/mkdev.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/modctl.h>

#include <sys/strtty.h>
#include <sys/suntty.h>
#include <sys/ksynch.h>

#include <sys/mcpzsdev.h>
#include <sys/mcpio.h>
#include <sys/mcpreg.h>
#include <sys/mcpcom.h>
#include <sys/mcpvar.h>

#include <sys/consdev.h>
#include <sys/ser_async.h>
#include <sys/debug/debug.h>
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/promif.h>

/*
 * Define some local macros.
 */

#define	MCP_NAME	"mcp"
#define	MCP_ZSWR1_INIT \
	(ZSWR1_SIE | ZSWR1_REQ_ENABLE | ZSWR1_REQ_NOT_WAIT | \
	ZSWR1_REQ_IS_RX | ZSWR1_RIE_SPECIAL_ONLY)

#define	DMADONE(x)	((x)->d_chip->d_ioaddr->csr)

#ifdef DEBUG
#define	MCP_DEBUG 0
#endif /* !DEBUG */

#if MCP_DEBUG > 0
static int mcp_debug = MCP_DEBUG;
#define	DEBUGF(level, args) \
	if (mcp_debug >= (level)) cmn_err args;
#else
#define	DEBUGF(level, args)	/* Nothing */
#endif /* !MCP_DEBUG */

/*
 * Driver public variables.
 */
void *mcp_soft_state_p = NULL;

/*
 * Driver private variables.
 */
static int mcp_zs_usec_delay = 2;
static int mcp_default_dtrlow = 3;

/*
 * Local Function Prototypes
 */

static int mcp_identify(dev_info_t *);
static int mcp_probe(dev_info_t *);
static int mcp_attach(dev_info_t *, ddi_attach_cmd_t);
static int mcp_detach(dev_info_t *dip, ddi_detach_cmd_t);
static int mcp_getinfo(dev_info_t *, ddi_info_cmd_t, void *, void **);
static u_int mcp_intr(caddr_t);
static int mcp_bus_ctl(dev_info_t *, dev_info_t *, ddi_ctl_enum_t,
	void *, void *);

static void cio_attach(mcp_dev_t *);

static int mcp_dma_start(struct _dma_chan_ *, char *, short);
static int mcp_dma_halt(struct _dma_chan_ *);
static struct _dma_chan_ *mcp_dma_getchan(caddr_t, int, int, int);
static unsigned short mcp_dma_getwc(struct _dma_chan_ *);
static void mcp_zs_program(struct zs_prog *zspp);

static int mcp_dma_attach(mcp_state_t *);
static void mcp_dma_init(struct _dma_chip_ *, struct _dma_device_ *, int);
static int mcp_dma_wait(struct _dma_chan_ *);
static int mcp_dma_done(struct _dma_chan_ *);

/*
 * MCP driver bus_ops and dev_ops
 */

static struct bus_ops mcp_bus_ops = {
	BUSO_REV,
	nullbusmap,		/* bus_map */
	0,			/* bus_get_intrspec */
	0,			/* bus_add_intrspec */
	0,			/* bus_remove_intrspec */
	0,			/* bus_map_fault */
	ddi_no_dma_map,
	ddi_no_dma_allochdl,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	mcp_bus_ctl,		/* bus_ctl */
	ddi_bus_prop_op,	/* bus_prop_op */
};

static struct dev_ops mcp_dev_ops = {
	DEVO_REV,		/* devo_rev */
	0,			/* refcnt */
	mcp_getinfo,		/* get_dev_info */
	mcp_identify,		/* identify */
	mcp_probe,		/* probe */
	mcp_attach,		/* attach */
	mcp_detach,		/* detach */
	nodev,			/* reset */
	(struct cb_ops *)0,	/* Driver Ops */
	&mcp_bus_ops		/* Bus Operations */
};

/*
 * Module linkage information for the kernel
 */
extern struct mod_ops mod_driverops;

static struct modldrv modldrv = {
	&mod_driverops,			/* Type of Module = Driver */
	"MCP/ALM-2 Nexus Driver v1.23",	/* Driver Identifier string. */
	&mcp_dev_ops,			/* Driver Ops. */
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modldrv, NULL
};

/*
 * Module Initialization functions.
 */

int
_init(void)
{
	int	stat;

	/* Allocate soft state */
	stat = ddi_soft_state_init(&mcp_soft_state_p, sizeof (mcp_state_t),
		N_MCP);
	if (stat != DDI_SUCCESS)
		return (stat);

	stat = mod_install(&modlinkage);
	if (stat != DDI_SUCCESS)
		ddi_soft_state_fini(&mcp_soft_state_p);

	return (stat);
}

int
_info(struct modinfo *infop)
{
	return (mod_info(&modlinkage, infop));
}

int
_fini()
{
	int stat = 0;

	if ((stat = mod_remove(&modlinkage)) != DDI_SUCCESS)
		return (stat);

	ddi_soft_state_fini(&mcp_soft_state_p);
	return (stat);
}

/*
 * Sun ALM-2 (MCP) Driver Code
 */

/*
 * static int
 * mcp_identify() - this driver entry point idenifies the device for
 *	the Kernel.  If the name of the dip node matches the driver
 *	name the identify function returns successfully.
 *
 *	Returns:	DDI_SUCCESS, if the name matches that of the driver.
 *			DDI_FAILURE, if name does not match.
 */

static int
mcp_identify(dev_info_t *dip)
{
	char *name = ddi_get_name(dip);

	DEBUGF(1, (CE_CONT, "mcp_identify: name = %s\n", name));

	if (strcmp(name, MCP_NAME) == 0)
		return (DDI_SUCCESS);
	else
		return (DDI_FAILURE);
}

/*
 * static int
 * mcp_probe() -  Probe for the existence of the device.
 *
 *	Returns:
 *		DDI_PROBE_SUCCESS, if mcp board found.
 *		DDI_FAILURE, if unable to probe the hardware.
 */

static int
mcp_probe(dev_info_t *dip)
{
	mcp_dev_t	*devp;
	int		error;
	int		board;

	/* Get the soft CAR property. */
	board = ddi_getprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS, "board", 0);

	/* Map the device now.  */
	if (ddi_map_regs(dip, 0, (caddr_t *)&devp,
			(off_t)0, sizeof (struct mcp_device)) != DDI_SUCCESS) {
		return (DDI_FAILURE);
		/* NOTREACHED */
	}

	/* Make sure the device is installed. */
	error = ddi_peeks(dip, (short *)&devp->reset_bid, (short *)0);
	if (error != DDI_SUCCESS) {
		DEBUGF(1, (CE_CONT, "mcp_probe%d: probe failed\n", board));
		ddi_unmap_regs(dip, 0, (caddr_t *)&devp, 0, 0);
		return (error);
		/* NOTREACHED */
	}

	/* Reset the board with this poke. */
	error = ddi_pokes(dip, (short *)&devp->reset_bid, 0);
	if (error != DDI_SUCCESS) {
		DEBUGF(1, (CE_CONT, "mcp_probe%d: probe failed\n", board));
		ddi_unmap_regs(dip, 0, (caddr_t *)&devp, 0, 0);
		return (error);
		/* NOTREACHED */
	}

	ddi_unmap_regs(dip, 0, (caddr_t *)&devp, 0, 0);
	return (DDI_PROBE_SUCCESS);
}


/*
 * static int
 * mcp_attach() - this driver entry point attaches the driver to the
 *	kernel.  It allocates the kernel resources necessary to
 *	maintain state about the mcp.
 *
 *	Returns:	DDI_SUCCESS, if able to attach.
 *			DDI_FAILURE, if unable to attach.
 */

static int
mcp_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	int		error, instance;
	int		vector;
	mcp_state_t	*softp;
	mcp_dev_t	*devp;
	char		name[20];

	DEBUGF(1, (CE_CONT, "mcp_attach: cmd, dip = 0x%x, 0x%x\n", cmd, dip));

	if (cmd != DDI_ATTACH) {
		cmn_err(CE_CONT, "mcp_attach: cmd != DDI_ATTACH\n");
		return (DDI_FAILURE);
		/* NOTREACHED */
	}

	instance = ddi_get_instance(dip);

	/* Allocate soft state associated with this instance. */
	if (ddi_soft_state_zalloc(mcp_soft_state_p, instance) != DDI_SUCCESS) {
		cmn_err(CE_CONT, "mcp_attach: Unable to alloc state\n");
		return (DDI_FAILURE);
		/* NOTREACHED */
	}

	softp = ddi_get_soft_state(mcp_soft_state_p, instance);
	softp->dip = dip;
	ddi_set_driver_private(dip, (caddr_t)softp);

	/* Get the soft CAR property. */
	softp->mcpsoftCAR = ddi_getprop(DDI_DEV_T_ANY, dip,
		DDI_PROP_DONTPASS, "flags", 0x1ffff);

	/* Map the device now.  */
	if (ddi_map_regs(dip, 0, (caddr_t *)&softp->devp,
			(off_t)0, sizeof (struct mcp_device)) != DDI_SUCCESS) {
		ddi_soft_state_free(mcp_soft_state_p, instance);
		cmn_err(CE_CONT, "mcp_attach: Unable to map registers\n");
		return (DDI_FAILURE);
		/* NOTREACHED */
	}

	/* Get pointer to device registers. */
	devp = softp->devp;

	/* Reset the board. */
	devp->reset_bid = 0xffff;

	/*
	 * Now allocate rest of kernel resources and initialize the rest
	 * of board.
	 */

	/* Install interrupts.  */
	error = ddi_add_intr(dip, (u_int)0, &softp->iblk_cookie,
		&softp->idev_cookie, mcp_intr, (caddr_t)dip);
	if (error != DDI_SUCCESS) {
		(void) mcp_detach(dip, DDI_DETACH);
		cmn_err(CE_CONT, "mcp_attach: unable to install intr\n");
		return (error);
		/* NOTREACHED */
	}

	sprintf(name, "dma_chip_mtx%d", instance);
	mutex_init(SOFT_DMA_MTX(softp), name, MUTEX_DRIVER,
		(void *)softp->iblk_cookie);

	softp->iface.priv = (caddr_t)softp;
	softp->iface.pdip = dip;
	softp->iface.dma_getchan = mcp_dma_getchan;
	softp->iface.dma_getwc = mcp_dma_getwc;
	softp->iface.dma_start = mcp_dma_start;
	softp->iface.dma_halt = mcp_dma_halt;
	softp->iface.zs_program = mcp_zs_program;

	/* Initialize the interrupt vecotr register. */
	devp->ivec = softp->idev_cookie.idev_vector;

	/* Initialize the CIO chip */
	cio_attach(devp);

	/* Initialize the DMA chips. */
	mcp_dma_attach(softp);

	while (devp->fifo[0] != FIFO_EMPTY)
		;
	CIO_WRITE(&devp->cio, CIO_PA_CSR, CIO_CLRIP);

	/* Enable the master interrupt for the MCP */
	devp->cio.portc_data = (devp->cio.portc_data & 0xf) | MCP_IE;

	ddi_report_dev(dip);
	return (DDI_SUCCESS);
}

/*
 * static int
 * mcp_detach() - Deallocate kernel resources associate with this instance
 *	of the driver.
 *
 *	Returns:	DDI_SUCCESS, if able to detach.
 *			DDI_FAILURE, if unable to detach.
 */

static int
mcp_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	int		i, instance;
	mcpcom_t	*zs;
	mcpaline_t	*za;
	mcp_state_t 	*softp;
	char		name[20];

	DEBUGF(1, (CE_CONT, "mcp_detach: cmd = 0x%x\n", cmd));
	DEBUGF(1, (CE_CONT, "mcp_detach: instance = %d\n",
		instance = ddi_get_instance(dip)));

	if (cmd != DDI_DETACH)
		return (DDI_FAILURE);

	instance = ddi_get_instance(dip);
	softp = ddi_get_soft_state(mcp_soft_state_p, instance);

	mutex_destroy(SOFT_DMA_MTX(softp));

	ddi_remove_intr(dip, 0, softp->iblk_cookie);
	ddi_unmap_regs(dip, 0, (caddr_t *)&softp->devp, 0, 0);
	ddi_soft_state_free(mcp_soft_state_p, instance);

	return (DDI_SUCCESS);
}

/*
 * static int
 * mcp_getinfo() - this routine translates the dip info dev_t and
 *	vice versa.
 *
 *	Returns:	DDI_SUCCESS, if successful.
 *			DDI_FAILURE, if unsuccessful.
 */

static int
mcp_getinfo(dev_info_t *dip, ddi_info_cmd_t cmd, void *arg, void **result)
{
	int 		error = DDI_SUCCESS;
	int 		instance;
	mcp_state_t	*softp;

	DEBUGF(1, (CE_CONT, "mcp_getinfo: cmd, arg = 0x%x, 0x%x\n", cmd, arg));

	switch (cmd) {
	case DDI_INFO_DEVT2DEVINFO:
		instance = MCP_INSTANCE((dev_t)arg);
		softp = ddi_get_soft_state(mcp_soft_state_p, instance);
		if (!softp) {
			*result = NULL;
		}

		*result = softp->dip;
		break;

	case DDI_INFO_DEVT2INSTANCE:
		instance = MCP_INSTANCE((dev_t)arg);
		*result = (void *)instance;
		break;

	default:
		DEBUGF(1, (CE_CONT, "mcp_getinfo: FAILED\n"));
		error = DDI_FAILURE;
	}

	DEBUGF(1, (CE_CONT, "mcp_getinfo: exiting ...\n"));
	return (error);
}

#define	MCP_INTR_MSG \
	"mcp_intr%d: Unexpected intr: (data = 0x%x, dvector = 0x%x)"

/*
 * static int
 * mcp_intr() - Process a interrupt from the MCP card.
 *
 *	Returns:	DDI_SUCCESS, if able to process interrupt.
 *			DDI_FAILURE, if unable to process intr.
 */

static u_int
mcp_intr(caddr_t arg)
{
	dev_info_t	*dip = (dev_info_t *)arg;
	mcp_state_t	*softp;

	int			bd;
	mcp_dev_t		*devp;
	struct _ciochip_	*cio;
	u_short			*fifo;

	int 		i;
	u_int		ln;
	u_char		dvector;
	u_char		status;
	u_char		mask;
	u_short		data;
	mcpcom_t	*zs;

	if (!dip)
		return (DDI_INTR_UNCLAIMED);


	softp = (mcp_state_t *)ddi_get_driver_private(dip);
	bd = ddi_get_instance(dip);
	devp = softp->devp;
	cio = &devp->cio;
	fifo = &(devp->fifo[0]);

	DEBUGF(1, (CE_CONT, "mcp_intr: dip = 0x%x, bd = 0x%x\n", dip, bd));

	do {
		/*
		 * If the vector corresponds to an action that requires
		 * us to empty the fifo first, and there is data in the
		 * fifo, drain the data up to the next layer.
		 */

		dvector = devp->devvector.ctr;
		DEBUGF(1, (CE_CONT, "mcp_intr: dvector = 0x%x\n", dvector));

		if ((dvector == MCP_NOINTR) || (dvector == CIO_PAD4_FIFO_E) ||
				((dvector & 0x87) == SCC0_XSINT) ||
				((dvector & 0x87) == SCC0_SRINT) ||
				(dvector == CIO_PAD5_FIFO_HF)) {

			while ((data = fifo[0]) != FIFO_EMPTY) {
				ln = (u_int)(data >> 8);
				DEBUGF(1, (CE_CONT,
					"mcp_intr: data = 0x%x\n", data));
				if (ln > 15) {
					cmn_err(CE_CONT, MCP_INTR_MSG,
						bd, data, dvector);
					continue;
				}

				zs = &softp->mcpcom[ln];
				if (zs->mcp_ops) {
					MCPOP_RXCHAR(zs, data & (zs->zs_mask));
				}
			}
		}

		/*
		 * Process the interrupt vector.
		 */
		ln = (dvector >> 3) & 0xf;

		switch (dvector) {
		case CIO_PBD0_TXEND:
		case CIO_PBD1_TXEND:
		case CIO_PBD2_TXEND:
		case CIO_PBD3_TXEND:
			zs = &softp->mcpcom[CIO_MCPBASE(dvector)];

			/*
			 * Reset * clear the source of the intr.
			 */
			mutex_enter(&zs->zs_excl);
			mutex_enter(&zs->zs_excl_hi);

			if (!(zs->zs_flags & MCP_PORT_ATTACHED)) {
				mutex_exit(&zs->zs_excl_hi);
				mutex_exit(&zs->zs_excl);
				break;
			}

			CIO_DMARESET(cio, (int)dvector);
			CIO_WRITE(cio, CIO_PB_CSR, CIO_CLRIP);
			status = DMADONE(zs->mcp_txdma);
			mutex_exit(&zs->zs_excl_hi);
			mutex_exit(&zs->zs_excl);

			for (i = 0; i < 4; zs++, i++) {
				if (zs->zs_flags & MCP_WAIT_DMA) {
					struct _dma_chan_ *dcp = zs->mcp_txdma;

					mask = 1 << dcp->d_chan;
					if (status & mask) {
						mutex_enter(&zs->zs_excl);
						mutex_enter(&zs->zs_excl_hi);

						dcp->d_chip->d_mask &= ~mask;
						zs->zs_flags &= ~MCP_WAIT_DMA;

						mutex_exit(&zs->zs_excl_hi);
						mutex_exit(&zs->zs_excl);

						if (zs->mcp_ops)
							MCPOP_TXEND(zs);
					}

				}
			}
			break;

		case SCC0_XSINT:
		case SCC1_XSINT:
		case SCC2_XSINT:
		case SCC3_XSINT:
		case SCC4_XSINT:
		case SCC5_XSINT:
		case SCC6_XSINT:
		case SCC7_XSINT:
		case SCC8_XSINT:
		case SCC9_XSINT:
		case SCC10_XSINT:
		case SCC11_XSINT:
		case SCC12_XSINT:
		case SCC13_XSINT:
		case SCC14_XSINT:
		case SCC15_XSINT:
			zs = &softp->mcpcom[ln];
			if (zs->mcp_ops)
				MCPOP_XSINT(zs);

			mutex_enter(&zs->zs_excl);
			mutex_enter(&zs->zs_excl_hi);

			if (zs->zs_flags & MCP_PORT_ATTACHED)
				MCP_SCC_WRITE0(ZSWR0_CLR_INTR);

			mutex_exit(&zs->zs_excl_hi);
			mutex_exit(&zs->zs_excl);
			break;

		case SCC0_SRINT:
		case SCC1_SRINT:
		case SCC2_SRINT:
		case SCC3_SRINT:
		case SCC4_SRINT:
		case SCC5_SRINT:
		case SCC6_SRINT:
		case SCC7_SRINT:
		case SCC8_SRINT:
		case SCC9_SRINT:
		case SCC10_SRINT:
		case SCC11_SRINT:
		case SCC12_SRINT:
		case SCC13_SRINT:
		case SCC14_SRINT:
		case SCC15_SRINT:
			zs = &softp->mcpcom[ln];
			if (zs->mcp_ops)
				MCPOP_SRINT(zs);

			mutex_enter(&zs->zs_excl);
			mutex_enter(&zs->zs_excl_hi);

			if (zs->zs_flags & MCP_PORT_ATTACHED)
				MCP_SCC_WRITE0(ZSWR0_CLR_INTR);

			mutex_exit(&zs->zs_excl_hi);
			mutex_exit(&zs->zs_excl);
			break;

		case SCC0_TXINT:
		case SCC1_TXINT:
		case SCC2_TXINT:
		case SCC3_TXINT:
		case SCC4_TXINT:
		case SCC5_TXINT:
		case SCC6_TXINT:
		case SCC7_TXINT:
		case SCC8_TXINT:
		case SCC9_TXINT:
		case SCC10_TXINT:
		case SCC11_TXINT:
		case SCC12_TXINT:
		case SCC13_TXINT:
		case SCC14_TXINT:
		case SCC15_TXINT:
			zs = &softp->mcpcom[ln];
			if (zs->mcp_ops)
				MCPOP_TXINT(zs);

			mutex_enter(&zs->zs_excl);
			mutex_enter(&zs->zs_excl_hi);

			if (zs->zs_flags & MCP_PORT_ATTACHED)
				MCP_SCC_WRITE0(ZSWR0_CLR_INTR);

			mutex_exit(&zs->zs_excl_hi);
			mutex_exit(&zs->zs_excl);
			break;

		case CIO_PBD4_RXEND:
			zs = &softp->mcpcom[0];

			/*
			 * Reset & clear the source of interrupt
			 */
			CIO_DMARESET(cio, (int)dvector);
			CIO_WRITE(cio, CIO_PB_CSR, CIO_CLRIP);
			status = DMADONE(zs->mcp_rxdma);

			for (i = 0; i < 4; zs++, i++) {
				struct _dma_chan_ *dcp = zs->mcp_rxdma;

				mutex_enter(&zs->zs_excl);
				mutex_enter(&zs->zs_excl_hi);

				mask = 1 << dcp->d_chan;
				if (status & mask) {
					dcp->d_chip->d_mask &= ~mask;
					mutex_exit(&zs->zs_excl_hi);
					mutex_exit(&zs->zs_excl);
					if (zs->mcp_ops)
						MCPOP_RXEND(zs);
				} else {
					mutex_exit(&zs->zs_excl_hi);
					mutex_exit(&zs->zs_excl);
				}
			}
			break;

		case SCC0_RXINT:
		case SCC1_RXINT:
		case SCC2_RXINT:
		case SCC3_RXINT:
		case SCC4_RXINT:
		case SCC5_RXINT:
		case SCC6_RXINT:
		case SCC7_RXINT:
		case SCC8_RXINT:
		case SCC9_RXINT:
		case SCC10_RXINT:
		case SCC11_RXINT:
		case SCC12_RXINT:
		case SCC13_RXINT:
		case SCC14_RXINT:
		case SCC15_RXINT:
			zs = &softp->mcpcom[ln];
			if (zs->mcp_ops)
				MCPOP_RXINT(zs);

			mutex_enter(&zs->zs_excl);
			mutex_enter(&zs->zs_excl_hi);

			if (zs->zs_flags & MCP_PORT_ATTACHED)
				MCP_SCC_WRITE0(ZSWR0_CLR_INTR);

			mutex_exit(&zs->zs_excl_hi);
			mutex_exit(&zs->zs_excl);
			break;

		case CIO_PBD5_PPTX:
			zs = &softp->mcpcom[N_MCP_ZSDEVS];

			/*
			 * Reset & clear the source of interrupt
			 */
			cio->portb_data = ~0x20;
			CIO_WRITE(cio, CIO_PB_CSR, CIO_CLRIP);
			status = DMADONE(zs->mcp_txdma);

			if (zs->zs_flags & MCP_WAIT_DMA) {
				mask = 1 << zs->mcp_txdma->d_chan;

				mutex_enter(&zs->zs_excl);
				mutex_enter(&zs->zs_excl_hi);

				if (status & mask) {
					zs->mcp_txdma->d_chip->d_mask &= ~mask;
					zs->zs_flags &= ~MCP_WAIT_DMA;
					mutex_exit(&zs->zs_excl_hi);
					mutex_exit(&zs->zs_excl);
					if (zs->mcp_ops)
						MCPOP_TXEND(zs);
				} else {
					mutex_exit(&zs->zs_excl_hi);
					mutex_exit(&zs->zs_excl);
				}
			}
			break;

		case CIO_PAD6_FIFO_F:
			while (fifo[0] != FIFO_EMPTY)
				;
			CIO_WRITE(cio, CIO_PA_CSR, CIO_CLRIP);
			break;

		case CIO_PAD0_DSRDM:
		case CIO_PAD1_DSRDM:
		case CIO_PAD2_DSRDM:
		case CIO_PAD3_DSRDM:

		case CIO_PAD5_FIFO_HF:
		case CIO_PAD4_FIFO_E:
			CIO_WRITE(cio, CIO_PA_CSR, CIO_CLRIP);
			break;

		case CIO_PBD6_PE:
			zs = &softp->mcpcom[N_MCP_ZSDEVS];

			/* Reset & clear the source of interrupt */
			cio->portb_data = ~MCPRPE;
			CIO_WRITE(cio, CIO_PB_CSR, CIO_CLRIP);
			if (zs->mcp_ops)
				MCPOP_PE(zs);
			break;

		case CIO_PBD7_SLCT:
			zs = &softp->mcpcom[N_MCP_ZSDEVS];

			/* Reset & clear the source of interrupt */
			cio->portb_data = ~MCPRSLCT;
			CIO_WRITE(cio, CIO_PB_CSR, CIO_CLRIP);
			if (zs->mcp_ops)
				MCPOP_SLCT(zs);
			break;
		}
	} while (dvector != MCP_NOINTR);

	return (DDI_INTR_CLAIMED);
}

/*
 * static int
 * mcp_bus_ctl() - this processes the requeust on the behalf of the
 *	children of mcp.
 *
 *	Returns:
 *		DDI_SUCCESS, when operation is successful.
 *		DDI_FAILURE, when operation failed.
 */

static int
mcp_bus_ctl(dev_info_t *dip, dev_info_t *rdip, ddi_ctl_enum_t ctlop,
	void *arg, void *result)
{
	switch (ctlop) {
	case DDI_CTLOPS_REPORTDEV:
		cmn_err(CE_CONT, "?%s%d at %s%d\n",
			ddi_get_name(rdip), ddi_get_instance(rdip),
			ddi_get_name(dip), ddi_get_instance(dip));
		break;

	case DDI_CTLOPS_INITCHILD: {
		dev_info_t	*me = dip;
		dev_info_t	*adip = (dev_info_t *)arg;
		char		name[MAXNAMELEN];
		int 		stat, port;
		int		length = sizeof (int);
		mcp_state_t	*softp;
		mcp_iface_t	*iface;

		stat = ddi_prop_op(DDI_DEV_T_NONE, adip,
			PROP_LEN_AND_VAL_BUF,
			DDI_PROP_DONTPASS | DDI_PROP_CANSLEEP,
			"port", (caddr_t)&port, &length);
		if (stat != DDI_SUCCESS) {
			cmn_err(CE_CONT, "mcp%d: prop op on child failed\n",
				ddi_get_instance(me));
			return (DDI_NOT_WELL_FORMED);
		}

		sprintf(name, "%d", port);
		ddi_set_name_addr(adip, name);

		softp = (mcp_state_t *)ddi_get_driver_private(me);
		iface = &softp->iface;

		ddi_set_driver_private(adip, (caddr_t)iface);
	}
	break;

	case DDI_CTLOPS_UNINITCHILD:
		ddi_set_driver_private((dev_info_t *)arg, NULL);
		ddi_set_name_addr((dev_info_t *)arg, NULL);
		break;

	default:
		cmn_err(CE_CONT, "%s%d: invalid op (%d) from %s%d\n",
			ddi_get_name(dip), ddi_get_instance(dip), ctlop,
			ddi_get_name(rdip), ddi_get_instance(rdip));
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}


/*
 * CIO rountines.
 */

/*
 * static void
 * cio_attach() - Initialize the Counter/Timer to chip to a known state.
 *
 *	Returns:	NONE
 */

static void
cio_attach(mcp_dev_t *devp)
{
	u_char			select;
	struct _ciochip_	*ciop = &devp->cio;

	/*
	 * Write a single zero to cio to clear and reset
	 */

	ciop->cntrl = 0;

	/*
	 * Initialize CIO port C,  While here, detect RS232/449
	 * interface
	 */

	CIO_WRITE(ciop, CIO_MICR, MASTER_INT_ENABLE);
	CIO_WRITE(ciop, CIO_PC_DPPR, MCPRVMEINT | MCPRDIAG);
	CIO_WRITE(ciop, CIO_PC_DDR, PORT0_RS232_SEL | PORT1_RS232_SEL);
	CIO_WRITE(ciop, CIO_PC_SIOCR, ONES_CATCHER);

	if (((select = ciop->portc_data) & PORT0_RS232_SEL) == 0) {
		DEBUGF(1, (CE_CONT,
			"_cio_attach: Port 0 supports RS449 interface\n"));
		devp->devctl[0].ctr |= EN_RS449_TX;
	}

	if (((select = ciop->portc_data) & PORT1_RS232_SEL) == 0) {
		DEBUGF(1, (CE_CONT,
			"_cio_attach: Port 1 supports RS449 interface\n"));
		devp->devctl[1].ctr |= EN_RS449_TX;
	}

	ciop->portc_data = (select & 0xf) & ~MCPRDIAG;	/* Ensure diag off */

	/*
	 * Initialize port A of cio chip
	 */

	CIO_WRITE(ciop, CIO_PA_MODE, CIO_BIT_PORT_MODE);
	CIO_WRITE(ciop, CIO_PA_DDR, CIO_ALL_INPUT);
	CIO_WRITE(ciop, CIO_PA_DPPR, FIFO_NON_INVERT);
	CIO_WRITE(ciop, CIO_PA_PP, FIFO_NOT_ONE);
	CIO_WRITE(ciop, CIO_PA_PM, FIFO_EMPTY_INTR_ONLY);
	CIO_WRITE(ciop, CIO_PA_CSR, PORT_INT_ENABLE);

	/*
	 * Initialize port B of cio chip
	 */

	CIO_WRITE(ciop, CIO_PB_MODE, CIO_BIT_PORT_MODE);
	CIO_WRITE(ciop, CIO_PB_DDR, CIO_ALL_INPUT);
	CIO_WRITE(ciop, CIO_PB_SIOCR, EOP_ONE);
	CIO_WRITE(ciop, CIO_PB_DPPR, EOP_INVERT | MCPRSLCT | MCPRPE);
	CIO_WRITE(ciop, CIO_PB_PP, EOP_ONE);
	CIO_WRITE(ciop, CIO_PB_PM, EOP_ONE);
	CIO_WRITE(ciop, CIO_PB_CSR, PORT_INT_ENABLE);
	CIO_WRITE(ciop, CIO_MCCR, MASTER_ENABLE);

	/*
	 * set vector for CIO
	 * vector bits assignment:
	 *  bit 0 -- select CIO or SCC   (1 == CIO)
	 *  bit 4 -- select port A or B  (0 == port A)
	 */

	CIO_WRITE(ciop, CIO_PA_IVR, 0x01);
	CIO_WRITE(ciop, CIO_PB_IVR, 0x11);
}

static void
mcp_zs_program(struct zs_prog *zspp)
{
	mcpcom_t *zs = (mcpcom_t *)zspp->zs;
	u_char c;
	u_char wr10 = 0, wr14 = 0;
	int loops;

	ASSERT(mutex_owned(&zs->zs_excl));
	ASSERT(mutex_owned(&zs->zs_excl_hi));

	/*
	 * There are some special cases to account for before reprogramming.
	 * We might be transmitting, so delay 100000 usec (worst case at 110
	 * baud) for this to finish, then disable the receiver until later,
	 * reset the External Status Change latches and the error bits, and
	 * drain the receive FIFO.
	 *
	 * XXX: Doing any kind of reset (WR9) here causes trouble!
	 */
	loops = 1000;
	do {
		MCP_SCC_READ(1, c);
		if (c & ZSRR1_ALL_SENT)
			break;
		DELAY(100);
	} while (--loops > 0);

	if (zspp->flags & ZSP_SYNC) {
		DEBUGF(1, (CE_CONT, "mcp_zs_program: zspp flags = 0x%x\n",
			zspp->flags));
		MCP_SCC_WRITE(7, SDLCFLAG);
		wr10 = ZSWR10_PRESET_ONES;
		if (zspp->flags & ZSP_NRZI)
			wr10 |= ZSWR10_NRZI;
		MCP_SCC_WRITE(10, wr10);
	} else {
		DEBUGF(1, (CE_CONT, "mcp_zs_program: zspp flags = 0x%x\n",
			zspp->flags));
		MCP_SCC_WRITE(3, 0);
		MCP_SCC_WRITE0(ZSWR0_RESET_TXINT);
		MCP_SCC_WRITE0(ZSWR0_RESET_STATUS);
		MCP_SCC_WRITE0(ZSWR0_RESET_ERRORS);

		c = MCP_SCC_READDATA();	/* Empty the FIFO */
		MCP_ZSDELAY();
		c = MCP_SCC_READDATA();
		MCP_ZSDELAY();
		c = MCP_SCC_READDATA();
		MCP_ZSDELAY();
	}

	/*
	 * Programming the SCC is done in three phases.
	 * Phase one sets operating modes:
	 */

	MCP_SCC_WRITE(4, zspp->wr4);
	MCP_SCC_WRITE(11, zspp->wr11);
	MCP_SCC_WRITE(12, zspp->wr12);
	MCP_SCC_WRITE(13, zspp->wr13);
	if (zspp->flags & ZSP_PLL) {
		MCP_SCC_WRITE(14, ZSWR14_DPLL_SRC_BAUD);
		MCP_SCC_WRITE(14, ZSWR14_DPLL_NRZI);
	} else
		MCP_SCC_WRITE(14, ZSWR14_DPLL_DISABLE);

	/*
	 * Phase two enables special hardware functions:
	 */
	wr14 = ZSWR14_BAUD_FROM_PCLK | ZSWR14_BAUD_ENA | ZSWR14_DTR_IS_REQUEST;
	if (zspp->flags & ZSP_LOOP)
		wr14 |= ZSWR14_LOCAL_LOOPBACK;
	if (zspp->flags & ZSP_ECHO)
		wr14 |= ZSWR14_AUTO_ECHO;

	MCP_SCC_WRITE(14, wr14);
	MCP_SCC_WRITE(3, zspp->wr3);
	MCP_SCC_WRITE(5, zspp->wr5);

	MCP_SCC_WRITE0(ZSWR0_RESET_TXCRC);


	/*
	 * Phase three enables interrupt sources:
	 */
	MCP_SCC_WRITE(15, zspp->wr15);
	MCP_SCC_WRITE0(ZSWR0_RESET_TXINT);
	MCP_SCC_WRITE0(ZSWR0_RESET_STATUS);
	MCP_SCC_WRITE0(ZSWR0_RESET_ERRORS);

	if (zspp->flags & ZSP_PARITY_SPECIAL) {
		MCP_SCC_WRITE(1, MCP_ZSWR1_INIT | ZSWR1_PARITY_SPECIAL);
	} else {
		MCP_SCC_WRITE(1, MCP_ZSWR1_INIT);
	}
}

/*
 * MCP DMA routines.
 */

static int
mcp_dma_attach(mcp_state_t *softp)
{
	u_char		*base = (u_char *)softp->devp;
	mcp_dev_t	*devp = softp->devp;
	int	i, j;

	if (!softp)
		return (FALSE);

	for (i = 0; i < N_MCP_DMA; i++) {
		softp->dma_chips[i].d_priv = (caddr_t)softp;

		mcp_dma_init(&softp->dma_chips[i], &(devp->dmas[i]),
			(i == 4) ? RX_DIR : TX_DIR);

		softp->dma_chips[i].rbase = base;
	}

	return (TRUE);
}

static void
mcp_dma_init(struct _dma_chip_ *dc, struct _dma_device_ *chip, int flag)
{
	struct _dma_chan_ 	*dma;
	mcp_state_t		*softp;
	short 			i, dir;
	struct _addr_wc_ 	*port;

	softp = (mcp_state_t *)dc->d_priv;
	mutex_enter(SOFT_DMA_MTX(softp));

	dc->d_ioaddr = chip;
	dc->d_mask = 0;

	chip->reset = 1;
	chip->csr = DMA_CSR_DREQLOW;
	chip->clrff = 1;

	for (i = 0; i < N_ADDR_WC; i++) {
		dma = &dc->d_chans[i];
		dma->d_chip = dc;
		dma->d_chan = i;
		dma->d_myport = &chip->addr_wc[i];
		dma->d_dir = flag;	/* 1 - xmit, 0 - recv */

		if (dma->d_dir == TX_DIR)
			dir = DMA_MODE_READ | DMA_MODE_SINGLE;
		else
			dir = DMA_MODE_WRITE | DMA_MODE_SINGLE;

		chip->mode = i + dir;
		port = &chip->addr_wc[i];

		port->baddr = 0;	/* low byte */
		port->baddr = 0;	/* high byte */
		port->wc = 0xFF;	/* low byte */
		port->wc = 0xFF;	/* high byte */
	}

	mutex_exit(SOFT_DMA_MTX(softp));
}

static struct _dma_chan_ *
mcp_dma_getchan(caddr_t state, int port, int dir, int scc)
{
	mcp_state_t *softp = (mcp_state_t *)state;

	if (scc == PR_DMA) {
		if (dir == TX_DIR)
			return (&softp->dma_chips[5].d_chans[port]);

		return (NULL);
	}

	if (dir == TX_DIR)
		return (&softp->dma_chips[CHIP(port)].d_chans[CHAN(port)]);

	if ((dir == RX_DIR) && (port < 4))
		return (&softp->dma_chips[4].d_chans[port]);

	return (NULL);
}

static int
mcp_dma_start(struct _dma_chan_ *dcp, char *addr, short len)
{
	mcp_state_t		*softp;
	long			linaddr;
	struct _addr_wc_	*port;

	DEBUGF(1, (CE_CONT, "mcp_dma_start: starting to output %d chars\n",
		len));

	softp = (mcp_state_t *)dcp->d_chip->d_priv;
	mutex_enter(SOFT_DMA_MTX(softp));

	linaddr = (long)((long)addr - (long)dcp->d_chip->rbase);

	dcp->d_chip->d_ioaddr->mask = DMA_MASK_SET + dcp->d_chan;
	dcp->d_chip->d_ioaddr->clrff = 1;

	port = dcp->d_myport;

	port->baddr = (u_char)(((short)linaddr) & 0x00ff);
	port->baddr = (u_char)((((short)linaddr) >> 8) & 0x00ff);

	len--;
	port->wc = len & 0x00ff;
	port->wc = (len >> 8) & 0x00ff;

	dcp->d_chip->d_mask |= (1 << dcp->d_chan);
	dcp->d_chip->d_ioaddr->mask = DMA_MASK_CLEAR + dcp->d_chan;

	mutex_exit(SOFT_DMA_MTX(softp));
	return (len);
}

static unsigned short
mcp_dma_getwc(struct _dma_chan_ *dcp)
{
	mcp_state_t		*softp;
	struct _addr_wc_ 	*port;
	u_char 			lo, hi;

	softp = (mcp_state_t *)dcp->d_chip->d_priv;
	mutex_enter(SOFT_DMA_MTX(softp));

	port = dcp->d_myport;
	lo = port->wc;
	hi = port->wc;

	mutex_exit(SOFT_DMA_MTX(softp));
	return ((hi << 8) + lo + 1);
}

static int
mcp_dma_halt(struct _dma_chan_ *dcp)
{
	mcp_state_t		*softp;
	struct _addr_wc_	*port;
	short			len;

	softp = (mcp_state_t *)dcp->d_chip->d_priv;
	mutex_enter(SOFT_DMA_MTX(softp));

	dcp->d_chip->d_ioaddr->mask = DMA_MASK_SET + dcp->d_chan;
	port = dcp->d_myport;
	len = port->wc;
	len |= port->wc << 8;
	len += 1;

	if (len > 0)
		dcp->d_chip->d_mask &= ~(1 << dcp->d_chan);

	mutex_exit(SOFT_DMA_MTX(softp));
	return ((int)len);
}

static int
mcp_dma_wait(struct _dma_chan_ *dcp)
{
	mcp_state_t		*softp;
	short mask;

	softp = (mcp_state_t *)dcp->d_chip->d_priv;
	mutex_enter(SOFT_DMA_MTX(softp));

	mask = 1 << dcp->d_chan;
	while ((dcp->d_chip->d_ioaddr->csr & mask) == 0)
		;

	mutex_exit(SOFT_DMA_MTX(softp));
}

static int
mcp_dma_done(struct _dma_chan_ *dcp)
{
	mcp_state_t		*softp;
	u_char			status;
	struct _dma_chip_	*dc = dcp->d_chip;
	u_char			mask;

	softp = (mcp_state_t *)dcp->d_chip->d_priv;
	mutex_enter(SOFT_DMA_MTX(softp));

	mask = 1 << dcp->d_chan;
	status = dc->d_ioaddr->csr;

	mutex_exit(SOFT_DMA_MTX(softp));
	return ((int)status);
}
