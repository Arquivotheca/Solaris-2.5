/*
 * Copyright (c) 1993 by Sun Microsystems, Inc.
 */

#ident	"@(#)soc.c  1.29	95/08/25 SMI"

/*
 * SOC - Serial Optical Channel Processor host adapter driver.
 */

#include <sys/types.h>
#include <note.h>
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

#include <sys/errno.h>
#include <sys/open.h>
#include <sys/varargs.h>
#include <sys/debug.h>
#include <sys/cpu.h>
#include <sys/autoconf.h>
#include <sys/conf.h>
#include <sys/stat.h>

#include <sys/map.h>
#include <sys/vmmac.h>
#include <sys/file.h>
#include <sys/syslog.h>

#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/dmaga.h>
#include <sys/ddi_impldefs.h>
#include <sys/ksynch.h>

#ifdef TRACE
#include <sys/vtrace.h>
#endif

#include <sys/socreg.h>
#include <sys/socmap.h>
#include <sys/soc_cq_defs.h>
#include <sys/fc4/linkapp.h>
#include <sys/fc4/fc_transport.h>
#include <sys/socvar.h>

/*
 * Local Macros
 */
#undef DEBUG

#if defined(lint) && !defined(DEBUG)
#define	DEBUG   1
#endif

#ifdef DEBUG
#define	SOC_DEBUG 1
#else
#define	SOC_DEBUG 0
#endif

#if SOC_DEBUG > 0
static	int soc_debug = SOC_DEBUG;
#define	DEBUGF(level, args) \
	if (soc_debug >= (level)) cmn_err args;
#define	SOCDEBUG(level, args) \
	if (soc_debug >= level) args;
#else
#define	DEBUGF(level, args)	/* Nothing */
#define	SOCDEBUG(level, args)	/* Nothing */
#endif

/*
 * Forward declarations
 *
 * Driver Entry points.
 */
static int soc_attach(dev_info_t *dip, ddi_attach_cmd_t cmd);
static int soc_bus_ctl(dev_info_t *dip, dev_info_t *rip,
	ddi_ctl_enum_t o, void *a, void *v);
static int soc_detach(dev_info_t *dip, ddi_detach_cmd_t cmd);
static int soc_dodetach(dev_info_t *dip);
static int soc_getinfo(dev_info_t *dip, ddi_info_cmd_t infocmd,
	void *arg, void **result);
static int soc_identify(dev_info_t *dip);
static int soc_probe(dev_info_t *dip);

/*
 * FC transport functions.
 */
static int soc_transport(fc_packet_t *, fc_sleep_t);
static int soc_reset(fc_packet_t *);
static int soc_doreset(soc_state_t *);
static fc_uc_cookie_t soc_uc_register(void *, fc_devdata_t devdata,
		void (*callback)(void *), void *);
static void soc_uc_unregister(void *, fc_uc_cookie_t);
static int soc_uc_get_pkt(void *, struct fc_packet *);
static fc_packet_t *soc_pkt_alloc(void *, fc_sleep_t);
static int soc_pkt_cache_alloc(soc_state_t *socp, fc_sleep_t);
static void soc_pkt_free(void *, fc_packet_t *);

/*
 * Driver internal functions.
 */
static int soc_cqalloc_init(soc_state_t *, int);
static void soc_cqinit(soc_state_t *, int);
static void soc_disable(soc_state_t *);
static int soc_dopoll(soc_state_t *, fc_packet_t *);
static int soc_do_online(soc_port_t *);
static void soc_download_fw(soc_state_t *);
static void soc_enable(soc_state_t *);
#ifdef	UNSOLICITED_POOLS
static int soc_establish_pools(soc_state_t *);
#endif	UNSOLICITED_POOLS
static void soc_init_cq_desc(soc_state_t *);
static void soc_init_transport_interface(soc_state_t *);
static unsigned int soc_intr(caddr_t arg);
static void soc_intr_solicited(soc_state_t *);
static void soc_intr_unsolicited(soc_state_t *);
static int soc_login_alloc(soc_state_t *, int);
static int soc_login(soc_state_t *, int, int, int);
static void soc_login_retry(soc_port_t *);
static void soc_login_done(fc_packet_t *);
static void soc_login_recovery(soc_port_t *);
static void soc_login_rvy_timeout(caddr_t);
static void soc_login_timeout(caddr_t);
static int soc_login_check(soc_port_t *, la_logi_t *);
static void soc_make_fabric_login(soc_request_t *,
	la_logi_t *, fc_pkt_extended_t *, int);
static void soc_make_nport_login(soc_state_t *, soc_request_t *,
	la_logi_t *, fc_pkt_extended_t *, int);
static int soc_start(soc_state_t *);
static void soc_start_ports(soc_state_t *);
static void soc_start_login(soc_state_t *, int);
static fc_statec_cookie_t soc_statec_register(void *,
	void (*)(void *, fc_statec_t), void *);
static void soc_statec_unregister(void *, fc_statec_cookie_t);
static void soc_interface_poll(void *);
static void soc_us_els(soc_state_t *, cqe_t *, cqe_t *);
static int soc_force_offline(soc_port_t *, int);
static void soc_force_offline_done(struct fc_packet *);

/*
 * SOC Circular Queue Management routines.
 */
static int soc_cq_enque(soc_state_t *, soc_port_t *, cqe_t *, int, fc_sleep_t,
			fc_pkt_extended_t *, int);

/*
 * Utility functions
 */
static void soc_hcopy(u_short *, u_short *, int);
static void soc_disp_err(soc_state_t *, u_int level, char *mid, char *msg);

#ifdef SOC_UDUMP
static void soc_udump(soc_state_t *);
static void soc_udump_watch();
#endif SOC_UDUMP

#ifdef DEBUG
static void soc_debug_soc_header(soc_header_t *);
static void soc_debug_cq_header(cq_hdr_t *);
static void soc_debug_fc_frame_header(fc_frame_header_t *);
static void soc_debug_soc_response(soc_response_t *);
static void soc_debug_soc_status(u_int);
#endif DEBUG


/*
 * Set this variable to kill host adapter reset recovery
 */
static	int soc_disable_reset = 0;

/*
 * This may be set in /etc/system to disable checking of worldwide names
 * after a port offline/online
 */
static	int soc_disable_wwn_check = 0;

/*
 * Set this variable to enable mutex statistics in this driver
 */
#ifdef	SOC_LOCK_STATS
static	int soc_lock_stats = 1;
extern	int lock_stats;
#endif	SOC_LOCK_STATS

/*
 * Locations used for managing soc microcode image dumps (for debugging)
 */
#ifdef SOC_UDUMP
static	caddr_t soc_udump_buf;
static	int soc_udump_p = -1;
static	int soc_udump_id;
static	int soc_udump_tick;
#endif SOC_UDUMP

/*
 * Used to record poll times for faster booting
 */
static	int soc_init_time;

/*
 * Default soc dma limits
 */

static ddi_dma_lim_t default_soclim = {
	(u_long) 0, (u_long) 0xffffffff, (u_int) ((1<<24)-1),
	DEFAULT_BURSTSIZE | BURST32 | BURST64, 1, (25*1024)
};

/*
 * Table used for setting the burst size in the soc config register
 */
static int soc_burst32_table[] = {
	SOC_CR_BURST_4,
	SOC_CR_BURST_4,
	SOC_CR_BURST_4,
	SOC_CR_BURST_4,
	SOC_CR_BURST_16,
	SOC_CR_BURST_32,
	SOC_CR_BURST_64
};

/*
 * Tables used to define the sizes of the Circular Queues
 *
 * To conserve DVMA/IOPB space, we make some of these queues small...
 */
static int soc_req_entries[] = {
	SOC_SMALL_CQ_ENTRIES,		/* Error (offline) requests */
	SOC_MAX_REQ_CQ_ENTRIES,		/* Most commands */
	0,				/* Not currently used */
	0				/* Not currently used */
};

static int soc_rsp_entries[] = {
	SOC_MAX_RSP_CQ_ENTRIES,		/* Solicited responses */
	SOC_SMALL_CQ_ENTRIES,		/* Unsolicited responses */
	0,				/* Not currently used */
	0				/* Not currently used */
};

/*
 * Bus ops vector
 */

static struct bus_ops soc_bus_ops = {
	BUSO_REV,		/* rev */
	nullbusmap,		/* int (*bus_map)() */
	0,			/* ddi_intrspec_t (*bus_get_intrspec)(); */
	0,			/* int (*bus_add_intrspec)(); */
	0,			/* void	(*bus_remove_intrspec)(); */
	i_ddi_map_fault,	/* int (*bus_map_fault)() */
	ddi_dma_map,		/* int (*bus_dma_map)() */
	ddi_dma_allochdl,
	ddi_dma_freehdl,
	ddi_dma_bindhdl,
	ddi_dma_unbindhdl,
	ddi_dma_flush,
	ddi_dma_win,
	ddi_dma_mctl,		/* int (*bus_dma_ctl)() */
	soc_bus_ctl,		/* int (*bus_ctl)() */
	ddi_bus_prop_op,	/* int (*bus_prop_op*)() */
};

/*
 * Soc driver ops structure.
 */

static struct dev_ops soc_ops = {
	DEVO_REV,		/* devo_rev, */
	0,			/* refcnt */
	soc_getinfo,		/* get_dev_info */
	soc_identify,		/* identify */
	soc_probe,		/* probe */
	soc_attach,		/* attach */
	soc_detach,		/* detach */
	nodev,			/* reset */
	(struct cb_ops *)0,	/* driver operations */
	&soc_bus_ops		/* bus operations */
};

/*
 * Driver private variables.
 */

static void *soc_soft_state_p = NULL;
static ddi_dma_lim_t *soclim = NULL;

/*
 * Warlock directives
 */
#if SOC_DEBUG > 0
/* READ-ONLY VARIABLES: soc_debug */
#endif

_NOTE(READ_ONLY_DATA(soc_disable_reset))
_NOTE(READ_ONLY_DATA(soc_disable_wwn_check))
_NOTE(READ_ONLY_DATA(default_soclim))

#ifdef	SOC_LOCK_STATS
/* READ-ONLY VARIABLES: soc_lock_stats */
#endif	SOC_LOCK_STATS

#ifdef SOC_UDUMP
_NOTE(SCHEME_PROTECTS_DATA("write only in attach", soc_udump_buf))
_NOTE(SCHEME_PROTECTS_DATA("safe sharing", soc_udump_p))
_NOTE(SCHEME_PROTECTS_DATA("single thread user", soc_udump_id))
_NOTE(SCHEME_PROTECTS_DATA("safe sharing", soc_udump_tick))
#endif SOC_UDUMP

_NOTE(READ_ONLY_DATA(default_soclim))
_NOTE(READ_ONLY_DATA(soc_burst32_table))
_NOTE(READ_ONLY_DATA(soc_req_entries))
_NOTE(READ_ONLY_DATA(soc_rsp_entries))
_NOTE(SCHEME_PROTECTS_DATA("ddi routines protect this", soc_soft_state_p))
_NOTE(SCHEME_PROTECTS_DATA("safe sharing", soclim))

_NOTE(SCHEME_PROTECTS_DATA("safe sharing", la_logi))
_NOTE(SCHEME_PROTECTS_DATA("safe sharing", soc_request))
_NOTE(SCHEME_PROTECTS_DATA("safe sharing", fc_packet_extended::fpe_pkt))
_NOTE(SCHEME_PROTECTS_DATA("safe sharing", fc_packet::fc_pkt_status))
_NOTE(SCHEME_PROTECTS_DATA("safe sharing", fc_packet::fc_pkt_flags))

_NOTE(SCHEME_PROTECTS_DATA("safe sharing", _socreg_::soc_csr))
_NOTE(MUTEX_PROTECTS_DATA(soc_state::k_imr_mtx, _socreg_::soc_imr))
_NOTE(SCHEME_PROTECTS_DATA("safe sharing", _socreg_::soc_cr))
_NOTE(SCHEME_PROTECTS_DATA("safe sharing", _socreg_::soc_sae))

_NOTE(SCHEME_PROTECTS_DATA("safe sharing", soc_state::xport))

_NOTE(SCHEME_PROTECTS_DATA("safe sharing", soc_init_time))

/*
 * This is the loadable module wrapper: "module configuration section".
 */

#include <sys/modctl.h>
extern struct mod_ops mod_driverops;

/*
 * Module linkage information for the kernel.
 */

static struct modldrv modldrv = {
	&mod_driverops,		/* Type of module.  This one is a driver */
	"SOC Fibre Channel Host Adapter Driver",
	&soc_ops,		/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modldrv, NULL
};

int	_init(void);
int	_fini(void);
int	_info(struct modinfo *modinfop);

/*
 * This is the module initialization/completion routines
 */

static char soc_initmsg[] = "soc _init: soc.c\t1.29\t95/08/25\n";

int
_init(void)
{
	int stat;

	DEBUGF(1, (CE_CONT, soc_initmsg));

	/* Allocate soft state.  */
	stat = ddi_soft_state_init(&soc_soft_state_p,
		sizeof (soc_state_t), MAX_NSOC);
	if (stat != 0)
		return (stat);

	/* Install the module */
	stat = mod_install(&modlinkage);
	if (stat != 0)
		ddi_soft_state_fini(&soc_soft_state_p);

	DEBUGF(4, (CE_CONT, "soc: _init: return=%d\n", stat));
	return (stat);
}

int
_fini(void)
{
	int stat;

	if ((stat = mod_remove(&modlinkage)) != 0)
		return (stat);

	DEBUGF(1, (CE_CONT, "soc: _fini: \n"));

	ddi_soft_state_fini(&soc_soft_state_p);

	DEBUGF(4, (CE_CONT, "soc: _fini: return=%d\n", stat));
	return (stat);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}


/*
 * SOC Entry points for Autoconfiguration.
 */


/*
 * static int
 * soc_attach() -  this routine initializes the soc to fiber channel node
 *	communcication.  It also performs the device attach initialization.
 *	It loads the SOC firmware and starts the SOC executing.
 *	Attach will cause the SOC driver to perform logins.  If both fabric
 *	logins succeed currently, attach will return DDI_FAILURE for now.
 *
 *	Returns:	DDI_SUCCESS, if able to attach properly to SOC.
 *			DDI_FAILURE, if unable to attach.
 */

static int
soc_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	int 			instance;
	int 			i, j;
	soc_state_t		*socp;
	char			*wwn = NULL;
	char			*portids = NULL;
	int			length = 0;
	soc_port_t		*portp0, *portp1;
	auto char		buf[SOC_PATH_NAME_LEN];
	char			*cptr;
#ifdef	SOC_LOCK_STATS
	int			lock_temp;
#endif	SOC_LOCK_STATS

	instance = ddi_get_instance(dip);
	DEBUGF(2, (CE_CONT, "soc%d: attach: cmd=0x%x dip=0x%x attach=0x%x\n",
		instance, cmd, (int)dip, (int)soc_attach));

	if (cmd != DDI_ATTACH) {
		return (DDI_FAILURE);
	}

	/* If we are in a slave-slot, then we can't be used. */
	if (ddi_slaveonly(dip) == DDI_SUCCESS) {
		cmn_err(CE_WARN,
			"%s soc%d attach failed: device in slave-only slot",
		    "ID[SUNWssa.soc.attach.4001]", instance);
		return (DDI_FAILURE);
	}

	if (ddi_intr_hilevel(dip, 0)) {
		/*
		 * Interrupt number '0' is a high-level interrupt.
		 * At this point you either add a special interrupt
		 * handler that triggers a soft interrupt at a lower level,
		 * or - more simply and appropriately here - you just
		 * fail the attach.
		 */
		cmn_err(CE_WARN,
		"%s soc%d attach failed: hilevel interrupt unsupported",
			"ID[SUNWssa.soc.attach.4002]", instance);
		return (DDI_FAILURE);
	}

	/* Allocate soft state. */
	if (ddi_soft_state_zalloc(soc_soft_state_p, instance) != DDI_SUCCESS) {
		cmn_err(CE_WARN, "%s soc%d attach failed: alloc soft state",
			"ID[SUNWssa.soc.attach.4003]", instance);
		return (DDI_FAILURE);
	}

	/*
	 * Initialize the state structure.
	 */
	socp = ddi_get_soft_state(soc_soft_state_p, instance);
	if (socp == (soc_state_t *)NULL) {
		cmn_err(CE_WARN, "%s soc%d attach failed: bad soft state",
			"ID[SUNWssa.soc.attach.4004]", instance);
		return (DDI_FAILURE);
	}
	DEBUGF(1, (CE_CONT, "soc%d: attach: soc soft state ptr=0x%x \n",
		instance, (int)socp));

	socp->dip = dip;
	soclim = &default_soclim;
	portp0 = &socp->port_status[0];
	portp1 = &socp->port_status[1];

	/* Get the full path name for displaying error messages */
	cptr = ddi_pathname(dip, buf);
	strcpy(socp->soc_name, cptr);
	/* after this point, we can call soc_disp_error instead of cmn_err */

	/*
	 * Get this SOC's world-wide name.
	 */
	if ((ddi_getlongprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS, "soc-wwn",
			(caddr_t)&wwn, &length) == DDI_PROP_SUCCESS) &&
			(length == sizeof (la_wwn_t))) {
		bcopy(wwn, (caddr_t)&socp->soc_ww_name, length);
		kmem_free((void *)wwn, length);
	} else {
		bcopy("1122334455667788",
			(caddr_t)&socp->soc_ww_name, sizeof (la_wwn_t));
	}

	/*
	 * Get source nport id's.
	 */
	if ((ddi_getlongprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS, "sids",
			(caddr_t)&portids, &length) == DDI_PROP_SUCCESS) &&
			(length == 2 * sizeof (int))) {
		portp0->sp_src_id = portids[0];
		portp1->sp_src_id = portids[1];
		kmem_free((void *)portids, length);
	} else {
		portp0->sp_src_id = 0x1;
		portp1->sp_src_id = 0x11;
	}

	/*
	 * Get destination nport id's.
	 */
	if ((ddi_getlongprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS, "dids",
			(caddr_t)&portids, &length) == DDI_PROP_SUCCESS) &&
			(length == 2 * sizeof (int))) {
		portp0->sp_dst_id = portids[0];
		portp1->sp_dst_id = portids[1];
		kmem_free((void *)portids, length);
	} else {
		portp0->sp_dst_id = 0x2;
		portp1->sp_dst_id = 0x12;
	}


	/*
	 * Initialize other fields in the port structures
	 */
	portp0->state_cb = NULL;
	portp1->state_cb = NULL;
	portp0->sp_port = 0;
	portp1->sp_port = 1;
	portp0->sp_state = socp;
	portp1->sp_state = socp;

	if (soc_login_alloc(socp, 0) != DDI_SUCCESS)
		goto fail;
	if (soc_login_alloc(socp, 1) != DDI_SUCCESS)
		goto fail;

	/*
	 * Map the external ram and registers for SOC.
	 * Note: Soc sbus host adapter provides 3 register definition
	 * but Sunfire on-board Soc has only one register definition.
	 */

	if ((ddi_dev_nregs(dip, &i) == DDI_SUCCESS) && (i == 1)) {
		/* Map XRAM */
		if (ddi_map_regs(dip, 0, &socp->socxrp, 0, 0) != DDI_SUCCESS) {
			socp->socxrp = NULL;
			soc_disp_err(socp, CE_WARN, "attach.4020",
				"attach failed: unable to map XRAM");
			goto fail;
		}
		/* Map registers */
		socp->socrp = (soc_reg_t *)(socp->socxrp + SOC_XRAM_SIZE);
	} else {
		/* Map EEPROM */
		if (ddi_map_regs(dip, 0, &socp->soc_eepromp, 0, 0) !=
		    DDI_SUCCESS) {
			socp->soc_eepromp = NULL;
			soc_disp_err(socp, CE_WARN, "attach.4010",
				"attach failed: unable to map eeprom");
			goto fail;
		}
		/* Map XRAM */
		if (ddi_map_regs(dip, 1, &socp->socxrp, 0, 0) != DDI_SUCCESS) {
			socp->socxrp = NULL;
			soc_disp_err(socp, CE_WARN, "attach.4020",
				"attach failed: unable to map XRAM");
			goto fail;
		}
		/* Map registers */
		if (ddi_map_regs(dip, 2, (caddr_t *)&socp->socrp, 0, 0) !=
			DDI_SUCCESS) {
			socp->socrp = NULL;
			soc_disp_err(socp, CE_WARN, "attach.4030",
				"attach failed: unable to map registers");
			goto fail;
		}
	}

	DEBUGF(1, (CE_CONT, "soc%d: attach: SOC XRAM: 0x%x SOC REG: 0x%x\n",
		instance, (int)socp->socxrp, (int)socp->socrp));

	/*
	 * Check to see we really have a SOC Host Adapter card installed
	 */
	if (ddi_peekl(dip, (long *)&socp->socrp->soc_csr.w, (long *)0) !=
		DDI_SUCCESS) {
		soc_disp_err(socp, CE_WARN, "attach.4040",
			"attach failed: unable to access status register");
		goto fail;
	}
	if (ddi_peeks(dip, (short *)&socp->socxrp, (short *)0) != DDI_SUCCESS) {
		soc_disp_err(socp, CE_WARN, "attach.4050",
			"attach failed: unable to access host adapter XRAM");
		goto fail;
	}

	/* now that we have our registers mapped make sure soc reset */
	soc_disable(socp);	/* reset the soc */

	/* Point to the SOC XRAM CQ Descriptor locations. */
	socp->xram_reqp = (soc_cq_t *)(socp->socxrp + SOC_XRAM_REQ_DESC);
	socp->xram_rspp = (soc_cq_t *)(socp->socxrp + SOC_XRAM_RSP_DESC);

	/*
	 * Install the interrupt routine.
	 */
	if (ddi_add_intr(dip,
		(u_int)0,
		&socp->iblkc,
		&socp->idevc,
		soc_intr,
		(caddr_t)socp) != DDI_SUCCESS) {
		    soc_disp_err(socp, CE_WARN, "attach.4060",
			"attach failed: unable to install interrupt handler");
		    goto fail;
	}
	DEBUGF(4, (CE_CONT, "soc%d: attach: add_intr: success\n", instance));

