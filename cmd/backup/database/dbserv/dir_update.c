#ident	"@(#)dir_update.c 1.9 92/03/25"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include "defs.h"
#include <assert.h>
#include <sys/stat.h>
#include <sys/mman.h>

static u_long dir_maxblk = 0;

static int dir_fd = -1;
static int transfd = -1;

char *dir_dblockmap;
int dir_dblockmapsize;

static struct cache_header *h;
static int dcsize;
static struct dir_block *dircache;

#ifdef __STDC__
static struct dir_entry *addentry(struct dir_block *, const char *, int,
	u_long, u_long);
static void newdir(u_long, u_long, u_long, u_long);
static int dir_newblock(u_long);
static struct dir_block *getfreeblock(u_long *);
static void flush_block(u_long, struct dir_block *);
#else
static struct dir_entry *addentry();
static void newdir();
static int dir_newblock();
static struct dir_block *getfreeblock();
static void flush_block();
#endif

/*
 * keep track of file mappings.  We keep a read mapping and a write mapping.
 * For each, we keep track of the base of the mapping, the first valid
 * record in the mapping, the block number of the first valid block contained
 * in the mapping, and the block number of the last valid block
 * contained in the mapping.
 */
static caddr_t writemap;
static caddr_t readmap;
static struct dir_block *rmapfirst;
static struct dir_block *wmapfirst;
static u_long rmapblk1, rmapblkend;
static u_long wmapblk1, wmapblkend;
static int transsize;

#ifdef __STDC__
dir_open(const char *host)
#else
dir_open(host)
	char *host;
#endif
{
	char dirfile_name[256];
	char dtransfile[256];
	int needblock0 = 0;
	struct stat stbuf;

	rmapfirst = wmapfirst = NULL_DIRBLK;
	rmapblk1 = rmapblkend = wmapblk1 = wmapblkend = transsize = 0;

	(void) sprintf(dirfile_name, "%s/%s", host, DIRFILE);
	dir_fd = open(dirfile_name, O_RDONLY);
	if (dir_fd == -1) {
		needblock0 = 1;
	} else {
		if (fstat(dir_fd, &stbuf) == -1) {
			perror("stat");
			(void) fprintf(stderr,
				gettext("cannot stat `%s'\n"), dirfile_name);
			(void) close(dir_fd);
			dir_fd = -1;
			return (-1);
		}
		if (stbuf.st_size % DIR_BLKSIZE) {
			(void) fprintf(stderr, gettext("%s: bad block size\n"),
				"dir_open");
			(void) close(dir_fd);
			dir_fd = -1;
			return (-1);
		}
		if (stbuf.st_size == 0)
			needblock0 = 1;
		dir_maxblk = stbuf.st_size / DIR_BLKSIZE;
	}
	(void) sprintf(dtransfile, "%s/%s%s", host, DIRFILE, TRANS_SUFFIX);
	if ((transfd = open(dtransfile, O_RDWR|O_CREAT|O_TRUNC, 0600)) == -1) {
		perror("dir_open/trans file open");
		(void) close(dir_fd);
		dir_fd = -1;
		return (-1);
	}
	dcsize = 4096;	/* should determine it dynamically! */
	dircache = (struct dir_block *)malloc(
		(unsigned)(dcsize*sizeof (struct dir_block)));
	if (dircache == NULL_DIRBLK) {
		(void) fprintf(stderr,
			gettext("cannot allocate directory cache\n"));
		(void) close(dir_fd);
		(void) close(transfd);
		dir_fd = transfd = -1;
		return (-1);
	}
	h = cache_init((caddr_t)dircache, dcsize,
			sizeof (struct dir_block), h);
	if (needblock0) {
		u_long instancerec;

		if (dir_newblock(NONEXISTENT_BLOCK) != DIR_ROOTBLK) {
			(void) fprintf(stderr,
			    gettext("%s: cannot create new directory block\n"),
				"dir_open");
			(void) close(dir_fd);
			(void) close(transfd);
			dir_fd = transfd = -1;
			return (-1);
		}
		if (dir_getblock(DIR_ROOTBLK) == NULL_DIRBLK) {
			(void) fprintf(stderr,
			    gettext("%s: cannot get new directory block\n"),
				"dir_open");
			(void) close(dir_fd);
			(void) close(transfd);
			dir_fd = transfd = -1;
			return (-1);
		}
		instancerec = instance_newrec(NONEXISTENT_BLOCK);
		/*
		 * XXX: we add a dummy entry here in order to insure
		 * that the new instance record gets marked dirty and
		 * written out (if it happens that we're not dumping
		 * the root directory in this dump it won't get
		 * updated otherwise...
		 */
		(void) instance_addent(instancerec, (u_long)0, (u_long)0);
#if 1
		if (instancerec != INSTANCE_FIRSTDATA)
			(void) fprintf(stderr,
				gettext("new dir, existing instance?\n"));
#endif
		newdir(DIR_ROOTBLK, DIR_ROOTBLK, instancerec, instancerec);
	}
	return (0);
}

