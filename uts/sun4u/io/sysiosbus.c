/*
 * Copyright (c) 1990-1995 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)sysiosbus.c 1.48	95/10/03 SMI"

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
#include <sys/spl.h>

#include <sys/iommu.h>
#include <sys/sysiosbus.h>
#include <sys/sysioerr.h>
#include <sys/iocache.h>

/* Useful debugging Stuff */
#include <sys/nexusdebug.h>
/* Bitfield debugging definitions for this file */
#define	SBUS_ATTACH_DEBUG	0x1
#define	SBUS_SBUSMEM_DEBUG	0x2
#define	SBUS_INTERRUPT_DEBUG	0x4
#define	SBUS_REGISTERS_DEBUG	0x8

/*
 * Interrupt registers table.
 * This table is necessary due to inconsistencies in the sysio register
 * layout.  If this gets fixed in the chip, we can get rid of this stupid
 * table.
 */
static struct sbus_slot_entry ino_1 = {SBUS_SLOT0_CONFIG, SBUS_SLOT0_MAPREG,
				    SBUS_SLOT0_L1_CLEAR, NULL};
static struct sbus_slot_entry ino_2 = {SBUS_SLOT0_CONFIG, SBUS_SLOT0_MAPREG,
				    SBUS_SLOT0_L2_CLEAR, NULL};
static struct sbus_slot_entry ino_3 = {SBUS_SLOT0_CONFIG, SBUS_SLOT0_MAPREG,
				    SBUS_SLOT0_L3_CLEAR, NULL};
static struct sbus_slot_entry ino_4 = {SBUS_SLOT0_CONFIG, SBUS_SLOT0_MAPREG,
				    SBUS_SLOT0_L4_CLEAR, NULL};
static struct sbus_slot_entry ino_5 = {SBUS_SLOT0_CONFIG, SBUS_SLOT0_MAPREG,
				    SBUS_SLOT0_L5_CLEAR, NULL};
static struct sbus_slot_entry ino_6 = {SBUS_SLOT0_CONFIG, SBUS_SLOT0_MAPREG,
				    SBUS_SLOT0_L6_CLEAR, NULL};
static struct sbus_slot_entry ino_7 = {SBUS_SLOT0_CONFIG, SBUS_SLOT0_MAPREG,
				    SBUS_SLOT0_L7_CLEAR, NULL};
static struct sbus_slot_entry ino_9 = {SBUS_SLOT1_CONFIG, SBUS_SLOT1_MAPREG,
				    SBUS_SLOT1_L1_CLEAR, NULL};
static struct sbus_slot_entry ino_10 = {SBUS_SLOT1_CONFIG, SBUS_SLOT1_MAPREG,
				    SBUS_SLOT1_L2_CLEAR, NULL};
static struct sbus_slot_entry ino_11 = {SBUS_SLOT1_CONFIG, SBUS_SLOT1_MAPREG,
				    SBUS_SLOT1_L3_CLEAR, NULL};
static struct sbus_slot_entry ino_12 = {SBUS_SLOT1_CONFIG, SBUS_SLOT1_MAPREG,
				    SBUS_SLOT1_L4_CLEAR, NULL};
static struct sbus_slot_entry ino_13 = {SBUS_SLOT1_CONFIG, SBUS_SLOT1_MAPREG,
				    SBUS_SLOT1_L5_CLEAR, NULL};
static struct sbus_slot_entry ino_14 = {SBUS_SLOT1_CONFIG, SBUS_SLOT1_MAPREG,
				    SBUS_SLOT1_L6_CLEAR, NULL};
static struct sbus_slot_entry ino_15 = {SBUS_SLOT1_CONFIG, SBUS_SLOT1_MAPREG,
				    SBUS_SLOT1_L7_CLEAR, NULL};
static struct sbus_slot_entry ino_17 = {SBUS_SLOT2_CONFIG, SBUS_SLOT2_MAPREG,
				    SBUS_SLOT2_L1_CLEAR, NULL};
static struct sbus_slot_entry ino_18 = {SBUS_SLOT2_CONFIG, SBUS_SLOT2_MAPREG,
				    SBUS_SLOT2_L2_CLEAR, NULL};
static struct sbus_slot_entry ino_19 = {SBUS_SLOT2_CONFIG, SBUS_SLOT2_MAPREG,
				    SBUS_SLOT2_L3_CLEAR, NULL};
static struct sbus_slot_entry ino_20 = {SBUS_SLOT2_CONFIG, SBUS_SLOT2_MAPREG,
				    SBUS_SLOT2_L4_CLEAR, NULL};
static struct sbus_slot_entry ino_21 = {SBUS_SLOT2_CONFIG, SBUS_SLOT2_MAPREG,
				    SBUS_SLOT2_L5_CLEAR, NULL};
static struct sbus_slot_entry ino_22 = {SBUS_SLOT2_CONFIG, SBUS_SLOT2_MAPREG,
				    SBUS_SLOT2_L6_CLEAR, NULL};
static struct sbus_slot_entry ino_23 = {SBUS_SLOT2_CONFIG, SBUS_SLOT2_MAPREG,
				    SBUS_SLOT2_L7_CLEAR, NULL};
static struct sbus_slot_entry ino_25 = {SBUS_SLOT3_CONFIG, SBUS_SLOT3_MAPREG,
				    SBUS_SLOT3_L1_CLEAR, NULL};
static struct sbus_slot_entry ino_26 = {SBUS_SLOT3_CONFIG, SBUS_SLOT3_MAPREG,
				    SBUS_SLOT3_L2_CLEAR, NULL};
static struct sbus_slot_entry ino_27 = {SBUS_SLOT3_CONFIG, SBUS_SLOT3_MAPREG,
				    SBUS_SLOT3_L3_CLEAR, NULL};
static struct sbus_slot_entry ino_28 = {SBUS_SLOT3_CONFIG, SBUS_SLOT3_MAPREG,
				    SBUS_SLOT3_L4_CLEAR, NULL};
static struct sbus_slot_entry ino_29 = {SBUS_SLOT3_CONFIG, SBUS_SLOT3_MAPREG,
				    SBUS_SLOT3_L5_CLEAR, NULL};
static struct sbus_slot_entry ino_30 = {SBUS_SLOT3_CONFIG, SBUS_SLOT3_MAPREG,
				    SBUS_SLOT3_L6_CLEAR, NULL};
static struct sbus_slot_entry ino_31 = {SBUS_SLOT3_CONFIG, SBUS_SLOT3_MAPREG,
				    SBUS_SLOT3_L7_CLEAR, NULL};
static struct sbus_slot_entry ino_32 = {SBUS_SLOT5_CONFIG, ESP_MAPREG,
					ESP_CLEAR, NULL};
static struct sbus_slot_entry ino_33 = {SBUS_SLOT5_CONFIG, ETHER_MAPREG,
					ETHER_CLEAR, NULL};
static struct sbus_slot_entry ino_34 = {SBUS_SLOT5_CONFIG, PP_MAPREG,
					PP_CLEAR, NULL};
static struct sbus_slot_entry ino_36 = {SBUS_SLOT4_CONFIG, AUDIO_MAPREG,
					AUDIO_CLEAR, NULL};
static struct sbus_slot_entry ino_40 = {SBUS_SLOT6_CONFIG, KBDMOUSE_MAPREG,
					KBDMOUSE_CLEAR,
					KBDMOUSE_INTR_STATE_SHIFT};
static struct sbus_slot_entry ino_41 = {SBUS_SLOT6_CONFIG, FLOPPY_MAPREG,
					FLOPPY_CLEAR, NULL};
static struct sbus_slot_entry ino_42 = {SBUS_SLOT6_CONFIG, UE_ECC_MAPREG,
					UE_ECC_CLEAR, NULL};
static struct sbus_slot_entry ino_43 = {SBUS_SLOT6_CONFIG, CE_ECC_MAPREG,
					CE_ECC_CLEAR, NULL};
static struct sbus_slot_entry ino_44 = {SBUS_SLOT6_CONFIG, SBUS_ERR_MAPREG,
					SBUS_ERR_CLEAR, NULL};
static struct sbus_slot_entry ino_45 = {SBUS_SLOT6_CONFIG, PM_WAKEUP_MAPREG,
					PM_WAKEUP_CLEAR, NULL};
static struct sbus_slot_entry ino_46 = {SBUS_SLOT6_CONFIG, FFB_MAPPING_REG,
					NULL, NULL};
