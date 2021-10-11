/*	@(#)nfs_export.c 1.11 88/02/08 SMI	*/

/*
 *		PROPRIETARY NOTICE (Combined)
 *
 *  This source code is unpublished proprietary information
 *  constituting, or derived under license from AT&T's Unix(r) System V.
 *
 *
 *
 *		Copyright Notice
 *
 *  Notice of copyright on this source code product does not indicate
 *  publication.
 *
 *  	(c) 1986, 1987, 1988, 1989, 1995  Sun Microsystems, Inc.
 *  	(c) 1983, 1984, 1985, 1986, 1987, 1988, 1989  AT&T.
 *		All rights reserved.
 */

#ident	"@(#)nfs_export.c	1.32	95/09/10 SMI"	/* SVr4.0 1.7	*/

#include <sys/types.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/socket.h>
#include <sys/errno.h>
#include <sys/uio.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/file.h>
#include <sys/tiuser.h>
#include <sys/kmem.h>
#include <sys/pathname.h>
#include <sys/debug.h>
#include <netinet/in.h>
#include <rpc/types.h>
#include <rpc/auth.h>
#include <rpc/auth_unix.h>
#include <rpc/auth_des.h>
#include <nfs/nfs.h>
#include <nfs/export.h>
#include <nfs/nfssys.h>
#include <rpc/clnt.h>
#include <sys/systm.h>
#include <sys/vtrace.h>

#define	EXPTABLESIZE 16

static struct exportinfo *exptable[EXPTABLESIZE];

static int	unexport(fsid_t *, fid_t *);
static int	findexivp(struct exportinfo **, vnode_t *, vnode_t *, cred_t *);
static int	loadaddrs(struct exaddrlist *);
static int	loadrootnames(struct export *);
static int	loadkerbnames(struct export *);
static void	freenames(struct export *);
static void	freekerbnames(struct export *);
static void	exportfree(struct exportinfo *);
static int	eqaddr(struct netbuf *, struct netbuf *, struct netbuf *);

#define	exportmatch(exi, fsid, fid)	\
	(EQFSID(&(exi)->exi_fsid, (fsid)) && EQFID(&(exi)->exi_fid, (fid)))

/*
 * exported_lock	Read/Write lock that protects the exportinfo list.
 *			This lock must be held when searching or modifiying
 *			the exportinfo list.
 */
static krwlock_t exported_lock;

#define	exptablehash(fsid, fid) (nfs_fhhash((fsid), (fid)) & (EXPTABLESIZE - 1))

/*
 * File handle hash function, good for producing hash values 16 bits wide.
 */
int
nfs_fhhash(fsid_t *fsid, fid_t *fid)
{
	register short *data;
	register int i, len;
	short h;

	ASSERT(fid != NULL);

	data = (short *)fid->fid_data;

	/* fid_data must be aligned on a short */
	ASSERT((((long)data) & (sizeof (short) - 1)) == 0);

	if (fid->fid_len == 10) {
		/*
		 * probably ufs: hash on bytes 4,5 and 8,9
		 */
		return (fsid->val[0] ^ data[2] ^ data[4]);
	}

	if (fid->fid_len == 6) {
		/*
		 * probably hsfs: hash on bytes 0,1 and 4,5
		 */
		return ((fsid->val[0] ^ data[0] ^ data[2]));
	}

	/*
	 * Some other file system. Assume that every byte is
	 * worth hashing.
	 */
	h = (short)fsid->val[0];

	/*
	 * Sanity check the length before using it
	 * blindly in case the client trashed it.
	 */
	if (fid->fid_len > NFS_FHMAXDATA)
		len = 0;
	else
		len = fid->fid_len / sizeof (short);

	/*
	 * This will ignore one byte if len is not a multiple of
	 * of sizeof (short). No big deal since we at least get some
	 * variation with fsid->val[0];
	 */
	for (i = 0; i < len; i++)
		h ^= data[i];

	return ((int)h);
}

/*
 * Counted byte string compare routine, optimized for file ids.
 */
