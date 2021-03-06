/*
 * Copyright (c) 1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */
#pragma	ident  "@(#)fas.c 1.70     95/09/13 SMI"

/*
 * ISSUES
 *
 * - block cmd:	eliminate the code?
 * - simplify msgin/out	handling
 * - asynchronous notification
 * - more consistent error messages
 * - report name of device on errors?
 * - tracing
 * - cleanup fas*h files, compare with doc
 * - add PIO (dma and fas366) stats/counters
 * - if	wide target renegotiates sync, back to narrow?
 * - last_msgout is not	accurate ????
 * - resolve XXXX
 * - improve msg reject	code (use special msg reject handler)
 * - better use	of IDE message
 * - reset notification
 * - keep track	if ATN remains asserted	and target not going into
 *   a msg-out phase
 * - doesn't work with shoebox (hang)
 * - suspend/resume testing
 *   watch for target-initiated	negotiations
 * - improve comments
 * - \n	in debug messages
 * - eliminate hacks
 * - no slave accesses when start address is odd and dma hasn't started
 *   this affect asserting ATN
 * - target didn't disconnect after CMD COMPLETE msg because ATN was
 *   asserted
 */

/*
 * fas - QLogic	fas366 wide/fast SCSI Processor	HBA driver with
 *	tagged and non-tagged queueing support
 */
#if defined(lint) && !defined(DEBUG)
#define	DEBUG	1
#define	FASDEBUG
#endif

/* XXXXX */
#define	ODDBYTE_HACK		/* enable odd byte hack	*/
#define	CMDAREA_HACK		/* cmd phase hangs */
#define	ATN_DEASSERT_HACK	/* send	negotiation bytes by PIO */
#define	CMD_TIMEOUT_HACK	/* print data-in hang warning */
#define	ETHERNET_HACK		/* do not enable 64 bit sbus */
#define	RESET_BUS_HACK		/* reset chip after bus reset	*/
#define	BREAK_XFER_HACK		/* break scsi write under special condition */
#define	OLDER_HM_REVS		/* eanble workarounds for older revs */

#undef TRACE	/* XXX remove when tracing compiles */

#define	DMA_REG_TRACING		/* enable dma register access tracing */

/*
 * standard header files
 */
#include <sys/note.h>
#include <sys/scsi/scsi.h>

/*
 * private header files
 */
#include <sys/scsi/adapters/fasdma.h>
#include <sys/scsi/adapters/fasreg.h>
#include <sys/scsi/adapters/fasvar.h>
#include <sys/scsi/adapters/fascmd.h>
#include <sys/scsi/adapters/reset_notify.h>

/*
 * non-ddi compliant header files
 */
#include <sys/varargs.h>
#include <sys/var.h>
#include <sys/proc.h>
#include <sys/thread.h>
#include <sys/vtrace.h>

extern u_char	scsi_cdb_size[];

/*
 * tunables
 */
static int		fas_selection_timeout =	250; /*	250 milliseconds */
static u_char		fas_default_offset = DEFAULT_OFFSET;

#ifdef	FASDEBUG
int			fasdebug = 0;
int			fasdebug_instance = -1;	/* debug all instances */
static int		fas_burstsizes_limit = -1;
static int		fas_no_sync_wide_backoff = 0;
#endif	/* FASDEBUG */

/*
 * Local static	data protected by global mutex
 */
static kmutex_t		fas_global_mutex; /* to	allow concurrent attach	*/
static void		*fas_state;	/* soft	state ptr		*/
static struct fas	*fas_head;	/* link	all softstate structures */
static struct fas	*fas_tail;	/* for fas_watch()		*/

static int		fas_scsi_watchdog_tick;	/* in seconds, for all	*/
					/* instances			*/
static long		fas_tick;	/* fas_watch() interval	in Hz	*/
static int		fas_reset_watch; /* timeout id for reset watch	*/

_NOTE(MUTEX_PROTECTS_DATA(fas_global_mutex, fas_reset_watch))
_NOTE(DATA_READABLE_WITHOUT_LOCK(fas_state fas_head fas_tail \
	fas_scsi_watchdog_tick fas_tick))

/*
 * dma attribute structure for scsi engine
 */
static ddi_dma_attr_t dma_fasattr	= {
	DMA_ATTR_V0, (unsigned long long)0,
	(unsigned long long)0xffffffff,	(unsigned long long)((1<<24)-1),
	1, DEFAULT_BURSTSIZE, 1,
	(unsigned long long)0xffffffff,	(unsigned long long)((1<<24)-1),
	1, 512,	0
};

/*
 * optional torture test stuff
 */
#ifdef	FASDEBUG
#define	FAS_TEST
static int fas_ptest_emsgin;
static int fas_ptest_msgin;
static int fas_ptest_msg = -1;
static int fas_ptest_status;
static int fas_ptest_data_in;
static int fas_atest;
static int fas_atest_disc;
static int fas_atest_reconn;
static void fas_test_abort(struct fas *fas, int	slot);
static int fas_rtest;
static int fas_rtest_type;
static void fas_test_reset(struct fas *fas, int	slot);
static int fas_force_timeout;
static int fas_btest;
static int fas_test_stop;
#endif

#ifdef CMDAREA_HACK
int buf_offset_var = 0;
int fas_enable_cmdarea_workaround = 1;
#endif

#ifdef ATN_DEASSERT_HACK
int fas_enable_atn_deassert_workaround = 1;
#endif

#ifdef CMD_TIMEOUT_HACK
int fas_enable_cmd_timeout_workaround = 0;
#endif

#ifdef ODDBYTE_HACK
static int fas_enable_oddbyte_workaround = 0;
#endif

#ifdef ETHERNET_HACK
static int fas_enable_sbus64 = 1;
#endif

#ifdef RESET_BUS_HACK
static int fas_enable_reset_bus_workaround = 1;
#endif

#ifdef BREAK_XFER_HACK
static int fas_break_xfer_flag = 0;
#endif

/*
 * warlock directives
 */
_NOTE(SCHEME_PROTECTS_DATA("unique per pkt", scsi_pkt fas_cmd buf))
_NOTE(DATA_READABLE_WITHOUT_LOCK(dma fasdebug))
_NOTE(SCHEME_PROTECTS_DATA("stable data", scsi_address))

/*
 * function prototypes
 *
 * scsa	functions are exported by means	of the transport table:
 */
static int fas_scsi_tgt_init(dev_info_t	*, dev_info_t *,
    scsi_hba_tran_t *, struct scsi_device *);
static int fas_scsi_start(struct scsi_address *ap, struct scsi_pkt *pkt);
static int fas_scsi_abort(struct scsi_address *ap, struct scsi_pkt *pkt);
static int fas_scsi_reset(struct scsi_address *ap, int level);
static int fas_scsi_getcap(struct scsi_address *ap, char *cap, int whom);
static int fas_scsi_setcap(struct scsi_address *ap, char *cap, int value,
    int	whom);
static struct scsi_pkt *fas_scsi_init_pkt(struct scsi_address *ap,
    struct scsi_pkt *pkt, struct buf *bp, int cmdlen, int statuslen,
    int	tgtlen,	int flags, int (*callback)(), caddr_t arg);
static void fas_scsi_destroy_pkt(struct	scsi_address *ap, struct scsi_pkt *pkt);
static void fas_scsi_dmafree(struct scsi_address *ap,
    struct scsi_pkt *pkt);
static void fas_scsi_sync_pkt(struct scsi_address *ap,
    struct scsi_pkt *pkt);

/*
 * internal functions:
 */
static int fas_prepare_pkt(register struct fas *fas, struct fas_cmd *sp);
static int fas_alloc_tag(struct	fas *fas, struct fas_cmd *sp);
static int fas_accept_pkt(struct fas *fas, struct fas_cmd *sp, int flag);
static void fas_empty_waitQ(struct fas *fas);
static void fas_check_waitQ_and_mutex_exit(struct fas *fas);
static int fas_istart(struct fas *fas);
static int fas_ustart(struct fas *fas);
static int fas_startcmd(struct fas *fas, struct	fas_cmd	*sp);

static int fas_pkt_alloc_extern(struct fas *fas, struct	fas_cmd	*sp,
    int	cmdlen,	int tgtlen, int	statuslen, int kf);
static void fas_pkt_destroy_extern(struct fas *fas, struct fas_cmd *sp);
static void fas_kmem_cache_constructor(void *buf, size_t size);
static void fas_kmem_cache_destructor(void *buf, size_t	size);

static int fas_finish(struct fas *fas);
static void fas_handle_qfull(struct fas	*fas, struct fas_cmd *sp);
static void fas_restart_cmd(struct fas *fas);
static int fas_dopoll(struct fas *fas, int timeout);
static void fas_runpoll(struct fas *fas, short slot, struct fas_cmd *sp);
static u_int fas_intr(caddr_t arg);
static void fas_intr_svc(struct	fas *fas);
static int fas_phasemanage(struct fas *fas);
static int fas_handle_unknown(struct fas *fas);
static int fas_handle_cmd_start(struct fas *fas);
static int fas_handle_cmd_done(struct fas *fas);
static int fas_handle_msg_out_start(struct fas *fas);
static int fas_handle_msg_out_done(struct fas *fas);
static int fas_handle_clearing(struct fas *fas);
static int fas_handle_data_start(struct	fas *fas);
static int fas_handle_data_done(struct fas *fas);
static int fas_handle_c_cmplt(struct fas *fas);
static int fas_handle_msg_in_start(struct fas *fas);
static int fas_handle_more_msgin(struct	fas *fas);
static int fas_handle_msg_in_done(struct fas *fas);
static int fas_onebyte_msg(struct fas *fas);
static int fas_twobyte_msg(struct fas *fas);
static int fas_multibyte_msg(struct fas	*fas);
static int fas_finish_select(struct fas	*fas);
static int fas_reselect_preempt(struct fas *fas);
static int fas_reconnect(struct	fas *fas);
static int fas_handle_selection(struct fas *fas);
static void fas_head_of_readyQ(struct fas *fas,	struct fas_cmd *sp);
static int fas_handle_gross_err_spurious_intr(struct fas *fas);
static int fas_check_dma_error(register	struct fas *fas);

static void fas_make_sdtr(struct fas *fas, int msgout_offset, int target);
static void fas_make_wdtr(struct fas *fas, int msgout_offset, int target,
    int	width);
static void fas_update_props(struct fas	*fas, int tgt);
static void fas_update_this_prop(struct	fas *fas, char *property, int value);

static int fas_commoncap(struct	scsi_address *ap, char *cap, int val,
    int	tgtonly, int doset);

static void fas_watch(caddr_t arg);
static void fas_watchsubr(struct fas *fas);
static void fas_cmd_timeout(struct fas *fas, int slot);
static void fas_sync_wide_backoff(struct fas *fas, struct fas_cmd *sp,
    int	slot);
static void fas_reset_sync_wide(struct fas *fas);
static void fas_set_wide_conf3(struct fas *fas,	int target, int	width);
static void fas_force_renegotiation(struct fas *fas, int target);

static int fas_set_new_window(struct fas *fas, struct fas_cmd *sp);
static int fas_restore_pointers(struct fas *fas, struct	fas_cmd	*sp);
static int fas_next_window(struct fas *fas, struct fas_cmd *sp,	u_int end);

static void fas_log(struct fas *fas, int level,	const char *fmt, ...);
void fas_printf(struct fas *fas,	const char *fmt, ...);
static void fas_printstate(struct fas *fas, char *msg);
static void fas_dump_cmd(struct	fas *fas, struct fas_cmd *sp);
static char *fas_state_name(u_short state);

static void fas_makeproxy_cmd(struct fas_cmd *sp,
    struct scsi_address	*ap, struct scsi_pkt *pkt, int nmsg, ...);
static int fas_do_proxy_cmd(struct fas *fas, struct fas_cmd *sp,
    struct scsi_address	*ap, char *what);

static void fas_internal_reset(struct fas *fas,	int reset_action);
static int fas_alloc_active_slots(struct fas *fas, int slot, int flag);

static int fas_abort_curcmd(struct fas *fas);
static int fas_abort_cmd(struct	fas *fas, struct fas_cmd *sp, int slot);
static int fas_do_scsi_abort(struct scsi_address *ap, struct scsi_pkt *pkt);
static int fas_do_scsi_reset(struct scsi_address *ap, int level);
static int fas_remove_from_readyQ(struct fas *fas, struct fas_cmd *sp,
    int	slot);
static void fas_flush_readyQ(struct fas	*fas, int slot);
static void fas_flush_tagQ(struct fas *fas, int	slot);
static void fas_flush_cmd(struct fas *fas, struct fas_cmd *sp,
    u_char reason, u_long stat);
static int fas_abort_connected_cmd(struct fas *fas, struct fas_cmd *sp,
    u_char msg,	int slot);
static int fas_abort_disconnected_cmd(struct fas *fas, struct scsi_address *ap,
    struct fas_cmd *sp,	u_char msg, int	slot);
static void fas_mark_packets(struct fas	*fas, int slot,	u_char reason,
    u_long stat);
static void fas_set_pkt_reason(struct fas *fas,	struct fas_cmd *sp,
    u_char reason, u_long stat);

static int fas_reset_bus(struct	fas *fas);
static int fas_reset_recovery(struct fas *fas);
static int fas_reset_connected_cmd(struct fas *fas, struct scsi_address	*ap,
    int	slot);
static int fas_reset_disconnected_cmd(struct fas *fas, struct scsi_address *ap);
static void fas_start_watch_reset_delay();
static void fas_setup_reset_delay(struct fas *fas);
static void fas_watch_reset_delay(caddr_t arg);
static int fas_watch_reset_delay_subr(register struct fas *fas);
static void fas_reset_cleanup(struct fas *fas, int slot);
static int fas_scsi_reset_notify(struct	scsi_address *ap, int flag,
    void (*callback)(caddr_t), caddr_t arg);

static void fas_set_throttles(struct fas *fas, int slot,
    int	n, int what);
static void fas_full_throttle(struct fas *fas, int slot);
static void fas_save_throttles(struct fas *fas,	int slot, int n,
    short *throttle);
static void fas_restore_throttles(struct fas *fas, int slot, int n,
    short *throttle);
static void fas_remove_cmd(struct fas *fas, struct fas_cmd *sp,	int timeout);
static void fas_decrement_ncmds(struct fas *fas, struct	fas_cmd	*sp);

static int fas_create_arq_pkt(struct fas *fas, struct scsi_address *ap);
static int fas_delete_arq_pkt(struct fas *fas, struct scsi_address *ap);
static void fas_handle_sts_chk(struct fas *fas,	struct fas_cmd *sp);
void fas_complete_arq_pkt(struct fas *fas, struct fas_cmd *sp,
    int	slot);

void fas_call_pkt_comp(register	struct fas *fas, register struct fas_cmd *sp);
void fas_empty_callbackQ(register struct callback_info *cb_info);
int fas_init_callbacks(struct fas *fas);

static int fas_check_dma_error(register	struct fas *fas);
static int fas_init_chip(struct	fas *fas, u_char id);

static void fas_read_fifo(struct fas *fas);
static void fas_write_fifo(struct fas *fas, u_char *buf, int length, int pad);

#ifdef FASDEBUG
static void fas_reg_cmd_write(struct fas *fas, u_char cmd);
static void fas_reg_write(struct fas *fas, volatile u_char *p, u_char what);
static u_char fas_reg_read(struct fas *fas, volatile u_char *p);

static void fas_dma_reg_write(struct fas *fas, volatile	u_long *p,
    u_long what);
static u_long fas_dma_reg_read(struct fas *fas,	volatile u_long	*p);
#else
#define	fas_reg_cmd_write(fas, cmd) \
	fas->f_reg->fas_cmd = (cmd), fas->f_last_cmd = (cmd)
#define	fas_reg_write(fas, p, what)  *(p) = (what)
#define	fas_reg_read(fas, p) *(p)
#define	fas_dma_reg_write(fas, p, what)	 *(p) =	(what)
#define	fas_dma_reg_read(fas, p) *(p)
#endif

/*
 * autoconfiguration data and routines.
 */
static int fas_info(dev_info_t *dip, ddi_info_cmd_t infocmd,
    void *arg, void **result);
static int fas_identify(dev_info_t *dev);
static int fas_attach(dev_info_t *dev, ddi_attach_cmd_t	cmd);
static int fas_detach(dev_info_t *dev, ddi_detach_cmd_t	cmd);

static struct dev_ops fas_ops =	{
	DEVO_REV,		/* devo_rev, */
	0,			/* refcnt  */
	fas_info,		/* info	*/
	fas_identify,		/* identify */
	nulldev,		/* probe */
	fas_attach,		/* attach */
	fas_detach,		/* detach */
	nodev,			/* reset */
	(struct	cb_ops *)0,	/* driver operations */
	NULL,			/* bus operations */
	ddi_power		/* power */
};

char _depends_on[] = "misc/scsi";

static struct modldrv modldrv =	{
	&mod_driverops,	/* Type	of module. This	one is a driver	*/
	"FAS SCSI Host Bus Adapter Driver", /* Name of the module. */
	&fas_ops,	/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1, (void	*)&modldrv, NULL
};

_init(void)
{
	int rval;
	/* CONSTCOND */
	ASSERT(NO_COMPETING_THREADS);

	rval = ddi_soft_state_init(&fas_state, sizeof (struct fas),
	    FAS_INITIAL_SOFT_SPACE);
	if (rval != 0) {
		return (rval);
	}

	if ((rval = scsi_hba_init(&modlinkage))	!= 0) {
		ddi_soft_state_fini(&fas_state);
		return (rval);
	}

	mutex_init(&fas_global_mutex, "fas_global_mutex", MUTEX_DRIVER,	NULL);
	if ((rval = mod_install(&modlinkage)) != 0) {
		mutex_destroy(&fas_global_mutex);
		ddi_soft_state_fini(&fas_state);
		scsi_hba_fini(&modlinkage);
		return (rval);
	}

	return (rval);
}

_fini(void)
{
	int	rval;
	/* CONSTCOND */
	ASSERT(NO_COMPETING_THREADS);

	if ((rval = mod_remove(&modlinkage)) ==	0) {
		ddi_soft_state_fini(&fas_state);
		scsi_hba_fini(&modlinkage);
		mutex_destroy(&fas_global_mutex);
	}
	return (rval);
}

_info(struct modinfo *modinfop)
{
	/* CONSTCOND */
	ASSERT(NO_COMPETING_THREADS);

	return (mod_info(&modlinkage, modinfop));
}

/*ARGSUSED*/
static int
fas_scsi_tgt_init(dev_info_t *hba_dip, dev_info_t *tgt_dip,
    scsi_hba_tran_t *hba_tran, struct scsi_device *sd)
{
	return (((sd->sd_address.a_target < NTARGETS_WIDE) &&
	    (sd->sd_address.a_lun < NLUNS_PER_TARGET)) ?
		DDI_SUCCESS : DDI_FAILURE);
}

/*
 * return DDI_FAILURE if driver does not have cb_ops structure
 */
/*ARGSUSED*/
static int
fas_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result)
{
	return (DDI_FAILURE);
}

static int
fas_identify(dev_info_t	*devi)
{
	char *dname = ddi_get_name(devi);
	/* CONSTCOND */
	ASSERT(NO_COMPETING_THREADS);

	if (strcmp(dname, "SUNW,fas") == 0) {
		return (DDI_IDENTIFIED);
	} else {
		return (DDI_NOT_IDENTIFIED);
	}
}

/*ARGSUSED*/
static int
fas_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	register struct	fas	*fas = NULL;
	volatile struct	dma	*dmar =	NULL;
	volatile struct	fasreg	*fasreg;
	ddi_dma_attr_t		*fas_dma_attr;
	ddi_device_acc_attr_t	dev_attr;
	ddi_acc_handle_t	regs_acc_handle;
	ddi_acc_handle_t	cmdarea_acc_handle;
	ddi_acc_handle_t	dmar_acc_handle;

	int			instance, id, power, slot, i, hm_rev;
	u_int			rlen, count;
	char			buf[64];
	scsi_hba_tran_t		*tran =	NULL;
	ulong_t			timestamp;
	char			intr_added = 0;
	char			mutex_init_done	= 0;
	char			hba_attached = 0;
	char			bound_handle = 0;
	char			*prop_template = "target%d-scsi-options";
	char			prop_str[32];

	/* CONSTCOND */
	ASSERT(NO_COMPETING_THREADS);

	switch (cmd) {
	case DDI_ATTACH:
		break;

	case DDI_RESUME:
		tran = (scsi_hba_tran_t	*)ddi_get_driver_private(dip);
		if (!tran) {
			return (DDI_FAILURE);
		}
		fas = TRAN2FAS(tran);
		if (!fas) {
			return (DDI_FAILURE);
		}
		/*
		 * Reset hardware and softc to "no outstanding commands"
		 * Note	that a check condition can result on first command
		 * to a	target.
		 */
		mutex_enter(FAS_MUTEX(fas));
		fas_internal_reset(fas,
		    FAS_RESET_SOFTC|FAS_RESET_FAS|FAS_RESET_DMA);

		fas->f_suspended = 0;

		mutex_exit(FAS_MUTEX(fas));
		return (DDI_SUCCESS);

	default:
		return (DDI_FAILURE);
	}

	instance = ddi_get_instance(dip);

	/*
	 * Since we know that some instantiations of this device can
	 * be plugged into slave-only SBus slots, check	to see whether
	 * this	is one such.
	 */
	if (ddi_slaveonly(dip) == DDI_SUCCESS) {
		cmn_err(CE_WARN,
		    "fas%d: device in slave-only slot",	instance);
		return (DDI_FAILURE);
	}

	if (ddi_intr_hilevel(dip, 0)) {
		/*
		 * Interrupt number '0'	is a high-level	interrupt.
		 * At this point you either add	a special interrupt
		 * handler that	triggers a soft	interrupt at a lower level,
		 * or -	more simply and	appropriately here - you just
		 * fail	the attach.
		 */
		cmn_err(CE_WARN,
		    "fas%d: Device is using a hilevel intr", instance);
		return (DDI_FAILURE);
	}

	/*
	 * Allocate softc information.
	 */
	if (ddi_soft_state_zalloc(fas_state, instance) != DDI_SUCCESS) {
		cmn_err(CE_WARN,
		    "fas%d: cannot allocate soft state", instance);
		goto fail;
	}

	fas = (struct fas *)ddi_get_soft_state(fas_state, instance);

	if (fas	== NULL) {
		goto fail;
	}

	/*
	 * map in device registers
	 */
	dev_attr.devacc_attr_version = DDI_DEVICE_ATTR_V0;
	dev_attr.devacc_attr_endian_flags = DDI_NEVERSWAP_ACC;
	dev_attr.devacc_attr_dataorder = DDI_STRICTORDER_ACC;

	if (ddi_regs_map_setup(dip, (u_int)0, (caddr_t *)&dmar,
	    (off_t)0, (off_t)sizeof (struct dma),
	    &dev_attr, &dmar_acc_handle) != DDI_SUCCESS) {
		cmn_err(CE_WARN, "fas%d: cannot map dma", instance);
		goto fail;
	}

	if (ddi_regs_map_setup(dip, (u_int)1, (caddr_t *)&fasreg,
	    (off_t)0, (off_t)sizeof (struct fasreg),
	    &dev_attr, &regs_acc_handle) != DDI_SUCCESS) {
		cmn_err(CE_WARN,
		    "fas%d: unable to map fas366 registers", instance);
		goto fail;
	}

	fas_dma_attr = &dma_fasattr;
	if (ddi_dma_alloc_handle(dip, fas_dma_attr,
	    DDI_DMA_SLEEP, NULL, &fas->f_dmahandle) != DDI_SUCCESS) {
		cmn_err(CE_WARN,
		    "fas%d: cannot alloc dma handle", instance);
		goto fail;
	}

	/*
	 * allocate cmdarea and	its dma	handle
	 */
#ifdef CMDAREA_HACK
	if (DMAREV(dmar) > 0) {
		fas_enable_cmdarea_workaround = 0;
	}

	/*
	 * software workaround for cmd phase hang:
	 * align the cmdarea buffer to offset 0
	 */
	if (fas_enable_cmdarea_workaround) {
		if (ddi_dma_mem_alloc(fas->f_dmahandle,	(u_int)2*PAGESIZE,
		    &dev_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
		    NULL, (caddr_t *)&fas->f_cmdarea, &rlen,
			&cmdarea_acc_handle) == DDI_SUCCESS) {
			u_long temp;
			temp = (u_long)fas->f_cmdarea;
			fas->f_cmdarea = (u_char *)((temp + (PAGESIZE -	1)) &
				~(PAGESIZE - 1)) + buf_offset_var;
		} else {
			cmn_err(CE_WARN,
			    "fas%d: cannot alloc cmd area", instance);
			goto fail;
		}
	} else {
		if (ddi_dma_mem_alloc(fas->f_dmahandle,	(u_int)2*FIFOSIZE,
		    &dev_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
		    NULL, (caddr_t *)&fas->f_cmdarea, &rlen,
		    &cmdarea_acc_handle) != DDI_SUCCESS) {

#else
	if (ddi_dma_mem_alloc(fas->f_dmahandle,	(u_int)2*FIFOSIZE,
	    &dev_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, (caddr_t *)&fas->f_cmdarea, &rlen,
	    &cmdarea_acc_handle) != DDI_SUCCESS) {
#endif
		cmn_err(CE_WARN,
		    "fas%d: cannot alloc cmd area", instance);
		goto fail;
	}

#ifdef CMDAREA_HACK
	}
#endif

	fas->f_reg = fasreg;
	fas->f_dma = dmar;
	fas->f_instance	 = instance;

	if (ddi_dma_addr_bind_handle(fas->f_dmahandle,
	    NULL, (caddr_t)fas->f_cmdarea,
	    rlen, DDI_DMA_RDWR|DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
	    &fas->f_dmacookie, &count) != DDI_DMA_MAPPED) {
		cmn_err(CE_WARN,
		    "fas%d: cannot bind cmdarea", instance);
		goto fail;
	}
	bound_handle++;

	ASSERT(count ==	1);

	/*
	 * Allocate a transport	structure
	 */
	tran = scsi_hba_tran_alloc(dip,	0);
	if (tran == NULL) {
		cmn_err(CE_WARN, "fas: scsi_hba_tran_alloc failed\n");
		goto fail;
	}

	/*
	 * initialize transport	structure
	 */
	fas->f_tran			= tran;
	fas->f_dev			= dip;
	tran->tran_hba_private		= fas;
	tran->tran_tgt_private		= NULL;
	tran->tran_tgt_init		= fas_scsi_tgt_init;
	tran->tran_tgt_probe		= NULL;
	tran->tran_tgt_free		= NULL;
	tran->tran_start		= fas_scsi_start;
	tran->tran_abort		= fas_scsi_abort;
	tran->tran_reset		= fas_scsi_reset;
	tran->tran_getcap		= fas_scsi_getcap;
	tran->tran_setcap		= fas_scsi_setcap;
	tran->tran_init_pkt		= fas_scsi_init_pkt;
	tran->tran_destroy_pkt		= fas_scsi_destroy_pkt;
	tran->tran_dmafree		= fas_scsi_dmafree;
	tran->tran_sync_pkt		= fas_scsi_sync_pkt;
	tran->tran_reset_notify		= fas_scsi_reset_notify;

	/*
	 * disable tagged queuing and wide for all targets
	 * (will be enabled by target driver if	required)
	 * sync	is enabled by default
	 */
	fas->f_nowide =	fas->f_notag = ALL_TARGETS;

	/*
	 * f_active is used for	saving disconnected cmds;
	 * For tagged targets, we need to increase the size later
	 * Only	allocate for Lun == 0, if we probe a lun > 0 then
	 * we allocate an active structure
	 * If TQ gets enabled then we need to increase the size
	 * to hold 256 cmds
	 */
	for (slot = 0; slot < N_SLOTS; slot += NLUNS_PER_TARGET) {
		(void) fas_alloc_active_slots(fas, slot, KM_SLEEP);
	}

	/*
	 * initialize the qfull	retry counts
	 */
	for (i = 0; i <	NTARGETS_WIDE; i++) {
		fas->f_qfull_retries[i]	= QFULL_RETRIES;
		fas->f_qfull_retry_interval[i] =
			drv_usectohz(QFULL_RETRY_INTERVAL * 1000);

	}

	/*
	 * Initialize throttles.
	 */
	fas_set_throttles(fas, 0, N_SLOTS, MAX_THROTTLE);

	/*
	 * Initialize mask of deferred property updates
	 */
	fas->f_props_update = 0;

	/*
	 * set host ID
	 */
	fas->f_fasconf = DEFAULT_HOSTID;
	id = ddi_prop_get_int(DDI_DEV_T_ANY, dip, 0, "initiator-id", -1);
	if (id == -1) {
		id = ddi_prop_get_int(DDI_DEV_T_ANY,	dip, 0,
		    "scsi-initiator-id", -1);
	}
	if (id != DEFAULT_HOSTID && id >= 0 && id < NTARGETS_WIDE) {
		fas_log(fas, CE_NOTE, "initiator SCSI ID now %d", id);
		fas->f_fasconf = (u_char)id;
	}

	/*
	 * find	the burstsize and reduce ours if necessary
	 */
	fas->f_dma_attr	= fas_dma_attr;
	fas->f_dma_attr->dma_attr_burstsizes &=
	    ddi_dma_burstsizes(fas->f_dmahandle);

#ifdef FASDEBUG
	fas->f_dma_attr->dma_attr_burstsizes &=	fas_burstsizes_limit;
	IPRINTF1("dma burstsize=%x\n", fas->f_dma_attr->dma_attr_burstsizes);
#endif
	/*
	 * Attach this instance	of the hba
	 */
	if (scsi_hba_attach_setup(dip, fas->f_dma_attr,	tran, 0) !=
	    DDI_SUCCESS) {
		fas_log(fas, CE_WARN, "scsi_hba_attach_setup failed");
		goto fail;
	}
	hba_attached++;

	/*
	 * if scsi-options property exists, use	it
	 */
	fas->f_scsi_options = ddi_prop_get_int(DDI_DEV_T_ANY,
	    dip, 0, "scsi-options", DEFAULT_SCSI_OPTIONS);

	/*
	 * if hm-rev property doesn't exist, use old scheme for rev
	 */
	hm_rev = ddi_prop_get_int(DDI_DEV_T_ANY, dip, 0,
	    "hm-rev", -1);

	if (hm_rev == 0xa0 || hm_rev == -1) {
		if (DMAREV(dmar) != 0) {
#ifdef OLDER_HM_REVS
			fas_enable_oddbyte_workaround = 1;
			fas_enable_sbus64 = 0;
#endif
			fas_log(fas, CE_NOTE,
			    "obsolete rev 2.0 hm chip, disabled workarounds");
		} else {
#ifdef OLDER_HM_REVS
			fas_enable_cmd_timeout_workaround = 1;
			fas_enable_oddbyte_workaround = 1;
			fas_enable_sbus64 = 0;
			if (fas->f_scsi_options > 0x2f8) {
				fas->f_scsi_options = 0x2f8;
			}
#endif
			fas_log(fas, CE_NOTE,
			    "obsolete and unsupported rev 1.0 hm chip");
		}
	} else if (hm_rev == 0x20) {
		fas_log(fas, CE_NOTE, "?rev 2.1 hm chip");
	} else {
		fas_log(fas, CE_NOTE, "?rev %x.%x hm chip",
		    (hm_rev >> 4) & 0xf, hm_rev & 0xf);
	}

	if ((fas->f_scsi_options & SCSI_OPTIONS_SYNC) == 0) {
		fas->f_nosync =	ALL_TARGETS;
	}

	if ((fas->f_scsi_options & SCSI_OPTIONS_WIDE) == 0) {
		fas->f_nowide =	ALL_TARGETS;
	}

	/*
	 * if target<n>-scsi-options property exists, use it;
	 * otherwise use the f_scsi_options
	 */
	for (i = 0; i <	NTARGETS_WIDE; i++) {
		(void) sprintf(prop_str, prop_template,	i);
		fas->f_target_scsi_options[i] =	ddi_prop_get_int(
			DDI_DEV_T_ANY, dip, 0, prop_str, fas->f_scsi_options);
		IPRINTF3("fas target%d_scsi_options=%x, fas scsi_options=%x\n",
			i, fas->f_target_scsi_options[i], fas->f_scsi_options);
		if (fas->f_target_scsi_options[i] != fas->f_scsi_options) {
			fas_log(fas, CE_NOTE, "target%x_scsi_options=0x%x",
			    i, fas->f_target_scsi_options[i]);
		}
		if (((fas->f_target_scsi_options[i] &
		    SCSI_OPTIONS_DR) ==	0) &&
		    (fas->f_target_scsi_options[i] & SCSI_OPTIONS_TAG))	{
			fas->f_target_scsi_options[i] &= ~SCSI_OPTIONS_TAG;
			fas_log(fas, CE_WARN,
			    "Disabled TQ since disconnects are disabled");
		}
	}

	fas->f_scsi_tag_age_limit =
	    ddi_prop_get_int(DDI_DEV_T_ANY, dip, 0, "scsi-tag-age-limit",
		DEFAULT_TAG_AGE_LIMIT);

	fas->f_scsi_reset_delay	= ddi_prop_get_int(DDI_DEV_T_ANY,
	    dip, 0, "scsi-reset-delay",	DEFAULT_RESET_DELAY);

	/*
	 * get iblock cookie and initialize mutexes
	 */
	if (ddi_get_iblock_cookie(dip, (u_int)0, &fas->f_iblock)
	    != DDI_SUCCESS) {
		cmn_err(CE_WARN, "fas_attach: cannot get iblock cookie");
		goto fail;
	}

	sprintf(buf, "fas%d_mutex", instance);
	mutex_init(&fas->f_mutex, buf, MUTEX_DRIVER, fas->f_iblock);

	/*
	 * initialize mutex for	waitQ
	 */
	sprintf(buf, "fas%d_waitQ_mutex", instance);
	mutex_init(&fas->f_waitQ_mutex,	buf, MUTEX_DRIVER,
	    fas->f_iblock);
	mutex_init_done++;

	/*
	 * install interrupt handler
	 */
	if (ddi_add_intr(dip, (u_int)0,
	    (ddi_iblock_cookie_t *)&fas->f_iblock, NULL,
	    fas_intr, (caddr_t)fas)) {
		cmn_err(CE_WARN, "fas: cannot add intr");
		goto fail;
	}
	intr_added++;

	/*
	 * initialize fas chip
	 */
	if (fas_init_chip(fas, id))	{
		cmn_err(CE_WARN, "fas: cannot initialize");
		goto fail;
	}

	/*
	 * create power	management property
	 */
	drv_getparm(TIME, &timestamp);
	(void) ddi_prop_update_int(DDI_DEV_T_NONE, dip,
	    "pm_timestamp", timestamp);
	power =	1;
	(void) ddi_prop_update_int(DDI_DEV_T_NONE, dip,
	    "pm_norm_pwr", power);

	/*
	 * create kmem cache for packets
	 */
	sprintf(buf, "fas%d_cache", instance);
	fas->f_kmem_cache = kmem_cache_create(buf,
		EXTCMD_SIZE, 8,
		fas_kmem_cache_constructor, fas_kmem_cache_destructor,
		NULL);
	if (fas->f_kmem_cache == NULL) {
		cmn_err(CE_WARN, "fas: cannot create kmem_cache");
		goto fail;
	}

	mutex_enter(&fas_global_mutex);
	/*
	 * initialize callback mechanism (immediate callback)
	 */
	if (fas_init_callbacks(fas)) {
		mutex_exit(&fas_global_mutex);
		goto fail;
	}

	/*
	 * at this point, we are not going to fail the attach
	 * so there is no need to undo the rest:
	 *
	 * add this fas	to the list, this makes	debugging easier
	 * and fas_watch() needs it to walk thru all fas's
	 */
	if (fas_head ==	NULL) {
		fas_head = fas;
	} else {
		fas_tail->f_next = fas;
	}
	fas_tail = fas;		/* point to last fas in	list */

	/*
	 * there is one	watchdog handler for all driver	instances.
	 * start the watchdog if it hasn't been	done yet
	 */
	if (fas_scsi_watchdog_tick == 0) {
		fas_scsi_watchdog_tick = ddi_prop_get_int(DDI_DEV_T_ANY,
			dip, 0,	"scsi-watchdog-tick", DEFAULT_WD_TICK);
		if (fas_scsi_watchdog_tick != DEFAULT_WD_TICK) {
			fas_log(fas, CE_NOTE, "scsi-watchdog-tick=%d",
			    fas_scsi_watchdog_tick);
		}
		fas_tick = drv_usectohz((clock_t)
		    fas_scsi_watchdog_tick * 1000000);
		IPRINTF2("fas scsi watchdog tick=%x, fas_tick=%x\n",
		    fas_scsi_watchdog_tick, fas_tick);
		(void) timeout(fas_watch, (caddr_t)0, fas_tick);
	}
	mutex_exit(&fas_global_mutex);

	ddi_report_dev(dip);

	return (DDI_SUCCESS);

fail:
	cmn_err(CE_WARN, "fas%d: cannot attach", instance);
	if (fas) {
		if (fas->f_cmdarea) {
			ddi_iopb_free((caddr_t)fas->f_cmdarea);
		}
		if (tran) {
			scsi_hba_tran_free(tran);
		}
		for (slot = 0; slot < N_SLOTS; slot += NLUNS_PER_TARGET) {
			struct f_slots *active = fas->f_active[slot];
			if (active) {
				kmem_free((caddr_t)active, active->f_size);
			}
		}
		if (mutex_init_done) {
			mutex_destroy(&fas->f_mutex);
			mutex_destroy(&fas->f_waitQ_mutex);
		}
		if (intr_added)	{
			ddi_remove_intr(dip, (u_int)0, fas->f_iblock);
		}
		if (hba_attached) {
			(void) scsi_hba_detach(dip);
		}
		if (fas->f_kmem_cache) {
			kmem_cache_destroy(fas->f_kmem_cache);
		}
		if (fas->f_cmdarea) {
			if (bound_handle) {
				ddi_dma_unbind_handle(fas->f_dmahandle);
			}
			ddi_dma_mem_free(&cmdarea_acc_handle);
		}
		if (fas->f_dmahandle) {
			ddi_dma_free_handle(fas->f_dmahandle);
		}
		ddi_soft_state_free(fas_state, instance);
	}

	ddi_regs_map_free(&regs_acc_handle);
	ddi_regs_map_free(&dmar_acc_handle);

	return (DDI_FAILURE);
}