static struct sbus_slot_entry ino_47 = {SBUS_SLOT6_CONFIG, THERMAL_MAPREG,
					THERMAL_CLEAR, NULL};

/* Construct the interrupt number array */
struct sbus_slot_entry *ino_table[] = {
	NULL, &ino_1, &ino_2, &ino_3, &ino_4, &ino_5, &ino_6, &ino_7,
	NULL, &ino_9, &ino_10, &ino_11, &ino_12, &ino_13, &ino_14, &ino_15,
	NULL, &ino_17, &ino_18, &ino_19, &ino_20, &ino_21, &ino_22, &ino_23,
	NULL, &ino_25, &ino_26, &ino_27, &ino_28, &ino_29, &ino_30, &ino_31,
	&ino_32, &ino_33, &ino_34, NULL, &ino_36, NULL, NULL, NULL,
	&ino_40, &ino_41, &ino_42, &ino_43, &ino_44, &ino_45, &ino_46, &ino_47};

/*
 * This table represents the Fusion interrupt priorities.  They range
 * from 1 - 15, so we'll pattern the priorities after the 4M.  We map Fusion
 * interrupt number to system priority.  The mondo number is used as an
 * index into this table.
 */
int interrupt_priorities[] = {
	-1, 2, 3, 5, 7, 9, 11, 13,	/* Slot 0 sbus level 1 - 7 */
	-1, 2, 3, 5, 7, 9, 11, 13,	/* Slot 1 sbus level 1 - 7 */
	-1, 2, 3, 5, 7, 9, 11, 13,	/* Slot 2 sbus level 1 - 7 */
	-1, 2, 3, 5, 7, 9, 11, 13,	/* Slot 3 sbus level 1 - 7 */
	4,				/* Onboard SCSI */
	6,				/* Onboard Ethernet */
	3,				/* Onboard Parallel port */
	-1,				/* Not in use */
	9,				/* Onboard Audio */
	-1, -1, -1,			/* Not in use */
	12,				/* Onboard keyboard/serial ports */
	11,				/* Onboard Floppy */
	15,				/* Sysio UE ECC error */
	10,				/* Sysio CE ECC error */
	10};				/* Sysio Sbus error */

/*
 * Hack to handle poke faults
 */
extern int pokefault;
static kmutex_t pokefault_mutex;

/* Interrupt counter flag.  To enable/disable spurious interrupt counter. */
static int intr_cntr_on;

/*
 * Function prototypes.
 */
static int
sbus_ctlops(dev_info_t *, dev_info_t *, ddi_ctl_enum_t, void *, void *);

static ddi_intrspec_t
sbus_get_intrspec(dev_info_t *dip, dev_info_t *rdip, u_int inumber);

static int
sbus_add_intrspec(dev_info_t *dip, dev_info_t *rdip,
	ddi_intrspec_t intrspec, ddi_iblock_cookie_t *iblock_cookiep,
	ddi_idevice_cookie_t *idevice_cookiep,
	u_int (*int_handler)(caddr_t int_handler_arg),
	caddr_t int_handler_arg, int kind);

static void
sbus_remove_intrspec(dev_info_t *dip, dev_info_t *rdip,
	ddi_intrspec_t intrspec, ddi_iblock_cookie_t iblock_cookiep);

static int
sbus_identify(dev_info_t *devi);

static int
sbus_attach(dev_info_t *devi, ddi_attach_cmd_t cmd);

static int
sbus_detach(dev_info_t *devi, ddi_detach_cmd_t cmd);

static int
find_sbus_slot(dev_info_t *dip, dev_info_t *rdip);

static int
make_sbus_ppd(dev_info_t *child);

static int
sbusmem_initchild(dev_info_t *dip, dev_info_t *child);

static int
sbus_initchild(dev_info_t *dip, dev_info_t *child);

static int
sbus_uninitchild(dev_info_t *dip);

static int
sbus_init(struct sbus_soft_state *softsp, int address);

static int
sbus_resume_init(struct sbus_soft_state *softsp, int resume);

static void
sbus_cpr_handle_intr_map_reg(u_ll_t *cpr_softsp, u_ll_t *baddr, int flag);

/*
 * Configuration data structures
 */
static struct bus_ops sbus_bus_ops = {
	BUSO_REV,
	i_ddi_bus_map,
	sbus_get_intrspec,
	sbus_add_intrspec,
	sbus_remove_intrspec,
	i_ddi_map_fault,
	iommu_dma_map,
	iommu_dma_allochdl,
	iommu_dma_freehdl,
	iommu_dma_bindhdl,
	iommu_dma_unbindhdl,
	iommu_dma_flush,
	iommu_dma_win,
	iommu_dma_mctl,
	sbus_ctlops,
	ddi_bus_prop_op
};

static struct dev_ops sbus_ops = {
	DEVO_REV,		/* devo_rev, */
	0,			/* refcnt  */
	ddi_no_info,		/* info */
	sbus_identify,		/* identify */
	0,			/* probe */
	sbus_attach,		/* attach */
	sbus_detach,		/* detach */
	nodev,			/* reset */
	(struct cb_ops *)0,	/* driver operations */
	&sbus_bus_ops		/* bus operations */
};

void *sbusp;		/* sbus soft state hook */
void *sbus_cprp;	/* subs suspend/resume soft state hook */

#include <sys/modctl.h>
extern struct mod_ops mod_driverops;

static struct modldrv modldrv = {
	&mod_driverops, 	/* Type of module.  This one is a driver */
	"SBus (sysio) nexus driver",	/* Name of module. */
	&sbus_ops,		/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modldrv, NULL
};

/*
 * These are the module initialization routines.
 */
int
_init(void)
{
	int error;

	if ((error = ddi_soft_state_init(&sbusp,
	    sizeof (struct sbus_soft_state), 1)) != 0)
		return (error);

	/*
	 * Initialize cpr soft state structure
	 */
	if ((error = ddi_soft_state_init(&sbus_cprp,
	    sizeof (u_ll_t) * MAX_INO_TABLE_SIZE, 0)) != 0)
		return (error);

	return (mod_install(&modlinkage));
}

