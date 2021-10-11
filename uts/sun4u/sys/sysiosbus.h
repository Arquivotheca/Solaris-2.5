/*
 * Copyright (c) 1991-1995 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef _SYS_SYSIOSBUS_H
#define	_SYS_SYSIOSBUS_H

#pragma ident	"@(#)sysiosbus.h	1.33	95/10/03 SMI"

#ifndef _ASM
#include <sys/avintr.h>
#endif

#ifdef	__cplusplus
extern "C" {
#endif

/* Type defs */
typedef	u_longlong_t	u_ll_t;

/* Things for debugging */
#ifdef SYSIO_MEM_DEBUG
#define	IO_MEMUSAGE
#endif /* SYSIO_MEM_DEBUG */

/*
 * sysio sbus constant definitions.
 */
#define	NATURAL_REG_SIZE	0x8	/* 8 Bytes is Fusion reg size */
#define	MIN_REG_SIZE		0x4	/* Smallest Fusion reg size */
#define	OFF_SYSIO_CTRL_REG	0x10
#define	SYSIO_CTRL_REG_SIZE	(NATURAL_REG_SIZE)
#define	OFF_SBUS_CTRL_REG	0x2000
#define	SBUS_CTRL_REG_SIZE	(NATURAL_REG_SIZE)
#define	OFF_SBUS_SLOT_CONFIG	0x2020
#define	SBUS_SLOT_CONFIG_SIZE	(NATURAL_REG_SIZE * 7)
#define	OFF_INTR_MAPPING_REG	0x2c00
/* #define	INTR_MAPPING_REG_SIZE	(NATURAL_REG_SIZE * 16 * 8)  */
#define	INTR_MAPPING_REG_SIZE	0x490
#define	OFF_CLR_INTR_REG	0x3408
/* #define	CLR_INTR_REG_SIZE	(NATURAL_REG_SIZE * 16 * 8) */
#define	CLR_INTR_REG_SIZE	0x488
#define	OFF_INTR_RETRY_REG	0x2c20
#define	INTR_RETRY_REG_SIZE	(MIN_REG_SIZE)
#define	OFF_SBUS_INTR_STATE_REG	0x4800
#define	SBUS_INTR_STATE_REG_SIZE (NATURAL_REG_SIZE * 2)
#define	SYSIO_IGN		46
#define	SBUS_ARBIT_ALL		0x3full
#define	SYSIO_VER_SHIFT		56

/* Error registers */
#define	OFF_SYSIO_ECC_REGS	0x20
#define	SYSIO_ECC_REGS_SIZE	NATURAL_REG_SIZE
#define	OFF_SYSIO_UE_REGS	0x30
#define	SYSIO_UE_REGS_SIZE	(NATURAL_REG_SIZE * 2)
#define	OFF_SYSIO_CE_REGS	0x40
#define	SYSIO_CE_REGS_SIZE	(NATURAL_REG_SIZE * 2)
#define	OFF_SBUS_ERR_REGS	0x2010
#define	SBUS_ERR_REGS_SIZE	(NATURAL_REG_SIZE * 2)

/* Interrupts */
#define	INTERRUPT_CPU_FIELD	26	/* Bit shift for mondo TID field */
#define	INTERRUPT_GROUP_NUMBER	6	/* Bit shift for mondo IGN field */
#define	INTERRUPT_VALID		0x80000000ull /* Mondo valid bit */
#define	SBUS_INTR_IDLE		0ull
#define	INT_PENDING 		3	/* state of the interrupt dispatch */
/*
 * Fix these (RAZ)
 * Interrupt Mapping Register defines
 */
#define	IMR_VALID		0x80000000ull	/* Valid bit */
#define	IMR_TID			0x7C000000ull	/* TID bits */
#define	IMR_IGN			0x000007C0ull	/* IGN bits */
#define	IMR_INO			0x0000003Full	/* INO bits */
#define	IMR_TID_SHIFT		26		/* Bit shift for TID field */
#define	IMR_IGN_SHIFT		6		/* Bit shift for IGN field */

