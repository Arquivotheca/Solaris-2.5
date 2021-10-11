#ident	"@(#)mutex.c	1.53	95/07/21 SMI"

/*
 * This files contains rountines that implement mutex locks.
 */

#include <sys/param.h>
#include <sys/time.h>
#include <sys/cpuvar.h>
#include <sys/thread.h>
#include <sys/debug.h>
#include <sys/debug/debug.h>
#include <sys/cmn_err.h>
#include <sys/dki_lock.h>
#include <sys/kmem.h>
#include <sys/time.h>
#include <sys/tblock.h>
#include <sys/sleepq.h>
#include <sys/prioinherit.h>
#include <sys/systm.h>
#include <sys/mutex_impl.h>
#include <sys/spl.h>

extern char *panicstr;
extern void panic_hook();

/*
 * Lock statistics enable.  This enables all DEFAULT locks to use statistics
 * if set non-zero before the lock in initialized.  Use /etc/system to set this.
 * Spin locks must select statistics at compile time.
 *
 * Other locks can be using statistics based on compile-time _LOCKTEST
 * or _MPSTATS defines or by specifying the statistics types directly to
 * mutex_init().
 */
int lock_stats = 0;		/* set non-zero to enable statistics */
static struct mutex_stats *mutex_stats_alloc(int sleep);
static void mutex_stats_free(struct mutex_stats *sp);


/* ARGSUSED */
static void
mutex_nop(mutex_impl_t *lp)
{
}

/*
 * General use mutex functions
 */
static lkstat_t *mutex_no_stats(mutex_impl_t *);
static lkstat_t *mutex_stat_get(mutex_impl_t *);

/*
 * Adaptive mutex functions.
 * 	Since adaptive is the default, it just goes to mutex_enter/exit(),
 *	which checks for non-adaptive types.
 */
static void	mutex_adaptive_init(mutex_impl_t *, char *,
			kmutex_type_t, void *);
static void	mutex_adaptive_destroy(mutex_impl_t *);
void		mutex_adaptive_enter(mutex_impl_t *);
static void	mutex_adaptive_exit(mutex_impl_t *);
int		mutex_adaptive_tryenter(mutex_impl_t *);

int		mutex_owned(kmutex_t *);
kthread_t	*mutex_owner(kmutex_t *);

/*
 * Spin mutex functions.
 */
static void	mutex_spin_init(mutex_impl_t *, char *, kmutex_type_t, void *);
static void	mutex_spin_enter(mutex_impl_t *);
static void	mutex_spin_exit(mutex_impl_t *);
static int	mutex_spin_tryenter(mutex_impl_t *);
static int	mutex_spin_owned(mutex_impl_t *);
static kthread_t *mutex_spin_owner(mutex_impl_t *);

/*
 * Adaptive mutexes with statistics.
 */
static void	mutex_adstat_init(mutex_impl_t *, char *,
			kmutex_type_t, void *);
static void	mutex_adstat_enter(mutex_impl_t *);
static void	mutex_adstat_exit(mutex_impl_t *);
static int	mutex_adstat_tryenter(mutex_impl_t *);
static int	mutex_adstat_owned(mutex_impl_t *);
static kthread_t *mutex_adstat_owner(mutex_impl_t *);
static void	mutex_stat_destroy(mutex_impl_t *);

/*
 * Spin mutexes with statistics.
 */
static void	mutex_spstat_init(mutex_impl_t *, char *,
			kmutex_type_t, void *);
static void	mutex_spstat_enter(mutex_impl_t *);
static void	mutex_spstat_exit(mutex_impl_t *);
static int	mutex_spstat_tryenter(mutex_impl_t *);

/*
 * Driver mutex types.
 *  	These init routines change the type to adaptive or spin.
 */
static void	mutex_driver_init(mutex_impl_t *, char *,
			kmutex_type_t, void *);
static void	mutex_drstat_init(mutex_impl_t *, char *,
			kmutex_type_t, void *);
static void	mutex_addef_init(mutex_impl_t *, char *, kmutex_type_t, void *);
static void	mutex_err(mutex_impl_t *);


/*
 * Mutex operations for variant types.
 */
struct mutex_ops {
	void	(*m_init)(mutex_impl_t *, char *, kmutex_type_t, void *);
	void	(*m_enter)(mutex_impl_t *);
	void	(*m_exit)(mutex_impl_t *);
	int	(*m_tryenter)(mutex_impl_t *);
	int	(*m_owned)(mutex_impl_t *);
	lkstat_t *(*m_stats)(mutex_impl_t *);
	void	(*m_destroy)(mutex_impl_t *);
	kthread_t *(*m_owner)(mutex_impl_t *);
};

