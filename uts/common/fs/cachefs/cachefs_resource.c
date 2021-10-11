/*
 * Copyright (c) 1992, Sun Microsystems, Inc.
 * All rights reserved.
 */
#pragma ident   "@(#)cachefs_resource.c 1.53     95/06/20 SMI"

#include <sys/param.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/cred.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/pathname.h>
#include <sys/uio.h>
#include <sys/tiuser.h>
#include <sys/sysmacros.h>
#include <sys/kmem.h>
#include <sys/mount.h>
#include <sys/ioctl.h>
#include <sys/statvfs.h>
#include <sys/errno.h>
#include <sys/debug.h>
#include <sys/cmn_err.h>
#include <sys/utsname.h>
#include <sys/bootconf.h>
#include <sys/modctl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <sys/callb.h>

#include <vm/hat.h>
#include <vm/as.h>
#include <vm/page.h>
#include <vm/pvn.h>
#include <vm/seg.h>
#include <vm/seg_map.h>
#include <vm/seg_vn.h>
#include <vm/rm.h>
#include <sys/fs/cachefs_fs.h>

extern kmutex_t cachefs_cnode_freelist_lock;
extern time_t time;

/* forward references */
struct lru_pointers *cachefs_lru_ptrs_get(cachefscache_t *cachep, u_int entno);
struct lru_idents *cachefs_lru_idents_get(cachefscache_t *cachep, u_int entno);
void cachefs_garbage_collect_queue(cachefscache_t *cachep);
static time_t cachefs_lru_front_atime(cachefscache_t *cachep);

#define	C_LRU_MASK	(~((MAXBSIZE / sizeof (struct lru_pointers) - 1)))
#define	C_LRU_WINDOW(ENTNO)	(ENTNO & C_LRU_MASK)
#define	C_LRUP_OFFSET(CACHEP, WINDOW)	((CACHEP)->c_lru_link_off + \
	((WINDOW) * sizeof (struct lru_pointers)))
#define	C_LRUI_OFFSET(CACHEP, WINDOW)	((CACHEP)->c_lru_idents_off + \
	((WINDOW) * sizeof (struct lru_idents)))

/*
 * This function frees the specific entry (adds it to the free pointers in the
 * LRU chain).
 * The assumption is that the caller has already unlinked the entry from the
 * LRU chains (by calling lru_unlink_entry prior to this call)
 * It is an error to try to free an entry that's sitting on the LRU chains
 */
void
cachefs_lru_free(struct cachefscache *cachep, u_int entno)
{
	struct lru_pointers *lru_ptr_ent;

	ASSERT((cachep->c_flags & (CACHE_NOCACHE|CACHE_NOFILL)) == 0);
	mutex_enter(&cachep->c_contentslock);

	cachefs_cache_dirty(cachep, 0);
	lru_ptr_ent = cachefs_lru_ptrs_get(cachep, entno);
	lru_ptr_ent->lru_fwd_idx = cachep->c_lruinfo.lru_free;
	lru_ptr_ent->lru_bkwd_idx = 0;
	cachep->c_lruinfo.lru_free = entno;

	mutex_exit(&cachep->c_contentslock);
}

/*
 * This function plucks a slot from the LRU free list and creates an LRU entry.
 */

int
cachefs_lru_alloc(struct cachefscache *cachep, struct fscache *fscp,
    u_int *entnop, int ent_fileno)
{
	int error = 0;
	u_int entno;
	struct lru_pointers *lru_ptr_ent;
	struct lru_idents *lru_ident_ent;
	int new = 0;

	ASSERT((cachep->c_flags & (CACHE_NOCACHE|CACHE_NOFILL)) == 0);
	mutex_enter(&cachep->c_contentslock);

	cachefs_cache_dirty(cachep, 0);
	entno = cachep->c_lruinfo.lru_free;
	if (entno == 0) {
		if (cachep->c_lruinfo.lru_entries >=
			cachep->c_label.cl_maxinodes) {
			error = ENOMEM;
			goto out;
		}
		entno = ++(cachep->c_lruinfo.lru_entries);
		new++;
	}

	lru_ptr_ent = cachefs_lru_ptrs_get(cachep, entno);
	if (new == 0)
		cachep->c_lruinfo.lru_free = lru_ptr_ent->lru_fwd_idx;
	else
		cachep->c_lruinfo.lru_free = 0;
	lru_ptr_ent->lru_fwd_idx = 0;
	lru_ptr_ent->lru_bkwd_idx = 0;

	lru_ident_ent = cachefs_lru_idents_get(cachep, entno);
	lru_ident_ent->lru_fsid = fscp->fs_cfsid;
	lru_ident_ent->lru_fileno = ent_fileno;
	lru_ident_ent->lru_local = 0;
	lru_ident_ent->lru_attrc = 0;
out:
	mutex_exit(&cachep->c_contentslock);
	if (error == 0)
		*entnop = entno;
	return (error);
}

