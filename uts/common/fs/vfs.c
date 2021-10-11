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
 * 	(c) 1986,1987,1988,1989,1993  Sun Microsystems, Inc
 * 	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *		All rights reserved.
 *
 */

#pragma ident	"@(#)vfs.c	1.56	94/12/20 SMI"	/* SVr4 1.42	*/

#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/user.h>
#include <sys/fstyp.h>
#include <sys/kmem.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/mount.h>
#include <sys/vfs.h>
#include <sys/statvfs.h>
#include <sys/statfs.h>
#include <sys/cred.h>
#include <sys/vnode.h>
#include <sys/dnlc.h>
#include <sys/file.h>
#include <sys/time.h>
#include <sys/cmn_err.h>
#include <sys/buf.h>
#include <sys/swap.h>
#include <sys/debug.h>
#include <sys/vnode.h>
#include <sys/modctl.h>
#include <sys/ddi.h>
#include <sys/pathname.h>

#include <vm/page.h>

/*
 * VFS global data.
 */
vnode_t *rootdir;		/* pointer to root vnode. */

static struct vfs root;
struct vfs *rootvfs = &root;	/* pointer to root vfs; head of VFS list. */
struct vfs **rvfs_head;		/* array of vfs ptrs for vfs hash list */
kmutex_t *rvfs_lock;		/* array of locks for vfs hash list */
int vfshsz = 512;		/* # of heads/locks in vfs hash arrays */
				/* must be power of 2!	*/

/*
 * VFS system calls: mount, umount, syssync, statfs, fstatfs, statvfs,
 * fstatvfs, and sysfs moved to common/syscall.
 */

/*
 * Update every mounted file system.  We call the vfs_sync operation of
 * each file system type, passing it a NULL vfsp to indicate that all
 * mounted file systems of that type should be updated.
 */
void
vfs_sync(int flag)
{
	register int i;

	for (i = 1; i < nfstype; i++) {
		RLOCK_VFSSW();
		if (vfssw[i].vsw_vfsops) {
		    (void) (*vfssw[i].vsw_vfsops->vfs_sync)(NULL, flag, CRED());
		}
		RUNLOCK_VFSSW();
	}
}

void
sync()
{
	vfs_sync(0);
}

/*
 * External routines.
 */
kmutex_t vfslist;	/* lock for accessing the vfs linked list */
krwlock_t vfssw_lock;	/* lock accesses to vfssw */

/*
 * vfs_mountroot is called by main() to mount the root filesystem.
 */
void
vfs_mountroot()
{
	int i;
	char buf[20];
	extern modrootloaded;

	/*
	 * First, initialize the mutex's used by vfs.
	 */
	mutex_init(&vfslist, "vfs list lock", MUTEX_DEFAULT, NULL);
	rw_init(&vfssw_lock, "vfssw lock", RW_DEFAULT, NULL);

	/*
	 * Alloc the vfs hash bucket array and locks
	 */
	rvfs_head = (struct vfs **) kmem_zalloc(vfshsz * sizeof (struct vfs *),
					KM_SLEEP);
	rvfs_lock = (kmutex_t *) kmem_zalloc(vfshsz * sizeof (kmutex_t),
					KM_SLEEP);

	for (i = 0; i < vfshsz; i++) {
		(void) sprintf(buf, "vfs list $ %d", i);
		mutex_init(&rvfs_lock[i], buf, MUTEX_DEFAULT, DEFAULT_WT);
	}

	/*
	 * Call machine-dependent routine "rootconf" to choose a root
	 * file system type.
	 */
	if (rootconf())
		cmn_err(CE_PANIC, "vfs_mountroot: cannot mount root");
	/*
	 * Get vnode for '/'.  Set up rootdir, u.u_rdir and u.u_cdir
	 * to point to it.  These are used by lookuppn() so that it
	 * knows where to start from ('/' or '.').
	 */
	if (VFS_ROOT(rootvfs, &rootdir))
		cmn_err(CE_PANIC, "vfs_mountroot: no root vnode");
	u.u_cdir = rootdir;
	VN_HOLD(u.u_cdir);
	u.u_rdir = NULL;
	/*
	 * Notify the module code that it can begin using the
	 * root filesystem instead of the boot program's services.
	 */
	modrootloaded = 1;
}


