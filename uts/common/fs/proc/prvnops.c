/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)prvnops.c	1.58	95/03/27 SMI"

#include <sys/types.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/cred.h>
#include <sys/debug.h>
#include <sys/dirent.h>
#include <sys/errno.h>
#include <sys/file.h>
#include <sys/inline.h>
#include <sys/kmem.h>
#include <sys/pathname.h>
#include <sys/proc.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/var.h>
#include <sys/mode.h>
#include <sys/poll.h>
#include <sys/user.h>
#include <sys/vfs.h>
#include <sys/vnode.h>

#include <sys/fault.h>
#include <sys/syscall.h>
#include <sys/procfs.h>

#include <fs/fs_subr.h>

#include <vm/rm.h>
#include <vm/as.h>
#include <vm/hat.h>

#include <fs/proc/prdata.h>

/*
 * Defined and initialized after all functions have been defined.
 */
extern struct vnodeops prvnodeops;

static struct kmem_cache *prnode_cache = NULL;

/*
 * Directory characteristics (patterned after the s5 file system).
 */
#define	PRROOTINO	2

#define	PRDIRSIZE	14
struct prdirect {
	u_short	d_ino;
	char	d_name[PRDIRSIZE];
};

#define	PRSDSIZE	(sizeof (struct prdirect))

static int
propen(vpp, flag, cr)
	vnode_t **vpp;
	register int flag;
	struct cred *cr;
{
	register vnode_t *vp = *vpp;
	register prnode_t *pnp = VTOP(vp);
	register int error = 0;
	register proc_t *p;

	/*
	 * Nothing to do for the /proc directory itself.
	 */
	if (vp->v_type == VDIR)
		return (0);

	/*
	 * If the process exists, lock it now.
	 * Otherwise we have a race condition with prclose().
	 * Kludge: if we are opening an lwp file descriptor or a
	 * page data file descriptor then we are coming in from
	 * prioctl() and the process is already locked.
	 */
	if (pnp->pr_type == PRT_PROC) {
		p = pr_p_lock(pnp);
		mutex_exit(&pr_pidlock);
		if (p == NULL)
			return (ENOENT);
		ASSERT(p == pnp->pr_proc);
	}
	p = pnp->pr_proc;
	ASSERT(p->p_flag & SPRLOCK);

	/*
	 * Maintain a count of opens for write.  Allow exactly one
	 * O_RDWR|O_EXCL request and fail subsequent ones (even for
	 * the super-user).  Once an exclusive open is in effect,
	 * fail subsequent attempts to open for writing (except for
	 * the super-user).
	 */
	if (flag & FWRITE) {
		if (flag & FEXCL) {
			if (pnp->pr_writers > 0) {
				error = EBUSY;
				goto out;
			}
			pnp->pr_flags |= PREXCL;
		} else if (pnp->pr_flags & PREXCL) {
			ASSERT(pnp->pr_writers > 0);
			if (cr->cr_uid) {
				error = EBUSY;
				goto out;
			}
		}
		pnp->pr_writers++;
	}

	/*
	 * Keep a count of opens so that we can identify the last close.
	 * The vnode reference count (v_count) is unsuitable for this
	 * because references are generated by other operations in
	 * addition to open and close.
	 */
	pnp->pr_opens++;

	/*
	 * Enable data collection for page data file;
	 * get unique id from the hat layer.
	 */
	if (pnp->pr_type == PRT_PDATA) {
		int id;

		if ((p->p_flag & SSYS) || p->p_as == &kas ||
		    (id = hat_startstat(p->p_as)) == -1)
			error = ENOMEM;
		else
			pnp->pr_hatid = (u_int)id;
	}

out:
	if (pnp->pr_type == PRT_PROC)
		prunlock(pnp);

	return (error);
}

/* ARGSUSED */
static int
prclose(vp, flag, count, offset, cr)
	register vnode_t *vp;
	int flag;
	int count;
	offset_t offset;
	struct cred *cr;
{
	register prnode_t *pnp = VTOP(vp);
	register proc_t *p;
	register kthread_t *t;
	register vnode_t *pvp;
	user_t *up;

	/*
	 * Nothing to do for the /proc directory itself.
	 */
	if (vp->v_type == VDIR)
		return (0);

	/*
	 * There is nothing more to do until the last close
	 * of the file table entry.
	 */
	if (count > 1)
		return (0);

