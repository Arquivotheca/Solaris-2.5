/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)lwp_sobj.c	1.6	95/08/08 SMI"	/* from SVr4.0 1.78 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/cred.h>
#include <sys/user.h>
#include <sys/errno.h>
#include <sys/file.h>
#include <sys/proc.h>
#include <sys/prsystm.h>
#include <sys/sobject.h>
#include <sys/fault.h>
#include <sys/time.h>
#include <sys/cmn_err.h>
#include <sys/machlock.h>
#include <sys/debug.h>
#include <sys/synch.h>
#include <sys/synch32.h>
#include <sys/mman.h>

#include <vm/as.h>
#include <vm/seg.h>
#include <vm/seg_vn.h>
#include <vm/anon.h>

static kthread_t * lwpsobj_owner(caddr_t);
static void lwp_unsleep(kthread_id_t t);
static void lwp_changepri(kthread_t *t, pri_t pri);

/*
 * Operations vector for threads sleeping on a
 * user-level s-object.
 */
static sobj_ops_t	lwp_sops = {
	"User-Level s-object",
	SOBJ_USER,
	QOBJ_DEF,
	lwpsobj_owner,
	lwp_unsleep,
	lwp_changepri
};

static sleepq_head_t	lwpsleepq[NSLEEPQ];

static sleepq_head_t *
lwpsqhash(quad *lwpchan)
{
	return (&lwpsleepq[(lwpchan->val[0] >> 8) & (NSLEEPQ - 1)]);
}

#ifdef DEBUG
int
sanelwpslpq()
{
	register kthread_id_t	nt;	/* next thread on sleep q */
	register sleepq_t	*sqp;
	register sleepq_head_t	*tsqp;
	register sleepq_head_t	*lwpsqp;
	register disp_lock_t	*qlock;
	int i;

	for (i = 0; i < NSLEEPQ; i++) {
		lwpsqp = &lwpsleepq[i];
		sqp = &lwpsqp->sq_queue;
		qlock = &lwpsqp->sq_lock;
		disp_lock_enter(qlock);
		if ((nt = sqp->sq_first) != NULL) {
			while (nt) {
				if (nt->t_state != TS_SLEEP) {
					disp_lock_exit(qlock);
					return ((int)nt);
				}
				if (nt->t_wchan0 == 0) {
					disp_lock_exit(qlock);
					return ((int)nt);
				}
				tsqp = lwpsqhash((quad *)&nt->t_wchan0);
				if (&tsqp->sq_queue != sqp) {
					disp_lock_exit(qlock);
					return ((int)nt);
				}
				nt = nt->t_link;
			}
		}
		disp_lock_exit(qlock);
	}
	return (0);
}
#endif /* DEBUG */

/*
 * Return a unique pair of identifiers (usually vnode/offset) that corresponds
 * to 'addr'.
 */
static int
get_lwpchan(struct as *as, caddr_t addr, int type, quad *lwpchan)
{
	struct seg *seg;
	struct segvn_data *svd;
	int offset;

	/*
	 * if LWP synch object was defined to be local to this
	 * process, it's type field is set to zero. The first
	 * word of the lwpchan is curproc and the second word
	 * is the synch object's virtual address.
	 */
	if (type == 0) {
		lwpchan->val[0] = (long)curproc;
		lwpchan->val[1] = (long)addr;
		return (1);
	}
top:
	AS_LOCK_ENTER(as, &as->a_lock, RW_READER);
	seg = as_segat(as, addr);
	if (seg == NULL) {
		AS_LOCK_EXIT(as, &as->a_lock);
		return (0);
	}
	SEGOP_GETVP(seg, addr, (struct vnode **)&lwpchan->val[0]);
	/*
	 * If there is no vnode associated with this addr, it must
	 * be anonymous memory from /dev/zero, tmpfs, shared mem, etc.
	 * Use the addr of the anon struct as the identifier in this case.
	 */
	if (lwpchan->val[0] == 0 || SEGOP_GETTYPE(seg, addr) == MAP_PRIVATE) {
		struct anon_map *amp;
		struct anon *ap = NULL;
		/*
		 * N.B.
		 * Assume address refers to an anonymous segvn
		 * page. This is not a general solution.
		 */
		svd = (struct segvn_data *)seg->s_data;
		SEGVN_LOCK_ENTER(as, &svd->lock, RW_READER);
		if ((amp = svd->amp) != NULL)
			ap = amp->anon[svd->anon_index + seg_page(seg, addr)];
		SEGVN_LOCK_EXIT(as, &svd->lock);

		/*
		 * We may have to force an anon struct to be allocated for
		 * this addr.
		 */
		if (amp == NULL || ap == NULL) {
			AS_LOCK_EXIT(as, &as->a_lock);
			if (as_fault(as->a_hat, as, (caddr_t)addr,
			    sizeof (int), F_INVAL, S_WRITE) != 0)
				return (0);
			goto top;
		}
		lwpchan->val[0] = (long)ap;
		lwpchan->val[1] = ((long)addr & PAGEOFFSET);
		ASSERT(lwpchan->val[0] != 0);
	} else {
		offset = SEGOP_GETOFFSET(seg, addr);
		lwpchan->val[1] = offset + (addr - seg->s_base);
	}
	AS_LOCK_EXIT(as, &as->a_lock);
	return (1);
}