int
dounmount(register struct vfs *vfsp, register cred_t *cr)
{
	register vnode_t *coveredvp;
	int error;

	/*
	 * Get covered vnode.
	 */
	coveredvp = vfsp->vfs_vnodecovered;
	ASSERT(coveredvp->v_flag & VVFSLOCK);

	/*
	 * Purge all dnlc entries for this vfs.
	 */
	(void) dnlc_purge_vfsp(vfsp, 0);

	VFS_SYNC(vfsp, 0, cr);

	/*
	 * Lock vnode to maintain fs status quo during unmount.  This
	 * has to be done after the sync because ufs_update tries to acquire
	 * the vfs_reflock.
	 */
	vfs_lock_wait(vfsp);

	if (error = VFS_UNMOUNT(vfsp, cr)) {
		vn_vfsunlock(coveredvp);
		vfs_unlock(vfsp);
	} else {
		--coveredvp->v_vfsp->vfs_nsubmounts;
		ASSERT(vfsp->vfs_nsubmounts == 0);
		vfs_remove(vfsp);
		vn_vfsunlock(coveredvp);
		mutex_destroy(&vfsp->vfs_reflock);
		kmem_free((caddr_t)vfsp, (u_int)sizeof (*vfsp));
		VN_RELE(coveredvp);
	}
	return (error);
}


/*
 * Vfs_unmountall() is called by uadmin() to unmount all
 * mounted file systems (except the root file system) during shutdown.
 * It follows the existing locking protocol when traversing the vfslist
 * to sync and unmount vfses. Even though there should be no
 * other thread running while the system is shutting down, it is prudent
 * to still follow the locking protocol.
 */
void
vfs_unmountall(void)
{
	register struct vfs *vfsp, *head_vfsp, *last_vfsp;
	int nvfs, i;
	struct vfs **unmount_list;

	/*
	 * Construct a list of vfses that we plan to unmount.
	 * Lock the covered vnode to avoid the race condiiton
	 * caused by another unmount. Skip those vfses that we cannot
	 * lock the covered vnodes.
	 */
	mutex_enter(&vfslist);

	for (vfsp = rootvfs->vfs_next, head_vfsp = last_vfsp = NULL,
	    nvfs = 0; vfsp != NULL; vfsp = vfsp->vfs_next) {
		/*
		 * skip any vfs that we cannot acquire the vfslock()
		 */
		if (vfs_lock(vfsp) == 0) {
			if (vn_vfslock(vfsp->vfs_vnodecovered) == 0) {

				/*
				 * put in the list of vfses to be unmounted
				 */
				if (last_vfsp)
					last_vfsp->vfs_list = vfsp;
				else
					head_vfsp = vfsp;
				last_vfsp = vfsp;

				nvfs++;
			} else
				vfs_unlock(vfsp);
		}
	}

	if (nvfs == 0) {
		mutex_exit(&vfslist);
		return;
	}

	last_vfsp ->vfs_list = NULL;

	unmount_list = (struct vfs **) kmem_alloc(nvfs *
	    sizeof (struct vfs *), KM_SLEEP);

	for (vfsp = head_vfsp, i = 0; vfsp != NULL; vfsp = vfsp->vfs_list) {
		unmount_list[i++] = vfsp;
		vfs_unlock(vfsp);
	}

	/*
	 * Once covered vnode is locked, no one can unmount the vfs.
	 * It is now safe to drop the vfslist mutex.
	 */

	mutex_exit(&vfslist);

	ASSERT(i == nvfs);

	for (i = 0; i < nvfs; i++)
		VFS_SYNC(unmount_list[i], SYNC_CLOSE, CRED());

	for (i = 0; i < nvfs; i++)
		(void) dounmount(unmount_list[i], CRED());

	kmem_free((caddr_t) unmount_list,
	    (u_int) (nvfs * sizeof (struct vfs *)));
}

/*
 * vfs_add is called by a specific filesystem's mount routine to add
 * the new vfs into the vfs list/hash and to cover the mounted-on vnode.
 * The vfs should already have been locked by the caller.
 *
 * coveredvp is zero if this is the root.
 */
