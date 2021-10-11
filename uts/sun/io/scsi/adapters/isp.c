/*
 * Copyright (c) 1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */
#pragma ident	"@(#)isp.c	1.119	95/09/08 SMI"

/*
 * isp - Emulex Intelligent SCSI Processor driver
 */

#if defined(lint) && !defined(DEBUG)
#define	DEBUG	1
#endif

#ifdef DEBUG
#define	ISPDEBUG
static int ispdebug = 0;
static int isp_enable_brk_fatal = 0;
static int isp_timeout_debug = 0;
#include <sys/debug.h>
#endif

/*
 * patch in case of hw problems
 */
static int isp_burst_sizes_limit = 0xff;

#include <sys/modctl.h>
#include <sys/scsi/scsi.h>

#include <sys/scsi/adapters/ispmail.h>
#include <sys/scsi/adapters/ispvar.h>
#include <sys/scsi/adapters/ispreg.h>
#include <sys/scsi/adapters/ispcmd.h>
#include <sys/scsi/adapters/reset_notify.h>
/*
 * NON-ddi compliant include files
 */
#include <sys/utsname.h>
#include <sys/kmem.h>

/*
 * ISP firmware download options:
 *	ISP_DOWNLOAD_FW_OFF		=> no download
 *	ISP_DOWNLOAD_FW_IF_NEWER	=>
 *		download if f/w level > current f/w level [Default]
 *	ISP_DOWNLOAD_FW_ALWAYS		=> always download
 */
int isp_download_fw = ISP_DOWNLOAD_FW_ALWAYS;

#include <sys/vtrace.h>

/*
 * External references
 */
extern u_char	scsi_cdb_size[];

/*
 * Firmware related externs
 */
extern u_short isp_risc_code_addr;
extern u_short isp_risc_code[];
extern u_short isp_risc_code_length;

#ifdef OLDTIMEOUT
/*
 * non-ddi compliant
 * lbolt is used for packet timeout handling
 */
extern clock_t	lbolt;
#endif

/*
 * dev_ops functions prototypes
 */
static int isp_info(dev_info_t *dip, ddi_info_cmd_t infocmd,
    void *arg, void **result);
static int isp_identify(dev_info_t *dip);
static int isp_attach(dev_info_t *dip, ddi_attach_cmd_t cmd);
static int isp_detach(dev_info_t *dip, ddi_detach_cmd_t cmd);

/*
 * Function prototypes
 *
 * SCSA functions exported by means of the transport table
 */
static int isp_scsi_start(struct scsi_address *ap, struct scsi_pkt *pkt);
static int isp_scsi_abort(struct scsi_address *ap, struct scsi_pkt *pkt);
static int isp_scsi_reset(struct scsi_address *ap, int level);
static int isp_scsi_getcap(struct scsi_address *ap, char *cap, int whom);
static int isp_scsi_setcap(struct scsi_address *ap, char *cap, int value,
    int whom);
static struct scsi_pkt *isp_scsi_init_pkt(struct scsi_address *ap,
    struct scsi_pkt *pkt, struct buf *bp, int cmdlen, int statuslen,
    int tgtlen, int flags, int (*callback)(), caddr_t arg);
static void isp_scsi_destroy_pkt(struct scsi_address *ap, struct scsi_pkt *pkt);
static void isp_scsi_dmafree(struct scsi_address *ap, struct scsi_pkt *pkt);
static void isp_scsi_sync_pkt(struct scsi_address *ap, struct scsi_pkt *pkt);
static int isp_scsi_reset_notify(struct scsi_address *ap, int flag,
    void (*callback)(caddr_t), caddr_t arg);

/*
 * isp interrupt handlers
 */
static u_int isp_intr(caddr_t arg);
static u_int isp_intr_loop(caddr_t arg);

/*
 * internal functions
 */
static int isp_i_commoncap(struct scsi_address *ap, char *cap, int val,
    int tgtonly, int doset);
static int isp_i_updatecap(struct isp *isp, int start_tgt, int end_tgt);
static void isp_i_update_props(struct isp *isp, int tgt, u_short cap,
    u_short synch);
static void isp_i_update_this_prop(struct isp *isp, char *property,
    int value, int size, int flag);
static void isp_i_update_sync_prop(struct isp *isp, struct isp_cmd *sp);
static void isp_i_initcap(struct isp *isp);

static void isp_i_watch();
static void isp_i_watch_isp(register struct isp *isp);
#ifdef OLDTIMEOUT
static void isp_i_old_watch_isp(register struct isp *isp, u_long local_lbolt);
static int isp_i_is_fatal_timeout(struct isp *isp, struct isp_cmd *sp);
#endif
static void isp_i_fatal_error(struct isp *isp);

static void isp_i_empty_waitQ(struct isp *isp);
static int isp_i_start_cmd(struct isp *isp, struct isp_cmd *sp);
static int isp_i_find_freeslot(struct isp *isp);
static int isp_i_polled_cmd_start(struct isp *isp, struct isp_cmd *sp);
static void isp_i_call_pkt_comp(register struct isp_cmd *head);
static void isp_i_handle_arq(struct isp *isp, struct isp_cmd *sp);
static int isp_i_handle_mbox_cmd(struct isp *isp);

static void isp_i_qflush(register struct isp *isp,
    u_short start_tgt, u_short end_tgt);
static int isp_i_reset_interface(register struct isp *isp, int action);
static int isp_i_set_marker(register struct isp *isp,
    short mod, short tgt, short lun);

static void isp_i_log(struct isp *isp, int level, char *fmt, ...);
static void isp_i_print_state(struct isp *isp);

static int isp_i_async_event(register struct isp *isp, short event);
static int isp_i_response_error(struct isp *isp, struct isp_response *resp);

static void isp_i_mbox_cmd_init(struct isp *isp,
    struct isp_mbox_cmd *mbox_cmdp, u_char n_mbox_out, u_char n_mbox_in,
    u_short reg0, u_short reg1, u_short reg2,
    u_short reg3, u_short reg4, u_short reg5);
static int isp_i_mbox_cmd_start(struct isp *isp,
    struct isp_mbox_cmd *mbox_cmdp);
static void isp_i_mbox_cmd_complete(struct isp *isp);

static int isp_i_download_fw(struct isp *isp,
    u_short risc_addrp, u_short *fw_addrp, u_short fw_len);
static int isp_i_alive(struct isp *isp);
static int isp_i_handle_qfull_cap(struct isp *isp,
	u_short start, u_short end, int val,
	int flag_get_set, int flag_cmd);

static int isp_i_pkt_alloc_extern(struct isp *isp, struct isp_cmd *sp,
	int cmdlen, int tgtlen, int statuslen, int kf);
static void isp_i_pkt_destroy_extern(struct isp *isp, struct isp_cmd *sp);

#ifdef ISPDEBUG
static void isp_i_test(struct isp *isp, struct isp_cmd *sp);
#endif
/*
 * kmem cache constuctor and destructor
 */
static void isp_kmem_cache_constructor(void * buf, size_t size);
static void isp_kmem_cache_destructor(void * buf, size_t size);

/*
 * waitQ macros, refer to comments on waitQ below
 */
#define	ISP_CHECK_WAITQ(isp)				\
	mutex_enter(ISP_WAITQ_MUTEX(isp));		\
	isp_i_empty_waitQ(isp);

#define	ISP_CHECK_WAITQ_AND_EXIT_REQ_MUTEX(isp)		\
	ISP_CHECK_WAITQ(isp)				\
	mutex_exit(ISP_REQ_MUTEX(isp));			\
	mutex_exit(ISP_WAITQ_MUTEX(isp));

/*
 * mutex for protecting variables shared by all instances of the driver
 */
static kmutex_t isp_global_mutex;

/*
 * Local static data
 */
static void *isp_state = NULL;
static struct isp *isp_head;	/* for linking softc structures */
static struct isp *isp_tail;	/* for linking softc structures */
static struct isp *isp_last;	/* for isp_intr_loop() */
static int isp_scsi_watchdog_tick; /* isp_i_watch() interval in sec */
static int isp_tick;		/* isp_i_watch() interval in HZ */
static int isp_timeout_id;	/* isp_i_watch() timeout id */
static int isp_selection_timeout = 250;


/*
 * default isp dma attr structure describes device
 * and DMA engine specific attributes/constrains necessary
 * to allocate DMA resources for ISP device.
 */
static ddi_dma_attr_t dma_ispattr = {
	DMA_ATTR_V0, (unsigned long long)0,
	(unsigned long long)0xffffffff, (unsigned long long)0xffffff,
	1, DEFAULT_BURSTSIZE | BURST32 | BURST64, 1,
	(unsigned long long)0xffffffff, (unsigned long long)((1<<24)-1),
	1, 512, 0
};

/* VARIABLES PROTECTED BY "Interrupt Thread": ispregs::		*/
/* VARIABLES PROTECTED BY isp_global_mutex: isp::isp_next	*/
/* VARIABLES PROTECTED BY isp_global_mutex: isp_timeout_id	*/
/* VARIABLES PROTECTED BY isp_global_mutex: isp_head, isp_tail	*/
/* VARIABLES PROTECTED BY "No Mutex Needed": isp_last		*/
/* VARIABLES PROTECTED BY "No Mutex Needed": isp_response::	*/
/* VARIABLES PROTECTED BY "No Mutex Needed": isp_request::	*/
/* VARIABLES PROTECTED BY "No Mutex Needed": scsi_arq_status::	*/
/* VARIABLES PROTECTED BY "SCSI PKT": buf::			*/
/* VARIABLES PROTECTED BY "Stable Data": scsi_pkt::, isp_cmd::	*/
/* VARIABLES PROTECTED BY "Stable Data": ddi_dma_req::, isp_tick */
/* READ-ONLY VARIABLES: ispdebug, scsi_address::		*/
/* READ-ONLY VARIABLES: hz, lbolt, scsi_watchdog_tick, scsi_cdb_size */
/* READ-ONLY VARIABLES: scsi_reset_delay, scsi_hba_tran::	*/
/* FUNCTIONS CALLED THROUGH POINTER scsi_pkt::pkt_comp:
	"isp.c":isp_scsi_start */
/* FUNCTIONS CALLED THROUGH POINTER scsi_pkt::pkt_comp:
	"isp.c":isp_scsi_abort, "isp.c":isp_scsi_reset */
/* FUNCTIONS CALLED THROUGH POINTER scsi_pkt::pkt_comp:
	"isp.c":isp_scsi_getcap, "isp.c":isp_scsi_setcap */
/* FUNCTIONS CALLED THROUGH POINTER scsi_pkt::pkt_comp:
	"isp.c":isp_scsi_dmafree */
/* FUNCTIONS CALLED THROUGH POINTER scsi_pkt::pkt_comp:
	"isp.c":isp_scsi_sync_pkt */
/* FUNCTIONS CALLED THROUGH POINTER scsi_pkt::pkt_comp:
	"isp.c":isp_scsi_destroy_pkt, "isp.c":isp_scsi_init_pkt */

/*
 * autoconfiguration routines.
 */

static struct dev_ops isp_ops = {
	DEVO_REV,		/* devo_rev, */
	0,			/* refcnt  */
	isp_info,		/* info */
	isp_identify,		/* identify */
	nulldev,		/* probe */
	isp_attach,		/* attach */
	isp_detach,		/* detach */
	nodev,			/* reset */
	(struct cb_ops *)0,	/* driver operations */
	NULL,			/* bus operations */
	ddi_power		/* power management */
};

char _depends_on[] = "misc/scsi";

static struct modldrv modldrv = {
	&mod_driverops, /* Type of module. This one is a driver */
	"ISP SCSI Host Bus Adapter Driver", /* Name of the module. */
	&isp_ops,	/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1, &modldrv, NULL
};

_init(void)
{
	/* NO OTHER THREADS ARE RUNNING */
	int ret;


	ret = ddi_soft_state_init(&isp_state, sizeof (struct isp),
	    ISP_INITIAL_SOFT_SPACE);
	if (ret != 0)
		return (ret);

	mutex_init(&isp_global_mutex, "isp_global_mutex", MUTEX_DRIVER, NULL);

	if ((ret = scsi_hba_init(&modlinkage)) != 0) {
		mutex_destroy(&isp_global_mutex);
		ddi_soft_state_fini(&isp_state);
		return (ret);
	}

	ret = mod_install(&modlinkage);
	if (ret != 0) {
		scsi_hba_fini(&modlinkage);
		mutex_destroy(&isp_global_mutex);
		ddi_soft_state_fini(&isp_state);
	}

	return (ret);
}

/*
 * nexus drivers are currently not unloaded so this routine is really redundant
 */
_fini(void)
{
	/* NO OTHER THREADS ARE RUNNING */

	int ret;


	if ((ret = mod_remove(&modlinkage)) != 0)
		return (ret);

	scsi_hba_fini(&modlinkage);

	mutex_destroy(&isp_global_mutex);

	ddi_soft_state_fini(&isp_state);

	return (ret);
}

_info(struct modinfo *modinfop)
{
	/* NO OTHER THREADS ARE RUNNING */
	return (mod_info(&modlinkage, modinfop));
}

/*ARGSUSED*/
static int
isp_scsi_tgt_init(dev_info_t *hba_dip, dev_info_t *tgt_dip,
    scsi_hba_tran_t *hba_tran, struct scsi_device *sd)
{
	return (((sd->sd_address.a_target < NTARGETS_WIDE) &&
		(sd->sd_address.a_lun < NLUNS_PER_TARGET)) ?
		DDI_SUCCESS : DDI_FAILURE);
}


/*
 * Given the device number return the devinfo pointer
 * from the scsi_device structure.
 */
/*ARGSUSED*/
static int
isp_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result)
{
	return (DDI_FAILURE);
}

static int
isp_identify(dev_info_t *dip)
{
/* NO OTHER THREADS ARE RUNNING */
	char *dname = ddi_get_name(dip);

	if ((strcmp(dname, "QLGC,isp") == 0) ||
	    (strcmp(dname, "SUNW,isp") == 0)) {
		return (DDI_IDENTIFIED);
	} else {
		return (DDI_NOT_IDENTIFIED);
	}
}

/*
 * Attach isp host adapter.  Allocate data structures and link
 * to isp_head list.  Initialize the isp and we're
 * on the air.
 */
/*ARGSUSED*/
static int
isp_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	/* NO OTHER THREADS ARE RUNNING */
	char buf[64];
	u_int (*intr)();
	register int id;
	caddr_t ispqueue;
	int mutex_initted = 0;
	int timeout_initted = 0;
	int interrupt_added = 0;
	int bound_handle = 0;
	register struct isp *isp;
	int instance;
	volatile struct ispregs *ip;
	ulong_t		timestamp;
	int		power;
	scsi_hba_tran_t *tran = NULL;
	ddi_device_acc_attr_t dev_attr;
	ddi_acc_handle_t acc_handle;
	u_int rlen, count;
	register struct isp *s_isp, *l_isp;
	ddi_dma_attr_t tmp_dma_attr;
	char *prop_template = "target%x-scsi-options";
	char prop_str[32];
	int i;

	instance = ddi_get_instance(dip);



	switch (cmd) {
	case DDI_ATTACH:
		dev_attr.devacc_attr_version = DDI_DEVICE_ATTR_V0;
		dev_attr.devacc_attr_endian_flags = DDI_NEVERSWAP_ACC;
		dev_attr.devacc_attr_dataorder = DDI_STRICTORDER_ACC;
		break;

	case DDI_RESUME:
		tran = (scsi_hba_tran_t *)ddi_get_driver_private(dip);
		if (!tran) {
			return (DDI_FAILURE);
		}
		isp = TRAN2ISP(tran);
		if (!isp) {
			return (DDI_FAILURE);
		}

		/*
		 * reset isp and bus
		 */
		ISP_MUTEX_ENTER(isp);
		if (isp_i_reset_interface(isp, ISP_FORCE_RESET_BUS)) {
			ISP_MUTEX_EXIT(isp);
			return (DDI_FAILURE);
		}
		mutex_exit(ISP_REQ_MUTEX(isp));
		(void) scsi_hba_reset_notify_callback(ISP_RESP_MUTEX(isp),
		    &isp->isp_reset_notify_listf);
		mutex_exit(ISP_RESP_MUTEX(isp));

		mutex_enter(ISP_REQ_MUTEX(isp));
		ISP_CHECK_WAITQ_AND_EXIT_REQ_MUTEX(isp);

		isp->isp_suspended = 0;
		return (DDI_SUCCESS);

	default:
		isp_i_log(NULL, CE_WARN,
		    "isp%d: Cmd != DDI_ATTACH/DDI_RESUME", instance);
		return (DDI_FAILURE);
	}

	/*
	 * Since we know that some instantiations of this device can
	 * be plugged into slave-only SBus slots, check to see whether
	 * this is one such.
	 */
	if (ddi_slaveonly(dip) == DDI_SUCCESS) {
		isp_i_log(NULL, CE_WARN,
		    "isp%d: Device in slave-only slot, unused",
		    instance);
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
		isp_i_log(NULL, CE_WARN,
		    "isp%d: Device is using a hilevel intr, unused",
		    instance);
		return (DDI_FAILURE);
	}

	/*
	 * Allocate isp data structure.
	 */
	if (ddi_soft_state_zalloc(isp_state, instance) != DDI_SUCCESS) {
		isp_i_log(NULL, CE_WARN, "isp%d: Failed to alloc soft state",
		    instance);
		return (DDI_FAILURE);
	}

	isp = (struct isp *)ddi_get_soft_state(isp_state, instance);
	if (isp == (struct isp *)NULL) {
		isp_i_log(NULL, CE_WARN, "isp%d: Bad soft state", instance);
		ddi_soft_state_free(isp_state, instance);
		return (DDI_FAILURE);
	}

	/*
	 * map in device registers
	 */

	if (ddi_regs_map_setup(dip, (u_int)0, (caddr_t *)&ip,
		(off_t)0, (off_t)sizeof (struct ispregs),
		&dev_attr, &isp->isp_acc_handle) != DDI_SUCCESS) {

		isp_i_log(NULL, CE_WARN, "isp%d: Unable to map registers",
		    instance);
		ddi_soft_state_free(isp_state, instance);
		return (DDI_FAILURE);

	}

	isp->isp_cmdarea = NULL;
	tmp_dma_attr = dma_ispattr;
	/*
	 * align isp_cmdarea on a long long boundary for future
	 * long long copies.
	 */

	if (ddi_dma_alloc_handle(dip, &tmp_dma_attr,
		DDI_DMA_SLEEP, NULL, &isp->isp_dmahandle) != DDI_SUCCESS) {
		cmn_err(CE_WARN,
			"isp%d: cannot alloc dma handle", instance);
			goto fail;
	}

	if (ddi_dma_mem_alloc(isp->isp_dmahandle,
		(u_int) ISP_QUEUE_SIZE + sizeof (long long),
		&dev_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
		NULL, (caddr_t *)&isp->isp_cmdarea, &rlen,
		&acc_handle) != DDI_SUCCESS) {
			cmn_err(CE_WARN,
			"isp%d: cannot alloc cmd area", instance);
			goto fail;
	}
	ispqueue = (caddr_t)(((unsigned)isp->isp_cmdarea +
	    sizeof (long long)) & ~(sizeof (long long) - 1));
	if (ddi_dma_addr_bind_handle(isp->isp_dmahandle,
		NULL, (caddr_t)ispqueue, (u_int) ISP_QUEUE_SIZE,
		DDI_DMA_RDWR|DDI_DMA_CONSISTENT,
		DDI_DMA_SLEEP, NULL, &isp->isp_dmacookie,
		&count) != DDI_DMA_MAPPED) {
			cmn_err(CE_WARN,
			"isp%d: cannot bind cmd area", instance);
			goto fail;
	}
	bound_handle++;
	bzero(isp->isp_cmdarea, ISP_QUEUE_SIZE + sizeof (long long));
	isp->isp_request_dvma =
	isp->isp_dmacookie.dmac_address;
	isp->isp_request_base =
	    (struct isp_request *)ispqueue;

	isp->isp_response_dvma =
	    isp->isp_request_dvma + (ISP_MAX_REQUESTS *
	    sizeof (struct isp_request));
	isp->isp_response_base = (struct isp_response *)
	    ((caddr_t)isp->isp_request_base +
	    (ISP_MAX_REQUESTS *
	    sizeof (struct isp_request)));
	isp->isp_request_in = 1;
	isp->isp_request_out = 1;
	isp->isp_response_in = 1;
	isp->isp_response_out = 1;

	/*
	 * Begin setting up isp softc structure.
	 */
	isp->isp_reg = ip;

	/*
	 * get cookie so we can initialize the mutexes
	 */
	if (ddi_get_iblock_cookie(dip, (u_int)0, &isp->isp_iblock)
	    != DDI_SUCCESS) {
		goto fail;
	}

	/*
	 * Allocate a transport structure
	 */
	tran = scsi_hba_tran_alloc(dip, 0);
	if (tran == NULL) {
		cmn_err(CE_WARN, "isp: scsi_hba_tran_alloc failed\n");
		goto fail;
	}

	isp->isp_tran		= tran;
	isp->isp_dip		= dip;

	tran->tran_hba_private	= isp;
	tran->tran_tgt_private	= NULL;
	tran->tran_tgt_init	= isp_scsi_tgt_init;
	tran->tran_tgt_probe	= NULL;
	tran->tran_tgt_free	= NULL;

	tran->tran_start	= isp_scsi_start;
	tran->tran_abort	= isp_scsi_abort;
	tran->tran_reset	= isp_scsi_reset;
	tran->tran_getcap	= isp_scsi_getcap;
	tran->tran_setcap	= isp_scsi_setcap;
	tran->tran_init_pkt	= isp_scsi_init_pkt;
	tran->tran_destroy_pkt	= isp_scsi_destroy_pkt;
	tran->tran_dmafree	= isp_scsi_dmafree;
	tran->tran_sync_pkt	= isp_scsi_sync_pkt;
	tran->tran_reset_notify = isp_scsi_reset_notify;


	/*
	 * find scsi host id property
	 */
	id = ddi_getprop(DDI_DEV_T_ANY, dip, 0, "scsi-initiator-id", -1);
	if (id != scsi_host_id && (id >= 0 && id < NTARGETS_WIDE)) {
		isp_i_log(isp, CE_NOTE, "initiator SCSI ID now %d", id);
		isp->isp_initiator_id = (u_char) id;
	} else {
		isp->isp_initiator_id = (u_char) scsi_host_id;
	}

	/*
	 * if scsi-options property exists, use it;
	 * otherwise use the global variable
	 */
	isp->isp_scsi_options =
		ddi_prop_get_int(DDI_DEV_T_ANY, dip, 0, "scsi-options",
		    scsi_options);
	ISP_DEBUG(isp, SCSI_DEBUG, "isp scsi_options=%x, global=%x",
	    isp->isp_scsi_options, scsi_options);
	if (isp->isp_scsi_options != scsi_options) {
		isp_i_log(isp, CE_NOTE, "isp scsi_options=0x%x",
		    isp->isp_scsi_options);
	}

	/*
	 * if target<n>-scsi-options property exists, use it;
	 * otherwise use the isp_scsi_options
	 */
	for (i = 0; i < NTARGETS_WIDE; i++) {
		(void) sprintf(prop_str, prop_template, i);
		isp->isp_target_scsi_options[i] = ddi_prop_get_int(
			DDI_DEV_T_ANY, dip, 0, prop_str, isp->isp_scsi_options);
		ISP_DEBUG(isp, SCSI_DEBUG, "isp target%d_scsi_options=%x, "
			"isp scsi_options=%x", i,
			isp->isp_target_scsi_options[i], isp->isp_scsi_options);
		if (isp->isp_target_scsi_options[i] != isp->isp_scsi_options) {
			isp_i_log(isp, CE_NOTE,
				"isp target%x_scsi_options=0x%x",
				i, isp->isp_target_scsi_options[i]);
		}
	}

	isp->isp_scsi_tag_age_limit =
		ddi_getprop(DDI_DEV_T_ANY, dip, 0, "scsi-tag-age-limit",
		    scsi_tag_age_limit);
	ISP_DEBUG(isp, SCSI_DEBUG, "isp scsi_tage_age_limit=%d, global=%d",
	    isp->isp_scsi_tag_age_limit, scsi_tag_age_limit);
	if (isp->isp_scsi_tag_age_limit != scsi_tag_age_limit) {
		isp_i_log(isp, CE_NOTE, "scsi-tag-age-limit=%d",
		    isp->isp_scsi_tag_age_limit);
	}

	isp->isp_scsi_reset_delay =
		ddi_getprop(DDI_DEV_T_ANY, dip, 0, "scsi-reset-delay",
		    scsi_reset_delay);
	ISP_DEBUG(isp, SCSI_DEBUG, "isp scsi_reset_delay=%d, global=%d",
	    isp->isp_scsi_reset_delay, scsi_reset_delay);
	if (isp->isp_scsi_reset_delay != scsi_reset_delay) {
		isp_i_log(isp, CE_NOTE, "scsi-reset-delay=%d",
		    isp->isp_scsi_reset_delay);
	}

	/*
	 * find the burstsize and reduce ours if necessary
	 * If no burst size found, select a reasonable default.
	 */
	tmp_dma_attr.dma_attr_burstsizes &=
		(ddi_dma_burstsizes(isp->isp_dmahandle) &
		isp_burst_sizes_limit);
	isp->isp_burst_size = tmp_dma_attr.dma_attr_burstsizes;


	ISP_DEBUG(isp, SCSI_DEBUG, "ispattr burstsize=%x",
		isp->isp_burst_size);

	if (isp->isp_burst_size == -1) {
		isp->isp_burst_size = DEFAULT_BURSTSIZE | BURST32 | BURST64;
		ISP_DEBUG(isp, SCSI_DEBUG, "Using default burst sizes, 0x%x",
		    isp->isp_burst_size);
	} else {
		isp->isp_burst_size &= BURSTSIZE_MASK;
		ISP_DEBUG(isp, SCSI_DEBUG, "burst sizes= 0x%x",
			isp->isp_burst_size);
	}
	if (isp->isp_burst_size & BURST64) {
		isp->isp_conf1_burst_flag =
		    ISP_SBUS_CONF1_BURST_ENABLE | ISP_SBUS_CONF1_BURST64;
	} else if (isp->isp_burst_size & BURST32) {
		isp->isp_conf1_burst_flag =
		    ISP_SBUS_CONF1_BURST_ENABLE | ISP_SBUS_CONF1_BURST32;
	} else if (isp->isp_burst_size & BURST16) {
		isp->isp_conf1_burst_flag =
		    ISP_SBUS_CONF1_BURST_ENABLE | ISP_SBUS_CONF1_BURST16;
	} else if (isp->isp_burst_size & BURST8) {
		isp->isp_conf1_burst_flag =
		    ISP_SBUS_CONF1_BURST_ENABLE | ISP_SBUS_CONF1_BURST8;
	}

	/*
	 * Attach this instance of the hba
	 */
	if (scsi_hba_attach_setup(dip, &tmp_dma_attr, tran, 0)
		!= DDI_SUCCESS) {
		cmn_err(CE_WARN, "isp: scsi_hba_attach failed\n");
		goto fail;
	}

	/*
	 * initialize the mbox sema
	 */
	(void) sprintf(buf, "isp%d_mbox_sema", instance);
	sema_init(ISP_MBOX_SEMA(isp), 1, buf, SEMA_DRIVER,
	    isp->isp_iblock);


	/*
	 * initialize the request & response mutex
	 */
	(void) sprintf(buf, "isp%d_waitq_mutex", instance);
	mutex_init(ISP_WAITQ_MUTEX(isp), buf, MUTEX_DRIVER,
	    isp->isp_iblock);

	/*
	 * mutexed to protect the isp request and response queue
	 */
	(void) sprintf(buf, "isp%d_request_mutex", instance);
	mutex_init(ISP_REQ_MUTEX(isp), buf, MUTEX_DRIVER,
	    isp->isp_iblock);
	(void) sprintf(buf, "isp%d_response_mutex", instance);
	mutex_init(ISP_RESP_MUTEX(isp), buf, MUTEX_DRIVER,
	    isp->isp_iblock);
	mutex_initted = 1;

	/*
	 * for sun4d/4u, install the normal interrupt handler; for
	 * sun4m/4c, a loop handler which services every isp
	 *
	 * XXX replace this if and when ddi_add_precise_intr
	 * is implemented
	 */
	if (strcmp(utsname.machine, "sun4d") == 0 ||
		strcmp(utsname.machine, "sun4u") == 0) {
		intr = isp_intr;
	} else {
		intr = isp_intr_loop;
	}

	if (ddi_add_intr(dip, (u_int)0,
	    (ddi_iblock_cookie_t *)&isp->isp_iblock,
	    (ddi_idevice_cookie_t *)0, intr, (caddr_t)isp)) {
		isp_i_log(NULL, CE_WARN, "isp%d: Cannot add intr", instance);
		goto fail;
	} else {
		interrupt_added = 1;
	}

	/*
	 * link all isp's, for isp_intr_loop but also debugging
	 */
	mutex_enter(&isp_global_mutex);
	isp->isp_next = NULL;

	if (isp_head) {
		isp_tail->isp_next = isp;
		isp_tail = isp;
	} else {
		isp_head = isp_tail = isp;
	}
	isp_last = isp_head;

	/*
	 * set up watchdog per all isp's
	 */
	if (isp_timeout_id == 0) {
#ifdef ISPDEBUG
		isp_scsi_watchdog_tick =
		    ddi_getprop(DDI_DEV_T_ANY, dip, 0, "scsi-watchdog-tick",
		    scsi_watchdog_tick);
		if (isp_scsi_watchdog_tick != scsi_watchdog_tick) {
			isp_i_log(isp, CE_NOTE, "scsi-watchdog-tick=%d",
			    isp_scsi_watchdog_tick);
		}
#else
		isp_scsi_watchdog_tick = 60;
#endif
		isp_tick =
		    drv_usectohz((clock_t)isp_scsi_watchdog_tick * 1000000);
		ISP_DEBUG(isp, SCSI_DEBUG,
		    "isp_scsi_watchdog_tick=%d, isp_tick=%d",
		    isp_scsi_watchdog_tick, isp_tick);
		isp_timeout_id = timeout(isp_i_watch, NULL, isp_tick);
		timeout_initted = 1;
	}

	mutex_exit(&isp_global_mutex);

	ISP_MUTEX_ENTER(isp);

	/*
	 * create kmem cache for packets
	 */
	sprintf(buf, "isp%d_cache", instance);
	isp->isp_kmem_cache = kmem_cache_create(buf,
		EXTCMDS_SIZE, 8, isp_kmem_cache_constructor,
		isp_kmem_cache_destructor, NULL);
	if (isp->isp_kmem_cache == NULL) {
		cmn_err(CE_WARN, "isp: cannot create kmem_cache");
		ISP_MUTEX_EXIT(isp);
		goto fail;
	}

	/*
	 * Download the ISP firmware that has been linked in
	 * We need the mutexes here to avoid assertion failures in
	 * the mbox cmds
	 */
	if (isp_i_download_fw(isp, isp_risc_code_addr, isp_risc_code,
	    isp_risc_code_length)) {
		ISP_MUTEX_EXIT(isp);
		goto fail;
	}

	/*
	 * Initialize the default Target Capabilites and Sync Rates
	 */
	isp_i_initcap(isp);

	/*
	 * reset isp and initialize capabilities
	 * Do NOT reset the bus since that will cause a reset delay
	 * which adds substantially to the boot time.
	 */
	if (isp_i_reset_interface(isp, ISP_RESET_BUS_IF_BUSY)) {
		ISP_MUTEX_EXIT(isp);
		goto fail;
	}
	ISP_MUTEX_EXIT(isp);

	ddi_report_dev(dip);

	isp_i_log(isp, CE_NOTE, "?Firmware Version: v%d.%02d",
	    isp->isp_major_rev, isp->isp_minor_rev);

	/*
	 * Create power management property
	 */
	drv_getparm(TIME, &timestamp);
	if (!timestamp) {
		timestamp = 1;		/* Workaround for bug 1125533 */
	}
	(void) ddi_prop_create(DDI_DEV_T_NONE, dip, DDI_PROP_CANSLEEP,
	    "pm_timestamp", (caddr_t)&timestamp, sizeof (timestamp));
	power = 1;
	(void) ddi_prop_create(DDI_DEV_T_NONE, dip, DDI_PROP_CANSLEEP,
	    "pm_norm_pwr", (caddr_t)&power, sizeof (power));

	return (DDI_SUCCESS);

