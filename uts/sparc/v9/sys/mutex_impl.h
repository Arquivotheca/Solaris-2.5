/*
 *	Copyright (c) 1991,1993 Sun Microsystems, Inc.
 */

#ifndef _SYS_MUTEX_IMPL_H
#define	_SYS_MUTEX_IMPL_H

#pragma ident	"@(#)mutex_impl.h	1.9	95/07/04 SMI"

#ifndef	_ASM
#include <sys/types.h>
#include <sys/machlock.h>
#include <sys/turnstile.h>
#include <sys/dki_lkinfo.h>
#endif

#ifdef	__cplusplus
extern "C" {
#endif

#ifndef	_ASM

/*
 * Implementation-specific Mutex structure:
 *
 * The m_lock field in the low-order byte of the first word of the mutex
 * is used as the lock for adaptive mutexes.  By leaving this byte set and
 * otherwise unused in other types of mutexes, the adaptive mutex_enter()
 * can simply try to set it without first checking the type.  If the ldstub
 * isn't successful on the first try, then the type field is checked before
 * proceeding.
 *
 * The owner field is stuffed into the first word with the lock so that
 * both the lock and the owner can be cleared at the same time with a single
 * store instruction.  This allows priority inheritance to work correctly
 * even if mutex_exit() is preempted.
 *
 * The lock and type fields must be bytes 0 and 7, respectively, in all
 * mutex types.
 */
typedef union mutex_impl {
	/*
	 * Generic mutex.  Fields common to all mutex types.
	 */
	struct generic_mutex {
		lock_t	m_lock;		/* 0	reserved for adaptive lock */
		char	_filler[6];	/* 1-6  type specific */
		uchar_t	m_type;		/* 7	type */
	} m_generic;

	/*
	 * Default adaptive mutex (without stats).
	 */
	struct adaptive_mutex {
		lock_t	m_lock;			/* 0	lock */
		uint_t	m_owner : 24;	/* 1-3  24 bits of owner thread_id */
		turnstile_id_t	m_waiters;	/* 4-5 turnstile cookie */
		disp_lock_t	m_wlock;	/* 6	waiters field lock */
		uchar_t		m_type;		/* 7	type (zero) */
	} m_adaptive;

	/*
	 * Another way of looking at adaptive mutexes.
	 */
	struct adaptive_mutex2 {
		uint_t		m_owner_lock;	/* 0-3	owner and lock */
		turnstile_id_t	m_waiters;	/* 4-5 turnstile cookie */
		disp_lock_t	m_wlock;	/* 6	waiters field lock */
		uchar_t		m_type;		/* 7	type (zero) */
	} m_adaptive2;

	/*
	 * Spin Mutex.
	 */
	struct spin_mutex {
		lock_t	m_dummylock;	/* 0	lock (always set) */
		lock_t	m_spinlock;	/* 1	real lock */
		ushort_t m_oldspl;	/* 2-3	old %psr value */

		ushort_t m_minspl;	/* 4-5	min PSR_PIL val if lock held */
		char	_filler;	/* 6	unused */
		uchar_t	m_type;		/* 7	type */
	} m_spin;

	/*
	 * Any mutex with statistics.
	 */
	struct stat_mutex {
		uint_t	m_stats_lock;	/* 0	non-zero lock followed by */
					/* 1-3	24 bits of stats package ptr */
		uchar_t	_filler[3];	/* 4-6	unused */
		uchar_t	m_type;		/* 7	type */
	} m_stats;
} mutex_impl_t;

/*
 * Macro to retrieve 32-bit pointer field out of mutex.
 * Relies on 32-byte alignment of mutex_stats and thread structures.
 */
#define	MUTEX_OWNER_PTR(owner_lock)	(owner_lock)

#define	MUTEX_OWNER_LOCK(lp)	(((struct adaptive_mutex2 *)(lp))->m_owner_lock)
#define	MUTEX_PTR(lp)		MUTEX_OWNER_PTR(MUTEX_OWNER_LOCK(lp))
#define	MUTEX_OWNER(lp)		((kthread_id_t)MUTEX_PTR(lp))
#define	MUTEX_NO_OWNER		((kthread_id_t)NULL)
#define	MUTEX_STATS_SET(lp, pt)	((lp)->m_stats.m_stats_lock = ((int)(pt)))
#define	MUTEX_NO_STATS		((struct mutex_stats *)NULL)
#define	MUTEX_STATS(lp)		((struct mutex_stats *)MUTEX_PTR(lp))
#define	MUTEX_ADAPTIVE_HELD	0xff	/* value of m_lock when held */
					/* only used by spin locks */

/*
 * Statistics package which replaces the owner field for some mutexes.
 */

struct mutex_stats {
	mutex_impl_t	m_real;			/* real mutex */
	lkinfo_t	lkinfo;			/* lock name and flags */
	lkstat_t	*lkstat;		/* statistics */
	char		name[LOCK_NAME_LEN];	/* name */
	uchar_t		flag;			/* flag */
};

/*
 * flags in mutex_stats.flag
 */
#define	MSTAT_STARTUP_ALLOC	1	/* allocated from startup pool */

#ifdef DEBUG
extern int mutex_critical_verifier;
#endif

#endif	/* _ASM */

#ifdef ATOMICS_WORK
/*
 * mutex_enter critical region parameters -- see lock_prim.s for details.
 */
#define	MUTEX_CRITICAL_UNION_START	(mutex_enter + 1*4 + ENTRY_SIZE)
#define	MUTEX_CRITICAL_REGION_SIZE	(5*4)
#define	MUTEX_CRITICAL_HOLE_SIZE	(1*4 + ENTRY_SIZE)
#define	MUTEX_CRITICAL_UNION_SIZE	\
	(2 * MUTEX_CRITICAL_REGION_SIZE + MUTEX_CRITICAL_HOLE_SIZE)
#else

#define	MUTEX_CRITICAL_UNION_START	(mutex_enter + 1*4 + ENTRY_SIZE)
#define	MUTEX_CRITICAL_REGION_SIZE	(6*4)
#define	MUTEX_CRITICAL_HOLE_SIZE	(1*4 + ENTRY_SIZE)
#define	MUTEX_CRITICAL_UNION_SIZE	\
	(2 * MUTEX_CRITICAL_REGION_SIZE + MUTEX_CRITICAL_HOLE_SIZE)
#endif


#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_MUTEX_IMPL_H */