void
vfs_add(coveredvp, vfsp, mflag)
	register vnode_t *coveredvp;
	register struct vfs *vfsp;
	int mflag;
{
	int vhno = VFSHASH(vfsp->vfs_fsid.val[0], vfsp->vfs_fsid.val[1]);

	ASSERT(MUTEX_HELD(&vfsp->vfs_reflock));
	if (coveredvp != NULL) {
		struct vfs **hp;

		mutex_enter(&vfslist);
		mutex_enter(&rvfs_lock[vhno]);
		vfsp->vfs_next = rootvfs->vfs_next;
		rootvfs->vfs_next = vfsp;

		/*
		 * Insert the VFS onto the end of the hash list so LOFS
		 * with the same fsid as UFS (or other) file systems will not
		 * hide the UFS.
		 */
		for (hp = &rvfs_head[vhno]; *hp != NULL; hp = &(*hp)->vfs_hash)
			;
		vfsp->vfs_hash = NULL;
		*hp = vfsp;			/* put VFS on end of hash */
		mutex_exit(&rvfs_lock[vhno]);
		mutex_exit(&vfslist);
		ASSERT(coveredvp->v_flag & VVFSLOCK);
		coveredvp->v_vfsmountedhere = vfsp;
	} else {
		/*
		 * This is the root of the whole world.  This is only
		 * called from mount root so no locking is needed.
		 */
		mutex_enter(&vfslist);
		mutex_enter(&rvfs_lock[vhno]);
		vfsp->vfs_next = rootvfs->vfs_next;
		rootvfs = vfsp;
		vfsp->vfs_hash = rvfs_head[vhno];
		rvfs_head[vhno] = vfsp;
		mutex_exit(&rvfs_lock[vhno]);
		mutex_exit(&vfslist);
	}
	vfsp->vfs_vnodecovered = coveredvp;

	if (mflag & MS_RDONLY)
		vfsp->vfs_flag |= VFS_RDONLY;
	else
		vfsp->vfs_flag &= ~VFS_RDONLY;

	if (mflag & MS_NOSUID)
		vfsp->vfs_flag |= VFS_NOSUID;
	else
		vfsp->vfs_flag &= ~VFS_NOSUID;
}

/*
 * Remove a vfs from the vfs list, and destroy pointers to it.
 * Should be called by filesystem "unmount" code after it determines
 * that an unmount is legal but before it destroys the vfs.
 */
void
vfs_remove(vfsp)
	register struct vfs *vfsp;
{
	register struct vfs *tvfsp;
	register vnode_t *vp;
	int vhno = VFSHASH(vfsp->vfs_fsid.val[0], vfsp->vfs_fsid.val[1]);

	ASSERT(MUTEX_HELD(&vfsp->vfs_reflock));

	/*
	 * Can't unmount root.  Should never happen because fs will
	 * be busy.
	 */
	if (vfsp == rootvfs)
		cmn_err(CE_PANIC, "vfs_remove: unmounting root");

	mutex_enter(&vfslist);
	mutex_enter(&rvfs_lock[vhno]);
	/*
	 * Remove from hash
	 */
	if (rvfs_head[vhno] == vfsp) {
		rvfs_head[vhno] = vfsp->vfs_hash;
		goto foundit;
	}
	for (tvfsp = rvfs_head[vhno]; tvfsp != NULL; tvfsp = tvfsp->vfs_hash) {
		if (tvfsp->vfs_hash == vfsp) {
			tvfsp->vfs_hash = vfsp->vfs_hash;
			goto foundit;
		}
	}
	cmn_err(CE_WARN, "vfs_remove: vfs not found in hash");

foundit:
	for (tvfsp = rootvfs; tvfsp != NULL; tvfsp = tvfsp->vfs_next) {
		if (tvfsp->vfs_next == vfsp) {
			/*
			 * Remove vfs from list, unmount covered vp.
			 */
			tvfsp->vfs_next = vfsp->vfs_next;
			mutex_exit(&rvfs_lock[vhno]);
			mutex_exit(&vfslist);
			vp = vfsp->vfs_vnodecovered;
			ASSERT(vp->v_flag & VVFSLOCK);
			vp->v_vfsmountedhere = NULL;
			/*
			 * Release lock and wakeup anybody waiting.
			 */
			vfs_unlock(vfsp);
			return;
		}
	}
	mutex_exit(&rvfs_lock[vhno]);
	mutex_exit(&vfslist);
	/*
	 * Can't find vfs to remove.
	 */
	cmn_err(CE_PANIC, "vfs_remove: vfs not found");
}

/*
 * Lock a filesystem to prevent access to it while mounting,
 * unmounting and syncing.  Return EBUSY immediately if lock
 * can't be acquired.
 */
int
vfs_lock(vfs_t *vfsp)
{
	if (mutex_tryenter(&vfsp->vfs_reflock) == 0)
		return (EBUSY);
	return (0);
}

void
vfs_lock_wait(vfs_t *vfsp)
{
	mutex_enter(&vfsp->vfs_reflock);
}

/*
 * Unlock a locked filesystem.
 */