void
#ifdef __STDC__
dir_close(const char *host)
#else
dir_close(host)
	char *host;
#endif
{
	register int i;
	struct cache_block *b;
	char mapfile[256];
	int fd;

#define	min(a, b)	(a < b ? a : b)

	/*
	 * write all modified blocks to transaction file
	 */
	dir_dblockmapsize = min(dir_maxblk, dir_dblockmapsize);
	for (i = 0; i < dir_dblockmapsize; i++) {
		if (dir_dblockmap[i]) {
			if ((b = cache_getblock(h, (u_long)i)) == NULL)
				continue;
			if ((b->flags & CACHE_ENT_DIRTY) == 0)
				continue;
			/*LINTED*/
			flush_block(b->blknum, (struct dir_block *)b->data);
		}
	}
	if (writemap) {
		release_map(writemap, 1);
		writemap = NULL;
	}
	if (readmap) {
		release_map(readmap, 0);
		readmap = NULL;
	}
	if (ftruncate(transfd, (off_t)(dir_maxblk*DIR_BLKSIZE)) == -1) {
		perror("ftruncate");
		(void) fprintf(stderr, gettext(
		    "%s: cannot truncate directory transaction file\n"),
			"dir_close");
		(void) close(transfd);
		transfd = -1;
		exit(1);
	}
	(void) fsync(transfd);
	(void) close(transfd);
	transfd = -1;
	(void) sprintf(mapfile, "%s/%s%s", host, DIRFILE, MAP_SUFFIX);
	if ((fd = open(mapfile, O_RDWR|O_CREAT|O_TRUNC, 0600)) == -1) {
		perror("dir_close/mapfile create");
	}
	if (write(fd, dir_dblockmap, dir_dblockmapsize) != dir_dblockmapsize) {
		perror("dir_close/mapfile write");
	}
	(void) fsync(fd);
	(void) close(fd);
	free((char *)dircache);
	dircache = NULL_DIRBLK;
	if (dir_fd != -1 && close(dir_fd) == -1) {
		perror("dir_close/close");
	}
	dir_fd = -1;
}

dir_newsubdir(blk, ep)
	u_long blk;
	struct dir_entry *ep;
{
	struct dir_entry *pep;	/* parent's "." entry */
	struct dir_block *pbp;	/* block containing above entry */
	u_long newblock, pblock;

	ep->de_directory = newblock = dir_newblock(NONEXISTENT_BLOCK);
	if (newblock == NONEXISTENT_BLOCK) {
		(void) fprintf(stderr,
			gettext("%s: cannot get new block\n"), "newsubdir");
		return (0);
	}

	dir_dirtyblock(blk);

	pblock = blk;
	if ((pep = dir_name_getblock(&pblock, &pbp, ".", 1)) == NULL_DIRENTRY) {
		(void) fprintf(stderr,
			gettext("%s: cannot get '..'\n"), "newsubdir");
		return (0);
	} else {
		newdir(newblock, pblock, ep->de_instances, pep->de_instances);
	}
	return (1);
}

/*
 * look up the given name in the given directory block
 */
struct dir_entry *
#ifdef __STDC__
dir_name_getblock(u_long *blknum,
	struct dir_block **bp,
	const char *name,
	int namelen)
#else
dir_name_getblock(blknum, bp, name, namelen)
	u_long *blknum;
	struct dir_block **bp;
	char *name;
	int namelen;
