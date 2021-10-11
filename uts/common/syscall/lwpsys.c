/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	Copyright (c) 1994 Sun Microsystems, Inc. */
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)lwpsys.c	1.7	95/07/19 SMI"	/* from SVr4.0 1.78 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/cred.h>
#include <sys/errno.h>
#include <sys/proc.h>
#include <sys/signal.h>
#include <sys/kmem.h>
#include <sys/unistd.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>

kthread_t *
idtot(kthread_t *head, int id)
{
	kthread_t *t;

	if (id == 0)
		return (curthread);
	t = head;
	do {
		if (t->t_tid == id)
			return (t);
	} while (head != (t = t->t_forw));
	return ((kthread_t *)NULL);
}

/*
 * Stop an lwp of the current process
 */
int
syslwp_suspend(int lwpid)
{
	kthread_id_t t;
	int error;
	proc_t *p = ttoproc(curthread);

	mutex_enter(&p->p_lock);
	if (lwpid == 0)
		t = curthread;
	else if ((t = idtot(p->p_tlist, lwpid)) == (kthread_id_t)NULL) {
		mutex_exit(&p->p_lock);
		return (set_errno(ESRCH));
	}
	error = lwp_suspend(t);
	mutex_exit(&p->p_lock);
	if (error) {
		return (set_errno(error));
	} else {
		return (0);
	}
}

int
syslwp_continue(int lwpid)
{
	kthread_id_t t;
	proc_t *p = ttoproc(curthread);

	mutex_enter(&p->p_lock);
	if (((t = idtot(p->p_tlist, lwpid)) == (kthread_id_t)NULL)) {
		mutex_exit(&p->p_lock);
		return (set_errno(ESRCH));
	}
	lwp_continue(t);
	mutex_exit(&p->p_lock);
	return (0);
}

int
lwp_kill(int lwpid, int sig)
{
	sigqueue_t *sqp;
	kthread_t *t;
	proc_t *p = ttoproc(curthread);

	if (sig < 0 || sig >= NSIG)
		return (set_errno(EINVAL));
	if (sig != 0)
		sqp = (sigqueue_t *)kmem_zalloc(sizeof (sigqueue_t), KM_SLEEP);
	mutex_enter(&p->p_lock);
	if ((t = idtot(p->p_tlist, lwpid)) == (kthread_t *)NULL) {
		mutex_exit(&p->p_lock);
		if (sig == 0)
			return (set_errno(ESRCH));
		kmem_free((caddr_t)sqp, sizeof (sigqueue_t));
		return (set_errno(EINVAL));
	}
	if (sig == 0) {
		mutex_exit(&p->p_lock);
		return (0);
	}
	sqp->sq_info.si_signo = sig;
	sqp->sq_info.si_code = SI_LWP;
	sqp->sq_info.si_pid = p->p_pid;
	sqp->sq_info.si_uid = CRED()->cr_ruid;
	sigaddqa(p, t, sqp);
	mutex_exit(&p->p_lock);
	return (0);
}

