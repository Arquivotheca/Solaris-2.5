/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)fhc.c 1.22	95/09/21 SMI"

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
#include <sys/intr.h>
#include <sys/intreg.h>
#include <sys/autoconf.h>
#include <sys/modctl.h>
#include <sys/spl.h>
#include <sys/machsystm.h>
#include <sys/machcpuvar.h>

#include <sys/fhc.h>

/* Useful debugging Stuff */
#include <sys/nexusdebug.h>

/*
 * This table represents the FHC interrupt priorities.  They range from
 * 1-15, and have been modeled after the sun4d interrupts. The mondo
 * number anded with 0x7 is used to index into this table. This was
 * done to save table space.
 */
static int fhc_int_priorities[] = {
	PIL_12,			/* System interrupt priority */
	PIL_12,			/* zs interrupt priority */
	PIL_12,			/* TOD interrupt priority */
	PIL_12			/* Fan Fail priority */
};

/*
 * The dont_calibrate variable is meant to be set to one in /etc/system
 * or by boot -h so that the calibration tables are not used. This
 * is useful for checking thermistors whose output seems to be incorrect.
 */
static int dont_calibrate = 0;

/*
 * The following tables correspond to the degress Celcius for each count
 * value possible from the 8-bit A/C convertors on each type of system
 * board for the UltraSPARC Server systems. To access a temperature,
 * just index into the correct table using the count from the A/D convertor
 * register, and that is the correct temperature in degress Celsius. These
 * values can be negative.
 */
static short cpu_table[] = {
-16,	-14,	-12,	-10,	-8,	-6,	-4,	-2,	/* 0-7 */
1,	4,	6,	8,	10,	12,	13,	15,	/* 8-15 */
16,	18,	19,	20,	22,	23,	24,	25,	/* 16-23 */
26,	27,	28,	29,	30,	31,	32,	33,	/* 24-31 */
34,	35,	35,	36,	37,	38,	39,	39,	/* 32-39 */
40,	41,	41,	42,	43,	44,	44,	45,	/* 40-47 */
46,	46,	47,	47,	48,	49,	49,	50,	/* 48-55 */
51,	51,	52,	53,	53,	54,	54,	55,	/* 56-63 */
55,	56,	56,	57,	57,	58,	58,	59,	/* 64-71 */
60,	60,	61,	61,	62,	62,	63,	63,	/* 72-79 */
64,	64,	65,	65,	66,	66,	67,	67,	/* 80-87 */
68,	68,	69,	69,	70,	70,	71,	71,	/* 88-95 */
72,	72,	73,	73,	74,	74,	75,	75,	/* 96-103 */
76,	76,	77,	77,	78,	78,	79,	79,	/* 104-111 */
80,	80,	81,	81,	82,	82,	83,	83,	/* 112-119 */
84,	84,	85,	85,	86,	86,	87,	87,	/* 120-127 */
88,	88,	89,	89,					/* 128-131 */
};

#define	CPU_MX_CNT	(sizeof (cpu_table)/sizeof (short))

static short io_table[] = {
0,	0,	0,	0,	0,	0,	0,	0,	/* 0-7 */
0,	0,	0,	0,	0,	0,	0,	0,	/* 8-15 */
0,	0,	0,	0,	0,	0,	0,	0,	/* 16-23 */
0,	0,	0,	0,	0,	0,	0,	0,	/* 24-31 */
0,	0,	0,	0,	0,	0,	0,	0,	/* 32-39 */
0,	3,	7,	10,	13,	15,	17,	19,	/* 40-47 */
21,	23,	25,	27,	28,	30,	31,	32,	/* 48-55 */
34,	35,	36,	37,	38,	39,	41,	42,	/* 56-63 */
43,	44,	45,	46,	46,	47,	48,	49,	/* 64-71 */
50,	51,	52,	53,	53,	54,	55,	56,	/* 72-79 */
57,	57,	58,	59,	60,	60,	61,	62,	/* 80-87 */
62,	63,	64,	64,	65,	66,	66,	67,	/* 88-95 */
68,	68,	69,	70,	70,	71,	72,	72,	/* 96-103 */
73,	73,	74,	75,	75,	76,	77,	77,	/* 104-111 */
78,	78,	79,	80,	80,	81,	81,	82,	/* 112-119 */
};

#define	IO_MN_CNT	40
#define	IO_MX_CNT	(sizeof (io_table)/sizeof (short))

static short clock_table[] = {
0,	0,	0,	0,	0,	0,	0,	0,	/* 0-7 */
0,	0,	0,	0,	1,	2,	4,	5,	/* 8-15 */
7,	8,	10,	11,	12,	13,	14,	15,	/* 16-23 */
17,	18,	19,	20,	21,	22,	23,	24,	/* 24-31 */
24,	25,	26,	27,	28,	29,	29,	30,	/* 32-39 */
31,	32,	32,	33,	34,	35,	35,	36,	/* 40-47 */
37,	38,	38,	39,	40,	40,	41,	42,	/* 48-55 */
42,	43,	44,	44,	45,	46,	46,	47,	/* 56-63 */
48,	48,	49,	50,	50,	51,	52,	52,	/* 64-71 */
53,	54,	54,	55,	56,	57,	57,	58,	/* 72-79 */
59,	59,	60,	60,	61,	62,	63,	63,	/* 80-87 */
64,	65,	65,	66,	67,	68,	68,	69,	/* 88-95 */
70,	70,	71,	72,	73,	74,	74,	75,	/* 96-103 */
76,	77,	78,	78,	79,	80,	81,	82,	/* 104-111 */
};

#define	CLK_MN_CNT	11
#define	CLK_MX_CNT	(sizeof (clock_table)/sizeof (short))

/*
 * poke faults
 * TODO - We need to remove this later once zs driver no longer uses
 * pokefaults. They do not work anyway on the fhc. It will never trap
 * due to a poke or peek.
 */
extern int pokefault;
static kmutex_t pokefault_mutex;

/*
 * Driver global board list mutex and list head pointer. The list is
 * protected by the mutex and contains a record of all known boards,
 * whether they are active and running in the kernel, boards disabled
 * by OBP, or boards hotplugged after the system has booted UNIX.
 */
static kmutex_t bdlist_mutex;
static struct bd_list *bd_list = NULL;

/*
 * Function prototypes
 */
static int fhc_ctlops(dev_info_t *, dev_info_t *, ddi_ctl_enum_t,
			void *, void *);

static ddi_intrspec_t fhc_get_intrspec(dev_info_t *dip,
					dev_info_t *rdip,
					u_int inumber);

static int fhc_add_intrspec(dev_info_t *dip, dev_info_t *rdip,
	ddi_intrspec_t intrspec, ddi_iblock_cookie_t *iblock_cookiep,
	ddi_idevice_cookie_t *idevice_cookiep,
	u_int (*int_handler)(caddr_t int_handler_arg),
	caddr_t int_handler_arg, int kind);

static void fhc_remove_intrspec(dev_info_t *dip, dev_info_t *rdip,
	ddi_intrspec_t intrspec, ddi_iblock_cookie_t iblock_cookiep);

static int fhc_identify(dev_info_t *devi);
static int fhc_attach(dev_info_t *devi, ddi_attach_cmd_t cmd);
static int fhc_detach(dev_info_t *devi, ddi_detach_cmd_t cmd);
static int fhc_init(struct fhc_soft_state *softsp);

static void fhc_uninit_child(dev_info_t *child);

#ifdef notdef
static int fhc_init_child(dev_info_t *, dev_info_t *);
static int fhc_make_ppd(dev_info_t *child);
#endif

static int fhc_ctl_xlate_intrs(dev_info_t *dip,
	dev_info_t *rdip, int *in,
	struct ddi_parent_private_data *pdptr);

static void fhc_add_kstats(struct fhc_soft_state *);
static int fhc_kstat_update(kstat_t *, int);

/*
 * board type and A/D convertor output passed in and real temperature
 * is returned.
 */
static short calibrate_temp(enum board_type, u_char);

/* Routine to determine if there are CPUs on this board. */
static int cpu_on_board(int);

