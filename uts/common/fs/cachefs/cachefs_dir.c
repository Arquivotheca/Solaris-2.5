/*
 * Copyright (c) 1992, Sun Microsystems, Inc.
 * All rights reserved.
 */
#pragma ident   "@(#)cachefs_dir.c 1.48     94/08/22 SMI"

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
#include <sys/ioctl.h>
#include <sys/statvfs.h>
#include <sys/errno.h>
#include <sys/debug.h>
#include <sys/cmn_err.h>
#include <sys/utsname.h>
#include <sys/bootconf.h>
#include <sys/modctl.h>
#include <sys/dirent.h>
#include <vm/seg.h>
#include <vm/faultcode.h>
#include <vm/hat.h>
#include <vm/seg_map.h>
#include <sys/fs/cachefs_fs.h>
#include <sys/fs/cachefs_dir.h>
#include <sys/fs/cachefs_log.h>

extern struct seg *segkmap;
caddr_t segmap_getmap();
int segmap_release();

/* forward declarations */
static int cachefs_getdents(struct cnode *, u_int, u_int *,
    u_int *, u_int, caddr_t, int *, cred_t *);
static int cachefs_stuffdir(struct cnode *, u_int, caddr_t,
    u_int *, u_int *, cred_t *);
static int cachefs_extenddir(struct cnode *, u_int *, cred_t *);

/*
 * cachefs_dirlook() called mainly by lookup (and create), looks up the cached
 * directory for an entry and returns the information there. If the directory
 * entry doesn't exist return ENOENT, if it is incomplete, return EINVAL.
 */
int
cachefs_dirlook(struct cnode *dcp, char *nm, struct fid *cookiep,
	u_int *flagp, u_int *d_offsetp, ino_t *file_nop, cred_t *cr)
{
	int error;
	struct vattr va;
	int blockoff = 0;
	int offset = 0;
	caddr_t addr;
	vnode_t *dvp = dcp->c_frontvp;
	struct fscache *fscp = C_TO_FSCACHE(dcp);
	cachefscache_t *cachep = fscp->fs_cache;
	int nmlen;

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_DIR)
		printf("cachefs_dirlook: ENTER dcp %x nm %s\n", (int) dcp, nm);
#endif
	ASSERT(CTOV(dcp)->v_type == VDIR);
	ASSERT(dcp->c_frontvp != NULL);
	ASSERT((dcp->c_flags & CN_NOCACHE) == 0);
	dvp = dcp->c_frontvp;

	va.va_mask = AT_SIZE;
	/* XXX should save dir size */
	error = VOP_GETATTR(dvp, &va, 0, cr);
	if (error)
		goto out;

	nmlen = strlen(nm);
	while (blockoff < va.va_size) {
		offset = 0;
		addr = segmap_getmap(segkmap, dvp, (u_int)blockoff);

		while (offset < MAXBSIZE && (blockoff + offset) < va.va_size) {
			struct c_dirent *dep;

			/*LINTED alignment okay*/
			dep = (struct c_dirent *)(addr + offset);
			if ((dep->d_flag & CDE_VALID) &&
				(nmlen == dep->d_namelen) &&
				strcmp(dep->d_name, nm) == 0) {
				if (dep->d_flag & CDE_COMPLETE) {
					if (cookiep)
						*cookiep = dep->d_cookie;
					if (flagp)
						*flagp = dep->d_flag;
					error = 0;
				} else {
					error = EINVAL;
				}
				if (file_nop)
					*file_nop = dep->d_fileno;
				if (d_offsetp)
					*d_offsetp = offset + blockoff;
				(void) segmap_release(segkmap, addr, 0);
				goto out;
			}
			ASSERT(dep->d_length != 0);
			offset += dep->d_length;
		}
		(void) segmap_release(segkmap, addr, 0);
		addr = NULL;
		blockoff += MAXBSIZE;
	}
	error = ENOENT;

out:
	if (CACHEFS_LOG_LOGGING(cachep, CACHEFS_LOG_RFDIR))
		cachefs_log_rfdir(cachep, error, fscp->fs_cfsvfsp,
		    &dcp->c_metadata.md_cookie, dcp->c_fileno, cr->cr_uid);