#ifdef	SOC_LOCK_STATS
	lock_temp = lock_stats;
	lock_stats |= soc_lock_stats;
#endif	SOC_LOCK_STATS

	mutex_init(&portp0->sp_mtx, "soc port 0 mutex",
			MUTEX_DRIVER, socp->iblkc);
	mutex_init(&portp1->sp_mtx, "soc port 1 mutex",
			MUTEX_DRIVER, socp->iblkc);
#ifdef	SOC_LOCK_STATS
	lock_stats = lock_temp;
#endif	SOC_LOCK_STATS

	socp->port_mtx_init = 1;

	/*
	 * Initailize the FC transport interface.
	 */
	soc_init_transport_interface(socp);

	/*
	 * TBD
	 * We have now allocated packet caches so must release
	 * if attach fails.
	 */

	/*
	 * Allocate request and response queues and init their mutexs.
	 */
	for (i = 0; i < N_CQS; i++) {
		if (soc_cqalloc_init(socp, i) != DDI_SUCCESS) {
			goto fail;
		}
	}

	/*
	 * Get the configuration register property.  This property is
	 * needed for when resetting the soc configuration register.
	 *
	 * Default value is 32 byte bursts
	 */
	socp->cfg = ddi_getprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
		"soc-config", SOC_CR_BURST_32);

	/*
	 * Adjust the burst size we'll use.
	 */
	j = ddi_dma_burstsizes(socp->request[0].skc_dhandle);
	for (i = 0; soc_burst32_table[i] != SOC_CR_BURST_64; i++)
	    if (!(j >>= 1)) break;
	socp->cfg = (socp->cfg & ~SOC_CR_SBUS_BURST_SIZE_MASK) |
			soc_burst32_table[i];

#ifdef	SOC_UDUMP
	if (!soc_udump_tick) {
	    soc_udump_buf = kmem_zalloc(SOC_XRAM_SIZE, KM_SLEEP);
	    soc_udump_tick = drv_usectohz((clock_t)4000000);
	    soc_udump_id = timeout(soc_udump_watch, (caddr_t)0, soc_udump_tick);
	}
#endif	SOC_UDUMP

	/*
	 * Set up structures to allow us to perform error recovery
	 */
	portp0->sp_offline = (struct fc_packet_extended *)
				soc_pkt_alloc((void *)portp0, FC_SLEEP);
	portp1->sp_offline = (struct fc_packet_extended *)
				soc_pkt_alloc((void *)portp1, FC_SLEEP);

	if (!portp0->sp_offline ||
	    !portp1->sp_offline) {
		soc_disp_err(socp, CE_WARN, "attach.4070",
		    "attach failed: could not alloc offline packet structure");
		goto fail;
	}
	portp0->sp_offline->cmd_state = FC_CMD_COMPLETE;
	portp1->sp_offline->cmd_state = FC_CMD_COMPLETE;

	/*
	 * Time to initialize the soc
	 * and start a login session.
	 */
	if (soc_start(socp) != DDI_SUCCESS)
		goto fail;

	ddi_set_driver_private(dip, (caddr_t)socp);
	ddi_report_dev(dip);

	DEBUGF(2, (CE_CONT,
	"soc%d: attach: &port_status[0]: 0x%x &xport[0]: 0x%x\n",
		instance, (int)&socp->port_status[0], (int)&socp->xport[0]));
	DEBUGF(2, (CE_CONT, "soc%d: attach O.K.\n\n", instance));

	return (DDI_SUCCESS);

fail:
	/* NOTE: we only goto fail if soft state allocated so no check */
	DEBUGF(1, (CE_CONT, "soc%d: attach: DDI_FAILURE\n", instance));

	/* Make sure soc reset */
	soc_disable(socp);

	/* let detach do the dirty work */
	(void) soc_dodetach(dip);

	return (DDI_FAILURE);
}


/*
 * soc_bus_ctl() - this routine performs the nexus bus function for the SOC.
 */

static int
soc_bus_ctl(dev_info_t *dip, dev_info_t *rip,
	ddi_ctl_enum_t op, void *a, void *v)
{
	int instance = ddi_get_instance(dip);
	char c;
	la_wwn_t *cptr;
	int wwn_hi, wwn_lo;
	int ssa_port_id;
	soc_priv_cmd_t	*privp;

	DEBUGF(2, (CE_CONT,
		"soc%d: bus_ctl: dip=0x%x rip=0x%x op=0x%x arg=0x%x\n",
		instance, (int)dip, (int)rip, (int)op, (int)a));

	switch (op) {
	case DDI_CTLOPS_REPORTDEV:
		/*
		 * Log text identifying this driver (d) & its child (r)
		 */
		cmn_err(CE_CONT, "?%s%d at %s%d: soc_port %d\n",
			ddi_get_name(rip), ddi_get_instance(rip),
			ddi_get_name(dip), ddi_get_instance(dip),
			ddi_getprop(DDI_DEV_T_ANY, rip, DDI_PROP_DONTPASS,
				"soc_port", -1));
		break;

	case DDI_CTLOPS_INITCHILD: {
		dev_info_t	*child_dip = (dev_info_t *)a;
		char		name[MAXNAMELEN];
		soc_state_t	*socp;
		int		soc_port;

		DEBUGF(2, (CE_CONT, "soc%d: bus_ctl: INITCHILD %s\n",
			instance, ddi_get_name(child_dip)));

		socp = (soc_state_t *)ddi_get_driver_private(dip);
		if (!socp)
			return (DDI_FAILURE);

		DEBUGF(2, (CE_CONT, "soc%d: soc_bus_ctl: socp=0x%x\n",
			instance, (int)socp));

		soc_port = ddi_getprop(DDI_DEV_T_ANY, child_dip,
			DDI_PROP_DONTPASS, "soc_port", -1);

		if (soc_port < 0 || soc_port > 1) {
			return (DDI_NOT_WELL_FORMED);
		}

		/*
		 * Check for duplicate INITCHILD.  We do this because
		 * there may be entries generated by both the OBP-built
		 * device tree and from the .conf file, and only
		 * one of these must be allowed to live.  Note that
		 * our processing of this is crude, and should really
		 * be more of the type performed in
		 * i_impl_ddi_sunbus_initchild() (for children of the
		 * sbus, etc. nexus).
		 */
		if (socp->port_status[soc_port].sp_child_state !=
			SOC_CHILD_UNINIT)
		    return (DDI_FAILURE);

		/*
		 * Make sure we've made a good attempt at bringing
		 * the port up
		 */
		soc_start_ports(socp);

		/*
		 * Only init-child if this port successfully did
		 * a n-port login.
		 */
		if (!(socp->port_status[soc_port].sp_status &
			NPORT_LOGIN_SUCCESS)) {
			DEBUGF(1, (CE_CONT,
				"soc%d: Port %d not logged in\n",
				instance, soc_port));
			return (DDI_FAILURE);
		}


		ddi_set_driver_private(child_dip,
			(caddr_t)&socp->xport[soc_port]);
		socp->busy++;

		/*
		 * Define name for this child
		 *
		 * Store 48 bits of the Pluto World Wide Name
		 * in ASCII format.  The port on the soc translates
		 * into a leading 'a' for port 0 and a 'b' for port 1.
		 */
		c = (soc_port) ? 'b' : 'a';
		cptr = &socp->port_status[soc_port].sp_d_wwn;
		wwn_hi = cptr->w.wwn_hi;
		wwn_lo = cptr->w.wwn_lo;

		/* Fake up a world wide name, if necessary */
		if (!wwn_hi && !wwn_lo) {
		    soc_disp_err(socp, CE_WARN, "wwn.3010",
			"No SSA World Wide Name, using defaults");
		    if (soc_port) {
			wwn_hi = 0x0000;
			wwn_lo = 0x00000001;
		    } else {
			wwn_hi = 0x0000;
			wwn_lo = 0x00000000;
		    }
		}

		privp = (soc_priv_cmd_t *)socp->port_status[soc_port].sp_login
			    ->fpe_pkt.fc_pkt_private;

		ssa_port_id = ((la_logi_t *)privp->resp)->nport_ww_name
				.w.nport_id;
		if (ddi_prop_create(DDI_DEV_T_NONE, child_dip,
		    DDI_PROP_CANSLEEP, "soc-remote-port-id",
		    (caddr_t)&ssa_port_id, sizeof (ssa_port_id)) !=
		    DDI_PROP_SUCCESS) {
		    soc_disp_err(socp, CE_WARN, "wwn.3030",
			"Could not create soc-remote-port-id property");
		}

		/*
		 * Truncate the leading zeros after the ","
		 * in order to conform to the Open Boot Prom format.
		 */
		sprintf((char *)name, "%c000%04x,%x", c, wwn_hi, wwn_lo);

		DEBUGF(2, (CE_CONT, "soc%d: bus_ctl: Child WWN %s\n",
			instance, name));

		ddi_set_name_addr(child_dip, name);

		DEBUGF(2, (CE_CONT,
		    "soc%d: bus_ctl: Did INITCHILD for soc_port %d\n",
		    instance, soc_port));

		socp->port_status[soc_port].sp_child_state = SOC_CHILD_INIT;

		break;
	}

	case DDI_CTLOPS_UNINITCHILD: {
		dev_info_t	*child_dip = (dev_info_t *)a;
		soc_state_t	*socp;

		socp = (soc_state_t *)ddi_get_driver_private(dip);
		DEBUGF(2, (CE_CONT, "soc%d: bus_ctl: UNINITCHILD\n", instance));

		ddi_set_driver_private(child_dip, NULL);
		(void) ddi_set_name_addr(child_dip, NULL);
		socp->busy--;
		break;
	}

	/*
	 * I've shamelessly lifted the code handling IOMIN
	 * from esp.c, including Matt's original comments.
	 */
	case DDI_CTLOPS_IOMIN: {
		/*
		 * MJ: we need to do something with this that is more
		 * MJ: intelligent than what we are doing.
		 */
		int val;

		DEBUGF(3, (CE_CONT, "soc%d: bus_ctl: IOMIN a=0x%x v=0x%x\n",
			instance, (int)a, *(int *)v));

		val = *((int *)v);

		val = maxbit(val, soclim->dlim_minxfer);
		/*
		 * The 'arg' value of nonzero indicates 'streaming' mode.
		 * If in streaming mode, pick the largest of our burstsizes
		 * available and say that that is our minimum value (modulo
		 * what minxfer is).
		 */
		if ((int)a) {
			val = maxbit(val,
			    1<<(ddi_fls(soclim->dlim_burstsizes)-1));
		} else {
			val = maxbit(val,
			    1<<(ddi_ffs(soclim->dlim_burstsizes)-1));
		}

		*((int *)v) = val;
		return (ddi_ctlops(dip, rip, op, a, v));
	}

	/*
	 * These ops are not available on this nexus.
	 */

	case DDI_CTLOPS_DMAPMAPC:
	case DDI_CTLOPS_REGSIZE:
	case DDI_CTLOPS_NREGS:
	case DDI_CTLOPS_NINTRS:
	case DDI_CTLOPS_AFFINITY:
	case DDI_CTLOPS_POKE_INIT:
	case DDI_CTLOPS_POKE_FLUSH:
	case DDI_CTLOPS_POKE_FINI:
	case DDI_CTLOPS_INTR_HILEVEL:
	case DDI_CTLOPS_XLATE_INTRS:
	case DDI_CTLOPS_SIDDEV:
		return (DDI_FAILURE);

	case DDI_CTLOPS_SLAVEONLY:
	case DDI_CTLOPS_REPORTINT:
	default:
		/*
		 * Remaining requests get passed up to our parent
		 */
		DEBUGF(2, (CE_CONT, "%s%d: op (%d) from %s%d\n",
			ddi_get_name(dip), ddi_get_instance(dip),
			op, ddi_get_name(rip), ddi_get_instance(rip)));
		return (ddi_ctlops(dip, rip, op, a, v));
	}

	return (DDI_SUCCESS);
}

/*
 * static int
 * soc_detach() - this is the detach entry point.  It deallocates the
 *	soft state associated with the "dip" and shutsdown the "instance"
 *	the soc.
 *
 * Note:  detach operations are not currently supported.
 */

/* ARGSUSED */
static int
soc_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	if (cmd != DDI_DETACH)
		return (DDI_FAILURE);

#ifdef	SOC_DETACH
	return (soc_dodetach(dev_info_t *dip);
#else	SOC_DETACH
	return (DDI_FAILURE);
#endif	SOC_DETACH
}

/*
 * soc_dodetach() - this does the real work of a detach.  It may be
 *	called to clean up if soc_attach() fails.
 */
static int
soc_dodetach(dev_info_t *dip)
{
	int		instance = ddi_get_instance(dip);
	int		i;
	soc_state_t	*socp;
	soc_port_t	*portp;

	/* Get the soft state struct. */
	if ((socp = ddi_get_soft_state(soc_soft_state_p, instance)) == 0) {
		return (DDI_FAILURE);
	}

	/*
	 * If somebody is still attached to us from above fail
	 * detach.
	 */
	if (socp->busy != 0) {
		return (DDI_FAILURE);
	}

	/* Make sure soc reset */
	soc_disable(socp);

	/*
	 * XXX - need to free the extended fc packet cache
	 */
	DEBUGF(2, (CE_CONT, "soc%d: detach: TBD!! Need to release fc_packets\n",
		instance));


	/*
	 * Release the login resources
	 */
	for (i = 0; i < N_SOC_NPORTS; i++) {
	    portp = &socp->port_status[i];
	    if (portp->sp_login) {
		fc_pkt_extended_t *fc_fcpkt = portp->sp_login;
		soc_priv_cmd_t *privp =
			(soc_priv_cmd_t *)fc_fcpkt->fpe_pkt.fc_pkt_private;

		ddi_dma_free(privp->cmd_handle);
		ddi_iopb_free(privp->cmd);
		ddi_dma_free(privp->resp_handle);
		ddi_iopb_free(privp->resp);
		soc_pkt_free((void *)portp, (fc_packet_t *)fc_fcpkt);
		kmem_free((caddr_t)privp, sizeof (soc_priv_cmd_t));
	    }
	    if (portp->sp_offline)
		soc_pkt_free((void *)portp, (fc_packet_t *)portp->sp_offline);

	    if (socp->port_mtx_init)
		mutex_destroy(&portp->sp_mtx);
	}

	/*
	 * Free request queues, if allocated
	 */
	for (i = 0; i < N_CQS; i++) {
		/* Free the queues and destroy their mutexes. */
		mutex_destroy(&socp->request[i].skc_mtx);
		mutex_destroy(&socp->response[i].skc_mtx);

		if (socp->request[i].skc_cq_raw) {
			ddi_dma_free(socp->request[i].skc_dhandle);
			ddi_iopb_free((caddr_t)socp->request[i].skc_cq_raw);
			socp->request[i].skc_cq_raw = NULL;
			socp->request[i].skc_cq = NULL;
		}
	}

	/*
	 * Free soc data buffer pool
	 */
	if (socp->pool_dhandle) {
		ddi_dma_free(socp->pool_dhandle);
	}
	if (socp->pool) {
		ddi_iopb_free((caddr_t)socp->pool);
	}

	/* release register map's */
	if (socp->soc_eepromp != NULL) {
		ddi_unmap_regs(dip, 0, &socp->soc_eepromp, 0, 0);
	}

	if (socp->socxrp != NULL) {
		ddi_unmap_regs(dip, 1, &socp->socxrp, 0, 0);
	}
	if (socp->socrp != NULL) {
		ddi_unmap_regs(dip, 2, (caddr_t *)&socp->socrp, 0, 0);
	}

	/* remove soc interrupt */
	if (socp->iblkc != (void *)NULL) {
		ddi_remove_intr(dip, (u_int)0, socp->iblkc);
		DEBUGF(2, (CE_CONT,
		"soc%d: detach: Removed SOC interrupt from ddi\n",
		instance));
	}

	ddi_soft_state_free(soc_soft_state_p, instance);

	return (DDI_SUCCESS);
}

/*
 * int
 * soc_getinfo() - Given the device number, return the devinfo
 * 	pointer or the instance number.  Note: this routine must be
 * 	successful on DDI_INFO_DEVT2INSTANCE even before attach.
 */

/*ARGSUSED*/
static int
soc_getinfo(dev_info_t *dip, ddi_info_cmd_t cmd, void *arg, void **result)
{
	int instance = getminor((dev_t)arg);
	soc_state_t *socp;

	DEBUGF(2, (CE_CONT, "soc_getinfo: entering ... cmd = 0x%x\n", cmd));

	switch (cmd) {
	case DDI_INFO_DEVT2DEVINFO:
		socp = ddi_get_soft_state(soc_soft_state_p, instance);
		if (socp)
			*result = socp->dip;
		else
			*result = NULL;
		break;

	case DDI_INFO_DEVT2INSTANCE:
		*result = (void *)instance;
		break;

	default:
		DEBUGF(2, (CE_CONT, "soc_getinfo: exiting ... FAILED\n"));
		return (DDI_FAILURE);
	}

	DEBUGF(4, (CE_CONT, "soc_getinfo: exiting ...\n"));
	return (DDI_SUCCESS);
}

/*
 * static int
 * soc_identify() - this routine attempts to match the soc driver to
 * 	the name property of the device associated with the "dip".
 */

static int
soc_identify(dev_info_t *dip)
{
	char *name = ddi_get_name(dip);

	if ((strcmp(name, "soc") == 0) || strcmp(name, "SUNW,soc") == 0) {
		DEBUGF(2, (CE_CONT, "soc_identify: identified %s\n", name));
		return (DDI_IDENTIFIED);
	}

	DEBUGF(2, (CE_CONT, "soc_identify: not-identified %s\n", name));
	return (DDI_NOT_IDENTIFIED);
}

/*
 * soc_probe()
 *
 */
static int
soc_probe(dev_info_t *dip)
{
	int instance = ddi_get_instance(dip);

	/*
	 * Is device self-identifying?
	 * If No - fail because soc is self identifying -
	 */
	if (ddi_dev_is_sid(dip) != DDI_SUCCESS) {
		cmn_err(CE_WARN, "%s soc%d probe: Not self-identifying",
			"!ID[SUNWssa.soc.driver.4001]", instance);
		return (DDI_PROBE_FAILURE);
	}

	return (DDI_SUCCESS);
}

/*
 * FC Transport functions
 */

