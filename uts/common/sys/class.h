/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SYS_CLASS_H
#define	_SYS_CLASS_H

#pragma ident	"@(#)class.h	1.27	95/09/11 SMI"

#include <sys/t_lock.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * NOTE: Developers making use of the scheduler class switch mechanism
 * to develop scheduling class modules should be aware that the
 * architecture is not frozen and the kernel interface for scheduling
 * class modules may change in future releases of System V.  Support
 * for the current interface is not guaranteed and class modules
 * developed to this interface may require changes in order to work
 * with future releases of the system.
 */

typedef struct sclass {
	char	*cl_name;	/* class name */
	void	(*cl_init)();	/* class specific initialization function */
	struct classfuncs *cl_funcs;	/* pointer to classfuncs structure */
	krwlock_t *cl_lock;	/* read/write lock for class structure */
	int	cl_count;	/* number of threads trying to load class */
	size_t	cl_size;	/* size of per-thread data */
} sclass_t;

#define	STATIC_SCHED		(krwlock_t *)0xffffffff
#define	LOADABLE_SCHED(s)	((s)->cl_lock != STATIC_SCHED)
#define	SCHED_INSTALLED(s)	((s)->cl_funcs != NULL)
#define	ALLOCATED_SCHED(s)	((s)->cl_lock != NULL)

#ifdef	_KERNEL
extern int	nclass;		/* number of configured scheduling classes */
extern char	*initclass;	/* class of init process */
extern struct sclass sclass[];	/* the class table */
#endif

/*
 * three different ops vectors are bundled together, here.
 * one is for each of the fundamental objects acted upon
 * by these operators: procs, threads, and the class manager itself.
 */
typedef struct classfuncs {
	struct class_ops {
		int		(*cl_admin)();
		int		(*cl_getclinfo)();
		int		(*cl_parmsin)();
		int		(*cl_parmsout)();
	} sclass;
	struct thread_ops {
		int		(*cl_enterclass)();
		void		(*cl_exitclass)();
		int		(*cl_fork)();
		void		(*cl_forkret)();
		void		(*cl_parmsget)();
		int		(*cl_parmsset)();
		void		(*cl_stop)();
		pri_t		(*cl_swapin)();
		pri_t 		(*cl_swapout)();
		void 		(*cl_trapret)();
		void		(*cl_preempt)();
		void		(*cl_setrun)();
		void		(*cl_sleep)();
		void		(*cl_tick)();
		void		(*cl_wakeup)();
		int		(*cl_donice)();
		struct cpu *	(*cl_cpu_choose)(struct _kthread *t, pri_t pri);
		pri_t		(*cl_globpri)();
		void		(*cl_set_process_group)(pid_t sid, \
				    pid_t bg_pgid, pid_t fg_pgid);
	} thread;
} classfuncs_t;

#define	CL_ADMIN(clp, uaddr, reqpcredp) \
	(*(clp)->cl_funcs->sclass.cl_admin)(uaddr, reqpcredp)

#define	CL_ENTERCLASS(t, cid, clparmsp, credp, bufp) \
	(sclass[cid].cl_funcs->thread.cl_enterclass) (t, cid, clparmsp, \
	    credp, bufp)

#define	CL_EXITCLASS(cid, clprocp)\
	(sclass[cid].cl_funcs->thread.cl_exitclass) (clprocp)

#define	CL_FORK(tp, ctp, bufp) \
	(*(tp)->t_clfuncs->cl_fork) (tp, ctp, bufp)

#define	CL_FORKRET(t, ct) (*(t)->t_clfuncs->cl_forkret)(t, ct)

#define	CL_GETCLINFO(clp, clinfop) \
	(*(clp)->cl_funcs->sclass.cl_getclinfo)(clinfop)

#define	CL_PARMSGET(t, clparmsp) \
	(*(t)->t_clfuncs->cl_parmsget)(t->t_cldata, clparmsp)

#define	CL_PARMSIN(clp, clparmsp, curpcid, curpcredp, tpcid, tpcredp, tpclpp) \
	(clp)->cl_funcs->sclass.cl_parmsin(clparmsp, curpcid, curpcredp, \
	    tpcid, tpcredp, tpclpp)

#define	CL_PARMSOUT(clp, clparmsp, curpcid, curpcredp, tpcid, tpcredp, tpclpp) \
	(clp)->cl_funcs->sclass.cl_parmsout(clparmsp, curpcid, curpcredp, \
	    tpcid, tpcredp, tpclpp)

#define	CL_PARMSSET(t, clparmsp, cid, curpcredp) \
	(*(t)->t_clfuncs->cl_parmsset)(clparmsp, t->t_cldata, cid, curpcredp)

#define	CL_PREEMPT(tp) (*(tp)->t_clfuncs->cl_preempt)(tp)

#define	CL_SETRUN(tp) (*(tp)->t_clfuncs->cl_setrun)(tp)

#define	CL_SLEEP(tp, disp) \
	(*(tp)->t_clfuncs->cl_sleep)(tp, disp)

#define	CL_STOP(t, why, what) \
	(*(t)->t_clfuncs->cl_stop)(t, why, what)

#define	CL_SWAPIN(t, flags) \
	(*(t)->t_clfuncs->cl_swapin)(t, flags)

#define	CL_SWAPOUT(t, flags) \
	(*(t)->t_clfuncs->cl_swapout)(t, flags)

#define	CL_TICK(t) (*(t)->t_clfuncs->cl_tick)(t)

#define	CL_TRAPRET(t) (*(t)->t_clfuncs->cl_trapret)(t)

#define	CL_WAKEUP(t) (*(t)->t_clfuncs->cl_wakeup)(t)

#define	CL_DONICE(t, cr, inc, ret) (*(t)->t_clfuncs->cl_donice)(t, cr, inc, ret)

#define	CL_CPU_CHOOSE(t, pri) (*(t)->t_clfuncs->cl_cpu_choose) (t, pri)

#define	CL_GLOBPRI(t) (*(t)->t_clfuncs->cl_globpri)(t->t_cldata)

#define	CL_SET_PROCESS_GROUP(t, s, b, f) \
	(*(t)->t_clfuncs->cl_set_process_group)(s, b, f)

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_CLASS_H */
