#pragma ident  "@(#)tmp_tnode.c 1.32    95/04/20 SMI"
/*  tmp_tnode.c 1.20 90/05/10 SMI */

/*
 * Copyright (c) 1989, 1990, 1991 by Sun Microsystems, Inc.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/t_lock.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <sys/sysmacros.h>
#include <sys/proc.h>
#include <sys/disp.h>
#include <sys/user.h>
#include <sys/time.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/stat.h>
#include <sys/mode.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <vm/seg.h>
#include <vm/seg_map.h>
#include <vm/anon.h>
#include <vm/page.h>
#include <vm/pvn.h>
#include <sys/fs/tmp.h>
#include <sys/fs/tmpnode.h>
#include <sys/debug.h>
#include <sys/cmn_err.h>
#include <sys/swap.h>
#include <sys/vtrace.h>

/*
 * Reserve swap space for the size of the file.
 * Called before growing a file (i.e. ftruncate, write)
 * Returns 0 on success.
 */
int
tmp_resv(
	register struct tmount *tm,
	register struct tmpnode *tp,
	register u_int delta,		/* size needed */
	register int pagecreate)	/* call anon_resv if set */
{
	u_int pages = btopr(delta);

	TMP_PRINT(T_DEBUG, "tmp_resv: tm %x tp %x delta %d pagecreate %d\n",
	    tm, tp, delta, pagecreate, 0);
	ASSERT(RW_WRITE_HELD(&tp->tn_rwlock));
	ASSERT(tp->tn_type == VREG);
	/*
	 * pagecreate is set only if we actually need to call anon_resv
	 * to reserve an additional page of anonymous memory.
	 * Since anon_resv always reserves a page at a time,
	 * it should only get called when we know we're growing the
	 * file into a new page or filling a hole.
	 *
	 * Deny if trying to reserve more than tmpfs can allocate
	 */
	if (pagecreate && ((tm->tm_anonmem + pages > tm->tm_anonmax) ||
	    (!anon_checkspace(ptob(pages + tmpfs_minfree))) ||
	    (anon_resv(delta) == 0))) {
		/* XXX remove for FCS? */
		if (tm->tm_anonmem + pages > tm->tm_anonmax)
			/*EMPTY*/
			TMP_PRINT(T_ALLOC,
			    "anonmem %x + pages %x > anonmax %x\n",
			    tm->tm_anonmem, pages, tm->tm_anonmax, 0, 0);
		if (!anon_checkspace(pages + tmpfs_minfree))
			/*EMPTY*/
			TMP_PRINT(T_ALLOC,
			    "not swap for pages %d + minfree %d\n",
			    pages, tmpfs_minfree, 0, 0, 0);
		return (1);
	}

	if (pagecreate)
		/*EMPTY*/
		TMP_PRINT(T_ALLOC, "tmp_resv: %d pages allocated\n",
		    pages, 0, 0, 0, 0);

	/*
	 * update statistics
	 */
	if (pagecreate) {
		mutex_enter(&tm->tm_contents);
		tm->tm_anonmem += pages;
		mutex_exit(&tm->tm_contents);

		TRACE_5(TR_FAC_VM, TR_ANON_TMPFS, "anon tmpfs:%u %u %u %u %u",
			TNTOV(tp), tp->tn_anon, roundup(tp->tn_size, PAGESIZE),
			roundup(delta, PAGESIZE), 1);
	}
	return (0);
}

/*
 * tmp_unresv - called when truncating a file
 * Only called if we're freeing at least pagesize bytes
 * because anon_unresv does a btopr(delta)
 */
