#ident	"@(#)dki_lock.c	1.20	95/02/17 SMI"

/*
 * Locking primitives mandated by Unix International.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/dki_lkinfo.h>
#include <sys/t_lock.h>
#include <sys/thread.h>
#include <sys/proc.h>
#include <sys/dki_lock.h>
#include <sys/kmem.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/cmn_err.h>
#include <sys/mutex_impl.h>
#include <sys/debug.h>
#include <sys/spl.h>

kmutex_t lkstat_mutex;

lksblk_t *lksblks_head;		/* head of list of lkstat blocks */

lkstat_sum_t	*lkstat_sums;

/*
 * Internal (opaque to drivers) structure of a lock.
 */
struct dki_lock {
	kmutex_t			dl_mutex;
	lkinfo_t		*dl_lkinfo;
};

/*
 * Internal (opaque to drivers) structure of a sleep lock.
 */
struct dki_sleep_lock {
	kmutex_t			dl_mutex;
	kcondvar_t		dl_cv;
	kthread_id_t		dl_owner;
	int			dl_blocked;
};

/*
 * allocate a lock.
 */
/* ARGSUSED */
lock_t *
dki_lock_alloc(char hierarchy, pl_t min_pl, lkinfo_t *lkinfo)
{
	struct dki_lock	*lp;

	lp = (struct dki_lock *)kmem_zalloc(sizeof (*lp), KM_SLEEP);
	lp->dl_lkinfo = lkinfo;
	mutex_init(&lp->dl_mutex, lkinfo->lk_name,
		MUTEX_DRIVER, (void *)min_pl);
	return ((lock_t *)lp);
}


/* ARGSUSED */
lock_t *
dki_lock_alloc_stat(char hierarchy, pl_t min_pl, lkinfo_t *lkinfo)
{
	struct dki_lock	*lp;
	lkstat_t	*statp;

	lp = (struct dki_lock *)kmem_zalloc(sizeof (*lp), KM_SLEEP);
	lp->dl_lkinfo = lkinfo;
	mutex_init(&lp->dl_mutex, lkinfo->lk_name,
		MUTEX_DRIVER_STAT, (void *)min_pl);
	statp = mutex_stats(&lp->dl_mutex);
	statp->ls_infop = lkinfo;
	return ((lock_t *)lp);
}

/*
 * XXX - not specified in latest DKI (draft 9/12/90).
 * XXX - reverify interface.
 */
int
lockstat(lock_t *lockp, lkstat_t *statp)
{
	register kmutex_t *lp = (kmutex_t *)lockp;
	register lkstat_t *sp;
	sp = mutex_stats(lp);
	if (sp != NULL) {
		*statp = *sp;		/* copy statistics structure */
		return (0);
	} else {
		return (EINVAL);
	}
}

/* ARGSUSED */
pl_t
dki_lock(lock_t *lockp, pl_t pl)
{
	register kmutex_t *lp = (kmutex_t *)lockp;

	mutex_enter(lp);
	return (0);
}

/* ARGSUSED */
pl_t
dki_trylock(lock_t *lockp, pl_t pl)
{
	register kmutex_t *lp = (kmutex_t *)lockp;

	if (mutex_tryenter(lp))
		return (0);
	else
		return (INVPL);
}

/* ARGSUSED */
void
dki_unlock(lock_t *lockp, pl_t pl)
{
	register kmutex_t *lp = (kmutex_t *)lockp;

	mutex_exit(lp);
}


void
dki_lock_dealloc(lock_t *lockp)
{
	struct dki_lock *lp = (struct dki_lock *)lockp;

	mutex_destroy(&lp->dl_mutex);
	kmem_free(lp, sizeof (struct dki_lock));
}

bool_t
ddi_lock_held(lock_t *lockp)
{
	register kmutex_t *lp = (kmutex_t *)lockp;

	return (mutex_owned(lp));
}

/*
 * condition variables.
 * These map directly on top of kcondvar_t.
 */
cond_t *
dki_cond_alloc(void)
{
	/*
	 * XXX - unfortunately kmem_zalloc gives us 8 bytes though we want two.
	 */
	return ((cond_t *)kmem_zalloc(sizeof (kcondvar_t), KM_SLEEP));
}


void
dki_cond_dealloc(cond_t *condp)
{
	kmem_free(condp, sizeof (kcondvar_t));
}


