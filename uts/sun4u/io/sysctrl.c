/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)sysctrl.c 1.23	95/06/02 SMI"

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/ddi_impldefs.h>
#include <sys/obpdefs.h>
#include <sys/cmn_err.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/debug.h>
#include <sys/sysmacros.h>
#include <sys/ivintr.h>
#include <sys/autoconf.h>
#include <sys/intreg.h>
#include <sys/proc.h>
#include <sys/modctl.h>
#include <sys/fhc.h>
#include <sys/sysctrl.h>

/* Useful debugging Stuff */
#include <sys/nexusdebug.h>

/*
 * Function prototypes
 */
static int sysctrl_identify(dev_info_t *devi);

static int sysctrl_attach(dev_info_t *devi, ddi_attach_cmd_t cmd);

static int sysctrl_detach(dev_info_t *devi, ddi_detach_cmd_t cmd);

static void nvram_update_powerfail(struct sysctrl_soft_state * softsp,
					u_int pattern);

static u_int system_high_handler(caddr_t arg);

static u_int spur_delay(caddr_t arg);

static void spur_retry(caddr_t arg);

static u_int spur_reenable(caddr_t arg);

static void spur_long_timeout(caddr_t arg);

static u_int spur_clear_count(caddr_t arg);

static u_int ac_fail_handler(caddr_t arg);

static void ac_fail_retry(caddr_t arg);

static u_int ac_fail_reenable(caddr_t arg);

static u_int ps_fail_int_handler(caddr_t arg);

static u_int ps_fail_poll_handler(caddr_t arg);

static u_int ps_fail_handler(struct sysctrl_soft_state * softsp, int fromint);

static void ps_log_state_change(int index, int present);

static void ps_log_pres_change(int index, int present);

static void ps_fail_retry(caddr_t arg);

static u_int pps_fanfail_handler(caddr_t arg);

static void pps_fanfail_retry(caddr_t arg);

static u_int pps_fanfail_reenable(caddr_t arg);

static void start_pps_fan_timeout(struct sysctrl_soft_state *softsp,
					enum pps_fan_type type);

static void pps_fan_ok(caddr_t arg);

static u_int bd_insert_handler(caddr_t arg);

static void bd_insert_timeout(caddr_t arg);

static u_int bd_insert_normal(caddr_t arg);

static void sysctrl_add_kstats(struct sysctrl_soft_state *softsp);

static int sysctrl_kstat_update(kstat_t *ksp, int rw);

static void init_remote_console_uart(struct sysctrl_soft_state *);

static void blink_led_timeout(caddr_t arg);

static u_int blink_led_handler(caddr_t arg);

static void overtemp_wakeup(void);

static void sysctrl_overtemp_poll(void);

/*
 * Configuration data structures
 */
static struct cb_ops sysctrl_cb_ops = {
	nulldev,		/* open */
	nulldev,		/* close */
	nulldev,		/* strategy */
	nulldev,		/* print */
	nulldev,		/* dump */
	nulldev,		/* read */
	nulldev,		/* write */
	nulldev,		/* ioctl */
	nodev,			/* devmap */
	nodev,			/* mmap */
	nodev,			/* segmap */
	nochpoll,		/* poll */
	ddi_prop_op,		/* cb_prop_op */
	0,			/* streamtab */
	D_MP|D_NEW		/* Driver compatibility flag */
};

static struct dev_ops sysctrl_ops = {
	DEVO_REV,		/* devo_rev */
	0,			/* refcnt */
	ddi_no_info,		/* getinfo */
	sysctrl_identify,	/* identify */
	nulldev,		/* probe */
	sysctrl_attach,		/* attach */
	sysctrl_detach,		/* detach */
	nulldev,		/* reset */
	&sysctrl_cb_ops,	/* cb_ops */
	(struct bus_ops *)0,	/* bus_ops */
};

void *sysctrlp;				/* sysctrl soft state hook */

/* # of secs to silence spurious interrupts */
static int spur_timeout_hz = SPUR_TIMEOUT_SEC * HZ;

/* # of secs to count spurious interrupts to print message */
static int spur_long_timeout_hz = SPUR_LONG_TIMEOUT_SEC * HZ;

/* # of secs between AC failure polling */
static int ac_timeout_hz = AC_TIMEOUT_SEC * HZ;

/* # of secs between Power Supply Failure polling */
static int ps_fail_timeout_hz = PS_FAIL_TIMEOUT_SEC * HZ;

/* # of secs between Peripheral Power Supply failure polling */
static int pps_fan_timeout_hz = PPS_FAN_TIMEOUT_SEC * HZ;

/* # of secs for an OK message when a fan goes good */
static int pps_fan_ok_timeout_hz = PPS_FAN_OK_TIMEOUT_SEC * HZ;

/* # of secs between Board Insertion polling */
static int bd_insert_timeout_hz = BRD_INSERT_TIMEOUT_SEC * HZ;

/* # of secs between toggle of OS LED */
static int blink_led_timeout_hz = BLINK_LED_TIMEOUT_SEC * HZ;

/* overtemp polling routine timeout delay */
static int overtemp_timeout_hz = OVERTEMP_TIMEOUT_SEC * HZ;

/* Specify which system interrupt condition to monitor */
int enable_sys_interrupt = SYS_AC_PWR_FAIL_EN | SYS_PPS_FAN_FAIL_EN |
			SYS_PS_FAIL_EN | SYS_SBRD_PRES_EN;

/* Should the overtemp_poll thread be running? */
static int sysctrl_do_overtemp_thread = 1;

/* Hack for the bringup remote console support */
#ifdef MPSAS
int enable_remote_console_reset = 0;
#else
int enable_remote_console_reset = 1;
#endif	/* MPSAS */

/* Indicates whether or not the overtemp thread has been started */
static int sysctrl_overtemp_thread_started = 0;

/*
 * Mutex used to protect the soft state list and their data.
 */
static kmutex_t overtemp_mutex;

/* The CV is used to wakeup the thread when needed. */
static kcondvar_t overtemp_cv;

/*
 * Linked list of all syctrl soft state struyctres. This is used for
 * temperature polling.
 */
struct sysctrl_soft_state *sys_list = NULL;

extern struct mod_ops mod_driverops;

static struct modldrv modldrv = {
	&mod_driverops,		/* Type of module.  This one is a driver */
	"Clock Board",		/* name of module */
	&sysctrl_ops,		/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1,		/* rev */
	(void *)&modldrv,
	NULL
};

#ifndef lint
static char _depends_on[] = "drv/fhc";
#endif  /* lint */

/*
 * These are the module initialization routines.
 */

int
_init(void)
{
	int error;

	if ((error = ddi_soft_state_init(&sysctrlp,
	    sizeof (struct sysctrl_soft_state), 1)) != 0)
		return (error);

	return (mod_install(&modlinkage));
}

