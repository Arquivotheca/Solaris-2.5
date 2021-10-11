/*
 *		PROPRIETARY NOTICE (Combined)
 *
 * This source code is unpublished proprietary information
 * constituting, or derived under license from AT&T's Unix(r) System V.
 *
 *
 *
 *		Copyright Notice
 *
 * Notice of copyright on this source code product does not indicate
 * publication.
 *
 *	(c) 1986-1992, 1994  Sun Microsystems, Inc.
 *	(c) 1983, 1984, 1985, 1986, 1987, 1988, 1989  AT&T.
 *		All rights reserved.
 */

#pragma ident	"@(#)nfs_acl_vnops.c	1.7	95/06/12 SMI"

#include <sys/param.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/cred.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/time.h>
#include <sys/vnode.h>
#include <sys/vfs.h>
#include <sys/file.h>
#include <sys/uio.h>
#include <sys/buf.h>
#include <sys/mman.h>
#include <sys/tiuser.h>
#include <sys/pathname.h>
#include <sys/dirent.h>
#include <sys/conf.h>
#include <sys/debug.h>
#include <sys/unistd.h>
#include <sys/vmsystm.h>
#include <sys/fcntl.h>
#include <sys/flock.h>
#include <sys/swap.h>
#include <sys/errno.h>
#include <sys/sysmacros.h>
#include <sys/disp.h>
#include <sys/kmem.h>
#include <sys/cmn_err.h>
#include <sys/vtrace.h>
#include <sys/pathconf.h>
#include <sys/dnlc.h>
#include <sys/acl.h>

#include <rpc/types.h>
#include <rpc/auth.h>
#include <rpc/clnt.h>
#include <rpc/xdr.h>
#include <nfs/nfs.h>
#include <nfs/nfs_clnt.h>
#include <nfs/rnode.h>
#include <nfs/nfs_acl.h>

#include <vm/hat.h>
#include <vm/as.h>
#include <vm/page.h>
#include <vm/pvn.h>
#include <vm/seg.h>
#include <vm/seg_map.h>
#include <vm/seg_kmem.h>
#include <vm/seg_vn.h>
#include <vm/rm.h>

#include <fs/fs_subr.h>

static kstat_named_t aclreqcnt_v2[] = {
	{ "null",	KSTAT_DATA_ULONG },
	{ "getacl",	KSTAT_DATA_ULONG },
	{ "setacl",	KSTAT_DATA_ULONG },
	{ "getattr",	KSTAT_DATA_ULONG },
	{ "access",	KSTAT_DATA_ULONG }
};
kstat_named_t *aclreqcnt_v2_ptr = aclreqcnt_v2;
ulong_t aclreqcnt_v2_ndata = sizeof (aclreqcnt_v2) / sizeof (kstat_named_t);

char *aclnames_v2[] = {
	"null", "getacl", "setacl", "getattr", "access"
};

/*
 * This table maps from NFS protocol number into call type.
 * Zero means a "Lookup" type call
 * One  means a "Read" type call
 * Two  means a "Write" type call
 * This is used to select a default time-out.
 */
char acl_call_type_v2[] = {
	0, 0, 1, 0, 0
};

/*
 * Similar table, but to determine which timer to use
 * (only real reads and writes!)
 */
char acl_timer_type_v2[] = {
	0, 0, 0, 0, 0
};

#define	ISVDEV(t) ((t == VBLK) || (t == VCHR) || (t == VFIFO))

static int nfs_acl_cache = 0;
static int nfs_acl_dup_cache(vsecattr_t *, vsecattr_t *);
static void nfs_acl_dup_res(rnode_t *, vsecattr_t *);

