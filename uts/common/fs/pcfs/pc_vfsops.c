/*
 * Copyright (c) 1990-1995 by Sun Microsystems, Inc.  All rights reserved.
 */

#pragma ident	"@(#)pc_vfsops.c 1.31	95/04/07 SMI"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kmem.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/cred.h>
#include <sys/disp.h>
#include <sys/buf.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/uio.h>
#include <sys/conf.h>
#undef NFSCLIENT
#include <sys/statvfs.h>
#include <sys/mount.h>
#include <sys/pathname.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/sysmacros.h>
#include <sys/conf.h>
#include <sys/mkdev.h>
#include <sys/swap.h>
#include <sys/sunddi.h>
#ifdef i386
#include <sys/dktp/fdisk.h>
#endif	/* i386 */
#include <sys/fs/pc_label.h>
#include <sys/fs/pc_fs.h>
#include <sys/fs/pc_dir.h>
#include <sys/fs/pc_node.h>
#include <fs/fs_subr.h>
#include <sys/modctl.h>
#include <sys/vol.h>
#include <sys/dkio.h>
#include <sys/open.h>

static int pcfs_psuedo_floppy(dev_t);

static int pcfsinit(struct vfssw *, int);
static int pcfs_mount(struct vfs *, struct vnode *, struct mounta *,
	struct cred *);
static int pcfs_unmount(struct vfs *, struct cred *);
static int pcfs_root(struct vfs *, struct vnode **);
static int pcfs_statvfs(struct vfs *, struct statvfs *);
static int pc_syncfsnodes(struct pcfs *);
static int pcfs_sync(struct vfs *, short, struct cred *);
#ifdef notdef
static int pcfs_vget(struct vfs *, struct vnode **, struct fid *);
#endif

int pc_lockfs(struct pcfs *);
void pc_unlockfs(struct pcfs *);
int pc_getfat(struct pcfs *);
int pc_syncfat(struct pcfs *);
void pc_invalfat(struct pcfs *);
void pc_badfs(struct pcfs *);
static int pcfs_badop();

extern struct vfsops pcfs_vfsops;

static struct vfssw vfw = {
	"pcfs",
	pcfsinit,
	&pcfs_vfsops,
	0
};

extern struct mod_ops mod_fsops;

static struct modlfs modlfs = {
	&mod_fsops,
	"filesystem for PC",
	&vfw
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modlfs, NULL
};

/*	Module is unloadable */

static int module_keepcnt = 0;

int
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

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

struct vfsops pcfs_vfsops = {
	pcfs_mount,
	pcfs_unmount,
	pcfs_root,
	pcfs_statvfs,
	pcfs_sync,
	pcfs_badop,	/* vfs_vget */
	fs_nosys,	/* vfs_mountroot */
	fs_nosys	/* vfs_swapvp */
};


int pcfsdebuglevel = 0;

/*
 * pcfslock:	protects the list of mounted pc filesystems "pc_mounttab.
 * pcfs_lock:	(inside per filesystem structure "pcfs")
 *		per filesystem lock. Most of the vfsops and vnodeops are
 *		protected by this lock.
 * pcnodes_lock: protects the pcnode hash table "pcdhead", "pcfhead".
 *
 * Lock hierarchy: pcfslock > pcfs_lock > pcnodes_lock
 */
kmutex_t	pcfslock;
krwlock_t pcnodes_lock; /* protect the pcnode hash table "pcdhead", "pcfhead" */

static int pcfstype;

/*
 * pseudo device numbers for the hard-disk DOS partitions;
 */
int pcfs_major;
int pcfs_minor;


static int
pcfsinit(struct vfssw *vswp, int fstype)
{
	vswp->vsw_vfsops = &pcfs_vfsops;
	pcfstype = fstype;
	(void) pc_init();
	mutex_init(&pcfslock, "PCFS lock", MUTEX_DEFAULT, DEFAULT_WT);
	rw_init(&pcnodes_lock,
	    "pcnode hash table lock", RW_DEFAULT, DEFAULT_WT);
	/*
	 * Assign unique major number for all pcfs hard-disk mounts
	 */
	if ((pcfs_major = getudev()) == -1) {
		cmn_err(CE_WARN, "pcfs: init: can't get unique device number");
		pcfs_major = 0;
	}
	return (0);
}

static struct pcfs *pc_mounttab = NULL;

extern struct pcfs_args pc_tz;

/*
 * pc_mount system call
 */
static int
pcfs_mount(
	register struct vfs *vfsp,
	struct vnode *mvp,
	struct mounta *uap,
	struct cred *cr)
{
	register struct pcfs *fsp;
	struct vnode *bvp;
	struct vnode *devvp;
	struct pathname special;
	daddr_t dosstart;
	dev_t pseudodev;
	dev_t xdev;
	char *spnp;
	char *data = uap->dataptr;
	int datalen = uap->datalen;
	int dos_ldrive = 0;
	int error;
	int fattype;
	int spnlen;
	struct vioc_info info;
	int rval;		/* set but not used */

	module_keepcnt++;

	if (!suser(cr)) {
		module_keepcnt--;
		return (EPERM);
	}

PCFSDEBUG(4)
prom_printf("pcfs_mount\n");
	if (mvp->v_type != VDIR) {
		module_keepcnt--;
		return (ENOTDIR);
	}
	mutex_enter(&mvp->v_lock);
	if ((uap->flags & MS_REMOUNT) == 0 &&
	    (uap->flags & MS_OVERLAY) == 0 &&
	    (mvp->v_count != 1 || (mvp->v_flag & VROOT))) {
		mutex_exit(&mvp->v_lock);
		module_keepcnt--;
		return (EBUSY);
	}
	mutex_exit(&mvp->v_lock);

