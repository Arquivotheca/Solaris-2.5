/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	Copyright (c) 1994 Sun Microsystems, Inc. */
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)sigtimedwait.c	1.10	95/05/01 SMI"	/* from SVr4.0 1.78 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/bitmap.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/errno.h>
#include <sys/proc.h>
#include <sys/fault.h>
#include <sys/procset.h>
#include <sys/signal.h>
#include <sys/siginfo.h>
#include <sys/time.h>
#include <sys/kmem.h>
#include <sys/debug.h>
#include <sys/condvar_impl.h>

#define	sigandset(x, y, z) (\
	(x)->__sigbits[0] = (y)->__sigbits[0] & (z)->__sigbits[0]; \
	(x)->__sigbits[1] = (y)->__sigbits[1] & (z)->__sigbits[1];

#define	getnotifysigs(rs, ns, ts)	(\
	(rs)->__sigbits[0] = ~((ns)->__sigbits[0]) & (ts)->__sigbits[0], \
	(rs)->__sigbits[1] =  ~((ns)->__sigbits[1]) & (ts)->__sigbits[1])

/*
 * Wait until a signal within a specified set is posted or until the
 * time interval 'timeout' if specified.  The signal is caught but
 * not delivered. The value of the signal is returned to the caller.
 */
int
sigtimedwait(sigset_t *setp, siginfo_t *siginfop, timespec_t *timeoutp)
{
	sigset_t set;
	k_sigset_t kset;
	k_sigset_t oldmask;
	k_sigset_t sigs;
	proc_t *p = curproc;
	kthread_t *t = curthread;
	klwp_t *lwp = ttolwp(curthread);
	timespec_t sig_timeout, now;
	long sig_time = 0;
	int ret = 0;
	k_siginfo_t info, *infop;
	int sig;
	kthread_t *ast;
	sigqueue_t *qp = NULL;

	if (copyin((caddr_t)setp, (caddr_t)&set, sizeof (sigset_t)))
		return (set_errno(EFAULT));
	sigutok(&set, &kset);
	sigdiffset(&kset, &cantmask);
	if (sigisempty(&kset))
		return (set_errno(EINVAL));
	if (timeoutp) {
		if (copyin((caddr_t)timeoutp, (caddr_t)&sig_timeout,
		    sizeof (timespec_t)))
			return (set_errno(EFAULT));
		if (itimerspecfix(&sig_timeout))
			return (set_errno(EINVAL));
		/*
		 * Convert the timespec value into lbolt equivalent
		 */
		if (timerspecisset(&sig_timeout)) {
			gethrestime(&now);
			timespecadd(&sig_timeout, &now);
			sig_time = timespectohz(&sig_timeout, now);
			/*
			 * Make sure sig_time + lbolt isn't negative.
			 */
			if (sig_time < (LONG_MAX - lbolt))
				sig_time += lbolt;
		}
	}
	mutex_enter(&p->p_lock);
	/*
	 * set the thread's signal mask to unmask
	 * those signals in the specified set.
	 */
	oldmask = t->t_hold;
	sigdiffset(&t->t_hold, &kset);
	if ((ast = p->p_aslwptp) != NULL) {
		sig = fsig(&p->p_notifsigs, t);
		if (sig > 0) {
			ret = sig;
			sigdelset(&p->p_notifsigs, sig);
			if (sigdeq(p, ast, sig, &qp) == 1)
				cv_signal(&p->p_notifcv);
			else if (sigismember(&ast->t_sig, sig))
				cv_signal(&p->p_notifcv);
			t->t_hold = oldmask;
			mutex_exit(&p->p_lock);
			if (qp != NULL) {
				infop = &qp->sq_info;
				if (siginfop && copyout((caddr_t)infop,
				    (caddr_t)siginfop, sizeof (k_siginfo_t)))
					ret = set_errno(EFAULT);
				if (qp->sq_func != NULL)
					(qp->sq_func)(qp);
				else
					kmem_free((caddr_t)qp,
					    sizeof (sigqueue_t));
			}
			return (ret);
		}
	}
	if (timeoutp) {
		while ((ret = cv_timedwait_sig(&u.u_cv, &p->p_lock,
		    sig_time)) > 0)
			;
	} else {
		while (ret = cv_wait_sig_swap(&u.u_cv, &p->p_lock))
			;
	}

	/*
	 * restore thread's signal mask to its previous value.
	 */
	t->t_hold = oldmask;
	aston(t);		/* so post_syscall sees new t_hold mask */

	mutex_exit(&p->p_lock);
	if (ret == -1)
		return (set_errno(EAGAIN));	/* timer expired */
	/*
	 * Don't bother with signal if it is not in request set.
	 */
	if (lwp->lwp_cursig == 0 || !sigismember(&kset, lwp->lwp_cursig)) {
		/*
		 * lwp_cursig is zero if pokelwps() awakened cv_wait_sig().
		 * this happens if some LWP in this process has forked or
		 * exited.
		 */
		return (set_errno(EINTR));
	}

	if (lwp->lwp_curinfo)
		infop = &lwp->lwp_curinfo->sq_info;
	else {
		infop = &info;
		infop->si_signo = lwp->lwp_cursig;
		infop->si_code = SI_NOINFO;
	}
	ret = lwp->lwp_cursig;
	lwp->lwp_cursig = 0;

	if (siginfop && copyout((caddr_t)infop, (caddr_t)siginfop,
	    sizeof (k_siginfo_t)))
		ret = set_errno(EFAULT);
	if (lwp->lwp_curinfo) {
		sigqueue_t *sqp = lwp->lwp_curinfo;
		if (sqp->sq_func != NULL)
			(sqp->sq_func)(sqp);
		else
			kmem_free((caddr_t)sqp, sizeof (sigqueue_t));
		lwp->lwp_curinfo = NULL;
	}
	return (ret);
}

