/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SYS_DISP_H
#define	_SYS_DISP_H

#pragma ident	"@(#)disp.h	1.32	95/08/18 SMI"	/* SVr4.0 1.11	*/

#include <sys/priocntl.h>
#include <sys/thread.h>
#include <sys/bitmap.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * The following is the format of a dispatcher queue entry.
 */
typedef struct dispq {
	kthread_id_t	dq_first;	/* first thread on queue or NULL */
	kthread_id_t	dq_last;	/* last thread on queue or NULL */
	int		dq_sruncnt;	/* number of loaded, runnable */
					/*    threads on queue */
} dispq_t;

/*
 * Dispatch queue structure. Contained in struct cpu (sys/cpuvar.h)
 */
typedef struct _disp {
	disp_lock_t	disp_lock;	/* protects dispatching fields */
	pri_t		disp_npri;	/* # of priority levels in queue */
	dispq_t		*disp_q;		/* the dispatch queue */
	dispq_t		*disp_q_limit;	/* ptr past end of dispatch queue */
	ulong		*disp_qactmap;	/* bitmap of active dispatch queues */

	/*
	 * Priorities:
	 *	disp_maxrunpri is the maximum run priority of runnable threads
	 * 	on this queue.  It is -1 if nothing is runnable.
	 *
	 *	disp_max_unbound_pri is the maximum run priority of threads on
	 *	this dispatch queue but runnable by any CPU.  This may be left
	 * 	artificially high, then corrected when some CPU tries to take
	 *	an unbound thread.  It is -1 if nothing is runnable.
	 */
	pri_t		disp_maxrunpri;	/* maximum run priority */
	pri_t		disp_max_unbound_pri;	/* max pri of unbound threads */

	int		disp_nrunnable;	/* runnable threads in cpu dispq */

	struct cpu	*disp_cpu;	/* cpu owning this queue or NULL */
} disp_t;

#if defined(_KERNEL)

#define	MAXCLSYSPRI	99
#define	MINCLSYSPRI	60

/*
 * Global scheduling variables.
 *	- See sys/cpuvar.h for CPU-local variables.
 */
extern int	nswapped;	/* number of swapped threads */
				/* nswapped protected by swap_lock */

extern	pri_t	minclsyspri;	/* minimum level of any system class */

/*
 * Kernel preemption occurs if a higher-priority thread is runnable with
 * a priority at or above kpreemptpri.
 */
extern	pri_t	kpreemptpri;	/* level above which preemption takes place */

/*
 * Macro for use by scheduling classes to decide whether the thread is about
 * to be scheduled or not.  This returns the maximum run priority.
 */
#define	DISP_MAXRUNPRI(t)	((t)->t_disp_queue->disp_maxrunpri)

/*
 * Public scheduling functions.
 */
#if defined(__STDC__)

extern boolean_t	dispdeq(kthread_id_t);
extern int		getcid(char *, id_t *);
extern int		getcidbyname(char *, id_t *);
extern int		parmsin(pcparms_t *, kthread_id_t, kthread_id_t);
extern int		parmsout(pcparms_t *, kthread_id_t);
extern int		parmsset(pcparms_t *, kthread_id_t);
extern void		dispinit(sclass_t *);
extern int		intr_active(struct cpu *, int);
extern void		parmsget(kthread_id_t, pcparms_t *);
extern void		preempt(void);
extern void		setbackdq(kthread_id_t);
extern void		setfrontdq(kthread_id_t);
extern void		swtch(void);
extern void		swtch_from_zombie(void);
extern void		dq_sruninc(kthread_id_t t);
extern void		dq_srundec(kthread_id_t t);
extern void		cpu_rechoose(kthread_id_t);
extern void		cpu_surrender(kthread_id_t);
extern void		kpreempt(int);
extern struct cpu *	disp_lowpri_cpu(struct cpu *hint);
extern int		disp_bound_threads(struct cpu *);
extern void		disp_cpu_inactive(struct cpu *);
extern void		resume(kthread_id_t);
extern void		resume_from_intr(kthread_id_t);
extern void		resume_from_zombie(kthread_id_t);
extern void		disp_swapped_enq(kthread_id_t);

#else

extern boolean_t	dispdeq();
extern int		getcid();
extern int		getcidbyname();
extern int		parmsin();
extern int		parmsout();
extern int		parmsset();
extern int		intr_active();
extern void		dispinit();
extern void		parmsget();
extern void		preempt();
extern void		setbackdq();
extern void		setfrontdq();
extern void		swtch();
extern void		swtch_from_zombie();
extern void		dq_sruninc();
extern void		dq_srundec();
extern void		cpu_rechoose();
extern void		cpu_surrender();
extern void		kpreempt();
extern struct cpu *	disp_lowpri_cpu();
extern int		disp_bound_threads();
extern void		disp_cpu_inactive();
extern void		resume();
extern void		resume_from_intr();
extern void		resume_from_zombie();
extern void		disp_swapped_enq();

#endif	/* __STDC__ */

#define	KPREEMPT_SYNC		(-1)
#define	kpreempt_disable()	(curthread->t_preempt++)
#define	kpreempt_enable()				\
	{						\
		if (--curthread->t_preempt == 0 &&	\
		    CPU->cpu_kprunrun)			\
			kpreempt(KPREEMPT_SYNC);	\
	}

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_DISP_H */
