/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#pragma	ident	"@(#)auto_vfsops.c 1.18     95/05/09 SMI"

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/proc.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/uio.h>
#include <sys/kmem.h>
#include <sys/cred.h>
#include <sys/statvfs.h>
#include <sys/mount.h>
#include <sys/tiuser.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/mkdev.h>
#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/pathname.h>
#include <rpc/types.h>
#include <rpc/auth.h>
#include <rpc/clnt.h>
#include <fs/fs_subr.h>
#include <sys/fs/autofs.h>
#include <sys/fs/autofs_prot.h>

int auto_init(vfssw_t *, int);

/*
 * This is the loadable module wrapper.
 */
#include <sys/modctl.h>

extern vfsops_t auto_vfsops;
int autofs_major;
int autofs_minor;
kmutex_t autofs_minor_lock;

static vfssw_t vfw = {
	"autofs",
	auto_init,
	&auto_vfsops,
	0
};

/*
 * Module linkage information for the kernel.
 */
extern struct mod_ops mod_fsops;

static struct modlfs modlfs = {
	&mod_fsops, "filesystem for autofs", &vfw
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modlfs, NULL
};

/*
 * This is the module initialization routine.
 */

/*
 * Don't allow the autofs module to be unloaded for now.
 */

static int module_keepcnt = 1;	/* ==0 means the module is unloadable */

/*
 * There are not enough stubs for rpcmod so we must force load it
 */
char _depends_on[] = "strmod/rpcmod";

_init(void)
{
	return (mod_install(&modlinkage));
}

_fini(void)
{
	if (module_keepcnt != 0)
		return (EBUSY);

	return (mod_remove(&modlinkage));
}

_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

extern struct vnodeops auto_vnodeops;
#ifdef	AUTODEBUG
extern int autodebug;
#endif	AUTODEBUG

static int autofs_fstype;

/*
 * autofs vfs operations.
 */
static int auto_mount(vfs_t *, vnode_t *, struct mounta *, cred_t *);
static int auto_unmount(vfs_t *, cred_t *);
static int auto_root(vfs_t *, vnode_t **);
static int auto_statvfs(vfs_t *, statvfs_t *);

struct vfsops auto_vfsops = {
	auto_mount,	/* mount */
	auto_unmount,	/* unmount */
	auto_root,	/* root */
	auto_statvfs,	/* statvfs */
	fs_sync,	/* sync */
	fs_nosys,	/* vget */
	fs_nosys,	/* mountroot */
	fs_nosys	/* swapvp */
};

static void
unmount_init(void)
{
	autonode_list = NULL;
	mutex_init(&autonode_list_lock, "autonode list",
			MUTEX_DEFAULT, NULL);
	if (thread_create(NULL, DEFAULTSTKSZ, do_unmount,
			NULL, 0, &p0, TS_RUN, 60) == NULL)
		cmn_err(CE_WARN, "autofs: unmount thread create failure\n");
}

#define	MAJOR_MIN 128
#define	vfsNVFS   (&vfssw[nfstype])

int
auto_init(
	vfssw_t *vswp,
	int fstype
)
{
	autofs_fstype = fstype;
	ASSERT(autofs_fstype != 0);
	/*
	 * Associate VFS ops vector with this fstype
	 */
	vswp->vsw_vfsops = &auto_vfsops;

	mutex_init(&autofs_minor_lock, "autofs minor lock",
		MUTEX_DEFAULT, NULL);
	mutex_init(&autonode_count_lock, "autonode count lock",
		MUTEX_DEFAULT, NULL);

	/*
	 * Assign unique major number for all autofs mounts
	 */
	if ((autofs_major = getudev()) == -1) {
		cmn_err(CE_WARN,
			"autofs: auto_init: can't get unique device number");
		autofs_major = 0;
	}
	unmount_init();

	return (0);
}

/*
 * autofs mount vfsop
 * Set up mount info record and attach it to vfs struct.
 */