int
_fini(void)
{
	int error;

	if ((error = mod_remove(&modlinkage)) != 0)
		return (error);

	ddi_soft_state_fini(&sysctrlp);
	return (0);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

static int
sysctrl_identify(dev_info_t *devi)
{
	char *name = ddi_get_name(devi);
	int rc = DDI_NOT_IDENTIFIED;

	/*
	 * Our device name is kind of wierd here. 'clock-board' would not
	 * be a good name for a device driver, so we call it system
	 * control, or 'sysctrl'.
	 */
	if ((strcmp(name, "clock-board") == 0) ||
	    (strcmp(name, "sysctrl") == 0)) {
		rc = DDI_IDENTIFIED;
	}

	return (rc);
}

static int
sysctrl_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	struct sysctrl_soft_state *softsp;
	int instance;
	u_char tmp_reg;
	char namebuf[128];

	switch (cmd) {
	case DDI_ATTACH:
		break;

	case DDI_RESUME:
		return (DDI_SUCCESS);

	default:
		return (DDI_FAILURE);
	}

	instance = ddi_get_instance(devi);

	if (ddi_soft_state_zalloc(sysctrlp, instance) != DDI_SUCCESS)
		return (DDI_FAILURE);

	softsp = ddi_get_soft_state(sysctrlp, instance);

	/* Set the dip in the soft state */
	softsp->dip = devi;

	/* Set up the parent dip */
	softsp->pdip = ddi_get_parent(softsp->dip);

	DPRINTF(SYSCTRL_ATTACH_DEBUG, ("sysctrl: devi= 0x%x\n, softsp=0x%x\n",
		devi, softsp));

	/*
	 * Map in the registers sets that OBP hands us. According
	 * to the sun4u device tree spec., the register sets are as
	 * follows:
	 *
	 *	0	Clock Frequency Registers (contains the bit
	 *		for enabling the remote console reset)
	 *	1	misc (has all the registers that we need
	 */
	if (ddi_map_regs(softsp->dip, 0,
	    (caddr_t *)&softsp->clk_freq1, 0, 0)) {
		cmn_err(CE_CONT, "sysctrl: unable to map internal registers");
		goto bad;
	}

	if (ddi_map_regs(softsp->dip, 1,
	    (caddr_t *)&softsp->csr, 0, 0)) {
		cmn_err(CE_CONT, "sysctrl: unable to map Clock Frequency "
			"registers");
		goto bad;
	}

	/*
	 * Fill in the virtual addresses of the registers in the
	 * sysctrl_soft_state structure. We do not want to calculate
	 * them on the fly. This way we waste a little memory, but
	 * avoid bugs down the road.
	 */
	softsp->clk_freq2 = (u_char *) ((caddr_t)softsp->clk_freq1 +
		SYS_OFF_CLK_FREQ2);

	softsp->status1 = (u_char *) ((caddr_t)softsp->csr +
		SYS_OFF_STAT1);

	softsp->status2 = (u_char *) ((caddr_t)softsp->csr +
		SYS_OFF_STAT2);

	softsp->ps_stat = (u_char *) ((caddr_t)softsp->csr +
		SYS_OFF_PSSTAT);

	softsp->ps_pres = (u_char *) ((caddr_t)softsp->csr +
		SYS_OFF_PSPRES);

	/*
	 * Map in and configure the remote console UART.
	 */
	if (enable_remote_console_reset) {
		/*
		 * There is no OBP register set for the remote console UART,
		 * so offset from the last register set, the misc register
		 * set, in order to map in the remote console UART.
		 */
		if (ddi_map_regs(softsp->dip, 1, (caddr_t *)&softsp->rcons_ctl,
		    RMT_CONS_OFFSET, RMT_CONS_LEN)) {
			cmn_err(CE_CONT, "sysctrl: unable to map remote "
				"console UART registers");
		} else {
			/*
			 * Program the UART to watch ttya console.
			 */
			init_remote_console_uart(softsp);

			/* Now enable the remote console reset control bits. */
			*(softsp->clk_freq2) |= RCONS_UART_EN;

			/* flush the hardware buffers */
			tmp_reg = *(softsp->csr);

			/* Now warn the user that remote reset is enabled */
			cmn_err(CE_NOTE,
				"?sysctrl: Remote console reset enabled");
		}
	}

	softsp->temp_reg = (u_char *) ((caddr_t)softsp->csr +
		SYS_OFF_TEMP);

	/* initialize the bit field for all pps fans to assumed good */
	softsp->pps_fan_external_state = SYS_AC_FAN_OK | SYS_KEYSW_FAN_OK;

	/*
	 * go ahead and setup the fan_arg structs since the values are
	 * constants.
	 */
	softsp->fan_arg[RACK].self = softsp;
	softsp->fan_arg[RACK].type = RACK;

	softsp->fan_arg[AC].self = softsp;
	softsp->fan_arg[AC].type = AC;

	softsp->fan_arg[KEYSW].self = softsp;
	softsp->fan_arg[KEYSW].type = KEYSW;

	/* shut off all interrupt sources */
	*(softsp->csr) &= ~(SYS_PPS_FAN_FAIL_EN | SYS_PS_FAIL_EN |
				SYS_AC_PWR_FAIL_EN | SYS_SBRD_PRES_EN);
	tmp_reg = *(softsp->csr);
#ifdef lint
	tmp_reg = tmp_reg;
#endif

	/*
	 * Now register our high interrupt with the system.
	 */
	if (ddi_add_intr(devi, 0, &softsp->iblock,
	    &softsp->idevice, (u_int (*)(caddr_t))nulldev, NULL) !=
	    DDI_SUCCESS)
		goto bad;

	(void) sprintf(namebuf, "sysctrl high mutex softsp 0x%0x",
		(int)softsp);
	mutex_init(&softsp->csr_mutex, namebuf, MUTEX_DRIVER,
		(void *)softsp->iblock);

	ddi_remove_intr(devi, 0, softsp->iblock);

	if (ddi_add_intr(devi, 0, &softsp->iblock,
	    &softsp->idevice, system_high_handler, (caddr_t) softsp) !=
	    DDI_SUCCESS)
		goto bad;

	if (ddi_add_softintr(devi, DDI_SOFTINT_LOW, &softsp->spur_id,
	    &softsp->spur_int_c, NULL, spur_delay, (caddr_t) softsp) !=
	    DDI_SUCCESS)
		goto bad;

	(void) sprintf(namebuf, "sysctrl spur int mutex softsp 0x%0x",
		(int)softsp);
	mutex_init(&softsp->spur_int_lock, namebuf, MUTEX_DRIVER,
		(void *)softsp->spur_int_c);


	if (ddi_add_softintr(devi, DDI_SOFTINT_LOW, &softsp->spur_high_id,
	    NULL, NULL, spur_reenable, (caddr_t) softsp) != DDI_SUCCESS)
		goto bad;

	if (ddi_add_softintr(devi, DDI_SOFTINT_LOW, &softsp->spur_long_to_id,
	    NULL, NULL, spur_clear_count, (caddr_t) softsp) != DDI_SUCCESS)
		goto bad;

	/*
	 * Now register low-level ac fail handler
	 */
	if (ddi_add_softintr(devi, DDI_SOFTINT_HIGH, &softsp->ac_fail_id,
	    NULL, NULL, ac_fail_handler, (caddr_t)softsp) != DDI_SUCCESS)
		goto bad;

	if (ddi_add_softintr(devi, DDI_SOFTINT_LOW, &softsp->ac_fail_high_id,
	    NULL, NULL, ac_fail_reenable, (caddr_t)softsp) != DDI_SUCCESS)
		goto bad;

	/*
	 * Now register low-level ps fail handler
	 */

	if (ddi_add_softintr(devi, DDI_SOFTINT_HIGH, &softsp->ps_fail_int_id,
	    &softsp->ps_fail_c, NULL, ps_fail_int_handler, (caddr_t)softsp) !=
	    DDI_SUCCESS)
		goto bad;

	(void) sprintf(namebuf, "sysctrl ps fail mutex softsp 0x%0x",
		(int)softsp);
	mutex_init(&softsp->ps_fail_lock, namebuf, MUTEX_DRIVER,
		(void *)softsp->ps_fail_c);

	if (ddi_add_softintr(devi, DDI_SOFTINT_LOW, &softsp->ps_fail_poll_id,
	    NULL, NULL, ps_fail_poll_handler, (caddr_t)softsp) !=
	    DDI_SUCCESS)
		goto bad;

	/*
	 * Now register low-level pps fan fail handler
	 */
	if (ddi_add_softintr(devi, DDI_SOFTINT_LOW, &softsp->pps_fan_id,
	    &softsp->pps_fan_c, NULL, pps_fanfail_handler, (caddr_t)softsp) !=
	    DDI_SUCCESS)
		goto bad;

	(void) sprintf(namebuf, "sysctrl pps fan mutex softsp 0x%0x",
		(int)softsp);
	mutex_init(&softsp->pps_fan_lock, namebuf, MUTEX_DRIVER,
		(void *)softsp->pps_fan_c);

	if (ddi_add_softintr(devi, DDI_SOFTINT_LOW, &softsp->pps_fan_high_id,
	    NULL, NULL, pps_fanfail_reenable, (caddr_t)softsp) !=
	    DDI_SUCCESS)
		goto bad;

	/*
	 * Now register low-level board insert handler
	 */
	if (ddi_add_softintr(devi, DDI_SOFTINT_LOW, &softsp->sbrd_pres_id,
	    &softsp->sbrd_pres_c, NULL, bd_insert_handler,
	    (caddr_t)softsp) != DDI_SUCCESS)
		goto bad;

	(void) sprintf(namebuf, "sysctrl sbrd_pres mutex softsp 0x%0x",
		(int)softsp);
	mutex_init(&softsp->sbrd_pres_lock, namebuf, MUTEX_DRIVER,
		(void *)softsp->sbrd_pres_c);

	if (ddi_add_softintr(devi, DDI_SOFTINT_LOW, &softsp->sbrd_gone_id,
	    NULL, NULL, bd_insert_normal, (caddr_t)softsp) != DDI_SUCCESS)
		goto bad;

	/*
	 * Now register led blink handler (interrupt level)
	 */
	if (ddi_add_softintr(devi, DDI_SOFTINT_LOW, &softsp->blink_led_id,
	    NULL, NULL, blink_led_handler, (caddr_t)softsp) != DDI_SUCCESS)
		goto bad;

	/* XXX start the board presence polling machine */
	/* XXX clear the board presence signals */
	/* XXX initialize the board state array */


	/* prime the power supply state machines */
	if (enable_sys_interrupt & SYS_PS_FAIL_EN)
		ddi_trigger_softintr(softsp->ps_fail_poll_id);

	/* kick off the OS led blinker */
	ddi_trigger_softintr(softsp->blink_led_id);

	/* Now enable selected interrupt sources */
	mutex_enter(&softsp->csr_mutex);
	*(softsp->csr) |= enable_sys_interrupt &
		(SYS_AC_PWR_FAIL_EN | SYS_PS_FAIL_EN |
		SYS_PPS_FAN_FAIL_EN | SYS_SBRD_PRES_EN);
	tmp_reg = *(softsp->csr);
#ifdef lint
	tmp_reg = tmp_reg;
#endif
	mutex_exit(&softsp->csr_mutex);

	/* Initialize the temperature */
	init_temp_arrays(&softsp->tempstat);

	/*
	 * Now add this soft state structure to the front of the linked list
	 * of soft state structures.
	 */
	mutex_enter(&overtemp_mutex);
	softsp->next = sys_list;
	sys_list = softsp;
	mutex_exit(&overtemp_mutex);

	/* Setup the kstats for this device */
	sysctrl_add_kstats(softsp);

	if (sysctrl_overtemp_thread_started == 0) {

		/*
		 * set up the overtemp mutex and condition variable before
		 * starting the thread.
		 */
		mutex_init(&overtemp_mutex, "Overtemp Mutex", MUTEX_DEFAULT,
			DEFAULT_WT);

		cv_init(&overtemp_cv, "Overtemp CV", CV_DRIVER, NULL);

		/* start up the overtemp polling thread */
		if (thread_create(NULL, PAGESIZE,
		    (void (*)())sysctrl_overtemp_poll, 0, 0, &p0, TS_RUN, 60)
		    == NULL) {
			cmn_err(CE_WARN, "sysctrl: cannot start thread");
			mutex_destroy(&overtemp_mutex);
			cv_destroy(&overtemp_cv);
		} else {
			sysctrl_overtemp_thread_started++;
		}
	}

	return (DDI_SUCCESS);

bad:
	/* XXX I'm sure there is more cleanup needed here */
	ddi_soft_state_free(sysctrlp, instance);
	cmn_err(CE_WARN,
	    "sysctrl: Initialization failure. Some system level events,");
	cmn_err(CE_CONT,
	    "sysctrl: {AC Fail, Fan Failure, PS Failure} not detected.");
	return (DDI_FAILURE);
}