fail:
	isp_i_log(NULL, CE_WARN, "isp%d: Unable to attach", instance);

	if (isp->isp_kmem_cache) {
		kmem_cache_destroy(isp->isp_kmem_cache);
	}
	if (isp->isp_cmdarea) {
		if (bound_handle) {
			ddi_dma_unbind_handle(isp->isp_dmahandle);
		}
		ddi_dma_mem_free(&acc_handle);
	}
	if (mutex_initted) {
		mutex_destroy(ISP_WAITQ_MUTEX(isp));
		mutex_destroy(ISP_REQ_MUTEX(isp));
		mutex_destroy(ISP_RESP_MUTEX(isp));
		sema_destroy(ISP_MBOX_SEMA(isp));
	}
	if (timeout_initted) {
		(void) untimeout(isp_timeout_id);
		mutex_enter(&isp_global_mutex);
		isp_timeout_id = 0;
		mutex_exit(&isp_global_mutex);
	}
	if (isp->isp_dmahandle) {
		ddi_dma_free_handle(&isp->isp_dmahandle);
	}

	/*
	 * this is not entirely safe; the isp_intr_loop() and
	 * isp_i_watch() go thru this list without the mutex
	 * Since these two functions don't use isp_tail and isp_head
	 * is atomically updated, it should be OK (?)
	 */
	mutex_enter(&isp_global_mutex);
	for (l_isp = s_isp = isp_head; s_isp != NULL;
	    s_isp = s_isp->isp_next) {
		if (s_isp == isp) {
			if (s_isp == isp_head) {
				isp_head = isp->isp_next;
				if (isp_tail == isp) {
					isp_tail = NULL;
				}
			} else {
				if (isp_tail == isp) {
					isp_tail = l_isp;
				}
				l_isp->isp_next = isp->isp_next;
			}
			break;
		}
		l_isp = s_isp;
	}
	mutex_exit(&isp_global_mutex);

	if (interrupt_added) {
		ddi_remove_intr(dip, (u_int)0, isp->isp_iblock);
	}
	if (tran) {
		scsi_hba_tran_free(tran);
	}
	ddi_regs_map_free(&isp->isp_acc_handle);
	ddi_soft_state_free(isp_state, instance);
	return (DDI_FAILURE);
}

/*ARGSUSED*/
static int
isp_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	/* NO OTHER THREADS ARE RUNNING */
	register struct isp	*isp;
	scsi_hba_tran_t		*tran;

	switch (cmd) {
	case DDI_DETACH:
		/*
		 * Be sure to call scsi_hba_detach(dip), if
		 * this is ever implemented.
		 */
		return (DDI_FAILURE);

	case DDI_SUSPEND:
		return (DDI_SUCCESS);

	case DDI_PM_SUSPEND:
		tran = (scsi_hba_tran_t *)ddi_get_driver_private(dip);
		if (!tran) {
			return (DDI_FAILURE);
		}
		isp = TRAN2ISP(tran);
		if (!isp) {
			return (DDI_FAILURE);
		}
		/*
		 * reset isp and bus
		 */
		ISP_MUTEX_ENTER(isp);
		if (isp_i_reset_interface(isp, ISP_FORCE_RESET_BUS)) {
			ISP_MUTEX_EXIT(isp);
			return (DDI_FAILURE);
		}
		isp->isp_suspended = 1;
		ISP_MUTEX_EXIT(isp);
		return (DDI_SUCCESS);

	default:
		return (DDI_FAILURE);
	}
}

/*
 * Function name : isp_i_download_fw ()
 *
 * Return Values : 0  on success.
 *		   -1 on error.
 *
 * Description	 : Uses the request and response queue iopb memory for dma.
 *		   Verifies that fw fits in iopb memory.
 *		   Verifies fw checksum.
 *		   Copies firmware to iopb memory.
 *		   Sends mbox cmd to ISP to stop executing from RAM and
 *			execute from internal ROM so downloading
 *			won't confuse it.
 *		   Sends mbox cmd to ISP to (down) Load RAM.
 *		   After command is done, resets ISP which starts it
 *			executing from new f/w.
 *
 * Context	 : Can be called ONLY from user context.
 *		 : NOT MT-safe.
 *		 : Driver must be in a quiescent state.
 */
static int
isp_i_download_fw(struct isp *isp,
    u_short risc_addr, u_short *fw_addrp, u_short fw_len)
{
	volatile struct ispregs *ip	= isp->isp_reg;
	int rval			= -1;
	int fw_len_bytes		= (int)fw_len * sizeof (unsigned short);
	u_short checksum		= 0;
	int found			= 0;
	char *string			= " Firmware  Version ";
	char *startp;
	char buf[10];
	int length;
	int major_rev, minor_rev;
	int old_major_rev, old_minor_rev;
	int delay_loops;
	struct isp_mbox_cmd mbox_cmd;
	u_short i;

	ISP_DEBUG2(isp, SCSI_DEBUG,
	    "isp_download_fw_start: risc = 0x%x fw = 0x%x, fw_len =0x%x",
	    risc_addr, fw_addrp, fw_len);

	/*
	 * if download is not necessary just return good status
	 */
	if (isp_download_fw == ISP_DOWNLOAD_FW_OFF) {
		goto done;
	}

	/*
	 * Since we use the request and response queue iopb
	 * we check to see if f/w will fit in this memory.
	 * This iopb memory presently is 32k and the f/w is about
	 * 13k but check the headers for definite values.
	 */
	if (fw_len_bytes > ISP_QUEUE_SIZE) {
		isp_i_log(isp, CE_WARN,
		    "Firmware should be < 0x%x bytes", ISP_QUEUE_SIZE);
		goto fail;
	}

	/*
	 * verify checksum equals zero
	 */
	for (i = 0; i < fw_len; i++)
		checksum += fw_addrp[i];
	if (checksum) {
		isp_i_log(isp, CE_WARN, "Firmware checksum incorrect");
		goto fail;
	}


	/*
	 * reset the ISP chip so that it will be in a known state.
	 */
	ip->isp_sbus_icr = ISP_SBUS_ICR_SOFT_RESET;
	ip->isp_cdma_control = ISP_DMA_CON_RESET_INT | ISP_DMA_CON_CLEAR_CHAN;
	ip->isp_dma_control = ISP_DMA_CON_RESET_INT | ISP_DMA_CON_CLEAR_CHAN;
	delay_loops = ISP_TIMEOUT_DELAY(ISP_SOFT_RESET_TIME,
	    ISP_CHIP_RESET_BUSY_WAIT_TIME);
	while (ip->isp_sbus_icr & ISP_SBUS_ICR_SOFT_RESET) {
		drv_usecwait(ISP_CHIP_RESET_BUSY_WAIT_TIME);
		if (--delay_loops < 0) {
			isp_i_log(isp, CE_WARN, "Chip reset timeout");
			goto fail;
		}
	}


	/*
	 * Reset the risc processor and initialize the Sbus
	 * control parameter configuration.
	 */
	ip->isp_hccr = ISP_HCCR_CMD_RESET;
	drv_usecwait(ISP_CHIP_RESET_BUSY_WAIT_TIME);
	ip->isp_sbus_conf1 = isp->isp_conf1_burst_flag;
	ip->isp_risc_mtr = ISP_RISC_MTR_PAGE0_DEFAULT |
	    ISP_RISC_MTR_PAGE1_DEFAULT;
	ip->isp_hccr = ISP_HCCR_CMD_RELEASE;
	isp->isp_mbox.mbox_flags |= ISP_MBOX_CMD_FLAGS_Q_NOT_INIT;
	/*
	 * start ISP firmware up
	 */
	isp_i_mbox_cmd_init(isp, &mbox_cmd, 2, 6,
	    ISP_MBOX_CMD_START_FW, risc_addr,
	    0, 0, 0, 0);
	if (isp_i_mbox_cmd_start(isp, &mbox_cmd)) {
		goto fail;
	}

	/*
	 * get new firmware version numbers
	 */
	startp = (char *)fw_addrp;
	length = fw_len_bytes;
	while (length - strlen(string)) {
		if (strncmp(startp, string, strlen(string)) == 0) {
			found = 1;
			break;
		}
		startp++;
		length --;
	}

	if (found) {
		startp += strlen(string);
		(void) strncpy(buf, startp, 5);
		buf[2] = buf[5] = NULL;
		startp = buf;
		major_rev = stoi(&startp);
		startp++;
		minor_rev = stoi(&startp);

		ISP_DEBUG(isp, SCSI_DEBUG, "New f/w: major = %d minor = %d",
		    major_rev, minor_rev);
	} else {
		goto done;
	}

	/*
	 * get ISP Ram firmware version numbers
	 */
	isp_i_mbox_cmd_init(isp, &mbox_cmd, 1, 6,
	    ISP_MBOX_CMD_ABOUT_PROM, 0, 0, 0, 0, 0);
	if (isp_i_mbox_cmd_start(isp, &mbox_cmd)) {
		goto fail;
	}

	ISP_DEBUG(isp, SCSI_DEBUG, "Old f/w: major = %d minor = %d",
	    mbox_cmd.mbox_in[1], mbox_cmd.mbox_in[2]);

	old_major_rev = mbox_cmd.mbox_in[1];
	old_minor_rev = mbox_cmd.mbox_in[2];

	if (isp_download_fw == ISP_DOWNLOAD_FW_IF_NEWER) {
		if (major_rev < (int)mbox_cmd.mbox_in[1] ||
		    minor_rev <= (int)mbox_cmd.mbox_in[2]) {
			goto done;
		}
	}

	/*
	 * copy firmware to iopb memory that was allocated for queues.
	 * XXX this assert is not quite right, area is a little smaller
	 */
	ASSERT(fw_len_bytes <= ISP_QUEUE_SIZE);
	bcopy((caddr_t)fw_addrp, (caddr_t)isp->isp_request_base,
	    fw_len_bytes);

	/*
	 * sync memory
	 */
	(void) ddi_dma_sync(isp->isp_dmahandle, (off_t)0, (int)fw_len_bytes,
	    DDI_DMA_SYNC_FORDEV);

	/*
	 * Send mailbox cmd to stop ISP from executing the present
	 * firmware and drop to executing the ROM firmware.
	 */
	ISP_DEBUG(isp, SCSI_DEBUG, "Stop Firmware");
	isp_i_mbox_cmd_init(isp, &mbox_cmd, 1, 6, ISP_MBOX_CMD_STOP_FW,
	    0, 0, 0, 0, 0);
	if (isp_i_mbox_cmd_start(isp, &mbox_cmd)) {
		isp_i_log(isp, CE_WARN, "Stop firmware failed");
		goto fail;
	}

	/*
	 * Since the ISP is executing from its ROM firmware
	 * it is now possible to muck around with the RAM firmware.
	 */
	ISP_DEBUG(isp, SCSI_DEBUG, "Load Ram");
	isp_i_mbox_cmd_init(isp, &mbox_cmd, 5, 1, ISP_MBOX_CMD_LOAD_RAM,
	    risc_addr, MSW(isp->isp_request_dvma),
	    LSW(isp->isp_request_dvma), fw_len, 0);
	if (isp_i_mbox_cmd_start(isp, &mbox_cmd)) {
		isp_i_log(isp, CE_WARN, "Load ram failed");
		isp_i_print_state(isp);
		goto fail;
	}

	/*
	 * reset the ISP chip so it starts with the new firmware
	 */
	ip->isp_hccr = ISP_HCCR_CMD_RESET;
	drv_usecwait(ISP_CHIP_RESET_BUSY_WAIT_TIME);
	ip->isp_hccr = ISP_HCCR_CMD_RELEASE;

	/*
	 * Start ISP firmware up.
	 */
	isp_i_mbox_cmd_init(isp, &mbox_cmd, 2, 6,
	    ISP_MBOX_CMD_START_FW, risc_addr,
	    0, 0, 0, 0);
	if (isp_i_mbox_cmd_start(isp, &mbox_cmd)) {
		goto fail;
	}

	/*
	 * get ISP Ram firmware version numbers
	 */
	isp_i_mbox_cmd_init(isp, &mbox_cmd, 1, 6,
	    ISP_MBOX_CMD_ABOUT_PROM, 0, 0, 0, 0, 0);
	if (isp_i_mbox_cmd_start(isp, &mbox_cmd)) {
		goto fail;
	}

	isp->isp_major_rev = mbox_cmd.mbox_in[1];
	isp->isp_minor_rev = mbox_cmd.mbox_in[2];

	ISP_DEBUG(isp, SCSI_DEBUG, "Downloaded f/w: major = %d minor = %d",
		    mbox_cmd.mbox_in[1], mbox_cmd.mbox_in[2]);

	isp_i_log(isp, CE_NOTE, "?Updated Firmware from v%d.%02d to v%d.%02d",
	    old_major_rev, old_minor_rev,
	    isp->isp_major_rev, isp->isp_minor_rev);

done:
	rval = 0;

fail:
	ISP_DEBUG2(isp, SCSI_DEBUG,
	    "isp_i_download_fw: 0x%x 0x%x, 0x%x, 0x%x, 0x%x, 0x%x",
	    ip->isp_mailbox0, ip->isp_mailbox1, ip->isp_mailbox2,
	    ip->isp_mailbox3, ip->isp_mailbox4, ip->isp_mailbox5);

	ISP_DEBUG2(isp, SCSI_DEBUG, "isp_download_fw_end: rval = %d", rval);

	bzero((caddr_t)isp->isp_request_base, ISP_QUEUE_SIZE);

	return (rval);
}

/*
 * Function name : isp_i_initcap
 *
 * Return Values : NONE
 * Description	 : Initializes the default target capabilites and
 *		   Sync Rates.
 *
 * Context	 : Called from the user thread through attach.
 *
 */
static void
isp_i_initcap(struct isp *isp)
{
	register int i;
	register u_short cap, synch;

	cap = 0;
	synch = 0;
	if (isp->isp_scsi_options & SCSI_OPTIONS_DR) {
		cap |= ISP_CAP_DISCONNECT;
	}
	if (isp->isp_scsi_options & SCSI_OPTIONS_PARITY) {
		cap |= ISP_CAP_PARITY;
	}
	if (isp->isp_scsi_options & SCSI_OPTIONS_SYNC) {
		cap |= ISP_CAP_SYNC;
		if (isp->isp_scsi_options & SCSI_OPTIONS_FAST) {
			synch = ISP_10M_SYNC_PARAMS;
		} else {
			synch = ISP_5M_SYNC_PARAMS;
		}
	}

	for (i = 0; i < NTARGETS_WIDE; i++) {
		isp->isp_cap[i] = cap;
		isp->isp_synch[i] = synch;
	}
	ISP_DEBUG(isp, SCSI_DEBUG, "default cap = 0x%x", cap);
}