int
_fini(void)
{
	int error;

	if ((error = mod_remove(&modlinkage)) != 0)
		return (error);

	ddi_soft_state_fini(&sbusp);
	ddi_soft_state_fini(&sbus_cprp);
	return (0);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

static int
sbus_identify(dev_info_t *devi)
{
	char *name = ddi_get_name(devi);
	int rc = DDI_NOT_IDENTIFIED;

	if (strcmp(name, "sbus") == 0) {
		rc = DDI_IDENTIFIED;
	}

	return (rc);
}

/*ARGSUSED*/
static int
sbus_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	struct sbus_soft_state *softsp;
	int instance, error;
	int address;
	u_ll_t *cpr_softsp;

#ifdef	DEBUG
	debug_info = 1;
	debug_print_level = 0;
#endif

	instance = ddi_get_instance(devi);

	switch (cmd) {
	case DDI_ATTACH:
		break;

	case DDI_RESUME:
		softsp = ddi_get_soft_state(sbusp, instance);

		if ((error = iommu_resume_init(softsp)) != DDI_SUCCESS)
			return (error);

		if ((error = sbus_resume_init(softsp, 1)) != DDI_SUCCESS)
			return (error);

		if ((error = sysio_err_resume_init(softsp)) != DDI_SUCCESS)
			return (error);

		if ((error = stream_buf_resume_init(softsp)) != DDI_SUCCESS)
			return (error);

		/*
		 * Restore Interrupt Mapping registers
		 */
		cpr_softsp = ddi_get_soft_state(sbus_cprp, instance);

		if (cpr_softsp != NULL) {
			sbus_cpr_handle_intr_map_reg(cpr_softsp,
				softsp->intr_mapping_reg, 0);
			ddi_soft_state_free(sbus_cprp, instance);
		}

		return (DDI_SUCCESS);

	default:
		return (DDI_FAILURE);
	}

	if (ddi_soft_state_zalloc(sbusp, instance) != DDI_SUCCESS)
		return (DDI_FAILURE);

	softsp = ddi_get_soft_state(sbusp, instance);

	/* Set the dip in the soft state */
	softsp->dip = devi;

	if ((softsp->upa_id = (int) ddi_getprop(DDI_DEV_T_ANY, softsp->dip,
	    DDI_PROP_DONTPASS, "upa-portid", -1)) == -1) {
		cmn_err(CE_WARN, "Unable to retrieve sbus upa-portid"
		    "property.");
		error = DDI_FAILURE;
		goto bad;
	}

	/*
	 * The firmware maps in all 3 pages of the sysio chips device
	 * device registers and exports the mapping in the int-sized
	 * property "address".  Read in this address and pass it to
	 * the subsidiary *_init functions, so we don't create extra
	 * mappings to the same physical pages and we don't have to
	 * retrieve the more than once.
	 */

	address = ddi_getprop(DDI_DEV_T_ANY, softsp->dip,
	    DDI_PROP_DONTPASS | DDI_PROP_CANSLEEP, "address", -1);

	if (address == -1) {
		cmn_err(CE_CONT, "?sbus%d: No sysio <address> property\n",
		    ddi_get_instance(softsp->dip));
		return (DDI_FAILURE);
	}

	DPRINTF(SBUS_ATTACH_DEBUG, ("sbus: devi=0x%x, softsp=0x%x\n",
	    devi, softsp));

#ifdef	notdef
	/*
	 * This bit of code, plus the firmware, will tell us if
	 * the #size-cells infrastructure code works, to some degree.
	 * You should be able to use the firmware to determine if
	 * the address returned by ddi_map_regs maps the correct phys. pages.
	 */

	{
		caddr_t addr;
		int rv;

		cmn_err(CE_CONT, "?sbus: address property = 0x%x\n", address);

		if ((rv = ddi_map_regs(softsp->dip, 0, &addr,
		    (off_t)0, (off_t)0)) != DDI_SUCCESS)  {
			cmn_err(CE_CONT, "?sbus: ddi_map_regs failed: %d\n",
			    rv);
		} else {
			cmn_err(CE_CONT, "?sbus: ddi_map_regs returned "
			    " virtual address 0x%x\n", addr);
		}
	}
#endif	notdef

	/* Init the pokefault mutex for sbus devices */
	mutex_init(&pokefault_mutex, "pokefault lock",
	    MUTEX_SPIN_DEFAULT, (void *)ipltospl(SBUS_ERR_PIL - 1));

	if ((error = iommu_init(softsp, address)) != DDI_SUCCESS)
		goto bad;

	if ((error = sbus_init(softsp, address)) != DDI_SUCCESS)
		goto bad;

	if ((error = sysio_err_init(softsp, address)) != DDI_SUCCESS)
		goto bad;

	if ((error = stream_buf_init(softsp, address)) != DDI_SUCCESS)
		goto bad;

	ddi_report_dev(devi);

	return (DDI_SUCCESS);

bad:
	ddi_soft_state_free(sbusp, instance);
	return (error);
}

/* ARGSUSED */
static int
sbus_detach(dev_info_t *devi, ddi_detach_cmd_t cmd)
{
	int instance;
	struct sbus_soft_state *softsp;
	u_ll_t *cpr_softsp;

	switch (cmd) {
	case DDI_SUSPEND:
		/*
		 * Allocate the cpr  soft data structure to save the current
		 * state of the interrupt mapping registers.
		 * This structure will be deallocated after the system
		 * is resumed.
		 */
		instance = ddi_get_instance(devi);

		if (ddi_soft_state_zalloc(sbus_cprp, instance)
			!= DDI_SUCCESS)
			return (DDI_FAILURE);

		cpr_softsp = ddi_get_soft_state(sbus_cprp, instance);

		softsp = ddi_get_soft_state(sbusp, instance);

		sbus_cpr_handle_intr_map_reg(cpr_softsp,
			softsp->intr_mapping_reg, 1);
		return (DDI_SUCCESS);

	case DDI_DETACH:
	default:
		return (DDI_FAILURE);
	}
}

static int
sbus_init(struct sbus_soft_state *softsp, int address)
{
	int i;
	extern void set_intr_mapping_reg(int, u_ll_t *, int);

	/*
	 * Simply add each registers offset to the base address
	 * to calculate the already mapped virtual address of
	 * the device register...
	 *
	 * define a macro for the pointer arithmetic; all registers
	 * are 64 bits wide and are defined as u_ll_t's.
	 */

#define	REG_ADDR(b, o)	(u_ll_t *)(unsigned)((unsigned)(b) + (unsigned)(o))

	softsp->sysio_ctrl_reg = REG_ADDR(address, OFF_SYSIO_CTRL_REG);
	softsp->sbus_ctrl_reg = REG_ADDR(address, OFF_SBUS_CTRL_REG);
	softsp->sbus_slot_config_reg = REG_ADDR(address, OFF_SBUS_SLOT_CONFIG);
	softsp->intr_mapping_reg = REG_ADDR(address, OFF_INTR_MAPPING_REG);
	softsp->clr_intr_reg = REG_ADDR(address, OFF_CLR_INTR_REG);
	softsp->intr_retry_reg = REG_ADDR(address, OFF_INTR_RETRY_REG);
	softsp->sbus_intr_state = REG_ADDR(address, OFF_SBUS_INTR_STATE_REG);

#undef	REG_ADDR

	DPRINTF(SBUS_REGISTERS_DEBUG, ("SYSIO Control reg: 0x%x\n"
	    "SBUS Control reg: 0x%x", softsp->sysio_ctrl_reg,
	    softsp->sbus_ctrl_reg));

	/* Diag reg 2 is the next 64 bit word after diag reg 1 */
	softsp->obio_intr_state = softsp->sbus_intr_state + 1;

	(void) sbus_resume_init(softsp, 0);

	/*
	 * Set the initial burstsizes for each slot to all 1's.  This will
	 * get changed at initchild time.
	 */
	for (i = 0; i < MAX_SBUS_SLOTS; i++)
/*LINTED constant truncated by assignment */
		softsp->sbus_slave_burstsizes[i] = 0xffffffff;
	/*
	 * Since SYSIO is used as an interrupt mastering device for slave
	 * only UPA devices, we call a dedicated kernel function to register
	 * The address of the interrupt mapping register for the slave device.
	 */
	set_intr_mapping_reg(softsp->upa_id,
	    softsp->intr_mapping_reg + FFB_MAPPING_REG, 1);

	/* Turn on spurious interrupt counter if we're not a DEBUG kernel. */
#ifndef DEBUG
	intr_cntr_on = 1;
#else
	intr_cntr_on = 0;
#endif


	return (DDI_SUCCESS);
}

/*
 * This procedure is part of sbus initialization. It is called by
 * sbus_init() and is invoked when the system is being resumed.
 */
static int
sbus_resume_init(struct sbus_soft_state *softsp, int resume)
{
	int i;
	u_int sbus_burst_sizes;

	/*
	 * This shouldn't be needed when we have a real OBP PROM.
	 * (RAZ) Get rid of this later!!!
	 */
	*softsp->sysio_ctrl_reg |= (u_ll_t) softsp->upa_id << 51;

	/*
	 * Set appropriate fields of SYSIO control register.
	 * Set the interrupt group number
	 */
	*softsp->sysio_ctrl_reg |= (u_ll_t) softsp->upa_id << SYSIO_IGN;

	/*
	 * Set appropriate fields of sbus control register.
	 * Set DVMA arbitration enable for all devices.
	 */
	*softsp->sbus_ctrl_reg |= SBUS_ARBIT_ALL;

	/* Calculate our burstsizes now so we don't have to do it later */
	sbus_burst_sizes = (SYSIO64_BURST_RANGE << SYSIO64_BURST_SHIFT)
		| SYSIO_BURST_RANGE;

	sbus_burst_sizes = ddi_getprop(DDI_DEV_T_ANY, softsp->dip,
		DDI_PROP_DONTPASS, "up-burst-sizes", sbus_burst_sizes);

	softsp->sbus_burst_sizes = sbus_burst_sizes & SYSIO_BURST_MASK;
	softsp->sbus64_burst_sizes = sbus_burst_sizes & SYSIO64_BURST_MASK;

	if (!resume) {
		/* Set burstsizes to smallest value */
		for (i = 0; i < MAX_SBUS_SLOTS; i++) {
			volatile u_longlong_t *config;
			u_ll_t tmpreg;

			config = softsp->sbus_slot_config_reg + i;

			/* Write out the burst size */
			tmpreg = (u_ll_t) 0;
			*config = tmpreg;

			/* Flush any write buffers */
			tmpreg = *softsp->sbus_ctrl_reg;

			DPRINTF(SBUS_REGISTERS_DEBUG, ("Sbus slot 0x%x slot "
			    "configuration reg: 0x%x", (i > 3) ? i + 9 : i,
			    config));
		}
	} else {
		/* Program the slot configuration registers */
		for (i = 0; i < MAX_SBUS_SLOTS; i++) {
			volatile u_longlong_t *config;
#ifndef lint
			u_ll_t tmpreg;
#endif /* !lint */
			u_int slave_burstsizes;

			slave_burstsizes = 0;
			if (softsp->sbus_slave_burstsizes[i] !=
			    0xffffffff) {
				config = softsp->sbus_slot_config_reg + i;

				if (softsp->sbus_slave_burstsizes[i] &
				    SYSIO64_BURST_MASK) {
					/* get the 64 bit burstsizes */
					slave_burstsizes =
					    softsp->sbus_slave_burstsizes[i]
					    >> SYSIO64_BURST_SHIFT;

					/* Turn on 64 bit PIO's on the sbus */
					*config |= SBUS_ETM;
				} else {
					slave_burstsizes =
					    softsp->sbus_slave_burstsizes[i] &
					    SYSIO_BURST_MASK;
				}

				/* Get burstsizes into sysio register format */
				slave_burstsizes >>= SYSIO_SLAVEBURST_REGSHIFT;

				/* Program the burstsizes */
				*config |= (u_ll_t) slave_burstsizes;

				/* Flush any write buffers */
#ifndef lint
				tmpreg = *softsp->sbus_ctrl_reg;
#endif /* !lint */
			}
		}
	}

	return (DDI_SUCCESS);
}

