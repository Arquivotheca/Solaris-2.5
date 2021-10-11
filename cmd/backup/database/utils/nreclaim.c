
#ident	"@(#)nreclaim.c 1.16 93/05/12"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include <config.h>
#include "defs.h"
#define	_POSIX_SOURCE	/* hack to avoid redef of MAXNAMLEN */
#define	_POSIX_C_SOURCE
#include <dirent.h>
#undef	_POSIX_C_SOURCE
#undef	_POSIX_SOURCE
#include <sys/stat.h>
#include <sys/mman.h>

/*
 * reclaim unused space in the dir and instance files of the
 * specified host.
 */

#define	ALL_AVAILABLE	DIRBLOCK_DATASIZE
#define	ENTRYSIZE(ep)	((sizeof (struct dir_entry)+\
			DE_NAMELEN(ep->de_name_len)))
#define	MIN_ENTRYSIZE	((sizeof (struct dir_entry)+DE_NAMELEN(1)))
#define	FREE_ENTRY	((char)0)

#define	PART_FREE	1
#define	ALL_FREE	2

static int freedroot;
static int dirs_freed;
static int inst_freed;

#ifdef __STDC__
static int checkdir(u_long);
static void compress_dir(u_long);
static void compress_block(struct dir_block *);
static void shuffle_blocks(struct dir_block *, u_long, u_long);
static void dir_freeblk(u_long);
static void move_entry(struct dir_entry *, struct dir_block *);
static void free_entry(struct dir_entry *);
static struct dir_entry *get_available(struct dir_entry *, struct dir_block *);
static struct dir_entry *get_valid(struct dir_entry *, struct dir_block *);
static struct dir_entry *get_cache_ent(u_long, int);
#else
static int checkdir();
static void compress_dir();
static void compress_block();
static void shuffle_blocks();
static void dir_freeblk();
static void move_entry();
static void free_entry();
static struct dir_entry *get_available();
static struct dir_entry *get_valid();
static struct dir_entry *get_cache_ent();
#endif

void
reclaim(dbroot, dbhost)
	char *dbroot;
	char *dbhost;
{
	char root[256];
	char thishost[BCHOSTNAMELEN+1];

	if (dbroot == NULL) {
		(void) fprintf(stderr, gettext(
			"Enter root of database file hierarchy: "));
		if (gets(root) == NULL)
			exit(1);
		dbroot = root;
	}
	if (chdir(dbroot) == -1) {
		perror("chdir");
		(void) fprintf(stderr,
			gettext("cannot cd to database root %s\n"), dbroot);
		exit(1);
	}

	for (;;) {
		char filename[MAXPATHLEN];
		struct stat stbuf;

		dbhost = getdbhost(dbhost);
		if (dbhost == NULL)
			continue;
		(void) sprintf(filename, "%s/%s", dbhost, DIRFILE);
		if (stat(filename, &stbuf) == -1) {
			(void) fprintf(stderr,
				gettext("Cannot find `%s'\n"), filename);
			dbhost = NULL;
			continue;
		}
		(void) sprintf(filename, "%s/%s", dbhost, INSTANCEFILE);
		if (stat(filename, &stbuf) == -1) {
			(void) fprintf(stderr,
				gettext("Cannot find `%s'\n"), filename);
			dbhost = NULL;
			continue;
		}
		break;
	}

	if (gethostname(thishost, BCHOSTNAMELEN)) {
		perror("gethostname");
		exit(1);
	}
	maint_lock();
	pokeserver(QUIESCE_OPERATION, thishost);

	(void) dir_open(dbhost);
	(void) instance_open(dbhost);
	(void) checkdir(DIR_ROOTBLK);
	if (freedroot) {
		(void) fprintf(stderr, gettext(
		    "Host `%s' removed from database\n\
since it no longer has any data.\n"),
			dbhost);
		/*
		 * Since I run as root, I can unlink a non-empty directory.
		 */
		if (unlink(dbhost) == -1) {
			perror("unlink");
		}
	} else {
		dir_close(dbhost);
		instance_close(dbhost);
		dir_trans(dbhost);
		instance_trans(dbhost);
		(void) fprintf(stderr,
		    gettext("freed %d dir blocks and %d instance records\n"),
			dirs_freed, inst_freed);
	}
	pokeserver(RESUME_OPERATION, thishost);
	maint_unlock();
}