#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_DIR)
		printf("c_dirlook: EXIT error = %d\n", error);
#endif
	return (error);
}

/*
 * cachefs_direnter adds a new directory entry. Takes as input a fid, flags
 * fileno and a sync flag. Most of the time, the caller is content with the
 * write to the (front) directory being done async. The exception being - for
 * local files, we should make sure that the directory entry is made
 * synchronously. That is notified by the caller.
 * 		issync == 0 || issync == SM_ASYNC !
 * Given an entry to enter y, we search thru the existing entries in the
 * directory, till we find an entry x such that
 * d_length(x) - DIRSIZ(x) >= CDE_SIZE(y).
 * Once we find this entry, we make
 * d_length(y) = d_length(x) - DIRSIZ(x) and
 * d_length(x) = DIRSIZ(x).
 * If we didn't find any entry x satisfying the space requirement, we allocate
 * a new block at the end of the directory.
 */
int
cachefs_direnter(struct cnode *dcp, char *nm, struct fid *cookiep, u_int flag,
    ino_t fileno, off_t doffset, cred_t *cr, int issync)
{
	struct vattr va;
	int offset, blockoff = 0;
	int error = 0;
	vnode_t *dvp = dcp->c_frontvp;
	struct c_dirent *dep;
	caddr_t addr;
	u_int esize = CDE_SIZE(nm);	/* Size of the entry to be added */
	u_char newblk = 0;
	u_int dirsize;

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_DIR)
		printf("c_direnter: ENTER dcp %x nm %s dirflg %x\n",
			(int) dcp, nm, dcp->c_metadata.md_flags);
#endif

	ASSERT(RW_WRITE_HELD(&dcp->c_statelock));
	ASSERT(CTOV(dcp)->v_type == VDIR);
	ASSERT((dcp->c_flags & CN_NOCACHE) == 0);
	ASSERT(dcp->c_frontvp != NULL);
	ASSERT(issync == 0 || issync == SM_ASYNC);
	ASSERT(esize <= (sizeof (struct c_dirent) + MAXNAMELEN));
	/*
	 * Get the current EOF for the directory(data file)
	 */
	va.va_mask = AT_SIZE;
	error = VOP_GETATTR(dcp->c_frontvp, &va, 0, cr);
	if (error)
		goto out;
	/*
	ASSERT((va.va_size & (MAXBSIZE - 1)) == 0);
	*/
	while (blockoff < va.va_size) {
		offset = 0;
		addr = segmap_getmap(segkmap, dvp, (u_int)blockoff);
		while ((offset < MAXBSIZE) &&
			((blockoff + offset) < va.va_size)) {

			/*LINTED alignment okay*/
			dep = (struct c_dirent *)(addr + offset);
			if (dep->d_length - C_DIRSIZ(dep) >= esize) {
				/*
				 * Found an entry that we can slice off and
				 * fit our new entry in !
				 * If the first entry in the block was not a
				 * valid entry, things are easy, otherwise...
				 */
				if (offset != 0 || (dep->d_flag & CDE_VALID)) {
					u_int new_len =
						dep->d_length - C_DIRSIZ(dep);

					dep->d_length = C_DIRSIZ(dep);
					dep = (struct c_dirent *)
						/*LINTED alignment okay*/
						((caddr_t)dep + C_DIRSIZ(dep));
					dep->d_length = new_len;
				}
				goto haveslot;
			}
			offset += dep->d_length;
		}
		(void) segmap_release(segkmap, addr, 0);
		blockoff += MAXBSIZE;
	}
	/*
	 * If we got here, we didn't find any space in the already allocated
	 * entries to fit this one in, so we extend the file by one more
	 * MAXBSIZE chunk, and fit the entry there.
	 */
	dirsize = va.va_size;
	error = cachefs_extenddir(dcp, &dirsize, cr);
	if (error != 0)
		goto out;

	addr = segmap_getmap(segkmap, dvp, (u_int)va.va_size);
	/*LINTED alignment okay*/
	dep = (struct c_dirent *)addr;
	dep->d_length = MAXBSIZE;
