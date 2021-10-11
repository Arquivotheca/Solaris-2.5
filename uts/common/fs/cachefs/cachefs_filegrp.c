/*
 * Copyright (c) 1992, Sun Microsystems, Inc.
 * All rights reserved.
 */
#pragma ident   "@(#)cachefs_filegrp.c 1.29     94/08/05 SMI"

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

#include <vm/hat.h>
#include <vm/as.h>
#include <vm/page.h>
#include <vm/pvn.h>
#include <vm/seg.h>
#include <vm/seg_map.h>
#include <vm/seg_vn.h>
#include <vm/rm.h>
#include <sys/fs/cachefs_fs.h>
#include <sys/fs/cachefs_log.h>


/* forward references */
int filegrpdir_find(filegrp_t *fgp);
int filegrpdir_create(filegrp_t *fgp);
int filegrp_write_space(vnode_t *vp, int offset, int cnt);
int filegrpattr_find(struct filegrp *fgp);
int filegrpattr_create(struct filegrp *fgp);

/*
 * ------------------------------------------------------------------
 *
 *		filegrp_create
 *
 * Description:
 *	Creates a filegrp object for the specified fscache.
 *	The CFS_FG_ALLOC_{ATTR, FILE} bits will be set in fg_flags
 *	if the cache is in NOCACHE and NOFILL mode or if
 *	the directory does not exist yet.
 *	The filegrp object maintains a reference to the specified
 *	fscache.
 * Arguments:
 *	fscp	fscache to create the file group in
 *	fileno	start file number for the file group
 * Returns:
 *	Returns the created filegrp object.
 * Preconditions:
 *	precond(fscp)
 *	precond(fscp->fs_options.opt_fgsize > 0)
 */

filegrp_t *
filegrp_create(struct fscache *fscp, ino_t fileno)
{
	filegrp_t *fgp;
	int fgsize;
	int flags;

	fgsize = fscp->fs_options.opt_fgsize;

	/*LINTED alignment okay*/
	fgp = (filegrp_t *)cachefs_kmem_zalloc(sizeof (filegrp_t), KM_SLEEP);
	fgp->fg_fileno = (ino_t) ((fileno / fgsize) * fgsize);
	fgp->fg_fscp = fscp;
	mutex_init(&fgp->fg_mutex, "filegrp access lock", MUTEX_DEFAULT, NULL);
	mutex_init(&fgp->fg_gc_mutex, "filegrp gc lock", MUTEX_DEFAULT, NULL);

	fgp->fg_headersize = sizeof (struct attrcache_header) +
	    (fgsize * sizeof (struct attrcache_index)) +
	    ((fgsize + 7) >> 3);

	fgp->fg_filesize = fgp->fg_headersize +
	    (fgsize * sizeof (struct cachefs_metadata));

	fgp->fg_flags |= CFS_FG_ALLOC_ATTR | CFS_FG_ALLOC_FILE;
	flags = fscp->fs_flags;
	if (flags & CFS_FS_READ) {
		fgp->fg_flags |= CFS_FG_READ;
		if (flags & CFS_FS_WRITE) {
			fgp->fg_flags |= CFS_FG_WRITE;
		}
	}

	if (fgp->fg_flags & CFS_FG_READ) {
		/* find the attrcache file and frontfile directory */
		(void) filegrpattr_find(fgp);

		/*
		 * XXX: we can tell from the file count in the attrcache
		 * whether we can expect to find a front file dir or
		 * not.  If not, we can save the lookup here...
		 */
		(void) filegrpdir_find(fgp);
	}

	return (fgp);
}

/*
 * ------------------------------------------------------------------
 *
 *		filegrp_destroy
 *
 * Description:
 *	Destroys the filegrp object and releases any kernel
 *	resource associated with it.
 *	Additionally if the on disk file group directory does
 *	not contain any front files it is removed.
 * Arguments:
 *	fgp	filegrp object to destroy
 * Returns:
 * Preconditions:
 *	precond(fgp is a valid filegrp object)
 *	precond(fgp->fg_count == 0)
 *	precond(fgp->fg_next == NULL)
 */

void
filegrp_destroy(filegrp_t *fgp)
{
	struct fscache *fscp = fgp->fg_fscp;
	char name[CFS_FRONTFILE_NAME_SIZE+1];
	char *fname;
	int error;

	ASSERT(fgp->fg_count == 0);
	ASSERT(fgp->fg_next == NULL);

	if (fgp->fg_attrvp) {
		if (fgp->fg_flags & CFS_FG_UPDATED) {
			error = filegrp_sync(fgp);
			if (error)
				cmn_err(CE_WARN,
				    "cachefs: UFS error on cache, "
				    "run fsck %d\n", error);
		}
		VN_RELE(fgp->fg_attrvp);
	}
	if (fgp->fg_header) {
		/*
		 * If there are no attrcache entries in use and
		 * if we can modify the cache.
		 */
		if ((fgp->fg_header->ach_count == 0) &&
		    (fgp->fg_flags & CFS_FG_WRITE)) {
			ASSERT(fgp->fg_header->ach_nffs == 0);

			/* remove attrcache file from the lru list */
			ASSERT(fgp->fg_flags & CFS_FG_LRU);
			cachefs_lru_remove(fscp->fs_cache,
				fgp->fg_header->ach_lruno);
			cachefs_lru_free(fscp->fs_cache,
				fgp->fg_header->ach_lruno);
			fgp->fg_flags &= ~CFS_FG_LRU;

			/* remove the attrcache file */
			make_ascii_name(fgp->fg_fileno, name);
			fname = name;
			error = VOP_REMOVE(fscp->fs_fsattrdir, fname, kcred);
			if (error) {
				cmn_err(CE_WARN,
				    "cachefs: error in cache, run fsck\n");
			} else {
				cachefs_freefile(fscp->fs_cache);
				cachefs_freeblocks(fscp->fs_cache,
				    fgp->fg_header->ach_nblks);
			}
		}
		cachefs_kmem_free((caddr_t)fgp->fg_header, fgp->fg_headersize);
	}
	if (fgp->fg_dirvp) {
		VN_RELE(fgp->fg_dirvp);
	}
	mutex_destroy(&fgp->fg_mutex);
	mutex_destroy(&fgp->fg_gc_mutex);

	bzero((caddr_t)fgp, sizeof (filegrp_t));
	cachefs_kmem_free((caddr_t)fgp, sizeof (filegrp_t));
}