static int
checkdir(blk)
	u_long blk;
{
	struct dir_block *bp;
	struct dir_entry *ep;
	u_long startblk, thisblk;
	int children, entries, keepers, retcnt;
	struct dir_block copyblock;
	int dotblock;

	startblk = thisblk = blk;
	retcnt = 0;
	do {
		if ((bp = dir_getblock(thisblk)) == NULL_DIRBLK) {
			(void) fprintf(stderr, gettext(
				"cannot get dirblk %lu\n"), blk);
			exit(1);
		}

		/*
		 * keep a local copy of the block in case it gets
		 * blown out of the cache when we recurse
		 */
		(void) bcopy((char *)bp, (char *)&copyblock, DIR_BLKSIZE);
		bp = &copyblock;
		/*LINTED [alignment ok]*/
		ep = (struct dir_entry *)bp->db_data;
		entries = keepers = 0;
		dotblock = 0;
		/*LINTED [alignment ok]*/
		for (; ep != DE_END(bp); ep = DE_NEXT(ep)) {
			if (strcmp(ep->de_name, "..") == 0 ||
					strcmp(ep->de_name, ".") == 0) {
				dotblock = 1;
				continue;
			}
			children = 0;
			entries++; keepers++;
			if (ep->de_directory != NONEXISTENT_BLOCK) {
				children = checkdir(ep->de_directory);
			}
			if (!children) {
				struct dir_entry *dep;

				if (ep->de_instances == NONEXISTENT_BLOCK ||
						unused_instance(
							ep->de_instances,
							&inst_freed)) {
					dep = get_cache_ent(thisblk,
						((char *)ep - (char *)bp));
					keepers--;
					free_entry(dep);
					dir_dirtyblock(thisblk);
				}
			}
		}
		bp = dir_getblock(thisblk);
		if (keepers == 0) {
			/*
			 * entire block freed - maybe.
			 * If the `dot' entry for a dir has valid instances,
			 * we can't get rid of the block.
			 */
			bp->db_flags = ALL_FREE;
			if (dotblock) {
				/*LINTED [alignment ok]*/
				ep = (struct dir_entry *)bp->db_data;
				if (strcmp(ep->de_name, ".") == 0) {
					if (ep->de_instances !=
							NONEXISTENT_BLOCK &&
							unused_instance(
							ep->de_instances,
							&inst_freed) == 0) {
						if (entries)
							bp->db_flags =
								PART_FREE;
						else
							bp->db_flags = 0;
					}
				} else {
					(void) fprintf(stderr, "dotblock!\n");
				}
			}
			if (bp->db_flags)
				dir_dirtyblock(thisblk);
		} else if (keepers != entries) {
			/*
			 * we freed some.
			 */
			bp->db_flags = PART_FREE;  /* unknown amount */
			dir_dirtyblock(thisblk);
		} else {
			/*
			 * nothing to free
			 */
			/*EMPTY*/
		}
		retcnt += keepers;
		thisblk = bp->db_next;
	} while (thisblk != startblk);
	compress_dir(startblk);
	return (retcnt);
}

