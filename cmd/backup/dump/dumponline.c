/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

#ident	"@(#)dumponline.c 1.0 90/11/09 SMI"

#ident	"@(#)dumponline.c 1.62 94/08/10"

#include "dump.h"
#include <config.h>
#include <grp.h>
#include <pwd.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/filio.h>
#ifdef USG
#include <sys/fs/ufs_filio.h>
#include <sys/fs/ufs_lockfs.h>
#else
#include <sys/filai.h>
#include <sys/lockfs.h>
#endif

struct inodesc {
	ino_t	id_inumber;		/* inode number */
	long	id_gen;			/* generation number */
	struct inodesc *id_next;	/* next on linked list */
};

static struct inodesc	ilist;		/* list of used inodesc structs */
static struct inodesc	*last;		/* last inodesc init'd or matched */
static struct inodesc	*freeinodesc;	/* free list of inodesc structs */
static struct inodesc	**ialloc;	/* allocated chunks, for freeing */
static u_long		nfree;		/* number free on list */
static int		nchunks;	/* number of allocations */

/*
 * If an mmap'ed file is truncated as it is being dumped or
 * faulted in, we are delivered a SIGBUS.
 */
static jmp_buf	truncate_buf;
static void	(*savebus)();
static int	incopy;

#ifdef __STDC__
static char *unrawname(char *);
static void onsigbus(int);
static int makeaddrs(int, off_t, off_t);
#else
static char *unrawname();
static void onsigbus();
static int makeaddrs();

extern char	*strstr();
#endif

#ifdef DEBUG
extern int xflag;
#endif

int	online = 1;			/* assume on-line mode */

/*ARGSUSED*/
static void
onsigbus(sig)
	int	sig;
{
	if (!incopy)
		dumpabort();
	incopy = 0;
	longjmp(truncate_buf, 1);
	/*NOTREACHED*/
}

void
#ifdef __STDC__
allocino(void)
#else
allocino()
#endif
{
	ino_t maxino;
	int nused;

	maxino = sblock->fs_ipg * sblock->fs_ncg;
	nused =  maxino - sblock->fs_cstotal.cs_nifree;
	freeinodesc = (struct inodesc *)
	    calloc((unsigned)nused, (unsigned)sizeof (struct inodesc));
	if (freeinodesc == (struct inodesc *)0) {
		msg(gettext("%s: out of memory\n"), "allocino");
		dumpabort();
	}
	nfree = nused;
	last = &ilist;
	ialloc =
	    /*LINTED [rvalue = malloc() and therefore aligned]*/
	    (struct inodesc **)xmalloc(2*sizeof (struct inodesc *));
	ialloc[0] = freeinodesc;
	ialloc[1] = (struct inodesc *)0;
	nchunks = 1;
}

void
#ifdef __STDC__
freeino(void)
#else
freeino()
#endif
{
	register int i;

	if (ialloc == (struct inodesc **)0)
		return;
	for (i = 0; i < nchunks; i++)
		if (ialloc[i] != 0)
			free(ialloc[i]);
	free(ialloc);
	ialloc = (struct inodesc **)0;
}

#define	INOINC	10

void
saveino(ino, ip)
	ino_t ino;
	struct dinode *ip;
{
	if (doingactive)
		return;