static struct mutex_ops mutex_ops[] = {
	{
		mutex_adaptive_init,		/* 0 adaptive */
		mutex_err,		/* handled in mutex_adaptive_enter() */
		mutex_adaptive_exit,
		mutex_adaptive_tryenter,
		(int (*)())mutex_err,		/* handled in mutex_owned */
		mutex_no_stats,
		mutex_adaptive_destroy,
		(kthread_t *(*)())mutex_err,	/* handled in mutex_owner */
	},
	{
		mutex_spin_init,		/* 1 spin */
		mutex_spin_enter,
		mutex_spin_exit,
		mutex_spin_tryenter,
		mutex_spin_owned,
		mutex_no_stats,
		mutex_nop,
		mutex_spin_owner,
	},
	{
		mutex_adstat_init,		/* 2 adaptive w statistics */
		mutex_adstat_enter,
		mutex_adstat_exit,
		mutex_adstat_tryenter,
		mutex_adstat_owned,
		mutex_stat_get,
		mutex_stat_destroy,
		mutex_adstat_owner,
	},
	{
		mutex_spstat_init,		/* 3 spin w statistics */
		mutex_spstat_enter,
		mutex_spstat_exit,
		mutex_spstat_tryenter,
		mutex_spin_owned,
		mutex_stat_get,
		mutex_stat_destroy,
		mutex_spin_owner,
	},
	{
		mutex_driver_init,		/* 4 driver */
		mutex_err,
		mutex_err,
		(int (*)())mutex_err,
		(int (*)())mutex_err,
		(lkstat_t *(*)())mutex_err,
		mutex_err
	},
	{
		mutex_drstat_init,		/* 5 driver w statistics */
		mutex_err,
		mutex_err,
		(int (*)())mutex_err,
		(int (*)())mutex_err,
		(lkstat_t *(*)())mutex_err,
		mutex_err
	},
	{
		mutex_addef_init,		/* 6 adaptive default */
		mutex_err,
		mutex_err,
		(int (*)())mutex_err,
		(int (*)())mutex_err,
		(lkstat_t *(*)())mutex_err,
		mutex_err
	}
};

#define	MUTEX_NTYPES (sizeof (mutex_ops) / sizeof (struct mutex_ops))

/*
 * Macro to find the ops vector based on the offset stored in the mutex.
 *
 * This lets mutex_enter, for example say:
 *	(*(MUTEX_OP(type_offset)->m_enter))(lp);
 * Which is faster than:
 *	(*(mutex_ops[m_type]->m_enter))(lp);
 * Because type_offset precomputes the multiply needed by indexing.
 */
#define	MUTEX_OP(type) ((struct mutex_ops *)((caddr_t)mutex_ops + (type)))

/*
 * get the old type (i.e. MUTEX_ADAPTIVE, MUTEX_SPIN etc.
 * from the offset stored in the lock's "type" field
 * (only after it's been init'd).
 */
#define	MUTEX_TYPE(offset)	((offset) / sizeof (struct mutex_ops))

/*
 * Get internal type, stored in the mutex, from the external kmutex_type_t.
 */
#define	MUTEX_INT_TYPE(type)	((type) * sizeof (struct mutex_ops))

static void	mutex_unsleep(kthread_t *t);
static void	mutex_changepri(kthread_t *t, pri_t pri);


/*
 * The sobj_ops vector exports a set of functions needed
 * when a thread is asleep on a synchronization object of
 * this type.
 *
 * Every blocking s-object should define one of these
 * structures and set the t_sobj_ops field of blocking
 * threads to it's address.
 *
 * Current users are setrun() and pi_changepri().
 */
static sobj_ops_t	mutex_sops = {
	"Mutex",
	SOBJ_MUTEX,
	QOBJ_MUTEX,
	mutex_owner,
	mutex_unsleep,
	mutex_changepri
};



/*
 * mutex_init:
 *
 * initialize a mutex lock.
 */
/* ARGSUSED */
void
mutex_init(kmutex_t *mp, char *name, kmutex_type_t type, void *arg)
{
	register mutex_impl_t	*lp = (mutex_impl_t *)mp;
	register struct mutex_ops *op;
	register u_int	ops_offset;

	if ((unsigned)type >= MUTEX_NTYPES)
		cmn_err(CE_PANIC,
			"mutex_init: bad type %d mutex %x", type, (int)lp);

	/*
	 * Make the type field in the lock be the offset in mutex_ops
	 * for the start of the ops for this type.  This will save a little
	 * time in mutex_enter().
	 */
	op = &mutex_ops[type];
	ops_offset = (caddr_t)op - (caddr_t)mutex_ops;
	ASSERT(ops_offset < 256);		/* must fit in a byte */
	lp->m_generic.m_type = (uchar_t)ops_offset;
	(*op->m_init)(lp, name, type, arg);
}

/*
 * mutex_destroy:
 */
void
mutex_destroy(kmutex_t *mp)
{
	register mutex_impl_t	*lp = (mutex_impl_t *)mp;

	(*(MUTEX_OP(lp->m_generic.m_type)->m_destroy))(lp);
}

