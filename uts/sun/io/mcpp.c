/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#ident "@(#)mcpp.c  1.19     95/08/02 SMI"

/*
 * Sun MCP Parallel Port Driver
 *
 *	Handles the parallel function for the ALM-2 card.
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
#define	MCPP_DEBUG 0
#endif

#ifdef DEBUG
static int mcpp_debug = MCPP_DEBUG;
#define	DEBUGF(level, args) \
	if (mcpp_debug >= (level)) cmn_err args;
#else
#define	DEBUGF(level, args)	/* Nothing */
#endif /* !MCP_DEBUG */

/*
 * External variables and functions.
 */
extern kcondvar_t lbolt_cv;

/*
 * Define some local variables.
 */

typedef struct mcpp_state {
	mcp_iface_t *iface;
	dev_info_t *dip;
} mcpp_soft_t;

static void *mcpp_soft_p;

/*
 * Local Function Prototypes.
 */

static int	mcpp_identify(dev_info_t *);
static int	mcpp_probe(dev_info_t *);
static int 	mcpp_attach(dev_info_t *, ddi_attach_cmd_t);
static int	mcpp_detach(dev_info_t *, ddi_detach_cmd_t);
static int	mcpp_getinfo(dev_info_t *, ddi_info_cmd_t, void *, void **);

static int	mcpp_txend(mcpcom_t *);
static void	mcpp_start(mcpaline_t *);
static void	mcpp_restart(mcpaline_t *);
static void	mcpp_copyin(queue_t *q, mblk_t *, caddr_t, uint_t);
static void	mcpp_copyout(queue_t *q, mblk_t *, caddr_t, uint_t);

static int	mcpp_open(queue_t *, dev_t *, int, int, cred_t *);
static int	mcpp_close(queue_t *, int, cred_t *);
static int	mcpp_wput(queue_t *, mblk_t *);

static void	mcpp_cntl(mcpaline_t *, int, unsigned char *);
static int	mcpp_pe(mcpcom_t *);
static int	mcpp_slct(mcpcom_t *);
static int	mcpp_ioctl(mcpaline_t *za, queue_t *q, mblk_t *mp);

/*
 * Define some protocol operations.
 */

/* Parallel Port MCP Ops vector */
static struct mcpops mcpp_ops = {
	0,		/* Xmit buffer empty. */
	0,		/* External Status */
	0,		/* Receive Char Avail */
	0,		/* Special Receive Cond. */
	mcpp_txend,	/* Transmit DMA Done. */
	0,		/* Receive DMA done. */
	0,		/* Fifo recv char avail. */
	mcpp_pe,	/* PE: printer out of paper */
	mcpp_slct,	/* SLCT: printer is on line. */
};

/*
 * Declare some streams structures.
 */

static struct module_info mcp_info = {
	0x4d44,		/* Module ID */
	"mcpp",		/* Module name */
	0,		/* Min packet size. */
	INFPSZ,		/* Max packet size. */
	2048,		/* Hi Water */
	128		/* Lo Water */
};

static struct qinit mcpp_rinit = {
	putq,		/* Put Proc. */
	NULL,		/* Service Proc. */
	mcpp_open,	/* Open */
	mcpp_close,	/* Close */
	NULL,		/* Admin */
	&mcp_info,	/* Module Info Struct. */
	NULL		/* Module Stat Struct. */
};

static struct qinit mcpp_winit = {
	mcpp_wput,	/* Put Proc. */
	NULL,		/* Service Proc. */
	NULL,		/* Open */
	NULL,		/* Close */
	NULL,		/* Admin */
	&mcp_info,	/* Module Info State. */
	NULL
};

/* MCP Stream tab for SCC's */
struct streamtab mcppstab = {
	&mcpp_rinit,
	&mcpp_winit,
	NULL,
	NULL,
};

/*
 * Define mcpp cb_ops and dev_ops.
 */

