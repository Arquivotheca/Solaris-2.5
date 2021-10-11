/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma	ident	"@(#)t_kspoll.c	1.17	94/03/31 SMI"	/* SVr4.0 1.7  */

/*
 *  		PROPRIETARY NOTICE (Combined)
 *
 *  This source code is unpublished proprietary information
 *  constituting, or derived under license from AT&T's Unix(r) System V.
 *  In addition, portions of such source code were derived from Berkeley
 *  4.3 BSD under license from the Regents of the University of
 *  California.
 *
 *
 *
 *  		Copyright Notice
 *
 *  Notice of copyright on this source code product does not indicate
 *  publication.
 *
 *  	(c) 1986,1987,1988,1989  Sun Microsystems, Inc.
 *  	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *		All rights reserved.
 */

/*
 *	This function waits for timo clock ticks for something
 *	to arrive on the specified stream. If more than one client is
 *	hanging off of a single endpoint, and at least one has specified
 *	a non-zero timeout, then all will be woken.
 *
 *	Returns:
 *		0 on success or positive error code. On
 *		success, "events" is set to
 *		 0	on timeout or no events(timout = 0),
 *		 1	if desired event has occurred
 *
 *	Most of the code is from strwaitq().
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/proc.h>
#include <sys/file.h>
#include <sys/user.h>
#include <sys/vnode.h>
#include <sys/errno.h>
#include <sys/stream.h>
#include <sys/ioctl.h>
#include <sys/stropts.h>
#include <sys/strsubr.h>
#include <sys/tihdr.h>
#include <sys/timod.h>
#include <sys/tiuser.h>
#include <sys/t_kuser.h>
#include <sys/signal.h>
#include <sys/siginfo.h>
#include <sys/debug.h>

static void ktli_poll(TIUSER *);

int
t_kspoll(
	register TIUSER		*tiptr,
	register int		timo,
	register int		waitflg,
	register int		*events)
{
	register struct file	*fp;
	register struct vnode	*vp;
	register struct stdata	*stp;
	klwp_t			*lwp = ttolwp(curthread);
	register int		timeid;
	int			error;

	error = 0;
	fp = tiptr->fp;
	vp = fp->f_vnode;
	stp = vp->v_stream;

	if (events == NULL || waitflg != READWAIT)
		return (EINVAL);

again:
	mutex_enter(&stp->sd_lock);

	if (stp->sd_flag & (STRDERR|STPLEX)) {
		error = (stp->sd_flag & STPLEX) ? EINVAL : stp->sd_rerror;
		mutex_exit(&stp->sd_lock);
		return (error);
	}
	if (stp->sd_flag & STRHUP) {
		mutex_exit(&stp->sd_lock);
		return (EIO);
	}

	if ((RD(stp->sd_wrq))->q_first != NULL) {
		*events = 1;
		mutex_exit(&stp->sd_lock);
		return (0);
	}

	if (timo == 0) {
		*events = 0;
		mutex_exit(&stp->sd_lock);
		return (0);
	}

	/*
	 * set timer and sleep.
	 */
	if (timo > 0) {
		KTLILOG(2, "t_kspoll: timo %x\n", timo);
		timeid = timeout((void (*)(caddr_t))ktli_poll,
		    (caddr_t)tiptr, (long)timo);
	}

	/*
	 * Indicate that the lwp is not to be stopped while doing
	 * this network traffic.  This is to avoid deadlock while
	 * debugging a process via /proc.
	 */
	if (lwp != NULL)
		lwp->lwp_nostop++;
	stp->sd_flag |= RSLEEP;
	if (!cv_wait_sig(&RD(stp->sd_wrq)->q_wait, &stp->sd_lock)) {

		/*
		 * only unset RSLEEP if no other procs are
		 * sleeping on this stream
		 */
		if (fp->f_count <= 1) {		/* shouldn't ever be < 1 */
			stp->sd_flag &= ~RSLEEP;
		}
		if (lwp != NULL)
			lwp->lwp_nostop--;
		mutex_exit(&stp->sd_lock);
		if (timo > 0)
			(void) untimeout(timeid);
		return (EINTR);
	}
	if (lwp != NULL)
		lwp->lwp_nostop--;
	KTLILOG(2, "t_kspoll: pid %d, woken from cv_wait_sig\n",
	    ttoproc(curthread)->p_pid);
	if (fp->f_count <= 1) {
		stp->sd_flag &= ~RSLEEP;
	}
	mutex_exit(&stp->sd_lock);

	if (timo > 0)
		(void) untimeout(timeid);

	/*
	 * see if the timer expired
	 */
	if (tiptr->flags & TIME_UP) {
		tiptr->flags &= ~ TIME_UP;
		*events = 0;
		return (0);
	}

	goto again;
}

static void
ktli_poll(register TIUSER *tiptr)
{
	register struct vnode	*vp;
	register struct file	*fp;
	register struct stdata	*stp;

	fp = tiptr->fp;
	vp = fp->f_vnode;
	stp = vp->v_stream;

	tiptr->flags |= TIME_UP;

	mutex_enter(&stp->sd_lock);
	cv_broadcast(&RD(stp->sd_wrq)->q_wait);
	mutex_exit(&stp->sd_lock);
}