/*
 * static int
 * soc_transport() - this routine is the work horse of the driver. It
 * 	queues packets from the upper layers into requests to the
 *	entity attached to the other end of the SOC (e.g. Pluto).
 *
 *	The soc_transport functions assumes that the addresses handed to
 *	it in the pkt are DVMA normalized addresss.
 *
 *	Returns:
 *		FC_TRANSPORT_SUCCESS, if able to get pkt transported.
 *		FC_TRANSPORT_FAILURE, if unable to transport.
 *		FC_TRANSPORT_UNAVAIL, if SOC port is offline
 */

static int
soc_transport(fc_packet_t *fcpkt, fc_sleep_t sleep)
{
	soc_port_t	*port_statusp = (soc_port_t *)fcpkt->fc_pkt_cookie;
	soc_state_t		*socp = port_statusp->sp_state;
	int			instance = ddi_get_instance(socp->dip);
	fc_pkt_extended_t	*fc_fcpkt = (fc_pkt_extended_t *)fcpkt;
	soc_request_t	*sp;
	int		transport_status;
	int		req_q_no = CQ_REQUEST_1;

	/*
	 * If a fabric is present, then we don't have valid ID's yet since
	 * fabrics are not supported yet.
	 */
	if (port_statusp->sp_status & PORT_FABRIC_PRESENT) {
		fcpkt->fc_pkt_flags |= FCFLAG_COMPLETE;
		return (FC_TRANSPORT_FAILURE);
	}

	DEBUGF(3, (CE_CONT, "soc%d: transport: port (%d) now transporting\n",
		instance, port_statusp->sp_port));
	DEBUGF(4, (CE_CONT, "soc%d: transport: ioclass, sleep = %d, %d\n",
		instance, fcpkt->fc_pkt_io_class, sleep));
	DEBUGF(4, (CE_CONT, "soc%d: transport: ioclass = %d\n",
		instance, fcpkt->fc_pkt_io_class));

	/*
	 * Now point to the soc request portion of the fc packet.
	 */
	sp = (soc_request_t *)&fc_fcpkt->fpe_cqe;

	sp->sr_soc_hdr.sh_request_token = (u_int)fcpkt;
	sp->sr_soc_hdr.sh_flags = SOC_FC_HEADER | port_statusp->sp_port;
	sp->sr_soc_hdr.sh_class = 2;

	switch (fcpkt->fc_pkt_io_class) {
	case FC_CLASS_IO_WRITE:
	case FC_CLASS_IO_READ:
		sp->sr_dataseg[0].fc_base = fcpkt->fc_pkt_cmd->fc_base;
		sp->sr_dataseg[0].fc_count = fcpkt->fc_pkt_cmd->fc_count;
		sp->sr_dataseg[1].fc_base = fcpkt->fc_pkt_rsp->fc_base;
		sp->sr_dataseg[1].fc_count = fcpkt->fc_pkt_rsp->fc_count;

		if (fcpkt->fc_pkt_datap != NULL) {
			sp->sr_dataseg[2].fc_base =
				fcpkt->fc_pkt_datap[0]->fc_base;
			sp->sr_dataseg[2].fc_count =
				fcpkt->fc_pkt_datap[0]->fc_count;
			sp->sr_soc_hdr.sh_seg_cnt = 3;
			sp->sr_soc_hdr.sh_byte_cnt =
				fcpkt->fc_pkt_datap[0]->fc_count;
		} else {
			sp->sr_soc_hdr.sh_seg_cnt = 2;
			sp->sr_soc_hdr.sh_byte_cnt = 0;
		}
		if (fcpkt->fc_pkt_io_class == FC_CLASS_IO_WRITE)
		    sp->sr_cqhdr.cq_hdr_type = CQ_TYPE_IO_WRITE;
		else
		    sp->sr_cqhdr.cq_hdr_type = CQ_TYPE_IO_READ;
		break;

	case FC_CLASS_OUTBOUND:
		sp->sr_dataseg[0].fc_base = fcpkt->fc_pkt_cmd->fc_base;
		sp->sr_dataseg[0].fc_count = fcpkt->fc_pkt_cmd->fc_count;
		sp->sr_soc_hdr.sh_byte_cnt = fcpkt->fc_pkt_cmd->fc_count;
		sp->sr_soc_hdr.sh_seg_cnt = 1;
		sp->sr_cqhdr.cq_hdr_type = CQ_TYPE_OUTBOUND;
		break;

	case FC_CLASS_INBOUND:
		sp->sr_dataseg[0].fc_base = fcpkt->fc_pkt_rsp->fc_base;
		sp->sr_dataseg[0].fc_count = fcpkt->fc_pkt_rsp->fc_count;
		sp->sr_soc_hdr.sh_byte_cnt = 0;
		sp->sr_soc_hdr.sh_seg_cnt = 1;
		sp->sr_cqhdr.cq_hdr_type = CQ_TYPE_INBOUND;
		break;

	case FC_CLASS_SIMPLE:
		sp->sr_dataseg[0].fc_base = fcpkt->fc_pkt_cmd->fc_base;
		sp->sr_dataseg[0].fc_count = fcpkt->fc_pkt_cmd->fc_count;
		sp->sr_dataseg[1].fc_base = fcpkt->fc_pkt_rsp->fc_base;
		sp->sr_dataseg[1].fc_count = fcpkt->fc_pkt_rsp->fc_count;
		sp->sr_soc_hdr.sh_byte_cnt = fcpkt->fc_pkt_cmd->fc_count;
		sp->sr_soc_hdr.sh_seg_cnt = 2;
		sp->sr_cqhdr.cq_hdr_type = CQ_TYPE_SIMPLE;
		break;

	case FC_CLASS_OFFLINE:
		sp->sr_soc_hdr.sh_byte_cnt = 0;
		sp->sr_soc_hdr.sh_seg_cnt = 0;
		sp->sr_cqhdr.cq_hdr_type = CQ_TYPE_OFFLINE;
		req_q_no = CQ_REQUEST_0;
		break;

	default:
		soc_disp_err(socp, CE_WARN, "link.4070",
			"!invalid fc_ioclass");
		fcpkt->fc_pkt_flags |= FCFLAG_COMPLETE;
		return (FC_TRANSPORT_FAILURE);
	}

	/*
	 * Fill in FC header.  Note that the next level up will fill
	 * in all but the routing information (i.e., source and dest
	 * IDs).  Things are partitioned this way so that the
	 * FC4 layer (e.g., pln) can have control over when exchanges
	 * are initiated, what exchange IDs to use (in the case of
	 * a continuing exchange), what sort of FC type field to specify, etc.
	 */
	sp->sr_fc_frame_hdr.d_id = port_statusp->sp_dst_id;
	sp->sr_fc_frame_hdr.s_id = port_statusp->sp_src_id;

	/* Fill in the CQ header. */
	sp->sr_cqhdr.cq_hdr_count = 1;
	sp->sr_cqhdr.cq_hdr_flags = 0;
	sp->sr_cqhdr.cq_hdr_seqno = 0;

	SOCDEBUG(2, soc_debug_soc_header(&sp->sr_soc_hdr));
	SOCDEBUG(3, soc_debug_fc_frame_header(&sp->sr_fc_frame_hdr));
	SOCDEBUG(3, soc_debug_cq_header(&sp->sr_cqhdr));

	fc_fcpkt->cmd_state &= ~FC_CMD_COMPLETE;
	fcpkt->fc_pkt_flags &= ~FCFLAG_COMPLETE;

	if ((transport_status =
		soc_cq_enque(socp, port_statusp,
			(cqe_t *)sp,
			req_q_no,
			sleep,
			fc_fcpkt, 0)) != FC_TRANSPORT_SUCCESS) {
		return (transport_status);
	}

	/*
	 * Poll if no interrupt flag set
	 */
	if ((fcpkt->fc_pkt_flags & FCFLAG_NOINTR) != 0) {
		/* */
		return (soc_dopoll(socp, fcpkt));
	}

	return (FC_TRANSPORT_SUCCESS);
}


/*
 * static int
 * soc_reset() - this routine resets the soc and then performs a login
 *	on the behalf of the pln or top level device.
 */

static int
soc_reset(fc_packet_t *fcpkt)
{
	soc_port_t	*port_statusp = (soc_port_t *)fcpkt->fc_pkt_cookie;
	soc_state_t		*socp = port_statusp->sp_state;

	/*
	 * Do the real reset work
	 */
	if (!soc_doreset(socp))
	    return (0);

	/*
	 * Call the completion routine for the reset request
	 */
	((fc_pkt_extended_t *)fcpkt)->cmd_state = FC_CMD_COMPLETE;
	fcpkt->fc_pkt_flags |= FCFLAG_COMPLETE;
	if (fcpkt->fc_pkt_comp)
	    (*fcpkt->fc_pkt_comp)(fcpkt);

	return (1);
}

/*
 * soc_doreset() - this does the actual work of resetting and reinitializing
 *	the soc
 */
static int
soc_doreset(soc_state_t *socp)
{
	register int	i;
	fc_pkt_extended_t	*fpep,
				*overflowh[N_CQS];
	soc_statec_cb_t		*s_cbp;
	soc_port_t		*port_statusp;
	soc_kcq_t		*kcq_req;
	soc_kcq_t		*kcq_rsp;

	/*
	 * Don't do anything if resets are disabled
	 */
	if (soc_disable_reset) {
	    soc_disp_err(socp, CE_WARN, "login.1010",
			"!reset with resets disabled");
	    return (0);
	}

	/*
	 * Acquire all request and response queue mutexes
	 *
	 * Note:  warlock gets confused by variables, so the effectiveness
	 * of its checking is reduced here...
	 */
#ifndef	__lock_lint
	for (i = 0; i < N_CQS; i++) {
#endif	__lock_lint
	    kcq_req = &socp->request[i];
	    kcq_rsp = &socp->response[i];
	    mutex_enter(&kcq_req->skc_mtx);
	    mutex_enter(&kcq_rsp->skc_mtx);
#ifndef	__lock_lint
	}
#endif	__lock_lint

	/*
	 * Shut off all soc interrupts
	 */
	mutex_enter(&socp->k_imr_mtx);
	socp->k_soc_imr = 0;
	socp->socrp->soc_imr = 0;

	/*
	 * Reset the hardware
	 */
	soc_disable(socp);

	/*
	 * Grab the circular queue overflow lists so we can flush them
	 * after releasing the mutexes
	 */
	for (i = 0; i < N_CQS; i++) {
	    soc_kcq_t *kcq = &socp->request[i];
	    overflowh[i] = kcq->skc_overflowh;
	    kcq->skc_overflowh = NULL;
	    kcq->skc_overflowt = NULL;
	    if (kcq->skc_full & SOC_SKC_SLEEP)
		cv_broadcast(&kcq->skc_cv);
	}

	/*
	 * XXX - reclaim any buffer pool buffers issued to the soc
	 * but not returned
	 */

	/*
	 * Reinitialize some stuff
	 */
	for (i = 0; i < N_CQS; i++)
	    soc_cqinit(socp, i);

	/*
	 * Mark all ports offline
	 */
	for (i = 0; i < N_SOC_NPORTS; i++) {
	    port_statusp = &socp->port_status[i];

	    mutex_enter(&port_statusp->sp_mtx);

	    port_statusp->sp_status = PORT_OFFLINE;
	    if (port_statusp->login_timer_running)
		untimeout(port_statusp->login_timeout_id);
	    port_statusp->login_timer_running = 0;
	    port_statusp->sp_login->cmd_state = FC_CMD_COMPLETE;
	    port_statusp->sp_offline->cmd_state = FC_CMD_COMPLETE;

	    mutex_exit(&port_statusp->sp_mtx);
	}
	socp->soc_shutdown = 1;

	/*
	 * Let go of all of the mutexes
	 */
	mutex_exit(&socp->k_imr_mtx);

#ifndef	__lock_lint
	for (i = N_CQS-1; i >= 0; i--) {
#endif	__lock_lint
	    kcq_req = &socp->request[i];
	    kcq_rsp = &socp->response[i];
	    mutex_exit(&kcq_rsp->skc_mtx);
	    mutex_exit(&kcq_req->skc_mtx);
#ifndef	__lock_lint
	}
#endif	__lock_lint

	/*
	 * Clear the circular queue overflow queues
	 */
#ifndef	__lock_lint
	for (i = 0; i < N_CQS; i++)
	    while ((fpep = overflowh[i]) != NULL) {
		fpep->fpe_pkt.fc_pkt_status = FC_STATUS_ERR_OFFLINE;
		fpep->fpe_pkt.fc_frame_resp = NULL;
		fpep->cmd_state = FC_CMD_COMPLETE;
		fpep->fpe_pkt.fc_pkt_flags |= FCFLAG_COMPLETE;
		if (fpep->fpe_pkt.fc_pkt_comp)
		    (*fpep->fpe_pkt.fc_pkt_comp)(&fpep->fpe_pkt);
		overflowh[i] = fpep->fpe_next;
	    }

#endif	__lock_lint

	/*
	 * Let everyone know we've reset the hardware
	 */
	for (i = 0; i < N_SOC_NPORTS; i++)
	    for (s_cbp = socp->port_status[i].state_cb;
		    s_cbp != NULL; s_cbp = s_cbp->next) {
		(*s_cbp->callback)(s_cbp->arg, FC_STATE_RESET);
	    }

	/*
	 * Start up the soc hardware
	 */
	(void) soc_start(socp);
	soc_start_ports(socp);

	return (1);
}

/*
 * soc_uc_register(fc_transport_t *fc, void (*callback)(), void *arg)
 *	this routine implements the asynchronous event registration
 *	function for the fc_transport interface.
 *
 *	Returns:
 *		fc_uc_cookie_t if successful
 *		NULL, not successful.
 *
 * NOTE that this is just a placeholder for a future feature.
 */

/* ARGSUSED */
static fc_uc_cookie_t
soc_uc_register(
	void			*cookie,
	fc_devdata_t		devdata,
	void			(*callback)(void *),
	void			*arg)
{
	soc_port_t		*port_statusp = (soc_port_t *)cookie;

	/* Not yet supported */
	return ((fc_uc_cookie_t)port_statusp->sp_state);
}

/* ARGSUSED */
static void
soc_uc_unregister(
	void			*cookie,
	fc_uc_cookie_t		uc_cookie)
{
#ifndef	lint
	soc_port_t		*port_statusp = (soc_port_t *)cookie;
#endif	lint
}

/*
 * soc_statec_register() - this routine adds a function to be called
 *	in the event of a state change on the interface.
 */

static fc_statec_cookie_t
soc_statec_register(
	void			*cookie,
	void			(*callback)(void *, fc_statec_t),
	void			*arg)
{
	soc_port_t		*port_statusp = (soc_port_t *)cookie;
	soc_statec_cb_t		*cbp, *cbn;

	cbp = kmem_zalloc(sizeof (soc_statec_cb_t), KM_SLEEP);
	if (cbp == NULL)
	    return (NULL);

	cbp->next = NULL;
	cbp->callback = callback;
	cbp->arg = arg;

	cbn = port_statusp->state_cb;
	if (cbn == NULL)
	    port_statusp->state_cb = cbp;
	else {
	    while (cbn->next != NULL)
		cbn = cbn->next;
	    cbn->next = cbp;
	}

	return ((fc_statec_cookie_t)cbp);
}

/*
 * soc_statec_unregister() - this routine deletes a function to be called
 *	in the event of a state change on the interface.
 */

static void
soc_statec_unregister(
	void			*cookie,
	fc_statec_cookie_t	statec_cookie)
{
	soc_port_t		*port_statusp = (soc_port_t *)cookie;
	soc_statec_cb_t		*cbp, *cbn;

	cbn = NULL;
	for (cbp = port_statusp->state_cb; cbp; cbn = cbp, cbp = cbp->next)
	    if (cbp == (soc_statec_cb_t *)statec_cookie) {
		if (cbn)
		    cbn->next = cbp->next;
		else
		    port_statusp->state_cb = cbp->next;
		kmem_free((void *)cbp, sizeof (soc_statec_cb_t));
		return;
	    }
}

/*
 * soc_interface_poll() - this routine allows interrupt processing
 *	to be performed if system interrupts are disabled
 */

static void
soc_interface_poll(
	void			*cookie)
{
	soc_port_t		*port_statusp = (soc_port_t *)cookie;
	soc_state_t		*socp = port_statusp->sp_state;
	register volatile soc_reg_t *socreg = socp->socrp;
	u_int			csr;

	csr = socreg->soc_csr.w;
	if (SOC_INTR_CAUSE(socp, csr)) {
		(void) soc_intr((caddr_t)socp);
	}
}

/*
 * static int
 * soc_uc_get_pkt(fc_transport_t *fc, struct fc_packet *fcpkt)
 *	return next queued unsolicited cmd.
 *
 *	Returns:
 *		-1 for failure
 *		length of the cmd payload, if successful
 */

/* ARGSUSED */
static int
soc_uc_get_pkt(
	void			*cookie,
	struct fc_packet	*fcpkt)
{
	soc_port_t		*port_statusp = (soc_port_t *)cookie;

	soc_disp_err(port_statusp->sp_state, CE_WARN, "driver.4080",
			"!no unsolicited commands to get");
	return (-1);
}

/*
 * static fc_packet_t *
 * soc_pkt_alloc() - this routine implements the fc packet allocation
 * 	for the FC transport interface.
 *
 *	Returns:	the allocated packet.
 */

static fc_packet_t *
soc_pkt_alloc(void *vp, fc_sleep_t sleep)
{
	soc_port_t		*port_statusp = (soc_port_t *)vp;
	soc_state_t		*socp = port_statusp->sp_state;
	fc_pkt_cache_t		*cache = &socp->fc_pkt_cache;
	fc_pkt_extended_t	*fpkt;

	mutex_enter(FC_PKT_CACHE_MUTEX(cache));

	/*
	 * If we have entries in the cache, then grab one from the
	 * cache.  If not, allocate a new block of them and add them
	 * to the cache.
	 */
#ifndef	__lock_lint
	if (FC_PKT_CACHE_FIRST(cache) == NULL) {
		if (soc_pkt_cache_alloc(socp, sleep) != DDI_SUCCESS) {
			mutex_exit(FC_PKT_CACHE_MUTEX(cache));
			return ((fc_packet_t *)NULL);
		}
	}

	fpkt = FC_PKT_CACHE_FIRST(cache);

	if (FC_PKT_CACHE_FIRST(cache) == FC_PKT_CACHE_LAST(cache)) {
		FC_PKT_CACHE_FIRST(cache) = NULL;
		FC_PKT_CACHE_LAST(cache) = NULL;
	} else {
		FC_PKT_CACHE_FIRST(cache) = FC_PKT_NEXT(fpkt);
	}
	FC_PKT_NEXT(fpkt) = NULL;

	DEBUGF(2, (CE_CONT,
		"soc%d: pkt_alloc: Grabbed packet from cache pkt=0x%x\n",
		ddi_get_instance(socp->dip), (int)fpkt));

#endif	__lock_lint
	mutex_exit(FC_PKT_CACHE_MUTEX(cache));

	/*
	 * Fill in pointers to the Fibre Channel headers available
	 * for upper level use
	 */
	fpkt->fpe_pkt.fc_frame_cmd =
		&((soc_request_t *)&fpkt->fpe_cqe)->sr_fc_frame_hdr;
	fpkt->fpe_pkt.fc_frame_resp = NULL;

	return ((fc_packet_t *)fpkt);
}

/*
 * static void
 * soc_pkt_cache_alloc() - this routine adds an increment of extended
 *	fc_packets to the pkt cache.
 */