haveslot:
	dep->d_flag = CDE_VALID | flag;
	if (cookiep) {
		dep->d_flag |= CDE_COMPLETE;
		dep->d_cookie = *cookiep;
	}
	dep->d_fileno = fileno;
	dep->d_namelen = strlen(nm);
	ASSERT(dep->d_namelen <= MAXNAMELEN);
	(void) bcopy((caddr_t)nm, dep->d_name, dep->d_namelen + 1);
	if (doffset == -1) {
		/*
		 * here we were called by a directory op when
		 * in non-strict consistency mode.  We don't know
		 * the correct backfs dir offset for this entry,
		 * so we set the cnode flag to indicate this
		 * fact to subsequent readdirs
		 */
		dep->d_offset = -1;
		dcp->c_metadata.md_flags |= MD_INVALREADDIR;
	} else {
		dep->d_offset = doffset;
	}
	dcp->c_flags |= CN_UPDATED | CN_NEED_FRONT_SYNC;
	/*
	 * If this is the first entry in this (newly allocated) MAXBSIZE block,
	 * we fill in the d_length here. In other cases, it is already filled in
	 */
	if (newblk)
		dep->d_length = MAXBSIZE;
	(void) segmap_release(segkmap, addr, SM_WRITE | issync);
out:
#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_DIR)
		printf("cachefs_direnter: EXIT error = %d\n", error);
#endif
	return (error);
}

/*
 * Quite simple, if the deleted entry is the first in the MAXBSIZE block,
 * we simply mark it invalid. Otherwise, the deleted entries d_length is
 * just added to the previous entry.
 */
int
cachefs_rmdirent(struct cnode *dcp, char *nm, cred_t *cr)
{
	int blockoff = 0;
	int offset = 0;
	struct vattr va;
	int error = ENOENT;
	vnode_t *dvp = dcp->c_frontvp;
	int nmlen;

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_DIR)
		printf("cachefs_rmdirent: ENTER dcp %x nm %s\n",
		    (int) dcp, nm);
#endif
	if ((dcp->c_metadata.md_flags & MD_FILE) == 0)
		ASSERT((dcp->c_metadata.md_flags & MD_POPULATED) == 0);

	if (((dcp->c_metadata.md_flags & MD_POPULATED) == 0) || dvp == NULL) {
		error = 0;
		goto out;
	}

	ASSERT(CTOV(dcp)->v_type == VDIR);
	ASSERT((dcp->c_flags & CN_NOCACHE) == 0);
	ASSERT(dvp != NULL);
	va.va_mask = AT_SIZE;
	error = VOP_GETATTR(dvp, &va, 0, cr);
	if (error)
		goto out;

	nmlen = strlen(nm);
	while (blockoff < va.va_size) {
		caddr_t addr;
		u_int *last_len;

		offset = 0;
		last_len = NULL;
		addr = segmap_getmap(segkmap, dvp, (u_int)blockoff);
		while (offset < MAXBSIZE && (blockoff + offset) < va.va_size) {
			struct c_dirent *dep;

			/*LINTED alignment okay*/
			dep  = (struct c_dirent *)(addr + offset);
			if ((dep->d_flag & CDE_VALID) &&
				(nmlen == dep->d_namelen) &&
				strcmp(dep->d_name, nm) == 0) {
				/*
				 * Found the entry. If this was the first entry
				 * in the MAXBSIZE block, Mark it invalid. Else
				 * add it's length to the previous entry's
				 * length.
				 */
				if (last_len == NULL) {
					ASSERT(offset == 0);
					dep->d_flag = 0;
				} else
					*last_len += dep->d_length;
				(void) segmap_release(segkmap, addr,
					SM_ASYNC | SM_WRITE);
				dcp->c_flags |= CN_UPDATED | CN_NEED_FRONT_SYNC;
				goto out;
			}
			last_len = &dep->d_length;
			offset += dep->d_length;
		}
		(void) segmap_release(segkmap, addr, 0);
		blockoff += MAXBSIZE;
	}
	error = ENOENT;

out:
#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_DIR)
		printf("cachefs_rmdirent: EXIT error = %d\n", error);
#endif
	return (error);
}

/*
 * This function fills in the cookie and file no of the directory entry
 * at the offset specified by offset - In other words, makes the entry
 * "complete".
 */