/*
 * Adaptive Mutexes.
 */
/* ARGSUSED */
static void
mutex_adaptive_init(mutex_impl_t *lp, char *name, kmutex_type_t type, void *arg)
{
	lp->m_adaptive2.m_owner_lock = 0;
	lp->m_adaptive.m_waiters = 0;
	disp_lock_init(&lp->m_adaptive.m_wlock, "adaptive mutex wlock");
}

static void
mutex_adaptive_destroy(mutex_impl_t *lp)
{
	turnstile_t	*ts;
	turnstile_id_t	waiters;
#ifdef DEBUG
	kthread_id_t	owner;

	owner = MUTEX_OWNER(lp);
	ASSERT(owner == MUTEX_NO_OWNER || owner == curthread);
#endif /* DEBUG */

	disp_lock_destroy(&lp->m_adaptive.m_wlock);

	if ((waiters = lp->m_adaptive.m_waiters) != 0 &&
	    (ts = tstile_pointer(waiters)) != NULL) {
		disp_lock_enter(&ts->ts_wlock);
		if (ts->ts_sobj_priv_data == (void *)lp) {
			ASSERT(TSTILE_EMPTY(ts, QOBJ_MUTEX));
			tstile_free(ts, &lp->m_adaptive.m_waiters);
		}
		disp_lock_exit(&ts->ts_wlock);
	}
}

/*
 * Adaptive locks spin when owner is running on a different processor and
 * otherwise sleeps.  This is done until the lock is acquired.
 *
 * This routine is called from the assembler mutex_enter() routine if the
 * lock is held or of the wrong type.
 */
void
mutex_adaptive_enter(mutex_impl_t *lp)
{
	register struct adaptive_mutex *rlp = &lp->m_adaptive;
	kthread_id_t	owner;
	u_int		owner_lock;	/* owner/lock word in mutex */
#ifdef	MP
	cpu_t 		*cpup;
	cpu_t 		*first_cpu;
#endif	/* MP */
	turnstile_t	*ts;
	turnstile_id_t	waiters;

	if (lp->m_generic.m_type != MUTEX_INT_TYPE(MUTEX_ADAPTIVE)) {
		(*(MUTEX_OP(lp->m_generic.m_type)->m_enter))(lp);
		return;
	}

	/*
	 * This function must not be called higher than splclock.
	 *
	 * This can be checked by looking at cpu_on_intr, but to be sure in the
	 * face of possible migration, preemption is disabled and on_intr is
	 * double-checked.
	 */
#ifdef MP
	cpup = CPU;
#if DEBUG
	if (CPU->cpu_on_intr && !panicstr) {
		kpreempt_disable();
		ASSERT(CPU->cpu_on_intr == 0);
		kpreempt_enable();
	}
#endif /* DEBUG */
#else /* MP */
	ASSERT(CPU->cpu_on_intr == 0 || panicstr);
#endif /* MP */

	CPU_STAT_ADDQ(CPU, cpu_sysinfo.mutex_adenters, 1);
	/*
	 * Assembler version has already tried the lock.  Loop non-atomicly
	 * until the lock appears to be free.
	 * If can't get lock and panicking, ignore lock;
	 */
	do {
spin:
		if (panicstr) {	/* hook for panic */
			panic_hook();
			return;
		}
		while ((owner_lock = lp->m_adaptive2.m_owner_lock) != 0) {
			owner = (kthread_id_t)MUTEX_OWNER_PTR(owner_lock);

			if (owner == curthread)
				cmn_err(CE_PANIC,
				    "recursive mutex_enter. mutex %x caller %x",
				    (int)lp, (int)caller());
#ifndef MP	/* if the architecture doesn't support MP */
			if (owner == MUTEX_NO_OWNER)
				cmn_err(CE_PANIC, "No mutex owner");
#else /* MP */
			/*
			 * If the owner is NULL,  or running on another CPU,
			 * spin.  Otherwise block.  We cannot dereference the
			 * owner, since it could be freed at any time.
			 */
			if (owner == MUTEX_NO_OWNER)
				continue;

			/*
			 * When searching the other CPUs, start with the one
			 * where we last saw the owner thread.
			 */
			first_cpu = cpup;	/* mark start of search */
			do {
				if (cpup->cpu_thread == owner)
					goto spin;
			} while ((cpup = cpup->cpu_next) != first_cpu);

#endif /* MP */
			/*
			 * Owner is not running,
			 * or pinned underneath this thread, so block.
			 */
			waiters = rlp->m_waiters;
			if (waiters == 0) {
				disp_lock_enter(&rlp->m_wlock);
				if (rlp->m_waiters == 0) {
					ts = tstile_alloc();
					/*
					 * Get lock on the turnstile.  This
					 * locks the sleep queue and the pointer
					 * back to the mutex.
					 */
					disp_lock_enter_high(&ts->ts_wlock);
					ts->ts_sobj_priv_data = rlp;
					rlp->m_waiters = ts->ts_id;
					disp_lock_exit_high(&rlp->m_wlock);
#ifdef MP
					/*
					 * Be sure that the non-zero waiters
					 * is seen by any other processor that
					 * might release the lock before we
					 * recheck the owners field.
					 */
					lock_mutex_flush();
#endif /* MP */
				} else {
					disp_lock_exit(&rlp->m_wlock);
					continue;
				}
			} else {
				ts = tstile_pointer(waiters);
				disp_lock_enter(&ts->ts_wlock);

				/*
				 * The waiters field could've changed, so
				 * verify that this is still the right ts.
				 */
				if (ts->ts_sobj_priv_data != (void *)rlp) {
					disp_lock_exit(&ts->ts_wlock);
					continue;
				}
			}

			/*
			 * double-check owner_lock field - if it has changed,
			 * re-evaluate.
			 */
			if (lp->m_adaptive2.m_owner_lock == owner_lock) {
				thread_lock_high(curthread);
				t_block(ts, (caddr_t)rlp, &mutex_sops,
					&ts->ts_wlock);
				pi_willto(curthread);	/* drops the lock */
				swtch();
			} else {
				/*
				 * If we allocated a turnstile above, but
				 * we are not going to wait, there will
				 * be an empty turnsile list for someone to
				 * handle in mutex_exit().
				 */
				disp_lock_exit(&ts->ts_wlock);
			}
		}
	} while (!mutex_adaptive_tryenter(lp));
	/*
	 * mutex_adaptive_tryenter() got the lock and set the owner,
	 * so just return.
	 */
}

