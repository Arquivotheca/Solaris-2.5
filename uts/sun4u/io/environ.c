/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)environ.c 1.13	95/06/08 SMI"

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
#include <sys/modctl.h>
#include <sys/proc.h>
#include <sys/fhc.h>
#include <sys/environ.h>

/* Useful debugging Stuff */
#include <sys/nexusdebug.h>

/*
 * Function prototypes
 */
static int environ_identify(dev_info_t *devi);

static int environ_attach(dev_info_t *devi, ddi_attach_cmd_t cmd);

static int environ_detach(dev_info_t *devi, ddi_detach_cmd_t cmd);

static int environ_init(struct environ_soft_state *softsp);

static int fanfail_init(struct environ_soft_state *softsp);

static u_int local_fanfail_handler(caddr_t arg);

static u_int fan_fail_softintr(caddr_t arg);

static void fan_retry_timeout(caddr_t arg);
static u_int fan_retry_softintr(caddr_t arg);

void environ_add_fan_kstats(struct environ_soft_state *softsp);
void environ_add_temp_kstats(struct environ_soft_state *softsp);

static void fan_ok_timeout(caddr_t arg);

static int environ_kstat_update(kstat_t *ksp, int rw);

static void overtemp_wakeup(void);

static void environ_overtemp_poll(void);

/*
 * Configuration data structures
 */
static struct cb_ops environ_cb_ops = {
	nulldev,		/* open */
	nulldev,		/* close */
	nulldev,		/* strategy */
	nulldev,		/* print */
	nodev,			/* dump */
	nulldev,		/* read */
	nulldev,		/* write */
	nulldev,		/* ioctl */
	nodev,			/* devmap */
	nodev,			/* mmap */
	nodev,			/* segmap */
	nochpoll,		/* poll */
	ddi_prop_op,		/* cb_prop_op */
	0,			/* streamtab */
	D_MP | D_NEW		/* Driver compatibility flag */
};

static struct dev_ops environ_ops = {
	DEVO_REV,		/* devo_rev, */
	0,			/* refcnt */
	ddi_no_info,		/* getinfo */
	environ_identify,	/* identify */
	nulldev,		/* probe */
	environ_attach,		/* attach */
	environ_detach,		/* detach */
	nulldev,		/* reset */
	&environ_cb_ops,	/* cb_ops */
	(struct bus_ops *)0,	/* bus_ops */
};

void *environp;			/* environ soft state hook */

/*
 * Mutex used to protect the soft state list and their data.
 */
static kmutex_t overtemp_mutex;

/* The CV is used to wakeup the thread when needed. */
static kcondvar_t overtemp_cv;

/* linked list of all environ soft states */
struct environ_soft_state *tempsp_list = NULL;

/* overtemp polling routine timeout delay */
static int overtemp_timeout_hz = OVERTEMP_TIMEOUT_SEC * HZ;

/* Fanfail reenable interrupt timeout delay */
static int fanfail_timeout_hz  = FANFAIL_TIMEOUT_SEC * HZ;

/* Fanfail OK timeout delay */
static int fanfail_ok_timeout_hz  = FANFAIL_OK_TIMEOUT_SEC * HZ;

/* Should we monitor fanfail interrupts? */
static int enable_fanfail_interrupt = 1;

/* Should the environ_overtemp_poll thread be running? */
static int environ_do_overtemp_thread = 1;

/* Indicates whether or not the overtemp thread has been started */
static int environ_overtemp_thread_started = 0;

extern struct mod_ops mod_driverops;

static struct modldrv modldrv = {
	&mod_driverops,		/* Type of module.  This one is a driver */
	"Environment Leaf",	/* name of module */
	&environ_ops,		/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1,
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

	if ((error = ddi_soft_state_init(&environp,
	    sizeof (struct environ_soft_state), 1)) != 0)
		return (error);

	return (mod_install(&modlinkage));
}

