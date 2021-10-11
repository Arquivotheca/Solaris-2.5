/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)prsubr.c	1.76	95/09/11 SMI"	/* from SVr4.0 1.44 */

#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/param.h>
#include <sys/cmn_err.h>
#include <sys/cred.h>
#include <sys/debug.h>
#include <sys/errno.h>
#include <sys/inline.h>
#include <sys/kmem.h>
#include <sys/mman.h>
#include <sys/proc.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/var.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/session.h>
#include <sys/tblock.h>

#include <sys/pcb.h>
#include <sys/signal.h>
#include <sys/user.h>
#include <sys/disp.h>
#include <sys/class.h>
#include <sys/ts.h>
#include <sys/bitmap.h>
#include <sys/poll.h>

#include <sys/fault.h>
#include <sys/syscall.h>
#include <sys/procfs.h>
#include <sys/cpuvar.h>

#include <vm/as.h>
#include <vm/rm.h>
#include <vm/seg.h>
#include <sys/vmparam.h>

#include <fs/proc/prdata.h>

struct prnode prrootnode;

extern u_int timer_resolution;

kmutex_t pr_mount_lock;

int
prinit(vswp, fstype)
	register struct vfssw *vswp;
	int fstype;
{
	register int dev;

	procfstype = fstype;
	ASSERT(procfstype != 0);
	/*
	 * Associate VFS ops vector with this fstype.
	 */
	vswp->vsw_vfsops = &prvfsops;

	/*
	 * Assign a unique "device" number (used by stat(2)).
	 */
	if ((dev = getudev()) == -1) {
		cmn_err(CE_WARN, "prinit: can't get unique device number");
		dev = 0;
	}
	procdev = makedevice(dev, 0);
	prmounted = 0;
	mutex_init(&prrootnode.pr_vnode.v_lock, "procfs v_lock",
				MUTEX_DEFAULT, DEFAULT_WT);
	mutex_init(&pr_mount_lock, "procfs mount lock",
				MUTEX_DEFAULT, DEFAULT_WT);

	return (0);
}


/*
 * Choose an lwp from the complete set of lwps for the process.
 * This is called for any operation applied to the process
 * file descriptor that requires an lwp to operate upon.
 *
 * Returns a pointer to the thread for the selected LWP,
 * and with the dispatcher lock held for the thread.
 *
 * The algorithm for choosing an lwp is critical for /proc semantics;
 * don't touch this code unless you know all of the implications.
 */
kthread_t *
prchoose(p)
	register proc_t *p;
{
	register kthread_t *t;
	kthread_t *t_onproc = NULL;	/* running on processor */
	kthread_t *t_run = NULL;	/* runnable, on disp queue */
	kthread_t *t_sleep = NULL;	/* sleeping */
	kthread_t *t_hold = NULL;	/* sleeping, performing hold */
	kthread_t *t_susp = NULL;	/* suspended stop */
	kthread_t *t_jstop = NULL;	/* jobcontrol stop */
	kthread_t *t_req = NULL;	/* requested stop */
	kthread_t *t_istop = NULL;	/* event-of-interest stop */

	ASSERT(MUTEX_HELD(&p->p_lock));

	if ((t = p->p_tlist) == NULL)
		return (t);
	do {
		if (VSTOPPED(t)) {	/* virtually stopped */
			if (t_req == NULL)
				t_req = t;
			continue;
		}

		thread_lock(t);		/* make sure thread is in good state */
		switch (t->t_state) {
		default:
			cmn_err(CE_PANIC,
			    "prchoose: bad thread state %d, thread 0x%x\n",
			    t->t_state, (int)t);

			break;
		case TS_SLEEP:
			/* this is filthy */
			if (t->t_wchan == (caddr_t)&p->p_holdlwps &&
			    t->t_wchan0 == 0) {
				if (t_hold == NULL)
					t_hold = t;
			} else {
				if (t_sleep == NULL)
					t_sleep = t;
			}
			break;
		case TS_RUN:
			if (t_run == NULL)
				t_run = t;
			break;
		case TS_ONPROC:
			if (t_onproc == NULL)
				t_onproc = t;
			break;
		case TS_ZOMB:		/* last possible choice */
			break;
		case TS_STOPPED:
			switch (t->t_whystop) {
			case PR_SUSPENDED:
				if (t_susp == NULL)
					t_susp = t;
				break;
			case PR_JOBCONTROL:
				if (t_jstop == NULL)
					t_jstop = t;
				break;
			case PR_REQUESTED:
				if (t_req == NULL)
					t_req = t;
				break;
			default:
				if (t_istop == NULL)
					t_istop = t;
				break;
			}
			break;
		}
		thread_unlock(t);
	} while ((t = t->t_forw) != p->p_tlist);

	if (t_onproc)
		t = t_onproc;
	else if (t_run)
		t = t_run;
	else if (t_sleep)
		t = t_sleep;
	else if (t_jstop)
		t = t_jstop;
	else if (t_istop)
		t = t_istop;
	else if (t_req)
		t = t_req;
	else if (t_hold)
		t = t_hold;
	else if (t_susp)
		t = t_susp;
	else			/* TS_ZOMB */
		t = p->p_tlist;

	if (t != NULL)
		thread_lock(t);
	return (t);
}

/*
 * Wakeup anyone sleeping on the /proc vnode for the process/lwp to stop.
 * Also call pollwakeup() if any lwps are waiting in poll() for POLLPRI
 * on the /proc file descriptor.  Called from stop() when a traced
 * process stops on an event of interest.  Also called from exit()
 * and prinvalidate() to indicate POLLHUP and POLLERR respectively.
 */
void
prnotify(vp)
	struct vnode *vp;
{
	register prnode_t *pnp = VTOP(vp);

	mutex_enter(&pnp->pr_lock);
	cv_broadcast(&pnp->pr_wait);
	mutex_exit(&pnp->pr_lock);
	if (pnp->pr_flags & PRPOLL) {
		/*
		 * We call pollwakeup() with POLLHUP to ensure that
		 * the pollers are awakened even if they are polling
		 * for nothing (i.e., waiting for the process to exit).
		 * This enables the use of the PRPOLL flag for optimization
		 * (we can turn off PRPOLL only if we know no pollers remain).
		 */
		pnp->pr_flags &= ~PRPOLL;
		pollwakeup(&pnp->pr_pollhead, POLLHUP);
	}
}

/*
 * Called from a hook in freeproc() when a traced process is removed
 * from the process table.  The proc-table pointers of all associated
 * /proc vnodes are cleared to indicate that the process has gone away.
 */
void
prfree(p)
	struct proc *p;
{
	register struct prnode *pnp;
	register struct vnode *vp;
	u_int slot = p->p_slot;