int
nfs_fhbcmp(char *d1, char *d2, int l)
{
	register int k;

	/*
	 * We are always passed pointers to the data portions of
	 * two fids, where pointers are always 2 bytes from 32 bit
	 * alignment. If the length is also 2 bytes off word alignment,
	 * we can do word compares, because the two bytes before the fid
	 * data are always the length packed into a 16 bit short, so we
	 * can safely start our comparisons at d1-2 and d2-2.
	 * If the length is 2 bytes off word alignment, that probably
	 * means that first two bytes are zeroes. This means that
	 * first word in each fid, including the length are going to be
	 * equal (we wouldn't call fhbcmp if the lengths weren't the
	 * same). Thus it makes the most sense to start comparing the
	 * last words of each data portion.
	 */

	if ((l & 0x3) == 2) {
		/*
		 * We are going move the data pointers to the
		 * last word. Adding just the length, puts us to the
		 * word past end of the data. So reduce length by one
		 * word length.
		 */
		k = l - 4;
		/*
		 * Both adjusted length and the data pointer are offset two
		 * bytes from word alignment. Adding them together gives
		 * us word alignment.
		 */
		d1 += k;
		d2 += k;
		l += 2;
		while (l -= 4) {
			if (*(long *)d1 != *(long *)d2)
				return (1);
			d1 -= 4;
			d2 -= 4;
		}
	} else {
		while (l--) {
			if (*d1++ != *d2++)
				return (1);
		}
	}
	return (0);
}

/*
 * Initialization routine for export routines. Should only be called once.
 */
int
nfs_exportinit(void)
{

	rw_init(&exported_lock, "exported_lock", RW_DEFAULT, DEFAULT_WT);
	return (0);
}

/*
 * Exportfs system call
 */
int
exportfs(struct exportfs_args *uap, cred_t *cr)
{
	vnode_t *vp;
	struct export *kex;
	struct exportinfo *exi;
	struct exportinfo *ex, *prev;
	fid_t fid;
	fsid_t fsid;
	int mounted_ro;
	int error;
	int exporthash;

	if (!suser(cr))
		return (EPERM);

	/*
	 * Get the vfs id
	 */
	error = lookupname(uap->dname, UIO_USERSPACE, FOLLOW, NULL, &vp);
	if (error)
		return (error);
	bzero((char *) &fid, sizeof (fid));
	fid.fid_len = MAXFIDSZ;
	error = VOP_FID(vp, &fid);
	fsid = vp->v_vfsp->vfs_fsid;
	mounted_ro = vp->v_vfsp->vfs_flag & VFS_RDONLY;
	VN_RELE(vp);
	if (error) {
		/*
		 * If VOP_FID returns ENOSPC then the fid supplied
		 * is too small.  For now we simply return EREMOTE.
		 */
		if (error == ENOSPC)
			error = EREMOTE;
		return (error);
	}

	if (uap->uex == NULL) {
		error = unexport(&fsid, &fid);
		return (error);
	}
	exi = (struct exportinfo *) kmem_zalloc(sizeof (*exi), KM_SLEEP);
	exi->exi_fsid = fsid;
	exi->exi_fid = fid;

	/*
	 * Build up the template fhandle
	 */
	exi->exi_fh.fh_fsid = fsid;
	if (exi->exi_fid.fid_len > sizeof (exi->exi_fh.fh_xdata)) {
		error = EREMOTE;
		goto error_return;
	}
	exi->exi_fh.fh_xlen = exi->exi_fid.fid_len;
	bcopy(exi->exi_fid.fid_data, exi->exi_fh.fh_xdata,
		exi->exi_fid.fid_len);

	exi->exi_fh.fh_len = sizeof (exi->exi_fh.fh_data);

	kex = &exi->exi_export;

	/*
	 * Load in everything, and do sanity checking
	 */
	if (copyin((caddr_t) uap->uex, (caddr_t) kex,
	    (u_int) sizeof (struct export))) {
		error = EFAULT;
		goto error_return;
	}
	if (kex->ex_flags & ~EX_ALL) {
		error = EINVAL;
		goto error_return;
	}
	if (!(kex->ex_flags & EX_RDONLY) && mounted_ro) {
		error = EROFS;
		goto error_return;
	}
	if (kex->ex_flags & EX_EXCEPTIONS) {
		error = loadaddrs(&kex->ex_roaddrs);
		if (error)
			goto error_return;
		error = loadaddrs(&kex->ex_rwaddrs);
		if (error)
			goto error_return;
	}
	switch (kex->ex_auth) {
	case AUTH_UNIX:
		error = loadaddrs(&kex->ex_unix.rootaddrs);
		break;
	case AUTH_DES:
		error = loadrootnames(kex);
		break;
	case AUTH_KERB:
		error = loadkerbnames(kex);
		break;
	default:
		error = EINVAL;
	}
	if (error) {
		goto error_return;
	}

	/*
	 * Insert the new entry at the front of the export list
	 */
	rw_enter(&exported_lock, RW_WRITER);
	exporthash = exptablehash(&exi->exi_fsid, &exi->exi_fid);
	exi->exi_hash = exptable[exporthash];
	exptable[exporthash] = exi;

	/*
	 * Check the rest of the list for an old entry for the fs.
	 * If one is found then unlink it, wait until this is the
	 * only reference and then free it.
	 */
	prev = exi;
	for (ex = prev->exi_hash; ex; prev = ex, ex = ex->exi_hash) {
		if (exportmatch(ex, &exi->exi_fsid, &exi->exi_fid)) {
			prev->exi_hash = ex->exi_hash;
			break;
		}
	}
	rw_exit(&exported_lock);
	if (ex) {
		exportfree(ex);
	}
	return (0);

error_return:
	kmem_free((char *)exi, sizeof (*exi));
	return (error);
}