/*ARGSUSED*/
static int
fas_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	register struct	fas	*fas;
	scsi_hba_tran_t		*tran;

	/* CONSTCOND */
	ASSERT(NO_COMPETING_THREADS);

	switch (cmd) {
	case DDI_DETACH:
		/*
		 * Be sure to call scsi_hba_detach(dip), if
		 * this	is ever	implemented.
		 */
		return (DDI_FAILURE);

	case DDI_SUSPEND:
	case DDI_PM_SUSPEND:
		tran = (scsi_hba_tran_t	*)ddi_get_driver_private(dip);
		if (!tran) {
			return (DDI_FAILURE);
		}
		fas = TRAN2FAS(tran);
		if (!fas) {
			return (DDI_FAILURE);
		}

		/*
		 * When	CPR writes out the state file it uses the fas driver.
		 * To stop the ASSERTs failing we pretend we are not suspended.
		 * There should	be a better way!
		 */
		mutex_enter(FAS_MUTEX(fas));

		if (cmd	!= DDI_SUSPEND)	{
			fas->f_suspended = 1;
		}

		if (fas->f_ncmds) {
			(void) fas_reset_bus(fas);
			(void) fas_dopoll(fas, SHORT_POLL_TIMEOUT);
		}

		/*
		 * disable dma and fas interrupt
		 */
		fas_dma_reg_write(fas, &fas->f_dma->dma_csr, 0);

		mutex_exit(FAS_MUTEX(fas));

		return (DDI_SUCCESS);

	default:
		return (DDI_FAILURE);
	}
	_NOTE(NOT_REACHED)
	/* NOTREACHED */
}


#ifdef FASDEBUG
/*
 * fas register	read/write functions with tracing
 */
static void
fas_reg_tracing(struct fas *fas, int type, int regno, int what)
{
	fas->f_reg_trace[fas->f_reg_trace_index++] = type;
	fas->f_reg_trace[fas->f_reg_trace_index++] = regno;
	fas->f_reg_trace[fas->f_reg_trace_index++] = what;
	fas->f_reg_trace[fas->f_reg_trace_index++] = gethrtime();
	fas->f_reg_trace[fas->f_reg_trace_index] = 0xff;
	if (fas->f_reg_trace_index >= REG_TRACE_BUF_SIZE) {
		fas->f_reg_trace_index = 0;
	}
}

static void
fas_reg_cmd_write(struct fas *fas, u_char cmd)
{
	register volatile struct fasreg	*fasreg	= fas->f_reg;
	int regno = ((int)(&fasreg->fas_cmd) - (int)fasreg);

	fasreg->fas_cmd	= cmd;
	fas->f_last_cmd	= cmd;

	EPRINTF1("issuing cmd %x\n", (u_char)cmd);
	fas_reg_tracing(fas, 0,	regno, cmd);
}

static void
fas_reg_write(struct fas *fas, volatile	u_char *p, u_char what)
{
	int regno = (int)p - (int)(fas->f_reg);

	*p = what;

	EPRINTF2("writing reg%x = %x\n", regno,	what);
	fas_reg_tracing(fas, 1,	regno, what);
}

static u_char
fas_reg_read(struct fas	*fas, volatile u_char *p)
{
	u_char what;
	int regno = (int)p - (int)(fas->f_reg);

	what = *p;

	EPRINTF2("reading reg%x => %x\n", regno, what);
	fas_reg_tracing(fas, 2,	regno, what);

	return (what);
}

/*
 * dma register	access routines
 */
static void
fas_dma_reg_write(struct fas *fas, volatile u_long *p, u_long what)
{
	*p = what;
#ifdef DMA_REG_TRACING
{
	int regno = (int)p - (int)(fas->f_dma);
	EPRINTF2("writing dma reg%x = %x\n", regno, what);
	fas_reg_tracing(fas, 3,	regno, what);
}
#endif
}

static u_long
fas_dma_reg_read(struct	fas *fas, volatile u_long *p)
{
	u_long what = *p;
#ifdef DMA_REG_TRACING
{
	int regno = (int)p - (int)(fas->f_dma);
	EPRINTF2("reading dma reg%x => %x\n", regno, what);
	fas_reg_tracing(fas, 4,	regno, what);
}
#endif
	return (what);
}
#endif

#define	FIFO_EMPTY(fas)	 (fas_reg_read(fas, &fas->f_reg->fas_stat2) & \
		FAS_STAT2_EMPTY)
#define	FIFO_CNT(fas) \
	(fas_reg_read(fas, &fas->f_reg->fas_fifo_flag) & FIFO_CNT_MASK)

#ifdef FASDEBUG
static void
fas_assert_atn(struct fas *fas)
{
	fas_reg_cmd_write(fas, CMD_SET_ATN);
#ifdef FAS_TEST
	if (fas_test_stop > 1)
		debug_enter("asserted atn");
#endif
}
#else
#define	fas_assert_atn(fas)  fas_reg_cmd_write(fas, CMD_SET_ATN)
#endif

/*
 * DMA macros; we use a	shadow copy of the dma_csr to	save unnecessary
 * reads
 */
#define	FAS_DMA_WRITE(fas, count, base,	cmd) { \
	register volatile struct fasreg	*fasreg	= fas->f_reg; \
	register volatile struct dma *dmar = fas->f_dma; \
	ASSERT((fas_dma_reg_read(fas, &dmar->dma_csr) &	DMA_ENDVMA) == 0); \
	SET_FAS_COUNT(fasreg, count); \
	fas_reg_cmd_write(fas, cmd); \
	fas_dma_reg_write(fas, &dmar->dma_count, count); \
	fas->f_dma_csr |= \
	    DMA_WRITE |	DMA_ENDVMA | DMA_DSBL_DRAIN; \
	fas_dma_reg_write(fas, &dmar->dma_addr,	(fas->f_lastdma	= base)); \
	fas_dma_reg_write(fas, &dmar->dma_csr, fas->f_dma_csr);	\
}

#define	FAS_DMA_READ(fas, count, base, dmacount, cmd) {	\
	register volatile struct fasreg	*fasreg	= fas->f_reg; \
	register volatile struct dma *dmar = fas->f_dma; \
	ASSERT((fas_dma_reg_read(fas, &dmar->dma_csr) &	DMA_ENDVMA) == 0); \
	SET_FAS_COUNT(fasreg, count); \
	fas_reg_cmd_write(fas, cmd); \
	fas->f_dma_csr |= \
	    (fas->f_dma_csr &	~DMA_WRITE) | DMA_ENDVMA | DMA_DSBL_DRAIN; \
	fas_dma_reg_write(fas, &dmar->dma_count, dmacount); \
	fas_dma_reg_write(fas, &dmar->dma_addr,	(fas->f_lastdma	= base)); \
	fas_dma_reg_write(fas, &dmar->dma_csr, fas->f_dma_csr);	\
}

static void
FAS_FLUSH_DMA(struct fas *fas)
{
	fas_dma_reg_write(fas, &fas->f_dma->dma_csr, DMA_RESET);
	fas->f_dma_csr |= (DMA_INTEN|DMA_TWO_CYCLE|DMA_DSBL_PARITY|
		DMA_DSBL_DRAIN);
	fas->f_dma_csr &= ~(DMA_ENDVMA | DMA_WRITE);
	fas_dma_reg_write(fas, &fas->f_dma->dma_csr, 0);
	fas_dma_reg_write(fas, &fas->f_dma->dma_csr, fas->f_dma_csr);
}

/*
 * FAS_FLUSH_DMA_HARD checks on	REQPEND	before taking away the reset
 */
static void
FAS_FLUSH_DMA_HARD(struct fas *fas)
{
	fas_dma_reg_write(fas, &fas->f_dma->dma_csr, DMA_RESET);
	fas->f_dma_csr |= (DMA_INTEN|DMA_TWO_CYCLE|DMA_DSBL_PARITY|
		DMA_DSBL_DRAIN);
	fas->f_dma_csr &= ~(DMA_ENDVMA | DMA_WRITE);
	while (fas_dma_reg_read(fas, &fas->f_dma->dma_csr) & DMA_REQPEND);
	fas_dma_reg_write(fas, &fas->f_dma->dma_csr, 0);
	fas_dma_reg_write(fas, &fas->f_dma->dma_csr, fas->f_dma_csr);
}

/*
 * update period, conf3, offset	reg, if	necessary
 */
#define	FAS_SET_PERIOD_OFFSET_CONF3_REGS(fas) \
{ \
	u_char period, offset, conf3; \
	period = fas->f_sync_period[target] & SYNC_PERIOD_MASK;	\
	offset = fas->f_offset[target];	\
	conf3  = fas->f_fasconf3[target]; \
	if ((period != fas->f_period_reg_last) || \
	    (offset != fas->f_offset_reg_last) || \
	    (conf3 != fas->f_fasconf3_reg_last)) { \
		fas->f_period_reg_last = period; \
		fas->f_offset_reg_last = offset; \
		fas->f_fasconf3_reg_last = conf3; \
		fas_reg_write(fas, &fasreg->fas_sync_period, period); \
		fas_reg_write(fas, &fasreg->fas_sync_offset, offset); \
		fas_reg_write(fas, &fasreg->fas_conf3, conf3); \
	} \
}

/*
 * fifo	read/write routines
 * always read the fifo	bytes before reading the interrupt register
 */
static void
fas_read_fifo(struct fas *fas)
{
	int stat = fas->f_stat;
	volatile struct	fasreg	 *fasreg = fas->f_reg;
	int		 i, stat2;

	stat &=	FAS_PHASE_MASK;
	if ((stat != FAS_PHASE_DATA_IN)	&&
	    (stat != FAS_PHASE_DATA_OUT) &&
	    (fas->f_state != ACTS_DATA_DONE)) {
		i = fas_reg_read(fas, &fasreg->fas_fifo_flag) &	FIFO_CNT_MASK;
		EPRINTF2("fas_read_fifo: fifo cnt=%x, stat=%x\n", i, stat);
		ASSERT(i <= FIFOSIZE);

		fas->f_fifolen = 0;
		while (i-- > 0)	{
			fas->f_fifo[fas->f_fifolen++] =	fas_reg_read(fas,
				&fasreg->fas_fifo_data);
			fas->f_fifo[fas->f_fifolen++] =	fas_reg_read(fas,
				&fasreg->fas_fifo_data);
		}
		stat2 =	fas_reg_read(fas, &fasreg->fas_stat2);
		if (stat2 & FAS_STAT2_ISHUTTLE)	{
			/* write pad byte */
			fas_reg_write(fas, &fasreg->fas_fifo_data, 0);
			fas->f_fifo[fas->f_fifolen++] =	fas_reg_read(fas,
				&fasreg->fas_fifo_data);
			/* flush pad byte */
			fas_reg_cmd_write(fas, CMD_FLUSH);
		}
		EPRINTF2("fas_read_fifo: fifo len=%x, stat2=%x\n",
			fas->f_fifolen,	stat);
	}
} /* fas_read_fifo */

static void
fas_write_fifo(struct fas *fas,	u_char *buf, int length, int pad)
{
	int i;
	volatile struct	fasreg	 *fasreg = fas->f_reg;

	EPRINTF1("writing fifo %x bytes\n", length);
	ASSERT(length <= 15);
	fas_reg_cmd_write(fas, CMD_FLUSH);
	for (i = 0; i <	length;	i++) {
		fas_reg_write(fas, &fasreg->fas_fifo_data, buf[i]);
		if (pad) {
			fas_reg_write(fas, &fasreg->fas_fifo_data, 0);
		}
	}
}

/*
 * Hardware and	Software internal reset	routines
 */
static int
fas_init_chip(register struct fas *fas,	u_char initiator_id)
{
	int		i;
	u_char		clock_conv;
	u_char		initial_conf3;
	u_long		ticks;
	static char	*prop_cfreq = "clock-frequency";

	/*
	 * Determine clock frequency of	attached FAS chip.
	 */
	i = ddi_prop_get_int(DDI_DEV_T_ANY, fas->f_dev,	0, prop_cfreq, -1);
	clock_conv = (i	+ FIVE_MEG - 1)	/ FIVE_MEG;
	if (clock_conv != CLOCK_40MHZ) {
		fas_log(fas, CE_WARN, "Bad clock frequency");
		return (-1);
	}

	fas->f_clock_conv = clock_conv;
	fas->f_clock_cycle = CLOCK_PERIOD(i);
	ticks =	FAS_CLOCK_TICK(fas);
	fas->f_stval = FAS_CLOCK_TIMEOUT(ticks,	fas_selection_timeout);

	DPRINTF5("%d mhz, clock_conv %d, clock_cycle %d, ticks %d, stval %d\n",
		i, fas->f_clock_conv, fas->f_clock_cycle,
		ticks, fas->f_stval);
	/*
	 * set up conf registers
	 */
	fas->f_fasconf |= FAS_CONF_PAREN;
	fas->f_fasconf2	= (u_char)(FAS_CONF2_FENABLE | FAS_CONF2_XL32);

	if (initiator_id < NTARGETS) {
		initial_conf3 =	FAS_CONF3_FASTCLK | FAS_CONF3_ODDBYTE_AUTO;
	} else {
		initial_conf3 =	FAS_CONF3_FASTCLK | FAS_CONF3_ODDBYTE_AUTO |
		    FAS_CONF3_IDBIT3;
	}

	for (i = 0; i <	NTARGETS_WIDE; i++) {
		fas->f_fasconf3[i] = initial_conf3;
	}

	/*
	 * Avoid resetting the scsi bus	since this causes a few	seconds
	 * delay per fas in boot and also causes busy conditions in some
	 * tape	devices.
	 */
	fas_internal_reset(fas,	FAS_RESET_SOFTC|FAS_RESET_FAS|FAS_RESET_DMA);

	/*
	 * initialize period and offset	for each target
	 */
	for (i = 0; i <	NTARGETS_WIDE; i++) {
		if (fas->f_target_scsi_options[i] & SCSI_OPTIONS_SYNC) {
			fas->f_offset[i] = fas_default_offset |
				fas->f_req_ack_delay;
		} else {
			fas->f_offset[i] = 0;
		}
		if (fas->f_target_scsi_options[i] & SCSI_OPTIONS_FAST) {
			fas->f_neg_period[i] =
			    (u_char)CONVERT_PERIOD(DEFAULT_FASTSYNC_PERIOD);
		} else {
			fas->f_neg_period[i] =
			    (u_char)CONVERT_PERIOD(DEFAULT_SYNC_PERIOD);
		}
	}
	return (0);
}

/*
 * reset bus, chip, dma, or soft state
 */