	/*
	 * Block the process against /proc so it can be freed.
	 * It cannot be freed while locked by some controlling process.
	 * Lock ordering:
	 *	pidlock -> pr_pidlock -> p->p_lock -> pnp->pr_lock
	 */
	mutex_enter(&pr_pidlock);
	mutex_enter(&p->p_lock);
	p->p_prwant++;
	while (p->p_flag & SPRLOCK) {
		mutex_exit(&p->p_lock);
		cv_wait(&pr_pid_cv[slot], &pr_pidlock);
		mutex_enter(&p->p_lock);
	}

	ASSERT(p->p_tlist == NULL);

	vp = p->p_plist;
	while (vp != NULL) {
		pnp = VTOP(vp);
		ASSERT(pnp->pr_thread == NULL);
		ASSERT(pnp->pr_proc != NULL);
		pnp->pr_proc = NULL;
		/*
		 * We can't call prnotify() here because we are holding
		 * pidlock.  We assert that there is no need to.
		 */
		mutex_enter(&pnp->pr_lock);
		cv_broadcast(&pnp->pr_wait);
		mutex_exit(&pnp->pr_lock);
		ASSERT(!(pnp->pr_flags & PRPOLL));

		vp = pnp->pr_vnext;
		pnp->pr_vnext = NULL;
	}
	p->p_trace = NULL;
	p->p_plist = NULL;

	/*
	 * We broadcast to wake up everyone waiting for this process.
	 * No one can reach this process from this point on.
	 */
	cv_broadcast(&pr_pid_cv[slot]);

	mutex_exit(&p->p_lock);
	mutex_exit(&pr_pidlock);
}

/*
 * Called from a hook in exit() when a traced process is becoming a zombie.
 */
void
prexit(p)
	register struct proc *p;
{
	ASSERT(MUTEX_HELD(&p->p_lock));

	if (p->p_trace) {
		VTOP(p->p_trace)->pr_flags |= PRZOMB;
		prnotify(p->p_trace);
	}
}

/*
 * Called when an lwp is destroyed.
 * lwps either destroy themselves or a sibling destroys them.
 * The thread pointer t is not necessarily the curthread.
 */
void
prlwpexit(t)
	kthread_t *t;
{
	register struct vnode *vp;
	register struct prnode *pnp;
	register struct proc *p = ttoproc(t);

	ASSERT(MUTEX_HELD(&p->p_lock));
	ASSERT(p == ttoproc(curthread));

	/*
	 * We block the process against /proc so we can safely do this.
	 * A controlling process may have dropped p_lock while fetching or
	 * setting the lwp's registers (to avoid deadlock with the clock).
	 * The lwp must not disappear while the process is marked SPRLOCK.
	 */
	prbarrier(p);
	/* LINTED lint confusion: used before set: pnp */
	for (vp = p->p_plist; vp != NULL; vp = pnp->pr_vnext) {
		pnp = VTOP(vp);
		if (pnp->pr_thread == t)
			pnp->pr_thread = NULL;
	}

	if (t->t_trace) {
		prnotify(t->t_trace);
		t->t_trace = NULL;
	}
	if (p->p_trace)
		prnotify(p->p_trace);
}

/*
 * Called from a hook in exec() when a process starts exec().
 */
void
prexecstart()
{
	register proc_t *p = ttoproc(curthread);
	register klwp_t *lwp = ttolwp(curthread);

	/*
	 * The SPREXEC flag blocks /proc operations for
	 * the duration of the exec().
	 * We can't start exec() while the process is
	 * locked by /proc, so we call prbarrier().
	 * lwp_nostop keeps the process from being stopped
	 * via job control for the duration of the exec().
	 */

	mutex_enter(&p->p_lock);
	prbarrier(p);
	lwp->lwp_nostop++;
	p->p_flag |= SPREXEC;
	mutex_exit(&p->p_lock);
}

/*
 * Called from a hook in exec() when a process finishes exec().
 */
void
prexecend()
{
	register prnode_t *pnp;
	register vnode_t *vp;
	register proc_t *p = ttoproc(curthread);
	register klwp_t *lwp = ttolwp(curthread);

	/*
	 * Wake up anyone waiting in /proc for the process to complete exec().
	 */
	mutex_enter(&p->p_lock);
	lwp->lwp_nostop--;
	p->p_flag &= ~SPREXEC;
	if ((vp = p->p_trace) != NULL) {
		pnp = VTOP(vp);
		mutex_enter(&pnp->pr_lock);
		cv_broadcast(&pnp->pr_wait);
		mutex_exit(&pnp->pr_lock);
	}
	if ((vp = curthread->t_trace) != NULL) {
		pnp = VTOP(vp);
		mutex_enter(&pnp->pr_lock);
		cv_broadcast(&pnp->pr_wait);
		mutex_exit(&pnp->pr_lock);
	}
	mutex_exit(&p->p_lock);
}

/*
 * Called from hooks in exec-related code when a traced process
 * attempts to exec(2) a setuid/setgid program or an unreadable
 * file.  Rather than fail the exec we invalidate the associated
 * /proc vnodes so that subsequent attempts to use them will fail.
 *
 * All /proc vnodes are retained on a linked list (rooted
 * at p_plist in the process structure) until last close.
 *
 * A controlling process must re-open the /proc file in order to
 * regain control.
 */
void
prinvalidate(up)
	register struct user *up;
{
	register kthread_t *t = curthread;
	register proc_t *p = ttoproc(t);
	register struct vnode *vp;
	register struct prnode *pnp;

	mutex_enter(&p->p_lock);

	/*
	 * At this moment, there can be only one lwp in the process.
	 */
	ASSERT(p->p_tlist == t && t->t_forw == t);

	/*
	 * Invalidate any currently active /proc vnodes.
	 */
	if ((vp = p->p_trace) != NULL) {
		VTOP(vp)->pr_flags |= PRINVAL;
		p->p_trace = NULL;
		prnotify(vp);
	}
	if ((vp = t->t_trace) != NULL) {
		VTOP(vp)->pr_flags |= PRINVAL;
		t->t_trace = NULL;
		prnotify(vp);
	}

	/*
	 * If any tracing flags are in effect and any vnodes are open for
	 * writing then set the requested-stop and run-on-last-close flags.
	 * Otherwise, clear all tracing flags.
	 */
	/* LINTED lint confusion: used before set: pnp */
	for (vp = p->p_plist; vp != NULL; vp = pnp->pr_vnext) {
		pnp = VTOP(vp);
		if (pnp->pr_writers)	/* some writer exists */
			break;
	}

	if ((p->p_flag & SPROCTR) && vp != NULL) {
		t->t_proc_flag |= TP_PRSTOP;
		aston(t);		/* so ISSIG will see the flag */
		p->p_flag |= SRUNLCL;
	} else {
		premptyset(&up->u_entrymask);		/* syscalls */
		premptyset(&up->u_exitmask);
		up->u_systrap = 0;
		premptyset(&p->p_sigmask);		/* signals */
		premptyset(&p->p_fltmask);		/* faults */
		t->t_proc_flag &= ~(TP_PRSTOP|TP_PRVSTOP);
		p->p_flag &= ~(SRUNLCL|SKILLCL|SPROCTR);
		prnostep(ttolwp(t));
	}

	mutex_exit(&p->p_lock);
}

