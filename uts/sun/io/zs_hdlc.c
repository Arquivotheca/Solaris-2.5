/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)zs_hdlc.c 1.37	95/02/09 SMI"

/*
 *	HDLC protocol handler for Z8530 SCC.
 */

#include	<sys/param.h>
#include	<sys/systm.h>
#include	<sys/types.h>
#include	<sys/sysmacros.h>
#include	<sys/kmem.h>
#include	<sys/stropts.h>
#include	<sys/stream.h>
#include	<sys/stat.h>
#include	<sys/cred.h>
#include	<sys/user.h>
#include	<sys/proc.h>
#include	<sys/file.h>
#include	<sys/uio.h>
#include	<sys/buf.h>
#include	<sys/mkdev.h>
#include	<sys/cmn_err.h>
#include	<sys/errno.h>
#include	<sys/ksynch.h>
#include	<sys/fcntl.h>

#include	<sys/zsdev.h>
#include	<sys/ser_sync.h>
#include	<sys/debug/debug.h>
#include	<sys/conf.h>
#include	<sys/ddi.h>
#include	<sys/sunddi.h>
#include	<sys/strsun.h>
#include	<sys/dlpi.h>

#define	ZSH_TRACING
#ifdef	ZSH_TRACING
#include	<sys/vtrace.h>

/*
 * Temp tracepoint definitions
 */
#define	TR_ZSH		50

#define	TR_ZSH_TXINT	1
#define	TR_ZSH_XSINT	2
#define	TR_ZSH_RXINT	3
#define	TR_ZSH_SRINT	4

#define	TR_ZSH_WPUT_START		5
#define	TR_ZSH_WPUT_END			6
#define	TR_ZSH_START_START		7
#define	TR_ZSH_START_END		8
#define	TR_ZSH_SOFT_START		9
#define	TR_ZSH_SOFT_END			10

#define	TR_ZSH_OPEN	 11
#define	TR_ZSH_CLOSE	12

#endif	ZSH_TRACING

/*
 * Logging definitions
 */

/*
#define	ZSH_DEBUG
*/
#ifdef ZSH_DEBUG

#ifdef ZS_DEBUG_ALL
extern	char	zs_h_log[];
extern	int	zs_h_log_n;
#define	zsh_h_log_add(c) \
	{ \
		if (zs_h_log_n >= ZS_H_LOG_MAX) \
			zs_h_log_n = 0; \
		zs_h_log[zs_h_log_n++] = 'A' + zs->zs_unit; \
		zs_h_log[zs_h_log_n++] = c; \
		zs_h_log[zs_h_log_n] = '\0'; \
	}
#define	zsh_h_log_clear
#else
#define	ZSH_H_LOG_MAX   0x8000
char zsh_h_log[2][ZSH_H_LOG_MAX +10];
int zsh_h_log_n[2];
#define	zsh_h_log_add(c) \
	{ \
		if (zsh_h_log_n[zs->zs_unit] >= ZSH_H_LOG_MAX) \
			zsh_h_log_n[zs->zs_unit] = 0; \
		zsh_h_log[zs->zs_unit][zsh_h_log_n[zs->zs_unit]++] = c; \
		zsh_h_log[zs->zs_unit][zsh_h_log_n[zs->zs_unit]] = '\0'; \
	}

#define	zsh_h_log_clear \
	{ register char *  p; \
	for (p = &zsh_h_log[zs->zs_unit][ZSH_H_LOG_MAX]; \
		p >= &zsh_h_log[zs->zs_unit][0]; p--) \
		*p = '\0'; \
	zsh_h_log_n[zs->zs_unit] = 0; \
	}
#endif

#define	ZSH_R0_LOG(r0)  { \
	if (r0 & ZSRR0_RX_READY) zsh_h_log_add('R'); \
	if (r0 & ZSRR0_TIMER) zsh_h_log_add('Z'); \
	if (r0 & ZSRR0_TX_READY) zsh_h_log_add('T'); \
	if (r0 & ZSRR0_CD) zsh_h_log_add('D'); \
	if (r0 & ZSRR0_SYNC) zsh_h_log_add('S'); \
	if (r0 & ZSRR0_CTS) zsh_h_log_add('C'); \
	if (r0 & ZSRR0_TXUNDER) zsh_h_log_add('U'); \
	if (r0 & ZSRR0_BREAK) zsh_h_log_add('B'); \
	}
#endif


char _depends_on[] = "drv/zs";

int maxzsh = 2;		/* see me about dragons... */
#define	MAXZSHCLONES	(80)	/* three clone opens per instance */
int zsh_timer_count = 10;
int zsh_default_mru = 1024;
extern timestruc_t hrestime;
extern int nzs;
extern struct zscom *zscom;
extern struct zs_prog *zs_prog;

struct ser_str *zsh_str = NULL;
unsigned char zsh_usedminor[MAXZSHCLONES];
extern kmutex_t zs_curr_lock;	/* lock protecting zscurr use for clone */


/*
 * The HDLC protocol
 */
extern void setzssoft();
extern void zsopinit(struct zscom *zs, struct zsops *zso);

int zsh_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result);
static int  zsh_identify(dev_info_t *dev);
static int  zsh_probe(dev_info_t *dev);
static int  zsh_attach(dev_info_t *dev, ddi_attach_cmd_t cmd);
static int  zsh_detach(dev_info_t *dev, ddi_detach_cmd_t cmd);
static int  zsh_open(queue_t *rq, dev_t *dev, int flag, int sflag, cred_t *cr);
static int  zsh_close(queue_t *rq, int flag);
static int  zsh_wput(queue_t *wq, mblk_t *mp);
static int zsh_start(struct zscom *zs, struct syncline *zss);
static void zsh_ioctl(queue_t *wq, mblk_t *mp);

static struct module_info hdlc_minfo = {
	0x5a48,		/* module ID number: "ZH" */
	"zsh",		/* module name */
	0,		/* minimum packet size accepted */
	INFPSZ,		/* maximum packet size accepted */
	12*1024,	/* queue high water mark (bytes) */
	4*1024		/* queue low water mark (bytes) */
};

static struct qinit hdlc_rinit = {
	putq,		/* input put procedure */
	NULL,		/* input service procedure */
	zsh_open,	/* open procedure */
	zsh_close,	/* close procedure */
	NULL,		/* reserved */
	&hdlc_minfo,	/* module info */
	NULL		/* reserved */
};

static struct qinit hdlc_winit = {
	zsh_wput,	/* output put procedure */
	NULL,		/* output service procedure */
	NULL,		/* open procedure */
	NULL,		/* close procedure */
	NULL,		/* reserved */
	&hdlc_minfo,	/* module info */
	NULL		/* reserved */
};

struct streamtab hdlctab = {
	&hdlc_rinit,	/* initialize read queue */
	&hdlc_winit,	/* initialize write queue */
	NULL,		/* mux read qinit */
	NULL		/* mux write qinit */
};

#define	ZSH_CONF_FLAG   (D_NEW | D_MP)

DDI_DEFINE_STREAM_OPS(zsh_ops, \
			zsh_identify, \
			zsh_probe, \
			zsh_attach, \
			zsh_detach, \
			nodev, \
			zsh_info, \
			ZSH_CONF_FLAG, \
			&hdlctab);


/*
 * This is the loadable module wrapper.
 */

#include	<sys/errno.h>
#include	<sys/modctl.h>

extern dseekneg_dold_flag;
extern struct mod_ops mod_driverops;


/*
 * Module linkage information for the kernel.
 */

static struct modldrv modldrv = {
	&mod_driverops, /* Type of module.  This one is a driver */
	"Z8530 serial HDLC drv V1.37",
	&zsh_ops,	/* our own ops for this module */
};

static struct modlinkage modlinkage = {
	MODREV_1,
	(void *)&modldrv,
	NULL
};

int
_init(void)
{
	return (mod_install(&modlinkage));
}

int
_fini(void)
{
	return (mod_remove(&modlinkage));
}

int
_info(struct modinfo *modinfop)
{
#ifdef ZSH_DEBUG
	printf("\nzsh: %s %s\n", __DATE__, __TIME__);
#endif
	return (mod_info(&modlinkage, modinfop));
}


/*
 * The HDLC interrupt entry points.
 */
static void	zsh_txint(struct zscom *zs);
static void	zsh_xsint(struct zscom *zs);
static void	zsh_rxint(struct zscom *zs);
static void	zsh_srint(struct zscom *zs);
static int	zsh_softint(struct zscom *zs);

extern struct zsops zsops_null;
struct zsops zsops_hdlc = {
	zsh_txint,
	zsh_xsint,
	zsh_rxint,
	zsh_srint,
	zsh_softint,
	NULL,
	NULL
};

static int	zsh_program(struct zscom *zs, struct scc_mode *sm);
static void	zsh_setmstat(struct zscom *zs, int event);
static void	zsh_rxbad(struct zscom *zs, struct syncline *zss);
static void	zsh_txbad(struct zscom *zs, struct syncline *zss);
static void	zsh_watchdog(struct zscom *zs);
static void	zsh_callback(struct zscom *zs);
static int	zsh_hdp_ok_or_rts_state(struct zscom *zs, struct syncline *zss);
static void	zsh_init_port(struct zscom *zs, struct syncline *zss);
static void	zsh_ack(mblk_t *mp, mblk_t *dp, unsigned int size);
static int	zsh_setmode(struct zscom *zs, struct syncline *zss,
			struct scc_mode *sm);
static void	zsh_copy(mblk_t *mp, mblk_t *dp, unsigned int size,
			unsigned char type);


/*
 * The HDLC Driver.
 */


/*
 * Special macros to handle STREAMS operations.
 * These are required to address memory leakage problems.
 * WARNING : the macro do NOT call ZSSETSOFT
 */

/*
 * Should be called holding only the adaptive (zs_excl) mutex.
 */