/*
 * Function name : isp_i_commoncap
 *
 * Return Values : TRUE - capability exists  or could be changed
 *		   FALSE - capability does not exist or could not be changed
 *		   value - current value of capability
 * Description	 : sets a capability for a target or all targets
 *		   or returns the current value of a capability
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
/*
 * SCSA host adapter get/set capability routines.
 * isp_scsi_getcap and isp_scsi_setcap are wrappers for isp_i_commoncap.
 */
static int
isp_i_commoncap(struct scsi_address *ap, char *cap,
    int val, int tgtonly, int doset)
{
	register struct isp *isp = ADDR2ISP(ap);
	register u_char tgt = ap->a_target;
	register int cidx;
	register int i;
	register int rval = FALSE;
	int update_isp = 0;
	u_short	start, end;

	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)) == 0 || ddi_in_panic());

	if (cap == (char *)0) {
		ISP_DEBUG(isp, SCSI_DEBUG, "isp_i_commoncap: invalid arg");
		return (rval);
	}

	cidx = scsi_hba_lookup_capstr(cap);
	if (cidx == -1) {
		return (UNDEFINED);
	}

	ISP_MUTEX_ENTER(isp);

	/*
	 * Process setcap request.
	 */
	if (doset) {
		/*
		 * At present, we can only set binary (0/1) values
		 */
		switch (cidx) {
		case SCSI_CAP_DMA_MAX:
		case SCSI_CAP_MSG_OUT:
		case SCSI_CAP_PARITY:
		case SCSI_CAP_UNTAGGED_QING:
		case SCSI_CAP_LINKED_CMDS:
		case SCSI_CAP_RESET_NOTIFICATION:
			/*
			 * None of these are settable via
			 * the capability interface.
			 */
			break;
		case SCSI_CAP_DISCONNECT:
			if ((isp->isp_target_scsi_options[tgt] &
				SCSI_OPTIONS_DR) == 0) {
				break;
			} else if (tgtonly) {
				if (val) {
					isp->isp_cap[tgt] |=
					    ISP_CAP_DISCONNECT;
				} else {
					isp->isp_cap[tgt] &=
					    ~ISP_CAP_DISCONNECT;
				}
			} else {
				for (i = 0; i < NTARGETS_WIDE; i++) {
					if (val) {
						isp->isp_cap[i] |=
						    ISP_CAP_DISCONNECT;
					} else {
						isp->isp_cap[i] &=
						    ~ISP_CAP_DISCONNECT;
					}
				}
			}
			rval = TRUE;
			update_isp++;
			break;
		case SCSI_CAP_SYNCHRONOUS:
			if ((isp->isp_target_scsi_options[tgt] &
				SCSI_OPTIONS_SYNC) == 0) {
				break;
			} else if (tgtonly) {
				if (val) {
					isp->isp_cap[tgt] |= ISP_CAP_SYNC;
				} else {
					isp->isp_cap[tgt] &= ~ISP_CAP_SYNC;
				}
			} else {
				for (i = 0; i < NTARGETS_WIDE; i++) {
					if (val) {
						isp->isp_cap[i] |=
						    ISP_CAP_SYNC;
					} else {
						isp->isp_cap[i] &=
						    ~ISP_CAP_SYNC;
					}
				}
			}
			rval = TRUE;
			update_isp++;
			break;
		case SCSI_CAP_TAGGED_QING:
			if ((isp->isp_target_scsi_options[tgt] &
				SCSI_OPTIONS_DR) == 0 ||
			    (isp->isp_target_scsi_options[tgt] &
				SCSI_OPTIONS_TAG) == 0) {
				break;
			} else if (tgtonly) {
				if (val) {
					isp->isp_cap[tgt] |= ISP_CAP_TAG;
				} else {
					isp->isp_cap[tgt] &= ~ISP_CAP_TAG;
				}
			} else {
				for (i = 0; i < NTARGETS_WIDE; i++) {
					if (val) {
						isp->isp_cap[i] |= ISP_CAP_TAG;
					} else {
						isp->isp_cap[i] &= ~ISP_CAP_TAG;
					}
				}
			}
			rval = TRUE;
			update_isp++;
			break;
		case SCSI_CAP_WIDE_XFER:
			if ((isp->isp_target_scsi_options[tgt] &
				SCSI_OPTIONS_WIDE) == 0) {
				break;
			} else if (tgtonly) {
				if (val) {
					isp->isp_cap[tgt] |= ISP_CAP_WIDE;
				} else {
					isp->isp_cap[tgt] &= ~ISP_CAP_WIDE;
				}
			} else {
				for (i = 0; i < NTARGETS_WIDE; i++) {
					if (val) {
						isp->isp_cap[i] |=
						    ISP_CAP_WIDE;
					} else {
						isp->isp_cap[i] &=
						    ~ISP_CAP_WIDE;
					}
				}
			}
			rval = TRUE;
			update_isp++;
			break;
		case SCSI_CAP_INITIATOR_ID:
			if (val < NTARGETS_WIDE) {
				struct isp_mbox_cmd mbox_cmd;

				isp->isp_initiator_id = (u_short) val;

				/*
				 * set Initiator SCSI ID
				 */
				isp_i_mbox_cmd_init(isp, &mbox_cmd, 2, 2,
				    ISP_MBOX_CMD_SET_SCSI_ID,
				    isp->isp_initiator_id,
				    0, 0, 0, 0);
				if (isp_i_mbox_cmd_start(isp, &mbox_cmd) == 0) {
					rval = TRUE;
				}
			}
			break;
		case SCSI_CAP_ARQ:
			if (tgtonly) {
				if (val) {
					isp->isp_cap[tgt] |= ISP_CAP_AUTOSENSE;
				} else {
					isp->isp_cap[tgt] &= ~ISP_CAP_AUTOSENSE;
				}
			} else {
				for (i = 0; i < NTARGETS_WIDE; i++) {
					if (val) {
						isp->isp_cap[i] |=
						    ISP_CAP_AUTOSENSE;
					} else {
						isp->isp_cap[i] &=
						    ~ISP_CAP_AUTOSENSE;
					}
				}
			}
			rval = TRUE;
			update_isp++;
			break;

		case SCSI_CAP_QFULL_RETRIES:
			if (tgtonly) {
				start = end = tgt;
			} else {
				start = 0;
				end = NTARGETS_WIDE;
			}
			rval = isp_i_handle_qfull_cap(isp, start, end,
				val, ISP_SET_QFULL_CAP,
				SCSI_CAP_QFULL_RETRIES);
			break;
		case SCSI_CAP_QFULL_RETRY_INTERVAL:
			if (tgtonly) {
				start = end = (u_short)tgt;
			} else {
				start = 0;
				end = NTARGETS_WIDE;
			}
			rval = isp_i_handle_qfull_cap(isp, start, end,
				val, ISP_SET_QFULL_CAP,
				SCSI_CAP_QFULL_RETRY_INTERVAL);
			break;

		default:
			ISP_DEBUG(isp, SCSI_DEBUG,
			    "isp_i_setcap: unsupported %d", cidx);
			rval = UNDEFINED;
			break;
		}

		ISP_DEBUG(isp, SCSI_DEBUG,
		"set cap: cap=%s,val=0x%x,tgtonly=0x%x,doset=0x%x,rval=%d\n",
		    cap, val, tgtonly, doset, rval);

		/*
		 * now update the isp, if necessary
		 */
		if ((rval == TRUE) && update_isp) {
			int start_tgt, end_tgt, i;

			if (tgtonly) {
				start_tgt = end_tgt = tgt;
			} else {
				start_tgt = 0;
				end_tgt = NTARGETS_WIDE;
			}
			if (isp_i_updatecap(isp, start_tgt, end_tgt)) {
				/*
				 * if we can't update the capabilities
				 * in the isp, we are hosed
				 */
				isp->isp_shutdown = 1;
				rval = FALSE;
			}
			for (i = start_tgt; i <= end_tgt; i++) {
				isp_i_update_props(isp, i, isp->isp_cap[i],
				    isp->isp_synch[i]);
			}
		}

	/*
	 * Process getcap request.
	 */
	} else {
		switch (cidx) {
		case SCSI_CAP_DMA_MAX:
			rval = 1 << 24; /* Limit to 16MB max transfer */
			break;
		case SCSI_CAP_MSG_OUT:
			rval = TRUE;
			break;
		case SCSI_CAP_DISCONNECT:
			if ((isp->isp_target_scsi_options[tgt] &
				SCSI_OPTIONS_DR) == 0) {
				break;
			} else if (tgtonly &&
			    (isp->isp_cap[tgt] & ISP_CAP_DISCONNECT) == 0) {
				break;
			}
			rval = TRUE;
			break;
		case SCSI_CAP_SYNCHRONOUS:
			if ((isp->isp_target_scsi_options[tgt] &
				SCSI_OPTIONS_SYNC) == 0) {
				break;
			} else if (tgtonly &&
			    (isp->isp_cap[tgt] & ISP_CAP_SYNC) == 0) {
				break;
			}
			rval = TRUE;
			break;
		case SCSI_CAP_WIDE_XFER:
			if ((isp->isp_target_scsi_options[tgt] &
				SCSI_OPTIONS_WIDE) == 0) {
				break;
			} else if (tgtonly &&
			    (isp->isp_cap[tgt] & ISP_CAP_WIDE) == 0) {
				break;
			}
			rval = TRUE;
			break;
		case SCSI_CAP_TAGGED_QING:
			if ((isp->isp_target_scsi_options[tgt] &
				SCSI_OPTIONS_DR) == 0 ||
			    (isp->isp_target_scsi_options[tgt] &
				SCSI_OPTIONS_TAG) == 0) {
				break;
			} else if (tgtonly &&
			    (isp->isp_cap[tgt] & ISP_CAP_TAG) == 0) {
				break;
			}
			rval = TRUE;
			break;
		case SCSI_CAP_UNTAGGED_QING:
			rval = TRUE;
			break;
		case SCSI_CAP_PARITY:
			if (isp->isp_target_scsi_options[tgt] &
				SCSI_OPTIONS_PARITY) {
				rval = TRUE;
			}
			break;
		case SCSI_CAP_INITIATOR_ID:
			rval = isp->isp_initiator_id;
			break;
		case SCSI_CAP_ARQ:
			if (isp->isp_cap[tgt] & ISP_CAP_AUTOSENSE) {
				rval = TRUE;
			}
			break;
		case SCSI_CAP_LINKED_CMDS:
			break;
		case SCSI_CAP_RESET_NOTIFICATION:
			rval = TRUE;
			break;
		case SCSI_CAP_QFULL_RETRIES:
			rval = isp_i_handle_qfull_cap(isp, tgt, tgt,
				0, ISP_GET_QFULL_CAP,
				SCSI_CAP_QFULL_RETRIES);
			break;
		case SCSI_CAP_QFULL_RETRY_INTERVAL:
			rval = isp_i_handle_qfull_cap(isp, tgt, tgt,
				0, ISP_GET_QFULL_CAP,
				SCSI_CAP_QFULL_RETRY_INTERVAL);
			break;
		default:
			ISP_DEBUG(isp, SCSI_DEBUG,
			    "isp_scsi_getcap: unsupported");
			rval = UNDEFINED;
			break;
		}
		ISP_DEBUG2(isp, SCSI_DEBUG,
		"get cap: cap=%s,val=0x%x,tgtonly=0x%x,doset=0x%x,rval=%d\n",
		    cap, val, tgtonly, doset, rval);
	}
	ISP_MUTEX_EXIT(isp);

	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)) == 0 || ddi_in_panic());

	return (rval);
}

/*
 * Function name : isp_scsi_getcap(), isp_scsi_setcap()
 *
 * Return Values : see isp_i_commoncap()
 * Description	 : wrappers for isp_i_commoncap()
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
static int
isp_scsi_getcap(struct scsi_address *ap, char *cap, int whom)
{
	int e;
	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_GETCAP_START,
	    "isp_scsi_getcap_start");
	e = isp_i_commoncap(ap, cap, 0, whom, 0);
	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_GETCAP_END,
	    "isp_scsi_getcap_end");
	return (e);
}

static int
isp_scsi_setcap(struct scsi_address *ap, char *cap, int value, int whom)
{
	int e;
	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_SETCAP_START,
	    "isp_scsi_setcap_start");
	e = isp_i_commoncap(ap, cap, value, whom, 1);
	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_SETCAP_END,
	    "isp_scsi_setcap_end");
	return (e);
}

/*
 * Function name : isp_i_updatecap()
 *
 * Return Values : -1	failed.
 *		    0	success
 *
 * Description	 : sync's the isp target parameters with the desired
 *		   isp_caps for the specified target range
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
static int
isp_i_updatecap(struct isp *isp, int start_tgt, int end_tgt)
{
	register u_short cap, synch;
	struct isp_mbox_cmd mbox_cmd;
	int i;
	int rval = -1;

	i = start_tgt;

	do {
		isp_i_mbox_cmd_init(isp, &mbox_cmd, 2, 4,
		    ISP_MBOX_CMD_GET_TARGET_CAP,
		    (u_short) (i << 8), 0, 0, 0, 0);
		if (isp_i_mbox_cmd_start(isp, &mbox_cmd)) {
		    goto fail;
		}

		cap   = mbox_cmd.mbox_in[2];
		synch = mbox_cmd.mbox_in[3];

		ISP_DEBUG2(isp, SCSI_DEBUG,
	"updatecap:tgt=%d:cap=0x%x,isp_cap=0x%x,synch=0x%x,isp_synch=0x%x",
		    i, cap, isp->isp_cap[i], synch, isp->isp_synch[i]);

		/*
		 * enable or disable ERRSYNC
		 */
		if (isp->isp_cap[i] & (ISP_CAP_WIDE | ISP_CAP_SYNC)) {
			isp->isp_cap[i] |= ISP_CAP_ERRSYNC;
		} else {
			isp->isp_cap[i] &= ~ISP_CAP_ERRSYNC;
		}

		/*
		 * Set isp cap if different from ours.
		 */
		if (isp->isp_cap[i] != cap ||
		    isp->isp_synch[i] != synch) {
			ISP_DEBUG(isp, SCSI_DEBUG,
		    "Setting Target %d, new cap=0x%x (was 0x%x), synch=0x%x",
			    i, isp->isp_cap[i], cap, isp->isp_synch[i]);
			isp_i_mbox_cmd_init(isp, &mbox_cmd, 4, 4,
			    ISP_MBOX_CMD_SET_TARGET_CAP, (u_short)(i << 8),
			    isp->isp_cap[i], isp->isp_synch[i], 0, 0);
			if (isp_i_mbox_cmd_start(isp, &mbox_cmd)) {
				goto fail;
			}
		}
	} while (++i < end_tgt);

	rval = 0;

fail:
	return (rval);
}


/*
 * Function name : isp_i_update_sync_prop()
 *
 * Description	 : called  when isp reports renegotiation
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
static void
isp_i_update_sync_prop(struct isp *isp, struct isp_cmd *sp)
{
	register u_short cap, synch;
	struct isp_mbox_cmd mbox_cmd;
	int target = TGT(sp);

	ISP_DEBUG(isp, SCSI_DEBUG,
	    "tgt %d.%d: Negotiated new rate", TGT(sp), LUN(sp));

	/*
	 * Get new rate from ISP and save for later
	 * chip resets or scsi bus resets.
	 */
	isp_i_mbox_cmd_init(isp, &mbox_cmd, 2, 4,
	    ISP_MBOX_CMD_GET_TARGET_CAP,
	    (u_short) (target << 8), 0, 0, 0, 0);
	if (isp_i_mbox_cmd_start(isp, &mbox_cmd)) {
		return;
	}

	cap   = mbox_cmd.mbox_in[2];
	synch = mbox_cmd.mbox_in[3];

	ISP_DEBUG(isp, SCSI_DEBUG,
	"tgt=%d: cap=0x%x, isp_cap=0x%x, synch=0x%x, isp_synch=0x%x",
	    target, cap, isp->isp_cap[target], synch,
	    isp->isp_synch[target]);

	isp->isp_cap[target]   = cap;
	isp->isp_synch[target] = synch;
	isp_i_update_props(isp, target, cap, synch);
}


/*
 * Function name : isp_i_update_props()
 *
 * Description	 : Creates/modifies/removes a target sync mode speed,
 *		   wide, and TQ properties
 *		   If offset is 0 then asynchronous mode is assumed and the
 *		   property is removed, if it existed.
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
static void
isp_i_update_props(struct isp *isp, int tgt, u_short cap, u_short synch)
{
	char	property[32];
	int	xfer_speed = 0;
	int	offset = ((int)synch >> 8) & 0xff;
	int	flag;

	(void) sprintf(property, "target%x-sync-speed", tgt);

	if (synch) {
		if (cap & ISP_CAP_WIDE) {
			/* double xfer speed if wide has been enabled */
			xfer_speed = (1024 * 1024)/(((int)synch & 0xff) * 2);
		} else {
			xfer_speed = (1024 * 1024)/(((int)synch & 0xff) * 4);
		}
	}
	isp_i_update_this_prop(isp, property, xfer_speed,
	    sizeof (xfer_speed), offset);


	(void) sprintf(property, "target%x-TQ", tgt);
	flag = cap & ISP_CAP_TAG;
	isp_i_update_this_prop(isp, property, 0, 0, flag);

	(void) sprintf(property, "target%x-wide", tgt);
	flag = cap & ISP_CAP_WIDE;
	isp_i_update_this_prop(isp, property, 0, 0, flag);
}

/*
 * Creates/modifies/removes a property
 */
static void
isp_i_update_this_prop(struct isp *isp, char *property,
    int value, int size, int flag)
{
	int	length;

	dev_info_t *dip = isp->isp_dip;

	ISP_DEBUG(isp, SCSI_DEBUG,
		"isp_i_update_this_prop: %s=%x, size=%x, flag=%x",
		property, value, size, flag);
	if (ddi_getproplen(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    property, &length) == DDI_PROP_SUCCESS) {
		if (flag == 0) {
			if (ddi_prop_remove(DDI_DEV_T_NONE, dip, property) !=
			    DDI_PROP_SUCCESS) {
				goto fail;
			}
		} else if (size) {
			if (ddi_prop_modify(DDI_DEV_T_NONE, dip,
			    0, property,
			    (caddr_t)&value, size) != DDI_PROP_SUCCESS) {
				goto fail;
			}
		}
	} else if (flag) {
		if (ddi_prop_create(DDI_DEV_T_NONE, dip, 0, property,
		    (caddr_t)&value, size) != DDI_PROP_SUCCESS) {
			goto fail;
		}
	}
	return;

fail:
	ISP_DEBUG(isp, SCSI_DEBUG,
	    "cannot create/modify/remove %s property\n", property);
}


/*
 * Function name : isp_i_handle_qfull_cap()
 *
 * Return Values : FALSE - if setting qfull capability failed
 *		   TRUE	 - if setting qfull capability succeeded
 *		   -1    - if getting qfull capability succeeded
 *		   value - if getting qfull capability succeeded
 * Description   :
 *			Must called with response and request mutex
 *			held.
 */
static int
isp_i_handle_qfull_cap(struct isp *isp, u_short start, u_short end,
	int val, int flag_get_set, int flag_retry_interval)
{
	struct isp_mbox_cmd mbox_cmd;
	short rval = 0;
	u_short cmd;
	u_short value = (u_short) val;

	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)));
	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)));

	if (flag_retry_interval == SCSI_CAP_QFULL_RETRIES) {
		if (flag_get_set == ISP_GET_QFULL_CAP) {
			cmd = ISP_MBOX_CMD_GET_QFULL_RETRIES;
		} else {
			cmd = ISP_MBOX_CMD_SET_QFULL_RETRIES;
			rval = TRUE;
		}

	} else {
		if (flag_get_set == ISP_GET_QFULL_CAP) {
			cmd = ISP_MBOX_CMD_GET_QFULL_RETRY_INTERVAL;
		} else {
			cmd = ISP_MBOX_CMD_SET_QFULL_RETRY_INTERVAL;
			rval = TRUE;
		}
	}
	do {

		isp_i_mbox_cmd_init(isp, &mbox_cmd, 3, 3,
		cmd, value, start, 0, 0, 0);
		if (isp_i_mbox_cmd_start(isp, &mbox_cmd)) {
			if (flag_get_set == ISP_SET_QFULL_CAP) {
				rval = FALSE;
			} else {
				rval = -1;
			}
			break;
		}
		if (flag_get_set == ISP_GET_QFULL_CAP) {
			rval = mbox_cmd.mbox_in[2];
		}

	} while (++start < end);

	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)));
	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)));

	return ((int)rval);
}



/*
 * (de)allocator for non-std size cdb/pkt_private/status
 */
/*ARGSUSED*/
static int
isp_i_pkt_alloc_extern(struct isp *isp, struct isp_cmd *sp,
	int cmdlen, int tgtlen, int statuslen, int kf)
{
	register caddr_t cdbp, scbp, tgt;
	int failure = 0;
	register struct scsi_pkt *pkt = CMD2PKT(sp);

	tgt = cdbp = scbp = NULL;
	if (cmdlen > sizeof (union scsi_cdb)) {
		if ((cdbp = kmem_zalloc((size_t)cmdlen, kf)) == NULL) {
			failure++;
		} else {
			pkt->pkt_cdbp = (opaque_t)cdbp;
			sp->cmd_flags |= CFLAG_CDBEXTERN;
		}
	}
	if (tgtlen > PKT_PRIV_LEN) {
		if ((tgt = kmem_zalloc(tgtlen, kf)) == NULL) {
			failure++;
		} else {
			sp->cmd_flags |= CFLAG_PRIVEXTERN;
			pkt->pkt_private = tgt;
		}
	}
	if (statuslen > EXTCMDS_STATUS_SIZE) {
		if ((scbp = kmem_zalloc((size_t)statuslen, kf)) == NULL) {
			failure++;
		} else {
			sp->cmd_flags |= CFLAG_SCBEXTERN;
			pkt->pkt_scbp = (opaque_t)scbp;
		}
	}
	if (failure) {
		isp_i_pkt_destroy_extern(isp, sp);
	}
	return (failure);
}

static void
isp_i_pkt_destroy_extern(struct isp *isp, struct isp_cmd *sp)
{
	register struct scsi_pkt *pkt = CMD2PKT(sp);

	if (sp->cmd_flags & CFLAG_FREE) {
		cmn_err(CE_PANIC,
		    "isp_scsi_impl_pktfree: freeing free packet");
		/* NOTREACHED */
	}
	if (sp->cmd_flags & CFLAG_CDBEXTERN) {
		kmem_free((caddr_t)pkt->pkt_cdbp,
		    (size_t)sp->cmd_cdblen);
	}
	if (sp->cmd_flags & CFLAG_SCBEXTERN) {
		kmem_free((caddr_t)pkt->pkt_scbp,
		    (size_t)sp->cmd_scblen);
	}
	if (sp->cmd_flags & CFLAG_PRIVEXTERN) {
		kmem_free((caddr_t)pkt->pkt_private,
		    (size_t)sp->cmd_privlen);
	}

	sp->cmd_flags = CFLAG_FREE;
	kmem_cache_free(isp->isp_kmem_cache, (void *)sp);
}


/*
 * Function name : isp_scsi_init_pkt
 *
 * Return Values : pointer to scsi_pkt, or NULL
 * Description	 : Called by kernel on behalf of a target driver
 *		   calling scsi_init_pkt(9F).
 *		   Refer to tran_init_pkt(9E) man page
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
static struct scsi_pkt *
isp_scsi_init_pkt(struct scsi_address *ap, register struct scsi_pkt *pkt,
	struct buf *bp, int cmdlen, int statuslen, int tgtlen,
	int flags, int (*callback)(), caddr_t arg)
{
	int kf;
	register int failure = 1;
	register struct isp_cmd *sp;
	register struct isp *isp = ADDR2ISP(ap);
	struct isp_cmd	*new_cmd = NULL;
/* #define	ISP_TEST_ALLOC_EXTERN */
#ifdef ISP_TEST_ALLOC_EXTERN
	cdblen *= 3; statuslen *= 3; tgtlen *= 3;