extern struct cb_ops no_cb_ops;
extern struct cpu_node cpunodes[];

/*
 * Configuration data structures
 */
static struct bus_ops fhc_bus_ops = {
	BUSO_REV,
	ddi_bus_map,		/* map */
	fhc_get_intrspec,	/* get_intrspec */
	fhc_add_intrspec,	/* add_intrspec */
	fhc_remove_intrspec,	/* remove_intrspec */
	i_ddi_map_fault,	/* map_fault */
	ddi_no_dma_map,		/* dma_map */
	ddi_no_dma_allochdl,
	ddi_no_dma_freehdl,
	ddi_no_dma_bindhdl,
	ddi_no_dma_unbindhdl,
	ddi_no_dma_flush,
	ddi_no_dma_win,
	ddi_dma_mctl,		/* dma_ctl */
	fhc_ctlops,		/* ctl */
	ddi_bus_prop_op,	/* prop_op */
};

static struct dev_ops fhc_ops = {
	DEVO_REV,		/* rev */
	0,			/* refcnt  */
	ddi_no_info,		/* getinfo */
	fhc_identify,		/* identify */
	nulldev,		/* probe */
	fhc_attach,		/* attach */
	fhc_detach,		/* detach */
	nulldev,		/* reset */
	&no_cb_ops,		/* cb_ops */
	&fhc_bus_ops		/* bus_ops */
};

/*
 * Driver globals
 * TODO - We need to investigate what locking needs to be done here.
 */
void *fhcp;				/* fhc soft state hook */

extern struct mod_ops mod_driverops;

static struct modldrv modldrv = {
	&mod_driverops,		/* Type of module.  This one is a driver */
	"FHC Nexus",		/* Name of module. */
	&fhc_ops,		/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1,		/* rev */
	(void *)&modldrv,
	NULL
};

/*
 * These are the module initialization routines.
 */

int
_init(void)
{
	int error;

	if ((error = ddi_soft_state_init(&fhcp,
	    sizeof (struct fhc_soft_state), 1)) != 0)
		return (error);

	mutex_init(&pokefault_mutex, "pokefault lock",
		MUTEX_SPIN_DEFAULT, (void *)ipltospl(15));

	mutex_init(&bdlist_mutex, "Board list lock",
		MUTEX_DEFAULT, DEFAULT_WT);

	return (mod_install(&modlinkage));
}

int
_fini(void)
{
	int error;

	if ((error = mod_remove(&modlinkage)) != 0)
		return (error);

	mutex_destroy(&pokefault_mutex);
	mutex_destroy(&bdlist_mutex);

	ddi_soft_state_fini(&fhcp);
	return (0);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

static int
fhc_identify(dev_info_t *devi)
{
	char *name = ddi_get_name(devi);
	int rc = DDI_NOT_IDENTIFIED;

	if (strcmp(name, "fhc") == 0) {
		rc = DDI_IDENTIFIED;
	}

	return (rc);
}

static int
fhc_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	struct fhc_soft_state *softsp;
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

	if (ddi_soft_state_zalloc(fhcp, instance) != DDI_SUCCESS)
		return (DDI_FAILURE);

	softsp = ddi_get_soft_state(fhcp, instance);

	/* Set the dip in the soft state */
	softsp->dip = devi;

	DPRINTF(FHC_ATTACH_DEBUG, ("fhc: devi= 0x%x\n, softsp=0x%x\n",
		devi, softsp));

	if ((error = fhc_init(softsp)) != DDI_SUCCESS)
		goto bad;

	ddi_report_dev(devi);

	return (DDI_SUCCESS);

bad:
	ddi_soft_state_free(fhcp, instance);
	return (error);
}

/* ARGSUSED */
static int
fhc_detach(dev_info_t *devi, ddi_detach_cmd_t cmd)
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

static int
fhc_init(struct fhc_soft_state *softsp)
{
	int i;
	u_int tmp_reg;
	char namebuf[128];
	int board;

	/*
	 * XXX
	 * returning DDI_FAILURE without unmapping the registers can
	 * cause a kernel map leak. This should be fixed at some
	 * point in the future.
	 */

	/*
	 * Map in the FHC registers. Specifying length and offset of
	 * zero maps in the entire OBP register set.
	 */

	/* map in register set 0 */
	if (ddi_map_regs(softsp->dip, 0,
	    (caddr_t *)&softsp->id, 0, 0)) {
		cmn_err(CE_CONT, "?FHC%d: unable to map FHC internal "
			"registers\n", ddi_get_instance(softsp->dip));
		return (DDI_FAILURE);
	}

	/*
	 * Fill in the virtual addresses of the registers in the
	 * fhc_soft_state structure.
	 */
	softsp->rctrl = (u_int *)((char *)(softsp->id) +
		FHC_OFF_RCTRL);
	softsp->ctrl = (u_int *)((char *)(softsp->id) +
		FHC_OFF_CTRL);
	softsp->bsr = (u_int *)((char *)(softsp->id) +
		FHC_OFF_BSR);
	softsp->jtag_ctrl = (u_int *)((char *)(softsp->id) +
		FHC_OFF_JTAG_CTRL);
	softsp->jtag_cmd = (u_int *)((char *)(softsp->id) +
		FHC_OFF_JTAG_CMD);

	/* map in register set 1 */
	if (ddi_map_regs(softsp->dip, 1,
	    (caddr_t *)&softsp->igr, 0, 0)) {
		cmn_err(CE_CONT, "?FHC%d: unable to map FHC IGR "
			"register\n", ddi_get_instance(softsp->dip));
		return (DDI_FAILURE);
	}

	/* map in register set 2 */
	if (ddi_map_regs(softsp->dip, 2,
	    (caddr_t *)&softsp->intr_regs[FHC_FANFAIL_INO].mapping_reg,
	    0, 0)) {
		cmn_err(CE_CONT, "?FHC%d: unable to map FHC Fan Fail "
			"IMR register\n", ddi_get_instance(softsp->dip));
		return (DDI_FAILURE);
	}

	/* map in register set 3 */
	if (ddi_map_regs(softsp->dip, 3,
	    (caddr_t *)&softsp->intr_regs[FHC_SYS_INO].mapping_reg,
	    0, 0)) {
		cmn_err(CE_CONT, "?FHC%d: unable to map FHC System "
			"IMR register\n", ddi_get_instance(softsp->dip));
		return (DDI_FAILURE);
	}

	/* map in register set 4 */
	if (ddi_map_regs(softsp->dip, 4,
	    (caddr_t *)&softsp->intr_regs[FHC_UART_INO].mapping_reg,
	    0, 0)) {
		cmn_err(CE_CONT, "?FHC%d: unable to map FHC UART "
			"IMR register\n", ddi_get_instance(softsp->dip));
		return (DDI_FAILURE);
	}

	/* map in register set 5 */
	if (ddi_map_regs(softsp->dip, 5,
	    (caddr_t *)&softsp->intr_regs[FHC_TOD_INO].mapping_reg,
	    0, 0)) {
		cmn_err(CE_CONT, "?FHC%d: unable to map FHC TOD "
			"IMR register\n", ddi_get_instance(softsp->dip));
		return (DDI_FAILURE);
	}

	/* Loop over all intr sets and setup the VAs for the ISMR */
	/* TODO - Make sure we are calculating the ISMR correctly. */
	for (i = 0; i < FHC_MAX_INO; i++) {
		softsp->intr_regs[i].clear_reg =
			(u_int *)((char *)(softsp->intr_regs[i].mapping_reg) +
			FHC_OFF_ISMR);
		/* Now clear the state machines to idle */
		*(softsp->intr_regs[i].clear_reg) = ISM_IDLE;
	}

	/*
	 * It is OK to not have a board# property. This happens for
	 * the board which is a child of central. However this FHC
	 * still needs a proper Interrupt Group Number programmed
	 * into the Interrupt Group register, because the other
	 * instance of FHC, which is not under central, will properly
	 * program the IGR. The numbers from the two settings of the
	 * IGR need to be the same. One driver cannot wait for the
	 * other to program the IGR, because there is no guarantee
	 * which instance of FHC will get attached first.
	 */
	if ((board = (int) ddi_getprop(DDI_DEV_T_ANY, softsp->dip,
	    DDI_PROP_DONTPASS, "board#", -1)) == -1) {
		/*
		 * Now determine the board number by reading the
		 * hardware register.
		 */
		board = FHC_BSR_TO_BD(*(softsp->bsr));
		softsp->is_central = 1;
	}

	/* Add the board to the board list */
	if (determine_board_type(softsp, board) == DDI_FAILURE) {
		return (DDI_FAILURE);
	}

	/* Initialize the mutex guarding the poll_list. */
	(void) sprintf(namebuf, "fhc poll mutex softsp 0x%x", (int)softsp);
	mutex_init(&softsp->poll_list_lock, namebuf, MUTEX_DRIVER, NULL);

	/* Initialize the mutex guarding the FHC CSR */
	(void) sprintf(namebuf, "fhc csr mutex softsp 0x%x", (int)softsp);
	mutex_init(&softsp->ctrl_lock, namebuf, MUTEX_DRIVER, NULL);

	/* Initialize the poll_list to be empty */
	for (i = 0; i < MAX_ZS_CNT; i++) {
		softsp->poll_list[i].funcp = NULL;
	}

	/* Modify the various registers in the FHC now */

	/*
	 * We know this board to be present now, record that state and
	 * remove the NOT_BRD_PRES condition
	 */
	if (!(softsp->is_central)) {
		mutex_enter(&softsp->ctrl_lock);
		*(softsp->ctrl) |= FHC_NOT_BRD_PRES;
		tmp_reg = *(softsp->ctrl);
#ifdef lint
		tmp_reg = tmp_reg;
#endif
		/* XXX record the board state in global space */
		mutex_exit(&softsp->ctrl_lock);

		/* Add kstats for all non-central instances of the FHC. */
		fhc_add_kstats(softsp);
	}

	/*
	 * setup the IGR. Shift the board number over by one to get
	 * the UPA MID.
	 */
	*(softsp->igr) = (softsp->list->board) << 1;

	/* Now flush the hardware store buffers. */
	tmp_reg = *(softsp->id);
#ifdef lint
	tmp_reg = tmp_reg;
#endif

	return (DDI_SUCCESS);
}

