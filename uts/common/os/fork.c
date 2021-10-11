/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	Copyright (c) 1994 Sun Microsystems, Inc. */
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)fork.c 1.86	95/09/05 SMI"	/* from SVr4.0 1.63 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/signal.h>
#include <sys/cred.h>
#include <sys/user.h>
#include <sys/systm.h>
#include <sys/cpuvar.h>
#include <sys/map.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <sys/proc.h>
#include <sys/cmn_err.h>
#include <sys/acct.h>
#include <sys/tuneable.h>
#include <sys/class.h>
#include <sys/kmem.h>
#include <sys/session.h>
#include <sys/ucontext.h>
#include <sys/procfs.h>
#include <sys/prsystm.h>
#include <sys/vmsystm.h>
#include <sys/vtrace.h>
#include <sys/debug.h>
#include <sys/shm.h>
#include <vm/as.h>
#include <vm/rm.h>
#include <c2/audit.h>
#include <sys/var.h>
#include <sys/strlog.h>

static longlong_t cfork(int, int);
static int getproc(proc_t **, int);
static void fork_fail(proc_t *);
static void forklwp_fail(proc_t *);

int fork_fail_pending;

/*
 * fork system call.
 */
longlong_t
fork(void)
{
	return (cfork(0, 0));
}

/*
 * The parent is stopped until the child invokes relvm().
 */
longlong_t
vfork(void)
{
	curthread->t_post_sys = 1;	/* so vfwait() will be called */
	return (cfork(1, 1));
}

/*
 * fork1 system call
 */
longlong_t
fork1(void)
{
	return (cfork(0, 1));
}