int
_fini(void)
{
	int error;

	if ((error = mod_remove(&modlinkage)) != 0)
		return (error);

	ddi_soft_state_fini(&environp);
	return (0);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

static int
environ_identify(dev_info_t *devi)
{
	char *name = ddi_get_name(devi);
	int rc = DDI_NOT_IDENTIFIED;

	if ((strcmp(name, "environment") == 0) ||
	    (strcmp(name, "environ") == 0)) {
		rc = DDI_IDENTIFIED;
	}

	return (rc);
}

static int
environ_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	struct environ_soft_state *softsp;
	int instance, error;

	switch (cmd) {
	case DDI_ATTACH:
		break;

	case DDI_RESUME:
		return (DDI_SUCCESS);

	default:
		return (DDI_FAILURE);
	}

	instance = ddi_get_instance(devi);

	if (ddi_soft_state_zalloc(environp, instance) != DDI_SUCCESS)
		return (DDI_FAILURE);

	softsp = ddi_get_soft_state(environp, instance);

	/* Set the dip in the soft state */
	softsp->dip = devi;

	/*
	 * The DDI documentation on ddi_getprop() routine says that
	 * you should always use the real dev_t when calling it,
	 * but all calls found in uts use either DDI_DEV_T_ANY
	 * or DDI_DEV_T_NONE. No notes either on how to find the real
	 * dev_t. So we are doing it in two steps.
	 */
	softsp->pdip = ddi_get_parent(softsp->dip);

	if ((softsp->board = (int) ddi_getprop(DDI_DEV_T_ANY, softsp->pdip,
	    DDI_PROP_DONTPASS, "board#", -1)) == -1) {
		cmn_err(CE_WARN, "Unable to retrieve environ board#"
			"property.");
		error = DDI_FAILURE;
		goto bad;
	}

	DPRINTF(ENVIRON_ATTACH_DEBUG, ("environ: devi= 0x%x\n, softsp=0x%x,",
		(int) devi, (int) softsp));

	/*
	 * Init the temperature device here. We start the overtemp
	 * polling thread here.
	 */
	if ((error = environ_init(softsp)) != DDI_SUCCESS) {
		goto bad;
	}

	if (enable_fanfail_interrupt) {
		if ((error = fanfail_init(softsp)) != DDI_SUCCESS) {
			goto bad;
		}
	}

	ddi_report_dev(devi);

	if (environ_overtemp_thread_started == 0) {

		/*
		 * set up the overtemp mutex and condition variable before
		 * starting the thread.
		 */
		mutex_init(&overtemp_mutex, "Overtemp Mutex", MUTEX_DEFAULT,
			DEFAULT_WT);

		cv_init(&overtemp_cv, "Overtemp CV", CV_DRIVER, NULL);

		/* Start the overtemp polling thread now. */
		if (thread_create(NULL, PAGESIZE,
		    (void (*)())environ_overtemp_poll, 0, 0, &p0, TS_RUN, 60)
		    == NULL) {
			cmn_err(CE_WARN, "environ: cannot start thread");
			mutex_destroy(&overtemp_mutex);
			cv_destroy(&overtemp_cv);
		} else {
			environ_overtemp_thread_started++;
		}
	}

	return (DDI_SUCCESS);

bad:
	ddi_soft_state_free(environp, instance);
	return (error);
}

/* ARGSUSED */
static int
environ_detach(dev_info_t *devi, ddi_detach_cmd_t cmd)
{
	switch (cmd) {
	case DDI_SUSPEND:
		/* Dont know how to handle this case, so fail it. */
		return (DDI_FAILURE);

	case DDI_DETACH:
		/*
		 * XXX think about what it means to remove the interrupt
		 * in light of the existing timeout mechanisms
		 */
	default:
		return (DDI_FAILURE);
	}
}

