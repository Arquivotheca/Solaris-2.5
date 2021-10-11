/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 */

#pragma	ident	"@(#)@(#)hsfsops.c	1.8	1.8	94/09/20	SMI"

#include <sys/param.h>
#include <sys/vnode.h>
#include <sys/fs/ufs_fsdir.h>
#include <sys/fs/ufs_fs.h>
#include <sys/fs/ufs_inode.h>
#include <sys/sysmacros.h>
#include <sys/promif.h>
#include <sys/filep.h>

#include <sys/fs/hsfs_spec.h>
#include <sys/fs/hsfs_isospec.h>
#include <sys/fs/hsfs_node.h>
#include <sys/fs/hsfs_susp.h>
#include <sys/fs/hsfs_rrip.h>

#include "hsfs_sig.h"

#include <sys/stat.h>
#include <sys/bootvfs.h>
#include <sys/bootdebug.h>

#define	hdbtodb(n)	((ISO_SECTOR_SIZE / DEV_BSIZE) * (n))

#define	NULL	0

/* May not need this... */
static u_int	sua_offset = 0;

/* The root inode on an HSFS filesystem can be anywhere! */
static u_int	root_ino = 0;		/* This is both a flag and a value */

fileid_t *head;

/* Only got one of these...ergo, only 1 fs open at once */
devid_t		*devp;

struct dirinfo {
	int 	loc;
	fileid_t *fi;
};

struct hs_direct {
    struct	direct	hs_ufs_dir;
    struct	hs_direntry hs_dir;
};

/*
 *  Function prototypes
 */

static int	boot_hsfs_mountroot(char *str);
static int	boot_hsfs_open(char *filename, int flags);
static int	boot_hsfs_close(int fd);
static int	boot_hsfs_read(int fd, caddr_t buf, int size);
static off_t	boot_hsfs_lseek(int, off_t, int);
/* static int	boot_hsfs_fstat(int fd, struct stat *stp); */
static void	boot_hsfs_closeall(int flag);

struct boot_fs_ops boot_hsfs_ops = {
	"hsfs",
	boot_hsfs_mountroot,
	boot_hsfs_open,
	boot_hsfs_close,
	boot_hsfs_read,
	boot_hsfs_lseek,
	boot_no_ops,
	boot_hsfs_closeall,
};

static 	ino_t	find(fileid_t *, char *);
static	ino_t	dlook(fileid_t *, char *);
static	int	opendir(fileid_t *, ino_t);
static	struct	hs_direct *readdir(struct dirinfo *);
static	u_int	parse_dir(fileid_t *, int, struct hs_direct *);
static	u_int	parse_susp(char *, u_int *, struct hs_direct *);
static	void	hs_seti(fileid_t *,  struct hs_direct *, ino_t);

extern	int	diskread(fileid_t *);
extern	struct inode *get_icache(ino_t);
extern	void	set_icache(struct inode *, ino_t);
extern	int	set_dbcache(fileid_t *);

/*
 *	There is only 1 open (mounted) device at any given time.
 *	So we can keep a single, global devp file descriptor to
 *	use to index into the di[] array.  This is not true for the
 *	fi[] array.  We can have more than one file open at once,
 *	so there is no global fd for the fi[].
 *	The user program must save the fd passed back from open()
 *	and use it to do subsequent read()'s.
 */

static int
opendir(fileid_t *filep, ino_t inode)
{
	struct hs_direct hsdep;
	u_int i;
	int retval;

	/* Set up the saio request */
	filep->fi_offset = 0;
	filep->fi_blocknum = hdbtodb(inode);
	filep->fi_count = ISO_SECTOR_SIZE;

	/* Maybe the block is in the disk block cache */
	if ((filep->fi_memp =
	    get_db_cache(filep->fi_blocknum, filep->fi_count)) == NULL) {
		/* Not in the block cache so read it from disk */
		if (retval = set_dbcache(filep)) {
			return (retval);
		}
	}

	filep->fi_offset = 0;
	filep->fi_blocknum = hdbtodb(inode);

	if (inode != root_ino)
	    return (0);

	if (parse_dir(filep, 0, &hsdep) > 0) {
		hs_seti(filep, &hsdep, inode);
		return (0);
	}
	return (1);
}

