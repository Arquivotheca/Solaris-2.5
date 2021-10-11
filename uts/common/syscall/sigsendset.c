/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	Copyright (c) 1994 Sun Microsystems, Inc. */
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)sigsendset.c	1.3	94/10/13 SMI"	/* from SVr4.0 1.78 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/proc.h>
#include <sys/fault.h>
#include <sys/procset.h>
#include <sys/signal.h>
#include <sys/siginfo.h>
#include <sys/debug.h>

int
sigsendsys(procset_t *psp, int sig)
{
	int error;
	procset_t set;
	sigsend_t v;


	if (sig < 0 || sig >= NSIG)
		return (set_errno(EINVAL));

	struct_zero((caddr_t)&v, sizeof (v));
	v.sig = sig;
	v.checkperm = 1;
	v.sicode = SI_USER;

	if (copyin((caddr_t)psp, (caddr_t)&set, sizeof (procset_t)))
		return (set_errno(EFAULT));
	if (error = sigsendset(&set, &v))
		return (set_errno(error));
	return (0);
}