static void
fas_internal_reset(register struct fas *fas, int reset_action)
{
	register volatile struct fasreg	*fasreg	= fas->f_reg;
	register volatile struct dma *dmar = fas->f_dma;

	if (reset_action & FAS_RESET_SCSIBUS)	{
		fas_reg_cmd_write(fas, CMD_RESET_SCSI);
		fas_setup_reset_delay(fas);
	}

	FAS_FLUSH_DMA_HARD(fas); /* resets and reinits the dma */

	/*
	 * NOTE: if dma	is aborted while active, indefinite hangs
	 * may occur; it is preferable to stop the target first	before
	 * flushing the	dma
	 */
	if (reset_action & FAS_RESET_DMA) {
		register int burstsizes	= fas->f_dma_attr->dma_attr_burstsizes;
		if (burstsizes & BURST64) {
			IPRINTF("64 byte burstsize\n");
			fas->f_dma_csr |= DMA_BURST64;
		} else if	(burstsizes & BURST32) {
			IPRINTF("32 byte burstsize\n");
			fas->f_dma_csr |= DMA_BURST32;
		} else {
			IPRINTF("16 byte burstsize\n");
		}
#ifdef ETHERNET_HACK
		if (fas_enable_sbus64 &&
		    (ddi_dma_set_sbus64(fas->f_dmahandle, burstsizes) ==
		    DDI_SUCCESS)) {
#else
		if (ddi_dma_set_sbus64(fas->f_dmahandle, burstsizes) ==
		    DDI_SUCCESS) {
#endif
			IPRINTF("enabled 64 bit sbus\n");
			fas->f_dma_csr |= DMA_WIDE_EN;
		}
	}

	if (reset_action & FAS_RESET_FAS) {
		/*
		 * 2 NOPs with DMA are required	here
		 * id_code is unreliable if we don't do	this)
		 */
		register u_char	idcode,	fcode;
		register int dmarev;

		fas_reg_cmd_write(fas, CMD_RESET_FAS);
		fas_reg_cmd_write(fas, CMD_NOP | CMD_DMA);
		fas_reg_cmd_write(fas, CMD_NOP | CMD_DMA);

		/*
		 * Re-load chip	configurations
		 * Only	load registers which are not loaded in fas_startcmd()
		 */
		fas_reg_write(fas, &fasreg->fas_clock_conv,
			(fas->f_clock_conv & CLOCK_MASK));

		fas_reg_write(fas, &fasreg->fas_timeout, fas->f_stval);

		/*
		 * enable default configurations
		 */
		fas->f_idcode =	idcode =
			fas_reg_read(fas, &fasreg->fas_id_code);
		fcode =	(u_char)(idcode	& FAS_FCODE_MASK) >> (u_char)3;
		fas->f_type = FAS366;
		IPRINTF2("Family code %d, revision %d\n",
		    fcode, (idcode & FAS_REV_MASK));
		dmarev = fas_dma_reg_read(fas, &dmar->dma_csr);
		dmarev = (dmarev >> 11) & 0xf;
		IPRINTF1("DMA channel revision %d\n", dmarev);

		fas_reg_write(fas, &fasreg->fas_conf, fas->f_fasconf);
		fas_reg_write(fas, &fasreg->fas_conf2, fas->f_fasconf2);

		fas->f_req_ack_delay = DEFAULT_REQ_ACK_DELAY;

		/*
		 * Just	in case... clear interrupt
		 */
		(void) fas_reg_read(fas, &fasreg->fas_intr);
	}

	if (reset_action & FAS_RESET_SOFTC) {
		fas->f_wdtr_sent = fas->f_sdtr_sent = 0;
		fas->f_wide_known = fas->f_sync_known =	0;
		fas->f_wide_enabled = fas->f_sync_enabled = 0;
		fas->f_omsglen = 0;
		fas->f_cur_msgout[0] = fas->f_last_msgout =
		    fas->f_last_msgin =	INVALID_MSG;
		fas->f_abort_msg_sent =	fas->f_reset_msg_sent =	0;
		fas->f_next_slot = 0;
		fas->f_current_sp = NULL;
		fas->f_fifolen = 0;
		fas->f_fasconf3_reg_last = fas->f_offset_reg_last =
			fas->f_period_reg_last = 0xff;

		/*
		 * By default we assume	embedded devices and save time
		 * checking for	timeouts in fas_watch()	by skipping
		 * the rest of luns
		 * If we're talking to any non-embedded	devices,
		 * we can't cheat and skip over	non-zero luns anymore
		 * in fas_watch() and fas_ustart().
		 */
		fas->f_dslot = NLUNS_PER_TARGET;

		New_state(fas, STATE_FREE);
	}
}


#ifdef FASDEBUG
/*
 * check if ncmds still	reflects the truth
 * count all cmds for this driver instance and compare with ncmds
 */
static void
fas_check_ncmds(struct fas *fas)
{
	int slot = 0;
	u_short	tag, t;
	int n, total = 0;

	do {
		if (fas->f_active[slot]) {
			struct fas_cmd *sp = fas->f_readyf[slot];
			t = fas->f_active[slot]->f_n_slots;
			while (sp != 0)	{
				sp = sp->cmd_forw;
				total++;
			}
			for (n = tag = 0; tag <	t; tag++) {
				if (fas->f_active[slot]->f_slot[tag] !=	0) {
					n++;
					total++;
				}
			}
			ASSERT(n == fas->f_tcmds[slot]);
		}
		slot = NEXTSLOT(slot, fas->f_dslot);
	} while	(slot != 0);

	if (total != fas->f_ncmds) {
		IPRINTF2("fas_check_ncmds: total=%x, ncmds=%x\n",
			total, fas->f_ncmds);
	}
	ASSERT(fas->f_ncmds >= fas->f_ndisc);
}
#else
#define	fas_check_ncmds(fas)
#endif

/*
 * SCSA	Interface functions
 *
 * Visible to the external world via the transport structure.
 *
 * fas_scsi_abort: abort a current cmd or all cmds for a target
 */
/*ARGSUSED*/
static int
fas_scsi_abort(struct scsi_address *ap,	struct scsi_pkt	*pkt)
{
	register struct	fas *fas = ADDR2FAS(ap);
	register int rval;

	IPRINTF2("fas_scsi_abort: target %d.%d\n", ap->a_target, ap->a_lun);

	mutex_enter(FAS_MUTEX(fas));
	ASSERT(!fas->f_suspended);
	rval =	fas_do_scsi_abort(ap, pkt);
	fas_check_waitQ_and_mutex_exit(fas);
	return (rval);
}

/*
 * reset handling: reset bus or	target
 */
/*ARGSUSED*/
static int
fas_scsi_reset(struct scsi_address *ap,	int level)
{
	struct fas *fas	= ADDR2FAS(ap);
	register rval;

	IPRINTF3("fas_scsi_reset: target %d.%d, level %d\n",
		ap->a_target, ap->a_lun, level);

	mutex_enter(FAS_MUTEX(fas));
	ASSERT(!fas->f_suspended);
	rval = fas_do_scsi_reset(ap, level);
	fas_check_waitQ_and_mutex_exit(fas);
	return (rval);
}

/*
 * entry point for reset notification setup, to	register or to cancel.
 */
static int
fas_scsi_reset_notify(struct scsi_address *ap, int flag,
    void (*callback)(caddr_t), caddr_t arg)
{
	struct fas	*fas = ADDR2FAS(ap);

	return (scsi_hba_reset_notify_setup(ap,	flag, callback,	arg,
		&fas->f_mutex, &fas->f_reset_notify_listf));
}

/*
 * capability interface
 */
/*ARGSUSED*/
static int
fas_scsi_getcap(struct scsi_address *ap, char *cap, int	whom)
{
	struct fas *fas	= ADDR2FAS(ap);
	DPRINTF3("fas_scsi_getcap: tgt=%x, cap=%s, whom=%x\n",
		ap->a_target, cap, whom);
	return (fas_commoncap(ap, cap, 0, whom,	0));
}

/*ARGSUSED*/
static int
fas_scsi_setcap(struct scsi_address *ap, char *cap, int	value, int whom)
{
	struct fas *fas	= ADDR2FAS(ap);
	IPRINTF4("fas_scsi_setcap: tgt=%x, cap=%s, value=%x, whom=%x\n",
		ap->a_target, cap, value, whom);
	return (fas_commoncap(ap, cap, value, whom, 1));
}

/*
 * pkt and dma allocation and deallocation
 */
/*ARGSUSED*/
static void
fas_scsi_dmafree(struct	scsi_address *ap, struct scsi_pkt *pkt)
{
	register struct	fas_cmd	*cmd = PKT2CMD(pkt);

	TRACE_0(TR_FAC_SCSI, TR_FAS_SCSI_IMPL_DMAFREE_START,
	    "fas_scsi_dmafree_start");

	if (cmd->cmd_flags & CFLAG_DMAVALID) {
		/*
		 * Free	the mapping.
		 */
		ddi_dma_unbind_handle(cmd->cmd_dmahandle);
		cmd->cmd_flags ^= CFLAG_DMAVALID;
	}
	TRACE_0(TR_FAC_SCSI, TR_FAS_SCSI_IMPL_DMAFREE_END,
	    "fas_scsi_dmafree_end");
}

/*ARGSUSED*/
static void
fas_scsi_sync_pkt(struct scsi_address *ap, struct scsi_pkt *pkt)
{
	register struct	fas_cmd	*sp = PKT2CMD(pkt);

	if (sp->cmd_flags & CFLAG_DMAVALID) {
		if (ddi_dma_sync(sp->cmd_dmahandle, 0, 0,
		    (sp->cmd_flags & CFLAG_DMASEND) ?
		    DDI_DMA_SYNC_FORDEV	: DDI_DMA_SYNC_FORCPU) !=
		    DDI_SUCCESS) {
			fas_log(ADDR2FAS(ap), CE_WARN,
			    "sync of pkt (%x) failed", pkt);
		}
	}
}

/*
 * initialize pkt and allocate DVMA resources
 */
static struct scsi_pkt *
fas_scsi_init_pkt(struct scsi_address *ap, struct scsi_pkt *pkt,
	struct buf *bp,	int cmdlen, int	statuslen, int tgtlen,
	int flags, int (*callback)(), caddr_t arg)
{
	int kf;
	register int failure = 1;
	register struct	fas_cmd	*cmd;
	register struct	fas *fas = ADDR2FAS(ap);
	struct fas_cmd *new_cmd;

	ASSERT(!fas->f_suspended);
/* #define	FAS_TEST_EXTRN_ALLOC */
#ifdef FAS_TEST_EXTRN_ALLOC
	cmdlen *= 4; statuslen *= 4; tgtlen *= 4;
#endif
	/*
	 * if no pkt was passed	then allocate a	pkt first
	 */
	if (pkt	== NULL) {
		TRACE_0(TR_FAC_SCSI, TR_FAS_SCSI_IMPL_PKTALLOC_START,
		    "fas_scsi_impl_pktalloc_start");

		kf = (callback == SLEEP_FUNC)? KM_SLEEP: KM_NOSLEEP;

		/*
		 * only	one size of pkt	(with arq).
		 */
		cmd = kmem_cache_alloc(fas->f_kmem_cache, kf);

		if (cmd) {
			register int *p;

			pkt = (struct scsi_pkt *)((u_char *)cmd	+
			    sizeof (struct fas_cmd));
			cmd->cmd_pkt		= pkt;
			pkt->pkt_ha_private	= (opaque_t)cmd;
			pkt->pkt_scbp	= (opaque_t)&cmd->cmd_scb;
			pkt->pkt_cdbp	= (opaque_t)&cmd->cmd_cdb_un;
			pkt->pkt_comp		= NULL;
			pkt->pkt_flags		= 0;
			pkt->pkt_time		= 0;
			pkt->pkt_resid		= 0;
			pkt->pkt_reason		= 0;
			pkt->pkt_address	= *ap;

			/* zero	cdbp and pkt_private */
			p = (int *)(pkt->pkt_cdbp =
				(opaque_t)&cmd->cmd_cdb_un);
			*p++	= 0;
			*p++	= 0;
			*p	= 0;
			p = (int *)(pkt->pkt_private = cmd->cmd_pkt_private);
			*p++	= 0;
			*p	= 0;

			cmd->cmd_cdblen		= (u_char)cmdlen;
			cmd->cmd_scblen		= (u_char)statuslen;
			cmd->cmd_privlen	= (u_char)tgtlen;
			cmd->cmd_cur_win	= 0;
			cmd->cmd_qfull_retries	= 0;
			cmd->cmd_flags		= 0;
			cmd->cmd_slot		=
				(Tgt(cmd) * NLUNS_PER_TARGET) |	Lun(cmd);
			failure	= 0;
#ifdef ODDBYTE_HACK
			cmd->cmd_bp = bp;
#endif
		}
		if (failure || (cmdlen > sizeof	(union scsi_cdb)) ||
		    (tgtlen > PKT_PRIV_LEN) ||
		    (statuslen > EXTCMDS_STATUS_SIZE)) {
			if (failure == 0) {
				/*
				 * if extern alloc fails, all will be
				 * deallocated,	including cmd
				 */
				failure	= fas_pkt_alloc_extern(fas, cmd,
				    cmdlen, tgtlen, statuslen, kf);
			}
			if (failure) {
				/*
				 * nothing to deallocate so just return
				 */
				TRACE_0(TR_FAC_SCSI,
					TR_FAS_SCSI_IMPL_PKTALLOC_END,
					"fas_scsi_impl_pktalloc_end");
				return (NULL);
			}
		}

		new_cmd	= cmd;

		TRACE_0(TR_FAC_SCSI, TR_FAS_SCSI_IMPL_PKTALLOC_END,
			"fas_scsi_impl_pktalloc_end");
	} else {
		cmd = PKT2CMD(pkt);
		new_cmd	= NULL;
	}

	/*
	 * Second step of fas_scsi_init_pkt: allocate a	handle
	 * if this pkt doesn't have one	yet, and bind the buf to
	 * the handle
	 */
	if (bp && bp->b_bcount != 0 &&
		    (cmd->cmd_flags & CFLAG_DMAVALID) == 0) {

		register int cmd_flags,	dma_flags;
		int rval;
		u_int dmacookie_count;

		TRACE_0(TR_FAC_SCSI, TR_SCSI_IMPL_DMAGET_START,
		    "fas_scsi_impl_dmaget_start");

		cmd_flags = cmd->cmd_flags;

		if (bp->b_flags	& B_READ) {
			cmd_flags &= ~CFLAG_DMASEND;
			dma_flags = DDI_DMA_READ | DDI_DMA_PARTIAL;
		} else {
			cmd_flags |= CFLAG_DMASEND;
			dma_flags = DDI_DMA_WRITE | DDI_DMA_PARTIAL;
		}
		if (flags & PKT_CONSISTENT) {
			cmd_flags |= CFLAG_CMDIOPB;
			dma_flags |= DDI_DMA_CONSISTENT;
		}

		/*
		 * allocate a dma handle if not	yet done
		 */
		if (cmd->cmd_dmahandle == NULL)	{
			if ((rval = ddi_dma_alloc_handle(fas->f_dev,
			    fas->f_dma_attr, callback, arg,
			    &cmd->cmd_dmahandle)) != DDI_SUCCESS) {
				goto dma_failure;
			}
		}

		/*
		 * bind	the handle to the buf
		 */
		rval = ddi_dma_buf_bind_handle(cmd->cmd_dmahandle, bp,
			dma_flags, callback, arg, &cmd->cmd_dmacookie,
			&dmacookie_count);

dma_failure:
		if (rval && rval != DDI_DMA_PARTIAL_MAP) {
			switch (rval) {
			case DDI_DMA_NORESOURCES:
				bioerror(bp, 0);
				break;
			case DDI_DMA_BADATTR:
			case DDI_DMA_NOMAPPING:
				bioerror(bp, EFAULT);
				break;
			case DDI_DMA_TOOBIG:
			default:
				bioerror(bp, EINVAL);
				break;
			}
			cmd->cmd_flags = cmd_flags & ~CFLAG_DMAVALID;
			if (new_cmd) {
				fas_scsi_destroy_pkt(ap, pkt);
			}
			TRACE_0(TR_FAC_SCSI, TR_SCSI_IMPL_DMAGET_END,
				"fas_scsi_impl_dmaget_end");
			return ((struct	scsi_pkt *)NULL);
		}
		ASSERT(dmacookie_count == 1);
		cmd->cmd_dmacount = bp->b_bcount;
		cmd->cmd_flags = cmd_flags | CFLAG_DMAVALID;

		ASSERT(cmd->cmd_dmahandle != NULL);
		TRACE_0(TR_FAC_SCSI, TR_SCSI_IMPL_DMAGET_END,
		    "fas_scsi_impl_dmaget_end");
	}

	return (pkt);
}

/*
 * unbind dma resources	and deallocate the pkt
 */
static void
fas_scsi_destroy_pkt(struct scsi_address *ap, struct scsi_pkt *pkt)
{
	register struct	fas_cmd	*sp = PKT2CMD(pkt);
	struct fas *fas	= ADDR2FAS(ap);

	/*
	 * fas_scsi_impl_dmafree inline	to speed things	up
	 */
	TRACE_0(TR_FAC_SCSI, TR_FAS_SCSI_IMPL_DMAFREE_START,
	    "fas_scsi_impl_dmafree_start");

	if (sp->cmd_flags & CFLAG_DMAVALID) {
		/*
		 * Free	the mapping.
		 */
		ddi_dma_unbind_handle(sp->cmd_dmahandle);
		sp->cmd_flags ^= CFLAG_DMAVALID;
	}

	TRACE_0(TR_FAC_SCSI, TR_FAS_SCSI_IMPL_DMAFREE_END,
	    "fas_scsi_impl_dmafree_end");

	TRACE_0(TR_FAC_SCSI, TR_FAS_SCSI_IMPL_PKTFREE_START,
	    "fas_scsi_impl_pktfree_start");

	if ((sp->cmd_flags &
	    (CFLAG_FREE	| CFLAG_CDBEXTERN | CFLAG_PRIVEXTERN |
	    CFLAG_SCBEXTERN)) == 0) {
		sp->cmd_flags =	CFLAG_FREE;
		kmem_cache_free(fas->f_kmem_cache, (void *)sp);
	} else {
		fas_pkt_destroy_extern(fas, sp);
	}

	TRACE_0(TR_FAC_SCSI, TR_FAS_SCSI_IMPL_PKTFREE_END,
	    "fas_scsi_impl_pktfree_end");
}

/*
 * allocate and	deallocate external pkt	space (ie. not part of fas_cmd)	for
 * non-standard	length cdb, pkt_private, status	areas
 * if allocation fails,	then deallocate	all external space and the pkt
 */
/* ARGSUSED */
static int
fas_pkt_alloc_extern(struct fas	*fas, struct fas_cmd *sp,
    int	cmdlen,	int tgtlen, int	statuslen, int kf)
{
	register caddr_t cdbp, scbp, tgt;
	int failure = 0;

	tgt = cdbp = scbp = NULL;
	if (cmdlen > sizeof (union scsi_cdb)) {
		if ((cdbp = kmem_zalloc((size_t)cmdlen,	kf)) ==	NULL) {
			failure++;
		} else {
			sp->cmd_pkt->pkt_cdbp =	(opaque_t)cdbp;
			sp->cmd_flags |= CFLAG_CDBEXTERN;
		}
	}
	if (tgtlen > PKT_PRIV_LEN) {
		if ((tgt = kmem_zalloc(tgtlen, kf)) == NULL) {
			failure++;
		} else {
			sp->cmd_flags |= CFLAG_PRIVEXTERN;
			sp->cmd_pkt->pkt_private = tgt;
		}
	}
	if (statuslen >	EXTCMDS_STATUS_SIZE) {
		if ((scbp = kmem_zalloc((size_t)statuslen, kf))	== NULL) {
			failure++;
		} else {
			sp->cmd_flags |= CFLAG_SCBEXTERN;
			sp->cmd_pkt->pkt_scbp =	(opaque_t)scbp;
		}
	}
	if (failure) {
		fas_pkt_destroy_extern(fas, sp);
	}
	return (failure);
}

/*
 * deallocate external pkt space and deallocate	the pkt
 */
static void
fas_pkt_destroy_extern(struct fas *fas,	struct fas_cmd *sp)
{
	if (sp->cmd_flags & CFLAG_FREE)	{
		cmn_err(CE_PANIC,
		    "fas_scsi_impl_pktfree: freeing free packet");
		_NOTE(NOT_REACHED)
		/* NOTREACHED */
	}
	if (sp->cmd_flags & CFLAG_CDBEXTERN) {
		kmem_free((caddr_t)sp->cmd_pkt->pkt_cdbp,
		    (size_t)sp->cmd_cdblen);
	}
	if (sp->cmd_flags & CFLAG_SCBEXTERN) {
		kmem_free((caddr_t)sp->cmd_pkt->pkt_scbp,
		    (size_t)sp->cmd_scblen);
	}
	if (sp->cmd_flags & CFLAG_PRIVEXTERN) {
		kmem_free((caddr_t)sp->cmd_pkt->pkt_private,
		    (size_t)sp->cmd_privlen);
	}
	sp->cmd_flags =	CFLAG_FREE;
	kmem_cache_free(fas->f_kmem_cache, (void *)sp);
}

/*
 * kmem	cache constructor and destructor:
 * When	constructing, we bzero the cmd (not really necessary), we can't
 * allocate the	handle because we don't	have the fas pointer here
 *    XXX - selective zeroing for constructor?
 * When	destructing, just free the dma handle
 */
static void
fas_kmem_cache_constructor(void	*buf, size_t size)
{
	struct fas_cmd *cmd = buf;
	bzero((caddr_t)cmd, size);
}

/* ARGSUSED */
static void
fas_kmem_cache_destructor(void *buf, size_t size)
{
	struct fas_cmd *cmd = buf;
	if (cmd->cmd_dmahandle)	{
		ddi_dma_free_handle(&cmd->cmd_dmahandle);
	}
}

/*
 * fas_scsi_start - Accept commands for	transport
 */
static int
fas_scsi_start(struct scsi_address *ap,	register struct	scsi_pkt *pkt)
{
	register struct	fas_cmd	*sp = PKT2CMD(pkt);
	register struct	fas *fas = ADDR2FAS(ap);
	register int rval;

	TRACE_0(TR_FAC_SCSI, TR_FAS_START_START, "fas_scsi_start_start");
	ASSERT(!fas->f_suspended);

	/*
	 * prepare packet before taking	the mutex
	 */
	rval = fas_prepare_pkt(fas, sp);
	if (rval != TRAN_ACCEPT) {
		TRACE_0(TR_FAC_SCSI, TR_FAS_START_PREPARE_PKT_END,
		    "fas_scsi_start_end (prepare_pkt)");
		return (rval);
	}

	/*
	 * fas mutex can be held for a long time; therefore, if	mutex is
	 * held, we queue the packet in	a waitQ; we now	need to	check
	 * the waitQ on	every mutex_exit(FAS_MUTEX(fas));
	 * don't put NOINTR cmds including proxy cmds in waitQ!	These
	 * cmds	are handled by fas_runpoll()
	 * if the waitQ	is non-empty, queue the	pkt anyway to preserve
	 * order
	 */
	if (sp->cmd_pkt_flags &	FLAG_NOINTR == 0) {
		mutex_enter(&fas->f_waitQ_mutex);
		if (fas->f_waitf || (mutex_tryenter(FAS_MUTEX(fas)) == 0)) {
			if (fas->f_waitf == NULL) {
				fas->f_waitb = fas->f_waitf = sp;
				sp->cmd_forw = NULL;
			} else {
				struct fas_cmd *dp = fas->f_waitb;
				dp->cmd_forw = fas->f_waitb = sp;
				sp->cmd_forw = NULL;
			}

			/*
			 * check again the FAS_MUTEX
			 *  XXX - is this still necessary?
			 */
			if (mutex_tryenter(FAS_MUTEX(fas))) {
				fas_empty_waitQ(fas);
				mutex_exit(FAS_MUTEX(fas));
			}
			mutex_exit(&fas->f_waitQ_mutex);
			goto done;
		}
		mutex_exit(&fas->f_waitQ_mutex);
	} else {
		mutex_enter(FAS_MUTEX(fas));
	}

	rval = fas_accept_pkt(fas, sp, TRAN_BUSY_OK);
	FAS_CHECK_WAITQ_AND_FAS_MUTEX_EXIT(fas);
	FAS_EMPTY_CALLBACKQ(fas);
done:
	TRACE_1(TR_FAC_SCSI, TR_FAS_START_END, "fas_scsi_start_end: ncmds %x",
		fas->f_ncmds);
	return (rval);
}

/*
 * prepare the pkt:
 * the pkt may have been resubmitted or	just reused so
 * initialize some fields, reset the dma window, and do	some checks
 */
static int
fas_prepare_pkt(register struct	fas *fas, register struct fas_cmd *sp)
{
	register struct	scsi_pkt *pkt =	CMD2PKT(sp);
	register u_char	cdbsize;

	/*
	 * Reinitialize	some fields that need it; the packet may
	 * have	been resubmitted
	 */
	pkt->pkt_reason	= CMD_CMPLT;
	pkt->pkt_state	= 0;
	pkt->pkt_statistics = 0;
	pkt->pkt_resid	= 0;
	sp->cmd_age	= 0;
	sp->cmd_pkt_flags = pkt->pkt_flags;

	/*
	 * Copy	the cdb	pointer	to the pkt wrapper area	as we
	 * might modify	this pointer. Zero status byte
	 */
	sp->cmd_cdbp = pkt->pkt_cdbp;
	*(pkt->pkt_scbp) = 0;

	if (sp->cmd_flags & CFLAG_DMAVALID) {
		pkt->pkt_resid	= sp->cmd_dmacount;

		/*
		 * if the pkt was resubmitted then the
		 * windows may be at the wrong number
		 */
		if (sp->cmd_cur_win) {
			sp->cmd_cur_win	= 0;
			if (fas_set_new_window(fas, sp)) {
				IPRINTF("cannot reset window\n");
				return (TRAN_BADPKT);
			}
		}
		sp->cmd_saved_cur_addr =
		    sp->cmd_cur_addr = sp->cmd_dmacookie.dmac_address;

		/*
		 * the common case is just one window, we worry
		 * about multiple windows when we run out of the
		 * current window
		 */
		sp->cmd_nwin = sp->cmd_saved_win = 0;
		sp->cmd_data_count = sp->cmd_saved_data_count =	0;

		/*
		 * consistent packets need to be sync'ed first
		 * (only for data going out)
		 */
		if (sp->cmd_flags & (CFLAG_CMDIOPB | CFLAG_DMASEND)) {
			(void) ddi_dma_sync(sp->cmd_dmahandle,	0, (u_int) -1,
			    DDI_DMA_SYNC_FORDEV);
		}
	}

	/*
	 * Lookup the cdb size.	 If can't figure out how big it	is
	 * (e.g. 6, 10,	or 12 bytes), use the cdb buffer size and punt.
	 * If the cdb buffer is	bigger than 12-bytes, reject the pkt.
	 */
	if ((cdbsize =
	    scsi_cdb_size[CDB_GROUPID((char)*sp->cmd_pkt->pkt_cdbp)]) == 0) {
		if ((cdbsize = sp->cmd_cdblen) > 12) {
			IPRINTF1("cdb too big (%x)!\n",	sp->cmd_cdblen);
			return (TRAN_BADPKT);
		}
	}
	sp->cmd_actual_cdblen =	cdbsize;

#ifdef FASDEBUG
	if (NOTAG(Tgt(sp)) && (pkt->pkt_flags &	FLAG_TAGMASK)) {
		IPRINTF2("tagged packet for non-tagged target %d.%d\n",
		    Tgt(sp), Lun(sp));
		TRACE_0(TR_FAC_SCSI, TR_FAS_PREPARE_PKT_TRAN_BADPKT_END,
		    "fas_prepare_pkt_end (tran_badpkt)");
		return (TRAN_BADPKT);
	}

	/*
	 * the SCSA spec states	that it	is an error to have no
	 * completion function when FLAG_NOINTR	is not set
	 */
	if ((pkt->pkt_comp == NULL) &&
	    ((pkt->pkt_flags & FLAG_NOINTR) == 0)) {
		IPRINTF("intr packet with pkt_comp == 0\n");
		TRACE_0(TR_FAC_SCSI, TR_FAS_PREPARE_PKT_TRAN_BADPKT_END,
		    "fas_prepare_pkt_end (tran_badpkt)");
		return (TRAN_BADPKT);
	}
#endif /* FASDEBUG */

	if ((fas->f_target_scsi_options[Tgt(sp)] & SCSI_OPTIONS_DR) == 0) {
		/*
		 * no need to reset tag	bits since tag queueing	will
		 * not be enabled if disconnects are disabled
		 */
		sp->cmd_pkt_flags |= FLAG_NODISCON;
	}

	sp->cmd_flags =	(sp->cmd_flags & ~CFLAG_TRANFLAG) |
		CFLAG_PREPARED | CFLAG_IN_TRANSPORT;

	TRACE_0(TR_FAC_SCSI, TR_FAS_PREPARE_PKT_TRAN_ACCEPT_END,
	    "fas_prepare_pkt_end (tran_accept)");
	return (TRAN_ACCEPT);
}

/*
 * emptying the	waitQ just before releasing FAS_MUTEX is a bit
 * tricky; if we release the waitQ mutex and then the FAS_MUTEX,
 * another thread could	queue a	cmd in the waitQ, just before
 * the FAS_MUTEX is released. This cmd is then stuck in	the waitQ unless
 * another cmd comes in	or fas_intr() or fas_watch() checks the	waitQ.
 * Therefore, by releasing the FAS_MUTEX before	releasing the waitQ mutex,
 * we prevent fas_scsi_start() filling the waitQ
 *
 * By setting NO_TRAN_BUSY, we force fas_accept_pkt() to queue up
 * the waitQ pkts in the readyQ.
 * If a	QFull condition	occurs,	the target driver may set its throttle
 * too high because of the requests queued up in the readyQ but	this
 * is not a big	problem. The throttle should be	periodically reset anyway.
 */
static void
fas_empty_waitQ(struct fas *fas)
{
	register struct	fas_cmd	*sp;
	register int rval;

	ASSERT(mutex_owned(&fas->f_waitQ_mutex));
	TRACE_0(TR_FAC_SCSI, TR_FAS_EMPTY_WAITQ_START,
	    "fas_empty_waitQ_start");

	while (fas->f_waitf) {
		sp = fas->f_waitf;
		fas->f_waitf = sp->cmd_forw;
		if (fas->f_waitb == sp)	{
			fas->f_waitb = NULL;
		}
		mutex_exit(&fas->f_waitQ_mutex);
		rval = fas_accept_pkt(fas, sp, NO_TRAN_BUSY);

		/*
		 * If the  packet was rejected for other reasons then
		 * complete it here
		 */
		if (rval != TRAN_ACCEPT) {
			ASSERT(rval != TRAN_BUSY);
			fas_set_pkt_reason(fas,	sp, CMD_TRAN_ERR, 0);
			if (sp->cmd_pkt->pkt_comp) {
				sp->cmd_flags |= CFLAG_FINISHED;
				fas_call_pkt_comp(fas, sp);
			}
			mutex_enter(&fas->f_waitQ_mutex);
			continue;
		}
		mutex_enter(&fas->f_waitQ_mutex);
	}
	TRACE_0(TR_FAC_SCSI, TR_FAS_EMPTY_WAITQ_END,
	    "fas_empty_waitQ_end");
}

/*
 * function wrapper for	two frequently used macros. for	the non-critical
 * path	we use the function
 */
static void
fas_check_waitQ_and_mutex_exit(struct fas *fas)
{
	_NOTE(LOCK_RELEASED_AS_SIDE_EFFECT(fas->f_mutex))
	FAS_CHECK_WAITQ_AND_FAS_MUTEX_EXIT(fas);
	FAS_EMPTY_CALLBACKQ(fas);
}

/*
 * fas_accept_pkt():
 * the flag argument is	to force fas_accept_pkt	to accept the pkt;
 * the caller cannot take the pkt back and it has to be	queued up in
 * the readyQ
 */
static int
fas_accept_pkt(register	struct fas *fas, register struct fas_cmd *sp, int flag)
{
	register short slot = sp->cmd_slot;
	int rval = TRAN_ACCEPT;

	TRACE_0(TR_FAC_SCSI, TR__FAS_START_START, "fas_accept_pkt_start");
	ASSERT(mutex_owned(FAS_MUTEX(fas)));
	ASSERT(fas->f_ncmds >= 0 && fas->f_ndisc >= 0);
	ASSERT(fas->f_ncmds >= fas->f_ndisc);
	ASSERT(fas->f_tcmds[slot] >= 0);

	/*
	 * prepare packet for transport	if this	hasn't been done yet and
	 * do some checks
	 */
	if ((sp->cmd_flags & CFLAG_PREPARED) ==	0) {
		rval = fas_prepare_pkt(fas, sp);
		if (rval != TRAN_ACCEPT) {
			IPRINTF1("prepare pkt failed, slot=%x\n", slot);
			sp->cmd_flags &= ~CFLAG_TRANFLAG;
			goto done;
		}
	}

	if (Lun(sp)) {
		EPRINTF("fas_accept_pkt: switching target and lun slot scan\n");
		fas->f_dslot = 1;

		if (fas->f_active[slot]	== NULL) {
			(void) fas_alloc_active_slots(fas, slot, KM_NOSLEEP);
		}
		if ((fas->f_active[slot] == NULL) ||
		    (NOTAG(Tgt(sp)) && (sp->cmd_pkt_flags & FLAG_TAGMASK))) {
			IPRINTF("fatal error on non-zero lun pkt\n");
			return (TRAN_FATAL_ERROR);
		}
	}

	/*
	 * we accepted the command; increment the count
	 * (we may still reject	later if TRAN_BUSY_OK)
	 */
	fas_check_ncmds(fas);
	fas->f_ncmds++;

	/*
	 * if it is a nointr packet, start it now
	 * (NO_INTR pkts are not queued	in the waitQ)
	 */
	if (sp->cmd_pkt_flags &	FLAG_NOINTR) {
		EPRINTF("starting a nointr cmd\n");
		fas_runpoll(fas, slot, sp);
		sp->cmd_flags &= ~CFLAG_TRANFLAG;
		goto done;
	}

	/*
	 * reset the throttle if we were draining
	 */
	if ((fas->f_tcmds[slot]	== 0) &&
	    (fas->f_throttle[slot] == DRAIN_THROTTLE)) {
		DPRINTF("reset throttle\n");
		ASSERT(fas->f_reset_delay[Tgt(sp)] == 0);
		fas_full_throttle(fas, slot);
	}

	/*
	 * accept the command:
	 * If no readyQ	and no bus free, and throttle is OK,
	 * run cmd immediately.
	 */
	if ((fas->f_readyf[slot] == NULL) && (fas->f_state == STATE_FREE) &&
	    (fas->f_throttle[slot] > fas->f_tcmds[slot])) {
		ASSERT(fas->f_current_sp == 0);
		(void) fas_startcmd(fas, sp);
	} else {
		/*
		 * If FLAG_HEAD is set, run cmd if target and bus are
		 * available. if first cmd in ready Q is request sense
		 * then	insert after this command, there shouldn't be more
		 * than	one request sense.
		 */
		if (sp->cmd_pkt_flags &	FLAG_HEAD) {
			struct fas_cmd *ssp = fas->f_readyf[slot];
			EPRINTF("que head\n");
			if (ssp	&&
			    *(ssp->cmd_pkt->pkt_cdbp) != SCMD_REQUEST_SENSE) {
				fas_head_of_readyQ(fas,	sp);
			} else if (ssp)	{
				struct fas_cmd *dp = ssp->cmd_forw;
				ssp->cmd_forw =	sp;
				sp->cmd_forw = dp;
				if (fas->f_readyb[slot]	== ssp)	{
					fas->f_readyb[slot] = sp;
				}
			} else {
				fas->f_readyf[slot] = fas->f_readyb[slot] = sp;
				sp->cmd_forw = NULL;
			}

		/*
		 * for tagged targets, check for qfull condition and
		 * return TRAN_BUSY (if	permitted), if throttle	has been
		 * exceeded
		 */
		} else if (TAGGED(Tgt(sp)) &&
			    (fas->f_tcmds[slot]	>= fas->f_throttle[slot]) &&
			    (fas->f_throttle[slot] > HOLD_THROTTLE) &&
			    (flag == TRAN_BUSY_OK)) {
				IPRINTF2(
				    "transport busy, slot=%x, ncmds=%x\n",
				    slot, fas->f_ncmds);
				rval = TRAN_BUSY;
				fas->f_ncmds--;
				sp->cmd_flags &=
				    ~(CFLAG_PREPARED | CFLAG_IN_TRANSPORT);
				goto done;
		/*
		 * append to readyQ or start a new readyQ
		 */
		} else if (fas->f_readyf[slot])	{
			register struct	fas_cmd	*dp = fas->f_readyb[slot];
			ASSERT(dp != 0);
			fas->f_readyb[slot] = sp;
			sp->cmd_forw = NULL;
			dp->cmd_forw = sp;
		} else {
			fas->f_readyf[slot] = fas->f_readyb[slot] = sp;
			sp->cmd_forw = NULL;
		}

		/*
		 * just	in case	that the bus is	free and we haven't
		 * been	able to	restart	for some reason
		 */
		if (fas->f_state == STATE_FREE)	{
			(void) fas_istart(fas);
		}
	}
done:
	fas_check_ncmds(fas);
	ASSERT(mutex_owned(FAS_MUTEX(fas)));
	TRACE_0(TR_FAC_SCSI, TR__FAS_START_END,	"fas_accept_pkt_end");
	return (rval);
}

/*
 * allocate a tag byte and check for tag aging
 */
static char fas_tag_lookup[] =
	{0, MSG_HEAD_QTAG, MSG_ORDERED_QTAG, 0,	MSG_SIMPLE_QTAG};

static int
fas_alloc_tag(register struct fas *fas,	register struct	fas_cmd	*sp)
{
	register struct	f_slots	*tag_slots;
	register int tag;
	register short slot = sp->cmd_slot;

	TRACE_0(TR_FAC_SCSI, TR_FAS_ALLOC_TAG_START, "fas_alloc_tag_start");
	ASSERT(mutex_owned(FAS_MUTEX(fas)));

	tag_slots = fas->f_active[slot];
	ASSERT(tag_slots->f_n_slots == NTAGS);

alloc_tag:
	tag = (fas->f_active[slot]->f_tags)++;
	if (fas->f_active[slot]->f_tags	>= NTAGS) {
		/*
		 * we reserve tag 0 for	non-tagged cmds
		 */
		fas->f_active[slot]->f_tags = 1;
	}
	EPRINTF1("tagged cmd, tag = %d\n", tag);

	/* Validate tag, should	never fail. */
	if (tag_slots->f_slot[tag] == 0) {
		/*
		 * Store assigned tag and tag queue type.
		 * Note, in case of multiple choice, default to	simple queue.
		 */
		ASSERT(tag < NTAGS);
		sp->cmd_tag[1] = (u_char)tag;
		sp->cmd_tag[0] = fas_tag_lookup[((sp->cmd_pkt_flags &
			FLAG_TAGMASK) >> 12)];
		EPRINTF1("tag= %d\n", tag);
		tag_slots->f_slot[tag] =  sp;
		(fas->f_tcmds[slot])++;
		ASSERT(mutex_owned(FAS_MUTEX(fas)));
		TRACE_0(TR_FAC_SCSI, TR_FAS_ALLOC_TAG_END,
		    "fas_alloc_tag_end");
		return (0);

	} else {
		register int age, i;

		/*
		 * Check tag age.  If timeouts enabled and
		 * tag age greater than	1, print warning msg.
		 * If timeouts enabled and tag age greater than
		 * age limit, begin draining tag que to	check for
		 * lost	tag cmd.
		 */
		age = tag_slots->f_slot[tag]->cmd_age++;
		if (age	>= fas->f_scsi_tag_age_limit &&
		    tag_slots->f_slot[tag]->cmd_pkt->pkt_time) {
			IPRINTF2("tag %d in use, age= %d\n", tag, age);
			DPRINTF("draining tag queue\n");
			if (fas->f_reset_delay[Tgt(sp)]	== 0) {
				fas->f_throttle[slot] =	DRAIN_THROTTLE;
			}
		}

		/* If tag in use, scan until a free one	is found. */
		for (i = 1; i <	NTAGS; i++) {
			tag = fas->f_active[slot]->f_tags;
			if (!tag_slots->f_slot[tag]) {
				EPRINTF1("found	free tag %d\n",	tag);
				break;
			}
			if (++(fas->f_active[slot]->f_tags) >= NTAGS) {
			/*
			 * we reserve tag 0 for	non-tagged cmds
			 */
				fas->f_active[slot]->f_tags = 1;
			}
			EPRINTF1("found	in use tag %d\n", tag);
		}

		/*
		 * If no free tags, we're in serious trouble.
		 * the target driver submitted more than 255
		 * requests
		 */
		if (tag_slots->f_slot[tag]) {
			IPRINTF1("slot %x: All tags in use!!!\n", slot);
			goto fail;
		}
		goto alloc_tag;
	}

fail:
	fas_head_of_readyQ(fas,	sp);

	TRACE_0(TR_FAC_SCSI, TR_FAS_ALLOC_TAG_END,
	    "fas_alloc_tag_end");
	return (-1);
}

/*
 * Internal Search Routine.
 *
 * Search for a	command	to start.
 */
static int
fas_istart(struct fas *fas)
{
	TRACE_0(TR_FAC_SCSI, TR_FAS_ISTART_START,
	    "fas_istart_start");
	EPRINTF("fas_istart:\n");

	if (fas->f_state == STATE_FREE && fas->f_ncmds > fas->f_ndisc) {
		(void) fas_ustart(fas);
	}
	TRACE_0(TR_FAC_SCSI, TR_FAS_ISTART_END,
	    "fas_istart_end");
	return (ACTION_RETURN);
}

static int
fas_ustart(register struct fas *fas)
{
	register struct	fas_cmd	*sp;
	register short slot = fas->f_next_slot;
	register short start_slot = slot;
	register short dslot = fas->f_dslot;

	TRACE_0(TR_FAC_SCSI, TR_FAS_USTART_START, "fas_ustart_start");
	EPRINTF1("fas_ustart: start_slot=%x\n",	fas->f_next_slot);
	ASSERT(fas->f_current_sp == NULL);
	ASSERT(dslot !=	0);
	if (dslot == NLUNS_PER_TARGET) {
		ASSERT((slot % NLUNS_PER_TARGET) == 0);
	}

	/*
	 * if readyQ not empty and  we are not draining, then we
	 * can start another cmd
	 */
	do {
		/*
		 * If all cmds drained from tag	Q, back	to full	throttle and
		 * start queueing up new cmds again.
		 */
		if (fas->f_throttle[slot] == DRAIN_THROTTLE &&
		    fas->f_tcmds[slot] == 0) {
			fas_full_throttle(fas, slot);
		}

		if (fas->f_readyf[slot]	&&
		    (fas->f_throttle[slot] > fas->f_tcmds[slot])) {
			sp = fas->f_readyf[slot];
			fas->f_readyf[slot] = sp->cmd_forw;
			if (sp->cmd_forw == NULL) {
				fas->f_readyb[slot] = NULL;
			}
			fas->f_next_slot = NEXTSLOT(slot, dslot);
			ASSERT((sp->cmd_pkt_flags & FLAG_NOINTR) == 0);
			TRACE_0(TR_FAC_SCSI, TR_FAS_USTART_END,
			    "fas_ustart_end");
			return (fas_startcmd(fas, sp));
		} else {
			slot = NEXTSLOT(slot, dslot);
		}
	} while	(slot != start_slot);

	EPRINTF("fas_ustart: no cmds to start\n");
	fas->f_next_slot = NEXTSLOT(slot, dslot);
	TRACE_0(TR_FAC_SCSI, TR_FAS_USTART_NOT_FOUND_END,
	    "fas_ustart_end (not_found)");
	return (FALSE);
}

/*
 * Start a command off
 */
static int
fas_startcmd(register struct fas *fas, register	struct fas_cmd *sp)
{
	register volatile struct fasreg	*fasreg	= fas->f_reg;
	u_short	 nstate;
	register u_char	cmd, target, lun;
	register u_short tshift;
	register caddr_t tp = (caddr_t)fas->f_cmdarea;
	register struct	scsi_pkt *pkt =	CMD2PKT(sp);
	register int slot = sp->cmd_slot;
	register struct	f_slots	*slots = fas->f_active[slot];
	register int i,	cdb_len;

#define	LOAD_CMDP	*(tp++)

	TRACE_0(TR_FAC_SCSI, TR_FAS_STARTCMD_START, "fas_startcmd_start");

	EPRINTF2("fas_startcmd: sp=%x flags=%x\n", sp, sp->cmd_pkt_flags);
	ASSERT((sp->cmd_flags &	CFLAG_FREE) == 0);
	ASSERT((sp->cmd_flags &	CFLAG_COMPLETED) == 0);
	ASSERT(fas->f_current_sp == NULL && fas->f_state == STATE_FREE);
	if ((sp->cmd_pkt_flags & FLAG_NOINTR) == 0) {
		ASSERT(fas->f_throttle[slot] > 0);
	}
	ASSERT(fas->f_reset_delay[Tgt(sp)] == 0);

	target		= Tgt(sp);
	lun		= Lun(sp);

	/*
	 * if a	non-tagged cmd is submitted to an active tagged	target
	 * then	drain before submitting	this cmd; SCSI-2 allows	RQSENSE
	 * to be untagged
	 */
	if (((sp->cmd_pkt_flags	& FLAG_TAGMASK)	== 0) &&
	    TAGGED(target) && fas->f_tcmds[slot] &&
	    ((sp->cmd_flags & CFLAG_CMDPROXY) == 0) &&
	    (*(sp->cmd_pkt->pkt_cdbp) != SCMD_REQUEST_SENSE)) {
		if ((sp->cmd_pkt_flags & FLAG_NOINTR) == 0) {
			register struct	fas_cmd	*dp;

			IPRINTF("untagged cmd, start draining\n");

			if (fas->f_reset_delay[Tgt(sp)]	== 0) {
				fas->f_throttle[slot] =	DRAIN_THROTTLE;
			}
			dp = fas->f_readyf[slot];
			fas->f_readyf[slot] = sp;
			sp->cmd_forw = dp;
			if (fas->f_readyb[slot]	== NULL) {
				fas->f_readyb[slot] = sp;
			}
		}
		return (FALSE);
	}

	/*
	 * The only reason that	this should happen
	 * is if we have a re-selection	attempt	starting.
	 */
	if (INTPENDING(fas)) {
preempt:
		New_state((fas), STATE_FREE);
		TRACE_0(TR_FAC_SCSI, TR_FAS_STARTCMD_PREEMPT_CALL,
		"fas_startcmd_preempt_call");

		fas_head_of_readyQ(fas,	sp);

		ASSERT(fas->f_current_sp == NULL);
		fas_intr_svc(fas);

		TRACE_0(TR_FAC_SCSI, TR_FAS_STARTCMD_RE_SELECTION_END,
		    "fas_startcmd_end (re_selection)");
		return (FALSE);
	}

	/*
	 * allocate a tag; if no tag available then put	request	back
	 * on the ready	queue and return; eventually a cmd returns and we
	 * get going again or we timeout
	 */
	if (TAGGED(target) && (sp->cmd_pkt_flags & FLAG_TAGMASK)) {
		if (fas_alloc_tag(fas, sp)) {
			return (FALSE);
		}
	} else {
		/*
		 * tag slot 0 is reserved for non-tagged cmds
		 * and should be empy because we have drained
		 */
		if ((sp->cmd_flags & CFLAG_CMDPROXY) ==	0) {
			ASSERT(fas->f_active[slot]->f_slot[0] == NULL);
			fas->f_active[slot]->f_slot[0] = sp;
			sp->cmd_tag[1] = 0;
			(fas->f_tcmds[slot])++;

			/*
			 * don't start any other cmd until this	one is finished
			 */
			fas->f_throttle[slot] =	DRAIN_THROTTLE;
		}
	}

	fas->f_current_sp = sp;
	fas->f_omsglen	= 0;
	tshift		= 1<<target;
	fas->f_sdtr_sent = fas->f_wdtr_sent =	0;
	cdb_len		= sp->cmd_actual_cdblen;

	/*
	 * first send identify message,	with or	without	disconnect priv.
	 */
	if (sp->cmd_pkt_flags &	FLAG_NODISCON) {
		LOAD_CMDP = fas->f_last_msgout = MSG_IDENTIFY |	lun;
		ASSERT((sp->cmd_pkt_flags & FLAG_TAGMASK) == 0);
	} else {
		LOAD_CMDP = fas->f_last_msgout = MSG_DR_IDENTIFY | lun;
	}

	/*
	 * normal case,	tagQ and we have negotiated wide and sync
	 * (proxy msg's	don't have tag flag set)
	 */
	if ((sp->cmd_pkt_flags & FLAG_TAGMASK) &&
	    ((fas->f_wide_known	| fas->f_nowide) &
	    (fas->f_sync_known | fas->f_nosync)	& tshift)) {

		EPRINTF("tag cmd\n");
		ASSERT((sp->cmd_pkt_flags & FLAG_NODISCON) == 0);

		fas->f_last_msgout = LOAD_CMDP = sp->cmd_tag[0];
		LOAD_CMDP = sp->cmd_tag[1];

		nstate = STATE_SELECT_NORMAL;
		cmd = CMD_SEL_ATN3 | CMD_DMA;

	/*
	 * is this a proxy message
	 */
	} else if (sp->cmd_flags & CFLAG_CMDPROXY) {

		IPRINTF2("proxy cmd, len=%x, msg=%x\n",
		    sp->cmd_cdb[FAS_PROXY_DATA],
		    sp->cmd_cdb[FAS_PROXY_DATA+1]);
		/*
		 * This	is a proxy command. It will have
		 * a message to	send as	part of	post-selection
		 * (e.g, MSG_ABORT or MSG_DEVICE_RESET)
		 */
		fas->f_omsglen = sp->cmd_cdb[FAS_PROXY_DATA];
		for (i = 0; i <	(u_int)fas->f_omsglen; i++) {
			fas->f_cur_msgout[i] =
			    sp->cmd_cdb[FAS_PROXY_DATA+1+i];
		}
		sp->cmd_cdb[FAS_PROXY_RESULT] =	FALSE;
		cdb_len	= 0;
		cmd = CMD_SEL_STOP | CMD_DMA;
		nstate = STATE_SELECT_N_SENDMSG;

	/*
	 * always negotiate wide first and sync	after wide
	 */
	} else if (((fas->f_wide_known | fas->f_nowide)	& tshift) == 0)	{
		int i =	0;

		/* First the tag message bytes */
		if (sp->cmd_pkt_flags &	FLAG_TAGMASK) {
			fas->f_cur_msgout[i++] = sp->cmd_tag[0];
			fas->f_cur_msgout[i++] = sp->cmd_tag[1];
		}

		/*
		 * Set up to send wide negotiating message.  This is getting
		 * a bit tricky	as we dma out the identify message and
		 * send	the other messages via the fifo	buffer.
		 */
		EPRINTF1("cmd with wdtr	msg, tag=%x\n",	sp->cmd_tag[1]);

		fas_make_wdtr(fas, i, target, FAS_XFER_WIDTH);

		cdb_len	= 0;
		nstate = STATE_SELECT_N_SENDMSG;
		cmd = CMD_SEL_STOP | CMD_DMA;

	/*
	 * negotiate sync xfer rate
	 */
	} else if (((fas->f_sync_known | fas->f_nosync)	& tshift) == 0)	{
		int i =	0;
		/*
		 * Set up to send wide negotiating message.  This is getting
		 * a bit tricky	as we dma out the identify message and
		 * send	the other messages via the fifo	buffer.
		 */
		if (sp->cmd_pkt_flags &	FLAG_TAGMASK) {
			fas->f_cur_msgout[i++] = sp->cmd_tag[0];
			fas->f_cur_msgout[i++] = sp->cmd_tag[1];
		}

		fas_make_sdtr(fas, i, target);

		cdb_len	= 0;
		cmd = CMD_SEL_STOP | CMD_DMA;
		nstate = STATE_SELECT_N_SENDMSG;

	/*
	 * normal cmds,	no negotiations	and not	a proxy	and no TQ
	 */
	} else {

		ASSERT((sp->cmd_pkt_flags & FLAG_TAGMASK) == 0);
		EPRINTF("std. cmd\n");

		nstate = STATE_SELECT_NORMAL;
		cmd = CMD_SEL_ATN | CMD_DMA;
	}

	/*
	 * Now load cdb	(if any)
	 */
	for (i = 0; i <	cdb_len; i++) {
		LOAD_CMDP = sp->cmd_cdbp[i];
	}

	/*
	 * calculate total dma amount:
	 */
	fas->f_lastcount = ((u_long)tp)	- (u_long)(fas->f_cmdarea);

	/*
	 * load	target id and enable bus id encoding and 32 bit	counter
	 */
	fas_reg_write(fas, (u_char *)&fasreg->fas_busid,
	    (target & 0xf) | FAS_BUSID_ENCODID | FAS_BUSID_32BIT_COUNTER);

	FAS_SET_PERIOD_OFFSET_CONF3_REGS(fas);

	fas_reg_cmd_write(fas, CMD_FLUSH);
	FAS_DMA_READ(fas, fas->f_lastcount,
	    (u_long)fas->f_dmacookie.dmac_address, 16, cmd);

	New_state(fas, (int)nstate);

#ifdef FASDEBUG
	if (DDEBUGGING)	{
		fas_dump_cmd(fas, sp);
	}
#endif /* FASDEBUG */

	/*
	 * if timeout == 0, then it has	no effect on the timeout
	 * handling; we	deal with this when an actual timeout occurs.
	 */
	if ((sp->cmd_flags & CFLAG_CMDPROXY) ==	0) {
		ASSERT(fas->f_tcmds[slot] >= 1);
	}
	i = pkt->pkt_time - slots->f_timebase;

	if (i == 0) {
		EPRINTF("dup timeout\n");
		(slots->f_dups)++;
		slots->f_timeout = slots->f_timebase;
	} else if (i > 0) {
		EPRINTF("new timeout\n");
		slots->f_timeout = slots->f_timebase = pkt->pkt_time;
		slots->f_dups =	1;
	}

	fas_check_ncmds(fas);

	TRACE_0(TR_FAC_SCSI, TR_FAS_STARTCMD_END, "fas_startcmd_end");

	return (TRUE);
}

/*
 * Interrupt Entry Point.
 * Poll	interrupts until they go away
 */
static u_int
fas_intr(caddr_t arg)
{
	register struct	fas *fas = (struct fas *)arg;

	TRACE_0(TR_FAC_SCSI, TR_FAS_POLL_SUN4D_START, "fas_intr_sun4d_start");

	do {
		mutex_enter(FAS_MUTEX(fas));

		while (INTPENDING(fas))	{
			fas_intr_svc(fas);
		}

		/*
		 * check and empty the waitQ and the callbackQ
		 */
		FAS_CHECK_WAITQ_AND_FAS_MUTEX_EXIT(fas);
		FAS_EMPTY_CALLBACKQ(fas);

	} while (INTPENDING(fas));

	TRACE_0(TR_FAC_SCSI, TR_FAS_POLL_SUN4D_END, "fas_intr_sun4d_end");
	return (DDI_INTR_CLAIMED);
}

/*
 * General interrupt service routine.
 */
static char *dma_bits	= DMA_BITS;

static void
fas_intr_svc(register struct fas *fas)
{
	static int (*evec[])(struct fas	*fas) =	{
		fas_finish_select,
		fas_reconnect,
		fas_phasemanage,
		fas_finish,
		fas_reset_recovery,
		fas_istart,
		fas_abort_curcmd,
		fas_reset_bus,
		fas_reset_bus,
		fas_handle_selection
	};
	register int action;
	register u_char	intr;
	register volatile struct fasreg	*fasreg	= fas->f_reg;
#ifdef TRACE
	register int i = 0;
#endif /* TRACE	*/

	TRACE_0(TR_FAC_SCSI, TR_FASSVC_START, "fas_intr_svc_start");
	EPRINTF1("fas_intr_svc: state=%x\n", fas->f_state);

	if (fas->f_state == ACTS_RESET)	{
		action = ACTION_FINRST;
		goto start_action;
	}

	/*
	 * A read of FAS interrupt register clears interrupt,
	 * so any other	volatile information needs to be latched
	 * up prior to reading the interrupt register.
	 */
	fas->f_stat = fas_reg_read(fas,	&fasreg->fas_stat);
	fas->f_stat2 = fas_reg_read(fas, &fasreg->fas_stat2);
	fas->f_step = fas_reg_read(fas,	&fasreg->fas_step);

#ifdef FASDEBUG
	if (fas_check_dma_error(fas)) {
		action = ACTION_RESET;
		goto start_action;
	}
#endif

	/*
	 * check for gross error or spurious interrupts
	 */
	if ((fas->f_stat & FAS_STAT_GERR) ||
	    ((fas->f_stat & FAS_STAT_IPEND) == 0)) {
		action = fas_handle_gross_err_spurious_intr(fas);
		goto start_action;
	}

	/*
	 * read	the fifo if there is something there or	still in the
	 * input shuttle
	 */
	if (((fas->f_stat2 & FAS_STAT2_EMPTY) == 0) ||
	    (fas->f_stat2 & FAS_STAT2_ISHUTTLE)) {
		fas_read_fifo(fas);
	}

	/*
	 * now it is finally safe to read the interrupt	register
	 * if we haven't done so yet
	 */
	intr = fas_reg_read(fas, (u_char *)&fasreg->fas_intr);
	fas->f_intr = intr;

	EPRINTF2("fas_intr_svc: intr=%x, stat=%x\n", fas->f_intr, fas->f_stat);
	EPRINTF2("dmacsr=%b\n",	fas->f_dma->dma_csr, dma_bits);

	/*
	 * Based upon the current state	of the host adapter driver
	 * we should be	able to	figure out what	to do with an interrupt.
	 *
	 * The FAS asserts an interrupt	with one or more of 8 possible
	 * bits	set in its interrupt register. These conditions	are
	 * SCSI	bus reset detected, an illegal command fed to the FAS,
	 * one of DISCONNECT, BUS SERVICE, FUNCTION COMPLETE conditions
	 * for the FAS,	a Reselection interrupt, or one	of Selection
	 * or Selection	with Attention.
	 *
	 * Of these possible interrupts, we can	deal with some right
	 * here	and now, irrespective of the current state of the driver.
	 *
	 * take	care of	the most likely	interrupts first and call the action
	 * immediately
	 */
	if ((intr & (FAS_INT_RESET|FAS_INT_ILLEGAL|FAS_INT_SEL|FAS_INT_SELATN|
	    FAS_INT_RESEL)) == 0) {
		/*
		 * The rest of the reasons for an interrupt can
		 * be handled based purely on the state	that the driver
		 * is currently	in now.
		 */
		if (fas->f_state & STATE_SELECTING) {
			action = fas_finish_select(fas);

		} else if (fas->f_state	& STATE_ITPHASES) {
			action = fas_phasemanage(fas);

		} else {
			fas_log(fas, CE_WARN, "spurious interrupt");
			action = ACTION_RETURN;
		}

	} else if ((intr & FAS_INT_RESEL) && ((intr &
	    (FAS_INT_RESET|FAS_INT_ILLEGAL|FAS_INT_SEL|FAS_INT_SELATN))	== 0)) {

		if (fas->f_state & STATE_SELECTING) {
			action = fas_reselect_preempt(fas);
		} else {
			ASSERT(fas->f_state == STATE_FREE);
			action = fas_reconnect(fas);
		}

	} else if (intr	& FAS_INT_RESET) {
		/*
		 * If we detect	a SCSI reset, we blow away the current
		 * command (if there is	one) and all disconnected commands
		 * because we now don't	know the state of them at all.
		 */
		action = ACTION_FINRST;

	} else if (intr	& FAS_INT_ILLEGAL) {
		/*
		 * This	should not happen. The one situation where
		 * we can get an ILLEGAL COMMAND interrupt is due to
		 * a bug in the	FAS366 during reselection which	we
		 * should be handling in fas_reconnect().
		 */
		IPRINTF1("lastcmd=%x\n", fas->f_reg->fas_cmd);
		fas_printstate(fas, "ILLEGAL bit set");
		action = ACTION_RESET;

	} else if (intr	& (FAS_INT_SEL|FAS_INT_SELATN))	{
		action = ACTION_SELECT;
	}

start_action:
	while (action != ACTION_RETURN)	{
		ASSERT((action >= 0) &&	(action	<= ACTION_SELECT));
		TRACE_3(TR_FAC_SCSI, TR_FASSVC_ACTION_CALL,
			"fas_intr_svc call: state %s, action %d (%d)",
			fas_state_name(fas->f_state), action, i++);
		action = (*evec[action])(fas);
	}
exit:
	TRACE_0(TR_FAC_SCSI, TR_FASSVC_END, "fas_intr_svc_end");
}

/*
 * Manage phase	transitions.
 */
static int
fas_phasemanage(register struct	fas *fas)
{
	register u_short state;
	register int action;
	static int (*pvecs[])(struct fas *fas) = {
		fas_handle_cmd_start,
		fas_handle_cmd_done,
		fas_handle_msg_out_start,
		fas_handle_msg_out_done,
		fas_handle_msg_in_start,
		fas_handle_more_msgin,
		fas_handle_msg_in_done,
		fas_handle_clearing,
		fas_handle_data_start,
		fas_handle_data_done,
		fas_handle_c_cmplt,
		fas_reconnect,
		fas_handle_unknown,
		fas_reset_recovery
	};
#ifdef TRACE
	register int i = 0;
#endif /* TRACE	*/
	TRACE_0(TR_FAC_SCSI, TR_FAS_PHASEMANAGE_START, "fas_phasemanage_start");

	do {
		EPRINTF1("fas_phasemanage: %s\n",
		    fas_state_name(fas->f_state	& STATE_ITPHASES));

		TRACE_2(TR_FAC_SCSI, TR_FAS_PHASEMANAGE_CALL,
			"fas_phasemanage_call: state %s (%d)",
			fas_state_name(fas->f_state), i++);

		state =	fas->f_state;

		if (!(state == STATE_FREE || state > ACTS_ENDVEC)) {
			ASSERT(pvecs[state-1] != NULL);
			action = (*pvecs[state-1]) (fas);
		} else {
			fas_log(fas, CE_WARN, "lost state in phasemanage");
			action = ACTION_ABORT_ALLCMDS;
		}

	} while	(action	== ACTION_PHASEMANAGE);

	TRACE_0(TR_FAC_SCSI, TR_FAS_PHASEMANAGE_END, "fas_phasemanage_end");
	return (action);
}

/*
 * remove a cmd	from active list and if	timeout	flag is	set, then
 * adjust timeouts; if a the same cmd will be resubmitted soon,	don't
 * bother to adjust timeouts (ie. don't	set this flag)
 */
static void
fas_remove_cmd(struct fas *fas,	struct fas_cmd *sp, int	new_timeout_flag)
{
	register int tag = sp->cmd_tag[1];
	register int slot = sp->cmd_slot;
	register struct	f_slots	*tag_slots = fas->f_active[slot];

	ASSERT(sp != NULL);
	EPRINTF4("remove tag %d slot %d for target %d.%d\n",
	    tag, slot, Tgt(sp),	Lun(sp));

	if (sp == tag_slots->f_slot[tag]) {
		tag_slots->f_slot[tag] = NULL;
		fas->f_tcmds[slot]--;
	}
	if (fas->f_current_sp == sp) {
		fas->f_current_sp = NULL;
	}

	ASSERT(sp != fas->f_active[sp->cmd_slot]->f_slot[sp->cmd_tag[1]]);

	if (new_timeout_flag !=	NEW_TIMEOUT) {
		return;
	}

	/*
	 * Figure out what to set tag Q	timeout	for...
	 *
	 * Optimize: If	we have	duplicate's of same timeout
	 * we're using,	then we'll use it again	until we run
	 * out of duplicates.  This should be the normal case
	 * for block and raw I/O.
	 * If no duplicates, we	have to	scan through tag que and
	 * find	the longest timeout value and use it.  This is
	 * going to take a while...
	 */
	if (sp->cmd_pkt->pkt_time == tag_slots->f_timebase) {
		if (--(tag_slots->f_dups) <= 0)	{
			if (fas->f_tcmds[slot])	{
				register struct	fas_cmd	*ssp;
				register u_long	n = 0;
				register u_short t = tag_slots->f_n_slots;
				register u_short i;
				/*
				 * This	crude check assumes we don't do
				 * this	too often which	seems reasonable
				 * for block and raw I/O.
				 */
				for (i = 0; i <	t; i++)	{
					ssp = tag_slots->f_slot[i];
					if (ssp	&&
					    (ssp->cmd_pkt->pkt_time > n)) {
						n = ssp->cmd_pkt->pkt_time;
						tag_slots->f_dups = 1;
					} else if (ssp &&
					    (ssp->cmd_pkt->pkt_time == n)) {
						tag_slots->f_dups++;
					}
				}
				tag_slots->f_timebase =	n;
				EPRINTF1("searching, new_timeout= %d\n", n);
			} else {
				tag_slots->f_dups = 0;
				tag_slots->f_timebase =	0;
			}
		}
	}
	tag_slots->f_timeout = tag_slots->f_timebase;

	ASSERT(fas->f_ncmds >= fas->f_ndisc);
}

/*
 * decrement f_ncmds and f_ndisc for this cmd before completing
 */
static void
fas_decrement_ncmds(struct fas *fas, struct fas_cmd *sp)
{
	ASSERT((sp->cmd_flags &	CFLAG_FREE) == 0);
	if ((sp->cmd_flags & CFLAG_FINISHED) ==	0) {
		fas->f_ncmds--;
		if (sp->cmd_flags & CFLAG_CMDDISC) {
			fas->f_ndisc--;
		}
		sp->cmd_flags |= CFLAG_FINISHED;
		sp->cmd_flags &= ~CFLAG_CMDDISC;
	}
	ASSERT((fas->f_ncmds >=	0) && (fas->f_ndisc >= 0));
	ASSERT(fas->f_ncmds >= fas->f_ndisc);
}

/*
 * Most	commonly called	phase handlers:
 *
 * Finish routines
 */
static int
fas_finish(register struct fas *fas)
{
	register struct	fas_cmd	*sp = fas->f_current_sp;
	register struct	scsi_pkt *pkt =	CMD2PKT(sp);
	register action	= ACTION_SEARCH;
	struct scsi_status *status =
	    (struct  scsi_status *)sp->cmd_pkt->pkt_scbp;

	TRACE_0(TR_FAC_SCSI, TR_FAS_FINISH_START,
	    "fas_finish_start");
	EPRINTF("fas_finish\n");

	/*
	 * immediately enable reselects
	 */
	fas_reg_cmd_write(fas, CMD_EN_RESEL);
	if (status->sts_chk) {
		/*
		 * In the case that we are getting a check condition
		 * clear our knowledge of synchronous capabilities.
		 * This	will unambiguously force a renegotiation
		 * prior to any	possible data transfer (we hope),
		 * including the data transfer for a UNIT ATTENTION
		 * condition generated by somebody powering on and
		 * off a target.
		 */
		fas_force_renegotiation(fas, Tgt(sp));
	}

	/*
	 * we will backoff sync/wide if	there were parity errors
	 */
	if (sp->cmd_pkt->pkt_statistics	& STAT_PERR) {
		fas_sync_wide_backoff(fas, sp, sp->cmd_slot);
	}

	/*
	 * Free	from active list and update counts
	 * We need to clean up this cmd	now, just in case fas_ustart()
	 * hits	a reset	or other fatal transport error
	 */
	fas_check_ncmds(fas);
	fas_remove_cmd(fas, sp,	NEW_TIMEOUT);
	fas_decrement_ncmds(fas, sp);
	fas_check_ncmds(fas);

	/*
	 * go to state free and	try to start a new cmd now
	 */
	New_state(fas, STATE_FREE);

	if ((fas->f_ncmds > fas->f_ndisc) && (*((char *)status)	== 0) &&
	    (INTPENDING(fas) == 0)) {
		if (fas_ustart(fas)) {
			action = ACTION_RETURN;
		}
	}

	/*
	 * if there was	a data xfer then calculate residue and
	 * sync	data for consistent memory xfers
	 */
	if (pkt->pkt_state & STATE_XFERRED_DATA) {
		pkt->pkt_resid = sp->cmd_dmacount - sp->cmd_data_count;
		if (sp->cmd_flags & CFLAG_CMDIOPB) {
			(void) ddi_dma_sync(sp->cmd_dmahandle, 0, (u_int)-1,
			    DDI_DMA_SYNC_FORCPU);
		}
		if (pkt->pkt_resid) {
			IPRINTF3("%d.%d finishes with %d resid\n",
			    Tgt(sp), Lun(sp), pkt->pkt_resid);
		}
	}

	if (sp->cmd_pkt_flags &	FLAG_NOINTR) {
		fas_call_pkt_comp(fas, sp);
		action = ACTION_RETURN;
	} else {
		/*
		 * start an autorequest	sense if there was a check condition.
		 * if arq has not been enabled,	fas_handle_sts_chk will	do
		 * do the callback
		 */
		if (status->sts_chk) {
			fas_handle_sts_chk(fas,	sp);
		} else if ((*((char *)status) &	STATUS_MASK) ==
		    STATUS_QFULL) {
			fas_handle_qfull(fas, sp);
		} else {
			fas_call_pkt_comp(fas, sp);
		}
	}

	TRACE_0(TR_FAC_SCSI, TR_FAS_FINISH_END,	"fas_finish_end");
	return (action);
}

/*
 * Complete the	process	of selecting a target
 */
static int
fas_finish_select(register struct fas *fas)
{
	register volatile struct dma *dmar = fas->f_dma;
	register struct	fas_cmd	*sp = fas->f_current_sp;
	register u_char	intr = fas->f_intr;
	register u_char	step = fas->f_step & FAS_STEP_MASK;

	TRACE_0(TR_FAC_SCSI, TR_FAS_FINISH_SELECT_START,
	    "fas_finish_select_start");
	EPRINTF("fas_finish_select:\n");
	ASSERT(sp != 0);

	/*
	 * Check for DMA gate array errors
	 */
	if ((fas->f_dma_csr = fas_dma_reg_read(fas, &dmar->dma_csr))
	    & DMA_ERRPEND) {
		/*
		 * It would be desirable to set	the ATN* line and attempt to
		 * do the whole	schmear	of INITIATOR DETECTED ERROR here,
		 * but that is too hard	to do at present.
		 */
		fas_log(fas, CE_WARN,
		    "Unrecoverable DMA error during selection");
		fas_set_pkt_reason(fas,	sp, CMD_TRAN_ERR, 0);

		TRACE_0(TR_FAC_SCSI, TR_FAS_FINISH_SELECT_RESET1_END,
		    "fas_finish_select_end (ACTION_RESET1)");
		return (ACTION_RESET);
	}

	/*
	 * Shut	off DMA	gate array
	 */
	FAS_FLUSH_DMA(fas);

	/*
	 * Did something respond to selection?
	 */
	if (intr == (FAS_INT_BUS|FAS_INT_FCMP))	{
		/*
		 * We succesfully selected a target (we	think).
		 * Now we figure out how botched things	are
		 * based upon the kind of selection we were
		 * doing and the state of the step register.
		 */
		switch (step) {
		case FAS_STEP_ARBSEL:
			/*
			 * In this case, we selected the target, but went
			 * neither into	MESSAGE	OUT nor	COMMAND	phase.
			 * However, this isn't a fatal error, so we just
			 * drive on.
			 *
			 * This	might be a good	point to note that we have
			 * a target that appears to not	accomodate
			 * disconnecting,
			 * but it really isn't worth the effort	to distinguish
			 * such	targets	fasecially from	others.
			 */
			/* FALLTHROUGH */

		case FAS_STEP_SENTID:
			/*
			 * In this case, we selected the target	and sent
			 * message byte	and have stopped with ATN* still on.
			 * This	case should only occur if we use the SELECT
			 * AND STOP command.
			 */
			/* FALLTHROUGH */

		case FAS_STEP_NOTCMD:
			/*
			 * In this case, we either didn't transition to	command
			 * phase, or,
			 * if we were using the	SELECT WITH ATN3 command,
			 * we possibly didn't send all message bytes.
			 */
			break;

		case FAS_STEP_PCMD:
			/*
			 * In this case, not all command bytes transferred.
			 */
			/* FALLTHROUGH */

		case FAS_STEP_DONE:
			/*
			 * This	is the usual 'good' completion point.
			 * If we we sent message byte(s), we subtract
			 * off the number of message bytes that	were
			 * ahead of the	command.
			 */
			sp->cmd_pkt->pkt_state |= STATE_SENT_CMD;
			break;

		default:
			fas_log(fas, CE_WARN,
			    "bad sequence step (0x%x) in selection", step);
			TRACE_0(TR_FAC_SCSI, TR_FAS_FINISH_SELECT_RESET3_END,
			    "fas_finish_select_end (ACTION_RESET3)");
			return (ACTION_RESET);
		}

		/*
		 * OR in common	state...
		 */
		sp->cmd_pkt->pkt_state |= (STATE_GOT_BUS|STATE_GOT_TARGET);

		/*
		 * data	pointer	initialization has already been	done
		 */
		New_state(fas, ACTS_UNKNOWN);
		TRACE_0(TR_FAC_SCSI, TR_FAS_FINISH_SELECT_ACTION3_END,
		    "fas_finish_select_end (action3)");
		return (fas_handle_unknown(fas));

	} else if (intr	== FAS_INT_DISCON) {
		/*
		 * make	sure we	negotiate when this target comes
		 * on line later on
		 */
		fas_force_renegotiation(fas, Tgt(sp));

		fas->f_sdtr_sent = fas->f_wdtr_sent = 0;
		sp->cmd_pkt->pkt_state |= STATE_GOT_BUS;

		fas_set_pkt_reason(fas,	sp, CMD_INCOMPLETE, 0);

		TRACE_0(TR_FAC_SCSI, TR_FAS_FINISH_SELECT_FINISH_END,
		    "fas_finish_select_end (ACTION_FINISH)");
		return (ACTION_FINISH);
	} else	{
		fas_printstate(fas, "undetermined selection failure");
		TRACE_0(TR_FAC_SCSI, TR_FAS_FINISH_SELECT_RESET2_END,
		    "fas_finish_select_end (ACTION_RESET2)");
		return (ACTION_RESET);
	}
	_NOTE(NOT_REACHED)
	/* NOTREACHED */
}

/*
 * a selection got preempted by	a reselection; shut down dma
 * and put back	cmd in the ready queue unless NOINTR
 */
static int
fas_reselect_preempt(struct fas	*fas)
{
	/*
	 * A reselection attempt glotzed our selection attempt.
	 * we put request back in the ready queue
	 */
	register struct	fas_cmd	*sp = fas->f_current_sp;

	/*
	 * Shut	off DMA	gate array
	 */
	FAS_FLUSH_DMA(fas);

	New_state(fas, STATE_FREE);

	if ((sp->cmd_flags & CFLAG_CMDPROXY) ==	0) {
		fas_remove_cmd(fas, sp,	NEW_TIMEOUT);
	}

	fas_head_of_readyQ(fas,	sp);

	return (fas_reconnect(fas));
}

/*
 * Handle the reconnection of a	target
 */
static int
fas_reconnect(register struct fas *fas)
{
	register volatile struct fasreg	*fasreg	= fas->f_reg;
	register struct	fas_cmd	*sp = NULL;
	register char target, lun;
	register u_char	tmp;
	u_char slot;
	int msg_accept_issued =	0;
	char *bad_reselect = NULL;

	TRACE_0(TR_FAC_SCSI, TR_FAS_RECONNECT_START,
	    "fas_reconnect_start");
	EPRINTF("fas_reconnect:\n");

	fas_check_ncmds(fas);

	switch (fas->f_state) {
	default:
		/*
		 * normal initial reconnect; we	get another interrupt later
		 * for the tag
		 */
		New_state(fas, ACTS_RESEL);

		/*
		 * Pick	up target id from fifo
		 *
		 * There should	only be	the reselecting	target's id
		 * and an identify message in the fifo.
		 */
		tmp = fas->f_fifo[1];
		fas->f_last_msgin = tmp;
		target = fas->f_fifo[0];
		if (fas->f_fifolen != 2) {
			bad_reselect = "bad reselect bytes";
			break;
		}

		EPRINTF2("fas_reconnect: target=%x, idmsg=%x\n",
			target,	tmp);

		if (fas->f_stat	& FAS_STAT_PERR) {
			break;
		}

		/*
		 * Check sanity	of message.
		 */
		if (!(IS_IDENTIFY_MSG(tmp)) || (tmp & INI_CAN_DISCON)) {
			bad_reselect = "bad identify msg";
			break;
		}

		lun = tmp & (NLUNS_PER_TARGET-1);

		/*
		 * update period, conf3, offset	reg, if	necessary
		 */
		FAS_SET_PERIOD_OFFSET_CONF3_REGS(fas);

		fas_reg_write(fas, (u_char *)&fasreg->fas_busid,
		(target	& 0xf) | FAS_BUSID_ENCODID | FAS_BUSID_32BIT_COUNTER);

		fas->f_resel_slot = slot = (target * NLUNS_PER_TARGET) | lun;

		/*
		 * If tag queueing in use, DMA in tag.
		 * Otherwise, we're ready to go.
		 * if tag 0 slot is non-empty, a non-tagged cmd	is
		 * reconnecting
		 */
		if (TAGGED(target) && fas->f_tcmds[slot] &&
		    (fas->f_active[slot]->f_slot[0] == NULL)) {
			register u_char	*c = (u_char *)fas->f_cmdarea;

			/*
			 * accept the identify msg
			 */
			fas_reg_cmd_write(fas, CMD_MSG_ACPT);

			/*
			 * If we've been doing tagged queueing and this
			 * request doesn't  do it,
			 * maybe it was	disabled for this one.	This is	rather
			 * dangerous as	it blows all pending tagged cmds away.
			 * But if target is confused, then we'll blow up
			 * shortly.
			 */
			*c++ = INVALID_MSG;
			*c   = INVALID_MSG;

			/*
			 * For tagged queuing, we should still be in msgin
			 * phase.
			 * If not, then	either we aren't running tagged
			 * queueing like we thought or the target died.
			 */
			if (INTPENDING(fas) == 0) {
				EPRINTF1("slow reconnect, slot=%x\n", slot);
				TRACE_0(TR_FAC_SCSI,
				    TR_FAS_RECONNECT_RETURN1_END,
				    "fas_reconnect_end (_RETURN1)");
				return (ACTION_RETURN);
			}

			fas->f_stat = fas_reg_read(fas,	&fasreg->fas_stat);
			fas->f_intr = fas_reg_read(fas,	&fasreg->fas_intr);
			if (fas->f_intr	& FAS_INT_RESET) {
				TRACE_0(TR_FAC_SCSI, TR_FAS_RECONNECT_F2_END,
				    "fas_reconnect_end (_F2)");
				return (ACTION_FINRST);
			}

			if ((fas->f_stat & FAS_PHASE_MASK) !=
			    FAS_PHASE_MSG_IN) {
				bad_reselect = "not in msgin phase";
				break;
			}

			if (fas->f_intr	& FAS_INT_DISCON) {
				bad_reselect = "unexpected bus free";
				break;
			}
		} else {
			fas->f_current_sp = sp = fas->f_active[slot]->f_slot[0];
			break;
		}
		/*FALLTHROUGH*/

	case ACTS_RESEL:
		{
			register u_char	*c = (u_char *)fas->f_cmdarea;
			register struct	f_slots	*tag_slots;
			int id,	tag;
			u_int i;

			slot = fas->f_resel_slot;
			target = slot/NLUNS_PER_TARGET;

			if ((fas->f_stat & FAS_PHASE_MASK) !=
			    FAS_PHASE_MSG_IN) {
				IPRINTF1("no tag for slot %x\n", slot);
				if (fas->f_intr	& ~(FAS_INT_BUS	|
				    FAS_INT_FCMP)) {
					New_state(fas, ACTS_UNKNOWN);
					TRACE_0(TR_FAC_SCSI,
					    TR_FAS_RECONNECT_PHASEMANAGE_END,
					    "fas_reconnect_end (_PHASEMANAGE)");
					return (ACTION_PHASEMANAGE);
				} else {
					bad_reselect = "not in msgin phase";
					break;
				}
			}

			FAS_DMA_WRITE(fas, 2,
			    (u_long)fas->f_dmacookie.dmac_address,
			    CMD_TRAN_INFO|CMD_DMA);

			msg_accept_issued = 1;
			fas_reg_cmd_write(fas, CMD_MSG_ACPT);

			for (i = 0; i <	(u_int)	RECONNECT_TAG_RCV_TIMEOUT;
			    i++) {
				/*
				 * timeout is not very accurate	but this
				 * should take no time at all
				 */
				if (INTPENDING(fas)) {
					fas->f_stat = fas_reg_read(fas,
					(u_char	*)&fas->f_reg->fas_stat);
					fas->f_intr = fas_reg_read(fas,
					(u_char	*)&fas->f_reg->fas_intr);
					if (fas->f_intr	& FAS_INT_RESET) {
						TRACE_0(TR_FAC_SCSI,
						    TR_FAS_RECONNECT_F3_END,
						    "fas_reconnect_end (_F3)");
						return (ACTION_FINRST);
					}
					if (fas->f_intr	& FAS_INT_FCMP)	{
						break;
					}
				}
			}

			if (i == (u_int) RECONNECT_TAG_RCV_TIMEOUT) {
				bad_reselect = "timeout on receiving tag msg";
				break;
			}

			FAS_FLUSH_DMA(fas);

			/*
			 * we should really do a sync here but that
			 * hurts performance too much; we'll just hang
			 * around till the tag byte flips
			 * This	is necessary on	sun4d or any system with an
			 * XBox
			 */
			if (*c == INVALID_MSG) {
				EPRINTF(
				    "fas_reconnect: invalid msg, polling\n");
				for (i = 0; i <	1000000; i++) {
					if (*c != INVALID_MSG)
						break;
				}
			}

			if (fas->f_stat	& FAS_STAT_PERR) {
				break;
			}

			if ((fas->f_stat & FAS_STAT_XZERO) == 0	||
			    (id	= *c++)	< MSG_SIMPLE_QTAG ||
			    id > MSG_ORDERED_QTAG) {
				/*
				 * Target agreed to do tagged queueing
				 * and lied!
				 * This	problem	implies	the drive firmware is
				 * broken.
				 */
				bad_reselect = "botched tag";
				break;
			}
			tag = *c;

			/* Set ptr to reconnecting scsi	pkt */
			tag_slots = fas->f_active[slot];
			if ((int)tag_slots != NULL) {
				sp = tag_slots->f_slot[tag];
			} else {
				bad_reselect = "Invalid tag";
				break;
			}

			fas->f_current_sp = sp;
		}
	}

	if (fas->f_stat	& FAS_STAT_PERR) {
		sp = NULL;
		bad_reselect = "Parity error in reconnect msg's";
	}

	if ((sp	== NULL	||
#ifdef FAS_TEST
	    (fas_atest_reconn &	(1<<Tgt(sp))) ||
#endif
	    (sp->cmd_flags & (CFLAG_CMDDISC|CFLAG_CMDPROXY)) ==	0)) {
		/*
		 * this	shouldn't really happen, so it is better
		 * to reset the	bus; some disks	accept the abort
		 * and then still reconnect
		 */
		if (bad_reselect == NULL) {
			bad_reselect = "no command";
		}
#ifdef FAS_TEST
		fas_atest_reconn = 0;
#endif
		goto bad;

	/*
	 *  XXX remove this case or make it an ASSERT
	 */
	} else if (sp->cmd_flags & CFLAG_CMDPROXY) {
		/*
		 * If we got here, we were already attempting to
		 * run a polled	proxy command for this target.
		 * Set ATN and,	copy in	the message, and drive
		 * on (ignoring	any parity error on the	identify).
		 */
		IPRINTF1("fas_reconnect: fielding proxy cmd for %d\n",
		    target);
		fas_assert_atn(fas);
		fas->f_omsglen = sp->cmd_cdb[FAS_PROXY_DATA];
		tmp = 0;
		while (tmp < fas->f_omsglen) {
			fas->f_cur_msgout[tmp] =
			    sp->cmd_cdb[FAS_PROXY_DATA+1+tmp];
			tmp++;
		}
		sp->cmd_cdb[FAS_PROXY_RESULT] =	FALSE;

		/*
		 * pretend that	the disconnected cmd is	still disconnected
		 * (this prevents ndisc	from going negative)
		 */
		fas->f_ndisc++;
		ASSERT((fas->f_ncmds >=	0) && (fas->f_ndisc >= 0));
		ASSERT(fas->f_ncmds >= fas->f_ndisc);
	}

	/*
	 * Accept the last message if we haven't done so
	*/
	if (msg_accept_issued == 0) {
		fas_reg_cmd_write(fas, CMD_MSG_ACPT);
	}

	ASSERT(fas->f_resel_slot == slot);
	ASSERT(fas->f_ndisc > 0);
	fas->f_ndisc--;
	sp->cmd_flags &= ~CFLAG_CMDDISC;
	New_state(fas, ACTS_UNKNOWN);

	/*
	 * A reconnect may imply a restore pointers operation
	 * Note	that some older	disks (Micropolis in Pbox) do not
	 * send	a save data ptr	on disconnect if all data has been
	 * xferred. So,	we cannot restore ptrs yet here.
	 */
	if ((sp->cmd_flags & CFLAG_DMAVALID) &&
	    (sp->cmd_data_count	!= sp->cmd_saved_data_count)) {
		sp->cmd_flags |= CFLAG_RESTORE_PTRS;
	}

	/*
	 * Return to await the FUNCTION	COMPLETE interrupt we
	 * should get out of accepting the IDENTIFY message.
	 */
	EPRINTF2("Reconnecting %d.%d\n", target, slot % NLUNS_PER_TARGET);
	TRACE_0(TR_FAC_SCSI, TR_FAS_RECONNECT_RETURN2_END,
	    "fas_reconnect_end (_RETURN2)");
	return (ACTION_RETURN);

bad:
	if (sp && (fas->f_stat	& FAS_STAT_PERR)) {
		sp->cmd_pkt->pkt_statistics |= STAT_PERR;
	}
	fas_log(fas, CE_WARN, "target %x: failed reselection (%s)",
		target, bad_reselect);

#ifdef FASDEBUG
	fas_printstate(fas, "failed reselection");
#endif
	TRACE_0(TR_FAC_SCSI, TR_FAS_RECONNECT_RESET5_END,
	    "fas_reconnect_end (_RESET5)");
	return (ACTION_RESET);
}

/*
 * handle unknown bus phase
 * we don't know what to expect	so check status	register for current
 * phase
 */
fas_handle_unknown(register struct fas *fas)
{
	TRACE_1(TR_FAC_SCSI, TR_FAS_HANDLE_UNKNOWN_START,
	    "fas_handle_unknown_start: fas %x",	fas);
	EPRINTF("fas_handle_unknown:\n");

	if ((fas->f_intr & FAS_INT_DISCON) == 0) {
		/*
		 * we call actions here	rather than returning to phasemanage
		 * (this is the	most frequently	called action)
		 */
		switch (fas->f_stat & FAS_PHASE_MASK) {
		case FAS_PHASE_DATA_IN:
		case FAS_PHASE_DATA_OUT:
			New_state(fas, ACTS_DATA);
			TRACE_0(TR_FAC_SCSI,
			    TR_FAS_HANDLE_UNKNOWN_PHASE_DATA_END,
			    "fas_handle_unknown_end (phase_data)");
			return (fas_handle_data_start(fas));

		case FAS_PHASE_MSG_OUT:
			New_state(fas, ACTS_MSG_OUT);
			TRACE_0(TR_FAC_SCSI,
			    TR_FAS_HANDLE_UNKNOWN_PHASE_MSG_OUT_END,
			    "fas_handle_unknown_end (phase_msg_out)");
			return (fas_handle_msg_out_start(fas));

		case FAS_PHASE_MSG_IN:
			New_state(fas, ACTS_MSG_IN);
			TRACE_0(TR_FAC_SCSI,
			    TR_FAS_HANDLE_UNKNOWN_PHASE_MSG_IN_END,
			    "fas_handle_unknown_end (phase_msg_in)");
			return (fas_handle_msg_in_start(fas));

		case FAS_PHASE_STATUS:
			fas_reg_cmd_write(fas, CMD_FLUSH);
#ifdef	FAS_TEST
			if (fas_ptest_status & (1<<Tgt(fas->f_current_sp))) {
				fas_reg_cmd_write(fas, CMD_SET_ATN);
			}
#endif	/* FAS_TEST */

			fas_reg_cmd_write(fas, CMD_COMP_SEQ);
			New_state(fas, ACTS_C_CMPLT);

			TRACE_0(TR_FAC_SCSI,
			    TR_FAS_HANDLE_UNKNOWN_PHASE_STATUS_END,
			    "fas_handle_unknown_end (phase_status)");
			return (fas_handle_c_cmplt(fas));

		case FAS_PHASE_COMMAND:
			New_state(fas, ACTS_CMD_START);
			TRACE_0(TR_FAC_SCSI,
			    TR_FAS_HANDLE_UNKNOWN_PHASE_CMD_END,
			    "fas_handle_unknown_end (phase_cmd)");
			return (fas_handle_cmd_start(fas));
		}

		fas_printstate(fas, "Unknown bus phase");
		TRACE_0(TR_FAC_SCSI, TR_FAS_HANDLE_UNKNOWN_RESET_END,
		    "fas_handle_unknown_end (reset)");
		return (ACTION_RESET);

	} else {
		/*
		 * Okay. What to do now? Let's try (for	the time being)
		 * assuming that the target went south and dropped busy,
		 * as a	disconnect implies that	either we received
		 * a completion	or a disconnect	message, or that we
		 * had sent an ABORT OPERATION or BUS DEVICE RESET
		 * message. In either case, we expected	the disconnect
		 * and should have fielded it elsewhere.
		 *
		 * If we see a chip disconnect here, this is an	unexpected
		 * loss	of BSY*. Clean up the state of the chip	and return.
		 *
		 */
		int msgout = fas->f_cur_msgout[0];
		struct fas_cmd *sp = fas->f_current_sp;
		int target = Tgt(sp);

		if (msgout == MSG_HEAD_QTAG || msgout == MSG_SIMPLE_QTAG) {
			msgout = fas->f_cur_msgout[2];
		}
		EPRINTF4("msgout: %x %x %x, last_msgout=%x\n",
			fas->f_cur_msgout[0], fas->f_cur_msgout[1],
			fas->f_cur_msgout[2], fas->f_last_msgout);

		if (msgout == MSG_ABORT	|| msgout == MSG_ABORT_TAG ||
		    msgout == MSG_DEVICE_RESET)	{
			IPRINTF2("Successful %s message to target %d\n",
			    scsi_mname(msgout),	Tgt(sp));
#ifdef FAS_TEST
			if (fas_test_stop)
				debug_enter("message abort/reset success");
#endif

			if (sp->cmd_flags & CFLAG_CMDPROXY) {
				sp->cmd_cdb[FAS_PROXY_RESULT] =	TRUE;
			}
			if (msgout == MSG_ABORT	|| msgout == MSG_ABORT_TAG) {
				fas->f_abort_msg_sent++;
				if ((sp->cmd_flags & CFLAG_CMDPROXY) ==	0) {
					fas_set_pkt_reason(fas,	sp,
					    CMD_ABORTED, STAT_ABORTED);
				}
			} else if (msgout == MSG_DEVICE_RESET) {
				fas->f_reset_msg_sent++;
				if ((sp->cmd_flags & CFLAG_CMDPROXY) ==	0) {
					fas_set_pkt_reason(fas,	sp,
					    CMD_RESET, STAT_DEV_RESET);
				}
				fas_force_renegotiation(fas, target);
			}
		} else {
			if ((fas->f_last_msgout	== MSG_EXTENDED) &&
			    (fas->f_last_msgin == MSG_REJECT)) {
				/*
				 * the target rejected the negotiations,
				 * so resubmit again (no_sync/no_wide
				 * is now set)
				 */
				New_state(fas, STATE_FREE);
				fas_reg_cmd_write(fas, CMD_EN_RESEL);
				fas_remove_cmd(fas, sp,	NEW_TIMEOUT);
				fas_decrement_ncmds(fas, sp);
				fas_check_ncmds(fas);
				sp->cmd_flags &= ~CFLAG_TRANFLAG;
				(void) fas_accept_pkt(fas, sp,	NO_TRAN_BUSY);
				fas_check_ncmds(fas);
				TRACE_0(TR_FAC_SCSI,
				    TR_FAS_HANDLE_UNKNOWN_INT_DISCON_END,
				    "fas_handle_unknown_end (int_discon)");
				return (ACTION_SEARCH);

			} else if (fas->f_last_msgout == MSG_EXTENDED)	{
				/*
				 * target dropped off the bus during
				 * negotiations
				 */
				fas_reset_sync_wide(fas);
				fas->f_sdtr_sent = fas->f_wdtr_sent = 0;
			}

			fas_set_pkt_reason(fas,	sp, CMD_UNX_BUS_FREE, 0);
#ifdef FASDEBUG
			fas_printstate(fas, "unexpected bus free");
#endif
		}
		TRACE_0(TR_FAC_SCSI, TR_FAS_HANDLE_UNKNOWN_INT_DISCON_END,
		    "fas_handle_unknown_end (int_discon)");
		return (ACTION_FINISH);
	}
	_NOTE(NOT_REACHED)
	/* NOTREACHED */
}

/*
 * handle target disconnecting
 */
static int
fas_handle_clearing(register struct fas	*fas)
{
	register struct	fas_cmd	*sp = fas->f_current_sp;

	TRACE_0(TR_FAC_SCSI, TR_FAS_HANDLE_CLEARING_START,
	    "fas_handle_clearing_start");
	EPRINTF("fas_handle_clearing:\n");

	if (fas->f_laststate ==	ACTS_C_CMPLT ||
	    fas->f_laststate ==	ACTS_MSG_IN_DONE) {
		if (INTPENDING(fas)) {
			register volatile struct fasreg	*fasreg	= fas->f_reg;

			fas->f_stat = fas_reg_read(fas,
				(u_char	*)&fasreg->fas_stat);
			fas->f_intr = fas_reg_read(fas,
				(u_char	*)&fasreg->fas_intr);
			if (fas->f_intr	& FAS_INT_RESET) {
				TRACE_0(TR_FAC_SCSI,
				    TR_FAS_HANDLE_CLEARING_FINRST_END,
				    "fas_handle_clearing_end (ACTION_FINRST)");
				return (ACTION_FINRST);
			}
		} else {
			/*
			 * change e_laststate for the next time	around
			 */
			fas->f_laststate = ACTS_CLEARING;
			TRACE_0(TR_FAC_SCSI, TR_FAS_HANDLE_CLEARING_RETURN1_END,
			    "fas_handle_clearing_end (ACTION_RETURN1)");
			return (ACTION_RETURN);
		}
	}

	if (fas->f_intr	== FAS_INT_DISCON) {
		/*
		 * At this point the FAS chip has disconnected.	The bus	should
		 * be either quiet or someone may be attempting	a reselection
		 * of us (or somebody else). Call the routine that sets	the
		 * chip	back to	a correct and known state.
		 * If the last message in was a	disconnect, search
		 * for new work	to do, else return to call fas_finish()
		 */
		fas->f_last_msgout = 0xff;
		fas->f_omsglen = 0;
		if (fas->f_last_msgin == MSG_DISCONNECT) {

			New_state(fas, STATE_FREE);
			fas_reg_cmd_write(fas, CMD_EN_RESEL);

			ASSERT(fas->f_current_sp != NULL);
			EPRINTF2("disconnecting %d.%d\n", Tgt(sp), Lun(sp));

			sp->cmd_pkt->pkt_statistics |= STAT_DISCON;
			sp->cmd_flags |= CFLAG_CMDDISC;
			if ((sp->cmd_flags & CFLAG_CMDPROXY) ==	0) {
				fas->f_ndisc++;
			}
			ASSERT((fas->f_ncmds >=	0) && (fas->f_ndisc >= 0));
			ASSERT(fas->f_ncmds >= fas->f_ndisc);

			fas->f_current_sp = NULL;

			/*
			 * start a cmd here to save time
			 */
			if ((fas->f_ncmds > fas->f_ndisc) && fas_ustart(fas)) {
				TRACE_0(TR_FAC_SCSI,
				    TR_FAS_HANDLE_CLEARING_RETURN2_END,
				    "fas_handle_clearing_end (ACTION_RETURN2)");
				return (ACTION_RETURN);
			}
			TRACE_0(TR_FAC_SCSI, TR_FAS_HANDLE_CLEARING_RETURN3_END,
			    "fas_handle_clearing_end (ACTION_RETURN3)");
			return (ACTION_RETURN);
		} else {
			TRACE_0(TR_FAC_SCSI, TR_FAS_HANDLE_CLEARING_END,
			    "fas_handle_clearing_end");
			return (fas_finish(fas));
		}
	} else {
		/*
		 * If the target didn't	disconnect from	the
		 * bus,	that is	a gross	fatal error.
		 * XXX this can	be caused by asserting ATN
		 * XXX check bus phase and if msgout, send a message
		 */
		fas_log(fas, CE_WARN,
		    "Target %d didn't disconnect after sending %s",
		    Tgt(sp), scsi_mname(fas->f_last_msgin));

		fas_set_pkt_reason(fas,	sp, CMD_TRAN_ERR, 0);

#ifdef FASDEBUG
		IPRINTF4("msgout: %x %x %x, last_msgout=%x\n",
			fas->f_cur_msgout[0], fas->f_cur_msgout[1],
			fas->f_cur_msgout[2], fas->f_last_msgout);
		IPRINTF1("last msgin=%x\n", fas->f_last_msgin);
#endif
		TRACE_0(TR_FAC_SCSI, TR_FAS_HANDLE_CLEARING_ABORT_END,
		    "fas_handle_clearing_end (ACTION_ABORT_CURCMD)");
		return (ACTION_ABORT_ALLCMDS);
	}
}

/*
 * handle data phase start
 */
static int
fas_handle_data_start(struct fas *fas)
{
	register u_long	end, amt;
	register struct	fas_cmd	*sp = fas->f_current_sp;
	register int sending, phase;

	TRACE_0(TR_FAC_SCSI, TR_FAS_HANDLE_DATA_START,
	    "fas_handle_data_start");
	EPRINTF("fas_handle_data_start:\n");

	if ((sp->cmd_flags & CFLAG_DMAVALID) ==	0) {
		fas_printstate(fas, "unexpected data phase");
bad:
		fas_set_pkt_reason(fas,	sp, CMD_TRAN_ERR, 0);

		TRACE_0(TR_FAC_SCSI, TR_FAS_HANDLE_DATA_ABORT1_END,
		    "fas_handle_data_end (ACTION_ABORT_CURCMD1)");
		return (ACTION_ABORT_CURCMD);
	} else {
		sending	= (sp->cmd_flags & CFLAG_DMASEND)? 1 : 0;
	}

	if (sp->cmd_flags & CFLAG_RESTORE_PTRS)	{
		if (fas_restore_pointers(fas, sp)) {
			return (ACTION_ABORT_CURCMD);
		}
		sp->cmd_flags &= ~CFLAG_RESTORE_PTRS;
	}

	/*
	 * And make sure our DMA pointers are in good shape.
	 *
	 * Because SCSI	is SCSI, the current DMA pointer has got to be
	 * greater than	or equal to our	DMA base address. All other cases
	 * that	might have affected this always	set curaddr to be >=
	 * to the DMA base address.
	 */
	ASSERT(sp->cmd_cur_addr	>= sp->cmd_dmacookie.dmac_address);
	end = sp->cmd_dmacookie.dmac_address + sp->cmd_dmacookie.dmac_size;

	DPRINTF5(
	"cmd_data_count=%x, dmacount=%x, curaddr=%x, end=%x, nwin=%x\n",
	    sp->cmd_data_count,	sp->cmd_dmacount, sp->cmd_cur_addr, end,
	    sp->cmd_nwin);
	DPRINTF2("dmac_address = %x, dmac_size=%x\n",
	    sp->cmd_dmacookie.dmac_address, sp->cmd_dmacookie.dmac_size);

	if (sp->cmd_cur_addr >=	end) {
		if (fas_next_window(fas, sp, end)) {
			goto bad;
		}
		end = sp->cmd_dmacookie.dmac_address +
			sp->cmd_dmacookie.dmac_size;
		DPRINTF2("dmac_address=%x, dmac_size=%x\n",
		    sp->cmd_dmacookie.dmac_address,
		    sp->cmd_dmacookie.dmac_size);
	}

	amt = end - sp->cmd_cur_addr;
	if (FAS_MAX_DMACOUNT < amt) {
		amt = FAS_MAX_DMACOUNT;
	}
	DPRINTF3("amt=%x, end=%x, cur_addr=%x\n", amt, end, sp->cmd_cur_addr);

#ifdef FASDEBUG
	/*
	 * Make	sure that we don't cross a boundary we can't handle
	 */
	end = sp->cmd_cur_addr + amt;
	if ((end & ~fas->f_dma_attr->dma_attr_seg) !=
	    (sp->cmd_cur_addr &	~fas->f_dma_attr->dma_attr_seg)) {
		EPRINTF3("curaddr %x curaddr+amt %x cntr_max %x\n",
		    sp->cmd_cur_addr, end, fas->f_dma_attr->dma_attr_seg);
		amt = (end & ~fas->f_dma_attr->dma_attr_seg) - sp->cmd_cur_addr;
		if (amt	== 0 ||	amt > FAS_MAX_DMACOUNT)	{
			fas_log(fas, CE_WARN, "illegal dma boundary? %x", amt);
			goto bad;
		}
	}
#endif

	end = sp->cmd_dmacookie.dmac_address + sp->cmd_dmacookie.dmac_size -
	    sp->cmd_cur_addr;
	if (amt	> end) {
		EPRINTF4("ovflow amt %x s.b. %x curaddr %x count %x\n",
		    amt, end, sp->cmd_cur_addr,	sp->cmd_dmacount);
		amt = end;
	}

	fas->f_lastcount = amt;

	EPRINTF4("%d.%d cmd 0x%x to xfer %x\n",	Tgt(sp), Lun(sp),
	    sp->cmd_pkt->pkt_cdbp[0], amt);

	phase =	fas->f_stat & FAS_PHASE_MASK;

#ifdef BREAK_XFER_HACK
	/*
	 * workaround for one extreme case in the FEPS
	 * scsi channel engine under the following conditions:
	 *   - scsi write
	 *   - odd byte address start
	 *   - odd byte count
	 *   - end address burst aligned
	 * in this case, the scsi transfer should be broken
	 * into 2 xfers: an even count xfer and a 1 byte xfer
	 */
	if ((phase == FAS_PHASE_DATA_OUT) && (amt & 0x1) &&
	    (sp->cmd_cur_addr & 0x1) && (amt > 1)) {
		amt--;
		fas->f_lastcount--;
		fas_break_xfer_flag = 1;
	}
#endif
	if ((phase == FAS_PHASE_DATA_IN) && !sending) {
		FAS_DMA_WRITE(fas, amt,	sp->cmd_cur_addr,
		    CMD_TRAN_INFO|CMD_DMA);
	} else if ((phase == FAS_PHASE_DATA_OUT) && sending) {
		FAS_DMA_READ(fas, amt, sp->cmd_cur_addr, amt,
		    CMD_TRAN_INFO|CMD_DMA);
	} else {
		fas_log(fas, CE_WARN,
		    "unwanted data xfer direction for Target %d", Tgt(sp));
		fas_set_pkt_reason(fas,	sp, CMD_DMA_DERR, 0);
		TRACE_0(TR_FAC_SCSI, TR_FAS_HANDLE_DATA_ABORT2_END,
		    "fas_handle_data_end (ACTION_ABORT_CURCMD2)");
		return (ACTION_ABORT_CURCMD);
	}

#ifdef	FAS_TEST
	if (!sending &&	(fas_ptest_data_in & (1<<Tgt(sp)))) {
		fas_reg_cmd_write(fas, CMD_SET_ATN);
	}
#endif	/* FAS_TEST */

	New_state(fas, ACTS_DATA_DONE);

	TRACE_0(TR_FAC_SCSI, TR_FAS_HANDLE_DATA_END,
	    "fas_handle_data_end (ACTION_RETURN)");
	return (ACTION_RETURN);
}

static int
fas_handle_data_done(register struct fas *fas)
{
	register volatile struct fasreg	*fasreg	= fas->f_reg;
	register volatile struct dma *dmar = fas->f_dma;
	register struct	fas_cmd	*sp = fas->f_current_sp;
	register u_long	xfer_amt;
	char was_sending;
	register u_char	stat, fifoamt, tgt;

	TRACE_0(TR_FAC_SCSI, TR_FAS_HANDLE_DATA_DONE_START,
	    "fas_handle_data_done_start");
	EPRINTF("fas_handle_data_done\n");

	tgt = Tgt(sp);
	stat = fas->f_stat;
	was_sending = (sp->cmd_flags & CFLAG_DMASEND) ?	1 : 0;

	/*
	 * Check for DMA errors	(parity	or memory fault)
	 */
	if ((fas->f_dma_csr = fas_dma_reg_read(fas, &dmar->dma_csr)) &
	    DMA_ERRPEND) {
		/*
		 * It would be desirable to set	the ATN* line and attempt to
		 * do the whole	schmear	of INITIATOR DETECTED ERROR here,
		 * but that is too hard	to do at present.
		 */
		fas_log(fas, CE_WARN, "Unrecoverable DMA error on dma %s",
		    (was_sending) ? "send" : "receive");
		fas_set_pkt_reason(fas,	sp, CMD_TRAN_ERR, 0);
		TRACE_0(TR_FAC_SCSI, TR_FAS_HANDLE_DATA_DONE_RESET_END,
		    "fas_handle_data_done_end (ACTION_RESET)");
		return (ACTION_RESET);
	}

	/*
	 * Data	Receive	conditions:
	 *
	 * Check for parity errors. If we have a parity	error upon
	 * receive, the	FAS chip has asserted ATN* for us already.
	 */
	if (!was_sending) {
#ifdef	FAS_TEST
		if (fas_ptest_data_in &	(1<<tgt)) {
			fas_ptest_data_in = 0;
			stat |=	FAS_STAT_PERR;
		}
#endif	/* FAS_TEST */
		if (stat & FAS_STAT_PERR) {
			fas_log(fas, CE_WARN,
			    "SCSI bus DATA IN phase parity error");
			fas->f_cur_msgout[0] = MSG_INITIATOR_ERROR;
			fas->f_omsglen = 1;
			sp->cmd_pkt->pkt_statistics |= STAT_PERR;
			sp->cmd_pkt->pkt_reason	= CMD_TRAN_ERR;
		}
	}

	FAS_FLUSH_DMA(fas);

	/*
	 * Check to make sure we're still connected to the target.
	 * If the target dropped the bus, that is a fatal error.
	 * We don't even attempt to count what we were transferring
	 * here. Let fas_handle_unknown	clean up for us.
	 */
	if (fas->f_intr	!= FAS_INT_BUS)	{
		New_state(fas, ACTS_UNKNOWN);
		TRACE_0(TR_FAC_SCSI, TR_FAS_HANDLE_DATA_DONE_PHASEMANAGE_END,
		    "fas_handle_data_done_end (ACTION_PHASEMANAGE)");
		return (ACTION_PHASEMANAGE);
	}

	/*
	 * Figure out how far we got.
	 * Latch up fifo amount	first and double if wide has been enabled
	 */
	fifoamt	= FIFO_CNT(fas);
	if (fas->f_wide_enabled	& (1<<tgt)) {
		fifoamt	= fifoamt << 1;
	}

	if (stat & FAS_STAT_XZERO) {
		xfer_amt = fas->f_lastcount;
	} else {
		GET_FAS_COUNT(fasreg, xfer_amt);
		xfer_amt = fas->f_lastcount - xfer_amt;
	}
	DPRINTF4("fifoamt=%x, xfer_amt=%x, lastcount=%x, stat=%x\n",
	    fifoamt, xfer_amt, fas->f_lastcount, stat);


	/*
	 * Unconditionally knock off by	the amount left
	 * in the fifo if we were sending out the SCSI bus.
	 *
	 * If we were receiving	from the SCSI bus, believe
	 * what	the chip told us (either XZERO or by the
	 * value calculated from the counter register).
	 * The reason we don't look at the fifo	for
	 * incoming data is that in synchronous	mode
	 * the fifo may	have further data bytes, and
	 * for async mode we assume that all data in
	 * the fifo will have been transferred before
	 * the fas asserts an interrupt.
	 */
	if (was_sending) {
		xfer_amt -= fifoamt;
	}


#ifdef ODDBYTE_HACK
{
	int phase = stat & FAS_PHASE_MASK;
	u_int nwin = 0;
	int rval;

	rval = ddi_dma_numwin(sp->cmd_dmahandle, &nwin);

	if (fas_enable_oddbyte_workaround &&
	    (phase != FAS_PHASE_DATA_IN) &&
	    (phase != FAS_PHASE_DATA_OUT) &&
	    ((nwin == 1) || /* offset calculation for 1 window only or */
	    (rval != DDI_SUCCESS)) && /* if there is no partial mapping */
	    (fas->f_stat2 & FAS_STAT2_ISHUTTLE)) {
		struct buf *bp = sp->cmd_bp;
		u_char odd_byte, *p;
		u_int start = gethrtime();

		ASSERT(was_sending == 0);

		fas_reg_write(fas, &fasreg->fas_fifo_data, 0);
		odd_byte = fas_reg_read(fas, &fasreg->fas_fifo_data);
		bp_mapin(bp);
		if (ddi_dma_sync(sp->cmd_dmahandle, 0, 0,
		    (sp->cmd_flags & CFLAG_DMASEND) ?
		    DDI_DMA_SYNC_FORDEV	: DDI_DMA_SYNC_FORCPU) !=
		    DDI_SUCCESS) {
			    IPRINTF1("sync of pkt (%x) failed",	sp);
		}

		/*
		 * if synchronous then increment xfer count
		 */
		if (fas->f_offset[Tgt(sp)]) {
			xfer_amt++;
		}
		p = (u_char *)(bp->b_un.b_addr + xfer_amt - 1 +
			sp->cmd_data_count);
		*p = odd_byte;
		bp_mapout(bp);
		IPRINTF1("time=%d nsec\n", (u_int)gethrtime() -	start);
		IPRINTF5(
		"cmd_data_count=%x cmd_cur_addr=%x, xfer_amt=%x, p=%x"
		" oddbyte=%x\n",
		sp->cmd_data_count, sp->cmd_cur_addr, xfer_amt,	p, odd_byte);
	} else if (((fas->f_stat & FAS_STAT_XZERO) == 0) &&
	    (phase != FAS_PHASE_DATA_IN) &&
	    (phase != FAS_PHASE_DATA_OUT) &&
	    (fas->f_stat2 & FAS_STAT2_ISHUTTLE)) {
		fas_log(fas, CE_WARN,
		    "input shuttle not empty at end of data phase");
		fas_set_pkt_reason(fas,	sp, CMD_TRAN_ERR, 0);
		TRACE_0(TR_FAC_SCSI, TR_FAS_HANDLE_DATA_DONE_RESET_END,
		    "fas_handle_data_done_end (ACTION_RESET)");
		return (ACTION_RESET);
	}
}
#else

#ifdef FASDEBUG
{
	int phase = stat & FAS_PHASE_MASK;

	if (((fas->f_stat & FAS_STAT_XZERO) == 0) &&
	    (phase != FAS_PHASE_DATA_IN) &&
	    (phase != FAS_PHASE_DATA_OUT) &&
	    (fas->f_stat2 & FAS_STAT2_ISHUTTLE)) {
		fas_log(fas, CE_WARN,
		    "input shuttle not empty at end of data phase");
		fas_set_pkt_reason(fas, sp, CMD_TRAN_ERR, 0);
		TRACE_0(TR_FAC_SCSI, TR_FAS_HANDLE_DATA_DONE_RESET_END,
		"fas_handle_data_done_end (ACTION_RESET)");
		return (ACTION_RESET);
	}
}
#endif /* FASDEBUG */

#endif /* ODDBYTE_HACK */

	/*
	 * If this was a synchronous transfer, flag it.
	 * Also	check for the errata condition of long
	 * last	REQ/ pulse for some synchronous	targets
	 */
	if (fas->f_offset[tgt])	{
		/*
		 * flag	that a synchronous data	xfer took place
		 */
		sp->cmd_pkt->pkt_statistics |= STAT_SYNC;

		if (was_sending)
			fas_reg_cmd_write(fas, CMD_FLUSH);
	} else {
		/*
		 * If we aren't	doing Synchronous Data Transfers,
		 * definitely offload the fifo.
		 */
		fas_reg_cmd_write(fas, CMD_FLUSH);
	}

	/*
	 * adjust pointers...
	 */
	DPRINTF3("before:cmd_data_count=%x, cmd_cur_addr=%x, xfer_amt=%x\n",
	    sp->cmd_data_count,	sp->cmd_cur_addr, xfer_amt);
	sp->cmd_data_count += xfer_amt;
	sp->cmd_cur_addr += xfer_amt;
	sp->cmd_pkt->pkt_state |= STATE_XFERRED_DATA;
	New_state(fas, ACTS_UNKNOWN);
	DPRINTF3("after:cmd_data_count=%x, cmd_cur_addr=%x, xfer_amt=%x\n",
	    sp->cmd_data_count,	sp->cmd_cur_addr, xfer_amt);

	stat &=	FAS_PHASE_MASK;
	if (stat == FAS_PHASE_DATA_IN || stat == FAS_PHASE_DATA_OUT) {
		fas->f_state = ACTS_DATA;
		TRACE_0(TR_FAC_SCSI,
		    TR_FAS_HANDLE_DATA_DONE_ACTION1_END,
		    "fas_handle_data_done_end (action1)");
		return (fas_handle_data_start(fas));
	}

	TRACE_0(TR_FAC_SCSI, TR_FAS_HANDLE_DATA_DONE_ACTION2_END,
	    "fas_handle_data_done_end (action2)");
	return (fas_handle_unknown(fas));
}

static char *msginperr = "SCSI bus MESSAGE IN phase parity error\n";

static int
fas_handle_c_cmplt(register struct fas *fas)
{
	register struct	fas_cmd	*sp = fas->f_current_sp;
	volatile struct	fasreg *fasreg = fas->f_reg;
	register u_char	sts, msg, msgout, intr,	perr;
	register u_char	*c = (u_char *)	fas->f_cmdarea;

	TRACE_0(TR_FAC_SCSI, TR_FAS_HANDLE_C_CMPLT_START,
	    "fas_handle_c_cmplt_start");
	EPRINTF("fas_handle_c_cmplt:\n");

	/*
	 * if target is	fast, we can get cmd. completion by the	time we	get
	 * here. Otherwise, we'll have to taken	an interrupt.
	 */
	if (fas->f_laststate ==	ACTS_UNKNOWN) {
		if (INTPENDING(fas)) {
			fas->f_stat = fas_reg_read(fas,
				(u_char	*)&fasreg->fas_stat);
			intr = fas_reg_read(fas, (u_char *)&fasreg->fas_intr);
			fas->f_intr = intr;
			if (intr & FAS_INT_RESET) {
				TRACE_0(TR_FAC_SCSI,
				    TR_FAS_HANDLE_C_CMPLT_FINRST_END,
				    "fas_handle_c_cmplt_end (ACTION_FINRST)");
				return (ACTION_FINRST);
			}
		} else {
			/*
			 * change f_laststate for the next time	around
			 */
			fas->f_laststate = ACTS_C_CMPLT;
			TRACE_0(TR_FAC_SCSI, TR_FAS_HANDLE_C_CMPLT_RETURN1_END,
			    "fas_handle_c_cmplt_end (ACTION_RETURN1)");
			return (ACTION_RETURN);
		}
	} else {
		intr = fas->f_intr;
	}

#ifdef	FAS_TEST
	if (fas_ptest_status & (1<<Tgt(sp))) {
		fas_ptest_status = 0;
		fas->f_stat |= FAS_STAT_PERR;
	} else if ((fas_ptest_msgin & (1<<Tgt(sp))) && fas_ptest_msg ==	0) {
		fas_reg_cmd_write(fas, CMD_SET_ATN);
		fas_ptest_msgin	= 0;
		fas_ptest_msg =	-1;
		fas->f_stat |= FAS_STAT_PERR;
	}
#endif	/* FAS_TEST */

	if (intr == FAS_INT_DISCON) {
		New_state(fas, ACTS_UNKNOWN);
		TRACE_0(TR_FAC_SCSI, TR_FAS_HANDLE_C_CMPLT_ACTION1_END,
		    "fas_handle_c_cmplt_end (action1)");
		return (fas_handle_unknown(fas));
	}

	if ((perr = (fas->f_stat & FAS_STAT_PERR)) != 0) {
		sp->cmd_pkt->pkt_statistics |= STAT_PERR;
	}

	/*
	 * do a	msg accept now and read	the fifo data
	 */
	if (intr & FAS_INT_FCMP) {
		fas_reg_cmd_write(fas, CMD_MSG_ACPT);
		*c = fas_reg_read(fas, (u_char *)&fasreg->fas_fifo_data);
		*(c+1) = fas_reg_read(fas, (u_char *)&fasreg->fas_fifo_data);
	}

	msgout = 0;
	msg = sts = INVALID_MSG;

	/*
	 * The FAS manuals state that this sequence completes
	 * with	a BUS SERVICE interrupt	if just	the status
	 * byte	was received, else a FUNCTION COMPLETE interrupt
	 * if both status and a	message	was received.
	 *
	 * The manuals also state that ATN* is asserted	if
	 * bad parity is detected.
	 *
	 * The one case	that we	cannot handle is where we detect
	 * bad parity for the status byte, but the target refuses
	 * to go to MESSAGE OUT	phase right away. This means that
	 * if that happens, we will misconstrue	the parity error
	 * to be for the completion message, not the status byte.
	 */
	if (intr & FAS_INT_FCMP) {
		sts = *c++;
		fas->f_last_msgin = fas->f_imsgarea[0] = msg = *c;
		if (perr) {
			fas_log(fas, CE_WARN, msginperr);
			sp->cmd_pkt->pkt_statistics |= STAT_PERR;
			msgout = MSG_MSG_PARITY;
		}
	} else if (intr	== FAS_INT_BUS)	{
		/*
		 * We only got the status byte.
		 */
		sts = *c;

		IPRINTF1("fas_handle_cmd_cmplt: sts=%x, no msg byte\n",	sts);

		if (perr) {
			/*
			 * If we get a parity error on a status	byte
			 * assume that it was a	CHECK CONDITION
			 */
			sts = STATUS_CHECK;
			fas_log(fas, CE_WARN,
			    "SCSI bus STATUS phase parity error");
			msgout = MSG_INITIATOR_ERROR;
		}
	} else {
		IPRINTF("fas_handle_cmd_cmplt: unexpected int\n");
		New_state(fas, ACTS_UNKNOWN);
		TRACE_0(TR_FAC_SCSI, TR_FAS_HANDLE_C_CMPLT_ACTION2_END,
		    "fas_handle_c_cmplt_end (action2)");
		return (fas_handle_unknown(fas));
	}

	EPRINTF2("fas_handle_c_cmplt: status=%x, msg=%x\n", sts, msg);

	if (sts	!= INVALID_MSG)	{
		sp->cmd_pkt->pkt_state |= STATE_GOT_STATUS;
		*(sp->cmd_pkt->pkt_scbp) = sts;
		EPRINTF1("Status=0x%x\n", sts);
	}

	if (msgout == 0) {
		EPRINTF1("Completion Message=%s\n", scsi_mname(msg));
		if (msg	== MSG_COMMAND_COMPLETE) {
			/*
			 * Actually, if	the message was	a 'linked command
			 * complete' message, the target isn't going to	be
			 * clearing the	bus.
			 */
			New_state(fas, ACTS_CLEARING);
		} else {
			fas->f_imsglen = 1;
			fas->f_imsgindex = 1;
			New_state(fas, ACTS_MSG_IN_DONE);
			TRACE_0(TR_FAC_SCSI, TR_FAS_HANDLE_C_CMPLT_ACTION3_END,
			    "fas_handle_c_cmplt_end (action3)");
			return (fas_handle_msg_in_done(fas));
		}
	} else {
		fas->f_cur_msgout[0] = msgout;
		fas->f_omsglen = 1;
		New_state(fas, ACTS_UNKNOWN);
	}

	if (intr != FAS_INT_BUS) {
		if (fas->f_state == ACTS_CLEARING) {
			TRACE_0(TR_FAC_SCSI, TR_FAS_HANDLE_C_CMPLT_ACTION4_END,
			    "fas_handle_c_cmplt_end (action4)");
			return (fas_handle_clearing(fas));
		}
		TRACE_0(TR_FAC_SCSI, TR_FAS_HANDLE_C_CMPLT_RETURN2_END,
		    "fas_handle_c_cmplt_end (ACTION_RETURN2)");
		return (ACTION_RETURN);
	} else {
		if (fas->f_state == ACTS_UNKNOWN) {
			TRACE_0(TR_FAC_SCSI, TR_FAS_HANDLE_C_CMPLT_ACTION5_END,
			    "fas_handle_c_cmplt_end (action5)");
			return (fas_handle_unknown(fas));
		}
		TRACE_0(TR_FAC_SCSI, TR_FAS_HANDLE_C_CMPLT_PHASEMANAGE_END,
		    "fas_handle_c_cmplt_end (ACTION_PHASEMANAGE)");
		return (ACTION_PHASEMANAGE);
	}
}

/*
 * prepare for accepting a message byte	from the fifo
 */
static int
fas_handle_msg_in_start(register struct	fas *fas)
{
	TRACE_0(TR_FAC_SCSI, TR_FAS_HANDLE_MSG_IN_START,
	    "fas_handle_msg_in_start");
	EPRINTF("fas_handle_msg_in_start\n");

	/*
	 * Pick	up a message byte.
	 * Clear the FIFO so we
	 * don't get confused.
	 */
	if (!FIFO_EMPTY(fas)) {
		fas_reg_cmd_write(fas, CMD_FLUSH);
	}
	fas_reg_cmd_write(fas, CMD_TRAN_INFO);
	fas->f_imsglen = 1;
	fas->f_imsgindex = 0;
	New_state(fas, ACTS_MSG_IN_DONE);

	/*
	 * give	a little extra time by returning to phasemanage
	 */
	TRACE_0(TR_FAC_SCSI, TR_FAS_HANDLE_MSG_IN_END,
	    "fas_handle_msg_in_end (ACTION_PHASEMANAGE)");
	return (ACTION_PHASEMANAGE);
}

/*
 * We come here	after issuing a	MSG_ACCEPT
 * command and are expecting more message bytes.
 * The FAS should be asserting a BUS SERVICE
 * interrupt status, but may have asserted
 * a different interrupt in the	case that
 * the target disconnected and dropped BSY*.
 *
 * In the case that we are eating up message
 * bytes (and throwing them away unread) because
 * we have ATN*	asserted (we are trying	to send
 * a message), we do not consider it an	error
 * if the phase	has changed out	of MESSAGE IN.
 */
static int
fas_handle_more_msgin(register struct fas *fas)
{
	TRACE_0(TR_FAC_SCSI, TR_FAS_HANDLE_MORE_MSGIN_START,
	    "fas_handle_more_msgin_start");
	EPRINTF("fas_handle_more_msgin\n");

	if (fas->f_intr	& FAS_INT_BUS) {
		if ((fas->f_stat & FAS_PHASE_MASK) == FAS_PHASE_MSG_IN)	{
			/*
			 * Fetch another byte of a message in.
			 */
			fas_reg_cmd_write(fas, CMD_TRAN_INFO);
			New_state(fas, ACTS_MSG_IN_DONE);
			TRACE_0(TR_FAC_SCSI,
			    TR_FAS_HANDLE_MORE_MSGIN_RETURN1_END,
			    "fas_handle_more_msgin_end (ACTION_RETURN)");
			return (ACTION_RETURN);
		}

		/*
		 * If we were gobbling up a message and	we have
		 * changed phases, handle this silently, else
		 * complain. In	either case, we	return to let
		 * fas_phasemanage() handle things.
		 *
		 * If it wasn't	a BUS SERVICE interrupt,
		 * let fas_phasemanage() find out if the
		 * chip	disconnected.
		 */
		if (fas->f_imsglen != 0) {
			fas_log(fas, CE_WARN,
			    "Premature end of extended message");
		}
	}
	New_state(fas, ACTS_UNKNOWN);
	TRACE_0(TR_FAC_SCSI, TR_FAS_HANDLE_MORE_MSGIN_RETURN2_END,
	    "fas_handle_more_msgin_end (action)");
	return (fas_handle_unknown(fas));
}

static int
fas_handle_msg_in_done(register	struct fas *fas)
{
	register struct	fas_cmd	*sp = fas->f_current_sp;
	register volatile struct fasreg	*fasreg	= fas->f_reg;
	register sndmsg	= 0;
	register u_char	msgin;

	TRACE_0(TR_FAC_SCSI, TR_FAS_HANDLE_MSG_IN_DONE_START,
	    "fas_handle_msg_in_done_start");
	EPRINTF("fas_handle_msg_in_done:\n");
	if (fas->f_laststate ==	ACTS_MSG_IN) {
		if (INTPENDING(fas)) {
			fas->f_stat = fas_reg_read(fas,
				(u_char	*)&fasreg->fas_stat);

			fas_read_fifo(fas);

			fas->f_intr = fas_reg_read(fas,
				(u_char	*)&fasreg->fas_intr);
			if (fas->f_intr	& FAS_INT_RESET) {
				TRACE_0(TR_FAC_SCSI,
				    TR_FAS_HANDLE_MSG_IN_DONE_FINRST_END,
				    "fas_handle_msg_in_done_end (_FINRST)");
				return (ACTION_FINRST);
			}
		} else {
			/*
			 * change f_laststate for the next time	around
			 */
			fas->f_laststate = ACTS_MSG_IN_DONE;
			TRACE_0(TR_FAC_SCSI,
			    TR_FAS_HANDLE_MSG_IN_DONE_RETURN1_END,
			    "fas_handle_msg_in_done_end (ACTION_RETURN1)");
			return (ACTION_RETURN);
		}
	}

	/*
	 * We can be called here for both the case where
	 * we had requested the	FAS chip to fetch a message
	 * byte	from the target	(at the	target's request).
	 * We can also be called in the	case where we had
	 * been	using the CMD_COMP_SEQ command to pick up
	 * both	a status byte and a completion message from
	 * a target, but where the message wasn't one of
	 * COMMAND COMPLETE, LINKED COMMAND COMPLETE, or
	 * LINKED COMMAND COMPLETE (with flag).	This is	a
	 * legal (albeit extremely unusual) SCSI bus trans-
	 * -ition, so we have to handle	it.
	 */
	if (fas->f_laststate !=	ACTS_C_CMPLT) {
#ifdef	FAS_TEST
reloop:
#endif	/* FAS_TEST */

		if (fas->f_intr	& FAS_INT_DISCON) {
			fas_log(fas, CE_WARN,
			    "premature end of input message");
			New_state(fas, ACTS_UNKNOWN);
			TRACE_0(TR_FAC_SCSI,
			    TR_FAS_HANDLE_MSG_IN_DONE_PHASEMANAGE_END,
			    "fas_handle_msg_in_done_end (ACTION_PHASEMANAGE)");
			return (ACTION_PHASEMANAGE);
		}

		/*
		 * Note	that if	e_imsglen is zero, then	we are skipping
		 * input message bytes,	so there is no reason to look for
		 * parity errors.
		 */
		if (fas->f_imsglen != 0	&& (fas->f_stat	& FAS_STAT_PERR)) {
			fas_log(fas, CE_WARN, msginperr);
			sndmsg = MSG_MSG_PARITY;
			sp->cmd_pkt->pkt_statistics |= STAT_PERR;
			fas_reg_cmd_write(fas, CMD_FLUSH);

		} else if ((msgin = fas->f_fifolen) != 1) {

			/*
			 * If we have got more than one	byte in	the fifo,
			 * that	is a gross screwup, and	we should let the
			 * target know that we have completely fouled up.
			 */
			fas_printf(fas,	"fifocount=%x\n", msgin);
			fas_printstate(fas, "input message botch");
			sndmsg = MSG_INITIATOR_ERROR;
			fas_reg_cmd_write(fas, CMD_FLUSH);
			fas_log(fas, CE_WARN, "input message botch");

		} else if (fas->f_imsglen == 0)	{
			/*
			 * If we are in	the middle of gobbling up and throwing
			 * away	a message (due to a previous message input
			 * error), drive on.
			 */
			msgin =	fas_reg_read(fas,
				(u_char	*)&fasreg->fas_fifo_data);
			New_state(fas, ACTS_MSG_IN_MORE);

		} else {
			msgin =	fas->f_fifo[0];
			fas->f_imsgarea[fas->f_imsgindex++] = msgin;
		}

	} else {
		/*
		 * In this case, we have been called (from
		 * fas_handle_c_cmplt()) with the message
		 * already stored in the message array.
		 */
		msgin =	fas->f_imsgarea[0];
	}

	/*
	 * Process this	message	byte (but not if we are
	 * going to be trying to send back some	error
	 * anyway)
	 */
	if (sndmsg == 0	&& fas->f_imsglen != 0)	{

		if (fas->f_imsgindex < fas->f_imsglen) {

			EPRINTF2("message byte %d: 0x%x\n",
			    fas->f_imsgindex-1,
			    fas->f_imsgarea[fas->f_imsgindex-1]);

			New_state(fas, ACTS_MSG_IN_MORE);

		} else if (fas->f_imsglen == 1)	{

#ifdef	FAS_TEST
			if ((fas_ptest_msgin & (1<<Tgt(sp))) &&
			    fas_ptest_msg == msgin) {
				fas_ptest_msgin	= 0;
				fas_ptest_msg =	-1;
				fas_reg_cmd_write(fas, CMD_SET_ATN);
				fas->f_stat |= FAS_STAT_PERR;
				fas->f_imsgindex -= 1;
				goto reloop;
			}
#endif	/* FAS_TEST */

			sndmsg = fas_onebyte_msg(fas);

		} else if (fas->f_imsglen == 2)	{
#ifdef	FAS_TEST
			if (fas_ptest_emsgin & (1<<Tgt(sp))) {
				fas_ptest_emsgin = 0;
				fas_reg_cmd_write(fas, CMD_SET_ATN);
				fas->f_stat |= FAS_STAT_PERR;
				fas->f_imsgindex -= 1;
				goto reloop;
			}
#endif	/* FAS_TEST */

			if (fas->f_imsgarea[0] ==  MSG_EXTENDED) {
				static char *tool =
				    "Extended message 0x%x is too long";

				/*
				 * Is the incoming message too long
				 * to be stored	in our local array?
				 */
				if ((int)(msgin+2) > IMSGSIZE) {
					fas_log(fas, CE_WARN,
					    tool, fas->f_imsgarea[0]);
					sndmsg = MSG_REJECT;
				} else {
					fas->f_imsglen = msgin + 2;
					New_state(fas, ACTS_MSG_IN_MORE);
				}
			} else {
				sndmsg = fas_twobyte_msg(fas);
			}

		} else {
			sndmsg = fas_multibyte_msg(fas);
		}
	}

	if (sndmsg < 0)	{
		/*
		 * If sndmsg is	less than zero,	one of the subsidiary
		 * routines needs to return some other state than
		 * ACTION_RETURN.
		 */
		TRACE_0(TR_FAC_SCSI, TR_FAS_HANDLE_MSG_IN_DONE_SNDMSG_END,
		    "fas_handle_msg_in_done_end (-sndmsg)");
		return (-sndmsg);
	} else if (sndmsg > 0) {
		if (IS_1BYTE_MSG(sndmsg)) {
			fas->f_omsglen = 1;
		}
		fas->f_cur_msgout[0] = (u_char)	sndmsg;

		/*
		 * The target is not guaranteed	to go to message out
		 * phase, period. Moreover, until the entire incoming
		 * message is transferred, the target may (and likely
		 * will) continue to transfer message bytes (which
		 * we will have	to ignore).
		 *
		 * In order to do this,	we'll go to 'infinite'
		 * message in handling by setting the current input
		 * message length to a sentinel	of zero.
		 *
		 * This	works regardless of the	message	we are trying
		 * to send out.	At the point in	time which we want
		 * to send a message in	rfasonse to an incoming	message
		 * we do not care any more about the incoming message.
		 *
		 * If we are sending a message in rfasonse to detecting
		 * a parity error on input, the	FAS chip has already
		 * set ATN* for	us, but	it doesn't hurt	to set it here
		 * again anyhow.
		 */
		fas_reg_cmd_write(fas, CMD_SET_ATN);
		New_state(fas, ACTS_MSG_IN_MORE);
		fas->f_imsglen = 0;
	}

	if (!FIFO_EMPTY(fas)) {
		fas_reg_cmd_write(fas, CMD_FLUSH);
	}

	fas_reg_cmd_write(fas, CMD_MSG_ACPT);

	if ((fas->f_laststate == ACTS_MSG_IN_DONE) &&
	    (fas->f_state == ACTS_CLEARING)) {
		TRACE_0(TR_FAC_SCSI, TR_FAS_HANDLE_MSG_IN_DONE_ACTION_END,
		    "fas_handle_msg_in_done_end	(action)");
		return (fas_handle_clearing(fas));
	}
	TRACE_0(TR_FAC_SCSI, TR_FAS_HANDLE_MSG_IN_DONE_RETURN2_END,
	    "fas_handle_msg_in_done_end	(ACTION_RETURN2)");
	return (ACTION_RETURN);
}

static int
fas_onebyte_msg(register struct	fas *fas)
{
	register struct	fas_cmd	*sp = fas->f_current_sp;
	register int msgout = 0;
	register u_char	msgin =	fas->f_last_msgin = fas->f_imsgarea[0];
	register tgt = Tgt(sp);

	EPRINTF("fas_onebyte_msg\n");

	if (msgin & MSG_IDENTIFY) {
		/*
		 * How did we get here?	We should only see identify
		 * messages on a reconnection, but we'll handle	this
		 * fine	here (just in case we get this)	as long	as
		 * we believe that this	is a valid identify message.
		 *
		 * For this to be a valid incoming message,
		 * bits	6-4 must must be zero. Also, the
		 * bit that says that I'm an initiator and
		 * can support disconnection cannot possibly
		 * be set here.
		 */

		char garbled = ((msgin & (BAD_IDENTIFY|INI_CAN_DISCON))	!= 0);

		fas_log(fas, CE_WARN, "%s message 0x%x from Target %d",
		    garbled ? "Garbled"	: "Identify", msgin, tgt);

		if (garbled) {
			/*
			 * If it's a garbled message,
			 * try and tell	the target...
			 */
			msgout = MSG_INITIATOR_ERROR;
		} else {
			New_state(fas, ACTS_UNKNOWN);
		}
		return (msgout);

	} else if (IS_2BYTE_MSG(msgin) || IS_EXTENDED_MSG(msgin)) {
		fas->f_imsglen = 2;
		New_state(fas, ACTS_MSG_IN_MORE);
		return (0);
	}

	New_state(fas, ACTS_UNKNOWN);

	switch (msgin) {
	case MSG_DISCONNECT:
		/*
		 * If we 'cannot' disconnect- reject this message.
		 * Note	that we	only key off of	the pkt_flags here-
		 * the FLAG_NODISCON was set in	fas_accept_pkt() if
		 * no disconnect was enabled in	scsi_options
		 */
		if (sp->cmd_pkt_flags &	FLAG_NODISCON) {
			msgout = MSG_REJECT;
			break;
		}
		/* FALLTHROUGH */
	case MSG_COMMAND_COMPLETE:
		fas->f_state = ACTS_CLEARING;
		break;

	/* XXXX? This	has been taken care of above	*/
	/* case	MSG_EXTENDED:			*/

	case MSG_NOP:
		break;

	/* XXX Make it a MSG_REJECT handler */
	case MSG_REJECT:
	{
		u_char reason =	0;
		u_char lastmsg = fas->f_last_msgout;
		/*
		 * The target is rejecting the last message we sent.
		 *
		 * If the last message we attempted to send out	was an
		 * extended message, we	were trying to negotiate sync
		 * xfers- and we're okay.
		 *
		 * Otherwise, a	target has rejected a message that
		 * it should have handled. We will abort the operation
		 * in progress and set the pkt_reason value here to
		 * show	why we have completed. The process of aborting
		 * may be via a	message	or may be via a	bus reset (as
		 * a last resort).
		 */
		msgout = (TAGGED(tgt)? MSG_ABORT_TAG : MSG_ABORT);

		switch (lastmsg) {
		case MSG_EXTENDED:
			fas_reset_sync_wide(fas);
			if (fas->f_wdtr_sent) {
				/*
				 * we still want to negotiate sync
				 */
				fas_reg_cmd_write(fas, CMD_SET_ATN);
				fas_make_sdtr(fas, 0, tgt);
				fas->f_wdtr_sent = 0;
			} else if (fas->f_sdtr_sent) {
				fas_reg_cmd_write(fas, CMD_CLR_ATN);
				fas->f_sdtr_sent = 0;
			}
			msgout = 0;
			break;
		case MSG_NOP:
			reason = CMD_NOP_FAIL;
			break;
		case MSG_INITIATOR_ERROR:
			reason = CMD_IDE_FAIL;
			break;
		case MSG_MSG_PARITY:
			reason = CMD_PER_FAIL;
			break;
		case MSG_REJECT:
			reason = CMD_REJECT_FAIL;
			break;
		/* XXX - abort not good, queue full handling or drain (?) */
		case MSG_SIMPLE_QTAG:
		case MSG_ORDERED_QTAG:
		case MSG_HEAD_QTAG:
			msgout = MSG_ABORT;
			reason = CMD_TAG_REJECT;
			break;
		case MSG_DEVICE_RESET:
			reason = CMD_BDR_FAIL;
			msgout = -ACTION_ABORT_CURCMD;
			break;
		case MSG_ABORT:
		case MSG_ABORT_TAG:
			/*
			 * If an RESET/ABORT OPERATION message is rejected
			 * it is time to yank the chain	on the bus...
			 */
			reason = CMD_ABORT_FAIL;
			msgout = -ACTION_ABORT_CURCMD;
			break;
		default:
			if (IS_IDENTIFY_MSG(lastmsg)) {
				if (TAGGED(tgt)) {
					/*
					 * this	often happens when the
					 * target rejected our tag
					 */
					reason = CMD_TAG_REJECT;
				} else {
					reason = CMD_ID_FAIL;
				}
			} else {
				reason = CMD_TRAN_ERR;
				msgout = -ACTION_ABORT_CURCMD;
			}

			break;
		}

		if (msgout) {
			fas_log(fas, CE_WARN,
			    "Target %d rejects our message '%s'",
			    tgt, scsi_mname(lastmsg));
			fas_set_pkt_reason(fas,	sp, reason, 0);
		}

		break;
	}
	case MSG_RESTORE_PTRS:
		sp->cmd_cdbp = sp->cmd_pkt->pkt_cdbp;
		if (sp->cmd_data_count != sp->cmd_saved_data_count) {
			if (fas_restore_pointers(fas, sp)) {
				msgout = -ACTION_ABORT_CURCMD;
			}
		}
		break;

	case MSG_SAVE_DATA_PTR:
		sp->cmd_saved_data_count = sp->cmd_data_count;
		sp->cmd_saved_win = sp->cmd_cur_win;
		sp->cmd_saved_cur_addr = sp->cmd_cur_addr;
		break;

	/* These don't make sense for us, and	*/
	/* will	be rejected			*/
	/*	case MSG_INITIATOR_ERROR	*/
	/*	case MSG_ABORT			*/
	/*	case MSG_MSG_PARITY		*/
	/*	case MSG_DEVICE_RESET		*/
	default:
		msgout = MSG_REJECT;
		fas_log(fas, CE_WARN,
		    "Rejecting message '%s' from Target %d",
		    scsi_mname(msgin), tgt);
		break;
	}

	EPRINTF1("Message in: %s\n", scsi_mname(msgin));

	return (msgout);
}

/*
 * phase handlers that are rarely used
 */
static int
fas_handle_cmd_start(struct fas	*fas)
{
	struct fas_cmd *sp = fas->f_current_sp;
	register caddr_t tp = (caddr_t)fas->f_cmdarea;
	register int i;
	register int amt = sp->cmd_cdblen;

	TRACE_0(TR_FAC_SCSI, TR_FAS_HANDLE_CMD_START_START,
	    "fas_handle_cmd_start_start");
	EPRINTF("fas_handle_cmd: send cmd\n");

	for (i = 0; i <	amt; i++) {
		*tp++ =	sp->cmd_cdbp[i];
	}
	fas_reg_cmd_write(fas, CMD_FLUSH);

	FAS_DMA_READ(fas, amt, (u_long)fas->f_dmacookie.dmac_address, amt,
	    CMD_TRAN_INFO|CMD_DMA);
	fas->f_lastcount = amt;

	New_state(fas, ACTS_CMD_DONE);

	TRACE_0(TR_FAC_SCSI, TR_FAS_HANDLE_CMD_START_END,
	    "fas_handle_cmd_start_end");
	return (ACTION_RETURN);
}

static int
fas_handle_cmd_done(register struct fas	*fas)
{
	register struct	fas_cmd	*sp = fas->f_current_sp;
	register u_char	intr = fas->f_intr;
	register volatile struct dma *dmar = fas->f_dma;

	TRACE_0(TR_FAC_SCSI, TR_FAS_HANDLE_CMD_DONE_START,
	    "fas_handle_cmd_done_start");
	EPRINTF("fas_handle_cmd_done\n");

	/*
	 * We should have gotten a BUS SERVICE interrupt.
	 * If it isn't that, and it isn't a DISCONNECT
	 * interrupt, we have a	"cannot	happen"	situation.
	 */
	if ((intr & FAS_INT_BUS) == 0) {
		if ((intr & FAS_INT_DISCON) == 0) {
			fas_printstate(fas, "cmd transmission error");
			TRACE_0(TR_FAC_SCSI, TR_FAS_HANDLE_CMD_DONE_ABORT1_END,
			    "fas_handle_cmd_done_end (abort1)");
			return (ACTION_ABORT_CURCMD);
		}
	} else {
		sp->cmd_pkt->pkt_state |= STATE_SENT_CMD;
	}

	fas->f_dma_csr = fas_dma_reg_read(fas, &dmar->dma_csr);
	FAS_FLUSH_DMA(fas);

	New_state(fas, ACTS_UNKNOWN);
	TRACE_0(TR_FAC_SCSI, TR_FAS_HANDLE_CMD_DONE_END,
	    "fas_handle_cmd_done_end");
	return (fas_handle_unknown(fas));
}

/*
 * Begin to send a message out
 */
static int
fas_handle_msg_out_start(register struct fas *fas)
{
	register struct	fas_cmd	*sp = fas->f_current_sp;
	register volatile struct fasreg	*fasreg	= fas->f_reg;
	register u_char	*msgout	= fas->f_cur_msgout;
	char amt = fas->f_omsglen;

	TRACE_0(TR_FAC_SCSI, TR_FAS_HANDLE_MSG_OUT_START,
	    "fas_handle_msg_out_start");
	EPRINTF("fas_handle_msg_out_start\n");

	/*
	 * Check to make *sure*	that we	are really
	 * in MESSAGE OUT phase. If the	last state
	 * was ACTS_MSG_OUT_DONE, then we are trying
	 * to resend a message that the	target stated
	 * had a parity	error in it.
	 *
	 * If this is the case,	and mark completion reason as CMD_NOMSGOUT.
	 * XXX:	Right now, we just *drive* on. Should we abort the command?
	 */
	if ((fas->f_stat & FAS_PHASE_MASK) != FAS_PHASE_MSG_OUT	&&
	    fas->f_laststate ==	ACTS_MSG_OUT_DONE) {
		fas_log(fas, CE_WARN,
		    "Target %d refused message resend",	Tgt(sp));
		fas_set_pkt_reason(fas,	sp, CMD_NOMSGOUT, 0);
		New_state(fas, ACTS_UNKNOWN);
		TRACE_0(TR_FAC_SCSI, TR_FAS_HANDLE_MSG_OUT_PHASEMANAGE_END,
		    "fas_handle_msg_out_end (ACTION_PHASEMANAGE)");
		return (ACTION_PHASEMANAGE);
	}

	/*
	 * Clean the fifo.
	 */
	fas_reg_cmd_write(fas, CMD_FLUSH);

	if (amt	== 0) {
		/*
		 * no msg to send
		 */
		*msgout	= MSG_NOP;
		amt = fas->f_omsglen = 1;
	}

	/*
	 * If msg only 1 byte, just dump it in the fifo	and go.	 For
	 * multi-byte msgs, dma	them to	save time.  If we have no
	 * msg to send and we're in msg	out phase, send	a NOP.
	 */
	fas->f_last_msgout = *msgout;

	if (amt	== 1) {
		fas_reg_write(fas, (u_char *)&fasreg->fas_fifo_data, *msgout);
		fas_reg_write(fas, (u_char *)&fasreg->fas_fifo_data, 0);
		fas_reg_cmd_write(fas, CMD_TRAN_INFO);

	} else {
#ifdef ATN_DEASSERT_HACK
		if (fas_enable_atn_deassert_workaround) {
			/*
			 * There is a bug in the fas366	that occasionaly
			 * deasserts the ATN signal prematurely when we send
			 * the sync/wide negotiation bytes out using DMA. The
			 * workaround here is to send the negotiation bytes out
			 * using PIO
			 */
			fas_write_fifo(fas, msgout, fas->f_omsglen, 1);
			fas_reg_cmd_write(fas, CMD_TRAN_INFO);
		} else {
			register caddr_t tp = (caddr_t)fas->f_cmdarea;
			register char i;

			for (i = 0; i <	amt; i++)
				*tp++ =	*msgout++;
			FAS_DMA_READ(fas, amt,
				(u_long)fas->f_dmacookie.dmac_address, amt+2,
				CMD_TRAN_INFO|CMD_DMA);
			fas->f_lastcount = amt;
		}
#else
		register caddr_t tp = (caddr_t)fas->f_cmdarea;
		register char i;

		for (i = 0; i <	amt; i++)
			*tp++ =	*msgout++;
		FAS_DMA_READ(fas, amt,
			(u_long)fas->f_dmacookie.dmac_address, amt+2,
			CMD_TRAN_INFO|CMD_DMA);
		fas->f_lastcount = amt;
#endif
	}
	EPRINTF2("amt=%x, last_msgout=%x\n", amt, fas->f_last_msgout);

	New_state(fas, ACTS_MSG_OUT_DONE);
	TRACE_0(TR_FAC_SCSI, TR_FAS_HANDLE_MSG_OUT_END,
	    "fas_handle_msg_out_end");
	return (ACTION_RETURN);
}

static int
fas_handle_msg_out_done(register struct	fas *fas)
{
	register struct	fas_cmd	*sp = fas->f_current_sp;
	register u_char	msgout,	phase;
	register int target = Tgt(sp);
	int	amt = fas->f_omsglen;
	int action;

	TRACE_0(TR_FAC_SCSI, TR_FAS_HANDLE_MSG_OUT_DONE_START,
	    "fas_handle_msg_out_done_start");
	msgout = fas->f_cur_msgout[0];
	if ((msgout == MSG_HEAD_QTAG) || (msgout == MSG_SIMPLE_QTAG)) {
		msgout = fas->f_cur_msgout[2];
	}
	EPRINTF4("msgout: %x %x %x, last_msgout=%x\n",
		fas->f_cur_msgout[0], fas->f_cur_msgout[1],
		fas->f_cur_msgout[2], fas->f_last_msgout);

	EPRINTF1("fas_handle_msgout_done: msgout=%x\n",	msgout);

#ifndef	ATN_DEASSERT_HACK
	/*
	 * If we dma'ed	out the	msg, we	have a little cleanup to do...
	 */
	if (amt	> 1) {
		FAS_FLUSH_DMA(fas);
	}
#else
	if (fas_enable_atn_deassert_workaround == 0) {
		/*
		 * If we dma'ed	out the	msg, we	have a little cleanup to do...
		 */
		if (amt	> 1) {
			FAS_FLUSH_DMA(fas);
		}
	} else {
		/*
		 * flush fifo, just in case some bytes were not	sent
		 */
		fas_reg_cmd_write(fas, CMD_FLUSH);
	}
#endif

	/*
	 * If the FAS disconnected, then the message we	sent caused
	 * the target to decide	to drop	BSY* and clear the bus.
	 */
	if (fas->f_intr	== FAS_INT_DISCON) {
		if (msgout == MSG_DEVICE_RESET || msgout == MSG_ABORT ||
		    msgout == MSG_ABORT_TAG) {
			/*
			 * If we sent a	device reset msg, then we need to do
			 * a synch negotiate again unless we have already
			 * inhibited synch.
			 */
#ifdef FAS_TEST
			if (fas_test_stop)
				debug_enter("message abort/reset success");
#endif
			if (msgout == MSG_ABORT	|| msgout == MSG_ABORT_TAG) {
				fas->f_abort_msg_sent++;
				if ((sp->cmd_flags & CFLAG_CMDPROXY) ==	0) {
					fas_set_pkt_reason(fas,	sp,
					    CMD_ABORTED, STAT_ABORTED);
				}
			} else if (msgout == MSG_DEVICE_RESET) {
				fas->f_reset_msg_sent++;
				if ((sp->cmd_flags & CFLAG_CMDPROXY) ==	0) {
					fas_set_pkt_reason(fas,	sp,
					    CMD_RESET, STAT_DEV_RESET);
				}
				fas_force_renegotiation(fas, Tgt(sp));
			}
			EPRINTF2("Successful %s message to target %d\n",
			    scsi_mname(msgout),	target);

			if (sp->cmd_flags & CFLAG_CMDPROXY) {
				sp->cmd_cdb[FAS_PROXY_RESULT] =	TRUE;
			}
			TRACE_0(TR_FAC_SCSI,
			    TR_FAS_HANDLE_MSG_OUT_DONE_FINISH_END,
			    "fas_handle_msg_out_done_end (ACTION_FINISH)");
			return (ACTION_FINISH);
		}
		/*
		 * If the target dropped busy on any other message, it
		 * wasn't expected. We will let	the code in fas_phasemanage()
		 * handle this unexpected bus free event.
		 */
		goto out;
	}

	/*
	 * What	phase have we transitioned to?
	 */
	phase =	fas->f_stat & FAS_PHASE_MASK;

	/*
	 * If we finish	sending	a message out, and we are
	 * still in message out	phase, then the	target has
	 * detected one	or more	parity errors in the message
	 * we just sent	and it is asking us to resend the
	 * previous message.
	 */
	if ((fas->f_intr & FAS_INT_BUS)	&& phase == FAS_PHASE_MSG_OUT) {
		/*
		 * As per SCSI-2 specification,	if the message to
		 * be re-sent is greater than one byte,	then we
		 * have	to set ATN*.
		 */
		if (amt	> 1) {
			fas_reg_cmd_write(fas, CMD_SET_ATN);
		}
		fas_log(fas, CE_WARN,
		    "SCSI bus MESSAGE OUT phase parity error");
		sp->cmd_pkt->pkt_statistics |= STAT_PERR;
		New_state(fas, ACTS_MSG_OUT);
		TRACE_0(TR_FAC_SCSI, TR_FAS_HANDLE_MSG_OUT_DONE_PHASEMANAGE_END,
		    "fas_handle_msg_out_done_end (ACTION_PHASEMANAGE)");
		return (ACTION_PHASEMANAGE);
	}


out:
	fas->f_last_msgout = msgout;
	fas->f_omsglen = 0;
	New_state(fas, ACTS_UNKNOWN);
	action = fas_handle_unknown(fas);
	TRACE_0(TR_FAC_SCSI, TR_FAS_HANDLE_MSG_OUT_DONE_END,
	    "fas_handle_msg_out_done_end");
	return (action);
}

static int
fas_twobyte_msg(struct fas *fas)
{
	register struct	fas_cmd	*sp = fas->f_current_sp;

	if ((fas->f_imsgarea[0]	== MSG_IGNORE_WIDE_RESID) &&
	    (fas->f_imsgarea[1]	== 1)) {
		int xfer_amt;
		/*
		 * the fas366/dma transfers the	exact transfer
		 * count that is  programmed in	xfer counters so there is
		 * no need to adjust the current total xfer count
		 * for this command unless the xfer_amt	is non-zero
		 * in which case the extra byte	has been xferred.
		 */
		GET_FAS_COUNT(fas->f_reg, xfer_amt);
		if (xfer_amt) {
			ASSERT(sp->cmd_dmacount	> 0);
			sp->cmd_dmacount--;
		}
		IPRINTF1("ignore wide resid %d\n", fas->f_imsgarea[1]);
		New_state(fas, ACTS_UNKNOWN);
		return (0);
	}

	fas_log(fas, CE_WARN,
	    "Two byte message '%s' 0x%x rejected",
	    scsi_mname(fas->f_imsgarea[0]), fas->f_imsgarea[1]);
	return (MSG_REJECT);
}

/*
 * handle receiving extended messages
 */
static int
fas_multibyte_msg(register struct fas *fas)
{
#ifdef FASDEBUG
	static char *mbs =
	    "Target %d now Synchronous at %d.%d MB/s max transmit rate\n";
	static char *mbs1 =
	    "Target %d now Synchronous at %d.0%d MB/s max transmit rate\n";
	static char *mbs2 =
	    "Target %d now Synchronous at %d.00%d MB/s max transmit rate\n";
#endif
	register struct	fas_cmd	*sp = fas->f_current_sp;
	register volatile struct fasreg	*fasreg	= fas->f_reg;
	u_char emsg = fas->f_imsgarea[2];
	int tgt	= Tgt(sp);
	register msgout	= 0;

	EPRINTF("fas_multibyte_msg:\n");

	if (emsg == MSG_SYNCHRONOUS) {
		u_int period, offset, regval;
		u_int minsync, maxsync,	clockval;
		u_int xfer_freq, xfer_div, xfer_mod, xfer_rate;

		period = fas->f_imsgarea[3] & 0xff;
		offset = fas->f_imsgarea[4] & 0xff;
		minsync	= MIN_SYNC_PERIOD(fas);
		maxsync	= MAX_SYNC_PERIOD(fas);
		DPRINTF5("sync msg received: %x %x %x %x %x\n",
		    fas->f_imsgarea[0],	fas->f_imsgarea[1],
		    fas->f_imsgarea[2],	fas->f_imsgarea[3],
		    fas->f_imsgarea[4]);
		DPRINTF3("received period %d offset %d from tgt %d\n",
		    period, offset, tgt);
		DPRINTF3("calculated minsync %d, maxsync %d for tgt %d\n",
		    minsync, maxsync, tgt);
		DPRINTF2("sync period %d, neg period %d\n",
		    fas->f_sync_period[tgt], fas->f_neg_period[tgt]);

		if ((++(fas->f_sdtr_sent)) &	1) {
			/*
			 * In cases where the target negotiates	synchronous
			 * mode	before we do, and we either have sync mode
			 * disabled, or	this target is known to	be a weak
			 * signal target, we send back a message indicating
			 * a desire to stay in asynchronous mode (the SCSI-2
			 * spec	states that if we have synchronous capability
			 * then	we cannot reject a SYNCHRONOUS DATA TRANSFER
			 * REQUEST message).
			 */
			IPRINTF1("SYNC negotiation initiated by target %d\n",
			    tgt);

			msgout = MSG_EXTENDED;
			period = max(period, fas->f_neg_period[tgt]);
			offset = min(offset, fas->f_offset[tgt]);
		}
		xfer_freq = regval = 0;

		/*
		 * If the target's offset is bigger than ours,
		 * that's okay.	We just	set to our maximum.
		 */
		if (offset > fas_default_offset) {
			offset = fas_default_offset;
		}

		if (offset && (period >	maxsync)) {
			/*
			 * We cannot transmit data in synchronous
			 * mode	this slow, so convert to asynchronous
			 * mode.
			 */
			msgout = MSG_EXTENDED;
			period = offset	= 0;

		} else if (offset && (period < minsync)) {
			/*
			 * If the target's period is less than ours,
			 * that's okay.	We can only transmit so	fast.
			 */
			period = minsync;
			regval = MIN_SYNC(fas);

		} else if (offset) {
			/*
			 * Conversion method for received PERIOD value
			 * to the number of input clock	ticks to the FAS.
			 *
			 * We adjust the input period value such that
			 * we always will transmit data	*not* faster
			 * than	the period value received.
			 */

			clockval = fas->f_clock_cycle /	1000;
			regval = (((period << 2) + clockval - 1) / clockval);

			/*
			 * correction if xfer rate <= 5MB/sec
			 * XXX do we need this?
			 */
			if (regval && (period >= FASTSCSI_THRESHOLD)) {
				regval--;
			}
		}

		if (offset) {
			fas->f_sync_period[tgt]	= regval & SYNC_PERIOD_MASK;
			fas_reg_write(fas, (u_char *)&fasreg->fas_sync_period,
			    fas->f_sync_period[tgt]);

			fas->f_offset[tgt] = offset | fas->f_req_ack_delay;
			fas_reg_write(fas, (u_char *)&fasreg->fas_sync_offset,
				fas->f_offset[tgt]);

			/*
			 * if transferring > 5 MB/sec then enable
			 * fastscsi in conf3
			 */
			if (period < FASTSCSI_THRESHOLD) {
				fas->f_fasconf3[tgt] |=	FAS_CONF3_FASTSCSI;
			} else {
				fas->f_fasconf3[tgt] &=	~FAS_CONF3_FASTSCSI;
			}

			fas_reg_write(fas, (u_char *)&fasreg->fas_conf3,
			    fas->f_fasconf3[tgt]);

			DPRINTF4("period %d (%d), offset %d to tgt %d\n",
				period,
				fas->f_sync_period[tgt]	& SYNC_PERIOD_MASK,
				fas->f_offset[tgt] & 0xf, tgt);
			DPRINTF1("req/ack delay = %x\n", fas->f_req_ack_delay);
			DPRINTF1("conf3 = %x\n", fas->f_fasconf3[tgt]);
#ifdef FASDEBUG
			/*
			 * Convert input clock cycle per
			 * byte	to nanoseconds per byte.
			 * (ns/b), and convert that to
			 * k-bytes/second.
			 */
			xfer_freq = FAS_SYNC_KBPS((regval *
				fas->f_clock_cycle) / 1000);
			xfer_rate = ((fas->f_nowide & (1<<tgt))? 1 : 2)	*
						xfer_freq;
			xfer_div = xfer_rate / 1000;
			xfer_mod = xfer_rate % 1000;


			if (xfer_mod > 99) {
				IPRINTF3(mbs, tgt, xfer_div, xfer_mod);
			} else if (xfer_mod > 9) {
				IPRINTF3(mbs1, tgt, xfer_div, xfer_mod);
			} else {
				IPRINTF3(mbs2, tgt, xfer_div, xfer_mod);
			}
			fas->f_sync_enabled |= (1<<tgt);
#endif

		} else {
			/*
			 * We are converting back to async mode.
			 */
			fas->f_sync_period[tgt]	= 0;
			fas_reg_write(fas,
				(u_char	*)&fasreg->fas_sync_period, 0);
			fas_reg_write(fas,
				(u_char	*)&fasreg->fas_sync_offset, 0);
			fas->f_offset[tgt] = 0;
			fas->f_fasconf3[tgt] &=	~FAS_CONF3_FASTSCSI;
			fas_reg_write(fas, &fasreg->fas_conf3,
				fas->f_fasconf3[tgt]);
			fas->f_sync_enabled &= ~(1<<tgt);
		}

		if (msgout) {
			fas_make_sdtr(fas, 0, tgt);
		}

		fas->f_props_update |= (1<<tgt);

	} else	if (emsg == MSG_WIDE_DATA_XFER)	{
		u_char width = fas->f_imsgarea[3] & 0xff;

		DPRINTF4("wide msg received: %x %x %x %x\n",
		    fas->f_imsgarea[0],	fas->f_imsgarea[1],
		    fas->f_imsgarea[2],	fas->f_imsgarea[3]);

		/* always renegotiate sync after wide */
		msgout = MSG_EXTENDED;

		if ((++(fas->f_wdtr_sent)) &	1) {
			IPRINTF1("Wide negotiation initiated by target %d\n",
			    tgt);
			fas_make_wdtr(fas, 0, tgt, width);
			IPRINTF1("sending wide sync %d back\n",	width);
		} else {
			/*
			 * renegotiate sync after wide
			 */
			fas_set_wide_conf3(fas,	tgt, width);
			ASSERT(width <=	1);
			fas->f_wdtr_sent = 0;
			fas_make_sdtr(fas, 0, tgt);
		}

		fas->f_props_update |= (1<<tgt);

	} else if (emsg	== MSG_MODIFY_DATA_PTR)	{
		msgout = MSG_REJECT;
	} else {
		fas_log(fas, CE_WARN,
		    "Rejecting message %s 0x%x from Target %d",
		    scsi_mname(MSG_EXTENDED), emsg, tgt);
		msgout = MSG_REJECT;
	}
out:
	New_state(fas, ACTS_UNKNOWN);
	return (msgout);
}

/*
 * handle an unexpected	selection attempt
 * XXX look for better way: msg reject, drop off the bus
 */
static int
fas_handle_selection(struct fas	*fas)
{
	fas_log(fas, CE_WARN, "Unexpected Selection Attempt");
	return (ACTION_RESET);
}

/*
 * dma window handling
 */
static int
fas_restore_pointers(struct fas	*fas, struct fas_cmd *sp)
{
	if (sp->cmd_data_count != sp->cmd_saved_data_count) {
		sp->cmd_data_count = sp->cmd_saved_data_count;
		sp->cmd_cur_addr = sp->cmd_saved_cur_addr;

		if (sp->cmd_cur_win != sp->cmd_saved_win) {
			sp->cmd_cur_win	= sp->cmd_saved_win;
			if (fas_set_new_window(fas, sp)) {
				return (-1);
			}
		}
		DPRINTF1("curaddr=%x\n", sp->cmd_cur_addr);
	}
	return (0);
}

static int
fas_set_new_window(struct fas *fas, struct fas_cmd *sp)
{
	off_t offset;
	u_int len;
	u_int count;

	if (ddi_dma_getwin(sp->cmd_dmahandle, sp->cmd_cur_win,
	    &offset, &len, &sp->cmd_dmacookie, &count) != DDI_SUCCESS) {
		return (-1);
	}

	DPRINTF4("new window %x: off=%x, len=%x, count=%x\n",
	    sp->cmd_cur_win, offset, len, count);

	ASSERT(count ==	1);
	return (0);
}

static int
fas_next_window(struct fas *fas, struct	fas_cmd	*sp, u_int end)
{

	/* are there more windows? */
	if (sp->cmd_nwin == 0) {
		u_int nwin = 0;
		(void) ddi_dma_numwin(sp->cmd_dmahandle, &nwin);
		sp->cmd_nwin = (u_char)nwin;
	}

	DPRINTF5(
	    "cmd_data_count=%x, dmacount=%x, curaddr=%x, end=%x, nwin=%x\n",
	    sp->cmd_data_count,	sp->cmd_dmacount, sp->cmd_cur_addr, end,
	    sp->cmd_nwin);

#ifdef BREAK_XFER_HACK
	if (fas_break_xfer_flag == 1) {
		EPRINTF("break odd count xfer: transferring one byte\n");
		fas_break_xfer_flag = 0;
		return (0);
	}
#endif
	if (sp->cmd_cur_win < sp->cmd_nwin) {
		sp->cmd_cur_win++;
		if (fas_set_new_window(fas, sp)) {
			fas_printstate(fas, "cannot set new window");
			sp->cmd_cur_win--;
			return (-1);
		}
	/*
	 * if there are	no more	windows, we have a data	overrun	condition
	 */
	} else {
		register int slot = sp->cmd_slot;

		fas_printstate(fas, "data transfer overrun");
		fas_set_pkt_reason(fas,	sp, CMD_DATA_OVR, 0);

		/*
		 * if we get data transfer overruns, assume we have
		 * a weak scsi bus. Note that this won't catch consistent
		 * underruns or	other noise related syndromes.
		 */
		fas_sync_wide_backoff(fas, sp, slot);
		return (-1);
	}
	sp->cmd_cur_addr = sp->cmd_dmacookie.dmac_address;
	DPRINTF1("cur_addr=%x\n", sp->cmd_cur_addr);
	return (0);
}

/*
 * dma error handler
 */
static int
fas_check_dma_error(register struct fas	*fas)
{
	/*
	 * was there a dma error that	caused fas_intr_svc() to be called?
	 */
	if (fas->f_dma->dma_csr	& DMA_ERRPEND) {
		/*
		 * It would be desirable to set	the ATN* line and attempt to
		 * do the whole	schmear	of INITIATOR DETECTED ERROR here,
		 * but that is too hard	to do at present.
		 */
		fas_log(fas, CE_WARN, "Unrecoverable DMA error");
		fas_printstate(fas, "dma error");
		fas_set_pkt_reason(fas,	fas->f_current_sp, CMD_TRAN_ERR, 0);
		return (-1);
	}
	return (0);
}

/*
 * check for gross error or spurious interrupt
 */
static int
fas_handle_gross_err_spurious_intr(struct fas *fas)
{
	register volatile struct fasreg	*fasreg	= fas->f_reg;

	if (fas->f_stat	& FAS_STAT_GERR) {
		fas_log(fas, CE_WARN,
		    "gross error in fas status (%x)", fas->f_stat);

		IPRINTF5("fas_cmd=%x, stat=%x, intr=%x, step=%x, fifoflag=%x\n",
		    fasreg->fas_cmd, fas->f_stat, fas->f_intr, fasreg->fas_step,
		    fasreg->fas_fifo_flag);

		fas_set_pkt_reason(fas,	fas->f_current_sp, CMD_TRAN_ERR, 0);

		fas_internal_reset(fas,	FAS_RESET_FAS);
		return (ACTION_RESET);
	}

	if ((fas->f_stat & FAS_STAT_IPEND) == 0) {
		fas->f_intr = fas_reg_read(fas,	&fasreg->fas_intr);
		if (fas->f_intr	& FAS_INT_RESET) {
			return (ACTION_FINRST);
		}

		if (fas_check_dma_error(fas)) {
			return (ACTION_RESET);
		}

		fas_log(fas, CE_WARN, "Spurious interrupt");
		return (ACTION_RETURN);
	}
	_NOTE(NOT_REACHED)
	/* NOTREACHED */
}

/*
 * set throttles for all luns of this target
 */
static void
fas_set_throttles(struct fas *fas, int slot, int n, int	what)
{
	register int i;

	ASSERT((n == 1)	|| (n == N_SLOTS) || (n	== NLUNS_PER_TARGET));
	ASSERT((slot + n) <= N_SLOTS);
	if (n == NLUNS_PER_TARGET) {
		slot &=	~(NLUNS_PER_TARGET - 1);
	}

	for (i = slot; i < (slot + n); i++) {
		if (what == HOLD_THROTTLE) {
			fas->f_throttle[i] = HOLD_THROTTLE;
		} else if ((fas->f_reset_delay[slot/NLUNS_PER_TARGET]) == 0) {
			if (what == MAX_THROTTLE) {
				int tshift = 1 << (slot/NLUNS_PER_TARGET);
				fas->f_throttle[i] = (short)
					((fas->f_notag & tshift)? 1 : what);
			} else {
				fas->f_throttle[i] = what;
			}
		}
	}
}

static void
fas_full_throttle(struct fas *fas, int slot)
{
	fas_set_throttles(fas, slot, 1,	MAX_THROTTLE);
}

/*
 * restore throttle unless reset delay in progress
 */
static void
fas_restore_throttles(struct fas *fas, int slot, int n,	short *throttles)
{
	register int i;

	ASSERT((n == 1)	|| (n == N_SLOTS) || (n	== NLUNS_PER_TARGET));
	ASSERT((slot + n) <= N_SLOTS);
	if (n == NLUNS_PER_TARGET) {
		slot &=	~(NLUNS_PER_TARGET - 1);
	}

	for (i = slot; i < (slot + n); i++) {
		if (fas->f_reset_delay[slot/NLUNS_PER_TARGET] == 0) {
			if (throttles[i	- slot]	== DRAIN_THROTTLE) {
				fas_full_throttle(fas, i);
			} else {
				fas->f_throttle[i] = throttles[i - slot];
			}
		}
	}
}

/*
 * save	throttles
 */
static void
fas_save_throttles(struct fas *fas, int	slot, int n, short *throttles)
{
	ASSERT((n == 1)	|| (n == N_SLOTS) || (n	== NLUNS_PER_TARGET));
	ASSERT((slot + n) <= N_SLOTS);
	if (n == NLUNS_PER_TARGET) {
		slot &=	~(NLUNS_PER_TARGET - 1);
	}
	bcopy((caddr_t)&fas->f_throttle[slot], (caddr_t)throttles,
	    n *	sizeof (fas->f_throttle[slot]));
}


/*
 * run a polled	cmd
 */
static void
fas_runpoll(register struct fas	*fas, short slot, struct fas_cmd *sp)
{
	int limit, i, n;
	int timeout = 0;

	DPRINTF4("runpoll: slot=%x, cmd=%x, current_sp=%x, tcmds=%x\n",
		slot, *((u_char	*)sp->cmd_pkt->pkt_cdbp),
		fas->f_current_sp, fas->f_tcmds[slot]);

	TRACE_0(TR_FAC_SCSI, TR_FAS_RUNPOLL_START, "fas_runpoll_start");

	/*
	 * wait	for cmd	to complete
	 * don't start new cmds	so set throttles to HOLD_THROTTLE
	 */
	while ((sp->cmd_flags &	CFLAG_COMPLETED) == 0) {
		fas_set_throttles(fas, slot, NLUNS_PER_TARGET, HOLD_THROTTLE);
		if ((fas->f_state != STATE_FREE) || INTPENDING(fas)) {
			if (fas_dopoll(fas, POLL_TIMEOUT) <= 0)	{
				IPRINTF("runpoll: timeout on draining\n");
				goto bad;
			}
		}

		ASSERT(fas->f_state == STATE_FREE);
		ASSERT(fas->f_current_sp == NULL);

		/*
		 * if this is not a proxy cmd, don't start the cmd
		 * without draining the	active cmd(s)
		 * for proxy cmds, we zap the active cmd and assume
		 * that	the caller will	take care of this
		 * For tagged cmds, wait with submitting a non-tagged
		 * cmd until the queue has been	drained
		 * If the cmd is a request sense, then draining	won't
		 * help	since we are in	contingence allegiance condition
		 */
		if (!(sp->cmd_flags & CFLAG_CMDPROXY)) {
			u_char *cmdp = (u_char *) sp->cmd_pkt->pkt_cdbp;

			if ((fas->f_tcmds[slot]) &&
			    (NOTAG(Tgt(sp)) ||
			    (((sp->cmd_pkt_flags & FLAG_TAGMASK) == 0) &&
			    (*cmdp != SCMD_REQUEST_SENSE)))) {
				if (timeout < POLL_TIMEOUT) {
					timeout	+= 100;
					drv_usecwait(100);
					continue;
				} else {
					fas_log(fas, CE_WARN,
					    "polled cmd failed (target busy)");
					goto cleanup;
				}
			}
		}

		/*
		 * If the draining of active commands killed the
		 * the current polled command, we're done..
		 */
		if (sp->cmd_flags & CFLAG_COMPLETED) {
			break;
		}

		/*
		 * ensure we are not accessing a target	too quickly
		 * after a reset. the throttles	get set	back later
		 * by the reset	delay watch; hopefully,	we don't go
		 * thru	this loop more than once
		 */
		if (fas->f_reset_delay[slot/NLUNS_PER_TARGET]) {
			IPRINTF1("reset delay set for slot %x\n", slot);
			drv_usecwait(fas->f_scsi_reset_delay * 1000);
			for (i = 0; i <	NTARGETS_WIDE; i++) {
				if (fas->f_reset_delay[i]) {
					int s =	i * NLUNS_PER_TARGET;
					int e =	s + NLUNS_PER_TARGET;
					fas->f_reset_delay[i] =	0;
					for (; s < e; s++) {
						fas_full_throttle(fas, i);
					}
				}
			}
		}

		/*
		 * fas_startcmd() will return false if preempted
		 */
		if (fas_startcmd(fas, sp) != TRUE) {
			if ((fas->f_state == STATE_FREE) &&
			    INTPENDING(fas) == 0) {
				/*
				 * XXX	this should not	happen but if it
				 * does	it could get us	in an indefinite loop
				 */
				IPRINTF("runpoll: cannot start new cmds\n");
				goto bad;
			}
			ASSERT(fas->f_current_sp != sp);
			continue;
		}

		/*
		 * We're now 'running' this command.
		 *
		 * fas_dopoll will always return when
		 * fas->f_state	is STATE_FREE, and
		 */
		limit =	sp->cmd_pkt->pkt_time *	1000000;
		if (limit == 0)	{
			limit =	POLL_TIMEOUT;
		}

		/*
		 * if the cmd disconnected, the	first call to fas_dopoll
		 * will	return with bus	free; we go thru the loop one more
		 * time	and wait limit usec for	the target to reconnect
		 */
		for (i = 0; i <= POLL_TIMEOUT; i += 100) {

			if ((n = fas_dopoll(fas, limit)) <= 0) {
				IPRINTF("runpoll: timeout on polling\n");
				goto bad;
			}

			/*
			 * If a	preemption occurred that caused	this
			 * command to actually not start, go around
			 * the loop again. If CFLAG_COMPLETED is set, the
			 * command completed
			 */
			if ((sp->cmd_flags & CFLAG_COMPLETED) ||
			    (sp->cmd_pkt->pkt_state == 0)) {
				break;
			}

			/*
			 * the bus may have gone free because the target
			 * disconnected; go thru the loop again
			 */
			ASSERT(fas->f_state == STATE_FREE);
			if (n == 0) {
				/*
				 * bump	i, we have waited limit	usecs in
				 * fas_dopoll
				 */
				i += limit - 100;
			}
		}

		if ((sp->cmd_flags & CFLAG_COMPLETED) == 0) {

			if (i >	POLL_TIMEOUT) {
				IPRINTF("polled timeout on disc. cmd\n");
				goto bad;
			}

			if (sp->cmd_pkt->pkt_state) {
				/*
				 * don't go thru the loop again; the cmd
				 * was already started
				 */
				IPRINTF("fas_runpoll: cmd started??\n");
				goto bad;
			}
		}
	}

	/*
	 * blindly restore throttles which is preferable over
	 * leaving throttle hanging at 0 and noone to clear it
	 */
	fas_set_throttles(fas, slot, NLUNS_PER_TARGET, MAX_THROTTLE);

	/*
	 * ensure that the cmd is completely removed
	 */
	fas_remove_cmd(fas, sp,	0);

	/*
	 * If we stored	up commands to do, start them off now.
	 */
	if ((fas->f_state == STATE_FREE) &&
	    (!(sp->cmd_flags & CFLAG_CMDPROXY))) {
		(void) fas_ustart(fas);
	}
exit:
	TRACE_0(TR_FAC_SCSI, TR_FAS_RUNPOLL_END, "fas_runpoll_end");
	return;

bad:
	fas_log(fas, CE_WARN, "Polled cmd failed");
#ifdef FASDEBUG
	fas_printstate(fas, "fas_runpoll: polled cmd failed");
#endif /* FASDEBUG */

cleanup:
	fas_set_throttles(fas, slot, NLUNS_PER_TARGET, MAX_THROTTLE);

	/*
	 * clean up all	traces of this sp because fas_runpoll will return
	 * before fas_reset_recovery() cleans up
	 */
	fas_remove_cmd(fas, sp,	NEW_TIMEOUT);
	fas_decrement_ncmds(fas, sp);
	fas_set_pkt_reason(fas,	sp, CMD_TRAN_ERR, 0);

	if ((sp->cmd_flags & CFLAG_CMDPROXY) ==	0) {
		(void) fas_reset_bus(fas);
	}
	goto exit;
}

/*
 * Poll	for command completion (i.e., no interrupts)
 * limit is in usec (and will not be very accurate)
 */
static int
fas_dopoll(register struct fas *fas, int limit)
{
	register int i,	n;

	/*
	 * timeout is not very accurate	since we don't know how
	 * long	the poll takes
	 * also	if the packet gets started fairly late,	we may
	 * timeout prematurely
	 * fas_dopoll always returns if	e_state	transitions to STATE_FREE
	 */
	TRACE_0(TR_FAC_SCSI, TR_FAS_DOPOLL_START, "fas_dopoll_start");

	if (limit == 0)	{
		limit =	POLL_TIMEOUT;
	}

	for (n = i = 0;	i < limit; i +=	100) {
		if (INTPENDING(fas)) {
			n++;
			fas_intr_svc(fas);
			if (fas->f_state == STATE_FREE)
				break;
		}
		drv_usecwait(100);
	}

	if (i >= limit && fas->f_state != STATE_FREE) {
		fas_printstate(fas, "polled command timeout");
		n = -1;
	}
	TRACE_1(TR_FAC_SCSI, TR_FAS_DOPOLL_END,	"fas_dopoll_end: rval %x", n);
	return (n);
}

/*
 * prepare a sync negotiation message
 */
static void
fas_make_sdtr(struct fas *fas, int msgout_offset, int target)
{
	register u_char	*p = fas->f_cur_msgout + msgout_offset;
	u_short	tshift = 1<<target;
	u_char period =	fas->f_neg_period[target];
	u_char offset =	fas->f_offset[target];

	/*
	 * sanity check	of period and offset
	 */
	if (fas->f_target_scsi_options[target] & SCSI_OPTIONS_FAST) {
		if (period < (u_char)(DEFAULT_FASTSYNC_PERIOD/4)) {
			period = (u_char)(DEFAULT_FASTSYNC_PERIOD/4);
		}
	} else if (fas->f_target_scsi_options[target] &	SCSI_OPTIONS_SYNC) {
		if (period < (u_char)(DEFAULT_SYNC_PERIOD/4)) {
			period = (u_char)(DEFAULT_SYNC_PERIOD/4);
		}
	} else {
		offset = 0;
	}

	if (fas->f_nosync & tshift) {
		offset = 0;
	}
	if ((u_char)(offset & 0xf) > fas_default_offset) {
		offset = fas_default_offset | fas->f_req_ack_delay;
	}

	fas->f_neg_period[target] = (u_char)period;
	fas->f_offset[target] =	(u_char)offset;

	*p++ = (u_char)MSG_EXTENDED;
	*p++ = (u_char)3;
	*p++ = (u_char)MSG_SYNCHRONOUS;
	*p++ = period;
	*p++ = offset &	0xf;
	fas->f_omsglen = 5 + msgout_offset;

	IPRINTF2("fas_make_sdtr: period = %x, offset = %x\n",
		period,	offset);
	/*
	 * increment sdtr flag,	odd value indicates that we initiated
	 * the negotiation
	 */
	fas->f_sdtr_sent++;

	/*
	 * the target may reject the optional sync message so
	 * to avoid negotiating	on every cmd, set sync known here
	 * we should not negotiate wide	after sync again
	 */
	fas->f_sync_known |= 1<<target;
	fas->f_wide_known |= 1<<target;
}

/*
 * prepare a wide negotiation message
 */
static void
fas_make_wdtr(struct fas *fas, int msgout_offset, int target, int width)
{
	register u_char	*p = fas->f_cur_msgout + msgout_offset;

	if (((fas->f_target_scsi_options[target] & SCSI_OPTIONS_WIDE) == 0) ||
	    (fas->f_nowide & (1<<target))) {
		width =	0;
	}
	width =	min(FAS_XFER_WIDTH, width);

	*p++ = (u_char)MSG_EXTENDED;
	*p++ = (u_char)2;
	*p++ = (u_char)MSG_WIDE_DATA_XFER;
	*p++ = (u_char)width;
	fas->f_omsglen = 4 + msgout_offset;
	IPRINTF1("fas_make_wdtr: width=%x\n", width);

	/*
	 * increment wdtr flag,	odd value indicates that we initiated
	 * the negotiation
	 */
	fas->f_wdtr_sent++;

	/*
	 * the target may reject the optional wide message so
	 * to avoid negotiating	on every cmd, set wide known here
	 */
	fas->f_wide_known |= 1<<target;

	fas_set_wide_conf3(fas,	target,	width);
}

/*
 * auto	request	sense support
 * create or destroy an	auto request sense packet
 */
static int
fas_create_arq_pkt(struct fas *fas, struct scsi_address	*ap)
{
	/*
	 * Allocate a request sense packet using get_pktiopb
	 */
	register struct	fas_cmd	*rqpktp;
	int slot = ap->a_target	* NLUNS_PER_TARGET | ap->a_lun;
	int rval = 0;
	register struct	buf *bp;
	struct arq_private_data	*arq_data;

	/*
	 * it would be nicer if	we could allow the target driver
	 * to specify the size but this	is easier and OK for most
	 * drivers to use SENSE_LENGTH
	 * Allocate a request sense packet.
	 */
	bp = scsi_alloc_consistent_buf(ap, (struct buf *)NULL,
	    SENSE_LENGTH, B_READ, SLEEP_FUNC, NULL);
	rqpktp = PKT2CMD(scsi_init_pkt(ap,
	    NULL, bp, CDB_GROUP0, 1, PKT_PRIV_LEN,
	    PKT_CONSISTENT, SLEEP_FUNC,	NULL));
	arq_data =
	    (struct arq_private_data *)(rqpktp->cmd_pkt->pkt_private);
	arq_data->arq_save_bp =	bp;

	RQ_MAKECOM_G0((CMD2PKT(rqpktp)),
	    FLAG_SENSING | FLAG_HEAD | FLAG_NODISCON,
	    (char)SCMD_REQUEST_SENSE, 0, (char)SENSE_LENGTH);
	rqpktp->cmd_flags |= CFLAG_CMDARQ;
	fas->f_arq_pkt[slot] = rqpktp;
	/*
	 * we need a function ptr here so abort/reset can
	 * defer callbacks; fas_call_pkt_comp()	calls
	 * fas_complete_arq_pkt() directly without releasing the lock
	 * However, since we are not calling back directly thru
	 * pkt_comp, don't check this with warlock
	 */
#ifndef	__lock_lint
	rqpktp->cmd_pkt->pkt_comp = fas_complete_arq_pkt;
#endif
	return (rval);
}

static int
fas_delete_arq_pkt(struct fas *fas, struct scsi_address	*ap)
{
	register struct	fas_cmd	*rqpktp;
	int slot = ap->a_target	* NLUNS_PER_TARGET | ap->a_lun;

	/*
	 * if there is still a pkt saved or no rqpkt
	 * then	we cannot deallocate or	there is nothing to do
	 */
	if ((rqpktp = fas->f_arq_pkt[slot]) != NULL) {
		struct arq_private_data	*arq_data =
		    (struct arq_private_data *)(rqpktp->cmd_pkt->pkt_private);
		struct buf *bp = arq_data->arq_save_bp;
		/*
		 * is arq pkt in use?
		 */
		if (arq_data->arq_save_sp) {
			return (-1);
		}

		scsi_destroy_pkt((struct scsi_pkt *)rqpktp);
		scsi_free_consistent_buf(bp);
	}
	return (0);
}

/*
 * complete an arq packet by copying over transport info and the actual
 * request sense data; called with mutex held from fas_call_pkt_comp()
 */
void
fas_complete_arq_pkt(struct fas	*fas, struct fas_cmd *sp, int slot)
{
	register struct	scsi_arq_status	*arqstat;
	register struct	arq_private_data *arq_data =
		    (struct arq_private_data *)sp->cmd_pkt->pkt_private;
	register struct	fas_cmd	*ssp = arq_data->arq_save_sp;
	register struct	buf *bp	= arq_data->arq_save_bp;

	DPRINTF1("completing arq pkt sp=%x\n", sp);
	ASSERT(sp == fas->f_arq_pkt[slot]);
	ASSERT(arq_data->arq_save_sp !=	NULL);
	ASSERT(ssp != fas->f_active[sp->cmd_slot]->f_slot[sp->cmd_tag[1]]);

	arqstat	= (struct scsi_arq_status *)(ssp->cmd_pkt->pkt_scbp);
	arqstat->sts_rqpkt_status = *((struct scsi_status *)
		(sp->cmd_pkt->pkt_scbp));
	arqstat->sts_rqpkt_reason = sp->cmd_pkt->pkt_reason;
	arqstat->sts_rqpkt_state  = sp->cmd_pkt->pkt_state;
	arqstat->sts_rqpkt_statistics =	sp->cmd_pkt->pkt_statistics;
	arqstat->sts_rqpkt_resid  = sp->cmd_pkt->pkt_resid;
	arqstat->sts_sensedata =
	    *((struct scsi_extended_sense *)bp->b_un.b_addr);
	ssp->cmd_pkt->pkt_state	|= STATE_ARQ_DONE;
	arq_data->arq_save_sp =	NULL;
	fas_call_pkt_comp(fas, ssp);
}

/*
 * handle check	condition and start an arq packet
 */
static void
fas_handle_sts_chk(struct fas *fas, struct fas_cmd *sp)
{
	register struct	fas_cmd	*arqsp =	fas->f_arq_pkt[sp->cmd_slot];
	register struct	arq_private_data *arq_data;
	register struct	buf *bp;

	if ((arqsp == NULL) || (arqsp == sp) ||
	    (sp->cmd_scblen < sizeof (struct scsi_arq_status)))	{
		IPRINTF("no arq packet or cannot arq on arq pkt\n");
		fas_call_pkt_comp(fas, sp);
		return;
	}

	arq_data = (struct arq_private_data *)arqsp->cmd_pkt->pkt_private;
	bp = arq_data->arq_save_bp;

	ASSERT(sp->cmd_flags & CFLAG_FINISHED);
	ASSERT(sp != fas->f_active[sp->cmd_slot]->f_slot[sp->cmd_tag[1]]);
	DPRINTF3("start arq for slot=%x, arqsp=%x, rqpkt=%x\n",
	    sp->cmd_slot, arqsp, fas->f_arq_pkt[sp->cmd_slot]);
	ASSERT(arq_data->arq_save_sp ==	NULL);

	arq_data->arq_save_sp =	sp;

	bzero((caddr_t)bp->b_un.b_addr,	sizeof (struct scsi_extended_sense));

	/*
	 * copy	the timeout from the original packet by	lack of	a better
	 * value
	 * we could take the residue of	the timeout but	that could cause
	 * premature timeouts perhaps
	 */
	arqsp->cmd_pkt->pkt_time = sp->cmd_pkt->pkt_time;
	arqsp->cmd_flags &= ~CFLAG_TRANFLAG;
	/*
	 * make sure that auto request sense always goes out
	 * after queue full and after throttle was set to draining
	 */
	fas_full_throttle(fas, sp->cmd_slot);
	(void) fas_accept_pkt(fas, arqsp, NO_TRAN_BUSY);
}

/*
 * handle qfull	condition
 */
static void
fas_handle_qfull(register struct fas *fas, struct fas_cmd *sp)
{
	int slot = sp->cmd_slot;

	if ((++sp->cmd_qfull_retries > fas->f_qfull_retries[Tgt(sp)]) ||
		(fas->f_qfull_retries[Tgt(sp)] == 0)) {
		/*
		 * We have exhausted the retries on QFULL, or,
		 * the target driver has indicated that	it
		 * wants to handle QFULL itself	by setting
		 * qfull-retries capability to 0. In either case
		 * we want the target driver's QFULL handling
		 * to kick in. We do this by having pkt_reason
		 * as CMD_CMPLT	and pkt_scbp as	STATUS_QFULL.
		 */
		IPRINTF2("%d.%d: status queue full, retries over\n",
			Tgt(sp), Lun(sp));
		fas_set_throttles(fas, slot, NLUNS_PER_TARGET, DRAIN_THROTTLE);
		fas_call_pkt_comp(fas, sp);
	} else {
		if (fas->f_reset_delay[Tgt(sp)]	== 0) {
			fas->f_throttle[slot] =
			    max((fas->f_tcmds[slot] - 2), 0);
		}
		IPRINTF3("%d.%d: status queue full, new throttle = %d, "
			"retrying\n", Tgt(sp), Lun(sp),	fas->f_throttle[slot]);
		sp->cmd_pkt->pkt_flags |= FLAG_HEAD;
		sp->cmd_flags &= ~CFLAG_TRANFLAG;
		(void) fas_accept_pkt(fas, sp, NO_TRAN_BUSY);

		/*
		 * when target gives queue full status with no commands
		 * outstanding (f_tcmds[] == 0), throttle is set to 0
		 * (HOLD_THROTTLE), and the queue full handling starts
		 * (see psarc/1994/313); if there are commands outstanding,
		 * the throttle is set to (f_tcmds[] - 2)
		 */
		if (fas->f_throttle[slot] == HOLD_THROTTLE) {
			/*
			 * By setting throttle to QFULL_THROTTLE, we
			 * avoid submitting new	commands and in
			 * fas_restart_cmd find	out slots which	need
			 * their throttles to be cleared.
			 */
			fas_set_throttles(fas, slot, NLUNS_PER_TARGET,
				QFULL_THROTTLE);
			if (fas->f_restart_cmd_timeid == 0) {
				fas->f_restart_cmd_timeid =
				    timeout(fas_restart_cmd, (caddr_t)fas,
				    fas->f_qfull_retry_interval[Tgt(sp)]);
			}
		}
	}
}

/*
 * invoked from	timeout() to restart qfull cmds	with throttle == 0
 */
static void
fas_restart_cmd(register struct	fas *fas)
{
	int i;

	IPRINTF("fas_restart_cmd:\n");

	mutex_enter(FAS_MUTEX(fas));
	fas->f_restart_cmd_timeid = 0;

	for (i = 0; i <	N_SLOTS; i += NLUNS_PER_TARGET)	{
		if (fas->f_reset_delay[i/NLUNS_PER_TARGET] == 0) {
			if (fas->f_throttle[i] == QFULL_THROTTLE) {
				fas_set_throttles(fas, i, NLUNS_PER_TARGET,
					MAX_THROTTLE);
			}
		}
	}

	(void) fas_ustart(fas);
	mutex_exit(FAS_MUTEX(fas));
}

/*
 * Timeout handling:
 * Command watchdog routines
 */
/*ARGSUSED*/
static void
fas_watch(caddr_t arg)
{
	struct fas *fas;

	for (fas = fas_head; fas != (struct fas	*)NULL;	fas = fas->f_next) {

		mutex_enter(FAS_MUTEX(fas));
		DPRINTF2("ncmds=%x, ndisc=%x\n", fas->f_ncmds, fas->f_ndisc);

		if (fas->f_ncmds) {
			int i;
			fas_watchsubr(fas);

			/*
			 * reset throttle. the throttle	may have been
			 * too low if queue full was caused by
			 * another initiator
			 */
			for (i = 0; i <	N_SLOTS; i++) {
				if (fas->f_throttle[i] > HOLD_THROTTLE)	{
					fas_full_throttle(fas, i);
				}
			}
		}

		if (fas->f_props_update) {
			int i;
			for (i = 0; i < NTARGETS_WIDE; i++) {
				if (fas->f_props_update & (1<<i)) {
					fas_update_props(fas, i);
				}
			}
			fas->f_props_update = 0;
		}

		fas_check_waitQ_and_mutex_exit(fas);
	}

again:
	(void) timeout(fas_watch, (caddr_t)0, fas_tick);
	TRACE_0(TR_FAC_SCSI, TR_FAS_WATCH_END, "fas_watch_end");
}

static void
fas_watchsubr(register struct fas *fas)
{
	register short slot;
	register int d = ((fas->f_dslot	== 0)? 1 : fas->f_dslot);
	register struct	f_slots	*tag_slots;

	for (slot = 0; slot < N_SLOTS; slot += d)  {

#ifdef FAS_TEST
		if (fas_btest) {
			fas_btest = 0;
			(void) fas_reset_bus(fas);
			return;
		}
		if (fas_force_timeout && fas->f_tcmds[slot]) {
			(void) fas_cmd_timeout(fas, slot);
			fas_force_timeout = 0;
			return;
		}
		fas_test_reset(fas, slot);
		fas_test_abort(fas, slot);
#endif /* FAS_TEST */

		/*
		 * check tagged	cmds first
		 */
		tag_slots = fas->f_active[slot];
		DPRINTF3(
		"fas_watchsubr: slot %x: tcmds=%x, timeout=%x\n",
		slot, fas->f_tcmds[slot], tag_slots->f_timeout);

		if ((fas->f_tcmds[slot]	> 0) &&	(tag_slots->f_timebase)) {
			tag_slots->f_timeout -=	fas_scsi_watchdog_tick;

			if (tag_slots->f_timeout < 0) {
				fas_cmd_timeout(fas, slot);
				return;
			}
			if ((tag_slots->f_timeout) <=
			    fas_scsi_watchdog_tick) {
				IPRINTF1("pending timeout on slot=%x\n",
					slot);
				IPRINTF("draining all queues\n");
				fas_set_throttles(fas, 0, N_SLOTS,
					DRAIN_THROTTLE);
			}
		}
	}
}

/*
 * timeout recovery
 */
static void
fas_cmd_timeout(register struct	fas *fas, int slot)
{
	int d =	((fas->f_dslot == 0)? 1	: fas->f_dslot);
	int target, lun, i, n, tag, ncmds;
	struct fas_cmd *sp = NULL;
	struct fas_cmd *ssp;

	ASSERT(fas->f_tcmds[slot]);

	/*
	 * set throttle	back; no more draining necessary
	 */
	for (i = 0; i <	N_SLOTS; i += d) {
		if (fas->f_throttle[i] == DRAIN_THROTTLE) {
			fas_full_throttle(fas, i);
		}
	}

	if (NOTAG(slot/NLUNS_PER_TARGET)) {
		sp = fas->f_active[slot]->f_slot[0];
	}

	/*
	 * if no interrupt pending for next second then	the current
	 * cmd must be stuck; switch slot and sp to current slot and cmd
	 */
	if (fas->f_state != STATE_FREE)	{
		for (i = 0; (i < 10000)	&& (INTPENDING(fas) == 0); i++)	{
			drv_usecwait(100);
		}
		if (INTPENDING(fas) == 0) {
			slot = fas->f_current_sp->cmd_slot;
			sp = fas->f_current_sp;
		}
	}

	target = slot /	NLUNS_PER_TARGET;
	lun = slot % NLUNS_PER_TARGET;

	/*
	 * update all outstanding  pkts	for this slot
	 */
	n = fas->f_active[slot]->f_n_slots;
	for (ncmds = tag = 0; tag < n; tag++) {
		ssp = fas->f_active[slot]->f_slot[tag];
		if (ssp	&& ssp->cmd_pkt->pkt_time) {
			fas_set_pkt_reason(fas,	ssp, CMD_TIMEOUT,
				STAT_TIMEOUT | STAT_ABORTED);
			ncmds++;
		}
	}

	/*
	 * no timed-out	cmds here?
	 */
	if (ncmds == 0)	{
		return;
	}

	/*
	 * dump	all we know about this timeout
	 */
	if (sp)	{
		if (sp->cmd_flags & CFLAG_CMDDISC) {
			fas_log(fas, CE_WARN,
			    "Disconnected command timeout for Target %d.%d",
			    target, lun);
		} else {
			ASSERT(sp == fas->f_current_sp);
			fas_log(fas, CE_WARN,
			    "Connected command timeout for Target %d.%d",
			    target, lun);
			/*
			 * Current command timeout appears to relate often
			 * to noisy SCSI in synchronous	mode.
			 */
			if (fas->f_state == ACTS_DATA_DONE) {
				fas_sync_wide_backoff(fas, sp, slot);
			}
#ifdef FASDEBUG
#ifdef CMD_TIMEOUT_HACK
		if (fas_enable_cmd_timeout_workaround) {
			int fas_cnt;
			GET_FAS_COUNT(fas->f_reg, fas_cnt);
			if ((fas->f_target_scsi_options[target]	&
			    SCSI_OPTIONS_FAST) &&
			    (fas->f_state == ACTS_DATA_DONE) &&
			    (fas->f_dma_csr & DMA_WRITE) &&
			    (fas_cnt > 0)) {
				fas_printf(fas,
				    "[known data-in hang problem]\n");
				fas_printf(fas,
				    "[workaround: scsi_options = 0x2f8]\n");
			} else {
				fas_printstate(fas, "timeout");
			}
		} else {
			fas_printstate(fas, "timeout");
		}
#else
			fas_printstate(fas, "timeout");
#endif
#else
			fas_dump_cmd(fas, sp);
#endif
		}
	} else {
		fas_log(fas, CE_WARN,
		    "Disconnected tagged cmd(s) (%d) timeout for Target %d.%d",
		    fas->f_tcmds[slot],	target,	lun);
	}

	if (fas_abort_cmd(fas, sp, slot) == ACTION_SEARCH) {
		(void) fas_istart(fas);
	}
}

/*
 * fas_sync_wide_backoff() increases sync period and enables slow
 * cable mode.
 * the second time, we revert back to narrow/async
 * we count on a bus reset to disable wide in the target and will
 * never renegotiate wide again
 */
static void
fas_sync_wide_backoff(register struct fas *fas,	register struct	fas_cmd	*sp,
    int	slot)
{
	char phase;
	u_short	state =	fas->f_state;
	u_char tgt = slot / NLUNS_PER_TARGET;
	u_int tshift = 1 << tgt;

	phase =	fas_reg_read(fas, &fas->f_reg->fas_stat);
	phase &=  FAS_PHASE_MASK;

	IPRINTF4(
	"fas_sync_wide_backoff: target %d: state=%x, phase=%x, sp=%x\n",
	    tgt, state,	phase, sp);

#ifdef FASDEBUG
	if (fas_no_sync_wide_backoff) {
		return;
	}
#endif

	/*
	 * if this not the first time then disable wide
	 */
	if (fas->f_backoff & tshift) {
		/*
		 * disable wide	for just this target
		 */
		fas->f_fasconf3[tgt] &=	~FAS_CONF3_WIDE;
		if ((fas->f_nowide & tshift) ==	0) {
			fas_log(fas, CE_WARN,
			    "Target %d disabled wide SCSI mode", tgt);
		}
		fas->f_nowide |= tshift;
	}

	/*
	 * reduce xfer rate. if	this is	the first time,	reduce by
	 * 20 or 50%. second time, disable sync
	 */
	if (fas->f_offset[tgt] != 0) {
		if (fas->f_backoff & tshift) {
			if (fas->f_offset[tgt])	{
				fas_log(fas, CE_WARN,
				    "Target %d reverting to async. mode",
				    tgt);
			}
			fas->f_sync_period[tgt]	= 0;
			fas->f_offset[tgt] = 0;
		} else {
			int period = fas->f_neg_period[tgt];

			fas->f_backoff |= tshift;

			if (period <  FASTSCSI_THRESHOLD) {
				/* increase period by 50% */
				period = (period * 150)/100;
			} else {
				/* increase period by 20% */
				period = (period * 120)/100;
			}
			fas->f_neg_period[tgt] = (u_char)period;

			fas_log(fas, CE_WARN,
			    "Target %d reducing sync. transfer rate", tgt);
		}
	}

	/*
	 * always enable slow cable mode, if not already enabled
	 */
	if ((fas->f_fasconf & FAS_CONF_SLOWMODE) == 0) {
		fas->f_fasconf |= FAS_CONF_SLOWMODE;
		fas_reg_write(fas, &fas->f_reg->fas_conf, fas->f_fasconf);
		IPRINTF("Reverting to slow SCSI cable mode\n");
	}

	fas->f_backoff |= tshift;

	/*
	 * Force sync renegotiation and	update properties
	 */
	fas_force_renegotiation(fas, tgt);
	fas->f_props_update |= (1<<tgt);
}

/*
 * handle failed negotiations (either reject or	bus free condition)
 */
static void
fas_reset_sync_wide(struct fas *fas)
{
	register struct	fas_cmd	*sp = fas->f_current_sp;
	register tgt = Tgt(sp);

	if (fas->f_wdtr_sent) {
		IPRINTF("wide neg message rejected or bus free\n");
		fas->f_nowide |= (1<<tgt);
		fas->f_fasconf3[tgt] &=	~FAS_CONF3_WIDE;
		fas_reg_write(fas, &fas->f_reg->fas_conf3,
		fas->f_fasconf3[tgt]);
		/*
		 * clear offset	just in	case it	goes to
		 * data	phase
		 */
		fas_reg_write(fas,
		    (u_char *)&fas->f_reg->fas_sync_offset, 0);
	} else if (fas->f_sdtr_sent) {
		register volatile struct fasreg	*fasreg	=
					fas->f_reg;
		IPRINTF("sync neg message rejected or bus free\n");
		fas->f_nosync |= (1<<tgt);
		fas->f_offset[tgt] = 0;
		fas->f_sync_period[tgt]	= 0;
		fas_reg_write(fas,
		    (u_char *)&fasreg->fas_sync_period,	0);
		fas_reg_write(fas,
		    (u_char *)&fasreg->fas_sync_offset,	0);
		fas->f_offset[tgt] = 0;
		fas->f_fasconf3[tgt] &=	~FAS_CONF3_FASTSCSI;
		fas_reg_write(fas, &fasreg->fas_conf3,
		    fas->f_fasconf3[tgt]);
	}

	fas_force_renegotiation(fas, tgt);
}

/*
 * force wide and sync renegotiation
 */
static void
fas_force_renegotiation(struct fas *fas, int target)
{
	register u_short tshift	= 1<<target;
	fas->f_sync_known &= ~tshift;
	fas->f_sync_enabled &= ~tshift;
	fas->f_wide_known &= ~tshift;
	fas->f_wide_enabled &= ~tshift;
}

/*
 * update conf3	register for wide negotiation
 */
static void
fas_set_wide_conf3(struct fas *fas, int	target,	int width)
{
	ASSERT(width <=	1);
	switch (width) {
	case 0:
		fas->f_fasconf3[target]	&= ~FAS_CONF3_WIDE;
		break;
	case 1:
		fas->f_fasconf3[target]	|= FAS_CONF3_WIDE;
		fas->f_wide_enabled |= (1<<target);
		break;
	}

	fas_reg_write(fas, &fas->f_reg->fas_conf3, fas->f_fasconf3[target]);
	fas->f_fasconf3_reg_last = fas->f_fasconf3[target];
}

/*
 * Abort command handling
 *
 * abort current cmd, either by device reset or immediately with bus reset
 * (usually an abort msg doesn't completely solve the problem, therefore
 * a device or bus reset is recommended)
 */
static int
fas_abort_curcmd(register struct fas *fas)
{
	if (fas->f_current_sp) {
		return (fas_abort_cmd(fas, fas->f_current_sp,
			fas->f_current_sp->cmd_slot));
	} else {
		return (fas_reset_bus(fas));
	}
}

static int
fas_abort_cmd(register struct fas *fas,	register struct	fas_cmd	*sp, int slot)
{
	struct scsi_address ap;

	ap.a_hba_tran =	fas->f_tran;
	ap.a_target = slot / NLUNS_PER_TARGET;
	ap.a_lun    = slot % NLUNS_PER_TARGET;
	ap.a_sublun = 0;

	IPRINTF1("abort cmd %x\n", sp);

	/*
	 * attempting to abort a connected cmd is usually fruitless, so
	 * only	try disconnected cmds
	 * a reset is preferable over an abort (see 1161701)
	 */
	if ((fas->f_current_sp && (fas->f_current_sp->cmd_slot != slot)) ||
	    (fas->f_state == STATE_FREE)) {
		IPRINTF2("attempting to reset target %d.%d\n",
		    ap.a_target, ap.a_lun);
		if (fas_do_scsi_reset(&ap, RESET_TARGET)) {
			return (ACTION_SEARCH);
		}
	}

	/*
	 * if the target won't listen, then a retry is useless
	 * there is also the possibility that the cmd still completed while
	 * we were trying to reset and the target driver may have done a
	 * device reset	which has blown	away this sp.
	 * well, we've tried, now pull the chain
	 */
	IPRINTF("aborting all cmds by bus reset\n");
	return (fas_reset_bus(fas));
}

/*
 * fas_do_scsi_abort() assumes that we already have the	mutex.
 * during the abort, we	hold the mutex and prevent callbacks by	setting
 * completion pointer to NULL. this will also avoid that a target driver
 * attempts to do a scsi_abort/reset while we are aborting.
 * because the completion pointer is NULL  we can still	update the
 * packet after	completion
 * the throttle	for this slot is cleared either	by fas_abort_connected_cmd
 * or fas_runpoll which	prevents new cmds from starting	while aborting
 */
static int
fas_do_scsi_abort(struct scsi_address *ap, struct scsi_pkt *pkt)
{
	register struct	fas *fas = ADDR2FAS(ap);
	register struct	fas_cmd	*sp;
	register int rval = FALSE;
	register short slot;
	struct fas_cmd *cur_sp = fas->f_current_sp;
	void	(*cur_savec)(),	(*sp_savec)();
	int	sp_tagged_flag,	abort_msg;

	if (pkt) {
		sp = PKT2CMD(pkt);
		slot = sp->cmd_slot;
	} else {
		sp = NULL;
		slot = ap->a_target * NLUNS_PER_TARGET | ap->a_lun;
	}

	/*
	 *   If	no specific command was	passed,	all cmds here will be aborted
	 *   If	a specific command was passed as an argument (to be aborted)
	 *   only the specified	command	will be	aborted
	 */
	ASSERT(mutex_owned(FAS_MUTEX(fas)));
	IPRINTF4("fas_scsi_abort for slot %x, sp=%x, pkt_flags=%x, cur_sp=%x\n",
	    slot, sp, (sp? sp->cmd_pkt_flags : 0), cur_sp);

	if (cur_sp) {
		/*
		 * prevent completion on current cmd
		 */
		cur_savec = cur_sp->cmd_pkt->pkt_comp;
		cur_sp->cmd_pkt->pkt_comp = NULL;
	}

	if (sp)	{
		IPRINTF3("aborting one command %x for %d.%d\n",
		    sp,	ap->a_target, ap->a_lun);
		rval = fas_remove_from_readyQ(fas, sp, slot);
		if (rval) {
			IPRINTF("aborted one ready cmd\n");
			fas_set_pkt_reason(fas,	sp, CMD_ABORTED, STAT_ABORTED);
			fas_decrement_ncmds(fas, sp);
			if (cur_sp) {
				cur_sp->cmd_pkt->pkt_comp = cur_savec;
			}
			fas_call_pkt_comp(fas, sp);
			goto exit;

		} else if ((sp != cur_sp) &&
		    (sp	!= fas->f_active[slot]->f_slot[sp->cmd_tag[1]])) {
			IPRINTF("cmd doesn't exist here\n");
			if (cur_sp) {
				cur_sp->cmd_pkt->pkt_comp = cur_savec;
			}
			rval = TRUE;
			goto exit;
		}

		/*
		 * the cmd exists here.	is it connected	or disconnected?
		 * if connected	but still selecting then can't abort now.
		 * prevent completion on this cmd
		 */
		sp_tagged_flag = (sp->cmd_pkt_flags & FLAG_TAGMASK);
		abort_msg = (sp_tagged_flag? MSG_ABORT_TAG : MSG_ABORT);
		sp_savec = sp->cmd_pkt->pkt_comp;
		sp->cmd_pkt->pkt_comp =	NULL;

		/* connected but not selecting?	*/
		if ((sp	== cur_sp) && (fas->f_state != STATE_FREE) &&
		    (sp->cmd_pkt->pkt_state)) {
			rval = fas_abort_connected_cmd(fas, sp,	abort_msg,
				slot);
			/*
			 * if aborting connected cmd fails, do not
			 * retry an abort disconnected cmd since this
			 * may cause confusion and the loss of pkts
			 */
		} else if ((sp != cur_sp) &&
		    (sp->cmd_flags & CFLAG_CMDDISC)) {
			rval = fas_abort_disconnected_cmd(fas, ap, sp,
				abort_msg, slot);
		}

		if (rval) {
			sp->cmd_flags |= CFLAG_COMPLETED;
			fas_set_pkt_reason(fas,	sp, CMD_ABORTED, STAT_ABORTED);
		}

		sp->cmd_pkt->pkt_comp =	sp_savec;

	} else {
		IPRINTF2("aborting all commands for %d.%d\n",
		    ap->a_target, ap->a_lun);
		abort_msg = MSG_ABORT;

		/* active and not selecting ? */
		if (cur_sp && (fas->f_state != STATE_FREE) &&
		    cur_sp->cmd_pkt->pkt_state)	{
			rval = fas_abort_connected_cmd(fas, cur_sp, abort_msg,
				slot);
		} else if (fas->f_tcmds[slot]) {
			rval = fas_abort_disconnected_cmd(fas, ap,
				    (struct fas_cmd *)0, abort_msg, slot);
		}
	}

done:
	/* complete the	current	sp */
	if (cur_sp) {
		cur_sp->cmd_pkt->pkt_comp = cur_savec;
		if (cur_sp->cmd_flags &	CFLAG_COMPLETED) {
			fas_remove_cmd(fas, cur_sp, NEW_TIMEOUT);
			cur_sp->cmd_flags &= ~CFLAG_COMPLETED;
			fas_decrement_ncmds(fas, cur_sp);
			fas_call_pkt_comp(fas, cur_sp);
		}
	}

	/* complete the	sp passed as 2nd arg */
	if (sp && (sp != cur_sp) && (sp->cmd_flags & CFLAG_COMPLETED)) {
		sp->cmd_flags &= ~CFLAG_COMPLETED;
		fas_remove_cmd(fas, sp,	NEW_TIMEOUT);
		fas_decrement_ncmds(fas, sp);
		fas_call_pkt_comp(fas, sp);
	}

	/* clean up all	cmds for this slot */
	if (rval && (abort_msg == MSG_ABORT)) {
		/*
		 * mark	all commands here as aborted
		 * abort msg has been accepted,	now cleanup queues;
		 */
		fas_mark_packets(fas, slot, CMD_ABORTED, STAT_ABORTED);
		fas_flush_tagQ(fas, slot);
		fas_flush_readyQ(fas, slot);
	}
exit:
	if (fas->f_state == STATE_FREE)	{
		(void) fas_ustart(fas);
	}

	ASSERT(mutex_owned(FAS_MUTEX(fas)));
	return (rval);
}

/*
 * mark	all packets with new reason and	update statistics
 */
static void
fas_mark_packets(struct	fas *fas, int slot, u_char reason, u_long stat)
{
	struct fas_cmd *sp = fas->f_readyf[slot];

	while (sp != 0)	{
		fas_set_pkt_reason(fas,	sp, reason, STAT_ABORTED);
		sp = sp->cmd_forw;
	}
	if (fas->f_tcmds[slot])	{
		int n =	0;
		u_short	tag;

		for (tag = 0; tag < fas->f_active[slot]->f_n_slots; tag++) {
			if ((sp	= fas->f_active[slot]->f_slot[tag]) != 0) {
				fas_set_pkt_reason(fas,	sp, reason, stat);
				n++;
			}
		}
		ASSERT(fas->f_tcmds[slot] == n);
	}
}

/*
 * set pkt_reason and OR in pkt_statistics flag
 */
static void
fas_set_pkt_reason(struct fas *fas, struct fas_cmd *sp,	u_char reason,
    u_long stat)
{
	if (sp)	{
		if (sp->cmd_pkt->pkt_reason == CMD_CMPLT) {
			sp->cmd_pkt->pkt_reason	= reason;
		}
		sp->cmd_pkt->pkt_statistics |= stat;
		IPRINTF3("sp=%x, pkt_reason=%x, pkt_stat=%x\n",
		    sp,	reason,	sp->cmd_pkt->pkt_statistics);
	}
}

/*
 * delete specified cmd	from the ready queue
 */
static int
fas_remove_from_readyQ(struct fas *fas,	struct fas_cmd *sp, int	slot)
{
	struct fas_cmd *ssp, *psp;

	/*
	 * command has not been	started	yet and	is still in the	ready queue
	 */
	if (sp)	{
		ASSERT(fas->f_ncmds > 0);
		/*
		 * find	packet on the ready queue and remove it
		 */
		for (psp = NULL, ssp = fas->f_readyf[slot]; ssp	!= NULL;
		    psp	= ssp, ssp = ssp->cmd_forw) {
			if (ssp	== sp) {
				if (fas->f_readyf[slot]	== sp) {
					fas->f_readyf[slot] = sp->cmd_forw;
				} else {
					psp->cmd_forw =	sp->cmd_forw;
				}
				if (fas->f_readyb[slot]	== sp) {
					fas->f_readyb[slot] = psp;
				}
				return (TRUE);
			}
		}
	}
	return (FALSE);
}

/*
 * add cmd to to head of the readyQ
 * due to tag allocation failure or preemption we have to return
 * this cmd to the readyQ
 */
static void
fas_head_of_readyQ(struct fas *fas, struct fas_cmd *sp)
{
	/*
	 * never return	a NOINTR pkt to	the readyQ
	 * (fas_runpoll	will resubmit)
	 */
	if ((sp->cmd_pkt_flags & FLAG_NOINTR) == 0) {
		register struct	fas_cmd	*dp;
		register int slot = sp->cmd_slot;

		dp = fas->f_readyf[slot];
		fas->f_readyf[slot] = sp;
		sp->cmd_forw = dp;
		if (fas->f_readyb[slot]	== NULL) {
			fas->f_readyb[slot] = sp;
		}
	}
}

/*
 * flush cmds in ready queue
 */
static void
fas_flush_readyQ(struct	fas *fas, int slot)
{
	if (fas->f_readyf[slot]) {
		struct fas_cmd *sp, *nsp;

		IPRINTF1("flushing ready queue, slot=%x\n", slot);
		ASSERT(fas->f_ncmds > 0);

		sp = fas->f_readyf[slot];
		fas->f_readyf[slot] = fas->f_readyb[slot] = NULL;

		while (sp != 0)	{
			/*
			 * save	the forward pointer before calling
			 * the completion routine
			 */
			nsp = sp->cmd_forw;
			ASSERT((sp->cmd_flags &	CFLAG_FREE) == 0);
			ASSERT(Tgt(sp) == slot/NLUNS_PER_TARGET);
			fas_decrement_ncmds(fas, sp);
			fas_call_pkt_comp(fas, sp);
			sp = nsp;
		}
		fas_check_ncmds(fas);
	}
}

/*
 * cleanup the tag queue
 * preserve some order by starting with	the oldest tag
 */
static void
fas_flush_tagQ(struct fas *fas,	int slot)
{
	u_short	tag, starttag;
	struct fas_cmd *sp;
	struct f_slots *tagque = fas->f_active[slot];

	if (tagque == NULL) {
		return;
	}

	DPRINTF2("flushing entire tag queue, slot=%x, tcmds=%x\n",
	    slot, fas->f_tcmds[slot]);

#ifdef FASDEBUG
	{
		int n =	0;
		for (tag = 0; tag < fas->f_active[slot]->f_n_slots; tag++) {
			if ((sp	= tagque->f_slot[tag]) != 0) {
				n++;
				ASSERT((sp->cmd_flags &	CFLAG_FREE) == 0);
				if (sp->cmd_pkt->pkt_reason == CMD_CMPLT) {
					if ((sp->cmd_flags & CFLAG_FINISHED) ==
					    0) {
						debug_enter("fas_flush_tagQ");
					}
				}
			}
		}
		ASSERT(fas->f_tcmds[slot] == n);
	}
#endif
	tag = starttag = fas->f_active[slot]->f_tags;

	do {
		if ((sp	= tagque->f_slot[tag]) != 0) {
			fas_flush_cmd(fas, sp, 0, 0);
		}
		tag = ((u_short)(tag + 1)) %
		    (u_short)fas->f_active[slot]->f_n_slots;
	} while	(tag !=	starttag);

	ASSERT(fas->f_tcmds[slot] == 0);
	EPRINTF2("ncmds = %x, ndisc=%x\n", fas->f_ncmds, fas->f_ndisc);
	fas_check_ncmds(fas);
}

/*
 * cleanup one active command
 */
static void
fas_flush_cmd(struct fas *fas, struct fas_cmd *sp, u_char reason,
    u_long  stat)
{
	register short slot = sp->cmd_slot;

	ASSERT(fas->f_ncmds > 0);
	ASSERT((sp->cmd_flags &	CFLAG_FREE) == 0);
	ASSERT(sp == fas->f_active[slot]->f_slot[sp->cmd_tag[1]]);

	fas_remove_cmd(fas, sp,	NEW_TIMEOUT);
	fas_decrement_ncmds(fas, sp);
	fas_set_pkt_reason(fas,	sp, reason, stat);
	fas_call_pkt_comp(fas, sp);

	EPRINTF2("ncmds = %x, ndisc=%x\n", fas->f_ncmds, fas->f_ndisc);
	fas_check_ncmds(fas);
}

/*
 * prepare a proxy cmd (a cmd sent on behalf of	the target driver,
 * usually for error recovery or abort/reset)
 */
static void
fas_makeproxy_cmd(struct fas_cmd *sp, struct scsi_address *ap,
    struct scsi_pkt *pkt, int nmsgs, ...)
{
	va_list	vap;
	int i;

	ASSERT(nmsgs <=	(CDB_GROUP5 - CDB_GROUP0 - 3));

	bzero((caddr_t)sp, sizeof (*sp));
	bzero((caddr_t)pkt, sizeof (*pkt));

	pkt->pkt_address	= *ap;
	pkt->pkt_cdbp		= (opaque_t)&sp->cmd_cdb[0];
	pkt->pkt_scbp		= (opaque_t)&sp->cmd_scb;
	pkt->pkt_ha_private	= (opaque_t)sp;
	sp->cmd_pkt		= pkt;
	sp->cmd_scblen		= 1;
	sp->cmd_pkt_flags	= pkt->pkt_flags = FLAG_NOINTR;
	sp->cmd_flags		= CFLAG_CMDPROXY;
	sp->cmd_cdb[FAS_PROXY_TYPE] = FAS_PROXY_SNDMSG;
	sp->cmd_cdb[FAS_PROXY_RESULT] =	FALSE;
	sp->cmd_cdb[FAS_PROXY_DATA] = (char)nmsgs;

	va_start(vap, nmsgs);
	for (i = 0; i <	nmsgs; i++) {
		sp->cmd_cdb[FAS_PROXY_DATA + 1 + i] = (u_char)va_arg(vap, int);
	}
	va_end(vap);
}

/*
 * send	a proxy	cmd and	check the result
 */
static int
fas_do_proxy_cmd(struct	fas *fas, struct fas_cmd *sp,
    struct scsi_address	*ap, char *what)
{
	int rval;

	IPRINTF3("Sending proxy %s message to %d.%d\n",	what,
	    ap->a_target, ap->a_lun);
	if (fas_accept_pkt(fas,	sp, TRAN_BUSY_OK) == TRAN_ACCEPT &&
	    sp->cmd_pkt->pkt_reason == CMD_CMPLT &&
	    sp->cmd_cdb[FAS_PROXY_RESULT] == TRUE) {
		IPRINTF3("Proxy %s succeeded for %d.%d\n", what,
		    ap->a_target, ap->a_lun);
		ASSERT(fas->f_current_sp != sp);
		rval = TRUE;
	} else {
		IPRINTF5(
		"Proxy %s failed for %d.%d, result=%x, reason=%x\n", what,
		    ap->a_target, ap->a_lun, sp->cmd_cdb[FAS_PROXY_RESULT],
		    sp->cmd_pkt->pkt_reason);
		ASSERT(fas->f_current_sp != sp);
		rval = FALSE;
	}
	return (rval);
}

/*
 * abort a connected command by	sending	an abort msg; hold off on
 * starting new	cmds by	setting	throttles to HOLD_THROTTLE
 */
static int
fas_abort_connected_cmd(struct fas *fas, struct	fas_cmd	*sp, u_char msg,
    int	slot)
{
	int rval = FALSE;
	int flags = sp->cmd_pkt_flags;
	short throttles[1];

	/*
	 * if reset delay active we cannot  access the target.
	 */
	if (fas->f_reset_delay[Tgt(sp)]) {
		return (rval);
	}

	/*
	 * only	abort while in data phase; otherwise we	mess up	msg phase
	 */
	if (!((fas->f_state == ACTS_DATA) ||
	    (fas->f_state == ACTS_DATA_DONE))) {
		return (rval);
	}


	IPRINTF3("Sending abort message %s to connected %d.%d\n",
	    scsi_mname(msg), Tgt(sp), Lun(sp));

	fas_save_throttles(fas,	slot, 1, throttles);
	fas_set_throttles(fas, slot, 1,	HOLD_THROTTLE);

	fas->f_abort_msg_sent =	0;
	fas->f_omsglen = 1;
	fas->f_cur_msgout[0] = msg;
	sp->cmd_pkt_flags |= FLAG_NOINTR;
	fas_assert_atn(fas);

	(void) fas_dopoll(fas, SHORT_POLL_TIMEOUT);

	/*
	 * now check if	the msg	was taken
	 * e_abort is set in fas_handle_msg_out_done when the abort
	 * msg has actually gone out (ie. msg out phase	occurred
	 */
	if (fas->f_abort_msg_sent && (sp->cmd_flags & CFLAG_COMPLETED))	{
		IPRINTF2("target %d.%d aborted\n",
			Tgt(sp), Lun(sp));
		rval = TRUE;
	} else {
		IPRINTF2("target %d.%d did not abort\n",
			Tgt(sp), Lun(sp));
	}
	fas_restore_throttles(fas, slot, 1, throttles);
	sp->cmd_pkt_flags = flags;
	fas->f_omsglen = 0;
	return (rval);
}

/*
 * abort a disconnected	command; if it is a tagged command, we need
 * to include the tag
 */
static int
fas_abort_disconnected_cmd(struct fas *fas, struct scsi_address	*ap,
    struct fas_cmd *sp,	u_char msg, int	slot)
{
	auto struct fas_cmd local;
	struct scsi_pkt	pkt;
	struct fas_cmd *proxy_cmdp = &local;
	int target = ap->a_target;

	/*
	 * if reset delay is active, we	cannot start a selection
	 * and there shouldn't be a cmd	outstanding
	 */
	if (fas->f_reset_delay[target] != 0) {
		return (FALSE);
	}

	IPRINTF1("aborting disconnected tagged cmd(s) with %s\n",
		scsi_mname(msg));
	if (sp && (TAGGED(target) && (msg == MSG_ABORT_TAG))) {
		int tag	= sp->cmd_tag[1];
		ASSERT(sp == fas->f_active[slot]->f_slot[tag]);
		fas_makeproxy_cmd(proxy_cmdp, ap, &pkt,	3,
		    MSG_SIMPLE_QTAG, tag, msg);
	} else {
		fas_makeproxy_cmd(proxy_cmdp, ap, &pkt,	1, msg);
	}

	return (fas_do_proxy_cmd(fas, proxy_cmdp, ap, scsi_mname(msg)));
}

/*
 * reset handling:
 * fas_do_scsi_reset assumes that we have already entered the mutex
 */
static int
fas_do_scsi_reset(struct scsi_address *ap, int level)
{
	register rval =	FALSE;
	struct fas *fas	= ADDR2FAS(ap);
	short slot = (ap->a_target * NLUNS_PER_TARGET) | ap->a_lun;

	ASSERT(mutex_owned(FAS_MUTEX(fas)));
	IPRINTF3("fas_scsi_reset for slot %x, level=%x, tcmds=%x\n",
		slot, level, fas->f_tcmds[slot]);

	if (level == RESET_ALL)	{
		/*
		 * We know that	fas_reset_bus()	returns	ACTION_RETURN.
		 */
		(void) fas_reset_bus(fas);

		/*
		 * Now call fas_dopoll() to field the reset interrupt
		 * which will then call	esp_reset_recovery which will
		 * call	the completion function	for all	commands.
		 */
		if (fas_dopoll(fas, SHORT_POLL_TIMEOUT)	<= 0) {
			/*
			 * reset fas
			 */
			fas_internal_reset(fas,	FAS_RESET_FAS);
			(void) fas_reset_bus(fas);
			if (fas_dopoll(fas, SHORT_POLL_TIMEOUT)	<= 0) {
				fas_log(fas,
				    CE_WARN, "reset scsi bus failed");
				New_state(fas, STATE_FREE);
			} else {
				rval = TRUE;
			}
		} else {
			rval = TRUE;
		}

	} else {
		struct fas_cmd *cur_sp = fas->f_current_sp;
		void (*savec)()	= NULL;

		/*
		 * zero	pkt_comp so it won't complete during the reset and
		 * we can still	update the packet after	the reset.
		 */
		if (cur_sp) {
			savec =	cur_sp->cmd_pkt->pkt_comp;
			cur_sp->cmd_pkt->pkt_comp = NULL;
		}

		/*
		 * is this a connected cmd but not selecting?
		 */
		if (cur_sp && (fas->f_state != STATE_FREE) &&
		    (cur_sp->cmd_pkt->pkt_state	!= 0) &&
		    (ap->a_target == (Tgt(cur_sp)))) {
			rval = fas_reset_connected_cmd(fas, ap,	slot);
		}

		/*
		 * if not connected or fas_reset_connected_cmd() failed,
		 * attempt a reset_disconnected_cmd
		 */
		if (rval == FALSE) {
			rval = fas_reset_disconnected_cmd(fas, ap);
		}

		/*
		 * cleanup if reset was	successful
		 * complete the	current	sp first.
		 */
		if (cur_sp) {
			cur_sp->cmd_pkt->pkt_comp = savec;
			if (cur_sp->cmd_flags &	CFLAG_COMPLETED) {
				fas_remove_cmd(fas, cur_sp, NEW_TIMEOUT);
				cur_sp->cmd_flags &= ~CFLAG_COMPLETED;
				fas_decrement_ncmds(fas, cur_sp);
				fas_call_pkt_comp(fas, cur_sp);
			}
		}

		if (rval == TRUE) {
			fas_reset_cleanup(fas, slot);
		} else {
			IPRINTF1("fas_scsi_reset failed for slot %x\n",	slot);
		}

		if (fas->f_state == STATE_FREE)	{
			(void) fas_ustart(fas);
		}
	}
exit:
	ASSERT(mutex_owned(FAS_MUTEX(fas)));
	ASSERT(fas->f_ncmds >= fas->f_ndisc);
	return (rval);
}

/*
 * reset delay is  handled by a	separate watchdog; this	ensures	that
 * regardless of fas_scsi_watchdog_tick, the reset delay will not change
 */
static void
fas_start_watch_reset_delay()
{
	mutex_enter(&fas_global_mutex);
	if (fas_reset_watch == 0) {
		fas_reset_watch	= timeout(fas_watch_reset_delay, (caddr_t)0,
		    drv_usectohz((clock_t)
		    FAS_WATCH_RESET_DELAY_TICK * 1000));
	}
	ASSERT(fas_reset_watch != 0);
	mutex_exit(&fas_global_mutex);
}

/*
 * set throttles to HOLD and set reset_delay for all target/luns
 */
static void
fas_setup_reset_delay(struct fas *fas)
{
	if (!ddi_in_panic()) {
		int i;

		fas_set_throttles(fas, 0, N_SLOTS, HOLD_THROTTLE);
		for (i = 0; i <	NTARGETS_WIDE; i++) {
			fas->f_reset_delay[i] = fas->f_scsi_reset_delay;
		}
		fas_start_watch_reset_delay();
	} else {
		drv_usecwait(fas->f_scsi_reset_delay * 1000);
	}
}

/*
 * fas_watch_reset_delay(_subr) is invoked by timeout() and checks every
 * fas instance for active reset delays
 */
/*ARGSUSED*/
static void
fas_watch_reset_delay(caddr_t arg)
{
	struct fas *fas;
	int not_done = 0;

	mutex_enter(&fas_global_mutex);
	fas_reset_watch	= 0;
	mutex_exit(&fas_global_mutex);
	for (fas = fas_head; fas != (struct fas	*)NULL;	fas = fas->f_next) {
		if (fas->f_tran	== 0) {
			continue;
		}
		mutex_enter(FAS_MUTEX(fas));
		not_done += fas_watch_reset_delay_subr(fas);
		fas_check_waitQ_and_mutex_exit(fas);
	}
	if (not_done) {
		fas_start_watch_reset_delay();
	}
}

static int
fas_watch_reset_delay_subr(register struct fas *fas)
{
	register short slot, s;
	int start_slot = -1;
	int done = 0;

	for (slot = 0; slot < N_SLOTS; slot += NLUNS_PER_TARGET)  {

		/*
		 * check if a reset delay is active; if	so back	to full	throttle
		 * which will unleash the cmds in the ready Q
		 */
		s = slot/NLUNS_PER_TARGET;
		if (fas->f_reset_delay[s] != 0)	{
			EPRINTF2("target%d: reset delay=%d\n", s,
			    fas->f_reset_delay[s]);
			fas->f_reset_delay[s] -= FAS_WATCH_RESET_DELAY_TICK;
			if (fas->f_reset_delay[s] <= 0)	{
				/*
				 * clear throttle for all luns on  this	target
				 */
				fas->f_reset_delay[s] =	0;
				fas_set_throttles(fas, slot,
				    NLUNS_PER_TARGET, MAX_THROTTLE);
				IPRINTF1("reset delay completed, slot=%x\n",
				    slot);
				if (start_slot == -1) {
					start_slot = slot;
				}
			} else {
				done = -1;
			}
		}
	}

	/*
	 * start a cmd if a reset delay	expired
	 */
	if (start_slot != -1 &&	fas->f_state ==	STATE_FREE) {
		(void) fas_ustart(fas);
	}
	return (done);
}

/*
 * cleanup after a device reset. this affects all target's luns
 */
static void
fas_reset_cleanup(struct fas *fas, int slot)
{
	/*
	 * reset msg has been accepted,	now cleanup queues;
	 * for all luns	of this	target
	 */
	int i, start, end;
	int target  = slot/NLUNS_PER_TARGET;

	start =	slot & ~(NLUNS_PER_TARGET-1);
	end = start + NLUNS_PER_TARGET;
	IPRINTF4("fas_reset_cleanup: slot %x, start=%x, end=%x, tcmds=%x\n",
	    slot, start, end, fas->f_tcmds[slot]);

	/*
	 * if we are not in panic set up a reset delay for this target,
	 * a zero throttle forces all new requests into	the ready Q
	 */
	if (!ddi_in_panic()) {
		fas_set_throttles(fas, start, NLUNS_PER_TARGET,	HOLD_THROTTLE);
		fas->f_reset_delay[target] = fas->f_scsi_reset_delay;
		fas_start_watch_reset_delay();
	} else {
		drv_usecwait(fas->f_scsi_reset_delay * 1000);
	}

	for (i = start;	i < end; i++) {
		fas_mark_packets(fas, i, CMD_RESET, STAT_DEV_RESET);
		fas_flush_tagQ(fas, i);
		fas_flush_readyQ(fas, i);
		if (fas->f_arq_pkt[i]) {
			struct fas_cmd *sp = fas->f_arq_pkt[i];
			struct arq_private_data	*arq_data =
			(struct	arq_private_data *)(sp->cmd_pkt->pkt_private);
			ASSERT(arq_data->arq_save_sp ==	NULL);
		}
		ASSERT(fas->f_tcmds[i] == 0);
	}
	ASSERT(fas->f_ncmds >= fas->f_ndisc);
}

/*
 * reset a currently disconnected target
 */
static int
fas_reset_disconnected_cmd(struct fas *fas, struct scsi_address	*ap)
{
	auto struct fas_cmd local;
	struct fas_cmd *sp = &local;
	struct scsi_pkt	pkt;

	/*
	 * if reset delay active we cannot  access the target
	 */
	if (fas->f_reset_delay[ap->a_target]) {
		return (FALSE);
	}

	fas_makeproxy_cmd(sp, ap, &pkt,	1, MSG_DEVICE_RESET);
	return (fas_do_proxy_cmd(fas, sp, ap, scsi_mname(MSG_DEVICE_RESET)));
}

/*
 * reset a target with a currently connected command
 * Assert ATN and send MSG_DEVICE_RESET, zero throttles	temporarily
 * to prevent new cmds from starting regardless	of the outcome
 */
static int
fas_reset_connected_cmd(struct fas *fas, struct	scsi_address *ap, int slot)
{
	int rval = FALSE;
	struct fas_cmd *sp = fas->f_current_sp;
	int flags = sp->cmd_pkt_flags;
	short throttles[NLUNS_PER_TARGET];

	/*
	 * if reset delay active we should not access the target
	 */
	if (fas->f_reset_delay[ap->a_target]) {
		return (rval);
	}

	/*
	 * only	attempt	to reset in data phase; during other phases
	 * asserting ATN may just cause confusion
	 */
	if (!((fas->f_state == ACTS_DATA) ||
	    (fas->f_state == ACTS_DATA_DONE))) {
		return (rval);
	}

	IPRINTF2("Sending reset message to connected %d.%d\n",
	    ap->a_target, ap->a_lun);
	fas->f_reset_msg_sent =	0;
	fas->f_omsglen = 1;
	fas->f_cur_msgout[0] = MSG_DEVICE_RESET;
	sp->cmd_pkt_flags |= FLAG_NOINTR;

	fas_assert_atn(fas);

	fas_save_throttles(fas,	slot, NLUNS_PER_TARGET,	throttles);
	fas_set_throttles(fas, slot, NLUNS_PER_TARGET, HOLD_THROTTLE);

	/*
	 * poll for interrupts until bus free
	 */
	(void) fas_dopoll(fas, SHORT_POLL_TIMEOUT);

	/*
	 * now check if	the msg	was taken
	 * f_reset is set in fas_handle_msg_out_done when
	 * msg has actually gone out  (ie. msg out phase occurred)
	 */
	if (fas->f_reset_msg_sent && (sp->cmd_flags & CFLAG_COMPLETED))	{
		IPRINTF2("target %d.%d reset\n", ap->a_target, ap->a_lun);
		rval = TRUE;
	} else {
		IPRINTF2("target %d.%d did not reset\n",
			ap->a_target, ap->a_lun);
	}
	fas_restore_throttles(fas, slot, NLUNS_PER_TARGET, throttles);
	sp->cmd_pkt_flags = flags;
	fas->f_omsglen = 0;

	return (rval);
}

/*
 * reset the scsi bus to blow all commands away
 */
static int
fas_reset_bus(struct fas *fas)
{
	IPRINTF("fas_reset_bus:\n");
	New_state(fas, ACTS_RESET);

	fas_internal_reset(fas,	FAS_RESET_SCSIBUS);

	/*
	 * Now that we've reset	the SCSI bus, we'll take a SCSI	RESET
	 * interrupt and use that to clean up the state	of things.
	 */
	return (ACTION_RETURN);
}

/*
 * fas_reset_recovery is called on the reset interrupt and cleans
 * up all cmds (active or waiting)
 */
static int
fas_reset_recovery(struct fas *fas)
{
	register short slot, start_slot;
	int i;

	IPRINTF("fas_reset_recovery:\n");
	fas_check_ncmds(fas);

	/*
	 * renegotiate wide and sync for all targets
	 */
	fas->f_sync_known = fas->f_wide_known =	0;

	/*
	 * reset dma engine
	 */
	FAS_FLUSH_DMA_HARD(fas);

	/*
	 * set throttles and reset delay
	 */
	fas_setup_reset_delay(fas);

	/*
	 * clear interrupts until they go away
	 */
	while (INTPENDING(fas))	{
		register volatile struct fasreg	*fasreg	= fas->f_reg;
		fas->f_stat = fas_reg_read(fas,	&fasreg->fas_stat);
		fas->f_stat2 = fas_reg_read(fas, &fasreg->fas_stat2);
		fas->f_step = fas_reg_read(fas,	&fasreg->fas_step);
		fas->f_intr = fas_reg_read(fas,	&fasreg->fas_intr);
	}

	fas_reg_cmd_write(fas, CMD_FLUSH);

#ifdef RESET_BUS_HACK
	if (fas_enable_reset_bus_workaround) {
		/*
		 * reset the chip, this shouldn't be necessary but sometimes
		 * we get a hang in the next data in phase
		 */
		fas_internal_reset(fas,	FAS_RESET_FAS);
	}
#endif

	/*
	 * reset was expected? if not, it must be external bus reset
	 */
	if (fas->f_state != ACTS_RESET)	{
		if (fas->f_ncmds) {
			fas_log(fas, CE_WARN, "external SCSI bus reset");
		}
	}

	if (fas->f_ncmds == 0) {
		New_state(fas, STATE_FREE);
		return (ACTION_RETURN);
	}

	/*
	 * completely reset the	state of the softc data.
	 */
	fas_internal_reset(fas,	FAS_RESET_SOFTC);

	/*
	 * Hold	the state of the host adapter open
	 */
	New_state(fas, ACTS_FROZEN);

	/*
	 * for right now just claim that all
	 * commands have been destroyed	by a SCSI reset
	 * and let already set reason fields or	callers
	 * decide otherwise for	specific commands.
	 */
	start_slot = fas->f_next_slot;
	slot = start_slot;
	do {
		fas_check_ncmds(fas);
		fas_mark_packets(fas, slot, CMD_RESET, STAT_BUS_RESET);
		fas_flush_tagQ(fas, slot);
		fas_flush_readyQ(fas, slot);
		if (fas->f_arq_pkt[slot]) {
			struct fas_cmd *sp = fas->f_arq_pkt[slot];
			struct arq_private_data	*arq_data =
			(struct	arq_private_data *)(sp->cmd_pkt->pkt_private);
			ASSERT(arq_data->arq_save_sp ==	NULL);
		}
		slot = NEXTSLOT(slot, fas->f_dslot);
	} while	(slot != start_slot);

	fas_check_ncmds(fas);

	/*
	 * reset timeouts
	 */
	for (i = 0; i <	N_SLOTS; i++) {
		if (fas->f_active[i]) {
			fas->f_active[i]->f_timebase = 0;
			fas->f_active[i]->f_timeout = 0;
			fas->f_active[i]->f_dups = 0;
		}
	}

	/*
	 * Move	the state back to free...
	 */
	New_state(fas, STATE_FREE);
	ASSERT(fas->f_ncmds >= fas->f_ndisc);

	/*
	 * perform the reset notification callbacks that are registered.
	 */
	(void) scsi_hba_reset_notify_callback(&fas->f_mutex,
		&fas->f_reset_notify_listf);

	/*
	 * if reset delay is still active a search is meaningless
	 * but do it anyway
	 */
	return (ACTION_SEARCH);
}

#ifdef FAS_TEST
/*
 * torture test	functions
 */
static void
fas_test_reset(struct fas *fas,	int slot)
{
	struct scsi_address ap;
	char target = slot/NLUNS_PER_TARGET;

	if (fas_rtest &	(1 << target)) {
		ap.a_hba_tran =	fas->f_tran;
		ap.a_target = target;
		ap.a_lun = ap.a_sublun = 0;
		if ((fas_rtest_type == 1) &&
		    (fas->f_state == ACTS_DATA_DONE)) {
			if (fas_do_scsi_reset(&ap, RESET_TARGET)) {
				fas_rtest = 0;
			}
		} else if ((fas_rtest_type == 2) &&
		    (fas->f_state == ACTS_DATA_DONE)) {
			if (fas_do_scsi_reset(&ap, RESET_ALL)) {
				fas_rtest = 0;
				if (fas_test_stop) {
					debug_enter("bus reset succeeded");
				}
			}
		} else {
			if (fas_do_scsi_reset(&ap, RESET_TARGET)) {
				fas_rtest = 0;
				if (fas_test_stop) {
					debug_enter("device reset succeeded");
				}
			}
		}
	}
}

static void
fas_test_abort(struct fas *fas,	int slot)
{
	struct fas_cmd *sp = fas->f_current_sp;
	struct scsi_address ap;
	char target = slot/NLUNS_PER_TARGET;
	struct scsi_pkt	*pkt = NULL;

	if (fas_atest &	(1 << target)) {
		ap.a_hba_tran =	fas->f_tran;
		ap.a_target = target;
		ap.a_lun = ap.a_sublun = 0;

		if ((fas_atest_disc == 0) && sp	&&
		    ((sp->cmd_flags & CFLAG_CMDDISC) ==	0)) {
			pkt = sp->cmd_pkt;
		} else if ((fas_atest_disc == 1) && NOTAG(target)) {
			sp = fas->f_active[slot]->f_slot[0];
			if (sp && (sp->cmd_flags & CFLAG_CMDDISC)) {
				pkt = sp->cmd_pkt;
			}
		} else if ((fas_atest_disc == 1) && (sp	== 0) &&
		    TAGGED(target) &&
		    (fas->f_tcmds[slot]	!= 0)) {
			int tag;
			/*
			 * find	the oldest tag
			 */
			for (tag = NTAGS-1; tag	>= 0; tag--) {
			    if ((sp = fas->f_active[slot]->f_slot[tag])	!= 0)
				break;
			}
			if (sp)	{
				pkt = sp->cmd_pkt;
			}
		} else if (fas_atest_disc == 2 && (sp == 0) &&
		    (fas->f_tcmds[slot]	!= 0)) {
			pkt = NULL;
		} else if (fas_atest_disc == 2 && NOTAG(target)) {
			pkt = NULL;
		} else if (fas_atest_disc == 3 && fas->f_readyf[slot]) {
			pkt = fas->f_readyf[slot]->cmd_pkt;
		} else if (fas_atest_disc == 4 &&
		    fas->f_readyf[slot]	&& fas->f_readyf[slot]->cmd_forw) {
			pkt = fas->f_readyf[slot]->cmd_forw->cmd_pkt;
		} else if (fas_atest_disc == 5 && fas->f_readyb[slot]) {
			pkt = fas->f_readyb[slot]->cmd_pkt;
		} else if ((fas_atest_disc == 6) &&
		    (fas->f_state == ACTS_DATA_DONE)) {
			pkt = sp->cmd_pkt;
		} else {
			return;
		}
		if (fas_do_scsi_abort(&ap, pkt)) {
			fas_atest = 0;
			if (fas_test_stop) {
				debug_enter("abort succeeded");
			}
		}
	}
}
#endif /* FAS_TEST */

/*
 * capability interface
 */
static int
fas_commoncap(struct scsi_address *ap, char *cap, int val,
    int	tgtonly, int doset)
{
	register struct	fas *fas = ADDR2FAS(ap);
	register cidx;
	register int target = ap->a_target;
	register u_short tshift	= (1<<target);
	register u_short ntshift = ~tshift;
	register rval =	FALSE;

	mutex_enter(FAS_MUTEX(fas));
	ASSERT(!fas->f_suspended);

	if (cap	== (char *)0) {
		goto exit;
	}

	cidx = scsi_hba_lookup_capstr(cap);
	if (cidx == -1)	{
		rval = UNDEFINED;
	} else if (doset) {
		/*
		 * we usually don't allow setting capabilities for
		 * other targets!
		 */
		if (!tgtonly) {
			goto exit;
		}
		switch (cidx) {
		case SCSI_CAP_DMA_MAX:
		case SCSI_CAP_MSG_OUT:
		case SCSI_CAP_PARITY:
		case SCSI_CAP_INITIATOR_ID:
		case SCSI_CAP_LINKED_CMDS:
		case SCSI_CAP_UNTAGGED_QING:
		case SCSI_CAP_RESET_NOTIFICATION:
			/*
			 * None	of these are settable via
			 * the capability interface.
			 */
			break;

		case SCSI_CAP_DISCONNECT:
			if (val)
				fas->f_target_scsi_options[ap->a_target] |=
				    SCSI_OPTIONS_DR;
			else
				fas->f_target_scsi_options[ap->a_target] &=
				    ~SCSI_OPTIONS_DR;

			break;

		case SCSI_CAP_SYNCHRONOUS:
			if (val) {
				/*
				 * if we have disabled sync, don't
				 * allow enabling it
				 */
				if (fas->f_nosync & tshift) {
					break;
				}
				if ((fas->f_target_scsi_options[target]	&
				    SCSI_OPTIONS_SYNC))	{
					fas->f_offset[target] =
					    fas_default_offset;
				} else {
					break;
				}
			} else {
				fas->f_offset[target] =	0;
			}
			fas->f_sync_known &= ntshift;
			fas->f_sync_enabled &= ntshift;
			rval = TRUE;
			break;

		case SCSI_CAP_TAGGED_QING:
		{
			int slot = target * NLUNS_PER_TARGET | ap->a_lun;
			u_short	old_notag = fas->f_notag;

			/* do not allow	with active tgt	*/
			if (fas->f_tcmds[slot])	{
				break;
			}

			slot =	target * NLUNS_PER_TARGET | ap->a_lun;

			if (val) {
				if (fas->f_target_scsi_options[target] &
				    SCSI_OPTIONS_TAG) {
					IPRINTF1("target %d: TQ enabled\n",
					    target);
					fas->f_notag &=	ntshift;
				} else {
					break;
				}
			} else {
				IPRINTF1("target %d: TQ disabled\n",
				    target);
				fas->f_notag |=	tshift;
			}

			if (val	&& fas_alloc_active_slots(fas, slot,
			    KM_NOSLEEP)) {
				fas->f_notag = old_notag;
				break;
			}

			fas_set_throttles(fas, slot,
			    NLUNS_PER_TARGET, MAX_THROTTLE);

			fas_update_props(fas, target);
			rval = TRUE;
			break;
		}

		case SCSI_CAP_WIDE_XFER:
			if (val) {
				if (fas->f_target_scsi_options[target] &
				    SCSI_OPTIONS_WIDE) {
					fas->f_nowide &= ntshift;
				} else {
					break;
				}
			} else {
				fas->f_nowide |= tshift;
			}
			fas->f_wide_known &= ntshift;
			fas->f_wide_enabled &= ntshift;
			rval = TRUE;
			break;

		case SCSI_CAP_ARQ:
			if (val) {
				if (fas_create_arq_pkt(fas, ap)) {
					break;
				}
			} else {
				if (fas_delete_arq_pkt(fas, ap)) {
					break;
				}
			}
			rval = TRUE;
			break;

		case SCSI_CAP_QFULL_RETRIES:
			fas->f_qfull_retries[target] = (u_char)val;
			rval = TRUE;
			break;

		case SCSI_CAP_QFULL_RETRY_INTERVAL:
			fas->f_qfull_retry_interval[target] =
					drv_usectohz(val * 1000);
			rval = TRUE;
			break;

		default:
			rval = UNDEFINED;
			break;
		}

	} else if (doset == 0) {
		int slot = target * NLUNS_PER_TARGET | ap->a_lun;

		switch (cidx) {
		case SCSI_CAP_DMA_MAX:
			/* very	high limit because of multiple dma windows */
			rval = 1<<30;
			break;
		case SCSI_CAP_MSG_OUT:
			rval = TRUE;
			break;
		case SCSI_CAP_DISCONNECT:
			if (tgtonly &&
			    (fas->f_target_scsi_options[target]	&
				SCSI_OPTIONS_DR)) {
				rval = TRUE;
			}
			break;
		case SCSI_CAP_SYNCHRONOUS:
			if (tgtonly && fas->f_offset[target]) {
				rval = TRUE;
			}
			break;
		case SCSI_CAP_PARITY:
			rval = TRUE;
			break;
		case SCSI_CAP_INITIATOR_ID:
			rval = MY_ID(fas);
			break;
		case SCSI_CAP_TAGGED_QING:
			if (tgtonly && ((fas->f_notag &	tshift)	== 0)) {
				rval = TRUE;
			}
			break;
		case SCSI_CAP_WIDE_XFER:
			if ((tgtonly &&	(fas->f_nowide & tshift) == 0))	{
				rval = TRUE;
			}
			break;
		case SCSI_CAP_UNTAGGED_QING:
			rval = TRUE;
			break;
		case SCSI_CAP_ARQ:
			if (tgtonly && fas->f_arq_pkt[slot]) {
				rval = TRUE;
			}
			break;
		case SCSI_CAP_LINKED_CMDS:
			break;
		case SCSI_CAP_RESET_NOTIFICATION:
			rval = TRUE;
			break;
		case SCSI_CAP_QFULL_RETRIES:
			rval = fas->f_qfull_retries[target];
			break;
		case SCSI_CAP_QFULL_RETRY_INTERVAL:
			rval = drv_hztousec(
				fas->f_qfull_retry_interval[target]) /
				1000;
			break;

		default:
			rval = UNDEFINED;
			break;
		}
	}
exit:
	if (val	&& tgtonly) {
		fas_update_props(fas, target);
	}
	fas_check_waitQ_and_mutex_exit(fas);

	if (doset) {
		IPRINTF6(
	    "fas_commoncap:tgt=%x,cap=%s,tgtonly=%x,doset=%x,val=%x,rval=%x\n",
		target,	cap, tgtonly, doset, val, rval);
	}
	return (rval);
}

/*
 * property management
 * fas_update_props:
 * create/update sync/wide/TQ/scsi-options properties for this target
 */
static void
fas_update_props(struct	fas *fas, int tgt)
{
	char	property[32];
	u_int	xfer_speed = 0;
	u_int	xfer_rate = 0;
	int	wide_enabled, tq_enabled;
	u_int	regval = fas->f_sync_period[tgt];
	int	offset = fas->f_offset[tgt];

	wide_enabled = ((fas->f_nowide & (1<<tgt)) == 0);
	if (offset && regval) {
		xfer_speed =
			FAS_SYNC_KBPS((regval *	fas->f_clock_cycle) / 1000);
		xfer_rate = ((wide_enabled)? 2 : 1) * xfer_speed;
	}
	(void) sprintf(property, "target%x-scsi-options", tgt);
	fas_update_this_prop(fas, property, fas->f_target_scsi_options[tgt]);

	(void) sprintf(property, "target%x-sync-speed",	tgt);
	fas_update_this_prop(fas, property, xfer_rate);

	(void) sprintf(property, "target%x-wide", tgt);
	fas_update_this_prop(fas, property, wide_enabled);

	(void) sprintf(property, "target%x-TQ",	tgt);
	tq_enabled = ((fas->f_notag & (1<<tgt))? 0 : 1);
	fas_update_this_prop(fas, property, tq_enabled);

}

static void
fas_update_this_prop(struct fas	*fas, char *property, int value)
{
	dev_info_t *dip	= fas->f_dev;

	IPRINTF2("update prop: %s %x value=%x\n", property, value);
	if (ddi_prop_update_int(DDI_DEV_T_NONE,	dip,
	    property, value) !=	DDI_PROP_SUCCESS)	{
		IPRINTF1("cannot modify/create %s property\n",	property);
	}
}

/*
 * allocate active slots array,	size is	dependent on whether tagQ enabled
 */
static int
fas_alloc_active_slots(struct fas *fas,	int slot, int flag)
{
	int target = slot / NLUNS_PER_TARGET;
	struct f_slots *old_active = fas->f_active[slot];
	struct f_slots *new_active;
	u_short	size;
	int rval = -1;

	if (fas->f_tcmds[slot])	{
		IPRINTF("cannot change size of active slots array\n");
		return (rval);
	}

	size = ((NOTAG(target))	? FAS_F_SLOT_SIZE : FAS_F_SLOTS_SIZE_TQ);
	EPRINTF4(
	"fas_alloc_active_slots: target=%x size=%x, old=%x, oldsize=%x\n",
		target,	size, old_active,
		((old_active ==	NULL) ?	-1 : old_active->f_size));

	new_active = (struct f_slots *)kmem_zalloc(size, flag);
	if (new_active == NULL)	{
		IPRINTF("new active alloc failed\n");
	} else {
		fas->f_active[slot] = new_active;
		fas->f_active[slot]->f_n_slots = (NOTAG(target)	? 1 : NTAGS);
		fas->f_active[slot]->f_size = size;
		/*
		 * reserve tag 0 for non-tagged	cmds to	tagged targets
		 */
		if (TAGGED(target)) {
			fas->f_active[slot]->f_tags = 1;
		}
		if (old_active)	{
			kmem_free((caddr_t)old_active, old_active->f_size);
		}
		rval = 0;
	}
	return (rval);
}

/*
 * Error logging, printing, and	debug print routines
 */
static char *fas_label = "fas";

static void
fas_log(struct fas *fas, int level, const char *fmt, ...)
{
	char buf[256];
	dev_info_t *dev;
	va_list	ap;

	if (fas) {
		dev = fas->f_dev;
	} else {
		dev = 0;
	}

	va_start(ap, fmt);
	(void) vsprintf(buf, fmt, ap);
	va_end(ap);

	scsi_log(dev, fas_label, level,	"%s\n",	buf);
}

/*VARARGS2*/
void
fas_printf(struct fas *fas, const char *fmt, ...)
{
	char buf [256];
	dev_info_t *dev;
	va_list	ap;

	if (fas) {
		dev = fas->f_dev;
	} else {
		dev = 0;
	}

	va_start(ap, fmt);
	(void) vsprintf(buf, fmt, ap);
	va_end(ap);

	{
		char label[32];
		if (dev) {
			(void) sprintf(label, "%s%d", fas_label, CNUM);
		} else {
			(void) sprintf(label, "%s", fas_label);
		}
		scsi_log(dev, label, CE_CONT, "%s", buf);
	}
}

static void
fas_printstate(register	struct fas *fas, char *msg)
{
	register volatile struct fasreg	*fasreg	= fas->f_reg;
	volatile struct	dma *dmar = fas->f_dma;
	u_int csr = fas_dma_reg_read(fas, &dmar->dma_csr);
	u_int count = fas_dma_reg_read(fas, &dmar->dma_count);
	u_int addr = fas_dma_reg_read(fas, &dmar->dma_addr);
	u_int test = fas_dma_reg_read(fas, &dmar->dma_test);
	u_int fas_cnt;

	fas_log(fas, CE_WARN, "%s: current fas state:",	msg);
	fas_printf(NULL, "Latched stat=0x%b intr=0x%b\n",
	    fas->f_stat, FAS_STAT_BITS,	fas->f_intr, FAS_INT_BITS);
	fas_printf(NULL, "last msgout: %s, last msgin: %s\n",
	    scsi_mname(fas->f_last_msgout), scsi_mname(fas->f_last_msgin));
	fas_printf(NULL, "DMA csr=0x%b\n", csr,	dma_bits);
	fas_printf(NULL,
	    "addr=%x dmacnt=%x test=%x last=%x last_cnt=%x\n",
	    addr, count, test, fas->f_lastdma, fas->f_lastcount);

	GET_FAS_COUNT(fasreg, fas_cnt);
	fas_printf(NULL, "fas state:\n");
	fas_printf(NULL, "\tcount(32)=%x cmd=%x stat=%x stat2=%x intr=%x\n",
	    fas_cnt, fasreg->fas_cmd, fasreg->fas_stat,	fasreg->fas_stat2,
	    fasreg->fas_intr);
	fas_printf(NULL,
	"\tstep=%x fifoflag=%x conf=%x test=%x conf2=%x conf3=%x\n",
	    fasreg->fas_step, fasreg->fas_fifo_flag, fasreg->fas_conf,
	    fasreg->fas_test, fasreg->fas_conf2, fasreg->fas_conf3);

	if (fas->f_current_sp) {
		fas_dump_cmd(fas, fas->f_current_sp);
	}
}

/*
 * dump	all we know about a cmd
 */
static void
fas_dump_cmd(struct fas	*fas, struct fas_cmd *sp)
{
	register i;
	register u_char	*cp = (u_char *) sp->cmd_pkt->pkt_cdbp;
	auto char buf[128];

	buf[0] = '\0';
	fas_printf(NULL, "Cmd dump for Target %d Lun %d:\n",
	    Tgt(sp), Lun(sp));
	(void) sprintf(&buf[0], " cdb=[");
	for (i = 0; i <	(int)sp->cmd_actual_cdblen; i++) {
		(void) sprintf(&buf[strlen(buf)], " 0x%x", *cp++);
	}
	(void) sprintf(&buf[strlen(buf)], " ]\n");
	fas_printf(NULL, buf);
	fas_printf(NULL, "State=%s Last State=%s\n",
	    fas_state_name(fas->f_state), fas_state_name(fas->f_laststate));
	fas_printf(NULL,
	    "pkt_state=0x%b pkt_flags=0x%x pkt_statistics=0x%x\n",
	    sp->cmd_pkt->pkt_state, scsi_state_bits, sp->cmd_pkt_flags,
	    sp->cmd_pkt->pkt_statistics);
	if (sp->cmd_pkt->pkt_state & STATE_GOT_STATUS) {
		fas_printf(NULL, "Status=0x%x\n", sp->cmd_pkt->pkt_scbp[0]);
	}
}

/*
 * state decoding for error messages
 */
static char *
fas_state_name(u_short state)
{
	if (state == STATE_FREE) {
		return ("FREE");
	} else if (state & STATE_SELECTING) {
		if (state == STATE_SELECT_NORMAL)
			return ("SELECT");
		else if	(state == STATE_SELECT_N_STOP)
			return ("SEL&STOP");
		else if	(state == STATE_SELECT_N_SENDMSG)
			return ("SELECT_SNDMSG");
		else
			return ("SEL_NO_ATN");
	} else {
		static struct {
			char *sname;
			char state;
		} names[] = {
			"CMD_START",		ACTS_CMD_START,
			"CMD_DONE",		ACTS_CMD_DONE,
			"MSG_OUT",		ACTS_MSG_OUT,
			"MSG_OUT_DONE",		ACTS_MSG_OUT_DONE,
			"MSG_IN",		ACTS_MSG_IN,
			"MSG_IN_MORE",		ACTS_MSG_IN_MORE,
			"MSG_IN_DONE",		ACTS_MSG_IN_DONE,
			"CLEARING",		ACTS_CLEARING,
			"DATA",			ACTS_DATA,
			"DATA_DONE",		ACTS_DATA_DONE,
			"CMD_CMPLT",		ACTS_C_CMPLT,
			"UNKNOWN",		ACTS_UNKNOWN,
			"RESEL",		ACTS_RESEL,
			"ENDVEC",		ACTS_ENDVEC,
			"RESET",		ACTS_RESET,
			"ABORTING",		ACTS_ABORTING,
			"FROZEN",		ACTS_FROZEN,
			0
		};
		register i;
		for (i = 0; names[i].sname; i++) {
			if (names[i].state == state)
				return (names[i].sname);
		}
	}
	return ("<BAD>");
}
