/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * +++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 *		PROPRIETARY NOTICE (Combined)
 *
 * This source code is unpublished proprietary information
 * constituting, or derived under license from AT&T's UNIX(r) System V.
 * In addition, portions of such source code were derived from Berkeley
 * 4.3 BSD under license from the Regents of the University of
 * California.
 *
 *
 *
 *		Copyright Notice
 *
 * Notice of copyright on this source code product does not indicate
 * publication.
 *
 *	(c) 1986,1987,1988,1989  Sun Microsystems, Inc
 *	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *		All rights reserved.
 *
 */

#ifndef _SYS_PARAM_H
#define	_SYS_PARAM_H

#pragma ident	"@(#)param.h	1.32	95/08/28 SMI"	/* from SVR4.0 11.28 */

#ifndef _ASM		/* Avoid typedef headaches for assembly files */
#include <sys/types.h>
#include <sys/isa_defs.h>
#endif /* _ASM */


#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Fundamental variables; don't change too often.
 */

#ifndef	_POSIX_VDISABLE
#define	_POSIX_VDISABLE 0	/* Disable special character functions */
#endif

#ifndef	MAX_INPUT
#define	MAX_INPUT	512	/* Maximum bytes stored in the input queue */
#endif

#ifndef	MAX_CANON
#define	MAX_CANON	256	/* Maximum bytes for canoical processing */
#endif

#define	UID_NOBODY	60001	/* user ID no body */
#define	GID_NOBODY	UID_NOBODY

#define	UID_NOACCESS	60002	/* user ID no access */

#define	MAXPID		30000	/* max process id */
#define	MAXUID		60002	/* max user id */
#define	MAXLINK		32767	/* max links */

#define	NMOUNT		40	/* est. of # mountable fs for quota calc */

#define	CANBSIZ		256	/* max size of typewriter line	*/

#define	NOFILE		20	/* this define is here for	*/
				/* compatibility purposes only	*/
				/* and will be removed in a	*/
				/* later release		*/

/*
 * These define the maximum and minimum allowable values of the
 * configurable parameter NGROUPS_MAX.
 */
#define	NGROUPS_UMIN	0
#define	NGROUPS_UMAX	32

/*
 * NGROUPS_MAX_DEFAULT: *MUST* match NGROUPS_MAX value in limits.h.
 * Remember that the NFS protocol must rev. before this can be increased
 */
#define	NGROUPS_MAX_DEFAULT	16

/*
 * Priorities.  Should not be altered too much.
 */

#define	PMASK	0177
#define	PCATCH	0400
#define	PNOSTOP	01000
#define	PSWP	0
#define	PINOD	10
#define	PSNDD	PINOD
#define	PAMAP	PINOD
#define	PPMAP	PAMAP
#define	PRIBIO	20
#define	PZERO	25
#define	PMEM	0
#define	NZERO	20
#define	PPIPE	26
#define	PVFS	27
#define	PWAIT	30
#define	PLOCK	35
#define	PSLEP	39
#define	PUSER	60
#define	PIDLE	127

/*
 * Fundamental constants of the implementation--cannot be changed easily.
 */

#define	NBPW	sizeof (int)	/* number of bytes in an integer */
#ifndef NULL
#define	NULL	0
#endif
#define	CMASK	0		/* default mask for file creation */
#define	CDLIMIT	(1L<<11)	/* default max write address */
#define	NBPS		0x20000	/* Number of bytes per segment */
#define	NBPSCTR		512	/* Bytes per disk sector.	*/
#define	UBSIZE		512	/* unix block size.		*/
#define	SCTRSHFT	9	/* Shift for BPSECT.		*/

#ifdef _LITTLE_ENDIAN
#define	lobyte(X)	(((unsigned char *)&(X))[0])
#define	hibyte(X)	(((unsigned char *)&(X))[1])
#define	loword(X)	(((ushort *)&(X))[0])
#define	hiword(X)	(((ushort *)&(X))[1])
#endif
#ifdef _BIG_ENDIAN
#define	lobyte(X)	(((unsigned char *)&(X))[1])
#define	hibyte(X)	(((unsigned char *)&(X))[0])
#define	loword(X)	(((ushort *)&(X))[1])
#define	hiword(X)	(((ushort *)&(X))[0])
#endif

/* REMOTE -- whether machine is primary, secondary, or regular */
#define	SYSNAME 9		/* # chars in system name */
#define	PREMOTE 39

