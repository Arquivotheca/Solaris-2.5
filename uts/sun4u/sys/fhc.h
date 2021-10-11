/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef	_SYS_FHC_H
#define	_SYS_FHC_H

#pragma ident	"@(#)fhc.h	1.16	95/09/21 SMI"

#ifdef	__cplusplus
extern "C" {
#endif


/* useful debugging stuff */
#define	FHC_ATTACH_DEBUG	0x1
#define	FHC_INTERRUPT_DEBUG	0x2
#define	FHC_REGISTERS_DEBUG	0x4
#define	FHC_CTLOPS_DEBUG	0x8

/*
 * OBP supplies us with 6 register sets for the FHC. The code for the fhc
 * driver relies on these register sets being presented by the PROM in the
 * order specified below. If this changes, the following comments must be
 * revised and the code in fhc_init() must be changed to reflect these
 * revisions.
 *
 * They are:
 * 	0	FHC internal registers
 * 	1	IGR Interrupt Group Number
 *	2	FanFail IMR, ISMR
 *	3	System IMR, ISMR
 *	4	UART IMR, ISMR
 *	5	TOD IMR, ISMR
 */

/*
 * The offsets are defined as offsets from the base of the OBP register
 * set which the register belongs to.
 */

/* Register set 0 */
#define	FHC_OFF_ID		0x0	/* FHC ID register */
#define	FHC_OFF_RCTRL		0x10	/* FHC Reset Control and Status */
#define	FHC_OFF_CTRL		0x20	/* FHC Control and Status */
#define	FHC_OFF_BSR		0x30	/* FHC Board Status Register */
#define	FHC_OFF_JTAG_CTRL	0xF0	/* JTAG Control Register */
#define	FHC_OFF_JTAG_CMD	0x100	/* JTAG Comamnd Register */

/* Register sets 2-5, the ISMR offset is the same */
#define	FHC_OFF_ISMR		0x10	/* FHC Interrupt State Machine */

/* Bit field defines for FHC Control and Status Register */
#define	FHC_CENTERDIS		0x00100000
#define	FHC_MOD_OFF		0x00008000
#define	FHC_ACDC_OFF		0x00004000
#define	FHC_FHC_OFF		0x00002000
#define	FHC_EPDA_OFF		0x00001000
#define	FHC_EPDB_OFF		0x00000800
#define	FHC_PS_OFF		0x00000400
#define	FHC_NOT_BRD_PRES	0x00000200
#define	FHC_LED_MID		0x00000020
#define	FHC_LED_RIGHT		0x00000010

/* Bit field defines for FHC Reset Control and Status Register */
#define	FHC_POR			0x80000000
#define	FHC_SOFT_POR		0x40000000
#define	FHC_SOFT_XIR		0x20000000


/*
 * The following defines are used by the fhc driver to determine the
 * difference between IO and CPU type boards. This will be replaced
 * later by JTAG scan to determine board type.
 */

/* XXX */
#define	FHC_UPADATA64A		0x40000
#define	FHC_UPADATA64B		0x20000
/* XXX */

/* Bit field defines for Board Status Register */
#define	FHC_DIAG_MODE		0x40

/* Size of temperature recording array */
#define	MAX_TEMP_HISTORY	16

/* Maximum number of boards in system. */
#define	MAX_BOARDS		16

/* Maximum number of Board Power Supplies. */
#define	MAX_PS_COUNT	8

/* Use predefined strings to name the kstats from this driver. */
#define	FHC_KSTAT_NAME		"fhc"
#define	CSR_KSTAT_NAMED		"csr"
#define	BSR_KSTAT_NAMED		"bsr"

#ifndef _ASM

/* Use predefined strings to name the kstats from this driver. */

/* Bit field defines for Interrupt Mapping registers */
#define	IMR_VALID	((u_int)1 << INR_EN_SHIFT) /* Mondo valid bit */

/* Bit defines for Interrupt State Machine Register */
#define	INT_PENDING	3	/* state of the interrupt dispatch */

struct intr_regs {
	volatile u_int *mapping_reg;
	volatile u_int *clear_reg;
};

#define	BD_IVINTR_SHFT		0x7

/*
 * Convert the Board Number field in the FHC Board Status Register to
 * a board number. The field in the register is bits 0,3-1 of the board
 * number. Therefore a macro is necessary to extract the board number.
 */
#define	FHC_BSR_TO_BD(bsr)	((((bsr) >> 16) & 0x1)  | \
				(((bsr) >> 12) & 0xE))