	/*
	 * If the process exists, lock it now.
	 * Otherwise we have a race condition with propen().
	 * Hold pr_pidlock across the reference to pr_opens,
	 * and pr_writers in case there is no process anymore,
	 * to cover the case of concurrent calls to prclose()
	 * after the process has been reaped by freeproc().
	 */
	p = pr_p_lock(pnp);

	/*
	 * When the last reference to a writable file descriptor goes
	 * away, decrement the count of writers.  When the count of
	 * writers drops to zero, clear the "exclusive-use" flag.
	 * Anomaly: we can't distinguish multiple writers enough to
	 * tell which one originally requested exclusive-use, so once
	 * exclusive-use is set it stays on until all writers have
	 * gone away.
	 */
	--pnp->pr_opens;
	if ((flag & FWRITE) && --pnp->pr_writers == 0)
		pnp->pr_flags &= ~PREXCL;
	mutex_exit(&pr_pidlock);

	/*
	 * If there is no process, there is nothing more to do.
	 */
	if (p == NULL)
		return (0);
	ASSERT(p == pnp->pr_proc);

	/*
	 * If this is a page data file,
	 * free the hat level statistics and return.
	 */
	if (pnp->pr_type == PRT_PDATA) {
		if (p->p_as != &kas && pnp->pr_hatid != 0)
			hat_freestat(p->p_as, pnp->pr_hatid);
		pnp->pr_hatid = 0;
		prunlock(pnp);
		return (0);
	}

	/*
	 * On last close of all writable file descriptors,
	 * perform run-on-last-close and/or kill-on-last-close logic.
	 */
	for (pvp = p->p_plist; pvp != NULL; pvp = VTOP(pvp)->pr_vnext)
		if (VTOP(pvp)->pr_writers)
			break;

	if (pvp == NULL &&
	    !(pnp->pr_flags & PRZOMB) &&
	    p->p_stat != SZOMB &&
	    (p->p_flag & (SRUNLCL|SKILLCL))) {
		int killproc;

		/*
		 * If any tracing flags are set, clear them.
		 */
		if (p->p_flag & SPROCTR) {
			up = prumap(p);
			premptyset(&up->u_entrymask);
			premptyset(&up->u_exitmask);
			up->u_systrap = 0;
			prunmap(p);
		}
		premptyset(&p->p_sigmask);
		premptyset(&p->p_fltmask);
		killproc = (p->p_flag & SKILLCL);
		p->p_flag &= ~(SRUNLCL|SKILLCL|SPROCTR);
		/*
		 * Cancel any outstanding single-step requests.
		 */
		if ((t = p->p_tlist) != NULL) {
			/*
			 * Drop p_lock because prnostep() touches the stack.
			 * The loop is safe because the process is SPRLOCK'd.
			 */
			mutex_exit(&p->p_lock);
			do {
				prnostep(ttolwp(t));
			} while ((t = t->t_forw) != p->p_tlist);
			mutex_enter(&p->p_lock);
		}
		/*
		 * Set runnable all lwps stopped by /proc.
		 */
		if (killproc)
			sigtoproc(p, NULL, SIGKILL, 0);
		else
			allsetrun(p);
	}

	prunlock(pnp);
	return (0);
}

/* ARGSUSED */
static int
prread(vp, uiop, ioflag, cr)
	register vnode_t *vp;
	register struct uio *uiop;
	int ioflag;
	struct cred *cr;
{
	static struct prdirect dotbuf[] = {
		{ PRROOTINO, "."  },
		{ PRROOTINO, ".." }
	};
	struct prdirect dirbuf;
	register int i, n, j;
	int minproc, maxproc, modoff;
	register prnode_t *pnp = VTOP(vp);
	int error = 0;