/*
 * ------------------------------------------------------------------
 *
 *		filegrp_allocattr
 *
 * Description:
 *	Tries to find the attrcache file for the given filegroup.
 *	If the file does not yet exist it is created.
 * Arguments:
 *	fgp	filegrp object
 * Returns:
 *	Returns 0 on success, an errno value on failure.
 * Preconditions:
 *	precond(fgp is a valid filegrp object)
 */

int
filegrp_allocattr(filegrp_t *fgp)
{
	int error = 0;

	mutex_enter(&fgp->fg_mutex);

	/* if we do not yet have the attrcache file */
	if (fgp->fg_flags & CFS_FG_ALLOC_ATTR) {
		/* fail if we tried to create it but failed previously */
		if (fgp->fg_flags & CFS_FG_NOCACHE) {
			error = ENOENT;
			goto out;
		}

		/* fail if we cannot read from the cache */
		if ((fgp->fg_flags & CFS_FG_READ) == 0) {
			error = ENOENT;
			goto out;
		}

		/* try to find the attrcache file in the cache */
		error = filegrpattr_find(fgp);
		if (error == ENOENT) {
			/* fail if we cannot create the attrcache file */
			if ((fgp->fg_flags & CFS_FG_WRITE) == 0) {
				error = ENOENT;
				goto out;
			}

			/* try to create the attrcache file */
			error = filegrpattr_create(fgp);
		}
	}
out:
	mutex_exit(&fgp->fg_mutex);

	return (error);
}

/*
 * ------------------------------------------------------------------
 *
 *		filegrp_hold
 *
 * Description:
 *	Increments the number of references to this filegrp object.
 * Arguments:
 *	fgp	filegrp object to reference
 * Returns:
 * Preconditions:
 *	precond(fgp is a valid filegrp object)
 */

void
filegrp_hold(filegrp_t *fgp)
{
	mutex_enter(&fgp->fg_mutex);

	fgp->fg_count++;

	/* remove attrcache file from the lru list if necessary */
	if ((fgp->fg_flags & CFS_FG_WRITE) &&
	    (fgp->fg_flags & CFS_FG_LRU)) {
		cachefs_lru_remove(fgp->fg_fscp->fs_cache,
			fgp->fg_header->ach_lruno);
		cachefs_active_add(fgp->fg_fscp->fs_cache,
			fgp->fg_header->ach_lruno);
		fgp->fg_flags &= ~CFS_FG_LRU;
	}

	mutex_exit(&fgp->fg_mutex);
}

/*
 * ------------------------------------------------------------------
 *
 *		filegrp_rele
 *
 * Description:
 *	Decrements the number of references to this filegrp object.
 * Arguments:
 *	fgp	filegrp object to dereference
 * Returns:
 * Preconditions:
 *	precond(fgp is a valid filegrp object)
 *	precond(number of refrences to filegrp is > 0)
 */

void
filegrp_rele(filegrp_t *fgp)
{
	mutex_enter(&fgp->fg_mutex);
	ASSERT(fgp->fg_count > 0);

	/* move attrcache file to the lru list if necessary */
	if (((fgp->fg_flags & CFS_FG_ALLOC_ATTR) == 0) &&
	    (fgp->fg_flags & CFS_FG_WRITE) &&
	    ((fgp->fg_flags & CFS_FG_LRU) == 0) &&
	    (fgp->fg_count == 1) &&
	    (fgp->fg_header->ach_nffs == 0)) {
		cachefs_active_remove(fgp->fg_fscp->fs_cache,
			fgp->fg_header->ach_lruno);
		cachefs_lru_add(fgp->fg_fscp->fs_cache,
			fgp->fg_header->ach_lruno);
		fgp->fg_flags |= CFS_FG_LRU;
	}

	fgp->fg_count--;

	mutex_exit(&fgp->fg_mutex);

}

/*
 * ------------------------------------------------------------------
 *
 *		filegrp_ffhold
 *
 * Description:
 *	Increments the count of the number of front files for
 *	this filegrp by one.
 * Arguments:
 *	fgp	filegrp object to reference
 * Returns:
 *	Returns 0 for success or a non-zero errno.
 * Preconditions:
 *	precond(fgp is a valid filegrp object)
 *	precond(number of refrences to filegrp is > 0)
 *	precond(filegrp is writable)
 */

int
filegrp_ffhold(filegrp_t *fgp)
{
	int error = 0;

	cachefs_cache_dirty(fgp->fg_fscp->fs_cache, 1);

	mutex_enter(&fgp->fg_mutex);
	ASSERT(fgp->fg_flags & CFS_FG_WRITE);
	ASSERT(fgp->fg_count > 0);

	/* if we do not have the directory vp yet */
	if (fgp->fg_flags & CFS_FG_ALLOC_FILE) {

		/* create the directory if necessary */
		if (fgp->fg_header->ach_nffs == 0) {
			error = filegrpdir_create(fgp);
			if (error)
				goto out;
		}

		/* else find the directory */
		else {
			error = filegrpdir_find(fgp);
			if (error) {
#ifdef CFSDEBUG
				printf("ffhold: no dir, errno %d, fileno %x\n",
					error, (int) fgp->fg_fileno);
#endif
				goto out;
			}
		}
	}
	ASSERT(fgp->fg_dirvp);

	/* if on the active list */
	if (fgp->fg_header->ach_nffs == 0) {
		ASSERT((fgp->fg_flags & CFS_FG_LRU) == 0);

		/* remove from the active list */
		cachefs_active_remove(fgp->fg_fscp->fs_cache,
			fgp->fg_header->ach_lruno);
	}

	fgp->fg_header->ach_nffs++;
	fgp->fg_flags |= CFS_FG_UPDATED;
	ASSERT(fgp->fg_header->ach_nffs <= fgp->fg_header->ach_count);

out:
	mutex_exit(&fgp->fg_mutex);

	return (error);
}