/*
 * Acquire the controlled process's p_lock and mark it SPRLOCK.
 * Return with pr_pidlock held in all cases.
 * Return with p_lock held if the the process still exists.
 * Return value is the process pointer if the process still exists, else NULL.
 * If we lock the process, give ourself kernel priority to avoid deadlocks;
 * this is undone in prunlock().
 */
proc_t *
pr_p_lock(pnp)
	register prnode_t *pnp;
{
	register proc_t *p;
	u_int slot;

	mutex_enter(&pr_pidlock);
	if ((p = pnp->pr_proc) == NULL)
		return (NULL);
	mutex_enter(&p->p_lock);
	slot = p->p_slot;
	while (p->p_flag & SPRLOCK) {
		p->p_prwant++;
		mutex_exit(&p->p_lock);
		cv_wait(&pr_pid_cv[slot], &pr_pidlock);
		if (pnp->pr_proc == NULL)
			return (NULL);
		ASSERT(p == pnp->pr_proc);
		mutex_enter(&p->p_lock);
		p->p_prwant--;
	}
	p->p_flag |= SPRLOCK;
	THREAD_KPRI_REQUEST();
	return (p);
}

/*
 * Lock the target process by setting SPRLOCK and grabbing p->p_lock.
 * This prevents any lwp of the process from disappearing and
 * blocks most operations that a process can perform on itself.
 * Returns 0 on success, a non-zero error number on failure.
 *
 * 'zdisp' is ZYES or ZNO to indicate whether encountering a
 * zombie process is to be considered an error.
 *
 * error returns:
 *	ENOENT: process or lwp has disappeared
 *		(or has become a zombie and zdisp == ZNO).
 *	EAGAIN: procfs vnode has become invalid.
 *	EINTR:  signal arrived while waiting for exec to complete.
 */
int
prlock(pnp, zdisp)
	register struct prnode *pnp;
	register int zdisp;
{
	register proc_t *p;
	register kthread_t *t;

again:
	p = pr_p_lock(pnp);
	mutex_exit(&pr_pidlock);

	/*
	 * Return ENOENT immediately if there is no process.
	 */
	if (p == NULL)
		return (ENOENT);

	ASSERT(p == pnp->pr_proc && p->p_stat != 0 && p->p_stat != SIDL);

	/*
	 * Return EAGAIN if we have encountered a security violation.
	 * (The process exec'd a set-id or unreadable executable file.)
	 */
	if (pnp->pr_flags & PRINVAL) {
		prunlock(pnp);
		return (EAGAIN);
	}

	/*
	 * Return ENOENT if process entered zombie state
	 * and we are not interested in zombies.
	 */
	if (zdisp == ZNO &&
	    ((pnp->pr_flags & PRZOMB) || p->p_tlist == NULL)) {
		prunlock(pnp);
		return (ENOENT);
	}

	/*
	 * If lwp-specific, check to see if lwp has disappeared.
	 */
	if (pnp->pr_type == PRT_LWP) {
		if ((t = pnp->pr_thread) == NULL ||
		    (zdisp == ZNO && t->t_state == TS_ZOMB)) {
			prunlock(pnp);
			return (ENOENT);
		}
		ASSERT(t->t_state != TS_FREE);
		ASSERT(ttoproc(t) == p);
	}

	/*
	 * If process is undergoing an exec(), wait for
	 * completion and then start all over again.
	 */
	if (p->p_flag & SPREXEC) {
		(void) prunmark(p);
		/*
		 * prunmark() might have dropped/reacquired p->p_lock.
		 * Check the SPREXEC flag again; it may have changed.
		 */
		if (!(p->p_flag & SPREXEC))
			mutex_exit(&p->p_lock);
		else {
			mutex_enter(&pnp->pr_lock);
			mutex_exit(&p->p_lock);
			if (!cv_wait_sig(&pnp->pr_wait, &pnp->pr_lock)) {
				mutex_exit(&pnp->pr_lock);
				return (EINTR);
			}
			mutex_exit(&pnp->pr_lock);
		}
		goto again;
	}

	/*
	 * We return holding p->p_lock.
	 */
	return (0);
}

/*
 * Undo prlock() and pr_p_lock().
 * p->p_lock is still held; pr_pidlock is no longer held.
 *
 * prunmark() drops the SPRLOCK flag and wakes up another thread,
 * if any, waiting for the flag to be dropped; it retains p->p_lock,
 * after dropping and reacquiring it if necessary.
 * Returns 1 if p->p_lock was dropped and reacquired, else 0.
 *
 * prunlock() calls prunmark() and then drops p->p_lock.
 */
int
prunmark(register proc_t *p)
{
	int rval = 0;

	ASSERT(p->p_flag & SPRLOCK);
	ASSERT(MUTEX_HELD(&p->p_lock));

	/*
	 * Lock ordering:
	 *	pr_pidlock -> p->p_lock
	 * We can safely drop p->p_lock because the process
	 * will not disappear while it is marked SPRLOCK.
	 */
	if (p->p_prwant) {
		/* Somebody wants the process; do it the hard way */
		mutex_exit(&p->p_lock);
		mutex_enter(&pr_pidlock);
		mutex_enter(&p->p_lock);
		cv_signal(&pr_pid_cv[p->p_slot]);
		mutex_exit(&pr_pidlock);
		rval = 1;
	}
	p->p_flag &= ~SPRLOCK;
	THREAD_KPRI_RELEASE();
	return (rval);
}

void
prunlock(struct prnode *pnp)
{
	register proc_t *p = pnp->pr_proc;

	ASSERT(p != NULL);
	(void) prunmark(p);
	mutex_exit(&p->p_lock);
}

/*
 * Called while holding p->p_lock to delay until the process is unlocked.
 * We enter holding p->p_lock; p->p_lock is dropped and reacquired.
 * The process cannot become locked again until p->p_lock is dropped.
 */
void
prbarrier(proc_t *p)
{
	ASSERT(MUTEX_HELD(&p->p_lock));

	if (p->p_flag & SPRLOCK) {
		/* The process is locked; delay until not locked */
		u_int slot = p->p_slot;

		mutex_exit(&p->p_lock);
		mutex_enter(&pr_pidlock);
		mutex_enter(&p->p_lock);
		p->p_prwant++;
		while (p->p_flag & SPRLOCK) {
			mutex_exit(&p->p_lock);
			cv_wait(&pr_pid_cv[slot], &pr_pidlock);
			mutex_enter(&p->p_lock);
		}
		if (--p->p_prwant)
			cv_signal(&pr_pid_cv[slot]);
		mutex_exit(&pr_pidlock);
	}
}

/*
 * Return process/lwp status.
 * The u-block is mapped in by this routine and unmapped at the end.
 */