/* ARGSUSED */
static longlong_t
cfork(int isvfork, int isfork1)
{
	proc_t *p = ttoproc(curthread);
	proc_t *cp, **orphpp;
	klwp_id_t clwp;
	kthread_id_t t;
	rval_t	r;
	int error;
	int i;

	/*
	 * If the calling LWP is doing a fork1() then the
	 * other LWPs in this process are not duplicated and
	 * don't need to be held where their kernel stacks
	 * can be cloned.  In general, the process is held with
	 * HOLDLWPS, so that the LWPs are at a point where their
	 * stacks can be copied which is on entry or exit from
	 * the kernel.
	 */
	if (!holdlwps(p, isfork1? HOLDLWP2 : HOLDLWPS)) {
		error = EINTR;
		goto forkerr;
	}

	/*
	 * Create a child proc struct. Place a VN_HOLD on appropriate vnodes.
	 */
	if (getproc(&cp, isfork1) < 0) {
		mutex_enter(&p->p_lock);
		continuelwps(p);
		mutex_exit(&p->p_lock);
		error = EAGAIN;
		goto forkerr;
	}

#ifdef TRACE
	trace_process_fork((u_long) (cp->p_pid), (u_long) (p->p_pid));
#endif	/* TRACE */
	TRACE_2(TR_FAC_PROC, TR_PROC_FORK,
		"proc_fork:cpid %d ppid %d", cp->p_pid, p->p_pid);

	/*
	 * Assign an address space to child
	 */
	if (isvfork) {
		cp->p_as = p->p_as;
		cp->p_flag |= SVFORK;
	} else {
		error = as_dup(p->p_as, &cp->p_as);
		if (error != 0) {
			fork_fail(cp);
			mutex_enter(&pidlock);
			orphpp = &p->p_orphan;
			while (*orphpp != cp)
				orphpp = &(*orphpp)->p_nextorph;
			*orphpp = cp->p_nextorph;
			ASSERT(p->p_child == cp);
			p->p_child = cp->p_sibling;
			if (p->p_child) {
				p->p_child->p_psibling = NULL;
			}
			pid_exit(cp);
			mutex_exit(&pidlock);

			mutex_enter(&p->p_lock);
			continuelwps(p);
			mutex_exit(&p->p_lock);
			/*
			 * Preserve ENOMEM error condition but
			 * map all others to EAGAIN.
			 */
			error = (error == ENOMEM) ? ENOMEM : EAGAIN;
			goto forkerr;
		}
		/* Duplicate parent's shared memory */
		if (p->p_segacct)
			shmfork(p, cp);
	}

	/*
	 * duplicate parent's lwps.
	 * mutual exclusion is not needed because the process is in the
	 * hold state and only the current lwp is running. however, if
	 * the p_lock mutex is not held, assertion checking for lwp
	 * routines will fail.
	 */
	i = ((isfork1) ? 1 : p->p_lwpcnt);
	for (t = curthread; i-- > 0; t = t->t_forw) {
		clwp = forklwp(ttolwp(t), cp);
		if (clwp == NULL) {
			if (!isvfork) {
				struct as *as;

				shmexit(cp);
				as = cp->p_as;
				cp->p_as = &kas;
				as_free(as);
			}
			forklwp_fail(cp);
			fork_fail(cp);
			mutex_enter(&pidlock);
			orphpp = &p->p_orphan;
			while (*orphpp != cp)
				orphpp = &(*orphpp)->p_nextorph;
			*orphpp = cp->p_nextorph;
			ASSERT(p->p_child == cp);
			p->p_child = cp->p_sibling;
			if (p->p_child) {
				p->p_child->p_psibling = NULL;
			}
			pid_exit(cp);
			mutex_exit(&pidlock);

			mutex_enter(&p->p_lock);
			continuelwps(p);
			mutex_exit(&p->p_lock);
			error = EAGAIN;
			goto forkerr;
		}
		/* only duplicate LWP IDs if doing a fork() */
		if (!isfork1)
			lwptot(clwp)->t_tid = t->t_tid;
	}

	/* make sure next lwp the child creates is unique */
	if (!isfork1)
		cp->p_lwptotal = p->p_lwptotal;

#ifdef i386
	if (p->p_ldt)			/* parent has a private LDT */
		(void) ldt_dup(p, cp);
#endif
	/*
	 * If the active child lwp has been marked to stop
	 * on exit from this fork, arrange for all other
	 * lwps to stop in sympathy with the active lwp.
	 */
	if (PTOU(cp)->u_systrap &&
	    prismember(&PTOU(cp)->u_exitmask, curthread->t_sysnum)) {
		for (t = cp->p_tlist->t_forw; t != cp->p_tlist; t = t->t_forw) {
			t->t_proc_flag |= TP_PRSTOP;
			aston(t);	/* so TP_PRSTOP will be seen */
		}
	}

	/* set return values for child */
	lwp_setrval(ttolwp(cp->p_tlist), p->p_pid, 1);

	/* set return values for parent */
	r.r_val1 = (int)cp->p_pid;
	r.r_val2 = 0;

	mutex_enter(&pidlock);
	/*
	 * Now that there are lwps and threads attached, add the new
	 * process to the process group.
	 */
	pgjoin(cp, p->p_pgidp);
	cp->p_stat = SRUN;

	/*
	 * Mutual exclusion is only needed here when assertion checking
	 * is on.
	 */
	if (isvfork) {
		CPU_STAT_ADDQ(CPU, cpu_sysinfo.sysvfork, 1);
		/*
		 * Grab p_lock before dropping pidlock to ensure the
		 * process will not disappear before we set it running.
		 */
		mutex_enter(&cp->p_lock);
		mutex_exit(&pidlock);
		continuelwps(cp);
		mutex_exit(&cp->p_lock);
	} else {
		CPU_STAT_ADDQ(CPU, cpu_sysinfo.sysfork, 1);
		/*
		 * It is CL_FORKRET's job to drop pidlock.
		 * If we do it here, the process could be set running
		 * and disappear before CL_FORKRET() is called.
		 */
		CL_FORKRET(curthread, cp->p_tlist);
		MUTEX_NOT_HELD(&pidlock);
	}

	return (r.r_vals);
forkerr:
	return ((longlong_t)set_errno(error));
}

/*
 * Free allocated resources from getproc() if a fork failed.
 */