/*
 * ------------------------------------------------------------------
 *
 *		filegrp_ffrele
 *
 * Description:
 *	Decrements the count of the number of front files for
 *	this filegrp by one.
 * Arguments:
 *	fgp	filegrp object to dereference
 * Returns:
 * Preconditions:
 *	precond(fgp is a valid filegrp object)
 *	precond(filegrp is writable)
 *	precond(number of refrences to filegrp is > 0)
 *	precond(number of front file references is > 0)
 */

void
filegrp_ffrele(filegrp_t *fgp)
{
	char name[CFS_FRONTFILE_NAME_SIZE+1];
	char *fname;
	struct fscache *fscp = fgp->fg_fscp;
	int error = 0;

	cachefs_cache_dirty(fgp->fg_fscp->fs_cache, 1);

	mutex_enter(&fgp->fg_mutex);
	ASSERT(fgp->fg_flags & CFS_FG_WRITE);
	ASSERT((fgp->fg_flags & CFS_FG_ALLOC_FILE) == 0);
	ASSERT(fgp->fg_dirvp != NULL);
	ASSERT(fgp->fg_count > 0);
	ASSERT(fgp->fg_header->ach_nffs > 0);
	ASSERT(fgp->fg_header->ach_nffs <= fgp->fg_header->ach_count);

	fgp->fg_header->ach_nffs--;
	fgp->fg_flags |= CFS_FG_UPDATED;

	if (fgp->fg_header->ach_nffs == 0) {
		make_ascii_name(fgp->fg_fileno, name);
		fname = name;
		error = VOP_RMDIR(fscp->fs_fscdirvp, fname,
		    fscp->fs_fscdirvp, kcred);
		if (error == 0) {
			cachefs_freefile(fscp->fs_cache);
			cachefs_freeblocks(fscp->fs_cache, 1);
			VN_RELE(fgp->fg_dirvp);
			fgp->fg_dirvp = NULL;
			fgp->fg_flags |= CFS_FG_ALLOC_FILE;
		} else {
			cmn_err(CE_WARN,
			    "cachefs: UFS cache error, run fsck\n");
		}

		/* put on the active list */
		cachefs_active_add(fgp->fg_fscp->fs_cache,
			fgp->fg_header->ach_lruno);
	}
	mutex_exit(&fgp->fg_mutex);

}

/*
 * ------------------------------------------------------------------
 *
 *		filegrp_sync
 *
 * Description:
 *	Writes the file group's attrcache header to the attrcache
 *	file if necessary and syncs it.
 * Arguments:
 *	fgp	filegrp object
 * Returns:
 *	Returns 0 on success, an errno value on failure.
 * Preconditions:
 *	precond(fgp is a valid filegrp object)
 */

int
filegrp_sync(filegrp_t *fgp)
{
	int error = 0;

	mutex_enter(&fgp->fg_mutex);

	if (((fgp->fg_flags & CFS_FG_UPDATED) == 0) ||
	    (fgp->fg_flags & CFS_FG_ALLOC_ATTR)) {
		mutex_exit(&fgp->fg_mutex);
		return (0);
	}

	ASSERT(fgp->fg_header->ach_nffs <= fgp->fg_header->ach_count);

	error = vn_rdwr(UIO_WRITE, fgp->fg_attrvp, (caddr_t)fgp->fg_header,
	    fgp->fg_headersize, 0, UIO_SYSSPACE, 0, RLIM_INFINITY,
	    kcred, NULL);

	if (error == 0)
		error = VOP_FSYNC(fgp->fg_attrvp, FSYNC, kcred);

	if (error == 0)
		fgp->fg_flags &= ~CFS_FG_UPDATED;

	mutex_exit(&fgp->fg_mutex);

	return (error);
}

/*
 * ------------------------------------------------------------------
 *
 *		filegrp_read_metadata
 *
 * Description:
 *	Reads the metadata for the specified fileno from the attrcache
 *	file belonging to the filegrp object.
 * Arguments:
 *	fgp	filegrp object
 *	fileno	the fileno to search for
 *	mdp	set to the metadata for the fileno
 * Returns:
 *	Returns 0 on success, an errno value on failure.
 * Preconditions:
 *	precond(fgp is a valid filegrp object)
 *	precond(mdp)
 *	precond(slotp)
 */

int
filegrp_read_metadata(filegrp_t *fgp, ino_t fileno,
    struct cachefs_metadata *mdp)
{
	int slot;
	int error;

	if (fgp->fg_flags & CFS_FG_ALLOC_ATTR)
		return (ENOENT);

	slot = filegrp_fileno_to_slot(fgp, fileno);
	if (slot == 0)
		return (ENOENT);

	/* see if metadata was ever written */
	fileno -= fgp->fg_fileno;
	if (fgp->fg_offsets[fileno].ach_written == 0)
		return (ENOENT);

	error = vn_rdwr(UIO_READ, fgp->fg_attrvp,
	    (caddr_t)mdp, sizeof (struct cachefs_metadata), slot,
	    UIO_SYSSPACE, 0, 0, (cred_t *)NULL, NULL);
	if (error) {
		cmn_err(CE_WARN, "cachefs: UFS cache error, run fsck\n");
	}

	return (error);
}

/*
 * ------------------------------------------------------------------
 *
 *		filegrp_create_metadata
 *
 * Description:
 *	Allocates a slot for the specified fileno.
 * Arguments:
 *	fgp	filegrp object
 *	fileno	the fileno to allocate a slot for
 * Returns:
 *	Returns 0 on success, an errno value on failure.
 * Preconditions:
 *	precond(fgp is a valid filegrp object)
 */