#define	MAX_SBUS		(30)
#define	MAX_SBUS_LEVEL		(7)
#define	MAX_SBUS_SLOTS	(7)		/* 4 external slots + 3 internal */
#define	EXT_SBUS_SLOTS		4	/* Number of external sbus slots */
#define	MAX_SBUS_SLOT_ADDR	0x10	/* Max slot address on SYSIO */
#define	SYSIO_BURST_RANGE	(0x7f)	/* 32 bit: 64 Byte to 1 Byte burst */
#define	SYSIO64_BURST_RANGE	(0x78)	/* 64 bit: 64 Byte to 8 Byte burst */
#define	SYSIO_BURST_MASK	0xffff
#define	SYSIO64_BURST_MASK	0xffff0000
#define	SYSIO64_BURST_SHIFT	16
#define	MAX_PIL			16

#define	SBUSMAP_BASE		(IOMMU_DVMA_DVFN)
/* SBUS map size: the whole 64M for now */
#define	SBUSMAP_SIZE		(iommu_btop(IOMMU_DVMA_RANGE))
#define	SBUSMAP_FRAG		(SBUSMAP_SIZE >> 3)

/* Slot config register defines */
#define	SBUS_ETM		0x4000ull
#define	SYSIO_SLAVEBURST_MASK	0x1e	/* Mask for hardware register */
#define	SYSIO_SLAVEBURST_RANGE	(0x78)	/* 32 bit: 64 Byte to 8 Byte burst */
#define	SYSIO64_SLAVEBURST_RANGE (0x78)	/* 64 bit: 64 Byte to 8 Byte burst */
#define	SYSIO_SLAVEBURST_REGSHIFT 2	/* Convert bit positions 2**8 to 2**1 */

/*
 * Offsets of sysio, sbus, registers
 */
/* Slot configuration register mapping offsets */
#define	SBUS_SLOT0_CONFIG	0x0
#define	SBUS_SLOT1_CONFIG	0x1
#define	SBUS_SLOT2_CONFIG	0x2
#define	SBUS_SLOT3_CONFIG	0x3
#define	SBUS_SLOT4_CONFIG	0x4
#define	SBUS_SLOT5_CONFIG	0x5
#define	SBUS_SLOT6_CONFIG	0x6

/* Interrupt mapping register mapping offsets */
#define	SBUS_SLOT0_MAPREG	0x0
#define	SBUS_SLOT1_MAPREG	0x1
#define	SBUS_SLOT2_MAPREG	0x2
#define	SBUS_SLOT3_MAPREG	0x3
#define	ESP_MAPREG		0x80
#define	ETHER_MAPREG		0x81
#define	PP_MAPREG		0x82
#define	AUDIO_MAPREG		0x83
#define	KBDMOUSE_MAPREG		0x85
#define	FLOPPY_MAPREG		0x86
#define	THERMAL_MAPREG		0x87
#define	UE_ECC_MAPREG		0x8E
#define	CE_ECC_MAPREG		0x8F
#define	SBUS_ERR_MAPREG		0x90
#define	PM_WAKEUP_MAPREG	0x91
#define	FFB_MAPPING_REG		0x92