/* ARGSUSED1 */
void
dki_cond_wait(cond_t *condp, int priority, lock_t *lockp, pl_t pl)
{
	register kmutex_t *lp = (kmutex_t *)lockp;

	cv_wait((kcondvar_t *)condp, lp);
}


/* ARGSUSED1 */
bool_t
dki_cond_wait_sig(cond_t *condp, int priority, lock_t *lockp, pl_t pl)
{
	register kmutex_t *lp = (kmutex_t *)lockp;

	return (cv_wait_sig((kcondvar_t *)condp, lp));
}


/* ARGSUSED1 */
void
dki_cond_signal(cond_t *condp, int flags)
{
	cv_signal((kcondvar_t *)condp);
}


/* ARGSUSED1 */
void
dki_cond_broadcast(cond_t *condp, int flags)
{
	cv_broadcast((kcondvar_t *)condp);
}

/*
 * Synchronization variables.
 * For now, implmented on top of condition variables, although this is
 * less efficient.
 */
sv_t	*
dki_sv_alloc(int flag)
{
	return ((sv_t *)kmem_zalloc(sizeof (kcondvar_t), flag));
}

void
dki_sv_dealloc(sv_t *svp)
{
	kmem_free(svp, sizeof (kcondvar_t));
}

/* ARGSUSED */
void
dki_sv_wait(sv_t *svp, int priority, lock_t *lkp)
{
	register struct dki_lock *lp = (struct dki_lock *)lkp;

	cv_wait((kcondvar_t *)svp, &lp->dl_mutex);
	mutex_exit(&lp->dl_mutex);
}

/* ARGSUSED */
bool_t
dki_sv_wait_sig(sv_t *svp, int priority, lock_t *lkp)
{
	register struct dki_lock *lp = (struct dki_lock *)lkp;
	bool_t	rv;

	rv = cv_wait_sig((kcondvar_t *)svp, &lp->dl_mutex);
	mutex_exit(&lp->dl_mutex);
	return (rv);
}

/* ARGSUSED1 */
void
dki_sv_signal(sv_t *svp, int flags)
{
	cv_signal((sv_t *)svp);
}

/* ARGSUSED1 */
void
dki_sv_broadcast(sv_t *svp, int flags)
{
	cv_broadcast((sv_t *)svp);
}

/*
 * Sleep locks.
 */

/*
 * allocate a sleep lock.
 */
/* ARGSUSED */
sleep_t *
dki_sleep_alloc(char hierarchy, lkinfo_t *lkinfo)
{
	struct dki_sleep_lock *lp;

	lp = (struct dki_sleep_lock *)kmem_zalloc(sizeof (*lp), KM_SLEEP);
	mutex_init(&lp->dl_mutex, lkinfo->lk_name, MUTEX_DEFAULT, NULL);
	return ((sleep_t *)lp);
}


sleep_t *
dki_sleep_alloc_stat(char hierarchy, lkinfo_t *lkinfo)
{
	struct dki_sleep_lock *lp;
	struct mutex_stats *sp;

	if (lkinfo->lk_flags & NOSTATS)
		return (dki_sleep_alloc(hierarchy, lkinfo));

	lp = (struct dki_sleep_lock *)kmem_zalloc(sizeof (*lp), KM_SLEEP);
	mutex_init(&lp->dl_mutex, lkinfo->lk_name, MUTEX_ADAPTIVE_STAT, NULL);
	sp = MUTEX_STATS((mutex_impl_t *)&lp->dl_mutex);
	sp->lkinfo = *lkinfo;
	return ((sleep_t *)lp);
}

void
dki_sleep_dealloc(sleep_t *lockp)
{
	register struct dki_sleep_lock *lp = (struct dki_sleep_lock *)lockp;

	mutex_destroy(&lp->dl_mutex);
	kmem_free((caddr_t)lp, sizeof (struct dki_sleep_lock));
}


/* ARGSUSED1 */
void
dki_sleep_lock(sleep_t *lockp, int priority)
{
	register struct dki_sleep_lock *lp = (struct dki_sleep_lock *)lockp;

	mutex_enter(&lp->dl_mutex);
	while (lp->dl_owner) {
		lp->dl_blocked++;
		cv_wait(&lp->dl_cv, &lp->dl_mutex);
		lp->dl_blocked--;
	}
	lp->dl_owner = curthread;
	mutex_exit(&lp->dl_mutex);
}