int
filegrp_create_metadata(filegrp_t *fgp, struct cachefs_metadata *md,
    ino_t fileno)
{
	struct fscache *fscp = fgp->fg_fscp;
	cachefscache_t *cachep = fscp->fs_cache;
	int slot;
	int bitno;
	u_char mask;
	int last;
	int xx;

	cachefs_cache_dirty(cachep, 1);

	mutex_enter(&fgp->fg_mutex);

	if (fgp->fg_flags & CFS_FG_ALLOC_ATTR) {
		mutex_exit(&fgp->fg_mutex);
		return (ENOENT);
	}

	slot = filegrp_fileno_to_slot(fgp, fileno);
	if (slot) {
		mutex_exit(&fgp->fg_mutex);
		return (0);
	}

	fileno = fileno - fgp->fg_fileno;

	ASSERT(fileno <= fgp->fg_fscp->fs_options.opt_fgsize);

	last = (((fgp->fg_fscp->fs_options.opt_fgsize + 7) & ~(7)) / 8);
	for (xx = 0; xx < last; xx++) {
		if (fgp->fg_alloclist[xx] != (u_char)0xff) {
			for (mask = 1, bitno = 0; bitno < 8; bitno++) {
				if ((mask & fgp->fg_alloclist[xx]) == 0) {
					slot = (xx * 8) + bitno;
					goto found;
				}
				mask <<= 1;
			}
		}
	}
found:
	if (xx == last) {
		cmn_err(CE_WARN, "cachefs: attrcache error, run fsck\n");
		mutex_exit(&fgp->fg_mutex);
		return (ENOMEM);
	}

	slot = (slot * sizeof (struct cachefs_metadata)) + fgp->fg_headersize;

	ASSERT(fgp->fg_header->ach_nffs <= fgp->fg_header->ach_count);
	fgp->fg_header->ach_count++;
	fgp->fg_offsets[fileno].ach_offset = slot;
	fgp->fg_offsets[fileno].ach_written = 0;
	fgp->fg_alloclist[xx] |= mask;
	fgp->fg_flags |= CFS_FG_UPDATED;

	mutex_exit(&fgp->fg_mutex);

	if (CACHEFS_LOG_LOGGING(cachep, CACHEFS_LOG_MDCREATE))
		cachefs_log_mdcreate(cachep, 0,
		    fscp->fs_cfsvfsp, &md->md_cookie, fgp->fg_fileno + fileno,
		    fgp->fg_header->ach_count);

	return (0);
}

/*
 * ------------------------------------------------------------------
 *
 *		filegrp_write_metadata
 *
 * Description:
 *	Writes metadata to the slot held by fileno.
 * Arguments:
 *	fgp	filegrp object
 *	fileno	the fileno to write the metadata for
 *	mdp	the metadata to write
 * Returns:
 *	Returns 0 on success, an errno value on failure.
 * Preconditions:
 *	precond(fgp is a valid filegrp object)
 *	precond(mdp)
 */
int
filegrp_write_metadata(filegrp_t *fgp, ino_t fileno,
    struct cachefs_metadata *mdp)
{
	int error;
	int slot;
	int nblks;
	struct fscache *fscp = fgp->fg_fscp;

	if (fgp->fg_flags & CFS_FG_ALLOC_ATTR)
		return (ENOENT);

	slot = filegrp_fileno_to_slot(fgp, fileno);
	if (slot == 0)
		return (ENOENT);

	cachefs_cache_dirty(fscp->fs_cache, 1);

	mutex_enter(&fgp->fg_mutex);

	/* allocate blocks for the data if necessary */
	nblks = slot + sizeof (struct cachefs_metadata);
	nblks = (nblks + MAXBSIZE - 1) / MAXBSIZE;
	nblks -= fgp->fg_header->ach_nblks;
	if (nblks > 0) {
		error = cachefs_allocblocks(fscp->fs_cache, nblks, kcred);
		if (error)
			goto out;
		error = filegrp_write_space(fgp->fg_attrvp,
			fgp->fg_header->ach_nblks * MAXBSIZE,
			nblks * MAXBSIZE);
		if (error) {
			cachefs_freeblocks(fscp->fs_cache, nblks);
			goto out;
		}
	} else
		nblks = 0;

	/* write the metadata */
	error = vn_rdwr(UIO_WRITE, fgp->fg_attrvp, (caddr_t)mdp,
			sizeof (struct cachefs_metadata), slot,
			UIO_SYSSPACE, 0, RLIM_INFINITY, kcred, NULL);
	if (error) {
		if (error != ENOSPC) {
			cmn_err(CE_WARN,
			    "cachefs: UFS write error %d, run fsck\n",
			    error);
		}
		cachefs_freeblocks(fscp->fs_cache, nblks);
		goto out;
	}

	/* mark metadata as having been written */
	fileno -= fgp->fg_fileno;
	fgp->fg_offsets[fileno].ach_written = 1;

	/* update number of blocks used by the attrcache file */
	fgp->fg_header->ach_nblks += nblks;

	/* force sync to be done eventually */
	fgp->fg_flags |= CFS_FG_UPDATED;

out:
	mutex_exit(&fgp->fg_mutex);

	return (error);
}

/*
 * ------------------------------------------------------------------
 *
 *		filegrp_destroy_metadata
 *
 * Description:
 *	Destroys the metadata associated with the specified fileno.
 * Arguments:
 *	fgp	filegrp object
 *	fileno	the fileno to destroy the metadata for
 * Returns:
 *	Returns 0 on success, an errno value on failure.
 * Preconditions:
 *	precond(fgp is a valid filegrp object)
 */

