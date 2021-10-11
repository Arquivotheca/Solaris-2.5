/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SYS_RT_H
#define	_SYS_RT_H

#pragma ident	"@(#)rt.h	1.11	93/08/17 SMI"	/* SVr4.0 1.4 */

#include <sys/types.h>
#include <sys/thread.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Real-time dispatcher parameter table entry
 */
typedef struct	rtdpent {
	pri_t	rt_globpri;	/* global (class independent) priority */
	long	rt_quantum;	/* default quantum associated with this level */
} rtdpent_t;


/*
 * Real-time class specific proc structure
 */
typedef struct rtproc {
	long		rt_pquantum;	/* time quantum given to this proc */
	long		rt_timeleft;	/* time remaining in procs quantum */
	pri_t		rt_pri;		/* priority within rt class */
	ushort		rt_flags;	/* flags defined below */
	kthread_id_t	rt_tp;		/* pointer to thread */
	char		*rt_pstatp;	/* pointer to p_stat */
	pri_t		*rt_pprip;	/* pointer to t_pri */
	uint		*rt_pflagp;	/* pointer to p_flag */
	struct rtproc	*rt_next;	/* link to next rtproc on list */
	struct rtproc	*rt_prev;	/* link to previous rtproc on list */
} rtproc_t;


/* Flags */
#define	RTBACKQ	0x0002		/* proc goes to back of disp q when preempted */


#ifdef _KERNEL
/*
 * Kernel version of real-time class specific parameter structure
 */
typedef struct	rtkparms {
	pri_t	rt_pri;
	long	rt_tqntm;
} rtkparms_t;
#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_RT_H */