#endif
{
	register struct dir_entry *ep, *end_of_block;
	register struct dir_block *tbp;
	register u_long startblock, curblock;

	startblock = curblock = *blknum;

	if ((tbp = dir_getblock(curblock)) == NULL_DIRBLK)
		return (NULL_DIRENTRY);

	if (tbp->db_spaceavail == DIRBLOCK_DATASIZE)
		/* empty block? */
		return (NULL_DIRENTRY);
	do {
		/*LINTED*/
		ep = (struct dir_entry *)(tbp)->db_data;
		/*LINTED*/
		end_of_block = DE_END(tbp);
		while (ep != end_of_block) {
			register char *s;
			register char *t;

			if (namelen == ep->de_name_len) {
				/*
				 * inline version of
				 * if (strcmp(ep->de_name, name) == 0)
				 *	return (ep);
				 */
				for (s = ep->de_name, t = (char *)name;
						*s == *t && *s; s++, t++)
					;
				if (*s == *t) {
					if (bp)
						*bp = tbp;
					*blknum = curblock;
					return (ep);
				}
				/* end inline */
			}
			ep = DE_NEXT(ep);
		}
		curblock = tbp->db_next;
		if (curblock != startblock)
			tbp = dir_getblock(curblock);
	} while (curblock != startblock);

	if (bp)
		*bp = tbp;
	*blknum = curblock;
	return (NULL_DIRENTRY);
}

struct dir_entry *
#ifdef __STDC__
dir_addent(u_long *blknum,
	struct dir_block **bp,
	const char *name,
	int namelen,
	u_long	instance)
#else
dir_addent(blknum, bp, name, namelen, instance)
	u_long *blknum;
	struct dir_block **bp;
	char *name;
	int namelen;
	u_long	instance;
#endif
{
	struct dir_entry *ep;
	u_long startblock, thisblock;

	if ((*bp = dir_getblock(*blknum)) == NULL_DIRBLK)
		return (NULL_DIRENTRY);

	/*
	 * XXX: could also try to re-use entries in the middle of the
	 * block which have been invalidated
	 */
	startblock = thisblock = *blknum;
	while ((*bp)->db_spaceavail <
			(DE_NAMELEN(namelen) + sizeof (struct dir_entry))) {
		if ((*bp)->db_next != startblock) {
			thisblock = (*bp)->db_next;
			if ((*bp = dir_getblock(thisblock)) == NULL_DIRBLK)
				return (NULL_DIRENTRY);
		} else {
			(*bp)->db_next = dir_newblock((*bp)->db_next);
			dir_dirtyblock(thisblock);
			thisblock = (*bp)->db_next;
			if ((*bp = dir_getblock(thisblock)) == NULL_DIRBLK)
				return (NULL_DIRENTRY);
		}
	}
	ep = addentry(*bp, name, namelen, instance, NONEXISTENT_BLOCK);
	dir_dirtyblock(thisblock);
	*blknum = thisblock;
	return (ep);
}

dir_add_instance(blk, ep, irec)
	u_long blk;
	struct dir_entry *ep;
	u_long irec;
{
	if (ep->de_instances != NONEXISTENT_BLOCK) {
		(void) fprintf(stderr, gettext("%s: already has instances!\n"),
			"dir_add_instance");
		return (-1);
	}
	ep->de_instances = irec;
	dir_dirtyblock(blk);
	return (0);
}

static struct dir_entry *
#ifdef __STDC__
addentry(struct dir_block *bp,
	const char *name,
	int namelen,
	u_long instance,
	u_long child_block)
#else
addentry(bp, name, namelen, instance, child_block)
	struct dir_block *bp;
	char *name;
	int namelen;
	u_long instance;
	u_long child_block;
#endif
{
	struct dir_entry *ep;

	ep = (struct dir_entry *)
		/*LINTED*/
		&bp->db_data[DIRBLOCK_DATASIZE - bp->db_spaceavail];
	ep->de_name_len = (u_short)namelen;
	ep->de_instances = instance;
	ep->de_directory = child_block;
	(void) strcpy(ep->de_name, name);
	bp->db_spaceavail -= DE_NAMELEN(namelen) + sizeof (struct dir_entry);
	return (ep);
}