int
filegrp_destroy_metadata(filegrp_t *fgp, ino_t fileno)
{
	int i;
	int bitno;
	u_char mask = 1;

	int slot;

	if (fgp->fg_flags & CFS_FG_ALLOC_ATTR)
		return (ENOENT);

	slot = filegrp_fileno_to_slot(fgp, fileno);
	if (slot == 0)
		return (ENOENT);

	cachefs_cache_dirty(fgp->fg_fscp->fs_cache, 1);

	mutex_enter(&fgp->fg_mutex);

	i = fileno - fgp->fg_fileno;
	fgp->fg_offsets[i].ach_offset = 0;
	fgp->fg_offsets[i].ach_written = 0;
	i = (slot - fgp->fg_headersize) /  sizeof (struct cachefs_metadata);
	bitno = i & 7;
	i = i >> 3;
	mask <<= bitno;
	if (fgp->fg_alloclist[i] & mask)
		fgp->fg_alloclist[i] &= ~mask;
	else
		cmn_err(CE_WARN,
		    "filegrp_destroy_metadata:"
		    " fileno %lu slot %d-%d fgp %x not allocated\n",
		    fileno, i, bitno, (int) fgp);

	fgp->fg_header->ach_count--;
	ASSERT(fgp->fg_header->ach_nffs <= fgp->fg_header->ach_count);
	fgp->fg_flags |= CFS_FG_UPDATED;
	mutex_exit(&fgp->fg_mutex);

	return (0);
}

/*
 * ------------------------------------------------------------------
 *
 *		filegrp_list_find
 *
 * Description:
 *	Looks for the filegrp that owns the specified fileno
 *	on the fscp filegrp lists.
 *	The fscp->fs_fslock must be held while this routine is called.
 *	By convention the filegrp object returned may be used as
 *	long as the fs_fslock is held.  To use the filegrp after
 *	dropping fs_fslock, call filegrp_hold.
 * Arguments:
 *	fscp	fscache object
 *	fileno	the fileno to search on
 * Returns:
 *	Returns the filegrp object if found, NULL if not.
 * Preconditions:
 *	precond(fscp is a valid fscache object)
 */

filegrp_t *
filegrp_list_find(struct fscache *fscp, ino_t fileno)
{
	long fgsize = fscp->fs_options.opt_fgsize;
	struct filegrp *fgp;

	/*
	 * Do some basic sanity checking
	 */
	ASSERT(MUTEX_HELD(&fscp->fs_fslock));
	fgp = fscp->fs_filegrp;
	/*
	 * Calculate the file group for this fileno and see if one
	 * exists
	 */
	fileno = (ino_t) ((fileno / fgsize) * fgsize);
	while (fgp != NULL) {
		if (fgp->fg_fileno == fileno)
			break;
		fgp = fgp->fg_next;
	}

	return (fgp);
}

/*
 * ------------------------------------------------------------------
 *
 *		filegrp_list_add
 *
 * Description:
 *	Adds the filegrp to the list of filegrps in the fscp.
 *	The fscp->fs_fslock must be held while this routine is called.
 * Arguments:
 *	fscp	fscache object
 *	fgp	filegrp object
 * Returns:
 * Preconditions:
 *	precond(fscp is a valid fscache object)
 *	precond(fgp is a valid filegrp object)
 *	precond(fgp is not already on a list of filegrps)
 */

void
filegrp_list_add(struct fscache *fscp, filegrp_t *fgp)
{
	ASSERT(MUTEX_HELD(&fscp->fs_fslock));
	ASSERT(fgp->fg_next == NULL);

	fgp->fg_next = fscp->fs_filegrp;
	fscp->fs_filegrp = fgp;
}

/*
 * ------------------------------------------------------------------
 *
 *		filegrp_list_remove
 *
 * Description:
 *	Removes the filegrp from the list of filegrps in the fscp.
 *	The fscp->fs_fslock must be held while this routine is called.
 * Arguments:
 *	fscp	fscache object
 *	fgp	filegrp object
 * Returns:
 * Preconditions:
 *	precond(fscp is a valid fscache object)
 *	precond(fgp is a valid filegrp object)
 *	precond(fgp is on the list of filegrps in fscp)
 */

void
filegrp_list_remove(struct fscache *fscp, filegrp_t *fgp)
{
	struct filegrp *fp;
	struct filegrp **pfgp;
	int found = 0;

	ASSERT(MUTEX_HELD(&fscp->fs_fslock));

	fp = fscp->fs_filegrp;
	pfgp = &fscp->fs_filegrp;

	while (fp != NULL) {
		if (fp == fgp) {
			*pfgp = fp->fg_next;
			fp->fg_next = NULL;
			found++;
			break;
		}
		pfgp = &fp->fg_next;
		fp = fp->fg_next;
	}
	ASSERT(found);
}

/*
 * ------------------------------------------------------------------
 *
 *		filegrp_list_gc
 *
 * Description:
 *	Traverses the filegrp lists and throws away any filegrps that are
 *	not in use.
 *	The fscp->fs_fslock must be held while this routine is called.
 * Arguments:
 *	fscp	fscache object
 * Returns:
 * Preconditions:
 *	precond(fscp is a valid fscache object)
 */

void
filegrp_list_gc(struct fscache *fscp)
{
	struct filegrp *next, *fgp;

	ASSERT(MUTEX_HELD(&fscp->fs_fslock));

	/*LINTED next is set before it's used */
	for (fgp = fscp->fs_filegrp; fgp != NULL; fgp = next) {
		next = fgp->fg_next;
		mutex_enter(&fgp->fg_mutex);
		if (fgp->fg_count > 0) {
			mutex_exit(&fgp->fg_mutex);
			continue;
		}
		mutex_exit(&fgp->fg_mutex);
		filegrp_list_remove(fscp, fgp);
		filegrp_destroy(fgp);
	}
}

/*
 * ------------------------------------------------------------------
 *
 *		filegrp_setup
 *
 * Description:
 *	Perform initialization actions on the given filegrp.
 *	The fgp->fg_mutex must be held while this routine is called.
 * Arguments:
 *	fgp	filegrp object
 *	flags	flags to be OR'ed into the fgp flags field
 *	dolru	indicates whether filegrp should be removed from lru or not
 * Returns:
 * Preconditions:
 *	precond(fgp is a valid filegrp object)
 */