	if (vp->v_type == VDIR) {
		/*
		 * Fake up ".", "..", and the /proc directory entries.
		 */
		if (uiop->uio_offset < 0 ||
		    uiop->uio_offset >= (v.v_proc + 2) * PRSDSIZE ||
		    uiop->uio_resid <= 0)
			return (0);
		if (uiop->uio_offset < 2*PRSDSIZE) {
			error = uiomove((caddr_t)dotbuf + uiop->uio_offset,
			    min(uiop->uio_resid, 2*PRSDSIZE - uiop->uio_offset),
			    UIO_READ, uiop);
			if (uiop->uio_resid <= 0 || error)
				return (error);
		}
		minproc = (uiop->uio_offset - 2*PRSDSIZE)/PRSDSIZE;
		maxproc = (uiop->uio_offset + uiop->uio_resid - 1)/PRSDSIZE;
		modoff = uiop->uio_offset % PRSDSIZE;
		for (j = 0; j < PRDIRSIZE; j++)
			dirbuf.d_name[j] = '\0';
		for (i = minproc; i < min(maxproc, v.v_proc); i++) {
			proc_t *p;
			mutex_enter(&pidlock);
			if ((p = pid_entry(i)) != NULL && p->p_stat != SIDL) {
				ASSERT(p->p_stat != 0);
				n = p->p_pid;
				mutex_exit(&pidlock);
				dirbuf.d_ino = ptoi(n);
				for (j = PNSIZ-1; j >= 0; j--) {
					dirbuf.d_name[j] = n % 10 + '0';
					n /= 10;
				}
			} else {
				mutex_exit(&pidlock);
				dirbuf.d_ino = 0;
				for (j = 0; j <= PNSIZ-1; j++)
					dirbuf.d_name[j] = '\0';
			}
			error = uiomove((caddr_t)&dirbuf + modoff,
			    min(uiop->uio_resid, PRSDSIZE - modoff),
			    UIO_READ, uiop);
			if (uiop->uio_resid <= 0 || error)
				return (error);
			modoff = 0;
		}
	} else if ((error = prlock(pnp, ZNO)) == 0) {
		register proc_t *p = pnp->pr_proc;
		register struct as *as = p->p_as;

		/*
		 * /proc I/O cannot be done to a system process.
		 */
		if ((p->p_flag & SSYS) || as == &kas) {
			prunlock(pnp);
			return ((pnp->pr_type == PRT_PDATA)? 0 : EIO);
		}

		/*
		 * We drop p_lock because we don't want to hold
		 * it over an I/O operation because that could
		 * lead to deadlock with the clock thread.
		 * The process will not disappear and its address
		 * space will not change because it is marked SPRLOCK.
		 */
		mutex_exit(&p->p_lock);
		if (pnp->pr_type == PRT_PDATA)
			error = prpdread(as, pnp->pr_hatid, uiop);
		else
			error = prusrio(as, UIO_READ, uiop);
		mutex_enter(&p->p_lock);
		prunlock(pnp);
	}
	return (error);
}

/* ARGSUSED */
static int
prwrite(vp, uiop, ioflag, cr)
	register vnode_t *vp;
	register struct uio *uiop;
	int ioflag;
	struct cred *cr;
{
	register prnode_t *pnp = VTOP(vp);
	int error;

	if (vp->v_type == VDIR)
		error = EISDIR;
	else if (pnp->pr_type == PRT_PDATA)
		error = EBADF;
	else if ((error = prlock(pnp, ZNO)) == 0) {
		register proc_t *p = pnp->pr_proc;
		register struct as *as = p->p_as;

		/*
		 * /proc I/O cannot be done to a system process.
		 */
		if ((p->p_flag & SSYS) || as == &kas) {
			prunlock(pnp);
			return (EIO);
		}

		/*
		 * See comments above (prread) about this locking dance.
		 */
		mutex_exit(&p->p_lock);
		error = prusrio(as, UIO_WRITE, uiop);
		mutex_enter(&p->p_lock);
		prunlock(pnp);
	}
	return (error);
}

/* prioctl in prioctl.c */

/* ARGSUSED */
static int
prgetattr(vp, vap, flags, cr)
	register vnode_t *vp;
	register struct vattr *vap;
	int flags;
	struct cred *cr;
{
	prnode_t *pnp = VTOP(vp);

