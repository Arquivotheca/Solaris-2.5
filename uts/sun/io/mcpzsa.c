/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#ident "@(#)mcpzsa.c  1.36     95/08/02 SMI"

/*
 * Sun MCP (ALM-2) Serial Port Driver
 *
 *	Handles asynchronous protocols for the Z8530 SCC's.  Handles
 *	normal UNIX support for terminals and modems.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/signal.h>
#include <sys/kmem.h>
#include <sys/termios.h>
#include <sys/termio.h>
#include <sys/strsubr.h>
#include <sys/stropts.h>
#include <sys/stream.h>
#include <sys/tty.h>
#include <sys/ptyvar.h>
#include <sys/cred.h>
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
#include <sys/promif.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>

/*
 * Define some local marros.
 */

#ifdef DEBUG
#define	MCP_DEBUG 0
#endif

#ifdef DEBUG
static int mcpzsa_debug = MCP_DEBUG;
#define	DEBUGF(level, args) \
	if (mcpzsa_debug >= (level)) cmn_err args;
#else
#define	DEBUGF(level, args)	/* Nothing */
#endif /* !MCP_DEBUG */

#define	MCP_ZS_PL(x)	(ipltospl((x)->idev_cookie.idev_priority))

#define	NSPEED		16
#define	MCP_PCLK	(19660800/4)
#define	MZSPEED(n)	ZSTimeConst(MCP_PCLK, n)

/*
 * Define references to external data.
 */
extern kcondvar_t lbolt_cv;

/*
 * Define some local variables.
 */
typedef struct mcpzsa_softc {
	mcp_iface_t *iface;
	dev_info_t *dip;
} mcpzsa_soft_t;

static int mcp_zs_usec_delay = 2;
static int mcp_default_dtrlow = 3;

static int mcpzsaticks = 3;
static int mcpzsadtrlow = 3;
static int mcpzsasoftdtr = 0;
static int mcpb134_weird = 0;	/* if set, use old B134 behavior */

static unsigned short mcp_speeds[NSPEED] = {
	0, 		MZSPEED(50), 	MZSPEED(75), 	MZSPEED(110),
	MZSPEED(134),	MZSPEED(150),	MZSPEED(200),	MZSPEED(300),
	MZSPEED(600),	MZSPEED(1200),	MZSPEED(1800),	MZSPEED(2400),
	MZSPEED(4800),	MZSPEED(9600),	MZSPEED(19200),	MZSPEED(38400),
};

static void *mcpzsa_state_p;

/*
 * Local Function Prototypes.
 */
static int	mcpzsa_identify(dev_info_t *);
static int	mcpzsa_probe(dev_info_t *);
static int 	mcpzsa_attach(dev_info_t *, ddi_attach_cmd_t);
static int	mcpzsa_detach(dev_info_t *, ddi_detach_cmd_t);
static int 	mcpzsa_get_dev_info(dev_info_t *, ddi_info_cmd_t,
	void *, void **);

static int	mcpzsa_open(queue_t *, dev_t *, int, int, cred_t *);
static int	mcpzsa_close(queue_t *, int, cred_t *);
static int	mcpzsa_wput(queue_t *, mblk_t *);
static int	mcpzsa_ioctl(mcpaline_t *, queue_t *, mblk_t *);
static void	mcpzsa_reioctl(mcpaline_t *);
static int	dm_to_mcp(int);
static int	mcp_to_dm(int);
static int	mcpzsa_mctl(mcpaline_t *, int, int);
static void	mcpzsa_start(mcpaline_t *);
static void	mcpzsa_restart(mcpaline_t *);
static int 	mcpzsa_txint(mcpcom_t *);
static int 	mcpzsa_xsint(mcpcom_t *);
static int 	mcpzsa_rxint(mcpcom_t *);
static int	mcpzsa_srint(mcpcom_t *);
static int	mcpzsa_txend(mcpcom_t *);
static int	mcpzsa_rxend(mcpcom_t *);
static int	mcpzsa_rxchar(mcpcom_t *, unsigned char);
static void	mcpzsa_drain(mcpaline_t *);
static void	mcpzsa_drain_callout(mcpaline_t *);
static void	mcpzsa_ringov(mcpaline_t *);
static void	mcpzsa_zs_program(mcpaline_t *);
static void	mcpzsa_mblk_debug(mblk_t *);

/*
 * Define some protocol operations.
 */

/* Async Protocol Ops. */
struct mcpops mcpops_async = {
	mcpzsa_txint,	/* Xmit buffer empty. */
	mcpzsa_xsint,	/* External Status */
	mcpzsa_rxint,	/* Receive Char Avail */
	mcpzsa_srint,	/* Special Receive Cond. */
	mcpzsa_txend,	/* Transmit DMA Done. */
	mcpzsa_rxend,	/* Receive DMA done. */
	mcpzsa_rxchar,	/* Fifo recv char avail. */
	0,
	0,
};

/*
 * Declare some streams structures.
 */

static struct module_info mcpzsa_info = {
	0x4d43,		/* Module ID */
	"mcpzsa",		/* Module name */
	0,		/* Min packet size. */
	INFPSZ,		/* Max packet size. */
	2048,		/* Hi Water */
	128		/* Lo Water */
};

static struct qinit mcpzsa_rinit = {
	putq,		/* Put Proc. */
	NULL,		/* Service Proc. */
	mcpzsa_open,	/* Open */
	mcpzsa_close,	/* Close */
	NULL,		/* Admin */
	&mcpzsa_info,	/* Module Info Struct. */
	NULL		/* Module Stat Struct. */
};

static struct qinit mcpzsa_winit = {
	mcpzsa_wput,	/* Put Proc. */
	NULL,		/* Service Proc. */
	NULL,		/* Open */
	NULL,		/* Close */
	NULL,		/* Admin */
	&mcpzsa_info,	/* Module Info State. */
	NULL
};

/* MCP Stream tab for SCC's */
struct streamtab mcpzsa_tab = {
	&mcpzsa_rinit,
	&mcpzsa_winit,
	NULL,
	NULL,
};

/*
 * MCPZSA driver cb_ops and dev_ops
 */

static struct cb_ops mcpzsa_cb_ops = {
	nodev,		/* Open */
	nodev,		/* Close. */
	nodev,		/* Strategy */
	nodev,		/* print */
	nodev,		/* dump */
	nodev, 		/* read */
	nodev,		/* write */
	nodev,		/* ioctl */
	nodev,		/* devmap */
	nodev, 		/* mmap */
	nodev, 		/* segmap */
	nochpoll,	/* Poll. */
	ddi_prop_op,	/* Prop Op. */
	&mcpzsa_tab,	/* streamtab. */
	D_NEW | D_MP
};

static struct dev_ops mcpzsa_dev_ops = {
	DEVO_REV,		/* Devo_rev */
	0,			/* Refcnt */
	mcpzsa_get_dev_info,	/* get_dev_info */
	mcpzsa_identify,	/* identify */
	mcpzsa_probe,		/* probe. */
	mcpzsa_attach,		/* attach */
	mcpzsa_detach,		/* detach */
	nodev,			/* reset */
	&mcpzsa_cb_ops,		/* Driver ops */
	(struct bus_ops *)0,	/* Bus ops. */
};

/*
 * Module linkage information for the kernel
 */
extern struct mod_ops mod_driverops;

static struct modldrv modldrv = {
	&mod_driverops,		/* Type of Module = Driver */
	"MCP ZS driver v1.36",	/* Driver Identifier string. */
	&mcpzsa_dev_ops,	/* Driver Ops. */
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modldrv, NULL
};

/*
 * Module initialization
 */

int
_init(void)
{
	int	stat;

	stat = ddi_soft_state_init(&mcpzsa_state_p, sizeof (mcpzsa_soft_t),
		N_MCP_ZSDEVS * N_MCP);
	if (stat != DDI_SUCCESS)
		return (stat);

	stat = mod_install(&modlinkage);
	if (stat != 0) {
		ddi_soft_state_fini(&mcpzsa_state_p);
	}
	return (stat);
}

int
_info(struct modinfo *infop)
{
	return (mod_info(&modlinkage, infop));
}

int
_fini(void)
{
	int	stat;

	if ((stat = mod_remove(&modlinkage)) != 0)
		return (stat);

	ddi_soft_state_fini(&mcpzsa_state_p);
	return (0);
}


/*
 * Autoconfiguration Routines.
 */

static int
mcpzsa_identify(dev_info_t *dip)
{
	char *name = ddi_get_name(dip);

	if (strcmp(name, "mcpzsa") == 0)
		return (DDI_IDENTIFIED);
	else
		return (DDI_NOT_IDENTIFIED);
}

/*ARGSUSED*/
static int
mcpzsa_probe(dev_info_t *dip)
{
	return (DDI_PROBE_SUCCESS);
}