static void
filegrp_setup(struct filegrp *fgp, int flags, int dolru)
{
	ASSERT(MUTEX_HELD(&fgp->fg_mutex));

	/* turn on the specified flags */
	if (flags)
		fgp->fg_flags |= flags;

	/* if the attrcache file exists, find it */
	if (fgp->fg_flags & CFS_FG_ALLOC_ATTR)
		(void) filegrpattr_find(fgp);

	/* if the attrcache directory exists, find it */
	if (((fgp->fg_flags & CFS_FG_ALLOC_ATTR) == 0) &&
	    (fgp->fg_flags & CFS_FG_ALLOC_FILE) &&
	    (fgp->fg_header->ach_nffs > 0)) {
		(void) filegrpdir_find(fgp);
	}

	/* move from lru list to active list if necessary */
	if (dolru && fgp->fg_flags & CFS_FG_LRU) {
		ASSERT(fgp->fg_header->ach_nffs == 0);
		if (fgp->fg_count > 0) {
			cachefs_lru_remove(fgp->fg_fscp->fs_cache,
				fgp->fg_header->ach_lruno);
			cachefs_active_add(fgp->fg_fscp->fs_cache,
				fgp->fg_header->ach_lruno);
			fgp->fg_flags &= ~CFS_FG_LRU;
		}
	}
}

/*
 * ------------------------------------------------------------------
 *
 *		filegrp_list_enable_caching_ro
 *
 * Description:
 *	Traverses the filegrp lists and enables the
 *	use of the cache read-only.
 *	The fscp->fs_fslock must be held while this routine is called.
 * Arguments:
 *	fscp	fscache object
 * Returns:
 * Preconditions:
 *	precond(fscp is a valid fscache object)
 */

void
filegrp_list_enable_caching_ro(struct fscache *fscp)
{
	struct filegrp *fgp;

	ASSERT(MUTEX_HELD(&fscp->fs_fslock));

	for (fgp = fscp->fs_filegrp; fgp != NULL; fgp = fgp->fg_next) {
		mutex_enter(&fgp->fg_mutex);
		filegrp_setup(fgp, 0, 0);
		mutex_exit(&fgp->fg_mutex);
	}
}

/*
 * ------------------------------------------------------------------
 *
 *		filegrp_list_enable_caching_rw
 *
 * Description:
 *	Traverses the filegrp lists and enables the
 *	use of the cache read-write.
 *	The fscp->fs_fslock must be held while this routine is called.
 * Arguments:
 *	fscp	fscache object
 * Returns:
 * Preconditions:
 *	precond(fscp is a valid fscache object)
 *	precond(all filegrps must be in the read-only state)
 */

void
filegrp_list_enable_caching_rw(struct fscache *fscp)
{
	struct filegrp *fgp;

	ASSERT(MUTEX_HELD(&fscp->fs_fslock));

	for (fgp = fscp->fs_filegrp; fgp != NULL; fgp = fgp->fg_next) {
		mutex_enter(&fgp->fg_mutex);
		filegrp_setup(fgp, CFS_FG_READ|CFS_FG_WRITE, 1);
		mutex_exit(&fgp->fg_mutex);
	}
}

/*
 * ------------------------------------------------------------------
 *
 *		filegrpdir_find
 *
 * Description:
 *	Tries to find the filegrp frontfile directory in the cache.
 *	If found CFS_FG_ALLOC_FILE is turned off.
 *	This routine should not be called if CFS_FG_ALLOC_FILE is
 *	already off.
 * Arguments:
 *	fgp	filegrp object
 * Returns:
 *	Returns 0 on success, an errno value on failure.
 * Preconditions:
 *	precond(fgp is a valid filegrp object)
 */

int
filegrpdir_find(filegrp_t *fgp)
{
	int error;
	vnode_t *dirvp;
	char name[CFS_FRONTFILE_NAME_SIZE+1];
	char *fname;
	struct fscache *fscp = fgp->fg_fscp;

	if (fgp->fg_flags & CFS_FG_ALLOC_ATTR)
		return (ENOENT);
	ASSERT(fgp->fg_flags & CFS_FG_ALLOC_FILE);

	make_ascii_name(fgp->fg_fileno, name);
	fname = name;
	error = VOP_LOOKUP(fscp->fs_fscdirvp, fname, &dirvp, NULL,
			0, NULL, kcred);
	if (error == 0) {
		fgp->fg_dirvp = dirvp;
		fgp->fg_flags &= ~CFS_FG_ALLOC_FILE;
#ifdef CFSDEBUG
		if (fgp->fg_header->ach_nffs == 0) {
			printf("filegrpdir_find: %s found but no front files\n",
				fname);
		}
#endif
	}
#ifdef CFSDEBUG
	else if (fgp->fg_header->ach_nffs != 0) {
		printf("filegrpdir_find: %s NOT found but %d front files\n",
			fname, fgp->fg_header->ach_nffs);
	}
#endif
	return (error);
}

/*
 * ------------------------------------------------------------------
 *
 *		filegrparttr_find
 *
 * Description:
 *	Tries to find the attrcache file for the given filegrp.
 *	If found the header information is read in and
 *	CFS_FG_ALLOC_ATTR is turned off.
 *	This routine should not be called if CFS_FG_ALLOC_ATTR is
 *	already off.
 * Arguments:
 *	fgp	filegrp object
 * Returns:
 *	Returns 0 on success, an errno value on failure.
 * Preconditions:
 *	precond(fgp is a valid filegrp object)
 *	precond(fgp is readable)
 */

