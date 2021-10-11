#ident	"@(#)instance_update.c 1.11 92/03/25"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include "defs.h"
#include <sys/stat.h>
#include <sys/mman.h>

static int inst_fd = -1;
static u_long	inst_maxblk = 0;
static char curhost[128];
static int transfd = -1, transsize;

/*
 * also used by reclaim utility
 */
char	*inst_dblockmap;
int	inst_dblockmapsize;

static struct instance_record *freelisthead;
static int freelistdirty;

#ifdef __STDC__
static int get_user_recsize(void);
static void instance_alloc(void);
static void freeall(void);
static void instance_dirtyrec(u_long);
static struct instance_entry *avail_ent(struct instance_record *);
static int isdeleted(struct instance_entry *);
static int knownvalid(struct instance_entry *);
static int knowndeleted(struct instance_entry *);
static int checkstatus(struct instance_entry *);
#else
static int get_user_recsize();
static void instance_alloc();
static void freeall();
static void instance_dirtyrec();
static struct instance_entry *avail_ent();
static int isdeleted();
static int knownvalid();
static int knowndeleted();
static int checkstatus();
#endif

/*
 * keep track of file mappings. We keep a read mapping and a write
 * mapping.  For each, we keep track of the base of the mapping, the
 * first valid record in that mapping, the record number of the first
 * record in the mapping and the record number of the last record in
 * the mapping.
 */
static caddr_t writemap, readmap;
static struct instance_record *wmapfirst;
static struct instance_record *rmapfirst;
static u_long wmapblk1, wmapblkend;
static u_long rmapblk1, rmapblkend;

#ifdef __STDC__
instance_open(const char *host)
#else
instance_open(host)
	char *host;
#endif
{
	char ifilename[256];
	char itransfile[256];
	struct stat stbuf;
	struct instance_record dummy;
	int	nentries;

	writemap = readmap = NULL;
	wmapfirst = rmapfirst = NULL_IREC;
	wmapblk1 = wmapblkend = rmapblk1 = rmapblkend = transsize = 0;

	(void) sprintf(ifilename, "%s/%s", host, INSTANCEFILE);
	if ((inst_fd = open(ifilename, O_RDONLY)) == -1) {
		/*
		 * get user specified default sizes
		 */
		if (nentries = get_user_recsize()) {
			entries_perrec = nentries;
			instance_recsize = COMPUTE_INST_RECSIZE(nentries);
		} else {
			entries_perrec = DEFAULT_INSTANCE_ENTRIES;
			instance_recsize = DEFAULT_INSTANCE_RECSIZE;
		}
		freelisthead = (struct instance_record *)
			malloc((unsigned)instance_recsize);
		if (freelisthead == NULL) {
			(void) fprintf(stderr,
			    gettext("%s: out of memory\n"), "instance_open");
			return (-1);
		}
		(void) bzero((char *)freelisthead, instance_recsize);
		freelisthead->i_entry[0].ie_dumpid = entries_perrec;
		freelisthead->i_entry[0].ie_dnode_index = instance_recsize;
		inst_maxblk = 0;
	} else {
		freelistdirty = 0;
		if (fstat(inst_fd, &stbuf) == -1) {
			perror("instance_open/fstat");
			(void) close(inst_fd);
			inst_fd = -1;
			return (-1);
		}
		(void) bzero((char *)&dummy, sizeof (struct instance_record));
		if (stbuf.st_size >= sizeof (struct instance_record)) {
			if (read(inst_fd, (char *)&dummy,
					sizeof (struct instance_record)) !=
					sizeof (struct instance_record)) {
				(void) fprintf(stderr,
				    gettext("%s: cannot read freelist\n"),
					"instance_open");
				(void) close(inst_fd);
				inst_fd = -1;
				return (-1);
			}
		}
		entries_perrec = dummy.i_entry[0].ie_dumpid;
		instance_recsize = dummy.i_entry[0].ie_dnode_index;
		if (instance_recsize == 0) {
			if (nentries = get_user_recsize()) {
				entries_perrec = nentries;
				instance_recsize =
					COMPUTE_INST_RECSIZE(nentries);
			} else {
				instance_recsize = DEFAULT_INSTANCE_RECSIZE;
				entries_perrec = DEFAULT_INSTANCE_ENTRIES;
			}
			dummy.i_entry[0].ie_dumpid = entries_perrec;
			dummy.i_entry[0].ie_dnode_index = instance_recsize;
			dummy.ir_next = INSTANCE_FREEREC;
			freelistdirty = 1;
		}
		freelisthead = (struct instance_record *)
			malloc((unsigned)instance_recsize);
		if (freelisthead == NULL) {
			(void) fprintf(stderr,
			    gettext("%s: out of memory\n"), "instance_open");
			(void) close(inst_fd);
			inst_fd = -1;
			return (-1);
		}
		(void) bzero((char *)freelisthead, instance_recsize);
		bcopy((char *)&dummy, (char *)freelisthead,
				sizeof (struct instance_record));
		if (stbuf.st_size % instance_recsize) {
			(void) fprintf(stderr, gettext("%s: block size\n"),
				"instance_open");
			(void) close(inst_fd);
			inst_fd = -1;
			return (-1);
		}
		inst_maxblk = stbuf.st_size / instance_recsize;
	}
	(void) sprintf(itransfile, "%s/%s%s", host, INSTANCEFILE, TRANS_SUFFIX);
	if ((transfd = open(itransfile, O_RDWR|O_CREAT|O_TRUNC, 0600)) == -1) {
		perror("instance_open/transfile");
		(void) close(inst_fd);
		inst_fd = -1;
		return (-1);
	}
	(void) strcpy(curhost, host);
	instance_alloc();
	return (0);
}