static int
soc_pkt_cache_alloc(soc_state_t *socp, fc_sleep_t sleep)
{
	fc_pkt_cache_t		*cache;
	fc_pkt_extended_t	*pkt, *pkthi;
	u_int			i;

	cache = &socp->fc_pkt_cache;

	mutex_exit(FC_PKT_CACHE_MUTEX(cache));

	switch (sleep) {
	case FC_SLEEP:
		pkt = kmem_zalloc(FC_PKT_CACHE_INCREMENT *
			sizeof (fc_pkt_extended_t), KM_SLEEP);
		break;

	case FC_NOSLEEP:
		pkt = kmem_zalloc(FC_PKT_CACHE_INCREMENT *
			sizeof (fc_pkt_extended_t), KM_NOSLEEP);
		if (!pkt) {
			mutex_enter(FC_PKT_CACHE_MUTEX(cache));
			return (DDI_FAILURE);
		}
	}

#ifndef	__lock_lint
	for (i = 0; i < FC_PKT_CACHE_INCREMENT; i++) {
		pkt[i].fpe_magic = FPE_MAGIC;
		if (i != (FC_PKT_CACHE_INCREMENT - 1))
			pkt[i].fpe_next = &pkt[i+1];
		else
			pkt[i].fpe_next = NULL;
	}
#endif	__lock_lint

	mutex_enter(FC_PKT_CACHE_MUTEX(cache));

	/* Set up high, low pkt pointers for response validation */
	if (!socp->fc_pkt_lo || (pkt < socp->fc_pkt_lo))
	    socp->fc_pkt_lo = pkt;
	if ((pkthi = pkt + FC_PKT_CACHE_INCREMENT) > socp->fc_pkt_hi)
	    socp->fc_pkt_hi = pkthi;

	FC_PKT_CACHE_FIRST(cache) = pkt;
	FC_PKT_CACHE_LAST(cache) = &pkt[FC_PKT_CACHE_INCREMENT - 1];
	DEBUGF(2, (CE_CONT,
	"soc%d: pkt_cache_alloc: allocated 0x%x packets at 0x%x\n",
	ddi_get_instance(socp->dip), FC_PKT_CACHE_INCREMENT, (int)pkt));

	return (DDI_SUCCESS);
}

/*
 * static void
 * soc_pkt_free() - this routine implements the FC transport interface
 *	fc packet deallocation.  The kernel memory associated is not
 *	freed.  It is placed back in the packet cache.
 */
static void
soc_pkt_free(void *vp, fc_packet_t *fc_fcpkt)
{
	soc_port_t		*port_statusp = (soc_port_t *)vp;
	soc_state_t		*socp = port_statusp->sp_state;
	fc_pkt_cache_t		*cache = &socp->fc_pkt_cache;
	fc_pkt_extended_t	*fpkt = (fc_pkt_extended_t *)fc_fcpkt;

	mutex_enter(FC_PKT_CACHE_MUTEX(cache));


#ifndef	__lock_lint
	if (FC_PKT_CACHE_FIRST(cache) == NULL) {
		FC_PKT_CACHE_FIRST(cache) = fpkt;
	} else {
		FC_PKT_NEXT(FC_PKT_CACHE_LAST(cache)) = fpkt;
	}
	FC_PKT_CACHE_LAST(cache) = fpkt;

	DEBUGF(2, (CE_CONT,
		"soc%d: pkt_free: Returned packet to free list: pkt=0x%x\n",
		ddi_get_instance(socp->dip), (int)fpkt));

#endif	__lock_lint
	mutex_exit(FC_PKT_CACHE_MUTEX(cache));
}

/*
 * Internal Driver Functions
 */

/*
 * static int
 * soc_cqalloc_init() - Inialize the entire Circular queue tables.
 *	Also, init the locks that are associated with the tables.
 *
 *	Returns:	DDI_SUCCESS, if able to init properly.
 *			DDI_FAILURE, if unable to init properly.
 */

static int
soc_cqalloc_init(soc_state_t *socp, int chn)
{
	int result;
	dev_info_t *dip = socp->dip;
	int cq_size;
#ifdef	SOC_LOCK_STATS
	int lock_temp;
#endif	SOC_LOCK_STATS

	/*
	 * Initialize the Request and Response Queue locks.
	 */

#ifdef	SOC_LOCK_STATS
	lock_temp = lock_stats;
	lock_stats |= soc_lock_stats;
#endif	SOC_LOCK_STATS
	mutex_init(&socp->request[chn].skc_mtx, "request.mtx", MUTEX_DRIVER,
		(void *)socp->iblkc);
	mutex_init(&socp->response[chn].skc_mtx, "response.mtx", MUTEX_DRIVER,
		(void *)socp->iblkc);
	cv_init(&socp->request[chn].skc_cv, "request.cv", CV_DRIVER,
		(void *)NULL);
	cv_init(&socp->response[chn].skc_cv, "response.cv", CV_DRIVER,
		(void *)NULL);
#ifdef	SOC_LOCK_STATS
	lock_stats = lock_temp;
#endif	SOC_LOCK_STATS

	/* Allocate DVMA resources for the Request Queue. */
	cq_size = soc_req_entries[chn] * sizeof (cqe_t);
	result = ddi_iopb_alloc(dip, soclim, cq_size + SOC_CQ_ALIGN,
			&socp->request[chn].skc_cq_raw);
	if (result != DDI_SUCCESS) {
		soc_disp_err(socp, CE_WARN, "driver.4020",
			"!alloc of request queue failed");
		mutex_destroy(&socp->request[chn].skc_mtx);
		mutex_destroy(&socp->response[chn].skc_mtx);
		return (DDI_FAILURE);
	}
	socp->request[chn].skc_cq = (cqe_t *)
		    (((u_long)socp->request[chn].skc_cq_raw + SOC_CQ_ALIGN - 1)
			& ((long)(~(SOC_CQ_ALIGN-1))));

	result = ddi_dma_addr_setup(dip, (struct as *)0,
		(caddr_t)socp->request[chn].skc_cq,
		cq_size, DDI_DMA_RDWR | DDI_DMA_CONSISTENT,
		DDI_DMA_DONTWAIT, 0, soclim, &socp->request[chn].skc_dhandle);

	if (!result) {
		result = ddi_dma_htoc(socp->request[chn].skc_dhandle, (off_t)0,
			&socp->request[chn].skc_dcookie);
	}

	if (result) {
		soc_disp_err(socp, CE_WARN, "driver.4040",
			"!DVMA request queue alloc failed");

		if (socp->request[chn].skc_dhandle)
			ddi_dma_free(socp->request[chn].skc_dhandle);

		ddi_iopb_free((caddr_t)socp->request[chn].skc_cq_raw);
		socp->request[chn].skc_cq_raw = NULL;
		socp->request[chn].skc_cq = NULL;
		mutex_destroy(&socp->request[chn].skc_mtx);
		mutex_destroy(&socp->response[chn].skc_mtx);

		return (DDI_FAILURE);
	}

	/*
	 * Initialize the queue pointers
	 */
	soc_cqinit(socp, chn);

	return (DDI_SUCCESS);
}

/*
 * soc_cqinit() - initializes the driver's circular queue pointers, etc.
 */

static void
soc_cqinit(soc_state_t *socp, int chn)
{
	soc_kcq_t *kcq_req = &socp->request[chn];
	soc_kcq_t *kcq_rsp = &socp->response[chn];

	/*
	 * Initialize the Request and Response Queue pointers
	 */
#ifndef	__lock_lint
	/*
	 * The response queues live in the host adapter's xRAM, so we
	 * just need to initialize the circular queue descriptors
	 * for the ucode appropriately.  We'll set the queue address
	 * to "1", which will cause the ucode to allocate a queue for
	 * us during initialization.  Then, the first time we get
	 * a response interrupt, we'll pick up the queue address assigned
	 * by the ucode.
	 */
	kcq_rsp->skc_cq = (cqe_t *)0;

	kcq_req->skc_seqno = 1;
	kcq_rsp->skc_seqno = 1;
	kcq_req->skc_in = 0;
	kcq_rsp->skc_in = 0;
	kcq_req->skc_out = 0;
	kcq_rsp->skc_out = 0;
	kcq_req->skc_last_index = soc_req_entries[chn] - 1;
	kcq_rsp->skc_last_index = soc_rsp_entries[chn] - 1;
	kcq_req->skc_full = 0;
	kcq_req->skc_overflowh = NULL;
	kcq_req->skc_overflowt = NULL;

	kcq_req->skc_xram_cqdesc =
		(socp->xram_reqp + (chn * sizeof (struct cq))/8);
	kcq_rsp->skc_xram_cqdesc =
		(socp->xram_rspp + (chn * sizeof (struct cq))/8);

	/*  Clear out memory we have allocated */
	bzero((caddr_t)kcq_req->skc_cq,
		soc_req_entries[chn] * sizeof (cqe_t));
#endif	__lock_lint
}

/*
 * Function name : soc_disable()
 *
 * Return Values :  none
 *
 * Description	 : Reset the soc
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 *
 * Note:  before calling this, the interface should be locked down
 * so that it is guaranteed that no other threads are accessing
 * the hardware.
 */
static	void
soc_disable(soc_state_t *socp)
{
#ifndef	__lock_lint
	/* Don't touch the hardware if the registers aren't mapped */
	if (!socp->socrp)
		return;

	socp->socrp->soc_imr = socp->k_soc_imr = 0;
	socp->socrp->soc_csr.w = SOC_CSR_SOFT_RESET;
#endif	__lock_lint
}

/*
 * Function name : soc_dopoll()
 *
 * Return Values :
 *		FC_TRANSPORT_SUCCESS
 *		FC_TRANSPORT_TIMEOUT
 *
 * Description	 :
 *		   Busy waits for I/O to complete or timeout.
 *
 * Context	 : Can be called from different kernel process threads.
 */
static int
soc_dopoll(soc_state_t *socp, fc_packet_t *fcpkt)
{
	register volatile soc_reg_t *socreg = socp->socrp;
	register int delay_loops;
	fc_pkt_extended_t	*fc_fcpkt = (fc_pkt_extended_t *)fcpkt;
	u_int			csr;
	int instance = ddi_get_instance(socp->dip);

	/*
	 * busy wait for command to finish ie. till FC_CMD_COMPLETE is set
	 */
	delay_loops = SOC_TIMEOUT_DELAY((fcpkt->fc_pkt_timeout + SOC_GRACE),
	    SOC_NOINTR_POLL_DELAY_TIME);
	DEBUGF(2, (CE_CONT,
		"soc%d: polling command time: %d delay_loops: %d\n",
		instance,
		(int)fcpkt->fc_pkt_timeout, delay_loops));

	while ((fc_fcpkt->cmd_state & FC_CMD_COMPLETE) == 0) {
		drv_usecwait(SOC_NOINTR_POLL_DELAY_TIME);

		if (--delay_loops <= 0) {

			DEBUGF(1, (CE_CONT,
			"soc%d: dopoll: Polled command timeout\n", instance));

			return (FC_TRANSPORT_TIMEOUT);
		}

		/*
		 * Because interrupts can be off during booting
		 * it is necessary to do this recursive call.
		 */
		csr = socreg->soc_csr.w;
		if (SOC_INTR_CAUSE(socp, csr)) {
			(void) soc_intr((caddr_t)socp);
		}
	}

	return (FC_TRANSPORT_SUCCESS);
}

/*
 * Wait for a port to go online
 */
static int
soc_do_online(soc_port_t *port_statusp)
{
	soc_state_t *socp = port_statusp->sp_state;
	register volatile soc_reg_t *socreg = socp->socrp;
	register int delay_loops;
	u_int			csr;

	/*
	 * busy wait for command to finish ie. till FC_CMD_COMPLETE is set
	 */
	delay_loops = SOC_TIMEOUT_DELAY(SOC_ONLINE_TIMEOUT,
					SOC_NOINTR_POLL_DELAY_TIME);

	while (port_statusp->sp_status & (PORT_OFFLINE|PORT_STATUS_FLAG)) {
		drv_usecwait(SOC_NOINTR_POLL_DELAY_TIME);

		if (--delay_loops <= 0) {

			return (FC_TRANSPORT_TIMEOUT);
		}

		/*
		 * Because interrupts can be off during booting
		 * it is necessary to do this recursive call.
		 */
		csr = socreg->soc_csr.w;
		if (SOC_INTR_CAUSE(socp, csr)) {
			(void) soc_intr((caddr_t)socp);
		}
	}

	return (FC_TRANSPORT_SUCCESS);
}

/*
 * static int
 * soc_force_offline() - force a soc port offline
 */
static int
soc_force_offline(soc_port_t *port_statusp, int poll)
{
	fc_pkt_extended_t *fc_fcpkt = port_statusp->sp_offline;
	soc_request_t	*sp = (soc_request_t *)&fc_fcpkt->fpe_cqe;

	/*
	 * Enqueue the soc request structure.
	 */
	fc_fcpkt->fpe_pkt.fc_pkt_timeout = SOC_OFFLINE_TIMEOUT;
	fc_fcpkt->fpe_pkt.fc_pkt_cookie = (void *)port_statusp;
	fc_fcpkt->fpe_pkt.fc_pkt_flags = 0;
	fc_fcpkt->fpe_pkt.fc_pkt_io_class = FC_CLASS_OFFLINE;
	fc_fcpkt->cmd_state = 0;

	sp->sr_soc_hdr.sh_request_token = (u_int)fc_fcpkt;
	sp->sr_soc_hdr.sh_flags = SOC_FC_HEADER | port_statusp->sp_port;
	sp->sr_soc_hdr.sh_byte_cnt = 0;
	sp->sr_soc_hdr.sh_seg_cnt = 0;
	sp->sr_cqhdr.cq_hdr_type = CQ_TYPE_OFFLINE;
	sp->sr_fc_frame_hdr.d_id = port_statusp->sp_dst_id;
	sp->sr_fc_frame_hdr.s_id = port_statusp->sp_src_id;

	/* Fill in the CQ header */
	sp->sr_cqhdr.cq_hdr_count = 1;
	sp->sr_cqhdr.cq_hdr_flags = 0;
	sp->sr_cqhdr.cq_hdr_seqno = 0;

	if (poll)
	    fc_fcpkt->fpe_pkt.fc_pkt_comp = NULL;
	else {
	    mutex_enter(&port_statusp->sp_mtx);
	    port_statusp->login_timer_running = 1;
	    port_statusp->login_timeout_id = timeout(soc_login_rvy_timeout,
			(caddr_t)port_statusp,
			(drv_usectohz)(SOC_OFFLINE_TIMEOUT*10000000));
	    mutex_exit(&port_statusp->sp_mtx);
	    fc_fcpkt->fpe_pkt.fc_pkt_comp = soc_force_offline_done;
	}

	/*
	 * Start the command
	 */
	if (soc_cq_enque(port_statusp->sp_state, NULL,
		&fc_fcpkt->fpe_cqe, CQ_REQUEST_0, FC_NOSLEEP, fc_fcpkt, 0)) {
		fc_fcpkt->cmd_state = FC_CMD_COMPLETE;
		return (DDI_FAILURE);
	}

	/* wait for it to complete */
	if (poll) {
	    if (soc_dopoll(port_statusp->sp_state,
		(fc_packet_t *)&fc_fcpkt->fpe_pkt) != FC_TRANSPORT_SUCCESS) {
		    return (SOC_OFFLINE_TIMEOUT_FLAG);
	    }
	    return (fc_fcpkt->fpe_pkt.fc_pkt_status);
	}
	else
	    return (DDI_SUCCESS);

}

/*
 * soc_force_offline_done() - completion routine for offline recovery
 */
static void
soc_force_offline_done(struct fc_packet *fpkt)
{
	soc_port_t *port_statusp = (soc_port_t *)fpkt->fc_pkt_cookie;

	mutex_enter(&port_statusp->sp_mtx);
	if (port_statusp->sp_login->cmd_state == FC_CMD_COMPLETE) {
	    untimeout(port_statusp->login_timeout_id);
	    port_statusp->login_timer_running = 0;
	}
	mutex_exit(&port_statusp->sp_mtx);
}

/*
 * Firmware related externs
 */
extern int soc_ucode[];
extern int soc_ucode_end;

/*
 * Function name : soc_download_fw ()
 *
 * Return Values :
 *
 * Description	 : Copies firmware from code
 *		   that has been linked into the soc module
 *		   into the soc's XRAM.
 *
 *		   Reads/Writes from/to XRAM must be half word.
 *
 *		   Verifies fw checksum after writing.
 */
static void
soc_download_fw(soc_state_t *socp)
{
	u_int	fw_len = 0;
	u_short	date_str[32];
	auto	char buf[256];
	int instance = ddi_get_instance(socp->dip);

	fw_len = (u_int)&soc_ucode_end - (u_int)&soc_ucode;

	DEBUGF(2, (CE_CONT,
	    "soc%d: download_fw: Loading Compiled u_code:\n", instance));

	/* Copy the firmware image */
	soc_hcopy((u_short *)&soc_ucode, (u_short *) socp->socxrp, fw_len);

	/* Get the date string from the firmware image */
	soc_hcopy((u_short *)(socp->socxrp+SOC_XRAM_FW_DATE_STR), date_str,
		sizeof (date_str));
	date_str[sizeof (date_str) / sizeof (u_short) - 1] = 0;

	if (*(caddr_t)date_str != '\0') {
	    sprintf(buf, "!host adapter fw date code: %s\n", (caddr_t)date_str);
	    soc_disp_err(socp, CE_CONT, "driver.1010", buf);
	} else {
	    sprintf(buf, "!host adapter fw date code: <not available>\n");
	    soc_disp_err(socp, CE_CONT, "driver.3010", buf);
	}
}

/*
 * Function name : soc_enable()
 *
 * Return Values :  none
 *
 * Description	 : Enable the soc to start executing
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 *
 * Note that collisions on the interrupt mask operations should be prevented
 * before entering this routine either by acquiring the interrupt mask
 * mutex or through guarantee of single-threaded access (e.g., called
 * from soc_attach()).
 */
static	void
soc_enable(soc_state_t *socp)
{
	int instance = ddi_get_instance(socp->dip);

	DEBUGF(2, (CE_CONT, "soc%d: enable:\n", instance));
	socp->socrp->soc_sae.w = 0;	/* slave access error reg - not used */
	socp->socrp->soc_cr.w = socp->cfg;
	socp->socrp->soc_csr.w = SOC_CSR_SOC_TO_HOST;

	socp->soc_shutdown = 0;

	/*
	 * Turn on Interrupts.
	 */
#ifndef	__lock_lint
	socp->k_soc_imr = SOC_CSR_SOC_TO_HOST | SOC_CSR_SLV_ACC_ERR;
	socp->socrp->soc_imr = socp->k_soc_imr;
#endif	__lock_lint
}

#ifdef	UNSOLICITED_POOLS
/*
 * static int
 * soc_establish_pools() - this routine tells the SOC of a scratch pool of
 *	memory to place LINK ctl application data as it arrives.
 *
 *	Returns:
 *		DDI_SUCCESS, upon establishing the pool.
 *		DDI_FAILURE, if unable to establish the pool.
 */

static int
soc_establish_pools(soc_state_t *socp)
{
	soc_request_t		rq;
	int			result;
	int instance = ddi_get_instance(socp->dip);

	DEBUGF(2, (CE_CONT, "soc%d: establish_pools: \n", instance));

	/* Allocate DVMA resources for the Request Queue. */
	result = ddi_iopb_alloc(socp->dip, soclim, (u_int)SOC_POOL_SIZE,
		(caddr_t *)&socp->pool);
	if (result != DDI_SUCCESS) {
		soc_disp_err(socp, CE_WARN, "driver.4090", "!alloc failed");
		return (DDI_FAILURE);
	}

	result = ddi_dma_addr_setup(socp->dip, (struct as *)0,
		(caddr_t)socp->pool, SOC_POOL_SIZE,
		DDI_DMA_RDWR | DDI_DMA_CONSISTENT,
		DDI_DMA_DONTWAIT, 0, soclim, &socp->pool_dhandle);
	if (result == DDI_DMA_MAPPED) {
		result = ddi_dma_htoc(socp->pool_dhandle, (off_t)0,
			&socp->pool_dcookie);
	} else {
		soc_disp_err(socp, CE_WARN, "driver.4100",
			"!DMA address setup failed");
		if (socp->pool)
			ddi_iopb_free((caddr_t)socp->pool);
		return (DDI_FAILURE);
	}

	if (result != DDI_SUCCESS) {
		soc_disp_err(socp, CE_WARN, "driver.4110",
			"!DVMA alloc failed");

		if (socp->pool_dhandle)
			ddi_dma_free(socp->pool_dhandle);

		if (socp->pool)
			ddi_iopb_free((caddr_t)socp->pool);
		return (DDI_FAILURE);
	}

	/*
	 * Fill in the request structure.
	 */
	rq.sr_soc_hdr.sh_request_token = 1;
	rq.sr_soc_hdr.sh_flags = SOC_FC_HEADER | SOC_UNSOLICITED |
		SOC_NO_RESPONSE;
	rq.sr_soc_hdr.sh_class = 0;
	rq.sr_soc_hdr.sh_seg_cnt = 1;
	rq.sr_soc_hdr.sh_byte_cnt = 0x800000;

	rq.sr_fc_frame_hdr.r_ctl = R_CTL_ELS_REQ;
	rq.sr_fc_frame_hdr.d_id = 0;
	rq.sr_fc_frame_hdr.s_id = 0;
	rq.sr_fc_frame_hdr.type = 0;
	rq.sr_fc_frame_hdr.f_ctl = 0;
	rq.sr_fc_frame_hdr.seq_id = 0;
	rq.sr_fc_frame_hdr.df_ctl = 0;
	rq.sr_fc_frame_hdr.seq_cnt = 0;
	rq.sr_fc_frame_hdr.ox_id = 0;
	rq.sr_fc_frame_hdr.rx_id = 0;
	rq.sr_fc_frame_hdr.ro = 0;

	rq.sr_cqhdr.cq_hdr_count = 1;
	rq.sr_cqhdr.cq_hdr_type = CQ_TYPE_UNSOLICITED;
	rq.sr_cqhdr.cq_hdr_flags = 0;
	rq.sr_cqhdr.cq_hdr_seqno = 0;

	rq.sr_dataseg[0].fc_base = (u_long)socp->pool_dcookie.dmac_address;
	rq.sr_dataseg[0].fc_count = SOC_POOL_SIZE;
	rq.sr_dataseg[1].fc_base = 0;
	rq.sr_dataseg[1].fc_count = 0;
	rq.sr_dataseg[2].fc_base = 0;
	rq.sr_dataseg[2].fc_count = 0;

	SOCDEBUG(3, soc_debug_soc_header(&rq.sr_soc_hdr));
	SOCDEBUG(3, soc_debug_fc_frame_header(&rq.sr_fc_frame_hdr));
	SOCDEBUG(3, soc_debug_cq_header(&rq.sr_cqhdr));

	/* Enque the request. */
	return (soc_cq_enque(socp, NULL, (cqe_t *)&rq, CQ_REQUEST_1, FC_SLEEP,
		NULL, 0));
}
#endif	UNSOLICITED_POOLS