int
mutex_tryenter(kmutex_t *mp)
{
	register mutex_impl_t *lp = (mutex_impl_t *)mp;

	return ((*(MUTEX_OP(lp->m_generic.m_type)->m_tryenter))(lp));
}


/*
 * mutex_vector_exit()
 *
 * This routine is called from the assembler mutex_exit() routine if the
 * lock is not adaptive.
 */
void
mutex_vector_exit(kmutex_t *mp)
{
	register mutex_impl_t *lp = (mutex_impl_t *)mp;

	(*(MUTEX_OP(lp->m_generic.m_type)->m_exit))(lp);
}

/*
 * mutex_adaptive_exit()
 *
 * This routine is called from mutex_vector_exit() if the assembler
 * mutex_exit() routine detected a mismatch on the owner field, indicating
 * that the thread called mutex_exit() on a lock that it didn't hold.
 */
static void
mutex_adaptive_exit(mutex_impl_t *lp)
{
	mutex_impl_t m = *lp;		/* get snapshot for panic message */

	if (panicstr)
		return;

	cmn_err(CE_PANIC, "mutex_adaptive_exit: mutex not owned by thread, "
	    "lp %x owner %x lock %x waiters %x curthread %x\n",
	    (int)lp, (int)MUTEX_OWNER(&m), m.m_adaptive.m_lock,
	    m.m_adaptive.m_waiters, (int)curthread);
}

/*
 * Release a thread blocked on the adaptive mutex.
 * The lock has already been cleared by mutex_exit() in assembler.
 * The waiters field has already been checked.
 * If we got here, there were waiters (but might not be anymore).
 *
 * The locking on the waiters field allows it to be cleared while holding
 * the turnstile's ts_wlock, as long as the turnstile still points back at
 * the lock.
 */
void
mutex_adaptive_release(mutex_impl_t *lp, turnstile_id_t waiters)
{
	turnstile_t	*ts;

	ts = tstile_pointer_verify(waiters);
	if (ts != NULL) {
		disp_lock_enter(&ts->ts_wlock);
		if (ts->ts_sobj_priv_data == (void *)lp) {
			if (PI_INHERITOR(curthread, ts))
				pi_waive(ts);
			t_release(ts, &lp->m_adaptive.m_waiters, QOBJ_MUTEX);
		}
		disp_lock_exit(&ts->ts_wlock);
	}
}

int
mutex_owned(kmutex_t *mp)
{
	register mutex_impl_t	*lp = (mutex_impl_t *)mp;

	if (panicstr)
		return (1);

	if (lp->m_generic.m_type != MUTEX_INT_TYPE(MUTEX_ADAPTIVE)) {
		return ((*(MUTEX_OP(lp->m_generic.m_type)->m_owned))(lp));
	} else {
		return (MUTEX_OWNER(lp) == curthread);
	}
}

kthread_t *
mutex_owner(kmutex_t *mp)
{
	register mutex_impl_t	*lp = (mutex_impl_t *)mp;

	if (lp->m_generic.m_type != MUTEX_INT_TYPE(MUTEX_ADAPTIVE)) {
		return ((*(MUTEX_OP(lp->m_generic.m_type)->m_owner))(lp));
	} else {
		register kthread_id_t t;

		t = MUTEX_OWNER(lp);
		if (t == MUTEX_NO_OWNER)
			t = NULL;
		return (t);
	}
}

