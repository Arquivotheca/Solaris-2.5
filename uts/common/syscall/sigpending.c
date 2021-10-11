/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	Copyright (c) 1994 Sun Microsystems, Inc. */
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)sigpending.c	1.4	95/03/15 SMI"	/* from SVr4.0 1.78 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/proc.h>
#include <sys/fault.h>
#include <sys/signal.h>
#include <sys/debug.h>

int
sigpending(int flag, sigset_t *setp)
{
	sigset_t set;
	k_sigset_t kset;
	proc_t *p;

	switch (flag) {
	case 1: /* sigpending */
		p = ttoproc(curthread);
		mutex_enter(&p->p_lock);
		if (p->p_aslwptp != NULL) {
			kset = p->p_notifsigs;
			sigorset(&kset, &p->p_aslwptp->t_sig);
		} else
			kset = p->p_sig;
		sigandset(&kset, &curthread->t_hold);
		mutex_exit(&p->p_lock);
		break;
	case 2: /* sigfillset */
		kset = fillset;
		break;
	/*
	 * XXX:
	 * case 3: Add case for MT processes. Here you would have to
	 * ignore curthread->t_hold and have a user-level wrapper do the masking
	 * with the calling (user) thread's signal mask.
	 *
	 *      p = ttoproc(curthread);
                mutex_enter(&p->p_lock);
                if (p->p_aslwptp != NULL) {
                        kset = p->p_notifsigs;
                        sigorset(&kset, &p->p_aslwptp->t_sig);
                } else
                        kset = p->p_sig;
                mutex_exit(&p->p_lock);
                break;
	 *
	 */
	default:
		return (set_errno(EINVAL));
	}

	sigktou(&kset, &set);
	if (copyout((caddr_t)&set, (caddr_t)setp, sizeof (sigset_t)))
		return (set_errno(EFAULT));
	return (0);
}
