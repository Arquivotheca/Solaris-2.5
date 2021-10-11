/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SYS_MACHPARAM_H
#define	_SYS_MACHPARAM_H

#pragma ident	"@(#)machparam.h	1.41	95/07/04 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Machine dependent parameters and limits - sun4d version.
 */

/*
 * The following symbols are required for the sun4d architecture.
 *	OPENPROMS	# Open Boot Prom interface
 *	MEMSEG		# uses 4.1 style memory segments
 *	NEWDUMP		# uses sparse crash dumping scheme
 *	SAIO_COMPAT	# OBP uses "bootparam" interface
 */
#define	MEMSEG
#define	NEWDUMP

/*
 * Define the VAC symbol if we could run on a machine
 * which has a Virtual Address Cache
 *
 * IOMMU and IOC are attributes of the system,
 * not the module; anyway, logic similar to the
 * above should be used.
 */
/* no need to bracketed with sun4d define */
#define	IOMMU
#define	STREAM_DVMA		/* this is the equivalence of the "old" IOC */
#define	CONSITENT_DVMA

/*
 * This is an artificial limit that will later be dynamic ??
 */
#ifdef	MP
#define	NCPU	20
#else
#define	NCPU	1
#endif

/*
 * Define the FPU symbol if we could run on a machine with an external
 * FPU (i.e. not integrated with the normal machine state like the vax).
 *
 * The fpu is defined in the architecture manual, and the kernel hides
 * its absence if it is not present, that's pretty integrated, no?
 */

#define	HZ	100		/* ticks/second of the clock */
#define	TICK	(1000000000/HZ)	/* nanoseconds per tick */

/*
 * MMU_PAGES* describes the physical page size used by the mapping hardware.
 * PAGES* describes the logical page size used by the system.
 */

#define	MMU_PAGESIZE	0x1000		/* 4096 bytes */
#define	MMU_PAGESHIFT	12		/* log2(MMU_PAGESIZE) */
#define	MMU_PAGEOFFSET	(MMU_PAGESIZE-1) /* Mask of address bits in page */
#define	MMU_PAGEMASK	(~MMU_PAGEOFFSET)

#define	MMU_SEGSIZE	0x40000		/* 256 K bytes */
#define	MMU_SEGSHIFT	18
#define	MMU_SEGOFFSET	(MMU_SEGSIZE - 1)
#define	MMU_SEGMASK	(~MMUPAGEOFFSET)

#define	PAGESIZE	0x1000		/* All of the above, for logical */
#define	PAGESHIFT	12
#define	PAGEOFFSET	(PAGESIZE - 1)
#define	PAGEMASK	(~PAGEOFFSET)

/*
 * DATA_ALIGN is used to define the alignment of the Unix data segment.
 * We leave this 8K so we are Sun4 binary compatible.
 */
#define	DATA_ALIGN	0x2000

/*
 * DEFAULT KERNEL THREAD stack size.
 */
#define	DEFAULTSTKSZ	2*PAGESIZE

/*
 * KERNELSIZE is the amount of virtual address space the kernel
 * uses in all contexts.
 */
#define	KERNELSIZE	(512*1024*1024)

/*
 * KERNELBASE is the virtual address which
 * the kernel text/data mapping starts in all contexts.
 */
#define	KERNELBASE	(0-KERNELSIZE)

/*
 * Define upper limit on user address space
 */
#define	USERLIMIT	KERNELBASE

/*
 * SYSBASE is the virtual address which
 * the kernel allocated memory mapping starts in all contexts.
 */
#define	SYSBASE		(0xF0200000)

/*
 * ARGSBASE is the base virtual address of the range which
 * the kernel uses to map the arguments for exec.
 */
#define	ARGSSIZE	(NCARGS)
#define	ARGSBASE	(SYSBASE - ARGSSIZE)


#define	NKL2PTS		((0 - KERNELBASE) / L2PTSIZE)
#define	NKL3PTS		((0 - KERNELBASE) / L3PTSIZE)

/*
 * Size of a kernel threads stack.  It must be a whole number of pages
 * since the segment it comes from will only allocate space in pages.
 */
#define	T_STACKSZ	2*PAGESIZE

/*
 * Msgbuf size.
 */
#define	MSG_BSIZE	((8 * 1024) - sizeof (struct msgbuf_hd))

/*
 * XXX - Old names for some backwards compatibility
 * Ones not used by any code in the Sun-4 S5R4 should be nuked (this means that
 * if some driver uses it, you can only nuke it if you change the driver not to
 * use it, which may not be worth the effort; if some common component of S5R4
 * uses it, it stays around until that component is changed).
 */
#define	NBPG		MMU_PAGESIZE
#define	PGOFSET		MMU_PAGEOFFSET
#define	PGSHIFT		MMU_PAGESHIFT

#define	CLSIZE		1
#define	CLSIZELOG2	0
#define	CLBYTES		PAGESIZE
#define	CLOFSET		PAGEOFFSET
#define	CLSHIFT		PAGESHIFT
#define	clrnd(i)	(i)

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_MACHPARAM_H */