#define	get_prop(di, pname, flag, pval, plen)	\
	(ddi_prop_op(DDI_DEV_T_NONE, di, PROP_LEN_AND_VAL_ALLOC, \
	flag | DDI_PROP_DONTPASS | DDI_PROP_CANSLEEP, \
	pname, (caddr_t)pval, plen))

struct prop_ispec {
	u_int	pri, vec;
};

/*
 * Create a sysio_parent_private_data structure from the ddi properties of
 * the dev_info node.
 *
 * The "reg" and either an "intr" or "interrupts" properties are required
 * if the driver wishes to create mappings or field interrupts on behalf
 * of the device.
 *
 * The "reg" property is assumed to be a list of at least one triple
 *
 *	<bustype, address, size>*1
 *
 * On pre-fusion machines, the "intr" property was the IPL for the system.
 * Most new sbus devices post an "interrupts" property that corresponds to
 * a particular bus level.  All devices on fusion using an "intr" property
 * will have it's contents translated into a bus level.  Hence, "intr" and
 * "interrupts on the fusion platform can be treated the same.
 *
 * The "interrupts" property is assumed to be a list of at least one
 * n-tuples that describes the interrupt capabilities of the bus the device
 * is connected to.  For SBus, this looks like
 *
 *	<SBus-level>*1
 *
 * (This property obsoletes the 'intr' property).
 *
 * The OBP_RANGES property is optional.
 */
static int
make_sbus_ppd(dev_info_t *child)
{
	register struct sysio_parent_private_data *pdptr;
	register int n;
	int *reg_prop, *rgstr_prop, *rng_prop, *intr_prop, *irupts_prop;
	int reg_len, rgstr_len, rng_len, intr_len, irupts_len;
	int has_registers = 0;

	pdptr = (struct sysio_parent_private_data *)
			kmem_zalloc(sizeof (*pdptr), KM_SLEEP);
	ddi_set_parent_data(child, (caddr_t)pdptr);

	/*
	 * Handle the 'reg'/'registers' properties.
	 * "registers" overrides "reg", but requires that "reg" be exported,
	 * so we can handle wildcard specifiers.  "registers" implies an
	 * sbus style device.  "registers" implies that we insert the
	 * correct value in the regspec_bustype field of each spec for a real
	 * (non-pseudo) device node.  "registers" is a s/w only property, so
	 * we inhibit the prom search for this property.
	 */
	if (get_prop(child, OBP_REG, 0, &reg_prop, &reg_len) != DDI_SUCCESS)
		reg_len = 0;

	/*
	 * Save the underlying slot number and slot offset.
	 * Among other things, we use these to name the child node.
	 */
	pdptr->slot = (u_int)-1;
	if (reg_len != 0) {
		pdptr->slot = ((struct regspec *)reg_prop)->regspec_bustype;
		pdptr->offset = ((struct regspec *)reg_prop)->regspec_addr;
	}

	rgstr_len = 0;
	get_prop(child, "registers", DDI_PROP_NOTPROM, &rgstr_prop, &rgstr_len);

	if (rgstr_len != 0)  {

		if ((ddi_get_nodeid(child) != DEVI_PSEUDO_NODEID) &&
		    (reg_len != 0))  {

			/*
			 * Convert wildcard "registers" for a real node...
			 * (Else, this is the wildcard prototype node)
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
	 *
	 * For backwards compatibility with the zillion old SBus cards in
	 * the world, we first look for the 'intr' property for the device.
	 */
	if (get_prop(child, OBP_INTR, 0, &intr_prop, &intr_len) !=
	    DDI_SUCCESS) {
		intr_len = 0;
	}

	/*
	 * If we're to support bus adapters and future platforms cleanly,
	 * we need to support the generalized 'interrupts' property.
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
		 */
		int *new;
		struct prop_ispec *l;

		cmn_err(CE_CONT, "?No 'interrupts' for %s%d\n",
		    DEVI(child)->devi_name, DEVI(child)->devi_instance);

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
			cmn_err(CE_CONT,
			    "Unable to translate 'interrupts' for %s%d\n",
			    DEVI(child)->devi_name,
			    DEVI(child)->devi_instance);
		}
		kmem_free(out, size);
	}
	return (has_registers);
}

/*
 * Special handling for "sbusmem" pseudo device nodes.
 * The special handling automatically creates the "reg"
 * property in the sbusmem nodes, based on the parent's
 * property so that each slot will automtically have a
 * correctly sized "reg" property, once created,
 * sbus_initchild does the rest of the work to init
 * the child node.
 */
static int
sbusmem_initchild(dev_info_t *dip, dev_info_t *child)
{
	register int i, n;
	int slot, size;
	char ident[10];

	slot = ddi_getprop(DDI_DEV_T_NONE, child,
	    DDI_PROP_DONTPASS | DDI_PROP_CANSLEEP, "slot", -1);
	if (slot == -1) {
		DPRINTF(SBUS_SBUSMEM_DEBUG, ("can't get slot property\n"));
		return (DDI_FAILURE);
	}

	/*
	 * Find the parent range corresponding to this "slot",
	 * so we can set the size of the child's "reg" property.
	 */
	for (i = 0, n = sparc_pd_getnrng(dip); i < n; i++) {
		struct rangespec *rp = sparc_pd_getrng(dip, i);

		if (rp->rng_cbustype == (u_int) slot) {
			struct regspec r;

			/* create reg property */

			r.regspec_bustype = (u_int) slot;
			r.regspec_addr = 0;
			r.regspec_size = rp->rng_size;
			(void) ddi_prop_create(DDI_DEV_T_NONE,
			    child, DDI_PROP_CANSLEEP, "reg",
			    (caddr_t) &r,
			    sizeof (struct regspec));

			/* create size property for slot */

			size = rp->rng_size;
			(void) ddi_prop_create(DDI_DEV_T_NONE,
			    child, DDI_PROP_CANSLEEP, "size",
			    (caddr_t) &size, sizeof (int));

			(void) sprintf(ident, "slot%x", slot);
			(void) ddi_prop_create(DDI_DEV_T_NONE,
			    child, DDI_PROP_CANSLEEP, "ident",
			    ident, sizeof (ident));

			return (DDI_SUCCESS);
		}
	}
	return (DDI_FAILURE);
}

/*
 * Called from the bus_ctl op of sysio sbus nexus driver
 * to implement the DDI_CTLOPS_INITCHILD operation.  That is, it names
 * the children of sysio sbusses based on the reg spec.
 *
 * Handles the following properties:
 *
 *	Property		value
 *	  Name			type
 *
 *	reg		register spec
 *	registers	wildcard s/w sbus register spec (.conf file property)
 *	intr		old-form interrupt spec
 *	interrupts	new (bus-oriented) interrupt spec
 *	ranges		range spec
 */