void
vfs_unlock(vfs_t *vfsp)
{
	mutex_exit(&vfsp->vfs_reflock);
}

struct vfs *
getvfs(fsid_t *fsid)
{
	struct vfs *vfsp;
	long val0 = fsid->val[0];
	long val1 = fsid->val[1];
	int vhno = VFSHASH(val0, val1);
	kmutex_t *hmp = &rvfs_lock[vhno];

	mutex_enter(hmp);
	for (vfsp = rvfs_head[vhno]; vfsp; vfsp = vfsp->vfs_hash) {
		if (vfsp->vfs_fsid.val[0] == val0 &&
		    vfsp->vfs_fsid.val[1] == val1) {
			mutex_exit(hmp);
			return (vfsp);
		}
	}
	mutex_exit(hmp);
	return (NULL);
}

/*
 * Search the vfs list for a specified device.  Returns a pointer to it
 * or NULL if no suitable entry is found.
 */
struct vfs *
vfs_devsearch(dev)
	dev_t dev;
{
	register struct vfs *vfsp;

	mutex_enter(&vfslist);
	for (vfsp = rootvfs; vfsp != NULL; vfsp = vfsp->vfs_next)
		if (vfsp->vfs_dev == dev)
			break;
	mutex_exit(&vfslist);
	return (vfsp);
}

/*
 * Search the vfs list for a specified vfsops.
 */

struct vfs *
vfs_opssearch(struct vfsops *ops)
{
	register struct vfs *vfsp;

	mutex_enter(&vfslist);
	for (vfsp = rootvfs; vfsp != NULL; vfsp = vfsp->vfs_next)
		if (vfsp->vfs_op == ops)
			break;
	mutex_exit(&vfslist);
	return (vfsp);
}

/*
 * Allocate an entry in vfssw for a file system type
 */

struct vfssw *
allocate_vfssw(char *type)
{
	register struct vfssw *vswp;

	ASSERT(VFSSW_WRITE_LOCKED());
	for (vswp = &vfssw[1]; vswp < &vfssw[nfstype]; vswp++)
		if (!ALLOCATED_VFSSW(vswp)) {
			vswp->vsw_name =
			    (char *)kmem_alloc(strlen(type) + 1, KM_SLEEP);
			strcpy(vswp->vsw_name, type);
			return (vswp);
		}
	return (NULL);
}

/*
 * Impose additional layer of translation between vfstype names
 * and module names in the filesystem.
 */
static char *
vfs_to_modname(char *vfstype)
{
	if (strcmp(vfstype, "proc") == 0) {
		vfstype = "procfs";
	} else if (strcmp(vfstype, "fd") == 0) {
		vfstype = "fdfs";
	} else if (strncmp(vfstype, "nfs", 3) == 0) {
		vfstype = "nfs";
	}

	return (vfstype);
}

/*
 * Find a vfssw entry given a file system type name.
 * Try to autoload the filesystem if it's not found.
 * If it's installed, return the vfssw locked to prevent unloading.
 */
struct vfssw *
vfs_getvfssw(type)
	register char *type;
{
	register struct vfssw *vswp;
	register char	*modname;
	extern int modrootloaded;
	int rval;

	RLOCK_VFSSW();
	if ((vswp = vfs_getvfsswbyname(type)) == NULL) {
		RUNLOCK_VFSSW();
		WLOCK_VFSSW();
		if ((vswp = vfs_getvfsswbyname(type)) == NULL) {
			if ((vswp = allocate_vfssw(type)) == NULL) {
				WUNLOCK_VFSSW();
				return (NULL);
			}
		}
		WUNLOCK_VFSSW();
		RLOCK_VFSSW();
	}

	modname = vfs_to_modname(type);

	/*
	 * Try to load the filesystem.
	 */
	if (!VFS_INSTALLED(vswp)) {
		RUNLOCK_VFSSW();
		if (modrootloaded)
			rval = modload("fs", modname);
		else
			rval = modloadonly("fs", modname);

		if (rval == -1)
			return (NULL);
		RLOCK_VFSSW();
	}

	return (vswp);
}

/*
 * Find a vfssw entry given a file system type name.
 */
struct vfssw *
vfs_getvfsswbyname(type)
	register char *type;
{
	register int i;

	ASSERT(VFSSW_LOCKED());
	if (type == NULL || *type == '\0')
		return (NULL);

	for (i = 1; i < nfstype; i++)
		if (strcmp(type, vfssw[i].vsw_name) == 0)
			return (&vfssw[i]);

	return (NULL);
}