static ddi_intrspec_t
fhc_get_intrspec(dev_info_t *dip, dev_info_t *rdip, u_int inumber)
{
	struct ddi_parent_private_data *ppdptr;

#ifdef	lint
	dip = dip;
#endif

	/*
	 * convert the parent private data pointer in the childs dev_info
	 * structure to a pointer to a sunddi_compat_hack structure
	 * to get at the interrupt specifications.
	 */
	ppdptr = (struct ddi_parent_private_data *)
		(DEVI(rdip))->devi_parent_data;

	/*
	 * validate the interrupt number.
	 */
	if (inumber >= ppdptr->par_nintr) {
		cmn_err(CE_WARN, "?fhc%d: Inumber 0x%x is out of range of "
			"par_nintr %d\n", ddi_get_instance(dip),
			inumber, ppdptr->par_nintr);
		return (NULL);
	}

	/*
	 * return the interrupt structure pointer.
	 */
	return ((ddi_intrspec_t)&ppdptr->par_intr[inumber]);
}

static u_int
fhc_intr_wrapper(caddr_t arg)
{
	u_int intr_return;
	u_int tmpreg;
	struct fhc_wrapper_arg *intr_info;

	tmpreg = ISM_IDLE;
	intr_info = (struct fhc_wrapper_arg *) arg;
	intr_return = (*intr_info->funcp)(intr_info->arg);

	/* Idle the state machine. */
	*(intr_info->clear_reg) = tmpreg;

	/* Flush the hardware store buffers. */
	tmpreg = *(intr_info->clear_reg);
#ifdef lint
	tmpreg = tmpreg;
#endif	/* lint */

	return (intr_return);
}

/*
 * fhc_zs_intr_wrapper
 *
 * This function handles intrerrupts where more than one device may interupt
 * the fhc with the same mondo.
 */

#define	MAX_INTR_CNT 10

static u_int
fhc_zs_intr_wrapper(caddr_t arg)
{
	struct fhc_soft_state *softsp = (struct fhc_soft_state *) arg;
	u_int (*funcp0)(caddr_t);
	u_int (*funcp1)(caddr_t);
	caddr_t arg0, arg1;
	u_int tmp_reg;
	u_int result = DDI_INTR_UNCLAIMED;
	volatile u_int *clear_reg;
	u_char *spurious_cntr = &softsp->spurious_zs_cntr;

	funcp0 = softsp->poll_list[0].funcp;
	funcp1 = softsp->poll_list[1].funcp;
	arg0 = softsp->poll_list[0].arg;
	arg1 = softsp->poll_list[1].arg;
	clear_reg = softsp->intr_regs[FHC_UART_INO].clear_reg;

	if (funcp0 != NULL) {
		if ((funcp0)(arg0) == DDI_INTR_CLAIMED) {
			result = DDI_INTR_CLAIMED;
		}
	}

	if (funcp1 != NULL) {
		if ((funcp1)(arg1) == DDI_INTR_CLAIMED) {
			result = DDI_INTR_CLAIMED;
		}
	}

	if (result == DDI_INTR_UNCLAIMED) {
		(*spurious_cntr)++;

		if (*spurious_cntr < MAX_INTR_CNT) {
			result = DDI_INTR_CLAIMED;
		} else {
			*spurious_cntr = (u_char) 0;
		}
	} else {
		*spurious_cntr = (u_char) 0;
	}

	/* Idle the state machine. */
	*(clear_reg) = ISM_IDLE;

	/* flush the store buffers. */
	tmp_reg = *(clear_reg);

#ifdef lint
	tmp_reg = tmp_reg;
#endif

	return (result);
}


/*
 * add_intrspec - Add an interrupt specification.
 */
