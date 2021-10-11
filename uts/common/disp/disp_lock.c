/*	Copyright (c) 1992 Sun Microsystems, Inc. */
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF SUN	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma ident	"@(#)disp_lock.c	1.10	94/09/29 SMI"

#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/inline.h>
#include <sys/disp.h>
#include <sys/kmem.h>
#include <sys/cpuvar.h>
#include <sys/vtrace.h>
#include <sys/dki_lock.h>
#include <sys/spl.h>

#define	LOCK_SPL	(ipltospl(LOCK_LEVEL))

#ifndef	DISP_DEBUG
#define	disp_lock_trace(lp)		/* dummy version of disp_lock_trace() */
#endif /* DISP_DEBUG */

/*
 * It is intended that statistics on these dispatcher locks eventually be
 * implemented under the #ifdef DISP_LOCK_STATS.
 */

/*
 * non-statistics-gathering versions.
 */
#ifndef DISP_LOCK_STATS

/*
 * Initialize a dispatcher lock.
 */
/* ARGSUSED */
void
disp_lock_init(disp_lock_t *lp, char *name)
{
	LOCK_INIT_CLEAR((lock_t *)lp);
}

/* ARGSUSED */
void
disp_lock_destroy(disp_lock_t *lp)
{
}
#endif	/* DISP_LOCK_STATS */

/*
 * Thread_lock() - get the correct dispatcher lock for the thread.
 */
void
thread_lock(kthread_id_t t)
{
	lock_t		*lp;
	lock_t		*rlp;
	int		s;

	lp = (lock_t *) t->t_lockp;	/* get tentative lock pointer */
	s = splhigh();
	for (;;) {
		while (!lock_try(lp)) {
			(void) splx(s);	/* lower spl */
			/*
			 * Spin on lock with non-atomic load to avoid cache
			 * activity.
			 *
			 * If the thread changes state, its lock pointer
			 * will change, too, so reload it during the spin.
			 */
			while (LOCK_HELD(lp)) {
				if (panicstr) {
					panic_hook();
					curthread->t_oldspl = splhigh();
					return;
				}
				lp = t->t_lockp;
			}
			s = splhigh();	/* raise spl again */
		}
		disp_lock_trace(lp);
		/*
		 * Here we have a lock, but is it still the right one?
		 */
		rlp = t->t_lockp;
		if (lp == rlp) {
			break;	/* correct lock, break out of the loop */
		}
		disp_lock_exit_high(lp);	/* so we get a trace */
		lp = rlp;
	}
	curthread->t_oldspl = s;	/* save spl in thread */
}

/*
 * Thread_lock_high() - get the correct dispatcher lock for the thread.
 *	This version is called when already at high spl.
 */
void
thread_lock_high(kthread_id_t t)
{
	lock_t		*lp;
	lock_t		*rlp;

	lp = (lock_t *) t->t_lockp;	/* get tentative lock pointer */
	for (;;) {
		while (!lock_try(lp)) {
			/*
			 * Spin on lock with non-atomic load to avoid cache
			 * activity.
			 *
			 * If the thread changes state, its lock pointer
			 * will change, too, so reload it during the spin.
			 */
			while (LOCK_HELD(lp)) {
				if (panicstr) {
					panic_hook();
					curthread->t_oldspl = splhigh();
					return;
				}
				lp = t->t_lockp;
			}
		}
		disp_lock_trace(lp);
		/*
		 * Here we have a lock, but is it still the right one?
		 */
		rlp = t->t_lockp;
		if (lp == rlp) {
			break;	/* correct lock, break out of the loop */
		}
		disp_lock_exit_high(lp);	/* so we get a trace */
		lp = rlp;
	}
}

/*
 * Called by THREAD_TRANSITION macro to change the thread state to
 * the intermediate state-in-transititon state.
 */
void
thread_transition(kthread_id_t t)
{
	disp_lock_t	*lp;

	ASSERT(THREAD_LOCK_HELD(t));
	ASSERT(t->t_lockp != &transition_lock);

	lp = t->t_lockp;
	t->t_lockp = &transition_lock;
	disp_lock_exit_high(lp);
}