static void
newdir(blknum, pblock, instance, pinstance)
	u_long blknum;
	u_long pblock;
	u_long instance, pinstance;
{
	struct dir_block *bp;

	if ((bp = dir_getblock(blknum)) == NULL_DIRBLK) {
		(void) fprintf(stderr,
			gettext("%s: %s error\n"), "newdir", "getblock");
		return;
	}
	if (bp->db_spaceavail != DIRBLOCK_DATASIZE) {
		(void) fprintf(stderr,
			gettext("%s: not a fresh block\n"), "newdir");
		return;
	}
	if (bp->db_next != blknum) {
		(void) fprintf(stderr,
			gettext("%s: first block discrepancy\n"), "newdir");
		return;
	}
	(void) addentry(bp, ".", 1, instance, blknum);
	(void) addentry(bp, "..", 2, pinstance, pblock);
	dir_dirtyblock(blknum);
}

static int
dir_newblock(next)
	u_long next;
{
	u_long curblk;
	struct dir_block *dp;
	struct cache_block *cbp;

	dp = NULL_DIRBLK;
	if (dir_maxblk == 0) {
		/* empty file */
		cbp = cache_alloc_block(h);
		cbp->blknum = DIR_FREEBLK;
		cache_insert(h, cbp);
		cbp->flags = CACHE_ENT_VALID;
		/*LINTED*/
		dp = (struct dir_block *)cbp->data;
		dp->db_spaceavail = 0;
		dp->db_next = DIR_FREEBLK;  /* empty freelist initially */
		dir_dirtyblock(DIR_FREEBLK);
		dir_maxblk++;
		dp = NULL_DIRBLK;
	} else {
		dp = getfreeblock(&curblk);
	}

	if (dp == NULL_DIRBLK) {
		curblk = dir_maxblk;
		cbp = cache_alloc_block(h);
		if (cbp->flags & CACHE_ENT_DIRTY) {
			/*LINTED*/
			flush_block(cbp->blknum, (struct dir_block *)cbp->data);
			cbp->flags &= ~CACHE_ENT_DIRTY;
		}
		cbp->blknum = curblk;
		cache_insert(h, cbp);
		dir_maxblk++;
		/*LINTED*/
		dp = (struct dir_block *)cbp->data;
	}
	dp->db_spaceavail = DIRBLOCK_DATASIZE;
	dp->db_flags = 0;
	if (next == NONEXISTENT_BLOCK)
		next = curblk;
	dp->db_next = next;
	dir_dirtyblock(curblk);
	return (curblk);
}

static struct dir_block *
getfreeblock(num)
	u_long *num;
{
	register struct dir_block *d, *d1;

	if ((d = dir_getblock(DIR_FREEBLK)) == NULL_DIRBLK) {
		(void) fprintf(stderr,
			gettext("cannot get %s\n"), "DIR_FREEBLK");
		return (NULL_DIRBLK);
	}

	if (d->db_next == DIR_FREEBLK)
		/* empty freelist */
		return (NULL_DIRBLK);

	*num = d->db_next;
	if ((d1 = dir_getblock(*num)) == NULL_DIRBLK) {
		(void) fprintf(stderr,
			gettext("corrupt directory freelist!\n"));
		return (NULL_DIRBLK);
	}
	d->db_next = d1->db_next;
	dir_dirtyblock(DIR_FREEBLK);
	return (d1);
}

/*
 * XXX: the `reclaim' utility calls this and dir_dirtyblock
 */
/*
 * retrieve the specified block
 */
