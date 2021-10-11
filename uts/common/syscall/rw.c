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
 * 	(c) 1986, 1987, 1988, 1989, 1994  Sun Microsystems, Inc
 * 	(c) 1983, 1984, 1985, 1986, 1987, 1988, 1989  AT&T.
 *			All rights reserved.
 *
 */

#ident	"@(#)rw.c	1.3	94/10/04 SMI"	/* SVr4 1.103	*/

#include <sys/param.h>
#include <sys/isa_defs.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/cred.h>
#include <sys/user.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/proc.h>
#include <sys/cpuvar.h>
#include <sys/uio.h>
#include <sys/ioreq.h>
#include <sys/debug.h>

/*
 * read, write, pread, pwrite, readv, and writev syscalls.
 */

int
read(int fdes, char *cbuf, size_t count)
{
	struct uio auio;
	struct iovec aiov;
	file_t *fp;
	register vnode_t *vp;
	struct cpu *cp;
	int cnt, fflag, ioflag, rwflag, error, bcount;

	if ((fp = GETF(fdes)) == NULL)
		return (set_errno(EBADF));
	if (((fflag = fp->f_flag) & FREAD) == 0) {
		RELEASEF(fdes);
		return (set_errno(EBADF));
	}
	vp = fp->f_vnode;
	rwflag = 0;
	if ((cnt = (int)count) < 0) {
		RELEASEF(fdes);
		return (set_errno(EINVAL));
	}
	aiov.iov_base = (caddr_t)cbuf;
	aiov.iov_len = cnt;
	VOP_RWLOCK(vp, rwflag);
	auio.uio_loffset = fp->f_offset;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_resid = bcount = cnt;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_limit = u.u_rlimit[RLIMIT_FSIZE].rlim_cur;
	auio.uio_fmode = fflag;

	ioflag = auio.uio_fmode & (FAPPEND|FSYNC|FDSYNC|FRSYNC);

	/* If read sync is not asked for, filter sync flags */
	if ((ioflag & FRSYNC) == 0)
		ioflag &= ~(FSYNC|FDSYNC);
	error = VOP_READ(vp, &auio, ioflag, fp->f_cred);
	cnt -= auio.uio_resid;
	CPU_STAT_ENTER_K();
	cp = CPU;
	CPU_STAT_ADDQ(cp, cpu_sysinfo.sysread, 1);
	CPU_STAT_ADDQ(cp, cpu_sysinfo.readch, (unsigned)cnt);
	CPU_STAT_EXIT_K();
	ttolwp(curthread)->lwp_ru.ioch += (unsigned)cnt;

	if (vp->v_type == VFIFO)	/* Backward compatibility */
		fp->f_offset = cnt;
	else if (((fp->f_flag & FAPPEND) == 0) ||
		(vp->v_type != VREG) || (bcount != 0))	/* POSIX */
		fp->f_offset = auio.uio_loffset;
	VOP_RWUNLOCK(vp, rwflag);

	if (error == EINTR && cnt != 0)
		error = 0;
	RELEASEF(fdes);
	if (error)
		return (set_errno(error));
	return (cnt);
}

int
write(int fdes, char *cbuf, size_t count)
{
	struct uio auio;
	struct iovec aiov;
	file_t *fp;
	register vnode_t *vp;
	struct cpu *cp;
	int cnt, fflag, ioflag, rwflag, error, bcount;

	if ((fp = GETF(fdes)) == NULL)
		return (set_errno(EBADF));
	if (((fflag = fp->f_flag) & FWRITE) == 0) {
		RELEASEF(fdes);
		return (set_errno(EBADF));
	}
	vp = fp->f_vnode;
	rwflag = 1;
	if ((cnt = (int)count) < 0) {
		RELEASEF(fdes);
		return (set_errno(EINVAL));
	}
	aiov.iov_base = (caddr_t)cbuf;
	aiov.iov_len = cnt;
	VOP_RWLOCK(vp, rwflag);
	auio.uio_loffset = fp->f_offset;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_resid = bcount = cnt;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_limit = u.u_rlimit[RLIMIT_FSIZE].rlim_cur;
	auio.uio_fmode = fflag;

	ioflag = auio.uio_fmode & (FAPPEND|FSYNC|FDSYNC|FRSYNC);

	error = VOP_WRITE(vp, &auio, ioflag, fp->f_cred);
	cnt -= auio.uio_resid;
	CPU_STAT_ENTER_K();
	cp = CPU;
	CPU_STAT_ADDQ(cp, cpu_sysinfo.syswrite, 1);
	CPU_STAT_ADDQ(cp, cpu_sysinfo.writech, (unsigned)cnt);
	CPU_STAT_EXIT_K();
	ttolwp(curthread)->lwp_ru.ioch += (unsigned)cnt;

	if (vp->v_type == VFIFO)	/* Backward compatibility */
		fp->f_offset = cnt;
	else if (((fp->f_flag & FAPPEND) == 0) ||
		(vp->v_type != VREG) || (bcount != 0))	/* POSIX */
		fp->f_offset = auio.uio_loffset;
	VOP_RWUNLOCK(vp, rwflag);

	if (error == EINTR && cnt != 0)
		error = 0;
	RELEASEF(fdes);
	if (error)
		return (set_errno(error));
	return (cnt);
}