static int
fhc_add_intrspec(
	dev_info_t *dip,
	dev_info_t *rdip,
	ddi_intrspec_t intrspec,
	ddi_iblock_cookie_t *iblock_cookiep,
	ddi_idevice_cookie_t *idevice_cookiep,
	u_int (*int_handler)(caddr_t int_handler_arg),
	caddr_t int_handler_arg,
	int kind)
{
	int mondo;
	struct fhc_wrapper_arg *fhc_arg;
	int hot;

	ASSERT(intrspec != 0);
	ASSERT(rdip != 0);
	ASSERT(ddi_get_driver(rdip) != 0);

	if (int_handler == NULL) {
		cmn_err(CE_WARN, "?fhc%d: Invalid interrupt handler\n",
			ddi_get_instance(dip));
		return (DDI_FAILURE);
	}
	switch (kind) {
	case IDDI_INTR_TYPE_NORMAL: {
		struct fhc_soft_state *softsp = (struct fhc_soft_state *)
		    ddi_get_soft_state(fhcp, ddi_get_instance(dip));
		struct fhcintrspec *ispec =
			(struct fhcintrspec *) intrspec;
		volatile u_int *mondo_vec_reg;
		u_int tmp_mondo_vec;
		u_int tmpreg; /* HW flush reg */
		struct dev_ops *dops = DEVI(rdip)->devi_ops;

		/* Is the child MT-hot? */
		if (dops->devo_bus_ops) {
			hot = 1;	/* Nexus drivers MUST be MT-safe */
		} else if (dops->devo_cb_ops->cb_flag & D_MP) {
			hot = 1;	/* Most leaves are MT-safe */
		} else {
			hot = 0;	/* MT-unsafe drivers ok (for now) */
		}

		/* get the mondo number */
		mondo = ispec->mondo;
		mondo_vec_reg = softsp->intr_regs[FHC_INO(mondo)].
			mapping_reg;

		/* Program the iblock cookie */
		if (iblock_cookiep) {
			*iblock_cookiep = (ddi_iblock_cookie_t)
				ipltospl(ispec->pil);
		}

		/* Program the device cookie */
		if (idevice_cookiep) {
			idevice_cookiep->idev_vector = 0;
			/*
			 * The idevice cookie contains the priority as
			 * understood by the device itself on the bus it
			 * lives on.  Let the nexi beneath sort out the
			 * translation (if any) that's needed.
			 */
			idevice_cookiep->idev_priority =
				(u_short) ispec->pil;
		}

		/*
		 * If the interrupt is for the zs chips, use the vector
		 * polling lists. Otherwise use a straight handler.
		 */
		if (FHC_INO(mondo) == FHC_UART_INO) {
			/* First lock the mutex for this poll_list */
			mutex_enter(&softsp->poll_list_lock);

			/*
			 * If polling list is empty, then install handler
			 * and enable interrupts for this mondo.
			 */
			if ((softsp->poll_list[0].funcp == NULL) &&
			    (softsp->poll_list[1].funcp == NULL)) {
				add_ivintr((u_int) ((softsp->list->board <<
				    BD_IVINTR_SHFT)|mondo),
				    (u_int) ispec->pil, fhc_zs_intr_wrapper,
				    softsp, (hot) ? NULL : &unsafe_driver);
			}

			/*
			 * Add this interrupt to the polling list.
			 */

			/* figure out where to add this item in the list */
			if (softsp->poll_list[0].funcp == NULL) {
				softsp->poll_list[0].arg = int_handler_arg;
				softsp->poll_list[0].funcp = int_handler;
			} else if (softsp->poll_list[1].funcp == NULL) {
				softsp->poll_list[1].arg = int_handler_arg;
				softsp->poll_list[1].funcp = int_handler;
			} else {	/* two elements already in list */
				cmn_err(CE_WARN,
					"?fhc%d: poll list overflow",
					ddi_get_instance(dip));
				mutex_exit(&softsp->poll_list_lock);
				return (DDI_FAILURE);
			}

			/*
			 * If both zs handlers are active, then this is the
			 * second add_intrspec called, so do not enable
			 * the IMR_VALID bit, it is already on.
			 */
			if ((softsp->poll_list[0].funcp != NULL) &&
			    (softsp->poll_list[1].funcp != NULL)) {
				/* now release the mutex and return */
				mutex_exit(&softsp->poll_list_lock);
				return (DDI_SUCCESS);
			} else {
				/* just release the nutex */
				mutex_exit(&softsp->poll_list_lock);
			}
		} else {	/* normal interrupt installation */
			/* Allocate a nexus interrupt data structure */
			fhc_arg = (struct fhc_wrapper_arg *) kmem_alloc(
				sizeof (struct fhc_wrapper_arg), KM_SLEEP);
			fhc_arg->child = ispec->child;
			fhc_arg->mapping_reg = mondo_vec_reg;
			fhc_arg->clear_reg =
				(softsp->intr_regs[FHC_INO(mondo)].clear_reg);
			fhc_arg->softsp = softsp;
			fhc_arg->funcp = int_handler;
			fhc_arg->arg = int_handler_arg;

			/*
			 * Save the fhc_arg in the ispec so we can use this info
			 * later to uninstall this interrupt spec.
			 */
			ispec->handler_arg = fhc_arg;
			add_ivintr((u_int) ((softsp->list->board <<
				BD_IVINTR_SHFT) | mondo),
				(u_int) ispec->pil, fhc_intr_wrapper,
				fhc_arg, (hot) ? NULL : &unsafe_driver);
		}

		/*
		 * 1206383
		 * Clear out a stale 'pending' or 'transmit' state in
		 * this device's ISM that might have been left from a
		 * previous session.
		 *
		 * Since all FHC interrupts are level interrupts, any
		 * real interrupting condition will immediately transition
		 * the ISM back to pending.
		 */
		*(softsp->intr_regs[FHC_INO(mondo)].clear_reg) = ISM_IDLE;

		/*
		 * Program the mondo vector accordingly.  This MUST be the
		 * last thing we do.  Once we program the mondo, the device
		 * may begin to interrupt.
		 * TODO - Need to come up with a strategy on how to pick
		 * a CPU to direct these interrupts to.
		 */
		tmp_mondo_vec = (u_int) getprocessorid() << INR_PID_SHIFT;

		/* don't do this for fan because fan has a special control */
		if (FHC_INO(mondo) != FHC_FANFAIL_INO)
			tmp_mondo_vec |= IMR_VALID;

		DPRINTF(FHC_INTERRUPT_DEBUG,
		    ("Mondo 0x%x mapping reg: 0x%x", mondo_vec_reg));

		/* Store it in the hardware reg. */
		*mondo_vec_reg = tmp_mondo_vec;

		/* Read a FHC register to flush store buffers */
		tmpreg = *(softsp->id);
#ifdef lint
		tmpreg = tmpreg;
#endif

		return (DDI_SUCCESS);
	}
	default:
		/*
		 * If we can't do it here, our parent can't either, so
		 * fail the request.
		 */
		cmn_err(CE_WARN, "?fhc%d: fhc_addintrspec() unknown request\n",
			ddi_get_instance(dip));
		return (DDI_INTR_NOTFOUND);
	}
}

/*
 * remove_intrspec - Remove an interrupt specification.
 */
static void
fhc_remove_intrspec(
	dev_info_t *dip,
	dev_info_t *rdip,
	ddi_intrspec_t intrspec,
	ddi_iblock_cookie_t iblock_cookiep)
{
	volatile u_int *mondo_vec_reg;
	volatile u_int tmpreg;
	int i;

	struct fhcintrspec *ispec = (struct fhcintrspec *) intrspec;
	struct fhc_wrapper_arg *arg = ispec->handler_arg;
	struct fhc_soft_state *softsp = (struct fhc_soft_state *)
		ddi_get_soft_state(fhcp, ddi_get_instance(dip));
	int mondo;

#ifdef lint
	rdip = rdip;
	iblock_cookiep = iblock_cookiep;
#endif
	mondo = ispec->mondo;


	if (FHC_INO(mondo) == FHC_UART_INO) {
		int intr_found = 0;

		/* Lock the poll_list first */
		mutex_enter(&softsp->poll_list_lock);

		/*
		 * Find which entry in the poll list belongs to this
		 * intrspec.
		 */
		for (i = 0; i < MAX_ZS_CNT; i++) {
			if (softsp->poll_list[i].funcp ==
			    ispec->handler_arg->funcp) {
				softsp->poll_list[i].funcp = NULL;
				intr_found++;
			}
		}

		/* If we did not find an entry, then we have a problem */
		if (!intr_found) {
			cmn_err(CE_WARN, "?fhc%d: Intrspec not found in"
				" poll list", ddi_get_instance(dip));
			mutex_exit(&softsp->poll_list_lock);
			return;
		}

		/*
		 * If we have removed all active entries for the poll
		 * list, then we have to disable interupts at this point.
		 */
		if ((softsp->poll_list[0].funcp == NULL) &&
		    (softsp->poll_list[1].funcp == NULL)) {
			*(softsp->intr_regs[FHC_UART_INO].mapping_reg) &=
				~IMR_VALID;
			/* flush the hardware buffers */
			tmpreg = *(softsp->ctrl);

			/* Eliminate the particular handler from the system. */
			rem_ivintr((softsp->list->board <<
				BD_IVINTR_SHFT) | mondo,
				(struct intr_vector *)NULL);
		}

		mutex_exit(&softsp->poll_list_lock);
	} else {
		mondo_vec_reg = arg->mapping_reg;

		/* Turn off the valid bit in the mapping register. */
		/* XXX what about FHC_FANFAIL owned imr? */
		*mondo_vec_reg &= ~IMR_VALID;

		/* flush the hardware store buffers */
		tmpreg = *(softsp->id);
#ifdef lint
		tmpreg = tmpreg;
#endif

		/* Eliminate the particular handler from the system. */
		rem_ivintr((softsp->list->board << BD_IVINTR_SHFT) |
			mondo, (struct intr_vector *)NULL);

		kmem_free(ispec->handler_arg, sizeof (struct fhc_wrapper_arg));
	}

	ispec->handler_arg = (struct fhc_wrapper_arg *) 0;
}

/*
 * fhc_control_fan_intr
 *
 * This little beauty back doors the fhc fanfail IMR to disable
 * and enable the interrupt.  We need this high level interrupt
 * routine because we must disable the interrupts from _within_
 * the interrupt handler itself.
 *
 * XXX ugly
 *
 * high level interrupt only -- protected by the caller
 */