/*
 * Function name : soc_init_cq_desc()
 *
 * Return Values : none
 *
 * Description	 : Initializes the request and response queue
 *		   descriptors in the SOC's XRAM
 *
 * Context	 : Should only be called during initialiation when
 *		   the SOC is reset.
 */
static void
soc_init_cq_desc(soc_state_t *socp)
{
	soc_cq_t	que_desc[HW_N_CQS];
	int		i;
	u_int		*ip;

	/*
	 * Finish CQ table initialization and give the descriptor
	 * table to the soc.  Note that we don't use all of the queues
	 * provided by the hardware, but we make sure we initialize the
	 * quantities in the unused fields in the hardware to zeroes.
	 */

	/*
	 * Do request queues
	 */
	for (i = 0; i < HW_N_CQS; i++) {
		if (soc_req_entries[i] && (i < N_CQS)) {
		    que_desc[i].cq_address =
			(cqe_t *)socp->request[i].skc_dcookie.dmac_address;
		    que_desc[i].cq_last_index = soc_req_entries[i] - 1;
		} else {
		    que_desc[i].cq_address = 0;
		    que_desc[i].cq_last_index = 0;
		}
		que_desc[i].cq_in = 0;
		que_desc[i].cq_out = 0;
		que_desc[i].cq_seqno = 1; /* required by SOC microcode */
	}

	/* copy to XRAM */
	soc_hcopy((u_short *) que_desc,		/* pointer to kernal copy */
		(u_short *) socp->xram_reqp,	/* pointer to xram location */
		N_CQS * sizeof (soc_cq_t));

	/*
	 * Do response queues
	 */
	for (i = 0; i < HW_N_CQS; i++) {
		if (soc_rsp_entries[i] && (i < N_CQS)) {
		    que_desc[i].cq_last_index = soc_rsp_entries[i] - 1;
		    ip = (u_int *)&que_desc[i].cq_address;
		    *ip = 1;
		} else {
		    que_desc[i].cq_address = 0;
		    que_desc[i].cq_last_index = 0;
		}
	}

	/* copy to XRAM */
	soc_hcopy((u_short *) que_desc,		/* pointer to kernal copy */
		(u_short *) socp->xram_rspp,	/* pointer to xram location */
		N_CQS * sizeof (soc_cq_t));
}


/*
 * soc_init_transport_interface() - initialize the FC transport interface
 *	structure.
 */

static void
soc_init_transport_interface(soc_state_t *socp)
{
	char	name[64];
	int	i;
#ifdef	PROM_WWN
	char	*pname = NULL;
	int	length = 0;
#endif	PROM_WWN
#ifdef	SOC_LOCK_STATS
	int	lock_temp;

	lock_temp = lock_stats;
	lock_stats |= soc_lock_stats;
#endif	SOC_LOCK_STATS

	/*
	 * Initialize the initial state of the pkt cache
	 */
	mutex_init(&socp->fc_pkt_cache.fpc_mutex, "soc cache mutex",
		MUTEX_DRIVER, (void *)socp->iblkc);

	/*
	 * Initialize the interrupt mask mutex
	 */
	mutex_init(&socp->k_imr_mtx, "soc interrupt mutex",
		MUTEX_DRIVER, (void *)socp->iblkc);

#ifdef	SOC_LOCK_STATS
	lock_stats = lock_temp;
#endif	SOC_LOCK_STATS

	mutex_enter(&(&socp->fc_pkt_cache)->fpc_mutex);
	(void) soc_pkt_cache_alloc(socp, FC_SLEEP);
	mutex_exit(&(&socp->fc_pkt_cache)->fpc_mutex);

	/*
	 * Initialize the transport interface structure.
	 */
	for (i = 0; i < N_SOC_NPORTS; i++) {
		sprintf(name, "soc.xport.mtx.%d.%d",
			ddi_get_instance(socp->dip), i);

#ifdef	SOC_LOCK_STATS
		lock_temp = lock_stats;
		lock_stats |= soc_lock_stats;
#endif	SOC_LOCK_STATS

		mutex_init(&socp->xport[i].fc_mtx, name,
			MUTEX_DRIVER, (void *)socp->iblkc);

		sprintf(name, "soc.xport.cv.%d.%d",
			ddi_get_instance(socp->dip), i);
		cv_init(&socp->xport[i].fc_cv, name, CV_DRIVER,
			(void *)socp->iblkc);

#ifdef	SOC_LOCK_STATS
		lock_stats = lock_temp;
#endif	SOC_LOCK_STATS

		socp->xport[i].fc_cookie = (void *)&socp->port_status[i];
		socp->xport[i].fc_dmalimp = soclim;
		socp->xport[i].fc_iblock = socp->iblkc;

		socp->xport[i].fc_transport = soc_transport;
		socp->xport[i].fc_reset = soc_reset;
		socp->xport[i].fc_pkt_alloc = soc_pkt_alloc;
		socp->xport[i].fc_pkt_free = soc_pkt_free;

		socp->xport[i].fc_uc_register = soc_uc_register;
		socp->xport[i].fc_uc_unregister = soc_uc_unregister;
		socp->xport[i].fc_statec_register = soc_statec_register;
		socp->xport[i].fc_statec_unregister = soc_statec_unregister;
		socp->xport[i].fc_interface_poll = soc_interface_poll;
		socp->xport[i].fc_uc_get_pkt = soc_uc_get_pkt;
	}

#ifdef	PROM_WWN
	/*
	 * Get the NPORTs wwn.
	 */
	if ((ddi_getlongprop(DDI_DEV_T_ANY, socp->dip, DDI_PROP_DONTPASS,
			"port-wwns",
			(caddr_t)&pname, &length) == DDI_PROP_SUCCESS) &&
			(length == 2 * sizeof (la_wwn_t))) {
		bcopy(pname,
		(caddr_t)&socp->port_status[0].sp_d_wwn, sizeof (la_wwn_t));
		bcopy(&pname[8], (caddr_t)&socp->port_status[1].sp_d_wwn,
			sizeof (la_wwn_t));
		kmem_free((void *)pname, length);
	} else {
		soc_disp_err(socp, CE_WARN, "wwn.3020",
			"!Could not get port world wide name");

	}
#endif	PROM_WWN
}


/*
 * static unsigned int
 * soc_intr() - this is the interrupt routine for the SOC. Process all
 *	possible incoming interrupts from the soc device.
 */

static unsigned int
soc_intr(caddr_t arg)
{
	soc_state_t *socp = (soc_state_t *)arg;
	register volatile soc_reg_t *socreg = socp->socrp;
	unsigned csr;
	int cause = 0;
	int instance = ddi_get_instance(socp->dip);
	int i, j, request;
	char full;

	csr = socreg->soc_csr.w;
	cause = (int)SOC_INTR_CAUSE(socp, csr);

	DEBUGF(2, (CE_CONT,
		"soc%d: intr: csr: 0x%x cause: 0x%x\n",
		instance, csr, cause));

	if (!cause) {
		return (DDI_INTR_UNCLAIMED);
	}

	while (cause) {
	/*
	 * Process the unsolicited messages first in case there are some
	 * high priority async events that we should act on.
	 *
	 */
	    if (cause & SOC_CSR_RSP_QUE_1) {
		    soc_intr_unsolicited(socp);
	    }

	    if (cause & SOC_CSR_RSP_QUE_0) {
		    soc_intr_solicited(socp);
	    }


	/*
	 * Process any request interrupts:
	 * We only allow request interrupts when the request
	 * queue is full and we are waiting so we can enque
	 * another command.
	 */
	    if ((request = (cause & SOC_CSR_HOST_TO_SOC)) != 0) {
		for (i = SOC_CSR_1ST_H_TO_S, j = 0; j < N_CQS; j++, i <<= 1) {
		    if (request & i) {
			soc_kcq_t *kcq = &socp->request[j];

#ifndef	__lock_lint
			if (kcq->skc_full) {
#endif	__lock_lint


			    mutex_enter(&kcq->skc_mtx);
			    full = kcq->skc_full;
			    kcq->skc_full = 0;

			    while (kcq->skc_overflowh) {
				if (soc_cq_enque(socp,
				    (soc_port_t *)
				    kcq->skc_overflowh->fpe_pkt.fc_pkt_cookie,
				    &kcq->skc_overflowh->fpe_cqe,
				    j, FC_NOSLEEP, NULL, 1)
				    != FC_TRANSPORT_SUCCESS)
					break;
				if ((kcq->skc_overflowh
					= kcq->skc_overflowh->fpe_next) == 0) {
				    kcq->skc_overflowt = NULL;
				    DEBUGF(2, (CE_CONT,
				"soc%d: intr: req queue %d overflow cleared\n",
					instance, j));
				}
			    }
			    if (!kcq->skc_overflowh && (full & SOC_SKC_SLEEP))
				cv_broadcast(&kcq->skc_cv);

			    if (!kcq->skc_overflowh) {
				/* Disable this queue's intrs */
				mutex_enter(&socp->k_imr_mtx);
				socp->socrp->soc_imr =
				(socp->k_soc_imr &= ~i);
				mutex_exit(&socp->k_imr_mtx);
			    }

			    mutex_exit(&kcq->skc_mtx);
#ifndef	__lock_lint
			}
#endif	__lock_lint
		    }
		}
	    }
	    csr = socreg->soc_csr.w;
	    cause = (int)SOC_INTR_CAUSE(socp, csr);
	}
	return (DDI_INTR_CLAIMED);
}

/*
 * static void
 * soc_intr_solicited() - this routine deque's solicited messages from
 *	the response queue.  The routine inturn routes the response
 *	to the appropriate place.
 *
 *	Returns:
 */

static void
soc_intr_solicited(soc_state_t *socp)
{
	soc_kcq_t		*kcq;
	volatile soc_kcq_t		*kcqv;
	soc_response_t	*srp;
	cqe_t		*cqe;
	u_int		status;
	fc_pkt_extended_t	*fc_fcpkt;
	fc_packet_t		*fcpkt;
	soc_header_t		*shp;
	register volatile soc_reg_t *socreg = socp->socrp;
	register volatile soc_cq_t *cqdesc;
	register volatile u_short *s;
	register u_short *src, *dst;
	u_int		i;
	u_char		index_in;
	int instance = ddi_get_instance(socp->dip);
	auto char buf[80];

	kcq = &socp->response[CQ_SOLICITED];
	kcqv = (volatile soc_kcq_t *)kcq;
	cqdesc = (volatile soc_cq_t *)kcq->skc_xram_cqdesc;

	/*
	 * Grab lock for request queue.
	 */
	mutex_enter(&kcq->skc_mtx);

	/*
	 * The host adapter ucode will assign the response queue address
	 * in xRAM.  If this is the first time through for us, we need
	 * to get the address that the ucode has assigned.
	 */
	if (kcq->skc_cq == NULL) {

	    /* The usual halfword-access-only stuff... */
	    s = (volatile u_short *)&cqdesc->cq_address;
	    i = *(s + 1) & ~1;
	    kcq->skc_cq = (cqe_t *)(socp->socxrp + i);
	}

	/*
	 * Process as many response queue entries as we can.
	 *
	 * We read the CQ entries from the host adapter's xRAM
	 * (in SBus slave space).
	 */
	cqe = &(kcq->skc_cq[kcqv->skc_out]);

	/* Find out where the newest entry lives in the queue */
	index_in = cqdesc->cq_in;

	while (kcqv->skc_out != index_in) {

		srp = (soc_response_t *)cqe;
		SOCDEBUG(3, soc_debug_soc_response(srp));
		shp = &srp->sr_soc_hdr;

		/*
		 * The only part of the status word that has useful information
		 * is the lowest byte... thus, we'll read only the bottom
		 * half of the word to conserve on PIO reads.
		 */
		s = (volatile u_short *)&srp->sr_soc_status;
		status = *(s + 1);

		/*
		 * Now, get the request_token.  The hardware supports only
		 * halfword reads, so we need to assemble it a halfword
		 * at a time.
		 */
		s = (volatile u_short *)&shp->sh_request_token;
		i = (*s << 16) | *(s + 1);

		if (((fc_fcpkt = (fc_pkt_extended_t *)i) == NULL) ||
#ifndef	__lock_lint
			(fc_fcpkt > socp->fc_pkt_hi) ||
			(fc_fcpkt < socp->fc_pkt_lo) ||
#endif	__lock_lint
			(fc_fcpkt->fpe_magic != FPE_MAGIC)) {
		    sprintf(buf, "!invalid FC packet; \n\
			in, out, seqno = 0x%x, 0x%x, 0x%x\n",
			kcqv->skc_in, kcqv->skc_out, kcqv->skc_seqno);
		    soc_disp_err(socp, CE_WARN, "link.4060", buf);
		    DEBUGF(1, (CE_CONT,
		    "\tsoc CR: 0x%x SAE: 0x%x CSR: 0x%x IMR: 0x%x\n",
			    socp->socrp->soc_cr.w,
			    socp->socrp->soc_sae.w,
			    socp->socrp->soc_csr.w,
			    socp->socrp->soc_imr));
		/*
		 * Update response queue ptrs and soc registers.
		 */
		    kcqv->skc_out++;
		    if ((kcqv->skc_out & kcq->skc_last_index) == 0) {
			    kcqv->skc_out = 0;
			    kcqv->skc_seqno++;
		    }

		} else {

			fcpkt = &fc_fcpkt->fpe_pkt;
			DEBUGF(2, (CE_CONT, "packet 0x%x complete\n",
				(int)fcpkt));

			/*
			 * map soc status codes to
			 * transport status codes
			 */
#ifdef	DEBUG
			if (status != SOC_OK)
			    SOCDEBUG(2, soc_debug_soc_status(status));
#endif	DEBUG
			switch (status) {
			    case SOC_OK:
				fcpkt->fc_pkt_status = FC_STATUS_OK;
				break;
			    case SOC_P_RJT:
				fcpkt->fc_pkt_status = FC_STATUS_P_RJT;
				break;
			    case SOC_F_RJT:
				fcpkt->fc_pkt_status = FC_STATUS_F_RJT;
				break;
			    case SOC_P_BSY:
				fcpkt->fc_pkt_status = FC_STATUS_P_BSY;
				break;
			    case SOC_F_BSY:
				fcpkt->fc_pkt_status = FC_STATUS_F_BSY;
				break;
			    case SOC_OFFLINE:
				fcpkt->fc_pkt_status = FC_STATUS_ERR_OFFLINE;
				break;
			    case SOC_TIMEOUT:
				fcpkt->fc_pkt_status = FC_STATUS_TIMEOUT;
				break;
			    case SOC_OVERRUN:
				fcpkt->fc_pkt_status = FC_STATUS_ERR_OVERRUN;
				break;
			    case SOC_UNKOWN_CQ_TYPE:
				fcpkt->fc_pkt_status =
					FC_STATUS_UNKNOWN_CQ_TYPE;
				break;
			    case SOC_BAD_SEG_CNT:
				fcpkt->fc_pkt_status = FC_STATUS_BAD_SEG_CNT;
				break;
			    case SOC_MAX_XCHG_EXCEEDED:
				fcpkt->fc_pkt_status =
					FC_STATUS_MAX_XCHG_EXCEEDED;
				break;
			    case SOC_BAD_XID:
				fcpkt->fc_pkt_status = FC_STATUS_BAD_XID;
				break;
			    case SOC_XCHG_BUSY:
				fcpkt->fc_pkt_status = FC_STATUS_XCHG_BUSY;
				break;
			    case SOC_BAD_POOL_ID:
				fcpkt->fc_pkt_status = FC_STATUS_BAD_POOL_ID;
				break;
			    case SOC_INSUFFICIENT_CQES:
				fcpkt->fc_pkt_status =
					FC_STATUS_INSUFFICIENT_CQES;
				break;
			    case SOC_ALLOC_FAIL:
				fcpkt->fc_pkt_status = FC_STATUS_ALLOC_FAIL;
				break;
			    case SOC_BAD_SID:
				fcpkt->fc_pkt_status = FC_STATUS_BAD_SID;
				break;
			    case SOC_NO_SEG_INIT:
				fcpkt->fc_pkt_status = FC_STATUS_NO_SEQ_INIT;
				break;
			    default:
				fcpkt->fc_pkt_status = FC_STATUS_ERROR;
				break;
			}

			ASSERT((fc_fcpkt->cmd_state & FC_CMD_COMPLETE) == 0);
			fc_fcpkt->cmd_state |= FC_CMD_COMPLETE;
			fcpkt->fc_pkt_flags |= FCFLAG_COMPLETE;

			/*
			 * Copy the response frame header (if there is one)
			 * so that the upper levels can use it.  Note that,
			 * for now, we'll copy the header only if there was
			 * some sort of non-OK status, to save the PIO reads
			 * required to get the header from the host adapter's
			 * xRAM.
			 */
			if ((status != SOC_OK) &&
				(srp->sr_soc_hdr.sh_flags & SOC_FC_HEADER)) {
			    fcpkt->fc_frame_resp = &fc_fcpkt->fpe_resp_hdr;
			    src = (u_short *)&srp->sr_fc_frame_hdr;
			    dst = (u_short *)fcpkt->fc_frame_resp;
			    for (i =
				sizeof (fc_frame_header_t)/sizeof (u_short);
				i != 0; i--)
				*dst++ = *src++;
			} else
			    fcpkt->fc_frame_resp = NULL;

			/*
			 * Update response queue ptrs and soc registers.
			 */
			kcqv->skc_out++;
			if ((kcqv->skc_out & kcq->skc_last_index) == 0) {
				kcqv->skc_out = 0;
				kcqv->skc_seqno++;
			}

			/*
			 * Call the completion routine
			 */
			if (fcpkt->fc_pkt_comp != NULL) {

				/*
				 * Give up the mutex to avoid a deadlock
				 * with the callback routine
				 */
				mutex_exit(&kcq->skc_mtx);

				(*fcpkt->fc_pkt_comp)(fcpkt);

				mutex_enter(&kcq->skc_mtx);
			} else {
				/*
				 * must be polling command
				 * - no completion routine
				 */
				DEBUGF(4, (CE_CONT,
			    "soc%d: intr_solicited: No Completion return\n",
				instance));
			}
		}

		/*
		 * We need to re-read the input and output pointers in
		 * case a polling routine should process some entries
		 * from the response queue while we're doing a callback
		 * routine with the response queue mutex dropped.
		 */
		cqe = &(kcq->skc_cq[kcqv->skc_out]);
		index_in = cqdesc->cq_in;

		/*
		 * Mess around with the hardware if we think we've run out
		 * of entries in the queue, just to make sure we've read
		 * all entries that are available.
		 */
		if (index_in == kcqv->skc_out) {

		    socreg->soc_csr.w =
			((kcqv->skc_out << 24) |
			(SOC_CSR_SOC_TO_HOST & ~SOC_CSR_RSP_QUE_0));

		/* Make sure the csr write has completed */
		    i = socreg->soc_csr.w;

		/*
		 * Update our idea of where the host adapter has placed
		 * the most recent entry in the response queue
		 */
		    index_in = cqdesc->cq_in;
		}
	}

	/* Drop lock for request queue. */
	mutex_exit(&kcq->skc_mtx);
}

