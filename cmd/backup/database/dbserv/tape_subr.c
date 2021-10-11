
#ident	"@(#)tape_subr.c 1.12 93/06/28"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include "defs.h"
#include <sys/stat.h>
#include <sys/mman.h>

static int	tape_fd = -1;
static u_long	tape_maxblk = 0;
static int	transfd = -1, transsize;

char *tape_dblockmap;
int tape_dblockmapsize;

/*
 * keep track of file mappings. We keep a read mapping and a write
 * mapping.  For each, we keep track of the base of the mapping, the
 * first valid record in that mapping, the record number of the first
 * record in the mapping and the record number of the last record in
 * the mapping.
 */
static caddr_t writemap, readmap;
static struct active_tape *wmapfirst;
static struct active_tape *rmapfirst;
static u_long wmapblk1, wmapblkend;
static u_long rmapblk1, rmapblkend;

static struct active_tape freelisthead;
static int freelistdirty;

#ifdef __STDC__
static void tape_dirtyrec(u_long);
static struct active_tape *tape_getrec(u_long);
static struct dump_entry *avail_ent(struct active_tape *);
static struct active_tape *getfreeblock(u_long *);
#else
static void tape_dirtyrec();
static struct active_tape *tape_getrec();
static struct dump_entry *avail_ent();
static struct active_tape *getfreeblock();
#endif

#ifdef __STDC__
tape_open(const char *host)
#else
tape_open(host)
	char *host;
#endif
{
	char ttransfile[256];
	struct stat stbuf;

	writemap = readmap = NULL;
	wmapfirst = rmapfirst = NULL_TREC;
	wmapblk1 = wmapblkend = rmapblk1 = rmapblkend = transsize = 0;
	tape_maxblk = 0;
	freelistdirty = 0;
	(void) bzero((char *)&freelisthead, TAPE_RECSIZE);

	if ((tape_fd = open(TAPEFILE, O_RDONLY)) != -1) {
		if (fstat(tape_fd, &stbuf) == -1) {
			perror("tape_open/fstat");
			return (-1);
		}
		if (stbuf.st_size % TAPE_RECSIZE) {
			(void) fprintf(stderr,
				gettext("%s: block size\n"), "tape_open");
			(void) close(tape_fd);
			tape_fd = -1;
			return (-1);
		}
		tape_maxblk = stbuf.st_size / TAPE_RECSIZE;

		if (read(tape_fd, (char *)&freelisthead,
					TAPE_RECSIZE) != TAPE_RECSIZE) {
			(void) fprintf(stderr, gettext(
				"%s: cannot read freelist\n"), "tape_open");
			(void) close(tape_fd);
			tape_fd = -1;
			return (-1);
		}
	}
	(void) sprintf(ttransfile, "%s/%s%s", host, TAPEFILE, TRANS_SUFFIX);
	if ((transfd = open(ttransfile, O_RDWR|O_CREAT|O_TRUNC, 0600)) == -1) {
		perror("tape_open/transfile");
		return (-1);
	}
	return (0);
}

void
#ifdef __STDC__
tape_close(const char *host)
#else
tape_close(host)
	char *host;
#endif
{
#define	min(a, b)	(a < b ? a : b)

	char mapfile[256];
	int fd;

	if (writemap) {
		release_map(writemap, 1);
		writemap = NULL;
	}
	if (readmap) {
		release_map(readmap, 0);
		readmap = NULL;
	}
	if (freelistdirty) {
		tape_dblockmap[TAPE_FREELIST] = 1;
		if (lseek(transfd, (off_t)(TAPE_FREELIST*TAPE_RECSIZE),
						SEEK_SET) == -1) {
			perror("tape_close/freerec seek");
		} else if (write(transfd, (char *)&freelisthead,
				TAPE_RECSIZE) != TAPE_RECSIZE) {
			perror("tape_close/freerec write");
		}
	}
	if (ftruncate(transfd, (off_t)(tape_maxblk*TAPE_RECSIZE)) == -1) {
		perror("ftruncate");
		(void) fprintf(stderr,
		    gettext("%s: cannot truncate transaction file\n"),
			"tape_close");
		exit(1);
	}
	(void) fsync(transfd);
	(void) close(transfd);
	transfd = -1;
	tape_dblockmapsize = min(tape_maxblk, tape_dblockmapsize);
	(void) sprintf(mapfile, "%s/%s%s", host, TAPEFILE, MAP_SUFFIX);
	if ((fd = open(mapfile, O_RDWR|O_CREAT|O_TRUNC, 0600)) == -1) {
		perror("open");
		(void) fprintf(stderr, gettext("%s: cannot write dirty map\n"),
			"tape_close");
	}
	if (write(fd, tape_dblockmap,
			tape_dblockmapsize) != tape_dblockmapsize) {
		perror("tape_dblockmap/write");
	}
	(void) fsync(fd);
	(void) close(fd);
	if (tape_fd != -1 && close(tape_fd) == -1)
		perror("tape_close/close");
	tape_fd = -1;
}