	if (datalen != sizeof (struct pcfs_args)) {
		module_keepcnt--;
		return (EINVAL);
	} else {
		struct pcfs_args tmp_tz;

		if (copyin(data, (caddr_t)&tmp_tz, sizeof (struct pcfs_args))) {
			module_keepcnt--;
			return (EFAULT);
		}
		/*
		 * more than one pc filesystem can be mounted on x86
		 * so the pc_tz structure is now a critical region
		 */
		mutex_enter(&pcfslock);
		if (pc_mounttab == NULL)
			(void) bcopy((char *)&tmp_tz, (char *)&pc_tz,
			    sizeof (struct pcfs_args));
		mutex_exit(&pcfslock);
	}
	/*
	 * Resolve path name of special file being mounted.
	 */
	if (error = pn_get(uap->spec, UIO_USERSPACE, &special)) {
		module_keepcnt--;
		return (error);
	}
	if (error =
	    lookupname(special.pn_path, UIO_SYSSPACE, FOLLOW, NULLVPP, &bvp)) {
		/*
		 * look for suffix to special
		 * which indicates the DOS logical drive on the hard disk
		 */
		spnlen = special.pn_pathlen;
		spnp = special.pn_path + spnlen - 1;
		if (spnlen > 2 && *spnp >= 'c' && *spnp <= 'z') {
			spnlen--;
			dos_ldrive = *spnp-- - 'c' + 1;
		} else if (spnlen > 2 && *spnp >= '0' && *spnp <= '9') {
			spnlen--;
			dos_ldrive = *spnp-- - '0';
			if (spnlen > 2 && *spnp >= '0' && *spnp <= '9') {
				spnlen--;
				dos_ldrive += 10 * (*spnp-- - '0');
			}
		}
		if (spnlen > 1 && dos_ldrive && dos_ldrive <= 24 &&
		    *spnp == ':') {
			/*
			 * remove suffix so that we have a real device name
			 */
			*spnp = '\0';
			error = lookupname(special.pn_path, UIO_SYSSPACE,
			    FOLLOW, NULLVPP, &bvp);
		}
		if (error) {
			pn_free(&special);
			module_keepcnt--;
			return (error);
		}
	}
	pn_free(&special);
	if (bvp->v_type != VBLK) {
		VN_RELE(bvp);
		module_keepcnt--;
		return (ENOTBLK);
	}
	xdev = bvp->v_rdev;
	VN_RELE(bvp);
	if (getmajor(xdev) >= devcnt) {
		module_keepcnt--;
		return (ENXIO);
	}
	/*
	 * Ensure that this device (or logical drive) isn't already mounted,
	 * unless this is a REMOUNT request
	 */
	if (dos_ldrive) {
		mutex_enter(&pcfslock);
		for (fsp = pc_mounttab; fsp; fsp = fsp->pcfs_nxt)
			if (fsp->pcfs_xdev == xdev &&
			    fsp->pcfs_ldrv == dos_ldrive) {
				mutex_exit(&pcfslock);
				if (uap->flags & MS_REMOUNT) {
					/*
					 * don't increment module_keepcnt
					 * for successful remount
					 */
					module_keepcnt--;
					return (0);
				} else {
					module_keepcnt--;
					return (EBUSY);
				}
			}
		/*
		 * assign a unique pseudo device number for the vfs
		 */
		do {
			pcfs_minor = (pcfs_minor + 1) & MAXMIN;
			pseudodev = makedevice(pcfs_major, pcfs_minor);
		} while (vfs_devsearch(pseudodev));
		mutex_exit(&pcfslock);
	} else {
		if (vfs_devsearch(xdev) != NULL)
			if (uap->flags & MS_REMOUNT) {
				/*
				 * don't increment module_keepcnt
				 * for successful remount
				 */
				module_keepcnt--;
				return (0);
			} else {
				module_keepcnt--;
				return (EBUSY);
			}
		pseudodev = xdev;
	}
	if (uap->flags & MS_RDONLY)
		vfsp->vfs_flag |= VFS_RDONLY;
	/*
	 * Mount the filesystem
	 */
	devvp = makespecvp(xdev, VBLK);
	if (IS_SWAPVP(devvp)) {
		VN_RELE(devvp);
		module_keepcnt--;
		return (EBUSY);
	}

	/*
	 * special handling for PCMCIA memory card
	 * with psuedo floppies organization
	 */
	if (dos_ldrive == 0 && pcfs_psuedo_floppy(xdev)) {
		dosstart = (daddr_t) 0;
		fattype = PCFS_PCMCIA_NO_CIS;
	} else {
#ifdef i386
		if (error = dosgetfattype(xdev, dos_ldrive, &dosstart,
		    &fattype)) {
			VN_RELE(devvp);
			module_keepcnt--;
			return (error);
		}
#else
		/*
		 * sparc hardware currently does not support fdisk format disk
		 */
		if (dos_ldrive == 0) {
			dosstart = (daddr_t) 0;
			fattype = 0;
		} else {
			VN_RELE(devvp);
			module_keepcnt--;
			return (ENOTBLK);
		}

#endif	/* i386 */
	}