#define	FHC_INO(ino) ((ino) & 0x7)

#define	FHC_MAX_INO	4

#define	FHC_SYS_INO		0x0
#define	FHC_UART_INO		0x1
#define	FHC_TOD_INO		0x2
#define	FHC_FANFAIL_INO		0x3

/*
 * Defines for the kstats created for passing temperature values and
 * history out to user level programs. All temperatures passed out
 * will be in degrees Centigrade, corrected for the board type the
 * temperature was read from. Since each Board type has a different
 * response curve for the A/D convertor, the temperatures are all
 * calibrated inside the kernel.
 */

#define	OVERTEMP_KSTAT_NAME	"temperature"

/*
 * Time averaging based method of recording temperature history.
 * Higher level temperature arrays are composed of temperature averages
 * of the array one level below. When the lower array completes a
 * set of data, the data is averaged and placed into the higher
 * level array. Then the lower level array is overwritten until
 * it is once again complete, where the process repeats.
 *
 * This method gives a user a fine grained view of the last minute,
 * and larger grained views of the temperature as one goes back in
 * time.
 *
 * The time units for the longer samples are based on the value
 * of the OVERTEMP_TIMEOUT_SEC and the number of elements in each
 * of the arrays between level 1 and the higher level.
 */

#define	OVERTEMP_TIMEOUT_SEC	2

/* definition of the clock board index */
#define	CLOCK_BOARD_INDEX	16

#define	L1_SZ		30	/* # of OVERTEMP_TIMEOUT_SEC samples */
#define	L2_SZ		15	/* size of array for level 2 samples */
#define	L3_SZ		12	/* size of array of level 3 samples */
#define	L4_SZ		4	/* size of array of level 4 samples */
#define	L5_SZ		2	/* size of array of level 5 samples */

/*
 * Macros for determining when to do the temperature averaging of arrays.
 */
#define	L2_INDEX(i)	((i) / L1_SZ)
#define	L2_REM(i)	((i) % L1_SZ)
#define	L3_INDEX(i)	((i) / (L1_SZ * L2_SZ))
#define	L3_REM(i)	((i) % (L1_SZ * L2_SZ))
#define	L4_INDEX(i)	((i) / (L1_SZ * L2_SZ * L3_SZ))
#define	L4_REM(i)	((i) % (L1_SZ * L2_SZ * L3_SZ))
#define	L5_INDEX(i)	((i) / (L1_SZ * L2_SZ * L3_SZ * L4_SZ))
#define	L5_REM(i)	((i) % (L1_SZ * L2_SZ * L3_SZ * L4_SZ))

/*
 * define for an illegal temperature. This temperature will never be seen
 * in a real system, so it is used as an illegal value in the various
 * functions processing the temperature data structure.
 */
#define	NA_TEMP		0x7FFF

/*
 * Main structure for passing the calibrated and time averaged temperature
 * values to user processes. This structure is copied out via the kstat
 * mechanism.
 */
struct temp_stats {
	u_int index;	/* index of current temperature */
	short l1[L1_SZ];	/* OVERTEMP_TIMEOUT_SEC samples */
	short l2[L2_SZ];	/* level 2 samples */
	short l3[L3_SZ];	/* level 3 samples */
	short l4[L4_SZ];	/* level 4 samples */
	short l5[L5_SZ];	/* level 5 samples */
	short max;		/* maximum temperature recorded */
	short min;		/* minimum temperature recorded */
};

/*
 * Enumerated types for defining type and state of system and clock
 * boards. These are used by both the kernel and user programs.
 */
