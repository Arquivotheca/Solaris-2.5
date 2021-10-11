/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)adjtime.c	1.2	94/09/13 SMI"	/* from SVr4.0 1.78 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/tuneable.h>
#include <sys/errno.h>
#include <sys/proc.h>
#include <sys/time.h>
#include <sys/debug.h>


extern int	hr_clock_lock();
extern void	hr_clock_unlock();

int
adjtime(struct timeval *delta, struct timeval *olddelta)
{
	struct timeval atv, oatv;
	register longlong_t ndelta;
	register longlong_t old_delta;
	int s;
#ifdef	bug1152245
	longlong_t ll_temp;
#endif
	if (!suser(CRED()))
		return (set_errno(EPERM));

	if (copyin((caddr_t)delta, (caddr_t)&atv, sizeof (struct timeval)))
		return (set_errno(EFAULT));

	/*
	 * If the fractional part of the time value is invalid, e.g.
	 * absolute value more than a million microseconds
	 * The SVVS test didn't initialize this field so it was garbage.
	 * There is no documented error return for this.
	 * XXX  If SVVS is broken, fix it.
	 */
	if (atv.tv_usec <= -MICROSEC || atv.tv_usec >= MICROSEC)
		return (set_errno(EINVAL));

	/*
	 * The SVID specifies that if delta is 0, then there is
	 * no effect upon time correction, just return olddelta.
	 */
	ndelta = (longlong_t)atv.tv_sec * NANOSEC + atv.tv_usec * 1000;
	mutex_enter(&tod_lock);
	s = hr_clock_lock();
	old_delta = timedelta;
	if (ndelta) {
		timedelta = ndelta;
		tod_needsync = 1;
	}
	hr_clock_unlock(s);
	mutex_exit(&tod_lock);

	if (olddelta) {
#ifndef	bug1152245
		oatv.tv_sec = old_delta / NANOSEC;
#else
		ll_temp = old_delta/ NANOSEC;
		oatv.tv_sec = ll_temp;
#endif
		oatv.tv_usec = (old_delta % NANOSEC) / 1000;
		if (copyout((caddr_t)&oatv, (caddr_t)olddelta,
		    sizeof (struct timeval)))
			return (set_errno(EFAULT));
	}
	return (0);
}