static int
environ_init(struct environ_soft_state *softsp)
{
	/*
	 * Initialize the fan_state to OK.
	 */
	softsp->fan_state = FAN_OK;

	environ_add_fan_kstats(softsp);

	/*
	 * If this environment node is on a CPU-less system board, i.e.,
	 * board type MEM_TYPE, then we do not want to map in, read
	 * the temperature register, create the polling entry for
	 * the overtemp polling thread, or create a kstat entry.
	 *
	 * The reason for this is that when no CPU modules are present
	 * on a CPU/Memory board, then the thermistors are not present,
	 * and the output of the A/D convertor is the max 8 bit value (0xFF)
	 */
	if (get_board_type(softsp->board) == MEM_BOARD) {
		return (DDI_SUCCESS);
	}

	/*
	 * Map in the temperature register. Once the temperature register
	 * is mapped, the timeout thread can read the temperature and
	 * update the temperature in the softsp.
	 */
	if (ddi_map_regs(softsp->dip, 0,
	    (caddr_t *)&softsp->temp_reg, 0, 0)) {
		cmn_err(CE_CONT, "environ%d: unable to map Temperature "
			"register\n", ddi_get_instance(softsp->dip));
		return (DDI_FAILURE);
	}

	/* Initialize the temperature */
	init_temp_arrays(&softsp->tempstat);

	/*
	 * Now add this soft state structure to the front of the linked list
	 * of soft state structures.
	 */
	mutex_enter(&overtemp_mutex);
	softsp->next = tempsp_list;
	tempsp_list = softsp;
	mutex_exit(&overtemp_mutex);

	/* Create kstats for this instance of the environ driver */
	environ_add_temp_kstats(softsp);

	return (DDI_SUCCESS);
}

/*
 *
 */
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

/*
 * This function polls all the system board digital temperature registers
 * and stores them in the history buffers using the fhc driver support
 * routines.
 * The temperature detected must then be checked against our current
 * policy for what to do in the case of overtemperature situations. We
 * must also allow for manufacturing's use of a heat chamber.
 */
static void
environ_overtemp_poll(void)
{
	struct environ_soft_state *list;

	/* The overtemp data strcutures are protected by a mutex. */
	mutex_enter(&overtemp_mutex);

	while (environ_do_overtemp_thread) {

		/*
		 * for each environment node that has been attached,
		 * read it and check for overtemp.
		 */
		for (list = tempsp_list; list != NULL; list = list->next) {
			if (list->temp_reg == NULL) {
				continue;
			}

			update_temp(list->pdip, &list->tempstat,
				*(list->temp_reg));

			/*
			 * Check the temperature limits for this type
			 * of system board.
			 */
		}

		/* now have this thread sleep for a while */
		(void) timeout(overtemp_wakeup, NULL, overtemp_timeout_hz);

		cv_wait(&overtemp_cv, &overtemp_mutex);

	}
	mutex_exit(&overtemp_mutex);

	thread_exit();
	/* NOTREACHED */
}

/*
 * fanfail_init
 *
 * activate the fanfail mechanism
 *
 * XXX cleanup the error return mechanisms
 * XXX make a destructor for this operation
 * XXX allow this routine to be called after attach for the
 *     case where the _other_ board is detaching and we should start
 *     monitoring the fans.
 */