/*
 * Adds the specified entry to the LRU list.
 */
void
cachefs_lru_add(struct cachefscache *cachep, u_int entno)
{
	struct lru_pointers *lru_ent;
	u_int prev_entno = 0;

	ASSERT(entno);
	if (cachep->c_flags & (CACHE_NOCACHE|CACHE_NOFILL))
		return;
	mutex_enter(&cachep->c_contentslock);
	cachefs_cache_dirty(cachep, 0);
	lru_ent = cachefs_lru_ptrs_get(cachep, entno);
	ASSERT(lru_ent->lru_bkwd_idx == 0);
	ASSERT(lru_ent->lru_fwd_idx == 0);
	lru_ent->lru_fwd_idx = cachep->c_lruinfo.lru_back;
	lru_ent->lru_bkwd_idx = 0;
	if (cachep->c_lruinfo.lru_back != 0) {
		ASSERT(cachep->c_lruinfo.lru_front != 0);
		prev_entno = cachep->c_lruinfo.lru_back;
	} else {
		ASSERT(cachep->c_lruinfo.lru_front == 0);
		cachep->c_lruinfo.lru_front = entno;
	}
	cachep->c_lruinfo.lru_back = entno;
	if (prev_entno) {
		lru_ent = cachefs_lru_ptrs_get(cachep, prev_entno);
		lru_ent->lru_bkwd_idx = entno;
	}
out:
	mutex_exit(&cachep->c_contentslock);
}

/*
 * Removes the entry from the LRU list.
 */
void
cachefs_lru_remove(struct cachefscache *cachep, int entno)
{
	u_int prev, next;
	struct lru_pointers *lru_ent;

	ASSERT(entno);
	if (cachep->c_flags & (CACHE_NOCACHE|CACHE_NOFILL))
		return;
	mutex_enter(&cachep->c_contentslock);
	cachefs_cache_dirty(cachep, 0);
	lru_ent = cachefs_lru_ptrs_get(cachep, entno);
	next = lru_ent->lru_fwd_idx;
	prev = lru_ent->lru_bkwd_idx;
	lru_ent->lru_fwd_idx = 0;
	lru_ent->lru_bkwd_idx = 0;
	if (cachep->c_lruinfo.lru_back == 0 ||
	    cachep->c_lruinfo.lru_front == 0) {
		ASSERT(cachep->c_lruinfo.lru_back == 0 &&
			cachep->c_lruinfo.lru_front == 0);
	}
	if (cachep->c_lruinfo.lru_back == entno) {
		cachep->c_lruinfo.lru_back = next;
	}
	if (cachep->c_lruinfo.lru_front == entno) {
		cachep->c_lruinfo.lru_front = prev;
	}
	if (prev) {
		lru_ent = cachefs_lru_ptrs_get(cachep, prev);
		lru_ent->lru_fwd_idx = next;
	}
	if (next) {
		lru_ent = cachefs_lru_ptrs_get(cachep, next);
		lru_ent->lru_bkwd_idx = prev;
	}

out:
	mutex_exit(&cachep->c_contentslock);
}

/*
 * Adds the specified entry to the ACTIVE list.
 */
void
cachefs_active_add(struct cachefscache *cachep, u_int entno)
{
	struct lru_pointers *lru_ent;
	u_int prev_entno = 0;

	ASSERT(entno);
	if (cachep->c_flags & (CACHE_NOCACHE|CACHE_NOFILL))
		return;
	mutex_enter(&cachep->c_contentslock);
	cachefs_cache_dirty(cachep, 0);
	lru_ent = cachefs_lru_ptrs_get(cachep, entno);
	ASSERT(lru_ent->lru_bkwd_idx == 0);
	ASSERT(lru_ent->lru_fwd_idx == 0);
	lru_ent->lru_fwd_idx = cachep->c_lruinfo.active_back;
	lru_ent->lru_bkwd_idx = 0;
	if (cachep->c_lruinfo.active_back != 0) {
		ASSERT(cachep->c_lruinfo.active_front != 0);
		prev_entno = cachep->c_lruinfo.active_back;
	} else {
		ASSERT(cachep->c_lruinfo.active_front == 0);
		cachep->c_lruinfo.active_front = entno;
	}
	cachep->c_lruinfo.active_back = entno;
	if (prev_entno) {
		lru_ent = cachefs_lru_ptrs_get(cachep, prev_entno);
		lru_ent->lru_bkwd_idx = entno;
	}
out:
	mutex_exit(&cachep->c_contentslock);
}

/*
 * Removes the entry from the ACTIVE list.
 */