	(void) VOP_PUTPAGE(devvp, (offset_t)0, (u_int)0, B_INVAL, cr);
	fsp = (struct pcfs *)kmem_zalloc((u_int)sizeof (struct pcfs), KM_SLEEP);
	fsp->pcfs_vfs = vfsp;
	fsp->pcfs_flags = fattype;
	fsp->pcfs_devvp = devvp;
	fsp->pcfs_xdev = xdev;
	fsp->pcfs_ldrv = dos_ldrive;
	fsp->pcfs_dosstart = dosstart;
	mutex_init(&fsp->pcfs_lock, "pcfs_lock", MUTEX_DEFAULT, NULL);

	/* set the "nocheck" flag if volmgt is managing this volume */
	info.vii_pathlen = 0;
	info.vii_devpath = 0;
	error = cdev_ioctl(fsp->pcfs_xdev, VOLIOCINFO, (int)&info,
	    FKIOCTL|FREAD, kcred, &rval);
	if (error == 0) {
		fsp->pcfs_flags |= PCFS_NOCHK;
	}

	vfsp->vfs_dev = pseudodev;
	vfsp->vfs_fstype = pcfstype;
	vfsp->vfs_fsid.val[0] = (long)pseudodev;
	vfsp->vfs_fsid.val[1] = pcfstype;
	vfsp->vfs_data = (caddr_t)fsp;
	vfsp->vfs_bcount = 0;

	error = pc_verify(fsp);
	if (error) {
		VN_RELE(devvp);
		mutex_destroy(&fsp->pcfs_lock);
		kmem_free((caddr_t)fsp, (u_int) sizeof (struct pcfs));
		module_keepcnt--;
		return (error);
	}
	vfsp->vfs_bsize = fsp->pcfs_clsize;

	mutex_enter(&pcfslock);
	fsp->pcfs_nxt = pc_mounttab;
	pc_mounttab = fsp;
	mutex_exit(&pcfslock);
	return (0);
}

/*
 * vfs operations
 */

/* ARGSUSED */
static int
pcfs_unmount(
	register struct vfs *vfsp,
	struct cred *cr)
{
	register struct pcfs *fsp, *fsp1;
PCFSDEBUG(4)
prom_printf("pcfs_unmount\n");
	fsp = VFSTOPCFS(vfsp);
	/*
	 * We don't have to lock fsp because the VVFSLOCK in vfs layer will
	 * prevent lookuppn from crossing the mount point.
	 */
	if (fsp->pcfs_nrefs) {
		return (EBUSY);
	}

	/* now there should be no pcp node on pcfhead or pcdhead. */

	mutex_enter(&pcfslock);
	if (fsp == pc_mounttab) {
		pc_mounttab = fsp->pcfs_nxt;
	} else {
		for (fsp1 = pc_mounttab; fsp1 != NULL; fsp1 = fsp1->pcfs_nxt)
			if (fsp1->pcfs_nxt == fsp)
				fsp1->pcfs_nxt = fsp->pcfs_nxt;
	}
	mutex_exit(&pcfslock);

	if (fsp->pcfs_fatp != (u_char *)0) {
		pc_invalfat(fsp);
	}
	VN_RELE(fsp->pcfs_devvp);
	mutex_destroy(&fsp->pcfs_lock);
	kmem_free((caddr_t)fsp, (u_int) sizeof (struct pcfs));
	module_keepcnt--;
	return (0);
}

/*
 * find root of pcfs
 */
static int
pcfs_root(
	struct vfs *vfsp,
	struct vnode **vpp)
{
	register struct pcfs *fsp;
	struct pcnode *pcp;

	fsp = VFSTOPCFS(vfsp);
	(void) pc_lockfs(fsp);
	pcp = pc_getnode(fsp, (daddr_t)0, 0, (struct pcdir *)0);
PCFSDEBUG(9)
prom_printf("pcfs_root(0x%x) pcp= 0x%x\n", vfsp, pcp);
	pc_unlockfs(fsp);
	*vpp = PCTOV(pcp);
	pcp->pc_flags |= PC_EXTERNAL;
	return (0);
}

/*
 * Get file system statistics.
 */
static int
pcfs_statvfs(
	register struct vfs *vfsp,
	struct statvfs *sp)
{
	register struct pcfs *fsp;
	int error;

	fsp = VFSTOPCFS(vfsp);
	error = pc_getfat(fsp);
	if (error)
		return (error);
	(void) bzero((caddr_t)sp, (int)sizeof (*sp));
	sp->f_bsize = sp->f_frsize = fsp->pcfs_clsize;
	sp->f_blocks = fsp->pcfs_ncluster;
	sp->f_bavail = sp->f_bfree = pc_freeclusters(fsp);
#ifdef notdef
	sp->f_fsid = (long)fsp->pcfs_devvp->v_rdev;
#endif notdef
	sp->f_fsid = vfsp->vfs_dev;
	strcpy(sp->f_basetype, vfssw[vfsp->vfs_fstype].vsw_name);
	sp->f_flag = vf_to_stf(vfsp->vfs_flag);
	sp->f_namemax = PCFNAMESIZE;
	return (0);
}