/*
 * Wake up a thread if it is sleeping on a mutex and put it on
 * the dispatch queue.
 * At this time, the only way we can "unsleep" a thread waiting on
 * a mutex is via setrun() when something like a cv_timedwait()
 * expires, or via pi_willto() if an inheriting thread is blocked
 * on a mutex with no owner.  If someone else grabs the mutex first,
 * mutex_enter will just go back to sleep anyway, so no harm done.
 */
static void
mutex_unsleep(kthread_t *t)
{
	register turnstile_t		*ts;

	ASSERT(THREAD_LOCK_HELD(t));
	if ((ts = t->t_ts) != NULL) {
		register struct adaptive_mutex	*mp;
		disp_lock_t			*lp = t->t_lockp;

		/*
		 * When we return from TSTILE_UNSLEEP(), the
		 * thread will be in transition, but the lock
		 * on the synch object will still be held.
		 */
		mp = (struct adaptive_mutex *)t->t_wchan;
		if (TSTILE_UNSLEEP(ts, QOBJ_MUTEX, t) != NULL) {
			tstile_free(ts, &mp->m_waiters);
			disp_lock_exit_high(lp);
			CL_SETRUN(t);
			return;
		}
	}
	cmn_err(CE_PANIC, "mutex_unsleep: 0x%x not in turnstile", t);
}

/*
 * Change the priority of a thread sleeping on
 * a mutex. This requires that we:
 *	o dequeue the thread from its turnstile.
 *	o change the thread's priority.
 *	o re-enqueue the thread.
 * Called: pi_changepri() via the SOBJ_CHANGEPRI() macro.
 * Conditions: the argument thread must be locked.
 * Side Effects: None.
 */
static void
mutex_changepri(kthread_t *t, pri_t pri)
{
	turnstile_t	*ts;

	ASSERT(THREAD_LOCK_HELD(t));
	if ((ts = t->t_ts) != NULL) {
		(void) TSTILE_DEQ(ts, QOBJ_MUTEX, t);
		t->t_epri = pri;
		TSTILE_INSERT(ts, QOBJ_MUTEX, t);
	} else {
		cmn_err(CE_PANIC,
			"mutex_changepri: 0x%x not in turnstile", (int)t);
	}
}


lkstat_t *
mutex_stats(kmutex_t *mp)
{
	register mutex_impl_t	*lp = (mutex_impl_t *)mp;

	return ((*(MUTEX_OP(lp->m_generic.m_type)->m_stats))(lp));
}

/* ARGSUSED */
static lkstat_t *
mutex_no_stats(mutex_impl_t *lp)
{
	return (NULL);
}


/*
 * Adaptive Mutexes with Statistics.
 * The statistics structure is maintained separately and pointed to
 * by the mutex.
 */
/* ARGSUSED */
static void
mutex_adstat_init(mutex_impl_t *lp, char *name, kmutex_type_t type, void *arg)
{
	register struct mutex_stats *sp;

	sp = mutex_stats_alloc(KM_SLEEP);	/* allocates zeroed struct */
	MUTEX_STATS_SET(lp, sp);	/* set stats pointer and dummylock */
	(void) sprintf_len(LOCK_NAME_LEN, sp->name, name,
	    (u_long)lp);	/* handle %x */
	sp->lkinfo.lk_name = sp->name;
	sp->lkstat = lkstat_alloc(&sp->lkinfo, KM_SLEEP);
	mutex_adaptive_init(&sp->m_real, name, MUTEX_ADAPTIVE, arg);
}

/*
 * Mutex_destroy for locks with statistics.  Used for spin and adaptive types.
 * Just free the stats package after adding accumulated statistics.
 */
static void
mutex_stat_destroy(mutex_impl_t *lp)
{
	register struct mutex_stats *sp = MUTEX_STATS(lp);

	lkstat_sum_on_destroy(sp->lkstat);
	lkstat_free(sp->lkstat, TRUE);
	mutex_adaptive_destroy(&sp->m_real);
	mutex_stats_free(sp);
	/*
	 * clear the stats pointer in the lock.  If the lock continues to
	 * be used, it'll look like an adaptive lock.
	 */
	lp->m_stats.m_stats_lock = NULL; /* clear pointer just in case ... */
	lp->m_stats.m_type = 0;		/* clear type */
}

/*
 * The lock statistics are protected by the mutex itself so they are updated
 * only after the lock is obtained.
 */