	if (nfree-- <= 0) {
		freeinodesc =
		    (struct inodesc *) calloc(INOINC, sizeof (struct inodesc));
		if (freeinodesc == (struct inodesc *)0) {
			msg(gettext("%s: out of memory\n"), "saveino");
			dumpabort();
		}
		nfree = INOINC-1;
		ialloc = (struct inodesc **)
		    realloc(ialloc, (nchunks+2)*sizeof (struct inodesc *));
		if (ialloc == (struct inodesc **)0) {
			msg(gettext("%s: out of memory\n"), "saveino");
			dumpabort();
		}
		ialloc[nchunks++] = freeinodesc;
		ialloc[nchunks] = (struct inodesc *)0;
	}
	if (ino > last->id_inumber) {
		/*
		 * Add to end of list
		 */
		last->id_next = freeinodesc++;
		last = last->id_next;
		last->id_inumber = ino;
		last->id_gen = ip->di_gen;
		last->id_next = (struct inodesc *)0;
	} else if (ino != last->id_inumber) {
		/*
		 * Fall back to insertion sort
		 */
		register struct inodesc *i;
		for (i = &ilist; i; i = i->id_next) {
			if (ino < i->id_next->id_inumber) {
				freeinodesc->id_next = i->id_next;
				freeinodesc->id_inumber = ino;
				freeinodesc->id_gen = ip->di_gen;
				i->id_next = freeinodesc++;
				break;
			} else if (ino == i->id_next->id_inumber)
				break;
		}
	}
}

void
resetino(ino)
	ino_t	ino;
{
	last = ilist.id_next;
	while (last && last->id_inumber < ino)
		last = last->id_next;
}

long
getigen(ino)
	ino_t	ino;
{
	while (last && last->id_inumber < ino) {
		last = last->id_next;
	}
	if (last->id_inumber == ino)
		return (last->id_gen);
	return (0);
}

static char *
unrawname(cp)
	char *cp;
{
	char *dp;
	extern char *getfullblkname();

	dp = getfullblkname(cp);
	if (dp == 0)
		return (0);
	if (*dp == '\0') {
		free(dp);
		return (0);
	}

	return (dp);
}

/*
 * Determine if specified device is mounted at
 * specified mount point.  Returns 1 if mounted,
 * 0 if not mounted, -1 on error.
 */
int
ismounted(devname, dirname)
	char	*devname;	/* name of device (raw or block) */
	char	*dirname;	/* name of f/s mount point */
{
	struct stat st;
	char	*blockname;	/* name of block device */
	dev_t	dev;

	if ((blockname = unrawname(devname)) == NULL) {
		msg(gettext("Cannot obtain block name from `%s'\n"), devname);
		return (-1);
	}
	if (stat(blockname, &st) < 0) {
		msg(gettext("Cannot obtain status of device `%s': %s\n"),
			blockname, strerror(errno));
		return (-1);
	}
	dev = st.st_rdev;
	if (stat(dirname, &st) < 0) {
		msg(gettext("Cannot obtain status of device `%s': %s\n"),
			dirname, strerror(errno));
		return (-1);
	}
	if (dev == st.st_dev)
		return (1);
	return (0);
}

/*
 * Determine whether on-line dump should be in effect.
 * Return 1 if on-line dump enabled, 0 otherwise.
 */
int
#ifdef __STDC__
checkonline(void)
#else
checkonline()
#endif
{
	struct dinode *ip = getino(UFSROOTINO);
	struct stat st;
	char *blockname;
	int fd;

	/*
	 * check for mounted fs
	 */
	if (!filesystem) {
#ifdef DEBUG

		/* XGETTEXT:  #ifdef DEBUG only */
		msg(gettext("OFFLINE: No filesystem\n"));
#endif
		return (0);
	}
	if (ismounted(disk, filesystem) <= 0) {
#ifdef DEBUG

		/* XGETTEXT:  #ifdef DEBUG only */
		msg(gettext("OFFLINE: Not mounted\n"));
#endif
		return (0);
	}
	/*
	 * sanity check for setuid
	 */
	if (geteuid() != 0) {
#ifdef DEBUG

		/* XGETTEXT:  #ifdef DEBUG only */
		msg(gettext("OFFLINE: Not super-user\n"));
#endif
		return (0);
	}
	/*
	 * check for FCNVT - option to fcntl
	 * that opens files given inode numbers
	 */
	blockname = unrawname(disk);
	if (stat(blockname, &st) < 0) {
		msg(gettext("Cannot obtain status of device `%s'\n"),
			blockname);
		return (0);
	}
	device = st.st_rdev;
	fd = openi(UFSROOTINO, ip->di_gen, filesystem);
	if (fd < 0 && errno != ESTALE) {
#ifdef DEBUG

		/* XGETTEXT:  #ifdef DEBUG only */
		msg(gettext("OFFLINE: No FIOIO support\n"));
#endif
		return (0);
	}
	/*
	 * check for _FIOAI - ioctl call
	 * that looks for holes
	 */
	if (ioctl(fd, _FIOAI, 0) < 0 && errno == ENOTTY) {
#ifdef DEBUG

		/* XGETTEXT:  #ifdef DEBUG only */
		msg(gettext("OFFLINE: No FIOAI support\n"));
#endif
		(void) close(fd);
		return (0);
	}
	/*
	 * check for _FIOSATIME
	 * that resets inode access, mod times
	 */
	if (ioctl(fd, _FIOSATIME, 0) < 0 && errno == ENOTTY) {
#ifdef DEBUG

		/* XGETTEXT:  #ifdef DEBUG only */
		msg(gettext("OFFLINE: No FIOSATIME support\n"));
#endif
		(void) close(fd);
		return (0);
	}
	(void) close(fd);
	return (getfsonline());
}