static void
lwp_block(quad *lwpchan)
{
	register klwp_t *lwp = ttolwp(curthread);
	register sleepq_head_t *sqh;

	/*
	 * Put the lwp in an orderly state for debugging,
	 * just as though it stopped on a /proc request.
	 */
	prstop(lwp, PR_REQUESTED, 0);

	thread_lock(curthread);
	curthread->t_flag |= T_WAKEABLE;
	LWPCHAN(curthread)->val[0] = lwpchan->val[0];
	LWPCHAN(curthread)->val[1] = lwpchan->val[1];
	curthread->t_sobj_ops = &lwp_sops;
	sqh = lwpsqhash(lwpchan);
	disp_lock_enter_high(&sqh->sq_lock);
	THREAD_SLEEP(curthread, &sqh->sq_lock);
	sleepq_insert(&sqh->sq_queue, curthread);
	thread_unlock(curthread);
	lwp->lwp_asleep = 1;
	lwp->lwp_sysabort = 0;
	lwp->lwp_ru.nvcsw++;
	if (curthread->t_proc_flag & TP_MSACCT)
		(void) new_mstate(curthread, LMS_SLEEP);
}

extern kmutex_t lwplock;

#ifdef i386
/*
 * For 386s, make sure copy-on-write happens, if necessary.  386s
 * don't have write protection against kernel accesses.
 */
extern faultcode_t forcefault(caddr_t addr, int len);
#define	FORCEFAULT(addr, len)	\
	if (forcefault(addr, len)) \
		return (set_errno(EFAULT))
#else
#define	FORCEFAULT(addr, len)
#endif

/*
 * A lwp blocks when the mutex is set.
 */