static int
sbus_initchild(dev_info_t *dip, dev_info_t *child)
{
	int rv, has_registers;
	char name[MAXNAMELEN];
	u_long slave_burstsizes;
	int slot;
	volatile u_ll_t *slot_reg;
#ifndef lint
	u_ll_t tmp;
#endif /* !lint */
	struct sbus_soft_state *softsp = (struct sbus_soft_state *)
	    ddi_get_soft_state(sbusp, ddi_get_instance(dip));
	extern int impl_ddi_merge_child(dev_info_t *child);
	extern int impl_ddi_merge_wildcard(dev_info_t *child);

	if (strcmp(ddi_get_name(child), "sbusmem") == 0) {
		if (sbusmem_initchild(dip, child) != DDI_SUCCESS)
			return (DDI_FAILURE);
	}

	/*
	 * Fill in parent-private data and note an indication if the
	 * "registers" property was used to fill in the data.
	 */
	has_registers = make_sbus_ppd(child);

	/*
	 * If this is a s/w node defined with the "registers" property,
	 * this means that this is a wildcard specifier, whose properties
	 * get applied to all previously defined h/w nodes with the same
	 * name and same parent.
	 */
	if ((has_registers) && (ddi_get_nodeid(child) == DEVI_PSEUDO_NODEID))
		return (impl_ddi_merge_wildcard(child));

	/*
	 * Name the device node using the underlying (prom) values
	 * of the first entry in the "reg" property.  For SBus devices,
	 * the textual form of the name is <name>@<slot#>,<offset>.
	 * This must match the prom's pathname or mountroot, etc, won't
	 * work.
	 */
	name[0] = '\0';
	if (sysio_pd_getslot(child) != (u_int)-1)
		(void) sprintf(name, "%x,%x", sysio_pd_getslot(child),
		    sysio_pd_getoffset(child));
	ddi_set_name_addr(child, name);

	/*
	 * If a pseudo node, attempt to merge it into a hw node,
	 * if merged, returns an indication that this node should
	 * be removed (after the caller uninitializes it).
	 */
	if ((rv = impl_ddi_merge_child(child)) != DDI_SUCCESS)
		return (rv);

	/* Figure out the child devices slot number */
	slot = sysio_pd_getslot(child);

	/* Modify the onboard slot numbers if applicable. */
	slot = (slot > 3) ? slot - 9 : slot;

	/* Get the slot configuration register for the child device. */
	slot_reg = softsp->sbus_slot_config_reg + slot;

	/*
	 * Program the devices slot configuration register for the
	 * appropriate slave burstsizes.
	 * The upper 16 bits of the slve-burst-sizes are for 64 bit sbus
	 * and the lower 16 bits are the burst sizes for 32 bit sbus. If
	 * we see that a device supports both 64 bit and 32 bit slave accesses,
	 * we default to 64 bit and turn it on in the slot config reg.
	 */
	if ((slave_burstsizes = (u_long) ddi_getprop(DDI_DEV_T_ANY, child,
	    DDI_PROP_DONTPASS, "slave-burst-sizes", 0)) != 0) {

		/* Make sure the child is an immediate descendant */
		if (dip == ddi_get_parent(child)) {
			u_int burstsizes = 0;

			/*
			 * Save the burstsizes of this device along
			 * with the burstsizes of previous devices that
			 * share this sbus slot */
			softsp->sbus_slave_burstsizes[slot] &=
			    (slave_burstsizes &
			    ((SYSIO64_SLAVEBURST_RANGE <<
			    SYSIO64_BURST_SHIFT) |
			    SYSIO_SLAVEBURST_RANGE));

			if (softsp->sbus_slave_burstsizes[slot] &
			    SYSIO64_BURST_MASK) {
				/* get the 64 bit burstsizes */
				burstsizes = softsp->sbus_slave_burstsizes[slot]
				    >> SYSIO64_BURST_SHIFT;

				/* Turn on 64 bit PIO's on the sbus */
				*slot_reg |= SBUS_ETM;
			} else {
				/* Turn off 64 bit PIO's on the sbus */
				*slot_reg &= ~SBUS_ETM;
			}

			if (softsp->sbus_slave_burstsizes[slot] &
			    SYSIO_BURST_MASK && !burstsizes) {
				burstsizes =
				    softsp->sbus_slave_burstsizes[slot] &
				    SYSIO_BURST_MASK;
			}

			/* Get the burstsizes into sysio register format */
			burstsizes >>= SYSIO_SLAVEBURST_REGSHIFT;

			if (burstsizes) {
				/* Reset reg in case we're scaling back */
				*slot_reg &= (u_ll_t) ~SYSIO_SLAVEBURST_MASK;

				/* Program the burstsizes */
				*slot_reg |= (u_ll_t) burstsizes;
			} else {
				/* reset the register */
				*slot_reg = (u_ll_t) burstsizes;
			}

			/* Flush system load/store buffers */
#ifndef lint
			tmp = *slot_reg;
#endif /* !lint */
		}
	} else {
		/*
		 * 0 the slots burstsizes if the child device doesn't have
		 * the correct property.
		 */
		softsp->sbus_slave_burstsizes[slot] = 0;

		/* reset the register since slave burst sizes not supported */
		*slot_reg = (u_ll_t) softsp->sbus_slave_burstsizes[slot];

		/* Flush system load/store buffers */
#ifndef lint
		tmp = *slot_reg;
#endif /* !lint */
	}

	return (DDI_SUCCESS);
}

static int
sbus_uninitchild(dev_info_t *dip)
{
	register struct sysio_parent_private_data *pdptr;
	register size_t n;

	if ((pdptr = (struct sysio_parent_private_data *)
	    ddi_get_parent_data(dip)) != NULL)  {
		if ((n = (size_t)pdptr->par_nintr) != 0)
			kmem_free(pdptr->par_intr, n *
			    sizeof (struct sysiointrspec));

		if ((n = (size_t)pdptr->par_nrng) != 0)
			kmem_free(pdptr->par_rng, n *
			    sizeof (struct rangespec));

		if ((n = pdptr->par_nreg) != 0)
			kmem_free(pdptr->par_reg, n * sizeof (struct regspec));

		kmem_free(pdptr, sizeof (*pdptr));
		ddi_set_parent_data(dip, NULL);
	}
	ddi_set_name_addr(dip, NULL);
	/*
	 * Strip the node to properly convert it back to prototype form
	 */
	ddi_remove_minor_node(dip, NULL);
	impl_rem_dev_props(dip);
	return (DDI_SUCCESS);
}