static int
mcpzsa_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	mcp_state_t	*softp;
	mcp_iface_t	*iface;
	mcpcom_t	*zs;
	mcpaline_t	*za;
	mcp_dev_t	*devp;
	mcpzsa_soft_t	*mcpzsap;
	caddr_t		bufp;
	int		instance, port, bd;
	char		name[80];

	if (cmd != DDI_ATTACH)
		return (DDI_FAILURE);

	/* Get the pointer to the interface from the parent. */
	if ((iface = (mcp_iface_t *)ddi_get_driver_private(dip)) == NULL) {
		cmn_err(CE_CONT, "mcpzsa%d: ddi_get_driver_private failed\n",
			ddi_get_instance(dip));
		return (DDI_FAILURE);
	}

	instance = ddi_get_instance(dip);
	port = instance % N_MCP_ZSDEVS;
	bd = instance / N_MCP_ZSDEVS;

	DEBUGF(1, (CE_CONT, "mcpzsa_attach: instance, port, bd = %d, %d, %d\n",
		instance, port, bd));

	if (ddi_soft_state_zalloc(mcpzsa_state_p, instance) != DDI_SUCCESS)
		return (DDI_FAILURE);

	mcpzsap = (mcpzsa_soft_t *)ddi_get_soft_state(mcpzsa_state_p, instance);
	mcpzsap->iface = iface;
	mcpzsap->dip = dip;

	softp = (mcp_state_t *)iface->priv;
	devp = softp->devp;
	bufp = (caddr_t)devp->ram.asyncbuf;

	zs = &softp->mcpcom[port];
	za = &softp->mcpaline[port];

	/*
	 * Initialize each serial port of the MCP
	 *
	 *	includes:
	 *
	 *		- disable xoff, clear one character buffer, setup
	 *		  links between data structures, and initialize
	 *		  dma channel and enable master interrupt and
	 *		  interrupt vector for SCC.
	 *
	 *	Vector bit assignments:
	 *		Bit 0:  Select CIO or SCC (0 == SCC)
	 *		Bits 4 - 7: Select SCC chip (0 to 8)
	 */
	sprintf(name, "%d", instance);
	if (ddi_create_minor_node(dip, name, S_IFCHR, instance,
			DDI_NT_SERIAL, NULL) != DDI_SUCCESS) {
		return (DDI_FAILURE);
		/* NOTREACHED */
	}

	sprintf(name, "%d,cu", instance);
	if (ddi_create_minor_node(dip, name, S_IFCHR, instance | MCP_OUTLINE,
			DDI_NT_SERIAL_DO, NULL) != DDI_SUCCESS) {
		return (DDI_FAILURE);
		/* NOTREACHED */
	}

	zs->zs_state = (caddr_t)softp;
	zs->zs_unit = port;
	zs->mc_unit = instance;

	/*
	 * Initialize locks.
	 */
	sprintf(name, "mcp.zs.lock%d", instance);
	mutex_init(&zs->zs_excl, name, MUTEX_DRIVER,
		(void *)softp->iblk_cookie);

	sprintf(name, "mcp.zs.lock.hi%d", instance);
	mutex_init(&zs->zs_excl_hi, name, MUTEX_DRIVER,
		(void *)softp->iblk_cookie);

	mutex_enter(&zs->zs_excl);
	mutex_enter(&zs->zs_excl_hi);

	zs->mcp_addr = softp->devp;
	zs->zs_addr = &zs->mcp_addr->sccs[port];
	zs->zs_flags = 0;
	zs->zs_rerror = 0;
	zs->mcp_addr->xbuf[port].xoff = DISABLE_XOFF;

	zs->mcp_txdma = MCP_DMA_GETCHAN(iface, (caddr_t)softp, port,
		TX_DIR, SCC_DMA);
	zs->mcp_rxdma = MCP_DMA_GETCHAN(iface, (caddr_t)softp, port,
		RX_DIR, SCC_DMA);
	zs->zs_priv = (caddr_t)za;

	za->za_common = (struct zscom *)zs;
	za->za_dmabuf = (u_char *)(bufp + (zs->zs_unit * ASYNC_BSZ));
	za->za_xoff = &(devp->xbuf[zs->zs_unit].xoff);
	za->za_devctl = &(devp->devctl[zs->zs_unit].ctr);

	/* XXX - disable input/output. */
	*za->za_devctl = 0;

	za->za_flags_cv = kmem_zalloc(sizeof (kcondvar_t), KM_SLEEP);

	RING_INIT(za);
	zs->zs_dtrlow = hrestime.tv_sec - mcp_default_dtrlow;

	if (softp->mcpsoftCAR & (1 << zs->zs_unit))
		za->za_ttycommon.t_flags |= TS_SOFTCAR;

	if (mcpzsasoftdtr && (za->za_ttycommon.t_flags & TS_SOFTCAR))
		(void) mcpzsa_mctl(za, ZS_ON, DMSET);
	else
		(void) mcpzsa_mctl(za, ZS_OFF, DMSET);

	if (port & 0x01) {
		MCP_SCC_WRITE(9, ZSWR9_MASTER_IE | ZSWR9_VECTOR_INCL_STAT);
		MCP_SCC_WRITE(2, 0x10 * (port / 2));
	}

	zs->zs_flags |= MCP_PORT_ATTACHED;

	mutex_exit(&zs->zs_excl_hi);
	mutex_exit(&zs->zs_excl);

	ddi_report_dev(dip);
	return (DDI_SUCCESS);
}

static int
mcpzsa_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	mcp_iface_t	*iface;
	mcp_state_t	*softp;
	int		port, instance;
	mcpcom_t	*zs;
	mcpaline_t	*za;
	char		name[80];

	if (cmd != DDI_DETACH)
		return (DDI_FAILURE);

	iface = (mcp_iface_t *)ddi_get_driver_private(dip);
	softp = (mcp_state_t *)iface->priv;
	instance = ddi_get_instance(dip);
	port = instance % (N_MCP_ZSDEVS);

	zs = &softp->mcpcom[port];

	mutex_enter(&zs->zs_excl);
	mutex_enter(&zs->zs_excl_hi);

	za = (mcpaline_t *)zs->zs_priv;
	if (za->za_flags & (ZAS_WOPEN | ZAS_ISOPEN)) {
		mutex_exit(&zs->zs_excl_hi);
		mutex_exit(&zs->zs_excl);
		return (DDI_FAILURE);
		/* NOTREACHED */
	}

	/*
	 * Clear interrupts for the SCC and reset the chip.
	 */
	if (port & 0x01) {
		MCP_SCC_WRITE(9, 0xC0);
	} else {
		MCP_SCC_WRITE(9, 0x80);
	}

	za->za_flags = ZAS_REFUSE;
	zs->zs_flags &= ~MCP_PORT_ATTACHED;
	zs->mcp_ops = NULL;

	mutex_exit(&zs->zs_excl_hi);
	mutex_exit(&zs->zs_excl);

	cv_destroy(za->za_flags_cv);
	mutex_destroy(&zs->zs_excl_hi);
	mutex_destroy(&zs->zs_excl);

	if (za->za_flags_cv) {
		kmem_free(za->za_flags_cv, sizeof (kcondvar_t));
		za->za_flags_cv = NULL;
	}

	bzero((caddr_t)zs, sizeof (mcpcom_t));
	bzero((caddr_t)za, sizeof (mcpaline_t));

	sprintf(name, "%d", instance);
	ddi_remove_minor_node(dip, name);
	sprintf(name, "%d,cu", instance);
	ddi_remove_minor_node(dip, name);

	ddi_soft_state_free(mcpzsa_state_p, instance);

	return (DDI_SUCCESS);
}

static int
mcpzsa_get_dev_info(dev_info_t *dip, ddi_info_cmd_t cmd, void *arg,
	void **result)
{
	int 	instance;
	int	error = DDI_FAILURE;
	mcpzsa_soft_t *mcpzsap;

#ifdef lint
	dip = dip;
#endif
	switch (cmd) {
	case DDI_INFO_DEVT2DEVINFO:
		instance = getminor((dev_t)arg);
		mcpzsap = ddi_get_soft_state(mcpzsa_state_p, instance);
		if (mcpzsap) {
			*result = (void *)mcpzsap->dip;
			error = DDI_SUCCESS;
		}
		break;

	case DDI_INFO_DEVT2INSTANCE:
		*result = (void *)getminor((dev_t)arg);
		error = DDI_SUCCESS;
		break;

	default:
		break;
	}

	return (error);
}

/*
 * Async Streams Routines.
 */

