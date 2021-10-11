/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	Copyright (c) 1994 Sun Microsystems, Inc. */
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)fio.c	1.51	95/10/18 SMI"	/* SVr4.0 1.25	*/

#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/signal.h>
#include <sys/cred.h>
#include <sys/user.h>
#include <sys/conf.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/pathname.h>
#include <sys/file.h>
#include <sys/proc.h>
#include <sys/var.h>
#include <sys/cpuvar.h>
#include <sys/open.h>
#include <sys/cmn_err.h>
#include <sys/priocntl.h>
#include <sys/procset.h>
#include <sys/debug.h>
#include <sys/kmem.h>
#include <c2/audit.h>

struct kmem_cache *file_cache;

/*
 * Convert a user supplied file descriptor into a pointer to a file
 * structure.  Only task is to check range of the descriptor (soft
 * resource limit was enforced at open time and shouldn't be checked
 * here). The GETF macro will check if p_lwptotal is > 1 and call getf().
 * Only differences are that u_flock is locked and a refcnt is bumped.
 *
 * NOTE: GETF() and RELEASEF() must be used together and getf() and
 * releasef() must be used together so as to ensure the consistant
 * handling of the refcnt.
 */
struct file *
getf(fd)
	register int fd;
{
	register struct uf_entry *pufe;
	register struct file *fp;
	register struct user *up;

	up = PTOU(curproc);

	mutex_enter(&up->u_flock);

	if (fd < 0 || fd >= up->u_nofiles) {
		mutex_exit(&up->u_flock);
		return ((struct file *)NULL);
	}

	pufe = &up->u_flist[fd];
	if ((fp = pufe->uf_ofile) == NULLFP) {
		mutex_exit(&up->u_flock);
		return ((struct file *)NULL);
	}
	pufe->uf_refcnt++;
#ifdef C2_AUDIT
	/*
	 * archive per file audit data
	 */
	if (audit_active)
		audit_getf(fd);
#endif
	mutex_exit(&up->u_flock);

	return (fp);
}

/*
 * Same as getf except that file descriptor entry is reset as well.
 * We have to grab u_flock in order to reset the file descriptor entry.
 */
struct file *
getandset(fd)
	register int fd;
{
	register struct uf_entry *pufe;
	register struct file *fp;
	register struct user *up;
	register struct proc *p = curproc;

	up = PTOU(p);

	mutex_enter(&up->u_flock);

	if (fd < 0 || fd >= up->u_nofiles) {
		mutex_exit(&up->u_flock);
		return ((struct file *)NULL);
	}

	pufe = &up->u_flist[fd];
	if ((fp = pufe->uf_ofile) == NULLFP) {
		mutex_exit(&up->u_flock);
		return ((struct file *)NULL);
	}
#ifdef C2_AUDIT
	/*
	 * archive per file audit data
	 */
	if (audit_active)
		audit_getf(fd);
#endif
	pufe->uf_ofile = NULLFP;
	pufe->uf_pofile |= FRESERVED;	/* reserve until done - might sleep */

	/*
	 * if already closing then we're somehow doing two
	 * closes. Time to panic.
	 */
	ASSERT((pufe->uf_pofile & FCLOSING) == 0);

	/*
	 * wait for other threads to stop using this file structure. We
	 * tell em to kick us awake when the refcount goes to zero.
	 */
	while (pufe->uf_refcnt > 0) {
		pufe->uf_pofile |= FCLOSING;
		cv_wait(&p->p_flock, &up->u_flock);
		pufe = &up->u_flist[fd];	/* in case of array resized */
	}
	pufe->uf_pofile &= ~(FRESERVED|FCLOSING); /* free uf_entry for reuse */
	mutex_exit(&up->u_flock);

	return (fp);
}


/*
 * Decrement uf_refcnt. Single threaded case handled by RELEASEF macro.
 *
 * NOTE: GETF() and RELEASEF() must be used together and getf() and
 * releasef() must be used together so as to ensure the consistant
 * handling of the refcnt.
 */
void
releasef(int fd)
{
	register struct uf_entry *pufe;
	register struct user *up;
	register proc_t *p = curproc;

	up = PTOU(p);

	mutex_enter(&up->u_flock);

	pufe = &up->u_flist[fd];
	pufe->uf_refcnt--;

	ASSERT(pufe->uf_refcnt >= 0);

	if ((pufe->uf_pofile & FCLOSING) && (pufe->uf_refcnt == 0))
		cv_signal(&p->p_flock);

	mutex_exit(&up->u_flock);
}