static void
compress_dir(blk)
	u_long blk;
{
	struct dir_block *bp;
	u_long thisblk, startblk, freeblk;
	int dotblock = 1;

	thisblk = startblk = blk;
	freeblk = NONEXISTENT_BLOCK;
	do {
		if (freeblk == startblk)
			startblk = thisblk;
		if ((bp = dir_getblock(thisblk)) == NULL_DIRBLK) {
			(void) fprintf(stderr,
			    gettext("%s: cannot get blk %lu\n"),
				"compress_dir", blk);
			exit(1);
		}
		if (bp->db_flags == PART_FREE) {
			/*
			 * some but not all entries were freed.
			 * Compress this block, and set db_spaceavail
			 * to reflect the amount of space now available
			 * at the end.
			 */
			compress_block(bp);
			dir_dirtyblock(thisblk);
		} else if (bp->db_flags == ALL_FREE) {
			/*
			 * update space available.
			 */
			bp->db_spaceavail = DIRBLOCK_DATASIZE;
			if (dotblock) {
				/*
				 * spaceavail decreases by the size of the
				 * entries for `.' and `..'
				 */
				bp->db_spaceavail -= DE_NAMELEN(1) +
						sizeof (struct dir_entry);
				bp->db_spaceavail -= DE_NAMELEN(2) +
						sizeof (struct dir_entry);
			}
			dir_dirtyblock(thisblk);
		}
		dotblock = 0;
		/*
		 * check all blocks remaining in the chain looking for
		 * entries which would fit in our free space.  If any
		 * are found, copy them to here...
		 */
		shuffle_blocks(bp, thisblk, startblk);

		freeblk = thisblk;
		thisblk = bp->db_next;
		if (bp->db_flags == ALL_FREE) {
			/*
			 * free this block.  Our predecessors next
			 * pointer must be adjusted appropriately
			 */
			++dirs_freed;
			bp->db_flags = 0;
			dir_freeblk(freeblk);
			if (freeblk == DIR_ROOTBLK) {
				freedroot = 1;
			}
		} else {
			bp->db_flags = 0;
			freeblk = NONEXISTENT_BLOCK;
		}
	} while (thisblk != startblk);
}

/*
 * this block contains empty entries.  Squeeze the block so that all
 * valid entries are at the beginning and all available free space is
 * at the end.
 */
static void
compress_block(bp)
	struct dir_block *bp;
{
	struct dir_entry *ep, *avail, *target, *next;
	int esize;

	/*LINTED [alignment ok]*/
	ep = (struct dir_entry *)bp->db_data;
	avail = get_available(ep, bp);
	if (avail) {
		target = get_valid(avail, bp);
		/*LINTED [alignment ok]*/
		if (!target && (avail == (struct dir_entry *)bp->db_data)) {
			/*
			 * first block entry is available, and no valid
			 * entries were found in the block.  This indicates
			 * a completely empty block.
			 */
			bp->db_flags = ALL_FREE;
			bp->db_spaceavail = DIRBLOCK_DATASIZE;
			return;
		}
	}
	while (avail && target) {
		next = get_valid(DE_NEXT(target), bp);
		esize = ENTRYSIZE(target);
#ifdef USG
		/*
		 * USG memcpy does not handle overlapping
		 * copies correctly, so we must use memmove.
		 * Note that the args are reversed from those
		 * of bcopy.
		 */
		(void) memmove((char *)avail, (char *)target, esize);
#else
		(void) bcopy((char *)target, (char *)avail, esize);
#endif
		/* free_entry(target); */
		/*LINTED [alignment ok]*/
		avail = (struct dir_entry *)((char *)avail + esize);
		target = next;
	}
	if (avail)
		bp->db_spaceavail = (int)bp + DIR_BLKSIZE - (int)avail;
	/*
	 * locate the next free entry. Call it `avail'.
	 * locate the next valid entry after 'avail'.  Call it `target'.
	 * locate the next valid entry after 'target'.  Call it `next'.
	 * Copy the entry at `target' to `available'.
	 * (Since `target' is moving toward lower addresses, we don't
	 * have to worry about over-writing anything that came after
	 * it.  It may, however overwrite its original starting point.
	 * I think bcopy is built to handle this case).
	 * Update `available' so it points just beyond the new copy.
	 * Set `target' equal to `next'.
	 * Set `next' equal to `next.next'
	 */
}

/*
 * move entries (if possible) from succeeding blocks into
 * `curbp'.  Stop checking blocks when we find a next pointer
 * equal to `first'.
 */