#define	ZSH_GETBLOCK(zs, allocbcount) \
{ \
	register int n = ZSH_MAX_RSTANDBY; \
	while (--n >= 0) { \
	    if (!zss->sl_rstandby[n]) { \
		if ((zss->sl_rstandby[n] = \
		    allocb(zss->sl_mru, BPRI_MED)) == NULL) { \
		    if (zss->sl_bufcid == 0) { \
			mutex_enter(zs->zs_excl_hi); \
			if (zss->sl_txstate != TX_OFF) { \
			    mutex_exit(zs->zs_excl_hi); \
			    zss->sl_bufcid = bufcall(zss->sl_mru, \
				    BPRI_MED, zsh_callback, (long)zs); \
			    break; \
			} else \
				mutex_exit(zs->zs_excl_hi); \
		    } \
		} \
		allocbcount--; \
	    } \
	} \
}

/*
 * Should be called holding the spin (zs_excl_hi) mutex.
 */
#define	ZSH_ALLOCB(mp) \
{ \
	register int n = ZSH_MAX_RSTANDBY; \
	mp = NULL; \
	while (--n >= 0)  { \
		if (mp = zss->sl_rstandby[n]) { \
			zss->sl_rstandby[n] = NULL; \
			break; \
		} \
	} \
}

#define	ZSH_PUTQ(mp) \
{ \
	register int wptr, rptr;  \
	wptr = zss->sl_rdone_wptr; \
	rptr = zss->sl_rdone_rptr; \
	zss->sl_rdone[wptr] = mp; \
	if ((wptr) + 1 == ZSH_RDONE_MAX) \
		zss->sl_rdone_wptr = wptr = 0; \
	else \
		zss->sl_rdone_wptr = ++wptr; \
	if (wptr == rptr) {  /* Should never occur */ \
		SCC_BIC(1, ZSWR1_INIT); \
		zss->sl_m_error = ENOSR; \
		ZSSETSOFT(zs); \
	} \
}

#define	ZSH_FREEMSG(mp) \
{ \
	ZSH_PUTQ(mp); \
}


/*
 * Should be called holding only the adaptive (zs_excl) mutex.
 */
#define	ZSH_GETQ(mp) \
{ \
	if (zss->sl_rdone_rptr != zss->sl_rdone_wptr) { \
		mp = zss->sl_rdone[zss->sl_rdone_rptr++]; \
		if (zss->sl_rdone_rptr == ZSH_RDONE_MAX) \
				zss->sl_rdone_rptr = 0; \
	} else \
		mp = NULL; \
}

#define	ZSH_FLUSHQ \
{ \
	register mblk_t *tmp; \
	while (1) { \
		ZSH_GETQ(tmp); \
		if (!(tmp)) \
			break; \
		freemsg(tmp); \
	} \
}


static int
zsh_identify(dev_info_t *dev)
{
	if (strcmp(ddi_get_name(dev), "zsh") == 0) {
		return (DDI_IDENTIFIED);
	} else {
		return (DDI_NOT_IDENTIFIED);
	}
}

static int
zsh_probe(dev_info_t *dev)
{
	return (DDI_PROBE_DONTCARE);
}

/*ARGSUSED*/
static int
zsh_attach(dev_info_t *dev, ddi_attach_cmd_t cmd)
{
	register int	unit;
	char		name[2] = { '\0', '\0'	};

	/*
	 * Since zsh is a child of the "pseudo" nexus, we can expect the
	 * attach routine to be called only once.  We need to create all
	 * necessary devices in one shot.  There is never more than one
	 * SCC chip that supports zsh devices.
	 */

	if (cmd != DDI_ATTACH)
		return (DDI_FAILURE);
	if (zscom == NULL)
		return (DDI_FAILURE);	/* zsattach not done */
	unit = 2 * ddi_get_instance(dev);
	if (unit > 1)
		return (DDI_FAILURE);	/* only use cpu ports */

	if (sizeof (struct syncline) > sizeof (zscom->zs_priv_str)) {
	    cmn_err(CE_WARN,
		"zsh attach failed. zsh and zs drivers must be recompiled");
	    return (DDI_FAILURE);
	}

	if (ddi_create_minor_node(dev, "zsh", S_IFCHR,
	    NULL, DDI_PSEUDO, CLONE_DEV) == DDI_FAILURE) {
		ddi_remove_minor_node(dev, NULL);
		cmn_err(CE_WARN, "zsh clone device creation failed.");
		return (DDI_FAILURE);
	}

	name[0] = (char)(unit + '0');
	if (ddi_create_minor_node(dev, name, S_IFCHR,
	    unit, DDI_PSEUDO, NULL) == DDI_FAILURE) {
		ddi_remove_minor_node(dev, NULL);
		return (DDI_FAILURE);
	}

	unit++;
	name[0] = (char)(unit + '0');
	if (ddi_create_minor_node(dev, name, S_IFCHR,
	    unit, DDI_PSEUDO, NULL) == DDI_FAILURE) {
		ddi_remove_minor_node(dev, NULL);
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

/* ARGSUSED */
int
zsh_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg,
void **result)
{
	register dev_t dev = (dev_t) arg;
	register int instance, error;
	register struct zscom *zs;

	if ((instance = UNIT(dev)) >= nzs)
		return (DDI_FAILURE);

	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		if (zscom == NULL) {
			error = DDI_FAILURE;
		} else {
			zs = &zscom[instance];
			*result = (void *) zs->zs_dip;
			error = DDI_SUCCESS;
		}
		break;
	case DDI_INFO_DEVT2INSTANCE:
		*result = (void *)instance;
		error = DDI_SUCCESS;
		break;
	default:
		error = DDI_FAILURE;
	}
	return (error);
}

/*ARGSUSED*/
static int
zsh_detach(dev_info_t *dev, ddi_detach_cmd_t cmd)
{
	register struct zscom		*zs0, *zs1;
	register struct syncline	*zss0, *zss1;
	register int			instance;

	if (cmd != DDI_DETACH)
		return (DDI_FAILURE);

	ddi_remove_minor_node(dev, NULL);

	return (DDI_SUCCESS);
}

static void
zsh_init_port(struct zscom *zs, struct syncline *zss)
{
	register u_char s0;

	SCC_WRITE(3, (ZSWR3_RX_ENABLE | ZSWR3_RXCRC_ENABLE | ZSWR3_RX_8));
	SCC_WRITE(5, (ZSWR5_TX_8 | ZSWR5_DTR | ZSWR5_TXCRC_ENABLE));
	zss->sl_rr0 = SCC_READ0();
	if (zss->sl_flags & SF_FDXPTP) {
		SCC_BIS(5, ZSWR5_TX_ENABLE);
		SCC_BIS(5, ZSWR5_RTS);
		s0 = SCC_READ0();
		if ((s0 & ZSRR0_CTS) ||
		    !(zss->sl_mode.sm_config & (CONN_SIGNAL | CONN_IBM))) {
			/*
			 * send msg that CTS is up
			 */
			zss->sl_rr0 |= ZSRR0_CTS;
			zss->sl_txstate = TX_IDLE;
		} else {
			zss->sl_flags |= SF_XMT_INPROG;
			zss->sl_txstate = TX_RTS;
			zss->sl_rr0 &= ~ZSRR0_CTS;
			zss->sl_wd_count = zsh_timer_count;
			if (!zss->sl_wd_id)
				zss->sl_wd_id = timeout(zsh_watchdog,
				    (caddr_t)zs, SIO_WATCHDOG_TICK);
		}
	} else {
		SCC_BIC(15, ZSR15_CTS);
		SCC_BIC(5, ZSWR5_TX_ENABLE);
		SCC_BIC(5, ZSWR5_RTS);
		zss->sl_flags &= ~SF_FLUSH_WQ;
	}
}

/*
 * Open routine.
 */

/*ARGSUSED*/
static int
zsh_open(queue_t *rq, dev_t *dev, int flag, int sflag, cred_t *cr)
{
	register struct zscom *zs;
	register struct syncline *zss;
	register struct ser_str *stp;
	register mblk_t *mp;
	register int	unit;
	register int	i;
	register int 	tmp;
	ulong_t		timestamp[3];
	int		len;

	if (sflag != CLONEOPEN) {
		if (rq->q_ptr)
			return (EBUSY);  /* We got a stream that is in use */

		unit = UNIT(*dev);
		if (unit >= maxzsh)
			return (ENXIO);  /* unit not configured */

		if (zscom == NULL)
			return (ENXIO);  /* device not found by autoconfig */
		zs = &zscom[unit];

		TRACE_1(TR_ZSH, TR_ZSH_OPEN, "zsh_open:unit = %d", unit);

		mutex_enter(zs->zs_excl);
		if ((zs->zs_ops != &zsops_null) &&
		    (zs->zs_ops != &zsops_hdlc)) {
			mutex_exit(zs->zs_excl);
			return (EBUSY);	 /* another protocol got here first */
		}

		/* Mark device as busy (for power management) */
		len = sizeof (timestamp);
		if (ddi_prop_op(DDI_DEV_T_ANY, zs->zs_dip,
		    PROP_LEN_AND_VAL_BUF, 0, "pm_timestamp",
		    (caddr_t)timestamp, &len) != DDI_PROP_SUCCESS)
			cmn_err(CE_WARN, "zsh_open: Can't read pm property");
		timestamp[unit%2+1] = 0;
		if (ddi_prop_modify(DDI_DEV_T_NONE, zs->zs_dip,
		    DDI_PROP_CANSLEEP, "pm_timestamp", (caddr_t)timestamp,
		    sizeof (timestamp)) != DDI_PROP_SUCCESS)
			cmn_err(CE_WARN, "zsh_open: Can't modify pm property");
		ddi_dev_is_needed(zs->zs_dip, unit%2+1, 1);

		zsopinit(zs, &zsops_hdlc);

		zss = (struct syncline *)&zscom[unit].zs_priv_str;
		stp = &zss->sl_stream;
		stp->str_state = NULL;
		stp->str_com = (caddr_t)zs;

		zss->sl_xhead = NULL;
		zss->sl_xactb = NULL;
		zs->zs_wr_cur = NULL;
		zs->zs_wr_lim = NULL;
		zs->zs_wr_cur = NULL;
		zs->zs_wr_lim = NULL;
		zss->sl_rhead = NULL;
		zss->sl_ractb = NULL;
		zs->zs_rd_cur = NULL;
		zs->zs_rd_lim = NULL;
		zss->sl_mstat = NULL;
		zss->sl_xstandby = NULL;
		zss->sl_wd_id = 0;
		zss->sl_soft_active = 0;
		zss->sl_stream.str_rq = NULL;

		zs->zs_priv = (caddr_t)zss;

		zss->sl_mru = zsh_default_mru;
		tmp = ZSH_MAX_RSTANDBY;
		ZSH_GETBLOCK(zs, tmp);
		if (zss->sl_rstandby[0] == NULL) {
		    cmn_err(CE_WARN, "zsh_open: can't alloc message block");
		    mutex_exit(zs->zs_excl);
		    return (ENOSR);
		}
		mutex_enter(zs->zs_excl_hi);
		ZSH_ALLOCB(zss->sl_ractb);
		zss->sl_txstate = TX_OFF;
		zss->sl_rr0 = SCC_READ0();
		zss->sl_flags &= (SF_INITIALIZED | SF_FDXPTP);
		if (zss->sl_flags & SF_INITIALIZED)
			zsh_init_port(zs, zss);
		mutex_exit(zs->zs_excl_hi);
		mutex_exit(zs->zs_excl);
	} else {   /* CLONEOPEN */
		mutex_enter(&zs_curr_lock);
		for (unit = maxzsh; unit < MAXZSHCLONES; unit++)
			if (!zsh_usedminor[unit]) {
				zsh_usedminor[unit] = unit;
				break;
			}
		mutex_exit(&zs_curr_lock);
		if (unit >= MAXZSHCLONES)	/* no slots available */
			return (ENODEV);
		*dev = makedevice(getmajor(*dev), unit);

		stp = kmem_zalloc(sizeof (struct ser_str), KM_NOSLEEP);
		if (stp == NULL) {
			cmn_err(CE_WARN,
			    "zsh clone open failed, no memory, rq=%x\n",
			    rq);
			return (ENOMEM);
		}
		stp->str_state = STR_CLONE;
		stp->str_com = NULL;	/* can't determine without ppa */
	}
	stp->str_rq = rq;
	stp->str_inst = unit;

	rq->q_ptr = WR(rq)->q_ptr = (caddr_t)stp;
	qprocson(rq);
	return (0);
}