static int
sbus_ctlops(dev_info_t *dip, dev_info_t *rdip,
    ddi_ctl_enum_t op, void *arg, void *result)
{
	struct sbus_soft_state *softsp = (struct sbus_soft_state *)
		ddi_get_soft_state(sbusp, ddi_get_instance(dip));

	switch (op) {

	case DDI_CTLOPS_INITCHILD:
		return (sbus_initchild(dip, (dev_info_t *)arg));

	case DDI_CTLOPS_UNINITCHILD:
		return (sbus_uninitchild(arg));

	case DDI_CTLOPS_IOMIN: {
		register int val = *((int *) result);

		/*
		 * The 'arg' value of nonzero indicates 'streaming' mode.
		 * If in streaming mode, pick the largest of our burstsizes
		 * available and say that that is our minimum value (modulo
		 * what mincycle is).
		 */
		if ((int) arg)
			val = maxbit(val,
			    (1 << (ddi_fls(softsp->sbus_burst_sizes) - 1)));
		else
			val = maxbit(val,
			    (1 << (ddi_ffs(softsp->sbus_burst_sizes) - 1)));

		*((int *) result) = val;
		return (ddi_ctlops(dip, rdip, op, arg, result));
	}

	case DDI_CTLOPS_REPORTDEV: {
		register dev_info_t *pdev;
		register int i, n;
		char *msgbuf;
		char *p;

	/*
	 * So we can do one atomic cmn_err call, we allocate a 4k
	 * buffer, and format the reportdev message into that buffer,
	 * send it to cmn_err, and then free the allocated buffer.
	 * Hopefully, 4k is enough for any sbus device.
	 */
#define	REPORTDEV_MSG_SZ	((size_t)4096)

		int sbusid = DEVI(dip)->devi_instance;

		if (DEVI_PD(rdip) == NULL)
			return (DDI_FAILURE);

		msgbuf = (char *)kmem_zalloc(REPORTDEV_MSG_SZ, KM_SLEEP);
		p = msgbuf;

		pdev = (dev_info_t *)DEVI(rdip)->devi_parent;
		(void) sprintf(p, "?%s%d at %s%d",
		    DEVI(rdip)->devi_name, DEVI(rdip)->devi_instance,
		    DEVI(pdev)->devi_name, DEVI(pdev)->devi_instance);
		p += strlen(p);

		for (i = 0, n = sysio_pd_getnreg(rdip); i < n; i++) {
			register struct regspec *rp;

			rp = sysio_pd_getreg(rdip, i);
			if (i == 0)
				(void) sprintf(p, ": SBus%d ", sbusid);
			else
				(void) sprintf(p, " and ");
			p += strlen(p);

			(void) sprintf(p, "slot 0x%x offset 0x%x",
			    rp->regspec_bustype, rp->regspec_addr);
			p += strlen(p);
		}

		for (i = 0, n = sysio_pd_getnintr(rdip); i < n; i++) {

			register int pri, sbuslevel;

			if (i == 0)
				(void) sprintf(p, " ");
			else
				(void) sprintf(p, ", ");
			p += strlen(p);

			pri = sysio_pd_getintr(rdip, i)->pil;
			if ((sbuslevel = sysio_pd_getintr(rdip, i)->bus_level)
			    != -1) {
				if (sbuslevel > MAX_SBUS_LEVEL)
					(void) sprintf(p, "Onboard device ");
				else
					(void) sprintf(p, "SBus level %d ",
					    sbuslevel);
				p += strlen(p);
			}

			(void) sprintf(p, "sparc9 ipl %d", pri);
			p += strlen(p);
		}

		(void) strcpy(p, "\n");
		cmn_err(CE_CONT, msgbuf);
		kmem_free((void *)msgbuf, REPORTDEV_MSG_SZ);
		return (DDI_SUCCESS);

#undef	REPORTDEV_MSG_SZ
	}

	case DDI_CTLOPS_XLATE_INTRS: {
		static int sbus_ctl_xlate_intrs(dev_info_t *, dev_info_t *,
			int *, struct sysio_parent_private_data *);

		return (sbus_ctl_xlate_intrs(dip, rdip, arg, result));

	}
	case DDI_CTLOPS_SLAVEONLY:
		return (DDI_FAILURE);

	case DDI_CTLOPS_AFFINITY: {
		dev_info_t *dipb = (dev_info_t *) arg;
		int r_slot, b_slot;

		if ((b_slot = find_sbus_slot(dip, dipb)) < 0)
			return (DDI_FAILURE);

		if ((r_slot = find_sbus_slot(dip, rdip)) < 0)
			return (DDI_FAILURE);

		return ((b_slot == r_slot)? DDI_SUCCESS : DDI_FAILURE);

	}
	case DDI_CTLOPS_DMAPMAPC:
		cmn_err(CE_CONT, "?DDI_DMAPMAPC called!!\n");
		return (DDI_FAILURE);

	case DDI_CTLOPS_INTR_HILEVEL:
		/*
		 * Indicate whether the interrupt specified is to be handled
		 * above lock level.  In other words, above the level that
		 * cv_signal and default type mutexes can be used.
		 *
		 * XXX This is a royal hack.  We should call a kernel
		 * specific routine to determine LOCK_LEVEL. (RAZ)
		 */
		*(int *) result =
		    (((struct sysiointrspec *)arg)->pil > LOCK_LEVEL);
		return (DDI_SUCCESS);

	/*
	 * XXX	This pokefault_mutex clutter needs to be done differently.
	 *	Note that i_ddi_poke() calls this routine in the order
	 *	INIT then optionally FLUSH then always FINI.
	 */
	case DDI_CTLOPS_POKE_INIT:
		mutex_enter(&pokefault_mutex);
		pokefault = -1;
		return (DDI_SUCCESS);

	case DDI_CTLOPS_POKE_FLUSH: {
		volatile u_ll_t tmpreg;

		/* Flush any sbus store buffers. */
		tmpreg = *softsp->sbus_ctrl_reg;

		/*
		 * Read the sbus error reg and see if a fault occured.  If
		 * one has, give the SYSIO time to packetize the interrupt
		 * for the fault and send it out.  The sbus error handler will
		 * 0 these fields when it's called to service the fault.
		 */
		tmpreg = *softsp->sbus_err_reg;
		while (tmpreg & SB_AFSR_P_TO || tmpreg & SB_AFSR_P_BERR)
			tmpreg = *softsp->sbus_err_reg;

		return (pokefault == 1 ? DDI_FAILURE : DDI_SUCCESS);
	}
	case DDI_CTLOPS_POKE_FINI:
		pokefault = 0;
		mutex_exit(&pokefault_mutex);
		return (DDI_SUCCESS);

	case DDI_CTLOPS_DVMAPAGESIZE:
		*(u_long *)result = IOMMU_PAGESIZE;
		return (DDI_SUCCESS);

	default:
		return (ddi_ctlops(dip, rdip, op, arg, result));
	}
}

static int
find_sbus_slot(dev_info_t *dip, dev_info_t *rdip)
{
	dev_info_t *child;
	int slot = -1;

	/*
	 * look for the node that's a direct child of this Sbus node.
	 */
	while (rdip && (child = ddi_get_parent(rdip)) != dip) {
		rdip = child;
	}

	/*
	 * If there is one, get the slot number of *my* child
	 */
	if (child == dip)
		slot = sysio_pd_getslot(rdip);

	return (slot);
}

/*
 * sbus_get_intrspec: sbus nex convert an interrupt number to an interrupt
 *			specification. The interrupt number determines
 *			which interrupt spec will be returned if more than
 *			one exists. Look into the parent private data
 *			area of the dev_info structure to find the interrupt
 *			specification.  First check to make sure there is
 *			one that matchs "inumber" and then return a pointer
 *			to it.  Return NULL if one could not be found.
 *
 */
static ddi_intrspec_t
sbus_get_intrspec(dev_info_t *dip, dev_info_t *rdip, u_int inumber)
{
	struct sysio_parent_private_data *ppdptr;

#ifdef	lint
	dip = dip;
#endif

	/*
	 * convert the parent private data pointer in the childs dev_info
	 * structure to a pointer to a sunddi_compat_hack structure
	 * to get at the interrupt specifications.
	 */
	ppdptr = (struct sysio_parent_private_data *)
	    (DEVI(rdip))->devi_parent_data;

	/*
	 * validate the interrupt number.
	 */
	if (inumber >= ppdptr->par_nintr) {
		return (NULL);
	}

	/*
	 * return the interrupt structure pointer.
	 */
	return ((ddi_intrspec_t)&ppdptr->par_intr[inumber]);
}

static u_int
run_vec_poll_list(caddr_t arg)
{

	struct sbus_wrapper_arg *sbus_arg;
	struct vec_poll_list **list_p = (struct vec_poll_list **) arg;
	struct vec_poll_list *pl;
	int rval = 0;

	for (pl = *list_p; pl != NULL; pl = pl->poll_next) {
		sbus_arg = (struct sbus_wrapper_arg *)pl->poll_arg;

		rval |= (*sbus_arg->funcp)(sbus_arg->arg);

	}

	return (rval);
}

/*
 * This is the sbus interrupt routine wrapper function.  This function
 * installs itself as a child devices interrupt handler.  It's function is
 * to dispatch a child devices interrupt handler, and then
 * reset the interrupt clear register for the child device.
 *
 * Warning: This routine may need to be implemented as an assembly level
 * routine to improve performance.
 */

#define	MAX_INTR_CNT 10

static u_int
sbus_intr_wrapper(caddr_t arg)
{
	u_int intr_return;
	volatile u_ll_t tmpreg;
	struct sbus_wrapper_arg *intr_info;
	u_char *spurious_cntr;

	intr_info = (struct sbus_wrapper_arg *) arg;
	spurious_cntr = &intr_info->softsp->spurious_cntrs[intr_info->pil];
	intr_return = (*intr_info->funcp)(intr_info->arg);

	/* Set the interrupt state machine to idle */
	tmpreg = *intr_info->softsp->sbus_ctrl_reg;
	tmpreg = SBUS_INTR_IDLE;
	*intr_info->clear_reg = tmpreg;
	tmpreg = *intr_info->softsp->sbus_ctrl_reg;

	if (intr_return == DDI_INTR_UNCLAIMED) {
		(*spurious_cntr)++;

		if (*spurious_cntr < MAX_INTR_CNT) {
			if (intr_cntr_on)
				return (DDI_INTR_CLAIMED);
		} else {
#ifdef DEBUG
			char buf[50];

			sprintf(buf, "10 unclaimed interrupts in a row "
			    "at pil level %d\n", intr_info->pil);
			debug_enter(buf);
#endif
			/*
			 * Reset spurrious counter once we acknowledge
			 * it to the system level.
			 */
			*spurious_cntr = (u_char) 0;
		}
	} else {
		*spurious_cntr = (u_char) 0;
	}

	return (intr_return);
}

