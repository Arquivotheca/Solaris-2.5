/*
 * Copyright (c) 1991-1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ident	"@(#)pci.c	1.16	95/06/06 SMI"

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/pci.h>
#include <sys/sunddi.h>
#include <sys/autoconf.h>
#include <sys/ddi_impldefs.h>
#include <sys/cmn_err.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/debug.h>
#include <sys/sysmacros.h>
#include <vm/as.h>
#include <vm/page.h>
#include <sys/ivintr.h>
#include <sys/spl.h>
#include <sys/async.h>
#include <sys/dvma.h>
#include <sys/pci_regs.h>
#include <sys/pci_var.h>
#include <sys/pci_iommu.h>

extern u_longlong_t iommu_tsb_physaddr[];
extern int pci_iommu_tsb_alloc_size;

/*
 * By defining INTERRUPT_PRIORITIES, the priority level of the
 * interrupt handler for a PCI device can be defined via an
 * "interrupt-priorities" property.  This property is an array
 * of integer values that have a one to one mapping the the
 * "interrupts" property.  For example, if a device's "interrupts"
 * property was (1, 2) and its "interrupt-priorities" value
 * was (5, 12), the handler for the first interrupt would run
 * at cpu priority 5 and the second at priority 12.  This would
 * override the drivers standard mechanism for assigning priorities
 * to interrupt handlers.
 */
#define	INTERRUPT_PRIORITIES

/*
 * By defining the ACC_ENABLE and setting pci_acc_enable to 1, the
 * nexus driver will ensure that the i/o space bit is enabled in
 * the requesting device's command register on register mapping
 * requests for i/o registers and that the mem space bit is
 * enabled for memory registers.
 */
#define	ACC_ENABLE

/*
 * By defining NO_ASSIGNED_ADDRESS, the driver will tolerate devices
 * with relocatable "reg" properties, but no "assigned-addresses"
 * properties.  The nexus driver will determine the addresses of
 * these devices by reading the base address registers in the
 * devices' configuration header.
 */
#define	NO_ASSIGNED_ADDRESS

/*
 * By defining NO_CLASS_CODE, the driver will tolerate devices
 * with no "class-code" property.  The nexus driver will determine
 * the base and sub class codes of the device by reading its
 * configuration header.
 */
#define	NO_CLASS_CODE

/*
 * By defining CONFIG_SPACE_SIZE_ZERO, the will fail mapping requests
 * for configuration space "reg" entries whose size is not zero.
#define	CONFIG_SPACE_SIZE_ZERO
 */

/*
 * The following are defined as variables to allow patching
 * for debugging and tuning.
 */
#ifdef DEBUG
static u_int pci_debug_flags = 0;
#endif
static u_int ecc_error_intr_enable = 1;		/* enable ue & ce interrupts */
static u_int pci_stream_buf_enable = 0x3; 	/* for pci0 and pci1 */
static u_int pci_error_intr_enable = 0x3; 	/* for pci0 and pci1 */
static u_int pci_sbh_error_enable = 0x3; 	/* for pci0 and pci1 */
static u_int pci_sbh_error_intr_enable = 0x3; 	/* for pci0 and pci1 */
static u_int pci_bus_parking_enable = 0x3; 	/* for pci0 and pci1 */
static u_int pci_cpu_prio_enable = 0x3; 	/* for pci0 and pci1 */
static u_int pci_b_arb_enable = 0xf;		/* pci0 - all 4 slots */
static u_int pci_a_arb_enable = 0xf;		/* pci1 - all 4 slots */
static u_int pci_intr_retry_intv = 5;		/* for interrupt retry reg */
static u_int pci_latency_timer = 40;		/* for pci latency timer reg */
static u_int pci_sync_buf_timeout = 100;	/* 100 ticks = 1 second */
static u_int pci_call_ue_error = 1;
static u_int pci_call_ce_error = 1;
#if defined(CALL_KILL_PROCS)
static u_int pci_call_kill_procs = 1;
#endif
static u_int pci_per_enable = 1;
static u_int pci_iommu_lock_enable = 0;
#if defined(ACC_ENABLE)
static u_int pci_acc_enable = 1;
#endif

/*
 * The following flag control when iommu flush operations occur.
 * When set to zero, flushing is done in the entry points responsible
 * for tearing down dvma space (ie the unbind handle entry point).
 * When set to non-zero, flushing will done in the routines responsible
 * for assigning assigning dvma space (ie the bind handle entry point).
 */
static u_int pci_flush_on_alloc = 0;

/*
 * The following flag controls behavior of the ino handler routine
 * when multiple interrupts are attached to a single ino.  Typically
 * this case would occur for the ino's assigned to the PCI bus slots
 * with multi-function devices or bus bridges.
 *
 * Setting the flag to zero causes the ino handler routine to return
 * after finding the first interrupt handler to claim the interrupt.
 *
 * Setting the flag to non-zero causes the ino handler routine to
 * return after making one complete pass through the interrupt
 * handlers.
 */
static u_int pci_check_all_handlers = 1;

/*
 * The following variable can be patched to control the
 * size of iommu dvma space as follows:
 *
 *	0 = 8 MB
 *	1 = 16 MB
 *	2 = 32 MB
 *	3 = 64 MB
 *	4 = 128 MB
 *	5 = 256 MB
 *	6 = 512 MB
 *	7 = 1 GB
 *
 * Note: This value can not be set to size that would be larger
 * than the size of the tsb table which is allocated in startup()
 * according to the variable pci_iommu_tsb_alloc_size.
 *
 * Congtiguous physical memory allocation routines are needed so
 * that the nexus drivers can be completely responsible for their
 * own tsb allocation.
 */
u_int pci_iommu_tsb_size = 0x3;

/*
 * This array is used to determine the sparc PIL at the which the
 * handler for a given INO will execute.  This table is for onboard
 * devices only.  A different scheme will be used for plug-in cards.
 */

static int ino_to_pil[] = {

	/* pil */		/* ino */

	-1, -1, -1, -1,  	/* 0x00 - 0x03: bus A slot 0 int#A, B, C, D */
	-1, -1, -1, -1,		/* 0x04 - 0x07: bus A slot 1 int#A, B, C, D */
	0, 0, 0, 0,  		/* 0x08 - 0x0B: unused */
	0, 0, 0, 0,		/* 0x0C - 0x0F: unused */

	-1, -1, -1, -1,  	/* 0x10 - 0x13: bus B slot 0 int#A, B, C, D */
	-1, -1, -1, -1,		/* 0x14 - 0x17: bus B slot 1 int#A, B, C, D */
	-1, -1, -1, -1,  	/* 0x18 - 0x1B: bus B slot 2 int#A, B, C, D */
	-1, -1, -1, -1,		/* 0x1C - 0x1F: bus B slot 3 int#A, B, C, D */

	4,			/* 0x20: SCSI */
	6,			/* 0x21: ethernet */
	3,			/* 0x22: parallel port */
	13,			/* 0x23: audio record */
	13,			/* 0x24: audio playback */
	14,			/* 0x25: power fail */
	0,			/* 0x26: unused */
	8,			/* 0x27: floppy */
	0,			/* 0x28: unused */
	12,			/* 0x29: keyboard */
	12,			/* 0x2A: mouse */
	12,			/* 0x2B: serial */
	0,			/* 0x2C: timer/counter 0 */
	0,			/* 0x2D: timer/counter 1 */
	14,			/* 0x2E: uncorrectable ECC errors */
	14,			/* 0x2F: correctable ECC errors */
	14,			/* 0x30: PCI bus A error */
	14,			/* 0x31: PCI bus B error */
	14			/* 0x32: power management wakeup */
};
/*
 * function prototypes for bus ops routines:
 */
static int
pci_map(dev_info_t *dip, dev_info_t *rdip, ddi_map_req_t *mp,
	off_t offset, off_t len, caddr_t *addrp);
static ddi_intrspec_t
pci_get_intrspec(dev_info_t *dip, dev_info_t *rdip, u_int inumber);
static int
pci_add_intrspec(dev_info_t *dip, dev_info_t *rdip,
	ddi_intrspec_t intrspec,
	ddi_iblock_cookie_t *iblock_cookiep,
	ddi_idevice_cookie_t *idevice_cookiep,
	u_int (*int_handler)(caddr_t int_handler_arg), caddr_t int_handler_arg,
	int kind);
static void
pci_remove_intrspec(dev_info_t *dip, dev_info_t *rdip,
	ddi_intrspec_t intrspec, ddi_iblock_cookie_t iblock_cookie);
static int
pci_dma_map(dev_info_t *dip, dev_info_t *rdip,
	struct ddi_dma_req *dmareq, ddi_dma_handle_t *handlep);
static int
pci_dma_allochdl(dev_info_t *dip, dev_info_t *rdip, ddi_dma_attr_t *attrp,
	int (*waitfp)(caddr_t), caddr_t arg, ddi_dma_handle_t *handlep);
static int
pci_dma_freehdl(dev_info_t *dip, dev_info_t *rdip,
	ddi_dma_handle_t handle);
static int
pci_dma_bindhdl(dev_info_t *dip, dev_info_t *rdip,
	ddi_dma_handle_t handle, struct ddi_dma_req *dmareq,
	ddi_dma_cookie_t *cookiep, u_int *ccountp);
static int
pci_dma_unbindhdl(dev_info_t *dip, dev_info_t *rdip,
	ddi_dma_handle_t handle);
static int
pci_dma_flush(dev_info_t *dip, dev_info_t *rdip,
	ddi_dma_handle_t handle, off_t off, u_int len,
	u_int cache_flags);
static int
pci_dma_win(dev_info_t *dip, dev_info_t *rdip,
	ddi_dma_handle_t handle, uint_t win, off_t *offp,
	uint_t *lenp, ddi_dma_cookie_t *cookiep, uint_t *ccountp);
static int
pci_dma_mctl(dev_info_t *dip, dev_info_t *rdip,
	ddi_dma_handle_t handle, enum ddi_dma_ctlops request,
	off_t *offp, u_int *lenp,
	caddr_t *objp, u_int cache_flags);
static int
pci_ctlops(dev_info_t *dip, dev_info_t *rdip,
	ddi_ctl_enum_t op, void *arg, void *result);

/*
 * function prototypes for dev ops routines:
 */
static int pci_identify(dev_info_t *dip);
static int pci_attach(dev_info_t *dip, ddi_attach_cmd_t cmd);
static int pci_detach(dev_info_t *dip, ddi_detach_cmd_t cmd);

/*
 * bus ops and dev ops structures:
 */
static struct bus_ops pci_bus_ops = {
	BUSO_REV,
	pci_map,
	pci_get_intrspec,
	pci_add_intrspec,
	pci_remove_intrspec,
	i_ddi_map_fault,
	pci_dma_map,
	pci_dma_allochdl,
	pci_dma_freehdl,
	pci_dma_bindhdl,
	pci_dma_unbindhdl,
	pci_dma_flush,
	pci_dma_win,
	pci_dma_mctl,
	pci_ctlops,
	ddi_bus_prop_op
};

static struct dev_ops pci_ops = {
	DEVO_REV,
	0,
	ddi_no_info,
	pci_identify,
	0,
	pci_attach,
	pci_detach,
	nodev,
	(struct cb_ops *)0,
	&pci_bus_ops
};

/*
 * function prototypes for fast dvma ops:
 */
void
fast_dvma_kaddr_load(ddi_dma_handle_t h, caddr_t a, u_int len, u_int index,
	ddi_dma_cookie_t *cp);
void
fast_dvma_unload(ddi_dma_handle_t h, u_int index, u_int view);
void
fast_dvma_sync(ddi_dma_handle_t h, u_int index, u_int view);

/*
 * fast dvma ops structure:
 */
static struct dvma_ops fast_dvma_ops = {
	DVMAO_REV,
	fast_dvma_kaddr_load,
	fast_dvma_unload,
	fast_dvma_sync
};

/*
 * module definitions:
 */
#include <sys/modctl.h>
extern struct mod_ops mod_driverops;

static struct modldrv modldrv = {
	&mod_driverops, 	/* Type of module.  This one is a driver */
	"PCI Bus nexus driver",	/* Name of module. */
	&pci_ops,		/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modldrv, NULL
};

/*
 * forward declarations:
 */
static u_int pci_intr_wrapper(caddr_t arg);
static int init_child(dev_info_t *child);
static int report_dev(dev_info_t *dip);

/*
 * forward declarations (functions used by attach & detach dev ops):
 */
static int map_pbm_regs(pci_devstate_t *pci_p, dev_info_t *dip);
static void unmap_pbm_regs(pci_devstate_t *pci_p);
static void set_psycho_regs_addr(psycho_devstate_t *p, caddr_t a);
static int add_pbm_intrs(pci_devstate_t *pci_p);
static void remove_pbm_intrs(pci_devstate_t *pci_p);
static int add_psycho_intrs(psycho_devstate_t *psycho_p);
static void remove_psycho_intrs(psycho_devstate_t *psycho_p);
static void configure_pbm(pci_devstate_t *pci_p);
static void configure_psycho(psycho_devstate_t *psycho_p);
static void alloc_sync_buf(pci_devstate_t *pci_p);
static void free_sync_buf(pci_devstate_t *pci_p);
static void create_pokefault_mutex(pci_devstate_t *pci_p);
static void destroy_pokefault_mutex(pci_devstate_t *pci_p);
static void init_intr_info(psycho_devstate_t *psycho_p);
static void dvma_rmap_init(psycho_devstate_t *psycho_p);
static void iommu_init(psycho_devstate_t *psycho_p);

/*
 * forward declarations (interrupt handlers):
 */
static u_int pci_error_intr(caddr_t a);
static u_int psycho_ue_intr(caddr_t a);
static u_int psycho_ce_intr(caddr_t a);
static u_int psycho_log_ue_error(struct ecc_flt *ecc, char *unum);
static u_int psycho_log_ce_error(struct ecc_flt *ecc, char *unum);

/*
 * forward declarations (functions used by bus map op):
 */
static int
xlate_reg_prop(pci_devstate_t *pci_p, dev_info_t *dip, pci_regspec_t *pci_rp,
#if defined(ACC_ENABLE)
	off_t off, off_t len, struct regspec *rp, u_int acc_enable);
#else
	off_t off, off_t len, struct regspec *rp);
#endif
static int
get_addr(pci_devstate_t *pci_p, dev_info_t *dip, pci_regspec_t *pci_rp,
	u_int *phys_low_p);
static int
get_reg_set(pci_devstate_t *pci_p, dev_info_t *child, int rnumber,
#if defined(ACC_ENABLE)
	off_t off, off_t len, struct regspec *rp, u_int acc_enable);
#else
	off_t off, off_t len, struct regspec *rp);
#endif

/*
 * forward declarations (functions used by bus add intrspec op):
 */
static u_int
xlate_interrupt(dev_info_t *dip, dev_info_t *rdip, u_int device, u_int intr);
static u_int
iline_to_pil(pci_devstate_t *pci_p, dev_info_t *child,
	u_int intr, u_int device);
static u_int
get_reg_set_size(dev_info_t *child, int rnumber);
static u_int
get_nreg_set(dev_info_t *child);
static u_int
get_nintr(dev_info_t *child);
static int check_limits(pci_devstate_t *pci_p,
	struct ddi_dma_req *dmareq, u_int *sizep);
static int check_dma_attr(pci_devstate_t *pci_p, ddi_dma_attr_t *attr,
	psycho_dma_t *dma_type);
static int
check_dma_size(pci_devstate_t *pci_p, ddi_dma_handle_t handle,
	struct ddi_dma_req *dmareq, u_int *sizep);
static int
check_dma_target(pci_devstate_t *pci_p,
	u_long vaddr, struct as *as, u_int size, u_int offset,
	psycho_dma_t *dma_type, ddi_dma_impl_t *mp);
static u_long
get_dvma_pages(pci_devstate_t *pci_p, int npages,
	u_long addrlo, u_long addrhi, int cansleep);
static void
map_window(pci_devstate_t *pci_p, ddi_dma_impl_t *mp, u_int window);
static void
unmap_window(pci_devstate_t *pci_p, ddi_dma_impl_t *mp);
static void
sbuf_flush(pci_devstate_t *pci_p, ddi_dma_impl_t *mp,
	off_t offset, u_int length);
static int
create_bypass_cookies(pci_devstate_t *pci_p, ddi_dma_impl_t *mp,
	ddi_dma_cookie_t *cookiep, u_int *ccountp);


/*
 * driver global data:
 */
static void *per_pci_state;		/* per-pbm soft state pointer */
static void *per_psycho_state;		/* per-psycho soft state pointer */
static kmutex_t psycho_state_mutex;	/* psycho soft state mutex */


int
_init(void)
{
	char *mutex_name = "psycho soft state mutex";
	int e;

	/*
	 * Initialize per-pci bus soft state pointer.
	 */
	e = ddi_soft_state_init(&per_pci_state,
				sizeof (pci_devstate_t), 1);
	if (e != 0)
		return (e);

	/*
	 * Initialize per-psycho soft state pointer.
	 */
	e = ddi_soft_state_init(&per_psycho_state,
				sizeof (psycho_devstate_t), 1);
	if (e != 0) {
		ddi_soft_state_fini(&per_pci_state);
		return (e);
	}

	/*
	 * Initialize the psycho soft state mutex.
	 */
	mutex_init(&psycho_state_mutex, mutex_name, MUTEX_DRIVER, NULL);

	/*
	 * Install the module.
	 */
	e = mod_install(&modlinkage);
	if (e != 0) {
		ddi_soft_state_fini(&per_pci_state);
		ddi_soft_state_fini(&per_psycho_state);
		mutex_destroy(&psycho_state_mutex);
	}
	return (e);
}

int
_fini(void)
{
	int e;

	/*
	 * Remove the module.
	 */
	e = mod_remove(&modlinkage);
	if (e != 0)
		return (e);

	/*
	 * Free the per-pci and per-psycho soft state info and destroy
	 * mutex for per-psycho soft state.
	 */
	ddi_soft_state_fini(&per_pci_state);
	ddi_soft_state_fini(&per_psycho_state);
	mutex_destroy(&psycho_state_mutex);
	return (e);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

/* device driver entry points */

/*
 * identify entry point:
 *
 * Identifies with nodes named "pci" and "SUNW,pci".
 */
static int
pci_identify(dev_info_t *dip)
{
	char *name = ddi_get_name(dip);
	int rc = DDI_NOT_IDENTIFIED;

	DBG1(D_IDENTIFY, NULL, "trying dip=%x\n", dip);
	if (strcmp(name, "pci") == 0 || strcmp(name, "SUNW,pci") == 0) {
		DBG1(D_IDENTIFY, NULL, "identified dip=%x\n", dip);
		rc = DDI_IDENTIFIED;
	}
	return (rc);
}

/*
 * attach entry point:
 *
 * normal attach:
 *	get the PBM upa-id from device node
 *	allocate and get per-PBM soft state structure
 *	cache bus-range property for PBM in per-PBM soft state structure
 *	allocate sync buffer and mutex for i/ocache flushing
 *	map in PBM control registers
 *	if per-Psycho soft state not allocated
 *		allocate and get per-Psycho soft state structure
 *		map in Psycho control registers
 *		initialize ino soft structures
 *		configure Psycho control registers
 *		initialize IOMMU and corresponding structure
 *		mark per-Psycho soft state as `attached'
 *	link per-Psycho soft state to per-PBM soft state
 *	link per-PBM soft state to per-Psycho soft state
 *	configure PBM control registers
 *	mark per-PBM soft state as `attached'
 *
 * resume attach:
 *	get per-PBM soft state structure
 *	get per-Psycho soft state structure
 *	if Psycho has not be resumed
 *		configure Psycho control registers
 *		mark per-Psycho soft state as `resumed'
 *	configure PBM control registers
 *	mark per-PBM soft state as `resumed'
 */
static int
pci_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	pci_devstate_t *pci_p;		/* per pci state pointer */
	psycho_devstate_t *psycho_p;	/* per psycho state pointer */
	int instance, upa_id, i;

	switch (cmd) {
	case DDI_ATTACH:

		/*
		 * Get the device's upa id.
		 */
		instance = ddi_get_instance(dip);
		upa_id = ddi_getprop(DDI_DEV_T_ANY, dip,
				DDI_PROP_DONTPASS, "upa-portid", -1);
		if (upa_id == -1) {
			cmn_err(CE_WARN, "%s%d: no upa-portid property\n",
				ddi_get_name(dip), instance);
			ddi_soft_state_free(dip, instance);
			return (DDI_FAILURE);
		}

		/*
		 * Allocate and get the per-pci soft state structure.
		 */
		if (alloc_pci_soft_state(instance) != DDI_SUCCESS) {
			cmn_err(CE_WARN, "%s%d: can't allocate pci state\n",
				ddi_get_name(dip), instance);
			return (DDI_FAILURE);
		}
		pci_p = get_pci_soft_state(instance);
		pci_p->dip = dip;
		pci_p->upa_id = upa_id;

		/*
		 * Get the bus-ranges property.
		 */
		i = sizeof (pci_p->bus_range);
		if (ddi_getlongprop_buf(DDI_DEV_T_NONE, dip,
					DDI_PROP_DONTPASS, "bus-range",
					(caddr_t)&pci_p->bus_range,
					&i) != DDI_SUCCESS) {
			cmn_err(CE_WARN, "%s%d: no bus-range property\n",
				ddi_get_name(dip), instance);
			free_pci_soft_state(instance);
			return (DDI_FAILURE);
		}
		DBG2(D_ATTACH, pci_p, "bus-range (%x,%x)\n",
			pci_p->bus_range.lo, pci_p->bus_range.hi);

		/*
		 * Map in the PBM's device registers.
		 */
		if (!map_pbm_regs(pci_p, dip)) {
			free_pci_soft_state(instance);
			return (DDI_FAILURE);
		}

		/*
		 * See if a per-psycho soft state structure has been
		 * already been allocated by our peer pci bus.
		 */
		mutex_enter(&psycho_state_mutex);
		psycho_p = get_psycho_soft_state(upa_id);
		if (psycho_p == NULL) {

			/*
			 * Allocate the per-psycho soft state structure.
			 */
			if (alloc_psycho_soft_state(upa_id) != DDI_SUCCESS) {
				cmn_err(CE_WARN,
					"%s%d: can't allocate psycho state\n",
					ddi_get_name(dip), instance);
				unmap_pbm_regs(pci_p);
				free_pci_soft_state(instance);
				mutex_exit(&psycho_state_mutex);
				return (DDI_FAILURE);
			}
			psycho_p = get_psycho_soft_state(upa_id);
			psycho_p->dip = dip;
			psycho_p->upa_id = upa_id;
			set_psycho_regs_addr(psycho_p, pci_p->address[2]);

			/*
			 * Initialize the intr structures, configure the
			 * psycho chip control parameters, allocate the
			 * dvma resource map and initialize the iommu.
			 */
			init_intr_info(psycho_p);
			if (!add_psycho_intrs(psycho_p)) {
				cmn_err(CE_WARN,
					"%s%d: can't add ecc interrupts\n",
					ddi_get_name(dip), instance);
				unmap_pbm_regs(pci_p);
				free_pci_soft_state(instance);
				mutex_exit(&psycho_state_mutex);
				free_psycho_soft_state(upa_id);
				return (DDI_FAILURE);
			}
			configure_psycho(psycho_p);
			iommu_init(psycho_p);
			dvma_rmap_init(psycho_p);
			psycho_p->state = ATTACHED;
		}
		mutex_exit(&psycho_state_mutex);

		/*
		 * Link the per-psycho soft state and per-pci
		 * soft state.
		 */
		if (pci_p->bus_range.lo == 0)
			psycho_p->pci_a_p = pci_p;
		else
			psycho_p->pci_b_p = pci_p;
		pci_p->psycho_p = psycho_p;

		/*
		 * Add the pci error interrupt handler for the PBM.
		 */
		if (!add_pbm_intrs(pci_p)) {
			cmn_err(CE_WARN,
				"%s%d: can't add bus error interrupt\n",
				ddi_get_name(dip), instance);
			unmap_pbm_regs(pci_p);
			free_pci_soft_state(instance);
			remove_psycho_intrs(psycho_p);
			rmfreemap(psycho_p->dvma_map);
			free_psycho_soft_state(upa_id);
			return (DDI_FAILURE);
		}

		/*
		 * Allocate the i/o cache flush/sync buffer and initialize
		 * it's mutex.
		 */
		alloc_sync_buf(pci_p);

		create_pokefault_mutex(pci_p);
		configure_pbm(pci_p);
		pci_p->state = ATTACHED;

		ddi_report_dev(dip);
		return (DDI_SUCCESS);

	case DDI_RESUME:

		/*
		 * Make sure the Psycho control registers and IOMMU
		 * are configured properly.
		 */
		pci_p = get_pci_soft_state(ddi_get_instance(dip));
		DBG1(D_ATTACH, pci_p, "DDI_RESUME dip=%x\n", dip);
		mutex_enter(&psycho_state_mutex);
		psycho_p = pci_p->psycho_p;
		if (psycho_p->state != RESUMED) {
			configure_psycho(psycho_p);
			psycho_p->state = RESUMED;
		}
		mutex_exit(&psycho_state_mutex);

		/*
		 * And do the same for the PBM control registers.
		 */
		configure_pbm(pci_p);
		pci_p->state = RESUMED;
		return (DDI_SUCCESS);
	}
	return (DDI_FAILURE);
}

/*
 * detach entry point:
 *
 * normal detach:
 *	unmap PBM device registers
 *	destroy mutex and free buffer syncing flushes
 *	if last per-PBM attached to per-Psycho soft state
 *		unmap Psycho control registers
 *		free IOMMU structures
 *		remove per-Psycho soft state
 *	free per-PBM soft state
 *
 * suspend and pm suspend detach:
 *	mark per-PBM soft state as `suspended'
 *	mark per-Psycho soft state as `suspended'
 */