/*
 * Remove the exported directory from the export list
 */
static int
unexport(fsid_t *fsid, fid_t *fid)
{
	struct exportinfo **tail;
	struct exportinfo *exi;

	rw_enter(&exported_lock, RW_WRITER);
	tail = &exptable[exptablehash(fsid, fid)];
	while (*tail != NULL) {
		if (exportmatch(*tail, fsid, fid)) {
			exi = *tail;
			*tail = (*tail)->exi_hash;
			rw_exit(&exported_lock);
			exportfree(exi);
			return (0);
		}
		tail = &(*tail)->exi_hash;
	}
	rw_exit(&exported_lock);
	return (EINVAL);
}

/*
 * Get file handle system call.
 * Takes file name and returns a file handle for it.
 */
int
nfs_getfh(struct nfs_getfh_args *uap, cred_t *cr)
{
	fhandle_t fh;
	vnode_t *vp;
	vnode_t *dvp;
	struct exportinfo *exi;
	int error;

	if (!suser(cr))
		return (EPERM);

	error = lookupname(uap->fname, UIO_USERSPACE, FOLLOW, &dvp, &vp);
	if (error == EINVAL) {
		/*
		 * if fname resolves to / we get EINVAL error
		 * since we wanted the parent vnode. Try again
		 * with NULL dvp.
		 */
		error = lookupname(uap->fname, UIO_USERSPACE, FOLLOW, NULL,
				    &vp);
		dvp = NULL;
	}
	if (!error && vp == NULL) {
		/*
		 * Last component of fname not found
		 */
		if (dvp) {
			VN_RELE(dvp);
		}
		error = ENOENT;
	}
	if (error)
		return (error);
	error = findexivp(&exi, dvp, vp, cr);
	if (!error) {
		error = makefh(&fh, vp, exi);
		rw_exit(&exported_lock);
		if (!error) {
			if (copyout((caddr_t)&fh, (caddr_t)uap->fhp,
			    sizeof (fh)))
				error = EFAULT;
		}
	}
	VN_RELE(vp);
	if (dvp != NULL) {
		VN_RELE(dvp);
	}
	return (error);
}

/*
 * Strategy: if vp is in the export list, then
 * return the associated file handle. Otherwise, ".."
 * once up the vp and try again, until the root of the
 * filesystem is reached.
 */