static void
shuffle_blocks(curbp, curblk, first)
	struct dir_block *curbp;
	u_long curblk;
	u_long first;
{
	struct dir_block *bp;
	struct dir_entry *ep;
	u_long blk;
	int keepers;

	if (curbp->db_spaceavail < MIN_ENTRYSIZE)
		return;

	blk = curbp->db_next;
	while (blk != first) {
		if ((bp = dir_getblock(blk)) == NULL_DIRBLK) {
			(void) fprintf(stderr, gettext(
				"%s: cannot get %lu\n"), "shuffle_blocks", blk);
			exit(1);
		}
		keepers = 0;
		/*LINTED [alignment ok]*/
		for (ep = (struct dir_entry *)bp->db_data;
		    /*LINTED [alignment ok]*/
		    ep != DE_END(bp); ep = DE_NEXT(ep)) {
			keepers++;
			if (ep->de_name[0] == FREE_ENTRY) {
				keepers--;
			} else if (ENTRYSIZE(ep) < curbp->db_spaceavail) {
				move_entry(ep, curbp);
				bp->db_flags = PART_FREE;
				curbp->db_flags = PART_FREE;
				dir_dirtyblock(curblk);
				dir_dirtyblock(blk);
				keepers--;
			}
		}
		if (keepers == 0) {
			bp->db_flags = ALL_FREE;
			dir_dirtyblock(blk);
		}
		blk = bp->db_next;
		if (curbp->db_spaceavail < MIN_ENTRYSIZE)
			break;
	}
}

static void
dir_freeblk(blk)
	u_long blk;
{
	struct dir_block *bp1, *bp2;
	u_long next;

	if ((bp1 = dir_getblock(blk)) == NULL_DIRBLK) {
		(void) fprintf(stderr, gettext(
			"%s: cannot locate blk %lu\n"), "dir_freeblk", blk);
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
					"dir_freeblk");
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
			"%s: cannot read freelist block\n"), "dir_freeblk");
		exit(1);
	}
	bp1->db_next = bp2->db_next;
	bp2->db_next = blk;
	dir_dirtyblock(blk);
	dir_dirtyblock(DIR_FREEBLK);
}

static void
move_entry(ep, bp)
	struct dir_entry *ep;
	struct dir_block *bp;
{
	struct dir_entry *newep;

	newep = (struct dir_entry *)&bp->db_data[DIRBLOCK_DATASIZE -
		/*LINTED [alignment ok]*/
						bp->db_spaceavail];
	newep->de_name_len = ep->de_name_len;
	newep->de_instances = ep->de_instances;
	newep->de_directory = ep->de_directory;
	(void) strcpy(newep->de_name, ep->de_name);
	bp->db_spaceavail -= (DE_NAMELEN(ep->de_name_len) +
					sizeof (struct dir_entry));

	free_entry(ep);
}

static void
free_entry(ep)
	struct dir_entry *ep;
{
	ep->de_name[0] = FREE_ENTRY;
}

static struct dir_entry *
get_available(ep, bp)
	struct dir_entry *ep;
	struct dir_block *bp;
{
	register struct dir_entry *e;

	/*LINTED [alignment ok]*/
	for (e = ep; e != DE_END(bp); e = DE_NEXT(e)) {
		if (e->de_name[0] == FREE_ENTRY)
			return (e);
	}
	return (NULL_DIRENTRY);
}

static struct dir_entry *
get_valid(ep, bp)
	struct dir_entry *ep;
	struct dir_block *bp;
{
	register struct dir_entry *e;

	/*LINTED [alignment ok]*/
	for (e = ep; e != DE_END(bp); e = DE_NEXT(e)) {
		if (e->de_name_len && e->de_name[0] != FREE_ENTRY)
			return (e);
	}
	return (NULL_DIRENTRY);
}

static struct dir_entry *
get_cache_ent(blknum, offset)
	u_long blknum;
	int offset;
{
	struct dir_block *dbp;
	struct dir_entry *dep;

	dbp = dir_getblock(blknum);
	if (dbp == NULL) {
		(void) fprintf(stderr,
			gettext("cannot get dir block %lu\n"), blknum);
		exit(1);
	}
	/*LINTED [alignment ok]*/
	dep = (struct dir_entry *) ((char *)dbp + offset);
	return (dep);
}