/*
 * MAXPATHLEN defines the longest permissible path length,
 * including the terminating null, after expanding symbolic links.
 * MAXSYMLINKS defines the maximum number of symbolic links
 * that may be expanded in a path name. It should be set high
 * enough to allow all legitimate uses, but halt infinite loops
 * reasonably quickly.
 * MAXNAMELEN is the length (including the terminating null) of
 * the longest permissible file (component) name.
 */
#define	MAXPATHLEN	1024
#define	MAXSYMLINKS	20
#define	MAXNAMELEN	256

#ifndef NADDR
#define	NADDR 13
#endif

/*
 * The following are defined to be the same as
 * defined in /usr/include/limits.h.  They are
 * needed for pipe and FIFO compatibility.
 */
#ifndef PIPE_BUF	/* max # bytes atomic in write to a pipe */
#define	PIPE_BUF	5120
#endif	/* PIPE_BUF */

#ifndef PIPE_MAX	/* max # bytes written to a pipe in a write */
#define	PIPE_MAX	5120
#endif	/* PIPE_MAX */

#define	NBBY	8			/* number of bits per byte */

/* macros replacing interleaving functions */
#define	dkblock(bp)	((bp)->b_blkno)
#define	dkunit(bp)	(minor((bp)->b_dev) >> 3)

/*
 * File system parameters and macros.
 *
 * The file system is made out of blocks of at most MAXBSIZE units,
 * with smaller units (fragments) only in the last direct block.
 * MAXBSIZE primarily determines the size of buffers in the buffer
 * pool. It may be made larger without any effect on existing
 * file systems; however making it smaller make make some file
 * systems unmountable.
 *
 * Note that the blocked devices are assumed to have DEV_BSIZE
 * "sectors" and that fragments must be some multiple of this size.
 */
#define	MAXBSIZE	8192
#define	DEV_BSIZE	512
#define	DEV_BSHIFT	9		/* log2(DEV_BSIZE) */
#define	MAXFRAG 	8
#define	MAXOFF_T	((off_t)(((u_long)1 << NBBY * sizeof (off_t) - 1) - 1))
#ifdef _NO_LONGLONG
#define	MAXOFFSET_T	MAXOFF_T
#else
#define	MAXOFFSET_T	((1LL << NBBY * sizeof (daddr_t) + DEV_BSHIFT - 1) - 1)
#endif

#define	btodb(bytes)			/* calculates (bytes / DEV_BSIZE) */ \
	((unsigned)(bytes) >> DEV_BSHIFT)
#define	dbtob(db)			/* calculates (db * DEV_BSIZE) */ \
	((unsigned)(db) << DEV_BSHIFT)

/*	64 bit versions of btodb and dbtob */
#define	lbtodb(bytes)			/* calculates (bytes / DEV_BSIZE) */ \
	((offset_t)(bytes) >> DEV_BSHIFT)
#define	ldbtob(db)			/* calculates (db * DEV_BSIZE) */ \
	((offset_t)(db) << DEV_BSHIFT)

#ifndef _ASM	/* Avoid typedef headaches for assembly files */
#ifndef NODEV
#define	NODEV	(dev_t)(-1)
#endif
#endif /* _ASM */

/*
 * Size of arg list passed in by user.
 */
#define	NCARGS	0x100000

/*
 * Scale factor for scaled integers used to count
 * %cpu time and load averages.
 */
#define	FSHIFT	8		/* bits to right of fixed binary point */
#define	FSCALE	(1<<FSHIFT)

/*
 * Delay units are in microseconds.
 *
 * XXX	These macros are not part of the DDI!
 */
#if defined(_KERNEL) && !defined(_ASM)
extern void drv_usecwait(clock_t);
#define	DELAY(n)	drv_usecwait(n)
#define	CDELAY(c, n)	\
{ \
	register int N = n; \
	while (--N > 0) { \
		if (c) \
			break; \
		drv_usecwait(1); \
	} \
}
#endif	/* defined(_KERNEL) && !defined(_ASM) */

#ifdef	__cplusplus
}
#endif

/*
 * The following is to free utilities from machine dependencies within
 * an architecture. Must be included after definition of DEV_BSIZE.
 */

#if (defined(_KERNEL) || defined(_KMEMUSER))

#if defined(_MACHDEP)
#include <sys/machparam.h>
#endif