static int
pc_syncfsnodes(register struct pcfs *fsp)
{
	register struct pchead * hp;
	register struct pcnode * pcp;
	int error;

PCFSDEBUG(7)
prom_printf("pcfs_syncfsnodes\n");
	(void) pc_lockfs(fsp);
	if (!(error = pc_syncfat(fsp))) {
		hp = pcfhead;
		while (hp < & pcfhead [ NPCHASH ]) {
			rw_enter(&pcnodes_lock, RW_READER);
			pcp = hp->pch_forw;
			while (pcp != (struct pcnode *) hp) {
				if (VFSTOPCFS(PCTOV(pcp) -> v_vfsp) == fsp)
					if (error = pc_nodesync(pcp))
						break;
				pcp = pcp -> pc_forw;
			}
			rw_exit(&pcnodes_lock);
			if (error)
				break;
			hp++;
		}
	}
	pc_unlockfs(fsp);
	return (error);
}

/*
 * Flush any pending I/O.
 */
/*ARGSUSED*/
static int
pcfs_sync(
	register struct vfs *vfsp,
	short flag,
	struct cred *cr)
{
	struct pcfs * fsp;
	int error = 0;

	/* this prevents the filesystem from being umounted. */
	mutex_enter(&pcfslock);
	if (vfsp != NULL) {
		error = pc_syncfsnodes(VFSTOPCFS(vfsp));
	} else {
		fsp = pc_mounttab;
		while (fsp != NULL) {
			error = pc_syncfsnodes(fsp);
			if (error) break;
			fsp = fsp->pcfs_nxt;
		}
	}
	mutex_exit(&pcfslock);
	return (error);
}

int
pc_lockfs(register struct pcfs *fsp)
{
	if ((fsp->pcfs_flags & PCFS_LOCKED) && (fsp->pcfs_owner == curthread)) {
		fsp->pcfs_count++;
	} else {
		mutex_enter(&fsp->pcfs_lock);
		if (fsp->pcfs_flags & PCFS_LOCKED)
			panic("pc_lockfs");
		(void) pc_getfat(fsp);
		fsp->pcfs_flags |= PCFS_LOCKED;
		fsp->pcfs_owner = curthread;
		fsp->pcfs_count++;
	}
	return (0);
}

void
pc_unlockfs(register struct pcfs *fsp)
{

	if ((fsp->pcfs_flags & PCFS_LOCKED) == 0)
		panic("pc_unlockfs");
	if (--fsp->pcfs_count < 0)
		panic("pc_unlockfs: count");
	if (fsp->pcfs_count == 0) {
		fsp->pcfs_flags &= ~PCFS_LOCKED;
		fsp->pcfs_owner = 0;
		mutex_exit(&fsp->pcfs_lock);
	}
}

struct bootsec {
	u_char	instr[3];
	u_char	version[8];
	u_char	bps[2];			/* bytes per sector */
	u_char	spcl;			/* sectors per alloction unit */
	u_char	res_sec[2];		/* reserved sectors, starting at 0 */
	u_char	nfat;			/* number of FATs */
	u_char	rdirents[2];		/* number of root directory entries */
	u_char	numsect[2];		/* old total sectors in logical image */
	u_char	mediadesriptor;		/* media descriptor byte */
	u_short	fatsec;			/* number of sectors per FAT */
	u_short	spt;			/* sectors per track */
	u_short nhead;			/* number of heads */
	u_short hiddensec;		/* number of hidden sectors */
	u_long	totalsec;		/* total sectors in logical image */
};

#ifdef i386
/*
 * ROUTINE: dosgetfattype()
 *
 * Get the FAT type for the DOS medium.
 * DOS floppies always have 12-bit FATs.
 * DOS logical drives on the hard disk have to be located through the
 * FDISK partition table. This routine reads in block 0 of the device,
 * and scans the partition table for the Primary or Extended DOS partition.
 */