/* ARGSUSED1 */
bool_t
dki_sleep_lock_sig(sleep_t *lockp, int priority)
{
	register struct dki_sleep_lock *lp = (struct dki_sleep_lock *)lockp;
	int	rv = 1;

	mutex_enter(&lp->dl_mutex);
	while (lp->dl_owner) {
		lp->dl_blocked++;
		rv = cv_wait_sig(&lp->dl_cv, &lp->dl_mutex);
		lp->dl_blocked--;
		if (rv == 0)
			break;
	}
	if (rv)
		lp->dl_owner = curthread;
	mutex_exit(&lp->dl_mutex);
	return (rv);
}


bool_t
dki_sleep_trylock(sleep_t *lockp)
{
	register struct dki_sleep_lock *lp = (struct dki_sleep_lock *)lockp;
	register bool_t	v;

	mutex_enter(&lp->dl_mutex);
	v = (lp->dl_owner == NULL);
	if (v)
		lp->dl_owner = curthread;
	mutex_exit(&lp->dl_mutex);
	return (v);
}


void
dki_sleep_unlock(sleep_t *lockp)
{
	register struct dki_sleep_lock *lp = (struct dki_sleep_lock *)lockp;

	mutex_enter(&lp->dl_mutex);
	ASSERT(lp->dl_owner == curthread);
	lp->dl_owner = NULL;
	if (lp->dl_blocked)
		cv_signal(&lp->dl_cv);
	mutex_exit(&lp->dl_mutex);
}


bool_t
dki_sleep_lockavail(sleep_t *lockp)
{
	register struct dki_sleep_lock *lp = (struct dki_sleep_lock *)lockp;

	/*
	 * Mutex not needed.  Caller should understand that the lock may
	 * be held even if lockavail returns TRUE.
	 */
	return (lp->dl_owner == NULL);
}


bool_t
dki_sleep_lockblkd(sleep_t *lockp)
{
	register struct dki_sleep_lock *lp = (struct dki_sleep_lock *)lockp;

	/*
	 * mutex not needed.  Caller should understand that the return
	 * value is potentially stale.
	 */
	return (lp->dl_blocked != 0);
}


bool_t
dki_sleep_lockowned(sleep_t *lockp)
{
	register struct dki_sleep_lock *lp = (struct dki_sleep_lock *)lockp;

	/*
	 * Mutex not needed.  If owner is the current thread, it won't change.
	 */
	return (lp->dl_owner == curthread);
}

int
sleeplockstat(sleep_t *lockp, lkstat_t *statp)
{
	register struct dki_sleep_lock *lp = (struct dki_sleep_lock *)lockp;
	lkstat_t *lkstat;

	if ((lkstat = mutex_stats(&lp->dl_mutex)) != NULL) {
		*statp = *lkstat;
		return (0);
	}
	return (EINVAL);
}

/*
 * Readers/writer locks.
 * Sun's readers/writers lock use the same typedef (krwlock_t).
 * But have a slightly different primitive set.
 */
/* ARGSUSED */
rwlock_t *
dki_rwlock_alloc(char hierarchy, pl_t min_pl, lkinfo_t *lkinfo)
{
	krwlock_t 	*lp;

	lp = kmem_alloc(sizeof (*lp), KM_SLEEP);
	if (min_pl > ipltospl(LOCK_LEVEL)) {
		panic("dki_rwlock_alloc: min_pl can't exceed LOCK_LEVEL");
	} else {
		rw_init(lp, lkinfo->lk_name, RW_DEFAULT, NULL);
	}
	return (lp);
}


/* ARGSUSED */
rwlock_t *
dki_rwlock_alloc_stat(char hierarchy, pl_t min_pl, lkinfo_t *lkinfo)
{
	krwlock_t 	*lp;

	lp = kmem_alloc(sizeof (*lp), KM_SLEEP);
	if (min_pl > ipltospl(LOCK_LEVEL)) {
		panic("dki_rwlock_alloc_stat: min_pl can't exceed LOCK_LEVEL");
	} else {
		/* lock stats not implemented yet */
		rw_init(lp, lkinfo->lk_name, RW_DEFAULT, NULL);
	}
	return (lp);
}


void
dki_rwlock_dealloc(rwlock_t *lockp)
{
	rw_destroy((krwlock_t *)lockp);
}


/* ARGSUSED */
pl_t
dki_rw_rdlock(rwlock_t *lockp, pl_t pl)
{
	rw_enter((krwlock_t *)lockp, RW_READER);
	return (0);
}