/*
 * add_intrspec - Add an interrupt specification.
 */
static int
sbus_add_intrspec(
	dev_info_t *dip,
	dev_info_t *rdip,
	ddi_intrspec_t intrspec,
	ddi_iblock_cookie_t *iblock_cookiep,
	ddi_idevice_cookie_t *idevice_cookiep,
	u_int (*int_handler)(caddr_t int_handler_arg),
	caddr_t int_handler_arg,
	int kind)
{
	ASSERT(intrspec != 0);
	ASSERT(rdip != 0);
	ASSERT(ddi_get_driver(rdip) != 0);

	if (int_handler == NULL) {
		cmn_err(CE_WARN, "Invalid interrupt handler\n");
		return (DDI_FAILURE);
	}
	switch (kind) {
	case IDDI_INTR_TYPE_NORMAL: {
		struct sbus_soft_state *softsp = (struct sbus_soft_state *)
		    ddi_get_soft_state(sbusp, ddi_get_instance(dip));
		struct sysiointrspec *ispec =
				(struct sysiointrspec *) intrspec;
		volatile u_ll_t *mondo_vec_reg;
		volatile u_ll_t tmp_mondo_vec;
		volatile u_ll_t *intr_state_reg;
		volatile u_ll_t	tmpreg;	/* HW flush reg */
		struct dev_ops *dops = DEVI(rdip)->devi_ops;
		u_int start_bit;
		int mondo;
		struct vec_poll_list  *poll_node;
		struct sbus_wrapper_arg *sbus_arg;
		int hot;
		u_int slot;
		extern int getprocessorid();

		/* Check if we have a valid sbus slot address */
		if ((slot = (u_int) find_sbus_slot(dip, rdip)) >=
		    MAX_SBUS_SLOT_ADDR) {
			return (DDI_FAILURE);
		}

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
		mondo_vec_reg = (softsp->intr_mapping_reg +
		    ino_table[mondo]->mapping_reg);

		/*
		 * GROSS - This is an intermediate step in identifying
		 * the exact bits which represent the device in the interrupt
		 * state diagnostic register.
		 */

		if (mondo > MAX_MONDO_EXTERNAL) {
			start_bit = ino_table[mondo]->diagreg_shift;
			intr_state_reg = softsp->obio_intr_state;
		} else {
			start_bit = 16 * (mondo >> 3) + 2 * (mondo & 0x7);
			intr_state_reg = softsp->sbus_intr_state;
		}


		/* Allocate a nexus interrupt data structure */
		sbus_arg = (struct sbus_wrapper_arg *) kmem_alloc(
		    sizeof (struct sbus_wrapper_arg), KM_SLEEP);
		sbus_arg->funcp = int_handler;
		sbus_arg->arg = int_handler_arg;
		sbus_arg->clear_reg = (softsp->clr_intr_reg +
		    ino_table[mondo]->clear_reg);
		DPRINTF(SBUS_REGISTERS_DEBUG, ("Mondo 0x%x Interrupt clear "
		    "reg: 0x%x", mondo, sbus_arg->clear_reg));
		sbus_arg->softsp = softsp;
		sbus_arg->pil = ispec->pil;

		/* Save the sbus_arg in ispecp so we can use the info later. */
		ispec->sbus_handler_arg = sbus_arg;

		/*
		 * Grab this lock here. So it will protect the poll list and
		 * calls to add_ivintr and rem_ivintr.
		 */
		mutex_enter(&softsp->intr_poll_list_lock);

		/* Check if we have a poll list to deal with */
		if (softsp->poll_array[mondo]) {
			/* poll list has been added */
			*mondo_vec_reg &= ~INTERRUPT_VALID;
			tmpreg = *softsp->sbus_ctrl_reg;
#ifdef	lint
			tmpreg = tmpreg;
#endif

			/*
			 * Two bits per mondo in the diagnostic register
			 * indicate the status of its interrupt.
			 * 0 - idle, 1 - transmit, 3 - pending.
			 */
			while (((int)((int) *intr_state_reg >>
				    (int)start_bit) & 0x3) == INT_PENDING);

			poll_node = (struct vec_poll_list *)kmem_alloc(
				    sizeof (struct vec_poll_list), KM_SLEEP);
			poll_node->poll_arg = sbus_arg;
			poll_node->poll_next = softsp->poll_array[mondo];
			softsp->poll_array[mondo] = poll_node;

		} else {
			struct intr_vector intr_node;

			/* Initialize intr_node */
			intr_node.iv_handler = (intrfunc) 0;

			/* check if a handler for this mondo exists */
			rem_ivintr((softsp->upa_id << 6 | mondo),
				&intr_node);

			/* Check for whether we are a precise interrupt. */
			if (!intr_node.iv_handler) {
				/*
				 * No handler added yet in the interrupt vector
				 * table for this mondo.
				 * Install the nexus interrupt wrapper in the
				 * system. The wrapper will call the device
				 * interrupt handler.
				 */
				add_ivintr((u_int) (softsp->upa_id << 6
				| mondo), (u_int) ispec->pil,
				sbus_intr_wrapper, (caddr_t) sbus_arg,
				(hot) ? NULL : &unsafe_driver);

			} else { /* Need to start a poll list */

				/* Disable interrupts from this mondo */
				*mondo_vec_reg &= ~INTERRUPT_VALID;
				tmpreg = *softsp->sbus_ctrl_reg;

				while (((int)((int) *intr_state_reg
				    >> (int)start_bit) & 0x3) == INT_PENDING);

				/* allocate a new poll node */
				poll_node = (struct vec_poll_list *)
				kmem_zalloc(sizeof (struct vec_poll_list),
				    KM_SLEEP);

				/* Add the new handler to poll node */
				poll_node->poll_arg = sbus_arg;

				/* Add the node to the head of the poll list */
				softsp->poll_array[mondo] = poll_node;

				/* allocate another poll node */
				poll_node = (struct vec_poll_list *)
				kmem_zalloc(sizeof (struct vec_poll_list),
				    KM_SLEEP);

				/* Add the old handler to the poll node */
				poll_node->poll_arg =
				    (struct sbus_wrapper_arg *)
				    intr_node.iv_arg;

				/* Add the second node to the poll list. */
				softsp->poll_array[mondo]->poll_next =
				    poll_node;

				/*
				 * Allocate a nexus interrupt data structure
				 * for the poll list function and install it.
				 */
				sbus_arg = (struct sbus_wrapper_arg *)
				    kmem_alloc(sizeof (struct sbus_wrapper_arg),
				    KM_SLEEP);

				sbus_arg->funcp = run_vec_poll_list;
				sbus_arg->arg =
				    (caddr_t) &softsp->poll_array[mondo];
				sbus_arg->clear_reg = (softsp->clr_intr_reg +
				    ino_table[mondo]->clear_reg);
				sbus_arg->softsp = softsp;
				sbus_arg->pil = ispec->pil;

				add_ivintr((u_int) (softsp->upa_id << 6
				    | mondo), (u_int) ispec->pil,
				    sbus_intr_wrapper, (caddr_t) sbus_arg,
				(hot) ? NULL : &unsafe_driver);

			}
		}

		softsp->intr_hndlr_cnt[slot]++;

		mutex_exit(&softsp->intr_poll_list_lock);

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
			    (u_short) ispec->bus_level;
		}

		/*
		 * Program the mondo vector accordingly.  This MUST be the
		 * last thing we do.  Once we program the mondo, the device
		 * may begin to interrupt.
		 */
		tmp_mondo_vec = (u_int) getprocessorid() <<
				    INTERRUPT_CPU_FIELD;
		tmp_mondo_vec |= INTERRUPT_VALID;

		DPRINTF(SBUS_REGISTERS_DEBUG, ("Mondo 0x%x mapping reg: 0x%x "
		    "Mapping register data 0x%x",
		    mondo, mondo_vec_reg, tmp_mondo_vec));

		/* Store it in the hardware reg. */
		*mondo_vec_reg = tmp_mondo_vec;

		/* Flush store buffers */
		tmpreg = *softsp->sbus_ctrl_reg;
		return (DDI_SUCCESS);

	}

	default:
		/*
		 * If we can't do it here, our parent can't either, so
		 * fail the request.
		 */
		return (DDI_INTR_NOTFOUND);
	}
}

