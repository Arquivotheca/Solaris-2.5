/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

#pragma ident	"@(#)dumptraverse.c	1.46	94/11/28 SMI"

#include "dump.h"
#include <sys/file.h>

extern	offset_t llseek();

#ifdef __STDC__
static void dmpindir(daddr_t, int, long *);
static void indir(daddr_t, int, long *);
static void blksout(daddr_t *, long);
static void dsrch(daddr_t, long, long);
static void dumpinode(struct dinode *);
#else
static void dmpindir();
static void indir();
static void blksout();
static void dsrch();
#endif

static	char msgbuf[256];

void
pass(fn, map)
	register void (*fn)(struct dinode *);
	register char *map;
{
	register int bits;
	ino_t maxino;

	maxino = sblock->fs_ipg * sblock->fs_ncg - 1;
	/*
	 * handle pass restarts
	 */
	if (ino != 0) {
		bits = ~0;
		if (map != NULL) {
			map += (ino / NBBY);
			bits = *map++;
		}
		bits >>= (ino % NBBY);
		resetino(ino);
		goto restart;
	}
	while (ino < maxino) {
		if ((ino % NBBY) == 0) {
			bits = ~0;
			if (map != NULL)
				bits = *map++;
		}
restart:
		ino++;
		if (bits & 1) {
			/*
			 * The following test is merely an optimization
			 * for common case where "add" will just return.
			 */
			if (!(fn == add && BIT(ino, nodmap)))
				(*fn)(getino(ino));
		}
		bits >>= 1;
	}
}

void
mark(ip)
	struct dinode *ip;
{
	register int f;

	f = ip->di_mode & IFMT;
	if (f == 0 || ip->di_nlink <= 0) {
		BIC(ino, clrmap);
		return;
	}
	BIS(ino, clrmap);
	if (f == IFDIR)
		BIS(ino, dirmap);
	if (ip->di_ctime >= spcl.c_ddate) {
		if (online)
			saveino(ino, ip);
		if (f == IFSHAD)
			return;
		BIS(ino, nodmap);
		if (f != IFREG && f != IFDIR && f != IFLNK) {
			esize += 1;
			return;
		}
		est(ip);
	} else if ((online) && (f == IFSHAD))
		saveino(ino, ip);
}

void
active_mark(ip)
	struct dinode *ip;
{
	register int f;

	f = ip->di_mode & IFMT;
	if (f == 0 || ip->di_nlink <= 0) {
		BIC(ino, clrmap);
		return;
	}
	BIS(ino, clrmap);
	if (f == IFDIR)
		BIS(ino, dirmap);
	if (BIT(ino, activemap)) {
		BIS(ino, nodmap);
		if (f != IFREG && f != IFDIR && f != IFLNK) {
			esize += 1;
			return;
		}
		est(ip);
	}
}

struct shcount {
	struct shcount *higher, *lower;
	ino_t ino;
	unsigned long count;
} shcounts = {
	NULL, NULL,
	0,
	0
};
static struct shcount *shc = NULL;

void
markshad(ip)
	struct dinode *ip;
{
	if (ip->di_shadow == 0)
		return;
	if (shc == NULL)
		shc = &shcounts;

	while ((ip->di_shadow > shc->ino) && (shc->higher))
		shc = shc->higher;
	while ((ip->di_shadow < shc->ino) && (shc->lower))
		shc = shc->lower;
	if (ip->di_shadow != shc->ino) {
		struct shcount *new;

		if ((new = malloc(sizeof (*new))) == NULL) {
			strcpy(msgbuf, gettext("Out of memory\n"));
			msg(msgbuf);
			dumpabort();
		}
		bzero(new, sizeof (*new));
		if (new->higher = shc->higher)
			shc->higher->lower = new;
		shc->higher = new;
		new->lower = shc;
		shc = new;
		shc->ino = ip->di_shadow;
	}

	BIS(ip->di_shadow, shamap);
	shc->count++;
}

void
estshad(ip)
	struct dinode *ip;
{
	long long esizeprime;

	if (ip->di_size <= sizeof (union u_shadow))
		return;

	while ((ino > shc->ino) && (shc->higher))
		shc = shc->higher;
	while ((ino < shc->ino) && (shc->lower))
		shc = shc->lower;
	if (ino != shc->ino)
		return; /* panic? */