static int
pci_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	int instance = ddi_get_instance(dip);
	pci_devstate_t *pci_p = get_pci_soft_state(instance);
	psycho_devstate_t *psycho_p = pci_p->psycho_p;
	int detach_psycho;

	switch (cmd) {
	case DDI_DETACH:

		/*
		 * Delete the mappings to the pci registers, free the i/o
		 * cache sync mutex and sync buffer.
		 */
		DBG1(D_DETACH, pci_p, "DDI_DETACH dip=%x\n", dip);
		remove_pbm_intrs(pci_p);
		unmap_pbm_regs(pci_p);
		free_sync_buf(pci_p);
		destroy_pokefault_mutex(pci_p);

		/*
		 * Examine our per-psycho soft state to see if we are the
		 * last per-pci soft state active for it.  If so detach it.
		 */
		mutex_enter(&psycho_state_mutex);
		if (pci_p->bus_range.lo == 0) {
			psycho_p->pci_a_p = NULL;
			detach_psycho = (psycho_p->pci_b_p == NULL);
		} else {
			psycho_p->pci_b_p = NULL;
			detach_psycho = (psycho_p->pci_a_p == NULL);
		}
		if (detach_psycho) {
			remove_psycho_intrs(psycho_p);
			rmfreemap(psycho_p->dvma_map);
			free_psycho_soft_state(pci_p->upa_id);
		}
		mutex_exit(&psycho_state_mutex);

		/*
		 * And finally free the per-pci soft state.
		 */
		free_pci_soft_state(instance);
		return (DDI_SUCCESS);

	case DDI_SUSPEND:
		DBG1(D_DETACH, pci_p, "DDI_SUSPEND dip=%x\n", dip);
		pci_p->state = SUSPENDED;
		mutex_enter(&psycho_state_mutex);
		psycho_p->state = SUSPENDED;
		mutex_exit(&psycho_state_mutex);
		return (DDI_SUCCESS);

	case DDI_PM_SUSPEND:
		DBG1(D_DETACH, pci_p, "DDI_PM_SUSPEND dip=%x\n", dip);
		pci_p->state = PM_SUSPENDED;
		mutex_enter(&psycho_state_mutex);
		psycho_p->state = PM_SUSPENDED;
		mutex_exit(&psycho_state_mutex);
		return (DDI_SUCCESS);
	}
	return (DDI_FAILURE);
}

/* bus driver entry points */

/*
 * bus map entry point:
 *
 * 	if map request is for an rnumber
 *		get the corresponding regspec from device node
 * 	build a new regspec in our parent's format
 *	build a new map_req with the new regspec
 *	call up the tree to complete the mapping
 */
static int
pci_map(dev_info_t *dip, dev_info_t *rdip, ddi_map_req_t *mp,
	off_t off, off_t len, caddr_t *addrp)
{
	pci_devstate_t *pci_p = get_pci_soft_state(ddi_get_instance(dip));
	struct regspec regspec;
	ddi_map_req_t p_map_request;
	int rnumber;
	int rval;

	/*
	 * User level mappings are not supported yet.
	 */
	if (mp->map_flags & DDI_MF_USER_MAPPING) {
		DBG2(D_MAP, pci_p, "rdip=%s%d: no user level mappings yet!\n",
			ddi_get_name(rdip), ddi_get_instance(rdip));
		return (DDI_ME_UNIMPLEMENTED);
	}

	/*
	 * Now handle the mapping according to its type.
	 */
	switch (mp->map_type) {
	case DDI_MT_REGSPEC:

		/*
		 * We assume the register specification is in PCI format.
		 * We must convert it into a regspec of our parent's
		 * and pass the request to our parent.
		 */
		DBG3(D_MAP, pci_p, "rdip=%s%d: REGSPEC - handlep=%x\n",
			ddi_get_name(rdip), ddi_get_instance(rdip),
			mp->map_handlep);
		rval = xlate_reg_prop(pci_p, rdip,
					(pci_regspec_t *)mp->map_obj.rp,
#if defined(ACC_ENABLE)
					off, len, &regspec, pci_acc_enable);
#else
					off, len, &regspec);
#endif
		break;

	case DDI_MT_RNUMBER:

		/*
		 * Get the "reg" property from the device node and convert
		 * it to our parent's format.
		 */
		DBG4(D_MAP, pci_p, "rdip=%s%d: rnumber=%x handlep=%x\n",
			ddi_get_name(rdip), ddi_get_instance(rdip),
			mp->map_obj.rnumber, mp->map_handlep);
		rnumber = mp->map_obj.rnumber;
		if (rnumber < 0)
			return (DDI_ME_RNUMBER_RANGE);
		rval = get_reg_set(pci_p, rdip,  rnumber, off, len,
#if defined(ACC_ENABLE)
					&regspec, pci_acc_enable);
#else
					&regspec);
#endif
		break;

	default:
		return (DDI_ME_INVAL);

	}
	if (rval != DDI_SUCCESS)
		return (rval);

	/*
	 * Now we have a copy of the PCI regspec converted to our parent's
	 * format.  Build a new map request based on this regspec and pass
	 * it to our parent.
	 */
	p_map_request = *mp;
	p_map_request.map_type = DDI_MT_REGSPEC;
	p_map_request.map_obj.rp = &regspec;
	return (ddi_map(dip, &p_map_request, 0, 0, addrp));
}

/*
 * bus get intrspec entry point:
 */
static ddi_intrspec_t
pci_get_intrspec(dev_info_t *dip, dev_info_t *rdip, u_int inumber)
{
	pci_devstate_t *pci_p = get_pci_soft_state(ddi_get_instance(dip));
	pci_regspec_t *pci_rp;
	int *pci_ip;
	int *intr_prio_p;
	struct intrspec *ispecp;
	u_int mondo, ino;
	u_int pil = 0;
	u_int phys_hi;
	u_int bus, device;
	u_int intr;
	int i;

	/*
	 * Get the requested inumber from the device by checking the
	 * interrupts property.
	 */
	if (ddi_getlongprop(DDI_DEV_T_NONE, rdip, DDI_PROP_DONTPASS,
	    "interrupts", (caddr_t)&pci_ip, &i) != DDI_SUCCESS)
		return ((ddi_intrspec_t)0);
	if (inumber >= (i / (int)sizeof (u_int)))
		return ((ddi_intrspec_t)0);
	intr = pci_ip[inumber];
	kmem_free((caddr_t)pci_ip, i);
	DBG3(D_G_ISPEC, pci_p, "rdip=%s%d intr=%x\n",
		ddi_get_name(rdip), ddi_get_instance(rdip), intr);

#if defined(INTERRUPT_PRIORITIES)
	/*
	 * Use the "interrupt-priorities" property to determine the
	 * the pil/ipl for the interrupt handler.
	 */
	if (ddi_getlongprop(DDI_DEV_T_NONE, rdip, DDI_PROP_DONTPASS,
				"interrupt-priorities", (caddr_t)&intr_prio_p,
				&i) == DDI_SUCCESS) {
		if (inumber < (i / (int)sizeof (u_int)))
			pil = intr_prio_p[inumber];
		DBG3(D_G_ISPEC, pci_p,
			"rdip=%s%d - using interrupt-priorities prop, pil=%x\n",
			ddi_get_name(rdip), ddi_get_instance(rdip), pil);
		kmem_free((caddr_t)intr_prio_p, i);
	}
#endif

	/*
	 * The interrupt property can be one of the the following:
	 *
	 *	a 1275 property (ie 1-4 for #A, #B, #C, #D)
	 *
	 *	a UPA mondo
	 *
	 * Convert it to an ispec with our parents format:
	 *
	 *	intrspec_pri - pil for handler
	 *	intrspec_vec - mondo
	 */
	switch (intr) {
	case PCI_INTA:
	case PCI_INTB:
	case PCI_INTC:
	case PCI_INTD:

		/*
		 * Use the devices reg property to determine it's PCI bus
		 * number and device number.
		 */
		if (ddi_getlongprop(DDI_DEV_T_NONE, rdip, DDI_PROP_DONTPASS,
					"reg", (caddr_t)&pci_rp, &i)
						!= DDI_SUCCESS)
			return ((ddi_intrspec_t)0);

		phys_hi = pci_rp[0].pci_phys_hi;
		bus = PCI_REG_BUS_G(phys_hi);
		device = PCI_REG_DEV_G(phys_hi);
		intr = xlate_interrupt(dip, rdip, device, intr);
		mondo = MAKE_MONDO(pci_p->upa_id, bus, device, intr);
		ino = MONDO_TO_INO(mondo);
#if defined(INTERRUPT_PRIORITIES)
		if (pil == 0)
			pil = iline_to_pil(pci_p, rdip, intr, phys_hi);
#else
		pil = iline_to_pil(pci_p, rdip, intr, phys_hi);
#endif
		kmem_free((caddr_t)pci_rp, i);
		break;

	default:

		/*
		 * The interrupt property is a mondo.  Make sure it is valid.
		 */
		mondo = intr;
		ino = MONDO_TO_INO(mondo);
#if defined(INTERRUPT_PRIORITIES)
		if (pil == 0)
			pil = ino_to_pil[ino];
#else
		pil = ino_to_pil[ino];
#endif
	}
	ispecp = &pci_p->psycho_p->ino_info[ino].intrspec;
	ispecp->intrspec_pri = pil;
	ispecp->intrspec_vec = mondo;
	DBG3(D_G_ISPEC, pci_p, "interrupt (%x) PCI (%x,%x)\n",
		intr, ispecp->intrspec_vec, ispecp->intrspec_pri);
	return ((ddi_intrspec_t)ispecp);
}

/*
 * bus add intrspec entry point:
 */
static int
pci_add_intrspec(dev_info_t *dip, dev_info_t *rdip,
	ddi_intrspec_t intrspec,
	ddi_iblock_cookie_t *iblock_cookiep,
	ddi_idevice_cookie_t *idevice_cookiep,
	u_int (*int_handler)(caddr_t int_handler_arg),
	caddr_t int_handler_arg,
	int kind)
{
	pci_devstate_t *pci_p = get_pci_soft_state(ddi_get_instance(dip));
	psycho_devstate_t *psycho_p;
	psycho_ino_info_t *ino_p;
	struct intrspec *ip = (struct intrspec *)intrspec;
	psycho_intr_req_t *irp;
	ddi_idevice_cookie_t idevice_cookie;
	char mutex_name[64];
	u_int ino, upa_id;
	int i;
	extern int getprocessorid();

	psycho_p = pci_p->psycho_p;
	switch (kind) {
	case IDDI_INTR_TYPE_NORMAL:

		/*
		 * Make sure the ino is valid.
		 */
		ino = MONDO_TO_INO(ip->intrspec_vec);
		upa_id = MONDO_TO_UPA_ID(ip->intrspec_vec);
		DBG3(D_A_ISPEC, pci_p,
			"rdip=%s%d - IDDI_INTR_TYPE_NORMAL, ino=%x\n",
			ddi_get_name(rdip), ddi_get_instance(rdip), ino);
		if (upa_id != pci_p->upa_id) {
			DBG1(D_A_ISPEC, pci_p, "mondo has bad upa id %x\n",
				upa_id);
			return (DDI_INTR_NOTFOUND);
		}
		if ((INO_TO_BUS(ino) == 'A' && INO_TO_SLOT(ino) > 1) ||
				ino > PSYCHO_MAX_INO) {
			DBG1(D_A_ISPEC, pci_p, "ino %x is invalid\n", ino);
			return (DDI_INTR_NOTFOUND);
		}

		/*
		 * The first level mutex (mutex1) is sufficient
		 * for locking the list until the first handler is
		 * installed.
		 */
		ino_p = &psycho_p->ino_info[ino];
		mutex_enter(&ino_p->mutex1);

		switch (ino_p->state) {
		case INO_FREE:

			/*
			 * Allocate and initialize the interrupt request
			 * structure.
			 */
			irp = kmem_alloc(sizeof (psycho_intr_req_t), KM_SLEEP);
			irp->dip = rdip;
			irp->handler = int_handler;
			irp->handler_arg = int_handler_arg;
			irp->next = irp;
			ino_p->state = INO_SINGLE;
			ino_p->head = irp;
			ino_p->tail = irp;
			ino_p->start = irp;
			ino_p->size = 1;

			/*
			 * Save the iblock cookie for further requests for
			 * this ino and program the caller's iblock cookie
			 * if necessary.
			 */
			ino_p->iblock_cookie = ipltospl(ip->intrspec_pri);
			if (iblock_cookiep)
				*iblock_cookiep = (ddi_iblock_cookie_t)
					ino_p->iblock_cookie;

			/*
			 * Program the device cookie.
			 */
			idevice_cookie.idev_vector = ip->intrspec_vec;
			idevice_cookie.idev_priority = ino_p->iblock_cookie;
			ino_p->idevice_cookie = idevice_cookie.idev_softint;
			if (idevice_cookiep)
				idevice_cookiep->idev_softint =
					ino_p->idevice_cookie;
			DBG2(D_A_ISPEC, pci_p, "iblock %x idevice %x\n",
				ino_p->iblock_cookie, ino_p->idevice_cookie);

			/*
			 * Create the second level mutex (mutex2).  This
			 * mutex is used to lock additions/removals from a
			 * list of handlers and the interrupt service routine.
			 */
			(void) sprintf(mutex_name,
					"mutex2 for upa-id %x ino %x",
					psycho_p->upa_id, ino);
			mutex_init(&ino_p->mutex2, mutex_name,
					MUTEX_DRIVER,
					(void *)ino_p->iblock_cookie);

			/*
			 * Install the nexus driver interrupt handler for
			 * this INO.
			 */
			add_ivintr(ip->intrspec_vec, ip->intrspec_pri,
					pci_intr_wrapper, (caddr_t)ino_p,
					&ino_p->mutex2);

			mutex_exit(&ino_p->mutex1);

			/*
			 * Enable the interrupt through its interrupt mapping
			 * register.
			 *
			 * Currently we target the interrupt to the processor
			 * which * happens to be installing it.  Is there a
			 * better way to balance this for MP systems?
			 */
			*ino_p->clear_reg =
				(*ino_p->clear_reg & ~PSYCHO_CIR_MASK) |
						PSYCHO_CIR_IDLE;
			*ino_p->map_reg = PSYCHO_IMR_VALID |
			    ((u_int) getprocessorid() << PSYCHO_IMR_TID_SHIFT);
			break;

		case INO_SINGLE:
		case INO_SHARED:

			/*
			 * Grap the second level mutex (mutex2) since a list
			 * of handlers is already present.
			 */
			mutex_enter(&ino_p->mutex2);
			DBG1(D_A_ISPEC, pci_p, "list had %d entries\n",
				ino_p->size);

			/*
			 * Scan the handler list to make sure an entry with
			 * this dip isn't already installed.
			 */
			irp = ino_p->head;
			for (i = 0; i < ino_p->size; i++) {
				if (irp->dip == rdip) {
					DBG(D_A_ISPEC, pci_p, "duplicate\n");
					mutex_exit(&ino_p->mutex2);
					mutex_exit(&ino_p->mutex1);
					return (DDI_FAILURE);
				}
				irp = irp->next;
			}

			/*
			 * Install the new handler at the end of the list.
			 */
			irp = kmem_alloc(sizeof (psycho_intr_req_t), KM_SLEEP);
			irp->dip = rdip;
			irp->handler = int_handler;
			irp->handler_arg = int_handler_arg;
			irp->next = ino_p->head;
			ino_p->tail->next = irp;
			ino_p->tail = irp;
			ino_p->size++;
			ino_p->state = INO_SHARED;
			ino_p->start = ino_p->head;

			/*
			 * Program the iblock and idevice cookies.
			 */
			if (iblock_cookiep)
				*iblock_cookiep = (ddi_iblock_cookie_t)
					ino_p->iblock_cookie;
			if (idevice_cookiep)
				idevice_cookiep->idev_softint =
					ino_p->idevice_cookie;
			DBG2(D_A_ISPEC, pci_p, "iblock %x idevice %x\n",
				ino_p->iblock_cookie, ino_p->idevice_cookie);

			mutex_exit(&ino_p->mutex2);
			mutex_exit(&ino_p->mutex1);
		}
		break;

	default:
		DBG2(D_A_ISPEC, pci_p, "rdip=%s%d - unsupported type\n",
			ddi_get_name(rdip), ddi_get_instance(rdip));
		return (DDI_FAILURE);
	}
	return (DDI_SUCCESS);
}

/*
 * bus remove intrspec entry point
 */
/*ARGSUSED*/
static void
pci_remove_intrspec(dev_info_t *dip, dev_info_t *rdip,
	ddi_intrspec_t intrspec, ddi_iblock_cookie_t iblock_cookie)
{
	pci_devstate_t *pci_p = get_pci_soft_state(ddi_get_instance(dip));
	psycho_devstate_t *psycho_p;
	psycho_ino_info_t *ino_p;
	struct intrspec *ip = (struct intrspec *)intrspec;
	psycho_intr_req_t *irp, *irp2;
	u_int ino;
	int i;

	/*
	 * Make sure the ino is valid.
	 */
	psycho_p = pci_p->psycho_p;
	ino = MONDO_TO_INO(ip->intrspec_vec);
	DBG3(D_R_ISPEC, pci_p, "rdip=%s%d, ino=%x\n",
		ddi_get_name(rdip), ddi_get_instance(rdip), ino);
	if ((INO_TO_BUS(ino) == 'A' && INO_TO_SLOT(ino) > 1) ||
			ino > PSYCHO_MAX_INO) {
		DBG1(D_A_ISPEC, pci_p, "ino %x is invalid\n", ino);
		return;
	}

	/*
	 * Get both level of mutex locks to prevent removal while the
	 * service routine is active.
	 */
	ino_p = &psycho_p->ino_info[ino];
	mutex_enter(&ino_p->mutex1);
	mutex_enter(&ino_p->mutex2);

	switch (ino_p->state) {
	case INO_SINGLE:

		/*
		 * Remove the interrupt request from the list.
		 */
		DBG(D_R_ISPEC, pci_p, "INO_SINGLE\n");
		irp = ino_p->head;
		if (irp->dip != rdip) {
			DBG1(D_R_ISPEC, pci_p, "dip %x not found\n", rdip);
			mutex_exit(&ino_p->mutex2);
			mutex_exit(&ino_p->mutex1);
			return;
		}
		ino_p->head = (psycho_intr_req_t *)0;
		ino_p->tail = (psycho_intr_req_t *)0;
		ino_p->start = (psycho_intr_req_t *)0;
		ino_p->size = 0;
		ino_p->state = INO_FREE;

		/*
		 * Before removing the handler, make sure we disable
		 * the interrupt.
		 */
		*ino_p->map_reg &= ~PSYCHO_IMR_VALID;

		/*
		 * Call up to our parent to handle the removal and mark
		 * that the ino now has no handler.
		 */
		rem_ivintr(ip->intrspec_vec, (struct intr_vector *)NULL);
		break;

	case INO_SHARED:

		/*
		 * Search the interrupt request list to for an entry with this
		 * dip.  If found remove it.
		 */
		DBG(D_R_ISPEC, pci_p, "INO_SHARED\n");
		irp = ino_p->head;
		irp2 = ino_p->tail;
		for (i = 0; i < ino_p->size; i++) {
			DBG1(D_R_ISPEC, pci_p, "entry dip=%x\n", irp->dip);
			if (irp->dip == rdip)
				break;
			irp2 = irp;
			irp = irp->next;
		}
		if (i == ino_p->size) {
			DBG1(D_R_ISPEC, pci_p, "dip %x not found\n", rdip);
			mutex_exit(&ino_p->mutex2);
			mutex_exit(&ino_p->mutex1);
			return;
		}

		/*
		 * Now remove the entry from the list.
		 */
		DBG(D_R_ISPEC, pci_p, "found - removing\n");
		if (irp == ino_p->head) {
			ino_p->head = irp->next;
			DBG1(D_R_ISPEC, pci_p, "head (new head is %x)\n",
				ino_p->head);
		}
		if (irp == ino_p->tail) {
			ino_p->tail = irp2;
			DBG1(D_R_ISPEC, pci_p, "tail (new tail is %x)\n",
				ino_p->tail);
		}
		irp2->next = irp->next;
		ino_p->start = ino_p->head;
		ino_p->size--;
		if (ino_p->size == 1) {
			DBG(D_R_ISPEC, pci_p, "state changed to INO_SINGLE\n");
			ino_p->state = INO_SINGLE;
		}
		break;
	}
	mutex_exit(&ino_p->mutex2);
	mutex_exit(&ino_p->mutex1);

	/*
	 * Free the memory held by the interrupt request.
	 */
	if (irp)
		kmem_free(irp, sizeof (psycho_intr_req_t));
}

/*
 * bus dma map entry point:
 *
 * This interface supports only dvma and peer to peer mappings.
 * iommu bypass mappings are available to allochdl/bindhdl
 * interfaces.
 */
static int
pci_dma_map(dev_info_t *dip, dev_info_t *rdip,
	struct ddi_dma_req *dmareq, ddi_dma_handle_t *handlep)
{
	pci_devstate_t *pci_p = get_pci_soft_state(ddi_get_instance(dip));
	ddi_dma_impl_t *mp = NULL;
	psycho_dma_t dma_type;
	u_int actual_size, requested_size;
	u_int *pfn_list;
	struct as *as;
	u_long vaddr = 0;
	u_int npages, offset;
	u_long dvma_addr;
	int rval;

	DBG3(D_DMA_MAP, pci_p, "mapping - rdip=%s%d type=%s\n",
		ddi_get_name(rdip), ddi_get_instance(rdip),
		handlep ? "alloc" : "advisory");

	/*
	 * Sanity check the dma limit structure and determine if the
	 * limits will restrict the size of the dma request.
	 */
	actual_size = requested_size = dmareq->dmar_object.dmao_size;
	rval = check_limits(pci_p, dmareq, &actual_size);
	if (rval)
		return (rval);

	/*
	 * If this is not an advisory call allocate and initialize
	 * a ddi_dma_impl_t structure to be passed back.
	 */
	if (handlep) {
		mutex_enter(&pci_p->handle_pool_mutex);
		mp = (ddi_dma_impl_t *)
			kmem_fast_alloc(&pci_p->handle_pool_base,
			    sizeof (*mp), 2,
			    (dmareq->dmar_fp == DDI_DMA_SLEEP) ?
				KM_SLEEP : KM_NOSLEEP);
		mutex_exit(&pci_p->handle_pool_mutex);
		if (mp == 0) {
			DBG(D_DMA_MAP, pci_p, "can't alloc dma_impl\n");
			if (dmareq->dmar_fp != DDI_DMA_DONTWAIT) {
				DBG(D_DMA_MAP, pci_p, "set callback\n");
				ddi_set_callback(dmareq->dmar_fp,
					dmareq->dmar_arg,
					&pci_p->handle_call_list_id);
			}
			return (DDI_DMA_NORESOURCES);
		}

		/*
		 * Save requestor's information in the handle.
		 */
		bzero((caddr_t)mp, sizeof (ddi_dma_impl_t));
		mp->dmai_rdip = rdip;
		mp->dmai_rflags = dmareq->dmar_flags & DMP_DDIFLAGS;
		mp->dmai_minxfer = dmareq->dmar_limits->dlim_minxfer;
		mp->dmai_burstsizes = dmareq->dmar_limits->dlim_burstsizes;
		mp->dmai_object = dmareq->dmar_object;
	}

	/*
	 * Get the pages to be mapped.
	 */
	switch (dmareq->dmar_object.dmao_type) {
	case DMA_OTYP_VADDR:

		/*
		 * Get the mappings virtual address, offset and address space
		 * structure from the map request.
		 */
		vaddr = (u_long) dmareq->dmar_object.dmao_obj.virt_obj.v_addr;
		offset = vaddr & IOMMU_PAGE_OFFSET;
		npages = IOMMU_BTOPR(actual_size + offset);
		vaddr &= ~IOMMU_PAGE_OFFSET;
		as = dmareq->dmar_object.dmao_obj.virt_obj.v_as;
		if (as == (struct as *)0)
			as = &kas;

		/*
		 * check_dma_target determines the type of dma (DVMA vs dma
		 * within a PBM).  It also checks for mixed type dma, intra-PBM
		 * dma, and discontiguous dma within a PBM.
		 *
		 * check_dma_target also creates a page frame list and
		 * stores it in the handle's nexus private data area.  This
		 * list will be used by our parent (iommu) for establishing
		 * mappings and moving windows.
		 */
		rval = check_dma_target(pci_p, vaddr, as, requested_size,
					offset, &dma_type, mp);
		if (rval) {
			if (mp) {
				mutex_enter(&pci_p->handle_pool_mutex);
				kmem_fast_free(&pci_p->handle_pool_base,
					(caddr_t)mp);
				mutex_exit(&pci_p->handle_pool_mutex);
			}
			return (rval);
		}

		/*
		 */
		break;

	case DMA_OTYP_PAGES:

		dma_type = IOMMU_XLATE;
		offset = dmareq->dmar_object.dmao_obj.pp_obj.pp_offset;
		npages = IOMMU_BTOPR(actual_size + offset);
		mp->dmai_nexus_private = NULL;
		break;

	case DMA_OTYP_PADDR:
	default:

		DBG1(D_DMA_MAP, pci_p, "unsupported dma type (%x)\n",
			dmareq->dmar_object.dmao_type);
		if (mp) {
			mutex_enter(&pci_p->handle_pool_mutex);
			kmem_fast_free(&pci_p->handle_pool_base, (caddr_t)mp);
			mutex_exit(&pci_p->handle_pool_mutex);
		}
		return (DDI_DMA_NOMAPPING);
	}

	/*
	 * If this was just an advisory mapping call then we're done.
	 */
	if (mp == NULL)
		return (DDI_DMA_MAPOK);

	/*
	 * Determine the number of windows and window size for of the
	 * request.
	 */
	if (mp->dmai_rflags & DDI_DMA_PARTIAL) {
		if (actual_size == requested_size)
			mp->dmai_rflags ^= DDI_DMA_PARTIAL;
		npages = IOMMU_BTOPR(actual_size + offset);
		mp->dmai_nwin = (requested_size / actual_size) +
				((requested_size % actual_size) ? 1 : 0);
		DBG3(D_DMA_MAP, pci_p, "partial - size %x to %x (%x pages)\n",
			dmareq->dmar_object.dmao_size, actual_size, npages);
	} else
		mp->dmai_nwin = 1;
	mp->dmai_ndvmapages = npages;
	mp->dmai_size = mp->dmai_winsize = actual_size;
	DBG2(D_DMA_MAP, pci_p, "dmai_size=%x ndvmapages=%x\n",
		mp->dmai_size, mp->dmai_ndvmapages);
	DBG2(D_DMA_MAP, pci_p, "dmai_winsize=%x nwin=%x\n",
		mp->dmai_winsize, mp->dmai_nwin);

	/*
	 * If we are doing DMA to the same PCI bus segment, we don't
	 * use the IOMMU, we just use the PCI bus address.
	 */
	if (dma_type == PCI_PEER_TO_PEER) {

		pfn_list = (u_int *) mp->dmai_nexus_private;
		pfn_list++;
		mp->dmai_mapping = (u_long)IOMMU_PTOB(pfn_list[0]) + offset;
		mp->dmai_ndvmapages = 0;

		/*
		 * DMA to the same PCI bus must be consistent and
		 * can't have a redzone.
		 */
		if (!(mp->dmai_rflags & DDI_DMA_CONSISTENT))
			mp->dmai_rflags |= DDI_DMA_CONSISTENT;
		if (mp->dmai_rflags & DDI_DMA_REDZONE)
			mp->dmai_rflags ^= DDI_DMA_REDZONE;

		/*
		 * Initialize the handle's cookie information based on the
		 * newly established mapping.
		 */
		mp->dmai_flags |= DMAI_FLAGS_INUSE;
		dump_dma_handle(D_DMA_MAP, pci_p, mp);
		*handlep = (ddi_dma_handle_t)mp;
		return (DDI_DMA_MAPPED);
	}