void
cachefs_active_remove(struct cachefscache *cachep, u_int entno)
{
	u_int prev, next;
	struct lru_pointers *lru_ent;

	ASSERT(entno);
	if (cachep->c_flags & (CACHE_NOCACHE|CACHE_NOFILL))
		return;
	mutex_enter(&cachep->c_contentslock);
	cachefs_cache_dirty(cachep, 0);
	lru_ent = cachefs_lru_ptrs_get(cachep, entno);
	next = lru_ent->lru_fwd_idx;
	prev = lru_ent->lru_bkwd_idx;
	lru_ent->lru_fwd_idx = 0;
	lru_ent->lru_bkwd_idx = 0;
	if (cachep->c_lruinfo.active_back == 0 ||
	    cachep->c_lruinfo.active_front == 0) {
		ASSERT(cachep->c_lruinfo.active_back == 0 &&
			cachep->c_lruinfo.active_front == 0);
	}
	if (cachep->c_lruinfo.active_back == entno) {
		cachep->c_lruinfo.active_back = next;
	}
	if (cachep->c_lruinfo.active_front == entno) {
		cachep->c_lruinfo.active_front = prev;
	}
	if (prev) {
		lru_ent = cachefs_lru_ptrs_get(cachep, prev);
		lru_ent->lru_fwd_idx = next;
	}
	if (next) {
		lru_ent = cachefs_lru_ptrs_get(cachep, next);
		lru_ent->lru_bkwd_idx = prev;
	}

out:
	mutex_exit(&cachep->c_contentslock);
}

/*
 * Moves the contents of the active list to the lru list.
 */
void
cachefs_move_active_to_lru(cachefscache_t *cachep)
{
	struct lru_info *infop = &cachep->c_lruinfo;
	struct lru_pointers *lru_ent;

	mutex_enter(&cachep->c_contentslock);

	/* quit if nothing on the active list */
	if (infop->active_back == 0)
		goto out;

	/* if lru list is empty, just move active list over */
	if (infop->lru_back == 0) {
		infop->lru_front = infop->active_front;
		infop->lru_back = infop->active_back;
	}

	/* else if lru list is not empty */
	else {
		/* get the pointer to the back of the lru list */
		lru_ent = cachefs_lru_ptrs_get(cachep, infop->lru_back);

		/* chain it to the front of the active list */
		lru_ent->lru_bkwd_idx = infop->active_front;

		/* get the pointer to the front of the active list */
		lru_ent = cachefs_lru_ptrs_get(cachep, infop->active_front);

		/* chain it to the back of the lru list */
		lru_ent->lru_fwd_idx = infop->lru_back;

		/* update the lru list back pointer */
		infop->lru_back = infop->active_back;
	}

	/* mark active list as empty */
	infop->active_front = 0;
	infop->active_back = 0;

out:
	mutex_exit(&cachep->c_contentslock);
}

/*
 * Sets/clears the lru_local bit in the lru_idents for the
 * specified entry.
 * Local in this context means a "local" file or pinned.
 *	islocal == 1 means set local bit
 *	islocal == 0 means clear local bit
 *	islocal == -1 means do not change local bit
 * Returns previous local state.
 */
int
cachefs_lru_local(struct cachefscache *cachep, int entno, int islocal)
{
	struct lru_idents *lru_ident_ent;
	int ret;

	ASSERT((cachep->c_flags & (CACHE_NOCACHE|CACHE_NOFILL)) == 0);
	mutex_enter(&cachep->c_contentslock);
	cachefs_cache_dirty(cachep, 0);
	lru_ident_ent = cachefs_lru_idents_get(cachep, entno);
	ret = lru_ident_ent->lru_local;
	if (islocal != -1)
		lru_ident_ent->lru_local = islocal;

	mutex_exit(&cachep->c_contentslock);
	return (ret);
}

/*
 * Sets/clears the lru_attrc bit in the lru_idents for the
 * specified entry.
 *	isattrc == 1 means set attrc bit
 *	isattrc == 0 means clear attrc bit
 *	isattrc == -1 means do not change attrc bit
 * Returns previous attrc state.
 */
int
cachefs_lru_attrc(struct cachefscache *cachep, int entno, int isattrc)
{
	struct lru_idents *lru_ident_ent;
	int ret;

	ASSERT((cachep->c_flags & (CACHE_NOCACHE|CACHE_NOFILL)) == 0);
	mutex_enter(&cachep->c_contentslock);
	cachefs_cache_dirty(cachep, 0);
	lru_ident_ent = cachefs_lru_idents_get(cachep, entno);
	ret = lru_ident_ent->lru_attrc;
	if (isattrc != -1)
		lru_ident_ent->lru_attrc = isattrc;

	mutex_exit(&cachep->c_contentslock);
	return (ret);
}