static
dosgetfattype(
	dev_t dev,
	int ldrive,
	daddr_t *strtsectp,
	int *fattypep)
{
	struct vnode *devvp;
	struct ipart dosp[FD_NUMPART];	/* incore fdisk partition structure */
	struct mboot *dosp_ptr;		/* boot structure pointer */
	daddr_t diskblk;		/* Disk block to get */
	daddr_t baseblk = 0;		/* base of Extended DOS disk */
	daddr_t partbias;		/* base of Extended DOS partition */
	buf_t *bp = NULL;		/* Disk buffer pointer */
	long xnumsect;
	int i;				/* Loop counter	*/
	int rval = 0;
	u_short signature;		/* DOS signature */
	u_char xsysid, sysid = 0;	/* System ID characters  */

	*strtsectp = (daddr_t)0;
	if (ldrive <= 0) {
PCFSDEBUG(4)
prom_printf("pc_getfattype: floppy has 12_bit FAT\n");
		*fattypep = 0;
		return (0);
	}
	/*
	 *  Device is not a floppy disk.  So assume a hard disk,
	 *  which must have a fdisk partition table in first sector.
	 *  The logical drive to be mounted can be the primary DOS partition
	 *  or in the Extended DOS partition.
	 */
PCFSDEBUG(5)
prom_printf("pc_getfattype: dev=%x  ldrive=%x  ", dev, ldrive);
	devvp = makespecvp(dev, VBLK);
	if (rval = VOP_OPEN(&devvp, FREAD, CRED())) {
PCFSDEBUG(1)
prom_printf("pc_getfattype: open error=%d\n", rval);
		return (rval);
	}
	/*
	 *  Read block 0 from device to get the fdisk table
	 */
	bp = bread(dev, (daddr_t)0, PC_SECSIZE);
	if (bp->b_flags & B_ERROR) {
PCFSDEBUG(1)
prom_printf("pc_getfattype: read error\n");
		rval = EIO;
		goto out;
	}
	/*
	 *  Check for signature at end of boot block for good value.
	 *  If not then error with invalid request.
	 */
	dosp_ptr = (struct mboot *)bp->b_un.b_addr;
	if (ltohs(dosp_ptr->signature) != MBB_MAGIC) {
		cmn_err(CE_NOTE, "!pcfs: DOS signature error");
		rval = EINVAL;
		goto out;
	}
	/*
	 *  Copy from disk block into memory aligned structure for fdisk usage.
	 */
	bcopy((caddr_t)dosp_ptr->parts, (caddr_t)dosp,
	    sizeof (struct ipart)*FD_NUMPART);

	if (ldrive == 1) {
		/*
		 * the first logical drive is C,
		 * which is the Primary DOS partition.
		 */
		for (i = 0; i < FD_NUMPART; i++) {
			if (dosp[i].systid == DOS_SYSFAT12 ||
			    dosp[i].systid == DOS_SYSFAT16 ||
			    dosp[i].systid == DOS_SYSHUGE)
				break;
		}
		if (i == FD_NUMPART) {
			cmn_err(CE_NOTE, "!pcfs: no primary partition");
			rval = EINVAL;
			goto out;
		}
		sysid = dosp[i].systid;
		*strtsectp = ltohl(dosp[i].relsect);
	} else {
		/*
		 * Logical drives D through Z (2 through 24) reside in the
		 * Extended DOS partition.
		 * First find the Extended DOS partition in the master fdisk
		 * partition table and prepare to walk the linked list of
		 * extended DOS partition tables.  Use the relsect value
		 * (which is the offset from the beginning of the disk) for
		 * the next extended fdisk table.  The first entry in the
		 * extended fdisk table is the local partition; the second
		 * entry maybe an entry for another extended DOS partition.
		 * If there is another extended DOS partition the address
		 * is determined from the relsect value plus the relsect
		 * value of the master extended DOS partition.
		 */
		for (i = 0; i < FD_NUMPART; i++) {
			if (dosp[i].systid == EXTDOS)
				break;
		}
		if (i == FD_NUMPART) {
			cmn_err(CE_NOTE, "!pcfs: no extended partition");
			rval = EINVAL;
			goto out;
		}
		diskblk = partbias = ltohl(dosp[i].relsect);
		xsysid = dosp[i].systid;
		xnumsect = ltohl(dosp[i].numsect);
		while (--ldrive && xsysid == EXTDOS) {
			brelse(bp);
			bp = bread(dev, diskblk, PC_SECSIZE);
			if (bp->b_flags & B_ERROR) {
PCFSDEBUG(1)
prom_printf("pc_getfattype: read error\n");
				rval = EIO;
				goto out;
			}
			dosp_ptr = (struct mboot *)bp->b_un.b_addr;
			if (ltohs(dosp_ptr->signature) != MBB_MAGIC) {
				cmn_err(CE_NOTE,
				    "!pcfs: extended partition signature err");
				rval = EINVAL;
				goto out;
			}
			bcopy((caddr_t)dosp_ptr->parts, (caddr_t)dosp,
			    2 * sizeof (struct ipart));
			sysid = dosp[0].systid;
			xsysid = dosp[1].systid;
			baseblk = diskblk;
			diskblk = ltohl(dosp[1].relsect) + partbias;
		}
		*strtsectp = ltohl(dosp[0].relsect) + baseblk;
		if (xnumsect < (*strtsectp - partbias)) {
			cmn_err(CE_NOTE,
			    "!pcfs: extended partition values bad");
			rval = EINVAL;
			goto out;
		}
	}
	/*
	 * Check the sysid value of the logical drive.
	 * Return the correct value for the type of FAT found.
	 * Else return a value of -1 for unknown FAT type.
	 */
	if ((sysid == DOS_SYSFAT16) || (sysid == DOS_SYSHUGE)) {
		*fattypep = PCFS_FAT16 | PCFS_NOCHK;
PCFSDEBUG(4)
prom_printf("pc_getfattype: 16-bit FAT\n");
	} else if (sysid == DOS_SYSFAT12) {
		*fattypep = PCFS_NOCHK;
PCFSDEBUG(4)
prom_printf("pc_getfattype: 12_bit FAT\n");
	} else {
		cmn_err(CE_NOTE, "!pcfs: unknown FAT type");
		rval = EINVAL;
	}

/*
 *   Release the buffer used
 */
out:
	if (bp != NULL)
		brelse(bp);
	(void) VOP_CLOSE(devvp, FREAD, 1, (offset_t)0, CRED());
	return (rval);
} /* dosgetfattype */
#endif	/* i386 */


/*
 * Get the boot parameter block and file allocation table.
 * If there is an old FAT, invalidate it.
 */