static void
mutex_adstat_enter(mutex_impl_t *lp)
{
	hrtime_t	start_time, end_time, wait_time;
	register struct mutex_stats *sp;
	register mutex_impl_t	*rlp;
	lkstat_t		*statsp;

	sp = MUTEX_STATS(lp);		/* get pointer to stats package */
	ASSERT(sp != MUTEX_NO_STATS);
	rlp = &sp->m_real;		/* real mutex inside stats package */
	statsp = sp->lkstat;		/* statsistics pointer */
	ASSERT(statsp != NULL);

	/*
	 * This function must not be called higher than splclock.
	 *
	 * This can be checked by looking at cpu_on_intr, but to be sure in the
	 * face of possible migration, preemption is disabled and on_intr is
	 * double-checked.
	 */
#ifdef MP
#if DEBUG
	if (CPU->cpu_on_intr && !panicstr) {
		kpreempt_disable();
		ASSERT(CPU->cpu_on_intr == 0);
		kpreempt_enable();
	}
#endif /* DEBUG */
#else /* MP */
	ASSERT(CPU->cpu_on_intr == 0 || panicstr);
#endif /* MP */

	/*
	 * Try to set the lock with adaptive_tryenter().
	 * If it fails, note the start time and wait in mutex_enter().
	 */
	if (!mutex_adaptive_tryenter(rlp)) {
		start_time = gethrtime();
		mutex_enter((kmutex_t *)rlp);

		/*
		 * read time and store as start of holding period, and compute
		 * wait time.  Unfortunately, the dl_t in the lockinfo struct
		 * isn't necessarily aligned right for hrtime_t, so we have
		 * to copy things.
		 */
		end_time = gethrtime();
		statsp->ls_fail++;
		start_time = end_time - start_time;
		*(dl_t *)&wait_time = statsp->ls_wtime;
		wait_time += start_time;
		statsp->ls_wtime = *(dl_t *)&wait_time;
	} else {
		end_time = gethrtime();
	}
	statsp->ls_stime = *(dl_t *)&end_time;
	statsp->ls_wrcnt++;		/* increment exclusive lock count */
}

int
mutex_adstat_tryenter(mutex_impl_t *lp)
{
	hrtime_t		 t1;
	register struct mutex_stats *sp;
	register mutex_impl_t	*rlp;
	lkstat_t		*statsp;
	int			rc;

	sp = MUTEX_STATS(lp);		/* get pointer to stats package */
	ASSERT(sp != MUTEX_NO_STATS);
	statsp = sp->lkstat;
	rlp = &sp->m_real;		/* real mutex inside stats package */

	rc = mutex_adaptive_tryenter(rlp);
	if (rc) {
		statsp = sp->lkstat;	/* statsistics pointer */
		ASSERT(statsp != NULL);
		statsp->ls_wrcnt++;	/* increment exclusive lock count */
		t1 = gethrtime();
		statsp->ls_stime = *(dl_t *)&t1; /* start time of hold */
	}
	return (rc);
}

/*
 * release a thread blocked on the adaptive mutex and unlock the mutex.
 */
static void
mutex_adstat_exit(mutex_impl_t *lp)
{
	hrtime_t		hold_time, now, start_time;
	register mutex_impl_t	*rlp;
	register struct mutex_stats *sp = MUTEX_STATS(lp);
	lkstat_t		*statsp = sp->lkstat;

	ASSERT(sp != MUTEX_NO_STATS);
	rlp = &sp->m_real;		/* real mutex inside stats package */

	now = gethrtime();

	if (MUTEX_OWNER(rlp) != curthread && panicstr == NULL)
		cmn_err(CE_PANIC,
		    "mutex_exit: mutex not held by thread: "
		    "owner %x thread %x",
		    (int)MUTEX_OWNER(rlp), (int)curthread);

	*(dl_t *)&start_time = statsp->ls_stime;
	hold_time = now - start_time;
	*(dl_t *)&start_time = statsp->ls_htime;
	hold_time += start_time;
	statsp->ls_htime = *(dl_t *)&hold_time;

	mutex_exit((kmutex_t *)rlp);		/* release real mutex */
}


static int
mutex_adstat_owned(mutex_impl_t *lp)
{
	register struct mutex_stats *sp = MUTEX_STATS(lp);

	ASSERT(sp != MUTEX_NO_STATS);
	return (MUTEX_OWNER(&sp->m_real) == curthread);
}

static kthread_t *
mutex_adstat_owner(mutex_impl_t *lp)
{
	register struct mutex_stats *sp = MUTEX_STATS(lp);
	register kthread_id_t t;

	ASSERT(sp != MUTEX_NO_STATS);
	t = MUTEX_OWNER(&sp->m_real);
	if (t == MUTEX_NO_OWNER)
		t = NULL;
	return (t);
}

static lkstat_t *
mutex_stat_get(mutex_impl_t *lp)
{
	register struct mutex_stats *sp = MUTEX_STATS(lp);

	ASSERT(sp != MUTEX_NO_STATS);
	return (sp->lkstat);
}

/*
 * Spin Mutexes.
 * 	Arg to init is the spl level associated with the lock.
 */