/*
 * Close routine.
 */
/*ARGSUSED*/
int zsh_tx_enable_in_close = 0;
static int
zsh_close(queue_t *rq, int flag)
{
	register struct ser_str *stp, *pstp;
	register struct zscom *zs;
	register struct syncline *zss;
	register mblk_t	*mp;
	register u_char c;
	register int i, sl_wd_id, sl_bufcid;
	ulong_t		timestamp[3];
	int		len;

	/*
	 * Note that a close is only called on the last close of a
	 * particular stream.  Assume that we need to do it all.
	 */
	qprocsoff(rq);				/* no new business after this */

	stp = (struct ser_str *)rq->q_ptr;
	if (stp == NULL)
		return (0);			/* already been closed once */

	if (stp->str_state == STR_CLONE) {
		zsh_usedminor[stp->str_inst] = 0;
	} else {
		zs = (struct zscom *)stp->str_com;
		if (zs == NULL)
			return (0);

		TRACE_1(TR_ZSH, TR_ZSH_CLOSE,
			"zsh_close:unit = %d", zs->zs_unit);

		zss = (struct syncline *)zs->zs_priv;
		mutex_enter(zs->zs_excl);
		flushq(WR(rq), FLUSHALL);
		mutex_enter(zs->zs_excl_hi);
		if (zss->sl_xstandby) {
			zss->sl_xstandby->b_wptr = zss->sl_xstandby->b_rptr;
			ZSH_FREEMSG(zss->sl_xstandby);
			zss->sl_xstandby = NULL;
		}
		mutex_exit(zs->zs_excl_hi);

		ZSH_FLUSHQ;

		/*
		 * Stop the Watchdog Timer.
		 */
		if (sl_wd_id = zss->sl_wd_id)
			zss->sl_wd_id = 0;

		/*
		 * Cancel outstanding "bufcall" request.
		 */
		if (sl_bufcid = zss->sl_bufcid)
			zss->sl_bufcid = 0;

		mutex_enter(zs->zs_excl_hi);
		if (zs->zs_wr_cur) {
			zs->zs_wr_cur = NULL;
			zs->zs_wr_lim = NULL;
			SCC_WRITE0(ZSWR0_SEND_ABORT);
			mutex_exit(zs->zs_excl_hi);
			ZSDELAY();
			ZSDELAY();
			mutex_enter(zs->zs_excl_hi);
		}
		zss->sl_txstate = TX_OFF;	/* so it can't rearm in close */

		zs->zs_wr_cur = NULL;
		zs->zs_wr_lim = NULL;
		SCC_BIC(15,
		    (ZSR15_TX_UNDER | ZSR15_BREAK | ZSR15_SYNC | ZSR15_CTS));
		SCC_WRITE(3, 0);		/* Quiesce receiver */
		if (zsh_tx_enable_in_close && !(zss->sl_flags & SF_FDXPTP)) {
			SCC_BIS(5, ZSWR5_TX_ENABLE);
		} else
			SCC_BIC(5, ZSWR5_TX_ENABLE);

		SCC_BIC(5,  (ZSWR5_DTR | ZSWR5_RTS | ZSWR5_TXCRC_ENABLE));
		SCC_WRITE0(ZSWR0_RESET_TXINT);		/* reset TX */
		SCC_WRITE0(ZSWR0_RESET_STATUS);		/* reset XS */
		SCC_WRITE0(ZSWR0_RESET_ERRORS);
		c = SCC_READDATA();			/* reset RX */
		ZSDELAY();
		c = SCC_READDATA();
		ZSDELAY();
		c = SCC_READDATA();
		ZSDELAY();


		/*
		 * Free up everything we ever allocated.
		 */
		if (mp = zss->sl_rhead) {
			zss->sl_ractb = NULL;	/* already freed */
			zs->zs_rd_cur = NULL;
			zs->zs_rd_lim = NULL;
			zss->sl_rhead = NULL;
		}
		mutex_exit(zs->zs_excl_hi);
		if (mp)
			freemsg(mp);

		mutex_enter(zs->zs_excl_hi);
		if (mp = zss->sl_ractb) {
			zs->zs_rd_cur = NULL;
			zs->zs_rd_lim = NULL;
			zss->sl_ractb = NULL;
		}
		mutex_exit(zs->zs_excl_hi);
		if (mp)
			freemsg(mp);

		for (i = 0; i < ZSH_MAX_RSTANDBY; i++) {
			mutex_enter(zs->zs_excl_hi);
			mp = zss->sl_rstandby[i];
			zss->sl_rstandby[i] = NULL;
			mutex_exit(zs->zs_excl_hi);
			if (mp)
				freemsg(mp);
		}

		mutex_enter(zs->zs_excl_hi);
		if (mp = zss->sl_xhead) {
			zss->sl_xhead = NULL;
			zss->sl_xactb = NULL;
		}
		mutex_exit(zs->zs_excl_hi);
		if (mp)
			freemsg(mp);

		ZSH_FLUSHQ;

		mutex_enter(zs->zs_excl_hi);
		if (mp = zss->sl_xstandby)
			zss->sl_xstandby = NULL;
		mutex_exit(zs->zs_excl_hi);
		if (mp)
			freemsg(mp);

		mutex_enter(zs->zs_excl_hi);
		if (mp = zss->sl_mstat)
			zss->sl_mstat = NULL;
		zss->sl_txstate = TX_OFF;	/* so it can't rearm in close */
		mutex_exit(zs->zs_excl_hi);
		if (mp)
			freemsg(mp);

		zss->sl_stream.str_rq = NULL;
		zsopinit(zs, &zsops_null);
		mutex_exit(zs->zs_excl);
		if (sl_wd_id)
			(void) untimeout(sl_wd_id);
		if (sl_bufcid)
			unbufcall(sl_bufcid);
		while (zss->sl_soft_active)
			drv_usecwait(1);

		/* Mark device as available for power management */
		len = sizeof (timestamp);
		if (ddi_prop_op(DDI_DEV_T_ANY, zs->zs_dip,
		    PROP_LEN_AND_VAL_BUF, DDI_PROP_CANSLEEP, "pm_timestamp",
		    (caddr_t)timestamp, &len) != DDI_PROP_SUCCESS)
			cmn_err(CE_WARN, "zsh_close: Can't read pm property");
		drv_getparm(TIME, &(timestamp[zs->zs_unit%2+1]));
		if (ddi_prop_modify(DDI_DEV_T_NONE, zs->zs_dip,
		    DDI_PROP_CANSLEEP, "pm_timestamp", (caddr_t)timestamp,
		    sizeof (timestamp)) != DDI_PROP_SUCCESS)
			cmn_err(CE_WARN, "zsh_close: Can't modify pm property");
	}

	if (stp->str_state == STR_CLONE)
		kmem_free(stp, sizeof (struct ser_str));

	rq->q_ptr = WR(rq)->q_ptr = NULL;

	return (0);
}

static int
zsh_hdp_ok_or_rts_state(struct zscom *zs, struct syncline *zss)
{
	register u_char s0;

	SCC_BIS(15, ZSR15_CTS);
	SCC_BIS(5, ZSWR5_RTS);
	s0 = SCC_READ0();
	if (s0 & ZSRR0_CTS) {
		SCC_BIS(5, ZSWR5_TX_ENABLE);
		zss->sl_rr0 |= ZSRR0_CTS;
		return (1);
	}
	zss->sl_flags |= SF_XMT_INPROG;
	zss->sl_txstate = TX_RTS;
	zss->sl_rr0 &= ~ZSRR0_CTS;
	zss->sl_wd_count = zsh_timer_count;
	return (0);
}

/*
 * Put procedure for write queue.
 */