/* ARGSUSED */
static int
sysctrl_detach(dev_info_t *devi, ddi_detach_cmd_t cmd)
{
	switch (cmd) {
	case DDI_SUSPEND:
		/* Dont know how to handle this case, so fail it. */
		return (DDI_FAILURE);

	case DDI_DETACH:
	default:
		return (DDI_FAILURE);
	}
}

/*
 * system_high_handler()
 * This routine handles system interrupts.
 *
 * This routine goes through all the interrupt sources and masks
 * off the enable bit if interrupting.  Because of the special
 * nature of the pps fan source bits, we also cache the state
 * of the fan bits for that special case.
 *
 * The rest of the work is done in the low level handlers
 */
static u_int
system_high_handler(caddr_t arg)
{
	struct sysctrl_soft_state *softsp = (struct sysctrl_soft_state *) arg;
	u_char csr;
	u_char status2;
	u_char tmp_reg;
	int serviced = 0;

	ASSERT(softsp);

	mutex_enter(&softsp->csr_mutex);

	/* read in the hardware registers */
	csr = *(softsp->csr);
	status2 = *(softsp->status2);

	if (csr & SYS_AC_PWR_FAIL_EN) {
		if (status2 & SYS_AC_FAIL) {

			/* save the powerfail state in nvram */
			if (softsp->nvram_offset_powerfail)
				nvram_update_powerfail(softsp, hrestime.tv_sec);

			/* disable this interrupt source */
			csr &= ~SYS_AC_PWR_FAIL_EN;

			ddi_trigger_softintr(softsp->ac_fail_id);
			serviced++;
		}
	}

	if (csr & SYS_PS_FAIL_EN) {
		if ((*(softsp->ps_stat) != 0xff) ||
		    ((~status2) & (SYS_PPS_DC_OK | SYS_CLK_DCREG0_OK |
		    SYS_CLK_DCREG1_OK))) {

			/* disable this interrupt source */
			csr &= ~SYS_PS_FAIL_EN;

			ddi_trigger_softintr(softsp->ps_fail_int_id);
			serviced++;
		}
	}

	if (csr & SYS_PPS_FAN_FAIL_EN) {
		if (status2 & SYS_RACK_FANFAIL ||
		    !(status2 & SYS_AC_FAN_OK) ||
		    !(status2 & SYS_KEYSW_FAN_OK)) {

			/*
			 * we must cache the fan status because it goes
			 * away when we disable interrupts !?!?!
			 */
			softsp->pps_fan_saved = status2;

			/* disable this interrupt source */
			csr &= ~SYS_PPS_FAN_FAIL_EN;

			ddi_trigger_softintr(softsp->pps_fan_id);
			serviced++;
		}
	}

	if (csr & SYS_SBRD_PRES_EN) {
		if (!(*(softsp->status1) & SYS_NOT_BRD_PRES)) {

			/* disable this interrupt source */
			csr &= ~SYS_SBRD_PRES_EN;

			ddi_trigger_softintr(softsp->sbrd_pres_id);
			serviced++;
		}
	}

	if (!serviced) {

		/*
		 * if we get here than it is likely that contact bounce
		 * is messing with us.  so, we need to shut this interrupt
		 * up for a while to let the contacts settle down.
		 * Then we will re-enable the interrupts that are enabled
		 * right now.  The trick is to disable the appropriate
		 * interrupts and then to re-enable them correctly, even
		 * though intervening handlers might have been working.
		 */

		/* remember all interrupts that could have caused it */
		softsp->saved_en_state |= csr &
		    (SYS_AC_PWR_FAIL_EN | SYS_PS_FAIL_EN |
		    SYS_PPS_FAN_FAIL_EN | SYS_SBRD_PRES_EN);

		/* and then turn them off */
		csr &= ~(SYS_AC_PWR_FAIL_EN | SYS_PS_FAIL_EN |
			SYS_PPS_FAN_FAIL_EN | SYS_SBRD_PRES_EN);

		/* and then bump the counter */
		softsp->spur_count++;

		/* and kick off the timeout */
		ddi_trigger_softintr(softsp->spur_id);
	}

	/* update the real csr */
	*(softsp->csr) = csr;
	tmp_reg = *(softsp->csr);
#ifdef lint
	tmp_reg = tmp_reg;
#endif
	mutex_exit(&softsp->csr_mutex);

	return (DDI_INTR_CLAIMED);
}

