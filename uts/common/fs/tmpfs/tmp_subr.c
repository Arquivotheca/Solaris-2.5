#pragma ident  "@(#)tmp_subr.c 1.22    94/11/16 SMI"
/*  tmp_subr.c 1.10 90/03/30 SMI */

/*
 * Copyright (c) 1989, 1990, 1991 by Sun Microsystems, Inc.
 */

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/param.h>
#include <sys/t_lock.h>
#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/debug.h>
#include <sys/time.h>
#include <sys/cmn_err.h>
#include <sys/vnode.h>
#include <sys/vfs.h>
#include <sys/cred.h>
#include <sys/kmem.h>
#include <sys/fs/tmp.h>
#include <sys/fs/tmpnode.h>


int tmpfsfstype;
dev_t tmpdev;

/*
 * The following are patchable variables limiting the amount of system
 * resources tmpfs can use.
 *
 * tmpfs_maxkmem limits the amount of kernel kmem_alloc memory
 * tmpfs can use for it's data structures (e.g. tmpnodes, directory entries)
 * It is not determined by setting a hard limit but rather as a percentage of
 * physical memory which is determined when tmpfs is first used in the system.
 *
 * tmpfs_minfree is the minimum amount of swap space that tmpfs leaves for
 * the rest of the system.  In other words, if the amount of free swap space
 * in the system (i.e. anoninfo.ani_free) drops below tmpfs_minfree, tmpfs
 * anon allocations will fail.
 *
 * There is also a per mount limit on the amount of swap space
 * (tmount.tm_anonmax) settable via a mount option.
 */
u_int tmpfs_maxkmem = 0;
u_int tmpfs_minfree = 0;

/*
 * Mutex to protect tmpfs global data (e.g. mount list, statistics)
 */
kmutex_t tmpfs_mutex;

/*
 * initialize global tmpfs locks and such
 * called when loading tmpfs module
 */
int
tmpfsinit(struct vfssw *vswp, int fstype)
{
	int dev;
	extern  void    tmpfs_hash_init();

	tmpfs_hash_init();
	mutex_init(&tmpfs_mutex, "tmpfs global data lock",
	    MUTEX_DEFAULT, DEFAULT_WT);
	tmpfsfstype = fstype;
	ASSERT(tmpfsfstype != 0);

	/*
	 * tmpfs_minfree doesn't need to be some function of configured
	 * swap space since it really is an absolute limit of swap space
	 * which still allows other processes to execute.
	 */
	if (tmpfs_minfree == 0)
		/*
		 * Set if not patched
		 */
		tmpfs_minfree = btopr(TMPMINFREE);

	/*
	 * The maximum amount of space tmpfs can allocate is
	 * TMPMAXPROCKMEM percent of physical memory
	 *
	 * XXX SHOULD BE PERCENTAGE OF KERNELMAP
	 */
	if (tmpfs_maxkmem == 0)
		tmpfs_maxkmem = MAX(PAGESIZE,
		    ptob(((u_int)physmem * TMPMAXPROCKMEM) / 100));

	vswp->vsw_vfsops = &tmp_vfsops;
	if ((dev = getudev()) == -1) {
		cmn_err(CE_WARN, "tmpfsinit: Can't get unique device number.");
		dev = 0;
	}
	tmpdev = makedevice(dev, 0);
	return (0);
}

/*
 * current (unique) timestamp for tmpnode updates
 */
static timestruc_t tmpuniqtime;

void
tmp_timestamp(struct tmpnode *tp, u_int flag)
{
	mutex_enter(&tp->tn_tlock);
	if ((flag & (TACC|TUPD|TCHG)) == 0) {
		mutex_exit(&tp->tn_tlock);
		return;
	}
	if (hrestime.tv_sec > tmpuniqtime.tv_sec ||
	    hrestime.tv_nsec > tmpuniqtime.tv_nsec) {
		tmpuniqtime.tv_sec = hrestime.tv_sec;
		tmpuniqtime.tv_nsec = hrestime.tv_nsec;
	} else {
		tmpuniqtime.tv_nsec++;
	}
	if (flag & TACC)
		tp->tn_atime = tmpuniqtime;
	if (flag & TUPD)
		tp->tn_mtime = tmpuniqtime;
	if (flag & TCHG)
		tp->tn_ctime = tmpuniqtime;
	tp->tn_flags &= ~(TACC|TUPD|TCHG);
	mutex_exit(&tp->tn_tlock);
}