static int
findexivp(struct exportinfo **exip, vnode_t *dvp, vnode_t *vp, cred_t *cr)
{
	fid_t fid;
	int error;

	VN_HOLD(vp);
	if (dvp != NULL) {
		VN_HOLD(dvp);
	}
	for (;;) {
		bzero((char *)&fid, sizeof (fid));
		fid.fid_len = MAXFIDSZ;
		error = VOP_FID(vp, &fid);
		if (error) {
			/*
			 * If VOP_FID returns ENOSPC then the fid supplied
			 * is too small.  For now we simply return EREMOTE.
			 */
			if (error == ENOSPC)
				error = EREMOTE;
			break;
		}
		*exip = findexport(&vp->v_vfsp->vfs_fsid, &fid);
		if (*exip != NULL) {
			/*
			 * Found the export info
			 */
			break;
		}

		/*
		 * We have just failed finding a matching export.
		 * If we're at the root of this filesystem, then
		 * it's time to stop (with failure).
		 */
		if (vp->v_flag & VROOT) {
			error = EINVAL;
			break;
		}

		/*
		 * Now, do a ".." up vp. If dvp is supplied, use it,
		 * otherwise, look it up.
		 */
		if (dvp == NULL) {
			error = VOP_LOOKUP(vp, "..", &dvp, NULL, 0, NULL, cr);
			if (error)
				break;
		}
		VN_RELE(vp);
		vp = dvp;
		dvp = NULL;
	}
	VN_RELE(vp);
	if (dvp != NULL) {
		VN_RELE(dvp);
	}
	return (error);
}

/*
 * Make an fhandle from a vnode
 */
int
makefh(fhandle_t *fh, vnode_t *vp, struct exportinfo *exi)
{
	int error;

	ASSERT(RW_READ_HELD(&exported_lock));

	*fh = exi->exi_fh;	/* struct copy */

	error = VOP_FID(vp, (fid_t *)&fh->fh_len);
	if (error) {
		/*
		 * Should be something other than EREMOTE
		 */
		return (EREMOTE);
	}
	return (0);
}

/*
 * Make an nfs_fh3 from a vnode
 */
int
makefh3(nfs_fh3 *fh, vnode_t *vp, struct exportinfo *exi)
{
	int error;

	ASSERT(RW_READ_HELD(&exported_lock));

	fh->fh3_length = sizeof (fh->fh3_u.nfs_fh3_i);
	fh->fh3_u.nfs_fh3_i.fh3_i = exi->exi_fh;	/* struct copy */

	error = VOP_FID(vp, (fid_t *)&fh->fh3_len);
	if (error) {
		/*
		 * Should be something other than EREMOTE
		 */
		return (EREMOTE);
	}
	return (0);
}

/*
 * Convert an fhandle into a vnode.
 * Uses the file id (fh_len + fh_data) in the fhandle to get the vnode.
 * WARNING: users of this routine must do a VN_RELE on the vnode when they
 * are done with it.
 */
vnode_t *
nfs_fhtovp(fhandle_t *fh, struct exportinfo *exi)
{
	register vfs_t *vfsp;
	vnode_t *vp;
	int error;

	TRACE_0(TR_FAC_NFS, TR_FHTOVP_START,
		"fhtovp_start");

	if (exi == NULL) {
		TRACE_1(TR_FAC_NFS, TR_FHTOVP_END,
			"fhtovp_end:(%S)", "exi NULL");
		return (NULL);	/* not exported */
	}
	vfsp = getvfs(&fh->fh_fsid);
	if (vfsp == NULL) {
		TRACE_1(TR_FAC_NFS, TR_FHTOVP_END,
			"fhtovp_end:(%S)", "getvfs NULL");
		return (NULL);
	}
	error = VFS_VGET(vfsp, &vp, (fid_t *)&(fh->fh_len));
	if (error || vp == NULL) {
		TRACE_1(TR_FAC_NFS, TR_FHTOVP_END,
			"fhtovp_end:(%S)", "VFS_GET failed or vp NULL");
		return (NULL);
	}
	TRACE_1(TR_FAC_NFS, TR_FHTOVP_END,
		"fhtovp_end:(%S)", "end");
	return (vp);
}