void
prgetstatus(t, sp)
	register kthread_t *t;
	register prstatus_t *sp;
{
	register proc_t *p = ttoproc(t);
	register klwp_t *lwp = ttolwp(t);
	register long flags;
	register user_t *up;
	register kthread_t *aslwptp;
	u_long restonano;

	ASSERT(MUTEX_HELD(&p->p_lock));

	up = prumap(p);
	bzero((caddr_t)sp, sizeof (*sp));
	flags = 0L;
	if (t->t_state == TS_STOPPED) {
		flags |= PR_STOPPED;
		if ((t->t_schedflag & TS_PSTART) == 0)
			flags |= PR_ISTOP;
	} else if (VSTOPPED(t)) {
		flags |= PR_STOPPED|PR_ISTOP;
	}
	if (!(flags & PR_ISTOP) && (t->t_proc_flag & TP_PRSTOP))
		flags |= PR_DSTOP;
	if (lwp->lwp_asleep)
		flags |= PR_ASLEEP;
	if (prisstep(lwp))
		flags |= PR_STEP;
	if (p->p_flag & SPRFORK)
		flags |= PR_FORK;
	if (p->p_flag & SRUNLCL)
		flags |= PR_RLC;
	if (p->p_flag & SKILLCL)
		flags |= PR_KLC;
	if (p->p_flag & SPASYNC)
		flags |= PR_ASYNC;
	if (p->p_flag & SBPTADJ)
		flags |= PR_BPTADJ;
	if (p->p_flag & STRC)
		flags |= PR_PCOMPAT;
	if (t->t_proc_flag & TP_MSACCT)
		flags |= PR_MSACCT;
	if (t == p->p_aslwptp)
		flags |= PR_ASLWP;
	sp->pr_flags = flags;
	if (VSTOPPED(t)) {
		sp->pr_why   = PR_REQUESTED;
		sp->pr_what  = 0;
	} else {
		sp->pr_why   = t->t_whystop;
		sp->pr_what  = t->t_whatstop;
	}

	if (t->t_whystop == PR_FAULTED)
		bcopy((caddr_t)&lwp->lwp_siginfo,
		    (caddr_t)&sp->pr_info, sizeof (k_siginfo_t));
	else if (lwp->lwp_curinfo)
		bcopy((caddr_t)&lwp->lwp_curinfo->sq_info,
		    (caddr_t)&sp->pr_info, sizeof (k_siginfo_t));

	sp->pr_cursig  = lwp->lwp_cursig;
	if ((aslwptp = p->p_aslwptp) != NULL) {
		k_sigset_t set;

		set = aslwptp->t_sig;
		sigorset(&set, &p->p_notifsigs);
		prassignset(&sp->pr_sigpend, &set);
	} else {
		prassignset(&sp->pr_sigpend, &p->p_sig);
	}
	prassignset(&sp->pr_lwppend, &t->t_sig);
	prassignset(&sp->pr_sighold, &t->t_hold);
	sp->pr_altstack = lwp->lwp_sigaltstack;
	prgetaction(p, up, lwp->lwp_cursig, &sp->pr_action);
	sp->pr_pid   = p->p_pid;
	sp->pr_ppid  = p->p_ppid;
	sp->pr_pgrp  = p->p_pgrp;
	sp->pr_sid   = p->p_sessp->s_sid;
	restonano = 1000000000 / timer_resolution;
	sp->pr_utime.tv_sec = p->p_utime / timer_resolution;
	sp->pr_utime.tv_nsec = (p->p_utime % timer_resolution) * restonano;
	sp->pr_stime.tv_sec = p->p_stime / timer_resolution;
	sp->pr_stime.tv_nsec = (p->p_stime % timer_resolution) * restonano;
	sp->pr_cutime.tv_sec = p->p_cutime / timer_resolution;
	sp->pr_cutime.tv_nsec = (p->p_cutime % timer_resolution) * restonano;
	sp->pr_cstime.tv_sec = p->p_cstime / timer_resolution;
	sp->pr_cstime.tv_nsec = (p->p_cstime % timer_resolution) * restonano;
	bcopy(sclass[t->t_cid].cl_name, sp->pr_clname,
	    min(sizeof (sclass[0].cl_name), sizeof (sp->pr_clname)-1));
	sp->pr_who = t->t_tid;
	sp->pr_nlwp = p->p_lwpcnt;
	sp->pr_brkbase = p->p_brkbase;
	sp->pr_brksize = p->p_brksize;
	sp->pr_stkbase = prgetstackbase(p);
	sp->pr_stksize = p->p_stksize;
	sp->pr_oldcontext = lwp->lwp_oldcontext;
	sp->pr_processor = t->t_cpu->cpu_id;
	sp->pr_bind = t->t_bind_cpu;
	prunmap(p);

	/*
	 * Fetch the current instruction, if not a system process.
	 * We don't attempt this unless the lwp is stopped.
	 */
	if ((p->p_flag & SSYS) || p->p_as == &kas)
		sp->pr_flags |= (PR_ISSYS|PR_PCINVAL);
	else if (!(flags & PR_STOPPED))
		sp->pr_flags |= PR_PCINVAL;
	else if (!prfetchinstr(lwp, &sp->pr_instr))
		sp->pr_flags |= PR_PCINVAL;

	/*
	 * Drop p_lock while touching the lwp's stack.
	 */
	mutex_exit(&p->p_lock);
	if ((t->t_state == TS_STOPPED &&
	    (t->t_whystop == PR_SYSENTRY || t->t_whystop == PR_SYSEXIT)) ||
	    (flags & PR_ASLEEP)) {
		int i;

		sp->pr_syscall = get_syscall_args(lwp,
			(int *)sp->pr_sysarg, &i);
		sp->pr_nsysarg = (short)i;
	}
	prgetprregs(lwp, sp->pr_reg);
	mutex_enter(&p->p_lock);
}

/*
 * Get the sigaction structure for the specified signal.  The u-block
 * must already have been mapped in by the caller.
 */
void
prgetaction(p, up, sig, sp)
	register proc_t *p;
	register user_t *up;
	register u_int sig;
	register struct sigaction *sp;
{
	sp->sa_handler = SIG_DFL;
	premptyset(&sp->sa_mask);
	sp->sa_flags = 0;

	if (sig != 0 && (unsigned)sig < NSIG) {
		sp->sa_handler = up->u_signal[sig-1];
		prassignset(&sp->sa_mask, &up->u_sigmask[sig-1]);
		if (sigismember(&up->u_sigonstack, sig))
			sp->sa_flags |= SA_ONSTACK;
		if (sigismember(&up->u_sigresethand, sig))
			sp->sa_flags |= SA_RESETHAND;
		if (sigismember(&up->u_sigrestart, sig))
			sp->sa_flags |= SA_RESTART;
		if (sigismember(&p->p_siginfo, sig))
			sp->sa_flags |= SA_SIGINFO;
		if (sigismember(&up->u_signodefer, sig))
			sp->sa_flags |= SA_NODEFER;
		switch (sig) {
		case SIGCLD:
			if (p->p_flag & SNOWAIT)
				sp->sa_flags |= SA_NOCLDWAIT;
			if ((p->p_flag & SJCTL) == 0)
				sp->sa_flags |= SA_NOCLDSTOP;
			break;
		case SIGWAITING:
			if (p->p_flag & SWAITSIG)
				sp->sa_flags |= SA_WAITSIG;
			break;
		}
	}
}