static int
mcpzsa_open(queue_t *q, dev_t *dev, int flag, int sflag, cred_t *credp)
{
	int		instance, zs_unit;
	mcp_iface_t	*iface;
	mcpaline_t	*za;
	mcpcom_t	*zs;
	mcp_state_t	*softp;
	mcpzsa_soft_t	*mcpzsap;
	struct termios	*termiosp;
	int len;

#ifdef lint
	sflag = sflag;
#endif
	/* Set up the ZS and ZA. */
	zs_unit = MCP_ZS_UNIT(*dev);
/*	instance = MCP_INSTANCE(*dev); */
	instance = (getminor(*dev) & ~MCP_OUTLINE);

	DEBUGF(1, (CE_CONT, "mcpzsa_open: instance, unit = (%d, %d)\n",
		instance, zs_unit));

	if (!(mcpzsap = (mcpzsa_soft_t *)ddi_get_soft_state(mcpzsa_state_p,
		instance)))
			return (ENODEV);
	iface = mcpzsap->iface;
	softp = (mcp_state_t *)iface->priv;

	zs = &softp->mcpcom[zs_unit];
	za = &softp->mcpaline[zs_unit];
	DEBUGF(1, (CE_CONT, "mcpzsa_open: mc_unit = %d\n",
		zs->mc_unit));

	mutex_enter(&zs->zs_excl);

again:
	if (za->za_flags & ZAS_REFUSE) {
		mutex_exit(&zs->zs_excl);
		return (ENODEV);
	}

	/*
	 * Sanity check: za_common and zs should agree.  If they don't
	 * either something is broken, or we're trying to open a port
	 * that never got attached.
	 */

	if (zs != (mcpcom_t *)za->za_common) {
		mutex_exit(&zs->zs_excl);
		return (ENODEV);
	}

	mutex_enter(&zs->zs_excl_hi);

	if (zs->mcp_ops == NULL)
		zs->mcp_ops = &mcpops_async;

	if (zs->mcp_ops != &mcpops_async) {
		mutex_exit(&zs->zs_excl_hi);
		mutex_exit(&zs->zs_excl);
		return (EPROTO);
		/* NOTREACHED */
	}

	zs->zs_priv = (caddr_t)za;
	za->za_dev = *dev;

	if ((za->za_flags & ZAS_ISOPEN) == 0) {
		/*
		 * Get the default termio settings (cflag).
		 * These are stored as a property in the
		 * "options" node.
		 */
		mutex_exit(&zs->zs_excl_hi);
		if (ddi_getlongprop(DDI_DEV_T_ANY, ddi_root_node(), 0,
		    "ttymodes", (caddr_t)&termiosp, &len) == DDI_PROP_SUCCESS &&
		    len == sizeof (struct termios)) {
			mutex_enter(&zs->zs_excl_hi);
			za->za_ttycommon.t_cflag = termiosp->c_cflag;
			kmem_free(termiosp, len);
		} else {
			cmn_err(CE_WARN,
			    "mcpzsa: Couldn't get ttymodes property.\n");
			mutex_enter(&zs->zs_excl_hi);
		}
		za->za_overrun = 0;
		za->za_ttycommon.t_iflag = 0;
		za->za_ttycommon.t_iocpending = NULL;
		za->za_ttycommon.t_size.ws_row = 0;
		za->za_ttycommon.t_size.ws_col = 0;
		za->za_ttycommon.t_size.ws_xpixel = 0;
		za->za_ttycommon.t_size.ws_ypixel = 0;
		za->za_wbufcid = 0;

		mcpzsa_zs_program(za);
	}

	else if ((za->za_ttycommon.t_flags & TS_XCLUDE) && !drv_priv(credp)) {
		mutex_exit(&zs->zs_excl_hi);
		mutex_exit(&zs->zs_excl);
		return (EBUSY);
	}

	else if ((*dev & MCP_OUTLINE) && !(za->za_flags & ZAS_OUT)) {
		mutex_exit(&zs->zs_excl_hi);
		mutex_exit(&zs->zs_excl);
		return (EBUSY);
	}

	(void) mcpzsa_mctl(za, ZS_ON, DMSET);
	if (*dev & MCP_OUTLINE)
		za->za_flags |= ZAS_OUT;

	/* Check carrier.  */
	if ((za->za_ttycommon.t_flags & TS_SOFTCAR) ||
			(mcpzsa_mctl(za, 0, DMGET) & ZSRR0_CD))
		za->za_flags |= ZAS_CARR_ON;

	mutex_exit(&zs->zs_excl_hi);

	/* Unless DTR is held high by softcarrier, set HUPCL.  */
	if ((za->za_ttycommon.t_flags & TS_SOFTCAR) == 0)
		za->za_ttycommon.t_cflag |= HUPCL;

	/*
	 * If FNDELAY clear, block until carrier up. Quit on interrupt.
	 */

	if (!(flag & (FNONBLOCK | FNDELAY)) &&
			!(za->za_ttycommon.t_cflag & CLOCAL)) {
		if (!(za->za_flags & (ZAS_CARR_ON | ZAS_OUT)) ||
				(za->za_flags & ZAS_OUT) &&
				!(*dev & MCP_OUTLINE)) {
			za->za_flags |= ZAS_WOPEN;

			if (!(COND_WAIT_SIG(za->za_flags_cv, PRISAME,
					(lock_t *)&zs->zs_excl, 0))) {
				za->za_flags &= ~ZAS_WOPEN;
				mutex_exit(&zs->zs_excl);
				return (EINTR);
			}
			za->za_flags &= ~ZAS_WOPEN;
			goto again;
		}
	} else if (za->za_flags & ZAS_OUT && !(*dev & MCP_OUTLINE)) {
		mutex_exit(&zs->zs_excl);
		return (EBUSY);
	}

	za->za_ttycommon.t_readq = q;
	za->za_ttycommon.t_writeq = WR(q);
	q->q_ptr = WR(q)->q_ptr = (caddr_t)za;
	za->za_flags &= ~ZAS_WOPEN;
	za->za_flags |= ZAS_ISOPEN;
	qprocson(q);
	mutex_exit(&zs->zs_excl);


	DEBUGF(1, (CE_CONT, "mcpzsa_open: exiting ...\n"));
	return (0);
}

/*ARGSUSED1*/
static int
mcpzsa_close(queue_t *q, int flag, cred_t *credp)
{
	mcpaline_t 	*za;
	mcpcom_t	*zs;
	mcp_state_t	*softp;
	int		temp_tid, temp_cid;

	if ((za = (mcpaline_t *)q->q_ptr) == NULL)
		return (ENODEV);

	zs = (mcpcom_t *)za->za_common;
	softp = (mcp_state_t *)zs->zs_state;

	DEBUGF(1, (CE_CONT, "mcpzsa_close: unit = 0x%d\n", zs->mc_unit));

	/* Did we already close this once? */
	mutex_enter(&zs->zs_excl);

	/*
	 * if we still have carrier, wait here until all data is gone
	 * or we are interrupted
	 */

	while ((za->za_flags & ZAS_CARR_ON) &&
			((WR(q)->q_count != 0) || (za->za_flags & ZAS_BUSY)))
		if (COND_WAIT_SIG(lbolt_condp, PRISAME,
				(lock_t *)&zs->zs_excl, 0) == FALSE)
			break;

	if (za->za_flags & (ZAS_BUSY|ZAS_PAUSED)) {
		mutex_enter(&zs->zs_excl_hi);
		if (*za->za_devctl & TXENABLE)
			*za->za_devctl &= ~TXENABLE;
		MCP_DMA_HALT(&softp->iface, zs->mcp_txdma);
		mutex_exit(&zs->zs_excl_hi);
		za->za_flags &= ~(ZAS_BUSY|ZAS_PAUSED);
	}

	/*
	 * If break is in progress, stop it.
	 */
	mutex_enter(&zs->zs_excl_hi);

	if (zs->zs_wreg[5] & ZSWR5_BREAK) {
		MCP_SCC_BIC(5, ZSWR5_BREAK);
		za->za_flags &= ~ZAS_BREAK;
	}

	za->za_ocnt = 0;

	/*
	 * If line isn't completely opened, or has HUPCL set,
	 * or has changed the status of TS_SOFTCAR,
	 * fix up the modem lines.
	 */

	if (((za->za_flags & (ZAS_WOPEN|ZAS_ISOPEN)) != ZAS_ISOPEN) ||
			(za->za_ttycommon.t_cflag & HUPCL) ||
			(za->za_flags & ZAS_SOFTC_ATTN)) {
		/*
		 * If DTR is being held high by softcarrier,
		 * set up the ZS_ON set; if not, hang up.
		 */
		if (za->za_ttycommon.t_flags & TS_SOFTCAR)
			(void) mcpzsa_mctl(za, ZS_ON, DMSET);
		else
			(void) mcpzsa_mctl(za, ZS_OFF, DMSET);

		mutex_exit(&zs->zs_excl_hi);

		/*
		 * Don't let an interrupt in the middle of close
		 * bounce us back to the top; just continue closing
		 * as if nothing had happened.
		 */
		delay(drv_usectohz(500000));

		mutex_enter(&zs->zs_excl_hi);
	}

	if ((za->za_flags & (ZAS_ISOPEN | ZAS_WOPEN)) == 0)
		MCP_SCC_BIC(1, ZSWR1_RIE);

	mutex_exit(&zs->zs_excl_hi);

out:
	/* Clear out device state.  */
	za->za_flags = 0;
	ttycommon_close(&za->za_ttycommon);
	COND_BROADCAST(za->za_flags_cv, NOPRMT);

	temp_cid = za->za_wbufcid;
	za->za_wbufcid = 0;

	temp_tid = za->za_polltid;
	za->za_polltid = 0;

	q->q_ptr = WR(q)->q_ptr = NULL;
	za->za_ttycommon.t_readq = NULL;
	za->za_ttycommon.t_writeq = NULL;

	mutex_exit(&zs->zs_excl);

	/* Cancel outstanding timeout */
	if (temp_tid) {
		untimeout(temp_tid);
	}
	/* Cancel outstanding "bufcall" request. */
	if (temp_cid)
		unbufcall(temp_cid);

	qprocsoff(q);
	return (0);
}

static void
mcpzsa_ack(mp, dp, size)
	mblk_t	*mp;
	mblk_t	*dp;
	uint	size;
{
	register struct iocblk  *iocp = (struct iocblk *)mp->b_rptr;

	mp->b_datap->db_type = M_IOCACK;
	iocp->ioc_count = size;
	iocp->ioc_error = 0;
	iocp->ioc_rval = 0;
	if (mp->b_cont != NULL)
		freeb(mp->b_cont);
	if (dp != NULL) {
		mp->b_cont = dp;
		dp->b_wptr += size;
	} else
		mp->b_cont = NULL;
}

static void
mcpzsa_copy(mp, dp, size, type)
	mblk_t		*mp;
	mblk_t		*dp;
	uint		size;
	unsigned char	type;
{
	register struct copyreq *cp = (struct copyreq *)mp->b_rptr;

	cp->cq_private = NULL;
	cp->cq_flag = 0;
	cp->cq_size = size;
	cp->cq_addr = (caddr_t)(*(long *)(mp->b_cont->b_rptr));
	if (mp->b_cont != NULL)
		freeb(mp->b_cont);
	if (dp != NULL) {
		mp->b_cont = dp;
		dp->b_wptr = dp->b_rptr + size;
	} else
		mp->b_cont = NULL;
	mp->b_datap->db_type = type;
	mp->b_wptr = mp->b_rptr + sizeof (*cp);
}