#endif
	/*
	 * If we've already allocated a pkt once,
	 * this request is for dma allocation only.
	 * since isp usually has TQ targets with ARQ enabled, always
	 * allocate an extended pkt
	 */
	if (pkt == NULL) {
		/*
		 * First step of isp_scsi_init_pkt:  pkt allocation
		 */
		TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_PKTALLOC_START,
		    "isp_i_scsi_pktalloc_start");

		kf = (callback == SLEEP_FUNC)? KM_SLEEP: KM_NOSLEEP;
		sp = kmem_cache_alloc(isp->isp_kmem_cache, kf);

		/*
		 * Selective zeroing of the pkt.
		 * Zeroing cmd_pkt, cmd_cdb_un, cmd_pkt_private, and cmd_flags.
		 */
		if (sp) {
			register int *p;

			pkt = (struct scsi_pkt *)((u_char *)sp +
			    sizeof (struct isp_cmd) + EXTCMDS_STATUS_SIZE);
			sp->cmd_pkt		= pkt;
			pkt->pkt_ha_private	= (opaque_t)sp;
			pkt->pkt_scbp		= (opaque_t)((u_char *)sp +
						    sizeof (struct isp_cmd));
			sp->cmd_flags		= 0;
			sp->cmd_cdblen		= cmdlen;
			sp->cmd_scblen		= statuslen;
			sp->cmd_privlen		= tgtlen;
			pkt->pkt_address	= *ap;
			pkt->pkt_comp		= NULL;
			pkt->pkt_flags		= 0;
			pkt->pkt_time		= 0;
			pkt->pkt_resid		= 0;
			pkt->pkt_statistics	= 0;
			pkt->pkt_reason		= 0;
			pkt->pkt_cdbp		= (opaque_t)&sp->cmd_cdb_un;
			/* zero cdbp and pkt_private */
			p = (int *)pkt->pkt_cdbp;
			*p++	= 0;
			*p++	= 0;
			*p	= 0;
			pkt->pkt_private = (opaque_t)sp->cmd_pkt_private;
			p = (int *)pkt->pkt_private;
			*p++	= 0;
			*p	= 0;
			failure = 0;
		}


		/*
		 * cleanup or do more allocations
		 */
		if (failure ||
		    (cmdlen > sizeof (union scsi_cdb)) ||
		    (tgtlen > PKT_PRIV_LEN) ||
		    (statuslen > EXTCMDS_STATUS_SIZE)) {
			if (failure == 0) {
				failure = isp_i_pkt_alloc_extern(isp, sp,
				    cmdlen, tgtlen, statuslen, kf);
			}
			if (failure) {
				TRACE_0(TR_FAC_SCSI_ISP,
				    TR_ISP_SCSI_PKTALLOC_END,
				    "isp_i_scsi_pktalloc_end (Error)");
				return (NULL);
			}
		}

		new_cmd = sp;
	} else {
		sp = PKT2CMD(pkt);
		new_cmd = NULL;
	}

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_PKTALLOC_END,
		    "isp_i_scsi_pktalloc_end");

DMAGET:
	/*
	 * Second step of isp_scsi_init_pkt:  dma allocation
	 */
	if (bp) {
		register int cmd_flags, dma_flags;
		int rval;
		u_int dmacookie_count;

		TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_DMAGET_START,
		    "isp_i_scsi_dmaget_start");

		cmd_flags = sp->cmd_flags;

		/*
		 * Get the host adapter's dev_info pointer
		 */
		if (bp->b_flags & B_READ) {
			cmd_flags &= ~CFLAG_DMASEND;
			dma_flags = DDI_DMA_READ;
		} else {
			cmd_flags |= CFLAG_DMASEND;
			dma_flags = DDI_DMA_WRITE;
		}
		if (flags & PKT_CONSISTENT) {
			cmd_flags |= CFLAG_CMDIOPB;
			dma_flags |= DDI_DMA_CONSISTENT;
		}
		if (sp->cmd_dmahandle == NULL) {
			ddi_dma_attr_t	tmp_dma_attr;
			tmp_dma_attr = dma_ispattr;
			tmp_dma_attr.dma_attr_burstsizes = isp->isp_burst_size;

			if ((rval = ddi_dma_alloc_handle(isp->isp_dip,
				&tmp_dma_attr, callback, arg,
				&sp->cmd_dmahandle)) != DDI_SUCCESS)
				goto dma_failure;
		}
		rval = ddi_dma_buf_bind_handle(sp->cmd_dmahandle, bp, dma_flags,
			callback, arg, &sp->cmd_dmacookie,
			&dmacookie_count);
dma_failure:
		if (rval) {
			switch (rval) {
			case DDI_DMA_NORESOURCES:
				bioerror(bp, 0);
				break;
			case DDI_DMA_NOMAPPING:
			case DDI_DMA_BADATTR:
				bioerror(bp, EFAULT);
				break;
			case DDI_DMA_TOOBIG:
				bioerror(bp, EINVAL);
				break;
			}
			sp->cmd_flags = cmd_flags & ~CFLAG_DMAVALID;
			if (new_cmd) {
				isp_scsi_destroy_pkt(ap, pkt);
			}
			TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_DMAGET_ERROR_END,
			    "isp_i_scsi_dmaget_end (Error)");
			return ((struct scsi_pkt *)NULL);
		}
		sp->cmd_dmacount = bp->b_bcount;
		sp->cmd_flags = cmd_flags | CFLAG_DMAVALID;

		TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_DMAGET_END,
		    "isp_i_scsi_dmaget_end");
	}

	return (pkt);
}

/*
 * Function name : isp_scsi_destroy_pkt
 *
 * Return Values : none
 * Description	 : Called by kernel on behalf of a target driver
 *		   calling scsi_destroy_pkt(9F).
 *		   Refer to tran_destroy_pkt(9E) man page
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
static void
isp_scsi_destroy_pkt(struct scsi_address *ap, struct scsi_pkt *pkt)
{
	struct isp_cmd *sp = PKT2CMD(pkt);
	register struct isp *isp = ADDR2ISP(ap);

	/*
	 * isp_scsi_dmafree inline to make things faster
	 */
	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_DMAFREE_START,
	    "isp_scsi_dmafree_start");

	if (sp->cmd_flags & CFLAG_DMAVALID) {
		/*
		 * Free the mapping.
		 */
		(void) ddi_dma_unbind_handle(sp->cmd_dmahandle);
		sp->cmd_flags ^= CFLAG_DMAVALID;
	}

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_DMAFREE_END,
	    "isp_scsi_dmafree_end");

	/*
	 * Free the pkt
	 */
	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_PKTFREE_START,
	    "isp_i_scsi_pktfree_start");

	/*
	 * first test the most common case
	 */
	if ((sp->cmd_flags &
	    (CFLAG_FREE | CFLAG_CDBEXTERN | CFLAG_PRIVEXTERN |
	    CFLAG_SCBEXTERN)) == 0) {
		sp->cmd_flags = CFLAG_FREE;
		kmem_cache_free(isp->isp_kmem_cache, (void *)sp);
	} else {
		isp_i_pkt_destroy_extern(isp, sp);
	}
	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_PKTFREE_DONE,
	    "isp_i_scsi_pktfree_done");

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_PKTFREE_END,
	    "isp_i_scsi_pktfree_end");
}


/*
 * Function name : isp_scsi_dmafree()
 *
 * Return Values : none
 * Description	 : free dvma resources
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
/*ARGSUSED*/
static void
isp_scsi_dmafree(struct scsi_address *ap, struct scsi_pkt *pkt)
{
	register struct isp_cmd *sp = PKT2CMD(pkt);

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_DMAFREE_START,
	    "isp_scsi_dmafree_start");

	if (sp->cmd_flags & CFLAG_DMAVALID) {
		/*
		 * Free the mapping.
		 */
		(void) ddi_dma_unbind_handle(sp->cmd_dmahandle);
		sp->cmd_flags ^= CFLAG_DMAVALID;
	}

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_DMAFREE_END,
	    "isp_scsi_dmafree_end");

}


/*
 * Function name : isp_scsi_sync_pkt()
 *
 * Return Values : none
 * Description	 : sync dma
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
/*ARGSUSED*/
static void
isp_scsi_sync_pkt(struct scsi_address *ap, struct scsi_pkt *pkt)
{
	register int i;
	register struct isp_cmd *sp = PKT2CMD(pkt);

	if (sp->cmd_flags & CFLAG_DMAVALID) {
		i = ddi_dma_sync(sp->cmd_dmahandle, 0, 0,
			(sp->cmd_flags & CFLAG_DMASEND) ?
			DDI_DMA_SYNC_FORDEV : DDI_DMA_SYNC_FORCPU);
		if (i != DDI_SUCCESS) {
			cmn_err(CE_WARN, "isp: sync pkt failed");
		}
	}
}

/*
 * routine for reset notification setup, to register or cancel.
 */
static int
isp_scsi_reset_notify(struct scsi_address *ap, int flag,
void (*callback)(caddr_t), caddr_t arg)
{
	struct isp	*isp = ADDR2ISP(ap);
	return (scsi_hba_reset_notify_setup(ap, flag, callback, arg,
		ISP_REQ_MUTEX(isp), &isp->isp_reset_notify_listf));
}

/*
 * the waitQ is used when the request mutex is held. requests will go
 * in the waitQ which will be emptied just before releasing the request
 * mutex; the waitQ reduces the contention on the request mutex significantly
 *
 * Note that the waitq mutex is released *after* the request mutex; this
 * closes a small window where we empty the waitQ but before releasing
 * the request mutex, the waitQ is filled again. isp_scsi_start will
 * attempt to get the request mutex after adding the cmd to the waitQ
 * which ensures that after the waitQ is always emptied.
 */
#define	ISP_CHECK_WAITQ_TIMEOUT(isp)					\
	if (isp->isp_waitq_timeout == 0) {				\
		isp->isp_waitq_timeout = timeout(isp_i_check_waitQ,	\
		    (caddr_t)isp, drv_usectohz((clock_t)1000000));	\
	}

static void
isp_i_check_waitQ(struct isp *isp)
{
	mutex_enter(ISP_REQ_MUTEX(isp));
	mutex_enter(ISP_WAITQ_MUTEX(isp));
	isp->isp_waitq_timeout = 0;
	isp_i_empty_waitQ(isp);
	mutex_exit(ISP_REQ_MUTEX(isp));
	if (isp->isp_waitf) {
		ISP_CHECK_WAITQ_TIMEOUT(isp);
	}
	mutex_exit(ISP_WAITQ_MUTEX(isp));
}

/*
 * Function name : isp_i_empty_waitQ()
 *
 * Return Values : none
 *
 * Description	 : empties the waitQ
 *		   copies the head of the queue and zeroes the waitQ
 *		   calls isp_i_start_cmd() for each packet
 *		   if all cmds have been submitted, check waitQ again
 *		   before exiting
 *		   if a transport error occurs, complete packet here
 *		   if a TRAN_BUSY occurs, then restore waitQ and try again
 *		   later
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
static void
isp_i_empty_waitQ(struct isp *isp)
{
	register struct isp_cmd *sp, *head, *tail;
	int rval;

	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)));
	ASSERT(mutex_owned(ISP_WAITQ_MUTEX(isp)));

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_EMPTY_WAITQ_START,
	    "isp_i_empty_waitQ_start");

again:
	/*
	 * walk thru the waitQ and attempt to start the cmd
	 */
	while (isp->isp_waitf) {
		/*
		 * copy queue head, clear wait queue and release WAITQ_MUTEX
		 */
		head = isp->isp_waitf;
		tail = isp->isp_waitb;
		isp->isp_waitf = isp->isp_waitb = NULL;
		mutex_exit(ISP_WAITQ_MUTEX(isp));

		/*
		 * empty the local list
		 */
		while (head) {
			sp = head;
			head = sp->cmd_forw;
			sp->cmd_forw = NULL;
			if ((rval = isp_i_start_cmd(isp, sp)) !=
			    TRAN_ACCEPT) {
				ISP_DEBUG(isp, SCSI_DEBUG,
				"isp_i_empty_waitQ: transport failed (%x)",
				rval);

				/*
				 * if the isp could not handle more requests,
				 * (rval was TRAN_BUSY) then
				 * put all requests back on the waitQ before
				 * releasing the REQ_MUTEX
				 * if there was another transport error then
				 * do not put this packet back on the queue
				 * but complete it here
				 */
				if (rval == TRAN_BUSY) {
					sp->cmd_forw = head;
					head = sp;
				}

				mutex_enter(ISP_WAITQ_MUTEX(isp));
				if (isp->isp_waitf) {
					tail->cmd_forw = isp->isp_waitf;
					isp->isp_waitf = head;
				} else {
					isp->isp_waitf = head;
					isp->isp_waitb = tail;
				}

				if (rval != TRAN_BUSY) {
					struct scsi_pkt *pkt = CMD2PKT(sp);
					ISP_SET_REASON(sp, CMD_TRAN_ERR);
					if (pkt->pkt_comp) {
						mutex_exit(
						    ISP_WAITQ_MUTEX(isp));
						mutex_exit(ISP_REQ_MUTEX(isp));
						(*pkt->pkt_comp)(pkt);
						mutex_enter(ISP_REQ_MUTEX(isp));
						mutex_enter(
						    ISP_WAITQ_MUTEX(isp));
					}
					goto again;
				} else {
					/*
					 * request queue was full; try again
					 * 1 sec later
					 */
					ISP_CHECK_WAITQ_TIMEOUT(isp);
					goto exit;
				}
			}
		}
		mutex_enter(ISP_WAITQ_MUTEX(isp));
	}

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_EMPTY_WAITQ_END,
	    "isp_i_empty_waitQ_end");

exit:
	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)));
	ASSERT(mutex_owned(ISP_WAITQ_MUTEX(isp)));
}


/*
 * Function name : isp_scsi_start()
 *
 * Return Values : TRAN_FATAL_ERROR	- isp has been shutdown
 *		   TRAN_BUSY		- request queue is full
 *		   TRAN_ACCEPT		- pkt has been submitted to isp
 *					  (or is held in the waitQ)
 * Description	 : init pkt
 *		   check the waitQ and if empty try to get the request mutex
 *		   if this mutex is held, put request in waitQ and return
 *		   if we can get the mutex, start the request
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 *
 * XXX: We assume that dvma bounds checking is performed by
 *	the target driver!  Also, that sp is *ALWAYS* valid.
 *
 * Note: No support for > 1 data segment.
 */
static int
isp_scsi_start(struct scsi_address *ap, register struct scsi_pkt *pkt)
{
	struct isp_cmd *sp = PKT2CMD(pkt);
	register struct isp *isp;
	register int rval = TRAN_ACCEPT;
	register int cdbsize;
	register struct isp_request *req;

	isp = ADDR2ISP(ap);

	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(ISP_WAITQ_MUTEX(isp)) == 0 || ddi_in_panic());

	ISP_DEBUG2(isp, SCSI_DEBUG, "isp_scsi_start %x", sp);
	TRACE_1(TR_FAC_SCSI_ISP, TR_ISP_SCSI_START_START,
	    "isp_scsi_start_start isp = %x", isp);

	/*
	 * if we have a shutdown, return packet
	 */
	if (isp->isp_shutdown) {
		return (TRAN_FATAL_ERROR);
	}


	ASSERT(!(sp->cmd_flags & CFLAG_IN_TRANSPORT));
	sp->cmd_flags = (sp->cmd_flags & ~CFLAG_TRANFLAG) |
			    CFLAG_IN_TRANSPORT;
	pkt->pkt_reason = CMD_CMPLT;

	/*
	 * Lookup the cdb size.	 If can't figure out how big it is
	 * (e.g. 6, 10, or 12 bytes), use the cdb buffer size and punt.
	 * If the cdb buffer is bigger than 12-bytes, reject the pkt.
	 */
	if ((cdbsize =
	    scsi_cdb_size[CDB_GROUPID((char)*pkt->pkt_cdbp)]) == 0) {
		if ((cdbsize = sp->cmd_cdblen) > ISP_CDBMAX) {
			ISP_DEBUG(isp, SCSI_DEBUG,
			    "isp_scsi_start: cdb too big!");
			TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_START_BIG_CDB,
			    "isp_scsi_start (Big CDB)");
			return (TRAN_BADPKT);
		}
	}

	/*
	 * set up request in cmd_isp_request area so it is ready to
	 * go once we have the request mutex
	 * XXX do we need to zero each time
	 */
	req = &sp->cmd_isp_request;

	req->req_header.cq_entry_type = CQ_TYPE_REQUEST;
	req->req_header.cq_entry_count = 1;
	req->req_header.cq_flags = 0;
	req->req_header.cq_seqno = 0;
	req->req_reserved = 0;
	req->req_token = (opaque_t)sp;
	req->req_target = TGT(sp);
	req->req_lun_trn = LUN(sp);
	req->req_time = pkt->pkt_time;
	ISP_SET_PKT_FLAGS(pkt->pkt_flags, req->req_flags);

	/*
	 * Setup dma transfers data segments.
	 *
	 * NOTE: Only 1 dataseg supported.
	 */
	if (sp->cmd_flags & CFLAG_DMAVALID) {
		/*
		 * Have to tell isp which direction dma transfer is going.
		 */
		TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_START_DMA_START,
		    "isp_scsi_start");
		pkt->pkt_resid = sp->cmd_dmacount;

		if (sp->cmd_flags & CFLAG_CMDIOPB) {
			(void) ddi_dma_sync(sp->cmd_dmahandle, 0, (u_int)0,
			    DDI_DMA_SYNC_FORDEV);
		}

		req->req_seg_count = 1;
		req->req_dataseg[0].d_count = sp->cmd_dmacount;
		req->req_dataseg[0].d_base = sp->cmd_dmacookie.dmac_address;
		if (sp->cmd_flags & CFLAG_DMASEND) {
			req->req_flags |= ISP_REQ_FLAG_DATA_WRITE;
		} else {
			req->req_flags |= ISP_REQ_FLAG_DATA_READ;
		}
		TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_START_DMA_END,
		    "isp_scsi_start");
	} else {
		req->req_seg_count = 0;
		req->req_dataseg[0].d_count = 0;
	}

	ISP_LOAD_REQUEST_CDB(req, sp, cdbsize);

	/*
	 * calculate deadline from pkt_time
	 * Instead of multiplying by 100 (ie. HZ), we multiply by 128 so
	 * we can shift and at the same time have a 28% grace period
	 * we ignore the rare case of pkt_time == 0 and deal with it
	 * in isp_i_watch()
	 */
#ifdef OLDTIMEOUT
	sp->cmd_deadline = lbolt + (pkt->pkt_time * 128);
	sp->cmd_start_time = lbolt;
#endif
	/*
	 * the normal case is a non-polled cmd, so deal with that first
	 */
	if ((pkt->pkt_flags & FLAG_NOINTR) == 0) {
		/*
		 * isp request mutex can be held for a long time; therefore,
		 * if request mutex is held, we queue the packet in a waitQ
		 * Consequently, we now need to check the waitQ before every
		 * release of the request mutex
		 *
		 * if the waitQ is non-empty, add cmd to waitQ to preserve
		 * some order
		 */
		mutex_enter(ISP_WAITQ_MUTEX(isp));
		if (isp->isp_waitf ||
		    (mutex_tryenter(ISP_REQ_MUTEX(isp)) == 0)) {
			if (isp->isp_waitf == NULL) {
				isp->isp_waitb = isp->isp_waitf = sp;
				sp->cmd_forw = NULL;
			} else {
				struct isp_cmd *dp = isp->isp_waitb;
				dp->cmd_forw = isp->isp_waitb = sp;
				sp->cmd_forw = NULL;
			}
			/*
			 * this is really paranoia and shouldn't
			 * be necessary
			 */
			if (mutex_tryenter(ISP_REQ_MUTEX(isp))) {
				isp_i_empty_waitQ(isp);
				mutex_exit(ISP_REQ_MUTEX(isp));
			}
			mutex_exit(ISP_WAITQ_MUTEX(isp));
		} else {
			mutex_exit(ISP_WAITQ_MUTEX(isp));

			rval = isp_i_start_cmd(isp, sp);
			if (rval == TRAN_BUSY) {
				/*
				 * put request back at the head of the waitQ
				 */
				mutex_enter(ISP_WAITQ_MUTEX(isp));
				sp->cmd_forw = isp->isp_waitf;
				isp->isp_waitf = sp;
				if (isp->isp_waitb == NULL) {
					isp->isp_waitb = sp;
				}
				mutex_exit(ISP_WAITQ_MUTEX(isp));
				rval = TRAN_ACCEPT;
			}
			ISP_CHECK_WAITQ_AND_EXIT_REQ_MUTEX(isp);
#ifndef OLDTIMEOUT
			isp->isp_alive = 1;
#endif
		}
	} else {
		rval = isp_i_polled_cmd_start(isp, sp);
	}
done:
	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(ISP_WAITQ_MUTEX(isp)) == 0 || ddi_in_panic());
	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_START_END, "isp_scsi_start_end");
	return (rval);
}


/*
 * Function name : isp_i_start_cmd()
 *
 * Return Values : TRAN_ACCEPT	- request is in the isp request queue
 *		   TRAN_BUSY	- request queue is full
 *
 * Description	 : if there is space in the request queue, copy over request
 *		   enter normal requests in the isp_slots list
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
static int
isp_i_start_cmd(register struct isp *isp, register struct isp_cmd *sp)
{
	register struct isp_request *req;
	register short slot;
	register struct scsi_pkt *pkt = CMD2PKT(sp);

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_START_CMD_START,
	    "isp_i_start_cmd_start");

	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)));
	ASSERT(mutex_owned(ISP_WAITQ_MUTEX(isp)) == 0 || ddi_in_panic());

	ISP_DEBUG2(isp, SCSI_DEBUG, "isp_i_start_cmd: sp=%x, pkt_time=%x",
	    sp, pkt->pkt_time);

	/*
	 * Check to see how much space is available in the
	 * Request Queue, save this so we do not have to do
	 * a lot of PIOs
	 */
	if (isp->isp_queue_space == 0) {
		ISP_UPDATE_QUEUE_SPACE(isp);

		/*
		 * Check now to see if the queue is still full
		 * Report TRAN_BUSY if we are full
		 */
		if (isp->isp_queue_space == 0) {
			TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_START_CMD_Q_FULL_END,
			    "isp_i_start_cmd_end (Queue Full)");
			return (TRAN_BUSY);
		}
	}

	/* The ability to disable auto request sense per packet */
	if ((sp->cmd_scblen <= sizeof (struct scsi_arq_status)) &&
	    (isp->isp_cap[TGT(sp)] & ISP_CAP_AUTOSENSE)) {
		sp->cmd_isp_request.req_flags |= ISP_REQ_FLAG_DISARQ;
	}

	/*
	 * Put I/O request in isp request queue to run.
	 * Get the next request in pointer.
	 */
	ISP_GET_NEXT_REQUEST_IN(isp, req);

	/*
	 * Copy 40 of the  64 byte request into the request queue
	 * (only 1 data seg)
	 */
	B40COPY(&sp->cmd_isp_request, req);

	/*
	 * Use correct offset and size for syncing
	 */
	(void) ddi_dma_sync(isp->isp_dmahandle,
	    (off_t)(isp->isp_request_in * sizeof (struct isp_request)),
	    (u_int)sizeof (struct isp_request),
	    DDI_DMA_SYNC_FORDEV);

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_START_CMD_AFTER_SYNC,
	    "isp_i_start_cmd_after_sync");

	/*
	 * Find a free slot to store the pkt in for crash protection for
	 * non-polled commands. Polled commands do not have to be kept
	 * track of since the busy wait loops keep track of them.
	 *
	 * We should *ALWAYS* be able to find a free slot; or we're broke!
	 */
	/* LOCK UNNEEDED BECAUSE "Write Only": isp->isp_response_mutex */
	if ((pkt->pkt_flags & FLAG_NOINTR) == 0) {
		slot = isp->isp_free_slot++;
		if (isp->isp_free_slot >= (u_short)ISP_MAX_SLOTS) {
			isp->isp_free_slot = 0;
		}
		if (isp->isp_slots[slot].slot_cmd) {
			slot = isp_i_find_freeslot(isp);
		}

		/*
		 * Update ISP deadline time for new cmd sent to ISP
		 * There is a race here with isp_i_watch(); if the compiler
		 * rearranges this code, then this may cause some false
		 * timeouts
		 */
#ifdef OLDTIMEOUT
		isp->isp_slots[slot].slot_deadline = sp->cmd_deadline;
#endif
		sp->cmd_slot = slot;
		isp->isp_slots[slot].slot_cmd = sp;
	}
	/* LOCK NEEDED: isp->isp_response_mutex */

	/*
	 * Tell isp it's got a new I/O request...
	 */
	ISP_SET_REQUEST_IN(isp);
	isp->isp_queue_space--;
	ASSERT(isp->isp_queue_space >= 0);

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_START_CMD_END,
	    "isp_i_start_cmd_end");

	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)));
	ASSERT(mutex_owned(ISP_WAITQ_MUTEX(isp)) == 0 || ddi_in_panic());
	return (TRAN_ACCEPT);
}