/*
 * Return 1 if user is allowed access to disk being dumped,
 * 0 if disallowed, -1 on error.
 */
int
isoperator(uid, gid)
	int	uid;
	int	gid;
{
	struct passwd *user = getpwuid(uid);
	struct stat sbuf;
	struct group *gp;
	register char **mem;
	int	ok = 0;

	if (getuid() == 0)	/* super-user? */
		return (1);
	if (stat(disk, &sbuf) < 0) {
		if (errno == EACCES)
			return (0);
		else
			return (-1);
	}
	if (((int)sbuf.st_uid == uid && (S_IRUSR & sbuf.st_mode)) ||
	    ((int)sbuf.st_gid == gid && (S_IRGRP & sbuf.st_mode)) ||
	    (S_IROTH & sbuf.st_mode))
		return (1);
	if (user == (struct passwd *)0)
		return (0);
	(void) setgrent();
	if (S_IRGRP & sbuf.st_mode) {
		if (user->pw_gid != sbuf.st_gid) {
			while (!ok && (gp = getgrent())) {
				if (gp->gr_gid != sbuf.st_gid)
					continue;
				for (mem = gp->gr_mem; *mem; mem++) {
					if (strcmp(*mem, user->pw_name) == 0) {
						ok++;
						break;
					}
				}
			}
		} else
			ok++;
	}
	(void) endgrent();
	if (ok == 0)
		errno = EACCES;
	return (ok);
}

/*
 * okwrite -- determine if a directory  can be modified
 * during the dump (i.e., file system lock mode does not
 * preclude writing.  If not writable and the retry parameter
 * is set, obtain an alternate name from an operator and
 * repeat the check until successful.  Returns the name
 * of a writable directory or NULL.
 *
 */
char *
okwrite(name, retry)
	char	*name;		/* file or directory */
	int	retry;		/* keep trying until successful */
{
	struct stat stat1, stat2;
	char *tmpname = (char *)0;
	char context[3000];

	if (!online || strcmp(getfslocktype(), "all"))
		return (name);

	if (stat(filesystem, &stat1) < 0) {
		msg(gettext("Cannot obtain status of file `%s': %s\n"),
		    filesystem, strerror(errno));
		dumpabort();
	}

	for (tmpname = name; /* EVER */; tmpname = getinput(
	    gettext("Specify an alternate directory: "), context)) {
		(void) sprintf(context, gettext("%s locked"), tmpname);
		if (stat(tmpname, &stat2) < 0) {
			msg(gettext(
			    "Cannot obtain status of directory `%s': %s\n"),
				tmpname, strerror(errno));
			if (!retry)
				return ((char *)0);
			continue;
		}
		if (stat1.st_dev == stat2.st_dev) {
			msg(gettext(
			    "Directory `%s' is on a locked file system (%s)\n"),
				tmpname, filesystem);
			if (!retry)
				return ((char *)0);
		} else
			break;
	}
	if (tmpname != name) {
		name = xmalloc((strlen(tmpname)+1));
		(void) strcpy(name, tmpname);
	}
	return (name);
}

