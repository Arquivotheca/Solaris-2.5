/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)rlimit.c	1.5	95/07/22 SMI"	/* from SVr4.0 1.78 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/tuneable.h>
#include <sys/user.h>
#include <sys/errno.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/proc.h>
#include <sys/resource.h>
#include <sys/ulimit.h>
#include <sys/debug.h>
#include <vm/as.h>


/*
 * ulimit could be moved into a user library, as calls to getrlimit and
 * setrlimit, were it not for binary compatibility restrictions
 */

off_t
ulimit(int cmd, long arg)
{
	proc_t *p = ttoproc(curthread);
	off_t	retval;

	switch (cmd) {

	case UL_GFILLIM: /* Return current file size limit. */
		retval = (u.u_rlimit[RLIMIT_FSIZE].rlim_cur >> SCTRSHFT);
		break;

	case UL_SFILLIM: /* Set new file size limit. */
	{
		register int error = 0;
		register rlim_t lim;

		lim = arg << SCTRSHFT;
		if (error = rlimit(RLIMIT_FSIZE, lim, lim)) {
			return (set_errno(error));
		}
		retval = arg;
		break;
	}

	case UL_GMEMLIM: /* Return maximum possible break value. */
	{
		register struct seg *seg;
		register struct seg *nextseg;
		register struct as *as = p->p_as;
		register caddr_t brkend;
		register caddr_t brkbase;
		uint size;

		brkend = (caddr_t)((int)
			(p->p_brkbase + p->p_brksize + PAGEOFFSET) & PAGEMASK);

		/*
		 * Find the segment with a virtual address
		 * greater than the end of the current break.
		 */
		nextseg = NULL;
		mutex_enter(&p->p_lock);
		brkbase = (caddr_t)p->p_brkbase;
		brkend = (caddr_t)p->p_brkbase + p->p_brksize;
		mutex_exit(&p->p_lock);

		AS_LOCK_ENTER(as, &as->a_lock, RW_READER);
		for (seg = as_findseg(as, brkend, 0); seg != NULL;
		    seg = AS_SEGP(as, seg->s_next)) {
			if (seg->s_base >= brkend) {
				nextseg = seg;
				break;
			}
		}

		/*
		 * We reduce the max break value (base+rlimit[DATA])
		 * if we run into another segment, the ublock or
		 * the end of memory.  We also are limited by
		 * rlimit[VMEM].
		 */
		retval = (off_t)
			(brkbase + u.u_rlimit[RLIMIT_DATA].rlim_cur);
		if (nextseg != NULL)
			retval = umin(retval, (off_t)nextseg->s_base);
		AS_LOCK_EXIT(as, &as->a_lock);
		/*
		 * Also handle case where rlimit[VMEM] has been
		 * lowered below the current address space size.
		 */
		size = u.u_rlimit[RLIMIT_VMEM].rlim_cur & PAGEMASK;
		if (as->a_size < size)
			size -= as->a_size;
		else
			size = 0;
		retval = umin(retval, (off_t)(brkend + size));
		/* truncate to 8-byte boundary to match sbrk */
		retval = retval & ~(8-1);
		break;
	}

	case UL_GDESLIM: /* Return approximate number of open files */
		retval = u.u_rlimit[RLIMIT_NOFILE].rlim_cur;
		break;

	default:
		return (set_errno(EINVAL));

	}
	return (retval);
}

int
getrlimit(int resource, struct rlimit *rlp)
{
	struct rlimit rlimit;
	struct proc *p = ttoproc(curthread);

	if (resource < 0 || resource >= RLIM_NLIMITS) {
		return (set_errno(EINVAL));
	}
	mutex_enter(&p->p_lock);
	rlimit = u.u_rlimit[resource];
	mutex_exit(&p->p_lock);

	if (copyout((caddr_t)&rlimit, (caddr_t)rlp, sizeof (struct rlimit))) {
		return (set_errno(EFAULT));
	}
	return (0);
}

int
setrlimit(int resource, struct rlimit *rlp)
{
	struct rlimit rlim;
	int	error;

	if (resource < 0 || resource >= RLIM_NLIMITS)
		return (set_errno(EINVAL));
	if (copyin((caddr_t)rlp, (caddr_t)&rlim, sizeof (rlim)))
		return (set_errno(EFAULT));
	if (error = rlimit(resource, rlim.rlim_cur, rlim.rlim_max))
		return (set_errno(error));
	return (0);
}