void
cachefs_dirent_mod(struct cnode *dcp, u_int offset, struct fid *cookiep,
    ino_t *file_nop)
{
	struct c_dirent *dep;
	u_int blockoff = (offset & MAXBMASK);
	u_int off = (offset & MAXBOFFSET);
	caddr_t addr;
	vnode_t *dvp = dcp->c_frontvp;

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_DIR)
		printf("cachefs_dirent_mod: ENTER dcp %x offset %d\n",
			(int) dcp, offset);
#endif
	ASSERT(CTOV(dcp)->v_type == VDIR);
	ASSERT((dcp->c_flags & CN_NOCACHE) == 0);
	ASSERT(dcp->c_frontvp != NULL);
	addr = segmap_getmap(segkmap, dvp, (u_int)blockoff);
	/*LINTED alignment okay*/
	dep = (struct c_dirent *)(addr + off);
	if (cookiep) {
		dep->d_flag |= CDE_COMPLETE;
		dep->d_cookie = *cookiep;
	}
	if (file_nop != NULL)
		dep->d_fileno = *file_nop;
	(void) segmap_release(segkmap, addr, SM_ASYNC | SM_WRITE);
	dcp->c_flags |= CN_UPDATED | CN_NEED_FRONT_SYNC;

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_DIR)
		printf("cachefs_dirent_mod: EXIT\n");
#endif
}

/*
 * Called by cachefs_read_dir(). Gets a bunch if durectory entries into buf and
 * packs them into buf.
 */
static int
cachefs_getdents(struct cnode *dcp, u_int beg_off, u_int *last_offp,
    u_int *cntp, u_int bufsize, caddr_t buf, int *eofp, cred_t *cr)
{

#define	DIR_ENDOFF	0x7fffffff

	struct vattr va;
	struct c_dirent *dep;
	caddr_t addr = NULL;
	struct dirent *gdp;
	u_int blockoff;
	u_int off;
	int error;
	vnode_t *dvp = dcp->c_frontvp;

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_DIR)
	printf(
	"cachefs_getdents: ENTER dcp %x beg_off %d mdflags %x cflags %x\n",
		(int) dcp, beg_off, dcp->c_metadata.md_flags, dcp->c_flags);
#endif
	/*
	 * blockoff has the offset of the MAXBSIZE block that contains the
	 * entry  to start with. off contains the offset relative to the
	 * begining of the MAXBSIZE block.
	 */
	if (eofp)
		*eofp = 0;
	/*LINTED alignment okay*/
	gdp = (struct dirent *)buf;
	*cntp = bufsize;
	va.va_mask = AT_SIZE;
	error = VOP_GETATTR(dvp, &va, 0, cr);
	if (error) {
		*cntp = 0;
		*last_offp = 0;
		if (eofp)
			*eofp = 1;
		goto out;
	}

	if (beg_off == DIR_ENDOFF) {
		*cntp = 0;
		*last_offp = DIR_ENDOFF;
		if (eofp)
			*eofp = 1;
		goto out;
	}

	/*
	 * locate the offset where we start reading.
	 */
	blockoff = off = 0;
	while (blockoff < va.va_size) {
		addr = segmap_getmap(segkmap, dvp, (u_int)blockoff);
		/*LINTED alignment okay*/
		dep = (struct c_dirent *)addr;
		off = 0;
		while (off < MAXBSIZE && dep->d_offset <= beg_off) {
			off += dep->d_length;
			/*LINTED alignment okay*/
			dep = (struct c_dirent *)(addr + off);
		}
		if (off < MAXBSIZE) {
			break;
		}
		(void) segmap_release(segkmap, addr, 0);
		addr = NULL;
		blockoff += MAXBSIZE;
	}

	if (blockoff >= va.va_size) {
		*cntp = 0;
		*last_offp = DIR_ENDOFF;
		if (eofp)
			*eofp = 1;
		goto out;
	}

	/*
	 * Just load up the buffer with directory entries.
	 */
	for (;;) {
		u_int size;

		ASSERT((caddr_t)dep < (addr + MAXBSIZE));
		if (dep->d_flag & CDE_VALID) {

			size = ((sizeof (struct dirent) - 1 +
				(dep->d_namelen + 1)) + 3) & ~3;
			ASSERT(size < MAXBSIZE);
			if (size > bufsize)
				break;
			gdp->d_reclen = size;
			ASSERT(dep->d_namelen <= MAXNAMELEN);
			ASSERT(dep->d_offset > (*last_offp));
			gdp->d_ino = dep->d_fileno;
			gdp->d_off = dep->d_offset;
			bcopy((caddr_t)dep->d_name, (caddr_t)gdp->d_name,
				dep->d_namelen + 1);
			bufsize -= size;
			/*LINTED alignment okay*/
			gdp = (struct dirent *)((caddr_t)gdp + gdp->d_reclen);
			*last_offp = dep->d_offset;
		}
		/*
		 * Increment the offset. If we've hit EOF, fill in
		 * the lastoff and current entries d_off field.
		 */
		off += dep->d_length;
		ASSERT(off <= MAXBSIZE);
		if ((blockoff + off) >= va.va_size) {
			*last_offp = DIR_ENDOFF;
			if (eofp)
				*eofp = 1;
			break;
		}
		/*
		 * If off == MAXBSIZE, then we need to adjust or
		 * window to the next MAXBSIZE block of the directory.
		 * Adjust blockoff, off and map it in. Also, increment
		 * the directory and buffer pointers.
		 */
		if (off == MAXBSIZE) {
			(void) segmap_release(segkmap, addr, 0);
			off = 0;
			blockoff += MAXBSIZE;
			addr = segmap_getmap(segkmap, dvp, (u_int)blockoff);
		}
		/*LINTED alignment okay*/
		dep = (struct c_dirent *)(addr + off);
	}
	*cntp -= bufsize;