/*
 * we've detected a spurious interrupt.
 * determine if we should log a message and if we need another timeout
 */
static u_int
spur_delay(caddr_t arg)
{
	struct sysctrl_soft_state *softsp = (struct sysctrl_soft_state *) arg;

	ASSERT(softsp);

	/* do we need to complain? */
	mutex_enter(&softsp->csr_mutex);

	/* NOTE: this is == because we want one message per long timeout */
	if (softsp->spur_count == MAX_SPUR_COUNT) {
		char buf[128];

		/* print out the candidates known at this time */
		/* XXX not perfect because of re-entrant nature but close */
		buf[0] = '\0';
		if (softsp->saved_en_state & SYS_AC_PWR_FAIL_EN)
			strcat(buf, "AC FAIL");
		if (softsp->saved_en_state & SYS_PPS_FAN_FAIL_EN)
			strcat(buf, buf[0] ? "|PPS FANS" : "PPS FANS");
		if (softsp->saved_en_state & SYS_PS_FAIL_EN)
			strcat(buf, buf[0] ? "|PS FAIL" : "PS FAIL");
		if (softsp->saved_en_state & SYS_SBRD_PRES_EN)
			strcat(buf, buf[0] ? "|BOARD INSERT" : "BOARD INSERT");

		cmn_err(CE_WARN, "sysctrl: unserviced interrupt."
				" possible sources [%s].", buf);
	}
	mutex_exit(&softsp->csr_mutex);

	mutex_enter(&softsp->spur_int_lock);

	/* do we need to start the short timeout? */
	if (softsp->spur_timeout_id == 0) {
		softsp->spur_timeout_id = timeout(spur_retry,
			(caddr_t) softsp, spur_timeout_hz);
	}

	/* do we need to start the long timeout? */
	if (softsp->spur_long_timeout_id == 0) {
		softsp->spur_long_timeout_id = timeout(spur_long_timeout,
			(caddr_t) softsp, spur_long_timeout_hz);
	}

	mutex_exit(&softsp->spur_int_lock);

	return (DDI_INTR_CLAIMED);
}

/*
 * spur_retry
 *
 * this routine simply triggers the interrupt which will re-enable
 * the interrupts disabled by the spurious int detection.
 */
static void
spur_retry(caddr_t arg)
{
	struct sysctrl_soft_state *softsp = (struct sysctrl_soft_state *) arg;

	ASSERT(softsp);

	ddi_trigger_softintr(softsp->spur_high_id);

	mutex_enter(&softsp->spur_int_lock);
	softsp->spur_timeout_id = 0;
	mutex_exit(&softsp->spur_int_lock);
}

/*
 * spur_reenable
 *
 * OK, we've been slient for a while.   Go ahead and re-enable the
 * interrupts that were enabled at the time of the spurious detection.
 */
static u_int
spur_reenable(caddr_t arg)
{
	struct sysctrl_soft_state *softsp = (struct sysctrl_soft_state *) arg;
	u_char tmp_reg;

	ASSERT(softsp);

	mutex_enter(&softsp->csr_mutex);

	/* reenable those who were spurious candidates */
	*(softsp->csr) |= softsp->saved_en_state &
		(SYS_AC_PWR_FAIL_EN | SYS_PS_FAIL_EN |
		SYS_PPS_FAN_FAIL_EN | SYS_SBRD_PRES_EN);
	tmp_reg = *(softsp->csr);
#ifdef lint
	tmp_reg = tmp_reg;
#endif

	/* clear out the saved state */
	softsp->saved_en_state = 0;

	mutex_exit(&softsp->csr_mutex);

	return (DDI_INTR_CLAIMED);
}

/*
 * spur_long_timeout
 *
 * this routine merely resets the spurious interrupt counter thus ending
 * the interval of interest.  of course this is done by triggering a
 * softint because the counter is protected by an interrupt mutex.
 */
