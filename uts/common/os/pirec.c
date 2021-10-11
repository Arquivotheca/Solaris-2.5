/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)pirec.c	1.11	94/09/29 SMI"

#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/thread.h>
#include <sys/reboot.h>
#include <sys/debug.h>
#include <sys/debug/debug.h>
#include <sys/cpuvar.h>
#include <sys/kmem.h>
#include <sys/pirec.h>

/*
 * Initialize the pirec using the function
 * arguments.
 */
void
pirec_init(pirec_t *p, kthread_t *t, pri_t epri)
{
	p->pi_epri_hi = (int)epri;
	p->pi_benef = t;
}	/* end of pirec_alloc */

/*
 * Zero out a pirec in-line.
 */
void
pirec_clear(pirec_t *p)
{
	p->pi_forw = NULL;
	p->pi_back = NULL;
	p->pi_benef = NULL;
	p->pi_epri_hi = 0;
}	/* end of pirec_clear */

/*
 * Insert a pirec in a circular linked list
 * of pirec's.
 */
void
pirec_insque(pirec_t *p, pirec_t *q)
{
	ASSERT(p != q);
	p->pi_forw = q;
	p->pi_back = q->pi_back;
	q->pi_back->pi_forw = p;
	q->pi_back = p;
}	/* end of pirec_insque */

/*
 * Remove a pirec from a circular linked list
 * of pirec's.
 */
void
pirec_remque(pirec_t *p)
{
	p->pi_back->pi_forw = p->pi_forw;
	p->pi_forw->pi_back = p->pi_back;
}	/* end of pirec_remque */

/*
 * Threads keep a list of pirecs corresponding to each
 * priority-inverted synchronization object that they
 * hold.
 *
 * Traverse this list of pirecs to compute the effective
 * dispatch priority of the thread.
 *
 * This operation is generally performed when a thread
 * releases a priority-inverted lock.
 */
pri_t
pirec_calcpri(pirec_t *pirec, pri_t disp_pri)
{
	pirec_t	*p;

	if ((p = pirec) != NULL) {
		u_int	dpri;

		dpri = (u_int)disp_pri;
		do {
			dpri = MAX(dpri, p->pi_epri_hi);
		} while ((p = p->pi_forw) != pirec);
		disp_pri = (pri_t)dpri;
	}
	return (disp_pri);
}	/* end of pirec_calcpri */

#ifdef PIREC_VERIFY
/*
 * Verify the integrity of a circular list of
 * pirecs.
 */
void
pirec_verify(pirec_t *pirec, kthread_t *t)
{
	pirec_t	*p;

	if ((p = pirec) != NULL) {
		do {
			ASSERT((p->pi_forw != NULL) &&
				(p->pi_back != NULL) &&
				(p->pi_epri_hi != 0) &&
				(p->pi_owner == t));
		} while ((p = p->p->pi_forw) != pirec);
	}
}	/* end of pirec_calcpri */
#endif /* PIREC_VERIFY */
