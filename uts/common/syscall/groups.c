/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)groups.c	1.3	95/10/04 SMI"	/* from SVr4.0 1.78 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/cred.h>
#include <sys/errno.h>
#include <sys/proc.h>
#include <sys/debug.h>
#include <sys/kmem.h>


/* ARGSUSED */
int
setgroups(u_int gidsetsize, gid_t *gidset)
{
	register proc_t *p;
	register cred_t *cr, *newcr;
	register u_short i;
	register u_short n = gidsetsize;
	gid_t	*groups = NULL;

	if (n != 0 && n <= (u_short)ngroups_max) {
		groups = (gid_t *)kmem_alloc(n * sizeof (gid_t), KM_SLEEP);
		if (copyin((caddr_t)gidset, (caddr_t)groups,
		    n * sizeof (gid_t)) != 0) {
			kmem_free((void *)groups, n * sizeof (gid_t));
			return (set_errno(EFAULT));
		}
	}
	p = ttoproc(curthread);
	mutex_enter(&p->p_crlock);
	cr = p->p_cred;
	if (!suser(cr)) {
		mutex_exit(&p->p_crlock);
		if (groups != NULL)
			kmem_free((void *)groups, n * sizeof (gid_t));
		return (set_errno(EPERM));
	}

	if (n > (u_short)ngroups_max) {
		if (groups != NULL)
			kmem_free((void *)groups, n * sizeof (gid_t));
		mutex_exit(&p->p_crlock);
		return (set_errno(EINVAL));
	}

	newcr = crdup(cr);

	if (n != 0) {
		bcopy((caddr_t) groups, (caddr_t)newcr->cr_groups,
		    n * sizeof (gid_t));
		kmem_free((void *)groups, n * sizeof (gid_t));
	}

	for (i = 0; i < n; i++) {
		if (newcr->cr_groups[i] < 0 || newcr->cr_groups[i] >= MAXUID) {
			mutex_exit(&p->p_crlock);
			crfree(newcr);
			return (set_errno(EINVAL));
		}
	}
	newcr->cr_ngroups = n;

	p->p_cred = newcr;
	crhold(newcr);			/* hold for the current thread */
	crfree(cr);			/* free the old one */
	mutex_exit(&p->p_crlock);

	/*
	 * Broadcast new cred to process threads (including the current one).
	 */
	crset(p, newcr);

	return (0);
}

int
getgroups(int gidsetsize, gid_t *gidset)
{
	register struct cred *cr;
	register gid_t n;

	cr = curthread->t_cred;
	n = cr->cr_ngroups;
	if (gidsetsize != 0) {
		if ((gid_t)gidsetsize < n)
			return (set_errno(EINVAL));
		if (copyout((caddr_t)cr->cr_groups, (caddr_t)gidset,
		    n * sizeof (gid_t)))
			return (set_errno(EFAULT));
	}

	return (n);
}