out:
	/*
	 * Release any maping that may exist.
	 */
	if (addr)
		(void) segmap_release(segkmap, addr, 0);
#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_DIR)
		printf("ccachefs_getdents: EXIT error = %d\n", error);
#endif
	return (error);
}

/*
 * Called by cachefs_readdir(). Fills up directories from the backFS.
 */
int
cachefs_read_dir(struct cnode *dcp, struct uio *uiop, int *eofp, cred_t *cr)
{
	int error;
	u_int count;
	u_int size;
	caddr_t buf;
	u_int next = uiop->uio_offset;
	struct fscache *fscp = C_TO_FSCACHE(dcp);
	cachefscache_t *cachep = fscp->fs_cache;

	ASSERT(CTOV(dcp)->v_type == VDIR);
	ASSERT((dcp->c_flags & CN_NOCACHE) == 0);
	ASSERT(RW_READ_HELD(&dcp->c_rwlock));

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_DIR)
		printf("cachefs_read_dir: ENTER dcp %x\n", (int) dcp);
#endif
	ASSERT((dcp->c_metadata.md_flags & (MD_FILE|MD_POPULATED)) ==
	    (MD_FILE|MD_POPULATED));

	size = uiop->uio_resid;
	buf = (caddr_t) cachefs_kmem_alloc(size, KM_SLEEP);
	error = cachefs_getdents(dcp, next, &next, &count, size,
			buf, eofp, cr);
	/*LINTED want count != 0*/
	if (error == 0 && count > 0) {
		ASSERT(count <= size);
		error = uiomove(buf, (int)count, UIO_READ, uiop);
		if (error == 0)
			uiop->uio_offset = next;
	}
out:
	(void) cachefs_kmem_free(buf, (u_int)size);
	if (CACHEFS_LOG_LOGGING(cachep, CACHEFS_LOG_RFDIR))
		cachefs_log_rfdir(cachep, error, fscp->fs_cfsvfsp,
		    &dcp->c_metadata.md_cookie, dcp->c_fileno, cr->cr_uid);
#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_DIR)
		printf("cachefs_read_dir: EXIT error = %d\n", error);
#endif
	return (error);
}

/*
 * Populates the directory from the back filesystem.
 */
int
cachefs_filldir(struct cnode *dcp, int type, cred_t *cr)
{
	int error = 0;
	struct uio uio;
	struct iovec iov;
	caddr_t buf = NULL;
	int count;
	int eof = 0;
	u_int frontoff, frontsize;
	struct fscache *fscp = C_TO_FSCACHE(dcp);
	cachefscache_t *cachep = fscp->fs_cache;

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_DIR)
		printf("cachefs_filldir: ENTER dcp %x\n", (int) dcp);