/*
 * Convert an nfs_fh3 into a vnode.
 * Uses the file id (fh_len + fh_data) in the file handle to get the vnode.
 * WARNING: users of this routine must do a VN_RELE on the vnode when they
 * are done with it.
 */
vnode_t *
nfs3_fhtovp(nfs_fh3 *fh, struct exportinfo *exi)
{
	register vfs_t *vfsp;
	vnode_t *vp;
	int error;

	if (exi == NULL)
		return (NULL);	/* not exported */

	if (fh->fh3_length != 32)	/* XXX */
		return (NULL);

	vfsp = getvfs(&fh->fh3_fsid);
	if (vfsp == NULL)
		return (NULL);

	error = VFS_VGET(vfsp, &vp, (fid_t *)&(fh->fh3_len));
	if (error || vp == NULL)
		return (NULL);

	return (vp);
}

/*
 * Find the export structure associated with the given filesystem
 * If found, then the read lock on the exports list is left to
 * indicate that an entry is still busy.
 */
/*
 * findexport() is splited into findexport() and checkexport() to fix
 * 1177604 so that checkexport() can be called by procedures that had
 * already obtained exported_lock to check the exptable.
 */
struct exportinfo *
checkexport(fsid_t *fsid, fid_t *fid)
{
	struct exportinfo *exi;

	ASSERT(RW_READ_HELD(&exported_lock));
	for (exi = exptable[exptablehash(fsid, fid)];
	    exi != NULL;
	    exi = exi->exi_hash) {
		if (exportmatch(exi, fsid, fid))
			return (exi);
	}
	return (NULL);
}

struct exportinfo *
findexport(fsid_t *fsid, fid_t *fid)
{
	struct exportinfo *exi;

	rw_enter(&exported_lock, RW_READER);
	if ((exi = checkexport(fsid, fid)) != NULL) {
		return (exi);
	}
	rw_exit(&exported_lock);
	return (NULL);
}

/*
 * Load from user space a list of exception addresses and masks
 */
static int
loadaddrs(struct exaddrlist *addrs)
{
	char *tmp;
	int allocsize;
	register int i;
	struct netbuf *uaddrs;
	struct netbuf *umasks;

	if (addrs->naddrs > EXMAXADDRS)
		return (EINVAL);
	if (addrs->naddrs == 0)
		return (0);

	allocsize = addrs->naddrs * sizeof (struct netbuf);
	uaddrs = addrs->addrvec;
	umasks = addrs->addrmask;

	addrs->addrvec = (struct netbuf *) kmem_alloc(allocsize, KM_SLEEP);
	if (copyin((caddr_t)uaddrs, (caddr_t)addrs->addrvec,
	    (u_int)allocsize)) {
		kmem_free((char *)addrs->addrvec, allocsize);
		return (EFAULT);
	}

	addrs->addrmask = (struct netbuf *) kmem_alloc(allocsize, KM_SLEEP);
	if (copyin((caddr_t)umasks, (caddr_t)addrs->addrmask,
	    (u_int)allocsize)) {
		kmem_free((char *)addrs->addrmask, allocsize);
		kmem_free((char *)addrs->addrvec, allocsize);
		return (EFAULT);
	}

	for (i = 0; i < addrs->naddrs; i++) {
		tmp = (char *) kmem_alloc(addrs->addrvec[i].len, KM_SLEEP);
		if (copyin(addrs->addrvec[i].buf, tmp,
		    (u_int)addrs->addrvec[i].len)) {
			register int j;

			for (j = 0; j < i; j++) {
				kmem_free((char *)addrs->addrvec[j].buf,
				    addrs->addrvec[j].len);
			}
			kmem_free(tmp, addrs->addrvec[i].len);
			kmem_free((char *)addrs->addrmask, allocsize);
			kmem_free((char *)addrs->addrvec, allocsize);
			return (EFAULT);
		}
		addrs->addrvec[i].buf = tmp;
	}

	for (i = 0; i < addrs->naddrs; i++) {
		tmp = (char *) kmem_alloc(addrs->addrmask[i].len, KM_SLEEP);
		if (copyin(addrs->addrmask[i].buf, tmp,
		    (u_int)addrs->addrmask[i].len)) {
			register int j;

			for (j = 0; j < i; j++) {
				kmem_free((char *)addrs->addrmask[j].buf,
				    addrs->addrmask[j].len);
			}
			kmem_free(tmp, addrs->addrmask[i].len);
			for (j = 0; j < addrs->naddrs; j++) {
				kmem_free((char *)addrs->addrvec[j].buf,
				    addrs->addrvec[j].len);
			}
			kmem_free((char *)addrs->addrmask, allocsize);
			kmem_free((char *)addrs->addrvec, allocsize);
			return (EFAULT);
		}
		addrs->addrmask[i].buf = tmp;
	}

	return (0);
}