int
pread(int fdes, char *cbuf, size_t count, off_t offset)
{
	struct uio auio;
	struct iovec aiov;
	file_t *fp;
	register vnode_t *vp;
	struct cpu *cp;
	int fflag, ioflag, rwflag, error, bcount;

	if ((fp = GETF(fdes)) == NULL)
		return (set_errno(EBADF));
	if (((fflag = fp->f_flag) & (FREAD | FOFFSET)) == 0) {
		RELEASEF(fdes);
		return (set_errno(EBADF));
	}
	vp = fp->f_vnode;
	rwflag = 0;
	if ((bcount = (int)count) < 0) {
		RELEASEF(fdes);
		return (set_errno(EINVAL));
	}
	aiov.iov_base = (caddr_t)cbuf;
	aiov.iov_len = bcount;
	VOP_RWLOCK(vp, rwflag);
	auio._uio_offset._p._u = 0;
	auio._uio_offset._p._l = offset;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_resid = bcount;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_limit = u.u_rlimit[RLIMIT_FSIZE].rlim_cur;
	auio.uio_fmode = fflag;

	ioflag = auio.uio_fmode & (FAPPEND|FSYNC|FDSYNC|FRSYNC);

	/* If read sync is not asked for, filter sync flags */
	if ((ioflag & FRSYNC) == 0)
		ioflag &= ~(FSYNC|FDSYNC);
	error = VOP_READ(vp, &auio, ioflag, fp->f_cred);
	bcount -= auio.uio_resid;
	CPU_STAT_ENTER_K();
	cp = CPU;
	CPU_STAT_ADDQ(cp, cpu_sysinfo.sysread, 1);
	CPU_STAT_ADDQ(cp, cpu_sysinfo.readch, (unsigned)bcount);
	CPU_STAT_EXIT_K();
	ttolwp(curthread)->lwp_ru.ioch += (unsigned)bcount;
	VOP_RWUNLOCK(vp, rwflag);

	if (error == EINTR && bcount != 0)
		error = 0;
	RELEASEF(fdes);
	if (error)
		return (set_errno(error));
	return (bcount);
}

int
pwrite(int fdes, char *cbuf, size_t count, off_t offset)
{
	struct uio auio;
	struct iovec aiov;
	file_t *fp;
	register vnode_t *vp;
	struct cpu *cp;
	int fflag, ioflag, rwflag, error, bcount;

	if ((fp = GETF(fdes)) == NULL)
		return (set_errno(EBADF));
	if (((fflag = fp->f_flag) & (FWRITE | FOFFSET)) == 0) {
		RELEASEF(fdes);
		return (set_errno(EBADF));
	}
	vp = fp->f_vnode;
	rwflag = 1;
	if ((bcount = (int)count) < 0) {
		RELEASEF(fdes);
		return (set_errno(EINVAL));
	}
	aiov.iov_base = (caddr_t)cbuf;
	aiov.iov_len = bcount;
	VOP_RWLOCK(vp, rwflag);
	auio._uio_offset._p._u = 0;
	auio._uio_offset._p._l = offset;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_resid = bcount;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_limit = u.u_rlimit[RLIMIT_FSIZE].rlim_cur;
	auio.uio_fmode = fflag;

	ioflag = auio.uio_fmode & (FAPPEND|FSYNC|FDSYNC|FRSYNC);

	error = VOP_WRITE(vp, &auio, ioflag, fp->f_cred);
	bcount -= auio.uio_resid;
	CPU_STAT_ENTER_K();
	cp = CPU;
	CPU_STAT_ADDQ(cp, cpu_sysinfo.syswrite, 1);
	CPU_STAT_ADDQ(cp, cpu_sysinfo.writech, (unsigned)bcount);
	CPU_STAT_EXIT_K();
	ttolwp(curthread)->lwp_ru.ioch += (unsigned)bcount;
	VOP_RWUNLOCK(vp, rwflag);

	if (error == EINTR && bcount != 0)
		error = 0;
	RELEASEF(fdes);
	if (error)
		return (set_errno(error));
	return (bcount);
}


/*
 * XXX -- The SVID refers to IOV_MAX, but doesn't define it.  Grrrr....
 * XXX -- However, SVVS expects readv() and writev() to fail if
 * XXX -- iovcnt > 16 (yes, it's hard-coded in the SVVS source),
 * XXX -- so I guess that's the "interface".
 */
#define	DEF_IOV_MAX	16