/*
 * Function name : soc_intr_unsolicited()
 *
 * Return Values : none
 *
 * Description	 : Processes entries in the unsolicited response
 *		   queue
 *
 *	The SOC will give us an unsolicited response
 *	whenever its status changes: OFFLINE, ONLINE,
 *	or in response to a packet arriving from an originator.
 *
 *	When message requests come in they will be placed in our
 *	buffer queue or in the next "inline" packet by the SOC hardware.
 *
 * Context	: Unsolicited interrupts must be masked
 */

static void
soc_intr_unsolicited(soc_state_t *socp)
{
	soc_kcq_t	*kcq;
	volatile soc_kcq_t	*kcqv;
	soc_response_t	*srp;
	volatile cqe_t	*cqe;
	int		port;
	register u_char	t_index, t_seqno;
	register volatile soc_reg_t *socreg = socp->socrp;
	volatile cqe_t	*cqe_cont = NULL;
	u_int		i;
	int		hdr_count;
	int		status;
	u_short		flags;
	register volatile soc_cq_t *cqdesc;
	register volatile u_short *s;
	soc_statec_cb_t	*s_cbp;
	auto char	buf[256];
	soc_port_t	*port_statusp;
	int instance = ddi_get_instance(socp->dip);
	auto cqe_t	temp_cqe,
			temp_cqe_cont;
	u_char		index_in;

	kcq = &socp->response[CQ_UNSOLICITED];
	kcqv = (volatile soc_kcq_t *)kcq;
	cqdesc = (volatile soc_cq_t *)kcq->skc_xram_cqdesc;

	/*
	 * Grab lock for response queue.
	 */
	mutex_enter(&kcq->skc_mtx);

	/*
	 * The host adapter ucode will assign the response queue address
	 * in xRAM.  If this is the first time through for us, we need
	 * to get the address that the ucode has assigned.
	 */
	if (kcq->skc_cq == NULL) {

	    /* The usual halfword-access-only stuff... */
	    s = (volatile u_short *)&cqdesc->cq_address;
	    i = *(s + 1) & ~1;
	    kcq->skc_cq = (cqe_t *)(socp->socxrp + i);
	}

	cqe = (volatile cqe_t *)&(kcq->skc_cq[kcqv->skc_out]);

	/* Find out where the newest entry lives in the queue */
	index_in = cqdesc->cq_in;

	while (kcqv->skc_out != index_in) {

		/* Check for continuation entries */
		if ((hdr_count = cqe->cqe_hdr.cq_hdr_count) != 1) {

		    t_seqno = kcqv->skc_seqno;
		    t_index = kcqv->skc_out + hdr_count;

		    i = index_in;
		    if (kcqv->skc_out > index_in)
			i += kcq->skc_last_index + 1;

		/*
		 * If we think the continuation entries haven't yet
		 * arrived, try once more before giving up
		 */
		    if (i < t_index) {

			socreg->soc_csr.w =
			    ((kcqv->skc_out << 24) |
			    (SOC_CSR_SOC_TO_HOST & ~SOC_CSR_RSP_QUE_1));

			/* Make sure the csr write has completed */
			i = socreg->soc_csr.w;

			/*
			 * Update our idea of where the host adapter has placed
			 * the most recent entry in the response queue
			 */
			i = index_in = cqdesc->cq_in;
			if (kcqv->skc_out > index_in)
			    i += kcq->skc_last_index + 1;

			/*
			 * Exit if the continuation entries haven't yet
			 * arrived
			 */
			if (i < t_index)
			    break;
		    }

		    if (t_index > kcq->skc_last_index) {
			t_seqno++;
			t_index &= kcq->skc_last_index;
		    }

		    cqe_cont = (volatile cqe_t *)
		    &(kcq->skc_cq[t_index ? t_index - 1 : kcq->skc_last_index]);


		    /* A cq_hdr_count > 2 is illegal; throw away the response */
		    if (hdr_count != 2) {
			soc_disp_err(socp, CE_WARN, "driver.4030",
			    "!too many continuation entries");
			DEBUGF(1, (CE_CONT,
				"soc%d: soc unsolicited entry count = %d\n",
				instance, cqe->cqe_hdr.cq_hdr_count));

			if ((++t_index & kcq->skc_last_index) == 0)
			    t_index = 0, t_seqno++;
			kcqv->skc_out = t_index;
			kcqv->skc_seqno = t_seqno;

			cqe = &(kcq->skc_cq[kcqv->skc_out]);
			cqe_cont = NULL;
			continue;
		    }
		}

		/*
		 * Update unsolicited response queue ptrs
		 */
		kcqv->skc_out++;
		if ((kcqv->skc_out & kcq->skc_last_index) == 0) {
			kcqv->skc_out = 0;
			kcqv->skc_seqno++;
		}

		if (cqe_cont != NULL) {
		    kcqv->skc_out++;
		    if ((kcqv->skc_out & kcq->skc_last_index) == 0) {
			    kcqv->skc_out = 0;
			    kcqv->skc_seqno++;
		    }
		}

		srp = (soc_response_t *)cqe;
		SOCDEBUG(3, soc_debug_soc_response(srp));
		flags = srp->sr_soc_hdr.sh_flags;
		port = flags & SOC_PORT_B;

		switch (flags & ~SOC_PORT_B) {
		case SOC_UNSOLICITED | SOC_FC_HEADER:

		/*
		 * Make a copy of the queue entries so we don't have
		 * to worry about the size of our accesses
		 */
		    soc_hcopy((u_short *)cqe, (u_short *)&temp_cqe,
			sizeof (cqe_t));
		    cqe = (volatile cqe_t *)&temp_cqe;
		    if (cqe_cont) {
			soc_hcopy((u_short *)cqe_cont,
			    (u_short *)&temp_cqe_cont, sizeof (cqe_t));
			cqe_cont = (volatile cqe_t *)&temp_cqe_cont;
		    }
		    srp = (soc_response_t *)cqe;

		    switch (srp->sr_fc_frame_hdr.r_ctl & R_CTL_ROUTING) {
		    case R_CTL_EXTENDED_SVC:
			/*
			 * Extended Link Services frame received
			 */
			soc_us_els(socp, (cqe_t *)cqe, (cqe_t *)cqe_cont);

			break;
		    case R_CTL_BASIC_SVC:
			sprintf(buf, "!unsupported Link Service command: 0x%x",
				srp->sr_fc_frame_hdr.type);
			soc_disp_err(socp, CE_WARN, "link.4020", buf);
			break;
		    case R_CTL_DEVICE_DATA:
			switch (srp->sr_fc_frame_hdr.type) {
			default:
			    sprintf(buf, "!unknown FC-4 command: 0x%x",
				srp->sr_fc_frame_hdr.type);
			    soc_disp_err(socp, CE_WARN, "link.4030", buf);
			    break;
			}
			break;
		    default:
			sprintf(buf, "!unsupported FC frame R_CTL: 0x%x",
			    srp->sr_fc_frame_hdr.r_ctl);
			soc_disp_err(socp, CE_WARN, "link.4040", buf);
			break;
		    }
		    break;

		case SOC_STATUS:
			port_statusp = &socp->port_status[port];

			/*
			 * Note that only the lsbyte of the status has
			 * interesting information...
			 */
			s = (volatile u_short *)&srp->sr_soc_status;
			status = *(s + 1);

			switch (status) {

			case SOC_ONLINE:
				sprintf(buf,
				"port %d: Fibre Channel is ONLINE\n", port);
				soc_disp_err(socp, CE_CONT, "link.6010", buf);
				/*
				 * If we have already logged in
				 * we must re-login to re-establish
				 * login state.
				 */
				mutex_enter(&port_statusp->sp_mtx);
				port_statusp->sp_status &=
					~(PORT_OFFLINE|PORT_STATUS_FLAG);
				if (!(port_statusp->sp_status
					& PORT_LOGIN_ACTIVE)) {
				    port_statusp->sp_status |=
						PORT_LOGIN_ACTIVE;
				    mutex_exit(&port_statusp->sp_mtx);
				    port_statusp->sp_login_retries =
					SOC_LOGIN_RETRIES;
				    mutex_exit(&kcq->skc_mtx);
				    if (soc_login(socp, port, FABRIC_FLAG,
					    0) != DDI_SUCCESS) {
					mutex_enter(&port_statusp->sp_mtx);
					port_statusp->sp_status |=
						PORT_LOGIN_RECOVERY;
					mutex_exit(&port_statusp->sp_mtx);
					soc_login_recovery(port_statusp);
				    }
				    mutex_enter(&kcq->skc_mtx);
				} else if ((port_statusp->sp_status
					& PORT_LOGIN_RECOVERY)) {
				    port_statusp->sp_status &=
					~PORT_LOGIN_RECOVERY;
				    mutex_exit(&port_statusp->sp_mtx);
				    mutex_exit(&kcq->skc_mtx);
				    soc_login_retry(port_statusp);
				    mutex_enter(&kcq->skc_mtx);
				} else {
				    mutex_exit(&port_statusp->sp_mtx);
				}
				break;

			case SOC_OFFLINE:
				/*
				 * SOC and Responder will both flush
				 * all active commands.
				 * So I don't have to do anything
				 * until it comes back online.
				 */
				sprintf(buf,
				"port %d: Fibre Channel is OFFLINE\n", port);
				soc_disp_err(socp, CE_CONT, "link.5010", buf);

				mutex_enter(&port_statusp->sp_mtx);
				port_statusp->sp_status |=
					PORT_OFFLINE;
				port_statusp->sp_status &=
					~PORT_STATUS_FLAG;
				mutex_exit(&port_statusp->sp_mtx);

				mutex_exit(&kcq->skc_mtx);
				for (s_cbp = port_statusp->state_cb;
					s_cbp != NULL; s_cbp = s_cbp->next) {
				    (*s_cbp->callback)(s_cbp->arg,
				    FC_STATE_OFFLINE);
				}
				mutex_enter(&kcq->skc_mtx);
				break;
			default:
				sprintf(buf, "!unknown status: 0x%x\n",
				status);
				soc_disp_err(socp, CE_WARN, "link.3020", buf);
			}
			break;
		default:
			sprintf(buf, "!unexpected state: flags: 0x%x\n",
			flags);
			soc_disp_err(socp, CE_WARN, "link.4050", buf);
			DEBUGF(1, (CE_CONT,
			"\tsoc CR: 0x%x SAE: 0x%x CSR: 0x%x IMR: 0x%x\n",
				socp->socrp->soc_cr.w,
				socp->socrp->soc_sae.w,
				socp->socrp->soc_csr.w,
				socp->socrp->soc_imr));
		}

		/*
		 * We need to re-read the input and output pointers in
		 * case a polling routine should process some entries
		 * from the response queue while we're doing a callback
		 * routine with the response queue mutex dropped.
		 */
		cqe = &(kcq->skc_cq[kcqv->skc_out]);
		index_in = cqdesc->cq_in;
		cqe_cont = NULL;

		/*
		 * Mess around with the hardware if we think we've run out
		 * of entries in the queue, just to make sure we've read
		 * all entries that are available.
		 */
		if (index_in == kcqv->skc_out) {

		    socreg->soc_csr.w =
			((kcqv->skc_out << 24) |
			(SOC_CSR_SOC_TO_HOST & ~SOC_CSR_RSP_QUE_1));

		/* Make sure the csr write has completed */
		    i = socreg->soc_csr.w;

		/*
		 * Update our idea of where the host adapter has placed
		 * the most recent entry in the response queue
		 */
		    index_in = cqdesc->cq_in;
		}
	}

	/* Release lock for response queue. */
	mutex_exit(&kcq->skc_mtx);
}

/*
 * soc_us_els() - This function handles unsolicited extended link
 *	service responses received from the soc.
 */
static void
soc_us_els(soc_state_t *socp, cqe_t *cqe, cqe_t *cqe_cont)
{
	soc_response_t	*srp = (soc_response_t *)cqe;
	els_payload_t	*els = (els_payload_t *)cqe_cont;
	int	i;
	char   *bp;
	auto	char buf[256];

	/*
	 * There should be a CQE continuation entry for all
	 * extended link services
	 */
	if ((els == NULL) || ((i = srp->sr_soc_hdr.sh_byte_cnt) == 0)) {
	    soc_disp_err(socp, CE_WARN, "link.4010",
		"!incomplete continuation entry");
	    return;
	}

	/* Quietly impose a maximum byte count */
	if (i > SOC_CQE_PAYLOAD) i = SOC_CQE_PAYLOAD;
	i -= sizeof (union els_cmd_u);

	/*
	 * Decode the LS_Command code
	 */
	switch (els->els_cmd.i) {
	    case LS_DISPLAY:
		els->els_data[i] = '\0';	/* terminate the string */
		for (bp = (char *)&(els->els_data[0]); *bp; bp++) {
			/* squash newlines */
			if (*bp == '\n') *bp = ' ';
		}
		sprintf(buf, "!message: %s\n", els->els_data);
		soc_disp_err(socp, CE_CONT, "link.1010", buf);
		break;

	    default:
		soc_disp_err(socp, CE_WARN, "link.3010",
			"!unknown LS_Command\n");
	}

}

/*
 * static int
 * soc_login_alloc() - This function allocates the necessary
 *	kernel resources for performing fabric or
 *	n-port login.
 *
 * Context:  cannot be called from interrupt context
 */

static int
soc_login_alloc(soc_state_t *socp, int port)
{
	soc_request_t 		*sp;
	fc_pkt_extended_t	*fc_fcpkt;
	soc_port_t		*port_statusp = &socp->port_status[port];

	la_logi_t		*logi = NULL;
	ddi_dma_handle_t	logihandle = NULL;
	ddi_dma_cookie_t	logicookie;

	caddr_t			larp = NULL;
	ddi_dma_handle_t	larphandle = NULL;
	ddi_dma_cookie_t	larpcookie;

	soc_priv_cmd_t		*privp = NULL;

	/*
	 * Allocate login structs.
	 */
	fc_fcpkt = (fc_pkt_extended_t *)
		soc_pkt_alloc((void *)port_statusp, FC_SLEEP);
	sp = (soc_request_t *)&fc_fcpkt->fpe_cqe;

	if ((ddi_iopb_alloc(socp->dip,
		soclim,
		(u_int)sizeof (la_logi_t),
		(caddr_t  *)&logi)) != DDI_SUCCESS) {

		goto fail;
	}


	if ((ddi_dma_addr_setup(socp->dip,
		(struct as *)0,
		(caddr_t)logi,
		sizeof (la_logi_t),
		DDI_DMA_RDWR | DDI_DMA_CONSISTENT,
		DDI_DMA_SLEEP,
		0,
		soclim,
		&logihandle)) != DDI_SUCCESS) {

		goto fail;
	}

	if ((ddi_dma_htoc(logihandle,
		(off_t)0,
		&logicookie)) != DDI_SUCCESS) {

		goto fail;
	}

	/*
	 * Allocate response area.
	 */
	if ((ddi_iopb_alloc(socp->dip,
		soclim,
		(u_int)sizeof (la_logi_t),
		&larp)) != DDI_SUCCESS) {

		goto fail;
	}

	if ((ddi_dma_addr_setup(socp->dip,
		(struct as *)0,
		(caddr_t)larp,
		sizeof (la_logi_t),
		DDI_DMA_RDWR | DDI_DMA_CONSISTENT,
		DDI_DMA_SLEEP,
		0,
		soclim,
		&larphandle)) != DDI_SUCCESS) {

		goto fail;
	}

	if ((ddi_dma_htoc(larphandle,
		(off_t)0,
		&larpcookie)) != DDI_SUCCESS) {

		goto fail;
	}

	if ((privp =
		kmem_zalloc(sizeof (soc_priv_cmd_t), KM_SLEEP)) == NULL) {
		goto fail;
	}

	privp->fpktp = (fc_packet_t *)fc_fcpkt;
	privp->cmd = (caddr_t)logi;
	privp->cmd_handle = logihandle;
	privp->resp = (caddr_t)larp;
	privp->resp_handle = larphandle;
	fc_fcpkt->fpe_pkt.fc_pkt_comp = NULL;
	fc_fcpkt->fpe_pkt.fc_pkt_private = privp;
	fc_fcpkt->fpe_pkt.fc_pkt_cookie = (void *)port_statusp;
	fc_fcpkt->cmd_state = FC_CMD_COMPLETE;

	sp->sr_dataseg[0].fc_base = (int)logicookie.dmac_address;
	sp->sr_dataseg[1].fc_base = (int)larpcookie.dmac_address;

	port_statusp->sp_login = fc_fcpkt;

	return (DDI_SUCCESS);

fail:
	soc_disp_err(socp, CE_WARN, "driver.4070", "!alloc failed\n");
	if (fc_fcpkt) soc_pkt_free((void *)&socp->port_status[port],
		(fc_packet_t *)fc_fcpkt);
	if (logi) ddi_iopb_free((caddr_t)logi);
	if (logihandle) ddi_dma_free(logihandle);
	if (larp) ddi_iopb_free((caddr_t)larp);
	if (larphandle) ddi_dma_free(larphandle);
	if (privp) kmem_free((caddr_t)privp, sizeof (soc_priv_cmd_t));
	return (DDI_FAILURE);
}

/*
 * static int
 * soc_login() - This function attempts to perform a fabric or
 *	n-port login.
 *
 *	port: Port to do the login on
 *	flag: Chooses between f-port or n-port login
 *	poll: Nonzero to used polled mode
 *
 *	Returns: SOC packet status
 *
 *	Context: may be called from any context
 */

static int
soc_login(soc_state_t *socp, int port, int flag, int poll)
{
	soc_port_t		*port_statusp = &socp->port_status[port];
	fc_pkt_extended_t	*fc_fcpkt = port_statusp->sp_login;
	soc_request_t 		*sp = (soc_request_t *)&fc_fcpkt->fpe_cqe;
	int instance = ddi_get_instance(socp->dip);

	soc_priv_cmd_t		*privp = (soc_priv_cmd_t *)
					fc_fcpkt->fpe_pkt.fc_pkt_private;

	DEBUGF(2, (CE_CONT,
		"soc%d: starting %s login on port: %d\n",
		instance, ((flag == FABRIC_FLAG) ?  "fabric" : "n-port"),
		port));

	if (!poll) {
	    mutex_enter(&port_statusp->sp_mtx);
	    if (port_statusp->sp_status & PORT_OFFLINE) {
		mutex_exit(&port_statusp->sp_mtx);
		return (DDI_FAILURE);
	    }
	    port_statusp->login_timer_running = 1;
	    port_statusp->login_timeout_id = timeout(soc_login_timeout,
			(caddr_t)port_statusp,
			(drv_usectohz)(SOC_LOGIN_TIMEOUT*10000000));
	    mutex_exit(&port_statusp->sp_mtx);
	}

	privp->flags = flag;

	if (flag == FABRIC_FLAG) {
		(void) soc_make_fabric_login(sp, (la_logi_t *)privp->cmd,
			fc_fcpkt, port);
	} else {
		(void) soc_make_nport_login(socp, sp, (la_logi_t *)privp->cmd,
			fc_fcpkt, port);
	}

	/*
	 * Sync DMA space for login packet.
	 */
	ddi_dma_sync(privp->cmd_handle, 0, 0, DDI_DMA_SYNC_FORDEV);

	fc_fcpkt->fpe_pkt.fc_pkt_comp = (poll) ? NULL : soc_login_done;
	fc_fcpkt->fpe_pkt.fc_pkt_timeout = SOC_LOGIN_TIMEOUT;

	/*
	 * Start the login command.
	 */
	fc_fcpkt->cmd_state &= ~FC_CMD_COMPLETE;
	if (soc_cq_enque(socp, NULL, &fc_fcpkt->fpe_cqe, CQ_REQUEST_1,
			FC_NOSLEEP, fc_fcpkt, 0)) {
		mutex_enter(&port_statusp->sp_mtx);
		if (port_statusp->login_timer_running)
		    untimeout(port_statusp->login_timeout_id);
		port_statusp->login_timer_running = 0;
		mutex_exit(&port_statusp->sp_mtx);

		fc_fcpkt->cmd_state |= FC_CMD_COMPLETE;
		return (DDI_FAILURE);
	}

	/* wait for it to complete */
	if (poll) {
	    if (soc_dopoll(socp, (fc_packet_t *)&fc_fcpkt->fpe_pkt) !=
			FC_TRANSPORT_SUCCESS) {

		    fc_fcpkt->fpe_pkt.fc_pkt_timeout = SOC_OFFLINE_TIMEOUT;

		    /* Timed out... try to force the port offline */
		    mutex_enter(&port_statusp->sp_mtx);
		    port_statusp->sp_status |= PORT_STATUS_FLAG;
		    mutex_exit(&port_statusp->sp_mtx);

		    if ((soc_force_offline(port_statusp, 1) != DDI_SUCCESS) ||
			(soc_dopoll(socp, (fc_packet_t *)&fc_fcpkt->fpe_pkt) !=
			    FC_TRANSPORT_SUCCESS)) {

			/* The offline failed... reset the hardware */
			if (!soc_doreset(socp))
			    return (DDI_FAILURE);
		    }

		    /* Wait for the port to come back online */
		    if (soc_do_online(port_statusp) != FC_TRANSPORT_SUCCESS)
			return (DDI_FAILURE);

		    return (SOC_LOGIN_TIMEOUT_FLAG);
	    }
	    return (fc_fcpkt->fpe_pkt.fc_pkt_status);
	}
	else
	    return (DDI_SUCCESS);
}

