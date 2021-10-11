/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)utssys.c	1.4	95/03/24 SMI"	/* from SVr4.0 1.78 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/errno.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/proc.h>
#include <sys/session.h>
#include <sys/var.h>
#include <sys/utsname.h>
#include <sys/utssys.h>
#include <sys/ustat.h>
#include <sys/statvfs.h>
#include <sys/kmem.h>
#include <sys/debug.h>
#include <sys/pathname.h>

static int uts_fusers(char *, int, char *, rval_t *);
static int dofusers(vnode_t *, int, char *, rval_t *);

/*
 * utssys()
 */

struct utssysa {
	union {
		char *cbuf;
		struct ustat *ubuf;
	} ub;
	union {
		int	mv;		/* for USTAT */
		int	flags;		/* for FUSERS */
	} un;
	int	type;
	char	*outbp;			/* for FUSERS */
};

/*ARGSUSED1*/
int
utssys(register struct utssysa *uap, rval_t *rvp)
{
	register int error = 0;

	switch (uap->type) {

	case UTS_UNAME:
	{
		char *buf = uap->ub.cbuf;

		if (copyout(utsname.sysname, buf, 8)) {
			error = EFAULT;
			break;
		}
		buf += 8;
		if (subyte(buf, 0) < 0) {
			error = EFAULT;
			break;
		}
		buf++;
		if (copyout(utsname.nodename, buf, 8)) {
			error = EFAULT;
			break;
		}
		buf += 8;
		if (subyte(buf, 0) < 0) {
			error = EFAULT;
			break;
		}
		buf++;
		if (copyout(utsname.release, buf, 8)) {
			error = EFAULT;
			break;
		}
		buf += 8;
		if (subyte(buf, 0) < 0) {
			error = EFAULT;
			break;
		}
		buf++;
		if (copyout(utsname.version, buf, 8)) {
			error = EFAULT;
			break;
		}
		buf += 8;
		if (subyte(buf, 0) < 0) {
			error = EFAULT;
			break;
		}
		buf++;
		if (copyout(utsname.machine, buf, 8)) {
			error = EFAULT;
			break;
		}
		buf += 8;
		if (subyte(buf, 0) < 0) {
			error = EFAULT;
			break;
		}
		rvp->r_val1 = 1;
		break;
	}

	case UTS_USTAT:
	{
		register struct vfs *vfsp;
		struct ustat ust;
		struct statvfs stvfs;
		char *cp, *cp2;
		int i;

		/*
		 * Search vfs list for user-specified device.
		 */
		mutex_enter(&vfslist);
		for (vfsp = rootvfs; vfsp != NULL; vfsp = vfsp->vfs_next)
			if (vfsp->vfs_dev == uap->un.mv ||
			    cmpdev(vfsp->vfs_dev) == uap->un.mv)
				break;
		mutex_exit(&vfslist);
		if (vfsp == NULL) {
			error = EINVAL;
			break;
		}
		if (error = VFS_STATVFS(vfsp, &stvfs))
			break;

		/*
		 * Think about bringing something like this back if and when
		 * the sizes of an ino_t and a ulong become different.
		 * (i.e.: the size of f_ffree in a struct statvfs and the
		 * size of f_tinode in a struct ustat)
		 *
		 *	if (stvfs.f_ffree > USHRT_MAX) {
		 *		error = EOVERFLOW;
		 *		break;
		 *	}
		 */

		ust.f_tfree = (daddr_t)(stvfs.f_bfree * (stvfs.f_frsize/512));
		ust.f_tinode = (ino_t)stvfs.f_ffree;

		cp = stvfs.f_fstr;
		cp2 = ust.f_fname;
		i = 0;
		while (i++ < sizeof (ust.f_fname))
			if (*cp != '\0')
				*cp2++ = *cp++;
			else
				*cp2++ = '\0';
		while (*cp != '\0' &&
		    (i++ < sizeof (stvfs.f_fstr) - sizeof (ust.f_fpack)))
			cp++;
		cp++;
		cp2 = ust.f_fpack;
		i = 0;
		while (i++ < sizeof (ust.f_fpack))
			if (*cp != '\0')
				*cp2++ = *cp++;
			else
				*cp2++ = '\0';
		if (copyout((caddr_t)&ust, uap->ub.cbuf, sizeof (ust)))
			error = EFAULT;
		break;
	}

	case UTS_FUSERS:
	{
		return (uts_fusers(uap->ub.cbuf, uap->un.flags,
							uap->outbp, rvp));
	}

	default:
		error = EINVAL;		/* ? */
		break;
	}

	return (error);
}