static int
fanfail_init(struct environ_soft_state *softsp)
{
	char namebuf[128];
	struct bd_list *oboard, *board;

	/* Is the other instance already monitoring this power supply? */
	if ((oboard = get_and_lock_bdlist(OTHER_BD_FOR_PS(softsp->board))) !=
	    NULL) {
		if (oboard->monitoring_fan) {
			unlock_bdlist();
			return (DDI_SUCCESS);
		}
	}

	/*
	 * first, prepare all the soft interrupt handlers
	 */
	if (ddi_add_softintr(softsp->dip, DDI_SOFTINT_HIGH,
	    &softsp->fan_fail_id, &softsp->fan_fail_c, NULL, fan_fail_softintr,
	    (caddr_t) softsp) != DDI_SUCCESS)
		goto bad;

	(void) sprintf(namebuf, "environ fan softint mutex softsp 0x%0x",
			(int)softsp);
	mutex_init(&softsp->fan_lock, namebuf, MUTEX_DRIVER,
		(void *)softsp->fan_fail_c);

	if (ddi_add_softintr(softsp->dip, DDI_SOFTINT_LOW,
	    &softsp->fan_retry_id, NULL, NULL, fan_retry_softintr,
	    (caddr_t) softsp) != DDI_SUCCESS)
		goto bad;

	/*
	 * XXX this should be protected by fhc_func_lock but that is
	 * tricky (and this disable is required for the case of already
	 * failed fan -- infinite interrupt loop).
	 */
	fhc_control_fan_intr(softsp->pdip, FHC_FAN_INTR_OFF);

	if (ddi_add_intr(softsp->dip, 0, &softsp->fan_high_c, NULL,
	    (u_int (*)(caddr_t))nulldev, NULL) != DDI_SUCCESS)
		goto bad;

	(void) sprintf(namebuf, "environ fan mutex softsp 0x%0x", (int)softsp);
	mutex_init(&softsp->fhc_func_lock, namebuf, MUTEX_DRIVER,
		(void *)softsp->fan_high_c);

	ddi_remove_intr(softsp->dip, 0, &softsp->fan_high_c);

	if (ddi_add_intr(softsp->dip, 0, NULL, NULL, local_fanfail_handler,
	    (caddr_t) softsp) != DDI_SUCCESS)
		goto bad;

	/*
	 * We've made it through initialization, note that we own this PS
	 */
	board = get_bdlist(softsp->board);
	ASSERT(board != NULL);
	board->monitoring_fan = 1;
	unlock_bdlist();

	/*
	 * ready to go, enable the fan interrupt
	 */
	mutex_enter(&softsp->fhc_func_lock);
	fhc_control_fan_intr(softsp->pdip, FHC_FAN_INTR_ON);
	mutex_exit(&softsp->fhc_func_lock);

	return (DDI_SUCCESS);

bad:
	unlock_bdlist();
	return (DDI_FAILURE);
}

/*
 * local_fanfail_handler
 *
 * This routine gets registered as the DDI handler for fan fail interrupts.
 * The hardware does not have any mask bits with which to control the
 * enabling of the interrupt.   So we need a prearranged hook into fhc.
 *
 * high level interrupt handler
 *
 */
static u_int
local_fanfail_handler(caddr_t arg)
{
	struct environ_soft_state *softsp = (struct environ_soft_state *)arg;

	ASSERT(softsp);

	/*
	 * let's call into our parent to have this interrupt disabled
	 * NOTE: the contention for this is managed in the parent since
	 * more than one child might be accessing it
	 */
	mutex_enter(&softsp->fhc_func_lock);
	fhc_control_fan_intr(softsp->pdip, FHC_FAN_INTR_OFF);
	mutex_exit(&softsp->fhc_func_lock);

	/* now, complain about the fan */
	ddi_trigger_softintr(softsp->fan_fail_id);

	return (DDI_INTR_CLAIMED);
}

/*
 * fan_fail_softintr
 *
 * We just got called from the high level interrupt.  Complain
 * and start a retry timeout
 *
 * softintr handler
 */
static u_int
fan_fail_softintr(caddr_t arg)
{
	struct environ_soft_state *softsp = (struct environ_soft_state *)arg;

	ASSERT(softsp);

	mutex_enter(&softsp->fan_lock);

	/* do we need to complain? */
	if (softsp->fan_state == FAN_OK) {
		cmn_err(CE_WARN, "Fan Failure on Power Supply %d detected.",
			BD_2_PS(softsp->board));
	}
	softsp->fan_state = FAN_FAIL;

	/* cancel any previous ok timeout in progress */
	if (softsp->ok_id != 0) {
		(void) untimeout(softsp->ok_id);
	}

	/* kick off the ok timeout */
	softsp->ok_id = timeout(fan_ok_timeout, (caddr_t) softsp,
				fanfail_ok_timeout_hz);

	mutex_exit(&softsp->fan_lock);

	/* retry in a few */
	(void) timeout(fan_retry_timeout, (caddr_t) softsp,
				fanfail_timeout_hz);

	return (DDI_INTR_CLAIMED);
}

/*
 * fan_retry_timeout
 *
 * This routine is started on the callout queue after a fanfail interrupt
 * occurs. It's purpose is to re-enable the fanfail interrupts and set a
 * timeout for fanfail cleanup. If the interrupt occurs first, we go back
 * to the FAN_FAIL state. But if the fan_cleanup timeout occurs before
 * another interrupt, we say that the fan has been fixed.
 *
 * called as timeout handler
 */
