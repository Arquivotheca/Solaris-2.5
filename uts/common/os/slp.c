/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)slp.c	1.48	94/09/29 SMI"	/* from SVr4.0 1.31 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/pcb.h>
#include <sys/signal.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/cpuvar.h>
#include <sys/systm.h>
#include <sys/sysinfo.h>
#include <sys/map.h>
#include <sys/vfs.h>
#include <sys/cred.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/inline.h>
#include <sys/disp.h>
#include <sys/class.h>
#include <sys/fault.h>
#include <sys/syscall.h>
#include <sys/ucontext.h>
#include <sys/prsystm.h>
#include <sys/priocntl.h>
#include <sys/vtrace.h>
#include <sys/tblock.h>
#include <sys/sleepq.h>

extern kmutex_t	unsafe_driver;

static void
slp_cv_wait(caddr_t wchan, kmutex_t *lp)
{
	thread_lock(curthread);		/* lock the thread */
	t_block_chan(wchan);		/* block on wchan */
	thread_unlock_nopreempt(curthread);	/* unlock the sleepq bucket */
	mutex_exit(lp);
	swtch();
	mutex_enter(lp);
}

static int
slp_cv_wait_sig(caddr_t wchan, kmutex_t *lp)
{
	register kthread_t *t = curthread;
	register proc_t *p = ttoproc(t);
	register klwp_t *lwp = ttolwp(t);
	int rval = 1;

	if (lwp == NULL) {
		slp_cv_wait(wchan, lp);
		return (rval);
	}

	lwp->lwp_asleep = 1;
	lwp->lwp_sysabort = 0;
	if (sigwaiting_check(lp) == 0) {
		thread_lock(t);
		t_block_sig_chan(wchan);
		thread_unlock_nopreempt(curthread);
		mutex_exit(lp);
		if (ISSIG(t, JUSTLOOKING) || ISHOLD(p))
			setrun(t);
		swtch();
		t->t_flag &= ~T_WAKEABLE;
		sigwaiting_decr();
		mutex_enter(lp);
	}
	if (ISSIG_PENDING(t, lwp, p)) {
		mutex_exit(lp);
		if (issig(FORREAL))
			rval = 0;
		mutex_enter(lp);
	}
	if (lwp->lwp_sysabort || ISHOLD(p))
		rval = 0;
	lwp->lwp_asleep = 0;
	lwp->lwp_sysabort = 0;
	return (rval);
}


/*
 * Give up the processor till a wakeup occurs
 * on chan. The disp argument determines whether
 * the sleep can be interrupted by a signal. If
 * disp & PMASK <= PZERO the SNWAKE bit in p_flag
 * is set and a signal cannot disturb the sleep;
 * if disp & PMASK > PZERO signals will be processed.
 * Callers of this routine must be prepared for
 * premature return, and check that the reason for
 * sleeping has gone away.
 */

int
sleep(caddr_t chan, int disp)
{
	register klwp_t		*lwp = ttolwp(curthread);
	short			dispwt;
	int			interruptible;
	extern pri_t		ts_maxkmdpri;

	if (panicstr) {
		panic_hook();
		return (0);
	}

	ASSERT(chan != 0);
	if (UNSAFE_DRIVER_LOCK_NOT_HELD())
		cmn_err(CE_PANIC, "sleep() called from an MT-safe driver");

	interruptible = ((disp & PMASK) > PZERO) | (disp & PCATCH);

	if (panicstr) {
		return (0);
	}


	dispwt = ts_maxkmdpri - (short)(disp & PMASK);
	if (dispwt < 0)
		dispwt = 0;
	if (interruptible) {
		/*
		 * Call class specific function to do whatever it deems
		 * appropriate before we give up the processor.
		 */
		if (!slp_cv_wait_sig(chan, &unsafe_driver))
			goto psig;
	} else {
		slp_cv_wait(chan, &unsafe_driver);
	}
out:
	return (0);

	/*
	 * If priority was low (>PZERO) and there has been a signal,
	 * then if PCATCH is set return 1, otherwise do a non-local
	 * jump to the qsav location.
	 */
psig:
	if (disp & PCATCH)
		return (1);
	/*
	 * Long jump back to the unsafe driver wrapper.
	 * This assumes that MT-unsafe drivers don't use
	 * lwp_qsav for their own (internal) purposes.
	 */
	mutex_exit(&unsafe_driver);
	longjmp(&lwp->lwp_qsav);
	/* NOTREACHED */
}

/*
 * Set the thread running; arrange for it to be swapped in if necessary.
 */
void
setrun_locked(kthread_id_t t)
{
	ASSERT(THREAD_LOCK_HELD(t));
	if (t->t_state == TS_SLEEP) {
		/*
		 * Take off sleep queue.
		 */
		SOBJ_UNSLEEP(t->t_sobj_ops, t);
	} else if (t->t_state & (TS_RUN | TS_ONPROC)) {
		/*
		 * Already on dispatcher queue.
		 */
		return;
	} else if (t->t_state == TS_STOPPED) {
		register proc_t *p = ttoproc(t);

		/*
		 * All of the sending of SIGCONT (TC_XSTART) and /proc
		 * (TC_PSTART) and lwp_continue() (TC_CSTART) must have
		 * requested that the thread be run.
		 * Just calling setrun() is not sufficient to set a stopped
		 * thread running.  TP_TXSTART is always set if the thread
		 * is not stopped by a jobcontrol stop signal.
		 * TP_TPSTART is always set if /proc is not controlling it.
		 * TP_TCSTART is always set if lwp_stop() didn't stop it.
		 * The thread won't be stopped unless one of these
		 * three mechanisms did it.
		 *
		 * These flags must be set before calling setrun_locked(t).
		 * They can't be passed as arguments because the streams
		 * code calls setrun() indirectly and the mechanism for
		 * doing so admits only one argument.  Note that the
		 * thread must be locked in order to change t_schedflags.
		 */
		if ((t->t_schedflag & TS_ALLSTART) != TS_ALLSTART) {
			return;
		}

		/*
		 * Process is no longer stopped (a thread is running).
		 * Make sure wait(2) will not see it until it stops again.
		 */
/* XXX - locking on p_wcode and p_wdata fields? */
		p->p_wcode = 0;
		p->p_wdata = 0;
		t->t_whystop = 0;
		t->t_whatstop = 0;
		/*
		 * Strictly speaking, we do not have to clear these
		 * flags here; they are cleared on entry to stop().
		 * However, they are confusing when doing kernel
		 * debugging or when they are revealed by ps(1).
		 */
		t->t_schedflag &= ~TS_ALLSTART;
		THREAD_TRANSITION(t);	/* drop stopped-thread lock */
		ASSERT(t->t_lockp == &transition_lock);
		ASSERT(t->t_wchan0 == 0 && t->t_wchan == NULL);
		/*
		 * Let the class put the process on the dispatcher queue.
		 */
		CL_SETRUN(t);
	}


}

void
setrun(kthread_id_t t)
{
	thread_lock(t);
	setrun_locked(t);
	thread_unlock(t);
}

void
wakeup(caddr_t chan)
{
	t_release_all_chan(chan);
}