/* ARGSUSED1 */
pl_t
dki_rw_wrlock(rwlock_t *lockp, pl_t pl)
{
	rw_enter((krwlock_t *)lockp, RW_WRITER);
	return (0);
}


/* ARGSUSED1 */
pl_t
dki_rwlock_tryrdlock(rwlock_t *lockp, pl_t pl)
{
	if (rw_tryenter((krwlock_t *)lockp, RW_READER))
		return (0);
	return (INVPL);
}


/* ARGSUSED1 */
pl_t
dki_rwlock_trywrlock(rwlock_t *lockp, pl_t pl)
{
	if (rw_tryenter((krwlock_t *)lockp, RW_WRITER))
		return (0);
	return (INVPL);
}


/* ARGSUSED1 */
void
dki_rw_unlock(rwlock_t *lockp, pl_t pl)
{
	rw_exit((krwlock_t *)lockp);
}

static void
lksblk_init(lksblk_t *lksblk)
{
	/* setup the freelist */
	lksblk->lsb_free = &lksblk->lsb_bufs[0];
	for (lksblk->lsb_nfree = 0; lksblk->lsb_nfree < (LSB_NLKDS - 1);
				lksblk->lsb_nfree++) {
		lksblk->lsb_bufs[lksblk->lsb_nfree].un.lsu_next =
			&lksblk->lsb_bufs[lksblk->lsb_nfree + 1];
	}
	lksblk->lsb_nfree =  LSB_NLKDS;
}
/* XXX - this stuff belongs in some header file. */

extern void *	startup_alloc(size_t size, void ** header);
extern void	startup_free(void *p, size_t size, void **header);

static void *	lkstats_header;		/* base for startup allocation */

static lksblk_t *
lksblk_alloc(int sleep)
{
	lksblk_t *lksblk;

	ASSERT(MUTEX_HELD(&lkstat_mutex));

	mutex_exit(&lkstat_mutex);
	if (kmem_ready) {
		lksblk = (lksblk_t *)kmem_zalloc(sizeof (lksblk_t), sleep);
	} else {
		lksblk = (lksblk_t *)
			startup_alloc(sizeof (lksblk_t), &lkstats_header);
	}
	mutex_enter(&lkstat_mutex);
	lksblk_init(lksblk);
	if (!kmem_ready)
		lksblk->lsb_nfree--;	/* keep block from ever being freed */
	return (lksblk);
}

static lkstat_t *
getlkstat(lksblk_t *lksblk)
{
	lkstat_t *lkstat;

	ASSERT(MUTEX_HELD(&lkstat_mutex));
	if (lksblk->lsb_nfree == 0) { /* no free lksblks */
		return (NULL);
	} else { /* get first lkstat off the freelist */
		lkstat = lksblk->lsb_free;
		lksblk->lsb_free = lkstat->un.lsu_next;
		lksblk->lsb_nfree--;
		bzero((caddr_t)lkstat, sizeof (lkstat_t));
		return (lkstat);
	}
}

lkstat_t *
lkstat_alloc(lkinfo_t *lkinfop, int sleep)
{
	lkstat_t *lkstat;
	lksblk_t *lksblk;

	mutex_enter(&lkstat_mutex);
	lksblk = lksblks_head;

	if (lksblk == NULL) { /* first alloc */
		lksblks_head = lksblk_alloc(sleep);
		lkstat = getlkstat(lksblks_head);
	} else {
		/* search for free lkstat in list of lksblks */
		while (((lkstat = getlkstat(lksblk)) == NULL) &&
			(lksblk->lsb_next != NULL)) {
			lksblk = lksblk->lsb_next;
		}
		if (lkstat == NULL) {
			/* didn't find one */
			/* allocate a new lksblk */
			lksblk = lksblk_alloc(sleep);
			/* put it at the front of the list */
			lksblk->lsb_next = lksblks_head;
			lksblks_head->lsb_prev = lksblk;
			lksblks_head = lksblk;
			/* allocate a new lkstat out of it */
			lkstat = getlkstat(lksblks_head);
		}  /* else found a free lkstat in one of the lksblks */
	}
	lkstat->ls_infop = lkinfop;
	mutex_exit(&lkstat_mutex);
	return (lkstat);
}