static ino_t
find(fileid_t *filep, char *path)
{
	register char *q;
	char c;
	ino_t inode;

	if (path == NULL || *path == '\0') {
		printf("null path\n");
		return (0);
	}

	if ((boothowto & RB_DEBUG) && (boothowto & RB_VERBOSE))
	    printf("find(): path=<%s>\n", path);

	/* Read the ROOT directory */
	if (opendir(filep, root_ino)) {
		printf("find(): root_ino opendir() failed!\n");
		return ((ino_t) -1);
	}

	while (*path) {
		while (*path == '/')
			path++;
		q = path;
		while (*q != '/' && *q != '\0')
			q++;
		c = *q;
		*q = '\0';

		if ((inode = dlook(filep, path)) != 0) {
			if (c == '\0')
				break;
			if (opendir(filep, inode)) {
				printf("find(): opendir(%d) failed!\n", inode);
				return ((ino_t) -1);
			}
			*q = c;
			path = q;
			continue;
		} else {
			return (0);
		}
	}
	return (inode);
}

static ino_t
dlook(fileid_t *filep, char *path)
{
	register struct hs_direct *hsdep;
	register struct direct *udp;
	register struct inode *ip;
	struct dirinfo dirp;
	register int len;
	ino_t in;

	ip = filep->fi_inode;
	if (path == NULL || *path == '\0')
		return (0);
	if ((ip->i_smode & IFMT) != IFDIR) {
		return (0);
	}
	if (ip->i_size == 0) {
		return (0);
	}
	len = strlen(path);
	/* first look through the directory entry cache */
	if ((in = get_dcache(path, len, ip->i_number)) != 0) {
		if ((filep->fi_inode = get_icache(in)) != NULL) {
			filep->fi_offset = 0;
			filep->fi_blocknum = hdbtodb(in);
			return (in);
		}
	}
	dirp.loc = 0;
	dirp.fi = filep;
	for (hsdep = readdir(&dirp); hsdep != NULL; hsdep = readdir(&dirp)) {
		udp = &hsdep->hs_ufs_dir;
		if (udp->d_namlen == 1 &&
		    udp->d_name[0] == '.' &&
		    udp->d_name[1] == '\0')
			continue;
		if (udp->d_namlen == 2 &&
		    udp->d_name[0] == '.' &&
		    udp->d_name[1] == '.' &&
		    udp->d_name[2] == '\0')
			continue;
		if (udp->d_namlen == len && !strcmp(path, udp->d_name)) {
			set_dcache(path, len, ip->i_number, udp->d_ino);
			hs_seti(filep, hsdep, udp->d_ino);
			filep->fi_offset = 0;
			filep->fi_blocknum = hdbtodb(udp->d_ino);
			/* put this entry into the cache */
			return (udp->d_ino);
		}
		/* Allow "*" to print all names at that level, w/out match */
		if (!strcmp(path, "*"))
			printf("%s\n", udp->d_name);
	}
	return (0);
}

/*
 * get next entry in a directory.
 */