#endif
	ASSERT((dcp->c_metadata.md_flags & MD_POPULATED) == 0);
	ASSERT(dcp->c_metadata.md_flags & MD_FILE);
	ASSERT(dcp->c_frontvp != NULL);
	ASSERT(dcp->c_backvp != NULL);

	if (type != RW_WRITER) {
		if (rw_tryupgrade(&dcp->c_statelock) == 0) {
			error = EAGAIN;
			goto out1;
		}
	}

	frontoff = frontsize = 0;

	buf = (caddr_t)cachefs_kmem_alloc(MAXBSIZE, KM_SLEEP);
	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_fmode = 0;
	uio.uio_offset = 0;
	for (;;) {
		/*
		 * Read in a buffer's worth of dirents and enter them in to the
		 * directory.
		 */
		uio.uio_resid = MAXBSIZE;
		iov.iov_base = buf;
		iov.iov_len = MAXBSIZE;
		VOP_RWLOCK(dcp->c_backvp, 0);
		error = VOP_READDIR(dcp->c_backvp, &uio, cr, &eof);
		VOP_RWUNLOCK(dcp->c_backvp, 0);
		if (error)
			goto out;
		count = MAXBSIZE - uio.uio_resid;
		ASSERT(count >= 0);
		if (count > 0)
			if (error = cachefs_stuffdir(dcp, count, buf,
			    &frontoff, &frontsize, cr))
				goto out;
		if (eof || count == 0)
			break;
	}
	/*
	 * Mark the directory as not empty. Also bang the flag that says that
	 * this directory needs to be sync'ed on inactive.
	 */
	dcp->c_metadata.md_flags |= MD_POPULATED;
	dcp->c_metadata.md_flags &= ~MD_INVALREADDIR;
	dcp->c_flags |= CN_UPDATED | CN_NEED_FRONT_SYNC;

out:
	if (error && error != EAGAIN) {
		cachefs_inval_object(dcp, cr);
	}
	if (type == RW_READER)
		rw_downgrade(&dcp->c_statelock);
	if (buf)
		cachefs_kmem_free(buf, (u_int)MAXBSIZE);

out1:
	if (CACHEFS_LOG_LOGGING(cachep, CACHEFS_LOG_FILLDIR))
		cachefs_log_filldir(cachep, error, fscp->fs_cfsvfsp,
		    &dcp->c_metadata.md_cookie, dcp->c_fileno, frontsize);

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_DIR)
		printf("cachefs_filldir: EXIT error = %d\n", error);
#endif
	return (error);
}


/*
 * If the directory contains only the elements "." and "..", then this returns
 * TRUE (= 1) else, this returns FALSE (= 0). Used br cachefs_rmdir()
 */
cachefs_dirempty(struct cnode *dcp, cred_t *cr)
{
	struct vattr va;
	int blockoff = 0;
	int offset;
	caddr_t addr;
	int error;
	vnode_t *dvp = dcp->c_frontvp;

#ifdef CFSDEBUG
	CFS_DEBUG(CFSDEBUG_DIR)
		printf("cachefs_dirempty: ENTER dcp %x\n", (int) dcp);
#endif
	ASSERT(CTOV(dcp)->v_type == VDIR);
	ASSERT((dcp->c_flags & CN_NOCACHE) == 0);
	ASSERT(dcp->c_frontvp != NULL);
	va.va_mask = AT_SIZE;
	error = VOP_GETATTR(dvp, &va, 0, cr);
	if (error)
		return (0);

	while (blockoff < va.va_size) {
		offset = 0;
		addr = segmap_getmap(segkmap, dvp, (u_int)blockoff);
		while (offset < MAXBSIZE && (blockoff + offset) < va.va_size) {
			struct c_dirent *dep;

			/*LINTED alignment okay*/
			dep = (struct c_dirent *)(addr + offset);
			if ((dep->d_flag & CDE_VALID) &&
				((strcmp(dep->d_name, ".") != 0) &&
				(strcmp(dep->d_name, "..") != 0))) {
				(void) segmap_release(segkmap, addr, 0);
				return (0);
			}
			offset += dep->d_length;
		}
		(void) segmap_release(segkmap, addr, 0);
		addr = NULL;
		blockoff += MAXBSIZE;
	}
	return (1);
}

