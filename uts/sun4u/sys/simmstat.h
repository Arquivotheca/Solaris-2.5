/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef	_SYS_SIMMSTAT_H
#define	_SYS_SIMMSTAT_H

#pragma ident	"@(#)simmstat.h	1.3	95/03/29 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/* useful debugging stuff */
#define	SIMMSTAT_ATTACH_DEBUG		0x1
#define	SIMMSTAT_REGISTERS_DEBUG	0x2

/*
 * OBP supplies us with 1 register set for the simm-staus node, so
 * we do not need multiple register set number defines and
 * register offsets.
 */

/* Use predefined strings to name the kstats from this driver. */
#define	SIMMSTAT_KSTAT_NAME	"simm-status"

/* Number of SIMM slots in Sunfire System Board */
#define	SIMM_COUNT		16

#if defined(_KERNEL)

struct simmstat_soft_state {
	dev_info_t *dip;	/* dev info of myself */
	dev_info_t *pdip;	/* dev info of my parent */
	int board;		/* Board number for this FHC */
	/* Mapped addresses of registers */
	volatile u_char *simmstat_base;	/* base of simmstatus registers */
};

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SIMMSTAT_H */