void
fhc_control_fan_intr(dev_info_t *dip, enum fhc_fan_mode mode)
{
	struct fhc_soft_state *softsp = (struct fhc_soft_state *)
		ddi_get_soft_state(fhcp, ddi_get_instance(dip));
	volatile u_int *m;
	u_int r;
	u_int tmpreg;

	ASSERT(softsp);

	/* Get the current value */
	m = softsp->intr_regs[FHC_FANFAIL_INO].mapping_reg;
	r = *m;

	/* Change it */
	if (mode == FHC_FAN_INTR_ON)
		r |= IMR_VALID;
	else
		r &= ~IMR_VALID;

	/* Store the modified IMR */
	*m = r;

	/* flush the hardware buffers */
	tmpreg = *m;

#ifdef lint
	tmpreg = tmpreg;
#endif

}

#ifdef notdef

/*
 * fhc_init_child()
 *
 * This function is called from the driver control ops routine on a
 * DDI_CTLOPS_INITCHILD request.  It builds and sets the device's
 * parent private data area.
 *
 * Handles the following properties:
 *
 *	Property	value
 *	Name		type
 *
 *	reg		register spec
 *	registers	wildcard s/w sbus register spec (.conf file property)
 *	intr		old-form interrupt spec
 *	interrupts	new (bus-oriented) interrupt spec
 *	ranges		range spec
 */

static char *cantmerge = "Cannot merge %s devinfo node %s@%s";

#define	get_prop(di, pname, flag, pval, plen)   \
	(ddi_prop_op(DDI_DEV_T_NONE, di, PROP_LEN_AND_VAL_ALLOC, \
	flag | DDI_PROP_DONTPASS | DDI_PROP_CANSLEEP, \
	pname, (caddr_t)pval, plen))

static int
fhc_init_child(dev_info_t *dip, dev_info_t *child)
{
	int rv, has_registers;
	char name[MAXNAMELEN];
	extern int impl_ddi_merge_child(dev_info_t *child);
	extern int impl_ddi_merge_wildcard(dev_info_t *child);

	/*
	 * Fill in parent-private data and note an indication if the
	 * "registers" property was used to fill in the data.
	 */
	has_registers = fhc_make_ppd(child);

	/*
	 * If this is a s/w node defined with the "registers" property,
	 * this means that this is a wildcard specifier, whose properties
	 * get applied to all previously defined h/w nodes with the same
	 * name and same parent.
	 *
	 * TODO - This branch should never be taken, and we are panicing
	 * for now if we take it.
	 */
	if ((has_registers) && (ddi_get_nodeid(child) == DEVI_PSEUDO_NODEID)) {
		cmn_err(CE_PANIC, "?fhc%d: fhc_init_child: software node "
			"found", ddi_get_instance(dip));
	}

	/*
	 * Force the name property to be generated from the "reg" property...
	 * (versus the "registers" property, so we always match the obp
	 * (versus the "registers" property, so we always match the obp
	 */

	name[0] = '\0';
	if ((has_registers) && (ddi_get_nodeid(child) != DEVI_PSEUDO_NODEID)) {
		int *reg_prop, reg_len;

		if (get_prop(child, OBP_REG, 0, &reg_prop, &reg_len) ==
		    DDI_SUCCESS)  {

			struct regspec *rp = (struct regspec *)reg_prop;

			sprintf(name, "%x,%x", rp->regspec_bustype,
				rp->regspec_addr);

			kmem_free(reg_prop, reg_len);
		}
	} else if (sparc_pd_getnreg(child) > 0) {
		sprintf(name, "%x,%x",
		    (u_int)sparc_pd_getreg(child, 0)->regspec_bustype,
		    (u_int)sparc_pd_getreg(child, 0)->regspec_addr);
	}

	ddi_set_name_addr(child, name);

	/*
	 * If a pseudo node, attempt to merge it into a hw node,
	 * if merged, returns an indication that this node should
	 * be removed (after the caller uninitializes it).
	 */
	if ((rv = impl_ddi_merge_child(child)) != DDI_SUCCESS)
		return (rv);

	return (DDI_SUCCESS);
}

#endif /* notdef */

static void
fhc_uninit_child(dev_info_t *child)
{
	struct ddi_parent_private_data *pdptr;
	int n;

	if ((pdptr = (struct ddi_parent_private_data *)
	    ddi_get_parent_data(child)) != NULL)  {
		if ((n = (size_t)pdptr->par_nintr) != 0)
			kmem_free(pdptr->par_intr, n *
				sizeof (struct fhcintrspec));

		if ((n = (size_t)pdptr->par_nrng) != 0)
			kmem_free(pdptr->par_rng, n *
				sizeof (struct rangespec));

		if ((n = pdptr->par_nreg) != 0)
			kmem_free(pdptr->par_reg, n * sizeof (struct regspec));

		kmem_free(pdptr, sizeof (*pdptr));
		ddi_set_parent_data(child, NULL);
	}
	ddi_set_name_addr(child, NULL);
	/*
	 * Strip the node to properly convert it back to prototype form
	 */
	ddi_remove_minor_node(child, NULL);
}

#ifdef notdef

struct prop_ispec {
	u_int	pri, vec;
};