int
cachefs_allocfile(cachefscache_t *cachep)
{
	int error = 0;
	int collect = 0;
	struct statvfs sb;
	int used;

	(void) VFS_STATVFS(cachep->c_dirvp->v_vfsp, &sb);
	used = sb.f_files - sb.f_ffree;

	mutex_enter(&cachep->c_contentslock);

	/* if there are no more available inodes */
	if ((cachep->c_usage.cu_filesused >= cachep->c_label.cl_maxinodes) ||
	    ((cachep->c_usage.cu_filesused > cachep->c_label.cl_filemin) &&
	    (used > cachep->c_label.cl_filetresh))) {
		error = ENOSPC;
		if ((cachep->c_flags & CACHE_GARBAGE_COLLECT) == 0)
			collect = 1;
	}

	/* else if there are more available inodes */
	else {
		cachefs_cache_dirty(cachep, 0);
		cachep->c_usage.cu_filesused++;
		if (((cachep->c_flags & CACHE_GARBAGE_COLLECT) == 0) &&
		    (cachep->c_usage.cu_filesused >=
		    cachep->c_label.cl_filehiwat))
			collect = 1;
	}

	mutex_exit(&cachep->c_contentslock);

	if (collect)
		cachefs_garbage_collect_queue(cachep);

	return (error);
}

void
cachefs_freefile(cachefscache_t *cachep)
{
	mutex_enter(&cachep->c_contentslock);
	ASSERT(cachep->c_usage.cu_filesused > 0);
	cachefs_cache_dirty(cachep, 0);
	cachep->c_usage.cu_filesused--;
	mutex_exit(&cachep->c_contentslock);
}

/*ARGSUSED*/ /* we really should get rid of `cr' someday */
int
cachefs_allocblocks(cachefscache_t *cachep, int nblks, cred_t *cr)
{
	int error = 0;
	int collect = 0;
	struct statvfs sb;
	int used;
	int blocks;

	(void) VFS_STATVFS(cachep->c_dirvp->v_vfsp, &sb);
	used = ((sb.f_blocks - sb.f_bfree) * sb.f_frsize) / MAXBSIZE;

	mutex_enter(&cachep->c_contentslock);

	/* if there are no more available blocks */
	blocks = cachep->c_usage.cu_blksused + nblks;
	if ((blocks >= cachep->c_label.cl_maxblks) ||
	    ((blocks > cachep->c_label.cl_blockmin) &&
	    (used > cachep->c_label.cl_blocktresh))) {
		error = ENOSPC;
		if ((cachep->c_flags & CACHE_GARBAGE_COLLECT) == 0)
			collect = 1;
	}

	/* else if there are more available blocks */
	else {
		cachefs_cache_dirty(cachep, 0);
		cachep->c_usage.cu_blksused += nblks;

		if (((cachep->c_flags & CACHE_GARBAGE_COLLECT) == 0) &&
		    (cachep->c_usage.cu_blksused >=
		    cachep->c_label.cl_blkhiwat))
			collect = 1;
	}

	mutex_exit(&cachep->c_contentslock);

	if (collect)
		cachefs_garbage_collect_queue(cachep);

	return (error);
}

void
cachefs_freeblocks(cachefscache_t *cachep, int nblks)
{
	mutex_enter(&cachep->c_contentslock);
	cachefs_cache_dirty(cachep, 0);
	cachep->c_usage.cu_blksused -= nblks;
	ASSERT(cachep->c_usage.cu_blksused >= 0);
	mutex_exit(&cachep->c_contentslock);
}

