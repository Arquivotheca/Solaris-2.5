/*	copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
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
 * 	(c) 1986,1987,1988,1989  Sun Microsystems, Inc
 * 	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *		All rights reserved.
 *
 */

/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#pragma	ident	"@(#)ufs_inode.c	2.135	95/10/15 SMI"
/* From SunOS 2.56 */

#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bitmap.h>
#include <sys/signal.h>
#include <sys/cred.h>
#include <sys/user.h>
#include <sys/vfs.h>
#include <sys/stat.h>
#include <sys/vnode.h>
#include <sys/buf.h>
#include <sys/proc.h>
#include <sys/disp.h>
#include <sys/dnlc.h>
#include <sys/mode.h>
#include <sys/cmn_err.h>
#include <sys/kstat.h>
#include <sys/acl.h>
#include <sys/fs/ufs_inode.h>
#include <sys/fs/ufs_fs.h>
#include <sys/fs/ufs_trans.h>
#include <sys/fs/ufs_acl.h>
#ifdef QUOTA
#include <sys/fs/ufs_quota.h>
#endif
#include <vm/hat.h>
#include <vm/as.h>
#include <vm/pvn.h>
#include <vm/seg.h>
#include <sys/swap.h>
#include <sys/cpuvar.h>
#include <sys/sysmacros.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/debug.h>
#include <fs/fs_subr.h>

struct kmem_cache *inode_cache;		/* cache of free inodes */

/* UFS Inode Cache Stats -- Not protected */
struct	instats ins = {
	{ "size",		KSTAT_DATA_ULONG },
	{ "maxsize",		KSTAT_DATA_ULONG },
	{ "hits",		KSTAT_DATA_ULONG },
	{ "misses",		KSTAT_DATA_ULONG },
	{ "mallocs",		KSTAT_DATA_ULONG },
	{ "frees",		KSTAT_DATA_ULONG },
	{ "maxsize reached",	KSTAT_DATA_ULONG },
	{ "puts at frontlist",	KSTAT_DATA_ULONG },
	{ "puts at backlist",	KSTAT_DATA_ULONG },
	{ "queues to free",	KSTAT_DATA_ULONG },
	{ "scans",		KSTAT_DATA_ULONG }
};

/* kstat data */
kstat_named_t	*instats_ptr = (kstat_named_t *)&ins;
ulong_t		instats_ndata = sizeof (ins) / sizeof (kstat_named_t);
kstat_t		*ufs_inode_kstat = NULL;

int ino_new;		/* Current # of inodes kmem_allocated. */

struct inode	*ireuse;		/* protected by ireuse_mutex */
int		nireuse;		/* # of entries on ireuse list */
kmutex_t	ireuse_mutex;		/* protects reuse list */

union ihead *ihead;	/* inode LRU cache, Chris Maltby */
kmutex_t *ih_lock;	/* protect inode cache hash table */
int *ih_ne;		/* # of inodes on this chain (debug) */
int ino_hashlen = 4;	/* desired average hash chain length */
int inohsz;		/* number of buckets in the hash table */
int sizeofcg;		/* sizeof (struct cg) (not including cg_space[]) */

kmutex_t	ufsvfs_mutex;
struct ufsvfs	*oldufsvfslist, *ufsvfslist;

/*
 * the threads that process idle inodes and free (deleted) inodes
 * have high water marks that are set in ufsinit().
 * These values but can be no less then the minimum shown below
 */
int		ufs_idle_max;		/* # of allowable idle inodes */
#define	UFS_MIN_IDLE_MAX	(16)	/* min # of allowable idle inodes */

static void ihinit(void);
extern void pvn_vpzero();
extern int hash2ints(int, int);

/*
 * Initialize the vfs structure
 */

int ufsfstype;
extern struct vnodeops ufs_vnodeops;
extern struct vfsops ufs_vfsops;

/*ARGSUSED*/
static int
ufs_inode_kstat_update(kstat_t *ksp, int rw)
{
	if (rw == KSTAT_WRITE)
		return (EACCES);
	ins.in_size.value.ul = ino_new;
	ins.in_maxsize.value.ul = ufs_ninode;
	return (0);
}

int
ufsinit(struct vfssw *vswp, int fstype)
{
	extern	kmutex_t	rename_lock;
	extern	kmutex_t	dirbufp_lock;
	struct cg		*cg	= NULL;
#if defined(DEBUG)
	/*
	 * XXX BJF disable debugging for harpy
	 */
	extern	int		ufs_debug;
	ufs_debug = 0;
#endif DEBUG

	/*
	 * needed for harpy
	 */
	sizeofcg = (caddr_t)&(cg->cg_space[0]) - (caddr_t)cg;

	/*
	 * idle thread runs when 100% of ufs_ninode entries are on the queue
	 */
	if (ufs_idle_max == 0)
		ufs_idle_max = ufs_ninode;
	if (ufs_idle_max == 0)
		ufs_idle_max = UFS_MIN_IDLE_MAX;

	ufs_thread_init(&ufs_idle_q, ufs_idle_max);
	ufs_thread_start(&ufs_idle_q, ufs_thread_idle, NULL);

	/*
	 * global hlock thread
	 */
	ufs_thread_init(&ufs_hlock, 1);
	ufs_thread_start(&ufs_hlock, ufs_thread_hlock, NULL);

	ihinit();
	mutex_init(&rename_lock, "rename lock", MUTEX_DEFAULT, DEFAULT_WT);
	mutex_init(&dirbufp_lock, "ufs dir kmem freelist", MUTEX_DEFAULT,
								DEFAULT_WT);
	mutex_init(&ireuse_mutex, "ireuse lock", MUTEX_DEFAULT, DEFAULT_WT);
	mutex_init(&ufsvfs_mutex, "ufsvfs mutex", MUTEX_DEFAULT, DEFAULT_WT);

	/*
	 * NOTE: Sun's auto-config initializes this in
	 * 	vfs_conf.c
	 * Associate vfs and vnode operations
	 */

	vswp->vsw_vfsops = &ufs_vfsops;
	ufsfstype = fstype;
#ifdef QUOTA
	/*
	 * I think this goes here.
	 */
	qtinit();
#endif
	if ((ufs_inode_kstat = kstat_create("ufs", 0, "inode_cache", "ufs",
	    KSTAT_TYPE_NAMED, instats_ndata, KSTAT_FLAG_VIRTUAL)) != NULL) {
		ufs_inode_kstat->ks_data = (void *) instats_ptr;
		ufs_inode_kstat->ks_update = ufs_inode_kstat_update;
		kstat_install(ufs_inode_kstat);
	}
	ufsfx_init();		/* fix-on-panic initialization */
	si_cache_init();
	return (0);
}