static void
spur_long_timeout(caddr_t arg)
{
	struct sysctrl_soft_state *softsp = (struct sysctrl_soft_state *) arg;

	ASSERT(softsp);

	ddi_trigger_softintr(softsp->spur_long_to_id);

	mutex_enter(&softsp->spur_int_lock);
	softsp->spur_long_timeout_id = 0;
	mutex_exit(&softsp->spur_int_lock);
}

/*
 * spur_clear_count
 *
 * simply clear out the spurious interrupt counter.
 *
 * softint level only
 */
static u_int
spur_clear_count(caddr_t arg)
{
	struct sysctrl_soft_state *softsp = (struct sysctrl_soft_state *) arg;

	ASSERT(softsp);

	mutex_enter(&softsp->csr_mutex);
	softsp->spur_count = 0;
	mutex_exit(&softsp->csr_mutex);

	return (DDI_INTR_CLAIMED);
}

/*
 * ac_fail_handler
 *
 * This routine polls the AC power failure bit in the system status2
 * register.  If we get to this routine, then we sensed an ac fail
 * condition.  Note the fact and check again in a few.
 *
 * Called as softint from high interrupt.
 */
static u_int
ac_fail_handler(caddr_t arg)
{
	struct sysctrl_soft_state *softsp = (struct sysctrl_soft_state *) arg;

	ASSERT(softsp);

	cmn_err(CE_WARN, "AC Power failure detected");
	(void) timeout(ac_fail_retry, (caddr_t) softsp, ac_timeout_hz);

	return (DDI_INTR_CLAIMED);
}

/*
 * The timeout from ac_fail_handler() that checks to see if the
 * condition persists.
 */
static void
ac_fail_retry(caddr_t arg)
{
	struct sysctrl_soft_state *softsp = (struct sysctrl_soft_state *) arg;

	ASSERT(softsp);

	if (*softsp->status2 & SYS_AC_FAIL) {	/* still bad? */
		(void) timeout(ac_fail_retry, (caddr_t) softsp, ac_timeout_hz);
	} else {
		cmn_err(CE_NOTE, "AC Power failure no longer detected");
		ddi_trigger_softintr(softsp->ac_fail_high_id);
	}
}

/*
 * The interrupt routine that we use to re-enable the interrupt.
 * Called from ddi_trigger_softint() in the ac_fail_retry() when
 * the AC is better.
 */
static u_int
ac_fail_reenable(caddr_t arg)
{
	struct sysctrl_soft_state *softsp = (struct sysctrl_soft_state *) arg;
	u_char tmp_reg;

	ASSERT(softsp);

	mutex_enter(&softsp->csr_mutex);
	*(softsp->csr) |= SYS_AC_PWR_FAIL_EN;
	tmp_reg = *(softsp->csr);
#ifdef lint
	tmp_reg = tmp_reg;
#endif
	mutex_exit(&softsp->csr_mutex);

	return (DDI_INTR_CLAIMED);
}

/*
 * ps_fail_int_handler
 *
 * Handle power supply failure interrupt.
 *
 * This wrapper is called as softint from hardware interrupt routine.
 */
static u_int
ps_fail_int_handler(caddr_t arg)
{
	return (ps_fail_handler((struct sysctrl_soft_state *)arg, 1));
}

/*
 * ps_fail_poll_handler
 *
 * Handle power supply failure interrupt.
 *
 * This wrapper is called as softint from power supply poll routine.
 */
static u_int
ps_fail_poll_handler(caddr_t arg)
{
	return (ps_fail_handler((struct sysctrl_soft_state *)arg, 0));
}

/*
 * ps_fail_handler
 *
 * This routine checks all eight of the board power supplies that are
 * installed plus the Peripheral power supply and the two DC OK. Since the
 * hardware bits are not enough to indicate Power Supply failure
 * vs. being turned off via software, the driver must maintain a
 * shadow state for the Power Supply status and monitor all changes.
 *
 * Called as a softint only.
 */
static u_int
ps_fail_handler(struct sysctrl_soft_state *softsp, int fromint)
{
	int i;
	struct ps_state *pstatp;
	int poll_needed = 0;
	u_char ps_stat, ps_pres, status1, status2;
	u_char tmp_reg;

	ASSERT(softsp);

	/* pre-read the hardware state */
	ps_stat = *softsp->ps_stat;
	ps_pres = *softsp->ps_pres;
	status1 = *softsp->status1;
	status2 = *softsp->status2;

	mutex_enter(&softsp->ps_fail_lock);

	for (i = 0, pstatp = &softsp->ps_stats[0]; i < PS_COUNT;
	    i++, pstatp++) {
		int	temp_psok;
		int	temp_pres;

		/*
		 * pre-compute the presence and ok bits for this
		 * power supply from the hardware registers.
		 */
		switch (i) {
		/* the core power supplies */
		case 0: case 1: case 2: case 3:
		case 4: case 5: case 6: case 7:
			temp_pres = !((ps_pres >> i) & 0x1);
			temp_psok = (ps_stat >> i) & 0x1;
			break;

		/* the peripheral power supply */
		case PPS_INDEX:
			temp_pres = !(status1 & SYS_NOT_PPS_PRES);
			temp_psok = status2 & SYS_PPS_DC_OK;
			break;

		/* shared clock power (this one is from PS1!) */
		case DC0_INDEX:
			temp_pres = !(ps_pres & 0x2);
			temp_psok = status2 & SYS_CLK_DCREG0_OK;
			break;

		/* shared clock power (this one is from pps) */
		case DC1_INDEX:
			temp_pres = !(status1 & SYS_NOT_PPS_PRES);
			temp_psok = status2 & SYS_CLK_DCREG1_OK;
			break;
		}

		/* *** Phase 1 -- power supply presence tests *** */

		/* do we know the presence status for this power supply? */
		if (pstatp->pshadow == PRES_UNKNOWN) {
			pstatp->pshadow = temp_pres ? PRES_IN : PRES_OUT;
			pstatp->dcshadow = temp_pres ? PS_BOOT : PS_OUT;
		} else {
			/* has the ps presence state changed? */
			if (!temp_pres ^ (pstatp->pshadow == PRES_IN)) {
				pstatp->pctr = 0;
			} else {
				/* a change! are we counting? */
				if (pstatp->pctr == 0) {
					pstatp->pctr = PS_PRES_CHANGE_TICKS;
				} else if (--pstatp->pctr == 0) {
					pstatp->pshadow = temp_pres ?
						PRES_IN : PRES_OUT;
					pstatp->dcshadow = temp_pres ?
						PS_UNKNOWN : PS_OUT;

					/*
					 * Now we know the state has
					 * changed, so we should log it.
					 */
					ps_log_pres_change(i, temp_pres);
				}
			}
		}

		/* *** Phase 2 -- power supply status tests *** */

		/* check if the Power Supply is removed or same as before */
		if ((pstatp->dcshadow == PS_OUT) ||
		    ((pstatp->dcshadow == PS_OK) && temp_psok) ||
		    ((pstatp->dcshadow == PS_FAIL) && !temp_psok)) {
			pstatp->dcctr = 0;
		} else {

			/* OK, a change, do we start the timer? */
			if (pstatp->dcctr == 0) {
				switch (pstatp->dcshadow) {
				case PS_BOOT:
					pstatp->dcctr = PS_FROM_BOOT_TICKS;
					break;

				case PS_UNKNOWN:
					pstatp->dcctr = PS_FROM_UNKNOWN_TICKS;
					break;

				case PS_OK:
					pstatp->dcctr = PS_FROM_OK_TICKS;
					break;

				case PS_FAIL:
					pstatp->dcctr = PS_FROM_FAIL_TICKS;
					break;

				default:
					cmn_err(CE_PANIC,
						"sysctrl: Unknown Power "
						"Supply State %d",
						pstatp->dcshadow);
				}
			}

			/* has the ticker expired? */
			if (--pstatp->dcctr == 0) {

				/* we'll skip OK messages during boot */
				if (!((pstatp->dcshadow == PS_BOOT) &&
				    temp_psok)) {
					ps_log_state_change(i, temp_psok);
				}

				/* regardless, update the shadow state */
				pstatp->dcshadow = temp_psok ? PS_OK : PS_FAIL;
			}
		}

		/*
		 * We will need to continue polling for three reasons:
		 * - a failing power supply is detected and we haven't yet
		 *   determined the power supplies existence.
		 * - the power supply is just installed and we're waiting
		 *   to give it a change to power up,
		 * - a failed power supply state is recognized
		 *
		 * NOTE: PS_FAIL shadow state is not the same as !temp_psok
		 * because of the persistence of PS_FAIL->PS_OK.
		 */
		if (!temp_psok ||
		    (pstatp->dcshadow == PS_UNKNOWN) ||
		    (pstatp->dcshadow == PS_FAIL)) {
			poll_needed++;
		}
	}

	mutex_exit(&softsp->ps_fail_lock);

	/*
	 * If we don't have ps problems that need to be polled for, then
	 * enable interrupts.
	 */
	if (!poll_needed) {
		mutex_enter(&softsp->csr_mutex);
		*(softsp->csr) |= SYS_PS_FAIL_EN;
		tmp_reg = *(softsp->csr);
#ifdef lint
		tmp_reg = tmp_reg;
#endif
		mutex_exit(&softsp->csr_mutex);
	}

	/*
	 * Only the polling loop re-triggers the polling loop timeout
	 */
	if (!fromint) {
		(void) timeout(ps_fail_retry, (caddr_t) softsp,
			ps_fail_timeout_hz);
	}

	return (DDI_INTR_CLAIMED);
}

