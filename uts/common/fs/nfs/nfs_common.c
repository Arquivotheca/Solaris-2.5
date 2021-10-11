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
 *	(c) 1986-1991, 1994, 1995  Sun Microsystems, Inc.
 *	(c) 1983, 1984, 1985, 1986, 1987, 1988, 1989  AT&T.
 *		All rights reserved.
 */

#pragma ident	"@(#)nfs_common.c	1.47	95/03/17 SMI"	/* SVr4.0 1.7 */

/*	nfs_common.c 1.4 88/09/19 SMI	*/

#include <sys/errno.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/pathname.h>
#include <sys/bootconf.h>
#include <fs/fs_subr.h>
#include <rpc/types.h>
#include <nfs/nfs.h>
#include <nfs/nfs_clnt.h>
#include <nfs/rnode.h>
#include <nfs/mount.h>
#include <nfs/nfssys.h>
#include <sys/debug.h>
#include <sys/cmn_err.h>

/*
 * This is the loadable module wrapper.
 */
#include <sys/systm.h>
#include <sys/modctl.h>
#include <sys/syscall.h>
#include <sys/ddi.h>

/*
 * The psuedo NFS filesystem to allow diskless booting to dynamically
 * mount either a NFS V2 or NFS V3 filesystem.  This only implements
 * the VFS_MOUNTROOT op and is only intended to be used by the
 * diskless booting code until the real root filesystem is mounted.
 * Nothing else should ever call this!
 *
 * The strategy is that if the initial rootfs type is set to "nfsdyn"
 * by loadrootmodules() this filesystem is called to mount the
 * root filesystem.  It first attempts to mount a V3 filesystem
 * and if that fails due to an RPC version mismatch it tries V2.
 * once the real mount succeeds the vfsops and rootfs name are changed
 * to reflect the real filesystem type.
 */
static int nfsdyninit(struct vfssw *, int);
static int nfsdyn_mountroot(vfs_t *, whymountroot_t);

struct vfsops nfsdyn_vfsops = {
	fs_nosys,	/* mount */
	fs_nosys,	/* unmount */
	fs_nosys,	/* root */
	fs_nosys,	/* statvfs */
	fs_sync,	/* sync */
	fs_nosys,	/* vget */
	nfsdyn_mountroot,
	fs_nosys	/* swapvp */
};

/*
 * This mutex is used to serialize initialzation of the NFS version
 * independent kstat structures.  These need to get initialized once
 * and only once, can happen in either one of two places, so the
 * initialization support needs to be serialized.
 */
kmutex_t nfs_kstat_lock;

/*
 * Server statistics.  These are defined here, rather than in the server
 * code, so that they can be referenced before the nfssrv kmod is loaded.
 */

static kstat_named_t svstat[] = {
	{ "calls",	KSTAT_DATA_ULONG },
	{ "badcalls",	KSTAT_DATA_ULONG },
};

kstat_named_t *svstat_ptr = svstat;
ulong_t svstat_ndata = sizeof (svstat) / sizeof (kstat_named_t);

static kstat_named_t aclproccnt_v2[] = {
	{ "null",	KSTAT_DATA_ULONG },
	{ "getacl",	KSTAT_DATA_ULONG },
	{ "setacl",	KSTAT_DATA_ULONG },
	{ "getattr",	KSTAT_DATA_ULONG },
	{ "access",	KSTAT_DATA_ULONG }
};

kstat_named_t *aclproccnt_v2_ptr = aclproccnt_v2;
ulong_t aclproccnt_v2_ndata = sizeof (aclproccnt_v2) / sizeof (kstat_named_t);

static kstat_named_t aclproccnt_v3[] = {
	{ "null",	KSTAT_DATA_ULONG },
	{ "getacl",	KSTAT_DATA_ULONG },
	{ "setacl",	KSTAT_DATA_ULONG }
};

kstat_named_t *aclproccnt_v3_ptr = aclproccnt_v3;
ulong_t aclproccnt_v3_ndata = sizeof (aclproccnt_v3) / sizeof (kstat_named_t);

