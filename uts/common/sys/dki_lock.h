/*
 *	Copyright (c) 1991, Sun Microsystems, Inc.
 */

#ifndef	_SYS_DKI_LOCK_H
#define	_SYS_DKI_LOCK_H

#pragma ident	"@(#)dki_lock.h	1.12	93/11/01 SMI"

/*
 * DKI/DDI MT synchronization primitives.
 */

/*
 * The driver including this file may define:
 *	_LOCKTEST	- to select versions of these routines that do
 *			hierarchy checking of the locks.
 *	_MPSTATS	- to select versions of these routines that keep
 *			statistics on lock usage and collisions.
 */

#include <sys/dki_lkinfo.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef int	bool_t;		/* XXX - properly belongs in types.h */
typedef int	pl_t;		/* XXX - belongs in machtypes.h */

#define	FALSE	0
#define	TRUE	1


#ifdef	__STDC__
/*
 * lock primitives.
 */
void	dki_lock_setup(lksblk_t *);
lock_t	*dki_lock_alloc(char hierarchy, pl_t min_pl, lkinfo_t *lkinfo);
lock_t	*dki_lock_alloc_stat(char hierarchy, pl_t min_pl, lkinfo_t *lkinfo);
pl_t	dki_lock(lock_t *lockp, pl_t pl);
pl_t	dki_trylock(lock_t *lockp, pl_t pl);
void	dki_unlock(lock_t *lockp, pl_t pl);
void	dki_lock_dealloc(lock_t *lockp);
bool_t	ddi_lock_held(lock_t *lockp);		/* Sun-DDI only */
int	lockstat(lock_t *lp, lkstat_t *statp);
#endif /* __STDC__ */

/*
 * Macros to be used by driver to select proper lock primitives based
 * on whether _LOCKTEST and/or _MPSTATS is defined.
 * For now, only two varieties.
 */
#if defined(_LOCKTEST) || defined(_MPSTATS)
#define	LOCK_ALLOC	dki_lock_alloc_stat
#else
#define	LOCK_ALLOC	dki_lock_alloc
#endif /* _LOCKTEST || _MPSTATS */

#define	LOCK		dki_lock
#define	TRYLOCK		dki_trylock
#define	UNLOCK		dki_unlock
#define	LOCK_DEALLOC	dki_lock_dealloc
#define	DDI_LOCK_HELD	ddi_lock_held

/*
 * Return value for TRYLOCK.
 */
#define	INVPL		((pl_t)(-1))	/* XXX - model-dependent */

/*
 * Synchronization variables.
 */
typedef	void	sv_t;		/* opaque type to be used by driver for sv */

#ifdef	__STDC__
sv_t	*dki_sv_alloc(int flag);
void	dki_sv_dealloc(sv_t *svp);
void	dki_sv_wait(sv_t *svp, int priority, lock_t *lkp);
bool_t	dki_sv_wait_sig(sv_t *svp, int priority, lock_t *lkp);
void	dki_sv_signal(sv_t *svp, int flags);
void	dki_sv_broadcast(sv_t *svp, int flags);
#endif	/* __STDC__ */

#define	SV_ALLOC	dki_sv_alloc
#define	SV_DEALLOC	dki_sv_dealloc
#define	SV_WAIT		dki_sv_wait
#define	SV_WAIT_SIG	dki_sv_wait_sig
#define	SV_SIGNAL	dki_sv_signal
#define	SV_BROADCAST	dki_sv_broadcast

/*
 * Condition variables.
 *	XXX - these are obsolete and replaced by synchronization variable.s
 */
#ifdef _KERNEL
typedef	void	cond_t;		/* opaque type to be used by driver for cond */
#endif

#ifdef	__STDC__
cond_t	*dki_cond_alloc(void);
void	dki_cond_dealloc(cond_t *condp);
void	dki_cond_wait(cond_t *condp, int priority, lock_t *lkp, pl_t pl);
bool_t	dki_cond_wait_sig(cond_t *condp, int priority, lock_t *lkp, pl_t pl);
void	dki_cond_signal(cond_t *condp, int flags);
void	dki_cond_broadcast(cond_t *condp, int flags);
#endif	/* __STDC__ */

#define	COND_ALLOC	dki_cond_alloc
#define	COND_DEALLOC	dki_cond_dealloc
#define	COND_WAIT	dki_cond_wait
#define	COND_WAIT_SIG	dki_cond_wait_sig
#define	COND_SIGNAL	dki_cond_signal
#define	COND_BROADCAST	dki_cond_broadcast