#define	MODESHIFT	3

int
tmp_taccess(struct tmpnode *tp, int mode, struct cred *cred)
{
	/*
	 * Superuser always gets access
	 */
	if (cred->cr_uid == 0)
		return (0);
	/*
	 * Check access based on owner, group and
	 * public permissions in tmpnode.
	 */
	if (cred->cr_uid != tp->tn_uid) {
		mode >>= MODESHIFT;
		if (groupmember(tp->tn_gid, cred) == 0)
			mode >>= MODESHIFT;
	}
	if ((tp->tn_mode & mode) == mode)
		return (0);
	return (EACCES);
}

/*
 * tmpfs kernel memory allocation
 * does some bookkeeping, calls kmem_zalloc() for the honey
 *
 * NULL returned on failure.
 */
char *
tmp_memalloc(struct tmount *tm, u_int size)
{
	char *cp;

	TMP_PRINT(T_DEBUG, "tmp_memalloc: tm %x size %d\n", tm, size, 0, 0, 0);

	mutex_enter(&tm->tm_contents);
	if ((tmp_kmemspace + size) < tmpfs_maxkmem) {
		tm->tm_kmemspace += size;
		mutex_exit(&tm->tm_contents);

		mutex_enter(&tmpfs_mutex);
		tmp_kmemspace += size;
		mutex_exit(&tmpfs_mutex);

		cp = (char *)kmem_zalloc(size, KM_SLEEP);
		TMP_PRINT(T_ALLOC, "tmp_memalloc: allocated cp %x size %d\n",
		    cp, size, 0, 0, 0);
		return (cp);
	}
	mutex_exit(&tm->tm_contents);
	TMP_PRINT(T_ALLOC, "tmp_memalloc: FAILED allocation size %d\n",
	    size, 0, 0, 0, 0);
	cmn_err(CE_WARN,
"tmp_memalloc: Memory allocation failed. tmpfs over memory limit\n");
	return (NULL);
}

/*
 * tmpfs kernel memory freer
 * does some bookkeeping, calls kmem_free()
 */
void
tmp_memfree(struct tmount *tm, char *cp, u_int size)
{
	TMP_PRINT(T_DEBUG, "tmp_memfree: tm %x cp %x size %d\n",
	    tm, cp, size, 0, 0);

	mutex_enter(&tm->tm_contents);
	if (tm->tm_kmemspace < size)
		cmn_err(CE_PANIC, "tmp_memfree: kmem %d size %d tmp_kmem %d\n",
		    tm->tm_kmemspace, size, tmp_kmemspace);
	tm->tm_kmemspace -= size;
	mutex_exit(&tm->tm_contents);

	TMP_PRINT(T_ALLOC, "tmp_memfree: freeing cp %x size %d\n",
	    cp, size, 0, 0, 0);
	kmem_free(cp, size);

	mutex_enter(&tmpfs_mutex);
	tmp_kmemspace -= size;
	mutex_exit(&tmpfs_mutex);
}

/*
 * Allocate and return an unused index number
 * (similar to inode #) for a new tmpfs file.
 */