static void
tmp_unresv(
	register struct tmount *tm,
	register struct tmpnode *tp,
	register u_int delta)
{
	TMP_PRINT((T_DEBUG | T_ALLOC), "tmp_unresv: tm %x tp %x delta %d\n",
	    tm, tp, delta, 0, 0);
	ASSERT(RW_WRITE_HELD(&tp->tn_rwlock));
	ASSERT(tp->tn_type == VREG);

	anon_unresv(delta);

	mutex_enter(&tm->tm_contents);
	tm->tm_anonmem -= btopr(delta);
	mutex_exit(&tm->tm_contents);

	TRACE_5(TR_FAC_VM, TR_ANON_TMPFS, "anon tmpfs:%u %u %u %u %u",
		TNTOV(tp), tp->tn_anon, roundup(tp->tn_size - delta, PAGESIZE),
		roundup(delta, PAGESIZE), 0);
}

/*
 * Called when referencing a tmpnode
 */
void
tmpnode_hold(struct tmpnode *tp)
{
	struct vnode *vp = TNTOV(tp);
	struct tmount *tm = VTOTM(TNTOV(tp));

	TMP_PRINT((T_DEBUG | T_ALLOC), "tmpnode_hold: tp %x\n", tp, 0, 0, 0, 0);

	VN_HOLD(vp);
	/*
	 * If tmpnode wasn't referenced, we mark it as such here and
	 * record the fact in the tmount structure for this file system
	 */
	mutex_enter(&tp->tn_tlock);
	if ((tp->tn_flags & TREF) == 0) {
		tp->tn_flags |= TREF;
		mutex_exit(&tp->tn_tlock);
		INCR_COUNT(&tm->tm_filerefcnt, &tm->tm_contents);
	} else {
		mutex_exit(&tp->tn_tlock);
	}
}

void
tmpnode_rele(struct tmpnode *tp)
{
	struct vnode *vp = TNTOV(tp);

	/*
	 * tm_filerefcnt is decremented in tmpnode_inactive
	 */

	TMP_PRINT((T_DEBUG | T_ALLOC),
	    "tmpnode_rele: tp %x nlink %d count %d\n",
	    tp, tp->tn_nlink, vp->v_count, 0, 0);

	/*
	 * This thread shouldn't be holding the contents lock
	 * on this tmpnode because inactive could be called
	 * via vn_rele
	 */
	VN_RELE(vp);
}

/*
 * TMAP_ALLOC is the number of bytes to grow the size of an anon array
 * when needed.  The anon array is bigger than needed so we don't need
 * to allocate a new one every time we grow the file.
 */
#define	TMAP_ALLOC (32 * PAGESIZE)

/*
 * Grow the anon pointer array to cover 'offset' bytes plus slack.
 */
void
tmpnode_growmap(struct tmpnode *tp, u_int offset)
{
	register int i, end, oldsize = tp->tn_asize, newsize;
	register struct anon **newapp, **oldapp;

	ASSERT(RW_WRITE_HELD(&tp->tn_rwlock));
	ASSERT(RW_WRITE_HELD(&tp->tn_contents));
	ASSERT(tp->tn_type == VREG);

	if (oldsize > offset)
		return;
	/*
	 * Calculate new length, rounding up in TMAP_ALLOC clicks
	 * to avoid reallocating the anon array each time the file grows.
	 */
	newsize = ((offset + TMAP_ALLOC) / TMAP_ALLOC) * TMAP_ALLOC;
	if (newsize < 0)
		newsize = MAXOFF_T;

	TMP_PRINT(T_DEBUG, "tmpnode_growmap: tp %x oldsize %x newsize %x\n",
	    tp, oldsize, newsize, 0, 0);

	newapp = (struct anon **)
	    kmem_zalloc(btopr(newsize) * sizeof (struct anon *), KM_SLEEP);
	TMP_PRINT(T_ALLOC,
	    "tmpnode_growmap allocate new anonarray %x size %d\n",
	    newapp, btopr(newsize) * sizeof (struct anon *), 0, 0, 0);

	oldapp = tp->tn_anon;

	/* Copy old array (if it exists). The rwlock protects it. */
	if (oldapp != NULL) {
		end = btopr(oldsize);
		for (i = 0; i < end;  i++)
			newapp[i] = oldapp[i];
		TMP_PRINT(T_ALLOC,
		    "tmpnode_growmap: freeing old anonarray %x size %d\n",
		    oldapp, btopr(oldsize) * sizeof (struct anon *), 0, 0, 0);
		kmem_free((char *)oldapp, end * sizeof (struct anon *));
	}
	tp->tn_asize = newsize;
	tp->tn_anon = newapp;
}