/*
 * Initialize hash links for inodes
 * and build inode free list.
 */
void
ihinit(void)
{
	register int i;
	register union	ihead *ih = ihead;
	char	buf[20];

	inohsz = 1 << highbit(ufs_ninode / ino_hashlen);
	ihead = kmem_zalloc(inohsz * sizeof (union ihead), KM_SLEEP);
	ih_lock = kmem_zalloc(inohsz * sizeof (kmutex_t), KM_SLEEP);
	ih_ne = UFS_DEBUG_ICACHE_ZALLOC(inohsz * sizeof (int), KM_SLEEP);

	for (i = 0, ih = ihead; i < inohsz; i++,  ih++) {
		ih->ih_head[0] = ih;
		ih->ih_head[1] = ih;
		(void) sprintf(buf, "inode $ %d", i);
		mutex_init(&ih_lock[i], buf, MUTEX_DEFAULT, DEFAULT_WT);
	}
	inode_cache = kmem_cache_create("inode_cache",
		sizeof (struct inode), 0, NULL, NULL, NULL);
}

/*
 * Look up an inode by device, inumber.  If it is in core (in the
 * inode structure), honor the locking protocol.  If it is not in
 * core, read it in from the specified device after freeing any pages.
 * In all cases, a pointer to a VN_HELD inode structure is returned.
 */