	/*
	 * The dma request requires the iommu.  Get the dvma space and
	 * map in the first window.
	 */
	dvma_addr =
		get_dvma_pages(pci_p, mp->dmai_ndvmapages + HAS_REDZONE(mp),
				dmareq->dmar_limits->dlim_addr_lo,
				dmareq->dmar_limits->dlim_addr_hi,
				(dmareq->dmar_fp == DDI_DMA_SLEEP) ? 1 : 0);
	if (dvma_addr == 0) {
		if (dmareq->dmar_fp != DDI_DMA_DONTWAIT) {
			DBG(D_DMA_MAP, pci_p, "dvma_addr 0 - set callback\n");
			ddi_set_callback(dmareq->dmar_fp,
					dmareq->dmar_arg,
					&pci_p->psycho_p->dvma_call_list_id);
		}
		DBG(D_DMA_MAP, pci_p, "dvma_addr 0 - DDI_DMA_NORESOURCES\n");
		return (DDI_DMA_NORESOURCES);
	}
	mp->dmai_mapping = (u_long) dvma_addr + offset;
	map_window(pci_p, mp, 0);

	/*
	 * Initialize the handle's cookie information based on the
	 * newly established mapping.
	 */
	mp->dmai_flags |= DMAI_FLAGS_INUSE;
	*handlep = (ddi_dma_handle_t)mp;

	return (mp->dmai_rflags & DDI_DMA_PARTIAL ? DDI_DMA_PARTIAL_MAP :
						    DDI_DMA_MAPPED);
}

/*
 * bus dma alloc handle entry point:
 */
static int
pci_dma_allochdl(dev_info_t *dip, dev_info_t *rdip, ddi_dma_attr_t *attrp,
	int (*waitfp)(caddr_t), caddr_t arg, ddi_dma_handle_t *handlep)
{
	pci_devstate_t *pci_p = get_pci_soft_state(ddi_get_instance(dip));
	psycho_dma_t dma_type;
	ddi_dma_impl_t *mp;
	int rval;

	/*
	 * Check the dma attributes.
	 */
	DBG2(D_DMA_ALLOCH, pci_p, "rdip=%s%d\n",
		ddi_get_name(rdip), ddi_get_instance(rdip));
	rval = check_dma_attr(pci_p, attrp, &dma_type);
	if (rval)
		return (rval);

	/*
	 * Allocate the handle.
	 */
	mutex_enter(&pci_p->handle_pool_mutex);
	mp = (ddi_dma_impl_t *)
		kmem_fast_alloc(&pci_p->handle_pool_base, sizeof (*mp), 2,
				(waitfp == DDI_DMA_SLEEP) ?
					KM_SLEEP : KM_NOSLEEP);
	mutex_exit(&pci_p->handle_pool_mutex);
	if (mp == 0) {
		DBG(D_DMA_ALLOCH, pci_p, "can't alloc dma_impl\n");
		if (waitfp != DDI_DMA_DONTWAIT) {
			DBG(D_DMA_ALLOCH, pci_p, "set callback\n");
			ddi_set_callback(waitfp, arg,
						&pci_p->handle_call_list_id);
		}
		return (DDI_DMA_NORESOURCES);
	}
	bzero((caddr_t)mp, sizeof (ddi_dma_impl_t));

	/*
	 * Save requestor's information
	 */
	mp->dmai_rdip = rdip;
	mp->dmai_attr = *attrp;
	mp->dmai_minxfer = (u_int) attrp->dma_attr_minxfer;
	mp->dmai_burstsizes = (u_int) attrp->dma_attr_burstsizes;
	if (dma_type == IOMMU_BYPASS)
		mp->dmai_flags |= DMAI_FLAGS_BYPASS;
	*handlep = (ddi_dma_handle_t)mp;
	DBG1(D_DMA_CTL, pci_p, "handle=%x\n", mp);
	return (DDI_SUCCESS);
}

/*
 * bus dma free handle entry point:
 */
static int
pci_dma_freehdl(dev_info_t *dip, dev_info_t *rdip,
	ddi_dma_handle_t handle)
{
	pci_devstate_t *pci_p = get_pci_soft_state(ddi_get_instance(dip));

	/*
	 * Free the dma handle.
	 */
	DBG2(D_DMA_FREEH, pci_p, "rdip=%s%d\n",
		ddi_get_name(rdip), ddi_get_instance(rdip));
	mutex_enter(&pci_p->handle_pool_mutex);
	kmem_fast_free(&pci_p->handle_pool_base, (caddr_t)handle);
	mutex_exit(&pci_p->handle_pool_mutex);

	/*
	 * Now that we've freed some resources,
	 * if there is anybody waiting for it
	 * try and get them going.
	 */
	if (pci_p->handle_call_list_id != 0) {
		DBG(D_DMA_FREEH, pci_p, "run handle callback\n");
		ddi_run_callback(&pci_p->handle_call_list_id);
	}
	return (DDI_SUCCESS);
}

/*
 * bus dma bind handle entry point:
 */
static int
pci_dma_bindhdl(dev_info_t *dip, dev_info_t *rdip,
	ddi_dma_handle_t handle, struct ddi_dma_req *dmareq,
	ddi_dma_cookie_t *cookiep, u_int *ccountp)
{
	pci_devstate_t *pci_p = get_pci_soft_state(ddi_get_instance(dip));
	ddi_dma_impl_t *mp = (ddi_dma_impl_t *)handle;
	psycho_dma_t dma_type;
	u_int actual_size, requested_size;
	u_int *pfn_list;
	struct as *as;
	u_long vaddr = 0;
	u_int npages, offset;
	u_long dvma_addr;
	void *p;
	size_t s;
	int rval;

	/*
	 * Make sure the handle is valid.
	 */
	DBG2(D_DMA_BINDH, pci_p, "rdip=%s%d\n",
		ddi_get_name(rdip), ddi_get_instance(rdip));
	if (mp->dmai_flags & DMAI_FLAGS_INUSE)
		return (DDI_DMA_INUSE);

	/*
	 * Check the requested size to be sure it fits within the handle's
	 * attributes.  If it doesn't then try to use a partial mapping.
	 */
	actual_size = requested_size = dmareq->dmar_object.dmao_size;
	rval = check_dma_size(pci_p, mp, dmareq, &actual_size);
	if (rval)
		return (rval);

	/*
	 * Save requestor's information in the handle.
	 */
	mp->dmai_rdip = rdip;
	mp->dmai_rflags = dmareq->dmar_flags & DMP_DDIFLAGS;
	mp->dmai_object = dmareq->dmar_object;

	/*
	 * Get the pages to be mapped.
	 */
	switch (dmareq->dmar_object.dmao_type) {
	case DMA_OTYP_VADDR:
		/*
		 * Get the mapping's virtual address, offset and address space
		 * structure from the map request.
		 */
		vaddr = (u_long) dmareq->dmar_object.dmao_obj.virt_obj.v_addr;
		offset = vaddr & IOMMU_PAGE_OFFSET;
		npages = IOMMU_BTOPR(actual_size + offset);
		vaddr &= ~IOMMU_PAGE_OFFSET;
		as = dmareq->dmar_object.dmao_obj.virt_obj.v_as;
		if (as == (struct as *)0)
			as = &kas;

		/*
		 * check_dma_target determines the type of dma (DVMA vs dma
		 * within a PBM).  It also checks for mixed type dma, intra-PBM
		 * dma, and discontiguous dma within a PBM.
		 *
		 * check_dma_target also creates a page frame list and
		 * stores it in the handle's nexus private data area.  This
		 * list will be used by our parent (iommu) for establishing
		 * mappings and moving windows.
		 */
		rval = check_dma_target(pci_p, vaddr, as, requested_size,
					offset, &dma_type, mp);
		if (rval)
			return (rval);
		break;

	case DMA_OTYP_PAGES:
		/*
		 * The mapping is to a list of memory pages.
		 */
		dma_type = IOMMU_XLATE;
		offset = dmareq->dmar_object.dmao_obj.pp_obj.pp_offset;
		npages = IOMMU_BTOPR(actual_size + offset);
		mp->dmai_nexus_private = NULL;
		break;

	case DMA_OTYP_PADDR:
	default:

		DBG1(D_DMA_BINDH, pci_p, "unsupported dma type (%x)\n",
			dmareq->dmar_object.dmao_type);
		return (DDI_DMA_NOMAPPING);
	}

	/*
	 * Determine the number of windows and window size for of the
	 * request.
	 */
	if (mp->dmai_rflags & DDI_DMA_PARTIAL) {
		if (actual_size == requested_size)
			mp->dmai_rflags ^= DDI_DMA_PARTIAL;
		npages = IOMMU_BTOPR(actual_size + offset);
		mp->dmai_nwin = (requested_size / actual_size) +
				((requested_size % actual_size) ? 1 : 0);
		DBG3(D_DMA_BINDH, pci_p, "partial - size %x to %x (%x pages)\n",
			dmareq->dmar_object.dmao_size, actual_size, npages);
	} else
		mp->dmai_nwin = 1;
	mp->dmai_size = mp->dmai_winsize = actual_size;

	/*
	 * We need to check the dma request to see if a DVMA bypass
	 * was implied.
	 */
	if (mp->dmai_inuse & DMAI_FLAGS_BYPASS)
		dma_type = IOMMU_BYPASS;

	/*
	 * Now finished the bind request based on the type of dma.
	 */
	switch (dma_type) {
	case IOMMU_XLATE:

		/*
		 * The dma request requires the iommu.  Get the dvma space and
		 * map in the first window.
		 */
		mp->dmai_ndvmapages = npages;
		dvma_addr =
			get_dvma_pages(pci_p,
					mp->dmai_ndvmapages + HAS_REDZONE(mp),
					(ulong) mp->dmai_attr.dma_attr_addr_lo,
					(ulong) mp->dmai_attr.dma_attr_addr_hi,
					(dmareq->dmar_fp == DDI_DMA_SLEEP) ?
						1 : 0);
		if (dvma_addr == 0) {
			if (dmareq->dmar_fp != DDI_DMA_DONTWAIT) {
				DBG(D_DMA_BINDH, pci_p,
					"dvma_addr 0 - set callback\n");
				ddi_set_callback(dmareq->dmar_fp,
					dmareq->dmar_arg,
					&pci_p->psycho_p->dvma_call_list_id);
			}
			DBG(D_DMA_BINDH, pci_p,
				"dvma_addr 0 - DDI_DMA_NORESOURCES\n");
			return (DDI_DMA_NORESOURCES);
		}
		mp->dmai_mapping = dvma_addr + offset;
		map_window(pci_p, mp, 0);

		/*
		 * Initialize the handle's cookie information based on the
		 * newly established mapping.
		 */
		*ccountp = 1;
		MAKE_DMA_COOKIE(cookiep, mp->dmai_mapping, mp->dmai_size);
		DBG2(D_DMA_BINDH, pci_p,
			"cookie - dmac_address=%x dmac_size=%x\n",
			cookiep->dmac_address, cookiep->dmac_size);
		break;

	case IOMMU_BYPASS:

		DBG(D_DMA_BINDH, pci_p, "IOMMU_BYPASS\n");
		pfn_list = (u_int *) mp->dmai_nexus_private;
		pfn_list++;

		/*
		 * IOMMU BYPASS DMA must be consistent and can't have
		 * a redzone.
		 */
		if (!(mp->dmai_rflags & DDI_DMA_CONSISTENT))
			mp->dmai_rflags |= DDI_DMA_CONSISTENT;
		if (mp->dmai_rflags & DDI_DMA_REDZONE)
			mp->dmai_rflags ^= DDI_DMA_REDZONE;

		rval = create_bypass_cookies(pci_p, mp, cookiep, ccountp);
		if (rval != 0) {

			p = (void *)mp->dmai_nexus_private;
			if (p) {
				s = *(size_t *)p;
				DBG2(D_DMA_BINDH, pci_p,
					"freeing private data (%x,%x)\n", p, s);
				kmem_free(p, s);
				mp->dmai_nexus_private = NULL;
			}
			return (rval);
		}
		break;

	case PCI_PEER_TO_PEER:

		pfn_list = (u_int *) mp->dmai_nexus_private;
		pfn_list++;
		mp->dmai_mapping = (u_long)IOMMU_PTOB(pfn_list[0]) + offset;
		mp->dmai_ndvmapages = 0;

		/*
		 * DMA to the same PCI bus must be consistent and
		 * can't have a redzone.
		 */
		if (!(mp->dmai_rflags & DDI_DMA_CONSISTENT))
			mp->dmai_rflags |= DDI_DMA_CONSISTENT;
		if (mp->dmai_rflags & DDI_DMA_REDZONE)
			mp->dmai_rflags ^= DDI_DMA_REDZONE;

		/*
		 * Initialize the handle's cookie information based on the
		 * newly established mapping.
		 */
		*ccountp = 1;
		MAKE_DMA_COOKIE(cookiep, mp->dmai_mapping, mp->dmai_size);
		DBG2(D_DMA_BINDH, pci_p,
			"cookie - dmac_address=%x dmac_size=%x\n",
			cookiep->dmac_address, cookiep->dmac_size);
	}
	mp->dmai_flags |= DMAI_FLAGS_INUSE;
	dump_dma_handle(D_DMA_BINDH, pci_p, mp);
	rval = mp->dmai_rflags & DDI_DMA_PARTIAL ?
			DDI_DMA_PARTIAL_MAP : DDI_DMA_MAPPED;
	return (rval);
}

/*
 * bus dma unbind handle entry point:
 */
static int
pci_dma_unbindhdl(dev_info_t *dip, dev_info_t *rdip,
	ddi_dma_handle_t handle)
{
	ddi_dma_impl_t *mp = (ddi_dma_impl_t *)handle;
	pci_devstate_t *pci_p = get_pci_soft_state(ddi_get_instance(dip));
	u_int npages;
	void *p;
	size_t s;

	DBG2(D_DMA_UNBINDH, pci_p, "rdip=%s%d\n",
		ddi_get_name(rdip), ddi_get_instance(rdip));
	if ((mp->dmai_flags & DMAI_FLAGS_INUSE) == 0) {
		DBG(D_DMA_UNBINDH, pci_p, "handle not bound\n");
		return (DDI_FAILURE);
	}

	/*
	 * If the handle is not using the iommu, all we need to is
	 * mark it as free.
	 */
	if (mp->dmai_ndvmapages == 0) {
		mp->dmai_flags &= ~DMAI_FLAGS_INUSE;
		DBG(D_DMA_UNBINDH, pci_p, "ndvmapages is 0\n");
		return (DDI_SUCCESS);
	}

	/*
	 * Here if the handle is using the iommu.  Unload all the iommu
	 * translations and free the correspsonding dvma space.
	 */
	if (!pci_flush_on_alloc)
		unmap_window(pci_p, mp);
	if (mp->dmai_nwin > 1) {
		/*
		 * In this case, the mapping for the last window
		 * may not require the full amount of mapping
		 * space allocated to the window.
		 */
		npages = (mp->dmai_winsize / IOMMU_PAGE_SIZE) +
			((mp->dmai_mapping & IOMMU_PAGE_OFFSET) ? 1 : 0);
	} else
		npages = mp->dmai_ndvmapages;
	rmfree(pci_p->psycho_p->dvma_map, (long)npages + HAS_REDZONE(mp),
		IOMMU_BTOP(mp->dmai_mapping));

	/*
	 * Flush the streaming cache if necessary.
	 */
	if (!(mp->dmai_rflags & DDI_DMA_CONSISTENT))
		sbuf_flush(pci_p, mp, 0, 0);

	/*
	 * Free any private data for the mapping.
	 */
	p = (void *)mp->dmai_nexus_private;
	if (p) {
		s = *(size_t *)p;
		DBG2(D_DMA_UNBINDH, pci_p, "freeing private data (%x,%x)\n",
			p, s);
		kmem_free(p, s);
		mp->dmai_nexus_private = NULL;
	}

	/*
	 * Now that we've freed some dvma space, see if there is anybody
	 * waiting for it.
	 */
	if (pci_p->psycho_p->dvma_call_list_id != 0) {
		DBG(D_DMA_UNBINDH, pci_p, "run dvma callback\n");
		ddi_run_callback(&pci_p->psycho_p->dvma_call_list_id);
	}

	mp->dmai_flags &= ~DMAI_FLAGS_INUSE;
	return (DDI_SUCCESS);
}

/*
 * bus dma flush entry point:
 */
/*ARGSUSED*/
static int
pci_dma_flush(dev_info_t *dip, dev_info_t *rdip,
	ddi_dma_handle_t handle, off_t off, u_int len,
	u_int cache_flags)
{
	pci_devstate_t *pci_p = get_pci_soft_state(ddi_get_instance(dip));
	ddi_dma_impl_t *mp = (ddi_dma_impl_t *)handle;

	DBG2(D_DMA_FLUSH, pci_p, "rdip=%s%d\n",
		ddi_get_name(rdip), ddi_get_instance(rdip));
	if (!(mp->dmai_rflags & DDI_DMA_CONSISTENT))
		sbuf_flush(pci_p, mp, off, len);
	return (DDI_SUCCESS);
}


/*
 * bus dma win entry point:
 */
pci_dma_win(dev_info_t *dip, dev_info_t *rdip,
	ddi_dma_handle_t handle, uint_t win, off_t *offp,
	uint_t *lenp, ddi_dma_cookie_t *cookiep, uint_t *ccountp)
{
	pci_devstate_t *pci_p = get_pci_soft_state(ddi_get_instance(dip));
	ddi_dma_impl_t *mp = (ddi_dma_impl_t *)handle;
	u_int curwin = mp->dmai_offset;

	/*
	 * Make sure the handle was set up for partial mappings.
	 */
	DBG2(D_DMA_WIN, pci_p, "rdip=%s%d\n",
		ddi_get_name(rdip), ddi_get_instance(rdip));
	dump_dma_handle(D_DMA_WIN, pci_p, mp);
	if ((mp->dmai_rflags & DDI_DMA_PARTIAL) == 0) {
		DBG(D_DMA_WIN, pci_p, "no partial mapping\n");
		return (DDI_FAILURE);
	}

	/*
	 * Check to be sure the window is in range.
	 */
	if (win >= mp->dmai_nwin) {
		DBG1(D_DMA_WIN, pci_p, "%x out of range\n", win);
		return (DDI_FAILURE);
	}

	/*
	 * Before moving the window, make sure the current window and
	 * new window are not the same.
	 */
	curwin = mp->dmai_offset / mp->dmai_winsize;
	if (win != curwin) {

		/*
		 * Handle streaming cache flushing for the current dma window.
		 */
		if (!(mp->dmai_rflags & DDI_DMA_CONSISTENT))
			sbuf_flush(pci_p, mp, 0, 0);

		/*
		 * Map the window into the iommu.
		 */
		if (!pci_flush_on_alloc)
			unmap_window(pci_p, mp);
		map_window(pci_p, mp, win);
	}

	/*
	 * Construct the cookie for new window and adjust offset, length
	 * and cookie counter parameters.
	 */
	MAKE_DMA_COOKIE(cookiep, mp->dmai_mapping, mp->dmai_size);
	DBG2(D_DMA_WIN, pci_p, "cookie - dmac_address=%x dmac_size=%x\n",
		cookiep->dmac_address, cookiep->dmac_size);
	*ccountp = 1;
	*offp = (off_t)mp->dmai_offset;
	*lenp = (u_int)IOMMU_PTOB(mp->dmai_ndvmapages -
					IOMMU_BTOPR(mp->dmai_mapping &
						IOMMU_PAGE_OFFSET));
	DBG2(D_DMA_WIN, pci_p, "*offp=%x *lenp=%x\n", *offp, *lenp);
	return (DDI_SUCCESS);
}

/*
 * bus dma control entry point:
 */