static int
zsh_wput(queue_t *wq, mblk_t *mp)
{
	register struct ser_str *stp = (struct ser_str *)wq->q_ptr;
	register struct zscom *zs;
	register struct syncline *zss = NULL;
	register u_char s0;
	register u_long	prim, error = 0;
	register union DL_primitives *dlp;
	register int	ppa;
	register mblk_t *tmp;
	register struct copyresp	*resp;

	/*
	 * stp->str_com supplied by open or DLPI attach.
	 */
	if (stp == NULL) {
		freemsg(mp);
		return (0);
	}
	zs = (struct zscom *)stp->str_com;

	TRACE_0(TR_ZSH, TR_ZSH_WPUT_START, "zsh_wput start");

	if ((mp->b_datap->db_type == M_FLUSH) &&
		    (stp->str_state == STR_CLONE)) {
		if (*mp->b_rptr & FLUSHW) {
			flushq(wq, FLUSHDATA);
			*mp->b_rptr &= ~FLUSHW;
		}
		if (*mp->b_rptr & FLUSHR)
			qreply(wq, mp);  /* let the read queues have at it */
		else
			freemsg(mp);
		return (0);
	}

	if ((zs == NULL) && (mp->b_datap->db_type != M_PROTO)) {
	    freemsg(mp);
	    cmn_err(CE_WARN,
		"zsh: clone device %d must be attached before use!",
		stp->str_inst);
	    putnextctl1(RD(wq), M_ERROR, EPROTO);
	    return (0);
	}

	if (stp->str_state == STR_CLONE) {	/* Clone opened, limited. */
		if ((mp->b_datap->db_type != M_PROTO) &&
		    (mp->b_datap->db_type != M_IOCTL) &&
		    (mp->b_datap->db_type != M_IOCDATA)) {
			freemsg(mp);
			cmn_err(CE_WARN,
			    "zsh%x: invalid operation for clone dev.\n",
			    stp->str_inst);
			putnextctl1(RD(wq), M_ERROR, EPROTO);
			return (0);
		}
	} else
		zss = (struct syncline *)zs->zs_priv;

	switch (mp->b_datap->db_type) {

	case M_DATA:
		/*
		 * Queue the message up to be transmitted.
		 * Set "in progress" flag and call the start routine.
		 */
		mutex_enter(zs->zs_excl_hi);
		if (!(zss->sl_flags & SF_INITIALIZED)) {
			mutex_exit(zs->zs_excl_hi);
			cmn_err(CE_WARN,
			    "zsh%x not initialized, can't send message",
			    zs->zs_unit);
			freemsg(mp);
			putnextctl1(RD(wq), M_ERROR, ECOMM);
			return (0);
		}
		mutex_exit(zs->zs_excl_hi);
		if (zs->zs_flags_need) {
			zs->zs_flags_need = 0;
			(void) zsh_softint(zs);
		}
		while (mp->b_wptr == mp->b_rptr) {
			register mblk_t *mp1;
			mp1 = unlinkb(mp);
			freemsg(mp);
			mp = mp1;
			if (!mp)
				return (0);
		}
		mutex_enter(zs->zs_excl);
		(void) putq(wq, mp);
		mutex_enter(zs->zs_excl_hi);
		if (zss->sl_flags & SF_FLUSH_WQ) {
			mutex_exit(zs->zs_excl_hi);
			flushq(wq, FLUSHDATA);
			mutex_exit(zs->zs_excl);

			TRACE_1(TR_ZSH, TR_ZSH_WPUT_END,
			    "zsh_wput end: unit = %d", zs->zs_unit);

			return (0);
		}
		tmp = NULL;
again:
		if (!zss->sl_xstandby) {
			if (tmp)
				zss->sl_xstandby = tmp;
			else {
				mutex_exit(zs->zs_excl_hi);
				tmp = getq(wq);
				mutex_enter(zs->zs_excl_hi);
				if (tmp)
					goto again;
			}
		} else if (tmp) {
			mutex_exit(zs->zs_excl_hi);
			(void) putbq(wq, tmp);
			mutex_enter(zs->zs_excl_hi);
		}

		if (zss->sl_flags & SF_XMT_INPROG) {
			mutex_exit(zs->zs_excl_hi);
			mutex_exit(zs->zs_excl);

			TRACE_1(TR_ZSH, TR_ZSH_WPUT_END,
			    "zsh_wput end: unit = %d", zs->zs_unit);

			return (0);
		}

		if (!zss->sl_wd_id) {
			zss->sl_wd_count = zsh_timer_count;
			zss->sl_txstate = TX_IDLE;
			mutex_exit(zs->zs_excl_hi);
			zss->sl_wd_id = timeout(zsh_watchdog, (caddr_t)zs,
			    SIO_WATCHDOG_TICK);
			mutex_enter(zs->zs_excl_hi);
		}

		zss->sl_flags |= SF_XMT_INPROG;
		if ((zss->sl_flags & SF_FDXPTP) ||
		    zsh_hdp_ok_or_rts_state(zs, zss))
			(void) zsh_start(zs, zss);
		mutex_exit(zs->zs_excl_hi);
		mutex_exit(zs->zs_excl);
		break;

	case M_PROTO:
		/*
		 * Here is where a clone device finds out about the
		 * hardware it is going to attach to.  The request is
		 * validated and a ppa is extracted from it and validated.
		 * This number is used to index the hardware data structure
		 * and the protocol data structure, in case the latter
		 * was not provided by a data-path open before this.
		 */
		if (stp->str_state != STR_CLONE) {
			freemsg(mp);
			return (EINVAL);
		}

		if (MBLKL(mp) < DL_ATTACH_REQ_SIZE) {
			prim = DL_ATTACH_REQ;
			error = DL_BADPRIM;
			goto end_proto;
		}
		dlp = (union DL_primitives *)mp->b_rptr;
		prim = dlp->dl_primitive;
		if (prim != DL_ATTACH_REQ) {
			error = DL_BADPRIM;
			goto end_proto;
		}
		ppa = dlp->attach_req.dl_ppa;
		if (ppa >= maxzsh) {
			error = DL_BADPPA;
			goto end_proto;
		}
		zs = &zscom[ppa];
		mutex_enter(zs->zs_excl);
		if ((zs->zs_ops != &zsops_null) &&
		    (zs->zs_ops != &zsops_hdlc)) {
			/*
			 * another protocol got here first
			 */
			error = (EBUSY);
			mutex_exit(zs->zs_excl);
			goto end_proto;

		}

		stp->str_com = (caddr_t)zs;
		mutex_exit(zs->zs_excl);
end_proto:
		if (error)
			dlerrorack(wq, mp, prim, error, 0);
		else
			dlokack(wq, mp, DL_ATTACH_REQ);
		break;

	case M_IOCTL:
		zsh_ioctl(wq, mp);
		break;

	case M_IOCDATA:
		resp = (struct copyresp *)mp->b_rptr;
		if (resp->cp_rval) {
			/*
			 * Just free message on failure.
			 */
			freemsg(mp);
			break;
		}

		switch (resp->cp_cmd) {

		case S_IOCGETMODE:
		case S_IOCGETSTATS:
		case S_IOCGETSPEED:
		case S_IOCGETMCTL:
		case S_IOCGETMRU:
			zsh_ack(mp, (mblk_t *)NULL, 0);
			qreply(wq, mp);
			break;

		case S_IOCSETMODE:
			zss  = (struct syncline *)&zs->zs_priv_str;
			mutex_enter(zs->zs_excl);
			error = zsh_setmode(zs, zss,
				(struct scc_mode *)mp->b_cont->b_rptr);
			if (error) {
				register struct iocblk  *iocp =
					(struct iocblk *)mp->b_rptr;
				mp->b_datap->db_type = M_IOCNAK;
				iocp->ioc_error = error;
			} else
				zsh_ack(mp, (mblk_t *)NULL, 0);
			mutex_exit(zs->zs_excl);
			qreply(wq, mp);
			break;

		case S_IOCSETMRU:
			zss  = (struct syncline *)&zs->zs_priv_str;
			mutex_enter(zs->zs_excl);
			zss->sl_mru = *(int *)mp->b_cont->b_rptr;
			mutex_exit(zs->zs_excl);
			zsh_ack(mp, (mblk_t *)NULL, 0);
			qreply(wq, mp);
			break;
		default:
			freemsg(mp);
		}
		break;

		/*
		 * We're at the bottom of the food chain, so we flush our
		 * write queue, clear the FLUSHW bit so it doesn't go round
		 * and round forever, then flush our read queue (since there's
		 * no read put procedure down here) and pass it up for any
		 * higher modules to deal with in their own way.
		 */
	case M_FLUSH:
		if (*mp->b_rptr & FLUSHW) {
			mutex_enter(zs->zs_excl);
			flushq(wq, FLUSHDATA);
			mutex_enter(zs->zs_excl_hi);
			tmp = zss->sl_xstandby;
			zss->sl_xstandby = NULL;
			mutex_exit(zs->zs_excl_hi);
			if (tmp)
				freemsg(tmp);
			mutex_exit(zs->zs_excl);
			*mp->b_rptr &= ~FLUSHW;
		}

		if (*mp->b_rptr & FLUSHR) {
			mutex_enter(zs->zs_excl);
			ZSH_FLUSHQ;
			mutex_exit(zs->zs_excl);
			qreply(wq, mp);  /* let the read queues have at it */
		} else
			freemsg(mp);
		break;

	default:
		/*
		 * "No, I don't want a subscription to Chain Store Age,
		 * thank you anyway."
		 */
		freemsg(mp);
		break;
	}

	TRACE_1(TR_ZSH, TR_ZSH_WPUT_END,
	    "zsh_wput end: unit = %d", zs->zs_unit);

	return (0);
}

/*
 * Get the next message from the write queue, set up the necessary pointers,
 * state info, etc., and start the transmit "engine" by sending the first
 * character.  We'll then rotate through txint until done, then get an xsint.
 */