/* Interrupt clear register mapping offsets */
#define	SBUS_SLOT0_L1_CLEAR	0x0
#define	SBUS_SLOT0_L2_CLEAR	0x1
#define	SBUS_SLOT0_L3_CLEAR	0x2
#define	SBUS_SLOT0_L4_CLEAR	0x3
#define	SBUS_SLOT0_L5_CLEAR	0x4
#define	SBUS_SLOT0_L6_CLEAR	0x5
#define	SBUS_SLOT0_L7_CLEAR	0x6
#define	SBUS_SLOT1_L1_CLEAR	0x8
#define	SBUS_SLOT1_L2_CLEAR	0x9
#define	SBUS_SLOT1_L3_CLEAR	0xa
#define	SBUS_SLOT1_L4_CLEAR	0xb
#define	SBUS_SLOT1_L5_CLEAR	0xc
#define	SBUS_SLOT1_L6_CLEAR	0xd
#define	SBUS_SLOT1_L7_CLEAR	0xe
#define	SBUS_SLOT2_L1_CLEAR	0x10
#define	SBUS_SLOT2_L2_CLEAR	0x11
#define	SBUS_SLOT2_L3_CLEAR	0x12
#define	SBUS_SLOT2_L4_CLEAR	0x13
#define	SBUS_SLOT2_L5_CLEAR	0x14
#define	SBUS_SLOT2_L6_CLEAR	0x15
#define	SBUS_SLOT2_L7_CLEAR	0x16
#define	SBUS_SLOT3_L1_CLEAR	0x18
#define	SBUS_SLOT3_L2_CLEAR	0x19
#define	SBUS_SLOT3_L3_CLEAR	0x1a
#define	SBUS_SLOT3_L4_CLEAR	0x1b
#define	SBUS_SLOT3_L5_CLEAR	0x1c
#define	SBUS_SLOT3_L6_CLEAR	0x1d
#define	SBUS_SLOT3_L7_CLEAR	0x1e
#define	ESP_CLEAR		0x7f
#define	ETHER_CLEAR		0x80
#define	PP_CLEAR		0x81
#define	AUDIO_CLEAR		0x82
#define	KBDMOUSE_CLEAR		0x84
#define	FLOPPY_CLEAR		0x85
#define	THERMAL_CLEAR		0x86
#define	UE_ECC_CLEAR		0x8D
#define	CE_ECC_CLEAR		0x8E
#define	SBUS_ERR_CLEAR		0x8F
#define	PM_WAKEUP_CLEAR		0x90

/*
 * Bit shift for accessing the keyboard mouse interrupt state reg.
 * note - The external devices are the only other devices where
 * we need to check the interrupt state before adding or removing
 * interrupts.  There is an algorithm to calculate their bit shift.
 */
#define	KBDMOUSE_INTR_STATE_SHIFT	10

#define	MAX_INO_TABLE_SIZE	48	/* Max num of sbus devices on sysio */
#define	MAX_MONDO_EXTERNAL	0x1f
#define	THERMAL_MONDO		0x2a
#define	UE_ECC_MONDO		0x34
#define	CE_ECC_MONDO		0x35
#define	SBUS_ERR_MONDO		0x36

/*
 * sysio sbus soft state data structure.
 * We use the sbus_ctrl_reg to flush hardware store buffers because
 * there is very little hardware contention on this register.
 */
volatile struct sbus_soft_state {
	dev_info_t *dip;		/* dev info of myself */
	int upa_id;			/* UPA ID of this SYSIO */

	u_ll_t *iommu_flush_reg;	/* IOMMU regs */
	u_ll_t *iommu_ctrl_reg;
	u_ll_t *tsb_base_addr;		/* Hardware reg for phys TSB base */
	u_ll_t *soft_tsb_base_addr;	/* phys address of TSB base */

	u_ll_t *sysio_ctrl_reg;		/* sysio regs */
	u_ll_t *sbus_ctrl_reg;		/* Reg also used to flush store bufs */
	u_ll_t *sbus_slot_config_reg;
	u_long sbus_slave_burstsizes[MAX_SBUS_SLOTS];

	u_ll_t *intr_mapping_reg;	/* Interrupt regs */
	u_ll_t *clr_intr_reg;
	u_ll_t *intr_retry_reg;
	u_ll_t *sbus_intr_state;
	u_ll_t *obio_intr_state;
	u_char intr_hndlr_cnt[MAX_SBUS_SLOT_ADDR];
	u_char spurious_cntrs[MAX_PIL + 1]; /* Spurious intr counter */

	u_ll_t *sysio_ecc_reg;		/* sysio ecc control reg */
	u_ll_t *sysio_ue_reg;		/* sysio ue ecc error regs */
	u_ll_t *sysio_ce_reg;		/* sysio ce ecc error regs */
	u_ll_t *sbus_err_reg;		/* sbus async error regs */

	u_ll_t *str_buf_ctrl_reg;	/* streaming buffer regs */
	u_ll_t *str_buf_flush_reg;
	u_ll_t *str_buf_sync_reg;
	kmutex_t sync_reg_lock;
	int stream_buf_off;

	u_int sbus_burst_sizes;
	u_int sbus64_burst_sizes;

	struct map *dvmamap;		/* DVMA map for this IOMMU */
	int dvma_call_list_id;		/* DVMA callback list */
	kmutex_t dma_pool_lock;
	caddr_t dmaimplbase;		/* dma_pool_lock protects this */
	int	dma_reserve;		/* Size reserved for fast DVMA */

	struct vec_poll_list *poll_array[MAX_INO_TABLE_SIZE];
					/* non-precise vector poll list */
	kmutex_t intr_poll_list_lock;	/* to add/rem to intr poll list */
#ifdef	DEBUG
	kmutex_t iomemlock;		/* Memory usage lock (debug only) */
	struct io_mem_list *iomem;	/* Memory usage list (debug only) */
#endif /* DEBUG */
};