/* ARGSUSED */
int
ufs_iget(struct vfs *vfsp, ino_t ino, struct inode **ipp, struct cred *cr)
{
	struct inode *ip, *sp;
	union ihead *ih;
	kmutex_t *ihm;
	struct buf *bp;
	struct dinode *dp;
	struct vnode *vp;
	int error;
	int ftype;	/* XXX - Remove later on */
	dev_t vfs_dev;
	struct ufsvfs *ufsvfsp;
	struct fs *fs;
	int hno;
	daddr_t bno;
	u_long ioff;

	CPU_STAT_ADD_K(cpu_sysinfo.ufsiget, 1);

	UFS_DEBUG_ICACHE_CHECK();

	/*
	 * Lookup inode in cache.
	 */
	vfs_dev = vfsp->vfs_dev;
	hno = INOHASH(vfs_dev, ino);
	ih = &ihead[hno];
	ihm = &ih_lock[hno];

again:
	mutex_enter(ihm);
	for (ip = ih->ih_chain[0]; ip != (struct inode *)ih; ip = ip->i_forw) {
		if (ino != ip->i_number || vfs_dev != ip->i_dev)
			continue;
		/*
		 * Found the interesting inode; hold it and drop the cache lock
		 */
		vp = ITOV(ip);	/* for locknest */
		VN_HOLD(vp);
		mutex_exit(ihm);
		rw_enter(&ip->i_contents, RW_READER);

		/*
		 * if necessary, remove from idle list
		 */
		if ((ip->i_flag & IREF) == 0)
			ufs_rmidle(ip);

		/*
		 * i_number == 0 means bad initialization; try again
		 */
		if (ip->i_number == 0) {
			rw_exit(&ip->i_contents);
			VN_RELE(vp);
			goto again;
		}

		ins.in_hits.value.ul++;
		*ipp = ip;
		rw_exit(&ip->i_contents);
		UFS_DEBUG_ICACHE_CHECK();
		return (0);
	}
	mutex_exit(ihm);

	/*
	 * Inode was not in cache.
	 *
	 * Use the first entry on the reuse list or allocate a new entry
	 */
	ins.in_misses.value.ul++;
	ufsvfsp = (struct ufsvfs *)vfsp->vfs_data;
	fs = ufsvfsp->vfs_fs;
	mutex_enter(&ireuse_mutex);
	if ((ip = ireuse) != 0) {
		ireuse = ip->i_freef;
		--nireuse;
	} else
		ino_new++;
	mutex_exit(&ireuse_mutex);

	if (ip == NULL) {
		ip = kmem_cache_alloc(inode_cache, KM_SLEEP);
		vp = ITOV(ip);

		rw_init(&ip->i_rwlock, "inode rwlock", RW_DEFAULT, DEFAULT_WT);
		rw_init(&ip->i_contents, "inode contents rwlock",
							RW_DEFAULT, DEFAULT_WT);
		mutex_init(&ip->i_tlock, "i_tlock", MUTEX_DEFAULT, DEFAULT_WT);
		mutex_init(&vp->v_lock, "ufs v_lock", MUTEX_DEFAULT,
								DEFAULT_WT);
		cv_init(&vp->v_cv, "vnode cv", CV_DEFAULT, NULL);

		ip->i_forw = ip;
		ip->i_back = ip;
		ip->i_mapcnt = 0;
		ip->i_dquot = NULL;
		ip->i_owner = NULL;
		cv_init(&ip->i_wrcv, "write_lock", CV_DRIVER, NULL);
		ins.in_malloc.value.ul++;
		ins.in_maxsize.value.ul = MAX(ins.in_maxsize.value.ul, ino_new);
	}

	/*
	 * at this point, we either have a newly allocated inode or
	 * an inode that no one else can reference (a really free inode)
	 */

	ip->i_freef = ip;
	ip->i_freeb = ip;
	ip->i_flag = IREF;
	ip->i_dev = vfs_dev;
	ip->i_ufsvfs = ufsvfsp;
	ip->i_devvp = ufsvfsp->vfs_devvp;
	ip->i_number = ino;
	ip->i_diroff = 0;
	ip->i_nextr = 0;
	ip->i_vcode = 0;
	ip->i_map = NULL;
	ip->i_rdev = 0;
	ip->i_writes = 0;
	ip->i_mode = 0;
	ip->i_delaylen = 0;
	ip->i_delayoff = 0;
	ip->i_nextrio = 0;
	ip->i_ufs_acl = NULL;
	ip->i_vcode = 0;

	bno = fsbtodb(fs, itod(fs, ino));
	ioff = (sizeof (struct dinode)) * (itoo(fs, ino));
	ip->i_doff = (offset_t)ioff + ldbtob(bno);

	/*
	 * initialize most of the vnode stuff;
	 */
	vp = ITOV(ip);
	if (ino == (ino_t)UFSROOTINO)
		vp->v_flag = VROOT;
	else
		vp->v_flag = 0;
	vp->v_count = 1;
	vp->v_op = &ufs_vnodeops;
	vp->v_vfsp = vfsp;
	vp->v_data = (caddr_t)ip;
	vp->v_vfsmountedhere = NULL;
	vp->v_stream = NULL;
	vp->v_pages = NULL;
	vp->v_type = VNON;
	vp->v_rdev = 0;
	vp->v_filocks = NULL;

	/*
	 * put a place holder in the cache (if not already threre)
	 */
	mutex_enter(ihm);
	for (sp = ih->ih_chain[0]; sp != (struct inode *)ih; sp = sp->i_forw)
		if (ino == sp->i_number && vfs_dev == sp->i_dev) {
			mutex_exit(ihm);
			mutex_enter(&ireuse_mutex);
			++nireuse;
			ip->i_freef = ireuse;
			ireuse = ip;
			mutex_exit(&ireuse_mutex);
			goto again;
		}
	rw_enter(&ip->i_contents, RW_WRITER);
	insque(ip, ih);
	UFS_DEBUG_ICACHE_INC(hno);
	mutex_exit(ihm);
	/*
	 * read the dinode
	 */
	bp = bread(ip->i_dev, bno, (int)fs->fs_bsize);

	/*
	 * Check I/O errors
	 */
	error = ((bp->b_flags & B_ERROR) ? geterror(bp) : 0);
	if (error) {
		brelse(bp);
		ip->i_number = 0;	/* in case someone is looking it up */
		rw_exit(&ip->i_contents);

		/* We don't want a bad inode sitting in the cache */
		mutex_enter(ihm);
		rw_enter(&ip->i_contents, RW_WRITER);
		remque(ip);
		rw_exit(&ip->i_contents);
		mutex_exit(ihm);

		VN_RELE(vp);
		UFS_DEBUG_ICACHE_CHECK();
		return (error);
	}
	/*
	 * initialize the inode's dinode
	 */
	dp = (struct dinode *)(ioff + bp->b_un.b_addr);
	ip->i_ic = dp->di_ic;			/* structure assignment */
	brelse(bp);

#if defined(DEBUG)
	/*
	 * We can't believe the checksum on the disk
	 * because older versions of the ufs debugging code
	 * computed it incorrectly.  We recompute it here.
	 */
	set_icksum(ip);
	(void) ufs_ivalid(ip);
#endif /* DEBUG */

	/*
	 * do stupid things
	 */
	if (ip->i_suid != UID_LONG)
		ip->i_uid = ip->i_suid;
	if (ip->i_sgid != GID_LONG)
		ip->i_gid = ip->i_sgid;

	ftype = ip->i_mode & IFMT;
	if (ftype == IFBLK || ftype == IFCHR) {
		vp->v_rdev = ip->i_rdev = ((((ip->i_ordev & 0xFFFF0000) == 0) ||
				((ip->i_ordev & 0xFFFF0000) == 0xFFFF0000)) ?
				expdev(ip->i_ordev): ip->i_ordev);
	}
	/*
	 * finish initializing the vnode
	 */
	vp->v_type = IFTOVT((mode_t)ip->i_mode);

	/*
	 * an old DBE hack
	 */
	if ((ip->i_mode & (ISVTX | IEXEC | IFDIR)) == ISVTX)
		vp->v_flag |= VSWAPLIKE;

	/*
	 * read the shadow
	 */
	if (ftype != 0 && ip->i_shadow != 0) {
		if ((error = ufs_si_load(ip, cr)) != 0) {
			ip->i_number = 0;
			ip->i_ufs_acl = NULL;
			rw_exit(&ip->i_contents);
			VN_RELE(vp);
			UFS_DEBUG_ICACHE_CHECK();
			return (error);
		}
	}

#ifdef QUOTA
	if (ip->i_mode != 0)
		ip->i_dquot = getinoquota(ip);
#endif
	TRANS_MATA_IGET(ufsvfsp, ip);
	*ipp = ip;
	rw_exit(&ip->i_contents);
	UFS_DEBUG_ICACHE_CHECK();

	return (0);
}

/*
 * Update times and vrele associated vnode
 *
 *	DO NOT USE: Please see ufs_inode.h for new locking rules that
 *	have obsoleted the use of ufs_iput.  You should be using
 *	ITIMES, ITIMES_NOLOCK, and VN_RELE as appropriate.
 */
void
ufs_iput(struct inode *ip)
{
	ITIMES(ip);
	VN_RELE(ITOV(ip));
}

/*
 * Vnode is no longer referenced, write the inode out
 * and if necessary, truncate and deallocate the file.
 */
