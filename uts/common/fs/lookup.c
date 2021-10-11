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
 * 		Copyright Notice
 *
 * Notice of copyright on this source code product does not indicate
 * publication.
 *
 * 	(c) 1986,1987,1988,1989  Sun Microsystems, Inc
 * 	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *		All rights reserved.
 *
 */

#ident	"@(#)lookup.c	1.29	95/10/20 SMI"	/* SVr4 1.18	*/

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/cpuvar.h>
#include <sys/errno.h>
#include <sys/cred.h>
#include <sys/user.h>
#include <sys/uio.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/pathname.h>
#include <sys/proc.h>
#include <sys/vtrace.h>
#include <sys/sysmacros.h>
#include <c2/audit.h>

/*
 * Very rarely are pathnames > 64 bytes, hence allocate space on
 * the stack for that rather then kmem_alloc it.
 */

#define	TYPICALMAXPATHLEN	64

/*
 * Lookup the user file name,
 * Handle allocation and freeing of pathname buffer, return error.
 */
int
lookupname(
	char *fnamep,			/* user pathname */
	enum uio_seg seg,		/* addr space that name is in */
	enum symfollow followlink,	/* follow sym links */
	vnode_t **dirvpp,		/* ret for ptr to parent dir vnode */
	vnode_t **compvpp)		/* ret for ptr to component vnode */
{
	char namebuf[TYPICALMAXPATHLEN + 4]; /* +4 because of bug 1170077 */
	struct pathname lookpn;
	int error;

	lookpn.pn_buf = namebuf;
	lookpn.pn_path = namebuf;
	lookpn.pn_pathlen = 0;
	lookpn.pn_bufsize = TYPICALMAXPATHLEN;

	if (seg == UIO_USERSPACE) {
		error = copyinstr(fnamep, namebuf,
		    TYPICALMAXPATHLEN, &lookpn.pn_pathlen);
	} else {
		error = copystr(fnamep, namebuf,
		    TYPICALMAXPATHLEN, &lookpn.pn_pathlen);
	}

	lookpn.pn_pathlen--; 		/* don't count the null byte */

	if (error == 0) {
		error = lookuppn(&lookpn, followlink, dirvpp, compvpp);
	}
	if (error == ENAMETOOLONG) {
		/*
		 * Wow! This thread used a pathname > TYPICALMAXPATHLEN bytes
		 * long! Do it the old way.
		 */
		if (error = pn_get(fnamep, seg, &lookpn))
			return (error);
		error = lookuppn(&lookpn, followlink, dirvpp, compvpp);
		pn_free(&lookpn);
	}

	return (error);
}

/*
 * Starting at current directory, translate pathname pnp to end.
 * Leave pathname of final component in pnp, return the vnode
 * for the final component in *compvpp, and return the vnode
 * for the parent of the final component in dirvpp.
 *
 * This is the central routine in pathname translation and handles
 * multiple components in pathnames, separating them at /'s.  It also
 * implements mounted file systems and processes symbolic links.
 */