/*
 * lockfs - lock a file system, or get
 * file system lock status.  Returns
 * current lock type.
 */
int
lockfs(fn, type)
	char	*fn;
	char	*type;
{
	time_t		now;
	int		fd, lock;
	struct lockfs	lf;
	char		*timestr, *cp;
	char comment[LOCKFS_MAXCOMMENTLEN];

#ifdef DEBUG
	if (xflag)
		printf("LOCKFS:  *fn = %s, *type = %s\n", (fn ? fn : "NULL"),
			(type ? type : "NULL"));
#endif

	(void) setreuid(-1, 0);

	if ((fd = open(fn, O_RDONLY)) < 0) {
		lockpid = 0;
		msg(gettext("Cannot open `%s': %s\n"), fn, strerror(errno));
		dumpabort();
	}

	(void) bzero((caddr_t)&lf, sizeof (struct lockfs));

	if (ioctl(fd, _FIOLFSS, &lf) == -1) {
		lockpid = 0;
		msg(gettext("Cannot obtain lock status: %s\n"),
			strerror(errno));
		dumpabort();
	}

	if (strcmp(type, "rename") == 0 || strcmp(type, "name") == 0)
		lock = LOCKFS_NLOCK;
	else if (strcmp(type, "delete") == 0)
		lock = LOCKFS_DLOCK;
	else if (strcmp(type, "all") == 0 || strcmp(type, "write") == 0) {
		lock = LOCKFS_WLOCK;
		readonly = 1;
	} else if (strcmp(type, "unlock") == 0 || strcmp(type, "scan") == 0)
		lock = LOCKFS_ULOCK;
	else if (strcmp(type, "status") == 0 || strcmp(type, "none") == 0) {
		(void) close(fd);
		(void) setreuid(-1, getuid());
		return (lf.lf_lock);
	} else {
		msg(gettext("Unknown lock type `%s'\n"), type);
		lockpid = 0;
		dumpabort();
	}

	if (lock != lf.lf_lock) {
		(void) time(&now);
		timestr = prdate(now, 1);
		cp = strchr(timestr, '\n');
		if (cp)
			*cp = '\0';
		(void) sprintf(comment, "hsmdump[%ld]: %s",
		    (long)getpid(), timestr);

		lf.lf_lock	= lock;
		lf.lf_flags	= 0;
		lf.lf_comment	= comment;
		lf.lf_comlen	= strlen(comment)+1;
		if (lockpid)
			lf.lf_key = lf.lf_key;

		if (ioctl(fd, _FIOLFS, &lf) < 0) {
			lockpid = 0;
			if ((errno == EDEADLOCK) || (errno == EDEADLK)) {
msg(gettext("Cannot write-lock a file system with a swap \
file or an accounting file present.\n"));
			} else {
				msg(gettext("Cannot lock file system: %s\n"),
				    strerror(errno));
			}
			dumpabort();
		}
		if (lock != LOCKFS_ULOCK)
			lockpid = getpid();
	}
	(void) close(fd);
	(void) setreuid(-1, getuid());
	return (lock);
}

/*
 * Open a file given its inode number,
 * generation number, and device identifier.
 */