int
readv(int fdes, struct iovec *iovp, int iovcnt)
{
	struct uio auio;
	struct iovec aiov[DEF_IOV_MAX];
	file_t *fp;
	register vnode_t *vp;
	struct cpu *cp;
	int fflag, ioflag, rwflag, count, error, bcount;
	int i;

	if ((fp = GETF(fdes)) == NULL)
		return (set_errno(EBADF));
	if (((fflag = fp->f_flag) & FREAD) == 0) {
		RELEASEF(fdes);
		return (set_errno(EBADF));
	}
	vp = fp->f_vnode;
	rwflag = 0;
	if (iovcnt <= 0 || iovcnt > DEF_IOV_MAX) {
		RELEASEF(fdes);
		return (set_errno(EINVAL));
	}
	if (copyin((caddr_t)iovp, (caddr_t)aiov,
	    (unsigned)iovcnt * sizeof (struct iovec))) {
		RELEASEF(fdes);
		return (set_errno(EFAULT));
	}
	count = 0;
	for (i = 0; i < iovcnt; i++) {
		int iovlen = aiov[i].iov_len;
		count += iovlen;
		if (iovlen < 0 || count < 0) {
			RELEASEF(fdes);
			return (set_errno(EINVAL));
		}
	}
	VOP_RWLOCK(vp, rwflag);
	auio.uio_loffset = fp->f_offset;
	auio.uio_iov = aiov;
	auio.uio_iovcnt = iovcnt;
	auio.uio_resid = bcount = count;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_limit = u.u_rlimit[RLIMIT_FSIZE].rlim_cur;
	auio.uio_fmode = fflag;

	ioflag = auio.uio_fmode & (FAPPEND|FSYNC|FDSYNC|FRSYNC);

	/* If read sync is not asked for, filter sync flags */
	if ((ioflag & FRSYNC) == 0)
		ioflag &= ~(FSYNC|FDSYNC);
	error = VOP_READ(vp, &auio, ioflag, fp->f_cred);
	count -= auio.uio_resid;
	CPU_STAT_ENTER_K();
	cp = CPU;
	CPU_STAT_ADDQ(cp, cpu_sysinfo.sysread, 1);
	CPU_STAT_ADDQ(cp, cpu_sysinfo.readch, (unsigned)count);
	CPU_STAT_EXIT_K();
	ttolwp(curthread)->lwp_ru.ioch += (unsigned)count;

	if (vp->v_type == VFIFO)	/* Backward compatibility */
		fp->f_offset = count;
	else if (((fp->f_flag & FAPPEND) == 0) ||
		(vp->v_type != VREG) || (bcount != 0))	/* POSIX */
		fp->f_offset = auio.uio_loffset;

	VOP_RWUNLOCK(vp, rwflag);

	if (error == EINTR && count != 0)
		error = 0;
	RELEASEF(fdes);
	if (error)
		return (set_errno(error));
	return (count);
}

int
writev(int fdes, struct iovec *iovp, int iovcnt)
{
	struct uio auio;
	struct iovec aiov[DEF_IOV_MAX];
	file_t *fp;
	register vnode_t *vp;
	struct cpu *cp;
	int fflag, ioflag, rwflag, count, error, bcount;
	int i;

	if ((fp = GETF(fdes)) == NULL)
		return (set_errno(EBADF));
	if (((fflag = fp->f_flag) & FWRITE) == 0) {
		RELEASEF(fdes);
		return (set_errno(EBADF));
	}
	vp = fp->f_vnode;
	rwflag = 1;
	if (iovcnt <= 0 || iovcnt > DEF_IOV_MAX) {
		RELEASEF(fdes);
		return (set_errno(EINVAL));
	}
	if (copyin((caddr_t)iovp, (caddr_t)aiov,
	    (unsigned)iovcnt * sizeof (struct iovec))) {
		RELEASEF(fdes);
		return (set_errno(EFAULT));
	}
	count = 0;
	for (i = 0; i < iovcnt; i++) {
		int iovlen = aiov[i].iov_len;
		count += iovlen;
		if (iovlen < 0 || count < 0) {
			RELEASEF(fdes);
			return (set_errno(EINVAL));
		}
	}
	VOP_RWLOCK(vp, rwflag);
	auio.uio_loffset = fp->f_offset;
	auio.uio_iov = aiov;
	auio.uio_iovcnt = iovcnt;
	auio.uio_resid = bcount = count;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_limit = u.u_rlimit[RLIMIT_FSIZE].rlim_cur;
	auio.uio_fmode = fflag;

	ioflag = auio.uio_fmode & (FAPPEND|FSYNC|FDSYNC|FRSYNC);

	error = VOP_WRITE(vp, &auio, ioflag, fp->f_cred);
	count -= auio.uio_resid;
	CPU_STAT_ENTER_K();
	cp = CPU;
	CPU_STAT_ADDQ(cp, cpu_sysinfo.syswrite, 1);
	CPU_STAT_ADDQ(cp, cpu_sysinfo.writech, (unsigned)count);
	CPU_STAT_EXIT_K();
	ttolwp(curthread)->lwp_ru.ioch += (unsigned)count;

	if (vp->v_type == VFIFO)	/* Backward compatibility */
		fp->f_offset = count;
	else if (((fp->f_flag & FAPPEND) == 0) ||
		(vp->v_type != VREG) || (bcount != 0))	/* POSIX */
		fp->f_offset = auio.uio_loffset;
	VOP_RWUNLOCK(vp, rwflag);

	if (error == EINTR && count != 0)
		error = 0;
	RELEASEF(fdes);
	if (error)
		return (set_errno(error));
	return (count);
}