/*
 * Function name : isp_intr_loop(), isp_intr()
 *
 * Return Values : DDI_INTR_CLAIMED, DDI_INTR_UNCLAIMED
 *
 * Description	 :
 * On sun4c/4m, controllers down the intr chain do not get serviced as
 * well as the first one in the chain. The interrupt dispatcher does
 * not check the rest of the chain if a handler claims the interrupt. On the
 * next interrupt, the dispatcher starts again at the first entry in the
 * chain.  Therefore, we loop thru all isp's looking for interrupts.
 * Fortunately, PIOs aren't very expensive on sun4m and sun4c but
 * other type of controllers down the line will suffer from this delay
 *
 * On sun4d, there will be multiple instances of isp_intr running on
 * different processors, so there is no need for a loop.
 *
 * Context:	   called by interrupt thread.
 */
/*ARGSUSED*/
static u_int
isp_intr_loop(caddr_t arg)
{
	register struct isp *isp, *active;
	register int serviced, n_intrs;

	/* READ-ONLY VARIABLES: isp::isp_next, isp_head */

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_INTR_LOOP_START,
	    "isp_intr_loop_start");

	active = isp =	isp_last;

	serviced = n_intrs = 0;
	for (;;) {
		if (ISP_INT_PENDING(isp)) {
			if ((isp_intr((caddr_t)isp)) == DDI_INTR_CLAIMED) {
				active = isp;
				serviced++;
				n_intrs++;
			}
		}

		isp = isp->isp_next;
		if (isp == NULL) {
			isp = isp_head;
		}
		if (isp == isp_last) {
			/*
			 * if we checked all isp's and no interrupts
			 * were serviced during this loop, break
			 */
			if (serviced == 0) {
				break;
			} else {
				serviced = 0;
			}
		}
	}

	/*
	 * the next time, start with an active isp since it is the most likely
	 * one to interrupt next
	 */
	isp_last = active;

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_INTR_LOOP_END,
	    "isp_intr_loop_end");
	return ((n_intrs == 0)? DDI_INTR_UNCLAIMED: DDI_INTR_CLAIMED);
}


static u_int
isp_intr(caddr_t arg)
{
	register struct isp_cmd *sp;
	register struct isp_cmd *head, *tail;
	register u_short response_in;
	register struct isp_response *resp;
	register struct isp *isp = (struct isp *)arg;
	register struct isp_slot *isp_slot;
	register int n;
	register off_t offset;
	register u_int sync_size;

	ISP_DEBUG2(isp, SCSI_DEBUG, "isp_intr entry");

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_INTR_START, "isp_intr_start");

#ifdef ISP_PERF
	isp->isp_intr_count++;
#endif

	do {
again:
		/*
		 * head list collects completed packets for callback later
		 */
		head = tail = NULL;

		/*
		 * Assume no mailbox events (e.g. mailbox cmds, asynch
		 * events, and isp dma errors) as common case.
		 */
		if (ISP_CHECK_SEMAPHORE_LOCK(isp) == 0) {
			mutex_enter(ISP_RESP_MUTEX(isp));
			/*
			 * Workaround a hardware problem bugid 1220411.
			 */
			if (isp->isp_mbox.mbox_flags &
			    ISP_MBOX_CMD_FLAGS_Q_NOT_INIT) {
				ISP_CLEAR_RISC_INT(isp);
				mutex_exit(ISP_RESP_MUTEX(isp));
				continue;
			}

			/*
			 * Loop through completion response queue and post
			 * completed pkts.  Check response queue again
			 * afterwards in case there are more and process them
			 * to keep interrupt response time low under heavy load.
			 *
			 * To conserve PIO's, we only update the isp response
			 * out index after draining it.
			 */
			isp->isp_response_in =
			    response_in = ISP_GET_RESPONSE_IN(isp);
			ASSERT(!(response_in >= ISP_MAX_RESPONSES));

			/*
			 * Calculate how many requests there are in the queue
			 * If this is < 0, then we are wrapping around
			 * and syncing the packets need two separate syncs
			 */
			n = response_in - isp->isp_response_out;
			offset = (off_t)((ISP_MAX_REQUESTS *
			    sizeof (struct isp_request)) +
			    (isp->isp_response_out *
			    sizeof (struct isp_response)));

			if (n == 1) {
				sync_size =
				    ((u_int) sizeof (struct isp_response));
			} else if (n > 0) {
				sync_size =
				    n * ((u_int) sizeof (struct isp_response));
			} else if (n < 0) {
				sync_size =
				    (ISP_MAX_REQUESTS - isp->isp_response_out) *
					((u_int) sizeof (struct isp_response));

				/*
				 * we wrapped around and need an extra sync
				 */
				(void) ddi_dma_sync(isp->isp_dmahandle,
				    (off_t)((ISP_MAX_REQUESTS *
					sizeof (struct isp_request))),
				    response_in *
					((u_int) sizeof (struct isp_response)),
				    DDI_DMA_SYNC_FORKERNEL);

				n = ISP_MAX_REQUESTS - isp->isp_response_out +
				    response_in;
			} else {
				goto update;
			}

			(void) ddi_dma_sync(isp->isp_dmahandle,
			    (off_t)offset, sync_size, DDI_DMA_SYNC_FORKERNEL);
			ISP_DEBUG2(isp, SCSI_DEBUG,
			    "sync: n=%d, in=%d, out=%d, offset=%d, size=%d\n",
			    n, response_in,
			    isp->isp_response_out, offset, sync_size);

			while (n-- > 0) {
				ISP_GET_NEXT_RESPONSE_OUT(isp, resp);
				ASSERT(resp != NULL);

				/*
				 * Paranoia:  This should never happen.
				 */
				if (ISP_IS_RESPONSE_INVALID(resp)) {
					ISP_DEBUG(isp, SCSI_DEBUG,
				"invalid response:in=%x, out=%x, mbx5=%x",
					isp->isp_response_in,
					isp->isp_response_out,
					isp->isp_reg->isp_mailbox5);
					continue;
				}
				sp = (struct isp_cmd *)resp->resp_token;
#ifdef ISPDEBUG
				ASSERT(sp != NULL);
				ASSERT((sp->cmd_flags & CFLAG_COMPLETED) == 0);
				ASSERT((sp->cmd_flags & CFLAG_FINISHED) == 0);
				sp->cmd_flags |= CFLAG_FINISHED;
				ISP_DEBUG2(isp, SCSI_DEBUG,
				    "isp_intr %x done, pkt_time=%x", sp,
				    CMD2PKT(sp)->pkt_time);
#endif

				/*
				 * copy over response packet in sp
				 * if status != 0 then copy over entire packet
				 * no need to check whether it is a check
				 * condition or something else.
				 */
				if (resp->resp_scb == 0) {
					B24COPY(resp,
					    &sp->cmd_isp_response);
				} else {
					B64COPY(resp,
					    &sp->cmd_isp_response);
				}

				/*
				 * Check response header flags.
				 */
				if (resp->resp_header.cq_flags &
				    CQ_FLAG_ERR_MASK) {
					ISP_DEBUG(isp, SCSI_DEBUG,
					    "flag error");
					if (isp_i_response_error(isp, resp) ==
					    ACTION_IGNORE) {
						continue;
					}
				}

#ifdef ISPDEBUG
				/*
				 * clear response header
				 */
				{
					register long long *header =
					    (long long *) resp;
					*header = NULL;
				}
#endif

				/*
				 * Update isp deadman timer list before
				 * doing the callback and while we are
				 * holding the response mutex.
				 * note that polled cmds are not in
				 * isp_slots list
				 */
				isp_slot = &isp->isp_slots[sp->cmd_slot];
				if (isp_slot->slot_cmd == sp) {
					isp_slot->slot_cmd = NULL;
#ifdef OLDTIMEOUT
					isp_slot->slot_deadline = 0;
#endif
				}

				if (head) {
					tail->cmd_forw = sp;
					tail = sp;
					tail->cmd_forw = NULL;
				} else {
					tail = head = sp;
					sp->cmd_forw = NULL;
				}

				TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_INTR_Q_END,
				    "isp_intr_queue_end");
			}
update:
			ISP_SET_RESPONSE_OUT(isp);
			ISP_CLEAR_RISC_INT(isp);
			mutex_exit(ISP_RESP_MUTEX(isp));

			if (head) {
				isp_i_call_pkt_comp(head);
			}

			TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_INTR_AGAIN,
			    "isp_intr_again");
		} else {
			if (isp_i_handle_mbox_cmd(isp) != ISP_AEN_SUCCESS) {
				return (DDI_INTR_CLAIMED);
			}
			/*
			 * if there was a reset then check the response
			 * queue again
			 */
			goto again;
		}

	} while (ISP_INT_PENDING(isp));
#ifndef OLDTIMEOUT
	isp->isp_alive = 1;
#endif


	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_INTR_END, "isp_intr_end");
	return (DDI_INTR_CLAIMED);
}

/*
 * Function name : isp_i_call_pkt_comp()
 *
 * Return Values : none
 *
 * Description	 :
 *		   callback into target driver
 *		   argument is a  NULL-terminated list of packets
 *		   copy over stuff from response packet
 *
 * Context	 : Can be called by interrupt thread.
 */
#ifdef ISPDEBUG
static int isp_test_reason;
/* VARIABLES PROTECTED BY "No Mutex Needed": isp_test_reason	*/
#endif

static void
isp_i_call_pkt_comp(register struct isp_cmd *head)
{
	register struct isp *isp;
	register struct isp_cmd *sp;
	register struct scsi_pkt *pkt;
	register struct isp_response *resp;
	register u_char status;

	isp = CMD2ISP(head);
	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(ISP_WAITQ_MUTEX(isp)) == 0 || ddi_in_panic());

	while (head) {
		TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_CALL_PKT_COMP_START,
		    "isp_i_call_pkt_comp_start");
		sp = head;
		pkt = CMD2PKT(sp);
		head = sp->cmd_forw;

		ASSERT(sp->cmd_flags & CFLAG_FINISHED);

		resp = &sp->cmd_isp_response;

		status = pkt->pkt_scbp[0] = (u_char) resp->resp_scb;
		if (pkt->pkt_reason == CMD_CMPLT) {
			pkt->pkt_reason = (u_char) resp->resp_reason;
			if (pkt->pkt_reason > CMD_UNX_BUS_FREE) {
				if (pkt->pkt_reason == TAG_REJECT) {
					pkt->pkt_reason = CMD_TAG_REJECT;
				} else {
					pkt->pkt_reason = CMD_TRAN_ERR;
				}
			}
		}
		pkt->pkt_state = ISP_GET_PKT_STATE(resp->resp_state);
		pkt->pkt_statistics = (u_long)
		    (ISP_GET_PKT_STATS(resp->resp_status_flags));
		pkt->pkt_resid = (long)resp->resp_resid;

		/*
		 * Check to see if the ISP has negotiated a new sync
		 * rate with the device and store that information
		 * for a latter date
		 */
		if (pkt->pkt_statistics & ISP_STAT_NEGOTIATE) {
			ISP_MUTEX_ENTER(isp);
			isp_i_update_sync_prop(isp, sp);
			pkt->pkt_statistics &= ~ISP_STAT_NEGOTIATE;
			ISP_MUTEX_EXIT(isp);
		}

		/*
		 * check for parity errors
		 */
		if (pkt->pkt_statistics & STAT_PERR) {
			isp_i_log(isp, CE_WARN, "Parity Error");
		}


#ifdef ISPDEBUG
		if (isp_test_reason &&
		    (pkt->pkt_reason == CMD_CMPLT)) {
			pkt->pkt_reason = (u_char) isp_test_reason;
			if (isp_test_reason == CMD_ABORTED) {
				pkt->pkt_statistics |= STAT_ABORTED;
			}
			if (isp_test_reason == CMD_RESET) {
				pkt->pkt_statistics |=
				    STAT_DEV_RESET | STAT_BUS_RESET;
			}
			isp_test_reason = 0;
		}
		if (pkt->pkt_resid || status ||
		    pkt->pkt_reason) {
			register u_char *cp;
			char buf[128];
			register int i;

			ISP_DEBUG(isp, SCSI_DEBUG,
	"tgt %d.%d: resid=%x,reason=%s,status=%x,stats=%x,state=%x",
				TGT(sp), LUN(sp), pkt->pkt_resid,
				scsi_rname(pkt->pkt_reason),
				(u_long) status,
				(u_long) pkt->pkt_statistics,
				(u_long) pkt->pkt_state);

			cp = (u_char *) pkt->pkt_cdbp;
			buf[0] = '\0';
			for (i = 0; i < (int)sp->cmd_cdblen; i++) {
				(void) sprintf(
				    &buf[strlen(buf)], " 0x%x", *cp++);
				if (strlen(buf) > 124) {
					break;
				}
			}
			ISP_DEBUG(isp, SCSI_DEBUG,
			"\tcflags=%x, cdb dump: %s", sp->cmd_flags, buf);

			if (pkt->pkt_reason == CMD_RESET) {
				ASSERT(pkt->pkt_statistics &
				    (STAT_BUS_RESET | STAT_DEV_RESET));
			} else if (pkt->pkt_reason ==
			    CMD_ABORTED) {
				ASSERT(pkt->pkt_statistics &
				    STAT_ABORTED);
			}
		}
		if (pkt->pkt_state & STATE_XFERRED_DATA) {
			if (ispdebug > 1 && pkt->pkt_resid) {
				ISP_DEBUG(isp, SCSI_DEBUG,
				    "%d.%d finishes with %d resid",
				    TGT(sp), LUN(sp), pkt->pkt_resid);
			}
		}
#endif	/* ISPDEBUG */


		/*
		 * was there a check condition and auto request sense?
		 * fake some arqstat fields
		 */
		if (status && ((pkt->pkt_state &
		    (STATE_GOT_STATUS | STATE_ARQ_DONE)) ==
		    (STATE_GOT_STATUS | STATE_ARQ_DONE))) {
			isp_i_handle_arq(isp, sp);
		}


		/*
		 * if data was xferred and this was an IOPB, we need
		 * to do a dma sync
		 */
		if ((sp->cmd_flags & CFLAG_CMDIOPB) &&
		    (pkt->pkt_state & STATE_XFERRED_DATA)) {

			/*
			 * only one segment yet
			 */
			(void) ddi_dma_sync(sp->cmd_dmahandle, 0,
			    (u_int)0, DDI_DMA_SYNC_FORCPU);
		}


		ASSERT(sp->cmd_flags & CFLAG_IN_TRANSPORT);
		ASSERT(sp->cmd_flags & CFLAG_FINISHED);
		ASSERT((sp->cmd_flags & CFLAG_COMPLETED) == 0);

		sp->cmd_flags = (sp->cmd_flags & ~CFLAG_IN_TRANSPORT) |
				CFLAG_COMPLETED;

		/*
		 * Call packet completion routine if FLAG_NOINTR is not set.
		 * If FLAG_NOINTR is set turning on CFLAG_COMPLETED in line
		 * above will cause busy wait loop in
		 * isp_i_polled_cmd_start() to exit.
		 */
		if (((pkt->pkt_flags & FLAG_NOINTR) == 0) &&
		    pkt->pkt_comp) {
			(*pkt->pkt_comp)(pkt);
		}

		TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_CALL_PKT_COMP_END,
		    "isp_i_call_pkt_comp_end");
	}
	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(ISP_WAITQ_MUTEX(isp)) == 0 || ddi_in_panic());
}


/*
 * Function name : isp_i_handle_mbox_cmd()
 *
 * Description	 : called from isp_intr() to handle a mbox or async event
 *
 * Context	 : Can be called by interrupt thread.
 */
static int
isp_i_handle_mbox_cmd(struct isp *isp)
{
	register int aen = ISP_AEN_FAILURE;
	register short event = isp->isp_reg->isp_mailbox0;

	/*
	 * process a mailbox event
	 */
#ifdef ISP_PERF
	isp->isp_rpio_count += 1;
#endif
	ISP_DEBUG2(isp, SCSI_DEBUG, "isp_intr: event= 0x%x", event);
	if (event & ISP_MBOX_EVENT_ASYNCH) {
		ISP_MUTEX_ENTER(isp);
		aen = isp_i_async_event(isp, event);
		ISP_MUTEX_EXIT(isp);
		TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_INTR_ASYNC_END,
		    "isp_intr_end (Async Event)");
	} else {
		isp_i_mbox_cmd_complete(isp);
		TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_INTR_MBOX_END,
		    "isp_intr_end (Mailbox Event)");
	}
	return (aen);
}

/*
 * Function name : isp_i_handle_arq()
 *
 * Description	 : called on an autorequest sense condition, sets up arqstat
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
static void
isp_i_handle_arq(struct isp *isp, struct isp_cmd *sp)
{
	register struct isp_response *resp = &sp->cmd_isp_response;
	char status;
	register struct scsi_pkt *pkt = CMD2PKT(sp);

	if (sp->cmd_scblen >= sizeof (struct scsi_arq_status)) {
		register struct scsi_arq_status *arqstat;

		ISP_DEBUG(isp, SCSI_DEBUG,
		    "tgt %d.%d: auto request sense", TGT(sp), LUN(sp));

		arqstat = (struct scsi_arq_status *)(pkt->pkt_scbp);
		status = pkt->pkt_scbp[0];
		bzero((caddr_t)arqstat, sizeof (struct scsi_arq_status));

		/*
		 * use same statistics as the original cmd
		 */
		arqstat->sts_rqpkt_statistics = pkt->pkt_statistics;
		arqstat->sts_rqpkt_state =
		    (STATE_GOT_BUS | STATE_GOT_TARGET |
		    STATE_SENT_CMD | STATE_XFERRED_DATA | STATE_GOT_STATUS);
		if (resp->resp_rqs_count <
		    sizeof (struct scsi_extended_sense)) {
			arqstat->sts_rqpkt_resid =
				sizeof (struct scsi_extended_sense) -
				resp->resp_rqs_count;
		}
		bcopy((caddr_t)resp->resp_request_sense,
		    (caddr_t)&arqstat->sts_sensedata,
		    sizeof (struct scsi_extended_sense));
		/*
		 * restore status which was wiped out by bzero
		 */
		pkt->pkt_scbp[0] = status;
	} else {
		/*
		 * bad packet; can't copy over ARQ data
		 * XXX need CMD_BAD_PKT
		 */
		ISP_SET_REASON(sp, CMD_TRAN_ERR);
	}
}


/*
 * Function name : isp_i_find_free_slot()
 *
 * Return Values : empty slot  number
 *
 * Description	 : find an empty slot in the isp_slots list
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
static int
isp_i_find_freeslot(struct isp *isp)
{
	register char found = 0;
	int slot = isp->isp_free_slot;
	int i;

	/*
	 * If slot in use, scan for a free one. Walk thru
	 * isp_slots, starting at current tag
	 * this should rarely happen.
	 */
	ISP_DEBUG(isp, SCSI_DEBUG, "found in use slot %d", slot);
	for (i = 0; i < (ISP_MAX_SLOTS - 1); i++) {
		slot = isp->isp_free_slot++;
		if (isp->isp_free_slot >=
		    (u_short)ISP_MAX_SLOTS) {
			isp->isp_free_slot = 0;
		}
		if (isp->isp_slots[slot].slot_cmd == NULL) {
			found = 1;
			break;
		}
		ISP_DEBUG2(isp, SCSI_DEBUG, "found in use slot %d", slot);
	}
	if (!found) {
		isp_i_log(0, CE_PANIC, "isp: no free slots!!!");
	}
	ISP_DEBUG(isp, SCSI_DEBUG, "found free slot %d", slot);
	return (slot);
}


/*
 * Function name : isp_i_response_error()
 *
 * Return Values : ACTION_CONTINUE
 *		   ACTION_IGNORE
 * Description	 : handle isp flag event
 *
 * Context	 : Can be called by interrupt thread.
 */
static int
isp_i_response_error(struct isp *isp, struct isp_response *resp)
{
	register struct isp_cmd *sp = (struct isp_cmd *)resp->resp_token;
	int rval;

	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)));
	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)) == 0 || ddi_in_panic());

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_RESPONSE_ERROR_START,
	    "isp_i_response_error_start");
	/*
	 * isp unable to run request because of queue full condition.
	 * Take it out of the slot struct and then call isp_scsi_start()
	 * XXX wouldn't it be better to put it back on waitQ??
	 */
	if (resp->resp_header.cq_flags & CQ_FLAG_FULL) {
		struct isp_mbox_cmd mbox_cmd;

		ISP_DEBUG(isp, SCSI_DEBUG,
		    "isp_i_response_error: queue full");

		mutex_enter(ISP_REQ_MUTEX(isp));
		isp_i_mbox_cmd_init(isp, &mbox_cmd, 2, 4,
		    ISP_MBOX_CMD_GET_DEVICE_QUEUE_STATE,
		    (u_short)TGT(sp), 0, 0, 0, 0);
		(void) isp_i_mbox_cmd_start(isp, &mbox_cmd);
		mutex_exit(ISP_REQ_MUTEX(isp));

		ISP_DEBUG(isp, SCSI_DEBUG,
		    "Q State = %x, Exec cmd count = 0x%x, Total cmds = 0x%x",
		    mbox_cmd.mbox_in[1],
		    mbox_cmd.mbox_in[2],
		    mbox_cmd.mbox_in[3]);

		if (isp->isp_slots[sp->cmd_slot].slot_cmd == sp) {
			isp->isp_slots[sp->cmd_slot].slot_cmd = NULL;
			sp->cmd_slot = 0;
		}
		sp->cmd_flags &= ~CFLAG_IN_TRANSPORT;
		mutex_exit(ISP_RESP_MUTEX(isp));
		(void) isp_scsi_start(CMD2ADDR(sp), CMD2PKT(sp));
		mutex_enter(ISP_RESP_MUTEX(isp));
		rval = ACTION_IGNORE;
	} else {

		/*
		 * For bad request pkts, flag error and try again.
		 * This should *NEVER* happen.
		 */
		ISP_SET_REASON(sp, CMD_TRAN_ERR);
		if (resp->resp_header.cq_flags & CQ_FLAG_BADPACKET) {
			isp_i_log(isp, CE_WARN, "Bad request pkt");
		} else if (resp->resp_header.cq_flags & CQ_FLAG_BADHEADER) {
			isp_i_log(isp, CE_WARN, "Bad request pkt header");
		}
		rval = ACTION_CONTINUE;
	}

	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)));
	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)) == 0 || ddi_in_panic());


	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_RESPONSE_ERROR_END,
	    "isp_i_response_error_end");

	return (rval);
}


