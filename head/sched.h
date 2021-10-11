/*	Copyright (c) 1993, by Sun Microsystems, Inc.	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF SMI	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SCHED_H
#define	_SCHED_H

#pragma ident	"@(#)sched.h	1.6	93/12/20 SMI"

#include <sys/time.h>

#ifdef	__cplusplus
extern "C" {
#endif

struct sched_param {
	int	sched_priority;	/* process execution scheduling priority */
	int	sched_pad[8];	/* sizeof(sched_param) ==		*/
				/*	sizeof(sched_priority) +	*/
				/*	sizeof(pcparms_t.pc_clparms)	*/
};

/*
 *	POSIX scheduling policies
 */
#define	SCHED_OTHER	0
#define	SCHED_FIFO	1
#define	SCHED_RR	2

/*
 * function prototypes
 */
#if	defined(__STDC__)
int	sched_getparam(pid_t pid, struct sched_param *param);
int	sched_setparam(pid_t pid, const struct sched_param *param);
int	sched_getscheduler(pid_t pid);
int	sched_setscheduler(pid_t pid, int policy,
		const struct sched_param *param);
int	sched_yield(void);
int	sched_get_priority_max(int policy);
int	sched_get_priority_min(int policy);
int	sched_get_rr_get_interval(pid_t pid, struct timespec *interval);
#else
int	sched_getparam();
int	sched_setparam();
int	sched_getscheduler();
int	sched_setscheduler();
int	sched_yield();
int	sched_get_priority_max();
int	sched_get_priority_min();
int	sched_get_rr_get_interval();
#endif	/* __STDC__ */

#ifdef	__cplusplus
}
#endif

#endif	/* _SCHED_H */
