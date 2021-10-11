/*
 * Copyright (c) 1995 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ident "@(#)stp4020.c	1.47	95/07/18 SMI"

/*
 * DRT device/interrupt handler
 */

/* #define	DRT_DEBUG 1 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/buf.h>
#include <sys/file.h>
#include <sys/uio.h>
#include <sys/conf.h>
#include <sys/stat.h>
#include <sys/autoconf.h>
#include <sys/vtoc.h>
#include <sys/dkio.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/ddidmareq.h>
#include <sys/ddi_impldefs.h>
#include <sys/kstat.h>
#include <sys/kmem.h>

#include <sys/pctypes.h>
#include <sys/pcmcia.h>
#include <sys/sservice.h>

#include <sys/stp4020_reg.h>
#include <sys/stp4020_var.h>

#define	OUTB(a, b)	outb(a, b)
#define	INB(a)		inb(a)

int drt_getinfo(dev_info_t *, ddi_info_cmd_t, void *, void **);
static int drt_identify(dev_info_t *);
int drt_probe(dev_info_t *);
static int drt_attach(dev_info_t *, ddi_attach_cmd_t);
static int drt_detach(dev_info_t *, ddi_detach_cmd_t);
u_int drt_hi_intr(caddr_t);
u_int drt_lo_intr(caddr_t);

static void drt_ll_reset(drt_dev_t *, int);
static void drt_stop_intr(drt_dev_t *, int);
static void drt_cpr(drt_dev_t *, int);
static void drt_new_card(drt_dev_t *, int);
static void drt_fixprops(dev_info_t *);
static int drt_inquire_adapter(dev_info_t *, inquire_adapter_t *);
static void drt_timestamp(drt_dev_t *);

int drt_open(dev_t *, int, int, cred_t *);
int drt_close(dev_t, int, int, cred_t *);
int drt_read(dev_t, struct uio *, cred_t *);
int drt_write(dev_t, struct uio *, cred_t *);
int drt_ioctl(dev_t, int, int, int, cred_t *, int *);
int drt_mmap(dev_t, off_t, int);

static struct cb_ops drt_ops = {
	drt_open,		/* open */
	drt_close,		/* close */
	nodev,			/* strategy */
	nodev,			/* print */
	nodev,			/* dump */
	drt_read,		/* read */
	drt_write,		/* write */
	drt_ioctl,		/* ioctl */
	nodev,			/* devmap */
	drt_mmap,		/* mmap */
	nodev,			/* segmap */
	nochpoll,		/* poll */
	nodev,			/* cb_prop_op */
	NULL,			/* streamtab  */
	D_NEW | D_MP		/* Driver compatibility flag */
};

static struct dev_ops drt_devops = {
	DEVO_REV,
	0,
	drt_getinfo,
	drt_identify,
	drt_probe,
	drt_attach,
	drt_detach,
	nulldev,
	&drt_ops,
	NULL,
	ddi_power
};

#if defined(DRT_DEBUG)
static void drt_dmp_regs(stp4020_socket_csr_t *);
int drt_debug = 0;
#endif

/* bit patterns to select voltage levels */
int drt_vpp_levels[13] = {
	0, 0, 0, 0, 0,
	1,			/* 5V */
	0, 0, 0, 0, 0, 0,
	2			/* 12V */
};
struct power_entry drt_power[DRT_NUM_POWER] = {
	{
		0,		/* off */
		VCC|VPP1|VPP2
	},
	{
		5*10,		/* 5Volt */
		VCC|VPP1|VPP2
	},
	{
		12*10,		/* 12Volt */
		VPP1|VPP2
	},
};

drt_dev_t *drt_get_driver_private(dev_info_t *);
u_int drt_hi_intr(caddr_t);
u_int drt_lo_intr(caddr_t);

static int drt_callback(dev_info_t *, int (*)(), int);
static int drt_inquire_adapter(dev_info_t *, inquire_adapter_t *);
static int drt_get_adapter(dev_info_t *, get_adapter_t *);
static int drt_get_page(dev_info_t *, get_page_t *);
static int drt_get_socket(dev_info_t *, get_socket_t *);
static int drt_get_status(dev_info_t *, get_ss_status_t *);
static int drt_get_window(dev_info_t *, get_window_t *);
static int drt_inquire_socket(dev_info_t *, inquire_socket_t *);
static int drt_inquire_window(dev_info_t *, inquire_window_t *);
static int drt_reset_socket(dev_info_t *, int, int);
static int drt_set_page(dev_info_t *, set_page_t *);
static int drt_set_window(dev_info_t *, set_window_t *);
static int drt_set_socket(dev_info_t *, set_socket_t *);
static int drt_set_interrupt(dev_info_t *, set_irq_handler_t *);
static int drt_clear_interrupt(dev_info_t *, clear_irq_handler_t *);
void drt_socket_card_id(drt_dev_t *, drt_socket_t *, int);

#ifdef	XXX	/* XXX what is this function for?? where is it?? */
static int drt_init_dev(dev_info_t *, init_dev_t *);
#endif

/*
 * pcmcia interface operations structure
 * this is the private interface that is exported to the nexus
 */
pcmcia_if_t drt_if_ops = {
	PCIF_MAGIC,
	PCIF_VERSION,
	drt_callback,
	drt_get_adapter,
	drt_get_page,
	drt_get_socket,
	drt_get_status,
	drt_get_window,
	drt_inquire_adapter,
	drt_inquire_socket,
	drt_inquire_window,
	drt_reset_socket,
	drt_set_page,
	drt_set_window,
	drt_set_socket,
	drt_set_interrupt,
	drt_clear_interrupt,
};

/*
 * This is the loadable module wrapper.
 */
#include <sys/modctl.h>

extern struct mod_ops mod_driverops;

static struct modldrv modldrv = {
	&mod_driverops,		/* Type of module. This one is a driver */
	"STP4020 (SUNW,pcmcia) adapter driver",	/* Name of the module. */
	&drt_devops,		/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modldrv, NULL
};

int
_init()
{
	return (mod_install(&modlinkage));
}