/*
 * log the change of power supply presence
 */
static void
ps_log_pres_change(int index, int present)
{
	switch (index) {
	/* the core power supplies */
	case 0: case 1: case 2: case 3:
	case 4: case 5: case 6: case 7:
		cmn_err(CE_NOTE, "Power Supply %d %s", index,
			present ? "Installed" : "Removed");
		break;

	/* the peripheral power supply */
	case PPS_INDEX:
		cmn_err(CE_NOTE, "Peripheral Power Supply %s",
			present ? "Installed" : "Removed");
		break;

	/* we don't mention a change of presence state for these */
	case DC0_INDEX:
	case DC1_INDEX:
		break;
	}
}

/*
 * log the change of power supply status
 */
static void
ps_log_state_change(int index, int status)
{
	int level = status ? CE_NOTE : CE_WARN;
	char *s = status ? "OK" : "Failing";

	switch (index) {
	/* the core power supplies */
	case 0: case 1: case 2: case 3:
	case 4: case 5: case 6: case 7:
		cmn_err(level, "Power Supply %d %s", index, s);
		break;

	/* the peripheral power supply */
	case PPS_INDEX:
		cmn_err(level, "Peripheral Power Supply %s", s);
		break;

	/* we don't mention a change of presence state for these */
	case DC0_INDEX:
		cmn_err(level, "Clock Board Regulator 0 (from PS 1) %s", s);
		break;

	case DC1_INDEX:
		cmn_err(level, "Clock Board Regulator 1 (from PPS) %s", s);
		break;
	}
}

/*
 * The timeout from ps_fail_handler() that simply re-triggers a check
 * of the ps condition.
 */
static void
ps_fail_retry(caddr_t arg)
{
	struct sysctrl_soft_state *softsp = (struct sysctrl_soft_state *) arg;

	ASSERT(softsp);

	ddi_trigger_softintr(softsp->ps_fail_poll_id);
}

/*
 * pps_fanfail_handler
 *
 * This routine is called from the high level handler.
 *
 * Regardless of the state of the fans as measured, we will always
 * delay the re-enabling of the fan timeout for a while to prevent
 * spinning on a bad signal.
 */
static u_int
pps_fanfail_handler(caddr_t arg)
{
	struct sysctrl_soft_state *softsp = (struct sysctrl_soft_state *) arg;
	u_char status2;

	ASSERT(softsp);

	/* what was the status as reported by system_high? */
	status2 = softsp->pps_fan_saved;

	mutex_enter(&softsp->pps_fan_lock);

	/*
	 * is the rack fan bad?
	 */
	if (status2 & SYS_RACK_FANFAIL) {
		start_pps_fan_timeout(softsp, RACK);

		/* is the fan still bad? */
		if (!(softsp->pps_fan_external_state & SYS_RACK_FANFAIL)) {
			cmn_err(CE_WARN,
				"Rack Exhaust Fan failure detected");
			softsp->pps_fan_external_state |= SYS_RACK_FANFAIL;
		}
	}

	/*
	 * is the ac box fan bad?
	 */
	if (!(status2 & SYS_AC_FAN_OK)) {
		start_pps_fan_timeout(softsp, AC);

		/* is the fan still bad? */
		if (softsp->pps_fan_external_state & SYS_AC_FAN_OK) {
			cmn_err(CE_WARN,
				"AC Box Fan failure detected");
			softsp->pps_fan_external_state &= ~SYS_AC_FAN_OK;
		}
	}

	/*
	 * is the keysw fan bad?
	 */
	if (!(status2 & SYS_KEYSW_FAN_OK)) {
		start_pps_fan_timeout(softsp, KEYSW);

		/* is the fan still bad? */
		if (softsp->pps_fan_external_state & SYS_KEYSW_FAN_OK) {
			cmn_err(CE_WARN,
				"Key Switch Fan failure detected");
			softsp->pps_fan_external_state &= ~SYS_KEYSW_FAN_OK;
		}
	}

	mutex_exit(&softsp->pps_fan_lock);

	/* always check again in a bit by re-enabling the fan interrupt */
	(void) timeout(pps_fanfail_retry, (caddr_t) softsp,
		pps_fan_timeout_hz);

	return (DDI_INTR_CLAIMED);
}

/*
 * after a bit of waiting, we simply re-enable the interrupt to
 * see if we get another one.  the softintr triggered routine does
 * the dirty work for us since it runs in the interrupt context.
 */