int
pc_getfat(register struct pcfs *fsp)
{
	struct vfs *vfsp = PCFSTOVFS(fsp);
	struct buf *tp = 0;
	struct buf *bp = 0;
	struct buf *ap = 0;
	u_char *fatp;
	register u_int *afatp;
	register u_int *xfatp;
	struct bootsec *bootp;
	struct vnode *devvp;
	int count;
	int error;
	int fatsize;
	int flags = 0;
	int nfat;
	int secsize;
	int secno;

PCFSDEBUG(5)
prom_printf("pc_getfat\n");
	devvp = fsp->pcfs_devvp;
	if (fsp->pcfs_fatp) {
		/*
		 * There is a FAT in core.
		 * If there are open file pcnodes or we have modified it or
		 * it hasn't timed out yet use the in core FAT.
		 * Otherwise invalidate it and get a new one
		 */
#ifdef notdef
		if (fsp->pcfs_frefs ||
		    (fsp->pcfs_flags & PCFS_FATMOD) ||
		    (hrestime.tv_sec < fsp->pcfs_fattime)) {
			return (0);
		} else {
			pc_invalfat(fsp);
		}
#endif notdef
		return (0);
	}
	/*
	 * Open block device mounted on.
	 */
	error = VOP_OPEN(&devvp,
	    (vfsp->vfs_flag & VFS_RDONLY) ? FREAD : FREAD|FWRITE,
	    CRED());
	if (error) {
PCFSDEBUG(1)
prom_printf("pc_getfat: open error=%d\n", error);
		return (error);
	}
	/*
	 * Get boot parameter block and check it for validity
	 *
	 * For media with a 1k sector-size, fd_strategy() requires
	 * the I/O size to be a 1k multiple; since the sector-size
	 * is not yet known, always read 1k here.
	 */
	tp = bread(fsp->pcfs_xdev, fsp->pcfs_dosstart, PC_SECSIZE * 2);
	if (tp->b_flags & (B_ERROR | B_STALE)) {
PCFSDEBUG(1)
prom_printf("pc_getfat: boot block error\n");
		flags = tp->b_flags & B_ERROR;
		error = EIO;
		goto out;
	}
	tp->b_flags |= B_STALE | B_AGE;
	bootp = (struct bootsec *)tp->b_un.b_addr;

	if (!(bootp->instr[0] == DOS_ID1 ||
	    (bootp->instr[0] == DOS_ID2a && bootp->instr[2] == DOS_ID2b))) {
		cmn_err(CE_NOTE, "!pcfs: boot block sig error");
		error = EINVAL;
		goto out;
	}

	/* get the sector size - may be more than 512 bytes */
	secsize = (int) ltohs(bootp->bps[0]);
	/* check for bogus sector size - fat should be at least 1 sector */
	if (secsize < 512 || (int)ltohs(bootp->fatsec) < 1 ||
	    bootp->nfat < 1) {
		cmn_err(CE_NOTE, "!pcfs: FAT size error");
		error = EINVAL;
		goto out;
	}

	switch (bootp->mediadesriptor) {
	default:
		cmn_err(CE_NOTE, "!pcfs: media-descriptor error, 0x%x",
		    bootp->mediadesriptor);
		error = EINVAL;
		goto out;

	case MD_FIXED:
		/*
		 * PCMCIA psuedo floppy is type MD_FIXED,
		 * but is accessed like a floppy
		 */
		if (!(fsp->pcfs_flags & PCFS_PCMCIA_NO_CIS)) {
			if (fsp->pcfs_ldrv <= 0) {
				/*
				 * do not mount explicit fdisk partition
				 */
				cmn_err(CE_NOTE,
				    "!pcfs: invalid logical drive");
				error = EINVAL;
				goto out;
			}
			/*
			 * need access to fdisk table to determine FAT type
			 */
			fsp->pcfs_flags |= PCFS_NOCHK;
		}
		/* FALLTHRU */
	case SS8SPT:
	case DS8SPT:
	case SS9SPT:
	case DS9SPT:
	case DS18SPT:
	case DS9_15SPT:
		/*
		 * all floppy media are assumed to have 12-bit FATs
		 * and a boot block at sector 0
		 */
		fsp->pcfs_secsize = secsize;
		fsp->pcfs_sdshift = secsize / DEV_BSIZE - 1;
		fsp->pcfs_entps = secsize / sizeof (struct pcdir);
		fsp->pcfs_spcl = (int) bootp->spcl;
		fsp->pcfs_fatsec = (int) ltohs(bootp->fatsec);
		fsp->pcfs_spt = (int) ltohs(bootp->spt);
		fsp->pcfs_rdirsec = (int) ltohs(bootp->rdirents[0])
		    * sizeof (struct pcdir) / secsize;
		fsp->pcfs_clsize = fsp->pcfs_spcl * secsize;
		fsp->pcfs_fatstart = fsp->pcfs_dosstart +
		    (daddr_t) ltohs(bootp->res_sec[0]);
		fsp->pcfs_rdirstart = fsp->pcfs_fatstart +
		    (bootp->nfat * fsp->pcfs_fatsec);
		fsp->pcfs_datastart = fsp->pcfs_rdirstart + fsp->pcfs_rdirsec;
		fsp->pcfs_ncluster = (((int) (ltohs(bootp->numsect[0]) ?
		    ltohs(bootp->numsect[0]) : ltohl(bootp->totalsec))) -
		    fsp->pcfs_datastart + fsp->pcfs_dosstart) / fsp->pcfs_spcl;
		fsp->pcfs_numfat = (int) bootp->nfat;
		fsp->pcfs_nxfrecls = PCF_FIRSTCLUSTER;
		break;
	}

	/*
	 * Get FAT and check it for validity
	 */
	fatsize = fsp->pcfs_fatsec * fsp->pcfs_secsize;
	bp = bread(fsp->pcfs_xdev,
	    pc_dbdaddr(fsp, fsp->pcfs_fatstart), fatsize);
	if (bp->b_flags & (B_ERROR | B_STALE)) {
PCFSDEBUG(1)
prom_printf("pc_getfat: bread error, fatstart\n");
		flags = bp->b_flags & B_ERROR;
		error = EIO;
		goto out;
	}
	bp->b_flags |= B_STALE | B_AGE;
	fatp = (u_char *)(bp->b_un.b_addr);

	if (fatp[0] != bootp->mediadesriptor ||
	    fatp[1] != 0xFF || fatp[2] != 0xFF) {
		cmn_err(CE_NOTE, "!pcfs: FAT signature error");
		error = EINVAL;
		goto out;
	}
	if (fatp[3] != 0xFF && (fsp->pcfs_flags & PCFS_FAT16))
		if (fsp->pcfs_fatsec <= 12) {
			/*
			 * We have a 12-bit FAT, rather than a 16-bit FAT.
			 * Ignore what the fdisk table says.
			 */
PCFSDEBUG(2)
prom_printf("pc_getfattype: forcing 12_bit FAT\n");
			fsp->pcfs_flags ^= PCFS_FAT16;
		} else {
			cmn_err(CE_NOTE, "!pcfs: FAT signature error");
			error = EINVAL;
			goto out;
		}

	/*
	 * Get alternate FATs and check for consistency
	 */
	for (nfat = 1; nfat < fsp->pcfs_numfat; nfat++) {
		secno = fsp->pcfs_fatstart + nfat*fsp->pcfs_fatsec;
		ap = bread(fsp->pcfs_xdev, pc_dbdaddr(fsp, secno), fatsize);
		if (ap->b_flags & (B_ERROR | B_STALE)) {
			cmn_err(CE_NOTE, "!pcfs: alternate FAT error");
			flags = ap->b_flags & B_ERROR;
			brelse(ap);
			error = EIO;
			goto out;
		}
		ap->b_flags |= B_STALE | B_AGE;
		afatp = (u_int *)(ap->b_un.b_addr);
		for (xfatp = (u_int *)fatp, count = fatsize / sizeof (u_int);
		    count--; /* null */) {
			if (*xfatp++ != *afatp++) {
				cmn_err(CE_NOTE,
				    "!pcfs: alternate FAT corrupted");
				break;
			}
		}
		brelse(ap);
	}
	fsp->pcfs_fatbp = bp;
	fsp->pcfs_fatp = fatp;
	fsp->pcfs_fattime = hrestime.tv_sec + PCFS_DISKTIMEOUT;

	brelse(tp);
	return (0);

out:
	cmn_err(CE_NOTE, "!pcfs: illegal disk format");
	uprintf("pcfs: disk not in DOS format!\n");
	if (tp)
		brelse(tp);
	if (bp)
		brelse(bp);

	if (flags) {
		pc_gldiskchanged(fsp);
	}
	(void) VOP_CLOSE(devvp, (vfsp->vfs_flag & VFS_RDONLY) ?
	    FREAD : FREAD|FWRITE, 1, (offset_t)0, CRED());
	return (error);
}

