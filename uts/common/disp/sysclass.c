/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)sysclass.c	1.35	95/09/11 SMI"	/* from SVr4.0 1.12 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/signal.h>
#include <sys/pcb.h>
#include <sys/user.h>
#include <sys/systm.h>
#include <sys/sysinfo.h>
#include <sys/var.h>
#include <sys/errno.h>
#include <sys/cmn_err.h>
#include <sys/proc.h>
#include <sys/debug.h>
#include <sys/inline.h>
#include <sys/disp.h>
#include <sys/class.h>
#include <sys/kmem.h>
#include <sys/cpuvar.h>

/*
 * Class specific code for the sys class. There are no
 * class specific data structures associated with
 * the sys class and the scheduling policy is trivially
 * simple. There is no time slicing.
 */

void		sys_init();
static void	sys_set_process_group(pid_t sid, pid_t bg_pgid, pid_t fg_pgid);
static int	sys_fork(), sys_enterclass(), sys_nosys(), sys_donice();
static void	sys_forkret(), sys_nullsys();
static pri_t	sys_swappri(kthread_id_t, int);
static cpu_t 	*sys_cpu_choose(kthread_id_t t, pri_t tpri);

struct classfuncs sys_classfuncs = {
	/* messages to class manager */
	{
		sys_nosys,	/* admin */
		sys_nosys,	/* getclinfo */
		sys_nosys,	/* parmsin */
		sys_nosys,	/* parmsout */
	},
	/* operations on threads */
	{
		sys_enterclass,	/* enterclass */
		sys_nullsys,	/* exitclass */
		sys_fork,
		sys_forkret,	/* forkret */
		sys_nullsys,	/* parmsget */
		sys_nosys,	/* parmsset */
		sys_nullsys,	/* stop */
		sys_swappri,	/* swapin */
		sys_swappri,	/* swapout */
		sys_nullsys,	/* trapret */
#ifdef KSLICE
		sys_preempt,
#else
		setfrontdq,
#endif
		setbackdq,	/* setrun */
		sys_nullsys,	/* sleep */
		sys_nullsys,	/* tick */
		setbackdq,	/* wakeup */
		sys_donice,
		sys_cpu_choose,
		(pri_t (*)())sys_nosys,	/* globpri */
		sys_set_process_group,
	}

};


/* ARGSUSED */
void
sys_init(cid, clparmsz, clfuncspp, maxglobprip, clprocsz)
	id_t		cid;
	int		clparmsz;
	classfuncs_t	**clfuncspp;
	pri_t		*maxglobprip;
	size_t		*clprocsz;
{
	*clfuncspp = &sys_classfuncs;

	if (v.v_maxsyspri < PSLEP) {
		cmn_err(CE_WARN,
		    "Max system class priority must be >= %d, "
		    "configured value is %d\n- resetting v.v_maxsyspri to %d\n",
		    PSLEP, v.v_maxsyspri, PSLEP);
		v.v_maxsyspri = PSLEP;
	}
	*maxglobprip = (pri_t)v.v_maxsyspri;
	*clprocsz = 0;
}

static int
sys_enterclass()
{
	return (0);
}

/* ARGSUSED */
static int
sys_fork(t, ct, bufp)
	kthread_id_t t;
	kthread_id_t ct;
	void	*bufp;
{
	/*
	 * No class specific data structure
	 */
	return (0);
}


/* ARGSUSED */
static void
sys_forkret(t, ct)
	kthread_id_t t;
	kthread_id_t ct;
{
	kthread_id_t last;
	register proc_t *pp = ttoproc(t);
	register proc_t *cp = ttoproc(ct);

	ASSERT(t == curthread);
	ASSERT(MUTEX_HELD(&pidlock));

	/*
	 * Grab the child's p_lock before dropping pidlock to ensure
	 * the process does not disappear before we set it running.
	 */
	mutex_enter(&cp->p_lock);
	mutex_exit(&pidlock);
	continuelwps(cp);
	mutex_exit(&cp->p_lock);

	mutex_enter(&pp->p_lock);
	pp->p_flag &= ~(HOLDLWPS|HOLDLWP2);
	last = t;
	while ((t = t->t_forw) != last) {
		if ((t->t_proc_flag & TP_HOLDLWP) == 0)
			lwp_continue(t);
	}
	mutex_exit(&pp->p_lock);
}

/* ARGSUSED */
static pri_t
sys_swappri(t, flags)
	kthread_id_t	t;
	int		flags;
{
	return (-1);
}

static int
sys_nosys()
{
	return (ENOSYS);
}


static void
sys_nullsys()
{
}

#ifdef KSLICE
static void
sys_preempt(t)
	kthread_id_t	t;
{
	extern int	kslice;

	if (kslice)
		setbackdq(t);
	else
		setfrontdq(t);
}
#endif


/* ARGSUSED */
static int
sys_donice(t, cr, incr, retvalp)
	kthread_id_t	t;
	cred_t		*cr;
	int		incr;
	int		*retvalp;
{
	return (EINVAL);
}

/*
 * Parameter which determines how recently a thread must have run
 * on the CPU to be considered loosely-bound to that CPU to reduce
 * cold cache effects.  The interval is in hertz.
 */
#define	SYS_RECHOOSE_INTERVAL	3

/*
 * Select a CPU for this thread to run on.
 */
/* ARGSUSED */
static cpu_t *
sys_cpu_choose(kthread_id_t t, pri_t tpri)
{
	cpu_t   *bestcpu;

	/*
	 * Only search for best CPU if the thread is at a high priority
	 * (from inheritance) or hasn't run for awhile.
	 */
	bestcpu = t->t_cpu;		/* start with last CPU used */
	if (tpri >= kpreemptpri ||
	    ((lbolt - t->t_disp_time) > SYS_RECHOOSE_INTERVAL &&
	    t != curthread))
		bestcpu = disp_lowpri_cpu(bestcpu->cpu_next_onln);
	return (bestcpu);
}

/* ARGSUSED */
static void
sys_set_process_group(pid_t sid, pid_t bg_pgid, pid_t fg_pgid)
{
	/* nop function */
}