static int
#ifdef __STDC__
get_user_recsize(void)
#else
get_user_recsize()
#endif
{
	/*
	 * see if users have over-ridden our default instance record
	 * size.
	 */
	FILE *fp;
	int entries;

	if ((fp = fopen(INSTANCECONFIG, "r")) == NULL) {
		return (0);
	}
	if (fscanf(fp, "%d", &entries) != 1) {
		(void) fclose(fp);
		return (0);
	}
	(void) fclose(fp);

	if (entries <= 0 || entries > 100)
		return (0);
	return (entries);
}

void
#ifdef __STDC__
instance_close(const char *host)
#else
instance_close(host)
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
		inst_dblockmap[INSTANCE_FREEREC] = 1;
		if (lseek(transfd, (off_t)(INSTANCE_FREEREC*instance_recsize),
					SEEK_SET) == -1) {
			perror("instclose/freerec seek");
		} else if (write(transfd, (char *)freelisthead,
				instance_recsize) != instance_recsize) {
			perror("instclose/freerec write");
		}
	}
	if (ftruncate(transfd, (off_t)(inst_maxblk*instance_recsize)) == -1) {
		perror("ftruncate");
		(void) fprintf(stderr,
		    gettext("%s: cannot truncate transaction file\n"),
			"instance_close");
		exit(1);
	}
	(void) fsync(transfd);
	(void) close(transfd);
	transfd = -1;
	inst_dblockmapsize = min(inst_maxblk, inst_dblockmapsize);
	(void) sprintf(mapfile, "%s/%s%s", host, INSTANCEFILE, MAP_SUFFIX);
	if ((fd = open(mapfile, O_RDWR|O_CREAT|O_TRUNC, 0600)) == -1) {
		perror("open");
		(void) fprintf(stderr, gettext("%s: cannot write dirty map\n"),
			"instance_close");
	}
	if (write(fd, inst_dblockmap,
			inst_dblockmapsize) != inst_dblockmapsize) {
		perror("inst_dblockmap/write");
	}
	(void) fsync(fd);
	(void) close(fd);
	freeall();
	if (inst_fd != -1 && close(inst_fd) == -1)
		perror("instance_close/close");
	inst_fd = -1;
}