/*ARGSUSED*/
static int
pci_dma_mctl(dev_info_t *dip, dev_info_t *rdip,
	ddi_dma_handle_t handle, enum ddi_dma_ctlops request,
	off_t *offp, u_int *lenp,
	caddr_t *objp, u_int cache_flags)
{
	pci_devstate_t *pci_p = get_pci_soft_state(ddi_get_instance(dip));
	psycho_devstate_t *psycho_p;
	ddi_dma_impl_t *mp = (ddi_dma_impl_t *)handle;
	ddi_dma_seg_impl_t *sp;
	ddi_dma_cookie_t *cp;
	ddi_dma_handle_t *handlep;
	struct ddi_dma_req *dmareq;
	u_int npages;
	int i;

	switch (request) {
	case DDI_DMA_FREE:

		DBG2(D_DMA_CTL, pci_p, "DDI_DMA_FREE: rdip=%s%d\n",
			ddi_get_name(rdip), ddi_get_instance(rdip));
		if (mp->dmai_ndvmapages) {
			/*
			 * Flush the streaming cache if necessary.
			 */
			if (!(mp->dmai_rflags & DDI_DMA_CONSISTENT))
				sbuf_flush(pci_p, mp, 0, 0);

			/*
			 * Now unload the iommu translations.
			 */
			psycho_p = pci_p->psycho_p;
			if (!pci_flush_on_alloc)
				unmap_window(pci_p, mp);

			/*
			 * Free the mapping's dvma space.
			 */
			if (mp->dmai_nwin > 1) {
				/*
				 * In this case, the mapping for the last window
				 * may not require the full amount of mapping
				 * space allocated to the window.
				 */
				npages = (mp->dmai_winsize /
						IOMMU_PAGE_SIZE) +
				((mp->dmai_mapping & IOMMU_PAGE_OFFSET)
					? 1 : 0);
			} else
				npages = mp->dmai_ndvmapages;
			rmfree(psycho_p->dvma_map,
				(long)npages + HAS_REDZONE(mp),
				IOMMU_BTOP(mp->dmai_mapping));

			/*
			 * Now that we've freed some dvma space, see if there
			 * is anyone waiting for some.
			 */
			if (psycho_p->dvma_call_list_id != 0) {
				DBG(D_DMA_CTL, pci_p, "run dvma callback\n");
				ddi_run_callback(&psycho_p->dvma_call_list_id);
			}
		}
		mp->dmai_flags &= ~DMAI_FLAGS_INUSE;

		/*
		 * Free the handle and its data.
		 */
		DBG1(D_DMA_CTL, pci_p, "freeing handle %x\n", mp);
		if (mp->dmai_nexus_private) {
			i = *(u_int *)mp->dmai_nexus_private;
			DBG2(D_DMA_CTL, pci_p, "freeing private data (%x,%x)\n",
				mp->dmai_nexus_private, i);
			kmem_free(mp->dmai_nexus_private, i);
			mp->dmai_nexus_private = NULL;
		}
		mutex_enter(&pci_p->handle_pool_mutex);
		kmem_fast_free(&pci_p->handle_pool_base, (caddr_t)mp);
		mutex_exit(&pci_p->handle_pool_mutex);

		/*
		 * Now that we've free a handle, see if there is anyone
		 * waiting for one.
		 */
		if (pci_p->handle_call_list_id != 0) {
			DBG(D_DMA_CTL, pci_p, "run handle callback\n");
			ddi_run_callback(&pci_p->handle_call_list_id);
		}
		return (DDI_SUCCESS);

	case DDI_DMA_SYNC:

		/*
		 * Flush the streaming cache if the mapping is not consistent.
		 */
		DBG2(D_DMA_CTL, pci_p, "DDI_DMA_SYNC: rdip=%s%d\n",
			ddi_get_name(rdip), ddi_get_instance(rdip));
		if (!(mp->dmai_rflags & DDI_DMA_CONSISTENT))
			sbuf_flush(pci_p, mp, *offp, *lenp);
		return (DDI_SUCCESS);

	case DDI_DMA_HTOC:

		/*
		 * Translate a DMA handle to DMA cookie.
		 */
		DBG2(D_DMA_CTL, pci_p, "DDI_DMA_HTOC: rdip=%s%d\n",
			ddi_get_name(rdip), ddi_get_instance(rdip));
		cp = (ddi_dma_cookie_t *)objp;
		if ((u_long) *offp >= (u_long) mp->dmai_size)
			return (DDI_FAILURE);
		MAKE_DMA_COOKIE(cp, mp->dmai_mapping + (u_long) *offp,
			mp->dmai_mapping + mp->dmai_size - cp->dmac_address);
		DBG2(D_DMA_CTL, pci_p,
			"HTOC: cookie - dmac_address=%x dmac_size=%x\n",
			cp->dmac_address, cp->dmac_size);
		return (DDI_SUCCESS);

	case DDI_DMA_REPWIN:

		/*
		 * The mapping must be a partial one.
		 */
		DBG2(D_DMA_CTL, pci_p, "DDI_DMA_REPWIN: rdip=%s%d\n",
			ddi_get_name(rdip), ddi_get_instance(rdip));
		if ((mp->dmai_rflags & DDI_DMA_PARTIAL) == 0) {
			DBG(D_DMA_CTL, pci_p, "REPWIN: no partial mapping\n");
			return (DDI_FAILURE);
		}

		*offp = (off_t)mp->dmai_offset;
		*lenp = (u_int)IOMMU_PTOB(mp->dmai_ndvmapages -
					IOMMU_BTOPR(mp->dmai_mapping &
						IOMMU_PAGE_OFFSET));
		DBG2(D_DMA_CTL, pci_p, "REPWIN: *offp=%x *lenp=%x\n",
			*offp, *lenp);
		return (DDI_SUCCESS);

	case DDI_DMA_MOVWIN:
	{
		u_int newwin, curwin;

		/*
		 * Determine the current and new windows.
		 */
		DBG4(D_DMA_CTL, pci_p,
			"DDI_DMA_MOVWIN: rdip=%s%d - lenp=%x offp-%x\n",
			ddi_get_name(rdip), ddi_get_instance(rdip),
			*lenp, *offp);
		dump_dma_handle(D_DMA_CTL, pci_p, mp);
		curwin = mp->dmai_offset / mp->dmai_winsize;
		newwin = *offp / mp->dmai_winsize;

		/*
		 * Make sure the mapping is a partial one,  The length
		 * is the mapping's window size, the offset is a window
		 * boundary, and the mapping has enough windows.
		 */
		if ((mp->dmai_rflags & DDI_DMA_PARTIAL) == 0 ||
				*lenp != mp->dmai_winsize ||
				*offp & (mp->dmai_winsize - 1) ||
				newwin >= mp->dmai_nwin)
			return (DDI_FAILURE);

		/*
		 * Now move the window, but only if the new one isn't
		 * the same as the current one.
		 */
		if (newwin != curwin) {

			/*
			 * Handle streaming cache flushing for the current
			 * dma window.
			 */
			if (!(mp->dmai_rflags & DDI_DMA_CONSISTENT))
				sbuf_flush(pci_p, mp, 0, 0);

			/*
			 * Advance the window to the one specfied.
			 */
			if (!pci_flush_on_alloc)
				unmap_window(pci_p, mp);
			map_window(pci_p, mp, *offp / mp->dmai_winsize);
		}

		/*
		 * Construct the dma cookie for the new window and return
		 * the new window's length and offset.
		 */
		MAKE_DMA_COOKIE((ddi_dma_cookie_t *)objp,
				mp->dmai_mapping, mp->dmai_size);
		DBG2(D_DMA_CTL, pci_p,
			"MOVWIN: cookie - dmac_address=%x dmac_size=%x\n",
			((ddi_dma_cookie_t *)objp)->dmac_address,
			((ddi_dma_cookie_t *)objp)->dmac_size);
		*offp = (off_t)mp->dmai_offset;
		*lenp = (u_int)IOMMU_PTOB(mp->dmai_ndvmapages -
					IOMMU_BTOPR(mp->dmai_mapping &
						IOMMU_PAGE_OFFSET));
		DBG2(D_DMA_CTL, pci_p, "MOVWIN: *offp=%x *lenp=%x\n",
			*offp, *lenp);
		return (DDI_SUCCESS);
	}

	case DDI_DMA_NEXTWIN:
	{
		ddi_dma_win_t *nwin, *owin;
		u_int newwin;

		owin = (ddi_dma_win_t *)offp;
		nwin = (ddi_dma_win_t *)objp;
		DBG5(D_DMA_CTL, pci_p,
			"DDI_DMA_NEXTWIN: rdip=%s%d - mp=%x owin=%x nwin=%x\n",
			ddi_get_name(rdip), ddi_get_instance(rdip),
			mp, owin, nwin);
		dump_dma_handle(D_DMA_CTL, pci_p, mp);

		/*
		 * If we don't have partial mappings, all we can only
		 * honor requests for the first window.
		 */
		if (!(mp->dmai_rflags & DDI_DMA_PARTIAL))
			if (*owin != NULL)
				return (DDI_DMA_DONE);

		/*
		 * See if this is the first nextwin request for this handle.
		 * If it is just return the handle.
		 */
		if (*owin == NULL) {
			mp->dmai_offset = 0;
			*nwin = (ddi_dma_win_t)mp;
			return (DDI_SUCCESS);
		}

		/*
		 * Make sure there really is a next window.
		 */
		newwin = (mp->dmai_offset / mp->dmai_winsize) + 1;
		if (newwin >= mp->dmai_nwin)
			return (DDI_DMA_DONE);

		/*
		 * Handle streaming cache flushing for the current dma window.
		 */
		if (!(mp->dmai_rflags & DDI_DMA_CONSISTENT))
			sbuf_flush(pci_p, mp, 0, 0);

		/*
		 * Map in the next dma window.
		 */
		if (!pci_flush_on_alloc)
			unmap_window(pci_p, mp);
		map_window(pci_p, mp, newwin);
		return (DDI_SUCCESS);
	}

	case DDI_DMA_NEXTSEG:
	{
		ddi_dma_seg_t *nseg, *oseg;

		oseg = (ddi_dma_seg_t *)lenp;
		nseg = (ddi_dma_seg_t *)objp;
		DBG5(D_DMA_CTL, pci_p,
			"DDI_DMA_NEXTSEG: rdip=%s%d - win=%x oseg=%x nseg=%x\n",
			ddi_get_name(rdip), ddi_get_instance(rdip),
			offp, oseg, nseg);

		/*
		 * Currently each window should have only one segment.
		 */
		if (*oseg != NULL) {
			DBG(D_DMA_CTL, pci_p, "NEXTSEG: done\n");
			return (DDI_DMA_DONE);
		}
		*nseg = *((ddi_dma_seg_t *)offp);
		return (DDI_SUCCESS);
	}

	case DDI_DMA_SEGTOC:

		sp = (ddi_dma_seg_impl_t *)handle;
		MAKE_DMA_COOKIE((ddi_dma_cookie_t *)objp,
				sp->dmai_mapping, sp->dmai_size);
		DBG4(D_DMA_CTL, pci_p,
			"DDI_DMA_SEGTOC: rdip=%s%d - address=%x size=%x\n",
			ddi_get_name(rdip), ddi_get_instance(rdip),
			((ddi_dma_cookie_t *)objp)->dmac_address,
			((ddi_dma_cookie_t *)objp)->dmac_size);
		*offp = sp->dmai_offset;
		*lenp = sp->dmai_size;
		DBG2(D_DMA_CTL, pci_p, "SEGTOC: *offp=%x *lenp=%x\n",
			*offp, *lenp);
		return (DDI_SUCCESS);

	case DDI_DMA_COFF:

		/*
		 * Return the mapping offset for a DMA cookie.  We process
		 * this request here to save a call to our parent.
		 */
		DBG2(D_DMA_CTL, pci_p, "DDI_DMA_COFF: rdip=%s%d\n",
			ddi_get_name(rdip), ddi_get_instance(rdip));
		cp = (ddi_dma_cookie_t *)offp;
		if (cp->dmac_address < mp->dmai_mapping ||
		    cp->dmac_address >= mp->dmai_mapping + mp->dmai_size) {
			DBG(D_DMA_CTL, pci_p, "DDI_DMA_COFF DDI_FAILURE\n");
			return (DDI_FAILURE);
		}
		*objp = (caddr_t)(cp->dmac_address - mp->dmai_mapping);
		DBG3(D_DMA_CTL, pci_p, "off=%x mapping=%x size=%x\n",
			(u_long) *objp, mp->dmai_mapping, mp->dmai_size);
		return (DDI_SUCCESS);

	case DDI_DMA_RESERVE:
	{
		struct fast_dvma *fdvma;
		u_long dvma_addr;

		DBG2(D_DMA_CTL, pci_p, "DDI_DMA_RESERVE: rdip=%s%d\n",
			ddi_get_name(rdip), ddi_get_instance(rdip));
		psycho_p = pci_p->psycho_p;
		dmareq = (struct ddi_dma_req *)offp;

		/*
		 * Check the limit structure.
		 */
		if ((dmareq->dmar_limits->dlim_addr_lo >=
				dmareq->dmar_limits->dlim_addr_hi) ||
				(dmareq->dmar_limits->dlim_addr_hi <
				psycho_p->iommu_dvma_base))
			return (DDI_DMA_BADLIMITS);

		mutex_enter(&pci_p->handle_pool_mutex);

		/*
		 * Check the size of the request.
		 */
		npages = dmareq->dmar_object.dmao_size;
		if (npages > psycho_p->dvma_reserve) {
			mutex_exit(&pci_p->handle_pool_mutex);
			return (DDI_DMA_NORESOURCES);
		}

		/*
		 * Allocate the dma handle.
		 */
		mp = (ddi_dma_impl_t *)
			kmem_fast_alloc(&pci_p->handle_pool_base,
					sizeof (*mp), 2, KM_SLEEP);
		bzero((caddr_t)mp, sizeof (ddi_dma_impl_t));
		mutex_exit(&pci_p->handle_pool_mutex);

		/*
		 * Get entries from dvma space map.
		 */
		dvma_addr = get_dvma_pages(pci_p, npages,
				dmareq->dmar_limits->dlim_addr_lo,
				dmareq->dmar_limits->dlim_addr_hi,
				(dmareq->dmar_fp == DDI_DMA_SLEEP) ? 1 : 0);
		if (dvma_addr == 0) {
			mutex_enter(&pci_p->handle_pool_mutex);
			psycho_p->dvma_reserve += npages;
			kmem_fast_free(&pci_p->handle_pool_base, (caddr_t)mp);
			mutex_exit(&pci_p->handle_pool_mutex);
			return (DDI_DMA_NOMAPPING);
		}
		psycho_p->dvma_reserve -= npages;

		/*
		 * Create the fast dvma request structure.
		 */
		fdvma = (struct fast_dvma *)
			kmem_alloc(sizeof (struct fast_dvma), KM_SLEEP);
		fdvma->pagecnt = (u_int *)
			kmem_alloc(npages * sizeof (u_int), KM_SLEEP);
		fdvma->ops = &fast_dvma_ops;
		fdvma->softsp = (caddr_t)pci_p;

		/*
		 * Initialize the handle.
		 */
		mp->dmai_rdip = rdip;
		mp->dmai_rflags = DMP_BYPASSNEXUS|DDI_DMA_READ;
		mp->dmai_minxfer = dmareq->dmar_limits->dlim_minxfer;
		mp->dmai_burstsizes = dmareq->dmar_limits->dlim_burstsizes;
		mp->dmai_mapping = dvma_addr;
		mp->dmai_ndvmapages = npages;
		mp->dmai_nwin = 0;
		mp->dmai_nexus_private = (caddr_t)fdvma;
		DBG4(D_DMA_CTL, pci_p,
			"DDI_DMA_RESERVE: mp=%x dvma=%x npages=%x private=%x\n",
			mp, dvma_addr, npages, fdvma);
		handlep = (ddi_dma_handle_t *)objp;
		*handlep = (ddi_dma_handle_t)mp;
		return (DDI_SUCCESS);
	}

	case DDI_DMA_RELEASE:
	{
		struct fast_dvma *fdvma;

		/*
		 * Make sure the handle has really been setup for fast dma.
		 */
		DBG2(D_DMA_CTL, pci_p, "DDI_DMA_RELEASE: rdip=%s%d\n",
			ddi_get_name(rdip), ddi_get_instance(rdip));
		if (mp->dmai_rflags != (DMP_BYPASSNEXUS|DDI_DMA_READ)) {
			DBG(D_DMA_CTL, pci_p,
				"DDI_DMA_RELEASE: not fast dma\n");
			return (DDI_FAILURE);
		}

		/*
		 * Make sure all the reserved dvma addresses are flushed
		 * from the iommu and freed.
		 */
		psycho_p = pci_p->psycho_p;
		if (!pci_flush_on_alloc)
			unmap_window(pci_p, mp);
		npages = mp->dmai_ndvmapages;
		rmfree(psycho_p->dvma_map, (long)npages,
			IOMMU_BTOP(mp->dmai_mapping));

		/*
		 * Now that we've freed some dvma space, see if there
		 * is anyone waiting for some.
		 */
		if (psycho_p->dvma_call_list_id != 0) {
			DBG(D_DMA_CTL, pci_p, "run dvma callback\n");
			ddi_run_callback(&psycho_p->dvma_call_list_id);
		}

		/*
		 * Free the memory allocated by the private data.
		 */
		fdvma = (struct fast_dvma *)mp->dmai_nexus_private;
		kmem_free(fdvma->pagecnt, npages * sizeof (u_int));
		kmem_free(fdvma, sizeof (struct fast_dvma));

		/*
		 * Free the handle and decrement reserve counter.
		 */
		mutex_enter(&pci_p->handle_pool_mutex);
		kmem_fast_free(&pci_p->handle_pool_base, (caddr_t)mp);
		psycho_p->dvma_reserve += npages;
		mutex_exit(&pci_p->handle_pool_mutex);

		/*
		 * Mark the handle as no longer in use.
		 */
		mp->dmai_flags &= ~DMAI_FLAGS_INUSE;
		mp->dmai_rflags &= ~(DMP_BYPASSNEXUS|DDI_DMA_READ);

		/*
		 * Now that we've free a handle, see if there is anyone
		 * waiting for one.
		 */
		if (pci_p->handle_call_list_id != 0) {
			DBG(D_DMA_CTL, pci_p, "run handle callback\n");
			ddi_run_callback(&pci_p->handle_call_list_id);
		}
		return (DDI_SUCCESS);
	}

	default:
		DBG3(D_DMA_CTL, pci_p, "unknown command (0x%x): rdip=%s%d\n",
			request, ddi_get_name(rdip), ddi_get_instance(rdip));
		return (DDI_FAILURE);
	}
}


/*
 * control ops entry point:
 *
 * Requests handled completely:
 *	DDI_CTLOPS_INITCHILD	see init_child() for details
 *	DDI_CTLOPS_UNINITCHILD
 *	DDI_CTLOPS_REPORTDEV	see report_dev() for details
 *	DDI_CTLOPS_XLATE_INTRS	nothing to do
 *	DDI_CTLOPS_IOMIN	cache line size if streaming otherwise 1
 *	DDI_CTLOPS_REGSIZE
 *	DDI_CTLOPS_NREGS
 *	DDI_CTLOPS_NINTRS
 *	DDI_CTLOPS_DVMAPAGESIZE
 *	DDI_CTLOPS_POKE_INIT
 *	DDI_CTLOPS_POKE_FLUSH
 *	DDI_CTLOPS_POKE_FINI
 *
 * All others passed to parent.
 */
static int
pci_ctlops(dev_info_t *dip, dev_info_t *rdip,
	ddi_ctl_enum_t op, void *arg, void *result)
{
	pci_devstate_t *pci_p = get_pci_soft_state(ddi_get_instance(dip));
	volatile u_longlong_t l;

	switch (op) {
	case DDI_CTLOPS_INITCHILD:
		DBG2(D_CTLOPS, pci_p, "DDI_CTLOPS_INITCHILD: rdip=%s%d\n",
			ddi_get_name(rdip), ddi_get_instance(rdip));
		return (init_child((dev_info_t)arg));

	case DDI_CTLOPS_UNINITCHILD:
		DBG2(D_CTLOPS, pci_p, "DDI_CTLOPS_UNINITCHILD: rdip=%s%d\n",
			ddi_get_name(rdip), ddi_get_instance(rdip));
		ddi_set_name_addr((dev_info_t)arg, NULL);
		ddi_remove_minor_node((dev_info_t)arg, NULL);
		impl_rem_dev_props((dev_info_t)arg);
		return (DDI_SUCCESS);

	case DDI_CTLOPS_REPORTDEV:
		DBG2(D_CTLOPS, pci_p, "DDI_CTLOPS_REPORTDEV: rdip=%s%d\n",
			ddi_get_name(rdip), ddi_get_instance(rdip));
		return (report_dev(rdip));

	case DDI_CTLOPS_XLATE_INTRS:
		DBG2(D_CTLOPS, pci_p, "DDI_CTLOPS_XLATE_INTRS: rdip=%s%d\n",
			ddi_get_name(rdip), ddi_get_instance(rdip));
		return (DDI_SUCCESS);

	case DDI_CTLOPS_IOMIN:

		/*
		 * If we are using the streaming cache, align on
		 * a cache line boundary.  Otherwise, no special
		 * alignment is required.
		 */
		DBG2(D_CTLOPS, pci_p, "DDI_CTLOPS_IOMIN: rdip=%s%d\n",
			ddi_get_name(rdip), ddi_get_instance(rdip));
		*((int *)result) = ((int)arg) ?
				PCI_SBUF_LINE_SIZE : 1;
		return (DDI_SUCCESS);

	case DDI_CTLOPS_REGSIZE:
		DBG2(D_CTLOPS, pci_p, "DDI_CTLOPS_REGSIZE: rdip=%s%d\n",
			ddi_get_name(rdip), ddi_get_instance(rdip));
		*((off_t *)result) = get_reg_set_size(rdip, *((int *)arg));
		return (DDI_SUCCESS);

	case DDI_CTLOPS_NREGS:
		DBG2(D_CTLOPS, pci_p, "DDI_CTLOPS_NREGS: rdip=%s%d\n",
			ddi_get_name(rdip), ddi_get_instance(rdip));
		*((u_int *) result) = get_nreg_set(rdip);
		return (DDI_SUCCESS);

	case DDI_CTLOPS_NINTRS:
		DBG2(D_CTLOPS, pci_p, "DDI_CTLOPS_NINTRS: rdip=%s%d\n",
			ddi_get_name(rdip), ddi_get_instance(rdip));
		*((u_int *) result) = get_nintr(rdip);
		return (DDI_SUCCESS);

	case DDI_CTLOPS_DVMAPAGESIZE:
		DBG2(D_CTLOPS, pci_p, "DDI_CTLOPS_DVMAPAGESIZE: rdip=%s%d\n",
			ddi_get_name(rdip), ddi_get_instance(rdip));
		*((u_long *) result) = IOMMU_PAGE_SIZE;
		return (DDI_SUCCESS);

	case DDI_CTLOPS_POKE_INIT:
		DBG2(D_CTLOPS, pci_p, "DDI_CTLOPS_POKE_INIT: rdip=%s%d\n",
			ddi_get_name(rdip), ddi_get_instance(rdip));
		mutex_enter(&pci_p->pokefault_mutex);
		pci_p->pokefault = -1;
		return (DDI_SUCCESS);

	case DDI_CTLOPS_POKE_FLUSH:
		DBG2(D_CTLOPS, pci_p, "DDI_CTLOPS_POKE_FLUSH: rdip=%s%d\n",
			ddi_get_name(rdip), ddi_get_instance(rdip));
		l = *pci_p->ctrl_reg;	/* flush any store buffers */
#if defined(lint)
		l = l;
#endif
		return (pci_p->pokefault == 1 ? DDI_FAILURE : DDI_SUCCESS);

	case DDI_CTLOPS_POKE_FINI:
		DBG2(D_CTLOPS, pci_p, "DDI_CTLOPS_POKE_FINI: rdip=%s%d\n",
			ddi_get_name(rdip), ddi_get_instance(rdip));
		pci_p->pokefault = 0;
		mutex_exit(&pci_p->pokefault_mutex);
		return (DDI_SUCCESS);
	}

	/*
	 * Now pass the request up to our parent.
	 */
	DBG2(D_CTLOPS, pci_p, "passing request to parent: rdip=%s%d\n",
		ddi_get_name(rdip), ddi_get_instance(rdip));
	return (ddi_ctlops(dip, rdip, op, arg, result));
}

/* support routines */

/*
 * pci_intr_wrapper
 *
 * This routine is used as wrapper around interrupt handlers installed by child
 * device drivers.  This routine invokes the associated interrupt handler then
 * clears the corresponding interrupt through the interrupt's clear interrupt
 * register.
 *
 * return value: DDI_INTR_CLAIMED if any handlers claimed the interrupt,
 *	DDI_INTR_UNCLAIMED otherwise.
 */
static u_int
pci_intr_wrapper(caddr_t arg)
{
	u_int r, result;
	psycho_ino_info_t *ino_p;
	psycho_intr_req_t *irp, *start_irp;
	u_longlong_t clear_int;
#if defined(DEBUG)
	u_int ino;
	pci_devstate_t *pci_p;
	u_longlong_t pci_int_diag;
	u_longlong_t obio_int_diag;
#endif

	/*
	 * Grap second level mutex so no one can update the handler
	 * list while we're processing it.
	 */
	ino_p = (psycho_ino_info_t *)arg;
#if defined(DEBUG)
	ino = ino_p->ino;
	pci_p = INO_TO_BUS(ino) == 'A' ? ino_p->psycho_p->pci_a_p :
					ino_p->psycho_p->pci_b_p;
#endif
#if 0
	mutex_enter(&ino_p->mutex2);
#endif

	switch (ino_p->state) {
	case INO_SINGLE:

		/*
		 * Call the single handler in the list.
		 */
		irp = ino_p->head;
		DBG3(D_INTR, pci_p,
			"ino %x, INO_SINGLE, handler=%x argument=%x\n",
			ino, irp->handler, irp->handler_arg);
		result = (*irp->handler)(irp->handler_arg);
		break;

	case INO_SHARED:

		/*
		 * Loop through list, starting at the place we left off
		 * last time.
		 */
		DBG2(D_INTR, pci_p, "ino %x, INO_SHARED, mode is %s\n",
			ino, pci_check_all_handlers ?
				"check all" : "first claimed");
		result = DDI_INTR_UNCLAIMED;
		irp = start_irp = ino_p->start;
		for (;;) {
			DBG2(D_INTR, pci_p, "handler=%x argument=%x\n",
				irp->handler, irp->handler_arg);
			r = (*irp->handler)(irp->handler_arg);
			if (r == DDI_INTR_CLAIMED) {
				DBG(D_INTR, pci_p, "interrupt claimed\n");
				result = DDI_INTR_CLAIMED;
				ino_p->start = irp->next;

				/*
				 * If we are not in "check all" mode, we
				 * can return since we known someone
				 * claimed the interrupt.
				 */
				if (!pci_check_all_handlers)
					break;
			}

			/*
			 * Continue through the list, unless we are back
			 * to our starting point.
			 */
			irp = irp->next;
			if (irp == start_irp) {
				DBG(D_INTR, pci_p, "scanned entire list\n");
				break;
			}
		}
		break;
	}
	DBG1(D_INTR, pci_p, "returning %s\n", (result == DDI_INTR_CLAIMED) ?
			"DDI_INTR_CLAIMED" : "DDI_INTR_UNCLAIMED");

	/*
	 * Clear the interrupt via the ino's clear interrupt register.
	 */
#if defined(DEBUG)
	pci_int_diag = *ino_p->psycho_p->pci_int_state_diag_reg;
	obio_int_diag =	*ino_p->psycho_p->obio_int_state_diag_reg;
	DBG2(D_INTR, pci_p, "pci_int_state_diag_reg=%x.%x\n",
		(u_int)(pci_int_diag >> 32),
		(u_int)(pci_int_diag & 0xffffffff));
	DBG2(D_INTR, pci_p, "obio_int_state_diag_reg=%x.%x\n",
		(u_int)(obio_int_diag >> 32),
		(u_int)(obio_int_diag & 0xffffffff));
#endif defined(DEBUG)
	clear_int = *ino_p->clear_reg;
	*ino_p->clear_reg = (clear_int & ~PSYCHO_CIR_MASK) | PSYCHO_CIR_IDLE;
	clear_int = *ino_p->clear_reg;
#if 0
	mutex_exit(&ino_p->mutex2);
#endif
	return (result);
}

/*
 * map_pbm_regs
 *
 * This function is called from the attach routine to map in the
 * Psycho register blocks and configuration space that belong to
 * this pci bus node.
 *
 * used by: pci_attach()
 *
 * return value: none
 */
static int
map_pbm_regs(pci_devstate_t *pci_p, dev_info_t *dip)
{
	ddi_device_acc_attr_t attr;
	caddr_t a;
	u_longlong_t base_pa;
	u_int n;
	int i;

	/*
	 * We expect to find an address property in the device node
	 * with virtual addresses for the pbm control regs, pbm
	 * configuration header and psycho control regs.
	 *
	 * I've been told that in configurations with multiple psycho's,
	 * the OBP may not create an address property.  To handle this
	 * case, we'll map the registers overself.
	 */
	i = sizeof (pci_p->address);
	if (ddi_getlongprop_buf(DDI_DEV_T_NONE, dip,
				DDI_PROP_DONTPASS, "address",
				(caddr_t)&pci_p->address, &i) != DDI_SUCCESS) {
		DBG(D_ATTACH, pci_p, "no address property\n");
		attr.devacc_attr_version = DDI_DEVICE_ATTR_V0;
		attr.devacc_attr_dataorder = DDI_STRICTORDER_ACC;
		attr.devacc_attr_endian_flags = DDI_NEVERSWAP_ACC;
		if (ddi_regs_map_setup(dip, 0, &pci_p->address[0], 0, 0,
					&attr, &pci_p->ac[0]) != DDI_SUCCESS) {
			cmn_err(CE_WARN, "%s%d: unable to map reg set 0\n",
				ddi_get_name(dip), ddi_get_instance(dip));
			return (0);
		}
		if (ddi_regs_map_setup(dip, 2, &pci_p->address[2], 0, 0,
					&attr, &pci_p->ac[2]) != DDI_SUCCESS) {
			cmn_err(CE_WARN, "%s%d: unable to map reg set 2\n",
				ddi_get_name(dip), ddi_get_instance(dip));
			ddi_regs_map_free(pci_p->ac[0]);
			return (0);
		}
	}
	DBG3(D_ATTACH, pci_p, "address (%x,%x,%x)\n", pci_p->address[0],
		pci_p->address[1], pci_p->address[2]);

	/*
	 * Now compute the address of specific registers.
	 */
	a = (caddr_t)pci_p->address[0];
	pci_p->reg_base = (u_longlong_t *)(a);
	pci_p->ctrl_reg = (u_longlong_t *)(a + PCI_CNTRL_REG_OFFSET);
	pci_p->sbuf_ctrl_reg = (u_longlong_t *)(a + SBUF_CTRL_REG_OFFSET);
	pci_p->sbuf_invl_reg = (u_longlong_t *)(a + SBUF_INVL_REG_OFFSET);
	pci_p->sbuf_sync_reg = (u_longlong_t *)(a + SBUF_SYNC_REG_OFFSET);
	pci_p->afsr = (u_longlong_t *)(a + PCI_AFSR_OFFSET);
	pci_p->afar = (u_longlong_t *)(a + PCI_AFAR_OFFSET);

	a = (caddr_t)pci_p->address[2];
	if (pci_p->bus_range.lo == 0) {
		pci_p->sbuf_data_diag_acc =
			(u_longlong_t *)(a + SBUF_A_DATA_DIAG_ACC_OFFSET);
		pci_p->sbuf_tag_diag_acc =
			(u_longlong_t *)(a + SBUF_A_TAG_DIAG_ACC_OFFSET);
	} else {
		pci_p->sbuf_data_diag_acc =
			(u_longlong_t *)(a + SBUF_B_DATA_DIAG_ACC_OFFSET);
		pci_p->sbuf_tag_diag_acc =
			(u_longlong_t *)(a + SBUF_B_TAG_DIAG_ACC_OFFSET);
	}

#if defined(NO_ASSIGNED_ADDRESS) || defined(NO_CLASS_CODE)
	/*
	 * The second register set contains the bridge's configuration
	 * header.  This header is at the very beginning of the bridge's
	 * configuration space.  We use this register set to map in
	 * then entire configuration space.
	 */
	attr.devacc_attr_version = DDI_DEVICE_ATTR_V0;
	attr.devacc_attr_dataorder = DDI_STRICTORDER_ACC;
	attr.devacc_attr_endian_flags = DDI_STRUCTURE_LE_ACC;
	n = pci_p->bus_range.hi - pci_p->bus_range.lo + 1;
	DBG1(D_ATTACH, pci_p, "mapping config space for %x buses\n", n);
	if (ddi_regs_map_setup(dip, 1, &a, 0, n * PCI_CONFIG_SPACE_SIZE,
				&attr, &pci_p->ac[1]) != DDI_SUCCESS) {
		cmn_err(CE_WARN, "%s%d: unable to map registers\n",
			ddi_get_name(dip), ddi_get_instance(dip));
		return (0);
	}
#else
	a = (caddr_t)pci_p->address[1];
#endif
	pci_p->config_space_base = a;
	pci_p->config_command_reg = (u_short *)(a + PCI_CONF_COMM);
	pci_p->config_status_reg = (u_short *)(a + PCI_CONF_STAT);
	pci_p->config_latency_reg = (u_char *)(a + PCI_CONF_LAT_TIMER_OFFSET);

	/*
	 * Now we need to compute the system address of the pci bus
	 * segment's configuration, i/o and memory spaces.
	 */
	a = (caddr_t)pci_p->address[2];
	base_pa = ((u_longlong_t)hat_getkpfnum(a) << MMU_PAGESHIFT);
	pci_p->base = base_pa;
	pci_p->last = base_pa + PCI_B_MEMORY + (PCI_MEM_SIZE - 1);
	pci_p->config_base = base_pa + PCI_CONFIG;
	if (pci_p->bus_range.lo == 0) {
		pci_p->mem_base = base_pa + PCI_B_MEMORY;
		pci_p->io_base = base_pa + PCI_B_IO;
	} else {
		pci_p->mem_base = base_pa + PCI_A_MEMORY;
		pci_p->io_base = base_pa + PCI_A_IO;
	}
	DBG2(D_ATTACH, pci_p, "config space at %x.%x\n",
		(u_int)(pci_p->config_base >> 32),
		(u_int)(pci_p->config_base & 0xffffffff));
	DBG2(D_ATTACH, pci_p, "i/o space at %x.%x\n",
		(u_int)(pci_p->io_base >> 32),
		(u_int)(pci_p->io_base & 0xffffffff));
	DBG2(D_ATTACH, pci_p, "mem space at %x.%x\n",
		(u_int)(pci_p->mem_base >> 32),
		(u_int)(pci_p->mem_base & 0xffffffff));
	return (1);
}

