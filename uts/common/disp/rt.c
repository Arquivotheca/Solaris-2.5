/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma	ident	"@(#)rt.c	1.52	95/09/11	SMI"

#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/cred.h>
#include <sys/proc.h>
#include <sys/pcb.h>
#include <sys/signal.h>
#include <sys/user.h>
#include <sys/priocntl.h>
#include <sys/class.h>
#include <sys/disp.h>
#include <sys/procset.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/rt.h>
#include <sys/rtpriocntl.h>
#include <sys/kmem.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/cpuvar.h>
#include <sys/vmsystm.h>

/*
 * This is the loadable module wrapper.
 */
#include <sys/modctl.h>

extern void rt_init();

static struct sclass csw = {
	"RT",
	rt_init,
	0
};

extern struct mod_ops mod_schedops;

/*
 * Module linkage information for the kernel.
 */
static struct modlsched modlsched = {
	&mod_schedops, "realtime scheduling class", &csw
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modlsched, NULL
};

static int module_keepcnt = 0;	/* ==0 means the module is unloadable */

_init()
{
	return (mod_install(&modlinkage));
}

_fini()
{
	return (EBUSY);		/* don't remove RT for now */
}

_info(modinfop)
	struct modinfo *modinfop;
{
	return (mod_info(&modlinkage, modinfop));
}


/*
 * Class specific code for the real-time class
 */

/*
 * Extern declarations for variables defined in the rt master file
 */
#define	RTMAXPRI 59
#define	RTNPROCS 60

rtdpent_t *rt_dptbl;	  /* real-time dispatcher parameter table */

pri_t rt_maxpri = RTMAXPRI;	/* maximum real-time priority */
int rt_nprocs = RTNPROCS;

#define	RTPMEMSZ	1024	/* request size for rtproc memory allocation */
#define	NANOSEC		1000000000

void		rt_init(id_t, int, classfuncs_t **, pri_t *, size_t *);
static int	rt_admin(), rt_enterclass(), rt_fork(), rt_getclinfo();
static int	rt_parmsin(), rt_parmsout(), rt_parmsset();
static int	rt_donice();
static void	rt_exitclass(), rt_forkret(), rt_nullsys();
static void	rt_parmsget(), rt_preempt(), rt_setrun(), rt_sleep();
static void	rt_stop(), rt_tick(), rt_wakeup();
static pri_t	rt_swappri(kthread_id_t, int);
static cpu_t	*rt_cpu_choose(kthread_id_t t, pri_t tpri);
static pri_t	rt_globpri(rtproc_t *rtprocp);
static void	rt_set_process_group(pid_t sid, pid_t bg_pgid, pid_t fg_pgid);


static id_t	rt_cid;		/* real-time class ID */
static rtproc_t	rt_plisthead;	/* dummy rtproc at head of rtproc list */
static int	rt_nanoperhz;
static kmutex_t	rt_dptblock;	/* protects realtime dispatch table */
static kmutex_t	rt_list_lock;	/* protects RT thread list */

extern rtdpent_t *rt_getdptbl();

static struct classfuncs rt_classfuncs = {
	/* class ops */
	rt_admin,
	rt_getclinfo,
	rt_parmsin,
	rt_parmsout,
	/* thread ops */
	rt_enterclass,
	rt_exitclass,
	rt_fork,
	rt_forkret,
	rt_parmsget,
	rt_parmsset,
	rt_stop,
	rt_swappri,
	rt_swappri,
	rt_nullsys,
	rt_preempt,
	rt_setrun,
	rt_sleep,
	rt_tick,
	rt_wakeup,
	rt_donice,
	rt_cpu_choose,
	rt_globpri,
	rt_set_process_group,
};

/*
 * Real-time class initialization. Called by dispinit() at boot time.
 * We can ignore the clparmsz argument since we know that the smallest
 * possible parameter buffer is big enough for us.
 */