/*
 * soc_login_done() - process completion of a login
 */
static void
soc_login_done(fc_packet_t *fcpkt)
{
	soc_port_t		*port_statusp =
					(soc_port_t *)fcpkt->fc_pkt_cookie;
	soc_priv_cmd_t		*privp =
					(soc_priv_cmd_t *)fcpkt->fc_pkt_private;
	soc_statec_cb_t		*cbp;
	int			i;

	mutex_enter(&port_statusp->sp_mtx);

	if (!(port_statusp->sp_status & PORT_LOGIN_ACTIVE)) {
	    mutex_exit(&port_statusp->sp_mtx);
	    return;
	}

	if ((port_statusp->login_timer_running) &&
		(port_statusp->sp_offline->cmd_state == FC_CMD_COMPLETE)) {
	    untimeout(port_statusp->login_timeout_id);
	    port_statusp->login_timer_running = 0;
	}

	if (privp->flags != FABRIC_FLAG) {
	    if (fcpkt->fc_pkt_status == FC_STATUS_OK) {
		port_statusp->sp_status &= ~PORT_LOGIN_ACTIVE;
		i = soc_login_check(port_statusp, (la_logi_t *)privp->resp);
		if (i)
		    port_statusp->sp_status |= NPORT_LOGIN_SUCCESS;
		mutex_exit(&port_statusp->sp_mtx);

		if (i) {
		    soc_disp_err(port_statusp->sp_state, CE_CONT, "login.6010",
			"!Fibre Channel login succeeded\n");
		    for (cbp = port_statusp->state_cb; cbp; cbp = cbp->next) {
			(*cbp->callback)(cbp->arg, FC_STATE_ONLINE);
		    }
		} else {
		    soc_disp_err(port_statusp->sp_state, CE_CONT, "login.5010",
			"!Fibre Channel login failed\n");
		}
	    } else if (fcpkt->fc_pkt_status == FC_STATUS_ERR_OFFLINE) {
		port_statusp->sp_status |= PORT_LOGIN_RECOVERY;
		mutex_exit(&port_statusp->sp_mtx);
		return;
	    } else {
		port_statusp->sp_status |= PORT_LOGIN_RECOVERY;
		mutex_exit(&port_statusp->sp_mtx);
		soc_login_recovery(port_statusp);
		return;
	    }
	} else {

	    if (fcpkt->fc_pkt_status == FC_STATUS_OK)
		port_statusp->sp_status |= PORT_FABRIC_PRESENT;
	    else if (fcpkt->fc_pkt_status == FC_STATUS_P_RJT)
		port_statusp->sp_status &= ~PORT_FABRIC_PRESENT;
	    else if (fcpkt->fc_pkt_status == FC_STATUS_ERR_OFFLINE) {
		port_statusp->sp_status |= PORT_LOGIN_RECOVERY;
		mutex_exit(&port_statusp->sp_mtx);
		return;
	    } else {
		port_statusp->sp_status |= PORT_LOGIN_RECOVERY;
		mutex_exit(&port_statusp->sp_mtx);
		soc_login_recovery(port_statusp);
		return;
	    }

	    mutex_exit(&port_statusp->sp_mtx);

	    if (soc_login(port_statusp->sp_state, port_statusp->sp_port,
			NPORT_FLAG, 0) != DDI_SUCCESS) {
		mutex_enter(&port_statusp->sp_mtx);
		port_statusp->sp_status |= PORT_LOGIN_RECOVERY;
		mutex_exit(&port_statusp->sp_mtx);
		soc_login_recovery(port_statusp);
	    }
	}
}

/*
 * soc_login_recovery() - try to recover from a login failure
 *	(in non-polled mode)
 */
static void
soc_login_recovery(soc_port_t *port_statusp)
{
	mutex_enter(&port_statusp->sp_mtx);
	if (port_statusp->sp_status & PORT_OFFLINE) {
	    mutex_exit(&port_statusp->sp_mtx);
	    return;
	}

	if (port_statusp->login_timer_running)
	    untimeout(port_statusp->login_timeout_id);

	mutex_exit(&port_statusp->sp_mtx);

	if (soc_force_offline(port_statusp, 0) != DDI_SUCCESS) {
	    mutex_enter(&port_statusp->sp_mtx);
	    if (port_statusp->sp_status & PORT_OFFLINE) {
		mutex_exit(&port_statusp->sp_mtx);
		return;
	    }
	    port_statusp->login_timer_running = 0;
	    untimeout(port_statusp->login_timeout_id);
	    mutex_exit(&port_statusp->sp_mtx);
	    (void) soc_doreset(port_statusp->sp_state);
	}
}

/*
 * soc_login_rvy_timeout() - process timeout of a login recovery
 */
static void
soc_login_rvy_timeout(caddr_t arg)
{
	soc_port_t	*port_statusp = (soc_port_t *)arg;

	(void) soc_doreset(port_statusp->sp_state);
}

/*
 * soc_login_retry() - retry a login procedure following recovery actions
 */
static void
soc_login_retry(soc_port_t *port_statusp)
{
	auto char	buf[80];

	/*
	 * Have we tried to log in too many times yet?
	 */
	if (port_statusp->sp_login_retries-- <= 0) {
	    sprintf(buf, "!login retry count exceeded for port: %d",
		port_statusp->sp_port);
	    soc_disp_err(port_statusp->sp_state, CE_WARN, "login.4020", buf);
	    mutex_enter(&port_statusp->sp_mtx);
	    port_statusp->sp_status &= ~PORT_LOGIN_ACTIVE;
	    mutex_exit(&port_statusp->sp_mtx);
	    return;
	}

	if (soc_login(port_statusp->sp_state, port_statusp->sp_port,
		FABRIC_FLAG, 0) != DDI_SUCCESS) {
	    mutex_enter(&port_statusp->sp_mtx);
	    port_statusp->sp_status |= PORT_LOGIN_RECOVERY;
	    mutex_exit(&port_statusp->sp_mtx);
	    soc_login_recovery(port_statusp);
	}
}

/*
 * soc_login_timeout() - process timeout of a login
 */
static void
soc_login_timeout(caddr_t arg)
{
	soc_port_t	*port_statusp = (soc_port_t *)arg;

	mutex_enter(&port_statusp->sp_mtx);

	if (!(port_statusp->sp_status & PORT_LOGIN_ACTIVE)) {
	    mutex_exit(&port_statusp->sp_mtx);
	    return;
	}

	port_statusp->login_timer_running = 0;

	mutex_exit(&port_statusp->sp_mtx);

	soc_login_recovery(port_statusp);
}

/*
 * static void
 * soc_make_fabric_login() - make fabric login structures for the designated
 *	port of the fabric.
 */

static void
soc_make_fabric_login(soc_request_t *sp, la_logi_t *logi,
	fc_pkt_extended_t *fc_fcpkt, int port)
{
	/*
	 * XXX - need to provide service parameters and ww name's
	 */
	logi->code = LS_FLOGI;

	/* Fill in the request header. */
	sp->sr_soc_hdr.sh_request_token = (u_long)fc_fcpkt;
	sp->sr_soc_hdr.sh_flags = SOC_FC_HEADER | port;
	sp->sr_soc_hdr.sh_class = 2;
	sp->sr_soc_hdr.sh_seg_cnt = 2;
	sp->sr_soc_hdr.sh_byte_cnt = sizeof (la_logi_t);

	/*
	 * Fill in the parts of the data segment structures
	 * we know.
	 */
	sp->sr_dataseg[0].fc_count = sizeof (la_logi_t);
	sp->sr_dataseg[1].fc_count = sizeof (la_logi_t);
	sp->sr_dataseg[2].fc_base = 0;
	sp->sr_dataseg[2].fc_count = 0;

	fc_fcpkt->fpe_pkt.fc_pkt_cmd = &sp->sr_dataseg[0];
	fc_fcpkt->fpe_pkt.fc_pkt_rsp = &sp->sr_dataseg[1];

	/* Fill in the Fabric Channel Header */
	sp->sr_fc_frame_hdr.r_ctl = R_CTL_ELS_REQ;
	sp->sr_fc_frame_hdr.d_id = FS_FABRIC_F_PORT;
	sp->sr_fc_frame_hdr.s_id = 0;
	sp->sr_fc_frame_hdr.type = TYPE_EXTENDED_LS;
	sp->sr_fc_frame_hdr.f_ctl = F_CTL_SEQ_INITIATIVE | F_CTL_FIRST_SEQ;
	sp->sr_fc_frame_hdr.seq_id = 0;
	sp->sr_fc_frame_hdr.df_ctl  = 0;
	sp->sr_fc_frame_hdr.seq_cnt = 0;
	sp->sr_fc_frame_hdr.ox_id = 0xffff;
	sp->sr_fc_frame_hdr.rx_id = 0xffff;
	sp->sr_fc_frame_hdr.ro = 0;

	/* Fill in the Circular Queue Header */
	sp->sr_cqhdr.cq_hdr_count = 1;
	sp->sr_cqhdr.cq_hdr_type = CQ_TYPE_SIMPLE;
	sp->sr_cqhdr.cq_hdr_flags = 0;
	sp->sr_cqhdr.cq_hdr_seqno = 0;

	SOCDEBUG(3, soc_debug_soc_header(&sp->sr_soc_hdr));
	SOCDEBUG(3, soc_debug_fc_frame_header(&sp->sr_fc_frame_hdr));
	SOCDEBUG(3, soc_debug_cq_header(&sp->sr_cqhdr));
}


/*
 * static void
 * soc_make_nport_login() - This routine fills in the data structures for
 *	a soc nport login attempt.  The difference here is that the
 *	nport id's are assigned by us since there is no fabric present.
 *
 *	Returns:	NONE
 */

static void
soc_make_nport_login(soc_state_t *socp, soc_request_t *sp, la_logi_t *logi,
	fc_pkt_extended_t *fc_fcpkt, int port)
{
	int instance = ddi_get_instance(socp->dip);

	DEBUGF(2, (CE_CONT, "soc%d: make_nport_login: port is (%d)\n",
		instance, port));

	socp->port_status[port].sp_dst_id =
		port + 0x200 + ddi_get_instance(socp->dip);
	socp->port_status[port].sp_src_id =
		port + 0x100 + ddi_get_instance(socp->dip);

	logi->code = LS_PLOGI;

	/*
	 * Set up SOC nports ww name.
	 */
	socp->soc_ww_name.w.naa_id = NAA_ID_IEEE_EXTENDED;
	socp->soc_ww_name.w.wwn_hi = (u_char)port << 8;

	bcopy((caddr_t)&socp->soc_ww_name, (caddr_t)&logi->nport_ww_name,
		sizeof (la_wwn_t));

	socp->soc_ww_name.w.naa_id = NAA_ID_IEEE;
	socp->soc_ww_name.w.wwn_hi = 0;

	bcopy((caddr_t)&socp->soc_ww_name, (caddr_t)&logi->node_ww_name,
		sizeof (la_wwn_t));

	/*
	 * Set up nports service parameters.
	 */
	socp->soc_service_params[4] &= ~SP_F_PORT_LOGIN;
	bcopy((caddr_t)socp->soc_service_params,
		(caddr_t)&logi->common_service, SOC_SVC_LENGTH);

	/*
	 * Fill in the request header.
	 */
	sp->sr_soc_hdr.sh_request_token = (u_int)fc_fcpkt;
	sp->sr_soc_hdr.sh_flags = SOC_FC_HEADER | port;
	sp->sr_soc_hdr.sh_class = 2;
	sp->sr_soc_hdr.sh_seg_cnt = 2;
	sp->sr_soc_hdr.sh_byte_cnt = sizeof (la_logi_t);

	/*
	 * Fill in the parts of the data segment structures
	 * we know.
	 */
	sp->sr_dataseg[0].fc_count = sizeof (la_logi_t);
	sp->sr_dataseg[1].fc_count = sizeof (la_logi_t);
	sp->sr_dataseg[2].fc_base = 0;
	sp->sr_dataseg[2].fc_count = 0;

	fc_fcpkt->fpe_pkt.fc_pkt_cmd = &sp->sr_dataseg[0];
	fc_fcpkt->fpe_pkt.fc_pkt_rsp = &sp->sr_dataseg[1];

	/* Fill in the Fabric Channel Header */
	sp->sr_fc_frame_hdr.r_ctl = R_CTL_ELS_REQ;
	sp->sr_fc_frame_hdr.d_id = socp->port_status[port].sp_dst_id;
	sp->sr_fc_frame_hdr.s_id = socp->port_status[port].sp_src_id;
	sp->sr_fc_frame_hdr.type = TYPE_EXTENDED_LS;
	sp->sr_fc_frame_hdr.f_ctl = F_CTL_SEQ_INITIATIVE | F_CTL_FIRST_SEQ;
	sp->sr_fc_frame_hdr.seq_id = 0;
	sp->sr_fc_frame_hdr.df_ctl  = 0;
	sp->sr_fc_frame_hdr.seq_cnt = 0;
	sp->sr_fc_frame_hdr.ox_id = 0xffff;
	sp->sr_fc_frame_hdr.rx_id = 0xffff;
	sp->sr_fc_frame_hdr.ro = 0;

	/* Fill in the Circular Queue Header */
	sp->sr_cqhdr.cq_hdr_count = 1;
	sp->sr_cqhdr.cq_hdr_type = CQ_TYPE_SIMPLE;
	sp->sr_cqhdr.cq_hdr_flags = 0;
	sp->sr_cqhdr.cq_hdr_seqno = 0;

	SOCDEBUG(3, soc_debug_soc_header(&sp->sr_soc_hdr));
	SOCDEBUG(3, soc_debug_fc_frame_header(&sp->sr_fc_frame_hdr));
	SOCDEBUG(3, soc_debug_cq_header(&sp->sr_cqhdr));
}

/*
 * Validate the received login parameters
 *
 * Returns:
 *	nonzero if successful
 *	0 otherwise
 */
static int
soc_login_check(soc_port_t *port_statusp, la_logi_t *lp)
{
	auto char buf[80];
	la_wwn_t node;
	u_int wwn_hi, wwn_lo;
	la_wwn_t *cptr;

	bcopy((caddr_t)&lp->node_ww_name, (caddr_t)&node, sizeof (la_wwn_t));

	cptr = &port_statusp->sp_d_wwn;
	wwn_hi = cptr->w.wwn_hi;
	wwn_lo = cptr->w.wwn_lo;

	if (!wwn_hi && !wwn_lo) {
	    bcopy((caddr_t)&lp->node_ww_name, (caddr_t)&port_statusp->sp_d_wwn,
		sizeof (la_wwn_t));
	} else {
	    if (((node.w.wwn_hi != wwn_hi) || (node.w.wwn_lo != wwn_lo)) &&
			!soc_disable_wwn_check) {
		sprintf(buf,
		"INCORRECT WWN: Found: 0x%04x,%08x Expected: 0x%04x,%08x",
		    node.w.wwn_hi, node.w.wwn_lo, wwn_hi, wwn_lo);
		soc_disp_err(port_statusp->sp_state, CE_WARN, "wwn.5020", buf);
		return (0);
	    }
	}
	return (1);
}

/*
 * static int
 * soc_start() - this routine resets the SOC, loads its descriptors, and
 *	starts the login process.
 */
static int
soc_start(soc_state_t *socp)
{
	soc_port_t	*port_statusp0,
			*port_statusp1;

	if (!socp)
		return (DDI_FAILURE);

	soc_download_fw(socp);
	soc_init_cq_desc(socp);	/* initialize the XRAM queue descriptors */

	/* Get own copy of service parameters. */
	soc_hcopy((u_short *) (socp->socxrp + SOC_XRAM_SERVICE_PARAMS),
		(u_short *) socp->soc_service_params, SOC_SVC_LENGTH);

	/*
	 * Say the login process is started.
	 * Say the ports are off-line since reset
	 */
	port_statusp0 = &socp->port_status[0];
	port_statusp1 = &socp->port_status[1];
#ifndef	__lock_lint
	port_statusp0->sp_status = PORT_LOGIN_ACTIVE | PORT_OFFLINE;
	port_statusp1->sp_status = PORT_LOGIN_ACTIVE | PORT_OFFLINE;
#endif	__lock_lint

	soc_enable(socp);	/* enable soc */

	/*
	 * Establish scratch pools for SOC to work with.
	 */
#ifdef	UNSOLICITED_POOLS
	if (soc_establish_pools(socp) != DDI_SUCCESS)
		return (DDI_FAILURE);
#endif	UNSOLICITED_POOLS

	/*
	 * If there's more than one host adapter, this will make the
	 * boot go faster, assuming all soc drivers are attached
	 * before any children are initialized.  The idea is that
	 * the online timeout in soc_start_ports() will be decreased
	 * in a particular soc driver instance if other instances have
	 * been polling for an online after this instance's host adapter
	 * was enabled.  We would use LBOLT for this, but it doesn't
	 * seem too reliable until system interrupts are enabled, well
	 * into the boot process.
	 */
	socp->init_time = soc_init_time;

	return (DDI_SUCCESS);
}

/*
 * soc_start_ports() - wait for a port to come on line
 */
void
soc_start_ports(soc_state_t *socp)
{
	int			delay_loops;
	register volatile soc_reg_t *socreg = socp->socrp;
	u_int			csr;
	soc_port_t		*port_statusp0,
				*port_statusp1;

	port_statusp0 = &socp->port_status[0];
	port_statusp1 = &socp->port_status[1];

	/*
	 * We subtract the time (in units of SOC_NOINTR_POLL_DELAY_TIME)
	 * that has elapsed since we enabled the hardware
	 * from the initial on line timeout delay for faster booting.
	 */
	delay_loops = SOC_TIMEOUT_DELAY(SOC_INITIAL_ONLINE,
					SOC_NOINTR_POLL_DELAY_TIME) -
			(soc_init_time - socp->init_time);
	if (delay_loops <= 0)
		delay_loops = 1;

	while ((delay_loops-- > 0) &&
			(!(port_statusp0->sp_status & NPORT_LOGIN_SUCCESS) ||
			!(port_statusp1->sp_status & NPORT_LOGIN_SUCCESS))) {

		drv_usecwait(SOC_NOINTR_POLL_DELAY_TIME);
		soc_init_time++;

		/*
		 * Poll the interrupt routine in case system interrupts
		 * are disabled
		 */
		csr = socreg->soc_csr.w;
		if (SOC_INTR_CAUSE(socp, csr)) {
			(void) soc_intr((caddr_t)socp);
		}

		/*
		 * If the port has gone on line but we haven't yet done
		 * a login, start the login.  Otherwise, if we've timed
		 * out waiting for an on line from the port, turn off
		 * the PORT_LOGIN_ACTIVE flag so that we may log in
		 * if the port goes on line at some later time.
		 */
		if ((port_statusp0->sp_status & PORT_LOGIN_ACTIVE) &&
			((delay_loops == 0) ||
			!(port_statusp0->sp_status & PORT_OFFLINE))) {

		    mutex_enter(&port_statusp0->sp_mtx);
		    if (port_statusp0->sp_status & PORT_OFFLINE) {
			port_statusp0->sp_status &= ~PORT_LOGIN_ACTIVE;
			mutex_exit(&port_statusp0->sp_mtx);
		    } else {
			mutex_exit(&port_statusp0->sp_mtx);
			soc_start_login(socp, 0);
		    }
		}

		if ((port_statusp1->sp_status & PORT_LOGIN_ACTIVE) &&
			((delay_loops == 0) ||
			!(port_statusp1->sp_status & PORT_OFFLINE))) {

		    mutex_enter(&port_statusp1->sp_mtx);
		    if (port_statusp1->sp_status & PORT_OFFLINE) {
			port_statusp1->sp_status &= ~PORT_LOGIN_ACTIVE;
			mutex_exit(&port_statusp1->sp_mtx);
		    } else {
			mutex_exit(&port_statusp1->sp_mtx);
			soc_start_login(socp, 1);
		    }
		}
	}
}