static void
pps_fanfail_retry(caddr_t arg)
{
	struct sysctrl_soft_state *softsp = (struct sysctrl_soft_state *) arg;

	ASSERT(softsp);

	ddi_trigger_softintr(softsp->pps_fan_high_id);
}

/*
 * The other half of the retry handler run from the interrupt context
 */
static u_int
pps_fanfail_reenable(caddr_t arg)
{
	struct sysctrl_soft_state *softsp = (struct sysctrl_soft_state *) arg;
	u_char tmp_reg;

	ASSERT(softsp);

	mutex_enter(&softsp->csr_mutex);
	*(softsp->csr) |= SYS_PPS_FAN_FAIL_EN;
	tmp_reg = *(softsp->csr);
#ifdef lint
	tmp_reg = tmp_reg;
#endif
	mutex_exit(&softsp->csr_mutex);

	return (DDI_INTR_CLAIMED);
}

/*
 * Start and stop pps OK timeout messages
 */
static void
start_pps_fan_timeout(struct sysctrl_soft_state *softsp,
			enum pps_fan_type type)
{
	ASSERT(mutex_owned(&softsp->pps_fan_lock));

	if (softsp->fan_arg[type].timeout_id != 0)
		(void) untimeout(softsp->fan_arg[type].timeout_id);
	softsp->fan_arg[type].timeout_id = timeout(pps_fan_ok,
		(caddr_t)&softsp->fan_arg[type], pps_fan_ok_timeout_hz);
}

/*
 * If we finally get this timeout, write out the message and let the
 * world know a fan is better.
 */
static void
pps_fan_ok(caddr_t arg)
{
	struct fan_ok_arg *fan_arg = (struct fan_ok_arg *) arg;
	struct sysctrl_soft_state *softsp;
	char *s, buf[128];

	ASSERT(fan_arg);

	softsp = fan_arg->self;

	ASSERT(softsp);

	mutex_enter(&softsp->pps_fan_lock);

	switch (fan_arg->type) {
	case RACK:
		s = "Rack Exhaust Fans OK";
		softsp->pps_fan_external_state &= ~SYS_RACK_FANFAIL;
		break;
	case AC:
		s = "AC Box Fans OK";
		softsp->pps_fan_external_state |= SYS_AC_FAN_OK;
		break;
	case KEYSW:
		s = "Key Switch Fan OK";
		softsp->pps_fan_external_state |= SYS_KEYSW_FAN_OK;
		break;
	default:
		sprintf(buf, "[Unknown Fan ID %d] Fan OK. softsp 0x%0x",
			fan_arg->type, (int)softsp);
		s = buf;
	}
	cmn_err(CE_NOTE, s);

	fan_arg->timeout_id = 0;

	mutex_exit(&softsp->pps_fan_lock);
}

static u_int
bd_insert_handler(caddr_t arg)
{
	struct sysctrl_soft_state *softsp = (struct sysctrl_soft_state *) arg;

	ASSERT(softsp);

	cmn_err(CE_NOTE, "Board Insert detected");

	/* XXX do something interesting here */
	/* XXX add deglitch here too */
	/* XXX perhaps the action is taken in the timeout routine */

	(void) timeout(bd_insert_timeout, (caddr_t) softsp,
		bd_insert_timeout_hz);

	return (DDI_INTR_CLAIMED);
}

static void
bd_insert_timeout(caddr_t arg)
{
	struct sysctrl_soft_state *softsp = (struct sysctrl_soft_state *) arg;

	ASSERT(softsp);

	ddi_trigger_softintr(softsp->sbrd_gone_id);
}

static u_int
bd_insert_normal(caddr_t arg)
{
	struct sysctrl_soft_state *softsp = (struct sysctrl_soft_state *) arg;
	u_char tmp_reg;

	ASSERT(softsp);

	/* has the condition been removed? */
	/* XXX add deglitch state machine here */
	if (!(*(softsp->status1) & SYS_NOT_BRD_PRES)) {
		/* check again in a few */
		(void) timeout(bd_insert_timeout, (caddr_t) softsp,
			bd_insert_timeout_hz);
	} else {
		cmn_err(CE_NOTE, "Board Insert no longer detected");

		/* Turn on the enable bit for this interrupt */
		mutex_enter(&softsp->csr_mutex);
		*(softsp->csr) |= SYS_SBRD_PRES_EN;
		/* flush the hardware store buffer */
		tmp_reg = *(softsp->csr);
#ifdef lint
		tmp_reg = tmp_reg;
#endif
		mutex_exit(&softsp->csr_mutex);
	}

	return (DDI_INTR_CLAIMED);
}

/*
 * blink LED handler.
 *
 * The actual bit manipulation needs to occur at interrupt level
 * because we need access to the CSR with its CSR mutex
 */
static u_int
blink_led_handler(caddr_t arg)
{
	struct sysctrl_soft_state *softsp = (struct sysctrl_soft_state *) arg;
	u_char tmp_reg;

	ASSERT(softsp);

	mutex_enter(&softsp->csr_mutex);
	*(softsp->csr) ^= SYS_LED_RIGHT;
	/* flush the hardware store buffer */
	tmp_reg = *(softsp->csr);
#ifdef lint
	tmp_reg = tmp_reg;
#endif
	mutex_exit(&softsp->csr_mutex);

	(void) timeout(blink_led_timeout, (caddr_t) softsp,
		blink_led_timeout_hz);

	return (DDI_INTR_CLAIMED);
}

/*
 * simply re-trigger the interrupt handler on led timeout
 */
static void
blink_led_timeout(caddr_t arg)
{
	struct sysctrl_soft_state *softsp = (struct sysctrl_soft_state *) arg;

	ASSERT(softsp);

	ddi_trigger_softintr(softsp->blink_led_id);
}

void
nvram_update_powerfail(struct sysctrl_soft_state *softsp, u_int pattern)
{
	u_char oldval[4];
	u_char newval[4];
	u_char old_checksum, new_checksum;
	int i;
	volatile u_char *nvram_powerfail;

	newval[3] = pattern & 0xFF;
	newval[2] = (pattern >> 8) & 0xFF;
	newval[1] = (pattern >> 16) & 0xFF;
	newval[0] = (pattern >> 24) & 0xFF;

	/*
	 * Set the base address of the powerfail array
	 */
	nvram_powerfail = softsp->nvram_base + softsp->nvram_offset_powerfail;

	/*
	 * get old value
	 */
	oldval[0] = *(nvram_powerfail);
	oldval[1] = *(nvram_powerfail + 1);
	oldval[2] = *(nvram_powerfail + 2);
	oldval[3] = *(nvram_powerfail + 3);

	/*
	 * get checksum
	 */
	old_checksum = *(softsp->nvram_base + OFF_NVRAM_CHECKSUM);

	new_checksum = old_checksum;

	/*
	 * if the byte value has changed, write the byte and update
	 * the checksum by removing old pattern and adding new.
	 */
	for (i = 0; i < 4; ++i)
		if (oldval[i] != newval[i]) {
			*(nvram_powerfail+ i) = newval[i];
			new_checksum = new_checksum ^ oldval[i] ^ newval[i];
		}

	/*
	 * write new checksum
	 */
	if (new_checksum != old_checksum) {
		*(softsp->nvram_base + OFF_NVRAM_CHECKSUM) = new_checksum;
	}
}