/* ARGSUSED */
void
rt_init(cid, clparmsz, clfuncspp, maxglobprip, clprocsz)
id_t		cid;
int		clparmsz;
classfuncs_t	**clfuncspp;
pri_t		*maxglobprip;
size_t		*clprocsz;
{

	rt_dptbl = rt_getdptbl();
	rt_cid = cid;	/* Record our class ID */

	/*
	 * Initialize the rtproc list.
	 */
	rt_plisthead.rt_next = rt_plisthead.rt_prev = &rt_plisthead;

	/*
	 * compute conversion from nanoseconds to ticks
	 */
	rt_nanoperhz = NANOSEC / hz;

	/*
	 * We're required to return a pointer to our classfuncs
	 * structure and the highest global priority value we use.
	 */
	*clfuncspp = &rt_classfuncs;
	*maxglobprip = rt_dptbl[rt_maxpri].rt_globpri;
	*clprocsz = sizeof (rtproc_t);
	mutex_init(&rt_dptblock, "rt dispatch tbl lock", MUTEX_DEFAULT, NULL);
	mutex_init(&rt_list_lock, "rt list lock", MUTEX_DEFAULT, NULL);
}

/*
 * Get or reset the rt_dptbl values per the user's request.
 */
/* ARGSUSED */
static int
rt_admin(uaddr, reqpcredp)
caddr_t	uaddr;
cred_t	*reqpcredp;
{
	rtadmin_t		rtadmin;
	register rtdpent_t	*tmpdpp;
	register int		userdpsz;
	register int		i;
	register int		rtdpsz;

	if (copyin(uaddr, (caddr_t)&rtadmin, sizeof (rtadmin_t)))
		return (EFAULT);

	rtdpsz = (rt_maxpri + 1) * sizeof (rtdpent_t);

	switch (rtadmin.rt_cmd) {

	case RT_GETDPSIZE:

		rtadmin.rt_ndpents = rt_maxpri + 1;
		if (copyout((caddr_t)&rtadmin, uaddr, sizeof (rtadmin_t)))
			return (EFAULT);
		break;

	case RT_GETDPTBL:

		userdpsz = min(rtadmin.rt_ndpents * sizeof (rtdpent_t),
		    rtdpsz);
		if (copyout((caddr_t)rt_dptbl,
		    (caddr_t)rtadmin.rt_dpents, userdpsz)) {
			return (EFAULT);
		}
		rtadmin.rt_ndpents = userdpsz / sizeof (rtdpent_t);
		if (copyout((caddr_t)&rtadmin, uaddr, sizeof (rtadmin_t))) {
			return (EFAULT);
		}
		break;

	case RT_SETDPTBL:

		/*
		 * We require that the requesting process have super user
		 * priveleges.  We also require that the table supplied by
		 * the user exactly match the current rt_dptbl in size.
		 */
		if (!suser(reqpcredp))
			return (EPERM);
		if (rtadmin.rt_ndpents * sizeof (rtdpent_t) != rtdpsz)
			return (EINVAL);

		/*
		 * We read the user supplied table into a temporary buffer
		 * where the time quantum values are validated before
		 * being copied to the rt_dptbl.
		 */
		tmpdpp = kmem_alloc(rtdpsz, KM_SLEEP);
		if (copyin((caddr_t)rtadmin.rt_dpents,
		    (caddr_t)tmpdpp, rtdpsz)) {
			kmem_free(tmpdpp, rtdpsz);
			return (EFAULT);
		}
		for (i = 0; i < rtadmin.rt_ndpents; i++) {

			/*
			 * Validate the user supplied time quantum values.
			 */
			if (tmpdpp[i].rt_quantum <= 0 &&
			    tmpdpp[i].rt_quantum != RT_TQINF) {
				kmem_free(tmpdpp, rtdpsz);
				return (EINVAL);
			}
		}

		/*
		 * Copy the user supplied values over the current rt_dptbl
		 * values.  The rt_globpri member is read-only so we don't
		 * overwrite it.
		 */
		mutex_enter(&rt_dptblock);
		for (i = 0; i < rtadmin.rt_ndpents; i++)
			rt_dptbl[i].rt_quantum = tmpdpp[i].rt_quantum;
		mutex_exit(&rt_dptblock);
		kmem_free(tmpdpp, rtdpsz);
		break;

	default:
		return (EINVAL);
	}
	return (0);
}


