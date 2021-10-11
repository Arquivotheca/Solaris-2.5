/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	Copyright (c) 1994 Sun Microsystems, Inc. */
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)sigaltstack.c	1.3	94/10/13 SMI"	/* from SVr4.0 1.78 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/proc.h>
#include <sys/fault.h>
#include <sys/signal.h>
#include <sys/siginfo.h>
#include <sys/debug.h>

int
sigaltstack(struct sigaltstack *ssp, struct sigaltstack *oss)
{
	register klwp_t *lwp = ttolwp(curthread);
	struct sigaltstack ss;

	/*
	 * User's oss and ss might be the same address, so copyin first and
	 * save before copying out.
	 */
	if (ssp) {
		if (lwp->lwp_sigaltstack.ss_flags & SS_ONSTACK)
			return (set_errno(EPERM));
		if (copyin((caddr_t)ssp, (caddr_t)&ss, sizeof (ss)))
			return (set_errno(EFAULT));
		if (ss.ss_flags & ~(SS_ONSTACK|SS_DISABLE))
			return (set_errno(EINVAL));
		if (!(ss.ss_flags & SS_DISABLE) && ss.ss_size < MINSIGSTKSZ)
			return (set_errno(ENOMEM));
	}

	if (oss) {
		if (copyout((caddr_t)&lwp->lwp_sigaltstack,
		    (caddr_t)oss, sizeof (struct sigaltstack)))
			return (set_errno(EFAULT));
	}

	if (ssp)
		lwp->lwp_sigaltstack = ss;

	return (0);
}