/*
 * soc_start_login() - This function does a fabric and/or
 *	n-port login on a soc port.
 *
 *	port: Port to do the login on
 *
 *	Returns: DDI_SUCCESS
 *		DDI_FAILURE
 *		It also sets status in the port status structure
 */
static void
soc_start_login(soc_state_t *socp, int port)
{
	soc_port_t *port_statusp = &socp->port_status[port];
	int flogin_ok;
	int i;
	soc_priv_cmd_t		*privp;
	int instance = ddi_get_instance(socp->dip);
	soc_statec_cb_t		*cbp;

	DEBUGF(2, (CE_CONT, "soc%d: start_login: Port %d\n", instance, port));

	port_statusp->sp_login_retries = SOC_LOGIN_RETRIES;

	while (port_statusp->sp_login_retries > 0) {

	/*
	 * First, try a fabric login
	 */
	    flogin_ok = 1;
	    switch (soc_login(socp, port, FABRIC_FLAG, 1)) {
		case FC_STATUS_OK:
			mutex_enter(&port_statusp->sp_mtx);
			port_statusp->sp_status |= PORT_FABRIC_PRESENT;
			mutex_exit(&port_statusp->sp_mtx);
			soc_disp_err(socp, CE_WARN, "link.4080",
		    "!Connections via a Fibre Channel Fabric are unsupported");
			break;

		case FC_STATUS_P_RJT:
			break;

		case FC_STATUS_ERR_OFFLINE:
			goto fail;

		case SOC_LOGIN_TIMEOUT_FLAG:
			port_statusp->sp_login_retries--;
			flogin_ok = 0;
			break;

		default:
			soc_disp_err(socp, CE_WARN, "login.5020",
				"!fabric login failed");
			goto fail;
	    }

	/*
	 * Do an n-port login.
	 */
	    if (flogin_ok) {
		privp = (soc_priv_cmd_t *)
			port_statusp->sp_login->fpe_pkt.fc_pkt_private;

		switch (soc_login(socp, port, NPORT_FLAG, 1)) {
		    case SOC_OK:
			i = soc_login_check(port_statusp,
						(la_logi_t *)privp->resp);
			mutex_enter(&port_statusp->sp_mtx);

			if (i)
			    port_statusp->sp_status |= NPORT_LOGIN_SUCCESS;
			port_statusp->sp_status &= ~PORT_LOGIN_ACTIVE;
			mutex_exit(&port_statusp->sp_mtx);
			if (i) {
			    soc_disp_err(port_statusp->sp_state, CE_CONT,
				"login.6010",
				"!Fibre Channel login succeeded\n");
			    for (cbp = port_statusp->state_cb; cbp;
				cbp = cbp->next) {
				(*cbp->callback)(cbp->arg, FC_STATE_ONLINE);
			    }
			} else {
			    soc_disp_err(port_statusp->sp_state, CE_CONT,
				"login.5010",
				"!Fibre Channel login failed\n");
			}
			return;

		    case FC_STATUS_ERR_OFFLINE:
			goto fail;

		    case SOC_LOGIN_TIMEOUT_FLAG:
			port_statusp->sp_login_retries--;
			break;

		    case SOC_P_RJT:
			soc_disp_err(socp, CE_WARN, "login.5030",
				"!N-PORT login not successful");
			goto fail;

		    default:
			soc_disp_err(socp, CE_WARN, "login.5040",
				"!N-PORT login failure\n");
			goto fail;
		}
	    }
	}

	soc_disp_err(socp, CE_WARN, "login.4040",
		"!login retry count exceeded");
	/* XXX: for which port? */

fail:
	mutex_enter(&port_statusp->sp_mtx);
	port_statusp->sp_status &= ~PORT_LOGIN_ACTIVE;
	mutex_exit(&port_statusp->sp_mtx);
}


/*
 * Circular Queue Management routines.
 */

/*
 * Function name : soc_cq_enque()
 *
 * Return Values :
 *		FC_TRANSPORT_SUCCESS, if able to que the entry.
 *		FC_TRANSPORT_QFULL, if queue full & sleep not set
 *
 * Description	 : Enqueues an entry into the solicited request
 *		   queue
 *
 * Context	:
 */

static int
soc_cq_enque(soc_state_t *socp, soc_port_t *port_statusp, cqe_t *cqe,
		int rqix, fc_sleep_t sleep,
		fc_pkt_extended_t *to_queue, int holding_mtx)
{
	int instance = ddi_get_instance(socp->dip);
	soc_kcq_t	*kcq;
	cqe_t		*sp;
	u_int		bitmask;
	union		short_out {
		volatile u_short	s;
		struct	{
		    u_char	dummy;
		    u_char	out;
		} s_t;
	} s_out;
	u_char		out;

	kcq = &socp->request[rqix];

	/*
	 * Grab lock for request queue.
	 */
#ifndef	__lock_lint
	if (!holding_mtx)
#endif	__lock_lint
	    mutex_enter(&kcq->skc_mtx);

	if ((socp->soc_shutdown) ||
	    (port_statusp &&
		!(port_statusp->sp_status & NPORT_LOGIN_SUCCESS))) {

#ifndef	__lock_lint
	    if (!holding_mtx)
#endif	__lock_lint
		mutex_exit(&kcq->skc_mtx);

	    return (FC_TRANSPORT_UNAVAIL);
	}

	bitmask = SOC_CSR_1ST_H_TO_S << rqix;

	/*
	 * Determine if the queue is full
	 */

	do {

	/*
	 * When the queue is full and an fc_pkt_extended structure
	 * is specified, then we'll place the item in the overflow
	 * queue.  If no fc_pkt_extended is sent to us, then we
	 * need to try to sleep instead.
	 */
	    if (kcq->skc_full) {

		    if (to_queue) {

			to_queue->fpe_next = NULL;
			if (!kcq->skc_overflowh) {
			    kcq->skc_overflowh = to_queue;
			    DEBUGF(2, (CE_CONT,
			    "soc%d: cq_enque: overflow on request queue %d\n",
				instance, rqix));
			} else
			    kcq->skc_overflowt->fpe_next = to_queue;
			kcq->skc_overflowt = to_queue;

			mutex_enter(&socp->k_imr_mtx);
			socp->socrp->soc_imr = (socp->k_soc_imr |= bitmask);
			mutex_exit(&socp->k_imr_mtx);
#ifndef	__lock_lint
			if (!holding_mtx)
#endif	__lock_lint
			    mutex_exit(&kcq->skc_mtx);

			return (FC_TRANSPORT_SUCCESS);
		    }

		    if (sleep != FC_SLEEP) {
#ifndef	__lock_lint
			    if (!holding_mtx)
#endif	__lock_lint
				mutex_exit(&kcq->skc_mtx);

			    return (FC_TRANSPORT_QFULL);
		    }

		/*
		 * If soc's que full, then we wait for an interrupt
		 * telling us we are not full.
		 */
		    mutex_enter(&socp->k_imr_mtx);
		    socp->socrp->soc_imr = (socp->k_soc_imr |= bitmask);
		    mutex_exit(&socp->k_imr_mtx);
		    DEBUGF(2, (CE_CONT,
			    "soc%d: cq_enque: request que %d is full: wait\n",
			    instance, rqix));

		    while (kcq->skc_full) {
			kcq->skc_full |= SOC_SKC_SLEEP;
			cv_wait(&kcq->skc_cv, &kcq->skc_mtx);
			if (socp->soc_shutdown) {
#ifndef	__lock_lint
			    if (!holding_mtx)
#endif	__lock_lint
				mutex_exit(&kcq->skc_mtx);

			    return (FC_TRANSPORT_UNAVAIL);
			}
		    }
	    }

	    if (((kcq->skc_in + 1) & kcq->skc_last_index)
			== (out = kcq->skc_out)) {
		/*
		 * get SOC's copy of out to update our copy of out
		 *
		 * The way this works is I do a short fetch
		 * from "cq_in" to use an aligned address.
		 * I assume cq_in is aligned on a short boundry.
		 *
		 * We only get the soc's copy when the queue wraps
		 * in order to minimize accesses to the XRAM because
		 * accesses to XRAM are expensive.
		 */
		s_out.s = *(u_short *)&kcq->skc_xram_cqdesc->cq_in;
		DEBUGF(2, (CE_CONT,
			"soc%d: cq_enque: &XRAM cq_in: 0x%x s_out.out 0x%x\n",
			instance, (int)&kcq->skc_xram_cqdesc->cq_in, s_out.s));

		kcq->skc_out = out = s_out.s_t.out;
		/* if soc's que still full set flag */
		kcq->skc_full = ((((kcq->skc_in + 1) &
			kcq->skc_last_index) == out)) ? SOC_SKC_FULL : 0;
	    }

	} while (kcq->skc_full);

	DEBUGF(2, (CE_CONT,
	"soc%d: cq_enque:  kin: 0x%x kout: 0x%x xram_out: 0x%x seqno: 0x%x\n",
		instance, kcq->skc_in, kcq->skc_out, out, kcq->skc_seqno));

	/* Now enque the entry. */
	sp = &(kcq->skc_cq[kcq->skc_in]);
	cqe->cqe_hdr.cq_hdr_seqno = kcq->skc_seqno;

	/* Give the entry to the SOC. */
	DEBUGF(2, (CE_CONT, "packet 0x%x enqued\n", *(int *)cqe));
	bcopy((caddr_t)cqe, (caddr_t)sp, sizeof (cqe_t));
	ddi_dma_sync(kcq->skc_dhandle, 0, 0, DDI_DMA_SYNC_FORDEV);

	DEBUGF(4, (CE_CONT,
		"soc%d: cq_enque: request entry %d kvm address: 0x%x\n",
		instance, kcq->skc_in, (int)sp));

	/*
	 * Update circular queue and ring SOC's doorbell.
	 */
	kcq->skc_in++;
	if ((kcq->skc_in & kcq->skc_last_index) == 0) {
		kcq->skc_in = 0;
		kcq->skc_seqno++;
	}

	DEBUGF(3, (CE_CONT,
		"soc%d: cq_enque: Ring soc's doorbell, bitmask: 0x%x\n",
		instance,
		(SOC_CSR_SOC_TO_HOST | (kcq->skc_in << 24) | bitmask)));
	socp->socrp->soc_csr.w =
		SOC_CSR_SOC_TO_HOST | (kcq->skc_in << 24) | bitmask;

	/* Let lock go for request queue. */
#ifndef	__lock_lint
	if (!holding_mtx)
#endif	__lock_lint
	    mutex_exit(&kcq->skc_mtx);

	return (FC_TRANSPORT_SUCCESS);
}


/*
 * Function name : soc_hcopy()
 *
 * Return Values :  none
 *
 * Description	 : Does half word (short) copies
 *		   len must be in bytes
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
static	void
soc_hcopy(u_short * h_src, u_short * h_dest, int len)
{
	int	i;

	DEBUGF(2, (CE_CONT,
		"soc_hcopy: src: 0x%x dest: 0x%x len: 0x%x\n",
		(int)h_src, (int)h_dest, len));

	for (i = 0; i < len/2; i++) {
		*h_dest++ = *h_src++;
	}
}


/*
 * Function name : soc_disp_err()
 *
 * Return Values : none
 *
 * Description   : displays an error message on the system console
 *		   with the full device pathname displayed
 */
static void
soc_disp_err(
	soc_state_t	*socp,
	u_int		level,
	char		*mid,
	char		*msg)
{
	char c;
	int instance;

	instance = ddi_get_instance(socp->dip);

	c = *msg;

	if (c == '!')		/* log only */
	    cmn_err(level,
		"!ID[SUNWssa.soc.%s] soc%d: %s", mid, instance, msg+1);
	else if (c == '?')	/* boot message - log && maybe console */
	    cmn_err(level,
		"?ID[SUNWssa.soc.%s] soc%d: %s", mid, instance, msg+1);
	else if (c == '^')	/* console only */
	    cmn_err(level, "^soc%d: %s", instance, msg+1);
	else	{		/* log and console */
	    cmn_err(level, "^soc%d: %s", instance, msg);
	    cmn_err(level, "!ID[SUNWssa.soc.%s] soc%d: %s", mid, instance, msg);
	}
}

#ifdef SOC_UDUMP

/*
 * Read an image of the soc xram and put it in kernel memory
 */
static void
soc_udump(
	soc_state_t *socp)
{
	/* Get a buffer if we don't already have one */
	if (!soc_udump_buf) {
	    if (!(soc_udump_buf = kmem_zalloc(SOC_XRAM_SIZE, KM_SLEEP))) {
		soc_disp_err(socp, CE_WARN,
		"ucode.0010", "DEBUG:  couldn't get soc ucode dump");
		return;
	    }
	}

	soc_hcopy((u_short *)socp->socxrp, (u_short *)soc_udump_buf,
			SOC_XRAM_SIZE);
	soc_disp_err(socp, CE_CONT, "ucode.0020", "did soc ucode dump\n");
	soc_udump_p = -1;
}

/*
 * Run from a timer to check if we require a ucode dump
 */
static void
soc_udump_watch()

{
	soc_state_t *socp;

	if (soc_udump_p >= 0) {
	    socp = ddi_get_soft_state(soc_soft_state_p, soc_udump_p);
	    if (!socp) {
		cmn_err(CE_WARN, "DEBUG: invalid soc ucode instance number\n");
		soc_udump_p = -1;
		return;
	    }
	    soc_udump(socp);
	}

	soc_udump_id = timeout(soc_udump_watch, (caddr_t)0, soc_udump_tick);
}

#endif SOC_UDUMP

#ifdef DEBUG
static void
soc_debug_soc_header(soc_header_t *shp)
{
	cmn_err(CE_CONT, "SOC Header.0x%x: \n", (int)shp);
	cmn_err(CE_CONT, "\ttoken: 0x%x\n", (int)shp->sh_request_token);
	cmn_err(CE_CONT, "\tflags: 0x%x\n", shp->sh_flags);
	cmn_err(CE_CONT, "\tclass: 0x%x\n", shp->sh_class);
	cmn_err(CE_CONT, "\tseg_cnt: 0x%x\n", shp->sh_seg_cnt);
	cmn_err(CE_CONT, "\tbyte_cnt: 0x%x\n", (int)shp->sh_byte_cnt);
}

static void
soc_debug_cq_header(cq_hdr_t *cqp)
{
	cmn_err(CE_CONT, "CQ header.0x%x:\n", (int)cqp);
	cmn_err(CE_CONT, "\tcount: 0x%x\n", cqp->cq_hdr_count);
	cmn_err(CE_CONT, "\ttype: 0x%x\n", cqp->cq_hdr_type);
	cmn_err(CE_CONT, "\tflags: 0x%x\n", cqp->cq_hdr_flags);
	cmn_err(CE_CONT, "\tseq_no: 0x%x\n", cqp->cq_hdr_seqno);
}

static void
soc_debug_fc_frame_header(fc_frame_header_t *fcp)
{
	cmn_err(CE_CONT, "Frame Header.0x%x:\n", (int)fcp);
	cmn_err(CE_CONT, "\tr_ctl: 0x%x\n", fcp->r_ctl);
	cmn_err(CE_CONT, "\td_id: 0x%x\n", fcp->d_id);
	cmn_err(CE_CONT, "\ts_id: 0x%x\n", fcp->s_id);
	cmn_err(CE_CONT, "\ttype: 0x%x\n", fcp->type);
	cmn_err(CE_CONT, "\tf_ctl: 0x%x\n", fcp->f_ctl);
	cmn_err(CE_CONT, "\tseq_id: 0x%x\n", fcp->seq_id);
	cmn_err(CE_CONT, "\tseq_cnt: 0x%x\n", fcp->seq_cnt);
	cmn_err(CE_CONT, "\tox_id: 0x%x\n", fcp->ox_id);
	cmn_err(CE_CONT, "\trx_id: 0x%x\n", fcp->rx_id);
	cmn_err(CE_CONT, "\tro: 0x%x\n", fcp->ro);
}

static void
soc_debug_soc_response(soc_response_t *srp)
{
	soc_debug_soc_header(&srp->sr_soc_hdr);
	soc_debug_fc_frame_header(&srp->sr_fc_frame_hdr);
}


/*
 * DEBUG function
 *
 * Function name : soc_debug_soc_status()
 *
 * Return Values :  none
 *
 * Description	 :
 * 	Print soc_status
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
static void
soc_debug_soc_status(u_int status)
{
	int i;

	cmn_err(CE_CONT, "SOC Status = ");

	if (status == SOC_P_RJT || status == SOC_MAX_XCHG_EXCEEDED)
		return;

	switch (status) {
		case SOC_OK:
			cmn_err(CE_CONT, "SOC_OK\n");
			break;
		case SOC_P_RJT:
			break;
		case SOC_F_RJT:
			cmn_err(CE_CONT, "SOC_F_RJT\n");
			break;
		case SOC_P_BSY:
			cmn_err(CE_CONT, "SOC_P_BSY\n");
			break;
		case SOC_F_BSY:
			cmn_err(CE_CONT, "SOC_F_BSY\n");
			break;
		case SOC_ONLINE:
			cmn_err(CE_CONT, "SOC_ONLINE\n");
			break;
		case SOC_OFFLINE:
			cmn_err(CE_CONT, "SOC_OFFLINE\n");
			break;
		case SOC_TIMEOUT:
			cmn_err(CE_CONT, "SOC_TIMEOUT\n");
			break;
		case SOC_OVERRUN:
			cmn_err(CE_CONT, "SOC_OVERRUN\n");
			break;
		case SOC_UNKOWN_CQ_TYPE:
			cmn_err(CE_CONT, "SOC_UNKOWN_CQ_TYPE\n");
			break;
		case SOC_BAD_SEG_CNT:
			cmn_err(CE_CONT, "SOC_BAD_SEG_CNT\n");
			break;
		case SOC_MAX_XCHG_EXCEEDED:
			break;
		case SOC_BAD_XID:
			cmn_err(CE_CONT, "SOC_BAD_XID\n");
			break;
		case SOC_XCHG_BUSY:
			cmn_err(CE_CONT, "SOC_XCHG_BUSY\n");
			break;
		case SOC_BAD_POOL_ID:
			cmn_err(CE_CONT, "SOC_BAD_POOL_ID\n");
			break;
		case SOC_INSUFFICIENT_CQES:
			cmn_err(CE_CONT, "SOC_INSUFFICIENT_CQES\n");
			break;
		case SOC_ALLOC_FAIL:
			cmn_err(CE_CONT, "SOC_ALLOC_FAIL\n");
			break;
		case SOC_BAD_SID:
			cmn_err(CE_CONT, "SOC_BAD_SID\n");
			break;
		case SOC_NO_SEG_INIT:
			cmn_err(CE_CONT, "SOC_NO_SEG_INIT\n");
			break;
		default:
			i = CE_CONT;
#ifdef	DEBUG
			if (soc_debug) i = CE_PANIC;
#endif	DEBUG
			cmn_err(i, "UNKNOWN 0x%x\n", status);
			break;
	}
}

#endif