/* XXX - not portable.  must establish mapping to user's lock. */
/* XXX - watch out for locks crossing a page boundary. */
int
lwp_mutex_lock(lwp_mutex_t *lp)
{
	register kthread_t *t = curthread;
	register proc_t *p = ttoproc(t);
	register klwp_t *lwp = ttolwp(t);
	int error = 0;
	u_char waiters;
	volatile int locked = 0;
	label_t ljb;
	int type;
	caddr_t lname;		/* logical name of mutex (vnode + offset) */
	quad lwpchan;
	register sleepq_head_t *sqh;
	static int iswanted();

	if ((caddr_t)lp >= (caddr_t)USERLIMIT)
		return (set_errno(EFAULT));

	FORCEFAULT((caddr_t)lp, sizeof (*lp));

	if (t->t_proc_flag & TP_MSACCT)
		(void) new_mstate(t, LMS_USER_LOCK);

	if (on_fault(&ljb)) {
		if (locked)
			mutex_exit(&lwplock);
		no_fault();
		return (set_errno(EFAULT));
	}
	/*
	 * Force Copy-on-write fault if lwp_mutex_t object is
	 * defined to be MAP_PRIVATE and it was initialized to
	 * USYNC_PROCESS.
	 */
	type = fuword_noerr((int *)&(lp->mutex_type));
	suword_noerr((int *)&(lp->mutex_type), type);
	lname = (caddr_t)&lp->mutex_type;
	mutex_enter(&lwplock);
	locked = 1;
	waiters = fubyte_noerr((caddr_t)&(lp->mutex_waiters));
	subyte_noerr((caddr_t)&(lp->mutex_waiters), 1);
	while (!ulock_try(&lp->mutex_lockw)) {
		if (!get_lwpchan(curproc->p_as, lname, type, &lwpchan)) {
			mutex_exit(&lwplock);
			no_fault();
			return (set_errno(EFAULT));
		}
		(void) sigwaiting_check(NULL);
		lwp_block(&lwpchan);
		/*
		 * Nothing should happen to cause the LWP to go
		 * to sleep again until after it returns from
		 * swtch().
		 */
		locked = 0;
		mutex_exit(&lwplock);
		if (ISSIG(t, JUSTLOOKING) || ISHOLD(p))
			setrun(t);
		swtch();
		t->t_flag &= ~T_WAKEABLE;
		sigwaiting_decr();
		if (ISSIG(t, FORREAL) || lwp->lwp_sysabort || ISHOLD(p)) {
			error = set_errno(EINTR);
			lwp->lwp_asleep = 0;
			lwp->lwp_sysabort = 0;
			/*
			 * Need to re-compute waiters bit. The waiters field in
			 * the lock is not reliable. Either of two things
			 * could have occurred: no lwp may have called 
			 * lwp_release() for me but I have woken up due to a
			 * signal. In this case, the waiter bit is incorrect
			 * since it is still set to 1, set above.
			 * OR an lwp_release() did occur for some other lwp
			 * on the same lwpchan. In this case, the waiter bit is
			 * correct. But which event occurred, one can't tell.
			 * So, recompute.
			 */
			mutex_enter(&lwplock);
			sqh = lwpsqhash(&lwpchan);
			disp_lock_enter(&sqh->sq_lock);
			waiters = iswanted(sqh->sq_queue.sq_first, &lwpchan);
			disp_lock_exit(&sqh->sq_lock);
			break;
		}
		lwp->lwp_asleep = 0;
		mutex_enter(&lwplock);
		locked = 1;
		waiters = fubyte_noerr((caddr_t)&(lp->mutex_waiters));
		subyte_noerr((caddr_t)&(lp->mutex_waiters), 1);
	}
	subyte_noerr((caddr_t)&(lp->mutex_waiters), waiters);
	locked = 0;
	mutex_exit(&lwplock);
	no_fault();
	return (error);
}


static int
iswanted(kthread_t *t, quad *lwpchan)
{
	/*
	 * The caller holds the dispatcher lock on the sleep queue.
	 */
	while (t != NULL) {
		if (LWPCHAN(t)->val[0] == lwpchan->val[0] &&
		    LWPCHAN(t)->val[1] == lwpchan->val[1]) {
			return (1);
		}
		t = t->t_link;
	}
	return (0);
}

static int
lwp_release(quad *lwpchan, u_char *waiters, int sync_type)
{
	register sleepq_head_t *sqh;
	register kthread_t *tp;
	register kthread_t **tpp;
	register kthread_t *t = curthread;

	sqh = lwpsqhash(lwpchan);
	disp_lock_enter(&sqh->sq_lock);		/* lock the sleep queue */
	tpp = &sqh->sq_queue.sq_first;
	while ((tp = *tpp) != NULL) {
		if (LWPCHAN(tp)->val[0] == lwpchan->val[0] &&
		    LWPCHAN(tp)->val[1] == lwpchan->val[1]) {
			/*
			 * The following is typically false. It could be true
			 * only if lwp_release() is called from 
			 * lwp_mutex_unlock() after reading the waiters field
			 * from memory in which the lwp lock used to be, but has
			 * since been re-used to hold a lwp cv or lwp semaphore.
			 * The thread "tp" found to match the lwp lock's wchan
			 * is actually sleeping for the cv or semaphore which
			 * now has the same wchan. In this case, lwp_release()
			 * should return failure.
			 */
			if (sync_type != (tp->t_flag & T_WAITCVSEM)) {
				ASSERT(sync_type == 0);
				/*
				 * assert that this can happen only for mutexes
				 * i.e. sync_type == 0, for correctly written
				 * user programs.
				 */
				disp_lock_exit(&sqh->sq_lock);
				return (0);
			}
			*tpp = tp->t_link;
			*waiters = iswanted(tp->t_link, lwpchan);
			tp->t_wchan0 = 0;
			tp->t_wchan = NULL;
			tp->t_link = 0;
			tp->t_sobj_ops = NULL;
			THREAD_TRANSITION(tp);	/* drops sleepq lock */
			CL_WAKEUP(tp);
			thread_unlock(tp);	/* drop run queue lock */
			return (1);
		}
		tpp = &tp->t_link;
	}
	*waiters = 0;
	disp_lock_exit(&sqh->sq_lock);
	return (0);
}