int
lookuppn(
	struct pathname *pnp,		/* pathname to lookup */
	enum symfollow followlink,	/* (don't) follow sym links */
	vnode_t **dirvpp,		/* ptr for parent vnode */
	vnode_t **compvpp)		/* ptr for entry vnode */
{
	vnode_t *vp;	/* current directory vp */
	vnode_t *cvp;	/* current component vp */
	vnode_t *rootvp;
	vnode_t *tvp;		/* addressable temp ptr */
	char component[MAXNAMELEN];	/* buffer for component (incl null) */
	int error;
	int nlink;
	int lookup_flags;
	proc_t *pp = curproc;
	int lwptotal = pp->p_lwptotal;

	CPU_STAT_ADDQ(CPU, cpu_sysinfo.namei, 1);
	nlink = 0;
	cvp = NULL;
	lookup_flags = dirvpp ? LOOKUP_DIR : 0;

	if (pnp->pn_pathlen == 0)
		return (ENOENT);

	if (lwptotal > 1)
		mutex_enter(&pp->p_lock);	/* for u_rdir and u_cdir */

	if ((rootvp = PTOU(pp)->u_rdir) == NULL)
		rootvp = rootdir;
	else if (rootvp != rootdir)	/* no need to VN_HOLD rootdir */
		VN_HOLD(rootvp);

	/*
	 * If pathname starts with '/', then start search at root.
	 * Otherwise, start search at current directory.
	 */
	if (pnp->pn_path[0] == '/') {
		do {
			pnp->pn_path++;
			pnp->pn_pathlen--;
		} while (pnp->pn_path[0] == '/');
		vp = rootvp;
	} else {
		vp = PTOU(pp)->u_cdir;
	}

	VN_HOLD(vp);

	if (lwptotal > 1)
		mutex_exit(&pp->p_lock);

#ifdef C2_AUDIT
	if (audit_active)
		audit_anchorpath(pnp, vp == rootvp);
#endif

	/*
	 * Eliminate any trailing slashes in the pathname.
	 */
	pn_fixslash(pnp);

next:
	/*
	 * Make sure we have a directory.
	 */
	if (vp->v_type != VDIR) {
		error = ENOTDIR;
		goto bad;
	}

	/*
	 * Process the next component of the pathname.
	 */
	if (error = pn_getcomponent(pnp, component)) {
#ifdef C2_AUDIT
		if (audit_active)
			audit_addcomponent(pnp);
#endif
		goto bad;
	}

	/*
	 * Check for degenerate name (e.g. / or "")
	 * which is a way of talking about a directory,
	 * e.g. "/." or ".".
	 */
	if (component[0] == 0) {
		/*
		 * If the caller was interested in the parent then
		 * return an error since we don't have the real parent.
		 */
		if (dirvpp != NULL) {
#ifdef C2_AUDIT
			if (audit_active)	/* end of path */
				audit_savepath(pnp, vp, EINVAL);
#endif
			VN_RELE(vp);
			if (rootvp != rootdir)
				VN_RELE(rootvp);
			return (EINVAL);
		}
#ifdef C2_AUDIT
		if (audit_active)	/* end of path */
			audit_savepath(pnp, vp, 0);
#endif
		(void) pn_set(pnp, ".");
		if (compvpp != NULL)
			*compvpp = vp;
		else
			VN_RELE(vp);
		if (rootvp != rootdir)
			VN_RELE(rootvp);
		return (0);
	}

	/*
	 * Handle "..": two special cases.
	 * 1. If we're at the root directory (e.g. after chroot)
	 *    then ignore ".." so we can't get out of this subtree.
	 * 2. If this vnode is the root of a mounted file system,
	 *    then replace it with the vnode that was mounted on
	 *    so that we take the ".." in the other file system.
	 */
	if (component[0] == '.' && component[1] == '.' && component[2] == 0) {
checkforroot:
		if (VN_CMP(vp, rootvp) || VN_CMP(vp, rootdir)) {
			cvp = vp;
			VN_HOLD(cvp);
			goto skip;
		}
		if (vp->v_flag & VROOT) {
			cvp = vp;
			vp = vp->v_vfsp->vfs_vnodecovered;
			VN_HOLD(vp);
			VN_RELE(cvp);
			goto checkforroot;
		}
	}

	/*
	 * Perform a lookup in the current directory.
	 */
	error = VOP_LOOKUP(vp, component, &tvp, pnp, lookup_flags,
		rootvp, CRED());
	cvp = tvp;
	if (error) {
		cvp = NULL;
		/*
		 * On error, return hard error if
		 * (a) we're not at the end of the pathname yet, or
		 * (b) the caller didn't want the parent directory, or
		 * (c) we failed for some reason other than a missing entry.
		 */
		if (pn_pathleft(pnp) || dirvpp == NULL || error != ENOENT)
			goto bad;
#ifdef C2_AUDIT
		if (audit_active)		/* reached end of path */
			audit_savepath(pnp, vp, error);
#endif
		pn_setlast(pnp);
		*dirvpp = vp;
		if (compvpp != NULL)
			*compvpp = NULL;
		if (rootvp != rootdir)
			VN_RELE(rootvp);
		return (0);
	}

	/*
	 * Traverse mount points.
	 */
	if (cvp->v_vfsmountedhere != NULL) {
		tvp = cvp;
		if ((error = traverse(&tvp)) != 0) {
			/*
			 * It is required to assign cvp here, because
			 * traverse() will return a held vnode which
			 * may different than the vnode that was passed
			 * in (even in the error case).  If traverse()
			 * changes the vnode it releases the original,
			 * and holds the new one.
			 */
			cvp = tvp;
			goto bad;
		}
		cvp = tvp;
	}

	/*
	 * If we hit a symbolic link and there is more path to be
	 * translated or this operation does not wish to apply
	 * to a link, then place the contents of the link at the
	 * front of the remaining pathname.
	 */
	if (cvp->v_type == VLNK && (followlink == FOLLOW || pn_pathleft(pnp))) {
		struct pathname linkpath;

		if (++nlink > MAXSYMLINKS) {
			error = ELOOP;
			goto bad;
		}
		pn_alloc(&linkpath);
		if (error = pn_getsymlink(cvp, &linkpath, CRED())) {
			pn_free(&linkpath);
			goto bad;
		}

#ifdef C2_AUDIT
		if (audit_active)
			audit_symlink(pnp, &linkpath);
#endif /* C2_AUDIT */

		if (pn_pathleft(&linkpath) == 0)
			(void) pn_set(&linkpath, ".");
		error = pn_insert(pnp, &linkpath);	/* linkpath before pn */
		pn_free(&linkpath);
		if (error)
			goto bad;
		VN_RELE(cvp);
		cvp = NULL;
		if (pnp->pn_pathlen == 0) {
			error = ENOENT;
			goto bad;
		}
		if (pnp->pn_path[0] == '/') {
			do {
				pnp->pn_path++;
				pnp->pn_pathlen--;
			} while (pnp->pn_path[0] == '/');
			VN_RELE(vp);
			vp = rootvp;
			VN_HOLD(vp);
		}
#ifdef C2_AUDIT
		if (audit_active)
			audit_anchorpath(pnp, vp == rootvp);
#endif
		pn_fixslash(pnp);
		goto next;
	}

skip:
	/*
	 * If no more components, return last directory (if wanted) and
	 * last component (if wanted).
	 */
	if (pn_pathleft(pnp) == 0) {
#ifdef C2_AUDIT
		if (audit_active)		/* reached end of path */
			audit_savepath(pnp, cvp, 0);
#endif
		pn_setlast(pnp);
		if (dirvpp != NULL) {
			/*
			 * Check that we have the real parent and not
			 * an alias of the last component.
			 */
			if (VN_CMP(vp, cvp)) {
				VN_RELE(vp);
				VN_RELE(cvp);
				if (rootvp != rootdir)
					VN_RELE(rootvp);
				return (EINVAL);
			}
			*dirvpp = vp;
		} else
			VN_RELE(vp);
		if (compvpp != NULL)
			*compvpp = cvp;
		else
			VN_RELE(cvp);
		if (rootvp != rootdir)
			VN_RELE(rootvp);
		return (0);
	}

	/*
	 * Skip over slashes from end of last component.
	 */
	while (pnp->pn_path[0] == '/') {
		pnp->pn_path++;
		pnp->pn_pathlen--;
	}

	/*
	 * Searched through another level of directory:
	 * release previous directory handle and save new (result
	 * of lookup) as current directory.
	 */
	VN_RELE(vp);
	vp = cvp;
	cvp = NULL;
	goto next;

bad:
#ifdef C2_AUDIT
	if (audit_active)	/* reached end of path */
		audit_savepath(pnp, cvp, error);
#endif
	/*
	 * Error.  Release vnodes and return.
	 */
	if (cvp)
		VN_RELE(cvp);
	VN_RELE(vp);
	if (rootvp != rootdir)
		VN_RELE(rootvp);
	return (error);
}