static int
zsh_start(struct zscom *zs, struct syncline *zss)
{
	register mblk_t *mp;
	register u_char s0;
	register u_char *wptr;
	register  u_char *rptr;
	register u_char sl_flags = zss->sl_flags;

	/*
	 * Attempt to grab the next M_DATA message off the queue (that's
	 * all that will be left after wput) and begin transmission.
	 * This routine is normally called after completion of a previous
	 * frame, or when zsh_wput gets a new message.  If we are in a
	 * mode that put us in the TX_RTS state, waiting for CTS, and CTS
	 * is not up yet, we have no business here.  Ditto if we're in
	 * either the TX_ACTIVE or TX_CRC states.  In these cases we
	 * don't clear SF_CALLSTART, so we don't forget there's work to do.
	 */

	TRACE_1(TR_ZSH, TR_ZSH_START_START,
	    "zsh_start start: unit = %d", zs->zs_unit);

	if (sl_flags & SF_PHONY) {
		sl_flags &= ~SF_PHONY;
		SCC_BIC(15, ZSR15_CTS);
		SCC_BIC(5, ZSWR5_RTS);
		SCC_WRITE0(ZSWR0_RESET_TXINT);
		SCC_BIC(5, ZSWR5_TX_ENABLE);
		zss->sl_rr0 &= ~ZSRR0_CTS;
		zss->sl_txstate = TX_IDLE;
		/*
		 * if we get another msg by chance zsh_watchog will start
		 */
		sl_flags &= ~SF_XMT_INPROG;
		zss->sl_flags = sl_flags;

		TRACE_1(TR_ZSH, TR_ZSH_START_END,
		    "zsh_start end: unit = %d", zs->zs_unit);

		return (0);
	}
	mp = zss->sl_xstandby;
	if (mp == NULL) {
		if (!(sl_flags & SF_FDXPTP)) {
			sl_flags |= SF_PHONY;
			ZSH_ALLOCB(mp);
			if (!mp)
				return (0);
			mp->b_datap->db_type = M_RSE;
			mp->b_wptr = mp->b_rptr + 1;
			goto transmit;
		}
		sl_flags &= ~SF_XMT_INPROG;
		zss->sl_flags = sl_flags;

		TRACE_1(TR_ZSH, TR_ZSH_START_END,
		    "zsh_start end: unit = %d", zs->zs_unit);

		return (0);
	}

transmit:
	zss->sl_xstandby = NULL;
	rptr = mp->b_rptr;
	wptr = mp->b_wptr;
	ZSSETSOFT(zs);

#ifdef ZSH_DEBUG
	if (zss->sl_xhead || zss->sl_xactb) {
		debug_enter("xhead1");
	}
#endif

	zss->sl_xhead = mp;
	zss->sl_xactb = mp;
	zss->sl_wd_count = zsh_timer_count;
	zss->sl_txstate = TX_ACTIVE;
	zss->sl_ocnt = 0;
	SCC_BIS(10, ZSWR10_UNDERRUN_ABORT);	/* abort on underrun */
	SCC_WRITE0(ZSWR0_RESET_TXCRC);		/* reset transmit CRC */
	zss->sl_ocnt = wptr - rptr;
	mp->b_wptr = rptr; /* to tell soft to free this msg */
	SCC_WRITEDATA(*rptr++);    /* resets TXINT */
	zs->zs_wr_cur = rptr;
	zs->zs_wr_lim = wptr;

	SCC_WRITE0(ZSWR0_RESET_EOM);

	TRACE_1(TR_ZSH, TR_ZSH_START_END,
	    "zsh_start end: unit = %d", zs->zs_unit);

	zss->sl_flags = sl_flags;
	return (1);
}


/*
 * Process an "ioctl" message sent down to us.
 */
static void
zsh_ioctl(queue_t *wq, mblk_t *mp)
{
	register struct ser_str *stp = (struct ser_str *)wq->q_ptr;
	register struct zscom *zs = (struct zscom *)stp->str_com;
	register struct syncline *zss  = (struct syncline *)&zs->zs_priv_str;
	register struct iocblk   *iocp = (struct iocblk *)mp->b_rptr;
	register struct scc_mode *sm;
	register struct sl_stats *st;
	register int		 *speed, *mru, *mtu;
	register u_char		 *msignals;
	register mblk_t		 *tmp;
	register int		 error = 0;

	mutex_enter(zs->zs_excl);
	if ((zs->zs_ops != &zsops_null) &&
	    (zs->zs_ops != &zsops_hdlc)) {
		/*
		 * another protocol got here first
		 */
		error = (EBUSY);
		goto end_zsh_ioctl;
	}


	switch (iocp->ioc_cmd) {

	case S_IOCGETMODE:
		tmp = allocb(sizeof (struct scc_mode), BPRI_MED);
		if (tmp == NULL) {
			error = EAGAIN;
			break;
		}
		if (iocp->ioc_count != TRANSPARENT)
			zsh_ack(mp, tmp, sizeof (struct scc_mode));
		else
			zsh_copy(mp, tmp, sizeof (struct scc_mode), M_COPYOUT);
		sm = (struct scc_mode *)mp->b_cont->b_rptr;
		bcopy((char *)&zss->sl_mode, (char *)sm,
		    sizeof (struct scc_mode));
		break;

	case S_IOCGETSTATS:
		tmp = allocb(sizeof (struct sl_stats), BPRI_MED);
		if (tmp == NULL) {
			error = EAGAIN;
			break;
		}
		if (iocp->ioc_count != TRANSPARENT)
			zsh_ack(mp, tmp, sizeof (struct sl_stats));
		else
			zsh_copy(mp, tmp, sizeof (struct sl_stats), M_COPYOUT);
		st = (struct sl_stats *)mp->b_cont->b_rptr;
		bcopy((char *)&zss->sl_st, (char *)st,
		    sizeof (struct sl_stats));
		break;

	case S_IOCGETSPEED:
		tmp = allocb(sizeof (int), BPRI_MED);
		if (tmp == NULL) {
			error = EAGAIN;
			break;
		}
		if (iocp->ioc_count != TRANSPARENT)
			zsh_ack(mp, tmp, sizeof (int));
		else
			zsh_copy(mp, tmp, sizeof (int), M_COPYOUT);
		speed = (int *)mp->b_cont->b_rptr;
		*speed = zss->sl_mode.sm_baudrate;
		break;

	case S_IOCGETMCTL:
		tmp = allocb(sizeof (char), BPRI_MED);
		if (tmp == NULL) {
			error = EAGAIN;
			break;
		}
		if (iocp->ioc_count != TRANSPARENT)
			zsh_ack(mp, tmp, sizeof (char));
		else
			zsh_copy(mp, tmp, sizeof (char), M_COPYOUT);
		msignals = (u_char *)mp->b_cont->b_rptr;
		*msignals = zss->sl_rr0 & (ZSRR0_CD | ZSRR0_CTS);
		break;

	case S_IOCGETMRU:
		tmp = allocb(sizeof (int), BPRI_MED);
		if (tmp == NULL) {
			error = EAGAIN;
			break;
		}
		if (iocp->ioc_count != TRANSPARENT)
			zsh_ack(mp, tmp, sizeof (int));
		else
			zsh_copy(mp, tmp, sizeof (int), M_COPYOUT);
		mru = (int *)mp->b_cont->b_rptr;
		*mru = zss->sl_mru;
		break;

	case S_IOCSETMODE:
		if (iocp->ioc_count != TRANSPARENT) {
		    if ((mp->b_cont->b_datap->db_lim - mp->b_cont->b_rptr) <
			sizeof (struct scc_mode)) {
			error = EINVAL;
			break;
		    }
		    error = zsh_setmode(zs, zss,
				(struct scc_mode *)mp->b_cont->b_rptr);
		    if (!error)
			zsh_ack(mp, (mblk_t *)NULL, 0);
		} else
			zsh_copy(mp, (mblk_t *)NULL, sizeof (struct scc_mode),
				M_COPYIN);
		break;

	case S_IOCCLRSTATS:
		mutex_enter(zs->zs_excl_hi);
		(void) bzero((caddr_t)&zss->sl_st, sizeof (struct sl_stats));
		mutex_exit(zs->zs_excl_hi);
		zsh_ack(mp, (mblk_t *)NULL, 0);
		break;

	case S_IOCSETMRU:
		if (iocp->ioc_count != TRANSPARENT) {
		    if ((mp->b_cont->b_datap->db_lim - mp->b_cont->b_rptr) <
			sizeof (int)) {
			error = EINVAL;
			break;
		    }
		    mru = (int *)mp->b_cont->b_rptr;
		    zss->sl_mru = *mru;
		    zsh_ack(mp, (mblk_t *)NULL, 0);
		} else
			zsh_copy(mp, (mblk_t *)NULL, sizeof (int), M_COPYIN);

		break;

	default:
		error = EINVAL;

	}
end_zsh_ioctl:
	iocp->ioc_error = error;
	mp->b_datap->db_type = (error) ? M_IOCNAK : M_IOCACK;
	mutex_exit(zs->zs_excl);
	qreply(wq, mp);
}

/*
 * Set the mode of the zsh port
 */

int
zsh_setmode(struct zscom *zs, struct syncline *zss, struct scc_mode *sm)
{
	register int error = 0;
	register mblk_t *mp;

	mutex_enter(zs->zs_excl_hi);
	if (sm->sm_rxclock == RXC_IS_PLL) {
		zss->sl_mode.sm_retval = SMERR_RXC;
		mutex_exit(zs->zs_excl_hi);
		return (EINVAL);		/* not supported */
	} else {
		if (((zss->sl_mode.sm_config ^ sm->sm_config) &
			CONN_SIGNAL) != 0) { /* Changing, going... */
			if (sm->sm_config & CONN_SIGNAL) { /* ...up. */
				mutex_exit(zs->zs_excl_hi);
				mp = allocb(
					sizeof (struct sl_status), BPRI_MED);
				mutex_enter(zs->zs_excl_hi);
				zss->sl_mstat = mp;
			} else {			/* ...down. */
				if (mp = zss->sl_mstat)
					zss->sl_mstat = NULL;
				mutex_exit(zs->zs_excl_hi);
				if (mp)
					freemsg(mp);
				mutex_enter(zs->zs_excl_hi);
			}
		}
		if (!(sm->sm_config & CONN_IBM)) {
			if (sm->sm_config & CONN_HDX) {
				zss->sl_mode.sm_retval = SMERR_HDX;
				mutex_exit(zs->zs_excl_hi);
				return (EINVAL);
			}
			if (sm->sm_config & CONN_MPT) {
				zss->sl_mode.sm_retval = SMERR_MPT;
				mutex_exit(zs->zs_excl_hi);
				return (EINVAL);
			}
		}
		zss->sl_flags &= ~SF_FDXPTP;		/* "conmode" */
		if ((sm->sm_config & (CONN_HDX | CONN_MPT)) == 0)
			zss->sl_flags |= SF_FDXPTP;

		mutex_exit(zs->zs_excl_hi);
		mutex_exit(zs->zs_excl);
		error = zsh_program(zs, sm);
		mutex_enter(zs->zs_excl);
		mutex_enter(zs->zs_excl_hi);
		if (!error && (zs->zs_ops != &zsops_null))
			zsh_init_port(zs, zss);
	}
	mutex_exit(zs->zs_excl_hi);

	return (error);
}