static void
lwp_release_all(quad *lwpchan)
{
	register sleepq_head_t	*sqh;
	register kthread_id_t tp;
	register kthread_id_t *tpp;

	sqh = lwpsqhash(lwpchan);
	disp_lock_enter(&sqh->sq_lock);		/* lock sleep q queue */
	tpp = &sqh->sq_queue.sq_first;
	while ((tp = *tpp) != NULL) {
		if (LWPCHAN(tp)->val[0] == lwpchan->val[0] &&
		    LWPCHAN(tp)->val[1] == lwpchan->val[1]) {
			*tpp = tp->t_link;
			tp->t_wchan0 = 0;
			tp->t_wchan = NULL;
			tp->t_link = 0;
			tp->t_sobj_ops = NULL;
			CL_WAKEUP(tp);
			thread_unlock_high(tp);	/* release run queue lock */
		} else {
			tpp = &tp->t_link;
		}
	}
	disp_lock_exit(&sqh->sq_lock);		/* drop sleep q lock */
}

/*
 * unblock a lwp that is trying to acquire this mutex. the blocked
 * lwp resumes and retries to acquire the lock.
 */
int
lwp_mutex_unlock(lwp_mutex_t *lp)
{
	quad lwpchan;
	u_char waiters;
	volatile int locked = 0;
	int type;
	caddr_t lname;		/* logical name of mutex (vnode + offset) */
	label_t ljb;

	if ((caddr_t)lp >= (caddr_t)USERLIMIT)
		return (set_errno(EFAULT));

	FORCEFAULT((caddr_t)lp, sizeof (*lp));

	if (on_fault(&ljb)) {
		if (locked)
			mutex_exit(&lwplock);
		no_fault();
		return (set_errno(EFAULT));
	}
	/*
	 * Force Copy-on-write fault if lwp_mutex_t object is
	 * defined to be MAP_PRIVATE, and type is USYNC_PROCESS
	 */
	type = fuword_noerr((int *) &(lp->mutex_type));
	suword_noerr((int *)&(lp->mutex_type), type);
	lname = (caddr_t)&lp->mutex_type;
	if (!get_lwpchan(curproc->p_as, lname, type, &lwpchan)) {
		no_fault();
		return (set_errno(EFAULT));
	}
	mutex_enter(&lwplock);
	locked = 1;
	/*
	 * Always wake up an lwp (if any) waiting on lwpchan. The woken lwp will
	 * re-try the lock in _lwp_mutex_lock(). The call to lwp_release() may
	 * fail.  If it fails, do not write into the waiter bit.
	 * The call to lwp_release() might fail due to one of three reasons:
	 *
	 * 	1. due to the thread which set the waiter bit not actually
	 *	   sleeping since it got the lock on the re-try. The waiter
	 *	   bit will then be correctly updated by that thread. This
	 *	   window may be closed by reading the wait bit again here
	 *	   and not calling lwp_release() at all if it is zero.
	 *	2. the thread which set the waiter bit and went to sleep
	 *	   was woken up by a signal. This time, the waiter recomputes
	 *	   the wait bit in the return with EINTR code.
	 *	3. the waiter bit read by lwp_mutex_unlock() was in
	 *	   memory that has been re-used after the lock was dropped.
	 *	   In this case, writing into the waiter bit would cause data
	 *	   corruption.
	 */
	if (lwp_release(&lwpchan, &waiters, 0) == 1) {
		subyte_noerr((caddr_t)&(lp->mutex_waiters), waiters);
	}
	mutex_exit(&lwplock);
	no_fault();
	return (0);
}

