/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)uadmin.c	1.2	95/03/11 SMI"	/* from SVr4.0 1.78 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/proc.h>
#include <sys/var.h>
#include <sys/uadmin.h>
#include <sys/signal.h>
#include <sys/time.h>
#include <vm/seg_kmem.h>
#include <sys/modctl.h>
#include <sys/debug.h>

/*
 * Administrivia system call.
 */

struct uadmina {
	int	cmd;
	int	fcn;
	int	mdep;
};

#define	BOOTSTRLEN	256

/* ARGSUSED */
int
uadmin(uap, rvp)
	register struct uadmina *uap;
	rval_t *rvp;
{
	int error = 0;
	int locked = 0;
	char *bootstr = NULL;
	char bootstrbuf[BOOTSTRLEN + 1];
	size_t len;
	extern kmutex_t ualock;
	extern int swapctl();

	/*
	 * Check cmd arg (fcn is system dependent & defaulted in mdboot())
	 * if wrong.
	 */
	switch (uap->cmd) {
	case A_SWAPCTL:		/* swapctl checks permissions itself */
		return (swapctl(uap, rvp));

	case A_SHUTDOWN:
	case A_REBOOT:
	case A_REMOUNT:
	case A_FREEZE:
		if (!suser(CRED()))
			return (EPERM);
		break;

	default:
		return (EINVAL);
	}

	switch (uap->cmd) {
	case A_SHUTDOWN:
	case A_REBOOT:
		/*
		 * Copy in the boot string now.
		 * We will release our address space so we can't do it later.
		 */
		len = 0;
		if ((bootstr = (char *)uap->mdep) != NULL &&
		    copyinstr(bootstr, bootstrbuf, BOOTSTRLEN, &len) == 0) {
			bootstrbuf[len] = 0;
			bootstr = bootstrbuf;
		} else {
			bootstr = NULL;
		}
		/* FALLTHROUGH */
	case A_REMOUNT:
		if (!mutex_tryenter(&ualock))
			return (0);
		locked = 1;
	}

	switch (uap->cmd) {

	case A_SHUTDOWN:
	{
		register struct proc *p;
		struct vnode *exec_vp;

		/*
		 * Release (almost) all of our own resources.
		 */
		p = ttoproc(curthread);
		exitlwps(p, 0);
		mutex_enter(&p->p_lock);
		p->p_flag |= SNOWAIT;
		sigfillset(&p->p_ignore);
		curthread->t_lwp->lwp_cursig = 0;
		if (p->p_exec) {
			exec_vp = p->p_exec;
			p->p_exec = NULLVP;
			mutex_exit(&p->p_lock);
			VN_RELE(exec_vp);
		} else {
			mutex_exit(&p->p_lock);
		}
		closeall(1);
		relvm(p);

		/*
		 * Kill all processes except kernel daemons and ourself.
		 * Make a first pass to stop all processes so they won't
		 * be trying to restart children as we kill them.
		 */
		mutex_enter(&pidlock);
		for (p = practive; p != NULL; p = p->p_next) {
			if (p->p_exec != NULLVP &&	/* kernel daemons */
			    p->p_as != &kas &&
			    p->p_stat != SZOMB) {
				mutex_enter(&p->p_lock);
				p->p_flag |= SNOWAIT;
				sigtoproc(p, NULL, SIGSTOP, 0);
				mutex_exit(&p->p_lock);
			}
		}
		p = practive;
		while (p != NULL) {
			if (p->p_exec != NULLVP &&	/* kernel daemons */
			    p->p_as != &kas &&
			    p->p_stat != SIDL &&
			    p->p_stat != SZOMB) {
				mutex_enter(&p->p_lock);
				if (sigismember(&p->p_sig, SIGKILL)) {
					mutex_exit(&p->p_lock);
					p = p->p_next;
				} else {
					sigtoproc(p, NULL, SIGKILL, 0);
					mutex_exit(&p->p_lock);
					(void) cv_timedwait(&p->p_srwchan_cv,
					    &pidlock, lbolt + HZ);
					p = practive;
				}
			} else {
				p = p->p_next;
			}
		}
		mutex_exit(&pidlock);

		vfs_unmountall();

		vfs_syncall();

		(void) VFS_MOUNTROOT(rootvfs, ROOT_UNMOUNT);
		/* FALLTHROUGH */
	}

	case A_REBOOT:
	{
		mdboot(uap->cmd, uap->fcn, bootstr);
		/* no return expected */
		break;
	}

	case A_REMOUNT:
		/* remount root file system */
		(void) VFS_MOUNTROOT(rootvfs, ROOT_REMOUNT);
		break;

	case A_FREEZE:
	{
		/* XXX: declare in some header file */
		extern int cpr(int);

		if (modload("misc", "cpr") == -1)
			return (EINVAL);
		else
			return (cpr(uap->fcn));
	}

	default:
		error = EINVAL;
	}

	if (locked)
		mutex_exit(&ualock);
	return (error);
}