/*
 * we keep a very small cache of blocks.
 *
 *  The GETENT entry is used by the static routine `tape_getrec()'.
 *  The LOOKENT entry is used by the exported routines `tape_{lookup,nextent}'.
 *  This allows us to read records internally (e.g., tape_remdump()) without
 *  disturbing the cached copy that an external client is using.
 *  The NEWENT entry is used by `tape_newrec()' when creating a new record.
 */
#define	TCACHESIZE	3
#define	NEWENT		0
#define	GETENT		1
#define	LOOKENT		2
static struct {
	u_long blknum;
	struct active_tape tr;
} tcache[TCACHESIZE];

static void
tape_dirtyrec(num)
	u_long num;
{
	struct active_tape *p, *dirtyp;

	if (tape_dblockmap == NULL || num >= tape_dblockmapsize) {
		int newsize;
		char *tp;

		if (tape_dblockmap) {
			newsize = num+1000;
			tp = (char *)realloc(tape_dblockmap, (unsigned)newsize);
			if (tp == NULL) {
				(void) fprintf(stderr,
					gettext("cannot extend tape map\n"));
				exit(1);
			}
			tape_dblockmap = tp;
			(void) bzero(&tp[tape_dblockmapsize],
					newsize-tape_dblockmapsize);
			tape_dblockmapsize = newsize;
		} else {
			newsize = tape_dblockmapsize = num+1000;
			tp = (char *)malloc((unsigned)newsize);
			if (tp == NULL) {
				(void) fprintf(stderr,
					gettext("cannot allocate tape map\n"));
				exit(1);
			}
			tape_dblockmap = tp;
			(void) bzero(tp, newsize);
		}
	}
	tape_dblockmap[num] = 1;
	if (num == tcache[NEWENT].blknum)
		dirtyp = &tcache[NEWENT].tr;
	else if (num == tcache[GETENT].blknum)
		dirtyp = &tcache[GETENT].tr;
	else if (num == tcache[LOOKENT].blknum)
		dirtyp = &tcache[LOOKENT].tr;
	else {
		(void) fprintf(stderr,
			gettext("%s: block mismatch!\n"), "tape_dirty");
		return;
	}
	p = (struct active_tape *)getmapblock(&writemap, (char **)&wmapfirst,
			&wmapblk1, &wmapblkend, num, TAPE_RECSIZE,
			/*LINTED*/
			transfd, PROT_READ|PROT_WRITE, 1, &transsize);
	bcopy((char *)dirtyp, (char *)p, TAPE_RECSIZE);
}

/*
 * lookup a tape record for the given label.
 */
struct active_tape *
#ifdef __STDC__
tape_lookup(const char *label,
	u_long *recnum)
#else
tape_lookup(label, recnum)
	char *label;
	u_long *recnum;
#endif
{
	struct active_tape *tp;
	register int i;

	for (i = TAPE_FIRSTDATA; i < tape_maxblk; i++) {
		if ((tp = tape_getrec((u_long)i)) == NULL_TREC) {
			(void) fprintf(stderr, gettext("%s: %s error\n"),
				"tape_lookup", "getrec");
			return (NULL_TREC);
		}
		if (bcmp(tp->tape_label, label, LBLSIZE) == 0) {
			*recnum = (u_long)i;
			tcache[LOOKENT].blknum = i;
			bcopy((char *)tp,
				(char *)&tcache[LOOKENT].tr, TAPE_RECSIZE);
			return (&tcache[LOOKENT].tr);
		}
	}
	return (NULL_TREC);
}

/*
 * follow a chain of records that started with a `tape_lookup()'
 */
struct active_tape *
#ifdef __STDC__
tape_nextent(const char *label,
	u_long recnum)
#else
tape_nextent(label, recnum)
	char *label;
	u_long recnum;