int
cachefs_victim(cachefscache_t *cachep)
{
	u_int entno;
	struct lru_idents *lru_ident_ent;
	struct lru_pointers *lru_ent;
	int error;
	int fsid;
	u_int fileno;
	struct fscache *fscp;
	struct filegrp *fgp;
	struct cachefs_metadata md;
	struct cnode *cp;
	int isattrc;

	ASSERT((cachep->c_flags & (CACHE_NOCACHE|CACHE_NOFILL)) == 0);
	fscp = NULL;
	fgp = NULL;

	/* get the fileno and fsid of the first item on the lru list */
	mutex_enter(&cachep->c_contentslock);
	entno = cachep->c_lruinfo.lru_front;
	if (entno == 0) {
		mutex_exit(&cachep->c_contentslock);
		error = ENOSPC;
		goto out;
	}
	lru_ident_ent = cachefs_lru_idents_get(cachep, entno);
	fsid = lru_ident_ent->lru_fsid;
	fileno = lru_ident_ent->lru_fileno;
	isattrc = lru_ident_ent->lru_attrc;
	mutex_exit(&cachep->c_contentslock);

	/* get the file system cache object for this fsid */
	mutex_enter(&cachep->c_fslistlock);
	fscp = fscache_list_find(cachep, fsid);
	if (fscp == NULL) {
		fscp = fscache_create(cachep);
		error = fscache_activate(fscp, fsid, NULL, NULL);
		if (error) {
			cmn_err(CE_WARN,
			    "cachefs: cache corruption, run fsck\n");
			fscache_destroy(fscp);
			fscp = NULL;
			mutex_exit(&cachep->c_fslistlock);
			error = 0;
			goto out;
		}
		fscache_list_add(cachep, fscp);
	}
	fscache_hold(fscp);
	mutex_exit(&cachep->c_fslistlock);

	/* get the file group object for this fileno */
	mutex_enter(&fscp->fs_fslock);
	fgp = filegrp_list_find(fscp, fileno);
	if (fgp == NULL) {
		fgp = filegrp_create(fscp, fileno);
		if (fgp->fg_flags & CFS_FG_ALLOC_ATTR) {
			if (isattrc == 0) {
				cmn_err(CE_WARN,
				    "cachefs: cache corruption, run fsck\n");
				delay(5*HZ);
			}
			filegrp_destroy(fgp);
			error = 0;
			fgp = NULL;
			mutex_exit(&fscp->fs_fslock);
			goto out;
		}
		filegrp_list_add(fscp, fgp);
	}

	/* if we are victimizing an attrcache file */
	if (isattrc) {
		mutex_enter(&fgp->fg_mutex);
		/* if the filegrp is not writable */
		if ((fgp->fg_flags & CFS_FG_WRITE) == 0) {
			mutex_exit(&fgp->fg_mutex);
			error = EROFS;
			fgp = NULL;
			mutex_exit(&fscp->fs_fslock);
			goto out;
		}

		/* if the filegrp did not go active on us */
		if ((fgp->fg_count == 0) && (fgp->fg_header->ach_nffs == 0)) {
			mutex_exit(&fgp->fg_mutex);
			filegrp_list_remove(fscp, fgp);
			fgp->fg_header->ach_count = 0;
			filegrp_destroy(fgp);
		} else {
#ifdef CFSDEBUG
			printf("c_victim: filegrp went active"
				" %x %lu %d %d %d\n",
				(int) fgp, fgp->fg_fileno,
				fgp->fg_header->ach_lruno,
				fgp->fg_count, fgp->fg_header->ach_nffs);
#endif
			ASSERT((fgp->fg_flags & CFS_FG_LRU) == 0);
			mutex_exit(&fgp->fg_mutex);
		}
		fgp = NULL;
		error = 0;
		mutex_exit(&fscp->fs_fslock);
		goto out;
	}
	ASSERT((fgp->fg_flags & CFS_FG_ALLOC_FILE) == 0);
	filegrp_hold(fgp);
	mutex_exit(&fscp->fs_fslock);

	/* lock the file group against action by inactive or lookup */
	mutex_enter(&fgp->fg_gc_mutex);

	/* see if the item is no longer on the lru list */
	mutex_enter(&cachep->c_contentslock);
	entno = cachep->c_lruinfo.lru_front;
	if (entno == 0) {
		mutex_exit(&cachep->c_contentslock);
		error = ENOSPC;
		goto out;
	}
	lru_ident_ent = cachefs_lru_idents_get(cachep, entno);
	if ((fsid != lru_ident_ent->lru_fsid) ||
	    (fileno != lru_ident_ent->lru_fileno)) {
		mutex_exit(&cachep->c_contentslock);
		error = 0;
		goto out;
	}

	/*
	 * If we got this far then we hold the contents (lru) lock
	 * and the fg_gc_mutex for the file group that owns
	 * the fileno that is on the front of the lru list.
	 * At this point inactive and lookup cannot be actively
	 * working on a cnode for this fileno.
	 */

	/* remove the victim from the lru list, it is on the front */
	cachefs_cache_dirty(cachep, 0);
	lru_ent = cachefs_lru_ptrs_get(cachep, entno);
	cachep->c_lruinfo.lru_front = lru_ent->lru_bkwd_idx;

	if (cachep->c_lruinfo.lru_back == entno) {
		/* list is empty now */
		cachep->c_lruinfo.lru_back = 0;
	} else {
		/* update new front of list to break link to victim */
		lru_ent = cachefs_lru_ptrs_get(cachep,
				cachep->c_lruinfo.lru_front);
		lru_ent->lru_fwd_idx = 0;
	}
	mutex_exit(&cachep->c_contentslock);

	/* Get the metadata from the attrcache file */
	error = filegrp_read_metadata(fgp, fileno, &md);
	if (error)
		goto out;

	mutex_enter(&cachefs_cnode_freelist_lock);
	mutex_enter(&fscp->fs_cnodelock);

	/* see if a cnode exists for this fileno */
	(void) cfind(fileno, &md.md_cookie, fscp, &cp);

	if (cp) {
		ASSERT(cp->c_flags & CN_LRU);

		/* if the cnode is active */
		if ((cp->c_flags & CN_INACTIVE) == 0) {
			rw_enter(&cp->c_statelock, RW_WRITER);

			/* indicate it is no longer on the lru list */
			cp->c_flags &= ~CN_LRU;

			rw_exit(&cp->c_statelock);
			mutex_exit(&fscp->fs_cnodelock);
			mutex_exit(&cachefs_cnode_freelist_lock);
			goto out;
		}

		ASSERT((cp->c_flags & CN_STALE) == 0);
		cachefs_remhash(cp);
		cachefs_remfree(cp);
		(void) cachefs_cnode_cnt(-1);
		if (CTOV(cp)->v_pages) {
			VN_HOLD(CTOV(cp));
			(void) VOP_PUTPAGE(CTOV(cp), (offset_t) 0, 0,
			    B_INVAL, kcred);
		}
		ASSERT(CTOV(cp)->v_pages == NULL);
		cachefs_kmem_free((caddr_t)cp, sizeof (struct cnode));
	}

	mutex_exit(&fscp->fs_cnodelock);
	mutex_exit(&cachefs_cnode_freelist_lock);

	cachefs_removefrontfile(&md, fileno, fgp);
	(void) filegrp_destroy_metadata(fgp, fileno);
	cachefs_lru_free(cachep, entno);

out:
	if (fgp) {
		mutex_exit(&fgp->fg_gc_mutex);
		filegrp_rele(fgp);
	}
	if (fscp) {
		fscache_rele(fscp);
	}
	return (error);
}