/*
 * lwp_cond_wait() has three arguments, a pointer to a condition variable,
 * a pointer to a mutex, and a pointer to a timeval for a timed wait.
 * The kernel puts the LWP to sleep on a unique 64 bit int called a
 * LWPCHAN.  The LWPCHAN is the concatenation of a vnode and a offset
 * which represents a physical memory address.  In this case, the
 * LWPCHAN used is the physical address of the condition variable.
 */
int
lwp_cond_wait(lwp_cond_t *cv, lwp_mutex_t *mp, timestruc_t *tsp)
{
	timestruc_t ts;			/* timed wait value */
	struct timeval	tv;
	kthread_t *t = curthread;
	klwp_t *lwp = ttolwp(t);
	proc_t *p = ttoproc(t);
	quad lwpchan, lwpchan2;
	caddr_t timedwait;
	int type;
	caddr_t cv_lname;		/* logical name of lwp_cond_t */
	caddr_t mp_lname;		/* logical name of lwp_mutex_t */
	u_char waiters;
	int error = 0;
	int id = 0;	/* timeout's id */
	long tim, runtime;
	volatile int locked = 0;
	label_t ljb;

	if ((caddr_t)cv >= (caddr_t)USERLIMIT ||
	    (caddr_t)mp >= (caddr_t)USERLIMIT)
		return (set_errno(EFAULT));

	if (t->t_proc_flag & TP_MSACCT)
		(void) new_mstate(t, LMS_USER_LOCK);

	if (on_fault(&ljb)) {
		if (locked)
			mutex_exit(&lwplock);
		/*
		 * set up another on_fault() for a possible fault
		 * on the user lock accessed at "efault"
		 */
		if (on_fault(&ljb)) {
			if (locked)
				mutex_exit(&lwplock);
			no_fault();
			return (set_errno(EFAULT));
		}
		goto efault;
	}

	/* set up on_fault() before copyin() for code under "efault" */
	if ((timedwait = (caddr_t)tsp) != NULL &&
	    copyin(timedwait, (caddr_t)&ts, sizeof (timestruc_t)))
		goto efault;

	FORCEFAULT((caddr_t)cv, sizeof (*cv));
	FORCEFAULT((caddr_t)mp, sizeof (*mp));

	/*
	 * Force Copy-on-write fault if lwp_cond_t and lwp_mutex_t
	 * objects are defined to be MAP_PRIVATE, and are USYNC_PROCESS
	 */
	type = fuword_noerr((int *)&(cv->cond_type));
	suword_noerr((int *)&(cv->cond_type), type);
	cv_lname = (caddr_t)&cv->cond_type;
	if ((type = fuword_noerr((int *)&(mp->mutex_type))) != NULL)
		suword_noerr((int *)&(mp->mutex_type), type);
	mp_lname = (caddr_t)&mp->mutex_type;

	/* convert user level condition variable, "cv", to a unique LWPCHAN. */
	if (!get_lwpchan(p->p_as, cv_lname, type, &lwpchan)) {
		goto efault;
	}
	/* convert user level mutex, "mp", to a unique LWPCHAN */
	if (!get_lwpchan(p->p_as, mp_lname, type, &lwpchan2)) {
		goto efault;
	}
	if (timedwait) {
		tv.tv_sec = ts.tv_sec;
		tv.tv_usec = ts.tv_nsec/1000;
		tim = tv.tv_sec * hz + (tv.tv_usec * hz)/1000000;
		runtime = tim + lbolt;
		id = timeout((void (*)())setrun, (caddr_t)t, tim);
	}
	/*
	 * lwplock ensures that the calling LWP is put to sleep atomically
	 * with respect to a possible wakeup which is a result of either
	 * an lwp_cond_signal() or an lwp_cond_broadcast().
	 *
	 * What's misleading, is that the LWP is put to sleep after the
	 * condition variable's mutex is released. this is OK as long as
	 * the release operation is also done while holding lwplock. the
	 * LWP is then put to sleep when the possibility of pagefaulting
	 * or sleeping is completely eliminated.
	 */
	mutex_enter(&lwplock);
	locked = 1;
	subyte_noerr((caddr_t)&(cv->cond_waiters), 1);
	/*
	 * unlock the condition variable's mutex. (pagefaults are possible
	 * here.)
	 */
	ulock_clear(&mp->mutex_lockw);
	if (fubyte_noerr((caddr_t)&(mp->mutex_waiters)) != 0) {
		/*
		 * Given the locking of lwplock around the release of the mutex
		 * and checking for waiters, the following call to lwp_release()
		 * can fail ONLY if the lock acquirer is interrupted after
		 * setting the waiter bit, calling lwp_block() and releasing
		 * lwplock. In this case, it could get pulled off the lwp
		 * sleep q (via setrun()) before the following call to
		 * lwp_release() occurs. In this case, the lock requestor will
		 * update the waiter bit correctly by re-evaluating it.
		 */
		if (lwp_release(&lwpchan2, &waiters, 0) > 0)
			subyte_noerr((caddr_t)&(mp->mutex_waiters), waiters);
	}
	(void) sigwaiting_check(NULL);
	t->t_flag |= T_WAITCVSEM;
	lwp_block(&lwpchan);
	/*
	 * Nothing should happen to cause the LWP to go to sleep
	 * until after it returns from swtch().
	 */
	mutex_exit(&lwplock);
	locked = 0;
	no_fault();
	if (ISSIG(t, JUSTLOOKING) || ISHOLD(p) ||
	    (timedwait && runtime <= lbolt)) {
		setrun(t);
	}
	swtch();
	t->t_flag &= ~(T_WAITCVSEM | T_WAKEABLE);
	if (timedwait) {
		if (id == -1) {
			/* callout limit exceeded */
			error = set_errno(EAGAIN);
			id = 0;
		} else {
			id = untimeout(id);
		}
	}
	sigwaiting_decr();
	if (ISSIG(t, FORREAL) || lwp->lwp_sysabort || ISHOLD(p))
		error = set_errno(EINTR);
	else if (id == -1)
		error = set_errno(ETIME);
	lwp->lwp_asleep = 0;
	lwp->lwp_sysabort = 0;
	/* mutex is re-aquired by caller */
	return (error);

efault:
	/*
	 * make sure that the user level lock is dropped before
	 * returning to caller, since the caller always re-acquires it.
	 */
	mutex_enter(&lwplock);
	locked = 1;

	ulock_clear(&mp->mutex_lockw);
	if (fubyte_noerr((caddr_t)&(mp->mutex_waiters)) != 0) {
		/*
		 * See comment above on lock clearing and lwp_release()
		 * success/failure.
		 */
		if (lwp_release(&lwpchan2, &waiters, 0) > 0)
			subyte_noerr((caddr_t)&(mp->mutex_waiters), waiters);
	}
	mutex_exit(&lwplock);
	no_fault();
	return (set_errno(EFAULT));
}