int
lwp_wait(int lwpid, int *departed)
{
	extern kmutex_t reaplock;
	kthread_t *t;
	proc_t *p = curproc;
	int zombcnt;
	int found;
	int error = 0;

	extern void deathrow_enq(kthread_t *);

	mutex_enter(&p->p_lock);
	if (lwpid == 0) {
		found = 0;
		for (;;) {
/*
	XXX
			if (p->p_lwpcnt == 1 && !p->p_zombcnt) {
				mutex_exit(&p->p_lock);
				return (set_errno(EINVAL));
			}
*/
			t = p->p_zomblist;
			zombcnt = p->p_zombcnt;
			while (zombcnt-- > 0) {
				if (!(t->t_proc_flag & TP_WAITFOR)) {
					found = 1;
					break;
				}
				t = t->t_forw;
			}
			if (found)
				break;
			if (!cv_wait_sig(&p->p_lwpexit, &p->p_lock)) {
				mutex_exit(&p->p_lock);
				return (set_errno(EINTR));
			}
		}
	} else {
		t = idtot(p->p_tlist, lwpid);
		/* check zombie list if not on process's p_tlist. */
		if (t == NULL) {
			error = ESRCH;
			if ((t = p->p_zomblist) != NULL) {
				zombcnt = p->p_zombcnt;
				while (zombcnt-- > 0) {
					if (t->t_tid == lwpid) {
						error = 0;
						break;
					}
					t = t->t_forw;
				}
			}
		} else if (t == curthread) {
			error = EDEADLK;
		} else if (!(t->t_proc_flag & TP_TWAIT)) {
			error = ESRCH;
		}
		if (!error && (t->t_proc_flag & TP_WAITFOR))
			error = ESRCH;
		if (error) {
			mutex_exit(&p->p_lock);
			return (set_errno(error));
		}
		t->t_proc_flag |= TP_WAITFOR;
		while (!(t->t_flag & T_LWPZOMB)) {
			if (!cv_wait_sig(&p->p_lwpexit, &p->p_lock)) {
				mutex_exit(&p->p_lock);
				return (set_errno(EINTR));
			}
		}
	}
	/*
	 * cleanup the zombie LWP. remove it from the process's
	 * zombie list and put in onto death-row.
	 */
	if (--p->p_zombcnt == 0)
		p->p_zomblist = NULL;
	else {
		t->t_forw->t_back = t->t_back;
		t->t_back->t_forw = t->t_forw;
		if (t == p->p_zomblist)
			p->p_zomblist = t->t_forw;
	}
	mutex_exit(&p->p_lock);

	/*
	 * Wait for thread to be off the processor and in TS_FREE state.
	 */
	lock_set(&t->t_lock);
	t->t_forw = NULL;
	t->t_back = NULL;
	mutex_enter(&reaplock);
	ASSERT(t->t_state == TS_FREE);
	deathrow_enq(t);
	mutex_exit(&reaplock);

	if (departed) {
		if (copyout((char *)&t->t_tid, (char *)departed, sizeof (int)))
			return (set_errno(EFAULT));
	}
	return (0);
}

/*
 * This routine may be replaced by a modified sigaddqa() routine.
 * It is essentially the same, except that it does not call
 * sigtoproc() and does not treat job control specially.
 */
void
bsigaddqa(proc_t *p, kthread_t *t, sigqueue_t *sigqp)
{
        register sigqueue_t **psqp;
        register int sig = sigqp->sq_info.si_signo;

	ASSERT(sig >= 1 && sig < NSIG);
        psqp = &t->t_sigqueue;
        if (sigismember(&p->p_siginfo, sig) &&
                        (sigqp->sq_info.si_code == SI_QUEUE)) {
                for (; *psqp != NULL; psqp = &(*psqp)->sq_next)
                                ;
        } else {
                for (; *psqp != NULL; psqp = &(*psqp)->sq_next)
                        if ((*psqp)->sq_info.si_signo == sig) {
                                if (sigqp->sq_func != NULL)
                                        (sigqp->sq_func)(sigqp);
                                else
                                        kmem_free(sigqp, sizeof (sigqueue_t));
                                return;
                        }
        }
        *psqp = sigqp;
        sigqp->sq_next = NULL;
}

int
lwp_sigredirect(int lwpid, int sig)
{
	proc_t *p = curproc;
	kthread_t *target = NULL;
	kthread_t *ast = NULL;
	sigqueue_t *qp;

	if (sig <= 0 || sig >= NSIG)
		return (set_errno(EINVAL));
	mutex_enter(&p->p_lock);
	if (!sigismember(&p->p_notifsigs, sig)) {
		mutex_exit(&p->p_lock);
		return(set_errno(EINVAL));
	}
	if ((target = idtot(p->p_tlist, lwpid)) == NULL) {
		mutex_exit(&p->p_lock);
		return(set_errno(ESRCH));
	}
	sigdelset(&p->p_notifsigs, sig);
	sigaddset(&target->t_sig, sig);
	ast = p->p_aslwptp;
	if (sigdeq(p, ast, sig, &qp) == 1) {
		/*
		 * If there is a signal queued up after this one, notification
		 * for this new signal needs to be sent up - wake up the aslwp.
		 */
		ASSERT(qp != NULL);
		cv_signal(&p->p_notifcv);
	} else if (sigismember(&ast->t_sig, sig))
		/*
		 * If, after this signal was put into the process notification
		 * set, p_notifsigs, and deleted from the aslwp, another,
		 * non-queued signal was added to the aslwp, then wake-up
		 * the aslwp to do the needful.
		 */
		cv_signal(&p->p_notifcv);
	if (qp != NULL)
		bsigaddqa(p, target, qp);
	thread_lock(target);
	eat_signal(target, sig);
	thread_unlock(target);
	mutex_exit(&p->p_lock);
	return (0);
}