/*
 * Allocate a tmpnode and add it to file list under mount point.
 *
 * Returns initialized and held tmpnode on success.
 */

struct tmpnode *
tmpnode_alloc(struct tmount *tm, struct vattr *vap, struct cred *cred)
{
	struct tmpnode *t;
	struct vnode *vp;

	TMP_PRINT(T_DEBUG, "tmpnode_alloc: tm %x type %d\n",
	    tm, vap->va_type, 0, 0, 0);
	ASSERT(vap != NULL);
	ASSERT(cred != NULL);

	/*
	 * No tm locks should be held by this thread.
	 */
	t = (struct tmpnode *)tmp_memalloc(tm, sizeof (struct tmpnode));
	if (t == NULL)
		return (NULL);
	rw_init(&t->tn_rwlock, "tmpnode rwlock", RW_DEFAULT, DEFAULT_WT);
	mutex_init(&t->tn_tlock, "tmpnode modtime lock",
	    MUTEX_DEFAULT, DEFAULT_WT);
	t->tn_mode = MAKEIMODE(vap->va_type, vap->va_mode);
	t->tn_mask = 0;
	t->tn_type = vap->va_type;
	t->tn_nodeid = tmp_imapalloc(tm);
	t->tn_nlink = 1;
	t->tn_size = 0;
	t->tn_uid = cred->cr_uid;
	t->tn_gid = cred->cr_gid;

	t->tn_fsid = tm->tm_dev;
	t->tn_rdev = vap->va_rdev;
	t->tn_blksize = PAGESIZE;
	t->tn_nblocks = 0;
	tmp_created(t);
	t->tn_dir = NULL;

	vp = TNTOV(t);
	mutex_init(&vp->v_lock, "tmpfs v_lock", MUTEX_DEFAULT, DEFAULT_WT);
	vp->v_flag = 0;
	vp->v_count = 0;	/* incremented in tmpnode_hold */
	vp->v_vfsmountedhere = 0;
	vp->v_op = &tmp_vnodeops;
	vp->v_vfsp = tm->tm_vfsp;
	vp->v_stream = (struct stdata *)NULL;
	vp->v_pages = (struct page *)NULL;
	vp->v_type = vap->va_type;
	vp->v_rdev = vap->va_rdev;
	vp->v_data = (caddr_t)t;
	vp->v_filocks = (struct filock *)0;

	/*
	 * Hold the tmpnode before adding it the to the list of tmpnodes.
	 */
	tmpnode_hold(t);

	mutex_enter(&tm->tm_contents);
	/*
	 * Increment the pseudo generation number for this tmpnode.
	 * Since tmpnodes are allocated and freed, there really is no
	 * particular generation number for a new tmpnode.  Just fake it
	 * by using a counter in each file system.
	 */
	t->tn_gen = tm->tm_gen++;

	switch (t->tn_type) {
	case VDIR:
		tm->tm_directories++;
		break;
	case VREG:
	case VBLK:
	case VCHR:
	case VLNK:
	case VFIFO:
		tm->tm_files++;
		break;
	default:
		cmn_err(CE_PANIC, "tmpnode_alloc: unknown file type 0x%x\n",
		    (int)t->tn_type);
		/*NOTREACHED*/
		break;
	}

	/*
	 * This assertion verifies that there is no way someone could
	 * unmount this filesystem while we are allocating a new file
	 */
	if (tm->tm_rootnode != NULL)
		ASSERT(tm->tm_filerefcnt >= 1);

	/*
	 * Add new tmpnode to end of linked list of tmpnodes for this tmpfs
	 * Root directory is handled specially in tmp_mount.
	 */
	if (tm->tm_rootnode != (struct tmpnode *)NULL) {
		t->tn_forw = NULL;
		t->tn_back = tm->tm_rootnode->tn_back;
		t->tn_back->tn_forw = tm->tm_rootnode->tn_back = t;
	}
	mutex_exit(&tm->tm_contents);

	INCR_COUNT(&tmp_files, &tmpfs_mutex);
	TMP_PRINT(T_ALLOC, "tmpnode_alloc: returning tp %x\n", t, 0, 0, 0, 0);
	return (t);
}