/*
 * wakeup one lwp that's blocked on this condition variable.
 */
int
lwp_cond_signal(lwp_cond_t *cv)
{
	quad lwpchan;
	u_char waiters;
	int type;
	caddr_t cv_lname;		/* logical name of lwp_cond_t */
	volatile int locked = 0;
	label_t ljb;

	if ((caddr_t)cv >= (caddr_t)USERLIMIT)
		return (set_errno(EFAULT));

	FORCEFAULT((caddr_t)cv, sizeof (*cv));

	if (on_fault(&ljb)) {
		no_fault();
		if (locked)
			mutex_exit(&lwplock);
		return (set_errno(EFAULT));
	}
	/*
	 * Force Copy-on-write fault if lwp_cond_t object is
	 * defined to be MAP_PRIVATE, and is USYNC_PROCESS.
	 */
	type = fuword_noerr((int *)&(cv->cond_type));
	suword_noerr((int *)&(cv->cond_type), type);
	cv_lname = (caddr_t)&cv->cond_type;
	if (!get_lwpchan(curproc->p_as, cv_lname, type, &lwpchan)) {
		no_fault();
		return (set_errno(EFAULT));
	}
	mutex_enter(&lwplock);
	locked = 1;
	if (fubyte_noerr((caddr_t)&(cv->cond_waiters))) {
		/*
		 * The following call to lwp_release() might fail but it is
		 * OK to write into the waiters bit below, since the memory
		 * could not have been re-used or unmapped (for correctly
		 * written user programs) as in the case of lwp_mutex_unlock().
		 * For an incorrect program, we should not care about data 
		 * corruption since this is just one instance of other places
		 * where corruption can occur for such a program. Of course
		 * if the memory is unmapped, normal fault recovery occurs.
		 */
		(void) lwp_release(&lwpchan, &waiters, T_WAITCVSEM);
		subyte_noerr((caddr_t)&(cv->cond_waiters), waiters);
	}
	mutex_exit(&lwplock);
	no_fault();
	return (0);
}

