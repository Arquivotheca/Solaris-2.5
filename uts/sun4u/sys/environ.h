/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef	_SYS_ENVIRON_H
#define	_SYS_ENVIRON_H

#pragma ident	"@(#)environ.h	1.14	95/06/08 SMI"

/*
 * Include any headers you depend on.
 */

#ifdef	__cplusplus
extern "C" {
#endif

/* useful debugging stuff */
#define	ENVIRON_ATTACH_DEBUG	0x1
#define	ENVIRON_INTERRUPT_DEBUG	0x2
#define	ENVIRON_REGISTERS_DEBUG	0x4

/*
 * OBP supplies us with 1 register set for the environment node
 *
 * It is:
 * 	0	Temperature register
 */

#define	ENVIRON_KSTAT_NAME	"environ"
#define	FANSTATUS_KSTAT_NAMED	"fanstatus"

/* Define the states of a fan */
enum env_fan_state { FAN_OK = 0, FAN_FAIL };

/*
 * Convert A Board Number to a Power Supply number.
 *
 * The mapping is like this:
 *
 * Board	PS
 *  0, 2	0
 *  1, 3	1
 *  4, 6	2
 *  5, 7	3
 *  8, 10	4
 *  9, 11	5
 *  12, 14	6
 *  13, 15	7
 */
#define	BD_2_PS(b)  ((((b) >> 1) & (~1)) | ((b) & 1))

/*
 * What is the board number for the other board associated with our
 * power supply?
 */
#define	OTHER_BD_FOR_PS(b) ((b) ^ 2)

#if defined(_KERNEL)

/* Default times for calling polling routine. */
#define	FANFAIL_TIMEOUT_SEC	3
#define	FANFAIL_OK_TIMEOUT_SEC	10

/* Structures used in the driver to manage the hardware */
struct environ_soft_state {
	dev_info_t *dip;		/* dev info of myself */
	dev_info_t *pdip;		/* dev info of parent */
	struct environ_soft_state *next;
	ddi_iblock_cookie_t fan_high_c;	/* cookie for fan interrupt */
	ddi_iblock_cookie_t fan_fail_c;	/* soft interrupt cookie */
	ddi_softintr_t fan_fail_id;	/* fan fail softint handler */
	ddi_softintr_t fan_retry_id;	/* fan re-enable softint handler */
	int board;			/* Board number for this FHC */
	volatile u_char *temp_reg;	/* VA of temperature register */
	struct temp_stats tempstat;	/* in memory storage of temperature */

	/* this mutex protects the following data */
	kmutex_t fan_lock;		/* low level mutex for fan stuff */
	enum env_fan_state fan_state;	/* current state of this boards fan */
	int ok_id;			/* timeout ID for the fan ok msg */

	/* this mutex protects access to the fhc fan control func */
	kmutex_t fhc_func_lock;		/* high level mutex for fhc func */
};

/*
 * Kstat structures used to contain data which is requested by user
 * programs.
 */
struct environkstat {
	struct kstat_named	fanstat;	/* Current Fan State */
};

#endif /* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ENVIRON_H */