/* ARGSUSED */
int
acl_getacl2(vnode_t *vp, vsecattr_t *vsp, int flag, cred_t *cr)
{
	int error;
	GETACL2args args;
	GETACL2res res;
	int douprintf;
	vattr_t va;
	rnode_t *rp;
	long seq;

	rp = VTOR(vp);
	if (rp->r_secattr != NULL) {
		error = nfs_validate_caches(vp, cr);
		if (error)
			return (error);
		mutex_enter(&rp->r_statelock);
		if (rp->r_secattr != NULL) {
			if (nfs_acl_dup_cache(vsp, rp->r_secattr)) {
				mutex_exit(&rp->r_statelock);
				return (0);
			}
		}
		mutex_exit(&rp->r_statelock);
	}

	args.fh = *VTOFH(vp);
	args.mask = vsp->vsa_mask;

	res.resok.acl.vsa_aclentp = NULL;
	res.resok.acl.vsa_dfaclentp = NULL;

	douprintf = 1;
	error = acl2call(VTOMI(vp), ACLPROC2_GETACL,
			xdr_GETACL2args, (caddr_t)&args,
			xdr_GETACL2res, (caddr_t)&res,
			cr, &douprintf, &res.status);

	if (error)
		return (error);

	error = geterrno(res.status);
	if (!error) {
		nattr_to_vattr(vp, &res.resok.attr, &va);
		nfs_cache_check(vp, va.va_ctime, va.va_mtime, va.va_size,
				&seq, cr);
		nfs_attrcache_va(vp, &va, seq);
		if (nfs_acl_cache)
			nfs_acl_dup_res(rp, &res.resok.acl);
		*vsp = res.resok.acl;
	} else {
		PURGE_STALE_FH(error, vp, cr);
	}

	return (error);
}

/* ARGSUSED */
int
acl_setacl2(vnode_t *vp, vsecattr_t *vsp, int flag, cred_t *cr)
{
	int error;
	SETACL2args args;
	SETACL2res res;
	int douprintf;
	vattr_t va;
	long seq;

	args.fh = *VTOFH(vp);
	args.acl = *vsp;

	douprintf = 1;
	error = acl2call(VTOMI(vp), ACLPROC2_SETACL,
			xdr_SETACL2args, (caddr_t)&args,
			xdr_SETACL2res, (caddr_t)&res,
			cr, &douprintf, &res.status);

	if (error)
		return (error);

	error = geterrno(res.status);
	if (!error) {
		nattr_to_vattr(vp, &res.resok.attr, &va);
		nfs_cache_check(vp, va.va_ctime, va.va_mtime, va.va_size,
				&seq, cr);
		nfs_attrcache_va(vp, &va, seq);
		if (nfs_acl_cache) {
			vsp->vsa_mask |= VSA_ACL | VSA_DFACL;
			nfs_acl_dup_res(VTOR(vp), vsp);
		}
	} else {
		PURGE_STALE_FH(error, vp, cr);
	}

	return (error);
}

/* ARGSUSED */
int
acl_getattr2_otw(vnode_t *vp, vattr_t *vap, cred_t *cr)
{
	int error;
	GETATTR2args args;
	GETATTR2res res;
	int douprintf;
	long seq;

	args.fh = *VTOFH(vp);

	douprintf = 1;
	error = acl2call(VTOMI(vp), ACLPROC2_GETATTR,
			xdr_GETATTR2args, (caddr_t)&args,
			xdr_GETATTR2res, (caddr_t)&res,
			cr, &douprintf, &res.status);

	if (error)
		return (error);

	error = geterrno(res.status);
	if (!error) {
		nattr_to_vattr(vp, &res.resok.attr, vap);
		nfs_cache_check(vp, vap->va_ctime, vap->va_mtime,
				vap->va_size, &seq, cr);
		nfs_attrcache_va(vp, vap, seq);
	} else {
		PURGE_STALE_FH(error, vp, cr);
	}

	return (error);
}

#ifdef DEBUG
static int acl_access2_cache_hits = 0;
static int acl_access2_cache_misses = 0;
#endif

