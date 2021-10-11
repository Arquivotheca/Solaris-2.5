/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * +++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 * 		PROPRIETARY NOTICE (Combined)
 *
 * This source code is unpublished proprietary information
 * constituting, or derived under license from AT&T's UNIX(r) System V.
 * In addition, portions of such source code were derived from Berkeley
 * 4.3 BSD under license from the Regents of the University of
 * California.
 *
 *
 *
 *		Copyright Notice
 *
 * Notice of copyright on this source code product does not indicate
 * publication.
 *
 * 	(c) 1986, 1987, 1988, 1989  Sun Microsystems, Inc
 * 	(c) 1983, 1984, 1985, 1986, 1987, 1988, 1989  AT&T.
 *			All rights reserved.
 *
 */

#ident	"@(#)ioctl.c	1.1	94/08/10 SMI"	/* SVr4 1.103	*/

#include <sys/param.h>
#include <sys/isa_defs.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/cred.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/ttold.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/mode.h>
#include <sys/proc.h>
#include <sys/uio.h>
#include <sys/kmem.h>
#include <sys/filio.h>
#include <sys/debug.h>

/*
 * I/O control.
 */
struct ioctla {
	int fdes;
	int cmd;
	int arg;
};

int
ioctl(uap, rvp)
	register struct ioctla *uap;
	rval_t *rvp;
{
	file_t *fp;
	register int error = 0;
	register vnode_t *vp;
	struct vattr vattr;
	offset_t offset;
	int flag;

	if ((fp = GETF(uap->fdes)) == NULL)
		return (EBADF);
	vp = fp->f_vnode;

	if (vp->v_type == VREG || vp->v_type == VDIR) {
		/*
		 * Handle these two ioctls for regular files and
		 * directories.  All others will usually be failed
		 * with ENOTTY by the VFS-dependent code.  System V
		 * always failed all ioctls on regular files, but SunOS
		 * supported these.
		 */
		switch (uap->cmd) {
		case FIONREAD:
			vattr.va_mask = AT_SIZE;
			if (error = VOP_GETATTR(vp, &vattr, 0, fp->f_cred)) {
				RELEASEF(uap->fdes);
				return (error);
			}
			offset = vattr.va_size - fp->f_offset;
			if (copyout((caddr_t)&offset, (caddr_t)uap->arg,
			    sizeof (int))) {
				RELEASEF(uap->fdes);
				return (EFAULT);
			}
			RELEASEF(uap->fdes);
			return (0);

		case FIONBIO:
			if (copyin((caddr_t)uap->arg, (caddr_t)&flag,
			    sizeof (int))) {
				RELEASEF(uap->fdes);
				return (EFAULT);
			}
			mutex_enter(&fp->f_tlock);
			if (flag)
				fp->f_flag |= FNDELAY;
			else
				fp->f_flag &= ~FNDELAY;
			mutex_exit(&fp->f_tlock);
			RELEASEF(uap->fdes);
			return (0);

		default:
			break;
		}
	}
	flag = fp->f_flag;
	error = VOP_IOCTL(fp->f_vnode, uap->cmd, uap->arg,
	    flag, fp->f_cred, &rvp->r_val1);
	if (error == 0) {
		switch (uap->cmd) {
		case FIONBIO:
			if (copyin((caddr_t)uap->arg, (caddr_t)&flag,
			    sizeof (int))) {
				RELEASEF(uap->fdes);
				return (EFAULT);		/* XXX */
			}
			mutex_enter(&fp->f_tlock);
			if (flag)
				fp->f_flag |= FNDELAY;
			else
				fp->f_flag &= ~FNDELAY;
			mutex_exit(&fp->f_tlock);
			break;

		default:
			break;
	    }
	}
	RELEASEF(uap->fdes);
	return (error);
}

/*
 * Compatibility service routine for folks with character drivers that
 * (a) use BSD-style ioctls and (b) want us to do all the
 * copyin/copyout stuff for them.
 */
static struct bsd_compat_ioctltab *
bsd_compat_ioctltab_lookup(cmd, xxioctl_tab)
	int cmd;
	struct bsd_compat_ioctltab *xxioctl_tab;
{
	struct bsd_compat_ioctltab *p;