void
ufs_iinactive(struct inode *ip)
{
	int		front;
	struct inode	*iq;
	struct ufs_q	*uq;
	struct vnode	*vp;

	/*
	 * Get exclusive access to inode data.
	 */
	rw_enter(&ip->i_contents, RW_WRITER);
	vp = ITOV(ip);

	ASSERT(ip->i_flag & IREF);

	/*
	 * For umount case: if vfsp is NULL, the inode is unhashed
	 * and clean.  It can be safely destroyed (cyf).
	 */
	if (vp->v_vfsp == NULL) {
		rw_exit(&ip->i_contents);

		mutex_enter(&ireuse_mutex);
		ino_new--;
		mutex_exit(&ireuse_mutex);

		ufs_si_del(ip);
		cv_destroy(&ip->i_wrcv);  /* throttling */
		rw_destroy(&ip->i_rwlock);
		rw_destroy(&ip->i_contents);
		ASSERT((vp->v_type == VCHR) || (vp->v_pages == NULL));
		kmem_cache_free(inode_cache, ip);
		ins.in_mfree.value.ul++;
		return;
	}

	/*
	 * queue idle inode to appropriate thread (v_count remains 1)
	 */
	front = 1;
	if (ip->i_fs->fs_ronly == 0 && ip->i_mode && ip->i_nlink <= 0) {
		/*
		 * The inode is deleted.
		 */

		/*
		 * NOIDEL means that deletes are not allowed at this time;
		 * whoever resets NOIDEL will also send this inode back
		 * thru ufs_iinactive.  IREF remains set.
		 */
		if (ULOCKFS_IS_NOIDEL(ITOUL(ip))) {
			mutex_enter(&vp->v_lock);
			vp->v_count--;
			mutex_exit(&vp->v_lock);
			rw_exit(&ip->i_contents);
			return;
		}

		if (!TRANS_ISTRANS(ip->i_ufsvfs)) {
			rw_exit(&ip->i_contents);
			ufs_delete(ip->i_ufsvfs, ip, 0);
			return;
		}

		/* queue to delete thread; IREF remains set */
		ins.in_qfree.value.ul++;
		uq = &ip->i_ufsvfs->vfs_delete;
	} else {
		/*
		 * queue to idle thread
		 *	The inode may be `held' again between the VN_RELE
		 *	and the i_contents lock in ufs_iinactive(). Or at
		 *	any time by pageout or other poorly behaved
		 *	subsystems.  The inode will be put on the idle list
		 *	anyway, and the idle thread will detect its busyness.
		 *
		 */
		uq = &ufs_idle_q;

		/* clear IREF means `on idle list' */
		ip->i_flag &= ~IREF;

		/*
		 * lru iff it has pages or is a fastsymlink; otherwise mru
		 */
		if (vp->v_pages || ip->i_flag & IFASTSYMLNK) {
			ins.in_frback.value.ul++;
			front = 0;
		} else
			ins.in_frfront.value.ul++;
	}

	mutex_enter(&uq->uq_mutex);

	/* add to q */
	if ((iq = uq->uq_ihead) != 0) {
		ip->i_freef = iq;
		ip->i_freeb = iq->i_freeb;
		iq->i_freeb->i_freef = ip;
		iq->i_freeb = ip;
		if (front)
			uq->uq_ihead = ip;
	} else {
		uq->uq_ihead = ip;
		ip->i_freef = ip;
		ip->i_freeb = ip;
	}

	/* wakeup thread(s) if q is overfull */
	if (++uq->uq_ne == uq->uq_maxne)
		cv_broadcast(&uq->uq_cv);

	/* all done, release the q and inode */
	mutex_exit(&uq->uq_mutex);
	rw_exit(&ip->i_contents);
}

/*
 * Check accessed and update flags on an inode structure.
 * If any are on, update the inode with the (unique) current time.
 * If waitfor is given, insure I/O order so wait for write to complete.
 */
