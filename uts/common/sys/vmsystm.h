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
 *	(c) 1986, 1987, 1988, 1989  Sun Microsystems, Inc
 *	(c) 1983, 1984, 1985, 1986, 1987, 1988, 1989  AT&T.
 *		All rights reserved.
 *
 */

#ifndef _SYS_VMSYSTM_H
#define	_SYS_VMSYSTM_H

#pragma ident	"@(#)vmsystm.h	2.26	94/11/08 SMI"	/* SVr4.0 1.8 */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Miscellaneous virtual memory subsystem variables and structures.
 */

#ifdef _KERNEL
extern int	freemem;	/* remaining blocks of free memory */
extern int	avefree;	/* 5 sec moving average of free memory */
extern int	avefree30;	/* 30 sec moving average of free memory */
extern int	deficit;	/* estimate of needs of new swapped in procs */
extern int	nscan;		/* number of scans in last second */
extern int	desscan;	/* desired pages scanned per second */

/* writable copies of tunables */
extern int	maxpgio;	/* max paging i/o per sec before start swaps */
extern int	lotsfree;	/* max free before clock freezes */
extern int	minfree;	/* minimum free pages before swapping begins */
extern int	desfree;	/* no of pages to try to keep free via daemon */

/*
 * TRUE if the pageout daemon, fsflush daemon or the scheduler.  These
 * processes can't sleep while trying to free up memory since a deadlock
 * will occur if they do sleep.
 */
#define	NOMEMWAIT() (ttoproc(curthread) == proc_pageout || \
			ttoproc(curthread) == proc_fsflush || \
			ttoproc(curthread) == proc_sched)

/* insure non-zero */
#define	nz(x)	((x) != 0 ? (x) : 1)

/*
 * Flags passed by the swapper to swapout routines of each
 * scheduling class.
 */
#define	HARDSWAP	1
#define	SOFTSWAP	2

extern int maxslp;
extern ulong pginrate;
extern ulong pgoutrate;
extern void swapout_lwp(klwp_t *);

extern	int valid_va_range(caddr_t *, u_int *, u_int, int);
extern	int valid_usr_range(caddr_t, size_t);
extern	int useracc(caddr_t, u_int, int);
extern	void map_addr(caddr_t *, u_int, off_t, int);
extern	void map_addr_proc(caddr_t *, u_int, off_t, int, struct proc *);
extern	void memialloc(u_int, u_int, u_int);
extern	void vmmeter(int);

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_VMSYSTM_H */