	esizeprime = esize;
	est(ip);
	esizeprime = esize - esizeprime;
	esizeprime *= shc->count - 1;
	esize += esizeprime;
}

void
freeshad()
{
	if (shc == NULL)
		return;

	while (shc->higher)
		shc = shc->higher;
	while (shc->lower) {
		shc = shc->lower;
		if (shc->higher) /* else panic? */
			(void) free(shc->higher);
	}
	/* these two lines should be unnecessary, but just to be safe... */
	bzero(shc, sizeof (*shc));
}

void
add(ip)
	struct	dinode	*ip;
{
	register int i;
	long filesize;

	if (BIT(ino, nodmap))
		return;
	if ((ip->di_mode & IFMT) != IFDIR) {
		(void) sprintf(msgbuf, gettext(
			"Warning - directory at inode `%lu' vanished!\n"), ino);
		msg(msgbuf);
		(void) opermes(LOG_WARNING, msgbuf);
		BIC(ino, dirmap);
		return;
	}
	nsubdir = 0;
	dadded = 0;
	filesize = ip->di_size;
	for (i = 0; i < NDADDR; i++) {
		if (ip->di_db[i] != 0)
			dsrch(ip->di_db[i], dblksize(sblock, ip, i), filesize);
		filesize -= sblock->fs_bsize;
	}
	for (i = 0; i < NIADDR; i++) {
		if (ip->di_ib[i] != 0)
			indir(ip->di_ib[i], i, &filesize);
	}
	if (dadded) {
		nadded++;
		if (!BIT(ino, nodmap)) {
			BIS(ino, nodmap);
			if (online)
				saveino(ino, ip);
			est(ip);
		}
	}
	if (nsubdir == 0)
		if (!BIT(ino, nodmap))
			BIC(ino, dirmap);
}

static void
indir(d, n, filesize)
	daddr_t d;
	int n;
	long *filesize;
{
	register i;
	daddr_t	idblk[MAXNINDIR];

	if (dadded || *filesize <= 0)
		return;
	bread(fsbtodb(sblock, d), (char *)idblk, sblock->fs_bsize);
	if (n <= 0) {
		for (i = 0; i < NINDIR(sblock); i++) {
			d = idblk[i];
			if (d != 0)
				dsrch(d, sblock->fs_bsize, *filesize);
			*filesize -= sblock->fs_bsize;
		}
	} else {
		n--;
		for (i = 0; i < NINDIR(sblock); i++) {
			d = idblk[i];
			if (d != 0)
				indir(d, n, filesize);
		}
	}
}

void
dirdump(ip)
	struct dinode *ip;
{
	/* watchout for dir inodes deleted and maybe reallocated */
	if (!online) {
		if ((ip->di_mode & IFMT) != IFDIR || ip->di_nlink < 2) {
			(void) sprintf(msgbuf, gettext(
			    "Warning - directory at inode `%lu' vanished!\n"),
				ino);
			msg(msgbuf);
			(void) opermes(LOG_WARNING, msgbuf);
			return;
		}
	}
	dump(ip);
}

static off_t offset;	/* current offset in file */

void
dumpmeta(ip)
	struct dinode *ip;
{
	if ((ip->di_shadow == 0) || shortmeta)
	    return;

	if (online) {
		ino_t savino = ino;
		ino = ip->di_shadow;
		resetino(ino);
		toslave(doinode, ip->di_shadow);
		ino = savino;
		resetino(ino);
	} else
		dumpinode(getino(ip->di_shadow));
}

int
hasshortmeta(ip)
	struct dinode **ip;
{
	ino_t savino;
	int rc;

	if ((*ip)->di_shadow == 0)
		return (0);
	savino = ino;
	*ip = getino((*ip)->di_shadow);
	rc = ((*ip)->di_size <= sizeof (union u_shadow));
	*ip = getino(ino = savino);
	return (rc);
}

void
dump(ip)
	struct dinode *ip;
{
	register int i;
	long size;

	if ((!BIT(ino, nodmap)) && (!BIT(ino, shamap)))
		return;

	if (online) {
		toslave(doinode, ino);
		dumpmeta(ip);
		return;
	}