	/*
	 * Return all the attributes.  Should be refined so that it
	 * returns only those asked for.
	 *
	 * Most of this is complete fakery anyway.
	 */
	if (vp->v_type == VDIR) {
		vap->va_uid = 0;
		vap->va_gid = 0;
		vap->va_nlink = 2;
		vap->va_nodeid = PRROOTINO;
		vap->va_size = (v.v_proc + 2) * PRSDSIZE;
		vap->va_atime = vap->va_mtime = vap->va_ctime = hrestime;
	} else {
		proc_t *p;
		struct as *as;
		user_t *up;

		p = pr_p_lock(pnp);
		mutex_exit(&pr_pidlock);
		if (p == NULL)
			return (ENOENT);
		mutex_enter(&p->p_crlock);
		vap->va_uid = p->p_cred->cr_ruid;
		vap->va_gid = p->p_cred->cr_rgid;
		mutex_exit(&p->p_crlock);
		vap->va_nlink = 1;
		/*
		 * XXX: fix me!
		 * A /proc lwp file descriptor should not return the
		 * same va_nodeid as the /proc process file descriptor.
		 */
		vap->va_nodeid = ptoi(p->p_pid);
		up = prumap(p);
		vap->va_atime.tv_sec = vap->va_mtime.tv_sec =
		    vap->va_ctime.tv_sec = up->u_start;
		vap->va_atime.tv_nsec = vap->va_mtime.tv_nsec =
		    vap->va_ctime.tv_nsec = 0;
		prunmap(p);
		if ((p->p_flag & SSYS) || (as = p->p_as) == &kas)
			vap->va_size = 0;
		else {
			/*
			 * We can drop p->p_lock before grabbing the
			 * address space lock because p->p_as will not
			 * change while the process is marked SPRLOCK.
			 */
			mutex_exit(&p->p_lock);
			AS_LOCK_ENTER(as, &as->a_lock, RW_READER);
			if (pnp->pr_type != PRT_PDATA)
				vap->va_size = rm_assize(as);
			else
				vap->va_size = prpdsize(as);
			AS_LOCK_EXIT(as, &as->a_lock);
			mutex_enter(&p->p_lock);
		}
		prunlock(pnp);
	}

	vap->va_type = vp->v_type;
	vap->va_mode = pnp->pr_mode;
	vap->va_fsid = procdev;
	vap->va_rdev = 0;
	vap->va_blksize = 1024;
	vap->va_nblocks = btod(vap->va_size);
	vap->va_vcode = 0;
	return (0);
}

/* ARGSUSED */
static int
praccess(vp, mode, flags, cr)
	register vnode_t *vp;
	register int mode;
	int flags;
	register struct cred *cr;
{
	register prnode_t *pnp = VTOP(vp);

	if (pnp->pr_flags & PRINVAL)
		return (EAGAIN);
	if ((mode & VWRITE) && (vp->v_vfsp->vfs_flag & VFS_RDONLY))
		return (EROFS);
	if (vp->v_type == VREG) {
		register proc_t *p;
		register int error = 0;
		register vnode_t *xvp;

		mutex_enter(&pr_pidlock);
		if ((p = pnp->pr_proc) == NULL) {
			mutex_exit(&pr_pidlock);
			return (ENOENT);
		}
		mutex_enter(&p->p_lock);
		mutex_exit(&pr_pidlock);
		if (cr->cr_uid != 0) {
			mutex_enter(&p->p_crlock);
			if (cr->cr_uid != p->p_cred->cr_ruid ||
			    cr->cr_uid != p->p_cred->cr_suid ||
			    cr->cr_gid != p->p_cred->cr_rgid ||
			    cr->cr_gid != p->p_cred->cr_sgid)
				error = EACCES;
			mutex_exit(&p->p_crlock);
		}

		if (error || cr->cr_uid == 0 ||
		    (p->p_flag & SSYS) || p->p_as == &kas ||
		    (xvp = p->p_exec) == NULL)
			mutex_exit(&p->p_lock);
		else {
			/*
			 * Determine if the process's executable is readable.
			 * We have to drop p->p_lock before the VOP operation.
			 */
			VN_HOLD(xvp);
			mutex_exit(&p->p_lock);
			error = VOP_ACCESS(xvp, VREAD, 0, cr);
			VN_RELE(xvp);
		}
		if (error)
			return (error);
	}
	if (cr->cr_uid == 0 || (pnp->pr_mode & mode) == mode)
		return (0);
	return (EACCES);
}

/*
 * Find or construct a process vnode for the given pid.
 */