void
ufs_iupdat(struct inode *ip, int waitfor)
{
	struct buf	*bp;
	struct fs	*fp;
	struct dinode	*dp;
	int		fastsymflag;
	struct ufsvfs	*ufsvfsp 	= ip->i_ufsvfs;
	int 		i;
	u_short		flag;
	o_uid_t		suid;
	o_gid_t		sgid;

	ASSERT(RW_WRITE_HELD(&ip->i_contents));

	/*
	 * Return if file system has been forcibly umounted.
	 */
	if (ufsvfsp == NULL)
		return;

	fp = ip->i_fs;
	flag = ip->i_flag;	/* Atomic read */
	if ((flag & (IUPD|IACC|ICHG|IMOD|IMODACC|IATTCHG)) != 0) {
		if (fp->fs_ronly) {
			ip->i_flag &= ~(IUPD|IACC|ICHG|IMOD|IMODACC|IATTCHG);
			return;
		}
		/*
		 * fs is active while metadata is being written
		 */
		mutex_enter(&ufsvfsp->vfs_lock);
		ufs_notclean(ufsvfsp);
		/*
		 * get the dinode
		 *	we don't have to read the buf for a trans device
		 */
		if (TRANS_ISTRANS(ufsvfsp)) {
			bp = getblk(ip->i_dev,
			    (daddr_t)fsbtodb(fp, itod(fp, ip->i_number)),
			    (int)fp->fs_bsize);
			if ((bp->b_flags & B_DONE) == 0)
				bp->b_flags |= B_AGE;
		} else {
			bp = bread(ip->i_dev,
			    (daddr_t)fsbtodb(fp, itod(fp, ip->i_number)),
			    (int)fp->fs_bsize);
			if (bp->b_flags & B_ERROR) {
				ip->i_flag &=
				    ~(IUPD|IACC|ICHG|IMOD|IMODACC|IATTCHG);
				brelse(bp);
				return;
			}
		}
		/*
		 * munge inode fields
		 */
		ITIMES_NOLOCK(ip);

		if ((ip->i_flag & (IMOD|IMODACC)) == IMODACC) {
			TRANS_INODE_FIELD(ufsvfsp, ip->i_atime, ip);
			TRANS_INODE_FIELD(ufsvfsp, ip->i_mtime, ip);
		}

		ip->i_flag &= ~(IUPD|IACC|ICHG|IMOD|IMODACC|IATTCHG);
		fastsymflag = (ip->i_flag & IFASTSYMLNK);

		/*
		 * For SunOS 5.0->5.4, these lines below read:
		 *
		 * suid = (ip->i_uid > MAXUID) ? UID_LONG : ip->i_uid;
		 * sgid = (ip->i_gid > MAXUID) ? GID_LONG : ip->i_gid;
		 *
		 * where MAXUID was set to 60002.  This was incorrect -
		 * the uids should have been constrained to what fitted into
		 * a 16-bit word.
		 *
		 * This means that files from 4.x filesystems that have an
		 * i_suid field larger than 60002 will have that field
		 * changed to 65535.
		 *
		 * Security note: 4.x UFS could never create a i_suid of
		 * UID_LONG since that would've corresponded to -1.
		 */
		suid = (u_long)ip->i_uid > (u_long)USHRT_MAX ?
			UID_LONG : ip->i_uid;
		sgid = (u_long)ip->i_gid > (u_long)USHRT_MAX ?
			GID_LONG : ip->i_gid;

		if ((ip->i_suid != suid) || (ip->i_sgid != sgid)) {
			ip->i_suid = suid;
			ip->i_sgid = sgid;
			TRANS_INODE(ufsvfsp, ip);
		}

		if ((ip->i_mode&IFMT) == IFBLK || (ip->i_mode&IFMT) == IFCHR) {
			/* load first direct block only if special device */

			if (ip->i_rdev &
			    ~((O_MAXMAJ << L_BITSMINOR) | O_MAXMIN)) {
				/* can't use old format */
				ip->i_ordev = ip->i_rdev;
			} else {
				ip->i_ordev = cmpdev(ip->i_rdev);
			}
		}

		/*
		 * copy inode to dinode (zero fastsymlnk in dinode)
		 */
		dp = (struct dinode *)bp->b_un.b_addr + itoo(fp, ip->i_number);
#if 0
disabled for now since we don't zero the fastsymlink.  set_icksum should
simply take the dinode.
#if defined(DEBUG)
		set_icksum(ip);
#endif /* DEBUG */
#endif 0

		dp->di_ic = ip->i_ic;	/* structure assignment */
		if (fastsymflag) {
			for (i = 1; i < NDADDR && dp->di_db[i]; i++)
				dp->di_db[i] = 0;
			for (i = 0; i < NIADDR && dp->di_ib[i]; i++)
				dp->di_ib[i] = 0;
		}
		if (TRANS_ISTRANS(ufsvfsp)) {
			TRANS_LOG(ufsvfsp, (caddr_t)dp, (offset_t)ip->i_doff,
				sizeof (struct dinode));
			brelse(bp);
		} else if (waitfor && (ip->i_ufsvfs->vfs_dio == 0)) {
			bwrite(bp);

			/*
			 * Synchronous write has guaranteed that inode
			 * has been written on disk so clear the flag
			 */
			ip->i_flag &= ~(IBDWRITE);
		} else {
			bdwrite(bp);

			/*
			 * This write hasn't guaranteed that inode has been
			 * written on the disk.
			 * Since, all updat flags on indoe are cleared, we must
			 * remember the condition in case inode is to be updated
			 * synchronously later (e.g.- fsync()/fdatasync())
			 * and inode has not been modified yet.
			 */
			ip->i_flag |= (IBDWRITE);
		}
	} else {
		/*
		 * In case previous inode update was done asynchronously
		 * (IBDWRITE) and this inode update request wants guaranteed
		 * (synchronous) disk update, flush the inode.
		 */
		if (waitfor && (flag & IBDWRITE)) {
			blkflush(ip->i_dev,
				(daddr_t)fsbtodb(fp, itod(fp, ip->i_number)));
			ip->i_flag &= ~(IBDWRITE);
		}
	}
}

#define	SINGLE	0	/* index of single indirect block */
#define	DOUBLE	1	/* index of double indirect block */
#define	TRIPLE	2	/* index of triple indirect block */

/*
 * Release blocks associated with the inode ip and
 * stored in the indirect block bn.  Blocks are free'd
 * in LIFO order up to (but not including) lastbn.  If
 * level is greater than SINGLE, the block is an indirect
 * block and recursive calls to indirtrunc must be used to
 * cleanse other indirect blocks.
 *
 * N.B.: triple indirect blocks are untested.
 */
static long
indirtrunc(struct inode *ip, daddr_t bn, daddr_t lastbn, int level, int flags)
{
	register int i;
	struct buf *bp, *copy;
	register daddr_t *bap;
	struct ufsvfs *ufsvfsp = ip->i_ufsvfs;
	struct fs *fs = ufsvfsp->vfs_fs;
	daddr_t nb, last;
	long factor;
	int blocksreleased = 0, nblocks;

	ASSERT(RW_WRITE_HELD(&ip->i_contents));
	/*
	 * Calculate index in current block of last
	 * block to be kept.  -1 indicates the entire
	 * block so we need not calculate the index.
	 */
	factor = 1;
	for (i = SINGLE; i < level; i++)
		factor *= NINDIR(fs);
	last = lastbn;
	if (lastbn > 0)
		last /= factor;
	nblocks = btodb(fs->fs_bsize);
	/*
	 * Get buffer of block pointers, zero those
	 * entries corresponding to blocks to be free'd,
	 * and update on disk copy first.
	 * *Unless* the root pointer has been synchronously
	 * written to disk.  If nothing points to this
	 * indirect block then don't bother zero'ing and
	 * writing it.
	 */
	bp = bread(ip->i_dev, (daddr_t)fsbtodb(fs, bn), (int)fs->fs_bsize);
	if (bp->b_flags & B_ERROR) {
		brelse(bp);
		return (0);
	}
	bap = bp->b_un.b_daddr;
	if ((flags & I_CHEAP) == 0) {
		u_int	zb;

		zb = (u_int)((NINDIR(fs) - (last + 1)) * sizeof (daddr_t));

		if (zb) {
			/*
			 * push any data into the log before we zero it
			 */
			if (bp->b_flags & B_DELWRI)
				TRANS_LOG(ufsvfsp, (caddr_t)bap,
					ldbtob(bp->b_blkno), bp->b_bcount);
			copy = ngeteblk(fs->fs_bsize);
			bcopy((caddr_t)bap, (caddr_t)copy->b_un.b_daddr,
				(u_int)fs->fs_bsize);
			bzero((caddr_t)&bap[last + 1], zb);

			TRANS_BUF(ufsvfsp,
				(caddr_t)&bap[last + 1] - (caddr_t)bap,
				zb, bp, DT_ABZERO);

			bwrite(bp);
			bp = copy, bap = bp->b_un.b_daddr;
		}
	} else {
		bp->b_flags &= ~B_DELWRI;
		bp->b_flags |= B_STALE | B_AGE;
	}

	/*
	 * Recursively free totally unused blocks.
	 */
	flags |= I_CHEAP;
	for (i = NINDIR(fs) - 1; i > last; i--) {
		nb = bap[i];
		if (nb == 0)
			continue;
		if (level > SINGLE) {
			blocksreleased +=
			    indirtrunc(ip, nb, (daddr_t)-1, level - 1, flags);
			free(ip, nb, (off_t)fs->fs_bsize, flags | I_IBLK);
		} else
			free(ip, nb, (off_t)fs->fs_bsize, flags);
		blocksreleased += nblocks;
	}
	flags &= ~I_CHEAP;

	/*
	 * Recursively free last partial block.
	 */
	if (level > SINGLE && lastbn >= 0) {
		last = lastbn % factor;
		nb = bap[i];
		if (nb != 0)
			blocksreleased += indirtrunc(ip, nb, last, level - 1,
				flags);
	}
	brelse(bp);
	return (blocksreleased);
}