int
openi(inumber, gen, fsname)
	ino_t	inumber;
	long	gen;
	char	*fsname;
{
	struct fioio fio;
	static int fd = -1;

	if (fsname == (char *)0 && fd < 0) {
		msg(gettext(
			"%s: internal error -- no file system!\n"), "openi");
		dumpabort();
	} else if (fsname) {
		if (fd >= 0)
			(void) close(fd);
		fd = open(fsname, O_RDONLY);
		if (fd < 0) {
			msg(gettext("Cannot open file system `%s': %s\n"),
			    fsname, strerror(errno));
			return (-1);
		}
	}
	fio.fio_ino = inumber;
	fio.fio_gen = gen;
	/*
	 * and open the file
	 */
	if (ioctl(fd, _FIOIO, &fio) < 0) {
		if (errno != ESTALE) {
			int save_errno = errno;

			msg(gettext("Cannot open file at inode `%lu': %s\n"),
			    inumber, strerror(errno));
			errno = save_errno;
		}
		return (-1);
	}
	return (fio.fio_fd);
}

#define	MINMAPSIZE	1024*1024
#define	MAXMAPSIZE	1024*1024*32

static caddr_t	mapbase;	/* base of mapped data */
static caddr_t	mapend;		/* end of mapped data */
static off_t	mapsize;	/* amount of mapped data */
/*
 * Map a file prior to dumping and start faulting in its
 * pages.  Stop if we catch a signal indicating our turn
 * to dump has arrived.  If the file is truncated out from
 * under us, immediately return.
 * NB:  the base of the mapped data may not coincide
 * exactly to the requested offset, due to alignment
 * constraints.
 */
caddr_t
mapfile(fd, offset, bytes, fetch)
	int	fd;
	off_t	offset;		/* offset within file */
	off_t	bytes;		/* number of bytes to map */
	int	fetch;		/* start faulting in pages */
{
	/*LINTED [c used during pre-fetch faulting]*/
	register char c, *p;
	int stride = sysconf(_SC_PAGESIZE);
	extern int caught;		/* pre-fetch until set */
	caddr_t	mapstart;		/* beginning of file's mapped data */
	off_t	mapoffset;		/* page-aligned offset */

	mapbase = mapend = (caddr_t) 0;

	if (bytes == 0)
		return ((caddr_t) 0);
	/*
	 * mmap the file for reading
	 */
	mapoffset = offset & ~(stride - 1);
	mapsize = bytes + (offset - mapoffset);
	if (mapsize > MAXMAPSIZE)
		mapsize = MAXMAPSIZE;
	while ((mapbase = mmap((caddr_t)0, mapsize, PROT_READ,
	    MAP_SHARED, fd, mapoffset)) == (caddr_t)-1 &&
	    errno == ENOMEM && mapsize >= MINMAPSIZE) {
		/*
		 * Due to address space limitations, we
		 * may not be able to map as much as we want.
		 */
		mapsize /= 2;	/* exponential back-off */
	}

	if (mapbase == (caddr_t)-1) {
		msg(gettext("Cannot map file at inode `%lu' into memory: %s\n"),
			ino, strerror(errno));
		if (!query(gettext(
		    "Do you want to attempt to continue? (\"yes\" or \"no\") "),
		    gettext("mmap error")))
			dumpabort();
		mapbase = (caddr_t) 0;
		return ((caddr_t) 0);
	}

	(void) madvise(mapbase, mapsize, MADV_SEQUENTIAL);
	mapstart = mapbase + (offset - mapoffset);
	mapend = mapbase + (mapsize - 1);

	if (!fetch)
		return (mapstart);

	savebus = signal(SIGBUS, onsigbus);
	if (setjmp(truncate_buf) == 0) {
		/*
		 * touch each page to pre-fetch by faulting
		 */
		incopy = 1;
		for (p = mapbase; !caught && p <= mapend; p += stride) {
			c = *p;
		}
		incopy = 0;
	}
#ifdef DEBUG
	else
		/* XGETTEXT:  #ifdef DEBUG only */
		msg(gettext(
			"FILE TRUNCATED (fault): Interrupting pre-fetch\n"));
#endif
	(void) signal(SIGBUS, savebus);
	return (mapstart);
}