static struct cb_ops mcpp_cb_ops = {
	nodev,		/* Open */
	nodev,		/* Close. */
	nodev,		/* Strategy */
	nodev,		/* print */
	nodev,		/* dump */
	nodev,		/* read */
	nodev,		/* write */
	nodev,		/* ioctl */
	nodev,		/* devmap */
	nodev,		/* mmap */
	nodev,		/* segmap */
	nochpoll,	/* Poll. */
	ddi_prop_op,	/* Prop Op. */
	&mcppstab,	/* streamtab. */
	D_NEW | D_MP,
};

static struct dev_ops mcpp_dev_ops = {
	DEVO_REV,	/* Devo_rev */
	0,		/* Refcnt */
	mcpp_getinfo,	/* get_dev_info */
	mcpp_identify,	/* identify */
	mcpp_probe,	/* probe. */
	mcpp_attach,	/* attach */
	mcpp_detach,	/* detach */
	nodev,		/* reset */
	&mcpp_cb_ops,	/* Driver ops */
	(struct bus_ops *)0,	/* Bus ops. */
};

/*
 * Module linkage information for the kernel
 */

static struct modldrv modldrv = {
	&mod_driverops,				/* Type of Module = Driver */
	"MCP Parallel Port driver v1.19",	/* Driver Identifier string. */
	&mcpp_dev_ops,				/* Driver Ops. */
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

	DEBUGF(1, (CE_CONT, "_init: entering ... \n"));

	stat = ddi_soft_state_init(&mcpp_soft_p, sizeof (mcpp_soft_t), N_MCP);
	if (stat != DDI_SUCCESS)
		return (stat);

	stat = mod_install(&modlinkage);
	if (stat != 0) {
		ddi_soft_state_fini(&mcpp_soft_p);
	}
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
	int	stat;

	DEBUGF(1, (CE_CONT, "_fini: entering ... \n"));

	if ((stat = mod_remove(&modlinkage)) != DDI_SUCCESS)
		return (stat);

	ddi_soft_state_fini(&mcpp_soft_p);

	DEBUGF(1, (CE_CONT, "_fini: exiting ... \n"));
	return (DDI_SUCCESS);
}

/*
 * Auto configuration routines.
 */

static int
mcpp_identify(dev_info_t *dip)
{
	DEBUGF(1, (CE_CONT, "mcpp_indentify: ddi_get_name(%s)\n",
		ddi_get_name(dip)));

	if (strcmp(ddi_get_name(dip), "mcpp") == 0)
		return (DDI_IDENTIFIED);
	else
		return (DDI_NOT_IDENTIFIED);
}

/*ARGSUSED*/
static int
mcpp_probe(dev_info_t *dip)
{
	return (DDI_PROBE_SUCCESS);
}