/*ARGSUSED*/
static int
auto_mount(
	vfs_t *vfsp,
	vnode_t *vp,
	struct mounta *uap,
	cred_t *cr
)
{
	int error = 0;
	struct auto_args args;
	vnode_t *rootvp = NULL;
	vnode_t *kvp;
	char *data = uap->dataptr;
	char datalen = uap->datalen;
	struct autoinfo *aip;
	char strbuff[MAXPATHLEN+1];
	dev_t autofs_dev;
	int len;

#ifdef AUTODEBUG
	auto_dprint(autodebug, 4,
	    "auto_mount: enter: vfs %x \n", vfsp);
#endif
	module_keepcnt++;

	if (!suser(cr)) {
		module_keepcnt--;
		return (EPERM);
	}

	/*
	 * Get arguments
	 */
	if (datalen != sizeof (args)) {
		module_keepcnt--;
		return (EINVAL);
	}
	if (copyin(data, (caddr_t)&args, sizeof (args))) {
		module_keepcnt--;
		return (EFAULT);
	}

	/*
	 * For a remount just update mount information
	 * i.e. default mount options, map name, etc.
	 */
	if (uap->flags & MS_REMOUNT) {
		aip = vfstoai(vfsp);
		if (aip == NULL) {
			module_keepcnt--;
			return (EINVAL);
		}

		aip->ai_direct = args.direct;
		aip->ai_mount_to = args.mount_to;
		aip->ai_rpc_to = args.rpc_to;

		/*
		 * Get default mount options
		 */
		if (copyinstr(args.opts, strbuff, sizeof (strbuff),
			(u_int *) &len)) {
			module_keepcnt--;
			return (EFAULT);
		}
		kmem_free(aip->ai_opts, aip->ai_optslen);
		aip->ai_opts = kmem_alloc(len, KM_SLEEP);
		aip->ai_optslen = len;
		bcopy(strbuff, aip->ai_opts, len);

		/*
		 * Get map name
		 */
		if (copyinstr(args.map, strbuff, sizeof (strbuff),
			(u_int *) &len)) {
			module_keepcnt--;
			return (EFAULT);
		}
		kmem_free(aip->ai_map, aip->ai_maplen);
		aip->ai_map = kmem_alloc(len, KM_SLEEP);
		aip->ai_maplen = len;
		bcopy(strbuff, aip->ai_map, len);

		return (0);
	}

	/*
	 * allocate autoinfo struct and attach it to vfs
	 */
	aip = (struct autoinfo *)kmem_zalloc(sizeof (*aip), KM_SLEEP);
	aip->ai_mountvfs = vfsp;

	aip->ai_mount_to = args.mount_to;
	aip->ai_rpc_to = args.rpc_to;
	aip->ai_refcnt = 0;
	vfsp->vfs_bsize = 1024;
	vfsp->vfs_fstype = autofs_fstype;

	/*
	 * Assign a unique device id to the mount
	 */
	mutex_enter(&autofs_minor_lock);
	do {
		autofs_minor = (autofs_minor + 1) & MAXMIN;
		autofs_dev = makedevice(autofs_major, autofs_minor);
	} while (vfs_devsearch(autofs_dev));
	mutex_exit(&autofs_minor_lock);

	vfsp->vfs_dev = autofs_dev;
	vfsp->vfs_fsid.val[0] = autofs_dev;
	vfsp->vfs_fsid.val[1] = autofs_fstype;
	vfsp->vfs_data = (caddr_t)aip;
	vfsp->vfs_bcount = 0;

	/*
	 * Get daemon address
	 */
	aip->ai_addr.len = args.addr.len;
	aip->ai_addr.maxlen = aip->ai_addr.len;
	aip->ai_addr.buf = (char *)kmem_alloc(args.addr.len, KM_SLEEP);
	if (copyin(args.addr.buf, aip->ai_addr.buf, args.addr.len)) {
		error = EFAULT;
		goto errout;
	}

	/*
	 * Get path for mountpoint
	 */
	if (copyinstr(args.path, strbuff, sizeof (strbuff), (u_int *) &len)) {
		error = EFAULT;
		goto errout;
	}
	aip->ai_path = kmem_alloc(len, KM_SLEEP);
	aip->ai_pathlen = len;
	bcopy(strbuff, aip->ai_path, len);

	/*
	 * Get default mount options
	 */
	if (copyinstr(args.opts, strbuff, sizeof (strbuff), (u_int *) &len)) {
		error = EFAULT;
		goto errout;
	}
	aip->ai_opts = kmem_alloc(len, KM_SLEEP);
	aip->ai_optslen = len;
	bcopy(strbuff, aip->ai_opts, len);

	/*
	 * Get map name
	 */
	if (copyinstr(args.map, strbuff, sizeof (strbuff), (u_int *) &len)) {
		error = EFAULT;
		goto errout;
	}
	aip->ai_map = kmem_alloc(len, KM_SLEEP);
	aip->ai_maplen = len;
	bcopy(strbuff, aip->ai_map, len);

	/*
	 * Get mount type
	 */
	aip->ai_direct = args.direct;

	/*
	 * Setup netconfig.
	 * Assume a connectionless loopback transport.
	 * XXX Need to think about passing the knconf
	 * in as a mount arg.
	 */
	if ((error = lookupname("/dev/ticlts", UIO_SYSSPACE, FOLLOW,
		NULLVPP, &kvp)) != 0) {
		cmn_err(CE_CONT, "autofs: lookupname: %d\n", error);
		goto errout;
	}
	aip->ai_knconf.knc_rdev = kvp->v_rdev;
	aip->ai_knconf.knc_protofmly = NC_LOOPBACK;
	aip->ai_knconf.knc_semantics = NC_TPI_CLTS;
	VN_RELE(kvp);

	/*
	 * Make the root vnode
	 */
	rootvp = makeautonode(VDIR, vfsp, cr);
	strcpy(vntoan(rootvp)->an_name, aip->ai_path); /* for debugging only */
	vntoan(rootvp)->an_parent = vntoan(rootvp);

	rootvp->v_flag |= VROOT;
	vntoan(rootvp)->an_mode = 0555;	/* read and search perms */
	if (aip->ai_direct)
		vntoan(rootvp)->an_mntflags |= MF_MNTPNT;
	aip->ai_rootvp = rootvp;

	/*
	 * All the root autnodes are held in a link list
	 * This facilitates the unmounting. The unmount
	 * thread then just has to traverse this link list
	 */
	mutex_enter(&autonode_list_lock);
	if (autonode_list == NULL)
		autonode_list = vntoan(rootvp);
	else {
		autonode_t *anp;
		anp = vntoan(rootvp);
		anp->an_next = autonode_list;
		autonode_list = anp;
	}
	mutex_exit(&autonode_list_lock);

#ifdef AUTODEBUG
	auto_dprint(autodebug, 5,
	    "auto_mount: vfs %x root %x aip %x\n",
	    vfsp, rootvp, aip);
#endif

	return (0);

errout:
	/*
	 * NOTE: We should only arrive here if aip has already been
	 * allocated, and this is NOT a remount.
	 */
	ASSERT(aip != NULL);
	ASSERT((uap->flags & MS_REMOUNT) == 0);

	if (aip->ai_addr.buf != NULL)
		kmem_free(aip->ai_addr.buf, aip->ai_addr.len);
	if (aip->ai_path != NULL)
		kmem_free(aip->ai_path, aip->ai_pathlen);
	if (aip->ai_opts != NULL)
		kmem_free(aip->ai_opts, aip->ai_optslen);
	if (aip->ai_map != NULL)
		kmem_free(aip->ai_map, aip->ai_maplen);
	kmem_free(aip, sizeof (*aip));

	module_keepcnt--;
	return (error);
}