/*
 * identical to releasef(), however, it can by called by any process.
 */
void
areleasef(int fd, proc_t *p)
{
	register struct uf_entry *pufe;
	register struct user *up;

	up = PTOU(p);

	mutex_enter(&up->u_flock);

	pufe = &up->u_flist[fd];
	pufe->uf_refcnt--;

	ASSERT(pufe->uf_refcnt >= 0);

	if ((pufe->uf_pofile & FCLOSING) && (pufe->uf_refcnt == 0))
		cv_signal(&p->p_flock);

	mutex_exit(&up->u_flock);
}

/*
 * Close all open file descriptors for the current process.
 * We have to grab u_flock in order to reset the file descriptor entries.
 */
void
closeall(flag)
	register int flag;
{
	register i;
	register file_t *fp;
	register struct uf_entry *ufp;
	register struct user *up;

	up = PTOU(curproc);
	mutex_enter(&up->u_flock);
	ufp = up->u_flist;
	for (i = 0; i < up->u_nofiles; i++, ufp++) {
		if ((fp = ufp->uf_ofile) != NULLFP) {
			if (flag)
				ufp->uf_ofile = NULLFP;
			/*
			 * Drop u_flock before calling closef to avoid
			 * a lock ordering problem with dofusers:
			 * close calls strclean that acquires pidlock.
			 * dofusers holds pidlock acquires u_flock.
			 * We are single-threaded here, so it is safe
			 * to drop u_flock (u_nofiles will not change).
			 */
			mutex_exit(&up->u_flock);
			(void) closef(fp);
			mutex_enter(&up->u_flock);
		}
	}
	mutex_exit(&up->u_flock);
}

/*
 * Internal form of close.  Decrement reference count on file
 * structure.  Decrement reference count on the vnode following
 * removal of the referencing file structure.
 */
int
closef(fp)
	register struct file *fp;
{
	register struct vnode *vp;
	register int error;
	int count;
	int flag;
	offset_t offset;

	/*
	 * Sanity check.
	 */
	ASSERT(fp != NULL);

#ifdef C2_AUDIT
	/*
	 * audit close of file (may be exit)
	 */
	if (audit_active)
		audit_closef(fp);
#endif

	mutex_enter(&fp->f_tlock);

	ASSERT(fp->f_count > 0);

	count = fp->f_count--;
	flag = fp->f_flag;
	offset = fp->f_offset;

	vp = fp->f_vnode;

	error = VOP_CLOSE(vp, flag, count, offset, fp->f_cred);

	if (count > 1) {
		mutex_exit(&fp->f_tlock);
		return (error);
	}
	ASSERT(fp->f_count == 0);
	mutex_exit(&fp->f_tlock);

	VN_RELE(vp);
#ifdef C2_AUDIT
	/*
	 * deallocate resources to audit_data
	 */
	if (audit_active)
		audit_unfalloc(fp);
#endif
	crfree(fp->f_cred);
	kmem_cache_free(file_cache, fp);
	return (error);
}


/*
 * Allocate a user file descriptor greater than or equal to "start" (supplied).
 * It marks the slot FRESERVED so that other threads won't grab the same
 * file descriptor.
 */
int
ufalloc(start, fdp)
	int start;
	int *fdp;
{
	register struct user *up = PTOU(curproc);
	register struct uf_entry *ufp;
	register int filelimit = up->u_rlimit[RLIMIT_NOFILE].rlim_cur;
	register int i;
	register int newsz;
	register int nofiles;

	i = start;
	mutex_enter(&up->u_flock);
	nofiles = up->u_nofiles;
	if (i < nofiles) {
		ufp = &up->u_flist[i];
		for (; i < nofiles; i++, ufp++) {
			if (i >= filelimit)	/* nofiles set to < nofiles */
				goto overlimit;
			if (ufp->uf_ofile == NULL &&
			    (ufp->uf_pofile & FRESERVED) != FRESERVED) {
				ufp->uf_pofile = FRESERVED;
				mutex_exit(&up->u_flock);
				*fdp = i;
				return (0);
			}
		}
	}

	if (i >= filelimit)
		goto overlimit;

	/*
	 * nofiles <= i and i < filelimit
	 *
	 * We need to allocate a bigger array of file descriptors.
	 * The u_flock lock is held in the case that kmem_zalloc sleeps.
	 * This prevents a second thread from bumping the size underneath us.
	 */
	newsz = nofiles + NFPCHUNK;
	if (newsz > filelimit)		/* use up rest of space */
		newsz = filelimit;
	else if (i >= newsz)		/* delta > chunk then use larger */
		newsz = i + 1;

	ufp = kmem_zalloc(newsz * sizeof (struct uf_entry), KM_SLEEP);

	/* copy old array into new one and then release the old one */
	bcopy((caddr_t)up->u_flist, (caddr_t)ufp,
		(size_t)(nofiles * sizeof (struct uf_entry)));
	kmem_free(up->u_flist, nofiles * sizeof (struct uf_entry));

	up->u_flist = ufp;
	up->u_nofiles = newsz;

	up->u_flist[i].uf_pofile = FRESERVED;

	mutex_exit(&up->u_flock);

	*fdp = i;
	return (0);

overlimit:
	mutex_exit(&up->u_flock);
	return (EMFILE);
}