/*
 * Traverse a mount point.  Routine accepts a vnode pointer as a reference
 * parameter and performs the indirection, releasing the original vnode.
 */
int
traverse(vnode_t **cvpp)
{
	int error = 0;
	vnode_t *cvp;
	vnode_t *tvp;

	cvp = *cvpp;
	mutex_enter(&cvp->v_lock);

	/*
	 * If this vnode is mounted on, then we transparently indirect
	 * to the vnode which is the root of the mounted file system.
	 * Before we do this we must check that an unmount is not in
	 * progress on this vnode.
	 */
	while (cvp->v_vfsmountedhere != NULL) {
		if (cvp->v_flag & VVFSLOCK) {
			cvp->v_flag |= VVFSWAIT;
			if (!cv_wait_sig(&cvp->v_cv, &cvp->v_lock)) {
				mutex_exit(&cvp->v_lock);
				/*
				 * BUG 1165736: lookuppn() expects a held
				 * vnode to be returned because it promptly
				 * calls VN_RELE after the error return
				 */
				*cvpp = cvp;
				return (EINTR);
			}
			continue;
		}
		/*
		 * I don't set VVFSLOCK because I am holding v_lock.  This
		 * will only work in VFS_ROOT never sleeps.  If it can sleep
		 * then I will need to set VVFSLOCK and exit v_lock.
		 */
		if (error = VFS_ROOT(cvp->v_vfsmountedhere, &tvp))
			break;
		mutex_exit(&cvp->v_lock);
		VN_RELE(cvp);
		cvp = tvp;
		mutex_enter(&cvp->v_lock);
	}

	mutex_exit(&cvp->v_lock);
	*cvpp = cvp;
	return (error);
}
