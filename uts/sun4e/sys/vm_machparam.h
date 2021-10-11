/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * Copyright (c) 1989 by Sun Microsystems, Inc.
 */

#ifndef _SYS_VM_MACHPARAM_H
#define	_SYS_VM_MACHPARAM_H

#pragma ident	"@(#)vm_machparam.h	1.12	94/03/09 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Machine dependent constants for Sun4e.
 */

/*
 * USRTEXT is the start of the user text/data space.
 */
#define	USRTEXT		0x2000

/*
 * Virtual memory related constants for UNIX resource control, all in bytes
 * The default stack size of 8M allows an optimization of mmu mapping
 * resources so that in normal use a single mmu region map entry (smeg)
 * can be used to map both the stack and shared libraries
 */
#define	DFLSSIZ		(8*1024*1024)		/* initial stack size limit */

#ifdef LATER

#define	DFLDSIZ		dfldsiz
#define	MAXDSIZ		maxdsiz
#define	MAXSSIZ		maxssiz

#ifndef _ASM
extern unsigned int	dfldsiz;
extern unsigned int 	maxdsiz;
extern unsigned int	maxssiz;
#endif /* !_ASM */

#endif /* LATER */

#ifndef DFLDSIZ
#define	DFLDSIZ		((511*1024*1024)-USRTEXT) /* initial data size limit */
#endif
#ifndef MAXDSIZ
#define	MAXDSIZ		((511*1024*1024)-USRTEXT) /* max data size limit */
#endif
#ifndef MAXSSIZ
#define	MAXSSIZ		((511*1024*1024)-KERNELSIZE) /* max stack size limit */
#endif

/*
 * The following are limits beyond which the hard or soft limits for stack
 * and data cannot be increased. These may be viewed as fundemental
 * characteristics of the system. Note: a bug in SVVS requires that the
 * default hardlimit be increasable, so the default hard limit must be
 * less than these physical limits.
 */
#define	DSIZE_LIMIT	((512*1024*1024)-USRTEXT) /* physical data limit */
#define	SSIZE_LIMIT	((512*1024*1024)-KERNELSIZE) /* physical stack limit */

/*
 * Size of the kernel segkmem system pte table.  This virtual
 * space is controlled by the resource map "kernelmap".
 */
#define	SYSPTSIZE	(0xe00000 / MMU_PAGESIZE)

/*
 * Size of the ethernet addressable kernel segkmem system pte table.
 * This virtual space is controlled by the resource map "ekernelmap".
 * The ethernet interfaces in some sun machines can address only
 * the upper 16 Megabytes of memory.  Since the ethernet
 * driver kmem_allocs its memory, we bias all kmem_allocs
 * to try ekernelmap first and if it fails try kernelmap.
 * Folks that allocate directly out of kernelmap, above,
 * get memory that is non-ethernet addressable.
 */
#define	E_SYSPTSIZE	(0x900000 / MMU_PAGESIZE)

/*
 * The virtual address space to be used by the seg_map segment
 * driver for fast kernel mappings.
 */
#define	SEGMAPSIZE	0x400000

/*
 * The time for a process to be blocked before being very swappable.
 * This is a number of seconds which the system takes as being a non-trivial
 * amount of real time. You probably shouldn't change this;
 * it is used in subtle ways (fractions and multiples of it are, that is, like
 * half of a ``long time'', almost a long time, etc.)
 * It is related to human patience and other factors which don't really
 * change over time.
 */
#define	MAXSLP 		20

/*
 * A swapped in process is given a small amount of core without being bothered
 * by the page replacement algorithm. Basically this says that if you are
 * swapped in you deserve some resources. We protect the last SAFERSS
 * pages against paging and will just swap you out rather than paging you.
 * Note that each process has at least UPAGES pages which are not
 * paged anyways so this number just means a swapped in process is
 * given around 32k bytes.
 */
/*
 * nominal ``small'' resident set size
 * protected against replacement
 */
#define	SAFERSS		3

/*
 * DISKRPM is used to estimate the number of paging i/o operations
 * which one can expect from a single disk controller.
 *
 * XXX - The system doesn't account for multiple swap devices.
 */
#define	DISKRPM		60

/*
 * Default Paging thresholds (see vm_pageout.c).
 * Strategy of 11/30/93:
 *	lotsfree is 1/32 of memory, but at most 512k bytes.
 *	desfree is 1/64 of memory, but at most 200k bytes.
 *	minfree is 1/2 of desfree, but at most 100k bytes.
 */
#define	LOTSFREE	(512 * 1024)
#define	LOTSFREEFRACT	32
#define	DESFREE		(200 * 1024)
#define	DESFREEFRACT	64
#define	MINFREE		(100 * 1024)
#define	MINFREEFRACT	2

/*
 * The maximum value for handspreadpages which is the the distance
 * between the two clock hands in pages.
 */
#define	MAXHANDSPREADPAGES	((64 * 1024 * 1024) / PAGESIZE)

/*
 * Paged text files that are less than PGTHRESH bytes
 * may be "prefaulted in" instead of demand paged.
 */
#define	PGTHRESH	(280 * 1024)

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_VM_MACHPARAM_H */