static void
mcpzsa_sendnak(queue_t *q, mblk_t *mp, int err)
{
	struct iocblk		*iocp = (struct iocblk *)mp->b_rptr;

	mp->b_datap->db_type = M_IOCNAK;
	iocp->ioc_count = 0;
	iocp->ioc_error = err;
	qreply(q, mp);
}

static int
mcpzsa_wput(queue_t *q, mblk_t *mp)
{
	mcpaline_t 	*za;
	mcpcom_t	*zs;
	mcp_state_t	*softp;
	struct copyresp	*resp;


	ASSERT(q != NULL);
	ASSERT(mp != NULL);

	za = (mcpaline_t *)q->q_ptr;
	zs = (mcpcom_t *)za->za_common;
	softp = (mcp_state_t *)zs->zs_state;

	DEBUGF(1, (CE_CONT, "mcpzsa_wput%d: db_type = 0x%x\n",
		zs->mc_unit, mp->b_datap->db_type));

	switch (mp->b_datap->db_type) {
	case M_STOP:
		mutex_enter(&zs->zs_excl);
		if (!(za->za_flags & ZAS_STOPPED)) {
			za->za_flags |= ZAS_STOPPED;
			if (za->za_flags & ZAS_BUSY) {
				mutex_enter(&zs->zs_excl_hi);
				if (*za->za_devctl & TXENABLE)
					*za->za_devctl &= ~TXENABLE;
				mutex_exit(&zs->zs_excl_hi);
				za->za_flags |= ZAS_PAUSED;
				za->za_flags &= ~ZAS_BUSY;
			}
		}
		mutex_exit(&zs->zs_excl);
		freemsg(mp);
		break;

	case M_START:
		mutex_enter(&zs->zs_excl);
		if (za->za_flags & ZAS_STOPPED) {
			za->za_flags &= ~ ZAS_STOPPED;
			mcpzsa_start(za);
		}
		mutex_exit(&zs->zs_excl);

		freemsg(mp);
		break;

	case M_IOCTL:
		switch (((struct iocblk *)mp->b_rptr)->ioc_cmd) {
		case TCSETSW:
		case TCSETSF:
		case TCSETAW:
		case TCSETAF:
		case TCSBRK:
			/*
			 * The changes do not take effect until all output
			 * queued before them is drained. Put this message on
			 * the queue, so that "mcpstart" will see it when
			 * it's done with the output before it. Poke the
			 * start routine, just in case.
			 */

			mutex_enter(&zs->zs_excl);
			(void) putq(q, mp);
			mcpzsa_start(za);
			mutex_exit(&zs->zs_excl);
			break;

		default:
			mcpzsa_ioctl(za, q, mp);
			break;
		}
		break;

	case M_IOCDATA:

		resp = (struct copyresp *)mp->b_rptr;
		if (resp->cp_rval) {
		    freemsg(mp);
		    break;
		}
		switch (resp->cp_cmd) {

		case TIOCMSET:
			mutex_enter(&zs->zs_excl);
			mutex_enter(&zs->zs_excl_hi);
			(void) mcpzsa_mctl(za,
			    dm_to_mcp(*(int *)mp->b_cont->b_rptr), DMSET);
			mutex_exit(&zs->zs_excl_hi);
			mutex_exit(&zs->zs_excl);
			mcpzsa_ack(mp, (mblk_t *)NULL, 0);
			qreply(q, mp);
			break;

		case TIOCMBIC:
			mutex_enter(&zs->zs_excl);
			mutex_enter(&zs->zs_excl_hi);
			(void) mcpzsa_mctl(za,
			    dm_to_mcp(*(int *)mp->b_cont->b_rptr), DMBIC);
			mutex_exit(&zs->zs_excl_hi);
			mutex_exit(&zs->zs_excl);
			mcpzsa_ack(mp, (mblk_t *)NULL, 0);
			qreply(q, mp);
			break;

		case TIOCMBIS:
			mutex_enter(&zs->zs_excl);
			mutex_enter(&zs->zs_excl_hi);
			(void) mcpzsa_mctl(za,
			    dm_to_mcp(*(int *)mp->b_cont->b_rptr), DMBIS);
			mutex_exit(&zs->zs_excl_hi);
			mutex_exit(&zs->zs_excl);
			mcpzsa_ack(mp, (mblk_t *)NULL, 0);
			qreply(q, mp);
			break;

		case TIOCMGET:
			mcpzsa_ack(mp, (mblk_t *)NULL, 0);
			qreply(q, mp);
			break;

		default:
			freemsg(mp);

		}
		break;


	case M_FLUSH:
		if (*mp->b_rptr & FLUSHW) {
			/* Abort any output in progress.  */
			mutex_enter(&zs->zs_excl);
			if (za->za_flags & (ZAS_BUSY|ZAS_PAUSED)) {
				mutex_enter(&zs->zs_excl_hi);
				if (*za->za_devctl & TXENABLE)
					*za->za_devctl &= ~TXENABLE;
				MCP_DMA_HALT(&softp->iface, zs->mcp_txdma);
				mutex_exit(&zs->zs_excl_hi);
				za->za_ocnt = 0;
				za->za_flags &= ~(ZAS_BUSY|ZAS_PAUSED);
			}

			flushq(q, FLUSHDATA);
			mutex_exit(&zs->zs_excl);

			*mp->b_rptr &= ~FLUSHW;
		}

		if (*mp->b_rptr & FLUSHR) {
			mutex_enter(&zs->zs_excl);
			flushq(RD(q), FLUSHDATA);
			mutex_exit(&zs->zs_excl);
			qreply(q, mp);
		} else
			freemsg(mp);

		/*
		 * We must make sure we process messages that survive the
		 * write-side flush.  Without this call, the close protocol
		 * with ldterm can hang forever.
		 */
		mutex_enter(&zs->zs_excl);
		mcpzsa_start(za);
		mutex_exit(&zs->zs_excl);
		break;

	case M_BREAK:
	case M_DELAY:
	case M_DATA:
		/*
		 * Queue the message up to be transmitted, and poke the start
		 * routine.
		 */
		mutex_enter(&zs->zs_excl);
		(void) putq(q, mp);
		mcpzsa_start(za);
		mutex_exit(&zs->zs_excl);

		break;

	case M_STOPI:
		mutex_enter(&zs->zs_excl);
		za->za_flowc = za->za_ttycommon.t_stopc;
		mcpzsa_start(za);
		mutex_exit(&zs->zs_excl);

		freemsg(mp);
		break;

	case M_STARTI:
		mutex_enter(&zs->zs_excl);
		za->za_flowc = za->za_ttycommon.t_startc;
		mcpzsa_start(za);
		mutex_exit(&zs->zs_excl);
		freemsg(mp);
		break;

	case M_CTL:
		/*
		 * These MC_SERVICE type messages are used by upper
		 * modules to tell this driver to send input up
		 * immediately, or that it can wait for normal
		 * processing that may or may not be done.  Sun
		 * requires these for the mouse module.
		 */
		mutex_enter(&zs->zs_excl);

		mcpzsa_mblk_debug(mp);

		switch (*mp->b_rptr) {
		case MC_SERVICEIMM:
			za->za_flags |= ZAS_SERVICEIMM;
			break;

		case MC_SERVICEDEF:
			za->za_flags &= ~ZAS_SERVICEIMM;
			break;
		}

		mutex_exit(&zs->zs_excl);

		freemsg(mp);
		break;

	default:
		freemsg(mp);
		break;
	}

	return (0);
}