/*
 * Truncate the inode ip to at most length size.
 * Free affected disk blocks -- the blocks of the
 * file are removed in reverse order.
 *
 * N.B.: triple indirect blocks are untested.
 */
static int i_genrand = 1234;
int
ufs_itrunc(struct inode *oip, u_long length, int flags, cred_t *cr)
{
	struct fs *fs = oip->i_fs;
	struct ufsvfs *ufsvfsp = oip->i_ufsvfs;
	struct inode *ip;
	daddr_t lastblock;
	off_t bsize;
	int boff;
	daddr_t bn, lastiblock[NIADDR];
	int level;
	long nblocks, blocksreleased = 0;
	int i;
	ushort_t mode;
	struct inode tip;
	int err;

	ASSERT(RW_WRITE_HELD(&oip->i_contents));
	/*
	 * We only allow truncation of regular files and directories
	 * to arbritary lengths here.  In addition, we allow symbolic
	 * links to be truncated only to zero length.  Other inode
	 * types cannot have their length set here.  Disk blocks are
	 * being dealt with - especially device inodes where
	 * ip->i_ordev is actually being stored in ip->i_db[0]!
	 */
	TRANS_INODE(ufsvfsp, oip);
	mode = oip->i_mode & IFMT;
	if (flags & I_FREE) {
		i_genrand *= 16843009;  /* turns into shift and adds */
		i_genrand++;
		oip->i_gen += ((i_genrand + lbolt) & 0xffff) + 1;
		oip->i_flag |= ICHG |IUPD;
		if (length == oip->i_size)
			return (0);
		flags |= I_CHEAP;
	}
	if (mode == IFIFO)
		return (0);
	if (mode != IFREG && mode != IFDIR && !(mode == IFLNK && length == 0) &&
	    mode != IFSHAD)
		return (EINVAL);
	if ((long)length < 0)
		return (EFBIG);
	if (mode == IFDIR)
		flags |= I_DIR;
	if (mode == IFSHAD)
		flags |= I_SHAD;
	if (oip == ufsvfsp->vfs_qinod)
		flags |= I_QUOTA;
	if (length == oip->i_size) {
		/* update ctime and mtime to please POSIX tests */
		oip->i_flag |= ICHG |IUPD;
		return (0);
	}
	/* wipe out fast symlink till next access */
	if (oip->i_flag & IFASTSYMLNK) {
		int j;

		oip->i_flag &= ~IFASTSYMLNK;

		for (j = 1; j < NDADDR && oip->i_db[j]; j++)
			oip->i_db[j] = 0;
		for (j = 0; j < NIADDR && oip->i_ib[j]; j++)
			oip->i_ib[j] = 0;
	}

	boff = blkoff(fs, length);

	if (length > oip->i_size) {
		/*
		 * Trunc up case.  BMAPALLOC will insure that the right blocks
		 * are allocated.  This includes extending the old frag to a
		 * full block (if needed) in addition to doing any work
		 * needed for allocating the last block.
		 */
		if (boff == 0)
			err = BMAPALLOC(oip, length - 1, (int)fs->fs_bsize, cr);
		else
			err = BMAPALLOC(oip, length - 1, boff, cr);

		if (err == 0) {
			/*
			 * Save old size and set inode's size now
			 * so that we don't cause too much of the
			 * file to be zero'd and pushed.
			 */
			u_long osize = oip->i_size;
			oip->i_size  = length;
			/*
			 * Make sure we zero out the remaining bytes of
			 * the page in case a mmap scribbled on it. We
			 * can't prevent a mmap from writing beyond EOF
			 * on the last page of a file.
			 *
			 * i_owner prevents recursive locks in ufs_get/putpage
			 */
			if ((boff = blkoff(fs, osize)) != 0) {
				bsize = lblkno(fs, osize - 1) >= NDADDR ?
				    fs->fs_bsize : fragroundup(fs, boff);
				ASSERT(oip->i_owner == NULL);
				oip->i_owner = curthread;
				pvn_vpzero(ITOV(oip), (u_int)osize,
				    (u_int)(bsize - boff));
				oip->i_owner = NULL;
			}
			oip->i_flag |= ICHG|IATTCHG;
			ITIMES_NOLOCK(oip);
		}

		return (err);
	}

	/*
	 * Update the pages of the file.  If the file is not being
	 * truncated to a block boundary, the contents of the
	 * pages following the end of the file must be zero'ed
	 * in case it ever become accessable again because
	 * of subsequent file growth.
	 */
	if (boff == 0) {
		(void) pvn_vplist_dirty(ITOV(oip), (u_int)length, ufs_putapage,
		    B_INVAL | B_TRUNC, CRED());
	} else {
		/*
		 * Make sure that the last block is properly allocated.
		 * We only really have to do this if the last block is
		 * actually allocated since ufs_bmap will now handle the case
		 * of an fragment which has no block allocated.  Just to
		 * be sure, we do it now independent of current allocation.
		 */
		err = BMAPALLOC(oip, length - 1, boff, cr);
		if (err)
			return (err);
		bsize = lblkno(fs, length - 1) >= NDADDR ?
		    fs->fs_bsize : fragroundup(fs, boff);
		/*
		 * i_owner prevents recursive locks in ufs_get/putpage
		 */
		ASSERT(oip->i_owner == NULL);
		oip->i_owner = curthread;
		pvn_vpzero(ITOV(oip), (u_int)length, (u_int)(bsize - boff));
		oip->i_owner = NULL;

		(void) pvn_vplist_dirty(ITOV(oip), (u_int)length, ufs_putapage,
		    B_INVAL | B_TRUNC, CRED());
	}

	/*
	 * Calculate index into inode's block list of
	 * last direct and indirect blocks (if any)
	 * which we want to keep.  Lastblock is -1 when
	 * the file is truncated to 0.
	 */
	lastblock = lblkno(fs, length + fs->fs_bsize - 1) - 1;
	lastiblock[SINGLE] = lastblock - NDADDR;
	lastiblock[DOUBLE] = lastiblock[SINGLE] - NINDIR(fs);
	lastiblock[TRIPLE] = lastiblock[DOUBLE] - NINDIR(fs) * NINDIR(fs);
	nblocks = btodb(fs->fs_bsize);

	/*
	 * Update file and block pointers
	 * on disk before we start freeing blocks.
	 * If we crash before free'ing blocks below,
	 * the blocks will be returned to the free list.
	 * lastiblock values are also normalized to -1
	 * for calls to indirtrunc below.
	 */
	tip = *oip;			/* structure copy */
	ip = &tip;

	for (level = TRIPLE; level >= SINGLE; level--)
		if (lastiblock[level] < 0) {
			oip->i_ib[level] = 0;
			lastiblock[level] = -1;
		}
	for (i = NDADDR - 1; i > lastblock; i--) {
		oip->i_db[i] = 0;
		flags |= I_CHEAP;
	}
	oip->i_size = length;
	oip->i_flag |= ICHG|IUPD|IATTCHG;
	if (!TRANS_ISTRANS(ufsvfsp))
		ufs_iupdat(oip, I_SYNC);	/* do sync inode update */

	/*
	 * Indirect blocks first.
	 */
	for (level = TRIPLE; level >= SINGLE; level--) {
		bn = ip->i_ib[level];
		if (bn != 0) {
			blocksreleased +=
			    indirtrunc(ip, bn, lastiblock[level], level, flags);
			if (lastiblock[level] < 0) {
				ip->i_ib[level] = 0;
				free(ip, bn, (off_t)fs->fs_bsize,
					flags | I_IBLK);
				blocksreleased += nblocks;
			}
		}
		if (lastiblock[level] >= 0)
			goto done;
	}

	/*
	 * All whole direct blocks or frags.
	 */
	for (i = NDADDR - 1; i > lastblock; i--) {
		bn = ip->i_db[i];
		if (bn == 0)
			continue;
		ip->i_db[i] = 0;
		bsize = (off_t)blksize(fs, ip, i);
		free(ip, bn, bsize, flags);
		blocksreleased += btodb(bsize);
	}
	if (lastblock < 0)
		goto done;

	/*
	 * Finally, look for a change in size of the
	 * last direct block; release any frags.
	 */
	bn = ip->i_db[lastblock];
	if (bn != 0) {
		off_t oldspace, newspace;

		/*
		 * Calculate amount of space we're giving
		 * back as old block size minus new block size.
		 */
		oldspace = blksize(fs, ip, lastblock);
		ip->i_size = length;
		newspace = blksize(fs, ip, lastblock);
		if (newspace == 0) {
			err = ufs_fault(ITOV(ip), "ufs_itrunc: newspace == 0");
			return (err);
		}
		if (oldspace - newspace > 0) {
			/*
			 * Block number of space to be free'd is
			 * the old block # plus the number of frags
			 * required for the storage we're keeping.
			 */
			bn += numfrags(fs, newspace);
			free(ip, bn, oldspace - newspace, flags);
			blocksreleased += btodb(oldspace - newspace);
		}
	}
done:
/* BEGIN PARANOIA */
	for (level = SINGLE; level <= TRIPLE; level++)
		if (ip->i_ib[level] != oip->i_ib[level]) {
			err = ufs_fault(ITOV(ip), "ufs_itrunc: indirect block");
			return (err);
		}

	for (i = 0; i < NDADDR; i++)
		if (ip->i_db[i] != oip->i_db[i]) {
			err = ufs_fault(ITOV(ip), "ufs_itrunc: direct block");
			return (err);
		}
/* END PARANOIA */
	oip->i_blocks -= blocksreleased;

	if (oip->i_blocks < 0) {		/* sanity */
		cmn_err(CE_NOTE,
		    "ufs_itrunc: %s/%d new size = %d, blocks = %d\n",
		    fs->fs_fsmnt, (int)oip->i_number, (int)oip->i_size,
		    (int)oip->i_blocks);
		oip->i_blocks = 0;
	}
	oip->i_flag |= ICHG|IATTCHG;
#ifdef QUOTA
	(void) chkdq(oip, -blocksreleased, 0, cr);
#endif
	return (0);
}