int
filegrpattr_find(struct filegrp *fgp)
{
	int error = 0;
	struct fscache *fscp = fgp->fg_fscp;
	vnode_t *attrvp;
	struct attrcache_header *ahp;
	char name[CFS_FRONTFILE_NAME_SIZE+1];
	char *fname;

	if (fgp->fg_flags & CFS_FG_NOCACHE)
		return (ENOENT);

	ASSERT(fgp->fg_flags & CFS_FG_ALLOC_ATTR);
	make_ascii_name(fgp->fg_fileno, name);
	fname = name;
	error = VOP_LOOKUP(fscp->fs_fsattrdir, fname,
	    &attrvp, NULL, 0, NULL, kcred);
	if (error) {
		return (error);
	}
	ahp = (struct attrcache_header *)cachefs_kmem_zalloc(
	    /*LINTED alignment okay*/
	    fgp->fg_headersize, KM_SLEEP);

	error = vn_rdwr(UIO_READ, attrvp, (caddr_t)ahp,
				fgp->fg_headersize, 0, UIO_SYSSPACE,
				0, RLIM_INFINITY, (cred_t *)NULL, NULL);
	if (error) {
		cmn_err(CE_WARN, "cachefs: Read attrcache error %d, run fsck\n",
		    error);
		cachefs_kmem_free((caddr_t)ahp, fgp->fg_headersize);
		fgp->fg_flags |= CFS_FG_NOCACHE;
		VN_RELE(attrvp);
	} else {
		ASSERT(ahp->ach_nffs <= ahp->ach_count);
		fgp->fg_attrvp = attrvp;
		fgp->fg_header = ahp;
		fgp->fg_offsets = (struct attrcache_index *)(ahp + 1);
		fgp->fg_alloclist = ((u_char *)fgp->fg_offsets) +
			(fscp->fs_options.opt_fgsize *
			sizeof (struct attrcache_index));
		fgp->fg_flags &= ~CFS_FG_ALLOC_ATTR;

		/* if the attr file is on the lru */
		if (fgp->fg_header->ach_nffs == 0) {
			if ((fgp->fg_count > 0) &&
			    (fgp->fg_flags & CFS_FG_WRITE)) {
				/* remove from lru, put on active */
				cachefs_lru_remove(fgp->fg_fscp->fs_cache,
					fgp->fg_header->ach_lruno);
				cachefs_active_add(fgp->fg_fscp->fs_cache,
					fgp->fg_header->ach_lruno);
			} else {
				/* indicate it is still on the lru */
				fgp->fg_flags |= CFS_FG_LRU;
			}
		}
	}

	return (error);
}

/*
 * ------------------------------------------------------------------
 *
 *		filegrpdir_create
 *
 * Description:
 *	Creates the filegrp directory in the cache.
 *	If created CFS_FG_ALLOC_FILE is turned off.
 *	This routine should not be called if CFS_FG_ALLOC_FILE is
 *	already off.
 * Arguments:
 *	fgp	filegrp object
 * Returns:
 *	Returns 0 on success, an errno value on failure.
 * Preconditions:
 *	precond(fgp is a valid filegrp object)
 *	precond(filegrp is writeable)
 */

int
filegrpdir_create(filegrp_t *fgp)
{
	int error;
	vnode_t *dirvp = NULL;
	struct vattr *attrp = NULL;
	char name[CFS_FRONTFILE_NAME_SIZE+1];
	char *fname;
	struct fscache *fscp = fgp->fg_fscp;

	ASSERT(fgp->fg_flags & CFS_FG_WRITE);

	if (fgp->fg_flags & (CFS_FG_ALLOC_ATTR | CFS_FG_NOCACHE))
		return (ENOENT);

	/* allocate a 1 block file for the directory */
	error = cachefs_allocfile(fscp->fs_cache);
	if (error) {
		return (error);
	}
	error = cachefs_allocblocks(fscp->fs_cache, 1, kcred);
	if (error) {
		cachefs_freefile(fscp->fs_cache);
		return (error);
	}

	/*
	 * Construct a name for this file group directory and then do a mkdir
	 */
	make_ascii_name(fgp->fg_fileno, name);
	fname = name;
	attrp = (struct vattr *)cachefs_kmem_alloc(sizeof (struct vattr),
			/*LINTED alignment okay*/
			KM_SLEEP);
	attrp->va_mode = S_IFDIR | 0777;
	attrp->va_uid = 0;
	attrp->va_gid = 0;
	attrp->va_type = VDIR;
	attrp->va_mask = AT_TYPE | AT_MODE | AT_UID | AT_GID;
	error = VOP_MKDIR(fscp->fs_fscdirvp, fname, attrp, &dirvp, kcred);
	if (error) {
		fgp->fg_flags |= CFS_FG_NOCACHE;
		cachefs_freefile(fscp->fs_cache);
		cachefs_freeblocks(fscp->fs_cache, 1);
	} else {
		fgp->fg_dirvp = dirvp;
		fgp->fg_flags &= ~CFS_FG_ALLOC_FILE;
	}

	if (attrp)
		cachefs_kmem_free((caddr_t)attrp, sizeof (*attrp));

	return (error);
}

/*
 * ------------------------------------------------------------------
 *
 *		filegrpattr_create
 *
 * Description:
 *	Creates the attrcache file for the given filegrp.
 *	If created CFS_FG_ALLOC_ATTR is turned off.
 *	This routine should not be called if CFS_FG_ALLOC_ATTR is
 *	already off.
 * Arguments:
 *	fgp	filegrp object
 * Returns:
 *	Returns 0 on success, an errno value on failure.
 * Preconditions:
 *	precond(fgp is a valid filegrp object)
 *	precond(filegrp is writable)
 */