int
_fini()
{
	return (mod_remove(&modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

static int
drt_identify(dev_info_t *devi)
{
	char *dname = ddi_get_name(devi);

	if (dname != NULL) {
		if (strchr(dname, ',') != NULL)
			dname = strchr(dname, ',') + 1;
		if (strcmp(dname, "pcmcia") == 0)
			return (DDI_IDENTIFIED);
	}
	return (DDI_NOT_IDENTIFIED);
}

/*
 * drt_getinfo()
 *	provide instance/device information about driver
 */
drt_getinfo(dev_info_t *dip, ddi_info_cmd_t cmd, void *arg, void **result)
{
	int error = DDI_SUCCESS;
	switch (cmd) {
	case DDI_INFO_DEVT2DEVINFO:
		/* should make independent of SUNW,pcmcia */
		dip = ddi_find_devinfo("SUNW,pcmcia", getminor((dev_t)arg), 1);
		*result = dip;
		break;
	case DDI_INFO_DEVT2INSTANCE:
		*result = 0;
		break;
	default:
		error = DDI_FAILURE;
		break;
	}

	return (error);
}

/*
 * drt_probe()
 *	dummy probe routine.
 *	not needed unless on VME
 */
drt_probe(dev_info_t *dip)
{
	if (ddi_dev_is_sid(dip) == DDI_FAILURE) {
				/* need to poke around */
		return (DDI_PROBE_FAILURE);
	}
	return (DDI_PROBE_SUCCESS);
}

/*
 * drt_attach()
 *	attach the DRT (SPARC STP4020) driver
 *	to the system.  This is a child of "sysbus" since that is where
 *	the hardware lives, but it provides services to the "pcmcia"
 *	nexus driver.  It gives a pointer back via its private data
 *	structure which contains both the dip and socket services entry
 *	points
 */
static int
drt_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	drt_dev_t *drt;
	struct pcmcia_adapter_nexus_private *drt_nexus;
	int i, base;
	char minordev[32];
#if defined(DRT_DEBUG)
	if (drt_debug) {
		printf("drt_attach: entered\n");
	}
#endif
	switch (cmd) {
	default:
		return (DDI_FAILURE);
	case DDI_RESUME:
		drt = (drt_dev_t *)drt_get_driver_private(dip);
#if defined(DRT_DEBUG)
		if (drt_debug) {
		    cmn_err(CE_CONT, "drt_attach: DDI_RESUME\n");
		}
#endif
		if (drt != NULL && drt->pc_flags & PCF_SUSPENDED) {
			/* XXX - why would drt be NULL?? */
			int sn;
			for (sn = 0; sn < DRSOCKETS; sn++) {
				drt_socket_t *sockp = &drt->pc_sockets[sn];

			    if (drt->pc_callback) {
				/*
				 * XXX - should we restore h/w state
				 *	before we do this??
				 */
				PC_CALLBACK(drt, dip, drt->pc_cb_arg,
							PCE_PM_RESUME, sn);
			    }

			    /* Restore adapter hardware state */
			    mutex_enter(&drt->pc_lock);
			    drt_cpr(drt, DRT_RESTORE_HW_STATE);
			    drt_new_card(drt, sn);
			    drt_socket_card_id(drt, sockp,
						drt->pc_csr->socket[sn].stat0);
			    mutex_exit(&drt->pc_lock);

			} /* for (sn) */
			mutex_enter(&drt->pc_lock);
			drt->pc_flags &= ~PCF_SUSPENDED;
			mutex_exit(&drt->pc_lock);
			/* do we want to do anything here??? */
			return (DDI_SUCCESS);
		}
		return (DDI_FAILURE);
	case DDI_ATTACH:
		break;
	}

	drt = (drt_dev_t *)kmem_zalloc(sizeof (drt_dev_t), KM_NOSLEEP);
	if (drt == NULL) {
		return (DDI_FAILURE);
	}

#if defined(DRT_DEBUG)
	if (drt_debug)
		printf("drt_attach: drt=%x\n", (int)drt);
#endif
	drt_nexus = (struct pcmcia_adapter_nexus_private *)
		kmem_zalloc(sizeof (struct pcmcia_adapter_nexus_private),
				KM_NOSLEEP);
	if (drt_nexus == NULL) {
		kmem_free(drt, sizeof (drt_dev_t));
		return (DDI_FAILURE);
	}
	/* map everything in we will ultimately need */
	drt->pc_devinfo = dip;
	drt->pc_csr = 0;
	if (ddi_map_regs(dip, DRMAP_ASIC_CSRS,
				(caddr_t *)&drt->pc_csr, (off_t)NULL,
				sizeof (stp4020_socket_csr_t)) != 0)
		printf("\tmap regs for csr failed\n");
#if defined(DRT_DEBUG)
	if (drt_debug) {
		printf("drt_attach: %x->%x\n", DRMAP_ASIC_CSRS,
							(int)drt->pc_csr);
	}
#endif

	drt_nexus->an_dip = dip;
	drt_nexus->an_if = &drt_if_ops;
	drt_nexus->an_private = drt;

	drt->pc_numpower = DRT_NUM_POWER;
	drt->pc_power = drt_power;

	drt->pc_numsockets = DRSOCKETS;

	ddi_set_driver_private(dip, (caddr_t)drt_nexus);

	/* allow property to override audio */
	if (ddi_getprop(DDI_DEV_T_NONE, dip,
			DDI_PROP_DONTPASS, "disable-audio", -1) == -1)
		drt->pc_flags |= PCF_AUDIO;

	/* now enable both interrupt handlers */
	if (ddi_add_intr(dip, 1, &drt->pc_icookie_hi, &drt->pc_dcookie_hi,
				drt_hi_intr, (caddr_t)dip) != DDI_SUCCESS) {
		/* if it fails, unwind everything */
		ddi_unmap_regs(dip, DRMAP_ASIC_CSRS, (caddr_t *)&drt->pc_csr,
				(off_t)NULL,
				sizeof (stp4020_socket_csr_t));
		kmem_free((caddr_t)drt, sizeof (drt_dev_t));
		kmem_free((caddr_t)drt_nexus, sizeof (*drt_nexus));
		return (DDI_FAILURE);
	}

	if (ddi_add_intr(dip, 0, &drt->pc_icookie_lo, &drt->pc_dcookie_lo,
				drt_lo_intr, (caddr_t)dip) != DDI_SUCCESS) {
		/* if it fails, unwind everything */
		ddi_remove_intr(dip, 0, &drt->pc_icookie_hi);
		ddi_unmap_regs(dip, DRMAP_ASIC_CSRS, (caddr_t *)&drt->pc_csr,
				(off_t)NULL,
				sizeof (stp4020_socket_csr_t));
		kmem_free((caddr_t)drt, sizeof (drt_dev_t));
		kmem_free((caddr_t)drt_nexus, sizeof (*drt_nexus));
		return (DDI_FAILURE);
	}
	mutex_init(&drt->pc_lock, "STP4020 register lock", MUTEX_DRIVER,
		    drt->pc_icookie_hi);
	mutex_init(&drt->pc_intr, "STP4020 interrupt lock", MUTEX_DRIVER,
		    drt->pc_icookie_hi);
	mutex_init(&drt->pc_tslock, "STP4020 PM timestamp lock", MUTEX_DRIVER,
		    drt->pc_icookie_hi);

	drt_nexus->an_iblock = &drt->pc_icookie_hi;
	drt_nexus->an_idev = &drt->pc_dcookie_hi;

	mutex_enter(&drt->pc_lock);

	ddi_report_dev(dip);
	base = ddi_get_instance(dip);
	sprintf(minordev, "adapter%d", base);
	base = base * 0x80;
	for (i = 0; i < DRSOCKETS; i++) {
		char slotdev[32];
		sprintf(slotdev, "%s,socket%d", minordev, i);

		/* work around for false status bugs */
		drt->pc_csr->socket[i].stat0 = 0x3FFF;
		drt->pc_csr->socket[i].stat1 = 0x3FFF;

		/*
		 * enable the socket as well
		 * want status change interrupts for all possible events
		 * We do this even though CS hasn't asked.  The system
		 * wants to manage these and will only tell CS of those
		 * it asks for
		 */
		drt->pc_csr->socket[i].ctl1 = 0; /* turn things off */
		drt->pc_csr->socket[i].ctl0 = 0;

		/* identify current state of card */
		drt_socket_card_id(drt, &drt->pc_sockets[i],
					drt->pc_csr->socket[i].stat0);

		/* finally, turn it on */
		drt->pc_csr->socket[i].ctl0 = DRT_CHANGE_DEFAULT;
	}

/*
 * XXX - To fix a strange interaction between CPR, PM and this driver,
 *	we don't use timestamps or power management for now.
 */
#ifdef	XXX
	drv_getparm(TIME, &drt->pc_timestamp);
	(void) ddi_prop_create(DDI_DEV_T_ANY, dip, DDI_PROP_CANSLEEP,
				"pm_timestamp", (caddr_t) &drt->pc_timestamp,
				sizeof (u_long));
	i = 1;
	(void) ddi_prop_create(DDI_DEV_T_NONE, dip, DDI_PROP_CANSLEEP,
				"pm_norm_pwr", (caddr_t) &i, sizeof (int));
#endif
	drt_fixprops(dip);

	mutex_exit(&drt->pc_lock);

	return (DDI_SUCCESS);
}

/*
 * drt_detach()
 *	request to detach from the system
 */
static int
drt_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	int i;
	drt_dev_t *drt = (drt_dev_t *)drt_get_driver_private(dip);

	switch (cmd) {
	case DDI_DETACH:
		if (drt != NULL) {
			/* turn everything off for all sockets and chips */
			for (i = 0; i < drt->pc_numsockets; i++) {
				drt->pc_csr->socket[i].ctl0 = 0;
				drt->pc_csr->socket[i].ctl1 = 0;
			}
			ddi_unmap_regs(dip, DRMAP_ASIC_CSRS,
					(caddr_t *)&drt->pc_csr,
					(off_t)NULL,
					sizeof (stp4020_socket_csr_t));
			ddi_remove_intr(dip, 0, drt->pc_icookie_lo);
			ddi_remove_intr(dip, 1, drt->pc_icookie_hi);
			drt->pc_flags = 0;
			mutex_destroy(&drt->pc_lock);
			return (DDI_SUCCESS);
		}
		break;

	case DDI_SUSPEND:
#if defined(DRT_DEBUG)
		if (drt_debug) {
		    cmn_err(CE_CONT, "drt_detach: DDI_SUSPEND\n");
		}
#endif
		if (drt != NULL) {
			/* XXX - why is this test necessary here? */
			int sn;
			mutex_enter(&drt->pc_lock);
			drt->pc_flags |= PCF_SUSPENDED;
			mutex_exit(&drt->pc_lock);
			for (sn = 0; sn < DRSOCKETS; sn++) {
			    /* drt_stop_intr(drt, sn); XXX ?? */
			    mutex_enter(&drt->pc_lock);
			    drt_new_card(drt, sn); /* clears sockp->drt_flags */
			    mutex_exit(&drt->pc_lock);
			    if (drt->pc_callback) {
				PC_CALLBACK(drt, dip, drt->pc_cb_arg,
							PCE_PM_SUSPEND, sn);
			    }
			}
			/*
			 * Save the adapter's hardware state here
			 */
			mutex_enter(&drt->pc_lock);
			drt_cpr(drt, DRT_SAVE_HW_STATE);
			mutex_exit(&drt->pc_lock);
			return (DDI_SUCCESS);
		} /* if (drt) */
	} /* switch */
	return (DDI_FAILURE);
}

drt_dev_t *
drt_get_driver_private(dev_info_t *dip)
{
	struct pcmcia_adapter_nexus_private *nexus;
	nexus = (struct pcmcia_adapter_nexus_private *)
		ddi_get_driver_private(dip);
	return ((drt_dev_t *)nexus->an_private);
}

/* ARGSUSED */
drt_open(dev_t *dev, int flag, int otype, cred_t *cred)
{

	return (0);
}

/* ARGSUSED */
drt_close(dev_t dev, int flag, int otype, cred_t *cred)
{

	return (0);
}

/* ARGSUSED */
drt_ioctl(dev_t dev, int cmd, int arg, int mode, cred_t *cred, int *rval)
{

	return (EINVAL);
}

/* ARGSUSED */
drt_mmap(dev_t dev, off_t off, int prot)
{

	return (EINVAL);
}

/* ARGSUSED */
drt_read(dev_t dev, struct uio *uiop, cred_t *cred)
{

	return (0);
}

/* ARGSUSED */
drt_write(dev_t dev, struct uio *uiop, cred_t *cred)
{

	return (0);
}

/*
 * drt_inquire_adapter()
 *	SocketServices InquireAdapter function
 *	get characteristics of the physical adapter
 */
static
drt_inquire_adapter(dev_info_t *dip, inquire_adapter_t *config)
{
	drt_dev_t *drt = drt_get_driver_private(dip);
#if defined(DRT_DEBUG)
	if (drt_debug)
		printf("drt_inquire_adapter\n");
#endif
	config->NumSockets = drt->pc_numsockets;
	config->NumWindows = DRT_NUMWINDOWS;
	config->NumEDCs = 0;
	config->AdpCaps = 0;
	config->ActiveHigh = 3;
	config->ActiveLow = 0;
	config->NumPower = drt->pc_numpower;
	config->power_entry = drt->pc_power; /* until we resolve this */
	drt_timestamp(drt);
	return (SUCCESS);
}

/*
 * drt_callback()
 *	The PCMCIA nexus calls us via this function
 *	in order to set the callback function we are
 *	to call the nexus with
 */
static int
drt_callback(dev_info_t *dip, int (*handler)(), int arg)
{
	drt_dev_t *drt = (drt_dev_t *)drt_get_driver_private(dip);
#if defined(DRT_DEBUG)
	if (drt_debug) {
#ifdef	XXX
		printf("drt_callback: drt=%x, lock=%x\n",
						(int)drt, (int)drt->pc_lock);
#endif
		printf("\thandler=%x, arg=%x\n", handler, arg);
	}
#endif
	if (handler != NULL) {
		drt->pc_callback = handler;
		drt->pc_cb_arg  = arg;
		drt->pc_flags |= PCF_CALLBACK;
	} else {
		drt->pc_callback = NULL;
		drt->pc_cb_arg = 0;
		drt->pc_flags &= ~PCF_CALLBACK;
	}
	/*
	 * we're now registered with the nexus
	 * it is acceptable to do callbacks at this point.
	 * don't call back from here though since it could block
	 */
	drt_timestamp(drt);
	return (PC_SUCCESS);
}

/*
 * drt_calc_speed()
 *	determine the bit pattern for speeds to be put in the control register
 */

static
drt_calc_speed(int speed)
{
	int length;
	int delay;
	/*
	 * the documented speed determination (25MHZ) is
	 * 250 + (CMDLNG - 4) * 40 < speed <= 250 + (CMDLNG - 3) * 40
	 * The value of CMDLNG is roughly determined by
	 * CMDLNG == ((speed - 250) / 40) + [3|4]
	 * the calculation is very approximate.
	 * for speeds <= 250ns, use simple formula
	 *
	 * this should really be based on processor speed.
	 */

	if (speed <= 250) {
		if (speed < 100)
			speed = 100;
		length = (speed - 100) / 50;
		if (speed <= 100)
			delay = 1;
		else
			delay = 2;
	} else {
		length = ((speed - 250) / 40);
		if ((250 + (length - 3) * 40) == speed)
			length += 3;
		else
			length += 4;
		delay = 2;
	}

#if defined(DRT_DEBUG)
	if (drt_debug)
		printf("drt_calc_speed: speed=%d, length=%x, "
			"delay=%x, ret=%x\n",
			speed, length, delay,
			(SET_DRWIN_CMDDLY(delay) | SET_DRWIN_CMDLNG(length)));
#endif
	return (SET_DRWIN_CMDDLY(delay) | SET_DRWIN_CMDLNG(length));
}

/*
 * drt_set_window
 *	essentially the same as the Socket Services specification
 *	We use socket and not adapter since they are identifiable
 *	but the rest is the same
 *
 *	dip	drt driver's device information
 *	window	parameters for the request
 */

drt_set_window(dev_info_t *dip, set_window_t *window)
{
	register int prevstate;
	int which, win;
	drt_dev_t *drt = drt_get_driver_private(dip);
	drt_socket_t *sockp;
	struct drt_window *winp;
	stp4020_socket_csr_t *csrp;
	int windex;

#if defined(DRT_DEBUG)
	if (drt_debug) {
		printf("drt_set_window: entered\n");
		printf("\twindow=%d, socket=%d, WindowSize=%d, speed=%d\n",
			window->window, window->socket, window->WindowSize,
			window->speed);
		printf("\tbase=%x, state=%x\n", (int)window->base,
							window->state);
	}
#endif

	drt_timestamp(drt);

	/*
	 * do some basic sanity checking on what we support
	 * we don't do paged mode
	 */
	if (window->state & WS_PAGED)
		return (BAD_ATTRIBUTE);

	/*
	 * make sure we use the correct internal socket/window
	 * combination
	 */
	win = window->window % DRWINDOWS;
	if (window->socket != (window->window / DRWINDOWS)) {
		return (BAD_SOCKET);
	}

	if ((window->WindowSize != DRWINSIZE &&
		!(window->state & WS_EXACT_MAPIN)) ||
	    window->WindowSize > DRWINSIZE) {
		return (BAD_SIZE);
	}

	sockp = &drt->pc_sockets[window->socket];

#if defined(DRT_DEBUG)
	if (drt_debug)
		printf("\tusing window/socket %d/%d\n", win, window->socket);
#endif

	/*
	 * we don't care about previous mappings.
	 * Card Services will deal with that so don't
	 * even check
	 */

	winp = &sockp->drt_windows[win];
	csrp = &drt->pc_csr->socket[window->socket];
	prevstate = winp->drtw_status;

	mutex_enter(&drt->pc_lock); /* protect the registers */
	which = 0;		/* no error */

	/* disable current settings */
	csrp->window[win].ctl0 = 0;

	/*
	 * disable current mapping
	 * this will handle the case of WS_ENABLED not being set
	 */

	if ((window->state & (WS_IO|WS_EXACT_MAPIN)) ==
	    (WS_IO|WS_EXACT_MAPIN)) {
		if (window->base != 0) {
			/* compensate for having to start at 0 */
			window->WindowSize += (ulong)window->base;
		}
	}

	if (window->socket == 0)
		windex = DRMAP_CARD0_WIN0 + win;
	else
		windex = DRMAP_CARD1_WIN0 + win;

	if (prevstate & DRW_MAPPED && window->WindowSize != winp->drtw_len) {
		mutex_exit(&drt->pc_lock);
		ddi_unmap_regs(drt->pc_devinfo, windex,
				&winp->drtw_base, 0, winp->drtw_len);
		mutex_enter(&drt->pc_lock);
#if defined(DRT_DEBUG)
		if (drt_debug)
			printf("\tunmapped: base being set to NULL\n");
#endif
		winp->drtw_status &= ~(DRW_MAPPED|DRW_ENABLED);
		winp->drtw_base = NULL;
	}

	if (window->state & WS_ENABLED) {
		if (winp->drtw_base == NULL) {
			mutex_exit(&drt->pc_lock);
			which = ddi_map_regs(drt->pc_devinfo,
						windex,
						&winp->drtw_base, 0,
						window->WindowSize);
			mutex_enter(&drt->pc_lock);
			if (which != DDI_SUCCESS) {
				printf("set_window: can't allocate map\n");
				mutex_exit(&drt->pc_lock);
				return (BAD_SIZE);
			}
#if defined(DRT_DEBUG)
			if (drt_debug)
				printf("\tmapped: base = %x, len=%x\n",
							(int)winp->drtw_base,
							window->WindowSize);
#endif
		}

		winp->drtw_status |= DRW_MAPPED | DRW_ENABLED;
		if (!(window->state & WS_IO)) {
			winp->drtw_speed = window->speed;
			winp->drtw_ctl0 = drt_calc_speed(window->speed);
			winp->drtw_ctl0 |= DRWIN_ASPSEL_CM;
			winp->drtw_flags &= ~DRW_IO;
			window->base = winp->drtw_base;
		} else {
			winp->drtw_flags |= DRW_IO;
			winp->drtw_ctl0 = DRWIN_ASPSEL_IO |
				drt_calc_speed(window->speed);
			window->base = winp->drtw_base + (ulong)window->base;
		}
		csrp->window[win].ctl0 = winp->drtw_ctl0;
		csrp->window[win].ctl1 = SET_DRWIN_WAITREQ(1) |
			SET_DRWIN_WAITDLY(0);
		winp->drtw_len = window->WindowSize;
	} else {
		mutex_enter(&drt->pc_lock);
		if (winp->drtw_status & DRW_ENABLED) {
			winp->drtw_status &= ~DRW_ENABLED;
			csrp->window[win].ctl0 = 0; /* force off */
		}
	}

#if defined(DRT_DEBUG)
	if (drt_debug) {
		printf("\tbase now set to %x, csrp=%x, winreg=%x, len=%x\n",
			(int)window->base, (int)csrp,
			(int)&csrp->window[win].ctl0,
			window->WindowSize);
		printf("\twindow type is now %s\n", window->state & WS_IO ?
			"I/O" : "memory");
		if (drt_debug > 1)
			drt_dmp_regs(csrp);
	}
#endif

	mutex_exit(&drt->pc_lock);

	return (SUCCESS);
}

/*
 * drt_card_state()
 *	compute the instantaneous Card State information
 */
drt_card_state(drt_dev_t *drt, int socket)
{
	int value, result;

	drt_timestamp(drt);

	mutex_enter(&drt->pc_lock); /* protect the registers */

	value = drt->pc_csr->socket[socket].stat0;
#if defined(DRT_DEBUG)
	if (drt_debug) {
		printf("drt_card_state: socket=%d, *lock=%x\n",
			socket, (int)&drt->pc_lock);
		printf("\tcsr@%x\n", (int)drt->pc_csr);

		printf("\tstat0=%b\n", value,
			"\020\1PWRON\2WAIT\3WP\4RDYBSY\5BVD1\6BVD2\7CD1"
			"\10CD2\011ACCTO\012WPC\013RBC\014BVD1C\015BVD2C"
			"\016CDSC\017STAT");
		printf("\tstat1=%x\n", drt->pc_csr->socket[socket].stat1);
		printf("\t&stat0=%x, &stat1=%x\n",
			(int)&drt->pc_csr->socket[socket].stat0,
			(int)&drt->pc_csr->socket[socket].stat1);
	}
#endif

	if (value & DRSTAT_WPST)
		result = SBM_WP;
	else
		result = 0;

	switch (value & DRSTAT_BVDST) {
	case DRSTAT_BATT_LOW:
		result |= SBM_BVD2;
		break;
	case DRSTAT_BATT_OK:
		break;
	default:
		/* battery dead */
		result |= SBM_BVD1;
		break;
	}

	if (value & DRSTAT_RDYST)
		result |= SBM_RDYBSY;
	if ((value & (DRSTAT_CD1ST|DRSTAT_CD2ST)) ==
	    (DRSTAT_CD1ST|DRSTAT_CD2ST))
		result |= SBM_CD;

	mutex_exit(&drt->pc_lock);

	return (result);
}

/*
 * drt_set_page()
 *	SocketServices SetPage function
 *	set the page of PC Card memory that should be in the mapped
 *	window
 */

drt_set_page(dev_info_t *dip, set_page_t *page)
{
	int which, socket, win;
	drt_dev_t *drt = drt_get_driver_private(dip);
	drt_socket_t *sockp;
	struct drt_window *winp;
	stp4020_socket_csr_t *csrp;

	drt_timestamp(drt);

	if (page->window >= DRT_NUMWINDOWS) {
#if defined(DRT_DEBUG)
		if (drt_debug)
			printf("drt_set_page: window=%d (%d)\n",
				page->window, DRWINDOWS);
#endif
		return (BAD_WINDOW);
	}

	win = page->window % DRWINDOWS;
	socket = page->window / DRWINDOWS;

	sockp = &drt->pc_sockets[socket];

#if defined(DRT_DEBUG)
	if (drt_debug) {
		printf("drt_set_page: window=%d, socket=%d, page=%d\n",
			win, socket, page->page);
	}
#endif

	/* only one page supported (fixed at 1MB) */
#if defined(DRT_DEBUG)
	if (drt_debug)
		printf("\tpage=%d\n", page->page);
#endif
	winp = &sockp->drt_windows[win];
	csrp = &drt->pc_csr->socket[socket];

	if (winp->drtw_flags & DRW_IO) {
		return (BAD_WINDOW);
	}

	if (page->page != 0) {
		return (BAD_PAGE);
	}

	mutex_enter(&drt->pc_lock); /* protect the registers */

	/*
	 * now map the card's memory pages - we start with page 0
	 */

	if (page->state & PS_ATTRIBUTE)
		which = SET_DRWIN_CMDDLY(2) | SET_DRWIN_CMDLNG(4);
	else
		which = winp->drtw_ctl0 & (DRWIN_CMDLNG_M|DRWIN_CMDDLY_M);

	which |= (page->state & PS_ATTRIBUTE) ?
			DRWIN_ASPSEL_AM : DRWIN_ASPSEL_CM;

	/* if card says Write Protect, enforce it */
	/* but we don't have hardware support to do it */

	/* The actual PC Card address mapping */
#if defined(DRT_DEBUG)
	if (drt_debug)
		printf("\ta2p=%x, base=%x, csrp=%x\n",
			(int)ADDR2PAGE(page->offset),
			SET_DRWIN_BASE(ADDR2PAGE(page->offset)),
			(int)csrp);
#endif
	which |= SET_DRWIN_BASE(ADDR2PAGE(page->offset));
	winp->drtw_addr = (caddr_t)page->offset;

	/* now set the register */
#if defined(DRT_DEBUG)
	if (drt_debug)
		printf("\tset ctl0=%x\n", which);
#endif

	csrp->window[win].ctl0 = (ushort_t)which;
	csrp->window[win].ctl1 = SET_DRWIN_WAITREQ(1) | SET_DRWIN_WAITDLY(0);

	/* now  */

#if defined(DRT_DEBUG)
	if (drt_debug) {
		printf("\tmemory type = %s\n",
			(which & DRWIN_ASPSEL_CM) ? "common" : "attribute");
	}
#endif


#if defined(DRT_DEBUG)
	if (drt_debug) {
		printf("\tpage offset=%x, base=%x (PC addr=%x, sockets=%d)\n",
			(int)page->offset, (int)winp->drtw_base,
			(int)winp->drtw_addr, drt->pc_numsockets);
		printf("\t*base=%x, win reg=%x\n",
			*(ushort *)winp->drtw_base,
			(int)&csrp->window[win].ctl0);
		if (drt_debug > 1)
			drt_dmp_regs(csrp);
	}
#endif
	mutex_exit(&drt->pc_lock);

	return (SUCCESS);
}

/*
 * drt_set_socket()
 *	Socket Services SetSocket call
 *	sets basic socket configuration
 */
static int
drt_set_socket(dev_info_t *dip, set_socket_t *socket)
{
	register int value, sock;
	drt_dev_t *drt = drt_get_driver_private(dip);
	drt_socket_t *sockp = &drt->pc_sockets[socket->socket];
	int irq = 0;
	int powerlevel = 0;
	int ind;

	drt_timestamp(drt);

	sock = socket->socket;

#if defined(DRT_DEBUG)
	if (drt_debug) {
		printf("drt_set_socket: entered (socket=%d)\n", sock);
	}
#endif
	/*
	 * check VccLevel, etc. before setting mutex
	 * if this is zero, power is being turned off
	 * if it is non-zero, power is being turned on.
	 * the default case is to assume Vcc only.
	 */

	/* this appears to be very implementation specific */

	if (socket->VccLevel == 0) {
		powerlevel = 0;
	} else  if (socket->VccLevel < drt->pc_numpower &&
		    drt_power[socket->VccLevel].ValidSignals & VCC) {
		/* enable Vcc */
		powerlevel = DRCTL_MSTPWR|DRCTL_PCIFOE;
		sockp->drt_vcc = socket->VccLevel;
	} else {
		return (BAD_VCC);
	}
#if defined(DRT_DEBUG)
	if (drt_debug) {
		printf("\tVccLevel=%d, Vpp1Level=%d, Vpp2Level=%d\n",
			socket->VccLevel,
			socket->Vpp1Level, socket->Vpp2Level);
	}
#endif
	ind = 0;		/* default index to 0 power */
	if (socket->Vpp1Level >= 0 && socket->Vpp1Level < drt->pc_numpower) {
		if (!(drt_power[socket->Vpp1Level].ValidSignals & VPP1)) {
			return (BAD_VPP);
		}
		ind = drt_power[socket->Vpp1Level].PowerLevel/10;
		powerlevel |= drt_vpp_levels[ind] << 2;
		sockp->drt_vpp1 = socket->Vpp1Level;
	}
	if (socket->Vpp2Level >= 0 && socket->Vpp2Level < drt->pc_numpower) {
		if (!(drt_power[socket->Vpp2Level].ValidSignals & VPP2)) {
			return (BAD_VPP);
		}
		ind = drt_power[socket->Vpp2Level].PowerLevel/10;
		powerlevel |= (drt_vpp_levels[ind] << 4);
		sockp->drt_vpp2 = socket->Vpp2Level;
	}

#if defined(DRT_DEBUG)
	if (drt_debug) {
		printf("\tpowerlevel=%x, ind=%x\n", powerlevel, ind);
	}
#endif
	mutex_enter(&drt->pc_lock); /* protect the registers */

	/* make sure not still in RESET */
	value = drt->pc_csr->socket[sock].ctl0;
	drt->pc_csr->socket[sock].ctl0 = value & ~DRCTL_RESET;
	/*
	 * ctlind processing -- we can ignore this
	 * there aren't any outputs on the chip for this
	 * the GUI will display what it thinks is correct
	 */


	/* handle event mask */
	sockp->drt_intmask = socket->SCIntMask;
	value = (drt->pc_csr->socket[sock].ctl0 & ~DRT_CHANGE_MASK) |
		DRT_CHANGE_DEFAULT; /* always want CD */

	if (socket->SCIntMask & SBM_CD)
		value |= DRCTL_CDIE;
	if (socket->SCIntMask & SBM_BVD1)
		value |= DRCTL_BVD1IE;
	if (socket->SCIntMask & SBM_BVD2)
		value |= DRCTL_BVD2IE;
	if (socket->SCIntMask & SBM_WP)
		value |= DRCTL_WPIE;
	if (socket->SCIntMask & SBM_RDYBSY)
		value |= DRCTL_RDYIE;
	/* irq processing */
	if (socket->IFType == IF_IO) {
				/* IRQ only for I/O */
		irq = socket->IREQRouting & 0xF;
		if (socket->IREQRouting & IRQ_ENABLE) {
			irq = DRCTL_IOIE;
			if (socket->IREQRouting & IRQ_PRIORITY) {
				irq |= DRCTL_IOILVL_SB1;
				sockp->drt_flags |= DRT_INTR_HIPRI;
			} else {
				irq |= DRCTL_IOILVL_SB0;
			}
			sockp->drt_flags |= DRT_INTR_ENABLED;
		} else {
			irq = 0; /* no interrupts */
			sockp->drt_flags &= ~(DRT_INTR_ENABLED|DRT_INTR_HIPRI);
		}
		sockp->drt_irq = socket->IREQRouting;

#if defined(DRT_DEBUG)
		if (drt_debug) {
			printf("\tsocket type is I/O and irq %x is %s\n", irq,
				(socket->IREQRouting & IRQ_ENABLE) ?
				"enabled" : "not enabled");
		}
#endif
		sockp->drt_flags |= DRT_SOCKET_IO;
		if (drt->pc_flags & PCF_AUDIO)
			value |= DRCTL_IFTYPE_IO | irq | DRCTL_SPKREN;
	} else {
		/* enforce memory mode */
		value &= ~(DRCTL_IFTYPE_IO | DRCTL_SPKREN);
		sockp->drt_flags &= ~(DRT_INTR_ENABLED|DRT_SOCKET_IO);
	}
	drt->pc_csr->socket[sock].ctl0 = (ushort_t)value;

	/*
	 * set power to socket
	 * note that the powerlevel was calculated earlier
	 */

	drt->pc_csr->socket[sock].ctl1 = (ushort_t)powerlevel;
#if defined(DRT_DEBUG)
	if (drt_debug) {
		printf("\tpowerlevel (socket->ctl1) = %x\n", powerlevel);
		if (drt_debug > 1)
			drt_dmp_regs(&drt->pc_csr->socket[sock]);
	}
#endif
	sockp->drt_state = socket->State;
	mutex_exit(&drt->pc_lock);
	return (SUCCESS);
}

/*
 * drt_inquire_socket()
 *	SocketServices InquireSocket function
 *	returns basic characteristics of the socket
 */
static int
drt_inquire_socket(dev_info_t *dip, inquire_socket_t *socket)
{
	int value;
	drt_dev_t *drt = drt_get_driver_private(dip);

	drt_timestamp(drt);

	socket->SCIntCaps = DRT_DEFAULT_INT_CAPS;
	socket->SCRptCaps = DRT_DEFAULT_RPT_CAPS;
	socket->CtlIndCaps = DRT_DEFAULT_CTL_CAPS;
	value = drt->pc_sockets[socket->socket].drt_flags;
	socket->SocketCaps = IF_IO | IF_MEMORY;
	socket->ActiveHigh = 3;	/* 0 and 1 */
	socket->ActiveLow = 0;

#ifdef	lint
	if (value > 0)
		panic("lint panic");
#endif

	return (SUCCESS);
}

/*
 * drt_inquire_window()
 *	SocketServices InquireWindow function
 *	returns detailed characteristics of the window
 *	this is where windows get tied to sockets
 */
static int
drt_inquire_window(dev_info_t *dip, inquire_window_t *window)
{
	int socket, win;
	drt_dev_t *drt = drt_get_driver_private(dip);
	struct drt_window *winp;
	iowin_char_t *io;
	mem_win_char_t *mem;

#if defined(DRT_DEBUG)
	if (drt_debug)
		printf("drt_inquire_window: win=%d\n", window->window);
#endif
	window->WndCaps = WC_COMMON|WC_ATTRIBUTE|WC_WAIT|WC_IO;

	/* get correct socket */
	socket = window->window / DRWINDOWS;
	win = window->window % DRWINDOWS;
	winp = &drt->pc_sockets[socket].drt_windows[win];
	/* initialize the socket map - one socket per window */
	PR_ZERO(window->Sockets);
	PR_SET(window->Sockets, socket);

	io = &window->iowin_char;
	io->IOWndCaps = WC_CALIGN|WC_IO_RANGE_PER_WINDOW|WC_WENABLE|
		WC_8BIT|WC_16BIT|WC_SIZE;
	io->FirstByte = (baseaddr_t)winp->drtw_base;
	io->LastByte = (baseaddr_t)winp->drtw_base + DRWINSIZE;
	io->MinSize = 1;
	io->MaxSize = DRWINSIZE;
	io->ReqGran = ddi_ptob(dip, 1);
	io->AddrLines = DRADDRLINES;
	io->EISASlot = 0;

	mem = &window->mem_win_char;
	mem->MemWndCaps = WC_CALIGN|WC_WENABLE|WC_8BIT|WC_16BIT;
	mem->FirstByte = (baseaddr_t)winp->drtw_base;
	mem->LastByte = (baseaddr_t)winp->drtw_base + DRWINSIZE;
#if defined(DRT_DEBUG)
	if (drt_debug) {
	    printf("\tFirstByte=%x, LastByte=%x\n",
		    (int)mem->FirstByte, (int)mem->LastByte);
	}
#endif
	mem->MinSize = DRWINSIZE;
	mem->MaxSize = DRWINSIZE;
	mem->ReqGran = ddi_ptob(dip, 1L);
	mem->ReqBase = 0;
	mem->ReqOffset = DRWINSIZE;
	mem->Slowest = MEM_SPEED_MAX;
	mem->Fastest = MEM_SPEED_MIN;

	return (SUCCESS);
}

/*
 * drt_get_adapter()
 *	SocketServices GetAdapter function
 *	this is nearly a no-op.
 */
static int
drt_get_adapter(dev_info_t *dip, get_adapter_t *adapt)
{
	drt_dev_t *drt = drt_get_driver_private(dip);

	drt_timestamp(drt);

	if (drt->pc_flags & PCF_INTRENAB)
		adapt->SCRouting = IRQ_ENABLE;
	adapt->state = 0;
	return (SUCCESS);
}

/*
 * drt_get_page()
 *	SocketServices GetPage function
 *	returns info about the window
 */
static int
drt_get_page(dev_info_t *dip, get_page_t *page)
{
	int socket, window;
	drt_dev_t *drt = drt_get_driver_private(dip);
	struct drt_window *winp;

	drt_timestamp(drt);

	window = page->window % DRWINDOWS;
	socket = page->window / DRWINDOWS;

	winp = &drt->pc_sockets[socket].drt_windows[window];

				/* ??? */
	if (page->page > 0)
		return (BAD_PAGE);

	page->state = winp->drtw_status;
	page->offset = (off_t)winp->drtw_addr;

	return (SUCCESS);
}

/*
 * drt_get_socket()
 *	SocketServices GetSocket
 *	returns information about the current socket settings
 */
static int
drt_get_socket(dev_info_t *dip, get_socket_t *socket)
{
	int socknum, irq_enabled;
	drt_socket_t *sockp;
	drt_dev_t *drt = drt_get_driver_private(dip);

	drt_timestamp(drt);

	socknum = socket->socket;
	sockp = &drt->pc_sockets[socknum];

	socket->SCIntMask = sockp->drt_intmask;
	socket->state = sockp->drt_state;
	socket->VccLevel = sockp->drt_vcc;
	socket->Vpp1Level = sockp->drt_vpp1;
	socket->Vpp2Level = sockp->drt_vpp2;
	socket->CtlInd = 0;	/* no indicators */
	irq_enabled = (sockp->drt_flags & DRT_INTR_ENABLED) ? IRQ_ENABLE : 0;
	irq_enabled |= (sockp->drt_flags & DRT_INTR_HIPRI) ? IRQ_HIGH : 0;
	socket->IRQRouting = sockp->drt_irq | irq_enabled;
	socket->IFType = (sockp->drt_flags & DRT_SOCKET_IO) ? IF_IO : IF_MEMORY;
	return (SUCCESS);
}

/*
 * drt_get_status()
 *	SocketServices GetStatus
 *	returns status information about the PC Card in
 *	the selected socket
 */
static int
drt_get_status(dev_info_t *dip, get_ss_status_t *status)
{
	int socknum, irq_enabled;
	drt_socket_t *sockp;
	drt_dev_t *drt = drt_get_driver_private(dip);
#if defined(DRT_DEBUG)
	if (drt_debug) {
		printf("drt_get_status: drt=%x\n", (int)drt);
	}
#endif

	if (drt == NULL) {
		return (BAD_ADAPTER);
	}

	drt_timestamp(drt);

	socknum = status->socket;
	sockp = &drt->pc_sockets[socknum];

	status->CardState = drt_card_state(drt, socknum);
	status->SocketState = sockp->drt_state;
	status->CtlInd = 0;	/* no indicators */
	irq_enabled = (sockp->drt_flags & DRT_INTR_ENABLED) ? IRQ_ENABLE : 0;
	status->IRQRouting = sockp->drt_irq | irq_enabled;
	status->IFType = (sockp->drt_flags & DRT_SOCKET_IO) ?
		IF_IO : IF_MEMORY;
	return (SUCCESS);
}

/*
 * drt_get_window()
 *	SocketServices GetWindow function
 *	returns state information about the specified window
 */
static int
drt_get_window(dev_info_t *dip, get_window_t *window)
{
	register int socket, win;
	drt_socket_t *sockp;
	drt_dev_t *drt = drt_get_driver_private(dip);
	struct drt_window *winp;

	drt_timestamp(drt);

	if (window->window >= DRT_NUMWINDOWS) {
#if defined(DRT_DEBUG)
		if (drt_debug)
			printf("drt_get_window: failed\n");
#endif
		return (BAD_WINDOW);
	}
	socket = window->window / DRWINDOWS;
	win = window->window % DRWINDOWS;
	window->socket = socket;
	sockp = &drt->pc_sockets[socket];
	winp = &sockp->drt_windows[win];

	window->size = winp->drtw_len;
	window->speed = winp->drtw_speed;
	window->base = (baseaddr_t)winp->drtw_base;
	window->state = 0;

	if (winp->drtw_flags & DRW_IO)
		window->state |= WS_IO;

	if (winp->drtw_status & DRW_ENABLED)
		window->state |= WS_ENABLED;
#if defined(DRT_DEBUG)
	if (drt_debug) {
		printf("drt_get_window: socket=%d, window=%d\n", socket, win);
		printf("\tsize=%d, speed=%d, base=%x, state=%x\n",
			window->size, (int)window->speed, (int)window->base,
			window->state);
	}
#endif

	return (SUCCESS);
}

/*
 * drt_ll_reset - This function handles the socket RESET signal timing and
 *			control.
 *
 *	There are two variables that control the RESET timing:
 *		drt_prereset_time - time in mS before asserting RESET
 *		drt_reset_time - time in mS to assert RESET
 *
 * XXX - need to rethink RESET timing delays to avoid using drv_usecwait
 */
int drt_prereset_time = 1;
int drt_reset_time = 5;

static void
drt_ll_reset(drt_dev_t *drt, int socket)
{
	u_long value;

	value = drt->pc_csr->socket[socket].ctl0;

	if (drt_prereset_time > 0)
		drv_usecwait(drt_prereset_time * 1000);

	/* turn reset on then off again */
	drt->pc_csr->socket[socket].ctl0 = value | DRCTL_RESET;

	if (drt_reset_time > 0)
		drv_usecwait(drt_reset_time * 1000);

	drt->pc_csr->socket[socket].ctl0 = value & ~DRCTL_RESET;

#if defined(DRT_DEBUG)
	if (drt_debug)
		printf("drt_ll_reset: socket=%d, ctl0=%x, ctl1=%x\n",
			socket,
			drt->pc_csr->socket[socket].ctl0,
			drt->pc_csr->socket[socket].ctl1);
#endif
}

/*
 * drt_new_card()
 *	put socket into known state on card insertion
 */
static void
drt_new_card(drt_dev_t *drt, int socket)
{
	drt->pc_csr->socket[socket].ctl0 = 0; /* off */
	drt->pc_csr->socket[socket].ctl0 = DRT_CHANGE_DEFAULT; /* on */
	drt->pc_csr->socket[socket].ctl1 = 0;
	drt->pc_sockets[socket].drt_state = 0;
	drt->pc_sockets[socket].drt_flags = 0;
}

/*
 * drt_reset_socket()
 *	SocketServices ResetSocket function
 *	puts the PC Card in the socket into the RESET state
 *	and then takes it out after the the cycle time
 *	The socket is back to initial state when done
 */
static int
drt_reset_socket(dev_info_t *dip, int socket, int mode)
{
	drt_dev_t *drt = drt_get_driver_private(dip);
	int window;
	drt_socket_t *sockp;

	mutex_enter(&drt->pc_lock); /* protect the registers */

	drt_ll_reset(drt, socket);

	if (mode == RESET_MODE_FULL) {
		/* need to unmap windows, etc. */

		drt->pc_sockets[socket].drt_state = 0;

		for (window = 0, sockp = &drt->pc_sockets[socket];
			window < DRT_NUMWINDOWS; window++) {
			sockp->drt_windows[window].drtw_status &= ~DRW_ENABLED;
		}
	}

	mutex_exit(&drt->pc_lock);
	drt_timestamp(drt);
	return (SUCCESS);
}

/*
 * drt_set_interrupt()
 *	SocketServices SetInterrupt function
 */
static int
drt_set_interrupt(dev_info_t *dip, set_irq_handler_t *handler)
{
	struct inthandler *intr;
	drt_dev_t *drt = drt_get_driver_private(dip);

	drt_timestamp(drt);

	intr = (struct inthandler *)kmem_zalloc(sizeof (struct inthandler),
						KM_NOSLEEP);
	if (intr == NULL) {
		return (BAD_IRQ);
	}

	intr->intr = (u_int (*)())handler->handler;
	intr->handler_id = handler->handler_id;
	intr->arg = handler->arg;
	intr->socket = handler->socket;
	intr->irq = handler->irq;
	intr->priority = handler->priority;

	mutex_enter(&drt->pc_intr);
	mutex_enter(&drt->pc_lock); /* protect the registers and structures */

	intr->next = drt->pc_handlers;
	drt->pc_handlers = intr;

	/* interrupt handlers for both interrupts already done in attach */

	/*
	 * need to fill in cookies in event of multiple high priority
	 * interrupt handlers on same IRQ
	 */

	if (intr->priority < PRIORITY_HIGH) {
		intr->iblk_cookie = drt->pc_icookie_lo;
		intr->idev_cookie = drt->pc_dcookie_lo;
	} else {
		intr->iblk_cookie = drt->pc_icookie_hi;
		intr->idev_cookie = drt->pc_dcookie_hi;
		handler->irq |= IRQ_PRIORITY;
	}
	mutex_exit(&drt->pc_lock);
	mutex_exit(&drt->pc_intr);

	handler->iblk_cookie = &intr->iblk_cookie;
	handler->idev_cookie = &intr->idev_cookie;

	return (SUCCESS);
}

/*
 * drt_clear_interrupt()
 *	SocketServices ClearInterrupt function
 *	"What  controls the socket interrupt?"
 */
static int
drt_clear_interrupt(dev_info_t *dip, clear_irq_handler_t *handler)
{
	int i;
	drt_dev_t *drt = drt_get_driver_private(dip);
	struct inthandler *intr, *prev;

	drt_timestamp(drt);

	mutex_enter(&drt->pc_lock); /* protect the registers */
	intr = drt->pc_handlers;
	prev = (struct inthandler *)&drt->pc_handlers;
	while (intr != NULL) {
		if (intr->handler_id == handler->handler_id) {
			i = intr->irq;
			prev->next = intr->next;
			kmem_free((caddr_t)intr, sizeof (struct inthandler));
			intr = prev->next;
		} else {
			prev = intr;
			intr = intr->next;
		}
	}
	mutex_exit(&drt->pc_lock);

#ifdef	lint
	if (i > 0)
		panic("lint panic");
#endif

	return (SUCCESS);
}

static void
drt_stop_intr(drt_dev_t *drt, int socket)
{
	struct inthandler *intr;

	drt_timestamp(drt);

	mutex_enter(&drt->pc_intr);
	for (intr = drt->pc_handlers; intr != NULL; intr = intr->next) {
		if (socket == intr->socket) {
			intr->socket |= 0x8000;	/* make an illegal socket */
		}
	}
	mutex_exit(&drt->pc_intr);
}

drt_do_intr(drt_dev_t *drt, int socket, int priority)
{
	struct inthandler *intr;
	int result = 0;

	mutex_enter(&drt->pc_intr);

#if defined(DRT_DEBUG)
	if (drt_debug > 2)
		printf("drt_do_intr(%x, %d, %d)\n", (int)drt,
							socket, priority);
#endif

	/*
	 * If we're suspended, then we don't need to process
	 *	any more interrupts. We have already (or will
	 *	shortly) be disabling all interrupts on the
	 *	adapter, but we still need to ACK any that
	 *	we receive and that the adapter has generated.
	 * XXX - do we really want to do this here, or does it
	 *	make more sense to let the clients receive any
	 *	interrupts even as we're in the process of
	 *	suspending?
	 */
	if (drt->pc_flags & PCF_SUSPENDED) {
		mutex_exit(&drt->pc_intr);
		return (DDI_INTR_CLAIMED);
	}

	for (intr = drt->pc_handlers; intr != NULL; intr = intr->next) {
#if defined(DRT_DEBUG)
		if (drt_debug > 2)
		printf("\tintr-> socket=%d, priority=%d, intr=%x,"
			"arg=%x (drt_flags=%x:%s)\n",
			intr->socket, intr->priority, intr->intr,
			(int)intr->arg,
			drt->pc_sockets[socket].drt_flags,
			(drt->pc_sockets[socket].drt_flags & DRT_INTR_ENABLED) ?
			"true":"false");
#endif
		/* may need to rethink the priority stuff */
		if (socket == intr->socket &&
		    (priority ^ (intr->priority < 10)) &&
		    drt->pc_sockets[socket].drt_flags & DRT_INTR_ENABLED) {
			result |= (*intr->intr)(intr->arg);
		}
	}
	mutex_exit(&drt->pc_intr);
	return (result);
}

u_int
drt_hi_intr(caddr_t arg)
{
	drt_dev_t *drt = drt_get_driver_private((dev_info_t *)arg);
	int i, intr_sockets = 0;
	int result, changes;

	mutex_enter(&drt->pc_lock);
#if 0
	/* this seems to cause a panic */
	drt_timestamp(drt);
#endif

#if defined(DRT_DEBUG)
	if (drt_debug)
		printf("drt_hi_intr: entered\n");
#endif

	/*
	 * need to change to only ACK and touch the slot that
	 * actually caused the interrupt.  Currently everything
	 * is acked
	 *
	 * we need to look at all known sockets to determine
	 * what might have happened, so step through the list
	 * of them
	 */

	for (i = 0; i < DRSOCKETS; i++) {
		int card_type;
		long x = drt->pc_cb_arg;
		drt_socket_t *sockp;

		sockp = &drt->pc_sockets[i];

		if (drt->pc_csr->socket[i].ctl0 & DRCTL_IFTYPE)
			card_type = IF_IO;
		else
			card_type = IF_MEMORY;

		changes = drt->pc_csr->socket[i].stat0;
#if defined(DRT_DEBUG)
		if (drt_debug)
			printf("\tstat0=%x, type=%s\n",
				changes, card_type == IF_IO ? "IO":"MEM");
#endif
		/* ack the interrupts we see */
		drt->pc_csr->socket[i].stat0 = (ushort_t)changes;

		if (changes & DRSTAT_SCINT) {
#if defined(DRT_DEBUG)
			if (drt_debug)
				printf("\tcard status change interrupt"
					" on socket %d\n", i);
#endif
			/* there was a valid interrupt on status change  */
			if (drt->pc_callback == NULL) {
				/* nothing to do */
				continue;
			}
			if (changes & DRSTAT_CDCHG) {
				if ((sockp->drt_flags &
				    DRT_CARD_PRESENT) &&
				    (changes & DRSTAT_CD_MASK) !=
				    DRSTAT_PRESENT_OK) {
					sockp->drt_flags &=
						~DRT_CARD_PRESENT;
					/*
					 * stop interrupt handler
					 * then do the callback
					 */
					drt_stop_intr(drt, i);
					/*
					 * XXX - note that drt_new_card will
					 *	clear sockp->drt_flags
					 */
					drt_new_card(drt, i); /* paranoia */
					PC_CALLBACK(drt, arg, x,
							PCE_CARD_REMOVAL, i);
					continue;
				} else {
					if ((changes & DRSTAT_CD_MASK) ==
					    DRSTAT_PRESENT_OK &&
					    !(sockp->drt_flags &
						DRT_CARD_PRESENT)) {
						drt_new_card(drt, i);
						drt_ll_reset(drt, i);
						sockp->drt_state |= SBM_CD;
						drt_socket_card_id(drt,
								    sockp,
								    changes);
						PC_CALLBACK(drt, arg, x,
								PCE_CARD_INSERT,
								i);
						continue;
					}
				}
				/*
				 * since other events may be the result of
				 * "bounce", don't check them on this pass.
				 * The insert code will check them anyway.
				 */
				continue;
			}

			/* Ready/Change Detect */
#if defined(DRT_DEBUG)
			if (drt_debug && changes & DRSTAT_RDYCHG)
				printf("\trdychg: stat=%x, type=%s\n",
					changes,
					card_type == IF_MEMORY ?
						"memory" : "I/O");
#endif
			if (card_type == IF_MEMORY &&
			    changes & DRSTAT_RDYCHG &&
			    changes & DRSTAT_RDYST) {
				sockp->drt_state |= SBM_RDYBSY;
				PC_CALLBACK(drt, arg, x, PCE_CARD_READY, i);
			}

			/* write protect switch moved */
			if (card_type == IF_MEMORY && changes & DRSTAT_WPCHG) {
				if (changes & DRSTAT_WPST)
					sockp->drt_state |= SBM_WP;
				else
					sockp->drt_state &= ~SBM_WP;
				PC_CALLBACK(drt, arg, x,
					    PCE_CARD_WRITE_PROTECT, i);
			}

			if (card_type == IF_MEMORY &&
			    changes & DRSTAT_BVDCHG) {
				/*
				 * there was a change in battery state.
				 * this could be a false alarm at
				 * card insertion but could be real.
				 * The individual change bits aren't
				 * meaningful so look at the live
				 * status and latch that
				 */
				switch (changes & DRSTAT_BVDST) {
				case DRSTAT_BATT_LOW:
					if (!(sockp->drt_flags &
					    DRT_BATTERY_LOW)) {
						sockp->drt_flags |=
							DRT_BATTERY_LOW;
						sockp->drt_state |= SBM_BVD2;
						sockp->drt_state &= ~SBM_BVD1;
						PC_CALLBACK(drt, arg, x,
							PCE_CARD_BATTERY_WARN,
							i);
					}
					break;
				case DRSTAT_BATT_OK:
					sockp->drt_state &=
						~(DRT_BATTERY_LOW|
							DRT_BATTERY_DEAD);
					sockp->drt_state &=
						~(SBM_BVD1|SBM_BVD2);
					break;
				default: /* battery failed */
					if (!(sockp->drt_flags &
					    DRT_BATTERY_DEAD)) {
						/* so we only see one of them */
						sockp->drt_flags |=
							DRT_BATTERY_DEAD;
						sockp->drt_flags &=
							DRT_BATTERY_LOW;
						sockp->drt_state |= SBM_BVD1;
						PC_CALLBACK(drt, arg, x,
							PCE_CARD_BATTERY_DEAD,
							i);
					}
				}
			}
			if (card_type == IF_IO &&
			    !(changes & DRSTAT_BVD1ST)) {
				/*
				 * Disable status change interrupts. We
				 *	will enable them again later after
				 *	Card Services has processed this
				 *	event.
				 */
				drt->pc_csr->socket[i].ctl0 &=
							~DRCTL_BVD1IE;

				/* we have an I/O status change */
				PC_CALLBACK(drt, arg, x,
					    PCE_CARD_STATUS_CHANGE,
					    i);
			}
#if 0
			/*
			 * need to reexamine this section to see what really
			 * needs to be done
			 */
			/* Battery Warn Detect */
			if (changes & DRSTAT_BVD2CHG) {
				if (card_type == IF_MEMORY &&
				    !(sockp->drt_flags & DRT_BATTERY_LOW)) {
					sockp->drt_flags |= DRT_BATTERY_LOW;
					sockp->drt_state |= SBM_BVD2;
					PC_CALLBACK(drt, arg, x,
							PCE_CARD_BATTERY_WARN,
							i);
				} else if (card_type == IF_IO) {
					PC_CALLBACK(drt, arg, x,
							PCE_CARD_STATUS_CHANGE,
							i);
				}
			}

			/* Battery Fail Detect */
			if (card_type == IF_MEMORY &&
			    changes & DRSTAT_BVD1CHG &&
			    !(sockp->drt_flags & DRT_BATTERY_DEAD)) {
				/* so we only see one of them */
				sockp->drt_flags |= DRT_BATTERY_DEAD;
				sockp->drt_state |= SBM_BVD1;
				PC_CALLBACK(drt, arg, x,
						PCE_CARD_BATTERY_DEAD, i);
			}
#endif
		}
		/* now flag any PC Card interrupts */
		if (card_type == IF_IO && changes & DRSTAT_IOINT) {
			intr_sockets |= 1 << i;
		}
#if defined(DRT_DEBUG)
		if (drt_debug)
			printf("\tsocket %d: ctl0=%x, ctl1=%x\n",
				i,
				drt->pc_csr->socket[i].ctl0,
				drt->pc_csr->socket[i].ctl1);
#endif
	}

	mutex_exit(&drt->pc_lock);

	for (i = 0, result = 0; i < DRSOCKETS; i++) {
		if (intr_sockets & (1 << i))
			result |= drt_do_intr(drt, i, 1);
	}

	if (changes & DRSTAT_SCINT || result)
		return (DDI_INTR_CLAIMED);

	return (DDI_INTR_UNCLAIMED);
}

u_int
drt_lo_intr(caddr_t arg)
{
	drt_dev_t *drt = drt_get_driver_private((dev_info_t *)arg);
	int i, result;

	drt_timestamp(drt);

#if defined(DRT_DEBUG)
	if (drt_debug)
		printf("drt_lo_intr(%x)\n", (int)arg);
#endif
	/*
	 * we need to look at all known sockets to determine
	 * what might have happened, so step through the list
	 * of them
	 */

	for (i = 0, result = 0; i < drt->pc_numsockets; i++) {
		if (drt->pc_csr->socket[i].stat0 & DRSTAT_IOINT) {
#if defined(DRT_DEBUG)
			if (drt_debug)
				printf("\tsocket=%x, stat0=%x\n",
					i, drt->pc_csr->socket[i].stat0);
#endif
			result |= drt_do_intr(drt, i, 0);
			drt->pc_csr->socket[i].stat0 |= DRSTAT_IOINT;
		}
	}
	if (result)
		return (DDI_INTR_CLAIMED);
	return (DDI_INTR_UNCLAIMED);
}

/*
 * drt_socket_card_id()
 *	figure out current status of card in socket
 *	this is used to prevent callbacks at card insertion
 */
/* ARGSUSED */
void
drt_socket_card_id(drt_dev_t *drt, drt_socket_t *socket, int status)
{

	/* need to record if a card is present to init state */
	if ((status & DRSTAT_CD_MASK) == DRSTAT_PRESENT_OK)
		socket->drt_flags |= DRT_CARD_PRESENT;

	/* check battery state to avoid callbacks */
	switch (status & DRSTAT_BVDST) {
	case DRSTAT_BATT_LOW:
		socket->drt_flags |= DRT_BATTERY_LOW;
		socket->drt_flags &= ~DRT_BATTERY_DEAD;
		socket->drt_state |= SBM_BVD2;
		socket->drt_state &= ~SBM_BVD1;
		break;
	case DRSTAT_BATT_OK:
		socket->drt_flags &= ~(DRT_BATTERY_LOW|DRT_BATTERY_DEAD);
		socket->drt_state &= ~(SBM_BVD1|SBM_BVD2);
		break;
	default:
				/* battery dead */
		socket->drt_flags |= DRT_BATTERY_DEAD;
		socket->drt_state |= SBM_BVD1;
		break;
	}

	/* check write protect status */
	if (status & DRSTAT_WPST)
		socket->drt_state |= SBM_WP;
	else
		socket->drt_state &= ~SBM_WP;

	/* and ready/busy */
	if (status & DRSTAT_RDYST)
		socket->drt_state |= SBM_RDYBSY;
	else
		socket->drt_state &= ~SBM_RDYBSY;
}

/*
 * drt_timestamp(drt)
 *	update the pm_timestamp value.
 *	we lock it to prevent two updates at the same time
 */
/* ARGSUSED */
static void
drt_timestamp(drt_dev_t *drt)
{

/*
 * XXX - Note that calling ddi_dev_is_needed on a suspend or resume
 *	will SOMETIMES cause the system to deadlock. For now, we
 *	don't call ddi_dev_is_needed ever and we don't update the
 *	timestamp.
 */
#ifdef	XXX
	dev_info_t *dip = drt->pc_devinfo;

	if (drt->pc_flags & PCF_SUSPENDED) {
		ddi_dev_is_needed(dip, 0, 1);
	}
	mutex_enter(&drt->pc_tslock);
	drv_getparm(TIME, &drt->pc_timestamp);
	(void) ddi_prop_modify(DDI_DEV_T_NONE, dip, 0,
				"pm_timestamp", (caddr_t)&drt->pc_timestamp,
				sizeof (u_long));
	mutex_exit(&drt->pc_tslock);
#endif
}

#if defined(DRT_DEBUG)
static void
drt_dmp_regs(stp4020_socket_csr_t *csrp)
{
	int i;

	printf("drt_dmp_regs (%x):\n", (int)csrp);
	printf("\tctl0: %b\n", csrp->ctl0,
		"\020\1IFTYPE\2SFTRST\3SPKREN\4IOILVL\5IOIE\6RSVD"
		"\7CTOIE\010WPIE\011RDYIE\012BVD1IE\013BVD2IE\014CDIE"
		"\015SCILVL\016PROMEN\017RSVDX");
	printf("\tctl1: %b\n", csrp->ctl1, "\020\1PCIFOE\1MSTPWR\7APWREN"
		"\10RSVD\11DIAGEN\12WAITDB\13WPDB\14RDYDB\15BVD1DB\16BVD2DB"
		"\17CD1DB\20LPBKEN");
	printf("\tstat0: %b\n", csrp->stat0, "\020\1PWRON\2WAITST\3WPST"
		"\4RDYST\5BVD1ST\6BVD2ST\7CD1ST\10CD2ST\11PCTO\12WPCHG"
		"\13RDCHG\14BVD1CHG\15BVD2CHG\16CDCHG\17SCINT\20IOINT");
	printf("\tstat1: types=%x, rev=%x\n", csrp->stat1 & DRSTAT_PCTYS_M,
		csrp->stat1 & DRSTAT_REV_M);
	for (i = 0; i < 3; i++) {
		printf("\twin%d:\tctl0: cmdlng=%x, cmddly=%x, "
			"aspsel=%x, base=%x\n", i,
			GET_DRWIN_CMDLNG(csrp->window[i].ctl0),
			GET_DRWIN_CMDDLY(csrp->window[i].ctl0),
			csrp->window[i].ctl0 & DRWIN_ASPSEL_M,
			GET_DRWIN_BASE(csrp->window[i].ctl0));
		printf("\t\tctl1: %x\n", csrp->window[i].ctl1);
	}
}

#endif

/*
 * drt_cpr - save/restore the adapter's hardware state
 */
static void
drt_cpr(drt_dev_t *drt, int cmd)
{
	int sn, wn;

	switch (cmd) {
	    case DRT_SAVE_HW_STATE:
		for (sn = 0; sn < DRSOCKETS; sn++) {
			stp4020_socket_csr_t *drs = &drt->pc_csr->socket[sn];
			for (wn = 0; wn < DRWINDOWS; wn++) {
				drt->saved_socket[sn].window[wn].ctl0 =
					drs->window[wn].ctl0;
				drt->saved_socket[sn].window[wn].ctl1 =
					drs->window[wn].ctl1;
			}
			drt->saved_socket[sn].ctl0 = drs->ctl0;
			drt->saved_socket[sn].ctl1 = drs->ctl1;
		}
		break;
	    case DRT_RESTORE_HW_STATE:
		for (sn = 0; sn < DRSOCKETS; sn++) {
			stp4020_socket_csr_t *drs = &drt->pc_csr->socket[sn];
			for (wn = 0; wn < DRWINDOWS; wn++) {
				drs->window[wn].ctl0 =
					drt->saved_socket[sn].window[wn].ctl0;
				drs->window[wn].ctl1 =
					drt->saved_socket[sn].window[wn].ctl1;
			}

			/* work around for false status bugs */
			/* XXX - why 0x3FFF and not 0xFFFF?? */
			drs->stat0 = 0x3FFF;
			drs->stat1 = 0x3FFF;

			drs->ctl0 = drt->saved_socket[sn].ctl0;
			drs->ctl1 = drt->saved_socket[sn].ctl1;
		}
		break;
	} /* switch */

}

/*
 * drt_fixprops(dip)
 *	if the adapter predates 1275 properties, add them.
 *	We do this by checking presence of the property
 *	and adding what we know if properties not present
 */
/* ARGSUSED */
static void
drt_fixprops(dev_info_t *dip)
{

	/*
	 * note that there are a number of properties that
	 * should be added here if not present
	 */

}