static int
mcpp_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	mcp_state_t	*softp;
	mcpp_soft_t	*mcpp;
	mcpcom_t	*zs;
	mcpaline_t	*za;
	mcp_iface_t	*iface;
	mcp_dev_t	*devp;
	int		instance;
	char		name[80];

	if (cmd != DDI_ATTACH)
		return (DDI_FAILURE);

	DEBUGF(1, (CE_CONT, "mcpp_attach%d: entering ...\n",
		ddi_get_instance(dip)));

	/* Get pointer fo the interface from the parent. */
	if ((iface = (mcp_iface_t *)ddi_get_driver_private(dip)) == NULL) {
		cmn_err(CE_CONT, "mcpp%d: parent failed initialize interface\n",
			ddi_get_instance(dip));
		return (DDI_FAILURE);
	}

	instance = ddi_get_instance(dip);
	if (ddi_soft_state_zalloc(mcpp_soft_p, instance) != DDI_SUCCESS)
		return (DDI_FAILURE);

	mcpp = (mcpp_soft_t *)ddi_get_soft_state(mcpp_soft_p, instance);
	mcpp->iface = iface;
	mcpp->dip = dip;

	softp = (mcp_state_t *)iface->priv;
	devp = softp->devp;
	zs = &softp->mcpcom[N_MCP_ZSDEVS];
	za = &softp->mcpaline[N_MCP_ZSDEVS];

	sprintf(name, "%d", instance);
	if (ddi_create_minor_node(dip, name, S_IFCHR, instance,
			"ddi_parallel", NULL) != DDI_SUCCESS) {
		ddi_soft_state_free(mcpp_soft_p, instance);
		return (DDI_FAILURE);
	}

	/*
	 * Initialize locks.
	 */
	za->za_flags_cv = kmem_zalloc(sizeof (kcondvar_t), KM_SLEEP);

	sprintf(name, "mcpp-lock%d", instance);
	mutex_init(&zs->zs_excl, name, MUTEX_DRIVER,
		(void *)softp->iblk_cookie);

	sprintf(name, "mcpp-lock-hi%d", instance);
	mutex_init(&zs->zs_excl_hi, name, MUTEX_DRIVER,
		(void *)softp->iblk_cookie);

	sprintf(name, "mcpp-cv%d", instance);
	cv_init(za->za_flags_cv, name, CV_DRIVER, (void *)softp->iblk_cookie);

	/*
	 * Now initialize the rest of the device.
	 */
	mutex_enter(&zs->zs_excl);
	mutex_enter(&zs->zs_excl_hi);

	zs->zs_unit = N_MCP_ZSDEVS;
	zs->mc_unit = instance;
	zs->zs_flags = 0;
	zs->zs_rerror = 0;
	zs->zs_state = (caddr_t)softp;

	zs->mcp_addr = devp;
	zs->mcp_txdma = MCP_DMA_GETCHAN(iface, (caddr_t)softp, 0,
		TX_DIR, PR_DMA);
	zs->mcp_rxdma = MCP_DMA_GETCHAN(iface, (caddr_t)softp, 0,
		RX_DIR, PR_DMA);

	if (softp->mcpsoftCAR & 0x10000)
		zs->zs_flags = MCPRIGNSLCT;
	else
		zs->zs_flags = 0;

	za->za_dmabuf = zs->mcp_addr->printer_buf;
	za->za_common = (struct zscom *)zs;
	zs->zs_priv = (caddr_t)za;

	mutex_exit(&zs->zs_excl_hi);
	mutex_exit(&zs->zs_excl);

	DEBUGF(1, (CE_CONT, "mcpp_attach%d: exiting ... \n", instance));

	ddi_report_dev(dip);
	return (DDI_SUCCESS);
}

static int
mcpp_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	mcpp_soft_t	*mcpp;
	mcp_state_t	*softp;
	mcpcom_t	*zs;
	mcpaline_t	*za;
	char		name[80];

	if (cmd != DDI_DETACH)
		return (DDI_FAILURE);

	DEBUGF(1, (CE_CONT, "mcpp_detach%d: entering ... \n",
		ddi_get_instance(dip)));

	mcpp = (mcpp_soft_t *)ddi_get_soft_state(mcpp_soft_p,
		ddi_get_instance(dip));
	softp = (mcp_state_t *)mcpp->iface->priv;
	zs = &(softp->mcpcom[N_MCP_ZSDEVS]);
	za = (mcpaline_t *)zs->zs_priv;

	mutex_enter(&zs->zs_excl);
	mutex_enter(&zs->zs_excl_hi);

	/* Is the device still open ?? */
	if (za->za_flags & (ZAS_ISOPEN | ZAS_WOPEN)) {
		mutex_exit(&zs->zs_excl_hi);
		mutex_exit(&zs->zs_excl);
		return (DDI_FAILURE);
	}

	za->za_flags = ZAS_REFUSE;

	mutex_exit(&zs->zs_excl_hi);
	mutex_exit(&zs->zs_excl);

	cv_destroy(za->za_flags_cv);
	mutex_destroy(&zs->zs_excl);
	mutex_destroy(&zs->zs_excl_hi);

	if (za->za_flags_cv) {
		kmem_free((caddr_t)za->za_flags_cv, sizeof (kcondvar_t));
		za->za_flags_cv = NULL;
	}

	sprintf(name, "%d", ddi_get_instance(dip));
	ddi_remove_minor_node(dip, name);
	ddi_soft_state_free(mcpp_soft_p, ddi_get_instance(dip));

	za->za_flags = 0;
	zs->mcp_ops = NULL;

	DEBUGF(1, (CE_CONT, "mcpp_detach%d: exiting ... \n",
		ddi_get_instance(dip)));
	return (DDI_SUCCESS);
}