static void
fork_fail(proc_t *cp)
{
	closeall(0);
	sigdelq(cp, NULL, 0);

	/*
	 * single threaded, so no locking needed here
	 */
	crfree(cp->p_cred);

	kmem_free(PTOU(cp)->u_flist,
		PTOU(cp)->u_nofiles * sizeof (struct uf_entry));

	VN_RELE(u.u_cdir);
	if (u.u_rdir)
		VN_RELE(u.u_rdir);
	if (cp->p_exec)
		VN_RELE(cp->p_exec);
}

/*
 * Clean up the lwps already created for this child process.
 * The fork failed while duplicating all the lwps of the parent
 * and those lwps already created must be freed.
 * This process is invisible to the rest of the system,
 * so we don't need to hold p->p_lock to protect the list.
 */
static void
forklwp_fail(proc_t *p)
{
	kthread_id_t t;

	while ((t = p->p_tlist) != NULL) {
		/*
		 * First remove the lwp from the process's p_tlist.
		 */
		if (t != t->t_forw)
			p->p_tlist = t->t_forw;
		else
			p->p_tlist = NULL;
		p->p_lwpcnt--;
		t->t_forw->t_back = t->t_back;
		t->t_back->t_forw = t->t_forw;
		/*
		 * Remove the thread from the all threads list.
		 * We need to hold pidlock for this.
		 */
		mutex_enter(&pidlock);
		t->t_next->t_prev = t->t_prev;
		t->t_prev->t_next = t->t_next;
		mutex_exit(&pidlock);

		thread_free(t);
	}
}

extern struct as kas;
extern id_t syscid;

/*
 * fork a kernel process.
 */
int
newproc(void (*pc)(), id_t cid, int pri)
{
	proc_t *p;
	struct user *up;

	if (getproc(&p, 0) < 0)
		return (EAGAIN);
	if (cid == syscid) {
		p->p_flag |= (SSYS | SLOCK | SNOWAIT);
		p->p_exec = NULL;
		/*
		 * kernel processes do not inherit /proc tracing flags.
		 */
		sigemptyset(&p->p_sigmask);
		premptyset(&p->p_fltmask);
		up = PTOU(p);
		up->u_systrap = 0;
		premptyset(&(up->u_entrymask));
		premptyset(&(up->u_exitmask));
	}
	p->p_as = &kas;

	if (lwp_create(pc, NULL, 0, p, TS_RUN, pri,
	    curthread->t_hold, cid) == NULL) {
		fork_fail(p);
		mutex_enter(&pidlock);
		pid_exit(p);
		mutex_exit(&pidlock);
		return (EAGAIN);
	} else {
		mutex_enter(&pidlock);
		pgjoin(p, curproc->p_pgidp);
		p->p_stat = SRUN;
		mutex_exit(&pidlock);
	}
	return (0);
}

/*
 * Setup context of child process.
 * up is a pointer to the child's u-area.
 * isfork1 is a flag indicating whether this
 * is a fork1 system call.
 */
void
setuctxt(user_t *up, int isfork1)
{
	int sz;

	/*
	 * XXX This code only works if the u_flist can not shrink.
	 */

	/* Copy u-block. */
	*up = *PTOU(curproc);

	cv_init(&up->u_cv, "u cv", CV_DEFAULT, NULL);
	mutex_init(&up->u_flock, "u flist lock", MUTEX_DEFAULT, DEFAULT_WT);
	/*
	 * I don't need to hold u_flock because all other lwp's in the
	 * parent have been held.
	 */
	sz = u.u_nofiles * sizeof (struct uf_entry);

	/*
	 * if fork1 (or vfork), the child process will have only one lwp.
	 * In this case, we only want to duplicate the open file dscriptors;
	 * we will leave the refcnt null and clear all flags in the pofile
	 * entry (but leave the FCLOSEXEC set)
	 */
	if (isfork1) {
		int i;
		struct uf_entry *pufe_s, *pufe_d;

		up->u_flist = kmem_zalloc(sz, KM_SLEEP);
		pufe_d = &up->u_flist[0];
		pufe_s = &u.u_flist[0];
		for (i = 0; i < u.u_nofiles; i++) {
			pufe_d[i].uf_ofile = pufe_s[i].uf_ofile;
			pufe_d[i].uf_pofile = pufe_s[i].uf_pofile & FCLOSEXEC;
		}
	} else {
		up->u_flist = kmem_alloc(sz, KM_SLEEP);
		bcopy((caddr_t)u.u_flist, (caddr_t)up->u_flist, (size_t)sz);
	}
}

