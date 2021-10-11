/*
 * Copyright (c) 1993-1994 Sun Microsystems, Inc.  All Rights Reserved.
 */

#pragma ident "@(#)dosops.c 1.5	94/08/09 SMI"
/*
 *	mini-MS-DOS filesystem for secondary boot
 */

#include <sys/param.h>
#include <sys/vnode.h>
#include <sys/sysmacros.h>
#include <sys/promif.h>
#include <sys/dirent.h>
#include <sys/stat.h>
#include <sys/fs/pc_label.h>
#include <sys/fs/pc_fs.h>
#include <sys/fs/pc_dir.h>
#include <sys/fs/pc_node.h>
#include <sys/bootvfs.h>
#include <sys/bootdebug.h>
#include "pcfilep.h"

#define	NULL	0

struct dirinfo {
	int	loc;
	fileid_t *fi;
};

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

typedef struct pcfsfile {
	struct pcfsfile *forw;	/* singly linked list */
	fileid_t pcfile;
	struct pcnode *xnodep;	/* just a pointer (could use fi_inode) */
} pcfsfile_t;

/*
 *  Function prototypes
 */
extern void diskette_ivc(void);

static	ino_t	find(struct pcfsfile *, char *);
static	int	disketteread(fileid_t *);

void	pc_invalfat(struct pcfs *);

static int	boot_pcfs_mountroot(char *str);
static int	boot_pcfs_open(char *filename, int flags);
static int	boot_pcfs_close(int fd);
static int	boot_pcfs_read(int fd, caddr_t buf, int size);
static off_t	boot_pcfs_lseek(int, off_t, int);
static int	boot_pcfs_fstat(int fd, struct stat *stp);
static void	boot_pcfs_closeall(int flag);

struct boot_fs_ops boot_pcfs_ops = {
	"pcfs",
	boot_pcfs_mountroot,
	boot_pcfs_open,
	boot_pcfs_close,
	boot_pcfs_read,
	boot_pcfs_lseek,
	boot_pcfs_fstat,
	boot_pcfs_closeall,
};

/*
 *	There is only 1 open (mounted) device at any given time.
 *	So we can keep a single, global devp file descriptor to
 *	use to index into the di[] array.  This is not true for the
 *	fi[] array.  We can have more than one file open at once,
 *	so there is no global fd for the fi[].
 *	The user program must save the fd passed back from open()
 *	and use it to do subsequent read()'s.
 */

devid_t	*fdevp = 0;
int	pcfsdebuglevel = 0;

static	u_int	flop_time = 0;
static	pcfsfile_t *pcfshead = NULL;
static	int	pcfsid = 1;	/* save 0 for special marker */

static	char	pc_dev_type = 0;	/* unnecessary for pc */

extern short	flop_bps;		/* bytes per sector */
extern short	flop_spt;		/* disk sectors per track */
extern short	flop_spc;		/* disk sectors per cylinder */
extern unsigned int	flop_endblk;

/*
 * Given an fd, do a search (in this case, linear linked list search)
 * and return the matching pcfsfile pointer or NULL;
 * By design, when fd is 0, we return a free pcfsfile structure (if any).
 */

static struct pcfsfile *
get_fileptr(int fd)
{
	struct pcfsfile *fileptr = pcfshead;

	while (fileptr != NULL) {
		if (fd == fileptr->pcfile.fi_filedes)
			break;
		fileptr = fileptr->forw;
	}
	return (fileptr);
}


/*
 * Determine whether a character is valid for a pc file system file name.
 */
int
pc_validchar(char c)
{
	register char *cp;
	register int n;
	static char valtab[] = {
		"$#&@!%()-{}<>`_\\^~|' "
	};

	/*
	 * Should be "$#&@!%()-{}`_^~' " ??
	 * From experiment in DOSWindows, *+=|\[];:",<>.?/ are illegal.
	 * See IBM DOS4.0 Tech Ref. B-57.
	 */

	if (c >= 'A' && c <= 'Z')
		return (1);
	if (c >= '0' && c <= '9')
		return (1);
	cp = valtab;
	n = sizeof (valtab);
	while (n--) {
		if (c == *cp++)
			return (1);
	}
	return (0);
}