/*
 * Function name : isp_i_polled_cmd_start()
 *
 * Return Values : TRAN_ACCEPT	if transaction was accepted
 *		   TRAN_BUSY	if I/O could not be started
 *		   TRAN_ACCEPT	if I/O timed out, pkt fields indicate error
 *
 * Description	 : Starts up I/O in normal fashion by calling isp_i_start_cmd().
 *		   Busy waits for I/O to complete or timeout.
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
static int
isp_i_polled_cmd_start(struct isp *isp, struct isp_cmd *sp)
{
	register int delay_loops;
	register int rval;
	register struct scsi_pkt *pkt = CMD2PKT(sp);

	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)) == 0 || ddi_in_panic());

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_RUN_POLLED_CMD_START,
	    "isp_i_polled_cmd_start_start");


	/*
	 * set timeout to SCSI_POLL_TIMEOUT for non-polling
	 * commands that do not have this field set
	 */
	if (pkt->pkt_time == 0) {
		pkt->pkt_time = SCSI_POLL_TIMEOUT;
	}


	/*
	 * try and start up command
	 */
	mutex_enter(ISP_REQ_MUTEX(isp));
	rval = isp_i_start_cmd(isp, sp);
	ISP_CHECK_WAITQ_AND_EXIT_REQ_MUTEX(isp);
	if (rval != TRAN_ACCEPT) {
		goto done;
	}

	/*
	 * busy wait for command to finish ie. till CFLAG_COMPLETED is set
	 */
retry:
	delay_loops = ISP_TIMEOUT_DELAY(
	    (pkt->pkt_time + (2 * ISP_GRACE)),
	    ISP_NOINTR_POLL_DELAY_TIME);

	ISP_DEBUG2(isp, SCSI_DEBUG,
	"delay_loops=%d, delay=%d, pkt_time=%x, cdb[0]=%x\n", delay_loops,
	ISP_NOINTR_POLL_DELAY_TIME, pkt->pkt_time,
	*(sp->cmd_pkt->pkt_cdbp));

	while ((sp->cmd_flags & CFLAG_COMPLETED) == 0) {
		drv_usecwait(ISP_NOINTR_POLL_DELAY_TIME);


		if (--delay_loops <= 0) {
			register struct isp_response *resp;

			/*
			 * Call isp_scsi_abort()  to abort the I/O
			 * and if isp_scsi_abort fails then
			 * blow everything away
			 */
			if ((isp_scsi_reset(&pkt->pkt_address, RESET_TARGET))
				== FALSE) {
				mutex_enter(ISP_RESP_MUTEX(isp));
				isp_i_fatal_error(isp);
				mutex_exit(ISP_RESP_MUTEX(isp));
			}

			resp = &sp->cmd_isp_response;
			bzero((caddr_t)resp, sizeof (struct isp_response));

			/*
			 * update stats in resp_status_flags
			 * isp_i_call_pkt_compl() copies this
			 * over to pkt_statistics
			 */
			resp->resp_status_flags |=
			    STAT_BUS_RESET | STAT_TIMEOUT;
			resp->resp_reason = CMD_TIMEOUT;
#ifdef ISPDEBUG
			sp->cmd_flags |= CFLAG_FINISHED;
#endif
			isp_i_call_pkt_comp(sp);
			break;
		}

		/*
		 * This should really not be here, but interrupts apparently
		 * can be off during booting on some machines which is really
		 * a bug. If this got fixed we could avoid this recursive call
		 * to isp_intr ()
		 */
		if (ISP_INT_PENDING(isp)) {
			(void) isp_intr((caddr_t)isp);
		}
	}
	ISP_DEBUG2(isp, SCSI_DEBUG, "polled cmd done\n");

done:
	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_RUN_POLLED_CMD_END,
	    "isp_i_polled_cmd_start_end");

	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)) == 0 || ddi_in_panic());

	return (rval);
}


/*
 * Function name : isp_i_async_event
 *
 * Return Values : -1	Fatal error occurred
 *		    0	normal return
 * Description	 :
 * An Event of 8002 is a Sys Err in the ISP.  This would require
 *	Chip reset.
 *
 * An Event of 8001 is a external SCSI Bus Reset
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
static int
isp_i_async_event(register struct isp *isp, short event)
{
	volatile struct ispregs *ip = isp->isp_reg;
	int rval = ISP_AEN_SUCCESS;

	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)));
	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)));

	TRACE_1(TR_FAC_SCSI_ISP, TR_ISP_I_ASYNCH_EVENT_START,
	    "isp_i_async_event_start(event = %d)", event);


	switch (ISP_GET_MBOX_EVENT(event)) {
	case ISP_MBOX_ASYNC_ERR:
		/*
		 * Force the current commands to timeout after
		 * resetting the chip.
		 */
		isp_i_log(isp, CE_WARN, "Firmware error");
		mutex_exit(ISP_REQ_MUTEX(isp));
		isp_i_fatal_error(isp);
		mutex_enter(ISP_REQ_MUTEX(isp));
		rval = ISP_AEN_RESET;
		break;

	case ISP_MBOX_ASYNC_REQ_DMA_ERR:
	case ISP_MBOX_ASYNC_RESP_DMA_ERR:
		/*
		 *  DMA failed in the ISP chip force a Reset
		 */
		isp_i_log(isp, CE_WARN, "DMA Failure (%x)", event);
		ISP_CLEAR_RISC_INT(isp);
		ISP_CLEAR_SEMAPHORE_LOCK(isp);
		mutex_exit(ISP_REQ_MUTEX(isp));
		isp_i_fatal_error(isp);
		mutex_enter(ISP_REQ_MUTEX(isp));
		rval = ISP_AEN_RESET;
		break;

	case ISP_MBOX_ASYNC_RESET:
		isp_i_log(isp, CE_WARN, "Received unexpected SCSI Reset");

		/* FALLTHROUGH */
	case ISP_MBOX_ASYNC_INT_RESET:
		/*
		 * Set a marker to for a internal SCSI BUS reset done
		 * to recover from a timeout.
		 */
		ISP_DEBUG(isp, SCSI_DEBUG,
		    "ISP initiated SCSI BUS Reset or external SCSI Reset");
		ISP_MUTEX_EXIT(isp);
		if (isp_i_set_marker(isp, SYNCHRONIZE_ALL, 0, 0)) {
			isp_i_log(isp, CE_WARN, "cannot set marker");
		}
		ISP_MUTEX_ENTER(isp);
		ISP_CLEAR_RISC_INT(isp);
		ISP_CLEAR_SEMAPHORE_LOCK(isp);

		mutex_exit(ISP_REQ_MUTEX(isp));
		(void) scsi_hba_reset_notify_callback(ISP_RESP_MUTEX(isp),
			&isp->isp_reset_notify_listf);
		mutex_enter(ISP_REQ_MUTEX(isp));
		break;

	default:
		isp_i_log(isp, CE_NOTE,
		    "mailbox regs(0-5): 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x",
		    event, ip->isp_mailbox1, ip->isp_mailbox2,
		    ip->isp_mailbox3, ip->isp_mailbox4, ip->isp_mailbox5);
		ISP_CLEAR_RISC_INT(isp);
		ISP_CLEAR_SEMAPHORE_LOCK(isp);
		break;
	}


	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_ASYNCH_EVENT_END,
	    "isp_i_async_event_end");

	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)));
	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)));
	return (rval);
}

/*
 * Function name : isp_i_mbox_cmd_complete ()
 *
 * Return Values : None.
 *
 * Description	 : Sets ISP_MBOX_CMD_FLAGS_COMPLETE flag so busy wait loop
 *		   in isp_i_mbox_cmd_start() exits.
 *
 * Context	 : Can be called by interrupt thread only.
 */
static void
isp_i_mbox_cmd_complete(struct isp *isp)
{
	volatile struct ispregs *ip = isp->isp_reg;
	volatile u_short	*mbox_regp = (u_short *)&ip->isp_mailbox0;
	int			delay_loops;
	u_char			i;

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_MBOX_CMD_COMPLETE_START,
	    "isp_i_mbox_cmd_complete_start");

	ISP_DEBUG2(isp, SCSI_DEBUG,
	    "isp_i_mbox_cmd_complete_start(cmd = 0x%x)",
	    isp->isp_mbox.mbox_cmd.mbox_out[0]);

	/*
	 * Check for completions that are caused by mailbox events
	 * but that do not set the mailbox status bit ie. 0x4xxx
	 * For now only the busy condition is checked, the others
	 * will automatically time out and error.
	 */
	delay_loops = ISP_TIMEOUT_DELAY(ISP_MBOX_CMD_BUSY_WAIT_TIME,
	    ISP_MBOX_CMD_BUSY_POLL_DELAY_TIME);
	while (ip->isp_mailbox0 == ISP_MBOX_BUSY) {
		drv_usecwait(ISP_MBOX_CMD_BUSY_POLL_DELAY_TIME);
		if (--delay_loops < 0) {
			isp->isp_mbox.mbox_cmd.mbox_in[0] =
			    ISP_MBOX_STATUS_FIRMWARE_ERR;
			goto fail;
		}
	}


	/*
	 * clear the risc interrupt only no more interrupt are pending
	 * We do not need isp_response_mutex because of isp semaphore
	 * lock is held.
	 */
	if (((ISP_GET_RESPONSE_IN(isp) - isp->isp_response_out) == 0) ||
		(isp->isp_mbox.mbox_flags & ISP_MBOX_CMD_FLAGS_Q_NOT_INIT)) {
		ISP_CLEAR_RISC_INT(isp);
	}

	/*
	 * save away status registers
	 */
	for (i = 0; i < ISP_MAX_MBOX_REGS; i++, mbox_regp++) {
		isp->isp_mbox.mbox_cmd.mbox_in[i] = *mbox_regp;
	}

fail:
	/*
	 * set flag so that busy wait loop will detect this and return
	 */
	isp->isp_mbox.mbox_flags |= ISP_MBOX_CMD_FLAGS_COMPLETE;

	/*
	 * clear the semaphore lock
	 */
	ISP_CLEAR_SEMAPHORE_LOCK(isp);

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_MBOX_CMD_COMPLETE_END,
	    "isp_i_mbox_cmd_complete_end");

	ISP_DEBUG2(isp, SCSI_DEBUG,
	    "isp_i_mbox_cmd_complete_end (cmd = 0x%x)",
	    isp->isp_mbox.mbox_cmd.mbox_out[0]);
}


/*
 * Function name : isp_i_mbox_cmd_init()
 *
 * Return Values : none
 *
 * Description	 : initializes mailbox command
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
static void
isp_i_mbox_cmd_init(struct isp *isp, struct isp_mbox_cmd *mbox_cmdp,
    u_char n_mbox_out, u_char n_mbox_in,
    u_short reg0, u_short reg1, u_short reg2,
    u_short reg3, u_short reg4, u_short reg5)
{

	ISP_DEBUG2(isp, SCSI_DEBUG,
	    "isp_i_mbox_cmd_init r0 = 0x%x r1 = 0x%x r2 = 0x%x",
	    reg0, reg1, reg2);
	ISP_DEBUG2(isp, SCSI_DEBUG,
	    "			 r3 = 0x%x r4 = 0x%x r5 = 0x%x",
	    reg3, reg4, reg5);

	mbox_cmdp->timeout	= ISP_MBOX_CMD_TIMEOUT;
	mbox_cmdp->retry_cnt	= ISP_MBOX_CMD_RETRY_CNT;
	mbox_cmdp->n_mbox_out	= n_mbox_out;
	mbox_cmdp->n_mbox_in	= n_mbox_in;
	mbox_cmdp->mbox_out[0]	= n_mbox_out >= 1 ? reg0 : 0;
	mbox_cmdp->mbox_out[1]	= n_mbox_out >= 2 ? reg1 : 0;
	mbox_cmdp->mbox_out[2]	= n_mbox_out >= 3 ? reg2 : 0;
	mbox_cmdp->mbox_out[3]	= n_mbox_out >= 4 ? reg3 : 0;
	mbox_cmdp->mbox_out[4]	= n_mbox_out >= 5 ? reg4 : 0;
	mbox_cmdp->mbox_out[5]	= n_mbox_out >= 6 ? reg5 : 0;
}


/*
 * Function name : isp_i_mbox_cmd_start()
 *
 * Return Values : 0   if no error
 *		   -1  on error
 *		   Status registers are returned in structure that is passed in.
 *
 * Description	 : Sends mailbox cmd to ISP and busy waits for completion.
 *		   Serializes accesses to the mailboxes.
 *		   Mailbox cmds are used to initialize the ISP, modify default
 *			parameters, and load new firmware.
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
static int isp_debug_mbox;

static int
isp_i_mbox_cmd_start(struct isp *isp, struct isp_mbox_cmd *mbox_cmdp)
{
	volatile struct		ispregs *ip = isp->isp_reg;
	volatile u_short	*mbox_regp = (u_short *)&ip->isp_mailbox0;
	char			retry_cnt = (char)mbox_cmdp->retry_cnt;
	int			delay_loops, rval = 0;
	u_char			i;

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_MBOX_CMD_START_START,
	    "isp_i_mbox_cmd_start_start");

	ISP_DEBUG2(isp, SCSI_DEBUG, "isp_i_mbox_cmd_start_start(cmd = 0x%x)",
	    mbox_cmdp->mbox_out[0]);

	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)));
	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)));

	/*
	 * allow only one thread to access mailboxes
	 * (release mutexes before the sema_p to avoid a deadlock when
	 * another thread needs to do a mailbox cmd and this thread needs
	 * to reenter the mutex again (see below) while holding the semaphore
	 */
	ISP_MUTEX_EXIT(isp);
	sema_p(ISP_MBOX_SEMA(isp));
	ISP_MUTEX_ENTER(isp);

	/*
	 * while waiting for semaphore shutdown flag may get set.
	 * If shutdown flag is set return -1
	 */
	if (isp->isp_shutdown) {
		rval = -1;
		sema_v(ISP_MBOX_SEMA(isp));
		return (rval);
	}

	/* save away mailbox command */
	bcopy((caddr_t)mbox_cmdp, (caddr_t)&isp->isp_mbox.mbox_cmd,
	    sizeof (struct isp_mbox_cmd));

retry:

	/* write outgoing registers */
	for (i = 0; i < isp->isp_mbox.mbox_cmd.n_mbox_out; i++, mbox_regp++)
		*mbox_regp = isp->isp_mbox.mbox_cmd.mbox_out[i];

#ifdef ISP_PERF
	isp->isp_wpio_count += isp->isp_mbox.mbox_cmd.n_mbox_out;
	isp->isp_mail_requests++;
#endif /* ISP_PERF */

	/*
	 * Turn completed flag off indicating mbox command was issued.
	 * Interrupt handler will set flag when done.
	 */
	isp->isp_mbox.mbox_flags &= ~ISP_MBOX_CMD_FLAGS_COMPLETE;

	/* signal isp that mailbox cmd was loaded */
	ISP_REG_SET_HOST_INT(isp);

	/* busy wait for mailbox cmd to be processed. */
	delay_loops = ISP_TIMEOUT_DELAY(mbox_cmdp->timeout,
	    ISP_MBOX_CMD_BUSY_POLL_DELAY_TIME);

	/*
	 * release mutexes, we are now protected by the sema and we don't
	 * want to deadlock with isp_intr()
	 */
	ISP_MUTEX_EXIT(isp);

	while ((isp->isp_mbox.mbox_flags & ISP_MBOX_CMD_FLAGS_COMPLETE) == 0) {

		drv_usecwait(ISP_MBOX_CMD_BUSY_POLL_DELAY_TIME);
		/* if cmd does not complete retry or return error */
		if (--delay_loops <= 0) {
			if (--retry_cnt <= 0) {
				rval = -1;
				goto done;
			} else {
				ISP_MUTEX_ENTER(isp);
				goto retry;
			}
		}

		if (ISP_CHECK_SEMAPHORE_LOCK(isp)) {
			register short event = isp->isp_reg->isp_mailbox0;
#ifdef ISP_PERF
			isp->isp_rpio_count += 1;
#endif
			if (event & ISP_MBOX_EVENT_ASYNCH) {
				/*
				 * if an async event occurs during
				 * fatal error recovery, we are hosed
				 * with a recursive mutex panic
				 */
				ISP_MUTEX_ENTER(isp);
				switch (ISP_GET_MBOX_EVENT(event)) {
				case ISP_MBOX_ASYNC_ERR:
				case ISP_MBOX_ASYNC_REQ_DMA_ERR:
				case ISP_MBOX_ASYNC_RESP_DMA_ERR:
					sema_v(ISP_MBOX_SEMA(isp));
					break;
				}
				if (isp_i_async_event(isp, event) ==
				    ISP_AEN_RESET) {
					rval = -1;
					return (rval);
				}
				ISP_MUTEX_EXIT(isp);
			} else {
				isp_i_mbox_cmd_complete(isp);
			}
		}
	}

	/*
	 * copy registers saved by isp_i_mbox_cmd_complete()
	 * to mbox_cmdp
	 */
	for (i = 0; i < ISP_MAX_MBOX_REGS; i++) {
	    mbox_cmdp->mbox_in[i] = isp->isp_mbox.mbox_cmd.mbox_in[i];
	}

#ifdef ISP_PERF
	isp->isp_rpio_count += isp->isp_mbox.mbox_cmd.n_mbox_in;
#endif

done:
	if (rval || isp_debug_mbox) {
		ISP_DEBUG(isp, SCSI_DEBUG, "mbox cmd %s:",
		    (rval ? "failed" : "succeeded"));
		ISP_DEBUG(isp, SCSI_DEBUG,
		    "cmd= 0x%x; 0x%x, 0x%x, 0x%x, 0x%x, 0x%x",
		    mbox_cmdp->mbox_out[0], mbox_cmdp->mbox_out[1],
		    mbox_cmdp->mbox_out[2], mbox_cmdp->mbox_out[3],
		    mbox_cmdp->mbox_out[4], mbox_cmdp->mbox_out[5]);
		ISP_DEBUG(isp, SCSI_DEBUG,
		    "status= 0x%x; 0x%x, 0x%x, 0x%x, 0x%x, 0x%x",
		    mbox_cmdp->mbox_in[0], mbox_cmdp->mbox_in[1],
		    mbox_cmdp->mbox_in[2], mbox_cmdp->mbox_in[3],
		    mbox_cmdp->mbox_in[4], mbox_cmdp->mbox_in[5]);
	} else {
		ISP_DEBUG2(isp, SCSI_DEBUG, "mbox cmd succeeded:");
		ISP_DEBUG2(isp, SCSI_DEBUG,
		    "cmd= 0x%x; 0x%x, 0x%x, 0x%x, 0x%x, 0x%x",
		    mbox_cmdp->mbox_out[0], mbox_cmdp->mbox_out[1],
		    mbox_cmdp->mbox_out[2], mbox_cmdp->mbox_out[3],
		    mbox_cmdp->mbox_out[4], mbox_cmdp->mbox_out[5]);
		ISP_DEBUG2(isp, SCSI_DEBUG,
		    "status= 0x%x; 0x%x, 0x%x, 0x%x, 0x%x, 0x%x",
		    mbox_cmdp->mbox_in[0], mbox_cmdp->mbox_in[1],
		    mbox_cmdp->mbox_in[2], mbox_cmdp->mbox_in[3],
		    mbox_cmdp->mbox_in[4], mbox_cmdp->mbox_in[5]);
	}

	ISP_MUTEX_ENTER(isp);

	sema_v(ISP_MBOX_SEMA(isp));

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_MBOX_CMD_START_END,
	    "isp_i_mbox_cmd_start_end");

	ISP_DEBUG2(isp, SCSI_DEBUG, "isp_i_mbox_cmd_start_end (cmd = 0x%x)",
	    mbox_cmdp->mbox_out[0]);

	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)));
	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)));

	return (rval);
}


/*
 * Function name : isp_i_watch()
 *
 * Return Values : none
 * Description	 :
 * Isp deadman timer routine.
 * A hung isp controller is detected by failure to complete
 * cmds within a timeout interval (including grace period for
 * isp error recovery).	 All target error recovery is handled
 * directly by the isp.
 *
 * If isp hung, restart by resetting the isp and flushing the
 * crash protection queue (isp_slots) via isp_i_qflush.
 *
 * we check only 1/8 of the slots at the time; this is really only a sanity
 * check on isp so we know when it dropped a packet. The isp performs
 * timeout checking and recovery on the target
 * If the isp dropped a packet then this is a fatal error
 *
 * if lbolt wraps around then those packets will never timeout; there
 * is small risk of a hang in this short time frame. It is cheaper though
 * to ignore this problem since this is an extremely unlikely event
 *
 * Context	 : Can be called by timeout thread.
 */
#ifdef ISPDEBUG
static int isp_test_abort;
static int isp_test_abort_all;
static int isp_test_reset;
static int isp_test_reset_all;
static int isp_test_fatal;
static int isp_debug_enter;
static int isp_debug_enter_count;
/* VARIABLES PROTECTED BY isp::isp_response_mutex: isp_test_abort	*/
/* VARIABLES PROTECTED BY isp::isp_response_mutex: isp_test_abort_all	*/
/* VARIABLES PROTECTED BY isp::isp_response_mutex: isp_test_reset	*/
/* VARIABLES PROTECTED BY isp::isp_response_mutex: isp_test_reset_all	*/
/* VARIABLES PROTECTED BY isp::isp_response_mutex: isp_test_fatal	*/
/* VARIABLES PROTECTED BY isp::isp_response_mutex: isp_debug_enter	*/
/* VARIABLES PROTECTED BY isp::isp_response_mutex: isp_debug_enter_count */
#endif


static void
isp_i_watch()
{
	register struct isp *isp = isp_head;
#ifdef OLDTIMEOUT
	register u_long local_lbolt;

	local_lbolt = lbolt;
#endif

	for (isp = isp_head; isp != NULL; isp = isp->isp_next) {
#ifdef OLDTIMEOUT
		isp_i_old_watch_isp(isp, local_lbolt);
#else
		if (isp->isp_shutdown) {
			continue;
		}
		isp_i_watch_isp(isp);
#endif
	}

	mutex_enter(&isp_global_mutex);
	isp_timeout_id = timeout(isp_i_watch, (caddr_t)isp, isp_tick);
	mutex_exit(&isp_global_mutex);
}

#ifdef OLDTIMEOUT
static void
isp_i_old_watch_isp(register struct isp *isp, u_long local_lbolt)
{
	register u_short slot;
	register u_short last_slot, max_slot;
	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)) == 0 || ddi_in_panic());

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_WATCH_START, "isp_i_watch_start");


#ifdef ISP_PERF
	isp->isp_perf_ticks += isp_scsi_watchdog_tick;

	if (isp->isp_request_count >= 20000) {
		isp_i_log(isp, CE_NOTE,
	"%d reqs/sec (ticks=%d, intr=%d, req=%d, rpio=%d, wpio=%d)",
		    isp->isp_request_count/isp->isp_perf_ticks,
		    isp->isp_perf_ticks,
		    isp->isp_intr_count, isp->isp_request_count,
		    isp->isp_rpio_count, isp->isp_wpio_count);

		isp->isp_request_count = isp->isp_perf_ticks = 0;
		isp->isp_intr_count = 0;
		isp->isp_rpio_count = isp->isp_wpio_count = 0;
	}