/* ARGSUSED */
int
acl_access2(vnode_t *vp, int mode, int flags, cred_t *cr)
{
	int error;
	ACCESS2args args;
	ACCESS2res res;
	int douprintf;
	access_cache *acp;
	uint32 acc;
	rnode_t *rp;
	cred_t *cred;
	vattr_t va;
	long seq;

	acc = 0;
	if (mode & VREAD)
		acc |= ACCESS2_READ;
	if (mode & VWRITE) {
		if ((vp->v_vfsp->vfs_flag & VFS_RDONLY) && !ISVDEV(vp->v_type))
			return (EROFS);
		if (vp->v_type == VDIR)
			acc |= ACCESS2_DELETE;
		acc |= ACCESS2_MODIFY | ACCESS2_EXTEND;
	}
	if (mode & VEXEC) {
		if (vp->v_type == VDIR)
			acc |= ACCESS2_LOOKUP;
		else
			acc |= ACCESS2_EXECUTE;
	}

	rp = VTOR(vp);
	if (rp->r_acc != NULL) {
		error = nfs_validate_caches(vp, cr);
		if (error)
			return (error);
	}

	mutex_enter(&rp->r_statelock);
	for (acp = rp->r_acc; acp != NULL; acp = acp->next) {
		/*
		 * Look for an entry by comparing credentials.
		 */
		if (crcmp(acp->cred, cr) == 0) {
#ifdef DEBUG
			acl_access2_cache_hits++;
#endif
			if ((acp->known & acc) == acc) {
				if ((acp->allowed & acc) == acc) {
					mutex_exit(&rp->r_statelock);
					return (0);
				}
				mutex_exit(&rp->r_statelock);
				return (EACCES);
			}
			break;
		}
	}
	mutex_exit(&rp->r_statelock);

#ifdef DEBUG
	acl_access2_cache_misses++;
#endif

	args.fh = *VTOFH(vp);
	args.access = acc;

	cred = cr;
tryagain:
	douprintf = 1;
	error = acl2call(VTOMI(vp), ACLPROC2_ACCESS,
			xdr_ACCESS2args, (caddr_t)&args,
			xdr_ACCESS2res, (caddr_t)&res,
			cred, &douprintf, &res.status);

	if (error) {
		if (cred != cr)
			crfree(cred);
		return (error);
	}

	error = geterrno(res.status);
	if (!error) {
		nattr_to_vattr(vp, &res.resok.attr, &va);
		nfs_cache_check(vp, va.va_ctime, va.va_mtime, va.va_size,
				&seq, cr);
		nfs_attrcache_va(vp, &va, seq);
		if (args.access != res.resok.access) {
			/*
			 * The following code implements the semantic that
			 * a setuid root program has *at least* the
			 * permissions of the user that is running the
			 * program.  See acl2call() for more portions
			 * of the implementation of this functionality.
			 */
			if (cred->cr_uid == 0 && cred->cr_ruid != 0) {
				cred = crdup(cr);
				cred->cr_uid = cred->cr_ruid;
				goto tryagain;
			}
			error = EACCES;
		}
		mutex_enter(&rp->r_statelock);
		for (acp = rp->r_acc; acp != NULL; acp = acp->next) {
			/*
			 * Look for an entry by comparing credentials.
			 */
			if (crcmp(acp->cred, cr) == 0)
				break;
		}
		if (acp != NULL) {
			acp->known |= args.access;
			acp->allowed &= ~args.access;
			acp->allowed |= res.resok.access;
		} else {
#ifdef DEBUG
			acp = access_cache_alloc(sizeof (*acp), KM_NOSLEEP);
#else
			acp = (access_cache *) kmem_alloc(sizeof (*acp),
							KM_NOSLEEP);
#endif
			if (acp != NULL) {
				acp->next = rp->r_acc;
				rp->r_acc = acp;
				crhold(cr);
				acp->cred = cr;
				acp->known = args.access;
				acp->allowed = res.resok.access;
			}
		}
		mutex_exit(&rp->r_statelock);
	} else {
		PURGE_STALE_FH(error, vp, cr);
	}

	if (cred != cr)
		crfree(cred);

	return (error);
}

static kstat_named_t aclreqcnt_v3[] = {
	{ "null",	KSTAT_DATA_ULONG },
	{ "getacl",	KSTAT_DATA_ULONG },
	{ "setacl",	KSTAT_DATA_ULONG }
};
kstat_named_t *aclreqcnt_v3_ptr = aclreqcnt_v3;
ulong_t aclreqcnt_v3_ndata = sizeof (aclreqcnt_v3) / sizeof (kstat_named_t);

char *aclnames_v3[] = {
	"null", "getacl", "setacl"
};

/*
 * This table maps from NFS protocol number into call type.
 * Zero means a "Lookup" type call
 * One  means a "Read" type call
 * Two  means a "Write" type call
 * This is used to select a default time-out.
 */
char acl_call_type_v3[] = {
	0, 0, 1
};

/*
 * Similar table, but to determine which timer to use
 * (only real reads and writes!)
 */
char acl_timer_type_v3[] = {
	0, 0, 0
};

