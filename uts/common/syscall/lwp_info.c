/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)lwp_info.c	1.2	94/09/13 SMI"	/* from SVr4.0 1.78 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/proc.h>
#include <sys/time.h>
#include <sys/debug.h>

/*
 * Get the time accounting information for the calling LWP.
 */
int
lwp_info(timestruc_t *tvp)
{
	timestruc_t tv[2];
	klwp_t *lwp = ttolwp(curthread);

	tv[0].tv_sec = (lwp->lwp_utime) / HZ;
	tv[0].tv_nsec = ((lwp->lwp_utime) % HZ) * (NANOSEC / HZ);

	tv[1].tv_sec = (lwp->lwp_stime) / HZ;
	tv[1].tv_nsec = ((lwp->lwp_stime) % HZ) * (NANOSEC / HZ);

	if (copyout((caddr_t)&tv, (caddr_t)tvp, sizeof (tv)))
		return (set_errno(EFAULT));

	return (0);
}
