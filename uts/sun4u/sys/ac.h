/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef	_SYS_AC_H
#define	_SYS_AC_H

#pragma ident	"@(#)ac.h	1.5	95/03/29 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/* useful debugging stuff */
#define	AC_ATTACH_DEBUG		0x1
#define	AC_REGISTERS_DEBUG	0x2

/*
 * OBP supplies us with two register sets for the AC nodes. They are:
 *
 *	0		miscellaneous regs
 *	1		Cache tags
 *
 * We do not use the cache tags for anything in the kernel, so we
 * do not map them in.
 */

/* Register set 0 Offsets */
#define	AC_OFF_MEMCTL		0x60
#define	AC_OFF_MEMDEC0		0x70
#define	AC_OFF_MEMDEC1		0x80

/* Use predefined strings to name the kstats from this driver. */
#define	AC_KSTAT_NAME		"address_controller"
#define	MEMCTL_KSTAT_NAMED	"acmemctl"
#define	MEMDECODE0_KSTAT_NAMED	"acmemdecode0"
#define	MEMDECODE1_KSTAT_NAMED	"acmemdecode1"

/* defines for Memory decode registers */
#define	AC_MEM_VALID		0x8000000000000000

/* defines for Memory control register */

/* size of a memory SIMM group in MB */
#define	RASIZE0(memctl)		(8 << ((((memctl) >> 8) & 0x7) << 1))
#define	RASIZE1(memctl)		(8 << ((((memctl) >> 11) & 0x7) << 1))

/*
 * Interleave factor of a memory SIMM group.
 * Possible values are 1, 2, 4, 8, and 16. 1 means not interleaved.
 * Larger groups can be interleaved with smaller groups. Groups
 * on the same board can be interleaved as well.
 */
#define	INTLV0(memctl)		(1 << ((memctl) & 0x7))
#define	INTLV1(memctl)		(1 << (((memctl) >> 3) & 0x7))

/*
 * Physical base mask of a memory SIMM group. Note that this is
 * not the real physical base, and is just used to match up the
 * interleaving of groups. The mask bits (UK) are used to mask
 * out the match (UM) field so that the bases can be compared.
 */
#define	GRP_UK(memdec)	(((memdec) >> 39) & 0xFFF)
#define	GRP_UM(memdec)	(((memdec) >> 12) & 0x7FFF)
#define	GRP_BASE(memdec) ((GRP_UM(memdec)) & ~(GRP_UK(memdec)))

#if defined(_KERNEL)

/* Structures used in the driver to manage the hardware */
struct ac_soft_state {
	dev_info_t *dip;	/* dev info of myself */
	dev_info_t *pdip;	/* dev info of my parent */
	int board;		/* Board number for this AC */
	/* Mapped addresses of registers */
	void *ac_base;		/* Base address of Address Controller */
	volatile u_longlong_t *ac_memctl;	/* Memory Control */
	volatile u_longlong_t *ac_memdecode0;	/* Memory Decode 0 */
	volatile u_longlong_t *ac_memdecode1;	/* Memory Decode 1 */
};

/* kstat structure used by ac to pass data to user programs. */
struct ac_kstat {
	struct kstat_named ac_memctl;		/* AC Memory control */
	struct kstat_named ac_memdecode0;	/* AC Memory Decode Bank 0 */
	struct kstat_named ac_memdecode1;	/* AC Memory Decode Bank 1 */
};

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_AC_H */