/* ARGSUSED */
static void
mutex_spin_init(mutex_impl_t *lp, char *name, kmutex_type_t type, void *arg)
{
#ifdef DEBUG
	if ((int)arg < ipltospl(LOCK_LEVEL)) {
		cmn_err(CE_PANIC, "mutex 0x%x initialized to 0x%x", lp, arg);
	}
#endif /* DEBUG */
	LOCK_INIT_CLEAR(&lp->m_spin.m_spinlock);
	lp->m_adaptive.m_lock = MUTEX_ADAPTIVE_HELD; /* indicates not default */
	lp->m_spin.m_minspl = (int)arg;
}

static void
mutex_spin_enter(mutex_impl_t *lp)
{
	/*
	 * Note:  Spin mutexes are used at high spl levels, so cannot do
	 * lock tracing.  Otherwise, lock tracing would have to block all
	 * interrupts.
	 *
	 * Also note that the mutex spl fields are only shorts, but this
	 * is enough for lock_set_spl on Suns.  Other machines may have
	 * to convert ipl levels to shorts or use larger mutexes.
	 */
	lp->m_spin.m_oldspl = lock_set_spl(&lp->m_spin.m_spinlock,
	    lp->m_spin.m_minspl);
}

static int
mutex_spin_tryenter(mutex_impl_t *lp)
{
	int	s;

	s = splr(lp->m_spin.m_minspl);
	if (lock_try(&lp->m_spin.m_spinlock)) {
		lp->m_spin.m_oldspl = (ushort_t)s;
		return (1);
	}
	(void) splx(s);
	return (0);
}

static void
mutex_spin_exit(mutex_impl_t *lp)
{
	lock_clear_splx(&lp->m_spin.m_spinlock, lp->m_spin.m_oldspl);
}

static int
mutex_spin_owned(mutex_impl_t *lp)
{
	return (LOCK_HELD(&lp->m_spin.m_spinlock));
}

/* ARGSUSED */
static kthread_t *
mutex_spin_owner(mutex_impl_t *lp)
{
	return (NULL);
}

/*
 * Spin Mutexes with statistics.
 * 	Arg to init is the spl level associated with the lock.
 *	The spl level must be non-zero.
 */
/* ARGSUSED */
static void
mutex_spstat_init(mutex_impl_t *lp, char *name, kmutex_type_t type, void *arg)
{
	register struct mutex_stats *sp;

	sp = mutex_stats_alloc(KM_SLEEP);	/* allocates zeroed struct */
	MUTEX_STATS_SET(lp, sp);	/* set stats pointer and dummylock */
	(void) sprintf_len(LOCK_NAME_LEN, sp->name, name,
	    (u_long)lp);	/* handle %x */
	sp->lkinfo.lk_name = sp->name;
	sp->lkstat = lkstat_alloc(&sp->lkinfo, KM_SLEEP);
	mutex_spin_init(&sp->m_real, name, MUTEX_SPIN, arg);
}


static void
mutex_spstat_enter(mutex_impl_t *lp)
{
	int	s;
	hrtime_t start_time, end_time;
	int waited = 0;
	register struct mutex_stats *sp = MUTEX_STATS(lp);
	register lkstat_t *statsp;
	register struct spin_mutex *rlp;

	ASSERT(sp != NULL);
	rlp = &sp->m_real.m_spin;
	statsp = sp->lkstat;
	ASSERT(statsp != NULL);

	start_time = gethrtime();			/* note start time */
	s = splr(rlp->m_minspl);
	if (lock_try(&rlp->m_spinlock)) {
		rlp->m_oldspl = (ushort_t)s;
	} else {
		(void) splx(s);
		rlp->m_oldspl = lock_set_spl(&rlp->m_spinlock, rlp->m_minspl);
		statsp->ls_fail++;
		waited = 1;
	}
	end_time = gethrtime();
	statsp->ls_stime = *(dl_t *)&end_time;

	if (waited) {
		/*
		 * compute wait time and add to accumulated wait time.
		 */
		start_time = end_time - start_time;
		*(dl_t *)&end_time = statsp->ls_wtime;
		end_time += start_time;
		statsp->ls_wtime = *(dl_t *)&end_time;
	}
	statsp->ls_wrcnt++;
}

static int
mutex_spstat_tryenter(mutex_impl_t *lp)
{
	hrtime_t t1;
	register struct mutex_stats *sp = MUTEX_STATS(lp);
	register lkstat_t *statsp;

	ASSERT(sp != MUTEX_NO_STATS);
	statsp = sp->lkstat;
	ASSERT(statsp != NULL);

	if (mutex_spin_tryenter(&sp->m_real)) {
		t1 = gethrtime();
		statsp->ls_stime = *(dl_t *)&t1;	/* start time of hold */
		statsp->ls_wrcnt++;
		return (1);
	}
	return (0);
}