/*
 * Determine the ways in which processes are using a named file or mounted
 * file system (path).  Normally return 0 with rvp->rval1 set to the number of
 * processes found to be using it.  For each of these, fill a f_user_t to
 * describe the process and its useage.  When successful, copy this list
 * of structures to the user supplied buffer (outbp).
 *
 * In error cases, clean up and return the appropriate errno.
 */

static int
uts_fusers(char *path, int flags, char *outbp, rval_t *rvp)
{
	vnode_t *fvp = NULL;
	int error;

	if ((error = lookupname(path, UIO_USERSPACE, FOLLOW, NULLVPP, &fvp))
	    != 0)
		return (error);
	ASSERT(fvp);
	error = dofusers(fvp, flags, outbp, rvp);
	VN_RELE(fvp);
	return (error);
}

static int
dofusers(vnode_t *fvp, int flags, char *outbp, rval_t *rvp)
{
	register proc_t *prp;
	register int pcnt = 0;		/* number of f_user_t's copied out */
	int error = 0;
	register int contained = (flags == F_CONTAINED);
	register vfs_t *cvfsp;
	register int use_flag = 0;
	file_t *fp;
	f_user_t *fuentry, *fubuf;	/* accumulate results here */
	int i;

	fuentry = fubuf = kmem_alloc(v.v_proc * sizeof (f_user_t), KM_SLEEP);
	if (contained && !(fvp->v_flag & VROOT)) {
		error = EINVAL;
		goto out;
	}
	if (fvp->v_count == 1)		/* no other active references */
		goto out;
	cvfsp = fvp->v_vfsp;
	ASSERT(cvfsp);

	mutex_enter(&pidlock);
	for (prp = practive; prp != NULL; prp = prp->p_next) {
		register user_t *up;

		mutex_enter(&prp->p_lock);
		if (prp->p_stat == SZOMB || prp->p_stat == SIDL) {
			mutex_exit(&prp->p_lock);
			continue;
		}

		up = PTOU(prp);
		if (up->u_cdir && (VN_CMP(fvp, up->u_cdir) || contained &&
		    up->u_cdir->v_vfsp == cvfsp)) {
			use_flag |= F_CDIR;
		}
		if (up->u_rdir && (VN_CMP(fvp, up->u_rdir) || contained &&
		    up->u_rdir->v_vfsp == cvfsp)) {
			use_flag |= F_RDIR;
		}
		if (prp->p_exec && (VN_CMP(fvp, prp->p_exec) ||
		    contained && prp->p_exec->v_vfsp == cvfsp)) {
			use_flag |= F_TEXT;
		}
		if (prp->p_trace && (VN_CMP(fvp, prp->p_trace) ||
		    contained && prp->p_trace->v_vfsp == cvfsp)) {
			use_flag |= F_TRACE;
		}
		TTY_HOLD(prp->p_sessp);
		if (prp->p_sessp && (VN_CMP(fvp, prp->p_sessp->s_vp) ||
		    contained && prp->p_sessp->s_vp &&
		    prp->p_sessp->s_vp->v_vfsp == cvfsp)) {
			use_flag |= F_TTY;
		}
		TTY_RELE(prp->p_sessp);
		mutex_exit(&prp->p_lock);

		mutex_enter(&up->u_flock);	/* get other user's lock */
		for (i = 0; i < up->u_nofiles; i++) {
			if ((fp = up->u_flist[i].uf_ofile) != NULLFP) {
				/*
				 * There is no race between the fp
				 * assignment and the line below
				 * because u_flock is held.  f_vnode
				 * will not change value because we
				 * make sure that the uf_ofile entry
				 * is not updated until the final
				 * value for f_vnode is acquired.
				 */
				if (fp->f_vnode &&
				    (VN_CMP(fvp, fp->f_vnode) ||
				    contained &&
				    fp->f_vnode->v_vfsp == cvfsp)) {
					use_flag |= F_OPEN;
					break; /* we don't count fds */
				}
			}
		}
		mutex_exit(&up->u_flock);
		/*
		 * mmap usage??
		 */
		if (use_flag) {
			fuentry->fu_pid = prp->p_pid;
			fuentry->fu_flags = use_flag;
			mutex_enter(&prp->p_crlock);
			fuentry->fu_uid = prp->p_cred->cr_ruid;
			mutex_exit(&prp->p_crlock);
			fuentry++;
			pcnt++;
			use_flag = 0;
		}
	}
	mutex_exit(&pidlock);
	if (copyout((caddr_t)fubuf, outbp, pcnt * sizeof (f_user_t)))
		error = EFAULT;
out:
	kmem_free(fubuf, v.v_proc * sizeof (f_user_t));
	rvp->r_val1 = pcnt;
	return (error);
}