/*
 * Count the number of segments in this process's address space.
 * We always return 0 for a system process.
 */
int
prnsegs(as)
	register struct as *as;
{
	int n = 0;
	register struct seg *seg;

	ASSERT(as != NULL && AS_READ_HELD(as, &as->a_lock));

	if (as != &kas) {
		for (seg = AS_SEGP(as, as->a_segs); seg != NULL;
		    seg = AS_SEGP(as, seg->s_next)) {
			caddr_t naddr;
			caddr_t saddr = seg->s_base;
			caddr_t eaddr = seg->s_base + seg->s_size;

			do {
				(void) as_getprot(as, &saddr, &naddr);
				if (saddr != naddr)
					n++;
			} while ((saddr = naddr) != eaddr);
		}
	}

	return (n);
}

/*
 * Fill an array of structures with memory map information.  The array
 * has already been zero-filled by the caller.
 */
void
prgetmap(as, brkaddr, stkaddr, prmapp)
	struct as *as;
	caddr_t brkaddr;
	caddr_t stkaddr;
	prmap_t *prmapp;
{
	register prmap_t *mp;
	register struct seg *seg;
	struct seg *brkseg, *stkseg;
	int prot;

	ASSERT(as != NULL && AS_READ_HELD(as, &as->a_lock));

	mp = prmapp;

	if ((seg = AS_SEGP(as, as->a_segs)) == NULL)
		return;

	brkseg = as_segat(as, brkaddr);
	stkseg = as_segat(as, stkaddr);

	do {
		caddr_t naddr;
		caddr_t saddr = seg->s_base;
		caddr_t eaddr = seg->s_base + seg->s_size;

		do {
			prot = as_getprot(as, &saddr, &naddr);
			if (saddr == naddr)
				continue;
			mp->pr_vaddr = saddr;
			mp->pr_size = naddr - saddr;
			mp->pr_off = SEGOP_GETOFFSET(seg, saddr);
			mp->pr_mflags = 0;
			if (prot & PROT_READ)
				mp->pr_mflags |= MA_READ;
			if (prot & PROT_WRITE)
				mp->pr_mflags |= MA_WRITE;
			if (prot & PROT_EXEC)
				mp->pr_mflags |= MA_EXEC;
			if (SEGOP_GETTYPE(seg, saddr) == MAP_SHARED)
				mp->pr_mflags |= MA_SHARED;
			if (seg == brkseg)
				mp->pr_mflags |= MA_BREAK;
			else if (seg == stkseg)
				mp->pr_mflags |= MA_STACK;
			mp->pr_pagesize = PAGESIZE;
			mp++;
		} while ((saddr = naddr) != eaddr);
	} while ((seg = AS_SEGP(as, seg->s_next)) != NULL);
}

/*
 * Return the size of the /proc page data file.
 */
long
prpdsize(as)
	register struct as *as;
{
	register struct seg *seg;
	register long size;

	ASSERT(as != NULL && AS_READ_HELD(as, &as->a_lock));

	if (as == &kas || (seg = AS_SEGP(as, as->a_segs)) == NULL)
		return (0);

	size = sizeof (prpageheader_t);
	do {
		caddr_t naddr;
		caddr_t saddr = seg->s_base;
		caddr_t eaddr = seg->s_base + seg->s_size;
		register int npage;

		do {
			(void) as_getprot(as, &saddr, &naddr);
			if ((npage = (naddr-saddr)/PAGESIZE) != 0)
				size += sizeof (prasmap_t) + round(npage);
		} while ((saddr = naddr) != eaddr);
	} while ((seg = AS_SEGP(as, seg->s_next)) != NULL);

	return (size);
}

/*
 * Read page data information.
 * The address space is locked and will not change.
 */
int
prpdread(as, hatid, uiop)
	struct as *as;
	u_int hatid;
	struct uio *uiop;
{
	caddr_t buf;
	long size;
	register prpageheader_t *php;
	register prasmap_t *pmp;
	struct seg *seg;
	int error;

	AS_LOCK_ENTER(as, &as->a_lock, RW_READER);

	if ((seg = AS_SEGP(as, as->a_segs)) == NULL) {
		AS_LOCK_EXIT(as, &as->a_lock);
		return (0);
	}
	size = prpdsize(as);
	ASSERT(size > 0);
	if (uiop->uio_resid < size) {
		AS_LOCK_EXIT(as, &as->a_lock);
		return (E2BIG);
	}

	buf = kmem_alloc(size, KM_SLEEP);
	php = (prpageheader_t *)buf;
	pmp = (prasmap_t *)(buf + sizeof (prpageheader_t));

	hrt2ts(gethrtime(), &php->pr_tstamp);
	php->pr_nmap = 0;
	php->pr_npage = 0;
	do {
		caddr_t naddr;
		caddr_t saddr = seg->s_base;
		caddr_t eaddr = saddr + seg->s_size;

		do {
			register u_int len;
			register int npage;
			register int prot;

			prot = as_getprot(as, &saddr, &naddr);
			if ((len = naddr - saddr) == 0)
				continue;
			npage = len/PAGESIZE;
			ASSERT(npage > 0);
			php->pr_nmap++;
			php->pr_npage += npage;
			pmp->pr_vaddr = saddr;
			pmp->pr_npage = npage;
			pmp->pr_off = SEGOP_GETOFFSET(seg, saddr);
			pmp->pr_mflags = 0;
			if (prot & PROT_READ)
				pmp->pr_mflags |= MA_READ;
			if (prot & PROT_WRITE)
				pmp->pr_mflags |= MA_WRITE;
			if (prot & PROT_EXEC)
				pmp->pr_mflags |= MA_EXEC;
			if (SEGOP_GETTYPE(seg, saddr) == MAP_SHARED)
				pmp->pr_mflags |= MA_SHARED;
			pmp->pr_pagesize = PAGESIZE;
			hat_getstatby(as, saddr, len, hatid,
			    (char *)(pmp+1), 1);
			pmp = (prasmap_t *)((caddr_t)(pmp+1) + round(npage));
		} while ((saddr = naddr) != eaddr);
	} while ((seg = AS_SEGP(as, seg->s_next)) != NULL);

	AS_LOCK_EXIT(as, &as->a_lock);

	ASSERT((caddr_t)pmp == buf+size);
	error = uiomove(buf, size, UIO_READ, uiop);
	kmem_free(buf, size);

	return (error);
}