/*
 * unmap_pbm_regs
 *
 * This function is called from the attach (on errors) and detach routines
 * to break down the mappings to the Psycho register blocks and configuration
 * space that belong to * this pci bus node.
 *
 * used by: pci_attach() and pci_detach()
 *
 * return value: none
 */
static void
unmap_pbm_regs(pci_devstate_t *pci_p)
{
	int i;

	for (i = 0; i < sizeof (pci_p->address) / sizeof (int); i++) {
		if (pci_p->ac[i])
			ddi_regs_map_free(pci_p->ac[i]);
	}
}

/*
 * add_pbm_intrs
 *
 * This function is called from the attach routine to install the PBM's
 * PCI error interrupt handler.
 *
 * used by: pci_attach()
 *
 * return value: 0 on error, 1 on success
 */
static int
add_pbm_intrs(pci_devstate_t *pci_p)
{
	int i, *ip;
	u_int be_mondo;
	psycho_ino_info_t *ino_p;

	if (ddi_getlongprop(DDI_DEV_T_NONE, pci_p->dip,
				DDI_PROP_DONTPASS, "interrupts",
				(caddr_t)&ip, &i) != DDI_SUCCESS) {
		DBG(D_ATTACH, pci_p, "can't get interrupts property\n");
		return (0);
	}
	pci_p->be_mondo = be_mondo = ip[0];
	kmem_free((caddr_t)ip, i);
	DBG2(D_ATTACH, pci_p, "add_pbm_intrs: pci error mondo=%x (ino=%x)\n",
		be_mondo, MONDO_TO_INO(be_mondo));

	/*
	 * The uncorrectable error interrupt is the second interrupt
	 * in the interrupts property.
	 */
	add_ivintr(be_mondo, ino_to_pil[MONDO_TO_INO(be_mondo)],
			pci_error_intr, (caddr_t)pci_p, (kmutex_t *)NULL);

	/*
	 * Now enable the correctable and uncorrectable error interrupts
	 * via their interrupt mapping registers.
	 */
	ino_p = &pci_p->psycho_p->ino_info[MONDO_TO_INO(be_mondo)];
	*ino_p->map_reg = PSYCHO_IMR_VALID |
			    ((u_int) getprocessorid() << PSYCHO_IMR_TID_SHIFT);
	return (1);
}


/*
 * remove_pbm_intrs
 *
 * This function removes the interrupt handler for PCI bus asynchronous
 * errors and disables that interrupt via its interrupt mapping register.
 *
 * used by: pci_detach() and pci_attach()
 *
 * return value: none
 */
static void
remove_pbm_intrs(pci_devstate_t *pci_p)
{
	psycho_ino_info_t *ino_p;

	rem_ivintr(pci_p->be_mondo, (struct intr_vector *)NULL);
	ino_p = &pci_p->psycho_p->ino_info[MONDO_TO_INO(pci_p->be_mondo)];
	*ino_p->map_reg &= ~PSYCHO_IMR_VALID;
}


/*
 * configure_pbm
 *
 * This function is called from the attach routine to initialize the PBM's
 * control registers.
 *
 * This routine is called from the attach routine to initialize
 * and configure the following the pbm registers:
 *
 * PCI control and status register
 *	SERR=1
 *	ERRINT_EN=1
 *	ARB_PARK=1, CPU_PRIO=1, ARB_PRIO=0
 *	ARB_EN=f
 *	SBH_ERR=1, SBH_INT_EN=1, ERRINT_EN
 *
 * PCI AFSR
 *	clear primary and secondary error bits
 *
 * Streaming buffer control register
 *	LRU_LPTR=0, LRU_LE=0, RR_DIS=0, DE=0, EN=1
 *
 * Bridge configuration header registers
 *	command register (SERR_EN=1, PER=1)
 *	status register (DPE=1, SSE=1, RMA=1, RTA=1, STA=1, DPAR=1)
 *
 * used by: pci_attach()
 *
 * return value: none
 */
static void
configure_pbm(pci_devstate_t *pci_p)
{
	u_longlong_t l;
	u_short s;
	int instance = ddi_get_instance(pci_p->dip);
	int epci = 0;
	int i;
	const u_longlong_t sbuf_tag_init = 0x0ull;
	const u_longlong_t sbuf_data_init = 0x0ull;

	l = *pci_p->ctrl_reg;	/* save control register state */

	/*
	 * See if any SERR# signals are asserted.  We'll clear
	 * them a little later.
	 */
	if (l & PCI_CNTRL_SERR)
		cmn_err(CE_WARN, "%s%d: SERR asserted on pci bus\n",
			ddi_get_name(pci_p->dip), instance);

	/*
	 * Determine if PCI bus is running at 33 mhz (epci == 0) or
	 * 66 mhz (epci == 1).
	 */
	if (l & PCI_CNTRL_SPEED) {
		DBG(D_ATTACH, pci_p, "66 mhz\n");
		epci = 1;
	}

	/*
	 * Enable error interrupts.
	 */
	if (pci_error_intr_enable & (1 << instance))
		l |= PCI_CNTRL_ERR_INT_EN;
	else {
		cmn_err(CE_WARN, "%s%d: PCI error interrupts disabled\n",
			ddi_get_name(pci_p->dip), instance);
		l &= ~PCI_CNTRL_ERR_INT_EN;
	}

	/*
	 * Enable pci streaming byte errors and error interrupts.
	 */
	if (pci_sbh_error_intr_enable & (1 << instance))
		l |= PCI_CNTRL_SBH_INT_EN;
	else {
		cmn_err(CE_WARN,
			"%s%d: streaming byte hole interrupts disabled\n",
			ddi_get_name(pci_p->dip), instance);
		l &= ~PCI_CNTRL_SBH_INT_EN;
	}
	if (pci_sbh_error_enable & (1 << instance))
		l |= PCI_CNTRL_SBH_ERR;
	else {
		cmn_err(CE_WARN,
			"%s%d: streaming byte hole errors disabled\n",
			ddi_get_name(pci_p->dip), instance);
		l &= ~PCI_CNTRL_SBH_ERR;
	}

	/*
	 * Enable/disable bus parking.
	 */
	if (pci_bus_parking_enable & (1 << instance))
		l |= PCI_CNTRL_ARB_PARK;
	else {
		cmn_err(CE_WARN, "%s%d: bus parking disabled\n",
			ddi_get_name(pci_p->dip), instance);
		l &= ~PCI_CNTRL_ARB_PARK;
	}

	/*
	 * Set up cpu arbitration scheme.
	 */
	if (pci_cpu_prio_enable & (1 << instance))
		l &= ~PCI_CNTRL_CPU_PRIO;
	else
		l |= PCI_CNTRL_CPU_PRIO;
	l &= ~PCI_CNTRL_ARB_PRIO_MASK;
	DBG1(D_ATTACH, pci_p, "cpu priority %s\n",
		(pci_bus_parking_enable & (1 << instance)) ?
		    "enabled" : "disabled");

	/*
	 * Enable arbitration.
	 */
	if (pci_p->bus_range.lo == 0)
		l = (l & ~PCI_CNTRL_ARB_EN_MASK) | pci_b_arb_enable;
	else
		l = (l & ~PCI_CNTRL_ARB_EN_MASK) | pci_a_arb_enable;

	/*
	 * Make sure SERR is clear
	 */
	l |= PCI_CNTRL_SERR;

	/*
	 * Make sure power management interrupt is disabled.
	 */
	l &= ~PCI_CNTRL_WAKEUP_EN;
	*pci_p->ctrl_reg = l;

	/*
	 * Clear any PBM errors.
	 */
	l = ((u_longlong_t)PCI_AFSR_E_MASK >> PCI_AFSR_PE_SHIFT) |
		((u_longlong_t)PCI_AFSR_E_MASK >> PCI_AFSR_SE_SHIFT);
	*pci_p->afsr = l;

	/*
	 * Invalidate all streaming cache entries via the diagnostic
	 * access registers.
	 */
	*pci_p->sbuf_ctrl_reg = SBUF_CNTRL_DIAG_ENABLE;
	for (i = 0; i < PCI_SBUF_ENTRIES; i++) {
		*pci_p->sbuf_data_diag_acc = sbuf_data_init;
		*pci_p->sbuf_tag_diag_acc = sbuf_tag_init;
	}

	/*
	 * Enable/disable streaming cache and set RR_DIS if
	 * the PCI bus is running at 33 mhz.
	 */
	l = 0;
	if (pci_stream_buf_enable & (1 << instance))
		l = SBUF_CNTRL_SBUF_ENABLE;
	else
		cmn_err(CE_WARN, "%s%d: streaming cache disabled\n",
			ddi_get_name(pci_p->dip), instance);
	if (epci == 0)
		l |= SBUF_CNTRL_RR_DISABLE;
	DBG2(D_ATTACH, pci_p, "writing %x.%x to streaming buffer csr\n",
		(u_int)(l >> 32), (u_int)(l & 0xffffffff));
	*pci_p->sbuf_ctrl_reg = l;

	/*
	 * Enable SERR# and parity reporting via command register.
	 */
	if (pci_per_enable)
		s = PCI_COMM_SERR_ENABLE | PCI_COMM_PARITY_DETECT;
	else
		s = PCI_COMM_SERR_ENABLE;
	DBG1(D_ATTACH, pci_p, "writing %x to conf command register\n", s);
	*pci_p->config_command_reg = s;

	/*
	 * Clear error bits in configuration status register.
	 */
	s = PCI_STAT_PERROR | PCI_STAT_S_PERROR |
		PCI_STAT_R_MAST_AB | PCI_STAT_R_TARG_AB |
		PCI_STAT_S_TARG_AB | PCI_STAT_S_PERROR;
	*pci_p->config_status_reg = s;

	/*
	 * Set latency timer register in configuration header.
	 */
	DBG1(D_ATTACH, pci_p, "writing %x to latency timer register\n",
		pci_latency_timer);
	*pci_p->config_latency_reg = pci_latency_timer;
}


/*
 * alloc_sync_buf
 *
 * This routine is used to allocate and initialize the data structures
 * required for streaming cache flush synchronization.  The psycho chip
 * requires a 64 byte block of memory aligned on a 64 byte boundary.
 * This routine also create a mutex for locking access to this structure.
 *
 * After a flush/invalidate the driver writes the physical address of
 * this memory block to the flush/sync register.  The psycho chip will
 * fill this block with ones the flush operation has completed.
 *
 * used by: pci_attach()
 *
 * return value: none
 */
static void
alloc_sync_buf(pci_devstate_t *pci_p)
{
	char mutex_name[64];

	/*
	 * Allocate the flush/sync buffer.  Make sure it's 64 byte
	 * aligned.
	 */
	pci_p->sync_flag_base =
		kmem_zalloc(2 * PCI_SYNC_FLAG_SIZE, KM_SLEEP);
	pci_p->sync_flag_vaddr = (u_longlong_t *)
		((u_int)pci_p->sync_flag_base + (PCI_SYNC_FLAG_SIZE - 1) &
				~(PCI_SYNC_FLAG_SIZE - 1));
	pci_p->sync_flag_addr = (u_longlong_t)
		(hat_getkpfnum((caddr_t)pci_p->sync_flag_vaddr)
			<< MMU_PAGESHIFT) +
			((u_int)pci_p->sync_flag_vaddr & ~MMU_PAGEMASK);
	DBG2(D_ATTACH, pci_p, "sync buffer - vaddr=%x paddr=%x\n",
		pci_p->sync_flag_vaddr, pci_p->sync_flag_addr);

	/*
	 * Create a mutex to go along with it.  While the mutex is all,
	 * all interrupts should be blocked.  This will prevent driver
	 * interrupt routines from attempting to acquire the mutex while
	 * held by a lower priority interrupt routine.
	 */
	(void) sprintf(mutex_name, "pci (%x,%x) sync reg mutex",
			pci_p->upa_id, ddi_get_instance(pci_p->dip));
	mutex_init(&pci_p->sync_mutex, mutex_name, MUTEX_DRIVER,
			(void *)ipltospl(LOCK_LEVEL));
}


/*
 * free_sync_buf
 *
 * This routine is used to free the flush sync buffer and destroy
 * its mutex.
 *
 * used by: pci_detach() and pci_attach()
 *
 * return value: none
 */
void
free_sync_buf(pci_devstate_t *pci_p)
{
	mutex_destroy(&pci_p->sync_mutex);
	kmem_free(pci_p->sync_flag_base, 2 * PCI_SYNC_FLAG_SIZE);
}


/*
 * create_pokefault_mutex
 *
 * This routine is used to create the mutex used to lock DDI_CTLOPS_POKE_*
 * requests.
 *
 * used by: pci_attach()
 *
 * return value: none
 */
static void
create_pokefault_mutex(pci_devstate_t *pci_p)
{
	char mutex_name[64];

	(void) sprintf(mutex_name, "pci (%x,%x) pokefault mutex",
			pci_p->upa_id, ddi_get_instance(pci_p->dip));
	mutex_init(&pci_p->pokefault_mutex, mutex_name, MUTEX_DRIVER,
			(void *)ipltospl(15));
	pci_p->pokefault = 0;
}


/*
 * destroy_pokefault_mutex
 *
 * This routine is used to destroy the mutex used to lock DDI_CTLOPS_POKE_*
 * requests.
 *
 * used by: pci_detach()
 *
 * return value: none
 */
static void
destroy_pokefault_mutex(pci_devstate_t *pci_p)
{
	mutex_destroy(&pci_p->pokefault_mutex);
}


/*
 * set_psycho_regs_addr
 *
 * This function is used the attach routine to determine the virtual
 * addresses of psycho registers needed by the driver.
 *
 * used by: pci_attach()
 *
 * return value: none
 */
static void
set_psycho_regs_addr(psycho_devstate_t *p, caddr_t a)
{
	extern void set_intr_mapping_reg(int, u_longlong_t *, int);

	/*
	 * Compute the virtual addresses registers that we will need.
	 */
	p->reg_base = (u_longlong_t *)(a);
	p->ctrl_reg = (u_longlong_t *)(a + PSYCHO_CNTRL_REG_OFFSET);
	p->iommu_ctrl_reg = (u_longlong_t *)(a + IOMMU_CTRL_REG_OFFSET);
	p->tsb_base_addr_reg = (u_longlong_t *)(a + TSB_BASE_REG_OFFSET);
	p->iommu_flush_reg = (u_longlong_t *)(a + IOMMU_FLUSH_REG_OFFSET);
	p->imap_reg = (u_longlong_t *)(a + IMAP_REG_OFFSET);
	p->obio_imap_reg = (u_longlong_t *)(a + OBIO_IMAP_REG_OFFSET);
	p->cleari_reg = (u_longlong_t *)(a + CLEARI_REG_OFFSET);
	p->obio_cleari_reg = (u_longlong_t *)(a + OBIO_CLEARI_REG_OFFSET);
	p->intr_retry_timer_reg = (u_longlong_t *)(a + INTR_RETRY_TIMER_OFFSET);
	p->ecc_ctrl_reg = (u_longlong_t *)(a + ECC_CNTRL_REG_OFFSET);
	p->ue_afsr = (u_longlong_t *)(a + UE_AFSR_OFFSET);
	p->ue_afar = (u_longlong_t *)(a + UE_AFAR_OFFSET);
	p->ce_afsr = (u_longlong_t *)(a + CE_AFSR_OFFSET);
	p->ce_afar = (u_longlong_t *)(a + CE_AFAR_OFFSET);
	p->tlb_tag_diag_acc = (u_longlong_t *)(a + TLB_TAG_DIAG_ACC_OFFSET);
	p->tlb_data_diag_acc  = (u_longlong_t *)(a + TLB_DATA_DIAG_ACC_OFFSET);
	p->pci_int_state_diag_reg =
			(u_longlong_t *)(a + PCI_INT_STATE_DIAG_REG);
	p->obio_int_state_diag_reg =
			(u_longlong_t *)(a + OBIO_INT_STATE_DIAG_REG);
	set_intr_mapping_reg(p->upa_id,
			(u_longlong_t *)(a + OBIO_GRAPHICS_IMAP_OFFSET), 1);
}

/*
 * add_psycho_intrs
 *
 * This functions is called from the attach routine to install the
 * the ue and ce error interrupt handlers.
 *
 * used by: pci_attach()
 *
 * return value: 0 on error, 1 on success
 */
static int
add_psycho_intrs(psycho_devstate_t *psycho_p)
{
	int i, *ip;
	u_int ue_mondo, ce_mondo;
	psycho_ino_info_t *ino_p;

	/*
	 * Get the interrupts property and determine the ino's for
	 * the correctable and uncorrectable error interrupts.
	 */
	if (ddi_getlongprop(DDI_DEV_T_NONE, psycho_p->dip,
				DDI_PROP_DONTPASS, "interrupts",
				(caddr_t)&ip, &i) != DDI_SUCCESS) {
		DBG(D_ATTACH, (pci_devstate_t *)psycho_p->upa_id,
			"can't get interrupts property\n");
		return (0);
	}
	psycho_p->ue_mondo = ue_mondo = ip[1];
	psycho_p->ce_mondo = ce_mondo = ip[2];
	kmem_free((caddr_t)ip, i);
	DBG2(D_ATTACH, (pci_devstate_t *)psycho_p->upa_id,
		"psycho ue_mondo=%x ce_mondo=%x\n", ue_mondo, ce_mondo);

	/*
	 * The uncorrectable error interrupt is the second interrupt
	 * in the interrupts property.
	 */
	add_ivintr(ue_mondo, ino_to_pil[MONDO_TO_INO(ue_mondo)],
			psycho_ue_intr, (caddr_t)psycho_p, (kmutex_t *)NULL);

	/*
	 * The correctable error interrupt is the third interrupt
	 * in the interrupts property.
	 */
	add_ivintr(ce_mondo, ino_to_pil[MONDO_TO_INO(ce_mondo)],
			psycho_ce_intr, (caddr_t)psycho_p, (kmutex_t *)NULL);

	/*
	 * Now enable the correctable and uncorrectable error interrupts
	 * via their interrupt mapping registers.
	 */
	ino_p = &psycho_p->ino_info[MONDO_TO_INO(ue_mondo)];
	*ino_p->map_reg = PSYCHO_IMR_VALID |
			    ((u_int) getprocessorid() << PSYCHO_IMR_TID_SHIFT);
	ino_p = &psycho_p->ino_info[MONDO_TO_INO(ce_mondo)];
	*ino_p->map_reg = PSYCHO_IMR_VALID |
			    ((u_int) getprocessorid() << PSYCHO_IMR_TID_SHIFT);
	return (1);
}


/*
 * remove_psycho_intrs
 *
 * This function removes the ue and ce interrupt handlers and disables
 * those interrupts via the interrupt mapping registers.
 *
 * used by: pci_detach() and pci_attach()
 *
 * return value: none
 */
static void
remove_psycho_intrs(psycho_devstate_t *psycho_p)
{
	psycho_ino_info_t *ino_p;

	DBG(D_ATTACH, (pci_devstate_t *)psycho_p->upa_id,
		"remove_pbm_intrs:\n");
	ino_p = &psycho_p->ino_info[MONDO_TO_INO(psycho_p->ue_mondo)];
	*ino_p->map_reg &= ~PSYCHO_IMR_VALID;
	ino_p = &psycho_p->ino_info[MONDO_TO_INO(psycho_p->ce_mondo)];
	*ino_p->map_reg &= ~PSYCHO_IMR_VALID;
	rem_ivintr(psycho_p->ue_mondo, (struct intr_vector *)NULL);
	rem_ivintr(psycho_p->ce_mondo, (struct intr_vector *)NULL);
}


/*
 * configure_psycho
 *
 * This routine is called from the attach routine to initialize
 * and configure the following psycho registers:
 *
 * UPA configuration register
 *	nothing to do, assume OBP has written with SCIQ0 with 0x2
 *
 * Psycho control register
 *	assume OBP has set MID, IGN, and MODE fields
 *	clear APERR if pending
 *	set APCKEN and clear IAP
 *
 * UE AFSR
 *	clear any primary or secondary error bits
 *
 * CE AFSR
 *	clear any primary or secondary error bits
 *
 * Interrupt retry timer registers
 *	set to zero
 *
 * used by: pci_attach()
 *
 * return value: none
 */
static void
configure_psycho(psycho_devstate_t *psycho_p)
{
	u_longlong_t l;

	l = *psycho_p->ctrl_reg;
	if (l & PSYCHO_CNTRL_APERR) {
		l |= PSYCHO_CNTRL_APERR;
		cmn_err(CE_WARN, "UPA address parity error\n");
	}
	l |= PSYCHO_CNTRL_APCKEN;
	l &= ~PSYCHO_CNTRL_IAP;
	*psycho_p->ctrl_reg = l;

	l = ((u_longlong_t)PSYCHO_UE_AFSR_E_MASK <<
			PSYCHO_UE_AFSR_PE_SHIFT) |
		((u_longlong_t)PSYCHO_UE_AFSR_E_MASK <<
			PSYCHO_UE_AFSR_SE_SHIFT);
	*psycho_p->ue_afsr = l;

	l = ((u_longlong_t)PSYCHO_CE_AFSR_E_MASK <<
			PSYCHO_CE_AFSR_PE_SHIFT) |
		((u_longlong_t)PSYCHO_CE_AFSR_E_MASK <<
			PSYCHO_CE_AFSR_SE_SHIFT);
	*psycho_p->ce_afsr = l;

	if (ecc_error_intr_enable)
		l = (PSYCHO_ECCCR_ECC_EN | PSYCHO_ECCCR_UE_INTEN |
			PSYCHO_ECCCR_CE_INTEN);
	else {
		cmn_err(CE_WARN, "%s%d: PCI error interrupts disabled\n",
			ddi_get_name(psycho_p->dip),
			ddi_get_instance(psycho_p->dip));
		l = 0;
	}
	*psycho_p->ecc_ctrl_reg = l;

	l = pci_intr_retry_intv;
	*psycho_p->intr_retry_timer_reg = l;
}

/*
 * init_intr_info
 *
 * This routine is called from the attach routine to initialize the psycho
 * per-ino structure for each ino.  For each per-ino structure is given a
 * mutex and pointers to the ino's interrupt mapping and clear interrupt
 * registers.
 *
 * used by: pci_attach()
 *
 * return value: none
 */
static void
init_intr_info(psycho_devstate_t *psycho_p)
{
	char mutex_name[64];
	psycho_ino_info_t *ip;
	int ino;

	ip = psycho_p->ino_info;
	for (ino = 0; ino <= PSYCHO_MAX_INO; ino++) {

		/*
		 * Create and initialize the global ino mutex.
		 */
		(void) sprintf(mutex_name, "mutex1 for upa-id %x ino %x",
				psycho_p->upa_id, ino);
		mutex_init(&ip->mutex1, mutex_name, MUTEX_DRIVER, NULL);

		/*
		 * Get addresses of the ino's clear interrupt and interrupt
		 * mapping registers.
		 */
		if (OBIO_INO(ino)) {
			ip->map_reg = (u_longlong_t *)
				((caddr_t)psycho_p->obio_imap_reg +
						((ino & 0x1f) << 3));
			ip->clear_reg = (u_longlong_t *)
				((caddr_t)psycho_p->obio_cleari_reg +
						((ino & 0x1f) << 3));
		} else {
			ip->map_reg = (u_longlong_t *)
				((caddr_t)psycho_p->imap_reg +
				((ino & 0x3c) << 1));
			ip->clear_reg = (u_longlong_t *)
				((caddr_t)psycho_p->cleari_reg +
				((ino & 0x1f) << 3));
		}
		ip->state = INO_FREE;
		ip->head = ip->tail = ip->start = (psycho_intr_req_t *)0;
		ip->size = 0;
		ip->ino = ino;
		ip->psycho_p = psycho_p;
		ip++;
	}
}

/*
 * dvma_rmap_init
 *
 * This routine is called from the attach routine to allocate and
 * initialize the resource map for DVMA space.
 *
 * used by: pci_attach()
 *
 * return value: none
 */
static void
dvma_rmap_init(psycho_devstate_t *psycho_p)
{
	char map_name[32];
	u_int frag = psycho_p->iommu_tsb_entries >> 3;

	/*
	 * Allocate space for the resource map.
	 */
	psycho_p->dvma_map = (struct map *)
		kmem_zalloc(frag * sizeof (struct map), KM_SLEEP);

	/*
	 * Construct name for the resource map.
	 */
	(void) sprintf(map_name, "dvma rmap (%x)", psycho_p->upa_id);

	/*
	 * Initialize the resource map.
	 */
	mapinit(psycho_p->dvma_map, (long)psycho_p->iommu_tsb_entries,
		(u_long)IOMMU_BTOP(psycho_p->iommu_dvma_base), map_name, frag);
}