static int
mcpzsa_ioctl(mcpaline_t *za, queue_t *q, mblk_t *mp)
{
	struct iocblk	*iocp;
	unsigned	datasize;
	mcpcom_t	*zs = (mcpcom_t *)za->za_common;
	struct termios	*cb;
	register mblk_t	*tmp;
	int	error;

	/*
	 * If we were holding an "ioctl" response pending the availability
	 * of an "mblk" to hold data to be passed up; another "ioctl" came
	 * through, which means that "ioctl" must have timed out or been
	 * aborted.
	 */

	if (za->za_ttycommon.t_iocpending != NULL) {
		freemsg(za->za_ttycommon.t_iocpending);
		za->za_ttycommon.t_iocpending = NULL;
	}

	iocp = (struct iocblk *)mp->b_rptr;
	DEBUGF(1, (CE_CONT, "mcpzsa_ioctl%d: iocp->ioc_cmd = 0x%x\n",
		zs->mc_unit, iocp->ioc_cmd));
	/*
	 * The only way which "ttycommon_ioctl" can fail is if the "ioctl"
	 * requires a response containing data to be returned to the user,
	 * and no mblk could be allocated for the data.  No such "ioctl"
	 * alters our state.  Thus, we always go ahead and do any
	 * state-changes the "ioctl" calls for.  If we couldn't allocate
	 * the data, "ttycommon_ioctl" has stashed the "ioctl" away safely,
	 * so we just call "bufcall" to request that we be called back when
	 * we stand a better chance of allocating data.
	 */

	datasize = ttycommon_ioctl(&za->za_ttycommon, q, mp, &error);
	if (datasize != 0) {
		DEBUGF(1, (CE_CONT, "mcpzsa_ioctl: initiating bufcall\n"));
		if (za->za_wbufcid)
			unbufcall(za->za_wbufcid);
		za->za_wbufcid = bufcall(datasize, BPRI_HI, mcpzsa_reioctl,
			(long)za);
		return (0);
	}

	mutex_enter(&zs->zs_excl);

	if (error == 0) {
		/*
		 * "ttycommon_ioctl" did most of the work; we just use the
		 * data it set up. Two exceptions:
		 * (1) if it's any kind of SET ioctl, we also grab the
		 * VLNEXT char for use when we stash the input
		 * characters. See mcpzsa_rxchar() below.
		 * (2) if it affects SOFTCAR, set the ATTN flag to force
		 * a reset of the modem line status on close.
		 */

		switch (iocp->ioc_cmd) {
		/*
		 * Only the termios-related ioctls define the VLNEXT
		 * character, so we can only reset that character's value from
		 * those TC* ioctls.
		 */
		case TCSETS:
		case TCSETSW:
		case TCSETSF:
			cb = (struct termios *)mp->b_cont->b_rptr;
			za->za_lnext = cb->c_cc[VLNEXT];
			/*FALLTHROUGH*/

		case TCSETA:
		case TCSETAW:
		case TCSETAF:
			mutex_enter(&zs->zs_excl_hi);
			mcpzsa_zs_program(za);
			mutex_exit(&zs->zs_excl_hi);
			break;

		case TIOCSSOFTCAR:
			za->za_flags |= ZAS_SOFTC_ATTN;
			break;
		}
	} else if (error < 0) {
		/*
		 * "ttycommon_ioctl" didn't do anything; we process it here.
		 */

		error = 0;
		mutex_enter(&zs->zs_excl_hi);

		switch (iocp->ioc_cmd) {

		case TCSBRK:
			if (*(int *)mp->b_cont->b_rptr == 0) {
				/*
				 * Set the break bit, and arrange for
				 * "mcp_restart" to be called in 1/4 second; it
				 * will turn the break bit off, and call
				 * "mcpstart" to grab the next message.
				 */

				MCP_SCC_BIS(5, ZSWR5_BREAK);
				(void) timeout(mcpzsa_restart, (caddr_t)za,
				    hz / 4);
				za->za_flags |= ZAS_BREAK;
			}
			mcpzsa_ack(mp, (mblk_t *)NULL, 0);
			break;

		case TIOCSBRK:
			MCP_SCC_BIS(5, ZSWR5_BREAK);
			mcpzsa_ack(mp, (mblk_t *)NULL, 0);
			break;

		case TIOCCBRK:
			MCP_SCC_BIC(5, ZSWR5_BREAK);
			mcpzsa_ack(mp, (mblk_t *)NULL, 0);
			break;

		case TIOCMSET:
			if (iocp->ioc_count != TRANSPARENT) {
			    (void) mcpzsa_mctl(za,
				dm_to_mcp(*(int *)mp->b_cont->b_rptr), DMSET);
			    mcpzsa_ack(mp, (mblk_t *)NULL, 0);
			} else {
			    mcpzsa_copy(mp, (mblk_t *)NULL, sizeof (int),
				M_COPYIN);
			}
			/* qreply done below */
			break;

		case TIOCMBIS:
			if (iocp->ioc_count != TRANSPARENT) {
			    (void) mcpzsa_mctl(za,
				dm_to_mcp(*(int *)mp->b_cont->b_rptr), DMBIS);
			    mcpzsa_ack(mp, (mblk_t *)NULL, 0);
			} else {
			    mcpzsa_copy(mp, (mblk_t *)NULL, sizeof (int),
				M_COPYIN);
			}
			/* qreply done below */
			break;

		case TIOCMBIC:
			if (iocp->ioc_count != TRANSPARENT) {
			    (void) mcpzsa_mctl(za,
				dm_to_mcp(*(int *)mp->b_cont->b_rptr), DMBIC);
			    mcpzsa_ack(mp, (mblk_t *)NULL, 0);
			} else {
			    mcpzsa_copy(mp, (mblk_t *)NULL, sizeof (int),
				M_COPYIN);
			}
			/* qreply done below */
			break;

		case TIOCMGET:
			tmp = allocb(sizeof (int), BPRI_MED);
			if (tmp == NULL) {
				mutex_exit(&zs->zs_excl_hi);
				mutex_exit(&zs->zs_excl);
				mcpzsa_sendnak(q, mp, EAGAIN);
				return (0);
			}
			if (iocp->ioc_count != TRANSPARENT) {
			    *(int *)tmp->b_rptr =
				mcp_to_dm(mcpzsa_mctl(za, 0, DMGET));
			    mcpzsa_ack(mp, (mblk_t *)tmp, 0);
			} else {
			    *(int *)tmp->b_rptr =
				mcp_to_dm(mcpzsa_mctl(za, 0, DMGET));
			    mcpzsa_copy(mp, (mblk_t *)tmp, sizeof (int),
				M_COPYOUT);
			}
			/* qreply done below */
			break;

		default:
			/* We don't understand it either.  */
			error = EINVAL;
			break;
		}
		mutex_exit(&zs->zs_excl_hi);
	}
	mutex_exit(&zs->zs_excl);

	if (error) {
		iocp->ioc_error = error;
		mp->b_datap->db_type = M_IOCNAK;
	}

	qreply(q, mp);
	return (0);
}

static void
mcpzsa_reioctl(mcpaline_t *za)
{
	mcpcom_t	*zs = (mcpcom_t *)za->za_common;
	queue_t		*q;
	mblk_t		*mp;

	/*
	 * The bufcall is no longer pending.
	 */
	mutex_enter(&zs->zs_excl);
	za->za_wbufcid = 0;
	if ((q = za->za_ttycommon.t_writeq) == NULL) {
		mutex_exit(&zs->zs_excl);
		return;
	}

	if ((mp = za->za_ttycommon.t_iocpending) != NULL) {
		za->za_ttycommon.t_iocpending = NULL;
		mutex_exit(&zs->zs_excl);
		mcpzsa_ioctl(za, q, mp);
	} else
		mutex_exit(&zs->zs_excl);
}

static void
mcpzsa_zs_program(mcpaline_t *za)
{
	mcpcom_t	*zs = (struct mcpcom *) za->za_common;
	mcp_state_t	*softp = (mcp_state_t *)zs->zs_state;
	mcp_iface_t	*iface = &softp->iface;
	struct zs_prog	*zspp;
	u_int		wr3, wr4, wr5, speed, baudrate, loops, flags = 0;
	u_char		v;

	ASSERT(mutex_owned(&zs->zs_excl));
	ASSERT(mutex_owned(&zs->zs_excl_hi));

	/* Hang up if null baudrate.  */
	if ((baudrate = za->za_ttycommon.t_cflag & CBAUD) == 0) {
		/* Hang Up line. */
		(void) mcpzsa_mctl(za, ZS_OFF, DMSET);
		return;
	}

	DEBUGF(1, (CE_CONT, "mcpzsa_zs_program%d: t_cflag = %o\n",
		zs->mc_unit, za->za_ttycommon.t_cflag));

	wr3 = (za->za_ttycommon.t_cflag & CREAD) ? ZSWR3_RX_ENABLE : 0;
	wr4 = ZSWR4_X16_CLK;
	wr5 = (zs->zs_wreg[5] & (ZSWR5_RTS | ZSWR5_DTR)) | ZSWR5_TX_ENABLE;

	if (mcpb134_weird && baudrate == B134) {
		/*
		 * XXX - should B134 set all this stuff in the compatibility
		 * module, leaving this stuff fairly clean?
		 */
		flags |= ZSP_PARITY_SPECIAL;
		wr3 |= ZSWR3_RX_6;
		wr4 |= ZSWR4_PARITY_ENABLE | ZSWR4_PARITY_EVEN;
		wr4 |= ZSWR4_1_5_STOP;
		wr5 |= ZSWR5_TX_6;
	} else {
		switch (za->za_ttycommon.t_cflag & CSIZE) {
		case CS5:
			wr3 |= ZSWR3_RX_5;
			wr5 |= ZSWR5_TX_5;
			zs->zs_mask = 0x1f;
			break;

		case CS6:
			wr3 |= ZSWR3_RX_6;
			wr5 |= ZSWR5_TX_6;
			zs->zs_mask = 0x3f;
			break;

		case CS7:
			wr3 |= ZSWR3_RX_7;
			wr5 |= ZSWR5_TX_7;
			zs->zs_mask = 0x7f;
			break;

		case CS8:
			wr3 |= ZSWR3_RX_8;
			wr5 |= ZSWR5_TX_8;
			zs->zs_mask = 0xff;
			break;
		}

		if (za->za_ttycommon.t_cflag & PARENB) {
			/*
			 * The PARITY_SPECIAL bit causes a special rx
			 * interrupt on parity errors. Turn it on iff we're
			 * checking the parity of characters.
			 */

			if (za->za_ttycommon.t_iflag & INPCK)
				flags |= ZSP_PARITY_SPECIAL;
			wr4 |= ZSWR4_PARITY_ENABLE;
			if (!(za->za_ttycommon.t_cflag & PARODD))
				wr4 |= ZSWR4_PARITY_EVEN;
		}

		wr4 |= (za->za_ttycommon.t_cflag & CSTOPB) ?
			ZSWR4_2_STOP : ZSWR4_1_STOP;
	}

	/*
	 * The AUTO_CD_CTS flag enables the hardware flow control feature of
	 * the 8530, which allows the state of CTS and DCD to control the
	 * enabling of the transmitter and receiver, respectively.  The
	 * receiver and transmitter still must have their enable bits set in
	 * WR3 and WR5, respectively, for CTS and DCD to be monitored this way.
	 * Hardware flow control can thus be implemented with no help from
	 * software.
	 */
	if ((za->za_ttycommon.t_cflag & CRTSCTS) && ((za->za_dev & 0xf) < 4))
		wr3 |= ZSWR3_AUTO_CD_CTS;

	if (za->za_ttycommon.t_iflag & IXON)
		*za->za_xoff = za->za_ttycommon.t_stopc;
	else
		*za->za_xoff = DISABLE_XOFF;

	speed = zs->zs_wreg[12] + (zs->zs_wreg[13] << 8);
	if (((zs->zs_wreg[1] & ZSWR1_PARITY_SPECIAL) &&
			!(flags & ZSP_PARITY_SPECIAL)) ||
			(!(zs->zs_wreg[1] & ZSWR1_PARITY_SPECIAL) &&
			(flags & ZSP_PARITY_SPECIAL)) ||
			wr3 != zs->zs_wreg[3] ||
			wr4 != zs->zs_wreg[4] || wr5 != zs->zs_wreg[5] ||
			speed != mcp_speeds[baudrate]) {

		if (*za->za_devctl & TXENABLE) {
			*za->za_devctl &= ~TXENABLE;
			za->za_flags |= ZAS_PAUSED;
		}

		/*
		 * Wait for that last damn character to get out the door.
		 * At most 1000 loops of 100 usec each is worst case of 110
		 * baud.  If something appears on the output queue then
		 * somebody higher up isn't synchronized and we give up.
		 */
		for (loops = 0; loops < 1000; ++loops) {
			MCP_SCC_READ(1, v);
			if (v & ZSRR1_ALL_SENT)
				break;
			drv_usecwait(1);
		}

		/* Disable Fifo while programming ZS. */
		*za->za_devctl &= ~EN_FIFO_RX;
		MCP_SCC_WRITE(3, 0);

		zspp = &(softp->zsp[zs->zs_unit]);
		zspp->zs = (struct zscom *)zs;

		zspp->flags = (u_char)flags;
		zspp->wr4 = (u_char)wr4;
		zspp->wr11 = (u_char)(ZSWR11_TXCLK_BAUD | ZSWR11_RXCLK_BAUD);

		speed = mcp_speeds[baudrate];
		zspp->wr12 = (u_char)(speed & 0xff);
		zspp->wr13 = (u_char)((speed >> 8) & 0xff);

		zspp->wr3 = (u_char)wr3;
		zspp->wr5 = (u_char)wr5;
		zspp->wr15 = (u_char)(ZSR15_BREAK | ZSR15_CTS | ZSR15_CD);
		MCP_ZS_PROGRAM(iface, zspp);

		DEBUGF(1, (CE_CONT,
			"mcpzsa_zs_program%d: returned from mcp_zs_program\n",
			zs->mc_unit));

		/* Re-enable the FIFO and start this port */
		*za->za_devctl |= EN_FIFO_RX;
		mutex_exit(&zs->zs_excl_hi);
		mcpzsa_start(za);
		mutex_enter(&zs->zs_excl_hi);
	}
}