/* ARGSUSED */
int
acl_getacl3(vnode_t *vp, vsecattr_t *vsp, int flag, cred_t *cr)
{
	int error;
	GETACL3args args;
	GETACL3res res;
	int douprintf;
	rnode_t *rp;

	rp = VTOR(vp);
	if (rp->r_secattr != NULL) {
		error = nfs3_validate_caches(vp, cr);
		if (error)
			return (error);
		mutex_enter(&rp->r_statelock);
		if (rp->r_secattr != NULL) {
			if (nfs_acl_dup_cache(vsp, rp->r_secattr)) {
				mutex_exit(&rp->r_statelock);
				return (0);
			}
		}
		mutex_exit(&rp->r_statelock);
	}

	args.fh = *VTOFH3(vp);
	args.mask = vsp->vsa_mask;

	res.resok.acl.vsa_aclentp = NULL;
	res.resok.acl.vsa_dfaclentp = NULL;

	douprintf = 1;
	error = acl3call(VTOMI(vp), ACLPROC3_GETACL,
			xdr_GETACL3args, (caddr_t)&args,
			xdr_GETACL3res, (caddr_t)&res,
			cr, &douprintf, &res.status);

	if (error)
		return (error);

	error = geterrno3(res.status);

	if (!error) {
		nfs3_cache_post_op_attr(vp, &res.resok.attr, cr);
		if (nfs_acl_cache)
			nfs_acl_dup_res(rp, &res.resok.acl);
		*vsp = res.resok.acl;
	} else {
		nfs3_cache_post_op_attr(vp, &res.resfail.attr, cr);
		PURGE_STALE_FH(error, vp, cr);
	}

	return (error);
}

/* ARGSUSED */
int
acl_setacl3(vnode_t *vp, vsecattr_t *vsp, int flag, cred_t *cr)
{
	int error;
	SETACL3args args;
	SETACL3res res;
	int douprintf;

	args.fh = *VTOFH3(vp);
	args.acl = *vsp;

	douprintf = 1;
	error = acl3call(VTOMI(vp), ACLPROC3_SETACL,
			xdr_SETACL3args, (caddr_t)&args,
			xdr_SETACL3res, (caddr_t)&res,
			cr, &douprintf, &res.status);

	if (error)
		return (error);

	error = geterrno3(res.status);
	if (!error) {
		nfs3_cache_post_op_attr(vp, &res.resok.attr, cr);
		if (nfs_acl_cache) {
			vsp->vsa_mask |= VSA_ACL | VSA_DFACL;
			nfs_acl_dup_res(VTOR(vp), vsp);
		}
	} else {
		nfs3_cache_post_op_attr(vp, &res.resfail.attr, cr);
		PURGE_STALE_FH(error, vp, cr);
	}

	return (error);
}

void
nfs_acl_free(vsecattr_t *vsp)
{

	if (vsp->vsa_aclentp != NULL) {
		kmem_free((caddr_t)vsp->vsa_aclentp, vsp->vsa_aclcnt *
					    sizeof (aclent_t));
	}
	if (vsp->vsa_dfaclentp != NULL) {
		kmem_free((caddr_t)vsp->vsa_dfaclentp, vsp->vsa_dfaclcnt *
					    sizeof (aclent_t));
	}
	kmem_free((caddr_t)vsp, sizeof (*vsp));
}

static int
nfs_acl_dup_cache(vsecattr_t *vsp, vsecattr_t *rvsp)
{
	size_t aclsize;

	if ((rvsp->vsa_mask & vsp->vsa_mask) != vsp->vsa_mask)
		return (0);

	if (vsp->vsa_mask & VSA_ACL) {
		ASSERT(rvsp->vsa_mask & VSA_ACLCNT);
		aclsize = rvsp->vsa_aclcnt * sizeof (aclent_t);
		vsp->vsa_aclentp = kmem_alloc(aclsize, KM_SLEEP);
		bcopy((caddr_t)rvsp->vsa_aclentp, (caddr_t)vsp->vsa_aclentp,
		    aclsize);
	}
	if (vsp->vsa_mask & VSA_ACLCNT)
		vsp->vsa_aclcnt = rvsp->vsa_aclcnt;
	if (vsp->vsa_mask & VSA_DFACL) {
		ASSERT(rvsp->vsa_mask & VSA_DFACLCNT);
		aclsize = rvsp->vsa_dfaclcnt * sizeof (aclent_t);
		vsp->vsa_dfaclentp = kmem_alloc(aclsize, KM_SLEEP);
		bcopy((caddr_t)rvsp->vsa_dfaclentp, (caddr_t)vsp->vsa_dfaclentp,
		    aclsize);
	}
	if (vsp->vsa_mask & VSA_DFACLCNT)
		vsp->vsa_dfaclcnt = rvsp->vsa_dfaclcnt;

	return (1);
}