int
filegrpattr_create(struct filegrp *fgp)
{
	int error;
	vnode_t *attrvp = NULL;
	struct attrcache_header *ahp = NULL;
	int nblks = 0;
	struct vattr *attrp = NULL;
	char name[CFS_FRONTFILE_NAME_SIZE+1];
	char *fname;
	struct fscache *fscp = fgp->fg_fscp;

	ASSERT(fgp->fg_flags & CFS_FG_WRITE);

	if (fgp->fg_flags & CFS_FG_NOCACHE)
		return (ENOENT);

	cachefs_cache_dirty(fscp->fs_cache, 1);

	/* allocate a file for the attrcache */
	error = cachefs_allocfile(fscp->fs_cache);
	if (error) {
		goto out;
	}

	make_ascii_name(fgp->fg_fileno, name);
	fname = name;
	attrp = (struct vattr *)cachefs_kmem_alloc(sizeof (struct vattr),
			/*LINTED alignment okay*/
			KM_SLEEP);
	attrp->va_mode = S_IFREG | 0666;
	attrp->va_uid = 0;
	attrp->va_gid = 0;
	attrp->va_type = VREG;
	attrp->va_mask = AT_TYPE | AT_MODE | AT_UID | AT_GID;
	error = VOP_CREATE(fscp->fs_fsattrdir, fname, attrp, EXCL, 0666,
			&attrvp, kcred);
	if (error) {
		cachefs_freefile(fscp->fs_cache);
		goto out;
	}

	/* alloc blocks for the attrcache header */
	nblks = (fgp->fg_headersize + MAXBSIZE - 1) / MAXBSIZE;
	error = cachefs_allocblocks(fscp->fs_cache, nblks, kcred);
	if (error) {
		nblks = 0;
		goto out;
	}

	/* Construct an attrcache header */
	ahp = (struct attrcache_header *)cachefs_kmem_zalloc(
	    /*LINTED alignment okay*/
	    fgp->fg_headersize, KM_SLEEP);

	/* write out the header to allocate space on ufs */
	error = vn_rdwr(UIO_WRITE, attrvp, (caddr_t)ahp,
		fgp->fg_headersize, 0, UIO_SYSSPACE, 0, RLIM_INFINITY,
		kcred, NULL);
	if (error)
		goto out;
	error = filegrp_write_space(attrvp, fgp->fg_headersize,
		(nblks * MAXBSIZE) - fgp->fg_headersize);
	if (error)
		goto out;
	error = VOP_FSYNC(attrvp, FSYNC, kcred);
	if (error)
		goto out;

	/* allocate an lru entry and mark it as an attrcache entry */
	error = cachefs_lru_alloc(fscp->fs_cache, fscp, &ahp->ach_lruno,
		fgp->fg_fileno);
	if (error)
		goto out;
	(void) cachefs_lru_attrc(fscp->fs_cache, ahp->ach_lruno, 1);
	if (fgp->fg_count == 0) {
		/* put on the lru */
		cachefs_lru_add(fgp->fg_fscp->fs_cache, ahp->ach_lruno);
		fgp->fg_flags |= CFS_FG_LRU;
	} else {
		/* put on active list */
		cachefs_active_add(fgp->fg_fscp->fs_cache, ahp->ach_lruno);
	}

out:
	if (error) {
		fgp->fg_flags |= CFS_FG_NOCACHE;
		if (attrvp) {
			VN_RELE(attrvp);
			(void) VOP_REMOVE(fscp->fs_fsattrdir, fname, kcred);
			cachefs_freefile(fscp->fs_cache);
		}
		if (ahp)
			cachefs_kmem_free((caddr_t)ahp, fgp->fg_headersize);
		if (nblks)
			cachefs_freeblocks(fscp->fs_cache, nblks);
	} else {
		fgp->fg_attrvp = attrvp;
		fgp->fg_header = ahp;
		fgp->fg_offsets = (struct attrcache_index *)(ahp + 1);
		fgp->fg_alloclist = ((u_char *)fgp->fg_offsets) +
			(fscp->fs_options.opt_fgsize *
			sizeof (struct attrcache_index));
		ahp->ach_count = 0;
		ahp->ach_nffs = 0;
		ahp->ach_nblks = nblks;
		fgp->fg_flags &= ~CFS_FG_ALLOC_ATTR;
		fgp->fg_flags |= CFS_FG_UPDATED;
	}

	if (attrp)
		cachefs_kmem_free((caddr_t)attrp, sizeof (*attrp));

	return (error);
}

/*
 * ------------------------------------------------------------------
 *
 *		filegrp_fileno_to_slot
 *
 * Description:
 *	Takes a fileno and returns the offset to the metadata
 *	slot for the specified filegrp.
 * Arguments:
 *	fgp	filegrp object
 *	fileno	file number to map to an offset
 * Returns:
 *	Returns the offset or 0 if the slot is not allocated yet
 *	or it is invalid.
 * Preconditions:
 *	precond(fgp is a valid filegrp object)
 *	precond(fgp is not ALLOC_PENDING or NOCACHE)
 */

int
filegrp_fileno_to_slot(filegrp_t *fgp, ino_t fileno)
{
	int xx;
	int slot;

	fileno -= fgp->fg_fileno;

	if (fileno > fgp->fg_fscp->fs_options.opt_fgsize) {
		cmn_err(CE_WARN, "cachefs: attrcache error, run fsck\n");
		return (0);
	}

	slot = fgp->fg_offsets[fileno].ach_offset;
	if (slot == 0)
		return (0);

	xx = fgp->fg_filesize - sizeof (struct cachefs_metadata);
	if ((slot < fgp->fg_headersize) || (xx < slot)) {
		cmn_err(CE_WARN, "cachefs: attrcache error, run fsck\n");
		return (0);
	}

	return (slot);
}

/*
 *
 *		filegrp_write_space
 *
 * Description:
 *	Writes garbage data to the specified file starting
 *	at the specified location for the specified number of bytes.
 *	slot for the specified filegrp.
 * Arguments:
 *	vp	vnode to write to
 *	offset	offset to write at
 *	cnt	number of bytes to write
 * Returns:
 *	Returns 0 for success or on error the result of the
 *	last vn_rdwr call.
 * Preconditions:
 *	precond(vp)
 */

int
filegrp_write_space(vnode_t *vp, int offset, int cnt)
{
	char *bufp;
	int xx;
	int error = 0;

	bufp = (char *)cachefs_kmem_zalloc(MAXBSIZE, KM_SLEEP);
	while (cnt > 0) {
		if (cnt > MAXBSIZE)
			xx = MAXBSIZE;
		else
			xx = cnt;
		error = vn_rdwr(UIO_WRITE, vp, (caddr_t)bufp,
			xx, offset, UIO_SYSSPACE, 0, RLIM_INFINITY,
			kcred, NULL);
		if (error)
			break;
		offset += xx;
		cnt -= xx;
	}
	cachefs_kmem_free((caddr_t)bufp, MAXBSIZE);
	return (error);
}