/*
 * Undo autofs mount
 */
static int
auto_unmount(
	vfs_t *vfsp,
	cred_t *cr
)
{
	struct autoinfo *aip;
	vnode_t *rvp;
	autonode_t *rap, *ap, **app;

	aip = vfstoai(vfsp);
#ifdef AUTODEBUG
	auto_dprint(autodebug, 4, "auto_unmount vfsp %x aip %x\n",
		vfsp, aip);
#endif

	if (!suser(cr))
		return (EPERM);

	rvp = aip->ai_rootvp;
	rap = vntoan(rvp);

	/*
	 * In case of all autofs mountpoints the following
	 * should be true
	 */
	ASSERT(rap->an_parent == rap);

	if (rvp->v_count > 1) {
#ifdef AUTODEBUG
		auto_dprint(autodebug, 5,
			"refcnt %d v_ct %d\n", aip->ai_refcnt,
			aip->ai_rootvp->v_count);
#endif
		return (EBUSY);
	}

	/*
	 * Remove the root vnode from the linked list of
	 * root autonodes.
	 */
	ASSERT(rap->an_dirents == NULL);

	mutex_enter(&autonode_list_lock);
	app = &autonode_list;
	for (;;) {
		ap = *app;
		if (ap == NULL)
			cmn_err(CE_PANIC,
				"autofs: "
				"root autonode not found for vfs %x\n",
				(int)vfsp);
		if (ap == rap)
			break;
		app = &ap->an_next;
	}
	*app = ap->an_next;
	mutex_exit(&autonode_list_lock);

	ASSERT(rvp->v_count == 1);
	mutex_enter(&rap->an_lock);
	rap->an_size -= 2;
	mutex_exit(&rap->an_lock);
	VN_RELE(rvp);

	kmem_free(aip->ai_addr.buf, aip->ai_addr.len);
	kmem_free(aip->ai_path, aip->ai_pathlen);
	kmem_free(aip->ai_opts, aip->ai_optslen);
	kmem_free(aip->ai_map, aip->ai_maplen);
	kmem_free(aip, sizeof (*aip));

	module_keepcnt--;
	return (0);
}

