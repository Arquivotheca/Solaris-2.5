/*
 *	Copyright (c) 1991-1993, Sun Microsystems, Inc.
 */

/*
 * t_lock.h:	Prototypes for disp_locks, plus include files
 *		that describe the interfaces to kernel synch.
 *		objects.
 */

#ifndef _SYS_T_LOCK_H
#define	_SYS_T_LOCK_H

#pragma ident	"@(#)t_lock.h	1.42	94/11/02 SMI"

#ifndef	_ASM
#include <sys/machlock.h>
#include <sys/dki_lkinfo.h>
#include <sys/sleepq.h>
#include <sys/turnstile.h>
#include <sys/mutex.h>
#include <sys/rwlock.h>
#include <sys/semaphore.h>
#include <sys/condvar.h>
#endif	/* _ASM */

#ifdef	__cplusplus
extern "C" {
#endif

#ifndef	_ASM

/*
 * Mutual exclusion locks described in common/sys/mutex.h.
 *
 * Semaphores described in common/sys/semaphore.h.
 *
 * Readers/Writer locks described in common/sys/rwlock.h.
 *
 * Condition variables described in common/sys/condvar.h
 */

/*
 * The value LOCK_NAME_LEN is the maximum length of a name stored
 * in lock stats. It is used by the mutex and rwlock code.
 */
#define	LOCK_NAME_LEN	18


#if defined(_KERNEL)

extern int ncpus;

extern	kmutex_t unsafe_driver;	/* protects MT-unsafe device drivers */

#define	UNSAFE_DRIVER_LOCK_HELD()	(mutex_owned(&unsafe_driver))
#define	UNSAFE_DRIVER_LOCK_NOT_HELD()	\
	(!mutex_owned(&unsafe_driver) || panicstr)

/*
 * Dispatcher lock type, macros and routines.
 *
 * disp_lock_t is defined in machlock.h
 */
extern	void	disp_lock_enter(disp_lock_t *);
extern	void	disp_lock_exit(disp_lock_t *);
extern	void	disp_lock_exit_nopreempt(disp_lock_t *);
extern	void	disp_lock_enter_high(disp_lock_t *);
extern	void	disp_lock_exit_high(disp_lock_t *);
extern	void	disp_lock_init(disp_lock_t *lp, char *name);
extern	void	disp_lock_destroy(disp_lock_t *lp);
extern	void	disp_lock_trace(disp_lock_t *lp); /* internal for tracing */

#define	DISP_LOCK_HELD(lp)	LOCK_HELD((lock_t *)(lp))

/*
 * The following definitions are for assertions which can be checked
 * statically by tools like lock_lint.  You can also define your own
 * run-time test for each.  If you don't, we define them to 1 so that
 * such assertions simply pass.
 */
#ifndef NO_LOCKS_HELD
#define	NO_LOCKS_HELD	1
#endif
#ifndef NO_COMPETING_THREADS
#define	NO_COMPETING_THREADS	1
#endif

#endif	/* defined(_KERNEL) */

#endif	/* _ASM */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_T_LOCK_H */