void
#ifdef __STDC__
unmapfile(void)
#else
unmapfile()
#endif
{
	if (mapbase) {
		(void) msync(mapbase, mapsize, MS_ASYNC|MS_INVALIDATE);
		(void) munmap(mapbase, mapsize);
		mapbase = (caddr_t) 0;
	}
}

/*
 * Fill in an inode structure from
 * data returned by stat().
 */
void
stattoi(sbuf, ip)
	struct stat *sbuf;
	struct dinode *ip;
{
	(void) bzero((char *)ip, sizeof (struct dinode));
	ip->di_mode = sbuf->st_mode;
	ip->di_nlink = sbuf->st_nlink;

#if defined(USG) && defined(di_suid)
	/*
	 * Initialize old [short] uid and gid
	 */
	if (sbuf->st_uid > (uid_t)UID_LONG)
		ip->di_suid = UID_LONG;
	else
		ip->di_suid = (o_uid_t)sbuf->st_uid;
	if (sbuf->st_gid > (gid_t)GID_LONG)
		ip->di_sgid = GID_LONG;
	else
		ip->di_sgid = (o_gid_t)sbuf->st_gid;
#endif
	ip->di_uid = sbuf->st_uid;
	ip->di_gid = sbuf->st_gid;
	if (ip->di_mode & (IFCHR|IFBLK))
		ip->di_ordev = sbuf->st_rdev;
	else
		ip->di_ordev = 0;
	ip->di_size = sbuf->st_size;
	ip->di_atime = sbuf->st_atim.tv_sec;
	ip->di_ic.ic_atspare = sbuf->st_atim.tv_nsec/1000;
	ip->di_mtime = sbuf->st_mtim.tv_sec;
	ip->di_ic.ic_mtspare = sbuf->st_mtim.tv_nsec/1000;
	ip->di_ctime = sbuf->st_ctim.tv_sec;
	ip->di_ic.ic_ctspare = sbuf->st_ctim.tv_nsec/1000;
	ip->di_blocks = sbuf->st_blocks;
}

#define	NDBPTB	(TP_BSIZE / DEV_BSIZE)

/*
 * Make a special record address map, for
 * the specified number of bytes beginning
 * at offset in fd.  Returns the number
 * of initialized map entries.
 */
static int
makeaddrs(fd, offset, bytes)
	int	fd;		/* open file descriptor */
	off_t	offset;		/* current byte offset into file */
	off_t	bytes;		/* number of bytes to map */
{
	register trec, i, n;
	static daddr_t daddr[TP_NINDIR * NDBPTB];
	struct fioai fai;
	long count;

	fai.fai_num   = howmany(bytes, DEV_BSIZE);
	fai.fai_off   = offset;
	fai.fai_size  = bytes;
	fai.fai_daddr = daddr;

	if (ioctl(fd, _FIOAI, (caddr_t) &fai) < 0) {
		char *msgp = gettext(
		    "Cannot get allocation data for file at inode `%lu': %s\n");
		msg(msgp, ino, strerror(errno));
		(void) opermes(LOG_WARNING, msgp, ino, strerror(errno));
		fai.fai_num = 0;
	}
	count = howmany(bytes, TP_BSIZE);
	(void) bzero(spcl.c_addr, count);
	for (trec = 0, n = 0; trec < count && n < fai.fai_num; trec++) {
		for (i = 0; i < NDBPTB && n < fai.fai_num; i++, n++) {
			if (daddr[n] != _FIOAI_HOLE) {
				spcl.c_addr[trec] = 1;
				n += (NDBPTB - i);
				break;
			}
		}
	}
	/*
	 * Restore bombs if we give it a count shorter
	 * than it expects (based on the file's size in
	 * the TS_INODE record) and the amount of data
	 * we supply is not a multiple of the file system
	 * block size.  Give it what it expects.
	 */
	for (; trec < count; trec++)
		spcl.c_addr[trec] = 0;
	return (trec);
}