static int
mcpp_getinfo(dev_info_t *dip, ddi_info_cmd_t cmd, void *arg, void **result)
{
	int		error = DDI_FAILURE;
	int		instance;
	mcpp_soft_t	*mcpp;

#ifdef lint
	dip = dip;
#endif
	switch (cmd) {
	case DDI_INFO_DEVT2DEVINFO:
		instance = getminor((dev_t)arg);
		mcpp = ddi_get_soft_state(mcpp_soft_p, instance);
		if (mcpp) {
			*result = mcpp->dip;
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
 * Parallel Streams Routines.
 */

/*ARGSUSED2*/
static int
mcpp_open(queue_t *q, dev_t *dev, int flag, int sflag, cred_t *credp)
{
	mcpcom_t	*zs;
	mcpaline_t	*za;
	mcpp_soft_t	*mcpp;
	mcp_state_t	*softp;
	int		instance;
	u_char		status;

	instance = getminor(*dev);
	if ((mcpp = ddi_get_soft_state(mcpp_soft_p, instance)) == NULL)
		return (ENODEV);

	softp = (mcp_state_t *)mcpp->iface->priv;
	za = &softp->mcpaline[N_MCP_ZSDEVS];
	zs = &softp->mcpcom[N_MCP_ZSDEVS];
	DEBUGF(1, (CE_CONT, "mcpp_open: unit = 0x%x\n", zs->zs_unit));

	mutex_enter(&zs->zs_excl);

	if (za->za_flags & ZAS_REFUSE) {
		mutex_exit(&zs->zs_excl);
		return (ENODEV);
	}

	if (za->za_common != (struct zscom *)zs) {
		mutex_exit(&zs->zs_excl);
		return (ENODEV);
	}

	mutex_enter(&zs->zs_excl_hi);

	if (zs->mcp_ops == NULL)
		zs->mcp_ops = &mcpp_ops;

	if (zs->mcp_ops != &mcpp_ops) {
		mutex_exit(&zs->zs_excl_hi);
		mutex_exit(&zs->zs_excl);
		return (ENODEV);
	}

	zs->zs_priv = (caddr_t) za;
	za->za_dev = *dev;
	za->za_flags |= ZAS_WOPEN;

	if ((za->za_flags & ZAS_ISOPEN) == 0)
		za->za_ttycommon.t_cflag = I_CFLAGS;

	else if (za->za_ttycommon.t_flags & TS_XCLUDE) {
		mutex_exit(&zs->zs_excl_hi);
		mutex_exit(&zs->zs_excl);
		return (ENXIO);
	}

	/* offline? */
	mcpp_cntl(za, MCPIOGPR, (unsigned char *)&status);
	DEBUGF(1, (CE_CONT, "mcpp_open: status = 0x%x\n",
		(unsigned char)status));

	if ((zs->zs_flags & MCPRIGNSLCT) == 0 &&
			(status & (MCPRPE | MCPRSLCT)) != (MCPRPE | MCPRSLCT)) {
		mutex_exit(&zs->zs_excl_hi);
		mutex_exit(&zs->zs_excl);
		if (status & MCPRINTSLCT)
		    cmn_err(CE_CONT, "Printer on mcpp%x is offline.\n",
			zs->mc_unit);
		return (ENXIO);
	}

	za->za_ttycommon.t_readq = q;
	za->za_ttycommon.t_writeq = WR(q);
	za->za_flags &= ~ZAS_WOPEN;
	za->za_flags |= ZAS_ISOPEN | ZAS_CARR_ON;

	mutex_exit(&zs->zs_excl_hi);
	mutex_exit(&zs->zs_excl);

	if (status & MCPRINTSLCT)
	    cmn_err(CE_CONT, "Printer on mcpp%x is online.\n",
		zs->mc_unit);

	q->q_ptr = WR(q)->q_ptr = (caddr_t) za;
	qprocson(q);

	return (0);
}

/*ARGSUSED1*/
static int
mcpp_close(queue_t *q, int flag, cred_t *credp)
{
	mcpaline_t 	*za;
	mcpcom_t	*zs;
	mcp_state_t	*softp;
	int	temp_tid, temp_cid;

	if ((za = (mcpaline_t *)q->q_ptr) == NULL)
		return (ENODEV);

	zs = (mcpcom_t *)za->za_common;
	softp = (mcp_state_t *)zs->zs_state;

	DEBUGF(1, (CE_CONT, "mcpp_close: unit = 0x%x\n", zs->zs_unit));

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

	if (za->za_flags & ZAS_BUSY) {
		mutex_enter(&zs->zs_excl_hi);
		(void) MCP_DMA_HALT(&softp->iface, zs->mcp_txdma);
		mutex_exit(&zs->zs_excl_hi);
	}

	za->za_ocnt = 0;

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

	/*
	 * Cancel outstanding "bufcall" request.
	 */
	if (temp_cid)
		unbufcall(temp_cid);

	/*
	 * Cancel outstanding timeout
	 */
	if (temp_tid)
		untimeout(temp_tid);

	qprocsoff(q);
	return (0);
}

static int
mcpp_wput(queue_t *q, mblk_t *mp)
{
	mcpaline_t 	*za;
	mcpcom_t	*zs;
	mcp_state_t	*softp;


	ASSERT(q != NULL);
	ASSERT(mp != NULL);

	za = (mcpaline_t *)q->q_ptr;
	zs = (mcpcom_t *)za->za_common;
	softp = (mcp_state_t *)zs->zs_state;

	DEBUGF(1, (CE_CONT, "mcpp_wput%d: db_type = 0x%x\n",
		zs->mc_unit, mp->b_datap->db_type));

	switch (mp->b_datap->db_type) {
	case M_STOP:
		mutex_enter(&zs->zs_excl);
		za->za_flags |= ZAS_STOPPED;
		mutex_exit(&zs->zs_excl);
		freemsg(mp);
		break;

	case M_START:
		mutex_enter(&zs->zs_excl);
		if (za->za_flags & ZAS_STOPPED) {
			za->za_flags &= ~ ZAS_STOPPED;
			mcpp_start(za);
		}
		mutex_exit(&zs->zs_excl);

		freemsg(mp);
		break;

	case M_IOCTL:
	case M_IOCDATA:
		mcpp_ioctl(za, q, mp);
		break;

	case M_FLUSH:
		if (*mp->b_rptr & FLUSHW) {
			/* Abort any output in progress.  */
			mutex_enter(&zs->zs_excl);

			if (za->za_flags & ZAS_BUSY) {
				mutex_enter(&zs->zs_excl_hi);
				(void) MCP_DMA_HALT(&softp->iface,
					zs->mcp_txdma);
				mutex_exit(&zs->zs_excl_hi);
				za->za_ocnt = 0;
				za->za_flags &= ~ZAS_BUSY;
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
		mcpp_start(za);
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
		mcpp_start(za);
		mutex_exit(&zs->zs_excl);

		break;

	default:
		freemsg(mp);
		break;
	}

	return (0);
}

static int
mcpp_ioctl(mcpaline_t *za, queue_t *q, mblk_t *mp)
{
	struct iocblk	*iocp;
	mcpcom_t	*zs = (mcpcom_t *)za->za_common;
	int	error = 0;
	u_char flags;
	caddr_t uaddr;

	iocp = (struct iocblk *)mp->b_rptr;

	switch (mp->b_datap->db_type) {
	case M_IOCDATA : {
		struct copyresp *csp;

		DEBUGF(1, (CE_CONT, "mcpp_ioctl%d: incoming iocdata\n",
			zs->mc_unit));
		csp = (struct copyresp *)(void *)mp->b_rptr;

		if (csp->cp_rval != 0) {
			DEBUGF(1, (CE_CONT, "mcpp_ioctl: iocdata rval = %d\n",
				csp->cp_rval));
			error = EIO;
			break;
		}

		if (csp->cp_private != (mblk_t *)-1) {
			mutex_enter(&zs->zs_excl);
			mutex_enter(&zs->zs_excl_hi);

			DEBUGF(1, (CE_CONT, "mcpp_ioctl: handling MCPIOSPR\n"));
			mcpp_cntl(za, iocp->ioc_cmd,
				(unsigned char *)mp->b_cont->b_rptr);

			mutex_exit(&zs->zs_excl_hi);
			mutex_exit(&zs->zs_excl);
		}

		iocp->ioc_count = 0;
		iocp->ioc_error = 0;
		mp->b_datap->db_type = M_IOCACK;
		qreply(q, mp);
		return (0);
	}

	case M_IOCTL : {
		/*
		 * If we were holding an "ioctl" response pending the
		 * availability of an "mblk" to hold data to be passed up;
		 * another "ioctl" came through, which means that "ioctl"
		 * must have timed out or been aborted.
		 */

		if (za->za_ttycommon.t_iocpending != NULL) {
			freemsg(za->za_ttycommon.t_iocpending);
			za->za_ttycommon.t_iocpending = NULL;
		}

		DEBUGF(1, (CE_CONT, "mcpp_ioctl%d: incoming ioctl\n",
			zs->mc_unit));


		switch (iocp->ioc_cmd) {
		case MCPIOGPR: {
			DEBUGF(1, (CE_CONT, "mcpp_ioctl: handling MCPIOGPR\n"));

			uaddr = *(caddr_t *)(void *)mp->b_cont->b_rptr;
			freemsg(mp->b_cont);

			if ((mp->b_cont = allocb(1, BPRI_HI)) == NULL) {
				/*
				 * This is so rare and so exceptional,
				 * don't bother with the callback.
				 */
				error = EINVAL;
				break;
			}

			mutex_enter(&zs->zs_excl);
			mutex_enter(&zs->zs_excl_hi);
			mcpp_cntl(za, iocp->ioc_cmd, (u_char *)&flags);
			mutex_exit(&zs->zs_excl_hi);
			mutex_exit(&zs->zs_excl);
			mp->b_cont->b_rptr[0] = flags;
			mp->b_cont->b_wptr++;

			DEBUGF(1, (CE_CONT, "mcpp_ioctl: flags = 0x%2x\n",
				flags));

			mcpp_copyout(q, mp, uaddr, sizeof (u_char));

			return (0);
		}

		case MCPIOSPR: {
			uaddr = *(caddr_t *)(void *)mp->b_cont->b_rptr;

			mcpp_copyin(q, mp, uaddr, sizeof (u_char));
			return (0);

		}

		default:
			DEBUGF(1, (CE_CONT, "mcpp_ioctl: wierd request= %d\n",
				iocp->ioc_cmd));
			/* We don't understand it either.  */
			error = EINVAL;
			break;
		}
	    }
	}

	if (error) {
		DEBUGF(1, (CE_CONT, "mcpp_ioctl: error = %d \n", error));
		iocp->ioc_error = error;
		mp->b_datap->db_type = M_IOCNAK;
	}

	DEBUGF(1, (CE_CONT, "mcpp_ioctl: error = %d \n", error));
	qreply(q, mp);

	return (0);
}

static void
mcpp_copyin(queue_t *q, mblk_t *mp, caddr_t addr, uint_t len)
{
	struct copyreq *cqp;

	cqp = (struct copyreq *)(void *)mp->b_rptr;
	mp->b_wptr = mp->b_wptr + sizeof (struct copyreq);
	cqp->cq_addr = addr;
	cqp->cq_size = len;
	cqp->cq_private = (mblk_t *)(void *)addr;
	cqp->cq_flag = 0;

	if (mp->b_cont != NULL) {
		freemsg(mp->b_cont);
		mp->b_cont = NULL;
	}
	mp->b_datap->db_type = M_COPYIN;
	qreply(q, mp);
}


static void
mcpp_copyout(queue_t *q, mblk_t *mp, caddr_t addr, uint_t len)
{
	struct copyreq *cqp;

	cqp = (struct copyreq *)(void *)mp->b_rptr;
	mp->b_wptr = mp->b_rptr + sizeof (struct copyreq);
	cqp->cq_addr = addr;
	cqp->cq_size = len;
	cqp->cq_private = (mblk_t *)-1;
	cqp->cq_flag = 0;
	mp->b_datap->db_type = M_COPYOUT;
	qreply(q, mp);
}

static void
mcpp_start(mcpaline_t *za)
{
	mcpcom_t	*zs;
	mcp_state_t	*softp;
	int		cc, avail_bytes;
	u_char		*cp;
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

	if (za->za_flags & ZAS_BREAK)
		return;

	/*
	 * If we're waiting for a delay timeout to expire or for the current
	 * transmission to complete, don't grab anything new.
	 */

	if (za->za_flags & (ZAS_BUSY | ZAS_DELAY))
		return;

	/*
	 * Hopefully we're attached to a stream.
	 */
	if ((q = za->za_ttycommon.t_writeq) == NULL)
		return;

	/*
	 * Set up to copy up to a bufferful of bytes into the MCP buffer.
	 */
	max_buf_size = PR_BSZ;
	cp = za->za_dmabuf;
	avail_bytes = max_buf_size;

	while ((bp = getq(q)) != NULL) {
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

			(void) timeout(mcpp_restart, (caddr_t)za, hz / 4);
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

			(void) timeout(mcpp_restart, (caddr_t)za,
				(int)(*(unsigned char *)bp->b_rptr + 6));
			za->za_flags |= ZAS_DELAY;
			freemsg(bp);
			return;

		case M_IOCTL:
			if (avail_bytes != max_buf_size) {
				(void) putbq(q, bp);
				goto transmit;
			}

			/*
			 * This ioctl was waiting for the output ahead of it
			 * to drain; obviously, it has. Do it, and then grab
			 * the next message after it.
			 */

			mutex_exit(&zs->zs_excl);
			mcpp_ioctl(za, q, bp);
			mutex_enter(&zs->zs_excl);
			continue;
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

		MCP_DMA_START(&softp->iface, zs->mcp_txdma,
			(char *)za->za_dmabuf, za->za_ocnt);
		za->za_flags |= ZAS_BUSY;
		zs->zs_flags |= MCP_WAIT_DMA;

		mutex_exit(&zs->zs_excl_hi);
	}
}

static void
mcpp_restart(mcpaline_t *za)
{
	mcpcom_t 	*zs = (mcpcom_t *)za->za_common;
	queue_t		*q;

	DEBUGF(1, (CE_CONT, "mcpp_start%d: restarting mcpp_start\n",
		zs->mc_unit));

	mutex_enter(&zs->zs_excl);

	/*
	 * If break timer expired, turn off break bit.
	 */
	za->za_flags &= ~(ZAS_DELAY | ZAS_BREAK);
	if ((q = za->za_ttycommon.t_writeq) != NULL)
		enterq(q);

	mcpp_start(za);
	if (q != NULL)
		leaveq(q);

	mutex_exit(&zs->zs_excl);
}

/*
 * Parallel Port Interrrupt Routines.
 */

static int
mcpp_txend(mcpcom_t *zs)
{
	mcpaline_t	*za = (mcpaline_t *)zs->zs_priv;

	DEBUGF(1, (CE_CONT, "mcpp_txend%d: xmit end intr\n", zs->zs_unit));
	mutex_enter(&zs->zs_excl);

	if (za->za_flags & ZAS_BUSY) {
		/*
		 * If a transmission has finished, indicate that it is
		 * finished, and start that line up again.
		 */
		za->za_flags &= ~ZAS_BUSY;
		za->za_ocnt = 0;
		mcpp_start(za);
	}

	mutex_exit(&zs->zs_excl);
	return (0);
}

static int
mcpp_pe(mcpcom_t *zs)
{
	struct _ciochip_ 	*ciop = &zs->mcp_addr->cio;
	u_char			uc;

	if (zs->zs_flags & MCPRINTPE && !(ciop->portb_data & MCPRPE)) {
		cmn_err(CE_WARN, "Printer on mcpp%x is out of paper.\n",
			zs->mc_unit);
		CIO_READ(ciop, CIO_PB_PP, uc);
		uc |= MCPRPE;
		CIO_WRITE(ciop, CIO_PB_PP, uc);
	} else {
		cmn_err(CE_WARN, "Printer on mcpp%x: paper ok.\n",
			zs->mc_unit);
		CIO_READ(ciop, CIO_PB_PP, uc);
		uc &= ~MCPRPE;
		CIO_WRITE(ciop, CIO_PB_PP, uc);
	}

	return (0);
}

static int
mcpp_slct(mcpcom_t *zs)
{
	struct _ciochip_ *ciop = &zs->mcp_addr->cio;
	u_char uc;

	if ((zs->zs_flags & MCPRINTSLCT) && !(ciop->portb_data & MCPRSLCT)) {
		cmn_err(CE_CONT, "Printer on mcpp%x is offline.\n",
			zs->mc_unit);
		CIO_READ(ciop, CIO_PB_PP, uc);
		uc |= MCPRSLCT;
		CIO_WRITE(ciop, CIO_PB_PP, uc);
	} else {
		cmn_err(CE_CONT, "Printer on mcpp%x is online.\n",
			zs->mc_unit);
		CIO_READ(ciop, CIO_PB_PP, uc);
		uc &= ~MCPRSLCT;
		CIO_WRITE(ciop, CIO_PB_PP, uc);
	}

	return (0);
}

/*
 * Parallel Port Maniputlation routines.
 */

static void
mcpp_cntl(mcpaline_t *za, int cmd, unsigned char *datap)
{
	mcpcom_t		*zs = (struct mcpcom *) za->za_common;
	struct _ciochip_	*cp = &zs->mcp_addr->cio;

	ASSERT(mutex_owned(&zs->zs_excl));
	ASSERT(mutex_owned(&zs->zs_excl_hi));

	/*
	 * the sense of the MCPRDIAG flag is reversed on the CIO,
	 * it is cleared when the printer is in diag mode.
	 */

	switch (cmd) {
	case MCPIOSPR:
		DEBUGF(1, (CE_CONT,
			"mcpp_cntl: datap, zs->zs_flags = 0x%2x, 0x%2x\n",
			*datap, zs->zs_flags));

		if ((*datap ^ zs->zs_flags) & (MCPRINTSLCT | MCPRINTPE)) {
			zs->zs_flags =
				(zs->zs_flags & ~(MCPRINTSLCT | MCPRINTPE)) |
				(*datap & (MCPRINTSLCT | MCPRINTPE));

		}

		if ((*datap ^ zs->zs_flags) & MCPRDIAG) {
			zs->zs_flags = (zs->zs_flags & ~MCPRDIAG) |
				(*datap & MCPRDIAG);

			cp->portc_data = (cp->portc_data & ~MCPRDIAG & 0xf) |
				(*datap & MCPRDIAG);
		}
		/*FALLTHROUGH*/

	case MCPIOGPR:
		*datap = zs->zs_flags & (MCPRINTSLCT | MCPRINTPE | MCPRIGNSLCT);
		*datap |= cp->portb_data & (MCPRPE | MCPRSLCT);
		*datap |= cp->portc_data & (MCPRVMEINT | MCPRDIAG);
		DEBUGF(1, (CE_CONT, "mcpp_cntl: datap = 0x%2x\n", *datap));
		break;
	}
}
