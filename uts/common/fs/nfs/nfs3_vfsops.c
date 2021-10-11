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
 *	Copyright (c) 1986-1991,1994,1995 by Sun Microsystems, Inc.
 *	Copyright (c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *	All rights reserved.
 */

#pragma ident	"@(#)nfs3_vfsops.c	1.51	95/10/19 SMI"
/* SVr4.0 1.16 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/cred.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/pathname.h>
#include <sys/sysmacros.h>
#include <sys/kmem.h>
#include <sys/kstat.h>
#include <sys/mkdev.h>
#include <sys/mount.h>
#include <sys/statvfs.h>
#include <sys/errno.h>
#include <sys/debug.h>
#include <sys/cmn_err.h>
#include <sys/utsname.h>
#include <sys/bootconf.h>
#include <sys/modctl.h>
#include <sys/acl.h>
#include <sys/flock.h>

#include <rpc/types.h>
#include <rpc/auth.h>
#include <rpc/clnt.h>

#include <nfs/nfs.h>
#include <nfs/nfs_clnt.h>
#include <nfs/rnode.h>
#include <nfs/mount.h>
#include <nfs/nfs_acl.h>

#include <fs/fs_subr.h>

static kstat_named_t rfsreqcnt_v3[] = {
	{ "null",	KSTAT_DATA_ULONG },
	{ "getattr",	KSTAT_DATA_ULONG },
	{ "setattr",	KSTAT_DATA_ULONG },
	{ "lookup",	KSTAT_DATA_ULONG },
	{ "access",	KSTAT_DATA_ULONG },
	{ "readlink",	KSTAT_DATA_ULONG },
	{ "read",	KSTAT_DATA_ULONG },
	{ "write",	KSTAT_DATA_ULONG },
	{ "create",	KSTAT_DATA_ULONG },
	{ "mkdir",	KSTAT_DATA_ULONG },
	{ "symlink",	KSTAT_DATA_ULONG },
	{ "mknod",	KSTAT_DATA_ULONG },
	{ "remove",	KSTAT_DATA_ULONG },
	{ "rmdir",	KSTAT_DATA_ULONG },
	{ "rename",	KSTAT_DATA_ULONG },
	{ "link",	KSTAT_DATA_ULONG },
	{ "readdir",	KSTAT_DATA_ULONG },
	{ "readdir+",	KSTAT_DATA_ULONG },
	{ "fsstat",	KSTAT_DATA_ULONG },
	{ "fsinfo",	KSTAT_DATA_ULONG },
	{ "pathconf",	KSTAT_DATA_ULONG },
	{ "commit",	KSTAT_DATA_ULONG }
};
static kstat_named_t *rfsreqcnt_v3_ptr = rfsreqcnt_v3;
static ulong_t rfsreqcnt_v3_ndata = sizeof (rfsreqcnt_v3) /
					sizeof (kstat_named_t);

static char *rfsnames_v3[] = {
	"null", "getattr", "setattr", "lookup", "access", "readlink", "read",
	"write", "create", "mkdir", "symlink", "mknod", "remove", "rmdir",
	"rename", "link", "readdir", "readdir+", "fsstat", "fsinfo",
	"pathconf", "commit"
};

/*
 * This table maps from NFS protocol number into call type.
 * Zero means a "Lookup" type call
 * One  means a "Read" type call
 * Two  means a "Write" type call
 * This is used to select a default time-out.
 */
static char call_type_v3[] = {
	0, 0, 1, 0, 0, 0, 1,
	2, 2, 2, 2, 2, 2, 2,
	2, 2, 1, 2, 0, 0, 0,
	2 };

/*
 * Similar table, but to determine which timer to use
 * (only real reads and writes!)
 */
static char timer_type_v3[] = {
	0, 0, 0, 0, 0, 0, 1,
	2, 0, 0, 0, 0, 0, 0,
	0, 0, 1, 1, 0, 0, 0,
	0 };

/*
 * nfs3 vfs operations.
 */
static	int	nfs3_mount(vfs_t *, vnode_t *, struct mounta *, cred_t *);
static	int	nfs3_unmount(vfs_t *, cred_t *);
static	int	nfs3_root(vfs_t *, vnode_t **);
static	int	nfs3_statvfs(vfs_t *, struct statvfs *);
static	int	nfs3_sync(vfs_t *, short, cred_t *);
static	int	nfs3_vget(vfs_t *, vnode_t **, fid_t *);
static	int	nfs3_mountroot(vfs_t *, whymountroot_t);

struct vfsops nfs3_vfsops = {
	nfs3_mount,
	nfs3_unmount,
	nfs3_root,
	nfs3_statvfs,
	nfs3_sync,
	nfs3_vget,
	nfs3_mountroot,
	fs_nosys
};

vnode_t nfs3_notfound;

/*
 * Initialize the vfs structure
 */

static int nfs3fstyp;

int
nfs3init(struct vfssw *vswp, int fstyp)
{
	vnode_t *vp;
	kstat_t *rfsproccnt_v3_kstat;
	kstat_t *rfsreqcnt_v3_kstat;
	kstat_t *aclproccnt_v3_kstat;
	kstat_t *aclreqcnt_v3_kstat;

	vswp->vsw_vfsops = &nfs3_vfsops;
	nfs3fstyp = fstyp;

	mutex_enter(&nfs_kstat_lock);
	if (nfs_client_kstat == NULL) {
		if ((nfs_client_kstat = kstat_create("nfs", 0, "nfs_client",
		    "misc", KSTAT_TYPE_NAMED, clstat_ndata,
		    KSTAT_FLAG_VIRTUAL | KSTAT_FLAG_WRITABLE)) != NULL) {
			nfs_client_kstat->ks_data = (void *)clstat_ptr;
			kstat_install(nfs_client_kstat);
		}
	}

	if (nfs_server_kstat == NULL) {
		if ((nfs_server_kstat = kstat_create("nfs", 0, "nfs_server",
		    "misc", KSTAT_TYPE_NAMED, svstat_ndata,
		    KSTAT_FLAG_VIRTUAL | KSTAT_FLAG_WRITABLE)) != NULL) {
			nfs_server_kstat->ks_data = (void *)svstat_ptr;
			kstat_install(nfs_server_kstat);
		}
	}
	mutex_exit(&nfs_kstat_lock);

	if ((rfsproccnt_v3_kstat = kstat_create("nfs", 0, "rfsproccnt_v3",
	    "misc", KSTAT_TYPE_NAMED, rfsproccnt_v3_ndata,
	    KSTAT_FLAG_VIRTUAL | KSTAT_FLAG_WRITABLE)) != NULL) {
		rfsproccnt_v3_kstat->ks_data = (void *)rfsproccnt_v3_ptr;
		kstat_install(rfsproccnt_v3_kstat);
	}
	if ((rfsreqcnt_v3_kstat = kstat_create("nfs", 0, "rfsreqcnt_v3",
	    "misc", KSTAT_TYPE_NAMED, rfsreqcnt_v3_ndata,
	    KSTAT_FLAG_VIRTUAL | KSTAT_FLAG_WRITABLE)) != NULL) {
		rfsreqcnt_v3_kstat->ks_data = (void *)rfsreqcnt_v3_ptr;
		kstat_install(rfsreqcnt_v3_kstat);
	}
	if ((aclproccnt_v3_kstat = kstat_create("nfs_acl", 0, "aclproccnt_v3",
	    "misc", KSTAT_TYPE_NAMED, aclproccnt_v3_ndata,
	    KSTAT_FLAG_VIRTUAL | KSTAT_FLAG_WRITABLE)) != NULL) {
		aclproccnt_v3_kstat->ks_data = (void *)aclproccnt_v3_ptr;
		kstat_install(aclproccnt_v3_kstat);
	}
	if ((aclreqcnt_v3_kstat = kstat_create("nfs_acl", 0, "aclreqcnt_v3",
	    "misc", KSTAT_TYPE_NAMED, aclreqcnt_v3_ndata,
	    KSTAT_FLAG_VIRTUAL | KSTAT_FLAG_WRITABLE)) != NULL) {
		aclreqcnt_v3_kstat->ks_data = (void *)aclreqcnt_v3_ptr;
		kstat_install(aclreqcnt_v3_kstat);
	}

	vp = &nfs3_notfound;
	bzero((caddr_t)vp, sizeof (*vp));
	mutex_init(&vp->v_lock, "rnode v_lock", MUTEX_DEFAULT, DEFAULT_WT);
	cv_init(&vp->v_cv, "rnode v_cv", CV_DEFAULT, NULL);
	vp->v_count = 1;
	vp->v_op = &nfs3_vnodeops;

	return (0);
}

/*
 * nfs mount vfsop
 * Set up mount info record and attach it to vfs struct.
 */
static int
nfs3_mount(vfs_t *vfsp, vnode_t *mvp, struct mounta *uap, cred_t *cr)
{
	char *data = uap->dataptr;
	int datalen = uap->datalen;
	int error;
	vnode_t *rtvp;			/* the server's root */
	mntinfo_t *mi;			/* mount info, pointed at by vfs */
	nfs_fh3 fh;			/* root fhandle */
	struct nfs_args args;		/* nfs mount arguments */
	struct netbuf addr;		/* server's address */
	int hlen;			/* length of hostname */
	char shostname[HOSTNAMESZ];	/* server's hostname */
	int nlen;			/* length of netname */
	char netname[MAXNETNAMELEN+1];	/* server's netname */
	struct netbuf syncaddr;		/* AUTH_DES time sync addr */
	struct knetconfig *knconf;	/* transport knetconfig structure */
	rnode_t *rp;

	if (!suser(cr))
		return (EPERM);

	if (mvp->v_type != VDIR)
		return (ENOTDIR);

	/*
	 * get arguments
	 */
	if (datalen != sizeof (args))
		return (EINVAL);

	if (copyin(data, (caddr_t)&args, sizeof (args)))
		return (EFAULT);

	/*
	 * For now, only check the llock flag on remount.  If there are
	 * currently locks set and the request changes the locking type,
	 * disallow the remount, because it's questionable whether we can
	 * transfer the locking state correctly.
	 */
	if (uap->flags & MS_REMOUNT) {
		if ((mi = VFTOMI(vfsp)) != NULL) {
			u_int new_mi_llock;
			u_int old_mi_llock;

			new_mi_llock = (args.flags & NFSMNT_LLOCK) ? 1 : 0;
			mutex_enter(&mi->mi_lock);
			old_mi_llock = (mi->mi_flags & MI_LLOCK) ? 1 : 0;
			if (old_mi_llock != new_mi_llock) {
				if (flk_vfs_has_locks(vfsp)) {
					mutex_exit(&mi->mi_lock);
					return (EBUSY);
				}
				if (new_mi_llock)
					mi->mi_flags |= MI_LLOCK;
				else
					mi->mi_flags &= ~MI_LLOCK;
			}
			mutex_exit(&mi->mi_lock);
		}
		return (0);
	}

	mutex_enter(&mvp->v_lock);
	if (!(uap->flags & MS_OVERLAY) &&
	    (mvp->v_count != 1 || (mvp->v_flag & VROOT))) {
		mutex_exit(&mvp->v_lock);
		return (EBUSY);
	}
	mutex_exit(&mvp->v_lock);

	/* make sure things are zeroed for errout: */
	rtvp = NULL;
	mi = NULL;
	addr.buf = NULL;
	syncaddr.buf = NULL;

	/*
	 * A valid knetconfig structure is required.
	 */
	if (!(args.flags & NFSMNT_KNCONF))
		return (EINVAL);

	/*
	 * Allocate space for a knetconfig structure and
	 * its strings and copy in from user-land.
	 */
	knconf = (struct knetconfig *) kmem_alloc(sizeof (*knconf), KM_SLEEP);
	if (copyin((caddr_t)args.knconf, (caddr_t)knconf, sizeof (*knconf))) {
		kmem_free((caddr_t)knconf, sizeof (*knconf));
		return (EFAULT);
	} else {
		size_t nmoved_tmp;
		char *p, *pf;

		pf = (char *) kmem_alloc(KNC_STRSIZE, KM_SLEEP);
		p = (char *) kmem_alloc(KNC_STRSIZE, KM_SLEEP);
		error = copyinstr((caddr_t)knconf->knc_protofmly, pf,
				KNC_STRSIZE, &nmoved_tmp);
		if (!error) {
			error = copyinstr((caddr_t)knconf->knc_proto,
					p, KNC_STRSIZE, &nmoved_tmp);
			if (!error) {
				knconf->knc_protofmly = pf;
				knconf->knc_proto = p;
			} else {
				kmem_free((caddr_t)pf, KNC_STRSIZE);
				kmem_free((caddr_t)p, KNC_STRSIZE);
				kmem_free((caddr_t)knconf, sizeof (*knconf));
				return (error);
			}
		} else {
			kmem_free((caddr_t)pf, KNC_STRSIZE);
			kmem_free((caddr_t)p, KNC_STRSIZE);
			kmem_free((caddr_t)knconf, sizeof (*knconf));
			return (error);
		}
	}

	/*
	 * Get server address
	 */
	if (copyin((caddr_t)args.addr, (caddr_t)&addr, sizeof (addr))) {
		addr.buf = NULL;
		error = EFAULT;
	} else {
		char *userbufptr = addr.buf;

		addr.buf = (char *) kmem_alloc(addr.len, KM_SLEEP);
		addr.maxlen = addr.len;
		if (copyin(userbufptr, addr.buf, addr.len))
			error = EFAULT;
	}
	if (error)
		goto errout;

	/*
	 * Get the root fhandle
	 */
	if (copyin((caddr_t)args.fh, (caddr_t)&fh, sizeof (fh))) {
		error = EFAULT;
		goto errout;
	}

	/*
	 * Get server's hostname
	 */
	if (args.flags & NFSMNT_HOSTNAME) {
		error = copyinstr(args.hostname, shostname,
			sizeof (shostname), (u_int *)&hlen);
		if (error)
			goto errout;
	} else
		(void) strncpy(shostname, "unknown-host", sizeof (shostname));


	if (args.flags & (NFSMNT_SECURE | NFSMNT_KERBEROS)) {
		/*
		 * If using AUTH_DES or AUTH_KERB, get time sync netbuf ...
		 */
		if (args.syncaddr == NULL)
			error = EINVAL;
		else {
			if (copyin((caddr_t)args.syncaddr, (caddr_t)&syncaddr,
			    sizeof (syncaddr))) {
				syncaddr.buf = NULL;
				error = EFAULT;
			} else {
				char *userbufptr = syncaddr.buf;

				syncaddr.buf = (char *) kmem_alloc(syncaddr.len,
								KM_SLEEP);
				syncaddr.maxlen = syncaddr.len;
				if (copyin(userbufptr, syncaddr.buf,
				    syncaddr.len))
					error = EFAULT;
			}

			/*
			 * ... and server's netname
			 */
			if (!error) {
				error = copyinstr(args.netname, netname,
					sizeof (netname), (u_int *)&nlen);
			}
		}
	} else
		nlen = -1;

	if (error)
		goto errout;

	/*
	 * Get root vnode.
	 */
	error = nfs3rootvp(&rtvp, vfsp, knconf, &addr, &syncaddr, &fh,
			shostname, netname, nlen, args.flags, cr);
	if (error)
		goto errout;

	/*
	 * Set option fields in mount info record
	 */
	mi = VTOMI(rtvp);
	if (args.flags & NFSMNT_NOAC) {
		mi->mi_flags |= MI_NOAC;
		PURGE_ATTRCACHE(rtvp);
	}
	if (args.flags & NFSMNT_NOCTO)
		mi->mi_flags |= MI_NOCTO;
	if (args.flags & NFSMNT_LLOCK)
		mi->mi_flags |= MI_LLOCK;
	if (args.flags & NFSMNT_GRPID)
		mi->mi_flags |= MI_GRPID;
	if (args.flags & NFSMNT_RETRANS) {
		if (args.retrans < 0) {
			error = EINVAL;
			goto errout;
		}
		mi->mi_retrans = args.retrans;
	}
	if (args.flags & NFSMNT_TIMEO) {
		if (args.timeo <= 0) {
			error = EINVAL;
			goto errout;
		}
		mi->mi_timeo = args.timeo;
		/*
		 * The following scales the standard deviation and
		 * and current retransmission timer to match the
		 * initial value for the timeout specified.
		 */
		mi->mi_timers[3].rt_deviate = (args.timeo * HZ * 2) / 5;
		mi->mi_timers[3].rt_rtxcur = args.timeo * HZ / 10;
	}
	if (args.flags & NFSMNT_RSIZE) {
		if (args.rsize <= 0) {
			error = EINVAL;
			goto errout;
		}
		mi->mi_tsize = MIN(mi->mi_tsize, args.rsize);
		mi->mi_curread = mi->mi_tsize;
	}
	if (args.flags & NFSMNT_WSIZE) {
		if (args.wsize <= 0) {
			error = EINVAL;
			goto errout;
		}
		mi->mi_stsize = MIN(mi->mi_stsize, args.wsize);
		mi->mi_curwrite = mi->mi_stsize;
	}
	if (args.flags & NFSMNT_ACREGMIN) {
		if (args.acregmin < 0)
			mi->mi_acregmin = ACMINMAX;
		else
			mi->mi_acregmin = MIN(args.acregmin, ACMINMAX);
	}
	if (args.flags & NFSMNT_ACREGMAX) {
		if (args.acregmax < 0)
			mi->mi_acregmax = ACMAXMAX;
		else
			mi->mi_acregmax = MIN(args.acregmax, ACMAXMAX);
	}
	if (args.flags & NFSMNT_ACDIRMIN) {
		if (args.acdirmin < 0)
			mi->mi_acdirmin = ACMINMAX;
		else
			mi->mi_acdirmin = MIN(args.acdirmin, ACMINMAX);
	}
	if (args.flags & NFSMNT_ACDIRMAX) {
		if (args.acdirmax < 0)
			mi->mi_acdirmax = ACMAXMAX;
		else
			mi->mi_acdirmax = MIN(args.acdirmax, ACMAXMAX);
	}

errout:
	if (error) {
		if (rtvp != NULL) {
			rp = VTOR(rtvp);
			if (rp->r_flags & RHASHED) {
				mutex_enter(&nfs_rtable_lock);
				rp_rmhash(rp);
				mutex_exit(&nfs_rtable_lock);
			}
			VN_RELE(rtvp);
		}
		if (mi != NULL) {
			nfs_async_stop(vfsp);
			purge_authtab(mi);
			nfs_free_mi(mi);
		} else {
			kmem_free((caddr_t)knconf->knc_protofmly, KNC_STRSIZE);
			kmem_free((caddr_t)knconf->knc_proto, KNC_STRSIZE);
			kmem_free((caddr_t)knconf, sizeof (*knconf));
			if (addr.buf != NULL)
				kmem_free((caddr_t)addr.buf, addr.len);
			if (syncaddr.buf != NULL)
				kmem_free((caddr_t)syncaddr.buf, syncaddr.len);
		}
	}
	return (error);
}

static int nfs3_dynamic = 0;	/* global variable to enable dynamic retrans. */
static u_short nfs3_max_threads = 8;	/* max number of active async threads */
static u_long nfs3_bsize = 32 * 1024;	/* client `block' size */

/*
 * Because we are not using a dynamic resizing of V3 UDP read
 * requests we must specify the maximum read size of the root.
 * There is no place to set the root mount options so we set the
 * default to a reasonable value and allow it to be tuned.
 * We need a generic way to set root mount options.
 */
u_long nfs3_root_rsize = 8 * 1024;	/* Max root read tsize */

int
nfs3rootvp(vnode_t **rtvpp, vfs_t *vfsp, struct knetconfig *kp,
	struct netbuf *addr, struct netbuf *syncaddr, nfs_fh3 *fh,
	char *shostname, char *netname, int nlen, int flags, cred_t *cr)
{
	vnode_t *rtvp;
	mntinfo_t *mi;
	dev_t nfs_dev;
	struct vattr va;
	struct FSINFO3args args;
	struct FSINFO3res res;
	int error;
	int douprintf;
	rnode_t *rp;

	ASSERT(cr->cr_ref != 0);

	/*
	 * Create a mount record and link it to the vfs struct.
	 */
	mi = (mntinfo_t *) kmem_zalloc(sizeof (*mi), KM_SLEEP);
	mutex_init(&mi->mi_lock, "mi_lock", MUTEX_DEFAULT, NULL);
	mi->mi_flags = MI_ACL;
	if (!(flags & NFSMNT_SOFT))
		mi->mi_flags |= MI_HARD;
	if (flags & NFSMNT_INT)
		mi->mi_flags |= MI_INT;
	mi->mi_addr = *addr;
	if (flags & (NFSMNT_SECURE | NFSMNT_KERBEROS))
		mi->mi_syncaddr = *syncaddr;
	mi->mi_knetconfig = kp;
	mi->mi_retrans = NFS_RETRIES;
	if (kp->knc_semantics == NC_TPI_COTS_ORD ||
	    kp->knc_semantics == NC_TPI_COTS)
		mi->mi_timeo = NFS_COTS_TIMEO;
	else
		mi->mi_timeo = NFS_TIMEO;
	mi->mi_prog = NFS_PROGRAM;
	mi->mi_vers = NFS_V3;
	mi->mi_rfsnames = rfsnames_v3;
	mi->mi_reqs = rfsreqcnt_v3;
	mi->mi_call_type = call_type_v3;
	mi->mi_timer_type = timer_type_v3;
	mi->mi_aclnames = aclnames_v3;
	mi->mi_aclreqs = aclreqcnt_v3_ptr;
	mi->mi_acl_call_type = acl_call_type_v3;
	mi->mi_acl_timer_type = acl_timer_type_v3;
	bcopy(shostname, mi->mi_hostname, HOSTNAMESZ);
	mi->mi_acregmin = ACREGMIN;
	mi->mi_acregmax = ACREGMAX;
	mi->mi_acdirmin = ACDIRMIN;
	mi->mi_acdirmax = ACDIRMAX;
	mi->mi_netnamelen = nlen;
	if (nlen > 0) {
		mi->mi_netname = (char *) kmem_alloc((u_int)nlen, KM_SLEEP);
		bcopy(netname, mi->mi_netname, (u_int)nlen);
	}
	mi->mi_authflavor =
		(flags & NFSMNT_SECURE) ? AUTH_DES :
		((flags & NFSMNT_KERBEROS) ? AUTH_KERB : AUTH_UNIX);
	/*
	 * If mount secure failed, we will fall back to AUTH_NONE
	 * and try again.
	 *
	 * The NFS Version 3 mount uses the FSINFO and GETATTR
	 * procedures.  The server should not care if these procedures
	 * have the proper authentication flavor, so if mount retries
	 * using AUTH_NONE that does not require a credential setup
	 * for root then the automounter would work without requiring
	 * root to be keylogged into AUTH_DES.
	 */
	if (mi->mi_authflavor != AUTH_UNIX)
		mi->mi_flags |= MI_TRYANON;

	if (flags & NFSMNT_RPCTIMESYNC)
		mi->mi_flags |= MI_RPCTIMESYNC;
	if (nfs3_dynamic)
		mi->mi_flags |= MI_DYNAMIC;

	/*
	 * Make a vfs struct for nfs.  We do this here instead of below
	 * because rtvp needs a vfs before we can do a getattr on it.
	 *
	 * Assign a unique device id to the mount
	 */
	mutex_enter(&nfs_minor_lock);
	do {
		nfs_minor = (nfs_minor + 1) & MAXMIN;
		nfs_dev = makedevice(nfs_major, nfs_minor);
	} while (vfs_devsearch(nfs_dev));
	mutex_exit(&nfs_minor_lock);

	vfsp->vfs_dev = nfs_dev;
	vfsp->vfs_fsid.val[0] = nfs_dev;
	vfsp->vfs_fsid.val[1] = nfs3fstyp;
	vfsp->vfs_data = (caddr_t)mi;
	vfsp->vfs_fstype = nfs3fstyp;
	vfsp->vfs_bsize = nfs3_bsize;

	/*
	 * Initialize fields used to support async putpage operations.
	 */
	mi->mi_async_reqs = mi->mi_async_tail = NULL;
	mi->mi_threads = 0;
	mi->mi_max_threads = nfs3_max_threads;
	mutex_init(&mi->mi_async_lock, "nfs async_lock", MUTEX_DEFAULT, NULL);
	cv_init(&mi->mi_async_reqs_cv, "nfs async_reqs_cv", CV_DEFAULT, NULL);
	cv_init(&mi->mi_async_cv, "nfs async_cv", CV_DEFAULT, NULL);

	/*
	 * Make the root vnode, use it to get attributes,
	 * then remake it with the attributes.
	 */
	rtvp = makenfs3node(fh, NULL, vfsp, cr);
	mutex_enter(&rtvp->v_lock);
	ASSERT(!(rtvp->v_flag & VROOT));
	rtvp->v_flag |= VROOT;
	mutex_exit(&rtvp->v_lock);

	mi->mi_rootvp = rtvp;

	args.fsroot = *VTOFH3(rtvp);
	douprintf = 1;
	error = rfs3call(mi, NFSPROC3_FSINFO,
			xdr_FSINFO3args, (caddr_t)&args,
			xdr_FSINFO3res, (caddr_t)&res,
			cr, &douprintf, &res.status);
	if (error)
		goto bad;
	error = geterrno3(res.status);
	if (error)
		goto bad;
	nfs3_cache_post_op_attr(rtvp, &res.resok.obj_attributes, cr);
	mi->mi_tsize = MIN(res.resok.rtpref, nfs3tsize());
	if (mi->mi_tsize == 0) {
		mi->mi_tsize = MIN(res.resok.rtmax, nfs3tsize());
		if (mi->mi_tsize == 0) {
#ifdef DEBUG
			cmn_err(CE_WARN,
			    "NFS3 server %s returned 0 for read transfer sizes",
				mi->mi_hostname);
#else
			cmn_err(CE_WARN,
			    "NFS server %s returned 0 for transfer sizes",
				mi->mi_hostname);
#endif
			error = EIO;
			goto bad;
		}
	}
	mi->mi_curread = mi->mi_tsize;
	mi->mi_stsize = MIN(res.resok.wtpref, nfs3tsize());
	if (mi->mi_stsize == 0) {
		mi->mi_stsize = MIN(res.resok.wtmax, nfs3tsize());
		if (mi->mi_stsize == 0) {
#ifdef DEBUG
			cmn_err(CE_WARN,
			"NFS3 server %s returned 0 for write transfer sizes",
				mi->mi_hostname);
#else
			cmn_err(CE_WARN,
			"NFS server %s returned 0 for write transfer sizes",
				mi->mi_hostname);
#endif
			error = EIO;
			goto bad;
		}
	}
	mi->mi_curwrite = mi->mi_stsize;
	if (res.resok.properties & FSF3_LINK)
		mi->mi_flags |= MI_LINK;
	if (res.resok.properties & FSF3_SYMLINK)
		mi->mi_flags |= MI_SYMLINK;

	if (!res.resok.obj_attributes.attributes) {
		va.va_mask = AT_ALL;
		error = nfs3getattr(rtvp, &va, cr);
		if (error)
			goto bad;
		rtvp->v_type = va.va_type;
	} else
		rtvp->v_type = VTOR(rtvp)->r_attr.va_type;

	/*
	 * MI_TRYANON is only for the mount operation so turn it back off.
	 */
	mi->mi_flags &= ~MI_TRYANON;

	*rtvpp = rtvp;
	return (0);
bad:
	/*
	 * An error occurred somewhere, need to clean up...
	 * We need to release our reference to the root vnode and
	 * destroy the mntinfo struct that we just created.
	 * However, we can't just free the mi_addr, mi_syncaddr,
	 * and mi_knetconfig structures because our caller allocated
	 * them, not us.
	 */
	rp = VTOR(rtvp);
	if (rp->r_flags & RHASHED) {
		mutex_enter(&nfs_rtable_lock);
		rp_rmhash(rp);
		mutex_exit(&nfs_rtable_lock);
	}
	VN_RELE(rtvp);
	nfs_async_stop(vfsp);
	mi->mi_addr.len = 0;
	mi->mi_authflavor = AUTH_NULL;
	mi->mi_syncaddr.len = 0;
	mi->mi_knetconfig = NULL;
	purge_authtab(mi);
	nfs_free_mi(mi);
	*rtvpp = NULL;
	return (error);
}

/*
 * vfs operations
 */
static int
nfs3_unmount(vfs_t *vfsp, cred_t *cr)
{
	mntinfo_t *mi;
	vnode_t *vp;
	u_short omax;

	if (!suser(cr))
		return (EPERM);

	/*
	 * Wait until all asynchronous putpage operations on
	 * this file system are complete before flushing rnodes
	 * from the cache.
	 */
	mi = VFTOMI(vfsp);
	omax = mi->mi_max_threads;
	nfs_async_stop(vfsp);

	rflush(vfsp, cr);

	vp = mi->mi_rootvp;
	mutex_enter(&nfs_rtable_lock);
	ASSERT(vp->v_count > 0);

	/*
	 * If the reference count on the root vnode is higher
	 * than 1 or if there are any other active vnodes on
	 * this file system, then the file system is busy and
	 * it can't be umounted.
	 */
	if (vp->v_count != 1 || check_rtable(vfsp, vp)) {
		mutex_exit(&nfs_rtable_lock);
		mutex_enter(&mi->mi_async_lock);
		mi->mi_max_threads = omax;
		mutex_exit(&mi->mi_async_lock);
		return (EBUSY);
	}

	/*
	 * Purge all rnodes belonging to this file system from the
	 * rnode hash queues and purge any resources allocated to
	 * them.
	 */
	purge_rtable(vfsp, cr);
	mutex_exit(&nfs_rtable_lock);

	VN_RELE(vp);

	purge_authtab(mi);
	nfs_free_mi(mi);

	return (0);
}

/*
 * find root of nfs
 */
static int
nfs3_root(vfs_t *vfsp, vnode_t **vpp)
{

	*vpp = VFTOMI(vfsp)->mi_rootvp;
	VN_HOLD(*vpp);
	return (0);
}

/*
 * Get file system statistics.
 */
static int
nfs3_statvfs(vfs_t *vfsp, struct statvfs *sbp)
{
	int error;
	struct FSSTAT3args args;
	struct FSSTAT3res res;
	struct mntinfo *mi;
	int douprintf;
	vnode_t *vp;
	cred_t *cr;

	mi = VFTOMI(vfsp);
	vp = mi->mi_rootvp;
	cr = CRED();

	args.fsroot = *VTOFH3(vp);

	douprintf = 1;
	error = rfs3call(mi, NFSPROC3_FSSTAT,
			xdr_FSSTAT3args, (caddr_t)&args,
			xdr_FSSTAT3res, (caddr_t)&res,
			cr, &douprintf, &res.status);

	if (error)
		return (error);

	error = geterrno3(res.status);
	if (!error) {
		nfs3_cache_post_op_attr(vp, &res.resok.obj_attributes, cr);
		sbp->f_bsize = MAXBSIZE;
		sbp->f_frsize = MAXBSIZE;
		sbp->f_blocks = (u_long)(res.resok.tbytes / MAXBSIZE);
		sbp->f_bfree = (u_long)(res.resok.fbytes / MAXBSIZE);
		sbp->f_bavail = (u_long)(res.resok.abytes / MAXBSIZE);
		sbp->f_files = (u_long)res.resok.tfiles;
		sbp->f_ffree = (u_long)res.resok.ffiles;
		sbp->f_favail = (u_long)res.resok.afiles;
		/*
		 *	XXX - This is wrong, should be a real fsid
		 */
		bcopy((caddr_t)&vfsp->vfs_fsid, (caddr_t)&sbp->f_fsid,
		    sizeof (fsid_t));
		/*
		 * 1192587 avoid userland confusion: "nfs" rather than "nfs3"
		 */
		(void) strncpy(sbp->f_basetype, "nfs", FSTYPSZ);
		sbp->f_flag = vf_to_stf(vfsp->vfs_flag);
		sbp->f_namemax = (u_long)-1;
	} else {
		nfs3_cache_post_op_attr(vp, &res.resfail.obj_attributes, cr);
		PURGE_STALE_FH(error, vp, cr);
	}

	return (error);
}

static kmutex_t nfs3_syncbusy;

/*
 * Flush dirty nfs files for file system vfsp.
 * If vfsp == NULL, all nfs files are flushed.
 */
/* ARGSUSED */
static int
nfs3_sync(vfs_t *vfsp, short flag, cred_t *cr)
{

	if (!(flag & SYNC_ATTR) && mutex_tryenter(&nfs3_syncbusy) != 0) {
		rflush(vfsp, cr);
		mutex_exit(&nfs3_syncbusy);
	}
	return (0);
}

/* ARGSUSED */
static int
nfs3_vget(vfs_t *vfsp, vnode_t **vpp, fid_t *fidp)
{
	int error;
	nfs_fh3 fh;
	vnode_t *vp;
	struct vattr va;

	if (fidp->fid_len > NFS3_FHSIZE) {
		*vpp = NULL;
		return (ESTALE);
	}

	fh.fh3_length = fidp->fid_len;
	bcopy(fidp->fid_data, fh.fh3_u.data, fh.fh3_length);

	vp = makenfs3node(&fh, NULL, vfsp, CRED());

	if (vp->v_type == VNON) {
		va.va_mask = AT_ALL;
		error = nfs3getattr(vp, &va, CRED());
		if (error) {
			VN_RELE(vp);
			*vpp = NULL;
			return (error);
		}
		vp->v_type = va.va_type;
	}

	*vpp = vp;

	return (0);
}

/* ARGSUSED */
static int
nfs3_mountroot(vfs_t *vfsp, whymountroot_t why)
{
	struct netbuf root_addr;
	vnode_t *rtvp;
	char root_hostname[SYS_NMLN+1];
	nfs_fh3 root_fhandle;
	int error;
	struct knetconfig *knconf;
	int size;
	char *root_path;
	struct pathname pn;
	char *name;
	cred_t *cr;
	mntinfo_t *mi;
	static char token[10];

	/* do this BEFORE getfile which causes xid stamps to be initialized */
	clkset(-1L);		/* hack for now - until we get time svc? */

	if (why == ROOT_REMOUNT) {
		/*
		 * Shouldn't happen.
		 */
		panic("nfs3_mountroot: why == ROOT_REMOUNT\n");
	}

	if (why == ROOT_UNMOUNT) {
		/*
		 * Nothing to do for NFS.
		 */
		return (0);
	}

	/*
	 * why == ROOT_INIT
	 */

	name = token;
	*name = 0;
	(void) getfsname("root", name);

	pn_alloc(&pn);
	root_path = pn.pn_path;

	knconf = (struct knetconfig *) kmem_zalloc(sizeof (*knconf), KM_SLEEP);
	knconf->knc_protofmly = (char *) kmem_alloc(KNC_STRSIZE, KM_SLEEP);
	knconf->knc_proto = (char *) kmem_alloc(KNC_STRSIZE, KM_SLEEP);

	if (error = mount3_root(knconf, *name ? name : "root",
			&root_addr, &root_fhandle, root_hostname, root_path)) {
		if (error == EPROTONOSUPPORT)
			nfs_cmn_err(error, CE_WARN,
"nfs3_mountroot: mount3_root failed: server doesn't support NFS V3");
		else
			nfs_cmn_err(error, CE_WARN,
				"nfs3_mountroot: mount3_root failed: %m");
		kmem_free((caddr_t)knconf->knc_protofmly, KNC_STRSIZE);
		kmem_free((caddr_t)knconf->knc_proto, KNC_STRSIZE);
		kmem_free((caddr_t)knconf, sizeof (*knconf));
		pn_free(&pn);
		return (error);
	}

	cr = crgetcred();

	error = nfs3rootvp(&rtvp, vfsp, knconf, &root_addr, NULL,
			&root_fhandle, root_hostname, NULL, -1, 0, cr);

	crfree(cr);

	if (error) {
		kmem_free((caddr_t)knconf->knc_protofmly, KNC_STRSIZE);
		kmem_free((caddr_t)knconf->knc_proto, KNC_STRSIZE);
		kmem_free((caddr_t)knconf, sizeof (*knconf));
		pn_free(&pn);
		return (error);
	}

	(void) vfs_lock_wait(vfsp);

	if (why != ROOT_BACKMOUNT)
		vfs_add(NULL, vfsp, 0);

	/*
	 * Set maximum attribute timeouts and turn off close-to-open
	 * consistency checking and set local locking.
	 */
	mi = VFTOMI(vfsp);
	if (why == ROOT_BACKMOUNT) {
		/* cache-only client */
		mi->mi_acregmin = ACREGMIN;
		mi->mi_acregmax = ACREGMAX;
		mi->mi_acdirmin = ACDIRMIN;
		mi->mi_acdirmax = ACDIRMAX;
	} else {
		/* diskless */
		mi->mi_acregmin = ACMINMAX;
		mi->mi_acregmax = ACMAXMAX;
		mi->mi_acdirmin = ACMINMAX;
		mi->mi_acdirmax = ACMAXMAX;
	}
	mutex_enter(&mi->mi_lock);
	mi->mi_flags |= (MI_NOCTO | MI_LLOCK);
	mi->mi_tsize = MIN(nfs3_root_rsize, nfs3tsize());
	mi->mi_curread = mi->mi_tsize;
	mutex_exit(&mi->mi_lock);

	vfs_unlock(vfsp);

	size = strlen(root_hostname);
	strcpy(rootfs.bo_name, root_hostname);
	rootfs.bo_name[size] = ':';
	strcpy(&rootfs.bo_name[size+1], root_path);

	pn_free(&pn);

	return (0);
}

/*
 * Initialization routine for VFS routines.  Should only be called once
 */
int
nfs3_vfsinit(void)
{

	mutex_init(&nfs3_syncbusy, "nfs3_syncbusy", MUTEX_DEFAULT, NULL);
	return (0);
}