/*
 * Dump a file in on-line mode.  If this is either
 * the first group of tape records in a large file
 * or the first group of records on a continuation
 * volume, we will be told how many tape records
 * are to be mapped by the first special record
 * (TS_INODE or TS_TAPE).
 */
void
dumpfile(fd, data, offset, size, expected, firstcount, mode)
	int	fd;		/* file descriptor */
	caddr_t	data;		/* mmap'ed data */
	off_t	offset;		/* starting offset within fd, data */
	off_t	size;		/* number of bytes in file */
	off_t	expected;	/* number of bytes expected by restore */
	int	firstcount;	/* number of tape blocks in 1st spclrec */
	int	mode;		/* file's mode (reg, dir, link) */
{
	int	blks, count;
	off_t	bytes;		/* will become number of bytes to dump */
	int	incr;
	caddr_t	dp;
	char buf[TP_BSIZE];
	register int i;

	if (size == 0 && spcl.c_type != TS_TAPE) {
		spcl.c_count = 0;
		spclrec();
		return;
	}
	if (mode == IFREG) {
		dp = data;
		incr = TP_BSIZE;
	} else {
		dp = buf;
		incr = 0;
	}
	/*
	 * If dumping less data than expected by restore,
	 * make sure we dump at least the expected amount
	 * or a multiple of the maximum file system blocksize
	 * whichever is less (restore bombs otherwise).  This
	 * can happen if the file is truncated during a volume
	 * change operation.
	 * NB:  we may need to dump more data ("bytes") than
	 * is actually in the file ("size").
	 */
	if ((expected - size) > MAXBSIZE)
		bytes = roundup(size, MAXBSIZE);
	else
		bytes = expected;
	if (fd < 0 || offset > size ||
	    (offset && (lseek(fd, offset, 0)) < 0)) {
		/*
		 * This can occur if a file is truncated out from
		 * under us.  If this file is being continued from
		 * the previous volume, write out the appropriate
		 * number of zero'ed records, otherwise just write
		 * out the special record.
		 */
		if (spcl.c_type != TS_TAPE) {
			spcl.c_count = 0;
		} else {
			spcl.c_count = firstcount;
			(void) bzero(buf, TP_BSIZE);
		}
		spclrec();
		for (i = 0; i < spcl.c_count; i++)
			if (spcl.c_addr[i])
				taprec(buf, 0);
		msg(gettext(
		    "NOTE: file at inode `%lu' was truncated during backup\n"),
			ino);
		return;		/* abort this file */
	}
	size -= offset;		/* undumped bytes in the file */
	bytes -= offset;	/* bytes to dump (may be more than in file) */
	savebus = signal(SIGBUS, onsigbus);
	count = 0;		/* for lint */
	for (blks = howmany(bytes, TP_BSIZE); blks > 0; blks -= count) {
		if (firstcount) {
			if (spcl.c_type == TS_TAPE)
				count = firstcount;
			else
				count = blks < firstcount ? blks : firstcount;
			firstcount = 0;
		} else
			count = blks < TP_NINDIR ? blks : TP_NINDIR;
		if (spcl.c_type == TS_TAPE)
			/*
			 * On continuation volumes, restore uses
			 * the map from the last special record
			 * written to the previous volume when
			 * restoring the file.  This map is already
			 * contained in spcl.c_addr.
			 */
			spcl.c_count = count;
		else if (mode == IFLNK) {
			for (i = 0; i < count; i++)
				spcl.c_addr[i] = 1;
			spcl.c_count = count;
		} else
			spcl.c_count = count = makeaddrs(fd, offset,
			    (off_t) MIN((count * TP_BSIZE), bytes));
		spclrec();
		if (spcl.c_count == 0)
			break;		/* abort this file */
		i = 0;
		if (setjmp(truncate_buf)) {
			/*
			 * File was truncated.  Fool the program
			 * into copying bytes from a zero'ed buffer
			 * to dump the remaining records and aborting
			 * the file after satisfying this spclrec.
			 */
			msg(gettext(
		"NOTE: file at inode `%lu' was truncated during backup\n"),
				ino);
			(void) bzero(buf, TP_BSIZE);
			dp = buf;	/* point to buffer */
			incr = 0;	/* don't increment buffer pointer */
			blks = count;	/* end after reaching spcl.c_count */
		}
		for (; i < count; i++, dp += incr) {
			if ((dp == (caddr_t)0 || dp > mapend) && incr) {
				/*
				 * End of map segment; mmap the
				 * next segment.
				 */
				unmapfile();
				dp = mapfile(fd, offset, size, 0);
			}
			if (spcl.c_addr[i]) {
				if (mode != IFREG) {
					/*
					 * mmap doesn't work for symlinks
					 * or dirs; use read() instead
					 */
					if (size < TP_BSIZE)	/* security */
						(void) bzero(buf, TP_BSIZE);
					if (read(fd, buf, TP_BSIZE) < 0) {
						msg(gettext(
				"Cannot read file at inode `%lu': %s\n"),
						    ino, strerror(errno));
						dumpailing("file read error");
					}
				}
				incopy = 1;
				taprec(dp, 0);
				incopy = 0;
			}
			size -= TP_BSIZE;
			bytes -= TP_BSIZE;
			offset += TP_BSIZE;
			if (size <= 0 && incr) {
				/*
				 * We have just reached the end of
				 * the mapped data for the file.  As
				 * with truncation, continue the dump
				 * using a zeroed buffer and stop
				 * after satisfying the current spclrec.
				 */
				(void) bzero(buf, TP_BSIZE);
				dp = buf;
				incr = 0;
				blks = count;
			}
		}
		spcl.c_type = TS_ADDR;
	}
	(void) signal(SIGBUS, savebus);
}