int
pc_getfat(struct pcfs *fsp)
{
	union {
		struct bootsec	bootsect;
		u_char buffer[PC_SECSIZE];
	} un_bs;
	struct bootsec *bootp = &un_bs.bootsect;
	u_char *fatp;
	u_int	fatsize;
	int	secsize;
	int	error;

	flop_time = prom_gettime();
	bootp = (struct bootsec *) &fdevp->un_fs.dummy[sizeof (struct pcfs)];
	/* read the superblock (BIOS Parameter Block) */
	flop_bps = PC_SECSIZE;
	flop_spt = 2;
	flop_spc = 4;
	flop_endblk =  2 * 8 * 40;
	error = prom_read_diskette(fdevp->di_dcookie, (caddr_t) bootp,
	    (u_int) PC_SECSIZE, 0, pc_dev_type);
	flop_time = prom_gettime();
	if (error != PC_SECSIZE) {
PCFSDEBUG(1) {
printf("dosops: short read of BPB.  0x%x chars read\n", error);
goany();
}
		return (-1);
	}
	if (!(bootp->instr[0] == DOS_ID1 ||
	    (bootp->instr[0] == DOS_ID2a && bootp->instr[2] == DOS_ID2b))) {
		printf("dosops: bad DOS signature ");
		goany();
		return (-2);
	}

	/* get the sector size - may be more than 512 bytes */
	secsize = (int) ltohs(bootp->bps[0]);
	/*
	 * check for bogus sector size
	 *  - fat should be at least 1 sector
	 */
	if (secsize < 512 || (int)ltohs(bootp->fatsec) < 1 || bootp->nfat < 1) {
		printf("dosops: bad BPB values ");
		goany();
		return (-2);
	}

	switch (bootp->mediadesriptor) {
	default:
		printf("dosops: unkown media descriptor ");
		goany();
		return (-2);

	case MD_FIXED:
		/*
		 * do not mount fdisk partition
		 */
		printf("dosops: explicit fdisk partititon ");
		goany();
		return (-2);
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
		fsp->pcfs_spcl = (int)bootp->spcl;
		fsp->pcfs_fatsec = (int)ltohs(bootp->fatsec);
		fsp->pcfs_spt = (int)bootp->spt;
		fsp->pcfs_rdirsec = (int)ltohs(bootp->rdirents[0])
		    * sizeof (struct pcdir) / secsize;
		fsp->pcfs_clsize = fsp->pcfs_spcl * secsize;
		fsp->pcfs_fatstart = (daddr_t) ltohs(bootp->res_sec[0]);
		fsp->pcfs_rdirstart = fsp->pcfs_fatstart +
		    (bootp->nfat * fsp->pcfs_fatsec);
		fsp->pcfs_datastart = fsp->pcfs_rdirstart + fsp->pcfs_rdirsec;
		fsp->pcfs_ncluster = (((int)ltohs(bootp->numsect[0]) ?
		    (int)ltohs(bootp->numsect[0]) : (int)bootp->totalsec) -
		    fsp->pcfs_datastart) / fsp->pcfs_spcl;
		fsp->pcfs_numfat = (int)bootp->nfat;
		break;
	}

	flop_bps = fsp->pcfs_secsize;
	flop_spt = fsp->pcfs_spt;
	flop_spc = ltohs(bootp->nhead) * fsp->pcfs_spt;
	flop_endblk = ltohs(bootp->numsect[0]) ?
	    ltohs(bootp->numsect[0]) : ltohl(bootp->totalsec);
	diskette_ivc();

	/*
	 * Get FAT and check it for validity
	 */
	fatsize = fsp->pcfs_fatsec * fsp->pcfs_secsize;
	fatp = (u_char *) bkmem_alloc(fatsize);

	error = prom_read_diskette(fdevp->di_dcookie, (caddr_t)fatp,
	    (u_int)fatsize, fsp->pcfs_fatstart, pc_dev_type);
	flop_time = prom_gettime();
	if (error != fatsize) {
		printf("dosops: bad read of FAT.  0x%x bytes read\n", error);
		goany();
		return (-2);
	}

	if (fatp[0] != bootp->mediadesriptor ||
	    fatp[1] != 0xFF || fatp[2] != 0xFF) {
		printf("dosops: bad FAT ");
		goany();
		return (-2);
	}
	fsp->pcfs_fatp = fatp;
	return (0);
}