/*
 * Return information used by ps(1).
 */
void
prgetpsinfo(p, psp, tp)
	register proc_t *p;
	register prpsinfo_t *psp;
	kthread_t *tp;
{
	kthread_t *t;
	register char c, state;
	user_t *up;
	u_long hztime;
	dev_t d;
	u_long pct;
	clock_t ticks;
	int retval, niceval;
	struct cred *cred;

	ASSERT(MUTEX_HELD(&p->p_lock));

	bzero((caddr_t)psp, sizeof (struct prpsinfo));

	if ((t = tp) == NULL)
		t = prchoose(p);	/* returns locked thread */
	else
		thread_lock(t);

	/* kludge: map thread state enum into process state enum */

	if (t == NULL) {
		state = TS_ZOMB;
	} else {
		state = VSTOPPED(t) ? TS_STOPPED : t->t_state;
		thread_unlock(t);
	}

	switch (state) {
	case TS_SLEEP:		state = SSLEEP;		break;
	case TS_RUN:		state = SRUN;		break;
	case TS_ONPROC:		state = SONPROC;	break;
	case TS_ZOMB:		state = SZOMB;		break;
	case TS_STOPPED:	state = SSTOP;		break;
	default:		state = 0;		break;
	}
	switch (state) {
	case SSLEEP:	c = 'S';	break;
	case SRUN:	c = 'R';	break;
	case SZOMB:	c = 'Z';	break;
	case SSTOP:	c = 'T';	break;
	case SIDL:	c = 'I';	break;
	case SONPROC:	c = 'O';	break;
#ifdef SXBRK
	case SXBRK:	c = 'X';	break;
#endif
	default:	c = '?';	break;
	}
	psp->pr_state = state;
	psp->pr_sname = c;
	psp->pr_zomb = (state == SZOMB);
	psp->pr_flag = p->p_flag;

	mutex_enter(&p->p_crlock);
	cred = p->p_cred;
	psp->pr_uid = cred->cr_ruid;
	psp->pr_gid = cred->cr_rgid;
	psp->pr_euid = cred->cr_uid;
	psp->pr_egid = cred->cr_gid;
	mutex_exit(&p->p_crlock);

	psp->pr_pid = p->p_pid;
	psp->pr_ppid = p->p_ppid;
	psp->pr_pgrp = p->p_pgrp;
	psp->pr_sid = p->p_sessp->s_sid;
	psp->pr_addr = prgetpsaddr(p);
	hztime = p->p_utime + p->p_stime;
	psp->pr_time.tv_sec = hztime / timer_resolution;
	psp->pr_time.tv_nsec =
	    (hztime % timer_resolution) * (1000000000 / timer_resolution);
	hztime = p->p_cutime + p->p_cstime;
	psp->pr_ctime.tv_sec = hztime / timer_resolution;
	psp->pr_ctime.tv_nsec =
	    (hztime % timer_resolution) * (1000000000 / timer_resolution);
	if (p->p_aslwptp)
		psp->pr_aslwpid = p->p_aslwptp->t_tid;
	if (state == SZOMB || t == NULL) {
		extern int wstat(int, int);	/* needs a header file */
		int wcode = p->p_wcode;		/* must be atomic read */

		if (wcode)
			psp->pr_wstat = wstat(wcode, p->p_wdata);
		psp->pr_lttydev = PRNODEV;
		psp->pr_ottydev = PRNODEV;
	} else {
		up = prumap(p);
		psp->pr_wchan = t->t_wchan;
		psp->pr_pri = t->t_pri;
		bcopy(sclass[t->t_cid].cl_name, psp->pr_clname,
		    min(sizeof (sclass[0].cl_name), sizeof (psp->pr_clname)-1));
		retval = CL_DONICE(t, NULL, 0, &niceval);
		if (retval == 0) {
			psp->pr_oldpri = v.v_maxsyspri - psp->pr_pri;
			psp->pr_nice = niceval + NZERO;
		} else {
			psp->pr_oldpri = 0;
			psp->pr_nice = 0;
		}
		d = cttydev(p);
#ifdef sun
		{
			extern dev_t rwsconsdev, rconsdev, uconsdev;
			/*
			 * If the controlling terminal is the real
			 * or workstation console device, map to what the
			 * user thinks is the console device.
			 */
			if (d == rwsconsdev || d == rconsdev)
				d = uconsdev;
		}
#endif
		psp->pr_lttydev = (d == NODEV) ? PRNODEV : d;
		psp->pr_ottydev = cmpdev(d);
		psp->pr_start.tv_sec = up->u_start;
		psp->pr_start.tv_nsec = 0L;
		bcopy(up->u_comm, psp->pr_fname,
		    min(sizeof (up->u_comm), sizeof (psp->pr_fname)-1));
		bcopy(up->u_psargs, psp->pr_psargs,
		    min(PRARGSZ-1, PSARGSZ));
		psp->pr_syscall = t->t_sysnum;
		psp->pr_argc = up->u_argc;
		psp->pr_argv = up->u_argv;
		psp->pr_envp = up->u_envp;
		prunmap(p);

		/* compute %cpu for the lwp or process */
		ticks = lbolt;
		pct = 0;
		if ((t = tp) == NULL)
			t = p->p_tlist;
		do {
			pct += cpu_decay(t->t_pctcpu, ticks - t->t_lbolt - 1);
			if (tp != NULL)		/* just do the one lwp */
				break;
		} while ((t = t->t_forw) != p->p_tlist);

		/* prorate over the online cpus so we don't exceed 100% */
		if (ncpus > 1)
			pct /= ncpus;
		if (pct > 0x8000)	/* might happen, due to rounding */
			pct = 0x8000;
		psp->pr_pctcpu = pct;
		psp->pr_cpu = (pct*100 + 0x6000) >> 15;	/* [0..99] */
		if (psp->pr_cpu > 99)
			psp->pr_cpu = 99;
	}
}


/*
 * Called when microstate accounting information is requested for a thread
 * where microstate accounting (TP_MSACCT) isn't on.  Turn it on for this and
 * all other LWPs in the process and get an estimate of usage so far.
 */