static int
fhc_make_ppd(dev_info_t *child)
{
	struct fhc_parent_private_data *pdptr;
	register int n;
	int *reg_prop, *rgstr_prop, *rng_prop, *intr_prop, *irupts_prop;
	int reg_len, rgstr_len, rng_len, intr_len, irupts_len;
	int has_registers = 0;

	pdptr = (struct fhc_parent_private_data *)
			kmem_zalloc(sizeof (*pdptr), KM_SLEEP);

	/* We set the data here, so that it can be used during the call */
	ddi_set_parent_data(child, (caddr_t)pdptr);

	/*
	 * Handle the 'reg'/'registers' properties.
	 * "registers" overrides "reg", but requires that "reg" be exported,
	 * so we can handle wildcard specifiers. "registers" implies that
	 * we insert the correct value in the regspec_bustype field of
	 * each spec for a real (non-pseudo) device node.  "registers"
	 * is a s/w only property, so we inhibit the prom search for this
	 * property.
	 */

	if (get_prop(child, OBP_REG, 0, &reg_prop, &reg_len) != DDI_SUCCESS)
		reg_len = 0;

	rgstr_len = 0;
	get_prop(child, "registers", DDI_PROP_NOTPROM, &rgstr_prop,
		&rgstr_len);

	if (rgstr_len != 0)  {

		if ((ddi_get_nodeid(child) != DEVI_PSEUDO_NODEID) &&
		    (reg_len != 0))  {

			/*
			 * Convert wildcard "registers" for a real node...
			 * (Else, this is the wildcard prototype node)
			 */

			/*
			 * TODO - Do we really need to do this? On the
			 * FHC, there is no concept of a slot. So do
			 * we really need a regspec_bustype value set?
			 */

			struct regspec *rp = (struct regspec *)reg_prop;
			u_int slot = rp->regspec_bustype;
			int i;

			rp = (struct regspec *)rgstr_prop;
			n = rgstr_len / sizeof (struct regspec);
			for (i = 0; i < n; ++i, ++rp)
				rp->regspec_bustype = slot;
		}

		if (reg_len != 0)
			kmem_free(reg_prop, reg_len);

		reg_prop = rgstr_prop;
		reg_len = rgstr_len;
		++has_registers;
	}
	if ((n = reg_len) != 0)  {
		pdptr->par_nreg = n / (int) sizeof (struct regspec);
		pdptr->par_reg = (struct regspec *)reg_prop;
	}

	/*
	 * See if I have ranges.
	 */
	if (get_prop(child, OBP_RANGES, 0, &rng_prop, &rng_len) ==
	    DDI_SUCCESS) {
		pdptr->par_nrng = rng_len / (int)(sizeof (struct rangespec));
		pdptr->par_rng = (struct rangespec *)rng_prop;
	}

	/*
	 * Handle the 'intr' and 'interrupts' properties
	 */

	/*
	 * First look for the 'intr' property for the device.
	 * TODO - Since we only have 'new' devices hanging off of the
	 * FHC, this is probably not needed. Need to check with
	 * DMK.
	 */
	if (get_prop(child, OBP_INTR, 0, &intr_prop, &intr_len) !=
	    DDI_SUCCESS) {
		intr_len = 0;
	}

	/*
	 * We need to support the generalized 'interrupts' property.
	 */
	if (get_prop(child, OBP_INTERRUPTS, 0, &irupts_prop, &irupts_len)
	    != DDI_SUCCESS) {
		irupts_len = 0;
	} else if (intr_len != 0) {
		/*
		 * If both 'intr' and 'interrupts' are defined,
		 * then 'interrupts' wins and we toss the 'intr' away.
		 */
		kmem_free(intr_prop, intr_len);
		intr_len = 0;
	}

	if (intr_len != 0) {

		/*
		 * IEEE 1275 firmware should always give us "interrupts"
		 * if "intr" exists -- either created by the driver or
		 * by a magic property (intr). Thus, this code shouldn't
		 * be necessary. (Early pre-fcs proms don't do this.)
		 *
		 * On Fusion machines, the PROM will give us an intr property
		 * for those old devices that don't support interrupts,
		 * however, the intr property will still be the bus level
		 * interrupt. Convert it to "interrupts" format ...
		 *
		 * TODO - FHC supports a limited set of children. We do
		 * not want to be too constrictive in case a later rev.
		 * of a system board introduces some new devices that
		 * parent to the FHC driver. But we do not want to try
		 * to support everything that the sbus driver does.
		 */

		int *new;
		struct prop_ispec *l;

		n = intr_len / sizeof (struct prop_ispec);
		irupts_len = sizeof (int) * n;
		l = (struct prop_ispec *) intr_prop;
		new = irupts_prop =
			(int *) kmem_zalloc((size_t)irupts_len, KM_SLEEP);

		while (n--) {
			*new = l->pri;
			new++;
			l++;
		}
		kmem_free(intr_prop, intr_len);
		/* Intentionally fall through to "interrupts" code */
	}

	if ((n = irupts_len) != 0) {
		size_t size;
		int *out;

		/*
		 * Translate the 'interrupts' property into an array
		 * of intrspecs for the rest of the DDI framework to
		 * toy with.  Only our ancestors really know how to
		 * do this, so ask 'em.  We massage the 'interrupts'
		 * property so that it is pre-pended by a count of
		 * the number of integers in the argument.
		 */
		size = sizeof (int) + n;
		out = kmem_alloc(size, KM_SLEEP);
		*out = n / sizeof (int);
		bcopy((caddr_t)irupts_prop, (caddr_t)(out + 1), (size_t) n);
		kmem_free(irupts_prop, irupts_len);
		if (ddi_ctlops(child, child, DDI_CTLOPS_XLATE_INTRS,
		    out, pdptr) != DDI_SUCCESS) {
			cmn_err(CE_CONT, "?fhc: Unable to translate "
				"'interrupts' for %s%d\n",
				DEVI(child)->devi_name,
				DEVI(child)->devi_instance);
		}
		kmem_free(out, size);
	}
	return (has_registers);
}

#endif	/* notdef */

/*
 * FHC Control Ops routine
 *
 * Requests handled here:
 *	DDI_CTLOPS_INITCHILD	see impl_ddi_sunbus_initchild() for details
 *	DDI_CTLOPS_UNINITCHILD	see fhc_uninit_child() for details
 *	DDI_CTLOPS_XLATE_INTRS	see fhc_ctl_xlate_intrs() for details
 *	DDI_CTLOPS_REPORTDEV	TODO - need to implement this.
 *	DDI_CTLOPS_INTR_HILEVEL	TODO - need to implement this.
 *	DDI_CTLOPS_POKE_INIT	TODO - need to remove this support later
 *	DDI_CTLOPS_POKE_FLUSH	TODO - need to remove this support later
 *	DDI_CTLOPS_POKE_FINI	TODO - need to remove this support later
 */
static int
fhc_ctlops(dev_info_t *dip, dev_info_t *rdip,
	ddi_ctl_enum_t op, void *arg, void *result)
{
	switch (op) {
	case DDI_CTLOPS_INITCHILD:
		DPRINTF(FHC_CTLOPS_DEBUG, ("DDI_CTLOPS_INITCHILD\n"));
		return (impl_ddi_sunbus_initchild((dev_info_t *)arg));

	case DDI_CTLOPS_UNINITCHILD:
		DPRINTF(FHC_CTLOPS_DEBUG, ("DDI_CTLOPS_UNINITCHILD\n"));
		fhc_uninit_child((dev_info_t *)arg);
		return (DDI_SUCCESS);

	case DDI_CTLOPS_XLATE_INTRS:
		DPRINTF(FHC_CTLOPS_DEBUG, ("DDI_CTLOPS_XLATE_INTRS\n"));
		return (fhc_ctl_xlate_intrs(dip, rdip, arg, result));

	case DDI_CTLOPS_REPORTDEV:
		/*
		 * TODO - Figure out what makes sense to report here.
		 */
		return (DDI_SUCCESS);

	case DDI_CTLOPS_INTR_HILEVEL:
		/*
		 * Indicate whether the interrupt specified is to be handled
		 * above lock level.  In other words, above the level that
		 * cv_signal and default type mutexes can be used.
		 *
		 * TODO - We should call a kernel specific routine to
		 * determine LOCK_LEVEL.
		 */
		*(int *) result =
			(((struct fhcintrspec *)arg)->pil > LOCK_LEVEL);
		return (DDI_SUCCESS);

		/*
		 * TODO - If we remove the  ddi_poke() from the zs_probe()
		 * routine, should we remove poke handling support from
		 * the fhc driver? (york)
		 */
	case DDI_CTLOPS_POKE_INIT:
		mutex_enter(&pokefault_mutex);
		pokefault = -1;
		return (DDI_SUCCESS);

	case DDI_CTLOPS_POKE_FLUSH:

		/*
		 * TODO - Figure out which AC to check to see that we have
		 * not issued a write to a non-replying UPA address. Then
		 * read this AC's error register and checkl if a fault
		 * occurred. On Sunfire hardware, this kind of write
		 * fails silently, and is only recorded in the AC's
		 * error register.
		 */
		return (pokefault == 1 ? DDI_FAILURE : DDI_SUCCESS);

	case DDI_CTLOPS_POKE_FINI:
		pokefault = 0;
		mutex_exit(&pokefault_mutex);
		return (DDI_SUCCESS);

	default:
		return (ddi_ctlops(dip, rdip, op, arg, result));
	}
}


/*
 * We're prepared to claim that the interrupt string is in
 * the form of a list of <FHCintr> specifications, or we're dealing
 * with on-board devices and we have an interrupt_number property which
 * gives us our mondo number.
 * Translate the mondos into fhcintrspecs.
 */
/* ARGSUSED */
static int
fhc_ctl_xlate_intrs(dev_info_t *dip, dev_info_t *rdip, int *in,
	struct ddi_parent_private_data *pdptr)
{
	register int n;
	register size_t size;
	register struct fhcintrspec *new;

	/*
	 * The list consists of <mondo interrupt level> elements
	 */
	if ((n = *in++) < 1)
		return (DDI_FAILURE);

	pdptr->par_nintr = n;
	size = n * sizeof (struct fhcintrspec);
	new = kmem_zalloc(size, KM_SLEEP);
	pdptr->par_intr = (struct intrspec *) new;

	while (n--) {
		int level = *in++, mondo;

		/*
		 * Create the FHC mondo number. Devices will have
		 * an "interrupts" property, that is equal to the mondo number.
		 */
		mondo = level;

		/* Sanity check the mondos range */
		if (FHC_INO(mondo) >= FHC_MAX_INO) {
			cmn_err(CE_WARN, "?fhc%d: Mondo vector 0x%x out "
				"of range",
				ddi_get_instance(dip), mondo);
			goto broken;
		}

		new->mondo = mondo;
		new->pil = fhc_int_priorities[FHC_INO(mondo)];
		new->child = rdip;
		DPRINTF(FHC_INTERRUPT_DEBUG, ("Interrupt info for device %s"
		    "Mondo: 0x%x, Pil: 0x%x, 0x%x\n",
		    ddi_get_name(rdip), new->mondo, new->pil));
		new++;
	}