void
cachefs_garbage_collect(cachefscache_t *cachep)
{
	int filelowat, blocklowat;
	longlong_t filelowatmax, blocklowatmax;
	int maxblks, maxfiles, threshblks, threshfiles;
	int error;
	struct cache_usage *cup = &cachep->c_usage;

	ASSERT((cachep->c_flags & (CACHE_NOCACHE|CACHE_NOFILL)) == 0);
	mutex_enter(&cachep->c_contentslock);
	ASSERT(cachep->c_flags & CACHE_GARBAGE_COLLECT);
	filelowat = cachep->c_label.cl_filelowat;
	blocklowat = cachep->c_label.cl_blklowat;
	maxblks = cachep->c_label.cl_maxblks;
	maxfiles = cachep->c_label.cl_maxinodes;
	threshblks = cachep->c_label.cl_blocktresh;
	threshfiles = cachep->c_label.cl_filetresh;
	mutex_exit(&cachep->c_contentslock);

	cachep->c_gc_count++;
	cachep->c_gc_time = time;
	cachep->c_gc_before = cachefs_lru_front_atime(cachep);

	/*
	 * since we're here, we're running out of blocks or files.
	 * file and block lowat are what determine how low we garbage
	 * collect.  in order to do any good, we should drop below
	 * maxblocks, threshblocks, or the current blocks, whichever
	 * is smaller (same goes for files).  however, we won't go
	 * below an arbitrary (small) minimum for each.
	 */

	/* move down for maxfiles and maxblocks */
	if ((filelowatmax = ((longlong_t) maxfiles * 7) / 10) < filelowat)
		filelowat = filelowatmax;
	if ((blocklowatmax = ((longlong_t) maxblks * 7) / 10) < blocklowat)
		blocklowat = blocklowatmax;

	/* move down for threshfiles and threshblocks */
	if ((filelowatmax = ((longlong_t) threshfiles * 7) / 10) < filelowat)
		filelowat = filelowatmax;
	if ((blocklowatmax = ((longlong_t) threshblks * 7) / 10) <
	    blocklowat)
		blocklowat = blocklowatmax;

	/* move down for current files and blocks */
	if ((filelowatmax = ((longlong_t) cup->cu_filesused * 7) / 10) <
	    filelowat)
		filelowat = filelowatmax;
	if ((blocklowatmax = ((longlong_t) cup->cu_blksused * 7) / 10) <
	    blocklowat)
		blocklowat = blocklowatmax;

	/* move up for an arbitrary minimum */
#define	MIN_BLKLO	640		/* 640*8192 == 5MB */
#define	MIN_FILELO	1000
	if (filelowat < MIN_FILELO)
		filelowat = MIN_FILELO;
	if (blocklowat < MIN_BLKLO)
		blocklowat = MIN_BLKLO;

	while (cup->cu_filesused > filelowat || cup->cu_blksused > blocklowat) {
		/* if the thread is to terminate */
		if (cachep->c_flags & CACHE_GARBAGE_THREADEXIT)
			break;

		error = cachefs_victim(cachep);
		if (error)
			break;
	}

	cachep->c_gc_after = cachefs_lru_front_atime(cachep);
}