/*
 * Load from user space the root user names into kernel space
 * (AUTH_DES only)
 */
static int
loadrootnames(struct export *kex)
{
	int error;
	char *exnames[EXMAXROOTNAMES];
	int i;
	u_int len;
	char netname[MAXNETNAMELEN+1];
	u_int allocsize;

	if (kex->ex_des.nnames > EXMAXROOTNAMES)
		return (EINVAL);
	if (kex->ex_des.nnames == 0)
		return (0);

	/*
	 * Get list of names from user space
	 */
	allocsize =  kex->ex_des.nnames * sizeof (char *);
	if (copyin((char *)kex->ex_des.rootnames, (char *)exnames, allocsize))
		return (EFAULT);
	kex->ex_des.rootnames = (char **) kmem_zalloc(allocsize, KM_SLEEP);

	/*
	 * And now copy each individual name
	 */
	for (i = 0; i < kex->ex_des.nnames; i++) {
		error = copyinstr(exnames[i], netname, sizeof (netname), &len);
		if (error)
			goto freeup;
		kex->ex_des.rootnames[i] = mem_alloc(len + 1);
		bcopy(netname, kex->ex_des.rootnames[i], len);
		kex->ex_des.rootnames[i][len] = '\0';
	}
	return (0);

freeup:
	freenames(kex);
	return (error);
}

/*
 * Load from user space the root user names into kernel space
 * (AUTH_KERB only)
 */
static int
loadkerbnames(struct export *kex)
{
	int error;
	char *exnames[EXMAXROOTNAMES];
	int i;
	u_int len;
	char netname[MAXNETNAMELEN+1];
	u_int allocsize;

	if (kex->ex_kerb.nnames > EXMAXROOTNAMES)
		return (EINVAL);
	if (kex->ex_kerb.nnames == 0)
		return (0);

	/*
	 * Get list of names from user space
	 */
	allocsize =  kex->ex_kerb.nnames * sizeof (char *);
	if (copyin((char *)kex->ex_kerb.rootnames, (char *)exnames, allocsize))
		return (EFAULT);
	kex->ex_kerb.rootnames = (char **) kmem_zalloc(allocsize, KM_SLEEP);

	/*
	 * And now copy each individual name
	 */
	for (i = 0; i < kex->ex_kerb.nnames; i++) {
		error = copyinstr(exnames[i], netname, sizeof (netname), &len);
		if (error)
			goto freeup;
		kex->ex_kerb.rootnames[i] = mem_alloc(len + 1);
		bcopy(netname, kex->ex_kerb.rootnames[i], len);
		kex->ex_kerb.rootnames[i][len] = '\0';
	}
	return (0);

freeup:
	freekerbnames(kex);
	return (error);
}

/*
 * Figure out everything we allocated in a root user name list in
 * order to free it up. (AUTH_DES only)
 */
static void
freenames(struct export *ex)
{
	int i;

	for (i = 0; i < ex->ex_des.nnames; i++) {
		if (ex->ex_des.rootnames[i] != NULL) {
			kmem_free((char *)ex->ex_des.rootnames[i],
				strlen(ex->ex_des.rootnames[i]) + 1);
		}
	}
	kmem_free((char *)ex->ex_des.rootnames,
	    ex->ex_des.nnames * sizeof (char *));
}