/*
 * Check mode permission on inode.  Mode is READ, WRITE or EXEC.
 * In the case of WRITE, the read-only status of the file system
 * is checked.  The mode is shifted to select the owner/group/other
 * fields.  The super user is granted all permissions except
 * writing to read-only file systems.
 */
int
ufs_iaccess(ip, mode, cr)
	register struct inode *ip;
	register int mode;
	register struct cred *cr;
{
	if (mode & IWRITE) {
		/*
		 * Disallow write attempts on read-only
		 * file systems, unless the file is a block
		 * or character device or a FIFO.
		 */
		if (ip->i_fs->fs_ronly != 0) {
			if ((ip->i_mode & IFMT) != IFCHR &&
			    (ip->i_mode & IFMT) != IFBLK &&
			    (ip->i_mode & IFMT) != IFIFO) {
				return (EROFS);
			}
		}
	}
	/*
	 * If you're the super-user,
	 * you always get access.
	 */
	if (cr->cr_uid == 0)
		return (0);
	/*
	 * it there is a shadow inode check to the presence of an acl,
	 * if the acl is there use the ufs_acl_access routine to check
	 * the acl
	 */
	if (ip->i_ufs_acl && ip->i_ufs_acl->aowner)
		return (suser(cr) ? 0: ufs_acl_access(ip, mode, cr));

	/*
	 * Access check is based on only
	 * one of owner, group, public.
	 * If not owner, then check group.
	 * If not a member of the group, then
	 * check public access.
	 */
	if (cr->cr_uid != ip->i_uid) {
		mode >>= 3;
		if (!groupmember((uid_t)ip->i_gid, cr))
			mode >>= 3;
	}
	if ((ip->i_mode & mode) == mode)
		return (0);
	return (EACCES);
}