/*
 * Copy message block
 */
static void
zsh_copy(mblk_t *mp, mblk_t *dp, unsigned int size, unsigned char type)
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
		dp->b_wptr += size;
	} else
		mp->b_cont = NULL;
	mp->b_datap->db_type = type;
	mp->b_wptr = mp->b_rptr + sizeof (*cp);
}

/*
 * Ack the ioctl
 */
static void
zsh_ack(mblk_t *mp, mblk_t *dp, unsigned int size)
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

/*
 * Transmit interrupt service procedure
 */

static void
zsh_txint(struct zscom *zs)
{
	register struct syncline *zss;
	register mblk_t *mp;
	register int tmp;
	register u_char	 *wr_cur;

	TRACE_1(TR_ZSH, TR_ZSH_TXINT,
	    "zsh_txint: unit = %d", zs->zs_unit);

	if ((wr_cur =  zs->zs_wr_cur) != NULL && (wr_cur <  zs->zs_wr_lim)) {
		SCC_WRITEDATA(*wr_cur++);
		zs->zs_wr_cur = wr_cur;
		return;
	}


	zss = (struct syncline *)&zs->zs_priv_str;

	switch (zss->sl_txstate) {

	/*
	 * we here because end of message block lim = cur
	 */
	case TX_ACTIVE:

		mp = zss->sl_xactb;

again_txint:
		mp = mp->b_cont;
		if (mp) {
			zss->sl_xactb = mp;
			zss->sl_ocnt += tmp = mp->b_wptr - mp->b_rptr;
			if (!tmp)
				goto again_txint;
			zs->zs_wr_cur = mp->b_rptr;
			zs->zs_wr_lim = mp->b_wptr;
			SCC_WRITEDATA(*zs->zs_wr_cur++);
			return;
		}

		/*
		 * This is where the fun starts.  At this point the
		 * last character in the frame has been sent.  We
		 * issue a RESET_TXINT so we won't get another txint
		 * until the CRC has been completely sent.  Also we
		 * reset the Abort-On-Underrun bit so that CRC is
		 * sent at EOM, rather than an Abort.
		 */
		zs->zs_wr_cur = zs->zs_wr_lim = NULL;
		zss->sl_txstate = TX_CRC;
		SCC_WRITE0(ZSWR0_RESET_TXINT);
		if (!(zss->sl_flags & SF_PHONY)) {
			SCC_BIC(10, ZSWR10_UNDERRUN_ABORT);
			zss->sl_st.opack++;
			zss->sl_st.ochar += zss->sl_ocnt;
		}
		zss->sl_ocnt = 0;
		ZSH_FREEMSG(zss->sl_xhead);
		zss->sl_xhead = zss->sl_xactb = NULL;
		ZSSETSOFT(zs);
		break;
	/*
	 * This txint means we have sent the CRC bytes at EOF.
	 * The next txint will mean we are sending or have sent the
	 * flag character at EOF, but we handle that differently, and
	 * enter different states,depending on whether we're IBM or not.
	 */
	case TX_CRC:
		if (!(zss->sl_flags & SF_FDXPTP)) {
			zss->sl_txstate = TX_FLAG;	/* HDX path */
		} else {	/* FDX path */
			if (!zsh_start(zs, zss)) {
				zss->sl_txstate = TX_IDLE;
				SCC_WRITE0(ZSWR0_RESET_TXINT);
			}
		}
		break;

	/*
	 * This txint means the closing flag byte is going out the door.
	 * We use this state to allow this to complete before dropping RTS.
	 */
	case TX_FLAG:
		zss->sl_txstate = TX_LAST;
		(void) zsh_start(zs, zss);
		break;

	/*
	 * Arriving here means the flag should be out and it's finally
	 * time to close the barn door.
	 */
	case TX_LAST:
		zss->sl_txstate = TX_IDLE;
		SCC_WRITE0(ZSWR0_RESET_TXINT);
		break;

	/*
	 * If transmit was aborted, do nothing - watchdog will recover.
	 */
	case TX_ABORTED:
		SCC_WRITE0(ZSWR0_RESET_TXINT);
		break;

	default:
		SCC_WRITE0(ZSWR0_RESET_TXINT);
		break;
	}
}

/*
 * External Status Change interrupt service procedure
 */
static void
zsh_xsint(struct zscom *zs)
{
	register struct syncline *zss = (struct syncline *)&zs->zs_priv_str;
	register mblk_t *mp;
	register u_char s0, x0;

	TRACE_1(TR_ZSH, TR_ZSH_XSINT,
	    "zsh_xsint: unit = %d", zs->zs_unit);

	s0 = SCC_READ0();
	x0 = s0 ^ zss->sl_rr0;
	zss->sl_rr0 = s0;
	SCC_WRITE0(ZSWR0_RESET_STATUS);

	if (s0 & ZSRR0_TXUNDER) {
		switch (zss->sl_txstate) {
		/*
		 * A transmitter underrun has occurred.  If we are not
		 * here as the result of an abort sent by the watchdog
		 * timeout routine, we need to send an abort to flush
		 * the transmitter.  Otherwise there is a danger of
		 * trashing the next frame but still sending a good crc.
		 * The TX_ABORTED flag is set so that the watchdog
		 * routine can initiate recovery.
		 */
		case TX_ACTIVE:
			SCC_WRITE0(ZSWR0_SEND_ABORT);
			SCC_WRITE0(ZSWR0_RESET_TXINT);
			zss->sl_st.underrun++;
			zsh_txbad(zs, zss);

			zss->sl_txstate = TX_ABORTED;
			zss->sl_wd_count = 0;
			break;

		case TX_CRC:
			break;

		case TX_FLAG:
			break;

		case TX_ABORTED:
			break;

		case TX_OFF:
			break;

		case TX_LAST:
			break;

		default:
			break;
		}
	}

	if ((x0 & ZSRR0_BREAK) && (s0 & ZSRR0_BREAK) && zs->zs_rd_cur) {
		zss->sl_st.abort++;
		zsh_rxbad(zs, zss);
	} else if ((s0 & ZSRR0_SYNC) && (zs->zs_rd_cur)) {
		/*
		 * Tricky code to avoid disaster in the case where
		 * an abort was detected while receiving a packet,
		 * but the abort did not last long enough to be
		 * detected by zsh_xsint - this can happen since
		 * the ZSRR0_BREAK is not latched.  Since an abort
		 * will automatically cause the SCC to enter
		 * hunt mode, hopefully, the sync/hunt bit will be
		 * set in this case (although if the interrupt is
		 * sufficiently delayed, the SCC may have sync'ed
		 * in again if it has detected a flag).
		 */
		zss->sl_st.abort++;
		zsh_rxbad(zs, zss);
	}

	if (x0 & s0 & ZSRR0_CTS) {
	    if (zss->sl_txstate == TX_RTS) {
		if (!(zss->sl_flags & SF_FDXPTP)) {
			SCC_BIS(5, ZSWR5_TX_ENABLE);
		}
		(void) zsh_start(zs, zss);
	    } else if ((zss->sl_mode.sm_config & (CONN_IBM | CONN_SIGNAL))) {
		zss->sl_flags &= ~SF_FLUSH_WQ;
		zsh_setmstat(zs, CS_CTS_UP);
	    }
	}

	/*
	 * We don't care about CTS transitions unless we are in either
	 * IBM or SIGNAL mode, or both.  So, if we see CTS drop, and we
	 * care, and we are not idle, send up a report message.
	 */
	if ((x0 & ZSRR0_CTS) && ((s0 & ZSRR0_CTS) == 0) &&
	    (zss->sl_txstate != TX_OFF) &&
	    (zss->sl_mode.sm_config & (CONN_IBM | CONN_SIGNAL))) {
		SCC_BIC(15, ZSR15_CTS);
		zsh_setmstat(zs, CS_CTS_DOWN);
		zss->sl_flags &= ~SF_XMT_INPROG;
		zss->sl_flags |= SF_FLUSH_WQ;
		zss->sl_st.cts++;
		if (zss->sl_txstate != TX_IDLE)
			SCC_WRITE0(ZSWR0_SEND_ABORT);
		SCC_WRITE0(ZSWR0_RESET_ERRORS);
		SCC_WRITE0(ZSWR0_RESET_TXINT);
		zss->sl_wd_count = 0;
		zsh_txbad(zs, zss);
	}
}


/*
 * Receive interrupt service procedure
 */
static void
zsh_rxint(struct zscom *zs)
{
	register struct syncline *zss = (struct syncline *)&zs->zs_priv_str;
	register mblk_t *bp = zss->sl_ractb;
	unsigned char *rd_cur;

	TRACE_1(TR_ZSH, TR_ZSH_RXINT, "zsh_rxint: unit = %d", zs->zs_unit);

	if ((rd_cur = zs->zs_rd_cur) && rd_cur < zs->zs_rd_lim) {
		*rd_cur++ = SCC_READDATA();
		zs->zs_rd_cur = rd_cur;
		return;
	}

	if (!rd_cur) { /* Beginning of frame */
		if (!bp) {
			ZSH_ALLOCB(bp);
			zss->sl_ractb = bp;
		}
		zss->sl_rhead = bp;
	} else {	/* end of data block should be cur==lim */
		bp->b_wptr = zs->zs_rd_cur;
		ZSH_ALLOCB(bp->b_cont);
		bp = zss->sl_ractb = bp->b_cont;
	}
	if (!bp) {
		zss->sl_st.nobuffers++;
		zsh_rxbad(zs, zss);
		return;
	}
	zs->zs_rd_cur = bp->b_wptr;
	zs->zs_rd_lim = bp->b_datap->db_lim;
	*zs->zs_rd_cur++ = SCC_READDATA(); /* Also resets interrupt */
}