static vnode_t *
prget(pid)
	pid_t pid;
{
	register proc_t *p;
	register vnode_t *vp;
	register prnode_t *pnp = prgetnode();

	mutex_enter(&pidlock);
	/*
	 * We check prmounted to make sure we don't allocate any more
	 * /proc vnodes after /proc is umounted.  Protected by pidlock.
	 */
	if (!prmounted || (p = prfind(pid)) == NULL || p->p_stat == SIDL) {
		mutex_exit(&pidlock);
		prfreenode(pnp);
		return (NULL);
	}
	mutex_enter(&p->p_lock);

	ASSERT(p->p_stat != 0);
	if ((vp = p->p_trace) != NULL) {
		ASSERT(!(VTOP(vp)->pr_flags & PRINVAL));
		VN_HOLD(vp);
		mutex_exit(&p->p_lock);
		mutex_exit(&pidlock);
		prfreenode(pnp);
		return (vp);
	}

	/*
	 * prgetnode() initialized most of the prnode.
	 * Finish the job.
	 */
	vp = &pnp->pr_vnode;
	pnp->pr_proc = p;
	pnp->pr_type = PRT_PROC;
	if (p->p_tlist == NULL)
		pnp->pr_flags |= PRZOMB;
	p->p_trace = vp;
	/*
	 * Link new vnode into list of all /proc vnodes for the process.
	 */
	pnp->pr_vnext = p->p_plist;
	p->p_plist = vp;
	mutex_exit(&p->p_lock);
	mutex_exit(&pidlock);
	return (vp);
}

/* ARGSUSED */
static int
prlookup(dp, comp, vpp, pnp, flags, rdir, cr)
	vnode_t *dp;
	register char *comp;
	vnode_t **vpp;
	struct pathname *pnp;
	int flags;
	vnode_t *rdir;
	struct cred *cr;
{
	register pid_t pid = 0;

	if (comp[0] == 0 || strcmp(comp, ".") == 0 || strcmp(comp, "..") == 0) {
		VN_HOLD(dp);
		*vpp = dp;
		return (0);
	}
	while (*comp) {
		if (*comp < '0' || *comp > '9')
			return (ENOENT);
		pid = 10 * pid + *comp++ - '0';
	}
	*vpp = prget(pid);
	return ((*vpp == NULL) ? ENOENT : 0);
}

/*
 * Find or construct an lwp vnode for the given tid.
 */
vnode_t *
prlwpnode(p, tid, pnp)
	register proc_t *p;
	u_int tid;		/* same type as t->t_tid */
	register prnode_t *pnp;	/* allocated by the caller */
{
	register vnode_t *vp;
	register kthread_t *t;

	ASSERT(MUTEX_HELD(&p->p_lock));

	if ((t = p->p_tlist) != NULL) {
		do {
			if (t->t_tid == tid)
				break;
		} while ((t = t->t_forw) != p->p_tlist);
	}

	if (t == NULL || t->t_tid != tid || ttolwp(t) == NULL) {
		prfreenode(pnp);
		return (NULL);
	}
	ASSERT(t->t_state != TS_FREE);

	if ((vp = t->t_trace) != NULL) {
		ASSERT(!(VTOP(vp)->pr_flags & PRINVAL));
		VN_HOLD(vp);
		prfreenode(pnp);
		return (vp);
	}

	/*
	 * prgetnode() initialized most of the prnode.
	 * Finish the job.
	 */
	pnp->pr_proc = p;
	pnp->pr_thread = t;
	pnp->pr_type = PRT_LWP;
	t->t_trace = vp = &pnp->pr_vnode;
	/*
	 * Link new vnode into list of all /proc vnodes for the process.
	 */
	pnp->pr_vnext = p->p_plist;
	p->p_plist = &pnp->pr_vnode;
	return (vp);
}

/*
 * Construct page data vnode for the given process.
 */
vnode_t *
prpdnode(p, pnp)
	register proc_t *p;
	register prnode_t *pnp;	/* allocated by the caller */
{
	/*
	 * prgetnode() initialized most of the prnode.
	 * Finish the job.
	 */
	pnp->pr_proc = p;
	pnp->pr_type = PRT_PDATA;
	pnp->pr_hatid = (u_int)0;
	/*
	 * Link new vnode into list of all /proc vnodes for the process.
	 */
	pnp->pr_vnext = p->p_plist;
	p->p_plist = &pnp->pr_vnode;
	return (&pnp->pr_vnode);
}

/*
 * New /proc vnode required; allocate it and fill in most of the fields.
 */