/*
 * Called by cachefs_filldir() to stuff a buffer of dir entries into
 * a front file.  This is more efficient than repeated calls to
 * cachefs_direnter, and it also allows us to maintain entries in backfs
 * order (readdir requires that entry offsets be ascending).
 */
static int
cachefs_stuffdir(struct cnode *dcp, u_int count, caddr_t buf,
    u_int *offsetp, u_int *fsizep, cred_t *cr)
{
	int error;
	caddr_t addr;
	struct c_dirent *cdep, *last;
	struct dirent *dep;
	int inblk, entsize;
	u_int blockoff = (*offsetp & MAXBMASK);
	u_int off = (*offsetp & MAXBOFFSET);

	ASSERT(RW_WRITE_HELD(&dcp->c_statelock));
	/*LINTED want count != 0*/
	ASSERT(count > 0);

	if (*offsetp >= *fsizep) {
		error = cachefs_extenddir(dcp, fsizep, cr);
		if (error)
			return (error);
	}

	last = NULL;
	addr = segmap_getmap(segkmap, dcp->c_frontvp, blockoff);
	/*LINTED alignment okay*/
	cdep = (struct c_dirent *)(addr+off);
	inblk = MAXBSIZE-off;
	if (*offsetp != 0) {
		ASSERT(cdep->d_length == inblk);
		inblk -= C_DIRSIZ(cdep);
		last = cdep;
		last->d_length -= inblk;
		off += last->d_length;
		/*LINTED alignment okay*/
		cdep = (struct c_dirent *)(addr+off);
	}
	/*LINTED alignment okay*/
	dep = (struct dirent *)buf;
	/*LINTED want count != 0*/
	while (count > 0) {
		if (last) {
			ASSERT(dep->d_off > last->d_offset);
		}
		entsize = CDE_SIZE(dep->d_name);
		if (entsize > inblk) {
			segmap_release(segkmap, addr, SM_WRITE);
			error = cachefs_extenddir(dcp, fsizep, cr);
			if (error)
				return (error);
			if (last) {
				last->d_length += inblk;
			}
			blockoff += MAXBSIZE;
			addr = segmap_getmap(segkmap, dcp->c_frontvp, blockoff);
			off = 0;
			/*LINTED alignment okay*/
			cdep = (struct c_dirent *)addr;
			inblk = MAXBSIZE;
			last = NULL;
		}
		cdep->d_length = entsize;
		cdep->d_fileno = dep->d_ino;
		cdep->d_namelen = strlen(dep->d_name);
		cdep->d_flag = CDE_VALID;
		(void) bcopy((caddr_t)dep->d_name, (caddr_t)cdep->d_name,
		    cdep->d_namelen+1);
		cdep->d_offset = dep->d_off;
		inblk -= entsize;
		count -= dep->d_reclen;
		/*LINTED alignment okay*/
		dep = (struct dirent *)(((caddr_t)dep) + dep->d_reclen);
		*offsetp = blockoff + off;
		off += entsize;
		last = cdep;
		/*LINTED alignment okay*/
		cdep = (struct c_dirent *)(addr + off);
	}
	if (last) {
		last->d_length += inblk;
	}
	segmap_release(segkmap, addr, SM_WRITE);

	return (0);
}

static int
cachefs_extenddir(struct cnode *dcp, u_int *cursize, cred_t *cr)
{
	struct vattr va;
	cachefscache_t *cachep = C_TO_FSCACHE(dcp)->fs_cache;
	int error = 0;

	ASSERT(RW_WRITE_HELD(&dcp->c_statelock));
	ASSERT(((*cursize) & (MAXBSIZE-1)) == 0);

	va.va_mask = AT_SIZE;
	va.va_size = *cursize + MAXBSIZE;
	error = cachefs_allocblocks(cachep, 1, cr);
	if (error)
		return (error);
	error = VOP_SETATTR(dcp->c_frontvp, &va, 0, cr);
	if (error) {
		cachefs_freeblocks(cachep, 1);
		return (error);
	}
	(dcp->c_metadata.md_frontblks)++;
	*cursize += MAXBSIZE;
	dcp->c_flags |= CN_UPDATED;
	return (0);
}