struct dir_block *
dir_getblock(blknum)
	u_long blknum;
{
	struct cache_block *cbp;
	struct dir_block *dbp;
	int fd;

	if ((cbp = cache_getblock(h, blknum)) == NULL_CACHE_BLOCK) {
		cbp = cache_alloc_block(h);
		if (cbp->flags & CACHE_ENT_DIRTY) {
			/*LINTED*/
			flush_block(cbp->blknum, (struct dir_block *)cbp->data);
			cbp->flags &= ~CACHE_ENT_DIRTY;
		}
		if (dir_dblockmap && dir_dblockmapsize > blknum &&
				dir_dblockmap[blknum]) {
			/* read from trans file */
			fd = transfd;
			if (lseek(fd, (off_t)(blknum*DIR_BLKSIZE),
						SEEK_SET) == -1) {
				perror("dir_getblock/lseek");
				return (NULL_DIRBLK);
			}
			if (read(fd, cbp->data, DIR_BLKSIZE) != DIR_BLKSIZE) {
				perror("dir_getblock/read");
				return (NULL_DIRBLK);
			}
		} else {
			/* read from original file */
			fd = dir_fd;

			dbp = (struct dir_block *)getmapblock(&readmap,
			    (char **)&rmapfirst, &rmapblk1,
			    &rmapblkend,
			    blknum, DIR_BLKSIZE,
			    /*LINTED*/
			    fd, PROT_READ, 0, (int *)0);
				/*LINTED*/
				*((struct dir_block *)cbp->data) = *dbp;
		}
		cbp->blknum = blknum;
		cache_insert(h, cbp);
	}
	/*LINTED*/
	return ((struct dir_block *)cbp->data);
}

void
dir_dirtyblock(num)
	u_long num;
{
	int newsize;
	char *p;

	cache_dirtyblock(h, num);
	if (dir_dblockmap == NULL || num >= dir_dblockmapsize) {
		if (dir_dblockmap) {
			newsize = num + 1000;
			p = (char *)realloc(dir_dblockmap, (unsigned)newsize);
			if (p == NULL) {
				(void) fprintf(stderr, gettext(
					"cannot extend block map\n"));
				exit(1);
			}
			dir_dblockmap = p;
			(void) bzero(&p[dir_dblockmapsize],
					newsize - dir_dblockmapsize);
			dir_dblockmapsize = newsize;
		} else {
			newsize = dir_dblockmapsize = num+1000;
			p = (char *)malloc((unsigned)newsize);
			if (p == NULL) {
				(void) fprintf(stderr, gettext(
					"cannot allocate block map\n"));
				exit(1);
			}
			dir_dblockmap = p;
			(void) bzero(dir_dblockmap, newsize);
		}
	}
	dir_dblockmap[num] = 1;
}

static void
flush_block(num, dbp)
	u_long num;
	struct dir_block *dbp;
{
	struct dir_block *p;

	p = (struct dir_block *)getmapblock(&writemap, (char **)&wmapfirst,
			&wmapblk1, &wmapblkend, num, DIR_BLKSIZE, transfd,
			/*LINTED*/
			PROT_READ|PROT_WRITE, 1, &transsize);
	*p = *dbp;
}

#ifdef notdef
/*
 * routines below here are called by the compress utility - not by update.
 */
void
free_dirblk(blk)
	u_long blk;
{
	struct dir_block *bp1, *bp2;
	u_long next;

	if ((bp1 = dir_getblock(blk)) == NULL_DIRBLK) {
		(void) fprintf(stderr, gettext("%s: cannot locate blk %d\n"),
			"free_dirblk", blk);
		exit(1);
	}

#ifdef lint
	bp2 = bp1;
#endif

	if (bp1->db_next != blk) {
		/*
		 * a chain of blocks - remove this one.
		 */
		for (next = bp1->db_next;
		    /*CONSTCOND*/ TRUE;
		    next = bp2->db_next) {
			bp2 = dir_getblock(next);
			if (bp2 == NULL_DIRBLK) {
				(void) fprintf(stderr,
				    gettext("%s: cannot follow chain\n"),
					"free_dirblk");
				exit(1);
			}
			if (bp2->db_next == blk) {
				bp2->db_next = bp1->db_next;
				dir_dirtyblock(next);
				break;
			}
		}
	}

	/*
	 * put this block on the freelist
	 */
	if ((bp2 = dir_getblock(DIR_FREEBLK)) == NULL_DIRBLK) {
		(void) fprintf(stderr, gettext(
			"%s: cannot read freelist block\n"), "free_dirblk");
		exit(1);
	}
	bp1->db_next = bp2->db_next;
	bp2->db_next = blk;
	dir_dirtyblock(blk);
	dir_dirtyblock(DIR_FREEBLK);
}
#endif