int
pc_syncfat(register struct pcfs *fsp)
{
	register struct buf *bp;
	register int fatsize;
	int nfat;
	int	error;

PCFSDEBUG(7)
prom_printf("pcfs_syncfat\n");
	if ((fsp->pcfs_fatp == (u_char *)0) || !(fsp->pcfs_flags & PCFS_FATMOD))
		return (0);
	/*
	 * write out all copies of FATs
	 */
	fsp->pcfs_flags &= ~PCFS_FATMOD;
	fsp->pcfs_fattime = hrestime.tv_sec + PCFS_DISKTIMEOUT;
	fatsize = fsp->pcfs_fatbp->b_bcount;
	for (nfat = 0; nfat < fsp->pcfs_numfat; nfat++) {
		bp = ngeteblk(fatsize);
		bp->b_edev = fsp->pcfs_xdev;
		bp->b_dev = cmpdev(bp->b_edev);
		bp->b_blkno = pc_dbdaddr(fsp,
		    fsp->pcfs_fatstart + nfat*fsp->pcfs_fatsec);
		bcopy((caddr_t)fsp->pcfs_fatp, bp->b_un.b_addr, (u_int)fatsize);
		bwrite2(bp);
		error = geterror(bp);
		brelse(bp);
		if (error) {
			pc_gldiskchanged(fsp);
			return (EIO);
		}
	}
PCFSDEBUG(6)
prom_printf("pcfs_syncfat: wrote out FAT\n");
	return (0);
}

void
pc_invalfat(register struct pcfs *fsp)
{
	struct pcfs *xfsp;
	int mount_cnt = 0;
PCFSDEBUG(7)
prom_printf("pc_invalfat\n");
	if (fsp->pcfs_fatp == (u_char *)0)
		panic("pc_invalfat");
	/*
	 * Release FAT
	 */
	brelse(fsp->pcfs_fatbp);
	fsp->pcfs_fatbp = (struct buf *)0;
	fsp->pcfs_fatp = (u_char *)0;
	/*
	 * Invalidate all the blocks associated with the device.
	 * Not needed if stateless.
	 */
	mutex_enter(&pcfslock);
	for (xfsp = pc_mounttab; xfsp; xfsp = xfsp->pcfs_nxt)
		if (xfsp != fsp && xfsp->pcfs_xdev == fsp->pcfs_xdev)
			mount_cnt++;
	mutex_exit(&pcfslock);
	if (!mount_cnt)
		binval(fsp->pcfs_xdev);
	/*
	 * close mounted device
	 */
	(void) VOP_CLOSE(fsp->pcfs_devvp,
	    (PCFSTOVFS(fsp)->vfs_flag & VFS_RDONLY) ? FREAD : FREAD|FWRITE,
	    1, (offset_t)0, CRED());
}

