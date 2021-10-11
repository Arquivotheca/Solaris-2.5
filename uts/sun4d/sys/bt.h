
/*
 * Copyright (c) 1991, by Sun Microsystems, Inc.
 */

#ifndef _SYS_BT_H
#define	_SYS_BT_H

#pragma ident	"@(#)bt.h	1.3	95/01/25 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * These values are decoded from physical addresses and other information,
 * and represent real and pseudo bus types.
 * They are only used as tokens--they do not relate numerically to any
 * physical reality.
 */

#define	BT_UNKNOWN	1	/* not a defined map area */
#define	BT_DRAM		2	/* system memory as DRAM */
#define	BT_NVRAM	3	/* system memory as NVRAM */
#define	BT_OBIO		4	/* on-board devices */
#define	BT_VIDEO	5	/* onboard video */
#define	BT_SBUS		6	/* S-Bus */
#define	BT_VME		7	/* VME Bus */

#if defined(_KERNEL) && !defined(_ASM)

extern int impl_bustype(u_int);

#endif /* _KERNEL && !_ASM */

#ifdef	__cplusplus
}
#endif

#endif /* !_SYS_BT_H */