/*
 * find root of autofs
 */
static int
auto_root(
	vfs_t *vfsp,
	vnode_t **vpp
)
{
	*vpp = (vnode_t *)vfstoai(vfsp)->ai_rootvp;
	/*
	 * this is the only place where we get a chance
	 * to time stamp the autonode for direct mounts
	 */
	vntoan(*vpp)->an_ref_time = hrestime.tv_sec;

#ifdef AUTODEBUG
	auto_dprint(autodebug, 5, "auto_root vfsp %x vnode %x\n",
		vfsp, *vpp);
#endif
	VN_HOLD(*vpp);
	return (0);
}

/*
 * Get file system statistics.
 */
static int
auto_statvfs(
	register vfs_t *vfsp,
	statvfs_t *sbp
)
{

#ifdef AUTODEBUG
	auto_dprint(autodebug, 4, "auto_statvfs %x\n", vfsp);
#endif

	bzero((caddr_t)sbp, (int)sizeof (*sbp));
	sbp->f_bsize	= vfsp->vfs_bsize;
	sbp->f_frsize	= sbp->f_bsize;
	sbp->f_blocks	= 0;
	sbp->f_bfree	= 0;
	sbp->f_bavail	= 0;
	sbp->f_files	= 0;
	sbp->f_ffree	= 0;
	sbp->f_favail	= 0;
	sbp->f_fsid	= vfsp->vfs_dev;
	strcpy(sbp->f_basetype, vfssw[vfsp->vfs_fstype].vsw_name);
	sbp->f_flag	= vf_to_stf(vfsp->vfs_flag);
	sbp->f_namemax	= MAXNAMELEN;
	strcpy(sbp->f_fstr, "autofs");

	return (0);
}