/*
 * Allocate a user file descriptor and a file structure.
 * Initialize the descriptor to point at the file structure.
 * If fdp is NULL, the user file descriptor will not be allocated.
 *
 * file table overflow -- if there are no available file structures.
 */
int
falloc(vp, flag, fpp, fdp)
	struct vnode *vp;
	int flag;
	struct file **fpp;
	int *fdp;
{
	register struct file *fp;
	int fd;
	register int error;

	if (fdp) {
		if (error = ufalloc(0, &fd))
			return (error);
	}
	fp = kmem_cache_alloc(file_cache, KM_SLEEP);
	/*
	 * Note: falloc returns the fp locked
	 */
	mutex_enter(&fp->f_tlock);
	fp->f_count = 1;
	fp->f_flag = (ushort_t)flag;
	fp->f_vnode = vp;
	fp->f_offset = 0;
	fp->f_audit_data = 0;
	crhold(fp->f_cred = CRED());
#ifdef C2_AUDIT
	/*
	 * allocate resources to audit_data
	 */
	if (audit_active)
		audit_falloc(fp);
#endif
	*fpp = fp;
	if (fdp)
		*fdp = fd;
	return (0);
}

/*ARGSUSED*/
static void
file_cache_constructor(void *buf, size_t size)
{
	file_t *fp = buf;

	mutex_init(&fp->f_tlock, "short term fp lock", MUTEX_DEFAULT,
		DEFAULT_WT);
}

/*ARGSUSED*/
static void
file_cache_destructor(void *buf, size_t size)
{
	file_t *fp = buf;

	mutex_destroy(&fp->f_tlock);
}

void
finit()
{
	file_cache = kmem_cache_create("file_cache", sizeof (file_t), 0,
		file_cache_constructor, file_cache_destructor, NULL);
}

void
unfalloc(fp)
	register struct file *fp;
{
	ASSERT(MUTEX_HELD(&fp->f_tlock));
	if (--fp->f_count <= 0) {
#ifdef C2_AUDIT
		/*
		 * deallocate resources to audit_data
		 */
		if (audit_active)
			audit_unfalloc(fp);
#endif
		crfree(fp->f_cred);
		mutex_exit(&fp->f_tlock);
		kmem_cache_free(file_cache, fp);
	} else
		mutex_exit(&fp->f_tlock);
}

/*
 * Given a file descriptor, set the user's
 * file pointer to the given parameter.
 */
void
setf(fd, fp)
	int fd;
	struct file *fp;
{
	register struct uf_entry *ufp;

#ifdef C2_AUDIT
	if (audit_active)
		audit_setf(fp, fd);
#endif /* C2_AUDIT */

	mutex_enter(&u.u_flock);
	if (fd < 0 || fd >= u.u_nofiles)
		cmn_err(CE_PANIC, "fd < 0 || fd >= u.u_nofiles");
	ufp = &u.u_flist[fd];
	ufp->uf_ofile = fp;

	/*
	 * Clear the reserved bit since we don't need it any more.
	 */
	ufp->uf_pofile &= ~FRESERVED;
	mutex_exit(&u.u_flock);
}

/*
 * Given a file descriptor, return the user's file flags.
 */
char
getpof(fd)
	int fd;
{
	char flag;

	mutex_enter(&u.u_flock);
	if (fd >= u.u_nofiles) {
		mutex_exit(&u.u_flock);
		return (0);
	}
	flag = u.u_flist[fd].uf_pofile;
	mutex_exit(&u.u_flock);
	return (flag);
}

/*
 * Given a file descriptor and file flags,
 * set the user's file flags.
 */