/*
 * Allocate a real-time class specific proc structure and
 * initialize it with the parameters supplied. Also move thread
 * to specified real-time priority.
 */
/* ARGSUSED */
static int
rt_enterclass(t, cid, rtkparmsp, reqpcredp, bufp)
	kthread_id_t		t;
	id_t			cid;
	register rtkparms_t	*rtkparmsp;
	cred_t			*reqpcredp;
	void			*bufp;
{
	register rtproc_t	*rtpp;
	register boolean_t	wasonq;

	/*
	 * For a thread to enter the real-time class the thread
	 * which initiates the request must be super-user.
	 * This may have been checked previously but if our
	 * caller passed us a credential structure we assume it
	 * hasn't and we check it here.
	 */
	if (reqpcredp != NULL && !suser(reqpcredp))
		return (EPERM);

	rtpp = (rtproc_t *)bufp;
	ASSERT(rtpp != NULL);

	module_keepcnt++;

	/*
	 * If this thread's lwp is swapped out, it will be brought in
	 * when it is put onto the runqueue.
	 *
	 * Now, Initialize the rtproc structure.
	 */
	if (rtkparmsp == NULL) {
		/*
		 * Use default values
		 */
		rtpp->rt_pri = 0;
		rtpp->rt_pquantum = rt_dptbl[0].rt_quantum;
	} else {
		/*
		 * Use supplied values
		 */
		if (rtkparmsp->rt_pri == RT_NOCHANGE) {
			rtpp->rt_pri = 0;
		} else {
			rtpp->rt_pri = rtkparmsp->rt_pri;
		}
		if (rtkparmsp->rt_tqntm == RT_TQINF)
			rtpp->rt_pquantum = RT_TQINF;
		else if (rtkparmsp->rt_tqntm == RT_TQDEF ||
		    rtkparmsp->rt_tqntm == RT_NOCHANGE)
			rtpp->rt_pquantum = rt_dptbl[rtpp->rt_pri].rt_quantum;
		else
			rtpp->rt_pquantum = rtkparmsp->rt_tqntm;
	}
	rtpp->rt_flags = 0;
	rtpp->rt_tp = t;
	/*
	 * Reset thread priority
	 */
	thread_lock(t);
	t->t_clfuncs = &(sclass[cid].cl_funcs->thread);
	t->t_cid = cid;
	t->t_cldata = (void *)rtpp;
	if (t == curthread) {
		t->t_pri = rt_dptbl[rtpp->rt_pri].rt_globpri;
		if (DISP_PRIO(t) > DISP_MAXRUNPRI(t)) {
			rtpp->rt_timeleft = rtpp->rt_pquantum;
		} else {
			rtpp->rt_flags |= RTBACKQ;
			cpu_surrender(t);
		}
	} else {
		wasonq = dispdeq(t);
		t->t_pri = rt_dptbl[rtpp->rt_pri].rt_globpri;
		if (wasonq == B_TRUE) {
			rtpp->rt_timeleft = rtpp->rt_pquantum;
			setbackdq(t);
		} else {
			rtpp->rt_flags |= RTBACKQ;
			if (t->t_state == TS_ONPROC)
				cpu_surrender(t);
		}
	}
	thread_unlock(t);
	/*
	 * Link new structure into rtproc list
	 */
	mutex_enter(&rt_list_lock);
	rtpp->rt_next = rt_plisthead.rt_next;
	rtpp->rt_prev = &rt_plisthead;
	rt_plisthead.rt_next->rt_prev = rtpp;
	rt_plisthead.rt_next = rtpp;
	mutex_exit(&rt_list_lock);
	return (0);
}