#define	NEWCACHESIZE	2
#define	NEWENT		0
#define	OLDENT		1
static struct {
	u_long	blknum;
	struct instance_record *ir;
} newcache[NEWCACHESIZE];

static void
#ifdef __STDC__
instance_alloc(void)
#else
instance_alloc()
#endif
{
	newcache[NEWENT].blknum = newcache[OLDENT].blknum = NONEXISTENT_BLOCK;
	newcache[NEWENT].ir = (struct instance_record *)
			malloc((unsigned)instance_recsize);
	newcache[OLDENT].ir = (struct instance_record *)
			malloc((unsigned)instance_recsize);
	if (newcache[NEWENT].ir == NULL || newcache[OLDENT].ir == NULL) {
		(void) fprintf(stderr, gettext("%s: cannot allocate cache\n"),
			"instance_update");
		exit(1);
	}
}

static void
#ifdef __STDC__
freeall(void)
#else
freeall()
#endif
{
	free((char *)newcache[OLDENT].ir);
	free((char *)newcache[NEWENT].ir);
	free((char *)freelisthead);
}

instance_newrec(next)
	u_long next;
{
	struct instance_record *ip;
	u_long curblk;

	if (inst_maxblk == 0) {
		freelisthead->ir_next = INSTANCE_FREEREC;
		freelistdirty++;
		inst_maxblk++;	/* freelisthead block */
	}

	ip = newcache[NEWENT].ir;
	if (freelisthead->ir_next != INSTANCE_FREEREC) {
		/*
		 * read from the instance file since we know that no
		 * records are ever freed as a result of update processing.
		 * If records were freed during update, we would have to
		 * consider reading from the transaction file here as
		 * well...
		 *
		 * We don't bother with any mapping or caching here
		 * since we assume this is a uncommon case...
		 */
		curblk = freelisthead->ir_next;
		if (lseek(inst_fd, (off_t)(curblk*instance_recsize),
						SEEK_SET) == -1) {
			perror("inst_newrec/freeblk lseek");
			return (NONEXISTENT_BLOCK);
		}
		if (read(inst_fd, (char *)ip,
				instance_recsize) != instance_recsize) {
			perror("inst_newrec/freeblk read");
			return (NONEXISTENT_BLOCK);
		}
		freelistdirty++;
		freelisthead->ir_next = ip->ir_next;
	} else {
		curblk = inst_maxblk++;
	}
	newcache[NEWENT].blknum = curblk;
	if (next == NONEXISTENT_BLOCK)
		next = curblk;
	ip->ir_next = next;
	(void) bzero((char *)ip->i_entry,
		(sizeof (struct instance_entry) * entries_perrec));
	return (curblk);
}

/*
 * XXX the `reclaim' utility calls this
 */
struct instance_record *
instance_getrec(recnum)
	u_long recnum;
{
	int fd;
	struct instance_record *irp;

	if (recnum == newcache[NEWENT].blknum)
		return (newcache[NEWENT].ir);

	if (recnum == newcache[OLDENT].blknum)
		return (newcache[OLDENT].ir);

	if (inst_dblockmap && inst_dblockmapsize > recnum &&
	    inst_dblockmap[recnum]) {
		(void) fprintf(stderr, gettext("%s: getting dirty block!\n"),
			"instance_getrec");
		fd = transfd;
		if (lseek(fd, (off_t)(recnum*instance_recsize),
						SEEK_SET) == -1) {
			perror("instance_get/lseek");
			return (NULL_IREC);
		}
		if (read(fd, (char *)newcache[OLDENT].ir,
				instance_recsize) != instance_recsize) {
			perror("instance_get/read");
			return (NULL_IREC);
		}
	} else {
		fd = inst_fd;
		irp = (struct instance_record *)getmapblock(&readmap,
			(char **)&rmapfirst, &rmapblk1, &rmapblkend, recnum,
			/*LINTED*/
			instance_recsize, fd, PROT_READ, 0, (int *)0);
		bcopy((char *)irp, (char *)newcache[OLDENT].ir,
				instance_recsize);
	}
	newcache[OLDENT].blknum = recnum;
	return (newcache[OLDENT].ir);
}

