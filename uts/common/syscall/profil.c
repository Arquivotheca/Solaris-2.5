/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)profil.c	1.2	94/09/13 SMI"	/* from SVr4.0 1.78 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/proc.h>
#include <sys/debug.h>



/*
 * Profiling.
 */
int
profil(short *bufbase, unsigned bufsize, unsigned pcoffset, unsigned pcscale)
{
	klwp_id_t lwp = ttolwp(curthread);
	struct proc *p = ttoproc(curthread);

	/* Set all profiling variables atomicly */
	mutex_enter(&p->p_pflock);
	lwp->lwp_prof.pr_base = bufbase;
	lwp->lwp_prof.pr_size = bufsize;
	lwp->lwp_prof.pr_off = pcoffset;
	lwp->lwp_prof.pr_scale = pcscale;
	mutex_exit(&p->p_pflock);
	mutex_enter(&p->p_lock);
	set_proc_pre_sys(p);	/* activate pre_syscall stime profiling code */
	mutex_exit(&p->p_lock);
	return (0);
}