	shortmeta = hasshortmeta(&ip);
	if (shortmeta) {
		ip = getino(ip->di_shadow);
		/* assume spcl.c_shadow is smaller than 1 block */
		bread(fsbtodb(sblock, ip->di_db[0]),
		    spcl.c_shadow.c_shadow, sizeof (spcl.c_shadow));
		spcl.c_flags |= DR_HASMETA;
	} else {
		spcl.c_flags &= ~DR_HASMETA;
	}
	ip = getino(ino);

	offset = 0;

	if (newtape) {
		spcl.c_type = TS_TAPE;
	} else if (pos)
		spcl.c_type = TS_ADDR;
	else
		spcl.c_type = TS_INODE;

	newtape = 0;
	dumpinode(ip);
	dumpmeta(ip);
	dumptodatabase = 0;
	pos = 0;
}

void
dumpinode(ip)
    struct dinode *ip;
{
	register int i;
	long size;

	i = ip->di_mode & IFMT;

	if (i == 0 || ip->di_nlink <= 0)
		return;

	spcl.c_dinode = *ip;
	spcl.c_count = 0;

	if ((i != IFDIR && i != IFREG && i != IFLNK && i != IFSHAD) ||
	    ip->di_size == 0) {
		toslave(dospcl, ino);
		return;
	}

	if (i == IFLNK || i == IFDIR)
		dumptodatabase++;

	size = NDADDR * sblock->fs_bsize;
	if (size > ip->di_size)
		size = ip->di_size;

	blksout(&ip->di_db[0], size);

	size = ip->di_size - size;
	if (size > 0) {
		for (i = 0; i < NIADDR; i++) {
			dmpindir(ip->di_ib[i], i, &size);
			if (size <= 0)
				break;
		}
	}
}

static void
dmpindir(blk, lvl, size)
	daddr_t blk;
	int lvl;
	long *size;
{
	int i;
	long cnt;
	daddr_t idblk[MAXNINDIR];

	if (blk != 0)
		bread(fsbtodb(sblock, blk), (char *)idblk, sblock->fs_bsize);
	else
		(void) bzero((char *)idblk, (int)sblock->fs_bsize);
	if (lvl <= 0) {
		cnt = NINDIR(sblock) * sblock->fs_bsize;
		if (cnt > *size)
			cnt = *size;
		*size -= cnt;
		blksout(&idblk[0], cnt);
		return;
	}
	lvl--;
	for (i = 0; i < NINDIR(sblock); i++) {
		dmpindir(idblk[i], lvl, size);
		if (*size <= 0)
			return;
	}
}

static void
blksout(blkp, bytes)
	daddr_t *blkp;
	long bytes;
{
	register int i, j, k, count;
	register int tbperfsb = sblock->fs_bsize / TP_BSIZE;

	off_t bytepos, diff;
	off_t bytecnt = 0;
	off_t byteoff = 0;	/* bytes to skip within first f/s block */
	off_t fragoff = 0;	/* frags to skip within first f/s block */

	int tpblkoff  = 0;	/* tape blocks to skip in first f/s block */
	int tpblkskip = 0;	/* total tape blocks to skip  */
	int skip = 0;		/* tape blocks to skip this pass */

	if (pos) {
		/*
		 * We get here if a slave throws a signal to the
		 * master indicating a partially dumped file.
		 * Begin by figuring out what was undone.
		 */
		bytepos = pos * TP_BSIZE;

		if ((offset + bytes) <= bytepos) {
			/* This stuff was dumped already, forget it. */
			offset += TP_BSIZE * howmany(bytes, TP_BSIZE);
			return;
		}

		if (offset < bytepos) {
			/*
			 * Some of this was dumped, some wasn't.
			 * Figure out what was done and skip it.
			 */
			diff = bytepos - offset;
			tpblkskip = howmany(diff, TP_BSIZE);
			blkp += diff / sblock->fs_bsize;

			bytecnt = diff % sblock->fs_bsize;
			byteoff = bytecnt % sblock->fs_fsize;
			tpblkoff = howmany(bytecnt, TP_BSIZE);
			fragoff = bytecnt / sblock->fs_fsize;
			bytecnt = sblock->fs_bsize - bytecnt;
		}
	}

	offset += bytes;

	while (bytes > 0) {
		if (bytes < TP_NINDIR*TP_BSIZE)
			count = howmany(bytes, TP_BSIZE);
		else
			count = TP_NINDIR;
		if (tpblkskip) {
			if (tpblkskip < TP_NINDIR) {
				bytes -= (tpblkskip * TP_BSIZE);
				skip = tpblkskip;
				tpblkskip = 0;
			} else {
				bytes -= TP_NINDIR*TP_BSIZE;
				tpblkskip -= TP_NINDIR;
				continue;
			}
		} else
			skip = 0;
		for (j = 0, k = 0; j < count - skip; j++, k++) {
			spcl.c_addr[j] = (blkp[k] != 0);
			for (i = tbperfsb - tpblkoff; --i > 0; j++)
				spcl.c_addr[j+1] = spcl.c_addr[j];
			tpblkoff = 0;
		}
		spcl.c_count = count - skip;
		toslave(dospcl, ino);
		bytecnt = MIN(bytes, bytecnt ? bytecnt : sblock->fs_bsize);
		j = 0;
		while (j < count - skip) {
			if (*blkp != 0) {
				dmpblk(*blkp+fragoff, bytecnt, byteoff);
			}
			blkp++;
			bytes -= bytecnt;
			j += howmany(bytecnt, TP_BSIZE);
			bytecnt = MIN(bytes, sblock->fs_bsize);
			byteoff = 0;
			fragoff = 0;
		}
		spcl.c_type = TS_ADDR;
		bytecnt = 0;
	}
	pos = 0;
}