/* XXX - temporary definitions for priority arguments to cond_wait */
/* XXX - these should be somewhere else */

#define	PRISAME	0
#define	PRI0	-5
#define	PRI1	-4
#define	PRI2	-3
#define	PRI3	-2
#define	PRI4	-1
#define	PRI5	1
#define	PRI6	2
#define	PRI7	3
#define	PRI8	4
#define	PRI9	5

/*
 * Flags for COND_SIGNAL() and COND_BROADCAST()
 */
#define	NOPRMT		1	/* don't preempt the current thread */


/*
 * Readers/writer locks.
 */
#if defined(__STDC__) && defined(_KERNEL)
typedef void rwlock_t;
rwlock_t *dki_rwlock_alloc(char hierarchy, pl_t min_pl, lkinfo_t *lkinfo);
rwlock_t *dki_rwlock_alloc_stat(char hierarchy, pl_t min_pl, lkinfo_t *lkinfo);
void	dki_rwlock_dealloc(rwlock_t *lockp);
pl_t	dki_rw_rdlock(rwlock_t *lockp, pl_t pl);
pl_t	dki_rw_wrlock(rwlock_t *lockp, pl_t pl);
pl_t	dki_rw_tryrdlock(rwlock_t *lockp, pl_t pl);
pl_t	dki_rw_trywrlock(rwlock_t *lockp, pl_t pl);
void	dki_rw_unlock(rwlock_t *lockp, pl_t pl);
int	rwlockstat(rwlock_t *rwlp, lkstat_t *statp);
#endif	/* defined (__STDC__) && defined (_KERNEL) */

#if defined(_LOCKTEST) || defined(_MPSTATS)
#define	RWLOCK_ALLOC	dki_rwlock_alloc_stat
#else
#define	RWLOCK_ALLOC	dki_rwlock_alloc
#endif /* _LOCKTEST || _MPSTATS */
#define	RWLOCK_DEALLOC	dki_rwlock_dealloc
#define	RW_RDLOCK	dki_rw_rdlock
#define	RW_WRLOCK	dki_rw_wrlock
#define	RW_TRYRDLOCK	dki_rw_tryrdlock
#define	RW_TRYWRLOCK	dki_rw_trywrlock
#define	RW_UNLOCK	dki_rw_unlock


/*
 * Sleep locks.
 */
typedef void	sleep_t;	/* opaque type of sleep lock for driver use */

#ifdef	__STDC__
sleep_t	*dki_sleep_alloc(char hierarchy, lkinfo_t *lkinfo);
sleep_t	*dki_sleep_alloc_stat(char hierarchy, lkinfo_t *lkinfo);
void	dki_sleep_dealloc(sleep_t *lockp);
void	dki_sleep_lock(sleep_t *lockp, int priority);
bool_t	dki_sleep_lock_sig(sleep_t *lockp, int priority);
bool_t	dki_sleep_trylock(sleep_t *lockp);
void	dki_sleep_unlock(sleep_t *lockp);
bool_t	dki_sleep_lockavail(sleep_t *lockp);
bool_t	dki_sleep_lockblkd(sleep_t *lockp);
bool_t	dki_sleep_lockowned(sleep_t *lockp);
int	sleeplockstat(sleep_t *rwlp, lkstat_t *statp);
#endif	/* __STDC__ */

#if defined(_LOCKTEST) || defined(_MPSTATS)
#define	SLEEP_ALLOC	dki_sleep_alloc_stat
#else
#define	SLEEP_ALLOC	dki_sleep_alloc
#endif /* _LOCKTEST || _MPSTATS */
#define	SLEEP_LOCK	dki_sleep_lock
#define	SLEEP_LOCK_SIG	dki_sleep_lock_sig
#define	SLEEP_TRYLOCK	dki_sleep_trylock
#define	SLEEP_UNLOCK	dki_sleep_unlock
#define	SLEEP_LOCKAVAIL	dki_sleep_lockavail
#define	SLEEP_LOCKBLKD	dki_sleep_lockblkd
#define	SLEEP_LOCKOWNED	dki_sleep_lockowned

#ifdef __STDC__
void lkstat_free(lkstat_t *lkstatp, bool_t nullinfo);
lkstat_t *lkstat_alloc(lkinfo_t *lkinfop, int sleep);
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_DKI_LOCK_H */