struct hs_direct *
readdir(struct dirinfo *dirp)
{
	static struct hs_direct hsdep;
	register struct direct *udp = &hsdep.hs_ufs_dir;
	register struct inode *ip;
	register fileid_t *filep;
	register daddr_t lbn, d;
	register int off;

	filep = dirp->fi;
	ip = filep->fi_inode;
	for (;;) {
		if (dirp->loc >= ip->i_size) {
			return (NULL);
		}
		off = dirp->loc & ((1 << ISO_SECTOR_SHIFT) - 1);
		if (off == 0) {
			lbn = hdbtodb(dirp->loc >> ISO_SECTOR_SHIFT);
			filep->fi_blocknum = lbn + hdbtodb(ip->i_number);
			filep->fi_count = ISO_SECTOR_SIZE;
			/* check the block cache */
			if ((filep->fi_memp = get_db_cache(filep->fi_blocknum,
			    filep->fi_count)) == 0)
				if (set_dbcache(filep))
					return ((struct hs_direct *)-1);
		}
		dirp->loc += parse_dir(filep, off, &hsdep);
		if (udp->d_reclen == 0 && dirp->loc <= ip->i_size) {
			dirp->loc = roundup(dirp->loc, ISO_SECTOR_SIZE);
			continue;
		}
		return (&hsdep);
	}
}

/*
 * Get the next block of data from the file.  If possible, dma right into
 * user's buffer
 */