void
pc_badfs(struct pcfs *fsp)
{
	uprintf("corrupted PC file system on dev 0x%x\n",
	    (int)fsp->pcfs_devvp->v_rdev);
}

#ifdef notdef
/*
 * The problem with supporting NFS mount PCFS filesystem is that there
 * is no good place to keep the generation number. The only possible
 * place is inside a directory entry. (There are a few words that are
 * not used.) But directory entries come and go. That is, if a
 * directory is removed completely, its directory blocks are freed
 * and the generation numbers are lost. Whereas in ufs, inode blocks
 * are dedicated for inodes, so the generation numbers are permanently
 * kept on the disk.
 */
static int
pcfs_vget(
	struct vfs *vfsp,
	struct vnode **vpp,
	struct fid *fidp)
{
	struct pcnode *pcp;
	struct pcdir *ep;
	struct pcfid *pcfid;
	struct pcfs *fsp;
	long eblkno;
	int eoffset;
	int flags;
	struct buf *bp;
	int error;

	pcfid = (struct pcfid *)fidp;
	fsp = VFSTOPCFS(vfsp);

	if (pcfid->pcfid_fileno == -1) {
		pcp = pc_getnode(fsp, (daddr_t)0, 0, (struct pcdir *)0);
		pcp -> pc_flags |= PC_EXTERNAL;
		*vpp = PCTOV(pcp);
		return (0);
	}
	if (pcfid->pcfid_fileno < 0) {
		eblkno = pc_daddrdb(fsp, pc_cldaddr(fsp,
		    -pcfid->pcfid_fileno - 1));
		eoffset = 0;
	} else {
		eblkno = pcfid->pcfid_fileno / fsp->pcfs_entps;
		eoffset = (pcfid->pcfid_fileno % fsp->pcfs_entps) *
		    sizeof (struct pcdir);
	}
	error = pc_lockfs(fsp);
	if (error) {
		*vpp = NULL;
		return (error);
	}

	if (eblkno >= fsp->pcfs_datastart || (eblkno-fsp->pcfs_rdirstart)
	    < (fsp->pcfs_rdirsec & ~(fsp->pcfs_spcl - 1))) {
		bp = bread(fsp->pcfs_xdev, eblkno, fsp->pcfs_clsize);
	} else {
		bp = bread(fsp->pcfs_xdev, eblkno,
		    (int) (fsp->pcfs_datastart - eblkno) * fsp->pcfs_secsize);
	}
	if ((bp)->b_flags & (B_ERROR | B_STALE)) {
		flags = bp -> b_flags & B_ERROR;
		brelse(bp);
		if (flags)
			pc_gldiskchanged(fsp);
		*vpp = NULL;
		pc_unlockfs(fsp);
		return (EIO);
	}
	ep = (struct pcdir *)(bp->b_un.b_addr + eoffset);
	if ((ep->pcd_gen == pcfid->pcfid_gen) &&
		pc_validchar(ep->pcd_filename[0])) {
		pcp = pc_getnode(fsp, eblkno, eoffset, ep);
		*vpp = PCTOV(pcp);
		pcp -> pc_flags |= PC_EXTERNAL;
	} else
		*vpp = NULL;
	pc_unlockfs(fsp);
	bp->b_flags |= B_STALE | B_AGE;
	brelse(bp);
	return (0);
}
#endif notdef

#ifdef notdef
/*ARGSUSED*/
static int
pcfs_vget(
	struct vfs *vp,
	struct vnode **vnp,
	struct fid *fp)
{
	panic("pcfs_vget");

	/*
	 * return for lint
	 */
	return (0);
}
#endif

/*ARGSUSED*/
static int
pcfs_badop(
	struct vfs *vp,
	struct vnode **vnp,
	struct fid *fp)
{
	panic("pcfs_badop");

	/* NOTREACHED */

	/*
	 * return for lint
	 */
	return (0);
}

/*
 * if device is a PCMCIA psuedo floppy, return 1
 * otherwise, return 0
 */
static int
pcfs_psuedo_floppy(dev_t rdev)
{
	int rval;	/* ignored */
	int error;
	int err;
	struct dk_cinfo info;

	if ((err = dev_open(&rdev, FREAD, OTYP_CHR, CRED())) != 0) {
PCFSDEBUG(1)
prom_printf("pcfs_psuedo_floppy: dev_open err=%d\n", err);
		return (0);
	}

	error = cdev_ioctl(rdev, DKIOCINFO, (int) &info, FKIOCTL,
	    CRED(), &rval);

	if ((err = dev_close(rdev, FREAD, OTYP_CHR, CRED())) != 0) {
PCFSDEBUG(1)
prom_printf("pcfs_psuedo_floppy: dev_close err=%d\n", err);
		return (0);
	}


	if ((error == 0) && (info.dki_ctype == DKC_PCMCIA_MEM) &&
		(info.dki_flags & DKI_PCMCIA_PFD))
		return (1);
	else
		return (0);
}