void
estimate_msacct(kthread_t *t, hrtime_t curtime)
{
	register proc_t	*p = ttoproc(t);
	register klwp_t *lwp;
	register struct mstate *ms;
	register hrtime_t ns;

	ASSERT(MUTEX_HELD(&p->p_lock));

	/*
	 * A system process (p0) could be referenced if the thread is
	 * in the process of exiting.  Don't turn on microstate accounting
	 * in that case.
	 */
	if (p->p_flag & SSYS)
		return;

	/*
	 * Loop through all the LWPs (kernel threads) in the process.
	 */
	t = p->p_tlist;
	do {
		int ms_prev;
		int lwp_state;
		hrtime_t total;
		int i;

		ASSERT((t->t_proc_flag & TP_MSACCT) == 0);

		lwp = ttolwp(t);
		ms = &lwp->lwp_mstate;

		bzero((caddr_t)&ms->ms_acct[0], sizeof (ms->ms_acct));

		/*
		 * Convert tick-based user and system time to microstate times.
		 */
		ns = (hrtime_t)nsec_per_tick;
		ms->ms_acct[LMS_USER] = lwp->lwp_utime * ns;
		ms->ms_acct[LMS_SYSTEM] = lwp->lwp_stime * ns;
		/*
		 * Add all unaccounted-for time to the LMS_SLEEP time.
		 */
		for (total = 0, i = 0; i < NMSTATES; i++)
			total += ms->ms_acct[i];
		ms->ms_acct[LMS_SLEEP] += curtime - ms->ms_start - total;
		t->t_waitrq = 0;

		/*
		 * Determine the current microstate and set the start time.
		 * Be careful not to touch the lwp while holding thread_lock().
		 */
		ms->ms_state_start = curtime;
		lwp_state = lwp->lwp_state;
		thread_lock(t);
		switch (t->t_state) {
		case TS_SLEEP:
			t->t_mstate = LMS_SLEEP;
			ms_prev = LMS_SYSTEM;
			break;
		case TS_RUN:
			t->t_waitrq = curtime;
			t->t_mstate = LMS_SLEEP;
			ms_prev = LMS_SYSTEM;
			break;
		case TS_ONPROC:
			/*
			 * The user/system state cannot be determined accurately
			 * on MP without stopping the thread.
			 * This might miss a system/user state transition.
			 */
			if (lwp_state == LWP_USER) {
				t->t_mstate = ms_prev = LMS_USER;
			} else {
				t->t_mstate = ms_prev = LMS_SYSTEM;
			}
			break;
		case TS_ZOMB:
		case TS_FREE:			/* shouldn't happen */
		case TS_STOPPED:
			t->t_mstate = LMS_STOPPED;
			ms_prev = LMS_SYSTEM;
			break;
		}
		thread_unlock(t);
		ms->ms_prev = ms_prev;	/* guess previous running state */
		t->t_proc_flag |= TP_MSACCT;
	} while ((t = t->t_forw) != p->p_tlist);
	p->p_flag |= SMSACCT;			/* set process-wide MSACCT */
	/*
	 * Set system call pre- and post-processing flags for the process.
	 * This must be done AFTER the TP_MSACCT flag is set.
	 * Do this outside of the loop to avoid re-ordering.
	 */
	set_proc_sys(p);
}

/*
 * Turn off microstate accounting for all LWPs in the process.
 */
void
disable_msacct(proc_t *p)
{
	register kthread_id_t t;

	ASSERT(MUTEX_HELD(&p->p_lock));

	p->p_flag &= ~SMSACCT;			/* clear process-wide MSACCT */
	/*
	 * Loop through all the LWPs (kernel threads) in the process.
	 */
	t = p->p_tlist;
	do {
		t->t_proc_flag &= ~TP_MSACCT;	/* clear per-thread flag */
	} while ((t = t->t_forw) != p->p_tlist);
}

/*
 * Return resource usage information.
 */
void
prgetusage(t, pup)
	register kthread_t *t;
	register prhusage_t *pup;
{
	register klwp_t *lwp = ttolwp(t);
	register hrtime_t *mstimep;
	register struct mstate *ms = &lwp->lwp_mstate;
	register int state;
	hrtime_t curtime;
	hrtime_t waitrq;

	curtime = pup->pr_tstamp;	/* passed by caller */

	/*
	 * If microstate accounting (TP_MSACCT) isn't on, turn it on and
	 * get an estimate of usage so far.
	 */
	if ((t->t_proc_flag & TP_MSACCT) == 0)
		estimate_msacct(t, curtime);

	pup->pr_lwpid	= t->t_tid;
	pup->pr_count	= 1;
	pup->pr_create	= ms->ms_start;
	pup->pr_term	= ms->ms_term;
	if (ms->ms_term == 0)
		pup->pr_rtime = curtime - ms->ms_start;
	else
		pup->pr_rtime = ms->ms_term - ms->ms_start;

	pup->pr_utime    = ms->ms_acct[LMS_USER];
	pup->pr_stime    = ms->ms_acct[LMS_SYSTEM];
	pup->pr_ttime    = ms->ms_acct[LMS_TRAP];
	pup->pr_tftime   = ms->ms_acct[LMS_TFAULT];
	pup->pr_dftime   = ms->ms_acct[LMS_DFAULT];
	pup->pr_kftime   = ms->ms_acct[LMS_KFAULT];
	pup->pr_ltime    = ms->ms_acct[LMS_USER_LOCK];
	pup->pr_slptime  = ms->ms_acct[LMS_SLEEP];
	pup->pr_wtime    = ms->ms_acct[LMS_WAIT_CPU];
	pup->pr_stoptime = ms->ms_acct[LMS_STOPPED];

	/*
	 * Adjust for time waiting in the dispatcher queue.
	 */
	waitrq = t->t_waitrq;	/* hopefully atomic */
	if (waitrq != 0) {
		pup->pr_wtime += curtime - waitrq;
		curtime = waitrq;
	}

	/*
	 * Adjust for time spent in current microstate.
	 */
	switch (state = t->t_mstate) {
	case LMS_SLEEP:
		/*
		 * Update the timer for the current sleep state.
		 */
		switch (state = ms->ms_prev) {
		case LMS_TFAULT:
		case LMS_DFAULT:
		case LMS_KFAULT:
		case LMS_USER_LOCK:
			break;
		default:
			state = LMS_SLEEP;
			break;
		}
		break;
	case LMS_TFAULT:
	case LMS_DFAULT:
	case LMS_KFAULT:
	case LMS_USER_LOCK:
		state = LMS_SYSTEM;
		break;
	}
	switch (state) {
	case LMS_USER:		mstimep = &pup->pr_utime;	break;
	case LMS_SYSTEM:	mstimep = &pup->pr_stime;	break;
	case LMS_TRAP:		mstimep = &pup->pr_ttime;	break;
	case LMS_TFAULT:	mstimep = &pup->pr_tftime;	break;
	case LMS_DFAULT:	mstimep = &pup->pr_dftime;	break;
	case LMS_KFAULT:	mstimep = &pup->pr_kftime;	break;
	case LMS_USER_LOCK:	mstimep = &pup->pr_ltime;	break;
	case LMS_SLEEP:		mstimep = &pup->pr_slptime;	break;
	case LMS_WAIT_CPU:	mstimep = &pup->pr_wtime;	break;
	case LMS_STOPPED:	mstimep = &pup->pr_stoptime;	break;
	default:		panic("prgetusage: unknown microstate");
	}
	*mstimep += curtime - ms->ms_state_start;

	/*
	 * Resource usage counters.
	 */
	pup->pr_minf  = lwp->lwp_ru.minflt;
	pup->pr_majf  = lwp->lwp_ru.majflt;
	pup->pr_nswap = lwp->lwp_ru.nswap;
	pup->pr_inblk = lwp->lwp_ru.inblock;
	pup->pr_oublk = lwp->lwp_ru.oublock;
	pup->pr_msnd  = lwp->lwp_ru.msgsnd;
	pup->pr_mrcv  = lwp->lwp_ru.msgrcv;
	pup->pr_sigs  = lwp->lwp_ru.nsignals;
	pup->pr_vctx  = lwp->lwp_ru.nvcsw;
	pup->pr_ictx  = lwp->lwp_ru.nivcsw;
	pup->pr_sysc  = lwp->lwp_ru.sysc;
	pup->pr_ioch  = lwp->lwp_ru.ioch;
}