/*
 * tmpnode_trunc - set length of tmpnode and deal with resources
 */
int
tmpnode_trunc(
	struct tmount *tm,
	struct tmpnode *tp,
	u_long newsize,
	struct cred *cred)
{
	register u_int oldsize = tp->tn_size;
	register u_int delta;
	struct vnode *vp = TNTOV(tp);
	int error = 0;

	ASSERT(RW_WRITE_HELD(&tp->tn_rwlock));
	ASSERT(RW_WRITE_HELD(&tp->tn_contents));

	TMP_PRINT((T_DEBUG | T_ALLOC),
	    "tmpnode_trunc: tp %x oldsz %d newsz %d type %d\n",
	    tp, oldsize, newsize, tp->tn_type, 0);

	if (newsize == oldsize) {
		/* Required by POSIX */
		mutex_enter(&tp->tn_tlock);
		tp->tn_flags |= (TUPD | TCHG);
		mutex_exit(&tp->tn_tlock);
		tmp_timestamp(tp, tp->tn_flags);
		goto out;
	}

	switch (tp->tn_type) {
	case VREG:
		/* Growing the file */
		if (newsize > oldsize) {
			delta = roundup(newsize, PAGESIZE) -
					roundup(oldsize, PAGESIZE);
			/*
			 * Grow the size of the anon array to the new size
			 * Reserve the space for the growth here.
			 * We do it this way for now because this is how
			 * tmpfs used to do it, and this way the reserved
			 * space is alway equal to the file size.
			 * Alternatively, we could wait to reserve space 'til
			 * someone tries to store into one of the newly
			 * trunc'ed up pages. This would give us behavior
			 * identical to ufs; i.e., you could fail a
			 * fault on storing into a holey region of a file
			 * if there is no space in the filesystem to fill
			 * the hole at that time.
			 */
			TMP_PRINT(T_ALLOC, "ttrunc: growing %d bytes\n",
			    newsize - oldsize, 0, 0, 0, 0);
			/*
			 * tmp_resv calls anon_resv only if we're extending
			 * the file into a new page
			 */
			if (tmp_resv(tm, tp, delta,
			    (btopr(newsize) != btopr(oldsize)))) {
				error = ENOSPC;
				goto out;
			}
			tmpnode_growmap(tp, (u_int)newsize);
			tp->tn_size = newsize;
			break;
		}

		/* Free anon pages if shrinking file over page boundary. */
		if (btopr(newsize) != btopr(oldsize)) {
			u_int freed;
			delta = roundup(oldsize, PAGESIZE) -
				roundup(newsize, PAGESIZE);
			TMP_PRINT(T_ALLOC, "ttrunc: shrinking %d bytes",
			    delta, 0, 0, 0, 0);
			freed = btop(anon_pages(tp->tn_anon,
			    (u_long) btopr(newsize), btopr(delta)));
			tp->tn_nblocks -= freed;
			anon_free(&tp->tn_anon[btopr(newsize)], (u_int)delta);
			tmp_unresv(tm, tp, delta);
		}

		/*
		 * Update the file size now to reflect the pages we just
		 * blew away as we're about to drop the
		 * contents lock to zero the partial page (which could
		 * re-enter tmpfs via getpage and try to reacquire the lock)
		 * Once we drop the lock, faulters can fill in holes in
		 * the file and if we haven't updated the size they
		 * may fill in holes that are beyond EOF, which will then
		 * never get cleared.
		 */
		tp->tn_size = newsize;
		/* Zero new size of file to page boundary. */
		if (tp->tn_anon[btop(newsize)] != NULL) {
			u_int zlen = PAGESIZE - (newsize & PAGEOFFSET);

			rw_exit(&tp->tn_contents);
			pvn_vpzero(TNTOV(tp), newsize, zlen);
			rw_enter(&tp->tn_contents, RW_WRITER);
		}

		if (newsize == 0) {
			/* Delete anon array for tmpnode */
			ASSERT(tp->tn_nblocks == 0);
			ASSERT(tp->tn_anon[0] == NULL);
			ASSERT(vp->v_pages == NULL);
			kmem_free((char *)tp->tn_anon,
			    btopr(tp->tn_asize) * sizeof (struct anon *));
			tp->tn_anon = NULL;
			tp->tn_asize = 0;
		}
		break;
	case VLNK:
		/*
		 * Don't do anything here
		 * tmpnode_free frees the memory
		 */
		if (newsize != 0)
			error = EINVAL;
		goto out;
	case VDIR:
		/*
		 * Remove all the directory entries under this directory.
		 */
		if (newsize != 0) {
			error = EINVAL;
			goto out;
		}
		tdirtrunc(tm, tp, cred);
		ASSERT(tp->tn_nlink == 0);
		break;
	default:
		goto out;
	}
	tmp_modified(tp);
out:
	return (error);
}