static void
instance_dirtyrec(num)
	u_long num;
{
	struct instance_record *p, *dirtyp;

	if (inst_dblockmap == NULL || num >= inst_dblockmapsize) {
		int newsize;
		char *tp;

		if (inst_dblockmap) {
			newsize = num+1000;
			tp = (char *)realloc(inst_dblockmap, (unsigned)newsize);
			if (tp == NULL) {
				(void) fprintf(stderr, gettext(
					"cannot extend instance map\n"));
				exit(1);
			}
			inst_dblockmap = tp;
			(void) bzero(&tp[inst_dblockmapsize],
					newsize-inst_dblockmapsize);
			inst_dblockmapsize = newsize;
		} else {
			newsize = inst_dblockmapsize = num+1000;
			tp = (char *)malloc((unsigned)newsize);
			if (tp == NULL) {
				(void) fprintf(stderr, gettext(
					"cannot allocate instance map\n"));
				exit(1);
			}
			inst_dblockmap = tp;
			(void) bzero(tp, newsize);
		}
	}
	inst_dblockmap[num] = 1;
	if (num == newcache[NEWENT].blknum)
		dirtyp = newcache[NEWENT].ir;
	else if (num == newcache[OLDENT].blknum)
		dirtyp = newcache[OLDENT].ir;
	else {
		(void) fprintf(stderr,
			gettext("%s: block mismatch!\n"), "instance_dirty");
		return;
	}

	p = (struct instance_record *)getmapblock(&writemap,
			(char **)&wmapfirst, &wmapblk1, &wmapblkend,
			num, instance_recsize,
			/*LINTED*/
			transfd, PROT_READ|PROT_WRITE, 1, &transsize);
	bcopy((char *)dirtyp, (char *)p, instance_recsize);
}

instance_addent(recnum, dumpid, dnode_idx)
	u_long	recnum;
	u_long	dumpid;
	u_long	dnode_idx;
{
	struct instance_record *ip;
	struct instance_entry *ep;
	u_long firstrec, thisrec;

	if ((ip = instance_getrec(recnum)) == NULL_IREC)
		return (-1);

	firstrec = thisrec = recnum;
	while ((ep = avail_ent(ip)) == NULL_IENT) {
		if (ip->ir_next == firstrec) {
			/*
			 * XXX: this blows the single block cache
			 * strategy!  However, it seems we never have
			 * more than 2 blocks "active" at a time, so
			 * a two block "cache" ought to work...
			 */
			ip->ir_next = instance_newrec(ip->ir_next);
			instance_dirtyrec(thisrec);
		}
		thisrec = ip->ir_next;
		if ((ip = instance_getrec(ip->ir_next)) == NULL_IREC)
			return (-1);
	}

	ep->ie_dumpid = dumpid;
	ep->ie_dnode_index = dnode_idx;

	instance_dirtyrec(thisrec);

	return (0);
}

static struct instance_entry *
avail_ent(rp)
	struct instance_record *rp;
{
	register int i;
	register struct instance_entry *ep;

	for (i = 0, ep = &rp->i_entry[i]; i < entries_perrec;
					i++, ep = &rp->i_entry[i]) {
		if (ep->ie_dumpid == 0) {
			return (ep);
		} else if (isdeleted(ep)) {
			/*
			 * XXX: need to check performance hit here!
			 */
			(void) bzero((char *)ep,
				sizeof (struct instance_entry));
			return (ep);
		}
	}
	return (NULL_IENT);
}