static void
fan_retry_timeout(caddr_t arg)
{
	struct environ_soft_state *softsp = (struct environ_soft_state *) arg;

	ASSERT(softsp);

	ddi_trigger_softintr(softsp->fan_retry_id);
}

/*
 * fan_retry_softintr
 *
 * The second have of fan_retry_timeout.
 *
 * called as softintr handler
 */
static u_int
fan_retry_softintr(caddr_t arg)
{
	struct environ_soft_state *softsp = (struct environ_soft_state *) arg;

	ASSERT(softsp);

	/*
	 * turn the fan interrupt back on.  if it is still failing,
	 * we'll get a new interrupt right away
	 */
	mutex_enter(&softsp->fhc_func_lock);
	fhc_control_fan_intr(softsp->pdip, FHC_FAN_INTR_ON);
	mutex_exit(&softsp->fhc_func_lock);

	return (DDI_INTR_CLAIMED);
}

/*
 * fan_ok_timeout
 *
 * If we get this far, we've re-enabled interrupts and haven't heard
 * from the fan in a while.  It must be ok, so let's say so.
 */
static void
fan_ok_timeout(caddr_t arg)
{
	struct environ_soft_state *softsp = (struct environ_soft_state *) arg;

	ASSERT(softsp);

	mutex_enter(&softsp->fan_lock);

	cmn_err(CE_NOTE, "Fans on Power Supply %d OK.",
		BD_2_PS(softsp->board));

	softsp->fan_state = FAN_OK;

	softsp->ok_id = 0;

	mutex_exit(&softsp->fan_lock);
}

void
environ_add_fan_kstats(struct environ_soft_state *softsp)
{
	struct  kstat   *ksp;
	struct environkstat *envksp;

	/*
	 * Create the fan kstat required for the environment
	 * driver.
	 * the kstat instances are tagged with the physical board number
	 * instead of ddi instance number.
	 */
	if ((ksp = kstat_create("unix", softsp->board,
	    ENVIRON_KSTAT_NAME, "misc", KSTAT_TYPE_NAMED,
	    sizeof (struct environkstat) / sizeof (kstat_named_t),
	    KSTAT_FLAG_PERSISTENT)) == NULL) {
		cmn_err(CE_WARN, "environ%d kstat_create failed",
			(int) softsp->dip);
		return;
	}


	envksp = (struct environkstat *)(ksp->ks_data);

	/* now init the named kstats */
	kstat_named_init(&envksp->fanstat, FANSTATUS_KSTAT_NAMED,
		KSTAT_DATA_ULONG);


	ksp->ks_update = environ_kstat_update;
	ksp->ks_private = (void *)softsp;

	kstat_install(ksp);
}

void
environ_add_temp_kstats(struct environ_soft_state *softsp)
{
	struct  kstat   *tksp;

	/*
	 * Create the fan kstat required for the environment
	 * driver.
	 * the kstat instances are tagged with the physical board number
	 * instead of ddi instance number.
	 */
	if ((tksp = kstat_create("unix", softsp->board,
	    OVERTEMP_KSTAT_NAME, "misc", KSTAT_TYPE_RAW,
	    sizeof (struct temp_stats), KSTAT_FLAG_PERSISTENT)) == NULL) {
		cmn_err(CE_WARN, "environ%d kstat_create failed",
			softsp->board);
		return;
	}

	tksp->ks_update = overtemp_kstat_update;
	tksp->ks_private = (void *) &softsp->tempstat;

	kstat_install(tksp);
}

static int
environ_kstat_update(kstat_t *ksp, int rw)
{
	struct environkstat *envksp;
	struct environ_soft_state *softsp;

	envksp = (struct environkstat *) ksp->ks_data;
	softsp = (struct environ_soft_state *) ksp->ks_private;

	/* this is a read-only kstat. Bail out on a write */

	if (rw == KSTAT_WRITE) {
		return (EACCES);
	} else {
		/*
		 * copy the current state of the hardware into the
		 * kstat structure. The copy is done so that the oldest
		 * data always ends up in the beginning of the array.
		 */
		envksp->fanstat.value.ul = softsp->fan_state;
	}
	return (0);
}
