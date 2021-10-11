/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma	ident	"@(#)putnext.c	1.15	94/07/20	SMI"

/*
 *		UNIX Device Driver Interface functions
 *	This file contains the C-versions of putnext() and put().
 *	Assembly language versions exist for some architectures.
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/debug.h>
#include <sys/t_lock.h>
#include <sys/cmn_err.h>
#include <sys/stream.h>
#include <sys/thread.h>
#include <sys/strsubr.h>
#include <sys/ddi.h>
#include <sys/vtrace.h>

/*
 * function: putnext()
 * purpose:  call the put routine of the queue linked to qp
 *
 * Note: this function is written to perform well on modern computer
 * architectures by e.g. preloading values into registers and "smearing" out
 * code.
 *
 * The DDI defines putnext() as a function returning int
 * but it is used everywhere as a function returning void.
 * The DDI should be changed, but we gratuitously return 0 for now.
 */
int
putnext(qp, mp)
	queue_t	*qp;
	mblk_t	*mp;
{
	syncq_t		*sq;
	u_long		count;
	u_long		flags;
	struct qinit	*qi;
	int		(*putproc)();
	struct stdata	*stp;

	TRACE_2(TR_FAC_STREAMS_FR, TR_PUTNEXT_START,
		"putnext_start:(%X, %X)", qp, mp);

	ASSERT(mp->b_datap->db_ref != 0);
	ASSERT(mp->b_next == NULL && mp->b_prev == NULL);
	stp = STREAM(qp);
	ASSERT(stp != NULL);
	flags = qp->q_flag;
	if (flags & QUNSAFE) {
		/* Coming from an unsafe module */
		putnext_from_unsafe(qp, mp);
		return (0);
	}
	ASSERT(UNSAFE_DRIVER_LOCK_NOT_HELD());
	/*
	 * Prevent q_next from changing by holding sd_lock until
	 * acquiring SQLOCK.
	 */
	mutex_enter(&stp->sd_lock);
	qp = qp->q_next;
	sq = qp->q_syncq;
	ASSERT(sq != NULL);
	qi = qp->q_qinfo;
	mutex_enter(SQLOCK(sq));
	mutex_exit(&stp->sd_lock);
	count = sq->sq_count;
	flags = sq->sq_flags;
	if (flags & (SQ_GOAWAY|SQ_CIPUT|SQ_UNSAFE)) {
		if (flags & SQ_GOAWAY) {
			fill_syncq(sq, qp, mp, NULL);
			mutex_exit(SQLOCK(sq));
			TRACE_3(TR_FAC_STREAMS_FR, TR_PUTNEXT_END,
				"putnext_end:(%X, %X, %X) done", qp, mp, sq);
			return (0);
		}
		if (flags & SQ_UNSAFE) {
			if (putnext_to_unsafe(qp, mp, flags, count, sq))
				return (0);
			putproc = qi->qi_putp;
			goto do_put;
		}
		/* Must be hot */
		ASSERT(flags & SQ_CIPUT);
	} else
		sq->sq_flags = flags | SQ_EXCL;
	sq->sq_count = count + 1;
	ASSERT(sq->sq_count != 0);		/* Wraparound */
	putproc = qi->qi_putp;
	mutex_exit(SQLOCK(sq));

do_put:
	(*putproc)(qp, mp);

	mutex_enter(SQLOCK(sq));
	flags = sq->sq_flags;
	count = sq->sq_count;
	if (flags & (SQ_QUEUED|SQ_UNSAFE|SQ_WANTWAKEUP|SQ_WANTEXWAKEUP)) {
		putnext_tail(sq, mp, flags, count);
		return (0);
	}
	ASSERT(count != 0);
	sq->sq_count = count - 1;
	ASSERT(flags & (SQ_EXCL|SQ_CIPUT));
	/*
	 * Safe to always drop SQ_EXCL:
	 *	Not SQ_CIPUT means we set SQ_EXCL above
	 *	For SQ_CIPUT SQ_EXCL will only be set if the put procedure
	 *	did a qwriter(INNER) in which case nobody else
	 *	is in the inner perimeter and we are exiting.
	 */
#ifdef DEBUG
	if ((flags & (SQ_EXCL|SQ_CIPUT)) == (SQ_EXCL|SQ_CIPUT)) {
		ASSERT(sq->sq_count == 0);
	}
#endif DEBUG
	sq->sq_flags = flags & ~SQ_EXCL;
	mutex_exit(SQLOCK(sq));
	TRACE_3(TR_FAC_STREAMS_FR, TR_PUTNEXT_END,
		"putnext_end:(%X, %X, %X) done", qp, mp, sq);
	return (0);
}