	return (DDI_SUCCESS);
	/*NOTREACHED*/

broken:
	cmn_err(CE_WARN, "?fhc%d: fhc_ctl_xlate_intrs() failed\n",
		ddi_get_instance(dip));
	kmem_free(pdptr->par_intr, size);
	pdptr->par_intr = (void *)0;
	pdptr->par_nintr = 0;
	return (DDI_FAILURE);
}

/*
 * This function initializes the temperature arrays for use. All
 * temperatures are set in to invalid value to start.
 */
void
init_temp_arrays(struct temp_stats *envstat)
{
	int i;

	envstat->index = 0;

	for (i = 0; i < L1_SZ; i++) {
		envstat->l1[i] = NA_TEMP;
	}

	for (i = 0; i < L2_SZ; i++) {
		envstat->l2[i] = NA_TEMP;
	}

	for (i = 0; i < L3_SZ; i++) {
		envstat->l3[i] = NA_TEMP;
	}

	for (i = 0; i < L4_SZ; i++) {
		envstat->l4[i] = NA_TEMP;
	}

	for (i = 0; i < L5_SZ; i++) {
		envstat->l5[i] = NA_TEMP;
	}

	envstat->max = NA_TEMP;
	envstat->min = NA_TEMP;
}

/*
 * This function manages the temperature history in the temperature
 * statistics buffer passed in. It calls the temperature calibration
 * routines and maintains the time averaged temperature data.
 */
void
update_temp(dev_info_t pdip, struct temp_stats *envstat, u_char value)
{
	u_int index;		/* The absolute temperature counter */
	u_int tmp_index;	/* temp index into upper level array */
	int count;		/* Count of non-zero values in array */
	int total;		/* sum total of non-zero values in array */
	short real_temp;	/* calibrated temperature */
	int i;
	struct fhc_soft_state *softsp;

	/* determine soft state pointer of parent */
	softsp = ddi_get_soft_state(fhcp, ddi_get_instance(pdip));

	envstat->index++;
	index = envstat->index;

	/*
	 * You need to update the level 5 intervals first, since
	 * they are based on the data from the level 4 intervals,
	 * and so on, down to the level 1 intervals.
	 */

	/* update the level 5 intervals if it is time */
	if (((tmp_index = L5_INDEX(index)) > 0) && (L5_REM(index) == 0)) {
		/* Generate the index within the level 5 array */
		tmp_index -= 1;		/* decrement by 1 for indexing */
		tmp_index = tmp_index % L5_SZ;

		/* take an average of the level 4 array */
		for (i = 0, count = 0, total = 0; i < L4_SZ; i++) {
			/* Do not include zero values in average */
			if (envstat->l4[i] != NA_TEMP) {
				total += (int) envstat->l4[i];
				count++;
			}
		}

		/*
		 * If there were any level 4 data points to average,
		 * do so.
		 */
		if (count != 0) {
			envstat->l5[tmp_index] = total/count;
		} else {
			envstat->l5[tmp_index] = NA_TEMP;
		}
	}

	/* update the level 4 intervals if it is time */
	if (((tmp_index = L4_INDEX(index)) > 0) && (L4_REM(index) == 0)) {
		/* Generate the index within the level 4 array */
		tmp_index -= 1;		/* decrement by 1 for indexing */
		tmp_index = tmp_index % L4_SZ;

		/* take an average of the level 3 array */
		for (i = 0, count = 0, total = 0; i < L3_SZ; i++) {
			/* Do not include zero values in average */
			if (envstat->l3[i] != NA_TEMP) {
				total += (int) envstat->l3[i];
				count++;
			}
		}

		/*
		 * If there were any level 3 data points to average,
		 * do so.
		 */
		if (count != 0) {
			envstat->l4[tmp_index] = total/count;
		} else {
			envstat->l4[tmp_index] = NA_TEMP;
		}
	}

	/* update the level 3 intervals if it is time */
	if (((tmp_index = L3_INDEX(index)) > 0) && (L3_REM(index) == 0)) {
		/* Generate the index within the level 3 array */
		tmp_index -= 1;		/* decrement by 1 for indexing */
		tmp_index = tmp_index % L3_SZ;

		/* take an average of the level 2 array */
		for (i = 0, count = 0, total = 0; i < L2_SZ; i++) {
			/* Do not include zero values in average */
			if (envstat->l2[i] != NA_TEMP) {
				total += (int) envstat->l2[i];
				count++;
			}
		}

		/*
		 * If there were any level 2 data points to average,
		 * do so.
		 */
		if (count != 0) {
			envstat->l3[tmp_index] = total/count;
		} else {
			envstat->l3[tmp_index] = NA_TEMP;
		}
	}

	/* update the level 2 intervals if it is time */
	if (((tmp_index = L2_INDEX(index)) > 0) && (L2_REM(index) == 0)) {
		/* Generate the index within the level 2 array */
		tmp_index -= 1;		/* decrement by 1 for indexing */
		tmp_index = tmp_index % L2_SZ;

		/* take an average of the level 1 array */
		for (i = 0, count = 0, total = 0; i < L1_SZ; i++) {
			/* Do not include zero values in average */
			if (envstat->l1[i] != NA_TEMP) {
				total += (int) envstat->l1[i];
				count++;
			}
		}

		/*
		 * If there were any level 1 data points to average,
		 * do so.
		 */
		if (count != 0) {
			envstat->l2[tmp_index] = total/count;
		} else {
			envstat->l2[tmp_index] = NA_TEMP;
		}
	}

	/* Run the calibration function using this board type */
	real_temp = calibrate_temp(softsp->list->type, value);

	envstat->l1[index % L1_SZ] = real_temp;

	/* update the maximum and minimum temperatures if necessary */
	if ((envstat->max == NA_TEMP) || (real_temp > envstat->max)) {
		envstat->max = real_temp;
	}

	if ((envstat->min == NA_TEMP) || (real_temp < envstat->min)) {
		envstat->min = real_temp;
	}
}

int
overtemp_kstat_update(kstat_t *ksp, int rw)
{
	struct temp_stats *tempstat;
	char *kstatp;
	int i;

	kstatp = (char *) ksp->ks_data;
	tempstat = (struct temp_stats *) ksp->ks_private;

	/*
	 * Kstat reads are used to retrieve the current system temperature
	 * history. Kstat writes are used to reset the max and min
	 * temperatures.
	 */
	if (rw == KSTAT_WRITE) {
		short max;	/* temporary copy of max temperature */
		short min;	/* temporary copy of min temperature */

		/*
		 * search for and reset the max and min to the current
		 * array contents. Old max and min values will get
		 * averaged out as they move into the higher level arrays.
		 */
		max = tempstat->l1[0];
		min = tempstat->l1[0];

		/* Pull the max and min from Level 1 array */
		for (i = 0; i < L1_SZ; i++) {
			if ((tempstat->l1[i] != NA_TEMP) &&
			    (tempstat->l1[i] > max)) {
				max = tempstat->l1[i];
			}

			if ((tempstat->l1[i] != NA_TEMP) &&
			    (tempstat->l1[i] < min)) {
				min = tempstat->l1[i];
			}
		}

		/* Pull the max and min from Level 2 array */
		for (i = 0; i < L2_SZ; i++) {
			if ((tempstat->l2[i] != NA_TEMP) &&
			    (tempstat->l2[i] > max)) {
				max = tempstat->l2[i];
			}

			if ((tempstat->l2[i] != NA_TEMP) &&
			    (tempstat->l2[i] < min)) {
				min = tempstat->l2[i];
			}
		}

		/* Pull the max and min from Level 3 array */
		for (i = 0; i < L3_SZ; i++) {
			if ((tempstat->l3[i] != NA_TEMP) &&
			    (tempstat->l3[i] > max)) {
				max = tempstat->l3[i];
			}

			if ((tempstat->l3[i] != NA_TEMP) &&
			    (tempstat->l3[i] < min)) {
				min = tempstat->l3[i];
			}
		}

		/* Pull the max and min from Level 4 array */
		for (i = 0; i < L4_SZ; i++) {
			if ((tempstat->l4[i] != NA_TEMP) &&
			    (tempstat->l4[i] > max)) {
				max = tempstat->l4[i];
			}

			if ((tempstat->l4[i] != NA_TEMP) &&
			    (tempstat->l4[i] < min)) {
				min = tempstat->l4[i];
			}
		}

		/* Pull the max and min from Level 5 array */
		for (i = 0; i < L5_SZ; i++) {
			if ((tempstat->l5[i] != NA_TEMP) &&
			    (tempstat->l5[i] > max)) {
				max = tempstat->l5[i];
			}

			if ((tempstat->l5[i] != NA_TEMP) &&
			    (tempstat->l5[i] < min)) {
				min = tempstat->l5[i];
			}
		}
	} else {
		/*
		 * copy the temperature history buffer into the
		 * kstat structure.
		 */
		bcopy((caddr_t) tempstat, kstatp, sizeof (struct temp_stats));
	}
	return (0);
}

