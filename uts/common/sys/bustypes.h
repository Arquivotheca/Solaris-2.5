/*
 * Copyright (c) 1989, 1990 by Sun Microsystems, Inc.
 */

#ifndef _SYS_BUSTYPES_H
#define	_SYS_BUSTYPES_H

#pragma ident	"@(#)bustypes.h	1.3	92/07/14 SMI"	/* SVr4 */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Defines for bus types.  These are magic cookies passed between drivers
 * and their parents to describe their address space.  Configuration mechanisms
 * use this as well.  Root nexus drivers on implementations using
 * "generic-addressing" also use these to describe register properties.
 * Generally, this will be non-self configuring architectures.
 *
 * The cookies presented here are used in two ways:
 *
 * 1.	All machines supporting VME use the VME space cookies.
 * 2.	On machines supporting "generic-addressing" in the root nexus,
 *	the generic cookies described in the bootom of the file are used
 *	to distinguish the spaces described by device regsiters.
 *
 *	Sun machines generally support OBMEM and OBIO spaces.
 *	Certain implementations support VME (Generally, servers).
 */

/*
 * Generic "bustypes" for VME devices....
 * These are compatable with OBP and are formed in the following manner...
 *
 *	Space 7:31	Set to zero
 *	Space 6		DM (0 = 16 bit access, 1 = 32 bit access)
 *	Space 0:5	VME Address Modifier bits AM0:5 (See VME spec)
 */

#define	SP_VME_A32D16_USER_D	0x0009
#define	SP_VME_A32D16_USER_I	0x000A
#define	SP_VME_A32D16_USER_BLK	0x000B

#define	SP_VME_A32D16_SUPV_D	0x000D
#define	SP_VME_A32D16_SUPV_I	0x000E
#define	SP_VME_A32D16_SUPV_BLK	0x000F

#define	SP_VME_A16D16_USER	0x0029

#define	SP_VME_A16D16_SUPV	0x002D

#define	SP_VME_A24D16_USER_D	0x0039
#define	SP_VME_A24D16_USER_I	0x003A
#define	SP_VME_A24D16_USER_BLK	0x003B

#define	SP_VME_A24D16_SUPV_D	0x003D
#define	SP_VME_A24D16_SUPV_I	0x003E
#define	SP_VME_A24D16_SUPV_BLK	0x003F

#define	SP_VME_A32D32_USER_D	0x0049
#define	SP_VME_A32D32_USER_I	0x004A
#define	SP_VME_A32D32_USER_BLK	0x004B

#define	SP_VME_A32D32_SUPV_D	0x004D
#define	SP_VME_A32D32_SUPV_I	0x004E
#define	SP_VME_A32D32_SUPV_BLK	0x004F

#define	SP_VME_A16D32_USER	0x0069

#define	SP_VME_A16D32_SUPV	0x006D

#define	SP_VME_A24D32_USER_D	0x0079
#define	SP_VME_A24D32_USER_I	0x007A
#define	SP_VME_A24D32_USER_BLK	0x007B

#define	SP_VME_A24D32_SUPV_D	0x007D
#define	SP_VME_A24D32_SUPV_I	0x007E
#define	SP_VME_A24D32_SUPV_BLK	0x007F

/*
 * Shorthand notation for kernel registers for device drivers...
 */

#define	SP_VME32D16		SP_VME_A32D16_SUPV_D
#define	SP_VME16D16		SP_VME_A16D16_SUPV
#define	SP_VME24D16		SP_VME_A24D16_SUPV_D
#define	SP_VME32D32		SP_VME_A32D32_SUPV_D
#define	SP_VME16D32		SP_VME_A16D32_SUPV
#define	SP_VME24D32		SP_VME_A24D32_SUPV_D

/*
 * Spaces 0x0000 - 0x007F reserved for VME spaces...
 */

#define	SP_VIRTUAL	0x0100		/* virtual address */
#define	SP_OBMEM	0x0200		/* on board memory */
#define	SP_OBIO		0x0210		/* on board i/o */

/*
 * The following are some Cookie name/value suggestions...
 * and are not necessarily supported at all (nexi for these devices
 * must handle and convert any requests for these spaces.)
 */

#define	SP_IPI		0x0300		/* IPI device bus */
#define	SP_SBUS		0x0400		/* SBus device bus */
#define	SB_XBOX		0x0500		/* XBox device bus */

#define	SP_MBMEM	0x1000		/* MultiBus memory */
#define	SP_MBIO		0x1100		/* MultiBus IO */

#define	SP_ATMEM	0x2000		/* AT Bus Memory */
#define	SP_ATIO		0x2100		/* AT IO */

#define	SP_FBMEM	0x3000		/* FutureBus Memory */
#define	SP_FBIO		0x3100		/* FutureBus IO */

#define	SP_UBMEM	0x4000		/* Arbitrary user bus memory space */
#define	SP_UBIO		0x4100		/* Arbitrary user bus IO space */

#define	SP_INVALID	((unsigned)-1)	/* This value reserved */

/*
 * Anything in the range 0x4000 - 0x4FFF reserved for arbitrary 3rd party use.
 */

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_BUSTYPES_H */