int
fsignum(k_sigset_t *s)
{
	register int i;

	for (i = 0; i < sizeof (k_sigset_t)/sizeof (u_long); i++) {
		if (s->__sigbits[i])
			return ((i * BT_NBIPUL) + lowbit(s->__sigbits[i]));
	}
	return (0);
}

/*
 * Wait for notification from the kernel of a signal that has been sent to the
 * process. The signal is not "delivered", due to changes in issig_forreal()
 * to not deliver it to the aslwp. Since this function can be called only by
 * the aslwp, the issig_forreal() changes for the aslwp should be OK.
 * The routine may return -1 and sets errno to EINTR if the wait was interrupted
 * by a fork() from some other LWP in the process. Or the EXITLWPS p_flag was
 * set. Otherwise, on success, it will return the signal number for the
 * signal whose notification has arrived at the process.
 */
int
signotifywait()
{

	sigset_t set;
	k_sigset_t kset;
	k_sigset_t oldmask;
	timespec_t sig_timeout, now;
	long sig_time = 0;
	proc_t *p = curproc;
	k_sigset_t notifysigs;
	register kthread_t *t = curthread;
	int sig = 0;

	/*
	 * Only the aslwp can make this system call.
	 */
	if (curthread != p->p_aslwptp)
		return (set_errno(EINVAL));
	mutex_enter(&p->p_lock);
	/*
	 * set the thread's signal mask to unmask all signals.
	 */
	oldmask = t->t_hold;
	sigemptyset(&t->t_hold);
	do {
		getnotifysigs(&notifysigs, &p->p_notifsigs, &t->t_sig);
		if (!sigisempty(&notifysigs))
			break;
		else {
			/*
			 * Mask all signals in notification set, so that, in
			 * case the same signal number appears in both the
			 * notification set and t_sig, it does not cause the
			 * aslwp to spin in the following loop:
				do {
					getnotifysigs();
					if (sigisempty())
						cv_wait_sig_swap();
				} while (1);
			 * leaving the signal in t_sig and not blocked in t_hold
			 * will force a wake-up of cv_wait_sig_swap(). The aslwp
			 * will then call getnotifysigs() again, to return an
			 * empty set again and so call cv_wait_sig_swap...
			 */
			t->t_hold = p->p_notifsigs;
			if (cv_wait_sig_swap(&p->p_notifcv, &p->p_lock) == 0) {
				/*
				 * cv_wait() above was interrupted by a
				 * significant event such as EXITLWPS being set
				 * in p_flag, etc. The return code of 0 from
				 * cv_wait() above indicates that such an event
				 * occurred. Return from signotifywait() in this
				 * case.
				 */
				t->t_hold = oldmask;
				mutex_exit(&p->p_lock);
				return (set_errno(EINTR));
			}
			/*
			 * Do not need to restore t_hold after the wake-up
			 * since the next change to t_hold is an assignment.
			 */
		}
	} while (1);
	ASSERT(!sigisempty(&notifysigs));
	sig = fsignum(&notifysigs);
	ASSERT(sig > 0);
	sigdelset(&t->t_sig, sig);
	sigaddset(&p->p_notifsigs, sig);
	t->t_hold = oldmask;
	mutex_exit(&p->p_lock);
	return (sig);
}