static int
isdeleted(ep)
	struct instance_entry *ep;
{
	if (knownvalid(ep))
		return (0);
	else if (knowndeleted(ep))
		return (1);
	else
		return (checkstatus(ep));
}

static struct instance_list {
	u_long dumpid;
	struct instance_list *nxt;
} *valid, *deleted;

static int
knownvalid(ep)
	struct instance_entry *ep;
{
	register struct instance_list *p;

	for (p = valid; p; p = p->nxt) {
		if (p->dumpid == ep->ie_dumpid)
			return (1);
	}
	return (0);
}

static int
knowndeleted(ep)
	struct instance_entry *ep;
{
	register struct instance_list *p;

	for (p = deleted; p; p = p->nxt) {
		if (p->dumpid == ep->ie_dumpid)
			return (1);
	}
	return (0);
}

static int
checkstatus(ep)
	struct instance_entry *ep;
{
	struct stat stbuf;
	char filename[64];
	int rc;
	struct instance_list *p;

	if ((p = (struct instance_list *)
			malloc((unsigned)sizeof (struct instance_list))) ==
			(struct instance_list *)0) {
		(void) fprintf(stderr,
		    gettext("%s: out of memory\n"), "checkstatus");
		return (0);
	}
	p->dumpid = ep->ie_dumpid;
	p->nxt = (struct instance_list *)0;

	(void) sprintf(filename, "%s/%s.%lu",
		curhost, DNODEFILE, ep->ie_dumpid);
	if (lstat(filename, &stbuf) == -1) {
		/* treat it as deleted */
		p->nxt = deleted;
		deleted = p;
		rc = 1;
	} else {
		p->nxt = valid;
		valid = p;
		rc = 0;
	}
	return (rc);
}

/*
 * these functions are used only by the compress utility (not during
 * update).
 */

/*
 * check for any valid instances at the given instance record.
 * If none, free the record.
 */
unused_instance(irec, cnt)
	u_long irec;
	int *cnt;
{
	struct instance_record *irp;
	struct instance_entry *ie;
	register int i;
	u_long startrec, thisrec, freerec;

	/*
	 * XXX: what about coalescing sparse instance records???
	 */

	if (inst_dblockmap && inst_dblockmapsize > irec &&
			inst_dblockmap[irec]) {
		/*
		 * during free space reclaim the only way for a
		 * record to be dirty is if it has already been freed.
		 */
		return (1);
	}

	startrec = thisrec = irec;
	do {
		if ((irp = instance_getrec(thisrec)) == NULL_IREC) {
			(void) fprintf(stderr,
			    gettext("unused istance cannot get record %lu\n"),
				irec);
			exit(1);
		}

		for (i = 0; i < entries_perrec; i++) {
			ie = &irp->i_entry[i];
			if (ie->ie_dumpid == 0)
				continue;
			if (knownvalid(ie))
				return (0);
			else if (knowndeleted(ie))
				continue;
			else if (checkstatus(ie) == 0)
				return (0);
		}
		thisrec = irp->ir_next;
	} while (thisrec != startrec);

	/*
	 * if we get here, this set of instance records does not
	 * point at any valid dumps.
	 */
	startrec = thisrec = irec;
	do {
		if ((irp = instance_getrec(thisrec)) == NULL_IREC) {
			(void) fprintf(stderr,
			    gettext("unused instance cannot get record %lu\n"),
				irec);
			exit(1);
		}

		freerec = thisrec;
		thisrec = irp->ir_next;
		(*cnt)++;
		instance_freerec(freerec, irp);
	} while (thisrec != startrec);
	return (1);
}

void
instance_freerec(num, irp)
	u_long num;
	struct instance_record *irp;
{
	irp->ir_next = freelisthead->ir_next;
	freelisthead->ir_next = num;
	instance_dirtyrec(num);
	freelistdirty = 1;
}