prnode_t *
prgetnode()
{
	register prnode_t *pnp;
	register vnode_t *vp;

	if (prnode_cache == NULL)
		prnode_cache = kmem_cache_create("prnode_cache",
			sizeof (prnode_t), 0, NULL, NULL, NULL);
	pnp = kmem_cache_alloc(prnode_cache, KM_SLEEP);
	bzero((caddr_t)pnp, sizeof (*pnp));
	cv_init(&pnp->pr_wait, "procfs pr_wait", CV_DEFAULT, NULL);
	mutex_init(&pnp->pr_lock, "procfs pr_lock", MUTEX_DEFAULT, DEFAULT_WT);
	vp = &pnp->pr_vnode;
	mutex_init(&vp->v_lock, "procfs v_lock", MUTEX_DEFAULT, DEFAULT_WT);
	vp->v_type = VREG;
	vp->v_vfsp = procvfs;
	vp->v_op = &prvnodeops;
	vp->v_count = 1;
	vp->v_data = (caddr_t) pnp;
	vp->v_flag = VNOMAP|VNOMOUNT|VNOSWAP;
	pnp->pr_mode = 0600;	/* R/W only by owner */

	return (pnp);
}

/*
 * Return /proc vnode to the cache.
 */
void
prfreenode(pnp)
	register prnode_t *pnp;
{
	cv_destroy(&pnp->pr_wait);
	mutex_destroy(&pnp->pr_lock);
	mutex_destroy(&PTOV(pnp)->v_lock);
	kmem_cache_free(prnode_cache, pnp);
}

/* ARGSUSED */
static int
prreaddir(vp, uiop, cr, eofp)
	vnode_t *vp;
	register struct uio *uiop;
	struct cred *cr;
	int *eofp;
{
	/* bp holds one dirent structure */
	char bp[round(sizeof (struct dirent)-1+PNSIZ+1)];
	struct dirent *dirent = (struct dirent *) bp;
	int reclen;
	register int i, j, n;
	int oresid, dsize;
	off_t off;

	if (uiop->uio_offset < 0 ||
	    uiop->uio_resid <= 0 ||
	    (uiop->uio_offset % PRSDSIZE) != 0)
		return (ENOENT);
	dsize = (char *)dirent->d_name - (char *)dirent;
	oresid = uiop->uio_resid;
	/*
	 * Loop until user's request is satisfied or until all processes
	 * have been examined.
	 */
	/* LINTED lint confusion: used before set: off */
	for (; uiop->uio_resid > 0; uiop->uio_offset = off + PRSDSIZE) {
		if ((off = uiop->uio_offset) == 0) {	/* "." */
			dirent->d_ino = PRROOTINO;
			dirent->d_name[0] = '.';
			dirent->d_name[1] = '\0';
			reclen = dsize+1+1;
		} else if (off == PRSDSIZE) { /* ".." */
			dirent->d_ino = PRROOTINO;
			dirent->d_name[0] = '.';
			dirent->d_name[1] = '.';
			dirent->d_name[2] = '\0';
			reclen = dsize+2+1;
		} else {
			/*
			 * Stop when entire proc table has been examined.
			 */
			proc_t *p;
			if ((i = (off-2*PRSDSIZE)/PRSDSIZE) >= v.v_proc)
				break;
			mutex_enter(&pidlock);
			if ((p = pid_entry(i)) == NULL || p->p_stat == SIDL) {
				mutex_exit(&pidlock);
				continue;
			}
			ASSERT(p->p_stat != 0);
			n = p->p_pid;
			mutex_exit(&pidlock);
			dirent->d_ino = ptoi(n);
			for (j = PNSIZ-1; j >= 0; j--) {
				dirent->d_name[j] = n % 10 + '0';
				n /= 10;
			}
			dirent->d_name[PNSIZ] = '\0';
			reclen = dsize+PNSIZ+1;
		}
		dirent->d_off = uiop->uio_offset + PRSDSIZE;
		/*
		 * Pad to nearest word boundary (if necessary).
		 */
		for (i = reclen; i < round(reclen); i++)
			dirent->d_name[i-dsize] = '\0';
		dirent->d_reclen = reclen = round(reclen);
		if (reclen > uiop->uio_resid) {
			/*
			 * Error if no entries have been returned yet.
			 */
			if (uiop->uio_resid == oresid)
				return (EINVAL);
			break;
		}
		/*
		 * uiomove() updates both resid and offset by the same
		 * amount.  But we want offset to change in increments
		 * of PRSDSIZE, which is different from the number of bytes
		 * being returned to the user.  So we set uio_offset
		 * separately, ignoring what uiomove() does.
		 */
		if (uiomove((caddr_t) dirent, reclen, UIO_READ, uiop))
			return (EFAULT);
	}
	if (eofp)
		*eofp = ((uiop->uio_offset-2*PRSDSIZE)/PRSDSIZE >= v.v_proc);
	return (0);
}