/*
 * create a child proc struct.
 */
static int
getproc(proc_t **cpp, int isfork1)
{
	proc_t		*pp, *cp;
	pid_t		newpid;
	struct user	*uarea;
	extern u_int	nproc;
	struct cred	*cr;

	cp = kmem_zalloc(sizeof (proc_t), KM_SLEEP);

	/*
	 * Make proc entry for child process
	 */
	mutex_init(&cp->p_lock, "proc", MUTEX_DEFAULT, DEFAULT_WT);
	mutex_init(&cp->p_crlock, "proc crlock", MUTEX_DEFAULT, DEFAULT_WT);
	mutex_init(&cp->p_pflock, "profiling", MUTEX_DEFAULT, DEFAULT_WT);
#ifdef i386
	mutex_init(&cp->p_ldtlock, "LDT lock", MUTEX_DEFAULT, DEFAULT_WT);
#endif
	cp->p_stat = SIDL;
	cp->p_mstart = gethrtime();

	if ((newpid = pid_assign(cp)) == -1) {
		if (nproc == v.v_proc) {
			CPU_STAT_ADD(CPU, cpu_sysinfo.procovf, 1);
			(void) strlog(0, 1, 0, SL_CONSOLE | SL_WARN,
			    "out of processes", 0);
		}
		goto bad;
	}

	/*
	 * If not super-user make sure that this user hasn't exceeded
	 * v.v_maxup processes, and that users collectively haven't
	 * exceeded v.v_maxupttl processes.
	 */
	mutex_enter(&pidlock);
	ASSERT(nproc < v.v_proc);	/* otherwise how'd we get our pid? */
	cr = CRED();
	if (nproc >= v.v_maxup && 	/* short-circuit; usually false */
	    cr->cr_uid &&
	    cr->cr_ruid &&
	    (nproc >= v.v_maxupttl || upcount_get(cr->cr_ruid) >= v.v_maxup)) {
		char buf[80];
		mutex_exit(&pidlock);
		(void) sprintf(buf, "out of per-user processes for uid %d",
			cr->cr_ruid);
		(void) strlog(0, 1, 0, SL_CONSOLE | SL_NOTE, buf, 0);
		goto bad;
	}

	/*
	 * Everything is cool, put the new proc on the active process list.
	 * It is already on the pid list and in /proc.
	 * Increment the per uid process count (upcount).
	 */
	nproc++;
	upcount_inc(cr->cr_ruid);

	cp->p_next = practive;
	practive->p_prev = cp;
	practive = cp;

	pp = ttoproc(curthread);
	cp->p_ignore = pp->p_ignore;
	cp->p_siginfo = pp->p_siginfo;
	/*
	 * If fork1(), do not inherit SWAITSIG or ASLWP settings of p_flag.
	 * SWAITSIG is not inherited since the child of fork1() has only 1
	 * user thread and the user-level should make an explicit call to
	 * turn it on, if it needs to. Correspondingly, the _sigwaitingset
	 * variable in libthread is turned off in the child of fork1(), in
	 * the wrapper to fork1() in libhread.
	 * The ASLWP flag is not inherited since the child of fork1() has only
	 * one lwp - the aslwp has not yet been cloned. So retaining the flag
	 * without the real aslwp is dangerous. The aslwp is explicitly created
	 * by the child, at user-level, just after fork1() returns, if it needs
	 * to - if it does, the ASLWP flag will be turned on in p_flag at that
	 * time.
	 */
	if (isfork1)
		cp->p_flag = SLOAD | (pp->p_flag & (SJCTL|SNOWAIT));
	else
		cp->p_flag = SLOAD
		    | (pp->p_flag & (SJCTL|SNOWAIT|SWAITSIG|ASLWP));
	if (cp->p_flag & SWAITSIG)
		cp->p_lwpblocked = 0;
	else
		cp->p_lwpblocked = -1;

	cp->p_sessp = pp->p_sessp;
	SESS_HOLD(pp->p_sessp);
	cp->p_exec = pp->p_exec;

	cp->p_brkbase = pp->p_brkbase;
	cp->p_brksize = pp->p_brksize;
	cp->p_stksize = pp->p_stksize;
	cp->p_ppid = pp->p_pid;

	/*
	 * Link up to parent-child-sibling chain.  No need to lock
	 * in general since only a call to freeproc() (done by the
	 * same parent as newproc()) diddles with the child chain.
	 */
	cp->p_sibling = pp->p_child;
	if (pp->p_child)
		pp->p_child->p_psibling = cp;

	cp->p_parent = pp;
	pp->p_child = cp;

	cp->p_child_ns = NULL;
	cp->p_sibling_ns = NULL;

	cp->p_nextorph = pp->p_orphan;
	cp->p_nextofkin = pp;
	pp->p_orphan = cp;

	mutex_exit(&pidlock);

	/*
	 * Duplicate any audit information kept in the process table
	 */
#ifdef C2_AUDIT
	if (audit_active)	/* copy audit data to cp */
		audit_newproc(cp);
#endif

	crhold(cp->p_cred = pp->p_cred);

	/*
	 * Bump up the counts on the file structures pointed at by the
	 * parents ofile table since the child will point at them too.
	 */
	bump_fcnts(pp);

	VN_HOLD(u.u_cdir);
	if (u.u_rdir)
		VN_HOLD(u.u_rdir);

	/*
	 * copy the parent's uarea.
	 */
	uarea = PTOU(cp);
	setuctxt(uarea, isfork1);
	cp->p_flag |= SULOAD;
	uarea->u_start = hrestime.tv_sec;
	uarea->u_ticks = lbolt;
	uarea->u_mem = rm_asrss(pp->p_as);
	uarea->u_nshmseg = 0;
	uarea->u_acflag = AFORK;

	/*
	 * If inherit-on-fork, copy /proc tracing flags to child.
	 */
	if ((pp->p_flag & SPRFORK) != 0) {
		cp->p_flag |= pp->p_flag & (SPROCTR|SPRFORK|SMSACCT);
		cp->p_sigmask = pp->p_sigmask;
		cp->p_fltmask = pp->p_fltmask;
	} else {
		sigemptyset(&cp->p_sigmask);
		premptyset(&cp->p_fltmask);
		uarea->u_systrap = 0;
		premptyset(&uarea->u_entrymask);
		premptyset(&uarea->u_exitmask);
	}

	if (cp->p_exec)
		VN_HOLD(cp->p_exec);
	*cpp = cp;
	return (0);

bad:
	ASSERT(MUTEX_NOT_HELD(&pidlock));

	mutex_destroy(&cp->p_lock);
	mutex_destroy(&cp->p_crlock);
	mutex_destroy(&cp->p_pflock);
#ifdef i386
	mutex_destroy(&cp->p_ldtlock);
#endif
	if (newpid != -1) {
		proc_entry_free(cp->p_pidp);
		pid_rele(cp->p_pidp);
	}
	kmem_free(cp, sizeof (proc_t));

	/*
	 * We most likely got into this situation because some process is
	 * forking out of control.  As punishment, put it to sleep for a
	 * bit so it can't eat the machine alive.  Sleep interval is chosen
	 * to allow no more than one fork failure per cpu per clock tick
	 * on average (yes, I just made this up).  This has two desirable
	 * properties: (1) it sets a constant limit on the fork failure
	 * rate, and (2) the busier the system is, the harsher the penalty
	 * for abusing it becomes.
	 */
	INCR_COUNT(&fork_fail_pending, &pidlock);
	delay(fork_fail_pending / ncpus + 1);
	DECR_COUNT(&fork_fail_pending, &pidlock);

	return (-1); /* out of memory or proc slots */
}