void
sysctrl_add_kstats(struct sysctrl_soft_state *softsp)
{
	struct kstat	*ksp;
	struct kstat	*tksp;
	struct sysctrl_kstat *sysksp;

	if ((ksp = kstat_create("unix", ddi_get_instance(softsp->dip),
	    SYSCTRL_KSTAT_NAME, "misc", KSTAT_TYPE_NAMED,
	    sizeof (struct sysctrl_kstat) / sizeof (kstat_named_t),
	    KSTAT_FLAG_PERSISTENT)) == NULL) {
		cmn_err(CE_WARN, "sysctrl%d kstat_create failed",
			ddi_get_instance(softsp->dip));
	}

	if ((tksp = kstat_create("unix", CLOCK_BOARD_INDEX,
	    OVERTEMP_KSTAT_NAME, "misc", KSTAT_TYPE_RAW,
	    sizeof (struct temp_stats), KSTAT_FLAG_PERSISTENT)) == NULL) {
		cmn_err(CE_WARN, "sysctrl kstat_create failed");
	}

	sysksp = (struct sysctrl_kstat *)(ksp->ks_data);

	/* now init the named kstats */
	kstat_named_init(&sysksp->csr, CSR_KSTAT_NAMED,
		KSTAT_DATA_CHAR);

	kstat_named_init(&sysksp->status1, STAT1_KSTAT_NAMED,
		KSTAT_DATA_CHAR);

	kstat_named_init(&sysksp->status2, STAT2_KSTAT_NAMED,
		KSTAT_DATA_CHAR);

	kstat_named_init(&sysksp->ps_shadow, PSSHAD_KSTAT_NAMED,
		KSTAT_DATA_CHAR);

	kstat_named_init(&sysksp->clk_freq2, CLK_FREQ2_KSTAT_NAMED,
		KSTAT_DATA_CHAR);

	kstat_named_init(&sysksp->fan_status, FAN_KSTAT_NAMED,
		KSTAT_DATA_CHAR);

	ksp->ks_update = sysctrl_kstat_update;
	ksp->ks_private = (void *)softsp;

	tksp->ks_update = overtemp_kstat_update;
	tksp->ks_private = (void *) &softsp->tempstat;

	kstat_install(ksp);
	kstat_install(tksp);
}

static int
sysctrl_kstat_update(kstat_t *ksp, int rw)
{
	struct sysctrl_kstat *sysksp;
	struct sysctrl_soft_state *softsp;
	int ps;

	sysksp = (struct sysctrl_kstat *)(ksp->ks_data);
	softsp = (struct sysctrl_soft_state *)(ksp->ks_private);

	/* this is a read-only kstat. Exit on a write */

	if (rw == KSTAT_WRITE) {
		return (EACCES);
	} else {
		/*
		 * copy the current state of the hardware into the
		 * kstat structure.
		 */
		sysksp->csr.value.c[0] = *(softsp->csr);
		sysksp->status1.value.c[0] = *(softsp->status1);
		sysksp->status2.value.c[0] = *(softsp->status2);
		sysksp->clk_freq2.value.c[0] = *(softsp->clk_freq2);

		for (ps = 0; ps < PS_COUNT; ps++) {
			sysksp->ps_shadow.value.c[ps] =
				softsp->ps_stats[ps].dcshadow;
		}
		sysksp->fan_status.value.c[0] = softsp->pps_fan_external_state;
	}
	return (0);
}

static void
overtemp_wakeup(void)
{
	/*
	 * grab mutex to guarantee that our wakeup call
	 * arrives after we go to sleep -- so we can't sleep forever.
	 */
	mutex_enter(&overtemp_mutex);
	cv_signal(&overtemp_cv);
	mutex_exit(&overtemp_mutex);
}

static void
sysctrl_overtemp_poll(void)
{
	struct sysctrl_soft_state *list;

	/* The overtemp data strcutures are protected by a mutex. */
	mutex_enter(&overtemp_mutex);

	while (sysctrl_do_overtemp_thread) {

		for (list = sys_list; list != NULL; list = list->next) {
			if (list->temp_reg == NULL) {
				continue;
			}

			update_temp(list->pdip, &list->tempstat,
				*(list->temp_reg));
		}

		/* now have this thread sleep for a while */
		(void) timeout(overtemp_wakeup, NULL, overtemp_timeout_hz);

		cv_wait(&overtemp_cv, &overtemp_mutex);

	}
	mutex_exit(&overtemp_mutex);

	thread_exit();
	/* NOTREACHED */
}

#define	TABLE_END	0xFF

struct uart_cmd {
	u_char reg;
	u_char data;
};

/*
 * Time constant defined by this formula:
 *	((4915200/32)/(baud) -2)
 */

struct uart_cmd uart_table[] = {
	{ 0x09, 0xc0 },	/* Force hardware reset */
	{ 0x04, 0x46 },	/* X16 clock mode, 1 stop bit/char, no parity */
	{ 0x03, 0xc0 },	/* Rx is 8 bits/char */
	{ 0x05, 0xe2 },	/* DTR, Tx is 8 bits/char, RTS */
	{ 0x09, 0x02 },	/* No vector returned on interrupt */
	{ 0x0b, 0x55 },	/* Rx Clock = Tx Clock = BR generator = ~TRxC OUT */
	{ 0x0c, 0x0e },	/* Time Constant = 0x000e for 9600 baud */
	{ 0x0d, 0x00 },	/* High byte of time constant */
	{ 0x0e, 0x02 },	/* BR generator comes from Z-SCC's PCLK input */
	{ 0x03, 0xc1 },	/* Rx is 8 bits/char, Rx is enabled */
	{ 0x05, 0xea },	/* DTR, Tx is 8 bits/char, Tx is enabled, RTS */
	{ 0x0e, 0x03 },	/* BR comes from PCLK, BR generator is enabled */
	{ 0x00, 0x30 },	/* Error reset */
	{ 0x00, 0x30 },	/* Error reset */
	{ 0x00, 0x10 },	/* external status reset */
	{ 0x03, 0xc1 },	/* Rx is 8 bits/char, Rx is enabled */
	{ TABLE_END, 0x0 }
};

static void
init_remote_console_uart(struct sysctrl_soft_state *softsp)
{
	int i = 0;

	/*
	 * Serial chip expects software to write to the control
	 * register first with the desired register number. Then
	 * write to the control register with the desired data.
	 * So walk thru table writing the register/data pairs to
	 * the serial port chip.
	 */
	while (uart_table[i].reg != TABLE_END) {
		*(softsp->rcons_ctl) = uart_table[i].reg;
		*(softsp->rcons_ctl) = uart_table[i].data;
		i++;
	}
}