/*
 * iommu_init
 *
 * This routine is called from the attach routine to allocate the iommu
 * tsb and initialize the chip's iommu hardware.
 *
 * used by: pci_attach()
 *
 * return value: none
 */
static void
iommu_init(psycho_devstate_t *psycho_p)
{
	int i;
	u_longlong_t tsb_paddr, iommu_ctrl_reg;
	const u_longlong_t tlb_tag_init = 0x0ull;
	const u_longlong_t tlb_data_init = 0x0ull;

	/*
	 * Get the address of the pre-allocated tsb.  (see startup.c)
	 */
	tsb_paddr = iommu_tsb_physaddr[psycho_p->upa_id];
	psycho_p->iommu_tsb_entries = (1 << pci_iommu_tsb_size) * 1024;
	psycho_p->iommu_dvma_base =
		0 - (psycho_p->iommu_tsb_entries * IOMMU_PAGE_SIZE);
	psycho_p->dvma_reserve = psycho_p->iommu_tsb_entries >> 2;
	DBG4(D_ATTACH, (pci_devstate_t *)psycho_p->upa_id,
		"iommu tsb - paddr=%x.%x entries=%x base=%x\n",
		(u_int)(tsb_paddr >> 32), (u_int)(tsb_paddr & 0xffffffff),
		psycho_p->iommu_tsb_entries, psycho_p->iommu_dvma_base);

	/*
	 * Initialize the tsb.
	 */
	for (i = 0; i < psycho_p->iommu_tsb_entries; i++)
		IOMMU_UNLOAD_TTE(tsb_paddr, i);

	/*
	 * Invalide the tlb entries using the diagnostic access
	 * registers.
	 */
	iommu_ctrl_reg = PSYCHO_IOMMU_DIAG_ENABLE;
	if (pci_iommu_lock_enable)
		iommu_ctrl_reg |= PSYCHO_IOMMU_CTRL_LCK_ENABLE;
	*psycho_p->iommu_ctrl_reg = iommu_ctrl_reg;
	for (i = 0; i < PSYCHO_IOMMU_TLB_SIZE; i++) {
		*psycho_p->tlb_tag_diag_acc = tlb_tag_init;
		*psycho_p->tlb_data_diag_acc = tlb_data_init;
	}

	/*
	 * Intialize the iommu's control and base address registers.
	 */
	*psycho_p->tsb_base_addr_reg = psycho_p->tsb_paddr = tsb_paddr;
	iommu_ctrl_reg = (u_longlong_t)
		((pci_iommu_tsb_size << PSYCHO_IOMMU_CTRL_TSB_SZ_SHIFT) |
			(PSYCHO_IOMMU_CTRL_TBW_SIZE <<
			    PSYCHO_IOMMU_CTRL_TBW_SZ_SHIFT) |
				PSYCHO_IOMMU_CTRL_ENABLE);
	if (pci_iommu_lock_enable)
		iommu_ctrl_reg |= PSYCHO_IOMMU_CTRL_LCK_ENABLE;
	*psycho_p->iommu_ctrl_reg = iommu_ctrl_reg;
}

/*
 * get_reg_set
 *
 * The routine will get an IEEE 1275 PCI format regspec for a given
 * device node and register number.
 *
 * used by: pci_map()
 *
 * return value:
 *
 *	DDI_SUCCESS		- on success
 *	DDI_ME_INVAL		- regspec is invalid
 *	DDI_ME_RNUMBER_RANGE	- rnumber out of range
 */
static int
get_reg_set(pci_devstate_t *pci_p, dev_info_t *child, int rnumber,
#if defined(ACC_ENABLE)
	off_t off, off_t len, struct regspec *rp, u_int acc_enable)
#else
	off_t off, off_t len, struct regspec *rp)
#endif
{
	pci_regspec_t *pci_rp;
	int i, n, rval;

	/*
	 * Get the reg property for the device.
	 */
	if (ddi_getlongprop(DDI_DEV_T_NONE, child, DDI_PROP_DONTPASS, "reg",
				(caddr_t)&pci_rp, &i) != DDI_SUCCESS)
		return (DDI_ME_RNUMBER_RANGE);

	n = i / (int)sizeof (pci_regspec_t);
	if (rnumber >= n)
		rval = DDI_ME_RNUMBER_RANGE;
	else {

		/*
		 * Convert each the pci format register specification to a
		 * UPA format register specification.
		 */
#if defined(ACC_ENABLE)
		rval = xlate_reg_prop(pci_p, child, &pci_rp[rnumber], off, len,
					rp, acc_enable);
#else
		rval = xlate_reg_prop(pci_p, child, &pci_rp[rnumber], off, len,
					rp);
#endif
	}
	kmem_free((caddr_t)pci_rp, i);
	return (rval);
}

/*
 * xlate_reg_prop
 *
 * This routine converts an IEEE 1275 PCI format regspec to a standard
 * regspec containing the corresponding system address.
 *
 * used by: pci_map()
 *
 * return value:
 *
 *	DDI_SUCCESS		- on success
 *	DDI_ME_INVAL		- regspec is invalid
 */
static int
xlate_reg_prop(pci_devstate_t *pci_p, dev_info_t *dip, pci_regspec_t *pci_rp,
#if defined(ACC_ENABLE)
	off_t off, off_t len, struct regspec *rp, u_int acc_enable)
#else
	off_t off, off_t len, struct regspec *rp)
#endif
{
	int assigned_addresses = 1;
	u_int bus, phys_hi, phys_low, size_low;
	u_int *base_addr_reg;
	u_short *command_reg;

	/*
	 * Make sure the bus number is valid.
	 */
	DBG5(D_MAP, pci_p, "pci regspec - ((%x,%x,%x) (%x,%x))\n",
		pci_rp->pci_phys_hi, pci_rp->pci_phys_mid,
		pci_rp->pci_phys_low,
		pci_rp->pci_size_hi, pci_rp->pci_size_low);
	phys_hi = pci_rp->pci_phys_hi;
	bus = PCI_REG_BUS_G(phys_hi);
	if (bus < pci_p->bus_range.lo || bus > pci_p->bus_range.hi) {
		DBG1(D_MAP, pci_p, "invalid bus number (%x)\n", bus);
		return (DDI_ME_INVAL);
	}

	/*
	 * Regardless of type code, phys_mid must always be zero.
	 */
	if (pci_rp->pci_phys_mid != 0 || pci_rp->pci_size_hi != 0) {
		DBG(D_MAP, pci_p, "phys_mid or size_hi not 0\n");
		return (DDI_ME_INVAL);
	}

	/*
	 * Configuration space addresses are the simplest case.
	 */
	if ((phys_hi & PCI_ADDR_MASK) == PCI_ADDR_CONFIG) {
		/*
		 * Bits 32-0 of the high address word act as an offset
		 * into configuration space.
		 */
		DBG2(D_MAP, pci_p, "config - off %x len %x\n", off, len);
		if (phys_hi & (PCI_RELOCAT_B|PCI_PREFETCH_B))
			return (DDI_ME_INVAL);
		if (pci_rp->pci_phys_low != 0)
			return (DDI_ME_INVAL);
		if (pci_rp->pci_size_low != 0)
#if defined(CONFIG_SPACE_SIZE_ZERO)
			return (DDI_ME_INVAL);
#else
#if 0
			cmn_err(CE_WARN,
				"%s%d: config register size not zero\n",
				ddi_get_name(dip),
				ddi_get_instance(dip));
#endif
#endif
		if ((off >= PCI_CONF_HDR_SIZE) || (len > PCI_CONF_HDR_SIZE) ||
				(off + len > PCI_CONF_HDR_SIZE))
			return (DDI_ME_INVAL);
		rp->regspec_bustype = pci_p->config_base >> 32;
		rp->regspec_addr = (pci_p->config_base & 0xffffffff) +
				(pci_rp->pci_phys_hi & PCI_CONF_ADDR_MASK) +
				off;
		rp->regspec_size = (len == 0 ? PCI_CONF_HDR_SIZE : len);
		DBG3(D_MAP, pci_p, "regspec (%x,%x,%x)\n", rp->regspec_bustype,
			rp->regspec_addr, rp->regspec_size);
		return (DDI_SUCCESS);
	}

	/*
	 * If the "reg" property specifies relocatable, get and interpret the
	 * "assigned-addresses" property.
	 */
	if ((phys_hi & PCI_RELOCAT_B) == 0) {
		if (get_addr(pci_p, dip, pci_rp, &phys_low) != DDI_SUCCESS)
#if defined(NO_ASSIGNED_ADDRESS)
			assigned_addresses = 0;
#else
			return (DDI_FAILURE);
#endif
	} else
		phys_low = pci_rp->pci_phys_low;

	/*
	 * Adjust the mapping request for the length and offset parameters.
	 */
	phys_low += off;
	size_low = (len == 0 ? pci_rp->pci_size_low : len);

	/*
	 * Build the regspec based on the address type code.
	 */
	switch (phys_hi & PCI_ADDR_MASK) {

	case PCI_ADDR_MEM64:
		DBG(D_MAP, pci_p, "64 bit memory space\n");
		return (DDI_FAILURE);

	case PCI_ADDR_MEM32:

		DBG2(D_MAP, pci_p, "memory space - off %x len %x\n", off, len);
#if defined(NO_ASSIGNED_ADDRESS)
		if ((phys_hi & PCI_RELOCAT_B) == 0 && assigned_addresses == 0) {

			/*
			 * Get the memory space address from the configration
			 * header base address register pair.  The high 32 bits
			 * of the address must be zero.
			 */
			DBG(D_MAP, pci_p, "relocatable\n");
			base_addr_reg =
				(u_int *)pci_config_addr(pci_p, phys_hi, 0);
			phys_low += *base_addr_reg & PCI_BASE_M_ADDR_M;
		}
#endif

		/*
		 * Psycho implements 31 bits of memory space.  Bit 31 (starting
		 * from 0) selects the iommu device. Make sure address and size
		 * are within range.
		 */
		if (phys_low & 0x80000000 || (size_low - 1) & 0x80000000)
			return (DDI_ME_INVAL);

		if ((phys_low + size_low) & 0x80000000) {
			DBG(D_MAP, pci_p, "addr + size too large\n");
			return (DDI_ME_INVAL);
		}

		/*
		 * Build the regspec in our parent's format.
		 */
		rp->regspec_bustype = pci_p->mem_base >> 32;
		rp->regspec_addr = phys_low + (pci_p->mem_base & 0xffffffff);
		rp->regspec_size = size_low;

#if defined(ACC_ENABLE)
		/*
		 * Check to see if we need to enable memory access for this
		 * device.
		 */
		if (acc_enable) {
			command_reg = (u_short *)
				pci_config_addr(pci_p, phys_hi, PCI_CONF_COMM);
			*command_reg |= PCI_COMM_ME;
		}
#endif
		break;

	case PCI_ADDR_IO:

		DBG2(D_MAP, pci_p, "i/o - off %x len %x\n", off, len);
#if defined(NO_ASSIGNED_ADDRESS)
		if ((phys_hi & PCI_RELOCAT_B) == 0 && assigned_addresses == 0) {
			DBG(D_MAP, pci_p, "relocatable\n");
			base_addr_reg =
				(u_int *)pci_config_addr(pci_p, phys_hi, 0);
			phys_low += *base_addr_reg & PCI_BASE_IO_ADDR_M;
		}
#endif

		/*
		 * Psycho implements a 16 bits of i/o space.  Make sure
		 * address * and size are within range.  Also make sure
		 * it's not prefetchable.
		 */
		if (phys_low & 0xffff0000 || (size_low - 1) & 0xffff0000)
			return (DDI_ME_INVAL);
		if ((phys_low + size_low) & 0xffff0000)
			return (DDI_ME_INVAL);
		if (phys_hi & PCI_PREFETCH_B)
			return (DDI_ME_INVAL);


		/*
		 * Build the regspec in our parent's format.
		 */
		rp->regspec_bustype = pci_p->io_base >> 32;
		rp->regspec_addr = (pci_p->io_base & 0xffffffff) + phys_low;
		rp->regspec_size = size_low;

#if defined(ACC_ENABLE)
		/*
		 * Check to see if we need to enable i/o access for this
		 * device.
		 */
		if (acc_enable) {
			command_reg = (u_short *)
				pci_config_addr(pci_p, phys_hi, PCI_CONF_COMM);
			*command_reg |= PCI_COMM_IO;
		}
#endif
	}
	DBG3(D_MAP, pci_p, "regspec (%x,%x,%x)\n",
		rp->regspec_bustype, rp->regspec_addr, rp->regspec_size);
	return (DDI_SUCCESS);
}


/*
 * get_addr
 *
 * This routine interprets the "assigned-addresses" property for a given
 * IEEE 1275 PCI format regspec.
 *
 * used by: xlate_reg_prop()
 *
 * return value:
 *
 *	1	- on success
 *	0	- on failure
 */
static int
get_addr(pci_devstate_t *pci_p, dev_info_t *dip, pci_regspec_t *pci_rp,
	u_int *phys_low_p)
{
	pci_regspec_t *assigned_addr;
	int assigned_addr_len = 0;
	u_int phys_hi = pci_rp->pci_phys_hi;
	int match = 0;
	int i;

	/*
	 * Initialize the physical address with the offset contained
	 * in the specified "reg" property entry.
	 */
	*phys_low_p = pci_rp->pci_phys_low;

	/*
	 * Attempt to get the "assigned-addresses" property for the
	 * requesting device.
	 */
	if (ddi_getlongprop(DDI_DEV_T_NONE, dip, DDI_PROP_DONTPASS,
				"assigned-addresses", (caddr_t)&assigned_addr,
				&assigned_addr_len) != DDI_SUCCESS) {
		cmn_err(CE_WARN, "%s%d: no assigned-addresses property\n",
			ddi_get_name(dip), ddi_get_instance(dip));
		return (DDI_FAILURE);
	}

	/*
	 * Scan the "assigned-addresses" for one that matches the specified
	 * "reg" property entry.
	 */
	DBG1(D_MAP, pci_p, "get_addr - matching %x\n",
		phys_hi & PCI_CONF_ADDR_MASK);
	for (i = 0; i < assigned_addr_len / sizeof (pci_regspec_t); i++) {
		DBG5(D_MAP, pci_p, "get addr - checking ((%x,%x,%x) (%x,%x))\n",
			assigned_addr->pci_phys_hi,
			assigned_addr->pci_phys_mid,
			assigned_addr->pci_phys_low,
			assigned_addr->pci_size_hi,
			assigned_addr->pci_size_low);
		if ((assigned_addr[i].pci_phys_hi & PCI_CONF_ADDR_MASK) ==
				(phys_hi & PCI_CONF_ADDR_MASK)) {
			*phys_low_p += assigned_addr[i].pci_phys_low;
			DBG1(D_MAP, pci_p, "match - phys_lo=%x\n", *phys_low_p);
			match = 1;
			break;
		}
	}

	/*
	 * Free the memory taken by the "assigned-addresses" property.
	 */
	if (assigned_addr_len)
		kmem_free((caddr_t)assigned_addr, assigned_addr_len);

	if (!match) {
		cmn_err(CE_WARN,
		"%s%d: no assigned-addresses for %s%d (%x,%x,%x,%x,%x)\n",
			ddi_get_name(pci_p->dip), ddi_get_instance(pci_p->dip),
			ddi_get_name(dip), ddi_get_instance(dip),
			pci_rp->pci_phys_hi, pci_rp->pci_phys_mid,
			pci_rp->pci_phys_low, pci_rp->pci_size_hi,
			pci_rp->pci_size_low);
		return (DDI_FAILURE);
	}
	return (DDI_SUCCESS);
}

/*
 * xlate_interrupt
 *
 * If there are any PCI to PCI bridges between us and the requesting device,
 * the interrupt property will need to be translated according to the PCI to
 * PCI bridge spec.
 *
 * used by: pci_get_intrspec()
 *
 * return value: x'lated interrupt property
 */
static u_int
xlate_interrupt(dev_info_t *dip, dev_info_t *rdip, u_int device, u_int intr)
{
	dev_info_t *d;
	char *name;
	u_int result = intr;

	DBG3(D_G_ISPEC, NULL, "xlate_interrupts for %s%d intr=%x\n",
		ddi_get_name(rdip), ddi_get_instance(rdip), intr);

	for (d = ddi_get_parent(rdip); d != dip; d = ddi_get_parent(d)) {
		name = ddi_get_name(d);
		if (strcmp(name, "pci") == 0 || strcmp(name, "pci_pci") == 0) {
			result = (result - 1 + (device % 4)) % 4 + 1;
			DBG1(D_G_ISPEC, NULL,
				"pci to pci bridge - result=%x\n", result);
		}
	}
	return (result);
}

/*
 * iline_to_pil
 *
 * This routine returns a sparc pil for a given PCI device.  The routine
 * read the class code and sub class code from the devices configuration
 * header and uses this information to derive the pil.
 *
 * used by: pci_get_intrspec()
 *
 * return value: sparc pil for the given device
 */