#endif
{
	register struct active_tape *tp;

	if (recnum != tcache[LOOKENT].tr.tape_next) {
		(void) fprintf(stderr, gettext("bad %s\n"), "tape_nextent");
		return (NULL_TREC);
	}

	if ((tp = tape_getrec(recnum)) == NULL_TREC) {
		(void) fprintf(stderr,
			gettext("%s: %s error\n"), "tape_nextent", "getrec");
		return (NULL_TREC);
	}

	if (bcmp(tp->tape_label, label, LBLSIZE)) {
		(void) fprintf(stderr,
			gettext("%s: label error\n"), "tape_nextent");
		return (NULL_TREC);
	}
	tcache[LOOKENT].blknum = recnum;
	bcopy((char *)tp, (char *)&tcache[LOOKENT].tr, TAPE_RECSIZE);
	return (&tcache[LOOKENT].tr);
}

#ifdef __STDC__
tape_newrec(const char *label,
	u_long next)
#else
tape_newrec(label, next)
	char *label;
	u_long next;
#endif
{
	struct active_tape *tp;
	u_long curblk;

	if (tape_maxblk == 0) {
		/* empty file */
		freelisthead.tape_next = TAPE_FREELIST;
		freelistdirty++;
		tape_maxblk++;
		tp = NULL_TREC;
	} else {
		tp = getfreeblock(&curblk);
	}
	if (tp == NULL_TREC) {
		curblk = tape_maxblk++;
	}
	if (next == NONEXISTENT_BLOCK)
		next = curblk;
	tp = &tcache[NEWENT].tr;
	tcache[NEWENT].blknum = curblk;
	tp->tape_next = next;
	(void) bzero((char *)tp->dumps,
		(sizeof (struct dump_entry) * DUMPS_PER_TAPEREC));
	bcopy((char *)label, (char *)tp->tape_label, LBLSIZE);
	tape_dirtyrec(curblk);
	return (curblk);
}

void
tape_remdump(dumpid)
	u_long dumpid;
{
	register int i, j;
	register struct active_tape *tp;

	for (i = TAPE_FIRSTDATA; i < tape_maxblk; i++) {
		if ((tp = tape_getrec((u_long)i)) == NULL_TREC) {
			(void) fprintf(stderr, gettext("%s: %s error\n"),
				"tape_remdump", "getrec");
			continue;
		}
		for (j = 0; j < DUMPS_PER_TAPEREC; j++) {
			if (tp->dumps[j].dump_id == dumpid) {
				(void) bzero((char *)&tp->dumps[j],
					sizeof (struct dump_entry));
				tape_dirtyrec((u_long)i);
			}
		}
	}
}

/*
 * connect the records specified in the array "chain", making sure that
 * each of them has a tape_label field that matches "label"
 */
void
#ifdef __STDC__
tape_rechain(const char *label, const u_long *chain, int chaincnt)
#else
tape_rechain(label, chain, chaincnt)
	char *label;
	u_long *chain;
	int chaincnt;
#endif
{
	register int i, idx;
	register struct active_tape *tp;

	for (i = 0; i < chaincnt; i++) {
		if ((tp = tape_getrec(chain[i])) == NULL_TREC) {
			(void) fprintf(stderr, gettext(
				"%s: %s failure\n"), "tape_rechain",
				"getrec");
			return;
		}
		if (bcmp(tp->tape_label, label, LBLSIZE)) {
			fprintf(stderr, gettext("%s: label error\n"),
				"tape_rechain");
			return;
		}
		idx = i + 1;
		if (idx >= chaincnt)
			idx = 0;
		tp->tape_next = chain[idx];
		tape_dirtyrec(chain[i]);
	}
}

tape_changehost(old, new)
	u_long old, new;
{
	register int i, j;
	register struct active_tape *tp;

	for (i = TAPE_FIRSTDATA; i < tape_maxblk; i++) {
		if ((tp = tape_getrec((u_long)i)) == NULL_TREC) {
			(void) fprintf(stderr, gettext("%s: %s error\n"),
				"tape_changehost", "getrec");
			continue;
		}
		for (j = 0; j < DUMPS_PER_TAPEREC; j++) {
			if (tp->dumps[j].host == old) {
				tp->dumps[j].host = new;
				tape_dirtyrec((u_long)i);
			}
		}
	}
	return (0);
}

#ifdef notdef
tape_setstatus(recnum, status)
	u_long recnum;
	int status;
{
	struct active_tape *tp;

	if ((tp = tape_getrec(recnum)) == NULL_TREC) {
		return (-1);
	}
	tp->tape_status = status;
	tape_dirtyrec(recnum);
	return (0);
}
#endif