/*
 * if necessary, remove an inode from the free list
 *	i_contents is held except at unmount
 */
void
ufs_rmidle(struct inode *ip)
{
	mutex_enter(&ip->i_tlock);
	if ((ip->i_flag & IREF) == 0) {
		mutex_enter(&ufs_idle_q.uq_mutex);
		if (ip == ufs_idle_q.uq_ihead)
			ufs_idle_q.uq_ihead = ip->i_freef;
		if (ip == ufs_idle_q.uq_ihead)
			ufs_idle_q.uq_ihead = NULL;
		ip->i_freef->i_freeb = ip->i_freeb;
		ip->i_freeb->i_freef = ip->i_freef;
		ip->i_freef = ip;
		ip->i_freeb = ip;
		ufs_idle_q.uq_ne--;
		mutex_exit(&ufs_idle_q.uq_mutex);
		VN_RELE(ITOV(ip));
	}
	ip->i_flag |= IREF;
	mutex_exit(&ip->i_tlock);
}

/*
 * scan the hash of inodes and call func with the inode locked
 */
int
ufs_scan_inodes(int rwtry, int (*func)(struct inode *, void *), void *arg)
{
	struct inode		*ip, *lip;
	struct vnode		*vp;
	union ihead		*ih;
	int			error;
	int			saverror	= 0;
	int			i;

	for (i = 0, ih = ihead; i < inohsz; i++, ih++) {
		mutex_enter(&ih_lock[i]);
		for (ip = ih->ih_chain[0], lip = NULL;
			ip != (struct inode *)ih;
			ip = lip->i_forw) {

			ins.in_scan.value.ul++;

			vp = ITOV(ip);
			VN_HOLD(vp);
			mutex_exit(&ih_lock[i]);
			if (lip)
				VN_RELE(ITOV(lip));
			lip = ip;
			/*
			 * Acquire the contents lock to make sure that the
			 * inode has been initialized in the cache.
			 */
			if (rwtry) {
				if (!rw_tryenter(&ip->i_contents, RW_WRITER)) {
					mutex_enter(&ih_lock[i]);
					continue;
				}
			} else
				rw_enter(&ip->i_contents, RW_WRITER);
			rw_exit(&ip->i_contents);

			/*
			 * i_number == 0 means bad initialization; ignore
			 */
			if (ip->i_number)
				if (error = (*func)(ip, arg))
					saverror = error;

			mutex_enter(&ih_lock[i]);
		}
		mutex_exit(&ih_lock[i]);
		if (lip) {
			VN_RELE(ITOV(lip));
		}
	}
	return (saverror);
}

/*
 * Mark inode with the current time
 */
void
ufs_imark(ip)
	struct inode *ip;
{
	/* XXX - This stuff should use uniqtime() */

	if (hrestime.tv_sec > iuniqtime.tv_sec ||
	    hrestime.tv_nsec/1000 > iuniqtime.tv_usec) {
		iuniqtime.tv_sec = hrestime.tv_sec;
		iuniqtime.tv_usec = hrestime.tv_nsec/1000;
	} else {
		iuniqtime.tv_usec++;
		/* Check for usec overflow */
		if (iuniqtime.tv_usec >= MICROSEC) {
			iuniqtime.tv_sec++;
			iuniqtime.tv_usec -= MICROSEC;
		}
	}
	if ((ip)->i_flag & IACC) {
		(ip)->i_atime = iuniqtime;
	}
	if (ip->i_flag & IUPD) {
		ip->i_mtime = iuniqtime;
		ip->i_flag |= IMODTIME;
	}
	if (ip->i_flag & ICHG) {
		ip->i_diroff = 0;
		ip->i_ctime = iuniqtime;
	}
}

/*
 * Update timestamps in inode
 */
void
ufs_itimes_nolock(ip)
	struct inode *ip;
{
	if (ip->i_flag & (IUPD|IACC|ICHG)) {
		if (ip->i_flag & ICHG)
			ip->i_flag |= IMOD;
		else
			ip->i_flag |= IMODACC;

		/* inline ufs_imark begin */

		/* XXX - This stuff should use uniqtime() */

		if (hrestime.tv_sec > iuniqtime.tv_sec ||
		    hrestime.tv_nsec/1000 > iuniqtime.tv_usec) {
			iuniqtime.tv_sec = hrestime.tv_sec;
			iuniqtime.tv_usec = hrestime.tv_nsec/1000;
		} else {
			iuniqtime.tv_usec++;
			/* Check for usec overflow */
			if (iuniqtime.tv_usec >= MICROSEC) {
				iuniqtime.tv_sec++;
				iuniqtime.tv_usec -= MICROSEC;
			}
		}
		if (ip->i_flag & IACC) {
			ip->i_atime = iuniqtime;
		}
		if (ip->i_flag & IUPD) {
			ip->i_mtime = iuniqtime;
			ip->i_flag |= IMODTIME;
		}
		if (ip->i_flag & ICHG) {
			ip->i_diroff = 0;
			ip->i_ctime = iuniqtime;
		}
		/* inline ufs_imark end */

		ip->i_flag &= ~(IACC|IUPD|ICHG);
	}
}