void
bitmap(map, typ)
	char *map;
	int typ;
{
	register int i, count;
	char *cp;

	if (!newtape)
		spcl.c_type = typ;
	else
		newtape = 0;
	for (i = 0; i < TP_NINDIR; i++)
		spcl.c_addr[i] = 1;
	count = howmany(msiz * sizeof (map[0]), TP_BSIZE) - pos;
	for (cp = &map[pos * TP_BSIZE]; count > 0; count -= spcl.c_count) {
		if (leftover) {
			spcl.c_count = leftover;
			leftover = 0;
		} else
			spcl.c_count = count > TP_NINDIR ? TP_NINDIR : count;
		spclrec();
		for (i = 0; i < spcl.c_count; i++, cp += TP_BSIZE)
			taprec(cp, 0);
		spcl.c_type = TS_ADDR;
	}
}

static void
dsrch(d, size, filesize)
	daddr_t d;
	long size, filesize;
{
	register struct direct *dp;
	long loc;
	char dblk[MAXBSIZE];

	if (dadded || filesize <= 0)
		return;
	if (filesize > size)
		filesize = size;
	bread(fsbtodb(sblock, d), dblk, roundup(filesize, DEV_BSIZE));
	loc = 0;
	while (loc < filesize) {
		/*LINTED [dblk is char[], loc (dp->d_reclen) % 4 == 0]*/
		dp = (struct direct *)(dblk + loc);
		if (dp->d_reclen == 0) {
			(void) sprintf(msgbuf, gettext(
		    "Warning - directory at inode `%lu' is corrupted\n"),
				ino);
			msg(msgbuf);
			(void) opermes(LOG_WARNING, msgbuf);
			break;
		}
		loc += dp->d_reclen;
		if (dp->d_ino == 0)
			continue;
		if (dp->d_name[0] == '.') {
			if (dp->d_name[1] == '\0') {
				if (dp->d_ino != ino) {
					(void) sprintf(msgbuf, gettext(
			"Warning - directory at inode `%lu' is corrupted:\n\
\t\".\" points to inode `%lu' - run fsck\n"),
					    ino, dp->d_ino);
					msg(msgbuf);
					(void) opermes(LOG_WARNING, msgbuf);
				}
				continue;
			}
			if (dp->d_name[1] == '.' && dp->d_name[2] == '\0') {
				if (!BIT(dp->d_ino, dirmap)) {
					(void) sprintf(msgbuf, gettext(
			"Warning - directory at inode `%lu' is corrupted:\n\
\t\"..\" points to non-directory inode `%lu' - run fsck\n"),
					    ino, dp->d_ino);
					msg(msgbuf);
					(void) opermes(LOG_WARNING, msgbuf);
				}
				continue;
			}
		}
		if (BIT(dp->d_ino, nodmap)) {
			dadded++;
			return;
		}
		if (BIT(dp->d_ino, dirmap))
			nsubdir++;
	}
}

#define	CACHESIZE 32