#endif /* ISP_PERF */

	mutex_enter(ISP_RESP_MUTEX(isp));

	last_slot = isp->isp_last_slot_watched;
	max_slot = ISP_MAX_SLOTS;

	ISP_DEBUG2(isp, SCSI_DEBUG,
	    "isp_i_watch: lbolt=%d, start_slot=0x%x, max_slot=0x%x\n",
	    local_lbolt, last_slot, max_slot);

	for (slot = last_slot; slot < max_slot; slot++) {
		struct isp_cmd *sp = isp->isp_slots[slot].slot_cmd;
		struct scsi_pkt *pkt = (sp != NULL)? CMD2PKT(sp) : NULL;
#ifdef ISPDEBUG

		/*
		 * This routine will return with holding ISP_RESP_MUTEX
		 */
		if (sp) {
			isp_i_test(isp, sp);
		}
#endif
		ASSERT(slot < (u_short)ISP_MAX_SLOTS);

		if (isp->isp_slots[slot].slot_cmd &&
		    (u_long) isp->isp_slots[slot].slot_deadline <=
		    local_lbolt) {
			struct isp_cmd *sp =
			    isp->isp_slots[slot].slot_cmd;

			if (pkt->pkt_time) {
#ifdef ISPDEBUG
				register u_char *cp;
				char buf[128];
				register int i;

				ISP_DEBUG(isp, SCSI_DEBUG,
				    "deadline=%x, local_lbolt=%x pkt_time=%x",
				    isp->isp_slots[slot].slot_deadline,
				    local_lbolt, pkt->pkt_time);
				ISP_DEBUG(isp, SCSI_DEBUG,
				"tgt %d.%d: sp=%x, pkt_flags=%x",
				TGT(sp), LUN(sp), sp, pkt->pkt_flags);

				cp = (u_char *) pkt->pkt_cdbp;
				buf[0] = '\0';
				for (i = 0; i < (int)sp->cmd_cdblen; i++) {
					(void) sprintf(
					    &buf[strlen(buf)], " 0x%x", *cp++);
					if (strlen(buf) > 124) {
						break;
					}
				}
				ISP_DEBUG(isp, SCSI_DEBUG,
				    "\tcflags=%x, cdb dump: %s",
				    sp->cmd_flags, buf);
#endif
				if (isp_i_is_fatal_timeout(isp, sp)) {
					isp_i_log(isp, CE_WARN,
					    "Fatal timeout on target %d.%d",
					    TGT(sp), LUN(sp));
					isp_i_fatal_error(isp);
					break;
				}
			}
		}
	}
#ifdef ISPDEBUG
	isp_test_abort = 0;
	if (isp_debug_enter && isp_debug_enter_count) {
		debug_enter("isp_i_watch");
		isp_debug_enter = 0;
	}
#endif
	last_slot = max_slot;
	if (last_slot >= (u_short)ISP_MAX_SLOTS) {
		last_slot = 0;
	}
	isp->isp_last_slot_watched = last_slot;
	mutex_exit(ISP_RESP_MUTEX(isp));

	/*
	 * Paranoia: just in case we haven`t emptied the waitQ, empty it
	 * here
	 */
	if (mutex_tryenter(ISP_REQ_MUTEX(isp))) {
		ISP_CHECK_WAITQ_AND_EXIT_REQ_MUTEX(isp);
	}

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_WATCH_END, "isp_i_watch_end");

	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)) == 0 || ddi_in_panic());
}


/*
 * Function name : isp_i_is_fatal_timeout()
 *
 * Return Values : 1 - is really a fatal timeout
 *		   0 - not a fatal timeout yet
 *
 * Description	 : verifies whether the sp has the earliest starting time
 *		   this will give the isp some more time but it is
 *		   still impossible to figure out when exactly a cmd
 *		   is started on the bus
 *
 * Context	 : called from isp_i_watch_isp
 */
static int
isp_i_is_fatal_timeout(struct isp *isp, struct isp_cmd *sp)
{
	register u_short slot;
	register u_short max_slot = ISP_MAX_SLOTS;
	u_long start_time = sp->cmd_start_time;

	ISP_DEBUG(isp, SCSI_DEBUG,
	    "fatal timeout check for %x, start_time=%x, lbolt=%x",
	    sp, start_time, lbolt);

	/*
	 * if the start_time is greater than current time, we have
	 * a wrap-around and might as well give up and declare
	 * timeout rather than dealing with the complications of
	 * lbolt wrap-around
	 */
	if (start_time >= lbolt) {
		return (1);
	}

	/*
	 * now go thru all slots and find an earlier starting time
	 * for the same scsi address
	 * if so, then that cmd should timeout first
	 */
	for (slot = 0; slot < max_slot; slot++) {
		struct isp_cmd *ssp = isp->isp_slots[slot].slot_cmd;

		if (ssp) {
			ISP_DEBUG(isp, SCSI_DEBUG,
			"checking %x, start_time=%x, tgt=%x",
			ssp, ssp->cmd_start_time, TGT(ssp));
		}

		if (ssp && (ssp->cmd_start_time < start_time) &&
		    (bcmp((char *)&(CMD2PKT(ssp)->pkt_address),
			(char *)&CMD2PKT(sp)->pkt_address,
			sizeof (struct scsi_address)) == 0)) {
			start_time = ssp->cmd_start_time;
			ISP_DEBUG(isp, SCSI_DEBUG,
			"found older cmd=%x, start_time=%x, timeout=%x",
			    ssp, start_time, CMD2PKT(ssp)->pkt_time);
			return (0);
		}
	}

	/*
	 * we didn't find an older cmd, so we have a real timeout
	 */
	return (1);
}
#else
static void
isp_i_watch_isp(register struct isp *isp)
{
	int slot;
	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)) == 0 || ddi_in_panic());

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_WATCH_START, "isp_i_watch_start");


#ifdef ISP_PERF
	isp->isp_perf_ticks += isp_scsi_watchdog_tick;

	if (isp->isp_request_count >= 20000) {
		isp_i_log(isp, CE_NOTE,
	"%d reqs/sec (ticks=%d, intr=%d, req=%d, rpio=%d, wpio=%d)",
		    isp->isp_request_count/isp->isp_perf_ticks,
		    isp->isp_perf_ticks,
		    isp->isp_intr_count, isp->isp_request_count,
		    isp->isp_rpio_count, isp->isp_wpio_count);

		isp->isp_request_count = isp->isp_perf_ticks = 0;
		isp->isp_intr_count = 0;
		isp->isp_rpio_count = isp->isp_wpio_count = 0;
	}
#endif /* ISP_PERF */
	if (!(isp->isp_alive) || !(ISP_INT_PENDING(isp))) {

		if (!isp_i_alive(isp)) {
			mutex_enter(ISP_RESP_MUTEX(isp));
			isp_i_fatal_error(isp);
			mutex_exit(ISP_RESP_MUTEX(isp));
		}
	}

	isp->isp_alive = 0;
#ifdef ISPDEBUG
	mutex_enter(ISP_RESP_MUTEX(isp));
	for (slot = 0; slot < ISP_MAX_SLOTS; slot++) {
		struct isp_cmd *sp = isp->isp_slots[slot].slot_cmd;

		if (sp) {
			isp_i_test(isp, sp);
			break;
		}
	}
	isp_test_abort = 0;
	if (isp_debug_enter && isp_debug_enter_count) {
		debug_enter("isp_i_watch");
		isp_debug_enter = 0;
	}
	mutex_exit(ISP_RESP_MUTEX(isp));
#endif

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_WATCH_END, "isp_i_watch_end");

	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)) == 0 || ddi_in_panic());
}

#endif


/*
 * Function name : isp_i_fatal_error()
 *
 * Return Values :  none
 *
 * Description	 :
 * Isp fatal error recovery:
 * Reset the isp and flush the active queues and attempt restart.
 * This should only happen in case of a firmware bug or hardware
 * death.  Flushing is from backup queue as ISP cannot be trusted.
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
static void
isp_i_fatal_error(struct isp *isp)
{
	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)));
	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)) == 0 || ddi_in_panic());

	/*
	 * If shutdown flag is set than no need to do
	 * fatal error recovery.
	 */
	if (isp->isp_shutdown) {
		return;
	}

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_TIMEOUT_START,
	    "isp_i_fatal_error_start");

	isp_i_log(isp, CE_WARN, "Fatal error, resetting interface");

	/*
	 * hold off starting new requests by grabbing the request
	 * mutex
	 */
	mutex_enter(ISP_REQ_MUTEX(isp));

	isp_i_print_state(isp);

#ifdef ISPDEBUG
	if (isp_enable_brk_fatal) {
		char buf[128];
		char path[128];
		(void) sprintf(buf,
		"isp_i_fatal_error: You can now look at %s",
		    ddi_pathname(isp->isp_dip, path));
		debug_enter(buf);
	}
#endif

	(void) isp_i_reset_interface(isp, ISP_FORCE_RESET_BUS);

	isp_i_qflush(isp, (u_short)0, (u_short) NTARGETS_WIDE);

	mutex_exit(ISP_REQ_MUTEX(isp));
	(void) scsi_hba_reset_notify_callback(ISP_RESP_MUTEX(isp),
	    &isp->isp_reset_notify_listf);
	mutex_enter(ISP_REQ_MUTEX(isp));

	ISP_CHECK_WAITQ_AND_EXIT_REQ_MUTEX(isp);

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_TIMEOUT_END, "isp_i_fatal_error_end");

	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)));
	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)) == 0 || ddi_in_panic());
}


/*
 * Function name : isp_i_qflush()
 *
 * Return Values : none
 * Description	 :
 *	Flush isp queues  over range specified
 *	from start_tgt to end_tgt.  Flushing goes from oldest to newest
 *	to preserve some cmd ordering.
 *	This is used for isp crash recovery as normally isp takes
 *	care of target or bus problems.
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
static void
isp_i_qflush(register struct isp *isp, u_short start_tgt, u_short end_tgt)
{
	register struct isp_cmd *sp;
	register struct isp_cmd *head, *tail;
	register short slot;
	register int i, n = 0;
	register struct isp_response *resp;

	ASSERT(start_tgt <= end_tgt);
	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)));
	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)));

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_QFLUSH_START,
	    "isp_i_qflush_start");

	ISP_DEBUG(isp, SCSI_DEBUG,
	    "isp_i_qflush: range= %d-%d", start_tgt, end_tgt);

	/*
	 * Flush specified range of active queue entries
	 * (e.g. target nexus).
	 * we allow for just flushing one target, ie start_tgt == end_tgt
	 */
	head = tail = NULL;

	/*
	 * If flushing active queue, start with current free slot
	 * ie. oldest request, to preserve some order.
	 */
	slot = isp->isp_free_slot;

	for (i = 0; i < ISP_MAX_SLOTS; i++) {

		sp = isp->isp_slots[slot].slot_cmd;
		if (sp &&
		    (TGT(sp) >= start_tgt) &&
		    (TGT(sp) <= end_tgt)) {
			isp->isp_slots[slot].slot_cmd = NULL;
			resp = &sp->cmd_isp_response;
			bzero((caddr_t)resp,
			    sizeof (struct isp_response));

			/*
			 * update stats in resp_status_flags
			 * isp_i_call_pkt_compl() copies this
			 * over to pkt_statistics
			 */
			resp->resp_status_flags = STAT_BUS_RESET | STAT_ABORTED;
			resp->resp_reason = CMD_RESET;
#ifdef ISPDEBUG
			sp->cmd_flags |= CFLAG_FINISHED;
#endif
			/*
			 * queue up sp
			 * we don't want to do a callback yet
			 * until we have flushed everything and
			 * can release the mutexes
			 */
			n++;
			if (head) {
				tail->cmd_forw = sp;
				tail = sp;
				tail->cmd_forw = NULL;
			} else {
				tail = head = sp;
				sp->cmd_forw = NULL;
			}
		}

		/*
		 * Wraparound
		 */
		if (++slot >= ISP_MAX_SLOTS) {
			slot = 0;
		}
	}

	/*
	 * XXX we don't worry about the waitQ since we cannot
	 * guarantee order anyway.
	 */
	if (head) {
		/*
		 * if we would	hold the REQ mutex and	the target driver
		 * decides to do a scsi_reset(), we will get a recursive
		 * mutex failure in isp_i_set_marker
		 */
		ISP_DEBUG(isp, SCSI_DEBUG, "isp_i_qflush: %d flushed", n);
		ISP_MUTEX_EXIT(isp);
		isp_i_call_pkt_comp(head);
		ISP_MUTEX_ENTER(isp);
	}

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_QFLUSH_END,
	    "isp_i_qflush_end");

	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)));
	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)));
}


/*
 * Function name : isp_i_set_marker()
 *
 * Return Values : none
 * Description	 :
 * Send marker request to unlock the request queue for a given target/lun
 * nexus.
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
/*ARGSUSED*/
static int
isp_i_set_marker(register struct isp *isp, short mod, short tgt, short lun)
{
	register struct isp_request *req;

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_SET_MARKER_START,
	    "isp_i_set_marker_start");

	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)) == 0 || ddi_in_panic());

	mutex_enter(ISP_REQ_MUTEX(isp));

	/*
	 * Check to see how much space is available in the
	 * Request Queue, save this so we do not have to do
	 * a lot of PIOs
	 */
	if (isp->isp_queue_space == 0) {
		ISP_UPDATE_QUEUE_SPACE(isp);

		/*
		 * Check now to see if the queue is still full
		 */
		if (isp->isp_queue_space == 0) {
			ISP_CHECK_WAITQ_AND_EXIT_REQ_MUTEX(isp);
			return (-1);
		}
	}

	ISP_GET_NEXT_REQUEST_IN(isp, req);
	req->req_header.cq_entry_type = CQ_TYPE_MARKER;
	req->req_target = (u_char) tgt;
	req->req_lun_trn = (u_char) lun;
	req->req_modifier = mod;

	/*
	 * Tell isp it's got a new I/O request...
	 */
	ISP_SET_REQUEST_IN(isp);
	ISP_CHECK_WAITQ_AND_EXIT_REQ_MUTEX(isp);
	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_SET_MARKER_END,
	    "isp_i_set_marker_end");

	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)) == 0 || ddi_in_panic());
	return (0);
}


/*
 * Function name : isp_scsi_abort()
 *
 * Return Values : FALSE	- abort failed
 *		   TRUE		- abort succeeded
 * Description	 :
 * SCSA interface routine to abort pkt(s) in progress.
 * Abort the pkt specified.  If NULL pkt, abort ALL pkts.
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
static int
isp_scsi_abort(struct scsi_address *ap, struct scsi_pkt *pkt)
{
	struct isp *isp = ADDR2ISP(ap);
	struct isp_mbox_cmd mbox_cmd;
	u_short	 arg, rval = FALSE;

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_ABORT_START,
	    "isp_scsi_abort_start");

	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)) == 0 || ddi_in_panic());

	/*
	 * hold off new requests, we need the req mutex anyway for mbox cmds.
	 * the waitQ must be empty since the request mutex was free
	 */
	ISP_MUTEX_ENTER(isp);

	/*
	 * If no space in request queue, return error
	 */
	if (isp->isp_queue_space == 0) {
		ISP_UPDATE_QUEUE_SPACE(isp);

		/*
		 * Check now to see if the queue is still full
		 */
		if (isp->isp_queue_space == 0) {
			ISP_DEBUG(isp, SCSI_DEBUG,
			    "isp_scsi_abort: No space in Queue for Marker");
			goto fail;
		}
	}

	if (pkt) {
		struct isp_cmd *sp = PKT2CMD(pkt);

		ISP_DEBUG(isp, SCSI_DEBUG, "aborting pkt 0x%x", (int)pkt);

		arg = ((u_short)ap->a_target << 8) | ((u_short)ap->a_lun);
		isp_i_mbox_cmd_init(isp, &mbox_cmd, 4, 4,
		    ISP_MBOX_CMD_ABORT_IOCB, arg, MSW(sp), LSW(sp), 0, 0);
		if (isp_i_mbox_cmd_start(isp, &mbox_cmd)) {
			goto fail;
		}

		ISP_MUTEX_EXIT(isp);
		if (isp_i_set_marker(isp, SYNCHRONIZE_NEXUS,
		    (short)ap->a_target, (short)ap->a_lun)) {
			/*
			 * XXX can we do better than fatal error?
			 */
			mutex_enter(ISP_RESP_MUTEX(isp));
			isp_i_fatal_error(isp);
			mutex_enter(ISP_REQ_MUTEX(isp));
			goto fail;
		}
	} else {
		ISP_DEBUG(isp, SCSI_DEBUG, "aborting all pkts");

		arg = ((u_short)ap->a_target << 8) | ((u_short)ap->a_lun);
		isp_i_mbox_cmd_init(isp, &mbox_cmd, 2, 2,
		    ISP_MBOX_CMD_ABORT_DEVICE, arg, 0, 0, 0, 0);
		if (isp_i_mbox_cmd_start(isp, &mbox_cmd)) {
			goto fail;
		}

		ISP_MUTEX_EXIT(isp);
		if (isp_i_set_marker(isp, SYNCHRONIZE_NEXUS,
		    (short)ap->a_target, (short)ap->a_lun)) {
			/*
			 * XXX can we do better than fatal error?
			 */
			mutex_enter(ISP_RESP_MUTEX(isp));
			isp_i_fatal_error(isp);
			mutex_enter(ISP_REQ_MUTEX(isp));
			goto fail;
		}
	}

	rval = TRUE;

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_ABORT_END,
	    "isp_scsi_abort_end");

	mutex_enter(ISP_REQ_MUTEX(isp));
	ISP_CHECK_WAITQ_AND_EXIT_REQ_MUTEX(isp);

	return (rval);

fail:
	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_ABORT_END,
	    "isp_scsi_abort_end");

	mutex_exit(ISP_RESP_MUTEX(isp));
	ISP_CHECK_WAITQ_AND_EXIT_REQ_MUTEX(isp);

	return (rval);
}


/*
 * Function name : isp_scsi_reset()
 *
 * Return Values : FALSE - reset failed
 *		   TRUE	 - reset succeeded
 * Description	 :
 * SCSA interface routine to perform scsi resets on either
 * a specified target or the bus (default).
 * XXX check waitQ as well
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
static int
isp_scsi_reset(struct scsi_address *ap, int level)
{
	struct isp *isp = ADDR2ISP(ap);
	struct isp_mbox_cmd mbox_cmd;
	u_short arg;
	int	rval = FALSE;

	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_RESET_START,
	    "isp_scsi_reset_start");

	ASSERT(mutex_owned(ISP_REQ_MUTEX(isp)) == 0 || ddi_in_panic());
	ASSERT(mutex_owned(ISP_RESP_MUTEX(isp)) == 0 || ddi_in_panic());

	/*
	 * hold off new requests, we need the req mutex anyway for mbox cmds.
	 * the waitQ must be empty since the request mutex was free
	 */
	ISP_MUTEX_ENTER(isp);

	/*
	 * If no space in request queue, return error
	 */
	if (isp->isp_queue_space == 0) {
		ISP_UPDATE_QUEUE_SPACE(isp);

		/*
		 * Check now to see if the queue is still full
		 */
		if (isp->isp_queue_space == 0) {
			ISP_DEBUG(isp, SCSI_DEBUG,
			    "isp_scsi_abort: No space in Queue for Marker");
			goto fail;
		}
	}

	if (level == RESET_TARGET) {
		ISP_DEBUG(isp, SCSI_DEBUG, "isp_scsi_reset: reset target %d",
		    ap->a_target);

		arg = ((u_short) ap->a_target << 8) | ((u_short) ap->a_lun);

		isp_i_mbox_cmd_init(isp, &mbox_cmd, 2, 2,
		    ISP_MBOX_CMD_ABORT_TARGET, arg, 0, 0, 0, 0);
		if (isp_i_mbox_cmd_start(isp, &mbox_cmd)) {
			goto fail;
		}

		ISP_MUTEX_EXIT(isp);
		if (isp_i_set_marker(isp, SYNCHRONIZE_TARGET,
		    (short)ap->a_target, (short)ap->a_lun)) {
			/*
			 * XXX can we do better than fatal error?
			 */
			mutex_enter(ISP_RESP_MUTEX(isp));
			isp_i_fatal_error(isp);
			mutex_enter(ISP_REQ_MUTEX(isp));
			goto fail;
		}
		ISP_MUTEX_ENTER(isp);
	} else {
		ISP_DEBUG(isp, SCSI_DEBUG, "isp_scsi_reset: reset bus");

		/*
		 * If firmware is 1.13 and earlier, scsi_reset_delay is
		 * given in milli seconds, but the isp chip needs this in
		 * seconds.  qlogic fixed this in firmware 1.14 so that
		 * scsi_reset_delay is addressed in seconds.
		 */
		if (isp->isp_major_rev == 1 && isp->isp_minor_rev < 14) {
			isp_i_mbox_cmd_init(isp, &mbox_cmd, 2, 2,
			    ISP_MBOX_CMD_BUS_RESET,
			    (u_short)(((int)isp->isp_scsi_reset_delay+999)
			    /1000), 0, 0, 0, 0);
		} else {
			isp_i_mbox_cmd_init(isp, &mbox_cmd, 2, 2,
			    ISP_MBOX_CMD_BUS_RESET,
			    (u_short)((int)isp->isp_scsi_reset_delay),
			    0, 0, 0, 0);
		}

		if (isp_i_mbox_cmd_start(isp, &mbox_cmd)) {
			goto fail;
		}

		mutex_exit(ISP_REQ_MUTEX(isp));
		(void) scsi_hba_reset_notify_callback(ISP_RESP_MUTEX(isp),
			&isp->isp_reset_notify_listf);
		mutex_enter(ISP_REQ_MUTEX(isp));

		ISP_MUTEX_EXIT(isp);
		if (isp_i_set_marker(isp, SYNCHRONIZE_ALL, 0, 0)) {
			/*
			 * XXX can we do better than fatal error?
			 */
			mutex_enter(ISP_RESP_MUTEX(isp));
			isp_i_fatal_error(isp);
			mutex_enter(ISP_REQ_MUTEX(isp));
			goto fail;
		}
		ISP_MUTEX_ENTER(isp);
	}

	rval = TRUE;

fail:
	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_SCSI_RESET_END,
	    "isp_scsi_reset_end");

	mutex_exit(ISP_RESP_MUTEX(isp));
	ISP_CHECK_WAITQ_AND_EXIT_REQ_MUTEX(isp);

	return (rval);
}


/*
 * Function name : isp_i_reset_interface()
 *
 * Return Values : 0 - success
 *		  -1 - hw failure
 *
 * Description	 :
 * Master reset routine for hardware/software.	Resets softc struct,
 * isp chip, and scsi bus.  The works!
 * This function is called from isp_attach with no mutexes held or
 * from isp_i_fatal_error with response and request mutex held
 *
 * NOTE: it is up to the caller to flush the response queue and isp_slots
 *	 list
 *
 * Context	 : Can be called from different kernel process threads.
 *		   Can be called by interrupt thread.
 */