/*
 * "sync" all file systems, and return only when all writes have been
 * completed.  For use by the reboot code; it's verbose.
 */
void
vfs_syncall()
{
	int iter;
	int nppbusy, nbpbusy;
	extern modrootloaded;
	extern int sync_timeout;

	/* root fs not mounted, don't sync.  We don't need double panic */
	if (!modrootloaded)
		return;

	/*
	 * set fs sync timeout counter so that it scales with the
	 * of dirty pages.  The minimum timeout setting here is 20
	 * seconds.
	 */
	sync_timeout = (page_busy() + bio_busy() + 10) * 2 * (HZ);

	printf("syncing file systems...");
	/*
	 * XXX - do we need to have a way to have sync
	 * force the writeback in the case of keep'ed pages?
	 */
	sync();

	for (iter = 1; iter <= 20; iter++) {
		nbpbusy = bio_busy();
		nppbusy = page_busy();

		if (nbpbusy == 0 && nppbusy == 0)
			break;
		if (nbpbusy != 0)
			printf(" [%d]", nbpbusy);
		if (nppbusy != 0)
			printf(" %d", nppbusy);
		DELAY(36000 * iter);
	}
	printf(" done\n");
	sync_timeout = 0;	/* clear fs sync timeout counter */
}

/*
 * Map VFS flags to statvfs flags.  These shouldn't really be separate
 * flags at all.
 */
u_long
vf_to_stf(vf)
	u_long vf;
{
	u_long stf = 0;

	if (vf & VFS_RDONLY)
		stf |= ST_RDONLY;
	if (vf & VFS_NOSUID)
		stf |= ST_NOSUID;
	if (vf & VFS_NOTRUNC)
		stf |= ST_NOTRUNC;

	return (stf);
}

/*
 * Use old-style function prototype for vfsstray() so
 * that we can use it anywhere in the vfsops structure.
 */
int vfsstray();
/*
 * Entries for (illegal) fstype 0.
 */
/* ARGSUSED */
int
vfsstray_sync(struct vfs *vfsp, short arg, struct cred *cr)
{
	cmn_err(CE_PANIC, "stray vfs operation");
	return (0);
}

struct vfsops vfs_strayops = {
	vfsstray,
	vfsstray,
	vfsstray,
	vfsstray,
	vfsstray_sync,
	vfsstray,
	vfsstray,
	vfsstray
};

/*
 * Entries for (illegal) fstype 0.
 */
int
vfsstray()
{
	cmn_err(CE_PANIC, "stray vfs operation");
	return (0);
}

/*
 * Called from startup() to initialize all loaded vfs's
 */
void
vfsinit(void)
{
	register int i;

	/*
	 * fstype 0 is (arbitrarily) invalid.
	 */
	vfssw[0].vsw_vfsops = &vfs_strayops;
	vfssw[0].vsw_name = "BADVFS";

	/*
	 * Call all the init routines.
	 */
	/*
	 * A mixture of loadable and non-loadable filesystems
	 * is tricky to support, because of contention over exactly
	 * when the filesystems vsw_init() routine should be
	 * run on the rootfs -- at this point in the boot sequence, the
	 * rootfs module has  been loaded into the table, but its _init()
	 * routine and the vsw_init() routine have yet to be called - this
	 * will happen when we actually do the proper modload() in rootconf().
	 *
	 * So we use the following heuristic.  For each name in the
	 * switch with a non-nil init routine, we look for a module
	 * of the appropriate name - if it exists, we infer that
	 * the loadable module code has either already vsw_init()-ed
	 * it, or will vsw_init() soon.  If it can't be found there, then
	 * we infer this is a statically configured filesystem so we get on
	 * and call its vsw_init() routine directly.
	 *
	 * Sigh.  There's got to be a better way to do this.
	 */
	ASSERT(VFSSW_LOCKED());		/* the root fs */
	RUNLOCK_VFSSW();
	for (i = 1; i < nfstype; i++) {
		RLOCK_VFSSW();
		if (vfssw[i].vsw_init) {
			struct modctl *mod_find_by_filename(char *, char *);
			char *modname;

			modname = vfs_to_modname(vfssw[i].vsw_name);
			/*
			 * XXX	Should probably hold the mod_lock here
			 */
			if (!mod_find_by_filename("fs", modname))
				(*vfssw[i].vsw_init)(&vfssw[i], i);
		}
		RUNLOCK_VFSSW();
	}
	RLOCK_VFSSW();
}
