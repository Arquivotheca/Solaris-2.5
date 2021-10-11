/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	Copyright (c) 1994 Sun Microsystems, Inc. */
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)sigaction.c	1.5	95/07/19 SMI"	/* from SVr4.0 1.78 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/errno.h>
#include <sys/proc.h>
#include <sys/fault.h>
#include <sys/signal.h>
#include <sys/siginfo.h>
#include <sys/debug.h>

int
sigaction(int sig, struct sigaction *actp, struct sigaction *oactp)
{
	struct sigaction act;
	struct sigaction oact;
	k_sigset_t set;
	proc_t *p;

	if (sig <= 0 || sig >= NSIG ||
	    (actp != NULL && sigismember(&cantmask, sig)))
		return (set_errno(EINVAL));

	/*
	 * act and oact might be the same address, so copyin act first.
	 */
	if (actp) {
		register void (*handler)();

		if (copyin((caddr_t)actp, (caddr_t)&act, sizeof (act)))
			return (set_errno(EFAULT));
#ifdef	sparc
		/*
		 * Check alignment of handler
		 */
		handler = act.sa_handler;
		if (handler != SIG_IGN && handler != SIG_DFL &&
		    ((int)handler & 0x3) != 0)
			return (set_errno(EINVAL));
#endif
	}

	p = curproc;
	mutex_enter(&p->p_lock);

	if (oactp) {
		register flags;
		register void (*disp)();

		disp = u.u_signal[sig - 1];

		flags = 0;
		if (disp != SIG_DFL && disp != SIG_IGN) {
			set = u.u_sigmask[sig-1];
			if (sigismember(&p->p_siginfo, sig))
				flags |= SA_SIGINFO;
			if (sigismember(&u.u_sigrestart, sig))
				flags |= SA_RESTART;
			if (sigismember(&u.u_sigonstack, sig))
				flags |= SA_ONSTACK;
			if (sigismember(&u.u_sigresethand, sig))
				flags |= SA_RESETHAND;
			if (sigismember(&u.u_signodefer, sig))
				flags |= SA_NODEFER;
		} else
			sigemptyset(&set);

		switch (sig) {
		case SIGCLD:
			if (p->p_flag & SNOWAIT)
				flags |= SA_NOCLDWAIT;
			if (!(p->p_flag & SJCTL))
				flags |= SA_NOCLDSTOP;
			break;
		case SIGWAITING:
			if (p->p_flag & SWAITSIG)
				flags |= SA_WAITSIG;
			break;
		}

		oact.sa_handler = disp;
		oact.sa_flags = flags;
		sigktou(&set, &oact.sa_mask);
	}

	if (actp) {
		if (sig == SIGCLD &&
			act.sa_handler != SIG_IGN &&
			act.sa_handler != SIG_DFL) {
				register proc_t *cp;
				/*
				 * Lock ordering requires us to acquire
				 * pidlock and p->p_lock in this order.
				 */
				mutex_exit(&p->p_lock);
				mutex_enter(&pidlock);
				mutex_enter(&p->p_lock);
				for (cp = p->p_child; cp; cp = cp->p_sibling) {
					if (cp->p_stat == SZOMB) {
					    if (p->p_aslwptp != NULL) {
						sigaddset(&p->p_aslwptp->t_sig,
						    SIGCLD);
						cv_signal(&p->p_notifcv);
					    } else
						sigaddset(&p->p_sig, SIGCLD);
					    break;
					}
				}
				mutex_exit(&pidlock);
		}

		sigutok(&act.sa_mask, &set);
		setsigact(sig, act.sa_handler, set, act.sa_flags);
	}

	mutex_exit(&p->p_lock);

	if (oactp &&
	    copyout((caddr_t)&oact, (caddr_t)oactp, sizeof (oact)))
		return (set_errno(EFAULT));

	return (0);
}
