/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SYS_MACHPARAM_H
#define	_SYS_MACHPARAM_H

#pragma ident	"@(#)machparam.h	1.3	93/11/10 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Machine dependent parameters and limits - sun4u version.
 */

/*
 * Define the VAC symbol (etc.) if we could run on a machine
 * which has a Virtual Address Cache
 *
 * This stuff gotta go.
 */
#define	IOMMU			/* support sun4u IOMMU */
#define	IOC			/* support sun4u IOCache */
#define	VAC			/* support virtual addressed caches */

/*
 * The maximum possible number of UPA devices in a system.
 */
#define	MAX_UPA			32

/*
 * Maximum number of CPUs we support
 */
#ifdef	MP
#define	NCPU	32
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

/*
 * XXX make sure the MMU_PAGESHIFT definition here is
 * consistent with the one in param.h
 */
#define	MMU_PAGESHIFT		13
#define	MMU_PAGESIZE		(1<<MMU_PAGESHIFT)
#define	MMU_PAGEOFFSET		(MMU_PAGESIZE - 1)
#define	MMU_PAGEMASK		(~MMU_PAGEOFFSET)

#define	MMU_PAGESHIFT64K	16
#define	MMU_PAGESIZE64K		(1 << MMU_PAGESHIFT64K)
#define	MMU_PAGEOFFSET64K	(MMU_PAGESIZE64K - 1)
#define	MMU_PAGEMASK64K		(~MMU_PAGEOFFSET64K)

#define	MMU_PAGESHIFT512K	19
#define	MMU_PAGESIZE512K	(1 << MMU_PAGESHIFT512K)
#define	MMU_PAGEOFFSET512K	(MMU_PAGESIZE512K - 1)
#define	MMU_PAGEMASK512K	(~MMU_PAGEOFFSET512K)

#define	MMU_PAGESHIFT4M		22
#define	MMU_PAGESIZE4M		(1 << MMU_PAGESHIFT4M)
#define	MMU_PAGEOFFSET4M	(MMU_PAGESIZE4M - 1)
#define	MMU_PAGEMASK4M		(~MMU_PAGEOFFSET4M)

#define	PAGESHIFT	13
#define	PAGESIZE	(1<<PAGESHIFT)
#define	PAGEOFFSET	(PAGESIZE - 1)
#define	PAGEMASK	(~PAGEOFFSET)

/*
 * DATA_ALIGN is used to define the alignment of the Unix data segment.
 */
#define	DATA_ALIGN	0x2000

/*
 * DEFAULT KERNEL THREAD stack size.
 */
#define	DEFAULTSTKSZ	PAGESIZE

/*
 * KERNELSIZE is the amount of virtual address space the kernel
 * uses in all contexts.
 */
#define	KERNELSIZE	0x20000000

/*
 * KERNELBASE is the virtual address which
 * the kernel text/data mapping starts in all contexts.
 */
#define	KERNELBASE	0x10000000

/*
 * Define userlimit
 */
#define	USERLIMIT	0xF0000000

/*
 * Define SEGKPBASE, start of the segkp segment.
 */
#define	SEGKPBASE	0x30000000

/*
 * Define SEGMAPBASE, start of the segmap segment.
 */
#define	SEGMAPBASE	0x40000000

/*
 * SYSBASE is the virtual address which
 * the kernel allocated memory mapping starts in all contexts.
 */
#define	SYSBASE		0x50000000

/*
 * Define the end of the Sysbase segment. This is where
 * kadb is being mapped.
 */
#define	SYSEND		0xEDD00000

/*
 * ARGSBASE is the base virtual address of the range which
 * the kernel uses to map the arguments for exec.
 */
#define	ARGSBASE	(SEGKPBASE - NCARGS)

/*
 * PPMAPBASE is the base virtual address of the range which
 * the kernel uses to quickly map pages for operations such
 * as ppcopy, pagecopy, pagezero, and pagesum.
 */
#define	PPMAPSIZE	(512 * 1024)
#define	PPMAPBASE	(ARGSBASE - PPMAPSIZE)

/*
 * Size of a kernel threads stack.  It must be a whole number of pages
 * since the segment it comes from will only allocate space in pages.
 */
#define	T_STACKSZ	PAGESIZE

/*
 * Msgbuf size.
 */
#define	MSG_BSIZE	((8 * 1024) - sizeof (struct msgbuf_hd))
#define	MSGBUF_BASE	(SYSBASE + MMU_PAGESIZE)

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

#define	PFN_TO_UPAID(pfn)	(((pfn) >> 20) & 0x1F)
#define	PFN_TO_BUSTYPE(pfn)	(((pfn) >> 19) & 0x1FF)
#define	BUSTYPE_TO_PFN(btype, pfn)			\
	((((btype) & 0x1FF) << 19) | (pfn) & 0x7FFFF)
#define	IO_BUSTYPE(pfn)	((PFN_TO_BUSTYPE(pfn) & 0x100) >> 8)

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_MACHPARAM_H */
