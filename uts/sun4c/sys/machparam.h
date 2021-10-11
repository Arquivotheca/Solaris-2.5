/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SYS_MACHPARAM_H
#define	_SYS_MACHPARAM_H

#pragma ident	"@(#)machparam.h	1.19	95/07/04 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Machine dependent parameters and limits - Sun4c version.
 */

/*
 * The following symbols are required for the Sun4c architecture.
 *	OPENPROMS	# Open Boot Prom interface
 *	MEMSEG		# uses 4.1 style memory segments
 *	NEWDUMP		# uses sparse crash dumping scheme
 *	SAIO_COMPAT	# OBP uses "bootparam" interface
 */
#define	MEMSEG
#define	NEWDUMP

/*
 * Define the VAC symbol if we could run on a machine
 * which has a Virtual Address Cache (e.g. SUN4C_60)
 */
#define	VAC

#define	NCPU 	1 	/* no MPs in this architectural family */
/*
 * Define the FPU symbol if we could run on a machine with an external
 * FPU (i.e. not integrated with the normal machine state like the vax).
 *
 * The fpu is defined in the architecture manual, and the kernel hides
 * its absence if it is not present, that's pretty integrated, no?
 */

/*
 * Define the MMU_3LEVEL symbol if we could run on a machine with
 * a three level mmu.   We also assume these machines have region
 * and user cache flush operations.
 */

/* none at present */

/*
 * Define IOC if we could run on machines that have an I/O cache.
 */

/* none at present */

/*
 * Define BCOPY_BUF if we could run on machines that have a bcopy buffer.
 */

/* none at present */

/*
 * Define VA_HOLE for machines that have a hole in the virtual address space.
 */
#define	VA_HOLE

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
#define	KERNELSIZE	(256*1024*1024)

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
 * ARGSBASE is the base virtual address of the range which
 * the kernel uses to map the arguments for exec.
 */
#define	ARGSBASE	(SYSBASE - NCARGS)

/*
 * SYSBASE is the virtual address which
 * the kernel allocated memory mapping starts in all contexts.
 */
#define	SYSBASE		(0-(64*1024*1024))

/*
 * E_SYSBASE is the virtual address which kernel allocated memory
 * mapping addressable by the ethernet starts in all contexts.
 */
#define	E_SYSBASE	(0-(16*1024*1024))

/*
 * Size of a kernel threads stack.  It must be a whole number of pages
 * since the segment it comes from will only allocate space in pages.
 */
#define	T_STACKSZ	2*PAGESIZE

/*
 * Msgbuf size.
 *
 *	XXX FIX THIS, HH - the prom and/or boot scribbles in msgbuf
 *	during boot.  luckily, it only spews in the last 1k, so we can
 *	avoid having icky stuff in msgbuf by making it only 7K (was 8).
 */
#define	MSG_BSIZE	((7 * 1024) - sizeof (struct msgbuf_hd))

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