static int
isp_i_reset_interface(register struct isp *isp, int action)
{
	volatile struct ispregs *ip;
	register int i;
	struct isp_mbox_cmd mbox_cmd;
	int rval = -1;

	TRACE_1(TR_FAC_SCSI_ISP, TR_ISP_I_RESET_INTERFACE_START,
	    "isp_i_reset_interface_start (action = %x)", action);

	ip = isp->isp_reg;

	/*
	 * Handle reset recovery; reset the bus before we reset the
	 * isp chip
	 */
	/*
	 * To speed up kernel boot, check if bus free.
	 * If it's free, skip scsi bus reset.
	 */
	ip->isp_hccr = ISP_HCCR_CMD_PAUSE;
	if ((action & ISP_FORCE_RESET_BUS) ||
	    (ip->isp_sxp_pins_control & ISP_SXP_BUS_BUSY)) {
		ISP_DEBUG(isp, SCSI_DEBUG, "Resetting SCSI bus");

		ip->isp_sxp_override = ISP_SXP_ORIDE_RESET_MODULE;

		ip->isp_sxp_command = ISP_SXP_RESET_BUS_CMD;

		drv_usecwait((clock_t)10000);

		/*
		 * After 10ms if the bus is not free than reset interface
		 * will fail.
		 */
		if (ip->isp_sxp_pins_control & ISP_SXP_BUS_BUSY) {
			goto fail;
		}

		drv_usecwait((clock_t)isp->isp_scsi_reset_delay * 1000);
	}

	/*
	 * Handle resetting isp chip.
	 */
	ISP_DEBUG(isp, SCSI_DEBUG, "Resetting isp");

	/*
	 * Reset the isp.
	 */
	ip->isp_sbus_icr = ISP_SBUS_ICR_SOFT_RESET;
	ip->isp_cdma_control =
	    ISP_DMA_CON_RESET_INT | ISP_DMA_CON_CLEAR_CHAN;
	ip->isp_dma_control =
	    ISP_DMA_CON_RESET_INT | ISP_DMA_CON_CLEAR_CHAN;

	/*
	 * Wait for isp to fire up.
	 */
	for (i = 0; i < ISP_RESET_WAIT; i++) {
		if (ISP_GET_MBOX_STATUS(ip->isp_mailbox0) !=
		    ISP_MBOX_BUSY) {
			break;
		}
		drv_usecwait(1000);
	}
	if (i == ISP_RESET_WAIT) {
		ISP_DEBUG(isp, SCSI_DEBUG, "timeout on isp to fire up");
		goto fail;
	}

	/*
	 * Initialize the SBus Control Parameter configuration.
	 */
	ip->isp_hccr = ISP_HCCR_CMD_PAUSE;
	ip->isp_sbus_conf1 = isp->isp_conf1_burst_flag;
	ip->isp_sbus_icr = ISP_SBUS_ICR_ENABLE_RISC_INT |
	    ISP_SBUS_ICR_ENABLE_ALL_INTS;

	ip->isp_risc_mtr = ISP_RISC_MTR_PAGE0_DEFAULT |
	    ISP_RISC_MTR_PAGE1_DEFAULT;

	ip->isp_hccr = ISP_HCCR_CMD_RELEASE;
	isp->isp_mbox.mbox_flags |= ISP_MBOX_CMD_FLAGS_Q_NOT_INIT;
	/*
	 * Start ISP firmware up.
	 */
	isp_i_mbox_cmd_init(isp, &mbox_cmd, 2, 6,
	    ISP_MBOX_CMD_START_FW, isp_risc_code_addr,
	    0, 0, 0, 0);
	if (isp_i_mbox_cmd_start(isp, &mbox_cmd)) {
		goto fail;
	}

	/*
	 * set Initiator SCSI ID
	 */
	isp_i_mbox_cmd_init(isp, &mbox_cmd, 2, 2,
	    ISP_MBOX_CMD_SET_SCSI_ID, isp->isp_initiator_id,
	    0, 0, 0, 0);
	if (isp_i_mbox_cmd_start(isp, &mbox_cmd)) {
		goto fail;
	}

	ISP_DEBUG(isp, SCSI_DEBUG, "Resetting queues");

	/*
	 * Initialize request and response queue indexes.
	 */
	isp->isp_request_in  = isp->isp_request_out = 0;
	isp->isp_request_ptr = isp->isp_request_base;

	isp->isp_response_in  = isp->isp_response_out = 0;
	isp->isp_response_ptr = isp->isp_response_base;

	isp_i_mbox_cmd_init(isp, &mbox_cmd, 5, 5,
	    ISP_MBOX_CMD_INIT_REQUEST_QUEUE, ISP_MAX_REQUESTS,
	    MSW(isp->isp_request_dvma), LSW(isp->isp_request_dvma),
	    isp->isp_request_in, 0);
	if (isp_i_mbox_cmd_start(isp, &mbox_cmd)) {
		goto fail;
	}

	isp_i_mbox_cmd_init(isp, &mbox_cmd, 6, 6,
	    ISP_MBOX_CMD_INIT_RESPONSE_QUEUE, ISP_MAX_RESPONSES,
	    MSW(isp->isp_response_dvma), LSW(isp->isp_response_dvma),
	    0, isp->isp_response_out);
	if (isp_i_mbox_cmd_start(isp, &mbox_cmd)) {
		goto fail;
	}
	isp->isp_mbox.mbox_flags &= ~ISP_MBOX_CMD_FLAGS_Q_NOT_INIT;
	/*
	 * Get ISP Ram firmware version numbers
	 */
	isp_i_mbox_cmd_init(isp, &mbox_cmd, 1, 6,
	    ISP_MBOX_CMD_ABOUT_PROM, 0, 0, 0, 0, 0);
	if (isp_i_mbox_cmd_start(isp, &mbox_cmd)) {
		goto fail;
	}

	isp->isp_major_rev = mbox_cmd.mbox_in[1];
	isp->isp_minor_rev = mbox_cmd.mbox_in[2];
	ISP_DEBUG2(isp, SCSI_DEBUG, "Firmware Version: major = %d, minor = %d",
	    isp->isp_major_rev, isp->isp_minor_rev);

	/*
	 * Handle isp capabilities adjustments.
	 */
	ISP_DEBUG(isp, SCSI_DEBUG, "Initializing isp capabilities");

	/*
	 * Check and adjust "host id" as required.
	 */
	isp_i_mbox_cmd_init(isp, &mbox_cmd, 1, 2,
	    ISP_MBOX_CMD_GET_SCSI_ID, 0, 0, 0, 0, 0);
	if (isp_i_mbox_cmd_start(isp, &mbox_cmd)) {
		goto fail;
	}
	if (mbox_cmd.mbox_in[1] != isp->isp_initiator_id) {
		ISP_DEBUG(isp, SCSI_DEBUG, "id = %d(%d)",
		    isp->isp_initiator_id,
		    mbox_cmd.mbox_in[1]);
		isp_i_mbox_cmd_init(isp, &mbox_cmd, 2, 2,
		    ISP_MBOX_CMD_SET_SCSI_ID, isp->isp_initiator_id,
		    0, 0, 0, 0);
		if (isp_i_mbox_cmd_start(isp, &mbox_cmd)) {
			goto fail;
		}
	}

	/*
	 * Check and adjust "retries" as required.
	 */
	isp_i_mbox_cmd_init(isp, &mbox_cmd, 1, 3,
	    ISP_MBOX_CMD_GET_RETRY_ATTEMPTS, 0, 0, 0, 0, 0);
	if (isp_i_mbox_cmd_start(isp, &mbox_cmd)) {
		goto fail;
	}
	if (mbox_cmd.mbox_in[1] != ISP_RETRIES ||
	    mbox_cmd.mbox_in[2] != ISP_RETRY_DELAY) {
		isp_i_mbox_cmd_init(isp, &mbox_cmd, 3, 3,
		    ISP_MBOX_CMD_SET_RETRY_ATTEMPTS, ISP_RETRIES,
		    ISP_RETRY_DELAY, 0, 0, 0);
		if (isp_i_mbox_cmd_start(isp, &mbox_cmd)) {
			goto fail;
		}
		ISP_DEBUG(isp, SCSI_DEBUG, "retries = %d(%d), delay = %d(%d)",
		    ISP_RETRIES, mbox_cmd.mbox_in[1],
		    ISP_RETRY_DELAY, mbox_cmd.mbox_in[2]);
	}

	isp_i_mbox_cmd_init(isp, &mbox_cmd, 2, 2,
	    ISP_MBOX_CMD_SET_SEL_TIMEOUT, isp_selection_timeout, 0, 0, 0, 0);
	if (isp_i_mbox_cmd_start(isp, &mbox_cmd)) {
		goto fail;
	}

	/*
	 * Check and adjust "tag age limit" as required.
	 */
	isp_i_mbox_cmd_init(isp, &mbox_cmd, 1, 2,
	    ISP_MBOX_CMD_GET_AGE_LIMIT, 0, 0, 0, 0, 0);
	if (isp_i_mbox_cmd_start(isp, &mbox_cmd)) {
		goto fail;
	}
	if (mbox_cmd.mbox_in[1] != isp->isp_scsi_tag_age_limit) {
		ISP_DEBUG(isp, SCSI_DEBUG, "tag age = %d(%d)",
		    isp->isp_scsi_tag_age_limit,
		    mbox_cmd.mbox_in[1]);
		isp_i_mbox_cmd_init(isp, &mbox_cmd, 2, 1,
		    ISP_MBOX_CMD_SET_AGE_LIMIT,
		    (u_short) isp->isp_scsi_tag_age_limit, 0, 0, 0, 0);
		if (isp_i_mbox_cmd_start(isp, &mbox_cmd)) {
			goto fail;
		}
	}

	/*
	 * Check and adjust isp queues as required.
	 */
	for (i = 0; i < NTARGETS_WIDE; i++) {
		isp_i_mbox_cmd_init(isp, &mbox_cmd, 2, 4,
		    ISP_MBOX_CMD_GET_DEVICE_QUEUE_PARAMS,
		    (u_short) (i << 8), 0, 0, 0, 0);
		if (isp_i_mbox_cmd_start(isp, &mbox_cmd)) {
		    goto fail;
		}
		ISP_DEBUG2(isp, SCSI_DEBUG,
		    "Max Queue Depth = 0x%x, Exec Trottle = 0x%x",
		    mbox_cmd.mbox_in[2], mbox_cmd.mbox_in[3]);
	}

	isp_i_mbox_cmd_init(isp, &mbox_cmd, 2, 4,
	    ISP_MBOX_CMD_GET_FIRMWARE_STATUS, 0, 0, 0, 0, 0);
	(void) isp_i_mbox_cmd_start(isp, &mbox_cmd);

	ISP_DEBUG(isp, SCSI_DEBUG, "Max # of IOs = 0x%x",
	    mbox_cmd.mbox_in[2]);

	/*
	 * Update caps from isp.
	 */
	ISP_DEBUG(isp, SCSI_DEBUG, "Getting isp capabilities");
	if (isp_i_updatecap(isp, 0, NTARGETS_WIDE)) {
		goto fail;
	}

	rval = 0;

fail:
	if (rval) {
		ISP_DEBUG(isp, SCSI_DEBUG, "reset interface failed");
		isp->isp_shutdown = 1;
		isp_i_log(isp, CE_WARN, "interface going offline");
		ISP_CLEAR_RISC_INT(isp);
		ip->isp_sbus_icr = ISP_SBUS_ICR_DISABLE_ALL_INTS;
		ip->isp_hccr = ISP_HCCR_CMD_PAUSE;
		ip->isp_risc_psr = 0xc000;
		ip->isp_risc_pcr = 0x1000;
		isp_i_qflush(isp, (u_short)0, (u_short) NTARGETS_WIDE);
	}
	TRACE_0(TR_FAC_SCSI_ISP, TR_ISP_I_RESET_INTERFACE_END,
	    "isp_i_reset_interface_end");

	return (rval);
}


#include <sys/varargs.h>

/*
 * Error logging, printing, and debug print routines
 */

/*VARARGS3*/
static void
isp_i_log(struct isp *isp, int level, char *fmt, ...)
{
	char buf[256];
	int boot_only = 0;
	dev_info_t *dip;
	va_list ap;

	if (isp) {
		dip = isp->isp_dip;
	} else {
		dip = 0;
	}

	va_start(ap, fmt);
	(void) vsprintf(buf, fmt, ap);
	va_end(ap);

	/*
	 * Workaround for bug 1135671
	 */
	if (buf[0] == '?') {
		boot_only = 1;
	}

	if (level == CE_WARN) {
		scsi_log(dip, "isp", level, "%s", buf);
	} else {
		if (boot_only) {
			scsi_log(dip, "isp", level, "?%s\n", &buf[1]);
		} else {
			scsi_log(dip, "isp", level, "%s\n", buf);
		}
	}
}


static void
isp_i_print_state(struct isp *isp)
{
/* NO OTHER THREADS ARE RUNNING */
	char buf[128];
	register int i;
	char risc_paused = 0;
	volatile struct ispregs *ip = isp->isp_reg;

	/* Put isp header in buffer for later messages. */
	isp_i_log(isp, CE_NOTE, "State dump from isp registers and driver:");

	/*
	 * Print isp registers.
	 */
	(void) sprintf(buf,
		"mailboxes(0-5): 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x",
		ip->isp_mailbox0, ip->isp_mailbox1,
		ip->isp_mailbox2, ip->isp_mailbox3,
		ip->isp_mailbox4, ip->isp_mailbox5);
	isp_i_log(isp, CE_NOTE, buf);

	if (ip->isp_hccr || ip->isp_sbus_sema) {
		(void) sprintf(buf,
			"hccr= 0x%x, sbus_sema= 0x%x",
			ip->isp_hccr, ip->isp_sbus_sema);
		isp_i_log(isp, CE_NOTE, buf);
	}
	if ((ip->isp_hccr & ISP_HCCR_PAUSE) == 0) {
		ip->isp_hccr = ISP_HCCR_CMD_PAUSE;
		risc_paused = 1;
	}

	(void) sprintf(buf,
		"sbus: isr= 0x%x, icr= 0x%x, conf0= 0x%x, conf1= 0x%x",
		ip->isp_sbus_isr, ip->isp_sbus_icr,
		ip->isp_sbus_conf0, ip->isp_sbus_conf1);
	isp_i_log(isp, CE_NOTE, buf);

	(void) sprintf(buf,
		"cdma: count= %d, addr= 0x%x, status= 0x%x, conf= 0x%x",
		ip->isp_cdma_count,
		(ip->isp_cdma_addr_hi <<16) | ip->isp_cdma_addr_lo,
		ip->isp_cdma_status, ip->isp_cdma_conf);
	if ((i = ip->isp_cdma_control) != 0) {
		(void) sprintf(&buf[strlen(buf)], ", control= 0x%x",
			(u_short) i);
	}
	if ((i = ip->isp_cdma_fifo_status) != 0) {
		(void) sprintf(&buf[strlen(buf)], ", fifo_status= 0x%x",
			(u_short) i);
	}
	isp_i_log(isp, CE_NOTE, buf);

	(void) sprintf(buf,
		"dma: count= %d, addr= 0x%x, status= 0x%x, conf= 0x%x",
		(ip->isp_dma_count_hi <<16) | ip->isp_dma_count_lo,
		(ip->isp_dma_addr_hi  <<16) | ip->isp_dma_addr_lo,
		ip->isp_dma_status, ip->isp_dma_conf);
	if ((i = ip->isp_dma_control) != 0) {
		(void) sprintf(&buf[strlen(buf)], ", control= 0x%x",
			(u_short) i);
	}
	if ((i = ip->isp_dma_fifo_status) != 0) {
		(void) sprintf(&buf[strlen(buf)], ", fifo_status= 0x%x",
			(u_short) i);
	}
	isp_i_log(isp, CE_NOTE, buf);

	/*
	 * If the risc isn't already paused, pause it now.
	 */
	if ((ip->isp_hccr & ISP_HCCR_PAUSE) == 0) {
		ip->isp_hccr = ISP_HCCR_CMD_PAUSE;
		risc_paused = 1;
	}

	(void) sprintf(buf,
	    "risc: R0-R7= 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x 0x%x",
	    ip->isp_risc_acc,
	    ip->isp_risc_r1,
	    ip->isp_risc_r2,
	    ip->isp_risc_r3,
	    ip->isp_risc_r4,
	    ip->isp_risc_r5,
	    ip->isp_risc_r6,
	    ip->isp_risc_r7);
	isp_i_log(isp, CE_NOTE, buf);

	(void) sprintf(buf,
	    "risc: R8-R15= 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x 0x%x",
	    ip->isp_risc_r8,
	    ip->isp_risc_r9,
	    ip->isp_risc_r10,
	    ip->isp_risc_r11,
	    ip->isp_risc_r12,
	    ip->isp_risc_r13,
	    ip->isp_risc_r14,
	    ip->isp_risc_r15);
	isp_i_log(isp, CE_NOTE, buf);

	(void) sprintf(buf,
	    "risc: PSR= 0x%x, IVR= 0x%x, PCR=0x%x, RAR0=0x%x, RAR1=0x%x",
	    ip->isp_risc_psr,
	    ip->isp_risc_ivr,
	    ip->isp_risc_pcr,
	    ip->isp_risc_rar0,
	    ip->isp_risc_rar1);
	isp_i_log(isp, CE_NOTE, buf);

	(void) sprintf(buf,
	    "risc: LCR= 0x%x, PC= 0x%x, MTR=0x%x, EMB=0x%x, SP=0x%x",
	    ip->isp_risc_lcr,
	    ip->isp_risc_pc,
	    ip->isp_risc_mtr,
	    ip->isp_risc_emb,
	    ip->isp_risc_sp);
	isp_i_log(isp, CE_NOTE, buf);

	/*
	 * If we paused the risc, restart it.
	 */
	if (risc_paused) {
		ip->isp_hccr = ISP_HCCR_CMD_RELEASE;
	}

	/*
	 * Print isp queue settings out.
	 */
	isp->isp_request_out = ip->isp_mailbox4;
	(void) sprintf(buf,
	    "request(in/out)= %d/%d, response(in/out)= %d/%d",
	    isp->isp_request_in, isp->isp_request_out,
	    isp->isp_response_in, isp->isp_response_out);
	isp_i_log(isp, CE_NOTE, buf);

	(void) sprintf(buf,
	    "request_ptr(current, base)=  0x%x (0x%x)",
	    (int)isp->isp_request_ptr, (int)isp->isp_request_base);
	isp_i_log(isp, CE_NOTE, buf);

	(void) sprintf(buf,
	    "response_ptr(current, base)= 0x%x (0x%x)",
	    (int)isp->isp_response_ptr, (int)isp->isp_response_base);
	isp_i_log(isp, CE_NOTE, buf);

	if (ip->isp_cdma_addr_hi || ip->isp_cdma_addr_lo) {
		(void) sprintf(buf,
		    "dvma request_ptr= 0x%x - 0x%x",
		    (int)isp->isp_request_dvma,
		    (int)isp->isp_response_dvma);
		isp_i_log(isp, CE_NOTE, buf);

		(void) sprintf(buf,
		    "dvma response_ptr= 0x%x - 0x%x",
		    (int)isp->isp_response_dvma,
		    (int)isp->isp_request_dvma + ISP_QUEUE_SIZE);
		isp_i_log(isp, CE_NOTE, buf);
	}


	/*
	 * Print out sync scsi info and suppress trailing zero
	 * period and offset entries.
	 */
	if (isp->isp_scsi_options & SCSI_OPTIONS_SYNC) {
		(void) sprintf(buf, "period/offset:");
		for (i = 0; i < NTARGETS; i++) {
			(void) sprintf(&buf[strlen(buf)], " %d/%d",
			    PERIOD_MASK(isp->isp_synch[i]),
			    OFFSET_MASK(isp->isp_synch[i]));
		}
		isp_i_log(isp, CE_NOTE, buf);
		(void) sprintf(buf, "period/offset:");
		for (i = NTARGETS; i < NTARGETS_WIDE; i++) {
			(void) sprintf(&buf[strlen(buf)], " %d/%d",
			    PERIOD_MASK(isp->isp_synch[i]),
			    OFFSET_MASK(isp->isp_synch[i]));
		}
		isp_i_log(isp, CE_NOTE, buf);
	}

/* OTHER THREADS ARE RUNNING */
}


/*
 * Function name : isp_i_alive()
 *
 * Return Values : FALSE - failed
 *		   TRUE	 - succeeded
 */
static int
isp_i_alive(struct isp *isp)
{
	struct isp_mbox_cmd mbox_cmd;
	u_short rval;
	u_short	total_io_complition;
	u_short	total_queued_io;
	u_short	total_exe_io;

	isp_i_mbox_cmd_init(isp, &mbox_cmd, 1, 1,
	ISP_MBOX_CMD_GET_ISP_STAT, 0, 0, 0, 0, 0);
	ISP_MUTEX_ENTER(isp);
	if (isp_i_mbox_cmd_start(isp, &mbox_cmd)) {
		rval = FALSE;
		goto fail;
	}

	total_io_complition = mbox_cmd.mbox_in[1];
	total_queued_io = mbox_cmd.mbox_in[3];
	total_exe_io = mbox_cmd.mbox_in[2];
#ifdef ISPDEBUG
	if (isp_timeout_debug) {
		ISP_DEBUG(isp, SCSI_DEBUG, "total_queued_io=%d,"
		"total_io_complition=%d, total_exe_io=%d",
		total_queued_io, total_io_complition, total_exe_io);
	}
#endif

	if ((total_io_complition == 0) && (total_exe_io == 0) &&
		(total_queued_io != 0)) {
		rval = FALSE;
		ISP_DEBUG(isp, SCSI_DEBUG, "total_queued_io=%d",
		total_queued_io);






	} else {
		rval = TRUE;
	}
fail:
	mutex_exit(ISP_RESP_MUTEX(isp));
	ISP_CHECK_WAITQ_AND_EXIT_REQ_MUTEX(isp);
	return (rval);
}

/*
 * kmem cache constructor and destructor.
 * When constructing, we bzero the isp cmd structure
 * When destructing, just free the dma handle
 */

static void
isp_kmem_cache_constructor(void * buf, size_t size)
{
	struct isp_cmd *sp = buf;
	bzero((caddr_t)sp, size);
}

/* ARGSUSED */
static void
isp_kmem_cache_destructor(void * buf, size_t size)
{
	struct isp_cmd *sp = buf;
	if (sp->cmd_dmahandle) {
		ddi_dma_free_handle(&sp->cmd_dmahandle);
	}
}
#ifdef ISPDEBUG
static void
isp_i_test(struct isp *isp, struct isp_cmd *sp)
{
	struct scsi_pkt *pkt = (sp != NULL)? CMD2PKT(sp) : NULL;

	if (isp_test_abort) {
		mutex_exit(ISP_RESP_MUTEX(isp));
		isp_scsi_abort(&pkt->pkt_address, pkt);
		mutex_enter(ISP_RESP_MUTEX(isp));
		isp_debug_enter_count++;
		isp_test_abort = 0;
	}
	if (isp_test_abort_all) {
		mutex_exit(ISP_RESP_MUTEX(isp));
		isp_scsi_abort(&pkt->pkt_address,
		    NULL);
		mutex_enter(ISP_RESP_MUTEX(isp));
		isp_debug_enter_count++;
		isp_test_abort_all = 0;
	}
	if (isp_test_reset) {
		mutex_exit(ISP_RESP_MUTEX(isp));
		isp_scsi_reset(&pkt->pkt_address,
		    RESET_TARGET);
		mutex_enter(ISP_RESP_MUTEX(isp));
		isp_debug_enter_count++;
		isp_test_reset = 0;
	}
	if (isp_test_reset_all) {
		mutex_exit(ISP_RESP_MUTEX(isp));
		isp_scsi_reset(&pkt->pkt_address,
		    RESET_ALL);
		mutex_enter(ISP_RESP_MUTEX(isp));
		isp_debug_enter_count++;
		isp_test_reset_all = 0;
	}
	if (isp_test_fatal) {
		isp_test_fatal = 0;
		isp_i_fatal_error(isp);
		isp_debug_enter_count++;
		isp_test_fatal = 0;
	}
}
#endif
