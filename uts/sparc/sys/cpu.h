/*
 * Copyright (c) 1990, by Sun Microsystems, Inc.
 */

#ifndef _SYS_CPU_H
#define	_SYS_CPU_H

#pragma ident	"@(#)cpu.h	1.16	93/04/22 SMI"

/*
 * This file contains common identification and reference information
 * for all sparc-based kernels.
 *
 * Coincidentally, the arch and mach fields that uniquely identifies
 * a cpu is what is stored in either nvram or idprom for a platform.
 * XXX: This may change!
 */

/*
 * Include generic bustype cookies.
 */
#include <sys/bustypes.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	CPU_ARCH	0xf0		/* mask for architecture bits */
#define	CPU_MACH	0x0f		/* mask for machine implementation */

#define	CPU_ANY		(CPU_ARCH|CPU_MACH)
#define	CPU_NONE	0

/*
 * sun4 architectures
 */

#define	SUN4_ARCH	0x20		/* arch value for Sun 4 */

#define	SUN4_MACH_260	0x01
#define	SUN4_MACH_110	0x02
#define	SUN4_MACH_330	0x03
#define	SUN4_MACH_470	0x04

#define	CPU_SUN4_260	(SUN4_ARCH + SUN4_MACH_260)
#define	CPU_SUN4_110	(SUN4_ARCH + SUN4_MACH_110)
#define	CPU_SUN4_330	(SUN4_ARCH + SUN4_MACH_330)
#define	CPU_SUN4_470	(SUN4_ARCH + SUN4_MACH_470)

#define	CPU_SUN4	(SUN4_ARCH + CPU_MACH)

/*
 * sun4c architectures
 */

#define	SUN4C_ARCH	0x50		/* arch value for Sun 4c */

#define	SUN4C_MACH_60		0x01	/* SPARCstation 1 */
#define	SUN4C_MACH_40		0x02	/* SPARCstation IPC */
#define	SUN4C_MACH_65		0x03	/* SPARCstation 1+ */
#define	SUN4C_MACH_20		0x04	/* SPARCstation SLC */
#define	SUN4C_MACH_75		0x05	/* SPARCstation 2 */
#define	SUN4C_MACH_25		0x06	/* SPARCstation ELC */
#define	SUN4C_MACH_50		0x07	/* SPARCstation IPX */
#define	SUN4C_MACH_70		0x08
#define	SUN4C_MACH_80		0x09
#define	SUN4C_MACH_10		0x0a
#define	SUN4C_MACH_45		0x0b
#define	SUN4C_MACH_05		0x0c
#define	SUN4C_MACH_85		0x0d
#define	SUN4C_MACH_32		0x0e

#define	CPU_SUN4C_60	(SUN4C_ARCH + SUN4C_MACH_60)
#define	CPU_SUN4C_40	(SUN4C_ARCH + SUN4C_MACH_40)
#define	CPU_SUN4C_65	(SUN4C_ARCH + SUN4C_MACH_65)
#define	CPU_SUN4C_20	(SUN4C_ARCH + SUN4C_MACH_20)
#define	CPU_SUN4C_75	(SUN4C_ARCH + SUN4C_MACH_75)
#define	CPU_SUN4C_25	(SUN4C_ARCH + SUN4C_MACH_25)
#define	CPU_SUN4C_50	(SUN4C_ARCH + SUN4C_MACH_50)
#define	CPU_SUN4C_70	(SUN4C_ARCH + SUN4C_MACH_70)
#define	CPU_SUN4C_80	(SUN4C_ARCH + SUN4C_MACH_80)
#define	CPU_SUN4C_10	(SUN4C_ARCH + SUN4C_MACH_10)
#define	CPU_SUN4C_45	(SUN4C_ARCH + SUN4C_MACH_45)
#define	CPU_SUN4C_05	(SUN4C_ARCH + SUN4C_MACH_05)
#define	CPU_SUN4C_85	(SUN4C_ARCH + SUN4C_MACH_85)
#define	CPU_SUN4C_32	(SUN4C_ARCH + SUN4C_MACH_32)

#define	CPU_SUN4C	(SUN4C_ARCH + CPU_MACH)

/*
 * sun4e architectures
 */

#define	SUN4E_ARCH	0x60		/* arch value for Sun 4e */
#define	SUN4E_MACH_120		0x01
#define	CPU_SUN4E	(SUN4E_ARCH + CPU_MACH)
#define	CPU_SUN4E_120	(SUN4E_ARCH + SUN4E_MACH_120)

/*
 * early sun4m architectures
 */

#define	SUN4M_ARCH	0x70		/* arch value for Sun 4m */

#define	SUN4M_MACH_60		0x01
#define	SUN4M_MACH_50		0x02
#define	SUN4M_MACH_40		0x03

#define	CPU_SUN4M_60	(SUN4M_ARCH + SUN4M_MACH_60)
#define	CPU_SUN4M_50	(SUN4M_ARCH + SUN4M_MACH_50)
#define	CPU_SUN4M_40	(SUN4M_ARCH + SUN4M_MACH_40)

#define	CPU_SUN4M	(SUN4M_ARCH + CPU_MACH)

/*
 * The ultimate value of 'cputype'.  If you find the top bit
 * set, then it means - "go look at the properties in the device tree"
 *
 * In this case, nothing else can or should be inferred from the value
 * of 'cputype' - the other bits are free for whatever e.g. manufacturing
 * wants to do with them.
 */
#define	OBP_ARCH	0x80		/* arch value for 4m, 4d and later */

/*
 * Global kernel variables of interest
 */

#if defined(_KERNEL) && !defined(_ASM)
extern short cputype;			/* machine type we are running on */
extern int dvmasize;			/* usable dvma size in clicks */

/*
 * Cache defines - right now these are only used on sun4m machines
 *
 * Each bit represents an attribute of the system's caches that
 * the OS must handle.  For example, VAC caches must have virtual
 * alias detection, VTAG caches must be flushed on every demap, etc.
 */
#define	CACHE_NONE		0	/* No caches of any type */
#define	CACHE_VAC		0x01	/* Virtual addressed cache */
#define	CACHE_VTAG		0x02	/* Virtual tagged cache */
#define	CACHE_PAC		0x04	/* Physical addressed cache */
#define	CACHE_PTAG		0x08	/* Physical tagged cache */
#define	CACHE_WRITEBACK		0x10	/* Writeback cache */
#define	CACHE_IOCOHERENT	0x20	/* I/O coherent cache */

extern int cache;

/*
 * Virtual Address Cache defines- right now just determine whether it
 * is a writeback or a writethru cache.
 *
 * MJ: a future merge with some of the sun4m structure defines could
 * MJ: tell us whether or not this cache has I/O going thru it, or
 * MJ: whether it is consistent, etc.
 */

#define	NO_VAC		0x0
#define	VAC_WRITEBACK	0x1	/* this vac is a writeback vac */
#define	VAC_WRITETHRU	0x2	/* this vac is a writethru vac */
#define	VAC_IOCOHERENT	0x100	/* i/o uses vac consistently */

/* set this to zero if no vac */
extern int vac;

#ifdef	IOC
extern int ioc;				/* there is an I/O cache */
#else
#define	ioc 0
#endif	/* IOC */

#ifdef	BCOPY_BUF
extern int bcopy_buf;			/* there is a bcopy buffer */
#else
#define	bcopy_buf 0
#endif	/* BCOPY_BUF */

#endif /* defined(_KERNEL) && !defined(_ASM) */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_CPU_H */