static int
getblock(fileid_t *filep, caddr_t buf, int count, int *rcount)
{
	register struct inode *ip;
	register caddr_t p;
	register int c, off, size, diff;
	register daddr_t lbn;
	devid_t	*devp;
	static int	pos;
	static char 	ind[] = "|/-\\";	/* that's entertainment? */
	static int	blks_read;
	extern int read_opt;

	devp = filep->fi_devp;
	ip = filep->fi_inode;
	p = filep->fi_memp;
	if (filep->fi_count <= 0) {

		/* find the amt left to be read in the file */
		diff = ip->i_size - filep->fi_offset;
		if (diff <= 0) {
			printf("Short read\n");
			return (-1);
		}

		/* which block (or frag) in the file do we read? */
		lbn = hdbtodb(filep->fi_offset >> ISO_SECTOR_SHIFT);

		/* which physical block on the device do we read? */
		filep->fi_blocknum = lbn + hdbtodb(ip->i_number);

		off = filep->fi_offset & ((1 << ISO_SECTOR_SHIFT) - 1);

		size = sizeof (filep->fi_buf);
		filep->fi_count = size;
		filep->fi_memp = filep->fi_buf;

		/*
		 * optimization if we are reading large blocks of data then
		 * we can go directly to user's buffer
		 */
		*rcount = 0;
		if (off == 0 && count >= size) {
			filep->fi_memp = buf;
			if (diskread(filep)) {
				return (-1);
			}
			*rcount = size;
			filep->fi_count = 0;
			read_opt++;
			if ((blks_read++ & 0x3) == 0)
				printf("%c\b", ind[pos++ & 3]);
			return (0);
		} else
			if (diskread(filep))
				return (-1);

		/*
		 * round and round she goes (though not on every block..
		 * - OBP's take a fair bit of time to actually print stuff)
		 */
		if ((blks_read++ & 0x3) == 0)
			printf("%c\b", ind[pos++ & 3]);

		if (filep->fi_offset - off + size >= ip->i_size)
			filep->fi_count = diff + off;
		filep->fi_count -= off;
		p = &filep->fi_memp[off];
	}
	filep->fi_memp = p;
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
boot_hsfs_read(int fd, caddr_t buf, int count)
{
	register int i, j;
	register struct inode *ip;
	caddr_t	n;
	fileid_t	*filep = head;
	int rcount;

	while ((filep = filep->fi_forw) != head)
		if (fd == filep->fi_filedes)
			break;

	if (filep == head) {
		printf("No file descriptor.\n");
		return (-1);
	}

	if (!filep->fi_taken) {
		printf("Must open() file first.\n");
		return (-1);
	}

	ip = filep->fi_inode;

	if (filep->fi_offset + count > ip->i_size)
		count = ip->i_size - filep->fi_offset;

	/* that was easy */
	if ((i = count) <= 0)
		return (0);

	n = buf;
	while (i > 0) {
		/* If we need to reload the buffer, do so */
		if ((j = filep->fi_count) <= 0) {
			getblock(filep, buf, i, &rcount);
			i -= rcount;
			buf += rcount;
			filep->fi_offset += rcount;
		} else {
			/* else just bcopy from our buffer */
			j = MIN(i, j);
			bcopy(filep->fi_memp, buf, (unsigned)j);
			buf += j;
			filep->fi_memp += j;
			filep->fi_offset += j;
			filep->fi_count -= j;
			i -= j;
		}
	}
	return (buf - n);
}

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
boot_hsfs_mountroot(char *str)
{
	ihandle_t	h;
	int 	i = 0;

	if ((boothowto & RB_DEBUG) && (boothowto & RB_VERBOSE))
	    printf("mountroot()\n");

	h = prom_open(str);

	if (h == 0) {
		printf("Cannot open %s\n", str);
		return (-1);
	}

	devp = (devid_t *)bkmem_alloc(sizeof (devid_t));
	devp->di_taken = 1;
	devp->di_dcookie = h;
	devp->di_desc = (char *)bkmem_alloc(strlen(str) + 1);
	strcpy(devp->di_desc, str);
	bzero(devp->un_fs.dummy, sizeof (devp->un_fs.dummy));
	head = (fileid_t *)bkmem_alloc(sizeof (fileid_t));
	head->fi_back = head->fi_forw = head;
	head->fi_filedes = 0;
	head->fi_taken = 0;

	return (0);
}

/*
 *	We allocate an fd here for use when talking
 *	to the file itself.
 */

static int
boot_hsfs_open(char *filename, int flags)
{
	fileid_t	*filep;
	ino_t	inode;
	int	i = 0;
	static int	filedes = 1;
	struct hs_volume *fsp;
	char *bufp;

	/* build and link a new file descriptor */
	filep = (fileid_t *)bkmem_alloc(sizeof (fileid_t));
	filep->fi_back = head->fi_back;
	filep->fi_forw = head;
	head->fi_back->fi_forw = filep;
	head->fi_back = filep;

	filep->fi_filedes = filedes++;
	filep->fi_taken = 1;
	filep->fi_path = (char *)bkmem_alloc(strlen(filename) + 1);
	strcpy(filep->fi_path, filename);
	filep->fi_devp = devp; /* dev is already "mounted" */

	filep->fi_inode->i_dev = 0;

	if (root_ino == 0) {			/* First time through */
	    bzero(filep->fi_buf, sizeof (filep->fi_buf));
	    /* setup read of the "superblock" */
	    filep->fi_blocknum = hdbtodb(ISO_VOLDESC_SEC);
	    filep->fi_count = ISO_SECTOR_SIZE;
	    filep->fi_memp = filep->fi_buf;
	    filep->fi_offset = 0;

	    if (diskread(filep)) {
		    printf("open(): read super block failed!\n");
		    close(filep->fi_filedes);
		    return (-1);
	    }

	    bufp = filep->fi_buf;

	    fsp = (struct hs_volume *) devp->un_fs.dummy;

	    /* Since RRIP is based on ISO9660, that's where we start */

	    if (ISO_DESC_TYPE(bufp) != ISO_VD_PVD ||
		strncmp(ISO_std_id(bufp), ISO_ID_STRING, ISO_ID_STRLEN) != 0 ||
		ISO_STD_VER(bufp) != ISO_ID_VER) {
		    printf("Not an HSFS filesystem.\n");
		    return (-1);
	    }

	    /* Now we fill in the volume descriptor */
	    fsp->vol_size = ISO_VOL_SIZE(bufp);
	    fsp->lbn_size = ISO_BLK_SIZE(bufp);
	    fsp->lbn_shift = ISO_SECTOR_SHIFT;
	    fsp->lbn_secshift = ISO_SECTOR_SHIFT;
	    fsp->vol_set_size = (u_short) ISO_SET_SIZE(bufp);
	    fsp->vol_set_seq = (u_short) ISO_SET_SEQ(bufp);

	    /* Make sure we have a valid logical block size */
	    if (fsp->lbn_size & ~(1 << fsp->lbn_shift)) {
		    printf("%d byte logical block size invalid.\n",
			    fsp->lbn_size);
		    return (-1);
	    }

	    /* Since an HSFS root could be located anywhere on the media! */
	    root_ino = IDE_EXT_LBN(ISO_root_dir(bufp));

	    if ((boothowto & RB_DEBUG) && (boothowto & RB_VERBOSE)) {
		    printf("root_ino=%d\n", root_ino);
		    printf("ID=");
		    for (i = 0; i < ISO_ID_STRLEN; i++)
			printf("%c", *(ISO_std_id(bufp)+i));
		    printf(" VS=%d\n", fsp->vol_size);
	    }
	}

	inode = find(filep, filename);
	if (inode == (ino_t)0) {
		if ((boothowto & RB_DEBUG) && (boothowto & RB_VERBOSE))
		    printf("open(%s) ENOENT\n", filename, filep->fi_filedes);
		close(filep->fi_filedes);
		return (-1);
	}

	filep->fi_blocknum = hdbtodb(inode);
	filep->fi_offset = filep->fi_count = 0;

	if ((boothowto & RB_DEBUG) && (boothowto & RB_VERBOSE))
	    printf("open(%s) fd=%d\n", filename, filep->fi_filedes);
	return (filep->fi_filedes);
}

/*
 *  We don't do any IO here.
 *  We just play games with the device pointers.
 */

/*ARGSUSED*/
static off_t
boot_hsfs_lseek(int filefd, off_t addr, int whence)
{
	fileid_t	*filep = head;

	while ((filep = filep->fi_forw) != head)
		if (filefd == filep->fi_filedes)
			break;

/* Make sure user knows what file he is talking to */
	if (filep == head || !filep->fi_taken)
		return (-1);

	filep->fi_offset = addr;
	filep->fi_blocknum = addr / DEV_BSIZE;
	filep->fi_count = 0;

	return (0);
}

static int
boot_hsfs_close(int fd)
{
	fileid_t	*filep = head;

	if ((boothowto & RB_DEBUG) && (boothowto & RB_VERBOSE))
	    printf("close(%d)\n", fd);

	while ((filep = filep->fi_forw) != head)
		if (fd == filep->fi_filedes)
			break;

/* Make sure user knows what file he is talking to */
	if (filep == head || !filep->fi_taken)
		return (-1);

	if (filep->fi_taken && (filep != head)) {

		/* Clear the ranks */
		bkmem_free(filep->fi_path, strlen(filep->fi_path)+1);
		filep->fi_blocknum = filep->fi_count = filep->fi_offset = 0;
		filep->fi_memp = (caddr_t)0;
		filep->fi_devp = 0;
		filep->fi_taken = 0;

		/* unlink and deallocate node */
		filep->fi_forw->fi_back = filep->fi_back;
		filep->fi_back->fi_forw = filep->fi_forw;
		bkmem_free(filep, sizeof (fileid_t));

		return (0);
	} else {
		/* Big problem */
		printf("\nFile descrip %d not allocated!", fd);
		return (-1);
	}
}

/*ARGSUSED*/
static void
boot_hsfs_closeall(int flag)
{
	int i;
	fileid_t	*filep = head;
	extern int verbosemode;

	while ((filep = filep->fi_forw) != head)
		if (filep->fi_taken)
			if (close(filep->fi_filedes))
				prom_panic("Filesystem may be inconsistent.\n");
	prom_close(devp->di_dcookie);
	devp->di_taken = 0;
	release_cache();
	if (verbosemode)
		print_cache_data();
}

static u_int
parse_dir(fileid_t *filep, int offset, struct hs_direct *hsdep)
{
	char *bufp = (char *)(filep->fi_memp + offset);
	register struct hs_volume *fsp =
		(struct hs_volume *) &filep->fi_devp->un_fs.dummy;
	register struct direct *udp = &hsdep->hs_ufs_dir;
	register struct hs_direntry *hdp = &hsdep->hs_dir;
	u_int ce_lbn;
	u_int ce_len;
	u_int nmlen;
	u_int i;
	u_char c;
	int ret_code;

	if (!(udp->d_reclen = IDE_DIR_LEN(bufp)))
		return (0);

	hdp->ext_lbn  = IDE_EXT_LBN(bufp);
	hdp->ext_size = IDE_EXT_SIZE(bufp);
	hdp->xar_len  = IDE_XAR_LEN(bufp);
	hdp->intlf_sz = IDE_INTRLV_SIZE(bufp);
	hdp->intlf_sk = IDE_INTRLV_SKIP(bufp);
	hdp->sym_link = NULL;

	udp->d_ino = hdp->ext_lbn;

	c = IDE_FLAGS(bufp);
	if (IDE_REGULAR_FILE(c)) {
		hdp->type = VREG;
		hdp->mode = IFREG;
		hdp->nlink = 1;
	} else if (IDE_REGULAR_DIR(c)) {
		hdp->type = VDIR;
		hdp->mode = IFDIR;
		hdp->nlink = 2;
	} else {
		printf("parse_dir(): file type=0x%x unknown.\n", c);
		return (-1);
	}

	/* Some initial conditions */
	nmlen = IDE_NAME_LEN(bufp);
	c = *IDE_NAME(bufp);
	/* Special Case: Current Directory */
	if (nmlen == 1 && c == '\0') {
		udp->d_name[0] = '.';
		udp->d_name[1] = '\0';
		udp->d_namlen = 1;
	/* Special Case: Parent Directory */
	} else if (nmlen == 1 && c == '\001') {
		udp->d_name[0] = '.';
		udp->d_name[1] = '.';
		udp->d_name[2] = '\0';
		udp->d_namlen = 2;
	/* Other file name */
	} else {
		udp->d_namlen = 0;
		for (i = 0; i < nmlen; i++) {
			c = *(IDE_name(bufp)+i);
			if (c == ';')
				break;
			else if (c == ' ')
				continue;
			else
				udp->d_name[udp->d_namlen++] = c;
		}
		udp->d_name[udp->d_namlen] = '\0';
	}
	/* System Use Fields */
	ce_len = IDE_SUA_LEN(bufp);
	ce_lbn = 0;
	if (ce_len > 0) {
	ce_lbn = parse_susp((char *)IDE_sys_use_area(bufp), &ce_len, hsdep);
	while (ce_lbn) {
	    daddr_t save_blocknum = filep->fi_blocknum;
	    daddr_t save_offset = filep->fi_offset;
	    caddr_t save_memp = filep->fi_memp;
	    u_int save_count = filep->fi_count;

#ifdef noisy
	    print_io_req(filep, "parse_dir(): [I]");
#endif	/* noisy */

	    filep->fi_blocknum = hdbtodb(ce_lbn);
	    filep->fi_offset = 0;
	    filep->fi_count = ISO_SECTOR_SIZE;

#ifdef noisy
	    print_io_req(filep, "parse_dir(): [0]");
#endif	/* noisy */

	    if ((filep->fi_memp = get_db_cache(filep->fi_blocknum,
		filep->fi_count)) == 0)
		    ret_code = set_dbcache(filep);

#ifdef noisy
	    print_io_req(filep, "parse_dir(): [1]");
#endif	/* noisy */

	    if (ret_code) {
		    filep->fi_blocknum = save_blocknum;
		    filep->fi_offset = save_offset;
		    filep->fi_memp = save_memp;
		    filep->fi_count = save_count;
		    printf("parse_dir(): set_dbcache() failed (%d)\n",
			    ret_code);
		    break;
	    }
	    ce_lbn = parse_susp(filep->fi_memp, &ce_len, hsdep);

	    filep->fi_blocknum = save_blocknum;
	    filep->fi_offset = save_offset;
	    filep->fi_memp = save_memp;
	    filep->fi_count = save_count;

#ifdef noisy
	    print_io_req(filep, "parse_dir(): [2]");
#endif	/* noisy */
	}
	}

	return (udp->d_reclen);
}

static u_int
parse_susp(char *bufp, u_int *ce_len, struct hs_direct *hsdep)
{
	register struct direct *udp = &hsdep->hs_ufs_dir;
	u_char *susp;
	u_int cur_off = 0;
	u_int blk_len = *ce_len;
	u_int susp_len = 0;
	u_int ce_lbn = 0;
	u_int i;

	while (cur_off < blk_len) {
	    susp = (u_char *)(bufp + cur_off);
	    if (susp[0] == '\0' || susp[1] == '\0')
		break;
	    susp_len = SUF_LEN(susp);
	    if (susp_len == 0)
		break;
	    for (i = 0; i < hsfs_num_sig; i++) {
		if (!strncmp(hsfs_sig_tab[i], susp, SUF_SIG_LEN)) {
#ifdef noisy
		    if ((boothowto & RB_DEBUG) && (boothowto & RB_VERBOSE))
			printf("  SUSP_%c%c %d\n", susp[0], susp[1], susp_len);
#endif	/* noisy */
		    switch (i) {
			case SUSP_SP_IX:
			    if (CHECK_BYTES_OK(susp)) {
				sua_offset = SP_SUA_OFFSET(susp);
			    }
			    break;

			case SUSP_CE_IX:
			    ce_lbn = CE_BLK_LOC(susp);
			    *ce_len = CE_CONT_LEN(susp);
#ifdef noisy
			    if ((boothowto & RB_DEBUG) &&
				(boothowto & RB_VERBOSE))
				printf("parse_susp(): CE: ce_lbn = %d ce_len=%d\n",
				    ce_lbn, *ce_len);
#endif	/* noisy */
			    break;

			case SUSP_ST_IX:
			    printf("parse_susp(): ST: returning %d\n", ce_lbn);
			    return (ce_lbn);

			case RRIP_SL_IX:
#ifdef noisy
			    if ((boothowto & RB_DEBUG) &&
				(boothowto & RB_VERBOSE))
				printf("parse_susp(): ******* SL *******\n");
#endif	/* noisy */
			    break;

			case RRIP_RR_IX:
			    break;

			case RRIP_NM_IX:
			    if (!RRIP_NAME_FLAGS(susp)) {
				udp->d_namlen = RRIP_NAME_LEN(susp);
				bcopy((char *)RRIP_name(susp),
					(char *) udp->d_name,
					udp->d_namlen);
				udp->d_name[udp->d_namlen] = '\0';
			    }
			    break;
		    }
		    cur_off += susp_len;
		    break;
		}
	    }
	    if (i > hsfs_num_sig) {
		printf("parse_susp(): Bad SUSP\n");
		cur_off = blk_len;
		break;
	    }
	}
	return (ce_lbn);
}

static void
hs_seti(fileid_t *filep, struct hs_direct *hsdep, ino_t inode)
{
	register struct inode *ip;

	/* Try the inode cache first */
	if ((filep->fi_inode = get_icache(inode)) != NULL)
		return;

	filep->fi_inode = (struct inode *) bkmem_alloc(sizeof (struct inode));
	ip = filep->fi_inode;
	bzero(ip, sizeof (struct inode));
	ip->i_size = hsdep->hs_dir.ext_size;
	ip->i_smode = hsdep->hs_dir.mode;
	ip->i_number = inode;
	set_icache(ip, inode);
}

static void
print_io_req(fileid_t *filep, char *str)
{
	printf("%s o=%d b=%d c=%d m=%x\n",
	str,
	filep->fi_offset,
	filep->fi_blocknum,
	filep->fi_count,
	(u_int)filep->fi_memp);
}