static int
dm_to_mcp(int bits)
{
	int    b = 0;

	if (bits & TIOCM_CAR)
		b |= ZSRR0_CD;

	if (bits & TIOCM_CTS)
		b |= ZSRR0_CTS;

	if (bits & TIOCM_RTS)
		b |= ZSWR5_RTS;

	if (bits & TIOCM_DTR)
		b |= ZSWR5_DTR;

	return (b);
}

static int
mcp_to_dm(int bits)
{
	int    b = 0;

	if (bits & ZSRR0_CD)
		b |= TIOCM_CAR;

	if (bits & ZSRR0_CTS)
		b |= TIOCM_CTS;

	if (bits & ZSWR5_RTS)
		b |= TIOCM_RTS;

	if (bits & ZSWR5_DTR)
		b |= TIOCM_DTR;
	return (b);
}

static int
mcpzsa_mctl(mcpaline_t *za, int bits, int how)
{
	mcpcom_t 	*zs = (mcpcom_t *)za->za_common;
	int s0, mbits, obits;
	int now, held;

	ASSERT(mutex_owned(&zs->zs_excl));
	ASSERT(mutex_owned(&zs->zs_excl_hi));

again:
	mbits = zs->zs_wreg[5] & (ZSWR5_RTS | ZSWR5_DTR);
	MCP_SCC_WRITE0(ZSWR0_RESET_STATUS);
	s0 = MCP_SCC_READ0();
	MCP_ZSDELAY();

	mbits |= s0 & (ZSRR0_CD | ZSRR0_CTS);
	obits = mbits;

	switch (how) {
	case DMSET:
		mbits = bits;
		break;

	case DMBIS:
		mbits |= bits;
		break;

	case DMBIC:
		mbits &= ~bits;
		break;

	case DMGET:
		return (mbits);
	}

	now = hrestime.tv_sec;
	held = now - zs->zs_dtrlow;

	/* If DTR is going low, stash current time away. */
	if (~mbits & obits & ZSWR5_DTR)
		zs->zs_dtrlow = now;

	/*
	 * If DTR is going high, sleep until it has been low a bit.
	 */

	if ((mbits &~obits & ZSWR5_DTR) && (held < mcpzsadtrlow)) {
		mutex_exit(&zs->zs_excl_hi);
		/*
		 * settling time of 1 sec at a time
		 */
		delay(drv_usectohz(1000000));
		mutex_enter(&zs->zs_excl_hi);
		goto again;
	}

	zs->zs_wreg[5] &= ~(ZSWR5_RTS | ZSWR5_DTR);
	MCP_SCC_BIS(5, mbits & (ZSWR5_RTS | ZSWR5_DTR));
	if (mbits & ZSWR5_DTR)
		*za->za_devctl |= MCP_DTR_ON;
	else
		*za->za_devctl &= ~MCP_DTR_ON;

	/* XXX - need to let a lock go. */
	return (mbits);
}