void
pc_invalfat(struct pcfs *fsp)
{
	bkmem_free(fsp->pcfs_fatp, fsp->pcfs_fatsec * fsp->pcfs_secsize);
	fsp->pcfs_fatp = (u_char *)0;
}


int
disketteread(fileid_t *filep)
{
	int err;

	/*
	 * PC needs no seek because all reads at lowest level are
	 * reads of absolute diskette sectors via PC bios
	 */

	err = prom_read_diskette(fdevp->di_dcookie, filep->fi_memp,
	    filep->fi_count, filep->fi_blocknum, pc_dev_type);
	flop_time = prom_gettime();
	if (err != filep->fi_count) {
		printf("dosops: short read.  0x%x chars read\n",
			filep->fi_count);
		goany();
		return (-1);
	}
	return (0);
}

static ino_t
find(struct pcfsfile *fileptr, char *path)
{
	struct pcnode *parentp;
	struct pcnode *pcp;
	register char *q;
	char c;

	if (path == NULL || *path == '\0') {
		printf("dosops: null path\n");
		return (0);
	}

	/* get root node */
	parentp = pc_getnode(&fdevp->un_fs.di_pcfs, (daddr_t)0, 0,
	    (struct pcdir *)0);

	while (*path) {
		while (*path == '/' || *path == '\\')
			path++;
		q = path;
		while (*q != '/' && *q != '\\' && *q != '\0')
			q++;
		c = *q;
		*q = '\0';

		if (pc_dirlook(parentp, path, &pcp) == 0) {
			if (c == '\0')
				break;
			*q = c;
			path = q;
			parentp = pcp;
			continue;
		} else {
			return (0);
		}
	}
	fileptr->xnodep = pcp;
	pcp->pc_vn = (struct vnode *)fileptr->xnodep;
	return (pc_makenodeid(pcp->pc_eblkno, pcp->pc_eoffset, &pcp->pc_entry,
	    fdevp->un_fs.di_pcfs.pcfs_entps));
}


/*
 * Get the next block of data from the file.  If possible, dma right into
 * user's buffer
 */
static int
getblock(struct pcfsfile *fileptr, caddr_t buf, int count, int *rcount)
{
	register caddr_t p;
	register daddr_t lbn;
	struct pcfs	*fsp;
	struct pcnode	*pcp;
	int	off, size, diff;
	int	error;
	extern int read_opt;
	fileid_t	*filep = &fileptr->pcfile;

	pcp = fileptr->xnodep;
	p = filep->fi_memp;

PCFSDEBUG(5)
printf("pc getblock: pcp=%x buf=%x count=%x\n", pcp, buf, count);

	if (filep->fi_count <= 0) {

		/* find the amt left to be read in the file */
		diff = pcp->pc_size - filep->fi_offset;
		if (diff <= 0) {
			printf("pc getblock: Short read\n");
			return (-1);
		}
		fsp = &fdevp->un_fs.di_pcfs;

		/* which block in the file do we read? */
		lbn = pc_lblkno(fsp, filep->fi_offset);

		/* which physical block on the device do we read? */
		error = pc_bmap(pcp, lbn, (daddr_t *)&filep->fi_blocknum, NULL);

		off = pc_blkoff(fsp, filep->fi_offset);

		size = pc_blksize(fsp, pcp, filep->fi_offset);
		filep->fi_count = size;
		filep->fi_memp = filep->fi_buf;
PCFSDEBUG(6) {
printf("pc getblock: size=%x\n", size);
goany();
}
		/*
		 * optimization if we are reading large blocks of data then
		 * we can go directly to user's buffer
		 */
		*rcount = 0;
		if (off == 0 && count >= size) {
			filep->fi_memp = buf;
			if (disketteread(filep)) {
				return (-1);
			}
			*rcount = size;
			filep->fi_count = 0;
			read_opt++;
			return (0);
		} else
			if (disketteread(filep))
				return (-1);

		if (filep->fi_offset - off + size >= pcp->pc_size)
			filep->fi_count = diff + off;
		filep->fi_count -= off;
		p = &filep->fi_memp[off];
	}
	filep->fi_memp = p;
	return (0);
}

static	int	fddev_open = 0;