tape_freerec(recnum)
	u_long	recnum;
{
	register struct active_tape *tp1;

	if ((tp1 = tape_getrec(recnum)) == NULL_TREC) {
		(void) fprintf(stderr,
		    gettext("%s: cannot get block %lu to free\n"),
			"tape_freerec", recnum);
		return (-1);
	}
	(void) bzero(tp1->tape_label, LBLSIZE);
	tp1->tape_next = freelisthead.tape_next;
	freelisthead.tape_next = recnum;
	freelistdirty++;
	tape_dirtyrec(recnum);
	return (0);
}

static struct active_tape *
tape_getrec(recnum)
	u_long recnum;
{
	int fd;
	struct active_tape *tp;

	if (recnum == tcache[NEWENT].blknum)
		return (&tcache[NEWENT].tr);

	if (recnum == tcache[GETENT].blknum)
		return (&tcache[GETENT].tr);

	if (tape_dblockmap && tape_dblockmapsize > recnum &&
			tape_dblockmap[recnum]) {
		fd = transfd;
		if (lseek(fd, (off_t)(recnum*TAPE_RECSIZE), SEEK_SET) == -1) {
			perror("tape_getrec/lseek");
			return (NULL_TREC);
		}
		if (read(fd, (char *)&tcache[GETENT].tr,
				TAPE_RECSIZE) != TAPE_RECSIZE) {
			perror("tape_getrec/read");
			return (NULL_TREC);
		}
	} else {
		fd = tape_fd;
		tp = (struct active_tape *)getmapblock(&readmap,
				(char **)&rmapfirst, &rmapblk1, &rmapblkend,
				recnum, TAPE_RECSIZE,
				/*LINTED*/
				fd, PROT_READ, 0, (int *)0);
		bcopy((char *)tp, (char *)&tcache[GETENT].tr, TAPE_RECSIZE);
	}
	tcache[GETENT].blknum = recnum;
	return (&tcache[GETENT].tr);
}

tape_addent(recnum, host, dumpid, pos)
	u_long	recnum;
	u_long	host;
	u_long	dumpid;
	u_long	pos;
{
	struct active_tape *tp;
	struct dump_entry *ep;
	u_long firstrec, thisrec, newrec;

	if ((tp = tape_getrec(recnum)) == NULL_TREC)
		return (-1);

	firstrec = thisrec = recnum;
	while ((ep = avail_ent(tp)) == NULL_DENT) {
		if (tp->tape_next == firstrec) {
			char holdlabel[LBLSIZE];
			u_long holdnext;

			/*
			 * no space available in any records that
			 * currently describe this tape so we get a
			 * new record.
			 * Be very careful about any data in our regrettably
			 * small cache since tape_newrec() may call
			 * tape_getrec() and invalidate the copy we've
			 * got here...
			 */
			holdnext = tp->tape_next;
			bcopy(tp->tape_label, holdlabel, LBLSIZE);
			newrec = tape_newrec(holdlabel, holdnext);

			/* get `thisrec' back in a place we can update */
			if ((tp = tape_getrec(thisrec)) == NULL_TREC)
				return (-1);
			tp->tape_next = newrec;
			tape_dirtyrec(thisrec);
		}
		thisrec = tp->tape_next;
		if ((tp = tape_getrec(tp->tape_next)) == NULL_TREC)
			return (-1);
	}

	ep->host = host;
	ep->dump_id = dumpid;
	ep->tapepos = pos;

	tape_dirtyrec(thisrec);

	return (0);
}

static struct dump_entry *
avail_ent(rp)
	struct active_tape *rp;
{
	register int i;
	register struct dump_entry *ep;

	for (i = 0, ep = &rp->dumps[i]; i < DUMPS_PER_TAPEREC;
					i++, ep = &rp->dumps[i]) {
		if (ep->host == 0)
			return (ep);
	}
	return (NULL_DENT);
}

static struct active_tape *
getfreeblock(blk)
	u_long *blk;
{
	register struct active_tape *tp1;

	if (freelisthead.tape_next == TAPE_FREELIST)
		/* empty freelist */
		return (NULL_TREC);
	*blk = freelisthead.tape_next;
	if ((tp1 = tape_getrec(freelisthead.tape_next)) == NULL_TREC) {
		(void) fprintf(stderr, gettext("corrupt freelist!\n"));
		return (NULL_TREC);
	}
	freelisthead.tape_next = tp1->tape_next;
	freelistdirty++;
	return (tp1);
}