/*
 * wakeup every lwp that's blocked on this condition variable.
 */
int
lwp_cond_broadcast(lwp_cond_t *cv)
{
	quad lwpchan;
	int type;
	caddr_t cv_lname;		/* logical name of lwp_cond_t */
	volatile int locked = 0;
	label_t ljb;

	if ((caddr_t)cv >= (caddr_t)USERLIMIT)
		return (set_errno(EFAULT));

	FORCEFAULT((caddr_t)cv, sizeof (*cv));

	if (on_fault(&ljb)) {
		no_fault();
		if (locked)
			mutex_exit(&lwplock);
		return (set_errno(EFAULT));
	}
	/*
	 * Force Copy-on-write fault if lwp_cond_t object is
	 * defined to be MAP_PRIVATE, and is USYNC_PROCESS.
	 */
	type = fuword_noerr((int *)&(cv->cond_type));
	suword_noerr((int *)&(cv->cond_type), type);
	cv_lname = (caddr_t)&cv->cond_type;
	if (!get_lwpchan(curproc->p_as, cv_lname, type, &lwpchan)) {
		no_fault();
		return (set_errno(EFAULT));
	}
	mutex_enter(&lwplock);
	locked = 1;
	if (fubyte_noerr((caddr_t)&(cv->cond_waiters))) {
		lwp_release_all(&lwpchan);
		subyte_noerr((caddr_t)&(cv->cond_waiters), 0);
	}
	mutex_exit(&lwplock);
	no_fault();
	return (0);
}

int
lwp_sema_p(volatile lwp_sema_t *sp)
{
	register kthread_t *t = curthread;
	register klwp_t *lwp = ttolwp(curthread);
	register proc_t *p = curproc;
	label_t ljb;
	volatile int locked = 0;
	int type;
	int tmpcnt;
	caddr_t sp_lname;		/* logical name of lwp_sema_t */
	quad lwpchan;

	if ((caddr_t)sp >= (caddr_t)USERLIMIT)
		return (set_errno(EFAULT));

	FORCEFAULT((caddr_t)sp, sizeof (*sp));

	if (on_fault(&ljb)) {
		if (locked)
			mutex_exit(&lwplock);
		no_fault();
		return (set_errno(EFAULT));
	}
	/*
	 * Force Copy-on-write fault if lwp_sema_t object is
	 * defined to be MAP_PRIVATE, and is USYNC_PROCESS.
	 */
	type = fuword_noerr((int *)&(sp->sema_type));
	suword_noerr((int *)&(sp->sema_type), type);
	sp_lname = (caddr_t)&sp->sema_type;
	mutex_enter(&lwplock);
	locked = 1;
	if (fuword_noerr((int *)&(sp->sema_count)) == 0) {
		mutex_exit(&lwplock);
		locked = 0;
		p = curproc;
		if (!get_lwpchan(p->p_as, sp_lname, type, &lwpchan)) {
			no_fault();
			return (set_errno(EFAULT));
		}
		mutex_enter(&lwplock);
		locked = 1;
		while (fuword_noerr((int *)&(sp->sema_count)) == 0) {
			subyte_noerr((caddr_t)&(sp->sema_waiters), 1);
			(void) sigwaiting_check(NULL);
			t->t_flag |= T_WAITCVSEM;
			lwp_block(&lwpchan);
			/*
			 * Nothing should happen to cause the LWP to
			 * sleep again until after it returns from
			 * swtch().
			 */
			mutex_exit(&lwplock);
			locked = 0;
			if (ISSIG(t, JUSTLOOKING) || ISHOLD(p))
				setrun(t);
			swtch();
			t->t_flag &= ~(T_WAITCVSEM | T_WAKEABLE);
			sigwaiting_decr();
			if (ISSIG(t, FORREAL) || lwp->lwp_sysabort ||
			    ISHOLD(p)) {
				lwp->lwp_asleep = 0;
				lwp->lwp_sysabort = 0;
				no_fault();
				return (set_errno(EINTR));
			}
			lwp->lwp_asleep = 0;
			mutex_enter(&lwplock);
			locked = 1;
		}
	}
	tmpcnt = fuword_noerr((int *)&(sp->sema_count));
	suword_noerr((int *)&(sp->sema_count), --tmpcnt);
	mutex_exit(&lwplock);
	no_fault();
	return (0);
}

