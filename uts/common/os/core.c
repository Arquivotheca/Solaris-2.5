/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)core.c	1.26	95/07/28 SMI"	/* SVr4.0 1.15	*/

#include <sys/param.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/siginfo.h>
#include <sys/cred.h>
#include <sys/user.h>
#include <sys/errno.h>
#include <sys/signal.h>
#include <sys/siginfo.h>
#include <sys/fault.h>
#include <sys/syscall.h>
#include <sys/ucontext.h>
#include <sys/procfs.h>
#include <sys/vnode.h>
#include <sys/var.h>
#include <sys/file.h>
#include <sys/exec.h>
#include <sys/debug.h>
#include <sys/stack.h>
#include <sys/kmem.h>
#include <sys/core.h>

#include <vm/as.h>


/*
 * Create a core image on the file "core".
 */
int
core(char *fp, proc_t *pp, struct cred *credp, rlim_t rlimit, int sig)
{
	struct vnode *vp;
	struct vattr vattr;
	register int error, closerr;
	register cred_t *crp = CRED();
	mode_t umask = PTOU(pp)->u_cmask;
	register struct execsw *eswp;
	klwp_t *lwp = ttolwp(curthread);
	mode_t perms;

	if (rlimit == 0)
		return (1);		/* don't confuse waitid() */

	if ((pp->p_flag & NOCD) || !hasprocperm(crp, credp))
		return (EPERM);

	/*
	 * The original intent of the core function interface was to
	 * be able to core dump any process, not just the context we
	 * are currently in. Unfortunately, we never got around to
	 * writing the code to deal with any other context but the current.
	 * This is not so bad as currently there is no user interface to
	 * get here with a non-current context. We will fix this later when
	 * an interface is provided to core dump a selected process.
	 */
	if (pp != curproc)  	/* only support current context for now */
		return (EINVAL);

	/*
	 * The presence of a current signal prevents file i/o
	 * from succeeding over a network.  We copy the current
	 * signal information to the side and cancel the current
	 * signal so that the core dump will succeed.
	 */
	ASSERT(lwp->lwp_cursig == sig);
	lwp->lwp_cursig = 0;
	if (lwp->lwp_curinfo == NULL)
		bzero((caddr_t)&lwp->lwp_siginfo, sizeof (k_siginfo_t));
	else {
		bcopy((caddr_t)&lwp->lwp_curinfo->sq_info,
		    (caddr_t)&lwp->lwp_siginfo, sizeof (k_siginfo_t));
		if (lwp->lwp_curinfo->sq_func != NULL)
			(lwp->lwp_curinfo->sq_func)(lwp->lwp_curinfo);
		else
			kmem_free((caddr_t)lwp->lwp_curinfo,
			    sizeof (*lwp->lwp_curinfo));
		lwp->lwp_curinfo = NULL;
	}

	/*
	 * set perms and va_mask to limit access to "core"
	 * based on attributes from the exec
	 */
	vattr.va_mask = AT_MODE | AT_GID;
	error = VOP_GETATTR(pp->p_exec, &vattr, 0, credp);
	if (error) {
		return (error);
	} else if (vattr.va_mode & VSUID) {
		perms = 0600;
		vattr.va_mask = AT_MODE | AT_UID;
		vattr.va_uid = crp->cr_uid;
	} else if (vattr.va_mode & VSGID) {
		perms = 0660;
		vattr.va_mask |= AT_UID;
		vattr.va_uid = crp->cr_uid;
	} else {
		perms = 0666;
		vattr.va_mask = 0;
	}

	perms &= ~umask;
	error = vn_open(fp, UIO_SYSSPACE, FWRITE | FTRUNC | FCREAT,
	    perms, &vp, CRCORE);
	if (error) {
		return (error);
	}

	if (VOP_ACCESS(vp, VWRITE, 0, credp) || vp->v_type != VREG)
		error = EACCES;
	else {
#ifdef sparc
		/*
		 * Flush user register windows to stack.
		 */
		flush_user_windows();
#endif /* sparc */
		vattr.va_size = 0;
		vattr.va_mask |= AT_SIZE;
		vattr.va_mode = perms;
		(void) VOP_SETATTR(vp, &vattr, 0, credp);
#ifdef SUN_SRC_COMPAT
		u.u_acflag |= ACORE;
#endif
		if ((eswp = findexectype((short)PTOU(pp)->u_execid)) != NULL) {
			error = (eswp->exec_core)(vp, pp, credp, rlimit, sig);
			rw_exit(eswp->exec_lock);
		} else
			error = ENOSYS;
	}

	closerr = VOP_CLOSE(vp, FWRITE, 1, (offset_t) 0, credp);
	VN_RELE(vp);

	return (error ? error : closerr);
}

/*
 * Common code to core dump process memory.
 */
int
core_seg(proc_t *pp, vnode_t *vp, off_t offset, caddr_t addr, size_t size,
		rlim_t rlimit, struct cred *credp)
{
	register caddr_t eaddr;
	caddr_t base;
	u_int len;
	register int err = 0;

	eaddr = (caddr_t)(addr + size);
	/* LINTED lint confusion: len used before set */
	for (base = addr; base < eaddr; base += len) {
		len = eaddr - base;
		if (as_memory(pp->p_as, &base, &len) != 0)
			return (0);
		err = vn_rdwr(UIO_WRITE, vp, base, (int)len,
		    offset + (base - (caddr_t)addr), UIO_USERSPACE, 0,
		    rlimit, credp, (int *)NULL);
		if (err)
			return (err);
	}
	return (0);
}

/* ARGSUSED */
int
nocore(vnode_t *vp, proc_t *pp, struct cred *credp, rlim_t rlimit, int sig)
{
	return (ENOSYS);
}
