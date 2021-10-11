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
 * 	Copyright (c) 1986,1987,1988,1989,1994, by Sun Microsystems, Inc
 * 	Copyright (c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *	All rights reserved.
 *
 */

#ident	"@(#)fcntl.c	1.5	94/12/09 SMI"	/* SVr4 1.103	*/

#include <sys/param.h>
#include <sys/isa_defs.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/mode.h>
#include <sys/proc.h>
#include <sys/filio.h>
#include <sys/debug.h>

/*
 * File control.
 */

int
fcntl(int fdes, int cmd, int arg)
{
	file_t *fp;
	register int retval = 0;
	vnode_t *vp;
	offset_t offset;
	int flag, fd;
	struct flock bf;
	struct o_flock obf;
	char flags;

	if ((fp = GETF(fdes)) == NULL)
		return (set_errno(EBADF));
	vp = fp->f_vnode;
	flag = fp->f_flag;
	offset = fp->f_offset;

	switch (cmd) {

	case F_DUPFD:
		if (arg < 0 ||
		    arg >= u.u_rlimit[RLIMIT_NOFILE].rlim_cur)
			retval = set_errno(EINVAL);
		else
			if (retval = ufalloc(arg, &fd)) {
				retval = set_errno(retval);
			} else {
				setf(fd, fp);
				flags = getpof(fd);
				flags = flags & ~FCLOSEXEC;
				setpof(fd, flags);
				mutex_enter(&fp->f_tlock);
				fp->f_count++;
				mutex_exit(&fp->f_tlock);
				retval = fd;
				break;
			}
		break;

	case F_GETFD:
		retval = getpof(fdes);
		break;

	case F_SETFD:
		(void) setpof(fdes, (char)arg);
		break;

	case F_GETFL:
		mutex_enter(&fp->f_tlock);
		retval = fp->f_flag+FOPEN;
		mutex_exit(&fp->f_tlock);
		break;

	case F_SETFL:
		if ((arg & (FNONBLOCK|FNDELAY)) == (FNONBLOCK|FNDELAY))
			arg &= ~FNDELAY;
		if (retval = VOP_SETFL(vp, flag, arg, fp->f_cred)) {
			retval = set_errno(retval);
		} else {
			arg &= FMASK;
			mutex_enter(&fp->f_tlock);
			fp->f_flag &= (FREAD|FWRITE);
			fp->f_flag |= (arg-FOPEN) & ~(FREAD|FWRITE);
			mutex_exit(&fp->f_tlock);
		}
		break;

	case F_GETLK:
	case F_O_GETLK:
	case F_SETLK:
	case F_SETLKW:
		/*
		 * Copy in input fields only.
		 */
		if (copyin((caddr_t)arg, (caddr_t)&bf, sizeof (obf))) {
			retval = set_errno(EFAULT);
			break;
		}
		if (retval =
		    VOP_FRLOCK(vp, cmd, &bf, flag, offset, fp->f_cred)) {
			retval = set_errno(retval);
			break;
		}

		/*
		 * If command is GETLK and no lock is found, only
		 * the type field is changed.
		 */
		if ((cmd == F_O_GETLK || cmd == F_GETLK) &&
		    bf.l_type == F_UNLCK) {
			if (copyout((caddr_t)&bf.l_type,
			    (caddr_t)&((struct flock *)arg)->l_type,
			    sizeof (bf.l_type)))
				retval = set_errno(EFAULT);
			break;
		}

		if (cmd == F_O_GETLK) {
			/*
			 * Return an SVR3 flock structure to the user.
			 */
			obf.l_type = bf.l_type;
			obf.l_whence = bf.l_whence;
			obf.l_start = bf.l_start;
			obf.l_len = bf.l_len;
			if (bf.l_sysid > SHRT_MAX || bf.l_pid > SHRT_MAX) {
				/*
				 * One or both values for the above fields
				 * is too large to store in an SVR3 flock
				 * structure.
				 */
				retval = set_errno(EOVERFLOW);
				break;
			}
			obf.l_sysid = (short) bf.l_sysid;
			obf.l_pid = (o_pid_t) bf.l_pid;
			if (copyout((caddr_t)&obf, (caddr_t)arg, sizeof (obf)))
				retval = set_errno(EFAULT);
		} else if (cmd == F_GETLK) {
			/*
			 * Copy out SVR4 flock.
			 */
			int i;

			for (i = 0; i < 4; i++)
				bf.l_pad[i] = 0;
			if (copyout((caddr_t)&bf, (caddr_t)arg, sizeof (bf)))
				retval = set_errno(EFAULT);
		} else if (cmd == F_SETLKW && ttolwp(curthread)->lwp_cursig) {
			/*
			 * (see bugid 1169410)
			 * This clause is a groddy hack to make the POSIX
			 * test suite pass.  It is here because the underlying
			 * code in reclock() will both receive a signal *and*
			 * grant the lock (and return success here).
			 * However, if a signal is received, then the test
			 * suite expects the flock() call to fail.
			 *
			 * We test above for the presence of a received signal
			 * and if so then we modify the lock request to
			 * unlock what we just successfully locked and
			 * then return EINTR.
			 *
			 * This groddy hack must be removed in the next release
			 * and the underlying reclock() must be changed not to
			 * return in this condition!
			 */
			bf.l_type = F_UNLCK;
			(void) VOP_FRLOCK(vp, cmd, &bf, flag,
			    offset, fp->f_cred);
			retval = set_errno(EINTR);
		}
		break;

	case F_CHKFL:
		/*
		 * This is for internal use only, to allow the vnode layer
		 * to validate a flags setting before applying it.  User
		 * programs can't issue it.
		 */
		retval = set_errno(EINVAL);
		break;

	case F_ALLOCSP:
	case F_FREESP:
		if ((flag & FWRITE) == 0)
			retval = EBADF;
		else if (vp->v_type != VREG)
			retval = EINVAL;
		/*
		 * For compatibility we overlay an SVR3 flock on an SVR4
		 * flock.  This works because the input field offsets
		 * in "struct flock" were preserved.
		 */
		else if (copyin((caddr_t)arg, (caddr_t)&bf, sizeof (obf)))
			retval = EFAULT;
		else
			retval = VOP_SPACE(vp, cmd, &bf, flag, offset,
			    fp->f_cred);
		if (retval)
			retval = set_errno(retval);
		break;

	default:
		retval = set_errno(EINVAL);
		break;
	}

	RELEASEF(fdes);
	return (retval);
}