/*
 * Sum resource usage information.
 */
void
praddusage(t, pup)
	register kthread_t *t;
	register prhusage_t *pup;
{
	register klwp_t *lwp = ttolwp(t);
	register hrtime_t *mstimep;
	register struct mstate *ms = &lwp->lwp_mstate;
	register int state;
	hrtime_t curtime;
	hrtime_t waitrq;

	curtime = pup->pr_tstamp;	/* passed by caller */

	/*
	 * If microstate accounting (TP_MSACCT) isn't on, turn it on and
	 * get an estimate of usage so far.
	 */
	if ((t->t_proc_flag & TP_MSACCT) == 0)
		estimate_msacct(t, curtime);

	if (ms->ms_term == 0)
		pup->pr_rtime += curtime - ms->ms_start;
	else
		pup->pr_rtime += ms->ms_term - ms->ms_start;
	pup->pr_utime	+= ms->ms_acct[LMS_USER];
	pup->pr_stime	+= ms->ms_acct[LMS_SYSTEM];
	pup->pr_ttime	+= ms->ms_acct[LMS_TRAP];
	pup->pr_tftime	+= ms->ms_acct[LMS_TFAULT];
	pup->pr_dftime	+= ms->ms_acct[LMS_DFAULT];
	pup->pr_kftime	+= ms->ms_acct[LMS_KFAULT];
	pup->pr_ltime	+= ms->ms_acct[LMS_USER_LOCK];
	pup->pr_slptime	+= ms->ms_acct[LMS_SLEEP];
	pup->pr_wtime	+= ms->ms_acct[LMS_WAIT_CPU];
	pup->pr_stoptime += ms->ms_acct[LMS_STOPPED];

	/*
	 * Adjust for time waiting in the dispatcher queue.
	 */
	waitrq = t->t_waitrq;	/* hopefully atomic */
	if (waitrq != 0) {
		pup->pr_wtime += curtime - waitrq;
		curtime = waitrq;
	}

	/*
	 * Adjust for time spent in current microstate.
	 */
	switch (state = t->t_mstate) {
	case LMS_SLEEP:
		/*
		 * Update the timer for the current sleep state.
		 */
		switch (state = ms->ms_prev) {
		case LMS_TFAULT:
		case LMS_DFAULT:
		case LMS_KFAULT:
		case LMS_USER_LOCK:
			break;
		default:
			state = LMS_SLEEP;
			break;
		}
		break;
	case LMS_TFAULT:
	case LMS_DFAULT:
	case LMS_KFAULT:
	case LMS_USER_LOCK:
		state = LMS_SYSTEM;
		break;
	}
	switch (state) {
	case LMS_USER:		mstimep = &pup->pr_utime;	break;
	case LMS_SYSTEM:	mstimep = &pup->pr_stime;	break;
	case LMS_TRAP:		mstimep = &pup->pr_ttime;	break;
	case LMS_TFAULT:	mstimep = &pup->pr_tftime;	break;
	case LMS_DFAULT:	mstimep = &pup->pr_dftime;	break;
	case LMS_KFAULT:	mstimep = &pup->pr_kftime;	break;
	case LMS_USER_LOCK:	mstimep = &pup->pr_ltime;	break;
	case LMS_SLEEP:		mstimep = &pup->pr_slptime;	break;
	case LMS_WAIT_CPU:	mstimep = &pup->pr_wtime;	break;
	case LMS_STOPPED:	mstimep = &pup->pr_stoptime;	break;
	default:		panic("praddusage: unknown microstate");
	}
	*mstimep += curtime - ms->ms_state_start;

	/*
	 * Resource usage counters.
	 */
	pup->pr_minf  += lwp->lwp_ru.minflt;
	pup->pr_majf  += lwp->lwp_ru.majflt;
	pup->pr_nswap += lwp->lwp_ru.nswap;
	pup->pr_inblk += lwp->lwp_ru.inblock;
	pup->pr_oublk += lwp->lwp_ru.oublock;
	pup->pr_msnd  += lwp->lwp_ru.msgsnd;
	pup->pr_mrcv  += lwp->lwp_ru.msgrcv;
	pup->pr_sigs  += lwp->lwp_ru.nsignals;
	pup->pr_vctx  += lwp->lwp_ru.nvcsw;
	pup->pr_ictx  += lwp->lwp_ru.nivcsw;
	pup->pr_sysc  += lwp->lwp_ru.sysc;
	pup->pr_ioch  += lwp->lwp_ru.ioch;
}

/*
 * Convert a prhusage_t to a prusage_t, in place.
 * This means convert each hrtime_t to a timestruc_t.
 */
void
prcvtusage(pup)
	register prhusage_t *pup;
{
	hrt2ts(pup->pr_tstamp,	(timestruc_t *)&pup->pr_tstamp);
	hrt2ts(pup->pr_create,	(timestruc_t *)&pup->pr_create);
	hrt2ts(pup->pr_term,	(timestruc_t *)&pup->pr_term);
	hrt2ts(pup->pr_rtime,	(timestruc_t *)&pup->pr_rtime);
	hrt2ts(pup->pr_utime,	(timestruc_t *)&pup->pr_utime);
	hrt2ts(pup->pr_stime,	(timestruc_t *)&pup->pr_stime);
	hrt2ts(pup->pr_ttime,	(timestruc_t *)&pup->pr_ttime);
	hrt2ts(pup->pr_tftime,	(timestruc_t *)&pup->pr_tftime);
	hrt2ts(pup->pr_dftime,	(timestruc_t *)&pup->pr_dftime);
	hrt2ts(pup->pr_kftime,	(timestruc_t *)&pup->pr_kftime);
	hrt2ts(pup->pr_ltime,	(timestruc_t *)&pup->pr_ltime);
	hrt2ts(pup->pr_slptime,	(timestruc_t *)&pup->pr_slptime);
	hrt2ts(pup->pr_wtime,	(timestruc_t *)&pup->pr_wtime);
	hrt2ts(pup->pr_stoptime, (timestruc_t *)&pup->pr_stoptime);
}

/*
 * Determine whether a set is empty.
 */
int
setisempty(sp, n)
	register u_long *sp;
	register unsigned n;
{
	while (n--)
		if (*sp++)
			return (0);
	return (1);
}