/* main routine for the garbage collection thread */
void
cachefs_garbage_collect_thread(cachefscache_t *cachep)
{
	int error;
	flock_t fl;
	callb_cpr_t cprinfo;
	kmutex_t cpr_lock;

	/* lock the lock file for exclusive write access */
	fl.l_type = F_WRLCK;
	fl.l_whence = 0;
	fl.l_start = 0;
	fl.l_len = 1024;
	fl.l_sysid = 0;
	fl.l_pid = 0;
	error = VOP_FRLOCK(cachep->c_lockvp, F_SETLK, &fl, FWRITE,
		(offset_t)0, kcred);
	if (error) {
		cmn_err(CE_WARN,
			"cachefs: Can't lock Cache Lock File\n");
	}

	mutex_init(&cpr_lock, "cachefs cpr lock", MUTEX_DEFAULT, NULL);
	CALLB_CPR_INIT(&cprinfo, &cpr_lock, callb_generic_cpr,
		"cfs_gct");
	mutex_enter(&cpr_lock);
	mutex_enter(&cachep->c_contentslock);

	/* loop while the thread is allowed to run */
	while ((cachep->c_flags & CACHE_GARBAGE_THREADEXIT) == 0) {

		/* wait for a wakeup call */
		cachep->c_flags &= ~CACHE_GARBAGE_COLLECT;
		CALLB_CPR_SAFE_BEGIN(&cprinfo);
		mutex_exit(&cpr_lock);
		cv_wait(&cachep->c_gccv, &cachep->c_contentslock);
		mutex_enter(&cpr_lock);
		CALLB_CPR_SAFE_END(&cprinfo, &cpr_lock);

		/* if the thread is to terminate */
		if (cachep->c_flags & CACHE_GARBAGE_THREADEXIT)
			break;

		/* if garbage collection is to run */
		if (cachep->c_flags & CACHE_GARBAGE_COLLECT) {
			mutex_exit(&cachep->c_contentslock);
			cachefs_garbage_collect(cachep);

			/*
			 * Prevent garbage collection from running more
			 * than once every 30 seconds.  This address
			 * those cases which do not allow removing
			 * an item from the lru by keeping gc from
			 * being a spin loop.
			 */
			delay(30*HZ);
			mutex_enter(&cachep->c_contentslock);
		}
	}

	cachep->c_flags &= ~CACHE_GARBAGE_THREADRUN;
	cv_broadcast(&cachep->c_gchaltcv);
	CALLB_CPR_SAFE_BEGIN(&cprinfo);
	CALLB_CPR_EXIT(&cprinfo);
	mutex_exit(&cachep->c_contentslock);
	mutex_exit(&cpr_lock);
	mutex_destroy(&cpr_lock);

	/* unlock the lock file */
	fl.l_type = F_UNLCK;
	fl.l_whence = 0;
	fl.l_start = 0;
	fl.l_len = 1024;
	fl.l_sysid = 0;
	fl.l_pid = 0;
	error = VOP_FRLOCK(cachep->c_lockvp, F_SETLK, &fl,
		FWRITE, (offset_t)0, kcred);
	if (error) {
		cmn_err(CE_WARN, "cachefs: Can't unlock lock file\n");
	}

	thread_exit();
	/*NOTREACHED*/
}

/* queues up a request to run the garbage collection */
void
cachefs_garbage_collect_queue(cachefscache_t *cachep)
{
	ASSERT((cachep->c_flags & (CACHE_NOCACHE|CACHE_NOFILL)) == 0);
	mutex_enter(&cachep->c_contentslock);

	/* quit if there is no garbage collection thread */
	if ((cachep->c_flags & CACHE_GARBAGE_THREADRUN) == 0) {
		mutex_exit(&cachep->c_contentslock);
		return;
	}

	/* quit if garbage collection is already in progress */
	if (cachep->c_flags & CACHE_GARBAGE_COLLECT) {
		mutex_exit(&cachep->c_contentslock);
		return;
	}

	/* quit if there is no garbage to collect */
	if (cachep->c_lruinfo.lru_front == 0) {
		mutex_exit(&cachep->c_contentslock);
		return;
	}

	/* indicate garbage collecting is in progress */
	cachep->c_flags |= CACHE_GARBAGE_COLLECT;

	/* wake up the garbage collection thread */
	cv_signal(&cachep->c_gccv);

	mutex_exit(&cachep->c_contentslock);
}

/*
 * Returns a pointer to the lru_pointers object specified by entno.
 * entno must be valid.
 * cachep->c_contents lock must be set.
 * The returned pointer may not be used after the c_contents lock
 * is released.
 */