static kstat_named_t rfsproccnt_v2[] = {
	{ "null",	KSTAT_DATA_ULONG },
	{ "getattr",	KSTAT_DATA_ULONG },
	{ "setattr",	KSTAT_DATA_ULONG },
	{ "root",	KSTAT_DATA_ULONG },
	{ "lookup",	KSTAT_DATA_ULONG },
	{ "readlink",	KSTAT_DATA_ULONG },
	{ "read",	KSTAT_DATA_ULONG },
	{ "wrcache",	KSTAT_DATA_ULONG },
	{ "write",	KSTAT_DATA_ULONG },
	{ "create",	KSTAT_DATA_ULONG },
	{ "remove",	KSTAT_DATA_ULONG },
	{ "rename",	KSTAT_DATA_ULONG },
	{ "link",	KSTAT_DATA_ULONG },
	{ "symlink",	KSTAT_DATA_ULONG },
	{ "mkdir",	KSTAT_DATA_ULONG },
	{ "rmdir",	KSTAT_DATA_ULONG },
	{ "readdir",	KSTAT_DATA_ULONG },
	{ "statfs",	KSTAT_DATA_ULONG }
};

kstat_named_t *rfsproccnt_v2_ptr = rfsproccnt_v2;
ulong_t rfsproccnt_v2_ndata = sizeof (rfsproccnt_v2) / sizeof (kstat_named_t);