static void
mcpzsa_start(mcpaline_t *za)
{
	mcpcom_t	*zs;
	mcp_state_t	*softp;
	int		cc, avail_bytes, loops;
	u_char		v, *cp;
	queue_t		*q;
	mblk_t		*bp, *nbp;
	int		max_buf_size;

	ASSERT(za != NULL);
	zs = (mcpcom_t *)za->za_common;
	ASSERT(mutex_owned(&zs->zs_excl));
	softp = (mcp_state_t *)zs->zs_state;

	/*
	 * If we're waiting for a break timeout to expire, don't grab
	 * anything new.
	 */

	if (za->za_flags & ZAS_BREAK) {
		DEBUGF(1, (CE_CONT, "mcpzsa_start%d: ZAS_BREAK\n",
			zs->mc_unit));
		return;
	}

	/*
	 * If we have a flow-control character to transmit, do it now.
	 * Suspend the current transmission, reach around the DMA chip, and
	 * stuff the flow-control character into the SCC. Then tidy up and go
	 * on our way. By the way, if we're a printer, forget it.
	 */

	if (za->za_flowc != '\0') {
		DEBUGF(1, (CE_CONT, "mcpzsa_start%d: flow control\n",
			zs->mc_unit));

		if (za->za_flags & ZAS_BUSY) {
			mutex_enter(&zs->zs_excl_hi);
			if (*za->za_devctl & TXENABLE)
				*za->za_devctl &= ~TXENABLE;
			mutex_exit(&zs->zs_excl_hi);
			za->za_flags |= ZAS_PAUSED;
		}

		/*
		 * Wait for end of transmission. See comment in mcpparam().
		 */
		loops = 1000;
		do {
			MCP_SCC_READ(1, v);
			if (v & ZSRR1_ALL_SENT)
				break;
			DELAY(100);
		} while (--loops > 0);

		mutex_enter(&zs->zs_excl_hi);

		cc = MCP_SCC_READ0();
		MCP_ZSDELAY();
		if (cc & ZSRR0_TX_READY) {
			MCP_SCC_WRITEDATA(za->za_flowc);
			za->za_flowc = '\0';
		}
		mutex_exit(&zs->zs_excl_hi);
	}

	/*
	 * If a DMA operation was terminated early, restart it unless output
	 * is stopped.
	 */

	if ((za->za_flags & ZAS_PAUSED) && !(za->za_flags & ZAS_STOPPED)) {
		DEBUGF(1, (CE_CONT, "mcpzsa_start%d: was ZAS_PAUSED\n",
			zs->mc_unit));
		za->za_flags &= ~ZAS_PAUSED;

		/*
		 * If there's data in the transmit DMA channel,
		 * we have to finish that first; mcpzsa_txend() will
		 * kick us again.
		 */

		mutex_enter(&zs->zs_excl_hi);
		if (MCP_DMA_GETWC(&softp->iface, zs->mcp_txdma)) {
			if (!(*za->za_devctl & TXENABLE))
				*za->za_devctl |= TXENABLE;
			za->za_flags |= ZAS_BUSY;
		}
		mutex_exit(&zs->zs_excl_hi);
	}

	/*
	 * If we're waiting for a delay timeout to expire or for the current
	 * transmission to complete, don't grab anything new.
	 */

	if (za->za_flags & (ZAS_BUSY | ZAS_DELAY)) {
		if (za->za_flags & ZAS_BUSY)
			DEBUGF(1, (CE_CONT, "mcpzsa_start%d: ZAS_BUSY\n",
				zs->mc_unit));
		if (za->za_flags & ZAS_DELAY)
			DEBUGF(1, (CE_CONT, "mcpzsa_start:%d ZAS_DELAY\n",
				zs->mc_unit));
		return;
	}

	/*
	 * Hopefully we're attached to a stream.
	 */
	if ((q = za->za_ttycommon.t_writeq) == NULL) {
		DEBUGF(1, (CE_CONT, "mcpzsa_start%d: q null\n",
			zs->mc_unit));
		return;
	}

	/*
	 * Set up to copy up to a bufferful of bytes into the MCP buffer.
	 */
	max_buf_size = ASYNC_BSZ;
	cp = za->za_dmabuf;
	avail_bytes = max_buf_size;

	for (;;) {
		if ((bp = getq(q)) == NULL) {
			return;
		}

		/*
		 * We have a message block to work on. Check whether it's a
		 * break, a delay, or an ioctl (the latter occurs if the
		 * ioctl in question was waiting for the output to drain). If
		 * it's one of those, process it immediately.
		 */

		switch (bp->b_datap->db_type) {
		case M_BREAK:
			if (avail_bytes != max_buf_size) {
				(void) putbq(q, bp);
				goto transmit;
			}

			/*
			 * Set the break bit, and arrange for "mcp_restart" to
			 * be called in 1/4 second; it will turn the break
			 * bit off, and call "mcp_restart" to grab the next
			 * message.
			 */

			mutex_enter(&zs->zs_excl_hi);
			MCP_SCC_BIS(5, ZSWR5_BREAK);
			mutex_exit(&zs->zs_excl_hi);

			(void) timeout(mcpzsa_restart, (caddr_t)za, hz / 4);
			za->za_flags |= ZAS_BREAK;

			freemsg(bp);
			return;

		case M_DELAY:
			if (avail_bytes != max_buf_size) {
				(void) putbq(q, bp);
				goto transmit;
			}

			/*
			 * Arrange for "mcp_restart" to be called when the delay
			 * expires; it will turn ZAS_DELAY off, then call
			 * "mcpstart" to grab the next message.
			 */

			(void) timeout(mcpzsa_restart, (caddr_t)za,
				(int)(*(unsigned char *)bp->b_rptr + 6));
			za->za_flags |= ZAS_DELAY;
			freemsg(bp);
			return;

		case M_IOCTL:
			if (avail_bytes != max_buf_size) {
				(void) putbq(q, bp);
				goto transmit;
			}

			DEBUGF(1, (CE_CONT, "mcpzsa_start%d: M_IOCTL\n",
				zs->mc_unit));

			/*
			 * This ioctl was waiting for the output ahead of it
			 * to drain; obviously, it has. Do it, and then grab
			 * the next message after it.
			 */

			mutex_exit(&zs->zs_excl);
			mcpzsa_ioctl(za, q, bp);
			mutex_enter(&zs->zs_excl);
			continue;

		case M_DATA:
			mcpzsa_mblk_debug(bp);
		}

		/*
		 * We have data to transmit. If output is stopped, put it
		 * back and try again later.
		 */

		if (za->za_flags & ZAS_STOPPED) {
			(void) putbq(q, bp);
			return;
		}

		za->za_ocnt = 0;

		while (bp != NULL) {
			while ((cc = bp->b_wptr - bp->b_rptr) != 0) {
				if (avail_bytes == 0) {
					(void) putbq(q, bp);
					goto transmit;
				}

				cc = MIN(cc, avail_bytes);

				/* Copy the bytes to the card. */
				bcopy((caddr_t)bp->b_rptr, (caddr_t)cp, cc);

				/* Update pointers and counters. */
				cp += cc;
				avail_bytes -= cc;
				bp->b_rptr += cc;
				za->za_ocnt += cc;
			}

			if (bp->b_wptr == bp->b_rptr) {
				if (bp->b_cont != NULL) {
					nbp = bp;
					bp = bp->b_cont;
					nbp->b_cont = NULL;

					freemsg(nbp);

					if (avail_bytes == 0) {
						(void) putbq(q, bp);
						goto transmit;
					}
				} else {
					freemsg(bp);
					goto transmit;
				}
			}
		}
	}

transmit:
	if (za->za_ocnt) {
		mutex_enter(&zs->zs_excl_hi);

		if (!(*za->za_devctl & TXENABLE))
			*za->za_devctl |= TXENABLE;
		zs->zs_flags |= MCP_WAIT_DMA;   /* Set this for mcpintr(). */

		MCP_DMA_START(&softp->iface, zs->mcp_txdma,
			(char *)za->za_dmabuf, za->za_ocnt);

		mutex_exit(&zs->zs_excl_hi);
		za->za_flags |= ZAS_BUSY;
	}
}

static void
mcpzsa_restart(mcpaline_t *za)
{
	mcpcom_t 	*zs = (mcpcom_t *)za->za_common;
	queue_t		*q;

	DEBUGF(1, (CE_CONT, "mcpzsa_start%d: restarting mcpzsa_start\n",
		zs->mc_unit));
	mutex_enter(&zs->zs_excl);

	/*
	 * If break timer expired, turn off break bit.
	 */
	if (za->za_flags & ZAS_BREAK) {
		mutex_enter(&zs->zs_excl_hi);
		MCP_SCC_BIC(5, ZSWR5_BREAK);
		mutex_exit(&zs->zs_excl_hi);
	}
	za->za_flags &= ~(ZAS_DELAY | ZAS_BREAK);
	if ((q = za->za_ttycommon.t_writeq) == NULL) {
		mutex_exit(&zs->zs_excl);
		return;
	}

	enterq(q);
	mcpzsa_start(za);
	leaveq(q);
	mutex_exit(&zs->zs_excl);
}

/*
 * Async Protocol Routines.
 */

/*ARGSUSED*/
static int
mcpzsa_txint(mcpcom_t *zs)
{
	return (0);
}

static int
mcpzsa_xsint(mcpcom_t *zs)
{
	mcpaline_t		*za = (mcpaline_t *)zs->zs_priv;
	mcp_state_t		*softp = (mcp_state_t *)zs->zs_state;
	u_char			x0, s0;
	queue_t			*q;
	int			m_break = 0;
	int			m_unhangup = 0;
	int			m_hangup = 0;

	mutex_enter(&zs->zs_excl);
	mutex_enter(&zs->zs_excl_hi);

	/*
	 * Found a break?
	 */
	s0 = MCP_SCC_READ0();
	MCP_ZSDELAY();

	x0 = s0 ^ za->za_rr0;
	za->za_rr0 = s0;

	MCP_SCC_WRITE0(ZSWR0_RESET_STATUS);

	if ((x0 & ZSRR0_BREAK) && (s0 & ZSRR0_BREAK)) {
		/*
		 * ZSRR0_BREAK turned off.  This means that the break sequence
		 * has completed (i.e., the stop bit finally arrived).
		 * Send M_BREAK upstream, and flag the interrupt routine to
		 * throw away the NUL character in the receiver. (The
		 * ldterm module upstream doesn't need it; it will use
		 * the M_BREAK to do the right thing.)
		 */

		MCP_SCC_WRITE0(ZSWR0_RESET_STATUS);
		++zs->zs_rerror;
		if ((q = za->za_ttycommon.t_readq) != NULL)
			m_break = 1;
	}

	/*
	 * Carrier Up?
	 */

	if ((za->za_ttycommon.t_flags & TS_SOFTCAR) ||
			(mcpzsa_mctl(za, 0, DMGET) & ZSRR0_CD)) {
		/* Carrier present */
		if ((za->za_flags & ZAS_CARR_ON) == 0) {
			za->za_flags |= ZAS_CARR_ON;
			if ((q = za->za_ttycommon.t_readq) != NULL)
				m_unhangup = 1;
			mutex_exit(&zs->zs_excl_hi);
			cv_broadcast(za->za_flags_cv);
		} else
			mutex_exit(&zs->zs_excl_hi);
	} else {
		if ((za->za_flags & ZAS_CARR_ON) &&
				!(za->za_ttycommon.t_cflag & CLOCAL)) {
			/*
			 * Carrier went away.
			 * Drop DTR, abort any output in progress,
			 * indicate that output is not stopped, and
			 * send a hangup notification upstream.
			 */
			(void) mcpzsa_mctl(za, ZSWR5_DTR, DMBIC);

			if (za->za_flags & (ZAS_BUSY|ZAS_PAUSED)) {
				if (*za->za_devctl & TXENABLE)
					*za->za_devctl &= ~TXENABLE;
				MCP_DMA_HALT(&softp->iface, zs->mcp_txdma);
				za->za_flags &= ~(ZAS_BUSY|ZAS_PAUSED);
			}

			za->za_flags &= ~ZAS_STOPPED;
			if ((q = za->za_ttycommon.t_readq) != NULL)
				m_hangup = 1;
		}
		za->za_flags &= ~ZAS_CARR_ON;
		mutex_exit(&zs->zs_excl_hi);
	}

	mutex_exit(&zs->zs_excl);

	if (m_break)
		putctl(q->q_next, M_BREAK);

	if (m_unhangup)
		putctl(q->q_next, M_UNHANGUP);

	if (m_hangup)
		putctl(q->q_next, M_HANGUP);

	return (0);
}

/*ARGSUSED*/
static int
mcpzsa_rxint(mcpcom_t *zs)
{
	return (0);
}