/*
 *	This routine will open a device as it is known by the
 *	V2 OBP.
 *	Interface Defn:
 *	err = mountroot(string);
 *	err:	0 on success
 *		-1 on failure
 *	string:	char string describing the properties of the device.
 *	We must not dork with any fi[]'s here.  Save that for later.
 */

static int
boot_pcfs_mountroot(char *str)
{
	int	h;
	int	error;

	h = prom_open(str);

#ifdef PCFS_OPS_DEBUG
	if ((boothowto & DBFLAGS) == DBFLAGS)
		printf("pcfs_mountroot(): %s\n", str);
#endif
	if (h == 0) {
		printf("pcfs_mountroot: cannot open %s\n", str);
		return (-1);
	}

	fdevp = (devid_t *)bkmem_alloc(sizeof (devid_t));
	fdevp->di_taken = 1;
	fdevp->di_dcookie = h;
	fdevp->di_desc = (char *) bkmem_alloc(strlen(str) + 1);
	strcpy(fdevp->di_desc, str);
	bzero(fdevp->un_fs.di_pcfs, PC_SECSIZE + sizeof (struct pcfs));

	pc_init();

	if (error = pc_getfat(&fdevp->un_fs.di_pcfs)) {
		prom_close(fdevp->di_dcookie);
		fdevp->di_taken = 0;
		bkmem_free(fdevp->di_desc, strlen(fdevp->di_desc) + 1);
		bkmem_free(fdevp, sizeof (devid_t));
		fdevp = NULL;

		if (error < -1)
			printf("pcfs_mountroot: bogus filesystem.\n");
		return (-1);
	}
	fddev_open = 1;
	return (0);
}

/*
 *	Use the supplied fd allocated by the host filesystem.
 *	filename is the DOS filename from SOLARIS.MAP
 */

/*ARGSUSED*/
static int
boot_pcfs_open(char *filename, int flags)
{	struct	pcfsfile *fileptr;
	ino_t	inode;
	fileid_t	*fp;
	int	new = 0;

	fileptr = get_fileptr(0);
	if (fileptr == NULL) {
		fileptr = (struct pcfsfile *) bkmem_alloc(
			sizeof (struct pcfsfile));
		new++;
	}
	fileptr->xnodep = NULL;
	fp = &fileptr->pcfile;
	fp->fi_offset = 0;
	fp->fi_path = (char *) bkmem_alloc(strlen(filename) + 1);
	strcpy(fp->fi_path, filename);
	fp->fi_devp = fdevp;

	/* No need to re-read SB on every file open */
	if (!fddev_open) {
		if (pc_getfat(&fdevp->un_fs.di_pcfs)) {
			printf("pcfs_open(): bogus filesystem.\n");
			return (-1);
		}

		fddev_open = 1;
	}

	inode = find(fileptr, filename);
	if (inode == (ino_t)0) {
		if (new)
			bkmem_free(fileptr, sizeof (struct pcfsfile));
		return (-1);
	}

	fp->fi_filedes = pcfsid++;
	if (new) {
		fileptr->forw = pcfshead;
		pcfshead = fileptr;
	}

	return (fp->fi_filedes);
}

/*
 * pcfs_fstat() only supports size and mode at present time.
 */

static int
boot_pcfs_fstat(int fd, struct stat *stp)
{
	struct pcfsfile *fileptr;

	fileptr = get_fileptr(fd);
	if (fileptr == NULL) {
		printf("pcfs_fstat(): no such fd %d\n", fd);
#ifdef PCFS_OPS_DEBUG
		goany();
#endif
		return (-1);
	}

	if (fileptr->xnodep->pc_entry.pcd_attr & PCA_DIR)
		stp->st_mode = S_IFDIR;
	else
		stp->st_mode = S_IFREG;
	stp->st_size = fileptr->xnodep->pc_size;
	return (0);
}

/*
 *  We don't do any IO here.
 *  We just play games with the device pointers.
 */