/*
 * Free resources associated with the tmpnode (but don't destroy the
 * node itself or unlink it from fs's list of nodes).
 * This thread can't come in holding tm_contents mutex.
 */
void
tmpnode_free(struct tmount *tm, struct tmpnode *tp, struct cred *cred)
{
	int error;

	ASSERT(RW_WRITE_HELD(&tp->tn_rwlock));
	ASSERT(tp->tn_nlink == 0);

	TMP_PRINT((T_DEBUG | T_ALLOC),
	    "tmpnode_free: tp %x nlink %d vcount %d type %d\n",
	    tp, tp->tn_nlink, TNTOV(tp)->v_count, tp->tn_type, 0);

#ifdef TMPFSDEBUG
	/*
	 * We shouldn't be able to find a directory entry pointing to
	 * this tmpnode
	 */
	if (tmpcheck && tmp_findentry(tm, tp)) {
		printf("tmpnode_free: found direntry for tp %x\n", tp);
		ASSERT(!tmp_findentry(tm, tp));
	}
#endif TMPFSDEBUG

	switch (tp->tn_type) {
	case VDIR:
		/* directory should already have been truncated */
		ASSERT(tp->tn_dir == NULL);
		DECR_COUNT(&tm->tm_directories, &tm->tm_contents);
		break;
	case VLNK:
		TMP_PRINT(T_ALLOC, "tmpnode_free: freeing symlink\n",
		    0, 0, 0, 0, 0);
		if (tp->tn_size)
			tmp_memfree(tm, (char *)tp->tn_symlink,
			    (u_int)tp->tn_size + 1);
		tp->tn_size = 0;
		DECR_COUNT(&tm->tm_files, &tm->tm_contents);
		break;
	case VREG:
		rw_enter(&tp->tn_contents, RW_WRITER);
		if (error = tmpnode_trunc(tm, tp, (u_long)0, cred))
			cmn_err(CE_PANIC,
"tmpnode_free: error %d trunc file %x type %d\n",
			    error, (int)tp, tp->tn_type);
		rw_exit(&tp->tn_contents);
		ASSERT(tp->tn_size == 0);
		ASSERT(tp->tn_nblocks == 0);
		DECR_COUNT(&tm->tm_files, &tm->tm_contents);
		break;
	case VFIFO:
	case VBLK:
	case VCHR:
		DECR_COUNT(&tm->tm_files, &tm->tm_contents);
		break;
	default:
		cmn_err(CE_PANIC, "tmpnode_free: unknown file type 0x%x\n",
		    (int)tp);
		/*NOTREACHED*/
		break;
	}

	DECR_COUNT(&tmp_files, &tmpfs_mutex);
	tmp_imapfree(tm, tp->tn_nodeid);
}