/*
 * Figure out everything we allocated in a root user name list in
 * order to free it up. (AUTH_KERB only)
 */
static void
freekerbnames(struct export *ex)
{
	int i;

	for (i = 0; i < ex->ex_kerb.nnames; i++) {
		if (ex->ex_kerb.rootnames[i] != NULL) {
			kmem_free((char *)ex->ex_kerb.rootnames[i],
				strlen(ex->ex_kerb.rootnames[i]) + 1);
		}
	}
	kmem_free((char *)ex->ex_kerb.rootnames,
		ex->ex_kerb.nnames * sizeof (char *));
}


/*
 * Free an entire export list node
 */
static void
exportfree(struct exportinfo *exi)
{
	register int i;
	struct export *ex;

	ex = &exi->exi_export;
	switch (ex->ex_auth) {
	case AUTH_UNIX:
		for (i = 0; i < ex->ex_unix.rootaddrs.naddrs; i++) {
			kmem_free(ex->ex_unix.rootaddrs.addrvec[i].buf,
				ex->ex_unix.rootaddrs.addrvec[i].len);
			kmem_free(ex->ex_unix.rootaddrs.addrmask[i].buf,
				ex->ex_unix.rootaddrs.addrmask[i].len);
		}
		kmem_free((char *)ex->ex_unix.rootaddrs.addrvec,
			ex->ex_unix.rootaddrs.naddrs * sizeof (struct netbuf));
		kmem_free((char *)ex->ex_unix.rootaddrs.addrmask,
			ex->ex_unix.rootaddrs.naddrs * sizeof (struct netbuf));
		break;
	case AUTH_DES:
		freenames(ex);
		break;
	case AUTH_KERB:
		freekerbnames(ex);
		break;
	}
	if (ex->ex_flags & EX_EXCEPTIONS) {
		for (i = 0; i < ex->ex_roaddrs.naddrs; i++) {
			kmem_free(ex->ex_roaddrs.addrvec[i].buf,
				ex->ex_roaddrs.addrvec[i].len);
			kmem_free(ex->ex_roaddrs.addrmask[i].buf,
				ex->ex_roaddrs.addrmask[i].len);
		}
		kmem_free((char *)ex->ex_roaddrs.addrvec,
			ex->ex_roaddrs.naddrs * sizeof (struct netbuf));
		kmem_free((char *)ex->ex_roaddrs.addrmask,
			ex->ex_roaddrs.naddrs * sizeof (struct netbuf));

		for (i = 0; i < ex->ex_rwaddrs.naddrs; i++) {
			kmem_free(ex->ex_rwaddrs.addrvec[i].buf,
				ex->ex_rwaddrs.addrvec[i].len);
			kmem_free(ex->ex_rwaddrs.addrmask[i].buf,
				ex->ex_rwaddrs.addrmask[i].len);
		}
		kmem_free((char *)ex->ex_rwaddrs.addrvec,
			ex->ex_rwaddrs.naddrs * sizeof (struct netbuf));
		kmem_free((char *)ex->ex_rwaddrs.addrmask,
			ex->ex_rwaddrs.naddrs * sizeof (struct netbuf));
	}
	kmem_free((char *)exi, sizeof (*exi));
}

/*
 * Free the export lock
 */
void
export_rw_exit(void)
{

	rw_exit(&exported_lock);
}

/*
 *	Determine whether two addresses are equal.
 *
 *	This is not as easy as it seems, since netbufs are opaque addresses
 *	and we're really concerned whether the host parts of the addresses
 *	are equal.  The solution is to check the supplied mask, whose address
 *	bits are 1 if we should compare the corresponding bits in addr1 and
 *	addr2, and 0 otherwise.
 */