static off_t
boot_pcfs_lseek(int fd, off_t addr, int whence)
{
	struct pcfsfile *fileptr;
	struct file_ident *fp;

	fileptr = get_fileptr(fd);
	if (fileptr == NULL)
		return (-1);
	fp = &fileptr->pcfile;

	switch (whence) {
	case SEEK_CUR:
		fp->fi_offset += addr;
		break;
	case SEEK_SET:
		fp->fi_offset = addr;
		break;
	default:
	case SEEK_END:
		printf("pcfs_lseek(): invalid whence value %d\n", whence);
		break;
	}

	fp->fi_blocknum = pc_lblkno(&fdevp->un_fs.di_pcfs, fp->fi_offset);
	fp->fi_count = 0;

	return (0);
}


/*
 *  This is the high-level read function.  It works like this.
 *  We assume that our IO device buffers up some amount of
 *  data ant that we can get a ptr to it.  Thus we need
 *  to actually call the device func about filesize/blocksize times
 *  and this greatly increases our IO speed.  When we already
 *  have data in the buffer, we just return that data (with bcopy() ).
 */

static int
boot_pcfs_read(int fd, caddr_t buf, int count)
{
	register int i, j;
	struct pcnode	*pcp;
	caddr_t	bufp;
	int rcount;
	struct pcfsfile *fileptr;
	struct file_ident *fp;

	fileptr = get_fileptr(fd);
	if (fileptr == NULL) {
		printf("pcfs_read(): no such fd %d\n", fd);
#ifdef PCFS_OPS_DEBUG
		goany();
#endif
		return (-1);
	}

	pcp = fileptr->xnodep;
	fp = &fileptr->pcfile;
PCFSDEBUG(5)
printf("pcfs_read: pcp=%x addr=%x count=%x\n", pcp, buf, count);
	if (fp->fi_offset + count > pcp->pc_size)
		count = pcp->pc_size - fp->fi_offset;

	/* that was easy */
	if ((i = count) <= 0) {
PCFSDEBUG(5)
printf("pcfs_read: pcp=%x EOF\n");
		return (0);
	}

	bufp = buf;
	while (i > 0) {
		/* If we need to reload the buffer, do so */
		if ((j = fp->fi_count) <= 0) {
			getblock(fileptr, bufp, i, &rcount);
			i -= rcount;
			bufp += rcount;
			fp->fi_offset += rcount;
		} else {
			/* else just bcopy from our buffer */
			j = MIN(i, j);
PCFSDEBUG(6)
printf("pcfs_read: %x from buffer\n", j);
			bcopy(fp->fi_memp, bufp, (unsigned) j);
			bufp += j;
			fp->fi_memp += j;
			fp->fi_offset += j;
			fp->fi_count -= j;
			i -= j;
		}
	}

	return (bufp - buf);
}

static int
boot_pcfs_close(int fd)
{
	struct pcfsfile *fileptr;
	fileid_t *fp;

	fileptr = get_fileptr(fd);
	if (fileptr == NULL) {
		printf("pcfs_close(): No such fd %d.\n", fd);
#ifdef PCFS_OPS_DEBUG
		goany();
#endif
		return (-1);
	}

	if (fileptr->xnodep) {
		pc_rele(fileptr->xnodep);
	}

	fp = &fileptr->pcfile;
	bkmem_free(fp->fi_path, strlen(fp->fi_path)+1);
	fp->fi_path = NULL;
	fp->fi_filedes = 0;	/* don't bother to free, make it re-usable */

	return (0);
}

/*ARGSUSED*/
static void
boot_pcfs_closeall(int flag)
{
	struct pcfs	*fsp = &fdevp->un_fs.di_pcfs;
	int	i;
	struct pcfsfile *fileptr = pcfshead;

	while (fileptr != NULL) {
		bkmem_free(fileptr, sizeof (struct pcfsfile));
		fileptr = fileptr->forw;
	}
	pcfshead = NULL;

	if (fdevp) {
		fddev_open = 0;
		pc_diskchanged(fsp);

		prom_close(fdevp->di_dcookie);
		fdevp->di_taken = 0;

		/* pc_invalfat(fsp); */

		bkmem_free(fdevp->di_desc, strlen(fdevp->di_desc) + 1);
		bkmem_free(fdevp, sizeof (devid_t));
		fdevp = NULL;

#if 0
		flop_time += 3000;
		while (prom_gettime() <= flop_time)
			/* wait for diskette motor to stop */
			;
#else
		for (i = 30; i; i--)
			/* delay for diskette motor to stop */
			wait100ms();
#endif
	}
}