long
tmp_imapalloc(struct tmount *tm)
{
	register struct tmpimap *tmapp;
	register int i, id;

	mutex_enter(&tm->tm_contents);
	for (i = 0, tmapp = tm->tm_inomap; tmapp;
	    i++, tmapp = tmapp->timap_next) {
		for (id = 0; id < TMPIMAPNODES; id++)
			if (!TESTBIT(tmapp->timap_bits, id))
				break;
		if (id < TMPIMAPNODES) {
			SETBIT(tmapp->timap_bits, id);
			mutex_exit(&tm->tm_contents);
			return ((i * TMPIMAPNODES) + id);
		}
		if (tmapp->timap_next == NULL) {
			int size = sizeof (struct tmpimap);
			/*
			 * We don't use tmp_memalloc here because it gets
			 * the tm_contents mutex on its own which would
			 * require us to drop it here.  We can't do that
			 * because it would allow someone else to allocate
			 * the same file we're trying to.
			 */
			if ((tmp_kmemspace + size) >= tmpfs_maxkmem) {
				mutex_exit(&tm->tm_contents);
				cmn_err(CE_WARN,
"tmp_imapalloc: Memory allocation failed. tmpfs over memory limit\n");
				return (-1);
			}
			tmapp->timap_next = (struct tmpimap *)
			    kmem_zalloc(sizeof (struct tmpimap), KM_SLEEP);
			tm->tm_kmemspace += size;
			mutex_enter(&tmpfs_mutex);
			tmp_kmemspace += size;
			mutex_exit(&tmpfs_mutex);
			TMP_PRINT(T_ALLOC,
			    "tmp_imapalloc: allocated tmpimap 0x%x size %d\n",
			    tmapp->timap_next, sizeof (struct tmpimap),
			    0, 0, 0);
		}
	}
	mutex_exit(&tm->tm_contents);
	cmn_err(CE_PANIC, "tmp_imapalloc: Can't allocate new nodeid\n");
	/*NOTREACHED*/
	return (-1);
}

/*
 * Free index number of a (being destroyed) tmpfs file.
 *
 * Return non-zero value if node-id not found (shouldn't happen...)
 */
void
tmp_imapfree(struct tmount *tm, ino_t number)
{
	register int i;
	register struct tmpimap *tmapp;

	mutex_enter(&tm->tm_contents);
	for (i = 1, tmapp = tm->tm_inomap; tmapp;
	    i++, tmapp = tmapp->timap_next) {
		if (number < i * TMPIMAPNODES) {
			CLEARBIT(tmapp->timap_bits, (number % TMPIMAPNODES));
			mutex_exit(&tm->tm_contents);
			return;
		}
	}
	mutex_exit(&tm->tm_contents);
	cmn_err(CE_WARN, "tmp_imapfree: Couldn't free nodeid %d\n",
	    (int)number);
}

#ifdef TMPFSDEBUG
#include <sys/stat.h>

/*
 * Routine to check for existing tmpnode in list hanging off mount struct
 * Used to verify existance before accessing.
 * Return 1 if tmpnode found, 0 if not.
 */
int
tmp_findnode(struct tmount *tm, struct tmpnode *tp)
{
	struct tmpnode *xtp;

	mutex_enter(&tm->tm_contents);
	for (xtp = tm->tm_rootnode; xtp; xtp = xtp->tn_forw) {
		if (xtp == tp) {
			mutex_exit(&tm->tm_contents);
			return (1);
		}
	}
	mutex_exit(&tm->tm_contents);
	return (0);
}

/*
 * Routine to check for existance of direntry pointing at tmpnode.
 * Used to verify that there isn't an entry before destroying the tmpnode
 */
int
tmp_findentry(struct tmount *tm, struct tmpnode *tp)
{
	struct tmpnode *xtp;
	struct tdirent *tdp;

	mutex_enter(&tm->tm_contents);
	for (xtp = tm->tm_rootnode; xtp; xtp = xtp->tn_forw) {
		if (xtp->tn_type == VDIR) {
			mutex_enter(&xtp->tn_tlock);
			for (tdp = xtp->tn_dir; tdp; tdp->td_next) {
				if (tdp->td_tmpnode == tp) {
					mutex_exit(&xtp->tn_tlock);
					mutex_exit(&tm->tm_contents);
					return (1);
				}
			}
			mutex_exit(&xtp->tn_tlock);
		}
	}
	mutex_exit(&tm->tm_contents);
	return (0);
}
#endif TMPFSDEBUG