/* delete an entry from the vec poll_list */
static int
delete_vec_poll_list(caddr_t funcp, struct vec_poll_list **ptr)
{
	struct vec_poll_list *walk, **prevp;
	struct sbus_wrapper_arg *sbus_arg;

	prevp = ptr;
	walk = *ptr;
	while (walk != NULL) {
		sbus_arg = (struct sbus_wrapper_arg *)walk->poll_arg;
		if ((caddr_t)sbus_arg->funcp == funcp) {
			*prevp = walk->poll_next;
			kmem_free(walk, sizeof (struct vec_poll_list));
			return (1);
		}
		prevp = &walk->poll_next;
		walk = walk->poll_next;
	}
	return (0);
}

/*
 * remove_intrspec - Remove an interrupt specification.
 */
/*ARGSUSED*/
static void
sbus_remove_intrspec(
	dev_info_t *dip,
	dev_info_t *rdip,
	ddi_intrspec_t intrspec,
	ddi_iblock_cookie_t iblock_cookiep)
{
	volatile u_ll_t *mondo_vec_reg;
	volatile u_ll_t *intr_state_reg;
#ifndef lint
	volatile u_ll_t tmpreg;
#endif /* !lint */
	struct sysiointrspec *ispec = (struct sysiointrspec *) intrspec;
	struct sbus_soft_state *softsp = (struct sbus_soft_state *)
	    ddi_get_soft_state(sbusp, ddi_get_instance(dip));
	int start_bit, mondo, slot;
	struct sbus_wrapper_arg *sbus_arg = ispec->sbus_handler_arg;
	struct intr_vector intr_node;

	/* Grab the mutex protecting the poll list */
	mutex_enter(&softsp->intr_poll_list_lock);

	mondo = ispec->mondo;

	mondo_vec_reg = (softsp->intr_mapping_reg +
	    ino_table[mondo]->mapping_reg);

	/* Turn off the valid bit in the mapping register. */
	*mondo_vec_reg &= ~INTERRUPT_VALID;
#ifndef lint
	tmpreg = *softsp->sbus_ctrl_reg;
#endif /* !lint */

	/* Get our bit position for checking intr pending */
	if (mondo > MAX_MONDO_EXTERNAL) {
		start_bit = ino_table[mondo]->diagreg_shift;
		intr_state_reg = softsp->obio_intr_state;
	} else {
		start_bit = 16 * (mondo >> 3) + 2 * (mondo & 0x7);
		intr_state_reg = softsp->sbus_intr_state;
	}

	slot = find_sbus_slot(dip, rdip);
	/* Decrement the intr handler count on this slot */
	softsp->intr_hndlr_cnt[slot]--;

	if (softsp->poll_array[mondo]) {
		int error;

		while (((int)((int) *intr_state_reg
		    >> (int)start_bit) & 0x3) == INT_PENDING);

		error = delete_vec_poll_list(
		    (caddr_t)sbus_arg->funcp,
		    &softsp->poll_array[mondo]);
		ASSERT(error != 0);
#ifdef lint
		error = error;
#endif /* lint */

		/* If we still have a list, we're done. */
		if (softsp->poll_array[mondo]) {
			/*
			 * Since we still have a poll list for this level, we
			 * don't need to check softsp->intr_hndlr_cnt[slot]
			 * before turning interrupts for the slot back on.
			 */
			*mondo_vec_reg |= INTERRUPT_VALID;
#ifndef lint
			tmpreg = *softsp->sbus_ctrl_reg;
#endif /* !lint */
			/*
			 * Free up the memory used for the sbus interrupt
			 * handler
			 */
			kmem_free(sbus_arg, sizeof (struct sbus_wrapper_arg));
			ispec->sbus_handler_arg = (struct sbus_wrapper_arg *) 0;
			mutex_exit(&softsp->intr_poll_list_lock);
			return;
		}

		/* Eliminate run_vec_poll_list handler from the system. */
		rem_ivintr(softsp->upa_id << 6 | mondo, &intr_node);
		/* Get the sbus_arg for run_vec_poll_list() */
		sbus_arg = (struct sbus_wrapper_arg *) intr_node.iv_arg;

	} else {
		/* Eliminate the particular handler from the system. */
		rem_ivintr(softsp->upa_id << 6 | mondo,
		    (struct intr_vector *) NULL);
	}

	/*
	 * If other devices are still installed for this slot, we need to
	 * turn the valid bit back on.
	 */
	if (softsp->intr_hndlr_cnt[slot] > 0) {
		*mondo_vec_reg |= INTERRUPT_VALID;
#ifndef lint
		tmpreg = *softsp->sbus_ctrl_reg;
#endif /* !lint */
	}

	/* Free up the memory used for the sbus interrupt handler */
	kmem_free(sbus_arg, sizeof (struct sbus_wrapper_arg));
	ispec->sbus_handler_arg = (struct sbus_wrapper_arg *) 0;

	mutex_exit(&softsp->intr_poll_list_lock);
}

/*
 * We're prepared to claim that the interrupt string is in
 * the form of a list of <SBusintr> specifications, or we're dealing
 * with on-board devices and we have an interrupt_number property which
 * gives us our mondo number.
 * Translate the sbus levels or mondos into sysiointrspecs.
 */
static int
sbus_ctl_xlate_intrs(dev_info_t *dip, dev_info_t *rdip, int *in,
	struct sysio_parent_private_data *pdptr)
{
	register int n;
	register size_t size;
	register struct sysiointrspec *new;

	/*
	 * The list consists of <SBuspri> elements
	 */
	if ((n = *in++) < 1) {
		cmn_err(CE_CONT, "?SBus: Bad interrupts specification for %s",
			ddi_get_name(rdip));
		return (DDI_FAILURE);
	}

	pdptr->par_nintr = n;
	size = n * sizeof (struct sysiointrspec);
	new = kmem_zalloc(size, KM_SLEEP);
	pdptr->par_intr = (struct sysiointrspec *) new;

	while (n--) {
		int level = *in++, mondo, slot;

		/*
		 * Create the sysio mondo number.  onboard devices will have
		 * an "interrupts" property, that is equal to the mondo number.
		 * If the devices are from the
		 * expansion slots, we construct the mondo number by putting
		 * the slot number in the upper three bits, and the sbus
		 * interrupt level in the lower three bits.
		 */
		if (level > MAX_SBUS_LEVEL) {
			mondo = level;
		} else {
			/* Construct mondo from slot and interrupts */
			if ((slot = find_sbus_slot(dip, rdip)) == -1) {
				cmn_err(CE_WARN, "Can't determine sbus slot "
				    "of child %s\n", ddi_get_name(rdip));
				goto broken;
			}

			mondo = slot << 3;
			mondo |= level;
		}

		/* Sanity check the mondos range */
		if (mondo >= MAX_INO_TABLE_SIZE) {
			cmn_err(CE_WARN, "Mondo vector 0x%x out of range",
			    mondo);
			goto broken;
		}
		/* Sanity check the mondos value */
		if (!ino_table[mondo]) {
			cmn_err(CE_WARN, "Mondo vector 0x%x is invalid",
			    mondo);
			goto broken;
		}

		new->mondo = mondo;
		new->pil = interrupt_priorities[mondo];
		new->bus_level = level;
		DPRINTF(SBUS_INTERRUPT_DEBUG, ("Interrupt info for device %s"
		    "Mondo: 0x%x, Pil: 0x%x, sbus level: 0x%x\n",
		    ddi_get_name(rdip), new->mondo, new->pil, new->bus_level));
		new++;
	}

	return (DDI_SUCCESS);
	/*NOTREACHED*/

broken:
	kmem_free(pdptr->par_intr, size);
	pdptr->par_intr = (void *)0;
	pdptr->par_nintr = 0;
	return (DDI_FAILURE);
}

/*
 * Called by suspend/resume to save/restore the interrupt status (valid bit)
 * of the interrupt mapping registers.
 */
static void
sbus_cpr_handle_intr_map_reg(u_ll_t *cpr_softsp, u_ll_t *baddr, int save)
{
	int i;
	volatile u_ll_t *mondo_vec_reg;

	for (i = 0; i < MAX_INO_TABLE_SIZE; i++) {
		if (ino_table[i] != NULL) {
			mondo_vec_reg = baddr + ino_table[i]->mapping_reg;
			if (save) {
				if (*mondo_vec_reg & INTERRUPT_VALID) {
					cpr_softsp[i] = *mondo_vec_reg;
				}
			} else {
				if (cpr_softsp[i]) {
					*mondo_vec_reg = cpr_softsp[i];
				}
			}
		}
	}
}