/*
 * Release virtual memory.
 * In the case of vfork(), the child was given exclusive access to its
 * parent's address space.  The parent is waiting in vfwait() for the
 * child to release its exclusive claim via relvm().
 */
void
relvm(proc_t *p)
{
	ASSERT((unsigned)p->p_lwpcnt <= 1);

	if (p->p_flag & SVFORK) {
		proc_t *pp = p->p_parent;
		/*
		 * The child process is either exec'ing or exit'ing.
		 * The child is now separated from the parent's address
		 * space.  The parent process is made dispatchable.
		 *
		 * This is a delicate locking maneuver, involving
		 * both the parent's p_lock and the child's p_lock.
		 * As soon as the SVFORK flag is turned off, the
		 * parent is free to run, but it must not run until
		 * we wake it up using its p_cv because it might
		 * exit and we would be referencing invalid memory.
		 * Therefore, we hold the parent with its p_lock
		 * while protecting our p_flags with our own p_lock.
		 */
		mutex_enter(&p->p_lock);	/* grab child's lock first */
		prbarrier(p);		/* make sure /proc is blocked out */
		mutex_enter(&pp->p_lock);
		p->p_flag &= ~SVFORK;
		p->p_as = &kas;
		/*
		 * child sizes are copied back to parent because
		 * child may have grown.
		 */
		pp->p_brkbase = p->p_brkbase;
		pp->p_brksize = p->p_brksize;
		pp->p_stksize = p->p_stksize;
		cv_signal(&pp->p_cv);
		mutex_exit(&pp->p_lock);
		mutex_exit(&p->p_lock);
	} else {
		if (p->p_as != &kas) {
			struct as *as;

			if (PTOU(p)->u_nshmseg)
				shmexit(p);
			/*
			 * We grab p_lock for the benefit of /proc
			 */
			mutex_enter(&p->p_lock);
			prbarrier(p);	/* make sure /proc is blocked out */
			as = p->p_as;
			p->p_as = &kas;
			mutex_exit(&p->p_lock);
			as_free(as);
		}
	}
}