/*
 * Free rtproc structure of thread.
 */
static void
rt_exitclass(rtprocp)
	rtproc_t *rtprocp;
{
	mutex_enter(&rt_list_lock);
	rtprocp->rt_prev->rt_next = rtprocp->rt_next;
	rtprocp->rt_next->rt_prev = rtprocp->rt_prev;
	mutex_exit(&rt_list_lock);
	kmem_free((caddr_t)rtprocp, sizeof (rtproc_t));
	module_keepcnt--;
}


/*
 * Allocate and initialize real-time class specific
 * proc structure for child.
 */
/* ARGSUSED */
static int
rt_fork(t, ct, bufp)
	kthread_id_t t, ct;
	void	*bufp;
{
	rtproc_t	*prtpp;
	rtproc_t	*crtpp;

	module_keepcnt++;

	ASSERT(MUTEX_HELD(&ttoproc(t)->p_lock));

	/*
	 * Initialize child's rtproc structure
	 */
	crtpp = (rtproc_t *)bufp;
	ASSERT(crtpp != NULL);
	prtpp = (rtproc_t *)t->t_cldata;
	thread_lock(t);
	crtpp->rt_timeleft = crtpp->rt_pquantum = prtpp->rt_pquantum;
	crtpp->rt_pri = prtpp->rt_pri;
	crtpp->rt_flags = prtpp->rt_flags & ~RTBACKQ;
	crtpp->rt_tp = ct;
	thread_unlock(t);

	/*
	 * Link new structure into rtproc list
	 */
	ct->t_cldata = (void *)crtpp;
	mutex_enter(&rt_list_lock);
	crtpp->rt_next = rt_plisthead.rt_next;
	crtpp->rt_prev = &rt_plisthead;
	rt_plisthead.rt_next->rt_prev = crtpp;
	rt_plisthead.rt_next = crtpp;
	mutex_exit(&rt_list_lock);
	return (0);
}


/*
 * The child goes to the back of its dispatcher queue while the
 * parent continues to run after a real time thread forks.
 */
/* ARGSUSED */
static void
rt_forkret(t, ct)
	kthread_id_t t;
	kthread_id_t ct;
{
	kthread_id_t		last;
	register proc_t		*pp = ttoproc(t);
	register proc_t		*cp = ttoproc(ct);

	ASSERT(t == curthread);
	ASSERT(MUTEX_HELD(&pidlock));

	/*
	 * Grab the child's p_lock before dropping pidlock to ensure
	 * the process does not disappear before we set it running.
	 */
	mutex_enter(&cp->p_lock);
	mutex_exit(&pidlock);
	cp->p_flag &= ~(HOLDLWPS|HOLDLWP2);
	last = ct;
	do {
		if ((ct->t_proc_flag & TP_HOLDLWP) == 0)
			lwp_continue(ct);
	} while ((ct = ct->t_forw) != last);
	mutex_exit(&cp->p_lock);

	mutex_enter(&pp->p_lock);
	pp->p_flag &= ~(HOLDLWPS|HOLDLWP2);
	last = t;
	t = t->t_forw;
	do {
		if ((t->t_proc_flag & TP_HOLDLWP) == 0)
			lwp_continue(t);
	} while ((t = t->t_forw) != last);
	mutex_exit(&pp->p_lock);
}


/*
 * Get information about the real-time class into the buffer
 * pointed to by rtinfop.  The maximum configured real-time
 * priority is the only information we supply.  We ignore the
 * class and credential arguments because anyone can have this
 * information.
 */
/* ARGSUSED */
static int
rt_getclinfo(rtinfop)
rtinfo_t	*rtinfop;
{
	rtinfop->rt_maxpri = rt_maxpri;
	return (0);
}

static void
rt_nullsys()
{
}