/*
 * wrapper for qi_putp entry in module ops vec.
 * implements asynchronous putnext().
 * This routine is not supported for unsafe drivers.
 */
void
put(qp, mp)
	queue_t	*qp;
	mblk_t	*mp;
{
	syncq_t	*sq;
	u_long		count;
	u_long		flags;
	struct qinit	*qi;
	int		(*putproc)();

	TRACE_2(TR_FAC_STREAMS_FR, TR_PUT_START,
		"put:(%X, %X)", qp, mp);
	ASSERT(mp->b_datap->db_ref != 0);
	ASSERT(mp->b_next == NULL && mp->b_prev == NULL);
	ASSERT(UNSAFE_DRIVER_LOCK_NOT_HELD());

	sq = qp->q_syncq;
	qi = qp->q_qinfo;
	ASSERT(sq != NULL);
	mutex_enter(SQLOCK(sq));
	count = sq->sq_count;
	flags = sq->sq_flags;
	if (flags & (SQ_GOAWAY|SQ_CIPUT|SQ_UNSAFE)) {
		if (flags & SQ_GOAWAY) {
			fill_syncq(sq, qp, mp, NULL);
			mutex_exit(SQLOCK(sq));
			TRACE_3(TR_FAC_STREAMS_FR, TR_PUTNEXT_END,
				"putnext_end:(%X, %X, %X) done", qp, mp, sq);
			return;
		}
		if (flags & SQ_UNSAFE) {
			if (putnext_to_unsafe(qp, mp, flags, count, sq))
				return;
			putproc = qi->qi_putp;
			goto do_put;
		}
		/* Must be hot */
		ASSERT(flags & SQ_CIPUT);
	} else
		sq->sq_flags = flags | SQ_EXCL;
	sq->sq_count = count + 1;
	ASSERT(sq->sq_count != 0);		/* Wraparound */
	putproc = qi->qi_putp;
	mutex_exit(SQLOCK(sq));

do_put:
	(*putproc)(qp, mp);

	mutex_enter(SQLOCK(sq));
	flags = sq->sq_flags;
	count = sq->sq_count;
	if (flags & (SQ_QUEUED|SQ_UNSAFE|SQ_WANTWAKEUP|SQ_WANTEXWAKEUP)) {
		putnext_tail(sq, mp, flags, count);
		return;
	}
	ASSERT(count != 0);
	sq->sq_count = count - 1;
	ASSERT(flags & (SQ_EXCL|SQ_CIPUT));
	/*
	 * Safe to always drop SQ_EXCL:
	 *	Not SQ_CIPUT means we set SQ_EXCL above
	 *	For SQ_CIPUT SQ_EXCL will only be set if the put procedure
	 *	did a qwriter(INNER) in which case nobody else
	 *	is in the inner perimeter and we are exiting.
	 */
#ifdef DEBUG
	if ((flags & (SQ_EXCL|SQ_CIPUT)) == (SQ_EXCL|SQ_CIPUT)) {
		ASSERT(sq->sq_count == 0);
	}
#endif DEBUG
	sq->sq_flags = flags & ~SQ_EXCL;
	mutex_exit(SQLOCK(sq));
	TRACE_3(TR_FAC_STREAMS_FR, TR_PUTNEXT_END,
		"putnext_end:(%X, %X, %X) done", qp, mp, sq);
}