static int
eqaddr(struct netbuf *addr1, struct netbuf *addr2, struct netbuf *mask)
{
	register char *a1, *a2, *m, *mend;

	if ((addr1->len != addr2->len) || (addr1->len != mask->len))
		return (0);

	for (a1 = addr1->buf,
		a2 = addr2->buf,
		m = mask->buf,
		mend = mask->buf + mask->len; m < mend; a1++, a2++, m++)
		if (((*a1) & (*m)) != ((*a2) & (*m)))
			return (0);
	return (1);
}

int
hostinlist(struct netbuf *na, struct exaddrlist *addrs)
{
	int i;

	for (i = 0; i < addrs->naddrs; i++) {
		if (eqaddr(na, &addrs->addrvec[i], &addrs->addrmask[i])) {
			return (1);
		}
	}
	return (0);
}

/*
 * The following routine is used only by the WabiGetfh()
 * routine of the Wabi driver to obtain filehandles
 * for locking.
 *
 * XXX This is a contract private interface
 * between Wabi and NFS and is to be superceded by an
 * improved fcntl() interface in 2.6.
 */

#include <nfs/nfs_clnt.h>
#include <nfs/rnode.h>
#include <sys/cmn_err.h>

int
wabi_makefhandle(int fd, struct nfs_fhandle *fh, int *vers)
{
	struct file *fp = NULL;
	struct vnode *vp;
	int ret = 0;
	int error = 0;
	struct exportinfo exi = {0};

	/*
	 * Get the file pointer from the file descriptor
	 */
	if ((fp = GETF(fd)) == NULL)
		return (EBADF);

	vp = (struct vnode *)fp->f_vnode;
	if (vp == NULL) {
		ret = EINVAL;
		goto done;
	}

	/*
	 * Special case NFS vnodes to return back known file handles
	 */
	if (vp->v_op == &nfs_vnodeops) {
		fh->fh_len = NFS_FHSIZE;
		bcopy((caddr_t)VTOFH(vp), fh->fh_buf, NFS_FHSIZE);
		*vers = NFS_VERSION;
	} else if (vp->v_op == &nfs3_vnodeops) {
		nfs_fh3 *fh3;
		fh3 = VTOFH3(vp);
		fh->fh_len = fh3->fh3_length;
		bcopy((caddr_t)fh3->fh3_u.data, fh->fh_buf, fh->fh_len);
		*vers = NFS_V3;
	} else {
		/*
		 * Construct a file handle for the underlying non-NFS
		 * filesystem.  Bogusity is that V2 and V3 file handles
		 * contain the same data now, this may change in the
		 * future but we will take advantage of it now and only
		 * build a V2 based fhandle.
		 *
		 * Normally the fhandle has fid info for the exported
		 * filesys in its xlen,xdata elements but since we are
		 * local such information may not exist (filesys does
		 * not have to be exported). This problem has been
		 * recognized in the implementation of the sharing
		 * daemon (uts/common/klm/lm_nlm_server.c) in that it
		 * ignores the exportinfo when mapping a file handle to vnode.
		 *
		 * makefh requires that the exportinfo passed in contains
		 * a template file handle with the fsid, fh_xdata, and
		 * fh_xlen filled in. Fill in the minimal amount
		 * of information needed, just the exi_fh. Leave the
		 * fh_xdata all zeros.
		 */
		exi.exi_fh.fh_fsid = vp->v_vfsp->vfs_fsid;
		exi.exi_fh.fh_xlen = sizeof (exi.exi_fh.fh_xdata);
		exi.exi_fh.fh_len = sizeof (exi.exi_fh.fh_data);

		*vers = NFS_VERSION;

		fh->fh_len = NFS_FHSIZE;

		/*
		 * Must get a readers lock on the exported_lock for makefh.
		 */
		rw_enter(&exported_lock, RW_READER);
		ret = makefh((fhandle_t *)fh->fh_buf, vp, &exi);
		rw_exit(&exported_lock);

		if (ret) {
			cmn_err(CE_WARN,
				"wabi_makefhandle failed, returned %d\n", ret);
			/* FALLTHRU */
		}
	}
done:
	/*
	 * Decrement open file counter
	 */
	RELEASEF(fd);
	return (ret);
}