/*
 * Get the real-time scheduling parameters of the thread pointed to by
 * rtprocp into the buffer pointed to by rtkparmsp.
 */
static void
rt_parmsget(rtprocp, rtkparmsp)
rtproc_t	*rtprocp;
rtkparms_t	*rtkparmsp;
{
	rtkparmsp->rt_pri = rtprocp->rt_pri;
	rtkparmsp->rt_tqntm = rtprocp->rt_pquantum;
}



/*
 * Check the validity of the real-time parameters in the buffer
 * pointed to by rtprmsp.  If our caller passes us a non-NULL
 * reqpcredp pointer we also verify that the requesting thread
 * (whose class and credentials are indicated by reqpcid and reqpcredp)
 * has the necessary permissions to set these parameters for a
 * target thread with class targpcid. We also convert the
 * rtparms buffer from the user supplied format to our internal
 * format (i.e. time quantum expressed in ticks).
 */
/* ARGSUSED */
static int
rt_parmsin(rtprmsp, reqpcid, reqpcredp, targpcid, targpcredp, rtpp)
register rtparms_t	*rtprmsp;
id_t			reqpcid;
cred_t			*reqpcredp;
id_t			targpcid;
cred_t			*targpcredp;
rtproc_t		*rtpp;
{
	/*
	 * First check the validity of parameters and convert
	 * the buffer to kernel format.
	 */
	if ((rtprmsp->rt_pri < 0 || rtprmsp->rt_pri > rt_maxpri) &&
	    rtprmsp->rt_pri != RT_NOCHANGE)
		return (EINVAL);

	if ((rtprmsp->rt_tqsecs == 0 && rtprmsp->rt_tqnsecs == 0) ||
	    rtprmsp->rt_tqnsecs >= NANOSEC)
		return (EINVAL);

	if (rtprmsp->rt_tqnsecs >= 0) {
		((rtkparms_t *)rtprmsp)->rt_tqntm = rtprmsp->rt_tqsecs * hz +
			(rtprmsp->rt_tqnsecs + rt_nanoperhz - 1) / rt_nanoperhz;
	} else {
		if (rtprmsp->rt_tqnsecs != RT_NOCHANGE &&
		    rtprmsp->rt_tqnsecs != RT_TQINF &&
		    rtprmsp->rt_tqnsecs != RT_TQDEF)
			return (EINVAL);
		((rtkparms_t *)rtprmsp)->rt_tqntm = rtprmsp->rt_tqnsecs;
	}

	/*
	 * If our caller passed us non-NULL cred pointers
	 * we are being asked to check permissions as well
	 * as the validity of the parameters. In order to
	 * set any parameters the real-time class requires
	 * that the requesting thread be real-time or
	 * super-user.  If the target thread is currently in
	 * a class other than real-time the requesting thread
	 * must be super-user.
	 */
	if (reqpcredp != NULL) {
		if (targpcid == rt_cid) {
			if (reqpcid != rt_cid && !suser(reqpcredp))
				return (EPERM);
		} else {  /* target thread is not real-time */
			if (!suser(reqpcredp))
				return (EPERM);
		}
	}

	return (0);
}

/*
 * Do required processing on the real-time parameter buffer
 * before it is copied out to the user. We ignore the class
 * and credential arguments passed by our caller because we
 * don't require any special permissions to read real-time
 * scheduling parameters.  All we have to do is convert the
 * buffer from kernel to user format (i.e. convert time quantum
 * from ticks to seconds-nanoseconds).
 */