/*
 * Ugly interrupt cruft due to sysio inconsistencies.
 */
struct sbus_slot_entry {
	u_ll_t slot_config;
	u_ll_t mapping_reg;
	u_ll_t clear_reg;
	int diagreg_shift;
};


/* sbus Interrupt routine wrapper structure */
struct sbus_wrapper_arg {
	struct sbus_soft_state *softsp;
	volatile u_ll_t *clear_reg;
	u_int pil;
	u_int (*funcp)();
	caddr_t arg;
};


/* sysio interrupt specification structure. */
struct sysiointrspec {
	u_int mondo;
	u_int pil;
	u_int bus_level;
	struct sbus_wrapper_arg *sbus_handler_arg;
};


/* Data element structure used when we need an interrupt poll list. */
static struct vec_poll_list {
	struct sbus_wrapper_arg *poll_arg;
	struct vec_poll_list *poll_next;
};

/*
 * SYSIO parent private data structure contains register, interrupt, property
 * and range information.
 * Note: the only thing different from the "generic" sbus parent private
 * data is the interrupt specification.
 */
struct sysio_parent_private_data {
	int par_nreg;			/* number of regs */
	struct regspec *par_reg;	/* array of regs */
	int par_nintr;			/* number of interrupts */
	struct sysiointrspec *par_intr;	/* array of possible interrupts */
	int par_nrng;			/* number of ranges */
	struct rangespec *par_rng;	/* array of ranges */
	u_int slot;			/* Slot number, on this sbus */
	u_int offset;			/* Offset of first real "reg" */
};
#define	SYSIO_PD(d)	\
	((struct sysio_parent_private_data *)DEVI((d))->devi_parent_data)

#define	sysio_pd_getnreg(dev)		(SYSIO_PD(dev)->par_nreg)
#define	sysio_pd_getnintr(dev)		(SYSIO_PD(dev)->par_nintr)
#define	sysio_pd_getnrng(dev)		(SYSIO_PD(dev)->par_nrng)
#define	sysio_pd_getslot(dev)		(SYSIO_PD(dev)->slot)
#define	sysio_pd_getoffset(dev)		(SYSIO_PD(dev)->offset)

#define	sysio_pd_getreg(dev, n)		(&SYSIO_PD(dev)->par_reg[(n)])
#define	sysio_pd_getintr(dev, n)	(&SYSIO_PD(dev)->par_intr[(n)])
#define	sysio_pd_getrng(dev, n)		(&SYSIO_PD(dev)->par_rng[(n)])

struct io_mem_list	{
	dev_info_t *rdip;
	u_long	ioaddr;
	u_long	addr;
	u_int	npages;
	u_int	*pfn;
	struct io_mem_list *next;
};

/*
 * Function prototypes.
 */


#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SYSIOSBUS_H */