/*
 * This function uses the calibration tables at the beginning of this file
 * to lookup the actual temperature of the thermistor in degrees Celcius.
 * If the measurement is out of the bounds of the acceptable values, the
 * closest boundary value is used instead.
 */
static short
calibrate_temp(enum board_type type, u_char temp)
{
	short result = NA_TEMP;

	if (dont_calibrate == 1) {
		return ((short) temp);
	}

	switch (type) {
	case CPU_BOARD:
		if (temp >= CPU_MX_CNT) {
			result = cpu_table[CPU_MX_CNT-1];
		} else {
			result = cpu_table[temp];
		}
		break;

	case IO_2SBUS_BOARD:
	case IO_SBUS_FFB_BOARD:
	case IO_PCI_BOARD:
		if (temp < IO_MN_CNT) {
			result = io_table[IO_MN_CNT];
		} else if (temp >= IO_MX_CNT) {
			result = io_table[IO_MX_CNT-1];
		} else {
			result = io_table[temp];
		}
		break;

	case CLOCK_BOARD:
		if (temp < CLK_MN_CNT) {
			result = clock_table[CLK_MN_CNT];
		} else if (temp >= CLK_MX_CNT) {
			result = clock_table[CLK_MX_CNT-1];
		} else {
			result = clock_table[temp];
		}
		break;

	default:
		cmn_err(CE_NOTE, "calibrate_temp: Incorrect board type");
		break;
	}

	return (result);
}

static void
fhc_add_kstats(struct fhc_soft_state *softsp)
{
	struct kstat *fhc_ksp;
	struct fhc_kstat *fhc_named_ksp;

	if ((fhc_ksp = kstat_create("unix", softsp->list->board,
	    FHC_KSTAT_NAME, "misc", KSTAT_TYPE_NAMED,
	    sizeof (struct fhc_kstat) / sizeof (kstat_named_t),
	    KSTAT_FLAG_PERSISTENT)) == NULL) {
		cmn_err(CE_WARN, "?fhc%d kstat_create failed",
			ddi_get_instance(softsp->dip));
		return;
	}

	fhc_named_ksp = (struct fhc_kstat *)(fhc_ksp->ks_data);

	/* initialize the named kstats */
	kstat_named_init(&fhc_named_ksp->csr,
		CSR_KSTAT_NAMED,
		KSTAT_DATA_ULONG);

	kstat_named_init(&fhc_named_ksp->bsr,
		BSR_KSTAT_NAMED,
		KSTAT_DATA_ULONG);

	fhc_ksp->ks_update = fhc_kstat_update;
	fhc_ksp->ks_private = (void *)softsp;
	kstat_install(fhc_ksp);
}

static int
fhc_kstat_update(kstat_t *ksp, int rw)
{
	struct fhc_kstat *fhcksp;
	struct fhc_soft_state *softsp;

	fhcksp = (struct fhc_kstat *) ksp->ks_data;
	softsp = (struct fhc_soft_state *) ksp->ks_private;

	/* this is a read-only kstat. Bail out on a write */
	if (rw == KSTAT_WRITE) {
		return (EACCES);
	} else {
		/*
		 * copy the current state of the hardware into the
		 * kstat structure.
		 */
		fhcksp->csr.value.ul = *softsp->ctrl;
		fhcksp->bsr.value.ul = *softsp->bsr;
	}
	return (0);
}

static int
cpu_on_board(int board)
{
	int upa_a = board << 1;
	int upa_b = (board << 1) + 1;

	if ((cpunodes[upa_a].nodeid != NULL) ||
	    (cpunodes[upa_b].nodeid != NULL)) {
		return (1);
	} else {
		return (0);
	}
}

/*
 * This function allocates a board list element and links it into the
 * driver global linked list of board elements. If a valid softsp is
 * passed in as an argument, then use it. This function always assumes
 * that the board /slot number passed in is correct.
 */
int
determine_board_type(struct fhc_soft_state *softsp, int board)
{
	struct bd_list *list;	/* temporary list pointer */

	/*
	 * Allocate a new board list element and link it into
	 * the list. All changes to the list must be locked with
	 * the global driver mutex.
	 */
	if ((list = (struct bd_list *) kmem_zalloc(sizeof (struct bd_list),
	    KM_SLEEP)) == NULL) {
		cmn_err(CE_CONT, "fhc: unable to allocate "
			"board %d list structure\n", board);
		return (DDI_FAILURE);
	}

	/* Check the softsp before using it. */
	if (softsp != NULL) {
		/* setup the links between list and softsp */
		list->softsp = softsp;
		softsp->list = list;

		/* XXX - The following assumption might need to be fixed. */

		/* If we have a softsp, the board is in the active state */
		list->state = ACTIVE_STATE;

		/*
		 * XXX - now determine the board type. The data transfer
		 * size is being used to determine the difference between
		 * IO board and CPU/Memory boards. This might need to be
		 * changed later.
		 */
		if (softsp->is_central == 1) {
			list->type = CLOCK_BOARD;
		} else if (cpu_on_board(board)) {
			list->type = CPU_BOARD;
		} else if ((*(softsp->bsr) & FHC_UPADATA64A) ||
		    (*(softsp->bsr) & FHC_UPADATA64B)) {
			list->type = IO_2SBUS_BOARD;
		} else {
			list->type = MEM_BOARD;
		}

	} else {
		/* XXX - no softsp, so this is a hotplug board */
		list->state = UNKNOWN_STATE;
	}

	/* Fill in all the standard list elements now. */
	list->board = board;

	/* now add the new element to the head of the list */
	mutex_enter(&bdlist_mutex);
	list->next = bd_list;
	bd_list = list;
	mutex_exit(&bdlist_mutex);

	return (DDI_SUCCESS);
}

/*
 * This function searches the board list database and returns a pointer
 * to the selected structure if it is found or NULL if it isn't.
 * The database is _always_ left in a locked state so that a subsequent
 * update can occur atomically.
 */
struct bd_list *
get_and_lock_bdlist(int board)
{
	ASSERT(!mutex_owned(&bdlist_mutex));
	mutex_enter(&bdlist_mutex);
	return (get_bdlist(board));
}

/*
 * Search for the bd_list entry for the specified board.
 */
struct bd_list *
get_bdlist(int board)
{
	struct bd_list *list = bd_list;

	ASSERT(mutex_owned(&bdlist_mutex));
	while (list != NULL) {
		if (list->board == board) {
			break;
		}
		list = list->next;
	}
	return (list);
}

/* unlock the database */
void
unlock_bdlist()
{
	ASSERT(mutex_owned(&bdlist_mutex));
	mutex_exit(&bdlist_mutex);
}

/*
 * return the type of a board based on its board number
 */
enum board_type
get_board_type(int board)
{
	struct bd_list *list;
	enum board_type type = -1;

	if ((list = get_and_lock_bdlist(board)) != NULL) {
		type = list->type;
	}
	unlock_bdlist();

	return (type);
}