/*
 * Wait for child to exec or exit.
 * Called by parent of vfork'ed process.
 * See important comments in relvm(), above.
 */
void
vfwait(pid_t pid)
{
	proc_t *pp = ttoproc(curthread);
	proc_t *cp;

	/*
	 * Wait for child to exec or exit.
	 */
	for (;;) {
		mutex_enter(&pidlock);
		cp = prfind(pid);
		if (cp == NULL || cp->p_parent != pp) {
			/*
			 * Child has exit()ed.
			 */
			mutex_exit(&pidlock);
			break;
		}
		/*
		 * Grab the child's p_lock before releasing pidlock.
		 * Otherwise, the child could exit and we would be
		 * referencing invalid memory.
		 */
		mutex_enter(&cp->p_lock);
		mutex_exit(&pidlock);
		if (!(cp->p_flag & SVFORK)) {
			/*
			 * Child has exec()ed or is exit()ing.
			 */
			mutex_exit(&cp->p_lock);
			break;
		}
		mutex_enter(&pp->p_lock);
		mutex_exit(&cp->p_lock);
		/*
		 * We might be waked up spuriously from the cv_wait().
		 * We have to do the whole operation over again to be
		 * sure the child's SVFORK flag really is turned off.
		 * We cannot make reference to the child because it can
		 * exit before we return and we would be referencing
		 * invalid memory.
		 */
		cv_wait(&pp->p_cv, &pp->p_lock);
		mutex_exit(&pp->p_lock);
	}
	mutex_enter(&pp->p_lock);
	continuelwps(pp);
	mutex_exit(&pp->p_lock);
}