/* ARGSUSED */
static int
prfsync(vp, syncflag, cr)
	vnode_t *vp;
	int syncflag;
	struct cred *cr;
{
	return (0);
}

/* ARGSUSED */
static void
prinactive(vp, cr)
	register vnode_t *vp;
	struct cred *cr;
{
	register prnode_t *pnp = VTOP(vp);
	register proc_t *p;
	register kthread_t *t;

	mutex_enter(&pr_pidlock);
	if ((p = pnp->pr_proc) != NULL)
		mutex_enter(&p->p_lock);
	mutex_enter(&vp->v_lock);

	if (vp->v_type == VDIR || vp->v_count > 1) {
		vp->v_count--;
		mutex_exit(&vp->v_lock);
		if (p != NULL)
			mutex_exit(&p->p_lock);
		mutex_exit(&pr_pidlock);
		return;
	}

	ASSERT(vp->v_count == 1);
	ASSERT(pnp->pr_opens == 0);
	mutex_exit(&pr_pidlock);

	if (p != NULL) {
		if (vp == p->p_trace)
			p->p_trace = NULL;
		if ((t = pnp->pr_thread) != NULL &&
		    vp == t->t_trace)
			t->t_trace = NULL;
		/*
		 * Remove the vnode from the list of
		 * all /proc vnodes for the process.
		 */
		if (vp == p->p_plist)
			p->p_plist = pnp->pr_vnext;
		else {
			register vnode_t *pvp = p->p_plist;

			ASSERT(pvp != NULL);
			while (VTOP(pvp)->pr_vnext != vp) {
				pvp = VTOP(pvp)->pr_vnext;
				ASSERT(pvp != NULL);
			}
			VTOP(pvp)->pr_vnext = pnp->pr_vnext;
		}
		mutex_exit(&p->p_lock);
	}
	pnp->pr_vnext = NULL;

	ASSERT(pnp->pr_pollhead.ph_list == NULL);

	mutex_exit(&vp->v_lock);
	prfreenode(pnp);
}

/* ARGSUSED */
static int
prseek(vp, ooff, noffp)
	vnode_t *vp;
	offset_t ooff;
	offset_t *noffp;
{
	return (0);
}

/*
 * Return the answer requested to poll().
 * POLLIN, POLLRDNORM, and POLLOUT are recognized as in fs_poll().
 * In addition, these have special meaning for /proc files:
 *	POLLPRI		process or lwp stopped on an event of interest
 *	POLLERR		/proc file descriptor is invalid
 *	POLLHUP		process or lwp has terminated
 */