int
lwp_sema_v(lwp_sema_t *sp)
{
	u_char waiters;
	label_t ljb;
	volatile int locked = 0;
	int type;
	caddr_t sp_lname;		/* logical name of lwp_sema_t */
	int tmpcnt;
	quad lwpchan;

	if ((caddr_t)sp >= (caddr_t)USERLIMIT)
		return (set_errno(EFAULT));

	FORCEFAULT((caddr_t)sp, sizeof (*sp));

	if (on_fault(&ljb)) {
		if (locked)
			mutex_exit(&lwplock);
		no_fault();
		return (set_errno(EFAULT));
	}
	/*
	 * Force Copy-on-write fault if lwp_sema_t object is
	 * defined to be MAP_PRIVATE, and is USYNC_PROCESS.
	 */
	type = fuword_noerr((int *)&(sp->sema_type));
	suword_noerr((int *)&(sp->sema_type), type);
	sp_lname = (caddr_t)&sp->type;
	mutex_enter(&lwplock);
	locked = 1;
	if (fubyte_noerr((caddr_t)&(sp->sema_waiters))) {
		mutex_exit(&lwplock);
		locked = 0;
		if (!get_lwpchan(curproc->p_as, sp_lname, type, &lwpchan)) {
			no_fault();
			return (set_errno(EFAULT));
		}
		mutex_enter(&lwplock);
		locked = 1;
		/*
		 * sp->waiters is only a hint. lwp_release() does nothing
		 * if there is no one waiting. The value of waiters is
		 * then set to zero.
		 */

		if (fubyte_noerr((caddr_t)&(sp->sema_waiters))) {
			/*
			 * See Comment in lwp_cond_signal() before the similar
			 * call to lwp_release().
			 */
			(void) lwp_release(&lwpchan, &waiters, T_WAITCVSEM);
			subyte_noerr((caddr_t)&(sp->sema_waiters), waiters);
		}
	}
	tmpcnt = fuword_noerr((int *)&(sp->sema_count));
	suword_noerr((int *)&(sp->sema_count), ++tmpcnt);
	mutex_exit(&lwplock);
	no_fault();
	return (0);
}
/*
 * Return the owner of the user-level s-object.
 * Since we can't really do this, return NULL.
 */
/* ARGSUSED */
static kthread_t *
lwpsobj_owner(caddr_t sobj)
{
	return ((kthread_t *)NULL);
}

/*
 * Wake up a thread asleep on a user-level synchronization
 * object.
 */
static void
lwp_unsleep(kthread_id_t t)
{
	ASSERT(THREAD_LOCK_HELD(t));
	if (t->t_wchan0 != 0) {
		register sleepq_head_t  *sqh;

		sqh = lwpsqhash((quad *)(&t->t_wchan0));
		if (sleepq_unsleep(&sqh->sq_queue, t) != NULL) {
			disp_lock_exit_high(&sqh->sq_lock);
			CL_SETRUN(t);
			return;
		}
	}
	cmn_err(CE_PANIC, "lwp_unsleep: thread %x not on sleepq", (int)t);
}

/*
 * Change the priority of a thread asleep on a user-level
 * synchronization object. To maintain proper priority order,
 * we:
 *	o dequeue the thread.
 *	o change its effective priority.
 *	o re-enqueue the thread.
 * Assumption: the thread is locked on entry.
 */
static void
lwp_changepri(kthread_t *t, pri_t pri)
{
	ASSERT(THREAD_LOCK_HELD(t));
	if (t->t_wchan0 != 0) {
		sleepq_head_t   *sqh;

		sqh = lwpsqhash((quad *)&t->t_wchan0);
		(void) sleepq_dequeue(&sqh->sq_queue, t);
		t->t_epri = pri;
		sleepq_insert(&sqh->sq_queue, t);
	} else {
		cmn_err(CE_PANIC,
			"lwp_changepri: 0x%x not on a sleep queue", (int)t);
	}
}