#ifdef	__cplusplus
extern "C" {
#endif

#if !defined(_MACHDEP)

/*
 * Implementation architecture independent sections of the kernel use
 * this section.
 */
#if defined(_KERNEL) && !defined(_ASM)
extern int hz;
extern const unsigned int _pagesize;
extern const unsigned int _pageshift;
extern const unsigned int _pageoffset;
extern const unsigned int _pagemask;
extern const unsigned int _mmu_pagesize;
extern const unsigned int _mmu_pageshift;
extern const unsigned int _mmu_pageoffset;
extern const unsigned int _mmu_pagemask;
extern const unsigned int _kernelbase;
extern const unsigned int _userlimit;
extern const unsigned int _argsbase;
extern const unsigned int _msg_bsize;
extern const unsigned int _defaultstksz;
extern const unsigned int _nbpg;
extern const int _ncpu;
extern const int _clsize;
#endif	/* defined(_KERNEL) && !defined(_ASM) */

#define	HZ		hz
#define	PAGESIZE	_pagesize
#define	PAGESHIFT	_pageshift
#define	PAGEOFFSET	_pageoffset
#define	PAGEMASK	_pagemask
#define	MMU_PAGESIZE	_mmu_pagesize
#define	MMU_PAGESHIFT	_mmu_pageshift
#define	MMU_PAGEOFFSET	_mmu_pageoffset
#define	MMU_PAGEMASK	_mmu_pagemask

#define	KERNELBASE	_kernelbase
#define	USERLIMIT	_userlimit
#define	ARGSBASE	_argsbase
#define	MSG_BSIZE	_msg_bsize
#define	DEFAULTSTKSZ	_defaultstksz
#define	NBPG		_nbpg
#define	NCPU		_ncpu
#define	CLSIZE		_clsize

#endif	/* defined(_MACHDEP) */

/*
 * Some random macros for units conversion.
 *
 * These are machine independent but contain constants (*PAGESHIFT) which
 * are only defined in the machine dependent file.
 */

/*
 * MMU pages to bytes, and back (with and without rounding)
 */
#define	mmu_ptob(x)	((x) << MMU_PAGESHIFT)
#define	mmu_btop(x)	(((unsigned)(x)) >> MMU_PAGESHIFT)
#define	mmu_btopr(x)	((((unsigned)(x) + MMU_PAGEOFFSET) >> MMU_PAGESHIFT))

/*
 * 2 versions of pages to disk blocks
 */
#define	mmu_ptod(x)	((x) << (MMU_PAGESHIFT - DEV_BSHIFT))
#define	ptod(x)		((x) << (PAGESHIFT - DEV_BSHIFT))

/*
 * pages to bytes, and back (with and without rounding)
 */
#define	ptob(x)		((x) << PAGESHIFT)
#define	btop(x)		(((unsigned)(x)) >> PAGESHIFT)
#define	btopr(x)	((((unsigned)(x) + PAGEOFFSET) >> PAGESHIFT))

/*
 * disk blocks to pages, rounded and truncated
 */
#define	NDPP		(PAGESIZE/DEV_BSHIFT)	/* # of disk blocks per page */
#define	dtop(DD)	(((DD) + NDPP - 1) >> (PAGESHIFT - DEV_BSHIFT))
#define	dtopt(DD)	((DD) >> (PAGESHIFT - DEV_BSHIFT))

/*
 * XXX - Old names for some backwards compatibility
 */
#define	PAGOFF(x)	(((uint)(x)) & PAGEOFFSET)
#define	NBPC		MMU_PAGESIZE
#define	NBPP		MMU_PAGESIZE
#define	BPCSHIFT	MMU_PAGESHIFT

#ifdef	__cplusplus
}
#endif

#else	/* (defined(_KERNEL) || defined(_KMEMUSER)) */

/*
 * The following are assorted machine dependent values which can be
 * obtained in a machine independent manner through sysconf(2) or
 * sysinfo(2). In order to guarentee that these provide the expected
 * value at all times, the System Private interface (leading underscore)
 * is used.
 */

#include <sys/unistd.h>

#ifdef	__cplusplus
extern "C" {
#endif

#if !defined(_ASM)
extern long _sysconf(int);	/* System Private interface to sysconf() */
#endif	/* !defined(_ASM) */

#define	HZ		((int)_sysconf(_SC_CLK_TCK))
#define	TICK		(1000000000/((int)_sysconf(_SC_CLK_TCK)))
#define	PAGESIZE	(_sysconf(_SC_PAGESIZE))
#define	PAGEOFFSET	(PAGESIZE - 1)
#define	PAGEMASK	(~PAGEOFFSET)

#ifdef	__cplusplus
}
#endif

#endif	/* (defined(_KERNEL) || defined(_KMEMUSER)) &&  defined(_MACHDEP) */

#endif	/* _SYS_PARAM_H */