kstat_named_t rfsproccnt_v3[] = {
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

kstat_named_t *rfsproccnt_v3_ptr = rfsproccnt_v3;
ulong_t rfsproccnt_v3_ndata = sizeof (rfsproccnt_v3) / sizeof (kstat_named_t);

/*
 * The following data structures are used to configure the NFS
 * system call, the NFS Version 2 client VFS, and the NFS Version
 * 3 client VFS into the system.
 */

/*
 * The NFS system call.
 */
static struct sysent nfssysent = {
	2,
	0,
	nfssys
};

static struct modlsys modlsys = {
	&mod_syscallops,
	"NFS syscall, client, and common",
	&nfssysent
};

/*
 * The NFS Dynamic client VFS.
 */
static struct vfssw vfw = {
	"nfsdyn",
	nfsdyninit,
	&nfsdyn_vfsops,
	0
};

static struct modlfs modlfs = {
	&mod_fsops,
	"network filesystem",
	&vfw
};

/*
 * The NFS Version 2 client VFS.
 */
static struct vfssw vfw2 = {
	"nfs",
	nfsinit,
	&nfs_vfsops,
	0
};

static struct modlfs modlfs2 = {
	&mod_fsops,
	"network filesystem version 2",
	&vfw2
};

/*
 * The NFS Version 3 client VFS.
 */
static struct vfssw vfw3 = {
	"nfs3",
	nfs3init,
	&nfs3_vfsops,
	0
};

static struct modlfs modlfs3 = {
	&mod_fsops,
	"network filesystem version 3",
	&vfw3
};

/*
 * We have too many linkage structures so we define our own XXX
 */
struct modlinkage_big {
	int		ml_rev;		/* rev of loadable modules system */
	void		*ml_linkage[5];	/* NULL terminated list of */
					/* linkage structures */
};

/*
 * All of the module configuration linkages required to configure
 * the system call and client VFS's into the system.
 */
static struct modlinkage_big modlinkage = {
	MODREV_1, (void *)&modlsys, (void *)&modlfs, (void *)&modlfs2,
				(void *)&modlfs3, NULL,
};

/*
 * specfs - for getfsname only??
 * rpcmod - too many symbols to build stubs for them all
 */
char _depends_on[] = "fs/specfs strmod/rpcmod";

/*
 * This routine is invoked automatically when the kernel module
 * containing this routine is loaded.  This allows module specific
 * initialization to be done when the module is loaded.
 */
_init(void)
{
	int status;

	if ((status = nfs_clntinit()) != 0) {
		cmn_err(CE_WARN, "_init: nfs_clntinit failed");
		return (status);
	}

	mutex_init(&nfs_kstat_lock, "nfs_kstat_lock", MUTEX_DEFAULT, NULL);

	return (mod_install((struct modlinkage *)&modlinkage));
}

_fini(void)
{

	/* Don't allow module to be unloaded */
	return (EBUSY);
}

_info(struct modinfo *modinfop)
{

	return (mod_info((struct modlinkage *)&modlinkage, modinfop));
}

/*
 * General utilities
 */

/*
 * Returns the prefered transfer size in bytes based on
 * what network interfaces are available.
 */
int
nfstsize(void)
{

	/*
	 * For the moment, just return NFS_MAXDATA until we can query the
	 * appropriate transport.
	 */
	return (NFS_MAXDATA);
}

/*
 * Returns the prefered transfer size in bytes based on
 * what network interfaces are available.
 */

static int nfs3_max_transfer_size = 32 * 1024;

int
nfs3tsize(void)
{

	/*
	 * For the moment, just return nfs3_max_transfer_size until we
	 * can query the appropriate transport.
	 */
	return (nfs3_max_transfer_size);
}

/* ARGSUSED */
static int
nfsdyninit(struct vfssw *vswp, int fstyp)
{
	vswp->vsw_vfsops = &nfsdyn_vfsops;

	return (0);
}

/* ARGSUSED */
static int
nfsdyn_mountroot(vfs_t *vfsp, whymountroot_t why)
{
	struct netbuf root_addr;
	vnode_t *rtvp;
	char root_hostname[SYS_NMLN+1];
	fhandle_t root_fhandle2;
	nfs_fh3 root_fhandle3;
	int error;
	struct knetconfig *knconf;
	int size;
	char *root_path;
	struct pathname pn;
	char *name;
	cred_t *cr;
	mntinfo_t *mi;
	static char token[10];
	int version = NFS_V3;
	u_long rsize;

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

	vfsp->vfs_op = &nfs3_vfsops;
	if (error = mount3_root(knconf, *name ? name : "root",
			&root_addr, &root_fhandle3, root_hostname, root_path)) {
		if (error != EPROTONOSUPPORT) {
			nfs_cmn_err(error, CE_WARN,
				"nfsdyn_mountroot: mount3_root failed: %m");
			kmem_free((caddr_t)knconf->knc_protofmly, KNC_STRSIZE);
			kmem_free((caddr_t)knconf->knc_proto, KNC_STRSIZE);
			kmem_free((caddr_t)knconf, sizeof (*knconf));
			pn_free(&pn);
			vfsp->vfs_op = &nfsdyn_vfsops;
			return (error);
		}
		version = NFS_VERSION;
		vfsp->vfs_op = &nfs_vfsops;
		if (error = mount_root(knconf, *name ? name : "root",
			&root_addr, &root_fhandle2, root_hostname, root_path)) {
			nfs_cmn_err(error, CE_WARN,
				"nfsdyn_mountroot: mount_root failed: %m");
			kmem_free((caddr_t)knconf->knc_protofmly, KNC_STRSIZE);
			kmem_free((caddr_t)knconf->knc_proto, KNC_STRSIZE);
			kmem_free((caddr_t)knconf, sizeof (*knconf));
			pn_free(&pn);
			vfsp->vfs_op = &nfsdyn_vfsops;
			return (error);
		}
	}

	cr = crgetcred();

	switch (version) {
	case NFS_VERSION:
		error = nfsrootvp(&rtvp, vfsp, knconf, &root_addr, NULL,
			&root_fhandle2, root_hostname, NULL, -1, 0, cr);
		rsize = nfstsize();
		break;
	case NFS_V3:
		error = nfs3rootvp(&rtvp, vfsp, knconf, &root_addr, NULL,
			&root_fhandle3, root_hostname, NULL, -1, 0, cr);
		rsize = MIN(nfs3_root_rsize, nfs3tsize());
		break;
	default:
		cmn_err(CE_PANIC, "nfsdyn_mountroot: impossible NFS version");
		/* NOTREACHED */
		break;
	}
	(void) strcpy(rootfs.bo_fstype, vfssw[vfsp->vfs_fstype].vsw_name);

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
	mi->mi_tsize = rsize;
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