struct dinode *
getino(ino)
	ino_t ino;
{
	static ino_t minino, maxino;
	static struct dinode itab[MAXINOPB];
	static struct dinode icache[CACHESIZE];
	static ino_t icacheval[CACHESIZE], lasti = 0;
	static int cacheoff = 0;
	int i;

	if (ino >= minino && ino < maxino) {
		lasti = ino;
		return (&itab[ino - minino]);
	}

	/* before we do major i/o, check for a secondary cache hit */
	for (i = 0; i < CACHESIZE; i++)
		if (icacheval[i] == ino)
			return (icache + i);

	/* we need to do major i/o.  throw the last inode retrieved into */
	/* the cache.  note: this copies garbage the first time it is    */
	/* used, but no harm done.					 */
	icacheval[cacheoff] = lasti;
	bcopy(itab + (lasti - minino), icache + cacheoff, sizeof (itab[0]));
	lasti = ino;
	if (++cacheoff >= CACHESIZE)
		cacheoff = 0;

#define	INOPERDB (DEV_BSIZE / sizeof (struct dinode))
	minino = ino &~ (INOPERDB - 1);
	maxino = (itog(sblock, ino) + 1) * sblock->fs_ipg;
	if (maxino > minino + MAXINOPB)
		maxino = minino + MAXINOPB;
	bread((daddr_t)(fsbtodb(sblock, itod(sblock, ino)) + itoo(sblock, ino) /
	    INOPERDB), (char *)itab,
	    (long)(maxino - minino) * sizeof (struct dinode));
	return (&itab[ino - minino]);
}

#define	BREADEMAX 32

#ifdef NO__LONGLONG__
#define	DEV_LSEEK(fd, offset, whence) \
	lseek((fd), (((off_t)(offset))*DEV_BSIZE), (whence))
#else
#define	DEV_LSEEK(fd, offset, whence) \
	llseek((fd), (((offset_t)(offset))*DEV_BSIZE), (whence))
#endif

void
bread(da, ba, cnt)
daddr_t da;
char	*ba;
long	cnt;
{
	char *dest;

	int n;
	int len;

	static int breaderrors = 0;

	/* mechanics for caching small bread requests.  these are */
	/* often small ACLs that are used over and over.	  */
	static char bcache[DEV_BSIZE * CACHESIZE];
	static daddr_t bcacheval[CACHESIZE];
	static int cacheoff = 0;
	int i;

	if (DEV_LSEEK(fi, da, L_SET) < 0) {
		msg(gettext("%s: %s error\n"), "bread", "dev_seek");
	}

	if (read(fi, ba, (int)cnt) == (int)cnt)
	    return;

	while (cnt > 0) {

		if (da >= fsbtodb(sblock, sblock->fs_size)) {
			msg(gettext(
			    "Warning - block %lu is beyond the end of `%s'\n"),
			    (u_long)da, disk);
			break;
		}

		if (cnt < DEV_BSIZE) {
			/* small read.  check for cache hit. */
			for (i = 0; i < CACHESIZE; i++)
				if (bcacheval[i] == da) {
					bcopy(bcache + (i * DEV_BSIZE),
					    ba, cnt);
					return;
				}

			/* no cache hit; throw this one into the cache... */
			len = cnt;
			dest = bcache + (cacheoff * DEV_BSIZE);
			bcacheval[cacheoff] = da;
			if (++cacheoff >= CACHESIZE)
				cacheoff = 0;
		} else {
			len = DEV_BSIZE;
			dest = ba;
		}

		if (DEV_LSEEK(fi, da, L_SET) < 0)
		    msg(gettext("%s: %s error\n"), "bread", "DEV_LSEEK2");

		n = read(fi, dest, DEV_BSIZE);
		if (n != DEV_BSIZE) {
			n = MAX(n, 0);
			bzero(dest+n, DEV_BSIZE-n);
			breaderrors += 1;
			msg(gettext(
			    "Warning - cannot read sector %lu of `%s'\n"),
			    (u_long)da, disk);
		}
		if (dest != ba)
			(void) bcopy(dest, ba, len);

		da++;
		ba += len;
		cnt -= len;
	}

	if (breaderrors > BREADEMAX) {
		msg(gettext(
		    "More than %d block read errors from dump device `%s'\n"),
		    BREADEMAX, disk);
		dumpailing(gettext("block read errors"));
		breaderrors = 0;
	}
}