static void
nfs_acl_dup_res(rnode_t *rp, vsecattr_t *vsp)
{
	size_t aclsize;
	vsecattr_t *rvsp;

	mutex_enter(&rp->r_statelock);
	if (rp->r_secattr != NULL)
		rvsp = rp->r_secattr;
	else {
		rvsp = (vsecattr_t *)kmem_zalloc(sizeof (*rvsp), KM_NOSLEEP);
		if (rvsp == NULL) {
			mutex_exit(&rp->r_statelock);
			return;
		}
		rp->r_secattr = rvsp;
	}

	if (vsp->vsa_mask & VSA_ACL) {
		if (rvsp->vsa_aclentp != NULL &&
		    rvsp->vsa_aclcnt != vsp->vsa_aclcnt) {
			aclsize = rvsp->vsa_aclcnt * sizeof (aclent_t);
			kmem_free((caddr_t)rvsp->vsa_aclentp, aclsize);
			rvsp->vsa_aclentp = NULL;
		}
		if (vsp->vsa_aclcnt != 0) {
			aclsize = vsp->vsa_aclcnt * sizeof (aclent_t);
			if (rvsp->vsa_aclentp == NULL) {
				rvsp->vsa_aclentp = kmem_alloc(aclsize,
								KM_SLEEP);
			}
			bcopy((caddr_t)vsp->vsa_aclentp,
			    (caddr_t)rvsp->vsa_aclentp, aclsize);
		}
		rvsp->vsa_aclcnt = vsp->vsa_aclcnt;
		rvsp->vsa_mask |= VSA_ACL | VSA_ACLCNT;
	}
	if (vsp->vsa_mask & VSA_ACLCNT) {
		if (rvsp->vsa_aclentp != NULL &&
		    rvsp->vsa_aclcnt != vsp->vsa_aclcnt) {
			aclsize = rvsp->vsa_aclcnt * sizeof (aclent_t);
			kmem_free((caddr_t)rvsp->vsa_aclentp, aclsize);
			rvsp->vsa_aclentp = NULL;
			rvsp->vsa_mask &= ~VSA_ACL;
		}
		rvsp->vsa_aclcnt = vsp->vsa_aclcnt;
		rvsp->vsa_mask |= VSA_ACLCNT;
	}
	if (vsp->vsa_mask & VSA_DFACL) {
		if (rvsp->vsa_dfaclentp != NULL &&
		    rvsp->vsa_dfaclcnt != vsp->vsa_dfaclcnt) {
			aclsize = rvsp->vsa_dfaclcnt * sizeof (aclent_t);
			kmem_free((caddr_t)rvsp->vsa_dfaclentp, aclsize);
			rvsp->vsa_dfaclentp = NULL;
		}
		if (vsp->vsa_dfaclcnt != 0) {
			aclsize = vsp->vsa_dfaclcnt * sizeof (aclent_t);
			if (rvsp->vsa_dfaclentp == NULL) {
				rvsp->vsa_dfaclentp = kmem_alloc(aclsize,
								KM_SLEEP);
			}
			bcopy((caddr_t)vsp->vsa_dfaclentp,
			    (caddr_t)rvsp->vsa_dfaclentp, aclsize);
		}
		rvsp->vsa_dfaclcnt = vsp->vsa_dfaclcnt;
		rvsp->vsa_mask |= VSA_DFACL | VSA_DFACLCNT;
	}
	if (vsp->vsa_mask & VSA_DFACLCNT) {
		if (rvsp->vsa_dfaclentp != NULL &&
		    rvsp->vsa_dfaclcnt != vsp->vsa_dfaclcnt) {
			aclsize = rvsp->vsa_dfaclcnt * sizeof (aclent_t);
			kmem_free((caddr_t)rvsp->vsa_dfaclentp, aclsize);
			rvsp->vsa_dfaclentp = NULL;
			rvsp->vsa_mask &= ~VSA_DFACL;
		}
		rvsp->vsa_dfaclcnt = vsp->vsa_dfaclcnt;
		rvsp->vsa_mask |= VSA_DFACLCNT;
	}
	mutex_exit(&rp->r_statelock);
}