/*ARGSUSED*/
static u_int
iline_to_pil(pci_devstate_t *pci_p, dev_info_t *child,
	u_int intr, u_int phys_hi)
{
	int class_code;
	u_char base_class_code;
	u_char sub_class_code;
#if defined(NO_CLASS_CODE)
	u_char *cp;
#endif
	u_int pil = 0;

	/*
	 * Use the "class-code" property to get the base and sub class
	 * codes for the requesting device.
	 */
	class_code = ddi_prop_get_int(DDI_DEV_T_ANY, child, DDI_PROP_DONTPASS,
					"class-code", -1);
	if (class_code != -1) {
		base_class_code = ((u_int)class_code & 0xff0000) >> 16;
		sub_class_code = ((u_int)class_code & 0xff00) >> 8;
	} else {

#if defined(NO_CLASS_CODE)
		/*
		 * If there is no "class-code" property, then get the
		 * base and sub class from the device's configuration
		 * header.
		 */
		cp = (u_char *)pci_config_addr(pci_p, phys_hi,
						PCI_CONF_BASCLASS);
		base_class_code = *cp;
		cp = (u_char *)pci_config_addr(pci_p, phys_hi,
						PCI_CONF_SUBCLASS);
		sub_class_code = *cp;
#else
		cmn_err(CE_WARN,
			"%s%d: %s%d has no class-code property\n",
			ddi_get_name(pci_p->dip), ddi_get_instance(pci_p->dip),
			ddi_get_name(dip), ddi_get_instance(dip),
		return (pil);
#endif
	}
	DBG2(D_G_ISPEC, pci_p, "base_class=%x sub_class=%x\n",
		base_class_code, sub_class_code);

	/*
	 * Use the class code values to construct an pil for the device.
	 */
	switch (base_class_code) {
	default:
	case PCI_CLASS_NONE:
		pil = 1;
		break;

	case PCI_CLASS_MASS:
		switch (sub_class_code) {
		case PCI_MASS_SCSI:
		case PCI_MASS_IDE:
		case PCI_MASS_FD:
		case PCI_MASS_IPI:
			pil = 0x4;
			break;
		}
		break;

	case PCI_CLASS_NET:
		pil = 0x6;
		break;

	case PCI_CLASS_DISPLAY:
		pil = 0x9;
		break;

	case PCI_CLASS_MM:
		pil = 0xa;
		break;

	case PCI_CLASS_MEM:
		pil = 0xa;
		break;

	case PCI_CLASS_BRIDGE:
		switch (sub_class_code) {
		case PCI_BRIDGE_HOST:
		case PCI_BRIDGE_ISA:
		case PCI_BRIDGE_EISA:
		case PCI_BRIDGE_MC:
		case PCI_BRIDGE_PCI:
		case PCI_BRIDGE_PCMCIA:
		default:
			pil = 0xa;
		}
		break;
	}
	DBG1(D_G_ISPEC, pci_p, "pil=%x\n", pil);
	return (pil);
}

/*
 * check_limits
 *
 * This routine is called from the dma map routine to sanity check the
 * limit structure and determine if the limit structure implies a
 * partial mapping.  If the size of the mapping exceeds the limits
 * and partial mapping is permitted, the partial size is returned
 * through the sizep parameter.
 *
 * used by: pci_dma_map()
 *
 * return value:
 *
 *	0			- on success
 *	DDI_DMA_NOMAPPING 	- if limits are bogus
 *	DDI_DMA_TOOBIG		- if limits are too small for transfer
 *				  an partial mapping not permitted
 */
static int
check_limits(pci_devstate_t *pci_p,
	struct ddi_dma_req *dmareq, u_int *sizep)
{
	u_int size = dmareq->dmar_object.dmao_size;
	u_int offset = size - 1;
	u_int range;

	/*
	 * Check limits:
	 */
	if (dmareq->dmar_limits->dlim_addr_lo >=
	    dmareq->dmar_limits->dlim_addr_hi) {
		DBG(D_DMA_MAP, pci_p, "dlim_addr_lo >= dlim_addr_hi\n");
		return (DDI_DMA_NOMAPPING);
	}

	/*
	 * Since IOMMU_DVMA_END is 0xffffffff there is no need
	 * to also check
	 *
	 *	    dmareq->dmar_limits->dlim_addr_lo > IOMMU_DVMA_END
	 */
	if (dmareq->dmar_limits->dlim_addr_hi <
			pci_p->psycho_p->iommu_dvma_base) {
		DBG(D_DMA_MAP, pci_p,
			"limits exclude dvma range (DDI_DMA_NOMAPPING)\n");
		return (DDI_DMA_NOMAPPING);
	}

	/*
	 * Check to be sure the transfer size is within the dma counter limit.
	 */
	if (offset > dmareq->dmar_limits->dlim_cntr_max) {
		DBG(D_DMA_MAP, pci_p, "size > cntr_max\n");
		if ((dmareq->dmar_flags & DDI_DMA_PARTIAL) == 0)
			return (DDI_DMA_TOOBIG);

		*sizep = dmareq->dmar_limits->dlim_cntr_max + 1;
		DBG1(D_DMA_MAP, pci_p, "DDI_DMA_PARTIAL size %x\n", *sizep);
	}

	/*
	 * Check to be sure the transfer size is within the dma address
	 * limits and dvma space.
	 */
	range = dmareq->dmar_limits->dlim_addr_hi -
		max(dmareq->dmar_limits->dlim_addr_lo,
			pci_p->psycho_p->iommu_dvma_base) + 1;
	if (offset > range) {
		DBG(D_DMA_MAP, pci_p, "offset too large\n");
		if ((dmareq->dmar_flags & DDI_DMA_PARTIAL) == 0)
			return (DDI_DMA_TOOBIG);
		*sizep = range;
		DBG1(D_DMA_MAP, pci_p, "DDI_DMA_PARTIAL size=%x\n", *sizep);
	}
	return (0);
}

/*
 * check_dma_attr
 *
 * This routine is called from the alloc handle entry point to sanity check the
 * dma attribute structure.
 *
 * use by; pci_dma_allochdl()
 *
 * return value:
 *
 *	0			- on success
 *	DDI_DMA_BADATTR		- attribute has invalid version number
 *				  or address limits exclude dvma space
 */

#if 1
static int pci_check_attr = 1;
#endif

static int
check_dma_attr(pci_devstate_t *pci_p, ddi_dma_attr_t *attr,
	psycho_dma_t *dma_type)
{
	u_longlong_t upper_bound;
	u_longlong_t lower_bound;

	/*
	 * Check version number.
	 */
	switch (attr->dma_attr_version) {
	case DMA_ATTR_V0:
		break;
	default:
		return (DDI_DMA_BADATTR);
	}

	if (attr->dma_attr_flags ||
			attr->dma_attr_addr_lo > (u_longlong_t)IOMMU_DVMA_END) {
		DBG(D_DMA_ALLOCH, pci_p, "bypass mode\n");
		*dma_type = IOMMU_BYPASS;
	} else
		*dma_type = IOMMU_XLATE;

	if (attr->dma_attr_addr_lo >= attr->dma_attr_addr_hi) {
		DBG(D_DMA_ALLOCH, pci_p,
			"dma_attr_addr_hi >= dma_attr_addr_hi\n");
		return (DDI_DMA_BADATTR);
	}

	switch (*dma_type) {
	case IOMMU_XLATE:
		lower_bound = (u_longlong_t)pci_p->psycho_p->iommu_dvma_base;
		upper_bound = (u_longlong_t)IOMMU_DVMA_END & 0xffffffffull;
		break;
	case IOMMU_BYPASS:
		lower_bound = IOMMU_BYPASS_BASE;
		upper_bound = IOMMU_BYPASS_END;
		break;
	}
	DBG4(D_DMA_ALLOCH, pci_p, "upper bound=%x.%x lower bound=%x.%x\n",
		(u_int)(upper_bound >> 32), (u_int)(upper_bound & 0xffffffff),
		(u_int)(lower_bound >> 32), (u_int)(lower_bound & 0xffffffff));

#if 1
	if (pci_check_attr == 0)
		return (0);
#endif

	/*
	 * Check the address limits.
	 */
	if (attr->dma_attr_addr_hi < lower_bound) {
		DBG2(D_DMA_ALLOCH, pci_p,
			"addr_hi (%x.%x) < lower_bound\n",
			(u_int)(attr->dma_attr_addr_hi >> 32),
			(u_int)(attr->dma_attr_addr_hi & 0xffffffff));
		return (DDI_DMA_BADATTR);
	}

	if (attr->dma_attr_addr_lo > upper_bound) {
		DBG2(D_DMA_ALLOCH, pci_p,
			"addr_lo (%x.%x) > upper_bound\n",
			(u_int)(attr->dma_attr_addr_lo >> 32),
			(u_int)(attr->dma_attr_addr_lo & 0xffffffff));
		return (DDI_DMA_BADATTR);
	}

	if (attr->dma_attr_addr_lo >= attr->dma_attr_addr_hi) {
		DBG(D_DMA_ALLOCH, pci_p,
			"dma_attr_addr_hi >= dma_attr_addr_hi\n");
		return (DDI_DMA_BADATTR);
	}
	return (0);
}

/*
 * check_dma_size
 *
 * This routine is called from the bind handle entry point to determine if the
 * dma request needs to be resized based upon the attributes for the handle.
 *
 * used by: pci_dma_bindhdl()
 *
 * return value:
 *
 *	0			- on success
 *	DDI_DMA_TOOBIG		- if limits are too small for transfer
 *				  an partial mapping not permitted
 */
static int
check_dma_size(pci_devstate_t *pci_p, ddi_dma_handle_t handle,
	struct ddi_dma_req *dmareq, u_int *sizep)
{

	ddi_dma_impl_t *mp = (ddi_dma_impl_t *)handle;
	u_int size = dmareq->dmar_object.dmao_size;
	u_int offset = size - 1;
	u_longlong_t range;

	if ((mp->dmai_flags & DMAI_FLAGS_BYPASS) == 0) {

		/*
		 * Check to be sure the transfer size is within the
		 * dma counter limit.
		 */
		if (offset > mp->dmai_attr.dma_attr_count_max) {
			DBG(D_DMA_BINDH, pci_p, "size > cntr_max\n");
			if ((dmareq->dmar_flags & DDI_DMA_PARTIAL) == 0)
				return (DDI_DMA_TOOBIG);
			*sizep = mp->dmai_attr.dma_attr_count_max + 1;
			DBG1(D_DMA_BINDH, pci_p, "DDI_DMA_PARTIAL size %x\n",
				*sizep);
		}
		range = mp->dmai_attr.dma_attr_addr_hi -
				max(mp->dmai_attr.dma_attr_addr_lo,
					pci_p->psycho_p->iommu_dvma_base) + 1;
	} else
		range = mp->dmai_attr.dma_attr_addr_hi -
				max(mp->dmai_attr.dma_attr_addr_lo,
					IOMMU_BYPASS_BASE) + 1;

	/*
	 * Check to be sure the transfer size is within the dma address
	 * limits and dvma space.
	 */
	if (offset > range) {
		DBG(D_DMA_BINDH, pci_p, "offset too large\n");
		if ((dmareq->dmar_flags & DDI_DMA_PARTIAL) == 0)
			return (DDI_DMA_TOOBIG);
		*sizep = range;
		DBG1(D_DMA_BINDH, pci_p, "DDI_DMA_PARTIAL size=%x\n", *sizep);
	}
	return (0);
}

/*
 * check_dma_target
 *
 * This routine is called from the dma map routine when the mapping
 * object is presented as a virtual address/size pair.  This routine
 * examines the physicalpages in the virtual address range to determine
 * if we have intra bus DMA or ordinary DVMA.  This routine also builds
 * and array of the page frame numbers for the virtual address range.
 * This array is stored in the dma handle's nexus private data in
 * case iommu translations are required.
 *
 * used by: pci_dma_map(),  pci_dma_bindhdl()
 *
 * return value:
 *
 *	DDI_SUCCESS
 *	DDI_DMA_NOMAPPING
 *
 */
static int
check_dma_target(pci_devstate_t *pci_p,
	u_long vaddr, struct as *as, u_int size, u_int offset,
	psycho_dma_t *dma_type, ddi_dma_impl_t *mp)
{
	u_int npages = IOMMU_BTOPR(size + offset);
	u_longlong_t paddr;
	u_int pfn, base_pfn, *pfnp;
	u_int i, psize, error = 0;
	enum { IO, MEM, OTHER, NONE } state = NONE;

	/*
	 * If we're not called as the result of an advisory mapping
	 * allocate an array to the pfn's of the virtual pages to be
	 * mapped so we don't do this again.
	 */
	if (mp) {
		psize = (npages + 1) * sizeof (u_int);
		pfnp = (u_int *) kmem_alloc(psize, KM_SLEEP);
		mp->dmai_nexus_private = (caddr_t)pfnp;
		*pfnp = psize;
		pfnp++;
		DBG2(D_CHK_TAR, pci_p, "dmai_nexus_private=%x (%x)\n",
			mp->dmai_nexus_private, psize);
	}

	AS_LOCK_ENTER(as, &as->a_lock, RW_READER);

	for (i = 0; i < npages && !error; i++, vaddr += IOMMU_PAGE_SIZE) {

		/*
		 * Get the pfn and physical address for this page.
		 */
		pfn = hat_getpfnum(as, (caddr_t)vaddr);
		if (pfn == (u_int) -1) {
			cmn_err(CE_WARN,
				"%s%d: can't get page frames for vaddr %x",
				ddi_get_name(pci_p->dip),
				ddi_get_instance(pci_p->dip),
				(u_int)vaddr);
			error = 1;
			break;
		}
		paddr = ((u_longlong_t)pfn << MMU_PAGESHIFT) | offset;
#ifdef DEBUG
		if (i < 2 || i == npages - 1) {
			DBG4(D_CHK_TAR, pci_p, "vaddr=%x pfn=%x paddr=%x.%x\n",
				vaddr, pfn, paddr >> 32, paddr & 0xffffffff);
		} else if (i == npages - 2) {
			DBG(D_CONT, pci_p, ".\n");
		} else
			DBG(D_CONT, pci_p, ".");
#endif

		/*
		 * If the physcal address of the page is outside this psycho's
		 * address then we will use the iommu.
		 */
		if (paddr < pci_p->base || paddr > pci_p->last) {

			/*
			 * dma outside psycho, check for mixed mode
			 */
			switch (state) {
			case NONE:
				DBG(D_CHK_TAR, pci_p, "state is OTHER\n");
				state = OTHER;
				*dma_type = IOMMU_XLATE;
				break;
			case OTHER:
				break;
			default:
				DBG(D_CHK_TAR, pci_p, "mix mode error\n");
				error = 1;
			}

			if (mp) pfnp[i] = pfn;
			continue;
		}

		/*
		 * Here if the physical address is within this psycho's
		 * address space.  Check to be sure it's either IO space
		 * or memory space and compute the PCI address corresponding
		 * the physical address.  In this case we don't use the iommu,
		 * we use the PCI address.
		 */
		DBG1(D_CHK_TAR, pci_p, "pfn %x within psycho\n", pfn);
		if (paddr >= pci_p->mem_base &&
			paddr < (pci_p->mem_base + PCI_MEM_SIZE)) {

			/*
			 * DMA to PCI memory space
			 */
			switch (state) {
			case NONE:
				DBG(D_CHK_TAR, pci_p, "state is PCI MEM\n");
				state = MEM;
				base_pfn = pci_p->mem_base >> MMU_PAGESHIFT;
				*dma_type = PCI_PEER_TO_PEER;
				mp->dmai_flags |= DMAI_FLAGS_PEER_TO_PEER;
				break;
			case MEM:
				break;
			default:
				DBG(D_CHK_TAR, pci_p, "PCI MEM mix error\n");
				error = 1;
			}

		} else {

			/*
			 * not PCI memory space - must be an error
			 */
			DBG(D_CHK_TAR, pci_p, "unaddressable error\n");
			error = 1;
		}
		if (mp)
			pfnp[i] = pfn - base_pfn;
	}

	AS_LOCK_EXIT(as, &as->a_lock);

	/*
	 * If there is a problem with the DMA target memory then we don't
	 * need to keep the page frame list around.
	 */
	if (mp && error) {
		DBG2(D_CHK_TAR, pci_p, "freeing nexus private data (%x, %x)\n",
			mp->dmai_nexus_private, psize);
		kmem_free(mp->dmai_nexus_private, psize);
		mp->dmai_nexus_private = NULL;
		return (DDI_DMA_NOMAPPING);
	}
	return (DDI_SUCCESS);
}


/*
 * Strings used by pci error logging functions.
 */
static char *pci_main_fmt =
	"%s error from pci (upa mid 0x%x, PBM %c) address=0x%llx\n";
static char *pci_blk_fmt =
	"transaction was a block operation, UPA bytemask %x\n";
static char *pci_sec_fmt =
	"%s secondary error\n";
static char *target_abort = "Target Abort";
static char *master_abort = "Master Abort";
static char *retries = "excessive retries";
static char *parity_error = "partiy error";

/*
 * pci_error_intr
 *
 * This routine is the interrupt handler for PCI bus error interrupts
 * for this PCI bus node.
 *
 * return value: DDI_INTR_CLAIMED
 */
static u_int
pci_error_intr(caddr_t a)
{
	pci_devstate_t *pci_p = (pci_devstate_t *)a;
	u_longlong_t afsr, afar;
	u_int err, upa_mid;
	char *pe = NULL;
	psycho_ino_info_t *ino_p;

	/*
	 * Read the asynchronous fault status register.
	 */
	afsr = *pci_p->afsr;
	afar = *pci_p->afar;

	/*
	 * Clear the errors.
	 */
	*pci_p->afsr = afsr;

	/*
	 * Clear the interrupt.
	 */
	ino_p = &pci_p->psycho_p->ino_info[MONDO_TO_INO(pci_p->be_mondo)];
	*ino_p->clear_reg = (*ino_p->clear_reg & ~PSYCHO_CIR_MASK) |
			PSYCHO_CIR_IDLE;

	/*
	 * Check for expect fault (ie ddi_poke)
	 */
	if (pci_p->pokefault == -1) {
		DBG(D_ERR_INTR, pci_p, "pokefault\n");
		pci_p->pokefault = 1;
	}

	/*
	 * Determine the type of primary error.
	 */
	err = (u_int)(afsr >> PCI_AFSR_PE_SHIFT) & PCI_AFSR_E_MASK;
	switch (err) {
	case PCI_AFSR_E_MA:
		pe = master_abort;
		break;
	case PCI_AFSR_E_TA:
		pe = target_abort;
		break;
	case PCI_AFSR_E_RTRY:
		pe = retries;
		break;
	case PCI_AFSR_E_PERR:
		pe = parity_error;
		break;
	}

	/*
	 * Determine the upa id that caused the error.
	 */
	upa_mid = (u_int)((afsr & PCI_AFSR_MID) >> PCI_AFSR_MID_SHIFT);

	cmn_err(CE_WARN, pci_main_fmt, pe, upa_mid,
			pci_p->bus_range.lo == 0 ? 'B' : 'A', afar);
	if (afsr & PCI_AFSR_BLK)
		cmn_err(CE_CONT, pci_blk_fmt,
			(int)((afsr & PCI_AFSR_BYTEMASK) >>
					PCI_AFSR_BYTEMASK_SHIFT));

	err = (u_int)(afsr >> PCI_AFSR_PE_SHIFT) & PCI_AFSR_E_MASK;
	if (err & PCI_AFSR_E_MA)
		cmn_err(CE_CONT, pci_sec_fmt, master_abort);
	if (err & PCI_AFSR_E_TA)
		cmn_err(CE_CONT, pci_sec_fmt, target_abort);
	if (err & PCI_AFSR_E_RTRY)
		cmn_err(CE_CONT, pci_sec_fmt, retries);
	if (err & PCI_AFSR_E_PERR)
		cmn_err(CE_CONT, pci_sec_fmt, parity_error);

	/*
	 * Need to call a routine to kill procs or panic.
	 */
#if defined(CALL_KILL_PROCS)
	if (pci_call_kill_procs) {
		/*
		 * kill_procs_on_page must be changed to global
		 * for this to work.
		 */
		if (kill_procs_on_page(&afsr) == -1)
			cmn_err(CE_PANIC, "fatal error");
	}
#endif
	return (DDI_INTR_CLAIMED);
}


/*
 * psycho_ue_intr
 *
 * This routine is the interrupt handler for uncorrectable asynchronous
 * errors.
 *
 *	Reads errors logged in UE AFSR and UE AFAR
 *	Clears errors logged in UE AFSR and UE AFAR
 *	Clears interrupt via clear interrupt register
 *	Calls system ue handler
 *
 * return value: DDI_INTR_CLAIMED
 */
static u_int
psycho_ue_intr(caddr_t a)
{
	psycho_devstate_t *psycho_p = (psycho_devstate_t *)a;
	psycho_ino_info_t *ino_p;
	u_longlong_t afsr, afar;
	u_char size, offset;
	u_short id, inst;
	struct ecc_flt ecc_flt;

	/*
	 * Read the fault registers.
	 */
	afsr = *psycho_p->ue_afsr;
	afar = *psycho_p->ue_afar;

	/*
	 * Clear the errors.
	 */
	*psycho_p->ue_afsr = afsr;

	/*
	 * Clear the interrupt.
	 */
	ino_p = &psycho_p->ino_info[MONDO_TO_INO(psycho_p->ue_mondo)];
	*ino_p->clear_reg = (*ino_p->clear_reg & ~PSYCHO_CIR_MASK) |
			PSYCHO_CIR_IDLE;

	/*
	 * Call system ecc handling code.
	 */
	size = (u_char) ((afsr & PSYCHO_UE_AFSR_BYTEMASK)
				>> PSYCHO_UE_AFSR_DW_OFFSET_SHIFT);
	offset = (u_char) ((afsr & PSYCHO_UE_AFSR_DW_OFFSET)
				>> PSYCHO_UE_AFSR_BYTEMASK_SHIFT);
	id = (u_short) psycho_p->upa_id;
	inst = (u_short) ddi_get_instance(psycho_p->dip);

	if (pci_call_ue_error)
		ue_error(&afsr, &afar, 0, size, offset, id, inst,
			(afunc)psycho_log_ue_error);
	else {
		ecc_flt.flt_stat = afsr;
		ecc_flt.flt_addr = afar;
		ecc_flt.flt_upa_id = id;
		ecc_flt.flt_inst = inst;
		(void) psycho_log_ue_error(&ecc_flt, "No SIMM Decoding");
	}
	return (DDI_INTR_CLAIMED);
}


/*
 * Strings used by error logging functions.
 */
static char *ecc_main_fmt =
	"%s error from pci%d (upa mid 0x%x) during %s transaction\n";
static char *ecc_blk_fmt =
	"transaction was a block operation, UPA bytemask 0x%x\n";
static char *dw_fmt =
	"address is 0x%llx, doubleword offset is 0x%x, id is %d\n";
static char *ecc_sec_fmt =
	"secondary error from %s transaction\n";
static char *dvma_rd = "dvma read";
static char *dvma_wr = "dvma write";
static char *pio_wr = "pio write";

/*
 * psycho_log_ue_error
 *
 * This function is called as a result of uncorrectable error interrupts
 * to log the error.
 *
 * used by: ue error handling interface
 *
 * return value: TBD
 */
/* ARGSUSED */
static u_int
psycho_log_ue_error(struct ecc_flt *ecc, char *unum)
{
	u_longlong_t afsr = ecc->flt_stat;
	u_longlong_t afar = ecc->flt_addr;
	u_short id = ecc->flt_upa_id;
	u_short inst = ecc->flt_inst;
	u_int err, upa_mid, dw_offset;
	char *pe;

	/*
	 * Determine the primary error type.
	 */
	err = (u_int)(afsr >> PSYCHO_UE_AFSR_PE_SHIFT) &
			PSYCHO_UE_AFSR_E_MASK;
	switch (err) {
	case PSYCHO_UE_AFSR_E_PIO:
		pe = pio_wr;
		break;
	case PSYCHO_UE_AFSR_E_DRD:
		pe = dvma_rd;
		break;
	case PSYCHO_UE_AFSR_E_DWR:
		pe = dvma_wr;
		break;
	}

	/*
	 * Get the upa id that caused the error.
	 */
	upa_mid = (u_int)((afsr & PSYCHO_UE_AFSR_MID)
				>> PSYCHO_UE_AFSR_MID_SHIFT);

	/*
	 * Determine the doubleword offset of the error.
	 */
	dw_offset = (u_int)((afsr & PSYCHO_UE_AFSR_DW_OFFSET)
				>> PSYCHO_UE_AFSR_DW_OFFSET_SHIFT);

	/*
	 * Log the errors.
	 */
	cmn_err(CE_WARN, ecc_main_fmt, "Uncorrectable", inst, upa_mid, pe);
	if (afsr & PSYCHO_UE_AFSR_BLK)
		cmn_err(CE_CONT, ecc_blk_fmt,
			(int)((afsr & PSYCHO_UE_AFSR_BYTEMASK)
				>> PSYCHO_UE_AFSR_BYTEMASK_SHIFT));
	cmn_err(CE_CONT, dw_fmt, afar, dw_offset, id);
	err = (u_int)(afsr >> PSYCHO_UE_AFSR_SE_SHIFT) &
			PSYCHO_UE_AFSR_E_MASK;
	if (err & PSYCHO_UE_AFSR_E_PIO)
		cmn_err(CE_CONT, ecc_sec_fmt, pio_wr);
	if (err & PSYCHO_UE_AFSR_E_DRD)
		cmn_err(CE_CONT, ecc_sec_fmt, dvma_rd);
	if (err & PSYCHO_UE_AFSR_E_DWR)
		cmn_err(CE_CONT, ecc_sec_fmt, dvma_wr);
	return (1);
}


/*
 * psycho_ce_intr
 *
 * This routine is the interrupt handler for correctable asynchronous
 * errors.
 *
 *	Reads errors logged in CE AFSR and CE AFAR
 *	Clears errors logged in CE AFSR and CE AFAR
 *	Clears interrupt via clear interrupt register
 *	Calls system ce handler
 *
 * return value: DDI_INTR_CLAIMED
 */
static u_int
psycho_ce_intr(caddr_t a)
{
	psycho_devstate_t *psycho_p = (psycho_devstate_t *)a;
	psycho_ino_info_t *ino_p;
	u_longlong_t afsr, afar;
	u_short err, id, inst;
	u_char ecc_synd, size, offset;
	struct ecc_flt ecc_flt;

	/*
	 * Read the fault registers.
	 */
	afsr = *psycho_p->ce_afsr;
	afar = *psycho_p->ce_afar;

	/*
	 * Check for false alarms.
	 */
	err = (u_short)(afsr >> PSYCHO_CE_AFSR_PE_SHIFT) &
			PSYCHO_CE_AFSR_E_MASK;
	if (err == 0) {
		DBG1(D_ERR_INTR, (pci_devstate_t *)psycho_p->upa_id,
			"ce: false alarm\n", psycho_p->upa_id);
		return (DDI_INTR_CLAIMED);
	}

	/*
	 * Clear the errors
	 */
	*psycho_p->ce_afsr = afsr;

	/*
	 * Clear the interrupt.
	 */
	ino_p = &psycho_p->ino_info[MONDO_TO_INO(psycho_p->ce_mondo)];
	*ino_p->clear_reg = (*ino_p->clear_reg & ~PSYCHO_CIR_MASK) |
			PSYCHO_CIR_IDLE;

	/*
	 * Call system ecc handling code.
	 */
	ecc_synd = (u_char)((afsr & PSYCHO_CE_AFSR_SYND)
				>> PSYCHO_CE_AFSR_SYND_SHIFT);
	size = (u_char)((afsr & PSYCHO_CE_AFSR_BYTEMASK)
				>> PSYCHO_CE_AFSR_BYTEMASK_SHIFT);
	offset = (u_char)((afsr & PSYCHO_CE_AFSR_DW_OFFSET)
				>> PSYCHO_CE_AFSR_DW_OFFSET_SHIFT);
	id = (u_short) psycho_p->upa_id;
	inst = (u_short) ddi_get_instance(psycho_p->dip);

	if (pci_call_ce_error)
		ce_error(&afsr, &afar, ecc_synd, size, offset, id, inst,
				(afunc)psycho_log_ce_error);
	else {
		ecc_flt.flt_stat = afsr;
		ecc_flt.flt_addr = afar;
		ecc_flt.flt_upa_id = id;
		ecc_flt.flt_inst = inst;
		(void) psycho_log_ce_error(&ecc_flt, "No SIMM Decoding");
	}
	return (DDI_INTR_CLAIMED);
}


/*
 * psycho_log_ce_error
 *
 * This function is called as a result of uncorrectable error interrupts
 * to log the error.
 *
 * used by: ce error handling interface
 *
 * return value: TBD
 */
/* ARGSUSED */
static u_int
psycho_log_ce_error(struct ecc_flt *ecc, char *unum)
{
	u_longlong_t afsr = ecc->flt_stat;
	u_longlong_t afar = ecc->flt_addr;
	u_short id = ecc->flt_upa_id;
	u_short inst = ecc->flt_inst;
	u_int err, upa_mid, dw_offset, ecc_synd;
	char *pe;

	/*
	 * Determine the primary error type.
	 */
	err = (u_int)(afsr >> PSYCHO_CE_AFSR_PE_SHIFT) &
			PSYCHO_CE_AFSR_E_MASK;
	switch (err) {
	case PSYCHO_CE_AFSR_E_PIO:
		pe = pio_wr;
		break;
	case PSYCHO_CE_AFSR_E_DRD:
		pe = dvma_rd;
		break;
	case PSYCHO_CE_AFSR_E_DWR:
		pe = dvma_wr;
		break;
	}

	/*
	 * Get the upa id that caused the error.
	 */
	upa_mid = (u_int)((afsr & PSYCHO_UE_AFSR_MID)
				>> PSYCHO_UE_AFSR_MID_SHIFT);

	/*
	 * Determine the doubleword offset of the error.
	 */
	dw_offset = (u_int)((afsr & PSYCHO_UE_AFSR_DW_OFFSET)
				>> PSYCHO_UE_AFSR_DW_OFFSET_SHIFT);

	/*
	 * Determine the error syndrome bits.
	 */
	ecc_synd = (u_int)((afsr & PSYCHO_CE_AFSR_SYND)
				>> PSYCHO_CE_AFSR_SYND_SHIFT);

	/*
	 * Log the errors.
	 */
	cmn_err(CE_WARN, ecc_main_fmt, "Correctable", inst, upa_mid, pe);
	cmn_err(CE_CONT, dw_fmt, afar, dw_offset, id);
	cmn_err(CE_CONT, "syndrome bits %x\n", ecc_synd);
	err = (u_int)(afsr >> PSYCHO_CE_AFSR_SE_SHIFT) &
			PSYCHO_CE_AFSR_E_MASK;
	if (err & PSYCHO_CE_AFSR_E_PIO)
		cmn_err(CE_CONT, ecc_sec_fmt, pio_wr);
	if (err & PSYCHO_CE_AFSR_E_DRD)
		cmn_err(CE_CONT, ecc_sec_fmt, dvma_rd);
	if (err & PSYCHO_CE_AFSR_E_DWR)
		cmn_err(CE_CONT, ecc_sec_fmt, dvma_wr);
	return (1);
}


/*
 * report_dev
 *
 * This function is called from our control ops routine on a
 * DDI_CTLOPS_REPORTDEV request.
 *
 * The display format is
 *
 *	<name><inst> at <pname><pinst> device <dev> function <func>
 *
 * where
 *
 *	<name>		this device's name property
 *	<inst>		this device's instance number
 *	<name>		parent device's name property
 *	<inst>		parent device's instance number
 *	<dev>		this device's device number
 *	<func>		this device's function number
 */
static int
report_dev(dev_info_t *dip)
{
	dev_info_t *pdip;
	char *a, func = 0, device[4];
	int i;

	/*
	 * Get the devices function and device number from
	 * its name_addr.
	 */
	a = ddi_get_name_addr(dip);
	for (i = 0; *a; a++, i++) {
		if (*a == ',') {
			a++;
			func = *a;
			break;
		}
		device[i] = *a;
	}
	device[i] = '\0';

	/*
	 * Now report it.
	 */
	pdip = ddi_get_parent(dip);
	if (func)
		cmn_err(CE_CONT, "?%s%d at %s%d: device %s function %c\n",
			ddi_get_name(dip), ddi_get_instance(dip),
			ddi_get_name(pdip), ddi_get_instance(pdip),
			device, func);
	else
		cmn_err(CE_CONT, "?%s%d at %s%d: device %s function 0\n",
			ddi_get_name(dip), ddi_get_instance(dip),
			ddi_get_name(pdip), ddi_get_instance(pdip),
			device);
	return (DDI_SUCCESS);
}


/*
 * init_child
 *
 * This function is called from our control ops routine on a
 * DDI_CTLOPS_INITCHILD request.  It builds and sets the device's
 * parent private data area.
 *
 * used by: pci_ctlops()
 *
 * return value: none
 */
static int
init_child(dev_info_t *child)
{
	pci_regspec_t *pci_rp;
	char name[10];
	int i;
	uint_t func;

	/*
	 * Set the address portion of the node name based on
	 * the function and device number.
	 */
	if (ddi_getlongprop(DDI_DEV_T_NONE, child, DDI_PROP_DONTPASS, "reg",
				(caddr_t)&pci_rp, &i) != DDI_SUCCESS) {
		return (DDI_FAILURE);
	}
	func = PCI_REG_FUNC_G(pci_rp[0].pci_phys_hi);
	if (func != 0)
		sprintf(name, "%x,%x",
			PCI_REG_DEV_G(pci_rp[0].pci_phys_hi), func);
	else
		sprintf(name, "%x", PCI_REG_DEV_G(pci_rp[0].pci_phys_hi));
	ddi_set_name_addr(child, name);
	ddi_set_parent_data(child, NULL);
	kmem_free((caddr_t)pci_rp, i);
	return (DDI_SUCCESS);
}


/*
 * get_reg_set_size
 *
 * Given a dev info pointer to a pci child and a register number, this
 * routine returns the size element of that reg set property.
 *
 * used by: pci_ctlops() - DDI_CTLOPS_REGSIZE
 *
 * return value: size of reg set on success, zero on error
 */
static u_int
get_reg_set_size(dev_info_t *child, int rnumber)
{
	pci_regspec_t *pci_rp;
	u_int size;
	int i;

	if (rnumber < 0)
		return (0);

	/*
	 * Get the reg property for the device.
	 */
	if (ddi_getlongprop(DDI_DEV_T_NONE, child, DDI_PROP_DONTPASS, "reg",
				(caddr_t)&pci_rp, &i) != DDI_SUCCESS)
		return (0);

	if (rnumber >= (i / (int)sizeof (pci_regspec_t)))
		return (0);

	size = pci_rp[rnumber].pci_size_low;
	kmem_free((caddr_t)pci_rp, i);
	return (size);
}


/*
 * get_nreg_set
 *
 * Given a dev info pointer to a pci child, this routine returns the
 * number of sets in its "reg" property.
 *
 * used by: pci_ctlops() - DDI_CTLOPS_NREGS
 *
 * return value: # of reg sets on success, zero on error
 */
static u_int
get_nreg_set(dev_info_t *child)
{
	pci_regspec_t *pci_rp;
	int i, n;

	/*
	 * Get the reg property for the device.
	 */
	if (ddi_getlongprop(DDI_DEV_T_NONE, child, DDI_PROP_DONTPASS, "reg",
				(caddr_t)&pci_rp, &i) != DDI_SUCCESS)
		return (0);

	n =  i / (int)sizeof (pci_regspec_t);
	kmem_free((caddr_t)pci_rp, i);
	return (n);
}


/*
 * get_nintr
 *
 * Given a dev info pointer to a pci child, this routine returns the
 * number of items in its "interrupts" property.
 *
 * used by: pci_ctlops() - DDI_CTLOPS_NREGS
 *
 * return value: # of interrupts on success, zero on error
 */
static u_int
get_nintr(dev_info_t *child)
{
	int *pci_ip;
	int i, n;

	if (ddi_getlongprop(DDI_DEV_T_NONE, child,
				DDI_PROP_DONTPASS, "interrupts",
				(caddr_t)&pci_ip, &i) != DDI_SUCCESS)
		return (0);

	n = i / (int)sizeof (u_int);
	kmem_free((caddr_t)pci_ip, i);
	return (n);
}


/*
 * get_dvma_pages
 *
 * The routines allocates npages of dvma space between the addresses
 * of addrlo and addrhi.  If no such space is available, the routine
 * will sleep based on the value on cansleep.
 *
 * used by: pci_dma_map(), pci_dma_bindhdl() and
 *		pci_dma_mctl() - DDI_DMA_RESERVE
 *
 * return value: dvma page frame number on success, on error
 */
static u_long
get_dvma_pages(pci_devstate_t *pci_p, int npages,
	u_long addrlo, u_long addrhi, int cansleep)
{
	u_long alo, ahi, addr;
	u_int ok;
	struct map *mp, *dvmamap = pci_p->psycho_p->dvma_map;

	if (addrhi != (u_long) -1) {
		/*
		 * -1 is our magic NOOP for no high limit. If it's not -1,
		 * make addrhi 1 bigger since ahi is a non-inclusive limit,
		 * but addrhi is an inclusive limit.
		 */
		addrhi++;
		ahi = addrhi >> IOMMU_PAGE_SHIFT;
	} else
		ahi = (addrhi >> IOMMU_PAGE_SHIFT) + 1;
	alo = addrlo >> IOMMU_PAGE_SHIFT;

	if (alo > IOMMU_BTOP(pci_p->psycho_p->iommu_dvma_base) ||
			ahi <= IOMMU_BTOP(IOMMU_DVMA_END)) {
		/*
		 * We have a constrained allocation.  Search for a piece that
		 * that fits.
		 */
		DBG(D_RMAP, pci_p, "constrained allocation\n");
		mutex_enter(&maplock(dvmamap));
again:
		for (mp = mapstart(dvmamap); mp->m_size; mp++) {
			if (alo < mp->m_addr) {
				if (ahi >= mp->m_addr + mp->m_size)
					ok = (mp->m_size >= npages);
				else
					ok = (mp->m_addr + npages <= ahi);
				addr = mp->m_addr;
			} else {
				if (ahi >= mp->m_addr + mp->m_size)
					ok = (alo + npages <=
						mp->m_addr + mp->m_size);
				else
					ok = (alo + npages <= ahi);
				addr = alo;
			}
			DBG3(D_RMAP, pci_p, "map entry %x:%x %s\n",
				mp->m_addr, mp->m_addr + mp->m_size,
				ok ? "ok" : "not ok");
			if (ok)
				break;
		}

		if (mp->m_size == 0) {
			DBG(D_RMAP, pci_p, "can't find slot\n");
			addr = 0;
			if (cansleep) {
				DBG(D_RMAP, pci_p,
					"sleep on constrained alloc\n");
				mapwant(dvmamap) = 1;
				cv_wait(&map_cv(dvmamap), &maplock(dvmamap));
				goto again;
			}
		}

		/*
		 * We found an appropriate map entry, now let rmget do the
		 * rest of the work.
		 */
		addr = rmget(dvmamap, (long)npages, addr);
		mutex_exit(&maplock(dvmamap));

	} else {
		/*
		 * We can allocate from anywhere within the map.
		 */
		if (cansleep)
			addr = rmalloc_wait(dvmamap, npages);
		else
			addr = rmalloc(dvmamap, npages);
	}
	return (addr << IOMMU_PAGE_SHIFT);
}

/*
 * map_window
 *
 * This routine is called to program a dvma window into the iommu.
 * Non partial mappings are viewed as single window mapping.
 *
 * used by: pci_dma_map(), pci_dma_bindhdl(), pci_dma_win(),
 *	and pci_dma_mctl() - DDI_DMA_MOVWIN, DDI_DMA_NEXTWIN
 *
 * return value: none
 */
static void
map_window(pci_devstate_t *pci_p, ddi_dma_impl_t *mp, u_int window)
{
	psycho_devstate_t *psycho_p = pci_p->psycho_p;
	u_long page_offset, offset;
	u_long dvma_addr;
	u_int npages, size;
	page_t *pp;
	u_int *pfn_list, pfn;
	int i;

	/*
	 * Determine the starting location of the next window.
	 */
	offset = window * mp->dmai_winsize;
	size = MIN(mp->dmai_winsize, mp->dmai_object.dmao_size - offset);
	DBG4(D_MAP_WIN, pci_p, "window=%x offset=%x (%x pages) size=%x\n",
		window, offset, offset / IOMMU_PAGE_SIZE, size);

	switch (mp->dmai_object.dmao_type) {
	case DMA_OTYP_VADDR:

		/*
		 * The handle's nexus private data contains the pfn
		 * array for the mapping.
		 */
		page_offset =
			(u_long) mp->dmai_object.dmao_obj.virt_obj.v_addr &
					IOMMU_PAGE_OFFSET;
		pfn_list = (u_int *) mp->dmai_nexus_private + 1;
		pfn_list += offset / IOMMU_PAGE_SIZE;
		npages = mmu_btopr(size + page_offset);
		dvma_addr = mp->dmai_mapping;
		DBG3(D_MAP_WIN, pci_p,
			"VADDR pfn_list=%x - %x page(s) at %x - pfn(s)=",
			pfn_list, npages, dvma_addr);
		for (i = 0; i < npages; i++) {
			if (pci_flush_on_alloc)
				*psycho_p->iommu_flush_reg =
					(dvma_addr & IOMMU_PAGE_MASK);
			DBG1(D_MAP_WIN|D_CONT, pci_p, "%x ", pfn_list[i]);
			IOMMU_LOAD_TTE(psycho_p->tsb_paddr,
					IOMMU_TSB_INDEX(psycho_p, dvma_addr),
					IOMMU_MAKE_TTE(mp, pfn_list[i]));
			dvma_addr += IOMMU_PAGE_SIZE;
		}
		DBG(D_MAP_WIN|D_CONT, pci_p, "\n");
		break;

	case DMA_OTYP_PAGES:

		/*
		 * Follow the page structure list until we get to
		 * the window's range.
		 *
		 * This code assumes the iommu's and mmu's page
		 * is the same.
		 */
		page_offset = mp->dmai_object.dmao_obj.pp_obj.pp_offset;
		pp = mp->dmai_object.dmao_obj.pp_obj.pp_pp;
		npages = mmu_btopr(size + page_offset);
		dvma_addr = mp->dmai_mapping;
		DBG2(D_MAP_WIN, pci_p, "PAGES - %x page(s) at %x - pfn(s)=",
			npages, dvma_addr);
		for (i = 0; i < npages; i++) {
			if (pci_flush_on_alloc)
				*psycho_p->iommu_flush_reg =
					(dvma_addr & IOMMU_PAGE_MASK);
			pfn = page_pptonum(pp);
			DBG1(D_MAP_WIN|D_CONT, pci_p, "%x ", pfn);
			IOMMU_LOAD_TTE(psycho_p->tsb_paddr,
					IOMMU_TSB_INDEX(psycho_p, dvma_addr),
					IOMMU_MAKE_TTE(mp, pfn));
			pp = pp->p_next;
			dvma_addr += IOMMU_PAGE_SIZE;
		}
		DBG(D_MAP_WIN|D_CONT, pci_p, "\n");
		break;
	}
	mp->dmai_ndvmapages = npages;
	mp->dmai_size = size;
	mp->dmai_offset = offset;

	/*
	 * Set up the red zone if requested.
	 */
	if (mp->dmai_rflags & DDI_DMA_REDZONE) {
		dvma_addr += mp->dmai_ndvmapages * IOMMU_PAGE_SIZE;
		DBG1(D_MAP_WIN, pci_p, "red zone at %x\n", dvma_addr);
		IOMMU_UNLOAD_TTE(psycho_p->tsb_paddr,
				IOMMU_TSB_INDEX(psycho_p, dvma_addr));
		*psycho_p->iommu_flush_reg = dvma_addr;
	}
	dump_dma_handle(D_MAP_WIN, pci_p, mp);
}

/*
 * umap_window
 *
 * This routine is called to break down the iommu mappings to a dvma window.
 * Non partial mappings are viewed as single window mapping.
 *
 * used by: pci_dma_unbindhdl(), pci_dma_window(),
 *	and pci_dma_mctl() - DDI_DMA_FREE, DDI_DMA_MOVWIN,
 *	and DDI_DMA_NEXTWIN
 *
 * return value: none
 */
static void
unmap_window(pci_devstate_t *pci_p, ddi_dma_impl_t *mp)
{
	psycho_devstate_t *psycho_p = pci_p->psycho_p;
	u_long dvma_addr;
	u_int npages;

	dvma_addr = mp->dmai_mapping & ~IOMMU_PAGE_OFFSET;
	npages = mp->dmai_ndvmapages;
	DBG2(D_UNMAP_WIN, pci_p, "%x page(s) at %x - flush reg:",
		npages, dvma_addr);
	while (npages) {
		IOMMU_UNLOAD_TTE(psycho_p->tsb_paddr,
					IOMMU_TSB_INDEX(psycho_p, dvma_addr));
		DBG1(D_UNMAP_WIN|D_CONT, pci_p, " %x",
			dvma_addr & IOMMU_PAGE_MASK);
		*psycho_p->iommu_flush_reg =
			(dvma_addr & IOMMU_PAGE_MASK);
		dvma_addr += IOMMU_PAGE_SIZE;
		npages--;
	}
	DBG(D_UNMAP_WIN|D_CONT, pci_p, "\n");
}


/*
 * sbuf_flush
 *
 * This function is called during bus nexus requests that require the
 * streaming cache to be flushed (dma mapping, dma binding, dma freeing,
 * dma unbinding, dma syncing, etc...).
 *
 * used by: pci_dma_unbindhdl(), pci_dma_window(), pci_dma_flush(),
 *	and pci_dma_mctl() - DDI_DMA_FREE, DDI_DMA_SYNC,
 *	DDI_DMA_MOVWIN, and DDI_DMA_NEXTWIN
 *
 * return value: none
 */
static void
sbuf_flush(pci_devstate_t *pci_p, ddi_dma_impl_t *mp,
	off_t offset, u_int length)
{
	u_long dvma_addr, npages;
	u_int poffset;
	clock_t start_bolt;
	extern clock_t lbolt;

	/*
	 * If the caches are disabled, there's nothing to do.
	 */
	DBG4(D_SBUF, pci_p,
		"dmai_mapping=%x, dmai_size=%x offset=%x length=%x\n",
		mp->dmai_mapping, mp->dmai_size, offset, length);
	if (!(pci_stream_buf_enable & (1 << ddi_get_instance(pci_p->dip)))) {
		DBG(D_SBUF, pci_p, "cache disabled\n");
		return;
	}

	/*
	 * Interpret the offset parameter.
	 */
	if (offset == (u_int)-1)
		offset = 0;
	if (offset >= mp->dmai_size) {
		DBG(D_SBUF, pci_p, "offset > mapping size\n");
		return;
	}
	dvma_addr = mp->dmai_mapping + offset;

	/*
	 * Interpret the length parameter.
	 */
	switch (length) {
	case 0:
	case (u_int)-1:
		length = mp->dmai_size;
		break;
	default:
		if (length > (mp->dmai_size - offset)) {
			DBG(D_SBUF, pci_p, "length > mapping size - offset\n");
			length = mp->dmai_size - offset;
		}
		break;
	}

	/*
	 * Grap the flush mutex.
	 */
	mutex_enter(&pci_p->sync_mutex);

	/*
	 * Initialize the sync flag to zero.
	 */
	*pci_p->sync_flag_vaddr = 0x0ull;

	/*
	 * Cause the flush on all virtual pages of the transfer.
	 *
	 * We start flushing from the end and work our way back to the
	 * beginning.  This should minimize the time we need to spend
	 * polling the sync flag.
	 */
	poffset = dvma_addr & IOMMU_PAGE_OFFSET;
	dvma_addr &= ~poffset;
	npages = IOMMU_BTOPR(length + poffset);
	DBG2(D_SBUF, pci_p, "addr=%x size=%x - flush reg:", dvma_addr, length);
	while (npages) {
		DBG1(D_SBUF|D_CONT, pci_p, " %x", dvma_addr & IOMMU_PAGE_MASK);
		*pci_p->sbuf_invl_reg =
			(u_longlong_t)(dvma_addr & IOMMU_PAGE_MASK);
		dvma_addr += IOMMU_PAGE_SIZE;
		npages--;
	}
	DBG(D_SBUF|D_CONT, pci_p, "\n");

	/*
	 * Ask the hardware to flag when the flush is complete.
	 */
	DBG1(D_SBUF, pci_p, "writing %x to flush sync register\n",
		pci_p->sync_flag_addr);
	*pci_p->sbuf_sync_reg = pci_p->sync_flag_addr;

	/*
	 * Poll the flush/sync flag.
	 */
	DBG(D_SBUF, pci_p, "polling flush sync buffer\n");
	start_bolt = lbolt;
	while (*pci_p->sync_flag_vaddr == 0x0ull) {
		if (lbolt - start_bolt >= pci_sync_buf_timeout) {
			cmn_err(CE_PANIC,
				"%s%d: streaming buffer flush timeout!",
				ddi_get_name(pci_p->dip),
				ddi_get_instance(pci_p->dip));
		}
	}

	/*
	 * Release the flush mutex.
	 */
	mutex_exit(&pci_p->sync_mutex);
}

/*
 * create_bypass_cookies
 *
 * return value: none
 */
static int
create_bypass_cookies(pci_devstate_t *pci_p, ddi_dma_impl_t *mp,
	ddi_dma_cookie_t *cookiep, u_int *ccountp)
{
	ddi_dma_cookie_t *cp = cookiep;
	u_int size = mp->dmai_size;
	u_int cc = 0;
	u_long offset;
	u_int balance, npages;
	u_int pfn, prev_pfn;
	u_longlong_t counter_max;
	u_int sgllen;
	page_t *pp;
	u_int *p;
	int i;

	/*
	 * Get the dma counter attribute.  We need to make sure that
	 * the size of any single cookie doesn't exceed this value.
	 */
	counter_max = mp->dmai_attr.dma_attr_count_max;
	if (counter_max == 0)
		counter_max = 0xffffffffffffffffull;

	/*
	 * Get the dma scatter/gather list length attribute.  This
	 * determines how many cookies we will be able to create.
	 */
	sgllen = (u_int)mp->dmai_attr.dma_attr_sgllen;
	DBG3(D_BYPASS, pci_p, "counter_max=%x.%x sgllen=%x\n",
		(u_int)(counter_max >> 32),
		(u_int)(counter_max & 0xffffffff), sgllen);

	switch (mp->dmai_object.dmao_type) {
	case DMA_OTYP_VADDR:

		/*
		 * The handle's nexus private data contains the pfn
		 * array for the mapping.
		 */
		offset = (u_long)mp->dmai_object.dmao_obj.virt_obj.v_addr &
					IOMMU_PAGE_OFFSET;
		p = (u_int *)mp->dmai_nexus_private;
		npages = (*p / sizeof (u_int)) - 1;
		p++;
		DBG4(D_BYPASS, pci_p,
			"VADDR p=%x, npages=%x vaddr=%x offset=%x\n",
			p, npages, mp->dmai_object.dmao_obj.virt_obj.v_addr,
			offset);

		/*
		 * Start the first cookie by initializing its address.
		 */
		if (cc++ > sgllen) {
			DBG1(D_BYPASS, pci_p, "sgllen exceeded - cc=%x\n", cc);
			return (DDI_DMA_TOOBIG);
		}
		DBG1(D_BYPASS, pci_p, "p[0]=%x\n", p[0]);
		cp->dmac_laddress = IOMMU_BYPASS_ADDR(p[0], offset);
		balance = IOMMU_PAGE_SIZE - offset;
		npages--;

		/*
		 * See if we need more than one cookie.  We will if the
		 * current pfn does not follow the previous pfn or if
		 * the current cookie size would exceed the limit
		 * counter.
		 */
		for (i = 1; size > balance && npages; i++, npages--) {

			DBG2(D_BYPASS, pci_p, "p[%x]=%x\n", i, p[i]);
			if (p[i] != p[i - 1] + 1 ||
					balance + MMU_PAGESIZE > counter_max) {

				/*
				 * We need another cookie. Set the size of
				 * current cookie and advance on to the
				 * next one.
				 */
				if (cc++ > sgllen) {
					DBG1(D_BYPASS, pci_p,
						"sgllen exceeded - %x\n", cc);
					return (DDI_DMA_TOOBIG);
				}
				cp->dmac_size = balance;
				size -= balance;
				balance = 0;
				DBG3(D_BYPASS, pci_p, "cookie (%x,%x,%x)\n",
					cp->dmac_notused, cp->dmac_address,
					cp->dmac_size);

				cp++;
				cp->dmac_laddress = IOMMU_BYPASS_ADDR(p[i], 0);
			}
			balance += MMU_PAGESIZE;
		}
		break;

	case DMA_OTYP_PAGES:

		offset = mp->dmai_object.dmao_obj.pp_obj.pp_offset;
		pp = mp->dmai_object.dmao_obj.pp_obj.pp_pp;
		npages = mmu_btopr(size + offset);
		DBG2(D_BYPASS, pci_p, "PAGES npages=%x counter_max=%x\n",
			npages, counter_max);

		/*
		 * Start the fisrt cookie by initializing its address.
		 */
		pfn = page_pptonum(pp);
		DBG1(D_BYPASS, pci_p, "pfn=%x\n", pfn);
		cp->dmac_laddress = IOMMU_BYPASS_ADDR(pfn, offset);
		balance = IOMMU_PAGE_SIZE - offset;
		npages--;

		/*
		 * See if we need more than one cookie.  We will if the
		 * current pfn does not follow the previous pfn or if
		 * the current cookie size would exceed the limit
		 * counter.
		 */
		while (size > balance && npages) {

			prev_pfn = pfn;
			pp = pp->p_next;
			pfn = page_pptonum(pp);
			DBG1(D_BYPASS, pci_p, "pfn=%x\n", pfn);
			if (pfn != prev_pfn ||
					balance + MMU_PAGESIZE > counter_max) {

				/*
				 * We need another cookie. Set the size of
				 * current cookie and advance on to the
				 * next one.
				 */
				if (cc++ > sgllen) {
					DBG1(D_BYPASS, pci_p,
						"sgllen exceeded - %x\n", cc);
					return (DDI_DMA_TOOBIG);
				}
				cp->dmac_size = balance;
				size -= balance;
				balance = 0;
				DBG3(D_BYPASS, pci_p, "cookie (%x,%x,%x)\n",
					cp->dmac_notused, cp->dmac_address,
					cp->dmac_size);

				cp++;
				cp->dmac_laddress = IOMMU_BYPASS_ADDR(pfn, 0);
			}
			balance += MMU_PAGESIZE;
			npages--;
		}
		break;
	}

	/*
	 * Finish up the last cookie.
	 */
	cp->dmac_size = size;
	*ccountp = cc;
	DBG4(D_BYPASS, pci_p, "last cookie (%x,%x,%x), count=%x\n",
		cp->dmac_notused, cp->dmac_address, cp->dmac_size, cc);
	return (0);
}


/*
 * The following routines are used to implement the sun4u fast dvma
 * routines on this bus.
 */

/*ARGSUSED*/
void
fast_dvma_kaddr_load(ddi_dma_handle_t h, caddr_t a, u_int len, u_int index,
	ddi_dma_cookie_t *cp)
{
	ddi_dma_impl_t *mp = (ddi_dma_impl_t *)h;
	struct fast_dvma *fdvma = (struct fast_dvma *)mp->dmai_nexus_private;
	pci_devstate_t *pci_p = (pci_devstate_t *)fdvma->softsp;
	psycho_devstate_t *psycho_p = pci_p->psycho_p;
	u_long offset, dvma_addr;
	u_int npages, pfn;
	int i;

	/*
	 * Make sure the index and length are in the range of the reserved
	 * space.
	 */
	DBG3(D_DVMA_KADDR_LOAD, pci_p, "a=%x len=%x index=%x\n", a, len, index);
	offset = (u_long)a & IOMMU_PAGE_OFFSET;
	npages = IOMMU_BTOPR(len + offset);
	if (index + npages >= mp->dmai_ndvmapages) {
		DBG3(D_DVMA_KADDR_LOAD, pci_p,
			"index %x + size %x exceeds reserved pages (%x)\n",
			index, npages, mp->dmai_ndvmapages);
		return;
	}
	dvma_addr = mp->dmai_mapping + IOMMU_PTOB(index);
	fdvma->pagecnt[index] = npages;

	/*
	 * Construct the dma cookie to be returned.
	 */
	cp->dmac_address = dvma_addr | offset;
	cp->dmac_size = len;
	DBG2(D_DVMA_KADDR_LOAD, pci_p, "dmac_address=%x dmac_size=%x\n",
		cp->dmac_address, cp->dmac_size);

	/*
	 * Now just map each of the specficied pages.
	 */
	for (i = 0; i < npages; i++) {
		pfn = hat_getpfnum(&kas, a);
		if (pfn == (u_int) -1) {
			cmn_err(CE_WARN,
				"%s%d: can't get page frames for vaddr %x",
				ddi_get_name(pci_p->dip),
				ddi_get_instance(pci_p->dip), (u_int)a);
		}
		if (pci_flush_on_alloc)
		    *psycho_p->iommu_flush_reg = (dvma_addr & IOMMU_PAGE_MASK);
		IOMMU_LOAD_TTE(psycho_p->tsb_paddr,
				IOMMU_TSB_INDEX(psycho_p, dvma_addr),
				IOMMU_MAKE_TTE(mp, pfn));
		dvma_addr += IOMMU_PAGE_SIZE;
		a += IOMMU_PAGE_SIZE;
	}
	mp->dmai_flags = DMAI_FLAGS_INUSE;
}

/*ARGSUSED*/
void
fast_dvma_unload(ddi_dma_handle_t h, u_int index, u_int view)
{
	ddi_dma_impl_t *mp = (ddi_dma_impl_t *)h;
	struct fast_dvma *fdvma;
	pci_devstate_t *pci_p;

	if (view == DDI_DMA_SYNC_FORKERNEL) {
		fdvma = (struct fast_dvma *)mp->dmai_nexus_private;
		pci_p = (pci_devstate_t *)fdvma->softsp;
		if (!pci_flush_on_alloc)
			unmap_window(pci_p, mp);
		sbuf_flush(pci_p, mp, IOMMU_PTOB(index),
				fdvma->pagecnt[index] * IOMMU_PAGE_SIZE);
	}
}

/*ARGSUSED*/
void
fast_dvma_sync(ddi_dma_handle_t h, u_int index, u_int view)
{
	ddi_dma_impl_t *mp = (ddi_dma_impl_t *)h;
	struct fast_dvma *fdvma;
	pci_devstate_t *pci_p;

	if (view == DDI_DMA_SYNC_FORKERNEL) {
		fdvma = (struct fast_dvma *)mp->dmai_nexus_private;
		pci_p = (pci_devstate_t *)fdvma->softsp;
		sbuf_flush(pci_p, mp, IOMMU_PTOB(index),
				fdvma->pagecnt[index] * IOMMU_PAGE_SIZE);
	}
}

/*
 * end of sun4u fast dvma support routines
 */


#ifdef DEBUG
extern void prom_printf(char *, ...);

static void
dump_dma_handle(u_int flag, pci_devstate_t *pci_p, ddi_dma_impl_t *hp)
{
	DBG4(flag, pci_p,
		"dma handle: inuse=%x mapping=%x size=%x ndvmapages=%x\n",
		hp->dmai_inuse, hp->dmai_mapping, hp->dmai_size,
		hp->dmai_ndvmapages);
	DBG3(flag|D_CONT, pci_p, "\t\toffset=%x winsize=%x win=%x\n",
		hp->dmai_offset, hp->dmai_winsize, hp->dmai_nwin);
}

static void
pci_debug(u_int flag, pci_devstate_t *pci_p, char *fmt,
	int a1, int a2, int a3, int a4, int a5)
{
	char *s = NULL;
	u_int cont = 0;
	u_int i;

	if (flag & D_CONT) {
		flag &= ~D_CONT;
		cont = 1;
	}
	if (pci_debug_flags & flag) {
		switch (flag) {
		case D_IDENTIFY:
			s = "identify"; break;
		case D_ATTACH:
			s = "attach"; break;
		case D_DETACH:
			s = "detach"; break;
		case D_MAP:
			s = "map"; break;
		case D_CTLOPS:
			s = "ctlops"; break;
		case D_G_ISPEC:
			s = "get_intrspec"; break;
		case D_A_ISPEC:
			s = "add_intrspec"; break;
		case D_R_ISPEC:
			s = "remove_intrspec"; break;
		case D_DMA_MAP:
			s = "dma_map"; break;
		case D_DMA_CTL:
			s = "dma_ctl"; break;
		case D_INTR:
			s = "intr"; break;
		case D_DMA_ALLOCH:
			s = "dma_allochdl"; break;
		case D_DMA_FREEH:
			s = "dma_freehdl"; break;
		case D_DMA_BINDH:
			s = "dma_bindhdl"; break;
		case D_DMA_UNBINDH:
			s = "dma_unbindhdl"; break;
		case D_DMA_FLUSH:
			s = "dma_flush"; break;
		case D_DMA_WIN:
			s = "dma_win"; break;
		case D_RMAP:
			s = "get_dvma_pages"; break;
		case D_MAP_WIN:
			s = "map_window"; break;
		case D_UNMAP_WIN:
			s = "unmap_window"; break;
		case D_SBUF:
			s = "sbuf_flush"; break;
		case D_CHK_TAR:
			s = "check_dma_target"; break;
		case D_INIT_CLD:
			s = "init_child"; break;
		case D_ERR_INTR:
			s = "err_intr"; break;
		case D_BYPASS:
			s = "create_bypass_cookies"; break;
		case D_DVMA_KADDR_LOAD:
			s = "fast_dvma_kaddr_load"; break;
		}


		if (s && cont == 0) {
			i = (u_int)pci_p;
			if (i == 0)
				prom_printf("pci: %s: ", s);
			else if (i & 0xffff0000)
				prom_printf("pci(%x,%c): %s: ",
						pci_p->upa_id,
						pci_p->bus_range.lo == 0
							? 'B' : 'A', s);
			else
				prom_printf("pci(%x): %s: ", i, s);
		}
		prom_printf(fmt, a1, a2, a3, a4, a5);
	}
}
#endif