	if (xxioctl_tab == NULL)
		return (NULL);

	for (p = xxioctl_tab;
	    p->cmd != 0 || p->flag != 0 || p->size != 0; p++) {
		if (p->cmd == cmd)
			return (p);
	}

	/* Didn't find it */
	return (NULL);
}

int
bsd_compat_ioctl(dev, cmd, arg, mode, cr, rvalp, xxioctl, xxioctl_tab)
	dev_t dev;
	int cmd;
	int arg;
	int mode;
	struct cred *cr;
	int *rvalp;
	int (*xxioctl)();
	struct bsd_compat_ioctltab *xxioctl_tab;
{
	register u_int size;
	int data[howmany(IOCPARM_MASK/2, sizeof (int))];
	register caddr_t iocparm;
	register int error = 0;
	struct bsd_compat_ioctltab *p;
	int com;
	label_t	saveq;

	saveq = ttolwp(curthread)->lwp_qsav; /* In case we goto done: */

	if ((p = bsd_compat_ioctltab_lookup(cmd, xxioctl_tab)) != NULL) {
		size = p->size;
		com = p->flag;
	} else {
		/*
		 * Interpret high order word to find
		 * amount of data to be copied to/from the
		 * user's address space.
		 */
		size = (cmd >> 16) && IOCPARM_MASK;
		com = cmd & (IOC_INOUT|IOC_VOID);
	}

	if (size <= sizeof (data))
		/*
		 * If size is less than IOCPARM_MASK/2, use data[]
		 * to avoid overheads from kmem_alloc and kmem_free.
		 */
		iocparm = (caddr_t)data;
	else {
		/*
		 * Note that with the use of ioctl_tab, size is not
		 * limited by IOCPARM_MASK, so we eliminate the SunOS
		 * 4.x check of
		 *	if (size <= IOCPARM_MASK)
		 *
		 * Get space from kmem_alloc if parameter size is more
		 * than IOCPARM_MASK / 2 to avoid kernel stack overflow.
		 */
		iocparm = kmem_alloc(size, KM_SLEEP);
	}
	if (com&IOC_IN) {
		if (size == sizeof (int) && arg == NULL)
			*(int *)iocparm = 0;
		else if (size != 0) {
			if (copyin((caddr_t)arg, iocparm, size) != 0)
				error = EFAULT;
			if (error)
				goto done;
		} else
			*(caddr_t *)iocparm = (caddr_t)arg;
	} else if ((com&IOC_OUT) && size)
		/*
		 * Zero the buffer on the stack so the user
		 * always gets back something deterministic.
		 */
		bzero(iocparm, size);
	else if (com&IOC_VOID)
		*(caddr_t *)iocparm = (caddr_t)arg;

	/*
	 * Do a setjmp to catch signals in MT-unsafe drivers.
	 */
	if (setjmp(&ttolwp(curthread)->lwp_qsav)) {
		error = EINTR;
		goto done;
	} else
		error = (*xxioctl)(dev, cmd, arg, mode, cr, rvalp);
	/*
	 * Copy any data to user, size was
	 * already set and checked above.
	 */
	if (error == 0) {
		if ((com&IOC_OUT) && size)
			if (copyout(iocparm, (caddr_t)arg, size) != 0)
				error = EFAULT;
	}
done:
	if (iocparm != (caddr_t)data)
		kmem_free(iocparm, size);

	ttolwp(curthread)->lwp_qsav = saveq;
	return (error);
}

/*
 * Old stty and gtty.  (Still.)
 */
struct sgttya {
	int	fdes;
	int	arg;
};

int
stty(uap, rvp)
	register struct sgttya *uap;
	rval_t *rvp;
{
	struct ioctla na;

	na.fdes = uap->fdes;
	na.cmd = TIOCSETP;
	na.arg = uap->arg;
	return (ioctl(&na, rvp));
}

int
gtty(uap, rvp)
	register struct sgttya *uap;
	rval_t *rvp;
{
	struct ioctla na;

	na.fdes = uap->fdes;
	na.cmd = TIOCGETP;
	na.arg = uap->arg;
	return (ioctl(&na, rvp));
}
