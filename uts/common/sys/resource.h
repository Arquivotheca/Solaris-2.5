/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SYS_RESOURCE_H
#define	_SYS_RESOURCE_H

#pragma ident	"@(#)resource.h	1.12	95/02/26 SMI"	/* SVr4.0 1.11 */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Process priority specifications
 */

#define	PRIO_PROCESS	0
#define	PRIO_PGRP	1
#define	PRIO_USER	2


/*
 * Resource limits
 */

#define	RLIMIT_CPU	0		/* cpu time in milliseconds */
#define	RLIMIT_FSIZE	1		/* maximum file size */
#define	RLIMIT_DATA	2		/* data size */
#define	RLIMIT_STACK	3		/* stack size */
#define	RLIMIT_CORE	4		/* core file size */
#define	RLIMIT_NOFILE	5		/* file descriptors */
#define	RLIMIT_VMEM	6		/* maximum mapped memory */
#define	RLIMIT_AS	RLIMIT_VMEM

#define	RLIM_NLIMITS	7		/* number of resource limits */

#define	RLIM_INFINITY	0x7fffffff

typedef unsigned long rlim_t;

struct rlimit {
	rlim_t	rlim_cur;		/* current limit */
	rlim_t	rlim_max;		/* maximum value for rlim_cur */
};

#ifdef _KERNEL

extern struct rlimit rlimits[];

#if defined(__STDC__)

extern int	rlimit(int, rlim_t, rlim_t);

#else

extern int	rlimit();

#endif	/* __STDC__ */

#else

#include <sys/time.h>

#define	RUSAGE_SELF	0
#define	RUSAGE_CHILDREN	-1

struct	rusage {
	struct timeval ru_utime;	/* user time used */
	struct timeval ru_stime;	/* system time used */
	long	ru_maxrss;		/* XXX: 0 */
	long	ru_ixrss;		/* XXX: 0 */
	long	ru_idrss;		/* XXX: sum of rm_asrss */
	long	ru_isrss;		/* XXX: 0 */
	long	ru_minflt;		/* any page faults not requiring I/O */
	long	ru_majflt;		/* any page faults requiring I/O */
	long	ru_nswap;		/* swaps */
	long	ru_inblock;		/* block input operations */
	long	ru_oublock;		/* block output operations */
	long	ru_msgsnd;		/* messages sent */
	long	ru_msgrcv;		/* messages received */
	long	ru_nsignals;		/* signals received */
	long	ru_nvcsw;		/* voluntary context switches */
	long	ru_nivcsw;		/* involuntary " */
};

#if defined(__STDC__)

extern int setrlimit(int, const struct rlimit *);
extern int getrlimit(int, struct rlimit *);

#else

extern int getrlimit();
extern int setrlimit();

#endif  /* __STDC__ */

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_RESOURCE_H */