void
lkstat_free(lkstat_t *lkstatp, bool_t nullinfo)
{
	lksblk_t *lksblk;


	mutex_enter(&lkstat_mutex);
	lksblk = lksblks_head;

	/* find out which lksblk lkstat was allocated from */
	while ((lkstatp < &lksblk->lsb_bufs[0]) ||
		(lkstatp > &lksblk->lsb_bufs[LSB_NLKDS])) {
		lksblk = lksblk->lsb_next;
	}

	if (nullinfo == TRUE) {
		lkstatp->ls_infop = NULL;
	}

	/* put it at front of the free list */
	lkstatp->un.lsu_next = lksblk->lsb_free;
	lksblk->lsb_free = lkstatp;
	lksblk->lsb_nfree++;

	/* if all lkstats on this lksblk's free list are free */
	/* remove the lksblk from the list and free it */
	if (lksblk->lsb_nfree == LSB_NLKDS) {
		if (lksblk->lsb_prev == NULL) { /* first on list */
			lksblks_head = lksblk->lsb_next;
			if (lksblk->lsb_next != NULL) { /* not tail of list */
				lksblks_head->lsb_prev = NULL;
			}
		} else if (lksblk->lsb_next == NULL) { /* tail of list */
			lksblk->lsb_prev->lsb_next = NULL;
		} else { /* middle of list */
			lksblk->lsb_prev->lsb_next = lksblk->lsb_next;
			lksblk->lsb_next->lsb_prev = lksblk->lsb_prev;
		}
		mutex_exit(&lkstat_mutex);
		kmem_free((caddr_t)lksblk, sizeof (lksblk_t));
		mutex_enter(&lkstat_mutex);
	}
	mutex_exit(&lkstat_mutex);
}


/* called from startup() */
/* ARGSUSED */
void
dki_lock_setup(lksblk_t *lksblks_head)
{
	mutex_init(&lkstat_mutex, "lkstat mutex", MUTEX_ADAPTIVE, NULL);
}


static void
lkstat_sumup(lkstat_t *sp, lkstat_sum_t *lss)
{
	hrtime_t	t1, t2;

	ASSERT(MUTEX_HELD(&lkstat_mutex));
	lss->sp->ls_wrcnt += sp->ls_wrcnt;
	lss->sp->ls_rdcnt += sp->ls_rdcnt;
	lss->sp->ls_solordcnt += sp->ls_solordcnt;
	lss->sp->ls_fail += sp->ls_fail;

	/*
	 * Add times.  Cannot use hrtime_t in lockstats because of alignment.
	 */
	*(dl_t *)&t1 = lss->sp->ls_wtime;
	*(dl_t *)&t2 = sp->ls_wtime;
	t1 += t2;
	lss->sp->ls_wtime = *(dl_t *)&t1;

	*(dl_t *)&t1 = lss->sp->ls_htime;
	*(dl_t *)&t2 = sp->ls_htime;
	t1 += t2;
	lss->sp->ls_htime = *(dl_t *)&t1;
}

void
lkstat_sum_on_destroy(lkstat_t *sp)
{
	lkstat_t *nsp;
	lkinfo_t *infop;
	lkstat_sum_t *lss;
	/*
	 * utility structure to collect kmem_allocation.
	 */
	struct sum_alloc {
		lkstat_sum_t	sum;
		lkinfo_t	lkinfo;
		lkstat_t	lkstat;
		char		name[LOCK_NAME_LEN];
	} *sap;

	mutex_enter(&lkstat_mutex);
	lss = lkstat_sums;

	while (lss != NULL) {
		if (strcmp(sp->ls_infop->lk_name,
		    lss->sp->ls_infop->lk_name) == 0)
			break;
		lss = lss->next;
	}

	if (lss != NULL) { /* found */
		lkstat_sumup(sp, lss);
	} else { /* start a new one */
		mutex_exit(&lkstat_mutex);
		sap = (struct sum_alloc *)kmem_zalloc(sizeof (*sap), KM_SLEEP);
		lss = &sap->sum;
		infop = &sap->lkinfo;
		nsp = &sap->lkstat;
		mutex_enter(&lkstat_mutex);
		/*
		 * copy name into summing stats structure.
		 */
		strncpy(sap->name, sp->ls_infop->lk_name, LOCK_NAME_LEN);
		infop->lk_name = sap->name;
		nsp->ls_infop = infop;
		lss->sp = nsp;

		lss->next = lkstat_sums;	/* link onto list */
		lkstat_sums = lss;
		lkstat_sumup(sp, lss);
	}
	mutex_exit(&lkstat_mutex);
}