static int
mcpzsa_srint(mcpcom_t *zs)
{
	mcpaline_t		*za = (mcpaline_t *)zs->zs_priv;
	short			s1;
	u_char			c;

	mutex_enter(&zs->zs_excl);
	mutex_enter(&zs->zs_excl_hi);

	MCP_SCC_READ(1, s1);
	c = MCP_SCC_READDATA();
	MCP_ZSDELAY();

	if (s1 & ZSRR1_PE) {
		/*
		 *	IGNPAR		PARMRK	RESULT
		 *	off		off   	0
		 *	off		on	3 byte sequence
		 *	on		either	ignored
		 */

		if (!(za->za_ttycommon.t_iflag & IGNPAR)) {
			/*
			 * The receive interrupt routine has already stuffed c
			 * into the ring.  Dig it out again, since the current
			 * mode settings don't allow it to appear in that
			 * position.
			 */

			if (RING_CNT(za) != 0)
				RING_UNPUT(za);
			else
				cmn_err(CE_WARN,
					"mcpzsa%d: parity error ignored\n",
					minor(za->za_dev));

			if (za->za_ttycommon.t_iflag & PARMRK) {
				/*
				 * Must stuff the 377 directly into the silo.
				 * If ISTRIP is set, rxchar() will detect the
				 * \377 and double it. This should always be
				 * safe, since we really should never have more
				 * than 16 or so characters in the input silo.
				 * But just in case...
				 */

				if (!RING_POK(za, 3))
					mcpzsa_drain(za);

				if (RING_POK(za, 3)) {
					RING_PUT(za, 0377);
					mutex_exit(&zs->zs_excl_hi);
					mutex_exit(&zs->zs_excl);

					mcpzsa_rxchar(zs, '\0');
					mcpzsa_rxchar(zs, c);

					mutex_enter(&zs->zs_excl);
					mutex_enter(&zs->zs_excl_hi);
				} else
					mcpzsa_ringov(za);
			} else {
				mutex_exit(&zs->zs_excl_hi);
				mutex_exit(&zs->zs_excl);
				mcpzsa_rxchar(zs, '\0');
				mutex_enter(&zs->zs_excl);
				mutex_enter(&zs->zs_excl_hi);
			}
		} else {
			if (RING_CNT(za) != 0)
				RING_UNPUT(za);
			else
				cmn_err(CE_WARN,
					"mcpzsa%d: parity error went up\n",
					minor(za->za_dev));
		}
	}

	if ((s1 & ZSRR1_DO) && (za->za_flags & ZAS_ISOPEN))
		cmn_err(CE_WARN, "mcpzsa%d: SCC silo overflow\n",
			minor(za->za_dev));

	++zs->zs_rerror;
	MCP_SCC_WRITE0(ZSWR0_RESET_ERRORS);

	mutex_exit(&zs->zs_excl_hi);
	mutex_exit(&zs->zs_excl);
	return (0);
}

static int
mcpzsa_txend(mcpcom_t *zs)
{
	mcpaline_t	*za = (mcpaline_t *)zs->zs_priv;
	queue_t		*q = za->za_ttycommon.t_readq;

	DEBUGF(1, (CE_CONT, "mcpzsa_txend%d: xmit end intr\n", zs->mc_unit));
	mutex_enter(&zs->zs_excl);

	if (za->za_flags & ZAS_BUSY) {
		/*
		 * If a transmission has finished, indicate that it is
		 * finished, and start that line up again.
		 */
		za->za_flags &= ~ZAS_BUSY;
		za->za_ocnt = 0;
		enterq(q);
		mcpzsa_start(za);
		leaveq(q);
	}

	mutex_exit(&zs->zs_excl);
	return (0);
}

/*ARGSUSED*/
static int
mcpzsa_rxend(mcpcom_t *zs)
{
	return (0);
}

static int
mcpzsa_rxchar(mcpcom_t *zs, unsigned char c)
{
	mcpaline_t	*za = (mcpaline_t *)zs->zs_priv;
	queue_t *q;

	mutex_enter(&zs->zs_excl);

	/*
	 * NULs arriving while a break sequence is in progress are part of
	 * that sequence.  Discard them.
	 */

	if (c == '\0' && (za->za_rr0 & ZSRR0_BREAK)) {
		mutex_exit(&zs->zs_excl);
		return (0);
	}

	/*
	 * Stash the incoming character. Maybe.
	 */
	mutex_enter(&zs->zs_excl_hi);

	/*
	 * XXX - Double echo the '\377' character here if need be.
	 * This positively should not be down here in the driver.
	 * Thank Ritchie for short-circuit evaluation.
	 */
	if ((za->za_ttycommon.t_iflag & PARMRK) &&
			!(za->za_ttycommon.t_iflag & (IGNPAR|ISTRIP)) &&
			(c == 0377)) {
		if (RING_POK(za, 2)) {
			RING_PUT(za, 0377);
			RING_PUT(za, c);
		} else
			mcpzsa_ringov(za);
	} else {
		if (RING_POK(za, 1)) {
			RING_PUT(za, c);
		} else
			mcpzsa_ringov(za);
	}

	/*
	 * XXX - The board has half a brain: it understands stop characters
	 * but doesn't grok literal-next (^V, usually). Thus, ^V^S sequences
	 * do not have the desired effect. The board stops dead, but the rest
	 * of the system continues on blithely because "that wasn't *really*
	 * a control-S." Handle the special case here.
	 */
	if (za->za_flags & ZAS_LNEXT) {
		if (c == za->za_ttycommon.t_stopc &&
				!(za->za_flags & ZAS_STOPPED)) {
			if (!(*za->za_devctl & TXENABLE))
				*za->za_devctl |= TXENABLE;
		}
		za->za_flags &= ~ZAS_LNEXT;
	} else if (c == za->za_lnext)
		za->za_flags |= ZAS_LNEXT;

	/*
	 * While we are here, if c might be the stop character, or the silo
	 * is at least half full, drain it. Otherwise, schedule a drain
	 * mcpzsaticks in the future...unless one is already scheduled.
	 */
	if (c == za->za_ttycommon.t_stopc || RING_FRAC(za))
		mcpzsa_drain(za);
	else if (za->za_polltid == 0)
		za->za_polltid = timeout(mcpzsa_drain_callout, (caddr_t)za,
			mcpzsaticks);

	mutex_exit(&zs->zs_excl_hi);
	mutex_exit(&zs->zs_excl);
	return (0);
}

static void
mcpzsa_drain_callout(mcpaline_t *za)
{
	mcpcom_t	*zs = (mcpcom_t *)za->za_common;
	queue_t		*q;
	int		temp_tid;

	mutex_enter(&zs->zs_excl);
	mutex_enter(&zs->zs_excl_hi);

	if (temp_tid = za->za_polltid) {
		za->za_polltid = 0;
		mcpzsa_drain(za);
	}

	mutex_exit(&zs->zs_excl_hi);
	mutex_exit(&zs->zs_excl);

	if (temp_tid)
		untimeout(temp_tid);
}

/*
 * static void
 * mcpzsa_drain - this routines sends received characters upstream to the
 *	streams modules and applications above this driver. If the dev
 *	corresponding to this stream has been closed, flush the characters.
 *
 *	Returns:	NONE.
 */

static void
mcpzsa_drain(mcpaline_t *za)
{
	mcpcom_t	*zs = (mcpcom_t *)za->za_common;
	mblk_t		*bp;
	int		cc;
	queue_t		*q;

	ASSERT(mutex_owned(&zs->zs_excl));
	ASSERT(mutex_owned(&zs->zs_excl_hi));


	/* Get q for read side. */
	if ((q = za->za_ttycommon.t_readq) == NULL) {
		RING_INIT(za);
		return;
	}

	if (za->za_flags & ZAS_DRAINING) {
		za->za_flags |= ZAS_ZSA_START;
		return;
	}

	za->za_flags |= ZAS_DRAINING;

	enterq(q);

tryagain:

	if ((cc = RING_CNT(za)) > 0) {
		if (cc > 16)
			cc = 16;

		if ((bp = allocb(cc, BPRI_MED)) != (mblk_t *)0) {
			if (canputnext(q)) {
				do {
					*(bp->b_wptr++) = RING_GET(za);
				} while (--cc > 0);

				mutex_exit(&zs->zs_excl_hi);
				mutex_exit(&zs->zs_excl);

				if (bp->b_wptr !=  bp->b_rptr) {
					putnext(q, bp);
				} else
					freemsg(bp);

				mutex_enter(&zs->zs_excl);
				mutex_enter(&zs->zs_excl_hi);
			} else {
				mutex_exit(&zs->zs_excl_hi);
				mutex_exit(&zs->zs_excl);
				ttycommon_qfull(&za->za_ttycommon, q);
				freemsg(bp);
				mutex_enter(&zs->zs_excl);
				mutex_enter(&zs->zs_excl_hi);
				RING_EAT(za, cc);
			}
		} else {
			RING_EAT(za, cc);
		}
	}
	if (za->za_flags & ZAS_ZSA_START) {
		za->za_flags &= ~ZAS_ZSA_START;
		goto tryagain;
	}

	za->za_flags &= ~ZAS_DRAINING;
	leaveq(q);

	if ((RING_CNT(za) != 0) && (za->za_polltid == 0))
		za->za_polltid = timeout(mcpzsa_drain_callout, (caddr_t)za,
			mcpzsaticks);
}

static void
mcpzsa_ringov(mcpaline_t *za)
{
	cmn_err(CE_WARN, "mcpzsa%d.%d: input ring overflow\n",
		MCP_INSTANCE(za->za_dev), MCP_ZS_UNIT(za->za_dev));
}

static void
mcpzsa_mblk_debug(mblk_t *mp)
{
	mblk_t *bp = mp;

	mp = bp;
}