void
#ifdef __STDC__
activepass(void)
#else
activepass()
#endif
{
	static int passno = 1;			/* active file pass number */
	char *ext, *old;
	char buf[3000];
	static char defext[] = ".retry";

	if (pipeout) {
		msg(gettext("Cannot re-dump active files to `%s'\n"), tape);
		dumpabort();
	}

	if (active > 1)
		(void) sprintf(buf, gettext(
		    "%d files were active and will be re-dumped\n"), active);
	else
		(void) sprintf(buf, gettext(
		    "1 file was active and will be re-dumped\n"));
	msg(buf);
	(void) opermes(LOG_INFO, buf);

	doingactive++;
	active = 0;
	reset();			/* reset tape params */
	spcl.c_ddate = spcl.c_date;	/* chain with last dump/pass */

	/*
	 * If archiving, create a new
	 * archive file.
	 */
	if (archivefile) {
		old = archivefile;
		ext = strstr(archivefile, defext);
		if (ext == (char *)0)
			archivefile = xmalloc(strlen(old) +
			    strlen(defext) + 5);
		else
			*ext = '\0';
		(void) sprintf(archivefile, "%s%s%d", old, defext, passno);
	}

	if (tapeout) {
		if (isrewind(to)) {
			/*
			 * A "rewind" tape device.  When we do
			 * the close, we will lose our position.
			 * Be nice and switch volumes.
			 */
			(void) sprintf(buf, gettext(
		"Warning - cannot dump active files to rewind device `%s'\n"),
				tape);
			msg(buf);
			(void) opermes(LOG_INFO, buf);
			close_rewind();
			changevol();
		} else {
			trewind();
			doposition = 0;
			filenum++;
		}
	} else {
		/*
		 * Not a tape.  Do a volume switch.
		 * This will advance to the next file
		 * if using a sequence of files, next
		 * diskette if using diskettes, or
		 * let the user move the old file out
		 * of the way.
		 */
		close_rewind();
		changevol();	/* switch files */
	}
	(void) sprintf(buf, gettext(
	    "Dumping active files (retry pass %d) to `%s'\n"), passno, tape);
	msg(buf);
	(void) opermes(LOG_INFO, buf);
	passno++;
}
