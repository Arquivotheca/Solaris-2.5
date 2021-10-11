/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	Copyright (c) 1994 Sun Microsystems, Inc. */
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)lwp_create.c	1.9	95/09/27 SMI"	/* from SVr4.0 1.78 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/syscall.h>
#include <sys/proc.h>
#include <sys/processor.h>
#include <sys/fault.h>
#include <sys/ucontext.h>
#include <sys/signal.h>
#include <sys/unistd.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>

/*
 * A process can create a special lwp, the "aslwp", to take signals sent
 * asynchronously to this process. The "aslwp" (short for Asynchronous Signals'
 * lwp) is like a daemon lwp within this process and it is the first recipient
 * of any signal sent asynchronously to the containing process. The aslwp is
 * created via a new, reserved flag (__LWP_ASLWP) to _lwp_create(2). Currently
 * only an MT process, i.e. a process linked with -lthread, creates such an lwp.
 * At user-level, "aslwp" is usually in a "sigwait()", waiting for all signals.
 * The aslwp is set up by calling setup_aslwp() from syslwp_create().
 */
static void
setup_aslwp(kthread_t *t)
{
	proc_t *p = ttoproc(t);

	ASSERT(MUTEX_HELD(&p->p_lock));
	ASSERT((p->p_flag & ASLWP) == 0 && p->p_aslwptp == NULL);
	p->p_flag |= ASLWP;
	p->p_aslwptp = t;
	/*
	 * Since "aslwp"'s thread pointer has just been advertised above, it
	 * is impossible for it to have received any signals directed via
	 * sigtoproc(). They are all in p_sig and the sigqueue is all in
	 * p_sigqueue.
	 */
	ASSERT(sigisempty(&t->t_sig));
	ASSERT(t->t_sigqueue == NULL);
	t->t_sig = p->p_sig;
	sigemptyset(&p->p_sig);
	t->t_sigqueue = p->p_sigqueue;
	p->p_sigqueue = NULL;
	/*
	 * initialize binding state - might have been initialized
	 * inappropriately in "lwp_create()".
	 */
	t->t_bind_cpu = PBIND_NONE;
	t->t_bound_cpu = 0;
}

/*
 * This structure is used only for copying the arguments
 * into the new LWP's arg area for the benefit of debuggers.
 */
struct lwp_createa {
	ucontext_t *ucp;
	int	flags;
	int	*new_lwp;
};

/*
 * Create a lwp.
 */
int
syslwp_create(ucontext_t *ucp, int flags, int *new_lwp)
{
	klwp_id_t lwp;
	proc_t *p = ttoproc(curthread);
	kthread_t *t;
	ucontext_t uc;
	k_sigset_t sigmask;
	int	tid;
	struct lwp_createa *ap;

	extern void lwp_rtt();

	if (copyin((caddr_t)ucp, (caddr_t)&uc, sizeof (ucontext_t)))
		return (set_errno(EFAULT));

	save_syscall_args();		/* save args for tracing first */
	sigutok(&uc.uc_sigmask, &sigmask);
	lwp = lwp_create(lwp_rtt, NULL, NULL, curproc, TS_STOPPED,
		curthread->t_pri, sigmask, curthread->t_cid);
	if (lwp == NULL)
		return (set_errno(EAGAIN));

	lwp_load(lwp, &uc);

	t = lwptot(lwp);
	/*
	 * copy new LWP's lwpid_t into the caller's specified buffer.
	 */
	if (new_lwp) {
		if (copyout((char *)&t->t_tid, (char *)new_lwp, sizeof (int))) {
			/*
			 * caller's buffer is not writable, return
			 * EFAULT, and terminate new LWP.
			 */
			mutex_enter(&p->p_lock);
			t->t_proc_flag |= TP_EXITLWP;
			t->t_sig_check = 1;
			t->t_sysnum = 0;
			lwp_continue(t);
			mutex_exit(&p->p_lock);
			return (set_errno(EFAULT));
		}
	}

	mutex_enter(&p->p_lock);
	/*
	 * Copy the syscall arguments to the new lwp's arg area
	 * for the benefit of debuggers.
	 */
	t->t_sysnum = SYS_lwp_create;
	ap = (struct lwp_createa *)lwp->lwp_arg;
	lwp->lwp_ap = (int *)ap;
	ap->ucp = ucp;
	ap->flags = flags;
	ap->new_lwp = new_lwp;

	/*
	 * If we are creating the aslwp, do some checks then set it up.
	 */
	if (flags & __LWP_ASLWP) {
		if (p->p_flag & ASLWP) {
			/*
			 * There is already an aslwp.
			 * Return EINVAL and terminate the new LWP.
			 */
			t->t_proc_flag |= TP_EXITLWP;
			t->t_sig_check = 1;
			t->t_sysnum = 0;
			lwp_continue(t);
			mutex_exit(&p->p_lock);
			return (set_errno(EINVAL));
		}
		setup_aslwp(t);
	}

	if (!(flags & LWP_DETACHED))
		t->t_proc_flag |= TP_TWAIT;

	tid = (int)t->t_tid;	/* for debugger */

	if ((flags & LWP_SUSPENDED))
		t->t_proc_flag |= TP_HOLDLWP;	/* create suspended */
	else
		lwp_continue(t);		/* start running */

	mutex_exit(&p->p_lock);

	return (tid);
}

/*
 * Exit the calling lwp
 */
void
syslwp_exit()
{
	proc_t *p = ttoproc(curthread);

	mutex_enter(&p->p_lock);
	lwp_exit();
	/* NOTREACHED */
}