/*
 * Special Receive Condition Interrupt routine
 */
static void
zsh_srint(struct zscom *zs)
{
	register struct syncline *zss = (struct syncline *)&zs->zs_priv_str;
	register u_char s1, c;
	register u_char *rd_cur;

	TRACE_1(TR_ZSH, TR_ZSH_SRINT,
	    "zsh_srint: unit = %d", zs->zs_unit);

	SCC_READ(1, s1);

	if (s1 & ZSRR1_RXEOF) {			/* end of frame */
		c = SCC_READDATA();
		SCC_WRITE0(ZSWR0_RESET_ERRORS);
		if (s1 & ZSRR1_FE) {		/* bad CRC */
			zss->sl_st.crc++;
			zsh_rxbad(zs, zss);
			return;
		}

		if (!(rd_cur = zs->zs_rd_cur))
			return;

		/*
		 * Drop one CRC byte from length because it came in
		 * before the special interrupt got here.
		 */
		zss->sl_ractb->b_wptr = rd_cur - 1;

		/*
		 * put on done queue
		 */
		ZSH_PUTQ(zss->sl_rhead);
		zss->sl_rhead = NULL;
		zss->sl_ractb = NULL;
		zs->zs_rd_cur = NULL;
		zs->zs_rd_lim = NULL;
		ZSSETSOFT(zs);

	} else if (s1 & ZSRR1_DO) {
		c = SCC_READDATA();
		SCC_WRITE0(ZSWR0_RESET_ERRORS);
		zss->sl_st.overrun++;
		zsh_rxbad(zs, zss);
	} else
		SCC_WRITE0(ZSWR0_RESET_ERRORS);
}

/*
 * Handle a second stage interrupt.
 * Does mostly lower priority buffer management stuff.
 */
static int
zsh_softint(struct zscom *zs)
{
	register struct syncline *zss;
	register queue_t *q;
	register mblk_t *mp, *tmp;
	register mblk_t *head = NULL, *tail = NULL;
	register int allocbcount = 0;
	register int n;
	register m_error;

	TRACE_1(TR_ZSH, TR_ZSH_SOFT_START,
	    "zsh_soft start: unit = %d", zs->zs_unit);

	mutex_enter(zs->zs_excl);
	zss = (struct syncline *)zs->zs_priv;
	if (!zss || (q = zss->sl_stream.str_rq) == NULL) {
		mutex_exit(zs->zs_excl);
		return (0);
	}
	m_error = zss->sl_m_error;

	zss->sl_m_error = 0;


	if (!zss->sl_mstat)
		zss->sl_mstat = allocb(sizeof (struct sl_status), BPRI_MED);

	mutex_enter(zs->zs_excl_hi);
	if (zss->sl_flags & SF_FLUSH_WQ) {
		if (!(zss->sl_flags & SF_FDXPTP)) {
			zss->sl_flags &= ~SF_FLUSH_WQ;
		} else {
			register u_char s0;

			s0 = SCC_READ0();
			if (s0 & ZSRR0_CTS) {
				zss->sl_rr0 |= ZSRR0_CTS;
				SCC_BIS(15, ZSR15_CTS);
				zss->sl_flags &= ~SF_FLUSH_WQ;
				zsh_setmstat(zs, CS_CTS_UP);
			}
			if (zss->sl_flags & SF_FLUSH_WQ) {
				mutex_exit(zs->zs_excl_hi);
				flushq(WR(q), FLUSHDATA);
				goto next;
			}
		}
	}
	mutex_exit(zs->zs_excl_hi);

next:
	while (1) {
		ZSH_GETQ(mp);
		if (!mp)
			break;

		if (mp->b_rptr == mp->b_wptr) {
			if (mp->b_datap->db_type == M_RSE) {
				allocbcount++;
				mp->b_datap->db_type == M_DATA;
			}
			freemsg(mp);
			continue;
		}
		if (mp->b_datap->db_type == M_DATA) {
			zss->sl_st.ichar += msgdsize(mp);
			zss->sl_st.ipack++;
			if (!(canputnext(q))) {
				zss->sl_st.ierror++;
				freemsg(mp);
				continue;
			}
		} else if (mp->b_datap->db_type == M_PROTO) {
			if (!(canputnext(q))) {
				freemsg(mp);
				continue;
			}
		}
		if (! head) {
			allocbcount++;
			zss->sl_soft_active = 1;
			head = mp;
		} else {
			if (!tail)
				tail = head;
			tail->b_next = mp;
			tail = mp;
		}
	}
	if (allocbcount)
		ZSH_GETBLOCK(zs, allocbcount);

	tmp = NULL;
again:
	mutex_enter(zs->zs_excl_hi);
	if (!zss->sl_xstandby) {
		if (tmp) {
			zss->sl_xstandby = tmp;
			mutex_exit(zs->zs_excl_hi);
		} else {
			mutex_exit(zs->zs_excl_hi);
			if (tmp = getq(WR(q)))
				goto again;
		}
	} else {
		mutex_exit(zs->zs_excl_hi);
		if (tmp)
			(void) putbq(WR(q), tmp);
	}

	mutex_exit(zs->zs_excl);

	while (head) {
		if (!tail) {
			putnext(q, head);
			break;
		}
		mp = head;
		head = head->b_next;
		mp->b_next = NULL;
		putnext(q, mp);

	}

	if (m_error)
		putnextctl1(q, M_ERROR, m_error);

	zss->sl_soft_active = 0;

	TRACE_1(TR_ZSH, TR_ZSH_SOFT_END,
	    "zsh_soft end: unit = %d", zs->zs_unit);

	return (0);
}

/*
 * Initialization routine.
 * Sets Clock sources, baud rate, modes and miscellaneous parameters.
 */
static int
zsh_program(struct zscom *zs, struct scc_mode *sm)
{
	register struct syncline *zss  = (struct syncline *)&zs->zs_priv_str;
	register struct zs_prog *zspp;
	register u_short	tconst = 0;
	register int	wr11 = 0;
	register int	baud = 0;
	register int	pll = 0;
	register int	speed = 0;
	register int	flags = ZSP_SYNC;
	int		err = 0;

	ddi_trigger_softintr(zs);	/* get our house in order */

	mutex_enter(zs->zs_excl);
	mutex_enter(zs->zs_excl_hi);

	switch (sm->sm_txclock) {
	case TXC_IS_TXC:
		wr11 |= ZSWR11_TXCLK_TRXC;
		break;
	case TXC_IS_RXC:
		wr11 |= ZSWR11_TXCLK_RTXC;
		break;
	case TXC_IS_BAUD:
		wr11 |= ZSWR11_TXCLK_BAUD;
		wr11 |= ZSWR11_TRXC_OUT_ENA + ZSWR11_TRXC_XMIT;
		baud++;
		break;
	case TXC_IS_PLL:
		wr11 |= ZSWR11_TXCLK_DPLL;
		pll++;
		break;
	default:
		zss->sl_mode.sm_retval = SMERR_TXC;
		err = EINVAL;
		goto out;
	}
	switch (sm->sm_rxclock) {
	case RXC_IS_RXC:
		wr11 |= ZSWR11_RXCLK_RTXC;
		break;
	case RXC_IS_TXC:
		wr11 |= ZSWR11_RXCLK_TRXC;
		break;
	case RXC_IS_BAUD:
		wr11 |= ZSWR11_RXCLK_BAUD;
		baud++;
		break;
	case RXC_IS_PLL:
		wr11 |= ZSWR11_RXCLK_DPLL;
		pll++;
		break;
	default:
		zss->sl_mode.sm_retval = SMERR_RXC;
		err = EINVAL;
		goto out;
	}
	if (baud && pll) {
		zss->sl_mode.sm_retval = SMERR_PLL;
		err = EINVAL;
		goto out;
	}
	if (pll && !(sm->sm_config & CONN_NRZI)) {
		zss->sl_mode.sm_retval = SMERR_PLL;
		err = EINVAL;
		goto out;
	}

	/*
	 * If we're going to use the BRG and the speed we want is != 0...
	 */
	if (baud && (speed = sm->sm_baudrate)) {
		tconst = (PCLK + speed) / (2 * speed) - 2;
		if (tconst == 0) {
			zss->sl_mode.sm_retval = SMERR_BAUDRATE;
			err = EINVAL;
			goto out;
		}
		sm->sm_baudrate = PCLK / (2 * ((int)tconst + 2));
	} else {
		tconst = 0;	/* Stop BRG.  Also quiesces pin 24. */
	}

	if (pll) {
		if (speed  = sm->sm_baudrate * 32)
			tconst = (PCLK + speed) / (2 * speed) - 2;
		else
			tconst = 0;
		if (tconst == 0) {
			zss->sl_mode.sm_retval = SMERR_BAUDRATE;
			err = EINVAL;
			goto out;
		}
		speed = PCLK / (2 * ((int)tconst + 2));
		sm->sm_baudrate = speed / 32;
		flags |= ZSP_PLL;
	}

	if ((sm->sm_config & (CONN_LPBK|CONN_ECHO)) == (CONN_LPBK|CONN_ECHO)) {
		zss->sl_mode.sm_retval = SMERR_LPBKS;
		err = EINVAL;
		goto out;
	}
	if (sm->sm_config & CONN_LPBK)
		flags |= ZSP_LOOP;
	if (sm->sm_config & CONN_NRZI)
		flags |= ZSP_NRZI;
	if (sm->sm_config & CONN_ECHO)
		flags |= ZSP_ECHO;

	zspp = &zs_prog[zs->zs_unit];

	zspp->zs = zs;
	zspp->flags = (u_char)flags;
	zspp->wr4 = ZSWR4_SDLC;
	zspp->wr11 = (u_char)wr11;
	zspp->wr12 = (u_char)(tconst & 0xff);
	zspp->wr13 = (u_char)((tconst >> 8) & 0xff);
	zspp->wr3 = (u_char)(ZSWR3_RX_ENABLE | ZSWR3_RXCRC_ENABLE | ZSWR3_RX_8);
	zspp->wr5 = (u_char)(ZSWR5_TX_8 | ZSWR5_DTR | ZSWR5_TXCRC_ENABLE);

	if (zss->sl_flags & SF_FDXPTP) {
		zspp->wr5 |= ZSWR5_RTS;
		zss->sl_rr0 |= ZSRR0_CTS;		/* Assume CTS is high */
	}
	if (sm->sm_config & CONN_IBM) {
		zspp->wr15 = (u_char)
		    (ZSR15_TX_UNDER | ZSR15_BREAK | ZSR15_SYNC | ZSR15_CTS);
		if (!(zss->sl_flags & SF_FDXPTP))
			zspp->wr15 &= ~ZSR15_CTS;
	} else {
		zspp->wr5 |= ZSWR5_TX_ENABLE;
		zspp->wr15 = (u_char)
		    (ZSR15_TX_UNDER | ZSR15_BREAK | ZSR15_SYNC);
		if (sm->sm_config & CONN_SIGNAL)
			zspp->wr15 |= ZSR15_CTS;
	}

	zs_program(zspp);
	SCC_WRITE0(ZSWR0_RESET_STATUS);		/* reset XS */
	SCC_WRITE0(ZSWR0_RESET_STATUS);		/* reset XS */
	zss->sl_flags |= SF_INITIALIZED;
	bzero((caddr_t)&zss->sl_st, (u_int)sizeof (struct sl_stats));
	bcopy((char *)sm, (char *)&zss->sl_mode, sizeof (struct scc_mode));
	zss->sl_mode.sm_retval = 0;	/* successful */
out:
	mutex_exit(zs->zs_excl_hi);
	mutex_exit(zs->zs_excl);
	return (err);
}