void
#ifdef __STDC__
setpof(int fd, char flags)
#else
setpof(fd, flags)
	int fd;
	char flags;
#endif
{
	mutex_enter(&u.u_flock);
	if (fd < 0 || fd >= u.u_nofiles)
		cmn_err(CE_PANIC, "fd < 0 || fd >= u.u_nofiles");
	u.u_flist[fd].uf_pofile = flags;
	mutex_exit(&u.u_flock);
}

/*
 * Allocate a file descriptor and assign it to the vnode "*vpp",
 * performing the usual open protocol upon it and returning the
 * file descriptor allocated.  It is the responsibility of the
 * caller to dispose of "*vpp" if any error occurs.
 */
int
fassign(vpp, mode, fdp)
	struct vnode **vpp;
	int mode;
	int *fdp;
{
	struct file *fp;
	register int error;
	int fd;

	if (error = falloc((struct vnode *)NULL, mode & FMASK, &fp, &fd))
		return (error);
	if (error = VOP_OPEN(vpp, mode, fp->f_cred)) {
		setf(fd, NULLFP);
		unfalloc(fp);
		return (error);
	}
	fp->f_vnode = *vpp;
	mutex_exit(&fp->f_tlock);
	/*
	 * Fill in the slot falloc reserved.
	 */
	setf(fd, fp);
	*fdp = fd;
	return (0);
}

/*
 * This is called from fork to bump up the counts on all of the file pointers
 * since there is a new process pointing at them.  Since it is called
 * when there is only 1 lwp we don't need to hold the u_flock.  Fork
 * used to call getf for every fd.  This routine is much more efficient than
 * that and also reduces the amount of locking.
 */
void
bump_fcnts(p)
	struct proc *p;
{
	register int i;
	register struct uf_entry *ufp;
	struct file *fp;
	register struct user *up;

	up = PTOU(p);
	ufp = up->u_flist;
	for (i = 0; i < up->u_nofiles; i++, ufp++) {
		if ((fp = ufp->uf_ofile) != NULLFP) {
			mutex_enter(&fp->f_tlock);
			fp->f_count++;
			mutex_exit(&fp->f_tlock);
		}
	}
}

/*
 * This is called from exec to close all fd's that have the FCLOSEXEC flag
 * set.  We have to grab u_flock in order to reset the file descriptor
 * entries.  Exec * used to call getf for every fd.  As with bump_fcnts
 * above, this routine is much more efficient than that and also reduces
 * the amount of locking.
 */
void
close_exec(p)
	struct proc *p;
{
	register int i;
	register struct uf_entry *ufp;
	struct file *fp;
	register struct user *up;

	up = PTOU(p);
	mutex_enter(&up->u_flock);
	ufp = up->u_flist;
	for (i = 0; i < up->u_nofiles; i++, ufp++) {
		if ((ufp->uf_pofile & FCLOSEXEC) &&
		    (fp = ufp->uf_ofile) != NULLFP) {
			ufp->uf_ofile = NULLFP;
			mutex_exit(&up->u_flock);
			(void) closef(fp);
			mutex_enter(&up->u_flock);
		}
	}
	mutex_exit(&up->u_flock);
}


/*
 * Common routine for modifying attributes of named files.
 */
int
namesetattr(char *fnamep, enum symfollow followlink,
		struct vattr *vap, int flags)
{
	vnode_t *vp;
	register int error = 0;

	if (error = lookupname(fnamep, UIO_USERSPACE, followlink, NULLVPP, &vp))
		return (set_errno(error));
	if (vp->v_vfsp->vfs_flag & VFS_RDONLY)
		error = set_errno(EROFS);
	else if (error = VOP_SETATTR(vp, vap, flags, CRED()))
		(void) set_errno(error);
	VN_RELE(vp);
	return (error);
}

/*
 * Common routine for modifying attributes of files referenced
 * by descriptor.
 */
int
fdsetattr(int fd, struct vattr *vap)
{
	file_t *fp;
	register vnode_t *vp;
	register int error = 0;

	if ((fp = GETF(fd)) != NULL) {
		vp = fp->f_vnode;
		if (vp->v_vfsp->vfs_flag & VFS_RDONLY) {
			error = set_errno(EROFS);
		} else if (error = VOP_SETATTR(vp, vap, 0, CRED()))
			(void) set_errno(error);
		RELEASEF(fd);
	} else
		error = set_errno(EBADF);
	return (error);
}