/* ARGSUSED */
static int
rt_parmsout(rtkprmsp, reqpcid, reqpcredp, targpcredp)
register rtkparms_t	*rtkprmsp;
id_t			reqpcid;
cred_t			*reqpcredp;
cred_t			*targpcredp;
{
	register int		rtsecs;

	if (rtkprmsp->rt_tqntm < 0) {
		/*
		 * Quantum field set to special value (e.g. RT_TQINF)
		 */
		((rtparms_t *)rtkprmsp)->rt_tqnsecs = rtkprmsp->rt_tqntm;
		((rtparms_t *)rtkprmsp)->rt_tqsecs = 0;
	} else {
		/* Convert quantum from ticks to seconds-nanoseconds */

		rtsecs = rtkprmsp->rt_tqntm;
		((rtparms_t *)rtkprmsp)->rt_tqsecs = rtsecs / hz;
		((rtparms_t *)rtkprmsp)->rt_tqnsecs = (rtsecs -
			hz * ((rtparms_t *)rtkprmsp)->rt_tqsecs) *
			(NANOSEC / hz);
	}

	return (0);
}


/*
 * Set the scheduling parameters of the thread pointed to by rtprocp
 * to those specified in the buffer pointed to by rtkprmsp.
 * Note that the parameters are expected to be in kernel format
 * (i.e. time quantm expressed in ticks).  Real time parameters copied
 * in from the user should be processed by rt_parmsin() before they are
 * passed to this function.
 */
static int
rt_parmsset(rtkprmsp, rtpp, reqpcid, reqpcredp)
register rtkparms_t	*rtkprmsp;
register rtproc_t	*rtpp;
id_t			reqpcid;
cred_t			*reqpcredp;
{
	kthread_id_t		tx;
	register boolean_t	wasonq;

	ASSERT(MUTEX_HELD(&(ttoproc(rtpp->rt_tp))->p_lock));

	/*
	 * Basic permissions enforced by generic kernel code
	 * for all classes require that a thread attempting
	 * to change the scheduling parameters of a target thread
	 * be super-user or have a real or effective UID
	 * matching that of the target thread. We are not
	 * called unless these basic permission checks have
	 * already passed. The real-time class requires in addition
	 * that the requesting thread be real-time unless it is super-user.
	 * This may also have been checked previously but if our caller
	 * passes us a credential structure we assume it hasn't and
	 * we check it here.
	 */
	if (reqpcredp != NULL && reqpcid != rt_cid && !suser(reqpcredp))
		return (EPERM);

	tx = rtpp->rt_tp;
	thread_lock(tx);
	if (rtkprmsp->rt_pri != RT_NOCHANGE) {
		rtpp->rt_pri = rtkprmsp->rt_pri;
		if (rtpp->rt_tp == curthread) {
			tx->t_pri = rt_dptbl[rtpp->rt_pri].rt_globpri;
			if (DISP_PRIO(curthread) <= DISP_MAXRUNPRI(curthread)) {
				rtpp->rt_flags |= RTBACKQ;
				cpu_surrender(curthread);
			}
		} else {
			wasonq = dispdeq(tx);
			tx->t_pri = rt_dptbl[rtpp->rt_pri].rt_globpri;
			if (wasonq == B_TRUE) {
				setbackdq(tx);
			} else {
				rtpp->rt_flags |= RTBACKQ;
				if (tx->t_state == TS_ONPROC)
					cpu_surrender(tx);
			}
		}
	}
	if (rtkprmsp->rt_tqntm == RT_TQINF)
		rtpp->rt_pquantum = RT_TQINF;
	else if (rtkprmsp->rt_tqntm == RT_TQDEF)
		rtpp->rt_timeleft = rtpp->rt_pquantum =
		    rt_dptbl[rtpp->rt_pri].rt_quantum;
	else if (rtkprmsp->rt_tqntm != RT_NOCHANGE)
		rtpp->rt_timeleft = rtpp->rt_pquantum = rtkprmsp->rt_tqntm;
	thread_unlock(tx);
	return (0);
}


/*
 * Arrange for thread to be placed in appropriate location
 * on dispatcher queue.  Runs at splhi() since the clock
 * interrupt can cause RTBACKQ to be set.
 */