/*
 * Function to store modem signal changes in sl_mstat field.
 * Note that these events are supposed to be so far apart in time that
 * we should always be able to send up the event and allocate a message
 * block before another one happens.  If not, we'll overwrite this one.
 */
static void
zsh_setmstat(struct zscom *zs, int event)
{
	register struct syncline *zss = (struct syncline *)&zs->zs_priv_str;
	register struct sl_status *mstat;
	register mblk_t * mp;

	if (((mp = zss->sl_mstat) != NULL) &&
	    (zss->sl_mode.sm_config & (CONN_SIGNAL))) {
		mstat = (struct sl_status *)mp->b_wptr;
		mstat->type = (zss->sl_mode.sm_config & CONN_IBM) ?
		    SLS_LINKERR : SLS_MDMSTAT;
		mstat->status = event;
		mstat->tstamp = hrestime;
		mp->b_wptr += sizeof (struct sl_status);
		mp->b_datap->db_type = M_PROTO;
		ZSH_PUTQ(mp);
		zss->sl_mstat = NULL;
		ZSSETSOFT(zs);
	}
}

/*
 * Received Bad Frame procedure
 */
static void
zsh_rxbad(struct zscom *zs, struct syncline *zss)
{
	register u_char c;

	/*
	 * swallow bad characters
	 */
	c = SCC_READDATA();
	c = SCC_READDATA();
	c = SCC_READDATA();

	SCC_BIS(3, ZSWR3_HUNT);	/* enter hunt mode - ignores rest of frame */

	zss->sl_st.ierror++;

	/*
	 * Free active receive message.
	 */
	if (zss->sl_rhead) {
		zss->sl_rhead->b_wptr = zss->sl_rhead->b_rptr;
		zss->sl_rhead->b_datap->db_type = M_RSE;
		ZSH_FREEMSG(zss->sl_rhead);
		zss->sl_ractb = NULL;
		zs->zs_rd_cur = NULL;
		zs->zs_rd_lim = NULL;
	}
	if (zss->sl_rhead) {
		zss->sl_rhead = NULL;
		ZSH_ALLOCB(zss->sl_ractb);
		zs->zs_rd_cur = NULL;
		zs->zs_rd_lim = NULL;
	}

	ZSSETSOFT(zs);
}

/*
 * Transmit error procedure
 */
static void
zsh_txbad(struct zscom *zs, struct syncline *zss)
{
	register mblk_t *mp;

	if (zss->sl_xhead) {		/* free the message we were sending */
		ZSH_FREEMSG(zss->sl_xhead);
		zss->sl_xactb = NULL;
		zs->zs_wr_cur = NULL;
		zs->zs_wr_lim = NULL;
	}
	zss->sl_xhead = NULL;

	if (!(zss->sl_flags & SF_FDXPTP)) {
		/*
		 * drop RTS and our notion of CTS
		 */
		SCC_BIC(5, ZSWR5_RTS);
		SCC_BIC(5, ZSWR5_TX_ENABLE);
		zss->sl_rr0 &= ~ZSRR0_CTS;
	}
	zss->sl_txstate = TX_IDLE;
	if (!(zss->sl_flags & SF_PHONY))
		zss->sl_st.oerror++;
}

/*
 * Transmitter watchdog timeout routine
 */
static void
zsh_watchdog(struct zscom *zs)
{
	register struct syncline *zss = (struct syncline *)&zs->zs_priv_str;
	register queue_t *wq;
	register mblk_t *mp;
	register int warning = 0;
	register u_char s0;
	register int do_flushwq = 0;

	/*
	 * The main reason for this routine is because, under some
	 * circumstances, a transmit interrupt may get lost (ie., if
	 * underrun occurs after the last character has been sent, and
	 * the tx interrupt following the abort gets scheduled before
	 * the current tx interrupt has been serviced).  Transmit can
	 * also get hung if the cable is pulled out and the clock was
	 * coming in from the modem.
	 */

	mutex_enter(zs->zs_excl);
	if (zss->sl_stream.str_rq)
		wq = WR(zss->sl_stream.str_rq);
	else {
		mutex_exit(zs->zs_excl);
		return;		/* guard against close/callback race */
	}

	mutex_enter(zs->zs_excl_hi);
	if (!(zss->sl_flags & SF_XMT_INPROG) && wq->q_first) {
		zss->sl_flags |= SF_XMT_INPROG;
		if ((zss->sl_flags & SF_FDXPTP) ||
		    zsh_hdp_ok_or_rts_state(zs, zss))
			(void) zsh_start(zs, zss);
		goto end_watchdog;
	}

	if (zss->sl_wd_count-- > 0)
		goto end_watchdog;

	if (zss->sl_flags & SF_FLUSH_WQ) {
		if (!(zss->sl_flags & SF_FDXPTP))
			zss->sl_flags &= ~SF_FLUSH_WQ;
		else {
			s0 = SCC_READ0();
			if (s0 & ZSRR0_CTS) {
				zss->sl_rr0 |= ZSRR0_CTS;
				SCC_BIS(15, ZSR15_CTS);
				zss->sl_flags &= ~SF_FLUSH_WQ;
				zsh_setmstat(zs, CS_CTS_UP);
			}
		}
	}

	switch (zss->sl_txstate) {

	case TX_ABORTED:
		/*
		 * Transmitter was hung ... try restarting it.
		 */
		if (zss->sl_flags & SF_FDXPTP) {
			zss->sl_flags |= SF_XMT_INPROG;
			(void) zsh_start(zs, zss);
		} else
			do_flushwq = 1;
		break;

	case TX_ACTIVE:
	case TX_CRC:
		/*
		 * Transmit is hung for some reason. Reset tx interrupt.
		 * Flush transmit fifo by sending an abort command
		 * which also sets the Underrun/EOM latch in WR0 and in
		 * turn generates an External Status interrupt that
		 * will reset the necessary message buffer pointers.
		 * The watchdog timer will cycle again to allow the SCC
		 * to settle down after the abort command.  The next
		 * time through we'll see that the state is now TX_ABORTED
		 * and call zsh_start to grab a new message.
		 */
		if (--zss->sl_wd_count <= 0) {
			SCC_WRITE0(ZSWR0_SEND_ABORT);
			SCC_WRITE0(ZSWR0_RESET_ERRORS);
			SCC_WRITE0(ZSWR0_RESET_TXINT);
			zsh_txbad(zs, zss);
			zss->sl_txstate = TX_ABORTED; /* must be after txbad */
			warning = 1;
		}
		break;

	case TX_RTS:
		/*
		 * Timer expired after we raised RTS.  CTS never came up.
		 */
		zss->sl_st.cts++;

		zsh_setmstat(zs, CS_CTS_TO);
		zss->sl_flags &= ~SF_XMT_INPROG;
		zss->sl_flags |= SF_FLUSH_WQ;
		ZSSETSOFT(zs);
		break;

	default:
		/*
		 * If we time out in an inactive state we set a soft
		 * interrupt.  This will call zsh_start which will
		 * clear SF_XMT_INPROG if the queue is empty.
		 */
		break;
	}
end_watchdog:
	if (zss->sl_txstate != TX_OFF) {
		mutex_exit(zs->zs_excl_hi);
		zss->sl_wd_id = timeout(zsh_watchdog, (caddr_t)zs,
		    SIO_WATCHDOG_TICK);
	} else {
		zss->sl_wd_id = 0;	/* safety */
		mutex_exit(zs->zs_excl_hi);
	}
	if (warning || do_flushwq) {
		flushq(wq, FLUSHDATA);
		mutex_enter(zs->zs_excl_hi);
		if (mp = zss->sl_xstandby)
			zss->sl_xstandby = NULL;
		mutex_exit(zs->zs_excl_hi);
		if (mp)
			freemsg(mp);
	}
	mutex_exit(zs->zs_excl);
	if (warning)
		cmn_err(CE_WARN, "zsh%x: transmit hung", zs->zs_unit);
}

static void
zsh_callback(struct zscom *zs)
{
	register struct syncline *zss = (struct syncline *)&zs->zs_priv_str;
	register int tmp = ZSH_MAX_RSTANDBY;

	mutex_enter(zs->zs_excl);
	if (zss->sl_bufcid) {
		zss->sl_bufcid = 0;
		ZSH_GETBLOCK(zs, tmp);
	}
	mutex_exit(zs->zs_excl);
}