enum board_type {	UNINIT_BOARD = 0,	/* Uninitialized board type */
			UNKNOWN_BOARD,		/* Unknown board type */
			CPU_BOARD,		/* System board CPU(s) */
			MEM_BOARD,		/* System board no CPUs */
			IO_2SBUS_BOARD,		/* 2 SBus IO Board */
			IO_SBUS_FFB_BOARD,	/* SBus and FFB IO Board */
			IO_PCI_BOARD,		/* PCI IO Board */
			CLOCK_BOARD };		/* System Clock board */

enum board_state {	UNKNOWN_STATE = 0,	/* Unknown board */
			ACTIVE_STATE,		/* active and working */
			HOTPLUG_STATE,		/* Hot plugged board */
			LOWPOWER_STATE };	/* disabled board */

#if defined(_KERNEL)

/*
 * zs design for fhc has two zs' interrupting on same interrupt mondo
 * This requires us to poll for zs and zs alone. The poll list has been
 * defined as a fixed size for simplicity.
 */
#define	MAX_ZS_CNT	2

/* FHC Interrupt routine wrapper structure */
struct fhc_wrapper_arg {
	struct fhc_soft_state *softsp;
	volatile u_int *clear_reg;
	volatile u_int *mapping_reg;
	dev_info_t *child;
	u_int (*funcp)();
	caddr_t arg;
};

/*
 * Function shared with child drivers which require fhc
 * support. They gain access to this function through the use of the
 * _depends_on variable.
 */
struct bd_list *get_and_lock_bdlist(int board);
struct bd_list *get_bdlist(int board);
void unlock_bdlist();
enum board_type get_board_type(int board);

void update_temp(dev_info_t pdip, struct temp_stats *envstat, u_char value);
int overtemp_kstat_update(kstat_t *ksp, int rw);
int determine_board_type(struct fhc_soft_state *softsp, int slot);
void init_temp_arrays(struct temp_stats *envstat);

/* Structures used in the driver to manage the hardware */
struct fhc_soft_state {
	dev_info_t *dip;		/* dev info of myself */
	struct bd_list *list;		/* pointer to board list entry */
	int is_central;			/* A central space instance of FHC */
	volatile u_int *id;		/* FHC ID register */
	volatile u_int *rctrl;		/* FHC Reset Control and Status */
	volatile u_int *bsr;		/* FHC Board Status register */
	volatile u_int *jtag_ctrl;	/* JTAG Control register */
	volatile u_int *jtag_cmd;	/* JTAG Comamnd register */
	volatile u_int *igr;		/* Interrupt Group Number */
	struct intr_regs intr_regs[FHC_MAX_INO];
	struct fhc_wrapper_arg poll_list[MAX_ZS_CNT];
	kmutex_t poll_list_lock;
	u_char spurious_zs_cntr;	/* Spurious counter for zs devices */

	/* this lock protects the following data */
	/* ! non interrupt use only ! */
	kmutex_t ctrl_lock;		/* lock for access to FHC CSR */
	volatile u_int *ctrl;		/* FHC Control and Status */
};

struct bd_list {
	enum board_type type;		/* Type of board FHC is on */
	struct fhc_soft_state *softsp;	/* handle for DDI soft state */
	struct bd_list *next;		/* next bd_list entry */
	int board;			/* board number */
	int monitoring_fan;		/* board is watching the ps fan */
	enum board_state state;		/* current state of this board */
};

/* FHC interrupt specification */
struct fhcintrspec {
	u_int mondo;
	u_int pil;
	dev_info_t *child;
	struct fhc_wrapper_arg *handler_arg;
};

/* kstat structure used by fhc to pass data to user programs. */
struct fhc_kstat {
	struct kstat_named csr;	/* FHC Control and Status Register */
	struct kstat_named bsr;	/* FHC Board Status Register */
};

/* communication from environ used to enable and disable the fan intr */
enum fhc_fan_mode { FHC_FAN_INTR_OFF, FHC_FAN_INTR_ON };
void	fhc_control_fan_intr(dev_info_t *dip, enum fhc_fan_mode mode);

#endif	/* _KERNEL */

#endif _ASM

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FHC_H */