static int
prpoll(vnode_t *vp,
	register short events,
	int anyyet,
	register short *reventsp,
	pollhead_t **phpp)
{
	register prnode_t *pnp = VTOP(vp);
	pollhead_t *php = &pnp->pr_pollhead;
	pollstate_t *ps = curthread->t_pollstate;
	int ps_lock_owned = mutex_owned(&ps->ps_lock);
	int pp_lock_owned = ps_lock_owned && pplockowned(php);
	proc_t *p;
	short revents;
	int error;

	*reventsp = revents = 0;
	*phpp = (pollhead_t *)NULL;

	if (vp->v_type == VDIR || pnp->pr_type == PRT_PDATA) {
		*reventsp |= POLLNVAL;
		return (0);
	}

	/*
	 * The poll() framework calls us once without holding either of
	 * ps->ps_lock or pplock() and then again while holding ps->ps_lock
	 * and possibly pplock().  If we hold these locks, we have to drop
	 * them here because otherwise we would have a lock ordering conflict
	 * with prnotify().
	 *
	 * prnotify:
	 *	holds p->p_lock
	 *	calls pollwakeup(): grabs either pplock() or ps->ps_lock
	 * here:
	 *	hold ps->ps_lock and possibly pplock()
	 *	call prlock(): grabs p->p_lock
	 */
	if (pp_lock_owned)
		ppunlock(php);
	if (ps_lock_owned)
		mutex_exit(&ps->ps_lock);

	if ((error = prlock(pnp, ZNO)) != 0) {
		if (ps_lock_owned)
			mutex_enter(&ps->ps_lock);
		if (pp_lock_owned)
			pplock(php);
		switch (error) {
		case ENOENT:		/* process or lwp died */
			*reventsp = POLLHUP;
			error = 0;
			break;
		case EAGAIN:		/* invalidated */
			*reventsp = POLLERR;
			error = 0;
			break;
		}
		return (error);
	}

	/*
	 * We have the process marked locked (SPRLOCK) and we are holding
	 * its p->p_lock.  We want to unmark the process but retain
	 * exclusive control w.r.t. other /proc controlling processes
	 * before reacquiring the polling locks.
	 *
	 * prunmark() does this for us.  It unmarks the process
	 * but retains p->p_lock so we still have exclusive control.
	 * We will drop p->p_lock at the end to relinquish control.
	 *
	 * We cannot call prunlock() at the end to relinquish control
	 * because prunlock(), like prunmark(), may drop and reacquire
	 * p->p_lock and that would lead to a lock order violation
	 * w.r.t. the polling locks we are about to reacquire.
	 */
	p = pnp->pr_proc;
	ASSERT(p != NULL);
	(void) prunmark(p);

	/*
	 * We reacquire the locks here so we don't lose any polling events.
	 */
	if (ps_lock_owned)
		mutex_enter(&ps->ps_lock);
	if (pp_lock_owned)
		pplock(php);

	if ((p->p_flag & SSYS) || p->p_as == &kas)
		revents = POLLNVAL;
	else {
		if (events & POLLIN)
			revents |= POLLIN;
		if (events & POLLRDNORM)
			revents |= POLLRDNORM;
		if (events & POLLOUT)
			revents |= POLLOUT;
		if (events & POLLPRI) {
			register kthread_t *t;

			if (pnp->pr_type == PRT_LWP) {
				t = pnp->pr_thread;
				thread_lock(t);
			} else {
				t = prchoose(p);	/* returns locked t */
			}
			ASSERT(t != NULL);

			if (ISTOPPED(t) || VSTOPPED(t))
				revents |= POLLPRI;
			thread_unlock(t);
		}
	}

	*reventsp = revents;
	if (!anyyet && revents == 0) {
		/*
		 * Arrange to wake up the polling lwp when
		 * the target process/lwp stops or terminates
		 * or when the file descriptor becomes invalid.
		 */
		pnp->pr_flags |= PRPOLL;
		*phpp = php;
	}
	mutex_exit(&p->p_lock);
	return (0);
}

/* in prioctl.c */
extern	int	prioctl(struct vnode *, int, int, int, struct cred *, int *);

/*
 * /proc vnode operations vector
 */
struct vnodeops prvnodeops = {
	propen,
	prclose,
	prread,
	prwrite,
	prioctl,
	fs_setfl,
	prgetattr,
	fs_nosys,	/* setattr */
	praccess,
	prlookup,
	fs_nosys,	/* create */
	fs_nosys,	/* remove */
	fs_nosys,	/* link */
	fs_nosys,	/* rename */
	fs_nosys,	/* mkdir */
	fs_nosys,	/* rmdir */
	prreaddir,
	fs_nosys,	/* symlink */
	fs_nosys,	/* readlink */
	prfsync,
	prinactive,
	fs_nosys,	/* fid */
	fs_rwlock,
	fs_rwunlock,
	prseek,
	fs_cmp,
	fs_nosys,	/* frlock */
	fs_nosys,	/* space */
	fs_nosys,	/* realvp */
	fs_nosys,	/* getpage */
	fs_nosys,	/* putpage */
	fs_nosys_map,	/* map */
	fs_nosys_addmap, /* addmap */
	fs_nosys,	/* delmap */
	prpoll,
	fs_nosys,	/* dump */
	fs_pathconf,
	fs_nosys,	/* pageio */
	fs_nosys,	/* dumpctl */
	fs_nodispose,	/* dispose */
	fs_nosys,	/* setsecattr */
	fs_fab_acl	/* getsecattr */
};
