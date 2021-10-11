/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef	_SYS_SYSCTRL_H
#define	_SYS_SYSCTRL_H

#pragma ident	"@(#)sysctrl.h	1.21	95/05/31 SMI"

/*
 * Include any headers you depend on.
 */

#ifdef	__cplusplus
extern "C" {
#endif

/* useful debugging stuff */
#define	SYSCTRL_ATTACH_DEBUG	0x1
#define	SYSCTRL_INTERRUPT_DEBUG	0x2
#define	SYSCTRL_REGISTERS_DEBUG	0x4

/*
 * OBP supplies us with 2 register sets for the clock-board node. The code for
 * the syctrl driver relies on these register sets being presented by the
 * PROM in the order specified below. If this changes, the following comments
 * must be revised and the code in sysctrl_init() must be changed to reflect
 * these revisions.
 *
 * They are:
 * 	0	Clock frequency registers
 *	1	misc registers
 */

/*
 * The offsets are defined as offsets in bytes from the base of the OBP
 * register to which the register belongs to.
 */

/* Register set 0 */
#define	SYS_OFF_CLK_FREQ2	0x2	/* offset of clock register 2 */

/* Important bits for Clock Frequency register 2 */
#define	RCONS_UART_EN	0x80	/* Remote console reset enabled */

/* Register set 1 */
#define	SYS_OFF_CTRL	0x0	/* Offset of System Control register */
#define	SYS_OFF_STAT1	0x10	/* Offset of System Status1 register */
#define	SYS_OFF_STAT2	0x20	/* Offset of System Status2 register */
#define	SYS_OFF_PSSTAT	0x30	/* Offset of Power Supply Status */
#define	SYS_OFF_PSPRES	0x40	/* Offset of Power Supply Prescence */
#define	SYS_OFF_TEMP	0x50	/* Offset of temperature register */

#define	RMT_CONS_OFFSET	0x4004	/* Offset of Remote Console UART */
#define	RMT_CONS_LEN	0x8	/* Size of Remote Console UART */

/* Bit field defines for System Control register */
#define	SYS_PPS_FAN_FAIL_EN	0x80	/* PPS Fan Fail Interrupt Enable */
#define	SYS_PS_FAIL_EN		0x40	/* PS DC Fail Interrupt Enable */
#define	SYS_AC_PWR_FAIL_EN	0x20	/* AC Power Fail Interrupt Enable */
#define	SYS_SBRD_PRES_EN	0x10	/* Board Insertion Interrupt En */
#define	SYS_PPS_PWR_OFF		0x08	/* Bit to turn off PPS */
#define	SYS_LED_LEFT		0x04	/* System Left LED. Reverse Logic */
#define	SYS_LED_MID		0x02	/* System Middle LED */
#define	SYS_LED_RIGHT		0x01	/* System Right LED */

/* Bit field defines for System Status1 register */
#define	SYS_SLOT6		0x80	/* Set for 6 slot chassis */
#define	SYS_NOT_TEST_BED	0x40	/* ==0 test bed jumper set */
#define	SYS_NOT_SECURE		0x20	/* ==0 Keyswitch in secure pos. */
#define	SYS_PERIPH_PRES		0x10	/* ==1 Peripheral tray present */
#define	SYS_NOT_BRD_PRES	0x08	/* ==0 When board inserted */
#define	SYS_NOT_PPS_PRES	0x04	/* ==0 If PPS present */
#define	SYS_TOD_NOT_RST		0x02	/* ==0 if TOD reset occurred */

/* Bit field defines for System Status2 register */
#define	SYS_RMTE_NOT_RST	0x80	/* Remote Console reset occurred */
#define	SYS_PPS_DC_OK		0x40	/* ==1 PPS DC OK */
#define	SYS_CLK_DCREG0_OK	0x20	/* DC-DC convertor OK on PS 1 */
#define	SYS_CLK_DCREG1_OK	0x10	/* DC-DC convertor OK on PPS */
#define	SYS_AC_FAIL		0x08	/* System lost AC Power source */
#define	SYS_RACK_FANFAIL	0x04	/* Peripheral Rack fan status */
#define	SYS_AC_FAN_OK		0x02	/* Status of 4 AC box fans */
#define	SYS_KEYSW_FAN_OK	0x01	/* Status of keyswitch fan */

#define	OFF_NVRAM_CHECKSUM	0x0	/* OBP dependency */

#ifndef _ASM

#define	SYSCTRL_KSTAT_NAME	"sysctrl"
#define	CSR_KSTAT_NAMED		"csr"
#define	STAT1_KSTAT_NAMED	"status1"
#define	STAT2_KSTAT_NAMED	"status2"
#define	PSSHAD_KSTAT_NAMED	"ps_shadow"
#define	CLK_FREQ2_KSTAT_NAMED	"clk_freq2"
#define	FAN_KSTAT_NAMED		"fan_status"

/* States of a power supply DC voltage. */
enum e_state { PS_BOOT = 0, PS_OUT, PS_UNKNOWN, PS_OK, PS_FAIL };
enum e_pres_state { PRES_UNKNOWN = 0, PRES_IN, PRES_OUT };

/*
 * 11 power supplies are managed -- 8 core power supplies,
 * the pps and the dc0, dc1 on the clock board.
 */
#define	PS_COUNT 11
/* core PS 0 thru 7 are index 0 thru 7 */
#define	PPS_INDEX 8
#define	DC0_INDEX 9
#define	DC1_INDEX 10

/* fan timeout structures */
enum pps_fan_type { RACK = 0, AC = 1, KEYSW = 2 };
#define	PPS_FAN_COUNT	3

#if defined(_KERNEL)

#define	SPUR_TIMEOUT_SEC	1
#define	SPUR_LONG_TIMEOUT_SEC	5
#define	AC_TIMEOUT_SEC		1
#define	PS_FAIL_TIMEOUT_SEC	0.5
#define	PPS_FAN_TIMEOUT_SEC	1
#define	PPS_FAN_OK_TIMEOUT_SEC	5
#define	BRD_INSERT_TIMEOUT_SEC	0.1 /* XXX */
#define	BLINK_LED_TIMEOUT_SEC	0.3

/*
 * how many ticks to wait to register the state change
 * NOTE: ticks are measured in PS_FAIL_TIMEOUT_SEC clicks
 */
#define	PS_PRES_CHANGE_TICKS	1
#define	PS_FROM_BOOT_TICKS	1
#define	PS_FROM_UNKNOWN_TICKS	10
#define	PS_FROM_OK_TICKS	1
#define	PS_FROM_FAIL_TICKS	4

/*
 * how many spurious interrupts to take during a SPUR_LONG_TIMEOUT_SEC
 * before complaining
 */
#define	MAX_SPUR_COUNT		2

/* Private struct to hand around when handling pps fan timeouts */
struct fan_ok_arg {
	struct sysctrl_soft_state *self;	/* our instance */
	enum pps_fan_type type;			/* which fan we are */
	int timeout_id;				/* our timeout id */
};

/*
 * Global driver structure which defines the presence and status of
 * all board power supplies.
 */
struct ps_state {
	int pctr;			/* tick counter for presense deglitch */
	int dcctr;			/* tick counter for dc ok deglitch */
	enum e_pres_state pshadow;	/* presense shadow state */
	enum e_state dcshadow;		/* dc ok shadow state */
};

/* Structures used in the driver to manage the hardware */
struct sysctrl_soft_state {
	dev_info_t *dip;		/* dev info of myself */
	dev_info_t *pdip;		/* dev info of parent */
	struct sysctrl_soft_state *next;
	int board;			/* Board number for this FHC */
	int mondo;			/* INO for this type of interrupt */

	ddi_iblock_cookie_t iblock;	/* High level interrupt cookie */
	ddi_idevice_cookie_t idevice;	/* TODO - Do we need this? */
	ddi_softintr_t spur_id;		/* when we get a spurious int... */
	ddi_iblock_cookie_t spur_int_c;	/* spur int cookie */
	ddi_softintr_t spur_high_id;	/* when we reenable disabled ints */
	ddi_softintr_t spur_long_to_id;	/* long timeout softint */
	ddi_softintr_t ac_fail_id;	/* ac fail softintr id */
	ddi_softintr_t ac_fail_high_id;	/* ac fail re-enable softintr id */
	ddi_softintr_t ps_fail_int_id;	/* ps fail from intr softintr id */
	ddi_iblock_cookie_t ps_fail_c;	/* ps fail softintr cookie */
	ddi_softintr_t ps_fail_poll_id;	/* ps fail from polling softintr */
	ddi_softintr_t pps_fan_id;	/* pps fan fail softintr id */
	ddi_iblock_cookie_t pps_fan_c;	/* pps fan fail softintr cookie */
	ddi_softintr_t pps_fan_high_id;	/* pps fan re-enable softintr id */
	ddi_softintr_t sbrd_pres_id;	/* sbrd softintr id */
	ddi_softintr_t sbrd_gone_id;	/* sbrd removed softintr id */
	ddi_iblock_cookie_t sbrd_pres_c; /* sbrd softintr cookie */
	ddi_softintr_t blink_led_id;	/* led blinker softint */

	volatile u_char *clk_freq1;	/* Clock frequency reg. 1 */
	volatile u_char *clk_freq2;	/* Clock frequency reg. 2 */
	volatile u_char *status1;	/* System Status1 register */
	volatile u_char *status2;	/* System Status2 register */
	volatile u_char *ps_stat;	/* Power Supply Status register */
	volatile u_char *ps_pres;	/* Power Supply Prescence register */
	volatile u_char *temp_reg;	/* VA of temperature register */
	volatile u_char *rcons_ctl;	/* VA of Remote console UART */
	volatile u_char *nvram_base;	/* mapped address of TOD/NVRAM */
	u_int nvram_offset_powerfail;	/* offset of powerfail array */

	/* This mutex protects the following data */
	/* NOTE: *csr should only be accessed from interrupt level */
	kmutex_t csr_mutex;		/* locking for csr enable bits */
	volatile u_char *csr;		/* System Control Register */
	u_char pps_fan_saved;		/* cached pps fanfail state */
	u_char saved_en_state;		/* spurious int cache */
	int spur_count;			/* count multiple spurious ints */

	/* This mutex protects the following data */
	kmutex_t spur_int_lock;		/* lock spurious interrupt data */
	int spur_timeout_id;		/* quiet the int timeout id */
	int spur_long_timeout_id;	/* spurious long timeout interval */

	/* This mutex protects the following data */
	kmutex_t pps_fan_lock;		/* low level lock */
	int pps_fan_external_state;	/* external state of the pps fans */
	struct fan_ok_arg fan_arg[PPS_FAN_COUNT];

	/* This mutex protects the following data */
	kmutex_t ps_fail_lock;		/* low level lock */
	struct ps_state ps_stats[PS_COUNT]; /* state struct for all ps */

	/* This mutex protects the following data */
	kmutex_t sbrd_pres_lock;	/* low level lock */
	/* XXX TODO */

	struct temp_stats tempstat;    /* in memory storage of temperature */
};

/*
 * Kstat structures used to contain data which is requested by user
 * programs.
 */
struct sysctrl_kstat {
	struct kstat_named	csr;		/* system control register */
	struct kstat_named	status1;	/* system status 1 */
	struct kstat_named	status2;	/* system status 2 */
	struct kstat_named	ps_shadow;	/* Power Supply status */
	struct kstat_named	clk_freq2;	/* Clock register 2 */
	struct kstat_named	fan_status;	/* shadow status 2 for fans */
};

#endif /* _KERNEL */
#endif _ASM

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SYSCTRL_H */