static void
rt_preempt(tid)
kthread_id_t	tid;
{
	rtproc_t	*rtpp = (rtproc_t *)(tid->t_cldata);
	register klwp_t *lwp;

	ASSERT(THREAD_LOCK_HELD(tid));

	/*
	 * If the state is user I allow swapping because I know I won't
	 * be holding any locks.
	 */
	if ((lwp = curthread->t_lwp) != NULL && lwp->lwp_state == LWP_USER)
		tid->t_schedflag &= ~TS_DONT_SWAP;
	if ((rtpp->rt_flags & RTBACKQ) != 0) {
		rtpp->rt_timeleft = rtpp->rt_pquantum;
		rtpp->rt_flags &= ~RTBACKQ;
		setbackdq(tid);
	} else
		setfrontdq(tid);

}

/*
 * Return the global priority associated with this rt_pri.
 */
static pri_t
rt_globpri(rtproc_t *rtprocp)
{
	return (rt_dptbl[rtprocp->rt_pri].rt_globpri);
}

static void
rt_setrun(tid)
kthread_id_t	tid;
{
	rtproc_t	*rtpp = (rtproc_t *)(tid->t_cldata);

	ASSERT(THREAD_LOCK_HELD(tid));

	rtpp->rt_timeleft = rtpp->rt_pquantum;
	rtpp->rt_flags &= ~RTBACKQ;
	setbackdq(tid);
}


/* ARGSUSED */
static void
rt_sleep(tid, disp)
kthread_id_t	tid;
short		disp;
{
}


/* ARGSUSED */
static void
rt_stop(tid, why, what)
kthread_id_t	tid;
int		why;
int		what;
{
}

/*
 * Return an effective priority for swapin/swapout.
 */
/* ARGSUSED */
static pri_t
rt_swappri(t, flags)
	kthread_id_t	t;
	int		flags;
{
	ASSERT(THREAD_LOCK_HELD(t));

	return (-1);
}

/*
 * Check for time slice expiration (unless thread has infinite time
 * slice).  If time slice has expired arrange for thread to be preempted
 * and placed on back of queue.
 */
static void
rt_tick(tid)
kthread_id_t	tid;
{
	register rtproc_t *rtpp = (rtproc_t *)(tid->t_cldata);

	ASSERT(MUTEX_HELD(&(ttoproc(tid))->p_lock));

	thread_lock(tid);
	if (rtpp->rt_pquantum != RT_TQINF && --rtpp->rt_timeleft == 0) {
		rtpp->rt_flags |= RTBACKQ;
		cpu_surrender(tid);
	}
	thread_unlock(tid);
}


/*
 * Place the thread waking up on the dispatcher queue.
 */
static void
rt_wakeup(tid)
kthread_id_t	tid;
{
	rtproc_t	*rtpp = (rtproc_t *)(tid->t_cldata);

	ASSERT(THREAD_LOCK_HELD(tid));

	rtpp->rt_timeleft = rtpp->rt_pquantum;
	rtpp->rt_flags &= ~RTBACKQ;
	setbackdq(tid);
}

/* ARGSUSED */
static int
rt_donice(t, cr, incr, retvalp)
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
#define	RT_RECHOOSE_INTERVAL	4

/*
 * Select a CPU for this thread to run on.  Note, loose affinity has been
 * implemented for real time.  However, by default kpreemptpri is equal to
 * the minimum real time priority so loose affinity has no effect unless
 * kpreemptpri is set to a higher value.
 */
static cpu_t *
rt_cpu_choose(kthread_id_t t, pri_t tpri)
{
	if (tpri >= kpreemptpri ||
	    ((lbolt - t->t_disp_time) > RT_RECHOOSE_INTERVAL && t != curthread))
		return (disp_lowpri_cpu(t->t_cpu));
	return (t->t_cpu);
}

/* ARGSUSED */
static void
rt_set_process_group(pid_t sid, pid_t bg_pgid,
    pid_t fg_pgid)
{
	/* nop function */
}