struct lru_pointers *
cachefs_lru_ptrs_get(cachefscache_t *cachep, u_int entno)
{
	u_int entwindow;
	struct lru_pointers *lru_ent;

	entwindow = C_LRU_WINDOW(entno);

	if (cachep->c_lru_ptrs == NULL || cachep->c_lp_window != entwindow) {
		if (cachep->c_lru_ptrs)
			segmap_release(segkmap, (caddr_t)cachep->c_lru_ptrs,
				(SM_WRITE | SM_ASYNC));
		cachep->c_lru_ptrs = (struct lru_pointers *)
				segmap_getmap(segkmap, cachep->c_resfilevp,
					/*LINTED alignment okay*/
					C_LRUP_OFFSET(cachep, entwindow));
		cachep->c_lp_window = entwindow;
	}
	lru_ent = &cachep->c_lru_ptrs[(entno - entwindow)];

	return (lru_ent);
}

/*
 * Returns a pointer to the lru_idents object specified by entno.
 * entno must be valid.
 * cachep->c_contents lock must be set.
 * The returned pointer may not be used after the c_contents lock
 * is released.
 */
struct lru_idents *
cachefs_lru_idents_get(cachefscache_t *cachep, u_int entno)
{
	u_int entwindow;
	struct lru_idents *lru_ident_ent;

	entwindow = C_LRU_WINDOW(entno);

	if (cachep->c_lru_idents == NULL || cachep->c_li_window != entwindow) {
		if (cachep->c_lru_idents)
			segmap_release(segkmap, (caddr_t)cachep->c_lru_idents,
				(SM_WRITE | SM_ASYNC));
		cachep->c_lru_idents = (struct lru_idents *)
			segmap_getmap(segkmap, cachep->c_resfilevp,
				/*LINTED alignment okay*/
				C_LRUI_OFFSET(cachep, entwindow));
		cachep->c_li_window = entwindow;
	}
	lru_ident_ent = &cachep->c_lru_idents[(entno - entwindow)];

	return (lru_ident_ent);
}

static time_t
cachefs_lru_front_atime(cachefscache_t *cachep)
{
	static char namebuf[MAXNAMELEN];

	struct lru_idents *lru_ident_ent;
	u_int entno, filegrp, fileno, fgsize;
	struct fscache *fscp;

	struct vnode *dirvp, *filevp;
	struct vattr va;

	int reledir = 0;
	int gotfile = 0;
	time_t rc = (time_t) 0;

	mutex_enter(&cachep->c_contentslock);
	entno = cachep->c_lruinfo.lru_front;
	if (entno == 0) {
		mutex_exit(&cachep->c_contentslock);
		goto out;
	}

	lru_ident_ent = cachefs_lru_idents_get(cachep, entno);
	fileno = lru_ident_ent->lru_fileno;
	mutex_enter(&cachep->c_fslistlock);
	if ((fscp = fscache_list_find(cachep, lru_ident_ent->lru_fsid))
	    == NULL) {
		mutex_exit(&cachep->c_fslistlock);
		mutex_exit(&cachep->c_contentslock);
		goto out;
	}

	if (lru_ident_ent->lru_attrc) {
		make_ascii_name(fileno, namebuf);
		dirvp = fscp->fs_fsattrdir;
	} else {
		dirvp = NULL;
		fgsize = fscp->fs_options.opt_fgsize;
		filegrp = ((fileno / fgsize) * fgsize);
		make_ascii_name(filegrp, namebuf);
		if (VOP_LOOKUP(fscp->fs_fscdirvp, namebuf,
		    &dirvp, (struct pathname *)NULL, 0,
		    (vnode_t *)NULL, kcred) == 0) {
			make_ascii_name(fileno, namebuf);
			reledir++;
		} else {
			mutex_exit(&cachep->c_fslistlock);
			mutex_exit(&cachep->c_contentslock);
			goto out;
		}
	}
	if (dirvp && VOP_LOOKUP(dirvp, namebuf, &filevp,
	    (struct pathname *) NULL, 0,
	    (vnode_t *) NULL, kcred) == 0) {
		gotfile = 1;
	}
	if (reledir)
		VN_RELE(dirvp);
	mutex_exit(&cachep->c_fslistlock);
	mutex_exit(&cachep->c_contentslock);

	if (gotfile) {
		va.va_mask = AT_ATIME;
		/*
		 * XXX: does getattr update atime?  If so, we
		 * need to set it back...
		 */
		if (VOP_GETATTR(filevp, &va, 0, kcred) == 0)
			rc = va.va_atime.tv_sec;
		VN_RELE(filevp);
	}

out:
	return (rc);
}