static void
mutex_spstat_exit(mutex_impl_t *lp)
{
	hrtime_t	now, start_time, hold_time;
	register struct mutex_stats *sp = MUTEX_STATS(lp);
	register lkstat_t *statsp;
	register struct spin_mutex *rlp;

	ASSERT(sp != MUTEX_NO_STATS);
	rlp = &sp->m_real.m_spin;
	statsp = sp->lkstat;
	ASSERT(statsp != NULL);

	now = gethrtime();
	*(dl_t *)&start_time = statsp->ls_stime;
	hold_time = now - start_time;
	*(dl_t *)&start_time = statsp->ls_htime;
	hold_time += start_time;
	statsp->ls_htime = *(dl_t *)&hold_time;

	lock_clear_splx(&rlp->m_spinlock, rlp->m_oldspl);
}


/*
 * Driver Mutexes.
 * 	Arg to init is the spl level associated with the lock.
 *	The spl level arg can be zero if no interrupt handler uses the lock.
 *
 *  	These init routines change the type to adaptive or spin.
 *	First version of driver type is two-word mutex, same as current
 *	implementation of other mutexes.  If other mutex types change,
 *	driver mutexes must somehow adapt without changing the size of
 *	the kmutex_t structure passed by old drivers.   This can be done
 *	by dynamically allocating the mutex.
 */
static void
mutex_driver_init(mutex_impl_t *lp, char *name, kmutex_type_t type, void *arg)
{
	/*
	 * XXX - remain bug compatible with previous releases.  Some
	 * drivers passed in the address of the ddi_intr_cookie instead
	 * of the cookie itself.  After KERNELBASE was lowered the
	 * address became positive and the SPIN_LOCK macro returned true
	 * instead of false.  If it looks like an address, make sure the
	 * resulting mutex is of type MUTEX_DEFAULT.
	 */
	if ((caddr_t)arg >= (caddr_t)KERNELBASE)
		arg = 0;

	if (SPIN_LOCK((pl_t)arg)) {
		type = MUTEX_SPIN;
	} else {
		type = MUTEX_DEFAULT;
		arg = NULL;
	}
	mutex_init((kmutex_t *)lp, name, type, arg);
}

/*
 * Driver Mutexes with statistics.
 * 	Arg to init is the spl level associated with the lock.
 *	The spl level arg can be zero if no interrupt handler uses the lock.
 *	See comments on mutex_driver_init().
 */
static void
mutex_drstat_init(mutex_impl_t *lp, char *name, kmutex_type_t type, void *arg)
{
	if (SPIN_LOCK((pl_t)arg)) {
		type = MUTEX_SPIN_STAT;
	} else {
		type = MUTEX_ADAPTIVE_STAT;
		arg = NULL;
	}
	mutex_init((kmutex_t *)lp, name, type, arg);
}

/*
 * Adaptive Default:  use statistics if patched on.
 */
static void
mutex_addef_init(mutex_impl_t *lp, char *name, kmutex_type_t type, void *arg)
{
	if (lock_stats)
		type = MUTEX_ADAPTIVE_STAT;
	else
		type = MUTEX_ADAPTIVE;

	mutex_init((kmutex_t *)lp, name, type, arg);
}

/*
 * Place holder for driver mutex operations.  Since the init routine
 * changes the type, this function should never be called.
 */
static void
mutex_err(mutex_impl_t *lp)
{
	cmn_err(CE_PANIC, "mutex_err called lp %x\n", (int)lp);
}

static void *mutex_stats_header;
static struct kmem_cache *mutex_stats_cache;

static struct mutex_stats *
mutex_stats_alloc(int sleep)
{
	register struct mutex_stats *sp;

	if (kmem_ready) {
		if (mutex_stats_cache == NULL) {
			int old_lock_stats = lock_stats;
			lock_stats = 0;
			mutex_stats_cache =
				kmem_cache_create("mutex_stats_cache",
					sizeof (struct mutex_stats),
					PTR24_ALIGN, NULL, NULL, NULL);
			lock_stats = old_lock_stats;
		}
		sp = kmem_cache_alloc(mutex_stats_cache, sleep);
		bzero((caddr_t)sp, sizeof (*sp));
	} else {
		sp = (struct mutex_stats *)
			startup_alloc(sizeof (struct mutex_stats),
			&mutex_stats_header);
		bzero((caddr_t)sp, sizeof (*sp));
		sp->flag = MSTAT_STARTUP_ALLOC;
	}
	if (((u_int)sp % PTR24_ALIGN) != 0)
		panic("mutex_stats_alloc: bad alignment for mutex_stats");
	if (sp == NULL)
		panic("mutex_stats_alloc: can't alloc mutex_stats");
	return (sp);
}

static void
mutex_stats_free(struct mutex_stats *sp)
{
	if (sp->flag & MSTAT_STARTUP_ALLOC) {
		startup_free((void *)sp, sizeof (*sp), &mutex_stats_header);
	} else {
		kmem_cache_free(mutex_stats_cache, sp);
	}
}
